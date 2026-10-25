/* wxkb.c - xkbcommon-x11 keymap and modifier-mask resolution
 *
 *  Window Maker window manager
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

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xkb.h>
#include <xcb/xtest.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-x11.h>

#include <WINGs/WINGsP.h>
#include <WINGs/WUtil.h>

#include "WindowMaker.h"
#include "wxkb.h"

/* one keymap per X connection, rebuilt on XCB_XKB_NEW_KEYBOARD_NOTIFY
 * and XCB_XKB_MAP_NOTIFY, state tracked from XCB_XKB_STATE_NOTIFY */
static struct xkb_context *ctx;
static struct xkb_keymap *keymap;
static struct xkb_state *state;
static int32_t device_id = -1;

/* resolved real-modifier masks for the named virtual modifiers,
 * derived from xcb_get_modifier_mapping plus a per-keycode keysym
 * lookup against the local keymap, recomputed on every refresh */
static unsigned int MetaMask, AltMask, SuperMask, HyperMask, ModeMask;
static unsigned int NumLockMask, ScrollLockMask;

/* keycodes present in the core modifier mapping, rebuilt on every
 * keyboard change so grab and spare-keycode scans stay local */
static unsigned char modifier_keycodes[256];

/* modifier-map slot indexes, the Xlib ShiftMapIndex..Mod5MapIndex
 * values; xcb's modifier mapping is the same slot order */
enum {
	ModShiftSlot = 0,
	ModLockSlot,
	ModControlSlot,
	Mod1Slot, Mod2Slot, Mod3Slot, Mod4Slot, Mod5Slot
};

static const char *index_to_name(int idx)
{
	switch (idx) {
	case ModShiftSlot:   return "ModShift";
	case ModLockSlot:    return "ModLock";
	case ModControlSlot: return "ModControl";
	case Mod1Slot:       return "Mod1";
	case Mod2Slot:       return "Mod2";
	case Mod3Slot:       return "Mod3";
	case Mod4Slot:       return "Mod4";
	case Mod5Slot:       return "Mod5";
	default:             return "???";
	}
}

static void refreshModifierKeycodes(void)
{
	xcb_get_modifier_mapping_cookie_t ck;
	xcb_get_modifier_mapping_reply_t *rep;
	xcb_keycode_t *map;
	int per_mod, i, total;

	memset(modifier_keycodes, 0, sizeof(modifier_keycodes));
	ck = xcb_get_modifier_mapping(wdisp->conn);
	rep = xcb_get_modifier_mapping_reply(wdisp->conn, ck, NULL);
	if (!rep)
		return;
	per_mod = rep->keycodes_per_modifier;
	total = per_mod * 8;
	map = xcb_get_modifier_mapping_keycodes(rep);
	for (i = 0; i < total; i++) {
		xcb_keycode_t kc = map[i];

		if (kc != 0)
			modifier_keycodes[kc] = 1;
	}
	free(rep);
}

/* the shared classification against the local keymap, recomputed on
 * every keyboard change */
