/*
 *  keybind_descriptions.h
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
 * Shared key-binding metadata, X-macro list.
 *
 * Expanded by the window manager (src/keybind.c) and WPrefs
 * (WPrefs.app/KeyboardShortcuts.c) so the two cannot drift. Each row is
 * (config key, category, description). The strings are wrapped in N_()
 * and translated at display time by the including program's gettext
 * domain, so this header must stay listed in both po/Makefile.am files
 * and the description text must not be reworded without updating the
 * translations.
 *
 * A key added to WCFG_SCHEMA_KEYBINDINGS but not here still lists with
 * the short action name as its text.
 */

#ifndef WMCONFIG_KEYBIND_DESCRIPTIONS_H_
#define WMCONFIG_KEYBIND_DESCRIPTIONS_H_

#ifdef VIRTUAL_DESKTOP
#define WCFG_KEYBIND_VDESK(_) \
	_("VirtualEdgeLeftKey",  N_("Virtual Desktop"), N_("Move VirtualDesktop to next left edge")) \
	_("VirtualEdgeRightKey", N_("Virtual Desktop"), N_("Move VirtualDesktop to next right edge")) \
	_("VirtualEdgeUpKey",    N_("Virtual Desktop"), N_("Move VirtualDesktop to next top edge")) \
	_("VirtualEdgeDownKey",  N_("Virtual Desktop"), N_("Move VirtualDesktop to next bottom edge")) \
	_("VirtualDesktopHomeKey",   N_("Virtual Desktop"), N_("Move VirtualDesktop to its origin")) \
	_("VirtualDesktopCenterKey", N_("Virtual Desktop"), N_("Center the focused window in the VirtualDesktop")) \
	_("VirtualDesktopPinKey",    N_("Virtual Desktop"), N_("Pin the focused window to the VirtualDesktop viewport"))
#else
#define WCFG_KEYBIND_VDESK(_)
#endif

#ifdef KEEP_XKB_LOCK_STATUS
#define WCFG_KEYBIND_LOCK(_) \
	_("ToggleKbdModeKey", N_("Session & Misc"), N_("Toggle keyboard language")) \
	_("AccentPickerKey", N_("Session & Misc"), N_("Pick accented and special characters"))
#else
#define WCFG_KEYBIND_LOCK(_)
#endif

