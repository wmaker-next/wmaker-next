/*
 *  check_swp_size.c
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

/* tests for the checked switch panel sizing arithmetic */

#include "wconfig.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "swp_size.h"

#include "check_common.h"

static void test_tile_size(void)
{
	CHECK(wSwitchPanelTileSize(1) == 1);
	CHECK(wSwitchPanelTileSize(64) == 77);		/* rounded 64 * 1.2 */
	CHECK(wSwitchPanelTileSize(16667) == 20000);	/* at the image cap */

	CHECK(wSwitchPanelTileSize(0) == 0);
	CHECK(wSwitchPanelTileSize(-1) == 0);
	CHECK(wSwitchPanelTileSize(INT_MIN) == 0);
	CHECK(wSwitchPanelTileSize(16668) == 0);
	CHECK(wSwitchPanelTileSize(INT_MAX) == 0);
}

static void test_device_px(void)
{
	int dev;

	CHECK(wSwitchPanelDevicePx(77, 1.0, &dev) && dev == 77);
	CHECK(wSwitchPanelDevicePx(77, 2.0, &dev) && dev == 154);
	CHECK(wSwitchPanelDevicePx(3, 1.5, &dev) && dev == 5);

	CHECK(!wSwitchPanelDevicePx(0, 1.0, &dev));
	CHECK(!wSwitchPanelDevicePx(-5, 1.0, &dev));
	CHECK(!wSwitchPanelDevicePx(77, 0.0, &dev));
	CHECK(!wSwitchPanelDevicePx(77, -1.0, &dev));
	CHECK(!wSwitchPanelDevicePx(INT_MAX, 2.0, &dev));
}

static void test_strip_and_pos(void)
{
	int x, w, i;

	CHECK(wSwitchPanelIconPos(0, 77, &x) && x == 0);
	CHECK(wSwitchPanelIconPos(3, 77, &x) && x == 231);
	CHECK(!wSwitchPanelIconPos(-1, 77, &x));
	CHECK(!wSwitchPanelIconPos(0, 0, &x));
	CHECK(!wSwitchPanelIconPos(INT_MAX, 20000, &x));

	CHECK(wSwitchPanelStripWidth(10, 77, 1.0, &w) && w == 770);
	CHECK(wSwitchPanelStripWidth(1, 77, 1.0, &w) && w == 77);
	CHECK(!wSwitchPanelStripWidth(1000, 77, 1.0, &w));
	CHECK(!wSwitchPanelStripWidth(0, 77, 1.0, &w));
	CHECK(!wSwitchPanelStripWidth(10, 0, 1.0, &w));
	CHECK(!wSwitchPanelStripWidth(500, 77, 2.0, &w));
	CHECK(wSwitchPanelChildCoordOk(32767, 1.0));
	CHECK(!wSwitchPanelChildCoordOk(32768, 1.0));
	CHECK(wSwitchPanelChildCoordOk(-32768, 1.0));
	CHECK(!wSwitchPanelChildCoordOk(-32770, 1.0));
	CHECK(wSwitchPanelStripWidth(2, 16383, 2.0, &w));
	CHECK(!wSwitchPanelStripWidth(2, 16384, 2.0, &w));
	CHECK(wSwitchPanelStripWidth(426, 77, 1.0, &w));
	CHECK(!wSwitchPanelStripWidth(427, 77, 1.0, &w));
	for (i = 0; i < 426; i++) {
		CHECK(wSwitchPanelIconPos(i, 77, &x));
		CHECK(wSwitchPanelChildCoordOk(x, 1.0));
		CHECK(wSwitchPanelChildCoordOk(-x, 1.0));
	}
	CHECK(wSwitchPanelStripWidth(284, 77, 1.5, &w));
	CHECK(!wSwitchPanelStripWidth(285, 77, 1.5, &w));
	for (i = 0; i < 284; i++) {
		CHECK(wSwitchPanelIconPos(i, 77, &x));
		CHECK(wSwitchPanelChildCoordOk(x, 1.5));
		CHECK(wSwitchPanelChildCoordOk(-x, 1.5));
	}

	CHECK(wSwitchPanelDrillExtra(10, 77, &x) && x == 87);
	CHECK(!wSwitchPanelDrillExtra(-1, 77, &x));
	CHECK(!wSwitchPanelDrillExtra(10, 0, &x));
}

