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

#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xkb.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-x11.h>

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

static const char *index_to_name(int idx)
{
	switch (idx) {
	case ShiftMapIndex:   return "ModShift";
	case LockMapIndex:    return "ModLock";
	case ControlMapIndex: return "ModControl";
	case Mod1MapIndex:    return "Mod1";
	case Mod2MapIndex:    return "Mod2";
	case Mod3MapIndex:    return "Mod3";
	case Mod4MapIndex:    return "Mod4";
	case Mod5MapIndex:    return "Mod5";
	default:              return "???";
	}
}

/* xkbcommon exposes no public vmod -> real-modifier mapping, so the
 * Alt/Meta/Super/Hyper resolution still walks the core modifier map.
 * One xcb round-trip replaces the two XGetModifierMapping walks that
 * lived in xmodifier.c (named modifiers) and startup.c (NumLock and
 * ScrollLock for the grab fan-out), and the per-keycode keysym lookup
 * reads the local keymap instead of round-tripping per column. */
static void resolveModifiers(void)
{
	xcb_connection_t *xc = XGetXCBConnection(dpy);
	xcb_get_modifier_mapping_reply_t *r;
	xcb_keycode_t *codes;
	int per, mi, ki, level;
	int meta_bit = 0, alt_bit = 0, super_bit = 0, hyper_bit = 0;
	int mode_bit = 0, num_bit = 0, scroll_bit = 0;

	r = xcb_get_modifier_mapping_reply(xc, xcb_get_modifier_mapping(xc), NULL);
	if (!r)
		return;
	codes = xcb_get_modifier_mapping_keycodes(r);
	per = r->keycodes_per_modifier;

#define store_modifier(name, bitp)						\
	do {									\
		if (mi < Mod1MapIndex)						\
			wwarning(_("key %s (0x%x) generates %s, which is nonsensical"), \
				 (name), kc, index_to_name(mi));		\
		else if (*(bitp) && *(bitp) != mi)				\
			wwarning(_("key %s (0x%x) generates both %s and %s, which is nonsensical"), \
				 (name), kc, index_to_name(*(bitp)),		\
				 index_to_name(mi));				\
		else								\
			*(bitp) = mi;						\
	} while (0)

	for (mi = 0; mi < 8; mi++) {
		for (ki = 0; ki < per; ki++) {
			xkb_keycode_t kc = codes[mi * per + ki];
			xkb_keysym_t last = XKB_KEY_NoSymbol;

			if (kc == 0)
				continue;

			/* check the unshifted and shifted columns of the
			 * first layout, matching the previous behaviour
			 * (XKeycodeToKeysym columns 0 and 2) */
			for (level = 0; level < 2; level++) {
				xkb_keysym_t sym = wXkbKeycodeToKeysym(kc, level);

				if (sym == XKB_KEY_NoSymbol || sym == last)
					continue;
				last = sym;

				switch (sym) {
				case XKB_KEY_Meta_L:
				case XKB_KEY_Meta_R:
					store_modifier("Meta", &meta_bit);
					break;
				case XKB_KEY_Alt_L:
				case XKB_KEY_Alt_R:
					store_modifier("Alt", &alt_bit);
					break;
				case XKB_KEY_Super_L:
				case XKB_KEY_Super_R:
					store_modifier("Super", &super_bit);
					break;
				case XKB_KEY_Hyper_L:
				case XKB_KEY_Hyper_R:
					store_modifier("Hyper", &hyper_bit);
					break;
				case XKB_KEY_Mode_switch:
					if (mi >= Mod1MapIndex)
						mode_bit = mi;
					break;
				case XKB_KEY_Num_Lock:
					num_bit = mi;
					break;
				case XKB_KEY_Scroll_Lock:
					scroll_bit = mi;
					break;
				default:
					break;
				}
			}
		}
	}
#undef store_modifier
	free(r);

	/* if there was no Meta key then Alt stands in for it, and Alt as a
	 * separate name becomes unavailable, this is the long-standing
	 * behaviour shortcut specs depend on */
	if (!meta_bit && alt_bit) {
		meta_bit = alt_bit;
		alt_bit = 0;
	}

	/* Mode_switch overrides everything since the server consumes that
	 * bit for group selection before we see the event, so a virtual
	 * modifier sharing it would never match */
	if (mode_bit) {
		const char *warn = NULL;

		if (mode_bit == meta_bit)       { warn = "Meta";  meta_bit = 0; }
		else if (mode_bit == hyper_bit) { warn = "Hyper"; hyper_bit = 0; }
		else if (mode_bit == super_bit) { warn = "Super"; super_bit = 0; }
		else if (mode_bit == alt_bit)   { warn = "Alt";   alt_bit = 0; }
		if (warn)
			wwarning("%s is being used for both Mode_switch and %s.",
				 index_to_name(mode_bit), warn);
	}

	MetaMask       = meta_bit   ? (1u << meta_bit)   : 0;
	AltMask        = alt_bit    ? (1u << alt_bit)    : 0;
	SuperMask      = super_bit  ? (1u << super_bit)  : 0;
	HyperMask      = hyper_bit  ? (1u << hyper_bit)  : 0;
	ModeMask       = mode_bit   ? (1u << mode_bit)   : 0;
	NumLockMask    = num_bit    ? (1u << num_bit)    : 0;
	ScrollLockMask = scroll_bit ? (1u << scroll_bit) : 0;

	(void) ModeMask;

	/* the dispatch mask drops CapsLock, NumLock and ScrollLock so a
	 * binding matches regardless of those, recomputed here so it stays
	 * correct after every keyboard change instead of only at startup */
	w_global.shortcut.modifiers_mask =
		0xff & ~(LockMask | NumLockMask | ScrollLockMask);
}

