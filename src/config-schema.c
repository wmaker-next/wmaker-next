/*
 * Typed Window Maker defaults schema.
 */

#include "wconfig.h"

#include <strings.h>

#include "WindowMaker.h"
#include "config-schema.h"

#define FIELD(name) offsetof(WConfig, name), sizeof(((WConfig *)0)->name)
#define NUM2STRING_(x) #x
#define NUM2STRING(x) NUM2STRING_(x)

static const WConfigEnum focus_modes[] = {
	{"Manual", WKF_CLICK, 0},
	{"ClickToFocus", WKF_CLICK, 1},
	{"Sloppy", WKF_SLOPPY, 0},
	{"SemiAuto", WKF_SLOPPY, 1},
	{"Auto", WKF_SLOPPY, 1},
	{NULL, 0, 0}
};

static const WConfigEnum titlebar_modes[] = {
	{"new", TS_NEW, 0},
	{"old", TS_OLD, 0},
	{"next", TS_NEXT, 0},
	{NULL, 0, 0}
};

static const WConfigEnum icon_positions[] = {
	{"blv", IY_BOTTOM | IY_LEFT | IY_VERT, 0},
	{"blh", IY_BOTTOM | IY_LEFT | IY_HORIZ, 0},
	{"brv", IY_BOTTOM | IY_RIGHT | IY_VERT, 0},
	{"brh", IY_BOTTOM | IY_RIGHT | IY_HORIZ, 0},
	{"tlv", IY_TOP | IY_LEFT | IY_VERT, 0},
	{"tlh", IY_TOP | IY_LEFT | IY_HORIZ, 0},
	{"trv", IY_TOP | IY_RIGHT | IY_VERT, 0},
	{"trh", IY_TOP | IY_RIGHT | IY_HORIZ, 0},
	{NULL, 0, 0}
};

static const WConfigEnum menu_styles[] = {
	{"normal", MS_NORMAL, 0},
	{"singletexture", MS_SINGLE_TEXTURE, 0},
	{"flat", MS_FLAT, 0},
	{NULL, 0, 0}
};

static const WConfigEnum title_justifications[] = {
	{"Left", WTJ_LEFT, 0},
	{"Center", WTJ_CENTER, 0},
	{"Right", WTJ_RIGHT, 0},
	{NULL, 0, 0}
};

static const WConfigEnum iconification_styles[] = {
	{"Zoom", WIS_ZOOM, 0},
	{"Twist", WIS_TWIST, 0},
	{"Flip", WIS_FLIP, 0},
	{"None", WIS_NONE, 0},
	{"random", WIS_RANDOM, 0},
	{NULL, 0, 0}
};

static const WConfigEnum mouse_button_actions[] = {
	{"None", WA_NONE, 0},
	{"SelectWindows", WA_SELECT_WINDOWS, 0},
	{"OpenApplicationsMenu", WA_OPEN_APPMENU, 0},
	{"OpenWindowListMenu", WA_OPEN_WINLISTMENU, 0},
	{"MoveToPrevWorkspace", WA_MOVE_PREVWORKSPACE, 0},
	{"MoveToNextWorkspace", WA_MOVE_NEXTWORKSPACE, 0},
	{"MoveToPrevWindow", WA_MOVE_PREVWINDOW, 0},
	{"MoveToNextWindow", WA_MOVE_NEXTWINDOW, 0},
	{NULL, 0, 0}
};

static const WConfigEnum mouse_wheel_actions[] = {
	{"None", WA_NONE, 0},
	{"SwitchWorkspaces", WA_SWITCH_WORKSPACES, 0},
	{"SwitchWindows", WA_SWITCH_WINDOWS, 0},
	{NULL, 0, 0}
};

static const WConfigEnum geometry_displays[] = {
	{"None", WDIS_NONE, 0},
	{"Center", WDIS_CENTER, 0},
	{"Corner", WDIS_TOPLEFT, 0},
	{"Floating", WDIS_FRAME_CENTER, 0},
	{"Line", WDIS_NEW, 0},
	{NULL, 0, 0}
};

static const WConfigEnum colormap_modes[] = {
	{"Manual", WCM_CLICK, 0},
	{"ClickToFocus", WCM_CLICK, 1},
	{"Auto", WCM_POINTER, 0},
	{"FocusFollowMouse", WCM_POINTER, 1},
	{NULL, 0, 0}
};

static const WConfigEnum placements[] = {
	{"Auto", WPM_AUTO, 0},
	{"Smart", WPM_SMART, 0},
	{"Cascade", WPM_CASCADE, 0},
	{"Random", WPM_RANDOM, 0},
	{"Manual", WPM_MANUAL, 0},
	{"Center", WPM_CENTER, 0},
	{NULL, 0, 0}
};

static const WConfigEnum workspace_borders[] = {
	{"None", WB_NONE, 0},
	{"LeftRight", WB_LEFTRIGHT, 0},
	{"TopBottom", WB_TOPBOTTOM, 0},
	{"AllDirections", WB_ALLDIRS, 0},
	{NULL, 0, 0}
};

static const WConfigEnum display_positions[] = {
	{"none", WD_NONE, 0},
	{"center", WD_CENTER, 0},
	{"top", WD_TOP, 0},
	{"bottom", WD_BOTTOM, 0},
	{"topleft", WD_TOPLEFT, 0},
	{"topright", WD_TOPRIGHT, 0},
	{"bottomleft", WD_BOTTOMLEFT, 0},
	{"bottomright", WD_BOTTOMRIGHT, 0},
	{NULL, 0, 0}
};

static const WConfigEnum speeds[] = {
	{"UltraFast", SPEED_ULTRAFAST, 0},
	{"Fast", SPEED_FAST, 0},
	{"Medium", SPEED_MEDIUM, 0},
	{"Slow", SPEED_SLOW, 0},
	{"UltraSlow", SPEED_ULTRASLOW, 0},
	{NULL, 0, 0}
};

static const WConfigEnum drag_maximized_windows[] = {
	{"Move", DRAGMAX_MOVE, 0},
	{"RestoreGeometry", DRAGMAX_RESTORE, 0},
	{"Unmaximize", DRAGMAX_UNMAXIMIZE, 0},
	{"NoMove", DRAGMAX_NOMOVE, 0},
	{NULL, 0, 0}
};

