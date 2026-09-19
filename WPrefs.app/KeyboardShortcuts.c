/* KeyboardShortcuts.c- keyboard shortcut bindings
 *
 *  WPrefs - Window Maker Preferences Program
 *
 *  Copyright (c) 1998-2003 Alfredo K. Kojima
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

#include "WPrefs.h"
#include <ctype.h>

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

typedef struct _Panel {
	WMBox *box;

	char *sectionName;

	char *description;

	CallbackRec callbacks;

	WMWidget *parent;

	WMLabel *actL;
	WMList *actLs;

	WMFrame *shoF;
	WMTextField *shoT;
	WMButton *cleB;
	WMButton *defB;

	WMLabel *instructionsL;

	WMColor *white;
	WMColor *black;
	WMColor *gray;
	WMFont *font;

	bool capturing;
	char **shortcuts;
	int actionCount;
} _Panel;

#define ICON_FILE	"keyshortcuts"

#define DIR_KEYS(stem, l, r, u, d) \
	{ stem "LeftKey",  N_(l) }, \
	{ stem "RightKey", N_(r) }, \
	{ stem "UpKey",    N_(u) }, \
	{ stem "DownKey",  N_(d) }

/*
 * List of user definable shortcut keys
 * First parameter is the internal keyword known by WMaker
 * Second is the text displayed to the user
 */
