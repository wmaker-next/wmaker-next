/* winmenu.c - command menu for windows
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2009-2026 Window Maker Team
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

#include "wconfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>


#include "WindowMaker.h"
#include "actions.h"
#include "menu.h"
#include "main.h"
#include "window.h"
#include "client.h"
#include "application.h"
#include "keybind.h"
#include "layout.h"
#include "misc.h"
#include "framewin.h"
#include "workspace.h"
#include "winspector.h"
#include "wmspec.h"
#include "dialog.h"
#include "stacking.h"
#include "icon.h"
#include "xinerama.h"
#include "winmenu.h"
#include "switchmenu.h"
#include "tabgroup.h"


static WMenu *makeWorkspaceMenu(WScreen *scr);
static WMenu *makeOptionsMenu(WScreen *scr);
static WMenu *makeRegionSnapMenu(WScreen *scr);
static WMenu *makeEdgeSnapMenu(WScreen *scr);

/*
 * Define the Menu entry that will be present in the Window menu
 *
 * The order of this list defines the order in which they will appear;
 * make sure to keep the structure below aligned with the list because
 * the constant index is used in many places.
 */
enum
{
	MC_MAXIMIZE,
	MC_FULLSCREEN,
	MC_REGION_SNAP,
	MC_EDGE_SNAP,
	MC_MINIATURIZE,
	MC_SHADE,
	MC_HIDE,
	MC_MOVERESIZE,
	MC_SELECT,
	MC_CHANGEWKSPC,
	MC_PROPERTIES,
	MC_OPTIONS,
	MC_GROUPTAB_ATTACH,
	MC_GROUPTAB_DETACH,
	MC_GROUPTAB_CLOSEALL,
	MC_RELAUNCH,
	MC_CLOSE,
	MC_KILL
};

static const struct {
	const char *label;
	WMenu *(*generate_submenu)(WScreen *scr);
} window_menu_entries[] = {
	[MC_MAXIMIZE]    = { N_("Maximize"), NULL },
	[MC_FULLSCREEN]  = { N_("Fullscreen"), NULL },
	[MC_REGION_SNAP] = { N_("Region snap"), makeRegionSnapMenu },
	[MC_EDGE_SNAP]   = { N_("Edge snap"), makeEdgeSnapMenu },
	[MC_MINIATURIZE] = { N_("Miniaturize"), NULL },
	[MC_SHADE]       = { N_("Shade"), NULL },
	[MC_HIDE]        = { N_("Hide"), NULL },
	[MC_MOVERESIZE]  = { N_("Resize/Move"), NULL },
	[MC_SELECT]      = { N_("Select"), NULL },
	[MC_CHANGEWKSPC] = { N_("Workspaces"), makeWorkspaceMenu },
	[MC_PROPERTIES]  = { N_("Attributes..."), NULL },
	[MC_OPTIONS]     = { N_("Options"), makeOptionsMenu },
	[MC_GROUPTAB_ATTACH]   = { N_("Attach as Tab..."), NULL },
	[MC_GROUPTAB_DETACH]   = { N_("Detach Tab"), NULL },
	[MC_GROUPTAB_CLOSEALL] = { N_("Close Tab Group"), NULL },
	[MC_RELAUNCH]    = { N_("Launch"), NULL },
	[MC_CLOSE]       = { N_("Close"), NULL },
	[MC_KILL]        = { N_("Kill"), NULL }
};

/*
 * Defines the menu entries for the Options sub-menu
 *
 * These options will be placed at the beginning of the menu, the rest will
 * be populated with the Window Shortcut possibilities
 */
enum
{
	WO_AUTO_HIDE_ON_UNFOCUS,
	WO_AUTO_MINIATURIZE_ON_UNFOCUS,
	WO_KEEP_AT_BOTTOM,
	WO_KEEP_ON_TOP,
	WO_LOCK_POSITION,
	WO_LOCK_SIZE,
	WO_OMNIPRESENT,
	WO_SKIP_SWITCH_PANEL,
	WO_SKIP_WINDOW_LIST
};

static const char *const menu_options_entries[] = {
	[WO_AUTO_HIDE_ON_UNFOCUS]        = N_("Auto-hide on unfocus"),
	[WO_AUTO_MINIATURIZE_ON_UNFOCUS] = N_("Auto-miniaturize on unfocus"),
	[WO_KEEP_AT_BOTTOM]      = N_("Keep at bottom"),
	[WO_KEEP_ON_TOP]         = N_("Keep on top"),
	[WO_LOCK_POSITION]       = N_("Lock position"),
	[WO_LOCK_SIZE]           = N_("Lock size"),
	[WO_OMNIPRESENT]         = N_("Omnipresent"),
	[WO_SKIP_SWITCH_PANEL]   = N_("Skip switch panel"),
	[WO_SKIP_WINDOW_LIST]    = N_("Skip window list")
};

/*
 * Defines the menu entries for the Region snap sub-menu
 */
