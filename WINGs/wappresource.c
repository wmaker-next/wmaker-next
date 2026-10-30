/*
 *  wappresource.c
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 1999-2004 Dan Pascu
 *  Copyright (c) 2000 Alfredo K. Kojima
 *  Copyright (c) 2009-2026 Window Maker Team
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

#include <unistd.h>

#include "WINGsP.h"


#include "GNUstep.h"


void WMSetApplicationIconWindow(WMScreen * scr, xcb_window_t window)
{
	scr->applicationIconWindow = window;

	if (scr->groupLeader) {
		WXWMHints *hints;

		hints = WXGetWMHints(scr->display, scr->groupLeader);
		hints->flags |= XCB_ICCCM_WM_HINT_ICON_WINDOW;
		hints->icon_window = window;

		WXSetWMHints(scr->display, scr->groupLeader, hints);
		wfree(hints);
	}
}

void WMSetApplicationIconImage(WMScreen * scr, RImage * image)
{
	WMPixmap *icon;

	if (scr->applicationIconImage == image)
		return;

	if (scr->applicationIconImage)
		RReleaseImage(scr->applicationIconImage);

	scr->applicationIconImage = RRetainImage(image);

	/* TODO: check whether we should set the pixmap only if there's none yet */
	if (image != NULL && (icon = WMCreatePixmapFromRImage(scr, image, 128)) != NULL) {
		WMSetApplicationIconPixmap(scr, icon);
		WMReleasePixmap(icon);
	}
}

RImage *WMGetApplicationIconImage(WMScreen * scr)
{
	return scr->applicationIconImage;
}

void WMSetApplicationIconPixmap(WMScreen * scr, WMPixmap * icon)
{
	if (scr->applicationIconPixmap == icon)
		return;

	if (scr->applicationIconPixmap)
		WMReleasePixmap(scr->applicationIconPixmap);

	scr->applicationIconPixmap = WMRetainPixmap(icon);

	if (scr->groupLeader) {
		WXWMHints *hints;

		hints = WXGetWMHints(scr->display, scr->groupLeader);
		hints->flags |= XCB_ICCCM_WM_HINT_ICON_PIXMAP | XCB_ICCCM_WM_HINT_ICON_MASK;
		hints->icon_pixmap = (icon != NULL ? icon->pixmap : XCB_NONE);
		hints->icon_mask = (icon != NULL ? icon->mask : XCB_NONE);

		WXSetWMHints(scr->display, scr->groupLeader, hints);
		wfree(hints);
	}
}

WMPixmap *WMGetApplicationIconPixmap(WMScreen * scr)
{
	return scr->applicationIconPixmap;
}

WMPixmap *WMCreateApplicationIconBlendedPixmap(WMScreen * scr, const RColor * color)
{
	WMPixmap *pix;

	if (scr->applicationIconImage) {
		RColor gray;

		if (!color) {
			WMColor *sysgray = WMGrayColor(scr);

			gray = WMGetRColorFromColor(sysgray);
			WMReleaseColor(sysgray);

			color = &gray;
		}

		pix = WMCreateBlendedPixmapFromRImage(scr, scr->applicationIconImage, color);
	} else {
		pix = NULL;
	}

	return pix;
}

void WMSetApplicationHasAppIcon(WMScreen * scr, bool flag)
{
	scr->aflags.hasAppIcon = ((flag == 0) ? 0 : 1);
}

void W_InitApplication(WMScreen * scr)
{
	xcb_window_t leader;
	WXWMHints *hints;

	leader = xcb_generate_id(scr->display);
	xcb_create_window(scr->display, XCB_COPY_FROM_PARENT, leader,
			  scr->rootWin, -1, -1, 1, 1, 0,
			  XCB_WINDOW_CLASS_COPY_FROM_PARENT, XCB_COPY_FROM_PARENT,
			  XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL, (const uint32_t[]){ 0, 0 });

	if (!scr->aflags.simpleApplication) {
		WXSetWMClass(scr->display, leader, "groupLeader", WMApplication.applicationName);

		WXSetCommand(scr->display, leader, WMApplication.argv, WMApplication.argc);

		hints = WXAllocWMHints();

		hints->flags = XCB_ICCCM_WM_HINT_WINDOW_GROUP;
		hints->window_group = leader;

		/* This code will never actually be reached, because to have
		 * scr->applicationIconPixmap set we need to have a screen first,
		 * but this function is called in the screen creation process.
		 * -Dan
		 */
		if (scr->applicationIconPixmap) {
			hints->flags |= XCB_ICCCM_WM_HINT_ICON_PIXMAP;
			hints->icon_pixmap = scr->applicationIconPixmap->pixmap;
			if (scr->applicationIconPixmap->mask) {
				hints->flags |= XCB_ICCCM_WM_HINT_ICON_MASK;
				hints->icon_mask = scr->applicationIconPixmap->mask;
			}
		}

		WXSetWMHints(scr->display, leader, hints);

		wfree(hints);
	}
	scr->groupLeader = leader;
}
