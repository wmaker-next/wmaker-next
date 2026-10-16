/*
 *  check_convolve - automated tests for RGrayscaleImage / RBlurImage /
 *  RStackBlurImage
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
 *
 *  Run by `make check`. Builds small RImages directly so no Display or
 *  RContext is required.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <config.h>
#include <wraster.h>

#include "check_common.h"

static void test_grayscale_rgb(void)
{
	RImage *img;
	unsigned char *d;

	img = RCreateImage(2, 1, 0);
	CHECK(img != NULL);
	if (!img)
		return;

	d = img->data;
	/* pure red and pure white */
	d[0] = 255; d[1] = 0;   d[2] = 0;
	d[3] = 255; d[4] = 255; d[5] = 255;

	RGrayscaleImage(img);

	/* BT.601 luma of pure red, 77*255 >> 8 == 76 */
	CHECK(d[0] == 76 && d[1] == 76 && d[2] == 76);
	/* white stays white (within the >>8 truncation) */
	CHECK(d[3] == d[4] && d[4] == d[5]);
	CHECK(d[3] >= 254);

	RReleaseImage(img);
}

static void test_grayscale_rgba(void)
{
	RImage *img;
	unsigned char *d;

	img = RCreateImage(1, 1, 1);
	CHECK(img != NULL);
	if (!img)
		return;

	d = img->data;
	d[0] = 0; d[1] = 255; d[2] = 0; d[3] = 123;

	RGrayscaleImage(img);

	CHECK(d[0] == d[1] && d[1] == d[2]);
	/* alpha must be untouched */
	CHECK(d[3] == 123);

	RReleaseImage(img);
}

/* A flat-colour image must be a fixed point of the blur for any radius,
 * including radii larger than the image. Exercises both formats. */
static void test_blur_flat(void)
{
	RImage *img;
	RColor fill;
	unsigned char *d;
	int i, n, ch, alpha;
	unsigned r;

	for (alpha = 0; alpha <= 1; alpha++) {
		ch = alpha ? 4 : 3;

		fill.red = 40; fill.green = 120; fill.blue = 200; fill.alpha = 255;

		for (r = 1; r <= 50; r += 7) {
			img = RCreateImage(17, 13, alpha);
			CHECK(img != NULL);
			if (!img)
				return;
			RFillImage(img, &fill);

			CHECK(RStackBlurImage(img, r) == 1);

			d = img->data;
			n = img->width * img->height;
			for (i = 0; i < n; i++) {
				if (d[i * ch] != 40 || d[i * ch + 1] != 120 ||
				    d[i * ch + 2] != 200) {
					fprintf(stderr,
						"FAIL blur-flat: alpha=%d r=%u pixel %d -> %d/%d/%d\n",
						alpha, r, i,
						d[i * ch], d[i * ch + 1], d[i * ch + 2]);
					check_failures++;
					break;
				}
			}
			RReleaseImage(img);
		}
	}
}

/* A single bright pixel on a black field must spread symmetrically and
 * the centre must stay the brightest sample. */
static void test_blur_impulse(void)
{
	RImage *img;
	unsigned char *d;
	int w, h, cx, cy, ch, off, c, l, r, t, b;

	w = 31; h = 31; cx = w / 2; cy = h / 2;

	img = RCreateImage(w, h, 0);
	CHECK(img != NULL);
	if (!img)
		return;

	ch = 3;
	d = img->data;
	memset(d, 0, (size_t)w * h * ch);
	off = (cy * w + cx) * ch;
	d[off] = d[off + 1] = d[off + 2] = 255;

	CHECK(RStackBlurImage(img, 5) == 1);

	c = d[off];
	l = d[(cy * w + cx - 3) * ch];
	r = d[(cy * w + cx + 3) * ch];
	t = d[((cy - 3) * w + cx) * ch];
	b = d[((cy + 3) * w + cx) * ch];

	/* exact reference values for r=5 (r1=6, divsum=36): after the
	 * horizontal pass the centre is 255*6/36 = 42 and offset 3 is
	 * 255*3/36 = 21, after the vertical pass centre = 42*6/36 = 7
	 * and offset 3 = 42*3/36 = 3. Pinned so a change to the divide
	 * (multiply-by-reciprocal, float rounding mode) cannot drift
	 * silently while still satisfying the relational checks below. */
	CHECK(c == 7);
	CHECK(l == 3);

	/* centre brightest */
	CHECK(c >= l && c >= r && c >= t && c >= b);
	/* horizontal and vertical symmetry */
	CHECK(l == r);
	CHECK(t == b);
	/* separable kernel, so the two axes match */
	CHECK(l == t);
	/* energy actually reached the neighbours */
	CHECK(l > 0);

	RReleaseImage(img);
}

