/* nxpm.c - load "normalized" XPM image
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include "wraster.h"
#include "imgformat.h"
#include "wr_i18n.h"


/*
 * Restricted support for XPM images.
 *
 * The images must be in the following "normalized" format:
 *
 *
 * line   	content
 * 1	        signature comment
 * 2		ignored ( normally "static char *xpm[] = {" )
 * 3		"width height color_count chars" where chars is 1 or 2
 * 4	        color definitions. Only c values with #rrggbb or #rrrrggggbbb
 *			format OR None
 * n		data
 *
 * - no comments or blank lines are allowed, except for the signature
 * - all lines must have at most 256 characters
 * - no white spaces allowed at left of each line
 */

#define LINEWIDTH 64


static void free_color_symbol_table(unsigned char *color_table[],
				     unsigned short *symbol_table)
{
	if (color_table[0])
		free(color_table[0]);
	if (color_table[1])
		free(color_table[1]);
	if (color_table[2])
		free(color_table[2]);
	if (color_table[3])
		free(color_table[3]);
	if (symbol_table)
		free(symbol_table);
}

/*
 * The shared color-table/pixel parser below is fed lines from one of two
 * sources: an in-memory char ** array (RGetImageFromXPMData) or a FILE
 * stream (RLoadXPM). xpm_next_line() abstracts that difference; the
 * struct carries whichever backing store is active.
 */
struct xpm_line_source {
	char **data;		/* in-memory: array of strings, NULL when reading from FILE */
	int data_index;		/* next index into data[] */
	int data_count;		/* total lines available in data[] (so we never read past) */
	FILE *file;		/* FILE source, NULL when reading from memory */
	char *buf;		/* fgets buffer when reading from FILE */
	int bufsize;
};

static const char *xpm_next_line(struct xpm_line_source *src)
{
	if (src->file) {
		if (!fgets(src->buf, src->bufsize, src->file))
			return NULL;
		return src->buf;
	}
	if (src->data_index >= src->data_count)
		return NULL;
	return src->data[src->data_index++];
}

/*
 * Shared body for both entry points: validate ccount/csize, build the
 * color/symbol tables, allocate the RImage, and render the pixel rows.
 *
 * The two source formats differ slightly inside each line: lines coming
 * from a FILE are wrapped in C string-literal quotes (so payload starts
 * at column 1 and is terminated by '"'), may be preceded by a one-line
 * comment, and are read into a fixed-size buffer; in-memory lines are
 * the raw payload starting at column 0 and NUL-terminated. Those small
 * divergences are keyed off `from_file` so the surrounding logic stays
 * single-sourced.
 */
