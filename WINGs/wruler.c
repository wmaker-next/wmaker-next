/*
 *  WINGs WMRuler: nifty ruler widget for WINGs :-)
 *
 *  Copyright (c) 1999-2000 Nwanua Elumeze
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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <cairo.h>

#include "WINGsP.h"
#include "wconfig.h"

#define MIN_DOC_WIDTH 10

typedef struct W_Ruler {
	W_Class widgetClass;
	W_View *view;
	W_View *pview;		/* the parent's view (for drawing the line) */

	WMAction *moveAction;	/* what to when while moving */
	WMAction *releaseAction;	/* what to do when released */
	void *clientData;

	WMColor *fg;
	WMFont *font;
	WMRulerMargins margins;
	int offset;
	int motion;		/* the position of the _moving_ marker(s) */
	int end;		/* the last tick on the baseline (restrict markers to it) */

	struct {
		unsigned int whichMarker:3;
		/*    0,    1,     2,     3,    4,       5,            6 */
		/* none, left, right, first, body, tabstop, first & body */

		unsigned int buttonPressed:1;
		unsigned int redraw:1;
		unsigned int RESERVED:27;
	} flags;
} Ruler;

/* Marker for left margin

   |\
   | \
   |__\
   |
   |

 */
static void drawLeftMarker(cairo_t *cr, Ruler *rPtr)
{
	int xpos = (rPtr->flags.whichMarker == 1 ? rPtr->motion : rPtr->margins.left);

	cairo_move_to(cr, xpos + 0.5, 8.5);
	cairo_line_to(cr, xpos + 0.5, 22.5);
	cairo_stroke(cr);
	cairo_move_to(cr, xpos, 1);
	cairo_line_to(cr, xpos + 6, 8);
	cairo_line_to(cr, xpos + 6, 9);
	cairo_line_to(cr, xpos, 9);
	cairo_close_path(cr);
	cairo_fill(cr);
}

/* Marker for right margin

  /|
 / |
/__|
   |
   |

 */
static void drawRightMarker(cairo_t *cr, Ruler *rPtr)
{
	int xpos = (rPtr->flags.whichMarker == 2 ? rPtr->motion : rPtr->margins.right);

	cairo_move_to(cr, xpos + 0.5, 8.5);
	cairo_line_to(cr, xpos + 0.5, 22.5);
	cairo_stroke(cr);
	cairo_move_to(cr, xpos + 1, 0);
	cairo_line_to(cr, xpos - 5, 7);
	cairo_line_to(cr, xpos - 5, 9);
	cairo_line_to(cr, xpos + 1, 9);
	cairo_close_path(cr);
	cairo_fill(cr);
}

/* Marker for first line only
   _____
   |___|
   |

 */
static void drawFirstMarker(cairo_t *cr, Ruler *rPtr)
{
	int xpos = ((rPtr->flags.whichMarker == 3 || rPtr->flags.whichMarker == 6) ?
		    rPtr->motion : rPtr->margins.first);

	cairo_rectangle(cr, xpos - 5, 10, 11, 5);
	cairo_fill(cr);
	cairo_move_to(cr, xpos + 0.5, 12.5);
	cairo_line_to(cr, xpos + 0.5, 22.5);
	cairo_stroke(cr);
}

/* Marker for rest of body
   _____
   \   /
    \./
 */
static void drawBodyMarker(cairo_t *cr, Ruler *rPtr)
{
	int xpos = ((rPtr->flags.whichMarker == 4 || rPtr->flags.whichMarker == 6) ?
		    rPtr->motion : rPtr->margins.body);

	cairo_move_to(cr, xpos - 5, 16);
	cairo_line_to(cr, xpos + 6, 16);
	cairo_line_to(cr, xpos, 22);
	cairo_close_path(cr);
	cairo_fill(cr);
}

