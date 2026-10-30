/*
 *  wscroller.c
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

/*
 * Until FreeBSD gets their act together;
 * http://www.mail-archive.com/freebsd-hackers@freebsd.org/msg69469.html
 */
#if defined( FREEBSD )
#   undef _XOPEN_SOURCE
#endif

#include <cairo.h>
#include <cairo-xcb.h>

#include "WINGsP.h"

#include <wraster.h>
#include <math.h>

/* undefine will disable the autoadjusting of the knob dimple to be
 * directly below the cursor
 * DOES NOT WORK */
#undef STRICT_NEXT_BEHAVIOUR

#define AUTOSCROLL_INITIAL_DELAY 	200

#define AUTOSCROLL_DELAY 		40

const char *WMScrollerDidScrollNotification = "WMScrollerDidScrollNotification";

typedef struct W_Scroller {
	W_Class widgetClass;
	W_View *view;

	void *clientData;
	WMAction *action;

	float knobProportion;
	float floatValue;

	WMHandlerID timerID;	/* for continuous scrolling mode */

#ifndef STRICT_NEXT_BEHAVIOUR
	int dragPoint;		/* point where the knob is being
				 * dragged */
#endif
	struct {
		WMScrollArrowPosition arrowsPosition:4;

		unsigned int horizontal:1;

		WMScrollerPart hitPart:4;

		/* */
		unsigned int documentFullyVisible:1;	/* document is fully visible */

		unsigned int prevSelected:1;

		unsigned int pushed:1;

		unsigned int incrDown:1;	/* whether increment button is down */

		unsigned int decrDown:1;

		unsigned int draggingKnob:1;

		unsigned int configured:1;

		unsigned int redrawPending:1;
	} flags;
} Scroller;

#define DEFAULT_ARROWS_POSITION	WSAMinEnd

#define BUTTON_SIZE             ((SCROLLER_WIDTH) - 4)

static void destroyScroller(Scroller * sPtr);
static void paintScroller(Scroller * sPtr);

static void willResizeScroller(W_ViewDelegate * self, WMView * view, unsigned int *width, unsigned int *height);
static void handleEvents(xcb_generic_event_t * event, void *data);
static void handleActionEvents(xcb_generic_event_t * event, void *data);

static void handleMotion(Scroller * sPtr, int mouseX, int mouseY);

W_ViewDelegate _ScrollerViewDelegate = {
	NULL,
	NULL,
	NULL,
	NULL,
	willResizeScroller
};

WMScroller *WMCreateScroller(WMWidget * parent)
{
	Scroller *sPtr;

	sPtr = wmalloc(sizeof(Scroller));
	sPtr->widgetClass = WC_Scroller;

	sPtr->view = W_CreateView(W_VIEW(parent));
	if (!sPtr->view) {
		wfree(sPtr);
		return NULL;
	}
	sPtr->view->self = sPtr;

	sPtr->view->delegate = &_ScrollerViewDelegate;

	sPtr->flags.documentFullyVisible = 1;

	WMCreateEventHandler(sPtr->view, XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY
			     | ClientMessageMask, handleEvents, sPtr);

	W_ResizeView(sPtr->view, SCROLLER_WIDTH, SCROLLER_WIDTH);
	sPtr->flags.arrowsPosition = DEFAULT_ARROWS_POSITION;

	WMCreateEventHandler(sPtr->view, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE
			     | XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_BUTTON_MOTION, handleActionEvents, sPtr);

	sPtr->flags.hitPart = WSNoPart;

	sPtr->floatValue = 0.0;
	sPtr->knobProportion = 1.0;

	return sPtr;
}

void WMSetScrollerArrowsPosition(WMScroller * sPtr, WMScrollArrowPosition position)
{
	sPtr->flags.arrowsPosition = position;
	if (sPtr->view->flags.realized) {
		paintScroller(sPtr);
	}
}

