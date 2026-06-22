/* xpm.c - load XPM image from file using libXpm
 *
 * Raster graphics library
 *
 * Copyright (c) 1997-2003 Alfredo K. Kojima
 * Copyright (c) 2014-2021 Window Maker Team
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
#include <ctype.h>
#include <X11/xpm.h>

#include "wraster.h"
#include "imgformat.h"
#include "wr_i18n.h"


/*
 * Parse the #rgb / #rrggbb / #rrrrggggbbbb forms locally, those cover the
 * vast majority of libXpm colour entries.  Anything else (named colours)
 * goes to the server via xcb_lookup_color so the colortable still resolves
 * "white"/"black"/etc without a libX11 Display*.
 */
static int xpm_parse_color(RContext *context, const char *spec,
			   unsigned char *r, unsigned char *g, unsigned char *b)
{
	xcb_lookup_color_reply_t *reply;
	size_t len;

	if (spec[0] == '#') {
		unsigned int v[12], n;

		for (n = 0; n < 12 && isxdigit((unsigned char)spec[n + 1]); n++)
			v[n] = isdigit((unsigned char)spec[n + 1]) ? spec[n + 1] - '0'
				: (tolower((unsigned char)spec[n + 1]) - 'a' + 10);
		if (n == 3) {
			*r = v[0] * 0x11; *g = v[1] * 0x11; *b = v[2] * 0x11;
			return 1;
		}
		if (n == 6 || n == 9 || n == 12) {
			n /= 3;
			*r = (v[0] << 4) | v[1];
			*g = (v[n] << 4) | v[n + 1];
			*b = (v[2 * n] << 4) | v[2 * n + 1];
			return 1;
		}
		return 0;
	}

	len = strlen(spec);
	reply = xcb_lookup_color_reply(context->xc,
			xcb_lookup_color(context->xc, context->cmap, len, spec), NULL);
	if (!reply)
		return 0;
	*r = reply->exact_red >> 8;
	*g = reply->exact_green >> 8;
	*b = reply->exact_blue >> 8;
	free(reply);
	return 1;
}

static RImage *create_rimage_from_xpm(RContext *context, XpmImage xpm)
{
	RImage *image;
	unsigned char *color_table[4];
	unsigned char *data;
	int i;
	int *p;

	if (xpm.height < 1 || xpm.width < 1) {
		RErrorCode = RERR_BADIMAGEFILE;
		return NULL;
	}

	if (xpm.colorTable == NULL) {
		RErrorCode = RERR_BADIMAGEFILE;
		return NULL;
	}
	image = RCreateImage(xpm.width, xpm.height, 1);
	if (!image)
		return NULL;

	/* make color table */
	for (i = 0; i < 4; i++) {
		color_table[i] = malloc(xpm.ncolors * sizeof(unsigned char));
		if (!color_table[i]) {
			for (i = i - 1; i >= 0; i--) {
				if (color_table[i])
					free(color_table[i]);
			}
			RReleaseImage(image);
			RErrorCode = RERR_NOMEMORY;
			return NULL;
		}
	}

	for (i = 0; i < xpm.ncolors; i++) {
		unsigned char cr, cg, cb;
		char *color = NULL;

		if (xpm.colorTable[i].c_color)
			color = xpm.colorTable[i].c_color;
		else if (xpm.colorTable[i].g_color)
			color = xpm.colorTable[i].g_color;
		else if (xpm.colorTable[i].g4_color)
			color = xpm.colorTable[i].g4_color;
		else if (xpm.colorTable[i].m_color)
			color = xpm.colorTable[i].m_color;
		else if (xpm.colorTable[i].symbolic)
			color = xpm.colorTable[i].symbolic;

		if (!color) {
			color_table[0][i] = 0xbe;
			color_table[1][i] = 0xbe;
			color_table[2][i] = 0xbe;
			color_table[3][i] = 0xff;
			continue;
		}

		if (strncmp(color, "None", 4) == 0) {
			color_table[0][i] = 0;
			color_table[1][i] = 0;
			color_table[2][i] = 0;
			color_table[3][i] = 0;
			continue;
		}
		if (xpm_parse_color(context, color, &cr, &cg, &cb)) {
			color_table[0][i] = cr;
			color_table[1][i] = cg;
			color_table[2][i] = cb;
			color_table[3][i] = 0xff;
		} else {
			color_table[0][i] = 0xbe;
			color_table[1][i] = 0xbe;
			color_table[2][i] = 0xbe;
			color_table[3][i] = 0xff;
		}
	}
	/* convert pixmap to RImage */
	p = (int *)xpm.data;
	data = image->data;
	for (i = 0; i < xpm.width * xpm.height; i++, p++) {
		*(data++) = color_table[0][*p];
		*(data++) = color_table[1][*p];
		*(data++) = color_table[2][*p];
		*(data++) = color_table[3][*p];
	}
	for (i = 0; i < 4; i++)
		free(color_table[i]);
	return image;
}

static int is_xpm_error(int status)
{
	if (status == XpmSuccess)
		return 0;

	switch (status) {
	case XpmOpenFailed:
		RErrorCode = RERR_OPEN;
		break;
	case XpmFileInvalid:
		RErrorCode = RERR_BADIMAGEFILE;
		break;
	case XpmNoMemory:
		RErrorCode = RERR_NOMEMORY;
		break;
	default:
		RErrorCode = RERR_BADIMAGEFILE;
		break;
	}
	return 1;
}

RImage *RGetImageFromXPMData(RContext *context, char **xpmData)
{
	RImage *image;
	XpmImage xpm;
	int status;

	status = XpmCreateXpmImageFromData(xpmData, &xpm, (XpmInfo *) NULL);
	if (is_xpm_error(status))
		return NULL;

	image = create_rimage_from_xpm(context, xpm);
	XpmFreeXpmImage(&xpm);
	return image;
}

RImage *RLoadXPM(RContext *context, const char *file)
{
	RImage *image;
	XpmImage xpm;
	int status;

	status = XpmReadFileToXpmImage((char *)file, &xpm, (XpmInfo *) NULL);
	if (is_xpm_error(status))
		return NULL;

	image = create_rimage_from_xpm(context, xpm);
	XpmFreeXpmImage(&xpm);
	return image;
}
