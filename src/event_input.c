/* event_input.c - keyboard, pointer, crossing and focus events
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
#include "winmenu.h"
#include "layout.h"
#include "switchmenu.h"
#include "wsmap.h"
#include "hotcorner.h"
#include "wsound.h"


#define MOD_MASK wPreferences.modifier_mask

static void hideChainOSD(WScreen *scr);
static void showChainOSD(WScreen *scr);

/* ------------------------------------------------------------------ *
 * Key-chain timeout support                                          *
 *                                                                    *
 * w_global.config->keychain_timeout_delay in milliseconds after a   *
 * chain leader is pressed, the chain is automatically cancelled so  *
 * the user is not stuck in a half-entered sequence. Set to 0 to     *
 * disable.                                                           *
 * ------------------------------------------------------------------ */

/* Cancels the chain on inactivity */
static void chainTimeoutCallback(void *data)
{
	WScreen *scr = (WScreen *)data;

	w_global.shortcut.chain_timeout_handler = NULL;
	hideChainOSD(scr);
	wXUngrabKeyboard(xconn, XCB_CURRENT_TIME);
	w_global.shortcut.curpos = NULL;
}

/* Start (or restart) the chain inactivity timer */
static void wStartChainTimer(WScreen *scr)
{
	if (w_global.config->keychain_timeout_delay > 0) {
		if (w_global.shortcut.chain_timeout_handler)
			WMDeleteTimerHandler(w_global.shortcut.chain_timeout_handler);
		w_global.shortcut.chain_timeout_handler =
			WMAddTimerHandler(w_global.config->keychain_timeout_delay, chainTimeoutCallback, scr);
	}
}

/* Cancel the chain inactivity timer, if armed */
static void wCancelChainTimer(void)
{
	WMDeleteTimerHandlerPtr(&w_global.shortcut.chain_timeout_handler);
}

void wCancelKeyChain(WScreen *scr)
{
	wCancelChainTimer();
	hideChainOSD(scr);
	wXUngrabKeyboard(xconn, XCB_CURRENT_TIME);
	w_global.shortcut.curpos = NULL;
}

/* ------------------------------------------------------------------ *
 * Key-chain prefix OSD                                               *
 *                                                                    *
 * When KeychainDisplayPosition is not "none", entering or advancing *
 * a multi-stroke key chain pops up scr->keychain_osd showing the     *
 * keys typed so far, e.g. "Mod4+W" then "Mod4+W M". The popup is     *
 * hidden again on leaf execution, cancel, unrecognized key, or       *
 * inactivity timeout.                                                *
 * ------------------------------------------------------------------ */

#define CHAIN_OSD_PAD 8
/* The shortcut parser only caps the source string at MAX_SHORTCUT_LENGTH
 * with no explicit step limit, so MAX_SHORTCUT_LENGTH/2 single-character
 * steps is the hard upper bound on chain depth. */
#define CHAIN_OSD_MAX_STEPS (MAX_SHORTCUT_LENGTH / 2)

static void formatChainPosition(WKeyNode *pos, char *buf, size_t bufsz)
{
	WKeyNode *path[CHAIN_OSD_MAX_STEPS];
	WKeyNode *node;
	int depth, i;
	bool truncated = false;

	depth = 0;
	for (node = pos; node != NULL; node = node->parent) {
		if (depth == CHAIN_OSD_MAX_STEPS) {
			truncated = true;
			break;
		}
		path[depth++] = node;
	}

	buf[0] = '\0';
	if (truncated)
		wstrlcat(buf, "... ", bufsz);

	for (i = depth - 1; i >= 0; i--) {
		char *seg;

		seg = GetShortcutLabelForKeycode(path[i]->modifier, path[i]->keycode);
		if (!seg)
			continue;

		if (buf[0] != '\0')
			wstrlcat(buf, " ", bufsz);
		wstrlcat(buf, seg, bufsz);
		wfree(seg);
	}
}

static void hideChainOSD(WScreen *scr)
{
	if (scr && scr->keychain_osd)
		WMUnmapWidget(scr->keychain_osd);
}

