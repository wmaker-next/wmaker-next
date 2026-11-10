/* load_jxl.c - load JXL (JPEG XL) image from file
 *
 * Raster graphics library
 *
 * Copyright (c) 2025-2026 Window Maker Team
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

#ifdef USE_JXL

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <jxl/decode.h>

#include "wraster.h"
#include "imgformat.h"
#include "wr_i18n.h"

RImage *RLoadJXL(const char *file)
{
	RImage *image = NULL;
	unsigned char *data = NULL, *pixels = NULL;
	size_t size;
	JxlDecoder *dec = NULL;
	JxlDecoderStatus status;
	JxlBasicInfo info;
	JxlPixelFormat format;
	size_t buffer_size;
	int width = 0, height = 0;
	int has_alpha = 0;

	/* Load file data */
	data = RReadWholeFile(file, &size);
	if (!data)
		return NULL;

	/* Create decoder */
	dec = JxlDecoderCreate(NULL);
	if (!dec) {
		RErrorCode = RERR_NOMEMORY;
		goto error;
	}

	/* Subscribe to basic info and full image */
	if (JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS) {
		RErrorCode = RERR_BADIMAGEFILE;
		goto error;
	}

	/* Set input data */
	if (JxlDecoderSetInput(dec, data, size) != JXL_DEC_SUCCESS) {
		RErrorCode = RERR_BADIMAGEFILE;
		goto error;
	}

	/* Process events */
	for (;;) {
		status = JxlDecoderProcessInput(dec);

		if (status == JXL_DEC_ERROR) {
			RErrorCode = RERR_BADIMAGEFILE;
			goto error;
		}

		if (status == JXL_DEC_NEED_MORE_INPUT) {
			RErrorCode = RERR_BADIMAGEFILE;
			goto error;
		}

		if (status == JXL_DEC_BASIC_INFO) {
			if (JxlDecoderGetBasicInfo(dec, &info) != JXL_DEC_SUCCESS) {
				RErrorCode = RERR_BADIMAGEFILE;
				goto error;
			}

			width = info.xsize;
			height = info.ysize;

			/* Check if image has alpha channel */
			has_alpha = (info.alpha_bits > 0);

			/* the RImage and the decoder output buffer are alive
			 * together */
			if (!RImageLoadWorkingSetWithinLimits(width, height,
							 has_alpha ? 8U : 6U)) {
				RErrorCode = RERR_BADIMAGEFILE;
				goto error;
			}

			/* Set pixel format based on alpha channel presence */
			if (has_alpha) {
				format.num_channels = 4;  /* RGBA */
			} else {
				format.num_channels = 3;  /* RGB */
			}
			format.data_type = JXL_TYPE_UINT8;
			format.endianness = JXL_NATIVE_ENDIAN;
			format.align = 0;

		} else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
			/* Allocate image with or without alpha */
			image = RCreateImage(width, height, has_alpha ? 1 : 0);
			if (!image) {
				RErrorCode = RERR_NOMEMORY;
				goto error;
			}

			/* Determine buffer size */
			if (JxlDecoderImageOutBufferSize(dec, &format, &buffer_size) != JXL_DEC_SUCCESS) {
				RErrorCode = RERR_BADIMAGEFILE;
				goto error;
			}

			/* the copy length derives from the basic info, so
			 * require the decoder's own size to cover it */
			if (buffer_size < (size_t)width * height * (has_alpha ? 4 : 3)) {
				RErrorCode = RERR_BADIMAGEFILE;
				goto error;
			}

			/* Allocate buffer */
			pixels = malloc(buffer_size);
			if (!pixels) {
				RErrorCode = RERR_NOMEMORY;
				goto error;
			}

			/* Set output buffer */
			if (JxlDecoderSetImageOutBuffer(dec, &format, pixels, buffer_size) != JXL_DEC_SUCCESS) {
				RErrorCode = RERR_BADIMAGEFILE;
				goto error;
			}
		} else if (status == JXL_DEC_FULL_IMAGE) {
			/* Image is ready, copy data directly for RGB or RGBA */
			if (has_alpha) {
				/* RGBA format - copy directly */
				memcpy(image->data, pixels, width * height * 4);
			} else {
				/* RGB format - copy directly */
				memcpy(image->data, pixels, width * height * 3);
			}
			break;
		} else if (status == JXL_DEC_SUCCESS) {
			/* All done */
			break;
		}
	}

	free(data);
	free(pixels);
	JxlDecoderDestroy(dec);
	return image;

error:
	if (data)
		free(data);
	if (pixels)
		free(pixels);
	if (image)
		RReleaseImage(image);
	if (dec)
		JxlDecoderDestroy(dec);
	return NULL;
}

#endif /* USE_JXL */