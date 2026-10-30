/*
 * Raster graphics library
 *
 * Copyright (c) 2014-2026 Window Maker Team
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
 *  License along with this library.
 */

#ifndef WRASTER_XUTIL_H
#define WRASTER_XUTIL_H


void RReleaseSharedSegment(RContext *context);

void r_put_ximage(RContext *context, xcb_drawable_t d, xcb_gcontext_t gc,
		  RXImage *rximg, int src_x, int src_y, int dest_x, int dest_y,
		  unsigned int width, unsigned int height);

RImage *r_unpack_ximage(RContext *context, xcb_image_t *ximg, xcb_image_t *mask);


#endif
