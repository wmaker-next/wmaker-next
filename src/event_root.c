/* event_root.c - root-window protocol messages: ClientMessage routing,
 *                ICCCM/WM_Sn selection ownership, colormap, visibility
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

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

#ifdef USE_DOCK_XDND
#include "xdnd.h"
#endif

#include "WindowMaker.h"
#include "GNUstep.h"
#include "window.h"
#include "actions.h"
#include "application.h"
#include "appicon.h"
#include "icon.h"
#include "stacking.h"
#include "defaults.h"
#include "framewin.h"
#include "wmspec.h"
#include "colormap.h"
#include "screen.h"
#include "shutdown.h"
#include "event.h"
#include "startup_notify.h"


static void handleClientMessage(const xcb_generic_event_t *xe, XEvent *ev)
{
	const xcb_client_message_event_t *e = (const xcb_client_message_event_t *)xe;
	WWindow *wwin;
	WObjDescriptor *desc;

	/* handle transition from Normal to Iconic state */
	if (e->type == w_global.atom.wm.change_state
	    && e->format == 32 && e->data.data32[0] == IconicState) {

		wwin = wWindowFor(e->window);
		if (!wwin)
			return;
		if (!wwin->flags.miniaturized)
			wIconifyWindow(wwin);
	} else if (e->type == w_global.atom.wm.colormap_notify && e->format == 32) {
		WScreen *scr = wScreenForRootWindow(e->window);

		if (!scr)
			return;

		if (e->data.data32[1] == 1) {	/* starting */
			wColormapAllowClientInstallation(scr, True);
		} else {	/* stopping */
			wColormapAllowClientInstallation(scr, False);
		}
	} else if (e->type == w_global.atom.wmaker.command) {

		char command[sizeof(e->data.data8) + 1];

		/* WPrefs and setstyle send this as format 8, anything else
		 * means data8 is not the right view of the payload */
		if (e->format != 8)
			return;

		memcpy(command, e->data.data8, sizeof(e->data.data8));
		command[sizeof(e->data.data8)] = '\0';

		/* the X-property control channel is deprecated, the in-tree
		 * senders use the Unix-socket IPC and only fall back here
		 * when connect() fails, anything else should switch to
		 * `wmakermsg reconfigure` */
		if (strncmp(command, "Reconfigure", sizeof("Reconfigure")) == 0) {
			wwarning(_("Got Reconfigure command over the deprecated X channel, use wmakermsg"));
			wDefaultsCheckDomains(NULL);
		} else {
			wwarning(_("Got unknown command %s over the deprecated X channel"), command);
		}

	} else if (e->type == w_global.atom.wmaker.wm_function && e->format == 32) {
		WApplication *wapp;

		/* the sender may target either the group leader (then
		 * wApplicationOf finds it directly) or any managed window
		 * of the application, both arms used to be open-coded
		 * with the same switch, resolve once and hand each
		 * function the pointer it needs */
		wapp = wApplicationOf(e->window);
		wwin = wapp ? wapp->main_window_desc : wWindowFor(e->window);
		if (!wapp && wwin)
			wapp = wApplicationOf(wwin->main_window);

		switch (e->data.data32[0]) {
		case WMFHideOtherApplications:
			if (wwin)
				wHideOtherApplications(wwin);
			break;
		case WMFHideApplication:
			if (wapp)
				wHideApplication(wapp);
			break;
		}
	} else if (e->type == w_global.atom.gnustep.wm_attr && e->format == 32) {
		wwin = wWindowFor(e->window);
		if (!wwin || !wwin->frame)
			return;
		switch (e->data.data32[0]) {
		case GSWindowLevelAttr:
			{
				int level = (int)e->data.data32[1];

				/* INT_MIN is the only illegal window level,
				 * matching wWindowGetInitialLevel */
				if (level != INT_MIN && WINDOW_LEVEL(wwin) != level)
					ChangeStackingLevel(wwin->frame->core, level);
			}
			break;
		}
	} else if (e->type == w_global.atom.gnustep.titlebar_state && e->format == 32) {
		wwin = wWindowFor(e->window);
		if (!wwin || !wwin->frame)
			return;
		switch (e->data.data32[0]) {
		case WMTitleBarNormal:
			wFrameWindowChangeState(wwin->frame, WS_UNFOCUSED);
			break;
		case WMTitleBarMain:
			wFrameWindowChangeState(wwin->frame, WS_PFOCUSED);
			break;
		case WMTitleBarKey:
			wFrameWindowChangeState(wwin->frame, WS_FOCUSED);
			break;
		}
	} else if (e->type == w_global.atom.wm.ignore_focus_events && e->format == 32) {
		WScreen *scr = wScreenForRootWindow(e->window);
		if (!scr)
			return;
		scr->flags.ignore_focus_events = e->data.data32[0] ? 1 : 0;
	} else if (wStartupNotifyProcessXcbEvent(xe)) {
		/* _NET_STARTUP_INFO{,_BEGIN}, consumed by libsn */
	} else if (wNETWMProcessClientMessage(&ev->xclient)) {
		/* do nothing */
#ifdef USE_DOCK_XDND
	} else if (wXDNDProcessClientMessage(&ev->xclient)) {
		/* do nothing */
#endif	/* USE_DOCK_XDND */
	} else {
		/*
		 * Non-standard thing, but needed by OffiX DND.
		 * For when the icon frame gets a ClientMessage
		 * that should have gone to the icon_window.
		 */
		if (XFindContext(dpy, e->window, w_global.context.client_win, (XPointer *) & desc) != XCNOENT) {
			struct WIcon *icon = NULL;

			if (desc->parent_type == WCLASS_MINIWINDOW) {
				icon = (WIcon *) desc->parent;
			} else if (desc->parent_type == WCLASS_DOCK_ICON || desc->parent_type == WCLASS_APPICON) {
				icon = ((WAppIcon *) desc->parent)->icon;
			}
			if (icon && (wwin = icon->owner)) {
				if (wwin->client_win != e->window) {
					ev->xclient.window = wwin->client_win;
					XSendEvent(dpy, wwin->client_win, False, NoEventMask, ev);
				}
			}
		}
	}
}

