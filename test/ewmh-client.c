/*
 *  ewmh-client.c
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 2026 Window Maker Team
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

/* Minimal deterministic X11 client for test/ewmh-layout-drive.sh. */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static Atom intern_atom(Display *dpy, const char *name)
{
	return XInternAtom(dpy, name, False);
}

static void set_utf8(Display *dpy, Window win, Atom prop, const char *text)
{
	Atom utf8 = intern_atom(dpy, "UTF8_STRING");

	XChangeProperty(dpy, win, prop, utf8, 8, PropModeReplace,
			(const unsigned char *)text, (int)strlen(text));
}

static void set_cardinal(Display *dpy, Window win, Atom prop, unsigned long value)
{
	long data[1];

	data[0] = (long)value;
	XChangeProperty(dpy, win, prop, XA_CARDINAL, 32, PropModeReplace,
			(const unsigned char *)data, 1);
}

static void set_window(Display *dpy, Window win, Atom prop, Window value)
{
	long data[1];

	data[0] = (long)value;
	XChangeProperty(dpy, win, prop, XA_WINDOW, 32, PropModeReplace,
			(const unsigned char *)data, 1);
}

static void set_cardinals(Display *dpy, Window win, Atom prop,
			  const long *data, int n)
{
	XChangeProperty(dpy, win, prop, XA_CARDINAL, 32, PropModeReplace,
			(const unsigned char *)data, n);
}

static long env_long(const char *name, long fallback)
{
	const char *s = getenv(name);
	char *end;
	long v;

	if (!s || !*s)
		return fallback;
	v = strtol(s, &end, 0);
	return (*end == '\0') ? v : fallback;
}

static Window env_window(const char *name, Window fallback)
{
	const char *s = getenv(name);
	char *end;
	unsigned long v;

	if (!s || !*s)
		return fallback;
	v = strtoul(s, &end, 0);
	return (*end == '\0') ? (Window)v : fallback;
}

static void send_root_message(Display *dpy, Window win, Atom message,
			      long d0, long d1, long d2, long d3, long d4)
{
	XEvent ev;
	Window root = DefaultRootWindow(dpy);

	memset(&ev, 0, sizeof(ev));
	ev.xclient.type = ClientMessage;
	ev.xclient.window = win;
	ev.xclient.message_type = message;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = d0;
	ev.xclient.data.l[1] = d1;
	ev.xclient.data.l[2] = d2;
	ev.xclient.data.l[3] = d3;
	ev.xclient.data.l[4] = d4;
	XSendEvent(dpy, root, False,
		   SubstructureRedirectMask | SubstructureNotifyMask, &ev);
	XFlush(dpy);
}

static void set_client_machine(Display *dpy, Window win)
{
	char hostname[256];
	char *list[1];
	XTextProperty text;

	if (gethostname(hostname, sizeof(hostname)) != 0)
		snprintf(hostname, sizeof(hostname), "localhost");
	hostname[sizeof(hostname) - 1] = '\0';
	list[0] = hostname;
	if (XStringListToTextProperty(list, 1, &text) != 0) {
		XSetWMClientMachine(dpy, win, &text);
		XFree(text.value);
	}
}

static void send_ping_reply(Display *dpy, XClientMessageEvent *msg)
{
	XEvent reply;
	Window root = DefaultRootWindow(dpy);

	memset(&reply, 0, sizeof(reply));
	reply.xclient = *msg;
	reply.xclient.window = root;
	XSendEvent(dpy, root, False,
		   SubstructureRedirectMask | SubstructureNotifyMask, &reply);
	XFlush(dpy);
}

/* Broadcast a startup-notification message to the root window the way
 * libstartup-notification does, the spec sends the key=value text 20 bytes
 * at a time in xclient.data.b, the first packet uses _NET_STARTUP_INFO_BEGIN
 * and continuations use _NET_STARTUP_INFO. The trailing NUL must be inside
 * a packet for libsn to see end of message. */