static void showChainOSD(WScreen *scr)
{
	char label[MAX_SHORTCUT_LENGTH * 2];

	if (!scr || !scr->keychain_osd)
		return;

	if (w_global.config->keychain_display_position == WD_NONE ||
	    w_global.shortcut.curpos == NULL) {
		hideChainOSD(scr);
		return;
	}

	formatChainPosition(w_global.shortcut.curpos, label, sizeof(label));
	WSetOSDViewText(scr->keychain_osd, label);

	wPlaceOSDOnHead(scr, scr->keychain_osd,
			w_global.config->keychain_display_position, CHAIN_OSD_PAD);
	WMMapWidget(scr->keychain_osd);
	xcb_configure_window(xconn, WMWidgetXID(scr->keychain_osd), XCB_CONFIG_WINDOW_STACK_MODE, (const uint32_t[]){ XCB_STACK_MODE_ABOVE });
}

void wRefreshKeyChainOSD(WScreen *scr)
{
	showChainOSD(scr);
}

static void hideLayoutOSD(void *data)
{
	WScreen *scr = data;

	scr->layout_osd_timer = NULL;
	/* the chord display owns this widget while a chain is armed,
	 * refresh instead of unconditionally hiding */
	if (w_global.shortcut.curpos)
		showChainOSD(scr);
	else
		hideChainOSD(scr);
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
			w_global.config->keychain_display_position, CHAIN_OSD_PAD);
	WMMapWidget(scr->keychain_osd);
	xcb_configure_window(xconn, WMWidgetXID(scr->keychain_osd), XCB_CONFIG_WINDOW_STACK_MODE, (const uint32_t[]){ XCB_STACK_MODE_ABOVE });
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

/* ------------------------------------------------------------------ *
 * keybind command dispatch                                          *
 * ------------------------------------------------------------------ */

static WWindow *windowUnderPointer(WScreen *scr)
{
	unsigned int mask;
	int foo;
	xcb_window_t bar, win;

	if (wXQueryPointer(xconn, scr->root_win, &bar, &win, &foo, &foo, &foo, &foo, &mask))
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

static void dispatchWKBDCommand(int command, WScreen *scr, WWindow *wwin, xcb_generic_event_t *event)
{
	short widx;
	int i;
#ifdef KEEP_XKB_LOCK_STATUS
	int xkbgroup;
#endif

	switch (command) {

	case WKBD_ROOTMENU:
		if (!CheckFullScreenWindowFocused(scr)) {
			WMRect rect = wGetRectForHead(scr, wGetHeadForPointerLocation(scr));
			OpenRootMenu(scr, rect.pos.x + rect.size.width / 2, rect.pos.y + rect.size.height / 2,
				     true);
		}
		break;
	case WKBD_WINDOWLIST:
		if (!CheckFullScreenWindowFocused(scr)) {
			WMRect rect = wGetRectForHead(scr, wGetHeadForPointerLocation(scr));
			OpenSwitchMenu(scr, rect.pos.x + rect.size.width / 2, rect.pos.y + rect.size.height / 2,
				       true);
		}
		break;

	case WKBD_WINDOWMENU:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin))
			OpenWindowMenu(wwin, wwin->frame_x, wwin->frame_y + wwin->frame->top_width, true);
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
				wClientSendProtocol(wwin, w_global.atom.gnustep.wm_miniaturize_window, ((xcb_button_press_event_t *)event)->time);
			else {
				wIconifyWindow(wwin);
			}
		}
		break;
	case WKBD_HIDE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin)) {
			CloseWindowMenu(scr);
			wRequestHideApplication(wApplicationOf(wwin->main_window), wwin);
		}
		break;
	case WKBD_HIDE_OTHERS:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin)) {
			CloseWindowMenu(scr);

			wHideOtherApplications(wwin);
		}
		break;
	case WKBD_SHOW_DESKTOP:
		CloseWindowMenu(scr);
		wNETWMShowingDesktopToggle(scr);
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
			CloseWindowMenu(scr);

			if (wwin->frame->core->stacking->window_level != WMFloatingLevel)
				ChangeStackingLevel(wwin->frame->core, WMFloatingLevel);
			else
				ChangeStackingLevel(wwin->frame->core, WMNormalLevel);
		}
		break;

	case WKBD_KEEP_AT_BOTTOM:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin)) {
			CloseWindowMenu(scr);

			if (wwin->frame->core->stacking->window_level != WMSunkenLevel)
				ChangeStackingLevel(wwin->frame->core, WMSunkenLevel);
			else
				ChangeStackingLevel(wwin->frame->core, WMNormalLevel);
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
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && !WFLAGP(wwin, no_closable)) {
			CloseWindowMenu(scr);
			if (wwin->protocols.DELETE_WINDOW) {
				wClientSendProtocol(wwin, w_global.atom.wm.delete_window, ((xcb_key_press_event_t *)event)->time);
				wNETWMPingWindow(wwin, ((xcb_key_press_event_t *)event)->time);
			}
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
		StartWindozeCycle(wwin, event, true, CYCLE_FLAT);
		break;

	case WKBD_FOCUSPREV:
		StartWindozeCycle(wwin, event, false, CYCLE_FLAT);
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
		StartWindozeCycle(wwin, event, true, CYCLE_CLASS_ONLY);
		break;

	case WKBD_GROUPWINDOWPREV:
		StartWindozeCycle(wwin, event, false, CYCLE_CLASS_ONLY);
		break;

	case WKBD_GROUPNEXT:
		StartWindozeCycle(wwin, event, true, CYCLE_GROUP);
		break;

	case WKBD_GROUPPREV:
		StartWindozeCycle(wwin, event, false, CYCLE_GROUP);
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
			wXFlush(xconn);
			wusleep(3000);
			wSelectWindow(wwin, !wwin->flags.selected);
			wXFlush(xconn);

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

	case WKBD_RUN:
		ExecuteInputCommand(scr, _("exec %A(Find)"));
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
		    w_global.config->clipboard_command[0])
			ExecuteShellCommand(scr, w_global.config->clipboard_command);
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
		if (!wPreferences.flags.noclip)
			wDockRaiseLower(scr->workspaces[scr->current_workspace]->clip);
		break;
	case WKBD_DOCKRAISELOWER:
		if (!wPreferences.flags.nodock)
			wDockRaiseLower(scr->dock);
		break;
