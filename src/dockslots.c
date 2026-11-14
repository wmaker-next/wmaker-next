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
 * Pure dock slot decisions. Bodies moved verbatim from dock.c, and the
 * occupancy snapshot and the screen-boundary checks are pre-computed
 * by the applier.
 */

#include "wconfig.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "dockslots.h"

void wDockSlotMapSize(int max_icons, int *mwidth, int *r)
{
	int mw = (int)ceil(sqrt(max_icons));

	/* In the worst case (the clip is in the corner of the screen),
	 * the amount of icons that fit in the clip is smaller.
	 * Double the map to get a safe value.
	 */
	mw += mw;

	*r = (mw - 1) / 2;
	*mwidth = mw;
}

int wDockSlotMapIndex(int mwidth, int r, int x, int y)
{
	return (WMAX(abs(x), abs(y)) > r) ? 0 : (((y) + r) * (mwidth) + (x) + r);
}

bool wDockFindFreeSlotPure(int mwidth, int r, const unsigned char *slot_map,
			   int *xindex, int *yindex)
{
	int x, y, i, done = 0;

	/* Find closest slot from the center that is free by scanning the
	 * map from the center to outward in circular passes.
	 * This will not result in a neat layout, but will be optimal
	 * in the sense that there will not be holes left.
	 */
	for (i = 1; i <= r && !done; i++) {
		/* top and bottom parts of the ring */
		for (x = -i; x <= i && !done; x++) {
			y = -i;
			if (slot_map[wDockSlotMapIndex(mwidth, r, x, y)] == 0) {
				*xindex = x;
				*yindex = y;
				done = 1;
				break;
			}
			y = i;
			if (slot_map[wDockSlotMapIndex(mwidth, r, x, y)] == 0) {
				*xindex = x;
				*yindex = y;
				done = 1;
				break;
			}
		}
		/* left and right parts of the ring */
		for (y = -i + 1; y <= i - 1 && !done; y++) {
			x = -i;
			if (slot_map[wDockSlotMapIndex(mwidth, r, x, y)] == 0) {
				*xindex = x;
				*yindex = y;
				done = 1;
				break;
			}
			x = i;
			if (slot_map[wDockSlotMapIndex(mwidth, r, x, y)] == 0) {
				*xindex = x;
				*yindex = y;
				done = 1;
				break;
			}
		}
	}
	return done;
}

bool wDockFindCornerSlotPure(int corner, int hcount, int vcount,
			     const unsigned char *hmap, const unsigned char *vmap,
			     int *x_pos, int *y_pos)
{
	int x = 0, y = 0, i, done = 0;

	/* search a vacant slot */
	for (i = 1; i < WMAX(vcount, hcount); i++) {
		if (i < vcount && vmap[i] == 0) {
			/* found a slot */
			x = 0;
			y = i;
			done = 1;
			break;
		} else if (i < hcount && hmap[i] == 0) {
			/* found a slot */
			x = i;
			y = 0;
			done = 1;
			break;
		}
	}

	/* If found a slot, translate to the corner's signed indices */
	if (done) {
		if (corner == WDOCKSLOT_C_NW || corner == WDOCKSLOT_C_NE)
			*y_pos = y;
		else
			*y_pos = -y;

		if (corner == WDOCKSLOT_C_NE || corner == WDOCKSLOT_C_SE)
			*x_pos = x;
		else
			*x_pos = -x;
	}

	return done;
}

static int cells_gcd(int x, int y)
{
	while (y != 0) {
		int t = x % y;

		x = y;
		y = t;
	}
	return x;
}

void wDockCompactClipPure(int *x, int *y, int n)
{
	int *ox, *oy;
	int i, j, gi, gj, rank;

	if (n < 1)
		return;

	ox = wmalloc(n * sizeof(int));
	oy = wmalloc(n * sizeof(int));
	memcpy(ox, x, n * sizeof(int));
	memcpy(oy, y, n * sizeof(int));

	for (i = 0; i < n; i++) {
		gi = cells_gcd(ox[i] < 0 ? -ox[i] : ox[i], oy[i] < 0 ? -oy[i] : oy[i]);
		if (gi == 0)
			continue;
		rank = 1;
		for (j = 0; j < n; j++) {
			if (j == i)
				continue;
			gj = cells_gcd(ox[j] < 0 ? -ox[j] : ox[j], oy[j] < 0 ? -oy[j] : oy[j]);
			if (gj == 0)
				continue;
			if (ox[j] * oy[i] != ox[i] * oy[j])
				continue;
			if (ox[j] * ox[i] + oy[j] * oy[i] <= 0)
				continue;
			if (gj < gi || (gj == gi && j < i))
				rank++;
		}
		x[i] = ox[i] / gi * rank;
		y[i] = oy[i] / gi * rank;
	}

	wfree(ox);
	wfree(oy);
}

