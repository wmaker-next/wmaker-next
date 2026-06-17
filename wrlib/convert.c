/* convert.c - convert RImage to Pixmap
 *
 * Raster graphics library
 *
 * Copyright (c) 1997-2003 Alfredo K. Kojima
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
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

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

static RXImage *image2TrueColor(RContext * ctx, RImage * image)
{
	RXImage *ximg;
	unsigned short rmask, gmask, bmask;
	unsigned short roffs, goffs, boffs;
	unsigned short *rtable, *gtable, *btable;
	int channels = (HAS_ALPHA(image) ? 4 : 3);
	unsigned char *ptr = image->data;
	unsigned long r, g, b;
	unsigned long pixel;
	int ofs, x, y;

	ximg = RCreateXImage(ctx, ctx->depth, image->width, image->height);
	if (!ximg) {
		return NULL;
	}

	roffs = ctx->red_offset;
	goffs = ctx->green_offset;
	boffs = ctx->blue_offset;

	rmask = ctx->visual->red_mask >> roffs;
	gmask = ctx->visual->green_mask >> goffs;
	bmask = ctx->visual->blue_mask >> boffs;

	rtable = computeTable(rmask);
	gtable = computeTable(gmask);
	btable = computeTable(bmask);

	if (rtable == NULL || gtable == NULL || btable == NULL) {
		RErrorCode = RERR_NOMEMORY;
		RDestroyXImage(ctx, ximg);
		return NULL;
	}

	if (rmask == 0xff && gmask == 0xff && bmask == 0xff) {
		for (y = 0; y < image->height; y++) {
			for (x = 0; x < image->width; x++, ptr += channels) {
				/* reduce pixel */
				r = ptr[0];
				g = ptr[1];
				b = ptr[2];
				pixel = (r << roffs) | (g << goffs) | (b << boffs);
				XPutPixel(ximg->image, x, y, pixel);
			}
		}
	} else {
		for (y = 0, ofs = 0; y < image->height; y++) {
			for (x = 0; x < image->width; x++, ofs += channels - 3) {
				/* reduce pixel */
				r = rtable[ptr[ofs++]];
				g = gtable[ptr[ofs++]];
				b = btable[ptr[ofs++]];
				pixel = (r << roffs) | (g << goffs) | (b << boffs);
				XPutPixel(ximg->image, x, y, pixel);
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
			XPutPixel(ximg->image, x, y, (*alpha <= threshold ? 0 : 1));
			alpha += 4;
		}
	}

	return ximg;
}

int RConvertImage(RContext * context, RImage * image, Pixmap * pixmap)
{
	RXImage *ximg = NULL;

	assert(context != NULL);
	assert(image != NULL);
	assert(pixmap != NULL);

	ximg = image2TrueColor(context, image);
	if (!ximg) {
		return False;
	}

	*pixmap = XCreatePixmap(context->dpy, context->drawable, image->width, image->height, context->depth);
	RPutXImage(context, *pixmap, context->copy_gc, ximg, 0, 0, 0, 0, image->width, image->height);
	RDestroyXImage(context, ximg);

	return True;
}

/* make the gc permanent (create with context creation).
 * GC creation is very expensive. altering its properties is not. -Dan
 */
int RConvertImageMask(RContext * context, RImage * image, Pixmap * pixmap, Pixmap * mask, int threshold)
{
	GC gc;
	XGCValues gcv;
	RXImage *ximg = NULL;

	assert(context != NULL);
	assert(image != NULL);
	assert(pixmap != NULL);
	assert(mask != NULL);

	if (!RConvertImage(context, image, pixmap))
		return False;

	if (image->format == RRGBFormat) {
		*mask = None;
		return True;
	}

	ximg = image2Bitmap(context, image, threshold);

	if (!ximg) {
		return False;
	}
	*mask = XCreatePixmap(context->dpy, context->drawable, image->width, image->height, 1);
	gcv.foreground = context->black;
	gcv.background = context->white;
	gcv.graphics_exposures = False;
	gc = XCreateGC(context->dpy, *mask, GCForeground | GCBackground | GCGraphicsExposures, &gcv);
	RPutXImage(context, *mask, gc, ximg, 0, 0, 0, 0, image->width, image->height);
	RDestroyXImage(context, ximg);
	XFreeGC(context->dpy, gc);

	return True;
}

Bool RGetClosestXColor(RContext * context, const RColor * color, XColor * retColor)
{
	unsigned short rmask, gmask, bmask;
	unsigned short roffs, goffs, boffs;
	unsigned short *rtable, *gtable, *btable;

	roffs = context->red_offset;
	goffs = context->green_offset;
	boffs = context->blue_offset;

	rmask = context->visual->red_mask >> roffs;
	gmask = context->visual->green_mask >> goffs;
	bmask = context->visual->blue_mask >> boffs;

	rtable = computeTable(rmask);
	gtable = computeTable(gmask);
	btable = computeTable(bmask);

	if (rtable == NULL || gtable == NULL || btable == NULL) {
		RErrorCode = RERR_NOMEMORY;
		return False;
	}

	retColor->pixel = (((unsigned long) rtable[color->red])   << roffs)
		       |  (((unsigned long) gtable[color->green]) << goffs)
		       |  (((unsigned long) btable[color->blue])  << boffs);

	retColor->red = color->red << 8;
	retColor->green = color->green << 8;
	retColor->blue = color->blue << 8;
	retColor->flags = DoRed | DoGreen | DoBlue;

	return True;
}
