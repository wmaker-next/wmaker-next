/* _NET_WM_STATE_DEMANDS_ATTENTION requestor for test/urgent-click-drive.sh:
 * sends an EWMH _NET_WM_STATE ClientMessage to the root window naming the
 * target window, since wmctrl passes unknown property names through as raw
 * atom names instead of mapping demands_attention to its _NET_WM_STATE
 * spelling.
 *
 * argv[1]  target window id (decimal or 0x-prefixed)
 * argv[2]  "add" or "remove"
 */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
	Display *dpy;
	Window w, root;
	XEvent ev;
	Atom state, demands;
	long op;

	if (argc < 3)
		return 2;
	if (strcmp(argv[2], "add") == 0)
		op = 1;		/* _NET_WM_STATE_ADD */
	else if (strcmp(argv[2], "remove") == 0)
		op = 0;	/* _NET_WM_STATE_REMOVE */
	else
		return 2;

	w = (Window)strtoul(argv[1], NULL, 0);
	if (!w)
		return 2;

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "urgent-req: cannot open display\n");
		return 1;
	}
	root = DefaultRootWindow(dpy);
	state = XInternAtom(dpy, "_NET_WM_STATE", False);
	demands = XInternAtom(dpy, "_NET_WM_STATE_DEMANDS_ATTENTION", False);

	memset(&ev, 0, sizeof(ev));
	ev.xclient.type = ClientMessage;
	ev.xclient.window = w;
	ev.xclient.message_type = state;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = op;
	ev.xclient.data.l[1] = (long)demands;
	XSendEvent(dpy, root, False,
		   SubstructureRedirectMask | SubstructureNotifyMask, &ev);
	XFlush(dpy);
	XCloseDisplay(dpy);
	return 0;
}