static void resolveModifiers(void)
{
	WXModifierSlots slots;
	const char *warn;

	refreshModifierKeycodes();
	W_XkbModifierSlots(wdisp->conn, keymap, &slots);

	/* a named modifier bound to the Shift, Lock or Control columns
	 * can never work as a shortcut modifier */
	if (slots.alt >= 0 && slots.alt < Mod1Slot)
		wwarning(_("Alt is bound to %s, which is nonsensical"),
			 index_to_name(slots.alt));
	if (slots.meta >= 0 && slots.meta < Mod1Slot)
		wwarning(_("Meta is bound to %s, which is nonsensical"),
			 index_to_name(slots.meta));
	if (slots.super >= 0 && slots.super < Mod1Slot)
		wwarning(_("Super is bound to %s, which is nonsensical"),
			 index_to_name(slots.super));
	if (slots.hyper >= 0 && slots.hyper < Mod1Slot)
		wwarning(_("Hyper is bound to %s, which is nonsensical"),
			 index_to_name(slots.hyper));

	/* if there was no Meta key then Alt stands in for it, and Alt as a
	 * separate name becomes unavailable, this is the long-standing
	 * behaviour shortcut specs depend on */
	if (slots.meta < 0 && slots.alt >= 0) {
		slots.meta = slots.alt;
		slots.alt = -1;
	}

	/* Mode_switch overrides everything since the server consumes that
	 * bit for group selection before we see the event, so a virtual
	 * modifier sharing it would never match */
	if (slots.mode_switch >= 0) {
		warn = NULL;

		if (slots.mode_switch == slots.meta) {
			warn = "Meta";
			slots.meta = -1;
		} else if (slots.mode_switch == slots.hyper) {
			warn = "Hyper";
			slots.hyper = -1;
		} else if (slots.mode_switch == slots.super) {
			warn = "Super";
			slots.super = -1;
		} else if (slots.mode_switch == slots.alt) {
			warn = "Alt";
			slots.alt = -1;
		}
		if (warn)
			wwarning("%s is being used for both Mode_switch and %s.",
				 index_to_name(slots.mode_switch), warn);
	}

	MetaMask       = slots.meta   >= 0 ? 1u << slots.meta   : 0;
	AltMask        = slots.alt    >= 0 ? 1u << slots.alt    : 0;
	SuperMask      = slots.super >= 0 ? 1u << slots.super : 0;
	HyperMask      = slots.hyper >= 0 ? 1u << slots.hyper : 0;
	ModeMask       = slots.mode_switch >= 0 ? 1u << slots.mode_switch : 0;
	NumLockMask    = slots.numlock >= 0 ? 1u << slots.numlock : 0;
	ScrollLockMask = slots.scrolllock >= 0 ? 1u << slots.scrolllock : 0;

	(void) ModeMask;

	/* the dispatch mask drops CapsLock, NumLock and ScrollLock so a
	 * binding matches regardless of those, recomputed here so it stays
	 * correct after every keyboard change instead of only at startup */
	w_global.shortcut.modifiers_mask =
		0xff & ~(XCB_MOD_MASK_LOCK | NumLockMask | ScrollLockMask);
}

static int loadKeymap(void)
{
	xcb_connection_t *xc = wdisp->conn;
	struct xkb_keymap *new_keymap;
	struct xkb_state *new_state;

	new_keymap = xkb_x11_keymap_new_from_device(ctx, xc, device_id,
						    XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!new_keymap)
		return 0;

	new_state = xkb_x11_state_new_from_device(new_keymap, xc, device_id);
	if (!new_state) {
		xkb_keymap_unref(new_keymap);
		return 0;
	}

	xkb_state_unref(state);
	xkb_keymap_unref(keymap);
	keymap = new_keymap;
	state = new_state;
	return 1;
}

int wXkbInit(void)
{
	xcb_connection_t *xc = wdisp->conn;
	uint16_t want, map_parts;
	uint8_t base_event = 0;

	if (!xkb_x11_setup_xkb_extension(xc,
					 XKB_X11_MIN_MAJOR_XKB_VERSION,
					 XKB_X11_MIN_MINOR_XKB_VERSION,
					 XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
					 NULL, NULL, &base_event, NULL)) {
		wwarning(_("XKB extension is not supported by the X server"));
		wdisp->xext.xkb.supported = 0;
		return 0;
	}

	wdisp->xext.xkb.supported = 1;
	wdisp->xext.xkb.event_base = base_event;

	ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!ctx)
		return 0;

	device_id = xkb_x11_get_core_keyboard_device_id(xc);
	if (device_id < 0 || !loadKeymap()) {
		wwarning(_("could not load the XKB keymap from the X server"));
		return 0;
	}

	/* selectAll == affectWhich and details == NULL asks for every
	 * sub-detail of each event type, the affectMap and map fields are
	 * only consulted for MapNotify so they carry the full part set */
	want = XCB_XKB_EVENT_TYPE_NEW_KEYBOARD_NOTIFY
	     | XCB_XKB_EVENT_TYPE_MAP_NOTIFY
	     | XCB_XKB_EVENT_TYPE_STATE_NOTIFY;
#ifdef KEEP_XKB_LOCK_STATUS
	want |= XCB_XKB_EVENT_TYPE_INDICATOR_STATE_NOTIFY;
#endif
	map_parts = XCB_XKB_MAP_PART_KEY_TYPES
		  | XCB_XKB_MAP_PART_KEY_SYMS
		  | XCB_XKB_MAP_PART_MODIFIER_MAP
		  | XCB_XKB_MAP_PART_EXPLICIT_COMPONENTS
		  | XCB_XKB_MAP_PART_KEY_ACTIONS
		  | XCB_XKB_MAP_PART_KEY_BEHAVIORS
		  | XCB_XKB_MAP_PART_VIRTUAL_MODS
		  | XCB_XKB_MAP_PART_VIRTUAL_MOD_MAP;

	xcb_xkb_select_events(xc, device_id, want, 0, want,
			      map_parts, map_parts, NULL);

	resolveModifiers();
	return 1;
}

