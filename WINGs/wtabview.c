/*
 *  wtabview.c
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 1999-2001 Alfredo K. Kojima
 *  Copyright (c) 1999-2004 Dan Pascu
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

#include "wconfig.h"

typedef struct W_TabView {
	W_Class widgetClass;
	W_View *view;

	struct W_TabViewItem **items;
	int itemCount;
	int maxItems;		/* size of items array, can be increased */

	int selectedItem;
	int firstVisible;

	int visibleTabs;

	WMFont *font;

	WMColor *lightGray;
	WMColor *tabColor;

	WMTabViewDelegate *delegate;

	short tabHeight;

	struct {
		WMReliefType relief:4;
		WMTitlePosition titlePosition:4;
		WMTabViewType type:2;

		unsigned enabled:1;
		unsigned tabbed:1;
		unsigned dontFitAll:1;
		unsigned bordered:1;
		unsigned uniformTabs:1;
	} flags;
} TabView;

typedef struct W_TabViewItem {
	WMTabView *tabView;

	W_View *view;

	char *label;

	short tabWidth;
	int identifier;

	struct {
		unsigned visible:1;
		unsigned enabled:1;
	} flags;
} W_TabViewItem;

#define DEFAULT_WIDTH	40
#define DEFAULT_HEIGHT	40

#define NORMAL_SIDE_OFFSET 8
#define BUTTONED_SIDE_OFFSET 15

static void destroyTabView(TabView * tPtr);
static void paintTabView(TabView * tPtr);

static void W_SetTabViewItemParent(WMTabViewItem * item, WMTabView * parent);

static void W_DrawLabel(WMTabViewItem *item, cairo_t *cr, WMRect rect, bool enabled);

static void W_UnmapTabViewItem(WMTabViewItem * item);

static void W_MapTabViewItem(WMTabViewItem * item);

static WMView *W_TabViewItemView(WMTabViewItem * item);

static int W_TabViewItemTabWidth(WMTabViewItem * item);

static void W_SetTabViewItemTabWidth(WMTabViewItem * item, int width);

static void recalcTabWidth(TabView * tPtr);
static void rearrange(TabView * tPtr);

static void didResize(struct W_ViewDelegate *, WMView *);

static W_ViewDelegate delegate = {
	NULL,
	NULL,
	didResize,
	NULL,
	NULL
};

static int positionOfTab(WMTabView * tabView, int tab)
{
	int i;
	int offs;

	if (tab < 0 || tab < tabView->firstVisible)
		return -1;

	if (tab > tabView->firstVisible + tabView->visibleTabs)
		return -1;

	if (tabView->flags.dontFitAll)
		offs = BUTTONED_SIDE_OFFSET;
	else
		offs = NORMAL_SIDE_OFFSET;

	for (i = tabView->firstVisible; i < tab; i++)
		offs += W_TabViewItemTabWidth(tabView->items[i]) - 10;

	return offs;
}

static int countVisibleTabs(TabView * tPtr, int first)
{
	int i;
	int width;

	if (first < 0) {
		width = W_VIEW_WIDTH(tPtr->view) - 2 * NORMAL_SIDE_OFFSET;
		first = 0;
	} else {
		width = W_VIEW_WIDTH(tPtr->view) - 2 * BUTTONED_SIDE_OFFSET;
	}

	for (i = first; i < tPtr->itemCount; i++) {
		width -= W_TabViewItemTabWidth(tPtr->items[i]) - 10;
		if (width <= 0) {
			return i - first;
		}
	}
	return i - first;
}

static void handleEvents(xcb_generic_event_t * event, void *data)
{
	TabView *tPtr = (TabView *) data;

	CHECK_CLASS(data, WC_TabView);

	switch ((event->response_type & 0x7f)) {
	case XCB_EXPOSE:
		if (((xcb_expose_event_t *)event)->count != 0)
			break;
		paintTabView(tPtr);
		break;

	case XCB_BUTTON_PRESS:
		if (tPtr->flags.enabled) {
			WMTabViewItem *item = WMTabViewItemAtPoint(tPtr,
								   ((xcb_button_press_event_t *)event)->event_x,
								   ((xcb_button_press_event_t *)event)->event_y);
			/*if (item && !item->flags.enabled)
			   break; */

			if (item && item->flags.enabled) {
				WMSelectTabViewItem(tPtr, item);
			} else if (tPtr->flags.dontFitAll) {
				int redraw = 0;
				int lastVisible = tPtr->firstVisible + tPtr->visibleTabs - 1;

				if (((xcb_button_press_event_t *)event)->event_x < BUTTONED_SIDE_OFFSET) {
					if (tPtr->firstVisible > 0) {
						redraw = 1;
						tPtr->firstVisible--;
					}
				} else if (((xcb_button_press_event_t *)event)->event_x > positionOfTab(tPtr, lastVisible)) {

					if (lastVisible < tPtr->itemCount - 1) {
						redraw = 1;
						tPtr->firstVisible++;
					}
				}
				tPtr->visibleTabs = countVisibleTabs(tPtr, tPtr->firstVisible);
				if (redraw) {
					paintTabView(tPtr);
				}
			}
		}
		break;

	case XCB_DESTROY_NOTIFY:
		destroyTabView(tPtr);
		break;
	}
}

