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

#ifndef WMDOCKSLOTS_H_
#define WMDOCKSLOTS_H_

#include "dockgeom.h"

/*
 * Pure dock slot policy, extracted from dock.c. The appliers build the
 * occupancy snapshot (icon slots, global icons, drawers, screen
 * bounds, clip corners) and perform every window operation, these
 * entry points only decide.
 */

/* clip corner classification for the free-slot border scan */
#define WDOCKSLOT_C_NONE 0
#define WDOCKSLOT_C_NW 1
#define WDOCKSLOT_C_NE 2
#define WDOCKSLOT_C_SW 3
#define WDOCKSLOT_C_SE 4

typedef struct {
	bool accepted;
	int xindex, yindex;
	int shift_start, shift_count;	/* drawer result: the affected
					 * across range, cell indices */
	bool shift_into_drawer;		/* drawer shift direction */
} WDockSnapResult;

/* slot map geometry for the circular free-slot search */
void wDockSlotMapSize(int max_icons, int *mwidth, int *r);
int wDockSlotMapIndex(int mwidth, int r, int x, int y);

/* circular free-slot scan over a pre-marked map. Cells marked with
 * wDockSlotMapIndex that are occupied or off-screen are skipped */
bool wDockFindFreeSlotPure(int mwidth, int r, const unsigned char *slot_map,
			   int *xindex, int *yindex);

/* corner border scan over pre-marked hmap/vmap (index 0 unused),
 * returning the corner's signed slot indices */
bool wDockFindCornerSlotPure(int corner, int hcount, int vcount,
			     const unsigned char *hmap, const unsigned char *vmap,
			     int *x_pos, int *y_pos);

/* renumber each line through the tile onto a contiguous 1..n run */
void wDockCompactClipPure(int *x, int *y, int n);

/* main dock snap decision along the dock axis. icon_along[i] (i in
 * [0, 2*detach_threshold], the requested slot at index
 * detach_threshold) is set when another appicon sits at that along
 * slot or the slot is not fully on screen, and drawer_along[i] when a
 * drawer occupies it. req_along is the raw along-axis request used to
 * pick the scan's starting side. */
WDockSnapResult wDockSnapMainPure(WDockEdge edge, int icon_size,
				  int along, int across, int req_along,
				  bool redocking, int detach_threshold,
				  const bool *icon_along,
				  const bool *drawer_along);

/* clip snap decision at the requested cell */
WDockSnapResult wDockSnapClipPure(int req_x, int req_y,
				  bool occupied_at, bool occupied_by_self,
				  bool has_neighbour, bool redocking);

/* drawer snap decision: across_req is the requested across cell and
 * hole the moving icon's across index, returning the reduced across
 * and the across range the applier must slide */
WDockSnapResult wDockSnapDrawerPure(WDockEdge edge, bool along_ok,
				    int across_req, int icon_count, int hole,
				    bool redocking);
#endif
