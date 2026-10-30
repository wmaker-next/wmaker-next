/*
 *  xdnd-source.c
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

/* Minimal XDND v3 source that drops a single file:// URI onto a target
 * window, used by test/dock-drop-drive.sh to drive the dock's main tile
 * without a real file manager.
 *
 * usage: xdnd-source <target-window-id> <local-path>
 *
 * exit 0  XdndStatus accepted and XdndFinished received
 * exit 1  XdndStatus refused or no reply
 * exit 2  setup failure
 */
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static Atom xdnd_enter, xdnd_position, xdnd_status, xdnd_leave, xdnd_drop;
static Atom xdnd_finished, xdnd_selection, xdnd_action_copy, uri_list;

static void send_cm(Display *dpy, Window dst, Atom type,
		    long d0, long d1, long d2, long d3, long d4)
{
	XEvent ev;

	memset(&ev, 0, sizeof(ev));
	ev.xclient.type = ClientMessage;
	ev.xclient.display = dpy;
	ev.xclient.window = dst;
	ev.xclient.message_type = type;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = d0;
	ev.xclient.data.l[1] = d1;
	ev.xclient.data.l[2] = d2;
	ev.xclient.data.l[3] = d3;
	ev.xclient.data.l[4] = d4;
	XSendEvent(dpy, dst, False, NoEventMask, &ev);
}

int main(int argc, char **argv)
{
	Display *dpy;
	Window src, target;
	XEvent ev;
	char uri[4200];
	int got_status = 0, accepted = 0, got_finished = 0;
	int i;

	if (argc != 3) {
		fprintf(stderr, "usage: %s <window-id> <path>\n", argv[0]);
		return 2;
	}
	target = (Window)strtoul(argv[1], NULL, 0);
	snprintf(uri, sizeof(uri), "file://%s\r\n", argv[2]);

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "cannot open display\n");
		return 2;
	}

	xdnd_enter = XInternAtom(dpy, "XdndEnter", False);
	xdnd_position = XInternAtom(dpy, "XdndPosition", False);
	xdnd_status = XInternAtom(dpy, "XdndStatus", False);
	xdnd_leave = XInternAtom(dpy, "XdndLeave", False);
	xdnd_drop = XInternAtom(dpy, "XdndDrop", False);
	xdnd_finished = XInternAtom(dpy, "XdndFinished", False);
	xdnd_selection = XInternAtom(dpy, "XdndSelection", False);
	xdnd_action_copy = XInternAtom(dpy, "XdndActionCopy", False);
	uri_list = XInternAtom(dpy, "text/uri-list", False);

	src = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 1, 1, 0, 0, 0);
	XSetSelectionOwner(dpy, xdnd_selection, src, CurrentTime);
	if (XGetSelectionOwner(dpy, xdnd_selection) != src) {
		fprintf(stderr, "could not own XdndSelection\n");
		return 2;
	}

	/* XdndEnter: source, version<<24 | three-types-flag, type1..3 */
	send_cm(dpy, target, xdnd_enter, src, (3L << 24), uri_list, 0, 0);
	/* XdndPosition: source, reserved, (x<<16)|y, time, action */
	send_cm(dpy, target, xdnd_position, src, 0, 0, CurrentTime, xdnd_action_copy);
	XFlush(dpy);

	for (i = 0; i < 200 && !got_status; i++) {
		while (XPending(dpy)) {
			XNextEvent(dpy, &ev);
			if (ev.type == ClientMessage &&
			    ev.xclient.message_type == xdnd_status) {
				got_status = 1;
				accepted = (ev.xclient.data.l[1] & 1) ? 1 : 0;
			}
		}
		usleep(10000);
	}
	if (!got_status) {
		fprintf(stderr, "no XdndStatus from target\n");
		return 1;
	}
	printf("status accepted=%d\n", accepted);
	if (!accepted) {
		send_cm(dpy, target, xdnd_leave, src, 0, 0, 0, 0);
		XFlush(dpy);
		return 1;
	}

	/* XdndDrop: source, reserved, time */
	send_cm(dpy, target, xdnd_drop, src, 0, CurrentTime, 0, 0);
	XFlush(dpy);

	for (i = 0; i < 200 && !got_finished; i++) {
		while (XPending(dpy)) {
			XNextEvent(dpy, &ev);
			if (ev.type == SelectionRequest) {
				XSelectionRequestEvent *r = &ev.xselectionrequest;
				XEvent reply;

				XChangeProperty(dpy, r->requestor, r->property,
						r->target, 8, PropModeReplace,
						(unsigned char *)uri,
						(int)strlen(uri));
				memset(&reply, 0, sizeof(reply));
				reply.xselection.type = SelectionNotify;
				reply.xselection.display = dpy;
				reply.xselection.requestor = r->requestor;
				reply.xselection.selection = r->selection;
				reply.xselection.target = r->target;
				reply.xselection.property = r->property;
				reply.xselection.time = r->time;
				XSendEvent(dpy, r->requestor, False, 0, &reply);
				XFlush(dpy);
			} else if (ev.type == ClientMessage &&
				   ev.xclient.message_type == xdnd_finished) {
				got_finished = 1;
			}
		}
		usleep(10000);
	}
	printf("finished=%d\n", got_finished);
	XCloseDisplay(dpy);
	return got_finished ? 0 : 1;
}
