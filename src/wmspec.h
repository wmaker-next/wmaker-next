/*  wmspec.h-- support for the wm-spec Hints
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1998-2003 Alfredo K. Kojima
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



#ifndef _WMSPEC_H_
#define _WMSPEC_H_

#include "screen.h"
#include "window.h"
#include <WINGs/WXProp.h>

/*
 * These constant provide information on the kind of window move/resize when
 * it is initiated by the application instead of by WindowMaker. They are
 * parameter for the client message _NET_WM_MOVERESIZE, as defined by the
 * FreeDesktop wm-spec standard:
 *   http://standards.freedesktop.org/wm-spec/1.5/ar01s04.html
 */
#define _NET_WM_MOVERESIZE_SIZE_TOPLEFT      0
#define _NET_WM_MOVERESIZE_SIZE_TOP          1
#define _NET_WM_MOVERESIZE_SIZE_TOPRIGHT     2
#define _NET_WM_MOVERESIZE_SIZE_RIGHT        3
#define _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT  4
#define _NET_WM_MOVERESIZE_SIZE_BOTTOM       5
#define _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT   6
#define _NET_WM_MOVERESIZE_SIZE_LEFT         7
#define _NET_WM_MOVERESIZE_MOVE              8	/* movement only */
#define _NET_WM_MOVERESIZE_SIZE_KEYBOARD     9	/* size via keyboard */
#define _NET_WM_MOVERESIZE_MOVE_KEYBOARD    10	/* move via keyboard */
#define _NET_WM_MOVERESIZE_CANCEL           11	/* cancel operation */

void wNETWMInitStuff(WScreen *scr);
void wNETWMCleanup(WScreen *scr);
void wNETWMFlushClientLists(WScreen *scr);
void wNETWMUpdateWorkarea(WScreen *scr);
bool wNETWMGetUsableArea(WScreen *scr, int head, WArea *area);

/* Publish or clear the dock's _NET_WM_STRUT(_PARTIAL) reservation on
 * the info window. wDockPublishStrut() decides between the two from
 * NoWindowOverDock and the dock presence, so callers can always call
 * it. Never pushed into scr->netdata->strut. */
void wDockPublishStrut(WScreen *scr);
void wDockClearStrut(WScreen *scr);
void wNETWMCheckInitialClientState(WWindow *wwin);
void wNETWMCheckInitialFrameState(WWindow *wwin);
void wNETWMCheckInitialClientStateCk(WWindow *wwin, WNETWMInitialCookies *ck);
void wNETWMCheckInitialFrameStateCk(WWindow *wwin, WXCookie opacity);
bool wNETWMProcessClientMessage(const xcb_client_message_event_t *event);

void wNETWMReqClientHints(xcb_window_t window, WNETWMHintCookies *ck);
void wNETWMReqInitialStateCookies(xcb_window_t window, WNETWMInitialCookies *ck);
void wNETWMRcvClientHints(WWindow *wwin, WNETWMHintCookies *ck, int *layer, int *workspace);
WXCookie wNETWMReqWindowName(xcb_window_t window);
void wNETWMCheckClientHintChange(WWindow *wwin, xcb_atom_t atom);
void wNETWMUpdateActions(WWindow *wwin, bool del);
void wNETWMUpdateDesktop(WScreen *scr);
void wNETWMUpdateCurrentDesktop(WScreen *scr);
void wNETWMShowingDesktopToggle(WScreen *scr);
void wNETWMShowingDesktopForgetWindow(WScreen *scr, WWindow *wwin);
void wNETWMPositionSplash(WWindow *wwin, int *x, int *y, int width, int height);
int wNETWMGetPidForWindow(xcb_window_t window);
WXCookie wNETWMReqPid(xcb_window_t window);
bool wNETWMIsPingProtocol(xcb_atom_t protocol);
void wNETWMPingWindow(WWindow *wwin, xcb_timestamp_t timestamp);
void wNETWMCancelPing(WWindow *wwin);
WWindow *wNETWMUserTimeWindowFor(xcb_window_t window);
void wNETWMUserTimeNotify(WWindow *wwin);
void wNETWMDetachUserTimeWindow(WWindow *wwin);
bool wNETWMShouldStealFocus(WWindow *wwin);
void wNETWMBumpUserTime(WScreen *scr, xcb_timestamp_t ts);
void wNETWMUpdateDesktopLayout(WScreen *scr);
void wNETWMCheckRootHintChange(WScreen *scr, xcb_atom_t atom);
bool wNETWMIsSyncRequestProtocol(xcb_atom_t protocol);
void wNETWMSyncRequestSetup(WWindow *wwin);
void wNETWMSyncRequestDestroy(WWindow *wwin);
void wNETWMSyncRequestSend(WWindow *wwin);
bool wNETWMSyncRequestPending(WWindow *wwin);
bool wNETWMSyncRequestHandleAlarm(xcb_generic_event_t *event);
int wNETWMGetCurrentDesktopFromHint(WScreen *scr);
char *wNETWMGetIconName(xcb_window_t window);
char *wNETWMGetWindowName(xcb_window_t window);
void wNETWMUpdateVisibleName(WWindow *wwin, const char *visible, const char *client_name);
void wNETWMUpdateVisibleIconName(WWindow *wwin, const char *visible, const char *client_name);
void wNETFrameExtents(WWindow *wwin);
void wNETCleanupFrameExtents(WWindow *wwin);
RImage *get_window_image_from_x11(xcb_window_t window);
#endif
