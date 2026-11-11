/*
 *  check_geometry.c
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
 *  check_geometry - tests for the wParseGeometry() grammar in
 *  geometry.c, the XParseGeometry stand-in from the C2 step.
 *
 *  Pattern follows check_calc.c: compile the test + ../geometry.c,
 *  link against libWUtil.la.
 */

#include "wconfig.h"
#include <stdbool.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <WINGs/WUtil.h>

#include "config_values.h"
#include "geometry.h"
#include "randr.h"

#include "check_common.h"

/* parse with sentinel outputs so untouched fields are detectable */
static int pg(const char *spec, int *x, int *y, unsigned *w, unsigned *h)
{
	*x = -777;
	*y = -777;
	*w = 777;
	*h = 777;
	return wParseGeometry(spec, x, y, w, h);
}

static void test_full_spec(void)
{
	int x, y, mask;
	unsigned w, h;

	mask = pg("640x480+100+50", &x, &y, &w, &h);
	CHECK(mask == (WidthValue | HeightValue | XValue | YValue));
	CHECK(w == 640 && h == 480 && x == 100 && y == 50);
}

static void test_prefix_and_signs(void)
{
	int x, y, mask;
	unsigned w, h;

	/* the optional '=' prefix is skipped */
	mask = pg("=640x480-100-50", &x, &y, &w, &h);
	CHECK(mask == (WidthValue | HeightValue | XValue | YValue
		       | XNegative | YNegative));
	CHECK(w == 640 && h == 480 && x == -100 && y == -50);

	/* offsets only; the sign trick yields negative x */
	mask = pg("-100+50", &x, &y, &w, &h);
	CHECK(mask == (XValue | YValue | XNegative));
	CHECK(x == -100 && y == 50);
	CHECK(w == 777 && h == 777);

	/* zero offsets still set the flags, only the sign flips */
	mask = pg("+0-0", &x, &y, &w, &h);
	CHECK(mask == (XValue | YValue | YNegative));
	CHECK(x == 0 && y == 0);

	/* the strtoul sign trick on a lone negative offset */
	mask = pg("-10", &x, &y, &w, &h);
	CHECK(mask == (XValue | XNegative));
	CHECK(x == -10);

	/* libX11's "+-5" form: the value's own sign applies, XNegative
	 * does not (the component sign was '+') */
	mask = pg("+-5", &x, &y, &w, &h);
	CHECK(mask == XValue);
	CHECK(x == -5);

	mask = pg("10x20+-5+2", &x, &y, &w, &h);
	CHECK(mask == (WidthValue | HeightValue | XValue | YValue));
	CHECK(w == 10 && h == 20 && x == -5 && y == 2);

	/* a doubled sign flips the value but XNegative rides the
	 * component sign */
	mask = pg("--5", &x, &y, &w, &h);
	CHECK(mask == (XValue | XNegative));
	CHECK(x == 5);

	/* X-only commit is fine when the spec ends there */
	mask = pg("10x20+1", &x, &y, &w, &h);
	CHECK(mask == (WidthValue | HeightValue | XValue));
	CHECK(x == 1);

	/* a position sign with no value kills the whole spec */
	mask = pg("10x20-5-", &x, &y, &w, &h);
	CHECK(mask == NoValue);
}

