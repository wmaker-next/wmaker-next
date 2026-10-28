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
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <https://www.gnu.org/licenses/>.
 *
 *  Run by `make check`. Writes tiny PNM samples to a temp directory,
 *  decodes each through RLoadImage(NULL, ...) (the PNM and XPM
 *  loaders run headless, no Display needed), and verifies dimensions
 *  and a known pixel. Also exercises RGetImageFromXPMData() with a
 *  transparent colour to confirm the RGB bytes are zero, not heap
 *  garbage.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <config.h>
#include <wraster.h>
#include <WINGs/WUtil.h>

#include "check_common.h"
#include "imgformat.h"

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

#if defined(__GNUC__) || defined(__clang__)
__attribute__((no_sanitize("nonnull-attribute")))
#endif
static RImage *load_noctx_index(const char *path, int index)
{
	return RLoadImageAtSize(NULL, path, index, 0, 0);
}
#if defined(__GNUC__) || defined(__clang__)
__attribute__((no_sanitize("nonnull-attribute")))
#endif
static RImage *xpmdata_noctx(char **data)
{
	return RGetImageFromXPMData(NULL, data);
}
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

static void append_bytes(const char *path, const void *data, size_t len)
{
	FILE *f = fopen(path, "ab");

	if (!f) {
		perror(path);
		exit(2);
	}
	fwrite(data, 1, len, f);
	fclose(f);
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
	append_bytes(p, (const unsigned char[]){0x80}, 1);
	expect_image("P4", load_noctx(p), 2, 1, 0, 0, 0);

	/* P5 binary graymap, 2x1, max 255, bytes 64 200 */
	s = "P5\n2 1\n255\n";
	p = write_sample("b.pgm", s, strlen(s));
	append_bytes(p, (const unsigned char[]){0x40, 0xc8}, 2);
	expect_image("P5", load_noctx(p), 2, 1, 64, 64, 64);

	/* P6 binary pixmap, 1x1, max 255, bytes 10 20 30 */
	s = "P6\n1 1\n255\n";
	p = write_sample("b.ppm", s, strlen(s));
	append_bytes(p, (const unsigned char[]){10, 20, 30}, 3);
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

	/* a tiny GIF can claim a 16385x16385 canvas; stb allocates two
	 * full RGBA canvases before returning, so both the single-frame
	 * and indexed paths must reject it before allocating */
	static const unsigned char gif_huge[] = {
		'G', 'I', 'F', '8', '9', 'a',
		0x01, 0x40, 0x01, 0x40,            /* logical screen 16385x16385 */
		0x80, 0, 0,                        /* GCT present, 2 entries */
		0, 0, 0, 255, 255, 255,            /* palette */
		0x3b
	};
	p = write_sample("huge.gif", gif_huge, sizeof(gif_huge));
	CHECK(load_noctx(p) == NULL);
	CHECK(load_noctx_index(p, 1) == NULL);

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
}

