/* appicon.c- icon for applications (not mini-window)
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
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include <cairo.h>
#include <cairo-xcb.h>
#include <WINGs/WINGsP.h>

#include "WindowMaker.h"
#include "window.h"
#include "icon.h"
#include "application.h"
#include "appicon.h"
#include "actions.h"
#include "stacking.h"
#include "dock.h"
#include "main.h"
#include "defaults.h"
#include "workspace.h"
#include "superfluous.h"
#include "menu.h"
#include "framewin.h"
#include "dialog.h"
#include "dialog_icon.h"
#include "startup_notify.h"
#include "xinerama.h"
#include "client.h"
#include "placement.h"
#include "misc.h"
#include "event.h"
#include "wsound.h"
#ifdef USE_DOCK_XDND
#include "xdnd.h"
#endif

/*
 * icon_file for the dock is got from the preferences file by
 * using the classname/instancename
 */

#define MOD_MASK       wdisp->modifier_mask
#define ICON_SIZE      w_global.config->icon_size

static void iconDblClick(WObjDescriptor * desc, xcb_generic_event_t * event);
static void iconExpose(WObjDescriptor * desc, xcb_generic_event_t * event);
static void wApplicationSaveIconPathFor(const char *iconPath, const char *wm_instance, const char *wm_class);
static WAppIcon *wAppIconCreate(WWindow * leader_win);
static void add_to_appicon_list(WScreen *scr, WAppIcon *appicon);
static void remove_from_appicon_list(WScreen *scr, WAppIcon *appicon);
static void create_appicon_from_dock(WWindow *wwin, WApplication *wapp, xcb_window_t main_window);

/* This function is used if the application is a .app. It checks if it has an icon in it
 * like for example /usr/local/GNUstep/Applications/WPrefs.app/WPrefs.tiff
 */
void wApplicationExtractDirPackIcon(const char *path, const char *wm_instance, const char *wm_class)
{
	char *iconPath = NULL;
	char *tmp = NULL;
	size_t tmp_len;

	if (strstr(path, ".app")) {
		tmp_len = strlen(path) + strlen(".tiff") + 1;
		tmp = wmalloc(tmp_len);

		if (wdisp->supports_tiff) {
			snprintf(tmp, tmp_len, "%s.tiff", path);
			if (access(tmp, R_OK) == 0)
				iconPath = tmp;
		}

		if (!iconPath) {
			snprintf(tmp, tmp_len, "%s.xpm", path);
			if (access(tmp, R_OK) == 0)
				iconPath = tmp;
		}

		if (!iconPath)
			wfree(tmp);

		if (iconPath) {
			wApplicationSaveIconPathFor(iconPath, wm_instance, wm_class);
			wfree(iconPath);
		}
	}
}

WAppIcon *wAppIconCreateForDock(WScreen *scr, const char *command, const char *wm_instance, const char *wm_class, int tile)
{
	WAppIcon *aicon;

	aicon = wmalloc(sizeof(WAppIcon));
	wretain(aicon);
	aicon->yindex = -1;
	aicon->xindex = -1;

	add_to_appicon_list(scr, aicon);

	if (command)
		aicon->command = wstrdup(command);

	if (wm_class)
		aicon->wm_class = (char *)wintern(wm_class);

	if (wm_instance)
		aicon->wm_instance = (char *)wintern(wm_instance);

	if (wdisp->flags.clip_merged_in_dock && wm_class != NULL && strcmp(wm_class, "WMDock") == 0)
		tile = TILE_CLIP;
	aicon->icon = icon_create_for_dock(scr, command, wm_instance, wm_class, tile);

#ifdef USE_DOCK_XDND
	wXDNDMakeAwareness(aicon->icon->core->window);
#endif

	/* will be overridden by dock */
	aicon->icon->core->descriptor.handle_mousedown = appIconMouseDown;
	aicon->icon->core->descriptor.handle_expose = iconExpose;
	aicon->icon->core->descriptor.parent_type = WCLASS_APPICON;
	aicon->icon->core->descriptor.parent = aicon;
	AddToStackList(aicon->icon->core);

	return aicon;
}

void create_appicon_for_application(WApplication *wapp, WWindow *wwin)
{
	/* Transient windows should never get their own appicon */
	if (wwin->transient_for != XCB_NONE && wwin->transient_for != wwin->screen_ptr->root_win)
		return;

	/* Try to create an icon from the dock or clip */
	create_appicon_from_dock(wwin, wapp, wapp->main_window);

	/* If app_icon was not found, create it */
	if (!wapp->app_icon) {
		/* Create the icon, icon_create_for_wwindow inside
		 * wAppIconCreate already runs wIconUpdate so the second
		 * call that used to follow here just re-read the
		 * wm_hints icon pixmap from the server for the same
		 * result */
		wapp->app_icon = wAppIconCreate(wapp->main_window_desc);

		/* Now, paint the icon */
		if (!WFLAGP(wapp->main_window_desc, no_appicon))
			paint_app_icon(wapp);
	}

	/* At this point the application is fully set up and all icon and
	 * window data are known - so try to save the icon file for docked
	 * applications to ensure that the file exists when window maker
	 * starts up next time. */
	if (wapp->app_icon->docked && !WFLAGP(wapp->main_window_desc, no_appicon))
		save_appicon(wapp->app_icon);
}

void unpaint_app_icon(WApplication *wapp)
{
	WAppIcon *aicon;
	WScreen *scr;
	WDock *clip;

	if (!wapp || !wapp->app_icon)
		return;

	aicon = wapp->app_icon;

	/* If the icon is docked, don't continue */
	if (aicon->docked)
		return;

	scr = wapp->main_window_desc->screen_ptr;
	clip = scr->workspaces[scr->current_workspace]->clip;

	if (!clip || !aicon->attracted || !clip->collapsed)
		xcb_unmap_window(wdisp->conn, aicon->icon->core->window);

	/* We want to avoid having it on the list  because otherwise
	 * there will be a hole when the icons are arranged with
	 * wArrangeIcons() */
	remove_from_appicon_list(scr, aicon);

	if (w_global.config->auto_arrange_icons && !aicon->attracted)
		wArrangeIcons(scr, true);
}

