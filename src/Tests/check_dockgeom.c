/*
 *  check_dockgeom.c
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

/*
 * check_dockgeom - unit tests for the pure dock edge geometry in
 * dockgeom.c. No display needed, the anchor rect is passed in.
 */

#include "wconfig.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dockgeom.h"

#include "check_common.h"

#define ISZ 64

/* local stand-in for wmkrect() so the test links without libWINGs */
static WMRect mkrect(int x, int y, unsigned int width, unsigned int height)
{
	WMRect r;

	r.pos.x = x;
	r.pos.y = y;
	r.size.width = width;
	r.size.height = height;
	return r;
}

static WMRect screen_rect(void)
{
	return mkrect(0, 0, 1920, 1080);
}

static WMRect head_rect(void)
{
	return mkrect(1920, 0, 800, 600);
}

static void test_origin_screen(void)
{
	WMRect r = screen_rect();
	int x, y;

	wDockGeomOrigin(r, WDOCK_EDGE_LEFT, ISZ, 0, &x, &y);
	CHECK(x == 0 && y == 0);
	wDockGeomOrigin(r, WDOCK_EDGE_RIGHT, ISZ, 0, &x, &y);
	CHECK(x == 1920 - ISZ && y == 0);
	wDockGeomOrigin(r, WDOCK_EDGE_TOP, ISZ, 0, &x, &y);
	CHECK(x == 0 && y == 0);
	wDockGeomOrigin(r, WDOCK_EDGE_BOTTOM, ISZ, 0, &x, &y);
	CHECK(x == 0 && y == 1080 - ISZ);

	wDockGeomOrigin(r, WDOCK_EDGE_LEFT, ISZ, 4, &x, &y);
	CHECK(x == 4 && y == 4);
	wDockGeomOrigin(r, WDOCK_EDGE_RIGHT, ISZ, 4, &x, &y);
	CHECK(x == 1920 - ISZ - 4 && y == 4);
	wDockGeomOrigin(r, WDOCK_EDGE_TOP, ISZ, 4, &x, &y);
	CHECK(x == 4 && y == 4);
	wDockGeomOrigin(r, WDOCK_EDGE_BOTTOM, ISZ, 4, &x, &y);
	CHECK(x == 4 && y == 1080 - ISZ - 4);
}

static void test_origin_head(void)
{
	WMRect r = head_rect();
	int x, y;

	wDockGeomOrigin(r, WDOCK_EDGE_LEFT, ISZ, 0, &x, &y);
	CHECK(x == 1920 && y == 0);
	wDockGeomOrigin(r, WDOCK_EDGE_RIGHT, ISZ, 0, &x, &y);
	CHECK(x == 1920 + 800 - ISZ && y == 0);
	wDockGeomOrigin(r, WDOCK_EDGE_TOP, ISZ, 0, &x, &y);
	CHECK(x == 1920 && y == 0);
	wDockGeomOrigin(r, WDOCK_EDGE_BOTTOM, ISZ, 0, &x, &y);
	CHECK(x == 1920 && y == 600 - ISZ);

	wDockGeomOrigin(r, WDOCK_EDGE_TOP, ISZ, 4, &x, &y);
	CHECK(x == 1924 && y == 4);
	wDockGeomOrigin(r, WDOCK_EDGE_BOTTOM, ISZ, 4, &x, &y);
	CHECK(x == 1924 && y == 600 - ISZ - 4);
}

static void test_horizontal_and_signs(void)
{
	char axis;
	int sign;

	CHECK(wDockEdgeHorizontal(WDOCK_EDGE_LEFT) == false);
	CHECK(wDockEdgeHorizontal(WDOCK_EDGE_RIGHT) == false);
	CHECK(wDockEdgeHorizontal(WDOCK_EDGE_TOP) == true);
	CHECK(wDockEdgeHorizontal(WDOCK_EDGE_BOTTOM) == true);

	wDockGeomAcrossSign(WDOCK_EDGE_LEFT, &axis, &sign);
	CHECK(axis == 'x' && sign == 1);
	wDockGeomAcrossSign(WDOCK_EDGE_RIGHT, &axis, &sign);
	CHECK(axis == 'x' && sign == -1);
	wDockGeomAcrossSign(WDOCK_EDGE_TOP, &axis, &sign);
	CHECK(axis == 'y' && sign == 1);
	wDockGeomAcrossSign(WDOCK_EDGE_BOTTOM, &axis, &sign);
	CHECK(axis == 'y' && sign == -1);
}

