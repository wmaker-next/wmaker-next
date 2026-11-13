/* event_client.c - client-window lifecycle and substructure events
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2014-2026 Window Maker Team
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

#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include "WindowMaker.h"
#include "window.h"
#include "actions.h"
#include "client.h"
#include "application.h"
#include "workspace.h"
#include "dock.h"
#include "tabgroup.h"
#include "framewin.h"
#include "xinerama.h"
#include "wmspec.h"
#include "screen.h"
#include "event.h"
#include "xsettings.h"
#include "properties.h"
#include "startup.h"

#define UI_SCALE_RESTART_DELAY 300

static WMHandlerID ui_scale_restart_timer;

static void uiScaleRestart(void *data)
{
	(void)data;

	ui_scale_restart_timer = NULL;
	wRequestRestart();
}

/* compare the configured Xft.dpi with the scale the session was laid
 * out with, uiScale itself is startup-only so this is the only signal
 * that a restart is needed
 */
static bool uiScaleDiffers(void)
{
	int i;

	for (i = 0; i < wdisp->screen_count; i++) {
		WMScreen *scr = wScreenWithNumber(i)->wmscreen;
		double applied = WMScreenUIScale(scr);
		double configured = WMScreenPeekUIScale(scr);

		if ((applied - configured) * (applied - configured) > 0.0001)
			return true;
	}
	return false;
}


static void handleResourceManagerChange(void)
{
	if (uiScaleDiffers()) {
		if (ui_scale_restart_timer)
			WMDeleteTimerHandler(ui_scale_restart_timer);
		ui_scale_restart_timer =
		    WMAddTimerHandler(UI_SCALE_RESTART_DELAY,
				      uiScaleRestart, NULL);
		return;
	}
	if (ui_scale_restart_timer) {
		WMDeleteTimerHandler(ui_scale_restart_timer);
		ui_scale_restart_timer = NULL;
	}
	/* the UI scale the XSETTINGS payload derives Xft/DPI from may
	 * have changed with it */
	wXSettingsRefreshAll();
}


static int matchWindow(const void *item, const void *cdata)
{
	return (((WFakeGroupLeader *) item)->origLeader == (xcb_window_t)(uintptr_t) cdata);
}

static void handleMapRequest(xcb_generic_event_t *ev)
{
	const xcb_map_request_event_t *e = (const xcb_map_request_event_t *)ev;
	WWindow *wwin;
	WScreen *scr;

	wwin = wWindowFor(e->window);
	if (wwin != NULL) {
		if (wwin->flags.shaded) {
			wUnshadeWindow(wwin);
		}
		/* deiconify window */
		if (wwin->flags.miniaturized && !wwin->flags.restore_iconified) {
			wDeiconifyWindow(wwin);
		} else if (wwin->flags.hidden) {
			WApplication *wapp = wApplicationOf(wwin->main_window);
			/* go to the last workspace that the user worked on the app */
			if (wapp) {
				wWorkspaceChange(wwin->screen_ptr, wapp->last_workspace);
			}
			wUnhideApplication(wapp, false, false);
		}
		return;
	}

	scr = wScreenForRootWindow(e->parent);
	if (!scr)
		return;

	wwin = wManageWindow(scr, e->window, NULL);

	/*
	 * This is to let the Dock know that the application it launched
	 * has already been mapped (eg: it has finished launching).
	 * It is not necessary for normally docked apps, but is needed for
	 * apps that were forcedly docked (like with dockit).
	 */
	if (scr->last_dock) {
		pid_t client_pid = wwin ? wwin->client_pid : wGetWindowPid(e->window);
		xcb_window_t track = (wwin && wwin->main_window != XCB_NONE
				&& wwin->main_window != e->window)
			       ? wwin->main_window : e->window;

		/* the fake group leader carries copies of the same
		 * class strings and WM_COMMAND wmaker set from wwin,
		 * so hand them through rather than reading them back
		 * per window */
		wDockTrackWindowLaunch(scr->last_dock, track,
				       wwin ? wwin->wm_instance : NULL,
				       wwin ? wwin->wm_class : NULL,
				       wwin ? wwin->wm_command : NULL,
				       client_pid);
	}

	if (wwin) {
		if (!wwin->flags.miniaturized && !wwin->flags.hidden)
			wClientSetState(wwin, XCB_ICCCM_WM_STATE_NORMAL, XCB_NONE);
		if (wwin->flags.maximized) {
			wMaximizeWindow(wwin, wwin->flags.maximized,
					wGetHeadForWindow(wwin));
		}
		if (wwin->flags.shaded) {
			wwin->flags.shaded = 0;
			wwin->flags.skip_next_animation = 1;
			wShadeWindow(wwin);
		}
		if (wwin->flags.miniaturized) {
			wwin->flags.miniaturized = 0;
			wwin->flags.skip_next_animation = 1;
			/* restore_iconified is only granted by the session
			 * paths, not for the client's own iconic hint */
			wIconifyWindow(wwin);
		}
		if (wwin->flags.fullscreen) {
			wwin->flags.fullscreen = 0;
			wFullscreenWindow(wwin);
		}
		if (wwin->flags.hidden) {
			WApplication *wapp = wApplicationOf(wwin->main_window);

			wwin->flags.hidden = 0;
			wwin->flags.skip_next_animation = 1;
			if (wapp) {
				wHideApplication(wapp);
			}
		}
		/* tab-group membership saved in the session is applied after
		 * the deferred shade and iconify so an iconified group is
		 * rebuilt from members that already own a miniwindow */
		wGroupRestoreMember(wwin);
		/* auto-group by WM_CLASS when no session group was restored */
		if (!wwin->tab_group)
			wGroupAutoAttach(wwin);
	}
}

