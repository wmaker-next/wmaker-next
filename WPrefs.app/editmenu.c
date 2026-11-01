/* editmenu.c - editable menus
 *
 *  WPrefs - Window Maker Preferences Program
 *
 *  Copyright (c) 2000-2003 Alfredo K. Kojima
 *  Copyright (c) 2008-2026 Window Maker Team
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

#include <cairo.h>
#include <WINGs/WINGsP.h>
#include <WINGs/WUtil.h>
#include <WINGs/WXCore.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <ctype.h>

#include "editmenu.h"

typedef struct W_EditMenuItem {
	W_Class widgetClass;
	WMView *view;

	struct W_EditMenu *parent;

	char *label;
	WMPixmap *pixmap;	/* pixmap to show at left */

	void *data;
	WMCallback *destroyData;

	struct W_EditMenu *submenu;	/* if it's a cascade, NULL otherwise */

	struct {
		unsigned isTitle:1;
		unsigned isHighlighted:1;
	} flags;
} EditMenuItem;

typedef struct W_EditMenu {
	W_Class widgetClass;
	WMView *view;

	struct W_EditMenu *parent;
	EditMenuItem *parentItem;	/* cascade owner, NULL for top-level menus */

	WMArray *items;		/* EditMenuItem */

	EditMenuItem *selectedItem;

	WMTextField *tfield;

	WMButton *closeB;

	int titleHeight;
	int itemHeight;

	WEditMenuDelegate *delegate;

	WMTextFieldDelegate *tdelegate;

	/* item dragging */
	int draggedItem;
	int dragX, dragY;

	/* only for non-standalone menu */
	WMSize maxSize;
	WMSize minSize;

	struct {
		unsigned standalone:1;
		unsigned isTitled:1;

		unsigned acceptsDrop:1;
		unsigned isFactory:1;
		unsigned isSelectable:1;
		unsigned isEditable:1;

		unsigned isTornOff:1;

		unsigned isDragging:1;
		unsigned isEditing:1;

		unsigned wasMapped:1;
	} flags;
} EditMenu;

/******************** WEditMenuItem ********************/

static void destroyEditMenuItem(WEditMenuItem * iPtr);
static void paintEditMenuItem(WEditMenuItem * iPtr);
static void handleItemEvents(xcb_generic_event_t * event, void *data);

static void handleItemClick(xcb_generic_event_t * event, void *data);

static W_Class EditMenuItemClass = 0;

W_Class InitEditMenuItem(void)
{
	/* register our widget with WINGs and get our widget class ID */
	if (!EditMenuItemClass) {
		EditMenuItemClass = W_RegisterUserWidget();
	}

	return EditMenuItemClass;
}

WEditMenuItem *WCreateEditMenuItem(WMWidget * parent, const char *title, bool isTitle)
{
	WEditMenuItem *iPtr;
	WMScreen *scr = WMWidgetScreen(parent);

	if (!EditMenuItemClass)
		InitEditMenuItem();

	iPtr = wmalloc(sizeof(WEditMenuItem));

	iPtr->widgetClass = EditMenuItemClass;

	iPtr->view = W_CreateView(W_VIEW(parent));
	if (!iPtr->view) {
		wfree(iPtr);
		return NULL;
	}
	iPtr->view->self = iPtr;

	iPtr->parent = parent;

	iPtr->label = wstrdup(title);

	iPtr->flags.isTitle = isTitle;

	if (isTitle) {
		WMSetWidgetBackgroundColor(iPtr, WMBlackColor(scr));
	}

	WMCreateEventHandler(iPtr->view, XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY, handleItemEvents, iPtr);

	WMCreateEventHandler(iPtr->view, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE
			     | XCB_EVENT_MASK_BUTTON_MOTION, handleItemClick, iPtr);

	return iPtr;
}

char *WGetEditMenuItemTitle(WEditMenuItem * item)
{
	return item->label;
}

void *WGetEditMenuItemData(WEditMenuItem * item)
{
	return item->data;
}

void WSetEditMenuItemData(WEditMenuItem * item, void *data, WMCallback * destroyer)
{
	item->data = data;
	item->destroyData = destroyer;
}

void WSetEditMenuItemImage(WEditMenuItem * item, WMPixmap * pixmap)
{
	if (item->pixmap)
		WMReleasePixmap(item->pixmap);
	item->pixmap = WMRetainPixmap(pixmap);
}