static void sn_send_packets(Display *dpy, Window root, Window src,
			    Atom begin_atom, Atom info_atom, const char *msg)
{
	XEvent ev;
	Atom type = begin_atom;
	const char *p = msg;
	const char *end = msg + strlen(msg) + 1;
	int i;

	while (p < end) {
		memset(&ev, 0, sizeof(ev));
		ev.xclient.type = ClientMessage;
		ev.xclient.display = dpy;
		ev.xclient.window = src;
		ev.xclient.message_type = type;
		ev.xclient.format = 8;
		for (i = 0; i < 20 && p < end; i++)
			ev.xclient.data.b[i] = *p++;
		XSendEvent(dpy, root, False, PropertyChangeMask, &ev);
		type = info_atom;
	}
}

/* one-shot broadcast of a "new:" startup-notification sequence so the
 * monitor path inside wmaker can be exercised without wmaker being the
 * launcher, EWMH_CLIENT_SN_BROADCAST="<id> <ts> <ws>" */
static int sn_broadcast_mode(Display *dpy)
{
	const char *spec = getenv("EWMH_CLIENT_SN_BROADCAST");
	Window root = DefaultRootWindow(dpy);
	Window src;
	Atom begin_atom, info_atom;
	char id[128];
	long ts = 0, ws = -1;
	char msg[256];

	if (!spec || sscanf(spec, "%127s %ld %ld", id, &ts, &ws) < 1)
		return 1;

	begin_atom = intern_atom(dpy, "_NET_STARTUP_INFO_BEGIN");
	info_atom = intern_atom(dpy, "_NET_STARTUP_INFO");

	/* libsn keys reassembly on the sending window, so we need a real
	 * window for xclient.window, root is rejected */
	src = XCreateSimpleWindow(dpy, root, -1, -1, 1, 1, 0, 0, 0);

	if (ws >= 0)
		snprintf(msg, sizeof(msg),
			 "new: ID=%s SCREEN=0 NAME=ewmh-drive TIMESTAMP=%ld DESKTOP=%ld",
			 id, ts, ws);
	else
		snprintf(msg, sizeof(msg),
			 "new: ID=%s SCREEN=0 NAME=ewmh-drive TIMESTAMP=%ld", id, ts);

	sn_send_packets(dpy, root, src, begin_atom, info_atom, msg);
	XFlush(dpy);
	XDestroyWindow(dpy, src);
	XFlush(dpy);
	return 0;
}

/* one-shot "remove:" for a previously broadcast sequence so the test
 * driver can exercise the COMPLETED teardown without waiting for the
 * 20 s timeout, EWMH_CLIENT_SN_REMOVE="<id>" */
static int sn_remove_mode(Display *dpy)
{
	const char *id = getenv("EWMH_CLIENT_SN_REMOVE");
	Window root = DefaultRootWindow(dpy);
	Window src;
	Atom begin_atom, info_atom;
	char msg[192];

	if (!id || !*id)
		return 1;

	begin_atom = intern_atom(dpy, "_NET_STARTUP_INFO_BEGIN");
	info_atom = intern_atom(dpy, "_NET_STARTUP_INFO");
	src = XCreateSimpleWindow(dpy, root, -1, -1, 1, 1, 0, 0, 0);

	snprintf(msg, sizeof(msg), "remove: ID=%s", id);
	sn_send_packets(dpy, root, src, begin_atom, info_atom, msg);
	XFlush(dpy);
	XDestroyWindow(dpy, src);
	XFlush(dpy);
	return 0;
}

/* one-shot read of the X server clock: change a property on a private
 * window and print the resulting PropertyNotify timestamp, used by the
 * asan driver so a focus-stealing check can stamp a client with a real
 * server time captured just before an interactive drag */
static int print_server_time_mode(Display *dpy)
{
	Window root = DefaultRootWindow(dpy);
	Window w = XCreateSimpleWindow(dpy, root, -1, -1, 1, 1, 0, 0, 0);
	XEvent ev;

	XSelectInput(dpy, w, PropertyChangeMask);
	XChangeProperty(dpy, w, XA_WM_NAME, XA_STRING, 8, PropModeReplace,
			(const unsigned char *)"t", 1);
	for (;;) {
		XNextEvent(dpy, &ev);
		if (ev.type == PropertyNotify && ev.xproperty.window == w) {
			printf("%lu\n", (unsigned long)ev.xproperty.time);
			break;
		}
	}
	XDestroyWindow(dpy, w);
	XFlush(dpy);
	return 0;
}