static void handleDestroyNotify(xcb_generic_event_t *ev)
{
	const xcb_destroy_notify_event_t *e = (const xcb_destroy_notify_event_t *)ev;
	WWindow *wwin;
	WApplication *app;
	WScreen *scr;
	int widx;

	wwin = wWindowFor(e->window);
	if (wwin) {
		wUnmanageWindow(wwin, false, true);
	}

	scr = wScreenForRootWindow(e->event);
	if (scr != NULL) {
		while ((widx = WMFindInArray(scr->fakeGroupLeaders, matchWindow,
					     (void *)(uintptr_t)e->window)) != WANotFound) {
			WFakeGroupLeader *fPtr;

			fPtr = WMGetFromArray(scr->fakeGroupLeaders, widx);
			if (fPtr->retainCount > 0) {
				fPtr->retainCount--;
				if (fPtr->retainCount == 0 && fPtr->leader != XCB_NONE) {
					xcb_destroy_window(wdisp->conn, fPtr->leader);
					fPtr->leader = XCB_NONE;
					wXFlush(wdisp->conn);
				}
			}
			fPtr->origLeader = XCB_NONE;
		}
	}

	app = wApplicationOf(e->window);
	if (app) {
		if (e->window == app->main_window) {
			app->refcount = 0;
			wwin = app->main_window_desc->screen_ptr->focused_window;
			while (wwin) {
				if (wwin->main_window == e->window) {
					wwin->main_window = XCB_NONE;
				}
				wwin = wwin->prev;
			}
		}
		wApplicationDestroy(app);
	}
}

static void handleMapNotify(xcb_generic_event_t *ev)
{
	const xcb_map_notify_event_t *e = (const xcb_map_notify_event_t *)ev;
	WWindow *wwin;
	WXCookie state_ck;
	int wm_state;

	wwin = wWindowFor(e->event);
	if (wwin && wwin->client_win == e->event) {
		state_ck = WXReqFormat32(wdisp->conn, e->event, wdisp->atom.wm.state,
					 wdisp->atom.wm.state, 1);
		wm_state = PropRcvWindowState(state_ck);
		if (wm_state == XCB_ICCCM_WM_STATE_ICONIC && !wwin->flags.miniaturized)
			wwin->flags.miniaturized = 1;
		if (wwin->flags.miniaturized) {
			if (!wwin->flags.restore_iconified) {
				wDeiconifyWindow(wwin);
			}
			/* else: keep iconic, skip wWindowMap + NormalState */
		} else {
			wXGrabServer(wdisp->conn);
			wWindowMap(wwin);
			wClientSetState(wwin, XCB_ICCCM_WM_STATE_NORMAL, XCB_NONE);
			wXUngrabServer(wdisp->conn);
		}
	}
}

