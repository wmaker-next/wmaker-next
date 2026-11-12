/*
 *  wcolorwell.c
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 1998 scottc
 *  Copyright (c) 1999-2004 Dan Pascu
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

#include <cairo.h>

#include "WINGsP.h"

#define XDND_COLOR_DATA_TYPE "application/X-color"

const char *WMColorWellDidChangeNotification = "WMColorWellDidChangeNotification";

typedef struct W_ColorWell {
	W_Class widgetClass;
	WMView *view;

	WMView *colorView;

	WMColor *color;

	WMAction *action;
	void *clientData;

	WMPoint ipoint;

	struct {
		unsigned int active:1;
		unsigned int bordered:1;
	} flags;

	WMArray *xdndTypes;
} ColorWell;

static char *_ColorWellActivatedNotification = "_ColorWellActivatedNotification";

static void destroyColorWell(ColorWell * cPtr);
static void paintColorWell(ColorWell * cPtr);

static void handleEvents(xcb_generic_event_t * event, void *data);

static void handleDragEvents(xcb_generic_event_t * event, void *data);

static void handleActionEvents(xcb_generic_event_t * event, void *data);

static void willResizeColorWell(W_ViewDelegate * self, WMView * view, unsigned int *width, unsigned int *height);

W_ViewDelegate _ColorWellViewDelegate = {
	NULL,
	NULL,
	NULL,
	NULL,
	willResizeColorWell
};

static WMArray *dropDataTypes(WMView * self);
static WMDragOperationType wantedDropOperation(WMView * self);
static bool acceptDropOperation(WMView * self, WMDragOperationType operation);
static WMData *fetchDragData(WMView * self, char *type);

static WMDragSourceProcs _DragSourceProcs = {
	dropDataTypes,
	wantedDropOperation,
	NULL,
	acceptDropOperation,
	NULL,
	NULL,
	fetchDragData
};

static WMArray *requiredDataTypes(WMView * self,
				  WMDragOperationType requestedOperation, WMArray * sourceDataTypes);
static WMDragOperationType allowedOperation(WMView * self,
					    WMDragOperationType requestedOperation, WMArray * sourceDataTypes);
static void performDragOperation(WMView * self, WMArray * dropDatas,
				 WMArray * operationsList, WMPoint * dropLocation);

static WMDragDestinationProcs _DragDestinationProcs = {
	NULL,
	requiredDataTypes,
	allowedOperation,
	NULL,
	performDragOperation,
	NULL
};

#define DEFAULT_WIDTH		60
#define DEFAULT_HEIGHT		30

#define MIN_WIDTH	16
#define MIN_HEIGHT	8

static void colorChangedObserver(void *data, WMNotification * notification)
{
	WMColorPanel *panel = (WMColorPanel *) WMGetNotificationObject(notification);
	WMColorWell *cPtr = (WMColorWell *) data;
	WMColor *color;

	if (!cPtr->flags.active)
		return;

	color = WMGetColorPanelColor(panel);

	WMSetColorWellColor(cPtr, color);
	WMPostNotificationName(WMColorWellDidChangeNotification, cPtr, NULL);
}

static void colorPanelClosedObserver(void *data, WMNotification *notification)
{
	WMColorWell *cPtr = (WMColorWell *) data;
	W_Screen *scr = cPtr->view->screen;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) notification;

	if (!cPtr->flags.active)
		return;

	cPtr->flags.active = 0;
	W_SetViewBackgroundColor(cPtr->view, scr->gray);
	if (cPtr->view->flags.realized && cPtr->view->flags.mapped)
		paintColorWell(cPtr);
}

static void updateColorCallback(void *self, void *data)
{
	WMColorPanel *panel = (WMColorPanel *) self;
	WMColorWell *cPtr = (ColorWell *) data;
	WMColor *color;

	color = WMGetColorPanelColor(panel);
	WMSetColorWellColor(cPtr, color);
	WMPostNotificationName(WMColorWellDidChangeNotification, cPtr, NULL);
}

static WMArray *getXdndTypeArray(void)
{
	WMArray *types = WMCreateArray(1);
	WMAddToArray(types, XDND_COLOR_DATA_TYPE);
	return types;
}

WMColorWell *WMCreateColorWell(WMWidget * parent)
{
	ColorWell *cPtr;

	cPtr = wmalloc(sizeof(ColorWell));

	cPtr->widgetClass = WC_ColorWell;

	cPtr->view = W_CreateView(W_VIEW(parent));
	if (!cPtr->view) {
		wfree(cPtr);
		return NULL;
	}
	cPtr->view->self = cPtr;

	cPtr->view->delegate = &_ColorWellViewDelegate;

	cPtr->colorView = W_CreateView(cPtr->view);
	if (!cPtr->colorView) {
		W_DestroyView(cPtr->view);
		wfree(cPtr);
		return NULL;
	}
	cPtr->colorView->self = cPtr;

	WMCreateEventHandler(cPtr->view, XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY
			     | ClientMessageMask, handleEvents, cPtr);

	WMCreateEventHandler(cPtr->colorView, XCB_EVENT_MASK_EXPOSURE, handleEvents, cPtr);

	WMCreateDragHandler(cPtr->colorView, handleDragEvents, cPtr);

	WMCreateEventHandler(cPtr->view, XCB_EVENT_MASK_BUTTON_PRESS, handleActionEvents, cPtr);
	WMCreateEventHandler(cPtr->colorView, XCB_EVENT_MASK_BUTTON_PRESS, handleActionEvents, cPtr);

	cPtr->colorView->flags.mapWhenRealized = 1;

	cPtr->flags.bordered = 1;

	W_ResizeView(cPtr->view, DEFAULT_WIDTH, DEFAULT_HEIGHT);

	cPtr->color = WMBlackColor(WMWidgetScreen(cPtr));

	WMAddNotificationObserver(colorChangedObserver, cPtr, WMColorPanelColorChangedNotification, NULL);
	WMAddNotificationObserver(colorPanelClosedObserver, cPtr, WMColorPanelClosedNotification, NULL);

	WMSetViewDragSourceProcs(cPtr->colorView, &_DragSourceProcs);
	WMSetViewDragDestinationProcs(cPtr->colorView, &_DragDestinationProcs);

	cPtr->xdndTypes = getXdndTypeArray();
	WMRegisterViewForDraggedTypes(cPtr->colorView, cPtr->xdndTypes);

	return cPtr;
}

void WMSetColorWellColor(WMColorWell * cPtr, WMColor * color)
{
	if (cPtr->color && cPtr->color != color) {
		WMReleaseColor(cPtr->color);
		cPtr->color = WMRetainColor(color);
	}

	if (cPtr->colorView->flags.realized && cPtr->colorView->flags.mapped)
		paintColorWell(cPtr);
}

WMColor *WMGetColorWellColor(WMColorWell * cPtr)
{
	return cPtr->color;
}

void WSetColorWellBordered(WMColorWell * cPtr, bool flag)
{
	flag = ((flag == 0) ? 0 : 1);
	if (cPtr->flags.bordered != flag) {
		cPtr->flags.bordered = flag;
		W_ResizeView(cPtr->view, cPtr->view->size.width, cPtr->view->size.height);
	}
}

static void willResizeColorWell(W_ViewDelegate * self, WMView * view, unsigned int *width, unsigned int *height)
{
	WMColorWell *cPtr = (WMColorWell *) view->self;
	int bw;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;

	if (cPtr->flags.bordered) {

		if (*width < MIN_WIDTH)
			*width = MIN_WIDTH;
		if (*height < MIN_HEIGHT)
			*height = MIN_HEIGHT;

		bw = (int)((float)WMIN(*width, *height) * 0.24F);

		W_ResizeView(cPtr->colorView, *width - 2 * bw, *height - 2 * bw);

		if (cPtr->colorView->pos.x != bw || cPtr->colorView->pos.y != bw)
			W_MoveView(cPtr->colorView, bw, bw);
	} else {
		W_ResizeView(cPtr->colorView, *width, *height);

		W_MoveView(cPtr->colorView, 0, 0);
	}
}

static void paintColorWell(ColorWell * cPtr)
{
	W_Screen *scr = cPtr->view->screen;
	cairo_t *cr;

	cr = WCairoBeginForDrawable(scr, cPtr->view->window);
	if (cr) {
		W_DrawRelief(cr, scr, 0, 0, cPtr->view->size.width, cPtr->view->size.height, WRRaised);
		WCairoEnd(cr);
	}

	cr = WCairoBeginForDrawable(scr, cPtr->colorView->window);
	if (cr) {
		W_DrawRelief(cr, scr, 0, 0,
				  cPtr->colorView->size.width, cPtr->colorView->size.height, WRSunken);
		WCairoEnd(cr);
	}

	if (cPtr->color)
		WMPaintColorSwatch(cPtr->color, cPtr->colorView->window,
				   2, 2, cPtr->colorView->size.width - 4, cPtr->colorView->size.height - 4);
}

static void handleEvents(xcb_generic_event_t * event, void *data)
{
	ColorWell *cPtr = (ColorWell *) data;

	CHECK_CLASS(data, WC_ColorWell);

	switch ((event->response_type & 0x7f)) {
	case XCB_EXPOSE:
		if (((xcb_expose_event_t *)event)->count != 0)
			break;
		paintColorWell(cPtr);
		break;

	case XCB_DESTROY_NOTIFY:
		destroyColorWell(cPtr);
		break;

	}
}

static WMArray *dropDataTypes(WMView * self)
{
	return ((ColorWell *) self->self)->xdndTypes;
}

static WMDragOperationType wantedDropOperation(WMView * self)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;

	return WDOperationCopy;
}

static bool acceptDropOperation(WMView * self, WMDragOperationType operation)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;

	return (operation == WDOperationCopy);
}

static WMData *fetchDragData(WMView * self, char *type)
{
	char *color = WMGetColorRGBDescription(((WMColorWell *) self->self)->color);
	WMData *data;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) type;

	data = WMCreateDataWithBytes(color, strlen(color) + 1);
	wfree(color);

	return data;
}

static WMPixmap *makeDragPixmap(WMColorWell * cPtr)
{
	WMScreen *scr = cPtr->view->screen;
	xcb_pixmap_t pix;
	cairo_t *cr;
	int d;

	pix = W_CreateScaledPixmap(scr, 16, 16);
	d = (int)(16 * scr->uiScale + 0.5);

	cr = WCairoBeginForDrawable(scr, pix);
	W_SetSourceColor(cr, cPtr->color);
	cairo_paint(cr);
	W_SetSourceColor(cr, scr->black);
	cairo_set_line_width(cr, 1.0);
	cairo_rectangle(cr, 0.5, 0.5, 15, 15);
	cairo_stroke(cr);
	WCairoEnd(cr);

	/* the drag follower window is created at pixmap->width x height
	 * with this as its background_pixmap, so hand back device size */
	return WMCreatePixmapFromXPixmaps(scr, pix, XCB_NONE, d, d, scr->depth);
}