static int loadKeymap(void)
{
	xcb_connection_t *xc = XGetXCBConnection(dpy);
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
	xcb_connection_t *xc = XGetXCBConnection(dpy);
	uint16_t want, map_parts;
	uint8_t base_event = 0;

	if (!xkb_x11_setup_xkb_extension(xc,
					 XKB_X11_MIN_MAJOR_XKB_VERSION,
					 XKB_X11_MIN_MINOR_XKB_VERSION,
					 XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
					 NULL, NULL, &base_event, NULL)) {
		wwarning(_("XKB extension is not supported by the X server"));
		w_global.xext.xkb.supported = 0;
		return 0;
	}

	w_global.xext.xkb.supported = 1;
	w_global.xext.xkb.event_base = base_event;

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

xkb_keysym_t wXkbKeycodeToKeysym(xkb_keycode_t kc, int level)
{
	const xkb_keysym_t *syms;
	int n;

	if (!keymap || kc == 0)
		return XKB_KEY_NoSymbol;
	n = xkb_keymap_key_get_syms_by_level(keymap, kc, 0, level, &syms);
	if (n < 1)
		return XKB_KEY_NoSymbol;
	return syms[0];
}

/* first keycode whose keysym list (effective layout, then any layout)
 * contains ks */
xkb_keycode_t wXkbKeysymToKeycode(xkb_keysym_t ks)
{
	xkb_keycode_t kc, min_kc, max_kc;

	if (!keymap || ks == XKB_KEY_NoSymbol)
		return 0;

	min_kc = xkb_keymap_min_keycode(keymap);
	max_kc = xkb_keymap_max_keycode(keymap);

	if (state) {
		for (kc = min_kc; kc <= max_kc; kc++) {
			xkb_layout_index_t lay = xkb_state_key_get_layout(state, kc);
			xkb_level_index_t nlev, lev;

			if (lay == XKB_LAYOUT_INVALID)
				continue;
			nlev = xkb_keymap_num_levels_for_key(keymap, kc, lay);
			for (lev = 0; lev < nlev; lev++) {
				const xkb_keysym_t *syms;
				int n, i;

				n = xkb_keymap_key_get_syms_by_level(keymap,
						kc, lay, lev, &syms);
				for (i = 0; i < n; i++)
					if (syms[i] == ks)
						return kc;
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
				for (i = 0; i < n; i++)
					if (syms[i] == ks)
						return kc;
			}
		}
	}
	return 0;
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
		return ShiftMask;
	else if (strcasecmp(key, "CONTROL") == 0)
		return ControlMask;
	else if (strcasecmp(key, "ALT") == 0 && AltMask != 0)
		return AltMask;
	else if (strcasecmp(key, "META") == 0 && MetaMask != 0)
		return MetaMask;
	else if (strcasecmp(key, "SUPER") == 0 && SuperMask != 0)
		return SuperMask;
	else if (strcasecmp(key, "HYPER") == 0 && HyperMask != 0)
		return HyperMask;
	else if (strcasecmp(key, "MOD1") == 0)
		return Mod1Mask;
	else if (strcasecmp(key, "MOD2") == 0)
		return Mod2Mask;
	else if (strcasecmp(key, "MOD3") == 0)
		return Mod3Mask;
	else if (strcasecmp(key, "MOD4") == 0)
		return Mod4Mask;
	else if (strcasecmp(key, "MOD5") == 0)
		return Mod5Mask;
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
		{ ShiftMask,   0, "Sh+" },
		{ ControlMask, 1, "^"   },
		{ AltMask,     8, "A+"  },
		{ Mod1Mask,    2, "M1+" },
		{ Mod2Mask,    3, "M2+" },
		{ Mod3Mask,    4, "M3+" },
		{ Mod4Mask,    5, "M4+" },
		{ Mod5Mask,    6, "M5+" },
		{ MetaMask,    7, "M+"  }
	};
	size_t i;

	if (mask < 0)
		return NULL;

	for (i = 0; i < sizeof(maps) / sizeof(maps[0]); i++) {
		if (mask == maps[i].mask) {
			int idx = maps[i].label_index;
			if (idx >= 0 && idx < 9
			    && wPreferences.modifier_short_labels[idx])
				return wPreferences.modifier_short_labels[idx];
			return maps[i].def;
		}
	}

	wwarning(_("Can't convert keymask 0x%04X to a shortcut label"), mask);
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
	xcb_connection_t *xc = XGetXCBConnection(dpy);

	xcb_xkb_latch_lock_state(xc, XCB_XKB_ID_USE_CORE_KBD,
				 0, 0, 1, group, 0, 0, 0);
	xcb_flush(xc);
}

