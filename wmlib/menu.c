/* menu.c - menu interface functions
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

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <xcb/xcb.h>

#include "WMaker.h"
#include "app.h"
#include "menu.h"

WMMenu *WMMenuCreate(WMAppContext *app, char *title)
{
	wmMenu *menu;

	if (strlen(title) > 255)
		return NULL;

	menu = malloc(sizeof(wmMenu));
	if (!menu)
		return NULL;

	menu->appcontext = app;
	menu->parent = NULL;
	menu->title = title;
	menu->entries = NULL;
	menu->first = NULL;

	menu->realized = 0;
	menu->code = app->last_menu_tag++;

	menu->entryline = malloc(strlen(title) + 32);
	menu->entryline2 = malloc(32);
	if (!menu->entryline || !menu->entryline2) {
		if (menu->entryline)
			free(menu->entryline);
		free(menu);
		return NULL;
	}
	snprintf(menu->entryline, strlen(title) + 32, "%i %i %s", wmBeginMenu, menu->code, title);
	snprintf(menu->entryline2, 32, "%i %i", wmEndMenu, menu->code);
	return menu;
}

int
WMMenuAddItem(WMMenu *menu, char *text, WMMenuAction action,
	      void *clientData, WMFreeFunction freedata, char *rtext)
{
	wmMenuEntry *entry;
	size_t entryline_size;
	const unsigned char *p;

	/* right side text is a single wire token */
	if (rtext) {
		if (rtext[0] == '\0' || strlen(rtext) > 4)
			return -1;
		for (p = (const unsigned char *)rtext; *p; p++)
			if (isspace(*p))
				return -1;
	}

	/* max size of menu text */
	if (strlen(text) > 255)
		return -1;

	entry = malloc(sizeof(wmMenuEntry));
	if (!entry)
		return -1;

	entryline_size = strlen(text) + (rtext ? strlen(rtext) : 0) + 100;
	entry->entryline = malloc(entryline_size);
	if (!entry->entryline) {
		free(entry);
		return -1;
	}

	if (menu->entries)
		entry->order = menu->entries->order + 1;
	else {
		entry->order = 0;
		menu->first = entry;
	}
	entry->next = NULL;
	entry->prev = menu->entries;
	if (menu->entries)
		menu->entries->next = entry;
	menu->entries = entry;

	entry->menu = menu;
	entry->text = text;
	entry->shortcut = rtext;
	entry->callback = action;
	entry->clientData = clientData;
	entry->free = freedata;
	entry->tag = menu->appcontext->last_menu_tag++;
	entry->cascade = NULL;
	entry->enabled = 1;

	if (!rtext)
		snprintf(entry->entryline, entryline_size, "%i %i %i %i %s", wmNormalItem, menu->code, entry->tag, 1, text);
	else
		snprintf(entry->entryline, entryline_size, "%i %i %i %i %s %s", wmDoubleItem,
			menu->code, entry->tag, 1, rtext, text);
	return entry->tag;
}

int WMMenuAddSubmenu(WMMenu *menu, char *text, WMMenu *submenu)
{
	wmMenuEntry *entry;
	size_t entryline_size;

	if (menu == submenu)
		return -1;

	/* max size of menu text */
	if (strlen(text) > 255)
		return -1;

	entry = malloc(sizeof(wmMenuEntry));
	if (!entry)
		return -1;

	entryline_size = strlen(text) + 100;
	entry->entryline = malloc(entryline_size);
	if (!entry->entryline) {
		free(entry);
		return -1;
	}

	if (menu->entries)
		entry->order = menu->entries->order + 1;
	else {
		entry->order = 0;
		menu->first = entry;
	}
	entry->next = NULL;
	entry->prev = menu->entries;
	if (menu->entries)
		menu->entries->next = entry;
	menu->entries = entry;
	entry->menu = menu;
	entry->text = text;
	entry->shortcut = NULL;
	entry->callback = NULL;
	entry->clientData = NULL;
	entry->free = NULL;
	entry->tag = menu->appcontext->last_menu_tag++;
	entry->cascade = submenu;
	entry->enabled = 1;

	snprintf(entry->entryline, entryline_size, "%i %i %i %i %i %s", wmSubmenuItem,
		menu->code, entry->tag, 1, submenu->code, text);
	return entry->tag;
}

static int countItems(WMMenu *menu, WMMenu **path, int depth)
{
	wmMenuEntry *entry = menu->first;
	int c, i, sub;

	if (depth > WMMENU_MAX_DEPTH)
		return -1;
	for (i = 0; i < depth; i++)
		if (path[i] == menu)
			return -1;
	path[depth] = menu;

	c = 1;
	while (entry) {
		c++;
		if (entry->cascade) {
			sub = countItems(entry->cascade, path, depth + 1);
			if (sub < 0)
				return -1;
			c += sub;
		}
		entry = entry->next;
	}
	c++;
	return c;
}

static int addItems(char **slist, int *index, WMMenu *menu,
		    WMMenu **path, int depth)
{
	wmMenuEntry *entry = menu->first;
	int i;

	if (depth > WMMENU_MAX_DEPTH)
		return -1;
	for (i = 0; i < depth; i++)
		if (path[i] == menu)
			return -1;
	path[depth] = menu;

	slist[(*index)++] = menu->entryline;
	while (entry) {
		slist[(*index)++] = entry->entryline;
		if (entry->cascade && addItems(slist, index, entry->cascade, path, depth + 1) < 0)
			return -1;
		entry = entry->next;
	}
	slist[(*index)++] = menu->entryline2;
	return 0;
}

int WMRealizeMenus(WMAppContext *app)
{
	int i, count;
	size_t total, n;
	char **slist;
	unsigned char *data, *p;
	WMMenu *path[WMMENU_MAX_DEPTH + 1];

	if (!app->main_menu)
		return 0;

	/* first count how many menu items there are */
	count = countItems(app->main_menu, path, 0);
	if (count < 0)
		return 0;
	if (count == 0)
		return 1;

	count++;
	slist = malloc(count * sizeof(char *));
	if (!slist)
		return 0;

	slist[0] = "WMMenu 0";
	i = 1;
	if (addItems(slist, &i, app->main_menu, path, 0) < 0) {
		free(slist);
		return 0;
	}

	/* the property is a text-property string list: every entry is
	 * NUL-terminated, matching XStringListToTextProperty() */
	total = 0;
	for (count = 0; count < i; count++)
		total += strlen(slist[count]) + 1;

	data = malloc(total);
	if (!data) {
		free(slist);
		return 0;
	}
	p = data;
	for (count = 0; count < i; count++) {
		n = strlen(slist[count]);
		memcpy(p, slist[count], n);
		p += n;
		*p++ = '\0';
	}
	free(slist);

	xcb_change_property(app->dpy, XCB_PROP_MODE_REPLACE, app->main_window,
			    app->menu_atom, XCB_ATOM_STRING, 8, total, data);
	free(data);

	return 1;
}
