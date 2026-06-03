/*
 * Typed Window Maker defaults schema, X-macro key list.
 *
 * Each entry describes one configuration key. The list is expanded at
 * four sites: the WConfigKey enum and the WConfig struct (wconfigmodel.h),
 * the config_schema[] table (config-schema.c), and the
 * runtime_preference_copies[] bridge table (wconfigmodel.c). Adding a
 * plain scalar key now means adding one row here.
 *
 * WCFG_SCHEMA(_) columns:
 *   K       enum suffix, expands to WCFG_KEY_##K
 *   name    proplist key string
 *   T       WConfigType suffix, expands to WCFG_##T, also selects the
 *           C field type via WCFG_CTYPE_##T
 *   field   WConfig struct field name
 *   def     default value as a string (or proplist description)
 *   etable  WConfigEnum table for WCFG_ENUM, NULL otherwise
 *   lo, hi  numeric bounds for scalars, target/count for non-numeric
 *   fl      WCFG_STARTUP_ONLY or WCFG_RUNTIME_RELOAD
 *   refresh refresh mask raised when this key changes at runtime
 *   bridge  BYTES, STRING or NONE, picks the wPreferences bridge copy
 *   pref    wPreferences field name when bridge != NONE
 *
 * WCFG_SCHEMA_KEYBINDINGS(_) columns:
 *   K       enum suffix
 *   name    proplist key string
 *   def     default key spec string
 *   wkbd    WKBD_* index
 */

#ifndef WMCONFIG_SCHEMA_DEF_H_
#define WMCONFIG_SCHEMA_DEF_H_

#ifdef USE_RANDR
# define WCFG_IF_RANDR(x) x
#else
# define WCFG_IF_RANDR(x)
#endif

#ifdef KEEP_XKB_LOCK_STATUS
# define WCFG_IF_XKB_LOCK(x) x
#else
# define WCFG_IF_XKB_LOCK(x)
#endif

/* C field type for each WConfigType, used by the WConfig struct expansion */
#define WCFG_CTYPE_BOOL                char
#define WCFG_CTYPE_INT                 int
#define WCFG_CTYPE_UINT                unsigned int
#define WCFG_CTYPE_ENUM                signed char
#define WCFG_CTYPE_STRING              char *
#define WCFG_CTYPE_PATH_LIST           char *
#define WCFG_CTYPE_COORD               WConfigCoord
#define WCFG_CTYPE_STRING_ARRAY        WConfigStringArray
#define WCFG_CTYPE_SWITCH_PANEL_IMAGES WConfigSwitchPanelImages
#define WCFG_CTYPE_KEY_BINDING         WConfigKeyBinding
#define WCFG_CTYPE_TEXTURE_SPEC        WConfigResourceSpec
#define WCFG_CTYPE_FONT_SPEC           WConfigResourceSpec
#define WCFG_CTYPE_COLOR_SPEC          WConfigResourceSpec
#define WCFG_CTYPE_CURSOR_SPEC         WConfigResourceSpec
#define WCFG_CTYPE_BACKGROUND_SPEC     WConfigResourceSpec

#define WCFG_REFRESH_TITLE_GEOM \
	(WCFG_REFRESH_WINDOW_FONT | WCFG_REFRESH_BUTTON_IMAGES | \
	 WCFG_REFRESH_MENU_TITLE_FONT | WCFG_REFRESH_MENU_FONT)

