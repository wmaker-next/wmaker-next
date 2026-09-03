/*
 *  WXkb - per-screen xkbcommon keyboard state for the xcb event path
 *
 *  Mirrors src/wxkb.c: the keymap is loaded from the server with
 *  xkb_x11_* and rebuilt on XCB_XKB_NEW_KEYBOARD_NOTIFY / MAP_NOTIFY,
 *  the modifier state is tracked from XCB_XKB_STATE_NOTIFY. The
 *  refresh is driven by W_XkbHandleEvent() from WMHandleEvent()'s
 *  extension pass, so the same event stream feeds the wmaker-side and
 *  the WINGs-side states.
 *
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

#include "wconfig.h"

#include <locale.h>
#include <stdlib.h>
#include <string.h>

#include <xcb/xcb.h>
#include <xcb/xkb.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-x11.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "WINGsP.h"

typedef struct W_XkbState {
	struct xkb_context *ctx;
	struct xkb_keymap *keymap;
	struct xkb_state *state;
	uint8_t base_event;
	int32_t device_id;
} W_XkbState;

/* screens with a live xkb stack, walked by W_XkbHandleEvent() since
 * XKB events carry no window to look a view up with */
#define MAX_XKB_SCREENS 8
static W_Screen *xkb_screens[MAX_XKB_SCREENS];

static int loadKeymap(W_XkbState *xk, xcb_connection_t *xc)
{
	struct xkb_keymap *new_keymap;
	struct xkb_state *new_state;

	new_keymap = xkb_x11_keymap_new_from_device(xk->ctx, xc, xk->device_id,
						    XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!new_keymap)
		return 0;

	new_state = xkb_x11_state_new_from_device(new_keymap, xc, xk->device_id);
	if (!new_state) {
		xkb_keymap_unref(new_keymap);
		return 0;
	}

	xkb_state_unref(xk->state);
	xkb_keymap_unref(xk->keymap);
	xk->keymap = new_keymap;
	xk->state = new_state;
	return 1;
}

void W_InitXkb(W_Screen *scr)
{
	W_XkbState *xk;
	xcb_connection_t *xc;
	struct xkb_compose_table *table;
	const char *locale;
	uint16_t want, map_parts;
	int i;

	if (scr->xkb)
		return;

	xk = wmalloc(sizeof(W_XkbState));
	xc = scr->display;

	if (!xkb_x11_setup_xkb_extension(xc,
					 XKB_X11_MIN_MAJOR_XKB_VERSION,
					 XKB_X11_MIN_MINOR_XKB_VERSION,
					 XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
					 NULL, NULL, &xk->base_event, NULL)) {
		wfree(xk);
		return;
	}

	xk->ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!xk->ctx) {
		wfree(xk);
		return;
	}

	/* compose shares the screen's context so keymap and compose
	 * rules come from one source */
	locale = setlocale(LC_CTYPE, NULL);
	if (!locale || locale[0] == '\0')
		locale = "C";

	table = xkb_compose_table_new_from_locale(xk->ctx, locale,
						  XKB_COMPOSE_COMPILE_NO_FLAGS);
	if (table) {
		scr->compose_state = xkb_compose_state_new(table,
						   XKB_COMPOSE_STATE_NO_FLAGS);
		xkb_compose_table_unref(table);
	}

	xk->device_id = xkb_x11_get_core_keyboard_device_id(xc);
	if (xk->device_id < 0 || !loadKeymap(xk, xc)) {
		wwarning(_("could not load the XKB keymap from the X server, "
			   "text field input is unavailable"));
		xkb_context_unref(xk->ctx);
		wfree(xk);
		return;
	}

	/* affectWhich == want with details == NULL asks for every
	 * sub-detail of each event type, the map fields are only
	 * consulted for MapNotify and carry the full part set */
	want = XCB_XKB_EVENT_TYPE_NEW_KEYBOARD_NOTIFY
	     | XCB_XKB_EVENT_TYPE_MAP_NOTIFY
	     | XCB_XKB_EVENT_TYPE_STATE_NOTIFY;
	map_parts = XCB_XKB_MAP_PART_KEY_TYPES
		  | XCB_XKB_MAP_PART_KEY_SYMS
		  | XCB_XKB_MAP_PART_MODIFIER_MAP
		  | XCB_XKB_MAP_PART_EXPLICIT_COMPONENTS
		  | XCB_XKB_MAP_PART_KEY_ACTIONS
		  | XCB_XKB_MAP_PART_KEY_BEHAVIORS
		  | XCB_XKB_MAP_PART_VIRTUAL_MODS
		  | XCB_XKB_MAP_PART_VIRTUAL_MOD_MAP;

	xcb_xkb_select_events(xc, xk->device_id, want, 0, want,
			      map_parts, map_parts, NULL);

	scr->xkb = xk;

	for (i = 0; i < MAX_XKB_SCREENS; i++) {
		if (!xkb_screens[i]) {
			xkb_screens[i] = scr;
			break;
		}
	}
}