static void paintEditMenuItem(WEditMenuItem * iPtr)
{
	WMScreen *scr = WMWidgetScreen(iPtr);
	WMColor *color;
	xcb_window_t win = W_VIEW(iPtr)->window;
	int w = W_VIEW(iPtr)->size.width;
	int h = W_VIEW(iPtr)->size.height;
	WMFont *font = (iPtr->flags.isTitle ? scr->boldFont : scr->normalFont);
	cairo_t *cr;

	if (!iPtr->view->flags.realized)
		return;

	color = scr->black;
	if (iPtr->flags.isTitle && !iPtr->flags.isHighlighted) {
		color = scr->white;
	}

	wXClearArea(scr->display, win, 0, 0, 0, 0, false);

	cr = WCairoBeginForDrawable(scr, win);
	if (cr) {
		W_DrawRelief(cr, scr, 0, 0, w + 1, h, WRRaised);
		WCairoEnd(cr);
	}

	WMDrawString(scr, win, color, font, 5, 3, iPtr->label, strlen(iPtr->label));

	if (iPtr->pixmap) {
		WMSize size = WMGetPixmapSize(iPtr->pixmap);

		WMDrawPixmap(iPtr->pixmap, win, w - size.width - 5, (h - size.height) / 2);
	} else if (iPtr->submenu) {
		/* draw the cascade indicator: three 1px bevel edges of a
		 * right-pointing triangle, dark on top, white below, black
		 * on the left. +0.5 keeps the 1px stroke on pixel centres. */
		cairo_t *cr = WCairoBeginForDrawable(scr, win);

		cairo_set_line_width(cr, 1.0);
		W_SetSourceColor(cr, scr->darkGray);
		cairo_move_to(cr, w - 11 + 0.5, 6 + 0.5);
		cairo_line_to(cr, w - 6 + 0.5, h / 2 - 1 + 0.5);
		cairo_stroke(cr);
		W_SetSourceColor(cr, scr->white);
		cairo_move_to(cr, w - 11 + 0.5, h - 8 + 0.5);
		cairo_line_to(cr, w - 6 + 0.5, h / 2 - 1 + 0.5);
		cairo_stroke(cr);
		W_SetSourceColor(cr, scr->black);
		cairo_move_to(cr, w - 12 + 0.5, 6 + 0.5);
		cairo_line_to(cr, w - 12 + 0.5, h - 8 + 0.5);
		cairo_stroke(cr);
		WCairoEnd(cr);
	}
}

static void highlightItem(WEditMenuItem * iPtr, bool high)
{
	if (iPtr->flags.isTitle)
		return;

	iPtr->flags.isHighlighted = high;

	if (high) {
		WMSetWidgetBackgroundColor(iPtr, WMWhiteColor(WMWidgetScreen(iPtr)));
	} else {
		if (!iPtr->flags.isTitle) {
			WMSetWidgetBackgroundColor(iPtr, WMGrayColor(WMWidgetScreen(iPtr)));
		} else {
			WMSetWidgetBackgroundColor(iPtr, WMBlackColor(WMWidgetScreen(iPtr)));
		}
	}
}

static int getItemTextWidth(WEditMenuItem * iPtr)
{
	WMScreen *scr = WMWidgetScreen(iPtr);

	if (iPtr->flags.isTitle) {
		return WMWidthOfString(scr->boldFont, iPtr->label, strlen(iPtr->label));
	} else {
		return WMWidthOfString(scr->normalFont, iPtr->label, strlen(iPtr->label));
	}
}

static void handleItemEvents(xcb_generic_event_t * event, void *data)
{
	WEditMenuItem *iPtr = (WEditMenuItem *) data;

	switch ((event->response_type & 0x7f)) {
	case XCB_EXPOSE:
		if (((xcb_expose_event_t *)event)->count != 0)
			break;
		paintEditMenuItem(iPtr);
		break;

	case XCB_DESTROY_NOTIFY:
		destroyEditMenuItem(iPtr);
		break;
	}
}

static void destroyEditMenuItem(WEditMenuItem * iPtr)
{
	if (iPtr->label)
		wfree(iPtr->label);
	if (iPtr->data && iPtr->destroyData)
		(*iPtr->destroyData) (iPtr->data);
	if (iPtr->submenu)
		WMDestroyWidget(iPtr->submenu);

	wfree(iPtr);
}

/******************** WEditMenu *******************/

static void destroyEditMenu(WEditMenu * mPtr);

static void updateMenuContents(WEditMenu * mPtr);

static void handleEvents(xcb_generic_event_t * event, void *data);

static void editItemLabel(WEditMenuItem * item);

static void stopEditItem(WEditMenu * menu, bool apply);

static void deselectItem(WEditMenu * menu);

static W_Class EditMenuClass = 0;

W_Class InitEditMenu(void)
{
	/* register our widget with WINGs and get our widget class ID */
	if (!EditMenuClass) {

		EditMenuClass = W_RegisterUserWidget();
	}

	return EditMenuClass;
}

typedef struct {
	int flags;
	int window_style;
	int window_level;
	int reserved;
	xcb_pixmap_t miniaturize_pixmap;	/* pixmap for miniaturize button */
	xcb_pixmap_t close_pixmap;	/* pixmap for close button */
	xcb_pixmap_t miniaturize_mask;	/* miniaturize pixmap mask */
	xcb_pixmap_t close_mask;	/* close pixmap mask */
	int extra_flags;
} GNUstepWMAttributes;

#define GSWindowStyleAttr       (1<<0)
#define GSWindowLevelAttr       (1<<1)

static void writeGNUstepWMAttr(WMScreen * scr, xcb_window_t window, GNUstepWMAttributes * attr)
{
	uint32_t data[9];

	/* copy fields explicitly, the struct layout may carry padding */
	data[0] = attr->flags;
	data[1] = attr->window_style;
	data[2] = attr->window_level;
	data[3] = 0;		/* reserved */
	/* The X protocol says XIDs are 32bit */
	data[4] = attr->miniaturize_pixmap;
	data[5] = attr->close_pixmap;
	data[6] = attr->miniaturize_mask;
	data[7] = attr->close_mask;
	data[8] = attr->extra_flags;
	WXSetFormat32N(scr->display, window, scr->attribsAtom, scr->attribsAtom,
		       data, 9);
}

