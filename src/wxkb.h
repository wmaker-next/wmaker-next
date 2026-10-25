/* wxkb.h - xkbcommon-x11 keymap and modifier-mask resolution
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

#ifndef WMWXKB_H
#define WMWXKB_H

#include <stdbool.h>

#include <xkbcommon/xkbcommon.h>
#include <xcb/xkb.h>

/* ---[ lifecycle ]------------------------------------------------------- */

int  wXkbInit(void);
void wXkbRefresh(void);
bool wXkbUpdateState(const xcb_xkb_state_notify_event_t *e);

/* ---[ keysym / keycode resolution ]------------------------------------- */

xkb_keysym_t  wXkbKeysymFromName(const char *name);
int           wXkbKeysymName(xkb_keysym_t ks, char *buf, size_t size);
xkb_keysym_t  wXkbKeycodeToKeysym(xkb_keycode_t kc, int level);
xkb_keycode_t wXkbKeysymToKeycode(xkb_keysym_t ks);
xkb_keysym_t  wXkbStateKeysym(xkb_keycode_t kc);
int           wXkbStateUtf8(xkb_keycode_t kc, char *buf, size_t size);

/* ---[ modifier-name resolution ]----------------------------------------
 *
 * Same names and signatures as the retired src/xmodifier.c so callers in
 * defaults.c, rootmenu.c, usermenu.c and misc.c only swap one #include.
 */
int  wXModifierFromKey(const char *key);
const char *wXModifierToShortcutLabel(int mask);

unsigned int wXkbNumLockMask(void);
unsigned int wXkbScrollLockMask(void);

/* ---[ layout (group) helpers ]------------------------------------------ */

int         wXkbNumLayouts(void);
const char *wXkbLayoutName(int idx);

#ifdef KEEP_XKB_LOCK_STATUS
void wXkbLockGroup(int group);
int  wXkbGetGroup(void);

/* ---[ accent-picker text injection ]------------------------------------- */

bool wXkbTypeText(const char *utf8);
bool wXkbTestAvailable(void);
xcb_keycode_t wXkbMinKeycode(void);
xcb_keycode_t wXkbMaxKeycode(void);
bool wXkbKeycodeIsModifier(xcb_keycode_t kc);
bool wXkbModifierDown(unsigned int mask);
#endif

/* ---[ lock-modifier grab fan-out ]--------------------------------------
 *
 * Issue the XGrabKey/XGrabButton for every CapsLock x NumLock x ScrollLock
 * combination so a binding still matches with any of those latched. Moved
 * here from startup.c so the masks are read from the same place that
 * resolves them.
 */
#ifdef NUMLOCK_HACK
void wHackedGrabKey(int keycode, unsigned int modifiers, xcb_window_t grab_window,
		    bool owner_events, int pointer_mode, int keyboard_mode);
#endif
void wHackedGrabButton(unsigned int button, unsigned int modifiers,
		       xcb_window_t grab_window, bool owner_events,
		       unsigned int event_mask, int pointer_mode,
		       int keyboard_mode, xcb_window_t confine_to, xcb_cursor_t cursor);

#endif /* WMWXKB_H */
