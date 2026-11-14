/*
 *  check_dockslots - unit tests for the pure dock slot decisions in
 *  src/dockslots.c. No display needed, the occupancy snapshots are
 *  pre-marked.
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

#include "dockslots.h"

#include "check_common.h"

#define ISZ 64
#define TH 3			/* DOCK_DETTACH_THRESHOLD */

static void test_slot_map(void)
{
	int mwidth, r;

	wDockSlotMapSize(64, &mwidth, &r);
	CHECK(mwidth == 2 * 8 && r == (16 - 1) / 2);

	/* the center cell indexes to the map center */
	CHECK(wDockSlotMapIndex(mwidth, r, 0, 0) == r * mwidth + r);
	/* one ring out */
	CHECK(wDockSlotMapIndex(mwidth, r, 1, 0) == r * mwidth + r + 1);
	CHECK(wDockSlotMapIndex(mwidth, r, 0, -1) == (r - 1) * mwidth + r);
	/* out-of-map cells collapse onto cell 0, matching the original
	 * macro's quirk */
	CHECK(wDockSlotMapIndex(mwidth, r, 99, 0) == 0);
}

static void test_free_slot_scan(void)
{
	int mwidth, r;
	unsigned char map[32 * 32];
	int xi, yi;

	wDockSlotMapSize(64, &mwidth, &r);

	/* empty map: the first cell in scan order, the top-left of the
	 * first ring, wins and the side pass does not overwrite it */
	memset(map, 0, sizeof(map));
	CHECK(wDockFindFreeSlotPure(mwidth, r, map, &xi, &yi));
	CHECK(xi == -1 && yi == -1);

	/* the whole first ring except the top-center cell: the top
	 * pass finds it before any side cell of the same ring */
	memset(map, 0, sizeof(map));
	map[wDockSlotMapIndex(mwidth, r, -1, -1)] = 1;
	map[wDockSlotMapIndex(mwidth, r, -1, 0)] = 1;
	map[wDockSlotMapIndex(mwidth, r, -1, 1)] = 1;
	map[wDockSlotMapIndex(mwidth, r, 0, 1)] = 1;
	map[wDockSlotMapIndex(mwidth, r, 1, -1)] = 1;
	map[wDockSlotMapIndex(mwidth, r, 1, 0)] = 1;
	map[wDockSlotMapIndex(mwidth, r, 1, 1)] = 1;
	CHECK(wDockFindFreeSlotPure(mwidth, r, map, &xi, &yi));
	CHECK(xi == 0 && yi == -1);

	/* a fully occupied first ring pushes the scan to the second
	 * ring, whose first free cell in scan order is its top-left */
	memset(map, 0, sizeof(map));
	{
		int x, y;

		for (y = -1; y <= 1; y++)
			for (x = -1; x <= 1; x++)
				map[wDockSlotMapIndex(mwidth, r, x, y)] = 1;
	}
	CHECK(wDockFindFreeSlotPure(mwidth, r, map, &xi, &yi));
	CHECK(xi == -2 && yi == -2);

	/* a fully occupied map leaves nothing within the scan radius */
	memset(map, 0, sizeof(map));
	{
		int x, y;

		for (y = -r; y <= r; y++)
			for (x = -r; x <= r; x++)
				map[wDockSlotMapIndex(mwidth, r, x, y)] = 1;
	}
	CHECK(!wDockFindFreeSlotPure(mwidth, r, map, &xi, &yi));
}

static void test_corner_border(void)
{
	unsigned char hmap[8];
	unsigned char vmap[8];
	int x, y;

	/* top-left corner: the first free vertical border slot maps to
	 * (0, +1) in corner signs */
	memset(hmap, 0, sizeof(hmap));
	memset(vmap, 0, sizeof(vmap));
	CHECK(wDockFindCornerSlotPure(WDOCKSLOT_C_NW, 8, 8, hmap, vmap, &x, &y));
	CHECK(x == 0 && y == 1);

	/* an occupied first vertical slot falls to the horizontal
	 * branch, which maps to (-1, 0) */
	vmap[1] = 1;
	CHECK(wDockFindCornerSlotPure(WDOCKSLOT_C_NW, 8, 8, hmap, vmap, &x, &y));
	CHECK(x == -1 && y == 0);

	/* both border axes fully occupied: no slot */
	memset(hmap, 0, sizeof(hmap));
	memset(vmap, 0, sizeof(vmap));
	{
		int i;

		for (i = 1; i < 8; i++) {
			hmap[i] = 1;
			vmap[i] = 1;
		}
	}
	CHECK(!wDockFindCornerSlotPure(WDOCKSLOT_C_NW, 8, 8, hmap, vmap, &x, &y));

	/* top-right corner translates the horizontal branch positive */
	memset(hmap, 0, sizeof(hmap));
	memset(vmap, 0, sizeof(vmap));
	vmap[1] = 1;
	CHECK(wDockFindCornerSlotPure(WDOCKSLOT_C_NE, 8, 8, hmap, vmap, &x, &y));
	CHECK(x == 1 && y == 0);

	/* bottom-left corner flips the vertical sign */
	memset(hmap, 0, sizeof(hmap));
	memset(vmap, 0, sizeof(vmap));
	CHECK(wDockFindCornerSlotPure(WDOCKSLOT_C_SW, 8, 8, hmap, vmap, &x, &y));
	CHECK(x == 0 && y == -1);
}

