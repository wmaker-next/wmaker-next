/* dock.c- built-in Dock module for WindowMaker
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 1998-2003 Dan Pascu
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
#include <libgen.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <math.h>
#include <limits.h>
#include <dirent.h>

#include <cairo.h>
#include <WINGs/WINGsP.h>
#include <sys/stat.h>

#include <WINGs/WUtil.h>

#ifndef PATH_MAX
#define PATH_MAX DEFAULT_PATH_MAX
#endif

#include "WindowMaker.h"
#include "wcore.h"
#include "window.h"
#include "icon.h"
#include "appicon.h"
#include "actions.h"
#include "stacking.h"
#include "desktopfile.h"
#include "dock.h"
#include "dockslots.h"
#include "wmspec.h"
#include "dockedapp.h"
#include "dialog.h"
#include "main.h"
#include "properties.h"
#include "menu.h"
#include "client.h"
#include "defaults.h"
#include "workspace.h"
#include "framewin.h"
#include "superfluous.h"
#include "xinerama.h"
#include "placement.h"
#include "misc.h"
#include "event.h"
#include "startup_notify.h"

/**** Local variables ****/
#define CLIP_REWIND       1
#define CLIP_IDLE         0
#define CLIP_FORWARD      2

#define MOD_MASK wdisp->modifier_mask
#define ICON_SIZE w_global.config->icon_size

/***** Local variables ****/

static WMPropList *dCommand = NULL;
static WMPropList *dPasteCommand = NULL;
#ifdef USE_DOCK_XDND
static WMPropList *dDropCommand = NULL;
#endif
static WMPropList *dAutoLaunch, *dLock;
static WMPropList *dName, *dForced, *dBuggyApplication, *dYes, *dNo;
static WMPropList *dDisplayName, *dComment;
static WMPropList *dDock, *dClip;
static WMPropList *dAutoAttractIcons;

static WMPropList *dPosition, *dApplications, *dLowered, *dCollapsed, *dEdge;

static WMPropList *dAutoCollapse, *dAutoRaiseLower, *dOmnipresent;

static WMPropList *dDrawers = NULL;

static void dockIconPaint(WAppIcon *btn);

static void iconMouseDown(WObjDescriptor *desc, xcb_generic_event_t *event);

static pid_t execCommand(WAppIcon *btn, const char *command, WSavedState *state);

static void trackDeadProcess(pid_t pid, unsigned int status, void *cdata);

static int getClipButton(int px, int py);

static void toggleLowered(WDock *dock);

static void toggleCollapsed(WDock *dock);

static void clipIconExpose(WObjDescriptor *desc, xcb_generic_event_t *event);

static void clipLeave(WDock *dock);

static void handleClipChangeWorkspace(WScreen *scr, xcb_generic_event_t *event);

static void clipEnterNotify(WObjDescriptor *desc, xcb_generic_event_t *event);
static void clipLeaveNotify(WObjDescriptor *desc, xcb_generic_event_t *event);
static void clipAutoCollapse(void *cdata);
static void clipAutoExpand(void *cdata);
static void launchDockedApplication(WAppIcon *btn, bool withSelection);

static void clipAutoLower(void *cdata);
static void clipAutoRaise(void *cdata);
static WAppIcon *mainIconCreate(WScreen *scr, int type, const char *name);

static void drawerIconExpose(WObjDescriptor *desc, xcb_generic_event_t *event);
static void removeDrawerCallback(WMenu *menu, WMenuEntry *entry);
static void drawerAppendToChain(WScreen *scr, WDock *drawer);
static char *findUniqueName(WScreen *scr, const char *instance_basename);
static void addADrawerCallback(WMenu *menu, WMenuEntry *entry);
static WDock *getDrawerAtAlong(WScreen *scr, int along);
static int indexOfHole(WDock *drawer, WAppIcon *moving_aicon, int redocking);
static void drawerConsolidateIcons(WDock *drawer);

static int onScreen(WScreen *scr, int x, int y);
static int dock_clamp_along_pixel(WScreen *scr, WDockEdge edge, int along,
				  int neg_extent, int pos_extent);
static void dock_live_span(const WDock *dock, int *neg, int *pos);

static void make_keys(void)
{
	if (dCommand != NULL)
		return;

	dCommand = WMRetainPropList(WMCreatePLString("Command"));
	dPasteCommand = WMRetainPropList(WMCreatePLString("PasteCommand"));
#ifdef USE_DOCK_XDND
	dDropCommand = WMRetainPropList(WMCreatePLString("DropCommand"));
#endif
	dLock = WMRetainPropList(WMCreatePLString("Lock"));
	dAutoLaunch = WMRetainPropList(WMCreatePLString("AutoLaunch"));
	dName = WMRetainPropList(WMCreatePLString("Name"));
	dDisplayName = WMRetainPropList(WMCreatePLString("DisplayName"));
	dComment = WMRetainPropList(WMCreatePLString("Comment"));
	dForced = WMRetainPropList(WMCreatePLString("Forced"));
	dBuggyApplication = WMRetainPropList(WMCreatePLString("BuggyApplication"));
	dYes = WMRetainPropList(WMCreatePLString("Yes"));
	dNo = WMRetainPropList(WMCreatePLString("No"));

	dPosition = WMCreatePLString("Position");
	dApplications = WMCreatePLString("Applications");
	dLowered = WMCreatePLString("Lowered");
	dCollapsed = WMCreatePLString("Collapsed");
	dEdge = WMCreatePLString("Edge");
	dAutoCollapse = WMCreatePLString("AutoCollapse");
	dAutoRaiseLower = WMCreatePLString("AutoRaiseLower");
	dAutoAttractIcons = WMCreatePLString("AutoAttractIcons");

	dOmnipresent = WMCreatePLString("Omnipresent");

	dDock = WMCreatePLString("Dock");
	dClip = WMCreatePLString("Clip");
	dDrawers = WMCreatePLString("Drawers");
}

static void renameCallback(WMenu *menu, WMenuEntry *entry)
{
	WDock *dock = entry->clientdata;
	char buffer[128];
	int wspace;
	char *name;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) menu;

	assert(entry->clientdata != NULL);

	wspace = dock->screen_ptr->current_workspace;

	name = wstrdup(dock->screen_ptr->workspaces[wspace]->name);

	snprintf(buffer, sizeof(buffer), _("Type the name for workspace %i:"), wspace + 1);
	if (wInputDialog(dock->screen_ptr, _("Rename Workspace"), buffer, &name))
		wWorkspaceRename(dock->screen_ptr, wspace, name);

	wfree(name);
}

static void toggleLoweredCallback(WMenu *menu, WMenuEntry *entry)
{
	assert(entry->clientdata != NULL);

	toggleLowered(entry->clientdata);

	entry->flags.indicator_on = !(((WDock *) entry->clientdata)->lowered);

	wMenuPaint(menu);
}

static int matchWindow(const void *item, const void *cdata)
{
	return (((WFakeGroupLeader *) item)->leader == (xcb_window_t)(uintptr_t) cdata);
}

static void killCallback(WMenu *menu, WMenuEntry *entry)
{
	WScreen *scr = menu->menu->screen_ptr;
	WAppIcon *icon;
	WFakeGroupLeader *fPtr;
	char *buffer, *shortname, **argv = NULL;
	int argc;

	if (!WCHECK_STATE(WSTATE_NORMAL))
		return;

	assert(entry->clientdata != NULL);

	icon = (WAppIcon *) entry->clientdata;

	icon->editing = 1;

	WCHANGE_STATE(WSTATE_MODAL);

	/* strip away dir names */
	if (icon->command)
		shortname = basename(icon->command);
	else if (icon->wm_instance && icon->wm_instance[0])
		shortname = icon->wm_instance;
	else
		shortname = _("application");
	/* separate out command options */
	wtokensplit(shortname, &argv, &argc);

	buffer = wstrconcat(argc > 0 ? argv[0] : shortname,
			    _(" will be forcibly closed.\n"
			      "Any unsaved changes will be lost.\n" "Please confirm."));

	if (icon->icon && icon->icon->owner) {
		fPtr = icon->icon->owner->fake_group;
	} else {
		/* is this really necessary? can we kill a non-running dock icon? */
		xcb_window_t win = icon->main_window;
		int index;

		index = WMFindInArray(scr->fakeGroupLeaders, matchWindow, (void *)(uintptr_t) win);
		if (index != WANotFound)
			fPtr = WMGetFromArray(scr->fakeGroupLeaders, index);
		else
			fPtr = NULL;
	}

	if (w_global.config->dont_confirm_kill
	    || wMessageDialog(menu->frame->screen_ptr, _("Kill Application"),
			      buffer, _("Yes"), _("No"), NULL) == WAPRDefault) {
		if (fPtr != NULL) {
			WWindow *wwin, *twin;

			wwin = scr->focused_window;
			while (wwin) {
				twin = wwin->prev;
				if (wwin->fake_group == fPtr)
					wClientKill(wwin);

				wwin = twin;
			}
		} else if (icon->icon && icon->icon->owner) {
			wClientKill(icon->icon->owner);
		}
	}

	wfree(buffer);
	wtokenfree(argv, argc);

	icon->editing = 0;

	WCHANGE_STATE(WSTATE_NORMAL);
}

/* TODO: replace this function with a member of the dock struct */
static int numberOfSelectedIcons(WDock *dock)
{
	WAppIcon *aicon;
	int i, n;

	n = 0;
	for (i = 1; i < dock->max_icons; i++) {
		aicon = dock->icon_array[i];
		if (aicon && aicon->icon->selected)
			n++;
	}

	return n;
}

static WMArray *getSelected(WDock *dock)
{
	WMArray *ret = WMCreateArray(8);
	WAppIcon *btn;
	int i;

	for (i = 1; i < dock->max_icons; i++) {
		btn = dock->icon_array[i];
		if (btn && btn->icon->selected)
			WMAddToArray(ret, btn);
	}

	return ret;
}

static void fillArrow(cairo_t *cr, WMColor *color,
		      double x0, double y0, double x1, double y1,
		      double x2, double y2)
{
	W_SetSourceColor(cr, color);
	cairo_move_to(cr, x0, y0);
	cairo_line_to(cr, x1, y1);
	cairo_line_to(cr, x2, y2);
	cairo_close_path(cr);
	cairo_fill_preserve(cr);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);
}

static void paintClipButtons(WAppIcon *clipIcon, bool lpushed, bool rpushed)
{
	xcb_window_t win = clipIcon->icon->core->window;
	WScreen *scr = clipIcon->icon->core->screen_ptr;
	int pt = CLIP_BUTTON_SIZE * ICON_SIZE / 64;
	int tp = ICON_SIZE - pt;
	int as = pt - 15;	/* 15 = 5+5+5 */
	WMColor *color;
	cairo_t *cr;

	color = scr->clip_title_color[CLIP_NORMAL];

	cr = WCairoBeginForDrawable(scr->wmscreen, win);
	if (!cr)
		return;

	/* pressed-corner white flash under the arrow */
	if (rpushed)
		fillArrow(cr, scr->white,
			  tp + 1, 1, ICON_SIZE - 2, 1, ICON_SIZE - 2, pt - 1);
	else if (lpushed)
		fillArrow(cr, scr->white,
			  1, tp, pt, ICON_SIZE - 2, 1, ICON_SIZE - 2);

	/* top right arrow */
	fillArrow(cr, rpushed ? scr->black : color,
		  ICON_SIZE - 5 - as, 5,
		  ICON_SIZE - 6, 5,
		  ICON_SIZE - 6, 4 + as);

	/* bottom left arrow */
	fillArrow(cr, lpushed ? scr->black : color,
		  5, ICON_SIZE - 5 - as,
		  5, ICON_SIZE - 6,
		  4 + as, ICON_SIZE - 6);

	WCairoEnd(cr);
}

RImage *wClipMakeTile(RImage *normalTile)
{
	RImage *tile = RCloneImage(normalTile);
	RColor black;
	RColor dark;
	RColor light;
	int pt, tp;
	int as;

	pt = CLIP_BUTTON_SIZE * w_global.config->icon_size / 64;
	tp = w_global.config->icon_size - 1 - pt;
	as = pt - 15;

	black.alpha = 255;
	black.red = black.green = black.blue = 0;

	dark.alpha = 0;
	dark.red = dark.green = dark.blue = 60;

	light.alpha = 0;
	light.red = light.green = light.blue = 80;

	/* top right */
	ROperateLine(tile, RSubtractOperation, tp, 0, w_global.config->icon_size - 2, pt - 1, &dark);
	RDrawLine(tile, tp - 1, 0, w_global.config->icon_size - 1, pt + 1, &black);
	ROperateLine(tile, RAddOperation, tp, 2, w_global.config->icon_size - 3, pt, &light);

	/* arrow bevel */
	ROperateLine(tile, RSubtractOperation, ICON_SIZE - 7 - as, 4, ICON_SIZE - 5, 4, &dark);
	ROperateLine(tile, RSubtractOperation, ICON_SIZE - 6 - as, 5, ICON_SIZE - 5, 6 + as, &dark);
	ROperateLine(tile, RAddOperation, ICON_SIZE - 5, 4, ICON_SIZE - 5, 6 + as, &light);

	/* bottom left */
	ROperateLine(tile, RAddOperation, 2, tp + 2, pt - 2, w_global.config->icon_size - 3, &dark);
	RDrawLine(tile, 0, tp - 1, pt + 1, w_global.config->icon_size - 1, &black);
	ROperateLine(tile, RSubtractOperation, 0, tp - 2, pt + 1, w_global.config->icon_size - 2, &light);

	/* arrow bevel */
	ROperateLine(tile, RSubtractOperation, 4, ICON_SIZE - 7 - as, 4, ICON_SIZE - 5, &dark);
	ROperateLine(tile, RSubtractOperation, 5, ICON_SIZE - 6 - as, 6 + as, ICON_SIZE - 5, &dark);
	ROperateLine(tile, RAddOperation, 4, ICON_SIZE - 5, 6 + as, ICON_SIZE - 5, &light);

	return tile;
}

static void omnipresentCallback(WMenu *menu, WMenuEntry *entry)
{
	WAppIcon *clickedIcon = entry->clientdata;
	WAppIcon *aicon;
	WDock *dock;
	WMArray *selectedIcons;
	WMArrayIterator iter;
	int failed;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) menu;

	assert(entry->clientdata != NULL);

	dock = clickedIcon->dock;

	selectedIcons = getSelected(dock);

	if (!WMGetArrayItemCount(selectedIcons))
		WMAddToArray(selectedIcons, clickedIcon);

	failed = 0;
	WM_ITERATE_ARRAY(selectedIcons, aicon, iter) {
		if (wClipMakeIconOmnipresent(aicon, !aicon->omnipresent) == WO_FAILED)
			failed++;
		else if (aicon->icon->selected)
			wIconSelect(aicon->icon);
	}
	WMFreeArray(selectedIcons);

	if (failed > 1) {
		wMessageDialog(dock->screen_ptr, _("Warning"),
			       _("Some icons cannot be made omnipresent. "
				 "Please make sure that no other icon is "
				 "docked in the same positions on the other "
				 "workspaces and the Clip is not full in "
				 "some workspace."), _("OK"), NULL, NULL);
	} else if (failed == 1) {
		wMessageDialog(dock->screen_ptr, _("Warning"),
			       _("Icon cannot be made omnipresent. "
				 "Please make sure that no other icon is "
				 "docked in the same position on the other "
				 "workspaces and the Clip is not full in "
				 "some workspace."), _("OK"), NULL, NULL);
	}
}

static void removeIcons(WMArray *icons, WDock *dock)
{
	WAppIcon *aicon;
	int keepit;
	WMArrayIterator it;

	WM_ITERATE_ARRAY(icons, aicon, it) {
		keepit = aicon->running && wApplicationOf(aicon->main_window);
		wDockDetach(dock, aicon);
		if (keepit) {
			PlaceIcon(dock->screen_ptr, &aicon->x_pos, &aicon->y_pos,
				wGetHeadForWindow(aicon->icon->owner));
			xcb_configure_window(wdisp->conn, aicon->icon->core->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ aicon->x_pos, aicon->y_pos });
			if (!dock->mapped || dock->collapsed)
				xcb_map_window(wdisp->conn, aicon->icon->core->window);
		}
	}
	WMFreeArray(icons);

	if (w_global.config->auto_arrange_icons)
		wArrangeIcons(dock->screen_ptr, true);
}

static void removeIconsCallback(WMenu *menu, WMenuEntry *entry)
{
	WAppIcon *clickedIcon = (WAppIcon *) entry->clientdata;
	WDock *dock;
	WMArray *selectedIcons;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) menu;

	assert(clickedIcon != NULL);

	dock = clickedIcon->dock;

	selectedIcons = getSelected(dock);

	if (WMGetArrayItemCount(selectedIcons)) {
		if (wMessageDialog(dock->screen_ptr,
					dock->type == WM_CLIP ? _("Workspace Clip") : _("Drawer"),
					_("All selected icons will be removed!"),
					_("OK"), _("Cancel"), NULL) != WAPRDefault) {
			WMFreeArray(selectedIcons);
			return;
		}
	} else {
		if (clickedIcon->xindex == 0 && clickedIcon->yindex == 0) {
			WMFreeArray(selectedIcons);
			return;
		}
		WMAddToArray(selectedIcons, clickedIcon);
	}

	removeIcons(selectedIcons, dock);

	if (dock->type == WM_DRAWER) {
		drawerConsolidateIcons(dock);
	}
}

static void keepIconsCallback(WMenu *menu, WMenuEntry *entry)
{
	WAppIcon *clickedIcon = (WAppIcon *) entry->clientdata;
	WDock *dock;
	WAppIcon *aicon;
	WMArray *selectedIcons;
	WMArrayIterator it;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) menu;

	assert(clickedIcon != NULL);
	dock = clickedIcon->dock;

	selectedIcons = getSelected(dock);

	if (!WMGetArrayItemCount(selectedIcons)
	    && clickedIcon != dock->screen_ptr->clip_icon) {
		char *command = NULL;

		if (!clickedIcon->command && !clickedIcon->editing) {
			int ok;
			bool gone;

			clickedIcon->editing = 1;
			wretain(clickedIcon);
			ok = wInputDialog(dock->screen_ptr, _("Keep Icon"),
					  _("Type the command used to launch the application"), &command);
			gone = clickedIcon->destroyed;
			if (!gone) {
				clickedIcon->editing = 0;
				if (ok)
					wAppIconSetCommand(clickedIcon, command);
			} else if (command) {
				wfree(command);
			}
			wrelease(clickedIcon);
			if (gone || !ok) {
				WMFreeArray(selectedIcons);
				return;
			}
		}

		WMAddToArray(selectedIcons, clickedIcon);
	}

	WM_ITERATE_ARRAY(selectedIcons, aicon, it) {
		if (aicon->icon->selected)
			wIconSelect(aicon->icon);

		if (aicon->attracted && aicon->command) {
			aicon->attracted = 0;
			if (aicon->icon->shadowed) {
				aicon->icon->shadowed = 0;

				/*
				 * Update icon pixmap, RImage doesn't change,
				 * so call wIconUpdate is not needed
				 */
				update_icon_pixmap(aicon->icon);

				/* Paint it */
				wAppIconPaint(aicon);
			}
		}
		save_appicon(aicon);
	}
	WMFreeArray(selectedIcons);
}

static void toggleAutoAttractCallback(WMenu *menu, WMenuEntry *entry)
{
	WDock *dock = (WDock *) entry->clientdata;
	WScreen *scr = dock->screen_ptr;

	assert(entry->clientdata != NULL);

	dock->attract_icons = !dock->attract_icons;

	entry->flags.indicator_on = dock->attract_icons;

	wMenuPaint(menu);

	if (dock->attract_icons) {
		if (dock->type == WM_DRAWER) {
			/* The newly auto-attracting dock is a drawer: disable any clip and
			 * previously attracting drawer */

			if (!wdisp->flags.noclip) {
				int i;
				for (i = 0; i < scr->workspace_count; i++)
					scr->workspaces[ i ]->clip->attract_icons = false;
					/* dock menu will be updated later, when opened */
			}

			if (scr->attracting_drawer != NULL)
				scr->attracting_drawer->attract_icons = false;
			scr->attracting_drawer = dock;
		} else {
			/* The newly auto-attracting dock is a clip: disable
			 * previously attracting drawer, if applicable */
			if (scr->attracting_drawer != NULL) {
				scr->attracting_drawer->attract_icons = false;
				/* again, its menu will be updated, later. */
				scr->attracting_drawer = NULL;
			}
		}
	}
}

static void selectCallback(WMenu *menu, WMenuEntry *entry)
{
	WAppIcon *icon = (WAppIcon *) entry->clientdata;

	assert(icon != NULL);

	wIconSelect(icon->icon);

	wMenuPaint(menu);
}

static void attractIconsCallback(WMenu *menu, WMenuEntry *entry)
{
	WAppIcon *clickedIcon = (WAppIcon *) entry->clientdata;
	WDock *clip; /* clip... is a WM_CLIP or a WM_DRAWER */
	WAppIcon *aicon;
	int x, y, x_pos, y_pos;
	bool update_icon = false;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) menu;

	assert(entry->clientdata != NULL);
	clip = clickedIcon->dock;

	aicon = clip->screen_ptr->app_icon_list;

	while (aicon) {
		if (!aicon->docked && wDockFindFreeSlot(clip, &x, &y)) {
			x_pos = clip->x_pos + x * ICON_SIZE;
			y_pos = clip->y_pos + y * ICON_SIZE;
			if (aicon->x_pos != x_pos || aicon->y_pos != y_pos)
				move_window(aicon->icon->core->window, aicon->x_pos, aicon->y_pos, x_pos, y_pos);

			aicon->attracted = 1;
			if (!aicon->icon->shadowed) {
				aicon->icon->shadowed = 1;
				update_icon = true;
			}
			wDockAttachIcon(clip, aicon, x, y, update_icon);
			if (clip->collapsed || !clip->mapped)
				xcb_unmap_window(wdisp->conn, aicon->icon->core->window);
		}
		aicon = aicon->next;
	}
}

static void selectIconsCallback(WMenu *menu, WMenuEntry *entry)
{
	WAppIcon *clickedIcon = (WAppIcon *) entry->clientdata;
	WDock *dock;
	WMArray *selectedIcons;
	WMArrayIterator iter;
	WAppIcon *btn;
	int i;

	assert(clickedIcon != NULL);
	dock = clickedIcon->dock;

	selectedIcons = getSelected(dock);

	if (!WMGetArrayItemCount(selectedIcons)) {
		for (i = 1; i < dock->max_icons; i++) {
			btn = dock->icon_array[i];
			if (btn && !btn->icon->selected)
				wIconSelect(btn->icon);
		}
	} else {
		WM_ITERATE_ARRAY(selectedIcons, btn, iter) {
			wIconSelect(btn->icon);
		}
	}
	WMFreeArray(selectedIcons);

	wMenuPaint(menu);
}

static void toggleCollapsedCallback(WMenu *menu, WMenuEntry *entry)
{
	assert(entry->clientdata != NULL);

	toggleCollapsed(entry->clientdata);

	entry->flags.indicator_on = ((WDock *) entry->clientdata)->collapsed;

	wMenuPaint(menu);
}

static void toggleAutoCollapseCallback(WMenu *menu, WMenuEntry *entry)
{
	WDock *dock;
	assert(entry->clientdata != NULL);

	dock = (WDock *) entry->clientdata;

	dock->auto_collapse = !dock->auto_collapse;

	entry->flags.indicator_on = ((WDock *) entry->clientdata)->auto_collapse;

	wMenuPaint(menu);
}

static void toggleAutoRaiseLower(WDock *dock)
{
	WDrawerChain *dc;

	dock->auto_raise_lower = !dock->auto_raise_lower;
	if (dock->type == WM_DOCK)
	{
		for (dc = dock->screen_ptr->drawers; dc != NULL; dc = dc->next) {
			toggleAutoRaiseLower(dc->adrawer);
		}
	}
}

static void toggleAutoRaiseLowerCallback(WMenu *menu, WMenuEntry *entry)
{
	WDock *dock;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) menu;

	assert(entry->clientdata != NULL);

	dock = (WDock *) entry->clientdata;

	toggleAutoRaiseLower(dock);

	entry->flags.indicator_on = ((WDock *) entry->clientdata)->auto_raise_lower;

	wMenuPaint(menu);
}

static void launchCallback(WMenu *menu, WMenuEntry *entry)
{
	WAppIcon *btn = (WAppIcon *) entry->clientdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) menu;

	launchDockedApplication(btn, false);
}

static void settingsCallback(WMenu *menu, WMenuEntry *entry)
{
	WAppIcon *btn = (WAppIcon *) entry->clientdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) menu;

	if (btn->editing)
		return;
	ShowDockAppSettingsPanel(btn);
}

static void hideCallback(WMenu *menu, WMenuEntry *entry)
{
	WApplication *wapp;
	WAppIcon *btn = (WAppIcon *) entry->clientdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) menu;

	wapp = wApplicationOf(btn->icon->owner->main_window);

	if (wapp->flags.hidden) {
		wWorkspaceChangeFromPointer(btn->icon->core->screen_ptr, wapp->last_workspace);
		wUnhideApplication(wapp, false, false);
	} else {
		wRequestHideApplication(wapp, btn->icon->owner);
	}
}

static void unhideHereCallback(WMenu *menu, WMenuEntry *entry)
{
	WApplication *wapp;
	WAppIcon *btn = (WAppIcon *) entry->clientdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) menu;

	wapp = wApplicationOf(btn->icon->owner->main_window);

	wUnhideApplication(wapp, false, true);
}

/* Anchor rect of the dock: the primary head under
 * KeepDockOnPrimaryHead, the whole root window otherwise. */
static WMRect dock_anchor_rect(WScreen *scr)
{
	if (w_global.config->keep_dock_on_primary_head)
		return wGetRectForHead(scr, scr->xine_info.primary_head);

	return wmkrect(0, 0, scr->scr_width, scr->scr_height);
}

/* Pinned coordinate of the dock's main tile for the edge, plus the
 * default free-axis coordinate. The caller holding an along-axis
 * coordinate overwrites the free axis afterwards. This is
 * getDockXPosition() generalised to (x, y). */
static void dock_origin(WScreen *scr, WDockEdge edge, int *x, int *y)
{
	wDockGeomOrigin(dock_anchor_rect(scr), edge, ICON_SIZE, DOCK_EXTRA_SPACE, x, y);
}

