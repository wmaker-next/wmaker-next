/*
 *  wlabel.c
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 1998 scottc
 *  Copyright (c) 1999-2004 Dan Pascu
 *  Copyright (c) 1999-2000 Alfredo K. Kojima
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

#include "wconfig.h"

#include "WINGsP.h"

typedef struct W_Label {
	W_Class widgetClass;
	W_View *view;

	char *caption;

	WMColor *textColor;
	WMFont *font;		/* if NULL, use default */

	W_Pixmap *image;

	PangoLayout *layout;	/* NULL in plain mode; label-owned in markup mode */

	struct {
		WMReliefType relief:3;
		WMImagePosition imagePosition:4;
		WMAlignment alignment:2;

		unsigned int noWrap:1;

		unsigned int redrawPending:1;
		unsigned int useMarkup:1;
	} flags;
} Label;

#define DEFAULT_WIDTH		60
#define DEFAULT_HEIGHT		14
#define DEFAULT_ALIGNMENT	WALeft
#define DEFAULT_RELIEF		WRFlat
#define DEFAULT_IMAGE_POSITION	WIPNoImage

static void destroyLabel(Label * lPtr);
static void paintLabel(Label * lPtr);
static PangoLayout *labelCreateLayout(Label *lPtr);
static void labelApplyText(Label *lPtr);

static void handleEvents(xcb_generic_event_t * event, void *data);

WMLabel *WMCreateLabel(WMWidget * parent)
{
	Label *lPtr;

	lPtr = wmalloc(sizeof(Label));

	lPtr->widgetClass = WC_Label;

	lPtr->view = W_CreateView(W_VIEW(parent));
	if (!lPtr->view) {
		wfree(lPtr);
		return NULL;
	}
	lPtr->view->self = lPtr;

	lPtr->textColor = WMRetainColor(lPtr->view->screen->black);

	WMCreateEventHandler(lPtr->view, XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY, handleEvents, lPtr);

	W_ResizeView(lPtr->view, DEFAULT_WIDTH, DEFAULT_HEIGHT);
	lPtr->flags.alignment = DEFAULT_ALIGNMENT;
	lPtr->flags.relief = DEFAULT_RELIEF;
	lPtr->flags.imagePosition = DEFAULT_IMAGE_POSITION;
	lPtr->flags.noWrap = 1;

	return lPtr;
}

void WMSetLabelImage(WMLabel * lPtr, WMPixmap * image)
{
	WMPixmap *old = lPtr->image;

	if (image)
		lPtr->image = WMRetainPixmap(image);
	else
		lPtr->image = NULL;

	if (old != NULL)
		WMReleasePixmap(old);

	if (lPtr->view->flags.realized) {
		paintLabel(lPtr);
	}
}

WMPixmap *WMGetLabelImage(WMLabel * lPtr)
{
	return lPtr->image;
}

char *WMGetLabelText(WMLabel * lPtr)
{
	return lPtr->caption;
}

void WMSetLabelImagePosition(WMLabel * lPtr, WMImagePosition position)
{
	lPtr->flags.imagePosition = position;
	if (lPtr->view->flags.realized) {
		paintLabel(lPtr);
	}
}

void WMSetLabelTextAlignment(WMLabel * lPtr, WMAlignment alignment)
{
	lPtr->flags.alignment = alignment;
	if (lPtr->view->flags.realized) {
		paintLabel(lPtr);
	}
}

void WMSetLabelRelief(WMLabel * lPtr, WMReliefType relief)
{
	lPtr->flags.relief = relief;
	if (lPtr->view->flags.realized) {
		paintLabel(lPtr);
	}
}

void WMSetLabelText(WMLabel * lPtr, const char *text)
{
	char *newCaption;

	if (text != NULL)
		newCaption = wstrdup(text);
	else
		newCaption = NULL;

	if (lPtr->caption)
		wfree(lPtr->caption);
	lPtr->caption = newCaption;
	if (lPtr->flags.useMarkup)
		labelApplyText(lPtr);
	if (lPtr->view->flags.realized) {
		paintLabel(lPtr);
	}
}

WMFont *WMGetLabelFont(WMLabel * lPtr)
{
	return lPtr->font;
}

void WMSetLabelFont(WMLabel * lPtr, WMFont * font)
{
	WMFont *old = lPtr->font;

	if (font)
		lPtr->font = WMRetainFont(font);
	else
		lPtr->font = NULL;
	if (old != NULL)
		WMReleaseFont(old);

	if (lPtr->layout) {
		WMFont *base = font ? font : lPtr->view->screen->normalFont;

		pango_layout_set_font_description(lPtr->layout,
			pango_layout_get_font_description(base->layout));
	}

	if (lPtr->view->flags.realized) {
		paintLabel(lPtr);
	}
}

