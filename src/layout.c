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
#include "config-values.h"

_Static_assert((int)WLAYOUT_FLOAT == (int)WVL_FLOAT
	       && (int)WLAYOUT_TILE == (int)WVL_TILE
	       && (int)WLAYOUT_TILE_H == (int)WVL_TILE_H
	       && (int)WLAYOUT_GRID == (int)WVL_GRID
	       && (int)WLAYOUT_MONOCLE == (int)WVL_MONOCLE,
	       "config-values.h WVL_* must match WLayout ordering");

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
	if (isnan(r))
		return WLAYOUT_RATIO_DEFAULT;
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

#include "screen.h"
#include "window.h"
#include "framewin.h"
#include "actions.h"
#include "workspace.h"
#include "stacking.h"
#include "xinerama.h"

static bool window_is_tileable(WWindow *wwin, int workspace)
{
	/* Multi-member windows are tiled by every member workspace's
	 * layout, the frame is recomputed on each switch and the single
	 * old_geometry copy only records the pre-tiling floating frame
	 * so two layouts never contend for it. Omnipresent windows still
	 * float so a sticky panel is never packed into a stack. */
	if (!wwin->frame || !WWIN_ON_WORKSPACE(wwin, workspace))
		return false;
	if (IS_OMNIPRESENT(wwin))
		return false;
	if (wwin->flags.miniaturized || wwin->flags.hidden
	    || wwin->flags.shaded || wwin->flags.fullscreen)
		return false;
	if (WFLAGP(wwin, no_resizable) || WFLAGP(wwin, no_movable))
		return false;
	if (WFLAGP(wwin, no_tiling) || wwin->flags.no_tiling)
		return false;
	if (wwin->transient_for != XCB_NONE
	    && wwin->transient_for != wwin->screen_ptr->root_win)
		return false;
	if (WFLAGP(wwin, floating) || WFLAGP(wwin, sunken))
		return false;
	return true;
}

/* Fill wins[] (caller-sized to at least the workspace population) with the
 * tileable windows on (workspace, head) in focus-MRU order and return the
 * count. head < 0 collects across every head. */
static int collect_tileable(WScreen *scr, int workspace, int head,
			    WWindow **wins, int max)
{
	WWindow *w;
	int n = 0;

	for (w = scr->focused_window; w && n < max; w = w->prev) {
		if (!window_is_tileable(w, workspace))
			continue;
		if (head >= 0 && wGetHeadForWindow(w) != head)
			continue;
		wins[n++] = w;
	}
	return n;
}

static void configure_into_cell(WWindow *wwin, WLayoutRect r)
{
	unsigned int cw, ch;
	int bw, vh;

	bw = HAS_BORDER(wwin) ? 2 * wFrameBorderWidth(wwin) : 0;
	cw = (r.w > bw) ? r.w - bw : 1;
	vh = r.h - wwin->frame->top_width - wwin->frame->bottom_width - bw;
	ch = (vh > 0) ? vh : 1;

	wWindowConstrainSize(wwin, &cw, &ch);

	if (!wwin->flags.tiled) {
		save_old_geometry(wwin, SAVE_GEOMETRY_ALL);
		wwin->flags.tiled = 1;
	}
	if (wwin->flags.maximized) {
		wwin->flags.maximized = 0;
		wwin->flags.old_maximized = 0;
		wWindowUpdateButtonImages(wwin);
	}
	wWindowConfigure(wwin, r.x, r.y, (int)cw, (int)ch);
	wWindowSynthConfigureNotify(wwin);
}

static void restore_tiled_window(WWindow *wwin)
{
	int x, y;
	unsigned int w, h;

	if (!wwin->flags.tiled)
		return;
	wwin->flags.tiled = 0;
	x = wwin->old_geometry.x;
	y = wwin->old_geometry.y;
	w = wwin->old_geometry.width ? wwin->old_geometry.width : wwin->client.width;
	h = wwin->old_geometry.height ? wwin->old_geometry.height : wwin->client.height;
	wWindowConstrainSize(wwin, &w, &h);
	wWindowConfigure(wwin, x, y, (int)w, (int)h);
	wWindowSynthConfigureNotify(wwin);
}