#ifdef KEEP_XKB_LOCK_STATUS
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
			startMarkCapture(scr, xconn, MARK_CAPTURE_SET);
		break;

	case WKBD_MARK_UNSET:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && wwin->mark_key_label != NULL)
			wWindowUnsetMark(wwin);
		break;

	case WKBD_MARK_BRING:
		startMarkCapture(scr, xconn, MARK_CAPTURE_BRING);
		break;

	case WKBD_MARK_JUMP:
		startMarkCapture(scr, xconn, MARK_CAPTURE_JUMP);
		break;

	case WKBD_MARK_SWAP:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin))
			startMarkCapture(scr, xconn, MARK_CAPTURE_SWAP);
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
	}
}

static void handleKeyPress(xcb_generic_event_t *ev)
{
	const xcb_key_press_event_t *e = (const xcb_key_press_event_t *)ev;
	WScreen *scr = wScreenForRootWindow(e->root);
	WWindow *wwin;
	WKeyNode  *siblings;
	WKeyNode  *match;
	WKeyAction *act;
	int modifiers;

	if (!scr)
		return;
	wwin = scr->focused_window;

	/* ignore CapsLock */
	modifiers = e->state & w_global.shortcut.modifiers_mask;

	/* ----------------------------------------------------------- *
	* Window mark capture mode                                     *
	*                                                              *
	* We grabbed the keyboard so all keypresses come here until    *
	* we release the grab.                                         *
	* ------------------------------------------------------------ */
	if (scr->flags.mark_capture_mode != MARK_CAPTURE_IDLE) {
		int capture_mode = scr->flags.mark_capture_mode;
		xkb_keysym_t cap_ksym;
		char label[MAX_SHORTCUT_LENGTH];

		/* Skip modifier-only keypresses */
		cap_ksym = wXkbKeycodeToKeysym(e->detail, 0);
		if (cap_ksym == XKB_KEY_NoSymbol || W_IsModifierKey(cap_ksym))
			return;

		/* Real key received: exit capture mode and release grab */
		scr->flags.mark_capture_mode = MARK_CAPTURE_IDLE;
		wXUngrabKeyboard(xconn, XCB_CURRENT_TIME);

		if (!GetCanonicalShortcutLabel(modifiers, cap_ksym, label, sizeof(label)))
			wstrlcpy(label, "?", sizeof(label));

		if (capture_mode == MARK_CAPTURE_SET) {
			WWindow *target = scr->focused_window;
			bool conflict = false;
			int i;

			/* Conflict check against static wmaker bindings */
			for (i = 0; i < WKBD_LAST; i++) {
				if (wKeyBindings[i].keycode == e->detail && wKeyBindings[i].modifier == modifiers) {
					wwarning("window mark: '%s' is already a wmaker binding, mark not assigned", label);
					conflict = true;
					break;
				}
			}

			/* Conflict check against existing marks on other windows */
			if (!conflict) {
				WWindow *tw = scr->focused_window;
				while (tw) {
					if (tw != target && tw->mark_key_label != NULL && strcmp(tw->mark_key_label, label) == 0) {
						wwarning("window mark: label '%s' is already used by another window, mark not assigned", label);
						conflict = true;
						break;
					}
					tw = tw->prev;
				}
			}

			if (!conflict && target != NULL)
				wWindowSetMark(target, label);

		} else {
			/* Find marked window by label */
			WWindow *tw = scr->focused_window;

			while (tw) {
				if (tw->mark_key_label != NULL && strcmp(tw->mark_key_label, label) == 0)
					break;
				tw = tw->prev;
			}
			if (tw == NULL) {
				wwarning("window mark: no window labelled '%s'", label);
			} else if (capture_mode == MARK_CAPTURE_BRING) {
				if (WWIN_PRIMARY_WS(tw) != scr->current_workspace)
					wWindowChangeWorkspace(tw, scr->current_workspace);
				wMakeWindowVisible(tw);
			} else if (capture_mode == MARK_CAPTURE_JUMP) {
				wMakeWindowVisible(tw);
			} else {
				/* MARK_CAPTURE_SWAP: swap position, size and workspace between focused and tw */
				WWindow *focused = scr->focused_window;
				int fx, fy, fw, fh, tx, ty, tw_w, tw_h;
				int f_ws, t_ws;

				if (focused == NULL || focused == tw)
					return;

				/* Snapshot both geometries */
				fx   = focused->frame_x;
				fy   = focused->frame_y;
				fw   = focused->client.width;
				fh   = focused->client.height;
				f_ws = WWIN_PRIMARY_WS(focused);

				tx   = tw->frame_x;
				ty   = tw->frame_y;
				tw_w = tw->client.width;
				tw_h = tw->client.height;
				t_ws = WWIN_PRIMARY_WS(tw);

				/* Swap workspaces first so configure lands in the right one */
				if (f_ws != t_ws) {
					wWindowChangeWorkspace(focused, t_ws);
					wWindowChangeWorkspace(tw, f_ws);
				}

				/* Swap positions and sizes */
				wWindowConfigure(focused, tx, ty, tw_w, tw_h);
				wWindowConfigure(tw, fx, fy, fw, fh);

				/* Follow origin window: switch view to the workspace it landed in,
				 * then restore focus to it */
				if (f_ws != t_ws)
					wWorkspaceChange(scr, t_ws);
				wSetFocusTo(scr, focused);
				warpPointerToFocusedWindow(focused);
			}
		}
		return;
	}

	/* ------------------------------------------------------------------ *
	 * Trie-based key-chain matching                                      *
	 *                                                                    *
	 * wKeyTreeRoot is a prefix trie covering ALL key bindings            *
	 * (wKeyBindings and root-menu shortcuts combined).                   *
	 * curpos tracks the last matched internal node.                      *
	 * NULL means we are at the root (idle).                              *
	 * ------------------------------------------------------------------ */

	if (w_global.shortcut.curpos != NULL) {
		/*  Inside a chain: look for the next key among children */
		if (e->detail == wKeyBindings[WKBD_KEYCHAIN_CANCEL].keycode &&
			modifiers == wKeyBindings[WKBD_KEYCHAIN_CANCEL].modifier) {
			wCancelKeyChain(scr);
			return;
		}

		siblings = w_global.shortcut.curpos->first_child;
		match = wKeyTreeFind(siblings, modifiers, e->detail);

		if (match != NULL && match->first_child != NULL) {
			/* Internal node: advance and keep waiting */
			w_global.shortcut.curpos = match;
			wStartChainTimer(scr);
			showChainOSD(scr);
			return;
		}

		if (match == NULL) {
			/* Unrecognized key inside chain: exit chain mode */
			wCancelKeyChain(scr);
			return;
		}

		/*
		 * Sticky-chain mode: when a KeychainCancelKey is configured,
		 * stay at the parent level after executing a leaf instead of always
		 * returning to root.
		 */
		if (wKeyBindings[WKBD_KEYCHAIN_CANCEL].keycode != 0) {
			WKeyNode *parent = match->parent;
			WKeyNode *child;
			int nchildren = 0;

			for (child = parent->first_child; child != NULL; child = child->next_sibling)
				nchildren++;

			if (nchildren > 1) {
				/* Multi-branch parent: stay in chain mode at this level */
				w_global.shortcut.curpos = parent;
				wStartChainTimer(scr);
				showChainOSD(scr);
			} else {
				/* Single-branch parent: nothing left to wait for, exit chain */
				wCancelKeyChain(scr);
			}
		} else {
			/* No cancel key configured: always exit chain after a leaf */
			wCancelKeyChain(scr);
		}
	} else {
		/* Idle: look for a root-level match */
		match = wKeyTreeFind(wKeyTreeRoot, modifiers, e->detail);

		if (match == NULL) {
			/* Not a known shortcut: try to redispatch it */
			static int dontLoop = 0;

			if (dontLoop > 10) {
				wwarning("problem with key event processing code");
				return;
			}
			dontLoop++;
			if (wwin && wwin->flags.internal_window &&
				wwin->client_leader != XCB_NONE) {
				((xcb_key_press_event_t *)ev)->event = wwin->client_leader;
				WMHandleEvent(ev);
			}
			dontLoop--;

			return;
		}

		if (match->first_child != NULL) {
			/* Internal node: enter chain mode */
			w_global.shortcut.curpos = match;
			wXGrabKeyboard(xconn, scr->root_win, false,
							XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_CURRENT_TIME);
			wStartChainTimer(scr);
			showChainOSD(scr);

			return;
		}
	}

	/* Execute all leaf actions for this key sequence */
	for (act = match->actions; act != NULL; act = act->next) {
		if (act->type == WKN_MENU) {
			WMenu *menu  = (WMenu *) act->u.menu.menu;
			WMenuEntry *entry = (WMenuEntry *) act->u.menu.entry;

			(*entry->callback)(menu, entry);
		} else {
			dispatchWKBDCommand(act->u.wkbd_idx, scr, wwin, ev);
		}
	}
}

