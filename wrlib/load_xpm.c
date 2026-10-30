/* load_xpm.c - load XPM image without libXpm
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
#include <strings.h>
#include <ctype.h>

#include "wraster.h"
#include "imgformat.h"

/*
 * Standalone XPM reader. Files are parsed as XPM3 (quoted payload
 * strings); the in-memory RGetImageFromXPMData() entry point takes
 * the raw, unquoted payload lines of an embedded XPM array.
 *
 * Tolerated beyond the strict grammar:
 * - the signature comment, blank lines, and whole-line C comments
 *   anywhere in the file (multi-line comments included)
 * - leading whitespace before the quoted strings
 * - values-line hotspots ("w h cpp ncolors [x y]")
 * - colour entries of any key (c, g4, g, m), with c preferred over
 *   g4/g and m. Symbolic (s) entries are skipped
 * - colour values: #rgb / #rrggbb / #rrrgggbbb / #rrrrggggbbbb, %gray,
 *   None/transparent, and named colours resolved through the colour
 *   name table below, falling back to the X server when a context is
 *   available
 * - one to four characters per pixel
 *
 * File pixel rows must still be complete quoted strings, and every
 * colour entry must resolve. Unknown colour names fail the load
 * rather than rendering garbage.
 */

#define MAX_XPM_LINE	65536	/* hard cap on any single file line */
#define MAX_XPM_COLORS	65536	/* hard cap on colour-table size */
#define MAX_XPM_CPP	4	/* characters per pixel supported */

struct xpm_line_source {
	char **data;		/* in-memory: array of strings, NULL when reading from FILE */
	int data_index;		/* next index into data[] */
	int data_count;		/* total lines available in data[] (so we never read past) */
	FILE *file;		/* FILE source, NULL when reading from memory */
	char *buf;		/* getline buffer when reading from FILE */
	size_t bufcap;
};

static const char *xpm_next_line(struct xpm_line_source *src)
{
	if (src->file) {
		ssize_t len = getline(&src->buf, &src->bufcap, src->file);

		if (len < 0 || len > MAX_XPM_LINE)
			return NULL;
		return src->buf;
	}
	if (src->data_index >= src->data_count)
		return NULL;
	return src->data[src->data_index++];
}

/*
 * Fetch the next line that carries data. For FILE sources, blank lines
 * and comments are skipped, and when a comment closes before the end
 * of a line, the remainder of that line is returned so a payload
 * written after an inline comment is still seen. Embedded string
 * arrays are generated clean, so no skipping there.
 */
static const char *xpm_next_data_line(struct xpm_line_source *src)
{
	const char *row;

	if (!src->file)
		return xpm_next_line(src);

	while ((row = xpm_next_line(src)) != NULL) {
		const char *p = row;

		for (;;) {
			while (*p == ' ' || *p == '\t' || *p == '\r')
				p++;
			if (*p == '\n' || *p == '\0')
				break;
			if (p[0] == '/' && p[1] == '*') {
				p += 2;
				while (*p != '\0' && !(p[0] == '*' && p[1] == '/'))
					p++;
				if (*p == '\0') {
					while ((row = xpm_next_line(src)) != NULL) {
						p = row;
						while (*p != '\0' && !(p[0] == '*' && p[1] == '/'))
							p++;
						if (*p != '\0')
							break;
					}
					if (!row)
						return NULL;
					p += 2;
					continue;
				}
				p += 2;
				continue;
			}
			return p;
		}
	}
	return NULL;
}

/*
 * The quoted payload of a FILE line ("...", possibly followed by a
 * comma). In-memory lines are the raw payload.
 */
static const char *xpm_row_payload(struct xpm_line_source *src,
				   const char *row, size_t *plen)
{
	const char *first, *last;

	if (!src->file) {
		*plen = strlen(row);
		return row;
	}
	first = strchr(row, '"');
	last = first ? strchr(first + 1, '"') : NULL;
	if (!first || !last) {
		*plen = 0;
		return NULL;
	}
	*plen = (size_t)(last - first - 1);
	return first + 1;
}

/* Colour name fallback used when no context (or no match on the
 * server) is available. Lowercase names, matched case-insensitively. */
struct xpm_named_color {
	const char *name;
	unsigned char red, green, blue;
};