void WMSetLabelTextColor(WMLabel * lPtr, WMColor * color)
{
	WMColor *old = lPtr->textColor;

	lPtr->textColor = WMRetainColor(color);
	if (old)
		WMReleaseColor(old);

	if (lPtr->view->flags.realized) {
		paintLabel(lPtr);
	}
}

void WMSetLabelWraps(WMLabel * lPtr, bool flag)
{
	flag = ((flag == 0) ? 0 : 1);
	if (lPtr->flags.noWrap != !flag) {
		lPtr->flags.noWrap = !flag;
		if (lPtr->view->flags.realized)
			paintLabel(lPtr);
	}
}

void WMSetLabelUseMarkup(WMLabel *lPtr, bool flag)
{
	flag = (flag != 0);
	if (lPtr->flags.useMarkup != flag) {
		lPtr->flags.useMarkup = flag;
		if (flag) {
			if (!lPtr->layout)
				lPtr->layout = labelCreateLayout(lPtr);
			labelApplyText(lPtr);
		}
		if (lPtr->view->flags.realized)
			paintLabel(lPtr);
	}
}

bool WMGetLabelUseMarkup(WMLabel *lPtr)
{
	return lPtr->flags.useMarkup;
}

void WMSetLabelMarkup(WMLabel *lPtr, const char *markup)
{
	if (!markup) {
		WMSetLabelText(lPtr, NULL);
		return;
	}
	if (!lPtr->flags.useMarkup)
		WMSetLabelUseMarkup(lPtr, true);
	WMSetLabelText(lPtr, markup);
}

static PangoLayout *labelCreateLayout(Label *lPtr)
{
	WMFont *font = lPtr->font ? lPtr->font : lPtr->view->screen->normalFont;
	PangoContext *context = pango_layout_get_context(font->layout);
	PangoLayout *layout = pango_layout_new(context);

	pango_layout_set_font_description(layout,
		pango_layout_get_font_description(font->layout));
	return layout;
}

static void labelApplyText(Label *lPtr)
{
	PangoAttrList *attrs = NULL;
	char *text = NULL;
	GError *err = NULL;

	if (!lPtr->layout)
		return;

	if (!lPtr->caption) {
		pango_layout_set_text(lPtr->layout, "", 0);
		pango_layout_set_attributes(lPtr->layout, NULL);
		return;
	}

	if (pango_parse_markup(lPtr->caption, -1, 0, &attrs, &text, NULL, &err)) {
		pango_layout_set_text(lPtr->layout, text, -1);
		pango_layout_set_attributes(lPtr->layout, attrs);
		pango_attr_list_unref(attrs);
		g_free(text);
	} else {
		wwarning(_("malformed label markup: %s"), err->message);
		g_error_free(err);
		pango_layout_set_text(lPtr->layout, lPtr->caption, -1);
		pango_layout_set_attributes(lPtr->layout, NULL);
	}
}

static void paintLabel(Label * lPtr)
{
	W_Screen *scrPtr = lPtr->view->screen;
	WMFont *font = (lPtr->font != NULL ? lPtr->font : scrPtr->normalFont);
	WMColor *color = lPtr->textColor ? lPtr->textColor : scrPtr->black;

	if (lPtr->flags.useMarkup && lPtr->layout)
		W_PaintTextAndImageMarkup(lPtr->view, !lPtr->flags.noWrap, color,
			font, lPtr->flags.relief,
			lPtr->flags.alignment, lPtr->image, lPtr->flags.imagePosition,
			NULL, 0, lPtr->layout);
	else
		W_PaintTextAndImage(lPtr->view, !lPtr->flags.noWrap, color,
			font, lPtr->flags.relief, lPtr->caption,
			lPtr->flags.alignment, lPtr->image, lPtr->flags.imagePosition,
			NULL, 0);
}

static void handleEvents(xcb_generic_event_t * event, void *data)
{
	Label *lPtr = (Label *) data;

	CHECK_CLASS(data, WC_Label);

	switch ((event->response_type & 0x7f)) {
	case XCB_EXPOSE:
		if (((xcb_expose_event_t *)event)->count != 0)
			break;
		paintLabel(lPtr);
		break;

	case XCB_DESTROY_NOTIFY:
		destroyLabel(lPtr);
		break;
	}
}

static void destroyLabel(Label * lPtr)
{
	if (lPtr->textColor)
		WMReleaseColor(lPtr->textColor);

	if (lPtr->caption)
		wfree(lPtr->caption);

	if (lPtr->font)
		WMReleaseFont(lPtr->font);

	if (lPtr->image)
		WMReleasePixmap(lPtr->image);

	if (lPtr->layout)
		g_object_unref(lPtr->layout);

	wfree(lPtr);
}