static void test_partial_and_missing(void)
{
	int x, y, mask;
	unsigned w, h;

	mask = pg("x480", &x, &y, &w, &h);
	CHECK(mask == HeightValue);
	CHECK(h == 480);

	mask = pg("640", &x, &y, &w, &h);
	CHECK(mask == WidthValue);

	/* the strict libX11 grammar: a trailing tail or an unconsumable
	 * middle invalidates the whole spec, no output is written */
	mask = pg("640x", &x, &y, &w, &h);
	CHECK(mask == NoValue);
	CHECK(w == 777 && h == 777 && x == -777 && y == -777);

	mask = pg("10x20abc", &x, &y, &w, &h);
	CHECK(mask == NoValue);
	CHECK(w == 777 && h == 777);

	mask = pg("10x 20+1+2", &x, &y, &w, &h);
	CHECK(mask == NoValue);

	mask = pg("  10x20", &x, &y, &w, &h);
	CHECK(mask == NoValue);

	mask = pg("1x2x3", &x, &y, &w, &h);
	CHECK(mask == NoValue);

	/* nothing parseable: NoValue, outputs untouched */
	mask = pg("", &x, &y, &w, &h);
	CHECK(mask == NoValue);
	mask = pg("garbage", &x, &y, &w, &h);
	CHECK(mask == NoValue);
	mask = pg("=", &x, &y, &w, &h);
	CHECK(mask == NoValue);
	CHECK(w == 777 && h == 777 && x == -777 && y == -777);

	CHECK(wParseGeometry(NULL, &x, &y, &w, &h) == NoValue);
}

/* raw anchors for all nine positions */
static void test_place_rect_anchors(void)
{
	WMRect area = { { 100, 200 }, { 800, 600 } };
	WMPoint p;

	/* center, and WD_NONE and unknown values default onto it */
	p = wGetPointToPlaceRect(area, WD_CENTER, 200, 100, 8);
	CHECK(p.x == 100 + (800 - 200) / 2 && p.y == 200 + (600 - 100) / 2);
	p = wGetPointToPlaceRect(area, WD_NONE, 200, 100, 8);
	CHECK(p.x == 100 + (800 - 200) / 2 && p.y == 200 + (600 - 100) / 2);
	p = wGetPointToPlaceRect(area, 4242, 200, 100, 8);
	CHECK(p.x == 100 + (800 - 200) / 2 && p.y == 200 + (600 - 100) / 2);
	p = wGetPointToPlaceRect(area, -1, 200, 100, 8);
	CHECK(p.x == 100 + (800 - 200) / 2 && p.y == 200 + (600 - 100) / 2);

	/* edges: centered axis centers, anchored axis sits pad in */
	p = wGetPointToPlaceRect(area, WD_TOP, 200, 100, 8);
	CHECK(p.x == 100 + (800 - 200) / 2 && p.y == 200 + 8);
	p = wGetPointToPlaceRect(area, WD_BOTTOM, 200, 100, 8);
	CHECK(p.x == 100 + (800 - 200) / 2 && p.y == 200 + 600 - 100 - 8);
	p = wGetPointToPlaceRect(area, WD_LEFT, 200, 100, 8);
	CHECK(p.x == 100 + 8 && p.y == 200 + (600 - 100) / 2);
	p = wGetPointToPlaceRect(area, WD_RIGHT, 200, 100, 8);
	CHECK(p.x == 100 + 800 - 200 - 8 && p.y == 200 + (600 - 100) / 2);

	/* corners: pad on both anchored edges */
	p = wGetPointToPlaceRect(area, WD_TOPLEFT, 200, 100, 8);
	CHECK(p.x == 108 && p.y == 208);
	p = wGetPointToPlaceRect(area, WD_TOPRIGHT, 200, 100, 8);
	CHECK(p.x == 100 + 800 - 200 - 8 && p.y == 208);
	p = wGetPointToPlaceRect(area, WD_BOTTOMLEFT, 200, 100, 8);
	CHECK(p.x == 108 && p.y == 200 + 600 - 100 - 8);
	p = wGetPointToPlaceRect(area, WD_BOTTOMRIGHT, 200, 100, 8);
	CHECK(p.x == 100 + 800 - 200 - 8 && p.y == 200 + 600 - 100 - 8);
}