static const struct xpm_named_color xpm_color_names[] = {
	{ "black",	0x00, 0x00, 0x00 },
	{ "white",	0xff, 0xff, 0xff },
	{ "red",	0xff, 0x00, 0x00 },
	{ "green",	0x00, 0xff, 0x00 },
	{ "blue",	0x00, 0x00, 0xff },
	{ "yellow",	0xff, 0xff, 0x00 },
	{ "magenta",	0xff, 0x00, 0xff },
	{ "cyan",	0x00, 0xff, 0xff },
	{ "aqua",	0x00, 0xff, 0xff },
	{ "fuchsia",	0xff, 0x00, 0xff },
	{ "gray",	0xbe, 0xbe, 0xbe },
	{ "grey",	0xbe, 0xbe, 0xbe },
	{ "silver",	0xc0, 0xc0, 0xc0 },
	{ "maroon",	0x80, 0x00, 0x00 },
	{ "navy",	0x00, 0x00, 0x80 },
	{ "olive",	0x80, 0x80, 0x00 },
	{ "purple",	0x80, 0x00, 0x80 },
	{ "teal",	0x00, 0x80, 0x80 },
	{ "lime",	0x00, 0xff, 0x00 },
	{ "orange",	0xff, 0xa5, 0x00 },
	{ "brown",	0xa5, 0x2a, 0x2a },
	{ "pink",	0xff, 0xc0, 0xcb },
	{ "gold",	0xff, 0xd7, 0x00 },
	{ "violet",	0xee, 0x82, 0xee },
	{ "indigo",	0x4b, 0x00, 0x82 },
	{ "coral",	0xff, 0x7f, 0x50 },
	{ "salmon",	0xfa, 0x80, 0x72 },
	{ "khaki",	0xf0, 0xe6, 0x8c },
	{ "wheat",	0xf5, 0xde, 0xb3 },
	{ "tan",	0xd2, 0xb4, 0x8c },
	{ "beige",	0xf5, 0xf5, 0xdc },
	{ "ivory",	0xff, 0xff, 0xf0 },
	{ "lavender",	0xe6, 0xe6, 0xfa },
	{ "plum",	0xdd, 0xa0, 0xdd },
	{ "orchid",	0xda, 0x70, 0xd6 },
	{ "turquoise",	0x40, 0xe0, 0xd0 },
	{ "chocolate",	0xd2, 0x69, 0x1e },
	{ "crimson",	0xdc, 0x14, 0x3c },
	{ "darkred",	0x8b, 0x00, 0x00 },
	{ "darkgreen",	0x00, 0x64, 0x00 },
	{ "darkblue",	0x00, 0x00, 0x8b },
	{ "gainsboro",	0xdc, 0xdc, 0xdc },
	{ "whitesmoke",	0xf5, 0xf5, 0xf5 },
	{ "dimgray",	0x69, 0x69, 0x69 },
	{ "dimgrey",	0x69, 0x69, 0x69 },
	{ "darkgray",	0xa9, 0xa9, 0xa9 },
	{ "darkgrey",	0xa9, 0xa9, 0xa9 },
	{ "lightgray",	0xd3, 0xd3, 0xd3 },
	{ "lightgrey",	0xd3, 0xd3, 0xd3 },
	{ "lightblue",	0xad, 0xd8, 0xe6 },
	{ "lightpink",	0xff, 0xb6, 0xc1 },
	{ "lightgreen",	0x90, 0xee, 0x90 },
	{ "lightcyan",	0xe0, 0xff, 0xff },
	{ "lightyellow", 0xff, 0xff, 0xe0 },
	{ "gray10",	0x1a, 0x1a, 0x1a },
	{ "gray20",	0x33, 0x33, 0x33 },
	{ "gray30",	0x4d, 0x4d, 0x4d },
	{ "gray40",	0x66, 0x66, 0x66 },
	{ "gray50",	0x7f, 0x7f, 0x7f },
	{ "gray60",	0x99, 0x99, 0x99 },
	{ "gray70",	0xb3, 0xb3, 0xb3 },
	{ "gray80",	0xcc, 0xcc, 0xcc },
	{ "gray90",	0xe5, 0xe5, 0xe5 },
};

