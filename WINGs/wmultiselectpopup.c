#include <cairo.h>

#include "wconfig.h"

#include <X11/keysym.h>

#include "WINGsP.h"

typedef struct W_MultiSelectPopUpButton {
	W_Class widgetClass;
	WMView *view;

	void *clientData;
	WMAction *action;

	char *caption;		/* shown when the selection is empty */

	WMArray *items;

	short highlightedItem;

	WMView *menuView;	/* override redirect popup menu */

	Pixmap menuPixmap;	/* background pixmap for menuView, freed on destroy */

	WMHandlerID timer;	/* for autoscroll */

	int scrollStartY;	/* for autoscroll */

	struct {
		unsigned int configured:1;
		unsigned int insideMenu:1;
		unsigned int enabled:1;
		unsigned int armed:1;	/* menu open by the current press; ignore its release */
	} flags;
} MultiSelectPopUpButton;

#define SCROLL_DELAY		10

#define DEFAULT_WIDTH	60
#define DEFAULT_HEIGHT 	20
#define DEFAULT_CAPTION	""

#define CHECK_X		 6
#define CHECK_INDENT	20

static void destroyMultiSelectPopUpButton(MultiSelectPopUpButton *bPtr);
static void paintMultiSelectPopUpButton(MultiSelectPopUpButton *bPtr);

static void handleEvents(XEvent *event, void *data);
static void handleActionEvents(XEvent *event, void *data);

static void resizeMenu(MultiSelectPopUpButton *bPtr);
static char *buildCaption(MultiSelectPopUpButton *bPtr);
static void paintCheckmark(cairo_t *cr, int y, int itemHeight);

WMMultiSelectPopUpButton *WMCreateMultiSelectPopUpButton(WMWidget *parent)
{
	MultiSelectPopUpButton *bPtr;
	W_Screen *scr = W_VIEW(parent)->screen;

	bPtr = wmalloc(sizeof(MultiSelectPopUpButton));

	bPtr->widgetClass = WC_MultiSelectPopUpButton;

	bPtr->view = W_CreateView(W_VIEW(parent));
	if (!bPtr->view) {
		wfree(bPtr);
		return NULL;
	}
	bPtr->view->self = bPtr;

	WMCreateEventHandler(bPtr->view, ExposureMask | StructureNotifyMask
			     | ClientMessageMask, handleEvents, bPtr);

	W_ResizeView(bPtr->view, DEFAULT_WIDTH, DEFAULT_HEIGHT);
	bPtr->caption = wstrdup(DEFAULT_CAPTION);

	WMCreateEventHandler(bPtr->view, ButtonPressMask | ButtonReleaseMask,
			     handleActionEvents, bPtr);

	bPtr->flags.enabled = 1;

	bPtr->items = WMCreateArrayWithDestructor(4, (WMFreeDataProc *)WMDestroyMenuItem);

	bPtr->highlightedItem = -1;

	bPtr->menuView = W_CreateUnmanagedTopView(scr);

	W_ResizeView(bPtr->menuView, bPtr->view->size.width, 1);

	WMCreateEventHandler(bPtr->menuView, ButtonPressMask | ButtonReleaseMask
			     | EnterWindowMask | LeaveWindowMask | PointerMotionMask
			     | KeyPressMask | ExposureMask, handleActionEvents, bPtr);

	return bPtr;
}

void WMSetMultiSelectPopUpButtonAction(WMMultiSelectPopUpButton *bPtr,
					WMAction *action, void *clientData)
{
	CHECK_CLASS(bPtr, WC_MultiSelectPopUpButton);

	bPtr->action = action;
	bPtr->clientData = clientData;
}

