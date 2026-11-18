/*
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

#include <WINGs/WXHints.h>
#include <string.h>

#include "WindowMaker.h"
#include "menu.h"
#include "window.h"
#ifdef USER_MENU
#include "usermenu.h"
#endif				/* USER_MENU */
#include "appicon.h"
#include "application.h"
#include "appmenu.h"
#include "properties.h"
#include "workspace.h"
#include "dock.h"
#include "defaults.h"
#include "wsound.h"
#include "misc.h"


/******** Local variables ********/

static WWindow *makeMainWindow(WScreen *scr, xcb_window_t window, WWindow *src)
{
	WWindow *wwin;
	WXWindowAttributes attr;

	wwin = wWindowCreate();
	wwin->screen_ptr = scr;
	wwin->client_win = window;
	wwin->main_window = window;

	if (src) {
		/* the main window is either the just-managed client window
		 * or the fake group leader wmaker created for it, copy what
		 * wManageWindow already decoded instead of re-reading
		 * WM_HINTS and WM_CLASS */
		if (src->wm_hints) {
			wwin->wm_hints = WXAllocWMHints();
			*wwin->wm_hints = *src->wm_hints;
			if (window != src->client_win) {
				/* fake leader, createFakeWindowGroupLeader
				 * pointed window_group at itself */
				wwin->wm_hints->window_group = window;
				wwin->wm_hints->flags |= XCB_ICCCM_WM_HINT_WINDOW_GROUP;
			}
		}
		wwin->wm_instance = (char *)wintern(src->wm_instance);
		wwin->wm_class = (char *)wintern(src->wm_class);
		/* the just-managed client already has CLIENT_EVENTS selected
		 * which covers PropertyChange and StructureNotify, the fake
		 * leader is fresh so select here */
		if (window != src->client_win)
			xcb_change_window_attributes(wdisp->conn, window, XCB_CW_EVENT_MASK, (const uint32_t[]){ XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY });
	} else {
		if (!wXGetWindowAttributes(wdisp->conn, window, &attr, NULL)) {
			wWindowDestroy(wwin);
			return NULL;
		}
		wwin->wm_hints = WXGetWMHints(wdisp->conn, window);
		PropGetWMClass(window, &wwin->wm_class, &wwin->wm_instance);
		xcb_change_window_attributes(wdisp->conn, window, XCB_CW_EVENT_MASK, (const uint32_t[]){ attr.your_event_mask | XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY });
	}

	wDefaultFillAttributes(wwin->wm_instance, wwin->wm_class, &wwin->user_flags,
			       &wwin->defined_user_flags, true);

	return wwin;
}

WApplication *wApplicationOf(xcb_window_t window)
{
	WApplication *wapp;

	if (window == XCB_NONE)
		return NULL;
	wapp = WMHashGet(wdisp->context.app_win, (void *)(uintptr_t) window);
	if (wapp == NULL)
		return NULL;
	return wapp;
}

/* Most recently focused window of the application asking for attention,
 * NULL when none is. */
WWindow *wApplicationUrgentWindow(WApplication *wapp)
{
	WScreen *scr;
	WWindow *wwin;

	if (!wapp || !wapp->main_window_desc)
		return NULL;

	scr = wapp->main_window_desc->screen_ptr;
	wwin = scr->focused_window;

	while (wwin) {
		if (wwin->main_window == wapp->main_window
		    && (wwin->flags.urgent || wwin->flags.net_demands_attention))
			return wwin;
		wwin = wwin->prev;
	}

	return NULL;
}

/* Window of the application to activate after cur, wrapping in manage
 * order, NULL when cur is not one of the application's windows or when
 * the application has no other window to go to. */
WWindow *wApplicationNextWindow(WApplication *wapp, WWindow *cur)
{
	WScreen *scr;
	WWindow *wlist, *next = NULL, *first = NULL;

	if (!wapp || !wapp->main_window_desc || !cur)
		return NULL;
	if (cur->main_window != wapp->main_window)
		return NULL;

	scr = wapp->main_window_desc->screen_ptr;
	wlist = scr->focused_window;
	if (!wlist)
		return NULL;
	while (wlist->prev)
		wlist = wlist->prev;

	for (; wlist; wlist = wlist->next) {
		if (wlist == cur || wlist->flags.internal_window)
			continue;
		if (wlist->main_window != wapp->main_window)
			continue;
		if (WFLAGP(wlist, no_focusable))
			continue;
		if (!first || wlist->creation_id < first->creation_id)
			first = wlist;
		if (wlist->creation_id > cur->creation_id
		    && (!next || wlist->creation_id < next->creation_id))
			next = wlist;
	}

	return next ? next : first;
}

