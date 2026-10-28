/* load_stb.c - vendored stb_image / stb_image_resize2 backends
 *
 * Raster graphics library
 *
 * Copyright (c) 2026 Window Maker Team
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
 * Both stb_image.h and stb_image_resize2.h are public domain (Sean
 * Barrett et al., see file headers). They are vendored unmodified and
 * compiled in this single translation unit so the project's
 * extra-warning set can be silenced for third-party code without
 * touching the rest of wrlib.
 *
 * stb_image is compiled with PNG and JPEG disabled because libpng and
 * libjpeg-turbo stay as the reference decoders (the latter measured
 * faster). PNM, BMP, TGA, GIF, PSD and PIC come from here. The resize
 * implementation backs RSmoothScaleImage in scale.c.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "wraster.h"
#include "imgformat.h"
#include "wr_i18n.h"

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wfloat-equal"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wredundant-decls"
#pragma GCC diagnostic ignored "-Wunused-macros"
#pragma GCC diagnostic ignored "-Wundef"
#endif

/* match RImageDimensionsWithinLimits() so stb rejects the same oversize
 * inputs as the native loaders, before allocating */
#define STBI_MAX_DIMENSIONS  20000

/* stb's GIF decoder keeps the RGBA output, the previous RGBA frame and
 * the history plane alive at once (9 bytes/pixel); leave headroom for
 * the returned RImage and allocator overhead. */
#define STBI_GIF_BYTES_PER_PIXEL 16U

/* Indexed decoding keeps stb's three planes, copies of the previous two
 * frames and the returned image, so peak use is 21 bytes/pixel instead
 * of growing with the frame count. */
#define STBI_GIF_STREAM_BYTES_PER_PIXEL 21U

/* stb allocates its decoded buffer before image_from_stbi() sees it.
 * The 3/4-component buffer is adopted, while 1/2-component buffers are
 * copied into a fresh RImage. */
static uint64_t stbiWorkingBytes(int components)
{
	switch (components) {
	case 1:
		return 4;	/* grey source plus RGB RImage */
	case 2:
		return 6;	/* grey+alpha source plus RGBA RImage */
	case 3:
		return 3;	/* adopted RGB buffer */
	case 4:
		return 4;	/* adopted RGBA buffer */
	default:
		return 8;	/* unknown, be conservative */
	}
}

#define STBI_ONLY_PNM
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_GIF
#define STBI_ONLY_PSD
#define STBI_ONLY_PIC
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

/* Wrap an stbi_load result as an RImage. Greyscale is widened to RGB so
 * callers see only the two formats RImage knows. The pixel buffer is
 * adopted directly when stb already returned 3 or 4 channels, otherwise
 * the data is copied into a fresh RImage. Takes ownership of data in
 * every case (freed on failure, adopted on success), so callers never
 * release it themselves. stbi_image_free() is plain free(), matching
 * the free(image->data) in RReleaseImage. */
static RImage *image_from_stbi(unsigned char *data, int w, int h, int n)
{
	RImage *img;
	unsigned char *s, *d;
	int i;

	if (!data) {
		RErrorCode = RERR_BADIMAGEFILE;
		return NULL;
	}
	if (!RImageDimensionsWithinLimits((unsigned)w, (unsigned)h)) {
		stbi_image_free(data);
		RErrorCode = RERR_BADIMAGEFILE;
		return NULL;
	}
	if (n == 3 || n == 4) {
		img = calloc(1, sizeof(*img));
		if (!img) {
			stbi_image_free(data);
			RErrorCode = RERR_NOMEMORY;
			return NULL;
		}
		img->width = w;
		img->height = h;
		img->format = (n == 4) ? RRGBAFormat : RRGBFormat;
		img->refCount = 1;
		img->data = data;
		return img;
	}

	/* n == 1 or 2: greyscale, with or without alpha */
	img = RCreateImage(w, h, n == 2);
	if (!img) {
		stbi_image_free(data);
		return NULL;
	}
	s = data;
	d = img->data;
	if (n == 1) {
		for (i = 0; i < w * h; i++, s++, d += 3)
			d[0] = d[1] = d[2] = s[0];
	} else {
		for (i = 0; i < w * h; i++, s += 2, d += 4) {
			d[0] = d[1] = d[2] = s[0];
			d[3] = s[1];
		}
	}
	stbi_image_free(data);
	return img;
}