static int format8_close_mode(Display *dpy)
{
	Window target = env_window("EWMH_CLIENT_FORMAT8_CLOSE_TARGET", None);
	Window root = DefaultRootWindow(dpy);
	XEvent ev;

	if (target == None)
		return 1;
	memset(&ev, 0, sizeof(ev));
	ev.xclient.type = ClientMessage;
	ev.xclient.window = target;
	ev.xclient.message_type = intern_atom(dpy, "_NET_CLOSE_WINDOW");
	ev.xclient.format = 8;
	XSendEvent(dpy, root, False,
		   SubstructureRedirectMask | SubstructureNotifyMask, &ev);
	XFlush(dpy);
	return 0;
}

/* Send a valid format-32 _NET_CLOSE_WINDOW ClientMessage so the WM
 * dispatches WM_DELETE_WINDOW (and a paired _NET_WM_PING) to the
 * target. data.l[0] = timestamp, data.l[1] = source indication
 * (0=unknown, 1=application, 2=pager). The WM ignores both fields and
 * uses w_global.timestamp.last_event, but the spec requires them so
 * clients/pagers can construct a well-formed request.
 * EWMH_CLIENT_CLOSE_TARGET=<xid>
 * EWMH_CLIENT_CLOSE_SOURCE=<n>      default 1
 * EWMH_CLIENT_CLOSE_TIME=<n>        default 0 (CurrentTime) */
static int close_mode(Display *dpy)
{
	Window target = env_window("EWMH_CLIENT_CLOSE_TARGET", None);
	long source = env_long("EWMH_CLIENT_CLOSE_SOURCE", 1);
	long ts = env_long("EWMH_CLIENT_CLOSE_TIME", 0);

	if (target == None)
		return 1;
	send_root_message(dpy, target, intern_atom(dpy, "_NET_CLOSE_WINDOW"),
			  ts, source, 0, 0, 0);
	return 0;
}

/* Set _NET_WM_ICON_GEOMETRY on the target window and send the matching
 * ClientMessage so wmaker's net_wm_icon_geometry handler re-reads the
 * property (PropertyNotify alone also works, the ClientMessage path is
 * what the spec mandates for an explicit client-driven update).
 * EWMH_CLIENT_ICON_GEOMETRY_TARGET=<xid>
 * EWMH_CLIENT_ICON_GEOMETRY="x y w h" */
static int icon_geometry_mode(Display *dpy)
{
	Window target = env_window("EWMH_CLIENT_ICON_GEOMETRY_TARGET", None);
	const char *spec = getenv("EWMH_CLIENT_ICON_GEOMETRY");
	long geom[4];

	if (target == None || !spec
	    || sscanf(spec, "%ld %ld %ld %ld",
		      &geom[0], &geom[1], &geom[2], &geom[3]) != 4)
		return 1;
	set_cardinals(dpy, target, intern_atom(dpy, "_NET_WM_ICON_GEOMETRY"),
		      geom, 4);
	send_root_message(dpy, target,
			  intern_atom(dpy, "_NET_WM_ICON_GEOMETRY"),
			  0, 0, 0, 0, 0);
	XFlush(dpy);
	return 0;
}

/* Send _NET_WM_MOVERESIZE with an arbitrary direction value to exercise
 * the direction-range validation. EWMH_CLIENT_MOVERESIZE_TARGET=<xid>
 * EWMH_CLIENT_MOVERESIZE_DIRECTION=<n> */
static int moveresize_mode(Display *dpy)
{
	Window target = env_window("EWMH_CLIENT_MOVERESIZE_TARGET", None);
	long direction = env_long("EWMH_CLIENT_MOVERESIZE_DIRECTION", 8);
	long x_root = env_long("EWMH_CLIENT_MOVERESIZE_X", 0);
	long y_root = env_long("EWMH_CLIENT_MOVERESIZE_Y", 0);
	long button = env_long("EWMH_CLIENT_MOVERESIZE_BUTTON", 0);

	if (target == None)
		return 1;
	send_root_message(dpy, target, intern_atom(dpy, "_NET_WM_MOVERESIZE"),
			  x_root, y_root, direction, button, 1);
	return 0;
}

