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

/*
 * Pure workspace-switch decision core. Bodies moved verbatim from
 * workspace.c, and the map/unmap/icon decisions and the focus claims
 * are emitted as steps, the selected-window move and the omnipresent
 * retag stay in the applier.
 */

#include "wconfig.h"

#include "wsplan.h"

void wWorkspaceWindowDecision(bool per_head, bool sticky_icons,
			      const WWsWinIn *win,
			      WWsStep *out, int max, int *count)
{
	int n = 0;

	if (per_head) {
		if (!win->on_target && !win->selected) {
			if (((win->mapped || win->shaded) && !win->omnipresent &&
			     !win->changing_workspace) ||
			    (!sticky_icons && win->miniaturized && win->has_icon &&
			     !win->omnipresent)) {
				if (n < max) {
					out[n].win = win->win;
					out[n].kind = (win->miniaturized && win->has_icon) ? WWS_UNMAP_ICON : WWS_UNMAP;
					out[n].focus_priority = WWS_FOCUS_NONE;
					n++;
				}
			}
		} else {
			bool claim = false;

			if (win->selected) {
				if (!win->miniaturized)
					claim = true;
			} else if (!win->hidden) {
				if (!(win->mapped || win->miniaturized) &&
				    !win->group_inactive_member) {
					if (n < max) {
						out[n].win = win->win;
						out[n].kind = WWS_REMAP;
						out[n].focus_priority =
						    (!win->no_focusable && win->effective_head_matches)
						    ? WWS_FOCUS_PRIMARY : WWS_FOCUS_NONE;
						n++;
					}
				}
				if (!sticky_icons && win->miniaturized && !win->omnipresent &&
				    win->has_icon) {
					if (n < max) {
						out[n].win = win->win;
						out[n].kind = WWS_MAP_ICON;
						out[n].focus_priority = WWS_FOCUS_NONE;
						n++;
					}
				}
			}
			/* Prefer a focus candidate on the initiating head */
			if ((win->mapped || win->shaded) && !win->no_focusable &&
			    win->effective_head_matches)
				claim = true;

			if (claim && n < max) {
				out[n].win = win->win;
				out[n].kind = WWS_KEEP;
				out[n].focus_priority = WWS_FOCUS_PRIMARY;
				n++;
			}
		}
		*count = n;
		return;
	}

	/* single-workspace path: the fallback (foc2) claim runs for
	 * every on-target window before the branch, as the original
	 * records it */
	if (win->on_target && (win->mapped || win->shaded) && !win->no_focusable) {
		if (n < max) {
			out[n].win = win->win;
			out[n].kind = WWS_KEEP;
			out[n].focus_priority = WWS_FOCUS_FALLBACK;
			n++;
		}
	}

	if (!win->on_target && !win->selected) {
		if (((win->mapped || win->shaded) && !win->changing_workspace) ||
		    (!sticky_icons && win->miniaturized && win->has_icon)) {
			if (n < max) {
				out[n].win = win->win;
				out[n].kind = (win->miniaturized && win->has_icon) ? WWS_UNMAP_ICON : WWS_UNMAP;
				out[n].focus_priority = WWS_FOCUS_NONE;
				n++;
			}
		}
	} else {
		if (win->selected) {
			if (!win->miniaturized && n < max) {
				out[n].win = win->win;
				out[n].kind = WWS_KEEP;
				out[n].focus_priority = WWS_FOCUS_PRIMARY;
				n++;
			}
		} else if (!win->hidden) {
			if (!(win->mapped || win->miniaturized) && !win->group_inactive_member) {
				if (n < max) {
					out[n].win = win->win;
					out[n].kind = WWS_REMAP;
					out[n].focus_priority = win->no_focusable
					    ? WWS_FOCUS_NONE : WWS_FOCUS_PRIMARY;
					n++;
				}
			}
			/* Also map miniwindow if not omnipresent */
			if (!sticky_icons && win->miniaturized && !win->omnipresent &&
			    win->has_icon) {
				if (n < max) {
					out[n].win = win->win;
					out[n].kind = WWS_MAP_ICON;
					out[n].focus_priority = WWS_FOCUS_NONE;
					n++;
				}
			}
		}
	}
	*count = n;
}

bool wWsLayoutPosition(int rows, int cols, int orientation, int corner,
		       int index, int *row, int *col)
{
	int r, c;

	if (rows < 1 || cols < 1 || index < 0 || index >= rows * cols)
		return false;

	if (orientation == 1) {
		c = index / rows;
		r = index % rows;
	} else {
		r = index / cols;
		c = index % cols;
	}
	if (corner == 1 || corner == 2)
		c = cols - 1 - c;
	if (corner == 2 || corner == 3)
		r = rows - 1 - r;

	*row = r;
	*col = c;
	return true;
}

int wWsLayoutIndex(int rows, int cols, int orientation, int corner, int r, int c)
{
	if (corner == 1 || corner == 2)
		c = cols - 1 - c;
	if (corner == 2 || corner == 3)
		r = rows - 1 - r;

	if (orientation == 1)
		return c * rows + r;
	return r * cols + c;
}

int wWsNeighbour(int rows, int cols, int orientation, int corner,
		 int workspace_count, bool cycle, int from, int dcol, int drow)
{
	int r, c, to;

	if (!wWsLayoutPosition(rows, cols, orientation, corner, from, &r, &c))
		return -1;

	c += dcol;
	r += drow;

	if (cycle) {
		c = ((c % cols) + cols) % cols;
		r = ((r % rows) + rows) % rows;
	} else if (c < 0 || c >= cols || r < 0 || r >= rows) {
		return -1;
	}

	to = wWsLayoutIndex(rows, cols, orientation, corner, r, c);
	if (to >= workspace_count) {
		if (!cycle)
			return -1;
		to %= workspace_count;
	}
	return to;
}

int wWsRelativeOccupiedTarget(int workspace_count, int from_ws, int amount,
			      bool cycle, const bool *occupied, const bool *allowed)
{
	int dir = (amount > 0) ? 1 : -1;
	int w = from_ws;
	int steps;

	if (amount == 0 || workspace_count <= 0)
		return -1;

	for (steps = 0; steps < workspace_count + 1; steps++) {
		w += dir;
		if (w < 0) {
			if (!cycle)
				return -1;
			w = workspace_count - 1;
		} else if (w >= workspace_count) {
			if (!cycle)
				return -1;
			w = 0;
		}
		if (occupied[w] && allowed[w]) {
			amount -= dir;
			if (amount == 0)
				return w;
		}
	}
	return -1;
}

int wWsDirectionalTarget(int rows, int cols, int orientation, int corner,
			 int workspace_count, int from_ws, int dcol, int drow,
			 bool cycle, bool skip_empty,
			 const bool *occupied, const bool *allowed)
{
	int from = from_ws;
	int to;
	int steps;

	if (workspace_count <= 0)
		return -1;

	for (steps = 0; steps < workspace_count; steps++) {
		to = wWsNeighbour(rows, cols, orientation, corner,
				  workspace_count, cycle, from, dcol, drow);
		if (to < 0 || to == from_ws)
			return -1;
		if (allowed[to] && (!skip_empty || occupied[to]))
			return to;
		from = to;
	}
	return -1;
}
