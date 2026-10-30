/*
 *  appmenu-client.c
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

/* Minimal standalone wmlib-WMMenu-style client for
 * test/appmenu-drive.sh. Emulates what a WINGs/GNUstep app does when
 * it publishes its menu through the _WINDOWMAKER_MENU property, no
 * wmlib dependency.
 *
 * The leader is a normal top-level window naming itself as group
 * leader; the menu property is published on it (wAppMenuGet reads
 * wapp->main_window). On a wmSelectItem ClientMessage from wmaker the
 * helper writes the received tag to _WMENU_TEST_ACK (format-32
 * cardinal) on the leader and maps a small panel window for tag 101,
 * mirroring FSViewer opening its Info panel.
 *
 * Environment knob: APPMENU_NAME (default "apptest") is the WM_CLASS
 * res_name/res_class and the menu title root.
 */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void publish_menu(Display *dpy, Window leader, Atom menu_atom, const char *name)
{
	char main_line[128];
	char *lines[8];
	XTextProperty tp;

	snprintf(main_line, sizeof(main_line), "1 1 %s", name);
	lines[0] = (char *)"WMMenu 0";
	lines[1] = main_line;
	lines[2] = (char *)"12 1 0 1 2 Info";
	lines[3] = (char *)"1 2 Info";
	lines[4] = (char *)"10 2 101 1 Info Panel...";
	lines[5] = (char *)"2";
	lines[6] = (char *)"10 1 102 1 Quit";
	lines[7] = (char *)"2";

	if (XStringListToTextProperty(lines, 8, &tp) == 0)
		return;
	XSetTextProperty(dpy, leader, &tp, menu_atom);
	XFree(tp.value);
}

int main(void)
{
	Display *dpy;
	Window root, leader, panel;
	const char *name;
	char panel_class[256];
	XClassHint class_hint;
	XWMHints hints;
	Atom del, protocols, menu_atom, ack_atom;
	XEvent ev;
	long tag;

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "appmenu-client: cannot open display\n");
		return 1;
	}

	name = getenv("APPMENU_NAME");
	if (!name || !*name)
		name = "apptest";

	root = DefaultRootWindow(dpy);
	leader = XCreateSimpleWindow(dpy, root, 200, 200, 300, 200, 0, 0,
				     WhitePixel(dpy, DefaultScreen(dpy)));
	panel = XCreateSimpleWindow(dpy, root, 250, 250, 200, 120, 0, 0,
				    WhitePixel(dpy, DefaultScreen(dpy)));

	class_hint.res_name = (char *)name;
	class_hint.res_class = (char *)name;
	XSetClassHint(dpy, leader, &class_hint);
	snprintf(panel_class, sizeof(panel_class), "%s.Panel", name);
	class_hint.res_class = panel_class;
	XSetClassHint(dpy, panel, &class_hint);
	XStoreName(dpy, leader, name);

	del = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
	menu_atom = XInternAtom(dpy, "_WINDOWMAKER_MENU", False);
	ack_atom = XInternAtom(dpy, "_WMENU_TEST_ACK", False);
	XSetWMProtocols(dpy, leader, &del, 1);
	XSetWMProtocols(dpy, panel, &del, 1);

	memset(&hints, 0, sizeof(hints));
	hints.flags = InputHint | WindowGroupHint;
	hints.input = True;
	hints.window_group = leader;
	XSetWMHints(dpy, leader, &hints);

	publish_menu(dpy, leader, menu_atom, name);

	XSelectInput(dpy, leader, StructureNotifyMask | PropertyChangeMask);
	XSelectInput(dpy, panel, StructureNotifyMask);

	XMapRaised(dpy, leader);
	XFlush(dpy);

	printf("leader 0x%lx\n", (unsigned long)leader);
	printf("panel 0x%lx\n", (unsigned long)panel);
	fflush(stdout);

	for (;;) {
		XNextEvent(dpy, &ev);
		if (ev.type == DestroyNotify && ev.xdestroywindow.window == leader)
			break;
		if (ev.type != ClientMessage)
			continue;
		if (ev.xclient.message_type == protocols
		    && (Atom)ev.xclient.data.l[0] == del) {
			if (ev.xclient.window == leader)
				break;
			if (ev.xclient.window == panel)
				XUnmapWindow(dpy, panel);
		} else if (ev.xclient.message_type == menu_atom
			   && ev.xclient.format == 32
			   && ev.xclient.data.l[1] == 1
			   && ev.xclient.window == leader) {
			/* WXSendClientMessage payload: l0 timestamp,
			 * l1 command (wmSelectItem), l2 tag */
			tag = (long)ev.xclient.data.l[2];
			XChangeProperty(dpy, leader, ack_atom, XA_CARDINAL, 32,
					PropModeReplace, (unsigned char *)&tag, 1);
			if (tag == 101)
				XMapWindow(dpy, panel);
			XFlush(dpy);
		}
	}

	XCloseDisplay(dpy);
	return 0;
}
