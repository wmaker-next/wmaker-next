/* cairo.c - RImage and gradient bridge to cairo image surfaces / patterns
 *
 * Raster graphics library
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

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include <cairo.h>

#include "wraster.h"

/* ((c * a) + rounding) / 255 without a divide */
#define MULT_A(c, a) (((unsigned)(c) * (a) + 127) / 255)

void RImageDropCairoSurface(RImage *image)
{
	if (image && image->cairo_surface) {
		cairo_surface_destroy((cairo_surface_t *)image->cairo_surface);
		image->cairo_surface = NULL;
	}
}

/*
 * Return a CAIRO_FORMAT_ARGB32 image surface holding a premultiplied
 * copy of the RImage pixels. RImage storage is byte-order RGB or RGBA
 * with straight alpha, cairo's ARGB32 is a native-endian 32-bit word
 * (A in the high byte, then R, G, B) with premultiplied colour, so one
 * pass swizzles and premultiplies. The result is cached on the RImage
 * so repeat calls are free until RReleaseImage(), and the caller gets
 * an added reference so its cairo_surface_destroy() is balanced. The
 * cache is a snapshot: mutating image->data afterwards does not update
 * it, call RImageDropCairoSurface() first if that matters.
 */
cairo_surface_t *RImageCairoSurface(RImage *image)
{
	cairo_surface_t *surface;
	unsigned char *src;
	unsigned char *row;
	uint32_t *dst;
	int stride;
	int x, y;
	unsigned a;

	if (!image)
		return NULL;

	if (image->cairo_surface)
		return cairo_surface_reference((cairo_surface_t *)image->cairo_surface);

	surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
					     image->width, image->height);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(surface);
		RErrorCode = RERR_NOMEMORY;
		return NULL;
	}

	cairo_surface_flush(surface);
	stride = cairo_image_surface_get_stride(surface);
	row = cairo_image_surface_get_data(surface);
	src = image->data;

	if (image->format == RRGBAFormat) {
		for (y = 0; y < image->height; y++) {
			dst = (uint32_t *)row;
			for (x = 0; x < image->width; x++) {
				a = src[3];
				*dst++ = ((uint32_t)a << 24)
				       | ((uint32_t)MULT_A(src[0], a) << 16)
				       | ((uint32_t)MULT_A(src[1], a) << 8)
				       |  (uint32_t)MULT_A(src[2], a);
				src += 4;
			}
			row += stride;
		}
	} else {
		for (y = 0; y < image->height; y++) {
			dst = (uint32_t *)row;
			for (x = 0; x < image->width; x++) {
				*dst++ = 0xff000000u
				       | ((uint32_t)src[0] << 16)
				       | ((uint32_t)src[1] << 8)
				       |  (uint32_t)src[2];
				src += 3;
			}
			row += stride;
		}
	}

	cairo_surface_mark_dirty(surface);
	image->cairo_surface = surface;
	return cairo_surface_reference(surface);
}

static cairo_pattern_t *linearForStyle(RGradientStyle style, unsigned width, unsigned height)
{
	switch (style) {
	case RHorizontalGradient:
		return cairo_pattern_create_linear(0, 0, width, 0);
	case RVerticalGradient:
		return cairo_pattern_create_linear(0, 0, 0, height);
	case RDiagonalGradient:
		/* the raster diagonal gradient interpolates along both axes
		 * so the top-right and bottom-left corners land on the
		 * midpoint colour, which is exactly a linear pattern from
		 * (0,0) to (width,height) */
		return cairo_pattern_create_linear(0, 0, width, height);
	}
	return NULL;
}

/*
 * Map a two-stop RGradientStyle onto a cairo linear pattern of the
 * same geometry as RRenderGradient(). width/height give the target
 * extent so the diagonal case has the right slope. The returned
 * pattern is owned by the caller and released with
 * cairo_pattern_destroy().
 */
cairo_pattern_t *RGradientCairoPattern(RGradientStyle style,
				       unsigned width, unsigned height,
				       const RColor *from, const RColor *to)
{
	cairo_pattern_t *pat;

	pat = linearForStyle(style, width, height);
	if (!pat)
		return NULL;

	cairo_pattern_add_color_stop_rgb(pat, 0.0,
			from->red / 255.0, from->green / 255.0, from->blue / 255.0);
	cairo_pattern_add_color_stop_rgb(pat, 1.0,
			to->red / 255.0, to->green / 255.0, to->blue / 255.0);
	return pat;
}

/*
 * Multi-stop variant matching RRenderMultiGradient(): colours is a
 * NULL-terminated array of RColor pointers spread evenly from 0 to 1.
 */
cairo_pattern_t *RMultiGradientCairoPattern(RGradientStyle style,
					    unsigned width, unsigned height,
					    RColor **colors)
{
	cairo_pattern_t *pat;
	int i, count;

	for (count = 0; colors[count] != NULL; count++)
		;
	if (count < 2)
		return NULL;

	pat = linearForStyle(style, width, height);
	if (!pat)
		return NULL;

	for (i = 0; i < count; i++)
		cairo_pattern_add_color_stop_rgb(pat, (double)i / (count - 1),
				colors[i]->red / 255.0,
				colors[i]->green / 255.0,
				colors[i]->blue / 255.0);
	return pat;
}
