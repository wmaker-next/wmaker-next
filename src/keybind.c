/* keybind.c - key-binding command dispatch
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2014-2026 Window Maker Team
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include <WINGs/WINGsP.h>

#include "WindowMaker.h"
#include "window.h"
#include "actions.h"
#include "client.h"
#include "main.h"
#include "cycling.h"
#include "tabgroup.h"
#include "keybind.h"
#include "application.h"
#include "stacking.h"
#include "defaults.h"
#include "workspace.h"
#ifdef VIRTUAL_DESKTOP
#include "vdesk.h"
#endif
#include "dock.h"
#include "framewin.h"
#include "balloon.h"
#include "xinerama.h"
#include "wmspec.h"
#include "rootmenu.h"
#include "colormap.h"
#include "screen.h"
#include "misc.h"
#include "event.h"
#include "osdview.h"
#include "wxkb.h"
#include "accent_picker.h"
#include "winmenu.h"
#include "layout.h"
#include "switchmenu.h"
#include "wsmap.h"
#include "hotcorner.h"
#include "cursor_find.h"
#include "wsound.h"

#include "config_schema.h"
#include "config_schema_def.h"
#include "keybind_descriptions.h"
#include "dialog.h"

static void hideLayoutOSD(void *data)
{
	WScreen *scr = data;

	scr->layout_osd_timer = NULL;
	/* the chord display owns this widget while a chain is armed,
	 * refresh instead of unconditionally hiding */
	wRefreshKeyChainOSD(scr);
}

static void flashLayoutOSD(WScreen *scr, const char *text)
{
	if (!scr || !scr->keychain_osd)
		return;
	if (w_global.config->keychain_display_position == WD_NONE)
		return;

	if (scr->layout_osd_timer)
		WMDeleteTimerHandler(scr->layout_osd_timer);

	WSetOSDViewText(scr->keychain_osd, text);
	wPlaceOSDOnHead(scr, scr->keychain_osd,
			w_global.config->keychain_display_position, WOSD_EDGE_PAD);
	WMMapWidget(scr->keychain_osd);
	/* OSD widget, never in the stacking list, direct configure */
	xcb_configure_window(wdisp->conn, WMWidgetXID(scr->keychain_osd), XCB_CONFIG_WINDOW_STACK_MODE, (const uint32_t[]){ XCB_STACK_MODE_ABOVE });
	scr->layout_osd_timer = WMAddTimerHandler(800, hideLayoutOSD, scr);
}

static void changeLayoutMasterCount(WScreen *scr, int step)
{
	WWorkspace *ws = scr->workspaces[scr->current_workspace];
	char buf[32];

	ws->master_count += step;
	if (ws->master_count < 0)
		ws->master_count = 0;
	wLayoutApply(scr, scr->current_workspace);
	snprintf(buf, sizeof(buf), "master %d", ws->master_count);
	flashLayoutOSD(scr, buf);
}

static void changeLayoutMasterRatio(WScreen *scr, float step)
{
	WWorkspace *ws = scr->workspaces[scr->current_workspace];
	char buf[32];

	ws->master_ratio += step;
	if (ws->master_ratio < WLAYOUT_RATIO_MIN)
		ws->master_ratio = WLAYOUT_RATIO_MIN;
	if (ws->master_ratio > WLAYOUT_RATIO_MAX)
		ws->master_ratio = WLAYOUT_RATIO_MAX;
	wLayoutApply(scr, scr->current_workspace);
	snprintf(buf, sizeof(buf), "ratio %.2f", (double)ws->master_ratio);
	flashLayoutOSD(scr, buf);
}

static void cycleLayout(WScreen *scr, int step)
{
	WWorkspace *ws = scr->workspaces[scr->current_workspace];
	WLayout next = (ws->layout + step + WLAYOUT_COUNT) % WLAYOUT_COUNT;

	wLayoutSetWorkspace(scr, scr->current_workspace, next);
	flashLayoutOSD(scr, wLayoutName(next));
}

static WWindow *windowUnderPointer(WScreen *scr)
{
	unsigned int mask;
	int foo;
	xcb_window_t bar, win;

	if (wXQueryPointer(wdisp->conn, scr->root_win, &bar, &win, &foo, &foo, &foo, &foo, &mask))
		return wWindowFor(win);
	return NULL;
}

static int CheckFullScreenWindowFocused(WScreen *scr)
{
	if (scr->focused_window && scr->focused_window->flags.fullscreen)
		return 1;
	else
		return 0;
}

static void startMarkCapture(WScreen *scr, xcb_connection_t *d, WMarkCaptureMode mode)
{
	scr->flags.mark_capture_mode = mode;
	wXGrabKeyboard(d, scr->root_win, false, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_CURRENT_TIME);
}

/* Dispatch time of the last key-press-triggered clipboard command, so
 * the IPC tick broadcast can flag key-invoked popups even when the
 * chord is already released by the time the helper reaches wmaker. The
 * hint is one-shot, one key invocation marks one tick. */
