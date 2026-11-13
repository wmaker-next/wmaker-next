/* convert.c - convert RImage to Pixmap
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
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <xcb/xcb_image.h>

#include "wraster.h"
#include "convert.h"
#include "xutil.h"
#include "wr_i18n.h"


#define HAS_ALPHA(I)	((I)->format == RRGBAFormat)

typedef struct RConversionTable {
	unsigned short table[256];
	unsigned short index;

	struct RConversionTable *next;
} RConversionTable;

static RConversionTable *conversionTable = NULL;

void r_destroy_conversion_tables(void)
{
	RConversionTable *tmp = conversionTable;

	while (tmp) {
		RConversionTable *tmp_to_delete = tmp;

		tmp = tmp->next;
		free(tmp_to_delete);
	}
	conversionTable = NULL;
}

static unsigned short *computeTable(unsigned short mask)
{
	RConversionTable *tmp = conversionTable;
	int i;

	while (tmp) {
		if (tmp->index == mask)
			break;
		tmp = tmp->next;
	}

	if (tmp)
		return tmp->table;

	tmp = (RConversionTable *) malloc(sizeof(RConversionTable));
	if (tmp == NULL)
		return NULL;

	for (i = 0; i < 256; i++)
		tmp->table[i] = (i * mask + 0x7f) / 0xff;

	tmp->index = mask;
	tmp->next = conversionTable;
	conversionTable = tmp;
	return tmp->table;
}

/* the host byte order in the X11 LSBFirst/MSBFirst encoding */
static xcb_image_order_t host_byte_order(void)
{
	static const uint32_t probe = 1;

	return *(const uint8_t *)&probe ? XCB_IMAGE_ORDER_LSB_FIRST
					: XCB_IMAGE_ORDER_MSB_FIRST;
}

static RXImage *image2TrueColor(RContext * ctx, RImage * image)
{
	RXImage *ximg;
	xcb_image_t *xi;
	unsigned short rmask, gmask, bmask;
	unsigned short roffs, goffs, boffs;
	unsigned short *rtable, *gtable, *btable;
	int channels = (HAS_ALPHA(image) ? 4 : 3);
	unsigned char *ptr = image->data;
	uint32_t r, g, b, pixel;
	uint8_t *row;
	bool swap;
	int x, y;

	ximg = RCreateXImage(ctx, ctx->depth, image->width, image->height);
	if (!ximg) {
		return NULL;
	}
	xi = ximg->ximg;

	roffs = ctx->red_offset;
	goffs = ctx->green_offset;
	boffs = ctx->blue_offset;

	rmask = ctx->xvisual->red_mask >> roffs;
	gmask = ctx->xvisual->green_mask >> goffs;
	bmask = ctx->xvisual->blue_mask >> boffs;

	swap = host_byte_order() != xi->byte_order;

	/* 32 bpp TrueColor: direct stride packer. The image carries the
	 * server's byte order and xcb_image_put sends the buffer verbatim,
	 * so a mismatched server order is byteswapped. The generic arm
	 * covers 15/16 bpp via xcb_image_put_pixel. */
	if (rmask == 0xff && gmask == 0xff && bmask == 0xff && xi->bpp == 32) {
		for (y = 0; y < image->height; y++) {
			row = xi->data + (size_t)y * xi->stride;
			for (x = 0; x < image->width; x++, ptr += channels) {
				pixel = ((uint32_t)ptr[0] << roffs)
				      | ((uint32_t)ptr[1] << goffs)
				      | ((uint32_t)ptr[2] << boffs);
				if (swap)
					pixel = ((pixel & 0x000000ffu) << 24)
					      | ((pixel & 0x0000ff00u) << 8)
					      | ((pixel & 0x00ff0000u) >> 8)
					      | (pixel >> 24);
				((uint32_t *)row)[x] = pixel;
			}
		}
	} else {
		rtable = computeTable(rmask);
		gtable = computeTable(gmask);
		btable = computeTable(bmask);

		if (rtable == NULL || gtable == NULL || btable == NULL) {
			RErrorCode = RERR_NOMEMORY;
			RDestroyXImage(ctx, ximg);
			return NULL;
		}
		for (y = 0; y < image->height; y++) {
			for (x = 0; x < image->width; x++, ptr += channels) {
				r = rtable[ptr[0]];
				g = gtable[ptr[1]];
				b = btable[ptr[2]];
				pixel = (r << roffs) | (g << goffs) | (b << boffs);
				xcb_image_put_pixel(xi, x, y, pixel);
			}
		}
	}

	return ximg;
}

static RXImage *image2Bitmap(RContext * ctx, RImage * image, int threshold)
{
	RXImage *ximg;
	unsigned char *alpha;
	int x, y;

	ximg = RCreateXImage(ctx, 1, image->width, image->height);
	if (!ximg) {
		return NULL;
	}
	alpha = image->data + 3;

	for (y = 0; y < image->height; y++) {
		for (x = 0; x < image->width; x++) {
			xcb_image_put_pixel(ximg->ximg, x, y, (*alpha <= threshold ? 0 : 1));
			alpha += 4;
		}
	}

	return ximg;
}

