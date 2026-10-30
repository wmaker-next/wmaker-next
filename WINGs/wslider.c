/*
 *  wslider.c
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 1998 scottc
 *  Copyright (c) 1999-2004 Dan Pascu
 *  Copyright (c) 1999 Alfredo K. Kojima
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
#include <cairo-xcb.h>

#include "WINGsP.h"

#include <wraster.h>

#undef STRICT_NEXT_BEHAVIOUR

typedef struct W_Slider {
	W_Class widgetClass;
	WMView *view;

	int minValue;
	int maxValue;

	int value;

	xcb_pixmap_t knobPixmap;
	WMPixmap *backPixmap;

	WMAction *action;
	void *clientData;

	int knobThickness;

	struct {
		unsigned int continuous:1;

		unsigned int vertical:1;
		unsigned int dragging:1;
		unsigned int enabled:1;
	} flags;

} Slider;

static void didResizeSlider(W_ViewDelegate * self, WMView * view);

W_ViewDelegate _SliderViewDelegate = {
	NULL,
	NULL,
	didResizeSlider,
	NULL,
	NULL
};

static void destroySlider(Slider * sPtr);
static void paintSlider(Slider * sPtr);

static void handleEvents(xcb_generic_event_t * event, void *data);
static void handleActionEvents(xcb_generic_event_t * event, void *data);

static void makeKnobPixmap(Slider * sPtr);

static void realizeObserver(void *self, WMNotification * not)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) not;

	makeKnobPixmap(self);
}

WMSlider *WMCreateSlider(WMWidget * parent)
{
	Slider *sPtr;

	sPtr = wmalloc(sizeof(Slider));
	sPtr->widgetClass = WC_Slider;

	sPtr->view = W_CreateView(W_VIEW(parent));
	if (!sPtr->view) {
		wfree(sPtr);
		return NULL;
	}
	sPtr->view->self = sPtr;

	sPtr->view->delegate = &_SliderViewDelegate;

	WMCreateEventHandler(sPtr->view, XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY, handleEvents, sPtr);

	WMCreateEventHandler(sPtr->view, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE
			     | XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_BUTTON_MOTION, handleActionEvents, sPtr);

	W_ResizeView(sPtr->view, 100, 16);
	sPtr->flags.vertical = 0;
	sPtr->minValue = 0;
	sPtr->maxValue = 100;
	sPtr->value = 50;

	sPtr->knobThickness = 20;

	sPtr->flags.continuous = 1;
	sPtr->flags.enabled = 1;

	WMAddNotificationObserver(realizeObserver, sPtr, WMViewRealizedNotification, sPtr->view);

	return sPtr;
}

void WMSetSliderImage(WMSlider * sPtr, WMPixmap * pixmap)
{
	if (sPtr->backPixmap)
		WMReleasePixmap(sPtr->backPixmap);

	sPtr->backPixmap = WMRetainPixmap(pixmap);

	if (sPtr->view->flags.mapped) {
		paintSlider(sPtr);
	}
}

void WMSetSliderEnabled(WMSlider *sPtr, bool flag)
{
	CHECK_CLASS(sPtr, WC_Slider);

	sPtr->flags.enabled = flag ? 1 : 0;
	if (sPtr->view->flags.mapped)
		paintSlider(sPtr);
}

bool WMGetSliderEnabled(WMSlider *sPtr)
{
	CHECK_CLASS(sPtr, WC_Slider);

	return sPtr->flags.enabled;
}

void WMSetSliderKnobThickness(WMSlider * sPtr, int thickness)
{
	assert(thickness > 0);

	sPtr->knobThickness = thickness;

	if (sPtr->knobPixmap) {
		makeKnobPixmap(sPtr);
	}

	if (sPtr->view->flags.mapped) {
		paintSlider(sPtr);
	}
}

int WMGetSliderMinValue(WMSlider * slider)
{
	CHECK_CLASS(slider, WC_Slider);

	return slider->minValue;
}

int WMGetSliderMaxValue(WMSlider * slider)
{
	CHECK_CLASS(slider, WC_Slider);

	return slider->maxValue;
}

int WMGetSliderValue(WMSlider * slider)
{
	CHECK_CLASS(slider, WC_Slider);

	return slider->value;
}

void WMSetSliderMinValue(WMSlider * slider, int value)
{
	CHECK_CLASS(slider, WC_Slider);

	slider->minValue = value;
	if (slider->value < value) {
		slider->value = value;
		if (slider->view->flags.mapped)
			paintSlider(slider);
	}
}

void WMSetSliderMaxValue(WMSlider * slider, int value)
{
	CHECK_CLASS(slider, WC_Slider);

	slider->maxValue = value;
	if (slider->value > value) {
		slider->value = value;
		if (slider->view->flags.mapped)
			paintSlider(slider);
	}
}

void WMSetSliderValue(WMSlider * slider, int value)
{
	CHECK_CLASS(slider, WC_Slider);

	if (value < slider->minValue)
		slider->value = slider->minValue;
	else if (value > slider->maxValue)
		slider->value = slider->maxValue;
	else
		slider->value = value;

	if (slider->view->flags.mapped)
		paintSlider(slider);
}

void WMSetSliderContinuous(WMSlider * slider, bool flag)
{
	CHECK_CLASS(slider, WC_Slider);

	slider->flags.continuous = ((flag == 0) ? 0 : 1);
}

void WMSetSliderAction(WMSlider * slider, WMAction * action, void *data)
{
	CHECK_CLASS(slider, WC_Slider);

	slider->action = action;
	slider->clientData = data;
}

static void kLine(cairo_t *cr, WMColor *c, int x1, int y1, int x2, int y2)
{
	W_SetSourceColor(cr, c);
	cairo_move_to(cr, x1 + 0.5, y1 + 0.5);
	cairo_line_to(cr, x2 + 0.5, y2 + 0.5);
	cairo_stroke(cr);
}

static void makeKnobPixmap(Slider * sPtr)
{
	WMScreen *scr = sPtr->view->screen;
	cairo_t *cr;
	xcb_pixmap_t pix;
	int w, h;

	if (sPtr->flags.vertical) {
		w = sPtr->view->size.width - 2;
		h = sPtr->knobThickness;
	} else {
		w = sPtr->knobThickness;
		h = sPtr->view->size.height - 2;
	}

	pix = W_CreateScaledPixmap(scr, w, h);

	cr = WCairoBeginForDrawable(scr, pix);
	if (!cr)
		return;
	cairo_set_line_width(cr, 1.0);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);

	W_SetSourceColor(cr, scr->gray);
	cairo_paint(cr);

	if (sPtr->knobThickness < 10) {
		W_DrawRelief(cr, scr, 0, 0, w, h, WRRaised);
	} else if (sPtr->flags.vertical) {
		kLine(cr, scr->white,    0, 0, 0, h - 3);
		kLine(cr, scr->white,    1, 0, 1, h - 3);
		kLine(cr, scr->darkGray, w - 2, 1, w - 2, h / 2 - 2);
		kLine(cr, scr->darkGray, w - 2, h / 2, w - 2, h - 2);

		kLine(cr, scr->white,    0, 0, w - 2, 0);
		kLine(cr, scr->darkGray, 1, h / 2 - 2, w - 3, h / 2 - 2);
		kLine(cr, scr->white,    0, h / 2 - 1, w - 3, h / 2 - 1);

		kLine(cr, scr->black,    w - 1, 0, w - 1, h - 2);

		kLine(cr, scr->darkGray, 0, h - 3, w - 2, h - 3);
		kLine(cr, scr->black,    0, h - 2, w - 1, h - 2);
		kLine(cr, scr->darkGray, 0, h - 1, w - 1, h - 1);
	} else {
		kLine(cr, scr->white,    0, 0, w - 3, 0);
		kLine(cr, scr->white,    0, 0, 0, h - 2);

		kLine(cr, scr->white,    1, 0, 1, h - 3);
		kLine(cr, scr->darkGray, w / 2 - 2, 1, w / 2 - 2, h - 3);
		kLine(cr, scr->white,    w / 2 - 1, 0, w / 2 - 1, h - 3);

		kLine(cr, scr->darkGray, w - 3, 0, w - 3, h - 2);
		kLine(cr, scr->black,    w - 2, 0, w - 2, h - 2);
		kLine(cr, scr->darkGray, w - 1, 0, w - 1, h - 1);

		kLine(cr, scr->black,    1, h - 1, w / 2 + 1, h - 1);
		kLine(cr, scr->darkGray, 1, h - 2, w / 2 - 2, h - 2);
		kLine(cr, scr->darkGray, w / 2, h - 2, w - 3, h - 2);

		kLine(cr, scr->black,    0, h - 1, w - 2, h - 1);
	}
	WCairoEnd(cr);

	if (sPtr->knobPixmap)
		xcb_free_pixmap(scr->display, sPtr->knobPixmap);
	sPtr->knobPixmap = pix;
}

static void didResizeSlider(W_ViewDelegate * self, WMView * view)
{
	Slider *sPtr = (Slider *) view->self;
	int width = sPtr->view->size.width;
	int height = sPtr->view->size.height;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;

	assert(width > 0);
	assert(height > 0);

	if (width > height) {
		if (sPtr->flags.vertical) {
			sPtr->flags.vertical = 0;
			if (sPtr->view->flags.realized)
				makeKnobPixmap(sPtr);
		}
	} else {
		if (!sPtr->flags.vertical) {
			sPtr->flags.vertical = 1;
			if (sPtr->view->flags.realized)
				makeKnobPixmap(sPtr);
		}
	}
}

static void paintSlider(Slider * sPtr)
{
	W_Screen *scr = sPtr->view->screen;
	WMSize size = sPtr->view->size;
	cairo_t *cr;
	int pos, kw, kh;

#define MINV sPtr->minValue
#define MAXV sPtr->maxValue
#define POSV sPtr->value

	/* the knob and backPixmap sources are server pixmaps, drawing
	 * directly onto the view keeps their composite server-side
	 * instead of reading them back into a client image buffer */
	cr = WCairoBeginForDrawable(scr, sPtr->view->window);
	if (!cr)
		return;
	cairo_set_line_width(cr, 1.0);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);

	if (sPtr->backPixmap) {
		WMSize psz = WMGetPixmapSize(sPtr->backPixmap);

		W_PaintArea(cr, scr, WMGetPixmapXID(sPtr->backPixmap),
				psz.width, psz.height, 0, 0,
				psz.width, psz.height, 1, 1);
	} else {
		W_SetSourceColor(cr, scr->gray);
		cairo_paint(cr);
		W_StippleFill(cr, scr, 0, 0, size.width, size.height);
	}

	if (sPtr->flags.vertical) {
		pos = (MAXV != MINV) ? (size.height - 2 - sPtr->knobThickness) * (POSV - MINV) / (MAXV - MINV) + 1 : 1;
		kw = size.width - 2;
		kh = sPtr->knobThickness;
		W_PaintArea(cr, scr, sPtr->knobPixmap, kw, kh, 0, 0, kw, kh, 1, pos);
	} else {
		pos = (MAXV != MINV) ? (size.width - 2 - sPtr->knobThickness) * (POSV - MINV) / (MAXV - MINV) + 1 : 1;
		kw = sPtr->knobThickness;
		kh = size.height - 2;
		W_PaintArea(cr, scr, sPtr->knobPixmap, kw, kh, 0, 0, kw, kh, pos, 1);
	}

	kLine(cr, scr->black, 0, 0, 0, size.height - 1);
	kLine(cr, scr->black, 0, 0, size.width, 0);
	kLine(cr, scr->white, size.width - 1, 0, size.width - 1, size.height - 1);
	kLine(cr, scr->white, 0, size.height - 1, size.width - 1, size.height - 1);

	if (!sPtr->flags.enabled) {
		W_SetSourceColor(cr, scr->gray);
		cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.5);
		cairo_paint(cr);
	}

	WCairoEnd(cr);
}

