/*
 *  wmultiselectpopup.c
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 2026 Window Maker Team
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

#include <cairo.h>

#include "wconfig.h"

#include <xkbcommon/xkbcommon-keysyms.h>

#include "WINGsP.h"

typedef struct W_MultiSelectPopUpButton {
	W_Class widgetClass;
	WMView *view;

	void *clientData;
	WMAction *action;

	char *caption;		/* shown when the selection is empty */

	WMArray *items;

	short highlightedItem;

	WMView *menuView;	/* override redirect popup menu */

	xcb_pixmap_t menuPixmap;	/* background pixmap for menuView, freed on destroy */

	WMHandlerID timer;	/* for autoscroll */

	int scrollStartY;	/* for autoscroll */

	struct {
		unsigned int configured:1;
		unsigned int insideMenu:1;
		unsigned int enabled:1;
		unsigned int armed:1;	/* menu open by the current press; ignore its release */
	} flags;
} MultiSelectPopUpButton;

#define SCROLL_DELAY		10

#define DEFAULT_WIDTH	60
#define DEFAULT_HEIGHT 	20
#define DEFAULT_CAPTION	""

#define CHECK_X		 6
#define CHECK_INDENT	20

static void destroyMultiSelectPopUpButton(MultiSelectPopUpButton *bPtr);
static void paintMultiSelectPopUpButton(MultiSelectPopUpButton *bPtr);

static void handleEvents(xcb_generic_event_t *event, void *data);
static void handleActionEvents(xcb_generic_event_t *event, void *data);

static void resizeMenu(MultiSelectPopUpButton *bPtr);
static char *buildCaption(MultiSelectPopUpButton *bPtr);
static void paintCheckmark(cairo_t *cr, int y, int itemHeight);

/* WMCreateArrayWithDestructor wants void(void *), WMDestroyMenuItem takes
 * WMMenuItem *, so route the call through a matching signature. */
static void destroyMenuItemData(void *data)
{
	WMDestroyMenuItem(data);
}

WMMultiSelectPopUpButton *WMCreateMultiSelectPopUpButton(WMWidget *parent)
{
	MultiSelectPopUpButton *bPtr;
	W_Screen *scr = W_VIEW(parent)->screen;

	bPtr = wmalloc(sizeof(MultiSelectPopUpButton));

	bPtr->widgetClass = WC_MultiSelectPopUpButton;

	bPtr->view = W_CreateView(W_VIEW(parent));
	if (!bPtr->view) {
		wfree(bPtr);
		return NULL;
	}
	bPtr->view->self = bPtr;

	WMCreateEventHandler(bPtr->view, XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY
			     | ClientMessageMask, handleEvents, bPtr);

	W_ResizeView(bPtr->view, DEFAULT_WIDTH, DEFAULT_HEIGHT);
	bPtr->caption = wstrdup(DEFAULT_CAPTION);

	WMCreateEventHandler(bPtr->view, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE,
			     handleActionEvents, bPtr);

	bPtr->flags.enabled = 1;

	bPtr->items = WMCreateArrayWithDestructor(4, destroyMenuItemData);

	bPtr->highlightedItem = -1;

	bPtr->menuView = W_CreateUnmanagedTopView(scr);

	W_ResizeView(bPtr->menuView, bPtr->view->size.width, 1);

	WMCreateEventHandler(bPtr->menuView, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE
			     | XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_POINTER_MOTION
			     | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_EXPOSURE, handleActionEvents, bPtr);

	return bPtr;
}

void WMSetMultiSelectPopUpButtonAction(WMMultiSelectPopUpButton *bPtr,
					WMAction *action, void *clientData)
{
	CHECK_CLASS(bPtr, WC_MultiSelectPopUpButton);

	bPtr->action = action;
	bPtr->clientData = clientData;
}

WMMenuItem *WMAddMultiSelectPopUpButtonItem(WMMultiSelectPopUpButton *bPtr, const char *title)
{
	WMMenuItem *item;

	CHECK_CLASS(bPtr, WC_MultiSelectPopUpButton);

	if (!title)
		return NULL;

	item = WMCreateMenuItem();
	WMSetMenuItemTitle(item, title);

	WMAddToArray(bPtr->items, item);

	if (bPtr->menuView && bPtr->menuView->flags.realized)
		resizeMenu(bPtr);

	return item;
}