#define CLIPBOARD_KEY_HINT_MAX_AGE 5

static time_t clipboard_key_time;

static void note_clipboard_key_dispatch(void)
{
	clipboard_key_time = time(NULL);
}

bool wClipboardTakeKeyHint(void)
{
	time_t now = time(NULL);
	bool recent = clipboard_key_time != (time_t)0 &&
		      now >= clipboard_key_time &&
		      now - clipboard_key_time <= CLIPBOARD_KEY_HINT_MAX_AGE;

	clipboard_key_time = (time_t)0;
	return recent;
}

void wKeybindDispatch(int command, WScreen *scr, WWindow *wwin, xcb_generic_event_t *event)
{
	short widx;
	int i;
	xcb_timestamp_t ts = event ? ((xcb_key_press_event_t *)event)->time
				   : wdisp->timestamp.last_event;
#ifdef KEEP_XKB_LOCK_STATUS
	int xkbgroup;
#endif

	switch (command) {

	case WKBD_ROOTMENU:
		if (!CheckFullScreenWindowFocused(scr)) {
			WMRect rect = wGetRectForHead(scr, wGetHeadForPointerLocation(scr));
			/* Grab before the menu is built so keys typed while it
			 * is being realized cannot leak to the focused client. */
			wXGrabKeyboard(wdisp->conn, scr->root_win, true,
				       XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_CURRENT_TIME);
			OpenRootMenu(scr, rect.pos.x + rect.size.width / 2, rect.pos.y + rect.size.height / 2,
				     true);
			wXUngrabKeyboard(wdisp->conn, XCB_CURRENT_TIME);
		}
		break;
	case WKBD_WINDOWLIST:
		if (!CheckFullScreenWindowFocused(scr)) {
			WMRect rect = wGetRectForHead(scr, wGetHeadForPointerLocation(scr));
			wXGrabKeyboard(wdisp->conn, scr->root_win, true,
				       XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_CURRENT_TIME);
			OpenSwitchMenu(scr, rect.pos.x + rect.size.width / 2, rect.pos.y + rect.size.height / 2,
				       true);
			wXUngrabKeyboard(wdisp->conn, XCB_CURRENT_TIME);
		}
		break;

	case WKBD_WINDOWMENU:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin)) {
			wXGrabKeyboard(wdisp->conn, scr->root_win, true,
				       XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_CURRENT_TIME);
			OpenWindowMenu(wwin, wwin->frame_x, wwin->frame_y + wwin->frame->top_width, true);
			wXUngrabKeyboard(wdisp->conn, XCB_CURRENT_TIME);
		}
		break;
	case WKBD_MINIATURIZEALL:
		CloseWindowMenu(scr);
		wHideAll(scr);
		break;
	case WKBD_MINIATURIZE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin)
		    && !WFLAGP(wwin, no_miniaturizable)) {
			CloseWindowMenu(scr);

			if (wwin->protocols.MINIATURIZE_WINDOW)
				wClientSendProtocol(wwin, wdisp->atom.gnustep.wm_miniaturize_window, ts);
			else {
				wIconifyWindow(wwin);
			}
		}
		break;
	case WKBD_HIDE:
		if (wwin) {
			CloseWindowMenu(scr);
			wRequestHideApplication(wApplicationOf(wwin->main_window), wwin);
		}
		break;
	case WKBD_HIDE_OTHERS:
		if (wwin) {
			CloseWindowMenu(scr);

			wHideOtherApplications(wwin);
		}
		break;
	case WKBD_SHOW_DESKTOP:
		CloseWindowMenu(scr);
		wNETWMShowingDesktopToggle(scr);
		break;
	case WKBD_SHOWALL:
		wShowAllWindows(scr);
		break;
	case WKBD_ARRANGEICONS:
		wArrangeIcons(scr, true);
		break;
	case WKBD_MINIATURIZE_OTHERS:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin)) {
			CloseWindowMenu(scr);

			wMiniaturizeOtherApplications(wwin);
		}
		break;
	case WKBD_MAXIMIZE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);

			handleMaximize(wwin, MAX_VERTICAL | MAX_HORIZONTAL | MAX_KEYBOARD);
		}
		break;
	case WKBD_FULLSCREEN:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin)
		    && (wwin->flags.fullscreen || IS_RESIZABLE(wwin))) {
			CloseWindowMenu(scr);
			if (wwin->flags.fullscreen)
				wUnfullscreenWindow(wwin);
			else
				wFullscreenWindow(wwin);
		}
		break;
	case WKBD_VMAXIMIZE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);

			handleMaximize(wwin, MAX_VERTICAL | MAX_KEYBOARD);
		}
		break;
	case WKBD_HMAXIMIZE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);

			handleMaximize(wwin, MAX_HORIZONTAL | MAX_KEYBOARD);
			movePointerToWindowCenter(wwin);
		}
		break;
	case WKBD_CENTRAL:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);

			handleMaximize(wwin, MAX_CENTRAL | MAX_KEYBOARD);
			movePointerToWindowCenter(wwin);
		}
		break;
	case WKBD_LHMAXIMIZE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);

			handleMaximize(wwin, MAX_VERTICAL | MAX_LEFTHALF | MAX_KEYBOARD);
			movePointerToWindowCenter(wwin);
		}
		break;
	case WKBD_RHMAXIMIZE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);

			handleMaximize(wwin, MAX_VERTICAL | MAX_RIGHTHALF | MAX_KEYBOARD);
			movePointerToWindowCenter(wwin);
		}
		break;
	case WKBD_THMAXIMIZE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);

			handleMaximize(wwin, MAX_HORIZONTAL | MAX_TOPHALF | MAX_KEYBOARD);
			movePointerToWindowCenter(wwin);
		}
		break;
	case WKBD_BHMAXIMIZE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);

			handleMaximize(wwin, MAX_HORIZONTAL | MAX_BOTTOMHALF | MAX_KEYBOARD);
			movePointerToWindowCenter(wwin);
		}
		break;
	case WKBD_LTCMAXIMIZE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);

			handleMaximize(wwin, MAX_LEFTHALF | MAX_TOPHALF | MAX_KEYBOARD);
			movePointerToWindowCenter(wwin);
		}
		break;
	case WKBD_RTCMAXIMIZE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);

			handleMaximize(wwin, MAX_RIGHTHALF | MAX_TOPHALF | MAX_KEYBOARD);
			movePointerToWindowCenter(wwin);
		}
		break;
	case WKBD_LBCMAXIMIZE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);

			handleMaximize(wwin, MAX_LEFTHALF | MAX_BOTTOMHALF | MAX_KEYBOARD);
			movePointerToWindowCenter(wwin);
		}
		 break;
	case WKBD_RBCMAXIMIZE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);

			handleMaximize(wwin, MAX_RIGHTHALF | MAX_BOTTOMHALF | MAX_KEYBOARD);
			movePointerToWindowCenter(wwin);
		}
		break;
	case WKBD_MAXIMUS:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);

			handleMaximize(wwin, MAX_MAXIMUS | MAX_KEYBOARD);
		}
		break;
	case WKBD_KEEP_ON_TOP:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin)) {
			bool on = wwin->frame->core->stacking->window_level != WMFloatingLevel;

			CloseWindowMenu(scr);

			/* keep the per-window flag in step with the level so
			 * _NET_WM_STATE_ABOVE is published and the choice persists */
			WSETPERWINDOWFLAG(wwin, floating, on);
			WSETPERWINDOWFLAG(wwin, sunken, 0);
			ChangeStackingLevel(wwin->frame->core,
					    on ? WMFloatingLevel : WMNormalLevel);
		}
		break;

	case WKBD_KEEP_AT_BOTTOM:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin)) {
			bool on = wwin->frame->core->stacking->window_level != WMSunkenLevel;

			CloseWindowMenu(scr);

			/* same for _NET_WM_STATE_BELOW */
			WSETPERWINDOWFLAG(wwin, sunken, on);
			WSETPERWINDOWFLAG(wwin, floating, 0);
			ChangeStackingLevel(wwin->frame->core,
					    on ? WMSunkenLevel : WMNormalLevel);
		}
		break;
	case WKBD_OMNIPRESENT:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin)) {
			CloseWindowMenu(scr);

			wWindowSetOmnipresent(wwin, !IS_OMNIPRESENT(wwin));
		}
		break;
	case WKBD_RAISE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin)) {
			CloseWindowMenu(scr);

			wRaiseFrame(wwin->frame->core);
		}
		break;
	case WKBD_LOWER:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin)) {
			CloseWindowMenu(scr);

			wLowerFrame(wwin->frame->core);
		}
		break;
	case WKBD_RAISELOWER:
		/* raise or lower the window under the pointer, not the
		 * focused one
		 */
		wwin = windowUnderPointer(scr);
		if (wwin)
			wRaiseLowerFrame(wwin->frame->core);
		break;
	case WKBD_SHADE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && !WFLAGP(wwin, no_shadeable)) {
			if (wwin->flags.shaded)
				wUnshadeWindow(wwin);
			else
				wShadeWindow(wwin);
		}
		break;
	case WKBD_MOVERESIZE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && (IS_RESIZABLE(wwin) || IS_MOVABLE(wwin))) {
			CloseWindowMenu(scr);

			wKeyboardMoveResizeWindow(wwin);
		}
		break;
	case WKBD_MOVE_TO_EDGE_LEFT:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_MOVABLE(wwin)) {
			CloseWindowMenu(scr);
			wMoveToEdge(wwin, DIRECTION_LEFT, true);
		}
		break;
	case WKBD_MOVE_TO_EDGE_RIGHT:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_MOVABLE(wwin)) {
			CloseWindowMenu(scr);
			wMoveToEdge(wwin, DIRECTION_RIGHT, true);
		}
		break;
	case WKBD_MOVE_TO_EDGE_UP:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_MOVABLE(wwin)) {
			CloseWindowMenu(scr);
			wMoveToEdge(wwin, DIRECTION_UP, true);
		}
		break;
	case WKBD_MOVE_TO_EDGE_DOWN:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_MOVABLE(wwin)) {
			CloseWindowMenu(scr);
			wMoveToEdge(wwin, DIRECTION_DOWN, true);
		}
		break;
	case WKBD_MOVE_TO_EDGE_LEFT_NOSNAP:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_MOVABLE(wwin)) {
			CloseWindowMenu(scr);
			wMoveToEdge(wwin, DIRECTION_LEFT, false);
		}
		break;
	case WKBD_MOVE_TO_EDGE_RIGHT_NOSNAP:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_MOVABLE(wwin)) {
			CloseWindowMenu(scr);
			wMoveToEdge(wwin, DIRECTION_RIGHT, false);
		}
		break;
	case WKBD_MOVE_TO_EDGE_UP_NOSNAP:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_MOVABLE(wwin)) {
			CloseWindowMenu(scr);
			wMoveToEdge(wwin, DIRECTION_UP, false);
		}
		break;
	case WKBD_MOVE_TO_EDGE_DOWN_NOSNAP:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_MOVABLE(wwin)) {
			CloseWindowMenu(scr);
			wMoveToEdge(wwin, DIRECTION_DOWN, false);
		}
		break;
	case WKBD_GROW_TO_EDGE_LEFT:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);
			wGrowToEdge(wwin, DIRECTION_LEFT);
		}
		break;
	case WKBD_GROW_TO_EDGE_RIGHT:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);
			wGrowToEdge(wwin, DIRECTION_RIGHT);
		}
		break;
	case WKBD_GROW_TO_EDGE_UP:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);
			wGrowToEdge(wwin, DIRECTION_UP);
		}
		break;
	case WKBD_GROW_TO_EDGE_DOWN:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);
			wGrowToEdge(wwin, DIRECTION_DOWN);
		}
		break;
	case WKBD_SHRINK_TO_EDGE_LEFT:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);
			wShrinkToEdge(wwin, DIRECTION_LEFT);
		}
		break;
	case WKBD_SHRINK_TO_EDGE_RIGHT:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);
			wShrinkToEdge(wwin, DIRECTION_RIGHT);
		}
		break;
	case WKBD_SHRINK_TO_EDGE_UP:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);
			wShrinkToEdge(wwin, DIRECTION_UP);
		}
		break;
	case WKBD_SHRINK_TO_EDGE_DOWN:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);
			wShrinkToEdge(wwin, DIRECTION_DOWN);
		}
		break;
	case WKBD_CLOSE:
		if (wwin && !WFLAGP(wwin, no_closable)) {
			CloseWindowMenu(scr);
			wClientSendProtocol(wwin, wdisp->atom.wm.delete_window, ts);
			wNETWMPingWindow(wwin, ts);
		}
		break;
	case WKBD_SELECT:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin)) {
			wSelectWindow(wwin, !wwin->flags.selected);
		}
		break;

	case WKBD_WORKSPACEMAP:
		if (w_global.config->enable_workspace_pager)
			StartWorkspaceMap(scr);
		break;

	case WKBD_FOCUSNEXT:
		StartWindozeCycle(scr, wwin, true, CYCLE_FLAT);
		break;

	case WKBD_FOCUSPREV:
		StartWindozeCycle(scr, wwin, false, CYCLE_FLAT);
		break;

	case WKBD_FOCUSLEFT:
		wSetFocusToDirection(scr, DIRECTION_LEFT);
		break;

	case WKBD_FOCUSRIGHT:
		wSetFocusToDirection(scr, DIRECTION_RIGHT);
		break;

	case WKBD_FOCUSUP:
		wSetFocusToDirection(scr, DIRECTION_UP);
		break;

	case WKBD_FOCUSDOWN:
		wSetFocusToDirection(scr, DIRECTION_DOWN);
		break;

	case WKBD_GROUPWINDOWNEXT:
		StartWindozeCycle(scr, wwin, true, CYCLE_CLASS_ONLY);
		break;

	case WKBD_GROUPWINDOWPREV:
		StartWindozeCycle(scr, wwin, false, CYCLE_CLASS_ONLY);
		break;

	case WKBD_GROUPNEXT:
		StartWindozeCycle(scr, wwin, true, CYCLE_GROUP);
		break;

	case WKBD_GROUPPREV:
		StartWindozeCycle(scr, wwin, false, CYCLE_GROUP);
		break;

	case WKBD_GROUPTAB_NEXT:
		wGroupTabNext(scr);
		break;

	case WKBD_GROUPTAB_PREV:
		wGroupTabPrev(scr);
		break;

	case WKBD_GROUPTAB_ATTACH:
		wGroupTabAttach(scr);
		break;

	case WKBD_GROUPTAB_DETACH:
		wGroupTabDetach(scr);
		break;

	case WKBD_LAYOUTCYCLE:
		cycleLayout(scr, 1);
		break;
	case WKBD_LAYOUTCYCLEBACK:
		cycleLayout(scr, -1);
		break;
	case WKBD_LAYOUTAXIS:
		i = scr->workspaces[scr->current_workspace]->layout;
		if (i == WLAYOUT_TILE)
			wLayoutSetWorkspace(scr, scr->current_workspace, WLAYOUT_TILE_H);
		else if (i == WLAYOUT_TILE_H)
			wLayoutSetWorkspace(scr, scr->current_workspace, WLAYOUT_TILE);
		flashLayoutOSD(scr, wLayoutName(scr->workspaces[scr->current_workspace]->layout));
		break;
	case WKBD_LAYOUTFLOAT:
		wLayoutSetWorkspace(scr, scr->current_workspace, WLAYOUT_FLOAT);
		flashLayoutOSD(scr, wLayoutName(WLAYOUT_FLOAT));
		break;
	case WKBD_MASTERCOUNTINC:
		changeLayoutMasterCount(scr, 1);
		break;
	case WKBD_MASTERCOUNTDEC:
		changeLayoutMasterCount(scr, -1);
		break;
	case WKBD_MASTERRATIOINC:
		changeLayoutMasterRatio(scr, WLAYOUT_RATIO_STEP);
		break;
	case WKBD_MASTERRATIODEC:
		changeLayoutMasterRatio(scr, -WLAYOUT_RATIO_STEP);
		break;
	case WKBD_TILINGTOGGLE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin))
			wLayoutSetWindowTiled(wwin, wwin->flags.no_tiling);
		break;
	case WKBD_TILINGSWAP:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin))
			wLayoutSwapMaster(scr, scr->current_workspace, wwin);
		break;

	case WKBD_WORKSPACE1 ... WKBD_WORKSPACE10:
		widx = command - WKBD_WORKSPACE1;
		i = (scr->current_workspace / 10) * 10 + widx;
		if (w_global.config->ws_advance || i < scr->workspace_count)
			wWorkspaceChange(scr, i);
		break;

	case WKBD_NEXTWORKSPACE:
		wWorkspaceRelativeChange(scr, 1);
		break;
	case WKBD_PREVWORKSPACE:
		wWorkspaceRelativeChange(scr, -1);
		break;
	case WKBD_LASTWORKSPACE:
		wWorkspaceChange(scr, scr->last_workspace);
		break;
	case WKBD_LEFTWORKSPACE:
		wWorkspaceDirectionalChange(scr, -1, 0);
		break;
	case WKBD_RIGHTWORKSPACE:
		wWorkspaceDirectionalChange(scr, 1, 0);
		break;
	case WKBD_UPWORKSPACE:
		wWorkspaceDirectionalChange(scr, 0, -1);
		break;
	case WKBD_DOWNWORKSPACE:
		wWorkspaceDirectionalChange(scr, 0, 1);
		break;

	case WKBD_MOVE_WORKSPACE1 ... WKBD_MOVE_WORKSPACE10:
		widx = command - WKBD_MOVE_WORKSPACE1;
		i = (scr->current_workspace / 10) * 10 + widx;
		if (wwin && (w_global.config->ws_advance || i < scr->workspace_count))
			wWindowChangeWorkspace(wwin, i);
		break;

	case WKBD_TOGGLE_WORKSPACE1 ... WKBD_TOGGLE_WORKSPACE10:
		widx = command - WKBD_TOGGLE_WORKSPACE1;
		i = (scr->current_workspace / 10) * 10 + widx;
		if (wwin && wwin->frame && i < scr->workspace_count) {
			if (WWIN_WS_MASK(wwin) & WS_BIT(i))
				wWindowRemoveFromWorkspace(wwin, i);
			else
				wWindowAddToWorkspace(wwin, i);
		}
		break;

	case WKBD_MOVE_NEXTWORKSPACE:
		if (wwin)
			wWindowChangeWorkspaceRelative(wwin, 1);
		break;
	case WKBD_MOVE_PREVWORKSPACE:
		if (wwin)
			wWindowChangeWorkspaceRelative(wwin, -1);
		break;
	case WKBD_MOVE_LASTWORKSPACE:
		if (wwin)
			wWindowChangeWorkspace(wwin, scr->last_workspace);
		break;
	case WKBD_MOVE_LEFTWORKSPACE:
	case WKBD_MOVE_RIGHTWORKSPACE:
	case WKBD_MOVE_UPWORKSPACE:
	case WKBD_MOVE_DOWNWORKSPACE:
		if (wwin) {
			int dc = 0, dr = 0;
			if (command == WKBD_MOVE_LEFTWORKSPACE)
				dc = -1;
			else if (command == WKBD_MOVE_RIGHTWORKSPACE)
				dc = 1;
			else if (command == WKBD_MOVE_UPWORKSPACE)
				dr = -1;
			else
				dr = 1;
			widx = wWorkspaceNeighbour(scr, scr->current_workspace, dc, dr);
			if (widx >= 0)
				wWindowChangeWorkspace(wwin, widx);
		}
		break;

	case WKBD_MOVE_NEXTWSLAYER:
	case WKBD_MOVE_PREVWSLAYER:
		{
			if (wwin) {
				int row, column;

				row = scr->current_workspace / 10;
				column = scr->current_workspace % 10;

				if (command == WKBD_MOVE_NEXTWSLAYER) {
					if ((row + 1) * 10 < scr->workspace_count)
						wWindowChangeWorkspace(wwin, column + (row + 1) * 10);
				} else {
					if (row > 0)
						wWindowChangeWorkspace(wwin, column + (row - 1) * 10);
				}
			}
		}
		break;

	case WKBD_WINDOW1:
	case WKBD_WINDOW2:
	case WKBD_WINDOW3:
	case WKBD_WINDOW4:
	case WKBD_WINDOW5:
	case WKBD_WINDOW6:
	case WKBD_WINDOW7:
	case WKBD_WINDOW8:
	case WKBD_WINDOW9:
	case WKBD_WINDOW10:

		widx = command - WKBD_WINDOW1;

		if (scr->shortcutWindows[widx]) {
			WMArray *list = scr->shortcutWindows[widx];
			int cw;
			int count = WMGetArrayItemCount(list);
			WWindow *twin;
			WMArrayIterator iter;
			WWindow *swin;

			wUnselectWindows(scr);
			cw = scr->current_workspace;

			WM_ETARETI_ARRAY(list, swin, iter) {
				if (count > 1)
					wWindowChangeWorkspace(swin, cw);

				wMakeWindowVisible(swin);

				if (count > 1)
					wSelectWindow(swin, true);
			}

			/* rotate the order of windows, to create a cycling effect */
			twin = WMGetFromArray(list, 0);
			WMDeleteFromArray(list, 0);
			WMAddToArray(list, twin);

		} else if (wwin && ISMAPPED(wwin) && ISFOCUSED(wwin)) {
			if (scr->shortcutWindows[widx]) {
				WMFreeArray(scr->shortcutWindows[widx]);
				scr->shortcutWindows[widx] = NULL;
			}

			if (wwin->flags.selected && scr->selected_windows) {
				scr->shortcutWindows[widx] = WMDuplicateArray(scr->selected_windows);
			} else {
				scr->shortcutWindows[widx] = WMCreateArray(4);
				WMAddToArray(scr->shortcutWindows[widx], wwin);
			}

			wSelectWindow(wwin, !wwin->flags.selected);
			wXFlush(wdisp->conn);
			wusleep(3000);
			wSelectWindow(wwin, !wwin->flags.selected);
			wXFlush(wdisp->conn);

		} else if (wwin && scr->selected_windows && WMGetArrayItemCount(scr->selected_windows)) {

			if (wwin->flags.selected && scr->selected_windows) {
				if (scr->shortcutWindows[widx]) {
					WMFreeArray(scr->shortcutWindows[widx]);
				}
				scr->shortcutWindows[widx] = WMDuplicateArray(scr->selected_windows);
			}
		}

		break;

	case WKBD_MOVE_12_TO_6_HEAD:
	case WKBD_MOVE_6_TO_12_HEAD:
		if (wwin)
			moveBetweenHeads(wwin, command - WKBD_MOVE_12_TO_6_HEAD);

		break;

	case WKBD_RELAUNCH:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin))
			(void) RelaunchWindow(wwin);

		break;

	case WKBD_SWITCH_SCREEN:
		warpPointerToNextMonitor(scr);
		break;

	case WKBD_FINDMOUSE:
		wFindMouseShow(scr);
		break;

	case WKBD_RUN:
		ExecuteInputCommand(scr, _("exec %A(Find)"));
		break;

	case WKBD_SHOWKEYBINDS:
		wShowShortcutsPanel(scr);
		break;

	case WKBD_EXIT:
		/* quick mode is not allowed to prevent inadvertently call */
		ExecuteExitCommand(scr, 0);
		break;

	case WKBD_PRINTS:
		ScreenCapture(scr, PRINT_SCREEN);
		break;

	case WKBD_PRINTW:
		ScreenCapture(scr, PRINT_WINDOW);
		break;

	case WKBD_PRINTP:
		ScreenCapture(scr, PRINT_PARTIAL);
		break;

	case WKBD_LOCKSCREEN:
		if (w_global.config->lock_screen_command &&
		    w_global.config->lock_screen_command[0]) {
			wSoundPlay(scr, WSOUND_LOCK);
			ExecuteShellCommand(scr, w_global.config->lock_screen_command);
		}
		break;

	case WKBD_CLIPBOARD:
		if (w_global.config->clipboard_command &&
		    w_global.config->clipboard_command[0]) {
			if (event)
				note_clipboard_key_dispatch();
			ExecuteShellCommand(scr, w_global.config->clipboard_command);
		}
		break;

	case WKBD_NEXTWSLAYER:
	case WKBD_PREVWSLAYER:
		{
			int row, column;

			row = scr->current_workspace / 10;
			column = scr->current_workspace % 10;

			if (command == WKBD_NEXTWSLAYER) {
				if ((row + 1) * 10 < scr->workspace_count)
					wWorkspaceChange(scr, column + (row + 1) * 10);
			} else {
				if (row > 0)
					wWorkspaceChange(scr, column + (row - 1) * 10);
			}
		}
		break;
	case WKBD_CLIPRAISELOWER:
		if (!wdisp->flags.noclip)
			wDockRaiseLower(scr->workspaces[scr->current_workspace]->clip);
		break;
	case WKBD_DOCKRAISELOWER:
		if (!wdisp->flags.nodock)
			wDockRaiseLower(scr->dock);
		break;
