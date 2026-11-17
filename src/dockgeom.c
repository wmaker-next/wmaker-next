/*
 *  dockgeom.c
 *
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

/* dockgeom.c - pure screen-edge geometry for the dock */

#include "wconfig.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "dockgeom.h"

bool wDockEdgeHorizontal(WDockEdge edge)
{
	return edge == WDOCK_EDGE_TOP || edge == WDOCK_EDGE_BOTTOM;
}

const char *wDockEdgeName(WDockEdge edge)
{
	switch (edge) {
	case WDOCK_EDGE_LEFT:
		return "Left";
	case WDOCK_EDGE_RIGHT:
		return "Right";
	case WDOCK_EDGE_TOP:
		return "Top";
	case WDOCK_EDGE_BOTTOM:
		return "Bottom";
	}
	return NULL;
}

bool wDockEdgeFromName(const char *str, WDockEdge *edge)
{
	WDockEdge e;

	if (!str)
		return false;

	for (e = WDOCK_EDGE_LEFT; e <= WDOCK_EDGE_BOTTOM; e++) {
		if (strcasecmp(str, wDockEdgeName(e)) == 0) {
			if (edge)
				*edge = e;
			return true;
		}
	}
	return false;
}

void wDockGeomAcrossSign(WDockEdge edge, char *axis, int *sign)
{
	char a;
	int s;

	switch (edge) {
	case WDOCK_EDGE_LEFT:
		a = 'x';
		s = 1;
		break;
	case WDOCK_EDGE_RIGHT:
		a = 'x';
		s = -1;
		break;
	case WDOCK_EDGE_TOP:
		a = 'y';
		s = 1;
		break;
	case WDOCK_EDGE_BOTTOM:
	default:
		a = 'y';
		s = -1;
		break;
	}

	if (axis)
		*axis = a;
	if (sign)
		*sign = s;
}

void wDockGeomOrigin(WMRect rect, WDockEdge edge, int icon_size, int extra,
		     int *x, int *y)
{
	int px, py;

	switch (edge) {
	case WDOCK_EDGE_RIGHT:
		px = rect.pos.x + rect.size.width - icon_size - extra;
		py = rect.pos.y + extra;
		break;
	case WDOCK_EDGE_TOP:
		px = rect.pos.x + extra;
		py = rect.pos.y + extra;
		break;
	case WDOCK_EDGE_BOTTOM:
		px = rect.pos.x + extra;
		py = rect.pos.y + rect.size.height - icon_size - extra;
		break;
	case WDOCK_EDGE_LEFT:
	default:
		px = rect.pos.x + extra;
		py = rect.pos.y + extra;
		break;
	}

	if (x)
		*x = px;
	if (y)
		*y = py;
}

WDockEdge wDockGeomNearestEdge(WMRect rect, int px, int py, WDockEdge current,
			       int icon_size)
{
	int dist[WDOCK_EDGE_BOTTOM + 1];
	WDockEdge e, best;
	int cur;

	if (px < rect.pos.x)
		px = rect.pos.x;
	else if (px >= rect.pos.x + rect.size.width)
		px = rect.pos.x + rect.size.width - 1;

	if (py < rect.pos.y)
		py = rect.pos.y;
	else if (py >= rect.pos.y + rect.size.height)
		py = rect.pos.y + rect.size.height - 1;

	dist[WDOCK_EDGE_LEFT] = px - rect.pos.x;
	dist[WDOCK_EDGE_RIGHT] = rect.pos.x + rect.size.width - 1 - px;
	dist[WDOCK_EDGE_TOP] = py - rect.pos.y;
	dist[WDOCK_EDGE_BOTTOM] = rect.pos.y + rect.size.height - 1 - py;

	best = WDOCK_EDGE_LEFT;
	for (e = WDOCK_EDGE_LEFT + 1; e <= WDOCK_EDGE_BOTTOM; e++)
		if (dist[e] < dist[best])
			best = e;

	if (current < WDOCK_EDGE_LEFT || current > WDOCK_EDGE_BOTTOM)
		return best;

	cur = dist[current];
	if (best != current && dist[best] <= cur - icon_size)
		return best;

	return current;
}

void wDockGeomTranspose(WDockEdge edge_from, WDockEdge edge_to,
			int *xindex, int *yindex)
{
	char axis;
	int sign;
	int xi, yi;

	xi = *xindex;
	yi = *yindex;

	if (wDockEdgeHorizontal(edge_from) != wDockEdgeHorizontal(edge_to)) {
		int t = xi;

		xi = yi;
		yi = t;
	}

	wDockGeomAcrossSign(edge_to, &axis, &sign);
	if (axis == 'x') {
		if (xi != 0) {
			xi = sign * abs(xi);
			yi = 0;
		}
	} else {
		if (yi != 0) {
			yi = sign * abs(yi);
			xi = 0;
		}
	}

	*xindex = xi;
	*yindex = yi;
}