static struct keyOption {
	const char *key;
	const char *title;
} keyOptions[] = {
	{ "RootMenuKey",    N_("Open applications menu") },
	{ "WindowListKey",  N_("Open window list menu") },
	{ "WindowMenuKey",  N_("Open window commands menu") },
	{ "HideKey",        N_("Hide active application") },
	{ "HideOthersKey",  N_("Hide other applications") },
	{ "ShowDesktopKey", N_("Toggle show desktop (minimize/restore all)") },
	{ "ShowAllKey",     N_("Show all windows") },
	{ "ArrangeIconsKey", N_("Arrange icons") },
	{ "MiniaturizeKey", N_("Miniaturize active window") },
	{ "MiniaturizeAllKey", N_("Miniaturize all windows") },
	{ "MiniaturizeOthersKey", N_("Miniaturize other windows") },
	{ "CloseKey",       N_("Close active window") },
	{ "MaximizeKey",    N_("Maximize active window") },
	{ "VMaximizeKey",   N_("Maximize active window vertically") },
	{ "HMaximizeKey",   N_("Maximize active window horizontally") },
	{ "LHMaximizeKey",  N_("Maximize active window left half") },
	{ "RHMaximizeKey",  N_("Maximize active window right half") },
	{ "THMaximizeKey",  N_("Maximize active window top half") },
	{ "BHMaximizeKey",  N_("Maximize active window bottom half") },
	{ "TLCMaximizeKey", N_("Maximize active window top left corner") },
	{ "TRCMaximizeKey", N_("Maximize active window top right corner") },
	{ "BLCMaximizeKey", N_("Maximize active window bottom left corner") },
	{ "BRCMaximizeKey", N_("Maximize active window bottom right corner") },
	{ "MaximusKey",     N_("Tile active window") },
	{ "LayoutCycleKey",     N_("Cycle workspace tiling layout") },
	{ "LayoutCycleBackKey", N_("Cycle workspace tiling layout back") },
	{ "LayoutAxisKey",      N_("Swap tiling layout axis") },
	{ "LayoutFloatKey",     N_("Set workspace layout to float") },
	{ "MasterCountIncKey",  N_("Increase layout master count") },
	{ "MasterCountDecKey",  N_("Decrease layout master count") },
	{ "MasterRatioIncKey",  N_("Increase layout master ratio") },
	{ "MasterRatioDecKey",  N_("Decrease layout master ratio") },
	{ "TilingToggleKey",    N_("Toggle tiling for active window") },
	{ "TilingSwapMasterKey", N_("Swap active window into master") },
	{ "ToggleFullscreenKey", N_("Toggle fullscreen for active window") },
	{ "CenterKey",      N_("Center active window") },
	{ "KeepOnTopKey",   N_("Toggle window on top status") },
	{ "KeepAtBottomKey",N_("Toggle window at bottom status") },
	{ "OmnipresentKey", N_("Toggle window omnipresent status") },
	{ "RaiseKey",       N_("Raise active window") },
	{ "LowerKey",       N_("Lower active window") },
	{ "RaiseLowerKey",  N_("Raise/Lower window under mouse pointer") },
	{ "ShadeKey",       N_("Shade active window") },
	{ "MoveResizeKey",  N_("Move/Resize active window") },
	/* Edge-snap actions */
	DIR_KEYS("MoveToEdge",
		 "Move active window to left edge",
		 "Move active window to right edge",
		 "Move active window to top edge",
		 "Move active window to bottom edge"),
	{ "MoveToEdgeLeftNoSnapKey",  N_("Move active window to left screen edge") },
	{ "MoveToEdgeRightNoSnapKey", N_("Move active window to right screen edge") },
	{ "MoveToEdgeUpNoSnapKey",    N_("Move active window to top screen edge") },
	{ "MoveToEdgeDownNoSnapKey",  N_("Move active window to bottom screen edge") },
	DIR_KEYS("GrowToEdge",
		 "Grow active window to left edge",
		 "Grow active window to right edge",
		 "Grow active window to top edge",
		 "Grow active window to bottom edge"),
	DIR_KEYS("ShrinkToEdge",
		 "Shrink active window from left edge",
		 "Shrink active window from right edge",
		 "Shrink active window from top edge",
		 "Shrink active window from bottom edge"),
	{ "SelectKey",      N_("Select active window") },
	{ "FocusNextKey",   N_("Focus next window") },
	{ "FocusPrevKey",   N_("Focus previous window") },
	/* Directional window focus */
	{ "FocusWindowLeftKey",  N_("Focus the window to the left") },
	{ "FocusWindowRightKey", N_("Focus the window to the right") },
	{ "FocusWindowUpKey",    N_("Focus the window above") },
	{ "FocusWindowDownKey",  N_("Focus the window below") },
	{ "GroupWindowNextKey",   N_("Focus next window in group") },
	{ "GroupWindowPrevKey",   N_("Focus previous window in group") },
	{ "GroupNextKey",   N_("Switch to next application group") },
	{ "GroupPrevKey",   N_("Switch to previous application group") },

	/* Titlebar tab grouping */
	{ "GroupTabNextKey",   N_("Switch to next tab in group") },
	{ "GroupTabPrevKey",   N_("Switch to previous tab in group") },
	{ "GroupTabAttachKey", N_("Attach window as tab") },
	{ "GroupTabDetachKey", N_("Detach tab from group") },

	/* Vim-like Window Marking */
	{ "MarkSetKey",   N_("Mark window: set mark") },
	{ "MarkUnsetKey", N_("Mark window: unset mark") },
	{ "MarkBringKey", N_("Mark window: bring marked window here") },
	{ "MarkJumpKey",  N_("Mark window: jump to marked window") },
	{ "MarkSwapKey",  N_("Mark window: swap with marked window") },

	/* Workspace Related */
	{ "WorkspaceMapKey",  N_("Open workspace pager") },
	{ "NextWorkspaceKey", N_("Switch to next workspace") },
	{ "PrevWorkspaceKey", N_("Switch to previous workspace") },
	{ "LastWorkspaceKey", N_("Switch to last used workspace") },
	{ "NextWorkspaceLayerKey", N_("Switch to next ten workspaces") },
	{ "PrevWorkspaceLayerKey", N_("Switch to previous ten workspaces") },
	{ "LeftWorkspaceKey",  N_("Switch to workspace on the left") },
	{ "RightWorkspaceKey", N_("Switch to workspace on the right") },
	{ "UpWorkspaceKey",    N_("Switch to workspace above") },
	{ "DownWorkspaceKey",  N_("Switch to workspace below") },
	{ "Workspace1Key",  N_("Switch to workspace 1") },
	{ "Workspace2Key",  N_("Switch to workspace 2") },
	{ "Workspace3Key",  N_("Switch to workspace 3") },
	{ "Workspace4Key",  N_("Switch to workspace 4") },
	{ "Workspace5Key",  N_("Switch to workspace 5") },
	{ "Workspace6Key",  N_("Switch to workspace 6") },
	{ "Workspace7Key",  N_("Switch to workspace 7") },
	{ "Workspace8Key",  N_("Switch to workspace 8") },
	{ "Workspace9Key",  N_("Switch to workspace 9") },
	{ "Workspace10Key", N_("Switch to workspace 10") },
	{ "MoveToNextWorkspaceKey",      N_("Move window to next workspace") },
	{ "MoveToPrevWorkspaceKey",      N_("Move window to previous workspace") },
	{ "MoveToLastWorkspaceKey",      N_("Move window to last used workspace") },
	{ "MoveToNextWorkspaceLayerKey", N_("Move window to next ten workspaces") },
	{ "MoveToPrevWorkspaceLayerKey", N_("Move window to previous ten workspaces") },
	{ "MoveToLeftWorkspaceKey",  N_("Move window to workspace on the left") },
	{ "MoveToRightWorkspaceKey", N_("Move window to workspace on the right") },
	{ "MoveToUpWorkspaceKey",    N_("Move window to workspace above") },
	{ "MoveToDownWorkspaceKey",  N_("Move window to workspace below") },
	{ "MoveToWorkspace1Key",  N_("Move window to workspace 1") },
	{ "MoveToWorkspace2Key",  N_("Move window to workspace 2") },
	{ "MoveToWorkspace3Key",  N_("Move window to workspace 3") },
	{ "MoveToWorkspace4Key",  N_("Move window to workspace 4") },
	{ "MoveToWorkspace5Key",  N_("Move window to workspace 5") },
	{ "MoveToWorkspace6Key",  N_("Move window to workspace 6") },
	{ "MoveToWorkspace7Key",  N_("Move window to workspace 7") },
	{ "MoveToWorkspace8Key",  N_("Move window to workspace 8") },
	{ "MoveToWorkspace9Key",  N_("Move window to workspace 9") },
	{ "MoveToWorkspace10Key", N_("Move window to workspace 10") },
	{ "ToggleWorkspace1Key",  N_("Toggle window on workspace 1") },
	{ "ToggleWorkspace2Key",  N_("Toggle window on workspace 2") },
	{ "ToggleWorkspace3Key",  N_("Toggle window on workspace 3") },
	{ "ToggleWorkspace4Key",  N_("Toggle window on workspace 4") },
#ifdef VIRTUAL_DESKTOP
	{ "VirtualEdgeLeftKey",  N_("Move VirtualDesktop to next left edge") },
	{ "VirtualEdgeRightKey", N_("Move VirtualDesktop to next right edge") },
	{ "VirtualEdgeUpKey",    N_("Move VirtualDesktop to next top edge") },
	{ "VirtualEdgeDownKey",  N_("Move VirtualDesktop to next bottom edge") },
	{ "VirtualDesktopHomeKey",   N_("Move VirtualDesktop to its origin") },
	{ "VirtualDesktopCenterKey", N_("Center the focused window in the VirtualDesktop") },
	{ "VirtualDesktopPinKey",    N_("Pin the focused window to the VirtualDesktop viewport") },
#endif
	{ "ToggleWorkspace5Key",  N_("Toggle window on workspace 5") },
	{ "ToggleWorkspace6Key",  N_("Toggle window on workspace 6") },
	{ "ToggleWorkspace7Key",  N_("Toggle window on workspace 7") },
	{ "ToggleWorkspace8Key",  N_("Toggle window on workspace 8") },
	{ "ToggleWorkspace9Key",  N_("Toggle window on workspace 9") },
	{ "ToggleWorkspace10Key", N_("Toggle window on workspace 10") },

	/* Window Selection */
	{ "WindowShortcut1Key",  N_("Shortcut for window 1") },
	{ "WindowShortcut2Key",  N_("Shortcut for window 2") },
	{ "WindowShortcut3Key",  N_("Shortcut for window 3") },
	{ "WindowShortcut4Key",  N_("Shortcut for window 4") },
	{ "WindowShortcut5Key",  N_("Shortcut for window 5") },
	{ "WindowShortcut6Key",  N_("Shortcut for window 6") },
	{ "WindowShortcut7Key",  N_("Shortcut for window 7") },
	{ "WindowShortcut8Key",  N_("Shortcut for window 8") },
	{ "WindowShortcut9Key",  N_("Shortcut for window 9") },
	{ "WindowShortcut10Key", N_("Shortcut for window 10") },

	/* Head Selection */
	{ "MoveTo12to6Head",     N_("Move to right/bottom/left/top head") },
	{ "MoveTo6to12Head",    N_("Move to left/top/right/bottom head") },

	/* Misc. */
	{ "WindowRelaunchKey", N_("Launch new instance of application") },
	{ "ScreenSwitchKey",   N_("Switch to next screen/monitor") },
	{ "RunKey",            N_("Find application") },
	{ "ExitKey",            N_("Exit Window Maker") },
	{ "DockRaiseLowerKey", N_("Raise/Lower Dock") },
	{ "ClipRaiseLowerKey", N_("Raise/Lower Clip") },
	{ "ScreenCaptureKey", N_("Capture the entire screen") },
	{ "WindowCaptureKey", N_("Capture a window") },
	{ "PartialCaptureKey", N_("Capture a portion of the screen") },
	{ "LockScreenKey",     N_("Lock screen") },
	{ "ClipboardKey",      N_("Open clipboard history") }
#ifdef XKB_MODELOCK
	,{ "ToggleKbdModeKey", N_("Toggle keyboard language") }
#endif				/* XKB_MODELOCK */
};