#define WLAYOUT_MAX_TILEABLE 128

void wLayoutApply(WScreen *scr, int workspace)
{
	WWorkspace *ws;
	WWindow *wins[WLAYOUT_MAX_TILEABLE];
	WLayoutRect out[WLAYOUT_MAX_TILEABLE];
	WArea usable;
	int head, n, i;

	if (!scr || workspace < 0 || workspace >= scr->workspace_count)
		return;
	ws = scr->workspaces[workspace];
	if (ws->layout == WLAYOUT_FLOAT)
		return;

	for (head = 0; head < wXineramaHeads(scr); head++) {
		n = collect_tileable(scr, workspace, head, wins, WLAYOUT_MAX_TILEABLE);
		if (n == 0)
			continue;
		usable = wGetUsableAreaForHead(scr, head, NULL, true);
		if (!wLayoutCompute(ws->layout, usable, ws->master_count,
				    ws->master_ratio, n, out))
			continue;
		for (i = 0; i < n; i++)
			configure_into_cell(wins[i], out[i]);
		if (ws->layout == WLAYOUT_MONOCLE)
			wRaiseFrame(wins[0]->frame->core);
	}
}

void wLayoutRestore(WScreen *scr, int workspace)
{
	WWindow *w;

	if (!scr)
		return;
	for (w = scr->focused_window; w; w = w->prev) {
		if (w->frame && WWIN_ON_WORKSPACE(w, workspace)
		    && !IS_OMNIPRESENT(w) && w->flags.tiled)
			restore_tiled_window(w);
	}
}

void wLayoutSetWorkspace(WScreen *scr, int workspace, WLayout layout)
{
	WWorkspace *ws;

	if (!scr || workspace < 0 || workspace >= scr->workspace_count)
		return;
	if (layout < 0 || layout >= WLAYOUT_COUNT)
		return;
	ws = scr->workspaces[workspace];
	if (ws->layout == layout)
		return;
	ws->layout = layout;
	if (layout == WLAYOUT_FLOAT)
		wLayoutRestore(scr, workspace);
	else
		wLayoutApply(scr, workspace);
	WMPostNotificationName(WMNWorkspaceLayoutChanged, scr,
			       (void *)(uintptr_t)workspace);
}

void wLayoutSetWindowTiled(WWindow *wwin, bool tiled)
{
	WScreen *scr;

	if (!wwin || !wwin->frame)
		return;
	scr = wwin->screen_ptr;
	if (tiled) {
		wwin->flags.no_tiling = 0;
	} else {
		wwin->flags.no_tiling = 1;
		if (wwin->flags.tiled)
			restore_tiled_window(wwin);
	}
	wLayoutApply(scr, WWIN_PRIMARY_WS(wwin));
}

void wLayoutSwapMaster(WScreen *scr, int workspace, WWindow *wwin)
{
	WWindow *wins[WLAYOUT_MAX_TILEABLE];
	int head, n;

	if (!scr || !wwin || !wwin->frame)
		return;
	if (workspace < 0 || workspace >= scr->workspace_count)
		return;
	if (scr->workspaces[workspace]->layout == WLAYOUT_FLOAT)
		return;
	/* the tileable list is built in focus-MRU order so the focused
	 * window is already the head, a re-apply is what promotes it into
	 * the master cell and demotes the previous master into the stack */
	head = wGetHeadForWindow(wwin);
	n = collect_tileable(scr, workspace, head, wins, WLAYOUT_MAX_TILEABLE);
	if (n < 2)
		return;
	wLayoutApply(scr, workspace);
}

#endif /* WLAYOUT_COMPUTE_ONLY */