#ifdef KEEP_XKB_LOCK_STATUS
	case WKBD_ACCENTPICKER:
		wAccentPickerOpen(scr, wwin, event);
		break;
	case WKBD_TOGGLE:
		if (w_global.config->modelock) {
			/*toggle */
			wwin = scr->focused_window;

			if (wwin && wwin->flags.mapped
			    && WWIN_PRIMARY_WS(wwin) == wwin->screen_ptr->current_workspace
			    && !wwin->flags.miniaturized && !wwin->flags.hidden) {
				xkbgroup = wXkbGetGroup();

				wwin->frame->languagemode = wwin->frame->last_languagemode;
				if (xkbgroup >= 0)
					wwin->frame->last_languagemode = xkbgroup;
				wXkbLockGroup(wwin->frame->languagemode);
				/* Update the language label text */
				wWindowGetLanguageLabel(wwin->frame->languagemode, wwin->frame->language_label);
			}
		}
		break;
#endif	/* KEEP_XKB_LOCK_STATUS */

	case WKBD_MARK_SET:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin))
			startMarkCapture(scr, wdisp->conn, MARK_CAPTURE_SET);
		break;

	case WKBD_MARK_UNSET:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && wwin->mark_key_label != NULL)
			wWindowUnsetMark(wwin);
		break;

	case WKBD_MARK_BRING:
		startMarkCapture(scr, wdisp->conn, MARK_CAPTURE_BRING);
		break;

	case WKBD_MARK_JUMP:
		startMarkCapture(scr, wdisp->conn, MARK_CAPTURE_JUMP);
		break;

	case WKBD_MARK_SWAP:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin))
			startMarkCapture(scr, wdisp->conn, MARK_CAPTURE_SWAP);
		break;