static void realizeObserver(void *self, WMNotification * not)
{
	WEditMenu *menu = (WEditMenu *) self;
	GNUstepWMAttributes attribs;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) not;

	memset(&attribs, 0, sizeof(GNUstepWMAttributes));
	attribs.flags = GSWindowStyleAttr | GSWindowLevelAttr;
	attribs.window_style = WMBorderlessWindowMask;
	attribs.window_level = WMSubmenuWindowLevel;

	writeGNUstepWMAttr(WMWidgetScreen(menu), menu->view->window, &attribs);
}

static void itemSelectObserver(void *self, WMNotification * notif)
{
	WEditMenu *menu = (WEditMenu *) self;
	WEditMenu *rmenu;

	rmenu = (WEditMenu *) WMGetNotificationObject(notif);
	/* check whether rmenu is from the same hierarchy of menu? */

	if (rmenu == menu) {
		return;
	}

	if (menu->selectedItem) {
		if (!menu->selectedItem->submenu)
			deselectItem(menu);
		if (menu->flags.isEditing)
			stopEditItem(menu, false);
	}
}

static WEditMenu *makeEditMenu(WMScreen * scr, WMWidget * parent, const char *title)
{
	WEditMenu *mPtr;
	WEditMenuItem *item;

	if (!EditMenuClass)
		InitEditMenu();

	mPtr = wmalloc(sizeof(WEditMenu));

	mPtr->widgetClass = EditMenuClass;

	if (parent) {
		mPtr->view = W_CreateView(W_VIEW(parent));
		mPtr->flags.standalone = 0;
	} else {
		mPtr->view = W_CreateTopView(scr);
		mPtr->flags.standalone = 1;
	}
	if (!mPtr->view) {
		wfree(mPtr);
		return NULL;
	}
	mPtr->view->self = mPtr;

	mPtr->flags.isSelectable = 1;
	mPtr->flags.isEditable = 1;

	W_SetViewBackgroundColor(mPtr->view, scr->darkGray);

	WMAddNotificationObserver(realizeObserver, mPtr, WMViewRealizedNotification, mPtr->view);

	WMAddNotificationObserver(itemSelectObserver, mPtr, "EditMenuItemSelected", NULL);

	mPtr->items = WMCreateArray(4);

	WMCreateEventHandler(mPtr->view, XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY, handleEvents, mPtr);

	if (title != NULL) {
		item = WCreateEditMenuItem(mPtr, title, true);

		WMMapWidget(item);
		WMAddToArray(mPtr->items, item);

		mPtr->flags.isTitled = 1;
	}

	mPtr->itemHeight = WMFontHeight(scr->normalFont) + 6;
	mPtr->titleHeight = WMFontHeight(scr->boldFont) + 8;

	updateMenuContents(mPtr);

	return mPtr;
}

WEditMenu *WCreateEditMenu(WMScreen * scr, const char *title)
{
	return makeEditMenu(scr, NULL, title);
}

WEditMenu *WCreateEditMenuPad(WMWidget * parent)
{
	WMScreen *scr = WMWidgetScreen(parent);
	WEditMenu *pad;

	pad = makeEditMenu(scr, parent, NULL);

	/* factory pads use tighter rows so the item palette fits the panel */
	pad->itemHeight = WMFontHeight(scr->normalFont) + 4;
	updateMenuContents(pad);

	return pad;
}

void WSetEditMenuDelegate(WEditMenu * mPtr, WEditMenuDelegate * delegate)
{
	mPtr->delegate = delegate;
}

WEditMenuItem *WInsertMenuItemWithTitle(WEditMenu * mPtr, int index, const char *title)
{
	WEditMenuItem *item;

	item = WCreateEditMenuItem(mPtr, title, false);

	WMMapWidget(item);

	if (index >= WMGetArrayItemCount(mPtr->items)) {
		WMAddToArray(mPtr->items, item);
	} else {
		if (index < 0)
			index = 0;
		if (mPtr->flags.isTitled)
			index++;
		WMInsertInArray(mPtr->items, index, item);
	}

	updateMenuContents(mPtr);

	return item;
}

WEditMenuItem *WGetEditMenuItem(WEditMenu * mPtr, int index)
{
	if (index >= WMGetArrayItemCount(mPtr->items))
		return NULL;

	return WMGetFromArray(mPtr->items, index + (mPtr->flags.isTitled ? 1 : 0));
}

WEditMenuItem *WAddMenuItemWithTitle(WEditMenu * mPtr, const char *title)
{
	return WInsertMenuItemWithTitle(mPtr, WMGetArrayItemCount(mPtr->items), title);
}

void WSetEditMenuTitle(WEditMenu * mPtr, const char *title)
{
	WEditMenuItem *item;

	item = WMGetFromArray(mPtr->items, 0);

	wfree(item->label);
	item->label = wstrdup(title);

	updateMenuContents(mPtr);

	WMRedisplayWidget(item);
}

char *WGetEditMenuTitle(WEditMenu * mPtr)
{
	WEditMenuItem *item;

	item = WMGetFromArray(mPtr->items, 0);

	return item->label;
}