WMMenuItem *WMAddMultiSelectPopUpButtonItem(WMMultiSelectPopUpButton *bPtr, const char *title)
{
	WMMenuItem *item;

	CHECK_CLASS(bPtr, WC_MultiSelectPopUpButton);

	if (!title)
		return NULL;

	item = WMCreateMenuItem();
	WMSetMenuItemTitle(item, title);

	WMAddToArray(bPtr->items, item);

	if (bPtr->menuView && bPtr->menuView->flags.realized)
		resizeMenu(bPtr);

	return item;
}

void WMRemoveAllMultiSelectPopUpButtonItems(WMMultiSelectPopUpButton *bPtr)
{
	CHECK_CLASS(bPtr, WC_MultiSelectPopUpButton);

	WMEmptyArray(bPtr->items);

	if (bPtr->menuView && bPtr->menuView->flags.realized)
		resizeMenu(bPtr);
	if (bPtr->view->flags.mapped)
		paintMultiSelectPopUpButton(bPtr);
}

void WMRemoveMultiSelectPopUpButtonItem(WMMultiSelectPopUpButton *bPtr, int index)
{
	CHECK_CLASS(bPtr, WC_MultiSelectPopUpButton);

	wassertr(index >= 0 && index < WMGetArrayItemCount(bPtr->items));

	WMDeleteFromArray(bPtr->items, index);

	if (bPtr->menuView && bPtr->menuView->flags.realized)
		resizeMenu(bPtr);
	if (bPtr->view->flags.mapped)
		paintMultiSelectPopUpButton(bPtr);
}

void WMSetMultiSelectPopUpButtonEnabled(WMMultiSelectPopUpButton *bPtr, Bool flag)
{
	CHECK_CLASS(bPtr, WC_MultiSelectPopUpButton);
	bPtr->flags.enabled = ((flag == 0) ? 0 : 1);
	if (bPtr->view->flags.mapped)
		paintMultiSelectPopUpButton(bPtr);
}

Bool WMGetMultiSelectPopUpButtonEnabled(WMMultiSelectPopUpButton *bPtr)
{
	CHECK_CLASS(bPtr, WC_MultiSelectPopUpButton);
	return bPtr->flags.enabled;
}

void WMSetMultiSelectPopUpButtonCaption(WMMultiSelectPopUpButton *bPtr, const char *text)
{
	CHECK_CLASS(bPtr, WC_MultiSelectPopUpButton);
	if (bPtr->caption)
		wfree(bPtr->caption);
	bPtr->caption = wstrdup(text ? text : "");
	if (bPtr->view->flags.mapped)
		paintMultiSelectPopUpButton(bPtr);
}

void WMSetMultiSelectPopUpButtonItemEnabled(WMMultiSelectPopUpButton *bPtr, int index, Bool flag)
{
	CHECK_CLASS(bPtr, WC_MultiSelectPopUpButton);
	WMSetMenuItemEnabled(WMGetFromArray(bPtr->items, index), (flag ? 1 : 0));
}

Bool WMGetMultiSelectPopUpButtonItemEnabled(WMMultiSelectPopUpButton *bPtr, int index)
{
	CHECK_CLASS(bPtr, WC_MultiSelectPopUpButton);
	return WMGetMenuItemEnabled(WMGetFromArray(bPtr->items, index));
}

int WMGetMultiSelectPopUpButtonNumberOfItems(WMMultiSelectPopUpButton *bPtr)
{
	CHECK_CLASS(bPtr, WC_MultiSelectPopUpButton);
	return WMGetArrayItemCount(bPtr->items);
}

char *WMGetMultiSelectPopUpButtonItem(WMMultiSelectPopUpButton *bPtr, int index)
{
	CHECK_CLASS(bPtr, WC_MultiSelectPopUpButton);
	if (index < 0 || index >= WMGetArrayItemCount(bPtr->items))
		return NULL;
	return WMGetMenuItemTitle(WMGetFromArray(bPtr->items, index));
}

WMMenuItem *WMGetMultiSelectPopUpButtonMenuItem(WMMultiSelectPopUpButton *bPtr, int index)
{
	CHECK_CLASS(bPtr, WC_MultiSelectPopUpButton);
	return WMGetFromArray(bPtr->items, index);
}

