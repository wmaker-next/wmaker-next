/*
 *  wlist.c
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 1998 scottc
 *  Copyright (c) 1999-2002 Dan Pascu
 *  Copyright (c) 1999-2000 Alfredo K. Kojima
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

#include <cairo-xcb.h>

#include "WINGsP.h"
#include <ctype.h>
#include <strings.h>

const char *WMListDidScrollNotification = "WMListDidScrollNotification";
const char *WMListSelectionDidChangeNotification = "WMListSelectionDidChangeNotification";

typedef struct W_List {
	W_Class widgetClass;
	W_View *view;

	WMArray *items;		/* list of WMListItem */
	WMArray *selectedItems;	/* list of selected WMListItems */
	WMColor *selColor;

	short itemHeight;

	int topItem;		/* index of first visible item */

	short fullFitLines;	/* no of lines that fit entirely */

	void *clientData;
	WMAction *action;
	void *doubleClientData;
	WMAction *doubleAction;

	WMListDrawProc *draw;

	WMHandlerID idleID;	/* for updating the scroller after adding elements */

	WMHandlerID selectID;	/* for selecting items in list while scrolling */
	WMHandlerID typeaheadID;	/* for clearing typeahead buffer */

	WMScroller *vScroller;

	xcb_pixmap_t doubleBuffer;

	char *typeahead;
	int typeaheadLen;

	int lastClicked;	/* anchor for double-click and range selection */
	int prevItem;		/* last item seen by the motion handler */

	struct {
		unsigned int allowMultipleSelection:1;
		unsigned int allowEmptySelection:1;
		unsigned int userDrawn:1;
		unsigned int userItemHeight:1;
		unsigned int dontFitAll:1;	/* 1 = last item won't be fully visible */
		unsigned int redrawPending:1;
		unsigned int buttonPressed:1;
		unsigned int buttonWasPressed:1;
		unsigned int doubleClick:1;	/* press fired doubleAction */
	} flags;
} List;

#define DEFAULT_WIDTH	150
#define DEFAULT_HEIGHT	150

#define SCROLL_DELAY    100
#define TYPEAHEAD_CLEAR_DELAY 700

/* the list view is a scaled window, so xcb_clear_area needs logical
 * units converted to device pixels
 */
static int listDevice(W_Screen *scr, int v)
{
	return (int)(v * scr->uiScale + 0.5);
}

static void destroyList(List * lPtr);
static void paintList(List * lPtr);

static void handleEvents(xcb_generic_event_t * event, void *data);
static void handleActionEvents(xcb_generic_event_t * event, void *data);

static void updateScroller(void *data);
static void scrollForwardSelecting(void *data);
static void scrollBackwardSelecting(void *data);

static void vScrollCallBack(WMWidget * scroller, void *self);

static void toggleItemSelection(WMList * lPtr, int index);
static void jumpToFirstItemWithPrefix(WMList * lPtr, const char *prefix, int prefixLen);
static void typeaheadTimeout(void *data);

static void updateGeometry(WMList * lPtr);
static void didResizeList(W_ViewDelegate * self, WMView * view);

static void unselectAllListItems(WMList * lPtr, WMListItem * exceptThis);

static W_ViewDelegate _ListViewDelegate = {
	NULL,
	NULL,
	didResizeList,
	NULL,
	NULL
};

static void updateDoubleBufferPixmap(WMList * lPtr)
{
	WMView *view = lPtr->view;
	WMScreen *scr = view->screen;

	if (!view->flags.realized)
		return;

	if (lPtr->doubleBuffer) {
		WMScreenAbandonDrawable(scr, lPtr->doubleBuffer);
		xcb_free_pixmap(scr->display, lPtr->doubleBuffer);
	}
	lPtr->doubleBuffer = W_CreateScaledPixmap(scr, view->size.width, lPtr->itemHeight);
}

static void realizeObserver(void *self, WMNotification * not)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) not;

	updateDoubleBufferPixmap(self);
}

static void releaseItem(void *data)
{
	WMListItem *item = (WMListItem *) data;

	if (item->text)
		wfree(item->text);
	wfree(item);
}

WMList *WMCreateList(WMWidget * parent)
{
	List *lPtr;
	W_Screen *scrPtr = W_VIEW(parent)->screen;

	lPtr = wmalloc(sizeof(List));
	lPtr->typeahead = NULL;
	lPtr->typeaheadLen = 0;
	lPtr->typeaheadID = NULL;
	lPtr->lastClicked = -1;
	lPtr->prevItem = -1;

	lPtr->widgetClass = WC_List;

	lPtr->view = W_CreateView(W_VIEW(parent));
	if (!lPtr->view) {
		wfree(lPtr);
		return NULL;
	}
	lPtr->view->self = lPtr;

	lPtr->view->delegate = &_ListViewDelegate;

	WMCreateEventHandler(lPtr->view, XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY
			     | ClientMessageMask, handleEvents, lPtr);

	WMCreateEventHandler(lPtr->view, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE
			     | XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_BUTTON_MOTION
			     | XCB_EVENT_MASK_KEY_PRESS, handleActionEvents, lPtr);

	lPtr->itemHeight = WMFontHeight(scrPtr->normalFont) + 1;

	lPtr->items = WMCreateArrayWithDestructor(4, releaseItem);
	lPtr->selectedItems = WMCreateArray(4);
	lPtr->selColor = W_SelectedBackColor(scrPtr);

	/* create the vertical scroller */
	lPtr->vScroller = WMCreateScroller(lPtr);
	WMMoveWidget(lPtr->vScroller, 1, 1);
	WMSetScrollerArrowsPosition(lPtr->vScroller, WSAMaxEnd);

	WMSetScrollerAction(lPtr->vScroller, vScrollCallBack, lPtr);

	/* make the scroller map itself when it's realized */
	WMMapWidget(lPtr->vScroller);

	W_ResizeView(lPtr->view, DEFAULT_WIDTH, DEFAULT_HEIGHT);

	WMAddNotificationObserver(realizeObserver, lPtr, WMViewRealizedNotification, lPtr->view);

	return lPtr;
}

void WMSetListAllowMultipleSelection(WMList * lPtr, bool flag)
{
	lPtr->flags.allowMultipleSelection = ((flag == 0) ? 0 : 1);
}

void WMSetListAllowEmptySelection(WMList * lPtr, bool flag)
{
	lPtr->flags.allowEmptySelection = ((flag == 0) ? 0 : 1);
}

static int comparator(const void *a, const void *b)
{
	return (strcmp((*(WMListItem **) a)->text, (*(WMListItem **) b)->text));
}

void WMSortListItems(WMList * lPtr)
{
	WMSortArray(lPtr->items, comparator);

	paintList(lPtr);
}

void WMSortListItemsWithComparer(WMList * lPtr, WMCompareDataProc * func)
{
	WMSortArray(lPtr->items, func);

	paintList(lPtr);
}

