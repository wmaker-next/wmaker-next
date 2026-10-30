/*
 *  tinywin-client.c
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

/* Minimal placeholder-size client for test/tiny-window-drive.sh: maps a
 * 1x1 top-level and then resizes it to its real geometry, the order qiv
 * and similar clients use. The client window id is printed on stdout so
 * the drive can locate the window before it is reparented.
 *
 * Environment knobs:
 *   TINYWIN_NAME    title (default "tinywin")
 *   TINYWIN_GEOM    final geometry (default "200x150+540+325")
 *   TINYWIN_DELAY   microseconds between map and resize (default 200000)
 *   TINYWIN_MAXSIZE WM_NORMAL_HINTS PMaxSize "WxH" (none by default)
 */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(void)
{
	const char *name = getenv("TINYWIN_NAME");
	const char *geom = getenv("TINYWIN_GEOM");
	const char *maxsize = getenv("TINYWIN_MAXSIZE");
	const char *delay = getenv("TINYWIN_DELAY");
	struct timespec ts;
	unsigned long usec;
	Display *dpy;
	Window w;
	Atom protos[2];
	int scr;
	unsigned int gw, gh;
	int gx, gy;

	if (!name || !*name)
		name = "tinywin";
	if (!geom || !*geom)
		geom = "200x150+540+325";
	usec = (delay && *delay) ? strtoul(delay, NULL, 0) : 200000UL;

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "tinywin-client: cannot open display\n");
		return 1;
	}
	scr = DefaultScreen(dpy);

	if (XParseGeometry(geom, &gx, &gy, &gw, &gh) == 0) {
		gx = 540;
		gy = 325;
		gw = 200;
		gh = 150;
	}

	w = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), gx, gy, 1, 1, 0,
				BlackPixel(dpy, scr), WhitePixel(dpy, scr));

	protos[0] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	protos[1] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	XSetWMProtocols(dpy, w, protos, 2);
	XStoreName(dpy, w, name);

	if (maxsize && *maxsize) {
		unsigned int mw, mh;
		XSizeHints hints;

		memset(&hints, 0, sizeof(hints));
		if (sscanf(maxsize, "%ux%u", &mw, &mh) == 2 && mw > 0 && mh > 0) {
			hints.flags = PMaxSize;
			hints.max_width = (int)mw;
			hints.max_height = (int)mh;
			XSetWMNormalHints(dpy, w, &hints);
		}
	}

	printf("0x%lx\n", (unsigned long)w);
	fflush(stdout);

	XMapWindow(dpy, w);
	XFlush(dpy);
	ts.tv_sec = 0;
	ts.tv_nsec = (long)usec * 1000;
	nanosleep(&ts, NULL);

	XMoveResizeWindow(dpy, w, gx, gy, gw, gh);
	XFlush(dpy);

	for (;;) {
		XEvent ev;

		XNextEvent(dpy, &ev);
		if (ev.type == DestroyNotify)
			break;
	}
	XCloseDisplay(dpy);
	return 0;
}