static int NumLockMask(W_Screen *scr)
{
	WXModifierSlots slots;
	void *state = W_XkbStateOf(scr);

	W_XkbModifierSlots(WMScreenDisplay(scr),
			   state ? xkb_state_get_keymap(state) : NULL, &slots);
	return slots.numlock >= 0 ? 1 << slots.numlock : 0;
}

/* Append the modifier prefix and key name to the keybuf */
static void build_key_combo(unsigned int xkstate, const char *keyname,
                             unsigned int numlock_mask, char keybuf[64])
{
	if (xkstate & XCB_MOD_MASK_CONTROL)
		wstrlcat(keybuf, "Control+", 64);
	if (xkstate & XCB_MOD_MASK_SHIFT)
		wstrlcat(keybuf, "Shift+", 64);
	if ((numlock_mask != XCB_MOD_MASK_1) && (xkstate & XCB_MOD_MASK_1))
		wstrlcat(keybuf, "Mod1+", 64);
	if ((numlock_mask != XCB_MOD_MASK_2) && (xkstate & XCB_MOD_MASK_2))
		wstrlcat(keybuf, "Mod2+", 64);
	if ((numlock_mask != XCB_MOD_MASK_3) && (xkstate & XCB_MOD_MASK_3))
		wstrlcat(keybuf, "Mod3+", 64);
	if ((numlock_mask != XCB_MOD_MASK_4) && (xkstate & XCB_MOD_MASK_4))
		wstrlcat(keybuf, "Mod4+", 64);
	if ((numlock_mask != XCB_MOD_MASK_5) && (xkstate & XCB_MOD_MASK_5))
		wstrlcat(keybuf, "Mod5+", 64);
	wstrlcat(keybuf, keyname, 64);
}