/* Clamp an (x, y) candidate inside the screen, then restore the pinned
 * axis for the edge so the clamp cannot pull the dock off it. While icon
 * wrapping is on, a drag that switched edges is clamped to the live run
 * so the transposed layout cannot overflow and let the wrap pass reorder
 * icons. Same-edge slides and oversized runs stay free. */
static void dock_keep_on_edge(WScreen *scr, WDockEdge edge, bool edge_switched, int *x, int *y)
{
	WMRect rect = dock_anchor_rect(scr);
	int ox, oy, neg, pos;

	dock_origin(scr, edge, &ox, &oy);
	wScreenKeepInside(scr, x, y, ICON_SIZE, ICON_SIZE);
	if (wDockEdgeHorizontal(edge))
		*y = oy;
	else
		*x = ox;
	if (!edge_switched || !wdisp->flags.wrap_appicons_in_dock)
		return;
	dock_live_span(scr->dock, &neg, &pos);
	if (wDockEdgeHorizontal(edge)) {
		if (neg + pos <= (int)rect.size.width)
			*x = dock_clamp_along_pixel(scr, edge, *x, neg, pos);
	} else {
		if (neg + pos <= (int)rect.size.height)
			*y = dock_clamp_along_pixel(scr, edge, *y, neg, pos);
	}
}

/* Clamp an along-axis pixel coordinate so a run reaching neg_extent
 * pixels behind and pos_extent pixels ahead of it stays inside the
 * anchor rect */
static int dock_clamp_along_pixel(WScreen *scr, WDockEdge edge, int along,
				  int neg_extent, int pos_extent)
{
	WMRect rect = dock_anchor_rect(scr);
	int lo, hi;

	if (wDockEdgeHorizontal(edge)) {
		lo = rect.pos.x + neg_extent;
		hi = rect.pos.x + (int)rect.size.width - pos_extent;
	} else {
		lo = rect.pos.y + neg_extent;
		hi = rect.pos.y + (int)rect.size.height - pos_extent;
	}

	if (hi < lo)
		hi = lo;

	if (along < lo)
		along = lo;
	else if (along > hi)
		along = hi;

	return along;
}

/* Which stored index is the stacking index for this edge. Vertical:
 * along is yindex, across is xindex. Horizontal: along is xindex,
 * across is yindex. */
int wDockAlongIndex(const WAppIcon *icon, WDockEdge edge)
{
	return wDockEdgeHorizontal(edge) ? icon->xindex : icon->yindex;
}

int wDockAcrossIndex(const WAppIcon *icon, WDockEdge edge)
{
	return wDockEdgeHorizontal(edge) ? icon->yindex : icon->xindex;
}

/* The screen-axis sign of an "into the screen" drawer step: Left +1 on
 * X, Right -1 on X, Top +1 on Y, Bottom -1 on Y. */
int wDockAcrossSign(WDockEdge edge)
{
	char axis;
	int sign;

	wDockGeomAcrossSign(edge, &axis, &sign);
	return sign;
}

/* Number of whole icons that fit on the anchor rect's along axis */
int wDockAlongLimit(const WDock *dock)
{
	WMRect rect = dock_anchor_rect(dock->screen_ptr);
	int len = wDockEdgeHorizontal(dock->edge) ?
		  (int)rect.size.width : (int)rect.size.height;

	return len / ICON_SIZE < 1 ? 1 : len / ICON_SIZE;
}

/* The slot reported over IPC and death handlers: the along-axis index
 * for WM_DOCK icons, yindex elsewhere. */
int wDockIconSlot(const WAppIcon *icon)
{
	if (!icon)
		return 0;

	if (icon->dock && icon->dock->type == WM_DOCK)
		return wDockAlongIndex(icon, icon->dock->edge);

	return icon->yindex;
}

void wDockInsetArea(WScreen *scr, WArea *area)
{
	int offset;

	if (!scr->dock)
		return;

	offset = ICON_SIZE + DOCK_EXTRA_SPACE;

	switch (scr->dock->edge) {
	case WDOCK_EDGE_LEFT:
		area->x1 += offset;
		break;
	case WDOCK_EDGE_RIGHT:
		area->x2 -= offset;
		break;
	case WDOCK_EDGE_TOP:
		area->y1 += offset;
		break;
	case WDOCK_EDGE_BOTTOM:
		area->y2 -= offset;
		break;
	}
}

/* Name is only used when type == WM_DRAWER and when restoring a specific
 * drawer, with a specific name. When creating a drawer, leave name to NULL
 * and mainIconCreate will find the first unused unique name */
static WAppIcon *mainIconCreate(WScreen *scr, int type, const char *name)
{
	WAppIcon *btn;
	int x_pos;

	switch(type) {
	case WM_CLIP:
		if (scr->clip_icon)
			return scr->clip_icon;

		btn = wAppIconCreateForDock(scr, NULL, "Logo", "WMClip", TILE_CLIP);
		btn->icon->core->descriptor.handle_expose = clipIconExpose;
		x_pos = 0;
		break;
	case WM_DOCK:
	default: /* to avoid a warning about btn and x_pos, basically */
		btn = wAppIconCreateForDock(scr, NULL, "Logo", "WMDock", TILE_NORMAL);
		if (wdisp->flags.clip_merged_in_dock)
			btn->icon->core->descriptor.handle_expose = clipIconExpose;
		dock_origin(scr, WDOCK_EDGE_RIGHT, &x_pos, NULL);
		break;
	case WM_DRAWER:
		if (name == NULL)
			name = findUniqueName(scr, "Drawer");
		btn = wAppIconCreateForDock(scr, NULL, name, "WMDrawer", TILE_DRAWER);
		btn->icon->core->descriptor.handle_expose = drawerIconExpose;
		x_pos = 0;
	}

	btn->xindex = 0;
	btn->yindex = 0;

	btn->icon->core->descriptor.handle_mousedown = iconMouseDown;
	btn->icon->core->descriptor.handle_enternotify = clipEnterNotify;
	btn->icon->core->descriptor.handle_leavenotify = clipLeaveNotify;
	btn->icon->core->descriptor.parent_type = WCLASS_DOCK_ICON;
	btn->icon->core->descriptor.parent = btn;
	xcb_map_window(wdisp->conn, btn->icon->core->window);
	btn->x_pos = x_pos;
	btn->y_pos = 0;
	btn->docked = 1;
	if (type == WM_CLIP ||
		(type == WM_DOCK && wdisp->flags.clip_merged_in_dock))
		scr->clip_icon = btn;

	return btn;
}

static void switchWSCommand(WMenu *menu, WMenuEntry *entry)
{
	WAppIcon *btn, *icon = (WAppIcon *) entry->clientdata;
	WScreen *scr = icon->icon->core->screen_ptr;
	WDock *src, *dest;
	WMArray *selectedIcons;
	int x, y;
	int i;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) menu;

	if (entry->order == scr->current_workspace)
		return;

	src = icon->dock;
	dest = scr->workspaces[entry->order]->clip;

	selectedIcons = getSelected(src);

	if (WMGetArrayItemCount(selectedIcons)) {
		WMArrayIterator iter;

		WM_ITERATE_ARRAY(selectedIcons, btn, iter) {
			if (wDockFindFreeSlot(dest, &x, &y)) {
				wDockMoveIconBetweenDocks(src, dest, btn, x, y);
				xcb_unmap_window(wdisp->conn, btn->icon->core->window);
			}
		}
	} else if (icon != scr->clip_icon) {
		if (wDockFindFreeSlot(dest, &x, &y)) {
			wDockMoveIconBetweenDocks(src, dest, icon, x, y);
			xcb_unmap_window(wdisp->conn, icon->icon->core->window);
		}
	} else {
		for (i = 1; i < src->max_icons; i++) {
			btn = src->icon_array[i];
			if (btn && wDockFindFreeSlot(dest, &x, &y)) {
				wDockMoveIconBetweenDocks(src, dest, btn, x, y);
				xcb_unmap_window(wdisp->conn, btn->icon->core->window);
			}
		}
	}
	WMFreeArray(selectedIcons);
}

static void launchDockedApplication(WAppIcon *btn, bool withSelection)
{
	WScreen *scr = btn->icon->core->screen_ptr;

	if (!btn->launching &&
	    ((!withSelection && btn->command != NULL) || (withSelection && btn->paste_command != NULL))) {
		if (!btn->forced_dock) {
			btn->relaunching = btn->running;
			btn->running = 1;
		}
		if (btn->wm_instance || btn->wm_class) {
			WWindowAttributes attr;
			memset(&attr, 0, sizeof(WWindowAttributes));
			wDefaultFillAttributes(btn->wm_instance, btn->wm_class, &attr, NULL, true);

			if (!attr.no_appicon && !btn->buggy_app)
				btn->launching = 1;
			else
				btn->running = 0;
		}
		btn->drop_launch = 0;
		btn->paste_launch = withSelection;
#ifdef USE_DOCK_XDND
		scr->xdest_is_gnustep =
			(withSelection && btn->wm_class && strcmp(btn->wm_class, "GNUstep") == 0);
#endif
		scr->last_dock = btn->dock;
		btn->pid = execCommand(btn, (withSelection ? btn->paste_command : btn->command), NULL);
#ifdef USE_DOCK_XDND
		scr->xdest_is_gnustep = 0;
#endif
		if (btn->pid > 0) {
			if (btn->buggy_app) {
				/* give feedback that the app was launched */
				btn->launching = 1;
				dockIconPaint(btn);
				btn->launching = 0;
				WMAddTimerHandler(200, (WMCallback *) dockIconPaint, btn);
			} else {
				dockIconPaint(btn);
			}
		} else {
			wwarning(_("could not launch application %s"), btn->command);
			btn->launching = 0;
			if (!btn->relaunching)
				btn->running = 0;
		}
	}
}

static void updateWorkspaceMenu(WMenu *menu, WAppIcon *icon)
{
	WScreen *scr;
	int i;

	if (!menu || !icon)
		return;

	scr = menu->frame->screen_ptr;

	for (i = 0; i < scr->workspace_count; i++) {
		if (i < menu->entry_no) {
			if (strcmp(menu->entries[i]->text, scr->workspaces[i]->name) != 0) {
				wfree(menu->entries[i]->text);
				menu->entries[i]->text = wstrdup(scr->workspaces[i]->name);
				menu->flags.realized = 0;
			}
			menu->entries[i]->clientdata = (void *)icon;
		} else {
			wMenuAddCallback(menu, scr->workspaces[i]->name, switchWSCommand, (void *)icon);

			menu->flags.realized = 0;
		}

		if (i == scr->current_workspace)
			wMenuSetEnabled(menu, i, false);
		else
			wMenuSetEnabled(menu, i, true);
	}

	if (!menu->flags.realized)
		wMenuRealize(menu);
}

static WMenu *makeWorkspaceMenu(WScreen *scr)
{
	WMenu *menu;

	menu = wMenuCreate(scr, NULL, false);
	if (!menu) {
		wwarning(_("could not create workspace submenu for Clip menu"));
		return NULL;
	}

	wMenuAddCallback(menu, "", switchWSCommand, (void *)scr->clip_icon);

	menu->flags.realized = 0;
	wMenuRealize(menu);

	return menu;
}

static void updateClipOptionsMenu(WMenu *menu, WDock *dock)
{
	WMenuEntry *entry;
	int index = 0;

	if (!menu || !dock)
		return;

	/* keep on top */
	entry = menu->entries[index];
	entry->flags.indicator_on = !dock->lowered;
	entry->clientdata = dock;
	wMenuSetEnabled(menu, index, dock->type == WM_CLIP);

	/* collapsed */
	entry = menu->entries[++index];
	entry->flags.indicator_on = dock->collapsed;
	entry->clientdata = dock;

	/* auto-collapse */
	entry = menu->entries[++index];
	entry->flags.indicator_on = dock->auto_collapse;
	entry->clientdata = dock;

	/* auto-raise/lower */
	entry = menu->entries[++index];
	entry->flags.indicator_on = dock->auto_raise_lower;
	entry->clientdata = dock;
	wMenuSetEnabled(menu, index, dock->lowered && (dock->type == WM_CLIP));

	/* attract icons */
	entry = menu->entries[++index];
	entry->flags.indicator_on = dock->attract_icons;
	entry->clientdata = dock;

	menu->flags.realized = 0;
	wMenuRealize(menu);
}


static WMenu *makeClipOptionsMenu(WScreen *scr)
{
	WMenu *menu;
	WMenuEntry *entry;

	menu = wMenuCreate(scr, NULL, false);
	if (!menu) {
		wwarning(_("could not create options submenu for Clip menu"));
		return NULL;
	}

	entry = wMenuAddCallback(menu, _("Keep on Top"), toggleLoweredCallback, NULL);
	entry->flags.indicator = 1;
	entry->flags.indicator_on = 1;
	entry->flags.indicator_type = MI_CHECK;

	entry = wMenuAddCallback(menu, _("Collapsed"), toggleCollapsedCallback, NULL);
	entry->flags.indicator = 1;
	entry->flags.indicator_on = 1;
	entry->flags.indicator_type = MI_CHECK;

	entry = wMenuAddCallback(menu, _("Autocollapse"), toggleAutoCollapseCallback, NULL);
	entry->flags.indicator = 1;
	entry->flags.indicator_on = 1;
	entry->flags.indicator_type = MI_CHECK;

	entry = wMenuAddCallback(menu, _("Autoraise"), toggleAutoRaiseLowerCallback, NULL);
	entry->flags.indicator = 1;
	entry->flags.indicator_on = 1;
	entry->flags.indicator_type = MI_CHECK;

	entry = wMenuAddCallback(menu, _("Autoattract Icons"), toggleAutoAttractCallback, NULL);
	entry->flags.indicator = 1;
	entry->flags.indicator_on = 1;
	entry->flags.indicator_type = MI_CHECK;

	menu->flags.realized = 0;
	wMenuRealize(menu);

	return menu;
}


static void setDockPositionNormalCallback(WMenu *menu, WMenuEntry *entry)
{
	WDock *dock = (WDock *) entry->clientdata;
	WDrawerChain *dc;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) menu;

	if (entry->flags.indicator_on) // already set, nothing to do
		return;
	// Do we come from auto raise lower or keep on top?
	if (dock->auto_raise_lower)
	{
		dock->auto_raise_lower = 0;
		// Only for aesthetic purposes, can be removed when Autoraise status is no longer exposed in drawer option menu
		for (dc = dock->screen_ptr->drawers; dc != NULL; dc = dc->next) {
			dc->adrawer->auto_raise_lower = 0;
		}
	}
	else
	{
		// Will take care of setting lowered = 0 in drawers
		toggleLowered(dock);
	}
	entry->flags.indicator_on = 1;
}

static void setDockPositionAutoRaiseLowerCallback(WMenu *menu, WMenuEntry *entry)
{
	WDock *dock = (WDock *) entry->clientdata;
	WDrawerChain *dc;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) menu;

	if (entry->flags.indicator_on) // already set, nothing to do
		return;
	// Do we come from normal or keep on top?
	if (!dock->lowered)
	{
		toggleLowered(dock);
	}
	dock->auto_raise_lower = 1;
	// Only for aesthetic purposes, can be removed when Autoraise status is no longer exposed in drawer option menu
	for (dc = dock->screen_ptr->drawers; dc != NULL; dc = dc->next) {
		dc->adrawer->auto_raise_lower = 1;
	}
	entry->flags.indicator_on = 1;
}

static void setDockPositionKeepOnTopCallback(WMenu *menu, WMenuEntry *entry)
{
	WDock *dock = (WDock *) entry->clientdata;
	WDrawerChain *dc;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) menu;

	if (entry->flags.indicator_on) // already set, nothing to do
		return;
	dock->auto_raise_lower = 0;
	// Only for aesthetic purposes, can be removed when Autoraise status is no longer exposed in drawer option menu
	for (dc = dock->screen_ptr->drawers; dc != NULL; dc = dc->next) {
		dc->adrawer->auto_raise_lower = 0;
	}
	toggleLowered(dock);
	entry->flags.indicator_on = 1;
}

static void updateDockPositionMenu(WMenu *menu, WDock *dock)
{
	WMenuEntry *entry;
	int index = 0;

	assert(menu);
	assert(dock);

	/* Normal level */
	entry = menu->entries[index++];
	entry->flags.indicator_on = (dock->lowered && !dock->auto_raise_lower);
	entry->clientdata = dock;

	/* Auto-raise/lower */
	entry = menu->entries[index++];
	entry->flags.indicator_on = dock->auto_raise_lower;
	entry->clientdata = dock;

	/* Keep on top */
	entry = menu->entries[index++];
	entry->flags.indicator_on = !dock->lowered;
	entry->clientdata = dock;
}

static WMenu *makeDockPositionMenu(WScreen *scr)
{
	/* When calling this, the dock is being created, so scr->dock is still not set
	 * Therefore the callbacks' clientdata and the indicators can't be set,
	 * they will be updated when the dock menu is opened. */
	WMenu *menu;
	WMenuEntry *entry;

	menu = wMenuCreate(scr, NULL, false);
	if (!menu) {
		wwarning(_("could not create options submenu for dock position menu"));
		return NULL;
	}

	entry = wMenuAddCallback(menu, _("Normal"), setDockPositionNormalCallback, NULL);
	entry->flags.indicator = 1;
	entry->flags.indicator_type = MI_DIAMOND;

	entry = wMenuAddCallback(menu, _("Auto raise & lower"), setDockPositionAutoRaiseLowerCallback, NULL);
	entry->flags.indicator = 1;
	entry->flags.indicator_type = MI_DIAMOND;

	entry = wMenuAddCallback(menu, _("Keep on Top"), setDockPositionKeepOnTopCallback, NULL);
	entry->flags.indicator = 1;
	entry->flags.indicator_type = MI_DIAMOND;

	menu->flags.realized = 0;
	wMenuRealize(menu);

	return menu;
}


static WMenu *dockMenuCreate(WScreen *scr, int type)
{
	WMenu *menu;
	WMenuEntry *entry;

	if (type == WM_CLIP && scr->clip_menu)
		return scr->clip_menu;

	if (type == WM_DRAWER && scr->drawer_menu)
		return scr->drawer_menu;

	menu = wMenuCreate(scr, NULL, false);
	if (type == WM_DOCK) {
		entry = wMenuAddCallback(menu, _("Dock position"), NULL, NULL);
		if (scr->dock_pos_menu == NULL)
			scr->dock_pos_menu = makeDockPositionMenu(scr);
		wMenuEntrySetCascade(menu, entry, scr->dock_pos_menu);

		if (!wdisp->flags.nodrawer)
			wMenuAddCallback(menu, _("Add a drawer"), addADrawerCallback, NULL);

	} else {
		if (type == WM_CLIP)
			entry = wMenuAddCallback(menu, _("Clip Options"), NULL, NULL);
		else /* if (type == WM_DRAWER) */
			entry = wMenuAddCallback(menu, _("Drawer options"), NULL, NULL);

		if (scr->clip_options == NULL)
			scr->clip_options = makeClipOptionsMenu(scr);

		wMenuEntrySetCascade(menu, entry, scr->clip_options);

		 /* The same menu is used for the dock and its appicons. If the menu
		  * entry text is different between the two contexts, or if it can
		  * change depending on some state, free the duplicated string (from
		  * wMenuInsertCallback) and use gettext's string */
		if (type == WM_CLIP) {
			entry = wMenuAddCallback(menu, _("Rename Workspace"), renameCallback, NULL);
			wfree(entry->text);
			entry->text = _("Rename Workspace"); /* can be: (Toggle) Omnipresent */
		}

		entry = wMenuAddCallback(menu, _("Selected"), selectCallback, NULL);
		entry->flags.indicator = 1;
		entry->flags.indicator_on = 1;
		entry->flags.indicator_type = MI_CHECK;

		entry = wMenuAddCallback(menu, _("Select All Icons"), selectIconsCallback, NULL);
		wfree(entry->text);
		entry->text = _("Select All Icons"); /* can be: Unselect all icons */

		entry = wMenuAddCallback(menu, _("Keep Icon"), keepIconsCallback, NULL);
		wfree(entry->text);
		entry->text = _("Keep Icon"); /* can be: Keep Icons */

		if (type == WM_CLIP) {
			entry = wMenuAddCallback(menu, _("Move Icon To"), NULL, NULL);
			wfree(entry->text);
			entry->text = _("Move Icon To"); /* can be: Move Icons to */
			scr->clip_submenu = makeWorkspaceMenu(scr);
			if (scr->clip_submenu)
				wMenuEntrySetCascade(menu, entry, scr->clip_submenu);
		}

		entry = wMenuAddCallback(menu, _("Remove Icon"), removeIconsCallback, NULL);
		wfree(entry->text);
		entry->text = _("Remove Icon"); /* can be: Remove Icons */

		wMenuAddCallback(menu, _("Attract Icons"), attractIconsCallback, NULL);
	}

	wMenuAddCallback(menu, _("Launch"), launchCallback, NULL);

	entry = wMenuAddCallback(menu, _("Unhide Here"), unhideHereCallback, NULL);
	wfree(entry->text);
	entry->text = _("Unhide Here"); /* can be: Bring Here */

	entry = wMenuAddCallback(menu, _("Hide"), hideCallback, NULL);
	wfree(entry->text);
	entry->text = _("Hide"); /* can be: Unhide */

	wMenuAddCallback(menu, _("Settings..."), settingsCallback, NULL);

	entry = wMenuAddCallback(menu, _("Kill"), killCallback, NULL);
	wfree(entry->text);
	entry->text = _("Kill"); /* can be: Remove drawer */

	if (type == WM_CLIP)
		scr->clip_menu = menu;

	if (type == WM_DRAWER)
		scr->drawer_menu = menu;

	return menu;
}

/* Capacity of a dock or drawer: the widest screen axis in whole
 * icons, so a dock that survives a resolution change or an edge
 * switch keeps every icon in the array. */
static int dock_capacity(WScreen *scr)
{
	int cap = WMAX(scr->scr_width, scr->scr_height) / w_global.config->icon_size;

	return cap < 1 ? 1 : cap;
}

/* Grow the icon array to hold at least `need` slots. Shrinking never
 * happens, matching the existing RandR behaviour. */
static void dock_grow_array(WDock *dock, int need)
{
	if (need <= dock->max_icons)
		return;

	dock->icon_array = wrealloc(dock->icon_array, sizeof(WAppIcon *) * need);
	memset(dock->icon_array + dock->max_icons, 0,
	       (need - dock->max_icons) * sizeof(WAppIcon *));
	dock->max_icons = need;
}

WDock *wDockCreate(WScreen *scr, int type, const char *name)
{
	WDock *dock;
	WAppIcon *btn;

	make_keys();

	dock = wmalloc(sizeof(WDock));

	switch (type) {
	case WM_CLIP:
		dock->max_icons = DOCK_MAX_ICONS;
		break;
	case WM_DRAWER:
	case WM_DOCK:
	default:
		/* the widest axis, so an edge switch never needs a
		 * realloc in the drag loop */
		dock->max_icons = dock_capacity(scr);
	}

	/* a tiny screen or a large icon_size can yield 0, which
	 * wmalloc(sizeof(*) * 0) trips assert(size > 0) on */
	if (dock->max_icons < 1)
		dock->max_icons = 1;

	dock->icon_array = wmalloc(sizeof(WAppIcon *) * dock->max_icons);

	btn = mainIconCreate(scr, type, name);

	btn->dock = dock;

	dock->x_pos = btn->x_pos;
	dock->y_pos = btn->y_pos;
	dock->screen_ptr = scr;
	dock->type = type;
	dock->icon_count = 1;
	if (type == WM_DRAWER)
		dock->edge = scr->dock->edge;
	else
		dock->edge = WDOCK_EDGE_RIGHT;
	dock->collapsed = 0;
	dock->auto_collapse = 0;
	dock->auto_collapse_magic = NULL;
	dock->auto_raise_lower = 0;
	dock->auto_lower_magic = NULL;
	dock->auto_raise_magic = NULL;
	dock->attract_icons = 0;
	dock->lowered = 1;
	dock->icon_array[0] = btn;
	wRaiseFrame(btn->icon->core);
	xcb_configure_window(wdisp->conn, btn->icon->core->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ btn->x_pos, btn->y_pos });

	/* create dock menu */
	dock->menu = dockMenuCreate(scr, type);

	if (type == WM_DRAWER) {
		drawerAppendToChain(scr, dock);
		dock->auto_collapse = 1;
		update_icon_pixmap(dock->icon_array[0]->icon);
	}

	return dock;
}

void wDockDestroy(WDock *dock)
{
	int i;
	WAppIcon *aicon;

	for (i = (dock->type == WM_CLIP) ? 1 : 0; i < dock->max_icons; i++) {
		aicon = dock->icon_array[i];
		if (aicon) {
			int keepit = aicon->running && wApplicationOf(aicon->main_window);
			wDockDetach(dock, aicon);
			if (keepit) {
				PlaceIcon(dock->screen_ptr, &aicon->x_pos, &aicon->y_pos,
					  wGetHeadForWindow(aicon->icon->owner));
				xcb_configure_window(wdisp->conn, aicon->icon->core->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ aicon->x_pos, aicon->y_pos });
				if (!dock->mapped || dock->collapsed)
					xcb_map_window(wdisp->conn, aicon->icon->core->window);
			}
		}
	}
	if (w_global.config->auto_arrange_icons)
		wArrangeIcons(dock->screen_ptr, true);
	wfree(dock->icon_array);
	if (dock->menu && dock->type != WM_CLIP)
		wMenuDestroy(dock->menu, true);
	if (dock->screen_ptr->last_dock == dock)
		dock->screen_ptr->last_dock = NULL;
	wfree(dock);
}

