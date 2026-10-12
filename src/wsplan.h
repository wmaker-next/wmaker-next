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

#ifndef WMWSPLAN_H_
#define WMWSPLAN_H_

#include "window.h"

/*
 * Pure workspace-switch policy core, extracted from workspace.c. The
 * applier walks the live window list, reduces each window into
 * WWsWinIn, applies the returned steps at their original traversal
 * point and drains the collected unmaps in reverse after the scan.
 */

/* maximum steps the per-window decision can emit */
#define WWS_MAX_STEPS 4

typedef enum {
	WWS_REMAP,		/* wWindowMap */
	WWS_UNMAP,		/* deferred drain, wWindowUnmap */
	WWS_MAP_ICON,		/* icon->mapped = 1 + xcb_map_window */
	WWS_UNMAP_ICON,		/* deferred drain, icon unmap */
	WWS_KEEP		/* decision-only carrier for focus claims */
} WWsStepKind;

typedef enum {
	WWS_FOCUS_NONE,
	WWS_FOCUS_PRIMARY,	/* claims the foc slot */
	WWS_FOCUS_FALLBACK	/* claims the foc2 slot, single-workspace path */
} WWsFocusPriority;

typedef struct {
	WWindow *win;		/* opaque handle for the applier */
	WWsStepKind kind;
	WWsFocusPriority focus_priority;
} WWsStep;

/* per-window decision inputs, pre-reduced by the applier */
typedef struct {
	WWindow *win;
	bool omnipresent, on_target, selected, changing_workspace;
	bool mapped, shaded, miniaturized, hidden, has_icon, no_focusable;
	bool group_inactive_member;
	bool effective_head_matches;
} WWsWinIn;

/* Evaluate one window at a time from the live traversal state. The
 * applier consumes immediate map decisions inline and collects unmaps
 * for the existing reverse-order drain. on_target means membership in
 * the target workspace in the single-workspace path and visibility on
 * at least one head in the per-head path. */
void wWorkspaceWindowDecision(bool per_head, bool sticky_icons,
			      const WWsWinIn *win,
			      WWsStep *out, int max, int *count);

/* _NET_DESKTOP_LAYOUT grid mapping, orientation 1 = column-major,
 * corner 0..3 = top-left, top-right, bottom-right, bottom-left */
bool wWsLayoutPosition(int rows, int cols, int orientation, int corner,
		       int index, int *row, int *col);
int wWsLayoutIndex(int rows, int cols, int orientation, int corner,
		   int r, int c);
int wWsNeighbour(int rows, int cols, int orientation, int corner,
		 int workspace_count, bool cycle, int from, int dcol, int drow);

/* Occupied-only relative walk. occupied/allowed are indexed by
 * workspace and pre-computed by the applier (allowed may require
 * occupancy only when the caller passes it pre-ANDed). */
int wWsRelativeOccupiedTarget(int workspace_count, int from_ws, int amount,
			      bool cycle, const bool *occupied,
			      const bool *allowed);

/* Directional switch decision loop: walks the grid from from_ws in the
 * requested direction until a workspace passes the allowed/occupied
 * test, and returns the target or -1 to stop. */
int wWsDirectionalTarget(int rows, int cols, int orientation, int corner,
			 int workspace_count, int from_ws, int dcol, int drow,
			 bool cycle, bool skip_empty,
			 const bool *occupied, const bool *allowed);
#endif