static void willResizeScroller(W_ViewDelegate * self, WMView * view, unsigned int *width, unsigned int *height)
{
	WMScroller *sPtr = (WMScroller *) view->self;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;

	if (*width > *height) {
		sPtr->flags.horizontal = 1;
		*height = SCROLLER_WIDTH;
	} else {
		sPtr->flags.horizontal = 0;
		*width = SCROLLER_WIDTH;
	}
}

void WMSetScrollerAction(WMScroller * sPtr, WMAction * action, void *clientData)
{
	CHECK_CLASS(sPtr, WC_Scroller);

	sPtr->action = action;

	sPtr->clientData = clientData;
}

void WMSetScrollerParameters(WMScroller * sPtr, float floatValue, float knobProportion)
{
	/*
	 * This value represents 1 pixel on a 4k wide screen, it makes
	 * a good minimum; this ensure a non-null value to avoid
	 * potential division-by-0.
	 * Please note that there is another size check when drawing
	 * the knob to make sure it will remain selectable.
	 */
	static const float min_knob_proportion = 1.0 / 4096.0;

	CHECK_CLASS(sPtr, WC_Scroller);

	assert(!isnan(floatValue));

	if (floatValue < 0.0F)
		sPtr->floatValue = 0.0F;
	else if (floatValue > 1.0F)
		sPtr->floatValue = 1.0F;
	else
		sPtr->floatValue = floatValue;

	if (knobProportion <= min_knob_proportion) {

		sPtr->knobProportion = min_knob_proportion;
		sPtr->flags.documentFullyVisible = 0;

	} else if (knobProportion >= 1.0F) {

		sPtr->knobProportion = 1.0F;
		sPtr->flags.documentFullyVisible = 1;

	} else {
		sPtr->knobProportion = knobProportion;
		sPtr->flags.documentFullyVisible = 0;
	}

	if (sPtr->view->flags.realized)
		paintScroller(sPtr);

	/*   WMPostNotificationName(WMScrollerDidScrollNotification, sPtr, NULL); */
}

float WMGetScrollerKnobProportion(WMScroller * sPtr)
{
	CHECK_CLASS(sPtr, WC_Scroller);

	return sPtr->knobProportion;
}

float WMGetScrollerValue(WMScroller * sPtr)
{
	CHECK_CLASS(sPtr, WC_Scroller);

	return sPtr->floatValue;
}

WMScrollerPart WMGetScrollerHitPart(WMScroller * sPtr)
{
	CHECK_CLASS(sPtr, WC_Scroller);

	return sPtr->flags.hitPart;
}

static void blitPixmap(cairo_t *cr, W_Screen *scr, W_Pixmap *pix, int x, int y)
{
	RContext *rc = scr->rcontext;
	cairo_surface_t *psurf, *msurf;

	psurf = cairo_xcb_surface_create(rc->xc, (xcb_drawable_t)pix->pixmap,
					 rc->xvisual, pix->width, pix->height);
	cairo_set_source_surface(cr, psurf, x, y);
	if (pix->mask != XCB_NONE) {
		msurf = cairo_xcb_surface_create_for_bitmap(rc->xc, rc->xscreen,
					pix->mask,
					pix->width, pix->height);
		cairo_mask_surface(cr, msurf, x, y);
		cairo_surface_destroy(msurf);
	} else {
		cairo_rectangle(cr, x, y, pix->width, pix->height);
		cairo_fill(cr);
	}
	cairo_surface_destroy(psurf);
}