void wXkbRefresh(void)
{
	if (!ctx || device_id < 0)
		return;
	if (!loadKeymap()) {
		wwarning(_("could not reload the XKB keymap, key bindings keep the previous mapping"));
		return;
	}
	resolveModifiers();
}

bool wXkbUpdateState(const xcb_xkb_state_notify_event_t *e)
{
	uint32_t before, after;

	if (!state)
		return false;
	before = xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE);
	xkb_state_update_mask(state,
			      e->baseMods, e->latchedMods, e->lockedMods,
			      e->baseGroup, e->latchedGroup, e->lockedGroup);
	after = xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE);
	return before != after;
}

/* ---[ keysym / keycode resolution ]------------------------------------- */

xkb_keysym_t wXkbKeysymFromName(const char *name)
{
	xkb_keysym_t ks;

	if (!name)
		return XKB_KEY_NoSymbol;
	ks = xkb_keysym_from_name(name, XKB_KEYSYM_NO_FLAGS);
	if (ks != XKB_KEY_NoSymbol)
		return ks;
	/* historic Xlib XStringToKeysym is case-sensitive but be lenient
	 * with hand-written shortcut specs */
	return xkb_keysym_from_name(name, XKB_KEYSYM_CASE_INSENSITIVE);
}

int wXkbKeysymName(xkb_keysym_t ks, char *buf, size_t size)
{
	if (ks == XKB_KEY_NoSymbol)
		return -1;
	return xkb_keysym_get_name(ks, buf, size);
}

/* first keysym at `level` across the keymap's layouts, the first
 * layout carrying a symbol at that level wins */
xkb_keysym_t wXkbKeycodeToKeysym(xkb_keycode_t kc, int level)
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

static bool find_keycode_level(xkb_keysym_t ks, xcb_keycode_t *out_kc,
			       xkb_layout_index_t *out_layout,
			       xkb_level_index_t *out_level)
{
	unsigned int kc;
	xcb_keycode_t min_kc, max_kc;

	if (!keymap || ks == XKB_KEY_NoSymbol)
		return false;

	min_kc = xkb_keymap_min_keycode(keymap);
	max_kc = xkb_keymap_max_keycode(keymap);

	if (state) {
		for (kc = min_kc; kc <= max_kc; kc++) {
			xkb_layout_index_t lay = xkb_state_key_get_layout(state, kc);
			xkb_level_index_t nlev, lev;

			if (lay == XKB_LAYOUT_INVALID)
				continue;
			nlev = xkb_keymap_num_levels_for_key(keymap,
					(xcb_keycode_t)kc, lay);
			for (lev = 0; lev < nlev; lev++) {
				const xkb_keysym_t *syms;
				int n, i;

				n = xkb_keymap_key_get_syms_by_level(keymap,
						kc, lay, lev, &syms);
				for (i = 0; i < n; i++) {
					if (syms[i] != ks)
						continue;
					*out_kc = (xcb_keycode_t)kc;
					*out_layout = lay;
					*out_level = lev;
					return true;
				}
			}
		}
	}

	for (kc = min_kc; kc <= max_kc; kc++) {
		xkb_layout_index_t nlay = xkb_keymap_num_layouts_for_key(keymap, kc);
		xkb_layout_index_t lay;

		for (lay = 0; lay < nlay; lay++) {
			xkb_level_index_t nlev =
				xkb_keymap_num_levels_for_key(keymap, kc, lay);
			xkb_level_index_t lev;

			for (lev = 0; lev < nlev; lev++) {
				const xkb_keysym_t *syms;
				int n, i;

				n = xkb_keymap_key_get_syms_by_level(keymap,
						kc, lay, lev, &syms);
				for (i = 0; i < n; i++) {
					if (syms[i] != ks)
						continue;
					*out_kc = (xcb_keycode_t)kc;
					*out_layout = lay;
					*out_level = lev;
					return true;
				}
			}
		}
	}
	return false;
}

/* first keycode whose keysym list (effective layout, then any layout)
 * contains ks */
