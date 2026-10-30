/*
 *  check_layout.c
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
 * check_layout - table-driven coverage of wLayoutCompute().
 *
 * The compute pass is pure geometry with no X calls, so build it with
 * WLAYOUT_COMPUTE_ONLY defined and assert every rect for each layout at
 * a spread of window counts, master counts and ratio clamps.
 */

#include "wconfig.h"

#define WLAYOUT_COMPUTE_ONLY
#include "../layout.c"

#include "check_common.h"

static const WArea A = { 0, 0, 1200, 800 };

static void check_rects(const char *tag, WLayout layout, int mc, float mr,
			int n, const WLayoutRect *want)
{
	WLayoutRect got[16];
	int i, r;

	r = wLayoutCompute(layout, A, mc, mr, n, got);
	if (r != n) {
		fprintf(stderr, "FAIL %s: returned %d rects, want %d\n", tag, r, n);
		check_failures++;
		return;
	}
	for (i = 0; i < n; i++) {
		if (got[i].x != want[i].x || got[i].y != want[i].y
		    || got[i].w != want[i].w || got[i].h != want[i].h) {
			fprintf(stderr,
				"FAIL %s[%d]: got (%d,%d %dx%d) want (%d,%d %dx%d)\n",
				tag, i, got[i].x, got[i].y, got[i].w, got[i].h,
				want[i].x, want[i].y, want[i].w, want[i].h);
			check_failures++;
		}
	}
}

static void check_bounds(const char *tag, WLayout layout, int mc, float mr, int n)
{
	WLayoutRect got[16];
	int i, r;

	r = wLayoutCompute(layout, A, mc, mr, n, got);
	CHECK(r == n);
	for (i = 0; i < n; i++) {
		if (got[i].w <= 0 || got[i].h <= 0
		    || got[i].x < A.x1 || got[i].y < A.y1
		    || got[i].x + got[i].w > A.x2
		    || got[i].y + got[i].h > A.y2) {
			fprintf(stderr, "FAIL %s[%d]: (%d,%d %dx%d) not inside area\n",
				tag, i, got[i].x, got[i].y, got[i].w, got[i].h);
			check_failures++;
		}
	}
}

static void test_float_and_empty(void)
{
	WLayoutRect out[4];

	CHECK(wLayoutCompute(WLAYOUT_FLOAT, A, 1, 0.55f, 3, out) == 0);
	CHECK(wLayoutCompute(WLAYOUT_TILE, A, 1, 0.55f, 0, out) == 0);
	CHECK(wLayoutCompute(WLAYOUT_TILE, (WArea){ 0, 0, 0, 800 }, 1, 0.55f, 3, out) == 0);
}

static void test_tile(void)
{
	static const WLayoutRect n1[] = { { 0, 0, 1200, 800 } };
	static const WLayoutRect n2[] = {
		{ 0, 0, 660, 800 }, { 660, 0, 540, 800 }
	};
	static const WLayoutRect n3[] = {
		{ 0, 0, 660, 800 }, { 660, 0, 540, 400 }, { 660, 400, 540, 400 }
	};
	static const WLayoutRect n5[] = {
		{ 0, 0, 660, 800 },
		{ 660, 0, 540, 200 }, { 660, 200, 540, 200 },
		{ 660, 400, 540, 200 }, { 660, 600, 540, 200 }
	};
	/* master_count == 3 with n == 5 */
	static const WLayoutRect n5m3[] = {
		{ 0, 0, 660, 266 }, { 0, 266, 660, 267 }, { 0, 533, 660, 267 },
		{ 660, 0, 540, 400 }, { 660, 400, 540, 400 }
	};
	/* master_count == 0: whole width is the stack */
	static const WLayoutRect n3m0[] = {
		{ 0, 0, 1200, 266 }, { 0, 266, 1200, 267 }, { 0, 533, 1200, 267 }
	};
	/* master_count >= n: whole width is the master column */
	static const WLayoutRect n2m3[] = {
		{ 0, 0, 1200, 400 }, { 0, 400, 1200, 400 }
	};

	check_rects("tile n=1", WLAYOUT_TILE, 1, 0.55f, 1, n1);
	check_rects("tile n=2", WLAYOUT_TILE, 1, 0.55f, 2, n2);
	check_rects("tile n=3", WLAYOUT_TILE, 1, 0.55f, 3, n3);
	check_rects("tile n=5", WLAYOUT_TILE, 1, 0.55f, 5, n5);
	check_rects("tile n=5 mc=3", WLAYOUT_TILE, 3, 0.55f, 5, n5m3);
	check_rects("tile n=3 mc=0", WLAYOUT_TILE, 0, 0.55f, 3, n3m0);
	check_rects("tile n=2 mc=3", WLAYOUT_TILE, 3, 0.55f, 2, n2m3);
	check_rects("tile n=2 mc=n+1", WLAYOUT_TILE, 3, 0.55f, 2, n2m3);
	check_bounds("tile n=7", WLAYOUT_TILE, 1, 0.55f, 7);
}