/* remove the screen from W_XkbHandleEvent()'s walk and release its
 * stack. The XKB event selection stays armed on the connection
 * because screens share it. */
void W_DeinitXkb(W_Screen *scr)
{
	W_XkbState *xk;
	int i;

	if (!scr->xkb)
		return;

	xk = scr->xkb;
	scr->xkb = NULL;

	for (i = 0; i < MAX_XKB_SCREENS; i++)
		if (xkb_screens[i] == scr)
			xkb_screens[i] = NULL;

	if (scr->compose_state) {
		xkb_compose_state_unref(scr->compose_state);
		scr->compose_state = NULL;
	}

	xkb_state_unref(xk->state);
	xkb_keymap_unref(xk->keymap);
	xkb_context_unref(xk->ctx);
	wfree(xk);
}

/* XKB events share the {response_type, xkbType, sequence, time,
 * deviceID} prefix, so any of the typed structs reads xkbType */
bool W_XkbHandleEvent(xcb_generic_event_t *event)
{
	const xcb_xkb_new_keyboard_notify_event_t *ne =
		(const xcb_xkb_new_keyboard_notify_event_t *)event;
	W_Screen *scr;
	bool matched = false;
	int i;

	for (i = 0; i < MAX_XKB_SCREENS; i++) {
		scr = xkb_screens[i];
		if (!scr || scr->display != W_XEvqConnection())
			continue;

		if ((event->response_type & 0x7f) !=
		    ((W_XkbState *)scr->xkb)->base_event)
			continue;

		if (ne->xkbType == XCB_XKB_STATE_NOTIFY) {
			const xcb_xkb_state_notify_event_t *se =
				(const xcb_xkb_state_notify_event_t *)event;

			xkb_state_update_mask(((W_XkbState *)scr->xkb)->state,
					      se->baseMods, se->latchedMods,
					      se->lockedMods, se->baseGroup,
					      se->latchedGroup, se->lockedGroup);
		} else if (ne->xkbType == XCB_XKB_NEW_KEYBOARD_NOTIFY
			   || ne->xkbType == XCB_XKB_MAP_NOTIFY) {
			if (!loadKeymap(scr->xkb, scr->display))
				wwarning(_("could not reload the XKB keymap, "
					   "keyboard input keeps the previous mapping"));
			else
				W_UpdateScreenModifierMasks(scr);
		}
		matched = true;
	}
	return matched;
}

/* ---[ lookup core ]----------------------------------------------------- */

/* the screen's struct xkb_state *, NULL before W_InitXkb() or when the
 * server has no XKB; W_LookupString()'s xcb arm and the unit tests
 * feed this to W_XkbLookupString() */
void *W_XkbStateOf(W_Screen *scr)
{
	W_XkbState *xk = scr->xkb;

	return xk ? xk->state : NULL;
}

/* first keysym at `level` across the keymap's layouts, the first
 * layout carrying a symbol at that level wins */