void WMSetMultiSelectPopUpButtonSelectionMask(WMMultiSelectPopUpButton *bPtr, uint64_t mask)
{
	int i, n;

	CHECK_CLASS(bPtr, WC_MultiSelectPopUpButton);
	n = WMGetArrayItemCount(bPtr->items);
	if (n > 64)
		n = 64;
	for (i = 0; i < n; i++) {
		WMMenuItem *item = WMGetFromArray(bPtr->items, i);

		WMSetMenuItemState(item, (mask & ((uint64_t)1 << i)) ? 1 : 0);
	}
	if (bPtr->view->flags.mapped)
		paintMultiSelectPopUpButton(bPtr);
}

uint64_t WMGetMultiSelectPopUpButtonSelectionMask(WMMultiSelectPopUpButton *bPtr)
{
	uint64_t mask = 0;
	int i, n;

	CHECK_CLASS(bPtr, WC_MultiSelectPopUpButton);
	n = WMGetArrayItemCount(bPtr->items);
	if (n > 64)
		n = 64;
	for (i = 0; i < n; i++) {
		WMMenuItem *item = WMGetFromArray(bPtr->items, i);

		if (WMGetMenuItemState(item))
			mask |= (uint64_t)1 << i;
	}
	return mask;
}

static char *buildCaption(MultiSelectPopUpButton *bPtr)
{
	int i, selected = 0, itemCount;
	char buf[256];
	int len = 0;

	itemCount = WMGetArrayItemCount(bPtr->items);
	for (i = 0; i < itemCount; i++) {
		WMMenuItem *item = WMGetFromArray(bPtr->items, i);

		if (WMGetMenuItemState(item))
			selected++;
	}

	if (selected == 0)
		return wstrdup(bPtr->caption ? bPtr->caption : "");

	if (selected == itemCount && itemCount > 0)
		return wstrdup(_("All"));

	buf[0] = '\0';
	for (i = 0; i < itemCount; i++) {
		WMMenuItem *item = WMGetFromArray(bPtr->items, i);

		if (WMGetMenuItemState(item)) {
			char *title = WMGetMenuItemTitle(item);
			int tl = title ? strlen(title) : 0;

			if (len + tl + 2 >= (int)sizeof(buf) - 4) {
				strcpy(buf + len, "...");
				len += 3;
				break;
			}
			if (len > 0) {
				buf[len++] = ',';
				buf[len++] = ' ';
			}
			memcpy(buf + len, title, tl);
			len += tl;
		}
	}
	buf[len] = '\0';
	return wstrdup(buf);
}

static void paintCheckmark(cairo_t *cr, int y, int itemHeight)
{
	double cx = CHECK_X + 1;
	double cy = y + itemHeight / 2.0;

	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_set_line_width(cr, 2.0);
	cairo_move_to(cr, cx, cy - 1);
	cairo_line_to(cr, cx + 2.5, cy + 2.5);
	cairo_line_to(cr, cx + 7, cy - 3.5);
	cairo_stroke(cr);
}

static void paintMultiSelectPopUpButton(MultiSelectPopUpButton *bPtr)
{
	W_Screen *scr = bPtr->view->screen;
	WMPixmap *ind;
	char *caption;
	cairo_t *cr;

	caption = buildCaption(bPtr);

	cr = WCairoBeginBuffered(scr, bPtr->view->window);
	if (!cr) {
		wfree(caption);
		return;
	}
	WCairoSetSourceColor(cr, scr->gray);
	cairo_paint(cr);

	W_DrawReliefCairo(cr, scr, 0, 0, bPtr->view->size.width, bPtr->view->size.height, WRRaised);

	if (caption && caption[0]) {
		W_PaintTextCairo(cr, scr->normalFont, 6,
				 (bPtr->view->size.height - WMFontHeight(scr->normalFont)) / 2,
				 bPtr->view->size.width - 20, WALeft,
				 bPtr->flags.enabled ? scr->black : scr->darkGray,
				 False, caption, strlen(caption));
	}

	ind = scr->popUpIndicator;
	WCairoPaintPixmap(cr, ind,
			 bPtr->view->size.width - ind->width - 4,
			 (bPtr->view->size.height - ind->height) / 2);

	WCairoEnd(cr);
	wfree(caption);
}

