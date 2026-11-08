/*
 *  clip_geom.c - head geometry helpers for the clipboard popup
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
 */

#include <stdlib.h>

#include <xcb/xcb.h>
#include <xcb/randr.h>
#include <xcb/xinerama.h>

#include "clip_geom.h"

static int queryRandR(xcb_connection_t *xc, xcb_window_t root,
		      ClipHead *out, int max)
{
	xcb_randr_get_monitors_reply_t *mr;
	xcb_randr_monitor_info_iterator_t mi;
	int count = 0;

	mr = xcb_randr_get_monitors_reply(xc,
			xcb_randr_get_monitors(xc, root, 1), NULL);
	if (!mr)
		return 0;

	for (mi = xcb_randr_get_monitors_monitors_iterator(mr);
	     mi.rem && count < max;
	     xcb_randr_monitor_info_next(&mi)) {
		if (mi.data->width <= 0 || mi.data->height <= 0)
			continue;
		out[count].x = mi.data->x;
		out[count].y = mi.data->y;
		out[count].w = mi.data->width;
		out[count].h = mi.data->height;
		count++;
	}
	free(mr);

	return count;
}

static int queryXinerama(xcb_connection_t *xc, ClipHead *out, int max)
{
	xcb_xinerama_is_active_reply_t *irep;
	xcb_xinerama_query_screens_reply_t *srep;
	xcb_xinerama_screen_info_t *info;
	int count = 0, n, i;

	irep = xcb_xinerama_is_active_reply(xc, xcb_xinerama_is_active(xc), NULL);
	if (!irep || !irep->state) {
		free(irep);
		return 0;
	}
	free(irep);

	srep = xcb_xinerama_query_screens_reply(xc,
			xcb_xinerama_query_screens(xc), NULL);
	if (!srep)
		return 0;

	n = xcb_xinerama_query_screens_screen_info_length(srep);
	info = xcb_xinerama_query_screens_screen_info(srep);
	for (i = 0; i < n && count < max; i++) {
		if (info[i].width <= 0 || info[i].height <= 0)
			continue;
		out[count].x = info[i].x_org;
		out[count].y = info[i].y_org;
		out[count].w = info[i].width;
		out[count].h = info[i].height;
		count++;
	}
	free(srep);

	return count;
}

int clipQueryHeads(xcb_connection_t *xc, xcb_window_t root,
		   ClipHead *out, int max)
{
	const xcb_query_extension_reply_t *ext;
	ClipHead randr[CLIP_MAX_HEADS], xinerama[CLIP_MAX_HEADS];
	xcb_get_geometry_reply_t *gr;
	int count_r = 0, count_x = 0;
	int max_x = 0, max_y = 0, root_w = 0, root_h = 0, i;

	if (max > CLIP_MAX_HEADS)
		max = CLIP_MAX_HEADS;

	ext = xcb_get_extension_data(xc, &xcb_randr_id);
	if (ext && ext->present)
		count_r = queryRandR(xc, root, randr, CLIP_MAX_HEADS);

	ext = xcb_get_extension_data(xc, &xcb_xinerama_id);
	if (ext && ext->present)
		count_x = queryXinerama(xc, xinerama, CLIP_MAX_HEADS);

	gr = xcb_get_geometry_reply(xc, xcb_get_geometry(xc, root), NULL);
	if (gr) {
		root_w = gr->width;
		root_h = gr->height;
		free(gr);
	}
	for (i = 0; i < count_r; i++) {
		if (randr[i].x + randr[i].w > max_x)
			max_x = randr[i].x + randr[i].w;
		if (randr[i].y + randr[i].h > max_y)
			max_y = randr[i].y + randr[i].h;
	}

	/* Some servers (Xephyr +xinerama, nested servers) expose real
	 * Xinerama heads but only a stub RandR view whose monitors cover
	 * less than the root window. Prefer Xinerama when it can account
	 * for more heads than that incomplete view. */
	if (count_x > count_r && (count_r == 0 || max_x < root_w || max_y < root_h)) {
		for (i = 0; i < count_x && i < max; i++)
			out[i] = xinerama[i];
		return count_x < max ? count_x : max;
	}
	if (count_r > 0) {
		for (i = 0; i < count_r && i < max; i++)
			out[i] = randr[i];
		return count_r < max ? count_r : max;
	}
	if (count_x > 0) {
		for (i = 0; i < count_x && i < max; i++)
			out[i] = xinerama[i];
		return count_x < max ? count_x : max;
	}
	return 0;
}

int clipHeadForRect(const ClipHead *heads, int count,
		    int x, int y, int w, int h)
{
	int i, best = -1;
	unsigned long best_area = 0;

	if (w <= 0 || h <= 0) {
		w = 1;
		h = 1;
	}

	for (i = 0; i < count; i++) {
		int x1 = x > heads[i].x ? x : heads[i].x;
		int y1 = y > heads[i].y ? y : heads[i].y;
		int rx = x + w, ry = y + h;
		int hx = heads[i].x + heads[i].w, hy = heads[i].y + heads[i].h;
		int x2 = rx < hx ? rx : hx;
		int y2 = ry < hy ? ry : hy;
		unsigned long area;

		if (x2 <= x1 || y2 <= y1)
			continue;
		area = (unsigned long)(x2 - x1) * (unsigned long)(y2 - y1);
		if (area > best_area) {
			best_area = area;
			best = i;
		}
	}

	return best;
}

bool clipWindowRootRect(xcb_connection_t *xc, xcb_window_t root,
			xcb_window_t win, int *x, int *y, int *w, int *h)
{
	xcb_get_geometry_reply_t *g;
	xcb_translate_coordinates_reply_t *t;

	if (win == XCB_NONE || win == XCB_INPUT_FOCUS_POINTER_ROOT || win == root)
		return false;

	g = xcb_get_geometry_reply(xc, xcb_get_geometry(xc, win), NULL);
	if (!g)
		return false;

	t = xcb_translate_coordinates_reply(xc,
			xcb_translate_coordinates(xc, win, root, 0, 0), NULL);
	if (!t) {
		free(g);
		return false;
	}

	*x = t->dst_x;
	*y = t->dst_y;
	*w = g->width;
	*h = g->height;
	free(t);
	free(g);

	return *w > 0 && *h > 0;
}
