/*
 *  wpopupbutton.c
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 1998 scottc
 *  Copyright (c) 1999-2004 Dan Pascu
 *  Copyright (c) 1999-2001 Alfredo K. Kojima
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

#include <cairo.h>

#include "WINGsP.h"

typedef struct W_PopUpButton {
	W_Class widgetClass;
	WMView *view;

	void *clientData;
	WMAction *action;

	char *caption;

	WMArray *items;

	short selectedItemIndex;

	short highlightedItem;

	WMView *menuView;	/* override redirect popup menu */

	xcb_pixmap_t menuPixmap;	/* background pixmap for menuView, freed on popdown/destroy */

	WMHandlerID timer;	/* for autoscroll */

	 /**/ int scrollStartY;	/* for autoscroll */

	struct {
		unsigned int pullsDown:1;

		unsigned int configured:1;

		unsigned int insideMenu:1;

		unsigned int enabled:1;

		unsigned int menuAutoWidth:1;

	} flags;
} PopUpButton;

#define MENU_BLINK_DELAY	60000
#define MENU_BLINK_COUNT	2

#define SCROLL_DELAY		10

#define DEFAULT_WIDTH	60
#define DEFAULT_HEIGHT 	20
#define DEFAULT_CAPTION	""

/* the popup menu view is a scaled top-level window, so device-space
 * parameters (root positions, xcb_clear_area) need logical values
 * multiplied by the screen scale
 */
static int popupDevice(W_Screen *scr, int v)
{
	return (int)(v * scr->uiScale + 0.5);
}

static void destroyPopUpButton(PopUpButton * bPtr);
static void paintPopUpButton(PopUpButton * bPtr);

static void handleEvents(xcb_generic_event_t * event, void *data);
static void handleActionEvents(xcb_generic_event_t * event, void *data);

static void resizeMenu(PopUpButton * bPtr);

/* WMCreateArrayWithDestructor wants void(void *), WMDestroyMenuItem takes
 * WMMenuItem *, so route the call through a matching signature. */
static void destroyMenuItemData(void *data)
{
	WMDestroyMenuItem(data);
}

WMPopUpButton *WMCreatePopUpButton(WMWidget * parent)
{
	PopUpButton *bPtr;
	W_Screen *scr = W_VIEW(parent)->screen;

	bPtr = wmalloc(sizeof(PopUpButton));

	bPtr->widgetClass = WC_PopUpButton;

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

	WMCreateEventHandler(bPtr->view, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE, handleActionEvents, bPtr);

	bPtr->flags.enabled = 1;

	bPtr->items = WMCreateArrayWithDestructor(4, destroyMenuItemData);

	bPtr->selectedItemIndex = -1;

	bPtr->menuView = W_CreateUnmanagedTopView(scr);

	W_ResizeView(bPtr->menuView, bPtr->view->size.width, 1);

	WMCreateEventHandler(bPtr->menuView, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE
			     | XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_BUTTON_MOTION
			     | XCB_EVENT_MASK_EXPOSURE, handleActionEvents, bPtr);

	return bPtr;
}

void WMSetPopUpButtonAction(WMPopUpButton * bPtr, WMAction * action, void *clientData)
{
	CHECK_CLASS(bPtr, WC_PopUpButton);

	bPtr->action = action;

	bPtr->clientData = clientData;
}

WMMenuItem *WMAddPopUpButtonItem(WMPopUpButton * bPtr, const char *title)
{
	WMMenuItem *item;

	CHECK_CLASS(bPtr, WC_PopUpButton);

	if (!title)
		return NULL;

	item = WMCreateMenuItem();
	WMSetMenuItemTitle(item, title);

	WMAddToArray(bPtr->items, item);

	if (bPtr->menuView && bPtr->menuView->flags.realized)
		resizeMenu(bPtr);

	return item;
}

WMMenuItem *WMInsertPopUpButtonItem(WMPopUpButton * bPtr, int index, const char *title)
{
	WMMenuItem *item;

	CHECK_CLASS(bPtr, WC_PopUpButton);

	if (!title)
		return NULL;

	item = WMCreateMenuItem();
	WMSetMenuItemTitle(item, title);

	WMInsertInArray(bPtr->items, index, item);

	/* if there is an selected item, update it's index to match the new
	 * position */
	if (index < bPtr->selectedItemIndex)
		bPtr->selectedItemIndex++;

	if (bPtr->menuView && bPtr->menuView->flags.realized)
		resizeMenu(bPtr);

	return item;
}