static void paintRuler(Ruler * rPtr)
{
	int marks[8] = { 11, 3, 5, 3, 7, 3, 5, 3 };
	W_Screen *scr = rPtr->view->screen;
	cairo_t *cr;
	int i, j, w, m;
	char c[3];

	if (!rPtr->view->flags.realized)
		return;

	cr = WCairoBeginBuffered(scr, rPtr->view->window);
	if (!cr)
		return;
	cairo_set_line_width(cr, 1.0);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);

	W_SetSourceColor(cr, scr->gray);
	cairo_paint(cr);

	W_DrawString(cr, rPtr->fg, rPtr->font,
			  rPtr->margins.left + 2, 26, _("0   inches"), 10);

	/* marker ticks */
	W_SetSourceColor(cr, rPtr->fg);
	i = j = m = 0;
	w = rPtr->view->size.width - rPtr->margins.left;
	while (m < w) {
		cairo_move_to(cr, rPtr->margins.left + m + 0.5, 23.5);
		cairo_line_to(cr, rPtr->margins.left + m + 0.5, marks[i % 8] + 23.5);
		cairo_stroke(cr);
		if (i != 0 && i % 8 == 0) {
			snprintf(c, sizeof(c), "%hu", ++j);
			W_DrawString(cr, rPtr->fg, rPtr->font,
					  rPtr->margins.left + 2 + m, 26, c, 2);
			W_SetSourceColor(cr, rPtr->fg);
		}
		m = (++i) * 10;
	}

	rPtr->end = rPtr->margins.left + m - 10;
	if (rPtr->margins.right > rPtr->end)
		rPtr->margins.right = rPtr->end;
	/* base line */
	cairo_move_to(cr, rPtr->margins.left + 0.5, 22.5);
	cairo_line_to(cr, rPtr->margins.left + m - 9.5, 22.5);
	cairo_stroke(cr);

	drawLeftMarker(cr, rPtr);
	drawRightMarker(cr, rPtr);
	drawFirstMarker(cr, rPtr);
	drawBodyMarker(cr, rPtr);

	WCairoEnd(cr);
	rPtr->flags.redraw = false;
}

static bool verifyMarkerMove(Ruler * rPtr, int x)
{
	if (rPtr->flags.whichMarker < 1 || rPtr->flags.whichMarker > 6)
		return false;

	switch (rPtr->flags.whichMarker) {
	case 1:
		if (x > rPtr->margins.right - 10 || x < rPtr->offset ||
		    rPtr->margins.body + x > rPtr->margins.right - MIN_DOC_WIDTH ||
		    rPtr->margins.first + x > rPtr->margins.right - MIN_DOC_WIDTH)
			return false;
		break;

	case 2:
		if (x < rPtr->margins.first + MIN_DOC_WIDTH || x < rPtr->margins.body + MIN_DOC_WIDTH || x < rPtr->margins.left + MIN_DOC_WIDTH || x > rPtr->end)	/*rPtr->view->size.width) */
			return false;
		break;

	case 3:
		if (x >= rPtr->margins.right - MIN_DOC_WIDTH || x < rPtr->margins.left)
			return false;
		break;

	case 4:
		if (x >= rPtr->margins.right - MIN_DOC_WIDTH || x < rPtr->margins.left)
			return false;
		break;

	case 6:
		if (x >= rPtr->margins.right - MIN_DOC_WIDTH || x < rPtr->margins.left)
			return false;
		break;

	default:
		return false;
	}

	rPtr->motion = x;
	return true;
}

static int whichMarker(Ruler * rPtr, int x, int y)
{
	if (x < rPtr->offset || y > 22)
		return 0;

	if (rPtr->margins.left - x >= -6 && y <= 9 && (rPtr->margins.left - x <= 0) && y >= 4) {
		rPtr->motion = rPtr->margins.left;
		return 1;
	}
	if (rPtr->margins.right - x >= -1 && y <= 11 && rPtr->margins.right - x <= 5 && y >= 4) {
		rPtr->motion = rPtr->margins.right;
		return 2;
	}
#if 0
	/* both first and body? */
	if (rPtr->margins.first - x <= 4 && rPtr->margins.first - x >= -5
	    && rPtr->margins.body - x <= 4 && rPtr->margins.body - x >= -5 && y >= 15 && y <= 17) {
		rPtr->motion = rPtr->margins.first;
		return 6;
	}
#endif

	if (rPtr->margins.first - x <= 4 && y <= 15 && rPtr->margins.first - x >= -5 && y >= 10) {
		rPtr->motion = rPtr->margins.first;
		return 3;
	}
	if (rPtr->margins.body - x <= 4 && y <= 21 && rPtr->margins.body - x >= -5 && y >= 17) {
		rPtr->motion = rPtr->margins.body;
		return 4;
	}
	/* do tabs (5) */

	return 0;
}