void WSetEditMenuAcceptsDrop(WEditMenu * mPtr, bool flag)
{
	mPtr->flags.acceptsDrop = flag;
}

void WSetEditMenuSubmenu(WEditMenu * mPtr, WEditMenuItem * item, WEditMenu * submenu)
{
	item->submenu = submenu;
	submenu->parent = mPtr;
	submenu->parentItem = item;

	paintEditMenuItem(item);
}

WEditMenu *WGetEditMenuSubmenu(WEditMenuItem *item)
{
	return item->submenu;
}

void WRemoveEditMenuItem(WEditMenu * mPtr, WEditMenuItem * item)
{
	if (WMRemoveFromArray(mPtr->items, item) != 0) {
		updateMenuContents(mPtr);
	}
}

void WSetEditMenuSelectable(WEditMenu * mPtr, bool flag)
{
	mPtr->flags.isSelectable = flag;
}

void WSetEditMenuEditable(WEditMenu * mPtr, bool flag)
{
	mPtr->flags.isEditable = flag;
}

void WSetEditMenuIsFactory(WEditMenu * mPtr, bool flag)
{
	mPtr->flags.isFactory = flag;
}

void WSetEditMenuMinSize(WEditMenu * mPtr, WMSize size)
{
	mPtr->minSize.width = size.width;
	mPtr->minSize.height = size.height;
}

void WSetEditMenuMaxSize(WEditMenu * mPtr, WMSize size)
{
	mPtr->maxSize.width = size.width;
	mPtr->maxSize.height = size.height;
}

WMPoint WGetEditMenuLocationForSubmenu(WEditMenu * mPtr, WEditMenu * submenu)
{
	WMArrayIterator iter;
	WEditMenuItem *item;
	int y;

	if (mPtr->flags.isTitled)
		y = -mPtr->titleHeight;
	else
		y = 0;
	WM_ITERATE_ARRAY(mPtr->items, item, iter) {
		if (item->submenu == submenu) {
			WMPoint pt = WMGetViewScreenPosition(mPtr->view);

			return wmkpoint(pt.x + mPtr->view->size.width, pt.y + y);
		}
		y += W_VIEW_HEIGHT(item->view);
	}

	puts("invalid submenu passed to WGetEditMenuLocationForSubmenu()");

	return wmkpoint(0, 0);
}

static void closeMenuAction(WMWidget * w, void *data)
{
	WEditMenu *menu = (WEditMenu *) data;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) w;

	WMAddIdleHandler(WMDestroyWidget, menu->closeB);
	menu->closeB = NULL;

	WEditMenuHide(menu);
}

void WTearOffEditMenu(WEditMenu * menu, WEditMenu * submenu)
{
	WEditMenuItem *item;

	submenu->flags.isTornOff = 1;

	item = (WEditMenuItem *) WMGetFromArray(submenu->items, 0);

	submenu->closeB = WMCreateCommandButton(item);
	WMResizeWidget(submenu->closeB, 15, 15);
	WMMoveWidget(submenu->closeB, W_VIEW(submenu)->size.width - 20, 3);
	WMRealizeWidget(submenu->closeB);
	WMSetButtonText(submenu->closeB, "X");
	WMSetButtonAction(submenu->closeB, closeMenuAction, submenu);
	WMMapWidget(submenu->closeB);

	if (menu->selectedItem && menu->selectedItem->submenu == submenu)
		deselectItem(menu);
}

bool WEditMenuIsTornOff(WEditMenu * mPtr)
{
	return mPtr->flags.isTornOff;
}

void WEditMenuHide(WEditMenu * mPtr)
{
	WEditMenuItem *item;
	int i = 0;

	if (WMWidgetIsMapped(mPtr)) {
		WMUnmapWidget(mPtr);
		mPtr->flags.wasMapped = 1;
	} else {
		mPtr->flags.wasMapped = 0;
	}
	while ((item = WGetEditMenuItem(mPtr, i++))) {
		WEditMenu *submenu;

		submenu = WGetEditMenuSubmenu(item);
		if (submenu) {
			WEditMenuHide(submenu);
		}
	}
}

void WEditMenuUnhide(WEditMenu * mPtr)
{
	WEditMenuItem *item;
	int i = 0;

	if (mPtr->flags.wasMapped) {
		WMMapWidget(mPtr);
	}
	while ((item = WGetEditMenuItem(mPtr, i++))) {
		WEditMenu *submenu;

		submenu = WGetEditMenuSubmenu(item);
		if (submenu) {
			WEditMenuUnhide(submenu);
		}
	}
}

void WEditMenuShowAt(WEditMenu * menu, int x, int y)
{
	WXSizeHints *hints;

	hints = WXAllocSizeHints();

	hints->flags = XCB_ICCCM_SIZE_HINT_US_POSITION;
	hints->x = x;
	hints->y = y;

	WMMoveWidget(menu, x, y);
	WXSetNormalHints(W_VIEW_DISPLAY(menu->view), W_VIEW_DRAWABLE(menu->view), hints);
	wfree(hints);

	WMMapWidget(menu);
}