static RImage *xpm_parse(struct xpm_line_source *src, int w, int h, int ccount, int csize)
{
	RImage *image = NULL;
	unsigned char *color_table[4] = { NULL, NULL, NULL, NULL };
	unsigned short *symbol_table = NULL;
	unsigned char *r, *g, *b, *a;
	const char *row;
	int from_file = (src->file != NULL);
	int i, j, k;
	int transp;
	unsigned short color;
	int bsize;
	unsigned int red, green, blue;
	char cline[LINEWIDTH + 1];
	char *buffer = NULL;

	if (csize != 1 && csize != 2)
		goto bad_format;

	if (ccount > 1 << (8 * csize))
		goto bad_format;

	color_table[0] = malloc(ccount);
	color_table[1] = malloc(ccount);
	color_table[2] = malloc(ccount);
	color_table[3] = malloc(ccount);
	symbol_table = malloc(ccount * sizeof(unsigned short));

	bsize = csize * w + 16;

	if (from_file)
		buffer = malloc(bsize);

	if (!color_table[0] || !color_table[1] || !color_table[2] || !color_table[3]
	    || !symbol_table || !bsize || (from_file && !buffer)) {
		RErrorCode = RERR_NOMEMORY;
		free_color_symbol_table(color_table, symbol_table);
		if (buffer)
			free(buffer);
		return NULL;
	}

	if (from_file) {
		/* color-definition lines were historically read through the
		 * fixed LINEWIDTH stack buffer, separate from the wider pixel
		 * row buffer; keep that sizing to preserve behavior */
		src->buf = cline;
		src->bufsize = LINEWIDTH;
	}

	transp = 0;
	/* get color table */
	for (i = 0; i < ccount; i++) {
		row = xpm_next_line(src);
		if (!row)
			goto bad_format;
		if (from_file && row[0] == '/') {
			row = xpm_next_line(src);
			if (!row)
				goto bad_format;
		}

		j = from_file ? 1 : 0;
		symbol_table[i] = row[j];
		if (csize == 2)
			symbol_table[i] |= row[j + 1] << 8;

		j += csize;
		while (row[j] != '#' && row[j] != 0 && row[j] != 'N'
		       && (!from_file || row[j] != '"'))
			j++;

		if (row[j] == '#') {
			k = 0;
			j++;
			while (row[j + k] != 0 && (!from_file || row[j + k] != '"'))
				k++;
			if (k == 6) {
				if (sscanf(&(row[j]), "%2x%2x%2x", &red, &green, &blue) != 3)
					goto bad_format;
			} else if (k == 12) {
				if (sscanf(&(row[j]), "%4x%4x%4x", &red, &green, &blue) != 3)
					goto bad_format;
				red >>= 8;
				green >>= 8;
				blue >>= 8;
			} else
				goto bad_format;

			color_table[0][i] = red;
			color_table[1][i] = green;
			color_table[2][i] = blue;
			color_table[3][i] = 255;
		} else if (strncmp(&(row[j]), "None", 4) == 0 || strncmp(&(row[j]), "none", 4) == 0) {
			color_table[0][i] = 0;
			color_table[1][i] = 0;
			color_table[2][i] = 0;
			color_table[3][i] = 0;
			transp = 1;
		} else {
			goto bad_format;
		}
	}

	image = RCreateImage(w, h, transp);
	if (!image) {
		free_color_symbol_table(color_table, symbol_table);
		if (buffer)
			free(buffer);
		return NULL;
	}

	r = image->data;
	g = image->data + 1;
	b = image->data + 2;
	if (image->format == RRGBAFormat)
		a = image->data + 3;
	else
		a = NULL;

	if (from_file) {
		src->buf = buffer;
		src->bufsize = bsize;
	}

	for (i = 0; i < h; i++) {
		row = xpm_next_line(src);
		if (!row)
			goto bad_format;
		if (from_file && row[0] == '/') {
			row = xpm_next_line(src);
			if (!row)
				goto bad_format;
		}
		if (from_file)
			row++;	/* step past the opening '"' */

		for (j = 0; j < w; j++) {
			if (from_file) {
				if (csize == 1) {
					color = row[j];
				} else {
					color = row[2 * j];
					color |= row[2 * j + 1] << 8;
				}
			} else if (csize == 1) {
				color = *row;
				if (color != '\0')
					row++;
			} else {
				if (row[0] != '\0' && row[1] != '\0') {
					color = row[0] | row[1];
					row += 2;
				} else {
					color = 0;
				}
			}

			for (k = 0; k < ccount; k++) {
				if (symbol_table[k] == color)
					break;
			}
			if (k == ccount)
				k = 0;

			*r = color_table[0][k];
			*g = color_table[1][k];
			*b = color_table[2][k];
			if (a) {
				*a = color_table[3][k];
				r += 4;
				g += 4;
				b += 4;
				a += 4;
			} else {
				r += 3;
				g += 3;
				b += 3;
			}
		}
	}

	free_color_symbol_table(color_table, symbol_table);
	if (buffer)
		free(buffer);
	return image;

 bad_format:
	RErrorCode = RERR_BADIMAGEFILE;
	free_color_symbol_table(color_table, symbol_table);
	if (buffer)
		free(buffer);
	if (image)
		RReleaseImage(image);
	return NULL;
}

RImage *RGetImageFromXPMData(RContext * context, char **data)
{
	struct xpm_line_source src;
	int w, h, ccount, csize;

	/*
	 * When using libXpm we need the context argument but the code here does
	 * not, so tell the compiler to not warn about it
	 */
	(void) context;

	if (sscanf(data[0], "%i %i %i %i", &w, &h, &ccount, &csize) != 4
	    || w <= 0 || h <= 0 || ccount <= 0 || csize <= 0) {
		RErrorCode = RERR_BADIMAGEFILE;
		return NULL;
	}

	src.data = data;
	src.data_index = 1;
	src.data_count = 1 + ccount + h;
	src.file = NULL;
	src.buf = NULL;
	src.bufsize = 0;

	return xpm_parse(&src, w, h, ccount, csize);
}

RImage *RLoadXPM(RContext * context, const char *file)
{
	RImage *image;
	struct xpm_line_source src;
	char line[LINEWIDTH + 1];
	int w, h, ccount, csize;
	FILE *f;

	/*
	 * When using libXpm we need the context argument but the code here does
	 * not, so tell the compiler to not warn about it
	 */
	(void) context;

	f = fopen(file, "rb");
	if (!f) {
		RErrorCode = RERR_OPEN;
		return NULL;
	}
	/* sig */
	if (!fgets(line, LINEWIDTH, f))
		goto bad_file;
	/* declaration */
	if (!fgets(line, LINEWIDTH, f))
		goto bad_file;

	/* data */
	if (!fgets(line, LINEWIDTH, f))
		goto bad_file;

	if (line[0] == '/')
		if (!fgets(line, LINEWIDTH, f))
			goto bad_file;

	if (sscanf(line, "\"%i %i %i %i\"", &w, &h, &ccount, &csize) != 4
	    || w <= 0 || h <= 0 || ccount <= 0 || csize <= 0)
		goto bad_file;

	src.data = NULL;
	src.data_index = 0;
	src.data_count = 0;
	src.file = f;
	src.buf = NULL;
	src.bufsize = 0;

	image = xpm_parse(&src, w, h, ccount, csize);
	fclose(f);
	return image;

 bad_file:
	RErrorCode = RERR_BADIMAGEFILE;
	fclose(f);
	return NULL;
}
