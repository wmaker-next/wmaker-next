/*
 *  check_load - automated decoder tests for libwraster
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Run by `make check`. Writes tiny PNM samples to a temp directory,
 *  decodes each through RLoadImage(NULL, ...) (the PNM and normalized
 *  XPM loaders ignore the context), and verifies dimensions and a known
 *  pixel. Also exercises RGetImageFromXPMData() with a transparent
 *  colour to confirm the RGB bytes are zero, not heap garbage.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <config.h>
#include <wraster.h>
#include <WINGs/WUtil.h>

#include "check_common.h"

/* RLoadImage is annotated nonnull(context) but the PNM and normalized-XPM
 * dispatches never touch it, and these tests must run without a Display.
 * Suppress both the compile-time warning and the runtime UBSan check for
 * this wrapper only. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnonnull"
#if defined(__GNUC__) || defined(__clang__)
__attribute__((no_sanitize("nonnull-attribute")))
#endif
static RImage *load_noctx(const char *path)
{
	return RLoadImage(NULL, path, 0);
}
#if defined(__GNUC__) || defined(__clang__)
__attribute__((no_sanitize("nonnull-attribute")))
#endif
static RImage *load_noctx_sized(const char *path, unsigned w, unsigned h)
{
	return RLoadImageAtSize(NULL, path, 0, w, h);
}
#ifndef USE_XPM
#if defined(__GNUC__) || defined(__clang__)
__attribute__((no_sanitize("nonnull-attribute")))
#endif
static RImage *xpmdata_noctx(char **data)
{
	return RGetImageFromXPMData(NULL, data);
}
#endif
#pragma GCC diagnostic pop

static char tmpdir[] = "/tmp/wrlib-check-XXXXXX";
static const char *srcdir;

static const char *fixture_path(const char *name)
{
	static char path[256];

	snprintf(path, sizeof(path), "%s/%s", srcdir, name);
	return path;
}

static char *write_sample(const char *name, const void *data, size_t len)
{
	static char path[256];
	FILE *f;

	snprintf(path, sizeof(path), "%s/%s", tmpdir, name);
	f = fopen(path, "wb");
	if (!f) {
		perror(path);
		exit(2);
	}
	fwrite(data, 1, len, f);
	fclose(f);
	return path;
}

static void expect_image(const char *what, RImage *img, int w, int h,
			 int r, int g, int b)
{
	int bpp;

	if (!img) {
		fprintf(stderr, "FAIL %s: RLoadImage returned NULL (%s)\n",
			what, RMessageForError(RErrorCode));
		check_failures++;
		return;
	}
	CHECK(img->width == w);
	CHECK(img->height == h);

	bpp = (img->format == RRGBAFormat) ? 4 : 3;
	CHECK(img->data[0] == r);
	CHECK(img->data[1] == g);
	CHECK(img->data[2] == b);
	(void)bpp;

	RReleaseImage(img);
}

static void test_pnm(void)
{
	const char *s;
	char *p;
	FILE *f;

	/* P1 ascii bitmap, 2x1, pixels 1 0 -> black white */
	s = "P1\n2 1\n1 0\n";
	p = write_sample("a.pbm", s, strlen(s));
	expect_image("P1", load_noctx(p), 2, 1, 0, 0, 0);

	/* P2 ascii graymap, 2x1, max 255, pixels 64 200 */
	s = "P2\n2 1\n255\n64 200\n";
	p = write_sample("a.pgm", s, strlen(s));
	expect_image("P2", load_noctx(p), 2, 1, 64, 64, 64);

	/* P3 ascii pixmap, 1x1, max 255, pixel (10 20 30) */
	s = "P3\n1 1\n255\n10 20 30\n";
	p = write_sample("a.ppm", s, strlen(s));
	expect_image("P3", load_noctx(p), 1, 1, 10, 20, 30);

	/* P4 binary bitmap, 2x1, byte 0x80 -> first pixel set (black) */
	s = "P4\n2 1\n";
	p = write_sample("b.pbm", s, strlen(s));
	f = fopen(p, "ab");
	fputc(0x80, f);
	fclose(f);
	expect_image("P4", load_noctx(p), 2, 1, 0, 0, 0);

	/* P5 binary graymap, 2x1, max 255, bytes 64 200 */
	s = "P5\n2 1\n255\n";
	p = write_sample("b.pgm", s, strlen(s));
	f = fopen(p, "ab");
	fputc(0x40, f); fputc(0xc8, f);
	fclose(f);
	expect_image("P5", load_noctx(p), 2, 1, 64, 64, 64);

	/* P6 binary pixmap, 1x1, max 255, bytes 10 20 30 */
	s = "P6\n1 1\n255\n";
	p = write_sample("b.ppm", s, strlen(s));
	f = fopen(p, "ab");
	fputc(10, f); fputc(20, f); fputc(30, f);
	fclose(f);
	expect_image("P6", load_noctx(p), 1, 1, 10, 20, 30);
}

