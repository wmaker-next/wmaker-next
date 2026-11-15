/*
 *  check_placecompute - unit tests for the pure placement policy in
 *  src/placecompute.c. No display needed, the occupancy is passed in.
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

#include <stdlib.h>
#include <string.h>

#include "placecompute.h"

#include "check_common.h"

/* the model expands the placed window by the extra dims */
#define TITLE_H 20
#define BORDER_W 2

static void fill_env(WPlaceEnv *env, int mode)
{
	memset(env, 0, sizeof(*env));
	env->usable.x1 = 0;
	env->usable.y1 = 0;
	env->usable.x2 = 640;
	env->usable.y2 = 480;
	env->mode = mode;
	env->icon_yard = 0;
	env->icon_size = 64;
	env->extra_w = 2 * BORDER_W;
	env->extra_h = TITLE_H + 2 * BORDER_W;
}

static WPlaceOcc mkocc(int x, int y, int w, int h, bool mapped)
{
	WPlaceOcc o;

	memset(&o, 0, sizeof(o));
	o.x = x;
	o.y = y;
	o.w = w;
	o.h = h;
	o.level = 0;		/* WMNormalLevel */
	o.mapped = mapped;
	return o;
}

static void test_cascade(void)
{
	WPlaceEnv env;
	int x, y;

	fill_env(&env, WPM_CASCADE);
	env.cascade_h = 20;

	/* each window steps down-right by h from the origin */
	env.cascade_index = 0;
	wPlaceWindowCompute(&env, NULL, 0, 100, 100, &x, &y);
	CHECK(x == 0 && y == 0);
	CHECK(env.cascade_next == 1);

	env.cascade_index = 3;
	wPlaceWindowCompute(&env, NULL, 0, 100, 100, &x, &y);
	CHECK(x == 60 && y == 60);
	CHECK(env.cascade_next == 4);

	/* the next step would exceed the usable area: wrap to the
	 * origin and reset the index (after the post-increment, so 1) */
	env.cascade_index = 23;
	wPlaceWindowCompute(&env, NULL, 0, 100, 100, &x, &y);
	CHECK(x == 0 && y == 0);
	CHECK(env.cascade_next == 1);

	/* WPM_AUTO/CENTER increment before the cascade call, so a reset
	 * leaves the index at 0 */
	fill_env(&env, WPM_AUTO);
	env.cascade_h = 20;
	env.cascade_index = 0;
	/* an occupancy wall leaves nothing free for the auto scan */
	{
		WPlaceOcc wall = mkocc(0, 0, 640, 480, true);

		wPlaceWindowCompute(&env, &wall, 1, 100, 100, &x, &y);
	}
	CHECK(x == 20 && y == 20);
	CHECK(env.cascade_next == 1);
}

static void test_smart(void)
{
	WPlaceEnv env;
	WPlaceOcc occ[2];
	int x, y;

	fill_env(&env, WPM_SMART);

	/* a window in the top-left corner and one to its right, with a
	 * gap between them: smart placement must pick the gap */
	occ[0] = mkocc(0, 0, 100, 100, true);
	occ[1] = mkocc(200, 0, 100, 100, true);

	wPlaceWindowCompute(&env, occ, 2, 90, 90, &x, &y);
	/* the coarse 8-px scan finds the zero-coverage cell at x=104,
	 * the 1-px refinement cannot beat it */
	CHECK(x == 104 && y == 0);
}

static void test_auto(void)
{
	WPlaceEnv env;
	WPlaceOcc occ[1];
	int x, y;

	fill_env(&env, WPM_AUTO);

	/* free screen: center placement with the frame metrics
	 * expanded exactly once, same as WPM_CENTER */
	wPlaceWindowCompute(&env, NULL, 0, 100, 100, &x, &y);
	CHECK(x == (640 - 104) / 2);
	CHECK(y == (480 - 124) / 2);

	/* center occupied: the scan starts at the top-left corner and
	 * the first cell is free there */
	occ[0] = mkocc(200, 140, 400, 300, true);
	wPlaceWindowCompute(&env, occ, 1, 100, 100, &x, &y);
	CHECK(x == 0 && y == 0);

	/* a wall overlaps every scan cell, the placement falls through
	 * to cascade with the pre-incremented index */
	fill_env(&env, WPM_AUTO);
	env.cascade_h = 20;
	occ[0] = mkocc(0, 0, 640, 480, true);
	wPlaceWindowCompute(&env, occ, 1, 100, 100, &x, &y);
	CHECK(x == 20 && y == 20);
	CHECK(env.cascade_next == 1);
}

static void test_center(void)
{
	WPlaceEnv env;
	WPlaceOcc wall = mkocc(0, 0, 640, 480, true);
	int x, y;

	fill_env(&env, WPM_CENTER);

	/* WPM_CENTER only checks the fit, not the occupancy */
	wPlaceWindowCompute(&env, &wall, 1, 100, 100, &x, &y);
	CHECK(x == (640 - 104) / 2);
	CHECK(y == (480 - 124) / 2);
	CHECK(env.cascade_next == 0);

	/* a window that does not fit falls through to the auto scan,
	 * the wall leaves nothing free, so the cascade path runs and
	 * overflows immediately: wrap to the origin, index resets */
	wPlaceWindowCompute(&env, &wall, 1, 600, 460, &x, &y);
	CHECK(x == 0 && y == 0);
	CHECK(env.cascade_next == 0);
}

