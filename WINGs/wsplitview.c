/*
 *  wsplitview.c
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

/*
 char *WMSplitViewDidResizeSubviewsNotification
 = "WMSplitViewDidResizeSubviewsNotification";
 char *WMSplitViewWillResizeSubviewsNotification
 = "WMSplitViewWillResizeSubviewsNotification";
 */

typedef struct W_SplitViewSubview {
	WMView *view;
	int minSize;
	int maxSize;
	int size;
	int pos;
} W_SplitViewSubview;

typedef struct W_SplitView {
	W_Class widgetClass;
	W_View *view;

	WMArray *subviews;

	WMSplitViewConstrainProc *constrainProc;

	struct {
		unsigned int vertical:1;
		unsigned int adjustOnPaint:1;
		unsigned int subviewsWereManuallyMoved:1;
	} flags;

	/* WMSplitViewResizeSubviewsProc *resizeSubviewsProc; */

} W_SplitView;

#define DIVIDER_THICKNESS   8
#define MIN_SUBVIEW_SIZE    4
#define MAX_SUBVIEW_SIZE    -1

/* TODO: rewrite --Dan */
#define _GetSubviewsCount() WMGetArrayItemCount(sPtr->subviews)

#define _GetPSubviewStructAt(i) \
    ((W_SplitViewSubview*)WMGetFromArray(sPtr->subviews,(i)))

#define _GetSubviewAt(i) \
    (((W_SplitViewSubview*)WMGetFromArray(sPtr->subviews,(i)))->view)

#define _GetMinSizeAt(i) \
    (((W_SplitViewSubview*)WMGetFromArray(sPtr->subviews,(i)))->minSize)

#define _GetMaxSizeAt(i) \
    (((W_SplitViewSubview*)WMGetFromArray(sPtr->subviews,(i)))->maxSize)

#define _GetSizeAt(i) \
    (((W_SplitViewSubview*)WMGetFromArray(sPtr->subviews,(i)))->size)

#define _GetPosAt(i) \
    (((W_SplitViewSubview*)WMGetFromArray(sPtr->subviews,(i)))->pos)

#define _GetSplitViewSize() \
    ((sPtr->flags.vertical) ? sPtr->view->size.width : sPtr->view->size.height)

static void destroySplitView(WMSplitView * sPtr);
static void paintSplitView(WMSplitView * sPtr);

static void handleEvents(xcb_generic_event_t * event, void *data);
static void handleActionEvents(xcb_generic_event_t * event, void *data);

static void getConstraints(WMSplitView * sPtr, int index, int *minSize, int *maxSize)
{
	*minSize = MIN_SUBVIEW_SIZE;
	*maxSize = MAX_SUBVIEW_SIZE;

	if (sPtr->constrainProc)
		(*sPtr->constrainProc) (sPtr, index, minSize, maxSize);

	if (*minSize < MIN_SUBVIEW_SIZE)
		*minSize = MIN_SUBVIEW_SIZE;

	if (*maxSize < MIN_SUBVIEW_SIZE)
		*maxSize = MAX_SUBVIEW_SIZE;
	else if (*maxSize < *minSize)
		*maxSize = *minSize;
}

static void updateConstraints(WMSplitView * sPtr)
{
	W_SplitViewSubview *p;
	int i, count;

	count = _GetSubviewsCount();
	for (i = 0; i < count; i++) {
		p = _GetPSubviewStructAt(i);
		getConstraints(sPtr, i, &(p->minSize), &(p->maxSize));
	}
}

static void resizeView(WMSplitView * sPtr, WMView * view, int size)
{
	int width, height;

	if (sPtr->flags.vertical) {
		width = size;
		height = sPtr->view->size.height;
	} else {
		width = sPtr->view->size.width;
		height = size;
	}

	if (view->self)
		WMResizeWidget(view->self, width, height);
	else
		W_ResizeView(view, width, height);
}

static void reparentView(WMSplitView * sPtr, WMView * view, int pos)
{
	int x, y;

	if (sPtr->flags.vertical) {
		x = pos;
		y = 0;
	} else {
		x = 0;
		y = pos;
	}

	W_ReparentView(view, sPtr->view, x, y);
}