static const struct {
	unsigned int indicator;
	const char *label;
	unsigned int shortcut_idx;
	int maxim_direction;
} menu_maximize_entries[] = {
	{ MI_SNAP_V, N_("Vertical"), WKBD_VMAXIMIZE, MAX_VERTICAL },
	{ MI_SNAP_H, N_("Horizontal"), WKBD_HMAXIMIZE, MAX_HORIZONTAL },
	{ MI_CENTRAL, N_("Central"), WKBD_CENTRAL, MAX_CENTRAL },
	{ MI_SNAP_LH, N_("Left half"), WKBD_LHMAXIMIZE, MAX_VERTICAL | MAX_LEFTHALF },
	{ MI_SNAP_RH, N_("Right half"), WKBD_RHMAXIMIZE, MAX_VERTICAL | MAX_RIGHTHALF },
	{ MI_SNAP_TH, N_("Top half"), WKBD_THMAXIMIZE, MAX_HORIZONTAL | MAX_TOPHALF },
	{ MI_SNAP_BH, N_("Bottom half"), WKBD_BHMAXIMIZE, MAX_HORIZONTAL | MAX_BOTTOMHALF },
	{ MI_SNAP_TL, N_("Top left"), WKBD_LTCMAXIMIZE, MAX_LEFTHALF | MAX_TOPHALF },
	{ MI_SNAP_TR, N_("Top right"), WKBD_RTCMAXIMIZE, MAX_RIGHTHALF | MAX_TOPHALF },
	{ MI_SNAP_BL, N_("Bottom left"), WKBD_LBCMAXIMIZE, MAX_LEFTHALF | MAX_BOTTOMHALF },
	{ MI_SNAP_BR, N_("Bottom right"), WKBD_RBCMAXIMIZE, MAX_RIGHTHALF | MAX_BOTTOMHALF },
	{ MI_SNAP_TILED, N_("Tiled"), WKBD_MAXIMUS, MAX_MAXIMUS }
};

static void updateOptionsMenu(WMenu * menu, WWindow * wwin);

static void execWindowOptionCommand(WMenu * menu, WMenuEntry * entry)
{
	WWindow *wwin = (WWindow *) entry->clientdata;
	int skip;
	bool on;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) menu;

	switch (entry->order) {
	case WO_KEEP_AT_BOTTOM:
		on = wwin->frame->core->stacking->window_level != WMSunkenLevel;
		WSETPERWINDOWFLAG(wwin, sunken, on);
		WSETPERWINDOWFLAG(wwin, floating, 0);
		ChangeStackingLevel(wwin->frame->core,
				    on ? WMSunkenLevel : WMNormalLevel);
		break;

	case WO_KEEP_ON_TOP:
		on = wwin->frame->core->stacking->window_level != WMFloatingLevel;
		WSETPERWINDOWFLAG(wwin, floating, on);
		WSETPERWINDOWFLAG(wwin, sunken, 0);
		ChangeStackingLevel(wwin->frame->core,
				    on ? WMFloatingLevel : WMNormalLevel);
		break;

	case WO_LOCK_POSITION:
		WSETPERWINDOWFLAG(wwin, no_movable, !WFLAGP(wwin, no_movable));
		wNETWMUpdateActions(wwin, false);
		break;

	case WO_LOCK_SIZE:
		WSETPERWINDOWFLAG(wwin, no_resizable, !WFLAGP(wwin, no_resizable));
		/* resizebar and maximize button key off IS_RESIZABLE() */
		wWindowConfigureBorders(wwin);
		wFrameWindowPaint(wwin->frame);
		wNETWMUpdateActions(wwin, false);
		break;

	case WO_OMNIPRESENT:
		wWindowSetOmnipresent(wwin, !IS_OMNIPRESENT(wwin));
		break;

	case WO_SKIP_SWITCH_PANEL:
		WSETPERWINDOWFLAG(wwin, skip_switchpanel,
				  !WFLAGP(wwin, skip_switchpanel));
		break;

	case WO_SKIP_WINDOW_LIST:
		skip = !WFLAGP(wwin, skip_window_list);
		WSETPERWINDOWFLAG(wwin, skip_window_list, skip);
		UpdateSwitchMenu(wwin->screen_ptr, wwin,
				 skip ? ACTION_REMOVE : ACTION_ADD);
		break;

	case WO_AUTO_MINIATURIZE_ON_UNFOCUS:
		WSETPERWINDOWFLAG(wwin, auto_miniaturize_on_unfocus,
				  !WFLAGP(wwin, auto_miniaturize_on_unfocus));
		if (WFLAGP(wwin, auto_miniaturize_on_unfocus))
			WSETPERWINDOWFLAG(wwin, auto_hide_on_unfocus, 0);
		break;

	case WO_AUTO_HIDE_ON_UNFOCUS:
		WSETPERWINDOWFLAG(wwin, auto_hide_on_unfocus,
				  !WFLAGP(wwin, auto_hide_on_unfocus));
		if (WFLAGP(wwin, auto_hide_on_unfocus))
			WSETPERWINDOWFLAG(wwin, auto_miniaturize_on_unfocus, 0);
		break;
	}
}

static void execMaximizeCommand(WMenu * menu, WMenuEntry * entry)
{
	WWindow *wwin = (WWindow *) entry->clientdata;
	int direction = menu_maximize_entries[entry->order].maxim_direction;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) menu;

	if (direction == MAX_MAXIMUS
	    && wwin->screen_ptr->workspaces[WWIN_PRIMARY_WS(wwin)]->layout != WLAYOUT_FLOAT) {
		wLayoutSetWindowTiled(wwin, wwin->flags.no_tiling);
		return;
	}

	handleMaximize(wwin, direction);
}

