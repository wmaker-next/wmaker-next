/*
 *  dockapp-client.c
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

/* Minimal standalone dockapp emulator for test/dockapp-drive.sh.
 *
 * Mirrors the libdockapp DACreateIcon/DAShow shape: a 64x64 leader
 * window published with WM_HINTS WithdrawnState + IconWindowHint +
 * WindowGroupHint and an unmapped 64x64 icon window wmaker swallows
 * into the appicon tile. No libdockapp dependency.
 *
 * Environment knobs:
 *   DOCKAPP_NAME       WM_CLASS res_name and window name (default "datest")
 *   DOCKAPP_HINTS      "class" (default): res_class is literally "DockApp",
 *                      exercising the is_dockapp class branch
 *                      "withdrawn": res_class is DOCKAPP_NAME, exercising
 *                      only the WM_HINTS WithdrawnState branch
 *   DOCKAPP_EXIT_AFTER exit cleanly after N seconds (0/absent = forever)
 */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void paint_icon(Display *dpy, Window win)
{
	XColor dark, light;
	Colormap cm = DefaultColormap(dpy, DefaultScreen(dpy));
	GC gc;
	int i, j;

	dark.flags = DoRed | DoGreen | DoBlue;
	dark.red = 0x3333;
	dark.green = 0x6666;
	dark.blue = 0x9999;
	light.flags = DoRed | DoGreen | DoBlue;
	light.red = 0xcccc;
	light.green = 0xcccc;
	light.blue = 0x3333;
	XAllocColor(dpy, cm, &dark);
	XAllocColor(dpy, cm, &light);

	gc = XCreateGC(dpy, win, 0, NULL);
	for (i = 0; i < 8; i++) {
		for (j = 0; j < 8; j++) {
			XSetForeground(dpy, gc, ((i + j) % 2) ? light.pixel : dark.pixel);
			XFillRectangle(dpy, win, gc, j * 8, i * 8, 8, 8);
		}
	}
	XSetForeground(dpy, gc, light.pixel);
	XFillRectangle(dpy, win, gc, 32, 32, 32, 32);
	XFreeGC(dpy, gc);
}

int main(int argc, char **argv)
{
	Display *dpy;
	Window root, leader, icon;
	const char *name, *mode, *exit_after;
	XClassHint class_hint;
	char class_buf[256];
	XWMHints hints;
	Atom del;
	XEvent ev;

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "dockapp-client: cannot open display\n");
		return 1;
	}

	name = getenv("DOCKAPP_NAME");
	if (!name || !*name)
		name = "datest";
	mode = getenv("DOCKAPP_HINTS");
	if (!mode || !*mode)
		mode = "class";
	exit_after = getenv("DOCKAPP_EXIT_AFTER");
	if (exit_after && *exit_after) {
		unsigned int secs = (unsigned int)strtoul(exit_after, NULL, 0);

		if (secs > 0)
			alarm(secs);
	}

	root = DefaultRootWindow(dpy);
	leader = XCreateSimpleWindow(dpy, root, 0, 0, 64, 64, 0, 0,
				     WhitePixel(dpy, DefaultScreen(dpy)));
	icon = XCreateSimpleWindow(dpy, root, 0, 0, 64, 64, 0, 0,
				   BlackPixel(dpy, DefaultScreen(dpy)));
	paint_icon(dpy, icon);

	class_hint.res_name = (char *)name;
	if (strcmp(mode, "withdrawn") == 0)
		snprintf(class_buf, sizeof(class_buf), "%s", name);
	else
		snprintf(class_buf, sizeof(class_buf), "DockApp");
	class_hint.res_class = class_buf;
	XSetClassHint(dpy, leader, &class_hint);
	XStoreName(dpy, leader, name);

	del = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(dpy, leader, &del, 1);

	/* WM_COMMAND must exist or wDockAttachIcon pops the command
	 * input dialog and the drive stalls */
	XSetCommand(dpy, leader, argv, argc);

	memset(&hints, 0, sizeof(hints));
	hints.flags = StateHint | IconWindowHint | WindowGroupHint;
	hints.initial_state = WithdrawnState;
	hints.icon_window = icon;
	hints.window_group = leader;
	XSetWMHints(dpy, leader, &hints);

	XSelectInput(dpy, leader, StructureNotifyMask | PropertyChangeMask);
	XSelectInput(dpy, icon, ButtonPressMask | ExposureMask | StructureNotifyMask);

	XMapRaised(dpy, leader);
	XFlush(dpy);

	printf("leader 0x%lx\n", (unsigned long)leader);
	printf("icon 0x%lx\n", (unsigned long)icon);
	fflush(stdout);

	for (;;) {
		XNextEvent(dpy, &ev);
		switch (ev.type) {
		case DestroyNotify:
			if (ev.xdestroywindow.window == leader) {
				XCloseDisplay(dpy);
				return 0;
			}
			break;
		case ClientMessage:
			if (ev.xclient.message_type ==
			    XInternAtom(dpy, "WM_PROTOCOLS", False)
			    && (Atom)ev.xclient.data.l[0] == del
			    && ev.xclient.window == leader) {
				XCloseDisplay(dpy);
				return 0;
			}
			break;
		case Expose:
			if (ev.xexpose.window == icon && ev.xexpose.count == 0)
				paint_icon(dpy, icon);
			break;
		default:
			/* UnmapNotify/MapNotify/ReparentNotify on the
			 * leader and Button press/release on the icon
			 * are tolerated */
			break;
		}
	}
}
