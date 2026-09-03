/*
 *  xcconn.h - tiny xcb-only client setup helpers
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The connect + screen/root boilerplate shared by the util clients.
 *  Depends only on libxcb and libc so binaries that link nothing
 *  beyond xcb can use them.
 */

#ifndef WMUTIL_XCCONN_H
#define WMUTIL_XCCONN_H

#include <xcb/xcb.h>

/* Connect to `name`, or to $DISPLAY when name is NULL or empty
 * (xcb_connect("") silently targets :0). On failure returns NULL,
 * printing "prog: could not open display \"name\"" when `prog` is not
 * NULL, and exits 1 when `fatal` is set. On success *screen is the
 * connected screen number. */
xcb_connection_t *xcconn_open(const char *name, const char *prog,
			      int fatal, int *screen);

xcb_screen_t *xcconn_screen(xcb_connection_t *conn, int n);

xcb_window_t xcconn_root(xcb_connection_t *conn, int n);

#endif /* WMUTIL_XCCONN_H */