void paint_app_icon(WApplication *wapp)
{
	WIcon *icon;
	WScreen *scr;
	WDock *attracting_dock;
	int x = 0, y = 0;
	bool update_icon = false;

	if (!wapp || !wapp->app_icon || !wapp->main_window_desc)
		return;

	icon = wapp->app_icon->icon;
	scr = wapp->main_window_desc->screen_ptr;
	wapp->app_icon->main_window = wapp->main_window;

	/* If the icon is docked, don't continue */
	if (wapp->app_icon->docked)
		return;

	attracting_dock = scr->attracting_drawer != NULL ?
		scr->attracting_drawer :
		scr->workspaces[scr->current_workspace]->clip;
	if (attracting_dock && attracting_dock->attract_icons &&
		wDockFindFreeSlot(attracting_dock, &x, &y)) {
		wapp->app_icon->attracted = 1;
		if (!icon->shadowed) {
			icon->shadowed = 1;
			update_icon = true;
		}
		wDockAttachIcon(attracting_dock, wapp->app_icon, x, y, update_icon);
	} else {
		/* We must know if the icon is painted in the screen,
		 * because if painted, then PlaceIcon will return the next
		 * space on the screen, and the icon will move */
		if (wapp->app_icon->next == NULL && wapp->app_icon->prev == NULL) {
			PlaceIcon(scr, &x, &y, wGetHeadForWindow(wapp->main_window_desc));
			wAppIconMove(wapp->app_icon, x, y);
			wLowerFrame(icon->core);
		}
	}

	/* If we want appicon (no_appicon is not set) and the icon is not
	 * in the appicon_list, we must add it. Else, we want to avoid
	 * having it on the list */
	if (!WFLAGP(wapp->main_window_desc, no_appicon) &&
	    wapp->app_icon->next == NULL && wapp->app_icon->prev == NULL)
		add_to_appicon_list(scr, wapp->app_icon);

	if (!attracting_dock || !wapp->app_icon->attracted || !attracting_dock->collapsed)
		xcb_map_window(wdisp->conn, icon->core->window);

	if (w_global.config->auto_arrange_icons && !wapp->app_icon->attracted)
		wArrangeIcons(scr, true);
}

void removeAppIconFor(WApplication *wapp)
{
	if (!wapp->app_icon)
		return;

	if (w_global.config->highlight_active_app)
		wIconSetHighlited(wapp->app_icon->icon, false);
	if (wapp->app_icon->docked && !wapp->app_icon->attracted) {
		wapp->app_icon->running = 0;
		/* since we keep it, we don't care if it was attracted or not */
		wapp->app_icon->attracted = 0;
		wapp->app_icon->icon->shadowed = 0;
		wapp->app_icon->main_window = XCB_NONE;
		wapp->app_icon->pid = 0;
		wapp->app_icon->icon->owner = NULL;
		wapp->app_icon->icon->icon_win = XCB_NONE;

		/* Set the icon image */
		set_icon_image_from_database(wapp->app_icon->icon, wapp->app_icon->wm_instance,
					     wapp->app_icon->wm_class, wapp->app_icon->command);

		/* Update the icon, because wapp->app_icon->icon could be NULL */
		wIconUpdate(wapp->app_icon->icon);

		/* Paint it */
		wAppIconPaint(wapp->app_icon);
	} else if (wapp->app_icon->docked) {
		wapp->app_icon->running = 0;
		if (wapp->app_icon->dock->type == WM_DRAWER) {
			wDrawerFillTheGap(wapp->app_icon->dock, wapp->app_icon, true);
		} else if (wapp->app_icon->dock->type == WM_CLIP) {
			wClipFillTheGap(wapp->app_icon->dock, wapp->app_icon);
		}
		wDockDetach(wapp->app_icon->dock, wapp->app_icon);
	} else {
		wAppIconDestroy(wapp->app_icon);
	}

	wapp->app_icon = NULL;

	if (w_global.config->auto_arrange_icons)
		wArrangeIcons(wapp->main_window_desc->screen_ptr, true);
}

static WAppIcon *wAppIconCreate(WWindow *leader_win)
{
	WAppIcon *aicon;

	aicon = wmalloc(sizeof(WAppIcon));
	wretain(aicon);
	aicon->yindex = -1;
	aicon->xindex = -1;
	aicon->prev = NULL;
	aicon->next = NULL;

	if (leader_win->wm_class)
		aicon->wm_class = (char *)wintern(leader_win->wm_class);

	if (leader_win->wm_instance)
		aicon->wm_instance = (char *)wintern(leader_win->wm_instance);

	aicon->icon = icon_create_for_wwindow(leader_win);
#ifdef USE_DOCK_XDND
	wXDNDMakeAwareness(aicon->icon->core->window);
#endif

	/* will be overridden if docked */
	aicon->icon->core->descriptor.handle_mousedown = appIconMouseDown;
	aicon->icon->core->descriptor.handle_expose = iconExpose;
	aicon->icon->core->descriptor.parent_type = WCLASS_APPICON;
	aicon->icon->core->descriptor.parent = aicon;
	AddToStackList(aicon->icon->core);
	/* icon_create_for_wwindow composited the pixmap with the
	 * miniwindow default show_title=1, so the appicon tile already
	 * carries an empty title strip. Clear the flag and re-render so
	 * the strip is gone; the wm_hints icon decode is cached on the
	 * owner WWindow, so this does not round-trip to the server */
	aicon->icon->show_title = 0;
	wIconUpdate(aicon->icon);

	return aicon;
}