void wClipIconPaint(WAppIcon *aicon)
{
	WScreen *scr = aicon->icon->core->screen_ptr;
	WWorkspace *workspace = scr->workspaces[scr->current_workspace];
	WMColor *color;
	xcb_window_t win = aicon->icon->core->window;
	int length, nlength;
	char *ws_name, ws_number[sizeof scr->current_workspace * CHAR_BIT / 3 + 1];
	int ty, tx;

	wIconPaint(aicon->icon);

	length = strlen(workspace->name);
	ws_name = wmalloc(length + 1);
	snprintf(ws_name, length + 1, "%s", workspace->name);
	snprintf(ws_number, sizeof ws_number, "%u", (unsigned int)(scr->current_workspace + 1));
	nlength = strlen(ws_number);

	if (wdisp->flags.noclip || !workspace->clip->collapsed)
		color = scr->clip_title_color[CLIP_NORMAL];
	else
		color = scr->clip_title_color[CLIP_COLLAPSED];

	ty = ICON_SIZE - WSCALE(scr, WMFontHeight(scr->clip_title_font)) - 3;

	tx = CLIP_BUTTON_SIZE * ICON_SIZE / 64;

	if(w_global.config->show_clip_title)
		WMDrawString(scr->wmscreen, win, color, scr->clip_title_font, tx, ty, ws_name, length);

	tx = (ICON_SIZE / 2 - WSCALE(scr, WMWidthOfString(scr->clip_title_font, ws_number, nlength))) / 2;

	WMDrawString(scr->wmscreen, win, color, scr->clip_title_font, tx, 2, ws_number, nlength);

	wfree(ws_name);

	if (aicon->launching)
		wAppIconPaintLaunching(scr, aicon->icon->core->window);

	paintClipButtons(aicon, aicon->dock->lclip_button_pushed, aicon->dock->rclip_button_pushed);
}

static void clipIconExpose(WObjDescriptor *desc, xcb_generic_event_t *event)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) desc;
	(void) event;

	wClipIconPaint(desc->parent);
}

static void dockIconPaint(WAppIcon *btn)
{
	if (btn == btn->icon->core->screen_ptr->clip_icon) {
		wClipIconPaint(btn);
	} else if (wIsADrawer(btn)) {
		wDrawerIconPaint(btn);
	} else {
		wAppIconPaint(btn);
		save_appicon(btn);
	}
}

static WMPropList *make_icon_state(WAppIcon *btn)
{
	WMPropList *node = NULL;
	WMPropList *command, *autolaunch, *lock, *name, *forced;
	WMPropList *position, *buggy, *omnipresent;
	char *tmp;
	char buffer[64];

	if (btn) {
		if (!btn->command)
			command = WMCreatePLString("-");
		else
			command = WMCreatePLString(btn->command);

		autolaunch = btn->auto_launch ? dYes : dNo;

		lock = btn->lock ? dYes : dNo;

		tmp = EscapeWM_CLASS(btn->wm_instance, btn->wm_class);

		name = WMCreatePLString(tmp);

		wfree(tmp);

		forced = btn->forced_dock ? dYes : dNo;

		buggy = btn->buggy_app ? dYes : dNo;

		if (!wdisp->flags.clip_merged_in_dock && btn == btn->icon->core->screen_ptr->clip_icon)
			snprintf(buffer, sizeof(buffer), "%i,%i", btn->x_pos, btn->y_pos);
		else
			snprintf(buffer, sizeof(buffer), "%hi,%hi", btn->xindex, btn->yindex);
		position = WMCreatePLString(buffer);

		node = WMCreatePLDictionary(dCommand, command,
					    dName, name,
					    dAutoLaunch, autolaunch,
					    dLock, lock,
					    dForced, forced, dBuggyApplication, buggy, dPosition, position, NULL);
		WMReleasePropList(command);
		WMReleasePropList(name);
		WMReleasePropList(position);

		omnipresent = btn->omnipresent ? dYes : dNo;
		if (btn->dock != btn->icon->core->screen_ptr->dock && (btn->xindex != 0 || btn->yindex != 0))
			WMPutInPLDictionary(node, dOmnipresent, omnipresent);

#ifdef USE_DOCK_XDND
		if (btn->dnd_command) {
			command = WMCreatePLString(btn->dnd_command);
			WMPutInPLDictionary(node, dDropCommand, command);
			WMReleasePropList(command);
		}
#endif	/* USE_DOCK_XDND */

		if (btn->paste_command) {
			command = WMCreatePLString(btn->paste_command);
			WMPutInPLDictionary(node, dPasteCommand, command);
			WMReleasePropList(command);
		}

		if (btn->display_name) {
			command = WMCreatePLString(btn->display_name);
			WMPutInPLDictionary(node, dDisplayName, command);
			WMReleasePropList(command);
		}

		if (btn->comment) {
			command = WMCreatePLString(btn->comment);
			WMPutInPLDictionary(node, dComment, command);
			WMReleasePropList(command);
		}
	}

	return node;
}

static WMPropList *dockSaveState(WDock *dock)
{
	int i;
	WMPropList *icon_info;
	WMPropList *list = NULL, *dock_state = NULL;
	WMPropList *value, *key;
	char buffer[256];

	list = WMCreatePLArray(NULL);

	for (i = (dock->type == WM_DOCK ? 0 : 1); i < dock->max_icons; i++) {
		WAppIcon *btn = dock->icon_array[i];

		if (!btn || btn->attracted)
			continue;

		icon_info = make_icon_state(dock->icon_array[i]);
		if (icon_info != NULL) {
			WMAddToPLArray(list, icon_info);
			WMReleasePropList(icon_info);
		}
	}

	dock_state = WMCreatePLDictionary(dApplications, list, NULL);

	if (dock->type == WM_DOCK) {
		snprintf(buffer, sizeof(buffer), "Applications%i", dock->screen_ptr->scr_height);
		key = WMCreatePLString(buffer);
		WMPutInPLDictionary(dock_state, key, list);
		WMReleasePropList(key);

		if (wDockEdgeHorizontal(dock->edge)) {
			snprintf(buffer, sizeof(buffer), "ApplicationsW%i",
				 dock->screen_ptr->scr_width);
			key = WMCreatePLString(buffer);
			WMPutInPLDictionary(dock_state, key, list);
			WMReleasePropList(key);
		}

		value = wDockEdgeName(dock->edge) ? WMCreatePLString(wDockEdgeName(dock->edge)) : NULL;
		if (value) {
			WMPutInPLDictionary(dock_state, dEdge, value);
			WMReleasePropList(value);
		}

		snprintf(buffer, sizeof(buffer), "%i,%i", dock->x_pos, dock->y_pos);
		value = WMCreatePLString(buffer);
		WMPutInPLDictionary(dock_state, dPosition, value);
		WMReleasePropList(value);
	}
	WMReleasePropList(list);

	if (dock->type == WM_CLIP || dock->type == WM_DRAWER) {
		value = (dock->collapsed ? dYes : dNo);
		WMPutInPLDictionary(dock_state, dCollapsed, value);

		value = (dock->auto_collapse ? dYes : dNo);
		WMPutInPLDictionary(dock_state, dAutoCollapse, value);

		value = (dock->attract_icons ? dYes : dNo);
		WMPutInPLDictionary(dock_state, dAutoAttractIcons, value);
	}

	if (dock->type == WM_DOCK || dock->type == WM_CLIP) {
		value = (dock->lowered ? dYes : dNo);
		WMPutInPLDictionary(dock_state, dLowered, value);

		value = (dock->auto_raise_lower ? dYes : dNo);
		WMPutInPLDictionary(dock_state, dAutoRaiseLower, value);
	}

	return dock_state;
}

void wDockSaveState(WScreen *scr, WMPropList *old_state)
{
	WMPropList *dock_state;
	WMPropList *keys;

	dock_state = dockSaveState(scr->dock);

	/*
	 * Copy saved states of docks with different sizes.
	 */
	if (old_state) {
		int i;
		WMPropList *tmp;

		keys = WMGetPLDictionaryKeys(old_state);
		for (i = 0; i < WMGetPropListItemCount(keys); i++) {
			tmp = WMGetFromPLArray(keys, i);

			if (strncasecmp(WMGetFromPLString(tmp), "applications", 12) == 0
			    && !WMGetFromPLDictionary(dock_state, tmp)) {

				WMPutInPLDictionary(dock_state, tmp, WMGetFromPLDictionary(old_state, tmp));
			}
		}
		WMReleasePropList(keys);
	}

	WMPutInPLDictionary(scr->session_state, dDock, dock_state);

	WMReleasePropList(dock_state);
}

void wClipSaveState(WScreen *scr)
{
	WMPropList *clip_state;

	clip_state = make_icon_state(scr->clip_icon);

	WMPutInPLDictionary(scr->session_state, dClip, clip_state);

	WMReleasePropList(clip_state);
}

WMPropList *wClipSaveWorkspaceState(WScreen *scr, int workspace)
{
	return dockSaveState(scr->workspaces[workspace]->clip);
}

static bool getBooleanDockValue(WMPropList *value, WMPropList *key)
{
	if (value) {
		if (WMIsPLString(value)) {
			if (strcasecmp(WMGetFromPLString(value), "YES") == 0)
				return true;
		} else {
			wwarning(_("bad value in docked icon state info %s"), WMGetFromPLString(key));
		}
	}
	return false;
}

static WAppIcon *restore_icon_state(WScreen *scr, WMPropList *info, int type,
				    int index, WDockEdge dock_edge)
{
	WAppIcon *aicon;
	WMPropList *cmd, *value;
	char *wclass, *winstance, *command;

	cmd = WMGetFromPLDictionary(info, dCommand);
	if (!cmd || !WMIsPLString(cmd))
		return NULL;

	/* parse window name */
	value = WMGetFromPLDictionary(info, dName);
	if (!value)
		return NULL;

	ParseWindowName(value, &winstance, &wclass, "dock");

	if (!winstance && !wclass)
		return NULL;

	/* get commands */
	command = wstrdup(WMGetFromPLString(cmd));

	if (strcmp(command, "-") == 0) {
		wfree(command);

		if (wclass)
			wfree(wclass);
		if (winstance)
			wfree(winstance);

		return NULL;
	}

	aicon = wAppIconCreateForDock(scr, command, winstance, wclass, TILE_NORMAL);
	if (wclass)
		wfree(wclass);
	if (winstance)
		wfree(winstance);

	wfree(command);

	aicon->icon->core->descriptor.handle_mousedown = iconMouseDown;
	aicon->icon->core->descriptor.handle_enternotify = clipEnterNotify;
	aicon->icon->core->descriptor.handle_leavenotify = clipLeaveNotify;
	aicon->icon->core->descriptor.parent_type = WCLASS_DOCK_ICON;
	aicon->icon->core->descriptor.parent = aicon;

#ifdef USE_DOCK_XDND
	cmd = WMGetFromPLDictionary(info, dDropCommand);
	if (cmd)
		aicon->dnd_command = wstrdup(WMGetFromPLString(cmd));
#endif

	cmd = WMGetFromPLDictionary(info, dPasteCommand);
	if (cmd)
		aicon->paste_command = wstrdup(WMGetFromPLString(cmd));

	cmd = WMGetFromPLDictionary(info, dDisplayName);
	if (cmd && WMIsPLString(cmd))
		aicon->display_name = wstrdup(WMGetFromPLString(cmd));

	cmd = WMGetFromPLDictionary(info, dComment);
	if (cmd && WMIsPLString(cmd))
		aicon->comment = wstrdup(WMGetFromPLString(cmd));

	/* check auto launch */
	value = WMGetFromPLDictionary(info, dAutoLaunch);

	aicon->auto_launch = getBooleanDockValue(value, dAutoLaunch);

	/* check lock */
	value = WMGetFromPLDictionary(info, dLock);

	aicon->lock = getBooleanDockValue(value, dLock);

	/* check if it wasn't normally docked */
	value = WMGetFromPLDictionary(info, dForced);

	aicon->forced_dock = getBooleanDockValue(value, dForced);

	/* check if we can rely on the stuff in the app */
	value = WMGetFromPLDictionary(info, dBuggyApplication);

	aicon->buggy_app = getBooleanDockValue(value, dBuggyApplication);

	/* get position in the dock */
	value = WMGetFromPLDictionary(info, dPosition);
	if (value && WMIsPLString(value)) {
		if (sscanf(WMGetFromPLString(value), "%hi,%hi", &aicon->xindex, &aicon->yindex) != 2)
			wwarning(_("bad value in docked icon state info %s"), WMGetFromPLString(dPosition));

		/* check position sanity */
		/* *Very* incomplete section! */
		if (type == WM_DOCK) {
			/* dock icons stack on the along axis, the
			 * across index is always zero */
			if (wDockEdgeHorizontal(dock_edge))
				aicon->yindex = 0;
			else
				aicon->xindex = 0;
		}
	} else {
		aicon->yindex = index;
		aicon->xindex = 0;
	}

	/* check if icon is omnipresent */
	value = WMGetFromPLDictionary(info, dOmnipresent);

	aicon->omnipresent = getBooleanDockValue(value, dOmnipresent);

	aicon->running = 0;
	aicon->docked = 1;

	return aicon;
}

#define COMPLAIN(key) wwarning(_("bad value in dock state info:%s"), key)

WAppIcon *wClipRestoreState(WScreen *scr, WMPropList *clip_state)
{
	WAppIcon *icon;
	WMPropList *value;

	icon = mainIconCreate(scr, WM_CLIP, NULL);

	if (!clip_state)
		return icon;

	WMRetainPropList(clip_state);

	/* restore position */

	value = WMGetFromPLDictionary(clip_state, dPosition);

	if (value) {
		if (!WMIsPLString(value)) {
			COMPLAIN("Position");
		} else {
			if (sscanf(WMGetFromPLString(value), "%i,%i", &icon->x_pos, &icon->y_pos) != 2)
				COMPLAIN("Position");

			/* check position sanity */
			if (!onScreen(scr, icon->x_pos, icon->y_pos))
				wScreenKeepInside(scr, &icon->x_pos, &icon->y_pos, ICON_SIZE, ICON_SIZE);
		}
	}
#ifdef USE_DOCK_XDND
	value = WMGetFromPLDictionary(clip_state, dDropCommand);
	if (value && WMIsPLString(value))
		icon->dnd_command = wstrdup(WMGetFromPLString(value));
#endif

	value = WMGetFromPLDictionary(clip_state, dPasteCommand);
	if (value && WMIsPLString(value))
		icon->paste_command = wstrdup(WMGetFromPLString(value));

	WMReleasePropList(clip_state);

	return icon;
}

WDock *wDockRestoreState(WScreen *scr, WMPropList *dock_state, int type)
{
	WDock *dock;
	WMPropList *apps;
	WMPropList *value;
	WMPropList *tmp;
	WAppIcon *aicon, *old_top;
	bool have_edge = false;
	int count, i;
	char buffer[64];

	dock = wDockCreate(scr, type, NULL);

	if (!dock_state)
		return dock;

	WMRetainPropList(dock_state);

	/* restore the dock edge. A known name wins, an unknown one warns
	 * and keeps the default so a typo cannot move the dock */
	value = WMGetFromPLDictionary(dock_state, dEdge);
	if (value && WMIsPLString(value)) {
		WDockEdge edge;

		if (wDockEdgeFromName(WMGetFromPLString(value), &edge)) {
			dock->edge = edge;
			have_edge = true;
		} else {
			wwarning(_("unknown dock edge \"%s\" in state info, keeping the current one"),
				 WMGetFromPLString(value));
		}
	}

	/* restore position */
	value = WMGetFromPLDictionary(dock_state, dPosition);
	if (value) {
		if (!WMIsPLString(value)) {
			COMPLAIN("Position");
		} else {
			if (sscanf(WMGetFromPLString(value), "%i,%i", &dock->x_pos, &dock->y_pos) != 2)
				COMPLAIN("Position");

			/* check position sanity */
			if (!onScreen(scr, dock->x_pos, dock->y_pos)) {
				int x = dock->x_pos;
				wScreenKeepInside(scr, &x, &dock->y_pos, ICON_SIZE, ICON_SIZE);
			}

			/* Is this needed any more? */
			if (type == WM_CLIP) {
				if (dock->x_pos < 0) {
					dock->x_pos = 0;
				} else if (dock->x_pos > scr->scr_width - ICON_SIZE) {
					dock->x_pos = scr->scr_width - ICON_SIZE;
				}
			} else {
				if (wDockEdgeHorizontal(dock->edge)) {
					int ox, oy;

					/* pinned axis is Y, the stored
					 * X is the along offset */
					dock_origin(scr, dock->edge, &ox, &oy);
					dock->y_pos = oy;
					wScreenKeepInside(scr, &dock->x_pos, &oy,
							  ICON_SIZE, ICON_SIZE);
				} else if (have_edge) {
					int ox, oy;

					/* pinned axis is X, the stored
					 * Y is the along offset */
					dock_origin(scr, dock->edge, &ox, &oy);
					dock->x_pos = ox;
					wScreenKeepInside(scr, &ox, &dock->y_pos,
							  ICON_SIZE, ICON_SIZE);
				} else {
					/* A state file written before
					 * the edge existed must not
					 * snap to the nearest edge: a
					 * right-side dock parked at
					 * the top of a wide screen is
					 * closer to the top edge than
					 * to the right one. Keep the
					 * X-midpoint test and never
					 * look at Y. */
					int left, right, midpoint;

					dock_origin(scr, WDOCK_EDGE_LEFT, &left, NULL);
					dock_origin(scr, WDOCK_EDGE_RIGHT, &right, NULL);
					midpoint = (left + right) / 2;

					if (dock->x_pos < midpoint) {
						dock->edge = WDOCK_EDGE_LEFT;
						dock->x_pos = left;
					} else {
						dock->edge = WDOCK_EDGE_RIGHT;
						dock->x_pos = right;
					}
				}
			}
		}
	}

	/* restore lowered/raised state */
	dock->lowered = 0;

	value = WMGetFromPLDictionary(dock_state, dLowered);
	if (value) {
		if (!WMIsPLString(value)) {
			COMPLAIN("Lowered");
		} else {
			if (strcasecmp(WMGetFromPLString(value), "YES") == 0)
				dock->lowered = 1;
		}
	}

	/* restore collapsed state */
	dock->collapsed = 0;

	value = WMGetFromPLDictionary(dock_state, dCollapsed);
	if (value) {
		if (!WMIsPLString(value)) {
			COMPLAIN("Collapsed");
		} else {
			if (strcasecmp(WMGetFromPLString(value), "YES") == 0)
				dock->collapsed = 1;
		}
	}

	/* restore auto-collapsed state */
	value = WMGetFromPLDictionary(dock_state, dAutoCollapse);
	if (value) {
		if (!WMIsPLString(value)) {
			COMPLAIN("AutoCollapse");
		} else {
			if (strcasecmp(WMGetFromPLString(value), "YES") == 0) {
				dock->auto_collapse = 1;
				dock->collapsed = 1;
			}
		}
	}

	/* restore auto-raise/lower state */
	value = WMGetFromPLDictionary(dock_state, dAutoRaiseLower);
	if (value) {
		if (!WMIsPLString(value)) {
			COMPLAIN("AutoRaiseLower");
		} else {
			if (strcasecmp(WMGetFromPLString(value), "YES") == 0)
				dock->auto_raise_lower = 1;
		}
	}

	/* restore attract icons state */
	dock->attract_icons = 0;

	value = WMGetFromPLDictionary(dock_state, dAutoAttractIcons);
	if (value) {
		if (!WMIsPLString(value)) {
			COMPLAIN("AutoAttractIcons");
		} else {
			if (strcasecmp(WMGetFromPLString(value), "YES") == 0)
				dock->attract_icons = 1;
		}
	}

	/* application list */

	/*
	 * When saving, it saves the dock state in
	 * Applications and Applicationsnnn (or ApplicationsWnnn for a
	 * horizontal dock).
	 *
	 * When loading, it will first try Applicationsnnn (or the
	 * width-keyed one when the edge is horizontal, so a stale
	 * height-keyed list cannot win). If it does not exist, use
	 * Applications as default.
	 */

	apps = NULL;
	if (wDockEdgeHorizontal(dock->edge)) {
		snprintf(buffer, sizeof(buffer), "ApplicationsW%i", scr->scr_width);
		tmp = WMCreatePLString(buffer);
		apps = WMGetFromPLDictionary(dock_state, tmp);
		WMReleasePropList(tmp);
	}

	if (!apps) {
		snprintf(buffer, sizeof(buffer), "Applications%i", scr->scr_height);
		tmp = WMCreatePLString(buffer);
		apps = WMGetFromPLDictionary(dock_state, tmp);
		WMReleasePropList(tmp);
	}

	if (!apps)
		apps = WMGetFromPLDictionary(dock_state, dApplications);

	if (!apps)
		goto finish;

	count = WMGetPropListItemCount(apps);
	if (count == 0)
		goto finish;

	old_top = dock->icon_array[0];

	/* dock->icon_count is set to 1 when dock is created.
	 * Since Clip is already restored, we want to keep it so for clip,
	 * but for dock we may change the default top tile, so we set it to 0.
	 */
	if (type == WM_DOCK)
		dock->icon_count = 0;

	for (i = 0; i < count; i++) {
		if (dock->icon_count >= dock->max_icons) {
			wwarning(_("there are too many icons stored in dock. Ignoring what doesn't fit"));
			break;
		}

		value = WMGetFromPLArray(apps, i);
		aicon = restore_icon_state(scr, value, type, dock->icon_count,
					   dock->edge);

		dock->icon_array[dock->icon_count] = aicon;

		if (aicon) {
			aicon->dock = dock;
			aicon->x_pos = dock->x_pos + (aicon->xindex * ICON_SIZE);
			aicon->y_pos = dock->y_pos + (aicon->yindex * ICON_SIZE);

			if (dock->lowered)
				ChangeStackingLevel(aicon->icon->core, WMNormalLevel);
			else
				ChangeStackingLevel(aicon->icon->core, WMDockLevel);

			wCoreConfigure(aicon->icon->core, aicon->x_pos, aicon->y_pos, 0, 0);
			if (!dock->collapsed)
				xcb_map_window(wdisp->conn, aicon->icon->core->window);

			wRaiseFrame(aicon->icon->core);

			dock->icon_count++;
		} else if (dock->icon_count == 0 && type == WM_DOCK) {
			dock->icon_count++;
		}
	}

	/* if the first icon is not defined, use the default */
	if (dock->icon_array[0] == NULL) {
		if (old_top == NULL)
			/* no icon was loaded and no default tile is available */
			goto finish;
		/* update default icon */
		old_top->x_pos = dock->x_pos;
		old_top->y_pos = dock->y_pos;
		if (dock->lowered)
			ChangeStackingLevel(old_top->icon->core, WMNormalLevel);
		else
			ChangeStackingLevel(old_top->icon->core, WMDockLevel);

		dock->icon_array[0] = old_top;
		xcb_configure_window(wdisp->conn, old_top->icon->core->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ dock->x_pos, dock->y_pos });
		/* we don't need to increment dock->icon_count here because it was
		 * incremented in the loop above.
		 */
	} else if (old_top != dock->icon_array[0]) {
		if (old_top == scr->clip_icon)
			scr->clip_icon = dock->icon_array[0];

		wAppIconDestroy(old_top);
	}

finish:
	if (type == WM_DRAWER && dock->icon_array[0])
		update_icon_pixmap(dock->icon_array[0]->icon);

	WMReleasePropList(dock_state);

	return dock;
}

void wDockLaunchWithState(WAppIcon *btn, WSavedState *state)
{
	if (btn && btn->command && !btn->running && !btn->launching) {
		btn->drop_launch = 0;
		btn->paste_launch = 0;

		btn->pid = execCommand(btn, btn->command, state);

		if (btn->pid > 0) {
			if (!btn->forced_dock && !btn->buggy_app) {
				btn->launching = 1;
				dockIconPaint(btn);
			}
		}
	} else {
		wfree(state);
	}
}

void wDockDoAutoLaunch(WDock *dock, int workspace)
{
	WAppIcon *btn;
	WSavedState *state;
	int i;

	for (i = 0; i < dock->max_icons; i++) {
		btn = dock->icon_array[i];
		if (!btn || !btn->auto_launch)
			continue;

		state = wmalloc(sizeof(WSavedState));
		state->workspace = workspace;
		/* TODO: this is klugy and is very difficult to understand
		 * what's going on. Try to clean up */
		wDockLaunchWithState(btn, state);
	}
}

#ifdef USE_DOCK_XDND
static char *launcherExecBasename(const char *exec)
{
	const char *p = exec;
	const char *start, *end, *slash;
	char *out;

	while (*p == ' ' || *p == '\t')
		p++;
	start = p;
	while (*p && *p != ' ' && *p != '\t')
		p++;
	end = p;
	if (start == end)
		return NULL;
	slash = end;
	while (slash > start && slash[-1] != '/')
		slash--;
	if (slash == end)
		return NULL;
	out = wmalloc(end - slash + 1);
	memcpy(out, slash, end - slash);
	out[end - slash] = 0;
	return out;
}

static bool launcherReadDesktopFile(const char *path, char **command,
				    char **instance, char **class, char **icon,
				    char **display_name, char **comment)
{
	WDesktopEntry de;

	if (!wDesktopEntryRead(path, &de))
		return false;

	*command = de.exec;
	*instance = launcherExecBasename(de.exec);
	*class = de.wmclass;
	if (!*class && *instance)
		*class = wstrdup(*instance);
	*display_name = de.name;
	*comment = de.comment;
	*icon = NULL;
	if (de.icon) {
		if (de.icon[0] == '/') {
			if (access(de.icon, R_OK) == 0)
				*icon = de.icon;
			else
				wfree(de.icon);
		} else {
			/* a bare theme name, resolve through the hicolor
			 * fallback theme and the legacy pixmaps directory */
			*icon = wXDGFindThemeIcon(de.icon, "apps", ICON_SIZE);
			wfree(de.icon);
		}
	}
	wfree(de.exec_raw);
	wfree(de.try_exec);
	wfree(de.mimetypes);
	return true;
}

static char *launcherPlistString(WMPropList *plist, const char *key)
{
	WMPropList *pkey, *value;
	char *out = NULL;

	pkey = WMCreatePLString(key);
	value = WMGetFromPLDictionary(plist, pkey);
	WMReleasePropList(pkey);
	if (value && WMIsPLString(value))
		out = wstrdup(WMGetFromPLString(value));
	return out;
}

