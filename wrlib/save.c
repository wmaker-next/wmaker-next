/* save.c - save image to file
 *
 * Raster graphics library
 *
 * Copyright (c) 1998-2003 Alfredo K. Kojima
 * Copyright (c) 2009-2026 Window Maker Team
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
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <strings.h>

#include "wraster.h"
#include "imgformat.h"
#include "wr_i18n.h"

int RSaveImage(RImage *image, const char *filename, const char *format)
{
	return RSaveTitledImage(image, filename, format, NULL);
}

int RSaveRawImage(RImage *image, const char *format, unsigned char **out_buf, size_t *out_size)
{
#if !defined(USE_PNG) && !defined(USE_JPEG)
	(void)image;
	(void)format;
	(void)out_buf;
	(void)out_size;
#endif
#ifdef USE_PNG
	if (strcasecmp(format, "PNG") == 0)
		return RSaveRawPNG(image, NULL, out_buf, out_size);
#endif

#ifdef USE_JPEG
	if (strcasecmp(format, "JPG") == 0)
		return RSaveRawJPEG(image, NULL, out_buf, out_size);

	if (strcasecmp(format, "JPEG") == 0)
		return RSaveRawJPEG(image, NULL, out_buf, out_size);
#endif

	RErrorCode = RERR_BADFORMAT;
	return 0;
}

int RSaveTitledImage(RImage *image, const char *filename, const char *format, char *title)
{
#if !defined(USE_PNG) && !defined(USE_JPEG)
	(void)title;
#endif
#ifdef USE_PNG
	if (strcasecmp(format, "PNG") == 0)
		return RSavePNG(image, filename, title);
#endif
#ifdef USE_JPEG
	if (strcasecmp(format, "JPG") == 0)
		return RSaveJPEG(image, filename, title);

	if (strcasecmp(format, "JPEG") == 0)
		return RSaveJPEG(image, filename, title);
#endif
	if (strcasecmp(format, "XPM") == 0)
		return RSaveXPM(image, filename);

	RErrorCode = RERR_BADFORMAT;
	return 0;
}