/* ------------------------------------------------------------------ *
 * pointer                                                            *
 * ------------------------------------------------------------------ */

static void executeWheelAction(WScreen *scr, int button, int action)
{
	WWindow *wwin;
	bool next_direction;

	if (button == XCB_BUTTON_INDEX_5 || button == Button6)
		next_direction = false;
	else
		next_direction = true;

	switch (action) {
	case WA_SWITCH_WORKSPACES:
		if (next_direction)
			wWorkspaceRelativeChangeFromPointer(scr, 1);
		else
			wWorkspaceRelativeChangeFromPointer(scr, -1);
		break;

	case WA_SWITCH_WINDOWS:
		wwin = scr->focused_window;
		if (next_direction)
			wWindowFocusNext(wwin, true);
		else
			wWindowFocusPrev(wwin, true);
		break;
	}
}

static void executeButtonAction(WScreen *scr, xcb_generic_event_t *event, int action)
{
	WWindow *wwin;

	switch (action) {
	case WA_SELECT_WINDOWS:
		wUnselectWindows(scr);
		wSelectWindows(scr, event);
		if (w_global.config->close_rootmenu_left_right_click) {
			WMenu *menu = scr->root_menu;
			WMPropList *definition;
			if (scr->root_menu) {
				wMenuDestroy(menu, true);
				scr->root_menu = NULL;
				definition = w_global.domain.root_menu->dictionary;
				menu = configureMenu(scr, definition);
				scr->root_menu = menu;
			}
		}
		break;
	case WA_OPEN_APPMENU:
		OpenRootMenu(scr, ((xcb_button_press_event_t *)event)->root_x, ((xcb_button_press_event_t *)event)->root_y, false);
		/* ugly hack */
		if (scr->root_menu) {
			if (scr->root_menu->brother->flags.mapped)
				((xcb_button_press_event_t *)event)->event = scr->root_menu->brother->frame->core->window;
			else
				((xcb_button_press_event_t *)event)->event = scr->root_menu->frame->core->window;
		}
		break;
	case WA_OPEN_WINLISTMENU:
		OpenSwitchMenu(scr, ((xcb_button_press_event_t *)event)->root_x, ((xcb_button_press_event_t *)event)->root_y, false);
		if (scr->switch_menu) {
			if (scr->switch_menu->brother->flags.mapped)
				((xcb_button_press_event_t *)event)->event = scr->switch_menu->brother->frame->core->window;
			else
				((xcb_button_press_event_t *)event)->event = scr->switch_menu->frame->core->window;
		}
		break;
	case WA_MOVE_PREVWORKSPACE:
		wWorkspaceRelativeChangeFromPointer(scr, -1);
		break;
	case WA_MOVE_NEXTWORKSPACE:
		wWorkspaceRelativeChangeFromPointer(scr, 1);
		break;
	case WA_MOVE_PREVWINDOW:
		wwin = scr->focused_window;
		wWindowFocusPrev(wwin, true);
		break;
	case WA_MOVE_NEXTWINDOW:
		wwin = scr->focused_window;
		wWindowFocusNext(wwin, true);
		break;
#ifdef VIRTUAL_DESKTOP
	case WA_VIRTUAL_DESKTOP_PAN:
		wVdeskDragPan(scr, event);
		break;
#endif
	}
}