static void rulerDidResize(W_ViewDelegate * self, WMView * view)
{
	Ruler *rPtr = (Ruler *) view->self;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;

	rPtr->flags.redraw = true;
	paintRuler(rPtr);
}

static void handleEvents(xcb_generic_event_t * event, void *data)
{
	Ruler *rPtr = (Ruler *) data;

	switch ((event->response_type & 0x7f)) {
	case XCB_EXPOSE:
		rulerDidResize(rPtr->view->delegate, rPtr->view);
		break;

	case XCB_MOTION_NOTIFY:
		if (rPtr->flags.buttonPressed && (((xcb_motion_notify_event_t *)event)->state & (XCB_BUTTON_INDEX_1 << 7))) {
			if (verifyMarkerMove(rPtr, ((xcb_motion_notify_event_t *)event)->event_x)) {
				cairo_t *cr;

				if (rPtr->moveAction)
					(rPtr->moveAction) (rPtr, rPtr->clientData);
				rPtr->flags.redraw = true;
				paintRuler(rPtr);
				cr = WCairoBeginForDrawable(rPtr->pview->screen,
							    rPtr->pview->window);
				if (cr) {
					W_SetSourceColor(cr, rPtr->pview->screen->darkGray);
					cairo_set_line_width(cr, 1.0);
					cairo_move_to(cr, rPtr->motion + 1.5, 40);
					cairo_line_to(cr, rPtr->motion + 1.5,
						      rPtr->pview->size.height - 5);
					cairo_stroke(cr);
					WCairoEnd(cr);
				}
			}
		}
		break;

	case XCB_BUTTON_PRESS:
		if (((xcb_button_press_event_t *)event)->detail != XCB_BUTTON_INDEX_1)
			return;
		rPtr->flags.buttonPressed = true;
		rPtr->flags.whichMarker = whichMarker(rPtr, ((xcb_motion_notify_event_t *)event)->event_x, ((xcb_motion_notify_event_t *)event)->event_y);
		break;

	case XCB_BUTTON_RELEASE:
		if (((xcb_button_press_event_t *)event)->detail != XCB_BUTTON_INDEX_1)
			return;
		rPtr->flags.buttonPressed = false;
		switch (rPtr->flags.whichMarker) {
		case 1:{
				int change = rPtr->margins.left - rPtr->motion;

				rPtr->margins.first -= change;
				rPtr->margins.body -= change;
				rPtr->margins.left = rPtr->motion;
				rPtr->flags.redraw = true;
				paintRuler(rPtr);
				break;
			}
		case 2:
			rPtr->margins.right = rPtr->motion;
			break;
		case 3:
			rPtr->margins.first = rPtr->motion;
			break;
		case 4:
			rPtr->margins.body = rPtr->motion;
			break;
		case 6:
			rPtr->margins.first = rPtr->margins.body = rPtr->motion;
			break;
		}
		if (rPtr->releaseAction)
			(rPtr->releaseAction) (rPtr, rPtr->clientData);
		break;
	}
}

W_ViewDelegate _RulerViewDelegate = {
	NULL,
	NULL,
	rulerDidResize,
	NULL,
	NULL
};