static void handleColormapNotify(const xcb_generic_event_t *xe, XEvent *ev)
{
	const xcb_colormap_notify_event_t *e = (const xcb_colormap_notify_event_t *)xe;
	WWindow *wwin;
	WScreen *scr;
	Bool reinstall = False;

	wwin = wWindowFor(e->window);
	if (!wwin)
		return;

	scr = wwin->screen_ptr;

	do {
		if (wwin) {
			if (e->_new) {
				if (e->window == wwin->client_win)
					wwin->colormap = e->colormap;

				if (wwin == scr->cmap_window && wwin->cmap_window_no == 0)
					scr->current_colormap = e->colormap;

				reinstall = True;
			} else if (e->state == XCB_COLORMAP_STATE_UNINSTALLED &&
				   scr->current_colormap == e->colormap) {

				/* some bastard app (like XV) removed our colormap */
				/*
				 * can't enforce or things like xscreensaver won't work
				 * reinstall = True;
				 */
			} else if (e->state == XCB_COLORMAP_STATE_INSTALLED &&
				   scr->current_colormap == e->colormap) {

				/* someone has put our colormap back */
				reinstall = False;
			}
		}
	} while (WXEvqTakeTyped(dpy, ColormapNotify, None, ev)
		 && (e = (const xcb_colormap_notify_event_t *)((WXWireEvent *)ev)->wire,
		     (wwin = wWindowFor(e->window)) || 1));

	if (reinstall && scr->current_colormap != None) {
		if (!scr->flags.colormap_stuff_blocked)
			XInstallColormap(dpy, scr->current_colormap);
	}
}