static void keychainTimeout(void *data)
{
	*(bool *)data = true;
}

static bool waitForKeychainEvent(xcb_connection_t *dpy, int timeout_ms)
{
	bool fired = false;
	WMHandlerID timer;

	timer = WMAddTimerHandler(timeout_ms, keychainTimeout, &fired);
	while (!fired && !WXEvqPending(dpy)) {
		wXFlush(dpy);
		W_HandleInputEvents(true, xcb_get_file_descriptor(dpy));
	}

	if (!fired)
		WMDeleteTimerHandler(timer);

	return WXEvqPending(dpy);
}

/*
 * Interactively capture a key shortcut or keychain,
 * function waits KeychainTimeoutDelay or 300 ms after
 * each key press for another key in the chain,
 * and returns the full key specification string.
 */
char *capture_shortcut(W_Screen *scr, bool *capturing, bool convert_case)
{
	xcb_generic_event_t *ev = NULL;
	xcb_connection_t *dpy = WMScreenDisplay(scr);
	xkb_keysym_t ksym, uksym;
	/* Large enough for several chained chords */
	char buffer[512];
	char keybuf[64];
	char *key;
	unsigned int numlock_mask;
	bool have_key = false;
	bool chain_mode;
	int timeout_ms;

	timeout_ms = GetIntegerForKey("KeychainTimeoutDelay");
	if (timeout_ms <= 0)
		timeout_ms = 300;

	buffer[0] = '\0';

	/* ---- Phase 1: capture the first key (blocking) ---- */
	while (*capturing) {
		wXAllowEvents(dpy, XCB_ALLOW_ASYNC_KEYBOARD, XCB_CURRENT_TIME);
		WMNextEvent(dpy, &ev);
		if ((ev->response_type & 0x7f) == XCB_KEY_PRESS && (((xcb_key_press_event_t *)ev)->detail) != 0) {
			numlock_mask = NumLockMask(scr);

			/* conditional mask check to get numeric keypad keys */
			ksym = W_XkbKeysymForKeycode(scr, (((xcb_key_press_event_t *)ev)->detail), (((xcb_key_press_event_t *)ev)->state) & numlock_mask?1:0);

			if (!W_IsModifierKey(ksym)) {
				char ksname[64];

				if (convert_case) {
					uksym = xkb_keysym_to_upper(ksym);
					xkb_keysym_get_name(uksym, ksname, sizeof(ksname));
				} else {
					xkb_keysym_get_name(ksym, ksname, sizeof(ksname));
				}
				key = ksname;

				keybuf[0] = '\0';
				build_key_combo((((xcb_key_press_event_t *)ev)->state), key, numlock_mask, keybuf);
				wstrlcat(buffer, keybuf, sizeof(buffer));
				have_key = true;
				break;
			}
		}
		WMHandleEvent(ev);
		free(ev);
		ev = NULL;
	}

	/* ---- Phase 2: collect additional chain keys with timeout ---- */
	chain_mode = (timeout_ms > 0);
	while (*capturing && chain_mode) {
		if (!WXEvqPending(dpy) && !waitForKeychainEvent(dpy, timeout_ms))
			break;  /* timeout: the chain is complete */

		free(ev);
		ev = NULL;
		wXAllowEvents(dpy, XCB_ALLOW_ASYNC_KEYBOARD, XCB_CURRENT_TIME);
		WMNextEvent(dpy, &ev);
		if ((ev->response_type & 0x7f) == XCB_KEY_PRESS && (((xcb_key_press_event_t *)ev)->detail) != 0) {
			numlock_mask = NumLockMask(scr);
			ksym = W_XkbKeysymForKeycode(scr, (((xcb_key_press_event_t *)ev)->detail),
			                            (((xcb_key_press_event_t *)ev)->state) & numlock_mask ? 1 : 0);

			if (!W_IsModifierKey(ksym)) {
				char ksname[64];

				if (convert_case) {
					uksym = xkb_keysym_to_upper(ksym);
					xkb_keysym_get_name(uksym, ksname, sizeof(ksname));
				} else {
					xkb_keysym_get_name(ksym, ksname, sizeof(ksname));
				}
				key = ksname;

				keybuf[0] = '\0';
				build_key_combo((((xcb_key_press_event_t *)ev)->state), key, numlock_mask, keybuf);
				wstrlcat(buffer, " ", sizeof(buffer));
				wstrlcat(buffer, keybuf, sizeof(buffer));
			}
		} else {
			WMHandleEvent(ev);
		}
		free(ev);
		ev = NULL;
	}

	/* WMNextEvent hands us the popped event, so release the last one */
	free(ev);

	if (!have_key || !*capturing)
		return NULL;

	*capturing = 0;
	return wstrdup(buffer);
}

