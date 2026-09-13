/* Minimal Motif-hints client for test/moveres-drive.sh and
 * test/mwm-hints-drive.sh: a plain top-level window that publishes
 * _MOTIF_WM_HINTS before mapping. The moveres drive uses the default
 * flags=MWM_HINTS_DECORATIONS, decorations=0 (a chrome-like
 * undecorated window). The mwm-hints drive varies the flags,
 * functions and decorations fields to exercise the partial decor
 * masks and the MWM_HINTS_FUNCTIONS bits.
 *
 * Environment knobs:
 *   MOTH_NAME   WM_CLASS name and resource (default "mothints")
 *   MOTH_GEOM   standard X geometry string (default "400x300+300+300")
 *   MOTH_FLAGS  hints flags field (default 2 = MWM_HINTS_DECORATIONS)
 *   MOTH_FUNCS  functions field (default 0)
 *   MOTH_DECOR  decorations field (default 0)
 */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static long env_long(const char *name, long fallback)
{
	const char *s = getenv(name);
	char *end;
	long v;

	if (!s || !*s)
		return fallback;
	v = strtol(s, &end, 0);
	if (end == s)
		return fallback;
	return v;
}

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
	long mwm[5];
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
	mwm[0] = env_long("MOTH_FLAGS", 2L);
	mwm[1] = env_long("MOTH_FUNCS", 0L);
	mwm[2] = env_long("MOTH_DECOR", 0L);
	mwm[3] = 0;
	mwm[4] = 0;
	XChangeProperty(dpy, w, motif, motif, 32, PropModeReplace,
			(unsigned char *)mwm, 5);

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