/* current effective keyboard group, -1 if the round-trip failed */
int wXkbGetGroup(void)
{
	xcb_connection_t *xc = XGetXCBConnection(dpy);
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
#endif /* KEEP_XKB_LOCK_STATUS */

/* ---[ lock-modifier grab fan-out ]-------------------------------------- */

#ifdef NUMLOCK_HACK
void
wHackedGrabKey(int keycode, unsigned int modifiers,
	       Window grab_window, Bool owner_events, int pointer_mode,
	       int keyboard_mode)
{
	if (modifiers == AnyModifier)
		return;

	/* grab all combinations of the modifier with CapsLock, NumLock and
	 * ScrollLock so a binding matches regardless of those latches */
	if (NumLockMask)
		XGrabKey(dpy, keycode, modifiers | NumLockMask,
			 grab_window, owner_events, pointer_mode, keyboard_mode);
	if (ScrollLockMask)
		XGrabKey(dpy, keycode, modifiers | ScrollLockMask,
			 grab_window, owner_events, pointer_mode, keyboard_mode);
	if (NumLockMask && ScrollLockMask)
		XGrabKey(dpy, keycode, modifiers | NumLockMask | ScrollLockMask,
			 grab_window, owner_events, pointer_mode, keyboard_mode);
	if (NumLockMask)
		XGrabKey(dpy, keycode, modifiers | NumLockMask | LockMask,
			 grab_window, owner_events, pointer_mode, keyboard_mode);
	if (ScrollLockMask)
		XGrabKey(dpy, keycode, modifiers | ScrollLockMask | LockMask,
			 grab_window, owner_events, pointer_mode, keyboard_mode);
	if (NumLockMask && ScrollLockMask)
		XGrabKey(dpy, keycode, modifiers | NumLockMask | ScrollLockMask | LockMask,
			 grab_window, owner_events, pointer_mode, keyboard_mode);
}
#endif

void
wHackedGrabButton(unsigned int button, unsigned int modifiers,
		  Window grab_window, Bool owner_events,
		  unsigned int event_mask, int pointer_mode,
		  int keyboard_mode, Window confine_to, Cursor cursor)
{
	XGrabButton(dpy, button, modifiers, grab_window, owner_events,
		    event_mask, pointer_mode, keyboard_mode, confine_to, cursor);

	if (modifiers == AnyModifier)
		return;

	XGrabButton(dpy, button, modifiers | LockMask, grab_window, owner_events,
		    event_mask, pointer_mode, keyboard_mode, confine_to, cursor);

#ifdef NUMLOCK_HACK
	if (NumLockMask)
		XGrabButton(dpy, button, modifiers | NumLockMask,
			    grab_window, owner_events, event_mask, pointer_mode,
			    keyboard_mode, confine_to, cursor);
	if (ScrollLockMask)
		XGrabButton(dpy, button, modifiers | ScrollLockMask,
			    grab_window, owner_events, event_mask, pointer_mode,
			    keyboard_mode, confine_to, cursor);
	if (NumLockMask && ScrollLockMask)
		XGrabButton(dpy, button, modifiers | ScrollLockMask | NumLockMask,
			    grab_window, owner_events, event_mask, pointer_mode,
			    keyboard_mode, confine_to, cursor);
	if (NumLockMask)
		XGrabButton(dpy, button, modifiers | NumLockMask | LockMask,
			    grab_window, owner_events, event_mask, pointer_mode,
			    keyboard_mode, confine_to, cursor);
	if (ScrollLockMask)
		XGrabButton(dpy, button, modifiers | ScrollLockMask | LockMask,
			    grab_window, owner_events, event_mask, pointer_mode,
			    keyboard_mode, confine_to, cursor);
	if (NumLockMask && ScrollLockMask)
		XGrabButton(dpy, button, modifiers | ScrollLockMask | NumLockMask | LockMask,
			    grab_window, owner_events, event_mask, pointer_mode,
			    keyboard_mode, confine_to, cursor);
#endif /* NUMLOCK_HACK */
}