static void test_names(void)
{
	WDockEdge e;

	CHECK(strcmp(wDockEdgeName(WDOCK_EDGE_LEFT), "Left") == 0);
	CHECK(strcmp(wDockEdgeName(WDOCK_EDGE_RIGHT), "Right") == 0);
	CHECK(strcmp(wDockEdgeName(WDOCK_EDGE_TOP), "Top") == 0);
	CHECK(strcmp(wDockEdgeName(WDOCK_EDGE_BOTTOM), "Bottom") == 0);

	CHECK(wDockEdgeFromName("top", &e) && e == WDOCK_EDGE_TOP);
	CHECK(wDockEdgeFromName("BOTTOM", &e) && e == WDOCK_EDGE_BOTTOM);
	CHECK(wDockEdgeFromName("Left", &e) && e == WDOCK_EDGE_LEFT);
	CHECK(wDockEdgeFromName("right", &e) && e == WDOCK_EDGE_RIGHT);

	/* unknown is a failure, not a silent Right */
	e = WDOCK_EDGE_RIGHT;
	CHECK(!wDockEdgeFromName("Centre", &e));
	CHECK(e == WDOCK_EDGE_RIGHT);
	CHECK(!wDockEdgeFromName("", &e));
	CHECK(!wDockEdgeFromName(NULL, &e));
}

static void test_hysteresis(void)
{
	WMRect r = screen_rect();

	/* pointer at the top of a right-side anchor stays Right: the
	 * upgrade trap, a tie never flips the edge */
	CHECK(wDockGeomNearestEdge(r, 1919, 0, WDOCK_EDGE_RIGHT, ISZ)
	      == WDOCK_EDGE_RIGHT);

	/* clearly closer to the top than the right */
	CHECK(wDockGeomNearestEdge(r, 1700, 5, WDOCK_EDGE_RIGHT, ISZ)
	      == WDOCK_EDGE_TOP);

	/* one pixel past the corner toward the top does not switch */
	CHECK(wDockGeomNearestEdge(r, 1918, 10, WDOCK_EDGE_RIGHT, ISZ)
	      == WDOCK_EDGE_RIGHT);

	/* more than ICON_SIZE closer to the top switches */
	CHECK(wDockGeomNearestEdge(r, 1919 - ISZ - 1, 0, WDOCK_EDGE_RIGHT, ISZ)
	      == WDOCK_EDGE_TOP);

	/* the same in the other diagonal */
	CHECK(wDockGeomNearestEdge(r, 1919, 1079, WDOCK_EDGE_RIGHT, ISZ)
	      == WDOCK_EDGE_RIGHT);
	CHECK(wDockGeomNearestEdge(r, 1700, 1074, WDOCK_EDGE_RIGHT, ISZ)
	      == WDOCK_EDGE_BOTTOM);

	/* a pointer outside the rect is clamped, so it cannot select an
	 * edge of a head it is not in */
	CHECK(wDockGeomNearestEdge(head_rect(), 100, 100, WDOCK_EDGE_RIGHT, ISZ)
	      == WDOCK_EDGE_LEFT);
	CHECK(wDockGeomNearestEdge(head_rect(), 4000, 300, WDOCK_EDGE_LEFT, ISZ)
	      == WDOCK_EDGE_RIGHT);
}