static void test_main_snap(void)
{
	WDockSnapResult res;
	bool icon_along[2 * TH + 1];
	bool drawer_along[2 * TH + 1];

	/* vertical dock on the left edge: xindex is the across axis (0
	 * on the dock strip), yindex the along axis */

	/* a new attach two slots down lands exactly there */
	memset(icon_along, 0, sizeof(icon_along));
	memset(drawer_along, 0, sizeof(drawer_along));
	res = wDockSnapMainPure(WDOCK_EDGE_LEFT, ISZ, 2, 0, 2 * ISZ + ISZ / 2,
				false, TH, icon_along, drawer_along);
	CHECK(res.accepted);
	CHECK(res.xindex == 0 && res.yindex == 2);

	/* a drop across the dock axis is refused for a new attach */
	res = wDockSnapMainPure(WDOCK_EDGE_LEFT, ISZ, 2, 1, 0,
				false, TH, icon_along, drawer_along);
	CHECK(!res.accepted);

	/* the requested slot taken by another appicon: the scan finds
	 * the nearest free slot, which on this vertical dock is one
	 * slot up */
	memset(icon_along, 0, sizeof(icon_along));
	memset(drawer_along, 0, sizeof(drawer_along));
	icon_along[TH] = true;
	icon_along[TH + 1] = true;
	res = wDockSnapMainPure(WDOCK_EDGE_LEFT, ISZ, 2, 0, 2 * ISZ + ISZ / 2,
				true, TH, icon_along, drawer_along);
	CHECK(res.accepted);
	CHECK(res.xindex == 0 && res.yindex == 1);

	/* a drawer on the requested slot refuses the whole snap */
	memset(icon_along, 0, sizeof(icon_along));
	memset(drawer_along, 0, sizeof(drawer_along));
	drawer_along[TH] = true;
	res = wDockSnapMainPure(WDOCK_EDGE_LEFT, ISZ, 2, 0, 2 * ISZ + ISZ / 2,
				true, TH, icon_along, drawer_along);
	CHECK(!res.accepted);

	/* redocking too far across the axis is refused */
	memset(icon_along, 0, sizeof(icon_along));
	memset(drawer_along, 0, sizeof(drawer_along));
	res = wDockSnapMainPure(WDOCK_EDGE_LEFT, ISZ, 2, TH + 1, 0,
				true, TH, icon_along, drawer_along);
	CHECK(!res.accepted);

	/* marked slots (taken or off-screen) are skipped by the scan in
	 * ring order: 2, 2, 3, 1, 4 */
	memset(icon_along, 0, sizeof(icon_along));
	memset(drawer_along, 0, sizeof(drawer_along));
	icon_along[TH] = true;
	icon_along[TH + 1] = true;
	icon_along[TH - 1] = true;
	res = wDockSnapMainPure(WDOCK_EDGE_LEFT, ISZ, 2, 0, 2 * ISZ + ISZ / 2,
				true, TH, icon_along, drawer_along);
	CHECK(res.accepted);
	CHECK(res.xindex == 0 && res.yindex == 4);
}

static void test_clip_snap(void)
{
	WDockSnapResult res;

	/* a neighbour near the drop cell accepts it */
	res = wDockSnapClipPure(3, 4, false, false, true, false);
	CHECK(res.accepted);
	CHECK(res.xindex == 3 && res.yindex == 4);

	/* no neighbour in the vicinity: refused */
	res = wDockSnapClipPure(3, 4, false, false, false, false);
	CHECK(!res.accepted);

	/* the cell is occupied by another icon: refused even with
	 * neighbours around */
	res = wDockSnapClipPure(3, 4, true, false, true, false);
	CHECK(!res.accepted);

	/* redocking onto the moving icon's own cell is accepted */
	res = wDockSnapClipPure(3, 4, true, true, true, true);
	CHECK(res.accepted);
}

