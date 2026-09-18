/*
 *  xcconn.c - tiny xcb-only client setup helpers
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

#include "xcconn.h"

#include <stdio.h>
#include <stdlib.h>

xcb_connection_t *xcconn_open(const char *name, const char *prog,
			      int fatal, int *screen)
{
	const char *shown;
	xcb_connection_t *c;
	int n = 0;

	if (name && *name) {
		shown = name;
		c = xcb_connect(name, &n);
	} else {
		shown = getenv("DISPLAY");
		if (!shown)
			shown = "";
		c = xcb_connect(NULL, &n);
	}
	if (xcb_connection_has_error(c)) {
		xcb_disconnect(c);
		if (prog)
			fprintf(stderr, "%s: could not open display \"%s\"\n", prog, shown);
		if (fatal)
			exit(1);
		return NULL;
	}
	if (screen)
		*screen = n;
	return c;
}

xcb_screen_t *xcconn_screen(xcb_connection_t *conn, int n)
{
	xcb_screen_iterator_t iter;

	iter = xcb_setup_roots_iterator(xcb_get_setup(conn));
	while (n-- > 0 && iter.rem)
		xcb_screen_next(&iter);
	return iter.data;
}

xcb_window_t xcconn_root(xcb_connection_t *conn, int n)
{
	xcb_screen_t *s = xcconn_screen(conn, n);

	return s->root;
}
