/*
 * Original idea and implementation by Frederik Schueler <fr.schueler@netsurf.de>
 * Rewritten by Pascal Hofstee <daeron@windowmaker.info>
 * - Added options to set min/max values
 * - centralized drawing into one pain function
 */

#include <cairo.h>

#include "WINGsP.h"

typedef struct W_ProgressIndicator {
	W_Class widgetClass;
	W_View *view;

	int value;
	int minValue;
	int maxValue;

	void *clientData;
} ProgressIndicator;

#define DEFAULT_PROGRESS_INDICATOR_WIDTH	276
#define DEFAULT_PROGRESS_INDICATOR_HEIGHT	16

/* define if only the ticks within the progress region should be displayed */
#undef SHOW_PROGRESS_TICKS_ONLY

static void didResizeProgressIndicator(W_ViewDelegate * self, WMView * view);

W_ViewDelegate _ProgressIndicatorDelegate = {
	NULL,
	NULL,
	didResizeProgressIndicator,
	NULL,
	NULL
};

static void destroyProgressIndicator(ProgressIndicator * pPtr);
static void paintProgressIndicator(ProgressIndicator * pPtr);
static void handleEvents(XEvent * event, void *data);

WMProgressIndicator *WMCreateProgressIndicator(WMWidget * parent)
{
	ProgressIndicator *pPtr;

	pPtr = wmalloc(sizeof(ProgressIndicator));

	pPtr->widgetClass = WC_ProgressIndicator;

	pPtr->view = W_CreateView(W_VIEW(parent));
	if (!pPtr->view) {
		wfree(pPtr);
		return NULL;
	}

	pPtr->view->self = pPtr;

	pPtr->view->delegate = &_ProgressIndicatorDelegate;

	WMCreateEventHandler(pPtr->view, ExposureMask | StructureNotifyMask, handleEvents, pPtr);

	W_ResizeView(pPtr->view, DEFAULT_PROGRESS_INDICATOR_WIDTH, DEFAULT_PROGRESS_INDICATOR_HEIGHT);

	/* Initialize ProgressIndicator Values */
	pPtr->value = 0;
	pPtr->minValue = 0;
	pPtr->maxValue = 100;

	return pPtr;
}

void WMSetProgressIndicatorMinValue(WMProgressIndicator * progressindicator, int value)
{
	CHECK_CLASS(progressindicator, WC_ProgressIndicator);

	progressindicator->minValue = value;
	if (progressindicator->value < value) {
		progressindicator->value = value;
		if (progressindicator->view->flags.mapped) {
			paintProgressIndicator(progressindicator);
		}
	}
}

void WMSetProgressIndicatorMaxValue(WMProgressIndicator * progressindicator, int value)
{
	CHECK_CLASS(progressindicator, WC_ProgressIndicator);

	progressindicator->maxValue = value;
	if (progressindicator->value > value) {
		progressindicator->value = value;
		if (progressindicator->view->flags.mapped) {
			paintProgressIndicator(progressindicator);
		}
	}
}

void WMSetProgressIndicatorValue(WMProgressIndicator * progressindicator, int value)
{
	CHECK_CLASS(progressindicator, WC_ProgressIndicator);

	progressindicator->value = value;

	/* Check if value is within min/max-range */
	if (progressindicator->minValue > value)
		progressindicator->value = progressindicator->minValue;

	if (progressindicator->maxValue < value)
		progressindicator->value = progressindicator->maxValue;

	if (progressindicator->view->flags.mapped) {
		paintProgressIndicator(progressindicator);
	}
}

int WMGetProgressIndicatorMinValue(WMProgressIndicator * progressindicator)
{
	CHECK_CLASS(progressindicator, WC_ProgressIndicator);

	return progressindicator->minValue;
}

int WMGetProgressIndicatorMaxValue(WMProgressIndicator * progressindicator)
{
	CHECK_CLASS(progressindicator, WC_ProgressIndicator);

	return progressindicator->maxValue;
}