static void test_transpose(void)
{
	int x, y;

	/* Right->Top of dock slots (0,0) (0,1) (0,2) */
	x = 0; y = 0;
	wDockGeomTranspose(WDOCK_EDGE_RIGHT, WDOCK_EDGE_TOP, &x, &y);
	CHECK(x == 0 && y == 0);
	x = 0; y = 1;
	wDockGeomTranspose(WDOCK_EDGE_RIGHT, WDOCK_EDGE_TOP, &x, &y);
	CHECK(x == 1 && y == 0);
	x = 0; y = 2;
	wDockGeomTranspose(WDOCK_EDGE_RIGHT, WDOCK_EDGE_TOP, &x, &y);
	CHECK(x == 2 && y == 0);

	/* Right->Top of a drawer slot (-2, 0) */
	x = -2; y = 0;
	wDockGeomTranspose(WDOCK_EDGE_RIGHT, WDOCK_EDGE_TOP, &x, &y);
	CHECK(x == 0 && y == 2);

	/* Right->Left of (-2, 0) re-signs only */
	x = -2; y = 0;
	wDockGeomTranspose(WDOCK_EDGE_RIGHT, WDOCK_EDGE_LEFT, &x, &y);
	CHECK(x == 2 && y == 0);

	/* Right->Left of a dock slot keeps the column */
	x = 0; y = 3;
	wDockGeomTranspose(WDOCK_EDGE_RIGHT, WDOCK_EDGE_LEFT, &x, &y);
	CHECK(x == 0 && y == 3);

	/* Top->Bottom flips the Y sign only */
	x = 0; y = 2;
	wDockGeomTranspose(WDOCK_EDGE_TOP, WDOCK_EDGE_BOTTOM, &x, &y);
	CHECK(x == 0 && y == -2);
	x = 2; y = 0;
	wDockGeomTranspose(WDOCK_EDGE_TOP, WDOCK_EDGE_BOTTOM, &x, &y);
	CHECK(x == 2 && y == 0);

	/* Top->Right of a drawer slot (0, +2) goes back to (-2, 0) */
	x = 0; y = 2;
	wDockGeomTranspose(WDOCK_EDGE_TOP, WDOCK_EDGE_RIGHT, &x, &y);
	CHECK(x == -2 && y == 0);

	/* Bottom->Top of a drawer slot (0, -3) goes to (0, +3) */
	x = 0; y = -3;
	wDockGeomTranspose(WDOCK_EDGE_BOTTOM, WDOCK_EDGE_TOP, &x, &y);
	CHECK(x == 0 && y == 3);
}

static void check_strut(WDockEdge edge, int want_t, int want_s, int want_e,
			int rx, int ry, int rw, int rh)
{
	uint32_t out[12];
	WMRect r = mkrect(rx, ry, rw, rh);

	wDockGeomStrut(r, 3840, 1080, edge, ISZ, out);
	CHECK(out[0] == (edge == WDOCK_EDGE_LEFT ? want_t : 0));
	CHECK(out[1] == (edge == WDOCK_EDGE_RIGHT ? want_t : 0));
	CHECK(out[2] == (edge == WDOCK_EDGE_TOP ? want_t : 0));
	CHECK(out[3] == (edge == WDOCK_EDGE_BOTTOM ? want_t : 0));
	CHECK(out[4] == (edge == WDOCK_EDGE_LEFT ? want_s : 0));
	CHECK(out[5] == (edge == WDOCK_EDGE_LEFT ? want_e : 0));
	CHECK(out[6] == (edge == WDOCK_EDGE_RIGHT ? want_s : 0));
	CHECK(out[7] == (edge == WDOCK_EDGE_RIGHT ? want_e : 0));
	CHECK(out[8] == (edge == WDOCK_EDGE_TOP ? want_s : 0));
	CHECK(out[9] == (edge == WDOCK_EDGE_TOP ? want_e : 0));
	CHECK(out[10] == (edge == WDOCK_EDGE_BOTTOM ? want_s : 0));
	CHECK(out[11] == (edge == WDOCK_EDGE_BOTTOM ? want_e : 0));
}