/*
 * check if the keystr entered is already set to another action
 * if found it returns the position in the keyOptions
 */
static int isKeySet(_Panel *panel, char *keystr)
{
	int i;
	char *str;

	for (i = 0; i < panel->actionCount; i++) {
		str = NULL;
		if (panel->shortcuts[i]) {
			str = wtrimspace(panel->shortcuts[i]);
			if (strlen(str) == 0) {
				wfree(str);
				str = NULL;
			}
		}
		if (str) {
			if (strcmp(keystr, str) == 0) {
				wfree(str);
				return i;
			}
			wfree(str);
		}
	}

	return -1;
}

static void captureClick(WMWidget * w, void *data)
{
	_Panel *panel = (_Panel *) data;
	xcb_connection_t *dpy = WMScreenDisplay(WMWidgetScreen(panel->parent));
	char *shortcut;

	if (!panel->capturing) {
		panel->capturing = 1;
		WMSetButtonText(w, _("Cancel"));
		WMSetLabelText(panel->instructionsL,
			       _("Press the desired shortcut key(s) or click Cancel to stop capturing."));
		wXGrabKeyboard(dpy, WMWidgetXID(panel->parent), true, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_CURRENT_TIME);
		shortcut = capture_shortcut(WMWidgetScreen(panel->parent), &panel->capturing, 1);
		if (shortcut) {
			int key_idx = -1;
			int row = WMGetListSelectedItemRow(panel->actLs);

			key_idx = isKeySet(panel, shortcut);
			if (key_idx >= 0 && (key_idx != row)) {
				char *msg;

				msg = wstrconcat(_("Key shortcut already in use by the "), _(keyOptions[key_idx].title));
				WMRunAlertPanel(WMWidgetScreen(w), GetWindow(),
						_("Error"),
						msg,
						_("OK"), NULL, NULL);
				wfree(msg);
				wfree(shortcut);
			} else {
				WMSetTextFieldText(panel->shoT, shortcut);
				if (row >= 0) {
					if (panel->shortcuts[row])
						wfree(panel->shortcuts[row]);
					panel->shortcuts[row] = shortcut;

					WMRedisplayWidget(panel->actLs);
				} else {
					wfree(shortcut);
				}
			}
		}
	}
	panel->capturing = 0;
	WMSetButtonText(w, _("Capture"));
	WMSetLabelText(panel->instructionsL, _("Click on Capture to interactively define the shortcut key(s)."));
	wXUngrabKeyboard(dpy, XCB_CURRENT_TIME);
}