static const struct {
	const char *label;
	unsigned int shortcut_idx;
	int direction;
	int action; /* 0 = move, 1 = grow, 2 = shrink */
} menu_edge_snap_entries[] = {
	{ N_("Move left"),  WKBD_MOVE_TO_EDGE_LEFT,  DIRECTION_LEFT,  0 },
	{ N_("Move right"), WKBD_MOVE_TO_EDGE_RIGHT, DIRECTION_RIGHT, 0 },
	{ N_("Move up"),    WKBD_MOVE_TO_EDGE_UP,    DIRECTION_UP,    0 },
	{ N_("Move down"),  WKBD_MOVE_TO_EDGE_DOWN,  DIRECTION_DOWN,  0 },
	{ N_("Grow left"),  WKBD_GROW_TO_EDGE_LEFT,  DIRECTION_LEFT,  1 },
	{ N_("Grow right"), WKBD_GROW_TO_EDGE_RIGHT, DIRECTION_RIGHT, 1 },
	{ N_("Grow up"),    WKBD_GROW_TO_EDGE_UP,    DIRECTION_UP,    1 },
	{ N_("Grow down"),  WKBD_GROW_TO_EDGE_DOWN,  DIRECTION_DOWN,  1 },
	{ N_("Shrink left"),  WKBD_SHRINK_TO_EDGE_LEFT,  DIRECTION_LEFT,  2 },
	{ N_("Shrink right"), WKBD_SHRINK_TO_EDGE_RIGHT, DIRECTION_RIGHT, 2 },
	{ N_("Shrink up"),    WKBD_SHRINK_TO_EDGE_UP,    DIRECTION_UP,    2 },
	{ N_("Shrink down"),  WKBD_SHRINK_TO_EDGE_DOWN,  DIRECTION_DOWN,  2 }
};

static void execEdgeSnap(WMenu *menu, WMenuEntry *entry)
{
	WWindow *wwin = (WWindow *) entry->clientdata;
	int idx = entry->order;

	(void) menu;

	switch (menu_edge_snap_entries[idx].action) {
	case 0:
		wMoveToEdge(wwin, menu_edge_snap_entries[idx].direction, true);
		break;
	case 1:
		wGrowToEdge(wwin, menu_edge_snap_entries[idx].direction);
		break;
	case 2:
		wShrinkToEdge(wwin, menu_edge_snap_entries[idx].direction);
		break;
	}
}

static void updateUnmaximizeShortcut(WMenuEntry * entry, int flags)
{
	int key;

	switch (flags & (MAX_HORIZONTAL | MAX_VERTICAL | MAX_LEFTHALF | MAX_RIGHTHALF | MAX_TOPHALF | MAX_BOTTOMHALF | MAX_MAXIMUS | MAX_CENTRAL)) {
	case MAX_HORIZONTAL:
		key = WKBD_HMAXIMIZE;
		break;

	case MAX_VERTICAL:
		key = WKBD_VMAXIMIZE;
		break;

	case MAX_CENTRAL:
		key = WKBD_CENTRAL;
		break;

	case MAX_LEFTHALF | MAX_VERTICAL:
		key = WKBD_LHMAXIMIZE;
		break;

	case MAX_RIGHTHALF | MAX_VERTICAL:
		key = WKBD_RHMAXIMIZE;
		break;

	case MAX_TOPHALF | MAX_HORIZONTAL:
		key = WKBD_THMAXIMIZE;
		break;

	case MAX_BOTTOMHALF | MAX_HORIZONTAL:
		key = WKBD_BHMAXIMIZE;
		break;

	case MAX_LEFTHALF | MAX_TOPHALF:
		key = WKBD_LTCMAXIMIZE;
		break;

	case MAX_RIGHTHALF | MAX_TOPHALF:
		key = WKBD_RTCMAXIMIZE;
		break;

	case MAX_LEFTHALF | MAX_BOTTOMHALF:
		key = WKBD_LBCMAXIMIZE;
		break;

	case MAX_RIGHTHALF | MAX_BOTTOMHALF:
		key = WKBD_RBCMAXIMIZE;
		break;

	case MAX_MAXIMUS:
		key = WKBD_MAXIMUS;
		break;

	default:
		key = WKBD_MAXIMIZE;
		break;
	}

	wMenuEntrySetShortcutText(entry, GetShortcutKey(wKeyBindings[key]));
}

