/* Minimal deterministic X11 client for test/ewmh-layout-drive.sh. */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
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

	if (target == None)
		return 1;
	send_root_message(dpy, target, intern_atom(dpy, "_NET_RESTACK_WINDOW"),
			  2, (long)sibling, detail, 0, 0);
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
	const char *startup_id = getenv("EWMH_CLIENT_STARTUP_ID");
	const char *res_class = getenv("EWMH_CLIENT_CLASS");

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
	win = XCreateSimpleWindow(dpy, root, 80, 80, 320, 180, 0,
				  BlackPixel(dpy, screen), WhitePixel(dpy, screen));
	XSelectInput(dpy, win, StructureNotifyMask | PropertyChangeMask);

	class_hint.res_name = (char *)"ewmh-drive-client";
	class_hint.res_class = (char *)(res_class ? res_class : "EwmhDriveClient");
	XSetClassHint(dpy, win, &class_hint);
	XStoreName(dpy, win, name);

	size_hints.flags = PPosition | PSize;
	size_hints.x = 80;
	size_hints.y = 80;
	size_hints.width = 320;
	size_hints.height = 180;
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