static void test_strut(void)
{
	/* full root */
	check_strut(WDOCK_EDGE_LEFT, ISZ, 0, 1079, 0, 0, 1920, 1080);
	check_strut(WDOCK_EDGE_RIGHT, ISZ, 0, 1079, 0, 0, 3840, 1080);
	check_strut(WDOCK_EDGE_TOP, ISZ, 0, 3839, 0, 0, 3840, 1080);
	check_strut(WDOCK_EDGE_BOTTOM, ISZ, 0, 3839, 0, 0, 3840, 1080);

	/* Inner edges cannot be expressed as root-edge struts. */
	check_strut(WDOCK_EDGE_LEFT, 0, 0, 0, 1920, 0, 800, 600);
	check_strut(WDOCK_EDGE_RIGHT, 0, 0, 0, 1920, 0, 800, 600);
	check_strut(WDOCK_EDGE_TOP, ISZ, 1920, 2719, 1920, 0, 800, 600);
	check_strut(WDOCK_EDGE_BOTTOM, 0, 0, 0, 1920, 0, 800, 600);
	check_strut(WDOCK_EDGE_RIGHT, ISZ, 0, 599, 3040, 0, 800, 600);
	check_strut(WDOCK_EDGE_BOTTOM, ISZ, 1920, 2719, 1920, 480, 800, 600);
	check_strut(WDOCK_EDGE_TOP, 0, 0, 0, 1920, 480, 800, 600);

	/* thickness clipped to the head size on the dock's axis */
	{
		uint32_t out[12];

		wDockGeomStrut(mkrect(0, 0, 800, 600), 3840, 1080, WDOCK_EDGE_LEFT,
			       10000, out);
		CHECK(out[0] == 800);
		wDockGeomStrut(head_rect(), 3840, 1080, WDOCK_EDGE_TOP,
			       10000, out);
		CHECK(out[2] == 600);
	}
}

static void test_head_on_edge(void)
{
	WMRect left = mkrect(0, 0, 1920, 1080);
	WMRect below = mkrect(0, 1080, 1920, 600);
	WMRect corner = mkrect(3040, 480, 800, 600);

	CHECK(wDockHeadOnEdge(left, 3840, 1680, WDOCK_EDGE_LEFT));
	CHECK(wDockHeadOnEdge(left, 3840, 1680, WDOCK_EDGE_TOP));
	CHECK(!wDockHeadOnEdge(left, 3840, 1680, WDOCK_EDGE_RIGHT));
	CHECK(!wDockHeadOnEdge(left, 3840, 1680, WDOCK_EDGE_BOTTOM));

	CHECK(wDockHeadOnEdge(below, 3840, 1680, WDOCK_EDGE_LEFT));
	CHECK(wDockHeadOnEdge(below, 3840, 1680, WDOCK_EDGE_BOTTOM));
	CHECK(!wDockHeadOnEdge(below, 3840, 1680, WDOCK_EDGE_TOP));

	CHECK(!wDockHeadOnEdge(corner, 3840, 1680, WDOCK_EDGE_TOP));
	CHECK(wDockHeadOnEdge(corner, 3840, 1680, WDOCK_EDGE_RIGHT));
	CHECK(!wDockHeadOnEdge(corner, 3840, 1680, WDOCK_EDGE_BOTTOM));
}

static void test_compress_fits(void)
{
	int seeds[3] = { 0, 5, 10 };
	int cells[4], idx[4], i;

	/* strictly inside the run: untouched */
	for (i = 0; i < 3; i++) {
		cells[i] = seeds[i];
		idx[i] = i;
	}
	wDockGeomCompressAlong(cells, idx, 3, 16);
	CHECK(cells[0] == 0 && cells[1] == 5 && cells[2] == 10);
	CHECK(idx[0] == 0 && idx[1] == 1 && idx[2] == 2);

	/* boundary: a span of 8 cells fits a run of 8 */
	cells[0] = 0;
	cells[1] = 7;
	idx[0] = 0;
	idx[1] = 1;
	wDockGeomCompressAlong(cells, idx, 2, 8);
	CHECK(cells[0] == 0 && cells[1] == 7);

	/* one cell over the run compresses */
	cells[0] = 0;
	cells[1] = 8;
	wDockGeomCompressAlong(cells, idx, 2, 8);
	CHECK(cells[0] == 0 && cells[1] == 7);

	/* a single tile and an empty call never divide by zero */
	cells[0] = 0;
	wDockGeomCompressAlong(cells, idx, 1, 1);
	CHECK(cells[0] == 0);
	wDockGeomCompressAlong(cells, idx, 0, 4);
}