#ifdef VIRTUAL_DESKTOP
	case WKBD_VDESK_LEFT:
		wVdeskKeyboardMoveDesktop(scr, VEC_LEFT);
		break;
	case WKBD_VDESK_RIGHT:
		wVdeskKeyboardMoveDesktop(scr, VEC_RIGHT);
		break;
	case WKBD_VDESK_UP:
		wVdeskKeyboardMoveDesktop(scr, VEC_UP);
		break;
	case WKBD_VDESK_DOWN:
		wVdeskKeyboardMoveDesktop(scr, VEC_DOWN);
		break;
	case WKBD_VDESK_HOME:
		wVdeskHome(scr);
		break;
	case WKBD_VDESK_CENTER:
		wVdeskCenter(scr, wwin);
		break;
	case WKBD_VDESK_PIN:
		wVdeskPin(scr, wwin);
		break;
#endif
	default:
		wwarning("keybind: unhandled command %d", command);
		break;
	}
}

/* ------------------------------------------------------------------ *
 * action name -> WKBD_* command lookup, derived from the same schema
 * rows the configuration keys come from so the two cannot drift.
 * ------------------------------------------------------------------ */

#define WCFG_SCHEMA_KB_ROW(K, name, def, wkbd) { name, wkbd },
static const struct {
	const char *key;
	int command;
} actionNames[] = {
	WCFG_SCHEMA_KEYBINDINGS(WCFG_SCHEMA_KB_ROW)
	{ NULL, -1 }
};
#undef WCFG_SCHEMA_KB_ROW

