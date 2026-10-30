/*
 *  double.c
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1998 scottc
 *  Copyright (c) 1999-2004 Dan Pascu
 *  Copyright (c) 1999 Alfredo K. Kojima
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

/*
 * Widget for testing double-clicks
 *
 */

#include <WINGs/WINGsP.h>
#include "WPrefs.h"


typedef struct W_DoubleTest {
	W_Class widgetClass;
	WMView *view;

	WMHandlerID timer;
	char on;
	char active;
	char *text;
} _DoubleTest;

/* some forward declarations */

static void destroyDoubleTest(_DoubleTest * dPtr);
static void paintDoubleTest(_DoubleTest * dPtr);

static void handleEvents(xcb_generic_event_t * event, void *data);
static void handleActionEvents(xcb_generic_event_t * event, void *data);

/* our widget class ID */
static W_Class DoubleTestClass = 0;

/*
 * Initializer for our widget. Must be called before creating any
 * instances of the widget.
 */
W_Class InitDoubleTest(void)
{
	/* register our widget with WINGs and get our widget class ID */
	if (!DoubleTestClass) {
		DoubleTestClass = W_RegisterUserWidget();
	}

	return DoubleTestClass;
}

/*
 * Our widget fabrication plant.
 */
DoubleTest *CreateDoubleTest(WMWidget * parent, const char *text)
{
	DoubleTest *dPtr;

	if (!DoubleTestClass)
		InitDoubleTest();

	/* allocate some storage for our new widget instance */
	dPtr = wmalloc(sizeof(DoubleTest));

	/* set the class ID */
	dPtr->widgetClass = DoubleTestClass;

	dPtr->view = W_CreateView(W_VIEW(parent));
	if (!dPtr->view) {
		wfree(dPtr);
		return NULL;
	}
	/* always do this */
	dPtr->view->self = dPtr;

	dPtr->text = wstrdup(text);

	WMCreateEventHandler(dPtr->view, XCB_EVENT_MASK_EXPOSURE	/* this allows us to know when we should paint */
			     | XCB_EVENT_MASK_STRUCTURE_NOTIFY,	/* this allows us to know things like when we are destroyed */
			     handleEvents, dPtr);

	WMCreateEventHandler(dPtr->view, XCB_EVENT_MASK_BUTTON_PRESS, handleActionEvents, dPtr);

	return dPtr;
}

static void paintDoubleTest(_DoubleTest * dPtr)
{
	W_Screen *scr = dPtr->view->screen;
	cairo_t *cr;

	if (dPtr->active) {
		WMPaintColorSwatch(scr->white, dPtr->view->window,
				   0, 0, dPtr->view->size.width, dPtr->view->size.height);
	} else {
		wXClearArea(scr->display, dPtr->view->window, 0, 0, 0, 0, false);
	}

	cr = WCairoBeginForDrawable(scr, dPtr->view->window);
	if (cr) {
		W_DrawRelief(cr, scr, 0, 0, dPtr->view->size.width,
				  dPtr->view->size.height, dPtr->on ? WRSunken : WRRaised);
		WCairoEnd(cr);
	}

	if (dPtr->text) {
		int y;
		y = (dPtr->view->size.height - scr->normalFont->height) / 2;
		cr = WCairoBeginForDrawable(scr, dPtr->view->window);
		if (cr) {
			W_PaintText(cr, scr->normalFont,
					 dPtr->on, dPtr->on + y, dPtr->view->size.width, WACenter,
					 scr->black, false, dPtr->text, strlen(dPtr->text));
			WCairoEnd(cr);
		}
	}
}

static void handleEvents(xcb_generic_event_t * event, void *data)
{
	_DoubleTest *dPtr = (_DoubleTest *) data;

	switch ((event->response_type & 0x7f)) {
	case XCB_EXPOSE:
		if (((xcb_expose_event_t *)event)->count != 0)
			break;
		paintDoubleTest(dPtr);
		break;

	case XCB_DESTROY_NOTIFY:
		destroyDoubleTest(dPtr);
		break;

	}
}

static void deactivate(void *data)
{
	_DoubleTest *dPtr = (_DoubleTest *) data;

	if (dPtr->active)
		dPtr->active = 0;
	paintDoubleTest(dPtr);

	dPtr->timer = NULL;
}

static void handleActionEvents(xcb_generic_event_t * event, void *data)
{
	_DoubleTest *dPtr = (_DoubleTest *) data;

	switch ((event->response_type & 0x7f)) {
	case XCB_BUTTON_PRESS:
		if (WMIsDoubleClick(event)) {
			if (dPtr->timer)
				WMDeleteTimerHandler(dPtr->timer);
			dPtr->timer = NULL;
			dPtr->on = !dPtr->on;
			dPtr->active = 0;
			paintDoubleTest(dPtr);
		} else {
			dPtr->timer = WMAddTimerHandler(WINGsConfiguration.doubleClickDelay, deactivate, dPtr);
			dPtr->active = 1;
			paintDoubleTest(dPtr);
		}
		break;
	}
}

static void destroyDoubleTest(_DoubleTest * dPtr)
{
	if (dPtr->timer)
		WMDeleteTimerHandler(dPtr->timer);
	if (dPtr->text)
		wfree(dPtr->text);

	wfree(dPtr);
}