void WMRemovePopUpButtonItem(WMPopUpButton * bPtr, int index)
{
	CHECK_CLASS(bPtr, WC_PopUpButton);

	wassertr(index >= 0 && index < WMGetArrayItemCount(bPtr->items));

	WMDeleteFromArray(bPtr->items, index);

	if (bPtr->selectedItemIndex >= 0 && !bPtr->flags.pullsDown) {
		if (index < bPtr->selectedItemIndex)
			bPtr->selectedItemIndex--;
		else if (index == bPtr->selectedItemIndex) {
			/* reselect first item if the removed item is the
			 * selected one */
			bPtr->selectedItemIndex = 0;
			if (bPtr->view->flags.mapped)
				paintPopUpButton(bPtr);
		}
	}

	if (bPtr->menuView && bPtr->menuView->flags.realized)
		resizeMenu(bPtr);
}

void WMSetPopUpButtonEnabled(WMPopUpButton * bPtr, bool flag)
{
	bPtr->flags.enabled = ((flag == 0) ? 0 : 1);
	if (bPtr->view->flags.mapped)
		paintPopUpButton(bPtr);
}

bool WMGetPopUpButtonEnabled(WMPopUpButton * bPtr)
{
	return bPtr->flags.enabled;
}

void WMSetPopUpButtonSelectedItem(WMPopUpButton * bPtr, int index)
{

	wassertr(index < WMGetArrayItemCount(bPtr->items));

	/* if (index >= WMGetArrayCount(bPtr->items))
	   index = -1; */

	bPtr->selectedItemIndex = index;

	if (bPtr->view->flags.mapped)
		paintPopUpButton(bPtr);
}

int WMGetPopUpButtonSelectedItem(WMPopUpButton * bPtr)
{
	if (!bPtr->flags.pullsDown && bPtr->selectedItemIndex < 0)
		return -1;
	else
		return bPtr->selectedItemIndex;
}

void WMSetPopUpButtonText(WMPopUpButton * bPtr, const char *text)
{
	if (bPtr->caption)
		wfree(bPtr->caption);
	if (text)
		bPtr->caption = wstrdup(text);
	else
		bPtr->caption = NULL;
	if (bPtr->view->flags.realized) {
		if (bPtr->flags.pullsDown || bPtr->selectedItemIndex < 0) {
			paintPopUpButton(bPtr);
		}
	}
}

void WMSetPopUpButtonItemEnabled(WMPopUpButton * bPtr, int index, bool flag)
{
	WMSetMenuItemEnabled(WMGetFromArray(bPtr->items, index), (flag ? 1 : 0));
}

bool WMGetPopUpButtonItemEnabled(WMPopUpButton * bPtr, int index)
{
	return WMGetMenuItemEnabled(WMGetFromArray(bPtr->items, index));
}

void WMSetPopUpButtonPullsDown(WMPopUpButton * bPtr, bool flag)
{
	bPtr->flags.pullsDown = ((flag == 0) ? 0 : 1);
	if (flag) {
		bPtr->selectedItemIndex = -1;
	}

	if (bPtr->view->flags.mapped)
		paintPopUpButton(bPtr);
}

void WMSetPopUpButtonMenuAutoWidth(WMPopUpButton *bPtr, bool flag)
{
	bPtr->flags.menuAutoWidth = ((flag == 0) ? 0 : 1);

	if (bPtr->menuView && bPtr->menuView->flags.realized)
		resizeMenu(bPtr);
}

int WMGetPopUpButtonNumberOfItems(WMPopUpButton * bPtr)
{
	return WMGetArrayItemCount(bPtr->items);
}

char *WMGetPopUpButtonItem(WMPopUpButton * bPtr, int index)
{
	if (index >= WMGetArrayItemCount(bPtr->items) || index < 0)
		return NULL;

	return WMGetMenuItemTitle(WMGetFromArray(bPtr->items, index));
}

WMMenuItem *WMGetPopUpButtonMenuItem(WMPopUpButton * bPtr, int index)
{
	return WMGetFromArray(bPtr->items, index);
}

