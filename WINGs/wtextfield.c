/*
 *  WINGs WMTextField: single-line text entry widget
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
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

#include <xkbcommon/xkbcommon-keysyms.h>

#include <ctype.h>

#define CURSOR_BLINK_ON_DELAY	600
#define CURSOR_BLINK_OFF_DELAY	300

const char *WMTextDidChangeNotification = "WMTextDidChangeNotification";
const char *WMTextDidBeginEditingNotification = "WMTextDidBeginEditingNotification";
const char *WMTextDidEndEditingNotification = "WMTextDidEndEditingNotification";

typedef struct W_TextField {
	W_Class widgetClass;
	W_View *view;

#if 0
	struct W_TextField *nextField;	/* next textfield in the chain */
	struct W_TextField *prevField;
#endif

	char *text;
	int textLen;		/* size of text */
	int bufferSize;		/* memory allocated for text */

	char *placeholderText;	/* hint shown while the field is empty */

	int viewPosition;	/* position of text being shown */

	int cursorPosition;	/* position of the insertion cursor */

	short usableWidth;
	short offsetWidth;	/* offset of text from border */

	WMRange selection;

	char *clipText;		/* CLIPBOARD snapshot taken at copy/cut time */
	int clipLen;

	WMFont *font;

	WMTextFieldDelegate *delegate;

	WMHandlerID timerID;	/* for cursor blinking */

	WMHandlerID pasteTimer;	/* for the awaited SelectionNotify */

	struct {
		WMAlignment alignment:2;

		unsigned int bordered:1;

		unsigned int beveled:1;

		unsigned int enabled:1;

		unsigned int focused:1;

		unsigned int cursorOn:1;

		unsigned int secure:1;	/* password entry style */

		unsigned int pointerGrabbed:1;

		unsigned int ownsSelection:1;

		unsigned int ownsClipboard:1;

		unsigned int waitingSelection:1;	/* requested selection, but
							 * didnt get yet */

		unsigned int notIllegalMovement:1;
	} flags;
} TextField;

#define NOTIFY(T,C,N,A) { WMNotification *notif = WMCreateNotification(N,T,A);\
    if ((T)->delegate && (T)->delegate->C)\
    (*(T)->delegate->C)((T)->delegate,notif);\
    WMPostNotification(notif);\
    WMReleaseNotification(notif);}

#define MIN_TEXT_BUFFER		2
#define TEXT_BUFFER_INCR	8

#define WM_EMACSKEYMASK   XCB_MOD_MASK_CONTROL

#define WM_EMACSKEY_LEFT  XKB_KEY_b
#define WM_EMACSKEY_RIGHT XKB_KEY_f
#define WM_EMACSKEY_HOME  XKB_KEY_a
#define WM_EMACSKEY_END   XKB_KEY_e
#define WM_EMACSKEY_BS    XKB_KEY_h
#define WM_EMACSKEY_DEL   XKB_KEY_d

#define DEFAULT_WIDTH		60
#define DEFAULT_HEIGHT		20
#define DEFAULT_BORDERED	true
#define DEFAULT_ALIGNMENT	WALeft

static void destroyTextField(TextField * tPtr);
static void paintTextField(TextField * tPtr);

static void handleEvents(xcb_generic_event_t * event, void *data);
static void handleTextFieldActionEvents(xcb_generic_event_t * event, void *data);
static void didResizeTextField(W_ViewDelegate * self, WMView * view);

struct W_ViewDelegate _TextFieldViewDelegate = {
	NULL,
	NULL,
	didResizeTextField,
	NULL,
	NULL
};

static void lostSelection(WMView * view, xcb_atom_t selection, void *cdata);

static WMData *requestHandler(WMView * view, xcb_atom_t selection, xcb_atom_t target, void *cdata, xcb_atom_t * type);

static WMSelectionProcs selectionHandler = {
	requestHandler,
	lostSelection,
	NULL
};

#define TEXT_WIDTH(tPtr, start) (WMWidthOfString((tPtr)->font, \
    &((tPtr)->text[(start)]), (tPtr)->textLen - (start)))

#define TEXT_WIDTH2(tPtr, start, end) (WMWidthOfString((tPtr)->font, \
    &((tPtr)->text[(start)]), (end) - (start)))

static void normalizeRange(TextField * tPtr, WMRange * range)
{
	if (range->position < 0 && range->count < 0)
		range->count = 0;

	if (range->count == 0) {
		/*range->position = 0; why is this? */
		return;
	}

	/* (1,-2) ~> (0,1) ; (1,-1) ~> (0,1) ; (2,-1) ~> (1,1) */
	if (range->count < 0) {	/* && range->position >= 0 */
		if (range->position + range->count < 0) {
			range->count = range->position;
			range->position = 0;
		} else {
			range->count = -range->count;
			range->position -= range->count;
		}
		/* (-2,1) ~> (0,0) ; (-1,1) ~> (0,0) ; (-1,2) ~> (0,1) */
	} else if (range->position < 0) {	/* && range->count > 0 */
		if (range->position + range->count < 0) {
			range->position = range->count = 0;
		} else {
			range->count += range->position;
			range->position = 0;
		}
	}

	if (range->position >= tPtr->textLen) {
		range->position = tPtr->textLen;
		range->count = 0;
	} else if (range->position + range->count > tPtr->textLen) {
		range->count = tPtr->textLen - range->position;
	}
}

static void memmv(char *dest, const char *src, int size)
{
	int i;

	if (dest > src) {
		for (i = size - 1; i >= 0; i--) {
			dest[i] = src[i];
		}
	} else if (dest < src) {
		for (i = 0; i < size; i++) {
			dest[i] = src[i];
		}
	}
}

/* Snapshot the current selection for CLIPBOARD so a later paste from
 * another window gets the bytes that were selected at copy time, not
 * the live (possibly empty) selection range. */
static void snapshotClipboard(TextField *tPtr)
{
	int start = tPtr->selection.count < 0
	    ? tPtr->selection.position + tPtr->selection.count
	    : tPtr->selection.position;
	int len = abs(tPtr->selection.count);

	wfree(tPtr->clipText);
	tPtr->clipText = wmalloc(len + 1);
	if (len > 0)
		memcpy(tPtr->clipText, &tPtr->text[start], len);
	tPtr->clipText[len] = '\0';
	tPtr->clipLen = len;
}

static int incrToFit(TextField * tPtr)
{
	int vp = tPtr->viewPosition;

	while (tPtr->usableWidth > 0
	       && TEXT_WIDTH(tPtr, tPtr->viewPosition) > tPtr->usableWidth) {
		int adv = oneUTF8CharForward(&tPtr->text[tPtr->viewPosition],
					     tPtr->textLen - tPtr->viewPosition);
		if (adv <= 0)
			break;
		tPtr->viewPosition += adv;
	}
	return vp != tPtr->viewPosition;
}

static int incrToFit2(TextField * tPtr)
{
	int vp = tPtr->viewPosition;

	while (tPtr->usableWidth > 0
	       && TEXT_WIDTH2(tPtr, tPtr->viewPosition, tPtr->cursorPosition)
		  >= tPtr->usableWidth) {
		int adv = oneUTF8CharForward(&tPtr->text[tPtr->viewPosition],
					     tPtr->cursorPosition - tPtr->viewPosition);
		if (adv <= 0)
			break;
		tPtr->viewPosition += adv;
	}
	return vp != tPtr->viewPosition;
}

static void decrToFit(TextField * tPtr)
{
	int vp = tPtr->viewPosition;

	while (tPtr->usableWidth > 0 && vp > 0) {
		int adv = oneUTF8CharBackward(&tPtr->text[vp], vp);
		if (adv == 0)
			break;
		vp += adv;
		if (TEXT_WIDTH(tPtr, vp) >= tPtr->usableWidth)
			break;
		tPtr->viewPosition = vp;
	}
}

