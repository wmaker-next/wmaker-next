/*
 * WindowMaker interface definitions
 *
 * Copyright (C) 1997-2003 Alfredo K. Kojima
 * Copyright (c) 2012-2026 Window Maker Team
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

#ifndef _WMLIB_H_
#define _WMLIB_H_

/* the definitions in this file can change at any time. WINGs has more
 * stable definitions */

#include <stdbool.h>
#include <stdint.h>
#include <xcb/xcb.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct {
	uint32_t flags;
	uint32_t window_style;
	uint32_t window_level;
	uint32_t reserved;
	xcb_pixmap_t miniaturize_pixmap;	/* pixmap for miniaturize button */
	xcb_pixmap_t close_pixmap;		/* pixmap for close button */
	xcb_pixmap_t miniaturize_mask;		/* miniaturize pixmap mask */
	xcb_pixmap_t close_mask;		/* close pixmap mask */
	uint32_t extra_flags;
} GNUstepWMAttributes;

#define GSWindowStyleAttr	(1<<0)
#define GSWindowLevelAttr	(1<<1)
#define GSMiniaturizePixmapAttr	(1<<3)
#define GSClosePixmapAttr	(1<<4)
#define GSMiniaturizeMaskAttr	(1<<5)
#define GSCloseMaskAttr		(1<<6)
#define GSExtraFlagsAttr	(1<<7)



#define GSClientResizeFlag	(1<<0)
#define GSFullKeyboardEventsFlag (1<<1)
#define GSMenuWindowFlag	(1<<2)
#define GSIconWindowFlag	(1<<3)
#define GSSkipWindowListFlag	(1<<4)
#define GSNoApplicationIconFlag	(1<<5)
#define GSDarkGrayTitlebarFlag	(1<<8)


#define WMFHideOtherApplications	10
#define WMFHideApplication		12


#ifndef _DEFINED_GNUSTEP_WINDOW_INFO
#define	_DEFINED_GNUSTEP_WINDOW_INFO
/*
 * Window levels are taken from GNUstep (gui/AppKit/NSWindow.h)
 * NSDesktopWindowLevel intended to be the level at which things
 * on the desktop sit ... so you should be able
 * to put a desktop background just below it.
 *
 * Applications are actually permitted to use any value in the
 * range INT_MIN+1 to INT_MAX
 */
enum {
	WMDesktopWindowLevel = -1000,	/* GNUstep addition */
	WMNormalWindowLevel = 0,
	WMFloatingWindowLevel = 3,
	WMSubmenuWindowLevel = 3,
	WMTornOffMenuWindowLevel = 3,
	WMMainMenuWindowLevel = 20,
	WMDockWindowLevel = 21,		/* Deprecated - use NSStatusWindowLevel */
	WMStatusWindowLevel = 21,
	WMModalPanelWindowLevel = 100,
	WMPopUpMenuWindowLevel = 101,
	WMScreenSaverWindowLevel = 1000
};


/* window attributes */
enum {
	WMBorderlessWindowMask = 0,
	WMTitledWindowMask = 1,
	WMClosableWindowMask = 2,
	WMMiniaturizableWindowMask = 4,
	WMResizableWindowMask = 8,
	WMIconWindowMask = 64,
	WMMiniWindowMask = 128
};
#endif

typedef struct _wmAppContext WMAppContext;

typedef struct _wmMenu WMMenu;

typedef void (*WMMenuAction)(void *clientdata, int code, xcb_timestamp_t timestamp);

typedef void (*WMFreeFunction)(void *clientdata);

int WMProcessEvent(WMAppContext *app, xcb_generic_event_t *event);


WMAppContext *WMAppCreateWithMain(xcb_connection_t *dpy, int screen_number,
		xcb_window_t main_window);

int WMAppAddWindow(WMAppContext *app, xcb_window_t window);

int WMAppSetMainMenu(WMAppContext *app, WMMenu *menu);


int WMRealizeMenus(WMAppContext *app);


void WMSetWindowAttributes(xcb_connection_t *dpy, xcb_window_t window,
		const GNUstepWMAttributes *attributes);


void WMHideApplication(WMAppContext *app);
void WMHideOthers(WMAppContext *app);

/*
 * WARNING: the menu related functions might be removed in the future.
 */
WMMenu *WMMenuCreate(WMAppContext *app, char *title);

int WMMenuAddItem(WMMenu *menu, char *text, WMMenuAction action,
		void *clientData, WMFreeFunction freedata, char *rtext);

int WMMenuAddSubmenu(WMMenu *menu, char *title, WMMenu *submenu);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
