/* dock.h- built-in Dock module for WindowMaker
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 1998-2003 Dan Pascu
 *  Copyright (c) 2013-2026 Window Maker Team
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

#ifndef WMDOCK_H_
#define WMDOCK_H_

#include "appicon.h"
#include "dockgeom.h"

typedef struct WDock {
    WScreen *screen_ptr;
    int x_pos, y_pos;		       /* position of the first icon */

    WAppIcon **icon_array;	       /* array of docked icons */
    int max_icons;

    int icon_count;

#define WM_DOCK        0
#define WM_CLIP        1
#define WM_DRAWER      2
    int type;

    WMagicNumber auto_expand_magic;
    WMagicNumber auto_collapse_magic;
    WMagicNumber auto_raise_magic;
    WMagicNumber auto_lower_magic;
    unsigned int auto_collapse:1;      /* if clip auto-collapse itself */
    unsigned int auto_raise_lower:1;   /* if clip should raise/lower when
					* entered/leaved */
    WDockEdge edge;		       /* the screen edge the dock is
					* pinned to. Drawers copy it and
					* open perpendicular to it. The
					* clip does not lay out by it. */
    unsigned int collapsed:1;
    unsigned int mapped:1;
    unsigned int lowered:1;
    unsigned int attract_icons:1;      /* If clip should attract app-icons */
    unsigned int auto_compact:1;

    unsigned int lclip_button_pushed:1;
    unsigned int rclip_button_pushed:1;

    /* auto-hide slide state, meaningful on scr->dock only. x_pos and
     * y_pos stay canonical, slide_offset applies on configure. */
    unsigned int auto_hide:1;	       /* persistent mode */
    unsigned int hidden:1;	       /* last fully reached state */
    unsigned int target_hidden:1;      /* current transition target */
    WMagicNumber auto_hide_magic;      /* one-shot hide-delay timer */
    WMagicNumber slide_magic;	       /* persistent animation timer */
    int slide_offset;		       /* signed root-normal delta, 0 = shown */
    int slide_from_offset;	       /* interpolation start */
    int64_t slide_started_ms;	       /* CLOCK_MONOTONIC timestamp */
    xcb_window_t reveal_strip;	       /* XCB_NONE when not mapped */

    struct WMenu *menu;

    struct WDDomain *defaults;
} WDock;





WDock *wDockCreate(WScreen *scr, int type, const char *name);
WDock *wDockRestoreState(WScreen *scr, WMPropList *dock_state, int type);

void wDockDestroy(WDock *dock);
void wDockHideIcons(WDock *dock);
void wDockShowIcons(WDock *dock);
void wDockLower(WDock *dock);
void wDockRaise(WDock *dock);
void wDockRaiseLower(WDock *dock);
void wDockSaveState(WScreen *scr, WMPropList *old_state);

bool wDockAttachIcon(WDock *dock, WAppIcon *icon, int x, int y, bool update_icon);
bool wDockSnapIcon(WDock *dock, WAppIcon *icon, int req_x, int req_y,
                   int *ret_x, int *ret_y, int redocking);
bool wDockFindFreeSlot(WDock *dock, int *req_x, int *req_y);
void wDockDetach(WDock *dock, WAppIcon *icon);
bool wDockMoveIconBetweenDocks(WDock *src, WDock *dest, WAppIcon *icon, int x, int y);
void wDockReattachIcon(WDock *dock, WAppIcon *icon, int x, int y);

void wSlideAppiconsDelta(WAppIcon **appicons, int n, int dx_cells, int dy_cells, char axis);
void wSlideAppiconsAcross(WAppIcon **appicons, int n, const WDock *drawer,
			  bool into_drawer);
void wDrawerFillTheGap(WDock *drawer, WAppIcon *aicon, bool redocking);
void wClipFillTheGap(WDock *clip, WAppIcon *aicon);

void wDockFinishLaunch(WAppIcon *icon);
void wDockTrackWindowLaunch(WDock *dock, xcb_window_t window, const char *wm_instance,
			    const char *wm_class, const char *command,
			    pid_t client_pid);
