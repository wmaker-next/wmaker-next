/*
 * Test X client for wmclipd selection handling.
 *
 * It can publish deterministic image/png data on PRIMARY and read image/png
 * back from CLIPBOARD, including INCR transfers in both directions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <WINGs/WXProp.h>

#include "clip-common.h"

#define CHUNK_SIZE 32768

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

static ClipIncrSend transfer;

static void serve_request(XSelectionRequestEvent *req, int use_incr)
{
	Atom property = req->property == None ? req->target : req->property;
	Atom targets[2];

	if (req->target == atom_targets) {
		targets[0] = atom_targets;
		targets[1] = atom_png;
		XChangeProperty(dpy, req->requestor, property, XA_ATOM, 32,
				PropModeReplace, (unsigned char *)targets, 2);
		WXSendSelectionNotify(dpy, req, property);
		return;
	}

	if (req->target != atom_png) {
		WXSendSelectionNotify(dpy, req, None);
		return;
	}

	if (use_incr) {
		if (!clip_incr_send_start(dpy, &transfer, req, atom_incr,
					   atom_png, payload, payload_len,
					   CHUNK_SIZE)) {
			WXSendSelectionNotify(dpy, req, None);
			return;
		}
		WXSendSelectionNotify(dpy, req, property);
	} else {
		XChangeProperty(dpy, req->requestor, property, atom_png, 8,
				PropModeReplace, payload, (int)payload_len);
		WXSendSelectionNotify(dpy, req, property);
	}
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
		if (ev.type == SelectionRequest)
			serve_request(&ev.xselectionrequest, use_incr);
		else if (ev.type == PropertyNotify)
			clip_incr_send_property_notify(dpy, &transfer, &ev.xproperty);
		else if (ev.type == DestroyNotify)
			clip_incr_send_destroy_notify(dpy, &transfer, &ev.xdestroywindow);
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
	unsigned char *data;
	size_t data_len, i;
	unsigned long checksum = 0;
	long long deadline;

	XDeleteProperty(dpy, win, atom_property);
	XConvertSelection(dpy, atom_clipboard, atom_png, atom_property, win,
			  CurrentTime);
	XFlush(dpy);

	deadline = clip_monotonic_ms() + 3000;
	for (;;) {
		if (!clip_wait_event(dpy, &ev, (int)(deadline - clip_monotonic_ms()))) {
			fprintf(stderr, "timed out waiting for SelectionNotify\n");
			return 4;
		}
		if (ev.type == SelectionNotify)
			break;
	}
	if (ev.xselection.property == None) {
		fprintf(stderr, "selection denied\n");
		return 4;
	}

	data = clip_retrieve_property(dpy, win, atom_property, atom_incr,
					CLIP_DEFAULT_MAX_BYTES, 3000,
					NULL, NULL, &type, &data_len);
	if (!data) {
		fprintf(stderr, "failed to read selection property\n");
		return 5;
	}

	for (i = 0; i < data_len; i++)
		checksum = (checksum + data[i]) & 0xffffffffUL;
	printf("%zu %lu\n", data_len, checksum);

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