static void test_compress_order(void)
{
	int cells[4] = { 0, 3, 6, 9 };
	int idx[4] = { 7, 8, 9, 10 };

	/* proportional squeeze keeps the order and shrinks the gaps */
	wDockGeomCompressAlong(cells, idx, 4, 5);
	CHECK(cells[0] == 0 && cells[1] == 1 && cells[2] == 3 && cells[3] == 4);
	CHECK(idx[0] == 7 && idx[1] == 8 && idx[2] == 9 && idx[3] == 10);
}

static void test_compress_no_shared_cells(void)
{
	int cells[4], idx[4], i;

	/* four icons onto a two-cell run: every icon keeps its own
	 * cell, the ones that do not fit extend past the run instead
	 * of stacking on the last visible cell */
	for (i = 0; i < 4; i++) {
		cells[i] = i;
		idx[i] = i;
	}
	wDockGeomCompressAlong(cells, idx, 4, 2);
	CHECK(cells[0] == 0 && cells[1] == 1 && cells[2] == 2 && cells[3] == 3);

	/* the same on the negative side: packing away from the main
	 * tile stays strictly monotonic */
	cells[0] = -5;
	cells[1] = -4;
	cells[2] = -3;
	cells[3] = 0;
	wDockGeomCompressAlong(cells, idx, 4, 2);
	CHECK(cells[0] == -3 && cells[1] == -2 && cells[2] == -1 && cells[3] == 0);

	/* all negative */
	cells[0] = -6;
	cells[1] = -4;
	cells[2] = -2;
	wDockGeomCompressAlong(cells, idx, 3, 2);
	CHECK(cells[0] == -3 && cells[1] == -2 && cells[2] == -1);
}

static void test_floor_div(void)
{
	CHECK(wDockGeomFloorDiv(0, 64) == 0);
	CHECK(wDockGeomFloorDiv(32, 64) == 0);
	CHECK(wDockGeomFloorDiv(63, 64) == 0);
	CHECK(wDockGeomFloorDiv(64, 64) == 1);
	CHECK(wDockGeomFloorDiv(96, 64) == 1);
	CHECK(wDockGeomFloorDiv(-1, 64) == -1);
	CHECK(wDockGeomFloorDiv(-32, 64) == -1);
	CHECK(wDockGeomFloorDiv(-63, 64) == -1);
	CHECK(wDockGeomFloorDiv(-64, 64) == -1);
	CHECK(wDockGeomFloorDiv(-65, 64) == -2);
	CHECK(wDockGeomFloorDiv(-96, 64) == -2);

	/* drawer-span rounding must floor, an aligned negative offset
	 * is a cell of its own */
	CHECK(wDockGeomFloorDiv(0 + ISZ / 2, ISZ) == 0);
	CHECK(wDockGeomFloorDiv(-32 + ISZ / 2, ISZ) == 0);
	CHECK(wDockGeomFloorDiv(-64 + ISZ / 2, ISZ) == -1);
	CHECK(wDockGeomFloorDiv(-96 + ISZ / 2, ISZ) == -1);
	CHECK(wDockGeomFloorDiv(-128 + ISZ / 2, ISZ) == -2);
	CHECK(wDockGeomFloorDiv(64 + ISZ / 2, ISZ) == 1);
}

/* the delta must place the chain on the exclusive boundary of
 * [root_lo, root_hi), leaving no on-root pixel */
static void check_hidden_outside(WDockEdge edge, int root_lo, int root_hi,
				 int shown_origin, int max_across)
{
	int delta = wDockGeomHideDelta(edge, root_lo, root_hi, shown_origin,
				       max_across, ISZ, 0);
	int origin = shown_origin + delta;

	if (edge == WDOCK_EDGE_LEFT || edge == WDOCK_EDGE_TOP) {
		int chain_hi = origin + (max_across + 1) * ISZ;

		CHECK(chain_hi <= root_lo);
		CHECK(chain_hi == root_lo);
	} else {
		int chain_lo = origin - max_across * ISZ;

		CHECK(chain_lo >= root_hi);
		CHECK(chain_lo == root_hi);
	}
}