RImage *RLoadStb(const char *file)
{
	int w, h, n;
	unsigned char *data;

	if (stbi_info(file, &w, &h, &n) &&
	    !RImageWorkingSetWithinLimits((uint64_t)w, (uint64_t)h,
					  stbiWorkingBytes(n) *
					  (stbi_is_16_bit(file) ? 2U : 1U))) {
		RErrorCode = RERR_BADIMAGEFILE;
		return NULL;
	}

	data = stbi_load(file, &w, &h, &n, 0);
	if (!data) {
		const char *reason = stbi_failure_reason();
		if (reason && strstr(reason, "open"))
			RErrorCode = RERR_OPEN;
		else
			RErrorCode = RERR_BADIMAGEFILE;
		return NULL;
	}
	return image_from_stbi(data, w, h, n);
}

/* GIF with frame selection. Indexed decoding walks the frames with stb's
 * compositing loop but keeps only the two previous frames needed by
 * disposal method 3, so memory does not grow with the frame count. The
 * simple stbi_load() path is used for index <= 0. */
RImage *RLoadStbGIF(const char *file, int index)
{
	unsigned char *raw, *one = NULL;
	unsigned char *prev[2] = { NULL, NULL };
	size_t flen, stride = 0;
	uint64_t pixels;
	int w = 0, h = 0;
	int iw, ih, in;
	int frame = 0, newest = 0, comp = 0;
	int rc;
	stbi__context s;
	stbi__gif g;
	stbi_uc *u;

	if (index <= 0) {
		if (stbi_info(file, &iw, &ih, &in) &&
		    !RImageWorkingSetWithinLimits((uint64_t)iw, (uint64_t)ih,
						  STBI_GIF_BYTES_PER_PIXEL)) {
			RErrorCode = RERR_BADIMAGEFILE;
			return NULL;
		}
		return RLoadStb(file);
	}

	raw = RReadWholeFile(file, &flen);
	if (!raw)
		return NULL;

	if (!stbi_info_from_memory(raw, (int)flen, &iw, &ih, &in)) {
		free(raw);
		RErrorCode = RERR_BADIMAGEFILE;
		return NULL;
	}
	pixels = (uint64_t)iw * (uint64_t)ih;
	if (!RImageWorkingSetWithinLimits((uint64_t)iw, (uint64_t)ih,
					  STBI_GIF_STREAM_BYTES_PER_PIXEL) ||
	    (uint64_t)flen > R_IMAGE_MAX_BYTES -
			     pixels * STBI_GIF_STREAM_BYTES_PER_PIXEL) {
		free(raw);
		RErrorCode = RERR_BADIMAGEFILE;
		return NULL;
	}

	stbi__start_mem(&s, raw, (int)flen);
	if (!stbi__gif_test(&s)) {
		free(raw);
		RErrorCode = RERR_BADIMAGEFILE;
		return NULL;
	}

	/* keep only the two previous composited frames needed by disposal
	 * method 3 instead of accumulating every frame in the buffer */
	memset(&g, 0, sizeof(g));
	rc = RERR_BADIMAGEFILE;
	for (;;) {
		u = stbi__gif_load_next(&s, &g, &comp, 4,
					frame >= 2 ? prev[newest ^ 1] : NULL);
		if (u == (stbi_uc *)&s) {
			if (frame == 0)
				goto done;
			one = malloc(stride);
			if (!one) {
				rc = RERR_NOMEMORY;
				goto done;
			}
			memcpy(one, prev[newest], stride);
			break;
		}
		if (!u)
			goto done;

		if (frame == 0) {
			w = g.w;
			h = g.h;
			pixels = (uint64_t)w * (uint64_t)h;
			if (!RImageWorkingSetWithinLimits((uint64_t)w, (uint64_t)h,
							  STBI_GIF_STREAM_BYTES_PER_PIXEL) ||
			    (uint64_t)flen > R_IMAGE_MAX_BYTES -
					     pixels * STBI_GIF_STREAM_BYTES_PER_PIXEL) {
				rc = RERR_BADIMAGEFILE;
				goto done;
			}
			stride = (size_t)w * (size_t)h * 4;
			prev[0] = malloc(stride);
			prev[1] = malloc(stride);
			if (!prev[0] || !prev[1]) {
				rc = RERR_NOMEMORY;
				goto done;
			}
		}

		if (frame == index) {
			one = g.out;
			g.out = NULL;
			break;
		}

		newest ^= 1;
		memcpy(prev[newest], u, stride);
		frame++;
	}

done:
	free(g.out);
	free(g.background);
	free(g.history);
	free(prev[0]);
	free(prev[1]);
	free(raw);
	if (!one) {
		RErrorCode = rc;
		return NULL;
	}
	return image_from_stbi(one, w, h, 4);
}