static int xpm_hex_color(const char *s, size_t len,
			 unsigned char *red, unsigned char *green,
			 unsigned char *blue)
{
	unsigned int v[12];
	size_t n;

	if (s[0] != '#' || len < 2 || len > 13)
		return 0;
	for (n = 0; n + 1 < len && n < 12 && isxdigit((unsigned char)s[n + 1]); n++)
		v[n] = isdigit((unsigned char)s[n + 1]) ? s[n + 1] - '0'
			: (tolower((unsigned char)s[n + 1]) - 'a' + 10);
	if (n + 1 != len)
		return 0;
	if (n == 3) {
		*red = v[0] * 0x11;
		*green = v[1] * 0x11;
		*blue = v[2] * 0x11;
		return 1;
	}
	if (n == 6 || n == 9 || n == 12) {
		n /= 3;
		*red = (v[0] << 4) | v[1];
		*green = (v[n] << 4) | v[n + 1];
		*blue = (v[2 * n] << 4) | v[2 * n + 1];
		return 1;
	}
	return 0;
}

static int xpm_gray_color(const char *s, size_t len,
			  unsigned char *red, unsigned char *green,
			  unsigned char *blue)
{
	char tmp[4];
	long pct;
	unsigned int v;

	if (s[0] != '%' || len < 2 || len > 4)
		return 0;
	memcpy(tmp, s + 1, len - 1);
	tmp[len - 1] = '\0';
	if (tmp[0] < '0' || tmp[0] > '9')
		return 0;
	pct = strtol(tmp, NULL, 10);
	if (pct < 0 || pct > 100)
		return 0;
	v = ((unsigned int)pct * 255) / 100;
	*red = v;
	*green = v;
	*blue = v;
	return 1;
}

static int xpm_named_color(const char *s, size_t len,
			   unsigned char *red, unsigned char *green,
			   unsigned char *blue)
{
	char tmp[32];
	size_t i;

	if (len == 0 || len >= sizeof(tmp))
		return 0;
	for (i = 0; i < len; i++)
		tmp[i] = tolower((unsigned char)s[i]);
	tmp[len] = '\0';
	for (i = 0; i < sizeof(xpm_color_names) / sizeof(xpm_color_names[0]); i++) {
		if (strcmp(tmp, xpm_color_names[i].name) == 0) {
			*red = xpm_color_names[i].red;
			*green = xpm_color_names[i].green;
			*blue = xpm_color_names[i].blue;
			return 1;
		}
	}
	return 0;
}

static int xpm_lookup_color(RContext *context, const char *s, size_t len,
			    unsigned char *red, unsigned char *green,
			    unsigned char *blue)
{
	xcb_lookup_color_reply_t *reply;

	if (!context || !context->xc || len == 0 || len > 255)
		return 0;
	reply = xcb_lookup_color_reply(context->xc,
			xcb_lookup_color(context->xc, context->cmap,
					 (unsigned int)len, s), NULL);
	if (!reply)
		return 0;
	*red = reply->exact_red >> 8;
	*green = reply->exact_green >> 8;
	*blue = reply->exact_blue >> 8;
	free(reply);
	return 1;
}

/*
 * Resolve one colour value token. Fills red/green/blue/alpha and
 * returns 1 on success.
 */
static int xpm_resolve_color(RContext *context, const char *s, size_t len,
			     unsigned char *red, unsigned char *green,
			     unsigned char *blue, unsigned char *alpha)
{
	*alpha = 255;
	if (len == 0)
		return 0;
	if (s[0] == '#')
		return xpm_hex_color(s, len, red, green, blue);
	if (s[0] == '%')
		return xpm_gray_color(s, len, red, green, blue);
	if ((len == 4 && strncasecmp(s, "none", 4) == 0)
	    || (len == 11 && strcasecmp(s, "transparent") == 0)) {
		*red = 0;
		*green = 0;
		*blue = 0;
		*alpha = 0;
		return 1;
	}
	if (xpm_named_color(s, len, red, green, blue))
		return 1;
	return xpm_lookup_color(context, s, len, red, green, blue);
}

/*
 * Walk the key/value token pairs that follow the colour symbol and
 * pick the value of the best entry: c, then g4/g, then m. Symbolic
 * (s) and unknown keys are consumed and ignored.
 */
