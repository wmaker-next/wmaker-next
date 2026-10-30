/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2009-2026 Window Maker Team
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

#include "wconfig.h"


#include <wraster.h>

#include <stdlib.h>
#include <string.h>
#include "WindowMaker.h"
#include <WINGs/WXProp.h>
#include "pixmap.h"

/*
 *----------------------------------------------------------------------
 * wPixmapCreateFromXPMData--
 * 	Creates a WPixmap structure and initializes it with the supplied
 * XPM structure data.
 *
 * Returns:
 * 	A WPixmap structure or NULL on failure.
 *
 * Notes:
 * 	DEF_XPM_CLOSENESS specifies the XpmCloseness
 *----------------------------------------------------------------------
 */
WPixmap *wPixmapCreateFromXPMData(WScreen * scr, char **data)
{
	RImage *image;
	WPixmap *pix;

	image = RGetImageFromXPMData(scr->rcontext, data);
	if (!image)
		return NULL;

	pix = wmalloc(sizeof(WPixmap));

	RConvertImageMask(scr->rcontext, image, &pix->image, &pix->mask, 128);

	pix->width = image->width;
	pix->height = image->height;
	pix->depth = scr->w_depth;

	RReleaseImage(image);

	return pix;
}

/*
 *----------------------------------------------------------------------
 * wPixmapCreateFromXBMData--
 * 	Creates a WPixmap structure and initializes it with the supplied
 * XBM structure data, size and mask.
 *
 * Returns:
 * 	A WPixmap structure or NULL on failure.
 *
 *----------------------------------------------------------------------
 */
WPixmap *wPixmapCreateFromXBMData(WScreen * scr, char *data, char *mask,
				  int width, int height, unsigned long fg, unsigned long bg)
{
	WPixmap *pix;

	pix = wmalloc(sizeof(WPixmap));
	pix->image = wXCreatePixmapFromBitmapData(wdisp->conn, scr->w_win, data, width, height, fg, bg, scr->w_depth);
	if (pix->image == XCB_NONE) {
		wfree(pix);
		return NULL;
	}
	if (mask) {
		pix->mask = wXCreateBitmapFromData(wdisp->conn, scr->w_win, mask, width, height);
	} else {
		pix->mask = XCB_NONE;
	}
	pix->width = width;
	pix->height = height;
	pix->depth = scr->w_depth;
	return pix;
}

WPixmap *wPixmapCreate(xcb_pixmap_t image, xcb_pixmap_t mask)
{
	WPixmap *pix;
	WXGeometry geom;

	pix = wmalloc(sizeof(WPixmap));
	pix->image = image;
	pix->mask = mask;
	if (!WXRcvGeometry(wdisp->conn, WXReqGeometry(wdisp->conn, (xcb_window_t) image), &geom)) {
		wwarning("wXGetGeometry() failed during wPixmapCreate()");
		wfree(pix);
		return NULL;
	}
	pix->width = geom.width;
	pix->height = geom.height;
	pix->depth = geom.depth;
	return pix;
}

/*
 *----------------------------------------------------------------------
 * wPixmapDestroy--
 * 	Destroys a WPixmap structure and the pixmap/mask it holds.
 *
 * Returns:
 * 	None
 *----------------------------------------------------------------------
 */
void wPixmapDestroy(WPixmap * pix)
{
	if (!pix->shared) {
		if (pix->mask && !pix->client_owned_mask) {
			xcb_free_pixmap(wdisp->conn, pix->mask);
		}

		if (pix->image && !pix->client_owned) {
			xcb_free_pixmap(wdisp->conn, pix->image);
		}
	}
	wfree(pix);
}