static void paintArrow(WMScroller *sPtr, cairo_t *cr, int part)
/*
 * part- 0 paints the decrement arrow, 1 the increment arrow
 */
{
	WMView *view = sPtr->view;
	WMScreen *scr = view->screen;
	W_Pixmap *arrow;
	int ofs, bx, by, down;

	if (part == 0) {	/* decrement button */
		down = sPtr->flags.decrDown;
		if (sPtr->flags.horizontal) {
			ofs = (sPtr->flags.arrowsPosition == WSAMaxEnd)
			    ? view->size.width - 2 * (BUTTON_SIZE + 1) - 1 : 2;
			arrow = down ? scr->hiLeftArrow : scr->leftArrow;
		} else {
			ofs = (sPtr->flags.arrowsPosition == WSAMaxEnd)
			    ? view->size.height - 2 * (BUTTON_SIZE + 1) - 1 : 2;
			arrow = down ? scr->hiUpArrow : scr->upArrow;
		}
	} else {		/* increment button */
		down = sPtr->flags.incrDown;
		if (sPtr->flags.horizontal) {
			ofs = (sPtr->flags.arrowsPosition == WSAMaxEnd)
			    ? view->size.width - BUTTON_SIZE + 1 - 3
			    : 2 + BUTTON_SIZE + 1;
			arrow = down ? scr->hiRightArrow : scr->rightArrow;
		} else {
			ofs = (sPtr->flags.arrowsPosition == WSAMaxEnd)
			    ? view->size.height - BUTTON_SIZE + 1 - 3
			    : 2 + BUTTON_SIZE + 1;
			arrow = down ? scr->hiDownArrow : scr->downArrow;
		}
	}

	if (sPtr->flags.horizontal) {
		bx = ofs;
		by = 2;
	} else {
		bx = 2;
		by = ofs;
	}

	if (down) {
		W_SetSourceColor(cr, scr->white);
		cairo_rectangle(cr, bx + 1, by + 1, BUTTON_SIZE - 2, BUTTON_SIZE - 2);
		cairo_fill(cr);
	}
	W_DrawRelief(cr, scr, bx, by, BUTTON_SIZE, BUTTON_SIZE, WRRaised);
	blitPixmap(cr, scr, arrow,
		   bx + (BUTTON_SIZE - arrow->width) / 2,
		   by + (BUTTON_SIZE - arrow->height) / 2);
}

static int knobLength(Scroller * sPtr)
{
	int tmp, length;

	if (sPtr->flags.horizontal)
		length = sPtr->view->size.width - 4;
	else
		length = sPtr->view->size.height - 4;

	if (sPtr->flags.arrowsPosition != WSANone) {
		length -= 2 * (BUTTON_SIZE + 1);
	}

	tmp = (int)((float)length * sPtr->knobProportion + 0.5F);
	/* keep minimum size */
	if (tmp < BUTTON_SIZE)
		tmp = BUTTON_SIZE;

	return tmp;
}

