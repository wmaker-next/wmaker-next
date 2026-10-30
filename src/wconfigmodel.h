/*
 *  wconfigmodel.h
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

/*
 * Typed Window Maker configuration model.
 */

#ifndef WMCONFIGMODEL_H_
#define WMCONFIGMODEL_H_

#include <stddef.h>
#include <stdint.h>


#include <WINGs/WUtil.h>

#include "keybind.h"
#include "config_schema_def.h"

typedef struct _WScreen WScreen;

typedef struct WConfigCoord {
	int x;
	int y;
} WConfigCoord;

typedef struct WConfigStringArray {
	size_t count;
	char **items;
} WConfigStringArray;

typedef struct WConfigSwitchPanelImages {
	char *tile;
	char *background;
	int center_width;
	int center_height;
} WConfigSwitchPanelImages;

typedef struct WConfigKeyBinding {
	char *spec;
} WConfigKeyBinding;

typedef struct WConfigResourceSpec {
	char *spec;
} WConfigResourceSpec;

typedef enum {
	WCFG_TEXTURE_WIDGET_COLOR,
	WCFG_TEXTURE_ICON_BACK,
	WCFG_TEXTURE_F_TITLE_BACK,
	WCFG_TEXTURE_P_TITLE_BACK,
	WCFG_TEXTURE_U_TITLE_BACK,
	WCFG_TEXTURE_TAB_INACTIVE_TITLE_BACK,
	WCFG_TEXTURE_RESIZEBAR_BACK,
	WCFG_TEXTURE_MENU_TITLE_BACK,
	WCFG_TEXTURE_MENU_TEXT_BACK,
	WCFG_TEXTURE_WORKSPACE_MAP_BACK
} WConfigTextureTarget;

typedef enum {
	WCFG_FONT_WINDOW_TITLE,
	WCFG_FONT_MENU_TITLE,
	WCFG_FONT_MENU_TEXT,
	WCFG_FONT_ICON_TITLE,
	WCFG_FONT_CLIP_TITLE,
	WCFG_FONT_LARGE_DISPLAY
} WConfigFontTarget;

typedef enum {
	WCFG_COLOR_HIGHLIGHT,
	WCFG_COLOR_HIGHLIGHT_TEXT,
	WCFG_COLOR_CLIP_TITLE,
	WCFG_COLOR_C_CLIP_TITLE,
	WCFG_COLOR_F_TITLE,
	WCFG_COLOR_P_TITLE,
	WCFG_COLOR_U_TITLE,
	WCFG_COLOR_TAB_INACTIVE_TITLE,
	WCFG_COLOR_MENU_TITLE,
	WCFG_COLOR_MENU_TEXT,
	WCFG_COLOR_MENU_DISABLED,
	WCFG_COLOR_ICON_TITLE,
	WCFG_COLOR_ICON_TITLE_BACK,
	WCFG_COLOR_FRAME_BORDER,
	WCFG_COLOR_FRAME_FOCUSED_BORDER,
	WCFG_COLOR_FRAME_SELECTED_BORDER,
	WCFG_COLOR_FRAME_INNER_BORDER,
	WCFG_COLOR_FRAME_INNER_FOCUSED_BORDER
} WConfigColorTarget;

typedef enum {
	WCFG_CURSOR_NORMAL,
	WCFG_CURSOR_ARROW,
	WCFG_CURSOR_MOVE,
	WCFG_CURSOR_RESIZE,
	WCFG_CURSOR_TOP_LEFT_RESIZE,
	WCFG_CURSOR_TOP_RIGHT_RESIZE,
	WCFG_CURSOR_BOTTOM_LEFT_RESIZE,
	WCFG_CURSOR_BOTTOM_RIGHT_RESIZE,
	WCFG_CURSOR_VERTICAL_RESIZE,
	WCFG_CURSOR_HORIZONTAL_RESIZE,
	WCFG_CURSOR_WAIT,
	WCFG_CURSOR_QUESTION,
	WCFG_CURSOR_TEXT,
	WCFG_CURSOR_SELECT,
	WCFG_CURSOR_CAPTURE
} WConfigCursorTarget;