static void moveView(WMSplitView * sPtr, WMView * view, int pos)
{
	int x, y;

	if (sPtr->flags.vertical) {
		x = pos;
		y = 0;
	} else {
		x = 0;
		y = pos;
	}

	if (view->self)
		WMMoveWidget(view->self, x, y);
	else
		W_MoveView(view, x, y);
}

static int checkSizes(WMSplitView * sPtr)
{
	int i, count, offset;
	W_SplitViewSubview *p;

	count = _GetSubviewsCount();
	offset = 0;
	for (i = 0; i < count; i++) {
		p = _GetPSubviewStructAt(i);
		if (p->size < p->minSize) {
			offset += p->minSize - p->size;
			p->size = p->minSize;
		} else if (p->maxSize != MAX_SUBVIEW_SIZE && p->size > p->maxSize) {
			offset += p->maxSize - p->size;
			p->size = p->maxSize;
		}
	}

	return (offset);
}

static void checkPositions(WMSplitView * sPtr)
{
	int i, count, pos;
	W_SplitViewSubview *p;

	count = _GetSubviewsCount();
	pos = 0;
	for (i = 0; i < count; i++) {
		p = _GetPSubviewStructAt(i);
		p->pos = pos;
		pos += p->size + DIVIDER_THICKNESS;
	}
}

static void updateSubviewsGeom(WMSplitView * sPtr)
{
	int i, count;
	W_SplitViewSubview *p;

	count = _GetSubviewsCount();
	for (i = 0; i < count; i++) {
		p = _GetPSubviewStructAt(i);
		resizeView(sPtr, p->view, p->size);
		moveView(sPtr, p->view, p->pos);
	}
}

static int getTotalSize(WMSplitView * sPtr)
{
	int i, count, totSize;

	count = _GetSubviewsCount();
	if (!count)
		return (0);

	totSize = 0;
	for (i = 0; i < count; i++)
		totSize += _GetSizeAt(i) + DIVIDER_THICKNESS;

	return (totSize - DIVIDER_THICKNESS);
}

static bool distributeOffsetEqually(WMSplitView * sPtr, int offset)
{
	W_SplitViewSubview *p;
	int i, count, sizeChanged, forced;

	if ((count = _GetSubviewsCount()) < 1)
		return (true);

	forced = false;
	while (offset != 0) {
		sizeChanged = 0;
		for (i = 0; i < count && offset != 0; i++) {
			p = _GetPSubviewStructAt(i);
			if (offset < 0) {
				if (p->size > p->minSize) {
					offset++;
					p->size--;
					sizeChanged = 1;
				}
			} else if (p->maxSize == MAX_SUBVIEW_SIZE || p->size < p->maxSize) {
				offset--;
				p->size++;
				sizeChanged = 1;
			}
		}
		if (offset != 0 && !sizeChanged) {
			p = _GetPSubviewStructAt(count - 1);
			if (offset > 0) {
				p->size += offset;
				p->maxSize = MAX_SUBVIEW_SIZE;
			}
			offset = 0;
			forced = true;
		}
	}

	return (forced);
}

static bool distributeOffsetFormEnd(WMSplitView * sPtr, int offset)
{
	W_SplitViewSubview *p;
	int i, count, sizeTmp;

	if ((count = _GetSubviewsCount()) < 1)
		return (true);

	for (i = count - 1; i >= 0 && offset != 0; i--) {
		p = _GetPSubviewStructAt(i);
		sizeTmp = p->size;
		if (offset > 0) {
			if (p->maxSize == MAX_SUBVIEW_SIZE || p->size + offset < p->maxSize)
				p->size += offset;
			else
				p->size = p->maxSize;
		} else {
			if (p->size + offset >= p->minSize)
				p->size += offset;
			else
				p->size = p->minSize;
		}
		offset -= p->size - sizeTmp;
	}

	return (offset == 0);
}

