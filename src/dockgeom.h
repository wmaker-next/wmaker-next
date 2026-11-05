/*
 *  dockgeom.h
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

/* dockgeom.h - pure screen-edge geometry for the dock
 *
 * Everything here is display independent: no X headers beyond the
 * WMRect struct, no WScreen calls, no I/O, so the unit can be tested
 * without a running display (src/Tests/check_dockgeom.c).
 */

#ifndef WMDOCKGEOM_H_
#define WMDOCKGEOM_H_

#include <stdbool.h>
#include <stdint.h>

#include <WINGs/WINGs.h>

typedef enum {
	WDOCK_EDGE_LEFT = 0,
	WDOCK_EDGE_RIGHT,
	WDOCK_EDGE_TOP,
	WDOCK_EDGE_BOTTOM
} WDockEdge;

/* Top or bottom, i.e. the dock is a horizontal shelf */
bool wDockEdgeHorizontal(WDockEdge edge);

/* "Left", "Right", "Top" or "Bottom", NULL outside the enum */
const char *wDockEdgeName(WDockEdge edge);

/* Case-insensitive parse. False and untouched *edge on unknown input,
 * so a typo cannot silently move the dock. */
bool wDockEdgeFromName(const char *str, WDockEdge *edge);

/* Axis ('x' or 'y') and sign of one "into the screen" drawer step:
 * Left +1 on X, Right -1 on X, Top +1 on Y, Bottom -1 on Y */
void wDockGeomAcrossSign(WDockEdge edge, char *axis, int *sign);

/* Pinned coordinate of the main tile for the edge, the free axis left
 * at the rect origin plus extra. Callers holding an along-axis
 * coordinate overwrite the free axis afterwards. */
void wDockGeomOrigin(WMRect rect, WDockEdge edge, int icon_size, int extra,
		     int *x, int *y);

/* Edge under (px, py) with one icon size of hysteresis against
 * current. The point is clamped to the rect before measuring, so a
 * pointer on another head does not select an edge of a rect it is not
 * in. Corners never flip the edge: switching requires the candidate
 * to be at least one icon size closer than the current one. */
WDockEdge wDockGeomNearestEdge(WMRect rect, int px, int py, WDockEdge current,
			       int icon_size);

/* Transpose one icon's screen-axis indices between two edges. A dock
 * icon (across magnitude 0) only swaps on an orientation change; a
 * drawer icon (across magnitude non-zero) is re-signed onto the new
 * across axis and the other index zeroed. Same-orientation calls only
 * rewrite the across sign. */
void wDockGeomTranspose(WDockEdge edge_from, WDockEdge edge_to,
			int *xindex, int *yindex);

/* Squeeze an along-axis layout onto a run of new_cells whole icons.
 * cells[] holds one entry per icon or drawer tile, idx[] is an opaque
 * tag permuted alongside. A layout that fits is untouched, otherwise
 * offsets map proportionally onto the run, the main tile stays at 0
 * and every entry keeps a cell of its own, past the run when it no
 * longer fits. */
void wDockGeomCompressAlong(int *cells, int *idx, int n, int new_cells);

/* The 12 _NET_WM_STRUT_PARTIAL CARDINALs in EWMH order (left, right,
 * top, bottom thicknesses, then the four start/end pairs). Thickness
 * is clipped to the head size on the dock's axis, the span is the
 * head's along-axis range in root coordinates, and the three unused
 * thicknesses are zero. */
void wDockGeomStrut(WMRect rect, int root_w, int root_h, WDockEdge edge,
		    int thickness, uint32_t out[12]);

bool wDockHeadOnEdge(WMRect head, int root_w, int root_h, WDockEdge edge);

#endif /* WMDOCKGEOM_H_ */
