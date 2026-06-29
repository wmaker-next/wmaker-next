/*
 * Test X client for wmclipd selection handling.
 *
 * It can publish deterministic image/png data on PRIMARY and read image/png
 * back from CLIPBOARD, including INCR transfers in both directions.
 */

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

#define CHUNK_SIZE 32768
/* same cap the production daemon uses, the test peer is local-trusted but
 * keeping the bound here means the harness exercises the same limits */
#define MAX_CLIP_BYTES ((size_t)256 * 1024 * 1024)

static Display *dpy;
static Window win;
static Atom atom_clipboard;
static Atom atom_primary;
static Atom atom_targets;
static Atom atom_png;
static Atom atom_incr;
static Atom atom_property;

static unsigned char *payload;
static size_t payload_len;

static struct {
	int active;
	Window requestor;
	Atom property;
	size_t offset;
} transfer;

static void send_notify(XSelectionRequestEvent *req, Atom property)
{
	XEvent ev;

	memset(&ev, 0, sizeof(ev));
	ev.xselection.type = SelectionNotify;
	ev.xselection.display = req->display;
	ev.xselection.requestor = req->requestor;
	ev.xselection.selection = req->selection;
	ev.xselection.target = req->target;
	ev.xselection.time = req->time;
	ev.xselection.property = property;
	XSendEvent(dpy, req->requestor, False, 0, &ev);
	XFlush(dpy);
}

static void serve_request(XSelectionRequestEvent *req, int use_incr)
{
	Atom property = req->property == None ? req->target : req->property;

	if (req->target == atom_targets) {
		Atom targets[2];

		targets[0] = atom_targets;
		targets[1] = atom_png;
		XChangeProperty(dpy, req->requestor, property, XA_ATOM, 32,
				PropModeReplace, (unsigned char *)targets, 2);
		send_notify(req, property);
		return;
	}

	if (req->target != atom_png) {
		send_notify(req, None);
		return;
	}

	if (use_incr) {
		unsigned long size = payload_len;

		transfer.active = 1;
		transfer.requestor = req->requestor;
		transfer.property = property;
		transfer.offset = 0;
		XSelectInput(dpy, req->requestor, PropertyChangeMask);
		XChangeProperty(dpy, req->requestor, property, atom_incr, 32,
				PropModeReplace, (unsigned char *)&size, 1);
		send_notify(req, property);
	} else {
		XChangeProperty(dpy, req->requestor, property, atom_png, 8,
				PropModeReplace, payload, (int)payload_len);
		send_notify(req, property);
	}
}

static void send_next_chunk(void)
{
	size_t count;

	if (!transfer.active)
		return;

	if (transfer.offset >= payload_len) {
		XChangeProperty(dpy, transfer.requestor, transfer.property,
				atom_png, 8, PropModeReplace, NULL, 0);
		XFlush(dpy);
		transfer.active = 0;
		return;
	}

	count = payload_len - transfer.offset;
	if (count > CHUNK_SIZE)
		count = CHUNK_SIZE;

	XChangeProperty(dpy, transfer.requestor, transfer.property, atom_png, 8,
			PropModeReplace, payload + transfer.offset, (int)count);
	XFlush(dpy);
	transfer.offset += count;
}

static int serve_selection(Atom selection, int use_incr)
{
	XEvent ev;

	XSetSelectionOwner(dpy, selection, win, CurrentTime);
	XFlush(dpy);
	if (XGetSelectionOwner(dpy, selection) != win) {
		fprintf(stderr, "failed to own selection\n");
		return 3;
	}

	for (;;) {
		XNextEvent(dpy, &ev);
		if (ev.type == SelectionRequest) {
			serve_request(&ev.xselectionrequest, use_incr);
		} else if (ev.type == PropertyNotify &&
			   transfer.active &&
			   ev.xproperty.window == transfer.requestor &&
			   ev.xproperty.atom == transfer.property &&
			   ev.xproperty.state == PropertyDelete) {
			send_next_chunk();
		}
	}
}

static int make_deterministic_payload(size_t bytes)
{
	size_t i;

	payload_len = bytes;
	payload = malloc(payload_len ? payload_len : 1);
	if (!payload) {
		perror("malloc");
		return 0;
	}
	for (i = 0; i < payload_len; i++)
		payload[i] = (unsigned char)(i & 0xff);
	return 1;
}