static void handleButtonPress(xcb_generic_event_t *ev)
{
	const xcb_button_press_event_t *e = (const xcb_button_press_event_t *)ev;
	WObjDescriptor *desc;
	WScreen *scr;

	scr = wScreenForRootWindow(e->root);
	if (!scr)
		return;

#ifdef BALLOON_TEXT
	wBalloonHide(scr);
#endif

	if (!w_global.config->disable_root_mouse && e->event == scr->root_win) {
		if (e->detail == XCB_BUTTON_INDEX_1 && w_global.config->mouse_button1 != WA_NONE) {
			executeButtonAction(scr, ev, w_global.config->mouse_button1);
		} else if (e->detail == XCB_BUTTON_INDEX_2 && w_global.config->mouse_button2 != WA_NONE) {
			executeButtonAction(scr, ev, w_global.config->mouse_button2);
		} else if (e->detail == XCB_BUTTON_INDEX_3 && w_global.config->mouse_button3 != WA_NONE) {
			executeButtonAction(scr, ev, w_global.config->mouse_button3);
		} else if (e->detail == Button8 && w_global.config->mouse_button8 != WA_NONE) {
			executeButtonAction(scr, ev, w_global.config->mouse_button8);
		} else if (e->detail == Button9 && w_global.config->mouse_button9 != WA_NONE) {
			executeButtonAction(scr, ev, w_global.config->mouse_button9);
		} else if (e->detail == XCB_BUTTON_INDEX_4 && w_global.config->mouse_wheel_scroll != WA_NONE) {
			executeWheelAction(scr, e->detail, w_global.config->mouse_wheel_scroll);
		} else if (e->detail == XCB_BUTTON_INDEX_5 && w_global.config->mouse_wheel_scroll != WA_NONE) {
			executeWheelAction(scr, e->detail, w_global.config->mouse_wheel_scroll);
		} else if (e->detail == Button6 && w_global.config->mouse_wheel_tilt != WA_NONE) {
			executeWheelAction(scr, e->detail, w_global.config->mouse_wheel_tilt);
		} else if (e->detail == Button7 && w_global.config->mouse_wheel_tilt != WA_NONE) {
			executeWheelAction(scr, e->detail, w_global.config->mouse_wheel_tilt);
		}
	}

	/* executeButtonAction() may have repointed ((xcb_button_press_event_t *)ev)->event at the
	 * frame of the menu it just mapped so this lookup resolves to the
	 * menu descriptor and reaches menuMouseDown(), the wire bytes in e
	 * are read-only so the lookup must read the cooked event */
	desc = WMHashGet(w_global.context.client_win, (void *)(uintptr_t) ((xcb_button_press_event_t *)ev)->child);
	if (desc == NULL) {
		desc = WMHashGet(w_global.context.client_win, (void *)(uintptr_t) ((xcb_button_press_event_t *)ev)->event);
		if (desc == NULL) {
			wXAllowEvents(xconn, XCB_ALLOW_ASYNC_POINTER, XCB_CURRENT_TIME);
			return;
		}
	}

	if (desc->parent_type == WCLASS_WINDOW) {
		wXFlush(xconn);

		if (e->state & ( MOD_MASK | XCB_MOD_MASK_CONTROL )) {
			wXAllowEvents(xconn, XCB_ALLOW_ASYNC_POINTER, XCB_CURRENT_TIME);
		} else {
			if (w_global.config->ignore_focus_click) {
				wXAllowEvents(xconn, XCB_ALLOW_ASYNC_POINTER, XCB_CURRENT_TIME);
			}
			wXAllowEvents(xconn, XCB_ALLOW_REPLAY_POINTER, XCB_CURRENT_TIME);
		}
		wXFlush(xconn);
	} else if (desc->parent_type == WCLASS_APPICON
		   || desc->parent_type == WCLASS_MINIWINDOW || desc->parent_type == WCLASS_DOCK_ICON) {
		if (e->state & MOD_MASK) {
			wXFlush(xconn);
			wXAllowEvents(xconn, XCB_ALLOW_ASYNC_POINTER, XCB_CURRENT_TIME);
			wXFlush(xconn);
		}
	}

	if (desc->handle_mousedown != NULL) {
		(*desc->handle_mousedown) (desc, ev);
	}

	/* save double-click information */
	if (scr->flags.next_click_is_not_double) {
		scr->flags.next_click_is_not_double = 0;
	} else {
		scr->last_click_time = e->time;
		scr->last_click_button = e->detail;
		scr->last_click_window = e->event;
	}
}

