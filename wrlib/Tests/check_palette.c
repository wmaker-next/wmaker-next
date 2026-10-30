/*
 *  check_palette.c
 *
 *  Raster graphics library
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, see <https://www.gnu.org/licenses/>.
 */

/*
 * check_palette - unit tests for the wallpaper-derived UI palette
 * extractor (wrlib/palette.c). No X server required.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "wraster.h"
#include "check_common.h"

static int approx_eq(double a, double b)
{
	return fabs(a - b) < 1e-9;
}

static RImage *make_image(unsigned width, unsigned height, int alpha)
{
	RImage *img = RCreateImage(width, height, alpha);

	CHECK(img != NULL);
	return img;
}

static void set_pixel(RImage *img, int x, int y, unsigned char r,
		      unsigned char g, unsigned char b)
{
	int ch = (img->format == RRGBAFormat) ? 4 : 3;
	unsigned char *p = img->data + (y * img->width + x) * ch;

	p[0] = r; p[1] = g; p[2] = b;
	if (ch == 4)
		p[3] = 255;
}

static int color_eq(RColor c, unsigned char r, unsigned char g, unsigned char b)
{
	return c.red == r && c.green == g && c.blue == b;
}

static void fill_solid(RImage *img, unsigned char r, unsigned char g, unsigned char b)
{
	int x, y;

	for (y = 0; y < img->height; y++)
		for (x = 0; x < img->width; x++)
			set_pixel(img, x, y, r, g, b);
}

/* A 2-colour image quantises to exactly those 2 swatches with the
 * expected populations. */
static void test_two_colour_quantise(void)
{
	RImage *img;
	RPalette *pal;
	int y;

	img = make_image(4, 4, 0);
	if (!img)
		return;
	for (y = 0; y < 4; y++) {
		set_pixel(img, 0, y, 255, 0, 0);	/* red, left half */
		set_pixel(img, 1, y, 255, 0, 0);
		set_pixel(img, 2, y, 0, 0, 255);	/* blue, right half */
		set_pixel(img, 3, y, 0, 0, 255);
	}

	pal = RExtractPalette(img);
	CHECK(pal != NULL);
	CHECK(pal->count == 2);
	if (pal->count == 2) {
		RPaletteSwatch a = pal->swatches[0];
		RPaletteSwatch b = pal->swatches[1];
		int red = (a.r == 255 && a.g == 0 && a.b == 0) ||
			  (b.r == 255 && b.g == 0 && b.b == 0);
		int blue = (a.r == 0 && a.g == 0 && a.b == 255) ||
			   (b.r == 0 && b.g == 0 && b.b == 255);

		CHECK(red && blue);
		CHECK(a.weight == 8);
		CHECK(b.weight == 8);
	}

	free(pal);
	RReleaseImage(img);
}

/* Near-white and near-black images select dark/light ladders under Auto. */
static void test_auto_ladder(void)
{
	RImage *black, *white;
	RDynamicTheme *th;

	black = make_image(2, 2, 0);
	if (!black)
		return;
	fill_solid(black, 5, 5, 5);
	th = RDeriveDynamicTheme(black, RDYNAMIC_MODE_AUTO);
	CHECK(th != NULL);
	CHECK(th->ladder == RDYNAMIC_LADDER_DARK);
	free(th);
	RReleaseImage(black);

	white = make_image(2, 2, 0);
	if (!white)
		return;
	fill_solid(white, 250, 250, 250);
	th = RDeriveDynamicTheme(white, RDYNAMIC_MODE_AUTO);
	CHECK(th != NULL);
	CHECK(th->ladder == RDYNAMIC_LADDER_LIGHT);
	free(th);
	RReleaseImage(white);
}