static void adjustSplitViewSubviews(WMSplitView * sPtr)
{
	W_SplitViewSubview *p;
	int i, count, adjSize, adjPad;

	CHECK_CLASS(sPtr, WC_SplitView);

#if 0
	printf("---- (adjustSplitViewSubviews - 1) ----\n");
	dumpSubviews(sPtr);
#endif

	if ((count = _GetSubviewsCount()) < 1)
		return;

	adjSize = (_GetSplitViewSize() - ((count - 1) * DIVIDER_THICKNESS)) / count;
	adjPad = (_GetSplitViewSize() - ((count - 1) * DIVIDER_THICKNESS)) % count;
	for (i = 0; i < count; i++) {
		p = _GetPSubviewStructAt(i);
		p->size = adjSize;
	}

	distributeOffsetEqually(sPtr, adjPad - checkSizes(sPtr));

	checkPositions(sPtr);
	updateSubviewsGeom(sPtr);

	sPtr->flags.subviewsWereManuallyMoved = 0;

#if 0
	printf("---- (adjustSplitViewSubviews - 2) ----\n");
	dumpSubviews(sPtr);
#endif
}

#if 0
static void handleSubviewResized(void *self, WMNotification * notif)
{
	WMSplitView *sPtr = (WMSplitView *) self;

	CHECK_CLASS(sPtr, WC_SplitView);

	if (WMGetNotificationName(notif) == WMViewSizeDidChangeNotification) {
		W_SplitViewSubview *p;
		int i, count, done;
		WMView *view = WMGetNotificationObject(notif);

		count = _GetSubviewsCount();
		done = 0;
		for (i = 0; i < count; i++) {
			p = _GetPSubviewStructAt(i);
			if (p->view == view) {
				done = 1;
				break;
			}
		}

		if (done) {
			/* TODO !!! */
			resizeView(sPtr, p->view, p->size);
			moveView(sPtr, p->view, p->pos);
		}
	}
}
#endif

static void handleViewResized(void *self, WMNotification * notification)
{
	WMSplitView *sPtr = (WMSplitView *) self;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) notification;

#if 0
	printf("---- (handleViewResized - 1) ----\n");
	dumpSubviews(sPtr);
#endif

	updateConstraints(sPtr);
	checkSizes(sPtr);

	if (sPtr->constrainProc || sPtr->flags.subviewsWereManuallyMoved) {
		distributeOffsetFormEnd(sPtr, _GetSplitViewSize() - getTotalSize(sPtr));
		checkPositions(sPtr);
		updateSubviewsGeom(sPtr);
	} else
		adjustSplitViewSubviews(sPtr);

	assert(checkSizes(sPtr) == 0);

#if 0
	printf("---- (handleViewResized - 2) ----\n");
	dumpSubviews(sPtr);
#endif
}

static void paintSplitView(WMSplitView * sPtr)
{
	W_SplitViewSubview *p;
	W_Screen *scr = sPtr->view->screen;
	int x, y, i, count;
	WMPixmap *dimple = scr->scrollerDimple;
	cairo_t *cr;

#if 0
	printf("---- (paintSplitView - 1) ----\n");
	dumpSubviews(sPtr);
#endif

	if (!sPtr->view->flags.mapped || !sPtr->view->flags.realized)
		return;

	xcb_clear_area(scr->display, 0, sPtr->view->window, 0, 0, 0, 0);

	count = _GetSubviewsCount();
	if (count == 0)
		return;

	if (sPtr->flags.adjustOnPaint) {
		handleViewResized(sPtr, NULL);
		sPtr->flags.adjustOnPaint = 0;
	}

	if (sPtr->flags.vertical) {
		x = ((DIVIDER_THICKNESS - dimple->width) / 2);
		y = (sPtr->view->size.height - dimple->height) / 2;
	} else {
		x = (sPtr->view->size.width - dimple->width) / 2;
		y = ((DIVIDER_THICKNESS - dimple->height) / 2);
	}

	cr = WCairoBeginForDrawable(scr, sPtr->view->window);
	for (i = 0; i < count - 1; i++) {
		p = _GetPSubviewStructAt(i);

		if (sPtr->flags.vertical)
			x += p->size;
		else
			y += p->size;

		W_PaintPixmap(cr, dimple, x, y);

		if (sPtr->flags.vertical)
			x += DIVIDER_THICKNESS;
		else
			y += DIVIDER_THICKNESS;
	}
	WCairoEnd(cr);

#if 0
	printf("---- (paintSplitView - 2) ----\n");
	dumpSubviews(sPtr);
#endif
}