/* Formats wrlib has no native loader for, decoded via the vendored
 * stb_image. BMP and TGA store BGR on disk so the writer swaps R/B,
 * the loader must swap back. GIF carries a transparent index so the
 * test asserts that alpha is decoded (the previous libgif loader
 * dropped it). */
static void test_stb_formats(void)
{
	RImage *img;
	char *p;

	/* 2x1 24-bit BMP, no compression, bottom-up rows. Pixel (0,0) is
	 * (10,20,30), pixel (1,0) is (40,50,60). Two pad bytes round the
	 * 6-byte row to 8. */
	static const unsigned char bmp[] = {
		'B','M', 62,0,0,0, 0,0,0,0, 54,0,0,0,
		40,0,0,0, 2,0,0,0, 1,0,0,0, 1,0, 24,0,
		0,0,0,0, 8,0,0,0, 0,0,0,0, 0,0,0,0,
		0,0,0,0, 0,0,0,0,
		30,20,10, 60,50,40, 0,0
	};
	p = write_sample("a.bmp", bmp, sizeof(bmp));
	expect_image("BMP", load_noctx(p), 2, 1, 10, 20, 30);

	/* 2x1 24-bit TGA, type 2 uncompressed, top-left origin */
	static const unsigned char tga[] = {
		0,0,2, 0,0,0,0,0, 0,0,0,0, 2,0,1,0, 24,0x20,
		30,20,10, 60,50,40
	};
	p = write_sample("a.tga", tga, sizeof(tga));
	expect_image("TGA", load_noctx(p), 2, 1, 10, 20, 30);

	/* 2x1 GIF89a with a graphic-control extension marking palette
	 * index 0 as transparent. Two-entry palette: 0=red, 1=green.
	 * LZW-coded pixels are [0,1] so pixel 0 is transparent and
	 * pixel 1 opaque green. */
	static const unsigned char gif[] = {
		'G','I','F','8','9','a',
		2,0, 1,0,                          /* logical screen 2x1 */
		0xf0, 0, 0,                        /* GCT present, 2 entries */
		255,0,0,  0,255,0,                 /* palette: red, green */
		0x21,0xf9, 4, 0x01,0,0, 0, 0,      /* GCE: transparent index 0 */
		0x2c, 0,0,0,0, 2,0,1,0, 0,         /* image descriptor 2x1 */
		2, 2, 0x44,0x0a, 0,                /* LZW min=2, [clear,0,1,eoi] */
		0x3b
	};
	p = write_sample("a.gif", gif, sizeof(gif));
	img = load_noctx(p);
	if (!img) {
		fprintf(stderr, "FAIL GIF: RLoadImage NULL (%s)\n",
			RMessageForError(RErrorCode));
		check_failures++;
	} else {
		CHECK(img->width == 2 && img->height == 1);
		CHECK(img->format == RRGBAFormat);
		CHECK(img->data[3] == 0);                  /* transparent */
		CHECK(img->data[4] == 0 &&
		      img->data[5] == 255 &&
		      img->data[6] == 0);                  /* green */
		CHECK(img->data[7] == 255);                /* opaque */
		RReleaseImage(img);
	}

	/* unrecognised content with no ImageMagick falls through to a
	 * format error rather than crashing the stb fallback */
	p = write_sample("junk.dat", "not an image format\n", 20);
	CHECK(load_noctx(p) == NULL);
}