static void handleEvents(XEvent *event, void *data)
{
	MultiSelectPopUpButton *bPtr = (MultiSelectPopUpButton *)data;

	CHECK_CLASS(data, WC_MultiSelectPopUpButton);

	switch (event->type) {
	case Expose:
		if (event->xexpose.count != 0)
			break;
		paintMultiSelectPopUpButton(bPtr);
		break;

	case DestroyNotify:
		destroyMultiSelectPopUpButton(bPtr);
		break;
	}
}

static void paintMenuEntry(MultiSelectPopUpButton *bPtr, int index, int highlight)
{
	W_Screen *scr = bPtr->view->screen;
	int yo, width, itemHeight, itemCount;
	char *title;
	cairo_t *cr;

	itemCount = WMGetArrayItemCount(bPtr->items);
	if (index < 0 || index >= itemCount)
		return;

	itemHeight = bPtr->view->size.height;
	width = bPtr->view->size.width;
	yo = (itemHeight - WMFontHeight(scr->normalFont)) / 2;

	cr = WCairoBeginForDrawable(scr, bPtr->menuView->window);
	if (!cr)
		return;

	/* unhighlight paints the gray menu background, highlight paints a white
	 * selection box. Both repaint the checkmark and text from the current
	 * item state, since the selection can change while the menu stays open */
	if (highlight)
		WCairoSetSourceColor(cr, scr->white);
	else
		WCairoSetSourceColor(cr, scr->gray);
	cairo_rectangle(cr, 1, index * itemHeight + 1, width - 3, itemHeight - 3);
	cairo_fill(cr);

	title = WMGetMultiSelectPopUpButtonItem(bPtr, index);

	W_DrawReliefCairo(cr, scr, 0, index * itemHeight, width, itemHeight, WRRaised);

	if (WMGetMenuItemState(WMGetFromArray(bPtr->items, index)))
		paintCheckmark(cr, index * itemHeight, itemHeight);

	W_PaintTextCairo(cr, scr->normalFont, CHECK_INDENT,
			 index * itemHeight + yo, width, WALeft, scr->black,
			 False, title, strlen(title));

	WCairoEnd(cr);
}

static Pixmap makeMenuPixmap(MultiSelectPopUpButton *bPtr)
{
	Pixmap pixmap;
	W_Screen *scr = bPtr->view->screen;
	WMMenuItem *item;
	WMArrayIterator iter;
	WMColor *color;
	char *text;
	cairo_t *cr;
	int yo, i;
	int width, height, itemHeight;

	itemHeight = bPtr->view->size.height;
	width = bPtr->view->size.width;
	height = itemHeight * WMGetArrayItemCount(bPtr->items);
	yo = (itemHeight - WMFontHeight(scr->normalFont)) / 2;

	pixmap = W_CreateScaledPixmap(scr, width, height);

	cr = WCairoBeginForDrawable(scr, pixmap);
	WCairoSetSourceColor(cr, scr->gray);
	cairo_paint(cr);

	i = 0;
	WM_ITERATE_ARRAY(bPtr->items, item, iter) {
		text = WMGetMenuItemTitle(item);

		W_DrawReliefCairo(cr, scr, 0, i * itemHeight, width, itemHeight, WRRaised);

		if (WMGetMenuItemState(item))
			paintCheckmark(cr, i * itemHeight, itemHeight);

		color = WMGetMenuItemEnabled(item) ? scr->black : scr->darkGray;

		W_PaintTextCairo(cr, scr->normalFont, CHECK_INDENT,
				 i * itemHeight + yo, width, WALeft, color,
				 False, text, strlen(text));

		i++;
	}
	WCairoEnd(cr);

	return pixmap;
}