static void paintScroller(Scroller * sPtr)
{
	WMView *view = sPtr->view;
	WMScreen *scr = view->screen;
	cairo_t *cr;
	int length, ofs;
	float knobP, knobL;

	/* the arrow and dimple sources are server pixmaps so draw
	 * directly onto the view and keep those composites server-side */
	cr = WCairoBeginForDrawable(scr, view->window);
	if (!cr)
		return;

	W_SetSourceColor(cr, scr->gray);
	cairo_paint(cr);
	W_SetSourceColor(cr, scr->black);
	cairo_set_line_width(cr, 1.0);
	cairo_rectangle(cr, 0.5, 0.5, view->size.width - 1, view->size.height - 1);
	cairo_stroke(cr);

	if (sPtr->flags.horizontal)
		length = view->size.width - 4;
	else
		length = view->size.height - 4;

	if (sPtr->flags.documentFullyVisible) {
		W_StippleFill(cr, scr, 2, 2, view->size.width - 4, view->size.height - 4);
	} else {
		ofs = 2;
		if (sPtr->flags.arrowsPosition == WSAMaxEnd) {
			length -= (BUTTON_SIZE + 1) * 2;
		} else if (sPtr->flags.arrowsPosition == WSAMinEnd) {
			ofs += (BUTTON_SIZE + 1) * 2;
			length -= (BUTTON_SIZE + 1) * 2;
		}

		knobL = (float)knobLength(sPtr);

		knobP = sPtr->floatValue * ((float)length - knobL);

		if (sPtr->flags.horizontal) {
			/* before */
			W_StippleFill(cr, scr, ofs, 2, (int)knobP, view->size.height - 4);

			/* knob */
			W_DrawRelief(cr, scr, ofs + (int)knobP, 2,
					  (int)knobL, view->size.height - 4, WRRaised);
			blitPixmap(cr, scr, scr->scrollerDimple,
				   ofs + (int)knobP + ((int)knobL - scr->scrollerDimple->width - 1) / 2,
				   (view->size.height - scr->scrollerDimple->height - 1) / 2);

			/* after */
			if ((int)(knobP + knobL) < length)
				W_StippleFill(cr, scr,
						  ofs + (int)(knobP + knobL), 2,
						  length - (int)(knobP + knobL), view->size.height - 4);
		} else {
			/* before */
			if (knobP > 0.0F)
				W_StippleFill(cr, scr, 2, ofs,
						  view->size.width - 4, (int)knobP);

			/* knob */
			W_DrawRelief(cr, scr, 2, ofs + (int)knobP,
					  view->size.width - 4, (int)knobL, WRRaised);
			blitPixmap(cr, scr, scr->scrollerDimple,
				   (view->size.width - scr->scrollerDimple->width - 1) / 2,
				   ofs + (int)knobP + ((int)knobL - scr->scrollerDimple->height - 1) / 2);

			/* after */
			if ((int)(knobP + knobL) < length)
				W_StippleFill(cr, scr,
						  2, ofs + (int)(knobP + knobL),
						  view->size.width - 4, length - (int)(knobP + knobL));
		}

		if (sPtr->flags.arrowsPosition != WSANone) {
			paintArrow(sPtr, cr, 0);
			paintArrow(sPtr, cr, 1);
		}
	}

	WCairoEnd(cr);
}

static void handleEvents(xcb_generic_event_t * event, void *data)
{
	Scroller *sPtr = (Scroller *) data;

	CHECK_CLASS(data, WC_Scroller);

	switch ((event->response_type & 0x7f)) {
	case XCB_EXPOSE:
		if (((xcb_expose_event_t *)event)->count == 0)
			paintScroller(sPtr);
		break;

	case XCB_DESTROY_NOTIFY:
		destroyScroller(sPtr);
		break;
	}
}

/*
 * locatePointInScroller-
 *     Return the part of the scroller where the point is located.
 */
static WMScrollerPart locatePointInScroller(Scroller * sPtr, int x, int y, int alternate)
{
	int width = sPtr->view->size.width;
	int height = sPtr->view->size.height;
	int c, p1, p2, p3, p4, p5, p6;
	int knobL, slotL;

	/* if there is no knob... */
	if (sPtr->flags.documentFullyVisible)
		return WSKnobSlot;

	if (sPtr->flags.horizontal)
		c = x;
	else
		c = y;

	/*     p1  p2           p3           p4   p5   p6
	 * |   |   |###########|             |#####|   |   |
	 * | < | > |###########|      O      |#####| < | > |
	 * |   |   |###########|             |#####|   |   |
	 */

	if (sPtr->flags.arrowsPosition == WSAMinEnd) {
		p1 = 18;
		p2 = 36;

		if (sPtr->flags.horizontal) {
			slotL = width - 36;
			p5 = width;
		} else {
			slotL = height - 36;
			p5 = height;
		}
		p6 = p5;
	} else if (sPtr->flags.arrowsPosition == WSAMaxEnd) {
		if (sPtr->flags.horizontal) {
			slotL = width - 36;
			p6 = width - 18;
		} else {
			slotL = height - 36;
			p6 = height - 18;
		}
		p5 = p6 - 18;

		p1 = p2 = 0;
	} else {
		/* no arrows */
		p1 = p2 = 0;

		if (sPtr->flags.horizontal) {
			slotL = p5 = p6 = width;
		} else {
			slotL = p5 = p6 = height;
		}
	}

	knobL = knobLength(sPtr);
	p3 = p2 + (int)((float)(slotL - knobL) * sPtr->floatValue);
	p4 = p3 + knobL;

	/* uses a mix of the NS and Win ways of doing scroll page */
	if (c <= p1)
		return alternate ? WSDecrementPage : WSDecrementLine;
	else if (c <= p2)
		return alternate ? WSIncrementPage : WSIncrementLine;
	else if (c <= p3)
		return WSDecrementPage;
	else if (c <= p4)
		return WSKnob;
	else if (c <= p5)
		return WSIncrementPage;
	else if (c <= p6)
		return alternate ? WSDecrementPage : WSDecrementLine;
	else
		return alternate ? WSIncrementPage : WSIncrementLine;
}

