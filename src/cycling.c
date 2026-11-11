/* cycling.c- window cycling
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 2000-2003 Alfredo K. Kojima
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

#include <poll.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "WindowMaker.h"
#include "GNUstep.h"
#include "screen.h"
#include "window.h"
#include "framewin.h"
#include "keybind.h"
#include "actions.h"
#include "wxkb.h"
#include "stacking.h"
#include "cycling.h"
#include "tabgroup.h"
#include "xinerama.h"
#include "misc.h"
#include "workspace.h"
#include "switchpanel.h"
#include "hotcorner.h"
#include "ipc.h"
#include <WINGs/WINGs.h>

/* delay in milliseconds before auto-drilling into a group */
#define AUTO_DRILL_DELAY 800

static bool cycle_take_event(WScreen *scr, uint32_t mask, xcb_generic_event_t **event, int timeout_ms)
{
	struct timespec ts;
	long long deadline = -1, now;
	int rc;

	if (timeout_ms >= 0) {
		clock_gettime(CLOCK_MONOTONIC, &ts);
		deadline = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000 + timeout_ms;
	}
	while (!WXEvqTakeMask(wdisp->conn, mask, XCB_NONE, event)) {
		if (deadline >= 0) {
			clock_gettime(CLOCK_MONOTONIC, &ts);
			now = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
			if (now >= deadline)
				return false;
			timeout_ms = (int)(deadline - now);
		}
		rc = wIPCWaitForCycle(scr, timeout_ms);
		if (rc == 0 || (rc < 0 && errno != EINTR) ||
		    xcb_connection_has_error(wdisp->conn))
			return false;
	}
	return true;
}

static void raiseWindow(WSwitchPanel * swpanel, WWindow * wwin)
{
	xcb_window_t swwin = wSwitchPanelGetWindow(swpanel);

	if (wwin->flags.mapped || wwin->flags.shaded) {
		if (swwin != XCB_NONE) {
			xcb_window_t win[2];

			win[0] = swwin;
			win[1] = wwin->frame->core->window;

			wXRestackWindows(wdisp->conn, win, 2);
		} else {
			/* the cycle raise goes through the deferred
			 * stacking list */
			wRaiseFrame(wwin->frame->core);
			CommitStacking(wwin->screen_ptr);
		}
		wHotCornersRaise(wwin->screen_ptr);
	}
}

/* base-level letter keysym of a keycode, or NoSymbol */
static int cycle_letter(xcb_keycode_t keycode)
{
	xkb_keysym_t ks = wXkbKeycodeToKeysym(keycode, 0);

	if ((ks >= XKB_KEY_a && ks <= XKB_KEY_z) || (ks >= XKB_KEY_A && ks <= XKB_KEY_Z))
		return (int) ks;
	return XKB_KEY_NoSymbol;
}

static WWindow *change_focus_and_raise(WWindow *newFocused, WWindow *oldFocused,
				       WSwitchPanel *swpanel, WScreen *scr, bool esc_cancel)
{
	if (!newFocused)
		return oldFocused;

	/* allow the focused window to float on top of a fullscreen window */
	if (oldFocused->flags.fullscreen)
		ChangeStackingLevel(oldFocused->frame->core, WMNormalLevel);

	wWindowFocus(newFocused, oldFocused);
	oldFocused = newFocused;

	if (w_global.config->circ_raise) {
		CommitStacking(scr);

		if (!esc_cancel)
			raiseWindow(swpanel, newFocused);
	}

	return oldFocused;
}