static void test_random(void)
{
	WPlaceEnv env;
	int x, y;

	fill_env(&env, WPM_RANDOM);
	srand(42);

	wPlaceWindowCompute(&env, NULL, 0, 100, 100, &x, &y);
	CHECK(x >= 0 && x + 104 <= 640);
	CHECK(y >= 0 && y + 124 <= 480);
	CHECK(env.cascade_next == 0);
}

static void test_icon_yard(void)
{
	WPlaceEnv env;
	WPlaceOcc occ[1];
	int x, y;

	fill_env(&env, 0);
	env.icon_yard = 0;
	env.icon_size = 64;

	/* empty screen, bottom-left horizontal yard: the first slot is
	 * the bottom-left corner cell */
	wPlaceIconCompute(&env, NULL, 0, &x, &y);
	CHECK(x == 0 && y == 480 - 64);

	/* a horizontal yard fills the bottom row to the right first */
	occ[0] = mkocc(0, 480 - 64, 64, 64, true);
	wPlaceIconCompute(&env, occ, 1, &x, &y);
	CHECK(x == 64 && y == 480 - 64);

	/* an icon straddling two cells marks both: the next free column
	 * is the one after the straddle */
	occ[0] = mkocc(32, 480 - 64, 64, 64, true);
	wPlaceIconCompute(&env, occ, 1, &x, &y);
	CHECK(x == 2 * 64 && y == 480 - 64);

	/* top origin walks from the top row */
	env.icon_yard = IY_TOP;
	occ[0] = mkocc(0, 0, 64, 64, true);
	wPlaceIconCompute(&env, occ, 1, &x, &y);
	CHECK(x == 64 && y == 0);

	/* a displaced icon blocks the slots its rectangle overlaps */
	occ[0] = mkocc(1, 0, 64, 64, true);
	wPlaceIconCompute(&env, occ, 1, &x, &y);
	CHECK(x == 2 * 64 && y == 0);

	occ[0] = mkocc(0, 1, 64, 64, true);
	wPlaceIconCompute(&env, occ, 1, &x, &y);
	CHECK(x == 64 && y == 0);

	occ[0] = mkocc(65, 0, 64, 64, true);
	wPlaceIconCompute(&env, occ, 1, &x, &y);
	CHECK(x == 0 && y == 0);

	/* right origin walks from the right edge going left */
	env.icon_yard = IY_RIGHT;
	occ[0] = mkocc(640 - 64, 480 - 64, 64, 64, true);
	wPlaceIconCompute(&env, occ, 1, &x, &y);
	CHECK(x == 640 - 2 * 64 && y == 480 - 64);

	/* vertical yard fills the column before moving sideways */
	env.icon_yard = IY_VERT;
	occ[0] = mkocc(0, 480 - 64, 64, 64, true);
	wPlaceIconCompute(&env, occ, 1, &x, &y);
	CHECK(x == 0 && y == 480 - 2 * 64);

	/* a slot must not overlap an existing icon: the slot at y=352
	 * overlaps an icon at y=384, the next free one is y=288 */
	fill_env(&env, 0);
	env.usable.x2 = 64;
	occ[0] = mkocc(0, 384, 64, 64, true);
	wPlaceIconCompute(&env, occ, 1, &x, &y);
	CHECK(x == 0 && y == 288);
}

static void test_icon_yard_negative_origin(void)
{
	WPlaceEnv env;
	WPlaceOcc occ[20];
	int x, y;
	int i;

	/* a head entirely above the origin: both yard rows are addressable */
	fill_env(&env, 0);
	env.usable.x1 = 0;
	env.usable.y1 = -128;
	env.usable.x2 = 640;
	env.usable.y2 = 0;
	env.icon_yard = 0;
	env.icon_size = 64;

	/* an icon one icon size above the head's top edge is within
	 * iconPosition's acceptance: it overlaps no slot and the
	 * bottom row stays free */
	occ[0] = mkocc(0, -192, 64, 64, true);
	wPlaceIconCompute(&env, occ, 1, &x, &y);
	CHECK(x == 0 && y == -64);

	/* with the bottom row full the scan finds the free upper row */
	for (i = 0; i < 10; i++)
		occ[i] = mkocc(i * 64, -64, 64, 64, true);
	occ[10] = mkocc(0, -128, 64, 64, true);
	wPlaceIconCompute(&env, occ, 11, &x, &y);
	CHECK(x == 64 && y == -128);

	/* with every row full the fallback stays on the head */
	for (i = 0; i < 10; i++)
		occ[i + 10] = mkocc(i * 64, -128, 64, 64, true);
	wPlaceIconCompute(&env, occ, 20, &x, &y);
	CHECK(x == 0 && y == -128);
}

int main(void)
{
	test_cascade();
	test_smart();
	test_auto();
	test_center();
	test_random();
	test_icon_yard();
	test_icon_yard_negative_origin();

	return check_report("check_placecompute");
}
