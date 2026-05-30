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
	{
		FILE *f = fopen(p, "ab");
		fputc(0x80, f);
		fclose(f);
	}
	expect_image("P4", load_noctx(p), 2, 1, 0, 0, 0);

	/* P5 binary graymap, 2x1, max 255, bytes 64 200 */
	s = "P5\n2 1\n255\n";
	p = write_sample("b.pgm", s, strlen(s));
	{
		FILE *f = fopen(p, "ab");
		fputc(0x40, f); fputc(0xc8, f);
		fclose(f);
	}
	expect_image("P5", load_noctx(p), 2, 1, 64, 64, 64);

	/* P6 binary pixmap, 1x1, max 255, bytes 10 20 30 */
	s = "P6\n1 1\n255\n";
	p = write_sample("b.ppm", s, strlen(s));
	{
		FILE *f = fopen(p, "ab");
		fputc(10, f); fputc(20, f); fputc(30, f);
		fclose(f);
	}
	expect_image("P6", load_noctx(p), 1, 1, 10, 20, 30);
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
	if (!mkdtemp(tmpdir)) {
		perror("mkdtemp");
		return 2;
	}

	test_pnm();
	test_pnm_reject();
	test_xpm_transparent();

	/* best-effort cleanup */
	{
		char cmd[300];
		snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir);
		if (system(cmd)) { /* ignore */ }
	}

	if (failures) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	printf("ok - check_load\n");
	return 0;
}