static void updateMenuContents(WEditMenu * mPtr)
{
	int newW, newH;
	int w;
	int i;
	int iheight = mPtr->itemHeight;
	int offs = 1;
	WMArrayIterator iter;
	WEditMenuItem *item;

	newW = 0;
	newH = offs;

	i = 0;
	WM_ITERATE_ARRAY(mPtr->items, item, iter) {
		w = getItemTextWidth(item);

		newW = WMAX(w, newW);

		WMMoveWidget(item, offs, newH);
		if (i == 0 && mPtr->flags.isTitled) {
			newH += mPtr->titleHeight;
		} else {
			newH += iheight;
		}
		i = 1;
	}

	newW += iheight + 10;
	newH--;

	if (mPtr->minSize.width)
		newW = WMAX(newW, mPtr->minSize.width);
	if (mPtr->maxSize.width)
		newW = WMIN(newW, mPtr->maxSize.width);

	if (mPtr->minSize.height)
		newH = WMAX(newH, mPtr->minSize.height);
	if (mPtr->maxSize.height)
		newH = WMIN(newH, mPtr->maxSize.height);

	if (W_VIEW(mPtr)->size.width == newW && mPtr->view->size.height == newH + 1)
		return;

	W_ResizeView(mPtr->view, newW, newH + 1);

	if (mPtr->closeB)
		WMMoveWidget(mPtr->closeB, newW - 20, 3);

	newW -= 2 * offs;

	i = 0;
	WM_ITERATE_ARRAY(mPtr->items, item, iter) {
		if (i == 0 && mPtr->flags.isTitled) {
			WMResizeWidget(item, newW, mPtr->titleHeight);
		} else {
			WMResizeWidget(item, newW, iheight);
		}
		i = 1;
	}
}

static void deselectItem(WEditMenu * menu)
{
	WEditMenu *submenu;
	WEditMenuItem *item = menu->selectedItem;

	highlightItem(item, false);

	if (menu->delegate && menu->delegate->itemDeselected) {
		(*menu->delegate->itemDeselected) (menu->delegate, menu, item);
	}
	submenu = item->submenu;

	if (submenu && !WEditMenuIsTornOff(submenu)) {
		WEditMenuHide(submenu);
	}

	menu->selectedItem = NULL;
}

static void selectItem(WEditMenu * menu, WEditMenuItem * item)
{
	if (!menu->flags.isSelectable || menu->selectedItem == item) {
		return;
	}
	if (menu->selectedItem) {
		deselectItem(menu);
	}

	if (menu->flags.isEditing) {
		stopEditItem(menu, false);
	}

	if (item && !item->flags.isTitle) {
		highlightItem(item, true);

		if (item->submenu && !W_VIEW_MAPPED(item->submenu->view)) {
			WMPoint pt;

			pt = WGetEditMenuLocationForSubmenu(menu, item->submenu);

			WEditMenuShowAt(item->submenu, pt.x, pt.y);

			item->submenu->flags.isTornOff = 0;
		}

		WMPostNotificationName("EditMenuItemSelected", menu, NULL);

		if (menu->delegate && menu->delegate->itemSelected) {
			(*menu->delegate->itemSelected) (menu->delegate, menu, item);
		}
	}

	menu->selectedItem = item;
}

static void paintMenu(WEditMenu * mPtr)
{
	W_View *view = mPtr->view;
	WMScreen *scr = W_VIEW_SCREEN(view);
	cairo_t *cr;

	cr = WCairoBeginForDrawable(scr, W_VIEW_DRAWABLE(view));
	if (!cr)
		return;
	W_DrawRelief(cr, scr, 0, 0, W_VIEW_WIDTH(view), W_VIEW_HEIGHT(view), WRSimple);
	WCairoEnd(cr);
}

static void handleEvents(xcb_generic_event_t * event, void *data)
{
	WEditMenu *mPtr = (WEditMenu *) data;

	switch ((event->response_type & 0x7f)) {
	case XCB_DESTROY_NOTIFY:
		destroyEditMenu(mPtr);
		break;

	case XCB_EXPOSE:
		if (((xcb_expose_event_t *)event)->count == 0)
			paintMenu(mPtr);
		break;
	}
}

/* -------------------------- Menu Label Editing ------------------------ */

static void stopEditItem(WEditMenu * menu, bool apply)
{
	if (apply) {
		wfree(menu->selectedItem->label);
		menu->selectedItem->label = WMGetTextFieldText(menu->tfield);

		updateMenuContents(menu);

		if (menu->delegate && menu->delegate->itemEdited) {
			(*menu->delegate->itemEdited) (menu->delegate, menu, menu->selectedItem);
		}

	}
	WMUnmapWidget(menu->tfield);
	menu->flags.isEditing = 0;
}

static void textEndedEditing(struct WMTextFieldDelegate *self, WMNotification * notif)
{
	WEditMenu *menu = (WEditMenu *) self->data;
	uintptr_t reason;
	int i;
	WEditMenuItem *item;

	if (!menu->flags.isEditing)
		return;

	reason = (uintptr_t)WMGetNotificationClientData(notif);

	switch (reason) {
	case WMEscapeTextMovement:
		stopEditItem(menu, false);
		break;

	case WMReturnTextMovement:
		stopEditItem(menu, true);
		break;

	case WMTabTextMovement:
		stopEditItem(menu, true);

		i = WMGetFirstInArray(menu->items, menu->selectedItem);
		item = WMGetFromArray(menu->items, i + 1);
		if (item != NULL) {
			selectItem(menu, item);
			editItemLabel(item);
		}
		break;

	case WMBacktabTextMovement:
		stopEditItem(menu, true);

		i = WMGetFirstInArray(menu->items, menu->selectedItem);
		item = WMGetFromArray(menu->items, i - 1);
		if (item != NULL) {
			selectItem(menu, item);
			editItemLabel(item);
		}
		break;
	}
}

