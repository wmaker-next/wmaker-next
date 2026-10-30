/*
 *  wmstate-client.c
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

/* Minimal _WINDOWMAKER_STATE planter for test/hint-inspector-drive.sh:
 * creates a top-level window that already carries the saved-state
 * property a surviving client would present after a warm restart, so
 * the manage-time restore path can be exercised without a real client
 * left over from a previous WM incarnation.
 *
 * The property type must be the _WINDOWMAKER_STATE atom itself,
 * matching wWindowSaveState()'s writer: the manage-time read is
 * type-checked and rejects a CARDINAL-typed copy.
 *
 * Env knobs:
 *   WMSTATE_TITLEBAR  no_titlebar tri-state in slot 17:
 *                     2 = set+on (default), 1 = set+off,
 *                     0 = plant nothing
 *   WMSTATE_CLASS     WM_CLASS res_class (default "WmstateTest")
 */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WSAVED_STATE_SLOTS 24
#define NO_TITLEBAR_SLOT 17

int main(void)
{
	const char *class_name = getenv("WMSTATE_CLASS");
	const char *titlebar = getenv("WMSTATE_TITLEBAR");
	Display *dpy;
	Window w, root;
	XClassHint ch;
	unsigned long data[WSAVED_STATE_SLOTS];
	Atom state_atom;
	int i, tb = 2;

	if (titlebar && *titlebar)
		tb = atoi(titlebar);

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "wmstate-client: cannot open display\n");
		return 1;
	}

	root = DefaultRootWindow(dpy);
	w = XCreateSimpleWindow(dpy, root, 200, 150, 320, 200, 1,
				BlackPixel(dpy, DefaultScreen(dpy)),
				WhitePixel(dpy, DefaultScreen(dpy)));
	XStoreName(dpy, w, "wmstate-client");

	ch.res_name = "wmstate-client";
	ch.res_class = (char *)(class_name && *class_name ? class_name : "WmstateTest");
	XSetClassHint(dpy, w, &ch);

	if (tb != 0) {
		state_atom = XInternAtom(dpy, "_WINDOWMAKER_STATE", False);
		for (i = 0; i < WSAVED_STATE_SLOTS; i++)
			data[i] = 0;
		data[NO_TITLEBAR_SLOT] = (unsigned long)tb;

		XChangeProperty(dpy, w, state_atom, state_atom, 32,
				PropModeReplace, (unsigned char *)data,
				WSAVED_STATE_SLOTS);
	}

	XMapWindow(dpy, w);
	XFlush(dpy);

	printf("0x%lx\n", (unsigned long)w);
	fflush(stdout);

	for (;;)
		sleep(3600);

	return 0;
}
