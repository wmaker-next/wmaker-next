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
#include <stdlib.h>
#include <string.h>

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
#include "xsettings.h"
#include "startup_notify.h"


static void handleClientMessage(xcb_generic_event_t *ev)
{
	const xcb_client_message_event_t *e = (const xcb_client_message_event_t *)ev;
	WWindow *wwin;
	WObjDescriptor *desc;


	/* handle transition from Normal to Iconic state */
	if (e->type == wdisp->atom.wm.change_state
	    && e->format == 32 && e->data.data32[0] == XCB_ICCCM_WM_STATE_ICONIC) {

		wwin = wWindowFor(e->window);
		if (!wwin)
			return;
		if (!wwin->flags.miniaturized)
			wIconifyWindow(wwin);
	} else if (e->type == wdisp->atom.wm.colormap_notify && e->format == 32) {
		WScreen *scr = wScreenForRootWindow(e->window);

		if (!scr)
			return;

		if (e->data.data32[1] == 1) {	/* starting */
			wColormapAllowClientInstallation(scr, true);
		} else {	/* stopping */
			wColormapAllowClientInstallation(scr, false);
		}
	} else if (e->type == wdisp->atom.wmaker.command) {

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
			wDefaultsReconfigure();
		} else {
			wwarning(_("Got unknown command %s over the deprecated X channel"), command);
		}

	} else if (e->type == wdisp->atom.wmaker.wm_function && e->format == 32) {
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
	} else if (e->type == wdisp->atom.gnustep.wm_attr && e->format == 32) {
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
	} else if (e->type == wdisp->atom.gnustep.titlebar_state && e->format == 32) {
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
	} else if (e->type == wdisp->atom.wm.ignore_focus_events && e->format == 32) {
		WScreen *scr = wScreenForRootWindow(e->window);
		if (!scr)
			return;
		scr->flags.ignore_focus_events = e->data.data32[0] ? 1 : 0;
	} else if (wStartupNotifyProcessXcbEvent(ev)) {
		/* _NET_STARTUP_INFO{,_BEGIN}, consumed by libsn */
	} else if (wNETWMProcessClientMessage(e)) {
		/* do nothing */
#ifdef USE_DOCK_XDND
	} else if (wXDNDProcessClientMessage(e)) {
		/* do nothing */
#endif	/* USE_DOCK_XDND */
	} else {
		/* an unrecognised ClientMessage that landed on an icon
		 * frame is forwarded to the reparented icon_window so a
		 * client speaking a private protocol to what it thinks is
		 * its own icon still receives it */
		desc = WMHashGet(wdisp->context.client_win, (void *)(uintptr_t) e->window);
		if (desc != NULL) {
			struct WIcon *icon = NULL;
			xcb_client_message_event_t fwd;

			if (desc->parent_type == WCLASS_MINIWINDOW) {
				icon = (WIcon *) desc->parent;
			} else if (desc->parent_type == WCLASS_DOCK_ICON || desc->parent_type == WCLASS_APPICON) {
				icon = ((WAppIcon *) desc->parent)->icon;
			}
			if (icon && (wwin = icon->owner)) {
				if (wwin->client_win != e->window) {
					fwd = *e;
					fwd.window = wwin->client_win;
					WXSendEvent32(wdisp->conn, &fwd, sizeof(fwd), 0,
						      wwin->client_win, 0);
				}
			}
		}
	}
}

static void handleColormapNotify(xcb_generic_event_t *ev)
{
	const xcb_colormap_notify_event_t *e = (const xcb_colormap_notify_event_t *)ev;
	WWindow *wwin;
	WScreen *scr;
	bool reinstall = false;
	xcb_generic_event_t *owned = NULL;

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

				reinstall = true;
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
				reinstall = false;
			}
		}

		/* the event this iteration processed is fully handled,
		 * hand it back before the condition takes the next one */
		if (owned) {
			free(owned);
			owned = NULL;
		}
	} while (WXEvqTakeTyped(wdisp->conn, XCB_COLORMAP_NOTIFY, XCB_NONE, &owned)
		 && (e = (const xcb_colormap_notify_event_t *)owned,
		     (wwin = wWindowFor(e->window)) || 1));

	if (reinstall && scr->current_colormap != XCB_NONE) {
		if (!scr->flags.colormap_stuff_blocked)
			xcb_install_colormap(wdisp->conn, scr->current_colormap);
	}
}

static void handleVisibilityNotify(xcb_generic_event_t *ev)
{
	const xcb_visibility_notify_event_t *e = (const xcb_visibility_notify_event_t *)ev;
	WWindow *wwin;

	wwin = wWindowFor(e->window);
	if (!wwin)
		return;
	wwin->flags.obscured = (e->state == XCB_VISIBILITY_FULLY_OBSCURED);
}

#ifdef USE_DOCK_XDND
static void handleSelectionNotify(xcb_generic_event_t *ev)
{
	wXDNDProcessSelection(ev);
}
#endif

static void handleSelectionRequest(xcb_generic_event_t *ev)
{
#ifdef USE_ICCCM_WMREPLACE
	const xcb_selection_request_event_t *e = (const xcb_selection_request_event_t *)ev;
	WScreen *scr;
	xcb_selection_notify_event_t notify;

	/*
	 * This event must be sent to the slection requester to not block him
	 *
	 * We create it with the answer 'there is no selection' by default
	 */
	memset(&notify, 0, sizeof(notify));
	notify.response_type = XCB_SELECTION_NOTIFY;
	notify.requestor = e->requestor;
	notify.selection = e->selection;
	notify.target = e->target;
	notify.property = XCB_NONE; /* This says that there is no selection */
	notify.time = e->time;

	scr = wScreenForWindow(e->owner);
	if (!scr)
		goto not_our_selection;

	if (e->owner != scr->info_window)
		goto not_our_selection;

	if (e->selection != scr->sn_atom)
		goto not_our_selection;

	if (e->target == wx_static_atoms[W_ATOM_VERSION]) {
		static const uint32_t icccm_version[] = { 2, 0 };

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

		WXSetFormat32N(wdisp->conn, e->requestor, notify.property,
			       XCB_ATOM_INTEGER, icccm_version,
			       wlengthof(icccm_version));
	}

 not_our_selection:
	if (notify.property == XCB_NONE) {
		char *selname = wXGetAtomName(wdisp->conn, e->selection);
		char *tgtname = wXGetAtomName(wdisp->conn, e->target);

		wwarning("received SelectionRequest(%s) for target=\"%s\" from requestor 0x%lX but we have no answer",
		         selname ? selname : "?", tgtname ? tgtname : "?", (unsigned long) e->requestor);
		free(selname);
		free(tgtname);
	}

	/* Send the answer to the requestor, the wire-shaped event built
	 * above goes out unpropagated with no event mask */
	WXSendEvent32(wdisp->conn, &notify, sizeof(notify), 0, e->requestor, 0);
#else
	(void) ev;
#endif
}

static void handleSelectionClear(xcb_generic_event_t *ev)
{
	const xcb_selection_clear_event_t *e = (const xcb_selection_clear_event_t *)ev;
#ifdef USE_ICCCM_WMREPLACE
	WScreen *scr = wScreenForWindow(e->owner);
#endif

	wXSettingsSelectionClear(e);

#ifdef USE_ICCCM_WMREPLACE
	if (!scr)
		return;

	if (e->selection != scr->sn_atom)
		return;

	wmessage(_("another window manager is replacing us!"));
	Shutdown(WSExitMode, true);
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