static WMColor *createTabColor(WMScreen *scr, const char *spec, const char *key,
			       unsigned short red, unsigned short green, unsigned short blue)
{
	WMColor *color = NULL;

	if (spec) {
		color = WMCreateNamedColor(scr, spec, false);
		if (!color)
			wwarning(_("could not parse color \"%s\" for %s"), spec, key);
	}
	if (!color)
		color = WMCreateRGBColor(scr, red, green, blue, false);

	return color;
}

WMTabView *WMCreateTabView(WMWidget * parent)
{
	TabView *tPtr;
	WMScreen *scr = WMWidgetScreen(parent);

	tPtr = wmalloc(sizeof(TabView));
	tPtr->widgetClass = WC_TabView;

	tPtr->view = W_CreateView(W_VIEW(parent));
	if (!tPtr->view) {
		wfree(tPtr);
		return NULL;
	}
	tPtr->view->self = tPtr;
	tPtr->view->delegate = &delegate;

	tPtr->lightGray = createTabColor(scr, WINGsConfiguration.tabLightColor,
					 "TabLightColor", 0xd9d9, 0xd9d9, 0xd9d9);
	tPtr->tabColor = createTabColor(scr, WINGsConfiguration.tabColor,
					"TabColor", 0x8420, 0x8420, 0x8420);

	tPtr->font = WMRetainFont(scr->normalFont);

	tPtr->flags.type = WTTopTabsBevelBorder;
	tPtr->flags.bordered = 1;
	tPtr->flags.uniformTabs = 0;
	tPtr->flags.enabled = 1;

	WMCreateEventHandler(tPtr->view, XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_BUTTON_PRESS, handleEvents, tPtr);

	WMResizeWidget(tPtr, DEFAULT_WIDTH, DEFAULT_HEIGHT);

	tPtr->tabHeight = WMFontHeight(tPtr->font) + 3;

	return tPtr;
}

void WMSetTabViewDelegate(WMTabView * tPtr, WMTabViewDelegate * delegate)
{
	tPtr->delegate = delegate;
}

WMTabViewItem *WMAddTabViewItemWithView(WMTabView * tPtr, WMView * view, int identifier, const char *label)
{
	WMTabViewItem *item;

	item = WMCreateTabViewItemWithIdentifier(identifier);
	WMSetTabViewItemView(item, view);
	WMAddItemInTabView(tPtr, item);
	WMSetTabViewItemLabel(item, label);

	return item;
}

void WMAddItemInTabView(WMTabView * tPtr, WMTabViewItem * item)
{
	WMInsertItemInTabView(tPtr, tPtr->itemCount, item);
}

void WMSetTabViewEnabled(WMTabView * tPtr, bool flag)
{
	tPtr->flags.enabled = ((flag == 0) ? 0 : 1);
	if (W_VIEW_REALIZED(tPtr->view))
		paintTabView(tPtr);
}