static void clearShortcut(WMWidget * w, void *data)
{
	_Panel *panel = (_Panel *) data;
	int row = WMGetListSelectedItemRow(panel->actLs);

	/* Parameter not used, but tell the compiler that it is ok */
	(void) w;

	/* Cancel any ongoing capture so the keychain loop is unblocked */
	panel->capturing = 0;

	WMSetTextFieldText(panel->shoT, NULL);

	if (row >= 0) {
		if (panel->shortcuts[row])
			wfree(panel->shortcuts[row]);
		panel->shortcuts[row] = NULL;
		WMRedisplayWidget(panel->actLs);
	}
}

static void typedKeys(void *observerData, WMNotification * notification)
{
	_Panel *panel = (_Panel *) observerData;
	int row = WMGetListSelectedItemRow(panel->actLs);

	/* Parameter not used, but tell the compiler that it is ok */
	(void) notification;

	if (row < 0)
		return;

	if (panel->shortcuts[row])
		wfree(panel->shortcuts[row]);
	panel->shortcuts[row] = WMGetTextFieldText(panel->shoT);
	if (strlen(panel->shortcuts[row]) == 0) {
		wfree(panel->shortcuts[row]);
		panel->shortcuts[row] = NULL;
	}
	WMRedisplayWidget(panel->actLs);
}

