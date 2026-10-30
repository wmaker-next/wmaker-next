/*
 *  testrot.c
 *
 *  Raster graphics library
 *
 *  Copyright (c) 2026 Window Maker Team
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

#include <xcb/xcb.h>
#include "wraster.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "tile.xpm"
xcb_connection_t *xc;
xcb_window_t win;
RContext *ctx;
RImage *img;
xcb_pixmap_t pix;

#define MAX(a,b) (a)>(b) ? (a) : (b)

int main(int argc, char **argv)
{
	RContextAttributes attr;
	float a;

	int sn;

	xc = xcb_connect(NULL, &sn);
	if (xcb_connection_has_error(xc)) {
		puts("can't open display");
		exit(1);
	}

	attr.flags = 0;
	ctx = RCreateContextXCB(xc, sn, &attr);

	if (argc < 2)
		img = RGetImageFromXPMData(ctx, image_name);
	else
		img = RLoadImage(ctx, argv[1], 0);

	if (!img) {
		puts(RMessageForError(RErrorCode));
		exit(1);
	}
	win = xcb_generate_id(xc);
	xcb_create_window(xc, XCB_COPY_FROM_PARENT, win,
			  xcb_setup_roots_iterator(xcb_get_setup(xc)).data->root,
			  10, 10, MAX(img->width, img->height), MAX(img->height, img->width),
			  0, XCB_WINDOW_CLASS_COPY_FROM_PARENT, XCB_COPY_FROM_PARENT,
			  0, NULL);
	xcb_configure_window(xc, win, XCB_CONFIG_WINDOW_STACK_MODE,
			     (uint32_t[]) { XCB_STACK_MODE_ABOVE });
	xcb_map_window(xc, win);
	xcb_flush(xc);

	a = 0;
	while (1) {
		RImage *tmp;

		a = a + 1.0;

		tmp = RRotateImage(img, a);
		if (!RConvertImage(ctx, tmp, &pix)) {
			puts(RMessageForError(RErrorCode));
			exit(1);
		}
		RReleaseImage(tmp);

		xcb_change_window_attributes(xc, win, XCB_CW_BACK_PIXMAP,
					     (uint32_t[]) { pix });
		xcb_free_pixmap(xc, pix);
		xcb_clear_area(xc, 0, win, 0, 0, 0, 0);
		xcb_flush(xc);
		usleep(50000);
	}
	xcb_disconnect(xc);
	exit(0);
}