/* Mode Light/Dark overrides the dominant-swatch decision. */
static void test_mode_override(void)
{
	RImage *black, *white;
	RDynamicTheme *th;

	black = make_image(2, 2, 0);
	if (!black)
		return;
	fill_solid(black, 5, 5, 5);
	/* Light mode forces the light ladder even though Auto would pick dark. */
	th = RDeriveDynamicTheme(black, RDYNAMIC_MODE_LIGHT);
	CHECK(th != NULL);
	CHECK(th->ladder == RDYNAMIC_LADDER_LIGHT);
	free(th);
	RReleaseImage(black);

	white = make_image(2, 2, 0);
	if (!white)
		return;
	fill_solid(white, 250, 250, 250);
	/* Dark mode forces the dark ladder even though Auto would pick light. */
	th = RDeriveDynamicTheme(white, RDYNAMIC_MODE_DARK);
	CHECK(th != NULL);
	CHECK(th->ladder == RDYNAMIC_LADDER_DARK);
	free(th);
	RReleaseImage(white);
}

/* WCAG text picker returns the higher-contrast of black/white. */
static void test_wcag_text_picker(void)
{
	RColor c;

	c = RPickWCAGText(5, 5, 5);
	CHECK(color_eq(c, 255, 255, 255));	/* white on near-black */

	c = RPickWCAGText(250, 250, 250);
	CHECK(color_eq(c, 0, 0, 0));		/* black on near-white */

	/* relative luminance sanity bounds */
	CHECK(approx_eq(RRelativeLuminance(0, 0, 0), 0.0));
	CHECK(approx_eq(RRelativeLuminance(255, 255, 255), 1.0));
	/* contrast ratio: white on black is 21:1 */
	CHECK(approx_eq(RWCAGContrastRatio(1.0, 0.0), 21.0));
}

/* A monochrome (all-black) image produces stepped variants: dominant back
 * stays black, secondary/tertiary are distinct, highlight reaches >= 3:1
 * against the menu back, and text is white throughout. */
static void test_monochrome_stepped_variants(void)
{
	RImage *img;
	RDynamicTheme *th;
	RColor dom, sec, ter, hl;
	double Lhl, Lsec;

	img = make_image(2, 2, 0);
	if (!img)
		return;
	fill_solid(img, 0, 0, 0);

	th = RDeriveDynamicTheme(img, RDYNAMIC_MODE_AUTO);
	CHECK(th != NULL);
	if (!th) {
		RReleaseImage(img);
		return;
	}

	dom = th->roles[RDYNAMIC_ROLE_DOMINANT_BACK];
	sec = th->roles[RDYNAMIC_ROLE_SECONDARY_BACK];
	ter = th->roles[RDYNAMIC_ROLE_TERTIARY_BACK];
	hl = th->roles[RDYNAMIC_ROLE_HIGHLIGHT_BACK];

	/* Tonal: surfaces at fixed tones (dark mode). Black wallpaper (S=0)
	 * produces neutral grays at L=6%, 16%, 26% — distinct from #000000
	 * and from each other. */
	CHECK(!color_eq(dom, 0, 0, 0));
	CHECK(!color_eq(sec, 0, 0, 0));
	CHECK(!color_eq(dom, sec.red, sec.green, sec.blue));
	CHECK(!color_eq(sec, ter.red, ter.green, ter.blue));

	/* highlight (primary tone 80%) >= 3:1 against the menu back */
	Lhl = RRelativeLuminance(hl.red, hl.green, hl.blue);
	Lsec = RRelativeLuminance(sec.red, sec.green, sec.blue);
	CHECK(RWCAGContrastRatio(Lhl, Lsec) >= 3.0);

	/* text is light (on dark surfaces) but not pure white — tonal */
	CHECK(th->roles[RDYNAMIC_ROLE_DOMINANT_TEXT].red > 200);
	CHECK(th->roles[RDYNAMIC_ROLE_SECONDARY_TEXT].red > 200);
	/* highlight text is dark (on the light accent) */
	CHECK(th->roles[RDYNAMIC_ROLE_HIGHLIGHT_TEXT].red < 80);

	free(th);
	RReleaseImage(img);
}

int main(void)
{
	test_two_colour_quantise();
	test_auto_ladder();
	test_mode_override();
	test_wcag_text_picker();
	test_monochrome_stepped_variants();

	return check_report("check_palette");
}