xkb_keycode_t wXkbKeysymToKeycode(xkb_keysym_t ks)
{
	xcb_keycode_t kc;
	xkb_layout_index_t lay;
	xkb_level_index_t lev;

	if (!find_keycode_level(ks, &kc, &lay, &lev))
		return 0;
	return kc;
}

xkb_keysym_t wXkbStateKeysym(xkb_keycode_t kc)
{
	if (!state)
		return XKB_KEY_NoSymbol;
	return xkb_state_key_get_one_sym(state, kc);
}

int wXkbStateUtf8(xkb_keycode_t kc, char *buf, size_t size)
{
	if (!state) {
		if (size)
			buf[0] = '\0';
		return 0;
	}
	return xkb_state_key_get_utf8(state, kc, buf, size);
}

/* ---[ modifier-name resolution ]---------------------------------------- */

int wXModifierFromKey(const char *key)
{
	if (strcasecmp(key, "SHIFT") == 0)
		return XCB_MOD_MASK_SHIFT;
	else if (strcasecmp(key, "CONTROL") == 0)
		return XCB_MOD_MASK_CONTROL;
	else if (strcasecmp(key, "ALT") == 0 && AltMask != 0)
		return AltMask;
	else if (strcasecmp(key, "META") == 0 && MetaMask != 0)
		return MetaMask;
	else if (strcasecmp(key, "SUPER") == 0 && SuperMask != 0)
		return SuperMask;
	else if (strcasecmp(key, "HYPER") == 0 && HyperMask != 0)
		return HyperMask;
	else if (strcasecmp(key, "MOD1") == 0)
		return XCB_MOD_MASK_1;
	else if (strcasecmp(key, "MOD2") == 0)
		return XCB_MOD_MASK_2;
	else if (strcasecmp(key, "MOD3") == 0)
		return XCB_MOD_MASK_3;
	else if (strcasecmp(key, "MOD4") == 0)
		return XCB_MOD_MASK_4;
	else if (strcasecmp(key, "MOD5") == 0)
		return XCB_MOD_MASK_5;
	else
		return -1;
}

const char *wXModifierToShortcutLabel(int mask)
{
	struct {
		int mask;
		int label_index;
		const char *def;
	} maps[] = {
		{ XCB_MOD_MASK_SHIFT,   0, "Sh+" },
		{ XCB_MOD_MASK_CONTROL, 1, "^"   },
		{ AltMask,     8, "A+"  },
		{ XCB_MOD_MASK_1,    2, "M1+" },
		{ XCB_MOD_MASK_2,    3, "M2+" },
		{ XCB_MOD_MASK_3,    4, "M3+" },
		{ XCB_MOD_MASK_4,    5, "M4+" },
		{ XCB_MOD_MASK_5,    6, "M5+" },
		{ MetaMask,    7, "M+"  }
	};
	size_t i;

	if (mask < 0)
		return NULL;

	for (i = 0; i < sizeof(maps) / sizeof(maps[0]); i++) {
		if (mask == maps[i].mask) {
			int idx = maps[i].label_index;
			if (idx >= 0 && idx < 9
			    && wdisp->modifier_short_labels[idx])
				return wdisp->modifier_short_labels[idx];
			return maps[i].def;
		}
	}

	wwarning(_("Can't convert keymask 0x%04X to a shortcut label"), (unsigned int)mask);
	return NULL;
}

unsigned int wXkbNumLockMask(void)
{
	return NumLockMask;
}

unsigned int wXkbScrollLockMask(void)
{
	return ScrollLockMask;
}

/* ---[ layout (group) helpers ]------------------------------------------ */

int wXkbNumLayouts(void)
{
	if (!keymap)
		return 0;
	return (int) xkb_keymap_num_layouts(keymap);
}

const char *wXkbLayoutName(int idx)
{
	if (!keymap || idx < 0)
		return NULL;
	return xkb_keymap_layout_get_name(keymap, (xkb_layout_index_t) idx);
}

#ifdef KEEP_XKB_LOCK_STATUS
/* lock the effective keyboard group, equivalent to XkbLockGroup with
 * lockGroup=1 and every other LatchLockState field cleared, the request
 * has no reply so any error surfaces through the WXEvqErrorHook */
void wXkbLockGroup(int group)
{
	xcb_connection_t *xc = wdisp->conn;

	xcb_xkb_latch_lock_state(xc, XCB_XKB_ID_USE_CORE_KBD,
				 0, 0, 1, group, 0, 0, 0);
	xcb_flush(xc);
}

