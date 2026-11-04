/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2010-2026 Window Maker Team
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
#ifndef WMPIXMAP_H_
#define WMPIXMAP_H_

#include "screen.h"

enum {
	WBTN_SVG_NONE = 0,
	WBTN_SVG_CLOSE,
	WBTN_SVG_BROKEN_CLOSE,
	WBTN_SVG_KILL,
	WBTN_SVG_ICONIFY,
	WBTN_SVG_MAXIMIZE,
	WBTN_SVG_RESTORE
};

typedef struct WPixmap {
    xcb_pixmap_t image;	       /* icon image */
    xcb_pixmap_t mask;	       /* icon mask */
    int width, height;		       /* size of pixmap */
    int depth;			       /* depth of pixmap */
    unsigned int shared:1;	       /* if pixmaps should be kept
                                        * when structure is freed */
    unsigned int client_owned:1;
    unsigned int client_owned_mask:1;
	unsigned int themed:1;	       /* loaded from a theme file, blit
					* with XCopyArea so its own colours
					* survive instead of being filled
					* through the mask in title_color */
	int svg_glyph;		       /* WBTN_SVG_* source, 0 for a pixmap */
	unsigned int svg_next:1;	       /* render the NEXT style backplate */
} WPixmap;


WPixmap *wPixmapCreate(xcb_pixmap_t image, xcb_pixmap_t mask);

WPixmap *wPixmapCreateFromXPMData(WScreen *scr, char **data);

WPixmap *wPixmapCreateFromXBMData(WScreen *scr, char *data, char *mask,
                                  int width, int height, unsigned long fg,
                                  unsigned long bg);
void wPixmapDestroy(WPixmap *pix);

#ifdef USE_SVG
extern WPixmap *wButtonGlyphGet(struct _WScreen *scr, const struct WPixmap *src, int size);
void wButtonGlyphCacheDestroy(struct _WScreen *scr);
#endif

#endif