void WMRemoveAllMultiSelectPopUpButtonItems(WMMultiSelectPopUpButton *bPtr)
{
	CHECK_CLASS(bPtr, WC_MultiSelectPopUpButton);

	WMEmptyArray(bPtr->items);

	if (bPtr->menuView && bPtr->menuView->flags.realized)
		resizeMenu(bPtr);
	if (bPtr->view->flags.mapped)
		paintMultiSelectPopUpButton(bPtr);
}

void WMRemoveMultiSelectPopUpButtonItem(WMMultiSelectPopUpButton *bPtr, int index)
{
	CHECK_CLASS(bPtr, WC_MultiSelectPopUpButton);

	wassertr(index >= 0 && index < WMGetArrayItemCount(bPtr->items));

	WMDeleteFromArray(bPtr->items, index);

	if (bPtr->menuView && bPtr->menuView->flags.realized)
		resizeMenu(bPtr);
	if (bPtr->view->flags.mapped)
		paintMultiSelectPopUpButton(bPtr);
}

void WMSetMultiSelectPopUpButtonEnabled(WMMultiSelectPopUpButton *bPtr, bool flag)
{
	CHECK_CLASS(bPtr, WC_MultiSelectPopUpButton);
	bPtr->flags.enabled = ((flag == 0) ? 0 : 1);
	if (bPtr->view->flags.mapped)
		paintMultiSelectPopUpButton(bPtr);
}

bool WMGetMultiSelectPopUpButtonEnabled(WMMultiSelectPopUpButton *bPtr)
{
	CHECK_CLASS(bPtr, WC_MultiSelectPopUpButton);
	return bPtr->flags.enabled;
}

void WMSetMultiSelectPopUpButtonCaption(WMMultiSelectPopUpButton *bPtr, const char *text)
{
	CHECK_CLASS(bPtr, WC_MultiSelectPopUpButton);
	if (bPtr->caption)
		wfree(bPtr->caption);
	bPtr->caption = wstrdup(text ? text : "");
	if (bPtr->view->flags.mapped)
		paintMultiSelectPopUpButton(bPtr);
}

void WMSetMultiSelectPopUpButtonItemEnabled(WMMultiSelectPopUpButton *bPtr, int index, bool flag)
{
	CHECK_CLASS(bPtr, WC_MultiSelectPopUpButton);
	if (index < 0 || index >= WMGetArrayItemCount(bPtr->items))
		return;
	WMSetMenuItemEnabled(WMGetFromArray(bPtr->items, index), (flag ? 1 : 0));
}

bool WMGetMultiSelectPopUpButtonItemEnabled(WMMultiSelectPopUpButton *bPtr, int index)
{
	CHECK_CLASS(bPtr, WC_MultiSelectPopUpButton);
	if (index < 0 || index >= WMGetArrayItemCount(bPtr->items))
		return false;
	return WMGetMenuItemEnabled(WMGetFromArray(bPtr->items, index));
}

int WMGetMultiSelectPopUpButtonNumberOfItems(WMMultiSelectPopUpButton *bPtr)
{
	CHECK_CLASS(bPtr, WC_MultiSelectPopUpButton);
	return WMGetArrayItemCount(bPtr->items);
}

char *WMGetMultiSelectPopUpButtonItem(WMMultiSelectPopUpButton *bPtr, int index)
{
	CHECK_CLASS(bPtr, WC_MultiSelectPopUpButton);
	if (index < 0 || index >= WMGetArrayItemCount(bPtr->items))
		return NULL;
	return WMGetMenuItemTitle(WMGetFromArray(bPtr->items, index));
}

WMMenuItem *WMGetMultiSelectPopUpButtonMenuItem(WMMultiSelectPopUpButton *bPtr, int index)
{
	CHECK_CLASS(bPtr, WC_MultiSelectPopUpButton);
	return WMGetFromArray(bPtr->items, index);
}