static WMTextFieldDelegate textFieldDelegate = {
	NULL,
	NULL,
	NULL,
	textEndedEditing,
	NULL,
	NULL
};

static void editItemLabel(WEditMenuItem * item)
{
	WEditMenu *mPtr = item->parent;
	WMTextField *tf;

	if (!mPtr->flags.isEditable) {
		return;
	}

	if (!mPtr->tfield) {
		mPtr->tfield = WMCreateTextField(mPtr);
		WMSetTextFieldBeveled(mPtr->tfield, false);
		WMRealizeWidget(mPtr->tfield);

		mPtr->tdelegate = wmalloc(sizeof(WMTextFieldDelegate));
		memcpy(mPtr->tdelegate, &textFieldDelegate, sizeof(WMTextFieldDelegate));

		mPtr->tdelegate->data = mPtr;

		WMSetTextFieldDelegate(mPtr->tfield, mPtr->tdelegate);
	}
	tf = mPtr->tfield;

	WMSetTextFieldText(tf, item->label);
	WMSelectTextFieldRange(tf, wmkrange(0, strlen(item->label)));
	WMResizeWidget(tf, mPtr->view->size.width, mPtr->itemHeight);
	WMMoveWidget(tf, 0, item->view->pos.y);
	WMMapWidget(tf);
	WMSetFocusToWidget(tf);

	mPtr->flags.isEditing = 1;
}

/* -------------------------------------------------- */

static void slideWindow(xcb_connection_t *dpy, xcb_window_t win, int srcX, int srcY, int dstX, int dstY)
{
	double x, y, dx, dy;
	int i;
	int iterations;

	iterations = WMIN(25, WMAX(abs(dstX - srcX), abs(dstY - srcY)));

	x = srcX;
	y = srcY;

	dx = (double)(dstX - srcX) / iterations;
	dy = (double)(dstY - srcY) / iterations;

	for (i = 0; i <= iterations; i++) {
		xcb_configure_window(dpy, win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
				     (const uint32_t[]){ (uint32_t) x, (uint32_t) y });
		wXFlush(dpy);

		wusleep(800);

		x += dx;
		y += dy;
	}
}

static WEditMenu *findMenuInWindow(xcb_connection_t *dpy, xcb_window_t toplevel, int x, int y)
{
	xcb_window_t foo, bar;
	xcb_window_t *children;
	unsigned nchildren;
	int i;
	WEditMenu *menu;
	WMView *view;

	view = W_GetViewForXWindow(toplevel);
	if (view && view->self && WMWidgetClass(view->self) == EditMenuClass) {
		menu = (WEditMenu *) view->self;
		if (menu->flags.acceptsDrop) {
			return menu;
		}
	}

	if (!wXQueryTree(dpy, toplevel, &foo, &bar, &children, &nchildren) || children == NULL) {
		return NULL;
	}

	/* first window that contains the point is the one; bad-window
	 * errors on the dying-tree walk are ignored by the WXProp
	 * readers, no handler bracket needed */
	for (i = nchildren - 1; i >= 0; i--) {
		WXWindowAttributes attr;

		if (wXGetWindowAttributes(dpy, children[i], &attr, NULL)
		    && attr.map_state == XCB_MAP_STATE_VIEWABLE
		    && x >= attr.x && y >= attr.y && x < attr.x + attr.width && y < attr.y + attr.height) {
			xcb_window_t tmp;

			tmp = children[i];

			menu = findMenuInWindow(dpy, tmp, x - attr.x, y - attr.y);
			if (menu) {
				wfree(children);
				return menu;
			}
		}
	}

	wfree(children);
	return NULL;
}

static void handleDragOver(WEditMenu *menu, WMView *view, WEditMenuItem *item, int y)
{
	WMScreen *scr = W_VIEW_SCREEN(menu->view);
	xcb_window_t blaw;
	int mx, my;
	int offs;

	wXTranslateCoordinates(scr->display, W_VIEW_DRAWABLE(menu->view), scr->rootWin, 0, 0, &mx, &my, &blaw);

	offs = menu->flags.standalone ? 0 : 1;

	W_MoveView(view, mx + offs, y);
	if (view->size.width != menu->view->size.width) {
		W_ResizeView(view, menu->view->size.width - 2 * offs, menu->itemHeight);
		W_ResizeView(item->view, menu->view->size.width - 2 * offs, menu->itemHeight);
	}
}

static void handleItemDrop(WEditMenu *menu, WEditMenuItem *item, int y)
{
	WMScreen *scr = W_VIEW_SCREEN(menu->view);
	xcb_window_t blaw;
	int mx, my;
	int index;

	wXTranslateCoordinates(scr->display, W_VIEW_DRAWABLE(menu->view), scr->rootWin, 0, 0, &mx, &my, &blaw);

	index = y - my;
	if (menu->flags.isTitled) {
		index -= menu->titleHeight;
	}
	index = (index + menu->itemHeight / 2) / menu->itemHeight;
	if (index < 0)
		index = 0;

	if (menu->flags.isTitled) {
		index++;
	}

	if (index > WMGetArrayItemCount(menu->items)) {
		WMAddToArray(menu->items, item);
	} else {
		WMInsertInArray(menu->items, index, item);
	}

	W_ReparentView(item->view, menu->view, 0, index * menu->itemHeight);

	item->parent = menu;
	if (item->submenu) {
		item->submenu->parent = menu;
	}

	updateMenuContents(menu);
}