static void test_drawer_snap(void)
{
	WDockSnapResult res;

	/* a drawer on a left-edge dock stacks along x: the drop three
	 * cells out lands there and the icon in the way slides toward
	 * the dock (the hole at 2 is below the drop) */
	res = wDockSnapDrawerPure(WDOCK_EDGE_LEFT, true, 3, 3, 2, false);
	CHECK(res.accepted);
	CHECK(res.xindex == 3 && res.yindex == 0);
	CHECK(res.shift_start == 3 && res.shift_count == 1);
	CHECK(res.shift_into_drawer == false);

	/* redocking clamps the requested cell to icon_count - 1 */
	res = wDockSnapDrawerPure(WDOCK_EDGE_LEFT, true, 5, 3, 1, true);
	CHECK(res.accepted);
	CHECK(res.xindex == 2 && res.yindex == 0);
	CHECK(res.shift_start == 2 && res.shift_count == 1);
	CHECK(res.shift_into_drawer == false);

	/* a hole at 1 with the drop at 3 shifts the two icons between
	 * them toward the dock */
	res = wDockSnapDrawerPure(WDOCK_EDGE_LEFT, true, 3, 4, 1, false);
	CHECK(res.accepted);
	CHECK(res.xindex == 3 && res.yindex == 0);
	CHECK(res.shift_start == 2 && res.shift_count == 2);
	CHECK(res.shift_into_drawer == false);

	/* drop nearer the dock than the hole: the icon slides deeper */
	res = wDockSnapDrawerPure(WDOCK_EDGE_LEFT, true, 1, 3, 2, false);
	CHECK(res.accepted);
	CHECK(res.xindex == 1 && res.yindex == 0);
	CHECK(res.shift_start == 1 && res.shift_count == 1);
	CHECK(res.shift_into_drawer == true);

	/* right-edge drawers carry negative indices */
	res = wDockSnapDrawerPure(WDOCK_EDGE_RIGHT, true, -1, 3, -2, false);
	CHECK(res.accepted);
	CHECK(res.xindex == -1 && res.yindex == 0);
	CHECK(res.shift_start == -1 && res.shift_count == 1);
	CHECK(res.shift_into_drawer == true);

	res = wDockSnapDrawerPure(WDOCK_EDGE_RIGHT, true, -3, 3, -1, false);
	CHECK(res.accepted);
	CHECK(res.xindex == -3 && res.yindex == 0);
	CHECK(res.shift_start == -3 && res.shift_count == 2);
	CHECK(res.shift_into_drawer == false);

	/* the wrong strip row refuses */
	res = wDockSnapDrawerPure(WDOCK_EDGE_LEFT, false, 3, 3, 1, false);
	CHECK(!res.accepted);

	/* across pointing away from the strip refuses */
	res = wDockSnapDrawerPure(WDOCK_EDGE_LEFT, true, -3, 3, 1, false);
	CHECK(!res.accepted);
}

static void test_clip_compact(void)
{
	int x[4], y[4];

	/* a line closes ranks keeping order: 1, 3, 5 becomes 1, 2, 3 */
	x[0] = -1; y[0] = 0;
	x[1] = -3; y[1] = 0;
	x[2] = -5; y[2] = 0;
	wDockCompactClipPure(x, y, 3);
	CHECK(x[0] == -1 && y[0] == 0);
	CHECK(x[1] == -2 && y[1] == 0);
	CHECK(x[2] == -3 && y[2] == 0);

	/* a run not touching the tile is pulled in: 2, 3 becomes 1, 2 */
	x[0] = 0; y[0] = 2;
	x[1] = 0; y[1] = 3;
	wDockCompactClipPure(x, y, 2);
	CHECK(x[0] == 0 && y[0] == 1);
	CHECK(x[1] == 0 && y[1] == 2);

	/* each line compacts on its own */
	x[0] = -2; y[0] = 0;
	x[1] = 0; y[1] = 3;
	x[2] = 2; y[2] = 2;
	x[3] = 4; y[3] = 4;
	wDockCompactClipPure(x, y, 4);
	CHECK(x[0] == -1 && y[0] == 0);
	CHECK(x[1] == 0 && y[1] == 1);
	CHECK(x[2] == 1 && y[2] == 1);
	CHECK(x[3] == 2 && y[3] == 2);

	/* a cell not on a line through the tile is left alone */
	x[0] = 2; y[0] = 1;
	x[1] = -3; y[1] = 0;
	wDockCompactClipPure(x, y, 2);
	CHECK(x[0] == 2 && y[0] == 1);
	CHECK(x[1] == -1 && y[1] == 0);
}

int main(void)
{
	test_slot_map();
	test_free_slot_scan();
	test_corner_border();
	test_main_snap();
	test_clip_snap();
	test_drawer_snap();
	test_clip_compact();

	return check_report("check_dockslots");
}