static int xpm_pick_color_value(const char *p, size_t len,
				const char **val, size_t *vlen)
{
	size_t i = 0;
	int best = 0;
	const char *bval = NULL;
	size_t bvlen = 0;

	while (i < len) {
		size_t klen, nlen;
		int prio;

		while (i < len && (p[i] == ' ' || p[i] == '\t'))
			i++;
		if (i >= len)
			break;
		klen = 0;
		while (i + klen < len && p[i + klen] != ' ' && p[i + klen] != '\t')
			klen++;
		if (klen == 1 && p[i] == 'c')
			prio = 4;
		else if (klen == 1 && p[i] == 'g')
			prio = 3;
		else if (klen == 2 && p[i] == 'g' && p[i + 1] == '4')
			prio = 3;
		else if (klen == 1 && p[i] == 'm')
			prio = 2;
		else
			prio = 0;
		i += klen;
		while (i < len && (p[i] == ' ' || p[i] == '\t'))
			i++;
		nlen = 0;
		while (i + nlen < len && p[i + nlen] != ' ' && p[i + nlen] != '\t')
			nlen++;
		if (nlen == 0)
			return 0;
		if (prio > best) {
			best = prio;
			bval = p + i;
			bvlen = nlen;
		}
		i += nlen;
	}
	*val = bval;
	*vlen = bvlen;
	return best > 0;
}

/* Pixel symbols sorted once after the colour table is read, then
 * binary-searched per pixel. idx keeps the original table position so
 * duplicate symbols stay first-wins. */
struct xpm_symbol {
	unsigned int sym;
	unsigned int idx;
};

static int xpm_symbol_cmp(const void *a, const void *b)
{
	const struct xpm_symbol *pa = a;
	const struct xpm_symbol *pb = b;

	if (pa->sym != pb->sym)
		return pa->sym < pb->sym ? -1 : 1;
	return (pa->idx > pb->idx) - (pa->idx < pb->idx);
}

/* Lowest table index matching sym, or 0 when absent (as with the
 * previous linear scan, unmatched pixels take colour 0). */
static int xpm_symbol_lookup(const struct xpm_symbol *table, int count,
			     unsigned int sym)
{
	int lo = 0, hi = count;

	while (lo < hi) {
		int mid = lo + (hi - lo) / 2;

		if (table[mid].sym < sym)
			lo = mid + 1;
		else
			hi = mid;
	}
	return (lo < count && table[lo].sym == sym) ? (int)table[lo].idx : 0;
}

static void free_color_symbol_table(unsigned char *color_table[],
				    struct xpm_symbol *symbol_table)
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
 * Shared body for both entry points: validate the values line, build
 * the colour/symbol tables, allocate the RImage, and render the pixel
 * rows.
 */
