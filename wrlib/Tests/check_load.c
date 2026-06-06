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

#include "../config.h"
#include <wraster.h>

static int failures;

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
#pragma GCC diagnostic pop

#define CHECK(cond) do {                                                   \
	if (!(cond)) {                                                     \
	    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);\
	    failures++;                                                    \
	}                                                                  \
    } while (0)

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
		failures++;
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

static void expect_golden(const char *what, RImage *img, int w, int h,
			  int fr, int fg, int fb, int lr, int lg, int lb)
{
	int bpp, last;

	if (!img) {
		fprintf(stderr, "FAIL %s: RLoadImage returned NULL (%s)\n",
			what, RMessageForError(RErrorCode));
		failures++;
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

	/* maxval > 255 must be rejected */
	s = "P6\n1 1\n65535\nxxxxxx";
	p = write_sample("wide.ppm", s, strlen(s));
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

	img = RGetImageFromXPMData(NULL, xpm);
	if (!img) {
		fprintf(stderr, "FAIL xpm: RGetImageFromXPMData NULL (%s)\n",
			RMessageForError(RErrorCode));
		failures++;
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

int main(void)
{
	char cmd[300];

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
	test_xpm_transparent();

	/* best-effort cleanup */
	snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir);
	if (system(cmd)) { /* ignore */ }

	if (failures) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	printf("ok - check_load\n");
	return 0;
}