static void handleVisibilityNotify(const xcb_generic_event_t *xe, XEvent *ev)
{
	const xcb_visibility_notify_event_t *e = (const xcb_visibility_notify_event_t *)xe;
	WWindow *wwin;
	(void) ev;

	wwin = wWindowFor(e->window);
	if (!wwin)
		return;
	wwin->flags.obscured = (e->state == XCB_VISIBILITY_FULLY_OBSCURED);
}

#ifdef USE_DOCK_XDND
static void handleSelectionNotify(const xcb_generic_event_t *xe, XEvent *ev)
{
	(void) xe;
	wXDNDProcessSelection(ev);
}
#endif

static void handleSelectionRequest(const xcb_generic_event_t *xe, XEvent *ev)
{
#ifdef USE_ICCCM_WMREPLACE
	const xcb_selection_request_event_t *e = (const xcb_selection_request_event_t *)xe;
	static Atom atom_version = None;
	WScreen *scr;
	XSelectionEvent notify;
	(void) ev;

	/*
	 * This event must be sent to the slection requester to not block him
	 *
	 * We create it with the answer 'there is no selection' by default
	 */
	notify.type = SelectionNotify;
	notify.display = dpy;
	notify.requestor = e->requestor;
	notify.selection = e->selection;
	notify.target = e->target;
	notify.property = None; /* This says that there is no selection */
	notify.time = e->time;

	scr = wScreenForWindow(e->owner);
	if (!scr)
		goto not_our_selection;

	if (e->owner != scr->info_window)
		goto not_our_selection;

	if (e->selection != scr->sn_atom)
		goto not_our_selection;

	if (atom_version == None)
		atom_version = XInternAtom(dpy, "VERSION", False);

	if (e->target == atom_version) {
		static const long icccm_version[] = { 2, 0 };

		/*
		 * This protocol is defined in ICCCM 2.0:
		 * http://www.x.org/releases/X11R7.7/doc/xorg-docs/icccm/icccm.html
		 *  "Communication with the Window Manager by Means of Selections"
		 */

		/*
		 * Setting the property means the content of the selection is available
		 * According to the ICCCM spec, we need to support being asked for a property
		 * set to 'None' for compatibility with old clients
		 */
		notify.property = (e->property == XCB_NONE) ? e->target : e->property;

		XChangeProperty(dpy, e->requestor, notify.property,
		                XA_INTEGER, 32, PropModeReplace,
		                (unsigned char *) icccm_version, wlengthof(icccm_version));
	}

 not_our_selection:
	if (notify.property == None)
		wwarning("received SelectionRequest(%s) for target=\"%s\" from requestor 0x%lX but we have no answer",
		         XGetAtomName(dpy, e->selection), XGetAtomName(dpy, e->target), (long) e->requestor);

	/* Send the answer to the requestor */
	XSendEvent(dpy, e->requestor, False, 0L, (XEvent *) &notify);
#else
	(void) xe;
	(void) ev;
#endif
}

static void handleSelectionClear(const xcb_generic_event_t *xe, XEvent *ev)
{
#ifdef USE_ICCCM_WMREPLACE
	const xcb_selection_clear_event_t *e = (const xcb_selection_clear_event_t *)xe;
	WScreen *scr = wScreenForWindow(e->owner);
	(void) ev;

	if (!scr)
		return;

	if (e->selection != scr->sn_atom)
		return;

	wmessage(_("another window manager is replacing us!"));
	Shutdown(WSExitMode, True);
#else
	(void) xe;
	(void) ev;
#endif
}

void wEventRootRegister(void)
{
	wEventRegisterHandler(XCB_CLIENT_MESSAGE, handleClientMessage);
	wEventRegisterHandler(XCB_COLORMAP_NOTIFY, handleColormapNotify);
	wEventRegisterHandler(XCB_VISIBILITY_NOTIFY, handleVisibilityNotify);
	wEventRegisterHandler(XCB_SELECTION_REQUEST, handleSelectionRequest);
	wEventRegisterHandler(XCB_SELECTION_CLEAR, handleSelectionClear);
#ifdef USE_DOCK_XDND
	wEventRegisterHandler(XCB_SELECTION_NOTIFY, handleSelectionNotify);
#endif
}