static const WConfigEntry config_schema[] = {
	{
		"ColormapSize", WCFG_KEY_COLORMAP_SIZE, WCFG_INT, "4",
		FIELD(colormap_size), NULL, 0, INT_MAX, WCFG_STARTUP_ONLY, 0
	},
	{
		"DisableDithering", WCFG_KEY_DISABLE_DITHERING, WCFG_BOOL, "NO",
		FIELD(disable_dithering), NULL, 0, 1, WCFG_STARTUP_ONLY, 0
	},
	{
		"IconSize", WCFG_KEY_ICON_SIZE, WCFG_INT, "64",
		FIELD(icon_size), NULL, 1, INT_MAX, WCFG_STARTUP_ONLY, 0
	},
	{
		"ModifierKey", WCFG_KEY_MODIFIER_KEY, WCFG_STRING, "Mod1",
		FIELD(modifier_key), NULL, 0, 0, WCFG_STARTUP_ONLY, 0
	},
	{
		"FocusMode", WCFG_KEY_FOCUS_MODE, WCFG_ENUM, "manual",
		FIELD(focus_mode), focus_modes, INT_MIN, INT_MAX, WCFG_STARTUP_ONLY, 0
	},
	{
		"NewStyle", WCFG_KEY_NEW_STYLE, WCFG_ENUM, "new",
		FIELD(new_style), titlebar_modes, INT_MIN, INT_MAX, WCFG_STARTUP_ONLY, 0
	},
	{
		"DisableDock", WCFG_KEY_DISABLE_DOCK, WCFG_BOOL, "NO",
		FIELD(disable_dock), NULL, 0, 1, WCFG_STARTUP_ONLY, 0
	},
	{
		"DisableClip", WCFG_KEY_DISABLE_CLIP, WCFG_BOOL, "NO",
		FIELD(disable_clip), NULL, 0, 1, WCFG_STARTUP_ONLY, 0
	},
	{
		"DisableDrawers", WCFG_KEY_DISABLE_DRAWERS, WCFG_BOOL, "NO",
		FIELD(disable_drawers), NULL, 0, 1, WCFG_STARTUP_ONLY, 0
	},
	{
		"ClipMergedInDock", WCFG_KEY_CLIP_MERGED_IN_DOCK, WCFG_BOOL, "NO",
		FIELD(clip_merged_in_dock), NULL, 0, 1, WCFG_STARTUP_ONLY, 0
	},
	{
		"DisableMiniwindows", WCFG_KEY_DISABLE_MINIWINDOWS, WCFG_BOOL, "NO",
		FIELD(disable_miniwindows), NULL, 0, 1, WCFG_STARTUP_ONLY, 0
	},
	{
		"EnableWorkspacePager", WCFG_KEY_ENABLE_WORKSPACE_PAGER,
		WCFG_BOOL, "NO",
		FIELD(enable_workspace_pager), NULL, 0, 1, WCFG_STARTUP_ONLY, 0
	},
	{
		"SwitchPanelIconSize", WCFG_KEY_SWITCH_PANEL_ICON_SIZE,
		WCFG_INT, "64",
		FIELD(switch_panel_icon_size), NULL, INT_MIN, INT_MAX,
		WCFG_STARTUP_ONLY, 0
	},
	{
		"IconPosition", WCFG_KEY_ICON_POSITION, WCFG_ENUM, "blh",
		FIELD(icon_yard), icon_positions, INT_MIN, INT_MAX,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"PixmapPath", WCFG_KEY_PIXMAP_PATH, WCFG_PATH_LIST,
		DEF_PIXMAP_PATHS,
		FIELD(pixmap_path), NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"IconPath", WCFG_KEY_ICON_PATH, WCFG_PATH_LIST, DEF_ICON_PATHS,
		FIELD(icon_path), NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"StickyIcons", WCFG_KEY_STICKY_ICONS, WCFG_BOOL, "NO",
		FIELD(sticky_icons), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WrapAppiconsInDock", WCFG_KEY_WRAP_APPICONS_IN_DOCK,
		WCFG_BOOL, "YES",
		FIELD(wrap_appicons_in_dock), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WindowPlaceOrigin", WCFG_KEY_WINDOW_PLACE_ORIGIN,
		WCFG_COORD, "(64, 0)",
		FIELD(window_place_origin), NULL, INT_MIN, INT_MAX,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"KeepDockOnPrimaryHead", WCFG_KEY_KEEP_DOCK_ON_PRIMARY_HEAD,
		WCFG_BOOL, "NO",
		FIELD(keep_dock_on_primary_head), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"ScreenshotFilenameTemplate",
		WCFG_KEY_SCREENSHOT_FILENAME_TEMPLATE, WCFG_STRING,
		DEF_SCREENSHOT_FILENAME_TEMPLATE,
		FIELD(screenshot_filename_template), NULL, INT_MIN, INT_MAX,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MenuStyle", WCFG_KEY_MENU_STYLE, WCFG_ENUM, "normal",
		FIELD(menu_style), menu_styles, INT_MIN, INT_MAX,
		WCFG_RUNTIME_RELOAD, WCFG_REFRESH_MENU_TEXTURE
	},
	{
		"TitleJustify", WCFG_KEY_TITLE_JUSTIFY, WCFG_ENUM, "center",
		FIELD(title_justification), title_justifications, INT_MIN, INT_MAX,
		WCFG_RUNTIME_RELOAD, WCFG_REFRESH_WINDOW_TITLE_COLOR
	},
	{
		"WindowTitleExtendSpace", WCFG_KEY_WINDOW_TITLE_EXTEND_SPACE,
		WCFG_INT, DEF_WINDOW_TITLE_EXTEND_SPACE,
		FIELD(window_title_clearance), NULL, INT_MIN, INT_MAX,
		WCFG_RUNTIME_RELOAD, WCFG_REFRESH_WINDOW_FONT |
		    WCFG_REFRESH_BUTTON_IMAGES | WCFG_REFRESH_MENU_TITLE_FONT |
		    WCFG_REFRESH_MENU_FONT
	},
	{
		"WindowTitleMinHeight", WCFG_KEY_WINDOW_TITLE_MIN_HEIGHT,
		WCFG_INT, "0",
		FIELD(window_title_min_height), NULL, INT_MIN, INT_MAX,
		WCFG_RUNTIME_RELOAD, WCFG_REFRESH_WINDOW_FONT |
		    WCFG_REFRESH_BUTTON_IMAGES | WCFG_REFRESH_MENU_TITLE_FONT |
		    WCFG_REFRESH_MENU_FONT
	},
	{
		"WindowTitleMaxHeight", WCFG_KEY_WINDOW_TITLE_MAX_HEIGHT,
		WCFG_INT, NUM2STRING(INT_MAX),
		FIELD(window_title_max_height), NULL, INT_MIN, INT_MAX,
		WCFG_RUNTIME_RELOAD, WCFG_REFRESH_WINDOW_FONT |
		    WCFG_REFRESH_BUTTON_IMAGES | WCFG_REFRESH_MENU_TITLE_FONT |
		    WCFG_REFRESH_MENU_FONT
	},
	{
		"MenuTitleExtendSpace", WCFG_KEY_MENU_TITLE_EXTEND_SPACE,
		WCFG_INT, DEF_MENU_TITLE_EXTEND_SPACE,
		FIELD(menu_title_clearance), NULL, INT_MIN, INT_MAX,
		WCFG_RUNTIME_RELOAD, WCFG_REFRESH_WINDOW_FONT |
		    WCFG_REFRESH_BUTTON_IMAGES | WCFG_REFRESH_MENU_TITLE_FONT |
		    WCFG_REFRESH_MENU_FONT
	},
	{
		"MenuTitleMinHeight", WCFG_KEY_MENU_TITLE_MIN_HEIGHT,
		WCFG_INT, "0",
		FIELD(menu_title_min_height), NULL, INT_MIN, INT_MAX,
		WCFG_RUNTIME_RELOAD, WCFG_REFRESH_WINDOW_FONT |
		    WCFG_REFRESH_BUTTON_IMAGES | WCFG_REFRESH_MENU_TITLE_FONT |
		    WCFG_REFRESH_MENU_FONT
	},
	{
		"MenuTitleMaxHeight", WCFG_KEY_MENU_TITLE_MAX_HEIGHT,
		WCFG_INT, NUM2STRING(INT_MAX),
		FIELD(menu_title_max_height), NULL, INT_MIN, INT_MAX,
		WCFG_RUNTIME_RELOAD, WCFG_REFRESH_WINDOW_FONT |
		    WCFG_REFRESH_BUTTON_IMAGES | WCFG_REFRESH_MENU_TITLE_FONT |
		    WCFG_REFRESH_MENU_FONT
	},
	{
		"MenuTextExtendSpace", WCFG_KEY_MENU_TEXT_EXTEND_SPACE,
		WCFG_INT, DEF_MENU_TEXT_EXTEND_SPACE,
		FIELD(menu_text_clearance), NULL, INT_MIN, INT_MAX,
		WCFG_RUNTIME_RELOAD, WCFG_REFRESH_WINDOW_FONT |
		    WCFG_REFRESH_BUTTON_IMAGES | WCFG_REFRESH_MENU_TITLE_FONT |
		    WCFG_REFRESH_MENU_FONT
	},
	{
		"IconificationStyle", WCFG_KEY_ICONIFICATION_STYLE, WCFG_ENUM, "Zoom",
		FIELD(iconification_style), iconification_styles, INT_MIN, INT_MAX,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"EnforceIconMargin", WCFG_KEY_ENFORCE_ICON_MARGIN, WCFG_BOOL, "NO",
		FIELD(enforce_icon_margin), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"DisableWSMouseActions", WCFG_KEY_DISABLE_WS_MOUSE_ACTIONS,
		WCFG_BOOL, "NO",
		FIELD(disable_root_mouse), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MouseLeftButtonAction", WCFG_KEY_MOUSE_LEFT_BUTTON_ACTION,
		WCFG_ENUM, "SelectWindows",
		FIELD(mouse_button1), mouse_button_actions, INT_MIN, INT_MAX,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MouseMiddleButtonAction", WCFG_KEY_MOUSE_MIDDLE_BUTTON_ACTION,
		WCFG_ENUM, "OpenWindowListMenu",
		FIELD(mouse_button2), mouse_button_actions, INT_MIN, INT_MAX,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MouseRightButtonAction", WCFG_KEY_MOUSE_RIGHT_BUTTON_ACTION,
		WCFG_ENUM, "OpenApplicationsMenu",
		FIELD(mouse_button3), mouse_button_actions, INT_MIN, INT_MAX,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MouseBackwardButtonAction", WCFG_KEY_MOUSE_BACKWARD_BUTTON_ACTION,
		WCFG_ENUM, "None",
		FIELD(mouse_button8), mouse_button_actions, INT_MIN, INT_MAX,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MouseForwardButtonAction", WCFG_KEY_MOUSE_FORWARD_BUTTON_ACTION,
		WCFG_ENUM, "None",
		FIELD(mouse_button9), mouse_button_actions, INT_MIN, INT_MAX,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MouseWheelAction", WCFG_KEY_MOUSE_WHEEL_ACTION, WCFG_ENUM, "None",
		FIELD(mouse_wheel_scroll), mouse_wheel_actions, INT_MIN, INT_MAX,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MouseWheelTiltAction", WCFG_KEY_MOUSE_WHEEL_TILT_ACTION,
		WCFG_ENUM, "None",
		FIELD(mouse_wheel_tilt), mouse_wheel_actions, INT_MIN, INT_MAX,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"DontConfirmKill", WCFG_KEY_DONT_CONFIRM_KILL, WCFG_BOOL, "NO",
		FIELD(dont_confirm_kill), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WindowTitleBalloons", WCFG_KEY_WINDOW_TITLE_BALLOONS,
		WCFG_BOOL, "YES",
		FIELD(window_balloon), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MiniwindowTitleBalloons", WCFG_KEY_MINIWINDOW_TITLE_BALLOONS,
		WCFG_BOOL, "NO",
		FIELD(miniwin_title_balloon), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MiniwindowPreviewBalloons", WCFG_KEY_MINIWINDOW_PREVIEW_BALLOONS,
		WCFG_BOOL, "NO",
		FIELD(miniwin_preview_balloon), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"AppIconBalloons", WCFG_KEY_APPICON_BALLOONS, WCFG_BOOL, "NO",
		FIELD(appicon_balloon), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"HelpBalloons", WCFG_KEY_HELP_BALLOONS, WCFG_BOOL, "NO",
		FIELD(help_balloon), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"EdgeResistance", WCFG_KEY_EDGE_RESISTANCE, WCFG_INT, "30",
		FIELD(edge_resistance), NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"ResizeIncrement", WCFG_KEY_RESIZE_INCREMENT, WCFG_INT, "0",
		FIELD(resize_increment), NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"Attraction", WCFG_KEY_ATTRACTION, WCFG_BOOL, "NO",
		FIELD(attract), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"DisableBlinking", WCFG_KEY_DISABLE_BLINKING, WCFG_BOOL, "NO",
		FIELD(dont_blink), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"SingleClickLaunch", WCFG_KEY_SINGLE_CLICK_LAUNCH, WCFG_BOOL, "NO",
		FIELD(single_click), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"StrictWindozeCycle", WCFG_KEY_STRICT_WINDOZE_CYCLE,
		WCFG_BOOL, "YES",
		FIELD(strict_windoze_cycle), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"SwitchPanelOnlyOpen", WCFG_KEY_SWITCH_PANEL_ONLY_OPEN,
		WCFG_BOOL, "NO",
		FIELD(panel_only_open), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MiniPreviewSize", WCFG_KEY_MINI_PREVIEW_SIZE, WCFG_INT, "128",
		FIELD(minipreview_size), NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"IgnoreGtkHints", WCFG_KEY_IGNORE_GTK_HINTS, WCFG_BOOL, "NO",
		FIELD(ignore_gtk_decoration_hints), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"NoWindowOverDock", WCFG_KEY_NO_WINDOW_OVER_DOCK, WCFG_BOOL, "NO",
		FIELD(no_window_over_dock), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"NoWindowOverIcons", WCFG_KEY_NO_WINDOW_OVER_ICONS,
		WCFG_BOOL, "NO",
		FIELD(no_window_over_icons), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"ResizeDisplay", WCFG_KEY_RESIZE_DISPLAY, WCFG_ENUM, "center",
		FIELD(size_display), geometry_displays, INT_MIN, INT_MAX,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MoveDisplay", WCFG_KEY_MOVE_DISPLAY, WCFG_ENUM, "floating",
		FIELD(move_display), geometry_displays, INT_MIN, INT_MAX,
		WCFG_RUNTIME_RELOAD, 0
	},
#ifdef USE_RANDR
	{
		"HotplugMonitor", WCFG_KEY_HOTPLUG_MONITOR, WCFG_BOOL, "NO",
		FIELD(hotplug_monitor), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
#endif
	{
		"HotCorners", WCFG_KEY_HOT_CORNERS, WCFG_BOOL, "NO",
		FIELD(hot_corners), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"HotCornerDelay", WCFG_KEY_HOT_CORNER_DELAY, WCFG_INT, "250",
		FIELD(hot_corner_delay), NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"HotCornerEdge", WCFG_KEY_HOT_CORNER_EDGE, WCFG_INT, "2",
		FIELD(hot_corner_edge), NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WindowListAppIcons", WCFG_KEY_WINDOW_LIST_APP_ICONS,
		WCFG_BOOL, "NO",
		FIELD(window_list_app_icons), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MouseWheelFocus", WCFG_KEY_MOUSE_WHEEL_FOCUS, WCFG_BOOL, "NO",
		FIELD(mouse_wheel_focus), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"KeychainTimeoutDelay", WCFG_KEY_KEYCHAIN_TIMEOUT_DELAY,
		WCFG_INT, "500",
		FIELD(keychain_timeout_delay), NULL, INT_MIN, INT_MAX,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"ShowClipTitle", WCFG_KEY_SHOW_CLIP_TITLE, WCFG_BOOL, "YES",
		FIELD(show_clip_title), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
#ifdef KEEP_XKB_LOCK_STATUS
	{
		"KbdModeLock", WCFG_KEY_KBD_MODE_LOCK, WCFG_BOOL, "NO",
		FIELD(modelock), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
#endif
	{
		"DialogHistoryLines", WCFG_KEY_DIALOG_HISTORY_LINES,
		WCFG_INT, "500",
		FIELD(history_lines), NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"CycleActiveHeadOnly", WCFG_KEY_CYCLE_ACTIVE_HEAD_ONLY,
		WCFG_BOOL, "NO",
		FIELD(cycle_active_head_only), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"CycleAllWorkspaces", WCFG_KEY_CYCLE_ALL_WORKSPACES,
		WCFG_BOOL, "NO",
		FIELD(cycle_all_workspaces), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"CycleIgnoreMinimized", WCFG_KEY_CYCLE_IGNORE_MINIMIZED,
		WCFG_BOOL, "NO",
		FIELD(cycle_ignore_minimized), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"DbClickFullScreen", WCFG_KEY_DB_CLICK_FULL_SCREEN,
		WCFG_BOOL, "NO",
		FIELD(double_click_fullscreen), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"CloseRootMenuByLeftOrRightMouseClick",
		WCFG_KEY_CLOSE_ROOT_MENU_BY_LEFT_OR_RIGHT_MOUSE_CLICK,
		WCFG_BOOL, "NO",
		FIELD(close_rootmenu_left_right_click), NULL, 0, 1,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"ColormapMode", WCFG_KEY_COLORMAP_MODE, WCFG_ENUM, "auto",
		FIELD(colormap_mode), colormap_modes, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"AutoFocus", WCFG_KEY_AUTO_FOCUS, WCFG_BOOL, "YES",
		FIELD(auto_focus), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"AutoFocusAcrossHeads", WCFG_KEY_AUTO_FOCUS_ACROSS_HEADS, WCFG_BOOL, "YES",
		FIELD(auto_focus_across_heads), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"RaiseDelay", WCFG_KEY_RAISE_DELAY, WCFG_INT, "0",
		FIELD(raise_delay), NULL, 0, INT_MAX, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"CirculateRaise", WCFG_KEY_CIRCULATE_RAISE, WCFG_BOOL, "NO",
		FIELD(circ_raise), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"Superfluous", WCFG_KEY_SUPERFLUOUS, WCFG_BOOL, "YES",
		FIELD(superfluous), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"AdvanceToNewWorkspace", WCFG_KEY_ADVANCE_TO_NEW_WORKSPACE, WCFG_BOOL, "NO",
		FIELD(ws_advance), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"CycleWorkspaces", WCFG_KEY_CYCLE_WORKSPACES, WCFG_BOOL, "NO",
		FIELD(ws_cycle), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WorkspaceNameDisplayPosition",
		WCFG_KEY_WORKSPACE_NAME_DISPLAY_POSITION, WCFG_ENUM, "center",
		FIELD(workspace_name_display_position), display_positions, INT_MIN, INT_MAX,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WindowPlacement", WCFG_KEY_WINDOW_PLACEMENT, WCFG_ENUM, "auto",
		FIELD(window_placement), placements, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WorkspaceBorder", WCFG_KEY_WORKSPACE_BORDER, WCFG_ENUM, "None",
		FIELD(workspace_border_position), workspace_borders, INT_MIN, INT_MAX,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WorkspaceBorderSize", WCFG_KEY_WORKSPACE_BORDER_SIZE, WCFG_UINT, "0",
		FIELD(workspace_border_size), NULL, 0, INT_MAX, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WrapMenus", WCFG_KEY_WRAP_MENUS, WCFG_BOOL, "NO",
		FIELD(wrap_menus), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"ScrollableMenus", WCFG_KEY_SCROLLABLE_MENUS, WCFG_BOOL, "YES",
		FIELD(scrollable_menus), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"DoubleClickTime", WCFG_KEY_DOUBLE_CLICK_TIME, WCFG_INT, "250",
		FIELD(double_click_time), NULL, 1, INT_MAX, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"SaveSessionOnExit", WCFG_KEY_SAVE_SESSION_ON_EXIT, WCFG_BOOL, "NO",
		FIELD(save_session_on_exit), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MenuScrollSpeed", WCFG_KEY_MENU_SCROLL_SPEED, WCFG_ENUM, "fast",
		FIELD(menu_scroll_speed), speeds, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"IconSlideSpeed", WCFG_KEY_ICON_SLIDE_SPEED, WCFG_ENUM, "fast",
		FIELD(icon_slide_speed), speeds, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"ShadeSpeed", WCFG_KEY_SHADE_SPEED, WCFG_ENUM, "fast",
		FIELD(shade_speed), speeds, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"BounceAppIconsWhenUrgent", WCFG_KEY_BOUNCE_APPICONS_WHEN_URGENT,
		WCFG_BOOL, "YES",
		FIELD(bounce_appicons_when_urgent), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"RaiseAppIconsWhenBouncing", WCFG_KEY_RAISE_APPICONS_WHEN_BOUNCING,
		WCFG_BOOL, "NO",
		FIELD(raise_appicons_when_bouncing), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"DoNotMakeAppIconsBounce", WCFG_KEY_DO_NOT_MAKE_APPICONS_BOUNCE,
		WCFG_BOOL, "NO",
		FIELD(do_not_make_appicons_bounce), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"ClipAutoraiseDelay", WCFG_KEY_CLIP_AUTORAISE_DELAY, WCFG_INT, "600",
		FIELD(clip_auto_raise_delay), NULL, 0, INT_MAX, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"ClipAutolowerDelay", WCFG_KEY_CLIP_AUTOLOWER_DELAY, WCFG_INT, "1000",
		FIELD(clip_auto_lower_delay), NULL, 0, INT_MAX, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"ClipAutoexpandDelay", WCFG_KEY_CLIP_AUTOEXPAND_DELAY, WCFG_INT, "600",
		FIELD(clip_auto_expand_delay), NULL, 0, INT_MAX, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"ClipAutocollapseDelay", WCFG_KEY_CLIP_AUTOCOLLAPSE_DELAY, WCFG_INT, "1000",
		FIELD(clip_auto_collapse_delay), NULL, 0, INT_MAX, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"AlignSubmenus", WCFG_KEY_ALIGN_SUBMENUS, WCFG_BOOL, "NO",
		FIELD(align_menus), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"ViKeyMenus", WCFG_KEY_VI_KEY_MENUS, WCFG_BOOL, "NO",
		FIELD(vi_key_menus), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"OpenTransientOnOwnerWorkspace", WCFG_KEY_OPEN_TRANSIENT_ON_OWNER_WORKSPACE,
		WCFG_BOOL, "NO",
		FIELD(open_transients_with_parent), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"IgnoreFocusClick", WCFG_KEY_IGNORE_FOCUS_CLICK, WCFG_BOOL, "NO",
		FIELD(ignore_focus_click), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"UseSaveUnders", WCFG_KEY_USE_SAVE_UNDERS, WCFG_BOOL, "NO",
		FIELD(use_saveunders), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"OpaqueMove", WCFG_KEY_OPAQUE_MOVE, WCFG_BOOL, "YES",
		FIELD(opaque_move), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"OpaqueResize", WCFG_KEY_OPAQUE_RESIZE, WCFG_BOOL, "NO",
		FIELD(opaque_resize), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"OpaqueMoveResizeKeyboard", WCFG_KEY_OPAQUE_MOVE_RESIZE_KEYBOARD,
		WCFG_BOOL, "NO",
		FIELD(opaque_move_resize_keyboard), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"DisableAnimations", WCFG_KEY_DISABLE_ANIMATIONS, WCFG_BOOL, "NO",
		FIELD(no_animations), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"DontLinkWorkspaces", WCFG_KEY_DONT_LINK_WORKSPACES, WCFG_BOOL, "YES",
		FIELD(no_autowrap), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WindowSnapping", WCFG_KEY_WINDOW_SNAPPING, WCFG_BOOL, "NO",
		FIELD(window_snapping), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"SnapEdgeDetect", WCFG_KEY_SNAP_EDGE_DETECT, WCFG_INT, "1",
		FIELD(snap_edge_detect), NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"SnapCornerDetect", WCFG_KEY_SNAP_CORNER_DETECT, WCFG_INT, "10",
		FIELD(snap_corner_detect), NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"SnapToTopMaximizesFullscreen",
		WCFG_KEY_SNAP_TO_TOP_MAXIMIZES_FULLSCREEN, WCFG_BOOL, "NO",
		FIELD(snap_to_top_maximizes_fullscreen), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"DragMaximizedWindow", WCFG_KEY_DRAG_MAXIMIZED_WINDOW, WCFG_ENUM, "Move",
		FIELD(drag_maximized_window), drag_maximized_windows, INT_MIN, INT_MAX,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MoveHalfMaximizedWindowsBetweenScreens",
		WCFG_KEY_MOVE_HALF_MAXIMIZED_WINDOWS_BETWEEN_SCREENS, WCFG_BOOL, "NO",
		FIELD(move_half_max_between_heads), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"AlternativeHalfMaximized", WCFG_KEY_ALTERNATIVE_HALF_MAXIMIZED,
		WCFG_BOOL, "NO",
		FIELD(alt_half_maximize), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"PointerWithHalfMaxWindows", WCFG_KEY_POINTER_WITH_HALF_MAX_WINDOWS,
		WCFG_BOOL, "NO",
		FIELD(pointer_with_half_max_windows), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"HighlightActiveApp", WCFG_KEY_HIGHLIGHT_ACTIVE_APP, WCFG_BOOL, "YES",
		FIELD(highlight_active_app), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"AutoArrangeIcons", WCFG_KEY_AUTO_ARRANGE_ICONS, WCFG_BOOL, "NO",
		FIELD(auto_arrange_icons), NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WorkspaceSpecificBack", WCFG_KEY_WORKSPACE_SPECIFIC_BACK,
		WCFG_BACKGROUND_SPEC, "()", FIELD(workspace_specific_back), NULL,
		WCFG_BACKGROUND_WORKSPACE_SPECIFIC, WCFG_BACKGROUND_WORKSPACE_SPECIFIC,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WorkspaceBack", WCFG_KEY_WORKSPACE_BACK, WCFG_BACKGROUND_SPEC,
		"(solid, \"rgb:50/50/75\")", FIELD(workspace_back), NULL,
		WCFG_BACKGROUND_WORKSPACE, WCFG_BACKGROUND_WORKSPACE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"SmoothWorkspaceBack", WCFG_KEY_SMOOTH_WORKSPACE_BACK,
		WCFG_BOOL, "NO", FIELD(smooth_workspace_back), NULL, 0, 1,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WidgetColor", WCFG_KEY_WIDGET_COLOR, WCFG_TEXTURE_SPEC,
		"(solid, gray)", FIELD(widget_color), NULL,
		WCFG_TEXTURE_WIDGET_COLOR, WCFG_TEXTURE_WIDGET_COLOR,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"IconBack", WCFG_KEY_ICON_BACK, WCFG_TEXTURE_SPEC,
		"(dgradient, \"rgb:a6/a6/b6\", \"rgb:51/55/61\")",
		FIELD(icon_back), NULL, WCFG_TEXTURE_ICON_BACK,
		WCFG_TEXTURE_ICON_BACK, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WindowTitleFont", WCFG_KEY_WINDOW_TITLE_FONT, WCFG_FONT_SPEC,
		"Sans:bold:pixelsize=12", FIELD(window_title_font), NULL,
		WCFG_FONT_WINDOW_TITLE, WCFG_FONT_WINDOW_TITLE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MenuTitleFont", WCFG_KEY_MENU_TITLE_FONT, WCFG_FONT_SPEC,
		"Sans:bold:pixelsize=12", FIELD(menu_title_font), NULL,
		WCFG_FONT_MENU_TITLE, WCFG_FONT_MENU_TITLE, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MenuTextFont", WCFG_KEY_MENU_TEXT_FONT, WCFG_FONT_SPEC,
		"Sans:pixelsize=12", FIELD(menu_text_font), NULL,
		WCFG_FONT_MENU_TEXT, WCFG_FONT_MENU_TEXT, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"IconTitleFont", WCFG_KEY_ICON_TITLE_FONT, WCFG_FONT_SPEC,
		"Sans:pixelsize=9", FIELD(icon_title_font), NULL,
		WCFG_FONT_ICON_TITLE, WCFG_FONT_ICON_TITLE, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"ClipTitleFont", WCFG_KEY_CLIP_TITLE_FONT, WCFG_FONT_SPEC,
		"Sans:bold:pixelsize=10", FIELD(clip_title_font), NULL,
		WCFG_FONT_CLIP_TITLE, WCFG_FONT_CLIP_TITLE, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"LargeDisplayFont", WCFG_KEY_LARGE_DISPLAY_FONT, WCFG_FONT_SPEC,
		"Sans:pixelsize=24", FIELD(large_display_font), NULL,
		WCFG_FONT_LARGE_DISPLAY, WCFG_FONT_LARGE_DISPLAY,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"HighlightColor", WCFG_KEY_HIGHLIGHT_COLOR, WCFG_COLOR_SPEC,
		"white", FIELD(highlight_color), NULL, WCFG_COLOR_HIGHLIGHT,
		WCFG_COLOR_HIGHLIGHT, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"HighlightTextColor", WCFG_KEY_HIGHLIGHT_TEXT_COLOR,
		WCFG_COLOR_SPEC, "black", FIELD(highlight_text_color), NULL,
		WCFG_COLOR_HIGHLIGHT_TEXT, WCFG_COLOR_HIGHLIGHT_TEXT,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"ClipTitleColor", WCFG_KEY_CLIP_TITLE_COLOR, WCFG_COLOR_SPEC,
		"black", FIELD(clip_title_color), NULL, WCFG_COLOR_CLIP_TITLE,
		WCFG_COLOR_CLIP_TITLE, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"CClipTitleColor", WCFG_KEY_CCLIP_TITLE_COLOR, WCFG_COLOR_SPEC,
		"rgb:61/61/61", FIELD(cclip_title_color), NULL,
		WCFG_COLOR_C_CLIP_TITLE, WCFG_COLOR_C_CLIP_TITLE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"FTitleColor", WCFG_KEY_F_TITLE_COLOR, WCFG_COLOR_SPEC,
		"white", FIELD(ftitle_color), NULL, WCFG_COLOR_F_TITLE,
		WCFG_COLOR_F_TITLE, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"PTitleColor", WCFG_KEY_P_TITLE_COLOR, WCFG_COLOR_SPEC,
		"white", FIELD(ptitle_color), NULL, WCFG_COLOR_P_TITLE,
		WCFG_COLOR_P_TITLE, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"UTitleColor", WCFG_KEY_U_TITLE_COLOR, WCFG_COLOR_SPEC,
		"black", FIELD(utitle_color), NULL, WCFG_COLOR_U_TITLE,
		WCFG_COLOR_U_TITLE, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"FTitleBack", WCFG_KEY_F_TITLE_BACK, WCFG_TEXTURE_SPEC,
		"(solid, black)", FIELD(ftitle_back), NULL,
		WCFG_TEXTURE_F_TITLE_BACK, WCFG_TEXTURE_F_TITLE_BACK,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"PTitleBack", WCFG_KEY_P_TITLE_BACK, WCFG_TEXTURE_SPEC,
		"(solid, gray40)", FIELD(ptitle_back), NULL,
		WCFG_TEXTURE_P_TITLE_BACK, WCFG_TEXTURE_P_TITLE_BACK,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"UTitleBack", WCFG_KEY_U_TITLE_BACK, WCFG_TEXTURE_SPEC,
		"(solid, \"rgb:aa/aa/aa\")", FIELD(utitle_back), NULL,
		WCFG_TEXTURE_U_TITLE_BACK, WCFG_TEXTURE_U_TITLE_BACK,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"ResizebarBack", WCFG_KEY_RESIZEBAR_BACK, WCFG_TEXTURE_SPEC,
		"(solid, \"rgb:aa/aa/aa\")", FIELD(resizebar_back), NULL,
		WCFG_TEXTURE_RESIZEBAR_BACK, WCFG_TEXTURE_RESIZEBAR_BACK,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MenuTitleColor", WCFG_KEY_MENU_TITLE_COLOR, WCFG_COLOR_SPEC,
		"white", FIELD(menu_title_color), NULL, WCFG_COLOR_MENU_TITLE,
		WCFG_COLOR_MENU_TITLE, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MenuTextColor", WCFG_KEY_MENU_TEXT_COLOR, WCFG_COLOR_SPEC,
		"black", FIELD(menu_text_color), NULL, WCFG_COLOR_MENU_TEXT,
		WCFG_COLOR_MENU_TEXT, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MenuDisabledColor", WCFG_KEY_MENU_DISABLED_COLOR, WCFG_COLOR_SPEC,
		"gray50", FIELD(menu_disabled_color), NULL,
		WCFG_COLOR_MENU_DISABLED, WCFG_COLOR_MENU_DISABLED,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MenuTitleBack", WCFG_KEY_MENU_TITLE_BACK, WCFG_TEXTURE_SPEC,
		"(solid, black)", FIELD(menu_title_back), NULL,
		WCFG_TEXTURE_MENU_TITLE_BACK, WCFG_TEXTURE_MENU_TITLE_BACK,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MenuTextBack", WCFG_KEY_MENU_TEXT_BACK, WCFG_TEXTURE_SPEC,
		"(solid, \"rgb:aa/aa/aa\")", FIELD(menu_text_back), NULL,
		WCFG_TEXTURE_MENU_TEXT_BACK, WCFG_TEXTURE_MENU_TEXT_BACK,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"IconTitleColor", WCFG_KEY_ICON_TITLE_COLOR, WCFG_COLOR_SPEC,
		"white", FIELD(icon_title_color), NULL, WCFG_COLOR_ICON_TITLE,
		WCFG_COLOR_ICON_TITLE, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"IconTitleBack", WCFG_KEY_ICON_TITLE_BACK, WCFG_COLOR_SPEC,
		"black", FIELD(icon_title_back), NULL, WCFG_COLOR_ICON_TITLE_BACK,
		WCFG_COLOR_ICON_TITLE_BACK, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"FrameBorderWidth", WCFG_KEY_FRAME_BORDER_WIDTH, WCFG_INT, "1",
		FIELD(frame_border_width), NULL, INT_MIN, INT_MAX,
		WCFG_RUNTIME_RELOAD, WCFG_REFRESH_FRAME_BORDER
	},
	{
		"FrameBorderColor", WCFG_KEY_FRAME_BORDER_COLOR, WCFG_COLOR_SPEC,
		"black", FIELD(frame_border_color), NULL,
		WCFG_COLOR_FRAME_BORDER, WCFG_COLOR_FRAME_BORDER,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"FrameFocusedBorderColor", WCFG_KEY_FRAME_FOCUSED_BORDER_COLOR,
		WCFG_COLOR_SPEC, "black", FIELD(frame_focused_border_color), NULL,
		WCFG_COLOR_FRAME_FOCUSED_BORDER, WCFG_COLOR_FRAME_FOCUSED_BORDER,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"FrameSelectedBorderColor", WCFG_KEY_FRAME_SELECTED_BORDER_COLOR,
		WCFG_COLOR_SPEC, "white", FIELD(frame_selected_border_color), NULL,
		WCFG_COLOR_FRAME_SELECTED_BORDER, WCFG_COLOR_FRAME_SELECTED_BORDER,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WorkspaceMapBack", WCFG_KEY_WORKSPACE_MAP_BACK, WCFG_TEXTURE_SPEC,
		"(solid, black)", FIELD(workspace_map_back), NULL,
		WCFG_TEXTURE_WORKSPACE_MAP_BACK, WCFG_TEXTURE_WORKSPACE_MAP_BACK,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"NormalCursor", WCFG_KEY_NORMAL_CURSOR, WCFG_CURSOR_SPEC,
		"(builtin, left_ptr)", FIELD(normal_cursor), NULL,
		WCFG_CURSOR_NORMAL, WCFG_CURSOR_NORMAL, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"ArrowCursor", WCFG_KEY_ARROW_CURSOR, WCFG_CURSOR_SPEC,
		"(builtin, top_left_arrow)", FIELD(arrow_cursor), NULL,
		WCFG_CURSOR_ARROW, WCFG_CURSOR_ARROW, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MoveCursor", WCFG_KEY_MOVE_CURSOR, WCFG_CURSOR_SPEC,
		"(builtin, fleur)", FIELD(move_cursor), NULL,
		WCFG_CURSOR_MOVE, WCFG_CURSOR_MOVE, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"ResizeCursor", WCFG_KEY_RESIZE_CURSOR, WCFG_CURSOR_SPEC,
		"(builtin, sizing)", FIELD(resize_cursor), NULL,
		WCFG_CURSOR_RESIZE, WCFG_CURSOR_RESIZE, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"TopLeftResizeCursor", WCFG_KEY_TOP_LEFT_RESIZE_CURSOR,
		WCFG_CURSOR_SPEC, "(builtin, top_left_corner)",
		FIELD(top_left_resize_cursor), NULL, WCFG_CURSOR_TOP_LEFT_RESIZE,
		WCFG_CURSOR_TOP_LEFT_RESIZE, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"TopRightResizeCursor", WCFG_KEY_TOP_RIGHT_RESIZE_CURSOR,
		WCFG_CURSOR_SPEC, "(builtin, top_right_corner)",
		FIELD(top_right_resize_cursor), NULL, WCFG_CURSOR_TOP_RIGHT_RESIZE,
		WCFG_CURSOR_TOP_RIGHT_RESIZE, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"BottomLeftResizeCursor", WCFG_KEY_BOTTOM_LEFT_RESIZE_CURSOR,
		WCFG_CURSOR_SPEC, "(builtin, bottom_left_corner)",
		FIELD(bottom_left_resize_cursor), NULL,
		WCFG_CURSOR_BOTTOM_LEFT_RESIZE, WCFG_CURSOR_BOTTOM_LEFT_RESIZE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"BottomRightResizeCursor", WCFG_KEY_BOTTOM_RIGHT_RESIZE_CURSOR,
		WCFG_CURSOR_SPEC, "(builtin, bottom_right_corner)",
		FIELD(bottom_right_resize_cursor), NULL,
		WCFG_CURSOR_BOTTOM_RIGHT_RESIZE, WCFG_CURSOR_BOTTOM_RIGHT_RESIZE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"VerticalResizeCursor", WCFG_KEY_VERTICAL_RESIZE_CURSOR,
		WCFG_CURSOR_SPEC, "(builtin, sb_v_double_arrow)",
		FIELD(vertical_resize_cursor), NULL, WCFG_CURSOR_VERTICAL_RESIZE,
		WCFG_CURSOR_VERTICAL_RESIZE, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"HorizontalResizeCursor", WCFG_KEY_HORIZONTAL_RESIZE_CURSOR,
		WCFG_CURSOR_SPEC, "(builtin, sb_h_double_arrow)",
		FIELD(horizontal_resize_cursor), NULL,
		WCFG_CURSOR_HORIZONTAL_RESIZE, WCFG_CURSOR_HORIZONTAL_RESIZE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WaitCursor", WCFG_KEY_WAIT_CURSOR, WCFG_CURSOR_SPEC,
		"(builtin, watch)", FIELD(wait_cursor), NULL, WCFG_CURSOR_WAIT,
		WCFG_CURSOR_WAIT, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"QuestionCursor", WCFG_KEY_QUESTION_CURSOR, WCFG_CURSOR_SPEC,
		"(builtin, question_arrow)", FIELD(question_cursor), NULL,
		WCFG_CURSOR_QUESTION, WCFG_CURSOR_QUESTION, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"TextCursor", WCFG_KEY_TEXT_CURSOR, WCFG_CURSOR_SPEC,
		"(builtin, xterm)", FIELD(text_cursor), NULL, WCFG_CURSOR_TEXT,
		WCFG_CURSOR_TEXT, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"SelectCursor", WCFG_KEY_SELECT_CURSOR, WCFG_CURSOR_SPEC,
		"(builtin, cross)", FIELD(select_cursor), NULL,
		WCFG_CURSOR_SELECT, WCFG_CURSOR_SELECT, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"CaptureCursor", WCFG_KEY_CAPTURE_CURSOR, WCFG_CURSOR_SPEC,
		"(builtin, crosshair)", FIELD(capture_cursor), NULL,
		WCFG_CURSOR_CAPTURE, WCFG_CURSOR_CAPTURE, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"HotCornerActions", WCFG_KEY_HOT_CORNER_ACTIONS,
		WCFG_STRING_ARRAY, "(\"None\", \"None\", \"None\", \"None\")",
		FIELD(hot_corner_actions), NULL, 4, 4, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"SwitchPanelImages", WCFG_KEY_SWITCH_PANEL_IMAGES,
		WCFG_SWITCH_PANEL_IMAGES, "(swtile.png, swback.png, 30, 40)",
		FIELD(switch_panel_images), NULL, INT_MIN, INT_MAX,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"ModifierKeyLabels", WCFG_KEY_MODIFIER_KEY_LABELS,
		WCFG_STRING_ARRAY,
		"(\"Shift+\", \"Control+\", \"Mod1+\", \"Mod2+\", \"Mod3+\", \"Mod4+\", \"Mod5+\")",
		FIELD(modifier_key_labels), NULL, 7, 7, WCFG_RUNTIME_RELOAD, 0
	},
	{
		"ModifierKeyShortLabels", WCFG_KEY_MODIFIER_KEY_SHORT_LABELS,
		WCFG_STRING_ARRAY,
		"(\"Sh+\", \"^\", \"M1+\", \"M2+\", \"M3+\", \"M4+\", \"M5+\", \"M+\", \"A+\")",
		FIELD(modifier_key_short_labels), NULL, 9, 9,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"RootMenuKey", WCFG_KEY_ROOT_MENU_KEY, WCFG_KEY_BINDING, "F12",
		FIELD(key_bindings[WKBD_ROOTMENU]), NULL, WKBD_ROOTMENU, WKBD_ROOTMENU,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WindowListKey", WCFG_KEY_WINDOW_LIST_KEY, WCFG_KEY_BINDING, "F11",
		FIELD(key_bindings[WKBD_WINDOWLIST]), NULL, WKBD_WINDOWLIST, WKBD_WINDOWLIST,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WindowMenuKey", WCFG_KEY_WINDOW_MENU_KEY, WCFG_KEY_BINDING, "Control+Escape",
		FIELD(key_bindings[WKBD_WINDOWMENU]), NULL, WKBD_WINDOWMENU, WKBD_WINDOWMENU,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"KeychainCancelKey", WCFG_KEY_KEYCHAIN_CANCEL_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_KEYCHAIN_CANCEL]), NULL, WKBD_KEYCHAIN_CANCEL, WKBD_KEYCHAIN_CANCEL,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"DockRaiseLowerKey", WCFG_KEY_DOCK_RAISE_LOWER_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_DOCKRAISELOWER]), NULL, WKBD_DOCKRAISELOWER, WKBD_DOCKRAISELOWER,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"ClipRaiseLowerKey", WCFG_KEY_CLIP_RAISE_LOWER_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_CLIPRAISELOWER]), NULL, WKBD_CLIPRAISELOWER, WKBD_CLIPRAISELOWER,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MiniaturizeKey", WCFG_KEY_MINIATURIZE_KEY, WCFG_KEY_BINDING, "Mod1+M",
		FIELD(key_bindings[WKBD_MINIATURIZE]), NULL, WKBD_MINIATURIZE, WKBD_MINIATURIZE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MinimizeAllKey", WCFG_KEY_MINIMIZE_ALL_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_MINIMIZEALL]), NULL, WKBD_MINIMIZEALL, WKBD_MINIMIZEALL,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"HideKey", WCFG_KEY_HIDE_KEY, WCFG_KEY_BINDING, "Mod1+H",
		FIELD(key_bindings[WKBD_HIDE]), NULL, WKBD_HIDE, WKBD_HIDE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"HideOthersKey", WCFG_KEY_HIDE_OTHERS_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_HIDE_OTHERS]), NULL, WKBD_HIDE_OTHERS, WKBD_HIDE_OTHERS,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MoveResizeKey", WCFG_KEY_MOVE_RESIZE_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_MOVERESIZE]), NULL, WKBD_MOVERESIZE, WKBD_MOVERESIZE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"CloseKey", WCFG_KEY_CLOSE_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_CLOSE]), NULL, WKBD_CLOSE, WKBD_CLOSE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MaximizeKey", WCFG_KEY_MAXIMIZE_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_MAXIMIZE]), NULL, WKBD_MAXIMIZE, WKBD_MAXIMIZE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"VMaximizeKey", WCFG_KEY_V_MAXIMIZE_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_VMAXIMIZE]), NULL, WKBD_VMAXIMIZE, WKBD_VMAXIMIZE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"HMaximizeKey", WCFG_KEY_H_MAXIMIZE_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_HMAXIMIZE]), NULL, WKBD_HMAXIMIZE, WKBD_HMAXIMIZE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"LHMaximizeKey", WCFG_KEY_LH_MAXIMIZE_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_LHMAXIMIZE]), NULL, WKBD_LHMAXIMIZE, WKBD_LHMAXIMIZE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"RHMaximizeKey", WCFG_KEY_RH_MAXIMIZE_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_RHMAXIMIZE]), NULL, WKBD_RHMAXIMIZE, WKBD_RHMAXIMIZE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"THMaximizeKey", WCFG_KEY_TH_MAXIMIZE_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_THMAXIMIZE]), NULL, WKBD_THMAXIMIZE, WKBD_THMAXIMIZE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"BHMaximizeKey", WCFG_KEY_BH_MAXIMIZE_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_BHMAXIMIZE]), NULL, WKBD_BHMAXIMIZE, WKBD_BHMAXIMIZE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"TLCMaximizeKey", WCFG_KEY_TLC_MAXIMIZE_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_LTCMAXIMIZE]), NULL, WKBD_LTCMAXIMIZE, WKBD_LTCMAXIMIZE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"TRCMaximizeKey", WCFG_KEY_TRC_MAXIMIZE_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_RTCMAXIMIZE]), NULL, WKBD_RTCMAXIMIZE, WKBD_RTCMAXIMIZE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"BLCMaximizeKey", WCFG_KEY_BLC_MAXIMIZE_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_LBCMAXIMIZE]), NULL, WKBD_LBCMAXIMIZE, WKBD_LBCMAXIMIZE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"BRCMaximizeKey", WCFG_KEY_BRC_MAXIMIZE_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_RBCMAXIMIZE]), NULL, WKBD_RBCMAXIMIZE, WKBD_RBCMAXIMIZE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MaximusKey", WCFG_KEY_MAXIMUS_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_MAXIMUS]), NULL, WKBD_MAXIMUS, WKBD_MAXIMUS,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"CenterKey", WCFG_KEY_CENTER_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_CENTRAL]), NULL, WKBD_CENTRAL, WKBD_CENTRAL,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"KeepOnTopKey", WCFG_KEY_KEEP_ON_TOP_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_KEEP_ON_TOP]), NULL, WKBD_KEEP_ON_TOP, WKBD_KEEP_ON_TOP,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"KeepAtBottomKey", WCFG_KEY_KEEP_AT_BOTTOM_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_KEEP_AT_BOTTOM]), NULL, WKBD_KEEP_AT_BOTTOM, WKBD_KEEP_AT_BOTTOM,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"OmnipresentKey", WCFG_KEY_OMNIPRESENT_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_OMNIPRESENT]), NULL, WKBD_OMNIPRESENT, WKBD_OMNIPRESENT,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"RaiseKey", WCFG_KEY_RAISE_KEY, WCFG_KEY_BINDING, "Mod1+Up",
		FIELD(key_bindings[WKBD_RAISE]), NULL, WKBD_RAISE, WKBD_RAISE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"LowerKey", WCFG_KEY_LOWER_KEY, WCFG_KEY_BINDING, "Mod1+Down",
		FIELD(key_bindings[WKBD_LOWER]), NULL, WKBD_LOWER, WKBD_LOWER,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"RaiseLowerKey", WCFG_KEY_RAISE_LOWER_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_RAISELOWER]), NULL, WKBD_RAISELOWER, WKBD_RAISELOWER,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"ShadeKey", WCFG_KEY_SHADE_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_SHADE]), NULL, WKBD_SHADE, WKBD_SHADE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"SelectKey", WCFG_KEY_SELECT_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_SELECT]), NULL, WKBD_SELECT, WKBD_SELECT,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WorkspaceMapKey", WCFG_KEY_WORKSPACE_MAP_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_WORKSPACEMAP]), NULL, WKBD_WORKSPACEMAP, WKBD_WORKSPACEMAP,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"FocusNextKey", WCFG_KEY_FOCUS_NEXT_KEY, WCFG_KEY_BINDING, "Mod1+Tab",
		FIELD(key_bindings[WKBD_FOCUSNEXT]), NULL, WKBD_FOCUSNEXT, WKBD_FOCUSNEXT,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"FocusPrevKey", WCFG_KEY_FOCUS_PREV_KEY, WCFG_KEY_BINDING, "Mod1+Shift+Tab",
		FIELD(key_bindings[WKBD_FOCUSPREV]), NULL, WKBD_FOCUSPREV, WKBD_FOCUSPREV,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"FocusWindowLeftKey", WCFG_KEY_FOCUS_WINDOW_LEFT_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_FOCUSLEFT]), NULL, WKBD_FOCUSLEFT, WKBD_FOCUSLEFT,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"FocusWindowRightKey", WCFG_KEY_FOCUS_WINDOW_RIGHT_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_FOCUSRIGHT]), NULL, WKBD_FOCUSRIGHT, WKBD_FOCUSRIGHT,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"FocusWindowUpKey", WCFG_KEY_FOCUS_WINDOW_UP_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_FOCUSUP]), NULL, WKBD_FOCUSUP, WKBD_FOCUSUP,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"FocusWindowDownKey", WCFG_KEY_FOCUS_WINDOW_DOWN_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_FOCUSDOWN]), NULL, WKBD_FOCUSDOWN, WKBD_FOCUSDOWN,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"GroupNextKey", WCFG_KEY_GROUP_NEXT_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_GROUPNEXT]), NULL, WKBD_GROUPNEXT, WKBD_GROUPNEXT,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"GroupPrevKey", WCFG_KEY_GROUP_PREV_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_GROUPPREV]), NULL, WKBD_GROUPPREV, WKBD_GROUPPREV,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"NextWorkspaceKey", WCFG_KEY_NEXT_WORKSPACE_KEY, WCFG_KEY_BINDING, "Mod1+Control+Right",
		FIELD(key_bindings[WKBD_NEXTWORKSPACE]), NULL, WKBD_NEXTWORKSPACE, WKBD_NEXTWORKSPACE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"PrevWorkspaceKey", WCFG_KEY_PREV_WORKSPACE_KEY, WCFG_KEY_BINDING, "Mod1+Control+Left",
		FIELD(key_bindings[WKBD_PREVWORKSPACE]), NULL, WKBD_PREVWORKSPACE, WKBD_PREVWORKSPACE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"LastWorkspaceKey", WCFG_KEY_LAST_WORKSPACE_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_LASTWORKSPACE]), NULL, WKBD_LASTWORKSPACE, WKBD_LASTWORKSPACE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"NextWorkspaceLayerKey", WCFG_KEY_NEXT_WORKSPACE_LAYER_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_NEXTWSLAYER]), NULL, WKBD_NEXTWSLAYER, WKBD_NEXTWSLAYER,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"PrevWorkspaceLayerKey", WCFG_KEY_PREV_WORKSPACE_LAYER_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_PREVWSLAYER]), NULL, WKBD_PREVWSLAYER, WKBD_PREVWSLAYER,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"Workspace1Key", WCFG_KEY_WORKSPACE1_KEY, WCFG_KEY_BINDING, "Mod1+1",
		FIELD(key_bindings[WKBD_WORKSPACE1]), NULL, WKBD_WORKSPACE1, WKBD_WORKSPACE1,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"Workspace2Key", WCFG_KEY_WORKSPACE2_KEY, WCFG_KEY_BINDING, "Mod1+2",
		FIELD(key_bindings[WKBD_WORKSPACE2]), NULL, WKBD_WORKSPACE2, WKBD_WORKSPACE2,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"Workspace3Key", WCFG_KEY_WORKSPACE3_KEY, WCFG_KEY_BINDING, "Mod1+3",
		FIELD(key_bindings[WKBD_WORKSPACE3]), NULL, WKBD_WORKSPACE3, WKBD_WORKSPACE3,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"Workspace4Key", WCFG_KEY_WORKSPACE4_KEY, WCFG_KEY_BINDING, "Mod1+4",
		FIELD(key_bindings[WKBD_WORKSPACE4]), NULL, WKBD_WORKSPACE4, WKBD_WORKSPACE4,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"Workspace5Key", WCFG_KEY_WORKSPACE5_KEY, WCFG_KEY_BINDING, "Mod1+5",
		FIELD(key_bindings[WKBD_WORKSPACE5]), NULL, WKBD_WORKSPACE5, WKBD_WORKSPACE5,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"Workspace6Key", WCFG_KEY_WORKSPACE6_KEY, WCFG_KEY_BINDING, "Mod1+6",
		FIELD(key_bindings[WKBD_WORKSPACE6]), NULL, WKBD_WORKSPACE6, WKBD_WORKSPACE6,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"Workspace7Key", WCFG_KEY_WORKSPACE7_KEY, WCFG_KEY_BINDING, "Mod1+7",
		FIELD(key_bindings[WKBD_WORKSPACE7]), NULL, WKBD_WORKSPACE7, WKBD_WORKSPACE7,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"Workspace8Key", WCFG_KEY_WORKSPACE8_KEY, WCFG_KEY_BINDING, "Mod1+8",
		FIELD(key_bindings[WKBD_WORKSPACE8]), NULL, WKBD_WORKSPACE8, WKBD_WORKSPACE8,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"Workspace9Key", WCFG_KEY_WORKSPACE9_KEY, WCFG_KEY_BINDING, "Mod1+9",
		FIELD(key_bindings[WKBD_WORKSPACE9]), NULL, WKBD_WORKSPACE9, WKBD_WORKSPACE9,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"Workspace10Key", WCFG_KEY_WORKSPACE10_KEY, WCFG_KEY_BINDING, "Mod1+0",
		FIELD(key_bindings[WKBD_WORKSPACE10]), NULL, WKBD_WORKSPACE10, WKBD_WORKSPACE10,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MoveToWorkspace1Key", WCFG_KEY_MOVE_TO_WORKSPACE1_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_MOVE_WORKSPACE1]), NULL, WKBD_MOVE_WORKSPACE1, WKBD_MOVE_WORKSPACE1,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MoveToWorkspace2Key", WCFG_KEY_MOVE_TO_WORKSPACE2_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_MOVE_WORKSPACE2]), NULL, WKBD_MOVE_WORKSPACE2, WKBD_MOVE_WORKSPACE2,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MoveToWorkspace3Key", WCFG_KEY_MOVE_TO_WORKSPACE3_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_MOVE_WORKSPACE3]), NULL, WKBD_MOVE_WORKSPACE3, WKBD_MOVE_WORKSPACE3,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MoveToWorkspace4Key", WCFG_KEY_MOVE_TO_WORKSPACE4_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_MOVE_WORKSPACE4]), NULL, WKBD_MOVE_WORKSPACE4, WKBD_MOVE_WORKSPACE4,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MoveToWorkspace5Key", WCFG_KEY_MOVE_TO_WORKSPACE5_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_MOVE_WORKSPACE5]), NULL, WKBD_MOVE_WORKSPACE5, WKBD_MOVE_WORKSPACE5,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MoveToWorkspace6Key", WCFG_KEY_MOVE_TO_WORKSPACE6_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_MOVE_WORKSPACE6]), NULL, WKBD_MOVE_WORKSPACE6, WKBD_MOVE_WORKSPACE6,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MoveToWorkspace7Key", WCFG_KEY_MOVE_TO_WORKSPACE7_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_MOVE_WORKSPACE7]), NULL, WKBD_MOVE_WORKSPACE7, WKBD_MOVE_WORKSPACE7,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MoveToWorkspace8Key", WCFG_KEY_MOVE_TO_WORKSPACE8_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_MOVE_WORKSPACE8]), NULL, WKBD_MOVE_WORKSPACE8, WKBD_MOVE_WORKSPACE8,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MoveToWorkspace9Key", WCFG_KEY_MOVE_TO_WORKSPACE9_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_MOVE_WORKSPACE9]), NULL, WKBD_MOVE_WORKSPACE9, WKBD_MOVE_WORKSPACE9,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MoveToWorkspace10Key", WCFG_KEY_MOVE_TO_WORKSPACE10_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_MOVE_WORKSPACE10]), NULL, WKBD_MOVE_WORKSPACE10, WKBD_MOVE_WORKSPACE10,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MoveToNextWorkspaceKey", WCFG_KEY_MOVE_TO_NEXT_WORKSPACE_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_MOVE_NEXTWORKSPACE]), NULL, WKBD_MOVE_NEXTWORKSPACE, WKBD_MOVE_NEXTWORKSPACE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MoveToPrevWorkspaceKey", WCFG_KEY_MOVE_TO_PREV_WORKSPACE_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_MOVE_PREVWORKSPACE]), NULL, WKBD_MOVE_PREVWORKSPACE, WKBD_MOVE_PREVWORKSPACE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MoveToLastWorkspaceKey", WCFG_KEY_MOVE_TO_LAST_WORKSPACE_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_MOVE_LASTWORKSPACE]), NULL, WKBD_MOVE_LASTWORKSPACE, WKBD_MOVE_LASTWORKSPACE,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MoveToNextWorkspaceLayerKey", WCFG_KEY_MOVE_TO_NEXT_WORKSPACE_LAYER_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_MOVE_NEXTWSLAYER]), NULL, WKBD_MOVE_NEXTWSLAYER, WKBD_MOVE_NEXTWSLAYER,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MoveToPrevWorkspaceLayerKey", WCFG_KEY_MOVE_TO_PREV_WORKSPACE_LAYER_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_MOVE_PREVWSLAYER]), NULL, WKBD_MOVE_PREVWSLAYER, WKBD_MOVE_PREVWSLAYER,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WindowShortcut1Key", WCFG_KEY_WINDOW_SHORTCUT1_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_WINDOW1]), NULL, WKBD_WINDOW1, WKBD_WINDOW1,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WindowShortcut2Key", WCFG_KEY_WINDOW_SHORTCUT2_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_WINDOW2]), NULL, WKBD_WINDOW2, WKBD_WINDOW2,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WindowShortcut3Key", WCFG_KEY_WINDOW_SHORTCUT3_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_WINDOW3]), NULL, WKBD_WINDOW3, WKBD_WINDOW3,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WindowShortcut4Key", WCFG_KEY_WINDOW_SHORTCUT4_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_WINDOW4]), NULL, WKBD_WINDOW4, WKBD_WINDOW4,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WindowShortcut5Key", WCFG_KEY_WINDOW_SHORTCUT5_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_WINDOW5]), NULL, WKBD_WINDOW5, WKBD_WINDOW5,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WindowShortcut6Key", WCFG_KEY_WINDOW_SHORTCUT6_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_WINDOW6]), NULL, WKBD_WINDOW6, WKBD_WINDOW6,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WindowShortcut7Key", WCFG_KEY_WINDOW_SHORTCUT7_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_WINDOW7]), NULL, WKBD_WINDOW7, WKBD_WINDOW7,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WindowShortcut8Key", WCFG_KEY_WINDOW_SHORTCUT8_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_WINDOW8]), NULL, WKBD_WINDOW8, WKBD_WINDOW8,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WindowShortcut9Key", WCFG_KEY_WINDOW_SHORTCUT9_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_WINDOW9]), NULL, WKBD_WINDOW9, WKBD_WINDOW9,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WindowShortcut10Key", WCFG_KEY_WINDOW_SHORTCUT10_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_WINDOW10]), NULL, WKBD_WINDOW10, WKBD_WINDOW10,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MoveTo12to6Head", WCFG_KEY_MOVE_TO12TO6_HEAD, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_MOVE_12_TO_6_HEAD]), NULL, WKBD_MOVE_12_TO_6_HEAD, WKBD_MOVE_12_TO_6_HEAD,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MoveTo6to12Head", WCFG_KEY_MOVE_TO6TO12_HEAD, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_MOVE_6_TO_12_HEAD]), NULL, WKBD_MOVE_6_TO_12_HEAD, WKBD_MOVE_6_TO_12_HEAD,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WindowRelaunchKey", WCFG_KEY_WINDOW_RELAUNCH_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_RELAUNCH]), NULL, WKBD_RELAUNCH, WKBD_RELAUNCH,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"ScreenSwitchKey", WCFG_KEY_SCREEN_SWITCH_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_SWITCH_SCREEN]), NULL, WKBD_SWITCH_SCREEN, WKBD_SWITCH_SCREEN,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"RunKey", WCFG_KEY_RUN_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_RUN]), NULL, WKBD_RUN, WKBD_RUN,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"ExitKey", WCFG_KEY_EXIT_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_EXIT]), NULL, WKBD_EXIT, WKBD_EXIT,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"ScreenCaptureKey", WCFG_KEY_SCREEN_CAPTURE_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_PRINTS]), NULL, WKBD_PRINTS, WKBD_PRINTS,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"WindowCaptureKey", WCFG_KEY_WINDOW_CAPTURE_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_PRINTW]), NULL, WKBD_PRINTW, WKBD_PRINTW,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"PartialCaptureKey", WCFG_KEY_PARTIAL_CAPTURE_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_PRINTP]), NULL, WKBD_PRINTP, WKBD_PRINTP,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MarkSetKey", WCFG_KEY_MARK_SET_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_MARK_SET]), NULL, WKBD_MARK_SET, WKBD_MARK_SET,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MarkUnsetKey", WCFG_KEY_MARK_UNSET_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_MARK_UNSET]), NULL, WKBD_MARK_UNSET, WKBD_MARK_UNSET,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MarkBringKey", WCFG_KEY_MARK_BRING_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_MARK_BRING]), NULL, WKBD_MARK_BRING, WKBD_MARK_BRING,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MarkJumpKey", WCFG_KEY_MARK_JUMP_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_MARK_JUMP]), NULL, WKBD_MARK_JUMP, WKBD_MARK_JUMP,
		WCFG_RUNTIME_RELOAD, 0
	},
	{
		"MarkSwapKey", WCFG_KEY_MARK_SWAP_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_MARK_SWAP]), NULL, WKBD_MARK_SWAP, WKBD_MARK_SWAP,
		WCFG_RUNTIME_RELOAD, 0
	},
#ifdef KEEP_XKB_LOCK_STATUS
	{
		"ToggleKbdModeKey", WCFG_KEY_TOGGLE_KBD_MODE_KEY, WCFG_KEY_BINDING, "None",
		FIELD(key_bindings[WKBD_TOGGLE]), NULL, WKBD_TOGGLE, WKBD_TOGGLE,
		WCFG_RUNTIME_RELOAD, 0
	},
#endif
};

_Static_assert(sizeof(config_schema) / sizeof(config_schema[0]) == WCFG_KEY_COUNT,
	       "config schema/key enum mismatch");

const WConfigEntry *wConfigSchema(void)
{
	return config_schema;
}

size_t wConfigSchemaCount(void)
{
	return sizeof(config_schema) / sizeof(config_schema[0]);
}

const WConfigEntry *wConfigEntryForKey(const char *key)
{
	size_t i;

	if (!key)
		return NULL;

	for (i = 0; i < wConfigSchemaCount(); i++)
		if (strcasecmp(config_schema[i].key, key) == 0)
			return &config_schema[i];

	return NULL;
}
