/* cycling.c- window cycling
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 2000-2003 Alfredo K. Kojima
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
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

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
#include <WINGs/WINGs.h>

/* delay in milliseconds before auto-drilling into a group */
#define AUTO_DRILL_DELAY 800

static void raiseWindow(WSwitchPanel * swpanel, WWindow * wwin)
{
	Window swwin = wSwitchPanelGetWindow(swpanel);

	if (wwin->flags.mapped || wwin->flags.shaded) {
		if (swwin != None) {
			Window win[2];

			win[0] = swwin;
			win[1] = wwin->frame->core->window;

			XRestackWindows(dpy, win, 2);
		} else
			XRaiseWindow(dpy, wwin->frame->core->window);
		wHotCornersRaise(wwin->screen_ptr);
	}
}

/* base-level letter keysym of a keycode, or NoSymbol */
static int cycle_letter(KeyCode keycode)
{
	xkb_keysym_t ks = wXkbKeycodeToKeysym(keycode, 0);

	if ((ks >= XK_a && ks <= XK_z) || (ks >= XK_A && ks <= XK_Z))
		return (int) ks;
	return NoSymbol;
}

static WWindow *change_focus_and_raise(WWindow *newFocused, WWindow *oldFocused,
				       WSwitchPanel *swpanel, WScreen *scr, Bool esc_cancel)
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