static void test_hide_delta(void)
{
	CHECK(wDockGeomHideDelta(WDOCK_EDGE_LEFT, 0, 1920, 0, 0, ISZ, 0) == -ISZ);
	CHECK(wDockGeomHideDelta(WDOCK_EDGE_TOP, 0, 1080, 0, 0, ISZ, 0) == -ISZ);
	CHECK(wDockGeomHideDelta(WDOCK_EDGE_RIGHT, 0, 1920, 1920 - ISZ, 0, ISZ, 0) == ISZ);
	CHECK(wDockGeomHideDelta(WDOCK_EDGE_BOTTOM, 0, 1080, 1080 - ISZ, 0, ISZ, 0) == ISZ);

	CHECK(wDockGeomHideDelta(WDOCK_EDGE_LEFT, 0, 1920, 40, 0, ISZ, 0) < 0);
	CHECK(wDockGeomHideDelta(WDOCK_EDGE_TOP, 0, 1080, 40, 0, ISZ, 0) < 0);
	CHECK(wDockGeomHideDelta(WDOCK_EDGE_RIGHT, 0, 1920, 1800, 0, ISZ, 0) > 0);
	CHECK(wDockGeomHideDelta(WDOCK_EDGE_BOTTOM, 0, 1080, 900, 0, ISZ, 0) > 0);

	CHECK(wDockGeomHideDelta(WDOCK_EDGE_LEFT, 0, 1920, 0, 2, ISZ, 0) == -3 * ISZ);
	CHECK(wDockGeomHideDelta(WDOCK_EDGE_TOP, 0, 1080, 0, 1, ISZ, 0) == -2 * ISZ);
	CHECK(wDockGeomHideDelta(WDOCK_EDGE_RIGHT, 0, 1920, 1920 - ISZ, 2, ISZ, 0)
	      == 3 * ISZ);
	CHECK(wDockGeomHideDelta(WDOCK_EDGE_BOTTOM, 0, 1080, 1080 - ISZ, 3, ISZ, 0)
	      == 4 * ISZ);

	check_hidden_outside(WDOCK_EDGE_LEFT, 0, 1920, 0, 0);
	check_hidden_outside(WDOCK_EDGE_LEFT, 0, 1920, 300, 2);
	check_hidden_outside(WDOCK_EDGE_TOP, 0, 1080, 0, 0);
	check_hidden_outside(WDOCK_EDGE_TOP, 0, 1080, 500, 4);
	check_hidden_outside(WDOCK_EDGE_RIGHT, 0, 1920, 1920 - ISZ, 0);
	check_hidden_outside(WDOCK_EDGE_RIGHT, 0, 1920, 1500, 3);
	check_hidden_outside(WDOCK_EDGE_BOTTOM, 0, 1080, 1080 - ISZ, 0);
	check_hidden_outside(WDOCK_EDGE_BOTTOM, 0, 1080, 200, 1);

	/* interior primary-head origin, still hidden against the root */
	check_hidden_outside(WDOCK_EDGE_LEFT, 0, 1920, 1200, 0);
	check_hidden_outside(WDOCK_EDGE_LEFT, 0, 3840, 1920, 2);
	check_hidden_outside(WDOCK_EDGE_RIGHT, 0, 1920, 700, 0);
	check_hidden_outside(WDOCK_EDGE_RIGHT, 0, 3840, 2656, 1);
	check_hidden_outside(WDOCK_EDGE_TOP, 0, 1080, 600, 2);
	check_hidden_outside(WDOCK_EDGE_BOTTOM, 0, 1080, 300, 0);

	check_hidden_outside(WDOCK_EDGE_LEFT, 0, 1920, -40, 0);
	check_hidden_outside(WDOCK_EDGE_RIGHT, 0, 1920, 2400, 0);
}

/* the chain must leave exactly keep_visible pixels of its near tile
 * inside the root, and never more than the chain depth */