WAppIcon *wDockFindIconForWindow(WDock *dock, xcb_window_t window);
void wDockDoAutoLaunch(WDock *dock, int workspace);
void wDockLaunchWithState(WAppIcon *btn, WSavedState *state);
void wDockSetEdge(WDock *dock, WDockEdge edge);
void wDockReanchor(WDock *dock);
void wDockApplyIconSize(WScreen *scr);
void wClipSnapToHead(WDock *clip);

/* The slot reported over IPC and death handlers: the along-axis index
 * for WM_DOCK icons, yindex elsewhere. */
int wDockIconSlot(const WAppIcon *icon);

/* Which stored index is the stacking index for this edge. Vertical:
 * along is yindex, across is xindex. Horizontal: along is xindex,
 * across is yindex. */
int wDockAlongIndex(const WAppIcon *icon, WDockEdge edge);
int wDockAcrossIndex(const WAppIcon *icon, WDockEdge edge);

/* The screen-axis sign of an "into the screen" drawer step */
int wDockAcrossSign(WDockEdge edge);

/* Number of whole icons that fit on the anchor rect's along axis, the
 * on-screen limit for snap, wrap and drawer restore. */
int wDockAlongLimit(const WDock *dock);

/* Shrink the area by the dock strip on the dock's edge, the whole
 * head. The caller decides whether the inset applies and whether the
 * dock touches the head in question. */
void wDockInsetArea(WScreen *scr, WArea *area);

#ifdef USE_DOCK_XDND
int wDockReceiveDNDDrop(WScreen *scr, xcb_generic_event_t *event);
#endif

void wClipIconPaint(WAppIcon *aicon);
void wClipSaveState(WScreen *scr);
WMPropList *wClipSaveWorkspaceState(WScreen *scr, int workspace);
WAppIcon *wClipRestoreState(WScreen *scr, WMPropList *clip_state);

void wDrawerIconPaint(WAppIcon *dicon);
void wDrawerIconTileShift(WIcon *icon, int *dx, int *dy);
void wDrawersSaveState(WScreen *scr);
void wDrawersRestoreState(WScreen *scr);
int wIsADrawer(WAppIcon *aicon);

void wClipUpdateForWorkspaceChange(WScreen *scr, int workspace);
void wClipRepointForActiveHead(WScreen *scr, int old_workspace, int new_workspace);

RImage *wClipMakeTile(RImage *normalTile);
RImage* wDrawerMakeTile(WScreen *scr, RImage *normalTile);

#define WO_FAILED          0
#define WO_NOT_APPLICABLE  1
#define WO_SUCCESS         2

/* The dock whose slide offset applies: self, root dock for a drawer,
 * NULL for a clip. */
WDock *wDockAutoHideOwner(WDock *dock);

/* Canonical shown coordinates plus the owner's slide offset on the
 * edge-normal axis, canonical when the chain does not auto-hide. */
void wDockAutoHideEffectivePosition(const WAppIcon *btn, int *x, int *y);

void wDockApplyIconPosition(WAppIcon *btn);

void dockSetLowered(WDock *dock, bool lowered);

void wDockAutoHideApplyInitial(WScreen *scr);
void wDockAutoHideGeometryChanged(WScreen *scr);
void wDockAutoHideStripUpdate(WScreen *scr);
void wDockAutoHideAnimationConfigChanged(WScreen *scr);
void wDockRevealRaise(WScreen *scr);

bool wDockRevealHandleCrossing(WScreen *scr, xcb_generic_event_t *ev, bool enter);

/* While the counter is positive the auto-hide delay never hides */
void wDockAutoHideBusyPush(WScreen *scr);
void wDockAutoHideBusyPop(WScreen *scr);

void wDockAutoHideDragBegin(WScreen *scr);
void wDockAutoHideDragEnd(WScreen *scr);

int wClipMakeIconOmnipresent(WAppIcon *aicon, int omnipresent);

#endif