void WMInsertItemInTabView(WMTabView * tPtr, int index, WMTabViewItem * item)
{
	wassertr(W_TabViewItemView(item) != NULL);

	if (tPtr->maxItems == tPtr->itemCount) {
		WMTabViewItem **items;

		items = wrealloc(tPtr->items, sizeof(WMTabViewItem *) * (tPtr->maxItems + 10));
		memset(&items[tPtr->maxItems], 0, sizeof(WMTabViewItem *) * 10);
		tPtr->items = items;
		tPtr->maxItems += 10;
	}

	if (index < 0)
		index = 0;
	else if (index > tPtr->itemCount)
		index = tPtr->itemCount;

	if (index == 0 && tPtr->items[0]) {
		W_UnmapTabViewItem(tPtr->items[0]);
	}

	if (index < tPtr->itemCount) {
		memmove(tPtr->items + index + 1, tPtr->items + index,
			(tPtr->itemCount - index) * sizeof(WMTabViewItem *));
	}

	tPtr->items[index] = item;

	tPtr->itemCount++;

	recalcTabWidth(tPtr);

	W_SetTabViewItemParent(item, tPtr);

	W_UnmapTabViewItem(item);

	if (tPtr->flags.bordered) {
		W_ReparentView(W_TabViewItemView(item), tPtr->view, 1, tPtr->tabHeight + 1);

		W_ResizeView(W_TabViewItemView(item), tPtr->view->size.width - 3,
			     tPtr->view->size.height - tPtr->tabHeight - 3);
	} else {
		W_ReparentView(W_TabViewItemView(item), tPtr->view, 0, tPtr->tabHeight);

		W_ResizeView(W_TabViewItemView(item), tPtr->view->size.width,
			     tPtr->view->size.height - tPtr->tabHeight);
	}

	if (index == 0) {
		W_MapTabViewItem(item);
	}
	if (tPtr->delegate && tPtr->delegate->didChangeNumberOfItems)
		(*tPtr->delegate->didChangeNumberOfItems) (tPtr->delegate, tPtr);

	if (W_VIEW_REALIZED(tPtr->view))
		paintTabView(tPtr);
}

void WMRemoveTabViewItem(WMTabView * tPtr, WMTabViewItem * item)
{
	int i;

	for (i = 0; i < tPtr->itemCount; i++) {
		if (tPtr->items[i] == item) {
			if (i < tPtr->itemCount - 1)
				memmove(&tPtr->items[i], &tPtr->items[i + 1],
				        (tPtr->itemCount - i - 1) * sizeof(WMTabViewItem *));
			else
				tPtr->items[i] = NULL;

			W_SetTabViewItemParent(item, NULL);

			tPtr->itemCount--;
			break;
		}
	}
	if (tPtr->delegate && tPtr->delegate->didChangeNumberOfItems)
		(*tPtr->delegate->didChangeNumberOfItems) (tPtr->delegate, tPtr);
}

static bool isInside(int x, int y, int width, int height, int px, int py)
{
	if (py >= y + height - 3 && py <= y + height && px >= x + py - (y + height - 3)
	    && px <= x + width - (py - (y + height - 3))) {

		return true;
	}
	if (py >= y + 3 && py < y + height - 3
	    && px >= x + 3 + ((y + 3) - py) * 3 / 7 && px <= x + width - 3 - ((y + 3) - py) * 3 / 7) {

		return true;
	}
	if (py >= y && py < y + 3 && px >= x + 7 + py - y && px <= x + width - 7 - (py - y)) {

		return true;
	}
	return false;
}

WMTabViewItem *WMTabViewItemAtPoint(WMTabView * tPtr, int x, int y)
{
	int i;
	int count = tPtr->visibleTabs;
	int first = tPtr->firstVisible;

	if (tPtr->flags.dontFitAll) {
		i = tPtr->selectedItem - tPtr->firstVisible;
		if (i >= 0 && i < tPtr->visibleTabs
		    && isInside(positionOfTab(tPtr, tPtr->selectedItem), 0,
				W_TabViewItemTabWidth(tPtr->items[tPtr->selectedItem]), tPtr->tabHeight, x, y)) {
			return tPtr->items[tPtr->selectedItem];
		}
	} else {
		i = tPtr->selectedItem;
		if (isInside(positionOfTab(tPtr, i), 0,
			     W_TabViewItemTabWidth(tPtr->items[i]), tPtr->tabHeight, x, y)) {
			return tPtr->items[i];
		}
	}

	for (i = first; i < first + count; i++) {
		int pos;

		pos = positionOfTab(tPtr, i);
		if (isInside(pos, 0, W_TabViewItemTabWidth(tPtr->items[i]), tPtr->tabHeight, x, y)) {
			return tPtr->items[i];
		}
	}
	return NULL;
}

void WMSetTabViewType(WMTabView * tPtr, WMTabViewType type)
{
	tPtr->flags.type = type;

	if (type != WTTopTabsBevelBorder)
		tPtr->tabHeight = 0;
	else
		tPtr->tabHeight = WMFontHeight(tPtr->font) + 3;

	if (type == WTNoTabsNoBorder)
		tPtr->flags.bordered = 0;
	else
		tPtr->flags.bordered = 1;

	rearrange(tPtr);
}

