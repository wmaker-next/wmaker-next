/*
 *  fuzz_wrlib.c
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

/* fuzz_wrlib - libFuzzer harness for the libwraster decoders
 *
 * Pure public-API driver: the first input byte selects a magic prefix
 * (or none) that is prepended to the payload before it is written to a
 * temp file, so each format is reachable regardless of what the payload
 * itself sniffs as, and RLoadImageAtSize() runs the shipped dispatch.
 * Bytes 1-2 carry a small format parameter (multi-frame GIF/TIFF frame
 * index, render-size hint). A successful decode is also pushed through
 * RSmoothScaleImage so the resize pipeline is exercised on attacker
 * shaped images. The zeroed fake context is a complete stand-in for a
 * real connection: the decoders only read attribs->flags (PNG gamma)
 * and treat xc == NULL as "no server" (XPM colour names).
 */

#include <config.h>

#include <wraster.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

enum {
	MAGIC_NONE = 0,		/* stb probes: BMP, TGA, GIF, PSD, PIC */
	MAGIC_PNG,
	MAGIC_GIF,
	MAGIC_BMP,
	MAGIC_JPEG,
	MAGIC_TIFF,
	MAGIC_WEBP,
	MAGIC_JXL,
	MAGIC_XPM,
	MAGIC_PPM,
	MAGIC_HEIF,		/* ISO-BMFF ftyp box, AVIF primary brand */
	MAGIC_XPMDATA		/* RGetImageFromXPMData on in-memory lines */
};

struct magic_prefix {
	const char *bytes;
	size_t len;
};

/* the prefixes carry embedded NULs, so the length must come from the
 * literal instead of strlen() */
#define MAGIC_PREFIX(s) { s, sizeof(s) - 1 }

static const struct magic_prefix magics[] = {
	[MAGIC_NONE] = { NULL, 0 },
	[MAGIC_PNG] = MAGIC_PREFIX("\x89PNG\r\n\x1a\n"),
	[MAGIC_GIF] = MAGIC_PREFIX("GIF89a"),
	[MAGIC_BMP] = MAGIC_PREFIX("BM"),
	[MAGIC_JPEG] = MAGIC_PREFIX("\xff\xd8\xff"),
	[MAGIC_TIFF] = MAGIC_PREFIX("II*\0"),
	[MAGIC_WEBP] = MAGIC_PREFIX("RIFF\0\0\0\0WEBPVP8 "),
	[MAGIC_JXL] = MAGIC_PREFIX("\xff\x0a"),
	[MAGIC_XPM] = MAGIC_PREFIX("/* XPM */\nstatic char *x[] = {\n"),
	[MAGIC_PPM] = MAGIC_PREFIX("P6\n8 8\n255\n"),
	[MAGIC_HEIF] = MAGIC_PREFIX("\0\0\0\030ftypavif"),
	[MAGIC_XPMDATA] = { NULL, 0 }
};

static char tmp_path[64];

/* zeroed fake context: the decoders read attribs->flags/depth for PNG
 * gamma and treat xc == NULL as "no server" for XPM colour names */
static RContextAttributes fake_attribs;
static RContext fake_context = { .attribs = &fake_attribs };

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	(void) argc;
	(void) argv;
	snprintf(tmp_path, sizeof(tmp_path), "/tmp/wrlib-fuzz-%d.bin", (int)getpid());
	return 0;
}

static RImage *load_headless(const char *path, int index, unsigned w, unsigned h)
{
	return RLoadImageAtSize(&fake_context, path, index, w, h);
}

static RImage *decode_xpmdata(const uint8_t *data, size_t size)
{
	char *copy;
	char **lines;
	size_t i, nlines = 1;
	RImage *img;

	copy = malloc(size + 1);
	if (!copy)
		return NULL;
	memcpy(copy, data, size);
	copy[size] = '\0';

	for (i = 0; i < size; i++)
		if (data[i] == '\n')
			nlines++;

	if (nlines > 4096)
		nlines = 4096;
	lines = malloc((nlines + 1) * sizeof(char *));
	if (!lines) {
		free(copy);
		return NULL;
	}

	{
		char *p = copy;
		size_t n = 0;

		while (n < nlines) {
			lines[n++] = p;
			p = strchr(p, '\n');
			if (!p)
				break;
			*p = '\0';
			p++;
		}
		lines[n] = NULL;
	}

	img = RGetImageFromXPMData(&fake_context, lines);
	free(lines);
	free(copy);
	return img;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	int sel, index;
	unsigned w, h;
	RImage *img;
	size_t payload;
	uint8_t *buf;

	if (size < 4)
		return 0;

	sel = data[0] % (MAGIC_XPMDATA + 1);
	index = (int)(data[1] & 0x03);
	w = 1u << (data[2] & 0x0f);	/* 1..32768 render-size hint */
	h = 1u << (data[3] & 0x0f);
	payload = size - 4;

	if (sel == MAGIC_XPMDATA) {
		img = decode_xpmdata(data + 4, payload);
	} else {
		const struct magic_prefix *magic = &magics[sel];
		size_t mlen = magic->len;
		FILE *f;

		buf = malloc(mlen + payload);
		if (!buf)
			return 0;
		if (mlen)
			memcpy(buf, magic->bytes, mlen);
		memcpy(buf + mlen, data + 4, payload);

		f = fopen(tmp_path, "wb");
		if (!f) {
			free(buf);
			return 0;
		}
		if (mlen + payload && fwrite(buf, 1, mlen + payload, f) != mlen + payload) {
			fclose(f);
			free(buf);
			return 0;
		}
		fclose(f);
		free(buf);

		RReleaseCache();
		img = load_headless(tmp_path, index, w, h);
	}

	if (img) {
		/* every decoded image is a scaling input somewhere in the
		 * callers; run the resize path on the result too */
		RImage *scaled = RSmoothScaleImage(img, 17, 9);

		if (scaled)
			RReleaseImage(scaled);
		RReleaseImage(img);
	}
	return 0;
}
