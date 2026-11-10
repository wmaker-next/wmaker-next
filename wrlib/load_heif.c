/* load_heif.c - load HEIF/HEIC/AVIF image from file
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

#include <config.h>

#ifdef USE_HEIF

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <libheif/heif.h>

#include "wraster.h"
#include "imgformat.h"
#include "wr_i18n.h"

static void heif_copy_pixels(RImage *image, const uint8_t *plane, int stride,
			     int bytes_per_pixel)
{
	int y;
	size_t row_bytes;

	row_bytes = (size_t)image->width * bytes_per_pixel;
	for (y = 0; y < (int)image->height; y++)
		memcpy(image->data + y * row_bytes, plane + y * stride, row_bytes);
}

RImage *RLoadHEIF(const char *file, int index)
{
	RImage *image = NULL;
	unsigned char *data = NULL;
	size_t size;
	struct heif_context *ctx = NULL;
	struct heif_image_handle *handle = NULL;
	struct heif_image *heif_img = NULL;
	struct heif_error err;
	int width, height;
	int has_alpha, bytes_per_pixel;
	const uint8_t *plane;
	int stride;
	int nimages;

	data = RReadWholeFile(file, &size);
	if (!data)
		return NULL;

	ctx = heif_context_alloc();
	if (!ctx) {
		RErrorCode = RERR_NOMEMORY;
		goto error;
	}

	err = heif_context_read_from_memory_without_copy(ctx, data, size, NULL);
	if (err.code) {
		fprintf(stderr, _("wrlib: could not read HEIF file \"%s\": %s\n"),
			file, err.message);
		RErrorCode = RERR_BADIMAGEFILE;
		goto error;
	}

	if (index <= 0) {
		/* index 0 is the primary image, guaranteed to exist */
		err = heif_context_get_primary_image_handle(ctx, &handle);
	} else {
		heif_item_id *ids;

		nimages = heif_context_get_number_of_top_level_images(ctx);
		if (index >= nimages) {
			RErrorCode = RERR_BADINDEX;
			goto error;
		}
		ids = malloc((size_t)nimages * sizeof(*ids));
		if (!ids) {
			RErrorCode = RERR_NOMEMORY;
			goto error;
		}
		/* the handle API takes item ids, not ordinals, and the
		 * ids are not a dense 0..n-1 range */
		heif_context_get_list_of_top_level_image_IDs(ctx, ids, nimages);
		err = heif_context_get_image_handle(ctx, ids[index], &handle);
		free(ids);
	}
	if (err.code) {
		fprintf(stderr, _("wrlib: no image %d in HEIF file \"%s\": %s\n"),
			index, file, err.message);
		RErrorCode = RERR_BADIMAGEFILE;
		goto error;
	}

	width = heif_image_handle_get_width(handle);
	height = heif_image_handle_get_height(handle);
	has_alpha = heif_image_handle_has_alpha_channel(handle);
	bytes_per_pixel = has_alpha ? 4 : 3;

	/* the libheif plane and the RImage are alive together */
	if (!RImageLoadWorkingSetWithinLimits(width, height,
					 (uint64_t)bytes_per_pixel * 2U)) {
		RErrorCode = RERR_BADIMAGEFILE;
		goto error;
	}

	err = heif_decode_image(handle, &heif_img, heif_colorspace_RGB,
				has_alpha ? heif_chroma_interleaved_RGBA :
					    heif_chroma_interleaved_RGB, NULL);
	if (err.code) {
		fprintf(stderr, _("wrlib: could not decode HEIF file \"%s\": %s\n"),
			file, err.message);
		RErrorCode = RERR_BADIMAGEFILE;
		goto error;
	}

	image = RCreateImage(width, height, has_alpha);
	if (!image) {
		RErrorCode = RERR_NOMEMORY;
		goto error;
	}

	plane = heif_image_get_plane_readonly(heif_img, heif_channel_interleaved,
					      &stride);
	if (!plane) {
		RErrorCode = RERR_BADIMAGEFILE;
		goto error;
	}

	/* the copy trusts the handle geometry, so reject a decoded image
	 * that disagrees or a stride too small for a full row */
	if (heif_image_get_width(heif_img, heif_channel_interleaved) != width
	    || heif_image_get_height(heif_img, heif_channel_interleaved) != height
	    || stride < width * bytes_per_pixel) {
		RErrorCode = RERR_BADIMAGEFILE;
		goto error;
	}

	heif_copy_pixels(image, plane, stride, bytes_per_pixel);

	heif_image_release(heif_img);
	heif_image_handle_release(handle);
	heif_context_free(ctx);
	free(data);

	return image;

error:
	if (image)
		RReleaseImage(image);
	if (heif_img)
		heif_image_release(heif_img);
	if (handle)
		heif_image_handle_release(handle);
	if (ctx)
		heif_context_free(ctx);
	free(data);
	return NULL;
}

#endif /* USE_HEIF */