typedef enum {
	WCFG_BACKGROUND_WORKSPACE_SPECIFIC,
	WCFG_BACKGROUND_WORKSPACE
} WConfigBackgroundTarget;

enum {
	WCFG_REFRESH_WINDOW_TEXTURES = 1u << 0,
	WCFG_REFRESH_MENU_TEXTURE = 1u << 1,
	WCFG_REFRESH_MENU_FONT = 1u << 2,
	WCFG_REFRESH_MENU_COLOR = 1u << 3,
	WCFG_REFRESH_MENU_TITLE_TEXTURE = 1u << 4,
	WCFG_REFRESH_MENU_TITLE_FONT = 1u << 5,
	WCFG_REFRESH_MENU_TITLE_COLOR = 1u << 6,
	WCFG_REFRESH_WINDOW_TITLE_COLOR = 1u << 7,
	WCFG_REFRESH_WINDOW_FONT = 1u << 8,
	WCFG_REFRESH_ICON_TILE = 1u << 9,
	WCFG_REFRESH_ICON_FONT = 1u << 10,
	WCFG_REFRESH_BUTTON_IMAGES = 1u << 11,
	WCFG_REFRESH_ICON_TITLE_COLOR = 1u << 12,
	WCFG_REFRESH_ICON_TITLE_BACK = 1u << 13,
	WCFG_REFRESH_WORKSPACE_MENU = 1u << 14,
	WCFG_REFRESH_INNER_BORDER = 1u << 15,
	WCFG_REFRESH_SYSTEM_COLOR = 1u << 16,
	WCFG_REFRESH_FRAME_BORDER = WCFG_REFRESH_MENU_FONT | WCFG_REFRESH_WINDOW_FONT
};

#define WCFG_ENUM_ROW(K, name, T, field, def, etable, lo, hi, fl, refresh) \
	WCFG_KEY_##K,
#define WCFG_ENUM_KB_ROW(K, name, def, wkbd) \
	WCFG_KEY_##K,

typedef enum {
	WCFG_SCHEMA(WCFG_ENUM_ROW)
	WCFG_SCHEMA_KEYBINDINGS(WCFG_ENUM_KB_ROW)
	WCFG_KEY_COUNT
} WConfigKey;

#undef WCFG_ENUM_ROW
#undef WCFG_ENUM_KB_ROW

#define WCFG_CHANGE_WORDS ((WCFG_KEY_COUNT + 31u) / 32u)
#define WCFG_CHANGE_WORD(key) ((unsigned int)(key) / 32u)
#define WCFG_CHANGE_BIT(key) (1u << ((unsigned int)(key) % 32u))

#define WCFG_STRUCT_ROW(K, name, T, field, def, etable, lo, hi, fl, refresh) \
	WCFG_CTYPE_##T field;

typedef struct WConfig {
	WCFG_SCHEMA(WCFG_STRUCT_ROW)
	WConfigKeyBinding key_bindings[WKBD_LAST];
} WConfig;

#undef WCFG_STRUCT_ROW

typedef struct WConfigChanges {
	uint32_t changed[WCFG_CHANGE_WORDS];
} WConfigChanges;

WConfig *wConfigCreateDefault(void);
WConfig *wConfigReadWindowMaker(WMPropList *dict);
void wConfigDestroy(WConfig *config);

/* Deep-copy a WConfig for independent modification and destruction */
WConfig *wConfigClone(const WConfig *src);

bool wConfigDiff(const WConfig *old_config, const WConfig *new_config,
		 WConfigChanges *changes);
bool wConfigChangesHas(const WConfigChanges *changes, WConfigKey key);

void wConfigApplyStatic(const WConfig *config);
unsigned int wConfigApplyRuntime(WScreen *scr, const WConfig *old_config,
				 const WConfig *new_config,
				 const WConfigChanges *changes);