void WMSelectFirstTabViewItem(WMTabView * tPtr)
{
	WMSelectTabViewItemAtIndex(tPtr, 0);
}

void WMSelectLastTabViewItem(WMTabView * tPtr)
{
	WMSelectTabViewItemAtIndex(tPtr, tPtr->itemCount);
}

void WMSelectNextTabViewItem(WMTabView * tPtr)
{
	WMSelectTabViewItemAtIndex(tPtr, tPtr->selectedItem + 1);
}

void WMSelectPreviousTabViewItem(WMTabView * tPtr)
{
	WMSelectTabViewItemAtIndex(tPtr, tPtr->selectedItem - 1);
}

WMTabViewItem *WMGetSelectedTabViewItem(WMTabView * tPtr)
{
	if (tPtr->selectedItem < 0 || tPtr->selectedItem >= tPtr->itemCount)
		return NULL;
	return tPtr->items[tPtr->selectedItem];
}

void WMSelectTabViewItem(WMTabView * tPtr, WMTabViewItem * item)
{
	int i;

	for (i = 0; i < tPtr->itemCount; i++) {
		if (tPtr->items[i] == item) {
			WMSelectTabViewItemAtIndex(tPtr, i);
			break;
		}
	}
}

void WMSelectTabViewItemAtIndex(WMTabView * tPtr, int index)
{
	WMTabViewItem *item;

	if (tPtr->itemCount <= 0)
		return;

	if (index < 0)
		index = 0;
	else if (index >= tPtr->itemCount)
		index = tPtr->itemCount - 1;

	if (index == tPtr->selectedItem) {
		return;
	}

	if (tPtr->selectedItem < 0 || tPtr->selectedItem >= tPtr->itemCount)
		item = NULL;
	else
		item = tPtr->items[tPtr->selectedItem];

	if (tPtr->delegate && tPtr->delegate->shouldSelectItem)
		if (!(*tPtr->delegate->shouldSelectItem) (tPtr->delegate, tPtr, tPtr->items[index]))
			return;

	if (tPtr->delegate && tPtr->delegate->willSelectItem)
		(*tPtr->delegate->willSelectItem) (tPtr->delegate, tPtr, tPtr->items[index]);

	if (item != NULL)
		W_UnmapTabViewItem(item);

	item = tPtr->items[index];

	W_MapTabViewItem(item);

	tPtr->selectedItem = index;

	if (tPtr->delegate && tPtr->delegate->didSelectItem)
		(*tPtr->delegate->didSelectItem) (tPtr->delegate, tPtr, tPtr->items[index]);

	paintTabView(tPtr);
}

static void recalcTabWidth(TabView * tPtr)
{
	int i;
	/*int twidth = W_VIEW(tPtr)->size.width; */
	int width;

	if (tPtr->flags.uniformTabs) {
		int tabWidth;

		tabWidth = 0;

		for (i = 0; i < tPtr->itemCount; i++) {
			char *str = WMGetTabViewItemLabel(tPtr->items[i]);

			if (str) {
				width = WMWidthOfString(tPtr->font, str, strlen(str));
				if (width > tabWidth)
					tabWidth = width;
			}
		}

		tabWidth = tabWidth + 30;

		for (i = 0; i < tPtr->itemCount; i++)
			W_SetTabViewItemTabWidth(tPtr->items[i], tabWidth);

		tPtr->firstVisible = 0;
		tPtr->visibleTabs = countVisibleTabs(tPtr, -1);
		if (tPtr->visibleTabs < tPtr->itemCount)
			tPtr->flags.dontFitAll = 1;
		else
			tPtr->flags.dontFitAll = 0;
	} else {
		for (i = 0; i < tPtr->itemCount; i++) {
			char *str = WMGetTabViewItemLabel(tPtr->items[i]);
			if (!str)
				continue;

			width = WMWidthOfString(tPtr->font, str, strlen(str)) + 30;

			W_SetTabViewItemTabWidth(tPtr->items[i], width);
		}

		if (countVisibleTabs(tPtr, -1) < tPtr->itemCount) {
			tPtr->flags.dontFitAll = 1;
			tPtr->firstVisible = 0;
			tPtr->visibleTabs = countVisibleTabs(tPtr, tPtr->firstVisible);
		} else {
			tPtr->flags.dontFitAll = 0;
			tPtr->firstVisible = 0;
			tPtr->visibleTabs = tPtr->itemCount;
		}
	}
}