static bool launcherReadAppBundle(const char *path, const char *base,
				  char **command, char **instance,
				  char **class, char **icon,
				  char **display_name, char **comment)
{
	char buf[PATH_MAX];
	WMPropList *plist;
	char *executable = NULL, *icon_name = NULL;
	size_t len;

	snprintf(buf, sizeof(buf), "%s/Resources/Info-gnustep.plist", path);
	plist = WMReadPropListFromFile(buf);
	if (plist && WMIsPLDictionary(plist)) {
		executable = launcherPlistString(plist, "NSExecutable");
		icon_name = launcherPlistString(plist, "NSIcon");
		*display_name = launcherPlistString(plist, "ApplicationName");
		*comment = launcherPlistString(plist, "ApplicationDescription");
	}
	if (plist)
		WMReleasePropList(plist);

	if (!executable) {
		/* fall back to the bundle name without the .app suffix */
		len = strlen(base);
		if (len > 4)
			len -= 4;
		executable = wmalloc(len + 1);
		memcpy(executable, base, len);
		executable[len] = 0;
	}

	snprintf(buf, sizeof(buf), "%s/%s", path, executable);
	if (access(buf, X_OK) != 0) {
		wfree(executable);
		wfree(icon_name);
		wfree(*display_name);
		wfree(*comment);
		*display_name = *comment = NULL;
		return false;
	}

	*command = wshellquote(buf);
	*instance = executable;
	*class = wstrdup("GNUstep");
	*icon = NULL;
	if (icon_name) {
		snprintf(buf, sizeof(buf), "%s/Resources/%s", path, icon_name);
		if (access(buf, R_OK) == 0)
			*icon = wstrdup(buf);
		wfree(icon_name);
	}
	return true;
}

static bool dockFindFreeAlongSlot(WDock *dock, int *along)
{
	WScreen *scr = dock->screen_ptr;
	int d, i, a, sign;

	for (d = 1; d < dock->max_icons; d++) {
		for (sign = 1; sign >= -1; sign -= 2) {
			a = sign * d;
			for (i = 0; i < dock->max_icons; i++) {
				if (dock->icon_array[i] &&
				    wDockAlongIndex(dock->icon_array[i], dock->edge) == a)
					break;
			}
			if (i < dock->max_icons)
				continue;
			if (!onScreen(scr, dock->x_pos + (wDockEdgeHorizontal(dock->edge) ? a * ICON_SIZE : 0),
				      dock->y_pos + (wDockEdgeHorizontal(dock->edge) ? 0 : a * ICON_SIZE)))
				continue;
			*along = a;
			return true;
		}
	}
	return false;
}

static bool dockLauncherFromDrop(WDock *dock, const char *path)
{
	WScreen *scr = dock->screen_ptr;
	WAppIcon *aicon;
	struct stat st;
	const char *base;
	char *command = NULL, *instance = NULL, *class = NULL, *icon = NULL;
	char *display_name = NULL, *comment = NULL;
	size_t blen;
	int x = 0, y = 0;
	bool got = false;

	if (dock->icon_count >= dock->max_icons)
		return false;

	if (stat(path, &st) != 0)
		return false;

	base = strrchr(path, '/');
	base = base ? base + 1 : path;
	blen = strlen(base);

	if (S_ISDIR(st.st_mode) && blen > 4 && strcmp(base + blen - 4, ".app") == 0) {
		got = launcherReadAppBundle(path, base, &command, &instance,
					    &class, &icon, &display_name, &comment);
	} else if (S_ISREG(st.st_mode) && blen > 8 && strcmp(base + blen - 8, ".desktop") == 0) {
		got = launcherReadDesktopFile(path, &command, &instance,
					      &class, &icon, &display_name, &comment);
	} else if (S_ISREG(st.st_mode) && access(path, X_OK) == 0) {
		command = wshellquote(path);
		instance = wstrdup(base);
		class = wstrdup(base);
		got = true;
	}

	if (!got || !command)
		return false;

	if (dock->type == WM_DOCK) {
		if (wDockEdgeHorizontal(dock->edge)) {
			if (!dockFindFreeAlongSlot(dock, &x))
				got = false;
		} else {
			if (!dockFindFreeAlongSlot(dock, &y))
				got = false;
		}
	} else {
		if (!wDockFindFreeSlot(dock, &x, &y))
			got = false;
	}

	if (!got) {
		wfree(command);
		wfree(instance);
		wfree(class);
		wfree(icon);
		wfree(display_name);
		wfree(comment);
		return false;
	}

	/* register the icon before creating the appicon so the dock tile
	 * picks it up from WMWindowAttributes on first paint */
	if (icon && (instance || class))
		wDefaultChangeIcon(instance, class, icon);

	aicon = wAppIconCreateForDock(scr, command, instance, class, TILE_NORMAL);
	aicon->display_name = display_name;
	aicon->comment = comment;
	wDockAttachIcon(dock, aicon, x, y, true);
	aicon->running = 0;
	xcb_map_window(wdisp->conn, aicon->icon->core->window);
	wAppIconPaint(aicon);

	wfree(command);
	wfree(instance);
	wfree(class);
	wfree(icon);
	return true;
}

/* the event is the SelectionNotify of the XDND drop, the requestor is
 * the tile the drop landed on; the old Xlib code aliased
 * XSelectionEvent.requestor onto XClientMessageEvent.window, the xcb
 * wire structs put time at that slot instead */
static WDock *findDock(WScreen *scr, xcb_generic_event_t *event, int *icon_pos)
{
	xcb_window_t requestor = ((xcb_selection_notify_event_t *)event)->requestor;
	WDock *dock;
	int i;

	dock = scr->dock;
	if (dock != NULL) {
		for (i = 0; i < dock->max_icons; i++) {
			if (dock->icon_array[i] &&
			    dock->icon_array[i]->icon->core->window == requestor) {
				*icon_pos = i;
				return dock;
			}
		}
	}

	dock = scr->workspaces[scr->current_workspace]->clip;
	if (dock != NULL) {
		for (i = 0; i < dock->max_icons; i++) {
			if (dock->icon_array[i] &&
			    dock->icon_array[i]->icon->core->window == requestor) {
				*icon_pos = i;
				return dock;
			}
		}
	}

	*icon_pos = -1;
	return NULL;
}

int wDockReceiveDNDDrop(WScreen *scr, xcb_generic_event_t *event)
{
	WDock *dock;
	WAppIcon *btn;
	int icon_pos;

	dock = findDock(scr, event, &icon_pos);
	if (!dock)
		return false;

	/* a single file dropped on the main dock or clip tile is treated
	 * as a request to dock that application, the path is classified
	 * as a .app bundle, a .desktop entry or a plain executable */
	if (icon_pos == 0) {
		if (scr->xdndpath)
			dockLauncherFromDrop(dock, scr->xdndpath);
		return false;
	}

	/*
	 * Return True if the drop was on an application icon window.
	 * In this case, let the ClientMessage handler redirect the
	 * message to the app.
	 */
	if (dock->icon_array[icon_pos]->icon->icon_win != XCB_NONE)
		return true;

	if (dock->icon_array[icon_pos]->dnd_command != NULL) {
		scr->flags.dnd_data_convertion_status = 0;

		btn = dock->icon_array[icon_pos];

		if (!btn->forced_dock) {
			btn->relaunching = btn->running;
			btn->running = 1;
		}
		if (btn->wm_instance || btn->wm_class) {
			WWindowAttributes attr;
			memset(&attr, 0, sizeof(WWindowAttributes));
			wDefaultFillAttributes(btn->wm_instance, btn->wm_class, &attr, NULL, true);

			if (!attr.no_appicon)
				btn->launching = 1;
			else
				btn->running = 0;
		}

		btn->paste_launch = 0;
		btn->drop_launch = 1;
		scr->xdest_is_gnustep =
			(btn->wm_class && strcmp(btn->wm_class, "GNUstep") == 0);
		scr->last_dock = dock;
		btn->pid = execCommand(btn, btn->dnd_command, NULL);
		scr->xdest_is_gnustep = 0;
		if (btn->pid > 0) {
			dockIconPaint(btn);
		} else {
			btn->launching = 0;
			if (!btn->relaunching)
				btn->running = 0;
		}
	}
	return false;
}
#endif	/* USE_DOCK_XDND */

bool wDockAttachIcon(WDock *dock, WAppIcon *icon, int x, int y, bool update_icon)
{
	WWindow *wwin;
	bool lupdate_icon = false;
	char *command = NULL;
	char *generated_command = NULL;
	size_t len = 0;
	int index;

	icon->editing = 0;

	if (update_icon)
		lupdate_icon = true;

	if (icon->command == NULL) {
		/* If icon->owner exists, it means the application is running */
		if (icon->icon->owner) {
			wwin = icon->icon->owner;
			command = GetCommandForWindow(wwin->client_win);
		}

		if (command) {
			icon->command = command;
		} else {
			/* icon->forced_dock = 1; */
			if (dock->type != WM_CLIP || !icon->attracted) {
				icon->editing = 1;
				wretain(icon);
				if (wInputDialog(dock->screen_ptr, _("Dock Icon"),
						 _("Type the command used to launch the application"), &command)) {
					if (icon->destroyed) {
						wfree(command);
						wrelease(icon);
						return false;
					}
					wAppIconSetCommand(icon, command);
					icon->editing = 0;
				} else {
					icon->editing = 0;
					if (command)
						wfree(command);
					if (icon->destroyed) {
						wrelease(icon);
						return false;
					}
					/* If the target is the dock, reject the icon. If
					 * the target is the clip, make it an attracted icon
					 */
					if (dock->type == WM_CLIP) {
						icon->attracted = 1;
						if (!icon->icon->shadowed) {
							icon->icon->shadowed = 1;
							lupdate_icon = true;
						}
					} else {
						wrelease(icon);
						return false;
					}
				}
				wrelease(icon);
			}
		}
	}

	for (index = 1; index < dock->max_icons; index++)
		if (dock->icon_array[index] == NULL)
			break;
	/* if (index == dock->max_icons)
	   return; */

	assert(index < dock->max_icons);

	dock->icon_array[index] = icon;
	icon->yindex = y;
	icon->xindex = x;

	icon->omnipresent = 0;

	icon->x_pos = dock->x_pos + x * ICON_SIZE;
	icon->y_pos = dock->y_pos + y * ICON_SIZE;

	dock->icon_count++;

	icon->running = 1;
	icon->launching = 0;
	icon->docked = 1;
	icon->dock = dock;
	icon->icon->core->descriptor.handle_mousedown = iconMouseDown;
	icon->icon->core->descriptor.handle_enternotify = clipEnterNotify;
	icon->icon->core->descriptor.handle_leavenotify = clipLeaveNotify;
	icon->icon->core->descriptor.parent_type = WCLASS_DOCK_ICON;
	icon->icon->core->descriptor.parent = icon;

	MoveInStackListUnder(dock->icon_array[index - 1]->icon->core, icon->icon->core);
	wAppIconMove(icon, icon->x_pos, icon->y_pos);

	/*
	 * Update icon pixmap, RImage doesn't change,
	 * so call wIconUpdate is not needed
	 */
	if (lupdate_icon)
		update_icon_pixmap(icon->icon);

	/* Paint it */
	wAppIconPaint(icon);

	/* Save it */
	save_appicon(icon);

	if (w_global.config->auto_arrange_icons)
		wArrangeIcons(dock->screen_ptr, true);

#ifdef USE_DOCK_XDND
	if (icon->command && !icon->dnd_command) {
		len = strlen(icon->command) + 8;
		generated_command = wmalloc(len);
		snprintf(generated_command, len, "%s %%d", icon->command);
		wAppIconSetDnDCommand(icon, generated_command);
	}
#endif

	if (icon->command && !icon->paste_command) {
		len = strlen(icon->command) + 8;
		generated_command = wmalloc(len);
		snprintf(generated_command, len, "%s %%s", icon->command);
		wAppIconSetPasteCommand(icon, generated_command);
	}

	return true;
}

void wDockReattachIcon(WDock *dock, WAppIcon *icon, int x, int y)
{
	int index;

	for (index = 1; index < dock->max_icons; index++) {
		if (dock->icon_array[index] == icon)
			break;
	}
	assert(index < dock->max_icons);

	icon->yindex = y;
	icon->xindex = x;

	icon->x_pos = dock->x_pos + x * ICON_SIZE;
	icon->y_pos = dock->y_pos + y * ICON_SIZE;
}

bool wDockMoveIconBetweenDocks(WDock *src, WDock *dest, WAppIcon *icon, int x, int y)
{
	WWindow *wwin;
	char *command = NULL;
	int index;
	bool update_icon = false;

	if (src == dest)
		return true;	/* No move needed, we're already there */

	if (dest == NULL)
		return false;

	/*
	 * For the moment we can't do this if we move icons in Clip from one
	 * workspace to other, because if we move two or more icons without
	 * command, the dialog box will not be able to tell us to which of the
	 * moved icons it applies. -Dan
	 */
	if ((dest->type == WM_DOCK /*|| dest->keep_attracted */ ) && icon->command == NULL) {
		/* If icon->owner exists, it means the application is running */
		if (icon->icon->owner) {
			wwin = icon->icon->owner;
			command = GetCommandForWindow(wwin->client_win);
		}

		if (command) {
			icon->command = command;
		} else {
			icon->editing = 1;
			/* icon->forced_dock = 1; */
			if (wInputDialog(src->screen_ptr, _("Dock Icon"),
					 _("Type the command used to launch the application"), &command)) {
				wAppIconSetCommand(icon, command);
			} else {
				icon->editing = 0;
				if (command)
					wfree(command);
				return false;
			}
			icon->editing = 0;
		}
	}

	if (dest->type == WM_DOCK || dest->type == WM_DRAWER)
		wClipMakeIconOmnipresent(icon, false);

	for (index = 1; index < src->max_icons; index++) {
		if (src->icon_array[index] == icon)
			break;
	}
	assert(index < src->max_icons);

	src->icon_array[index] = NULL;
	src->icon_count--;

	for (index = 1; index < dest->max_icons; index++) {
		if (dest->icon_array[index] == NULL)
			break;
	}

	assert(index < dest->max_icons);

	dest->icon_array[index] = icon;
	icon->dock = dest;

	/* deselect the icon */
	if (icon->icon->selected)
		wIconSelect(icon->icon);

	icon->icon->core->descriptor.handle_enternotify = clipEnterNotify;
	icon->icon->core->descriptor.handle_leavenotify = clipLeaveNotify;

	/* set it to be kept when moving to dock.
	 * Unless the icon does not have a command set
	 */
	if (icon->command && (dest->type == WM_DOCK || dest->type == WM_DRAWER)) {
		icon->attracted = 0;
		if (icon->icon->shadowed) {
			icon->icon->shadowed = 0;
			update_icon = true;
		}
	}

	if (src->auto_collapse || src->auto_raise_lower)
		clipLeave(src);

	icon->yindex = y;
	icon->xindex = x;

	icon->x_pos = dest->x_pos + x * ICON_SIZE;
	icon->y_pos = dest->y_pos + y * ICON_SIZE;

	dest->icon_count++;

	MoveInStackListUnder(dest->icon_array[index - 1]->icon->core, icon->icon->core);

	/*
	 * Update icon pixmap, RImage doesn't change,
	 * so call wIconUpdate is not needed
	 */
	if (update_icon)
		update_icon_pixmap(icon->icon);

	/* Paint it */
	wAppIconPaint(icon);

	return true;
}

void wDockDetach(WDock *dock, WAppIcon *icon)
{
	int index;
	bool update_icon = false;

	/* make the settings panel be closed */
	if (icon->panel)
		DestroyDockAppSettingsPanel(icon->panel);

	/* This must be called before icon->dock is set to NULL.
	 * Don't move it. -Dan
	 */
	wClipMakeIconOmnipresent(icon, false);

	icon->docked = 0;
	icon->dock = NULL;
	icon->attracted = 0;
	icon->auto_launch = 0;
	if (icon->icon->shadowed) {
		icon->icon->shadowed = 0;
		update_icon = true;
	}

	/* deselect the icon */
	if (icon->icon->selected)
		wIconSelect(icon->icon);

	wAppIconSetCommand(icon, NULL);
#ifdef USE_DOCK_XDND
	wAppIconSetDnDCommand(icon, NULL);
#endif
	wAppIconSetPasteCommand(icon, NULL);

	for (index = 1; index < dock->max_icons; index++)
		if (dock->icon_array[index] == icon)
			break;

	assert(index < dock->max_icons);
	dock->icon_array[index] = NULL;
	icon->yindex = -1;
	icon->xindex = -1;

	dock->icon_count--;

	/* Remove the Cached Icon */
	remove_cache_icon(icon->icon->file);

	/* if the dock is not attached to an application or
	 * the application did not set the appropriate hints yet,
	 * destroy the icon */
	if (!icon->running || !wApplicationOf(icon->main_window)) {
		wAppIconDestroy(icon);
	} else {
		icon->icon->core->descriptor.handle_mousedown = appIconMouseDown;
		icon->icon->core->descriptor.handle_enternotify = NULL;
		icon->icon->core->descriptor.handle_leavenotify = NULL;
		icon->icon->core->descriptor.parent_type = WCLASS_APPICON;
		icon->icon->core->descriptor.parent = icon;

		ChangeStackingLevel(icon->icon->core, NORMAL_ICON_LEVEL);

		/*
		 * Update icon pixmap, RImage doesn't change,
		 * so call wIconUpdate is not needed
		 */
		if (update_icon)
			update_icon_pixmap(icon->icon);

		/* Paint it */
		wAppIconPaint(icon);

		if (w_global.config->auto_arrange_icons)
			wArrangeIcons(dock->screen_ptr, true);
	}

	if (dock->auto_collapse || dock->auto_raise_lower)
		clipLeave(dock);
}

/*
 * returns the closest Dock slot index for the passed
 * coordinates.
 *
 * Returns False if icon can't be docked.
 *
 * Note: this function should NEVER alter ret_x or ret_y, unless it will
 * return True. -Dan
 */
/* Redocking == true means either icon->dock == dock (normal case)
 * or we are called from handleDockMove for a drawer */
bool wDockSnapIcon(WDock *dock, WAppIcon *icon, int req_x, int req_y, int *ret_x, int *ret_y, int redocking)
{
	WScreen *scr = dock->screen_ptr;
	int dx, dy;
	int ex_x, ex_y;
	int i, offset = ICON_SIZE / 2;
	bool horiz;
	int along, across, req_along;
	WAppIcon *aicon = NULL;
	WAppIcon *nicon = NULL;

	if (wdisp->flags.noupdates)
		return false;

	dx = dock->x_pos;
	dy = dock->y_pos;

	/* if the dock is full */
	if (!redocking && (dock->icon_count >= dock->max_icons))
		return false;

	/* exact position */
	if (req_y < dy)
		ex_y = (req_y - offset - dy) / ICON_SIZE;
	else
		ex_y = (req_y + offset - dy) / ICON_SIZE;

	if (req_x < dx)
		ex_x = (req_x - offset - dx) / ICON_SIZE;
	else
		ex_x = (req_x + offset - dx) / ICON_SIZE;

	/* check if the icon is outside the screen boundaries */
	if (!onScreen(scr, dx + ex_x * ICON_SIZE, dy + ex_y * ICON_SIZE))
		return false;

	horiz = wDockEdgeHorizontal(dock->edge);
	if (horiz) {
		along = ex_x;
		across = ex_y;
		req_along = req_x + offset - dx;
	} else {
		along = ex_y;
		across = ex_x;
		req_along = req_y + offset - dy;
	}

	switch (dock->type) {
	case WM_DOCK: {
		WDockSnapResult res;
		bool icon_along[2 * DOCK_DETTACH_THRESHOLD + 1];
		bool drawer_along[2 * DOCK_DETTACH_THRESHOLD + 1];
		int th = DOCK_DETTACH_THRESHOLD;
		int off;

		/* We can return False right away if
		 * - we do not come from this dock (which is a WM_DOCK),
		 * - we are not right over it, and
		 * - we are not the main tile of a drawer.
		 * In the latter case, we are called from handleDockMove. */
		if (icon->dock != dock && across != 0 &&
			!(icon->dock && icon->dock->type == WM_DRAWER && icon == icon->dock->icon_array[0]))
			return false;

		memset(icon_along, 0, sizeof(icon_along));
		memset(drawer_along, 0, sizeof(drawer_along));

		/* mark the along-axis neighbourhood: slots not fully on
		 * screen, slots taken by another appicon and slots taken
		 * by a drawer */
		for (off = -th; off <= th; off++) {
			int a = along + off;

			if (!onScreen(scr, dx + (horiz ? a * ICON_SIZE : 0),
				      dy + (horiz ? 0 : a * ICON_SIZE)))
				icon_along[th + off] = true;
		}
		for (i = 0; i < dock->max_icons; i++) {
			nicon = dock->icon_array[i];
			if (nicon && nicon != icon) {
				int idx = wDockAlongIndex(nicon, dock->edge) - along + th;

				if (idx >= 0 && idx <= 2 * th)
					icon_along[idx] = true;
			}
		}
		for (off = -th; off <= th; off++) {
			if (getDrawerAtAlong(scr, along + off))
				drawer_along[th + off] = true;
		}

		res = wDockSnapMainPure(dock->edge, ICON_SIZE, along, across,
					req_along, redocking, th,
					icon_along, drawer_along);
		if (res.accepted) {
			*ret_x = res.xindex;
			*ret_y = res.yindex;
			return true;
		}
		break;
	}
	case WM_CLIP:
	{
		int neighbours = 0;
		int start, stop, k;
		bool occupied_at = false;
		bool occupied_by_self = false;
		WDockSnapResult res;

		start = icon->omnipresent ? 0 : scr->current_workspace;
		stop = icon->omnipresent ? scr->workspace_count : start + 1;

		for (k = start; k < stop; k++) {
			WDock *tmp = scr->workspaces[k]->clip;
			if (!tmp)
				continue;
			for (i = 0; i < tmp->max_icons; i++) {
				nicon = tmp->icon_array[i];
				if (nicon && nicon->xindex == ex_x && nicon->yindex == ex_y) {
					occupied_at = true;
					occupied_by_self = (nicon == icon);
					break;
				}
			}
			if (occupied_at)
				break;
		}
		for (k = start; k < stop; k++) {
			WDock *tmp = scr->workspaces[k]->clip;
			if (!tmp)
				continue;
			for (i = 0; i < tmp->max_icons; i++) {
				nicon = tmp->icon_array[i];
				if (nicon && nicon != icon &&	/* Icon can't be its own neighbour */
				    (abs(nicon->xindex - ex_x) <= CLIP_ATTACH_VICINITY &&
				     abs(nicon->yindex - ex_y) <= CLIP_ATTACH_VICINITY)) {
					neighbours = 1;
					break;
				}
			}
			if (neighbours)
				break;
		}

		res = wDockSnapClipPure(ex_x, ex_y, occupied_at, occupied_by_self,
					neighbours, redocking);
		if (res.accepted) {
			*ret_x = res.xindex;
			*ret_y = res.yindex;
			return true;
		}
		break;
	}
	case WM_DRAWER:
	{
		WAppIcon *aicons_to_shift[ dock->icon_count ];
		char axis;
		int sign;
		int across = horiz ? ex_y : ex_x;
		int index_of_hole;
		int j;
		bool along_ok;
		WDockSnapResult res;

		wDockGeomAcrossSign(dock->edge, &axis, &sign);

		/* the along-of-dock cell must stay on the strip row, and
		 * the across cell must point into the strip. The hole
		 * index is only consulted once these pass, exactly as
		 * the original pre-checks did */
		along_ok = ((horiz ? ex_x : ex_y) == 0) &&
			   !(abs(across) - dock->icon_count > DOCK_DETTACH_THRESHOLD) &&
			   (across * sign >= 0);

		index_of_hole = along_ok ? indexOfHole(dock, icon, redocking) : 0;

		res = wDockSnapDrawerPure(dock->edge, along_ok, across,
					  dock->icon_count, index_of_hole,
					  redocking);
		if (!res.accepted)
			return false;

		/* Find the appicons between where icon was (the hole) and
		 * where it wants to be (the reduced across) and slide them. */
		j = 0;
		for (i = 1; i < dock->max_icons; i++) {
			aicon = dock->icon_array[i];
			if ((aicon != NULL) && (aicon != icon) &&
			    (wDockAcrossIndex(aicon, dock->edge) >= res.shift_start &&
			     wDockAcrossIndex(aicon, dock->edge) < res.shift_start + res.shift_count))
				aicons_to_shift[ j++ ] = aicon;
		}
		if (j != res.shift_count)
			/* the across set is not the expected [1..n]\{hole},
			 * slide what was found rather than aborting */
			wwarning("wDockSnapIcon: shifting %d appicons between %d and %d (expected %d)",
				j, res.shift_start, res.shift_start + res.shift_count, res.shift_count);

		wSlideAppiconsAcross(aicons_to_shift, j, dock,
				     res.shift_into_drawer);

		*ret_x = res.xindex;
		*ret_y = res.yindex;
		return true;
	}
	}
	return false;
}

static int onScreen(WScreen *scr, int x, int y)
{
	WMRect rect;
	int flags;

	rect.pos.x = x;
	rect.pos.y = y;
	rect.size.width = rect.size.height = ICON_SIZE;

	wGetRectPlacementInfo(scr, rect, &flags);

	return !(flags & (XFLAG_DEAD | XFLAG_PARTIAL));
}

/*
 * returns True if it can find a free slot in the dock,
 * in which case it changes x_pos and y_pos accordingly.
 * Else returns False.
 */