static void handleMotionNotify(xcb_generic_event_t *ev)
{
	const xcb_motion_notify_event_t *e = (const xcb_motion_notify_event_t *)ev;
	WObjDescriptor *desc = NULL;

	desc = WMHashGet(w_global.context.client_win, (void *)(uintptr_t) e->event);
	if (desc != NULL) {
		if (desc->handle_motionnotify)
			(*desc->handle_motionnotify) (desc, ev);
	}

	if (w_global.config->scrollable_menus) {
		WScreen *scr = wScreenForRootWindow(e->root);
		WMPoint p;
		WMRect rect;

		if (!scr)
			return;
		p = wmkpoint(e->root_x, e->root_y);
		rect = wGetRectForHead(scr, wGetHeadForPoint(scr, p));

		if (scr->flags.jump_back_pending ||
		    p.x <= (rect.pos.x + 1) ||
		    p.x >= (rect.pos.x + rect.size.width - 2) ||
		    p.y <= (rect.pos.y + 1) || p.y >= (rect.pos.y + rect.size.height - 2)) {
			WMenu *menu;

			menu = wMenuUnderPointer(scr);
			if (menu != NULL)
				wMenuScroll(menu);
		}
	}
}

/* ------------------------------------------------------------------ *
 * crossing and focus                                                 *
 * ------------------------------------------------------------------ */