static void tLine(cairo_t *cr, WMColor *c, int x1, int y1, int x2, int y2)
{
	W_SetSourceColor(cr, c);
	cairo_move_to(cr, x1 + 0.5, y1 + 0.5);
	cairo_line_to(cr, x2 + 0.5, y2 + 0.5);
	cairo_stroke(cr);
}

/* WRRaised without the top line, that row belongs to the tab strip */
static void drawRelief(cairo_t *cr, W_Screen *scr, int x, int y,
		       unsigned int width, unsigned int height)
{
	tLine(cr, scr->white, x, y, x, y + height - 1);

	tLine(cr, scr->black, x, y + height - 1, x + width - 1, y + height - 1);
	tLine(cr, scr->darkGray, x + 1, y + height - 2, x + width - 2, y + height - 2);

	tLine(cr, scr->black, x + width - 1, y, x + width - 1, y + height - 1);
	tLine(cr, scr->darkGray, x + width - 2, y + 1, x + width - 2, y + height - 2);
}

static void drawTab(TabView *tPtr, cairo_t *cr, int x, int y,
		    unsigned int width, unsigned int height, bool selected)
{
	WMScreen *scr = W_VIEW(tPtr)->screen;
	WMColor *hi = selected ? scr->white : tPtr->lightGray;
	double tx[8], ty[8];
	int k;

	tx[0] = x + (selected ? 0 : 1);
	ty[0] = y + height - (selected ? 0 : 1);
	tx[1] = x + 3;             ty[1] = y + height - 3;
	tx[2] = x + 7;             ty[2] = y + 3;
	tx[3] = x + 10;            ty[3] = y;
	tx[4] = x + width - 10;    ty[4] = y;
	tx[5] = x + width - 7;     ty[5] = y + 3;
	tx[6] = x + width - 3;     ty[6] = y + height - 3;
	tx[7] = x + width - (selected ? 0 : 1);
	ty[7] = y + height - (selected ? 0 : 1);

	W_SetSourceColor(cr, selected ? scr->gray : tPtr->tabColor);
	cairo_move_to(cr, tx[0], ty[0]);
	for (k = 1; k < 8; k++)
		cairo_line_to(cr, tx[k], ty[k]);
	cairo_close_path(cr);
	cairo_fill(cr);

	W_SetSourceColor(cr, hi);
	cairo_move_to(cr, tx[0] + 0.5, ty[0] + 0.5);
	for (k = 1; k <= 4; k++)
		cairo_line_to(cr, tx[k] + 0.5, ty[k] + 0.5);
	cairo_stroke(cr);

	tLine(cr, scr->darkGray, tx[4], ty[4], tx[5], ty[5]);

	W_SetSourceColor(cr, scr->black);
	cairo_move_to(cr, tx[5] + 0.5, ty[5] + 0.5);
	cairo_line_to(cr, tx[6] + 0.5, ty[6] + 0.5);
	cairo_line_to(cr, tx[7] + 0.5, ty[7] + 0.5);
	cairo_stroke(cr);

	tLine(cr, selected ? scr->gray : scr->white, tx[0], ty[0], tx[7], ty[7]);
}

static void paintDot(cairo_t *cr, W_Screen *scr, int x, int y)
{
	W_SetSourceColor(cr, scr->black);
	cairo_rectangle(cr, x, y, 2, 2);
	cairo_fill(cr);
	W_SetSourceColor(cr, scr->white);
	cairo_rectangle(cr, x, y, 1, 1);
	cairo_fill(cr);
}