/* self-inverting rubber-band strip in the divider gap: DIFFERENCE with
 * a white source gives |1-c| per channel, so painting the same rect
 * twice restores the original pixels just like the previous GXxor GC */
static void drawDragingRectangle(WMSplitView * sPtr, int pos)
{
	cairo_t *cr;
	int x, y, w, h;

	if (sPtr->flags.vertical) {
		x = pos;
		y = 0;
		w = DIVIDER_THICKNESS;
		h = sPtr->view->size.height;
	} else {
		x = 0;
		y = pos;
		w = sPtr->view->size.width;
		h = DIVIDER_THICKNESS;
	}

	cr = WCairoBeginForDrawable(sPtr->view->screen, sPtr->view->window);
	if (!cr)
		return;
	cairo_set_operator(cr, CAIRO_OPERATOR_DIFFERENCE);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_rectangle(cr, x, y, w, h);
	cairo_fill(cr);
	WCairoEnd(cr);
}

static void getMinMaxDividerCoord(WMSplitView * sPtr, int divider, int *minC, int *maxC)
{
	int relMinC, relMaxC;
	int totSize = _GetSizeAt(divider) + _GetSizeAt(divider + 1);

	relMinC = _GetMinSizeAt(divider);
	if (_GetMaxSizeAt(divider + 1) != MAX_SUBVIEW_SIZE && relMinC < totSize - _GetMaxSizeAt(divider + 1))
		relMinC = totSize - _GetMaxSizeAt(divider + 1);

	relMaxC = totSize - _GetMinSizeAt(divider + 1);
	if (_GetMaxSizeAt(divider) != MAX_SUBVIEW_SIZE && relMaxC > _GetMaxSizeAt(divider))
		relMaxC = _GetMaxSizeAt(divider);

	*minC = _GetPosAt(divider) + relMinC;
	*maxC = _GetPosAt(divider) + relMaxC;
}

static void dragDivider(WMSplitView * sPtr, int clickX, int clickY)
{
	int divider, pos, ofs, done, dragging;
	int i, count;
	xcb_generic_event_t *ev = NULL;
	WMScreen *scr;
	int minCoord, maxCoord, coord;

	if (sPtr->constrainProc) {
		updateConstraints(sPtr);
		checkSizes(sPtr);
		distributeOffsetFormEnd(sPtr, _GetSplitViewSize() - getTotalSize(sPtr));
		checkPositions(sPtr);
		updateSubviewsGeom(sPtr);
	}

	scr = sPtr->view->screen;
	divider = ofs = pos = done = 0;
	coord = (sPtr->flags.vertical) ? clickX : clickY;
	count = _GetSubviewsCount();
	if (count < 2)
		return;

	for (i = 0; i < count - 1; i++) {
		pos += _GetSizeAt(i) + DIVIDER_THICKNESS;
		if (coord < pos) {
			ofs = coord - pos + DIVIDER_THICKNESS;
			done = 1;
			break;
		}
		divider++;
	}

	if (!done)
		return;

	getMinMaxDividerCoord(sPtr, divider, &minCoord, &maxCoord);

	/* the modal WMMaskEvent loop dispatches non-motion/release events
	 * via WMHandleEvent; an Expose or other handler may destroy the
	 * split view during the loop, keep the view alive across the loop
	 * so the post-loop resize accesses below do not touch freed memory,
	 * the eventual release runs the destructor which frees the widget */
	W_RetainView(sPtr->view);

	done = 0;
	dragging = 0;
	while (!done) {
		WMMaskEvent(scr->display, XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_EXPOSURE, &ev);
		if (!ev)
			break;

		coord = (sPtr->flags.vertical)
			? ((xcb_motion_notify_event_t *)ev)->event_x
			: ((xcb_motion_notify_event_t *)ev)->event_y;

		switch (ev->response_type & 0x7f) {
		case XCB_BUTTON_RELEASE:
			done = 1;
			if (dragging)
				drawDragingRectangle(sPtr, pos);
			break;

		case XCB_MOTION_NOTIFY:
			if (dragging)
				drawDragingRectangle(sPtr, pos);
			if (coord - ofs < minCoord)
				pos = minCoord;
			else if (coord - ofs > maxCoord)
				pos = maxCoord;
			else
				pos = coord - ofs;
			drawDragingRectangle(sPtr, pos);
			dragging = 1;
			break;

		default:
			WMHandleEvent(ev);
			break;
		}

		free(ev);
		ev = NULL;
	}

	/* the view was retained above, so even if a handler marked it
	 * alreadyDead it is still allocated; bail out before touching the
	 * subview array if a destroy is pending */
	if (sPtr->view->flags.alreadyDead) {
		W_ReleaseView(sPtr->view);
		return;
	}

	if (dragging) {
		W_SplitViewSubview *p1, *p2;
		int totSize;

		p1 = _GetPSubviewStructAt(divider);
		p2 = _GetPSubviewStructAt(divider + 1);

		totSize = p1->size + DIVIDER_THICKNESS + p2->size;

		p1->size = pos - p1->pos;
		p2->size = totSize - p1->size - DIVIDER_THICKNESS;
		p2->pos = p1->pos + p1->size + DIVIDER_THICKNESS;

		resizeView(sPtr, p1->view, p1->size);
		moveView(sPtr, p2->view, p2->pos);
		resizeView(sPtr, p2->view, p2->size);
		sPtr->flags.subviewsWereManuallyMoved = 1;
	}

	W_ReleaseView(sPtr->view);
}

