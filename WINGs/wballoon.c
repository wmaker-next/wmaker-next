
#include "wconfig.h"
#include "WINGsP.h"

#ifdef USE_XSHAPE
#include <X11/Xlib-xcb.h>
#include <xcb/shape.h>
#endif

typedef struct W_Balloon {
	W_View *view;

	WMHashTable *table;	/* Table from view ptr to text */

	WMColor *backColor;
	WMColor *textColor;
	WMFont *font;

	WMHandlerID timer;	/* timer for showing balloon */

	WMHandlerID noDelayTimer;

	int delay;

	Window forWindow;	/* window for which the balloon
				 * is being show in the moment */

	struct {
		WMAlignment alignment:2;
		unsigned enabled:1;

		unsigned noDelay:1;
	} flags;
} Balloon;

#define DEFAULT_WIDTH		60
#define DEFAULT_HEIGHT		14
#define DEFAULT_ALIGNMENT	WALeft
#define DEFAULT_DELAY		500

#define NO_DELAY_DELAY		150

static void destroyBalloon(Balloon * bPtr);

static void handleEvents(XEvent * event, void *data);

static void showText(Balloon * bPtr, int x, int y, int w, int h, const char *text);

struct W_Balloon *W_CreateBalloon(WMScreen * scr)
{
	Balloon *bPtr;

	bPtr = wmalloc(sizeof(Balloon));

	bPtr->view = W_CreateUnmanagedTopView(scr);
	if (!bPtr->view) {
		wfree(bPtr);
		return NULL;
	}
	bPtr->view->self = bPtr;

	bPtr->textColor = WMRetainColor(bPtr->view->screen->black);

	WMCreateEventHandler(bPtr->view, StructureNotifyMask, handleEvents, bPtr);

	W_ResizeView(bPtr->view, DEFAULT_WIDTH, DEFAULT_HEIGHT);
	bPtr->flags.alignment = DEFAULT_ALIGNMENT;

	bPtr->table = WMCreateHashTable(WMIntHashCallbacks);

	bPtr->delay = DEFAULT_DELAY;

	bPtr->flags.enabled = 1;

	return bPtr;
}

void WMSetBalloonTextAlignment(WMScreen * scr, WMAlignment alignment)
{
	scr->balloon->flags.alignment = alignment;

}

void WMSetBalloonTextForView(const char *text, WMView * view)
{
	char *oldText = NULL;
	WMScreen *scr = view->screen;

	if (text) {
		oldText = WMHashInsert(scr->balloon->table, view, wstrdup(text));
	} else {
		oldText = WMHashGet(scr->balloon->table, view);

		WMHashRemove(scr->balloon->table, view);
	}

	if (oldText) {
		wfree(oldText);
	}
}

void WMSetBalloonFont(WMScreen * scr, WMFont * font)
{
	Balloon *bPtr = scr->balloon;

	if (bPtr->font != NULL)
		WMReleaseFont(bPtr->font);

	if (font)
		bPtr->font = WMRetainFont(font);
	else
		bPtr->font = NULL;
}

void WMSetBalloonTextColor(WMScreen * scr, WMColor * color)
{
	Balloon *bPtr = scr->balloon;

	if (bPtr->textColor)
		WMReleaseColor(bPtr->textColor);

	bPtr->textColor = WMRetainColor(color);
}

void WMSetBalloonDelay(WMScreen * scr, int delay)
{
	scr->balloon->delay = delay;
}

void WMSetBalloonEnabled(WMScreen * scr, Bool flag)
{
	scr->balloon->flags.enabled = ((flag == 0) ? 0 : 1);

	W_UnmapView(scr->balloon->view);
}

static void clearNoDelay(void *data)
{
	Balloon *bPtr = (Balloon *) data;

	bPtr->flags.noDelay = 0;
	bPtr->noDelayTimer = NULL;
}

void W_BalloonHandleLeaveView(WMView * view)
{
	Balloon *bPtr = view->screen->balloon;

	if (bPtr->forWindow == view->window) {
		if (bPtr->view->flags.mapped) {
			W_UnmapView(bPtr->view);
			bPtr->noDelayTimer = WMAddTimerHandler(NO_DELAY_DELAY, clearNoDelay, bPtr);
		}
		WMDeleteTimerHandlerPtr(&bPtr->timer);

		bPtr->forWindow = None;
	}
}

/*
 * botar balao perto do cursor
 * so mapear balao se o mouse ficar parado pelo delay
 *
 */