/* offset and negative origins */
static void test_place_rect_origins(void)
{
	WMRect neg = { { -1920, -100 }, { 1920, 1080 } };
	WMRect zero = { { 0, 0 }, { 512, 384 } };
	WMPoint p;

	p = wGetPointToPlaceRect(neg, WD_TOPLEFT, 64, 32, 0);
	CHECK(p.x == -1920 && p.y == -100);
	p = wGetPointToPlaceRect(neg, WD_BOTTOMRIGHT, 64, 32, 16);
	CHECK(p.x == -1920 + 1920 - 64 - 16 && p.y == -100 + 1080 - 32 - 16);
	p = wGetPointToPlaceRect(neg, WD_CENTER, 64, 32, 0);
	CHECK(p.x == -1920 + (1920 - 64) / 2 && p.y == -100 + (1080 - 32) / 2);

	p = wGetPointToPlaceRect(zero, WD_CENTER, 512, 384, 0);
	CHECK(p.x == 0 && p.y == 0);
}

static void test_oversized_osd(void)
{
	WMRect area = { { 100, 200 }, { 100, 100 } };
	WMRect zero = { { 0, 0 }, { 100, 100 } };
	WMPoint p;
	int width = 200, height = 201;

	p = wGetOSDPointToPlaceRect(zero, WD_CENTER, 200, 200, 8);
	CHECK(p.x == 2147483598 && p.y == 2147483598);
	p = wGetOSDPointToPlaceRect(zero, WD_TOP, 200, 200, 8);
	CHECK(p.x == 2147483598 && p.y == 8);
	p = wGetOSDPointToPlaceRect(zero, WD_LEFT, 200, 200, 8);
	CHECK(p.x == 8 && p.y == 2147483598);
	p = wGetOSDPointToPlaceRect(zero, WD_NONE, 200, 200, 8);
	CHECK(p.x == 2147483598 && p.y == 2147483598);
	p = wGetOSDPointToPlaceRect(zero, 999, 200, 200, 8);
	CHECK(p.x == 2147483598 && p.y == 2147483598);
	p = wGetOSDPointToPlaceRect(area, WD_CENTER, width, height, 8);
	CHECK(p.x == (int)(area.pos.x + (area.size.width - width) / 2));
	CHECK(p.y == (int)(area.pos.y + (area.size.height - height) / 2));
	p = wGetOSDPointToPlaceRect(area, WD_TOP, width, height, 8);
	CHECK(p.x == (int)(area.pos.x + (area.size.width - width) / 2));
	CHECK(p.y == area.pos.y + 8);
	p = wGetPointToPlaceRect(area, WD_CENTER, width, height, 8);
	CHECK(p.x == 50 && p.y == 150);
}

/* switch-panel constraint: clamp on fitting axes, origin otherwise */
static void test_clamp_to_area(void)
{
	WMRect area = { { 100, 200 }, { 800, 600 } };
	WMPoint p;

	/* the far endpoint of the clamp range is inclusive */
	p = wClampPointToArea(area, 100, 200, 800, 100);
	CHECK(p.x == 100 && p.y == 200);
	p = wClampPointToArea(area, 100, 700, 800, 100);
	CHECK(p.x == 100 && p.y == 700);
	p = wClampPointToArea(area, 100, 701, 800, 100);
	CHECK(p.x == 100 && p.y == 700);

	p = wClampPointToArea(area, 50, 900, 200, 100);
	CHECK(p.x == 100 && p.y == 200 + 600 - 100);
	p = wClampPointToArea(area, 850, 400, 200, 100);
	CHECK(p.x == 100 + 800 - 200 && p.y == 400);

	p = wClampPointToArea(area, 400, 400, 200, 100);
	CHECK(p.x == 400 && p.y == 400);

	/* oversized on either or both axes anchors at the origin */
	p = wClampPointToArea(area, 0, 400, 900, 100);
	CHECK(p.x == 100 && p.y == 400);
	p = wClampPointToArea(area, 400, 0, 200, 900);
	CHECK(p.x == 400 && p.y == 200);
	p = wClampPointToArea(area, -50, -50, 900, 900);
	CHECK(p.x == 100 && p.y == 200);
}