static void execMenuCommand(WMenu * menu, WMenuEntry * entry)
{
	WWindow *wwin = (WWindow *) entry->clientdata;

	CloseWindowMenu(menu->frame->screen_ptr);

	switch (entry->order) {
	case MC_CLOSE:
		/* send delete message */
		wClientSendProtocol(wwin, wdisp->atom.wm.delete_window,
								  wdisp->timestamp.last_event);
		wNETWMPingWindow(wwin, wdisp->timestamp.last_event);
		break;

	case MC_KILL:
		wretain(wwin);
		if (w_global.config->dont_confirm_kill
		    || wMessageDialog(menu->frame->screen_ptr, _("Kill Application"),
				      _
				      ("This will kill the application.\nAny unsaved changes will be lost.\nPlease confirm."),
				      _("Yes"), _("No"), NULL) == WAPRDefault) {
			if (!wwin->flags.destroyed)
				wClientKill(wwin);
		}
		wrelease(wwin);
		break;

	case MC_MINIATURIZE:
		if (wwin->flags.miniaturized) {
			wDeiconifyWindow(wwin);
		} else {
			if (wwin->protocols.MINIATURIZE_WINDOW) {
				wClientSendProtocol(wwin, wdisp->atom.gnustep.wm_miniaturize_window,
										  wdisp->timestamp.last_event);
			} else {
				wIconifyWindow(wwin);
			}
		}
		break;

	case MC_MAXIMIZE:
		if (wwin->flags.maximized)
			wUnmaximizeWindow(wwin);
		else
			wMaximizeWindow(wwin, MAX_VERTICAL | MAX_HORIZONTAL,
					wGetHeadForWindow(wwin));
		break;

	case MC_FULLSCREEN:
		if (wwin->flags.fullscreen)
			wUnfullscreenWindow(wwin);
		else
			wFullscreenWindow(wwin);
		break;

	case MC_SHADE:
		if (wwin->flags.shaded)
			wUnshadeWindow(wwin);
		else
			wShadeWindow(wwin);
		break;

	case MC_SELECT:
		if (!wwin->flags.miniaturized)
			wSelectWindow(wwin, !wwin->flags.selected);
		else
			wIconSelect(wwin->icon);
		break;

	case MC_MOVERESIZE:
		wKeyboardMoveResizeWindow(wwin);
		break;

	case MC_PROPERTIES:
		wShowInspectorForWindow(wwin);
		break;

	case MC_RELAUNCH:
		(void) RelaunchWindow(wwin);
		break;

	case MC_HIDE:
		wRequestHideApplication(wApplicationOf(wwin->main_window), wwin);
		break;

	case MC_GROUPTAB_ATTACH:
		wGroupPickAndAttach(wwin->screen_ptr, wwin);
		break;

	case MC_GROUPTAB_DETACH:
		if (wwin->tab_group)
			wGroupDetach(wwin);
		break;

	case MC_GROUPTAB_CLOSEALL:
		if (wwin->tab_group)
			wGroupCloseAll(wwin->tab_group);
		break;
	}
}

static void switchWSCommand(WMenu * menu, WMenuEntry * entry)
{
	WWindow *wwin = (WWindow *) entry->clientdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) menu;

	wSelectWindow(wwin, false);
	if (wwin->frame && (WWIN_WS_MASK(wwin) & WS_BIT(entry->order)))
		wWindowRemoveFromWorkspace(wwin, entry->order);
	else
		wWindowAddToWorkspace(wwin, entry->order);
}

static void makeShortcutCommand(WMenu * menu, WMenuEntry * entry)
{
	WWindow *wwin = (WWindow *) entry->clientdata;
	WScreen *scr = wwin->screen_ptr;
	int index = entry->order - wlengthof(menu_options_entries);

	/* Parameter not used, but tell the compiler that it is ok */
	(void) menu;

	if (scr->shortcutWindows[index]) {
		WMFreeArray(scr->shortcutWindows[index]);
		scr->shortcutWindows[index] = NULL;
	}

	if (wwin->flags.selected && scr->selected_windows) {
		scr->shortcutWindows[index] = WMDuplicateArray(scr->selected_windows);
		/*WMRemoveFromArray(scr->shortcutWindows[index], wwin);
		   WMInsertInArray(scr->shortcutWindows[index], 0, wwin); */
	} else {
		scr->shortcutWindows[index] = WMCreateArray(4);
		WMAddToArray(scr->shortcutWindows[index], wwin);
	}

	wSelectWindow(wwin, !wwin->flags.selected);
	wXFlush(wdisp->conn);
	wusleep(3000);
	wSelectWindow(wwin, !wwin->flags.selected);
	wXFlush(wdisp->conn);
}

static void updateWorkspaceMenu(WMenu * menu)
{
	WScreen *scr = menu->frame->screen_ptr;
	char title[MAX_WORKSPACENAME_WIDTH + 1];
	WMenuEntry *entry;
	int i;

	for (i = 0; i < scr->workspace_count; i++) {
		if (i < menu->entry_no) {
			entry = menu->entries[i];
			if (strcmp(entry->text, scr->workspaces[i]->name) != 0) {
				wfree(entry->text);
				wstrlcpy(title, scr->workspaces[i]->name, sizeof(title));
				menu->entries[i]->text = wstrdup(title);
				menu->flags.realized = 0;
			}
		} else {
			wstrlcpy(title, scr->workspaces[i]->name, sizeof(title));

			entry = wMenuAddCallback(menu, title, switchWSCommand, NULL);
			entry->flags.indicator = 1;
			entry->flags.indicator_type = MI_CHECK;
			menu->flags.realized = 0;
		}

		/* workspace shortcut labels */
		if (i / 10 == scr->current_workspace / 10)
			wMenuEntrySetShortcutText(entry,
						  GetShortcutKey(wKeyBindings[WKBD_TOGGLE_WORKSPACE1 + (i % 10)]));
		else
			wMenuEntrySetShortcutText(entry, NULL);
	}

	if (!menu->flags.realized)
		wMenuRealize(menu);
}