static void test_tile_ratio_clamp(void)
{
	static const WLayoutRect lo[] = {
		{ 0, 0, 120, 800 }, { 120, 0, 1080, 800 }
	};
	static const WLayoutRect hi[] = {
		{ 0, 0, 1080, 800 }, { 1080, 0, 120, 800 }
	};

	check_rects("tile ratio<min", WLAYOUT_TILE, 1, 0.01f, 2, lo);
	check_rects("tile ratio>max", WLAYOUT_TILE, 1, 1.5f, 2, hi);
}

/* a NaN ratio must fall back to the default split and never reach the
 * undefined (int)(prim * NaN) conversion. Infinities clamp to the range. */
static void test_tile_ratio_nonfinite(void)
{
	static const WLayoutRect def[] = {
		{ 0, 0, 660, 800 }, { 660, 0, 540, 400 }, { 660, 400, 540, 400 }
	};

	check_rects("tile ratio nan", WLAYOUT_TILE, 1, (float)NAN, 3, def);
	check_bounds("tile ratio inf", WLAYOUT_TILE, 1, INFINITY, 3);
	check_bounds("tile ratio -inf", WLAYOUT_TILE, 1, -INFINITY, 3);
}

static void test_tile_h(void)
{
	static const WLayoutRect n3[] = {
		{ 0, 0, 1200, 440 }, { 0, 440, 600, 360 }, { 600, 440, 600, 360 }
	};

	check_rects("tileh n=3", WLAYOUT_TILE_H, 1, 0.55f, 3, n3);
	check_bounds("tileh n=7 mc=2", WLAYOUT_TILE_H, 2, 0.55f, 7);
}

static void test_grid(void)
{
	static const WLayoutRect n1[] = { { 0, 0, 1200, 800 } };
	static const WLayoutRect n2[] = {
		{ 0, 0, 600, 800 }, { 600, 0, 600, 800 }
	};
	/* n=3: cols=2 rows=2, last column has one cell full-height */
	static const WLayoutRect n3[] = {
		{ 0, 0, 600, 400 }, { 0, 400, 600, 400 }, { 600, 0, 600, 800 }
	};
	/* n=5: cols=3 rows=2, last column has one cell full-height */
	static const WLayoutRect n5[] = {
		{ 0, 0, 400, 400 }, { 0, 400, 400, 400 },
		{ 400, 0, 400, 400 }, { 400, 400, 400, 400 },
		{ 800, 0, 400, 800 }
	};

	check_rects("grid n=1", WLAYOUT_GRID, 1, 0.55f, 1, n1);
	check_rects("grid n=2", WLAYOUT_GRID, 1, 0.55f, 2, n2);
	check_rects("grid n=3", WLAYOUT_GRID, 1, 0.55f, 3, n3);
	check_rects("grid n=5", WLAYOUT_GRID, 1, 0.55f, 5, n5);
	check_bounds("grid n=7", WLAYOUT_GRID, 1, 0.55f, 7);
}

static void test_monocle(void)
{
	WLayoutRect out[5];
	int i;

	CHECK(wLayoutCompute(WLAYOUT_MONOCLE, A, 1, 0.55f, 5, out) == 5);
	for (i = 0; i < 5; i++) {
		CHECK(out[i].x == 0 && out[i].y == 0);
		CHECK(out[i].w == 1200 && out[i].h == 800);
	}
}

static void test_names(void)
{
	CHECK_STR(wLayoutName(WLAYOUT_FLOAT), "float");
	CHECK_STR(wLayoutName(WLAYOUT_TILE), "tile");
	CHECK_STR(wLayoutName(WLAYOUT_TILE_H), "tileh");
	CHECK_STR(wLayoutName(WLAYOUT_GRID), "grid");
	CHECK_STR(wLayoutName(WLAYOUT_MONOCLE), "monocle");
	CHECK_STR(wLayoutName(-1), "float");
	CHECK_STR(wLayoutName(WLAYOUT_COUNT), "float");
	CHECK_STR(wLayoutDisplayName(WLAYOUT_FLOAT), "Float");
	CHECK_STR(wLayoutDisplayName(WLAYOUT_TILE), "Tile");
	CHECK_STR(wLayoutDisplayName(WLAYOUT_TILE_H), "Tile wide");
	CHECK_STR(wLayoutDisplayName(WLAYOUT_GRID), "Grid");
	CHECK_STR(wLayoutDisplayName(WLAYOUT_MONOCLE), "Monocle");
	CHECK_STR(wLayoutDisplayName(-1), "Float");
	CHECK_STR(wLayoutDisplayName(WLAYOUT_COUNT), "Float");
	CHECK(wLayoutFromName("Tile") == WLAYOUT_TILE);
	CHECK(wLayoutFromName("MONOCLE") == WLAYOUT_MONOCLE);
	CHECK(wLayoutFromName("nope") == WLAYOUT_FLOAT);
	CHECK(wLayoutFromName(NULL) == WLAYOUT_FLOAT);
}

int main(void)
{
	test_float_and_empty();
	test_tile();
	test_tile_ratio_clamp();
	test_tile_ratio_nonfinite();
	test_tile_h();
	test_grid();
	test_monocle();
	test_names();
	return check_report("check_layout");
}