/* integer-limit sentinels saturate instead of wrapping */
static void test_place_limits(void)
{
	WMRect hi = { { INT_MAX - 10, INT_MAX - 10 }, { 20, 20 } };
	WMRect lo = { { INT_MIN, INT_MIN }, { 10, 10 } };
	WMPoint p;

	p = wGetPointToPlaceRect(hi, WD_BOTTOMRIGHT, 4, 4, 4);
	CHECK(p.x == INT_MAX && p.y == INT_MAX);
	p = wGetPointToPlaceRect(hi, WD_TOPLEFT, 4, 4, INT_MAX);
	CHECK(p.x == INT_MAX && p.y == INT_MAX);
	p = wGetPointToPlaceRect(lo, WD_TOP, 4, 4, INT_MIN);
	CHECK(p.x == INT_MIN + (10 - 4) / 2 && p.y == INT_MIN);
	/* a saturated anchor stays inside a range that would overflow */
	p = wClampPointToArea(hi, INT_MAX, INT_MAX, 4, 4);
	CHECK(p.x == INT_MAX && p.y == INT_MAX);
	p = wClampPointToArea(lo, INT_MIN, INT_MIN, 4, 4);
	CHECK(p.x == INT_MIN && p.y == INT_MIN);
	p = wClampPointToArea(hi, INT_MAX - 10, INT_MAX - 10, 4, 4);
	CHECK(p.x == INT_MAX - 10 && p.y == INT_MAX - 10);
}

static void test_mask_bits(void)
{
	/* the bit values are the Xutil.h XParseGeometry contract */
	CHECK(NoValue == 0x0000);
	CHECK(XValue == 0x0001);
	CHECK(YValue == 0x0002);
	CHECK(WidthValue == 0x0004);
	CHECK(HeightValue == 0x0008);
	CHECK(AllValues == 0x000f);
	CHECK(XNegative == 0x0010);
	CHECK(YNegative == 0x0020);
}

#ifdef USE_RANDR
static void test_randr_screen_size(void)
{
	int width, height;

	CHECK(wRandRScreenSizeForMode(1920, 1080, XCB_RANDR_ROTATION_ROTATE_0,
				      1920, 1920, 1080, 1, 1, 8192, 8192,
				      &width, &height));
	CHECK(width == 3840 && height == 1080);
	CHECK(wRandRScreenSizeForMode(1920, 1080, XCB_RANDR_ROTATION_ROTATE_90,
				      1920, 1920, 1080, 1, 1, 8192, 8192,
				      &width, &height));
	CHECK(width == 3000 && height == 1920);
	CHECK(wRandRScreenSizeForMode(1920, 1080, XCB_RANDR_ROTATION_ROTATE_270 |
				      XCB_RANDR_ROTATION_REFLECT_X, 1920,
				      4000, 1080, 1, 1, 8192, 8192,
				      &width, &height));
	CHECK(width == 4000 && height == 1920);
	CHECK(wRandRScreenSizeForMode(1920, 1080, XCB_RANDR_ROTATION_ROTATE_180,
				      0, 1024, 768, 2048, 1200, 8192, 8192,
				      &width, &height));
	CHECK(width == 2048 && height == 1200);
	CHECK(!wRandRScreenSizeForMode(1920, 1080, XCB_RANDR_ROTATION_ROTATE_90,
				       1920, 1920, 1080, 1, 1, 2999, 8192,
				       &width, &height));
	CHECK(!wRandRScreenSizeForMode(1920, 1080, XCB_RANDR_ROTATION_ROTATE_270,
				       1920, 1920, 1080, 1, 1, 8192, 1919,
				       &width, &height));
}
#endif

int main(void)
{
	test_full_spec();
	test_prefix_and_signs();
	test_partial_and_missing();
	test_place_rect_anchors();
	test_place_rect_origins();
	test_oversized_osd();
	test_clamp_to_area();
	test_place_limits();
	test_mask_bits();
#ifdef USE_RANDR
	test_randr_screen_size();
#endif

	return check_report("check_geometry");
}
