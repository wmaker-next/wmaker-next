/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2014-2015 Window Maker Team
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
 *  with this program; if not, write to the Free Software Foundation
 */

/* Many part of code are ripped of an example from JX's site */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <WINGs/WXProp.h>

#include "WindowMaker.h"
#include "dock.h"
#include "xdnd.h"
#include "workspace.h"


static Atom _XA_XdndAware;
static Atom _XA_XdndEnter;
static Atom _XA_XdndLeave;
static Atom _XA_XdndDrop;
static Atom _XA_XdndPosition;
static Atom _XA_XdndStatus;
static Atom _XA_XdndActionCopy;
static Atom _XA_XdndSelection;
static Atom _XA_XdndFinished;
static Atom _XA_XdndTypeList;
static Atom _XA_WINDOWMAKER_XDNDEXCHANGE;
static Atom supported_typelist;
static Atom selected_typelist;
static Window xdnd_requestor = None;

void wXDNDInitializeAtoms(void)
{
	_XA_XdndAware = XInternAtom(dpy, "XdndAware", False);
	_XA_XdndEnter = XInternAtom(dpy, "XdndEnter", False);
	_XA_XdndLeave = XInternAtom(dpy, "XdndLeave", False);
	_XA_XdndDrop = XInternAtom(dpy, "XdndDrop", False);
	_XA_XdndPosition = XInternAtom(dpy, "XdndPosition", False);
	_XA_XdndStatus = XInternAtom(dpy, "XdndStatus", False);
	_XA_XdndActionCopy = XInternAtom(dpy, "XdndActionCopy", False);
	_XA_XdndSelection = XInternAtom(dpy, "XdndSelection", False);
	_XA_XdndFinished = XInternAtom(dpy, "XdndFinished", False);
	_XA_XdndTypeList = XInternAtom(dpy, "XdndTypeList", False);
	_XA_WINDOWMAKER_XDNDEXCHANGE = XInternAtom(dpy, "_WINDOWMAKER_XDNDEXCHANGE", False);

	supported_typelist = XInternAtom(dpy, "text/uri-list", False);
}

void wXDNDMakeAwareness(Window window)
{
	WXSetAtom(dpy, window, _XA_XdndAware, (Atom)XDND_VERSION);
}

static void wXDNDDecodeURI(char *uri)
{
	char *last = uri + strlen(uri);
	while (uri < last-2) {
		if (*uri == '%') {
			int h;
			if (sscanf(uri+1, "%2X", &h) != 1)
				break;
			*uri = h;
			memmove(uri+1, uri+3, last - (uri+2));
			last -= 2;
		}
		uri++;
	}
}

static size_t wXDNDQuotedLength(const char *string)
{
	size_t length = 3;

	while (*string) {
		if (*string == '"' || *string == '\\')
			length++;
		length++;
		string++;
	}

	return length;
}

static void wXDNDAppendQuoted(char **dest, const char *string)
{
	char *p = *dest;

	*p++ = ' ';
	*p++ = '"';
	while (*string) {
		if (*string == '"' || *string == '\\')
			*p++ = '\\';
		*p++ = *string++;
	}
	*p++ = '"';
	*p = '\0';
	*dest = p;
}