void WMSetMultiSelectPopUpButtonSelectionMask(WMMultiSelectPopUpButton *bPtr, uint64_t mask)
{
	int i, n;

	CHECK_CLASS(bPtr, WC_MultiSelectPopUpButton);
	n = WMGetArrayItemCount(bPtr->items);
	if (n > 64)
		n = 64;
	for (i = 0; i < n; i++) {
		WMMenuItem *item = WMGetFromArray(bPtr->items, i);

		WMSetMenuItemState(item, (mask & ((uint64_t)1 << i)) ? 1 : 0);
	}
	if (bPtr->view->flags.mapped)
		paintMultiSelectPopUpButton(bPtr);
}

uint64_t WMGetMultiSelectPopUpButtonSelectionMask(WMMultiSelectPopUpButton *bPtr)
{
	uint64_t mask = 0;
	int i, n;

	CHECK_CLASS(bPtr, WC_MultiSelectPopUpButton);
	n = WMGetArrayItemCount(bPtr->items);
	if (n > 64)
		n = 64;
	for (i = 0; i < n; i++) {
		WMMenuItem *item = WMGetFromArray(bPtr->items, i);

		if (WMGetMenuItemState(item))
			mask |= (uint64_t)1 << i;
	}
	return mask;
}

static char *buildCaption(MultiSelectPopUpButton *bPtr)
{
	int i, selected = 0, itemCount;
	char buf[256];
	int len = 0;

	itemCount = WMGetArrayItemCount(bPtr->items);
	for (i = 0; i < itemCount; i++) {
		WMMenuItem *item = WMGetFromArray(bPtr->items, i);

		if (WMGetMenuItemState(item))
			selected++;
	}

	if (selected == 0)
		return wstrdup(bPtr->caption ? bPtr->caption : "");

	if (selected == itemCount && itemCount > 0)
		return wstrdup(_("All"));

	buf[0] = '\0';
	for (i = 0; i < itemCount; i++) {
		WMMenuItem *item = WMGetFromArray(bPtr->items, i);

		if (WMGetMenuItemState(item)) {
			char *title = WMGetMenuItemTitle(item);
			int tl = title ? strlen(title) : 0;

			if (len + tl + 2 >= (int)sizeof(buf) - 4) {
				strcpy(buf + len, "...");
				len += 3;
				break;
			}
			if (len > 0) {
				buf[len++] = ',';
				buf[len++] = ' ';
			}
			memcpy(buf + len, title, tl);
			len += tl;
		}
	}
	buf[len] = '\0';
	return wstrdup(buf);
}

static void paintCheckmark(cairo_t *cr, int y, int itemHeight)
{
	double cx = CHECK_X + 1;
	double cy = y + itemHeight / 2.0;

	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_set_line_width(cr, 2.0);
	cairo_move_to(cr, cx, cy - 1);
	cairo_line_to(cr, cx + 2.5, cy + 2.5);
	cairo_line_to(cr, cx + 7, cy - 3.5);
	cairo_stroke(cr);
}

static void paintMultiSelectPopUpButton(MultiSelectPopUpButton *bPtr)
{
	W_Screen *scr = bPtr->view->screen;
	WMPixmap *ind;
	char *caption;
	cairo_t *cr;

	caption = buildCaption(bPtr);

	cr = WCairoBeginBuffered(scr, bPtr->view->window);
	if (!cr) {
		wfree(caption);
		return;
	}
	W_SetSourceColor(cr, scr->gray);
	cairo_paint(cr);

	W_DrawRelief(cr, scr, 0, 0, bPtr->view->size.width, bPtr->view->size.height, WRRaised);

	if (caption && caption[0]) {
		W_PaintText(cr, scr->normalFont, 6,
				 (bPtr->view->size.height - WMFontHeight(scr->normalFont)) / 2,
				 bPtr->view->size.width - 20, WALeft,
				 bPtr->flags.enabled ? scr->black : scr->darkGray,
				 false, caption, strlen(caption));
	}

	ind = scr->popUpIndicator;
	W_PaintPixmap(cr, ind,
			 bPtr->view->size.width - ind->width - 4,
			 (bPtr->view->size.height - ind->height) / 2);

	WCairoEnd(cr);
	wfree(caption);
}

