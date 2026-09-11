/*
 * Demo user widget for WINGs
 *
 * Author: Alfredo K. Kojima
 *
 * This file is in the public domain.
 *
 */

/*
 *
 * Include the WINGs private data header.
 *
 *
 */
#include <WINGs/WINGsP.h>

/*
 * Our public header.
 */
#include "mywidget.h"

/*
 * Define the widget "class"
 */
typedef struct W_MyWidget {
	/* these two fields must be present in all your widgets in this
	 * exact position */
	W_Class widgetClass;
	WMView *view;

	/* put your stuff here */
	char *text;

} _MyWidget;

/* some forward declarations */

static void destroyMyWidget(_MyWidget * mPtr);
static void paintMyWidget(_MyWidget * mPtr);

static void handleEvents(xcb_generic_event_t * event, void *data);
static void handleActionEvents(xcb_generic_event_t * event, void *data);

/*
 * Delegates
 * See the source for the other widgets to see how to use.
 * You won't need to use this most of the time.
 */
static W_ViewDelegate _MyWidgetDelegate = {
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};

/* our widget class ID */
static W_Class myWidgetClass = 0;

/*
 * Initializer for our widget. Must be called before creating any
 * instances of the widget.
 */
W_Class InitMyWidget(WMScreen * scr)
{
	(void)scr;
	/* register our widget with WINGs and get our widget class ID */
	if (!myWidgetClass) {
		myWidgetClass = W_RegisterUserWidget();
	}

	return myWidgetClass;
}

/*
 * Our widget fabrication plant.
 */
MyWidget *CreateMyWidget(WMWidget * parent)
{
	MyWidget *mPtr;

	/* allocate some storage for our new widget instance */
	mPtr = wmalloc(sizeof(MyWidget));
	/* initialize it */
	memset(mPtr, 0, sizeof(MyWidget));

	/* set the class ID */
	mPtr->widgetClass = myWidgetClass;

	/*
	 * Create the view for our widget.
	 * Note: the Window for the view is only created after the view is
	 * realized with W_RealizeView()
	 *
	 * Consider the returned view as read-only.
	 */
	mPtr->view = W_CreateView(W_VIEW(parent));
	if (!mPtr->view) {
		wfree(mPtr);
		return NULL;
	}
	/* always do this */
	mPtr->view->self = mPtr;

	/* setup the delegates for the view */
	mPtr->view->delegate = &_MyWidgetDelegate;

	/*
	 * Intercept some events for our widget, so that we can handle them.
	 */
	WMCreateEventHandler(mPtr->view, XCB_EVENT_MASK_EXPOSURE	/* this allows us to know when we should paint */
			     | XCB_EVENT_MASK_STRUCTURE_NOTIFY,	/* this allows us to know things like when we are destroyed */
			     handleEvents, mPtr);

	/*
	 * Intercept some other events. This could be merged with the above
	 * call, but we separate for more organization.
	 */
	WMCreateEventHandler(mPtr->view, XCB_EVENT_MASK_BUTTON_PRESS, handleActionEvents, mPtr);

	return mPtr;
}

/*
 * Paint our widget contents.
 */
static void paintMyWidget(_MyWidget * mPtr)
{
	W_Screen *scr = mPtr->view->screen;
	WMColor *color;
	cairo_t *cr;

	if (mPtr->text) {

		color = WMWhiteColor(scr);

		cr = WCairoBeginForDrawable(scr, mPtr->view->window);
		if (cr) {
			W_PaintText(cr, scr->normalFont, 0, 0,
					 mPtr->view->size.width, WACenter, color, false,
					 mPtr->text, strlen(mPtr->text));
			WCairoEnd(cr);
		}

		WMReleaseColor(color);
	}
}

static void handleEvents(xcb_generic_event_t * event, void *data)
{
	_MyWidget *mPtr = (_MyWidget *) data;

	switch (event->response_type & 0x7f) {
	case XCB_EXPOSE:
		if (((xcb_expose_event_t *)event)->count != 0)
			break;
		paintMyWidget(mPtr);
		break;

	case XCB_DESTROY_NOTIFY:
		destroyMyWidget(mPtr);
		break;

	}
}

static void handleActionEvents(xcb_generic_event_t * event, void *data)
{
	_MyWidget *mPtr = (_MyWidget *) data;

	switch (event->response_type & 0x7f) {
	case XCB_BUTTON_PRESS:
		wXBell(mPtr->view->screen->display, 100);
		break;
	}
}

void SetMyWidgetText(MyWidget * mPtr, char *text)
{
	CHECK_CLASS(mPtr, myWidgetClass);

	if (mPtr->text)
		wfree(mPtr->text);

	mPtr->text = wstrdup(text);

	if (W_VIEW_MAPPED(mPtr->view)) {
		paintMyWidget(mPtr);
	}
}

static void destroyMyWidget(_MyWidget * mPtr)
{
	/*
	 * Free all data we allocated for our widget.
	 */

	if (mPtr->text)
		wfree(mPtr->text);

	wfree(mPtr);
}