static void expect_golden(const char *what, RImage *img, int w, int h,
			  int fr, int fg, int fb, int lr, int lg, int lb)
{
	int bpp, last;

	if (!img) {
		fprintf(stderr, "FAIL %s: RLoadImage returned NULL (%s)\n",
			what, RMessageForError(RErrorCode));
		check_failures++;
		return;
	}
	CHECK(img->width == w);
	CHECK(img->height == h);

	bpp = (img->format == RRGBAFormat) ? 4 : 3;
	last = (w * h - 1) * bpp;

	CHECK(img->data[0] == fr);
	CHECK(img->data[1] == fg);
	CHECK(img->data[2] == fb);
	CHECK(img->data[last + 0] == lr);
	CHECK(img->data[last + 1] == lg);
	CHECK(img->data[last + 2] == lb);

	RReleaseImage(img);
}

/* Golden-file decode tests for all six PNM variants. These pin the exact
 * output of the current loader so behaviour stays identical when the
 * backend is later swapped (stb_image only handles P5/P6 natively). Each
 * fixture is a 3x2 image with a known pattern, and both the first and the
 * last pixel are checked so row stride and end-of-buffer placement are
 * covered as well as the basic colour mapping. */
static void test_pnm_golden(void)
{
	const char *p;

	/* P1 ascii bitmap: 0->white, 1->black */
	p = fixture_path("pnm-p1.pbm");
	expect_golden("golden P1", load_noctx(p), 3, 2,
		      255, 255, 255, 0, 0, 0);

	/* P2 ascii greymap: grey expands to R=G=B */
	p = fixture_path("pnm-p2.pgm");
	expect_golden("golden P2", load_noctx(p), 3, 2,
		      255, 255, 255, 32, 32, 32);

	/* P3 ascii pixmap: row 0 red/green/blue, row 1 white/black/grey128 */
	p = fixture_path("pnm-p3.ppm");
	expect_golden("golden P3", load_noctx(p), 3, 2,
		      255, 0, 0, 128, 128, 128);

	/* P4 raw bitmap: packed MSB first, rows padded to byte boundary */
	p = fixture_path("pnm-p4.pbm");
	expect_golden("golden P4", load_noctx(p), 3, 2,
		      255, 255, 255, 0, 0, 0);

	/* P5 raw greymap */
	p = fixture_path("pnm-p5.pgm");
	expect_golden("golden P5", load_noctx(p), 3, 2,
		      255, 255, 255, 32, 32, 32);

	/* P6 raw pixmap */
	p = fixture_path("pnm-p6.ppm");
	expect_golden("golden P6", load_noctx(p), 3, 2,
		      255, 0, 0, 128, 128, 128);
}

static void test_pnm_reject(void)
{
	char *p;
	RImage *img;

	const char *s;

	/* dimension overflow must be rejected, not handed to RCreateImage */
	s = "P6\n9999999999 1\n255\n";
	p = write_sample("huge.ppm", s, strlen(s));
	img = load_noctx(p);
	CHECK(img == NULL);

	/* 16-bit P6 is accepted via stb_image and downconverted to 8-bit
	 * (the previous bespoke loader rejected maxval > 255). Six 0x78
	 * bytes are three big-endian 0x7878 samples, 0x7878>>8 == 0x78. */
	s = "P6\n1 1\n65535\nxxxxxx";
	p = write_sample("wide.ppm", s, strlen(s));
	img = load_noctx(p);
	CHECK(img != NULL);
	if (img) {
		CHECK(img->width == 1 && img->height == 1);
		CHECK(img->data[0] == 0x78);
		RReleaseImage(img);
	}

	/* truncated P6 pixel block must still be rejected */
	s = "P6\n2 2\n255\nrgb";
	p = write_sample("short.ppm", s, strlen(s));
	img = load_noctx(p);
	CHECK(img == NULL);
}