static void handleEvents(xcb_generic_event_t *event, void *data)
{
	MultiSelectPopUpButton *bPtr = (MultiSelectPopUpButton *)data;

	CHECK_CLASS(data, WC_MultiSelectPopUpButton);

	switch ((event->response_type & 0x7f)) {
	case XCB_EXPOSE:
		if (((xcb_expose_event_t *)event)->count != 0)
			break;
		paintMultiSelectPopUpButton(bPtr);
		break;

	case XCB_DESTROY_NOTIFY:
		destroyMultiSelectPopUpButton(bPtr);
		break;
	}
}

static void paintMenuEntry(MultiSelectPopUpButton *bPtr, int index, int highlight)
{
	W_Screen *scr = bPtr->view->screen;
	int yo, width, itemHeight, itemCount;
	char *title;
	cairo_t *cr;

	itemCount = WMGetArrayItemCount(bPtr->items);
	if (index < 0 || index >= itemCount)
		return;

	itemHeight = bPtr->view->size.height;
	width = bPtr->view->size.width;
	yo = (itemHeight - WMFontHeight(scr->normalFont)) / 2;

	cr = WCairoBeginForDrawable(scr, bPtr->menuView->window);
	if (!cr)
		return;

	/* unhighlight paints the gray menu background, highlight paints the
	 * selection color. Both repaint the checkmark and text from the
	 * current item state, since the selection can change while the menu
	 * stays open */
	if (highlight)
		W_SetSourceColor(cr, scr->selectedBack);
	else
		W_SetSourceColor(cr, scr->gray);
	cairo_rectangle(cr, 1, index * itemHeight + 1, width - 3, itemHeight - 3);
	cairo_fill(cr);

	title = WMGetMultiSelectPopUpButtonItem(bPtr, index);

	W_DrawRelief(cr, scr, 0, index * itemHeight, width, itemHeight, WRRaised);

	if (WMGetMenuItemState(WMGetFromArray(bPtr->items, index)))
		paintCheckmark(cr, index * itemHeight, itemHeight);

	W_PaintText(cr, scr->normalFont, CHECK_INDENT,
			 index * itemHeight + yo, width, WALeft, scr->black,
			 false, title, strlen(title));

	WCairoEnd(cr);
}

static xcb_pixmap_t makeMenuPixmap(MultiSelectPopUpButton *bPtr)
{
	xcb_pixmap_t pixmap;
	W_Screen *scr = bPtr->view->screen;
	WMMenuItem *item;
	WMArrayIterator iter;
	WMColor *color;
	char *text;
	cairo_t *cr;
	int yo, i;
	int width, height, itemHeight;

	itemHeight = bPtr->view->size.height;
	width = bPtr->view->size.width;
	height = itemHeight * WMGetArrayItemCount(bPtr->items);
	yo = (itemHeight - WMFontHeight(scr->normalFont)) / 2;

	pixmap = W_CreateScaledPixmap(scr, width, height);

	cr = WCairoBeginForDrawable(scr, pixmap);
	W_SetSourceColor(cr, scr->gray);
	cairo_paint(cr);

	i = 0;
	WM_ITERATE_ARRAY(bPtr->items, item, iter) {
		text = WMGetMenuItemTitle(item);

		W_DrawRelief(cr, scr, 0, i * itemHeight, width, itemHeight, WRRaised);

		if (WMGetMenuItemState(item))
			paintCheckmark(cr, i * itemHeight, itemHeight);

		color = WMGetMenuItemEnabled(item) ? scr->black : scr->darkGray;

		W_PaintText(cr, scr->normalFont, CHECK_INDENT,
				 i * itemHeight + yo, width, WALeft, color,
				 false, text, strlen(text));

		i++;
	}
	WCairoEnd(cr);

	return pixmap;
}

static void resizeMenu(MultiSelectPopUpButton *bPtr)
{
	int height;

	height = WMGetArrayItemCount(bPtr->items) * bPtr->view->size.height;
	if (height > 0)
		W_ResizeView(bPtr->menuView, bPtr->view->size.width, height);
}