void StartWindozeCycle(WScreen *scr, WWindow *wwin, bool next, CycleMode mode)
{

	WShortKey binding, fwd_binding, bwd_binding;
	WSwitchPanel    *swpanel       = NULL;
	xcb_keycode_t         leftKey        = wXkbKeysymToKeycode(XKB_KEY_Left);
	xcb_keycode_t         rightKey       = wXkbKeysymToKeycode(XKB_KEY_Right);
	xcb_keycode_t         upKey         = wXkbKeysymToKeycode(XKB_KEY_Up);
	xcb_keycode_t         downKey       = wXkbKeysymToKeycode(XKB_KEY_Down);
	xcb_keycode_t         graveKey      = wXkbKeysymToKeycode(XKB_KEY_grave);
	xcb_keycode_t         homeKey        = wXkbKeysymToKeycode(XKB_KEY_Home);
	xcb_keycode_t         endKey         = wXkbKeysymToKeycode(XKB_KEY_End);
	xcb_keycode_t         shiftLKey      = wXkbKeysymToKeycode(XKB_KEY_Shift_L);
	xcb_keycode_t         shiftRKey      = wXkbKeysymToKeycode(XKB_KEY_Shift_R);
	xcb_keycode_t         ctrlLKey       = wXkbKeysymToKeycode(XKB_KEY_Control_L);
	xcb_keycode_t         ctrlRKey       = wXkbKeysymToKeycode(XKB_KEY_Control_R);
	xcb_keycode_t         altLKey        = wXkbKeysymToKeycode(XKB_KEY_Alt_L);
	xcb_keycode_t         altRKey        = wXkbKeysymToKeycode(XKB_KEY_Alt_R);
	xcb_keycode_t         superLKey      = wXkbKeysymToKeycode(XKB_KEY_Super_L);
	xcb_keycode_t         superRKey      = wXkbKeysymToKeycode(XKB_KEY_Super_R);
	xcb_keycode_t         metaLKey       = wXkbKeysymToKeycode(XKB_KEY_Meta_L);
	xcb_keycode_t         metaRKey      = wXkbKeysymToKeycode(XKB_KEY_Meta_R);
	xcb_keycode_t         hyperLKey      = wXkbKeysymToKeycode(XKB_KEY_Hyper_L);
	xcb_keycode_t         hyperRKey      = wXkbKeysymToKeycode(XKB_KEY_Hyper_R);
	xcb_keycode_t         escapeKey      = wXkbKeysymToKeycode(XKB_KEY_Escape);
	xcb_keycode_t         returnKey      = wXkbKeysymToKeycode(XKB_KEY_Return);
	bool            esc_cancel     = false;
	bool            somethingElse  = false;
	bool            done           = false;
	bool            autoDrilled    = false;
	bool            hasModifier;
	int             modifiers;
	WWindow         *newFocused;
	WWindow         *oldFocused;
	xcb_generic_event_t *ev = NULL;


	if (!wwin || scr->flags.doing_alt_tab)
		return;

	switch (mode) {
	case CYCLE_GROUP:
		fwd_binding = wKeyBindings[WKBD_GROUPNEXT];
		bwd_binding = wKeyBindings[WKBD_GROUPPREV];
		break;
	case CYCLE_CLASS_ONLY:
		fwd_binding = wKeyBindings[WKBD_GROUPWINDOWNEXT];
		bwd_binding = wKeyBindings[WKBD_GROUPWINDOWPREV];
		break;
	default: /* CYCLE_FLAT */
		fwd_binding = wKeyBindings[WKBD_FOCUSNEXT];
		bwd_binding = wKeyBindings[WKBD_FOCUSPREV];
		break;
	}
	binding = next ? fwd_binding : bwd_binding;

	hasModifier = (binding.modifier != 0);
	if (hasModifier)
		wXGrabKeyboard(wdisp->conn, scr->root_win, false, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_CURRENT_TIME);

	scr->flags.doing_alt_tab = 1;

	swpanel = wInitSwitchPanel(scr, wwin, mode);
	oldFocused = wwin;

	if (swpanel) {
		if (wwin->flags.mapped && !w_global.config->panel_only_open)
			newFocused = wSwitchPanelSelectNext(swpanel, !next, true, mode, STRIP_MAIN);
		else
			newFocused = wSwitchPanelSelectFirst(swpanel, false, STRIP_MAIN);

		oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, false);
	} else {
		if (wWindowIsVisibleOnAnyHead(wwin))
			newFocused = wwin;
		else
			newFocused = NULL;
	}

	while (hasModifier && !done) {
		long eventMask = XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_EXPOSURE
				 | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_ENTER_WINDOW;
		bool gotEvent;

		if (!swpanel)
			break;

		free(ev);
		ev = NULL;
		if (mode == CYCLE_GROUP && !wSwitchPanelIsDrilled(swpanel) && !wSwitchPanelIsDrillPreview(swpanel) && !autoDrilled)
			gotEvent = cycle_take_event(scr, eventMask, &ev, AUTO_DRILL_DELAY);
		else
			gotEvent = cycle_take_event(scr, eventMask, &ev, -1);

		if (!gotEvent) {
			if (mode == CYCLE_GROUP && !wSwitchPanelIsDrilled(swpanel) && !wSwitchPanelIsDrillPreview(swpanel) && !autoDrilled) {
				newFocused = wSwitchPanelPreviewDrill(swpanel);
				autoDrilled = true;
			}
			continue;
		}

		/* mask to the modifier bits the bindings actually use, so stray
		 * CapsLock/NumLock bits don't break the comparison */
		modifiers = (((xcb_key_press_event_t *)ev)->state) & (fwd_binding.modifier | bwd_binding.modifier | XCB_MOD_MASK_SHIFT);

		switch ((ev->response_type & 0x7f)) {
		case XCB_KEY_PRESS:
			if ((fwd_binding.keycode == (((xcb_key_press_event_t *)ev)->detail)
			     && fwd_binding.modifier == modifiers)
			    || (mode != CYCLE_GROUP && (((xcb_key_press_event_t *)ev)->detail) == rightKey)) {

				newFocused = wSwitchPanelSelectNext(swpanel, false, (((xcb_key_press_event_t *)ev)->detail) != rightKey, mode, STRIP_MAIN);
				oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, false);
				if (mode == CYCLE_GROUP && (wSwitchPanelIsDrilled(swpanel) || wSwitchPanelIsDrillPreview(swpanel))) {
					newFocused = wSwitchPanelRefreshSub(swpanel);
					oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, false);
				}
				autoDrilled = false;

			} else if ((bwd_binding.keycode == (((xcb_key_press_event_t *)ev)->detail)
				    && bwd_binding.modifier == modifiers)
				   || (mode != CYCLE_GROUP && (((xcb_key_press_event_t *)ev)->detail) == leftKey)) {

				newFocused = wSwitchPanelSelectNext(swpanel, true, (((xcb_key_press_event_t *)ev)->detail) != leftKey, mode, STRIP_MAIN);
				oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, false);
				if (mode == CYCLE_GROUP && (wSwitchPanelIsDrilled(swpanel) || wSwitchPanelIsDrillPreview(swpanel))) {
					newFocused = wSwitchPanelRefreshSub(swpanel);
					oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, false);
				}
				autoDrilled = false;

			} else if (mode == CYCLE_GROUP && ((((xcb_key_press_event_t *)ev)->detail) == rightKey || (((xcb_key_press_event_t *)ev)->detail) == leftKey)) {

				/* Arrows act on the focused strip: sub when drilled, main
				 * otherwise. On the main strip they switch groups, on the
				 * sub strip they cycle the drilled group's windows. */
				Strip s = wSwitchPanelIsDrilled(swpanel) ? STRIP_SUB : STRIP_MAIN;

				newFocused = wSwitchPanelSelectNext(swpanel, (((xcb_key_press_event_t *)ev)->detail) == leftKey, false, mode, s);
				oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, false);
				if (s == STRIP_MAIN && (wSwitchPanelIsDrilled(swpanel) || wSwitchPanelIsDrillPreview(swpanel))) {
					newFocused = wSwitchPanelRefreshSub(swpanel);
					oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, false);
				}
				autoDrilled = false;

			} else if ((((xcb_key_press_event_t *)ev)->detail) == homeKey || (((xcb_key_press_event_t *)ev)->detail) == endKey) {

				Strip s = wSwitchPanelIsDrilled(swpanel) ? STRIP_SUB : STRIP_MAIN;

				newFocused = wSwitchPanelSelectFirst(swpanel, (((xcb_key_press_event_t *)ev)->detail) != homeKey, s);
				oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, false);
				if (s == STRIP_MAIN) {
					if (wSwitchPanelIsDrilled(swpanel) || wSwitchPanelIsDrillPreview(swpanel)) {
						newFocused = wSwitchPanelRefreshSub(swpanel);
						oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, false);
					}
					autoDrilled = false;
				}

			} else if ((((xcb_key_press_event_t *)ev)->detail) == escapeKey) {

				/* Focus the first window of the swpanel, despite the 'False' */
				newFocused = wSwitchPanelSelectFirst(swpanel, false, STRIP_MAIN);
				oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, true);
				esc_cancel = true;
				done = true;

			} else if ((((xcb_key_press_event_t *)ev)->detail) == returnKey) {

				/* Close the switch panel without eating the keypress */
				done = true;

			} else if (mode == CYCLE_GROUP
				   && ((((xcb_key_press_event_t *)ev)->detail) == graveKey || (((xcb_key_press_event_t *)ev)->detail) == downKey)) {

				if (wSwitchPanelIsDrilled(swpanel)) {
					int back = (modifiers & XCB_MOD_MASK_SHIFT) != 0;

					/* CYCLE_GROUP sub-strip entries all
					 * share the group class, so the
					 * class-skip is a no-op here. */
					newFocused = wSwitchPanelSelectNext(swpanel, back, false, CYCLE_GROUP, STRIP_SUB);
				} else {
					/* transition from preview to drilled */
					newFocused = wSwitchPanelDrill(swpanel);
				}
				oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, false);

			} else if (mode == CYCLE_GROUP && (((xcb_key_press_event_t *)ev)->detail) == upKey) {

				if (wSwitchPanelIsDrilled(swpanel) || wSwitchPanelIsDrillPreview(swpanel)) {
					newFocused = wSwitchPanelUndrill(swpanel);
					oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, false);
				}
				autoDrilled = true;

			} else if ((((xcb_key_press_event_t *)ev)->detail) != shiftLKey && (((xcb_key_press_event_t *)ev)->detail) != shiftRKey &&
				   (((xcb_key_press_event_t *)ev)->detail) != ctrlLKey && (((xcb_key_press_event_t *)ev)->detail) != ctrlRKey &&
				   (((xcb_key_press_event_t *)ev)->detail) != altLKey && (((xcb_key_press_event_t *)ev)->detail) != altRKey &&
				   (((xcb_key_press_event_t *)ev)->detail) != superLKey && (((xcb_key_press_event_t *)ev)->detail) != superRKey &&
				   (((xcb_key_press_event_t *)ev)->detail) != metaLKey && (((xcb_key_press_event_t *)ev)->detail) != metaRKey &&
				   (((xcb_key_press_event_t *)ev)->detail) != hyperLKey && (((xcb_key_press_event_t *)ev)->detail) != hyperRKey) {
				int letter = cycle_letter((((xcb_key_press_event_t *)ev)->detail));

				if (letter != XKB_KEY_NoSymbol) {
					int back = (modifiers & XCB_MOD_MASK_SHIFT) != 0;
					Strip s = wSwitchPanelIsDrilled(swpanel) ? STRIP_SUB : STRIP_MAIN;

					newFocused = wSwitchPanelSelectByKey(swpanel, letter, back, s);
					if (newFocused)
						oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, false);
					if (s == STRIP_MAIN) {
						if (wSwitchPanelIsDrilled(swpanel) || wSwitchPanelIsDrillPreview(swpanel)) {
							newFocused = wSwitchPanelRefreshSub(swpanel);
							oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, false);
						}
						autoDrilled = false;
					}
				} else {
					somethingElse = true;
					done = true;
				}
			}
			break;

		case XCB_KEY_RELEASE:
			if ((((xcb_key_press_event_t *)ev)->detail) == shiftLKey || (((xcb_key_press_event_t *)ev)->detail) == shiftRKey)
				if (w_global.config->strict_windoze_cycle)
					break;

			if ((((xcb_key_press_event_t *)ev)->detail) == leftKey || (((xcb_key_press_event_t *)ev)->detail) == rightKey)
				break;

			if ((((xcb_key_press_event_t *)ev)->detail) == downKey || (((xcb_key_press_event_t *)ev)->detail) == upKey)
				break;

			if (mode == CYCLE_GROUP && (((xcb_key_press_event_t *)ev)->detail) == graveKey)
				break;

			if ((((xcb_key_press_event_t *)ev)->detail) == returnKey)
				break;

			/* a lookup letter's release is part of the consumed keystroke */
			if (cycle_letter((((xcb_key_press_event_t *)ev)->detail)) != XKB_KEY_NoSymbol)
				break;

			if ((((xcb_key_press_event_t *)ev)->detail) != binding.keycode)
				done = true;

			break;

		case XCB_ENTER_NOTIFY:

			/* ignore unwanted EnterNotify's */
			break;

		case XCB_LEAVE_NOTIFY:
		case XCB_MOTION_NOTIFY:

		case XCB_BUTTON_RELEASE:
			{
				WWindow *tmp;
				tmp = wSwitchPanelHandleEvent(swpanel, ev);
				if (tmp) {
					newFocused = tmp;
					oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, false);
					if (!wSwitchPanelIsDrilled(swpanel) && !wSwitchPanelIsDrillPreview(swpanel))
						autoDrilled = false;

					if ((ev->response_type & 0x7f) == XCB_BUTTON_RELEASE)
						done = true;
				}
			}
			break;

		default:
			WMHandleEvent(ev);
			break;
		}

		/* the somethingElse event is re-dispatched after the loop;
		 * keep its buffer until then */
		if (!somethingElse) {
			free(ev);
			ev = NULL;
		}
	}

	if (hasModifier)
		wXUngrabKeyboard(wdisp->conn, XCB_CURRENT_TIME);

	if (swpanel)
		wSwitchPanelDestroy(swpanel);

	if (newFocused && !esc_cancel) {
		if (newFocused->tab_group && newFocused->tab_group_index != newFocused->tab_group->active_index)
			wGroupSetActive(newFocused->tab_group, newFocused->tab_group_index);

		wRaiseFrame(newFocused->frame->core);
		CommitStacking(scr);
		if (!newFocused->flags.mapped)
			wMakeWindowVisible(newFocused);
		wSetFocusTo(scr, newFocused);
		warpPointerToFocusedWindow(newFocused);
	}

	scr->flags.doing_alt_tab = 0;

	if (somethingElse) {
		WMHandleEvent(ev);
		free(ev);
	}
}