Bool wXDNDProcessSelection(XEvent *event)
{
	WScreen *scr = wScreenForWindow(event->xselection.requestor);
	char *retain;
	char *delme = NULL;
	Window selowner = XGetSelectionOwner(dpy, _XA_XdndSelection);

	/* only act on the conversion we requested in the XdndDrop handler,
	 * a forged SelectionNotify must match every field we passed to
	 * XConvertSelection() including the requestor window we own */
	if (xdnd_requestor != None &&
	    event->xselection.requestor == xdnd_requestor &&
	    event->xselection.selection == _XA_XdndSelection &&
	    event->xselection.target == selected_typelist &&
	    event->xselection.property == _XA_WINDOWMAKER_XDNDEXCHANGE) {
		delme = WXRcvString(dpy,
				    WXReqString8(dpy, xdnd_requestor,
						 _XA_WINDOWMAKER_XDNDEXCHANGE,
						 selected_typelist),
				    NULL);
		XDeleteProperty(dpy, xdnd_requestor, _XA_WINDOWMAKER_XDNDEXCHANGE);
	}
	xdnd_requestor = None;
	selected_typelist = None;

	/*send finished */
	WXSendClientMessage(dpy, selowner, 0, selowner, _XA_XdndFinished,
			    event->xselection.requestor, 0, 0, 0, 0);

	/*process dropping */
	if (delme) {
		WMArray *items;
		WMArrayIterator iter;
		int length;
		size_t total_size = 0;
		char *tmp;
		char *out;

		scr->xdestring = delme;
		items = WMCreateArray(4);
		retain = wstrdup(scr->xdestring);
		wfree(scr->xdestring);

		length = strlen(retain);

		/* search in string */
		while (length--) {
			if (retain[length] == '\r') {	/* useless char, nuke it */
				retain[length] = 0;
			}
			if (retain[length] == '\n') {
				if (retain[length + 1] != '\0') {
					tmp = wstrdup(&retain[length + 1]);
					if (!strncmp(tmp, "file://", 7)) {
						wXDNDDecodeURI(&tmp[7]);
						total_size += wXDNDQuotedLength(&tmp[7]);
					}
					WMAddToArray(items, tmp);
					/* this is nonsense -- if (length)
					   WMAppendArray(items, WMCreateArray(1)); */
				}
				retain[length] = 0;
			}
		}
		/* final one */
		tmp = wstrdup(retain);
		if (!strncmp(tmp, "file://", 7)) {
			wXDNDDecodeURI(&tmp[7]);
			total_size += wXDNDQuotedLength(&tmp[7]);
		}
		WMAddToArray(items, tmp);
		wfree(retain);

		/* now pack new string */
		scr->xdestring = wmalloc(total_size + 1);
		scr->xdestring[0] = 0;	/* empty string */
		out = scr->xdestring;
		WM_ETARETI_ARRAY(items, tmp, iter) {
			/* only supporting file: URI objects */
			if (!strncmp(tmp, "file://", 7)) {
				wXDNDAppendQuoted(&out, &tmp[7]);
			}
			wfree(tmp);
		}
		WMFreeArray(items);
		if (scr->xdestring[0])
			wDockReceiveDNDDrop(scr, event);
		wfree(scr->xdestring);	/* this xdestring is not from Xlib (no XFree) */
	}

	return True;
}

static Bool isAwareXDND(Window window)
{
	Atom version;

	if (!window)
		return False;
	return WXGetAtom(dpy, window, _XA_XdndAware, &version);
}

static Bool acceptXDND(Window window)
{
	WScreen *scr = wScreenForWindow(window);
	WDock *dock;
	int icon_pos, i;

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
		return False;

	if (isAwareXDND(dock->icon_array[icon_pos]->icon->icon_win))
		return False;

	if (dock->icon_array[icon_pos]->dnd_command != NULL)
		return True;

	return False;
}

static void wXDNDGetTypeList(Display *dpy, Window window)
{
	Atom *typelist;
	size_t i, count;

	typelist = WXGetAtomList(dpy, window, _XA_XdndTypeList, 1024, &count);
	if (!typelist) {
		wwarning(_("wXDNDGetTypeList failed = %ld"), _XA_XdndTypeList);
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

Bool wXDNDProcessClientMessage(XClientMessageEvent *event)
{
	/* every XDND client message carries 32-bit data fields */
	if (event->format != 32)
		return False;

	if (event->message_type == _XA_XdndEnter) {
		int i;

		/* reset so a previous drag's negotiated type does not leak
		 * into XdndPosition/XdndDrop for this one */
		selected_typelist = None;

		if (XDND_ENTER_THREE_TYPES(event)) {
			for (i = 0; i < 3; i++) {
				if (XDND_ENTER_TYPE(event, i) == supported_typelist) {
					selected_typelist = supported_typelist;
					break;
				}
			}
		} else {
			wXDNDGetTypeList(dpy, XDND_ENTER_SOURCE_WIN(event));
		}
		return True;
	} else if (event->message_type == _XA_XdndLeave) {
		selected_typelist = None;
		xdnd_requestor = None;
		return True;
	} else if (event->message_type == _XA_XdndDrop) {
		if (selected_typelist == supported_typelist &&
		    XDND_DROP_SOURCE_WIN(event) == XGetSelectionOwner(dpy, _XA_XdndSelection)) {
			xdnd_requestor = event->window;
			XConvertSelection(dpy, _XA_XdndSelection, selected_typelist,
					  _XA_WINDOWMAKER_XDNDEXCHANGE, xdnd_requestor, CurrentTime);
		} else {
			selected_typelist = None;
		}
		return True;
	} else if (event->message_type == _XA_XdndPosition) {
		Window srcwin = XDND_POSITION_SOURCE_WIN(event);
		if (selected_typelist == supported_typelist) {
			long flags = (acceptXDND(event->window) ? 0x1L : 0L) | 0x2L;
			WXSendClientMessage(dpy, srcwin, 0, srcwin, _XA_XdndStatus,
					    event->window, flags, 0, 0, _XA_XdndActionCopy);
		}
		return True;
	}
	return False;
}
