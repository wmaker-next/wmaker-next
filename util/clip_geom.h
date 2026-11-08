/*
 *  clip_geom.h - head geometry helpers for the clipboard popup
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <https://www.gnu.org/licenses/>.
 *
 *  Monitor geometry queries for the typing-triggered clipboard popup,
 *  which must land on the head of the focused window and not the head
 *  under the mouse pointer. Like clip_common these helpers depend only
 *  on libxcb and libc so test binaries that do not link libWUtil can
 *  use them.
 */

#ifndef WMUTIL_CLIP_GEOM_H
#define WMUTIL_CLIP_GEOM_H

#include <xcb/xcb.h>
#include <stdbool.h>

typedef struct {
	int x, y, w, h;
} ClipHead;

/* Upper bound on heads clipQueryHeads() will report. */
#define CLIP_MAX_HEADS 32

/* Fill out[0..] with the head rects in root coordinates. RandR 1.5
 * monitors are authoritative, except on servers that describe their
 * heads only through Xinerama while RandR is a stub covering less than
 * the root window, where the Xinerama screens win. Returns the count, 0
 * when the server describes no heads (the caller then treats the whole
 * work area as one head). */
int clipQueryHeads(xcb_connection_t *xc, xcb_window_t root,
		   ClipHead *out, int max);

/* Index of the head with the largest intersection area with the given
 * rect, or -1 when the rect touches no head. A rect with w or h <= 0 is
 * treated as a 1x1 point. */
int clipHeadForRect(const ClipHead *heads, int count,
		    int x, int y, int w, int h);

/* Root-relative rect of `win` in root coordinates. Fails for the root
 * window, the no-focus placeholders and unqueryable windows. */
bool clipWindowRootRect(xcb_connection_t *xc, xcb_window_t root,
			xcb_window_t win, int *x, int *y, int *w, int *h);

#endif /* WMUTIL_CLIP_GEOM_H */