static void test_xpm_extended(void)
{
	static const char with_comments[] =
		"/* XPM */\n"
		"/* leading comment */\n"
		"static char *x[] = {\n"
		"/* spanning\n"
		"   two lines */\n"
		"\n"
		"\"2 1 2 1\",\n"
		"/* colour table */\n"
		"\"r c #ff0000\",\n"
		"\"g c #00ff00\",\n"
		"\n"
		"\"rg\"\n"
		"};\n";
	static const char named_colors[] =
		"/* XPM */\n"
		"static char *x[] = {\n"
		"\"2 1 3 1\",\n"
		"\"w c white\",\n"
		"\"g c gray50\",\n"
		"\"b c black\",\n"
		"\"wgb\"\n"
		"};\n";
	static const char cpp_four[] =
		"/* XPM */\n"
		"static char *x[] = {\n"
		"\"2 1 2 4\",\n"
		"\"abcd c #112233\",\n"
		"\"efgh c #445566\",\n"
		"\"abcdefgh\"\n"
		"};\n";
	static const char with_hotspot[] =
		"/* XPM */\n"
		"static char *x[] = {\n"
		"\"2 1 1 1 1 0\",\n"
		"\"x c white\",\n"
		"\"xx\"\n"
		"};\n";
	static const char short_row[] =
		"/* XPM */\n"
		"static char *x[] = {\n"
		"\"4 1 1 1\",\n"
		"\"x c #ff0000\",\n"
		"\"xx\"\n"
		"};\n";
	static const char bad_hex[] =
		"/* XPM */\n"
		"static char *x[] = {\n"
		"\"1 1 1 1\",\n"
		"\"x c #ff00zz\",\n"
		"\"x\"\n"
		"};\n";
	static const char unknown_name[] =
		"/* XPM */\n"
		"static char *x[] = {\n"
		"\"1 1 1 1\",\n"
		"\"x c notacolorname\",\n"
		"\"x\"\n"
		"};\n";
	static const char cpp_five[] =
		"/* XPM */\n"
		"static char *x[] = {\n"
		"\"1 1 1 5\",\n"
		"\"abcde c #112233\",\n"
		"\"abcde\"\n"
		"};\n";
	static const char gray_pct[] =
		"/* XPM */\n"
		"static char *x[] = {\n"
		"\"1 1 1 1\",\n"
		"\"x c %50\",\n"
		"\"x\"\n"
		"};\n";
	static const char key_pref[] =
		"/* XPM */\n"
		"static char *x[] = {\n"
		"\"2 1 2 1\",\n"
		"\"a g #808080 g4 #404040 m #202020\",\n"
		"\"b m #404040\",\n"
		"\"ab\"\n"
		"};\n";
	static const char inline_cmt[] =
		"/* XPM */\n"
		"static char *x[] = {\n"
		"/* two pixels */ \"2 1 2 1\",\n"
		"/* red */ \"r c #ff0000\",\n"
		"\"g c #00ff00\",\n"
		"\"rg\"\n"
		"};\n";
	static const char cmt_close_payload[] =
		"/* XPM */\n"
		"static char *x[] = {\n"
		"\"2 1 2 1\",\n"
		"/* red\n"
		"   ends */ \"r c #ff0000\",\n"
		"\"g c #00ff00\",\n"
		"\"rg\"\n"
		"};\n";
	static const char *const huge_dims[] = {
		"1 2000000000 2000000000 1",
		"x c #ff0000",
		"x"
	};
	char buf[512];
	RImage *img;

	/* comments and blank lines, multi-line comments included */
	img = load_noctx(write_sample("cmt.xpm", with_comments, sizeof(with_comments) - 1));
	CHECK(img != NULL);
	if (img) {
		CHECK(img->width == 2 && img->height == 1);
		CHECK(img->data[0] == 255 && img->data[1] == 0 && img->data[2] == 0);
		CHECK(img->data[3] == 0 && img->data[4] == 255 && img->data[5] == 0);
		RReleaseImage(img);
	}

	/* named colours resolve headless through the fallback table */
	img = load_noctx(write_sample("named.xpm", named_colors, sizeof(named_colors) - 1));
	CHECK(img != NULL);
	if (img) {
		CHECK(img->width == 2 && img->height == 1);
		CHECK(img->data[0] == 255 && img->data[1] == 255 && img->data[2] == 255);
		CHECK(img->data[3] == 0x7f && img->data[4] == 0x7f && img->data[5] == 0x7f);
		RReleaseImage(img);
	}

	/* four characters per pixel */
	img = load_noctx(write_sample("cpp4.xpm", cpp_four, sizeof(cpp_four) - 1));
	CHECK(img != NULL);
	if (img) {
		CHECK(img->width == 2 && img->height == 1);
		CHECK(img->data[0] == 0x11 && img->data[1] == 0x22 && img->data[2] == 0x33);
		CHECK(img->data[3] == 0x44 && img->data[4] == 0x55 && img->data[5] == 0x66);
		RReleaseImage(img);
	}

	/* values-line hotspot fields are ignored */
	img = load_noctx(write_sample("hot.xpm", with_hotspot, sizeof(with_hotspot) - 1));
	CHECK(img != NULL);
	if (img)
		RReleaseImage(img);

	/* a colour line far beyond the historical 64-char buffer */
	snprintf(buf, sizeof(buf),
		 "/* XPM */\n"
		 "static char *x[] = {\n"
		 "\"1 1 2 1\",\n"
		 "\"a c #123456\",\n"
		 "\"  c #abcdef s %0200d\",\n"
		 "\" a\"\n"
		 "};\n", 1);
	img = load_noctx(write_sample("long.xpm", buf, strlen(buf)));
	CHECK(img != NULL);
	if (img) {
		CHECK(img->width == 1 && img->height == 1);
		CHECK(img->data[0] == 0xab && img->data[1] == 0xcd && img->data[2] == 0xef);
		RReleaseImage(img);
	}

	/* %nn grayscale (NEWS-advertised): %50 == 127 */
	expect_golden("xpm gray %50",
		      load_noctx(write_sample("gray.xpm", gray_pct,
					      sizeof(gray_pct) - 1)),
		      1, 1, 127, 127, 127, 127, 127, 127);

	/* key preference: c > g/g4 > m; the g value beats g4/m, and an
	 * m-only entry still resolves */
	expect_golden("xpm key preference",
		      load_noctx(write_sample("keys.xpm", key_pref,
					      sizeof(key_pref) - 1)),
		      2, 1, 0x80, 0x80, 0x80, 0x40, 0x40, 0x40);

	/* inline comments: payload after the comment close on the values
	 * and colour lines */
	img = load_noctx(write_sample("icmt.xpm", inline_cmt, sizeof(inline_cmt) - 1));
	CHECK(img != NULL);
	if (img) {
		CHECK(img->width == 2 && img->height == 1);
		CHECK(img->data[0] == 255 && img->data[1] == 0 && img->data[2] == 0);
		CHECK(img->data[3] == 0 && img->data[4] == 255 && img->data[5] == 0);
		RReleaseImage(img);
	}

	/* multi-line comment whose closing line carries the payload */
	img = load_noctx(write_sample("mccmt.xpm", cmt_close_payload,
				      sizeof(cmt_close_payload) - 1));
	CHECK(img != NULL);
	if (img) {
		CHECK(img->width == 2 && img->height == 1);
		CHECK(img->data[0] == 255 && img->data[1] == 0 && img->data[2] == 0);
		CHECK(img->data[3] == 0 && img->data[4] == 255 && img->data[5] == 0);
		RReleaseImage(img);
	}

	/* rejects: short pixel row, bad hex, unknown colour name, cpp>4 */
	img = load_noctx(write_sample("short.xpm", short_row, sizeof(short_row) - 1));
	CHECK(img == NULL);
	img = load_noctx(write_sample("badhex.xpm", bad_hex, sizeof(bad_hex) - 1));
	CHECK(img == NULL);
	img = load_noctx(write_sample("badname.xpm", unknown_name, sizeof(unknown_name) - 1));
	CHECK(img == NULL);
	img = load_noctx(write_sample("cpp5.xpm", cpp_five, sizeof(cpp_five) - 1));
	CHECK(img == NULL);

	/* in-memory dimension caps reject near-INT_MAX values before
	 * 1 + ccount + h can overflow */
	CHECK(xpmdata_noctx((char **)huge_dims) == NULL);
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

/* HEIC/AVIF fixtures. Two 2x1 solid gray-128 images: gray is neutral in
 * every YCbCr matrix and range convention, so a correct decode lands
 * within a few units of 128 no matter which codec plugin decodes it.
 * The fixture .heic carries primary brand "heix" and the .avif "avif",
 * both must sniff as HEIF, while an MP4 "isom" ftyp must not. A missing
 * HEVC/AV1 decoder plugin makes the decode a skip (printed, not failed)
 * since the loader is only as able to decode as the libheif it links
 * against. */
static void heif_expect(const char *what, RImage *img)
{
	int bpp, last;

	if (!img) {
		fprintf(stderr, "SKIP %s: decode failed (%s); decoder codec missing?\n",
			what, RMessageForError(RErrorCode));
		return;
	}
	CHECK(img->width == 2);
	CHECK(img->height == 1);

	bpp = (img->format == RRGBAFormat) ? 4 : 3;
	last = bpp;

	CHECK(abs(img->data[0] - 128) <= 4);
	CHECK(abs(img->data[1] - 128) <= 4);
	CHECK(abs(img->data[2] - 128) <= 4);
	CHECK(abs(img->data[last + 0] - 128) <= 4);
	CHECK(abs(img->data[last + 1] - 128) <= 4);
	CHECK(abs(img->data[last + 2] - 128) <= 4);
	RReleaseImage(img);
}

static void test_heif(void)
{
	const char *p;
	/* a 24-byte ftyp box with MP4's primary brand "isom": ISO-BMFF
	 * itself, but never a HEIF image (split so "ftyp" does not
	 * extend the \x18 escape) */
	static const char mp4[] = "\0\0\0\x18" "ftypisom\0\0\0\0isomiso2mp41";

	p = write_sample("mp4.bin", mp4, sizeof(mp4) - 1);
	CHECK(RGetImageFileFormat(p) == NULL);
	CHECK(load_noctx(p) == NULL);

#ifdef USE_HEIF
	RImage *img;

	/* the committed fixtures sniff as HEIF regardless of what codecs
	 * the linked libheif can actually decode */
	p = fixture_path("heif-test.avif");
	CHECK(RGetImageFileFormat(p) != NULL &&
	      strcmp(RGetImageFileFormat(p), "HEIF") == 0);
	p = fixture_path("heif-test.heic");
	CHECK(RGetImageFileFormat(p) != NULL &&
	      strcmp(RGetImageFileFormat(p), "HEIF") == 0);

	/* decoded pixels: solid gray in both fixtures */
	heif_expect("AVIF", load_noctx(fixture_path("heif-test.avif")));
	heif_expect("HEIC", load_noctx(fixture_path("heif-test.heic")));

	/* single-image files have no frame 1, only the primary image */
	img = load_noctx_index(fixture_path("heif-test.heic"), 1);
	CHECK(img == NULL);
	if (img)
		RReleaseImage(img);
#else
	(void)heif_expect;
	(void)load_noctx_index;
#endif
}

/* 8x8 solid P6, write_sample() returns a shared static buffer */
static void write_ppm(char *path, size_t pathlen, const char *name,
		      unsigned char r, unsigned char g, unsigned char b)
{
	unsigned char blob[16 + 8 * 8 * 3];
	size_t n, i;

	n = (size_t)snprintf((char *)blob, sizeof(blob), "P6\n8 8\n255\n");
	for (i = 0; i < 8 * 8; i++) {
		blob[n++] = r;
		blob[n++] = g;
		blob[n++] = b;
	}
	snprintf(path, pathlen, "%s", write_sample(name, blob, n));
}

/* Decode cache tests. RReleaseCache() forces init_cache() to re-read
 * the pinned environment (it snapshots lazily) */
static void test_cache(void)
{
	char pa[256], pb[256], pc[256], pd[256];
	char oe_buf[32], op_buf[32];
	struct stat st;
	struct timespec touch[2];
	RImage *img, *img2;
	const char *oe, *op;

	oe = getenv("RIMAGE_CACHE");
	op = getenv("RIMAGE_CACHE_SIZE");
	snprintf(oe_buf, sizeof(oe_buf), "%s", oe ? oe : "");
	snprintf(op_buf, sizeof(op_buf), "%s", op ? op : "");
	setenv("RIMAGE_CACHE", "3", 1);
	setenv("RIMAGE_CACHE_SIZE", "4096", 1);
	RReleaseCache();

	/* a hit returns an equal-content copy whose mutation does not
	 * leak into the next fetch */
	write_ppm(pa, sizeof(pa), "cache-a.ppm", 255, 0, 0);
	img = load_noctx(pa);
	CHECK(img != NULL && img->width == 8 && img->height == 8);
	CHECK(img && img->data[0] == 255 && img->data[1] == 0 && img->data[2] == 0);
	img2 = load_noctx(pa);
	CHECK(img2 != NULL);
	CHECK(img2 && img2->data[0] == 255 && img2->data[1] == 0 && img2->data[2] == 0);
	RReleaseImage(img2);
	if (img) {
		img->data[0] = 0;
		img->data[2] = 255;
		RReleaseImage(img);
	}
	img = load_noctx(pa);
	CHECK(img != NULL);
	CHECK(img && img->data[0] == 255 && img->data[1] == 0 && img->data[2] == 0);
	RReleaseImage(img);

	/* same-second rewrite must invalidate: the file keeps tv_sec and
	 * only tv_nsec moves, which a seconds-granularity compare would
	 * happily return stale */
	write_ppm(pb, sizeof(pb), "cache-b.ppm", 0, 255, 0);
	img = load_noctx(pb);
	CHECK(img != NULL);
	CHECK(img && img->data[0] == 0 && img->data[1] == 255 && img->data[2] == 0);
	RReleaseImage(img);
	CHECK(stat(pb, &st) == 0);
	write_ppm(pb, sizeof(pb), "cache-b.ppm", 0, 0, 255);
	touch[0].tv_sec = st.st_mtim.tv_sec;
	touch[0].tv_nsec = st.st_mtim.tv_nsec ? st.st_mtim.tv_nsec - 1 : 1;
	touch[1] = touch[0];
	CHECK(utimensat(AT_FDCWD, pb, touch, 0) == 0);
	img = load_noctx(pb);
	CHECK(img != NULL);
	CHECK(img && img->data[0] == 0 && img->data[1] == 0 && img->data[2] == 255);
	RReleaseImage(img);

	/* cycle the LRU past the three-entry bound, then re-verify each
	 * still decodes to its own content (sanitizers watch the frees) */
	write_ppm(pc, sizeof(pc), "cache-c.ppm", 128, 0, 128);
	write_ppm(pd, sizeof(pd), "cache-d.ppm", 0, 128, 128);
	img = load_noctx(pc);
	CHECK(img != NULL && img->data[0] == 128);
	RReleaseImage(img);
	img = load_noctx(pd);
	CHECK(img != NULL && img->data[0] == 0 && img->data[2] == 128);
	RReleaseImage(img);
	img = load_noctx(pc);
	CHECK(img != NULL && img->data[0] == 128);
	RReleaseImage(img);
	img = load_noctx(pa);
	CHECK(img != NULL && img->data[0] == 255 && img->data[2] == 0);
	RReleaseImage(img);

	/* RReleaseCache() drops everything and a reload decodes clean */
	RReleaseCache();
	img = load_noctx(pa);
	CHECK(img != NULL);
	CHECK(img && img->data[0] == 255 && img->data[1] == 0 && img->data[2] == 0);
	RReleaseImage(img);

	/* a vanished file evicts its own entries and keeps failing */
	img = load_noctx(pc);
	CHECK(img != NULL && img->data[0] == 128);
	RReleaseImage(img);
	unlink(pc);
	img = load_noctx(pc);
	CHECK(img == NULL);

	/* restore the caller's cache environment */
	if (oe)
		setenv("RIMAGE_CACHE", oe_buf, 1);
	else
		unsetenv("RIMAGE_CACHE");
	if (op)
		setenv("RIMAGE_CACHE_SIZE", op_buf, 1);
	else
		unsetenv("RIMAGE_CACHE_SIZE");
	RReleaseCache();
}

static void test_working_set_limits(void)
{
	/* 5000x5000 RGBA with a full-size intermediate stays under 2G */
	CHECK(RImageWorkingSetWithinLimits(5000, 5000, 8));
	/* 20000x20000 needs more than one full RGBA buffer */
	CHECK(!RImageWorkingSetWithinLimits(20000, 20000, 8));
	CHECK(!RImageWorkingSetWithinLimits(20000, 20000, 6));
	/* one full RGBA buffer still fits the generic cap */
	CHECK(RImageWorkingSetWithinLimits(20000, 20000, 4));
	CHECK(!RImageWorkingSetWithinLimits(0, 1, 4));
	CHECK(!RImageWorkingSetWithinLimits(1, 1, 0));
	CHECK(!RImageWorkingSetWithinLimits(20001, 1, 4));
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
	test_working_set_limits();
	test_stb_formats();
	test_xpm_transparent();
	test_xpm_extended();
	test_svg();
	test_heif();
	test_cache();

	/* best-effort cleanup */
	(void)wspawnvp_wait((char *[]){"rm", "-rf", tmpdir, NULL});

	return check_report("check_load");
}
