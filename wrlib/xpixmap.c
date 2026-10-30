/* xpixmap.c - Make RImage from Pixmap or XImage
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
#include <assert.h>

#include <xcb/xcb_image.h>

#include "wraster.h"
#include "imgformat.h"
#include "xutil.h"
#include "wr_i18n.h"


static int get_shifts(unsigned long mask)
{
	int i = 0;

	while (mask) {
		mask >>= 1;
		i++;
	}
	return i;
}

#define NORMALIZE_RED(pixel)	((rshift>0) ? ((pixel) & rmask) >> rshift \
    : ((pixel) & rmask) << -rshift)
#define NORMALIZE_GREEN(pixel)	((gshift>0) ? ((pixel) & gmask) >> gshift \
    : ((pixel) & gmask) << -gshift)
#define NORMALIZE_BLUE(pixel)	((bshift>0) ? ((pixel) & bmask) >> bshift \
    : ((pixel) & bmask) << -bshift)

#define MIN(a,b) ((a)<(b)?(a):(b))

/*
 * TrueColor reverse-pack and mask overlay against an xcb_image_t.
 * xcb_image_get returns plane_mask but not the visual masks (same as
 * XGetImage on a pixmap), so the context's visual is used whenever the
 * depths match, which is every in-tree caller.
 */
static RImage *unpack_generic(RContext *context, xcb_image_t *image, xcb_image_t *mask,
			      int width, int height, int depth,
			      int mwidth, int mheight)
{
	RImage *img;
	unsigned char *data;
	uint32_t pixel;
	int x, y;
	int rshift, gshift, bshift;
	uint32_t rmask, gmask, bmask;

	img = RCreateImage(width, height, mask != NULL);
	if (!img)
		return NULL;

	if (context->depth == depth) {
		rmask = context->xvisual->red_mask;
		gmask = context->xvisual->green_mask;
		bmask = context->xvisual->blue_mask;
	} else {
		rmask = gmask = bmask = 0;
	}

	rshift = get_shifts(rmask) - 8;
	gshift = get_shifts(gmask) - 8;
	bshift = get_shifts(bmask) - 8;

	data = img->data;

	if (depth == 1) {
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				pixel = xcb_image_get_pixel(image, x, y);
				if (pixel) {
					*data++ = 0;
					*data++ = 0;
					*data++ = 0;
				} else {
					*data++ = 0xff;
					*data++ = 0xff;
					*data++ = 0xff;
				}
				if (mask)
					data++;
			}
		}
	} else {
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				pixel = xcb_image_get_pixel(image, x, y);
				*(data++) = NORMALIZE_RED(pixel);
				*(data++) = NORMALIZE_GREEN(pixel);
				*(data++) = NORMALIZE_BLUE(pixel);
				if (mask)
					data++;
			}
		}
	}

	if (mask) {
		data = img->data + 3;
		for (y = 0; y < MIN(mheight, height); y++) {
			for (x = 0; x < MIN(mwidth, width); x++) {
				if (mwidth <= width && xcb_image_get_pixel(mask, x, y)) {
					*data = 0xff;
				} else {
					*data = 0;
				}
				data += 4;
			}
			for (; x < width; x++) {
				*data = 0;
				data += 4;
			}
		}
		for (; y < height; y++) {
			for (x = 0; x < width; x++) {
				*data = 0;
				data += 4;
			}
		}
	}
	return img;
}

RImage *r_unpack_ximage(RContext *context, xcb_image_t *image, xcb_image_t *mask)
{
	return unpack_generic(context, image, mask,
			      image->width, image->height, image->depth,
			      mask ? mask->width : 0, mask ? mask->height : 0);
}

RImage *RCreateImageFromRXImage(RContext *context, RXImage *image, RXImage *mask)
{
	assert(image != NULL && image->ximg != NULL);

	return r_unpack_ximage(context, image->ximg, mask ? mask->ximg : NULL);
}

RImage *RCreateImageFromDrawable(RContext * context, xcb_drawable_t drawable, xcb_pixmap_t mask)
{
	RImage *image;
	xcb_image_t *pimg, *mimg;
	xcb_get_geometry_cookie_t dck, mck;
	xcb_get_geometry_reply_t *dgeom, *mgeom = NULL;
	unsigned int w, h, mask_w, mask_h;

	assert(drawable != XCB_NONE);

	dck = xcb_get_geometry(context->xc, drawable);
	if (mask)
		mck = xcb_get_geometry(context->xc, mask);
	dgeom = xcb_get_geometry_reply(context->xc, dck, NULL);
	if (!dgeom) {
		if (mask)
			free(xcb_get_geometry_reply(context->xc, mck, NULL));
		fprintf(stderr, _("wrlib: invalid window or pixmap passed to RCreateImageFromDrawable\n"));
		return NULL;
	}
	w = dgeom->width;
	h = dgeom->height;
	free(dgeom);
	if (!RImageDimensionsWithinLimits(w, h)) {
		if (mask)
			free(xcb_get_geometry_reply(context->xc, mck, NULL));
		RErrorCode = RERR_NOMEMORY;
		return NULL;
	}
	pimg = xcb_image_get(context->xc, drawable, 0, 0, w, h,
			     ~(uint32_t)0, XCB_IMAGE_FORMAT_Z_PIXMAP);
	if (!pimg) {
		if (mask)
			free(xcb_get_geometry_reply(context->xc, mck, NULL));
		RErrorCode = RERR_XERROR;
		return NULL;
	}
	mimg = NULL;
	if (mask) {
		mgeom = xcb_get_geometry_reply(context->xc, mck, NULL);
		if (mgeom) {
			mask_w = mgeom->width > w ? w : mgeom->width;
			mask_h = mgeom->height > h ? h : mgeom->height;
			free(mgeom);
			if (mask_w > 0 && mask_h > 0)
				mimg = xcb_image_get(context->xc, mask, 0, 0, mask_w, mask_h,
						     ~(uint32_t)0, XCB_IMAGE_FORMAT_Z_PIXMAP);
		}
	}

	image = r_unpack_ximage(context, pimg, mimg);

	xcb_image_destroy(pimg);
	if (mimg)
		xcb_image_destroy(mimg);

	return image;
}