static void updateMakeShortcutMenu(WMenu * menu, WWindow * wwin)
{
	WMenu *smenu = menu->cascades[menu->entries[MC_OPTIONS]->cascade];
	int i;
	char buffer[64];
	xcb_keycode_t kcode;

	if (!smenu)
		return;

	for (i = wlengthof(menu_options_entries); i < smenu->entry_no; i++) {
		int shortcutNo = i - wlengthof(menu_options_entries);
		WMenuEntry *entry = smenu->entries[i];
		WMArray *shortSelWindows = wwin->screen_ptr->shortcutWindows[shortcutNo];

		snprintf(buffer, sizeof(buffer), _("Set Shortcut %i"), shortcutNo + 1);

		if (!shortSelWindows) {
			entry->flags.indicator_on = 0;
		} else {
			entry->flags.indicator_on = 1;
			if (WMCountInArray(shortSelWindows, wwin))
				entry->flags.indicator_type = MI_DIAMOND;
			else
				entry->flags.indicator_type = MI_CHECK;
		}

		if (strcmp(buffer, entry->text) != 0) {
			wfree(entry->text);
			entry->text = wstrdup(buffer);
			smenu->flags.realized = 0;
		}

		kcode = wKeyBindings[WKBD_WINDOW1 + shortcutNo].keycode;

		if (kcode) {
			char *tmp;

			tmp = GetShortcutKey(wKeyBindings[WKBD_WINDOW1 + shortcutNo]);
			if (tmp == NULL) {
				if (entry->rtext != NULL) {
					/* There was a shortcut, but there is no more */
					wfree(entry->rtext);
					entry->rtext = NULL;
					smenu->flags.realized = 0;
				}
			} else if (entry->rtext == NULL) {
				/* There was no shortcut, but there is one now */
				entry->rtext = tmp;
				smenu->flags.realized = 0;
			} else if (strcmp(tmp, entry->rtext) != 0) {
				/* There was a shortcut, but it has changed */
				wfree(entry->rtext);
				entry->rtext = tmp;
				smenu->flags.realized = 0;
			} else {
				/* There was a shortcut but it did not change */
				wfree(tmp);
			}
			wMenuSetEnabled(smenu, i, true);
		} else {
			wMenuSetEnabled(smenu, i, false);
			if (entry->rtext) {
				wfree(entry->rtext);
				entry->rtext = NULL;
				smenu->flags.realized = 0;
			}
		}
		entry->clientdata = wwin;
	}
	if (!smenu->flags.realized)
		wMenuRealize(smenu);
}

static void updateOptionsMenu(WMenu * menu, WWindow * wwin)
{
	WMenu *smenu = menu->cascades[menu->entries[MC_OPTIONS]->cascade];

	/* keep at bottom check */
	smenu->entries[WO_KEEP_AT_BOTTOM]->clientdata = wwin;
	smenu->entries[WO_KEEP_AT_BOTTOM]->flags.indicator_on =
	    (wwin->frame->core->stacking->window_level == WMSunkenLevel) ? 1 : 0;
	wMenuSetEnabled(smenu, WO_KEEP_AT_BOTTOM, !wwin->flags.miniaturized);
	wMenuEntrySetShortcutText(smenu->entries[WO_KEEP_AT_BOTTOM], GetShortcutKey(wKeyBindings[WKBD_KEEP_AT_BOTTOM]));

	/* keep on top check */
	smenu->entries[WO_KEEP_ON_TOP]->clientdata = wwin;
	smenu->entries[WO_KEEP_ON_TOP]->flags.indicator_on =
	    (wwin->frame->core->stacking->window_level == WMFloatingLevel) ? 1 : 0;
	wMenuSetEnabled(smenu, WO_KEEP_ON_TOP, !wwin->flags.miniaturized);
	wMenuEntrySetShortcutText(smenu->entries[WO_KEEP_ON_TOP], GetShortcutKey(wKeyBindings[WKBD_KEEP_ON_TOP]));

	smenu->entries[WO_LOCK_POSITION]->clientdata = wwin;
	smenu->entries[WO_LOCK_POSITION]->flags.indicator_on =
	    WFLAGP(wwin, no_movable);

	smenu->entries[WO_LOCK_SIZE]->clientdata = wwin;
	smenu->entries[WO_LOCK_SIZE]->flags.indicator_on =
	    WFLAGP(wwin, no_resizable);

	/* omnipresent check */
	smenu->entries[WO_OMNIPRESENT]->clientdata = wwin;
	smenu->entries[WO_OMNIPRESENT]->flags.indicator_on = IS_OMNIPRESENT(wwin);
	wMenuEntrySetShortcutText(smenu->entries[WO_OMNIPRESENT], GetShortcutKey(wKeyBindings[WKBD_OMNIPRESENT]));

	smenu->entries[WO_SKIP_SWITCH_PANEL]->clientdata = wwin;
	smenu->entries[WO_SKIP_SWITCH_PANEL]->flags.indicator_on =
	    WFLAGP(wwin, skip_switchpanel);

	smenu->entries[WO_SKIP_WINDOW_LIST]->clientdata = wwin;
	smenu->entries[WO_SKIP_WINDOW_LIST]->flags.indicator_on =
	    WFLAGP(wwin, skip_window_list);

	smenu->entries[WO_AUTO_MINIATURIZE_ON_UNFOCUS]->clientdata = wwin;
	smenu->entries[WO_AUTO_MINIATURIZE_ON_UNFOCUS]->flags.indicator_on =
	    WFLAGP(wwin, auto_miniaturize_on_unfocus);

	smenu->entries[WO_AUTO_HIDE_ON_UNFOCUS]->clientdata = wwin;
	smenu->entries[WO_AUTO_HIDE_ON_UNFOCUS]->flags.indicator_on =
	    WFLAGP(wwin, auto_hide_on_unfocus);

	smenu->flags.realized = 0;
	wMenuRealize(smenu);
}