static void test_limits(void)
{
	CHECK(wSwitchPanelImageSizeOk(1, 1));
	CHECK(wSwitchPanelImageSizeOk(WSP_IMAGE_MAX, WSP_IMAGE_MAX));
	CHECK(!wSwitchPanelImageSizeOk(0, 10));
	CHECK(!wSwitchPanelImageSizeOk(WSP_IMAGE_MAX + 1, 10));

	CHECK(wSwitchPanelWidgetSizeOk(77, 77, 1.0));
	CHECK(!wSwitchPanelWidgetSizeOk(0, 77, 1.0));
	CHECK(wSwitchPanelWidgetSizeOk(WSP_XDIM_MAX, 77, 1.0));
	CHECK(!wSwitchPanelWidgetSizeOk(WSP_XDIM_MAX + 1, 77, 1.0));
	CHECK(!wSwitchPanelWidgetSizeOk(40000, 77, 2.0));
}

static void init(WSwitchPanelSizing *in)
{
	memset(in, 0, sizeof(*in));
	in->icon_size = 64;
	in->count = 10;
	in->border = 10;
	in->label_height = 25;
	in->extra_h = 0;
	in->head_w = 512;
	in->head_h = 384;
	in->ui_scale = 1.0;
	in->opt_in_fit = false;
}

/* the effective-default path keeps the historical margin counts */
static void test_legacy_counts(void)
{
	WSwitchPanelSizing in;

	init(&in);
	/* 10 * 77 = 770 > 512, so (512 - 40) / 77 = 6 */
	CHECK(wSwitchPanelVisibleCount(&in) == 6);

	in.count = 3;
	in.head_w = 30;
	CHECK(wSwitchPanelVisibleCount(&in) == 1);
	in.head_w = 0;
	CHECK(wSwitchPanelVisibleCount(&in) == 1);

	in.count = 6;
	in.head_w = 6 * 77;
	CHECK(wSwitchPanelVisibleCount(&in) == 6);
	in.head_w = 6 * 77 - 1;
	CHECK(wSwitchPanelVisibleCount(&in) == (6 * 77 - 1 - 40) / 77);

	in.count = 0;
	CHECK(wSwitchPanelVisibleCount(&in) == 0);

	in.count = 5;
	in.icon_size = 0;
	CHECK(wSwitchPanelVisibleCount(&in) == -1);
	in.icon_size = 64;
	in.ui_scale = 0.0;
	CHECK(wSwitchPanelVisibleCount(&in) == -1);
}

/* the opt-in fit includes the complete panel and device dimensions */
static void test_optin_counts(void)
{
	WSwitchPanelSizing in;
	WSwitchPanelSize sz;

	init(&in);
	in.opt_in_fit = true;

	/* 10 * 77 + 20 = 790 is wider than the 512 head, the height
	 * 25 + 77 + 20 = 122 still fits 384 */
	CHECK(wSwitchPanelVisibleCount(&in) < 10);
	CHECK(wSwitchPanelCalcSize(&in, &sz, NULL) == 0);
	CHECK(sz.dev_panel_width <= in.head_w);
	CHECK(sz.dev_panel_height <= in.head_h);

	in.head_w = 10;
	in.head_h = 10;
	CHECK(wSwitchPanelVisibleCount(&in) == 1);

	in.head_w = 2 * 77 + 2 * 10;
	in.head_h = 25 + 77 + 2 * 10;
	in.count = 8;
	CHECK(wSwitchPanelVisibleCount(&in) == 2);

	init(&in);
	in.opt_in_fit = true;
	in.ui_scale = 2.0;
	in.head_w = 2 * (6 * 77 + 2 * 10);
	in.head_h = 2 * (25 + 77 + 2 * 10);
	in.count = 10;
	CHECK(wSwitchPanelVisibleCount(&in) <= 6);
	CHECK(wSwitchPanelVisibleCount(&in) >= 5);
}

static void test_calc_dims(void)
{
	WSwitchPanelSizing in;
	WSwitchPanelSize sz;

	init(&in);
	CHECK(wSwitchPanelCalcDims(&in, 6, &sz, NULL) == 0);
	CHECK(sz.tile == 77);
	CHECK(sz.visible == 6);
	CHECK(sz.full_width == 770);
	CHECK(sz.strip_width == 6 * 77);
	CHECK(sz.content_height == 25 + 77);
	CHECK(sz.panel_width == 6 * 77 + 20);
	CHECK(sz.panel_height == 25 + 77 + 20);
	CHECK(sz.dev_panel_width == sz.panel_width);
	CHECK(sz.dev_panel_height == sz.panel_height);

	CHECK(wSwitchPanelCalcDims(&in, 11, &sz, NULL) == -1);
	CHECK(wSwitchPanelCalcDims(&in, -1, &sz, NULL) == -1);

	in.extra_h = 87;
	CHECK(wSwitchPanelCalcDims(&in, 6, &sz, NULL) == 0);
	CHECK(sz.panel_height == 25 + 77 + 87 + 20);
}