static void dragMenu(WEditMenu * menu)
{
	WMScreen *scr = W_VIEW_SCREEN(menu->view);
	xcb_generic_event_t *ev = NULL;
	bool done = false;
	int dx, dy;
	unsigned blau;
	xcb_window_t blaw;

	wXGetGeometry(scr->display, W_VIEW_DRAWABLE(menu->view), &blaw, &dx, &dy,
		      &blau, &blau, &blau, &blau);

	wXTranslateCoordinates(scr->display, W_VIEW_DRAWABLE(menu->view), scr->rootWin, dx, dy, &dx, &dy, &blaw);

	dx = menu->dragX - dx;
	dy = menu->dragY - dy;

	wXGrabPointer(scr->display, scr->rootWin, false,
		     XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION,
		     XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, scr->defaultCursor, XCB_CURRENT_TIME);

	if (menu->parent)
		WTearOffEditMenu(menu->parent, menu);

	while (!done) {
		free(ev);
		ev = NULL;
		WMNextEvent(scr->display, &ev);

		switch ((ev->response_type & 0x7f)) {
		case XCB_BUTTON_RELEASE:
			done = true;
			break;

		case XCB_MOTION_NOTIFY: {
			/* keep only the newest queued motion */
			xcb_generic_event_t *nev;

			while (WXEvqTakeMask(scr->display, XCB_EVENT_MASK_BUTTON_MOTION, XCB_NONE, &nev)) {
				free(ev);
				ev = nev;
			}

			WMMoveWidget(menu, (((xcb_motion_notify_event_t *)ev)->root_x) - dx, (((xcb_motion_notify_event_t *)ev)->root_y) - dy);
			break;
		}

		default:
			WMHandleEvent(ev);
			break;
		}

		free(ev);
		ev = NULL;
	}

	wXUngrabPointer(scr->display, XCB_CURRENT_TIME);
}

static WEditMenuItem *duplicateItem(WEditMenuItem * item)
{
	WEditMenuItem *nitem;

	nitem = WCreateEditMenuItem(item->parent, item->label, false);
	if (item->pixmap)
		nitem->pixmap = WMRetainPixmap(item->pixmap);

	return nitem;
}

static WEditMenu *duplicateMenu(WEditMenu * menu)
{
	WEditMenu *nmenu;
	WEditMenuItem *item;
	WMArrayIterator iter;
	bool first = menu->flags.isTitled;

	nmenu = WCreateEditMenu(WMWidgetScreen(menu), WGetEditMenuTitle(menu));

	memcpy(&nmenu->flags, &menu->flags, sizeof(menu->flags));
	nmenu->delegate = menu->delegate;

	WM_ITERATE_ARRAY(menu->items, item, iter) {
		WEditMenuItem *nitem;

		if (first) {
			first = false;
			continue;
		}

		nitem = WAddMenuItemWithTitle(nmenu, item->label);
		if (item->pixmap)
			WSetEditMenuItemImage(nitem, item->pixmap);

		if (menu->delegate && menu->delegate->itemCloned) {
			(*menu->delegate->itemCloned) (menu->delegate, menu, item, nitem);
		}
	}

	WMRealizeWidget(nmenu);

	return nmenu;
}