/* Send a format-8 ClientMessage to a GNUstep/wmaker atom on a managed
 * window to verify the handler rejects non-32-format messages.
 * EWMH_CLIENT_FORMAT8_GNUSTEP_TARGET=<xid>
 * EWMH_CLIENT_FORMAT8_GNUSTEP_ATOM=<atom-name> */
static int format8_gnustep_mode(Display *dpy)
{
	Window target = env_window("EWMH_CLIENT_FORMAT8_GNUSTEP_TARGET", None);
	const char *atom_name = getenv("EWMH_CLIENT_FORMAT8_GNUSTEP_ATOM");
	Window root = DefaultRootWindow(dpy);
	XEvent ev;

	if (target == None || !atom_name)
		return 1;
	memset(&ev, 0, sizeof(ev));
	ev.xclient.type = ClientMessage;
	ev.xclient.window = target;
	ev.xclient.message_type = intern_atom(dpy, atom_name);
	ev.xclient.format = 8;
	ev.xclient.data.b[0] = 1;
	XSendEvent(dpy, root, False,
		   SubstructureRedirectMask | SubstructureNotifyMask, &ev);
	XFlush(dpy);
	return 0;
}

/* set _NET_DESKTOP_NAMES on root to the comma-separated list in
 * EWMH_CLIENT_SET_DESKTOP_NAMES, encoded as the spec's NUL-separated
 * UTF8_STRING, then send the ClientMessage wmaker reacts to so the
 * read is triggered without depending on a root PropertyNotify watch */
static int set_desktop_names_mode(Display *dpy)
{
	const char *src = getenv("EWMH_CLIENT_SET_DESKTOP_NAMES");
	Window root = DefaultRootWindow(dpy);
	Atom prop = intern_atom(dpy, "_NET_DESKTOP_NAMES");
	Atom utf8 = intern_atom(dpy, "UTF8_STRING");
	char buf[1024];
	size_t i, n;

	if (!src)
		return 1;
	for (i = 0, n = 0; src[i] && n + 1 < sizeof(buf); i++)
		buf[n++] = (src[i] == ',') ? '\0' : src[i];
	buf[n++] = '\0';
	XChangeProperty(dpy, root, prop, utf8, 8, PropModeReplace,
			(unsigned char *)buf, (int)n);
	send_root_message(dpy, root, prop, 0, 0, 0, 0, 0);
	XFlush(dpy);
	return 0;
}

static int activate_mode(Display *dpy)
{
	Window target = env_window("EWMH_CLIENT_ACTIVATE_TARGET", None);
	long source = env_long("EWMH_CLIENT_ACTIVATE_SOURCE", 1);
	long ts = env_long("EWMH_CLIENT_ACTIVATE_TIME", 0);

	if (target == None)
		return 1;
	send_root_message(dpy, target, intern_atom(dpy, "_NET_ACTIVE_WINDOW"),
			  source, ts, 0, 0, 0);
	return 0;
}

static int restack_mode(Display *dpy)
{
	Window target = env_window("EWMH_CLIENT_RESTACK_TARGET", None);
	Window sibling = env_window("EWMH_CLIENT_RESTACK_SIBLING", None);
	long detail = env_long("EWMH_CLIENT_RESTACK_DETAIL", 0);
	long source = env_long("EWMH_CLIENT_RESTACK_SOURCE", 2);

	if (target == None)
		return 1;
	send_root_message(dpy, target, intern_atom(dpy, "_NET_RESTACK_WINDOW"),
			  source, (long)sibling, detail, 0, 0);
	return 0;
}

static int fsmonitors_mode(Display *dpy)
{
	Window target = env_window("EWMH_CLIENT_FSMONITORS_TARGET", None);
	const char *spec = getenv("EWMH_CLIENT_FSMONITORS");
	long m[4];

	if (target == None || !spec
	    || sscanf(spec, "%ld %ld %ld %ld", &m[0], &m[1], &m[2], &m[3]) != 4)
		return 1;
	send_root_message(dpy, target,
			  intern_atom(dpy, "_NET_WM_FULLSCREEN_MONITORS"),
			  m[0], m[1], m[2], m[3], 1);
	return 0;
}