static void test_overflow_sentinels(void)
{
	WSwitchPanelSizing in;
	WSwitchPanelSize sz;

	init(&in);
	in.count = INT_MAX;
	CHECK(wSwitchPanelCalcDims(&in, 1, &sz, NULL) == -1);

	init(&in);
	in.count = 2000;
	in.icon_size = 16667;	/* tile 20000 */
	CHECK(wSwitchPanelCalcDims(&in, 1, &sz, NULL) == -1);

	init(&in);
	in.border = INT_MAX;
	CHECK(wSwitchPanelCalcDims(&in, 1, &sz, NULL) == -1);

	init(&in);
	in.extra_h = INT_MAX;
	CHECK(wSwitchPanelCalcDims(&in, 1, &sz, NULL) == -1);

	init(&in);
	in.label_height = 0;
	CHECK(wSwitchPanelCalcDims(&in, 1, &sz, NULL) == -1);

	init(&in);
	in.ui_scale = 1e300;
	CHECK(wSwitchPanelCalcDims(&in, 1, &sz, NULL) == -1);
	CHECK(wSwitchPanelCalcSize(&in, &sz, NULL) == -1);

	init(&in);
	in.border = 15000;
	CHECK(wSwitchPanelCalcDims(&in, 5, &sz, NULL) == -1);
}

static void test_error_codes(void)
{
	WSwitchPanelSizing in;
	WSwitchPanelSize sz;
	WSwitchPanelError err;

	init(&in);
	err = WSP_ERR_STRIP;
	CHECK(wSwitchPanelCalcDims(&in, 5, &sz, &err) == 0);
	CHECK(err == WSP_ERR_NONE);

	init(&in);
	in.count = 2000;
	in.icon_size = 16667;
	err = WSP_ERR_NONE;
	CHECK(wSwitchPanelCalcDims(&in, 1, &sz, &err) == -1);
	CHECK(err == WSP_ERR_STRIP);

	init(&in);
	in.border = 15000;
	err = WSP_ERR_NONE;
	CHECK(wSwitchPanelCalcDims(&in, 5, &sz, &err) == -1);
	CHECK(err == WSP_ERR_IMAGE);

	/* the widget check reports before the image caps are consulted */
	init(&in);
	in.ui_scale = 2.0;
	in.border = 16200;
	err = WSP_ERR_NONE;
	CHECK(wSwitchPanelCalcDims(&in, 5, &sz, &err) == -1);
	CHECK(err == WSP_ERR_WIDGET);

	init(&in);
	in.icon_size = 0;
	err = WSP_ERR_NONE;
	CHECK(wSwitchPanelCalcDims(&in, 5, &sz, &err) == -1);
	CHECK(err == WSP_ERR_INPUT);
	err = WSP_ERR_NONE;
	CHECK(wSwitchPanelCalcSize(&in, &sz, &err) == -1);
	CHECK(err == WSP_ERR_INPUT);
	init(&in);
	in.ui_scale = 1e300;
	err = WSP_ERR_NONE;
	CHECK(wSwitchPanelCalcDims(&in, 5, &sz, &err) == -1);
	CHECK(err == WSP_ERR_INPUT);
}

static void test_raster_bytes(void)
{
	long long bytes;

	CHECK(wSwitchPanelRasterBytes(1, 1, &bytes) && bytes == 4);
	CHECK(wSwitchPanelRasterBytes(2, 3, &bytes) && bytes == 24);
	CHECK(wSwitchPanelRasterBytes(77, 132, &bytes) && bytes == 77LL * 132 * 4);

	CHECK(!wSwitchPanelRasterBytes(0, 4, &bytes));
	CHECK(!wSwitchPanelRasterBytes(4, 0, &bytes));
	CHECK(!wSwitchPanelRasterBytes(-1, 4, &bytes));
	CHECK(!wSwitchPanelRasterBytes(4, 4, NULL));

	CHECK(!wSwitchPanelRasterBytes(INT_MAX, INT_MAX, &bytes));
	CHECK(!wSwitchPanelRasterBytes(INT_MAX, (1 << 30) + 1, &bytes));
}

