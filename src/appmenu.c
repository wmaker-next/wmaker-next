/* appmenu.c- application defined menu
 *
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

#include "wconfig.h"

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
#include "appmenuparse.h"
#include "framewin.h"


typedef struct {
	short code;
	short tag;
	xcb_window_t window;
} WAppMenuData;

#define MAX_APPMENU_ENTRIES 1024

static void sendMessage(xcb_window_t window, int what, int tag)
{
	WXSendClientMessage(wdisp->conn, window, XCB_EVENT_MASK_NO_EVENT, window,
			    wdisp->atom.wmaker.menu,
			    wdisp->timestamp.last_event, what, tag, 0, 0);
	wXFlush(wdisp->conn);
}

static void notifyClient(WMenu * menu, WMenuEntry * entry)
{
	WAppMenuData *data = entry->clientdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) menu;

	sendMessage(data->window, WAM_SELECT_ITEM, data->tag);
}

/* Build the WMenu for one parsed node. The node tree is already
 * validated, the only failure left is menu-side allocation. */
static WMenu *buildAppMenu(WScreen *scr, xcb_window_t win,
			   const WAppMenuNode *node, bool is_main)
{
	const WAppMenuNode *child;
	WMenu *menu;

	menu = wMenuCreateForApp(scr, node->title, is_main);
	if (!menu)
		return NULL;

	for (child = node->first_child; child; child = child->next) {
		if (child->kind == WAM_KIND_ITEM || child->kind == WAM_KIND_DOUBLE_ITEM) {
			WAppMenuData *data;
			WMenuEntry *entry;

			data = malloc(sizeof(WAppMenuData));
			if (data == NULL) {
				wMenuDestroy(menu, true);
				wwarning(_("appmenu: out of memory creating menu for window %lx"),
					 (unsigned long)win);
				return NULL;
			}
			data->code = node->code;
			data->tag = child->tag;
			data->window = win;
			entry = wMenuAddCallback(menu, child->title, notifyClient, data);
			if (!entry) {
				wMenuDestroy(menu, true);
				wwarning(_("appmenu: out of memory creating menu for window %lx"),
					 (unsigned long)win);
				free(data);
				return NULL;
			}
			entry->rtext = child->rtext ? wstrdup(child->rtext) : NULL;
			entry->free_cdata = free;

		} else if (child->kind == WAM_KIND_SUBMENU) {
			WMenu *submenu;
			WMenuEntry *entry;

			submenu = buildAppMenu(scr, win, child->first_child, false);
			if (!submenu) {
				wMenuDestroy(menu, true);
				return NULL;
			}

			entry = wMenuAddCallback(menu, child->title, NULL, NULL);
			if (!entry) {
				wMenuDestroy(menu, true);
				wMenuDestroy(submenu, true);
				wwarning(_("appmenu: out of memory creating menu for window %lx"),
					 (unsigned long)win);
				return NULL;
			}

			wMenuEntrySetCascade(menu, entry, submenu);
		}
	}

	return menu;
}

WMenu *wAppMenuGet(WScreen * scr, xcb_window_t window)
{
	int count;
	char **slist;
	const char *bad;
	WAppMenuNode *root;
	WMenu *menu;

	slist = WXGetTextPropertyList(wdisp->conn, window, wdisp->atom.wmaker.menu, &count);
	if (!slist)
		return NULL;
	if (count < 1) {
		WXFreeStringList(slist, count);
		return NULL;
	}
	if (strcmp(slist[0], "WMMenu 0") != 0) {
		wwarning(_("appmenu: unknown version of WMMenu in window %lx: %s"), (unsigned long)window, slist[0]);
		WXFreeStringList(slist, count);
		return NULL;
	}
	if (count < 2) {
		/* a header line alone is not a menu, the old reader
		 * walked one past the list here */
		wwarning(_("appmenu: bad menu entry \"%s\" in window %lx"),
			 slist[0], (unsigned long)window);
		WXFreeStringList(slist, count);
		return NULL;
	}
	if (count > MAX_APPMENU_ENTRIES) {
		wwarning(_("appmenu: too many menu entries (%d) in window %lx"), count, (unsigned long)window);
		WXFreeStringList(slist, count);
		return NULL;
	}

	bad = NULL;
	root = NULL;
	switch (wAppMenuParse((const char *const *)slist + 1, count - 1, &bad, &root)) {
	case WAPPMENU_OK:
		menu = buildAppMenu(scr, window, root, true);
		wAppMenuFreeNode(root);
		break;
	case WAPPMENU_BAD_ENTRY:
		if (bad)
			wwarning(_("appmenu: bad menu entry \"%s\" in window %lx"),
				 bad, (unsigned long)window);
		else
			wwarning(_("appmenu: bad menu entry in window %lx"), (unsigned long)window);
		menu = NULL;
		break;
	case WAPPMENU_TITLE_TOO_LONG:
		wwarning(_("appmenu: menu command size exceeded in window %lx"), (unsigned long)window);
		menu = NULL;
		break;
	case WAPPMENU_TOO_DEEP:
		wwarning(_("appmenu: menu nesting too deep in window %lx"), (unsigned long)window);
		menu = NULL;
		break;
	default:
		wwarning(_("appmenu: unparsable menu in window %lx"), (unsigned long)window);
		menu = NULL;
		break;
	}

	WXFreeStringList(slist, count);

	if (menu)
		menu->parent = NULL;

	return menu;
}

void wAppMenuDestroy(WMenu * menu)
{
	if (menu)
		wMenuDestroy(menu, true);
}

static void mapmenus(WMenu * menu)
{
	int i;

	if (menu->flags.mapped)
		xcb_map_window(wdisp->conn, menu->frame->core->window);
	if (menu->brother->flags.mapped)
		xcb_map_window(wdisp->conn, menu->brother->frame->core->window);
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
	if (wwin && (w_global.config->focus_mode != WKF_CLICK)) {
		int x, min;

		min = 20;	/* Keep at least 20 pixels visible */
		if (wwin->frame_x > min) {
			x = wwin->frame_x - menu->frame->core->width;
		} else {
			x = min - menu->frame->core->width;
		}
		wMenuMove(menu, x, wwin->frame_y, true);
	}
	mapmenus(menu);

}

static void unmapmenus(WMenu * menu)
{
	int i;

	if (menu->flags.mapped)
		xcb_unmap_window(wdisp->conn, menu->frame->core->window);
	if (menu->brother->flags.mapped)
		xcb_unmap_window(wdisp->conn, menu->brother->frame->core->window);
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