bool wDockFindFreeSlot(WDock *dock, int *x_pos, int *y_pos)
{
	WScreen *scr = dock->screen_ptr;
	WAppIcon *btn;
	WAppIconChain *chain;
	unsigned char *slot_map;
	int mwidth;
	int r;
	int x, y;
	int i, done = false;
	int corner;
	int ex = scr->scr_width, ey = scr->scr_height;
	int sx = 0, sy = 0;
	int extra_count = 0;

	if (dock->type == WM_DRAWER) {
		char axis;
		int sign;

		if (dock->icon_count >= dock->max_icons) { /* drawer is full */
			return false;
		}
		wDockGeomAcrossSign(dock->edge, &axis, &sign);
		if (axis == 'x') {
			*x_pos = dock->icon_count * sign;
			*y_pos = 0;
		} else {
			*x_pos = 0;
			*y_pos = dock->icon_count * sign;
		}
		return true;
	}

	if (dock->type == WM_CLIP && dock != scr->workspaces[scr->current_workspace]->clip)
		extra_count = scr->global_icon_count;

	/* if the dock is full */
	if (dock->icon_count + extra_count >= dock->max_icons)
		return false;

	/* shrink the free rectangle on the dock's edge, so a clip sitting
	 * in a corner does not treat the dock strip as a free border */
	if (!wdisp->flags.nodock && scr->dock) {
		int offset = ICON_SIZE + DOCK_EXTRA_SPACE;

		switch (scr->dock->edge) {
		case WDOCK_EDGE_RIGHT:
			ex -= offset;
			break;
		case WDOCK_EDGE_LEFT:
			sx += offset;
			break;
		case WDOCK_EDGE_TOP:
			sy += offset;
			break;
		case WDOCK_EDGE_BOTTOM:
			ey -= offset;
			break;
		}
	}

	if (ex < dock->x_pos)
		ex = dock->x_pos;

	/* check if clip is in a corner */
	if (dock->type == WM_CLIP) {
		if (dock->x_pos < sx + 1 && dock->y_pos < sy + 1)
			corner = WDOCKSLOT_C_NE;
		else if (dock->x_pos < sx + 1 && dock->y_pos >= (ey - ICON_SIZE))
			corner = WDOCKSLOT_C_SE;
		else if (dock->x_pos >= (ex - ICON_SIZE) && dock->y_pos >= (ey - ICON_SIZE))
			corner = WDOCKSLOT_C_SW;
		else if (dock->x_pos >= (ex - ICON_SIZE) && dock->y_pos < sy + 1)
			corner = WDOCKSLOT_C_NW;
		else
			corner = WDOCKSLOT_C_NONE;
	} else {
		corner = WDOCKSLOT_C_NONE;
	}

	/* If the clip is in the corner, use only slots that are in the border
	 * of the screen */
	if (corner != WDOCKSLOT_C_NONE) {
		unsigned char *hmap, *vmap;
		int hcount, vcount;

		hcount = WMIN(dock->max_icons, scr->scr_width / ICON_SIZE);
		vcount = WMIN(dock->max_icons, scr->scr_height / ICON_SIZE);
		hmap = wmalloc(hcount + 1);
		vmap = wmalloc(vcount + 1);

		/* mark used positions */
		switch (corner) {
		case WDOCKSLOT_C_NE:
			for (i = 0; i < dock->max_icons; i++) {
				btn = dock->icon_array[i];
				if (!btn)
					continue;

				if (btn->xindex == 0 && btn->yindex > 0 && btn->yindex < vcount)
					vmap[btn->yindex] = 1;
				else if (btn->yindex == 0 && btn->xindex > 0 && btn->xindex < hcount)
					hmap[btn->xindex] = 1;
			}
			for (chain = scr->global_icons; chain != NULL; chain = chain->next) {
				btn = chain->aicon;
				if (btn->xindex == 0 && btn->yindex > 0 && btn->yindex < vcount)
					vmap[btn->yindex] = 1;
				else if (btn->yindex == 0 && btn->xindex > 0 && btn->xindex < hcount)
					hmap[btn->xindex] = 1;
			}
			break;
		case WDOCKSLOT_C_NW:
			for (i = 0; i < dock->max_icons; i++) {
				btn = dock->icon_array[i];
				if (!btn)
					continue;

				if (btn->xindex == 0 && btn->yindex > 0 && btn->yindex < vcount)
					vmap[btn->yindex] = 1;
				else if (btn->yindex == 0 && btn->xindex < 0 && btn->xindex > -hcount)
					hmap[-btn->xindex] = 1;
			}
			for (chain = scr->global_icons; chain != NULL; chain = chain->next) {
				btn = chain->aicon;
				if (btn->xindex == 0 && btn->yindex > 0 && btn->yindex < vcount)
					vmap[btn->yindex] = 1;
				else if (btn->yindex == 0 && btn->xindex < 0 && btn->xindex > -hcount)
					hmap[-btn->xindex] = 1;
			}
			break;
		case WDOCKSLOT_C_SE:
			for (i = 0; i < dock->max_icons; i++) {
				btn = dock->icon_array[i];
				if (!btn)
					continue;

				if (btn->xindex == 0 && btn->yindex < 0 && btn->yindex > -vcount)
					vmap[-btn->yindex] = 1;
				else if (btn->yindex == 0 && btn->xindex > 0 && btn->xindex < hcount)
					hmap[btn->xindex] = 1;
			}
			for (chain = scr->global_icons; chain != NULL; chain = chain->next) {
				btn = chain->aicon;
				if (btn->xindex == 0 && btn->yindex < 0 && btn->yindex > -vcount)
					vmap[-btn->yindex] = 1;
				else if (btn->yindex == 0 && btn->xindex > 0 && btn->xindex < hcount)
					hmap[btn->xindex] = 1;
			}
			break;
		case WDOCKSLOT_C_SW:
		default:
			for (i = 0; i < dock->max_icons; i++) {
				btn = dock->icon_array[i];
				if (!btn)
					continue;

				if (btn->xindex == 0 && btn->yindex < 0 && btn->yindex > -vcount)
					vmap[-btn->yindex] = 1;
				else if (btn->yindex == 0 && btn->xindex < 0 && btn->xindex > -hcount)
					hmap[-btn->xindex] = 1;
			}
			for (chain = scr->global_icons; chain != NULL; chain = chain->next) {
				btn = chain->aicon;
				if (btn->xindex == 0 && btn->yindex < 0 && btn->yindex > -vcount)
					vmap[-btn->yindex] = 1;
				else if (btn->yindex == 0 && btn->xindex < 0 && btn->xindex > -hcount)
					hmap[-btn->xindex] = 1;
			}
		}
		done = wDockFindCornerSlotPure(corner, hcount, vcount, hmap, vmap,
					       x_pos, y_pos);
		wfree(vmap);
		wfree(hmap);
		if (done)
			return true;
		/* else, try to find a slot somewhere else */
	}

	/* a map of mwidth x mwidth would be enough if we allowed icons to be
	 * placed outside of screen */
	wDockSlotMapSize(dock->max_icons, &mwidth, &r);

	slot_map = wmalloc(mwidth * mwidth);

	/* mark used slots in the map. If the slot falls outside the map
	 * (for example, when all icons are placed in line), ignore them. */
	for (i = 0; i < dock->max_icons; i++) {
		btn = dock->icon_array[i];
		if (btn)
			slot_map[wDockSlotMapIndex(mwidth, r, btn->xindex, btn->yindex)] = 1;
	}

	for (chain = scr->global_icons; chain != NULL; chain = chain->next)
		slot_map[wDockSlotMapIndex(mwidth, r, chain->aicon->xindex, chain->aicon->yindex)] = 1;

	/* mark the cells whose pixel position is not fully on screen, so
	 * the pure scan can skip them without a server round trip */
	for (y = -r; y <= r; y++) {
		for (x = -r; x <= r; x++) {
			if (!onScreen(scr, dock->x_pos + x * ICON_SIZE,
				      dock->y_pos + y * ICON_SIZE))
				slot_map[wDockSlotMapIndex(mwidth, r, x, y)] = 1;
		}
	}

	done = wDockFindFreeSlotPure(mwidth, r, slot_map, x_pos, y_pos);

	wfree(slot_map);
	return done;
}

static void moveDockOne(WDock *dock, int new_x, int new_y)
{
	WAppIcon *btn;
	int i;

	dock->x_pos = new_x;
	dock->y_pos = new_y;
	for (i = 0; i < dock->max_icons; i++) {
		btn = dock->icon_array[i];
		if (btn) {
			btn->x_pos = new_x + btn->xindex * ICON_SIZE;
			btn->y_pos = new_y + btn->yindex * ICON_SIZE;
			xcb_configure_window(wdisp->conn, btn->icon->core->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ btn->x_pos, btn->y_pos });
		}
	}
}

static void moveDock(WDock *dock, int new_x, int new_y)
{
	WDrawerChain *dc;

	if (dock->type == WM_DOCK) {
		for (dc = dock->screen_ptr->drawers; dc != NULL; dc = dc->next) {
			WDock *drawer = dc->adrawer;

			/* keep the drawer's along-axis offset from the
			 * dock. A drawer does not slide independently
			 * during a dock drag, it keeps its slot */
			if (wDockEdgeHorizontal(dock->edge))
				moveDock(drawer,
					 drawer->x_pos - dock->x_pos + new_x,
					 new_y);
			else
				moveDock(drawer, new_x,
					 drawer->y_pos - dock->y_pos + new_y);
		}
	}

	moveDockOne(dock, new_x, new_y);

	/* the head under the dock can change during an along-axis slide,
	 * the span is the full head so a pure slide of the pinned head
	 * does not need a rewrite */
	if (dock->type == WM_DOCK)
		wDockPublishStrut(dock->screen_ptr);
}

/* Re-derive every dock, drawer and clip slot pixel position from
 * xindex/yindex against the current IconSize and re-anchor the dock
 * against its edge. moveDock() already recurses into every drawer when
 * called on the dock and recomputes both axes, so the only extra step
 * is to re-derive the pinned axis (which was scr_width - ICON_SIZE for
 * a right dock) and walk the per-workspace clips. dock->max_icons and
 * the icon_array allocation are grown when the screen grew, matching
 * the "do not drop icons" rule. */
void wDockApplyIconSize(WScreen *scr)
{
	int i;
	WDrawerChain *dc;

	if (scr->dock) {
		int x, y;

		dock_grow_array(scr->dock, dock_capacity(scr));
		for (dc = scr->drawers; dc != NULL; dc = dc->next)
			dock_grow_array(dc->adrawer, dock_capacity(scr));

		dock_origin(scr, scr->dock->edge, &x, &y);
		if (wDockEdgeHorizontal(scr->dock->edge))
			x = scr->dock->x_pos;
		else
			y = scr->dock->y_pos;
		moveDock(scr->dock, x, y);
	}

	if (!wdisp->flags.noclip && scr->workspaces)
		for (i = 0; i < scr->workspace_count; i++)
			if (scr->workspaces[i] && scr->workspaces[i]->clip)
				moveDock(scr->workspaces[i]->clip,
					 scr->workspaces[i]->clip->x_pos,
					 scr->workspaces[i]->clip->y_pos);
}

/* Re-pin the dock at its current edge without changing it, e.g. after
 * a KeepDockOnPrimaryHead toggle or a RandR reconfiguration. */
static void dock_along_span(const WDock *dock, const int *drawer_off,
			    int ndrawers, int *neg_out, int *pos_out);

void wDockReanchor(WDock *dock)
{
	WScreen *scr = dock->screen_ptr;
	WDrawerChain *dc;
	int *drawer_off;
	int x, y, n, i, neg, pos;

	if (dock->type != WM_DOCK)
		return;

	n = 0;
	for (dc = scr->drawers; dc != NULL; dc = dc->next)
		n++;
	drawer_off = wmalloc(sizeof(int) * (n > 0 ? n : 1));
	i = 0;
	for (dc = scr->drawers; dc != NULL; dc = dc->next, i++) {
		if (wDockEdgeHorizontal(dock->edge))
			drawer_off[i] = dc->adrawer->x_pos - dock->x_pos;
		else
			drawer_off[i] = dc->adrawer->y_pos - dock->y_pos;
	}

	/* the preserved along offset must fit the anchor rect again,
	 * a RandR shrink can leave the old pixel past its end */
	dock_along_span(dock, drawer_off, n, &neg, &pos);
	wfree(drawer_off);

	dock_origin(scr, dock->edge, &x, &y);
	if (wDockEdgeHorizontal(dock->edge))
		x = dock_clamp_along_pixel(scr, dock->edge, dock->x_pos, neg, pos);
	else
		y = dock_clamp_along_pixel(scr, dock->edge, dock->y_pos, neg, pos);

	moveDock(dock, x, y);
	wScreenUpdateUsableArea(scr);
}

/* wDockGeomTranspose takes int pointers, the stored indices are
 * shorts */
static void dock_transpose_icon(WAppIcon *btn, WDockEdge from, WDockEdge to)
{
	int xi = btn->xindex, yi = btn->yindex;

	wDockGeomTranspose(from, to, &xi, &yi);
	btn->xindex = xi;
	btn->yindex = yi;
}

/* The dock's along-axis footprint, in pixels, measured from the main
 * tile: dock icons plus drawer main tiles on both sides of it (an
 * icon can sit above or left of the main tile, at a negative along
 * index). Drawer contents open across the edge and do not count. */
static void dock_along_span(const WDock *dock, const int *drawer_off,
			    int ndrawers, int *neg_out, int *pos_out)
{
	int neg = 0, pos = ICON_SIZE;
	int i;

	for (i = 0; i < dock->max_icons; i++) {
		WAppIcon *btn = dock->icon_array[i];
		int a, e;

		if (!btn)
			continue;
		a = wDockAlongIndex(btn, dock->edge);
		if (a >= 0) {
			e = (a + 1) * ICON_SIZE;
			if (e > pos)
				pos = e;
		} else {
			e = -a * ICON_SIZE;
			if (e > neg)
				neg = e;
		}
	}
	for (i = 0; i < ndrawers; i++) {
		int cell = (drawer_off[i] + ICON_SIZE / 2) / ICON_SIZE;
		int e;

		if (cell >= 0) {
			e = (cell + 1) * ICON_SIZE;
			if (e > pos)
				pos = e;
		} else {
			e = -cell * ICON_SIZE;
			if (e > neg)
				neg = e;
		}
	}
	*neg_out = neg;
	*pos_out = pos;
}

/* Live along-axis extents of the dock run, for the drag clamp */
static void dock_live_span(const WDock *dock, int *neg, int *pos)
{
	WScreen *scr = dock->screen_ptr;
	WDrawerChain *dc;
	int *off;
	int n, i;

	n = 0;
	for (dc = scr->drawers; dc != NULL; dc = dc->next)
		n++;
	off = wmalloc(sizeof(int) * (n > 0 ? n : 1));
	i = 0;
	for (dc = scr->drawers; dc != NULL; dc = dc->next, i++) {
		if (wDockEdgeHorizontal(dock->edge))
			off[i] = dc->adrawer->x_pos - dock->x_pos;
		else
			off[i] = dc->adrawer->y_pos - dock->y_pos;
	}
	dock_along_span(dock, off, n, neg, pos);
	wfree(off);
}

/*
 * Move the dock to another screen edge. The saved indices stay
 * screen-axis cells. A vertical/horizontal switch transposes them, a
 * switch within an axis only flips the across sign. The along layout
 * is then fitted to the new side: the dock's own offset rescales to
 * the same fraction of the usable run, and a layout that cannot fit
 * the shorter side (the bottom edge is usually longer than the sides)
 * has its along offsets compressed proportionally, so icons stay on
 * screen instead of being pushed past it. Icons that still do not fit
 * keep their own cells past the visible run. The drawer strip follows
 * the perpendicular axis and every drawer keeps its along-axis pixel
 * offset from the dock. Icons that no longer fit the new on-screen
 * run stay in the array, they are never dropped here: the next
 * open-drawer slide pulls them toward the anchor until the strip is
 * on screen, the same primitive as fill-the-gap. Do not "fix" an
 * overhanging drawer by deleting icons.
 */
void wDockSetEdge(WDock *dock, WDockEdge new_edge)
{
	WScreen *scr = dock->screen_ptr;
	WDockEdge old_edge = dock->edge;
	WAppIcon *btn;
	WAppIcon **btns;
	WDrawerChain *dc;
	WMRect rect;
	int *drawer_off;
	int *cells, *idx;
	int i, n, m, m_dock, x, y, ox, oy, along;
	int neg_pre, pos_pre, neg_new, pos_new;
	int old_usable, new_usable;
	int old_len, new_len, new_cells;

	if (new_edge == dock->edge)
		return;

	dock_origin(scr, old_edge, &ox, &oy);
	if (wDockEdgeHorizontal(old_edge))
		along = dock->x_pos - ox;
	else
		along = dock->y_pos - oy;

	/* remember each drawer's along-axis offset before anything moves */
	n = 0;
	for (dc = scr->drawers; dc != NULL; dc = dc->next)
		n++;
	drawer_off = wmalloc(sizeof(int) * (n > 0 ? n : 1));
	i = 0;
	for (dc = scr->drawers; dc != NULL; dc = dc->next, i++) {
		if (wDockEdgeHorizontal(old_edge))
			drawer_off[i] = dc->adrawer->x_pos - dock->x_pos;
		else
			drawer_off[i] = dc->adrawer->y_pos - dock->y_pos;
	}

	dock_along_span(dock, drawer_off, n, &neg_pre, &pos_pre);
	rect = dock_anchor_rect(scr);
	old_len = wDockEdgeHorizontal(old_edge) ?
		(int)rect.size.width : (int)rect.size.height;
	new_len = wDockEdgeHorizontal(new_edge) ?
		(int)rect.size.width : (int)rect.size.height;
	new_cells = new_len / ICON_SIZE;
	if (new_cells < 1)
		new_cells = 1;

	/* a layout that cannot fit the new side is compressed
	 * proportionally, the main tile stays at cell 0, icons that
	 * still do not fit keep their own cells past the visible run.
	 * Dock icons are tagged with their dense ordinal and recovered
	 * through btns, not their icon_array slot: the array is sparse
	 * after detachments, so a raw slot can collide with the drawer
	 * ordinals that follow. */
	btns = wmalloc(sizeof(WAppIcon *) * dock->max_icons);
	cells = wmalloc(sizeof(int) * (dock->max_icons + n));
	idx = wmalloc(sizeof(int) * (dock->max_icons + n));
	m = 0;
	for (i = 0; i < dock->max_icons; i++) {
		btn = dock->icon_array[i];
		if (!btn)
			continue;
		cells[m] = wDockAlongIndex(btn, old_edge);
		btns[m] = btn;
		idx[m] = m;
		m++;
	}
	m_dock = m;
	for (i = 0; i < n; i++) {
		cells[m] = (drawer_off[i] + ICON_SIZE / 2) / ICON_SIZE;
		idx[m] = m_dock + i;
		m++;
	}
	wDockGeomCompressAlong(cells, idx, m, new_cells);
	for (i = 0; i < m; i++) {
		if (idx[i] >= m_dock)
			drawer_off[idx[i] - m_dock] = cells[i] * ICON_SIZE;
		else if (wDockEdgeHorizontal(old_edge))
			btns[idx[i]]->xindex = cells[i];
		else
			btns[idx[i]]->yindex = cells[i];
	}
	wfree(cells);
	wfree(idx);
	wfree(btns);

	/* rescale the dock's own offset to the same fraction of the
	 * usable run on the new side */
	dock_along_span(dock, drawer_off, n, &neg_new, &pos_new);
	old_usable = old_len - neg_pre - pos_pre;
	new_usable = new_len - neg_new - pos_new;
	if (old_usable > 0 && new_usable > 0)
		along = (along * new_usable + old_usable / 2) / old_usable;

	/* transpose the dock's icons, the main tile stays (0, 0) */
	for (i = 0; i < dock->max_icons; i++) {
		btn = dock->icon_array[i];
		if (!btn)
			continue;
		dock_transpose_icon(btn, old_edge, new_edge);
	}
	dock->edge = new_edge;

	/* the drawers open perpendicular to the new edge */
	for (dc = scr->drawers; dc != NULL; dc = dc->next) {
		WDock *drawer = dc->adrawer;

		for (i = 0; i < drawer->max_icons; i++) {
			btn = drawer->icon_array[i];
			if (!btn)
				continue;
			dock_transpose_icon(btn, old_edge, new_edge);
		}
		drawer->edge = new_edge;
	}

	dock_origin(scr, new_edge, &ox, &oy);
	if (wDockEdgeHorizontal(new_edge)) {
		x = dock_clamp_along_pixel(scr, new_edge, ox + along, neg_new, pos_new);
		y = oy;
	} else {
		x = ox;
		y = dock_clamp_along_pixel(scr, new_edge, oy + along, neg_new, pos_new);
	}

	i = 0;
	for (dc = scr->drawers; dc != NULL; dc = dc->next, i++) {
		WDock *drawer = dc->adrawer;
		int dx, dy;

		/* the drawer keeps its pixel offset from the dock, and
		 * the clamp only has to fit the drawer's own tile: the
		 * offset is part of the span the dock was clamped
		 * with */
		if (wDockEdgeHorizontal(new_edge)) {
			dx = dock_clamp_along_pixel(scr, new_edge, x + drawer_off[i], 0, ICON_SIZE);
			dy = y;
		} else {
			dx = x;
			dy = dock_clamp_along_pixel(scr, new_edge, y + drawer_off[i], 0, ICON_SIZE);
		}
		moveDockOne(drawer, dx, dy);
	}
	wfree(drawer_off);

	moveDockOne(dock, x, y);

	/* the drawer tile bakes the arrow direction, rebuild it */
	if (!wdisp->flags.nodrawer) {
		if (scr->drawer_tile)
			RReleaseImage(scr->drawer_tile);
		scr->drawer_tile = wDrawerMakeTile(scr, scr->icon_tile);
		for (dc = scr->drawers; dc != NULL; dc = dc->next) {
			WAppIcon *main_tile = dc->adrawer->icon_array[0];

			wIconUpdate(main_tile->icon);
			wDrawerIconPaint(main_tile);
		}
	}

	wArrangeIcons(scr, false);
	wScreenUpdateUsableArea(scr);
}

/* Snap a clip back onto a visible head after a RandR reconfiguration,
 * preserving its relative position within that head */
void wClipSnapToHead(WDock *clip)
{
	WScreen *scr = clip->screen_ptr;
	WMRect rect, head;
	float rel_x, rel_y;
	int x = clip->x_pos;
	int y = clip->y_pos;

	/* Already fully on a visible head, nothing to do */
	if (!wScreenKeepInside(scr, &x, &y, ICON_SIZE, ICON_SIZE))
		return;

	rect.pos.x = clip->x_pos;
	rect.pos.y = clip->y_pos;
	rect.size.width  = ICON_SIZE;
	rect.size.height = ICON_SIZE;

	/* Find the nearest remaining head to the clip's old position */
	head = wGetRectForHead(scr, wGetHeadForRect(scr, rect));

	/* a head that cannot hold the icon has no valid position for it,
	 * leave the clip where wScreenKeepInside clamped it */
	if (head.size.width < (unsigned int)ICON_SIZE
	    || head.size.height < (unsigned int)ICON_SIZE) {
		moveDock(clip, x, y);
		return;
	}

	/* Compute fractional position within that head and clamp to [0..1] */
	rel_x = (float)(clip->x_pos - head.pos.x) / (float)head.size.width;
	rel_y = (float)(clip->y_pos - head.pos.y) / (float)head.size.height;

	if (rel_x < 0.0f) rel_x = 0.0f;
	else if (rel_x > 1.0f) rel_x = 1.0f;
	if (rel_y < 0.0f) rel_y = 0.0f;
	else if (rel_y > 1.0f) rel_y = 1.0f;

	x = head.pos.x + (int)(rel_x * (float)(head.size.width - (unsigned int)ICON_SIZE));
	y = head.pos.y + (int)(rel_y * (float)(head.size.height - (unsigned int)ICON_SIZE));

	moveDock(clip, x, y);
}

static pid_t execCommand(WAppIcon *btn, const char *command, WSavedState *state)
{
	WScreen *scr = btn->icon->core->screen_ptr;
	struct LaunchSetup ls;
	pid_t pid;
	char **argv = NULL;
	int argc;
	int sn_ws;
	char *cmdline;

	cmdline = ExpandOptions(scr, command);

	if (scr->flags.dnd_data_convertion_status || !cmdline) {
		if (cmdline)
			wfree(cmdline);
		if (state)
			wfree(state);
		return 0;
	}

	wtokensplit(cmdline, &argv, &argc);

	if (!argc) {
		if (cmdline)
			wfree(cmdline);
		if (state)
			wfree(state);
		return 0;
	}

	argv = wrealloc(argv, (argc + 1) * sizeof(*argv));
	argv[argc] = NULL;

	if (state && state->workspace >= 0)
		sn_ws = state->workspace;
	else if (btn->dock && btn->dock != scr->dock && btn->dock->type != WM_DRAWER && !btn->omnipresent)
		sn_ws = scr->current_workspace;
	else
		sn_ws = scr->current_workspace;

	ls.scr = scr;
	ls.sn = wStartupLaunchBegin(scr, argv[0],
				    btn->wm_instance ? btn->wm_instance : argv[0],
				    btn->wm_class, sn_ws, btn);

	pid = wspawnvp_setup(argv, LaunchChildSetup, &ls);
	wtokenfree(argv, argc);

	if (pid > 0) {
		wStartupLaunchSpawned(ls.sn, pid);
		if (!state) {
			state = wmalloc(sizeof(WSavedState));
			state->hidden = -1;
			state->miniaturized = -1;
			state->shaded = -1;
			if (btn->dock == scr->dock || btn->dock->type == WM_DRAWER || btn->omnipresent)
				state->workspace = -1;
			else
				state->workspace = scr->current_workspace;
		}
		wWindowAddSavedState(btn->wm_instance, btn->wm_class, cmdline, pid, state);
		wAddDeathHandler(pid, trackDeadProcess, btn->dock, "dock", wDockIconSlot(btn));
	} else {
		wStartupLaunchAbort(ls.sn);
		if (state)
			wfree(state);
	}
	wfree(cmdline);
	return pid;
}

void wDockHideIcons(WDock *dock)
{
	int i;

	if (dock == NULL)
		return;

	for (i = 1; i < dock->max_icons; i++) {
		if (dock->icon_array[i])
			xcb_unmap_window(wdisp->conn, dock->icon_array[i]->icon->core->window);
	}
	dock->mapped = 0;

	dockIconPaint(dock->icon_array[0]);
}

void wDockShowIcons(WDock *dock)
{
	int i;
	WAppIcon *btn;

	if (dock == NULL)
		return;

	btn = dock->icon_array[0];
	moveDock(dock, btn->x_pos, btn->y_pos);

	/* Deleting any change in stacking level, this function is now only about
	   mapping icons */

	if (!dock->collapsed) {
		for (i = 1; i < dock->max_icons; i++) {
			if (dock->icon_array[i])
				xcb_map_window(wdisp->conn, dock->icon_array[i]->icon->core->window);
		}
	}
	dock->mapped = 1;

	dockIconPaint(btn);
}

void wDockLower(WDock *dock)
{
	int i;
	WDrawerChain *dc;

	if (dock->type == WM_DOCK) {
		for (dc = dock->screen_ptr->drawers; dc != NULL; dc = dc->next)
			wDockLower(dc->adrawer);
	}
	for (i = 0; i < dock->max_icons; i++) {
		if (dock->icon_array[i])
			wLowerFrame(dock->icon_array[i]->icon->core);
	}
}