#undef TEXT_WIDTH
#undef TEXT_WIDTH2

static WMData *requestHandler(WMView * view, xcb_atom_t selection, xcb_atom_t target, void *cdata, xcb_atom_t * type)
{
	TextField *tPtr = view->self;
	int count;
	WMData *data;
	const char *src;
	int srclen;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) cdata;

	if (target == XCB_ATOM_STRING || target == wx_static_atoms[W_ATOM_TEXT]
	    || target == wx_static_atoms[W_ATOM_COMPOUND_TEXT]) {
		if (selection == tPtr->view->screen->clipboardAtom) {
			src = tPtr->clipText ? tPtr->clipText : "";
			srclen = tPtr->clipLen;
		} else {
			count = tPtr->selection.count < 0
			    ? tPtr->selection.position + tPtr->selection.count
			    : tPtr->selection.position;
			src = &tPtr->text[count];
			srclen = abs(tPtr->selection.count);
		}
		data = WMCreateDataWithBytes(src, srclen);
		WMSetDataFormat(data, 8);
		*type = target;

		return data;
	}

	if (target == wx_static_atoms[W_ATOM_TARGETS]) {
		/* format-32 property payload: writeSelection sizes
		 * format-32 items as sizeof(long) (the LP64 contract),
		 * so the atoms ride in long-sized slots */
		unsigned long supported_type[4];

		supported_type[0] = wx_static_atoms[W_ATOM_TARGETS];
		supported_type[1] = XCB_ATOM_STRING;
		supported_type[2] = wx_static_atoms[W_ATOM_TEXT];
		supported_type[3] = wx_static_atoms[W_ATOM_COMPOUND_TEXT];

		data = WMCreateDataWithBytes(supported_type, sizeof(supported_type));
		WMSetDataFormat(data, 32);

		*type = XCB_ATOM_ATOM;
		return data;
	}

	return NULL;

}

static void lostSelection(WMView * view, xcb_atom_t selection, void *cdata)
{
	TextField *tPtr = (WMTextField *) view->self;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) cdata;

	if (selection == XCB_ATOM_PRIMARY && tPtr->flags.ownsSelection) {
		WMDeleteSelectionHandler(view, selection, XCB_CURRENT_TIME);
		tPtr->flags.ownsSelection = 0;
		if (!tPtr->flags.ownsClipboard && tPtr->selection.count != 0) {
			tPtr->selection.count = 0;
			paintTextField(tPtr);
		}
	} else if (selection != XCB_ATOM_PRIMARY && tPtr->flags.ownsClipboard) {
		WMDeleteSelectionHandler(view, selection, XCB_CURRENT_TIME);
		tPtr->flags.ownsClipboard = 0;
		wfree(tPtr->clipText);
		tPtr->clipText = NULL;
		tPtr->clipLen = 0;
	}
}

static void selectionNotification(void *observerData, WMNotification * notification)
{
	WMView *observerView = (WMView *) observerData;
	xcb_atom_t selection = (xcb_atom_t)(uintptr_t) WMGetNotificationObject(notification);
	WMView *newOwnerView = (WMView *) WMGetNotificationClientData(notification);

	if (observerView != newOwnerView) {
		lostSelection(observerView, selection, NULL);
	}
}

static void realizeObserver(void *self, WMNotification * not)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) not;

	W_CreateIC(((TextField *) self)->view);
}

WMTextField *WMCreateTextField(WMWidget * parent)
{
	TextField *tPtr;

	tPtr = wmalloc(sizeof(TextField));
	tPtr->widgetClass = WC_TextField;

	tPtr->view = W_CreateView(W_VIEW(parent));
	if (!tPtr->view) {
		wfree(tPtr);
		return NULL;
	}
	tPtr->view->self = tPtr;

	tPtr->view->delegate = &_TextFieldViewDelegate;

	tPtr->view->attribFlags |= XCB_CW_CURSOR;
	tPtr->view->attribs.cursor = tPtr->view->screen->textCursor;

	W_SetViewBackgroundColor(tPtr->view, W_TextFieldBackColor(tPtr->view->screen));

	tPtr->text = wmalloc(MIN_TEXT_BUFFER);
	tPtr->textLen = 0;
	tPtr->bufferSize = MIN_TEXT_BUFFER;

	tPtr->flags.enabled = 1;

	WMCreateEventHandler(tPtr->view, XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_FOCUS_CHANGE, handleEvents, tPtr);

	tPtr->font = WMRetainFont(tPtr->view->screen->normalFont);

	tPtr->flags.bordered = DEFAULT_BORDERED;
	tPtr->flags.beveled = true;
	tPtr->flags.alignment = DEFAULT_ALIGNMENT;
	tPtr->offsetWidth = WMAX((tPtr->view->size.height - WMFontHeight(tPtr->font)) / 2, 1);

	W_ResizeView(tPtr->view, DEFAULT_WIDTH, DEFAULT_HEIGHT);

	WMCreateEventHandler(tPtr->view, XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW
			     | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_BUTTON_1_MOTION,
			     handleTextFieldActionEvents, tPtr);

	WMAddNotificationObserver(selectionNotification, tPtr->view,
				  WMSelectionOwnerDidChangeNotification, (void *)XCB_ATOM_PRIMARY);
	WMAddNotificationObserver(selectionNotification, tPtr->view,
				  WMSelectionOwnerDidChangeNotification,
				  (void *)(uintptr_t) tPtr->view->screen->clipboardAtom);

	WMAddNotificationObserver(realizeObserver, tPtr, WMViewRealizedNotification, tPtr->view);

	tPtr->flags.cursorOn = 1;

	return tPtr;
}

void WMSetTextFieldDelegate(WMTextField * tPtr, WMTextFieldDelegate * delegate)
{
	CHECK_CLASS(tPtr, WC_TextField);

	tPtr->delegate = delegate;
}

WMTextFieldDelegate *WMGetTextFieldDelegate(WMTextField * tPtr)
{
	CHECK_CLASS(tPtr, WC_TextField);

	return tPtr->delegate;
}

void WMInsertTextFieldText(WMTextField * tPtr, const char *text, int position)
{
	int len;

	CHECK_CLASS(tPtr, WC_TextField);

	if (!text)
		return;

	len = strlen(text);

	/* check if buffer will hold the text */
	if (len + tPtr->textLen >= tPtr->bufferSize) {
		tPtr->bufferSize = tPtr->textLen + len + TEXT_BUFFER_INCR;
		tPtr->text = wrealloc(tPtr->text, tPtr->bufferSize);
	}

	if (position < 0 || position >= tPtr->textLen) {
		/* append the text at the end */
		wstrlcat(tPtr->text, text, tPtr->bufferSize);
		tPtr->textLen += len;
		tPtr->cursorPosition += len;
		incrToFit(tPtr);
	} else {
		/* insert text at position */
		memmv(&(tPtr->text[position + len]), &(tPtr->text[position]), tPtr->textLen - position + 1);

		memcpy(&(tPtr->text[position]), text, len);

		tPtr->textLen += len;
		if (position >= tPtr->cursorPosition) {
			tPtr->cursorPosition += len;
			incrToFit2(tPtr);
		} else {
			incrToFit(tPtr);
		}
	}

	paintTextField(tPtr);
}

void WMDeleteTextFieldRange(WMTextField * tPtr, WMRange range)
{
	CHECK_CLASS(tPtr, WC_TextField);

	normalizeRange(tPtr, &range);

	if (!range.count)
		return;

	memmv(&(tPtr->text[range.position]), &(tPtr->text[range.position + range.count]),
	      tPtr->textLen - (range.position + range.count) + 1);

	/* better than nothing ;) */
	if (tPtr->cursorPosition > range.position)
		tPtr->viewPosition += oneUTF8CharBackward(&tPtr->text[tPtr->viewPosition], tPtr->viewPosition);
	tPtr->textLen -= range.count;
	tPtr->cursorPosition = range.position;

	decrToFit(tPtr);

	/* Ensure cursor is visible after deletion */
	if (tPtr->cursorPosition < tPtr->viewPosition) {
		tPtr->viewPosition = tPtr->cursorPosition;
	} else {
		incrToFit2(tPtr);
	}

	paintTextField(tPtr);
}