static void paintTabView(TabView * tPtr)
{
	WMScreen *scr = W_VIEW(tPtr)->screen;
	cairo_t *cr;
	int i;

	cr = WCairoBeginForDrawable(scr, W_VIEW(tPtr)->window);
	if (!cr)
		return;
	cairo_set_line_width(cr, 1.0);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);

	if (tPtr->flags.type == WTTopTabsBevelBorder) {
		int count = tPtr->visibleTabs;
		int first = tPtr->firstVisible;
		int moreAtLeft;
		int moreAtRight;
		int selectedIsVisible;
		int ty;
		int twidth, theight;

		ty = 2;
		theight = tPtr->tabHeight;

		W_SetSourceColor(cr, W_VIEW(tPtr)->backColor);
		cairo_rectangle(cr, 0, 0, W_VIEW(tPtr)->size.width, theight);
		cairo_fill(cr);

		if (tPtr->flags.dontFitAll) {
			moreAtLeft = first > 0;
			moreAtRight = (first + count) < tPtr->itemCount;
			if (tPtr->selectedItem >= first && tPtr->selectedItem < first + count)
				selectedIsVisible = 1;
			else
				selectedIsVisible = 0;
		} else {
			moreAtLeft = 0;
			moreAtRight = 0;
			selectedIsVisible = 1;
		}

		if (moreAtRight) {
			drawTab(tPtr, cr, positionOfTab(tPtr, first + count), 0,
				W_VIEW_WIDTH(tPtr->view), theight, false);
		}
		for (i = first + count - 1; i >= first; i--) {
			if (!selectedIsVisible || i != tPtr->selectedItem) {
				twidth = W_TabViewItemTabWidth(tPtr->items[i]);

				drawTab(tPtr, cr, positionOfTab(tPtr, i), 0, twidth, theight, false);
			}
		}
		if (moreAtLeft) {
			drawTab(tPtr, cr, positionOfTab(tPtr, 0) - 2 * BUTTONED_SIDE_OFFSET,
				0, BUTTONED_SIDE_OFFSET * 4, theight, false);
		}

		if (selectedIsVisible) {
			int idx = tPtr->selectedItem;

			drawTab(tPtr, cr, positionOfTab(tPtr, idx),
				0, W_TabViewItemTabWidth(tPtr->items[idx]), theight, true);

			tLine(cr, scr->white, 0, theight - 1,
			      positionOfTab(tPtr, idx), theight - 1);
			tLine(cr, scr->white,
			      positionOfTab(tPtr, idx) + W_TabViewItemTabWidth(tPtr->items[idx]),
			      theight - 1, W_VIEW_WIDTH(tPtr->view) - 1, theight - 1);
		} else {
			tLine(cr, scr->white, 0, theight - 1,
			      W_VIEW_WIDTH(tPtr->view), theight - 1);
		}

		for (i = 0; i < count; i++) {
			WMRect rect;

			rect.pos.x = 15 + positionOfTab(tPtr, first + i);
			rect.pos.y = ty;
			rect.size.width = W_TabViewItemTabWidth(tPtr->items[first + i]);
			rect.size.height = theight;
			W_DrawLabel(tPtr->items[first + i], cr, rect,
				    tPtr->flags.enabled && tPtr->items[first + i]->flags.enabled);
		}

		if (moreAtLeft) {
			paintDot(cr, scr, 4, 10);
			paintDot(cr, scr, 7, 10);
			paintDot(cr, scr, 10, 10);
		}
		if (moreAtRight) {
			int x;

			x = positionOfTab(tPtr, tPtr->firstVisible + tPtr->visibleTabs);

			x = x + (W_VIEW_WIDTH(tPtr->view) - x) / 2;
			paintDot(cr, scr, x + 5, 10);
			paintDot(cr, scr, x + 8, 10);
			paintDot(cr, scr, x + 11, 10);
		}
	}
	switch (tPtr->flags.type) {
	case WTTopTabsBevelBorder:
		drawRelief(cr, scr, 0, tPtr->tabHeight - 1,
			   W_VIEW(tPtr)->size.width, W_VIEW(tPtr)->size.height - tPtr->tabHeight + 1);
		break;

	case WTNoTabsBevelBorder:
		W_DrawRelief(cr, scr, 0, 0, W_VIEW(tPtr)->size.width,
				  W_VIEW(tPtr)->size.height, WRRaised);
		break;

	case WTNoTabsLineBorder:
		W_DrawRelief(cr, scr, 0, 0, W_VIEW(tPtr)->size.width,
				  W_VIEW(tPtr)->size.height, WRSimple);
		break;

	case WTNoTabsNoBorder:
		break;
	}

	WCairoEnd(cr);
}

