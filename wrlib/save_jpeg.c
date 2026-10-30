/* save_jpeg.c - save JPEG image
 *
 * Raster graphics library
 *
 * Copyright (c) 2023-2026 Window Maker Team
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
#include <errno.h>
#include <jpeglib.h>

#include "wraster.h"
#include "imgformat.h"
#include "wr_i18n.h"

/* Structure for JPEG memory destination */
struct jpeg_mem_data {
	unsigned char **out_buf;
	size_t *out_size;
	unsigned char *buffer;
	size_t buffer_size;
};

/* JPEG memory destination methods */
static void jpeg_init_mem_destination(j_compress_ptr cinfo)
{
	struct jpeg_mem_data *dest = (struct jpeg_mem_data *)cinfo->client_data;
	dest->buffer_size = 32768; /* Initial buffer size */
	dest->buffer = malloc(dest->buffer_size);
	if (!dest->buffer) {
		/* Memory allocation failed - will be caught by caller */
		dest->buffer_size = 0;
		return;
	}
	cinfo->dest->next_output_byte = dest->buffer;
	cinfo->dest->free_in_buffer = dest->buffer_size;
}

static boolean jpeg_empty_mem_output_buffer(j_compress_ptr cinfo)
{
	struct jpeg_mem_data *dest = (struct jpeg_mem_data *)cinfo->client_data;
	size_t old_size = dest->buffer_size;
	dest->buffer_size *= 2;
	dest->buffer = realloc(dest->buffer, dest->buffer_size);
	if (!dest->buffer) {
		/* Memory allocation failed - signal error */
		dest->buffer_size = 0;
		return FALSE;
	}
	cinfo->dest->next_output_byte = dest->buffer + old_size;
	cinfo->dest->free_in_buffer = dest->buffer_size - old_size;
	return TRUE;
}

static void jpeg_term_mem_destination(j_compress_ptr cinfo)
{
	struct jpeg_mem_data *dest = (struct jpeg_mem_data *)cinfo->client_data;
	*dest->out_size = dest->buffer_size - cinfo->dest->free_in_buffer;
	*dest->out_buf = dest->buffer;
	/* Don't free dest->buffer here - caller will free it */
}

/*
 *  Save RImage to JPEG data in memory
 */

int RSaveRawJPEG(RImage *img, char *title, unsigned char **out_buf, size_t *out_size)
{
	int x, y;
	unsigned char *buffer;
	size_t row_stride, buffer_size;
	RColor pixel;
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	struct jpeg_destination_mgr jpeg_dest;
	struct jpeg_mem_data mem_data;
	JSAMPROW row_pointer;

	*out_buf = NULL;
	*out_size = 0;

	if (!RImageDimensionsWithinLimits(img->width, img->height) ||
	    (size_t)img->width > SIZE_MAX / 3 ||
	    (size_t)img->height > SIZE_MAX / ((size_t)img->width * 3)) {
		RErrorCode = RERR_BADIMAGEFILE;
		return 0;
	}

	/* collect separate RGB values to a buffer */
	row_stride = (size_t)img->width * 3;
	buffer_size = row_stride * (size_t)img->height;
	buffer = malloc(buffer_size);
	if (!buffer) {
		RErrorCode = RERR_NOMEMORY;
		return 0;
	}

	for (y = 0; y < img->height; y++) {
		for (x = 0; x < img->width; x++) {
			size_t offset = (size_t)y * row_stride + (size_t)x * 3;

			RGetPixel(img, x, y, &pixel);
			/* Handle transparent pixels by converting them to white
			 * since JPEG doesn't support transparency */
			if (pixel.alpha == 0) {
				buffer[offset + 0] = 255; /* white red */
				buffer[offset + 1] = 255; /* white green */
				buffer[offset + 2] = 255; /* white blue */
			} else {
				buffer[offset + 0] = pixel.red;
				buffer[offset + 1] = pixel.green;
				buffer[offset + 2] = pixel.blue;
			}
		}
	}

	/* Setup Exception handling */
	cinfo.err = jpeg_std_error(&jerr);

	/* Initialize cinfo structure */
	jpeg_create_compress(&cinfo);

	/* Set up custom memory destination */
	mem_data.out_buf = out_buf;
	mem_data.out_size = out_size;
	jpeg_dest.init_destination = jpeg_init_mem_destination;
	jpeg_dest.empty_output_buffer = jpeg_empty_mem_output_buffer;
	jpeg_dest.term_destination = jpeg_term_mem_destination;
	cinfo.dest = &jpeg_dest;
	cinfo.dest->next_output_byte = NULL;
	cinfo.dest->free_in_buffer = 0;
	cinfo.client_data = &mem_data;

	cinfo.image_width = img->width;
	cinfo.image_height = img->height;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;

	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, 85, TRUE);
	jpeg_start_compress(&cinfo, TRUE);

	/* Set title if any */
	if (title)
		jpeg_write_marker(&cinfo, JPEG_COM, (const JOCTET*)title, strlen(title));

	while (cinfo.next_scanline < cinfo.image_height) {
		row_pointer = (JSAMPROW) &buffer[(size_t)cinfo.next_scanline * row_stride];
		jpeg_write_scanlines(&cinfo, &row_pointer, 1);
	}

	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);
	
	/* Clean */
	free(buffer);

	return 1;
}

/*
 *  Save RImage to JPEG file
 */

int RSaveJPEG(RImage *img, const char *filename, char *title)
{
	FILE *file;
	unsigned char *jpeg_data;
	size_t jpeg_size;
	size_t written;

	/* Generate JPEG data in memory */
	if (!RSaveRawJPEG(img, title, &jpeg_data, &jpeg_size)) {
		return 0;
	}

	/* Write to file */
	file = fopen(filename, "wb");
	if (!file) {
		free(jpeg_data);
		RErrorCode = RERR_OPEN;
		return 0;
	}

	written = fwrite(jpeg_data, 1, jpeg_size, file);
	fclose(file);
	free(jpeg_data);

	if (written != jpeg_size) {
		RErrorCode = RERR_WRITE;
		return 0;
	}

	return 1;
}
