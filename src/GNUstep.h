/* GNUstep.h-- stuff for compatibility with GNUstep applications
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
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


#ifndef WMGNUSTEP_H_
#define WMGNUSTEP_H_

#include <stdint.h>



#define GNUSTEP_WM_MINIATURIZE_WINDOW "_GNUSTEP_WM_MINIATURIZE_WINDOW"
#define GNUSTEP_WM_HIDE_APP           "_GNUSTEP_WM_HIDE_APP"

#define GNUSTEP_WM_ATTR_NAME  "_GNUSTEP_WM_ATTR"

#define	GNUSTEP_TITLEBAR_STATE 	"_GNUSTEP_TITLEBAR_STATE"
enum {
    WMTitleBarKey = 0,
    WMTitleBarNormal = 1,
    WMTitleBarMain = 2
};

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
    WMDesktopWindowLevel = -1000, /* GNUstep addition     */
    WMNormalWindowLevel = 0,
    WMFloatingWindowLevel = 3,
    WMSubmenuWindowLevel = 3,
    WMTornOffMenuWindowLevel = 3,
    WMMainMenuWindowLevel = 20,
    WMDockWindowLevel = 21,       /* Deprecated - use NSStatusWindowLevel */
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

/* window manager -> appkit notifications */
#define GNUSTEP_WM_NOTIFICATION		"GNUSTEP_WM_NOTIFICATION"


typedef struct {
    uint32_t flags;
    uint32_t window_style;
    uint32_t window_level;
    uint32_t reserved;
    xcb_pixmap_t miniaturize_pixmap;	       /* pixmap for miniaturize button */
    xcb_pixmap_t close_pixmap;	       /* pixmap for close button */
    xcb_pixmap_t miniaturize_mask;	       /* miniaturize pixmap mask */
    xcb_pixmap_t close_mask;		       /* close pixmap mask */
    uint32_t extra_flags;
} GNUstepWMAttributes;

#define GSWindowStyleAttr 	(1<<0)
#define GSWindowLevelAttr 	(1<<1)
#define GSMiniaturizePixmapAttr	(1<<3)
#define GSClosePixmapAttr	(1<<4)
#define GSMiniaturizeMaskAttr	(1<<5)
#define GSCloseMaskAttr		(1<<6)
#define GSExtraFlagsAttr	(1<<7)

/* extra flags */
#define GSDocumentEditedFlag	(1<<0)

#define GSNoApplicationIconFlag	(1<<5)


#define WMFHideOtherApplications	10
#define WMFHideApplication		12

#endif
