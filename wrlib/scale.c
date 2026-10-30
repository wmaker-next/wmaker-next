/* scale.c - image scaling
 *
 * Raster graphics library
 *
 * Copyright (c) 1997-2003 Alfredo K. Kojima
 * Copyright (c) 2009-2026 Window Maker Team
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

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "wraster.h"
#include "scale.h"
#include "wr_i18n.h"

/* declarations only, the implementation is compiled once in load_stb.c */
#include "stb_image_resize2.h"


/*
 *----------------------------------------------------------------------
 * RScaleImage--
 * 	Creates a scaled copy of an image.
 *
 * Returns:
 * 	The new scaled image.
 *
 *----------------------------------------------------------------------
 */
RImage *RScaleImage(RImage * image, unsigned new_width, unsigned new_height)
{
	int ox;
	int px, py;
	register int x, y, t;
	int dx, dy;
	unsigned char *s;
	unsigned char *d;
	RImage *img;

	if (image == NULL)
		return NULL;

	if (new_width == image->width && new_height == image->height)
		return RCloneImage(image);

	img = RCreateImage(new_width, new_height, image->format == RRGBAFormat);

	if (!img)
		return NULL;

	/* fixed point math idea taken from Imlib by
	 * Carsten Haitzler (Rasterman) */
	dx = (image->width << 16) / new_width;
	dy = (image->height << 16) / new_height;

	py = 0;

	d = img->data;

	if (image->format == RRGBAFormat) {
		for (y = 0; y < new_height; y++) {
			t = image->width * (py >> 16);

			s = image->data + (t << 2);	/* image->data+t*4 */

			ox = 0;
			px = 0;
			for (x = 0; x < new_width; x++) {
				px += dx;

				*(d++) = *(s);
				*(d++) = *(s + 1);
				*(d++) = *(s + 2);
				*(d++) = *(s + 3);

				t = (px - ox) >> 16;
				ox += t << 16;

				s += t << 2;	/* t*4 */
			}
			py += dy;
		}
	} else {
		for (y = 0; y < new_height; y++) {
			t = image->width * (py >> 16);

			s = image->data + (t << 1) + t;	/* image->data+t*3 */

			ox = 0;
			px = 0;
			for (x = 0; x < new_width; x++) {
				px += dx;

				*(d++) = *(s);
				*(d++) = *(s + 1);
				*(d++) = *(s + 2);

				t = (px - ox) >> 16;
				ox += t << 16;

				s += (t << 1) + t;	/* t*3 */
			}
			py += dy;
		}
	}

	return img;
}

/*
 * Filtered rescaling is delegated to stb_image_resize2 (single-header,
 * public domain, SIMD float pipeline). The previous implementation was
 * the Graphics Gems III double-precision two-pass loop, which dropped
 * the alpha channel and ran ~4-5x slower at the same Catmull-Rom
 * kernel.
 *
 * Bell and Lanczos3 have no direct stbir kernel. Bell (quadratic
 * B-spline) maps to the cubic B-spline as the nearest Gaussian-like,
 * Lanczos3 maps to Catmull-Rom as the nearest sharp interpolant. Both
 * are visually equivalent for icon and wallpaper scaling, and neither
 * is the wrlib default.
 */

static stbir_filter scale_filter = STBIR_FILTER_CATMULLROM;

void wraster_change_filter(RScalingFilter type)
{
	switch (type) {
	case RBoxFilter:
		scale_filter = STBIR_FILTER_BOX;
		break;
	case RTriangleFilter:
		scale_filter = STBIR_FILTER_TRIANGLE;
		break;
	case RBellFilter:
	case RBSplineFilter:
		scale_filter = STBIR_FILTER_CUBICBSPLINE;
		break;
	case RMitchellFilter:
		scale_filter = STBIR_FILTER_MITCHELL;
		break;
	case RLanczos3Filter:
	case RCatmullRomFilter:
	default:
		scale_filter = STBIR_FILTER_CATMULLROM;
		break;
	}
}

RImage *RSmoothScaleImage(RImage *src, unsigned new_width, unsigned new_height)
{
	RImage *dst;
	stbir_pixel_layout layout;
	int alpha;

	alpha = (src->format == RRGBAFormat);
	dst = RCreateImage(new_width, new_height, alpha);
	if (!dst)
		return NULL;

	/* STBIR_RGBA premultiplies before filtering and unpremultiplies
	 * after so colour does not bleed across transparent edges.
	 * STBIR_EDGE_REFLECT matches the previous implementation's
	 * boundary handling. */
	layout = alpha ? STBIR_RGBA : STBIR_RGB;
	if (!stbir_resize(src->data, src->width, src->height, 0,
			  dst->data, new_width, new_height, 0,
			  layout, STBIR_TYPE_UINT8,
			  STBIR_EDGE_REFLECT, scale_filter)) {
		RReleaseImage(dst);
		RErrorCode = RERR_NOMEMORY;
		return NULL;
	}
	return dst;
}