/* current effective keyboard group, -1 if the round-trip failed */
int wXkbGetGroup(void)
{
	xcb_connection_t *xc = wdisp->conn;
	xcb_xkb_get_state_reply_t *r;
	int group;

	r = xcb_xkb_get_state_reply(xc,
			xcb_xkb_get_state(xc, XCB_XKB_ID_USE_CORE_KBD), NULL);
	if (!r)
		return -1;
	group = r->group;
	free(r);
	return group;
}

/* ---[ accent-picker injection ]------------------------------------------ */

xcb_keycode_t wXkbMinKeycode(void)
{
	if (!keymap)
		return 0;
	return xkb_keymap_min_keycode(keymap);
}

xcb_keycode_t wXkbMaxKeycode(void)
{
	if (!keymap)
		return 0;
	return xkb_keymap_max_keycode(keymap);
}

bool wXkbKeycodeIsModifier(xcb_keycode_t kc)
{
	return modifier_keycodes[kc] != 0;
}

static void syncServer(void)
{
	xcb_get_input_focus_reply_t *rep;

	rep = xcb_get_input_focus_reply(wdisp->conn,
					xcb_get_input_focus(wdisp->conn), NULL);
	free(rep);
}

static bool fakeKey(xcb_keycode_t kc, bool press)
{
	if (kc == 0)
		return false;
	xcb_test_fake_input(wdisp->conn,
			    press ? XCB_KEY_PRESS : XCB_KEY_RELEASE,
			    kc, XCB_CURRENT_TIME, XCB_NONE, 0, 0, 0);
	return true;
}

bool wXkbTestAvailable(void)
{
	static int available = -1;
	const xcb_query_extension_reply_t *ext;
	xcb_test_get_version_reply_t *rep;

	if (available >= 0)
		return available != 0;
	ext = xcb_get_extension_data(wdisp->conn, &xcb_test_id);
	if (!ext || !ext->present) {
		available = 0;
		wwarning(_("XTest extension is not available, accent picker cannot inject text"));
		return false;
	}
	rep = xcb_test_get_version_reply(wdisp->conn,
			xcb_test_get_version(wdisp->conn, XCB_TEST_MAJOR_VERSION,
					     XCB_TEST_MINOR_VERSION), NULL);
	available = rep != NULL;
	free(rep);
	if (!available)
		wwarning(_("XTest extension is not usable, accent picker cannot inject text"));
	return available != 0;
}

static xcb_keycode_t keysymKeycode(xkb_keysym_t ks)
{
	return wXkbKeysymToKeycode(ks);
}

static xcb_keycode_t modifierKeycode(xkb_keysym_t first, xkb_keysym_t second)
{
	xcb_keycode_t kc = keysymKeycode(first);

	if (kc == 0 && second != XKB_KEY_NoSymbol)
		kc = keysymKeycode(second);
	return kc;
}

static bool keyDown(const char *keys, xcb_keycode_t kc)
{
	if (kc == 0)
		return false;
	return (keys[kc >> 3] & (1 << (kc & 7))) != 0;
}