char *WMGetTextFieldText(WMTextField * tPtr)
{
	CHECK_CLASS(tPtr, WC_TextField);

	return wstrdup(tPtr->text);
}

void WMSetTextFieldText(WMTextField * tPtr, const char *text)
{
	CHECK_CLASS(tPtr, WC_TextField);

	if ((text && strcmp(tPtr->text, text) == 0) || (!text && tPtr->textLen == 0))
		return;

	if (text == NULL) {
		tPtr->text[0] = 0;
		tPtr->textLen = 0;
	} else {
		tPtr->textLen = strlen(text);

		if (tPtr->textLen >= tPtr->bufferSize) {
			tPtr->bufferSize = tPtr->textLen + TEXT_BUFFER_INCR;
			tPtr->text = wrealloc(tPtr->text, tPtr->bufferSize);
		}
		wstrlcpy(tPtr->text, text, tPtr->bufferSize);
	}

	tPtr->cursorPosition = tPtr->selection.position = tPtr->textLen;
	tPtr->viewPosition = 0;
	tPtr->selection.count = 0;

	if (tPtr->view->flags.realized)
		paintTextField(tPtr);
}

void WMSetTextFieldPlaceholder(WMTextField * tPtr, const char *text)
{
	CHECK_CLASS(tPtr, WC_TextField);

	if (tPtr->placeholderText) {
		wfree(tPtr->placeholderText);
		tPtr->placeholderText = NULL;
	}

	if (text && text[0])
		tPtr->placeholderText = wstrdup(text);

	if (tPtr->view->flags.realized)
		paintTextField(tPtr);
}

void WMSetTextFieldAlignment(WMTextField * tPtr, WMAlignment alignment)
{
	CHECK_CLASS(tPtr, WC_TextField);

	tPtr->flags.alignment = alignment;

	if (tPtr->view->flags.realized) {
		paintTextField(tPtr);
	}
}

void WMSetTextFieldBordered(WMTextField * tPtr, bool bordered)
{
	CHECK_CLASS(tPtr, WC_TextField);

	tPtr->flags.bordered = bordered;

	if (tPtr->view->flags.realized) {
		paintTextField(tPtr);
	}
}

void WMSetTextFieldBeveled(WMTextField * tPtr, bool flag)
{
	CHECK_CLASS(tPtr, WC_TextField);

	tPtr->flags.beveled = ((flag == 0) ? 0 : 1);

	if (tPtr->view->flags.realized) {
		paintTextField(tPtr);
	}
}

void WMSetTextFieldSecure(WMTextField * tPtr, bool flag)
{
	CHECK_CLASS(tPtr, WC_TextField);

	tPtr->flags.secure = ((flag == 0) ? 0 : 1);

	if (tPtr->view->flags.realized) {
		paintTextField(tPtr);
	}
}

bool WMGetTextFieldEditable(WMTextField * tPtr)
{
	CHECK_CLASS(tPtr, WC_TextField);

	return tPtr->flags.enabled;
}

void WMSetTextFieldEditable(WMTextField * tPtr, bool flag)
{
	CHECK_CLASS(tPtr, WC_TextField);

	tPtr->flags.enabled = ((flag == 0) ? 0 : 1);

	if (tPtr->view->flags.realized) {
		paintTextField(tPtr);
	}
}

void WMSelectTextFieldRange(WMTextField * tPtr, WMRange range)
{
	CHECK_CLASS(tPtr, WC_TextField);

	if (tPtr->flags.enabled) {
		normalizeRange(tPtr, &range);

		tPtr->selection = range;

		tPtr->cursorPosition = range.position + range.count;

		if (tPtr->view->flags.realized) {
			paintTextField(tPtr);
		}
	}
}

void WMSetTextFieldCursorPosition(WMTextField * tPtr, unsigned int position)
{
	CHECK_CLASS(tPtr, WC_TextField);

	if (tPtr->flags.enabled) {
		if (position > tPtr->textLen)
			position = tPtr->textLen;

		tPtr->cursorPosition = position;
		if (tPtr->view->flags.realized) {
			paintTextField(tPtr);
		}
	}
}

unsigned WMGetTextFieldCursorPosition(WMTextField *tPtr)
{
	CHECK_CLASS(tPtr, WC_TextField);

	return tPtr->cursorPosition;
}

void WMSetTextFieldNextTextField(WMTextField * tPtr, WMTextField * next)
{
	CHECK_CLASS(tPtr, WC_TextField);
	if (next == NULL) {
		if (tPtr->view->nextFocusChain)
			tPtr->view->nextFocusChain->prevFocusChain = NULL;
		tPtr->view->nextFocusChain = NULL;
		return;
	}

	CHECK_CLASS(next, WC_TextField);

	if (tPtr->view->nextFocusChain)
		tPtr->view->nextFocusChain->prevFocusChain = NULL;
	if (next->view->prevFocusChain)
		next->view->prevFocusChain->nextFocusChain = NULL;

	tPtr->view->nextFocusChain = next->view;
	next->view->prevFocusChain = tPtr->view;
}

void WMSetTextFieldPrevTextField(WMTextField * tPtr, WMTextField * prev)
{
	CHECK_CLASS(tPtr, WC_TextField);
	if (prev == NULL) {
		if (tPtr->view->prevFocusChain)
			tPtr->view->prevFocusChain->nextFocusChain = NULL;
		tPtr->view->prevFocusChain = NULL;
		return;
	}

	CHECK_CLASS(prev, WC_TextField);

	if (tPtr->view->prevFocusChain)
		tPtr->view->prevFocusChain->nextFocusChain = NULL;
	if (prev->view->nextFocusChain)
		prev->view->nextFocusChain->prevFocusChain = NULL;

	tPtr->view->prevFocusChain = prev->view;
	prev->view->nextFocusChain = tPtr->view;
}

void WMSetTextFieldFont(WMTextField * tPtr, WMFont * font)
{
	WMFont *newFont;

	CHECK_CLASS(tPtr, WC_TextField);

	newFont = WMRetainFont(font);
	if (tPtr->font)
		WMReleaseFont(tPtr->font);
	tPtr->font = newFont;

	tPtr->offsetWidth = WMAX((tPtr->view->size.height - WMFontHeight(tPtr->font)) / 2, 1);

	if (tPtr->view->flags.realized) {
		paintTextField(tPtr);
	}
}

WMFont *WMGetTextFieldFont(WMTextField * tPtr)
{
	return tPtr->font;
}

static void didResizeTextField(W_ViewDelegate * self, WMView * view)
{
	WMTextField *tPtr = (WMTextField *) view->self;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;

	tPtr->offsetWidth = WMAX((tPtr->view->size.height - WMFontHeight(tPtr->font)) / 2, 1);

	tPtr->usableWidth = tPtr->view->size.width - 2 * tPtr->offsetWidth;
}

static char *makeHiddenString(int length)
{
	char *data = wmalloc(length + 1);

	memset(data, '*', length);
	data[length] = '\0';

	return data;
}