void StartWindozeCycle(WWindow *wwin, XEvent *event, Bool next, CycleMode mode)
{

	WShortKey binding, fwd_binding, bwd_binding;
	WSwitchPanel    *swpanel       = NULL;
	WScreen         *scr           = wScreenForRootWindow(event->xkey.root);
	KeyCode         leftKey        = wXkbKeysymToKeycode(XK_Left);
	KeyCode         rightKey       = wXkbKeysymToKeycode(XK_Right);
	KeyCode         upKey         = wXkbKeysymToKeycode(XK_Up);
	KeyCode         downKey       = wXkbKeysymToKeycode(XK_Down);
	KeyCode         graveKey      = wXkbKeysymToKeycode(XK_grave);
	KeyCode         homeKey        = wXkbKeysymToKeycode(XK_Home);
	KeyCode         endKey         = wXkbKeysymToKeycode(XK_End);
	KeyCode         shiftLKey      = wXkbKeysymToKeycode(XK_Shift_L);
	KeyCode         shiftRKey      = wXkbKeysymToKeycode(XK_Shift_R);
	KeyCode         ctrlLKey       = wXkbKeysymToKeycode(XK_Control_L);
	KeyCode         ctrlRKey       = wXkbKeysymToKeycode(XK_Control_R);
	KeyCode         altLKey        = wXkbKeysymToKeycode(XK_Alt_L);
	KeyCode         altRKey        = wXkbKeysymToKeycode(XK_Alt_R);
	KeyCode         superLKey      = wXkbKeysymToKeycode(XK_Super_L);
	KeyCode         superRKey      = wXkbKeysymToKeycode(XK_Super_R);
	KeyCode         metaLKey       = wXkbKeysymToKeycode(XK_Meta_L);
	KeyCode         metaRKey      = wXkbKeysymToKeycode(XK_Meta_R);
	KeyCode         hyperLKey      = wXkbKeysymToKeycode(XK_Hyper_L);
	KeyCode         hyperRKey      = wXkbKeysymToKeycode(XK_Hyper_R);
	KeyCode         escapeKey      = wXkbKeysymToKeycode(XK_Escape);
	KeyCode         returnKey      = wXkbKeysymToKeycode(XK_Return);
	Bool            esc_cancel     = False;
	Bool            somethingElse  = False;
	Bool            done           = False;
	Bool            autoDrilled    = False;
	Bool            hasModifier;
	int             modifiers;
	WWindow         *newFocused;
	WWindow         *oldFocused;
	XEvent          ev;


	if (!wwin)
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
		XGrabKeyboard(dpy, scr->root_win, False, GrabModeAsync, GrabModeAsync, CurrentTime);

	scr->flags.doing_alt_tab = 1;

	swpanel = wInitSwitchPanel(scr, wwin, mode);
	oldFocused = wwin;

	if (swpanel) {
		if (wwin->flags.mapped && !w_global.config->panel_only_open)
			newFocused = wSwitchPanelSelectNext(swpanel, !next, True, mode, STRIP_MAIN);
		else
			newFocused = wSwitchPanelSelectFirst(swpanel, False, STRIP_MAIN);

		oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, False);
	} else {
		if (wWindowIsVisibleOnAnyHead(wwin))
			newFocused = wwin;
		else
			newFocused = NULL;
	}

	while (hasModifier && !done) {
		long eventMask = KeyPressMask | KeyReleaseMask | ExposureMask
				 | PointerMotionMask | ButtonReleaseMask | EnterWindowMask;
		Bool gotEvent;

		if (!swpanel)
			break;

		if (mode == CYCLE_GROUP && !wSwitchPanelIsDrilled(swpanel) && !wSwitchPanelIsDrillPreview(swpanel) && !autoDrilled)
			gotEvent = wMaskEventTimed(dpy, eventMask, None, &ev, AUTO_DRILL_DELAY);
		else
			gotEvent = wMaskEventTimed(dpy, eventMask, None, &ev, -1);

		if (!gotEvent) {
			if (mode == CYCLE_GROUP && !wSwitchPanelIsDrilled(swpanel) && !wSwitchPanelIsDrillPreview(swpanel) && !autoDrilled) {
				newFocused = wSwitchPanelPreviewDrill(swpanel);
				autoDrilled = True;
			}
			continue;
		}

		/* mask to the modifier bits the bindings actually use, so stray
		 * CapsLock/NumLock bits don't break the comparison */
		modifiers = ev.xkey.state & (fwd_binding.modifier | bwd_binding.modifier | ShiftMask);

		switch (ev.type) {
		case KeyPress:
			if ((fwd_binding.keycode == ev.xkey.keycode
			     && fwd_binding.modifier == modifiers)
			    || (mode != CYCLE_GROUP && ev.xkey.keycode == rightKey)) {

				newFocused = wSwitchPanelSelectNext(swpanel, False, ev.xkey.keycode != rightKey, mode, STRIP_MAIN);
				oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, False);
				if (mode == CYCLE_GROUP && (wSwitchPanelIsDrilled(swpanel) || wSwitchPanelIsDrillPreview(swpanel))) {
					newFocused = wSwitchPanelRefreshSub(swpanel);
					oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, False);
				}
				autoDrilled = False;

			} else if ((bwd_binding.keycode == ev.xkey.keycode
				    && bwd_binding.modifier == modifiers)
				   || (mode != CYCLE_GROUP && ev.xkey.keycode == leftKey)) {

				newFocused = wSwitchPanelSelectNext(swpanel, True, ev.xkey.keycode != leftKey, mode, STRIP_MAIN);
				oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, False);
				if (mode == CYCLE_GROUP && (wSwitchPanelIsDrilled(swpanel) || wSwitchPanelIsDrillPreview(swpanel))) {
					newFocused = wSwitchPanelRefreshSub(swpanel);
					oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, False);
				}
				autoDrilled = False;

			} else if (mode == CYCLE_GROUP && (ev.xkey.keycode == rightKey || ev.xkey.keycode == leftKey)) {

				/* Arrows act on the focused strip: sub when drilled, main
				 * otherwise. On the main strip they switch groups, on the
				 * sub strip they cycle the drilled group's windows. */
				Strip s = wSwitchPanelIsDrilled(swpanel) ? STRIP_SUB : STRIP_MAIN;

				newFocused = wSwitchPanelSelectNext(swpanel, ev.xkey.keycode == leftKey, False, mode, s);
				oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, False);
				if (s == STRIP_MAIN && (wSwitchPanelIsDrilled(swpanel) || wSwitchPanelIsDrillPreview(swpanel))) {
					newFocused = wSwitchPanelRefreshSub(swpanel);
					oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, False);
				}
				autoDrilled = False;

			} else if (ev.xkey.keycode == homeKey || ev.xkey.keycode == endKey) {

				Strip s = wSwitchPanelIsDrilled(swpanel) ? STRIP_SUB : STRIP_MAIN;

				newFocused = wSwitchPanelSelectFirst(swpanel, ev.xkey.keycode != homeKey, s);
				oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, False);
				if (s == STRIP_MAIN) {
					if (wSwitchPanelIsDrilled(swpanel) || wSwitchPanelIsDrillPreview(swpanel)) {
						newFocused = wSwitchPanelRefreshSub(swpanel);
						oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, False);
					}
					autoDrilled = False;
				}

			} else if (ev.xkey.keycode == escapeKey) {

				/* Focus the first window of the swpanel, despite the 'False' */
				newFocused = wSwitchPanelSelectFirst(swpanel, False, STRIP_MAIN);
				oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, True);
				esc_cancel = True;
				done = True;

			} else if (ev.xkey.keycode == returnKey) {

				/* Close the switch panel without eating the keypress */
				done = True;

			} else if (mode == CYCLE_GROUP
				   && (ev.xkey.keycode == graveKey || ev.xkey.keycode == downKey)) {

				if (wSwitchPanelIsDrilled(swpanel)) {
					int back = (modifiers & ShiftMask) != 0;

					/* CYCLE_GROUP sub-strip entries all
					 * share the group class, so the
					 * class-skip is a no-op here. */
					newFocused = wSwitchPanelSelectNext(swpanel, back, False, CYCLE_GROUP, STRIP_SUB);
				} else {
					/* transition from preview to drilled */
					if (wSwitchPanelIsDrillPreview(swpanel))
						wSwitchPanelUndrill(swpanel);
					newFocused = wSwitchPanelDrill(swpanel);
				}
				oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, False);

			} else if (mode == CYCLE_GROUP && ev.xkey.keycode == upKey) {

				if (wSwitchPanelIsDrilled(swpanel) || wSwitchPanelIsDrillPreview(swpanel)) {
					newFocused = wSwitchPanelUndrill(swpanel);
					oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, False);
				}
				autoDrilled = True;

			} else if (ev.xkey.keycode != shiftLKey && ev.xkey.keycode != shiftRKey &&
				   ev.xkey.keycode != ctrlLKey && ev.xkey.keycode != ctrlRKey &&
				   ev.xkey.keycode != altLKey && ev.xkey.keycode != altRKey &&
				   ev.xkey.keycode != superLKey && ev.xkey.keycode != superRKey &&
				   ev.xkey.keycode != metaLKey && ev.xkey.keycode != metaRKey &&
				   ev.xkey.keycode != hyperLKey && ev.xkey.keycode != hyperRKey) {
				int letter = cycle_letter(ev.xkey.keycode);

				if (letter != NoSymbol) {
					int back = (modifiers & ShiftMask) != 0;
					Strip s = wSwitchPanelIsDrilled(swpanel) ? STRIP_SUB : STRIP_MAIN;

					newFocused = wSwitchPanelSelectByKey(swpanel, letter, back, s);
					if (newFocused)
						oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, False);
					if (s == STRIP_MAIN) {
						if (wSwitchPanelIsDrilled(swpanel) || wSwitchPanelIsDrillPreview(swpanel)) {
							newFocused = wSwitchPanelRefreshSub(swpanel);
							oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, False);
						}
						autoDrilled = False;
					}
				} else {
					somethingElse = True;
					done = True;
				}
			}
			break;

		case KeyRelease:
			if (ev.xkey.keycode == shiftLKey || ev.xkey.keycode == shiftRKey)
				if (w_global.config->strict_windoze_cycle)
					break;

			if (ev.xkey.keycode == leftKey || ev.xkey.keycode == rightKey)
				break;

			if (ev.xkey.keycode == downKey || ev.xkey.keycode == upKey)
				break;

			if (mode == CYCLE_GROUP && ev.xkey.keycode == graveKey)
				break;

			if (ev.xkey.keycode == returnKey)
				break;

			/* a lookup letter's release is part of the consumed keystroke */
			if (cycle_letter(ev.xkey.keycode) != NoSymbol)
				break;

			if (ev.xkey.keycode != binding.keycode)
				done = True;

			break;

		case EnterNotify:

			/* ignore unwanted EnterNotify's */
			break;

		case LeaveNotify:
		case MotionNotify:

		case ButtonRelease:
			{
				WWindow *tmp;
				tmp = wSwitchPanelHandleEvent(swpanel, &ev);
				if (tmp) {
					newFocused = tmp;
					oldFocused = change_focus_and_raise(newFocused, oldFocused, swpanel, scr, False);
					if (!wSwitchPanelIsDrilled(swpanel) && !wSwitchPanelIsDrillPreview(swpanel))
						autoDrilled = False;

					if (ev.type == ButtonRelease)
						done = True;
				}
			}
			break;

		default:
			WMHandleEvent(&ev);
			break;
		}
	}

	if (hasModifier)
		XUngrabKeyboard(dpy, CurrentTime);

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

	if (somethingElse)
		WMHandleEvent(&ev);
}