void wDockGeomCompressAlong(int *cells, int *idx, int n, int new_cells)
{
	int min_cell, max_cell;
	int i, j;
	int num, den, pos_budget, neg_budget, cell, prev;

	if (n < 1 || new_cells < 1)
		return;

	min_cell = max_cell = cells[0];
	for (i = 1; i < n; i++) {
		if (cells[i] < min_cell)
			min_cell = cells[i];
		if (cells[i] > max_cell)
			max_cell = cells[i];
	}

	/* a layout that fits the run is left untouched */
	if (max_cell - min_cell < new_cells)
		return;

	/* stable insertion sort by cell, ties keep their input order */
	for (i = 1; i < n; i++) {
		int c = cells[i];
		int t = idx[i];

		j = i - 1;
		while (j >= 0 && cells[j] > c) {
			cells[j + 1] = cells[j];
			idx[j + 1] = idx[j];
			j--;
		}
		cells[j + 1] = c;
		idx[j + 1] = t;
	}

	num = new_cells - 1;
	den = max_cell - min_cell;
	pos_budget = (num * max_cell + den / 2) / den;
	neg_budget = num - pos_budget;

	/* positives pack upward from the main tile, negatives downward.
	 * Every entry keeps a cell of its own: entries past the last
	 * visible cell extend beyond the run instead of sharing one, so
	 * sliding the dock along the edge reaches them */
	prev = 0;
	for (i = 0; i < n; i++) {
		if (cells[i] < 0)
			continue;
		if (cells[i] == 0) {
			prev = 0;
			continue;
		}
		cell = (cells[i] * pos_budget + den / 2) / den;
		if (cell <= prev)
			cell = prev + 1;
		prev = cell;
		cells[i] = cell;
	}

	prev = 0;
	for (i = n - 1; i >= 0; i--) {
		if (cells[i] >= 0)
			continue;
		cell = -((-cells[i]) * neg_budget + den / 2) / den;
		if (cell >= prev)
			cell = prev - 1;
		prev = cell;
		cells[i] = cell;
	}
}

bool wDockHeadOnEdge(WMRect head, int root_w, int root_h, WDockEdge edge)
{
	int x2 = head.pos.x + (int)head.size.width;
	int y2 = head.pos.y + (int)head.size.height;

	switch (edge) {
	case WDOCK_EDGE_LEFT:
		return head.pos.x == 0;
	case WDOCK_EDGE_RIGHT:
		return x2 == root_w;
	case WDOCK_EDGE_TOP:
		return head.pos.y == 0;
	case WDOCK_EDGE_BOTTOM:
		return y2 == root_h;
	}

	return false;
}

void wDockGeomStrut(WMRect rect, int root_w, int root_h, WDockEdge edge,
		    int thickness, uint32_t out[12])
{
	int start, end;

	memset(out, 0, sizeof(uint32_t) * 12);

	if (thickness <= 0 || root_w <= 0 || root_h <= 0 ||
	    rect.size.width <= 0 || rect.size.height <= 0)
		return;

	/* EWMH cannot describe an inner edge without reserving the space
	 * between it and the root edge. Do not publish a misleading strut. */
	if ((edge == WDOCK_EDGE_LEFT && rect.pos.x != 0) ||
	    (edge == WDOCK_EDGE_RIGHT && rect.pos.x + rect.size.width != root_w) ||
	    (edge == WDOCK_EDGE_TOP && rect.pos.y != 0) ||
	    (edge == WDOCK_EDGE_BOTTOM && rect.pos.y + rect.size.height != root_h))
		return;

	if (wDockEdgeHorizontal(edge)) {
		if (thickness > rect.size.height)
			thickness = rect.size.height;
		start = rect.pos.x;
		end = rect.pos.x + rect.size.width - 1;
		if (end > root_w - 1)
			end = root_w - 1;
	} else {
		if (thickness > rect.size.width)
			thickness = rect.size.width;
		start = rect.pos.y;
		end = rect.pos.y + rect.size.height - 1;
		if (end > root_h - 1)
			end = root_h - 1;
	}
	if (start < 0)
		start = 0;
	if (start > end)
		return;

	switch (edge) {
	case WDOCK_EDGE_LEFT:
		out[0] = thickness;	/* left */
		out[4] = start;		/* left_start */
		out[5] = end;		/* left_end */
		break;
	case WDOCK_EDGE_RIGHT:
		out[1] = thickness;	/* right */
		out[6] = start;		/* right_start */
		out[7] = end;		/* right_end */
		break;
	case WDOCK_EDGE_TOP:
		out[2] = thickness;	/* top */
		out[8] = start;		/* top_start */
		out[9] = end;		/* top_end */
		break;
	case WDOCK_EDGE_BOTTOM:
		out[3] = thickness;	/* bottom */
		out[10] = start;	/* bottom_start */
		out[11] = end;		/* bottom_end */
		break;
	}
}

int wDockGeomFloorDiv(int num, int den)
{
	int q = num / den;

	if (num % den != 0 && num < 0)
		q--;

	return q;
}

int wDockGeomHideDelta(WDockEdge edge, int root_lo, int root_hi,
		       int shown_origin, int max_across, int icon_size,
		       int keep_visible)
{
	int chain_lo, chain_hi, extent;

	if (max_across < 0)
		max_across = 0;
	if (keep_visible < 0)
		keep_visible = 0;

	if (edge == WDOCK_EDGE_LEFT || edge == WDOCK_EDGE_TOP) {
		chain_lo = shown_origin;
		chain_hi = shown_origin + (max_across + 1) * icon_size;
		extent = chain_hi - chain_lo;
		if (keep_visible > extent)
			keep_visible = extent;

		return root_lo - chain_hi + keep_visible;
	}

	chain_lo = shown_origin - max_across * icon_size;
	chain_hi = shown_origin + icon_size;
	extent = chain_hi - chain_lo;
	if (keep_visible > extent)
		keep_visible = extent;

	return root_hi - chain_lo - keep_visible;
}