static void showBalloon(void *data)
{
	char *text;
	WMView *view = (WMView *) data;
	Balloon *bPtr = view->screen->balloon;
	int x, y;
	Window foo;

	bPtr->timer = NULL;

	text = WMHashGet(bPtr->table, view);
	if (!text)
		return;

	XTranslateCoordinates(view->screen->display, view->window, view->screen->rootWin, 0, 0, &x, &y, &foo);

	if (!bPtr->view->flags.realized)
		W_RealizeView(bPtr->view);

	showText(bPtr, x, y, view->size.width, view->size.height, text);

	bPtr->flags.noDelay = 1;
}

void W_BalloonHandleEnterView(WMView * view)
{
	Balloon *bPtr = view->screen->balloon;
	char *text;

	if (!bPtr->flags.enabled)
		return;

	text = WMHashGet(bPtr->table, view);
	if (!text) {
		if (bPtr->view->flags.realized)
			W_UnmapView(bPtr->view);

		return;
	}

	WMDeleteTimerHandlerPtr(&bPtr->timer);

	WMDeleteTimerHandlerPtr(&bPtr->noDelayTimer);

	bPtr->forWindow = view->window;

	if (bPtr->flags.noDelay) {
		bPtr->timer = NULL;

		showBalloon(view);
	} else {
		bPtr->timer = WMAddTimerHandler(bPtr->delay, showBalloon, view);
	}
}

#define TOP	0
#define BOTTOM	1
#define LEFT	0
#define RIGHT	2

#define 	SPACE	12

static void showText(Balloon * bPtr, int x, int y, int w, int h, const char *text)
{
	WMScreen *scr = bPtr->view->screen;
	Display *dpy = WMScreenDisplay(scr);
	int width;
	int height;
	Pixmap pixmap;
	Pixmap mask;
	WMFont *font = bPtr->font ? bPtr->font : scr->normalFont;
	int textHeight;
	int side = 0;
	int ty;
	int bx, by;
	const char *ptr, *ptr2;
	int tw;

	ptr2 = ptr = text;
	width = 0;
	while (ptr && ptr2) {
		ptr2 = strchr(ptr, '\n');
		if (ptr2) {
			tw = WMWidthOfString(font, ptr, ptr2 - ptr);
		} else {
			tw = WMWidthOfString(font, ptr, strlen(ptr));
		}
		if (tw > width)
			width = tw;
		if (ptr2)
			ptr = ptr2 + 1;
		else
			break;
	}

	width += 16;

	textHeight = W_GetTextHeight(font, text, width, False);

	height = textHeight + 4;

	if (height < 16)
		height = 16;
	if (width < height)
		width = height;

	if (x + width > scr->rootView->size.width) {
		side = RIGHT;
		bx = x - width + w / 2;
		if (bx < 0)
			bx = 0;
	} else {
		side = LEFT;
		bx = x + w / 2;
	}
	if (bx + width > scr->rootView->size.width)
		bx = scr->rootView->size.width - width;

	if (y - (height + SPACE) < 0) {
		side |= TOP;
		by = y + h - 1;
		ty = SPACE;
	} else {
		side |= BOTTOM;
		by = y - (height + SPACE);
		ty = 0;
	}
	WCairoRenderBalloon(scr, width, height + SPACE,
			    0.5, ((side & BOTTOM) ? 0 : SPACE) + 0.5,
			    width - 1, height - 1,
			    ((side & BOTTOM) ? WCairoBalloonBottom : 0)
			    | ((side & RIGHT) ? WCairoBalloonRight : 0),
			    SPACE, scr->white, scr->black, &pixmap, &mask);

	W_PaintText(bPtr->view, pixmap, font, 8, ty + (height - textHeight) / 2,
		    width, bPtr->flags.alignment,
		    bPtr->textColor ? bPtr->textColor : scr->black, False, text, strlen(text));

	XSetWindowBackgroundPixmap(dpy, bPtr->view->window, pixmap);

	W_ResizeView(bPtr->view, width, height + SPACE);

	XFreePixmap(dpy, pixmap);

#ifdef USE_XSHAPE
	xcb_shape_mask(XGetXCBConnection(dpy), XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING,
		       bPtr->view->window, 0, 0, mask);
#endif
	XFreePixmap(dpy, mask);

	W_MoveView(bPtr->view, bx, by);

	W_MapView(bPtr->view);
}

static void handleEvents(XEvent * event, void *data)
{
	Balloon *bPtr = (Balloon *) data;

	switch (event->type) {
	case DestroyNotify:
		destroyBalloon(bPtr);
		break;
	}
}

static void destroyBalloon(Balloon * bPtr)
{
	WMHashEnumerator e;
	char *str;

	e = WMEnumerateHashTable(bPtr->table);

	while ((str = WMNextHashEnumeratorItem(&e))) {
		wfree(str);
	}
	WMFreeHashTable(bPtr->table);

	if (bPtr->textColor)
		WMReleaseColor(bPtr->textColor);

	if (bPtr->font)
		WMReleaseFont(bPtr->font);

	wfree(bPtr);
}
