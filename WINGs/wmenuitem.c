/*
 *  wmenuitem.c
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 1999-2004 Alfredo K. Kojima
 *  Copyright (c) 2000-2004 Dan Pascu
 *  Copyright (c) 2009-2026 Window Maker Team
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

#include "WINGsP.h"

typedef struct W_MenuItem {
	char *title;

	WMPixmap *image;

	char *shortcutKey;
	int shortcutModifierMask;

	WMAction *action;
	void *data;

	void *object;

	WMPixmap *onStateImage;
	WMPixmap *offStateImage;
	WMPixmap *mixedStateImage;

	struct {
		unsigned enabled:1;
		unsigned state:2;
	} flags;
} MenuItem;

WMMenuItem *WMGetSeparatorMenuItem(void)
{
	return NULL;
}

bool WMMenuItemIsSeparator(WMMenuItem * item)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) item;

	return false;
}

WMMenuItem *WMCreateMenuItem(void)
{
	WMMenuItem *item;

	item = wmalloc(sizeof(MenuItem));

	item->flags.enabled = 1;

	return item;
}

void WMDestroyMenuItem(WMMenuItem * item)
{
	if (item->title)
		wfree(item->title);

	if (item->image)
		WMReleasePixmap(item->image);

	if (item->shortcutKey)
		wfree(item->shortcutKey);

	if (item->onStateImage)
		WMReleasePixmap(item->onStateImage);

	if (item->offStateImage)
		WMReleasePixmap(item->offStateImage);

	if (item->mixedStateImage)
		WMReleasePixmap(item->mixedStateImage);

	wfree(item);
}

bool WMGetMenuItemEnabled(WMMenuItem * item)
{
	return item->flags.enabled;
}

void WMSetMenuItemEnabled(WMMenuItem * item, bool flag)
{
	item->flags.enabled = ((flag == 0) ? 0 : 1);
}

char *WMGetMenuItemShortcut(WMMenuItem * item)
{
	return item->shortcutKey;
}

unsigned WMGetMenuItemShortcutModifierMask(WMMenuItem * item)
{
	return item->shortcutModifierMask;
}

void WMSetMenuItemShortcut(WMMenuItem * item, const char *shortcut)
{
	char *newShortcut;

	newShortcut = wstrdup(shortcut);

	if (item->shortcutKey)
		wfree(item->shortcutKey);
	item->shortcutKey = newShortcut;
}

void WMSetMenuItemShortcutModifierMask(WMMenuItem * item, unsigned mask)
{
	item->shortcutModifierMask = mask;
}

void *WMGetMenuItemRepresentedObject(WMMenuItem * item)
{
	return item->object;
}

void WMSetMenuItemRepresentedObject(WMMenuItem * item, void *object)
{
	item->object = object;
}

void WMSetMenuItemAction(WMMenuItem * item, WMAction * action, void *data)
{
	item->action = action;
	item->data = data;
}

WMAction *WMGetMenuItemAction(WMMenuItem * item)
{
	return item->action;
}

void *WMGetMenuItemData(WMMenuItem * item)
{
	return item->data;
}

void WMSetMenuItemTitle(WMMenuItem * item, const char *title)
{
	char *newTitle;

	if (title)
		newTitle = wstrdup(title);
	else
		newTitle = NULL;

	if (item->title)
		wfree(item->title);
	item->title = newTitle;
}

char *WMGetMenuItemTitle(WMMenuItem * item)
{
	return item->title;
}

void WMSetMenuItemState(WMMenuItem * item, int state)
{
	item->flags.state = state;
}

int WMGetMenuItemState(WMMenuItem * item)
{
	return item->flags.state;
}

void WMSetMenuItemPixmap(WMMenuItem * item, WMPixmap * pixmap)
{
	if (item->image)
		WMReleasePixmap(item->image);

	item->image = WMRetainPixmap(pixmap);
}

WMPixmap *WMGetMenuItemPixmap(WMMenuItem * item)
{
	return item->image;
}

void WMSetMenuItemOnStatePixmap(WMMenuItem * item, WMPixmap * pixmap)
{
	if (item->onStateImage)
		WMReleasePixmap(item->onStateImage);

	item->onStateImage = WMRetainPixmap(pixmap);
}

WMPixmap *WMGetMenuItemOnStatePixmap(WMMenuItem * item)
{
	return item->onStateImage;
}

void WMSetMenuItemOffStatePixmap(WMMenuItem * item, WMPixmap * pixmap)
{
	if (item->offStateImage)
		WMReleasePixmap(item->offStateImage);

	item->offStateImage = WMRetainPixmap(pixmap);
}

WMPixmap *WMGetMenuItemOffStatePixmap(WMMenuItem * item)
{
	return item->offStateImage;
}

void WMSetMenuItemMixedStatePixmap(WMMenuItem * item, WMPixmap * pixmap)
{
	if (item->mixedStateImage)
		WMReleasePixmap(item->mixedStateImage);

	item->mixedStateImage = WMRetainPixmap(pixmap);
}

WMPixmap *WMGetMenuItemMixedStatePixmap(WMMenuItem * item)
{
	return item->mixedStateImage;
}
