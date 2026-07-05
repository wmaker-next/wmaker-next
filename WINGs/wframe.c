
#include <cairo.h>

#include "WINGsP.h"

typedef struct W_Frame {
	W_Class widgetClass;
	W_View *view;

	char *caption;
	WMColor *textColor;

	struct {
		WMReliefType relief:4;
		WMTitlePosition titlePosition:4;
	} flags;
} Frame;

#define DEFAULT_RELIEF 	WRGroove
#define DEFAULT_TITLE_POSITION	WTPAtTop
#define DEFAULT_WIDTH		40
#define DEFAULT_HEIGHT		40

static void destroyFrame(Frame * fPtr);
static void paintFrame(Frame * fPtr);
static void repaintFrame(Frame * fPtr);

void WMSetFrameTitlePosition(WMFrame * fPtr, WMTitlePosition position)
{
	fPtr->flags.titlePosition = position;

	if (fPtr->view->flags.realized) {
		repaintFrame(fPtr);
	}
}

void WMSetFrameRelief(WMFrame * fPtr, WMReliefType relief)
{
	fPtr->flags.relief = relief;

	if (fPtr->view->flags.realized) {
		repaintFrame(fPtr);
	}
}

void WMSetFrameTitle(WMFrame * fPtr, const char *title)
{
	if (fPtr->caption)
		wfree(fPtr->caption);

	if (title)
		fPtr->caption = wstrdup(title);
	else
		fPtr->caption = NULL;

	if (fPtr->view->flags.realized) {
		repaintFrame(fPtr);
	}
}

static void repaintFrame(Frame * fPtr)
{
	W_View *view = fPtr->view;
	W_Screen *scrPtr = view->screen;

	XClearWindow(scrPtr->display, view->window);
	paintFrame(fPtr);
}

static void paintFrame(Frame * fPtr)
{
	W_View *view = fPtr->view;
	W_Screen *scrPtr = view->screen;
	WMFont *font = scrPtr->normalFont;
	cairo_t *cr;
	int tx, ty, tw, th, tlen;
	int fy, fh;
	Bool drawTitle;

	if (fPtr->caption != NULL) {
		th = WMFontHeight(font);
		tlen = strlen(fPtr->caption);
	} else {
		th = 0;
		tlen = 0;
	}

	switch (fPtr->flags.titlePosition) {
	case WTPAboveTop:
		ty = 0;
		fy = th + 4;
		fh = view->size.height - fy;
		break;

	case WTPAtTop:
		ty = 0;
		fy = th / 2;
		fh = view->size.height - fy;
		break;

	case WTPBelowTop:
		ty = 4;
		fy = 0;
		fh = view->size.height;
		break;

	case WTPAboveBottom:
		ty = view->size.height - th - 4;
		fy = 0;
		fh = view->size.height;
		break;

	case WTPAtBottom:
		ty = view->size.height - th;
		fy = 0;
		fh = view->size.height - th / 2;
		break;

	case WTPBelowBottom:
		ty = view->size.height - th;
		fy = 0;
		fh = view->size.height - th - 4;
		break;

	default:
		ty = 0;
		fy = 0;
		fh = view->size.height;
	}

	if (tlen > 0 && fPtr->flags.titlePosition != WTPNoTitle) {
		tw = WMWidthOfString(font, fPtr->caption, tlen);

		tx = (view->size.width - tw) / 2;

		drawTitle = True;
	} else {
		tw = tx = 0;
		drawTitle = False;
	}

	cr = WCairoBeginForDrawable(scrPtr, view->window);
	if (!cr)
		return;

	/* punch the title span out of the clip so the relief lines skip
	 * it, EVEN_ODD on the outer view rectangle minus the (tx,ty,tw,th)
	 * inner rectangle is the same shape the XRegion subtract built */
	cairo_save(cr);
	if (drawTitle) {
		cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
		cairo_rectangle(cr, 0, 0, view->size.width, view->size.height);
		cairo_rectangle(cr, tx, ty, tw, th);
		cairo_clip(cr);
	}
	W_DrawReliefCairo(cr, scrPtr, 0, fy, view->size.width, fh, fPtr->flags.relief);
	cairo_restore(cr);

	if (drawTitle) {
		/* fill backColor under the caption first so a repeat paint
		 * without an XClearWindow does not thicken the AA fringe */
		WCairoSetSourceColor(cr, view->backColor);
		cairo_rectangle(cr, tx, ty, tw, th);
		cairo_fill(cr);
		WMDrawStringCairo(cr, fPtr->textColor ? fPtr->textColor : scrPtr->black,
				  font, tx, ty, fPtr->caption, tlen);
	}

	WCairoEnd(cr);
}

static void handleEvents(XEvent * event, void *data)
{
	Frame *fPtr = (Frame *) data;

	CHECK_CLASS(data, WC_Frame);

	switch (event->type) {
	case Expose:
		if (event->xexpose.count == 0)
			paintFrame(fPtr);
		break;

	case DestroyNotify:
		destroyFrame(fPtr);
		break;
	}
}

void WMSetFrameTitleColor(WMFrame *fPtr, WMColor *color)
{
	if (fPtr->textColor)
		WMReleaseColor(fPtr->textColor);
	fPtr->textColor = WMRetainColor(color);

	if (fPtr->view->flags.realized) {
		repaintFrame(fPtr);
	}
}

WMFrame *WMCreateFrame(WMWidget * parent)
{
	Frame *fPtr;

	fPtr = wmalloc(sizeof(Frame));

	fPtr->widgetClass = WC_Frame;

	fPtr->view = W_CreateView(W_VIEW(parent));
	if (!fPtr->view) {
		wfree(fPtr);
		return NULL;
	}
	fPtr->view->self = fPtr;
	fPtr->textColor = WMRetainColor(fPtr->view->screen->black);

	WMCreateEventHandler(fPtr->view, ExposureMask | StructureNotifyMask, handleEvents, fPtr);

	fPtr->flags.relief = DEFAULT_RELIEF;
	fPtr->flags.titlePosition = DEFAULT_TITLE_POSITION;

	WMResizeWidget(fPtr, DEFAULT_WIDTH, DEFAULT_HEIGHT);

	return fPtr;
}

static void destroyFrame(Frame * fPtr)
{
	if (fPtr->textColor)
		WMReleaseColor(fPtr->textColor);

	if (fPtr->caption)
		wfree(fPtr->caption);

	wfree(fPtr);
}
