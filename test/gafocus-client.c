/*
 *  gafocus-client.c
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

/* Globally-active client for test/restart-focus-drive.sh: maps a
 * top-level with InputHint=False and WM_TAKE_FOCUS, so the window
 * manager must use the focus protocol. A WM_TAKE_FOCUS request is
 * answered with XSetInputFocus() after a configurable delay, the way a
 * toolkit answers from its own main loop. The window id is printed on
 * stdout so the drive can locate the window before it is reparented.
 *
 * Environment knobs:
 *   GAFOCUS_NAME         title (default "gafocus")
 *   GAFOCUS_GEOM         geometry (default "200x120+50+50")
 *   GAFOCUS_USER_TIME    _NET_WM_USER_TIME value (unset skips the property)
 *   GAFOCUS_REPLY_DELAY  milliseconds before XSetInputFocus (default 20)
 *   GAFOCUS_ARM_AFTER_MS ignore requests arriving before this many
 *                       milliseconds after startup (default 0), so a
 *                       drive can let the map-time focus requests pass
 *                       unanswered and only exercise later ones
 */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int ignore_errors(Display *dpy, XErrorEvent *ev)
{
	(void)dpy;
	(void)ev;
	return 0;
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

int main(void)
{
	const char *name = getenv("GAFOCUS_NAME");
	const char *geom = getenv("GAFOCUS_GEOM");
	const char *utime = getenv("GAFOCUS_USER_TIME");
	const char *delay = getenv("GAFOCUS_REPLY_DELAY");
	struct timespec ts, start;
	unsigned long ms, arm;
	XWMHints wmhints;
	XClassHint classhint;
	Display *dpy;
	Window w;
	Atom protos[1], user_time;
	int scr;
	unsigned int gw, gh;
	int gx, gy;

	if (!name || !*name)
		name = "gafocus";
	if (!geom || !*geom)
		geom = "200x120+50+50";
	ms = (delay && *delay) ? strtoul(delay, NULL, 0) : 20UL;
	arm = getenv("GAFOCUS_ARM_AFTER_MS") ? strtoul(getenv("GAFOCUS_ARM_AFTER_MS"), NULL, 0) : 0UL;

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "gafocus-client: cannot open display\n");
		return 1;
	}
	scr = DefaultScreen(dpy);

	if (XParseGeometry(geom, &gx, &gy, &gw, &gh) == 0) {
		gx = 50;
		gy = 50;
		gw = 200;
		gh = 120;
	}

	w = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), gx, gy, gw, gh, 1,
				BlackPixel(dpy, scr), WhitePixel(dpy, scr));

	protos[0] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	XSetWMProtocols(dpy, w, protos, 1);

	memset(&wmhints, 0, sizeof(wmhints));
	wmhints.flags = InputHint;
	wmhints.input = False;
	XSetWMHints(dpy, w, &wmhints);

	memset(&classhint, 0, sizeof(classhint));
	classhint.res_name = (char *)name;
	classhint.res_class = "GaFocus";
	XSetClassHint(dpy, w, &classhint);

	XStoreName(dpy, w, name);
	XSetIconName(dpy, w, name);
	set_client_machine(dpy, w);

	if (utime && *utime) {
		unsigned long t = strtoul(utime, NULL, 0);

		user_time = XInternAtom(dpy, "_NET_WM_USER_TIME", False);
		XChangeProperty(dpy, w, user_time, XA_CARDINAL, 32, PropModeReplace,
				(const unsigned char *)&t, 1);
	}

	printf("0x%lx\n", (unsigned long)w);
	fflush(stdout);

	XMapWindow(dpy, w);
	XFlush(dpy);

	XSetErrorHandler(ignore_errors);

	clock_gettime(CLOCK_MONOTONIC, &start);

	for (;;) {
		XEvent ev;

		XNextEvent(dpy, &ev);
		if (ev.type == DestroyNotify)
			break;
		if (ev.type == ClientMessage) {
			if (ev.xclient.message_type == XInternAtom(dpy, "WM_PROTOCOLS", False)
			    && (Atom)ev.xclient.data.l[0] == protos[0]) {
				struct timespec now;

				clock_gettime(CLOCK_MONOTONIC, &now);
				if ((now.tv_sec - start.tv_sec) * 1000L
				    + (now.tv_nsec - start.tv_nsec) / 1000000L < (long)arm)
					continue;
				ts.tv_sec = (time_t)(ms / 1000);
				ts.tv_nsec = (long)(ms % 1000) * 1000000;
				nanosleep(&ts, NULL);
				XSetInputFocus(dpy, w, RevertToParent, CurrentTime);
				XFlush(dpy);
			}
		}
	}
	XCloseDisplay(dpy);
	return 0;
}
