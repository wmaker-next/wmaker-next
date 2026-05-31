/*
 * Typed Window Maker configuration model.
 */

#ifndef WMCONFIGMODEL_H_
#define WMCONFIGMODEL_H_

#include <stddef.h>
#include <stdint.h>

#include <X11/Xlib.h>

#include <WINGs/WUtil.h>

#include "keybind.h"

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
	WCFG_COLOR_MENU_TITLE,
	WCFG_COLOR_MENU_TEXT,
	WCFG_COLOR_MENU_DISABLED,
	WCFG_COLOR_ICON_TITLE,
	WCFG_COLOR_ICON_TITLE_BACK,
	WCFG_COLOR_FRAME_BORDER,
	WCFG_COLOR_FRAME_FOCUSED_BORDER,
	WCFG_COLOR_FRAME_SELECTED_BORDER
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
	WCFG_REFRESH_FRAME_BORDER = WCFG_REFRESH_MENU_FONT | WCFG_REFRESH_WINDOW_FONT
};

typedef enum {
	WCFG_KEY_COLORMAP_SIZE,
	WCFG_KEY_DISABLE_DITHERING,
	WCFG_KEY_ICON_SIZE,
	WCFG_KEY_MODIFIER_KEY,
	WCFG_KEY_FOCUS_MODE,
	WCFG_KEY_NEW_STYLE,
	WCFG_KEY_DISABLE_DOCK,
	WCFG_KEY_DISABLE_CLIP,
	WCFG_KEY_DISABLE_DRAWERS,
	WCFG_KEY_CLIP_MERGED_IN_DOCK,
	WCFG_KEY_DISABLE_MINIWINDOWS,
	WCFG_KEY_ENABLE_WORKSPACE_PAGER,
	WCFG_KEY_SWITCH_PANEL_ICON_SIZE,
	WCFG_KEY_ICON_POSITION,
	WCFG_KEY_PIXMAP_PATH,
	WCFG_KEY_ICON_PATH,
	WCFG_KEY_STICKY_ICONS,
	WCFG_KEY_WRAP_APPICONS_IN_DOCK,
	WCFG_KEY_WINDOW_PLACE_ORIGIN,
	WCFG_KEY_KEEP_DOCK_ON_PRIMARY_HEAD,
	WCFG_KEY_SCREENSHOT_FILENAME_TEMPLATE,
	WCFG_KEY_MENU_STYLE,
	WCFG_KEY_TITLE_JUSTIFY,
	WCFG_KEY_WINDOW_TITLE_EXTEND_SPACE,
	WCFG_KEY_WINDOW_TITLE_MIN_HEIGHT,
	WCFG_KEY_WINDOW_TITLE_MAX_HEIGHT,
	WCFG_KEY_MENU_TITLE_EXTEND_SPACE,
	WCFG_KEY_MENU_TITLE_MIN_HEIGHT,
	WCFG_KEY_MENU_TITLE_MAX_HEIGHT,
	WCFG_KEY_MENU_TEXT_EXTEND_SPACE,
	WCFG_KEY_ICONIFICATION_STYLE,
	WCFG_KEY_ENFORCE_ICON_MARGIN,
	WCFG_KEY_DISABLE_WS_MOUSE_ACTIONS,
	WCFG_KEY_MOUSE_LEFT_BUTTON_ACTION,
	WCFG_KEY_MOUSE_MIDDLE_BUTTON_ACTION,
	WCFG_KEY_MOUSE_RIGHT_BUTTON_ACTION,
	WCFG_KEY_MOUSE_BACKWARD_BUTTON_ACTION,
	WCFG_KEY_MOUSE_FORWARD_BUTTON_ACTION,
	WCFG_KEY_MOUSE_WHEEL_ACTION,
	WCFG_KEY_MOUSE_WHEEL_TILT_ACTION,
	WCFG_KEY_DONT_CONFIRM_KILL,
	WCFG_KEY_WINDOW_TITLE_BALLOONS,
	WCFG_KEY_MINIWINDOW_TITLE_BALLOONS,
	WCFG_KEY_MINIWINDOW_PREVIEW_BALLOONS,
	WCFG_KEY_APPICON_BALLOONS,
	WCFG_KEY_HELP_BALLOONS,
	WCFG_KEY_EDGE_RESISTANCE,
	WCFG_KEY_RESIZE_INCREMENT,
	WCFG_KEY_ATTRACTION,
	WCFG_KEY_DISABLE_BLINKING,
	WCFG_KEY_SINGLE_CLICK_LAUNCH,
	WCFG_KEY_STRICT_WINDOZE_CYCLE,
	WCFG_KEY_SWITCH_PANEL_ONLY_OPEN,
	WCFG_KEY_MINI_PREVIEW_SIZE,
	WCFG_KEY_IGNORE_GTK_HINTS,
	WCFG_KEY_NO_WINDOW_OVER_DOCK,
	WCFG_KEY_NO_WINDOW_OVER_ICONS,
	WCFG_KEY_RESIZE_DISPLAY,
	WCFG_KEY_MOVE_DISPLAY,
#ifdef USE_RANDR
	WCFG_KEY_HOTPLUG_MONITOR,
#endif
	WCFG_KEY_HOT_CORNERS,
	WCFG_KEY_HOT_CORNER_DELAY,
	WCFG_KEY_HOT_CORNER_EDGE,
	WCFG_KEY_WINDOW_LIST_APP_ICONS,
	WCFG_KEY_MOUSE_WHEEL_FOCUS,
	WCFG_KEY_KEYCHAIN_TIMEOUT_DELAY,
	WCFG_KEY_SHOW_CLIP_TITLE,
#ifdef KEEP_XKB_LOCK_STATUS
	WCFG_KEY_KBD_MODE_LOCK,
#endif
	WCFG_KEY_DIALOG_HISTORY_LINES,
	WCFG_KEY_CYCLE_ACTIVE_HEAD_ONLY,
	WCFG_KEY_CYCLE_ALL_WORKSPACES,
	WCFG_KEY_CYCLE_IGNORE_MINIMIZED,
	WCFG_KEY_DB_CLICK_FULL_SCREEN,
	WCFG_KEY_CLOSE_ROOT_MENU_BY_LEFT_OR_RIGHT_MOUSE_CLICK,
	WCFG_KEY_COLORMAP_MODE,
	WCFG_KEY_AUTO_FOCUS,
	WCFG_KEY_AUTO_FOCUS_ACROSS_HEADS,
	WCFG_KEY_RAISE_DELAY,
	WCFG_KEY_CIRCULATE_RAISE,
	WCFG_KEY_SUPERFLUOUS,
	WCFG_KEY_ADVANCE_TO_NEW_WORKSPACE,
	WCFG_KEY_CYCLE_WORKSPACES,
	WCFG_KEY_WORKSPACE_NAME_DISPLAY_POSITION,
	WCFG_KEY_WINDOW_PLACEMENT,
	WCFG_KEY_WORKSPACE_BORDER,
	WCFG_KEY_WORKSPACE_BORDER_SIZE,
	WCFG_KEY_WRAP_MENUS,
	WCFG_KEY_SCROLLABLE_MENUS,
	WCFG_KEY_DOUBLE_CLICK_TIME,
	WCFG_KEY_SAVE_SESSION_ON_EXIT,
	WCFG_KEY_MENU_SCROLL_SPEED,
	WCFG_KEY_ICON_SLIDE_SPEED,
	WCFG_KEY_SHADE_SPEED,
	WCFG_KEY_BOUNCE_APPICONS_WHEN_URGENT,
	WCFG_KEY_RAISE_APPICONS_WHEN_BOUNCING,
	WCFG_KEY_DO_NOT_MAKE_APPICONS_BOUNCE,
	WCFG_KEY_CLIP_AUTORAISE_DELAY,
	WCFG_KEY_CLIP_AUTOLOWER_DELAY,
	WCFG_KEY_CLIP_AUTOEXPAND_DELAY,
	WCFG_KEY_CLIP_AUTOCOLLAPSE_DELAY,
	WCFG_KEY_ALIGN_SUBMENUS,
	WCFG_KEY_VI_KEY_MENUS,
	WCFG_KEY_OPEN_TRANSIENT_ON_OWNER_WORKSPACE,
	WCFG_KEY_IGNORE_FOCUS_CLICK,
	WCFG_KEY_USE_SAVE_UNDERS,
	WCFG_KEY_OPAQUE_MOVE,
	WCFG_KEY_OPAQUE_RESIZE,
	WCFG_KEY_OPAQUE_MOVE_RESIZE_KEYBOARD,
	WCFG_KEY_DISABLE_ANIMATIONS,
	WCFG_KEY_DONT_LINK_WORKSPACES,
	WCFG_KEY_WINDOW_SNAPPING,
	WCFG_KEY_SNAP_EDGE_DETECT,
	WCFG_KEY_SNAP_CORNER_DETECT,
	WCFG_KEY_SNAP_TO_TOP_MAXIMIZES_FULLSCREEN,
	WCFG_KEY_DRAG_MAXIMIZED_WINDOW,
	WCFG_KEY_MOVE_HALF_MAXIMIZED_WINDOWS_BETWEEN_SCREENS,
	WCFG_KEY_ALTERNATIVE_HALF_MAXIMIZED,
	WCFG_KEY_POINTER_WITH_HALF_MAX_WINDOWS,
	WCFG_KEY_HIGHLIGHT_ACTIVE_APP,
	WCFG_KEY_AUTO_ARRANGE_ICONS,
	WCFG_KEY_WORKSPACE_SPECIFIC_BACK,
	WCFG_KEY_WORKSPACE_BACK,
	WCFG_KEY_SMOOTH_WORKSPACE_BACK,
	WCFG_KEY_WIDGET_COLOR,
	WCFG_KEY_ICON_BACK,
	WCFG_KEY_WINDOW_TITLE_FONT,
	WCFG_KEY_MENU_TITLE_FONT,
	WCFG_KEY_MENU_TEXT_FONT,
	WCFG_KEY_ICON_TITLE_FONT,
	WCFG_KEY_CLIP_TITLE_FONT,
	WCFG_KEY_LARGE_DISPLAY_FONT,
	WCFG_KEY_HIGHLIGHT_COLOR,
	WCFG_KEY_HIGHLIGHT_TEXT_COLOR,
	WCFG_KEY_CLIP_TITLE_COLOR,
	WCFG_KEY_CCLIP_TITLE_COLOR,
	WCFG_KEY_F_TITLE_COLOR,
	WCFG_KEY_P_TITLE_COLOR,
	WCFG_KEY_U_TITLE_COLOR,
	WCFG_KEY_F_TITLE_BACK,
	WCFG_KEY_P_TITLE_BACK,
	WCFG_KEY_U_TITLE_BACK,
	WCFG_KEY_RESIZEBAR_BACK,
	WCFG_KEY_MENU_TITLE_COLOR,
	WCFG_KEY_MENU_TEXT_COLOR,
	WCFG_KEY_MENU_DISABLED_COLOR,
	WCFG_KEY_MENU_TITLE_BACK,
	WCFG_KEY_MENU_TEXT_BACK,
	WCFG_KEY_ICON_TITLE_COLOR,
	WCFG_KEY_ICON_TITLE_BACK,
	WCFG_KEY_FRAME_BORDER_WIDTH,
	WCFG_KEY_FRAME_BORDER_COLOR,
	WCFG_KEY_FRAME_FOCUSED_BORDER_COLOR,
	WCFG_KEY_FRAME_SELECTED_BORDER_COLOR,
	WCFG_KEY_WORKSPACE_MAP_BACK,
	WCFG_KEY_NORMAL_CURSOR,
	WCFG_KEY_ARROW_CURSOR,
	WCFG_KEY_MOVE_CURSOR,
	WCFG_KEY_RESIZE_CURSOR,
	WCFG_KEY_TOP_LEFT_RESIZE_CURSOR,
	WCFG_KEY_TOP_RIGHT_RESIZE_CURSOR,
	WCFG_KEY_BOTTOM_LEFT_RESIZE_CURSOR,
	WCFG_KEY_BOTTOM_RIGHT_RESIZE_CURSOR,
	WCFG_KEY_VERTICAL_RESIZE_CURSOR,
	WCFG_KEY_HORIZONTAL_RESIZE_CURSOR,
	WCFG_KEY_WAIT_CURSOR,
	WCFG_KEY_QUESTION_CURSOR,
	WCFG_KEY_TEXT_CURSOR,
	WCFG_KEY_SELECT_CURSOR,
	WCFG_KEY_CAPTURE_CURSOR,
	WCFG_KEY_HOT_CORNER_ACTIONS,
	WCFG_KEY_SWITCH_PANEL_IMAGES,
	WCFG_KEY_MODIFIER_KEY_LABELS,
	WCFG_KEY_MODIFIER_KEY_SHORT_LABELS,
	WCFG_KEY_ROOT_MENU_KEY,
	WCFG_KEY_WINDOW_LIST_KEY,
	WCFG_KEY_WINDOW_MENU_KEY,
	WCFG_KEY_KEYCHAIN_CANCEL_KEY,
	WCFG_KEY_DOCK_RAISE_LOWER_KEY,
	WCFG_KEY_CLIP_RAISE_LOWER_KEY,
	WCFG_KEY_MINIATURIZE_KEY,
	WCFG_KEY_MINIMIZE_ALL_KEY,
	WCFG_KEY_HIDE_KEY,
	WCFG_KEY_HIDE_OTHERS_KEY,
	WCFG_KEY_MOVE_RESIZE_KEY,
	WCFG_KEY_CLOSE_KEY,
	WCFG_KEY_MAXIMIZE_KEY,
	WCFG_KEY_V_MAXIMIZE_KEY,
	WCFG_KEY_H_MAXIMIZE_KEY,
	WCFG_KEY_LH_MAXIMIZE_KEY,
	WCFG_KEY_RH_MAXIMIZE_KEY,
	WCFG_KEY_TH_MAXIMIZE_KEY,
	WCFG_KEY_BH_MAXIMIZE_KEY,
	WCFG_KEY_TLC_MAXIMIZE_KEY,
	WCFG_KEY_TRC_MAXIMIZE_KEY,
	WCFG_KEY_BLC_MAXIMIZE_KEY,
	WCFG_KEY_BRC_MAXIMIZE_KEY,
	WCFG_KEY_MAXIMUS_KEY,
	WCFG_KEY_CENTER_KEY,
	WCFG_KEY_KEEP_ON_TOP_KEY,
	WCFG_KEY_KEEP_AT_BOTTOM_KEY,
	WCFG_KEY_OMNIPRESENT_KEY,
	WCFG_KEY_RAISE_KEY,
	WCFG_KEY_LOWER_KEY,
	WCFG_KEY_RAISE_LOWER_KEY,
	WCFG_KEY_SHADE_KEY,
	WCFG_KEY_SELECT_KEY,
	WCFG_KEY_WORKSPACE_MAP_KEY,
	WCFG_KEY_FOCUS_NEXT_KEY,
	WCFG_KEY_FOCUS_PREV_KEY,
	WCFG_KEY_FOCUS_WINDOW_LEFT_KEY,
	WCFG_KEY_FOCUS_WINDOW_RIGHT_KEY,
	WCFG_KEY_FOCUS_WINDOW_UP_KEY,
	WCFG_KEY_FOCUS_WINDOW_DOWN_KEY,
	WCFG_KEY_GROUP_NEXT_KEY,
	WCFG_KEY_GROUP_PREV_KEY,
	WCFG_KEY_NEXT_WORKSPACE_KEY,
	WCFG_KEY_PREV_WORKSPACE_KEY,
	WCFG_KEY_LAST_WORKSPACE_KEY,
	WCFG_KEY_NEXT_WORKSPACE_LAYER_KEY,
	WCFG_KEY_PREV_WORKSPACE_LAYER_KEY,
	WCFG_KEY_WORKSPACE1_KEY,
	WCFG_KEY_WORKSPACE2_KEY,
	WCFG_KEY_WORKSPACE3_KEY,
	WCFG_KEY_WORKSPACE4_KEY,
	WCFG_KEY_WORKSPACE5_KEY,
	WCFG_KEY_WORKSPACE6_KEY,
	WCFG_KEY_WORKSPACE7_KEY,
	WCFG_KEY_WORKSPACE8_KEY,
	WCFG_KEY_WORKSPACE9_KEY,
	WCFG_KEY_WORKSPACE10_KEY,
	WCFG_KEY_MOVE_TO_WORKSPACE1_KEY,
	WCFG_KEY_MOVE_TO_WORKSPACE2_KEY,
	WCFG_KEY_MOVE_TO_WORKSPACE3_KEY,
	WCFG_KEY_MOVE_TO_WORKSPACE4_KEY,
	WCFG_KEY_MOVE_TO_WORKSPACE5_KEY,
	WCFG_KEY_MOVE_TO_WORKSPACE6_KEY,
	WCFG_KEY_MOVE_TO_WORKSPACE7_KEY,
	WCFG_KEY_MOVE_TO_WORKSPACE8_KEY,
	WCFG_KEY_MOVE_TO_WORKSPACE9_KEY,
	WCFG_KEY_MOVE_TO_WORKSPACE10_KEY,
	WCFG_KEY_MOVE_TO_NEXT_WORKSPACE_KEY,
	WCFG_KEY_MOVE_TO_PREV_WORKSPACE_KEY,
	WCFG_KEY_MOVE_TO_LAST_WORKSPACE_KEY,
	WCFG_KEY_MOVE_TO_NEXT_WORKSPACE_LAYER_KEY,
	WCFG_KEY_MOVE_TO_PREV_WORKSPACE_LAYER_KEY,
	WCFG_KEY_WINDOW_SHORTCUT1_KEY,
	WCFG_KEY_WINDOW_SHORTCUT2_KEY,
	WCFG_KEY_WINDOW_SHORTCUT3_KEY,
	WCFG_KEY_WINDOW_SHORTCUT4_KEY,
	WCFG_KEY_WINDOW_SHORTCUT5_KEY,
	WCFG_KEY_WINDOW_SHORTCUT6_KEY,
	WCFG_KEY_WINDOW_SHORTCUT7_KEY,
	WCFG_KEY_WINDOW_SHORTCUT8_KEY,
	WCFG_KEY_WINDOW_SHORTCUT9_KEY,
	WCFG_KEY_WINDOW_SHORTCUT10_KEY,
	WCFG_KEY_MOVE_TO12TO6_HEAD,
	WCFG_KEY_MOVE_TO6TO12_HEAD,
	WCFG_KEY_WINDOW_RELAUNCH_KEY,
	WCFG_KEY_SCREEN_SWITCH_KEY,
	WCFG_KEY_RUN_KEY,
	WCFG_KEY_EXIT_KEY,
	WCFG_KEY_SCREEN_CAPTURE_KEY,
	WCFG_KEY_WINDOW_CAPTURE_KEY,
	WCFG_KEY_PARTIAL_CAPTURE_KEY,
	WCFG_KEY_MARK_SET_KEY,
	WCFG_KEY_MARK_UNSET_KEY,
	WCFG_KEY_MARK_BRING_KEY,
	WCFG_KEY_MARK_JUMP_KEY,
	WCFG_KEY_MARK_SWAP_KEY,
#ifdef KEEP_XKB_LOCK_STATUS
	WCFG_KEY_TOGGLE_KBD_MODE_KEY,
#endif
	WCFG_KEY_COUNT
} WConfigKey;

