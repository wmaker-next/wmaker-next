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


#ifndef WMCORE_H_
#define WMCORE_H_

#include "screen.h"

/* Event mask applied to every child WCoreWindow (titlebar, resizebar,
 * titlebar buttons, menu body). Exposed so callers that want extra
 * events on a single core window can OR them in via XSelectInput
 * without duplicating the base set. */
#define WCORE_CHILD_EVENT_MASK  (XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE | \
				 XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | \
				 XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_EXPOSURE | \
				 XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW)

typedef struct WStacking {
	struct _WCoreWindow *above;
	struct _WCoreWindow *under;
	short window_level;
	struct _WCoreWindow *child_of;	/* owner for transient window */
} WStacking;

typedef struct _WCoreWindow {
	xcb_window_t window;
	int width;			/* size of the window */
	int height;
	WScreen *screen_ptr;		/* ptr to screen of the window */

	WObjDescriptor descriptor;
	WStacking *stacking;		/* window stacking information */
} WCoreWindow;

WCoreWindow *wCoreCreateTopLevel(WScreen *screen, int x, int y, int width,
				 int height, int bwidth,
				 int depth, xcb_visualtype_t *visual, xcb_colormap_t colormap,
				 WMPixel border_pixel);

WCoreWindow *wCoreCreate(WCoreWindow *parent, int x, int y,
			 int width, int height);

void wCoreDestroy(WCoreWindow *core);
void wCoreConfigure(WCoreWindow *core, int req_x, int req_y,
		    int req_w, int req_h);
#endif
