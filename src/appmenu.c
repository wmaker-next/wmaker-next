/* appmenu.c- application defined menu
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
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
#include <X11/Xutil.h>
#include <X11/Xproto.h>
#include <X11/Xatom.h>
#include <WINGs/WXProp.h>
#include <WINGs/WXHints.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "WindowMaker.h"
#include "menu.h"
#include "actions.h"
#include "appmenu.h"
#include "framewin.h"


typedef struct {
	short code;
	short tag;
	Window window;
} WAppMenuData;

enum {
	wmBeginMenu = 1,
	wmEndMenu = 2,
	wmNormalItem = 10,
	wmDoubleItem = 11,
	wmSubmenuItem = 12
};

enum {
	wmSelectItem = 1
};

#define MAX_APPMENU_DEPTH 64
#define MAX_APPMENU_ENTRIES 1024

static void sendMessage(Window window, int what, int tag)
{
	WXSendClientMessage(dpy, window, NoEventMask, window,
			    w_global.atom.wmaker.menu,
			    w_global.timestamp.last_event, what, tag, 0, 0);
	XFlush(dpy);
}

static void notifyClient(WMenu * menu, WMenuEntry * entry)
{
	WAppMenuData *data = entry->clientdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) menu;

	sendMessage(data->window, wmSelectItem, data->tag);
}

static WMenu *parseMenuCommandWithDepth(WScreen *scr, Window win, char **slist,
					int count, int *index, int depth)
{
	WMenu *menu;
	int command;
	int code, pos;
	char title[300];
	char rtext[300];

	if (depth > MAX_APPMENU_DEPTH) {
		wwarning(_("appmenu: menu nesting too deep in window %lx"), win);
		return NULL;
	}

	if (sscanf(slist[*index], "%i %i %n", &command, &code, &pos) < 2 || command != wmBeginMenu) {
		wwarning(_("appmenu: bad menu entry \"%s\" in window %lx"), slist[*index], win);
		return NULL;
	}
	if (wstrlcpy(title, &slist[*index][pos], sizeof(title)) >= sizeof(title)) {
		wwarning(_("appmenu: menu command size exceeded in window %lx"), win);
		return NULL;
	}
	menu = wMenuCreateForApp(scr, title, *index == 1);
	if (!menu)
		return NULL;
	*index += 1;
	while (*index < count) {
		int ecode, etag, enab;

		if (sscanf(slist[*index], "%i", &command) != 1) {
			wMenuDestroy(menu, True);
			wwarning(_("appmenu: bad menu entry \"%s\" in window %lx"), slist[*index], win);
			return NULL;
		}

		if (command == wmEndMenu) {
			*index += 1;
			break;

		} else if (command == wmNormalItem || command == wmDoubleItem) {
			WAppMenuData *data;
			WMenuEntry *entry;

			if (command == wmNormalItem) {
				if (sscanf(slist[*index], "%i %i %i %i %n",
					   &command, &ecode, &etag, &enab, &pos) != 4 || ecode != code) {
					wMenuDestroy(menu, True);
					wwarning(_("appmenu: bad menu entry \"%s\" in window %lx"),
						 slist[*index], win);
					return NULL;
				}
				wstrlcpy(title, &slist[*index][pos], sizeof(title));
				rtext[0] = 0;
			} else {
				if (sscanf(slist[*index], "%i %i %i %i %299s %n",
					   &command, &ecode, &etag, &enab, rtext, &pos) != 5 || ecode != code) {
					wMenuDestroy(menu, True);
					wwarning(_("appmenu: bad menu entry \"%s\" in window %lx"),
						 slist[*index], win);
					return NULL;
				}
				wstrlcpy(title, &slist[*index][pos], sizeof(title));
			}
			data = malloc(sizeof(WAppMenuData));
			if (data == NULL) {
				wwarning(_("appmenu: out of memory creating menu for window %lx"), win);
				wMenuDestroy(menu, True);
				return NULL;
			}
			data->code = code;
			data->tag = etag;
			data->window = win;
			entry = wMenuAddCallback(menu, title, notifyClient, data);
			if (!entry) {
				wMenuDestroy(menu, True);
				wwarning(_("appmenu: out of memory creating menu for window %lx"), win);
				free(data);
				return NULL;
			}
			if (rtext[0] != 0)
				entry->rtext = wstrdup(rtext);
			else
				entry->rtext = NULL;
			entry->free_cdata = free;
			*index += 1;

		} else if (command == wmSubmenuItem) {
			int ncode;
			WMenuEntry *entry;
			WMenu *submenu;

			if (sscanf(slist[*index], "%i %i %i %i %i %n",
				   &command, &ecode, &etag, &enab, &ncode, &pos) != 5 || ecode != code) {
				wMenuDestroy(menu, True);
				wwarning(_("appmenu: bad menu entry \"%s\" in window %lx"), slist[*index], win);

				return NULL;
			}
			wstrlcpy(title, &slist[*index][pos], sizeof(title));
			*index += 1;

			submenu = parseMenuCommandWithDepth(scr, win, slist, count, index, depth + 1);
			if (!submenu) {
				wMenuDestroy(menu, True);
				return NULL;
			}

			entry = wMenuAddCallback(menu, title, NULL, NULL);

			if (!entry) {
				wMenuDestroy(menu, True);
				wMenuDestroy(submenu, True);
				wwarning(_("appmenu: out of memory creating menu for window %lx"), win);
				return NULL;
			}

			wMenuEntrySetCascade(menu, entry, submenu);

		} else {
			wMenuDestroy(menu, True);
			wwarning(_("appmenu: bad menu entry \"%s\" in window %lx"), slist[*index], win);
			return NULL;
		}
	}

	return menu;
}

static WMenu *parseMenuCommand(WScreen *scr, Window win, char **slist, int count, int *index)
{
	return parseMenuCommandWithDepth(scr, win, slist, count, index, 0);
}

WMenu *wAppMenuGet(WScreen * scr, Window window)
{
	int count, i;
	char **slist;
	WMenu *menu;

	slist = WXGetTextPropertyList(dpy, window, w_global.atom.wmaker.menu, &count);
	if (!slist)
		return NULL;
	if (count < 1) {
		WXFreeStringList(slist, count);
		return NULL;
	}
	if (strcmp(slist[0], "WMMenu 0") != 0) {
		wwarning(_("appmenu: unknown version of WMMenu in window %lx: %s"), window, slist[0]);
		WXFreeStringList(slist, count);
		return NULL;
	}
	if (count > MAX_APPMENU_ENTRIES) {
		wwarning(_("appmenu: too many menu entries (%d) in window %lx"), count, window);
		WXFreeStringList(slist, count);
		return NULL;
	}

	i = 1;
	menu = parseMenuCommand(scr, window, slist, count, &i);
	if (menu)
		menu->parent = NULL;

	WXFreeStringList(slist, count);

	return menu;
}

void wAppMenuDestroy(WMenu * menu)
{
	if (menu)
		wMenuDestroy(menu, True);
}

static void mapmenus(WMenu * menu)
{
	int i;

	if (menu->flags.mapped)
		XMapWindow(dpy, menu->frame->core->window);
	if (menu->brother->flags.mapped)
		XMapWindow(dpy, menu->brother->frame->core->window);
	for (i = 0; i < menu->cascade_no; i++) {
		if (menu->cascades[i])
			mapmenus(menu->cascades[i]);
	}
}

void wAppMenuMap(WMenu * menu, WWindow * wwin)
{

	if (!menu)
		return;

	if (!menu->flags.mapped) {
		wMenuMap(menu);
	}
	if (wwin && (wPreferences.focus_mode != WKF_CLICK)) {
		int x, min;

		min = 20;	/* Keep at least 20 pixels visible */
		if (wwin->frame_x > min) {
			x = wwin->frame_x - menu->frame->core->width;
		} else {
			x = min - menu->frame->core->width;
		}
		wMenuMove(menu, x, wwin->frame_y, True);
	}
	mapmenus(menu);

}

static void unmapmenus(WMenu * menu)
{
	int i;

	if (menu->flags.mapped)
		XUnmapWindow(dpy, menu->frame->core->window);
	if (menu->brother->flags.mapped)
		XUnmapWindow(dpy, menu->brother->frame->core->window);
	for (i = 0; i < menu->cascade_no; i++) {
		if (menu->cascades[i])
			unmapmenus(menu->cascades[i]);
	}
}

void wAppMenuUnmap(WMenu * menu)
{
	if (menu)
		unmapmenus(menu);
}
