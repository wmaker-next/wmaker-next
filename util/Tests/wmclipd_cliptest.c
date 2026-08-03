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

#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <X11/keysym.h>

#include "clip-common.h"

#define CHUNK_SIZE 32768

static xcb_connection_t *xc;
static xcb_window_t win;
static xcb_window_t root_window;
static xcb_atom_t atom_clipboard;
static xcb_atom_t atom_primary;
static xcb_atom_t atom_targets;
static xcb_atom_t atom_png;
static xcb_atom_t atom_incr;
static xcb_atom_t atom_property;

static unsigned char *payload;
static size_t payload_len;

static ClipIncrSend transfer;

static void send_selection_notify(xcb_selection_request_event_t *req,
				  xcb_atom_t property)
{
	xcb_selection_notify_event_t ev;

	memset(&ev, 0, sizeof(ev));
	ev.response_type = XCB_SELECTION_NOTIFY;
	ev.time = req->time;
	ev.requestor = req->requestor;
	ev.selection = req->selection;
	ev.target = req->target;
	ev.property = property;
	xcb_send_event(xc, 0, req->requestor, 0, (const char *)&ev);
	xcb_flush(xc);
}

static void serve_request(xcb_selection_request_event_t *req, int use_incr)
{
	xcb_atom_t property = req->property == XCB_ATOM_NONE ?
		req->target : req->property;
	xcb_atom_t targets[2];

	if (req->target == atom_targets) {
		targets[0] = atom_targets;
		targets[1] = atom_png;
		xcb_change_property(xc, XCB_PROP_MODE_REPLACE, req->requestor,
				    property, XCB_ATOM_ATOM, 32, 2, targets);
		send_selection_notify(req, property);
		return;
	}

	if (req->target != atom_png) {
		send_selection_notify(req, XCB_ATOM_NONE);
		return;
	}

	if (use_incr) {
		if (!clip_incr_send_start(xc, &transfer, req, atom_incr,
					   atom_png, payload, payload_len,
					   CHUNK_SIZE)) {
			send_selection_notify(req, XCB_ATOM_NONE);
			return;
		}
		send_selection_notify(req, property);
	} else {
		xcb_change_property(xc, XCB_PROP_MODE_REPLACE, req->requestor,
				    property, atom_png, 8, payload_len,
				    payload);
		send_selection_notify(req, property);
	}
}

static xcb_window_t selection_owner(xcb_atom_t selection)
{
	xcb_get_selection_owner_cookie_t ck;
	xcb_get_selection_owner_reply_t *rep;
	xcb_window_t owner;

	ck = xcb_get_selection_owner(xc, selection);
	rep = xcb_get_selection_owner_reply(xc, ck, NULL);

	owner = rep ? rep->owner : 0;
	free(rep);
	return owner;
}