static xkb_keysym_t keysym_for_level(struct xkb_keymap *keymap,
				     xcb_keycode_t kc, int level)
{
	xkb_layout_index_t layouts, l;
	const xkb_keysym_t *syms;
	int n;

	if (!keymap || kc == 0)
		return XKB_KEY_NoSymbol;

	layouts = xkb_keymap_num_layouts_for_key(keymap, kc);
	for (l = 0; l < layouts; l++) {
		n = xkb_keymap_key_get_syms_by_level(keymap, kc, l,
						     (xkb_level_index_t) level,
						     &syms);
		if (n > 0 && syms[0] != XKB_KEY_NoSymbol)
			return syms[0];
	}
	return XKB_KEY_NoSymbol;
}

/* the shared keycode resolution for the shortcut editors */
xkb_keysym_t W_XkbKeysymForKeycode(W_Screen *scr, xcb_keycode_t kc, int level)
{
	W_XkbState *xk;

	if (!scr || scr->xkb == NULL)
		return XKB_KEY_NoSymbol;

	xk = scr->xkb;
	return keysym_for_level(xk->keymap, kc, level);
}

/* the modifier slot each named modifier is bound to, -1 when unbound.
 * The binding still lives in the core modifier mapping, the keysym
 * side is read against the full keymap across every layout. */
void W_XkbModifierSlots(xcb_connection_t *xc, struct xkb_keymap *keymap,
			WXModifierSlots *slots)
{
	xcb_get_modifier_mapping_reply_t *r;
	xcb_keycode_t *codes;
	int per, mi, ki, level;

	slots->alt = slots->meta = slots->super = slots->hyper = -1;
	slots->mode_switch = slots->numlock = slots->scrolllock = -1;

	if (!keymap)
		return;

	r = xcb_get_modifier_mapping_reply(xc, xcb_get_modifier_mapping(xc),
					  NULL);
	if (!r)
		return;
	codes = xcb_get_modifier_mapping_keycodes(r);
	per = r->keycodes_per_modifier;

#define W_MOD_SLOT_(fld)				\
	do {						\
		if (slots->fld < 0)			\
			slots->fld = mi;		\
	} while (0)
#define W_LOCK_SLOT_(fld)			\
	slots->fld = mi

	for (mi = 0; mi < 8; mi++) {
		for (ki = 0; ki < per; ki++) {
			xcb_keycode_t kc = codes[mi * per + ki];

			if (kc == 0)
				continue;
			for (level = 0; level < 2; level++) {
				switch (keysym_for_level(keymap, kc, level)) {
				case XKB_KEY_Alt_L:
				case XKB_KEY_Alt_R:
					W_MOD_SLOT_(alt);
					break;
				case XKB_KEY_Meta_L:
				case XKB_KEY_Meta_R:
					W_MOD_SLOT_(meta);
					break;
				case XKB_KEY_Super_L:
				case XKB_KEY_Super_R:
					W_MOD_SLOT_(super);
					break;
				case XKB_KEY_Hyper_L:
				case XKB_KEY_Hyper_R:
					W_MOD_SLOT_(hyper);
					break;
				case XKB_KEY_Mode_switch:
					W_MOD_SLOT_(mode_switch);
					break;
				case XKB_KEY_Num_Lock:
					W_LOCK_SLOT_(numlock);
					break;
				case XKB_KEY_Scroll_Lock:
					W_LOCK_SLOT_(scrolllock);
					break;
				default:
					break;
				}
			}
		}
	}
#undef W_LOCK_SLOT_
#undef W_MOD_SLOT_
	free(r);
}

/* XKeysymToKeycode equivalent against the screen's keymap: the first
 * keycode carrying the keysym at any layout/level, 0 when none does.
 * The crash dialog needs this before any key event arrives. */
xcb_keycode_t W_KeysymToKeycode(W_Screen *scr, xkb_keysym_t keysym)
{
	W_XkbState *xk;

	if (!scr || keysym == XKB_KEY_NoSymbol)
		return 0;

	xk = scr->xkb;
	if (!xk || !xk->keymap)
		return 0;

	return W_XkbKeysymToKeycode(xk->keymap, keysym);
}