static void updateMaximizeMenu(WMenu * menu, WWindow * wwin)
{
	WMenu *smenu = menu->cascades[menu->entries[MC_REGION_SNAP]->cascade];
	int i;

	for (i = 0; i < smenu->entry_no; i++) {
		smenu->entries[i]->clientdata = wwin;
		wMenuEntrySetShortcutText(smenu->entries[i],
					  GetShortcutKey(wKeyBindings[menu_maximize_entries[i].shortcut_idx]));
		if (menu_maximize_entries[i].maxim_direction == MAX_MAXIMUS) {
			wfree(smenu->entries[i]->text);
			if (wwin->screen_ptr->workspaces[WWIN_PRIMARY_WS(wwin)]->layout != WLAYOUT_FLOAT
			    && !wwin->flags.no_tiling)
				smenu->entries[i]->text = wstrdup(_("Untiled"));
			else
				smenu->entries[i]->text = wstrdup(_("Tiled"));
		}
	}

	smenu->flags.realized = 0;
	wMenuRealize(smenu);
}

static void updateEdgeSnapMenu(WMenu *menu, WWindow *wwin)
{
	WMenu *smenu = menu->cascades[menu->entries[MC_EDGE_SNAP]->cascade];
	int i;

	for (i = 0; i < smenu->entry_no; i++) {
		smenu->entries[i]->clientdata = wwin;
		wMenuEntrySetShortcutText(smenu->entries[i],
					  GetShortcutKey(wKeyBindings[menu_edge_snap_entries[i].shortcut_idx]));
	}

	smenu->flags.realized = 0;
	wMenuRealize(smenu);
}

static WMenu *makeWorkspaceMenu(WScreen * scr)
{
	WMenu *menu;

	menu = wMenuCreate(scr, NULL, false);
	if (!menu) {
		wwarning(_("could not create submenu for window menu"));
		return NULL;
	}

	updateWorkspaceMenu(menu);

	/*
	 * The Workspace Menu is made visible in the screen structure because
	 * it is updated when there is a change on workspaces. This was done
	 * to be efficient, avoiding re-generating completely the window menu
	 * and its sub-menus every time it is needed.
	 */
	scr->workspace_submenu = menu;

	return menu;
}

static WMenu *makeOptionsMenu(WScreen * scr)
{
	WMenu *menu;
	WMenuEntry *entry;
	int i;

	menu = wMenuCreate(scr, NULL, false);
	if (!menu) {
		wwarning(_("could not create submenu for window menu"));
		return NULL;
	}

	for (i = 0; i < wlengthof(menu_options_entries); i++) {
		entry = wMenuAddCallback(menu, _(menu_options_entries[i]), execWindowOptionCommand, NULL);
		entry->flags.indicator = 1;
		entry->flags.indicator_type = MI_CHECK;
	}

	for (i = 0; i < MAX_WINDOW_SHORTCUTS; i++) {
		entry = wMenuAddCallback(menu, "", makeShortcutCommand, NULL);
		entry->flags.indicator = 1;
	}

	return menu;
}

static WMenu *makeRegionSnapMenu(WScreen *scr)
{
	WMenu *menu;
	WMenuEntry *entry;
	int i;

	menu = wMenuCreate(scr, NULL, false);
	if (!menu) {
		wwarning(_("could not create submenu for window menu"));
		return NULL;
	}

	for (i = 0; i < wlengthof(menu_maximize_entries); i++) {
		entry = wMenuAddCallback(menu, _(menu_maximize_entries[i].label), execMaximizeCommand, NULL);
		entry->flags.indicator = entry->flags.indicator_on = 1;
		entry->flags.indicator_type = menu_maximize_entries[i].indicator;
	}

	return menu;
}

static WMenu *makeEdgeSnapMenu(WScreen *scr)
{
	WMenu *menu;
	WMenuEntry *entry;
	int i;

	menu = wMenuCreate(scr, NULL, false);
	if (!menu) {
		wwarning(_("could not create submenu for window menu"));
		return NULL;
	}

	for (i = 0; i < wlengthof(menu_edge_snap_entries); i++) {
		entry = wMenuAddCallback(menu, _(menu_edge_snap_entries[i].label), execEdgeSnap, NULL);
		wMenuEntrySetShortcutText(entry,
			GetShortcutKey(wKeyBindings[menu_edge_snap_entries[i].shortcut_idx]));
	}

	return menu;
}

static WMenu *createWindowMenu(WScreen * scr)
{
	WMenu *menu;
	int i;

	menu = wMenuCreate(scr, NULL, false);

	for (i = 0; i < wlengthof(window_menu_entries); i++) {
		WMenuEntry *entry;

		entry = wMenuAddCallback(menu, _(window_menu_entries[i].label),
		                         (window_menu_entries[i].generate_submenu == NULL)?execMenuCommand:NULL,
		                         NULL);
		if (window_menu_entries[i].generate_submenu != NULL) {
			WMenu *submenu;

			submenu = window_menu_entries[i].generate_submenu(scr);
			wMenuEntrySetCascade(menu, entry, submenu);
		}
	}

	return menu;
}

