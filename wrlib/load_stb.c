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

/* GIF with frame selection. stbi_load_gif_from_memory composites every
 * frame with correct disposal and returns one w*h*4*nframes buffer, so
 * frame N is just an offset into it. The simple stbi_load() path is
 * used for index <= 0 to avoid holding the full animation in memory for
 * the common single-frame case. */
RImage *RLoadStbGIF(const char *file, int index)
{
	unsigned char *raw, *frames, *one;
	size_t flen;
	int w, h, z, n;
	int *delays = NULL;

	if (index <= 0)
		return RLoadStb(file);

	raw = RReadWholeFile(file, &flen);
	if (!raw)
		return NULL;

	frames = stbi_load_gif_from_memory(raw, (int)flen, &delays,
					   &w, &h, &z, &n, 4);
	free(raw);
	free(delays);
	if (!frames) {
		RErrorCode = RERR_BADIMAGEFILE;
		return NULL;
	}
	if (index >= z)
		index = z - 1;

	one = malloc((size_t)w * h * 4);
	if (!one) {
		stbi_image_free(frames);
		RErrorCode = RERR_NOMEMORY;
		return NULL;
	}
	memcpy(one, frames + (size_t)index * w * h * 4, (size_t)w * h * 4);
	stbi_image_free(frames);

	return image_from_stbi(one, w, h, 4);
}