static void paintCursor(TextField * tPtr)
{
	int cx;
	WMScreen *screen = tPtr->view->screen;
	cairo_t *cr;
	int textWidth;
	char *text;

	if (tPtr->flags.secure)
		text = makeHiddenString(strlen(tPtr->text));
	else
		text = tPtr->text;

	cx = WMWidthOfString(tPtr->font, &(text[tPtr->viewPosition]), tPtr->cursorPosition - tPtr->viewPosition);

	switch (tPtr->flags.alignment) {
	case WARight:
		textWidth = WMWidthOfString(tPtr->font, text, tPtr->textLen);
		if (textWidth < tPtr->usableWidth)
			cx += tPtr->offsetWidth + tPtr->usableWidth - textWidth + 1;
		else
			cx += tPtr->offsetWidth + 1;
		break;
	case WALeft:
		cx += tPtr->offsetWidth + 1;
		break;
	case WAJustified:
		/* not supported */
	case WACenter:
		textWidth = WMWidthOfString(tPtr->font, text, tPtr->textLen);
		if (textWidth < tPtr->usableWidth)
			cx += tPtr->offsetWidth + (tPtr->usableWidth - textWidth) / 2;
		else
			cx += tPtr->offsetWidth;
		break;
	}
	/* the caret toggled through GXxor so a second draw restored the
	 * pixels underneath, DIFFERENCE with a white source is the same
	 * per-channel inversion and round-trips the same way */
	cr = WCairoBeginForDrawable(screen, tPtr->view->window);
	if (cr) {
		cairo_set_operator(cr, CAIRO_OPERATOR_DIFFERENCE);
		cairo_set_source_rgb(cr, 1, 1, 1);
		cairo_rectangle(cr, cx, tPtr->offsetWidth, 1,
				tPtr->view->size.height - 2 * tPtr->offsetWidth);
		cairo_fill(cr);
		WCairoEnd(cr);
	}

	W_SetPreeditPositon(tPtr->view, cx, 0);

	if (tPtr->flags.secure) {
		wfree(text);
	}
}

static void reliefLine(cairo_t *cr, WMColor *c, int x1, int y1, int x2, int y2)
{
	W_SetSourceColor(cr, c);
	cairo_move_to(cr, x1 + 0.5, y1 + 0.5);
	cairo_line_to(cr, x2 + 0.5, y2 + 0.5);
	cairo_stroke(cr);
}

static void drawRelief(cairo_t *cr, W_Screen *scr, int width, int height, bool beveled)
{
	if (!beveled) {
		W_SetSourceColor(cr, scr->darkGray);
		cairo_rectangle(cr, 0.5, 0.5, width - 1, height - 1);
		cairo_stroke(cr);
		return;
	}

	/* top left */
	reliefLine(cr, scr->darkGray, 0, 0, width - 1, 0);
	reliefLine(cr, scr->darkGray, 0, 1, width - 2, 1);
	reliefLine(cr, scr->darkGray, 0, 0, 0, height - 2);
	reliefLine(cr, scr->darkGray, 1, 0, 1, height - 3);

	/* bottom right */
	reliefLine(cr, scr->white, 0, height - 1, width - 1, height - 1);
	reliefLine(cr, scr->gray,  1, height - 2, width - 2, height - 2);
	reliefLine(cr, scr->white, width - 1, 0, width - 1, height - 1);
	reliefLine(cr, scr->gray,  width - 2, 1, width - 2, height - 3);
}

static void paintTextField(TextField * tPtr)
{
	W_Screen *screen = tPtr->view->screen;
	W_View *view = tPtr->view;
	cairo_t *cr;
	int tx, ty, tw;
	int rx;
	int bd;
	char *text;
	WMColor *color;

	if (!view->flags.realized || !view->flags.mapped)
		return;

	bd = tPtr->flags.bordered ? 2 : 0;

	if (tPtr->flags.secure) {
		text = makeHiddenString(strlen(tPtr->text));
	} else {
		text = tPtr->text;
	}

	cr = WCairoBeginBuffered(screen, view->window);
	if (!cr) {
		if (tPtr->flags.secure)
			wfree(text);
		return;
	}
	cairo_set_line_width(cr, 1.0);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);

	W_SetSourceColor(cr, screen->textFieldBack);
	cairo_paint(cr);

	if (tPtr->textLen > 0) {
		tw = WMWidthOfString(tPtr->font, &(text[tPtr->viewPosition]), tPtr->textLen - tPtr->viewPosition);

		ty = tPtr->offsetWidth;
		switch (tPtr->flags.alignment) {
		case WALeft:
			tx = tPtr->offsetWidth + 1;
			break;
		case WACenter:
			tx = tPtr->offsetWidth + (tPtr->usableWidth - tw) / 2;
			break;
		default:
		case WARight:
			tx = tPtr->offsetWidth + tPtr->usableWidth - tw - 1;
			break;
		}

		color = tPtr->flags.enabled ? screen->black : screen->darkGray;

		cairo_save(cr);
		cairo_rectangle(cr, bd, bd, view->size.width - 2 * bd,
				view->size.height - 2 * bd);
		cairo_clip(cr);
		W_DrawString(cr, color, tPtr->font, tx, ty,
				  &(text[tPtr->viewPosition]),
				  tPtr->textLen - tPtr->viewPosition);

		if (tPtr->selection.count) {
			int count, count2;

			count = tPtr->selection.count < 0
			    ? tPtr->selection.position + tPtr->selection.count : tPtr->selection.position;
			count2 = abs(tPtr->selection.count);
			if (count < tPtr->viewPosition) {
				count2 = abs(count2 - abs(tPtr->viewPosition - count));
				count = tPtr->viewPosition;
			}

			rx = tx + WMWidthOfString(tPtr->font, &(text[tPtr->viewPosition]),
			                          count - tPtr->viewPosition);

			W_SetSourceColor(cr, screen->gray);
			cairo_rectangle(cr, rx, ty,
					WMWidthOfString(tPtr->font, &(text[count]), count2),
					WMFontHeight(tPtr->font));
			cairo_fill(cr);
			W_DrawString(cr, color, tPtr->font, rx, ty,
					  &(text[count]), count2);
		}
		cairo_restore(cr);
	} else if (tPtr->placeholderText) {
		int px, pw;

		pw = WMWidthOfString(tPtr->font, tPtr->placeholderText,
				     strlen(tPtr->placeholderText));
		switch (tPtr->flags.alignment) {
		case WARight:
			px = pw < tPtr->usableWidth ?
			    tPtr->offsetWidth + tPtr->usableWidth - pw + 1 :
			    tPtr->offsetWidth + 1;
			break;
		case WACenter:
			px = pw < tPtr->usableWidth ?
			    tPtr->offsetWidth + (tPtr->usableWidth - pw) / 2 :
			    tPtr->offsetWidth;
			break;
		default:
			px = tPtr->offsetWidth + 1;
			break;
		}
		cairo_save(cr);
		cairo_rectangle(cr, bd, bd, view->size.width - 2 * bd,
				view->size.height - 2 * bd);
		cairo_clip(cr);
		W_DrawString(cr, screen->darkGray, tPtr->font,
				  px, tPtr->offsetWidth,
				  tPtr->placeholderText,
				  strlen(tPtr->placeholderText));
		cairo_restore(cr);
	}

	/* draw relief */
	if (tPtr->flags.bordered)
		drawRelief(cr, screen, view->size.width, view->size.height,
			   tPtr->flags.beveled);

	if (tPtr->flags.secure)
		wfree(text);

	WCairoEnd(cr);

	/* draw cursor */
	if (tPtr->flags.focused && tPtr->flags.enabled && tPtr->flags.cursorOn) {
		paintCursor(tPtr);
	}
}

static void blinkCursor(void *data)
{
	TextField *tPtr = (TextField *) data;

	if (tPtr->flags.cursorOn) {
		tPtr->timerID = WMAddTimerHandler(CURSOR_BLINK_OFF_DELAY, blinkCursor, data);
	} else {
		tPtr->timerID = WMAddTimerHandler(CURSOR_BLINK_ON_DELAY, blinkCursor, data);
	}
	paintCursor(tPtr);
	tPtr->flags.cursorOn = !tPtr->flags.cursorOn;
}

