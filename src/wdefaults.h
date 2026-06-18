/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
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

#ifndef WMWDEFAULTS_H_
#define WMWDEFAULTS_H_

#include <stdint.h>

/*
 * Typed WMWindowAttributes schema.
 *
 * WATTR_BOOLS(_) lists every boolean attribute key. Columns:
 *   K     enum suffix, expands to WATTR_##K (bit index in WWindowAttrs)
 *   name  proplist key string
 *   flag  matching WWindowAttributes bitfield name
 *
 * Icon and StartWorkspace are the two non-boolean keys and are handled
 * as plain string fields alongside the bitset.
 */
#ifdef XKB_BUTTON_HINT
# define WATTR_IF_XKB_BUTTON(x) x
#else
# define WATTR_IF_XKB_BUTTON(x)
#endif

#define WATTR_BOOLS(_) \
	_(NO_TITLEBAR,           "NoTitlebar",           no_titlebar) \
	_(NO_RESIZEBAR,          "NoResizebar",          no_resizebar) \
	_(NO_MINIATURIZE_BUTTON, "NoMiniaturizeButton",  no_miniaturize_button) \
	_(NO_MINIATURIZABLE,     "NoMiniaturizable",     no_miniaturizable) \
	_(NO_CLOSE_BUTTON,       "NoCloseButton",        no_close_button) \
	_(NO_BORDER,             "NoBorder",             no_border) \
	_(NO_HIDE_OTHERS,        "NoHideOthers",         no_hide_others) \
	_(NO_MOUSE_BINDINGS,     "NoMouseBindings",      no_bind_mouse) \
	_(NO_KEY_BINDINGS,       "NoKeyBindings",        no_bind_keys) \
	_(NO_APP_ICON,           "NoAppIcon",            no_appicon) \
	_(SHARED_APP_ICON,       "SharedAppIcon",        shared_appicon) \
	_(KEEP_ON_TOP,           "KeepOnTop",            floating) \
	_(KEEP_ON_BOTTOM,        "KeepOnBottom",         sunken) \
	_(OMNIPRESENT,           "Omnipresent",          omnipresent) \
	_(SKIP_WINDOW_LIST,      "SkipWindowList",       skip_window_list) \
	_(SKIP_SWITCH_PANEL,     "SkipSwitchPanel",      skip_switchpanel) \
	_(KEEP_INSIDE_SCREEN,    "KeepInsideScreen",     dont_move_off) \
	_(UNFOCUSABLE,           "Unfocusable",          no_focusable) \
	_(ALWAYS_USER_ICON,      "AlwaysUserIcon",       always_user_icon) \
	_(START_MINIATURIZED,    "StartMiniaturized",    start_miniaturized) \
	_(START_HIDDEN,          "StartHidden",          start_hidden) \
	_(START_MAXIMIZED,       "StartMaximized",       start_maximized) \
	_(DONT_SAVE_SESSION,     "DontSaveSession",      dont_save_session) \
	_(EMULATE_APP_ICON,      "EmulateAppIcon",       emulate_appicon) \
	_(FOCUS_ACROSS_WORKSPACE, "FocusAcrossWorkspace", focus_across_wksp) \
	_(FULL_MAXIMIZE,         "FullMaximize",         full_maximize) \
	_(SWALLOW_CHILD,         "SwallowChild",         swallow_child) \
	_(IGNORE_DECORATION_CHANGES, "IgnoreDecorationChanges", ignore_decoration_changes) \
	_(NO_TABBING,            "NoTabbing",            no_tabbing) \
	_(AUTO_TAB_GROUP,        "AutoTabGroup",         auto_tab_group) \
	WATTR_IF_XKB_BUTTON( \
	_(NO_LANGUAGE_BUTTON,    "NoLanguageButton",     no_language_button) \
	)

#define WATTR_ENUM_ROW(K, name, flag) WATTR_##K,
typedef enum {
	WATTR_BOOLS(WATTR_ENUM_ROW)
	WATTR_BOOL_COUNT
} WWindowAttrKey;
#undef WATTR_ENUM_ROW

/*
 * One parsed WMWindowAttributes entry. value/set are bitsets indexed by
 * WWindowAttrKey, set carries which keys this entry supplied so the
 * four-level merge knows what to override. icon and start_workspace are
 * owned copies, NULL when the entry does not supply them.
 */
typedef struct WWindowAttrs {
	uint32_t value;
	uint32_t set;
	char *icon;
	char *start_workspace;
} WWindowAttrs;

#endif /* WMWDEFAULTS_H_ */