static int serve_selection(xcb_atom_t selection, int use_incr)
{
	xcb_generic_event_t *ev;

	xcb_set_selection_owner(xc, win, selection, XCB_TIME_CURRENT_TIME);
	xcb_flush(xc);

	if (selection_owner(selection) != win) {
		fprintf(stderr, "failed to own selection\n");
		return 3;
	}

	for (;;) {
		ev = xcb_wait_for_event(xc);

		if (!ev)
			return 4;
		uint8_t rt = ev->response_type & 0x7f;

		if (rt == XCB_SELECTION_REQUEST)
			serve_request((xcb_selection_request_event_t *)ev,
				      use_incr);
		else if (rt == XCB_PROPERTY_NOTIFY)
			clip_incr_send_property_notify(xc, &transfer,
				(xcb_property_notify_event_t *)ev);
		else if (rt == XCB_DESTROY_NOTIFY)
			clip_incr_send_destroy_notify(xc, &transfer,
				(xcb_destroy_notify_event_t *)ev);
		free(ev);
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
	xcb_key_symbols_t *syms;
	xcb_keycode_t *keycodes;
	xcb_key_press_event_t ev;
	xcb_window_t target;

	target = (xcb_window_t)strtoul(window_id, NULL, 0);
	if (target == 0)
		return 2;

	syms = xcb_key_symbols_alloc(xc);

	if (!syms)
		return 3;
	keycodes = xcb_key_symbols_get_keycode(syms, XK_v);
	xcb_key_symbols_free(syms);

	if (!keycodes)
		return 3;

	memset(&ev, 0, sizeof(ev));
	ev.response_type = XCB_KEY_PRESS;
	ev.detail = keycodes[0];
	ev.root = root_window;
	ev.event = target;
	ev.root_x = 1;
	ev.root_y = 1;
	ev.event_x = 1;
	ev.event_y = 1;
	ev.state = XCB_KEY_BUT_MASK_CONTROL;
	ev.same_screen = 1;
	ev.time = XCB_TIME_CURRENT_TIME;

	free(keycodes);

	xcb_set_input_focus(xc, target, XCB_INPUT_FOCUS_PARENT,
			    XCB_TIME_CURRENT_TIME);
	xcb_send_event(xc, 0, target, XCB_EVENT_MASK_KEY_PRESS,
		       (const char *)&ev);

	ev.response_type = XCB_KEY_RELEASE;
	xcb_send_event(xc, 0, target, XCB_EVENT_MASK_KEY_RELEASE,
		       (const char *)&ev);
	xcb_flush(xc);

	return 0;
}

static int read_clipboard(void)
{
	xcb_generic_event_t *ev;
	xcb_atom_t type;
	xcb_selection_notify_event_t *sn;
	unsigned char *data;
	size_t data_len, i;
	unsigned long checksum = 0;
	long long deadline;

	xcb_delete_property(xc, win, atom_property);
	xcb_convert_selection(xc, win, atom_clipboard, atom_png, atom_property,
			      XCB_TIME_CURRENT_TIME);
	xcb_flush(xc);

	deadline = clip_monotonic_ms() + 3000;
	for (;;) {
		int wait_ms = (int)(deadline - clip_monotonic_ms());

		if (wait_ms < 0)
			wait_ms = 0;
		if (!clip_wait_event(xc, &ev, wait_ms)) {
			fprintf(stderr, "timed out waiting for SelectionNotify\n");
			return 4;
		}
		if ((ev->response_type & 0x7f) == XCB_SELECTION_NOTIFY)
			break;
		free(ev);
	}

	sn = (xcb_selection_notify_event_t *)ev;
	if (sn->property == XCB_ATOM_NONE) {
		fprintf(stderr, "selection denied\n");
		free(ev);
		return 4;
	}
	free(ev);

	data = clip_retrieve_property(xc, win, atom_property, atom_incr,
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
	int screen_num;
	xcb_screen_iterator_t iter;
	xcb_screen_t *screen;
	xcb_intern_atom_cookie_t cookies[5];
	xcb_intern_atom_reply_t *rep;
	uint32_t mask;
	int i = 0;

	xc = xcb_connect(NULL, &screen_num);

	if (xcb_connection_has_error(xc)) {
		fprintf(stderr, "cannot open display\n");
		xcb_disconnect(xc);

		xc = NULL;
		return 0;
	}

	iter = xcb_setup_roots_iterator(xcb_get_setup(xc));

	for (; screen_num > 0; screen_num--)
		xcb_screen_next(&iter);

	screen = iter.data;
	root_window = screen->root;

	cookies[i++] = xcb_intern_atom(xc, 0, strlen("CLIPBOARD"), "CLIPBOARD");
	cookies[i++] = xcb_intern_atom(xc, 0, strlen("TARGETS"), "TARGETS");
	cookies[i++] = xcb_intern_atom(xc, 0, strlen("image/png"), "image/png");
	cookies[i++] = xcb_intern_atom(xc, 0, strlen("INCR"), "INCR");
	cookies[i++] = xcb_intern_atom(xc, 0, strlen("WMCLIPD_TEST"), "WMCLIPD_TEST");

	i = 0;
	rep = xcb_intern_atom_reply(xc, cookies[i++], NULL);
	if (!rep) goto fail;
	atom_clipboard = rep->atom; free(rep);
	atom_primary = XCB_ATOM_PRIMARY;
	rep = xcb_intern_atom_reply(xc, cookies[i++], NULL);
	if (!rep) goto fail;
	atom_targets = rep->atom; free(rep);
	rep = xcb_intern_atom_reply(xc, cookies[i++], NULL);
	if (!rep) goto fail;
	atom_png = rep->atom; free(rep);
	rep = xcb_intern_atom_reply(xc, cookies[i++], NULL);
	if (!rep) goto fail;
	atom_incr = rep->atom; free(rep);
	rep = xcb_intern_atom_reply(xc, cookies[i++], NULL);
	if (!rep) goto fail;
	atom_property = rep->atom; free(rep);

	win = xcb_generate_id(xc);
	xcb_create_window(xc, XCB_COPY_FROM_PARENT, win, root_window,
			  0, 0, 1, 1, 0, XCB_WINDOW_CLASS_COPY_FROM_PARENT,
			  XCB_COPY_FROM_PARENT, 0, NULL);
	mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
	xcb_change_window_attributes(xc, win, XCB_CW_EVENT_MASK, &mask);
	xcb_flush(xc);

	return 1;
fail:
	fprintf(stderr, "wmclipd_cliptest: intern_atom reply failed\n");
	return 0;
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