void CloseWindowMenu(WScreen * scr)
{
	if (scr->window_menu) {
		if (scr->window_menu->flags.mapped)
			wMenuUnmap(scr->window_menu);

		if (scr->window_menu->entries[0]->clientdata) {
			WWindow *wwin = (WWindow *) scr->window_menu->entries[0]->clientdata;

			wwin->flags.menu_open_for_me = 0;
		}
		scr->window_menu->entries[0]->clientdata = NULL;
	}
}

static void updateMenuForWindow(WMenu * menu, WWindow * wwin)
{
	WApplication *wapp = wApplicationOf(wwin->main_window);
	WScreen *scr = wwin->screen_ptr;
	int i;

	updateOptionsMenu(menu, wwin);
	updateMaximizeMenu(menu, wwin);
	updateEdgeSnapMenu(menu, wwin);

	updateMakeShortcutMenu(menu, wwin);

	wMenuSetEnabled(menu, MC_HIDE, wwin->protocols.HIDE_APP
			|| (wapp != NULL && !WFLAGP(wapp->main_window_desc, no_appicon)));

	wMenuSetEnabled(menu, MC_CLOSE, (wwin->protocols.DELETE_WINDOW && !WFLAGP(wwin, no_closable)));

	if (wwin->flags.miniaturized) {
		wfree(menu->entries[MC_MINIATURIZE]->text);
		menu->entries[MC_MINIATURIZE]->text = wstrdup(_("Deminiaturize"));
	} else {
		wfree(menu->entries[MC_MINIATURIZE]->text);
		menu->entries[MC_MINIATURIZE]->text = wstrdup(_("Miniaturize"));
	}

	wMenuSetEnabled(menu, MC_MINIATURIZE, !WFLAGP(wwin, no_miniaturizable));

	if (wwin->flags.maximized) {
		wfree(menu->entries[MC_MAXIMIZE]->text);
		menu->entries[MC_MAXIMIZE]->text = wstrdup(_("Unmaximize"));
		updateUnmaximizeShortcut(menu->entries[MC_MAXIMIZE], wwin->flags.maximized);
	} else {
		wfree(menu->entries[MC_MAXIMIZE]->text);
		menu->entries[MC_MAXIMIZE]->text = wstrdup(_("Maximize"));
		wMenuEntrySetShortcutText(menu->entries[MC_MAXIMIZE], GetShortcutKey(wKeyBindings[WKBD_MAXIMIZE]));
	}
	wMenuSetEnabled(menu, MC_MAXIMIZE, IS_RESIZABLE(wwin));

	if (wwin->flags.fullscreen) {
		wfree(menu->entries[MC_FULLSCREEN]->text);
		menu->entries[MC_FULLSCREEN]->text = wstrdup(_("Exit Fullscreen"));
	} else {
		wfree(menu->entries[MC_FULLSCREEN]->text);
		menu->entries[MC_FULLSCREEN]->text = wstrdup(_("Fullscreen"));
	}
	wMenuEntrySetShortcutText(menu->entries[MC_FULLSCREEN],
				  GetShortcutKey(wKeyBindings[WKBD_FULLSCREEN]));
	wMenuSetEnabled(menu, MC_FULLSCREEN, wwin->flags.fullscreen || IS_RESIZABLE(wwin));

	wMenuSetEnabled(menu, MC_MOVERESIZE, IS_RESIZABLE(wwin)
			&& !wwin->flags.miniaturized);

	wMenuSetEnabled(menu, MC_EDGE_SNAP, (IS_MOVABLE(wwin) || IS_RESIZABLE(wwin))
			&& !wwin->flags.miniaturized);

	if (wwin->flags.shaded) {
		wfree(menu->entries[MC_SHADE]->text);
		menu->entries[MC_SHADE]->text = wstrdup(_("Unshade"));
	} else {
		wfree(menu->entries[MC_SHADE]->text);
		menu->entries[MC_SHADE]->text = wstrdup(_("Shade"));
	}

	wMenuSetEnabled(menu, MC_SHADE, !WFLAGP(wwin, no_shadeable)
			&& !wwin->flags.miniaturized);

	if (wwin->flags.selected) {
		wfree(menu->entries[MC_SELECT]->text);
		menu->entries[MC_SELECT]->text = wstrdup(_("Deselect"));
	} else {
		wfree(menu->entries[MC_SELECT]->text);
		menu->entries[MC_SELECT]->text = wstrdup(_("Select"));
	}

	wMenuSetEnabled(menu, MC_CHANGEWKSPC, !IS_OMNIPRESENT(wwin));

	if (!wwin->flags.inspector_open) {
		wMenuSetEnabled(menu, MC_PROPERTIES, true);
	} else {
		wMenuSetEnabled(menu, MC_PROPERTIES, false);
	}

	/* Update shortcut labels except for (Un)Maximize which is
	 * handled separately.
	 */
	wMenuEntrySetShortcutText(menu->entries[MC_MINIATURIZE], GetShortcutKey(wKeyBindings[WKBD_MINIATURIZE]));
	wMenuEntrySetShortcutText(menu->entries[MC_SHADE], GetShortcutKey(wKeyBindings[WKBD_SHADE]));
	wMenuEntrySetShortcutText(menu->entries[MC_HIDE], GetShortcutKey(wKeyBindings[WKBD_HIDE]));
	wMenuEntrySetShortcutText(menu->entries[MC_MOVERESIZE], GetShortcutKey(wKeyBindings[WKBD_MOVERESIZE]));
	wMenuEntrySetShortcutText(menu->entries[MC_SELECT], GetShortcutKey(wKeyBindings[WKBD_SELECT]));
	wMenuEntrySetShortcutText(menu->entries[MC_RELAUNCH], GetShortcutKey(wKeyBindings[WKBD_RELAUNCH]));
	wMenuEntrySetShortcutText(menu->entries[MC_CLOSE], GetShortcutKey(wKeyBindings[WKBD_CLOSE]));

	/* tab group entries. "Attach as Tab..." is the entry point and stays
	 * visible (disabled when not tabable); "Detach Tab" and "Close Tab
	 * Group" only make sense once the window is already grouped, so hide
	 * them rather than grey them out. */
	wMenuSetEnabled(menu, MC_GROUPTAB_ATTACH, wGroupIsTabable(wwin));
	wMenuSetHidden(menu, MC_GROUPTAB_DETACH, wwin->tab_group == NULL);
	wMenuSetHidden(menu, MC_GROUPTAB_CLOSEALL, wwin->tab_group == NULL);
	wMenuEntrySetShortcutText(menu->entries[MC_GROUPTAB_ATTACH], GetShortcutKey(wKeyBindings[WKBD_GROUPTAB_ATTACH]));
	wMenuEntrySetShortcutText(menu->entries[MC_GROUPTAB_DETACH], GetShortcutKey(wKeyBindings[WKBD_GROUPTAB_DETACH]));

	/* set the client data of the entries to the window */
	for (i = 0; i < menu->entry_no; i++) {
		menu->entries[i]->clientdata = wwin;
	}

	for (i = 0; i < scr->workspace_submenu->entry_no; i++) {
		scr->workspace_submenu->entries[i]->clientdata = wwin;
		scr->workspace_submenu->entries[i]->flags.indicator_on =
			wwin->frame && (WWIN_WS_MASK(wwin) & WS_BIT(i)) ? 1 : 0;
		wMenuSetEnabled(scr->workspace_submenu, i, true);
	}
	scr->workspace_submenu->flags.realized = 0;

	menu->flags.realized = 0;
	wMenuRealize(menu);
}