void wDockRaise(WDock *dock)
{
	int i;
	WDrawerChain *dc;

	for (i = dock->max_icons - 1; i >= 0; i--) {
		if (dock->icon_array[i])
			wRaiseFrame(dock->icon_array[i]->icon->core);
	}
	if (dock->type == WM_DOCK) {
		for (dc = dock->screen_ptr->drawers; dc != NULL; dc = dc->next)
			wDockRaise(dc->adrawer);
	}
}

void wDockRaiseLower(WDock *dock)
{
	if (!dock->icon_array[0]->icon->core->stacking->above
	    || (dock->icon_array[0]->icon->core->stacking->window_level
		!= dock->icon_array[0]->icon->core->stacking->above->stacking->window_level))
		wDockLower(dock);
	else
		wDockRaise(dock);
}

void wDockFinishLaunch(WAppIcon *icon)
{
	icon->launching = 0;
	icon->relaunching = 0;
	dockIconPaint(icon);
}

WAppIcon *wDockFindIconForWindow(WDock *dock, xcb_window_t window)
{
	WAppIcon *icon;
	int i;

	for (i = 0; i < dock->max_icons; i++) {
		icon = dock->icon_array[i];
		if (icon && icon->main_window == window)
			return icon;
	}
	return NULL;
}

void wDockTrackWindowLaunch(WDock *dock, xcb_window_t window, const char *wm_instance,
			    const char *wm_class, const char *command,
			    pid_t client_pid)
{
	WAppIcon *icon;
	WApplication *wapp;
	int i;
	bool firstPass = true;
	bool found = false;
	bool own_class = false;
	char *own_command = NULL;

	/* every caller that holds a managed WWindow already has the
	 * interned class strings and the joined WM_COMMAND from the
	 * wManagePrefetch batch, the fake group leader carries copies of
	 * the same values, only re-read when the caller could not supply
	 * either (a window that wManageWindow refused) */
	if (!wm_instance && !wm_class) {
		if (!PropGetWMClass(window, (char **)&wm_class, (char **)&wm_instance)) {
			wunintern(wm_class);
			wunintern(wm_instance);
			return;
		}
		own_class = true;
		own_command = GetCommandForWindow(window);
		command = own_command;
	}

 retry:
	for (i = 0; i < dock->max_icons; i++) {
		icon = dock->icon_array[i];
		if (!icon)
			continue;

		/* app is already attached to icon */
		if (icon->main_window == window) {
			found = true;
			break;
		}

		if ((icon->wm_instance || icon->wm_class)
		    && (icon->launching || !icon->running)) {

			if (icon->wm_instance && wm_instance && strcmp(icon->wm_instance, wm_instance) != 0)
				continue;

			if (icon->wm_class && wm_class && strcmp(icon->wm_class, wm_class) != 0)
				continue;

			if (firstPass && command && icon->command && strcmp(icon->command, command) != 0)
				continue;

			if (!icon->relaunching) {
				/* Possibly an application that was docked with dockit,
				 * but the user did not update WMState to indicate that
				 * it was docked by force */
				wapp = wApplicationOf(window);
				if (!wapp) {
					icon->forced_dock = 1;
					icon->running = 0;
				}
				if (!icon->forced_dock)
					icon->main_window = window;
			}
			found = true;
			if (!w_global.config->no_animations && !icon->launching &&
			    !dock->screen_ptr->flags.startup && !dock->collapsed) {
				WAppIcon *aicon;
				int x0, y0;

				icon->launching = 1;
				dockIconPaint(icon);

				aicon = wAppIconCreateForDock(dock->screen_ptr, NULL,
							      wm_instance, wm_class, TILE_NORMAL);
				PlaceIcon(dock->screen_ptr, &x0, &y0, wGetHeadForWindow(aicon->icon->owner));
				wAppIconMove(aicon, x0, y0);
				/* Should this always be lowered? -Dan */
				if (dock->lowered)
					wLowerFrame(aicon->icon->core);
				xcb_map_window(wdisp->conn, aicon->icon->core->window);
				aicon->launching = 1;
				wAppIconPaint(aicon);
				slide_window(aicon->icon->core->window, x0, y0, icon->x_pos, icon->y_pos);
				xcb_unmap_window(wdisp->conn, aicon->icon->core->window);
				wAppIconDestroy(aicon);
			}
			wDockFinishLaunch(icon);
			break;
		}
	}

	if (firstPass && !found) {
		firstPass = false;
		goto retry;
	}

	/* pid fallback for tiles whose recorded class cannot match the
	 * window, typically launchers created from a .desktop drop without
	 * StartupWMClass= where the class was synthesized from the Exec
	 * basename, only consider tiles we just launched ourselves so a
	 * generic toolkit class cannot capture an unrelated tile, the pid
	 * must come from the mapped client because window may be a
	 * shared_appicon fake group leader owned by wmaker itself */
	for (i = 0; !found && client_pid > 0 && i < dock->max_icons; i++) {
		icon = dock->icon_array[i];
		if (!icon || !icon->launching || icon->pid <= 0)
			continue;
		if (icon->pid != client_pid)
			continue;
		if (!icon->relaunching) {
			wapp = wApplicationOf(window);
			if (!wapp) {
				icon->forced_dock = 1;
				icon->running = 0;
			}
			if (!icon->forced_dock)
				icon->main_window = window;
		}
		wDockFinishLaunch(icon);
		break;
	}

	if (own_command)
		wfree(own_command);

	if (own_class) {
		wunintern(wm_class);
		wunintern(wm_instance);
	}
}

void wClipUpdateForWorkspaceChange(WScreen *scr, int workspace)
{
	if (!wdisp->flags.noclip) {
		scr->clip_icon->dock = scr->workspaces[workspace]->clip;
		if (scr->current_workspace != workspace) {
			WDock *old_clip = scr->workspaces[scr->current_workspace]->clip;
			WAppIconChain *chain = scr->global_icons;

			while (chain) {
				wDockMoveIconBetweenDocks(chain->aicon->dock,
						     scr->workspaces[workspace]->clip,
						     chain->aicon, chain->aicon->xindex, chain->aicon->yindex);
				if (scr->workspaces[workspace]->clip->collapsed)
					xcb_unmap_window(wdisp->conn, chain->aicon->icon->core->window);
				chain = chain->next;
			}

			wDockHideIcons(old_clip);
			if (old_clip->auto_raise_lower) {
				WMDeleteTimerHandlerPtr(&old_clip->auto_raise_magic);
				wDockLower(old_clip);
			}
			if (old_clip->auto_collapse) {
				WMDeleteTimerHandlerPtr(&old_clip->auto_expand_magic);
				old_clip->collapsed = 1;
			}
			wDockShowIcons(scr->workspaces[workspace]->clip);
		}
	}
}

/* Mirror-only clip update for per-head focus moves: when focus crosses
 * to a head showing a different workspace we want the single visible
 * clip icon to follow that head's workspace, but without migrating
 * omnipresent icons through scr->global_icons (that should only happen
 * on a real workspace switch). Just hide/show the two clips and
 * repoint scr->clip_icon->dock. */
void wClipRepointForActiveHead(WScreen *scr, int old_workspace, int new_workspace)
{
	WDock *old_clip, *new_clip;

	if (wdisp->flags.noclip || !scr->clip_icon)
		return;
	if (old_workspace == new_workspace)
		return;
	if (new_workspace < 0 || new_workspace >= scr->workspace_count)
		return;

	new_clip = scr->workspaces[new_workspace]->clip;
	scr->clip_icon->dock = new_clip;

	if (old_workspace >= 0 && old_workspace < scr->workspace_count) {
		old_clip = scr->workspaces[old_workspace]->clip;
		if (old_clip && old_clip != new_clip) {
			wDockHideIcons(old_clip);
			if (old_clip->auto_raise_lower) {
				WMDeleteTimerHandlerPtr(&old_clip->auto_raise_magic);
				wDockLower(old_clip);
			}
			if (old_clip->auto_collapse) {
				WMDeleteTimerHandlerPtr(&old_clip->auto_expand_magic);
				old_clip->collapsed = 1;
			}
		}
	}
	if (new_clip)
		wDockShowIcons(new_clip);
}

static void trackDeadProcess(pid_t pid, unsigned int status, void *cdata)
{
	WDock *dock = cdata;
	WAppIcon *icon;
	int i;

	wStartupLaunchChildExited(dock->screen_ptr, pid);

	for (i = 0; i < dock->max_icons; i++) {
		icon = dock->icon_array[i];
		if (!icon)
			continue;

		if (icon->pid == pid) {
			/* the startup-notification timeout may already have
			 * cleared launching for a window we never managed to
			 * attach by class or pid, only reset running when no
			 * main_window was recorded so a forking launcher whose
			 * spawned process exits does not detach a live app */
			if (!icon->relaunching && icon->main_window == XCB_NONE)
				icon->running = 0;
			if (icon->launching)
				wDockFinishLaunch(icon);
			icon->pid = 0;
			if (status == 127) {
				char msg[PATH_MAX];
				char *cmd;

#ifdef USE_DOCK_XDND
				if (icon->drop_launch)
					cmd = icon->dnd_command;
				else
#endif
				if (icon->paste_launch)
					cmd = icon->paste_command;
				else
					cmd = icon->command;

				snprintf(msg, sizeof(msg), _("Could not execute command \"%s\""), cmd);

				wMessageDialog(dock->screen_ptr, _("Error"), msg, _("OK"), NULL, NULL);
			}
			break;
		}
	}
}

/* This function is called when the dock switches state between
 * "normal" (including auto-raise/lower) and "keep on top". It is
 * therefore clearly distinct from wDockLower/Raise, which are called
 * each time a not-kept-on-top dock is lowered/raised. */
static void toggleLowered(WDock *dock)
{
	WAppIcon *tmp;
	WDrawerChain *dc;
	int newlevel, i;

	if (!dock->lowered) {
		newlevel = WMNormalLevel;
		dock->lowered = 1;
	} else {
		newlevel = WMDockLevel;
		dock->lowered = 0;
	}

	for (i = 0; i < dock->max_icons; i++) {
		tmp = dock->icon_array[i];
		if (!tmp)
			continue;

		ChangeStackingLevel(tmp->icon->core, newlevel);

		/* When the dock is no longer "on top", explicitly lower it as well.
		 * It saves some CPU cycles (probably) to do it ourselves here
		 * rather than calling wDockLower at the end of toggleLowered */
		if (dock->lowered)
			wLowerFrame(tmp->icon->core);
	}

	if (dock->type == WM_DOCK) {
		for (dc = dock->screen_ptr->drawers; dc != NULL; dc = dc->next) {
			toggleLowered(dc->adrawer);
		}
		wScreenUpdateUsableArea(dock->screen_ptr);
	}
}

static void toggleCollapsed(WDock *dock)
{
	if (dock->collapsed) {
		dock->collapsed = 0;
		wDockShowIcons(dock);
	} else {
		dock->collapsed = 1;
		wDockHideIcons(dock);
	}
}

static void openDockMenu(WDock *dock, WAppIcon *aicon, xcb_generic_event_t *event)
{
	WScreen *scr = dock->screen_ptr;
	WObjDescriptor *desc;
	WMenuEntry *entry;
	WApplication *wapp = NULL;
	int index = 0;
	int x_pos;
	int y_pos;
	int n_selected;
	int appIsRunning = aicon->running && aicon->icon && aicon->icon->owner;

	if (dock->type == WM_DOCK) {
		/* Dock position menu */
		updateDockPositionMenu(scr->dock_pos_menu, dock);
		dock->menu->flags.realized = 0;
		if (!wdisp->flags.nodrawer) {
			/* add a drawer */
			entry = dock->menu->entries[++index];
			entry->clientdata = aicon;
			wMenuSetEnabled(dock->menu, index, true);
		}
	} else {
		/* clip/drawer options */
		if (scr->clip_options)
			updateClipOptionsMenu(scr->clip_options, dock);

		n_selected = numberOfSelectedIcons(dock);

		if (dock->type == WM_CLIP) {
			/* Rename Workspace */
			entry = dock->menu->entries[++index];
			if (aicon == scr->clip_icon) {
				entry->callback = renameCallback;
				entry->clientdata = dock;
				entry->flags.indicator = 0;
				entry->text = _("Rename Workspace");
			} else {
				entry->callback = omnipresentCallback;
				entry->clientdata = aicon;
				if (n_selected > 0) {
					entry->flags.indicator = 0;
					entry->text = _("Toggle Omnipresent");
				} else {
					entry->flags.indicator = 1;
					entry->flags.indicator_on = aicon->omnipresent;
					entry->flags.indicator_type = MI_CHECK;
					entry->text = _("Omnipresent");
				}
			}
		}

		/* select/unselect icon */
		entry = dock->menu->entries[++index];
		entry->clientdata = aicon;
		entry->flags.indicator_on = aicon->icon->selected;
		wMenuSetEnabled(dock->menu, index, aicon != scr->clip_icon && !wIsADrawer(aicon));

		/* select/unselect all icons */
		entry = dock->menu->entries[++index];
		entry->clientdata = aicon;
		if (n_selected > 0)
			entry->text = _("Unselect All Icons");
		else
			entry->text = _("Select All Icons");

		wMenuSetEnabled(dock->menu, index, dock->icon_count > 1);

		/* keep icon(s) */
		entry = dock->menu->entries[++index];
		entry->clientdata = aicon;
		if (n_selected > 1)
			entry->text = _("Keep Icons");
		else
			entry->text = _("Keep Icon");

		wMenuSetEnabled(dock->menu, index, dock->icon_count > 1);

		if (dock->type == WM_CLIP) {
			/* this is the workspace submenu part */
			entry = dock->menu->entries[++index];
			if (n_selected > 1 ||
			    (n_selected == 0 && aicon == scr->clip_icon && dock->icon_count > 2))
				entry->text = _("Move Icons To");
			else
				entry->text = _("Move Icon To");

			if (scr->clip_submenu)
				updateWorkspaceMenu(scr->clip_submenu, aicon);

			wMenuSetEnabled(dock->menu, index, !aicon->omnipresent && dock->icon_count > 1);
		}

		/* remove icon(s) */
		entry = dock->menu->entries[++index];
		entry->clientdata = aicon;
		if (n_selected > 1)
			entry->text = _("Remove Icons");
		else
			entry->text = _("Remove Icon");

		wMenuSetEnabled(dock->menu, index, dock->icon_count > 1);

		/* attract icon(s) */
		entry = dock->menu->entries[++index];
		entry->clientdata = aicon;

		dock->menu->flags.realized = 0;
		wMenuRealize(dock->menu);
	}

	if (aicon->icon->owner)
		wapp = wApplicationOf(aicon->icon->owner->main_window);
	else
		wapp = NULL;

	/* launch */
	entry = dock->menu->entries[++index];
	entry->clientdata = aicon;
	wMenuSetEnabled(dock->menu, index, aicon->command != NULL);

	/* unhide here */
	entry = dock->menu->entries[++index];
	entry->clientdata = aicon;
	if (wapp && wapp->flags.hidden)
		entry->text = _("Unhide Here");
	else
		entry->text = _("Bring Here");

	wMenuSetEnabled(dock->menu, index, appIsRunning);

	/* hide */
	entry = dock->menu->entries[++index];
	entry->clientdata = aicon;
	if (wapp && wapp->flags.hidden)
		entry->text = _("Unhide");
	else
		entry->text = _("Hide");

	wMenuSetEnabled(dock->menu, index, appIsRunning);

	/* settings */
	entry = dock->menu->entries[++index];
	entry->clientdata = aicon;
	wMenuSetEnabled(dock->menu, index, !aicon->editing && !wdisp->flags.noupdates);

	/* kill or remove drawer */
	entry = dock->menu->entries[++index];
	entry->clientdata = aicon;
	if (wIsADrawer(aicon)) {
		entry->callback = removeDrawerCallback;
		entry->text = _("Remove drawer");
		wMenuSetEnabled(dock->menu, index, true);
	} else {
		entry->callback = killCallback;
		entry->text = _("Kill");
		wMenuSetEnabled(dock->menu, index, appIsRunning);
	}

	if (!dock->menu->flags.realized)
		wMenuRealize(dock->menu);

	if (dock->type == WM_CLIP) {
		x_pos = ((xcb_button_press_event_t *)event)->root_x - dock->menu->frame->core->width / 2 - 1;
		if (x_pos < 0) {
			x_pos = 0;
		} else if (x_pos + dock->menu->frame->core->width > scr->scr_width - 2) {
			x_pos = scr->scr_width - dock->menu->frame->core->width - 4;
		}
		y_pos = ((xcb_button_press_event_t *)event)->root_y + 2;
		if (y_pos < 0) {
			y_pos = 0;
		} else if (y_pos + dock->menu->frame->core->height > scr->scr_height - 2) {
			y_pos = scr->scr_height - dock->menu->frame->core->height - 4;
		}
	} else {
		int menu_w = dock->menu->frame->core->width;
		int menu_h = dock->menu->frame->core->height;
		int gap = 3;

		/* open away from the screen edge the dock is pinned to */
		switch (dock->edge) {
		case WDOCK_EDGE_RIGHT:
			x_pos = dock->x_pos + ICON_SIZE + DOCK_EXTRA_SPACE - menu_w - gap;
			y_pos = ((xcb_button_press_event_t *)event)->root_y + 2;
			break;
		case WDOCK_EDGE_LEFT:
			x_pos = dock->x_pos + ICON_SIZE + DOCK_EXTRA_SPACE + gap;
			y_pos = ((xcb_button_press_event_t *)event)->root_y + 2;
			break;
		case WDOCK_EDGE_TOP:
			x_pos = ((xcb_button_press_event_t *)event)->root_x + 2;
			y_pos = dock->y_pos + ICON_SIZE + DOCK_EXTRA_SPACE + gap;
			break;
		case WDOCK_EDGE_BOTTOM:
		default:
			x_pos = ((xcb_button_press_event_t *)event)->root_x + 2;
			y_pos = dock->y_pos - menu_h - gap;
			break;
		}

		if (x_pos < 0)
			x_pos = 0;
		else if (x_pos + menu_w > scr->scr_width - 2)
			x_pos = scr->scr_width - menu_w - 4;

		if (y_pos < 0)
			y_pos = 0;
		else if (y_pos + menu_h > scr->scr_height - 2)
			y_pos = scr->scr_height - menu_h - 4;
	}

	wMenuMapAt(dock->menu, x_pos, y_pos, false);

	/* allow drag select */
	event->response_type |= 0x80;
	desc = &dock->menu->menu->descriptor;
	(*desc->handle_mousedown) (desc, event);
}