#define WCFG_CHANGE_WORDS ((WCFG_KEY_COUNT + 31u) / 32u)
#define WCFG_CHANGE_WORD(key) ((unsigned int)(key) / 32u)
#define WCFG_CHANGE_BIT(key) (1u << ((unsigned int)(key) % 32u))

typedef struct WConfig {
	int colormap_size;
	char disable_dithering;
	int icon_size;
	char *modifier_key;
	signed char focus_mode;
	signed char new_style;
	char disable_dock;
	char disable_clip;
	char disable_drawers;
	char clip_merged_in_dock;
	char disable_miniwindows;
	char enable_workspace_pager;
	int switch_panel_icon_size;
	signed char icon_yard;
	char *pixmap_path;
	char *icon_path;
	char sticky_icons;
	char wrap_appicons_in_dock;
	WConfigCoord window_place_origin;
	char keep_dock_on_primary_head;
	char *screenshot_filename_template;
	signed char menu_style;
	signed char title_justification;
	int window_title_clearance;
	int window_title_min_height;
	int window_title_max_height;
	int menu_title_clearance;
	int menu_title_min_height;
	int menu_title_max_height;
	int menu_text_clearance;
	signed char iconification_style;
	char enforce_icon_margin;
	char disable_root_mouse;
	signed char mouse_button1;
	signed char mouse_button2;
	signed char mouse_button3;
	signed char mouse_button8;
	signed char mouse_button9;
	signed char mouse_wheel_scroll;
	signed char mouse_wheel_tilt;
	char dont_confirm_kill;
	char window_balloon;
	char miniwin_title_balloon;
	char miniwin_preview_balloon;
	char appicon_balloon;
	char help_balloon;
	int edge_resistance;
	int resize_increment;
	char attract;
	char dont_blink;
	char single_click;
	char strict_windoze_cycle;
	char panel_only_open;
	int minipreview_size;
	char ignore_gtk_decoration_hints;
	char no_window_over_dock;
	char no_window_over_icons;
	signed char size_display;
	signed char move_display;
#ifdef USE_RANDR
	char hotplug_monitor;
#endif
	char hot_corners;
	int hot_corner_delay;
	int hot_corner_edge;
	char window_list_app_icons;
	char mouse_wheel_focus;
	int keychain_timeout_delay;
	char show_clip_title;
#ifdef KEEP_XKB_LOCK_STATUS
	char modelock;
#endif
	int history_lines;
	char cycle_active_head_only;
	char cycle_all_workspaces;
	char cycle_ignore_minimized;
	char double_click_fullscreen;
	char close_rootmenu_left_right_click;
	signed char colormap_mode;
	char auto_focus;
	char auto_focus_across_heads;
	int raise_delay;
	char circ_raise;
	char superfluous;
	char ws_advance;
	char ws_cycle;
	signed char workspace_name_display_position;
	signed char window_placement;
	signed char workspace_border_position;
	unsigned int workspace_border_size;
	char wrap_menus;
	char scrollable_menus;
	int double_click_time;
	char save_session_on_exit;
	signed char menu_scroll_speed;
	signed char icon_slide_speed;
	signed char shade_speed;
	char bounce_appicons_when_urgent;
	char raise_appicons_when_bouncing;
	char do_not_make_appicons_bounce;
	int clip_auto_raise_delay;
	int clip_auto_lower_delay;
	int clip_auto_expand_delay;
	int clip_auto_collapse_delay;
	char align_menus;
	char vi_key_menus;
	char open_transients_with_parent;
	char ignore_focus_click;
	char use_saveunders;
	char opaque_move;
	char opaque_resize;
	char opaque_move_resize_keyboard;
	char no_animations;
	char no_autowrap;
	char window_snapping;
	int snap_edge_detect;
	int snap_corner_detect;
	char snap_to_top_maximizes_fullscreen;
	signed char drag_maximized_window;
	char move_half_max_between_heads;
	char alt_half_maximize;
	char pointer_with_half_max_windows;
	char highlight_active_app;
	char auto_arrange_icons;
	WConfigResourceSpec workspace_specific_back;
	WConfigResourceSpec workspace_back;
	char smooth_workspace_back;
	WConfigResourceSpec widget_color;
	WConfigResourceSpec icon_back;
	WConfigResourceSpec window_title_font;
	WConfigResourceSpec menu_title_font;
	WConfigResourceSpec menu_text_font;
	WConfigResourceSpec icon_title_font;
	WConfigResourceSpec clip_title_font;
	WConfigResourceSpec large_display_font;
	WConfigResourceSpec highlight_color;
	WConfigResourceSpec highlight_text_color;
	WConfigResourceSpec clip_title_color;
	WConfigResourceSpec cclip_title_color;
	WConfigResourceSpec ftitle_color;
	WConfigResourceSpec ptitle_color;
	WConfigResourceSpec utitle_color;
	WConfigResourceSpec ftitle_back;
	WConfigResourceSpec ptitle_back;
	WConfigResourceSpec utitle_back;
	WConfigResourceSpec resizebar_back;
	WConfigResourceSpec menu_title_color;
	WConfigResourceSpec menu_text_color;
	WConfigResourceSpec menu_disabled_color;
	WConfigResourceSpec menu_title_back;
	WConfigResourceSpec menu_text_back;
	WConfigResourceSpec icon_title_color;
	WConfigResourceSpec icon_title_back;
	int frame_border_width;
	WConfigResourceSpec frame_border_color;
	WConfigResourceSpec frame_focused_border_color;
	WConfigResourceSpec frame_selected_border_color;
	WConfigResourceSpec workspace_map_back;
	WConfigResourceSpec normal_cursor;
	WConfigResourceSpec arrow_cursor;
	WConfigResourceSpec move_cursor;
	WConfigResourceSpec resize_cursor;
	WConfigResourceSpec top_left_resize_cursor;
	WConfigResourceSpec top_right_resize_cursor;
	WConfigResourceSpec bottom_left_resize_cursor;
	WConfigResourceSpec bottom_right_resize_cursor;
	WConfigResourceSpec vertical_resize_cursor;
	WConfigResourceSpec horizontal_resize_cursor;
	WConfigResourceSpec wait_cursor;
	WConfigResourceSpec question_cursor;
	WConfigResourceSpec text_cursor;
	WConfigResourceSpec select_cursor;
	WConfigResourceSpec capture_cursor;
	WConfigStringArray hot_corner_actions;
	WConfigSwitchPanelImages switch_panel_images;
	WConfigStringArray modifier_key_labels;
	WConfigStringArray modifier_key_short_labels;
	WConfigKeyBinding key_bindings[WKBD_LAST];
} WConfig;