static void test_budget(void)
{
	WSwitchPanelBudget b;
	long long alloc_max = WSP_ALLOC_LIMIT;
	long long peak_max = WSP_PEAK_LIMIT;

	wSwitchPanelBudgetInit(&b);
	CHECK(b.live == 0 && b.peak == 0);

	CHECK(wSwitchPanelBudgetAdd(&b, alloc_max));
	CHECK(b.live == alloc_max && b.peak == alloc_max);
	CHECK(!wSwitchPanelBudgetAdd(&b, alloc_max + 1));
	CHECK(b.live == alloc_max);

	wSwitchPanelBudgetInit(&b);
	CHECK(wSwitchPanelBudgetAdd(&b, peak_max / 4));
	CHECK(wSwitchPanelBudgetAdd(&b, peak_max / 4));
	CHECK(wSwitchPanelBudgetAdd(&b, peak_max / 4));
	CHECK(wSwitchPanelBudgetAdd(&b, peak_max / 4));
	CHECK(b.live == peak_max && b.peak == peak_max);
	CHECK(!wSwitchPanelBudgetAdd(&b, 1));
	CHECK(b.live == peak_max);

	wSwitchPanelBudgetRemove(&b, peak_max / 4);
	CHECK(b.live == peak_max - peak_max / 4);
	CHECK(b.peak == peak_max);
	CHECK(wSwitchPanelBudgetAdd(&b, 1));

	wSwitchPanelBudgetInit(&b);
	CHECK(!wSwitchPanelBudgetAdd(&b, 0));
	CHECK(!wSwitchPanelBudgetAdd(&b, -1));
	CHECK(!wSwitchPanelBudgetAdd(&b, alloc_max + 1));
	CHECK(b.live == 0);

	wSwitchPanelBudgetRemove(&b, 4096);
	CHECK(b.live == 0);
	wSwitchPanelBudgetAdd(&b, 16);
	wSwitchPanelBudgetRemove(&b, 64);
	CHECK(b.live == 0);

	{
		long long one, four;

		CHECK(wSwitchPanelRasterBytes(77, 77, &one));
		CHECK(wSwitchPanelRasterBytes(154, 154, &four));
		CHECK(four == 4 * one);
	}

	{
		long long img, pix;
		int i;

		wSwitchPanelBudgetInit(&b);
		CHECK(wSwitchPanelRasterBytes(67, 67, &img));
		CHECK(wSwitchPanelRasterBytes(77, 77, &pix));
		for (i = 0; i < 10; i++)
			CHECK(wSwitchPanelBudgetAdd(&b, img) &&
			      wSwitchPanelBudgetAdd(&b, pix));
		CHECK(b.live == 10 * (img + pix));
	}

	{
		long long old_b, new_b;

		wSwitchPanelBudgetInit(&b);
		CHECK(wSwitchPanelRasterBytes(715, 132, &old_b));
		CHECK(wSwitchPanelRasterBytes(715, 220, &new_b));
		CHECK(wSwitchPanelBudgetAdd(&b, old_b));
			CHECK(wSwitchPanelBudgetAdd(&b, new_b));
		CHECK(b.live == old_b + new_b);
			wSwitchPanelBudgetRemove(&b, old_b);
		CHECK(b.live == new_b);
	}

	wSwitchPanelBudgetInit(&b);
	CHECK(wSwitchPanelBudgetAdd(&b, 715LL * 132 * 4));
	CHECK(wSwitchPanelBudgetAdd(&b, 1430LL * 264 * 4));
	{
		long long original = b.live;
		int i;

		for (i = 0; i < 100; i++) {
			CHECK(wSwitchPanelBudgetRebuild(&b, 715, 132, 715, 220, 2.0));
			CHECK(wSwitchPanelBudgetRebuild(&b, 715, 220, 715, 132, 2.0));
			CHECK(b.live == original);
		}
		CHECK(!wSwitchPanelBudgetRebuild(&b, 715, 132, 20000, 20000, 2.0));
		CHECK(b.live == original);
	}
}