static void handleEvents(xcb_generic_event_t * event, void *data)
{
	TextField *tPtr = (TextField *) data;

	CHECK_CLASS(data, WC_TextField);

	switch ((event->response_type & 0x7f)) {
	case XCB_FOCUS_IN:
		W_FocusIC(tPtr->view);
		if (W_FocusedViewOfToplevel(W_TopLevelOfView(tPtr->view)) != tPtr->view)
			return;
		tPtr->flags.focused = 1;

		if (!tPtr->timerID) {
			tPtr->timerID = WMAddTimerHandler(CURSOR_BLINK_ON_DELAY, blinkCursor, tPtr);
		}

		paintTextField(tPtr);

		NOTIFY(tPtr, didBeginEditing, WMTextDidBeginEditingNotification, NULL);

		tPtr->flags.notIllegalMovement = 0;
		break;

	case XCB_FOCUS_OUT:
		W_UnFocusIC(tPtr->view);
		tPtr->flags.focused = 0;

		WMDeleteTimerHandlerPtr(&tPtr->timerID);

		paintTextField(tPtr);
		if (!tPtr->flags.notIllegalMovement) {
			NOTIFY(tPtr, didEndEditing, WMTextDidEndEditingNotification,
			       (void *)WMIllegalTextMovement);
		}
		break;

	case XCB_EXPOSE:
		if (((xcb_expose_event_t *)event)->count != 0)
			break;
		paintTextField(tPtr);
		break;

	case XCB_DESTROY_NOTIFY:
		destroyTextField(tPtr);
		break;
	}
}

/* paste chain: UTF8 first, CLIPBOARD before PRIMARY, cut buffer last.
 * The step index rides the callback cdata. */
static void pasteText(WMView *view, xcb_atom_t selection, xcb_atom_t target,
		      xcb_timestamp_t timestamp, void *cdata, WMData *data, bool refused);

static bool pasteTarget(TextField *tPtr, unsigned step, xcb_timestamp_t timestamp)
{
	bool clipboard = (step < 2);
	bool utf8 = (step & 1) == 0;
	xcb_atom_t selection = clipboard ? tPtr->view->screen->clipboardAtom
					 : XCB_ATOM_PRIMARY;
	xcb_atom_t target = utf8 ? tPtr->view->screen->utf8String
				 : XCB_ATOM_STRING;

	return WMRequestSelection(tPtr->view, selection, target, timestamp,
				  pasteText, (void *)(uintptr_t) step);
}

static void pasteSelectionTimeout(void *cdata)
{
	TextField *tPtr = (TextField *) cdata;

	tPtr->pasteTimer = NULL;
	/* a hung selection owner must not block the field's input, nor
	 * may a late notify insert after the timeout */
	tPtr->flags.waitingSelection = 0;
	WMCancelSelectionRequests(tPtr->view);
}

static void armPasteTimer(TextField *tPtr)
{
	WMDeleteTimerHandlerPtr(&tPtr->pasteTimer);
	tPtr->pasteTimer = WMAddTimerHandler(2000, pasteSelectionTimeout, tPtr);
}

static bool pasteCutBuffer(TextField *tPtr)
{
	char *text;
	int n;

	text = wXFetchBuffer(tPtr->view->screen->display, tPtr->view->screen->screen, &n, 0);
	if (text == NULL)
		return false;

	WMInsertTextFieldText(tPtr, text, tPtr->cursorPosition);
	wfree(text);
	/* the caller skips the selection recompute on a synchronous
	 * paste, so clear it here like the async path leaves it */
	tPtr->selection.count = 0;
	tPtr->selection.position = tPtr->cursorPosition;
	return true;
}

static void pasteText(WMView *view, xcb_atom_t selection, xcb_atom_t target,
		      xcb_timestamp_t timestamp, void *cdata, WMData *data, bool refused)
{
	TextField *tPtr = (TextField *) view->self;
	uintptr_t step = (uintptr_t) cdata;
	unsigned len, fmt;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) selection;
	(void) target;

	tPtr->flags.waitingSelection = 0;
	WMDeleteTimerHandlerPtr(&tPtr->pasteTimer);

	if (data != NULL) {
		fmt = WMGetDataFormat(data);
		len = WMGetDataLength(data);

		/* Only accept format-8 (string) data, and NUL-terminate
		 * the copy since WMInsertTextFieldText does strlen() and
		 * WMData payloads are not guaranteed NUL-terminated. */
		if (fmt == 8 && len > 0) {
			char *tmp = wmalloc(len + 1);

			memcpy(tmp, WMDataBytes(data), len);
			tmp[len] = '\0';
			WMInsertTextFieldText(tPtr, tmp, tPtr->cursorPosition);
			wfree(tmp);
			NOTIFY(tPtr, didChange, WMTextDidChangeNotification, (void *)WMInsertTextEvent);
		}
	} else if (refused && step < 3 && pasteTarget(tPtr, step + 1, timestamp)) {
		/* the owner refused this target, continue down the chain */
		tPtr->flags.waitingSelection = 1;
		armPasteTimer(tPtr);
	} else if (pasteCutBuffer(tPtr)) {
		NOTIFY(tPtr, didChange, WMTextDidChangeNotification, (void *)WMInsertTextEvent);
	}
}

/* Shared paste path for Shift+Insert, Ctrl+V, and middle-click.
 * tryClipboardFirst picks CLIPBOARD then PRIMARY, else PRIMARY only.
 * Returns True on synchronous cut-buffer fallback insert (caller posts the
 * change notification). Returns False when an async request is in flight
 * or nothing was pasted. deleteSelection deletes the current selection
 * first. */
static bool requestPaste(TextField *tPtr, xcb_timestamp_t timestamp, bool tryClipboardFirst, bool deleteSelection)
{
	bool deleted = false;
	unsigned step, last = 3;

	if (deleteSelection && tPtr->selection.count) {
		WMDeleteTextFieldRange(tPtr, tPtr->selection);
		deleted = true;
	}

	for (step = (tryClipboardFirst ? 0 : 2); step <= last; step++) {
		if (pasteTarget(tPtr, step, timestamp)) {
			tPtr->flags.waitingSelection = 1;
			armPasteTimer(tPtr);
			/* The selection was deleted but the paste is async:
			 * post the change notification now so delegates see
			 * the deletion even if the async paste later fails. */
			if (deleted)
				NOTIFY(tPtr, didChange, WMTextDidChangeNotification,
				       (void *)WMDeleteTextEvent);
			return false;
		}
	}

	if (pasteCutBuffer(tPtr))
		return true;

	return deleted;
}