static void raiseWindow(WScreen *scr)
{
	WWindow *wwin;

	scr->autoRaiseTimer = NULL;

	wwin = wWindowFor(scr->autoRaiseWindow);
	if (!wwin)
		return;

	if (!wwin->flags.destroyed && wwin->flags.focused) {
		wRaiseFrame(wwin->frame->core);
		/* this is needed or a race condition will occur */
		wXSync(xconn);
	}
}

static void handleEnterNotify(xcb_generic_event_t *ev)
{
	const xcb_enter_notify_event_t *e = (const xcb_enter_notify_event_t *)ev;
	WWindow *wwin;
	WObjDescriptor *desc = NULL;
	xcb_generic_event_t *peek = NULL;
	WScreen *scr = wScreenForRootWindow(e->root);
#ifdef VIRTUAL_DESKTOP
	void (*vdHandler)(xcb_generic_event_t *) = NULL;
#endif

	/* full_sequence is in bounds on a queue-drained event, libxcb
	 * sizes the buffer past the 32 wire bytes and fills it there */
	if (wEventCrossingIgnored(ev->full_sequence))
		return;

	if (WXEvqTakeTyped(xconn, XCB_LEAVE_NOTIFY, e->event, &peek)) {
		/* already left the window..., the lookahead is consumed
		 * either way */
		wSaveTimestamp(peek);
		if ((((xcb_enter_notify_event_t *)peek)->mode) == e->mode && (((xcb_enter_notify_event_t *)peek)->detail) == e->detail) {
			free(peek);
			return;
		}
		free(peek);
	}

	if (e->mode == XCB_NOTIFY_MODE_NORMAL) {
		int c = wHotCornerForWindow(scr, e->event);
		if (c) {
			wHotCornersEnter(scr, c);
			return;
		}
	}

#ifdef VIRTUAL_DESKTOP
	vdHandler = (wVEdgeTable != NULL) ?
		WMHashGet(wVEdgeTable, (void *)(uintptr_t) e->event) : NULL;
	if (vdHandler != NULL) {
		(*vdHandler)(ev);
		return;
	}
#endif

	desc = WMHashGet(w_global.context.client_win, (void *)(uintptr_t) e->event);
	if (desc != NULL) {
		if (desc->handle_enternotify)
			(*desc->handle_enternotify) (desc, ev);
	}

	/* enter to window */
	wwin = wWindowFor(e->event);
	if (!wwin) {
		if (w_global.config->colormap_mode == WCM_POINTER) {
			wColormapInstallForWindow(scr, NULL);
		}
		if (scr->autoRaiseTimer && e->root == e->event) {
			WMDeleteTimerHandler(scr->autoRaiseTimer);
			scr->autoRaiseTimer = NULL;
		}
	} else {
		/* set auto raise timer even if in focus-follows-mouse mode
		 * and the event is for the frame window, even if the window
		 * has focus already. Useful if you move the pointer from a focused
		 * window to the root window and back pretty fast
		 *
		 * set focus if in focus-follows-mouse mode and the event
		 * is for the frame window and window doesn't have focus yet */
		if (w_global.config->focus_mode == WKF_SLOPPY
		    && wwin->frame->core->window == e->event && !scr->flags.doing_alt_tab) {

			if (!wwin->flags.focused && !WFLAGP(wwin, no_focusable))
				wSetFocusTo(scr, wwin);

			WMDeleteTimerHandlerPtr(&scr->autoRaiseTimer);

			if (w_global.config->raise_delay && !WFLAGP(wwin, no_focusable)) {
				scr->autoRaiseWindow = wwin->frame->core->window;
				scr->autoRaiseTimer
				    = WMAddTimerHandler(w_global.config->raise_delay, (WMCallback *) raiseWindow, scr);
			}
		}
		/* Install colormap for window, if the colormap installation mode
		 * is colormap_follows_mouse */
		if (w_global.config->colormap_mode == WCM_POINTER) {
			if (wwin->client_win == e->event)
				wColormapInstallForWindow(scr, wwin);
			else
				wColormapInstallForWindow(scr, NULL);
		}
	}

	if (e->event == e->root
	    && e->detail == XCB_NOTIFY_MODE_NORMAL
	    && e->detail != XCB_NOTIFY_DETAIL_INFERIOR && w_global.config->focus_mode != WKF_CLICK) {

		wSetFocusTo(scr, scr->focused_window);
	}
#ifdef BALLOON_TEXT
	wBalloonEnteredObject(scr, desc);
#endif
}