WMListItem *WMInsertListItem(WMList * lPtr, int row, const char *text)
{
	WMListItem *item;

	CHECK_CLASS(lPtr, WC_List);

	item = wmalloc(sizeof(WMListItem));
	item->text = wstrdup(text);

	row = WMIN(row, WMGetArrayItemCount(lPtr->items));

	if (row < 0)
		WMAddToArray(lPtr->items, item);
	else
		WMInsertInArray(lPtr->items, row, item);

	/* update the scroller when idle, so that we don't waste time
	 * updating it when another item is going to be added later */
	if (!lPtr->idleID) {
		lPtr->idleID = WMAddIdleHandler((WMCallback *) updateScroller, lPtr);
	}

	return item;
}

void WMRemoveListItem(WMList * lPtr, int row)
{
	WMListItem *item;
	int topItem = lPtr->topItem;
	int selNotify = 0;

	CHECK_CLASS(lPtr, WC_List);

	/*wassertr(row>=0 && row<WMGetArrayItemCount(lPtr->items)); */
	if (row < 0 || row >= WMGetArrayItemCount(lPtr->items))
		return;

	item = WMGetFromArray(lPtr->items, row);
	if (item->selected) {
		WMRemoveFromArray(lPtr->selectedItems, item);
		selNotify = 1;
	}

	if (row <= lPtr->topItem + lPtr->fullFitLines + lPtr->flags.dontFitAll)
		lPtr->topItem--;
	if (lPtr->topItem < 0)
		lPtr->topItem = 0;

	WMDeleteFromArray(lPtr->items, row);

	if (!lPtr->idleID) {
		lPtr->idleID = WMAddIdleHandler((WMCallback *) updateScroller, lPtr);
	}
	if (lPtr->topItem != topItem) {
		WMPostNotificationName(WMListDidScrollNotification, lPtr, NULL);
	}
	if (selNotify) {
		WMPostNotificationName(WMListSelectionDidChangeNotification, lPtr, NULL);
	}
}

WMListItem *WMGetListItem(WMList * lPtr, int row)
{
	return WMGetFromArray(lPtr->items, row);
}

WMArray *WMGetListItems(WMList * lPtr)
{
	return lPtr->items;
}

void WMSetListUserDrawProc(WMList * lPtr, WMListDrawProc * proc)
{
	lPtr->flags.userDrawn = 1;
	lPtr->draw = proc;
}

WMColor *WMGetListSelectionColor(WMList *lPtr)
{
	return lPtr->selColor;
}

void WMSetListUserDrawItemHeight(WMList * lPtr, unsigned short height)
{
	assert(height > 0);

	lPtr->flags.userItemHeight = 1;
	lPtr->itemHeight = height;

	updateDoubleBufferPixmap(lPtr);

	updateGeometry(lPtr);
}

void WMClearList(WMList * lPtr)
{
	int selNo = WMGetArrayItemCount(lPtr->selectedItems);

	WMEmptyArray(lPtr->selectedItems);
	WMEmptyArray(lPtr->items);

	lPtr->topItem = 0;

	if (lPtr->idleID) {
		WMDeleteIdleHandler(lPtr->idleID);
		lPtr->idleID = NULL;
	}
	WMDeleteTimerHandlerPtr(&lPtr->selectID);
	WMDeleteTimerHandlerPtr(&lPtr->typeaheadID);
	if (lPtr->typeahead) {
		lPtr->typeahead[0] = '\0';
		lPtr->typeaheadLen = 0;
	}
	if (lPtr->view->flags.realized) {
		updateScroller(lPtr);
	}
	if (selNo > 0) {
		WMPostNotificationName(WMListSelectionDidChangeNotification, lPtr, NULL);
	}
}

void WMSetListAction(WMList * lPtr, WMAction * action, void *clientData)
{
	lPtr->action = action;
	lPtr->clientData = clientData;
}

void WMSetListDoubleAction(WMList * lPtr, WMAction * action, void *clientData)
{
	lPtr->doubleAction = action;
	lPtr->doubleClientData = clientData;
}

WMArray *WMGetListSelectedItems(WMList * lPtr)
{
	return lPtr->selectedItems;
}

WMListItem *WMGetListSelectedItem(WMList * lPtr)
{
	return WMGetFromArray(lPtr->selectedItems, 0);
}

int WMGetListSelectedItemRow(WMList * lPtr)
{
	WMListItem *item = WMGetFromArray(lPtr->selectedItems, 0);

	return (item != NULL ? WMGetFirstInArray(lPtr->items, item) : WLNotFound);
}

int WMGetListItemHeight(WMList * lPtr)
{
	return lPtr->itemHeight;
}

void WMSetListPosition(WMList * lPtr, int row)
{
	lPtr->topItem = row;
	if (lPtr->topItem + lPtr->fullFitLines > WMGetArrayItemCount(lPtr->items))
		lPtr->topItem = WMGetArrayItemCount(lPtr->items) - lPtr->fullFitLines;

	if (lPtr->topItem < 0)
		lPtr->topItem = 0;

	if (lPtr->view->flags.realized)
		updateScroller(lPtr);
}

void WMSetListBottomPosition(WMList * lPtr, int row)
{
	if (WMGetArrayItemCount(lPtr->items) > lPtr->fullFitLines) {
		lPtr->topItem = row - lPtr->fullFitLines;
		if (lPtr->topItem < 0)
			lPtr->topItem = 0;
		if (lPtr->view->flags.realized)
			updateScroller(lPtr);
	}
}

int WMGetListNumberOfRows(WMList * lPtr)
{
	return WMGetArrayItemCount(lPtr->items);
}

int WMGetListPosition(WMList * lPtr)
{
	return lPtr->topItem;
}

bool WMListAllowsMultipleSelection(WMList * lPtr)
{
	return lPtr->flags.allowMultipleSelection;
}

bool WMListAllowsEmptySelection(WMList * lPtr)
{
	return lPtr->flags.allowEmptySelection;
}

static void scrollByAmount(WMList * lPtr, int amount)
{
	int itemCount = WMGetArrayItemCount(lPtr->items);

	if ((amount < 0 && lPtr->topItem > 0) || (amount > 0 && (lPtr->topItem + lPtr->fullFitLines < itemCount))) {

		lPtr->topItem += amount;
		if (lPtr->topItem < 0)
			lPtr->topItem = 0;
		if (lPtr->topItem + lPtr->fullFitLines > itemCount)
			lPtr->topItem = itemCount - lPtr->fullFitLines;

		updateScroller(lPtr);
	}
}