static void handleTextFieldKeyPress(TextField * tPtr, xcb_generic_event_t * event)
{
	char buffer[64];
	xkb_keysym_t ksym;
	const char *textEvent = NULL;
	void *data = NULL;
	int count, refresh = 0;
	int control_pressed = 0;
	int cancelSelection = 1;
	bool shifted, controlled, modified;
	bool relay = true;

	/* xcb path: forward printable keys / dead keys to the protocol IM.
	 * Text committed by the IM arrives via commit_string -> WMInsertTextFieldText,
	 * navigation keysyms (arrows, BackSpace, Return, Escape, ...) bypass the IM. */
	if (W_IMForwardKey(tPtr->view, event))
		return;

	/*printf("(%d,%d) -> ", tPtr->selection.position, tPtr->selection.count); */
	if (((xcb_key_press_event_t *)event)->state & WM_EMACSKEYMASK)
		control_pressed = 1;

	shifted = (((xcb_key_press_event_t *)event)->state & XCB_MOD_MASK_SHIFT ? true : false);
	controlled = (((xcb_key_press_event_t *)event)->state & XCB_MOD_MASK_CONTROL ? true : false);
	modified = shifted || controlled;

	count = W_LookupString(tPtr->view, (const xcb_key_press_event_t *)event, buffer, 63, &ksym, NULL);
	if (count < 0)
		count = 0;
	else if (count > 63)
		count = 63;
	buffer[count] = '\0';

	switch (ksym) {
	case XKB_KEY_Tab:
		/* FALLTHRU */
	case XKB_KEY_ISO_Left_Tab:
		if (!controlled) {
			if (shifted) {
				if (tPtr->view->prevFocusChain) {
					W_SetFocusOfTopLevel(W_TopLevelOfView(tPtr->view),
							     tPtr->view->prevFocusChain);
					tPtr->flags.notIllegalMovement = 1;
				}
				data = (void *)WMBacktabTextMovement;
			} else {
				if (tPtr->view->nextFocusChain) {
					W_SetFocusOfTopLevel(W_TopLevelOfView(tPtr->view),
							     tPtr->view->nextFocusChain);
					tPtr->flags.notIllegalMovement = 1;
				}
				data = (void *)WMTabTextMovement;
			}
			textEvent = WMTextDidEndEditingNotification;

			cancelSelection = 0;

			relay = false;
		}
		break;

	case XKB_KEY_Escape:
		if (!modified) {
			data = (void *)WMEscapeTextMovement;
			textEvent = WMTextDidEndEditingNotification;

			relay = false;
		}
		break;

		/* FALLTHRU */
	case XKB_KEY_KP_Enter:
		/* FALLTHRU */
	case XKB_KEY_Return:
		if (!modified) {
			data = (void *)WMReturnTextMovement;
			textEvent = WMTextDidEndEditingNotification;

			relay = false;
		}
		break;

	case WM_EMACSKEY_LEFT:
		if (!control_pressed)
			goto normal_key;
		else
			controlled = false;

		/* FALLTHRU */
	case XKB_KEY_KP_Left:
		/* FALLTHRU */
	case XKB_KEY_Left:
		if (tPtr->cursorPosition > 0) {
			int i;

			i = tPtr->cursorPosition;
			i += oneUTF8CharBackward(&tPtr->text[i], i);
			if (controlled) {
				while (i > 0 && tPtr->text[i] != ' ')
					i--;
				while (i > 0 && tPtr->text[i] == ' ')
					i--;

				tPtr->cursorPosition = (i > 0) ? i + 1 : 0;
			} else
				tPtr->cursorPosition = i;

			if (tPtr->cursorPosition < tPtr->viewPosition) {
				tPtr->viewPosition = tPtr->cursorPosition;
			}
			refresh = 1;
		}
		if (shifted)
			cancelSelection = 0;

		relay = false;

		break;

	case WM_EMACSKEY_RIGHT:
		if (!control_pressed)
			goto normal_key;
		else
			controlled = false;

		/* FALLTHRU */
	case XKB_KEY_KP_Right:
		/* FALLTHRU */
	case XKB_KEY_Right:
		if (tPtr->cursorPosition < tPtr->textLen) {
			int i;

			i = tPtr->cursorPosition;
			if (controlled) {
				while (tPtr->text[i] && tPtr->text[i] != ' ')
					i++;
				while (tPtr->text[i] == ' ')
					i++;
			} else {
				i += oneUTF8CharForward(&tPtr->text[i], tPtr->textLen - i);
			}
			tPtr->cursorPosition = i;

			incrToFit2(tPtr);
			refresh = 1;
		}
		if (shifted)
			cancelSelection = 0;

		relay = false;

		break;

	case WM_EMACSKEY_HOME:
		if (!control_pressed)
			goto normal_key;
		else
			controlled = false;

		/* FALLTHRU */
	case XKB_KEY_KP_Home:
		/* FALLTHRU */
	case XKB_KEY_Home:
		if (!controlled) {
			if (tPtr->cursorPosition > 0) {
				tPtr->cursorPosition = 0;
				if (tPtr->viewPosition > 0) {
					tPtr->viewPosition = 0;
				}
				refresh = 1;
			}
			if (shifted)
				cancelSelection = 0;

			relay = false;
		}
		break;

	case WM_EMACSKEY_END:
		if (!control_pressed)
			goto normal_key;
		else
			controlled = false;

		/* FALLTHRU */
	case XKB_KEY_KP_End:
		/* FALLTHRU */
	case XKB_KEY_End:
		if (!controlled) {
			if (tPtr->cursorPosition < tPtr->textLen) {
				tPtr->cursorPosition = tPtr->textLen;
				tPtr->viewPosition = 0;

				incrToFit(tPtr);
				refresh = 1;
			}
			if (shifted)
				cancelSelection = 0;

			relay = false;
		}
		break;

	case WM_EMACSKEY_BS:
		if (!control_pressed)
			goto normal_key;
		else
			modified = false;

		/* FALLTHRU */
	case XKB_KEY_BackSpace:
		if (!modified) {
			if (tPtr->selection.count) {
				WMDeleteTextFieldRange(tPtr, tPtr->selection);
				data = (void *)WMDeleteTextEvent;
				textEvent = WMTextDidChangeNotification;
			} else if (tPtr->cursorPosition > 0) {
				int i = oneUTF8CharBackward(&tPtr->text[tPtr->cursorPosition],
							    tPtr->cursorPosition);
				WMRange range;
				range.position = tPtr->cursorPosition + i;
				range.count = -i;
				WMDeleteTextFieldRange(tPtr, range);
				data = (void *)WMDeleteTextEvent;
				textEvent = WMTextDidChangeNotification;
			}

			relay = false;
		}
		break;

	case WM_EMACSKEY_DEL:
		if (!control_pressed)
			goto normal_key;
		else
			modified = false;

		/* FALLTHRU */
	case XKB_KEY_KP_Delete:
		/* FALLTHRU */
	case XKB_KEY_Delete:
		if (!modified) {
			if (tPtr->selection.count) {
				WMDeleteTextFieldRange(tPtr, tPtr->selection);
				data = (void *)WMDeleteTextEvent;
				textEvent = WMTextDidChangeNotification;
			} else if (tPtr->cursorPosition < tPtr->textLen) {
				WMRange range;
				range.position = tPtr->cursorPosition;
				range.count = oneUTF8CharForward(&tPtr->text[tPtr->cursorPosition],
								 tPtr->textLen - tPtr->cursorPosition);
				WMDeleteTextFieldRange(tPtr, range);
				data = (void *)WMDeleteTextEvent;
				textEvent = WMTextDidChangeNotification;
			}

			relay = false;
		}
		break;

	case XKB_KEY_Insert:
		if (!shifted || controlled)
			goto normal_key;
		/* Shift+Insert: paste from clipboard, fall back to primary */
		if (tPtr->flags.enabled && !tPtr->flags.waitingSelection) {
			if (requestPaste(tPtr, ((xcb_key_press_event_t *)event)->time, true, true)) {
				data = (void *)WMInsertTextEvent;
				textEvent = WMTextDidChangeNotification;
			}
			cancelSelection = 0;
		}
		relay = false;
		break;

	case XKB_KEY_c:
	case XKB_KEY_C:
		if (!controlled)
			goto normal_key;
		/* Ctrl+C: copy selection to clipboard */
		if (!tPtr->flags.secure && tPtr->selection.count != 0) {
			int start = tPtr->selection.count < 0
			    ? tPtr->selection.position + tPtr->selection.count
			    : tPtr->selection.position;

			wXStoreBuffer(tPtr->view->screen->display, tPtr->view->screen->screen,
				      &tPtr->text[start],
				      abs(tPtr->selection.count), 0);
			snapshotClipboard(tPtr);
			if (!tPtr->flags.ownsClipboard)
				tPtr->flags.ownsClipboard =
				    WMCreateSelectionHandler(tPtr->view,
							     tPtr->view->screen->clipboardAtom,
							     ((xcb_key_press_event_t *)event)->time,
							     &selectionHandler, NULL);
		}
		cancelSelection = 0;
		relay = false;
		break;

	case XKB_KEY_v:
	case XKB_KEY_V:
		if (!controlled)
			goto normal_key;
		/* Ctrl+V: paste from clipboard, fall back to primary */
		if (tPtr->flags.enabled && !tPtr->flags.waitingSelection) {
			if (requestPaste(tPtr, ((xcb_key_press_event_t *)event)->time, true, true)) {
				data = (void *)WMInsertTextEvent;
				textEvent = WMTextDidChangeNotification;
			}
			cancelSelection = 0;
		}
		relay = false;
		break;

	case XKB_KEY_x:
	case XKB_KEY_X:
		if (!controlled)
			goto normal_key;
		/* Ctrl+X: cut — copy to clipboard then delete */
		if (!tPtr->flags.secure && tPtr->selection.count != 0) {
			int start = tPtr->selection.count < 0
			    ? tPtr->selection.position + tPtr->selection.count
			    : tPtr->selection.position;

			wXStoreBuffer(tPtr->view->screen->display, tPtr->view->screen->screen,
				      &tPtr->text[start],
				      abs(tPtr->selection.count), 0);
			snapshotClipboard(tPtr);
			if (!tPtr->flags.ownsClipboard)
				tPtr->flags.ownsClipboard =
				    WMCreateSelectionHandler(tPtr->view,
							     tPtr->view->screen->clipboardAtom,
							     ((xcb_key_press_event_t *)event)->time,
							     &selectionHandler, NULL);
			WMDeleteTextFieldRange(tPtr, tPtr->selection);
			tPtr->selection.count = 0;
			tPtr->selection.position = tPtr->cursorPosition;
			data = (void *)WMDeleteTextEvent;
			textEvent = WMTextDidChangeNotification;
		}
		cancelSelection = 0;
		relay = false;
		break;

 normal_key:
	default:
		if (!controlled) {
			if (count > 0 && !iscntrl(buffer[0])) {
				if (tPtr->selection.count)
					WMDeleteTextFieldRange(tPtr, tPtr->selection);
				WMInsertTextFieldText(tPtr, buffer, tPtr->cursorPosition);
				data = (void *)WMInsertTextEvent;
				textEvent = WMTextDidChangeNotification;

				relay = false;
			}
		}
		break;
	}

	if (relay) {
		WMRelayToNextResponder(W_VIEW(tPtr), event);
		return;
	}

	/* Do not allow text selection in secure text fields */
	if (cancelSelection || tPtr->flags.secure) {
		lostSelection(tPtr->view, XCB_ATOM_PRIMARY, NULL);

		if (tPtr->selection.count) {
			tPtr->selection.count = 0;
			refresh = 1;
		}
		tPtr->selection.position = tPtr->cursorPosition;
	} else {
		if (tPtr->selection.count != tPtr->cursorPosition - tPtr->selection.position) {

			tPtr->selection.count = tPtr->cursorPosition - tPtr->selection.position;

			refresh = 1;
		}
	}

	/*printf("(%d,%d)\n", tPtr->selection.position, tPtr->selection.count); */

	if (textEvent) {
		WMNotification *notif = WMCreateNotification(textEvent, tPtr, data);

		if (tPtr->delegate) {
			if (textEvent == WMTextDidBeginEditingNotification && tPtr->delegate->didBeginEditing)
				(*tPtr->delegate->didBeginEditing) (tPtr->delegate, notif);

			else if (textEvent == WMTextDidEndEditingNotification && tPtr->delegate->didEndEditing)
				(*tPtr->delegate->didEndEditing) (tPtr->delegate, notif);

			else if (textEvent == WMTextDidChangeNotification && tPtr->delegate->didChange)
				(*tPtr->delegate->didChange) (tPtr->delegate, notif);
		}

		WMPostNotification(notif);
		WMReleaseNotification(notif);
	}

	if (refresh)
		paintTextField(tPtr);

	/*printf("(%d,%d)\n", tPtr->selection.position, tPtr->selection.count); */
}

