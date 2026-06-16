/*
 *  check_image - automated tests for libwraster image geometry helpers
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Run by `make check`. Builds small RImages directly so no Display or
 *  RContext is required, and checks the scale, sub-image, flip, rotate,
 *  tile, centre, HSV and alpha-flatten helpers that icon and theme
 *  loading depend on.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <config.h>
#include <wraster.h>

static int failures;

#define CHECK(cond) do {                                                   \
	if (!(cond)) {                                                     \
	    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);\
	    failures++;                                                    \
	}                                                                  \
    } while (0)

static unsigned char *pixel(RImage *img, int x, int y)
{
	int ch = (img->format == RRGBAFormat) ? 4 : 3;

	return img->data + (y * img->width + x) * ch;
}

static void put_rgb(RImage *img, int x, int y, int r, int g, int b)
{
	unsigned char *p = pixel(img, x, y);

	p[0] = r; p[1] = g; p[2] = b;
}

/* 2x2 RGB image with a distinct red value at every position so any
 * permutation is detectable: (0,0)=10 (1,0)=20 (0,1)=30 (1,1)=40 */
static RImage *make_marker_image(void)
{
	RImage *img = RCreateImage(2, 2, False);

	put_rgb(img, 0, 0, 10, 0, 0);
	put_rgb(img, 1, 0, 20, 0, 0);
	put_rgb(img, 0, 1, 30, 0, 0);
	put_rgb(img, 1, 1, 40, 0, 0);
	return img;
}

static void test_create_limits(void)
{
	RImage *img;

	RErrorCode = RERR_NONE;
	CHECK(RCreateImage(0, 4, False) == NULL);
	CHECK(RErrorCode == RERR_NOMEMORY);

	CHECK(RCreateImage(4, 0, False) == NULL);
	CHECK(RCreateImage(20001, 1, False) == NULL);
	CHECK(RCreateImage(1, 20001, True) == NULL);

	img = RCreateImage(20000, 1, False);
	CHECK(img != NULL);
	if (img)
		RReleaseImage(img);
}

static void test_clone_retain(void)
{
	RImage *img, *clone, *same;

	img = make_marker_image();
	CHECK(img->refCount == 1);

	clone = RCloneImage(img);
	CHECK(clone && clone != img);
	CHECK(clone->refCount == 1);
	CHECK(img->refCount == 1);
	CHECK(memcmp(clone->data, img->data, 2 * 2 * 3) == 0);

	same = RRetainImage(img);
	CHECK(same == img);
	CHECK(img->refCount == 2);
	RReleaseImage(img);
	CHECK(img->refCount == 1);

	/* RFlipImage with no flip flags returns a retained source */
	same = RFlipImage(img, 0);
	CHECK(same == img);
	CHECK(img->refCount == 2);
	RReleaseImage(same);

	RReleaseImage(clone);
	RReleaseImage(img);
}

static void test_subimage(void)
{
	RImage *src, *sub;
	int x, y;

	src = RCreateImage(8, 8, True);
	for (y = 0; y < 8; y++)
		for (x = 0; x < 8; x++) {
			unsigned char *p = pixel(src, x, y);
			p[0] = x; p[1] = y; p[2] = 0; p[3] = 200;
		}

	sub = RGetSubImage(src, 2, 3, 4, 2);
	CHECK(sub && sub->width == 4 && sub->height == 2);
	CHECK(sub->format == RRGBAFormat);
	CHECK(pixel(sub, 0, 0)[0] == 2 && pixel(sub, 0, 0)[1] == 3);
	CHECK(pixel(sub, 3, 1)[0] == 5 && pixel(sub, 3, 1)[1] == 4);
	CHECK(pixel(sub, 0, 0)[3] == 200);
	RReleaseImage(sub);

	/* a request that overruns the source is clamped, not refused */
	sub = RGetSubImage(src, 6, 6, 10, 10);
	CHECK(sub && sub->width == 2 && sub->height == 2);
	CHECK(pixel(sub, 1, 1)[0] == 7 && pixel(sub, 1, 1)[1] == 7);
	RReleaseImage(sub);

	RReleaseImage(src);
}