#define ACTION_NAME_COUNT (sizeof(actionNames) / sizeof(actionNames[0]))

static const char *actionShortNames[ACTION_NAME_COUNT];
/* One NUL-terminated copy of every schema key fits by construction: each
 * row uses at most its key length plus one NUL, so the sum of the
 * sizeof() of all keys is an upper bound. */
#define WCFG_SCHEMA_KB_SIZE(K, name, def, wkbd) sizeof(name) +
static char actionShortBuf[0 + WCFG_SCHEMA_KEYBINDINGS(WCFG_SCHEMA_KB_SIZE) 0];
#undef WCFG_SCHEMA_KB_SIZE
static int actionShortCount = -1;

/* "NextWorkspaceKey" advertises as "NextWorkspace", while rows whose name
 * does not end in "Key" are advertised verbatim. KEYCHAIN_CANCEL is
 * consumed by the key-chain matcher and has no dispatch case, so it is
 * not advertised. */
static void buildActionShortNames(void)
{
	size_t i, used = 0;
	int n = 0;

	if (actionShortCount >= 0)
		return;

	for (i = 0; actionNames[i].key != NULL; i++) {
		const char *key = actionNames[i].key;
		size_t len = strlen(key);

		if (actionNames[i].command == WKBD_KEYCHAIN_CANCEL)
			continue;

		if (len > 3 && strcmp(key + len - 3, "Key") == 0)
			len -= 3;

		if (used + len + 1 > sizeof(actionShortBuf)) {
			wwarning("keybind: action short-name buffer overflow");
			break;
		}

		memcpy(actionShortBuf + used, key, len);
		actionShortBuf[used + len] = '\0';
		actionShortNames[n++] = actionShortBuf + used;
		used += len + 1;
	}

	actionShortCount = n;
}

