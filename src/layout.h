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

#ifndef _WMLAYOUT_H_
#define _WMLAYOUT_H_

#include "WindowMaker.h"

typedef enum {
	WLAYOUT_FLOAT,
	WLAYOUT_TILE,
	WLAYOUT_TILE_H,
	WLAYOUT_GRID,
	WLAYOUT_MONOCLE,
	WLAYOUT_COUNT
} WLayout;

#define WLAYOUT_RATIO_MIN     0.10f
#define WLAYOUT_RATIO_MAX     0.90f
#define WLAYOUT_RATIO_DEFAULT 0.55f
#define WLAYOUT_RATIO_STEP    0.05f

typedef struct {
	int x, y, w, h;
} WLayoutRect;

/* Fill out[0..n-1] with frame-outer rects inside 'usable'.
 * n == 0 is a no-op. Returns the number of rects written (== n).
 * WLAYOUT_FLOAT writes nothing and returns 0. */
int wLayoutCompute(WLayout layout, WArea usable,
                   int master_count, float master_ratio,
                   int n, WLayoutRect *out);

const char *wLayoutName(WLayout layout);
/* human-readable label for menus and OSDs, untranslated (mark the
 * literals with N_ in layout.c, translate at the use site) */
const char *wLayoutDisplayName(WLayout layout);
WLayout wLayoutFromName(const char *name);

struct _WScreen;
struct WWindow;

/* Collect tileable windows on (scr, workspace) per head in focus-MRU order,
 * compute, then wWindowConfigure each. No-op when layout == WLAYOUT_FLOAT. */
void wLayoutApply(struct _WScreen *scr, int workspace);

/* Return the visible member workspace on the window's head, or -1. */
int wLayoutWorkspaceForWindow(struct WWindow *wwin);

/* Reapply the workspace currently visible on the window's head. */
void wLayoutApplyForWindow(struct WWindow *wwin);

/* Restore every tiled window on the workspace to its saved old_geometry.
 * Called when the workspace layout is switched back to WLAYOUT_FLOAT. */
void wLayoutRestore(struct _WScreen *scr, int workspace);

/* Mark the window opted-out and, when opting back in, drop maximized state
 * so it re-enters the layout cleanly. Calls wLayoutApply(). */
void wLayoutSetWindowTiled(struct WWindow *wwin, bool tiled);

/* Change the workspace layout, restoring old geometries when going to
 * WLAYOUT_FLOAT and re-applying otherwise. */
void wLayoutSetWorkspace(struct _WScreen *scr, int workspace, WLayout layout);

/* Swap the focused tileable window with the first master slot (focus-MRU
 * head) and re-apply. */
void wLayoutSwapMaster(struct _WScreen *scr, int workspace, struct WWindow *wwin);

#endif