static void test_scale(void)
{
	RImage *src, *out;

	src = RCreateImage(2, 2, True);
	pixel(src, 0, 0)[0] = 50;  pixel(src, 0, 0)[3] = 200;
	pixel(src, 1, 0)[0] = 60;  pixel(src, 1, 0)[3] = 200;
	pixel(src, 0, 1)[0] = 70;  pixel(src, 0, 1)[3] = 200;
	pixel(src, 1, 1)[0] = 80;  pixel(src, 1, 1)[3] = 200;

	/* scaling to the same size yields a clone, not a shared reference */
	out = RScaleImage(src, 2, 2);
	CHECK(out && out != src && out->refCount == 1);
	CHECK(out->format == RRGBAFormat);
	CHECK(memcmp(out->data, src->data, 2 * 2 * 4) == 0);
	RReleaseImage(out);

	/* nearest-neighbour upscale, each quadrant copies its source pixel */
	out = RScaleImage(src, 4, 4);
	CHECK(out && out->width == 4 && out->height == 4);
	CHECK(out->format == RRGBAFormat);
	CHECK(pixel(out, 0, 0)[0] == 50 && pixel(out, 1, 1)[0] == 50);
	CHECK(pixel(out, 3, 0)[0] == 60);
	CHECK(pixel(out, 0, 3)[0] == 70);
	CHECK(pixel(out, 3, 3)[0] == 80);
	CHECK(pixel(out, 3, 3)[3] == 200);
	RReleaseImage(out);

	/* RSmoothScaleImage always emits RGB regardless of source alpha */
	out = RSmoothScaleImage(src, 4, 4);
	CHECK(out && out->format == RRGBFormat);
	RReleaseImage(out);

	RReleaseImage(src);
}

static void test_flip(void)
{
	RImage *src, *out;

	src = make_marker_image();

	out = RFlipImage(src, RHorizontalFlip);
	CHECK(out && out != src && out->format == RRGBFormat);
	CHECK(pixel(out, 0, 0)[0] == 20 && pixel(out, 1, 0)[0] == 10);
	CHECK(pixel(out, 0, 1)[0] == 40 && pixel(out, 1, 1)[0] == 30);
	RReleaseImage(out);

	out = RFlipImage(src, RVerticalFlip);
	CHECK(out != NULL);
	CHECK(pixel(out, 0, 0)[0] == 30 && pixel(out, 1, 0)[0] == 40);
	CHECK(pixel(out, 0, 1)[0] == 10 && pixel(out, 1, 1)[0] == 20);
	RReleaseImage(out);

	out = RFlipImage(src, RHorizontalFlip | RVerticalFlip);
	CHECK(out != NULL);
	CHECK(pixel(out, 0, 0)[0] == 40 && pixel(out, 1, 1)[0] == 10);
	RReleaseImage(out);

	CHECK(RFlipImage(NULL, RHorizontalFlip) == NULL);

	RReleaseImage(src);
}

static void test_rotate(void)
{
	RImage *src, *out;

	src = make_marker_image();

	out = RRotateImage(src, 0.0F);
	CHECK(out && out != src);
	CHECK(memcmp(out->data, src->data, 2 * 2 * 3) == 0);
	RReleaseImage(out);

	/* 90 degrees clockwise: bottom-left becomes top-left */
	out = RRotateImage(src, 90.0F);
	CHECK(out && out->width == 2 && out->height == 2);
	CHECK(pixel(out, 0, 0)[0] == 30 && pixel(out, 1, 0)[0] == 10);
	CHECK(pixel(out, 0, 1)[0] == 40 && pixel(out, 1, 1)[0] == 20);
	RReleaseImage(out);

	out = RRotateImage(src, 180.0F);
	CHECK(out != NULL);
	CHECK(pixel(out, 0, 0)[0] == 40 && pixel(out, 1, 1)[0] == 10);
	RReleaseImage(out);

	RReleaseImage(src);

	/* non-square source, 90 degrees swaps width and height */
	src = RCreateImage(3, 1, False);
	put_rgb(src, 0, 0, 1, 0, 0);
	put_rgb(src, 1, 0, 2, 0, 0);
	put_rgb(src, 2, 0, 3, 0, 0);
	out = RRotateImage(src, 90.0F);
	CHECK(out && out->width == 1 && out->height == 3);
	CHECK(pixel(out, 0, 0)[0] == 1);
	CHECK(pixel(out, 0, 2)[0] == 3);
	RReleaseImage(out);
	RReleaseImage(src);
}