static void resizeMenu(MultiSelectPopUpButton *bPtr)
{
	int height;

	height = WMGetArrayItemCount(bPtr->items) * bPtr->view->size.height;
	if (height > 0)
		W_ResizeView(bPtr->menuView, bPtr->view->size.width, height);
}

static void popUpMenu(MultiSelectPopUpButton *bPtr)
{
	W_Screen *scr = bPtr->view->screen;
	Window dummyW;
	int scrHeight = scr->rootView->size.height;
	int x, y;

	if (!bPtr->flags.enabled)
		return;

	if (WMGetArrayItemCount(bPtr->items) < 1)
		return;

	if (!bPtr->menuView->flags.realized) {
		W_RealizeView(bPtr->menuView);
		resizeMenu(bPtr);
	}

	XTranslateCoordinates(scr->display, bPtr->view->window, scr->rootWin,
			      0, 0, &x, &y, &dummyW);

	y += bPtr->view->size.height;

	if (W_VIEW_WIDTH(bPtr->menuView) != W_VIEW_WIDTH(bPtr->view)
	    || W_VIEW_HEIGHT(bPtr->menuView) != W_VIEW_HEIGHT(bPtr->view))
		resizeMenu(bPtr);

	/* menus taller than the screen keep the bottom-edge autoscroll */
	if (y + bPtr->menuView->size.height > scrHeight
	    && bPtr->menuView->size.height <= scrHeight)
		y = scrHeight - bPtr->menuView->size.height;
	if (y < 0)
		y = 0;

	W_MoveView(bPtr->menuView, x, y);

	if (bPtr->menuPixmap != None)
		XFreePixmap(scr->display, bPtr->menuPixmap);
	bPtr->menuPixmap = makeMenuPixmap(bPtr);
	XSetWindowBackgroundPixmap(scr->display, bPtr->menuView->window, bPtr->menuPixmap);
	XClearWindow(scr->display, bPtr->menuView->window);

	W_MapView(bPtr->menuView);

	/* trigger a full expose so the Expose handler repaints every entry
	 * with the current checkmark state */
	XClearWindow(scr->display, bPtr->menuView->window);

	bPtr->highlightedItem = -1;

	XGrabKeyboard(scr->display, bPtr->menuView->window, False,
		      GrabModeAsync, GrabModeAsync, CurrentTime);
}

static void popDownMenu(MultiSelectPopUpButton *bPtr)
{
	W_Screen *scr = bPtr->view->screen;

	XUngrabKeyboard(scr->display, CurrentTime);
	W_UnmapView(bPtr->menuView);
	bPtr->flags.insideMenu = 0;
	bPtr->highlightedItem = -1;
}