static void listClick(WMWidget * w, void *data)
{
	_Panel *panel = (_Panel *) data;
	int row = WMGetListSelectedItemRow(w);

	WMSetTextFieldText(panel->shoT, panel->shortcuts[row]);
}

static void showData(_Panel * panel)
{
	char *str;
	int i;

	for (i = 0; i < panel->actionCount; i++) {

		str = GetStringForKey(keyOptions[i].key);
		if (panel->shortcuts[i])
			wfree(panel->shortcuts[i]);
		if (str)
			panel->shortcuts[i] = wtrimspace(str);
		else
			panel->shortcuts[i] = NULL;

		if (panel->shortcuts[i] &&
		    (strcasecmp(panel->shortcuts[i], "none") == 0 || strlen(panel->shortcuts[i]) == 0)) {
			wfree(panel->shortcuts[i]);
			panel->shortcuts[i] = NULL;
		}
	}
	WMRedisplayWidget(panel->actLs);
}

static void paintItem(WMList * lPtr, int index, xcb_drawable_t d, char *text, int state, WMRect * rect)
{
	int width, height, x, y;
	_Panel *panel = (_Panel *) WMGetHangedData(lPtr);
	WMScreen *scr = WMWidgetScreen(lPtr);
	WMColor *backColor = (state & WLDSSelected) ? WMGetListSelectionColor(lPtr) : panel->gray;

	width = rect->size.width;
	height = rect->size.height;
	x = rect->pos.x;
	y = rect->pos.y;

	WMPaintColorSwatch(backColor, d, x, y, width, height);

	if (panel->shortcuts[index]) {
		WMPixmap *pix = WMGetSystemPixmap(scr, WSICheckMark);
		WMSize size = WMGetPixmapSize(pix);

		WMDrawPixmap(pix, d, x + (20 - size.width) / 2, (height - size.height) / 2 + y);
		WMReleasePixmap(pix);
	}

	WMDrawString(scr, d, panel->black, panel->font, x + 20, y, text, strlen(text));
}

static int cmpKeyOptions(const void *v1, const void *v2)
{
	int rc;
	const struct keyOption *opt1 = (struct keyOption *)v1;
	const struct keyOption *opt2 = (struct keyOption *)v2;

	if ((rc = strncmp(opt1->title, opt2->title, 20)) < 0)
		return -1;
	else if (rc > 0)
		return 1;
	return 0;
}