static int state_has_atom(Display *dpy, Window win, Atom prop, Atom needle)
{
	Atom type;
	int format;
	unsigned long n, after, i;
	unsigned char *data = NULL;
	int found = 0;

	if (XGetWindowProperty(dpy, win, prop, 0, 64, False, XA_ATOM,
			       &type, &format, &n, &after, &data) != Success)
		return 0;
	if (type == XA_ATOM && format == 32 && after == 0 && data) {
		long *atoms = (long *)data;

		for (i = 0; i < n; i++) {
			if ((Atom)atoms[i] == needle) {
				found = 1;
				break;
			}
		}
	}
	if (data)
		XFree(data);
	return found;
}

static int wm_state_mode(Display *dpy)
{
	Window target = env_window("EWMH_CLIENT_STATE_TARGET", None);
	long action = env_long("EWMH_CLIENT_STATE_ACTION", 1);
	const char *a1 = getenv("EWMH_CLIENT_STATE_ATOM1");
	const char *a2 = getenv("EWMH_CLIENT_STATE_ATOM2");
	long atom1, atom2;

	if (target == None || !a1)
		return 1;
	atom1 = (long)intern_atom(dpy, a1);
	atom2 = a2 ? (long)intern_atom(dpy, a2) : 0;
	send_root_message(dpy, target, intern_atom(dpy, "_NET_WM_STATE"),
			  action, atom1, atom2, 2, 0);
	return 0;
}

static int seticon_mode(Display *dpy)
{
	Window target = env_window("EWMH_CLIENT_SETICON_TARGET", None);
	long argb = env_long("EWMH_CLIENT_SETICON_ARGB", 0xFFFF0000L);
	long icon[2 + 16 * 16];
	int i;

	if (target == None)
		return 1;
	icon[0] = 16;
	icon[1] = 16;
	for (i = 0; i < 16 * 16; i++)
		icon[2 + i] = argb;
	set_cardinals(dpy, target, intern_atom(dpy, "_NET_WM_ICON"),
		      icon, 2 + 16 * 16);
	XFlush(dpy);
	return 0;
}