static bool typeKeysym(xkb_keysym_t ks)
{
	xcb_keycode_t kc, shift, level3;
	xkb_layout_index_t layout;
	xkb_level_index_t level;
	xkb_mod_mask_t masks[8];
	xkb_mod_index_t shift_idx, level3_idx, mod5_idx;
	xkb_mod_mask_t allowed = 0;
	xkb_mod_mask_t chosen = 0;
	char keys[32];
	xcb_query_keymap_reply_t *krep;
	bool need_shift, need_level3;
	bool taken_shift = false, taken_level3 = false;
	int n, i;

	if (!find_keycode_level(ks, &kc, &layout, &level))
		return false;
	/* only use the direct path when the found layout is the one the
	 * key currently uses. Keysyms that only exist in another group are
	 * injected through the spare-keycode remap instead. */
	if (state && xkb_state_key_get_layout(state, kc) != layout)
		return false;

	shift_idx = xkb_keymap_mod_get_index(keymap, "Shift");
	level3_idx = xkb_keymap_mod_get_index(keymap, "LevelThree");
	mod5_idx = xkb_keymap_mod_get_index(keymap, "Mod5");
	if (shift_idx != XKB_MOD_INVALID)
		allowed |= (xkb_mod_mask_t)1 << shift_idx;
	if (level3_idx != XKB_MOD_INVALID)
		allowed |= (xkb_mod_mask_t)1 << level3_idx;
	if (mod5_idx != XKB_MOD_INVALID)
		allowed |= (xkb_mod_mask_t)1 << mod5_idx;

	n = xkb_keymap_key_get_mods_for_level(keymap, kc, layout, level,
					      masks, sizeof(masks) / sizeof(masks[0]));
	for (i = 0; i < n; i++) {
		if ((masks[i] & ~allowed) == 0) {
			chosen = masks[i];
			break;
		}
	}
	if (n > 0 && i == n)
		return false;

	need_shift = shift_idx != XKB_MOD_INVALID &&
		(chosen & ((xkb_mod_mask_t)1 << shift_idx)) != 0;
	need_level3 = (level3_idx != XKB_MOD_INVALID &&
		(chosen & ((xkb_mod_mask_t)1 << level3_idx)) != 0) ||
		(mod5_idx != XKB_MOD_INVALID &&
		(chosen & ((xkb_mod_mask_t)1 << mod5_idx)) != 0);

	krep = xcb_query_keymap_reply(wdisp->conn,
				      xcb_query_keymap(wdisp->conn), NULL);
	if (!krep)
		return false;
	memcpy(keys, krep->keys, sizeof(keys));
	free(krep);

	shift = modifierKeycode(XKB_KEY_Shift_L, XKB_KEY_Shift_R);
	level3 = modifierKeycode(XKB_KEY_ISO_Level3_Shift, XKB_KEY_Alt_R);

	if (need_shift && !keyDown(keys, shift))
		taken_shift = fakeKey(shift, true);
	if (need_level3 && !keyDown(keys, level3))
		taken_level3 = fakeKey(level3, true);

	fakeKey(kc, true);
	fakeKey(kc, false);

	if (taken_level3)
		fakeKey(level3, false);
	if (taken_shift)
		fakeKey(shift, false);
	xcb_flush(wdisp->conn);
	return true;
}

static xcb_keycode_t spareKeycode(const char *keys)
{
	unsigned int kc;
	xcb_keycode_t min_kc, max_kc;

	if (!keymap)
		return 0;
	min_kc = xkb_keymap_min_keycode(keymap);
	max_kc = xkb_keymap_max_keycode(keymap);

	for (kc = min_kc; kc <= max_kc; kc++) {
		xkb_layout_index_t nlay, lay;
		bool used = false;

		if (wXkbKeycodeIsModifier((xcb_keycode_t)kc) ||
		    keyDown(keys, (xcb_keycode_t)kc))
			continue;
		nlay = xkb_keymap_num_layouts_for_key(keymap, (xcb_keycode_t)kc);
		for (lay = 0; lay < nlay && !used; lay++) {
			xkb_level_index_t nlev, lev;

			nlev = xkb_keymap_num_levels_for_key(keymap,
					(xcb_keycode_t)kc, lay);
			for (lev = 0; lev < nlev; lev++) {
				const xkb_keysym_t *syms;
				int n = xkb_keymap_key_get_syms_by_level(keymap,
						(xcb_keycode_t)kc, lay, lev, &syms);

				if (n > 0 && syms[0] != XKB_KEY_NoSymbol) {
					used = true;
					break;
				}
			}
		}
		if (!used)
			return (xcb_keycode_t)kc;
	}
	return 0;
}