static WMenu *open_window_menu_core(WWindow *wwin)
{
	WScreen *scr = wwin->screen_ptr;
	WMenu *menu;

	wwin->flags.menu_open_for_me = 1;

	if (!scr->window_menu) {
		scr->window_menu = createWindowMenu(scr);
	} else {
		updateWorkspaceMenu(scr->workspace_submenu);
	}

	menu = scr->window_menu;
	if (menu->flags.mapped) {
		wMenuUnmap(menu);
		if (menu->entries[0]->clientdata == wwin)
			return NULL;
	}

	updateMenuForWindow(menu, wwin);

	return menu;
}

static void prepare_menu_position(WMenu *menu, int *x, int *y)
{
	WMRect rect;

	rect = wGetRectForHead(menu->frame->screen_ptr,
			       wGetHeadForPointerLocation(menu->frame->screen_ptr));
	if (*x < rect.pos.x - menu->frame->core->width / 2)
		*x = rect.pos.x - menu->frame->core->width / 2;
	if (*y < rect.pos.y)
		*y = rect.pos.y;
}

void OpenWindowMenu(WWindow *wwin, int x, int y, int keyboard)
{
	WMenu *menu;

	menu = open_window_menu_core(wwin);
	if (!menu)
		return;

	/* Specific menu position */
	x -= menu->frame->core->width / 2;
	if (x + menu->frame->core->width > wwin->frame_x + wwin->frame->core->width)
		x = wwin->frame_x + wwin->frame->core->width - menu->frame->core->width;
	if (x < wwin->frame_x)
		x = wwin->frame_x;

	/* Common menu position */
	prepare_menu_position(menu, &x, &y);

	if (!wwin->flags.internal_window)
		wMenuMapAt(menu, x, y, keyboard);
}

void OpenWindowMenu2(WWindow *wwin, int x, int y, int keyboard)
{
	int i;
	WMenu *menu;
	WScreen *scr = wwin->screen_ptr;

	menu = open_window_menu_core(wwin);
	if (!menu)
		return;

	/* Specific menu position */
	for (i = 0; i < scr->workspace_submenu->entry_no; i++) {
		scr->workspace_submenu->entries[i]->clientdata = wwin;
		scr->workspace_submenu->entries[i]->flags.indicator_on =
			wwin->frame && (WWIN_WS_MASK(wwin) & WS_BIT(i)) ? 1 : 0;
		wMenuSetEnabled(scr->workspace_submenu, i, true);
	}

	x -= menu->frame->core->width / 2;

	/* Common menu position */
	prepare_menu_position(menu, &x, &y);

	if (!wwin->flags.internal_window)
		wMenuMapAt(menu, x, y, keyboard);
}

void OpenMiniwindowMenu(WWindow * wwin, int x, int y)
{
	WMenu *menu;

	menu = open_window_menu_core(wwin);
	if (!menu)
		return;

	x -= menu->frame->core->width / 2;

	wMenuMapAt(menu, x, y, false);
}

void DestroyWindowMenu(WScreen *scr)
{
	if (scr->window_menu) {
		wMenuDestroy(scr->window_menu, true);
		scr->window_menu = NULL;
	}
}