static void popUpMenu(MultiSelectPopUpButton *bPtr)
{
	W_Screen *scr = bPtr->view->screen;
	xcb_window_t dummyW;
	int scrHeight = scr->rootView->size.height;
	int x, y;

	if (!bPtr->flags.enabled)
		return;

	if (WMGetArrayItemCount(bPtr->items) < 1)
		return;

	if (!bPtr->menuView->flags.realized) {
		W_RealizeView(bPtr->menuView);
		resizeMenu(bPtr);
	}

	wXTranslateCoordinates(scr->display, bPtr->view->window, scr->rootWin,
			      0, 0, &x, &y, &dummyW);

	y += bPtr->view->size.height;

	if (W_VIEW_WIDTH(bPtr->menuView) != W_VIEW_WIDTH(bPtr->view)
	    || W_VIEW_HEIGHT(bPtr->menuView) != W_VIEW_HEIGHT(bPtr->view))
		resizeMenu(bPtr);

	/* menus taller than the screen keep the bottom-edge autoscroll */
	if (y + bPtr->menuView->size.height > scrHeight
	    && bPtr->menuView->size.height <= scrHeight)
		y = scrHeight - bPtr->menuView->size.height;
	if (y < 0)
		y = 0;

	W_MoveView(bPtr->menuView, x, y);

	if (bPtr->menuPixmap != XCB_NONE)
		xcb_free_pixmap(scr->display, bPtr->menuPixmap);
	bPtr->menuPixmap = makeMenuPixmap(bPtr);
	xcb_change_window_attributes(scr->display, bPtr->menuView->window, XCB_CW_BACK_PIXMAP, (const uint32_t[]){ bPtr->menuPixmap });
	xcb_clear_area(scr->display, 0, bPtr->menuView->window, 0, 0, 0, 0);

	W_MapView(bPtr->menuView);

	/* trigger a full expose so the Expose handler repaints every entry
	 * with the current checkmark state */
	xcb_clear_area(scr->display, 0, bPtr->menuView->window, 0, 0, 0, 0);

	bPtr->highlightedItem = -1;

	wXGrabKeyboard(scr->display, bPtr->menuView->window, false,
		      XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_CURRENT_TIME);
}

static void popDownMenu(MultiSelectPopUpButton *bPtr)
{
	W_Screen *scr = bPtr->view->screen;

	wXUngrabKeyboard(scr->display, XCB_CURRENT_TIME);
	W_UnmapView(bPtr->menuView);
	bPtr->flags.insideMenu = 0;
	bPtr->highlightedItem = -1;
}

static void autoScroll(void *data)
{
	MultiSelectPopUpButton *bPtr = (MultiSelectPopUpButton *)data;
	int scrHeight = WMWidgetScreen(bPtr)->rootView->size.height;
	int repeat = 0;
	int dy = 0;

	if (bPtr->scrollStartY >= scrHeight - 1
	    && bPtr->menuView->pos.y + bPtr->menuView->size.height >= scrHeight - 1) {
		repeat = 1;
		if (bPtr->menuView->pos.y + bPtr->menuView->size.height - 5 <= scrHeight - 1)
			dy = scrHeight - 1 - (bPtr->menuView->pos.y + bPtr->menuView->size.height);
		else
			dy = -5;
	} else if (bPtr->scrollStartY <= 1 && bPtr->menuView->pos.y < 1) {
		repeat = 1;
		if (bPtr->menuView->pos.y + 5 > 1)
			dy = 1 - bPtr->menuView->pos.y;
		else
			dy = 5;
	}

	if (repeat) {
		int oldItem;

		W_MoveView(bPtr->menuView, bPtr->menuView->pos.x, bPtr->menuView->pos.y + dy);

		oldItem = bPtr->highlightedItem;
		bPtr->highlightedItem = (bPtr->scrollStartY - bPtr->menuView->pos.y)
		    / bPtr->view->size.height;

		if (oldItem != bPtr->highlightedItem) {
			WMMenuItem *item;

			paintMenuEntry(bPtr, oldItem, false);
			if (bPtr->highlightedItem >= 0 &&
			    bPtr->highlightedItem < WMGetArrayItemCount(bPtr->items)) {
				item = WMGetMultiSelectPopUpButtonMenuItem(bPtr, bPtr->highlightedItem);
				paintMenuEntry(bPtr, bPtr->highlightedItem, WMGetMenuItemEnabled(item));
			} else {
				bPtr->highlightedItem = -1;
			}
		}

		bPtr->timer = WMAddTimerHandler(SCROLL_DELAY, autoScroll, bPtr);
	} else {
		bPtr->timer = NULL;
	}
}