#define WCFG_KEYBIND_DESCRIPTIONS(_) \
	_("RootMenuKey",    N_("Menus"), N_("Open applications menu")) \
	_("WindowListKey",  N_("Menus"), N_("Open window list menu")) \
	_("WindowMenuKey",  N_("Menus"), N_("Open window commands menu")) \
	\
	_("DockRaiseLowerKey", N_("Dock & Screen"), N_("Raise/Lower Dock")) \
	_("ClipRaiseLowerKey", N_("Dock & Screen"), N_("Raise/Lower Clip")) \
	_("ScreenSwitchKey",   N_("Dock & Screen"), N_("Switch to next screen/monitor")) \
	_("ScreenCaptureKey",  N_("Dock & Screen"), N_("Capture the entire screen")) \
	_("WindowCaptureKey",  N_("Dock & Screen"), N_("Capture a window")) \
	_("PartialCaptureKey", N_("Dock & Screen"), N_("Capture a portion of the screen")) \
	_("FindMouseKey",      N_("Dock & Screen"), N_("Find the mouse pointer")) \
	\
	_("MiniaturizeKey",       N_("Windows"), N_("Miniaturize active window")) \
	_("MiniaturizeAllKey",    N_("Windows"), N_("Miniaturize all windows")) \
	_("MiniaturizeOthersKey", N_("Windows"), N_("Miniaturize other windows")) \
	_("HideKey",              N_("Windows"), N_("Hide active application")) \
	_("HideOthersKey",        N_("Windows"), N_("Hide other applications")) \
	_("ShowDesktopKey",       N_("Windows"), N_("Toggle show desktop (minimize/restore all)")) \
	_("ShowAllKey",           N_("Windows"), N_("Show all windows")) \
	_("ArrangeIconsKey",      N_("Windows"), N_("Arrange icons")) \
	_("MoveResizeKey",        N_("Windows"), N_("Move/Resize active window")) \
	_("CloseKey",             N_("Windows"), N_("Close active window")) \
	_("MaximizeKey",          N_("Windows"), N_("Maximize active window")) \
	_("VMaximizeKey",         N_("Windows"), N_("Maximize active window vertically")) \
	_("HMaximizeKey",         N_("Windows"), N_("Maximize active window horizontally")) \
	_("LHMaximizeKey",        N_("Windows"), N_("Maximize active window left half")) \
	_("RHMaximizeKey",        N_("Windows"), N_("Maximize active window right half")) \
	_("THMaximizeKey",        N_("Windows"), N_("Maximize active window top half")) \
	_("BHMaximizeKey",        N_("Windows"), N_("Maximize active window bottom half")) \
	_("TLCMaximizeKey",       N_("Windows"), N_("Maximize active window top left corner")) \
	_("TRCMaximizeKey",       N_("Windows"), N_("Maximize active window top right corner")) \
	_("BLCMaximizeKey",       N_("Windows"), N_("Maximize active window bottom left corner")) \
	_("BRCMaximizeKey",       N_("Windows"), N_("Maximize active window bottom right corner")) \
	_("MaximusKey",           N_("Windows"), N_("Tile active window")) \
	_("MoveToEdgeLeftKey",           N_("Windows"), N_("Move active window to left edge")) \
	_("MoveToEdgeRightKey",          N_("Windows"), N_("Move active window to right edge")) \
	_("MoveToEdgeUpKey",             N_("Windows"), N_("Move active window to top edge")) \
	_("MoveToEdgeDownKey",           N_("Windows"), N_("Move active window to bottom edge")) \
	_("MoveToEdgeLeftNoSnapKey",     N_("Windows"), N_("Move active window to left screen edge")) \
	_("MoveToEdgeRightNoSnapKey",    N_("Windows"), N_("Move active window to right screen edge")) \
	_("MoveToEdgeUpNoSnapKey",       N_("Windows"), N_("Move active window to top screen edge")) \
	_("MoveToEdgeDownNoSnapKey",     N_("Windows"), N_("Move active window to bottom screen edge")) \
	_("GrowToEdgeLeftKey",           N_("Windows"), N_("Grow active window to left edge")) \
	_("GrowToEdgeRightKey",          N_("Windows"), N_("Grow active window to right edge")) \
	_("GrowToEdgeUpKey",             N_("Windows"), N_("Grow active window to top edge")) \
	_("GrowToEdgeDownKey",           N_("Windows"), N_("Grow active window to bottom edge")) \
	_("ShrinkToEdgeLeftKey",         N_("Windows"), N_("Shrink active window from left edge")) \
	_("ShrinkToEdgeRightKey",        N_("Windows"), N_("Shrink active window from right edge")) \
	_("ShrinkToEdgeUpKey",           N_("Windows"), N_("Shrink active window from top edge")) \
	_("ShrinkToEdgeDownKey",         N_("Windows"), N_("Shrink active window from bottom edge")) \
	_("ToggleFullscreenKey",         N_("Windows"), N_("Toggle fullscreen for active window")) \
	_("CenterKey",                   N_("Windows"), N_("Center active window")) \
	_("KeepOnTopKey",                N_("Windows"), N_("Toggle window on top status")) \
	_("KeepAtBottomKey",             N_("Windows"), N_("Toggle window at bottom status")) \
	_("OmnipresentKey",              N_("Windows"), N_("Toggle window omnipresent status")) \
	_("RaiseKey",                    N_("Windows"), N_("Raise active window")) \
	_("LowerKey",                    N_("Windows"), N_("Lower active window")) \
	_("RaiseLowerKey",               N_("Windows"), N_("Raise/Lower window under mouse pointer")) \
	_("ShadeKey",                    N_("Windows"), N_("Shade active window")) \
	_("SelectKey",                   N_("Windows"), N_("Select active window")) \
	_("WindowRelaunchKey",           N_("Windows"), N_("Launch new instance of application")) \
	_("MoveTo12to6Head",             N_("Windows"), N_("Move to right/bottom/left/top head")) \
	_("MoveTo6to12Head",             N_("Windows"), N_("Move to left/top/right/bottom head")) \
	\
	_("WindowShortcut1Key",  N_("Window Shortcuts"), N_("Shortcut for window 1")) \
	_("WindowShortcut2Key",  N_("Window Shortcuts"), N_("Shortcut for window 2")) \
	_("WindowShortcut3Key",  N_("Window Shortcuts"), N_("Shortcut for window 3")) \
	_("WindowShortcut4Key",  N_("Window Shortcuts"), N_("Shortcut for window 4")) \
	_("WindowShortcut5Key",  N_("Window Shortcuts"), N_("Shortcut for window 5")) \
	_("WindowShortcut6Key",  N_("Window Shortcuts"), N_("Shortcut for window 6")) \
	_("WindowShortcut7Key",  N_("Window Shortcuts"), N_("Shortcut for window 7")) \
	_("WindowShortcut8Key",  N_("Window Shortcuts"), N_("Shortcut for window 8")) \
	_("WindowShortcut9Key",  N_("Window Shortcuts"), N_("Shortcut for window 9")) \
	_("WindowShortcut10Key", N_("Window Shortcuts"), N_("Shortcut for window 10")) \
	\
	_("FocusNextKey",        N_("Focus & Groups"), N_("Focus next window")) \
	_("FocusPrevKey",        N_("Focus & Groups"), N_("Focus previous window")) \
	_("FocusWindowLeftKey",  N_("Focus & Groups"), N_("Focus the window to the left")) \
	_("FocusWindowRightKey", N_("Focus & Groups"), N_("Focus the window to the right")) \
	_("FocusWindowUpKey",    N_("Focus & Groups"), N_("Focus the window above")) \
	_("FocusWindowDownKey",  N_("Focus & Groups"), N_("Focus the window below")) \
	_("GroupWindowNextKey",  N_("Focus & Groups"), N_("Focus next window in group")) \
	_("GroupWindowPrevKey",  N_("Focus & Groups"), N_("Focus previous window in group")) \
	_("GroupNextKey",        N_("Focus & Groups"), N_("Switch to next application group")) \
	_("GroupPrevKey",        N_("Focus & Groups"), N_("Switch to previous application group")) \
	_("GroupTabNextKey",     N_("Focus & Groups"), N_("Switch to next tab in group")) \
	_("GroupTabPrevKey",     N_("Focus & Groups"), N_("Switch to previous tab in group")) \
	_("GroupTabAttachKey",   N_("Focus & Groups"), N_("Attach window as tab")) \
	_("GroupTabDetachKey",   N_("Focus & Groups"), N_("Detach tab from group")) \
	\
	_("WorkspaceMapKey",             N_("Workspaces"), N_("Open workspace pager")) \
	_("NextWorkspaceKey",            N_("Workspaces"), N_("Switch to next workspace")) \
	_("PrevWorkspaceKey",            N_("Workspaces"), N_("Switch to previous workspace")) \
	_("LastWorkspaceKey",            N_("Workspaces"), N_("Switch to last used workspace")) \
	_("NextWorkspaceLayerKey",       N_("Workspaces"), N_("Switch to next ten workspaces")) \
	_("PrevWorkspaceLayerKey",       N_("Workspaces"), N_("Switch to previous ten workspaces")) \
	_("LeftWorkspaceKey",            N_("Workspaces"), N_("Switch to workspace on the left")) \
	_("RightWorkspaceKey",           N_("Workspaces"), N_("Switch to workspace on the right")) \
	_("UpWorkspaceKey",              N_("Workspaces"), N_("Switch to workspace above")) \
	_("DownWorkspaceKey",            N_("Workspaces"), N_("Switch to workspace below")) \
	_("Workspace1Key",               N_("Workspaces"), N_("Switch to workspace 1")) \
	_("Workspace2Key",               N_("Workspaces"), N_("Switch to workspace 2")) \
	_("Workspace3Key",               N_("Workspaces"), N_("Switch to workspace 3")) \
	_("Workspace4Key",               N_("Workspaces"), N_("Switch to workspace 4")) \
	_("Workspace5Key",               N_("Workspaces"), N_("Switch to workspace 5")) \
	_("Workspace6Key",               N_("Workspaces"), N_("Switch to workspace 6")) \
	_("Workspace7Key",               N_("Workspaces"), N_("Switch to workspace 7")) \
	_("Workspace8Key",               N_("Workspaces"), N_("Switch to workspace 8")) \
	_("Workspace9Key",               N_("Workspaces"), N_("Switch to workspace 9")) \
	_("Workspace10Key",              N_("Workspaces"), N_("Switch to workspace 10")) \
	_("MoveToNextWorkspaceKey",      N_("Workspaces"), N_("Move window to next workspace")) \
	_("MoveToPrevWorkspaceKey",      N_("Workspaces"), N_("Move window to previous workspace")) \
	_("MoveToLastWorkspaceKey",      N_("Workspaces"), N_("Move window to last used workspace")) \
	_("MoveToNextWorkspaceLayerKey", N_("Workspaces"), N_("Move window to next ten workspaces")) \
	_("MoveToPrevWorkspaceLayerKey", N_("Workspaces"), N_("Move window to previous ten workspaces")) \
	_("MoveToLeftWorkspaceKey",      N_("Workspaces"), N_("Move window to workspace on the left")) \
	_("MoveToRightWorkspaceKey",     N_("Workspaces"), N_("Move window to workspace on the right")) \
	_("MoveToUpWorkspaceKey",        N_("Workspaces"), N_("Move window to workspace above")) \
	_("MoveToDownWorkspaceKey",      N_("Workspaces"), N_("Move window to workspace below")) \
	_("MoveToWorkspace1Key",         N_("Workspaces"), N_("Move window to workspace 1")) \
	_("MoveToWorkspace2Key",         N_("Workspaces"), N_("Move window to workspace 2")) \
	_("MoveToWorkspace3Key",         N_("Workspaces"), N_("Move window to workspace 3")) \
	_("MoveToWorkspace4Key",         N_("Workspaces"), N_("Move window to workspace 4")) \
	_("MoveToWorkspace5Key",         N_("Workspaces"), N_("Move window to workspace 5")) \
	_("MoveToWorkspace6Key",         N_("Workspaces"), N_("Move window to workspace 6")) \
	_("MoveToWorkspace7Key",         N_("Workspaces"), N_("Move window to workspace 7")) \
	_("MoveToWorkspace8Key",         N_("Workspaces"), N_("Move window to workspace 8")) \
	_("MoveToWorkspace9Key",         N_("Workspaces"), N_("Move window to workspace 9")) \
	_("MoveToWorkspace10Key",        N_("Workspaces"), N_("Move window to workspace 10")) \
	_("ToggleWorkspace1Key",         N_("Workspaces"), N_("Toggle window on workspace 1")) \
	_("ToggleWorkspace2Key",         N_("Workspaces"), N_("Toggle window on workspace 2")) \
	_("ToggleWorkspace3Key",         N_("Workspaces"), N_("Toggle window on workspace 3")) \
	_("ToggleWorkspace4Key",         N_("Workspaces"), N_("Toggle window on workspace 4")) \
	_("ToggleWorkspace5Key",         N_("Workspaces"), N_("Toggle window on workspace 5")) \
	_("ToggleWorkspace6Key",         N_("Workspaces"), N_("Toggle window on workspace 6")) \
	_("ToggleWorkspace7Key",         N_("Workspaces"), N_("Toggle window on workspace 7")) \
	_("ToggleWorkspace8Key",         N_("Workspaces"), N_("Toggle window on workspace 8")) \
	_("ToggleWorkspace9Key",         N_("Workspaces"), N_("Toggle window on workspace 9")) \
	_("ToggleWorkspace10Key",        N_("Workspaces"), N_("Toggle window on workspace 10")) \
	\
	_("LayoutCycleKey",      N_("Tiling & Layout"), N_("Cycle workspace tiling layout")) \
	_("LayoutCycleBackKey",  N_("Tiling & Layout"), N_("Cycle workspace tiling layout back")) \
	_("LayoutAxisKey",       N_("Tiling & Layout"), N_("Swap tiling layout axis")) \
	_("LayoutFloatKey",      N_("Tiling & Layout"), N_("Set workspace layout to float")) \
	_("MasterCountIncKey",   N_("Tiling & Layout"), N_("Increase layout master count")) \
	_("MasterCountDecKey",   N_("Tiling & Layout"), N_("Decrease layout master count")) \
	_("MasterRatioIncKey",   N_("Tiling & Layout"), N_("Increase layout master ratio")) \
	_("MasterRatioDecKey",   N_("Tiling & Layout"), N_("Decrease layout master ratio")) \
	_("TilingToggleKey",     N_("Tiling & Layout"), N_("Toggle tiling for active window")) \
	_("TilingSwapMasterKey", N_("Tiling & Layout"), N_("Swap active window into master")) \
	\
	_("MarkSetKey",   N_("Window Marks"), N_("Mark window: set mark")) \
	_("MarkUnsetKey", N_("Window Marks"), N_("Mark window: unset mark")) \
	_("MarkBringKey", N_("Window Marks"), N_("Mark window: bring marked window here")) \
	_("MarkJumpKey",  N_("Window Marks"), N_("Mark window: jump to marked window")) \
	_("MarkSwapKey",  N_("Window Marks"), N_("Mark window: swap with marked window")) \
	\
	_("RunKey",          N_("Session & Misc"), N_("Find application")) \
	_("ShowKeybindsKey", N_("Session & Misc"), N_("Show keyboard shortcuts")) \
	_("ExitKey",         N_("Session & Misc"), N_("Exit Window Maker")) \
	_("LockScreenKey",   N_("Session & Misc"), N_("Lock screen")) \
	_("ClipboardKey",    N_("Session & Misc"), N_("Open clipboard history")) \
	\
	WCFG_KEYBIND_VDESK(_) \
	WCFG_KEYBIND_LOCK(_)

#endif /* WMCONFIG_KEYBIND_DESCRIPTIONS_H_ */
