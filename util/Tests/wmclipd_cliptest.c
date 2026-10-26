/*
 * Test X client for wmclipd selection handling.
 *
 * It can publish deterministic image/png data on PRIMARY and read image/png
 * back from CLIPBOARD, including INCR transfers in both directions. It can
 * also own PRIMARY and serve XA_STRING text, which is what the plain
 * xcb paste clients (wxpaste) request.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <xcb/xcb.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-x11.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "clip_common.h"
#include "xcconn.h"
#include <WINGs/WXCore.h>

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

static int serve_text_mode;

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
	WXSendEvent32(xc, &ev, sizeof(ev), 0, req->requestor, 0);
	xcb_flush(xc);
}

static void serve_request(xcb_selection_request_event_t *req, int use_incr)
{
	xcb_atom_t property = req->property == XCB_ATOM_NONE ?
		req->target : req->property;
	xcb_atom_t targets[2];

	if (req->target == atom_targets) {
		targets[0] = atom_targets;
		targets[1] = serve_text_mode ? XCB_ATOM_STRING : atom_png;
		xcb_change_property(xc, XCB_PROP_MODE_REPLACE, req->requestor,
				    property, XCB_ATOM_ATOM, 32, 2, targets);
		send_selection_notify(req, property);
		return;
	}

	if (serve_text_mode) {
		if (req->target == XCB_ATOM_STRING) {
			xcb_change_property(xc, XCB_PROP_MODE_REPLACE, req->requestor,
					    property, XCB_ATOM_STRING, 8,
					    payload_len, payload);
			send_selection_notify(req, property);
		} else {
			send_selection_notify(req, XCB_ATOM_NONE);
		}
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

static int serve_selection(xcb_atom_t selection, int use_incr, const char *ready)
{
	xcb_generic_event_t *ev;

	xcb_set_selection_owner(xc, win, selection, XCB_TIME_CURRENT_TIME);
	xcb_flush(xc);

	if (selection_owner(selection) != win) {
		fprintf(stderr, "failed to own selection\n");
		return 3;
	}
	if (ready) {
		printf("%s\n", ready);
		fflush(stdout);
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
	return serve_selection(atom_primary, use_incr, NULL);
}

static int serve_clipboard_file(const char *path, int use_incr)
{
	if (!load_payload_file(path))
		return 2;
	return serve_selection(atom_clipboard, use_incr, NULL);
}

static int serve_string(const char *text)
{
	payload_len = strlen(text);
	payload = malloc(payload_len ? payload_len : 1);
	if (!payload) {
		perror("malloc");
		return 2;
	}
	memcpy(payload, text, payload_len);
	serve_text_mode = 1;
	return serve_selection(atom_primary, 0, "owning PRIMARY, serving STRING");
}

static xcb_keycode_t keycode_for_keysym(xcb_connection_t *c, xkb_keysym_t want)
{
	xcb_keycode_t found = 0;
	struct xkb_context *ctx;
	struct xkb_keymap *keymap;
	xkb_keycode_t kc, min_kc, max_kc;
	const xkb_keysym_t *syms;
	int dev, n;

	if (!xkb_x11_setup_xkb_extension(c,
					 XKB_X11_MIN_MAJOR_XKB_VERSION,
					 XKB_X11_MIN_MINOR_XKB_VERSION,
					 XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
					 NULL, NULL, NULL, NULL))
		return 0;
	ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!ctx)
		return 0;
	dev = xkb_x11_get_core_keyboard_device_id(c);
	if (dev < 0) {
		xkb_context_unref(ctx);
		return 0;
	}
	keymap = xkb_x11_keymap_new_from_device(ctx, c, dev,
						XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!keymap) {
		xkb_context_unref(ctx);
		return 0;
	}
	min_kc = xkb_keymap_min_keycode(keymap);
	max_kc = xkb_keymap_max_keycode(keymap);
	for (kc = min_kc; kc <= max_kc; kc++) {
		n = xkb_keymap_key_get_syms_by_level(keymap, kc, 0, 0, &syms);
		if (n > 0 && syms[0] == want) {
			found = (xcb_keycode_t)kc;
			break;
		}
	}
	xkb_keymap_unref(keymap);
	xkb_context_unref(ctx);
	return found;
}

static int send_ctrl_v(const char *window_id)
{
	xcb_keycode_t keycode;
	xcb_key_press_event_t ev;
	xcb_window_t target;

	target = (xcb_window_t)strtoul(window_id, NULL, 0);
	if (target == 0)
		return 2;

	keycode = keycode_for_keysym(xc, XKB_KEY_v);
	if (!keycode)
		return 3;

	memset(&ev, 0, sizeof(ev));
	ev.response_type = XCB_KEY_PRESS;
	ev.detail = keycode;
	ev.root = root_window;
	ev.event = target;
	ev.root_x = 1;
	ev.root_y = 1;
	ev.event_x = 1;
	ev.event_y = 1;
	ev.state = XCB_KEY_BUT_MASK_CONTROL;
	ev.same_screen = 1;
	ev.time = XCB_TIME_CURRENT_TIME;

	xcb_set_input_focus(xc, target, XCB_INPUT_FOCUS_PARENT,
			    XCB_TIME_CURRENT_TIME);
	WXSendEvent32(xc, &ev, sizeof(ev), 0, target, XCB_EVENT_MASK_KEY_PRESS);

	ev.response_type = XCB_KEY_RELEASE;
	WXSendEvent32(xc, &ev, sizeof(ev), 0, target, XCB_EVENT_MASK_KEY_RELEASE);
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
			fprintf(stderr, "timed out waiting for XCB_SELECTION_NOTIFY\n");
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
	xcb_intern_atom_cookie_t cookies[5];
	xcb_intern_atom_reply_t *rep;
	uint32_t mask;
	int screen_num;
	int i = 0;

	xc = xcconn_open(NULL, "wmclipd_cliptest", 0, &screen_num);
	if (!xc)
		return 0;

	root_window = xcconn_root(xc, screen_num);

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
	if (argc >= 2 && strcmp(argv[1], "serve-string") == 0)
		return serve_string(argc >= 3 ? argv[2] : "wmclipd cliptest");
	if (argc >= 3 && strcmp(argv[1], "send-ctrl-v") == 0)
		return send_ctrl_v(argv[2]);
	if (argc >= 2 && strcmp(argv[1], "read-clipboard") == 0)
		return read_clipboard();

	fprintf(stderr, "usage: %s serve-primary-direct|serve-primary-incr|serve-clipboard-file|serve-clipboard-file-incr|serve-string|send-ctrl-v|read-clipboard [arg]\n",
		argv[0]);
	return 2;
}