static void test_tile(void)
{
	RImage *tile, *out;

	tile = make_marker_image();

	out = RMakeTiledImage(tile, 5, 3);
	CHECK(out && out->width == 5 && out->height == 3);
	CHECK(out->format == RRGBFormat);
	/* tile repeats with the source period */
	CHECK(pixel(out, 0, 0)[0] == 10 && pixel(out, 2, 0)[0] == 10);
	CHECK(pixel(out, 1, 0)[0] == 20 && pixel(out, 3, 0)[0] == 20);
	CHECK(pixel(out, 0, 2)[0] == 10 && pixel(out, 1, 2)[0] == 20);
	RReleaseImage(out);

	RReleaseImage(tile);
}

static void test_center(void)
{
	RImage *src, *out;
	RColor bg;

	bg.red = 9; bg.green = 9; bg.blue = 9; bg.alpha = 255;

	src = make_marker_image();

	out = RMakeCenteredImage(src, 4, 4, &bg);
	CHECK(out && out->width == 4 && out->height == 4);
	CHECK(out->format == RRGBFormat);
	/* border filled with the background colour */
	CHECK(pixel(out, 0, 0)[0] == 9 && pixel(out, 3, 3)[0] == 9);
	/* the 2x2 source sits at offset (1,1) */
	CHECK(pixel(out, 1, 1)[0] == 10 && pixel(out, 2, 1)[0] == 20);
	CHECK(pixel(out, 1, 2)[0] == 30 && pixel(out, 2, 2)[0] == 40);
	RReleaseImage(out);

	RReleaseImage(src);
}

static void check_hsv(int r, int g, int b, int h, int s, int v)
{
	RColor rgb, back;
	RHSVColor hsv;

	rgb.red = r; rgb.green = g; rgb.blue = b; rgb.alpha = 0;
	RRGBtoHSV(&rgb, &hsv);
	CHECK(hsv.hue == h);
	CHECK(hsv.saturation == s);
	CHECK(hsv.value == v);

	RHSVtoRGB(&hsv, &back);
	CHECK(back.red == r && back.green == g && back.blue == b);
}

static void test_hsv(void)
{
	check_hsv(0, 0, 0, 0, 0, 0);
	check_hsv(255, 255, 255, 0, 0, 255);
	check_hsv(255, 0, 0, 0, 255, 255);
	check_hsv(0, 255, 0, 120, 255, 255);
	check_hsv(0, 0, 255, 240, 255, 255);
	check_hsv(255, 255, 0, 60, 255, 255);
}

static void test_combine_color(void)
{
	RImage *img;
	RColor bg;
	unsigned char *p;

	bg.red = 100; bg.green = 100; bg.blue = 100; bg.alpha = 255;

	/* RGB images have no alpha to flatten, so the call is a no-op */
	img = RCreateImage(1, 1, False);
	put_rgb(img, 0, 0, 50, 60, 70);
	RCombineImageWithColor(img, &bg);
	CHECK(pixel(img, 0, 0)[0] == 50);
	CHECK(pixel(img, 0, 0)[1] == 60);
	CHECK(pixel(img, 0, 0)[2] == 70);
	RReleaseImage(img);

	img = RCreateImage(1, 2, True);
	p = pixel(img, 0, 0);
	p[0] = 200; p[1] = 0; p[2] = 0; p[3] = 255;
	p = pixel(img, 0, 1);
	p[0] = 200; p[1] = 0; p[2] = 0; p[3] = 0;

	RCombineImageWithColor(img, &bg);

	/* fully opaque pixel keeps its own colour */
	p = pixel(img, 0, 0);
	CHECK(p[0] >= 199 && p[1] == 0 && p[2] == 0);
	/* fully transparent pixel becomes the background */
	p = pixel(img, 0, 1);
	CHECK(p[0] >= 99 && p[0] <= 100);
	CHECK(p[1] >= 99 && p[1] <= 100);
	CHECK(p[2] >= 99 && p[2] <= 100);

	RReleaseImage(img);
}

int main(void)
{
	test_create_limits();
	test_clone_retain();
	test_subimage();
	test_scale();
	test_flip();
	test_rotate();
	test_tile();
	test_center();
	test_hsv();
	test_combine_color();

	if (failures) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	printf("ok - check_image\n");
	return 0;
}