static void handleEvents(xcb_generic_event_t * event, void *data)
{
	Slider *sPtr = (Slider *) data;

	CHECK_CLASS(data, WC_Slider);

	switch ((event->response_type & 0x7f)) {
	case XCB_EXPOSE:
		if (((xcb_expose_event_t *)event)->count != 0)
			break;
		paintSlider(sPtr);
		break;

	case XCB_DESTROY_NOTIFY:
		destroySlider(sPtr);
		break;

	}
}

#define DECR_PART	1
#define KNOB_PART	2
#define INCR_PART	3

static int getSliderPart(Slider * sPtr, int x, int y)
{
	int p;
	int pos;
	WMSize size = sPtr->view->size;

	if (sPtr->flags.vertical) {
		p = y;
		pos = (MAXV != MINV) ? (size.height - 2 - sPtr->knobThickness) * (POSV - MINV) / (MAXV - MINV) : 0;
		if (p < pos)
			return INCR_PART;
		if (p > pos + sPtr->knobThickness)
			return DECR_PART;
		return KNOB_PART;
	} else {
		p = x;
		pos = (MAXV != MINV) ? (size.width - 2 - sPtr->knobThickness) * (POSV - MINV) / (MAXV - MINV) : 0;
		if (p < pos)
			return DECR_PART;
		if (p > pos + sPtr->knobThickness)
			return INCR_PART;
		return KNOB_PART;
	}
}