static void createPanel(Panel * p)
{
	_Panel *panel = (_Panel *) p;
	WMScreen *scr = WMWidgetScreen(panel->parent);
	WMColor *color;
	WMFont *boldFont;
	int i;

	panel->capturing = 0;

	panel->white = WMWhiteColor(scr);
	panel->black = WMBlackColor(scr);
	panel->gray = WMGrayColor(scr);
	panel->font = WMSystemFontOfSize(scr, 12);

	panel->box = WMCreateBox(panel->parent);
	WMSetViewExpandsToParent(WMWidgetView(panel->box), 2, 2, 2, 2);

	boldFont = WMBoldSystemFontOfSize(scr, 12);

	/* **************** Actions **************** */
	panel->actL = WMCreateLabel(panel->box);
	WMResizeWidget(panel->actL, 314, 20);
	WMMoveWidget(panel->actL, 9, 9);
	WMSetLabelFont(panel->actL, boldFont);
	WMSetLabelText(panel->actL, _("Actions"));
	WMSetLabelRelief(panel->actL, WRSunken);
	WMSetLabelTextAlignment(panel->actL, WACenter);
	color = WMDarkGrayColor(scr);
	WMSetWidgetBackgroundColor(panel->actL, color);
	WMReleaseColor(color);
	WMSetLabelTextColor(panel->actL, panel->white);

	panel->actLs = WMCreateList(panel->box);
	WMResizeWidget(panel->actLs, 314, 191);
	WMMoveWidget(panel->actLs, 9, 31);
	WMSetListUserDrawProc(panel->actLs, paintItem);
	WMHangData(panel->actLs, panel);

	qsort(keyOptions, wlengthof(keyOptions), sizeof(keyOptions[0]), cmpKeyOptions);
	for (i = 0; i < wlengthof(keyOptions); i++) {
		WMAddListItem(panel->actLs, _(keyOptions[i].title));
	}
	WMSetListAction(panel->actLs, listClick, panel);

	panel->actionCount = WMGetListNumberOfRows(panel->actLs);
	panel->shortcuts = wmalloc(sizeof(char *) * panel->actionCount);

    /***************** Shortcut ****************/

	panel->shoF = WMCreateFrame(panel->box);
	WMResizeWidget(panel->shoF, 178, 214);
	WMMoveWidget(panel->shoF, 333, 8);
	WMSetFrameTitle(panel->shoF, _("Shortcut"));

	panel->shoT = WMCreateTextField(panel->shoF);
	WMResizeWidget(panel->shoT, 160, 20);
	WMMoveWidget(panel->shoT, 9, 65);
	WMAddNotificationObserver(typedKeys, panel, WMTextDidChangeNotification, panel->shoT);

	panel->cleB = WMCreateCommandButton(panel->shoF);
	WMResizeWidget(panel->cleB, 75, 24);
	WMMoveWidget(panel->cleB, 9, 95);
	WMSetButtonText(panel->cleB, _("Clear"));
	WMSetButtonAction(panel->cleB, clearShortcut, panel);

	panel->defB = WMCreateCommandButton(panel->shoF);
	WMResizeWidget(panel->defB, 75, 24);
	WMMoveWidget(panel->defB, 94, 95);
	WMSetButtonText(panel->defB, _("Capture"));
	WMSetButtonAction(panel->defB, captureClick, panel);

	panel->instructionsL = WMCreateLabel(panel->shoF);
	WMResizeWidget(panel->instructionsL, 160, 55);
	WMMoveWidget(panel->instructionsL, 9, 140);
	WMSetLabelTextAlignment(panel->instructionsL, WACenter);
	WMSetLabelWraps(panel->instructionsL, true);
	WMSetLabelText(panel->instructionsL, _("Click on Capture to interactively define the shortcut key."));

	WMMapSubwidgets(panel->shoF);

	WMReleaseFont(boldFont);

	WMRealizeWidget(panel->box);
	WMMapSubwidgets(panel->box);

	showData(panel);
}

static void storeData(_Panel * panel)
{
	int i;
	char *str;

	for (i = 0; i < panel->actionCount; i++) {
		str = NULL;
		if (panel->shortcuts[i]) {
			str = wtrimspace(panel->shortcuts[i]);
			if (strlen(str) == 0) {
				wfree(str);
				str = NULL;
			}
		}
		if (str) {
			SetStringForKey(str, keyOptions[i].key);
			wfree(str);
		} else {
			SetStringForKey("None", keyOptions[i].key);
		}
	}
}

Panel *InitKeyboardShortcuts(WMWidget *parent)
{
	_Panel *panel;

	panel = wmalloc(sizeof(_Panel));

	panel->sectionName = _("Keyboard Shortcut Preferences");

	panel->description = _("Change the keyboard shortcuts for actions such\n"
			       "as changing workspaces and opening menus.");

	panel->parent = parent;

	panel->callbacks.createWidgets = createPanel;
	panel->callbacks.updateDomain = storeData;

	AddSection(panel, ICON_FILE);

	return panel;
}
