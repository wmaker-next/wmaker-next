/* ppm.c - load PPM image from file
 *
 * Raster graphics library
 *
 * Copyright (c) 1997-2003 Alfredo K. Kojima
 * Copyright (c) 2013-2026 Window Maker Team
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


/*
 * fileio.c - routines to read elements based on Netpbm
 *
 * Copyright (C) 1988 by Jef Poskanzer.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  This software is provided "as is" without express or
 * implied warranty.
 */

static char pm_getc(FILE *const fileP, const char *filename)
{
	int ich;
	char ch;

	ich = getc(fileP);
	if (ich == EOF)
		fprintf(stderr, _("wrlib: EOF / read error reading a byte from PPM file \"%s\"\n"), filename);
	ch = (char)ich;

	if (ch == '#') {
		do {
			ich = getc(fileP);
			if (ich == EOF) {
				fprintf(stderr, _("wrlib: EOF / read error reading a byte from PPM file \"%s\"\n"), filename);
				return '\n';
			}
			ch = (char)ich;
		} while (ch != '\n' && ch != '\r');
	}
	return ch;
}

static unsigned char pm_getrawbyte(FILE *const file, const char *filename)
{
	int iby;

	iby = getc(file);
	if (iby == EOF)
		fprintf(stderr, _("wrlib: EOF / read error reading a byte from PPM file \"%s\"\n"), filename);
	return (unsigned char)iby;
}

static int pm_getuint(FILE *const ifP, const char *filename)
{
	char ch;
	unsigned int i;

	do {
		ch = pm_getc(ifP, filename);
	} while (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r');

	if (ch < '0' || ch > '9') {
		fprintf(stderr, _("wrlib: junk in PPM file \"%s\", expected an unsigned integer but got 0x%02X\n"), filename, (unsigned int)ch);
		return -1;
	}

	i = 0;
	do {
		unsigned int const digitVal = ch - '0';

		if (i > INT_MAX / 10) {
			fprintf(stderr, _("wrlib: ASCII decimal integer in PPM file \"%s\" is too large to be processed\n"), filename);
			return -1;
		}

		i *= 10;

		if (i > INT_MAX - digitVal) {
			fprintf(stderr, _("wrlib: ASCII decimal integer in PPM file \"%s\" is too large to be processed\n"), filename);
			return -1;
		}

		i += digitVal;

		ch = pm_getc(ifP, filename);
	} while (ch >= '0' && ch <= '9');

	return i;
}
/* end of fileio.c re-used code */
/******************************************************************************************/

/* P2/P3 ASCII graymap and pixmap. The binary P5/P6 variants are routed
 * to stb_image from RLoadPPM() so only the plain encoding is handled
 * here. */
static RImage *load_ascii_pnm(FILE *file, int w, int h, int max, int channels,
			      const char *filename)
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
				val = pm_getuint(file, filename);
				if (val > max || val < 0) {
					RErrorCode = RERR_BADIMAGEFILE;
					RReleaseImage(image);
					return NULL;
				}
				val = val * 255 / max;
				*(ptr++) = val;
				if (channels == 1) {
					*(ptr++) = val;
					*(ptr++) = val;
				}
			}
		}
	}
	return image;
}

/* PBM: support for portable bitmap ascii and binary encoding */
static RImage *load_bitmap(FILE *file, int w, int h, int max, int raw, const char *filename)
{
	RImage *image;
	int val;
	unsigned char *ptr;

	if (raw != '1' && raw != '4') {
		RErrorCode = RERR_BADFORMAT;
		return NULL;
	}

	image = RCreateImage(w, h, 0);
	if (!image) {
		RErrorCode = RERR_NOMEMORY;
		return NULL;
	}

	ptr = image->data;
	if (raw == '1') {
		int i = 0;

		while (i < w * h) {
			val = pm_getuint(file, filename);

			if (val > max || val < 0) {
				RErrorCode = RERR_BADIMAGEFILE;
				RReleaseImage(image);
				return NULL;
			}

			val = (val == 0) ? 255 : 0;
			*(ptr++) = val;
			*(ptr++) = val;
			*(ptr++) = val;
			i++;
		}
	} else {
		if (raw == '4') {
			unsigned char buf;
			int bitshift;
			int x, y;

			for (y = 0; y < h; y++) {
				bitshift = -1;
				for (x = 0; x < w; x++) {
					if (bitshift == -1) {
						buf = pm_getrawbyte(file, filename);
						bitshift = 7;
					}
					val = (buf >> bitshift) & 1;
					val = (val == 0) ? 255 : 0;
					--bitshift;
					*(ptr++) = val;
					*(ptr++) = val;
					*(ptr++) = val;
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
	char buffer[256];
	int w, h, m;
	int type;

	file = fopen(file_name, "rb");
	if (!file) {
		RErrorCode = RERR_OPEN;
		return NULL;
	}

	/* get signature */
	if (!fgets(buffer, 255, file)) {
		RErrorCode = RERR_BADIMAGEFILE;
		fclose(file);
		return NULL;
	}

	/* accept bitmaps, pixmaps or graymaps */
	if (buffer[0] != 'P' || (buffer[1] < '1' || buffer[1] > '6')) {
		RErrorCode = RERR_BADFORMAT;
		fclose(file);
		return NULL;
	}

	type = buffer[1];

	/* Binary graymap/pixmap (P5/P6) go through stb_image, which reads
	 * the pixel block in one fread instead of byte-at-a-time and is
	 * the reference PNM reader for the project. ASCII PNM (P1/P2/P3)
	 * and binary PBM (P4) are not supported by stb so the
	 * Netpbm-derived parser below handles those. */
	if (type == '5' || type == '6') {
		fclose(file);
		return RLoadStb(file_name);
	}

	/* skip comments */
	while (1) {
		if (!fgets(buffer, 255, file)) {
			RErrorCode = RERR_BADIMAGEFILE;
			fclose(file);
			return NULL;
		}

		if (buffer[0] != '#')
			break;
	}

	/* get size, header fields are ASCII decimal per the PNM spec, and a
	 * 9-digit field width keeps the conversion inside INT_MAX so the
	 * range checks below and in RCreateImage() see a defined value */
	if (sscanf(buffer, "%9d %9d", &w, &h) != 2 || w < 1 || h < 1) {
		/* Short file */
		RErrorCode = RERR_BADIMAGEFILE;
		fclose(file);
		return NULL;
	}

	if (type == '2' || type == '3') {
		if (!fgets(buffer, 255, file)) {
			RErrorCode = RERR_BADIMAGEFILE;
			fclose(file);
			return NULL;
		}
		/* get max value */
		if (sscanf(buffer, "%9d", &m) != 1 || m < 1 || m > 255) {
			RErrorCode = RERR_BADIMAGEFILE;
			fclose(file);
			return NULL;
		}
	} else {
		m = 1;
	}

	if (type == '1' || type == '4') {
		/* Portable Bit Map: P1 is for 'plain' (ascii, rare), P4 for 'regular' (binary) */
		image = load_bitmap(file, w, h, m, type, file_name);
	} else if (type == '2') {
		image = load_ascii_pnm(file, w, h, m, 1, file_name);
	} else if (type == '3') {
		image = load_ascii_pnm(file, w, h, m, 3, file_name);
	}

	fclose(file);
	return image;
}
