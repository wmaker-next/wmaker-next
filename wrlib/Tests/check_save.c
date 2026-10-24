/*
 *  check_save - automated encoder tests for libwraster
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
 *  Run by `make check`. Saves one image through RSaveTitledImage() as PNG
 *  and JPEG when the build enables those encoders, checks the container
 *  magic and the format probe, then decodes each file back through
 *  RLoadImage() and compares the dimensions and colour. This covers the
 *  screenshot writer path without needing an X server.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <config.h>
#include <wraster.h>
#include <WINGs/WUtil.h>

#include "check_common.h"
#include "imgformat.h"

/* The raster loaders only consult the context's attributes and depth
 * (PNG gamma), so a stack context keeps this test X-free. */
static char tmpdir[] = "/tmp/wrlib-save-XXXXXX";

#if defined(USE_PNG) || defined(USE_JPEG)
static unsigned char file_byte(const char *path, int offset)
{
	FILE *f = fopen(path, "rb");
	unsigned char b = 0;

	if (f) {
		size_t n = 0;

		if (fseek(f, offset, SEEK_SET) == 0)
			n = fread(&b, 1, 1, f);
		if (n != 1)
			b = 0;
		fclose(f);
	}
	return b;
}

static bool has_magic(const char *path, const unsigned char *magic,
		      size_t len)
{
	size_t i;

	for (i = 0; i < len; i++)
		if (file_byte(path, (int)i) != magic[i])
			return false;
	return true;
}

static void expect_roundtrip(RContext *context, const char *what,
			     RImage *original, const char *path,
			     const char *expected_format)
{
	static const unsigned char png_magic[] = { 0x89, 'P', 'N', 'G',
						   0x0d, 0x0a, 0x1a, 0x0a };
	static const unsigned char jpeg_magic[] = { 0xff, 0xd8, 0xff };
	RImage *decoded;
	int bpp;

	if (strcmp(expected_format, "PNG") == 0)
		CHECK(has_magic(path, png_magic, sizeof(png_magic)));
	else
		CHECK(has_magic(path, jpeg_magic, sizeof(jpeg_magic)));

	CHECK_STR(RGetImageFileFormat(path), expected_format);

	decoded = RLoadImage(context, path, 0);
	if (!decoded) {
		fprintf(stderr, "FAIL %s: RLoadImage returned NULL (%s)\n",
			what, RMessageForError(RErrorCode));
		check_failures++;
		return;
	}

	CHECK(decoded->width == (int)original->width);
	CHECK(decoded->height == (int)original->height);

	bpp = (decoded->format == RRGBAFormat) ? 4 : 3;
	/* JPEG is lossy, a flat colour must still land close to the source */
	CHECK(abs((int)decoded->data[0] - (int)original->data[0]) <= 12);
	CHECK(abs((int)decoded->data[1] - (int)original->data[1]) <= 12);
	CHECK(abs((int)decoded->data[2] - (int)original->data[2]) <= 12);
	CHECK(bpp == 3 || decoded->data[3] == 255);

	RReleaseImage(decoded);
}
#endif

int main(void)
{
	RImage *img;
	RContext context;
	RContextAttributes attrs;
	char png_path[256];
	char jpg_path[256];
	int encoded = 0;
	int i;

	if (!mkdtemp(tmpdir)) {
		perror("mkdtemp");
		return 2;
	}

	memset(&context, 0, sizeof(context));
	memset(&attrs, 0, sizeof(attrs));
	context.attribs = &attrs;
	context.depth = 24;

	img = RCreateImage(64, 48, 0);
	if (!img) {
		fprintf(stderr, "RCreateImage failed: %s\n",
			RMessageForError(RErrorCode));
		rmdir(tmpdir);
		return 2;
	}
	for (i = 0; i < img->width * img->height; i++) {
		img->data[3 * i + 0] = 37;
		img->data[3 * i + 1] = 120;
		img->data[3 * i + 2] = 205;
	}

	snprintf(png_path, sizeof(png_path), "%s/save.png", tmpdir);
	snprintf(jpg_path, sizeof(jpg_path), "%s/save.jpg", tmpdir);

#ifdef USE_PNG
	CHECK(RSaveTitledImage(img, png_path, "png", "check_save") != 0);
	expect_roundtrip(&context, "PNG", img, png_path, "PNG");
	encoded++;
#endif

#ifdef USE_JPEG
	CHECK(RSaveTitledImage(img, jpg_path, "jpg", "check_save") != 0);
	expect_roundtrip(&context, "JPEG", img, jpg_path, "JPEG");
	encoded++;
#endif

	if (!encoded) {
		fprintf(stderr, "SKIP: neither PNG nor JPEG encoder is enabled\n");
		RReleaseImage(img);
		rmdir(tmpdir);
		return 77;
	}

	unlink(png_path);
	unlink(jpg_path);
	RReleaseImage(img);
	rmdir(tmpdir);

	return check_report("wrlib save");
}