#define WCFG_SCHEMA(_) \
	_(COLORMAP_SIZE, "ColormapSize", INT, colormap_size, "4", \
	  NULL, 0, INT_MAX, WCFG_STARTUP_ONLY, 0, NONE, colormap_size) \
	_(DISABLE_DITHERING, "DisableDithering", BOOL, disable_dithering, "NO", \
	  NULL, 0, 1, WCFG_STARTUP_ONLY, 0, NONE, disable_dithering) \
	_(ICON_SIZE, "IconSize", INT, icon_size, "64", \
	  NULL, 1, INT_MAX, WCFG_STARTUP_ONLY, 0, NONE, icon_size) \
	_(MODIFIER_KEY, "ModifierKey", STRING, modifier_key, "Mod1", \
	  NULL, 0, 0, WCFG_STARTUP_ONLY, 0, NONE, modifier_key) \
	_(FOCUS_MODE, "FocusMode", ENUM, focus_mode, "manual", \
	  focus_modes, INT_MIN, INT_MAX, WCFG_STARTUP_ONLY, 0, NONE, focus_mode) \
	_(NEW_STYLE, "NewStyle", ENUM, new_style, "new", \
	  titlebar_modes, INT_MIN, INT_MAX, WCFG_STARTUP_ONLY, 0, NONE, new_style) \
	_(DISABLE_DOCK, "DisableDock", BOOL, disable_dock, "NO", \
	  NULL, 0, 1, WCFG_STARTUP_ONLY, 0, NONE, disable_dock) \
	_(DISABLE_CLIP, "DisableClip", BOOL, disable_clip, "NO", \
	  NULL, 0, 1, WCFG_STARTUP_ONLY, 0, NONE, disable_clip) \
	_(DISABLE_DRAWERS, "DisableDrawers", BOOL, disable_drawers, "NO", \
	  NULL, 0, 1, WCFG_STARTUP_ONLY, 0, NONE, disable_drawers) \
	_(CLIP_MERGED_IN_DOCK, "ClipMergedInDock", BOOL, clip_merged_in_dock, "NO", \
	  NULL, 0, 1, WCFG_STARTUP_ONLY, 0, NONE, clip_merged_in_dock) \
	_(DISABLE_MINIWINDOWS, "DisableMiniwindows", BOOL, disable_miniwindows, "NO", \
	  NULL, 0, 1, WCFG_STARTUP_ONLY, 0, NONE, disable_miniwindows) \
	_(ENABLE_WORKSPACE_PAGER, "EnableWorkspacePager", BOOL, enable_workspace_pager, "NO", \
	  NULL, 0, 1, WCFG_STARTUP_ONLY, 0, NONE, enable_workspace_pager) \
	_(SWITCH_PANEL_ICON_SIZE, "SwitchPanelIconSize", INT, switch_panel_icon_size, "64", \
	  NULL, INT_MIN, INT_MAX, WCFG_STARTUP_ONLY, 0, NONE, switch_panel_icon_size) \
	_(ICON_POSITION, "IconPosition", ENUM, icon_yard, "blh", \
	  icon_positions, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0, BYTES, icon_yard) \
	_(PIXMAP_PATH, "PixmapPath", PATH_LIST, pixmap_path, DEF_PIXMAP_PATHS, \
	  NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0, STRING, pixmap_path) \
	_(ICON_PATH, "IconPath", PATH_LIST, icon_path, DEF_ICON_PATHS, \
	  NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0, STRING, icon_path) \
	_(STICKY_ICONS, "StickyIcons", BOOL, sticky_icons, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, sticky_icons) \
	_(WRAP_APPICONS_IN_DOCK, "WrapAppiconsInDock", BOOL, wrap_appicons_in_dock, "YES", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, NONE, wrap_appicons_in_dock) \
	_(WINDOW_PLACE_ORIGIN, "WindowPlaceOrigin", COORD, window_place_origin, "(64, 0)", \
	  NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0, NONE, window_place_origin) \
	_(KEEP_DOCK_ON_PRIMARY_HEAD, "KeepDockOnPrimaryHead", BOOL, keep_dock_on_primary_head, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, keep_dock_on_primary_head) \
	_(SCREENSHOT_FILENAME_TEMPLATE, "ScreenshotFilenameTemplate", STRING, \
	  screenshot_filename_template, DEF_SCREENSHOT_FILENAME_TEMPLATE, \
	  NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0, STRING, screenshot_filename_template) \
	_(MENU_STYLE, "MenuStyle", ENUM, menu_style, "normal", \
	  menu_styles, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, \
	  WCFG_REFRESH_MENU_TEXTURE, BYTES, menu_style) \
	_(TITLE_JUSTIFY, "TitleJustify", ENUM, title_justification, "center", \
	  title_justifications, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, \
	  WCFG_REFRESH_WINDOW_TITLE_COLOR, BYTES, title_justification) \
	_(WINDOW_TITLE_EXTEND_SPACE, "WindowTitleExtendSpace", INT, window_title_clearance, \
	  DEF_WINDOW_TITLE_EXTEND_SPACE, NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, \
	  WCFG_REFRESH_TITLE_GEOM, BYTES, window_title_clearance) \
	_(WINDOW_TITLE_MIN_HEIGHT, "WindowTitleMinHeight", INT, window_title_min_height, "0", \
	  NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, WCFG_REFRESH_TITLE_GEOM, \
	  BYTES, window_title_min_height) \
	_(WINDOW_TITLE_MAX_HEIGHT, "WindowTitleMaxHeight", INT, window_title_max_height, \
	  NUM2STRING(INT_MAX), NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, \
	  WCFG_REFRESH_TITLE_GEOM, BYTES, window_title_max_height) \
	_(MENU_TITLE_EXTEND_SPACE, "MenuTitleExtendSpace", INT, menu_title_clearance, \
	  DEF_MENU_TITLE_EXTEND_SPACE, NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, \
	  WCFG_REFRESH_TITLE_GEOM, BYTES, menu_title_clearance) \
	_(MENU_TITLE_MIN_HEIGHT, "MenuTitleMinHeight", INT, menu_title_min_height, "0", \
	  NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, WCFG_REFRESH_TITLE_GEOM, \
	  BYTES, menu_title_min_height) \
	_(MENU_TITLE_MAX_HEIGHT, "MenuTitleMaxHeight", INT, menu_title_max_height, \
	  NUM2STRING(INT_MAX), NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, \
	  WCFG_REFRESH_TITLE_GEOM, BYTES, menu_title_max_height) \
	_(MENU_TEXT_EXTEND_SPACE, "MenuTextExtendSpace", INT, menu_text_clearance, \
	  DEF_MENU_TEXT_EXTEND_SPACE, NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, \
	  WCFG_REFRESH_TITLE_GEOM, BYTES, menu_text_clearance) \
	_(ICONIFICATION_STYLE, "IconificationStyle", ENUM, iconification_style, "Zoom", \
	  iconification_styles, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0, \
	  BYTES, iconification_style) \
	_(ENFORCE_ICON_MARGIN, "EnforceIconMargin", BOOL, enforce_icon_margin, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, enforce_icon_margin) \
	_(DISABLE_WS_MOUSE_ACTIONS, "DisableWSMouseActions", BOOL, disable_root_mouse, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, disable_root_mouse) \
	_(MOUSE_LEFT_BUTTON_ACTION, "MouseLeftButtonAction", ENUM, mouse_button1, \
	  "SelectWindows", mouse_button_actions, INT_MIN, INT_MAX, \
	  WCFG_RUNTIME_RELOAD, 0, BYTES, mouse_button1) \
	_(MOUSE_MIDDLE_BUTTON_ACTION, "MouseMiddleButtonAction", ENUM, mouse_button2, \
	  "OpenWindowListMenu", mouse_button_actions, INT_MIN, INT_MAX, \
	  WCFG_RUNTIME_RELOAD, 0, BYTES, mouse_button2) \
	_(MOUSE_RIGHT_BUTTON_ACTION, "MouseRightButtonAction", ENUM, mouse_button3, \
	  "OpenApplicationsMenu", mouse_button_actions, INT_MIN, INT_MAX, \
	  WCFG_RUNTIME_RELOAD, 0, BYTES, mouse_button3) \
	_(MOUSE_BACKWARD_BUTTON_ACTION, "MouseBackwardButtonAction", ENUM, mouse_button8, \
	  "None", mouse_button_actions, INT_MIN, INT_MAX, \
	  WCFG_RUNTIME_RELOAD, 0, BYTES, mouse_button8) \
	_(MOUSE_FORWARD_BUTTON_ACTION, "MouseForwardButtonAction", ENUM, mouse_button9, \
	  "None", mouse_button_actions, INT_MIN, INT_MAX, \
	  WCFG_RUNTIME_RELOAD, 0, BYTES, mouse_button9) \
	_(MOUSE_WHEEL_ACTION, "MouseWheelAction", ENUM, mouse_wheel_scroll, "None", \
	  mouse_wheel_actions, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0, \
	  BYTES, mouse_wheel_scroll) \
	_(MOUSE_WHEEL_TILT_ACTION, "MouseWheelTiltAction", ENUM, mouse_wheel_tilt, "None", \
	  mouse_wheel_actions, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0, \
	  BYTES, mouse_wheel_tilt) \
	_(DONT_CONFIRM_KILL, "DontConfirmKill", BOOL, dont_confirm_kill, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, dont_confirm_kill) \
	_(WINDOW_TITLE_BALLOONS, "WindowTitleBalloons", BOOL, window_balloon, "YES", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, window_balloon) \
	_(MINIWINDOW_TITLE_BALLOONS, "MiniwindowTitleBalloons", BOOL, miniwin_title_balloon, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, miniwin_title_balloon) \
	_(MINIWINDOW_PREVIEW_BALLOONS, "MiniwindowPreviewBalloons", BOOL, miniwin_preview_balloon, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, miniwin_preview_balloon) \
	_(APPICON_BALLOONS, "AppIconBalloons", BOOL, appicon_balloon, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, appicon_balloon) \
	_(HELP_BALLOONS, "HelpBalloons", BOOL, help_balloon, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, help_balloon) \
	_(EDGE_RESISTANCE, "EdgeResistance", INT, edge_resistance, "30", \
	  NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0, BYTES, edge_resistance) \
	_(RESIZE_INCREMENT, "ResizeIncrement", INT, resize_increment, "0", \
	  NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0, BYTES, resize_increment) \
	_(ATTRACTION, "Attraction", BOOL, attract, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, attract) \
	_(DISABLE_BLINKING, "DisableBlinking", BOOL, dont_blink, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, dont_blink) \
	_(SINGLE_CLICK_LAUNCH, "SingleClickLaunch", BOOL, single_click, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, single_click) \
	_(STRICT_WINDOZE_CYCLE, "StrictWindozeCycle", BOOL, strict_windoze_cycle, "YES", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, strict_windoze_cycle) \
	_(SWITCH_PANEL_ONLY_OPEN, "SwitchPanelOnlyOpen", BOOL, panel_only_open, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, panel_only_open) \
	_(MINI_PREVIEW_SIZE, "MiniPreviewSize", INT, minipreview_size, "128", \
	  NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0, BYTES, minipreview_size) \
	_(IGNORE_GTK_HINTS, "IgnoreGtkHints", BOOL, ignore_gtk_decoration_hints, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, ignore_gtk_decoration_hints) \
	_(NO_WINDOW_OVER_DOCK, "NoWindowOverDock", BOOL, no_window_over_dock, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, no_window_over_dock) \
	_(NO_WINDOW_OVER_ICONS, "NoWindowOverIcons", BOOL, no_window_over_icons, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, no_window_over_icons) \
	_(RESIZE_DISPLAY, "ResizeDisplay", ENUM, size_display, "center", \
	  geometry_displays, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0, BYTES, size_display) \
	_(MOVE_DISPLAY, "MoveDisplay", ENUM, move_display, "floating", \
	  geometry_displays, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0, BYTES, move_display) \
	WCFG_IF_RANDR( \
	_(HOTPLUG_MONITOR, "HotplugMonitor", BOOL, hotplug_monitor, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, hotplug_monitor) \
	) \
	_(HOT_CORNERS, "HotCorners", BOOL, hot_corners, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, hot_corners) \
	_(HOT_CORNER_DELAY, "HotCornerDelay", INT, hot_corner_delay, "250", \
	  NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0, BYTES, hot_corner_delay) \
	_(HOT_CORNER_EDGE, "HotCornerEdge", INT, hot_corner_edge, "2", \
	  NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0, BYTES, hot_corner_edge) \
	_(WINDOW_LIST_APP_ICONS, "WindowListAppIcons", BOOL, window_list_app_icons, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, window_list_app_icons) \
	_(MOUSE_WHEEL_FOCUS, "MouseWheelFocus", BOOL, mouse_wheel_focus, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, mouse_wheel_focus) \
	_(KEYCHAIN_TIMEOUT_DELAY, "KeychainTimeoutDelay", INT, keychain_timeout_delay, "500", \
	  NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0, BYTES, keychain_timeout_delay) \
	_(SHOW_CLIP_TITLE, "ShowClipTitle", BOOL, show_clip_title, "YES", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, show_clip_title) \
	WCFG_IF_XKB_LOCK( \
	_(KBD_MODE_LOCK, "KbdModeLock", BOOL, modelock, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, modelock) \
	) \
	_(DIALOG_HISTORY_LINES, "DialogHistoryLines", INT, history_lines, "500", \
	  NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0, BYTES, history_lines) \
	_(CYCLE_ACTIVE_HEAD_ONLY, "CycleActiveHeadOnly", BOOL, cycle_active_head_only, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, cycle_active_head_only) \
	_(CYCLE_ALL_WORKSPACES, "CycleAllWorkspaces", BOOL, cycle_all_workspaces, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, cycle_all_workspaces) \
	_(CYCLE_IGNORE_MINIMIZED, "CycleIgnoreMinimized", BOOL, cycle_ignore_minimized, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, cycle_ignore_minimized) \
	_(DB_CLICK_FULL_SCREEN, "DbClickFullScreen", BOOL, double_click_fullscreen, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, double_click_fullscreen) \
	_(CLOSE_ROOT_MENU_BY_LEFT_OR_RIGHT_MOUSE_CLICK, \
	  "CloseRootMenuByLeftOrRightMouseClick", BOOL, close_rootmenu_left_right_click, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, close_rootmenu_left_right_click) \
	_(COLORMAP_MODE, "ColormapMode", ENUM, colormap_mode, "auto", \
	  colormap_modes, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0, BYTES, colormap_mode) \
	_(AUTO_FOCUS, "AutoFocus", BOOL, auto_focus, "YES", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, auto_focus) \
	_(AUTO_FOCUS_ACROSS_HEADS, "AutoFocusAcrossHeads", BOOL, auto_focus_across_heads, "YES", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, auto_focus_across_heads) \
	_(RAISE_DELAY, "RaiseDelay", INT, raise_delay, "0", \
	  NULL, 0, INT_MAX, WCFG_RUNTIME_RELOAD, 0, BYTES, raise_delay) \
	_(CIRCULATE_RAISE, "CirculateRaise", BOOL, circ_raise, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, circ_raise) \
	_(SUPERFLUOUS, "Superfluous", BOOL, superfluous, "YES", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, superfluous) \
	_(ADVANCE_TO_NEW_WORKSPACE, "AdvanceToNewWorkspace", BOOL, ws_advance, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, ws_advance) \
	_(CYCLE_WORKSPACES, "CycleWorkspaces", BOOL, ws_cycle, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, ws_cycle) \
	_(WORKSPACE_NAME_DISPLAY_POSITION, "WorkspaceNameDisplayPosition", ENUM, \
	  workspace_name_display_position, "center", display_positions, INT_MIN, INT_MAX, \
	  WCFG_RUNTIME_RELOAD, 0, BYTES, workspace_name_display_position) \
	_(WINDOW_PLACEMENT, "WindowPlacement", ENUM, window_placement, "auto", \
	  placements, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0, BYTES, window_placement) \
	_(WORKSPACE_BORDER, "WorkspaceBorder", ENUM, workspace_border_position, "None", \
	  workspace_borders, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0, \
	  BYTES, workspace_border_position) \
	_(WORKSPACE_BORDER_SIZE, "WorkspaceBorderSize", UINT, workspace_border_size, "0", \
	  NULL, 0, INT_MAX, WCFG_RUNTIME_RELOAD, 0, BYTES, workspace_border_size) \
	_(WRAP_MENUS, "WrapMenus", BOOL, wrap_menus, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, wrap_menus) \
	_(SCROLLABLE_MENUS, "ScrollableMenus", BOOL, scrollable_menus, "YES", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, scrollable_menus) \
	_(DOUBLE_CLICK_TIME, "DoubleClickTime", INT, double_click_time, "250", \
	  NULL, 1, INT_MAX, WCFG_RUNTIME_RELOAD, 0, BYTES, dblclick_time) \
	_(SAVE_SESSION_ON_EXIT, "SaveSessionOnExit", BOOL, save_session_on_exit, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, save_session_on_exit) \
	_(MENU_SCROLL_SPEED, "MenuScrollSpeed", ENUM, menu_scroll_speed, "fast", \
	  speeds, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0, BYTES, menu_scroll_speed) \
	_(ICON_SLIDE_SPEED, "IconSlideSpeed", ENUM, icon_slide_speed, "fast", \
	  speeds, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0, BYTES, icon_slide_speed) \
	_(SHADE_SPEED, "ShadeSpeed", ENUM, shade_speed, "fast", \
	  speeds, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0, BYTES, shade_speed) \
	_(BOUNCE_APPICONS_WHEN_URGENT, "BounceAppIconsWhenUrgent", BOOL, \
	  bounce_appicons_when_urgent, "YES", NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, \
	  BYTES, bounce_appicons_when_urgent) \
	_(RAISE_APPICONS_WHEN_BOUNCING, "RaiseAppIconsWhenBouncing", BOOL, \
	  raise_appicons_when_bouncing, "NO", NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, \
	  BYTES, raise_appicons_when_bouncing) \
	_(DO_NOT_MAKE_APPICONS_BOUNCE, "DoNotMakeAppIconsBounce", BOOL, \
	  do_not_make_appicons_bounce, "NO", NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, \
	  BYTES, do_not_make_appicons_bounce) \
	_(CLIP_AUTORAISE_DELAY, "ClipAutoraiseDelay", INT, clip_auto_raise_delay, "600", \
	  NULL, 0, INT_MAX, WCFG_RUNTIME_RELOAD, 0, BYTES, clip_auto_raise_delay) \
	_(CLIP_AUTOLOWER_DELAY, "ClipAutolowerDelay", INT, clip_auto_lower_delay, "1000", \
	  NULL, 0, INT_MAX, WCFG_RUNTIME_RELOAD, 0, BYTES, clip_auto_lower_delay) \
	_(CLIP_AUTOEXPAND_DELAY, "ClipAutoexpandDelay", INT, clip_auto_expand_delay, "600", \
	  NULL, 0, INT_MAX, WCFG_RUNTIME_RELOAD, 0, BYTES, clip_auto_expand_delay) \
	_(CLIP_AUTOCOLLAPSE_DELAY, "ClipAutocollapseDelay", INT, clip_auto_collapse_delay, "1000", \
	  NULL, 0, INT_MAX, WCFG_RUNTIME_RELOAD, 0, BYTES, clip_auto_collapse_delay) \
	_(ALIGN_SUBMENUS, "AlignSubmenus", BOOL, align_menus, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, align_menus) \
	_(VI_KEY_MENUS, "ViKeyMenus", BOOL, vi_key_menus, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, vi_key_menus) \
	_(OPEN_TRANSIENT_ON_OWNER_WORKSPACE, "OpenTransientOnOwnerWorkspace", BOOL, \
	  open_transients_with_parent, "NO", NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, \
	  BYTES, open_transients_with_parent) \
	_(IGNORE_FOCUS_CLICK, "IgnoreFocusClick", BOOL, ignore_focus_click, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, ignore_focus_click) \
	_(USE_SAVE_UNDERS, "UseSaveUnders", BOOL, use_saveunders, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, use_saveunders) \
	_(OPAQUE_MOVE, "OpaqueMove", BOOL, opaque_move, "YES", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, opaque_move) \
	_(OPAQUE_RESIZE, "OpaqueResize", BOOL, opaque_resize, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, opaque_resize) \
	_(OPAQUE_MOVE_RESIZE_KEYBOARD, "OpaqueMoveResizeKeyboard", BOOL, \
	  opaque_move_resize_keyboard, "NO", NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, \
	  BYTES, opaque_move_resize_keyboard) \
	_(DISABLE_ANIMATIONS, "DisableAnimations", BOOL, no_animations, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, no_animations) \
	_(DONT_LINK_WORKSPACES, "DontLinkWorkspaces", BOOL, no_autowrap, "YES", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, no_autowrap) \
	_(WINDOW_SNAPPING, "WindowSnapping", BOOL, window_snapping, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, window_snapping) \
	_(SNAP_EDGE_DETECT, "SnapEdgeDetect", INT, snap_edge_detect, "1", \
	  NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0, BYTES, snap_edge_detect) \
	_(SNAP_CORNER_DETECT, "SnapCornerDetect", INT, snap_corner_detect, "10", \
	  NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0, BYTES, snap_corner_detect) \
	_(SNAP_TO_TOP_MAXIMIZES_FULLSCREEN, "SnapToTopMaximizesFullscreen", BOOL, \
	  snap_to_top_maximizes_fullscreen, "NO", NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, \
	  BYTES, snap_to_top_maximizes_fullscreen) \
	_(DRAG_MAXIMIZED_WINDOW, "DragMaximizedWindow", ENUM, drag_maximized_window, "Move", \
	  drag_maximized_windows, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, 0, \
	  BYTES, drag_maximized_window) \
	_(MOVE_HALF_MAXIMIZED_WINDOWS_BETWEEN_SCREENS, \
	  "MoveHalfMaximizedWindowsBetweenScreens", BOOL, move_half_max_between_heads, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, move_half_max_between_heads) \
	_(ALTERNATIVE_HALF_MAXIMIZED, "AlternativeHalfMaximized", BOOL, alt_half_maximize, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, alt_half_maximize) \
	_(POINTER_WITH_HALF_MAX_WINDOWS, "PointerWithHalfMaxWindows", BOOL, \
	  pointer_with_half_max_windows, "NO", NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, \
	  BYTES, pointer_with_half_max_windows) \
	_(HIGHLIGHT_ACTIVE_APP, "HighlightActiveApp", BOOL, highlight_active_app, "YES", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, highlight_active_app) \
	_(AUTO_ARRANGE_ICONS, "AutoArrangeIcons", BOOL, auto_arrange_icons, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, BYTES, auto_arrange_icons) \
	_(WORKSPACE_SPECIFIC_BACK, "WorkspaceSpecificBack", BACKGROUND_SPEC, \
	  workspace_specific_back, "()", NULL, \
	  WCFG_BACKGROUND_WORKSPACE_SPECIFIC, WCFG_BACKGROUND_WORKSPACE_SPECIFIC, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, workspace_specific_back) \
	_(WORKSPACE_BACK, "WorkspaceBack", BACKGROUND_SPEC, workspace_back, \
	  "(solid, \"rgb:50/50/75\")", NULL, \
	  WCFG_BACKGROUND_WORKSPACE, WCFG_BACKGROUND_WORKSPACE, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, workspace_back) \
	_(SMOOTH_WORKSPACE_BACK, "SmoothWorkspaceBack", BOOL, smooth_workspace_back, "NO", \
	  NULL, 0, 1, WCFG_RUNTIME_RELOAD, 0, NONE, smooth_workspace_back) \
	_(WIDGET_COLOR, "WidgetColor", TEXTURE_SPEC, widget_color, "(solid, gray)", NULL, \
	  WCFG_TEXTURE_WIDGET_COLOR, WCFG_TEXTURE_WIDGET_COLOR, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, widget_color) \
	_(ICON_BACK, "IconBack", TEXTURE_SPEC, icon_back, \
	  "(dgradient, \"rgb:a6/a6/b6\", \"rgb:51/55/61\")", NULL, \
	  WCFG_TEXTURE_ICON_BACK, WCFG_TEXTURE_ICON_BACK, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, icon_back) \
	_(WINDOW_TITLE_FONT, "WindowTitleFont", FONT_SPEC, window_title_font, \
	  "Sans:bold:pixelsize=12", NULL, WCFG_FONT_WINDOW_TITLE, WCFG_FONT_WINDOW_TITLE, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, window_title_font) \
	_(MENU_TITLE_FONT, "MenuTitleFont", FONT_SPEC, menu_title_font, \
	  "Sans:bold:pixelsize=12", NULL, WCFG_FONT_MENU_TITLE, WCFG_FONT_MENU_TITLE, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, menu_title_font) \
	_(MENU_TEXT_FONT, "MenuTextFont", FONT_SPEC, menu_text_font, \
	  "Sans:pixelsize=12", NULL, WCFG_FONT_MENU_TEXT, WCFG_FONT_MENU_TEXT, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, menu_text_font) \
	_(ICON_TITLE_FONT, "IconTitleFont", FONT_SPEC, icon_title_font, \
	  "Sans:pixelsize=9", NULL, WCFG_FONT_ICON_TITLE, WCFG_FONT_ICON_TITLE, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, icon_title_font) \
	_(CLIP_TITLE_FONT, "ClipTitleFont", FONT_SPEC, clip_title_font, \
	  "Sans:bold:pixelsize=10", NULL, WCFG_FONT_CLIP_TITLE, WCFG_FONT_CLIP_TITLE, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, clip_title_font) \
	_(LARGE_DISPLAY_FONT, "LargeDisplayFont", FONT_SPEC, large_display_font, \
	  "Sans:pixelsize=24", NULL, WCFG_FONT_LARGE_DISPLAY, WCFG_FONT_LARGE_DISPLAY, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, large_display_font) \
	_(HIGHLIGHT_COLOR, "HighlightColor", COLOR_SPEC, highlight_color, "white", NULL, \
	  WCFG_COLOR_HIGHLIGHT, WCFG_COLOR_HIGHLIGHT, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, highlight_color) \
	_(HIGHLIGHT_TEXT_COLOR, "HighlightTextColor", COLOR_SPEC, highlight_text_color, \
	  "black", NULL, WCFG_COLOR_HIGHLIGHT_TEXT, WCFG_COLOR_HIGHLIGHT_TEXT, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, highlight_text_color) \
	_(CLIP_TITLE_COLOR, "ClipTitleColor", COLOR_SPEC, clip_title_color, "black", NULL, \
	  WCFG_COLOR_CLIP_TITLE, WCFG_COLOR_CLIP_TITLE, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, clip_title_color) \
	_(CCLIP_TITLE_COLOR, "CClipTitleColor", COLOR_SPEC, cclip_title_color, \
	  "rgb:61/61/61", NULL, WCFG_COLOR_C_CLIP_TITLE, WCFG_COLOR_C_CLIP_TITLE, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, cclip_title_color) \
	_(F_TITLE_COLOR, "FTitleColor", COLOR_SPEC, ftitle_color, "white", NULL, \
	  WCFG_COLOR_F_TITLE, WCFG_COLOR_F_TITLE, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, ftitle_color) \
	_(P_TITLE_COLOR, "PTitleColor", COLOR_SPEC, ptitle_color, "white", NULL, \
	  WCFG_COLOR_P_TITLE, WCFG_COLOR_P_TITLE, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, ptitle_color) \
	_(U_TITLE_COLOR, "UTitleColor", COLOR_SPEC, utitle_color, "black", NULL, \
	  WCFG_COLOR_U_TITLE, WCFG_COLOR_U_TITLE, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, utitle_color) \
	_(F_TITLE_BACK, "FTitleBack", TEXTURE_SPEC, ftitle_back, "(solid, black)", NULL, \
	  WCFG_TEXTURE_F_TITLE_BACK, WCFG_TEXTURE_F_TITLE_BACK, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, ftitle_back) \
	_(P_TITLE_BACK, "PTitleBack", TEXTURE_SPEC, ptitle_back, "(solid, gray40)", NULL, \
	  WCFG_TEXTURE_P_TITLE_BACK, WCFG_TEXTURE_P_TITLE_BACK, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, ptitle_back) \
	_(U_TITLE_BACK, "UTitleBack", TEXTURE_SPEC, utitle_back, \
	  "(solid, \"rgb:aa/aa/aa\")", NULL, \
	  WCFG_TEXTURE_U_TITLE_BACK, WCFG_TEXTURE_U_TITLE_BACK, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, utitle_back) \
	_(RESIZEBAR_BACK, "ResizebarBack", TEXTURE_SPEC, resizebar_back, \
	  "(solid, \"rgb:aa/aa/aa\")", NULL, \
	  WCFG_TEXTURE_RESIZEBAR_BACK, WCFG_TEXTURE_RESIZEBAR_BACK, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, resizebar_back) \
	_(MENU_TITLE_COLOR, "MenuTitleColor", COLOR_SPEC, menu_title_color, "white", NULL, \
	  WCFG_COLOR_MENU_TITLE, WCFG_COLOR_MENU_TITLE, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, menu_title_color) \
	_(MENU_TEXT_COLOR, "MenuTextColor", COLOR_SPEC, menu_text_color, "black", NULL, \
	  WCFG_COLOR_MENU_TEXT, WCFG_COLOR_MENU_TEXT, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, menu_text_color) \
	_(MENU_DISABLED_COLOR, "MenuDisabledColor", COLOR_SPEC, menu_disabled_color, \
	  "gray50", NULL, WCFG_COLOR_MENU_DISABLED, WCFG_COLOR_MENU_DISABLED, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, menu_disabled_color) \
	_(MENU_TITLE_BACK, "MenuTitleBack", TEXTURE_SPEC, menu_title_back, \
	  "(solid, black)", NULL, \
	  WCFG_TEXTURE_MENU_TITLE_BACK, WCFG_TEXTURE_MENU_TITLE_BACK, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, menu_title_back) \
	_(MENU_TEXT_BACK, "MenuTextBack", TEXTURE_SPEC, menu_text_back, \
	  "(solid, \"rgb:aa/aa/aa\")", NULL, \
	  WCFG_TEXTURE_MENU_TEXT_BACK, WCFG_TEXTURE_MENU_TEXT_BACK, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, menu_text_back) \
	_(ICON_TITLE_COLOR, "IconTitleColor", COLOR_SPEC, icon_title_color, "white", NULL, \
	  WCFG_COLOR_ICON_TITLE, WCFG_COLOR_ICON_TITLE, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, icon_title_color) \
	_(ICON_TITLE_BACK, "IconTitleBack", COLOR_SPEC, icon_title_back, "black", NULL, \
	  WCFG_COLOR_ICON_TITLE_BACK, WCFG_COLOR_ICON_TITLE_BACK, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, icon_title_back) \
	_(FRAME_BORDER_WIDTH, "FrameBorderWidth", INT, frame_border_width, "1", \
	  NULL, INT_MIN, INT_MAX, WCFG_RUNTIME_RELOAD, WCFG_REFRESH_FRAME_BORDER, \
	  NONE, frame_border_width) \
	_(FRAME_BORDER_COLOR, "FrameBorderColor", COLOR_SPEC, frame_border_color, \
	  "black", NULL, WCFG_COLOR_FRAME_BORDER, WCFG_COLOR_FRAME_BORDER, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, frame_border_color) \
	_(FRAME_FOCUSED_BORDER_COLOR, "FrameFocusedBorderColor", COLOR_SPEC, \
	  frame_focused_border_color, "black", NULL, \
	  WCFG_COLOR_FRAME_FOCUSED_BORDER, WCFG_COLOR_FRAME_FOCUSED_BORDER, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, frame_focused_border_color) \
	_(FRAME_SELECTED_BORDER_COLOR, "FrameSelectedBorderColor", COLOR_SPEC, \
	  frame_selected_border_color, "white", NULL, \
	  WCFG_COLOR_FRAME_SELECTED_BORDER, WCFG_COLOR_FRAME_SELECTED_BORDER, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, frame_selected_border_color) \
	_(WORKSPACE_MAP_BACK, "WorkspaceMapBack", TEXTURE_SPEC, workspace_map_back, \
	  "(solid, black)", NULL, \
	  WCFG_TEXTURE_WORKSPACE_MAP_BACK, WCFG_TEXTURE_WORKSPACE_MAP_BACK, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, workspace_map_back) \
	_(NORMAL_CURSOR, "NormalCursor", CURSOR_SPEC, normal_cursor, \
	  "(builtin, left_ptr)", NULL, WCFG_CURSOR_NORMAL, WCFG_CURSOR_NORMAL, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, normal_cursor) \
	_(ARROW_CURSOR, "ArrowCursor", CURSOR_SPEC, arrow_cursor, \
	  "(builtin, top_left_arrow)", NULL, WCFG_CURSOR_ARROW, WCFG_CURSOR_ARROW, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, arrow_cursor) \
	_(MOVE_CURSOR, "MoveCursor", CURSOR_SPEC, move_cursor, \
	  "(builtin, fleur)", NULL, WCFG_CURSOR_MOVE, WCFG_CURSOR_MOVE, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, move_cursor) \
	_(RESIZE_CURSOR, "ResizeCursor", CURSOR_SPEC, resize_cursor, \
	  "(builtin, sizing)", NULL, WCFG_CURSOR_RESIZE, WCFG_CURSOR_RESIZE, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, resize_cursor) \
	_(TOP_LEFT_RESIZE_CURSOR, "TopLeftResizeCursor", CURSOR_SPEC, top_left_resize_cursor, \
	  "(builtin, top_left_corner)", NULL, \
	  WCFG_CURSOR_TOP_LEFT_RESIZE, WCFG_CURSOR_TOP_LEFT_RESIZE, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, top_left_resize_cursor) \
	_(TOP_RIGHT_RESIZE_CURSOR, "TopRightResizeCursor", CURSOR_SPEC, top_right_resize_cursor, \
	  "(builtin, top_right_corner)", NULL, \
	  WCFG_CURSOR_TOP_RIGHT_RESIZE, WCFG_CURSOR_TOP_RIGHT_RESIZE, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, top_right_resize_cursor) \
	_(BOTTOM_LEFT_RESIZE_CURSOR, "BottomLeftResizeCursor", CURSOR_SPEC, \
	  bottom_left_resize_cursor, "(builtin, bottom_left_corner)", NULL, \
	  WCFG_CURSOR_BOTTOM_LEFT_RESIZE, WCFG_CURSOR_BOTTOM_LEFT_RESIZE, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, bottom_left_resize_cursor) \
	_(BOTTOM_RIGHT_RESIZE_CURSOR, "BottomRightResizeCursor", CURSOR_SPEC, \
	  bottom_right_resize_cursor, "(builtin, bottom_right_corner)", NULL, \
	  WCFG_CURSOR_BOTTOM_RIGHT_RESIZE, WCFG_CURSOR_BOTTOM_RIGHT_RESIZE, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, bottom_right_resize_cursor) \
	_(VERTICAL_RESIZE_CURSOR, "VerticalResizeCursor", CURSOR_SPEC, vertical_resize_cursor, \
	  "(builtin, sb_v_double_arrow)", NULL, \
	  WCFG_CURSOR_VERTICAL_RESIZE, WCFG_CURSOR_VERTICAL_RESIZE, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, vertical_resize_cursor) \
	_(HORIZONTAL_RESIZE_CURSOR, "HorizontalResizeCursor", CURSOR_SPEC, \
	  horizontal_resize_cursor, "(builtin, sb_h_double_arrow)", NULL, \
	  WCFG_CURSOR_HORIZONTAL_RESIZE, WCFG_CURSOR_HORIZONTAL_RESIZE, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, horizontal_resize_cursor) \
	_(WAIT_CURSOR, "WaitCursor", CURSOR_SPEC, wait_cursor, \
	  "(builtin, watch)", NULL, WCFG_CURSOR_WAIT, WCFG_CURSOR_WAIT, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, wait_cursor) \
	_(QUESTION_CURSOR, "QuestionCursor", CURSOR_SPEC, question_cursor, \
	  "(builtin, question_arrow)", NULL, WCFG_CURSOR_QUESTION, WCFG_CURSOR_QUESTION, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, question_cursor) \
	_(TEXT_CURSOR, "TextCursor", CURSOR_SPEC, text_cursor, \
	  "(builtin, xterm)", NULL, WCFG_CURSOR_TEXT, WCFG_CURSOR_TEXT, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, text_cursor) \
	_(SELECT_CURSOR, "SelectCursor", CURSOR_SPEC, select_cursor, \
	  "(builtin, cross)", NULL, WCFG_CURSOR_SELECT, WCFG_CURSOR_SELECT, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, select_cursor) \
	_(CAPTURE_CURSOR, "CaptureCursor", CURSOR_SPEC, capture_cursor, \
	  "(builtin, crosshair)", NULL, WCFG_CURSOR_CAPTURE, WCFG_CURSOR_CAPTURE, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, capture_cursor) \
	_(HOT_CORNER_ACTIONS, "HotCornerActions", STRING_ARRAY, hot_corner_actions, \
	  "(\"None\", \"None\", \"None\", \"None\")", NULL, 4, 4, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, hot_corner_actions) \
	_(SWITCH_PANEL_IMAGES, "SwitchPanelImages", SWITCH_PANEL_IMAGES, switch_panel_images, \
	  "(swtile.png, swback.png, 30, 40)", NULL, INT_MIN, INT_MAX, \
	  WCFG_RUNTIME_RELOAD, 0, NONE, switch_panel_images) \
	_(MODIFIER_KEY_LABELS, "ModifierKeyLabels", STRING_ARRAY, modifier_key_labels, \
	  "(\"Shift+\", \"Control+\", \"Mod1+\", \"Mod2+\", \"Mod3+\", \"Mod4+\", \"Mod5+\")", \
	  NULL, 7, 7, WCFG_RUNTIME_RELOAD, 0, NONE, modifier_key_labels) \
	_(MODIFIER_KEY_SHORT_LABELS, "ModifierKeyShortLabels", STRING_ARRAY, \
	  modifier_key_short_labels, \
	  "(\"Sh+\", \"^\", \"M1+\", \"M2+\", \"M3+\", \"M4+\", \"M5+\", \"M+\", \"A+\")", \
	  NULL, 9, 9, WCFG_RUNTIME_RELOAD, 0, NONE, modifier_key_short_labels)