static void handlePush(Scroller * sPtr, int pushX, int pushY, int alternate)
{
	WMScrollerPart part;
	int doAction = 0;
#ifndef STRICT_NEXT_BEHAVIOUR
	int length, knobP;
	int buttonsLen;
#endif

	part = locatePointInScroller(sPtr, pushX, pushY, alternate);

	sPtr->flags.hitPart = part;

	switch (part) {
	case WSIncrementLine:
		sPtr->flags.incrDown = 1;
		doAction = 1;
		break;

	case WSIncrementPage:
		doAction = 1;
		break;

	case WSDecrementLine:
		sPtr->flags.decrDown = 1;
		doAction = 1;
		break;

	case WSDecrementPage:
		doAction = 1;
		break;

	case WSKnob:
		sPtr->flags.draggingKnob = 1;
#ifndef STRICT_NEXT_BEHAVIOUR
		if (sPtr->flags.horizontal)
			sPtr->dragPoint = pushX;
		else
			sPtr->dragPoint = pushY;

		if (sPtr->flags.arrowsPosition != WSANone)
			buttonsLen = 2 * (BUTTON_SIZE + 1);
		else
			buttonsLen = 0;

		if (sPtr->flags.horizontal)
			length = sPtr->view->size.width - 4 - buttonsLen;
		else
			length = sPtr->view->size.height - 4 - buttonsLen;

		knobP = (int)(sPtr->floatValue * (float)(length - knobLength(sPtr)));

		if (sPtr->flags.arrowsPosition == WSAMinEnd)
			sPtr->dragPoint -= 2 + buttonsLen + knobP;
		else
			sPtr->dragPoint -= 2 + knobP;
#endif				/* STRICT_NEXT_BEHAVIOUR */
		/* This does not seem necessary here since we don't know yet if the
		 * knob will be dragged later. -Dan
		 handleMotion(sPtr, pushX, pushY); */
		break;

	case WSDecrementWheel:
	case WSIncrementWheel:
	case WSKnobSlot:
	case WSNoPart:
		/* dummy */
		break;
	}

	if (doAction && sPtr->action) {
		/* the action callback may destroy the scroller (e.g. close
		 * the containing window), keep the view alive across the call
		 * and the notification so the caller does not touch freed
		 * memory, mirroring the wbutton.c:476-484 pattern */
		W_RetainView(sPtr->view);

		(*sPtr->action) (sPtr, sPtr->clientData);

		WMPostNotificationName(WMScrollerDidScrollNotification, sPtr, NULL);

		W_ReleaseView(sPtr->view);
	}
}