int WMSelectPopUpButtonPreviousItem(WMPopUpButton * bPtr)
{
	int testIndex;

	CHECK_CLASS(bPtr, WC_PopUpButton);

	if (bPtr->flags.pullsDown || bPtr->selectedItemIndex < 0)
		return -1;

	testIndex = bPtr->selectedItemIndex - 1;

	while (testIndex >= 0 && !WMGetPopUpButtonItemEnabled(bPtr, testIndex))
		testIndex--;

	if (testIndex != -1) {
		WMSetPopUpButtonSelectedItem(bPtr, testIndex);
		if (bPtr->action)
			(*bPtr->action) (bPtr, bPtr->clientData);
		return testIndex;
	}

	return -1;
}

int WMSelectPopUpButtonNextItem(WMPopUpButton * bPtr)
{
	int itemCount;
	int testIndex;

	CHECK_CLASS(bPtr, WC_PopUpButton);

	if (bPtr->flags.pullsDown || bPtr->selectedItemIndex < 0)
		return -1;

	itemCount = WMGetArrayItemCount(bPtr->items);
	testIndex = bPtr->selectedItemIndex + 1;

	while (testIndex < itemCount && !WMGetPopUpButtonItemEnabled(bPtr, testIndex))
		testIndex++;

	if (testIndex != itemCount) {
		WMSetPopUpButtonSelectedItem(bPtr, testIndex);
		if (bPtr->action)
			(*bPtr->action) (bPtr, bPtr->clientData);
		return testIndex;
	}

	return -1;
}

static void paintPopUpButton(PopUpButton * bPtr)
{
	W_Screen *scr = bPtr->view->screen;
	WMPixmap *ind;
	char *caption;
	cairo_t *cr;

	if (bPtr->flags.pullsDown) {
		caption = bPtr->caption;
	} else {
		if (bPtr->selectedItemIndex < 0) {
			/* if no item selected, show the caption */
			caption = bPtr->caption;
		} else {
			caption = WMGetPopUpButtonItem(bPtr, bPtr->selectedItemIndex);
		}
	}

	cr = WCairoBeginBuffered(scr, bPtr->view->window);
	if (!cr)
		return;
	W_SetSourceColor(cr, scr->gray);
	cairo_paint(cr);

	W_DrawRelief(cr, scr, 0, 0, bPtr->view->size.width, bPtr->view->size.height, WRRaised);

	if (caption) {
		W_PaintText(cr, scr->normalFont, 6,
				 (bPtr->view->size.height - WMFontHeight(scr->normalFont)) / 2,
				 bPtr->view->size.width, WALeft,
				 bPtr->flags.enabled ? scr->black : scr->darkGray,
				 false, caption, strlen(caption));
	}

	ind = bPtr->flags.pullsDown ? scr->pullDownIndicator : scr->popUpIndicator;
	W_PaintPixmap(cr, ind,
			  bPtr->view->size.width - ind->width - 4,
			  (bPtr->view->size.height - ind->height) / 2);

	WCairoEnd(cr);
}

static void handleEvents(xcb_generic_event_t * event, void *data)
{
	PopUpButton *bPtr = (PopUpButton *) data;

	CHECK_CLASS(data, WC_PopUpButton);

	switch ((event->response_type & 0x7f)) {
	case XCB_EXPOSE:
		if (((xcb_expose_event_t *)event)->count != 0)
			break;
		paintPopUpButton(bPtr);
		break;

	case XCB_DESTROY_NOTIFY:
		destroyPopUpButton(bPtr);
		break;
	}
}