static RXImage *image2TrueColorPixmap(RContext *context, RImage *image, xcb_pixmap_t *pixmap,
				      xcb_void_cookie_t *create_ck)
{
	RXImage *ximg = image2TrueColor(context, image);

	if (!ximg)
		return NULL;
	*pixmap = xcb_generate_id(context->xc);
	*create_ck = xcb_create_pixmap_checked(context->xc, context->depth, *pixmap,
					       context->drawable, image->width, image->height);
	return ximg;
}

int RConvertImage(RContext * context, RImage * image, xcb_pixmap_t * pixmap)
{
	RXImage *ximg;
	xcb_void_cookie_t create_ck;
	xcb_generic_error_t *error;

	assert(context != NULL);
	assert(image != NULL);
	assert(pixmap != NULL);

	*pixmap = XCB_NONE;

	ximg = image2TrueColorPixmap(context, image, pixmap, &create_ck);
	if (!ximg) {
		return 0;
	}

	error = xcb_request_check(context->xc, create_ck);
	if (error) {
		free(error);
		*pixmap = XCB_NONE;
		RErrorCode = RERR_XERROR;
		RDestroyXImage(context, ximg);
		return 0;
	}
	r_put_ximage(context, *pixmap, context->copy_xgc, ximg, 0, 0, 0, 0, image->width, image->height);
	RDestroyXImage(context, ximg);

	return 1;
}

int RConvertImageMask(RContext * context, RImage * image, xcb_pixmap_t * pixmap, xcb_pixmap_t * mask, int threshold)
{
	xcb_gcontext_t gc;
	uint32_t gcvals[3];
	RXImage *color, *ximg;
	xcb_void_cookie_t ck_pix, ck_mask, ck_gc;
	xcb_generic_error_t *err_pix, *err_mask, *err_gc;

	assert(context != NULL);
	assert(image != NULL);
	assert(pixmap != NULL);
	assert(mask != NULL);

	*pixmap = *mask = XCB_NONE;

	if (image->format == RRGBFormat) {
		*mask = XCB_NONE;
		return RConvertImage(context, image, pixmap);
	}

	ximg = image2Bitmap(context, image, threshold);
	if (!ximg) {
		*pixmap = *mask = XCB_NONE;
		return 0;
	}
	color = image2TrueColorPixmap(context, image, pixmap, &ck_pix);
	if (!color) {
		RDestroyXImage(context, ximg);
		*pixmap = *mask = XCB_NONE;
		return 0;
	}
	gcvals[0] = context->black;
	gcvals[1] = context->white;
	gcvals[2] = 0;
	*mask = xcb_generate_id(context->xc);
	ck_mask = xcb_create_pixmap_checked(context->xc, 1, *mask,
					    context->drawable, image->width, image->height);
	gc = xcb_generate_id(context->xc);
	ck_gc = xcb_create_gc_checked(context->xc, gc, *mask,
				      XCB_GC_FOREGROUND | XCB_GC_BACKGROUND |
				      XCB_GC_GRAPHICS_EXPOSURES, gcvals);

	/* Check the newest cookie first. One barrier covers the whole
	 * batch and the older checks pick up errors already read. */
	err_gc = xcb_request_check(context->xc, ck_gc);
	err_mask = xcb_request_check(context->xc, ck_mask);
	err_pix = xcb_request_check(context->xc, ck_pix);
	if (err_pix || err_mask || err_gc) {
		free(err_pix);
		free(err_mask);
		free(err_gc);
		if (!err_gc)
			xcb_free_gc(context->xc, gc);
		if (!err_mask)
			xcb_free_pixmap(context->xc, *mask);
		if (!err_pix)
			xcb_free_pixmap(context->xc, *pixmap);
		*pixmap = *mask = XCB_NONE;
		RDestroyXImage(context, color);
		RDestroyXImage(context, ximg);
		RErrorCode = RERR_XERROR;
		return 0;
	}

	r_put_ximage(context, *pixmap, context->copy_xgc, color, 0, 0, 0, 0, image->width, image->height);
	r_put_ximage(context, *mask, gc, ximg, 0, 0, 0, 0, image->width, image->height);
	RDestroyXImage(context, color);
	RDestroyXImage(context, ximg);
	xcb_free_gc(context->xc, gc);

	return 1;
}

int RGetClosestXColor(RContext * context, const RColor * color, uint32_t *pixel)
{
	unsigned short rmask, gmask, bmask;
	unsigned short roffs, goffs, boffs;
	unsigned short *rtable, *gtable, *btable;

	roffs = context->red_offset;
	goffs = context->green_offset;
	boffs = context->blue_offset;

	rmask = context->xvisual->red_mask >> roffs;
	gmask = context->xvisual->green_mask >> goffs;
	bmask = context->xvisual->blue_mask >> boffs;

	rtable = computeTable(rmask);
	gtable = computeTable(gmask);
	btable = computeTable(bmask);

	if (rtable == NULL || gtable == NULL || btable == NULL) {
		RErrorCode = RERR_NOMEMORY;
		return 0;
	}

	*pixel = (((unsigned long) rtable[color->red])   << roffs)
	       | (((unsigned long) gtable[color->green]) << goffs)
	       | (((unsigned long) btable[color->blue])  << boffs);

	return 1;
}
