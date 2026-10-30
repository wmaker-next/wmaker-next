/*
 *  selreply-client.c
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

/* Selection owner for test/selection-reply-drive.sh: owns PRIMARY and
 * answers SelectionRequest events for XA_STRING after a configurable
 * delay, so a reply can be made to land long after the requestor gave
 * up. The owning window is never mapped.
 *
 * argv[1]  reply text (default "selreply")
 * argv[2]  reply delay in milliseconds (default 0)
 */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(int argc, char **argv)
{
	const char *text = argc > 1 ? argv[1] : "selreply";
	long delay_ms = argc > 2 ? strtol(argv[2], NULL, 0) : 0;
	Display *dpy;
	Window w;
	int scr;

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "selreply-client: cannot open display\n");
		return 1;
	}
	scr = DefaultScreen(dpy);

	w = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), -10, -10, 1, 1, 0,
				BlackPixel(dpy, scr), WhitePixel(dpy, scr));
	XStoreName(dpy, w, "selreply-owner");
	XSetSelectionOwner(dpy, XA_PRIMARY, w, CurrentTime);
	XFlush(dpy);
	printf("0x%lx\n", (unsigned long)w);
	fflush(stdout);

	for (;;) {
		XEvent ev, reply;
		XSelectionRequestEvent *req;
		struct timespec ts;

		XNextEvent(dpy, &ev);
		if (ev.type != SelectionRequest)
			continue;
		req = &ev.xselectionrequest;
		printf("requestor 0x%lx\n", (unsigned long)req->requestor);
		fflush(stdout);

		ts.tv_sec = delay_ms / 1000;
		ts.tv_nsec = (delay_ms % 1000) * 1000000L;
		nanosleep(&ts, NULL);

		memset(&reply, 0, sizeof(reply));
		reply.xselection.type = SelectionNotify;
		reply.xselection.display = dpy;
		reply.xselection.requestor = req->requestor;
		reply.xselection.selection = req->selection;
		reply.xselection.target = req->target;
		reply.xselection.time = req->time;
		reply.xselection.property = req->property;
		if (req->target == XA_STRING)
			XChangeProperty(dpy, req->requestor, req->property,
					XA_STRING, 8, PropModeReplace,
					(unsigned char *)text, (int)strlen(text));
		else
			reply.xselection.property = None;
		XSendEvent(dpy, req->requestor, False, 0, &reply);
		XFlush(dpy);
	}
}
