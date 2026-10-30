/*
 *  wtest.c
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1998 scottc
 *  Copyright (c) 2004-2005 Dan Pascu
 *  Copyright (c) 2009-2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/* wtest - wmlib smoke test
 *
 * Builds a menu tree, publishes it through WMRealizeMenus() and checks
 * the _WINDOWMAKER_MENU text property against the wire format the WM
 * parser in src/appmenu.c consumes. Then it synthesizes the
 * wmSelectItem ClientMessage the WM sends back and checks WMProcessEvent
 * dispatches it to the right entry. It also round-trips the packed
 * _GNUSTEP_WM_ATTR format-32 property.
 *
 * Needs an X display; built on demand with `make -C test wtest` and run
 * against the Xvfb driver session.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <xcb/xcb.h>

#include <WMaker.h>

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while (0)

static int cb_calls;
static int cb_tag = -1;
static xcb_timestamp_t cb_time;

static void itemCallback(void *clientdata, int code, xcb_timestamp_t timestamp)
{
	(void)clientdata;
	cb_calls++;
	cb_tag = code;
	cb_time = timestamp;
}

static xcb_atom_t intern(xcb_connection_t *conn, const char *name)
{
	xcb_intern_atom_cookie_t cookie = xcb_intern_atom(conn, 0, strlen(name), name);
	xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(conn, cookie, NULL);
	xcb_atom_t atom = XCB_ATOM_NONE;

	if (reply) {
		atom = reply->atom;
		free(reply);
	}
	return atom;
}

static int checkMenuProperty(xcb_connection_t *conn, xcb_window_t win)
{
	static const char *const expected[] = {
		"WMMenu 0",
		"1 100 Test Menu",
		"12 100 102 1 101 File",
		"1 101 File",
		"10 101 105 1 New",
		"11 101 106 1 o Open",
		"2 101",
		"10 100 103 1 Hide",
		"10 100 104 1 Quit",
		"2 100"
	};
	const int expected_count = (int)(sizeof(expected) / sizeof(expected[0]));
	xcb_atom_t atom = intern(conn, "_WINDOWMAKER_MENU");
	xcb_get_property_cookie_t cookie;
	xcb_get_property_reply_t *reply;
	uint8_t *value;
	int length, count = 0;
	char *p, *end;

	cookie = xcb_get_property(conn, 0, win, atom, XCB_GET_PROPERTY_TYPE_ANY, 0, 0x100000);
	reply = xcb_get_property_reply(conn, cookie, NULL);
	if (!reply) {
		fprintf(stderr, "FAIL: no reply for _WINDOWMAKER_MENU\n");
		failures++;
		return -1;
	}
	if (reply->type != XCB_ATOM_STRING || reply->format != 8) {
		fprintf(stderr, "FAIL: menu property type/format %u/%u\n",
			(unsigned int)reply->type, (unsigned int)reply->format);
		failures++;
		free(reply);
		return -1;
	}
	value = xcb_get_property_value(reply);
	length = xcb_get_property_value_length(reply);

	p = (char *)value;
	end = p + length;
	while (p < end && count < expected_count) {
		if (strcmp(p, expected[count]) != 0) {
			fprintf(stderr, "FAIL: menu[%d] = \"%s\", expected \"%s\"\n",
				count, p, expected[count]);
			failures++;
		}
		count++;
		p += strlen(p) + 1;
	}
	if (count != expected_count) {
		fprintf(stderr, "FAIL: menu has %d entries, expected %d\n", count, expected_count);
		failures++;
	}
	free(reply);
	return count;
}

static void checkSetWindowAttributes(xcb_connection_t *conn, xcb_window_t win)
{
	GNUstepWMAttributes attr;
	xcb_atom_t atom = intern(conn, "_GNUSTEP_WM_ATTR");
	xcb_get_property_cookie_t cookie;
	xcb_get_property_reply_t *reply;
	uint32_t *words;

	memset(&attr, 0, sizeof(attr));
	attr.flags = GSWindowStyleAttr | GSWindowLevelAttr;
	attr.window_style = WMTitledWindowMask | WMClosableWindowMask;
	attr.window_level = WMNormalWindowLevel;
	attr.miniaturize_pixmap = 0xabc;
	attr.close_pixmap = 0xdef;
	attr.miniaturize_mask = 0x123;
	attr.close_mask = 0x456;
	attr.extra_flags = 0x42;

	WMSetWindowAttributes(conn, win, &attr);

	cookie = xcb_get_property(conn, 0, win, atom, XCB_GET_PROPERTY_TYPE_ANY, 0, 16);
	reply = xcb_get_property_reply(conn, cookie, NULL);
	if (!reply) {
		fprintf(stderr, "FAIL: no reply for _GNUSTEP_WM_ATTR\n");
		failures++;
		return;
	}
	if (reply->type != atom || reply->format != 32
	    || xcb_get_property_value_length(reply) != 9 * 4) {
		fprintf(stderr, "FAIL: attribute property type/format/length %u/%u/%d\n",
			(unsigned int)reply->type, (unsigned int)reply->format,
			xcb_get_property_value_length(reply));
		failures++;
		free(reply);
		return;
	}
	words = xcb_get_property_value(reply);
	CHECK(words[0] == (GSWindowStyleAttr | GSWindowLevelAttr));
	CHECK(words[1] == (WMTitledWindowMask | WMClosableWindowMask));
	CHECK(words[2] == (uint32_t)WMNormalWindowLevel);
	CHECK(words[3] == 0);
	CHECK(words[4] == 0xabc);
	CHECK(words[5] == 0xdef);
	CHECK(words[6] == 0x123);
	CHECK(words[7] == 0x456);
	CHECK(words[8] == 0x42);
	free(reply);
}

int main(void)
{
	xcb_connection_t *conn;
	xcb_screen_t *screen;
	xcb_window_t leader;
	xcb_atom_t menu_atom;
	xcb_client_message_event_t event;
	WMAppContext *app;
	WMMenu *menu, *submenu;
	int scr;

	conn = xcb_connect(NULL, &scr);
	if (xcb_connection_has_error(conn)) {
		fprintf(stderr, "wtest: cannot connect to display, skipping\n");
		xcb_disconnect(conn);
		return 0;
	}
	screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
	menu_atom = intern(conn, "_WINDOWMAKER_MENU");

	leader = xcb_generate_id(conn);
	xcb_create_window(conn, XCB_COPY_FROM_PARENT, leader, screen->root,
			  0, 0, 10, 10, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
			  screen->root_visual, 0, NULL);

	app = WMAppCreateWithMain(conn, scr, leader);
	CHECK(app != NULL);

	menu = WMMenuCreate(app, "Test Menu");
	submenu = WMMenuCreate(app, "File");
	WMMenuAddSubmenu(menu, "File", submenu);
	WMMenuAddItem(menu, "Hide", itemCallback, NULL, NULL, NULL);
	WMMenuAddItem(menu, "Quit", itemCallback, NULL, NULL, NULL);
	WMMenuAddItem(submenu, "New", itemCallback, NULL, NULL, NULL);
	WMMenuAddItem(submenu, "Open", itemCallback, NULL, NULL, "o");
	WMAppSetMainMenu(app, menu);
	CHECK(WMRealizeMenus(app) != 0);

	checkMenuProperty(conn, leader);

	/* now dispatch a synthetic selection back through the context */
	memset(&event, 0, sizeof(event));
	event.response_type = XCB_CLIENT_MESSAGE;
	event.format = 32;
	event.window = leader;
	event.type = menu_atom;
	event.data.data32[0] = 0x1234;
	event.data.data32[1] = 1;
	event.data.data32[2] = 103;

	cb_calls = 0;
	cb_tag = -1;
	CHECK(WMProcessEvent(app, (xcb_generic_event_t *)&event) != 0);
	CHECK(cb_calls == 1);
	CHECK(cb_tag == 103);
	CHECK(cb_time == 0x1234);

	/* wrong format must not dispatch */
	event.format = 8;
	CHECK(WMProcessEvent(app, (xcb_generic_event_t *)&event) == 0);
	event.format = 32;

	/* wrong message type must not dispatch */
	event.type = XCB_ATOM_NONE;
	CHECK(WMProcessEvent(app, (xcb_generic_event_t *)&event) == 0);

	checkSetWindowAttributes(conn, leader);

	xcb_destroy_window(conn, leader);
	xcb_disconnect(conn);

	if (failures) {
		fprintf(stderr, "wtest: %d failure(s)\n", failures);
		return 1;
	}
	printf("wtest: ok\n");
	return 0;
}