static void vScrollCallBack(WMWidget * scroller, void *self)
{
	WMList *lPtr = (WMList *) self;
	int oldTopItem = lPtr->topItem;
	int itemCount = WMGetArrayItemCount(lPtr->items);

	switch (WMGetScrollerHitPart((WMScroller *) scroller)) {
	case WSDecrementLine:
		scrollByAmount(lPtr, -1);
		break;

	case WSIncrementLine:
		scrollByAmount(lPtr, 1);
		break;

	case WSDecrementPage:
		scrollByAmount(lPtr, -lPtr->fullFitLines + (1 - lPtr->flags.dontFitAll) + 1);
		break;

	case WSIncrementPage:
		scrollByAmount(lPtr, lPtr->fullFitLines - (1 - lPtr->flags.dontFitAll) - 1);
		break;

	case WSDecrementWheel:
		scrollByAmount(lPtr, -lPtr->fullFitLines / 3);
		break;

	case WSIncrementWheel:
		scrollByAmount(lPtr, lPtr->fullFitLines / 3);
		break;

	case WSKnob:
		lPtr->topItem = WMGetScrollerValue(lPtr->vScroller) * (float)(itemCount - lPtr->fullFitLines);

		if (oldTopItem != lPtr->topItem)
			paintList(lPtr);	/* use updateScroller(lPtr) here? -Dan */
		break;

	case WSKnobSlot:
	case WSNoPart:
	default:
		/* do nothing */
		break;
	}

	if (lPtr->topItem != oldTopItem)
		WMPostNotificationName(WMListDidScrollNotification, lPtr, NULL);
}

static void paintItem(List * lPtr, int index)
{
	WMView *view = lPtr->view;
	W_Screen *scr = view->screen;
	int width, height, x, y, tlen;
	WMListItem *itemPtr;
	xcb_drawable_t d = lPtr->doubleBuffer;
	WMColor *back;
	WMRect rect;
	int flags;
	cairo_t *cr;

	itemPtr = WMGetFromArray(lPtr->items, index);

	width = lPtr->view->size.width - 2 - 19;
	height = lPtr->itemHeight;
	x = 19;
	y = 2 + (index - lPtr->topItem) * lPtr->itemHeight + 1;
	tlen = strlen(itemPtr->text);

	if (lPtr->flags.userDrawn) {
		rect.size.width = width;
		rect.size.height = height;
		rect.pos.x = 0;
		rect.pos.y = 0;

		flags = itemPtr->uflags;
		if (itemPtr->disabled)
			flags |= WLDSDisabled;
		if (itemPtr->selected)
			flags |= WLDSSelected;
		if (itemPtr->isBranch)
			flags |= WLDSIsBranch;

		if (lPtr->draw)
			(*lPtr->draw) (lPtr, index, d, itemPtr->text, flags, &rect);
	} else {
		back = (itemPtr->selected ? lPtr->selColor : view->backColor);
		cr = WCairoBeginForDrawable(scr, d);
		W_SetSourceColor(cr, back);
		cairo_rectangle(cr, 0, 0, width, height);
		cairo_fill(cr);
		W_PaintText(cr, scr->normalFont, 4, 0, width, WALeft,
				 scr->black, false, itemPtr->text, tlen);
		WCairoEnd(cr);
	}

	/* the buffer is view->size.width wide but only the row area right
	 * of the scroller strip is copied onto the on-screen window */
	cr = WCairoBeginForDrawable(scr, view->window);
	W_PaintArea(cr, scr, d, view->size.width, height,
			0, 0, width, height, x, y);

	if ((index - lPtr->topItem + lPtr->fullFitLines) * lPtr->itemHeight > lPtr->view->size.height - 2)
		W_DrawRelief(cr, scr, 0, 0,
				  lPtr->view->size.width, lPtr->view->size.height, WRSunken);
	WCairoEnd(cr);
}

static void paintList(List * lPtr)
{
	W_Screen *scrPtr = lPtr->view->screen;
	int i, lim, itemCount;
	cairo_t *cr;

	if (!lPtr->view->flags.mapped)
		return;

	itemCount = WMGetArrayItemCount(lPtr->items);
	if (itemCount > 0) {
		if (lPtr->topItem + lPtr->fullFitLines + lPtr->flags.dontFitAll > itemCount) {

			lim = itemCount - lPtr->topItem;
			xcb_clear_area(scrPtr->display, false, lPtr->view->window,
				       listDevice(scrPtr, 19),
				       listDevice(scrPtr, 2 + lim * lPtr->itemHeight),
				       listDevice(scrPtr, lPtr->view->size.width - 21),
				       listDevice(scrPtr, lPtr->view->size.height
						  - lim * lPtr->itemHeight - 3));
		} else {
			lim = lPtr->fullFitLines + lPtr->flags.dontFitAll;
		}
		for (i = lPtr->topItem; i < lPtr->topItem + lim; i++) {
			paintItem(lPtr, i);
		}
	} else {
		xcb_clear_area(scrPtr->display, 0, lPtr->view->window, 0, 0, 0, 0);
	}
	cr = WCairoBeginForDrawable(scrPtr, lPtr->view->window);
	if (!cr)
		return;
	W_DrawRelief(cr, scrPtr, 0, 0, lPtr->view->size.width, lPtr->view->size.height, WRSunken);
	WCairoEnd(cr);
}

#if 0
static void scrollTo(List * lPtr, int newTop)
{

}
#endif

static void updateScroller(void *data)
{
	List *lPtr = (List *) data;

	float knobProportion, floatValue, tmp;
	int count = WMGetArrayItemCount(lPtr->items);

	if (lPtr->idleID)
		WMDeleteIdleHandler(lPtr->idleID);
	lPtr->idleID = NULL;

	paintList(lPtr);

	if (count == 0 || count <= lPtr->fullFitLines)
		WMSetScrollerParameters(lPtr->vScroller, 0, 1);
	else {
		tmp = lPtr->fullFitLines;
		knobProportion = tmp / (float)count;

		floatValue = (float)lPtr->topItem / (float)(count - lPtr->fullFitLines);

		WMSetScrollerParameters(lPtr->vScroller, floatValue, knobProportion);
	}
}

static void scrollForwardSelecting(void *data)
{
	List *lPtr = (List *) data;
	int lastSelected;

	lastSelected = lPtr->topItem + lPtr->fullFitLines + lPtr->flags.dontFitAll - 1;

	if (lastSelected >= WMGetArrayItemCount(lPtr->items) - 1) {
		lPtr->selectID = NULL;
		if (lPtr->flags.dontFitAll)
			scrollByAmount(lPtr, 1);
		return;
	}

	/* selecting NEEDS to be done before scrolling to avoid flickering */
	if (lPtr->flags.allowMultipleSelection) {
		WMListItem *item;
		WMRange range;

		item = WMGetFromArray(lPtr->selectedItems, 0);
		range.position = WMGetFirstInArray(lPtr->items, item);
		if (lastSelected + 1 >= range.position) {
			range.count = lastSelected - range.position + 2;
		} else {
			range.count = lastSelected - range.position;
		}
		WMSetListSelectionToRange(lPtr, range);
	} else {
		WMSelectListItem(lPtr, lastSelected + 1);
	}
	scrollByAmount(lPtr, 1);

	lPtr->selectID = WMAddTimerHandler(SCROLL_DELAY, scrollForwardSelecting, lPtr);
}