static void autoScroll(void *data)
{
	MultiSelectPopUpButton *bPtr = (MultiSelectPopUpButton *)data;
	int scrHeight = WMWidgetScreen(bPtr)->rootView->size.height;
	int repeat = 0;
	int dy = 0;

	if (bPtr->scrollStartY >= scrHeight - 1
	    && bPtr->menuView->pos.y + bPtr->menuView->size.height >= scrHeight - 1) {
		repeat = 1;
		if (bPtr->menuView->pos.y + bPtr->menuView->size.height - 5 <= scrHeight - 1)
			dy = scrHeight - 1 - (bPtr->menuView->pos.y + bPtr->menuView->size.height);
		else
			dy = -5;
	} else if (bPtr->scrollStartY <= 1 && bPtr->menuView->pos.y < 1) {
		repeat = 1;
		if (bPtr->menuView->pos.y + 5 > 1)
			dy = 1 - bPtr->menuView->pos.y;
		else
			dy = 5;
	}

	if (repeat) {
		int oldItem;

		W_MoveView(bPtr->menuView, bPtr->menuView->pos.x, bPtr->menuView->pos.y + dy);

		oldItem = bPtr->highlightedItem;
		bPtr->highlightedItem = (bPtr->scrollStartY - bPtr->menuView->pos.y)
		    / bPtr->view->size.height;

		if (oldItem != bPtr->highlightedItem) {
			WMMenuItem *item;

			paintMenuEntry(bPtr, oldItem, False);
			if (bPtr->highlightedItem >= 0 &&
			    bPtr->highlightedItem < WMGetArrayItemCount(bPtr->items)) {
				item = WMGetMultiSelectPopUpButtonMenuItem(bPtr, bPtr->highlightedItem);
				paintMenuEntry(bPtr, bPtr->highlightedItem, WMGetMenuItemEnabled(item));
			} else {
				bPtr->highlightedItem = -1;
			}
		}

		bPtr->timer = WMAddTimerHandler(SCROLL_DELAY, autoScroll, bPtr);
	} else {
		bPtr->timer = NULL;
	}
}

