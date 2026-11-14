/*
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

/* Many part of code are ripped of an example from JX's site */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>


#include <WINGs/WXProp.h>

#include "WindowMaker.h"
#include "dock.h"
#include "xdnd.h"
#include "xdnduri.h"
#include "workspace.h"


static xcb_atom_t _XA_XdndAware;
static xcb_atom_t _XA_XdndEnter;
static xcb_atom_t _XA_XdndLeave;
static xcb_atom_t _XA_XdndDrop;
static xcb_atom_t _XA_XdndPosition;
static xcb_atom_t _XA_XdndStatus;
static xcb_atom_t _XA_XdndActionCopy;
static xcb_atom_t _XA_XdndSelection;
static xcb_atom_t _XA_XdndFinished;
static xcb_atom_t _XA_XdndTypeList;
static xcb_atom_t _XA_WINDOWMAKER_XDNDEXCHANGE;
static xcb_atom_t supported_typelist;

static xcb_atom_t selected_typelist;
static xcb_window_t xdnd_requestor = XCB_NONE;

void wXDNDInitializeAtoms(void)
{
	_XA_XdndAware = wx_static_atoms[W_ATOM_XDND_AWARE];
	_XA_XdndEnter = wx_static_atoms[W_ATOM_XDND_ENTER];
	_XA_XdndLeave = wx_static_atoms[W_ATOM_XDND_LEAVE];
	_XA_XdndDrop = wx_static_atoms[W_ATOM_XDND_DROP];
	_XA_XdndPosition = wx_static_atoms[W_ATOM_XDND_POSITION];
	_XA_XdndStatus = wx_static_atoms[W_ATOM_XDND_STATUS];
	_XA_XdndActionCopy = wx_static_atoms[W_ATOM_XDND_ACTION_COPY];
	_XA_XdndSelection = wx_static_atoms[W_ATOM_XDND_SELECTION];
	_XA_XdndFinished = wx_static_atoms[W_ATOM_XDND_FINISHED];
	_XA_XdndTypeList = wx_static_atoms[W_ATOM_XDND_TYPE_LIST];
	_XA_WINDOWMAKER_XDNDEXCHANGE = wx_static_atoms[W_ATOM_WINDOWMAKER_XDNDEXCHANGE];
	supported_typelist = wx_static_atoms[W_ATOM_TEXT_URI_LIST];
}

void wXDNDMakeAwareness(xcb_window_t window)
{
	WXSetAtom(wdisp->conn, window, _XA_XdndAware, (xcb_atom_t)XDND_VERSION);
}

bool wXDNDProcessSelection(xcb_generic_event_t *event)
{
	WScreen *scr = wScreenForWindow(((xcb_selection_notify_event_t *)event)->requestor);
	char *delme = NULL;
	xcb_window_t selowner = wXGetSelectionOwner(wdisp->conn, _XA_XdndSelection);

	if (!scr)
		return false;

	/* only act on the conversion we requested in the XdndDrop handler,
	 * a forged SelectionNotify must match every field we passed to
	 * wXConvertSelection() including the requestor window we own */
	if (xdnd_requestor != XCB_NONE &&
	    ((xcb_selection_notify_event_t *)event)->requestor == xdnd_requestor &&
	    ((xcb_selection_notify_event_t *)event)->selection == _XA_XdndSelection &&
	    ((xcb_selection_notify_event_t *)event)->target == selected_typelist &&
	    ((xcb_selection_notify_event_t *)event)->property == _XA_WINDOWMAKER_XDNDEXCHANGE) {
		delme = WXRcvString(wdisp->conn,
				    WXReqString8(wdisp->conn, xdnd_requestor,
						 _XA_WINDOWMAKER_XDNDEXCHANGE,
						 selected_typelist),
				    NULL);
		wXDeleteProperty(wdisp->conn, xdnd_requestor, _XA_WINDOWMAKER_XDNDEXCHANGE);
	}
	xdnd_requestor = XCB_NONE;
	selected_typelist = XCB_NONE;

	/*send finished */
	WXSendClientMessage(wdisp->conn, selowner, 0, selowner, _XA_XdndFinished,
			    ((xcb_selection_notify_event_t *)event)->requestor, 0, 0, 0, 0);

	/*process dropping */
	if (delme) {
		WMArray *items;
		size_t quoted;
		int file_count = 0;
		char *payload;
		char *single_path = NULL;

		items = WMCreateArray(4);
		payload = wstrdup(delme);
		wfree(delme);

		quoted = wXDNDParseURIList(payload, items);
		wfree(payload);

		/* now pack new string */
		scr->xdestring = wmalloc(quoted + 1);
		scr->xdestring[0] = 0;	/* empty string */
		wXDNDJoinQuotedList(items, scr->xdestring, &file_count, &single_path);
		WMFreeArray(items);
		/* a lone file:// path is offered to the main dock/clip tile
		 * as a candidate launcher, multi-file drops keep going through
		 * the per-tile DropCommand only */
		scr->xdndpath = single_path;
		if (scr->xdestring[0])
			wDockReceiveDNDDrop(scr, event);
		wfree(scr->xdestring);	/* this xdestring is not from Xlib (no XFree) */
		scr->xdestring = NULL;
		if (scr->xdndpath) {
			wfree(scr->xdndpath);
			scr->xdndpath = NULL;
		}
	}

	return true;
}

