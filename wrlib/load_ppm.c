/* ppm.c - load PPM image from file
 *
 *  Raster graphics library
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2013-2026 Window Maker Team
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
#include <limits.h>

#include "wraster.h"
#include "imgformat.h"
#include "wr_i18n.h"

typedef struct {
	FILE *file;
	const char *filename;
	int pushback;
	int has_pushback;
} PmReader;

static int pm_read_byte(PmReader *reader)
{
	if (reader->has_pushback) {
		reader->has_pushback = 0;
		return reader->pushback;
	}
	return getc(reader->file);
}

static void pm_unread_byte(PmReader *reader, int c)
{
	if (c == EOF)
		return;
	reader->pushback = c;
	reader->has_pushback = 1;
}

static int pm_is_space(int c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static int pm_skip_space(PmReader *reader)
{
	int c;

	for (;;) {
		c = pm_read_byte(reader);
		if (c == EOF)
			return 0;
		if (pm_is_space(c))
			continue;
		if (c == '#') {
			do {
				c = pm_read_byte(reader);
			} while (c != EOF && c != '\n' && c != '\r');
			if (c == EOF)
				return -1;
			continue;
		}
		pm_unread_byte(reader, c);
		return 0;
	}
}

static int pm_read_uint(PmReader *reader, int *value)
{
	int c;
	unsigned int result = 0;

	if (pm_skip_space(reader) < 0)
		return -1;

	c = pm_read_byte(reader);
	if (c < '0' || c > '9') {
		if (c != EOF)
			fprintf(stderr, _("wrlib: junk in PPM file \"%s\", expected an unsigned integer but got 0x%02X\n"),
				reader->filename, (unsigned int)c);
		return -1;
	}

	do {
		unsigned int digit = (unsigned int)(c - '0');

		if (result > (unsigned int)INT_MAX / 10 ||
		    (result == (unsigned int)INT_MAX / 10 && digit > (unsigned int)INT_MAX % 10)) {
			fprintf(stderr, _("wrlib: ASCII decimal integer in PPM file \"%s\" is too large to be processed\n"),
				reader->filename);
			return -1;
		}
		result = result * 10 + digit;
		c = pm_read_byte(reader);
	} while (c >= '0' && c <= '9');

	pm_unread_byte(reader, c);
	*value = (int)result;
	return 0;
}

/* consume the single whitespace character before a binary raster */
static int pm_read_separator(PmReader *reader)
{
	int c = pm_read_byte(reader);

	if (c == EOF || !pm_is_space(c))
		return -1;
	if (c == '\r') {
		int next = pm_read_byte(reader);

		if (next != '\n')
			pm_unread_byte(reader, next);
	}
	return 0;
}

/* P2/P3 ASCII graymap and pixmap */
static RImage *load_ascii_pnm(PmReader *reader, int w, int h, int max, int channels)
{
	RImage *image;
	unsigned char *ptr;
	int x, y, c, val;

	image = RCreateImage(w, h, 0);
	if (!image) {
		RErrorCode = RERR_NOMEMORY;
		return NULL;
	}

	ptr = image->data;
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			for (c = 0; c < channels; c++) {
				if (pm_read_uint(reader, &val) < 0 || val > max) {
					RErrorCode = RERR_BADIMAGEFILE;
					RReleaseImage(image);
					return NULL;
				}
				val = val * 255 / max;
				*(ptr++) = (unsigned char)val;
				if (channels == 1) {
					*(ptr++) = (unsigned char)val;
					*(ptr++) = (unsigned char)val;
				}
			}
		}
	}
	return image;
}

static int pm_read_pbm_pixel(PmReader *reader)
{
	int c;

	if (pm_skip_space(reader) < 0)
		return -1;

	c = pm_read_byte(reader);
	if (c != '0' && c != '1') {
		if (c != EOF)
			fprintf(stderr, _("wrlib: invalid digit in plain PBM file \"%s\"\n"), reader->filename);
		return -1;
	}
	return c - '0';
}