static void rearrange(TabView * tPtr)
{
	int i;
	int width, height;
	int bordered = tPtr->flags.bordered;

	recalcTabWidth(tPtr);

	width = tPtr->view->size.width - (bordered ? 3 : 0);
	height = tPtr->view->size.height - tPtr->tabHeight - (bordered ? 3 : 0);

	for (i = 0; i < tPtr->itemCount; i++) {
		W_MoveView(W_TabViewItemView(tPtr->items[i]), 1 * bordered, tPtr->tabHeight + 1 * bordered);
		W_ResizeView(W_TabViewItemView(tPtr->items[i]), width, height);
	}
	if (W_VIEW_MAPPED(tPtr->view) && W_VIEW_REALIZED(tPtr->view))
		paintTabView(tPtr);
}

static void didResize(struct W_ViewDelegate *deleg, WMView * view)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) deleg;

	rearrange(view->self);
}

static void destroyTabView(TabView * tPtr)
{
	int i;

	for (i = 0; i < tPtr->itemCount; i++) {
		WMSetTabViewItemView(tPtr->items[i], NULL);
		WMDestroyTabViewItem(tPtr->items[i]);
	}
	wfree(tPtr->items);

	WMReleaseColor(tPtr->lightGray);
	WMReleaseColor(tPtr->tabColor);
	WMReleaseFont(tPtr->font);

	wfree(tPtr);
}

/******************************************************************/

static void W_SetTabViewItemParent(WMTabViewItem * item, WMTabView * parent)
{
	item->tabView = parent;
}

static void W_DrawLabel(WMTabViewItem *item, cairo_t *cr, WMRect rect, bool enabled)
{
	WMScreen *scr = W_VIEW(item->tabView)->screen;

	if (!item->label)
		return;

	W_DrawString(cr, enabled ? scr->black : scr->darkGray,
			  item->tabView->font, rect.pos.x, rect.pos.y,
			  item->label, strlen(item->label));
}

static void W_UnmapTabViewItem(WMTabViewItem * item)
{
	wassertr(item->view);

	W_UnmapView(item->view);

	item->flags.visible = 0;
}

static void W_MapTabViewItem(WMTabViewItem * item)
{
	wassertr(item->view);

	W_MapView(item->view);
	W_RaiseView(item->view);

	item->flags.visible = 1;
}

static WMView *W_TabViewItemView(WMTabViewItem * item)
{
	return item->view;
}

static int W_TabViewItemTabWidth(WMTabViewItem * item)
{
	return item->tabWidth;
}

static void W_SetTabViewItemTabWidth(WMTabViewItem * item, int width)
{
	item->tabWidth = width;
}

WMTabViewItem *WMCreateTabViewItemWithIdentifier(int identifier)
{
	WMTabViewItem *item;

	item = wmalloc(sizeof(WMTabViewItem));
	item->identifier = identifier;
	item->flags.enabled = 1;

	return item;
}

WMTabViewItem *WMCreateTabViewItem(int identifier, const char *label)
{
	WMTabViewItem *item;

	item = wmalloc(sizeof(WMTabViewItem));
	item->identifier = identifier;
	item->flags.enabled = 1;

	WMSetTabViewItemLabel(item, label);

	return item;
}

void WMSetTabViewItemEnabled(WMTabViewItem * tPtr, bool flag)
{
	tPtr->flags.enabled = ((flag == 0) ? 0 : 1);
	if (tPtr->tabView && W_VIEW_REALIZED(tPtr->tabView->view))
		paintTabView(tPtr->tabView);
}

int WMGetTabViewItemIdentifier(WMTabViewItem * item)
{
	return item->identifier;
}

void WMSetTabViewFont(WMTabView * tPtr, WMFont * font)
{
	if (tPtr->font)
		WMReleaseFont(tPtr->font);

	tPtr->font = WMRetainFont(font);
	tPtr->tabHeight = WMFontHeight(tPtr->font) + 3;
	recalcTabWidth(tPtr);
}

void WMSetTabViewItemLabel(WMTabViewItem * item, const char *label)
{
	if (item->label)
		wfree(item->label);

	if (label)
		item->label = wstrdup(label);
	else
		item->label = NULL;

	if (item->tabView)
		recalcTabWidth(item->tabView);
}

char *WMGetTabViewItemLabel(WMTabViewItem * item)
{
	return item->label;
}

void WMSetTabViewItemView(WMTabViewItem * item, WMView * view)
{
	item->view = view;
}

WMView *WMGetTabViewItemView(WMTabViewItem * item)
{
	return item->view;
}

void WMDestroyTabViewItem(WMTabViewItem * item)
{
	if (item->label)
		wfree(item->label);

	if (item->view)
		W_DestroyView(item->view);

	wfree(item);
}