static void scrollBackwardSelecting(void *data)
{
	List *lPtr = (List *) data;

	if (lPtr->topItem < 1) {
		lPtr->selectID = NULL;
		return;
	}

	/* selecting NEEDS to be done before scrolling to avoid flickering */
	if (lPtr->flags.allowMultipleSelection) {
		WMListItem *item;
		WMRange range;

		item = WMGetFromArray(lPtr->selectedItems, 0);
		range.position = WMGetFirstInArray(lPtr->items, item);
		if (lPtr->topItem - 1 >= range.position) {
			range.count = lPtr->topItem - range.position;
		} else {
			range.count = lPtr->topItem - range.position - 2;
		}
		WMSetListSelectionToRange(lPtr, range);
	} else {
		WMSelectListItem(lPtr, lPtr->topItem - 1);
	}
	scrollByAmount(lPtr, -1);

	lPtr->selectID = WMAddTimerHandler(SCROLL_DELAY, scrollBackwardSelecting, lPtr);
}

static void handleEvents(xcb_generic_event_t * event, void *data)
{
	List *lPtr = (List *) data;

	CHECK_CLASS(data, WC_List);

	switch ((event->response_type & 0x7f)) {
	case XCB_EXPOSE:
		if (((xcb_expose_event_t *)event)->count != 0)
			break;
		paintList(lPtr);
		break;

	case XCB_DESTROY_NOTIFY:
		destroyList(lPtr);
		break;

	}
}

static int matchTitle(const void *item, const void *title)
{
	const WMListItem *wl_item = item;
	const char *s_title = title;

	return (strcmp(wl_item->text, s_title) == 0 ? 1 : 0);
}

int WMFindRowOfListItemWithTitle(WMList * lPtr, const char *title)
{
	/*
	 * We explicitely discard the 'const' attribute here because the
	 * call-back function handler must not be made with a const
	 * attribute, but our local call-back function (above) does have
	 * it properly set, so we're consistent
	 */
	return WMFindInArray(lPtr->items, matchTitle, (char *) title);
}

void WMSelectListItem(WMList * lPtr, int row)
{
	WMListItem *item;

	if (row >= WMGetArrayItemCount(lPtr->items))
		return;

	if (row < 0) {
		/* row = -1 will deselects all for backward compatibility.
		 * will be removed later. -Dan */
		WMUnselectAllListItems(lPtr);
		return;
	}

	item = WMGetFromArray(lPtr->items, row);
	if (item->selected)
		return;		/* Return if already selected */

	if (!lPtr->flags.allowMultipleSelection) {
		/* unselect previous selected items */
		unselectAllListItems(lPtr, NULL);
	}

	/* select item */
	item->selected = 1;
	WMAddToArray(lPtr->selectedItems, item);

	if (lPtr->view->flags.mapped && row >= lPtr->topItem && row <= lPtr->topItem + lPtr->fullFitLines) {
		paintItem(lPtr, row);
	}

	WMPostNotificationName(WMListSelectionDidChangeNotification, lPtr, NULL);
}

void WMUnselectListItem(WMList * lPtr, int row)
{
	WMListItem *item = WMGetFromArray(lPtr->items, row);

	if (!item || !item->selected)
		return;

	if (!lPtr->flags.allowEmptySelection && WMGetArrayItemCount(lPtr->selectedItems) <= 1) {
		return;
	}

	item->selected = 0;
	WMRemoveFromArray(lPtr->selectedItems, item);

	if (lPtr->view->flags.mapped && row >= lPtr->topItem && row <= lPtr->topItem + lPtr->fullFitLines) {
		paintItem(lPtr, row);
	}

	WMPostNotificationName(WMListSelectionDidChangeNotification, lPtr, NULL);
}

void WMSelectListItemsInRange(WMList * lPtr, WMRange range)
{
	WMListItem *item;
	int position = range.position, k = 1, notify = 0;
	int total = WMGetArrayItemCount(lPtr->items);

	if (!lPtr->flags.allowMultipleSelection)
		return;
	if (range.count == 0)
		return;		/* Nothing to select */

	if (range.count < 0) {
		range.count = -range.count;
		k = -1;
	}

	for (; range.count > 0 && position >= 0 && position < total; range.count--) {
		item = WMGetFromArray(lPtr->items, position);
		if (!item->selected) {
			item->selected = 1;
			WMAddToArray(lPtr->selectedItems, item);
			if (lPtr->view->flags.mapped && position >= lPtr->topItem
			    && position <= lPtr->topItem + lPtr->fullFitLines) {
				paintItem(lPtr, position);
			}
			notify = 1;
		}
		position += k;
	}

	if (notify) {
		WMPostNotificationName(WMListSelectionDidChangeNotification, lPtr, NULL);
	}
}

void WMSetListSelectionToRange(WMList * lPtr, WMRange range)
{
	WMListItem *item;
	int mark1, mark2, i, k;
	int position = range.position, notify = 0;
	int total = WMGetArrayItemCount(lPtr->items);

	if (!lPtr->flags.allowMultipleSelection)
		return;

	if (range.count == 0) {
		WMUnselectAllListItems(lPtr);
		return;
	}

	if (range.count < 0) {
		mark1 = range.position + range.count + 1;
		mark2 = range.position + 1;
		range.count = -range.count;
		k = -1;
	} else {
		mark1 = range.position;
		mark2 = range.position + range.count;
		k = 1;
	}
	if (mark1 > total)
		mark1 = total;
	if (mark2 < 0)
		mark2 = 0;

	WMEmptyArray(lPtr->selectedItems);

	for (i = 0; i < mark1; i++) {
		item = WMGetFromArray(lPtr->items, i);
		if (item->selected) {
			item->selected = 0;
			if (lPtr->view->flags.mapped && i >= lPtr->topItem
			    && i <= lPtr->topItem + lPtr->fullFitLines) {
				paintItem(lPtr, i);
			}
			notify = 1;
		}
	}
	for (; range.count > 0 && position >= 0 && position < total; range.count--) {
		item = WMGetFromArray(lPtr->items, position);
		if (!item->selected) {
			item->selected = 1;
			if (lPtr->view->flags.mapped && position >= lPtr->topItem
			    && position <= lPtr->topItem + lPtr->fullFitLines) {
				paintItem(lPtr, position);
			}
			notify = 1;
		}
		WMAddToArray(lPtr->selectedItems, item);
		position += k;
	}
	for (i = mark2; i < total; i++) {
		item = WMGetFromArray(lPtr->items, i);
		if (item->selected) {
			item->selected = 0;
			if (lPtr->view->flags.mapped && i >= lPtr->topItem
			    && i <= lPtr->topItem + lPtr->fullFitLines) {
				paintItem(lPtr, i);
			}
			notify = 1;
		}
	}

	if (notify) {
		WMPostNotificationName(WMListSelectionDidChangeNotification, lPtr, NULL);
	}
}