static int load_payload_file(const char *path)
{
	FILE *fp;
	struct stat st;
	size_t nread;

	if (stat(path, &st) != 0 || st.st_size <= 0) {
		perror("stat");
		return 0;
	}
	payload_len = (size_t)st.st_size;
	payload = malloc(payload_len);
	if (!payload) {
		perror("malloc");
		return 0;
	}
	fp = fopen(path, "rb");
	if (!fp) {
		perror("fopen");
		free(payload);
		payload = NULL;
		payload_len = 0;
		return 0;
	}
	nread = fread(payload, 1, payload_len, fp);
	if (fclose(fp) != 0 || nread != payload_len) {
		fprintf(stderr, "failed to read payload file\n");
		free(payload);
		payload = NULL;
		payload_len = 0;
		return 0;
	}
	return 1;
}

static int serve_primary(int use_incr, size_t bytes)
{
	if (!make_deterministic_payload(bytes))
		return 2;
	return serve_selection(atom_primary, use_incr);
}

static int serve_clipboard_file(const char *path, int use_incr)
{
	if (!load_payload_file(path))
		return 2;
	return serve_selection(atom_clipboard, use_incr);
}

static int wait_for_event(XEvent *ev, int timeout_ms)
{
	struct pollfd pfd;
	int rc;

	while (!XPending(dpy)) {
		pfd.fd = ConnectionNumber(dpy);
		pfd.events = POLLIN;
		pfd.revents = 0;
		rc = poll(&pfd, 1, timeout_ms);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			return 0;
		}
		if (rc == 0)
			return 0;
		if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
			return 0;
	}

	XNextEvent(dpy, ev);
	return 1;
}

static int wait_for_selection_notify(XEvent *ev)
{
	for (;;) {
		if (!wait_for_event(ev, 3000))
			return 0;
		if (ev->type == SelectionNotify)
			return 1;
	}
}

static int read_property_chunk(Atom property, Atom *type_out,
			       unsigned char **data_out, size_t *len_out)
{
	Atom type;
	Atom type2;
	int format;
	int format2;
	unsigned long nitems;
	unsigned long nitems2;
	unsigned long bytes_after;
	unsigned long bytes_after2;
	unsigned char *prop = NULL;
	unsigned char *prop2 = NULL;
	long length;

	if (XGetWindowProperty(dpy, win, property, 0, 0, False,
			       AnyPropertyType, &type, &format, &nitems,
			       &bytes_after, &prop) != Success) {
		return 0;
	}
	if (prop)
		XFree(prop);

	if (format != 8 || bytes_after > MAX_CLIP_BYTES)
		return 0;

	length = (long)((bytes_after + 3) / 4);
	if (length == 0)
		length = 1;

	if (XGetWindowProperty(dpy, win, property, 0, length, True,
			       AnyPropertyType, &type2, &format2, &nitems2,
			       &bytes_after2, &prop2) != Success) {
		return 0;
	}

	if (format2 != 8 || bytes_after2 != 0) {
		if (prop2)
			XFree(prop2);
		return 0;
	}

	*type_out = type2;
	*data_out = prop2;
	*len_out = (size_t)nitems2;
	return 1;
}

static int send_ctrl_v(const char *window_id)
{
	XKeyEvent ev;
	Window target;

	target = (Window)strtoul(window_id, NULL, 0);
	if (target == None)
		return 2;

	memset(&ev, 0, sizeof(ev));
	ev.display = dpy;
	ev.window = target;
	ev.root = DefaultRootWindow(dpy);
	ev.subwindow = None;
	ev.time = CurrentTime;
	ev.x = 1;
	ev.y = 1;
	ev.x_root = 1;
	ev.y_root = 1;
	ev.same_screen = True;
	ev.keycode = XKeysymToKeycode(dpy, XK_v);
	ev.state = ControlMask;
	XSetInputFocus(dpy, target, RevertToParent, CurrentTime);
	ev.type = KeyPress;
	if (!XSendEvent(dpy, target, False, KeyPressMask, (XEvent *)&ev))
		return 3;
	ev.type = KeyRelease;
	XSendEvent(dpy, target, False, KeyReleaseMask, (XEvent *)&ev);
	XFlush(dpy);
	return 0;
}