static float floatValueForPoint(Scroller * sPtr, int point)
{
	float floatValue = 0;
	float position;
	int slotOfs, slotLength, knobL;

	if (sPtr->flags.horizontal)
		slotLength = sPtr->view->size.width - 4;
	else
		slotLength = sPtr->view->size.height - 4;

	slotOfs = 2;
	if (sPtr->flags.arrowsPosition == WSAMaxEnd) {
		slotLength -= (BUTTON_SIZE + 1) * 2;
	} else if (sPtr->flags.arrowsPosition == WSAMinEnd) {
		slotOfs += (BUTTON_SIZE + 1) * 2;
		slotLength -= (BUTTON_SIZE + 1) * 2;
	}

	knobL = (float)knobLength(sPtr);
#ifdef STRICT_NEXT_BEHAVIOUR
	if (point < slotOfs + knobL / 2)
		position = (float)(slotOfs + knobL / 2);
	else if (point > slotOfs + slotLength - knobL / 2)
		position = (float)(slotOfs + slotLength - knobL / 2);
	else
		position = (float)point;

	floatValue = (position - (float)(slotOfs + slotLength / 2))
	    / (float)(slotLength - knobL);
#else
	/* Adjust the last point to lie inside the knob slot */
	if (point < slotOfs)
		position = (float)slotOfs;
	else if (point > slotOfs + slotLength)
		position = (float)(slotOfs + slotLength);
	else
		position = (float)point;

	/* Compute the float value */
	floatValue = (position - (float)slotOfs) / (float)(slotLength - knobL);
#endif

	assert(!isnan(floatValue));
	return floatValue;
}

static void handleMotion(Scroller * sPtr, int mouseX, int mouseY)
{
	if (sPtr->flags.draggingKnob) {
		float newFloatValue;
		int point;

		if (sPtr->flags.horizontal) {
			point = mouseX;
		} else {
			point = mouseY;
		}

#ifndef STRICT_NEXT_BEHAVIOUR
		point -= sPtr->dragPoint;
#endif

		newFloatValue = floatValueForPoint(sPtr, point);
		WMSetScrollerParameters(sPtr, newFloatValue, sPtr->knobProportion);
		if (sPtr->action) {
			W_RetainView(sPtr->view);
			(*sPtr->action) (sPtr, sPtr->clientData);
			WMPostNotificationName(WMScrollerDidScrollNotification, sPtr, NULL);
			W_ReleaseView(sPtr->view);
		}
	} else {
		int part;

		part = locatePointInScroller(sPtr, mouseX, mouseY, false);

		sPtr->flags.hitPart = part;

		if (part == WSIncrementLine && sPtr->flags.decrDown) {
			sPtr->flags.decrDown = 0;
			sPtr->flags.incrDown = 1;
		} else if (part == WSDecrementLine && sPtr->flags.incrDown) {
			sPtr->flags.incrDown = 0;
			sPtr->flags.decrDown = 1;
		} else if (part != WSIncrementLine && part != WSDecrementLine) {
			sPtr->flags.incrDown = 0;
			sPtr->flags.decrDown = 0;
		}
	}
}

static void autoScroll(void *clientData)
{
	Scroller *sPtr = (Scroller *) clientData;
	W_View *view = sPtr->view;

	W_RetainView(view);
	if (sPtr->action) {
		(*sPtr->action) (sPtr, sPtr->clientData);
		WMPostNotificationName(WMScrollerDidScrollNotification, sPtr, NULL);
	}
	if (!view->flags.alreadyDead)
		sPtr->timerID = WMAddTimerHandler(AUTOSCROLL_DELAY, autoScroll, clientData);
	W_ReleaseView(view);
}

