/*
 *  mrs-client.c
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

/* Minimal _NET_WM_MOVERESIZE client for test/moveres-drive.sh: a
 * chrome-like undecorated top-level (MWM decorations=0, Static
 * gravity) that hands a titlebar drag over to the window manager.
 *
 * Env knobs:
 *   MRS_GEOM      standard X geometry string (default "400x300+300+120")
 *   MRS_RESIZE    when set, a press in the right EDGE_W pixels sends a
 *                 SIZE_RIGHT resize handoff instead of a move
 *   MRS_HOLD_GRAB when set, the message is sent while the implicit
 *                 grab is still held and released 50ms later,
 *                 reproducing the grab race the WM must retry through
 */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TITLE_H 40
#define EDGE_W 8

int main(void)
{
	const char *geom = getenv("MRS_GEOM");
	Display *dpy;
	Window w, root;
	XClassHint ch;
	XSizeHints hints;
	Atom motif, wm_protocols, wm_delete, moveresize;
	unsigned long mwm[5];
	int scr, gx, gy;
	unsigned int gw, gh;

	if (!geom || !*geom)
		geom = "400x300+300+120";

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "mrs-client: cannot open display\n");
		return 1;
	}
	scr = DefaultScreen(dpy);
	root = RootWindow(dpy, scr);

	if (XParseGeometry(geom, &gx, &gy, &gw, &gh) == 0) {
		gx = 300; gy = 120; gw = 400; gh = 300;
	}

	w = XCreateSimpleWindow(dpy, root, gx, gy, gw, gh, 0,
				BlackPixel(dpy, scr), WhitePixel(dpy, scr));

	motif = XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
	mwm[0] = 2;		/* MWM_HINTS_DECORATIONS */
	mwm[1] = 0;
	mwm[2] = 0;		/* no decorations, chrome-like */
	mwm[3] = 0;
	mwm[4] = 0;
	XChangeProperty(dpy, w, motif, motif, 32, PropModeReplace,
			(unsigned char *)mwm, 5);

	ch.res_name = (char *)"mrsclient";
	ch.res_class = (char *)"MRSClient";
	XSetClassHint(dpy, w, &ch);

	memset(&hints, 0, sizeof(hints));
	hints.flags = USPosition | USSize | PWinGravity;
	hints.x = gx;
	hints.y = gy;
	hints.width = (int)gw;
	hints.height = (int)gh;
	hints.win_gravity = StaticGravity;
	XSetWMNormalHints(dpy, w, &hints);

	XStoreName(dpy, w, "mrs-client");

	wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
	wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(dpy, w, &wm_delete, 1);

	moveresize = XInternAtom(dpy, "_NET_WM_MOVERESIZE", False);

	XSelectInput(dpy, w,
		     ButtonPressMask | ButtonReleaseMask |
		     PointerMotionMask | StructureNotifyMask);
	XMapWindow(dpy, w);

	for (;;) {
		XEvent ev;
		XEvent msg;

		XNextEvent(dpy, &ev);
		if (ev.type == ClientMessage &&
		    (Atom)ev.xclient.data.l[0] == wm_delete)
			break;
		if (ev.type == ButtonPress && ev.xbutton.button == 1 &&
		    getenv("MRS_RESIZE") && ev.xbutton.x >= (int)gw - EDGE_W) {
			memset(&msg, 0, sizeof(msg));
			msg.xclient.type = ClientMessage;
			msg.xclient.window = w;
			msg.xclient.message_type = moveresize;
			msg.xclient.format = 32;
			msg.xclient.data.l[0] = ev.xbutton.x_root;
			msg.xclient.data.l[1] = ev.xbutton.y_root;
			msg.xclient.data.l[2] = 3;	/* SIZE_RIGHT */
			msg.xclient.data.l[3] = ev.xbutton.button;
			msg.xclient.data.l[4] = 1;
			XUngrabPointer(dpy, CurrentTime);
			XSendEvent(dpy, root, False,
				   SubstructureRedirectMask |
				   SubstructureNotifyMask, &msg);
			XFlush(dpy);
			continue;
		}
		if (ev.type == ButtonPress && ev.xbutton.button == 1 &&
		    ev.xbutton.y < TITLE_H) {
			memset(&msg, 0, sizeof(msg));
			msg.xclient.type = ClientMessage;
			msg.xclient.window = w;
			msg.xclient.message_type = moveresize;
			msg.xclient.format = 32;
			msg.xclient.data.l[0] = ev.xbutton.x_root;
			msg.xclient.data.l[1] = ev.xbutton.y_root;
			msg.xclient.data.l[2] = 8;	/* MOVE */
			msg.xclient.data.l[3] = ev.xbutton.button;
			msg.xclient.data.l[4] = 1;	/* application */
			XSendEvent(dpy, root, False,
				   SubstructureRedirectMask |
				   SubstructureNotifyMask, &msg);
			XFlush(dpy);
			if (getenv("MRS_HOLD_GRAB"))
				nanosleep(&(struct timespec){0, 50000000L}, NULL);
			XUngrabPointer(dpy, CurrentTime);
			XFlush(dpy);
		}
	}
	return 0;
}