/******************************************************************/
static void iconDblClick(WObjDescriptor *desc, xcb_generic_event_t *event)
{
	WAppIcon *btn = desc->parent;
	WDock *dock = btn->dock;
	WApplication *wapp = NULL;
	int unhideHere;
	WWindow *uwin;

	if (btn->icon->owner && !(((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_CONTROL)) {
		wapp = wApplicationOf(btn->icon->owner->main_window);

		assert(wapp != NULL);

		unhideHere = (((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_SHIFT);

		uwin = wApplicationUrgentWindow(wapp);
		if (uwin && !unhideHere) {
			/* activate the window asking for attention */
			wWindowSingleFocus(uwin);
		} else {
			/* go to the last workspace that the user worked on the app */
			if (wapp->last_workspace != dock->screen_ptr->current_workspace
			    && !unhideHere)
				wWorkspaceChangeFromPointer(dock->screen_ptr, wapp->last_workspace);

			wUnhideApplication(wapp, ((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_2, unhideHere);
		}

		if (((xcb_button_press_event_t *)event)->state & MOD_MASK)
			wHideOtherApplications(btn->icon->owner);
	} else {
		if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_1) {
			if (((xcb_button_press_event_t *)event)->state & MOD_MASK) {
				/* raise/lower dock */
				toggleLowered(dock);
			} else if (btn == dock->screen_ptr->clip_icon) {
				if (getClipButton(((xcb_button_press_event_t *)event)->event_x, ((xcb_button_press_event_t *)event)->event_y) != CLIP_IDLE)
					handleClipChangeWorkspace(dock->screen_ptr, event);
				else if (wdisp->flags.clip_merged_in_dock) {
					// Is actually the dock
					if (btn->command)
					{
						if (!btn->launching && (!btn->running || (((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_CONTROL)))
							launchDockedApplication(btn, false);
					}
					else
					{
						wShowInfoPanel(dock->screen_ptr);
					}
				}
				else
					toggleCollapsed(dock);
			} else if (wIsADrawer(btn)) {
				toggleCollapsed(dock);
			} else if (btn->command) {
				if (!btn->launching && (!btn->running || (((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_CONTROL)))
					launchDockedApplication(btn, false);
			} else if (btn->xindex == 0 && btn->yindex == 0 && btn->dock->type == WM_DOCK) {
				wShowInfoPanel(dock->screen_ptr);
			}
		}
	}
}

/* Edge under the pointer with one icon of hysteresis. The anchor head
 * is the primary one under KeepDockOnPrimaryHead, matching the dock
 * origin, the head under the pointer otherwise. */
static WDockEdge wDockEdgeAtPointer(WScreen *scr, int px, int py, WDockEdge current)
{
	WMRect rect;
	int head;

	if (w_global.config->keep_dock_on_primary_head)
		head = scr->xine_info.primary_head;
	else
		head = wGetHeadForPointerLocation(scr);

	rect = wGetRectForHead(scr, head);

	return wDockGeomNearestEdge(rect, px, py, current, ICON_SIZE);
}

/* Wrapped along-axis pixel for a position off the visible run. The
 * modulo lands in the partial band at the far edge for an icon that
 * just crossed the near edge, so such a target snaps to the far end of
 * the anchor rect instead. */
static int dock_wrap_along_pixel(const WDock *dock, int pos)
{
	WMRect rect = dock_anchor_rect(dock->screen_ptr);
	int run = wDockAlongLimit(dock) * ICON_SIZE;
	int lo, hi, wrapped;

	if (wDockEdgeHorizontal(dock->edge)) {
		lo = (int)rect.pos.x;
		hi = lo + (int)rect.size.width - ICON_SIZE;
	} else {
		lo = (int)rect.pos.y;
		hi = lo + (int)rect.size.height - ICON_SIZE;
	}

	wrapped = (pos + run) % run;
	if (wrapped < lo || wrapped > hi)
		wrapped = (pos < lo) ? hi : lo;

	return wrapped;
}

/* Wrap the along index of the icons that ended off the visible run
 * (WrapAppiconsInDock). The modulo is the along limit, not max_icons,
 * so icons wrap inside the visible run rather than into the spare
 * array tail. Drawer anchors wrap with the same index. */
static void dock_wrap_icons(WScreen *scr, WDock *dock)
{
	WAppIcon *tmpaicon;
	WDrawerChain *dc;
	int i, j, ok, new_pix, new_index;

	for (i = 0; i < dock->max_icons; i++) {
		tmpaicon = dock->icon_array[i];
		if (tmpaicon == NULL)
			continue;
		if (onScreen(scr, tmpaicon->x_pos, tmpaicon->y_pos))
			continue;
		if (wDockEdgeHorizontal(dock->edge)) {
			new_pix = dock_wrap_along_pixel(dock, tmpaicon->x_pos);
			new_index = (new_pix - dock->x_pos) / ICON_SIZE;
			if (!onScreen(scr, new_pix, tmpaicon->y_pos))
				continue;
			ok = 1;
			for (j = 0; j < dock->max_icons; j++) {
				if (dock->icon_array[j] != NULL &&
				    wDockAlongIndex(dock->icon_array[j], dock->edge) == new_index) {
					ok = 0;
					break;
				}
			}
			if (!ok || getDrawerAtAlong(scr, new_index) != NULL)
				continue;
			wDockReattachIcon(dock, tmpaicon, new_index, tmpaicon->yindex);
		} else {
			new_pix = dock_wrap_along_pixel(dock, tmpaicon->y_pos);
			new_index = (new_pix - dock->y_pos) / ICON_SIZE;
			if (!onScreen(scr, tmpaicon->x_pos, new_pix))
				continue;
			ok = 1;
			for (j = 0; j < dock->max_icons; j++) {
				if (dock->icon_array[j] != NULL &&
				    wDockAlongIndex(dock->icon_array[j], dock->edge) == new_index) {
					ok = 0;
					break;
				}
			}
			if (!ok || getDrawerAtAlong(scr, new_index) != NULL)
				continue;
			wDockReattachIcon(dock, tmpaicon, tmpaicon->xindex, new_index);
		}
	}

	for (dc = scr->drawers; dc != NULL; dc = dc->next) {
		WAppIcon *main_tile = dc->adrawer->icon_array[0];

		if (onScreen(scr, main_tile->x_pos, main_tile->y_pos))
			continue;
		if (wDockEdgeHorizontal(dock->edge)) {
			new_pix = dock_wrap_along_pixel(dock, main_tile->x_pos);
			new_index = (new_pix - dock->x_pos) / ICON_SIZE;
			if (!onScreen(scr, new_pix, main_tile->y_pos))
				continue;
			ok = 1;
			for (j = 0; j < dock->max_icons; j++) {
				if (dock->icon_array[j] != NULL &&
				    wDockAlongIndex(dock->icon_array[j], dock->edge) == new_index) {
					ok = 0;
					break;
				}
			}
			if (!ok || getDrawerAtAlong(scr, new_index) != NULL)
				continue;
			moveDock(dc->adrawer, new_pix, dc->adrawer->y_pos);
		} else {
			new_pix = dock_wrap_along_pixel(dock, main_tile->y_pos);
			new_index = (new_pix - dock->y_pos) / ICON_SIZE;
			if (!onScreen(scr, main_tile->x_pos, new_pix))
				continue;
			ok = 1;
			for (j = 0; j < dock->max_icons; j++) {
				if (dock->icon_array[j] != NULL &&
				    wDockAlongIndex(dock->icon_array[j], dock->edge) == new_index) {
					ok = 0;
					break;
				}
			}
			if (!ok || getDrawerAtAlong(scr, new_index) != NULL)
				continue;
			moveDock(dc->adrawer, dc->adrawer->x_pos, new_pix);
		}
	}
}

static void handleDockMove(WDock *dock, WAppIcon *aicon, xcb_generic_event_t *event)
{
	WScreen *scr = dock->screen_ptr;
	int ofs_x = ((xcb_button_press_event_t *)event)->event_x, ofs_y = ((xcb_button_press_event_t *)event)->event_y;
	WIcon *icon = aicon->icon;
	WAppIcon *tmpaicon;
	int x = aicon->x_pos, y = aicon->y_pos;;
	int shad_x = x, shad_y = y;
	xcb_generic_event_t *ev = NULL;
	int grabbed = 0, done, i, idx;
	bool edge_switched = false;
	xcb_pixmap_t ghost = XCB_NONE;
	int superfluous = w_global.config->superfluous;	/* we catch it to avoid problems */

	if (wXGrabPointer(wdisp->conn, aicon->icon->core->window, true, XCB_EVENT_MASK_BUTTON_MOTION
			 | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS, XCB_GRAB_MODE_ASYNC,
			 XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE, XCB_CURRENT_TIME) != WXGrabSuccess)
		wwarning("pointer grab failed for dock move");

	if (dock->type == WM_DRAWER) {
		xcb_window_t wins[2];
		wins[0] = icon->core->window;
		wins[1] = scr->dock_shadow;
		wXRestackWindows(wdisp->conn, wins, 2);
		xcb_configure_window(wdisp->conn, scr->dock_shadow, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, (const uint32_t[]){ aicon->x_pos, aicon->y_pos, ICON_SIZE, ICON_SIZE });
		if (superfluous) {
			if (icon->pixmap!=XCB_NONE)
				ghost = MakeGhostIcon(scr, icon->pixmap);
			else
				ghost = MakeGhostIcon(scr, icon->core->window);

			xcb_change_window_attributes(wdisp->conn, scr->dock_shadow, XCB_CW_BACK_PIXMAP, (const uint32_t[]){ ghost });
			xcb_clear_area(wdisp->conn, 0, scr->dock_shadow, 0, 0, 0, 0);
		}
		xcb_map_window(wdisp->conn, scr->dock_shadow);
	}

	done = 0;
	while (!done) {
		free(ev);
		ev = NULL;
		WMMaskEvent(wdisp->conn, XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS
			    | XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_ENTER_WINDOW, &ev);
		if (!ev)
			break;
		switch ((ev->response_type & 0x7f)) {
		case XCB_EXPOSE:
			WMHandleEvent(ev);
			break;

                case XCB_ENTER_NOTIFY:
                        /* It means the cursor moved so fast that it entered
                         * something else (if moving slowly, it would have
                         * stayed in the dock that is being moved. Ignore such
                         * "spurious" EnterNotifiy's */
                        break;

		case XCB_MOTION_NOTIFY:
			if (!grabbed) {
				if (abs(ofs_x - (((xcb_motion_notify_event_t *)ev)->event_x)) >= MOVE_THRESHOLD
				    || abs(ofs_y - (((xcb_motion_notify_event_t *)ev)->event_y)) >= MOVE_THRESHOLD) {
					wXChangeActivePointerGrab(wdisp->conn, XCB_EVENT_MASK_BUTTON_MOTION
								 | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS,
								 wdisp->cursor[WCUR_MOVE], XCB_CURRENT_TIME);
					grabbed = 1;
				}
				break;
			}
			switch (dock->type) {
			case WM_CLIP:
				x = (((xcb_motion_notify_event_t *)ev)->root_x) - ofs_x;
				y = (((xcb_motion_notify_event_t *)ev)->root_y) - ofs_y;
				wScreenKeepInside(scr, &x, &y, ICON_SIZE, ICON_SIZE);
				moveDock(dock, x, y);
				break;
			case WM_DOCK:
			{
				WDockEdge new_edge;

				x = (((xcb_motion_notify_event_t *)ev)->root_x) - ofs_x;
				y = (((xcb_motion_notify_event_t *)ev)->root_y) - ofs_y;
				new_edge = wDockEdgeAtPointer(scr,
							      ((xcb_motion_notify_event_t *)ev)->root_x,
							      ((xcb_motion_notify_event_t *)ev)->root_y,
							      dock->edge);
				if (new_edge != dock->edge) {
					edge_switched = true;
					wDockSetEdge(dock, new_edge);
					wArrangeIcons(scr, false);
				}
				/* the pinned axis stays on the edge, the
				 * free axis follows the pointer */
				dock_keep_on_edge(scr, dock->edge, edge_switched, &x, &y);
				moveDock(dock, x, y);
				if (wdisp->flags.wrap_appicons_in_dock)
					dock_wrap_icons(scr, dock);
				break;
			}
			case WM_DRAWER:
			{
				WDock *real_dock = dock->screen_ptr->dock;
				bool snapped;
				int ix, iy;
				x = (((xcb_motion_notify_event_t *)ev)->root_x) - ofs_x;
				y = (((xcb_motion_notify_event_t *)ev)->root_y) - ofs_y;
				snapped = wDockSnapIcon(real_dock, aicon, x, y, &ix, &iy, true);
				if (snapped) {
					shad_x = real_dock->x_pos + ix * w_global.config->icon_size;
					shad_y = real_dock->y_pos + iy * w_global.config->icon_size;
					xcb_configure_window(wdisp->conn, scr->dock_shadow, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ shad_x, shad_y });
				}
				moveDock(dock, x, y);
				break;
			}
			}
			break;

		case XCB_BUTTON_PRESS:
			break;

		case XCB_BUTTON_RELEASE:
			if ((((xcb_button_press_event_t *)ev)->detail) != ((xcb_button_press_event_t *)event)->detail)
				break;
			wXUngrabPointer(wdisp->conn, XCB_CURRENT_TIME);
				if (dock->type == WM_DRAWER) {
					xcb_window_t wins[dock->icon_count];
					char axis;
					int sign, offset_index;
					int from_x, from_y, to_x, to_y;

					wDockGeomAcrossSign(dock->edge, &axis, &sign);

					memset(wins, 0, sizeof(wins)); /* XCB_NONE, skipped by slide_windows */

					/*
					 * The across index of the icons is negative when the
					 * drawer opens toward the negative axis (right dock
					 * or bottom dock). Use an offset here so we can have
					 * an always positive index for the storage in the
					 * 'wins' array.
					 */
					offset_index = (sign < 0) ? dock->icon_count - 1 : 0;

					for (i = 0; i < dock->max_icons; i++) {
						tmpaicon = dock->icon_array[i];
						if (tmpaicon == NULL)
							continue;
						idx = wDockAcrossIndex(tmpaicon, dock->edge) + offset_index;
						if (idx < 0 || idx >= dock->icon_count) {
							wwarning("handleDockMove: across index %d out of range [0..%d]",
								 idx, dock->icon_count - 1);
							continue;
						}
						wins[idx] = tmpaicon->icon->core->window;
					}

					/* the slide origin is the extreme
					 * icon on the open side */
					if (axis == 'x') {
						from_x = (sign < 0) ? x - (dock->icon_count - 1) * ICON_SIZE : x;
						to_x = (sign < 0) ? shad_x - (dock->icon_count - 1) * ICON_SIZE : shad_x;
						from_y = y;
						to_y = shad_y;
					} else {
						from_x = x;
						to_x = shad_x;
						from_y = (sign < 0) ? y - (dock->icon_count - 1) * ICON_SIZE : y;
						to_y = (sign < 0) ? shad_y - (dock->icon_count - 1) * ICON_SIZE : shad_y;
					}
					slide_windows(wins, dock->icon_count,
						      from_x, from_y, to_x, to_y, axis);
					xcb_unmap_window(wdisp->conn, scr->dock_shadow);
					moveDock(dock, shad_x, shad_y);
					xcb_configure_window(wdisp->conn, scr->dock_shadow, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, (const uint32_t[]){ ICON_SIZE, ICON_SIZE });
				}
			done = 1;
			break;
		}

		free(ev);
		ev = NULL;
	}
	if (superfluous) {
		if (ghost != XCB_NONE)
			xcb_free_pixmap(wdisp->conn, ghost);
		xcb_change_window_attributes(wdisp->conn, scr->dock_shadow, XCB_CW_BACK_PIXEL, (const uint32_t[]){ scr->white_pixel });
	}
}


static int getClipButton(int px, int py)
{
	int pt = (CLIP_BUTTON_SIZE + 2) * ICON_SIZE / 64;

	if (px < 0 || py < 0 || px >= ICON_SIZE || py >= ICON_SIZE)
		return CLIP_IDLE;

	if (py <= pt - ((int)ICON_SIZE - 1 - px))
		return CLIP_FORWARD;
	else if (px <= pt - ((int)ICON_SIZE - 1 - py))
		return CLIP_REWIND;

	return CLIP_IDLE;
}

static void handleClipChangeWorkspace(WScreen *scr, xcb_generic_event_t *event)
{
	xcb_generic_event_t *ev = NULL;
	int done, direction, new_ws;
	int new_dir;
	WDock *clip = scr->clip_icon->dock;

	direction = getClipButton(((xcb_button_press_event_t *)event)->event_x, ((xcb_button_press_event_t *)event)->event_y);

	clip->lclip_button_pushed = direction == CLIP_REWIND;
	clip->rclip_button_pushed = direction == CLIP_FORWARD;

	wClipIconPaint(scr->clip_icon);
	done = 0;
	while (!done) {
		free(ev);
		ev = NULL;
		WMMaskEvent(wdisp->conn, XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS, &ev);
		if (!ev)
			break;
		switch ((ev->response_type & 0x7f)) {
		case XCB_EXPOSE:
			WMHandleEvent(ev);
			break;

		case XCB_MOTION_NOTIFY:
			new_dir = getClipButton((((xcb_motion_notify_event_t *)ev)->event_x), (((xcb_motion_notify_event_t *)ev)->event_y));
			if (new_dir != direction) {
				direction = new_dir;
				clip->lclip_button_pushed = direction == CLIP_REWIND;
				clip->rclip_button_pushed = direction == CLIP_FORWARD;
				wClipIconPaint(scr->clip_icon);
			}
			break;

		case XCB_BUTTON_PRESS:
			break;

		case XCB_BUTTON_RELEASE:
			if ((((xcb_button_press_event_t *)ev)->detail) == ((xcb_button_press_event_t *)event)->detail)
				done = 1;
		}

		free(ev);
		ev = NULL;
	}

	clip->lclip_button_pushed = 0;
	clip->rclip_button_pushed = 0;

	new_ws = w_global.config->ws_advance || (((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_CONTROL);

	/* SkipEmptyWorkspaces: take the same occupied-only walk as the Clip
	 * scroll-wheel, suppressing the ws_advance/ControlMask path below. */
	if (w_global.config->skip_empty_workspaces) {
		if (direction == CLIP_FORWARD)
			wWorkspaceRelativeChangeFromPointer(scr, 1);
		else if (direction == CLIP_REWIND)
			wWorkspaceRelativeChangeFromPointer(scr, -1);
	} else if (direction == CLIP_FORWARD) {
		if (scr->current_workspace < scr->workspace_count - 1)
			wWorkspaceChangeFromPointer(scr, scr->current_workspace + 1);
		else if (new_ws && scr->current_workspace < MAX_WORKSPACES - 1)
			wWorkspaceChangeFromPointer(scr, scr->current_workspace + 1);
		else if (w_global.config->ws_cycle)
			wWorkspaceChangeFromPointer(scr, 0);
	} else if (direction == CLIP_REWIND) {
		if (scr->current_workspace > 0)
			wWorkspaceChangeFromPointer(scr, scr->current_workspace - 1);
		else if (scr->current_workspace == 0 && w_global.config->ws_cycle)
			wWorkspaceChangeFromPointer(scr, scr->workspace_count - 1);
	}

	wClipIconPaint(scr->clip_icon);
}

static void iconMouseDown(WObjDescriptor *desc, xcb_generic_event_t *event)
{
	WAppIcon *aicon = desc->parent;
	WDock *dock = aicon->dock;
	WScreen *scr = aicon->icon->core->screen_ptr;

	if (aicon->editing || WCHECK_STATE(WSTATE_MODAL))
		return;

	scr->last_dock = dock;

	if (dock->menu->flags.mapped)
		wMenuUnmap(dock->menu);

	if (IsDoubleClick(scr, event)) {
		/* double-click was not in the main clip icon */
		if (dock->type != WM_CLIP || aicon->xindex != 0 || aicon->yindex != 0
		    || getClipButton(((xcb_button_press_event_t *)event)->event_x, ((xcb_button_press_event_t *)event)->event_y) == CLIP_IDLE) {
			iconDblClick(desc, event);
			return;
		}
	}

	if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_1) {
		if (((xcb_button_press_event_t *)event)->state & MOD_MASK)
			wDockLower(dock);
		else
			wDockRaise(dock);

		if ((((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_SHIFT) && aicon != scr->clip_icon && dock->type != WM_DOCK) {
			wIconSelect(aicon->icon);
			return;
		}

		if (aicon->yindex == 0 && aicon->xindex == 0) {
			if (getClipButton(((xcb_button_press_event_t *)event)->event_x, ((xcb_button_press_event_t *)event)->event_y) != CLIP_IDLE &&
				(dock->type == WM_CLIP || (dock->type == WM_DOCK && wdisp->flags.clip_merged_in_dock)))
				handleClipChangeWorkspace(scr, event);
			else
				handleDockMove(dock, aicon, event);
		} else {
			bool hasMoved = wHandleAppIconMove(aicon, event);
			if (w_global.config->single_click && !hasMoved)
				iconDblClick(desc, event);
		}
	} else if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_2 && aicon == scr->clip_icon) {
		if (!scr->clip_ws_menu)
			scr->clip_ws_menu = wWorkspaceMenuMake(scr, false);

		if (scr->clip_ws_menu) {
			WMenu *wsMenu = scr->clip_ws_menu;
			int xpos;

			wWorkspaceMenuUpdate(scr, wsMenu);

			xpos = ((xcb_button_press_event_t *)event)->root_x - wsMenu->frame->core->width / 2 - 1;
			if (xpos < 0) {
				xpos = 0;
			} else if (xpos + wsMenu->frame->core->width > scr->scr_width - 2) {
				xpos = scr->scr_width - wsMenu->frame->core->width - 4;
			}
			wMenuMapAt(wsMenu, xpos, ((xcb_button_press_event_t *)event)->root_y + 2, false);

			desc = &wsMenu->menu->descriptor;
			event->response_type |= 0x80;
			(*desc->handle_mousedown) (desc, event);
		}
	} else if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_2 && dock->type == WM_CLIP &&
		   (((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_SHIFT) && aicon != scr->clip_icon) {
		wClipMakeIconOmnipresent(aicon, !aicon->omnipresent);
	} else if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_3) {
		if ((event->response_type & 0x80) &&
		    wXGrabPointer(wdisp->conn, aicon->icon->core->window, true, XCB_EVENT_MASK_BUTTON_MOTION
				 | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS, XCB_GRAB_MODE_ASYNC,
				 XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE, XCB_CURRENT_TIME) != WXGrabSuccess) {
			wwarning("pointer grab failed for dockicon menu");
			return;
		}

		openDockMenu(dock, aicon, event);
	} else if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_2) {
		WAppIcon *btn = desc->parent;

		if (!btn->launching && (!btn->running || (((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_CONTROL)))
			launchDockedApplication(btn, true);
	} else if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_4 && dock->type == WM_CLIP) {
		wWorkspaceRelativeChangeFromPointer(scr, 1);
	} else if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_5 && dock->type == WM_CLIP) {
		wWorkspaceRelativeChangeFromPointer(scr, -1);
	}
}

static void clipEnterNotify(WObjDescriptor *desc, xcb_generic_event_t *event)
{
	WAppIcon *btn = (WAppIcon *) desc->parent;
	WDock *dock, *tmp;
	WScreen *scr;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) event;

	assert((event->response_type & 0x7f) == XCB_ENTER_NOTIFY);

	if (desc->parent_type != WCLASS_DOCK_ICON)
		return;

	scr = btn->icon->core->screen_ptr;
	dock = btn->dock;

	if (dock == NULL)
		return;

	/* The auto raise/lower code */
	tmp = (dock->type == WM_DRAWER ? scr->dock : dock);
	WMDeleteTimerHandlerPtr(&tmp->auto_lower_magic);
	if (tmp->auto_raise_lower && !tmp->auto_raise_magic)
		tmp->auto_raise_magic = WMAddTimerHandler(w_global.config->clip_auto_raise_delay, clipAutoRaise, (void *) tmp);

	if (dock->type != WM_CLIP && dock->type != WM_DRAWER)
		return;

	/* The auto expand/collapse code */
	WMDeleteTimerHandlerPtr(&dock->auto_collapse_magic);
	if (dock->auto_collapse && !dock->auto_expand_magic)
		dock->auto_expand_magic = WMAddTimerHandler(w_global.config->clip_auto_expand_delay, clipAutoExpand, (void *)dock);
}

static void clipLeave(WDock *dock)
{
	xcb_generic_event_t *event = NULL;
	WObjDescriptor *desc = NULL;
	WDock *tmp;

	if (dock == NULL)
		return;

	if (WXEvqTakeTyped(wdisp->conn, XCB_ENTER_NOTIFY, XCB_NONE, &event) != false) {
		desc = WMHashGet(wdisp->context.client_win, (void *)(uintptr_t) (((xcb_enter_notify_event_t *)event)->event));
		if (desc != NULL
		    && desc->parent_type == WCLASS_DOCK_ICON
		    && ((WAppIcon *) desc->parent)->dock == dock) {
			/* We haven't left the dock/clip/drawer yet */
			WXEvqPutBack(event);
			return;
		}

		WXEvqPutBack(event);
	} else {
		/* We entered a withdrawn window, so we're still in Clip */
		return;
	}

	tmp = (dock->type == WM_DRAWER ? dock->screen_ptr->dock : dock);
	WMDeleteTimerHandlerPtr(&tmp->auto_raise_magic);
	if (tmp->auto_raise_lower && !tmp->auto_lower_magic)
		tmp->auto_lower_magic = WMAddTimerHandler(w_global.config->clip_auto_lower_delay, clipAutoLower, (void *)tmp);

	if (dock->type != WM_CLIP && dock->type != WM_DRAWER)
		return;

	WMDeleteTimerHandlerPtr(&dock->auto_expand_magic);
	if (dock->auto_collapse && !dock->auto_collapse_magic)
		dock->auto_collapse_magic = WMAddTimerHandler(w_global.config->clip_auto_collapse_delay, clipAutoCollapse, (void *)dock);
}

static void clipLeaveNotify(WObjDescriptor *desc, xcb_generic_event_t *event)
{
	WAppIcon *btn = (WAppIcon *) desc->parent;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) event;

	assert((event->response_type & 0x7f) == XCB_LEAVE_NOTIFY);

	if (desc->parent_type != WCLASS_DOCK_ICON)
		return;

	clipLeave(btn->dock);
}

static void clipAutoCollapse(void *cdata)
{
	WDock *dock = (WDock *) cdata;

	if (dock->type != WM_CLIP && dock->type != WM_DRAWER)
		return;

	if (dock->auto_collapse) {
		dock->collapsed = 1;
		wDockHideIcons(dock);
	}
	dock->auto_collapse_magic = NULL;
}

static void clipAutoExpand(void *cdata)
{
	WDock *dock = (WDock *) cdata;

	if (dock->type != WM_CLIP && dock->type != WM_DRAWER)
		return;

	if (dock->auto_collapse) {
		dock->collapsed = 0;
		wDockShowIcons(dock);
	}
	dock->auto_expand_magic = NULL;
}

static void clipAutoLower(void *cdata)
{
	WDock *dock = (WDock *) cdata;

	if (dock->auto_raise_lower)
		wDockLower(dock);

	dock->auto_lower_magic = NULL;
}

static void clipAutoRaise(void *cdata)
{
	WDock *dock = (WDock *) cdata;

	if (dock->auto_raise_lower)
		wDockRaise(dock);

	dock->auto_raise_magic = NULL;
}

static bool iconCanBeOmnipresent(WAppIcon *aicon)
{
	WScreen *scr = aicon->icon->core->screen_ptr;
	WDock *clip;
	WAppIcon *btn;
	int i, j;

	for (i = 0; i < scr->workspace_count; i++) {
		clip = scr->workspaces[i]->clip;

		if (clip == aicon->dock)
			continue;

		if (clip->icon_count + scr->global_icon_count >= clip->max_icons)
			return false;	/* Clip is full in some workspace */

		for (j = 0; j < clip->max_icons; j++) {
			btn = clip->icon_array[j];
			if (btn && btn->xindex == aicon->xindex && btn->yindex == aicon->yindex)
				return false;
		}
	}

	return true;
}

int wClipMakeIconOmnipresent(WAppIcon *aicon, int omnipresent)
{
	WScreen *scr = aicon->icon->core->screen_ptr;
	WAppIconChain *new_entry, *tmp, *tmp1;
	int status = WO_SUCCESS;

	if ((scr->dock && aicon->dock == scr->dock) || aicon == scr->clip_icon)
		return WO_NOT_APPLICABLE;

	if (aicon->omnipresent == omnipresent)
		return WO_SUCCESS;

	if (omnipresent) {
		if (iconCanBeOmnipresent(aicon)) {
			aicon->omnipresent = 1;
			new_entry = wmalloc(sizeof(WAppIconChain));
			new_entry->aicon = aicon;
			new_entry->next = scr->global_icons;
			scr->global_icons = new_entry;
			scr->global_icon_count++;
		} else {
			aicon->omnipresent = 0;
			status = WO_FAILED;
		}
	} else {
		aicon->omnipresent = 0;
		if (!scr->global_icons) {
			wwarning("wClipMakeIconOmnipresent: global_icons is NULL");
			return WO_FAILED;
		}
		if (aicon == scr->global_icons->aicon) {
			tmp = scr->global_icons->next;
			wfree(scr->global_icons);
			scr->global_icons = tmp;
			scr->global_icon_count--;
		} else {
			tmp = scr->global_icons;
			while (tmp->next) {
				if (tmp->next->aicon == aicon) {
					tmp1 = tmp->next->next;
					wfree(tmp->next);
					tmp->next = tmp1;
					scr->global_icon_count--;
					break;
				}
				tmp = tmp->next;
			}
		}
	}

	wAppIconPaint(aicon);

	return status;
}

static void drawerAppendToChain(WScreen *scr, WDock *drawer)
{
	WDrawerChain **where_to_add;

	where_to_add = &scr->drawers;
	while ((*where_to_add) != NULL) {
		where_to_add = &(*where_to_add)->next;
	}

	*where_to_add = wmalloc(sizeof(WDrawerChain));
	(*where_to_add)->adrawer = drawer;
	(*where_to_add)->next = NULL;
	scr->drawer_count++;
}


static void drawerRemoveFromChain(WScreen *scr, WDock *drawer)
{
	WDrawerChain *next, **to_remove;

	to_remove = &scr->drawers;
	while (true) {
		if (*to_remove == NULL) {
			wwarning("The drawer to be removed can not be found.");
			return;
		}
		if ((*to_remove)->adrawer == drawer)
			break;

		to_remove = &(*to_remove)->next;
	}
	next = (*to_remove)->next;
	wfree(*to_remove);
	*to_remove = next;
	scr->drawer_count--;
}


/* Don't free the returned string. Duplicate it. */
static char * findUniqueName(WScreen *scr, const char *instance_basename)
{
	static char buffer[128];
	WDrawerChain *dc;
	int i;
	bool already_in_use = true;

#define UNIQUE_NAME_WATCHDOG 128
	for (i = 0; already_in_use && i < UNIQUE_NAME_WATCHDOG; i++) {
		snprintf(buffer, sizeof buffer, "%s%d", instance_basename, i);

		already_in_use = false;

		for (dc = scr->drawers; dc != NULL; dc = dc->next) {
			if (!strncmp(dc->adrawer->icon_array[0]->wm_instance, buffer,
					sizeof buffer)) {
				already_in_use = true;
				break;
			}
		}
	}

	if (i == UNIQUE_NAME_WATCHDOG)
		wwarning("Couldn't find a unique name for drawer in %d attempts.", i);
#undef UNIQUE_NAME_WATCHDOG

	return buffer;
}


static void drawerIconExpose(WObjDescriptor *desc, xcb_generic_event_t *event)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) event;

	wDrawerIconPaint((WAppIcon *) desc->parent);
}


static int addADrawer(WScreen *scr)
{
	int i, along, sig, found, ax, ay;
	WDock *drawer, *dock = scr->dock;
	WDrawerChain *dc;
	char can_be_here[2 * dock->max_icons - 1];

	if (dock->icon_count + scr->drawer_count >= dock->max_icons)
		return -1;

	for (along = -dock->max_icons + 1; along < dock->max_icons; along++)
		can_be_here[along + dock->max_icons - 1] = true;
	for (i = 0; i < dock->max_icons; i++) {
		if (dock->icon_array[i] != NULL) {
			int idx = wDockAlongIndex(dock->icon_array[i], dock->edge)
				  + dock->max_icons - 1;
			if (idx >= 0 && idx < 2 * dock->max_icons - 1)
				can_be_here[idx] = false;
		}
	}
	for (dc = scr->drawers; dc != NULL; dc = dc->next) {
		if (wDockEdgeHorizontal(dock->edge))
			along = (int) ((dc->adrawer->x_pos - dock->x_pos) / ICON_SIZE);
		else
			along = (int) ((dc->adrawer->y_pos - dock->y_pos) / ICON_SIZE);
		can_be_here[along + dock->max_icons - 1] = false;
	}

	found = false;
	for (sig = 1; !found && sig > -2; sig -= 2) // 1, then -1
	{
		for (along = sig; sig * along < dock->max_icons; along += sig)
		{
			ax = dock->x_pos + (wDockEdgeHorizontal(dock->edge) ?
					    along * ICON_SIZE : 0);
			ay = dock->y_pos + (wDockEdgeHorizontal(dock->edge) ?
					    0 : along * ICON_SIZE);
			if (can_be_here[along + dock->max_icons - 1] &&
				onScreen(scr, ax, ay))
			{
				found = true;
				break;
			}
		}
	}

	if (!found)
		/* This can happen even when dock->icon_count + scr->drawer_count
		 * < dock->max_icons when the dock is not aligned on an
		 * ICON_SIZE multiple, as some space is lost above and under it */
		return -1;

	drawer = wDockCreate(scr, WM_DRAWER, NULL);
	drawer->lowered = scr->dock->lowered;
	if (!drawer->lowered)
		ChangeStackingLevel(drawer->icon_array[0]->icon->core, WMDockLevel);
	else
		ChangeStackingLevel(drawer->icon_array[0]->icon->core, WMNormalLevel);
	drawer->auto_raise_lower = scr->dock->auto_raise_lower;
	if (wDockEdgeHorizontal(dock->edge)) {
		drawer->x_pos = dock->x_pos + ICON_SIZE * along;
		drawer->y_pos = dock->y_pos;
	} else {
		drawer->x_pos = dock->x_pos;
		drawer->y_pos = dock->y_pos + ICON_SIZE * along;
	}
	drawer->icon_array[0]->xindex = 0;
	drawer->icon_array[0]->yindex = 0;
	drawer->icon_array[0]->x_pos = drawer->x_pos;
	drawer->icon_array[0]->y_pos = drawer->y_pos;
	xcb_configure_window(wdisp->conn, drawer->icon_array[0]->icon->core->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ drawer->icon_array[0]->x_pos, drawer->icon_array[0]->y_pos });

	return 0;
}


static void addADrawerCallback(WMenu *menu, WMenuEntry *entry)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) menu;

	assert(entry->clientdata!=NULL);
	addADrawer(((WAppIcon *) entry->clientdata)->dock->screen_ptr);
}


static void drawerDestroy(WDock *drawer)
{
	WScreen *scr;
	int i;
	WAppIcon *aicon = NULL;
	WMArray *icons;

	if (drawer == NULL)
		return;

	scr = drawer->screen_ptr;

	/* Note regarding menus: we can't delete any dock/clip/drawer menu, because
	 * that would (attempt to) wfree some memory in gettext library (see menu
	 * entries that have several "versions", such like "Hide" and "Unhide"). */
	wDefaultPurgeInfo(drawer->icon_array[0]->wm_instance,
			drawer->icon_array[0]->wm_class);

	if (drawer->icon_count == 2) {
		/* Drawer contains a single appicon: dock it where the drawer was */
		for (i = 1; i < drawer->max_icons; i++) {
			aicon = drawer->icon_array[i];
			if (aicon != NULL)
				break;
		}

		if (!aicon) {
			wwarning("drawerDestroy: icon_count==2 but no appicon found");
		} else {
			/* the along-axis slot the drawer occupied */
			if (wDockEdgeHorizontal(scr->dock->edge))
				wDockMoveIconBetweenDocks(drawer, scr->dock, aicon,
						(drawer->x_pos - scr->dock->x_pos) / ICON_SIZE, 0);
			else
				wDockMoveIconBetweenDocks(drawer, scr->dock, aicon,
						0, (drawer->y_pos - scr->dock->y_pos) / ICON_SIZE);
			xcb_configure_window(wdisp->conn, aicon->icon->core->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ drawer->x_pos, drawer->y_pos });
			xcb_map_window(wdisp->conn, aicon->icon->core->window);
		}
	} else if (drawer->icon_count > 2) {
		icons = WMCreateArray(drawer->icon_count - 1);
		for (i = 1; i < drawer->max_icons; i++) {
			aicon = drawer->icon_array[i];
			if (aicon == NULL)
				continue;
			WMAddToArray(icons, aicon);
		}
		removeIcons(icons, drawer);
	}

	WMDeleteTimerHandlerPtr(&drawer->auto_collapse_magic);
	WMDeleteTimerHandlerPtr(&drawer->auto_lower_magic);

	wAppIconDestroy(drawer->icon_array[0]);
	wfree(drawer->icon_array);
	drawer->icon_array = NULL;

	drawerRemoveFromChain(scr, drawer);
	if (scr->last_dock == drawer)
		scr->last_dock = NULL;
	if (scr->attracting_drawer == drawer)
		scr->attracting_drawer = NULL;

	wfree(drawer);
}


static void removeDrawerCallback(WMenu *menu, WMenuEntry *entry)
{
	WDock *dock = ((WAppIcon*)entry->clientdata)->dock;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) menu;

	assert(dock != NULL);

	if (dock->icon_count > 2) {
		if (wMessageDialog(dock->screen_ptr, _("Drawer"),
					_("All icons in this drawer will be detached!"),
					_("OK"), _("Cancel"), NULL) != WAPRDefault)
			return;
	}
	drawerDestroy(dock);
}


