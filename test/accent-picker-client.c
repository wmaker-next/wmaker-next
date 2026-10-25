/* accent-picker-client.c - focusable XKB client that prints typed text */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>

#include <xcb/xcb.h>
#include <xcb/xkb.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-x11.h>

static struct xkb_context *ctx;
static struct xkb_keymap *keymap;
static struct xkb_state *state;
static int32_t device_id;
static xcb_connection_t *conn;
static unsigned char pressed[32];

static void reload_keymap(void)
{
	struct xkb_keymap *new_keymap;
	struct xkb_state *new_state;
	int i;

	new_keymap = xkb_x11_keymap_new_from_device(ctx, conn, device_id,
						    XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!new_keymap)
		return;
	new_state = xkb_x11_state_new_from_device(new_keymap, conn, device_id);
	if (!new_state) {
		xkb_keymap_unref(new_keymap);
		return;
	}
	xkb_state_unref(state);
	xkb_keymap_unref(keymap);
	keymap = new_keymap;
	state = new_state;

	for (i = 0; i < 256; i++)
		if (pressed[i >> 3] & (1 << (i & 7)))
			xkb_state_update_key(state, (xkb_keycode_t)i, XKB_KEY_DOWN);
}

static void note_key(xcb_keycode_t kc, int down)
{
	if (down)
		pressed[kc >> 3] |= (unsigned char)(1 << (kc & 7));
	else
		pressed[kc >> 3] &= (unsigned char)~(1 << (kc & 7));
}

static void handle_key(xcb_keycode_t kc, int down)
{
	char buf[64];
	xkb_keysym_t ks;

	note_key(kc, down);
	xkb_state_update_key(state, kc, down ? XKB_KEY_DOWN : XKB_KEY_UP);
	if (!down)
		return;
	ks = xkb_state_key_get_one_sym(state, kc);
	if (ks == XKB_KEY_NoSymbol)
		return;
	if (xkb_keysym_to_utf8(ks, buf, sizeof(buf)) <= 0)
		return;
	if ((unsigned char)buf[0] < 0x20 || (unsigned char)buf[0] == 0x7f)
		return;
	printf("%s\n", buf);
	fflush(stdout);
}

int main(int argc, char **argv)
{
	xcb_screen_t *screen;
	xcb_window_t win;
	uint32_t mask;
	xcb_generic_event_t *ev;
	int screen_num = 0;
	int seconds = 60;
	int done = 0;
	uint8_t event_base = 0, error_base = 0;
	uint16_t want;
	uint16_t parts;

	if (argc > 1)
		seconds = atoi(argv[1]);
	if (seconds <= 0)
		seconds = 60;

	conn = xcb_connect(NULL, &screen_num);
	if (xcb_connection_has_error(conn))
		return 1;
	if (!xkb_x11_setup_xkb_extension(conn, 1, 0,
			XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
			NULL, NULL, &event_base, &error_base))
		return 1;
	device_id = xkb_x11_get_core_keyboard_device_id(conn);
	if (device_id < 0)
		return 1;
	ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!ctx)
		return 1;
	keymap = xkb_x11_keymap_new_from_device(ctx, conn, device_id,
						XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!keymap)
		return 1;
	state = xkb_x11_state_new_from_device(keymap, conn, device_id);
	if (!state)
		return 1;

	want = XCB_XKB_EVENT_TYPE_NEW_KEYBOARD_NOTIFY | XCB_XKB_EVENT_TYPE_MAP_NOTIFY;
	parts = XCB_XKB_MAP_PART_KEY_TYPES | XCB_XKB_MAP_PART_KEY_SYMS |
		XCB_XKB_MAP_PART_MODIFIER_MAP;
	xcb_xkb_select_events(conn, device_id, want, 0, want, parts, parts, NULL);

	screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
	win = xcb_generate_id(conn);
	mask = XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
	       XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY |
	       XCB_EVENT_MASK_FOCUS_CHANGE;
	xcb_create_window(conn, XCB_COPY_FROM_PARENT, win, screen->root,
			  100, 100, 400, 200, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
			  screen->root_visual, XCB_CW_EVENT_MASK, &mask);
	{
		static const char name[] = "AccentPickerClient";
		static const char cls[] = "AccentPickerClient\0AccentPickerClient";

		xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win,
				    XCB_ATOM_WM_NAME, XCB_ATOM_STRING,
				    8, (uint32_t)(sizeof(name) - 1), name);
		xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win,
				    XCB_ATOM_WM_CLASS, XCB_ATOM_STRING,
				    8, (uint32_t)sizeof(cls) - 1, cls);
	}
	xcb_map_window(conn, win);
	xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, win,
			    XCB_CURRENT_TIME);
	xcb_flush(conn);
	printf("ready\n");
	fflush(stdout);

	while (!done) {
		struct pollfd pfd;

		pfd.fd = xcb_get_file_descriptor(conn);
		pfd.events = POLLIN;
		pfd.revents = 0;
		if (poll(&pfd, 1, seconds * 1000) <= 0)
			break;
		while ((ev = xcb_poll_for_event(conn)) != NULL) {
			uint8_t type = ev->response_type & 0x7f;

			if (type == event_base) {
				const xcb_xkb_new_keyboard_notify_event_t *xev =
					(const xcb_xkb_new_keyboard_notify_event_t *)ev;

				if (xev->xkbType == XCB_XKB_NEW_KEYBOARD_NOTIFY ||
				    xev->xkbType == XCB_XKB_MAP_NOTIFY)
					reload_keymap();
			} else if (type == XCB_MAPPING_NOTIFY) {
				reload_keymap();
			} else if (type == XCB_KEY_PRESS) {
				handle_key(((xcb_key_press_event_t *)ev)->detail, 1);
			} else if (type == XCB_KEY_RELEASE) {
				handle_key(((xcb_key_release_event_t *)ev)->detail, 0);
			}
			free(ev);
		}
	}

	xcb_destroy_window(conn, win);
	xkb_state_unref(state);
	xkb_keymap_unref(keymap);
	xkb_context_unref(ctx);
	xcb_disconnect(conn);
	return 0;
}