typedef struct WConfigChanges {
	uint32_t changed[WCFG_CHANGE_WORDS];
} WConfigChanges;

WConfig *wConfigCreateDefault(void);
WConfig *wConfigReadWindowMaker(WMPropList *dict);
void wConfigDestroy(WConfig *config);

Bool wConfigDiff(const WConfig *old_config, const WConfig *new_config,
		 WConfigChanges *changes);
Bool wConfigChangesHas(const WConfigChanges *changes, WConfigKey key);
Bool wConfigRuntimePreferenceCopiesAreValid(void);

void wConfigApplyStatic(const WConfig *config);
unsigned int wConfigApplyRuntime(WScreen *scr, const WConfig *old_config,
				 const WConfig *new_config,
				 const WConfigChanges *changes);

void wConfigCacheWindowMaker(WMPropList *dict);
void wConfigAdoptWindowMaker(WConfig *config);
void wConfigClearWindowMaker(void);

void wConfigApplySwitchPanelImages(WScreen *scr,
				   const WConfigSwitchPanelImages *images);
void wConfigApplyModifierKeyLabels(WScreen *scr,
				   const WConfigStringArray *labels);
void wConfigApplyModifierKeyShortLabels(WScreen *scr,
					const WConfigStringArray *labels);
void wConfigApplyHotCornerActions(WScreen *scr,
				  const WConfigStringArray *actions);
void wConfigApplyModifierKey(const char *spec, const char *default_spec);
unsigned int wConfigApplyKeyBinding(WScreen *scr, const char *key,
				    int binding_index, const char *spec,
				    const char *default_spec,
				    Bool *binding_changed);
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
				   const WConfig *new_config,
				   WConfigChanges *changes);
/* Test convenience wrapper; production reloads prepare one diff and adopt it. */
unsigned int wConfigApplyRuntimeDefaults(WScreen *scr, WMPropList *new_dict);

#endif
