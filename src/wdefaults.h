/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
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
	_(NO_TILING,             "NoTiling",             no_tiling) \
	_(AUTO_HIDE_ON_UNFOCUS,  "AutoHideOnUnfocus",    auto_hide_on_unfocus) \
	_(AUTO_MINIATURIZE_ON_UNFOCUS, "AutoMiniaturizeOnUnfocus", auto_miniaturize_on_unfocus) \
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
 *
 * The frame_border_* fields are per-window frame border overrides:
 * widths are plain ints (0 when absent), color specs are owned strings
 * resolved to WMColor at map time by wDefaultFillFrameBorders(), NULL when
 * the entry does not supply them. frame_set is a bitmask of
 * WATTR_FRAME_* keys that this entry supplied.
 *
 * start_workspaces is the plural "StartWorkspaces" array (owned workspace
 * name copies). start_workspace (singular) is kept for backward reads of
 * the old single-string "StartWorkspace" key.
 *
 * start_geometry_spec is the StartGeometry attribute: an owned
 * copy of the "WxH+X+Y" string, parsed at map time via XParseGeometry.
 *
 * mark_key_spec is the MarkKey attribute: an owned copy of the
 * vim-like mark label string, applied at manage time. Three states:
 *   NULL  - entry does not supply MarkKey
 *   ""    - local unmark: clear this window's own mark
 *   "x"   - global steal: unmark any other window holding "x", then
 *           mark this window
 * The empty-vs-non-empty split selects local-unmark vs. global-steal,
 * so the accessor returns the first non-NULL spec (not the first
 * non-empty one).
 */
#define WATTR_FRAME_BORDER_WIDTH          (1u << 0)
#define WATTR_FRAME_BORDER_COLOR          (1u << 1)
#define WATTR_FRAME_FOCUSED_BORDER_COLOR (1u << 2)
#define WATTR_FRAME_SELECTED_BORDER_COLOR (1u << 3)
#define WATTR_FRAME_INNER_BORDER_WIDTH   (1u << 4)
#define WATTR_FRAME_INNER_BORDER_COLOR   (1u << 5)
#define WATTR_FRAME_INNER_FOCUSED_BORDER_COLOR (1u << 6)
#define WATTR_FRAME_INNER_BORDER_SIDES  (1u << 7)

typedef struct WWindowAttrs {
	uint64_t value;
	uint64_t set;
	char *icon;
	char *start_workspace;
	char **start_workspaces;
	int start_workspace_count;
	uint32_t frame_set;
	int frame_border_width;
	int frame_inner_border_width;
	unsigned int frame_inner_border_sides;
	char *frame_border_color_spec;
	char *frame_focused_border_color_spec;
	char *frame_selected_border_color_spec;
	char *frame_inner_border_color_spec;
	char *frame_inner_focused_border_color_spec;
	char *start_geometry_spec;
	char *mark_key_spec;
} WWindowAttrs;

char *find_in_icon_cache(const char *file_name,
			 const char *winstance, const char *wclass);

bool wDefaultHasDynamicRules(void);

/* Bytes of a client-supplied window title handed to regexec() for a
 * MatchTitle predicate. The property reader caps WM_NAME/_NET_WM_NAME
 * at 16 MiB. Without this bound a user-authored ERE could be driven
 * into pathological backtracking by a hostile title, and the
 * re-evaluation runs on every title change. */
#define DYNRULE_MAX_TITLE_LEN 512

#define DYNRULE_MAX_PREDICATE_LEN 512

#endif /* WMWDEFAULTS_H_ */