static int pointToCursorPosition(TextField * tPtr, int x)
{
	int a, b, pos, prev, tw;

	if (tPtr->flags.bordered)
		x -= 2;

	if (WMWidthOfString(tPtr->font, &(tPtr->text[tPtr->viewPosition]),
			    tPtr->textLen - tPtr->viewPosition) <= x)
		return tPtr->textLen;

	a = tPtr->viewPosition;
	b = tPtr->textLen;

	/* we halve the text until we get into a 10 byte vicinity of x */
	while (b - a > 10) {
		pos = (a + b) / 2;
		pos += seekUTF8CharStart(&tPtr->text[pos], pos - a);
		tw = WMWidthOfString(tPtr->font, &(tPtr->text[tPtr->viewPosition]), pos - tPtr->viewPosition);
		if (tw > x) {
			b = pos;
		} else if (tw < x) {
			a = pos;
		} else {
			return pos;
		}
	}

	/* at this point x can be positioned on any glyph between 'a' and 'b-1'
	 * inclusive, with the exception of the left border of the 'a' glyph and
	 * the right border or the 'b-1' glyph
	 *
	 * ( <--- range for x's position ---> )
	 * a a+1 .......................... b-1 b
	 */
	pos = prev = a;
	while (pos <= b) {
		tw = WMWidthOfString(tPtr->font, &(tPtr->text[tPtr->viewPosition]), pos - tPtr->viewPosition);
		if (tw > x) {
			return prev;
		} else if (pos == b) {
			break;
		}
		prev = pos;
		pos += oneUTF8CharForward(&tPtr->text[pos], b - pos);
	}

	return b;
}