static RImage *xpm_parse(struct xpm_line_source *src, int w, int h,
			 int ccount, int csize, RContext *context)
{
	RImage *image = NULL;
	unsigned char *color_table[4] = { NULL, NULL, NULL, NULL };
	struct xpm_symbol *symbol_table = NULL;
	unsigned char *r, *g, *b, *a;
	const char *row, *payload, *val;
	size_t plen, vlen;
	int i, j, k;
	int transp;
	unsigned int sym;

	if (csize < 1 || csize > MAX_XPM_CPP)
		goto bad_format;
	if (ccount < 1 || ccount > MAX_XPM_COLORS)
		goto bad_format;
	if (w < 1 || h < 1)
		goto bad_format;

	color_table[0] = malloc(ccount);
	color_table[1] = malloc(ccount);
	color_table[2] = malloc(ccount);
	color_table[3] = malloc(ccount);
	symbol_table = malloc(ccount * sizeof(*symbol_table));

	if (!color_table[0] || !color_table[1] || !color_table[2] || !color_table[3]
	    || !symbol_table) {
		RErrorCode = RERR_NOMEMORY;
		free_color_symbol_table(color_table, symbol_table);
		return NULL;
	}

	transp = 0;
	/* get color table */
	for (i = 0; i < ccount; i++) {
		row = xpm_next_data_line(src);
		if (!row)
			goto bad_format;
		payload = xpm_row_payload(src, row, &plen);
		if (!payload || plen < (size_t)csize)
			goto bad_format;

		sym = 0;
		for (j = 0; j < csize; j++)
			sym |= (unsigned int)(unsigned char)payload[j] << (8 * j);
		symbol_table[i].sym = sym;
		symbol_table[i].idx = i;

		if (!xpm_pick_color_value(payload + csize, plen - csize, &val, &vlen))
			goto bad_format;
		if (!xpm_resolve_color(context, val, vlen,
				       &color_table[0][i], &color_table[1][i],
				       &color_table[2][i], &color_table[3][i]))
			goto bad_format;
		if (color_table[3][i] == 0)
			transp = 1;
	}

	qsort(symbol_table, ccount, sizeof(*symbol_table), xpm_symbol_cmp);

	image = RCreateImage(w, h, transp);
	if (!image) {
		free_color_symbol_table(color_table, symbol_table);
		return NULL;
	}

	r = image->data;
	g = image->data + 1;
	b = image->data + 2;
	if (image->format == RRGBAFormat)
		a = image->data + 3;
	else
		a = NULL;

	for (i = 0; i < h; i++) {
		row = xpm_next_data_line(src);
		if (!row)
			goto bad_format;
		payload = xpm_row_payload(src, row, &plen);
		if (!payload || plen < (size_t)(w * csize))
			goto bad_format;

		for (j = 0; j < w; j++) {
			unsigned int color = 0;
			int l;

			for (l = 0; l < csize; l++)
				color |= (unsigned int)(unsigned char)payload[j * csize + l] << (8 * l);

			k = xpm_symbol_lookup(symbol_table, ccount, color);

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
	return image;

 bad_format:
	RErrorCode = RERR_BADIMAGEFILE;
	free_color_symbol_table(color_table, symbol_table);
	if (image)
		RReleaseImage(image);
	return NULL;
}

RImage *RGetImageFromXPMData(RContext *context, char **data)
{
	struct xpm_line_source src;
	int w, h, ccount, csize;

	/* mirror the caps xpm_parse() and RCreateImage() apply before
	 * 1 + ccount + h is computed, so near-INT_MAX values cannot
	 * overflow the line count */
	if (sscanf(data[0], "%i %i %i %i", &w, &h, &ccount, &csize) != 4
	    || w <= 0 || h <= 0 || ccount <= 0 || csize <= 0
	    || w > (int)R_IMAGE_MAX_WIDTH || h > (int)R_IMAGE_MAX_HEIGHT
	    || ccount > MAX_XPM_COLORS || csize > MAX_XPM_CPP) {
		RErrorCode = RERR_BADIMAGEFILE;
		return NULL;
	}

	src.data = data;
	src.data_index = 1;
	src.data_count = 1 + ccount + h;
	src.file = NULL;
	src.buf = NULL;
	src.bufcap = 0;

	return xpm_parse(&src, w, h, ccount, csize, context);
}

RImage *RLoadXPM(RContext *context, const char *file)
{
	RImage *image;
	struct xpm_line_source src;
	const char *row, *payload;
	size_t plen;
	int w, h, ccount, csize;
	FILE *f;

	f = fopen(file, "rb");
	if (!f) {
		RErrorCode = RERR_OPEN;
		return NULL;
	}

	src.data = NULL;
	src.data_index = 0;
	src.data_count = 0;
	src.file = f;
	src.buf = NULL;
	src.bufcap = 0;

	/* scan past signature comment, declaration and any further
	 * comments to the values line */
	for (;;) {
		int nw, nh, ncount, nsize;

		row = xpm_next_data_line(&src);
		if (!row)
			goto bad_file;
		payload = xpm_row_payload(&src, row, &plen);
		if (!payload)
			continue;
		if (sscanf(payload, "%i %i %i %i",
			   &nw, &nh, &ncount, &nsize) == 4
		    && nw > 0 && nh > 0 && ncount > 0 && nsize > 0) {
			w = nw;
			h = nh;
			ccount = ncount;
			csize = nsize;
			break;
		}
	}

	image = xpm_parse(&src, w, h, ccount, csize, context);
	free(src.buf);
	fclose(f);
	return image;

 bad_file:
	RErrorCode = RERR_BADIMAGEFILE;
	free(src.buf);
	fclose(f);
	return NULL;
}