static void dragItem(WEditMenu * menu, WEditMenuItem * item, bool copy)
{
	static WXColor black = { 0, 0, 0, 0 };
	static WXColor green = { 0x0045b045, 0x4500, 0xb000, 0x4500 };
	static WXColor back = { 0, 0xffff, 0xffff, 0xffff };
	xcb_connection_t *dpy = W_VIEW_DISPLAY(menu->view);
	WMScreen *scr = W_VIEW_SCREEN(menu->view);
	int x = 0, y = 0;
	int dx, dy;
	bool done = false;
	xcb_window_t blaw;
	int blai;
	unsigned blau;
	xcb_window_t win;
	WMView *dview;
	int orix, oriy;
	bool enteredMenu = false;
	WMSize oldSize = item->view->size;
	WEditMenuItem *dritem = item;
	WEditMenu *dmenu = NULL;

	if (item->flags.isTitle) {
		WMRaiseWidget(menu);

		dragMenu(menu);

		return;
	}

	selectItem(menu, NULL);

	win = scr->rootWin;

	wXTranslateCoordinates(dpy, W_VIEW_DRAWABLE(item->view), win, 0, 0, &orix, &oriy, &blaw);

	dview = W_CreateUnmanagedTopView(scr);
	W_SetViewBackgroundColor(dview, scr->black);
	W_ResizeView(dview, W_VIEW_WIDTH(item->view), W_VIEW_HEIGHT(item->view));
	W_MoveView(dview, orix, oriy);
	W_RealizeView(dview);

	if (menu->flags.isFactory || copy) {
		dritem = duplicateItem(item);

		W_ReparentView(dritem->view, dview, 0, 0);
		WMResizeWidget(dritem, oldSize.width, oldSize.height);
		WMRealizeWidget(dritem);
		WMMapWidget(dritem);
	} else {
		W_ReparentView(item->view, dview, 0, 0);
	}

	W_MapView(dview);

	dx = menu->dragX - orix;
	dy = menu->dragY - oriy;

	wXGrabPointer(dpy, scr->rootWin, false,
		     XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION,
		     XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, scr->defaultCursor, XCB_CURRENT_TIME);

	if (menu->flags.acceptsDrop)
		wXRecolorCursor(dpy, scr->defaultCursor, &green, &back);

	while (!done) {
		xcb_generic_event_t *ev = NULL;

		WMNextEvent(dpy, &ev);

		switch ((ev->response_type & 0x7f)) {
		case XCB_MOTION_NOTIFY: {
			xcb_generic_event_t *nev;

			while (WXEvqTakeMask(dpy, XCB_EVENT_MASK_BUTTON_MOTION, XCB_NONE, &nev)) {
				free(ev);
				ev = nev;
			}

			wXQueryPointer(dpy, win, &blaw, &blaw, &blai, &blai, &x, &y, &blau);

			dmenu = findMenuInWindow(dpy, win, x, y);

			if (dmenu) {
				handleDragOver(dmenu, dview, dritem, y - dy);
				if (!enteredMenu) {
					enteredMenu = true;
					wXRecolorCursor(dpy, scr->defaultCursor, &green, &back);
				}
			} else {
				if (enteredMenu) {
					W_ResizeView(dview, oldSize.width, oldSize.height);
					W_ResizeView(dritem->view, oldSize.width, oldSize.height);
					enteredMenu = false;
					wXRecolorCursor(dpy, scr->defaultCursor, &black, &back);
				}
				W_MoveView(dview, x - dx, y - dy);
			}

			break;
		}

		case XCB_BUTTON_RELEASE:
			done = true;
			break;

		default:
			WMHandleEvent(ev);
			break;
		}

		free(ev);
		ev = NULL;
	}
	wXRecolorCursor(dpy, scr->defaultCursor, &black, &back);

	wXUngrabPointer(dpy, XCB_CURRENT_TIME);

	if (!enteredMenu) {
		bool rem = true;

		if (!menu->flags.isFactory && !copy) {
			W_UnmapView(dview);
			if (menu->delegate && menu->delegate->shouldRemoveItem) {
				rem = (*menu->delegate->shouldRemoveItem) (menu->delegate, menu, item);
			}
			W_MapView(dview);
		}

		if (!rem || menu->flags.isFactory || copy) {
			slideWindow(dpy, W_VIEW_DRAWABLE(dview), x - dx, y - dy, orix, oriy);

			if (!menu->flags.isFactory && !copy) {
				WRemoveEditMenuItem(menu, dritem);
				handleItemDrop(dmenu ? dmenu : menu, dritem, oriy);
			}
		} else {
			WRemoveEditMenuItem(menu, dritem);
		}
	} else {
		WRemoveEditMenuItem(menu, dritem);

		if (menu->delegate && menu->delegate->itemCloned && (menu->flags.isFactory || copy)) {
			(*menu->delegate->itemCloned) (menu->delegate, menu, item, dritem);
		}

		handleItemDrop(dmenu, dritem, y - dy);

		if (item->submenu && (menu->flags.isFactory || copy)) {
			WEditMenu *submenu;

			submenu = duplicateMenu(item->submenu);
			WSetEditMenuSubmenu(dmenu, dritem, submenu);
		}
	}

	/* can't destroy now because we're being called from
	 * the event handler of the item. and destroying now,
	 * would mean destroying the item too in some cases.
	 */
	WMAddIdleHandler((WMCallback *) W_DestroyView, dview);
}

static void handleItemClick(xcb_generic_event_t * event, void *data)
{
	WEditMenuItem *item = (WEditMenuItem *) data;
	WEditMenu *menu = item->parent;

	switch ((event->response_type & 0x7f)) {
	case XCB_BUTTON_PRESS:
		selectItem(menu, item);

		if (WMIsDoubleClick(event)) {
			editItemLabel(item);
		}

		menu->flags.isDragging = 1;
		menu->dragX = ((xcb_button_press_event_t *)event)->root_x;
		menu->dragY = ((xcb_button_press_event_t *)event)->root_y;
		break;

	case XCB_BUTTON_RELEASE:
		menu->flags.isDragging = 0;
		break;

	case XCB_MOTION_NOTIFY:
		if (menu->flags.isDragging) {
			if (abs(((xcb_button_press_event_t *)event)->root_x - menu->dragX) > 5
			    || abs(((xcb_button_press_event_t *)event)->root_y - menu->dragY) > 5) {
				menu->flags.isDragging = 0;
				dragItem(menu, item, ((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_CONTROL);
			}
		}
		break;
	}
}

static void destroyEditMenu(WEditMenu * mPtr)
{
	WMRemoveNotificationObserver(mPtr);

	if (mPtr->parentItem)
		mPtr->parentItem->submenu = NULL;

	WMFreeArray(mPtr->items);

	wfree(mPtr->tdelegate);

	wfree(mPtr);
}