static void handleActionEvents(XEvent *event, void *data)
{
	MultiSelectPopUpButton *bPtr = (MultiSelectPopUpButton *)data;
	int oldItem;
	int scrHeight = WMWidgetScreen(bPtr)->rootView->size.height;
	int i, n, first, last, itemHeight;

	CHECK_CLASS(data, WC_MultiSelectPopUpButton);

	if (WMGetArrayItemCount(bPtr->items) < 1)
		return;

	switch (event->type) {
	case Expose:
		itemHeight = bPtr->view->size.height;
		n = WMGetArrayItemCount(bPtr->items);
		first = event->xexpose.y / itemHeight;
		last = (event->xexpose.y + event->xexpose.height - 1) / itemHeight;
		if (last > n - 1)
			last = n - 1;
		for (i = first; i <= last; i++)
			paintMenuEntry(bPtr, i, i == bPtr->highlightedItem);
		break;

	case LeaveNotify:
		bPtr->flags.insideMenu = 0;
		if (bPtr->menuView->flags.mapped)
			paintMenuEntry(bPtr, bPtr->highlightedItem, False);
		bPtr->highlightedItem = -1;
		break;

	case EnterNotify:
		bPtr->flags.insideMenu = 1;
		break;

	case KeyPress:
		if (bPtr->menuView->flags.mapped) {
			KeySym ksym;

			XLookupString(&event->xkey, NULL, 0, &ksym, NULL);
			if (ksym == XK_Escape) {
				WMDeleteTimerHandlerPtr(&bPtr->timer);
				XUngrabPointer(bPtr->view->screen->display, CurrentTime);
				popDownMenu(bPtr);
				if (bPtr->view->flags.mapped)
					paintMultiSelectPopUpButton(bPtr);
			}
		}
		break;

	case MotionNotify:
		if (bPtr->flags.insideMenu) {
			oldItem = bPtr->highlightedItem;
			bPtr->highlightedItem = event->xmotion.y / bPtr->view->size.height;
			if (oldItem != bPtr->highlightedItem) {
				WMMenuItem *item;

				paintMenuEntry(bPtr, oldItem, False);
				if (bPtr->highlightedItem >= 0 &&
				    bPtr->highlightedItem < WMGetArrayItemCount(bPtr->items)) {
					item = WMGetMultiSelectPopUpButtonMenuItem(bPtr, bPtr->highlightedItem);
					paintMenuEntry(bPtr, bPtr->highlightedItem, WMGetMenuItemEnabled(item));
				} else {
					bPtr->highlightedItem = -1;
				}
			}

			if (event->xmotion.y_root >= scrHeight - 1 || event->xmotion.y_root <= 1) {
				bPtr->scrollStartY = event->xmotion.y_root;
				if (!bPtr->timer)
					autoScroll(bPtr);
			} else {
				WMDeleteTimerHandlerPtr(&bPtr->timer);
			}
		}
		break;

	case ButtonPress:
		if (!bPtr->flags.enabled)
			break;

		if (event->xbutton.button == WINGsConfiguration.mouseWheelUp ||
		    event->xbutton.button == WINGsConfiguration.mouseWheelDown)
			break;

		if (bPtr->menuView->flags.mapped) {
			/* menu already open (subsequent press during a
			 * multi-select session): just update the highlight
			 * without re-popping or re-grabbing */
			if (bPtr->flags.insideMenu && bPtr->highlightedItem >= 0)
				paintMenuEntry(bPtr, bPtr->highlightedItem, False);
			bPtr->highlightedItem = event->xbutton.y / bPtr->view->size.height;
			if (bPtr->highlightedItem >= 0 &&
			    bPtr->highlightedItem < WMGetArrayItemCount(bPtr->items)) {
				WMMenuItem *item = WMGetMultiSelectPopUpButtonMenuItem(bPtr, bPtr->highlightedItem);

				paintMenuEntry(bPtr, bPtr->highlightedItem, WMGetMenuItemEnabled(item));
			} else {
				bPtr->highlightedItem = -1;
			}
			break;
		}

		popUpMenu(bPtr);
		bPtr->highlightedItem = -1;
		bPtr->flags.insideMenu = 1;
		bPtr->flags.armed = 1;
		XGrabPointer(bPtr->view->screen->display, bPtr->menuView->window,
			     False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask
			     | EnterWindowMask | LeaveWindowMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
		break;

	case ButtonRelease:
		WMDeleteTimerHandlerPtr(&bPtr->timer);

		if (event->xbutton.button == WINGsConfiguration.mouseWheelUp ||
		    event->xbutton.button == WINGsConfiguration.mouseWheelDown)
			break;

		/* the release that opened the menu must not toggle anything */
		if (bPtr->flags.armed) {
			bPtr->flags.armed = 0;
			break;
		}

		if (bPtr->flags.insideMenu && bPtr->highlightedItem >= 0
		    && bPtr->highlightedItem < WMGetArrayItemCount(bPtr->items)) {
			WMMenuItem *item = WMGetMultiSelectPopUpButtonMenuItem(bPtr, bPtr->highlightedItem);

			if (WMGetMenuItemEnabled(item)) {
				/* toggle the item, keep the menu open */
				int state = WMGetMenuItemState(item) ? 0 : 1;
				W_Screen *scr = bPtr->view->screen;

				WMSetMenuItemState(item, state);
				paintMenuEntry(bPtr, bPtr->highlightedItem, True);
				/* refresh the menu background pixmap so a later
				 * expose reflects the new checkmark state */
				if (bPtr->menuPixmap != None)
					XFreePixmap(scr->display, bPtr->menuPixmap);
				bPtr->menuPixmap = makeMenuPixmap(bPtr);
				XSetWindowBackgroundPixmap(scr->display,
							   bPtr->menuView->window,
							   bPtr->menuPixmap);
				paintMultiSelectPopUpButton(bPtr);
				if (bPtr->action)
					(*bPtr->action)(bPtr, bPtr->clientData);
				break;
			}
		}

		/* release outside the menu, or on a disabled item: close */
		XUngrabPointer(bPtr->view->screen->display, CurrentTime);
		popDownMenu(bPtr);
		paintMultiSelectPopUpButton(bPtr);
		break;
	}
}

static void destroyMultiSelectPopUpButton(MultiSelectPopUpButton *bPtr)
{
	if (bPtr->timer)
		WMDeleteTimerHandler(bPtr->timer);

	WMFreeArray(bPtr->items);

	if (bPtr->caption)
		wfree(bPtr->caption);

	if (bPtr->menuPixmap != None)
		XFreePixmap(bPtr->view->screen->display, bPtr->menuPixmap);

	W_DestroyView(bPtr->menuView);

	wfree(bPtr);
}
