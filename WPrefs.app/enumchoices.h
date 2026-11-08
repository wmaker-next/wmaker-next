/* enumchoices.h - WPrefs enum option tables
 *
 *  WPrefs - Window Maker Preferences Program
 *
 *  Copyright (c) 1998-2003 Alfredo K. Kojima
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
 * Each panel that offers a popup or radio group for an enum-typed
 * WindowMaker defaults key expands one of these macros into its local
 * static table, and src/Tests/check_wprefs_schema.c expands every macro
 * into a flat (key, db_value) manifest checked against
 * wConfigSchemaEnumTable() in both directions, so adding a value to the
 * schema without adding it here fails make check.
 *
 * A few panels key their widgets on schema enum values or hardcode the
 * db strings instead of expanding a table. Their macros exist only so
 * check_wprefs_schema pins the value set, and the owning panel is named
 * in the comment.
 *
 * Row shapes:
 *   R(db_value, label)           plain 2-column tables
 *   R(idx, db_value, aux)        designated-init tables, aux is label or
 *                                file_name depending on the panel
 */

#ifndef WPREFS_ENUMCHOICES_H_
#define WPREFS_ENUMCHOICES_H_

/* Icons.c, key "IconificationStyle" */
#define WPREFS_TABLE_icon_animation(R) \
	R("zoom",  N_("Shrinking/Zooming")) \
	R("twist", N_("Spinning/Twisting")) \
	R("flip",  N_("3D-flipping")) \
	R("none",  N_("None"))

/* Appearance.c, key "MenuStyle", aux is the preview pixmap file name */
#define WPREFS_TABLE_menu_style(R) \
	R(MSTYLE_NORMAL, "normal",        "msty1") \
	R(MSTYLE_SINGLE, "singletexture", "msty2") \
	R(MSTYLE_FLAT,   "flat",          "msty3")

/* Appearance.c, key "TitleJustify", idx is WMAlignment */
#define WPREFS_TABLE_wintitle_align(R) \
	R(WALeft,   "left",   N_("Left")) \
	R(WACenter, "center", N_("Center")) \
	R(WARight,  "right",  N_("Right"))

/* MouseSettings.c, keys "Mouse{Left,Middle,Right,Backward,Forward}ButtonAction" */
#ifdef VIRTUAL_DESKTOP
#define WPREFS_TABLE_button_actions(R) \
	R("None",                 N_("None")) \
	R("OpenApplicationsMenu", N_("Applications Menu")) \
	R("OpenWindowListMenu",   N_("Window List Menu")) \
	R("SelectWindows",        N_("Select Windows")) \
	R("MoveToPrevWorkspace",  N_("Previous Workspace")) \
	R("MoveToNextWorkspace",  N_("Next Workspace")) \
	R("MoveToPrevWindow",     N_("Previous Window")) \
	R("MoveToNextWindow",     N_("Next Window")) \
	R("VirtualDesktopPan",    N_("Pan Viewport"))
#else
#define WPREFS_TABLE_button_actions(R) \
	R("None",                 N_("None")) \
	R("OpenApplicationsMenu", N_("Applications Menu")) \
	R("OpenWindowListMenu",   N_("Window List Menu")) \
	R("SelectWindows",        N_("Select Windows")) \
	R("MoveToPrevWorkspace",  N_("Previous Workspace")) \
	R("MoveToNextWorkspace",  N_("Next Workspace")) \
	R("MoveToPrevWindow",     N_("Previous Window")) \
	R("MoveToNextWindow",     N_("Next Window"))
#endif

/* MouseSettings.c, keys "MouseWheelAction" / "MouseWheelTiltAction" */
#define WPREFS_TABLE_wheel_actions(R) \
	R("None",             N_("None")) \
	R("SwitchWorkspaces", N_("Switch Workspaces")) \
	R("SwitchWindows",    N_("Switch Windows"))

/* Workspace.c, key "DefaultWorkspaceLayout" */
#define WPREFS_TABLE_workspace_layouts(R) \
	R("float",   N_("Float")) \
	R("tile",    N_("Tile")) \
	R("tileh",   N_("Tile wide")) \
	R("grid",    N_("Grid")) \
	R("monocle", N_("Monocle"))

/* WindowHandling.c, key "WindowPlacement" */
#define WPREFS_TABLE_window_placements(R) \
	R("auto",    N_("Automatic")) \
	R("random",  N_("Random")) \
	R("manual",  N_("Manual")) \
	R("cascade", N_("Cascade")) \
	R("smart",   N_("Smart")) \
	R("center",  N_("Center"))

/* Expert.c, key "TabAttachArea". Surfaced as an OPTION_WMAKER_ALT
 * checkbox that reads the value pair from the schema directly, this
 * table is here only so check_wprefs_schema asserts the enum stays
 * two-valued. */
#define WPREFS_TABLE_tab_attach_area(R) \
	R("Titlebar", N_("Titlebar")) \
	R("Window",   N_("Window"))