/* keymap-only core of W_KeysymToKeycode for the headless tests, the
 * first keycode carrying the keysym at any layout/level */
xcb_keycode_t W_XkbKeysymToKeycode(void *keymap_, xkb_keysym_t keysym)
{
	struct xkb_keymap *keymap = keymap_;
	xkb_layout_index_t layouts, l;
	xkb_level_index_t levels, lvl;
	xcb_keycode_t min_kc, max_kc, kc;
	const xkb_keysym_t *syms;
	int n;

	if (!keymap || keysym == XKB_KEY_NoSymbol)
		return 0;

	min_kc = xkb_keymap_min_keycode(keymap);
	max_kc = xkb_keymap_max_keycode(keymap);
	layouts = xkb_keymap_num_layouts(keymap);

	for (kc = min_kc; kc <= max_kc && kc >= min_kc; kc++) {
		for (l = 0; l < layouts; l++) {
			levels = xkb_keymap_num_levels_for_key(keymap, kc, l);
			for (lvl = 0; lvl < levels; lvl++) {
				n = xkb_keymap_key_get_syms_by_level(keymap,
								     kc, l, lvl,
								     &syms);
				if (n > 0 && syms[0] == keysym)
					return (xcb_keycode_t) kc;
			}
		}
	}
	return 0;
}

/* Xutil.h's IsModifierKey range against xkbcommon keysyms: the keypad
 * and function keys live elsewhere, Shift_L (0xffe1) through Hyper_R
 * (0xffee) is exactly the modifier set. */
bool W_IsModifierKey(xkb_keysym_t keysym)
{
	return keysym >= XKB_KEY_Shift_L && keysym <= XKB_KEY_Hyper_R;
}

/*
 * xcb-path W_LookupString core: keysym and UTF-8 text from the
 * xkbcommon state, then dead-key / Compose via W_IMComposeKey().
 *
 *  > 0  text in buffer, keysym and status out-params filled
 *  0    keysym only (no text), still composing, or nothing
 *
 * status uses the XLookupNone/XLookupKeySym/XLookupBoth contract the
 * callers already know.
 */
int W_XkbLookupString(void *xkb_state, void *compose_state,
		      const xcb_key_press_event_t *event,
		      char *buffer, int buflen,
		      xkb_keysym_t *keysym, WXLookupStatus *status)
{
	struct xkb_state *state = xkb_state;
	xkb_keysym_t ksym;
	int n, cn;

	if (!state) {
		if (status)
			*status = WX_LOOKUP_NONE;
		return 0;
	}

	ksym = xkb_state_key_get_one_sym(state, event->detail);
	if (keysym)
		*keysym = ksym;

	/* xkb_state_key_get_utf8() reports the NUL for keys with no
	 * text, XLookupString returned 0 there, normalise so the
	 * return value and status keep the old contract */
	if (buflen > 0 && buffer) {
		n = xkb_state_key_get_utf8(state, event->detail,
					   buffer, (size_t)buflen);
		if (n > 0 && buffer[0] == '\0')
			n = 0;
	} else {
		/* no buffer: XLookupString copies no text */
		n = 0;
	}
	if (n < 0)
		n = 0;

	if (status)
		*status = (n > 0) ? WX_LOOKUP_BOTH
			: (ksym != XKB_KEY_NoSymbol) ? WX_LOOKUP_KEYSYM : WX_LOOKUP_NONE;

	if (compose_state && buffer && buflen > 0 && ksym != XKB_KEY_NoSymbol) {
		xkb_keysym_t composed_sym = ksym;

		cn = W_IMComposeKey(compose_state, ksym, buffer, buflen,
				    &composed_sym);
		if (cn > 0) {
			if (keysym)
				*keysym = composed_sym;
			if (status)
				*status = WX_LOOKUP_BOTH;
			return cn;
		}
		/* still composing: swallow the dead key silently */
		if (xkb_compose_state_get_status(compose_state)
		    == XKB_COMPOSE_COMPOSING) {
			if (status)
				*status = WX_LOOKUP_NONE;
			return 0;
		}
	}

	return n;
}