static void handleDragEvents(xcb_generic_event_t * event, void *data)
{
	WMColorWell *cPtr = (ColorWell *) data;

	if ((event->response_type & 0x7f) == XCB_BUTTON_PRESS && ((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_1) {
		/* initialise drag icon */
		WMSetViewDragImage(cPtr->colorView, makeDragPixmap(cPtr));
	}

	WMDragImageFromView(cPtr->colorView, event);
}

static void handleActionEvents(xcb_generic_event_t * event, void *data)
{
	WMColorWell *cPtr = (ColorWell *) data;
	WMScreen *scr = WMWidgetScreen(cPtr);
	WMColorPanel *cpanel;
	W_View *top;
	WMWidget *topWidget;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) event;

	if (cPtr->flags.active)
		W_SetViewBackgroundColor(cPtr->view, scr->gray);
	else
		W_SetViewBackgroundColor(cPtr->view, scr->white);
	paintColorWell(cPtr);

	cPtr->flags.active ^= 1;

	if (cPtr->flags.active) {
		WMPostNotificationName(_ColorWellActivatedNotification, cPtr, NULL);
	}
	cpanel = WMGetColorPanel(scr);

	top = W_TopLevelOfView(cPtr->view);
	topWidget = top ? WMWidgetOfView(top) : NULL;
	if (topWidget && WMWidgetClass(topWidget) == WC_Window)
		WMSetColorPanelOwner(cpanel, (WMWindow *) topWidget);

	WMSetColorPanelAction(cpanel, updateColorCallback, cPtr);

	if (cPtr->color)
		WMSetColorPanelColor(cpanel, cPtr->color);
	WMShowColorPanel(cpanel);
}

static void destroyColorWell(ColorWell * cPtr)
{
	WMScreen *scr = WMWidgetScreen(cPtr);
	WMColorPanel *cpanel = WMGetExistingColorPanel(scr);
	WMWindow *owner = NULL;
	W_View *top;

	WMRemoveNotificationObserver(cPtr);

	if (cpanel) {
		top = W_TopLevelOfView(cPtr->view);
		if (top && top->flags.alreadyDead) {
			WMWidget *topWidget = WMWidgetOfView(top);

			if (topWidget && WMWidgetClass(topWidget) == WC_Window)
				owner = (WMWindow *) topWidget;
		}
		W_DetachColorWellFromPanel(cpanel, cPtr, owner);
	}

	if (cPtr->color)
		WMReleaseColor(cPtr->color);

	WMFreeArray(cPtr->xdndTypes);

	wfree(cPtr);
}

static bool dropIsOk(WMDragOperationType request, WMArray * sourceDataTypes)
{
	WMArrayIterator iter;
	char *type;

	if (request == WDOperationCopy) {
		WM_ITERATE_ARRAY(sourceDataTypes, type, iter) {
			if (type != NULL && strcmp(type, XDND_COLOR_DATA_TYPE) == 0) {
				return true;
			}
		}
	}

	return false;
}

static WMArray *requiredDataTypes(WMView * self, WMDragOperationType request, WMArray * sourceDataTypes)
{
	if (dropIsOk(request, sourceDataTypes))
		return ((ColorWell *) self->self)->xdndTypes;
	else
		return NULL;
}

static WMDragOperationType allowedOperation(WMView * self, WMDragOperationType request, WMArray * sourceDataTypes)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;

	if (dropIsOk(request, sourceDataTypes))
		return WDOperationCopy;
	else
		return WDOperationNone;
}

static void performDragOperation(WMView * self, WMArray * dropData, WMArray * operations, WMPoint * dropLocation)
{
	char *colorName;
	WMColor *color;
	WMData *data;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) operations;
	(void) dropLocation;

	/* only one operation requested (WDOperationCopy) implies only one data */
	data = (WMData *) WMGetFromArray(dropData, 0);

	if (data != NULL) {
		colorName = (char *)WMDataBytes(data);
		color = WMCreateNamedColor(W_VIEW_SCREEN(self), colorName, true);
		if (color) {
			WMSetColorWellColor(self->self, color);
			WMReleaseColor(color);
		}
	}
}