static int read_clipboard(void)
{
	XEvent ev;
	Atom type;
	int format;
	unsigned long nitems;
	unsigned long bytes_after;
	unsigned char *prop = NULL;
	unsigned char *data = NULL;
	unsigned char *chunk = NULL;
	int data_from_x = 0;
	size_t data_len = 0;
	size_t chunk_len;
	unsigned long checksum = 0;
	long length;
	size_t i;

	XDeleteProperty(dpy, win, atom_property);
	XConvertSelection(dpy, atom_clipboard, atom_png, atom_property, win,
			  CurrentTime);
	XFlush(dpy);

	if (!wait_for_selection_notify(&ev) || ev.xselection.property == None) {
		fprintf(stderr, "selection denied\n");
		return 4;
	}

	if (XGetWindowProperty(dpy, win, atom_property, 0, 0, False,
			       AnyPropertyType, &type, &format, &nitems,
			       &bytes_after, &prop) != Success) {
		return 5;
	}
	if (prop)
		XFree(prop);

	if (type == atom_incr) {
		XDeleteProperty(dpy, win, atom_property);
		XFlush(dpy);

		for (;;) {
			if (!wait_for_event(&ev, 3000))
				return 6;
			if (ev.type != PropertyNotify ||
			    ev.xproperty.atom != atom_property ||
			    ev.xproperty.state != PropertyNewValue) {
				continue;
			}
			if (!read_property_chunk(atom_property, &type, &chunk,
						 &chunk_len)) {
				return 7;
			}
			if (chunk_len == 0) {
				XFree(chunk);
				break;
			}
			if (chunk_len > MAX_CLIP_BYTES ||
			    data_len > MAX_CLIP_BYTES - chunk_len) {
				XFree(chunk);
				free(data);
				return 8;
			}
			data = realloc(data, data_len + chunk_len);
			if (!data) {
				perror("realloc");
				XFree(chunk);
				return 8;
			}
			memcpy(data + data_len, chunk, chunk_len);
			data_len += chunk_len;
			XFree(chunk);
		}
	} else {
		if (bytes_after > MAX_CLIP_BYTES)
			return 9;
		length = (long)((bytes_after + 3) / 4);
		if (length == 0)
			length = 1;
		if (XGetWindowProperty(dpy, win, atom_property, 0, length,
				       True, AnyPropertyType, &type, &format,
				       &nitems, &bytes_after, &data) != Success) {
			return 9;
		}
		if (format != 8 || bytes_after != 0) {
			if (data)
				XFree(data);
			return 10;
		}
		data_from_x = 1;
		data_len = (size_t)nitems;
	}

	for (i = 0; i < data_len; i++)
		checksum = (checksum + data[i]) & 0xffffffffUL;
	printf("%zu %lu\n", data_len, checksum);

	if (data && data_from_x)
		XFree(data);
	else
		free(data);
	return 0;
}

static int init_x11(void)
{
	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "cannot open display\n");
		return 0;
	}

	atom_clipboard = XInternAtom(dpy, "CLIPBOARD", False);
	atom_primary = XA_PRIMARY;
	atom_targets = XInternAtom(dpy, "TARGETS", False);
	atom_png = XInternAtom(dpy, "image/png", False);
	atom_incr = XInternAtom(dpy, "INCR", False);
	atom_property = XInternAtom(dpy, "WMCLIPD_TEST", False);

	win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 1, 1,
				  0, 0, 0);
	XSelectInput(dpy, win, PropertyChangeMask);
	XFlush(dpy);
	return 1;
}

int main(int argc, char **argv)
{
	if (!init_x11())
		return 1;

	if (argc >= 2 && strcmp(argv[1], "serve-primary-direct") == 0)
		return serve_primary(0, argc >= 3 ? strtoul(argv[2], NULL, 10) : 4096);
	if (argc >= 2 && strcmp(argv[1], "serve-primary-incr") == 0)
		return serve_primary(1, argc >= 3 ? strtoul(argv[2], NULL, 10) : 131072);
	if (argc >= 3 && strcmp(argv[1], "serve-clipboard-file") == 0)
		return serve_clipboard_file(argv[2], 0);
	if (argc >= 3 && strcmp(argv[1], "serve-clipboard-file-incr") == 0)
		return serve_clipboard_file(argv[2], 1);
	if (argc >= 3 && strcmp(argv[1], "send-ctrl-v") == 0)
		return send_ctrl_v(argv[2]);
	if (argc >= 2 && strcmp(argv[1], "read-clipboard") == 0)
		return read_clipboard();

	fprintf(stderr, "usage: %s serve-primary-direct|serve-primary-incr|serve-clipboard-file|serve-clipboard-file-incr|send-ctrl-v|read-clipboard [arg]\n",
		argv[0]);
	return 2;
}