void WMSelectAllListItems(WMList * lPtr)
{
	int i, itemCount;
	WMListItem *item;

	if (!lPtr->flags.allowMultipleSelection)
		return;

	if (WMGetArrayItemCount(lPtr->items) == WMGetArrayItemCount(lPtr->selectedItems)) {
		return;		/* All items are selected already */
	}

	WMFreeArray(lPtr->selectedItems);
	lPtr->selectedItems = WMCreateArrayWithArray(lPtr->items);

	itemCount = WMGetArrayItemCount(lPtr->items);
	for (i = 0; i < itemCount; i++) {
		item = WMGetFromArray(lPtr->items, i);
		if (!item->selected) {
			item->selected = 1;
			if (lPtr->view->flags.mapped && i >= lPtr->topItem
			    && i <= lPtr->topItem + lPtr->fullFitLines) {
				paintItem(lPtr, i);
			}
		}
	}

	WMPostNotificationName(WMListSelectionDidChangeNotification, lPtr, NULL);
}

/*
 * Be careful from where you call this function! It doesn't honor the
 * allowEmptySelection flag and doesn't send a notification about selection
 * change! You need to manage these in the functions from where you call it.
 *
 * This will unselect all items if exceptThis is NULL, else will keep
 * exceptThis selected.
 * Make sure that exceptThis is one of the already selected items if not NULL!
 *
 */
static void unselectAllListItems(WMList * lPtr, WMListItem * exceptThis)
{
	int i, itemCount;
	WMListItem *item;

	itemCount = WMGetArrayItemCount(lPtr->items);
	for (i = 0; i < itemCount; i++) {
		item = WMGetFromArray(lPtr->items, i);
		if (item != exceptThis && item->selected) {
			item->selected = 0;
			if (lPtr->view->flags.mapped && i >= lPtr->topItem
			    && i <= lPtr->topItem + lPtr->fullFitLines) {
				paintItem(lPtr, i);
			}
		}
	}

	WMEmptyArray(lPtr->selectedItems);
	if (exceptThis != NULL) {
		exceptThis->selected = 1;
		WMAddToArray(lPtr->selectedItems, exceptThis);
	}
}

void WMUnselectAllListItems(WMList * lPtr)
{
	int keep;
	WMListItem *keepItem;

	keep = lPtr->flags.allowEmptySelection ? 0 : 1;

	if (WMGetArrayItemCount(lPtr->selectedItems) == keep)
		return;

	keepItem = (keep == 1 ? WMGetFromArray(lPtr->selectedItems, 0) : NULL);

	unselectAllListItems(lPtr, keepItem);

	WMPostNotificationName(WMListSelectionDidChangeNotification, lPtr, NULL);
}

static int getItemIndexAt(List * lPtr, int clickY)
{
	int index;

	index = (clickY - 2) / lPtr->itemHeight + lPtr->topItem;

	if (index < 0 || index >= WMGetArrayItemCount(lPtr->items))
		return -1;

	return index;
}

static void toggleItemSelection(WMList * lPtr, int index)
{
	WMListItem *item = WMGetFromArray(lPtr->items, index);

	if (item && item->selected) {
		WMUnselectListItem(lPtr, index);
	} else {
		WMSelectListItem(lPtr, index);
	}
}

static int findItemWithPrefix(List * lPtr, const char *prefix, int prefixLen)
{
	if (prefixLen <= 0 || !prefix)
		return -1;

	int i, itemCount;

	itemCount = WMGetArrayItemCount(lPtr->items);
	for (i = 0; i < itemCount; i++) {
		WMListItem *item = WMGetFromArray(lPtr->items, i);

		if (!item || !item->text || item->text[0] == '\0')
			continue;
		if (strncasecmp(item->text, prefix, prefixLen) == 0)
			return i;
	}

	return -1;
}

static void jumpToFirstItemWithPrefix(WMList * lPtr, const char *prefix, int prefixLen)
{
	int index, oldTop, visibleCount;

	index = findItemWithPrefix(lPtr, prefix, prefixLen);
	if (index < 0)
		return;

	if (lPtr->flags.allowMultipleSelection) {
		WMRange range;

		range.position = index;
		range.count = 1;
		WMSetListSelectionToRange(lPtr, range);
	} else {
		WMSelectListItem(lPtr, index);
		/* Trigger action callback */
		if (lPtr->action)
			(*lPtr->action) (lPtr, lPtr->clientData);
	}

	visibleCount = lPtr->fullFitLines + lPtr->flags.dontFitAll;
	if (visibleCount < 1)
		visibleCount = 1;

	oldTop = lPtr->topItem;
	if (index < lPtr->topItem) {
		lPtr->topItem = index;
	} else {
		if (lPtr->flags.dontFitAll) {
			if (lPtr->fullFitLines <= 0) {
				lPtr->topItem = index;
			} else {
				int lastFullyVisible = lPtr->topItem + lPtr->fullFitLines - 1;
				if (index > lastFullyVisible)
					lPtr->topItem = index - lPtr->fullFitLines + 1;
			}
		} else if (index >= lPtr->topItem + visibleCount) {
			lPtr->topItem = index - visibleCount + 1;
		}
	}
	if (lPtr->topItem < 0)
		lPtr->topItem = 0;

	if (lPtr->view->flags.realized && lPtr->topItem != oldTop)
		updateScroller(lPtr);
}

static void typeaheadTimeout(void *data)
{
	List *lPtr = (List *) data;

	lPtr->typeaheadID = NULL;
	if (lPtr->typeahead) {
		lPtr->typeahead[0] = '\0';
		lPtr->typeaheadLen = 0;
	}
}

