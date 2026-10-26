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
#include "config_values.h"

_Static_assert((int)WLAYOUT_FLOAT == (int)WVL_FLOAT
	       && (int)WLAYOUT_TILE == (int)WVL_TILE
	       && (int)WLAYOUT_TILE_H == (int)WVL_TILE_H
	       && (int)WLAYOUT_GRID == (int)WVL_GRID
	       && (int)WLAYOUT_MONOCLE == (int)WVL_MONOCLE,
	       "config_values.h WVL_* must match WLayout ordering");

/* Indexed by WLayout, matches the WConfig enum table in config_schema.c and
 * the WMState per-workspace Layout key. */
static const char *layout_names[WLAYOUT_COUNT] = {
	"float", "tile", "tileh", "grid", "monocle"
};

/* Human-readable labels, matching the WPrefs DefaultWorkspaceLayout
 * wording. Translated at the use site. */
static const char *const layout_display_names[WLAYOUT_COUNT] = {
	N_("Float"), N_("Tile"), N_("Tile wide"), N_("Grid"), N_("Monocle")
};

const char *wLayoutName(WLayout layout)
{
	if (layout < 0 || layout >= WLAYOUT_COUNT)
		return layout_names[WLAYOUT_FLOAT];
	return layout_names[layout];
}

const char *wLayoutDisplayName(WLayout layout)
{
	if (layout < 0 || layout >= WLAYOUT_COUNT)
		return layout_display_names[WLAYOUT_FLOAT];
	return layout_display_names[layout];
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
static int tile_master_count(int master_count, int n)
{
	if (master_count > n)
		master_count = n;
	if (master_count < 0)
		master_count = 0;
	return master_count;
}

static void grid_cols_rows(int n, int *cols, int *rows)
{
	*cols = 1;
	while (*cols * *cols < n)
		(*cols)++;
	*rows = (n + *cols - 1) / *cols;
}

static void compute_tile(WArea a, int master_count, float master_ratio,
			 int horiz, int n, WLayoutRect *out)
{
	int aw = a.x2 - a.x1;
	int ah = a.y2 - a.y1;
	int prim = horiz ? ah : aw;
	int sec = horiz ? aw : ah;
	int mprim, sy, sh, i, k;

	master_count = tile_master_count(master_count, n);

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

	grid_cols_rows(n, &cols, &rows);

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
	/* a tab group moves as one window through the peer sync, so only
	 * the active member is tileable, packing an inactive member would
	 * drag the group's visible frame into its cell */
	if (wwin->tab_group
	    && wwin->tab_group->active_index != wwin->tab_group_index)
		return false;
	return true;
}

/* Tileable and on the given head. head < 0 matches every head. */
static bool window_tileable_on_head(WWindow *wwin, int workspace, int head)
{
	if (!window_is_tileable(wwin, workspace))
		return false;
	if (head >= 0 && wGetHeadForWindow(wwin) != head)
		return false;
	return true;
}

/* Number of tileable windows on (workspace, head). head < 0 counts
 * across every head. */
static int count_tileable(WScreen *scr, int workspace, int head)
{
	WWindow *w;
	int n = 0;

	for (w = scr->focused_window; w; w = w->prev) {
		if (window_tileable_on_head(w, workspace, head))
			n++;
	}
	return n;
}

/* Fill wins[] (sized to at least count_tileable(scr, workspace, head))
 * with the tileable windows on (workspace, head) in focus-MRU order and
 * return the count. head < 0 collects across every head. */
static int collect_tileable(WScreen *scr, int workspace, int head,
			    WWindow **wins, int max)
{
	WWindow *w;
	int n = 0;

	for (w = scr->focused_window; w && n < max; w = w->prev) {
		if (window_tileable_on_head(w, workspace, head))
			wins[n++] = w;
	}
	return n;
}

static void apply_tiled_geometry(WWindow *wwin, int x, int y,
				 unsigned int cw, unsigned int ch, bool full,
				 int cell_w, int cell_h)
{
	int maxed;
	int il, ir, it, ib, fb, fw, fh, aw, ah;

	if (!wwin->flags.tiled) {
		if (!wwin->flags.maximized)
			save_old_geometry(wwin, SAVE_GEOMETRY_ALL);
		wwin->flags.tiled = 1;
	}
	/* a cell covering the whole usable area reports as maximized so
	 * the window menu, titlebar button and _NET_WM_STATE match what
	 * is on screen, but not when the size hints shrank the frame
	 * below the cell. Rounding to resize increments is tolerated */
	if (full) {
		wFrameInnerBorderWidths(wwin, &il, &ir, &it, &ib);
		fb = HAS_BORDER(wwin) ? wFrameBorderWidth(wwin) : 0;
		fw = (int)cw + il + ir + 2 * fb;
		fh = (int)ch + wwin->frame->top_width + wwin->frame->bottom_width
		     + it + ib + 2 * fb;
		aw = ah = 0;
		if (wwin->normal_hints && !wwin->flags.maximized) {
			if (wwin->normal_hints->width_inc > 1)
				aw = wwin->normal_hints->width_inc - 1;
			if (wwin->normal_hints->height_inc > 1)
				ah = wwin->normal_hints->height_inc - 1;
		}
		if (fw + aw < cell_w || fh + ah < cell_h)
			full = false;
	}
	maxed = full ? (MAX_VERTICAL | MAX_HORIZONTAL) : 0;
	if (wwin->flags.maximized != maxed || wwin->flags.old_maximized) {
		wwin->flags.old_maximized = 0;
		wwin->flags.maximized = maxed;
		wWindowUpdateButtonImages(wwin);
		WMPostNotificationName(WMNChangedState, wwin, "maximize");
	}
	wWindowConfigure(wwin, x, y, (int)cw, (int)ch);
	wWindowSynthConfigureNotify(wwin);
}

static void tiled_client_size(WWindow *wwin, int vw, int vh,
			      unsigned int *cw, unsigned int *ch)
{
	int il, ir, it, ib, bw;

	wFrameInnerBorderWidths(wwin, &il, &ir, &it, &ib);
	bw = HAS_BORDER(wwin) ? wFrameBorderWidth(wwin) : 0;
	vw -= il + ir + 2 * bw;
	vh -= wwin->frame->top_width + wwin->frame->bottom_width + it + ib + 2 * bw;
	*cw = (vw > 0) ? (unsigned int)vw : 1;
	*ch = (vh > 0) ? (unsigned int)vh : 1;
	wWindowConstrainSize(wwin, cw, ch);
}

static void configure_into_cell(WWindow *wwin, WLayoutRect r, bool full)
{
	unsigned int cw, ch;

	tiled_client_size(wwin, r.w, r.h, &cw, &ch);
	apply_tiled_geometry(wwin, r.x, r.y, cw, ch, full, r.w, r.h);
}

/* Pack one column of tiled windows with window_gap between neighbours.
 * Shares are measured from the real frame footprints so the rounding
 * size hints force on a client flows down the column instead of
 * leaving gaps. band is the column's extent along the primary axis,
 * and the return value is the column's footprint along that axis. */
static int pack_column(WWindow **wins, int cnt, bool pack_x,
		       int bx, int by, int band, WArea area, int gap)
{
	int pack_len = pack_x ? area.x2 - area.x1 : area.y2 - area.y1;
	int avail = pack_len - gap * (cnt - 1);
	int used = 0, wide = 0, fw, fh;
	int il, ir, it, ib, fb, share, vw, vh, fx, fy, i;
	bool full = (cnt == 1 && band == (pack_x ? area.y2 - area.y1
						  : area.x2 - area.x1));
	unsigned int cw, ch;
	WWindow *wwin;

	for (i = 0; i < cnt; i++) {
		wwin = wins[i];
		wFrameInnerBorderWidths(wwin, &il, &ir, &it, &ib);
		fb = HAS_BORDER(wwin) ? wFrameBorderWidth(wwin) : 0;
		share = (avail - used) / (cnt - i);
		if (pack_x) {
			vw = share;
			vh = band;
			fx = bx + used + i * gap;
			fy = by;
		} else {
			vw = band;
			vh = share;
			fx = bx;
			fy = by + used + i * gap;
		}
		tiled_client_size(wwin, vw, vh, &cw, &ch);
		apply_tiled_geometry(wwin, fx, fy, cw, ch, full, vw, vh);
		fw = (int)cw + il + ir + 2 * fb;
		fh = (int)ch + wwin->frame->top_width + wwin->frame->bottom_width
		     + it + ib + 2 * fb;
		used += pack_x ? fw : fh;
		if (wide < (pack_x ? fh : fw))
			wide = pack_x ? fh : fw;
	}
	return wide;
}

static void restore_tiled_window(WWindow *wwin)
{
	int x, y;
	unsigned int w, h;

	if (!wwin->flags.tiled)
		return;
	wwin->flags.tiled = 0;
	if (wwin->flags.maximized || wwin->flags.old_maximized) {
		wwin->flags.maximized = 0;
		wwin->flags.old_maximized = 0;
		wWindowUpdateButtonImages(wwin);
		WMPostNotificationName(WMNChangedState, wwin, "maximize");
	}
	x = wwin->old_geometry.x;
	y = wwin->old_geometry.y;
	w = wwin->old_geometry.width ? wwin->old_geometry.width : wwin->client.width;
	h = wwin->old_geometry.height ? wwin->old_geometry.height : wwin->client.height;
	wWindowConstrainSize(wwin, &w, &h);
	wWindowConfigure(wwin, x, y, (int)w, (int)h);
	wWindowSynthConfigureNotify(wwin);
}

/* A layout change on a workspace nobody is looking at must not move
 * the shared windows that are visible on another workspace, so the
 * apply and restore passes only run for a workspace currently shown.
 * The switch path re-applies (or restores) when the workspace becomes
 * visible, a stored layout never applies before that. */
static bool workspace_layout_visible(WScreen *scr, int workspace)
{
	int head;

	if (!w_global.config->per_head_workspaces)
		return workspace == scr->current_workspace;

	for (head = 0; head < wXineramaHeads(scr); head++)
		if (scr->head_workspace[head] == workspace)
			return true;
	return false;
}

void wLayoutApply(WScreen *scr, int workspace)
{
	WWorkspace *ws;
	WWindow **wins;
	WLayoutRect *out;
	WArea usable;
	int nwindows, n, i, c, cols, rows, head, gap, mc, band, used, cnt;
	bool pack_x;

	if (!scr || workspace < 0 || workspace >= scr->workspace_count)
		return;
	ws = scr->workspaces[workspace];
	if (ws->layout == WLAYOUT_FLOAT)
		return;
	if (!workspace_layout_visible(scr, workspace))
		return;

	/* each head collects a subset of the workspace population, so one
	 * population-sized pair of arrays serves every head */
	nwindows = count_tileable(scr, workspace, -1);
	if (nwindows == 0)
		return;
	wins = wmalloc(nwindows * sizeof(WWindow *));
	out = wmalloc(nwindows * sizeof(WLayoutRect));

	for (head = 0; head < wXineramaHeads(scr); head++) {
		n = collect_tileable(scr, workspace, head, wins, nwindows);
		if (n == 0)
			continue;
		usable = wGetUsableAreaForHead(scr, head, NULL, true);
		if (!wLayoutCompute(ws->layout, usable, ws->master_count,
				    ws->master_ratio, n, out))
			continue;
		gap = w_global.config->window_gap;
		if (ws->layout == WLAYOUT_MONOCLE) {
			for (i = 0; i < n; i++)
				configure_into_cell(wins[i], out[i], true);
			wRaiseFrame(wins[0]->frame->core);
			continue;
		}
		if (ws->layout == WLAYOUT_TILE || ws->layout == WLAYOUT_TILE_H) {
			pack_x = (ws->layout == WLAYOUT_TILE_H);
			mc = tile_master_count(ws->master_count, n);
			if (mc > 0 && mc < n) {
				band = pack_x ? out[0].h - gap : out[0].w - gap;
				used = pack_column(wins, mc, pack_x,
						   usable.x1, usable.y1,
						   band, usable, gap);
				band = (pack_x ? usable.y2 - usable.y1
					: usable.x2 - usable.x1) - used - gap;
				pack_column(wins + mc, n - mc, pack_x,
					    pack_x ? usable.x1 : usable.x1 + used + gap,
					    pack_x ? usable.y1 + used + gap : usable.y1,
					    band, usable, gap);
			} else {
				pack_column(wins, n, pack_x, usable.x1, usable.y1,
					    pack_x ? usable.y2 - usable.y1
						   : usable.x2 - usable.x1,
					    usable, gap);
			}
		} else {
			grid_cols_rows(n, &cols, &rows);
			used = 0;
			for (c = 0; c < cols; c++) {
				cnt = (c == cols - 1) ? n - c * rows : rows;
				band = (usable.x2 - usable.x1 - used
					- gap * (cols - 1)) / (cols - c);
				used += pack_column(wins + c * rows, cnt, false,
						    usable.x1 + used + c * gap,
						    usable.y1, band, usable, gap);
			}
		}
	}
	wfree(wins);
	wfree(out);
}

void wLayoutRestore(WScreen *scr, int workspace)
{
	WWindow *w;

	if (!scr)
		return;
	if (!workspace_layout_visible(scr, workspace))
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
	if (!scr || !wwin || !wwin->frame)
		return;
	if (workspace < 0 || workspace >= scr->workspace_count)
		return;
	if (scr->workspaces[workspace]->layout == WLAYOUT_FLOAT)
		return;
	/* the tileable list is built in focus-MRU order so the focused
	 * window is already the head, a re-apply is what promotes it into
	 * the master cell and demotes the previous master into the stack */
	if (count_tileable(scr, workspace, wGetHeadForWindow(wwin)) < 2)
		return;
	wLayoutApply(scr, workspace);
}

#endif /* WLAYOUT_COMPUTE_ONLY */