static void paintMenuEntry(PopUpButton * bPtr, int index, int highlight)
{
	W_Screen *scr = bPtr->view->screen;
	int yo;
	int width, itemHeight, itemCount;
	char *title;
	cairo_t *cr;

	itemCount = WMGetArrayItemCount(bPtr->items);
	if (index < 0 || index >= itemCount)
		return;

	itemHeight = bPtr->view->size.height;
	width = bPtr->menuView->size.width;
	yo = (itemHeight - WMFontHeight(scr->normalFont)) / 2;

	if (!highlight) {
		xcb_clear_area(scr->display, false, bPtr->menuView->window, 0,
			       popupDevice(scr, index * itemHeight),
			       popupDevice(scr, width),
			       popupDevice(scr, itemHeight));
		return;
	}

	cr = WCairoBeginForDrawable(scr, bPtr->menuView->window);
	if (!cr)
		return;
	W_SetSourceColor(cr, scr->selectedBack);
	cairo_rectangle(cr, 1, index * itemHeight + 1, width - 3, itemHeight - 3);
	cairo_fill(cr);

	title = WMGetPopUpButtonItem(bPtr, index);

	W_DrawRelief(cr, scr, 0, index * itemHeight, width, itemHeight, WRRaised);

	W_PaintText(cr, scr->normalFont, 6,
			 index * itemHeight + yo, width, WALeft, scr->black,
			 false, title, strlen(title));

	if (!bPtr->flags.pullsDown && index == bPtr->selectedItemIndex)
		W_PaintPixmap(cr, scr->popUpIndicator,
				  width - scr->popUpIndicator->width - 4,
				  index * itemHeight + (itemHeight - scr->popUpIndicator->height) / 2);
	WCairoEnd(cr);
}

static xcb_pixmap_t makeMenuPixmap(PopUpButton * bPtr)
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
	width = bPtr->menuView->size.width;
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

		color = WMGetMenuItemEnabled(item) ? scr->black : scr->darkGray;

		W_PaintText(cr, scr->normalFont, 6,
				 i * itemHeight + yo, width, WALeft, color,
				 false, text, strlen(text));

		if (!bPtr->flags.pullsDown && i == bPtr->selectedItemIndex)
			W_PaintPixmap(cr, scr->popUpIndicator,
					  width - scr->popUpIndicator->width - 4,
					  i * itemHeight + (itemHeight - scr->popUpIndicator->height) / 2);

		i++;
	}
	WCairoEnd(cr);

	return pixmap;
}

static void resizeMenu(PopUpButton * bPtr)
{
	int height;
	int width = bPtr->view->size.width;
	int scrWidth = bPtr->view->screen->rootView->size.width;

	if (bPtr->flags.menuAutoWidth) {
		W_Screen *scr = bPtr->view->screen;
		WMMenuItem *item;
		WMArrayIterator iter;
		int maxw = 0;

		WM_ITERATE_ARRAY(bPtr->items, item, iter) {
			char *title = WMGetMenuItemTitle(item);
			int w = title ? WMWidthOfString(scr->normalFont, title, strlen(title)) : 0;

			if (w > maxw)
				maxw = w;
		}
		width = maxw + scr->popUpIndicator->width + 12;
	}

	if (width < bPtr->view->size.width)
		width = bPtr->view->size.width;
	if (width > scrWidth)
		width = scrWidth;

	height = WMGetArrayItemCount(bPtr->items) * bPtr->view->size.height;
	if (height > 0)
		W_ResizeView(bPtr->menuView, width, height);
}

static void popUpMenu(PopUpButton * bPtr)
{
	W_Screen *scr = bPtr->view->screen;
	xcb_window_t dummyW;
	int scrWidth = scr->rootView->size.width;
	int itemHeight;
	int menuWidth;
	int x, y;

	if (!bPtr->flags.enabled)
		return;

	if (!bPtr->menuView->flags.realized) {
		W_RealizeView(bPtr->menuView);
		resizeMenu(bPtr);
	}

	if (WMGetArrayItemCount(bPtr->items) < 1)
		return;

	wXTranslateCoordinates(scr->display, bPtr->view->window, scr->rootWin, 0, 0, &x, &y, &dummyW);

	itemHeight = popupDevice(scr, bPtr->view->size.height);

	if (bPtr->flags.pullsDown) {
		y += itemHeight;
	} else {
		y -= itemHeight * bPtr->selectedItemIndex;
	}

	if (W_VIEW_WIDTH(bPtr->menuView) != W_VIEW_WIDTH(bPtr->view))
		resizeMenu(bPtr);

	menuWidth = popupDevice(scr, bPtr->menuView->size.width);
	if (x + menuWidth > scrWidth)
		x = scrWidth - menuWidth;
	if (x < 0)
		x = 0;

	W_MoveView(bPtr->menuView, x, y);

	if (bPtr->menuPixmap != XCB_NONE)
		xcb_free_pixmap(scr->display, bPtr->menuPixmap);
	bPtr->menuPixmap = makeMenuPixmap(bPtr);
	xcb_change_window_attributes(scr->display, bPtr->menuView->window, XCB_CW_BACK_PIXMAP, (const uint32_t[]){ bPtr->menuPixmap });
	xcb_clear_area(scr->display, 0, bPtr->menuView->window, 0, 0, 0, 0);

	W_MapView(bPtr->menuView);

	bPtr->highlightedItem = 0;
	if (!bPtr->flags.pullsDown && bPtr->selectedItemIndex < 0)
		paintMenuEntry(bPtr, bPtr->selectedItemIndex, true);
}