static bool remapTypeKeysym(xkb_keysym_t ks)
{
	xcb_keycode_t kc;
	xcb_get_keyboard_mapping_reply_t *rep;
	xcb_keysym_t *original, *replacement;
	char keys[32];
	xcb_query_keymap_reply_t *krep;
	struct timespec settle = { 0, 5000000 };
	int per, i;
	bool ok = false;

	krep = xcb_query_keymap_reply(wdisp->conn,
				      xcb_query_keymap(wdisp->conn), NULL);
	if (!krep)
		return false;
	memcpy(keys, krep->keys, sizeof(keys));
	free(krep);

	kc = spareKeycode(keys);
	if (kc == 0) {
		static bool warned;

		if (!warned) {
			wwarning(_("accent picker: no spare keycode for text injection"));
			warned = true;
		}
		return false;
	}

	rep = xcb_get_keyboard_mapping_reply(wdisp->conn,
			xcb_get_keyboard_mapping(wdisp->conn, kc, 1), NULL);
	if (!rep)
		return false;
	per = rep->keysyms_per_keycode;
	if (per > xcb_get_keyboard_mapping_keysyms_length(rep))
		per = xcb_get_keyboard_mapping_keysyms_length(rep);
	if (per <= 0) {
		free(rep);
		return false;
	}
	original = wmalloc((size_t)per * sizeof(xcb_keysym_t));
	replacement = wmalloc((size_t)per * sizeof(xcb_keysym_t));
	if (!original || !replacement) {
		wfree(original);
		wfree(replacement);
		free(rep);
		return false;
	}
	memcpy(original, xcb_get_keyboard_mapping_keysyms(rep),
	       (size_t)per * sizeof(xcb_keysym_t));
	free(rep);

	for (i = 0; i < per; i++)
		replacement[i] = (xcb_keysym_t)ks;

	xcb_change_keyboard_mapping(wdisp->conn, 1, kc, (uint8_t)per, replacement);
	xcb_flush(wdisp->conn);
	syncServer();
	nanosleep(&settle, NULL);

	ok = fakeKey(kc, true) && fakeKey(kc, false);
	xcb_flush(wdisp->conn);
	{
		/* let clients process the key event before the mapping is
		 * restored, otherwise they resolve it against the old map */
		struct timespec hold = { 0, 100000000 };

		nanosleep(&hold, NULL);
	}
	syncServer();

	xcb_change_keyboard_mapping(wdisp->conn, 1, kc, (uint8_t)per, original);
	xcb_flush(wdisp->conn);
	syncServer();

	wfree(original);
	wfree(replacement);
	return ok;
}

bool wXkbTypeText(const char *utf8)
{
	const char *p;
	size_t len;

	if (!utf8 || !*utf8)
		return false;
	if (!wXkbTestAvailable())
		return false;

	len = strlen(utf8);
	p = utf8;
	while ((size_t)(p - utf8) < len) {
		unsigned char c = (unsigned char)*p;
		uint32_t cp;
		int n;
		xkb_keysym_t ks;

		if (c < 0x80) {
			cp = c;
			n = 1;
		} else if ((c & 0xe0) == 0xc0 && p + 1 < utf8 + len) {
			cp = ((uint32_t)(c & 0x1f) << 6) |
			     ((uint32_t)p[1] & 0x3f);
			n = 2;
		} else if ((c & 0xf0) == 0xe0 && p + 2 < utf8 + len) {
			cp = ((uint32_t)(c & 0x0f) << 12) |
			     (((uint32_t)p[1] & 0x3f) << 6) |
			     ((uint32_t)p[2] & 0x3f);
			n = 3;
		} else if ((c & 0xf8) == 0xf0 && p + 3 < utf8 + len) {
			cp = ((uint32_t)(c & 0x07) << 18) |
			     (((uint32_t)p[1] & 0x3f) << 12) |
			     (((uint32_t)p[2] & 0x3f) << 6) |
			     ((uint32_t)p[3] & 0x3f);
			n = 4;
		} else {
			return false;
		}

		ks = xkb_utf32_to_keysym(cp);
		if (ks == XKB_KEY_NoSymbol)
			return false;
		if (!typeKeysym(ks) && !remapTypeKeysym(ks))
			return false;
		p += n;
	}
	return true;
}

static bool modifierDown(unsigned int mask)
{
	xcb_get_modifier_mapping_cookie_t ck;
	xcb_get_modifier_mapping_reply_t *rep;
	xcb_keycode_t *map;
	char keys[32];
	xcb_query_keymap_reply_t *krep;
	int per, slot, i;
	bool down = false;

	if (mask == 0)
		return false;
	slot = 0;
	while (slot < 8 && !(mask & (1u << slot)))
		slot++;
	if (slot >= 8)
		return false;

	krep = xcb_query_keymap_reply(wdisp->conn,
				      xcb_query_keymap(wdisp->conn), NULL);
	if (!krep)
		return false;
	memcpy(keys, krep->keys, sizeof(keys));
	free(krep);

	ck = xcb_get_modifier_mapping(wdisp->conn);
	rep = xcb_get_modifier_mapping_reply(wdisp->conn, ck, NULL);
	if (!rep)
		return false;
	per = rep->keycodes_per_modifier;
	map = xcb_get_modifier_mapping_keycodes(rep);
	for (i = 0; i < per; i++) {
		xcb_keycode_t kc = map[slot * per + i];

		if (kc != 0 && keyDown(keys, kc)) {
			down = true;
			break;
		}
	}
	free(rep);
	return down;
}