/* P1/P4: portable bitmap */
static RImage *load_bitmap(PmReader *reader, int w, int h, int type)
{
	RImage *image;
	int val;
	unsigned char *ptr;

	image = RCreateImage(w, h, 0);
	if (!image) {
		RErrorCode = RERR_NOMEMORY;
		return NULL;
	}

	ptr = image->data;
	if (type == '1') {
		int i;

		for (i = 0; i < w * h; i++) {
			val = pm_read_pbm_pixel(reader);
			if (val < 0) {
				RErrorCode = RERR_BADIMAGEFILE;
				RReleaseImage(image);
				return NULL;
			}
			val = val ? 0 : 255;
			*(ptr++) = (unsigned char)val;
			*(ptr++) = (unsigned char)val;
			*(ptr++) = (unsigned char)val;
		}
	} else {
		int x, y, bitshift = -1;
		int buf = 0;

		for (y = 0; y < h; y++) {
			bitshift = -1;
			for (x = 0; x < w; x++) {
				if (bitshift == -1) {
					buf = pm_read_byte(reader);
					if (buf == EOF) {
						RErrorCode = RERR_BADIMAGEFILE;
						RReleaseImage(image);
						return NULL;
					}
					bitshift = 7;
				}
				val = (buf >> bitshift) & 1;
				val = val ? 0 : 255;
				--bitshift;
				*(ptr++) = (unsigned char)val;
				*(ptr++) = (unsigned char)val;
				*(ptr++) = (unsigned char)val;
			}
		}
	}
	return image;
}

static int pm_read_sample(PmReader *reader, int bytes)
{
	int hi, lo;

	hi = pm_read_byte(reader);
	if (hi == EOF)
		return -1;
	if (bytes == 1)
		return hi;

	lo = pm_read_byte(reader);
	if (lo == EOF)
		return -1;
	return (hi << 8) | lo;
}

/* P5/P6 binary graymap and pixmap, samples stored most-significant byte first */
static RImage *load_binary_pnm(PmReader *reader, int w, int h, int max, int channels)
{
	RImage *image;
	unsigned char *ptr;
	int x, y, c, val;
	int sample_bytes = (max > 255) ? 2 : 1;

	image = RCreateImage(w, h, 0);
	if (!image) {
		RErrorCode = RERR_NOMEMORY;
		return NULL;
	}

	ptr = image->data;
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			if (channels == 1) {
				val = pm_read_sample(reader, sample_bytes);
				if (val < 0 || val > max) {
					RErrorCode = RERR_BADIMAGEFILE;
					RReleaseImage(image);
					return NULL;
				}
				val = val * 255 / max;
				*(ptr++) = (unsigned char)val;
				*(ptr++) = (unsigned char)val;
				*(ptr++) = (unsigned char)val;
			} else {
				for (c = 0; c < 3; c++) {
					val = pm_read_sample(reader, sample_bytes);
					if (val < 0 || val > max) {
						RErrorCode = RERR_BADIMAGEFILE;
						RReleaseImage(image);
						return NULL;
					}
					*(ptr++) = (unsigned char)(val * 255 / max);
				}
			}
		}
	}
	return image;
}

RImage *RLoadPPM(const char *file_name)
{
	FILE *file;
	RImage *image = NULL;
	PmReader reader;
	int w, h, max, type, c;

	file = fopen(file_name, "rb");
	if (!file) {
		RErrorCode = RERR_OPEN;
		return NULL;
	}

	reader.file = file;
	reader.filename = file_name;
	reader.pushback = 0;
	reader.has_pushback = 0;

	c = pm_read_byte(&reader);
	if (c != 'P') {
		RErrorCode = RERR_BADFORMAT;
		goto done;
	}

	c = pm_read_byte(&reader);
	if (c < '1' || c > '6') {
		RErrorCode = RERR_BADFORMAT;
		goto done;
	}
	type = c;

	if (pm_read_uint(&reader, &w) < 0 || pm_read_uint(&reader, &h) < 0 ||
	    w < 1 || h < 1 ||
	    !RImageLoadWorkingSetWithinLimits((uint64_t)w, (uint64_t)h, 3U)) {
		RErrorCode = RERR_BADIMAGEFILE;
		goto done;
	}

	if (type == '2' || type == '3' || type == '5' || type == '6') {
		if (pm_read_uint(&reader, &max) < 0 || max < 1 || max > 65535) {
			RErrorCode = RERR_BADIMAGEFILE;
			goto done;
		}
	} else {
		max = 1;
	}

	if (type == '4' || type == '5' || type == '6') {
		if (pm_read_separator(&reader) < 0) {
			RErrorCode = RERR_BADIMAGEFILE;
			goto done;
		}
	}

	if (type == '1' || type == '4') {
		image = load_bitmap(&reader, w, h, type);
	} else if (type == '2') {
		image = load_ascii_pnm(&reader, w, h, max, 1);
	} else if (type == '3') {
		image = load_ascii_pnm(&reader, w, h, max, 3);
	} else if (type == '5') {
		image = load_binary_pnm(&reader, w, h, max, 1);
	} else {
		image = load_binary_pnm(&reader, w, h, max, 3);
	}

done:
	fclose(file);
	return image;
}
