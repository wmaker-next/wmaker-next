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
#include <strings.h>

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
#include "wsound.h"
#include "config_schema_public.h"


#define MOD_MASK wdisp->modifier_mask

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
	wXUngrabKeyboard(wdisp->conn, XCB_CURRENT_TIME);
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
	wXUngrabKeyboard(wdisp->conn, XCB_CURRENT_TIME);
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
			w_global.config->keychain_display_position, WOSD_EDGE_PAD);
	WMMapWidget(scr->keychain_osd);
	/* OSD widget, never in the stacking list, direct configure */
	xcb_configure_window(wdisp->conn, WMWidgetXID(scr->keychain_osd), XCB_CONFIG_WINDOW_STACK_MODE, (const uint32_t[]){ XCB_STACK_MODE_ABOVE });
}

void wRefreshKeyChainOSD(WScreen *scr)
{
	showChainOSD(scr);
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
#ifdef KEEP_XKB_LOCK_STATUS
	if (wAccentPickerHandleKeyPress(scr, (xcb_key_press_event_t *)ev))
		return;
#endif
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
		wXUngrabKeyboard(wdisp->conn, XCB_CURRENT_TIME);

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
			wXGrabKeyboard(wdisp->conn, scr->root_win, false,
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
			wKeybindDispatch(act->u.wkbd_idx, scr, wwin, ev);
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

static int extraMouseButtonAction(int button)
{
	const WConfigStringArray *extra = &w_global.config->extra_mouse_buttons;
	const WConfigEnum *actions = wConfigSchemaEnumTable("MouseLeftButtonAction");
	size_t i;

	if (!actions)
		return WA_NONE;

	for (i = 0; i < extra->count; i++) {
		const WConfigEnum *e;
		const char *item = extra->items[i];
		char *end;
		long n;

		if (!item)
			continue;

		n = strtol(item, &end, 10);
		if (end == item || n < 10 || n > 255 || n != button)
			continue;

		while (*end == ' ' || *end == '\t')
			end++;
		if (!*end)
			continue;

		for (e = actions; e->string; e++) {
			if (!e->is_alias && strcasecmp(e->string, end) == 0)
				return e->value;
		}

		wwarning(_("invalid action \"%s\" for button %ld in ExtraMouseButtons"), end, n);
		return WA_NONE;
	}

	return WA_NONE;
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

#ifdef KEEP_XKB_LOCK_STATUS
	if (wAccentPickerHandleButtonPress(scr, (xcb_button_press_event_t *)ev))
		return;
#endif

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
		} else if (e->detail > Button9) {
			int action = extraMouseButtonAction(e->detail);

			if (action != WA_NONE)
				executeButtonAction(scr, ev, action);
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
	desc = WMHashGet(wdisp->context.client_win, (void *)(uintptr_t) ((xcb_button_press_event_t *)ev)->child);
	if (desc == NULL) {
		desc = WMHashGet(wdisp->context.client_win, (void *)(uintptr_t) ((xcb_button_press_event_t *)ev)->event);
		if (desc == NULL) {
			wXAllowEvents(wdisp->conn, XCB_ALLOW_ASYNC_POINTER, XCB_CURRENT_TIME);
			return;
		}
	}

	if (desc->parent_type == WCLASS_WINDOW) {
		wXFlush(wdisp->conn);

		if (e->state & ( MOD_MASK | XCB_MOD_MASK_CONTROL )) {
			wXAllowEvents(wdisp->conn, XCB_ALLOW_ASYNC_POINTER, XCB_CURRENT_TIME);
		} else {
			if (w_global.config->ignore_focus_click) {
				wXAllowEvents(wdisp->conn, XCB_ALLOW_ASYNC_POINTER, XCB_CURRENT_TIME);
			}
			wXAllowEvents(wdisp->conn, XCB_ALLOW_REPLAY_POINTER, XCB_CURRENT_TIME);
		}
		wXFlush(wdisp->conn);
	} else if (desc->parent_type == WCLASS_APPICON
		   || desc->parent_type == WCLASS_MINIWINDOW || desc->parent_type == WCLASS_DOCK_ICON) {
		if (e->state & MOD_MASK) {
			wXFlush(wdisp->conn);
			wXAllowEvents(wdisp->conn, XCB_ALLOW_ASYNC_POINTER, XCB_CURRENT_TIME);
			wXFlush(wdisp->conn);
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

	desc = WMHashGet(wdisp->context.client_win, (void *)(uintptr_t) e->event);
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
		wXSync(wdisp->conn);
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
	if (!scr)
		return;

	if (WXEvqTakeTyped(wdisp->conn, XCB_LEAVE_NOTIFY, e->event, &peek)) {
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

	desc = WMHashGet(wdisp->context.client_win, (void *)(uintptr_t) e->event);
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
	if (!scr)
		return;

	if (e->mode == XCB_NOTIFY_MODE_NORMAL) {
		int corner = wHotCornerForWindow(scr, e->event);

		if (corner) {
			if (!wHotCornerContainsPoint(scr, corner, e->root_x, e->root_y))
				wHotCornersLeave(scr);
			return;
		}
	}

	desc = WMHashGet(wdisp->context.client_win, (void *)(uintptr_t) e->event);
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
	while (WXEvqTakeTyped(wdisp->conn, XCB_FOCUS_IN, XCB_NONE, &junk))
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

#ifdef KEEP_XKB_LOCK_STATUS
static void handleKeyRelease(xcb_generic_event_t *ev)
{
	const xcb_key_release_event_t *e =
		(const xcb_key_release_event_t *)ev;
	WScreen *scr = wScreenForRootWindow(e->root);

	if (scr)
		wAccentPickerHandleKeyRelease(scr,
			(xcb_key_release_event_t *)ev);
}
#endif

void wEventInputRegister(void)
{
	wEventRegisterHandler(XCB_KEY_PRESS, handleKeyPress);
#ifdef KEEP_XKB_LOCK_STATUS
	wEventRegisterHandler(XCB_KEY_RELEASE, handleKeyRelease);
#endif
	wEventRegisterHandler(XCB_BUTTON_PRESS, handleButtonPress);
	wEventRegisterHandler(XCB_MOTION_NOTIFY, handleMotionNotify);
	wEventRegisterHandler(XCB_ENTER_NOTIFY, handleEnterNotify);
	wEventRegisterHandler(XCB_LEAVE_NOTIFY, handleLeaveNotify);
	wEventRegisterHandler(XCB_FOCUS_IN, handleFocusIn);
}
