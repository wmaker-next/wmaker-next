/*
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

#include "wconfig.h"

#include <string.h>
#include <math.h>

#include "layout.h"

/* Indexed by WLayout, matches the WConfig enum table in config-schema.c and
 * the WMState per-workspace Layout key. */
static const char *layout_names[WLAYOUT_COUNT] = {
	"float", "tile", "tileh", "grid", "monocle"
};

const char *wLayoutName(WLayout layout)
{
	if (layout < 0 || layout >= WLAYOUT_COUNT)
		return layout_names[WLAYOUT_FLOAT];
	return layout_names[layout];
}

WLayout wLayoutFromName(const char *name)
{
	int i;

	if (!name)
		return WLAYOUT_FLOAT;
	for (i = 0; i < WLAYOUT_COUNT; i++) {
		if (strcasecmp(name, layout_names[i]) == 0)
			return i;
	}
	return WLAYOUT_FLOAT;
}

static float clamp_ratio(float r)
{
	if (r < WLAYOUT_RATIO_MIN)
		return WLAYOUT_RATIO_MIN;
	if (r > WLAYOUT_RATIO_MAX)
		return WLAYOUT_RATIO_MAX;
	return r;
}

/* master column at master_ratio of the primary axis, master_count windows
 * stacked evenly along the secondary axis inside it, remaining windows
 * stacked evenly in the leftover strip. horiz swaps the axes. A single
 * tileable window always gets the whole area regardless of master_count. */
static void compute_tile(WArea a, int master_count, float master_ratio,
			 int horiz, int n, WLayoutRect *out)
{
	int aw = a.x2 - a.x1;
	int ah = a.y2 - a.y1;
	int prim = horiz ? ah : aw;
	int sec = horiz ? aw : ah;
	int mprim, sy, sh, i, k;

	if (master_count > n)
		master_count = n;
	if (master_count < 0)
		master_count = 0;

	if (master_count == 0)
		mprim = 0;
	else if (master_count == n)
		mprim = prim;
	else
		mprim = (int)(prim * clamp_ratio(master_ratio));

	for (i = 0; i < n; i++) {
		int in_master = (i < master_count);
		int col_off = in_master ? 0 : mprim;
		int col_w = in_master ? mprim : prim - mprim;
		int cnt = in_master ? master_count : n - master_count;

		k = in_master ? i : i - master_count;
		sy = sec * k / cnt;
		sh = sec * (k + 1) / cnt - sy;
		if (horiz) {
			out[i].x = a.x1 + sy;
			out[i].y = a.y1 + col_off;
			out[i].w = sh;
			out[i].h = col_w;
		} else {
			out[i].x = a.x1 + col_off;
			out[i].y = a.y1 + sy;
			out[i].w = col_w;
			out[i].h = sh;
		}
	}
}

/* near-square grid: cols = ceil(sqrt(n)), rows = ceil(n/cols). Cells fill
 * column-major so the master (out[0]) is top-left, and any short final
 * column has its cells stretched to consume the full height. */
static void compute_grid(WArea a, int n, WLayoutRect *out)
{
	int aw = a.x2 - a.x1;
	int ah = a.y2 - a.y1;
	int cols, rows, i, c, r, cnt, cx, cw, cy, ch;

	cols = 1;
	while (cols * cols < n)
		cols++;
	rows = (n + cols - 1) / cols;

	for (i = 0; i < n; i++) {
		c = i / rows;
		r = i % rows;
		/* the last column may hold fewer than 'rows' windows */
		cnt = (c == cols - 1) ? n - c * rows : rows;
		cx = aw * c / cols;
		cw = aw * (c + 1) / cols - cx;
		cy = ah * r / cnt;
		ch = ah * (r + 1) / cnt - cy;
		out[i].x = a.x1 + cx;
		out[i].y = a.y1 + cy;
		out[i].w = cw;
		out[i].h = ch;
	}
}

int wLayoutCompute(WLayout layout, WArea usable,
		   int master_count, float master_ratio,
		   int n, WLayoutRect *out)
{
	int aw = usable.x2 - usable.x1;
	int ah = usable.y2 - usable.y1;
	int i;

	if (n <= 0 || aw <= 0 || ah <= 0 || layout == WLAYOUT_FLOAT)
		return 0;

	switch (layout) {
	case WLAYOUT_TILE:
		compute_tile(usable, master_count, master_ratio, 0, n, out);
		break;
	case WLAYOUT_TILE_H:
		compute_tile(usable, master_count, master_ratio, 1, n, out);
		break;
	case WLAYOUT_GRID:
		compute_grid(usable, n, out);
		break;
	case WLAYOUT_MONOCLE:
		for (i = 0; i < n; i++) {
			out[i].x = usable.x1;
			out[i].y = usable.y1;
			out[i].w = aw;
			out[i].h = ah;
		}
		break;
	default:
		return 0;
	}
	return n;
}

/* ---------- apply pass ----------------------------------------------- */

#ifndef WLAYOUT_COMPUTE_ONLY

#endif /* WLAYOUT_COMPUTE_ONLY */