static void handleTextFieldActionEvents(xcb_generic_event_t * event, void *data)
{
	TextField *tPtr = (TextField *) data;
	static xcb_timestamp_t lastButtonReleasedEvent = 0;
	static xcb_timestamp_t lastButtonReleasedEvent2 = 0;
	xcb_connection_t *dpy = W_XEvqConnection();

	CHECK_CLASS(data, WC_TextField);

	switch ((event->response_type & 0x7f)) {
	case XCB_KEY_PRESS:
		if (tPtr->flags.waitingSelection) {
			return;
		}
		if (tPtr->flags.enabled && tPtr->flags.focused) {
			handleTextFieldKeyPress(tPtr, event);
			wXDefineCursor(dpy, W_VIEW(tPtr)->window, W_VIEW(tPtr)->screen->invisibleCursor);
			tPtr->flags.pointerGrabbed = 1;
		}
		break;

	case XCB_MOTION_NOTIFY:

		if (tPtr->flags.pointerGrabbed) {
			tPtr->flags.pointerGrabbed = 0;
			wXDefineCursor(dpy, W_VIEW(tPtr)->window, W_VIEW(tPtr)->screen->textCursor);
		}
		if (tPtr->flags.waitingSelection) {
			return;
		}

		if (tPtr->flags.enabled && (((xcb_motion_notify_event_t *)event)->state & (XCB_BUTTON_INDEX_1 << 7))) {

			if (tPtr->viewPosition < tPtr->textLen && ((xcb_motion_notify_event_t *)event)->event_x > tPtr->usableWidth) {
				if (WMWidthOfString(tPtr->font,
						    &(tPtr->text[tPtr->viewPosition]),
						    tPtr->cursorPosition - tPtr->viewPosition)
				    > tPtr->usableWidth) {
					tPtr->viewPosition += oneUTF8CharForward(&tPtr->text[tPtr->viewPosition],
										 tPtr->textLen -
										 tPtr->viewPosition);
				}
			} else if (tPtr->viewPosition > 0 && ((xcb_motion_notify_event_t *)event)->event_x < 0) {
				paintCursor(tPtr);
				tPtr->viewPosition += oneUTF8CharBackward(&tPtr->text[tPtr->viewPosition],
									  tPtr->viewPosition);
			}

			if (tPtr->flags.alignment == WARight) {
				int textWidth = WMWidthOfString(tPtr->font, tPtr->text, tPtr->textLen);
				if (textWidth < tPtr->usableWidth) {
					tPtr->cursorPosition = pointToCursorPosition(tPtr,
											     ((xcb_motion_notify_event_t *)event)->event_x - tPtr->usableWidth + textWidth);
				} else {
					tPtr->cursorPosition = pointToCursorPosition(tPtr, ((xcb_motion_notify_event_t *)event)->event_x);
				}
			} else if (tPtr->flags.alignment == WACenter) {
				int textWidth = WMWidthOfString(tPtr->font, tPtr->text, tPtr->textLen);
				if (textWidth < tPtr->usableWidth) {
					tPtr->cursorPosition = pointToCursorPosition(tPtr,
											     ((xcb_motion_notify_event_t *)event)->event_x - (tPtr->usableWidth - textWidth) / 2);
				} else {
					tPtr->cursorPosition = pointToCursorPosition(tPtr, ((xcb_motion_notify_event_t *)event)->event_x);
				}
			} else {
				tPtr->cursorPosition = pointToCursorPosition(tPtr, ((xcb_motion_notify_event_t *)event)->event_x);
			}

			/* Do not allow text selection in secure textfields */
			if (tPtr->flags.secure) {
				tPtr->selection.position = tPtr->cursorPosition;
			}

			tPtr->selection.count = tPtr->cursorPosition - tPtr->selection.position;

			paintCursor(tPtr);
			paintTextField(tPtr);

		}
		break;

	case XCB_BUTTON_PRESS:
		if (tPtr->flags.pointerGrabbed) {
			tPtr->flags.pointerGrabbed = 0;
			wXDefineCursor(dpy, W_VIEW(tPtr)->window, W_VIEW(tPtr)->screen->textCursor);
			break;
		}

		if (tPtr->flags.waitingSelection) {
			break;
		}

		switch (tPtr->flags.alignment) {
			int textWidth;
		case WARight:
			textWidth = WMWidthOfString(tPtr->font, tPtr->text, tPtr->textLen);
			if (tPtr->flags.enabled && !tPtr->flags.focused) {
				WMSetFocusToWidget(tPtr);
			}
			if (textWidth < tPtr->usableWidth) {
				tPtr->cursorPosition = pointToCursorPosition(tPtr,
										     ((xcb_button_press_event_t *)event)->event_x - tPtr->usableWidth
										     + textWidth);
			} else
				tPtr->cursorPosition = pointToCursorPosition(tPtr, ((xcb_button_press_event_t *)event)->event_x);

			if (tPtr->flags.focused) {
				tPtr->selection.position = tPtr->cursorPosition;
				tPtr->selection.count = 0;
			}
			paintTextField(tPtr);
			break;

		case WACenter:
			textWidth = WMWidthOfString(tPtr->font, tPtr->text, tPtr->textLen);
			if (tPtr->flags.enabled && !tPtr->flags.focused) {
				WMSetFocusToWidget(tPtr);
			}
			if (textWidth < tPtr->usableWidth) {
				tPtr->cursorPosition = pointToCursorPosition(tPtr,
									     ((xcb_button_press_event_t *)event)->event_x - (tPtr->usableWidth - textWidth) / 2);
			} else {
				tPtr->cursorPosition = pointToCursorPosition(tPtr, ((xcb_button_press_event_t *)event)->event_x);
			}
			if (tPtr->flags.focused) {
				tPtr->selection.position = tPtr->cursorPosition;
				tPtr->selection.count = 0;
			}
			paintTextField(tPtr);
			break;

		case WALeft:
			if (tPtr->flags.enabled && !tPtr->flags.focused) {
				WMSetFocusToWidget(tPtr);
			}
			if (tPtr->flags.focused && ((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_1) {
				tPtr->cursorPosition = pointToCursorPosition(tPtr, ((xcb_button_press_event_t *)event)->event_x);
				tPtr->selection.position = tPtr->cursorPosition;
				tPtr->selection.count = 0;
				paintTextField(tPtr);
			}
			break;
		default:
			break;
		}

		if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_2 && tPtr->flags.enabled) {
			/* Middle-click: paste from primary selection */
			if (requestPaste(tPtr, ((xcb_button_press_event_t *)event)->time, false, false)) {
				NOTIFY(tPtr, didChange, WMTextDidChangeNotification,
					(void *)WMInsertTextEvent);
			}
		}

		break;

	case XCB_BUTTON_RELEASE:
		if (tPtr->flags.pointerGrabbed) {
			tPtr->flags.pointerGrabbed = 0;
			wXDefineCursor(dpy, W_VIEW(tPtr)->window, W_VIEW(tPtr)->screen->textCursor);
		}
		if (tPtr->flags.waitingSelection) {
			break;
		}

		if (!tPtr->flags.secure && tPtr->selection.count != 0) {
			int start, count;
			wXRotateBuffers(dpy, tPtr->view->screen->screen, 1);

			count = abs(tPtr->selection.count);
			if (tPtr->selection.count < 0)
				start = tPtr->selection.position - count;
			else
				start = tPtr->selection.position;

			wXStoreBuffer(dpy, tPtr->view->screen->screen, &tPtr->text[start], count, 0);
		}

		if (!tPtr->flags.secure &&
		    ((xcb_button_press_event_t *)event)->time - lastButtonReleasedEvent <= WINGsConfiguration.doubleClickDelay) {

			if (((xcb_button_press_event_t *)event)->time - lastButtonReleasedEvent2 <=
			    2 * WINGsConfiguration.doubleClickDelay) {
				tPtr->selection.position = 0;
				tPtr->selection.count = tPtr->textLen;
			} else {
				int pos, cnt;
				char *txt;
				pos = tPtr->selection.position;
				cnt = tPtr->selection.count;
				txt = tPtr->text;
				while (pos >= 0) {
					if (txt[pos] == ' ' || txt[pos] == '\t')
						break;
					pos--;
				}
				pos++;

				while (pos + cnt < tPtr->textLen) {
					if (txt[pos + cnt] == ' ' || txt[pos + cnt] == '\t')
						break;
					cnt++;
				}
				tPtr->selection.position = pos;
				tPtr->selection.count = cnt;
			}
			paintTextField(tPtr);

			if (!tPtr->flags.ownsSelection) {
				tPtr->flags.ownsSelection =
				    WMCreateSelectionHandler(tPtr->view,
							     XCB_ATOM_PRIMARY,
							     ((xcb_button_press_event_t *)event)->time, &selectionHandler, NULL);
			}
		} else if (!tPtr->flags.secure && tPtr->selection.count != 0 && !tPtr->flags.ownsSelection) {
			tPtr->flags.ownsSelection =
			    WMCreateSelectionHandler(tPtr->view,
						     XCB_ATOM_PRIMARY, ((xcb_button_press_event_t *)event)->time, &selectionHandler, NULL);
		}

		lastButtonReleasedEvent2 = lastButtonReleasedEvent;
		lastButtonReleasedEvent = ((xcb_button_press_event_t *)event)->time;

		break;
	}
}

static void destroyTextField(TextField * tPtr)
{
	if (tPtr->timerID)
		WMDeleteTimerHandler(tPtr->timerID);

	WMDeleteTimerHandlerPtr(&tPtr->pasteTimer);

	W_DestroyIC(tPtr->view);

	WMReleaseFont(tPtr->font);
	WMDeleteSelectionHandler(tPtr->view, XCB_ATOM_PRIMARY, XCB_CURRENT_TIME);
	WMDeleteSelectionHandler(tPtr->view, tPtr->view->screen->clipboardAtom, XCB_CURRENT_TIME);
	WMRemoveNotificationObserver(tPtr);

	if (tPtr->clipText)
		wfree(tPtr->clipText);

	if (tPtr->placeholderText)
		wfree(tPtr->placeholderText);

	if (tPtr->text)
		wfree(tPtr->text);

	wfree(tPtr);
}
