/* load_svg.c - load SVG image from file via librsvg into a cairo surface
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

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <cairo.h>
#include <librsvg/rsvg.h>

#include "wraster.h"
#include "imgformat.h"

/* legitimate icon-theme SVGs are a few KiB, cap the input well below
 * RReadWholeFile's default so a hostile file cannot hand librsvg an
 * arbitrarily large document */
#define RLOAD_SVG_MAX_BYTES (4u << 20)

/* rendered when the file declares no intrinsic pixel size (viewBox
 * only, or 100% width/height) and the caller passed 0,0 */
#define RLOAD_SVG_DEFAULT_SIZE 256

/* Convert one CAIRO_FORMAT_ARGB32 image surface into a fresh
 * RRGBAFormat RImage: cairo stores premultiplied colour in a
 * native-endian word, RImage wants byte-order straight-alpha RGBA. */
static RImage *rimage_from_argb32(cairo_surface_t *surface)
{
	RImage *image;
	unsigned char *dst;
	unsigned char *row;
	uint32_t px, a;
	int stride, w, h, x, y;

	w = cairo_image_surface_get_width(surface);
	h = cairo_image_surface_get_height(surface);

	image = RCreateImage(w, h, 1);
	if (!image)
		return NULL;

	cairo_surface_flush(surface);
	stride = cairo_image_surface_get_stride(surface);
	row = cairo_image_surface_get_data(surface);
	dst = image->data;

	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			px = ((uint32_t *)row)[x];
			a = px >> 24;
			if (a == 0) {
				dst[0] = dst[1] = dst[2] = dst[3] = 0;
			} else if (a == 0xff) {
				dst[0] = (px >> 16) & 0xff;
				dst[1] = (px >>  8) & 0xff;
				dst[2] =  px        & 0xff;
				dst[3] = 0xff;
			} else {
				/* un-premultiply with rounding */
				dst[0] = (((px >> 16) & 0xff) * 255 + a / 2) / a;
				dst[1] = (((px >>  8) & 0xff) * 255 + a / 2) / a;
				dst[2] = (( px        & 0xff) * 255 + a / 2) / a;
				dst[3] = a;
			}
			dst += 4;
		}
		row += stride;
	}
	return image;
}

RImage *RLoadSVGFromData(const char *data, size_t size, unsigned int width, unsigned int height)
{
	RImage *image;
	RsvgHandle *handle;
	RsvgRectangle viewport;
	GError *gerr;
	cairo_surface_t *surface;
	cairo_t *cr;
	gdouble iw, ih;
	unsigned target_w, target_h;

	image = NULL;
	gerr = NULL;

	if (size == 0 || size > RLOAD_SVG_MAX_BYTES) {
		RErrorCode = RERR_BADIMAGEFILE;
		return NULL;
	}

	handle = rsvg_handle_new_from_data((const guint8 *)data, size, &gerr);
	if (!handle) {
		RErrorCode = RERR_BADIMAGEFILE;
		if (gerr)
			g_error_free(gerr);
		return NULL;
	}

	/* an icon-theme file must not resolve external <image href> or
	 * <use> targets, and passing the buffer rather than the path
	 * already left the base URI unset so relative references cannot
	 * resolve either */

	if (width && height) {
		target_w = width;
		target_h = height;
	} else if (rsvg_handle_get_intrinsic_size_in_pixels(handle, &iw, &ih) &&
		   isfinite(iw) && isfinite(ih) && iw >= 1.0 && ih >= 1.0) {
		if (iw > RLOAD_SVG_DEFAULT_SIZE)
			iw = RLOAD_SVG_DEFAULT_SIZE;
		if (ih > RLOAD_SVG_DEFAULT_SIZE)
			ih = RLOAD_SVG_DEFAULT_SIZE;
		target_w = (unsigned)(iw + 0.5);
		target_h = (unsigned)(ih + 0.5);
	} else {
		target_w = target_h = RLOAD_SVG_DEFAULT_SIZE;
	}

	if (!RImageLoadWorkingSetWithinLimits(target_w, target_h, 8U)) {
		RErrorCode = RERR_BADIMAGEFILE;
		g_object_unref(handle);
		return NULL;
	}

	surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, target_w, target_h);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		RErrorCode = RERR_NOMEMORY;
		cairo_surface_destroy(surface);
		g_object_unref(handle);
		return NULL;
	}

	cr = cairo_create(surface);
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = target_w;
	viewport.height = target_h;
	if (!rsvg_handle_render_document(handle, cr, &viewport, &gerr)) {
		RErrorCode = RERR_BADIMAGEFILE;
		if (gerr)
			g_error_free(gerr);
		cairo_destroy(cr);
		cairo_surface_destroy(surface);
		g_object_unref(handle);
		return NULL;
	}
	cairo_destroy(cr);
	g_object_unref(handle);

	image = rimage_from_argb32(surface);
	cairo_surface_destroy(surface);
	return image;
}

RImage *RLoadSVG(const char *file_name, unsigned int width, unsigned int height)
{
	RImage *image;
	unsigned char *data;
	size_t size;

	data = RReadWholeFile(file_name, &size);
	if (!data)
		return NULL;

	image = RLoadSVGFromData((const char *)data, size, width, height);
	free(data);
	return image;
}