bool wXkbModifierDown(unsigned int mask)
{
	return modifierDown(mask);
}

#endif /* KEEP_XKB_LOCK_STATUS */

/* ---[ lock-modifier grab fan-out ]-------------------------------------- */

#ifdef NUMLOCK_HACK
void
wHackedGrabKey(int keycode, unsigned int modifiers,
	       xcb_window_t grab_window, bool owner_events, int pointer_mode,
	       int keyboard_mode)
{
	if (modifiers == XCB_MOD_MASK_ANY)
		return;

	/* grab all combinations of the modifier with CapsLock, NumLock and
	 * ScrollLock so a binding matches regardless of those latches */
	if (NumLockMask)
		wXGrabKey(wdisp->conn, keycode, modifiers | NumLockMask,
			 grab_window, owner_events, pointer_mode, keyboard_mode);
	if (ScrollLockMask)
		wXGrabKey(wdisp->conn, keycode, modifiers | ScrollLockMask,
			 grab_window, owner_events, pointer_mode, keyboard_mode);
	if (NumLockMask && ScrollLockMask)
		wXGrabKey(wdisp->conn, keycode, modifiers | NumLockMask | ScrollLockMask,
			 grab_window, owner_events, pointer_mode, keyboard_mode);
	if (NumLockMask)
		wXGrabKey(wdisp->conn, keycode, modifiers | NumLockMask | XCB_MOD_MASK_LOCK,
			 grab_window, owner_events, pointer_mode, keyboard_mode);
	if (ScrollLockMask)
		wXGrabKey(wdisp->conn, keycode, modifiers | ScrollLockMask | XCB_MOD_MASK_LOCK,
			 grab_window, owner_events, pointer_mode, keyboard_mode);
	if (NumLockMask && ScrollLockMask)
		wXGrabKey(wdisp->conn, keycode, modifiers | NumLockMask | ScrollLockMask | XCB_MOD_MASK_LOCK,
			 grab_window, owner_events, pointer_mode, keyboard_mode);
}
#endif

void
wHackedGrabButton(unsigned int button, unsigned int modifiers,
		  xcb_window_t grab_window, bool owner_events,
		  unsigned int event_mask, int pointer_mode,
		  int keyboard_mode, xcb_window_t confine_to, xcb_cursor_t cursor)
{
	wXGrabButton(wdisp->conn, button, modifiers, grab_window, owner_events,
		    event_mask, pointer_mode, keyboard_mode, confine_to, cursor);

	if (modifiers == XCB_MOD_MASK_ANY)
		return;

	wXGrabButton(wdisp->conn, button, modifiers | XCB_MOD_MASK_LOCK, grab_window, owner_events,
		    event_mask, pointer_mode, keyboard_mode, confine_to, cursor);

#ifdef NUMLOCK_HACK
	if (NumLockMask)
		wXGrabButton(wdisp->conn, button, modifiers | NumLockMask,
			    grab_window, owner_events, event_mask, pointer_mode,
			    keyboard_mode, confine_to, cursor);
	if (ScrollLockMask)
		wXGrabButton(wdisp->conn, button, modifiers | ScrollLockMask,
			    grab_window, owner_events, event_mask, pointer_mode,
			    keyboard_mode, confine_to, cursor);
	if (NumLockMask && ScrollLockMask)
		wXGrabButton(wdisp->conn, button, modifiers | ScrollLockMask | NumLockMask,
			    grab_window, owner_events, event_mask, pointer_mode,
			    keyboard_mode, confine_to, cursor);
	if (NumLockMask)
		wXGrabButton(wdisp->conn, button, modifiers | NumLockMask | XCB_MOD_MASK_LOCK,
			    grab_window, owner_events, event_mask, pointer_mode,
			    keyboard_mode, confine_to, cursor);
	if (ScrollLockMask)
		wXGrabButton(wdisp->conn, button, modifiers | ScrollLockMask | XCB_MOD_MASK_LOCK,
			    grab_window, owner_events, event_mask, pointer_mode,
			    keyboard_mode, confine_to, cursor);
	if (NumLockMask && ScrollLockMask)
		wXGrabButton(wdisp->conn, button, modifiers | ScrollLockMask | NumLockMask | XCB_MOD_MASK_LOCK,
			    grab_window, owner_events, event_mask, pointer_mode,
			    keyboard_mode, confine_to, cursor);
#endif /* NUMLOCK_HACK */
}