#define DRAWER_ARROW_PX(v) ((v) * ICON_SIZE / 64)
#define DRAWER_ARROW_REACH 11

void wDrawerIconPaint(WAppIcon *dicon)
{
	xcb_window_t win = dicon->icon->core->window;
	WScreen *scr = dicon->icon->core->screen_ptr;
	WMColor *color;
	cairo_t *cr;
	int c = ICON_SIZE / 2;
	int tip = DRAWER_ARROW_PX(5);
	int base = DRAWER_ARROW_PX(10);

	wIconPaint(dicon->icon);

	if (!dicon->dock->collapsed)
		color = scr->clip_title_color[CLIP_NORMAL];
	else
		color = scr->clip_title_color[CLIP_COLLAPSED];

	cr = WCairoBeginForDrawable(scr->wmscreen, win);
	if (!cr)
		return;

	/* the arrow points into the screen, at the drawer contents */
	switch (dicon->dock->edge) {
	case WDOCK_EDGE_RIGHT:
		fillArrow(cr, color,
			  base, c - tip,
			  base, c + tip,
			  tip,  c);
		break;
	case WDOCK_EDGE_LEFT:
		fillArrow(cr, color,
			  ICON_SIZE - 1 - base, c - tip,
			  ICON_SIZE - 1 - base, c + tip,
			  ICON_SIZE - 1 - tip,  c);
		break;
	case WDOCK_EDGE_TOP:
		fillArrow(cr, color,
			  c - tip, ICON_SIZE - 1 - base,
			  c + tip, ICON_SIZE - 1 - base,
			  c, ICON_SIZE - 1 - tip);
		break;
	case WDOCK_EDGE_BOTTOM:
		fillArrow(cr, color,
			  c - tip, base,
			  c + tip, base,
			  c, tip);
		break;
	}
	WCairoEnd(cr);
}


RImage* wDrawerMakeTile(WScreen *scr, RImage *normalTile)
{
	RImage *tile = RCloneImage(normalTile);
	RColor dark;
	RColor light;
	int c, m = ICON_SIZE / 2;
	int reach, inner, half;

	dark.alpha = 0;
	dark.red = dark.green = dark.blue = 60;

	light.alpha = 0;
	light.red = light.green = light.blue = 80;

	c = m;
	reach = DRAWER_ARROW_PX(11);
	inner = DRAWER_ARROW_PX(4);
	half = DRAWER_ARROW_PX(7);

	/* arrow bevel pointing at the drawer contents */
	if (!scr->dock) {
		/* assume a right-side dock, the tile is built before the
		 * dock exists */
		ROperateLine(tile, RSubtractOperation, reach, c - half,
			inner, c, &dark);       /* / */
		ROperateLine(tile, RSubtractOperation, reach, c + half,
			inner, c, &dark);       /* \ */
		ROperateLine(tile, RAddOperation,      reach, c - half,
			reach, c + half, &light); /* | */
	} else {
		switch (scr->dock->edge) {
		case WDOCK_EDGE_LEFT:
			ROperateLine(tile, RSubtractOperation, ICON_SIZE-1 - reach, c - half,
				ICON_SIZE-1 - inner, c, &dark);      /* \ */
			ROperateLine(tile, RAddOperation,      ICON_SIZE-1 - reach, c + half,
				ICON_SIZE-1 - inner, c, &light);     /* / */
			ROperateLine(tile, RSubtractOperation, ICON_SIZE-1 - reach, c - half,
				ICON_SIZE-1 - reach, c + half, &dark); /* | */
			break;
		case WDOCK_EDGE_TOP:
			ROperateLine(tile, RSubtractOperation, c - half, ICON_SIZE-1 - reach,
				c, ICON_SIZE-1 - inner, &dark);       /* \ */
			ROperateLine(tile, RSubtractOperation, c + half, ICON_SIZE-1 - reach,
				c, ICON_SIZE-1 - inner, &dark);       /* / */
			ROperateLine(tile, RAddOperation,      c - half, ICON_SIZE-1 - reach,
				c + half, ICON_SIZE-1 - reach, &light); /* - */
			break;
		case WDOCK_EDGE_BOTTOM:
			ROperateLine(tile, RSubtractOperation, c - half, reach,
				c, inner, &dark);       /* / */
			ROperateLine(tile, RSubtractOperation, c + half, reach,
				c, inner, &dark);       /* \ */
			ROperateLine(tile, RAddOperation,      c - half, reach,
				c + half, reach, &light); /* - */
			break;
		case WDOCK_EDGE_RIGHT:
		default:
			ROperateLine(tile, RSubtractOperation, reach, c - half,
				inner, c, &dark);       /* / */
			ROperateLine(tile, RSubtractOperation, reach, c + half,
				inner, c, &dark);       /* \ */
			ROperateLine(tile, RAddOperation,      reach, c - half,
				reach, c + half, &light); /* | */
			break;
		}
	}
	return tile;
}


int wIsADrawer(WAppIcon *aicon)
{
	return aicon && aicon->dock &&
		aicon->dock->type == WM_DRAWER && aicon->dock->icon_array[0] == aicon;
}

void wDrawerIconTileShift(WIcon *icon, int *dx, int *dy)
{
	WScreen *scr;
	WDrawerChain *dc;

	*dx = 0;
	*dy = 0;

	if (!icon || !icon->core)
		return;

	scr = icon->core->screen_ptr;
	for (dc = scr->drawers; dc != NULL; dc = dc->next) {
		WDock *drawer = dc->adrawer;

		if (!drawer->icon_array[0] || drawer->icon_array[0]->icon != icon)
			continue;

		switch (drawer->edge) {
		case WDOCK_EDGE_RIGHT:
			*dx = DRAWER_ARROW_PX(DRAWER_ARROW_REACH) / 2;
			break;
		case WDOCK_EDGE_LEFT:
			*dx = -DRAWER_ARROW_PX(DRAWER_ARROW_REACH) / 2;
			break;
		case WDOCK_EDGE_TOP:
			*dy = -DRAWER_ARROW_PX(DRAWER_ARROW_REACH) / 2;
			break;
		case WDOCK_EDGE_BOTTOM:
			*dy = DRAWER_ARROW_PX(DRAWER_ARROW_REACH) / 2;
			break;
		}
		return;
	}
}


/* Find a drawer by the along-axis slot of its main tile relative to
 * the dock */
static WDock *getDrawerAtAlong(WScreen *scr, int along)
{
	WDrawerChain *dc;

	for (dc = scr->drawers; dc != NULL; dc = dc->next) {
		WDock *drawer = dc->adrawer;
		int delta;

		if (wDockEdgeHorizontal(scr->dock->edge))
			delta = drawer->x_pos - scr->dock->x_pos;
		else
			delta = drawer->y_pos - scr->dock->y_pos;

		if (delta == along * ICON_SIZE)
			return drawer;
	}
	return NULL;
}



/* Find the "hole" a moving appicon created when snapped into the
 * drawer. redocking is a boolean. If the moving appicon comes from the
 * drawer, drawer->icon_count is correct. If not, redocking is then False and
 * there are now drawer->icon_count plus one appicons in the drawer. */
static int indexOfHole(WDock *drawer, WAppIcon *moving_aicon, int redocking)
{
	int index_of_hole, i;

	/* Classic interview question...
	 *
	 * We have n-1 (n = drawer->icon_count-1 or drawer->icon_count, see
	 * redocking) appicons, whose across indices are unique in [1..n]. One
	 * is missing: that's where the ghost of the moving appicon is, that's
	 * what the function should return.
	 *
	 * We compute 1+2+...+n (this sum is equal to n*(n+1)/2), we subtract to
	 * this sum the across index of each of the n-1 appicons, and we get the
	 * correct index! */

	if (redocking) {
		index_of_hole = (drawer->icon_count - 1) * drawer->icon_count / 2;
	} else {
		index_of_hole = drawer->icon_count * (drawer->icon_count + 1) / 2;
	}
	index_of_hole *= wDockAcrossSign(drawer->edge);

	for (i = 1; i < drawer->max_icons; i++) {
		if (drawer->icon_array[i] && drawer->icon_array[i] != moving_aicon)
			index_of_hole -= wDockAcrossIndex(drawer->icon_array[i], drawer->edge);
	}
	/* wmessage(" Index of the moving appicon is %d (%sredocking)", index_of_hole, (redocking ? "" : "not ")); */
	if (abs(index_of_hole) > abs(drawer->icon_count) - (redocking ? 1 : 0))
		wwarning(" index_of_hole is too large ! (%d greater than %d)",
			index_of_hole, abs(drawer->icon_count) - (redocking ? 1 : 0));
	if (index_of_hole == 0)
		wwarning(" index_of_hole == 0 (%sredocking, icon_count == %d)", (redocking ? "" : "not "), drawer->icon_count);

	return index_of_hole;
}


/* Slide n icons by (dx_cells, dy_cells) cells and restack them along
 * the stack axis at the destination. The deltas are -1, 0 or +1. */
void wSlideAppiconsDelta(WAppIcon **appicons, int n, int dx_cells, int dy_cells, char axis)
{
	int i;
	int min_index = 9999, from_x = -1, from_y = -1, first = 0;
	WAppIcon *aicon;

	if (n < 1)
		return;

	xcb_window_t wins[n];

	memset(wins, 0, sizeof(wins)); /* XCB_NONE, skipped by slide_windows */

	for (i = 0; i < n; i++) {
		int idx;

		aicon = appicons[i];
		aicon->xindex += dx_cells;
		aicon->yindex += dy_cells;
		/* exactly one of the two changes, so this is the
		 * along-the-drawer coordinate */
		idx = aicon->xindex + aicon->yindex;
		if (idx < min_index) {
			min_index = idx;
			first = i;
			from_x = aicon->x_pos;
			from_y = aicon->y_pos;
		}
		aicon->x_pos += dx_cells * ICON_SIZE;
		aicon->y_pos += dy_cells * ICON_SIZE;
	}

	for (i = 0; i < n; i++) {
		int idx;

		aicon = appicons[i];
		idx = aicon->xindex + aicon->yindex - min_index;
		if (idx < 0 || idx >= n) {
			wwarning("wSlideAppiconsDelta: index %d out of range [0..%d]",
				 idx, n - 1);
			continue;
		}
		wins[idx] = aicon->icon->core->window;
	}
	aicon = appicons[first];
	slide_windows(wins, n, from_x, from_y, aicon->x_pos, aicon->y_pos, axis);
}

void wSlideAppiconsAcross(WAppIcon **appicons, int n, const WDock *drawer,
			  bool into_drawer)
{
	char axis;
	int sign;

	wDockGeomAcrossSign(drawer->edge, &axis, &sign);
	if (axis == 'x')
		wSlideAppiconsDelta(appicons, n, into_drawer ? sign : -sign, 0, axis);
	else
		wSlideAppiconsDelta(appicons, n, 0, into_drawer ? sign : -sign, axis);
}


void wDrawerFillTheGap(WDock *drawer, WAppIcon *aicon, bool redocking)
{
	int i, j;
	int index_of_hole = indexOfHole(drawer, aicon, redocking);
	WAppIcon *aicons_to_shift[drawer->icon_count];

	j = 0;
	for (i = 0; i < drawer->max_icons; i++) {
		WAppIcon *ai = drawer->icon_array[i];
		if (ai && ai != aicon &&
			abs(wDockAcrossIndex(ai, drawer->edge)) > abs(index_of_hole))
			aicons_to_shift[j++] = ai;
	}
	if (j != drawer->icon_count - abs(index_of_hole) - (redocking ? 1 : 0))
		wwarning("Removing aicon at index %d from %s: j=%d but should be %d",
			index_of_hole,
			drawer->icon_array[0] ? drawer->icon_array[0]->wm_instance : "(no main tile)",
			j, drawer->icon_count - abs(index_of_hole) - (redocking ? 1 : 0));
	wSlideAppiconsAcross(aicons_to_shift, j, drawer, false);
}


static void drawerConsolidateIcons(WDock *drawer)
{
	WAppIcon *aicons_to_shift[drawer->icon_count];
	int maxRemaining = 0;
	int sum = 0;
	int i;
	for (i = 0; i < drawer->max_icons; i++) {
		WAppIcon *ai = drawer->icon_array[i];
		if (ai == NULL)
			continue;
		sum += abs(wDockAcrossIndex(ai, drawer->edge));
		if (abs(wDockAcrossIndex(ai, drawer->edge)) > maxRemaining)
			maxRemaining = abs(wDockAcrossIndex(ai, drawer->edge));
	}
	while (sum != maxRemaining * (maxRemaining + 1) / 2) { // while there is a hole
		WAppIcon *ai;
		int n;
		// Look up for the hole at max position
		int maxDeleted;
		for (maxDeleted = maxRemaining - 1; maxDeleted > 0; maxDeleted--) {
			bool foundAppIconThere = false;
			for (i = 0; i < drawer->max_icons; i++) {
				WAppIcon *ai = drawer->icon_array[i];
				if (ai == NULL)
					continue;
				if (abs(wDockAcrossIndex(ai, drawer->edge)) == maxDeleted) {
					foundAppIconThere = true;
					break;
				}
			}
			if (!foundAppIconThere)
				break;
		}
		assert(maxDeleted > 0); // would mean while test is wrong
		n = 0;
		for (i = 0; i < drawer->max_icons; i++) {
			ai = drawer->icon_array[i];
			if (ai != NULL && abs(wDockAcrossIndex(ai, drawer->edge)) > maxDeleted)
				aicons_to_shift[n++] = ai;
		}
		assert(n == maxRemaining - maxDeleted); // for the code review ;-)
		wSlideAppiconsAcross(aicons_to_shift, n, drawer, false);
		// Efficient beancounting
		maxRemaining -= 1;
		sum -= n;
	}
}



/* similar to wDockRestoreState, but a lot a specific stuff too... */
static WDock * drawerRestoreState(WScreen *scr, WMPropList *drawer_state)
{
	WDock *drawer;
	WMPropList *apps, *value, *dock_state;
	WAppIcon *aicon;
	int count, i;

	if (!drawer_state)
		return NULL;

	make_keys();

	WMRetainPropList(drawer_state);

	/* Get the instance name, and create a drawer */
	value = WMGetFromPLDictionary(drawer_state, dName);
	drawer = wDockCreate(scr, WM_DRAWER, WMGetFromPLString(value));

	/* restore DnD command and paste command */
#ifdef USE_DOCK_XDND
	value = WMGetFromPLDictionary(drawer_state, dDropCommand);
	if (value && WMIsPLString(value))
		drawer->icon_array[0]->dnd_command = wstrdup(WMGetFromPLString(value));
#endif /* USE_DOCK_XDND */

	value = WMGetFromPLDictionary(drawer_state, dPasteCommand);
	if (value && WMIsPLString(value))
		drawer->icon_array[0]->paste_command = wstrdup(WMGetFromPLString(value));

	/* restore position */
	value = WMGetFromPLDictionary(drawer_state, dPosition);
	if (!value || !WMIsPLString(value))
		COMPLAIN("Position");
	else {
		int x, y, along_index;
		int limit = wDockAlongLimit(scr->dock);

		if (sscanf(WMGetFromPLString(value), "%i,%i", &x, &y) != 2)
			COMPLAIN("Position");

		/* Ignore the stored perpendicular coordinate: the drawer
		 * opens from the dock's current anchor and its along
		 * offset is re-derived from the stored along pixel. */
		if (wDockEdgeHorizontal(scr->dock->edge)) {
			along_index = (x - scr->dock->x_pos) / ICON_SIZE;
			if (along_index >= limit) {
				/* Here we should do something more intelligent, since it
				 * can happen even if the user hasn't hand-edited his
				 * G/D/State file (but uses a lower resolution). */
				along_index = limit - 1;
			}
			x = scr->dock->x_pos + along_index * ICON_SIZE;
			y = scr->dock->y_pos;
		} else {
			along_index = (y - scr->dock->y_pos) / ICON_SIZE;
			if (along_index >= limit) {
				/* Here we should do something more intelligent, since it
				 * can happen even if the user hasn't hand-edited his
				 * G/D/State file (but uses a lower resolution). */
				along_index = limit - 1;
			}
			y = scr->dock->y_pos + along_index * ICON_SIZE;
			x = scr->dock->x_pos;
		}
		moveDock(drawer, x, y);
	}

	/* restore dock properties (applist and others) */
	dock_state = WMGetFromPLDictionary(drawer_state, dDock);

	/* restore lowered/raised state: same as scr->dock, no matter what */
	drawer->lowered = scr->dock->lowered;
	if (!drawer->lowered)
		ChangeStackingLevel(drawer->icon_array[0]->icon->core, WMDockLevel);
	else
		ChangeStackingLevel(drawer->icon_array[0]->icon->core, WMNormalLevel);
	wRaiseFrame(drawer->icon_array[0]->icon->core);

	/* restore collapsed state */
	drawer->collapsed = 0;
	value = WMGetFromPLDictionary(dock_state, dCollapsed);
	if (value && strcasecmp(WMGetFromPLString(value), "YES") == 0) {
		drawer->collapsed = 1;
	}
	/* restore auto-collapsed state */
	value = WMGetFromPLDictionary(dock_state, dAutoCollapse);
	if (value && strcasecmp(WMGetFromPLString(value), "YES") == 0) {
		drawer->auto_collapse = 1;
		drawer->collapsed = 1;
	} else {
		drawer->auto_collapse = 0; // because wDockCreate sets it (drawers only)
	}

	/* restore auto-raise/lower state: same as scr->dock, no matter what */
	drawer->auto_raise_lower = scr->dock->auto_raise_lower;

	/* restore attract icons state */
	drawer->attract_icons = 0;
	value = WMGetFromPLDictionary(dock_state, dAutoAttractIcons);
	if (value && strcasecmp(WMGetFromPLString(value), "YES") == 0) {
		drawer->attract_icons = 1;
		scr->attracting_drawer = drawer;
	}

	/* application list */
	apps = WMGetFromPLDictionary(dock_state, dApplications);

	if (!apps) {
		goto finish;
	}

	count = WMGetPropListItemCount(apps);

	if (count == 0)
		goto finish;

	for (i=0; i<count; i++) {
		if (drawer->icon_count >= drawer->max_icons) {
			wwarning(_("there are too many icons stored in drawer. Ignoring what doesn't fit"));
			break;
		}

		value = WMGetFromPLArray(apps, i);
		aicon = restore_icon_state(scr, value, WM_DRAWER, drawer->icon_count,
					   drawer->edge);

		drawer->icon_array[drawer->icon_count] = aicon;

		if (aicon) {
			char axis;
			int sign, mag;

			/* don't trust the stored direction sign or axis:
			 * the drawer opens where the dock's current edge
			 * says, so re-derive the across index from the
			 * magnitudes */
			wDockGeomAcrossSign(drawer->edge, &axis, &sign);
			mag = WMAX(abs(aicon->xindex), abs(aicon->yindex));
			if (axis == 'x') {
				aicon->xindex = sign * mag;
				aicon->yindex = 0;
			} else {
				aicon->xindex = 0;
				aicon->yindex = sign * mag;
			}
			aicon->dock = drawer;
			aicon->x_pos = drawer->x_pos + (aicon->xindex * ICON_SIZE);
			aicon->y_pos = drawer->y_pos + (aicon->yindex * ICON_SIZE);

			if (!drawer->lowered)
				ChangeStackingLevel(aicon->icon->core, WMDockLevel);
			else
				ChangeStackingLevel(aicon->icon->core, WMNormalLevel);

			wCoreConfigure(aicon->icon->core, aicon->x_pos, aicon->y_pos, 0, 0);

			if (!drawer->collapsed)
				xcb_map_window(wdisp->conn, aicon->icon->core->window);
			wRaiseFrame(aicon->icon->core);

			drawer->icon_count++;
		}
	}

finish:
	WMReleasePropList(drawer_state);

	return drawer;
}


/* Same kind of comment than for previous function: this function is
 * very similar to make_icon_state, but has substential differences as
 * well. */
static WMPropList *drawerSaveState(WDock *drawer)
{
	WMPropList *pstr, *drawer_state;
	WAppIcon *ai;
	char buffer[64];

	ai = drawer->icon_array[0];
	/* Store its name */
	pstr = WMCreatePLString(ai->wm_instance);
	drawer_state = WMCreatePLDictionary(dName, pstr, NULL); /* we need this final NULL */
	WMReleasePropList(pstr);

	/* Store its position */
	snprintf(buffer, sizeof(buffer), "%i,%i", ai->x_pos, ai->y_pos);
	pstr = WMCreatePLString(buffer);
	WMPutInPLDictionary(drawer_state, dPosition, pstr);
	WMReleasePropList(pstr);

#ifdef USE_DOCK_XDND
	/* Store its DnD command */
	if (ai->dnd_command) {
		pstr = WMCreatePLString(ai->dnd_command);
		WMPutInPLDictionary(drawer_state, dDropCommand, pstr);
		WMReleasePropList(pstr);
	}
#endif /* USE_DOCK_XDND */

	/* Store its paste command */
	if (ai->paste_command) {
		pstr = WMCreatePLString(ai->paste_command);
		WMPutInPLDictionary(drawer_state, dPasteCommand, pstr);
		WMReleasePropList(pstr);
	}

	/* Store applications list and other properties */
	pstr = dockSaveState(drawer);
	WMPutInPLDictionary(drawer_state, dDock, pstr);
	WMReleasePropList(pstr);

	return drawer_state;
}


void wDrawersSaveState(WScreen *scr)
{
	WMPropList *all_drawers, *drawer_state;
	int i;
	WDrawerChain *dc;

	make_keys();

	all_drawers = WMCreatePLArray(NULL);
	for (i=0, dc = scr->drawers;
	     i < scr->drawer_count;
	     i++, dc = dc->next) {
		drawer_state = drawerSaveState(dc->adrawer);
		WMAddToPLArray(all_drawers, drawer_state);
		WMReleasePropList(drawer_state);
	}
	WMPutInPLDictionary(scr->session_state, dDrawers, all_drawers);
	WMReleasePropList(all_drawers);
}


void wDrawersRestoreState(WScreen *scr)
{
	WMPropList *all_drawers, *drawer_state;
	int i;

	make_keys();

	if (scr->session_state == NULL)
		return;

	all_drawers = WMGetFromPLDictionary(scr->session_state, dDrawers);
	if (!all_drawers)
		return;

	for (i = 0; i < WMGetPropListItemCount(all_drawers); i++) {
		drawer_state = WMGetFromPLArray(all_drawers, i);
		drawerRestoreState(scr, drawer_state);
		// Note: scr->drawers was updated when the the drawer was created
	}
}
