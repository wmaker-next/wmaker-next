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

#include <X11/Xlib.h>
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


static int matchWindow(const void *item, const void *cdata)
{
	return (((WFakeGroupLeader *) item)->origLeader == (Window) cdata);
}

static void handleMapRequest(const xcb_generic_event_t *xe, XEvent *ev)
{
	const xcb_map_request_event_t *e = (const xcb_map_request_event_t *)xe;
	WWindow *wwin;
	WScreen *scr;
	(void) ev;

	wwin = wWindowFor(e->window);
	if (wwin != NULL) {
		if (wwin->flags.shaded) {
			wUnshadeWindow(wwin);
		}
		/* deiconify window */
		if (wwin->flags.miniaturized) {
			wDeiconifyWindow(wwin);
		} else if (wwin->flags.hidden) {
			WApplication *wapp = wApplicationOf(wwin->main_window);
			/* go to the last workspace that the user worked on the app */
			if (wapp) {
				wWorkspaceChange(wwin->screen_ptr, wapp->last_workspace);
			}
			wUnhideApplication(wapp, False, False);
		}
		return;
	}

	scr = wScreenForRootWindow(e->parent);

	wwin = wManageWindow(scr, e->window, NULL);

	/*
	 * This is to let the Dock know that the application it launched
	 * has already been mapped (eg: it has finished launching).
	 * It is not necessary for normally docked apps, but is needed for
	 * apps that were forcedly docked (like with dockit).
	 */
	if (scr->last_dock) {
		pid_t client_pid = wwin ? wwin->client_pid : wGetWindowPid(e->window);
		Window track = (wwin && wwin->main_window != None
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
		wClientSetState(wwin, NormalState, None);
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

static void handleDestroyNotify(const xcb_generic_event_t *xe, XEvent *ev)
{
	const xcb_destroy_notify_event_t *e = (const xcb_destroy_notify_event_t *)xe;
	WWindow *wwin;
	WApplication *app;
	WScreen *scr;
	int widx;
	(void) ev;

	wwin = wWindowFor(e->window);
	if (wwin) {
		wUnmanageWindow(wwin, False, True);
	}

	scr = wScreenForRootWindow(e->event);
	if (scr != NULL) {
		while ((widx = WMFindInArray(scr->fakeGroupLeaders, matchWindow,
					     (void *)(uintptr_t)e->window)) != WANotFound) {
			WFakeGroupLeader *fPtr;

			fPtr = WMGetFromArray(scr->fakeGroupLeaders, widx);
			if (fPtr->retainCount > 0) {
				fPtr->retainCount--;
				if (fPtr->retainCount == 0 && fPtr->leader != None) {
					XDestroyWindow(dpy, fPtr->leader);
					fPtr->leader = None;
					XFlush(dpy);
				}
			}
			fPtr->origLeader = None;
		}
	}

	app = wApplicationOf(e->window);
	if (app) {
		if (e->window == app->main_window) {
			app->refcount = 0;
			wwin = app->main_window_desc->screen_ptr->focused_window;
			while (wwin) {
				if (wwin->main_window == e->window) {
					wwin->main_window = None;
				}
				wwin = wwin->prev;
			}
		}
		wApplicationDestroy(app);
	}
}

static void handleMapNotify(const xcb_generic_event_t *xe, XEvent *ev)
{
	const xcb_map_notify_event_t *e = (const xcb_map_notify_event_t *)xe;
	WWindow *wwin;
	(void) ev;

	wwin = wWindowFor(e->event);
	if (wwin && wwin->client_win == e->event) {
		if (wwin->flags.miniaturized) {
			wDeiconifyWindow(wwin);
		} else {
			XGrabServer(dpy);
			wWindowMap(wwin);
			wClientSetState(wwin, NormalState, None);
			XUngrabServer(dpy);
		}
	}
}

static void handleUnmapNotify(const xcb_generic_event_t *xe, XEvent *ev)
{
	const xcb_unmap_notify_event_t *e = (const xcb_unmap_notify_event_t *)xe;
	WWindow *wwin;
	XEvent peek;
	Bool withdraw = False;

	/* only process windows with StructureNotify selected
	 * (ignore SubstructureNotify) */
	wwin = wWindowFor(e->window);
	if (!wwin)
		return;

	/* whether the event is a Withdrawal request */
	if (e->event == wwin->screen_ptr->root_win && ev->xany.send_event)
		withdraw = True;

	if (wwin->client_win != e->event && !withdraw)
		return;

	if (!wwin->flags.mapped && !withdraw
	    && wWindowIsVisibleOnAnyHead(wwin)
	    && !wwin->flags.miniaturized && !wwin->flags.hidden)
		return;

	XGrabServer(dpy);
	XUnmapWindow(dpy, wwin->frame->core->window);
	wwin->flags.mapped = 0;
	XSync(dpy, 0);
	/* check if the window was destroyed */
	if (WXEvqTakeTyped(dpy, DestroyNotify, wwin->client_win, &peek)) {
		/* DispatchEvent reaches wUnmanageWindow and the
		 * WMNUnmanaged observers, which issue several X
		 * round-trips; release the server grab first so other
		 * clients are not stalled while we tear the window down */
		XUngrabServer(dpy);
		DispatchEvent(&peek);
	} else {
		Bool reparented = False;

		if (WXEvqTakeTyped(dpy, ReparentNotify, wwin->client_win, &peek))
			reparented = True;

		/* withdraw window */
		wwin->flags.mapped = 0;
		if (!reparented)
			wClientSetState(wwin, WithdrawnState, None);

		XUngrabServer(dpy);
		/* if the window was reparented, do not reparent it back to the
		 * root window */
		wUnmanageWindow(wwin, !reparented, False);
	}
}

static void handleConfigureRequest(const xcb_generic_event_t *xe, XEvent *ev)
{
	const xcb_configure_request_event_t *e = (const xcb_configure_request_event_t *)xe;

	(void) ev;
	wClientConfigure(wWindowFor(e->window), e);
}

static void handlePropertyNotify(const xcb_generic_event_t *xe, XEvent *ev)
{
	const xcb_property_notify_event_t *e = (const xcb_property_notify_event_t *)xe;
	WWindow *wwin;
	WApplication *wapp;

	(void) ev;

	wwin = wWindowFor(e->window);
	if (wwin) {
		wClientCheckProperty(wwin, e->atom);
	} else {
		WScreen *scr = wScreenForRootWindow(e->window);
		if (scr && scr->root_win == e->window) {
			/* xrdb writes RESOURCE_MANAGER only on screen 0's
			 * root, but Xft.dpi is a session-wide value so a
			 * change there re-reads for every managed screen */
			if (e->atom == XCB_ATOM_RESOURCE_MANAGER) {
				int i;
				for (i = 0; i < w_global.screen_count; i++)
					WMScreenReadUIScale(wScreenWithNumber(i)->wmscreen);
				return;
			}
			wNETWMCheckRootHintChange(scr, e->atom);
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

static void handleExpose(const xcb_generic_event_t *xe, XEvent *ev)
{
	const xcb_expose_event_t *e = (const xcb_expose_event_t *)xe;
	WObjDescriptor *desc;
	XEvent junk;

	while (WXEvqTakeTyped(dpy, Expose, e->window, &junk)) ;

	if (XFindContext(dpy, e->window, w_global.context.client_win, (XPointer *) & desc) == XCNOENT) {
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