WMRuler *WMCreateRuler(WMWidget * parent)
{
	Ruler *rPtr = wmalloc(sizeof(Ruler));
	unsigned int w = WMWidgetWidth(parent);

	rPtr->widgetClass = WC_Ruler;

	rPtr->view = W_CreateView(W_VIEW(parent));

	if (!rPtr->view) {
		wfree(rPtr);
		return NULL;
	}

	rPtr->view->self = rPtr;

	W_ResizeView(rPtr->view, w, 40);

	WMCreateEventHandler(rPtr->view, XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY
			     | XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_FOCUS_CHANGE
			     | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_KEY_RELEASE
			     | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_BUTTON_1_MOTION, handleEvents, rPtr);

	rPtr->view->delegate = &_RulerViewDelegate;

	rPtr->fg = WMBlackColor(rPtr->view->screen);
	rPtr->font = WMSystemFontOfSize(rPtr->view->screen, 8);

	rPtr->offset = 22;
	rPtr->margins.left = 22;
	rPtr->margins.body = 22;
	rPtr->margins.first = 42;
	rPtr->margins.right = (w < 502 ? w : 502);
	rPtr->margins.tabs = NULL;

	rPtr->flags.whichMarker = 0;	/* none */
	rPtr->flags.buttonPressed = false;
	rPtr->flags.redraw = true;

	rPtr->moveAction = NULL;
	rPtr->releaseAction = NULL;

	rPtr->pview = W_VIEW(parent);

	return rPtr;
}

void WMSetRulerMargins(WMRuler * rPtr, WMRulerMargins margins)
{
	if (!rPtr)
		return;
	rPtr->margins.left = margins.left + rPtr->offset;
	rPtr->margins.right = margins.right + rPtr->offset;
	rPtr->margins.first = margins.first + rPtr->offset;
	rPtr->margins.body = margins.body + rPtr->offset;
	rPtr->margins.tabs = margins.tabs;	/*for loop */
	rPtr->flags.redraw = true;
	paintRuler(rPtr);

}

WMRulerMargins *WMGetRulerMargins(WMRuler * rPtr)
{
	WMRulerMargins *margins = wmalloc(sizeof(WMRulerMargins));

	if (!rPtr) {
		margins->first = margins->body = margins->left = 0;
		margins->right = 100;
		return margins;
	}

	margins->left = rPtr->margins.left - rPtr->offset;
	margins->right = rPtr->margins.right - rPtr->offset;
	margins->first = rPtr->margins.first - rPtr->offset;
	margins->body = rPtr->margins.body - rPtr->offset;
	/*for */
	margins->tabs = rPtr->margins.tabs;

	return margins;
}

bool WMIsMarginEqualToMargin(WMRulerMargins * aMargin, WMRulerMargins * anotherMargin)
{
	if (aMargin == anotherMargin)
		return true;
	else if (!aMargin || !anotherMargin)
		return false;
	if (aMargin->left != anotherMargin->left)
		return false;
	if (aMargin->first != anotherMargin->first)
		return false;
	if (aMargin->body != anotherMargin->body)
		return false;
	if (aMargin->right != anotherMargin->right)
		return false;

	return true;
}

void WMSetRulerOffset(WMRuler * rPtr, int pixels)
{
	if (!rPtr || pixels < 0 || pixels + MIN_DOC_WIDTH >= rPtr->view->size.width)
		return;
	rPtr->offset = pixels;
	/*rulerDidResize(rPtr, rPtr->view); */
}

int WMGetRulerOffset(WMRuler * rPtr)
{
	if (!rPtr)
		return 0;	/* what value should return if no ruler? -1 or 0? */
	return rPtr->offset;
}

void WMSetRulerReleaseAction(WMRuler * rPtr, WMAction * action, void *clientData)
{
	if (!rPtr)
		return;

	rPtr->releaseAction = action;
	rPtr->clientData = clientData;
}

void WMSetRulerMoveAction(WMRuler * rPtr, WMAction * action, void *clientData)
{
	if (!rPtr)
		return;

	rPtr->moveAction = action;
	rPtr->clientData = clientData;
}

/* _which_ one was released */
int WMGetReleasedRulerMargin(WMRuler * rPtr)
{
	if (!rPtr)
		return 0;
	return rPtr->flags.whichMarker;
}

/* _which_ one is being grabbed */
int WMGetGrabbedRulerMargin(WMRuler * rPtr)
{
	if (!rPtr)
		return 0;
	return rPtr->flags.whichMarker;
}