static int actionNameMatches(const char *key, const char *wanted)
{
	size_t len = strlen(key);

	if (strcmp(key, wanted) == 0)
		return 1;
	if (len > 3 && strcmp(key + len - 3, "Key") == 0
	    && strncmp(key, wanted, len - 3) == 0 && wanted[len - 3] == '\0')
		return 1;
	return 0;
}

int wKeybindForAction(const char *name)
{
	size_t i;

	if (!name)
		return -1;

	for (i = 0; actionNames[i].key != NULL; i++) {
		if (actionNames[i].command == WKBD_KEYCHAIN_CANCEL)
			continue;
		if (actionNameMatches(actionNames[i].key, name))
			return actionNames[i].command;
	}
	return -1;
}

int wKeybindActionCount(void)
{
	buildActionShortNames();
	return actionShortCount;
}

const char *wKeybindActionName(int index)
{
	buildActionShortNames();
	if (index < 0 || index >= actionShortCount)
		return NULL;
	return actionShortNames[index];
}

/* Grouping and description for every schema key-binding row, from the
 * table shared with WPrefs so the two cannot drift. See
 * keybind_descriptions.h. */
static const struct {
	const char *key;
	const char *category;
	const char *description;
} keybindMeta[] = {
#define KEYBIND_META(key, category, description) { key, category, description },
	WCFG_KEYBIND_DESCRIPTIONS(KEYBIND_META)
#undef KEYBIND_META
	{ NULL, NULL, NULL }
};