static void handleActionEvents(xcb_generic_event_t * event, void *data)
{
	List *lPtr = (List *) data;
	WMScreen *scr = lPtr->view->screen;
	int tmp, height;
	int topItem = lPtr->topItem;
	char buffer[16];
	xkb_keysym_t ksym;
	WXLookupStatus status;
	int len;
	WXWindowAttributes wattr;

	CHECK_CLASS(data, WC_List);

	switch ((event->response_type & 0x7f)) {
	case XCB_BUTTON_RELEASE:
		/* Ignore mouse wheel events, they're not "real" button events */
		if (((xcb_button_press_event_t *)event)->detail == WINGsConfiguration.mouseWheelUp ||
		    ((xcb_button_press_event_t *)event)->detail == WINGsConfiguration.mouseWheelDown) {
			break;
		}

		lPtr->flags.buttonPressed = 0;
		WMDeleteTimerHandlerPtr(&lPtr->selectID);
		tmp = getItemIndexAt(lPtr, ((xcb_button_press_event_t *)event)->event_y);

		if (tmp >= 0 && !lPtr->flags.doubleClick) {
			if (lPtr->action)
				(*lPtr->action) (lPtr, lPtr->clientData);
		}
		lPtr->flags.doubleClick = 0;

		if (!(((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_SHIFT))
			lPtr->lastClicked = lPtr->prevItem = tmp;

		break;

	case XCB_ENTER_NOTIFY:
		WMDeleteTimerHandlerPtr(&lPtr->selectID);
		WMSetFocusToWidget(lPtr);
		break;

	case XCB_LEAVE_NOTIFY:
		height = WMWidgetHeight(lPtr);
		if (lPtr->flags.buttonPressed && !lPtr->selectID) {
			if (((xcb_enter_notify_event_t *)event)->event_y >= height) {
				lPtr->selectID = WMAddTimerHandler(SCROLL_DELAY, scrollForwardSelecting, lPtr);
			} else if (((xcb_enter_notify_event_t *)event)->event_y <= 0) {
				lPtr->selectID = WMAddTimerHandler(SCROLL_DELAY, scrollBackwardSelecting, lPtr);
			}
		}
		WMWidget *parentWidget = WMWidgetOfView(lPtr->view->parent);
		if (parentWidget)
			WMSetFocusToWidget(parentWidget);
		break;

	case XCB_BUTTON_PRESS:
		if (((xcb_button_press_event_t *)event)->event_x <= WMWidgetWidth(lPtr->vScroller))
			break;
		if (((xcb_button_press_event_t *)event)->detail == WINGsConfiguration.mouseWheelDown ||
		    ((xcb_button_press_event_t *)event)->detail == WINGsConfiguration.mouseWheelUp) {
			int amount = 0;

			if (((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_CONTROL) {
				amount = lPtr->fullFitLines - (1 - lPtr->flags.dontFitAll) - 1;
			} else if (((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_SHIFT) {
				amount = 1;
			} else {
				amount = lPtr->fullFitLines / 3;
				if (amount == 0)
					amount++;
			}
			if (((xcb_button_press_event_t *)event)->detail == WINGsConfiguration.mouseWheelUp)
				amount = -amount;

			scrollByAmount(lPtr, amount);
			break;
		}

		tmp = getItemIndexAt(lPtr, ((xcb_button_press_event_t *)event)->event_y);
		lPtr->flags.buttonPressed = 1;
		lPtr->flags.doubleClick = 0;

		if (tmp >= 0) {
			if (tmp == lPtr->lastClicked && WMIsDoubleClick(event)) {
				WMSelectListItem(lPtr, tmp);
				lPtr->flags.doubleClick = 1;
				if (lPtr->doubleAction)
					(*lPtr->doubleAction) (lPtr, lPtr->doubleClientData);
			} else {
				if (!lPtr->flags.allowMultipleSelection) {
					if (((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_CONTROL) {
						toggleItemSelection(lPtr, tmp);
					} else {
						WMSelectListItem(lPtr, tmp);
					}
				} else {
					WMRange range;
					WMListItem *lastSel;

					if (((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_CONTROL) {
						toggleItemSelection(lPtr, tmp);
					} else if (((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_SHIFT) {
						if (WMGetArrayItemCount(lPtr->selectedItems) == 0) {
							WMSelectListItem(lPtr, tmp);
						} else {
							lastSel = WMGetFromArray(lPtr->items, lPtr->lastClicked);
							range.position = WMGetFirstInArray(lPtr->items, lastSel);
							if (tmp >= range.position)
								range.count = tmp - range.position + 1;
							else
								range.count = tmp - range.position - 1;

							WMSetListSelectionToRange(lPtr, range);
						}
					} else {
						range.position = tmp;
						range.count = 1;
						WMSetListSelectionToRange(lPtr, range);
					}
				}
			}
		}

		if (!(((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_SHIFT))
			lPtr->lastClicked = lPtr->prevItem = tmp;

		break;

	case XCB_MOTION_NOTIFY:
		height = WMWidgetHeight(lPtr);
		if (lPtr->selectID && ((xcb_motion_notify_event_t *)event)->event_y > 0 && ((xcb_motion_notify_event_t *)event)->event_y < height) {
			WMDeleteTimerHandler(lPtr->selectID);
			lPtr->selectID = NULL;
		}
		if (lPtr->flags.buttonPressed && !lPtr->selectID) {
			if (((xcb_motion_notify_event_t *)event)->event_y <= 0) {
				lPtr->selectID = WMAddTimerHandler(SCROLL_DELAY, scrollBackwardSelecting, lPtr);
				break;
			} else if (((xcb_motion_notify_event_t *)event)->event_y >= height) {
				lPtr->selectID = WMAddTimerHandler(SCROLL_DELAY, scrollForwardSelecting, lPtr);
				break;
			}

			tmp = getItemIndexAt(lPtr, ((xcb_motion_notify_event_t *)event)->event_y);
			if (tmp >= 0 && tmp != lPtr->prevItem) {
				if (lPtr->flags.allowMultipleSelection) {
					WMRange range;

					range.position = lPtr->lastClicked;
					if (tmp >= lPtr->lastClicked)
						range.count = tmp - lPtr->lastClicked + 1;
					else
						range.count = tmp - lPtr->lastClicked - 1;
					WMSetListSelectionToRange(lPtr, range);
				} else {
					WMSelectListItem(lPtr, tmp);
				}
			}
			lPtr->prevItem = tmp;
		}
		break;

	case XCB_KEY_PRESS:
		if (((xcb_key_press_event_t *)event)->state & (XCB_MOD_MASK_CONTROL | scr->altModifierMask))
			break;

		if (!(wXGetWindowAttributes(scr->display, lPtr->view->window, &wattr, NULL) && wattr.map_state == XCB_MAP_STATE_VIEWABLE))
			break;

		len = W_LookupString(lPtr->view, (const xcb_key_press_event_t *)event, buffer, (int)sizeof(buffer) - 1, &ksym, &status);
		if (len < 0)
			break;
		if (len > (int)sizeof(buffer) - 1)
			len = (int)sizeof(buffer) - 1;
		if (len > 0)
			buffer[len] = '\0';

		/* Handle navigation keys */
		switch (ksym) {
		case XKB_KEY_Up: {
			int newRow;
			int itemCount = WMGetArrayItemCount(lPtr->items);
			int cur = WMGetListSelectedItemRow(lPtr);

			if (lPtr->flags.allowMultipleSelection && WMGetArrayItemCount(lPtr->selectedItems) > 0) {
				WMListItem *lastSel = WMGetFromArray(lPtr->selectedItems, WMGetArrayItemCount(lPtr->selectedItems) - 1);
				if (lastSel)
					cur = WMGetFirstInArray(lPtr->items, lastSel);
			}

			if (cur == WLNotFound)
				cur = lPtr->topItem;
			newRow = cur - 1;
			if (newRow < 0)
				newRow = 0;

			if (newRow != cur && itemCount > 0) {
				if (lPtr->flags.allowMultipleSelection) {
					if (((xcb_key_press_event_t *)event)->state & XCB_MOD_MASK_SHIFT) {
						WMRange range;
						int anchor = WMGetListSelectedItemRow(lPtr);
						if (anchor == WLNotFound || WMGetArrayItemCount(lPtr->selectedItems) == 0) {
							anchor = cur;
						}

						range.position = anchor;
						if (newRow >= anchor)
							range.count = newRow - anchor + 1;
						else
							range.count = newRow - anchor - 1;
						WMSetListSelectionToRange(lPtr, range);
					} else {
						WMRange range = { .position = newRow, .count = 1 };
						WMSetListSelectionToRange(lPtr, range);
						lPtr->lastClicked = newRow;
					}
				} else {
					WMSelectListItem(lPtr, newRow);
					lPtr->lastClicked = newRow;
				}

				/* Ensure visibility */
				if (newRow < lPtr->topItem) {
					lPtr->topItem = newRow;
					if (lPtr->view->flags.realized)
						updateScroller(lPtr);
				}
				/* Trigger action callback */
				if (lPtr->action)
					(*lPtr->action) (lPtr, lPtr->clientData);
			}
			break;
		}
		case XKB_KEY_Down: {
			int newRow;
			int itemCount = WMGetArrayItemCount(lPtr->items);
			int cur = WMGetListSelectedItemRow(lPtr);

			if (lPtr->flags.allowMultipleSelection && WMGetArrayItemCount(lPtr->selectedItems) > 0) {
				WMListItem *lastSel = WMGetFromArray(lPtr->selectedItems, WMGetArrayItemCount(lPtr->selectedItems) - 1);
				if (lastSel)
					cur = WMGetFirstInArray(lPtr->items, lastSel);
			}

			if (cur == WLNotFound)
				cur = lPtr->topItem;
			newRow = cur + 1;
			if (newRow >= itemCount)
				newRow = itemCount - 1;

			if (newRow != cur && itemCount > 0) {
				if (lPtr->flags.allowMultipleSelection) {
					if (((xcb_key_press_event_t *)event)->state & XCB_MOD_MASK_SHIFT) {
						WMRange range;
						int anchor = WMGetListSelectedItemRow(lPtr);
						if (anchor == WLNotFound || WMGetArrayItemCount(lPtr->selectedItems) == 0) {
							anchor = cur;
						}

						range.position = anchor;
						if (newRow >= anchor)
							range.count = newRow - anchor + 1;
						else
							range.count = newRow - anchor - 1;
						WMSetListSelectionToRange(lPtr, range);
					} else {
						WMRange range = { .position = newRow, .count = 1 };
						WMSetListSelectionToRange(lPtr, range);
						lPtr->lastClicked = newRow;
					}
				} else {
					WMSelectListItem(lPtr, newRow);
					lPtr->lastClicked = newRow;
				}

				/* Ensure visibility */
				if (newRow > (lPtr->topItem + lPtr->fullFitLines - 1)) {
					lPtr->topItem = newRow - lPtr->fullFitLines + 1;
					if (lPtr->topItem < 0)
						lPtr->topItem = 0;
					/* Ensure we don't scroll past the end */
					if (lPtr->topItem + lPtr->fullFitLines > itemCount)
						lPtr->topItem = itemCount - lPtr->fullFitLines;
					if (lPtr->topItem < 0)
						lPtr->topItem = 0;
					if (lPtr->view->flags.realized)
						updateScroller(lPtr);
				}

				/* Trigger action callback */
				if (lPtr->action)
					(*lPtr->action) (lPtr, lPtr->clientData);
			}
			break;
		}
		case XKB_KEY_Page_Up: {
			int newRow;
			int page = lPtr->fullFitLines > 0 ? lPtr->fullFitLines : 1;
			int cur = WMGetListSelectedItemRow(lPtr);

			if (cur == WLNotFound)
				cur = lPtr->topItem;
			newRow = cur - page;
			if (newRow < 0)
				newRow = 0;

			if (newRow != cur) {
				if (lPtr->flags.allowMultipleSelection) {
					if (((xcb_key_press_event_t *)event)->state & XCB_MOD_MASK_SHIFT) {
						WMRange range;
						int anchor = WMGetListSelectedItemRow(lPtr);
						if (anchor == WLNotFound || WMGetArrayItemCount(lPtr->selectedItems) == 0) {
							anchor = cur;
						}

						range.position = anchor;
						if (newRow >= anchor)
							range.count = newRow - anchor + 1;
						else
							range.count = newRow - anchor - 1;
						WMSetListSelectionToRange(lPtr, range);
					} else {
						WMRange range = { .position = newRow, .count = 1 };
						WMSetListSelectionToRange(lPtr, range);
						lPtr->lastClicked = newRow;
					}
				} else {
					WMSelectListItem(lPtr, newRow);
					lPtr->lastClicked = newRow;
				}
				if (newRow < lPtr->topItem) {
					lPtr->topItem = newRow;
					if (lPtr->view->flags.realized)
						updateScroller(lPtr);
				}
				/* Trigger action callback */
				if (lPtr->action)
					(*lPtr->action) (lPtr, lPtr->clientData);
			}
			break;
		}
		case XKB_KEY_Page_Down: {
			int newRow;
			int page = lPtr->fullFitLines > 0 ? lPtr->fullFitLines : 1;
			int itemCount = WMGetArrayItemCount(lPtr->items);
			int cur = WMGetListSelectedItemRow(lPtr);

			if (cur == WLNotFound)
				cur = lPtr->topItem;
			newRow = cur + page;
			if (newRow >= itemCount)
				newRow = itemCount - 1;

			if (newRow != cur && itemCount > 0) {
				if (lPtr->flags.allowMultipleSelection) {
					if (((xcb_key_press_event_t *)event)->state & XCB_MOD_MASK_SHIFT) {
						WMRange range;
						int anchor = WMGetListSelectedItemRow(lPtr);
						if (anchor == WLNotFound || WMGetArrayItemCount(lPtr->selectedItems) == 0) {
							anchor = cur;
						}

						range.position = anchor;
						if (newRow >= anchor)
							range.count = newRow - anchor + 1;
						else
							range.count = newRow - anchor - 1;
						WMSetListSelectionToRange(lPtr, range);
					} else {
						WMRange range = { .position = newRow, .count = 1 };
						WMSetListSelectionToRange(lPtr, range);
						lPtr->lastClicked = newRow;
					}
				} else {
					WMSelectListItem(lPtr, newRow);
					lPtr->lastClicked = newRow;
				}

				/* Ensure visibility */
				if (newRow > (lPtr->topItem + lPtr->fullFitLines - 1)) {
					lPtr->topItem = newRow - lPtr->fullFitLines + 1;
					if (lPtr->topItem < 0)
						lPtr->topItem = 0;
					/* Ensure we don't scroll past the end */
					if (lPtr->topItem + lPtr->fullFitLines > itemCount)
						lPtr->topItem = itemCount - lPtr->fullFitLines;
					if (lPtr->topItem < 0)
						lPtr->topItem = 0;
					if (lPtr->view->flags.realized)
						updateScroller(lPtr);
				}

				/* Trigger action callback */
				if (lPtr->action)
					(*lPtr->action) (lPtr, lPtr->clientData);
			}
			break;
		}
		case XKB_KEY_Home: {
			int itemCount = WMGetArrayItemCount(lPtr->items);
			if (itemCount > 0) {
				int newRow = 0;
				if (lPtr->flags.allowMultipleSelection) {
					if (((xcb_key_press_event_t *)event)->state & XCB_MOD_MASK_SHIFT) {
						WMRange range;
						int anchor = WMGetListSelectedItemRow(lPtr);
						if (anchor == WLNotFound || WMGetArrayItemCount(lPtr->selectedItems) == 0) {
							anchor = lPtr->topItem;
						}

						range.position = anchor;
						if (newRow >= anchor)
							range.count = newRow - anchor + 1;
						else
							range.count = newRow - anchor - 1;
						WMSetListSelectionToRange(lPtr, range);
					} else {
						WMRange range = { .position = newRow, .count = 1 };
						WMSetListSelectionToRange(lPtr, range);
						lPtr->lastClicked = newRow;
					}
				} else {
					WMSelectListItem(lPtr, newRow);
					lPtr->lastClicked = newRow;
				}
				if (newRow < lPtr->topItem) {
					lPtr->topItem = newRow;
					if (lPtr->view->flags.realized)
						updateScroller(lPtr);
				}
				/* Trigger action callback */
				if (lPtr->action)
					(*lPtr->action) (lPtr, lPtr->clientData);
			}
			break;
		}
		case XKB_KEY_End: {
			int itemCount = WMGetArrayItemCount(lPtr->items);
			if (itemCount > 0) {
				int newRow = itemCount - 1;
				if (lPtr->flags.allowMultipleSelection) {
					if (((xcb_key_press_event_t *)event)->state & XCB_MOD_MASK_SHIFT) {
						WMRange range;
						int anchor = WMGetListSelectedItemRow(lPtr);
						if (anchor == WLNotFound || WMGetArrayItemCount(lPtr->selectedItems) == 0) {
							anchor = lPtr->topItem;
						}

						range.position = anchor;
						if (newRow >= anchor)
							range.count = newRow - anchor + 1;
						else
							range.count = newRow - anchor - 1;
						WMSetListSelectionToRange(lPtr, range);
					} else {
						WMRange range = { .position = newRow, .count = 1 };
						WMSetListSelectionToRange(lPtr, range);
						lPtr->lastClicked = newRow;
					}
				} else {
					WMSelectListItem(lPtr, newRow);
					lPtr->lastClicked = newRow;
				}

				/* Ensure the last item is fully visible */
				lPtr->topItem = itemCount - lPtr->fullFitLines;
				if (lPtr->topItem < 0)
					lPtr->topItem = 0;
				if (lPtr->view->flags.realized)
					updateScroller(lPtr);

				/* Trigger action callback */
				if (lPtr->action)
					(*lPtr->action) (lPtr, lPtr->clientData);
			}
			break;
		}
		default:
			break;
		}

		/* If we handled a navigation keysym, avoid falling through to typeahead logic */
		if (ksym == XKB_KEY_Up || ksym == XKB_KEY_Down || ksym == XKB_KEY_Page_Up || ksym == XKB_KEY_Page_Down || ksym == XKB_KEY_Home || ksym == XKB_KEY_End)
			break;

		if (len <= 0)
			break;

		buffer[len] = '\0';

		if (ksym == XKB_KEY_Escape) {
			WMDeleteTimerHandlerPtr(&lPtr->typeaheadID);
			if (lPtr->typeahead) {
				lPtr->typeahead[0] = '\0';
				lPtr->typeaheadLen = 0;
			}
			break;
		}

		if (ksym == XKB_KEY_BackSpace) {
			if (lPtr->typeaheadLen > 0 && lPtr->typeahead) {
				lPtr->typeaheadLen--;
				lPtr->typeahead[lPtr->typeaheadLen] = '\0';
			}
		} else if (len == 1 && isalnum((unsigned char)buffer[0])) {
			if (!lPtr->typeahead) {
				lPtr->typeahead = wmalloc(2);
				lPtr->typeaheadLen = 0;
			}
			lPtr->typeahead = wrealloc(lPtr->typeahead, lPtr->typeaheadLen + 2);
			lPtr->typeahead[lPtr->typeaheadLen] = buffer[0];
			lPtr->typeaheadLen++;
			lPtr->typeahead[lPtr->typeaheadLen] = '\0';
		} else {
			break;
		}

		if (lPtr->typeaheadLen > 0)
			jumpToFirstItemWithPrefix(lPtr, lPtr->typeahead, lPtr->typeaheadLen);

		WMDeleteTimerHandlerPtr(&lPtr->typeaheadID);
		if (lPtr->typeaheadLen > 0)
			lPtr->typeaheadID = WMAddTimerHandler(TYPEAHEAD_CLEAR_DELAY, typeaheadTimeout, lPtr);
		break;

	}
	if (lPtr->topItem != topItem)
		WMPostNotificationName(WMListDidScrollNotification, lPtr, NULL);
}

static void updateGeometry(WMList * lPtr)
{
	lPtr->fullFitLines = (lPtr->view->size.height - 4) / lPtr->itemHeight;
	if (lPtr->fullFitLines * lPtr->itemHeight < lPtr->view->size.height - 4) {
		lPtr->flags.dontFitAll = 1;
	} else {
		lPtr->flags.dontFitAll = 0;
	}

	if (WMGetArrayItemCount(lPtr->items) - lPtr->topItem <= lPtr->fullFitLines) {
		lPtr->topItem = WMGetArrayItemCount(lPtr->items) - lPtr->fullFitLines;
		if (lPtr->topItem < 0)
			lPtr->topItem = 0;
	}

	updateScroller(lPtr);
}

static void didResizeList(W_ViewDelegate * self, WMView * view)
{
	WMList *lPtr = (WMList *) view->self;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;

	WMResizeWidget(lPtr->vScroller, SCROLLER_WIDTH - 1, view->size.height - 2);

	updateDoubleBufferPixmap(lPtr);

	updateGeometry(lPtr);
}

static void destroyList(List * lPtr)
{
	if (lPtr->idleID)
		WMDeleteIdleHandler(lPtr->idleID);
	lPtr->idleID = NULL;

	WMDeleteTimerHandlerPtr(&lPtr->selectID);

	WMDeleteTimerHandlerPtr(&lPtr->typeaheadID);

	if (lPtr->selColor)
		WMReleaseColor(lPtr->selColor);

	if (lPtr->selectedItems)
		WMFreeArray(lPtr->selectedItems);

	if (lPtr->items)
		WMFreeArray(lPtr->items);

	if (lPtr->doubleBuffer) {
		WMScreenAbandonDrawable(lPtr->view->screen, lPtr->doubleBuffer);
		wXFreePixmap(lPtr->view->screen->display, lPtr->doubleBuffer);
	}

	if (lPtr->typeahead)
		wfree(lPtr->typeahead);

	WMRemoveNotificationObserver(lPtr);

	wfree(lPtr);
}
