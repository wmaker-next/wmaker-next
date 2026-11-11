/*
 *  check_iconfit.c
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

/* real image scaling for the switch panel icon path */

#include "wconfig.h"

#include <stdio.h>

#include <wraster.h>

#include "iconscale.h"
#include "swp_size.h"

#include "check_common.h"

static RImage *make_image(int w, int h)
{
	RColor color;
	RImage *img = RCreateImage(w, h, 1);

	if (!img)
		return NULL;
	color.red = 120;
	color.green = 60;
	color.blue = 200;
	color.alpha = 255;
	RFillImage(img, &color);
	return img;
}

static void test_validate_icon_size(void)
{
	RImage *img, *out;

	img = make_image(50, 50);
	out = wIconValidateIconSize(img, 64);
	CHECK(out == img);
	CHECK(out && out->width == 50 && out->height == 50);
	RReleaseImage(out);

	img = make_image(64, 64);
	out = wIconValidateIconSize(img, 32);
	CHECK(out && out->width == 29 && out->height == 29);
	RReleaseImage(out);

	img = make_image(100, 50);
	out = wIconValidateIconSize(img, 64);
	CHECK(out && out->width == 61 && out->height == 30);
	RReleaseImage(out);

	img = make_image(64, 64);
	out = wIconValidateIconSize(img, 3);
	CHECK(out && out->width == 1 && out->height == 1);
	RReleaseImage(out);

	img = make_image(64, 64);
	out = wIconValidateIconSize(img, 2);
	CHECK(out && out->width == 1 && out->height == 1);
	RReleaseImage(out);

	img = make_image(64, 64);
	out = wIconValidateIconSize(img, 1);
	CHECK(out && out->width == 1 && out->height == 1);
	RReleaseImage(out);

	img = make_image(1000, 1);
	out = wIconValidateIconSize(img, 3);
	CHECK(out && out->width == 1 && out->height == 1);
	RReleaseImage(out);

	img = make_image(1, 1000);
	out = wIconValidateIconSize(img, 1);
	CHECK(out && out->width == 1 && out->height == 1);
	RReleaseImage(out);

	img = make_image(1000, 2);
	out = wIconValidateIconSize(img, 64);
	CHECK(out && out->width == 61 && out->height == 1);
	RReleaseImage(out);

	img = make_image(2, 1000);
	out = wIconValidateIconSize(img, 64);
	CHECK(out && out->width == 1 && out->height == 61);
	RReleaseImage(out);

	CHECK(wIconValidateIconSize(NULL, 64) == NULL);

	img = make_image(100, 50);
	out = wIconValidateIconSizeStrict(img, 64);
	CHECK(out && out->width == 61 && out->height == 30);
	RReleaseImage(out);
}

static void test_tile_fit_render(void)
{
	static const struct {
		int w, h;
	} sources[] = {
		{ 64, 64 }, { 100, 50 }, { 50, 100 },
		{ 1000, 1 }, { 1, 1000 }, { 1000, 3 }, { 3, 1000 },
	};
	static const struct {
		int icon_size, tile;
	} cases[] = {
		{ 1, 1 }, { 2, 2 }, { 3, 4 }, { 64, 77 },
	};
	size_t i, c;

	for (i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
		for (c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
			RImage *img, *out, *tile;
			int w, h;

			img = make_image(sources[i].w, sources[i].h);
			CHECK(img != NULL);
			if (!img)
				continue;
			img = wIconValidateIconSize(img, cases[c].icon_size);
			CHECK(img != NULL);
			if (!img)
				continue;
			CHECK(wSwitchPanelIconDims(img->width, img->height,
						  cases[c].tile, &w, &h));
			CHECK(w >= 1 && h >= 1 && w <= cases[c].tile &&
			      h <= cases[c].tile);
			out = RScaleImage(img, w, h);
			CHECK(out != NULL);
			if (out) {
				CHECK(out->width == w && out->height == h);
				tile = RCreateImage(cases[c].tile, cases[c].tile, 1);
				CHECK(tile != NULL);
				if (tile) {
					RColor background = { 0, 0, 0, 255 };
					RColor pixel;

					RFillImage(tile, &background);
					RCombineArea(tile, out, 0, 0, w, h,
						     (cases[c].tile - w) / 2,
						     (cases[c].tile - h) / 2);
					RGetPixel(tile, (cases[c].tile - w) / 2,
						  (cases[c].tile - h) / 2, &pixel);
					CHECK(pixel.red == 120 && pixel.green == 60 &&
					      pixel.blue == 200);
					RReleaseImage(tile);
				}
				RReleaseImage(out);
			}
			RReleaseImage(img);
		}
	}
}

int main(void)
{
	test_validate_icon_size();
	test_tile_fit_render();

	return check_report("check_iconfit");
}