WApplication *wApplicationCreate(WWindow * wwin)
{
	WScreen *scr = wwin->screen_ptr;
	xcb_window_t main_window = wwin->main_window;
	WApplication *wapp;
	WWindow *leader;
	WWindow *src;
	WXGeometry xg;

	if (main_window == XCB_NONE || main_window == scr->root_win)
		return NULL;

	wapp = wApplicationOf(main_window);
	if (wapp) {
		wapp->refcount++;
		if (wapp->app_icon && wapp->app_icon->docked &&
		    wapp->app_icon->relaunching && wapp->main_window_desc->fake_group)
			wDockFinishLaunch(wapp->app_icon);

		return wapp;
	}

	/* when the main window is the just-managed client or the fake
	 * group leader wmaker created for it, everything makeMainWindow
	 * needs is already on wwin and the window is known to exist, so
	 * skip the GetGeometry round-trip and thread the decoded hints
	 * through */
	if (main_window == wwin->client_win)
		src = wwin;
	else if (wwin->fake_group && wwin->fake_group->leader == main_window)
		src = wwin;
	else
		src = NULL;

	/* check if the window is valid */
	if (!src && !WXRcvGeometry(wdisp->conn, WXReqGeometry(wdisp->conn, main_window), &xg))
		return NULL;

	wapp = wmalloc(sizeof(WApplication));

	wapp->refcount = 1;
	wapp->last_focused = NULL;
	wapp->urgent_bounce_timer = NULL;

	wapp->last_workspace = 0;

	wapp->main_window = main_window;
	wapp->main_window_desc = makeMainWindow(scr, main_window, src);
	if (!wapp->main_window_desc) {
		wfree(wapp);
		return NULL;
	}

	wapp->main_window_desc->fake_group = wwin->fake_group;
	wapp->main_window_desc->net_icon_image = RRetainImage(wwin->net_icon_image);

	leader = wWindowFor(main_window);
	if (leader)
		leader->main_window = main_window;

	/* createFakeWindowGroupLeader never sets _WINDOWMAKER_MENU on the
	 * fake leader, so skip the read */
	if (wwin->fake_group && wwin->fake_group->leader == main_window)
		wapp->menu = NULL;
	else
		wapp->menu = wAppMenuGet(scr, main_window);
#ifdef USER_MENU
	if (!wapp->menu)
		wapp->menu = wUserMenuGet(scr, wapp->main_window_desc);

	wAppMenuMap(wapp->menu, wwin);
#endif

	/* Set application wide attributes from the leader */
	wapp->flags.hidden = WFLAGP(wapp->main_window_desc, start_hidden);
	wapp->flags.emulated = WFLAGP(wapp->main_window_desc, emulate_appicon);

	/* application descriptor */
	WMHashInsert(wdisp->context.app_win, (void *)(uintptr_t) main_window, wapp);

	/* link into the per-screen application list, wApplicationDestroy
	 * unlinks from here and the list keeps wapp reachable from
	 * w_global until wApplicationDestroy removes the hash entry */
	wapp->prev = NULL;
	wapp->next = scr->wapp_list;
	if (scr->wapp_list)
		scr->wapp_list->prev = wapp;
	scr->wapp_list = wapp;

	create_appicon_for_application(wapp, wwin);
	wSoundPlay(wwin->screen_ptr, WSOUND_APPSTART);

	return wapp;
}

void wApplicationDestroy(WApplication *wapp)
{
	WWindow *wwin;
	WScreen *scr;

	if (!wapp)
		return;

	wapp->refcount--;
	if (wapp->refcount > 0)
		return;

	WMDeleteTimerHandlerPtr(&wapp->urgent_bounce_timer);
	wAppBounceCancel(wapp);

	scr = wapp->main_window_desc->screen_ptr;

	if (wapp == scr->wapp_list) {
		if (wapp->next)
			wapp->next->prev = NULL;
		scr->wapp_list = wapp->next;
	} else {
		if (wapp->next)
			wapp->next->prev = wapp->prev;
		if (wapp->prev)
			wapp->prev->next = wapp->next;
	}

	WMHashRemove(wdisp->context.app_win, (void *)(uintptr_t) wapp->main_window);
	wAppMenuDestroy(wapp->menu);

	/* Remove application icon */
	removeAppIconFor(wapp);

	wwin = wWindowFor(wapp->main_window_desc->client_win);

	wWindowDestroy(wapp->main_window_desc);
	if (wwin) {
		/* undelete client window descriptor that was deleted in
		 * wWindowDestroy */
		WMHashInsert(wdisp->context.client_win, (void *)(uintptr_t) wwin->client_win,
			     &wwin->client_descriptor);
	}
	wSoundPlay(scr, WSOUND_APPEXIT);
	wfree(wapp);
}

void wApplicationActivate(WApplication *wapp)
{
	if (wapp->app_icon) {
		wIconSetHighlited(wapp->app_icon->icon, true);
		wAppIconPaint(wapp->app_icon);
	}
}

void wApplicationDeactivate(WApplication *wapp)
{
	if (wapp->app_icon) {
		wIconSetHighlited(wapp->app_icon->icon, false);
		wAppIconPaint(wapp->app_icon);
	}
}