static void test_panel_budget_lifecycle(void)
{
	WSwitchPanelBudget with_bg, plain, unchanged;
	WSwitchPanelSize size = { 0 };
	long long tile, dev_tile, icon, old_bg, old_dev, new_bg, new_dev;
	int i;

	size.tile = 77;
	size.dev_tile = 154;
	size.panel_width = 715;
	size.panel_height = 132;
	size.dev_panel_width = 1430;
	size.dev_panel_height = 264;
	CHECK(wSwitchPanelRasterBytes(77, 77, &tile));
	CHECK(wSwitchPanelRasterBytes(154, 154, &dev_tile));
	CHECK(wSwitchPanelRasterBytes(67, 67, &icon));
	CHECK(wSwitchPanelRasterBytes(715, 132, &old_bg));
	CHECK(wSwitchPanelRasterBytes(1430, 264, &old_dev));
	CHECK(wSwitchPanelRasterBytes(715, 220, &new_bg));
	CHECK(wSwitchPanelRasterBytes(1430, 440, &new_dev));

	CHECK(wSwitchPanelBudgetCreate(&plain, &size, 10, 64, false));
	CHECK(plain.live == 2 * tile + dev_tile + 10 * (icon + dev_tile));
	CHECK(wSwitchPanelBudgetCreate(&with_bg, &size, 10, 64, true));
	CHECK(with_bg.live == plain.live + old_bg + old_dev);
	CHECK(with_bg.peak == plain.live + 2 * old_bg + 3 * old_dev);
	for (i = 0; i < 100; i++) {
		CHECK(wSwitchPanelBudgetRebuild(&with_bg, 715, 132, 715, 220, 2.0));
		CHECK(with_bg.live == plain.live + new_bg + new_dev);
		CHECK(wSwitchPanelBudgetRebuild(&with_bg, 715, 220, 715, 132, 2.0));
		CHECK(with_bg.live == plain.live + old_bg + old_dev);
	}
	unchanged = with_bg;
	CHECK(!wSwitchPanelBudgetRebuild(&with_bg, 715, 132, 20000, 20000, 2.0));
	CHECK(with_bg.live == unchanged.live && with_bg.peak == unchanged.peak);
	size.panel_width = 20000;
	size.panel_height = 20000;
	size.dev_panel_width = 20000;
	size.dev_panel_height = 20000;
	CHECK(wSwitchPanelBudgetCreate(&plain, &size, 10, 64, false));
	unchanged = plain;
	CHECK(!wSwitchPanelBudgetCreate(&plain, &size, 10, 64, true));
	CHECK(plain.live == unchanged.live && plain.peak == unchanged.peak);
}

static void test_icon_dims(void)
{
	int w, h;

	CHECK(wSwitchPanelIconDims(64, 64, 77, &w, &h) && w == 64 && h == 64);
	CHECK(wSwitchPanelIconDims(4, 2, 4, &w, &h) && w == 4 && h == 2);

	CHECK(wSwitchPanelIconDims(64, 32, 4, &w, &h) && w == 4 && h == 2);
	CHECK(wSwitchPanelIconDims(32, 64, 4, &w, &h) && w == 2 && h == 4);
	CHECK(wSwitchPanelIconDims(100, 50, 77, &w, &h) && w == 77 && h == 38);

	CHECK(wSwitchPanelIconDims(1000, 1, 1, &w, &h) && w == 1 && h == 1);
	CHECK(wSwitchPanelIconDims(1, 1000, 1, &w, &h) && w == 1 && h == 1);
	CHECK(wSwitchPanelIconDims(1000, 1, 2, &w, &h) && w == 2 && h == 1);
	CHECK(wSwitchPanelIconDims(1, 1000, 2, &w, &h) && w == 1 && h == 2);
	CHECK(wSwitchPanelIconDims(1000, 3, 4, &w, &h) && w == 4 && h == 1);
	CHECK(wSwitchPanelIconDims(3, 1000, 4, &w, &h) && w == 1 && h == 4);
	CHECK(wSwitchPanelIconDims(64, 64, 1, &w, &h) && w == 1 && h == 1);
	CHECK(wSwitchPanelIconDims(64, 64, 2, &w, &h) && w == 2 && h == 2);

	CHECK(!wSwitchPanelIconDims(0, 4, 4, &w, &h));
	CHECK(!wSwitchPanelIconDims(4, -1, 4, &w, &h));
	CHECK(!wSwitchPanelIconDims(4, 4, 0, &w, &h));
	CHECK(!wSwitchPanelIconDims(4, 4, 4, NULL, &h));
}

int main(void)
{
	test_tile_size();
	test_device_px();
	test_strip_and_pos();
	test_limits();
	test_legacy_counts();
	test_optin_counts();
	test_calc_dims();
	test_overflow_sentinels();
	test_error_codes();
	test_raster_bytes();
	test_budget();
	test_panel_budget_lifecycle();
	test_icon_dims();

	return check_report("check_swp_size");
}
