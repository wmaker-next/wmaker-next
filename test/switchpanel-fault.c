/*
 *  switchpanel-fault.c
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wraster.h>

static int fail_once(const char *kind)
{
	const char *path = getenv("WMAKER_PANEL_FAULT_PATH");
	char value[32];
	FILE *f;

	if (!path)
		return 0;
	f = fopen(path, "r");
	if (!f)
		return 0;
	if (!fgets(value, sizeof(value), f)) {
		fclose(f);
		return 0;
	}
	fclose(f);
	value[strcspn(value, "\r\n")] = '\0';
	if (strcmp(value, kind) != 0)
		return 0;
	return unlink(path) == 0;
}

RImage *RCreateImage(unsigned int width, unsigned int height, int alpha)
{
	static RImage *(*real_create)(unsigned int, unsigned int, int);

	if (!real_create)
		real_create = dlsym(RTLD_NEXT, "RCreateImage");
	if (width == 77 && height == 77 && fail_once("tile"))
		return NULL;
	return real_create(width, height, alpha);
}

RImage *RSmoothScaleImage(RImage *image, unsigned int width, unsigned int height)
{
	static RImage *(*real_scale)(RImage *, unsigned int, unsigned int);

	if (!real_scale)
		real_scale = dlsym(RTLD_NEXT, "RSmoothScaleImage");
	if (fail_once("smooth"))
		return NULL;
	return real_scale(image, width, height);
}

int RConvertImage(RContext *context, RImage *image, xcb_pixmap_t *pixmap)
{
	static int (*real_convert)(RContext *, RImage *, xcb_pixmap_t *);

	if (!real_convert)
		real_convert = dlsym(RTLD_NEXT, "RConvertImage");
	if (image->width == 77 && image->height == 77 && fail_once("convert"))
		return 0;
	return real_convert(context, image, pixmap);
}

int RConvertImageMask(RContext *context, RImage *image, xcb_pixmap_t *pixmap,
		      xcb_pixmap_t *mask, int threshold)
{
	static int (*real_convert)(RContext *, RImage *, xcb_pixmap_t *, xcb_pixmap_t *, int);

	if (!real_convert)
		real_convert = dlsym(RTLD_NEXT, "RConvertImageMask");
	if (fail_once("mask"))
		return 0;
	return real_convert(context, image, pixmap, mask, threshold);
}

xcb_void_cookie_t xcb_create_window_checked(xcb_connection_t *connection, uint8_t depth,
					    xcb_window_t window, xcb_window_t parent,
					    int16_t x, int16_t y, uint16_t width, uint16_t height,
					    uint16_t border_width, uint16_t class,
					    xcb_visualid_t visual, uint32_t mask, const void *values)
{
	static xcb_void_cookie_t (*real_create)(xcb_connection_t *, uint8_t, xcb_window_t,
						xcb_window_t, int16_t, int16_t, uint16_t, uint16_t,
						uint16_t, uint16_t, xcb_visualid_t, uint32_t, const void *);

	if (!real_create)
		real_create = dlsym(RTLD_NEXT, "xcb_create_window_checked");
	if (fail_once("window") ||
	    (width == 77 && height == 77 && fail_once("windowchild")))
		depth = 255;
	return real_create(connection, depth, window, parent, x, y, width, height,
			   border_width, class, visual, mask, values);
}