static void handleEvents(xcb_generic_event_t * event, void *data)
{
	WMSplitView *sPtr = (WMSplitView *) data;

	CHECK_CLASS(data, WC_SplitView);

	switch ((event->response_type & 0x7f)) {
	case XCB_EXPOSE:
		if (((xcb_expose_event_t *)event)->count != 0)
			break;
		paintSplitView(sPtr);
		break;

	case XCB_DESTROY_NOTIFY:
		destroySplitView(sPtr);
		break;
	}
}

static void handleActionEvents(xcb_generic_event_t * event, void *data)
{

	CHECK_CLASS(data, WC_SplitView);

	switch ((event->response_type & 0x7f)) {
	case XCB_BUTTON_PRESS:
		if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_1)
			dragDivider(data, ((xcb_button_press_event_t *)event)->event_x, ((xcb_button_press_event_t *)event)->event_y);
		break;
	}
}

static void destroySplitView(WMSplitView * sPtr)
{
	WMFreeArray(sPtr->subviews);

	WMRemoveNotificationObserver(sPtr);

	wfree(sPtr);
}

WMSplitView *WMCreateSplitView(WMWidget * parent)
{
	WMSplitView *sPtr;

	sPtr = wmalloc(sizeof(WMSplitView));
	sPtr->widgetClass = WC_SplitView;

	sPtr->view = W_CreateView(W_VIEW(parent));
	if (!sPtr->view) {
		wfree(sPtr);
		return NULL;
	}
	sPtr->view->self = sPtr;

	WMSetViewNotifySizeChanges(sPtr->view, true);

	WMCreateEventHandler(sPtr->view, XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY
			     | ClientMessageMask, handleEvents, sPtr);

	WMCreateEventHandler(sPtr->view, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE
			     | XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW, handleActionEvents, sPtr);

	WMAddNotificationObserver(handleViewResized, sPtr, WMViewSizeDidChangeNotification, sPtr->view);

	sPtr->subviews = WMCreateArrayWithDestructor(8, wfree);

	return sPtr;
}

void WMAdjustSplitViewSubviews(WMSplitView * sPtr)
{
	CHECK_CLASS(sPtr, WC_SplitView);

	checkSizes(sPtr);

	adjustSplitViewSubviews(sPtr);

	assert(checkSizes(sPtr) == 0);
}