/* Re-issue the passive button grabs that depend on wdisp->modifier_mask
 * for every managed window, after a live ModifierKey change. Key grabs use
 * the per-shortcut modifier and need no re-grab here. */
void wRegrabWindowMouseGrabs(WScreen *scr);

/* Re-issue the passive key grabs for every managed window. Call once
 * after a batch of keybinding changes, not per binding. */
void wRegrabFocusedWindowKeys(WScreen *scr);

/* Re-apply every FONT_SPEC entry; used after a WINGs font-cache detach so
 * WM-side font pointers re-open under the current antialias flag. */
unsigned int wConfigForceReapplyFonts(WScreen *scr);

/* Re-apply the IconBack texture so setIconTile() re-renders the icon,
 * clip and drawer tiles at the current IconSize even when IconBack
 * itself did not change. Returns the icon-tile refresh bit. */
unsigned int wConfigForceReapplyIconBack(WScreen *scr);

/* AntialiasedText (WMGLOBAL) live-apply: re-read via WMReloadConfiguration
 * from wDefaultsCheckDomains, and on a change detach the WINGs font cache
 * and re-open WM-side fonts. Returns the font refresh mask. */
unsigned int wConfigApplyAntialiasedText(WScreen *scr);
/* Re-apply the WMGLOBAL system colors in place. Returns the system-color
 * refresh bit when a refresh was applied, 0 otherwise. */
unsigned int wConfigApplySystemColors(WScreen *scr);

void wConfigCacheWindowMaker(WMPropList *dict);
void wConfigAdoptWindowMaker(WConfig *config);
void wConfigClearWindowMaker(void);

void wConfigApplySwitchPanelImages(WScreen *scr,
				   const WConfigSwitchPanelImages *images);
void wConfigApplyModifierKeyLabels(WScreen *scr,
				   const WConfigStringArray *labels);

/* Parse a FrameInnerBorderSides array into the INNER_SIDE_* bitmask
 * defined in window.h. An empty array means all four sides. */
unsigned int wFrameInnerBorderSidesFromArray(const WConfigStringArray *array);
void wConfigApplyModifierKeyShortLabels(WScreen *scr,
					const WConfigStringArray *labels);
void wConfigApplyHotCornerActions(WScreen *scr,
				  const WConfigStringArray *actions);
void wConfigApplyModifierKey(const char *spec, const char *default_spec);
void wConfigApplyTabDragModifier(const char *spec);
unsigned int wConfigApplyKeyBinding(WScreen *scr, const char *key,
				    int binding_index, const char *spec,
				    const char *default_spec,
				    bool *binding_changed);
unsigned int wConfigApplyTextureSpec(WScreen *scr, const char *key,
				     int target,
				     const WConfigResourceSpec *spec,
				     const char *default_spec);
unsigned int wConfigApplyFontSpec(WScreen *scr, const char *key,
				  int target,
				  const WConfigResourceSpec *spec,
				  const char *default_spec);
unsigned int wConfigApplyColorSpec(WScreen *scr, const char *key,
				   int target,
				   const WConfigResourceSpec *spec,
				   const char *default_spec);
unsigned int wConfigApplyCursorSpec(WScreen *scr, const char *key,
				    int target,
				    const WConfigResourceSpec *spec,
				    const char *default_spec);
unsigned int wConfigApplyWorkspaceBackgroundSpec(WScreen *scr, const char *key,
						 int target,
						 const WConfigResourceSpec *spec,
						 const char *default_spec);

void wConfigApplyStaticDefaults(WMPropList *dict);
void wConfigPrepareRuntimeDefaults(WMPropList *new_dict,
				   const WConfig *old_config,
				   WConfig *new_config,
				   WConfigChanges *changes);
/* Test convenience wrapper; production reloads prepare one diff and adopt it. */
unsigned int wConfigApplyRuntimeDefaults(WScreen *scr, WMPropList *new_dict);

#endif
