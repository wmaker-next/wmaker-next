/* event.c - WindowMaker event handler
 *
 * WMlib - WindowMaker application programming interface
 *
 * Copyright (C) 1997-2003 Alfredo K. Kojima
 * Copyright (c) 2009-2026 Window Maker Team
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, see <https://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <xcb/xcb.h>

#include "WMaker.h"
#include "app.h"
#include "menu.h"

enum {
	wmSelectItem = 1
};

static wmMenuEntry *findEntry(WMMenu *menu, int tag)
{
	wmMenuEntry *entry = menu->first;
	wmMenuEntry *tmp;

	while (entry) {
		if (entry->tag == tag)
			return entry;
		if (entry->cascade) {
			tmp = findEntry(entry->cascade, tag);
			if (tmp)
				return tmp;
		}
		entry = entry->next;
	}
	return NULL;
}

static void wmHandleMenuEvents(WMAppContext *app, xcb_client_message_event_t *event)
{
	wmMenuEntry *entry;

	switch (event->data.data32[1]) {
	case wmSelectItem:
		entry = findEntry(app->main_menu, (int)event->data.data32[2]);
		if (entry && entry->callback) {
			(*entry->callback) (entry->clientData, (int)event->data.data32[2],
					    (xcb_timestamp_t) event->data.data32[0]);
		}
		break;
	}
}

int WMProcessEvent(WMAppContext *app, xcb_generic_event_t *event)
{
	int proc = 0;
	xcb_client_message_event_t *ev;

	if ((event->response_type & 0x7f) != XCB_CLIENT_MESSAGE)
		return 0;

	ev = (xcb_client_message_event_t *)event;
	if (ev->format == 32
	    && ev->type == app->menu_atom
	    && ev->window == app->main_window) {
		if (!app->main_menu)
			return 0;
		wmHandleMenuEvents(app, ev);
		proc = 1;
	}
	return proc;
}