static void popDownMenu(PopUpButton * bPtr)
{
	W_UnmapView(bPtr->menuView);
}

static void autoScroll(void *data)
{
	PopUpButton *bPtr = (PopUpButton *) data;
	W_Screen *scr = bPtr->view->screen;
	int scrHeight = scr->rootView->size.height;
	int itemHeight = popupDevice(scr, bPtr->view->size.height);
	int menuHeight = popupDevice(scr, bPtr->menuView->size.height);
	int step = popupDevice(scr, 5);
	int repeat = 0;
	int dy = 0;

	if (bPtr->scrollStartY >= scrHeight - 1
	    && bPtr->menuView->pos.y + menuHeight >= scrHeight - 1) {
		repeat = 1;

		if (bPtr->menuView->pos.y + menuHeight - step <= scrHeight - 1)
			dy = scrHeight - 1 - (bPtr->menuView->pos.y + menuHeight);
		else
			dy = -step;

	} else if (bPtr->scrollStartY <= 1 && bPtr->menuView->pos.y < 1) {
		repeat = 1;

		if (bPtr->menuView->pos.y + step > 1)
			dy = 1 - bPtr->menuView->pos.y;
		else
			dy = step;
	}

	if (repeat) {
		int oldItem;

		W_MoveView(bPtr->menuView, bPtr->menuView->pos.x, bPtr->menuView->pos.y + dy);

		oldItem = bPtr->highlightedItem;
		bPtr->highlightedItem = (bPtr->scrollStartY - bPtr->menuView->pos.y)
		    / itemHeight;

		if (oldItem != bPtr->highlightedItem) {
			WMMenuItem *item;

			paintMenuEntry(bPtr, oldItem, false);

			if (bPtr->highlightedItem >= 0 && bPtr->highlightedItem < WMGetArrayItemCount(bPtr->items)) {
				item = WMGetPopUpButtonMenuItem(bPtr, bPtr->highlightedItem);
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

static void wheelScrollUp(PopUpButton * bPtr)
{
	int testIndex = bPtr->selectedItemIndex - 1;

	while (testIndex >= 0 && !WMGetPopUpButtonItemEnabled(bPtr, testIndex))
		testIndex--;
	if (testIndex != -1) {
		WMSetPopUpButtonSelectedItem(bPtr, testIndex);
		if (bPtr->action)
			(*bPtr->action) (bPtr, bPtr->clientData);
	}
}

static void wheelScrollDown(PopUpButton * bPtr)
{
	int itemCount = WMGetArrayItemCount(bPtr->items);
	int testIndex = bPtr->selectedItemIndex + 1;

	while (testIndex < itemCount && !WMGetPopUpButtonItemEnabled(bPtr, testIndex))
		testIndex++;
	if (testIndex != itemCount) {
		WMSetPopUpButtonSelectedItem(bPtr, testIndex);
		if (bPtr->action)
			(*bPtr->action) (bPtr, bPtr->clientData);
	}
}

static void handleActionEvents(xcb_generic_event_t * event, void *data)
{
	PopUpButton *bPtr = (PopUpButton *) data;
	int oldItem;
	int scrHeight = WMWidgetScreen(bPtr)->rootView->size.height;

	CHECK_CLASS(data, WC_PopUpButton);

	if (WMGetArrayItemCount(bPtr->items) < 1)
		return;

	switch ((event->response_type & 0x7f)) {
		/* called for menuView */
	case XCB_EXPOSE:
		paintMenuEntry(bPtr, bPtr->highlightedItem, true);
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

	case XCB_MOTION_NOTIFY:
		if (bPtr->flags.insideMenu) {
			oldItem = bPtr->highlightedItem;
			bPtr->highlightedItem = ((xcb_motion_notify_event_t *)event)->event_y / bPtr->view->size.height;
			if (oldItem != bPtr->highlightedItem) {
				WMMenuItem *item;

				paintMenuEntry(bPtr, oldItem, false);
				if (bPtr->highlightedItem >= 0 &&
				    bPtr->highlightedItem < WMGetArrayItemCount(bPtr->items)) {
					item = WMGetPopUpButtonMenuItem(bPtr, bPtr->highlightedItem);
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

		/* called for bPtr->view */
	case XCB_BUTTON_PRESS:
		if (!bPtr->flags.enabled)
			break;

		if (((xcb_button_press_event_t *)event)->detail == WINGsConfiguration.mouseWheelUp) {
			if (!bPtr->menuView->flags.mapped && !bPtr->flags.pullsDown) {
				wheelScrollUp(bPtr);
			}
			break;
		} else if (((xcb_button_press_event_t *)event)->detail == WINGsConfiguration.mouseWheelDown) {
			if (!bPtr->menuView->flags.mapped && !bPtr->flags.pullsDown) {
				wheelScrollDown(bPtr);
			}
			break;
		}
		popUpMenu(bPtr);
		if (!bPtr->flags.pullsDown) {
			bPtr->highlightedItem = bPtr->selectedItemIndex;
			bPtr->flags.insideMenu = 1;
		} else {
			bPtr->highlightedItem = -1;
			bPtr->flags.insideMenu = 0;
		}
		wXGrabPointer(bPtr->view->screen->display, bPtr->menuView->window,
			     false, XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_ENTER_WINDOW
			     | XCB_EVENT_MASK_LEAVE_WINDOW, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE, XCB_CURRENT_TIME);
		break;

	case XCB_BUTTON_RELEASE:
		if (((xcb_button_press_event_t *)event)->detail == WINGsConfiguration.mouseWheelUp ||
		    ((xcb_button_press_event_t *)event)->detail == WINGsConfiguration.mouseWheelDown) {
			break;
		}
		wXUngrabPointer(bPtr->view->screen->display, ((xcb_button_press_event_t *)event)->time);
		if (!bPtr->flags.pullsDown)
			popDownMenu(bPtr);

		WMDeleteTimerHandlerPtr(&bPtr->timer);

		if (bPtr->flags.insideMenu && bPtr->highlightedItem >= 0) {
			WMMenuItem *item;

			item = WMGetPopUpButtonMenuItem(bPtr, bPtr->highlightedItem);

			if (WMGetMenuItemEnabled(item)) {
				int i;
				WMSetPopUpButtonSelectedItem(bPtr, bPtr->highlightedItem);

				if (bPtr->flags.pullsDown) {
					for (i = 0; i < MENU_BLINK_COUNT; i++) {
						paintMenuEntry(bPtr, bPtr->highlightedItem, false);
						wXSync(bPtr->view->screen->display);
						wusleep(MENU_BLINK_DELAY);
						paintMenuEntry(bPtr, bPtr->highlightedItem, true);
						wXSync(bPtr->view->screen->display);
						wusleep(MENU_BLINK_DELAY);
					}
				}
				paintMenuEntry(bPtr, bPtr->highlightedItem, false);
				popDownMenu(bPtr);
				if (bPtr->menuView->flags.mapped)
					popDownMenu(bPtr);
				if (bPtr->action)
					(*bPtr->action) (bPtr, bPtr->clientData);
				break;
			}
		}
		if (bPtr->menuView->flags.mapped)
			popDownMenu(bPtr);
		break;
	}
}

static void destroyPopUpButton(PopUpButton * bPtr)
{
	if (bPtr->timer) {
		WMDeleteTimerHandler(bPtr->timer);
	}

	WMFreeArray(bPtr->items);

	if (bPtr->caption)
		wfree(bPtr->caption);

	if (bPtr->menuPixmap != XCB_NONE)
		wXFreePixmap(bPtr->view->screen->display, bPtr->menuPixmap);

	/* have to destroy explicitly because the popup is a toplevel */
	W_DestroyView(bPtr->menuView);

	wfree(bPtr);
}