/* ret for a WM_DOCK/WM_DRAWER snap: across on the across axis, along
 * on the along axis */
static void dock_snap_ret(WDockEdge edge, int *ret_x, int *ret_y,
			  int across, int along)
{
	if (wDockEdgeHorizontal(edge)) {
		*ret_x = along;
		*ret_y = across;
	} else {
		*ret_x = across;
		*ret_y = along;
	}
}

WDockSnapResult wDockSnapMainPure(WDockEdge edge, int icon_size,
				  int along, int across, int req_along,
				  bool redocking, int detach_threshold,
				  const bool *icon_along,
				  const bool *drawer_along)
{
	WDockSnapResult r;
	int th = detach_threshold;
	int sig, done, closest;
	int i;

	r.accepted = false;
	r.xindex = 0;
	r.yindex = 0;
	r.shift_start = 0;
	r.shift_count = 0;
	r.shift_into_drawer = false;

	if (!redocking && across != 0)
		return r;

	/* a drawer occupying the requested slot takes it */
	if (drawer_along[th])
		return r;

	if (!redocking) {
		/* if slot is free and the icon is close enough, return it */
		if (!icon_along[th] && across == 0) {
			dock_snap_ret(edge, &r.xindex, &r.yindex, 0, along);
			r.accepted = true;
		}
		return r;
	}

	/* Possible cases when redocking:
	 *
	 * icon dragged out of range of any slot -> rejected
	 * icon dragged on a drawer -> rejected (to open the drawer)
	 * icon dragged to range of free slot
	 * icon dragged to range of same slot
	 * icon dragged to range of different icon
	 */
	if (abs(across) > th)
		return r;

	/* the requested slot is free or holds the moving icon itself */
	if (!icon_along[th]) {
		dock_snap_ret(edge, &r.xindex, &r.yindex, 0, along);
		r.accepted = true;
		return r;
	}

	/* start looking at the upper slot or lower? */
	if (along * icon_size < req_along)
		sig = 1;
	else
		sig = -1;

	done = 0;
	/* look for closest free slot */
	for (i = 0; i < (th + 1) * 2 && !done; i++) {
		closest = sig * (i / 2) + along;
		/* check if this slot is fully on the screen and not used
		 * by another icon or a drawer */
		done = !icon_along[th + closest - along] &&
		       !drawer_along[th + closest - along];
		sig = -sig;
	}
	if (done &&
	    ((along >= closest && along - closest < th + 1)
	     || (along < closest && closest - along <= th + 1))) {
		dock_snap_ret(edge, &r.xindex, &r.yindex, 0, closest);
		r.accepted = true;
	}
	return r;
}

WDockSnapResult wDockSnapClipPure(int req_x, int req_y,
				  bool occupied_at, bool occupied_by_self,
				  bool has_neighbour, bool redocking)
{
	WDockSnapResult r;

	r.accepted = false;
	r.xindex = 0;
	r.yindex = 0;
	r.shift_start = 0;
	r.shift_count = 0;
	r.shift_into_drawer = false;

	if (has_neighbour && (!occupied_at || (redocking && occupied_by_self))) {
		r.accepted = true;
		r.xindex = req_x;
		r.yindex = req_y;
	}
	return r;
}

WDockSnapResult wDockSnapDrawerPure(WDockEdge edge, bool along_ok,
				    int across_req, int icon_count, int hole,
				    bool redocking)
{
	WDockSnapResult r;
	char axis;
	int sign;
	int across = across_req;

	r.accepted = false;
	r.xindex = 0;
	r.yindex = 0;
	r.shift_start = 0;
	r.shift_count = 0;
	r.shift_into_drawer = false;

	if (!along_ok)
		return r;

	wDockGeomAcrossSign(edge, &axis, &sign);

	/* the along-of-dock cell must stay on the strip row, and the
	 * across cell must point into the strip */
	if (abs(across) - icon_count > DOCK_DETTACH_THRESHOLD ||
	    across * sign < 0)
		return r;

	if (across == 0)
		across = sign;

	/* "Reduce" across but keep its sign */
	if (redocking) {
		if (abs(across) > icon_count - 1)	/* minus 1: do not take icon_array[0] into account */
			across = across * (icon_count - 1) / abs(across);	/* don't use *= ! */
	} else {
		if (abs(across) > icon_count)
			across = across * icon_count / abs(across);
	}

	r.accepted = true;
	dock_snap_ret(edge, &r.xindex, &r.yindex, across, 0);
	r.shift_into_drawer = (abs(hole) > abs(across));
	if (hole < across) {
		r.shift_start = hole + 1;
		r.shift_count = across - hole;
	} else {
		r.shift_start = across;
		r.shift_count = hole - across;
	}
	return r;
}