void WMAddSplitViewSubview(WMSplitView * sPtr, WMView * subview)
{
	int wasMapped, count;
	W_SplitViewSubview *p;

	CHECK_CLASS(sPtr, WC_SplitView);

	p = (W_SplitViewSubview *) wmalloc(sizeof(W_SplitViewSubview));
	if (!p)
		return;

	wasMapped = subview->flags.mapped;
	if (wasMapped)
		W_UnmapView(subview);

	count = _GetSubviewsCount();
	p->view = subview;
	getConstraints(sPtr, count, &(p->minSize), &(p->maxSize));
	if (sPtr->flags.vertical)
		p->size = subview->size.width;
	else
		p->size = subview->size.height;

	WMAddToArray(sPtr->subviews, p);
	reparentView(sPtr, subview, 0);

	/*
	   We should have something like that...

	   WMSetViewNotifySizeChanges(subview, True);
	   WMAddNotificationObserver(handleSubviewResized, sPtr,
	   WMViewSizeDidChangeNotification,
	   subview);
	   WMSetViewNotifyMoveChanges(subview, True);
	   WMAddNotificationObserver(handleSubviewResized, sPtr,
	   WMViewMoveDidChangeNotification,
	   subview);
	 */
	if (wasMapped) {
		W_MapView(subview);

		sPtr->flags.adjustOnPaint = 1;
		paintSplitView(sPtr);
	} else {
		handleViewResized(sPtr, NULL);
	}
}

WMView *WMGetSplitViewSubviewAt(WMSplitView * sPtr, int index)
{
	CHECK_CLASS(sPtr, WC_SplitView);

	if (index >= 0 && index < _GetSubviewsCount())
		return (_GetSubviewAt(index));
	else
		return (NULL);
}

void WMRemoveSplitViewSubview(WMSplitView * sPtr, WMView * view)
{
	W_SplitViewSubview *p;
	int i, count;

	CHECK_CLASS(sPtr, WC_SplitView);

	/* TODO: rewrite this. This code with macros is getting more complex than it worths */
	count = _GetSubviewsCount();
	for (i = 0; i < count; i++) {
		p = _GetPSubviewStructAt(i);
		if (p->view == view) {
			WMDeleteFromArray(sPtr->subviews, i);
			sPtr->flags.adjustOnPaint = 1;
			paintSplitView(sPtr);
			break;
		}
	}
}

void WMRemoveSplitViewSubviewAt(WMSplitView * sPtr, int index)
{
	CHECK_CLASS(sPtr, WC_SplitView);

	/* TODO: same about rewrite */
	if (index >= 0 && index < _GetSubviewsCount()) {
		WMDeleteFromArray(sPtr->subviews, index);
		sPtr->flags.adjustOnPaint = 1;
		paintSplitView(sPtr);
	}
}

void WMSetSplitViewConstrainProc(WMSplitView * sPtr, WMSplitViewConstrainProc * proc)
{
	CHECK_CLASS(sPtr, WC_SplitView);

	sPtr->constrainProc = proc;
}

int WMGetSplitViewSubviewsCount(WMSplitView * sPtr)
{
	CHECK_CLASS(sPtr, WC_SplitView);

	return (_GetSubviewsCount());
}

bool WMGetSplitViewVertical(WMSplitView * sPtr)
{
	CHECK_CLASS(sPtr, WC_SplitView);

	return (sPtr->flags.vertical == 1);
}

void WMSetSplitViewVertical(WMSplitView * sPtr, bool flag)
{
	int vertical;

	CHECK_CLASS(sPtr, WC_SplitView);

	vertical = ((flag == 0) ? 0 : 1);
	if (sPtr->flags.vertical == vertical)
		return;

	sPtr->flags.vertical = vertical;

	/*    if (sPtr->view->flags.mapped && sPtr->view->flags.realized) */
	handleViewResized(sPtr, NULL);
	/*    else
	   sPtr->flags.adjustOnPaint = 1;
	 */
}

int WMGetSplitViewDividerThickness(WMSplitView *sPtr)
{
	CHECK_CLASS(sPtr, WC_SplitView);

	(void) sPtr;

	return (DIVIDER_THICKNESS);
}

#if 0
void WMSetSplitViewResizeSubviewsProc(WMSplitView * sPtr, WMSplitViewResizeSubviewsProc * proc)
{
	CHECK_CLASS(sPtr, WC_SplitView);

	sPtr->resizeSubviewsProc = proc;
}
#endif