static void handleLeaveNotify(xcb_generic_event_t *ev)
{
	const xcb_leave_notify_event_t *e = (const xcb_leave_notify_event_t *)ev;
	WObjDescriptor *desc = NULL;
	WScreen *scr = wScreenForRootWindow(e->root);

	if (wEventCrossingIgnored(ev->full_sequence))
		return;

	if (e->mode == XCB_NOTIFY_MODE_NORMAL &&
	    wHotCornerForWindow(scr, e->event)) {
		wHotCornersLeave(scr);
		return;
	}

	desc = WMHashGet(w_global.context.client_win, (void *)(uintptr_t) e->event);
	if (desc != NULL) {
		if (desc->handle_leavenotify)
			(*desc->handle_leavenotify) (desc, ev);
	}
}

static void handleFocusIn(xcb_generic_event_t *ev)
{
	const xcb_focus_in_event_t *e;
	WWindow *wwin;
	xcb_generic_event_t *junk = NULL;

	/* applications that like stealing the focus, only the last
	 * FocusIn currently queued matters */
	while (WXEvqTakeTyped(xconn, XCB_FOCUS_IN, XCB_NONE, &junk))
		free(junk);
	e = (const xcb_focus_in_event_t *)ev;

	if (e->mode == XCB_NOTIFY_MODE_UNGRAB
	    || e->mode == XCB_NOTIFY_MODE_GRAB
	    || e->detail > XCB_NOTIFY_DETAIL_NONLINEAR_VIRTUAL) {
		return;
	}

	wwin = wWindowFor(e->event);
	if (wwin && !wwin->flags.focused) {
		if (wwin->flags.mapped)
			wSetFocusTo(wwin->screen_ptr, wwin);
		else
			wSetFocusTo(wwin->screen_ptr, NULL);
	} else if (!wwin) {
		WScreen *scr = wScreenForWindow(e->event);
		if (scr)
			wSetFocusTo(scr, NULL);
	}
}

void wEventInputRegister(void)
{
	wEventRegisterHandler(XCB_KEY_PRESS, handleKeyPress);
	wEventRegisterHandler(XCB_BUTTON_PRESS, handleButtonPress);
	wEventRegisterHandler(XCB_MOTION_NOTIFY, handleMotionNotify);
	wEventRegisterHandler(XCB_ENTER_NOTIFY, handleEnterNotify);
	wEventRegisterHandler(XCB_LEAVE_NOTIFY, handleLeaveNotify);
	wEventRegisterHandler(XCB_FOCUS_IN, handleFocusIn);
}