#define WCFG_SCHEMA_KEYBINDINGS(_) \
	_(ROOT_MENU_KEY, "RootMenuKey", "F12", WKBD_ROOTMENU) \
	_(WINDOW_LIST_KEY, "WindowListKey", "F11", WKBD_WINDOWLIST) \
	_(WINDOW_MENU_KEY, "WindowMenuKey", "Control+Escape", WKBD_WINDOWMENU) \
	_(KEYCHAIN_CANCEL_KEY, "KeychainCancelKey", "None", WKBD_KEYCHAIN_CANCEL) \
	_(DOCK_RAISE_LOWER_KEY, "DockRaiseLowerKey", "None", WKBD_DOCKRAISELOWER) \
	_(CLIP_RAISE_LOWER_KEY, "ClipRaiseLowerKey", "None", WKBD_CLIPRAISELOWER) \
	_(MINIATURIZE_KEY, "MiniaturizeKey", "Mod1+M", WKBD_MINIATURIZE) \
	_(MINIMIZE_ALL_KEY, "MinimizeAllKey", "None", WKBD_MINIMIZEALL) \
	_(HIDE_KEY, "HideKey", "Mod1+H", WKBD_HIDE) \
	_(HIDE_OTHERS_KEY, "HideOthersKey", "None", WKBD_HIDE_OTHERS) \
	_(MOVE_RESIZE_KEY, "MoveResizeKey", "None", WKBD_MOVERESIZE) \
	_(CLOSE_KEY, "CloseKey", "None", WKBD_CLOSE) \
	_(MAXIMIZE_KEY, "MaximizeKey", "None", WKBD_MAXIMIZE) \
	_(V_MAXIMIZE_KEY, "VMaximizeKey", "None", WKBD_VMAXIMIZE) \
	_(H_MAXIMIZE_KEY, "HMaximizeKey", "None", WKBD_HMAXIMIZE) \
	_(LH_MAXIMIZE_KEY, "LHMaximizeKey", "None", WKBD_LHMAXIMIZE) \
	_(RH_MAXIMIZE_KEY, "RHMaximizeKey", "None", WKBD_RHMAXIMIZE) \
	_(TH_MAXIMIZE_KEY, "THMaximizeKey", "None", WKBD_THMAXIMIZE) \
	_(BH_MAXIMIZE_KEY, "BHMaximizeKey", "None", WKBD_BHMAXIMIZE) \
	_(TLC_MAXIMIZE_KEY, "TLCMaximizeKey", "None", WKBD_LTCMAXIMIZE) \
	_(TRC_MAXIMIZE_KEY, "TRCMaximizeKey", "None", WKBD_RTCMAXIMIZE) \
	_(BLC_MAXIMIZE_KEY, "BLCMaximizeKey", "None", WKBD_LBCMAXIMIZE) \
	_(BRC_MAXIMIZE_KEY, "BRCMaximizeKey", "None", WKBD_RBCMAXIMIZE) \
	_(MAXIMUS_KEY, "MaximusKey", "None", WKBD_MAXIMUS) \
	_(CENTER_KEY, "CenterKey", "None", WKBD_CENTRAL) \
	_(KEEP_ON_TOP_KEY, "KeepOnTopKey", "None", WKBD_KEEP_ON_TOP) \
	_(KEEP_AT_BOTTOM_KEY, "KeepAtBottomKey", "None", WKBD_KEEP_AT_BOTTOM) \
	_(OMNIPRESENT_KEY, "OmnipresentKey", "None", WKBD_OMNIPRESENT) \
	_(RAISE_KEY, "RaiseKey", "Mod1+Up", WKBD_RAISE) \
	_(LOWER_KEY, "LowerKey", "Mod1+Down", WKBD_LOWER) \
	_(RAISE_LOWER_KEY, "RaiseLowerKey", "None", WKBD_RAISELOWER) \
	_(SHADE_KEY, "ShadeKey", "None", WKBD_SHADE) \
	_(SELECT_KEY, "SelectKey", "None", WKBD_SELECT) \
	_(WORKSPACE_MAP_KEY, "WorkspaceMapKey", "None", WKBD_WORKSPACEMAP) \
	_(FOCUS_NEXT_KEY, "FocusNextKey", "Mod1+Tab", WKBD_FOCUSNEXT) \
	_(FOCUS_PREV_KEY, "FocusPrevKey", "Mod1+Shift+Tab", WKBD_FOCUSPREV) \
	_(FOCUS_WINDOW_LEFT_KEY, "FocusWindowLeftKey", "None", WKBD_FOCUSLEFT) \
	_(FOCUS_WINDOW_RIGHT_KEY, "FocusWindowRightKey", "None", WKBD_FOCUSRIGHT) \
	_(FOCUS_WINDOW_UP_KEY, "FocusWindowUpKey", "None", WKBD_FOCUSUP) \
	_(FOCUS_WINDOW_DOWN_KEY, "FocusWindowDownKey", "None", WKBD_FOCUSDOWN) \
	_(GROUP_NEXT_KEY, "GroupNextKey", "None", WKBD_GROUPNEXT) \
	_(GROUP_PREV_KEY, "GroupPrevKey", "None", WKBD_GROUPPREV) \
	_(NEXT_WORKSPACE_KEY, "NextWorkspaceKey", "Mod1+Control+Right", WKBD_NEXTWORKSPACE) \
	_(PREV_WORKSPACE_KEY, "PrevWorkspaceKey", "Mod1+Control+Left", WKBD_PREVWORKSPACE) \
	_(LAST_WORKSPACE_KEY, "LastWorkspaceKey", "None", WKBD_LASTWORKSPACE) \
	_(NEXT_WORKSPACE_LAYER_KEY, "NextWorkspaceLayerKey", "None", WKBD_NEXTWSLAYER) \
	_(PREV_WORKSPACE_LAYER_KEY, "PrevWorkspaceLayerKey", "None", WKBD_PREVWSLAYER) \
	_(WORKSPACE1_KEY, "Workspace1Key", "Mod1+1", WKBD_WORKSPACE1) \
	_(WORKSPACE2_KEY, "Workspace2Key", "Mod1+2", WKBD_WORKSPACE2) \
	_(WORKSPACE3_KEY, "Workspace3Key", "Mod1+3", WKBD_WORKSPACE3) \
	_(WORKSPACE4_KEY, "Workspace4Key", "Mod1+4", WKBD_WORKSPACE4) \
	_(WORKSPACE5_KEY, "Workspace5Key", "Mod1+5", WKBD_WORKSPACE5) \
	_(WORKSPACE6_KEY, "Workspace6Key", "Mod1+6", WKBD_WORKSPACE6) \
	_(WORKSPACE7_KEY, "Workspace7Key", "Mod1+7", WKBD_WORKSPACE7) \
	_(WORKSPACE8_KEY, "Workspace8Key", "Mod1+8", WKBD_WORKSPACE8) \
	_(WORKSPACE9_KEY, "Workspace9Key", "Mod1+9", WKBD_WORKSPACE9) \
	_(WORKSPACE10_KEY, "Workspace10Key", "Mod1+0", WKBD_WORKSPACE10) \
	_(MOVE_TO_WORKSPACE1_KEY, "MoveToWorkspace1Key", "None", WKBD_MOVE_WORKSPACE1) \
	_(MOVE_TO_WORKSPACE2_KEY, "MoveToWorkspace2Key", "None", WKBD_MOVE_WORKSPACE2) \
	_(MOVE_TO_WORKSPACE3_KEY, "MoveToWorkspace3Key", "None", WKBD_MOVE_WORKSPACE3) \
	_(MOVE_TO_WORKSPACE4_KEY, "MoveToWorkspace4Key", "None", WKBD_MOVE_WORKSPACE4) \
	_(MOVE_TO_WORKSPACE5_KEY, "MoveToWorkspace5Key", "None", WKBD_MOVE_WORKSPACE5) \
	_(MOVE_TO_WORKSPACE6_KEY, "MoveToWorkspace6Key", "None", WKBD_MOVE_WORKSPACE6) \
	_(MOVE_TO_WORKSPACE7_KEY, "MoveToWorkspace7Key", "None", WKBD_MOVE_WORKSPACE7) \
	_(MOVE_TO_WORKSPACE8_KEY, "MoveToWorkspace8Key", "None", WKBD_MOVE_WORKSPACE8) \
	_(MOVE_TO_WORKSPACE9_KEY, "MoveToWorkspace9Key", "None", WKBD_MOVE_WORKSPACE9) \
	_(MOVE_TO_WORKSPACE10_KEY, "MoveToWorkspace10Key", "None", WKBD_MOVE_WORKSPACE10) \
	_(MOVE_TO_NEXT_WORKSPACE_KEY, "MoveToNextWorkspaceKey", "None", WKBD_MOVE_NEXTWORKSPACE) \
	_(MOVE_TO_PREV_WORKSPACE_KEY, "MoveToPrevWorkspaceKey", "None", WKBD_MOVE_PREVWORKSPACE) \
	_(MOVE_TO_LAST_WORKSPACE_KEY, "MoveToLastWorkspaceKey", "None", WKBD_MOVE_LASTWORKSPACE) \
	_(MOVE_TO_NEXT_WORKSPACE_LAYER_KEY, "MoveToNextWorkspaceLayerKey", "None", WKBD_MOVE_NEXTWSLAYER) \
	_(MOVE_TO_PREV_WORKSPACE_LAYER_KEY, "MoveToPrevWorkspaceLayerKey", "None", WKBD_MOVE_PREVWSLAYER) \
	_(WINDOW_SHORTCUT1_KEY, "WindowShortcut1Key", "None", WKBD_WINDOW1) \
	_(WINDOW_SHORTCUT2_KEY, "WindowShortcut2Key", "None", WKBD_WINDOW2) \
	_(WINDOW_SHORTCUT3_KEY, "WindowShortcut3Key", "None", WKBD_WINDOW3) \
	_(WINDOW_SHORTCUT4_KEY, "WindowShortcut4Key", "None", WKBD_WINDOW4) \
	_(WINDOW_SHORTCUT5_KEY, "WindowShortcut5Key", "None", WKBD_WINDOW5) \
	_(WINDOW_SHORTCUT6_KEY, "WindowShortcut6Key", "None", WKBD_WINDOW6) \
	_(WINDOW_SHORTCUT7_KEY, "WindowShortcut7Key", "None", WKBD_WINDOW7) \
	_(WINDOW_SHORTCUT8_KEY, "WindowShortcut8Key", "None", WKBD_WINDOW8) \
	_(WINDOW_SHORTCUT9_KEY, "WindowShortcut9Key", "None", WKBD_WINDOW9) \
	_(WINDOW_SHORTCUT10_KEY, "WindowShortcut10Key", "None", WKBD_WINDOW10) \
	_(MOVE_TO12TO6_HEAD, "MoveTo12to6Head", "None", WKBD_MOVE_12_TO_6_HEAD) \
	_(MOVE_TO6TO12_HEAD, "MoveTo6to12Head", "None", WKBD_MOVE_6_TO_12_HEAD) \
	_(WINDOW_RELAUNCH_KEY, "WindowRelaunchKey", "None", WKBD_RELAUNCH) \
	_(SCREEN_SWITCH_KEY, "ScreenSwitchKey", "None", WKBD_SWITCH_SCREEN) \
	_(RUN_KEY, "RunKey", "None", WKBD_RUN) \
	_(EXIT_KEY, "ExitKey", "None", WKBD_EXIT) \
	_(SCREEN_CAPTURE_KEY, "ScreenCaptureKey", "None", WKBD_PRINTS) \
	_(WINDOW_CAPTURE_KEY, "WindowCaptureKey", "None", WKBD_PRINTW) \
	_(PARTIAL_CAPTURE_KEY, "PartialCaptureKey", "None", WKBD_PRINTP) \
	_(MARK_SET_KEY, "MarkSetKey", "None", WKBD_MARK_SET) \
	_(MARK_UNSET_KEY, "MarkUnsetKey", "None", WKBD_MARK_UNSET) \
	_(MARK_BRING_KEY, "MarkBringKey", "None", WKBD_MARK_BRING) \
	_(MARK_JUMP_KEY, "MarkJumpKey", "None", WKBD_MARK_JUMP) \
	_(MARK_SWAP_KEY, "MarkSwapKey", "None", WKBD_MARK_SWAP) \
	WCFG_IF_XKB_LOCK( \
	_(TOGGLE_KBD_MODE_KEY, "ToggleKbdModeKey", "None", WKBD_TOGGLE) \
	)

#endif