static void keybindMetaFor(const char *key, const char **category,
			   const char **description)
{
	size_t i;

	for (i = 0; keybindMeta[i].key != NULL; i++) {
		if (strcmp(keybindMeta[i].key, key) == 0) {
			*category = _(keybindMeta[i].category);
			*description = _(keybindMeta[i].description);
			return;
		}
	}

	*category = _("Other");
	*description = NULL;
}

WKeybindInfo *wKeybindCollect(int include_unbound, int *count)
{
	const WConfigEntry *schema = wConfigSchema();
	size_t schema_count = wConfigSchemaCount();
	WKeybindInfo *items;
	size_t i;
	int n = 0, cap = 0;

	*count = 0;

	for (i = 0; i < schema_count; i++)
		if (schema[i].type == WCFG_KEY_BINDING
		    && schema[i].min_value != WKBD_KEYCHAIN_CANCEL)
			cap++;

	if (cap == 0)
		return NULL;

	items = wmalloc(sizeof(*items) * cap);

	for (i = 0; i < schema_count; i++) {
		const WConfigEntry *e = &schema[i];
		WKeybindInfo *info;
		WShortKey *key;
		size_t len;
		const char *name;

		if (e->type != WCFG_KEY_BINDING
		    || e->min_value == WKBD_KEYCHAIN_CANCEL
		    || e->min_value < 0 || e->min_value >= WKBD_LAST)
			continue;

		key = &wKeyBindings[e->min_value];
		info = &items[n];

		len = strlen(e->key);
		if (len > 3 && strcmp(e->key + len - 3, "Key") == 0)
			len -= 3;

		name = wstrndup(e->key, len);
		info->action = name;
		info->config_key = e->key;
		info->spec = (w_global.config
			      && w_global.config->key_bindings[e->min_value].spec)
			? w_global.config->key_bindings[e->min_value].spec
			: e->default_value;
		keybindMetaFor(e->key, &info->category, &info->description);
		if (!info->description)
			info->description = info->action;
		info->label = GetShortcutKey(*key);
		info->bound = info->label != NULL;

		if (!info->bound && !include_unbound) {
			wfree((char *)name);
			continue;
		}

		n++;
	}

	*count = n;
	return items;
}

void wKeybindFree(WKeybindInfo *items, int count)
{
	int i;

	if (!items)
		return;

	for (i = 0; i < count; i++) {
		wfree(items[i].label);
		wfree((char *)items[i].action);
	}
	wfree(items);
}
