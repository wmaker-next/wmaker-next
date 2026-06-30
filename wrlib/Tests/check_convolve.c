/*
 *  check_convolve - automated tests for RGrayscaleImage / RStackBlurImage
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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

int main(void)
{
	test_grayscale_rgb();
	test_grayscale_rgba();
	test_blur_flat();
	test_blur_impulse();
	test_blur_noop();

	return check_report("check_convolve");
}