void wAppIconDestroy(WAppIcon * aicon)
{
	WScreen *scr = aicon->icon->core->screen_ptr;

	WMDeleteTimerHandlerPtr(&aicon->launch_timer);
	wDeleteDeathHandlersForData(aicon);
	wStartupNotifyForgetIcon(aicon);
	RemoveFromStackList(aicon->icon->core);
	wIconDestroy(aicon->icon);
	if (aicon->command)
		wfree(aicon->command);
#ifdef USE_DOCK_XDND
	if (aicon->dnd_command)
		wfree(aicon->dnd_command);
#endif
	if (aicon->paste_command)
		wfree(aicon->paste_command);
	if (aicon->display_name)
		wfree(aicon->display_name);
	if (aicon->comment)
		wfree(aicon->comment);
	if (aicon->wm_instance)
		wunintern(aicon->wm_instance);

	if (aicon->wm_class)
		wunintern(aicon->wm_class);

	remove_from_appicon_list(scr, aicon);

	aicon->destroyed = 1;
	wrelease(aicon);
}

static void drawCorner(WIcon * icon)
{
	WScreen *scr = icon->core->screen_ptr;
	WTexSolid *tex = scr->icon_title_texture;
	cairo_t *cr;

	cr = WCairoBeginForDrawable(scr->wmscreen, icon->core->window);
	if (!cr)
		return;

	cairo_set_source_rgb(cr, tex->normal.red / 65535.0,
			     tex->normal.green / 65535.0,
			     tex->normal.blue / 65535.0);
	cairo_move_to(cr, 1, 1);
	cairo_line_to(cr, 12, 1);
	cairo_line_to(cr, 1, 12);
	cairo_close_path(cr);
	cairo_fill(cr);

	cairo_set_source_rgb(cr, tex->light.red / 65535.0,
			     tex->light.green / 65535.0,
			     tex->light.blue / 65535.0);
	cairo_set_line_width(cr, 1.0);
	cairo_move_to(cr, 0.5, 12.5);
	cairo_line_to(cr, 0.5, 0.5);
	cairo_line_to(cr, 12.5, 0.5);
	cairo_stroke(cr);

	WCairoEnd(cr);
}

void wAppIconMove(WAppIcon * aicon, int x, int y)
{
	xcb_configure_window(wdisp->conn, aicon->icon->core->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ x, y });
	aicon->x_pos = x;
	aicon->y_pos = y;
}

#ifdef WS_INDICATOR
static void updateDockNumbers(WScreen *scr)
{
	int length;
	char ws_numbers[20];
	WAppIcon *dicon = scr->dock->icon_array[0];

	snprintf(ws_numbers, sizeof(ws_numbers), "%i [ %i ]", scr->current_workspace + 1, ((scr->current_workspace / 10) + 1));
	length = strlen(ws_numbers);

	xcb_clear_area(wdisp->conn, false, dicon->icon->core->window, 2, 2, 50, WSCALE(scr, WMFontHeight(scr->icon_title_font)) + 1);

	WMDrawString(scr->wmscreen, dicon->icon->core->window, scr->black,
		     scr->icon_title_font, 4, 3, ws_numbers, length);

	WMDrawString(scr->wmscreen, dicon->icon->core->window, scr->white,
		     scr->icon_title_font, 3, 2, ws_numbers, length);
}
#endif				/* WS_INDICATOR */

void wAppIconPaint(WAppIcon *aicon)
{
	WApplication *wapp;
	WScreen *scr = aicon->icon->core->screen_ptr;

	if (aicon->icon->owner)
		wapp = wApplicationOf(aicon->icon->owner->main_window);
	else
		wapp = NULL;

	wIconPaint(aicon->icon);

# ifdef WS_INDICATOR
	if (aicon->docked && scr->dock && scr->dock == aicon->dock && aicon->yindex == 0)
		updateDockNumbers(scr);
# endif
	if (aicon->docked && !aicon->running && aicon->command != NULL)
		wAppIconPaintDockDots(scr, aicon->icon->core->window,
				      scr->dock_dots->width);
#ifdef HIDDENDOT
	if (wapp && wapp->flags.hidden)
		wAppIconPaintDockDots(scr, aicon->icon->core->window, 7);
#endif				/* HIDDENDOT */

	if (aicon->omnipresent)
		drawCorner(aicon->icon);

	if (aicon->launching)
		wAppIconPaintLaunching(scr, aicon->icon->core->window);
}

/* the three-dot not-running indicator (or its first-column-only
 * single-dot variant when width is clamped), masked through the
 * shared depth-1 mask so only the dot bodies land */
void wAppIconPaintDockDots(WScreen *scr, xcb_window_t win, int width)
{
	RContext *rc = scr->rcontext;
	WPixmap *dots = scr->dock_dots;
	cairo_surface_t *psurf, *msurf;
	cairo_t *cr;

	cr = WCairoBeginForDrawable(scr->wmscreen, win);
	if (!cr)
		return;
	psurf = cairo_xcb_surface_create(rc->xc, (xcb_drawable_t)dots->image,
					 rc->xvisual, dots->width, dots->height);
	msurf = cairo_xcb_surface_create_for_bitmap(rc->xc, rc->xscreen,
						    (xcb_pixmap_t)dots->mask,
						    dots->width, dots->height);
	cairo_rectangle(cr, 0, 0, width, dots->height);
	cairo_clip(cr);
	cairo_set_source_surface(cr, psurf, 0, 0);
	cairo_mask_surface(cr, msurf, 0, 0);
	cairo_surface_destroy(psurf);
	cairo_surface_destroy(msurf);
	WCairoEnd(cr);
}

/* half-white overlay while a docked launcher is starting: the
 * FillStippled 50% checkerboard read as a uniform lightening at icon
 * scale, so paint the same effect as a translucent white fill */
void wAppIconPaintLaunching(WScreen *scr, xcb_window_t win)
{
	cairo_t *cr;

	cr = WCairoBeginForDrawable(scr->wmscreen, win);
	if (!cr)
		return;
	cairo_set_source_rgba(cr, 1, 1, 1, 0.5);
	cairo_rectangle(cr, 0, 0, w_global.config->icon_size,
			w_global.config->icon_size);
	cairo_fill(cr);
	WCairoEnd(cr);
}

