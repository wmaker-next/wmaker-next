/*
 * Raster graphics library
 *
 * Copyright (c) 1997-2003 Alfredo K. Kojima
 * Copyright (c) 2013-2026 Window Maker Team
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
 * Functions to load and save RImage from/to file in a specific file format
 *
 * These functions are for WRaster library's internal use only, please use
 * the RLoadImage function defined in 'wraster.h'
 */

#ifndef IMGFORMAT_INTERNAL_H
#define IMGFORMAT_INTERNAL_H

#include <stdint.h>

#define R_IMAGE_MAX_WIDTH 20000U
#define R_IMAGE_MAX_HEIGHT 20000U
/* total decoder working set, not just one output buffer */
#define R_IMAGE_MAX_BYTES UINT64_C(0x80000000)
/* 20000^2*4 < 2G */
#define R_IMAGE_MAX_PIXELS (R_IMAGE_MAX_BYTES / 4U)

static inline int RImageDimensionsWithinLimits(uint64_t width, uint64_t height)
{
	return width >= 1 && height >= 1 &&
	       width <= R_IMAGE_MAX_WIDTH &&
	       height <= R_IMAGE_MAX_HEIGHT &&
	       width <= R_IMAGE_MAX_PIXELS / height;
}

/* Decoders that keep a full intermediate image alive next to the output
 * pass the total bytes per pixel they need. Reject before allocating so a
 * tiny compressed header cannot force a multi-buffer allocation. */
static inline int RImageWorkingSetWithinLimits(uint64_t width, uint64_t height,
					       uint64_t bytes_per_pixel)
{
	return bytes_per_pixel > 0 &&
	       RImageDimensionsWithinLimits(width, height) &&
	       width * height <= R_IMAGE_MAX_BYTES / bytes_per_pixel;
}

typedef enum {
	IM_ERROR   = -1,
	IM_UNKNOWN =  0,
	IM_XPM     =  1,
	IM_TIFF    =  2,
	IM_PNG     =  3,
	IM_PPM     =  4,
	IM_JPEG    =  5,
	IM_GIF     =  6,
	IM_WEBP    =  7,
	IM_JXL     =  8,
	IM_BMP     =  9,
	IM_SVG     = 10,
	IM_HEIF    = 11
} WRImgFormat;

/* How many image types we have. */
/* Increase this when adding new image types! */
#define IM_TYPES    11

/*
 * Function for Loading in a specific format
 */
RImage *RLoadPPM(const char *file);

/* stb_image: P5/P6 PNM, BMP, TGA, GIF, PSD, PIC */
RImage *RLoadStb(const char *file);
RImage *RLoadStbGIF(const char *file, int index);

RImage *RLoadXPM(RContext *context, const char *file);

#ifdef USE_TIFF
RImage *RLoadTIFF(const char *file, int index);
#endif

#ifdef USE_PNG
RImage *RLoadPNG(RContext *context, const char *file);
#endif

#ifdef USE_JPEG
RImage *RLoadJPEG(const char *file);
#endif

#ifdef USE_JXL
RImage *RLoadJXL(const char *file);
#endif

#ifdef USE_WEBP
RImage *RLoadWEBP(const char *file);
#endif

#ifdef USE_SVG
RImage *RLoadSVG(const char *file, unsigned width, unsigned height);
#endif

#ifdef USE_HEIF
RImage *RLoadHEIF(const char *file, int index);
#endif

#ifdef USE_MAGICK
RImage *RLoadMagick(const char *file_name);

void RReleaseMagick(void);
#endif

/*
 * Read an entire file into a freshly malloc'd buffer for in-memory
 * decoders. Sets RErrorCode and returns NULL on open/seek/read/alloc
 * failure or when the file exceeds the internal size cap. The caller
 * frees the buffer with free().
 */
unsigned char *RReadWholeFile(const char *path, size_t *out_size);

/*
 * Function for Saving in a specific format
 */
int RSaveXPM(RImage *image, const char *filename);

#ifdef USE_PNG
int RSavePNG(RImage *image, const char *filename, char *title);
int RSaveRawPNG(RImage *image, char *title, unsigned char **out_buf, size_t *out_size);
#endif

#ifdef USE_JPEG
int RSaveJPEG(RImage *image, const char *filename, char *title);
int RSaveRawJPEG(RImage *image, char *title, unsigned char **out_buf, size_t *out_size);
#endif


#endif