/* Expert.c, key "SwitcherOrder". Surfaced as an OPTION_WMAKER_ALT
 * checkbox, this table only pins the value pair for check_wprefs_schema. */
#define WPREFS_TABLE_switcher_order(R) \
	R("Focus", N_("Focus")) \
	R("Age",   N_("Age"))

/* WindowHandling.c, key "DragMaximizedWindow" */
#define WPREFS_TABLE_drag_maximized_window(R) \
	R("Move",            N_("...changes its position (normal behavior)")) \
	R("RestoreGeometry", N_("...restores its unmaximized geometry")) \
	R("Unmaximize",      N_("...considers the window now unmaximized")) \
	R("NoMove",          N_("...does not move the window"))

/* Preferences.c, key "ResizeDisplay" */
#define WPREFS_TABLE_resize_display(R) \
	R("corner",   N_("Corner of screen")) \
	R("center",   N_("Center of screen")) \
	R("floating", N_("Center of resized window")) \
	R("none",     N_("Disabled"))

/* Preferences.c, key "MoveDisplay" */
#define WPREFS_TABLE_move_display(R) \
	R("corner",   N_("Corner of screen")) \
	R("center",   N_("Center of screen")) \
	R("floating", N_("Center of resized window")) \
	R("none",     N_("Disabled"))

/* Focus.c, key "FocusMode". The two radios key on the WKF_* numeric
 * values, no string table to route. Rows pin the non-alias value set,
 * the label column is not rendered. */
#define WPREFS_TABLE_focus_mode(R) \
	R("Manual", "click to focus") \
	R("Sloppy", "focus follows mouse")

/* Focus.c, key "ColormapMode". Same shape as FocusMode, WCM_* keyed. */
#define WPREFS_TABLE_colormap_mode(R) \
	R("Manual", "manually install colormaps") \
	R("Auto", "automatically install colormaps")

/* Configurations.c, key "NewStyle". The panel writes these strings from
 * its four titlebar-style radios. */
#define WPREFS_TABLE_titlebar_mode(R) \
	R("new", "new style") \
	R("old", "old style") \
	R("next", "NeXT style") \
	R("flat", "flat style")

/* Icons.c, key "IconPosition". Expanded by Icons.c for the 8-way
 * position grid. Rows are in grid order (the createPanel switch relies
 * on it), not schema order. */
#define WPREFS_TABLE_icon_position(R) \
	R("tlv", "top left, vertical") \
	R("tlh", "top left, horizontal") \
	R("trv", "top right, vertical") \
	R("trh", "top right, horizontal") \
	R("blv", "bottom left, vertical") \
	R("blh", "bottom left, horizontal") \
	R("brv", "bottom right, vertical") \
	R("brh", "bottom right, horizontal")

/* Preferences.c, key "WorkspaceBorder". Written as a WB_* bitmask from
 * two checkboxes, all four schema values are reachable. */
#define WPREFS_TABLE_workspace_border(R) \
	R("None", "no border") \
	R("LeftRight", "left and right") \
	R("TopBottom", "top and bottom") \
	R("AllDirections", "all edges")

/* Workspace.c, key "WorkspaceNameDisplayPosition" (KeychainDisplayPosition
 * has no UI). The popup's selected index is written as the enum value,
 * so rows must stay in value order. Pinned by check_wprefs_schema. */
#define WPREFS_TABLE_display_position(R) \
	R("none",        N_("Disable")) \
	R("center",      N_("Center")) \
	R("top",         N_("Top")) \
	R("bottom",      N_("Bottom")) \
	R("left",        N_("Left")) \
	R("right",       N_("Right")) \
	R("topleft",     N_("Top/Left")) \
	R("topright",    N_("Top/Right")) \
	R("bottomleft",  N_("Bottom/Left")) \
	R("bottomright", N_("Bottom/Right"))

/* Configurations.c (IconSlideSpeed, ShadeSpeed) and MenuPreferences.c
 * (MenuScrollSpeed). The five-button radio index maps to SPEED_* through
 * SetSpeedForKey(), so rows must stay in fast-to-slow value order. */
#define WPREFS_TABLE_speed(R) \
	R("UltraFast", "ultra fast") \
	R("Fast", "fast") \
	R("Medium", "medium") \
	R("Slow", "slow") \
	R("UltraSlow", "ultra slow")

/* Docks.c, key "DockEdge". Rows are in schema order, the values are
 * stored as the WDockEdge enumerators. */
#define WPREFS_TABLE_dock_edge(R) \
	R(WDOCK_EDGE_LEFT,   "Left",   N_("Left")) \
	R(WDOCK_EDGE_RIGHT,  "Right",  N_("Right")) \
	R(WDOCK_EDGE_TOP,    "Top",    N_("Top")) \
	R(WDOCK_EDGE_BOTTOM, "Bottom", N_("Bottom"))

#endif /* WPREFS_ENUMCHOICES_H_ */