/* Save the application icon, if it's a dockapp then use it with dock = True */
void save_appicon(WAppIcon *aicon)
{
	char *path;

	if (!aicon)
		return;

	if (!aicon->docked || aicon->attracted)
		return;

	path = wIconStore(aicon->icon);
	if (!path)
		return;

	wApplicationSaveIconPathFor(path, aicon->wm_instance, aicon->wm_class);
	wfree(path);
}

#define canBeDocked(wwin)  ((wwin) && ((wwin)->wm_class||(wwin)->wm_instance))

/* main_window may not have the full command line; try to find one which does */
static void relaunchApplication(WApplication *wapp)
{
	WScreen *scr;
	WWindow *wlist, *next;

	scr = wapp->main_window_desc->screen_ptr;
	wlist = scr->focused_window;
	if (! wlist)
		return;

	while (wlist->prev)
		wlist = wlist->prev;

	while (wlist) {
		next = wlist->next;

		if (wlist->main_window == wapp->main_window) {
			if (RelaunchWindow(wlist))
				return;
		}

		wlist = next;
	}
}

static void relaunchCallback(WMenu * menu, WMenuEntry * entry)
{
	WApplication *wapp = (WApplication *) entry->clientdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) menu;

	relaunchApplication(wapp);
}

static void hideCallback(WMenu * menu, WMenuEntry * entry)
{
	WApplication *wapp = (WApplication *) entry->clientdata;

	if (wapp->flags.hidden) {
		wWorkspaceChangeFromPointer(menu->menu->screen_ptr, wapp->last_workspace);
		wUnhideApplication(wapp, false, false);
	} else {
		wRequestHideApplication(wapp, NULL);
	}
}

static void unhideHereCallback(WMenu * menu, WMenuEntry * entry)
{
	WApplication *wapp = (WApplication *) entry->clientdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) menu;

	wUnhideApplication(wapp, false, true);
}

static void setIconCallback(WMenu *menu, WMenuEntry *entry)
{
	WAppIcon *icon = ((WApplication *) entry->clientdata)->app_icon;
	char *file = NULL;
	WScreen *scr;
	int result;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) menu;

	assert(icon != NULL);

	if (icon->editing)
		return;

	icon->editing = 1;
	scr = icon->icon->core->screen_ptr;

	wretain(icon);

	result = wIconChooserDialog(scr, &file, icon->wm_instance, icon->wm_class, NULL);

	if (result) {
		if (!icon->destroyed) {
			if (!wIconChangeImageFile(icon->icon, file)) {
				wMessageDialog(scr, _("Error"),
				               _("Could not open specified icon file"),
				               _("OK"), NULL, NULL);
			} else {
				wDefaultChangeIcon(icon->wm_instance, icon->wm_class, file);
				wAppIconPaint(icon);
			}
		}
		if (file)
			wfree(file);
	}
	icon->editing = 0;
	wrelease(icon);
}

static void killCallback(WMenu * menu, WMenuEntry * entry)
{
	WApplication *wapp = (WApplication *) entry->clientdata;
	WFakeGroupLeader *fPtr;
	char *buffer;
	char *shortname;

	if (!WCHECK_STATE(WSTATE_NORMAL))
		return;

	WCHANGE_STATE(WSTATE_MODAL);

	assert(entry->clientdata != NULL);

	shortname = (wapp->app_icon && wapp->app_icon->wm_instance)
		    ? basename(wapp->app_icon->wm_instance)
		    : _("application");

	buffer = wstrconcat(shortname,
			    _(" will be forcibly closed.\n"
			      "Any unsaved changes will be lost.\n" "Please confirm."));

	fPtr = wapp->main_window_desc->fake_group;

	wretain(wapp->main_window_desc);
	if (w_global.config->dont_confirm_kill
	    || wMessageDialog(menu->frame->screen_ptr, _("Kill Application"),
			      buffer, _("Yes"), _("No"), NULL) == WAPRDefault) {
		if (fPtr != NULL) {
			WWindow *wwin, *twin;

			wwin = wapp->main_window_desc->screen_ptr->focused_window;
			while (wwin) {
				twin = wwin->prev;
				if (wwin->fake_group == fPtr)
					wClientKill(wwin);
				wwin = twin;
			}
		} else if (!wapp->main_window_desc->flags.destroyed) {
			wClientKill(wapp->main_window_desc);
		}
	}
	wrelease(wapp->main_window_desc);
	wfree(buffer);
	WCHANGE_STATE(WSTATE_NORMAL);
}

static WMenu *createApplicationMenu(WScreen *scr)
{
	WMenu *menu;

	menu = wMenuCreate(scr, NULL, false);
	wMenuAddCallback(menu, _("Unhide Here"), unhideHereCallback, NULL);
	wMenuAddCallback(menu, _("Hide"), hideCallback, NULL);
	wMenuAddCallback(menu, _("Launch"), relaunchCallback, NULL);
	wMenuAddCallback(menu, _("Set Icon..."), setIconCallback, NULL);
	wMenuAddCallback(menu, _("Kill"), killCallback, NULL);

	return menu;
}

static void openApplicationMenu(WApplication * wapp, int x, int y)
{
	WMenu *menu;
	WScreen *scr = wapp->main_window_desc->screen_ptr;
	int i;

	if (!scr->icon_menu) {
		scr->icon_menu = createApplicationMenu(scr);
		wfree(scr->icon_menu->entries[1]->text);
	}

	menu = scr->icon_menu;

	if (wapp->flags.hidden)
		menu->entries[1]->text = _("Unhide");
	else
		menu->entries[1]->text = _("Hide");

	menu->flags.realized = 0;
	wMenuRealize(menu);

	x -= menu->frame->core->width / 2;
	if (x + menu->frame->core->width > scr->scr_width)
		x = scr->scr_width - menu->frame->core->width;

	if (x < 0)
		x = 0;

	/* set client data */
	for (i = 0; i < menu->entry_no; i++)
		menu->entries[i]->clientdata = wapp;

	wMenuMapAt(menu, x, y, false);
}