static bool isAwareXDND(xcb_window_t window)
{
	xcb_atom_t version;

	if (!window)
		return false;
	return WXGetAtom(wdisp->conn, window, _XA_XdndAware, &version);
}

static bool acceptXDND(xcb_window_t window)
{
	WScreen *scr = wScreenForWindow(window);
	WDock *dock;
	int icon_pos, i;

	if (!scr)
		return false;

	icon_pos = -1;
	dock = scr->dock;
	if (dock) {
		for (i = 0; i < dock->max_icons; i++) {
			if (dock->icon_array[i]
			    && dock->icon_array[i]->icon->core->window == window) {
				icon_pos = i;
				break;
			}
		}
	}
	if (icon_pos < 0) {
		dock = scr->workspaces[scr->current_workspace]->clip;
		if (dock) {
			for (i = 0; i < dock->max_icons; i++) {
				if (dock->icon_array[i]
				    && dock->icon_array[i]->icon->core->window == window) {
					icon_pos = i;
					break;
				}
			}
		}
	}
	if (icon_pos < 0)
		return false;

	/* the main dock/clip tile accepts a single dropped path so a
	 * launcher can be created in the next free slot */
	if (icon_pos == 0)
		return dock->icon_count < dock->max_icons;

	if (isAwareXDND(dock->icon_array[icon_pos]->icon->icon_win))
		return false;

	if (dock->icon_array[icon_pos]->dnd_command != NULL)
		return true;

	return false;
}

static void wXDNDGetTypeList(xcb_window_t window)
{
	xcb_atom_t *typelist;
	size_t i, count;

	typelist = WXGetAtomList(wdisp->conn, window, _XA_XdndTypeList, 1024, &count);
	if (!typelist) {
		wwarning(_("wXDNDGetTypeList failed = %u"), _XA_XdndTypeList);
		return;
	}

	for (i = 0; i < count; i++) {
		if (typelist[i] == supported_typelist) {
			selected_typelist = typelist[i];
			break;
		}
	}
	wfree(typelist);
}

bool wXDNDProcessClientMessage(const xcb_client_message_event_t *event)
{
	/* every XDND client message carries 32-bit data fields */
	if (event->format != 32)
		return false;

	if (event->type == _XA_XdndEnter) {
		int i;

		/* reset so a previous drag's negotiated type does not leak
		 * into XdndPosition/XdndDrop for this one */
		selected_typelist = XCB_NONE;

		if (XDND_ENTER_THREE_TYPES(event)) {
			for (i = 0; i < 3; i++) {
				if (XDND_ENTER_TYPE(event, i) == supported_typelist) {
					selected_typelist = supported_typelist;
					break;
				}
			}
		} else {
			wXDNDGetTypeList(XDND_ENTER_SOURCE_WIN(event));
		}
		return true;
	} else if (event->type == _XA_XdndLeave) {
		selected_typelist = XCB_NONE;
		xdnd_requestor = XCB_NONE;
		return true;
	} else if (event->type == _XA_XdndDrop) {
		if (selected_typelist == supported_typelist &&
		    XDND_DROP_SOURCE_WIN(event) == wXGetSelectionOwner(wdisp->conn, _XA_XdndSelection)) {
			xdnd_requestor = event->window;
			wXConvertSelection(wdisp->conn, _XA_XdndSelection, selected_typelist,
					  _XA_WINDOWMAKER_XDNDEXCHANGE, xdnd_requestor, XCB_CURRENT_TIME);
		} else {
			selected_typelist = XCB_NONE;
		}
		return true;
	} else if (event->type == _XA_XdndPosition) {
		xcb_window_t srcwin = XDND_POSITION_SOURCE_WIN(event);
		if (selected_typelist == supported_typelist) {
			long flags = (acceptXDND(event->window) ? 0x1L : 0L) | 0x2L;
			WXSendClientMessage(wdisp->conn, srcwin, 0, srcwin, _XA_XdndStatus,
					    event->window, flags, 0, 0, _XA_XdndActionCopy);
		}
		return true;
	}
	return false;
}
