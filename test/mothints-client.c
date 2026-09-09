/* Minimal chrome-like client for test/moveres-drive.sh: a plain
 * top-level window that publishes _MOTIF_WM_HINTS with
 * MWM_HINTS_DECORATIONS and decorations=0 before mapping, so wmaker
 * manages it with no titlebar, no resizebar, no buttons and no
 * border, the way undecorated toolkit windows ask for it.
 *
 * Environment knobs: MOTH_NAME (default "mothints") is the WM_CLASS
 * name and resource, MOTH_GEOM (default "400x300+300+300") is the
 * standard X geometry string.
 */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
	const char *name = getenv("MOTH_NAME");
	const char *geom = getenv("MOTH_GEOM");
	Display *dpy;
	Window w;
	XSizeHints hints;
	Atom motif, type;
	int fmt;
	unsigned char *prop = NULL;
	unsigned long len, bytes;
	long dec[] = { 2, 0, 0, 0, 0 };	/* MWM_HINTS_DECORATIONS, decorations=0 */
	XClassHint ch;
	int scr;
	unsigned int gw, gh;
	int gx, gy;

	if (!name || !*name)
		name = "mothints";
	if (!geom || !*geom)
		geom = "400x300+300+300";

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "mothints-client: cannot open display\n");
		return 1;
	}
	scr = DefaultScreen(dpy);

	if (XParseGeometry(geom, &gx, &gy, &gw, &gh) == 0) {
		gw = 400;
		gh = 300;
		gx = 300;
		gy = 300;
	}

	w = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), gx, gy, gw, gh, 0,
				BlackPixel(dpy, scr), WhitePixel(dpy, scr));

	motif = XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
	XChangeProperty(dpy, w, motif, motif, 32, PropModeReplace,
			(unsigned char *)dec, 5);

	XStoreName(dpy, w, name);
	ch.res_name = (char *)name;
	ch.res_class = (char *)name;
	XSetClassHint(dpy, w, &ch);
	memset(&hints, 0, sizeof(hints));
	hints.flags = USPosition | USSize;
	hints.x = gx;
	hints.y = gy;
	hints.width = (int)gw;
	hints.height = (int)gh;
	XSetWMNormalHints(dpy, w, &hints);

	XMapWindow(dpy, w);
	XFlush(dpy);

	for (;;) {
		XEvent ev;

		XNextEvent(dpy, &ev);
		if (ev.type == DestroyNotify)
			break;
		if (ev.type == PropertyNotify && ev.xproperty.window == w) {
			if (XGetWindowProperty(dpy, w, motif, 0, 5, False, motif,
					       &type, &fmt, &len, &bytes, &prop) == Success
			    && prop) {
				XFree(prop);
				prop = NULL;
			}
		}
	}
	XCloseDisplay(dpy);
	return 0;
}