/******************************************************************/

static void iconExpose(WObjDescriptor *desc, xcb_generic_event_t *event)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) event;

	wAppIconPaint(desc->parent);
}

static void iconDblClick(WObjDescriptor *desc, xcb_generic_event_t *event)
{
	WAppIcon *aicon = desc->parent;
	WApplication *wapp;
	WScreen *scr = aicon->icon->core->screen_ptr;
	int unhideHere;
	WWindow *uwin;

	assert(aicon->icon->owner != NULL);

	wapp = wApplicationOf(aicon->icon->owner->main_window);

	if (((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_CONTROL) {
		relaunchApplication(wapp);
		return;
	}

	unhideHere = (((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_SHIFT);

	uwin = wApplicationUrgentWindow(wapp);
	if (uwin && !unhideHere) {
		/* activate the window asking for attention */
		wWindowSingleFocus(uwin);
	} else {
		/* go to the last workspace that the user worked on the app */
		if (!unhideHere && wapp->last_workspace != scr->current_workspace)
			wWorkspaceChangeFromPointer(scr, wapp->last_workspace);

		wUnhideApplication(wapp, ((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_2, unhideHere);
	}

	if (((xcb_button_press_event_t *)event)->state & MOD_MASK)
		wHideOtherApplications(aicon->icon->owner);
}

void appIconMouseDown(WObjDescriptor * desc, xcb_generic_event_t * event)
{
	WAppIcon *aicon = desc->parent;
	WScreen *scr = aicon->icon->core->screen_ptr;
	bool hasMoved;

	if (aicon->editing || WCHECK_STATE(WSTATE_MODAL))
		return;

	if (IsDoubleClick(scr, event)) {
		/* Middle or right mouse actions were handled on first click */
		if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_1)
			iconDblClick(desc, event);
		return;
	}

	if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_2) {
		WApplication *wapp = wApplicationOf(aicon->icon->owner->main_window);

		if (wapp)
			relaunchApplication(wapp);

		return;
	}

	if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_3) {
		WObjDescriptor *desc;
		WApplication *wapp = wApplicationOf(aicon->icon->owner->main_window);

		if (!wapp)
			return;

		if ((event->response_type & 0x80) &&
		    wXGrabPointer(wdisp->conn, aicon->icon->core->window, true, XCB_EVENT_MASK_BUTTON_MOTION
				 | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS, XCB_GRAB_MODE_ASYNC,
				 XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE, XCB_CURRENT_TIME) != WXGrabSuccess) {
			wwarning("pointer grab failed for appicon menu");
			return;
		}

		openApplicationMenu(wapp, ((xcb_button_press_event_t *)event)->root_x, ((xcb_button_press_event_t *)event)->root_y);

		/* allow drag select of menu */
		desc = &scr->icon_menu->menu->descriptor;
		event->response_type |= 0x80;
		(*desc->handle_mousedown) (desc, event);
		return;
	}

	hasMoved = wHandleAppIconMove(aicon, event);
	if (w_global.config->single_click && !hasMoved && aicon->dock != NULL)
	{
		iconDblClick(desc, event);
	}
}

static void clipReattachAfterFail(WDock *clip, WAppIcon *aicon,
				  int from_x, int from_y, int to_x, int to_y)
{
	WScreen *scr = clip->screen_ptr;
	WAppIconChain *chain;
	int x = aicon->xindex, y = aicon->yindex;
	bool taken = false;
	int i;

	for (i = 1; i < clip->max_icons; i++) {
		WAppIcon *ai = clip->icon_array[i];

		if (ai && ai != aicon && ai->xindex == x && ai->yindex == y) {
			taken = true;
			break;
		}
	}
	for (chain = scr->global_icons; !taken && chain; chain = chain->next) {
		if (chain->aicon != aicon && chain->aicon->xindex == x &&
		    chain->aicon->yindex == y)
			taken = true;
	}
	if (taken) {
		wDockFindFreeSlot(clip, &x, &y);
		to_x = clip->x_pos + x * ICON_SIZE;
		to_y = clip->y_pos + y * ICON_SIZE;
	}
	slide_window(aicon->icon->core->window, from_x, from_y, to_x, to_y);
	wDockReattachIcon(clip, aicon, x, y);
}

bool wHandleAppIconMove(WAppIcon *aicon, xcb_generic_event_t *event)
{
	WIcon *icon = aicon->icon;
	WScreen *scr = icon->core->screen_ptr;
	WDock *originalDock = aicon->dock; /* can be NULL */
	WDock *lastDock = originalDock;
	/* clip, dock and drawers (order determined at runtime), plus
	 * originalDock when it is not one of them */
	int drawer_count = 0;
	int dock_count;
	WDock **allDocks;
	WDrawerChain *dc;
	bool dockable, ondock;
	bool grabbed = false;
	bool collapsed = false; /* Stores the collapsed state of lastDock, before the moving appicon entered it */
	int superfluous = w_global.config->superfluous; /* we cache it to avoid problems */
	int omnipresent = aicon->omnipresent; /* this must be cached */
	bool showed_all_clips = false;

	int clickButton = ((xcb_button_press_event_t *)event)->detail;
	xcb_pixmap_t ghost = XCB_NONE;
	xcb_window_t wins[2]; /* Managing shadow window */
	xcb_generic_event_t *ev = NULL;

	int x = aicon->x_pos, y = aicon->y_pos;
	int ofs_x = ((xcb_button_press_event_t *)event)->event_x, ofs_y = ((xcb_button_press_event_t *)event)->event_y;
	int shad_x = x, shad_y = y;
	int ix = aicon->xindex, iy = aicon->yindex;
	int i;
	int oldX = x;
	int oldY = y;
	bool hasMoved = false;

	if (wdisp->flags.noupdates && originalDock != NULL)
		return false;

	if (!(((xcb_button_press_event_t *)event)->state & MOD_MASK))
		wRaiseFrame(icon->core);
	else {
		/* If Mod is pressed for an docked appicon, assume it is to undock it,
		 * so don't lower it */
		if (originalDock == NULL)
			wLowerFrame(icon->core);
	}

	if (wXGrabPointer(wdisp->conn, icon->core->window, true, XCB_EVENT_MASK_BUTTON_MOTION
			 | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS, XCB_GRAB_MODE_ASYNC,
			 XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE, XCB_CURRENT_TIME) != WXGrabSuccess) {
		wwarning("Pointer grab failed in wHandleAppIconMove");
	}

	if (originalDock != NULL) {
	    dockable = true;
	    ondock = true;
	}
	else {
		ondock = false;
		if (wdisp->flags.nodock && wdisp->flags.noclip && wdisp->flags.nodrawer)
			dockable = 0;
		else
			dockable = canBeDocked(icon->owner);
	}

	/* We try the various docks in that order:
	 * - First, the dock the appicon comes from, if any
	 * - Then, the drawers
	 * - Then, the "dock" (WM_DOCK)
	 * - Finally, the clip
	 */
	/* derive the bound from the live list, not scr->drawer_count */
	for (dc = scr->drawers; dc != NULL; dc = dc->next)
		drawer_count++;
	dock_count = drawer_count + 3;
	allDocks = wmalloc(sizeof(*allDocks) * (size_t)dock_count);

	i = 0;
	if (originalDock != NULL)
		allDocks[ i++ ] = originalDock;
	/* Testing scr->drawers is enough, no need to test wdisp->flags.nodrawer */
	for (dc = scr->drawers; dc != NULL; dc = dc->next) {
		if (dc->adrawer != originalDock)
			allDocks[ i++ ] = dc->adrawer;
	}
	if (!wdisp->flags.nodock && scr->dock != originalDock)
		allDocks[ i++ ] = scr->dock;

	if (!wdisp->flags.noclip &&
	    originalDock != scr->workspaces[scr->current_workspace]->clip)
		allDocks[ i++ ] = scr->workspaces[scr->current_workspace]->clip;

	for ( ; i < dock_count; i++) /* In case the clip, the dock, or both, are disabled */
		allDocks[ i ] = NULL;

	wins[0] = icon->core->window;
	wins[1] = scr->dock_shadow;
	wXRestackWindows(wdisp->conn, wins, 2);
	xcb_configure_window(wdisp->conn, scr->dock_shadow, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, (const uint32_t[]){ aicon->x_pos, aicon->y_pos, ICON_SIZE, ICON_SIZE });
	if (superfluous) {
		if (icon->pixmap != XCB_NONE)
			ghost = MakeGhostIcon(scr, icon->pixmap);
		else
			ghost = MakeGhostIcon(scr, icon->core->window);
		xcb_change_window_attributes(wdisp->conn, scr->dock_shadow, XCB_CW_BACK_PIXMAP, (const uint32_t[]){ ghost });
		xcb_clear_area(wdisp->conn, 0, scr->dock_shadow, 0, 0, 0, 0);
	}
	if (ondock)
		xcb_map_window(wdisp->conn, scr->dock_shadow);

	while (1) {
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
			 * stayed in the appIcon that is being moved. Ignore
			 * such "spurious" EnterNotifiy's */
			break;

		case XCB_MOTION_NOTIFY:
			hasMoved = true;
			if (!grabbed) {
				if (abs(ofs_x - (((xcb_motion_notify_event_t *)ev)->event_x)) >= MOVE_THRESHOLD
				    || abs(ofs_y - (((xcb_motion_notify_event_t *)ev)->event_y)) >= MOVE_THRESHOLD) {
					wXChangeActivePointerGrab(wdisp->conn, XCB_EVENT_MASK_BUTTON_MOTION
								 | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS,
								 wdisp->cursor[WCUR_MOVE], XCB_CURRENT_TIME);
					grabbed = 1;
				} else {
					break;
				}
			}

			if (omnipresent && !showed_all_clips) {
				int i;
				for (i = 0; i < scr->workspace_count; i++) {
					if (i == scr->current_workspace)
						continue;

					wDockShowIcons(scr->workspaces[i]->clip);
					/* Note: if dock is collapsed (for instance, because it
					   auto-collapses), its icons still won't show up */
				}
				showed_all_clips = true; /* To prevent flickering */
			}

			x = (((xcb_motion_notify_event_t *)ev)->root_x) - ofs_x;
			y = (((xcb_motion_notify_event_t *)ev)->root_y) - ofs_y;
			wAppIconMove(aicon, x, y);

			WDock *theNewDock = NULL;
			if (!((((xcb_motion_notify_event_t *)ev)->state) & MOD_MASK) || aicon->launching || aicon->lock || originalDock == NULL) {
				for (i = 0; dockable && i < dock_count; i++) {
					WDock *theDock = allDocks[i];
					if (theDock == NULL)
						break;
					if (wDockSnapIcon(theDock, aicon, x, y, &ix, &iy, (theDock == originalDock))) {
						theNewDock = theDock;
						break;
					}
				}
				if (originalDock != NULL && theNewDock == NULL &&
					(aicon->launching || aicon->lock || aicon->running)) {
					/* In those cases, stay in lastDock if no dock really wants us */
					theNewDock = lastDock;
				}
			}
			if (lastDock != NULL && lastDock != theNewDock) {
				/* Leave lastDock in the state we found it */
				if (lastDock->type == WM_DRAWER) {
					wDrawerFillTheGap(lastDock, aicon, (lastDock == originalDock));
				} else if (lastDock->type == WM_CLIP && lastDock == originalDock) {
					wClipFillTheGap(lastDock, aicon);
				}
				if (collapsed) {
					lastDock->collapsed = 1;
					wDockHideIcons(lastDock);
					collapsed = false;
				}
				if (lastDock->auto_raise_lower) {
					wDockLower(lastDock);
				}
			}
			if (theNewDock != NULL) {
				if (lastDock != theNewDock) {
					collapsed = theNewDock->collapsed;
					if (collapsed) {
						theNewDock->collapsed = 0;
						wDockShowIcons(theNewDock);
					}
					if (theNewDock->auto_raise_lower) {
						wDockRaise(theNewDock);
						/* And raise the moving tile above it */
						wRaiseFrame(aicon->icon->core);
					}
					lastDock = theNewDock;
				}

				shad_x = lastDock->x_pos + ix*w_global.config->icon_size;
				shad_y = lastDock->y_pos + iy*w_global.config->icon_size;

				xcb_configure_window(wdisp->conn, scr->dock_shadow, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ shad_x, shad_y });

				if (!ondock) {
					xcb_map_window(wdisp->conn, scr->dock_shadow);
				}
				ondock = 1;
			} else {
				lastDock = theNewDock; // i.e., NULL
				if (ondock) {
					xcb_unmap_window(wdisp->conn, scr->dock_shadow);
					/*
					 * Leaving that weird comment for now.
					 * But if we see no gap, there is no need to fill one!
					 * We could test ondock first and the lastDock to NULL afterwards
					   if (lastDock_before_it_was_null->type == WM_DRAWER) {
					   wDrawerFillTheGap(lastDock, aicon, (lastDock == originalDock));
					   } */

				}
				ondock = 0;
			}
			break;

		case XCB_BUTTON_PRESS:
			break;

		case XCB_BUTTON_RELEASE:
			if ((((xcb_button_press_event_t *)ev)->detail) != clickButton)
				break;
			wXUngrabPointer(wdisp->conn, XCB_CURRENT_TIME);

			bool docked = false;
			if (ondock) {
				slide_window(icon->core->window, x, y, shad_x, shad_y);
				xcb_unmap_window(wdisp->conn, scr->dock_shadow);
				if (originalDock == NULL) { // docking an undocked appicon
					docked = wDockAttachIcon(lastDock, aicon, ix, iy, false);
					if (!docked) {
						/* AppIcon got rejected (happens only when we can't get the
						   command for that appicon, and the user cancels the
						   wInputDialog asking for one). Make the rejection obvious by
						   sliding the icon to its old position */
						if (lastDock->type == WM_DRAWER) {
							// Also fill the gap left in the drawer
							wDrawerFillTheGap(lastDock, aicon, false);
						}
						slide_window(icon->core->window, x, y, oldX, oldY);
					}
				}
				else { // moving a docked appicon to a dock
					if (originalDock == lastDock) {
						docked = true;
						wDockReattachIcon(originalDock, aicon, ix, iy);
					}
					else {
						docked = wDockMoveIconBetweenDocks(originalDock, lastDock, aicon, ix, iy);
						if (!docked) {
							/* Possible scenario: user moved an auto-attracted appicon
							   from the clip to the dock, and cancelled the wInputDialog
							   asking for a command */
							if (lastDock->type == WM_DRAWER) {
								wDrawerFillTheGap(lastDock, aicon, false);
							}
							/* If aicon comes from a drawer, make some room to reattach it */
							if (originalDock->type == WM_DRAWER) {
								WAppIcon *aiconsToShift[ originalDock->icon_count ];
								int j = 0;
								int across = wDockAcrossIndex(aicon, originalDock->edge);

								for (i = 0; i < originalDock->max_icons; i++) {
									WAppIcon *ai = originalDock->icon_array[ i ];
									if (ai && ai != aicon &&
										abs(wDockAcrossIndex(ai, originalDock->edge)) >= abs(across))
										aiconsToShift[j++] = ai;
								}
								if (j != originalDock->icon_count - abs(across) - 1)
									// Trust this never happens?
									wwarning("Shifting j=%d appicons (instead of %d!) to reinsert aicon at index %d.",
										j, originalDock->icon_count - abs(across) - 1, across);
								wSlideAppiconsAcross(aiconsToShift, j, originalDock, true);
								// Trust the appicon is inserted at exactly the same place, so its oldX/oldY are consistent with its "new" location?
							}

							if (originalDock->type == WM_CLIP) {
								clipReattachAfterFail(originalDock, aicon, x, y, oldX, oldY);
							} else {
								slide_window(icon->core->window, x, y, oldX, oldY);
								wDockReattachIcon(originalDock, aicon, aicon->xindex, aicon->yindex);
							}
						}
						else {
							if (originalDock->auto_collapse && !originalDock->collapsed) {
								originalDock->collapsed = 1;
								wDockHideIcons(originalDock);
							}
							if (originalDock->auto_raise_lower)
								wDockLower(originalDock);
						}
					}
				}
				// No matter what happened above, check to lower lastDock
				// Don't see why I commented out the following 2 lines
				/* if (lastDock->auto_raise_lower)
				   wDockLower(lastDock); */
				/* If docked (or tried to dock) to a auto_collapsing dock, unset
				 * collapsed, so that wHandleAppIconMove doesn't collapse it
				 * right away (the timer will take care of it) */
				if (lastDock->auto_collapse)
					collapsed = 0;
				if (docked)
					wSoundPlay(scr, WSOUND_DOCK);
			} else {
				if (originalDock != NULL) { /* Detaching a docked appicon */
					if (superfluous && !aicon->running && !w_global.config->no_animations) {
						/* We need to deselect it, even if is deselected in
						 * wDockDetach(), because else DoKaboom() will fail.
						 */
						if (aicon->icon->selected)
							wIconSelect(aicon->icon);
						wSoundPlay(scr, WSOUND_KABOOM);
						DoKaboom(scr, aicon->icon->core->window, x, y);
					} else {
						wSoundPlay(scr, WSOUND_UNDOCK);
					}
					if (originalDock->type == WM_CLIP)
						wClipFillTheGap(originalDock, aicon);
					wDockDetach(originalDock, aicon);
					if (originalDock->auto_collapse && !originalDock->collapsed) {
						originalDock->collapsed = 1;
						wDockHideIcons(originalDock);
					}
					if (originalDock->auto_raise_lower)
						wDockLower(originalDock);
				}
			}
			// Can't remember why the icon hiding is better done above than below (commented out)
			// Also, lastDock is quite different from originalDock
			/*
			  if (collapsed) {
				lastDock->collapsed = 1;
				wDockHideIcons(lastDock);
				collapsed = 0;
			}
			*/
			if (superfluous) {
				if (ghost != XCB_NONE)
					xcb_free_pixmap(wdisp->conn, ghost);
				xcb_change_window_attributes(wdisp->conn, scr->dock_shadow, XCB_CW_BACK_PIXEL, (const uint32_t[]){ scr->white_pixel });
			}
			if (showed_all_clips) {
				int i;
				for (i = 0; i < scr->workspace_count; i++) {
					if (i == scr->current_workspace)
						continue;

					wDockHideIcons(scr->workspaces[i]->clip);
				}
			}
			if (w_global.config->auto_arrange_icons && !(originalDock != NULL && docked))
				/* Need to rearrange unless moving from dock to dock */
				wArrangeIcons(scr, true);
			free(ev);
			wfree(allDocks);
			return hasMoved;
		}
	}

	wfree(allDocks);
	return hasMoved;
}

/* This function save the application icon and store the path in the Dictionary */
static void wApplicationSaveIconPathFor(const char *iconPath, const char *wm_instance, const char *wm_class)
{
	WMPropList *dict = w_global.domain.window_attr->dictionary;
	WMPropList *adict, *key, *iconk;
	WMPropList *val;
	char *tmp;

	WMPLSetCaseSensitive(true);

	tmp = get_name_for_instance_class(wm_instance, wm_class);
	key = WMCreatePLString(tmp);
	wfree(tmp);

	adict = WMGetFromPLDictionary(dict, key);
	iconk = WMCreatePLString("Icon");

	if (adict) {
		val = WMGetFromPLDictionary(adict, iconk);
	} else {
		/* no dictionary for app, so create one */
		adict = WMCreatePLDictionary(NULL, NULL);
		WMPutInPLDictionary(dict, key, adict);
		WMReleasePropList(adict);
		val = NULL;
	}

	if (!val) {
		val = WMCreatePLString(iconPath);
		WMPutInPLDictionary(adict, iconk, val);
		WMReleasePropList(val);
	} else {
		val = NULL;
	}

	WMReleasePropList(key);
	WMReleasePropList(iconk);

	WMPLSetCaseSensitive(false);

	if (val) {
		if (!wdisp->flags.noupdates)
			UpdateDomainFile(w_global.domain.window_attr);
		wWindowAttrsReload();
	}
}

static WAppIcon *findDockIconFor(WDock *dock, WWindow *wwin, xcb_window_t main_window,
				 pid_t client_pid)
{
	WAppIcon *aicon = NULL;

	aicon = wDockFindIconForWindow(dock, main_window);
	if (!aicon) {
		wDockTrackWindowLaunch(dock, main_window, wwin->wm_instance,
				       wwin->wm_class, wwin->wm_command,
				       client_pid);
		aicon = wDockFindIconForWindow(dock, main_window);
	}
	return aicon;
}

static void create_appicon_from_dock(WWindow *wwin, WApplication *wapp, xcb_window_t main_window)
{
	WScreen *scr = wwin->screen_ptr;
	pid_t client_pid;

	wapp->app_icon = NULL;

	/* main_window may be a shared_appicon fake leader owned by wmaker,
	 * so resolve the spawning process from the actual client window */
	client_pid = wwin->client_pid;

	if (scr->last_dock)
		wapp->app_icon = findDockIconFor(scr->last_dock, wwin, main_window, client_pid);

	/* check main dock if we did not find it in last dock */
	if (!wapp->app_icon && scr->dock)
		wapp->app_icon = findDockIconFor(scr->dock, wwin, main_window, client_pid);

	/* check clips */
	if (!wapp->app_icon) {
		int i;
		for (i = 0; i < scr->workspace_count; i++) {
			WDock *dock = scr->workspaces[i]->clip;

			if (dock)
				wapp->app_icon = findDockIconFor(dock, wwin, main_window, client_pid);

			if (wapp->app_icon)
				break;
		}
	}

	/* Finally check drawers */
	if (!wapp->app_icon) {
		WDrawerChain *dc;
		for (dc = scr->drawers; dc != NULL; dc = dc->next) {
			wapp->app_icon = findDockIconFor(dc->adrawer, wwin, main_window, client_pid);
			if (wapp->app_icon)
				break;
		}
	}

	/* If created, then set some flags */
	if (wapp->app_icon && !WFLAGP(wapp->main_window_desc, no_appicon)) {
		WWindow *mainw = wapp->main_window_desc;

		wapp->app_icon->running = 1;
		wapp->app_icon->icon->owner = mainw;

		if (!WFLAGP(wwin, always_user_icon)) {
			if (mainw->wm_hints && (mainw->wm_hints->flags & XCB_ICCCM_WM_HINT_ICON_WINDOW))
				wapp->app_icon->icon->icon_win = mainw->wm_hints->icon_window;

			/* Update the icon images */
			wIconUpdate(wapp->app_icon->icon);

			/* Paint it */
			wAppIconPaint(wapp->app_icon);
		}
	}
}

/* Add the appicon to the appiconlist */
static void add_to_appicon_list(WScreen *scr, WAppIcon *appicon)
{
	appicon->prev = NULL;
	appicon->next = scr->app_icon_list;
	if (scr->app_icon_list)
		scr->app_icon_list->prev = appicon;

	scr->app_icon_list = appicon;
}

/* Remove the appicon from the appiconlist */
static void remove_from_appicon_list(WScreen *scr, WAppIcon *appicon)
{
	if (appicon == scr->app_icon_list) {
		if (appicon->next)
			appicon->next->prev = NULL;
		scr->app_icon_list = appicon->next;
	} else {
		if (appicon->next)
			appicon->next->prev = appicon->prev;
		if (appicon->prev)
			appicon->prev->next = appicon->next;
	}

	appicon->prev = NULL;
	appicon->next = NULL;
}