static void handleActionEvents(xcb_generic_event_t *event, void *data)
{
	MultiSelectPopUpButton *bPtr = (MultiSelectPopUpButton *)data;
	int oldItem;
	int scrHeight = WMWidgetScreen(bPtr)->rootView->size.height;
	int i, n, first, last, itemHeight;

	CHECK_CLASS(data, WC_MultiSelectPopUpButton);

	if (WMGetArrayItemCount(bPtr->items) < 1
	    && (event->response_type & 0x7f) != XCB_BUTTON_RELEASE
	    && (event->response_type & 0x7f) != XCB_KEY_PRESS)
		return;

	switch ((event->response_type & 0x7f)) {
	case XCB_EXPOSE:
		itemHeight = bPtr->view->size.height;
		n = WMGetArrayItemCount(bPtr->items);
		first = ((xcb_expose_event_t *)event)->y / itemHeight;
		last = (((xcb_expose_event_t *)event)->y
			+ (int)((xcb_expose_event_t *)event)->height - 1) / itemHeight;
		if (last > n - 1)
			last = n - 1;
		for (i = first; i <= last; i++)
			paintMenuEntry(bPtr, i, i == bPtr->highlightedItem);
		break;

	case XCB_LEAVE_NOTIFY:
		bPtr->flags.insideMenu = 0;
		if (bPtr->menuView->flags.mapped)
			paintMenuEntry(bPtr, bPtr->highlightedItem, false);
		bPtr->highlightedItem = -1;
		break;

	case XCB_ENTER_NOTIFY:
		bPtr->flags.insideMenu = 1;
		break;

	case XCB_KEY_PRESS:
		if (bPtr->menuView->flags.mapped) {
			xkb_keysym_t ksym;

			ksym = W_EventKeysym(W_VIEW_SCREEN(bPtr->view),
					     (const xcb_key_press_event_t *)event);
			if (ksym == XKB_KEY_Escape) {
				WMDeleteTimerHandlerPtr(&bPtr->timer);
				wXUngrabPointer(bPtr->view->screen->display, XCB_CURRENT_TIME);
				popDownMenu(bPtr);
				if (bPtr->view->flags.mapped)
					paintMultiSelectPopUpButton(bPtr);
			}
		}
		break;

	case XCB_MOTION_NOTIFY:
		if (bPtr->flags.insideMenu) {
			oldItem = bPtr->highlightedItem;
			bPtr->highlightedItem = ((xcb_motion_notify_event_t *)event)->event_y / bPtr->view->size.height;
			if (oldItem != bPtr->highlightedItem) {
				WMMenuItem *item;

				paintMenuEntry(bPtr, oldItem, false);
				if (bPtr->highlightedItem >= 0 &&
				    bPtr->highlightedItem < WMGetArrayItemCount(bPtr->items)) {
					item = WMGetMultiSelectPopUpButtonMenuItem(bPtr, bPtr->highlightedItem);
					paintMenuEntry(bPtr, bPtr->highlightedItem, WMGetMenuItemEnabled(item));
				} else {
					bPtr->highlightedItem = -1;
				}
			}

			if (((xcb_motion_notify_event_t *)event)->root_y >= scrHeight - 1 || ((xcb_motion_notify_event_t *)event)->root_y <= 1) {
				bPtr->scrollStartY = ((xcb_motion_notify_event_t *)event)->root_y;
				if (!bPtr->timer)
					autoScroll(bPtr);
			} else {
				WMDeleteTimerHandlerPtr(&bPtr->timer);
			}
		}
		break;

	case XCB_BUTTON_PRESS:
		if (!bPtr->flags.enabled)
			break;

		if (((xcb_button_press_event_t *)event)->detail == WINGsConfiguration.mouseWheelUp ||
		    ((xcb_button_press_event_t *)event)->detail == WINGsConfiguration.mouseWheelDown)
			break;

		if (bPtr->menuView->flags.mapped) {
			/* menu already open (subsequent press during a
			 * multi-select session): just update the highlight
			 * without re-popping or re-grabbing */
			if (bPtr->flags.insideMenu && bPtr->highlightedItem >= 0)
				paintMenuEntry(bPtr, bPtr->highlightedItem, false);
			bPtr->highlightedItem = ((xcb_button_press_event_t *)event)->event_y / bPtr->view->size.height;
			if (bPtr->highlightedItem >= 0 &&
			    bPtr->highlightedItem < WMGetArrayItemCount(bPtr->items)) {
				WMMenuItem *item = WMGetMultiSelectPopUpButtonMenuItem(bPtr, bPtr->highlightedItem);

				paintMenuEntry(bPtr, bPtr->highlightedItem, WMGetMenuItemEnabled(item));
			} else {
				bPtr->highlightedItem = -1;
			}
			break;
		}

		popUpMenu(bPtr);
		bPtr->highlightedItem = -1;
		bPtr->flags.insideMenu = 1;
		bPtr->flags.armed = 1;
		wXGrabPointer(bPtr->view->screen->display, bPtr->menuView->window,
			     false, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION
			     | XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE, XCB_CURRENT_TIME);
		break;

	case XCB_BUTTON_RELEASE:
		WMDeleteTimerHandlerPtr(&bPtr->timer);

		if (((xcb_button_press_event_t *)event)->detail == WINGsConfiguration.mouseWheelUp ||
		    ((xcb_button_press_event_t *)event)->detail == WINGsConfiguration.mouseWheelDown)
			break;

		/* the release that opened the menu must not toggle anything */
		if (bPtr->flags.armed) {
			bPtr->flags.armed = 0;
			break;
		}

		if (bPtr->flags.insideMenu && bPtr->highlightedItem >= 0
		    && bPtr->highlightedItem < WMGetArrayItemCount(bPtr->items)) {
			WMMenuItem *item = WMGetMultiSelectPopUpButtonMenuItem(bPtr, bPtr->highlightedItem);

			if (WMGetMenuItemEnabled(item)) {
				/* toggle the item, keep the menu open */
				int state = WMGetMenuItemState(item) ? 0 : 1;
				W_Screen *scr = bPtr->view->screen;

				WMSetMenuItemState(item, state);
				paintMenuEntry(bPtr, bPtr->highlightedItem, true);
				/* refresh the menu background pixmap so a later
				 * expose reflects the new checkmark state */
				if (bPtr->menuPixmap != XCB_NONE)
					xcb_free_pixmap(scr->display, bPtr->menuPixmap);
				bPtr->menuPixmap = makeMenuPixmap(bPtr);
				xcb_change_window_attributes(scr->display, bPtr->menuView->window, XCB_CW_BACK_PIXMAP, (const uint32_t[]){ bPtr->menuPixmap });
				paintMultiSelectPopUpButton(bPtr);
				if (bPtr->action)
					(*bPtr->action)(bPtr, bPtr->clientData);
				break;
			}
		}

		/* release outside the menu, or on a disabled item: close */
		wXUngrabPointer(bPtr->view->screen->display, XCB_CURRENT_TIME);
		popDownMenu(bPtr);
		paintMultiSelectPopUpButton(bPtr);
		break;
	}
}

static void destroyMultiSelectPopUpButton(MultiSelectPopUpButton *bPtr)
{
	if (bPtr->timer)
		WMDeleteTimerHandler(bPtr->timer);

	WMFreeArray(bPtr->items);

	if (bPtr->caption)
		wfree(bPtr->caption);

	if (bPtr->menuPixmap != XCB_NONE)
		wXFreePixmap(bPtr->view->screen->display, bPtr->menuPixmap);

	W_DestroyView(bPtr->menuView);

	wfree(bPtr);
}