static void handleUnmapNotify(xcb_generic_event_t *ev)
{
	const xcb_unmap_notify_event_t *e = (const xcb_unmap_notify_event_t *)ev;
	WWindow *wwin;
	xcb_generic_event_t *peek = NULL;
	bool withdraw = false;

	/* only process windows with StructureNotify selected
	 * (ignore SubstructureNotify) */
	wwin = wWindowFor(e->window);
	if (!wwin)
		return;

	/* the ICCCM withdrawal is the one deliberate synthetic unmap */
	if (ev->response_type & 0x80) {
		if (e->event != wwin->screen_ptr->root_win)
			return;
		withdraw = true;
	}

	if (wwin->client_win != e->event && !withdraw)
		return;

	if (!wwin->flags.mapped && !withdraw
	    && wWindowIsVisibleOnAnyHead(wwin)
	    && !wwin->flags.miniaturized && !wwin->flags.hidden)
		return;

	wXGrabServer(wdisp->conn);
	xcb_unmap_window(wdisp->conn, wwin->frame->core->window);
	wwin->flags.mapped = 0;
	wXSync(wdisp->conn);
	/* check if the window was destroyed */
	if (WXEvqTakeTyped(wdisp->conn, XCB_DESTROY_NOTIFY, wwin->client_win, &peek)) {
		/* DispatchEvent reaches wUnmanageWindow and the
		 * WMNUnmanaged observers, which issue several X
		 * round-trips; release the server grab first so other
		 * clients are not stalled while we tear the window down */
		wXUngrabServer(wdisp->conn);
		DispatchEvent(peek);
		free(peek);
		peek = NULL;
	} else {
		bool reparented = false;

		if (WXEvqTakeTyped(wdisp->conn, XCB_REPARENT_NOTIFY, wwin->client_win, &peek)) {
			reparented = true;
			free(peek);
			peek = NULL;
		}

		/* withdraw window */
		wwin->flags.mapped = 0;
		if (!reparented)
			wClientSetState(wwin, XCB_ICCCM_WM_STATE_WITHDRAWN, XCB_NONE);

		wXUngrabServer(wdisp->conn);
		/* if the window was reparented, do not reparent it back to the
		 * root window */
		wUnmanageWindow(wwin, !reparented, false);
	}
}

static void handleConfigureRequest(xcb_generic_event_t *ev)
{
	const xcb_configure_request_event_t *e = (const xcb_configure_request_event_t *)ev;

	wClientConfigure(wWindowFor(e->window), e);
}

static void handlePropertyNotify(xcb_generic_event_t *ev)
{
	const xcb_property_notify_event_t *e = (const xcb_property_notify_event_t *)ev;
	WWindow *wwin;
	WApplication *wapp;


	wwin = wWindowFor(e->window);
	if (wwin) {
		wClientCheckProperty(wwin, e->atom);
	} else {
		WScreen *scr = wScreenForRootWindow(e->window);
		if (scr && scr->root_win == e->window) {
			/* xrdb may publish RESOURCE_MANAGER on any screen's
			 * root, and a change to Xft.dpi cannot be applied to
			 * a running session because wm-side frames and the
			 * WINGs view tree keep their device geometry, so
			 * restart once the resource database settles.
			 */
			if (e->atom == XCB_ATOM_RESOURCE_MANAGER) {
				handleResourceManagerChange();
				return;
			}
			wNETWMCheckRootHintChange(scr, e->atom);
			return;
		}
		/* a secondary-only instance still watches screen 0's root,
		 * where xrdb may be the only place Xft.dpi is published */
		if (e->window == wXRootWindow(wdisp->conn, 0)) {
			if (e->atom == XCB_ATOM_RESOURCE_MANAGER)
				handleResourceManagerChange();
			return;
		}
		/* a client may carry _NET_WM_USER_TIME on a separate
		 * helper window, route those notifies to the owner */
		wwin = wNETWMUserTimeWindowFor(e->window);
		if (wwin) {
			wNETWMUserTimeNotify(wwin);
			return;
		}
	}
	wapp = wApplicationOf(e->window);
	if (wapp) {
		wClientCheckProperty(wapp->main_window_desc, e->atom);
	}
}

static void handleExpose(xcb_generic_event_t *ev)
{
	const xcb_expose_event_t *e = (const xcb_expose_event_t *)ev;
	WObjDescriptor *desc;
	xcb_generic_event_t *junk = NULL;

	while (WXEvqTakeTyped(wdisp->conn, XCB_EXPOSE, e->window, &junk))
		free(junk);

	desc = WMHashGet(wdisp->context.client_win, (void *)(uintptr_t) e->window);
	if (desc == NULL) {
		return;
	}

	if (desc->handle_expose) {
		(*desc->handle_expose) (desc, ev);
	}
}

void wEventClientRegister(void)
{
	wEventRegisterHandler(XCB_MAP_REQUEST, handleMapRequest);
	wEventRegisterHandler(XCB_DESTROY_NOTIFY, handleDestroyNotify);
	wEventRegisterHandler(XCB_MAP_NOTIFY, handleMapNotify);
	wEventRegisterHandler(XCB_UNMAP_NOTIFY, handleUnmapNotify);
	wEventRegisterHandler(XCB_CONFIGURE_REQUEST, handleConfigureRequest);
	wEventRegisterHandler(XCB_PROPERTY_NOTIFY, handlePropertyNotify);
	wEventRegisterHandler(XCB_EXPOSE, handleExpose);
}