int main(void)
{
	Display *dpy;
	Window root, win, user_time_helper = None;
	int screen;
	XClassHint class_hint;
	XSizeHints size_hints;
	Atom wm_protocols, wm_delete_window, net_wm_ping;
	Atom net_wm_sync_request, net_wm_sync_request_counter;
	Atom net_wm_name, net_wm_pid, net_wm_user_time, net_wm_user_time_window;
	Atom net_wm_window_type, net_wm_window_type_normal, net_wm_icon;
	Atom gnustep_hide_app = None;
	Atom protocols[4];
	int nprotocols = 0;
	Atom window_type[1];
	const char *name = getenv("EWMH_CLIENT_NAME");
	long user_time = env_long("EWMH_CLIENT_USER_TIME", -1);
	long strut_left = env_long("EWMH_CLIENT_STRUT_LEFT", 0);
	long sync_counter = env_long("EWMH_CLIENT_SYNC_COUNTER", -1);
	long use_time_helper = env_long("EWMH_CLIENT_USER_TIME_HELPER", 0);
	long request_frame_extents = env_long("EWMH_CLIENT_REQUEST_FRAME_EXTENTS", 0);
	long bad_icon = env_long("EWMH_CLIENT_BAD_ICON", 0);
	long no_ping_reply = env_long("EWMH_CLIENT_NO_PING_REPLY", 0);
	long ignore_delete = env_long("EWMH_CLIENT_IGNORE_DELETE", 0);
	long want_gnustep_hide = env_long("EWMH_CLIENT_GNUSTEP_HIDE", 0);
	long transient_for = env_long("EWMH_CLIENT_TRANSIENT_FOR", 0);
	long state_above = env_long("EWMH_CLIENT_STATE_ABOVE", 0);
	const char *restore_on_unfullscreen = getenv("EWMH_CLIENT_RESTORE_ON_UNFULLSCREEN");
	int restore_w = 0, restore_h = 0;
	Atom net_wm_state = None, net_wm_state_fullscreen = None;
	int was_fullscreen = 0, want_restored_print = 0;
	const char *startup_id = getenv("EWMH_CLIENT_STARTUP_ID");
	const char *res_class = getenv("EWMH_CLIENT_CLASS");
	const char *res_name = getenv("EWMH_CLIENT_INSTANCE");
	const char *gravity = getenv("EWMH_CLIENT_GRAVITY");
	const char *pos_x = getenv("EWMH_CLIENT_X");
	const char *pos_y = getenv("EWMH_CLIENT_Y");
	const char *sz_w = getenv("EWMH_CLIENT_WIDTH");
	const char *sz_h = getenv("EWMH_CLIENT_HEIGHT");
	int init_x, init_y;
	unsigned int init_w, init_h;

	if (!name)
		name = "ewmh-drive-client";

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "could not open display\n");
		return 2;
	}
	if (getenv("EWMH_CLIENT_PRINT_SERVER_TIME")) {
		int ret = print_server_time_mode(dpy);
		XCloseDisplay(dpy);
		return ret;
	}
	if (getenv("EWMH_CLIENT_RESTACK_TARGET")) {
		int ret = restack_mode(dpy);
		XCloseDisplay(dpy);
		return ret;
	}
	if (getenv("EWMH_CLIENT_SETICON_TARGET")) {
		int ret = seticon_mode(dpy);
		XCloseDisplay(dpy);
		return ret;
	}
	if (getenv("EWMH_CLIENT_STATE_TARGET")) {
		int ret = wm_state_mode(dpy);
		XCloseDisplay(dpy);
		return ret;
	}
	if (getenv("EWMH_CLIENT_FSMONITORS_TARGET")) {
		int ret = fsmonitors_mode(dpy);
		XCloseDisplay(dpy);
		return ret;
	}
	if (getenv("EWMH_CLIENT_ACTIVATE_TARGET")) {
		int ret = activate_mode(dpy);
		XCloseDisplay(dpy);
		return ret;
	}
	if (getenv("EWMH_CLIENT_SET_DESKTOP_NAMES")) {
		int ret = set_desktop_names_mode(dpy);
		XCloseDisplay(dpy);
		return ret;
	}
	if (getenv("EWMH_CLIENT_FORMAT8_CLOSE_TARGET")) {
		int ret = format8_close_mode(dpy);
		XCloseDisplay(dpy);
		return ret;
	}
	if (getenv("EWMH_CLIENT_CLOSE_TARGET")) {
		int ret = close_mode(dpy);

		XCloseDisplay(dpy);
		return ret;
	}
	if (getenv("EWMH_CLIENT_ICON_GEOMETRY_TARGET")) {
		int ret = icon_geometry_mode(dpy);

		XCloseDisplay(dpy);
		return ret;
	}
	if (getenv("EWMH_CLIENT_MOVERESIZE_TARGET")) {
		int ret = moveresize_mode(dpy);
		XCloseDisplay(dpy);
		return ret;
	}
	if (getenv("EWMH_CLIENT_FORMAT8_GNUSTEP_TARGET")) {
		int ret = format8_gnustep_mode(dpy);
		XCloseDisplay(dpy);
		return ret;
	}
	if (getenv("EWMH_CLIENT_SN_BROADCAST")) {
		int ret = sn_broadcast_mode(dpy);
		XCloseDisplay(dpy);
		return ret;
	}
	if (getenv("EWMH_CLIENT_SN_REMOVE")) {
		int ret = sn_remove_mode(dpy);
		XCloseDisplay(dpy);
		return ret;
	}

	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	init_x = pos_x ? atoi(pos_x) : 80;
	init_y = pos_y ? atoi(pos_y) : 80;
	init_w = sz_w ? (unsigned int)atoi(sz_w) : 320;
	init_h = sz_h ? (unsigned int)atoi(sz_h) : 180;

	win = XCreateSimpleWindow(dpy, root, init_x, init_y, init_w, init_h, 0,
				  BlackPixel(dpy, screen), WhitePixel(dpy, screen));
	XSelectInput(dpy, win, StructureNotifyMask | PropertyChangeMask);

	class_hint.res_name = (char *)(res_name ? res_name : "ewmh-drive-client");
	class_hint.res_class = (char *)(res_class ? res_class : "EwmhDriveClient");
	XSetClassHint(dpy, win, &class_hint);
	XStoreName(dpy, win, name);

	size_hints.flags = PPosition | PSize;
	size_hints.x = pos_x ? atoi(pos_x) : 80;
	size_hints.y = pos_y ? atoi(pos_y) : 80;
	size_hints.width = sz_w ? atoi(sz_w) : 320;
	size_hints.height = sz_h ? atoi(sz_h) : 180;
	if (gravity) {
		/* Accept the X11 gravity names case-insensitively. Map
		 * them to the NorthWestGravity..SouthEastGravity +
		 * StaticGravity constants from <X11/Xlib.h>. */
		const struct {
			const char *name;
			int value;
		} gravities[] = {
			{ "northwest", NorthWestGravity },
			{ "north",     NorthGravity     },
			{ "northeast", NorthEastGravity },
			{ "west",      WestGravity      },
			{ "center",    CenterGravity    },
			{ "east",      EastGravity      },
			{ "southwest", SouthWestGravity },
			{ "south",     SouthGravity     },
			{ "southeast", SouthEastGravity },
			{ "static",    StaticGravity    },
		};
		size_t gi, glen = strlen(gravity);
		char lower[16];
		int matched = -1;

		if (glen > 0 && glen < sizeof(lower)) {
			for (gi = 0; gi < glen; gi++)
				lower[gi] = (char)tolower((unsigned char)gravity[gi]);
			lower[glen] = '\0';
			for (gi = 0; gi < sizeof(gravities) / sizeof(gravities[0]); gi++) {
				if (strcmp(lower, gravities[gi].name) == 0) {
					matched = gravities[gi].value;
					break;
				}
			}
		}
		if (matched != -1) {
			size_hints.flags |= PWinGravity;
			size_hints.win_gravity = matched;
		}
	}
	XSetWMNormalHints(dpy, win, &size_hints);

	net_wm_name = intern_atom(dpy, "_NET_WM_NAME");
	net_wm_pid = intern_atom(dpy, "_NET_WM_PID");
	net_wm_user_time = intern_atom(dpy, "_NET_WM_USER_TIME");
	net_wm_user_time_window = intern_atom(dpy, "_NET_WM_USER_TIME_WINDOW");
	net_wm_window_type = intern_atom(dpy, "_NET_WM_WINDOW_TYPE");
	net_wm_window_type_normal = intern_atom(dpy, "_NET_WM_WINDOW_TYPE_NORMAL");
	net_wm_icon = intern_atom(dpy, "_NET_WM_ICON");
	wm_protocols = intern_atom(dpy, "WM_PROTOCOLS");
	wm_delete_window = intern_atom(dpy, "WM_DELETE_WINDOW");
	net_wm_ping = intern_atom(dpy, "_NET_WM_PING");
	net_wm_sync_request = intern_atom(dpy, "_NET_WM_SYNC_REQUEST");
	net_wm_sync_request_counter = intern_atom(dpy, "_NET_WM_SYNC_REQUEST_COUNTER");

	set_utf8(dpy, win, net_wm_name, name);
	set_cardinal(dpy, win, net_wm_pid, (unsigned long)getpid());
	if (use_time_helper) {
		user_time_helper = XCreateSimpleWindow(dpy, win, 0, 0, 1, 1, 0,
						BlackPixel(dpy, screen), WhitePixel(dpy, screen));
		set_window(dpy, win, net_wm_user_time_window, user_time_helper);
		if (user_time >= 0)
			set_cardinal(dpy, user_time_helper, net_wm_user_time,
				     (unsigned long)user_time);
	} else if (user_time >= 0) {
		set_cardinal(dpy, win, net_wm_user_time, (unsigned long)user_time);
	}
	if (startup_id)
		set_utf8(dpy, win, intern_atom(dpy, "_NET_STARTUP_ID"), startup_id);
	if (sync_counter >= 0)
		set_cardinal(dpy, win, net_wm_sync_request_counter,
			     (unsigned long)sync_counter);
	set_client_machine(dpy, win);

	if (strut_left > 0) {
		long strut[12] = {0};
		strut[0] = strut_left;
		strut[5] = (long)DisplayHeight(dpy, screen) - 1;
		set_cardinals(dpy, win, intern_atom(dpy, "_NET_WM_STRUT_PARTIAL"),
			      strut, 12);
		set_cardinals(dpy, win, intern_atom(dpy, "_NET_WM_STRUT"),
			      strut, 4);
	}
	if (bad_icon) {
		long icon[3] = {2147483647L, 2147483647L, 0};
		set_cardinals(dpy, win, net_wm_icon, icon, 3);
	}
	if (transient_for != 0)
		XSetTransientForHint(dpy, win, (Window)transient_for);
	if (state_above) {
		long st[1];

		st[0] = (long)intern_atom(dpy, "_NET_WM_STATE_ABOVE");
		XChangeProperty(dpy, win, intern_atom(dpy, "_NET_WM_STATE"),
				XA_ATOM, 32, PropModeReplace,
				(const unsigned char *)st, 1);
	}

	if (restore_on_unfullscreen
	    && sscanf(restore_on_unfullscreen, "%d %d", &restore_w, &restore_h) == 2) {
		net_wm_state = intern_atom(dpy, "_NET_WM_STATE");
		net_wm_state_fullscreen = intern_atom(dpy, "_NET_WM_STATE_FULLSCREEN");
	} else {
		restore_on_unfullscreen = NULL;
	}

	window_type[0] = net_wm_window_type_normal;
	XChangeProperty(dpy, win, net_wm_window_type, XA_ATOM, 32,
			PropModeReplace, (const unsigned char *)window_type, 1);

	protocols[nprotocols++] = wm_delete_window;
	protocols[nprotocols++] = net_wm_ping;
	if (sync_counter >= 0)
		protocols[nprotocols++] = net_wm_sync_request;
	if (want_gnustep_hide) {
		gnustep_hide_app = intern_atom(dpy, "_GNUSTEP_WM_HIDE_APP");
		protocols[nprotocols++] = gnustep_hide_app;
	}
	XSetWMProtocols(dpy, win, protocols, nprotocols);

	if (request_frame_extents) {
		send_root_message(dpy, win, intern_atom(dpy, "_NET_REQUEST_FRAME_EXTENTS"),
				  0, 0, 0, 0, 0);
		printf("0x%lx\n", win);
		fflush(stdout);
	} else {
		XMapWindow(dpy, win);
		XFlush(dpy);
		printf("0x%lx\n", win);
		if (user_time_helper != None)
			printf("0x%lx\n", user_time_helper);
		fflush(stdout);
	}

	for (;;) {
		XEvent ev;

		XNextEvent(dpy, &ev);
		if (restore_on_unfullscreen && ev.type == PropertyNotify
		    && ev.xproperty.atom == net_wm_state) {
			int now_fs = state_has_atom(dpy, win, net_wm_state,
						    net_wm_state_fullscreen);

			/* mirror what GTK apps do on the FULLSCREEN bit
			 * dropping out of _NET_WM_STATE, immediately force
			 * a client-chosen geometry so the WM has to honour
			 * a ConfigureRequest racing its own restore */
			if (was_fullscreen && !now_fs) {
				XMoveResizeWindow(dpy, win, 0, 0,
						  restore_w, restore_h);
				XFlush(dpy);
				want_restored_print = 1;
			}
			was_fullscreen = now_fs;
			continue;
		}
		if (want_restored_print && ev.type == ConfigureNotify
		    && ev.xconfigure.window == win) {
			printf("RESTORED %dx%d\n",
			       ev.xconfigure.width, ev.xconfigure.height);
			fflush(stdout);
			want_restored_print = 0;
			continue;
		}
		if (ev.type != ClientMessage)
			continue;
		if (ev.xclient.message_type != wm_protocols || ev.xclient.format != 32)
			continue;
		if ((Atom)ev.xclient.data.l[0] == net_wm_ping) {
			if (!no_ping_reply)
				send_ping_reply(dpy, &ev.xclient);
		} else if (gnustep_hide_app != None
			   && (Atom)ev.xclient.data.l[0] == gnustep_hide_app) {
			printf("HIDE_APP\n");
			fflush(stdout);
		} else if ((Atom)ev.xclient.data.l[0] == wm_delete_window) {
			if (ignore_delete)
				continue;
			XDestroyWindow(dpy, win);
			XCloseDisplay(dpy);
			return 0;
		}
	}
}