static void check_kept_visible(WDockEdge edge, int root_lo, int root_hi,
			       int shown_origin, int max_across, int keep)
{
	int delta = wDockGeomHideDelta(edge, root_lo, root_hi, shown_origin,
				       max_across, ISZ, keep);
	int origin = shown_origin + delta;
	int extent = (max_across + 1) * ISZ;
	int want = keep;

	if (want > extent)
		want = extent;

	if (edge == WDOCK_EDGE_LEFT || edge == WDOCK_EDGE_TOP) {
		int chain_hi = origin + extent;

		CHECK(chain_hi - root_lo == want);
		CHECK(chain_hi <= root_lo + extent);
	} else {
		int chain_lo = origin - max_across * ISZ;

		CHECK(root_hi - chain_lo == want);
		CHECK(chain_lo >= root_hi - extent);
	}
}

static void test_hide_delta_keep_visible(void)
{
	int i;

	/* zero keeps the full hide */
	CHECK(wDockGeomHideDelta(WDOCK_EDGE_LEFT, 0, 1920, 0, 0, ISZ, 0) == -ISZ);
	CHECK(wDockGeomHideDelta(WDOCK_EDGE_RIGHT, 0, 1920, 1920 - ISZ, 0, ISZ, 0) == ISZ);

	/* one icon left on screen shortens the delta by that much */
	CHECK(wDockGeomHideDelta(WDOCK_EDGE_LEFT, 0, 1920, 0, 0, ISZ, 10) == -ISZ + 10);
	CHECK(wDockGeomHideDelta(WDOCK_EDGE_TOP, 0, 1080, 0, 0, ISZ, 10) == -ISZ + 10);
	CHECK(wDockGeomHideDelta(WDOCK_EDGE_RIGHT, 0, 1920, 1920 - ISZ, 0, ISZ, 10) == ISZ - 10);
	CHECK(wDockGeomHideDelta(WDOCK_EDGE_BOTTOM, 0, 1080, 1080 - ISZ, 0, ISZ, 10) == ISZ - 10);

	/* a full-tile keep on a lone dock is the shown position */
	CHECK(wDockGeomHideDelta(WDOCK_EDGE_LEFT, 0, 1920, 0, 0, ISZ, ISZ) == 0);
	CHECK(wDockGeomHideDelta(WDOCK_EDGE_RIGHT, 0, 1920, 1920 - ISZ, 0, ISZ, ISZ) == 0);

	for (i = 0; i <= 4 * ISZ; i += ISZ / 4) {
		check_kept_visible(WDOCK_EDGE_LEFT, 0, 1920, 0, 0, i);
		check_kept_visible(WDOCK_EDGE_LEFT, 0, 1920, 300, 2, i);
		check_kept_visible(WDOCK_EDGE_TOP, 0, 1080, 0, 3, i);
		check_kept_visible(WDOCK_EDGE_RIGHT, 0, 1920, 1920 - ISZ, 0, i);
		check_kept_visible(WDOCK_EDGE_RIGHT, 0, 1920, 1500, 2, i);
		check_kept_visible(WDOCK_EDGE_BOTTOM, 0, 1080, 1080 - ISZ, 1, i);
		check_kept_visible(WDOCK_EDGE_LEFT, 0, 1920, 1200, 1, i);
	}

	/* the keep never pushes the chain past the shown position */
	CHECK(wDockGeomHideDelta(WDOCK_EDGE_LEFT, 0, 1920, 0, 0, ISZ, 10 * ISZ) == 0);
	CHECK(wDockGeomHideDelta(WDOCK_EDGE_RIGHT, 0, 1920, 1920 - ISZ, 2, ISZ, 10 * ISZ) == 0);
}

int main(void)
{
	test_origin_screen();
	test_origin_head();
	test_horizontal_and_signs();
	test_names();
	test_hysteresis();
	test_transpose();
	test_compress_fits();
	test_compress_order();
	test_compress_no_shared_cells();
	test_strut();
	test_head_on_edge();
	test_floor_div();
	test_hide_delta();
	test_hide_delta_keep_visible();

	return check_report("check_dockgeom");
}