static void handleActionEvents(xcb_generic_event_t * event, void *data)
{
	Scroller *sPtr = (Scroller *) data;
	int wheelDecrement, wheelIncrement;
	int id, dd;

	/* check if we're really dealing with a scroller, as something
	 * might have gone wrong in the event dispatching stuff */
	CHECK_CLASS(sPtr, WC_Scroller);

	id = sPtr->flags.incrDown;
	dd = sPtr->flags.decrDown;

	switch ((event->response_type & 0x7f)) {
	case XCB_ENTER_NOTIFY:
		break;

	case XCB_LEAVE_NOTIFY:
		WMDeleteTimerHandlerPtr(&sPtr->timerID);
		sPtr->flags.incrDown = 0;
		sPtr->flags.decrDown = 0;
		break;

	case XCB_BUTTON_PRESS:
		if (sPtr->flags.documentFullyVisible)
			break;

		if (sPtr->flags.horizontal) {
			wheelDecrement = WINGsConfiguration.mouseWheelDown;
			wheelIncrement = WINGsConfiguration.mouseWheelUp;
		} else {
			wheelDecrement = WINGsConfiguration.mouseWheelUp;
			wheelIncrement = WINGsConfiguration.mouseWheelDown;
		}

		if (((xcb_button_press_event_t *)event)->detail == wheelDecrement) {
			if (((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_CONTROL) {
				sPtr->flags.hitPart = WSDecrementPage;
			} else if (((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_SHIFT) {
				sPtr->flags.hitPart = WSDecrementLine;
			} else {
				sPtr->flags.hitPart = WSDecrementWheel;
			}
			if (sPtr->action) {
				W_RetainView(sPtr->view);
				(*sPtr->action) (sPtr, sPtr->clientData);
				WMPostNotificationName(WMScrollerDidScrollNotification, sPtr, NULL);
				W_ReleaseView(sPtr->view);
			}
		} else if (((xcb_button_press_event_t *)event)->detail == wheelIncrement) {
			if (((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_CONTROL) {
				sPtr->flags.hitPart = WSIncrementPage;
			} else if (((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_SHIFT) {
				sPtr->flags.hitPart = WSIncrementLine;
			} else {
				sPtr->flags.hitPart = WSIncrementWheel;
			}
			if (sPtr->action) {
				W_RetainView(sPtr->view);
				(*sPtr->action) (sPtr, sPtr->clientData);
				WMPostNotificationName(WMScrollerDidScrollNotification, sPtr, NULL);
				W_ReleaseView(sPtr->view);
			}
		} else {
			handlePush(sPtr, ((xcb_button_press_event_t *)event)->event_x, ((xcb_button_press_event_t *)event)->event_y,
				   (((xcb_button_press_event_t *)event)->state & sPtr->view->screen->altModifierMask)
				   || ((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_2);
			/* continue scrolling if pushed on the buttons */
			if (sPtr->flags.hitPart == WSIncrementLine || sPtr->flags.hitPart == WSDecrementLine) {
				sPtr->timerID = WMAddTimerHandler(AUTOSCROLL_INITIAL_DELAY, autoScroll, sPtr);
			}
		}
		break;

	case XCB_BUTTON_RELEASE:
		if (sPtr->flags.draggingKnob) {
			if (sPtr->action) {
				W_RetainView(sPtr->view);
				(*sPtr->action) (sPtr, sPtr->clientData);
				WMPostNotificationName(WMScrollerDidScrollNotification, sPtr, NULL);
				W_ReleaseView(sPtr->view);
			}
		}
		WMDeleteTimerHandlerPtr(&sPtr->timerID);
		sPtr->flags.incrDown = 0;
		sPtr->flags.decrDown = 0;
		sPtr->flags.draggingKnob = 0;
		break;

	case XCB_MOTION_NOTIFY:
		handleMotion(sPtr, ((xcb_button_press_event_t *)event)->event_x, ((xcb_button_press_event_t *)event)->event_y);
		if (sPtr->timerID && sPtr->flags.hitPart != WSIncrementLine
		    && sPtr->flags.hitPart != WSDecrementLine) {
			WMDeleteTimerHandler(sPtr->timerID);
			sPtr->timerID = NULL;
		}
		break;
	}
	if (id != sPtr->flags.incrDown || dd != sPtr->flags.decrDown)
		paintScroller(sPtr);
}

static void destroyScroller(Scroller * sPtr)
{
	/* we don't want autoscroll try to scroll a freed widget */
	if (sPtr->timerID) {
		WMDeleteTimerHandler(sPtr->timerID);
	}

	wfree(sPtr);
}