static void test_blur_noop(void)
{
	RImage *img;

	img = RCreateImage(4, 4, 0);
	CHECK(img != NULL);
	if (!img)
		return;

	/* radius 0 and tiny images must succeed without touching data */
	CHECK(RStackBlurImage(img, 0) == 1);
	RReleaseImage(img);

	img = RCreateImage(1, 1, 1);
	CHECK(RStackBlurImage(img, 10) == 1);
	RReleaseImage(img);
}

/* A single impulse of 100 on a zero field must spread as the documented
 * 1 1 1 / 1 2 1 / 1 1 1 mask over 10: the centre becomes 20, the eight
 * surrounding interior samples 10, and the border rows/columns, which
 * are never convolved, stay 0. Exercises both pixel formats. */
static void test_blur3_impulse(void)
{
	RImage *img;
	unsigned char *d;
	int x, y, i, ch, alpha, got, want;

	for (alpha = 0; alpha <= 1; alpha++) {
		ch = alpha ? 4 : 3;

		img = RCreateImage(5, 5, alpha);
		CHECK(img != NULL);
		if (!img)
			return;
		memset(img->data, 0, (size_t)5 * 5 * ch);
		for (i = 0; i < 3; i++)
			img->data[(2 * 5 + 2) * ch + i] = 100;

		CHECK(RBlurImage(img) == 1);

		d = img->data;
		for (y = 0; y < 5; y++) {
			for (x = 0; x < 5; x++) {
				if (x == 2 && y == 2)
					want = 20;
				else if (x >= 1 && x <= 3 && y >= 1 && y <= 3)
					want = 10;
				else
					want = 0;

				for (i = 0; i < 3; i++) {
					got = d[(y * 5 + x) * ch + i];
					if (got != want) {
						fprintf(stderr,
							"FAIL blur3-impulse: alpha=%d (%d,%d) ch%d -> %d, want %d\n",
							alpha, x, y, i, got, want);
						check_failures++;
					}
				}
			}
		}
		RReleaseImage(img);
	}
}

/* Images without an interior must come back untouched. */
static void test_blur3_tiny(void)
{
	RImage *img;
	unsigned char *d;
	int h, alpha, ch, n, i, untouched;

	for (alpha = 0; alpha <= 1; alpha++) {
		for (h = 1; h <= 2; h++) {
			img = RCreateImage(4, h, alpha);
			CHECK(img != NULL);
			if (!img)
				return;
			ch = alpha ? 4 : 3;
			n = 4 * h * ch;
			d = img->data;
			for (i = 0; i < n; i++)
				d[i] = (unsigned char)(i + 1);

			CHECK(RBlurImage(img) == 1);

			untouched = 1;
			for (i = 0; i < n; i++)
				untouched &= (d[i] == (unsigned char)(i + 1));
			CHECK(untouched);

			RReleaseImage(img);
		}
	}
}

int main(void)
{
	test_grayscale_rgb();
	test_grayscale_rgba();
	test_blur_flat();
	test_blur_impulse();
	test_blur_noop();
	test_blur3_impulse();
	test_blur3_tiny();

	return check_report("check_convolve");
}