int WMGetProgressIndicatorValue(WMProgressIndicator * progressindicator)
{
	CHECK_CLASS(progressindicator, WC_ProgressIndicator);

	return progressindicator->value;
}

static void didResizeProgressIndicator(W_ViewDelegate * self, WMView * view)
{
	WMProgressIndicator *pPtr = (WMProgressIndicator *) view->self;
	int width = pPtr->view->size.width;
	int height = pPtr->view->size.height;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;
	(void) width;
	(void) height;

	assert(width > 0);
	assert(height > 0);
}

static void hLine(cairo_t *cr, WMColor *c, double x1, double y, double x2)
{
	WCairoSetSourceColor(cr, c);
	cairo_move_to(cr, x1 + 0.5, y + 0.5);
	cairo_line_to(cr, x2 + 0.5, y + 0.5);
	cairo_stroke(cr);
}

static void vLine(cairo_t *cr, WMColor *c, double x, double y1, double y2)
{
	WCairoSetSourceColor(cr, c);
	cairo_move_to(cr, x + 0.5, y1 + 0.5);
	cairo_line_to(cr, x + 0.5, y2 + 0.5);
	cairo_stroke(cr);
}

static void paintProgressIndicator(ProgressIndicator * pPtr)
{
	W_Screen *scr = pPtr->view->screen;
	WMSize size = pPtr->view->size;
	cairo_t *cr;
	int perc, w, h;
	double unit, i;

	unit = (double)(size.width - 3.0) / 100;

	cr = WCairoBeginBuffered(scr, pPtr->view->window);
	if (!cr)
		return;
	cairo_set_line_width(cr, 1.0);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);

	WCairoSetSourceColor(cr, scr->gray);
	cairo_paint(cr);

	/* Calculate size of Progress to draw and paint ticks */
	perc = (pPtr->maxValue != pPtr->minValue) ? (pPtr->value - pPtr->minValue) * 100 / (pPtr->maxValue - pPtr->minValue) : 0;

	w = (int)((double)(perc * unit));
	h = size.height - 2;

	if (w > (size.width - 3))
		w = size.width - 3;

	if (w > 0) {
		WCairoStippleFill(cr, scr, 2, 1, w, h);

		/* Draw Progress Marks */
		i = (5.0 * unit);

#ifdef SHOW_PROGRESS_TICKS_ONLY
		while ((int)i < w + 5) {
#else
		while ((int)i < (size.width - 3)) {
#endif
			vLine(cr, scr->darkGray, (int)i + 2, h - 1, h - 3);

			i += (5.0 * unit);

#ifdef SHOW_PROGRESS_TICKS_ONLY
			if ((int)i >= w)
				break;
#endif

			vLine(cr, scr->darkGray, (int)i + 2, h - 1, h - 6);

			i += (5.0 * unit);
		}
	}

	vLine(cr, scr->black, w + 2, 1, h + 1);
	hLine(cr, scr->gray, 2, h, w + 2);

	vLine(cr, scr->darkGray, 0, 0, size.height - 1);
	hLine(cr, scr->darkGray, 0, 0, size.width);
	vLine(cr, scr->black, 1, 1, size.height - 1);
	hLine(cr, scr->black, 1, 1, size.width - 1);

	vLine(cr, scr->white, size.width - 1, 0, size.height - 1);
	hLine(cr, scr->white, 0, size.height - 1, size.width - 1);

	WCairoEnd(cr);
}

static void handleEvents(XEvent * event, void *data)
{
	ProgressIndicator *pPtr = (ProgressIndicator *) data;

	CHECK_CLASS(data, WC_ProgressIndicator);

	switch (event->type) {
	case Expose:
		if (event->xexpose.count != 0)
			break;
		paintProgressIndicator(pPtr);
		break;
	case DestroyNotify:
		destroyProgressIndicator(pPtr);
		break;
	}
}

static void destroyProgressIndicator(ProgressIndicator * pPtr)
{
	WMRemoveNotificationObserver(pPtr);

	wfree(pPtr);
}