static void test_xpm_transparent(void)
{
#ifdef USE_XPM
	/* the libXpm-backed loader needs a Display, only the normalized
	 * fallback (built when --without-xpm) can run headless */
	printf("skip - xpm transparent (libXpm build needs a Display)\n");
	return;
#else
	/* 2x1 XPM, first pixel transparent (None), second red */
	static char *xpm[] = {
		"2 1 2 1",
		"  c None",
		"r c #ff0000",
		" r"
	};
	RImage *img;

	img = xpmdata_noctx(xpm);
	if (!img) {
		fprintf(stderr, "FAIL xpm: RGetImageFromXPMData NULL (%s)\n",
			RMessageForError(RErrorCode));
		check_failures++;
		return;
	}
	CHECK(img->width == 2 && img->height == 1);
	CHECK(img->format == RRGBAFormat);

	/* transparent pixel must have RGB cleared to 0, not heap garbage */
	CHECK(img->data[0] == 0);
	CHECK(img->data[1] == 0);
	CHECK(img->data[2] == 0);
	CHECK(img->data[3] == 0);   /* alpha */

	/* second pixel is opaque red */
	CHECK(img->data[4] == 255);
	CHECK(img->data[5] == 0);
	CHECK(img->data[6] == 0);
	CHECK(img->data[7] == 255);

	RReleaseImage(img);
#endif
}

static void test_svg(void)
{
	RImage *img;
	char *path;
	static const char golden[] =
		"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"2\" height=\"2\">"
		"<rect width=\"2\" height=\"2\" fill=\"#4080c0\"/></svg>";
	static const char huge[] =
		"<svg xmlns=\"http://www.w3.org/2000/svg\" "
		"viewBox=\"0 0 1e9 1e9\" width=\"1e9\" height=\"1e9\"/>";

#ifdef USE_SVG
	/* identFile() must recognise both a bare <svg root and one
	 * behind an XML prolog and BOM */
	path = write_sample("bare.svg", golden, sizeof(golden) - 1);
	CHECK(RGetImageFileFormat(path) != NULL &&
	      strcmp(RGetImageFileFormat(path), "SVG") == 0);
	path = write_sample("bom.svg", "\xef\xbb\xbf<?xml version=\"1.0\"?>\n<svg/>", 30);
	CHECK(RGetImageFileFormat(path) != NULL &&
	      strcmp(RGetImageFileFormat(path), "SVG") == 0);

	/* 2x2 flat colour at intrinsic size */
	path = write_sample("g.svg", golden, sizeof(golden) - 1);
	img = load_noctx(path);
	CHECK(img != NULL);
	if (img) {
		CHECK(img->width == 2 && img->height == 2);
		CHECK(img->format == RRGBAFormat);
		CHECK(img->data[0] == 0x40 && img->data[1] == 0x80
		      && img->data[2] == 0xc0 && img->data[3] == 0xff);
		RReleaseImage(img);
	}

	/* rendered at a caller-supplied size */
	img = load_noctx_sized(path, 8, 8);
	CHECK(img != NULL);
	if (img) {
		CHECK(img->width == 8 && img->height == 8);
		CHECK(img->data[0] == 0x40 && img->data[3] == 0xff);
		RReleaseImage(img);
	}

	/* absurd viewBox is clamped, not honoured, and the loader must
	 * not attempt a 1e9x1e9 surface allocation */
	path = write_sample("huge.svg", huge, sizeof(huge) - 1);
	img = load_noctx(path);
	CHECK(img == NULL || (img->width <= 256 && img->height <= 256));
	if (img)
		RReleaseImage(img);
#else
	(void)golden;
	(void)huge;
	(void)load_noctx_sized;
	(void)img;
	(void)path;
#endif
}

int main(void)
{
	srcdir = getenv("srcdir");
	if (!srcdir)
		srcdir = ".";

	if (!mkdtemp(tmpdir)) {
		perror("mkdtemp");
		return 2;
	}

	test_pnm();
	test_pnm_golden();
	test_pnm_reject();
	test_stb_formats();
	test_xpm_transparent();
	test_svg();

	/* best-effort cleanup */
	(void)wspawnvp_wait((char *[]){"rm", "-rf", tmpdir, NULL});

	return check_report("check_load");
}