static int valueForMousePoint(Slider * sPtr, int x, int y)
{
	WMSize size = sPtr->view->size;
	int f;

	if (sPtr->flags.vertical) {
		f = (y - sPtr->knobThickness / 2) * (MAXV - MINV)
		    / ((int)size.height - 2 - sPtr->knobThickness);
	} else {
		f = (x - sPtr->knobThickness / 2) * (MAXV - MINV)
		    / ((int)size.width - 2 - sPtr->knobThickness);
	}

	f += sPtr->minValue;
	if (f < sPtr->minValue)
		f = sPtr->minValue;
	else if (f > sPtr->maxValue)
		f = sPtr->maxValue;

	return f;
}

static void handleActionEvents(xcb_generic_event_t * event, void *data)
{
	WMSlider *sPtr = (Slider *) data;

	CHECK_CLASS(data, WC_Slider);

	if (!sPtr->flags.enabled)
		return;

	switch ((event->response_type & 0x7f)) {
	case XCB_BUTTON_PRESS:
		if (((xcb_button_press_event_t *)event)->detail == WINGsConfiguration.mouseWheelDown && !sPtr->flags.dragging) {
			/* Wheel down */
			if (sPtr->value + 1 <= sPtr->maxValue) {
				WMSetSliderValue(sPtr, sPtr->value + 1);
				if (sPtr->flags.continuous && sPtr->action) {
					(*sPtr->action) (sPtr, sPtr->clientData);
				}
			}
		} else if (((xcb_button_press_event_t *)event)->detail == WINGsConfiguration.mouseWheelUp && !sPtr->flags.dragging) {
			/* Wheel up */
			if (sPtr->value - 1 >= sPtr->minValue) {
				WMSetSliderValue(sPtr, sPtr->value - 1);
				if (sPtr->flags.continuous && sPtr->action) {
					(*sPtr->action) (sPtr, sPtr->clientData);
				}
			}
		} else if (getSliderPart(sPtr, ((xcb_button_press_event_t *)event)->event_x, ((xcb_button_press_event_t *)event)->event_y)
			   == KNOB_PART)
			sPtr->flags.dragging = 1;
		else {
#ifdef STRICT_NEXT_BEHAVIOUR
			sPtr->flags.dragging = 1;

			sPtr->value = valueForMousePoint(sPtr, ((xcb_motion_notify_event_t *)event)->event_x, ((xcb_motion_notify_event_t *)event)->event_y);
			paintSlider(sPtr);
#else
			int tmp;

			if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_2) {
				sPtr->flags.dragging = 1;

				sPtr->value = valueForMousePoint(sPtr, ((xcb_motion_notify_event_t *)event)->event_x, ((xcb_motion_notify_event_t *)event)->event_y);
				paintSlider(sPtr);
			} else {
				tmp = valueForMousePoint(sPtr, ((xcb_motion_notify_event_t *)event)->event_x, ((xcb_motion_notify_event_t *)event)->event_y);
				if (tmp < sPtr->value)
					tmp = sPtr->value - 1;
				else
					tmp = sPtr->value + 1;
				WMSetSliderValue(sPtr, tmp);
			}
#endif

			if (sPtr->flags.continuous && sPtr->action) {
				(*sPtr->action) (sPtr, sPtr->clientData);
			}
		}
		break;

	case XCB_BUTTON_RELEASE:
		if (!sPtr->flags.continuous && sPtr->action) {
			(*sPtr->action) (sPtr, sPtr->clientData);
		}
		sPtr->flags.dragging = 0;
		break;

	case XCB_MOTION_NOTIFY:
		if (sPtr->flags.dragging) {
			sPtr->value = valueForMousePoint(sPtr, ((xcb_motion_notify_event_t *)event)->event_x, ((xcb_motion_notify_event_t *)event)->event_y);
			paintSlider(sPtr);

			if (sPtr->flags.continuous && sPtr->action) {
				(*sPtr->action) (sPtr, sPtr->clientData);
			}
		}
		break;
	}
}

static void destroySlider(Slider * sPtr)
{
	if (sPtr->knobPixmap)
		wXFreePixmap(sPtr->view->screen->display, sPtr->knobPixmap);

	if (sPtr->backPixmap)
		WMReleasePixmap(sPtr->backPixmap);

	WMRemoveNotificationObserver(sPtr);

	wfree(sPtr);
}
