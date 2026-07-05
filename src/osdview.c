/* osdview.c - generic single-line on-screen-display popup
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 2000 Alfredo K. Kojima
 *  Copyright (c) 2026 Window Maker Team
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include "wconfig.h"

#include <cairo.h>
#include <pango/pangocairo.h>

#include <WINGs/WINGsP.h>

#include "WindowMaker.h"
#include "screen.h"
#include "xinerama.h"
#include "config-values.h"
#include "osdview.h"

#define OSDVIEW_PAD_W 8
#define OSDVIEW_PAD_H 6

struct W_OSDView {
	W_Class widgetClass;
	WMView *view;

	WMColor *color;
	WMColor *bgColor;

	WMFont *font;

	char *text;
	WMSize text_size;

	WCairoSurface *csurf;

	unsigned fixed_size:1;
};

static void handleEvents(XEvent *event, void *clientData);
static void paint(WOSDView *osd);

WOSDView *WCreateOSDView(WMScreen *scr)
{
	WOSDView *osd;
	static W_Class widgetClass = 0;

	if (!widgetClass)
		widgetClass = W_RegisterUserWidget();

	osd = wmalloc(sizeof(WOSDView));
	osd->widgetClass = widgetClass;

	osd->view = W_CreateUnmanagedTopView(scr);
	if (!osd->view) {
		wfree(osd);
		return NULL;
	}
	osd->view->self = osd;

	/* The Floating MoveDisplay/ResizeDisplay mode XMoveWindow()s this
	 * window on every motion step while the non-opaque move/resize loop
	 * holds XGrabServer(), so client repaints for the area it vacates are
	 * blocked and a blue trail accumulates over any window the pointer
	 * crosses. With save_under the X server restores the obscured pixels
	 * itself on each move so no client repaint is needed under the grab,
	 * the window is small and never resized so the saved pixmap cost is
	 * negligible, and under a compositor save_under is a no-op. */
	osd->view->attribFlags |= CWSaveUnder;
	osd->view->attribs.save_under = True;

	osd->font = WMSystemFontOfSize(scr, 12);
	if (!osd->font) {
		W_DestroyView(osd->view);
		wfree(osd);
		return NULL;
	}

	osd->bgColor = WMCreateRGBColor(scr, 0x3333, 0x6666, 0x9999, True);
	osd->color = WMWhiteColor(scr);

	WMCreateEventHandler(osd->view, ExposureMask, handleEvents, osd);

	WMSetWidgetBackgroundColor(osd, osd->bgColor);

	/* W_RealizeView() asserts on a zero-sized view, so give the widget a
	 * minimal nonzero size now in case the caller realizes it before the
	 * first WSetOSDViewText() / WSetOSDViewFixedTemplate() call. */
	W_ResizeView(osd->view, OSDVIEW_PAD_W, WMFontHeight(osd->font) + OSDVIEW_PAD_H);

	return osd;
}

void WSetOSDViewFont(WOSDView *osd, WMFont *font)
{
	if (font == NULL || font == osd->font)
		return;

	WMReleaseFont(osd->font);
	osd->font = WMRetainFont(font);

	if (osd->text) {
		osd->text_size.width = WMWidthOfString(osd->font, osd->text, strlen(osd->text));
		osd->text_size.height = WMFontHeight(osd->font);
	}
	if (!osd->fixed_size)
		W_ResizeView(osd->view, osd->text_size.width + OSDVIEW_PAD_W,
			     WMFontHeight(osd->font) + OSDVIEW_PAD_H);

	if (W_VIEW_MAPPED(osd->view))
		paint(osd);
}

void WSetOSDViewFixedTemplate(WOSDView *osd, const char *tmpl)
{
	int w, h;

	w = WMWidthOfString(osd->font, tmpl, strlen(tmpl));
	h = WMFontHeight(osd->font);

	osd->fixed_size = 1;
	W_ResizeView(osd->view, w + OSDVIEW_PAD_W, h + OSDVIEW_PAD_H);
}

void WSetOSDViewText(WOSDView *osd, const char *text)
{
	if (osd->text)
		wfree(osd->text);
	osd->text = wstrdup(text);

	osd->text_size.width = WMWidthOfString(osd->font, osd->text, strlen(osd->text));
	osd->text_size.height = WMFontHeight(osd->font);

	if (!osd->fixed_size)
		W_ResizeView(osd->view, osd->text_size.width + OSDVIEW_PAD_W,
			     osd->text_size.height + OSDVIEW_PAD_H);

	paint(osd);
}

static void paint(WOSDView *osd)
{
	cairo_t *cr;
	int w, h;

	if (!W_VIEW_REALIZED(osd->view))
		return;

	w = W_VIEW_WIDTH(osd->view);
	h = W_VIEW_HEIGHT(osd->view);

	/* the surface is bound to the view's window for its lifetime (a
	 * WOSDView is never destroyed), the resize below is a no-op when
	 * the view size has not changed */
	if (!osd->csurf)
		osd->csurf = WCairoSurfaceCreate(W_VIEW_SCREEN(osd->view),
						 W_VIEW_DRAWABLE(osd->view),
						 w, h, WCairoBuffered);
	else
		WCairoSurfaceResize(osd->csurf, w, h);

	cr = WCairoBegin(osd->csurf);

	WCairoSetSourceColor(cr, osd->bgColor);
	cairo_paint(cr);

	if (osd->text) {
		/* the layout was already measured with this text by
		 * WSetOSDViewText()/WSetOSDViewFont(), so this is a
		 * cheap same-string set */
		pango_layout_set_text(osd->font->layout, osd->text, -1);
		pango_cairo_update_layout(cr, osd->font->layout);
		WCairoSetSourceColor(cr, osd->color);
		cairo_move_to(cr,
			      (w - osd->text_size.width) / 2,
			      (h - osd->text_size.height) / 2);
		pango_cairo_show_layout(cr, osd->font->layout);
	}

	W_DrawReliefCairo(cr, W_VIEW_SCREEN(osd->view), 0, 0, w, h, WRSimple);

	WCairoEnd(cr);
}

static void handleEvents(XEvent *event, void *clientData)
{
	WOSDView *osd = (WOSDView *)clientData;

	switch (event->type) {
	case Expose:
		paint(osd);
		break;
	}
}

void wPlaceOSDOnHead(WScreen *scr, WOSDView *osd, int wd_pos, int pad)
{
	WMRect rect;
	int head, w, h, px, py;

	head = wGetHeadForPointerLocation(scr);
	rect = wGetRectForHead(scr, head);
	w = W_VIEW_WIDTH(osd->view);
	h = W_VIEW_HEIGHT(osd->view);

	switch (wd_pos) {
	case WD_TOP:
		px = rect.pos.x + (rect.size.width - w) / 2;
		py = rect.pos.y + pad;
		break;
	case WD_BOTTOM:
		px = rect.pos.x + (rect.size.width - w) / 2;
		py = rect.pos.y + rect.size.height - h - pad;
		break;
	case WD_TOPLEFT:
		px = rect.pos.x + pad;
		py = rect.pos.y + pad;
		break;
	case WD_TOPRIGHT:
		px = rect.pos.x + rect.size.width - w - pad;
		py = rect.pos.y + pad;
		break;
	case WD_BOTTOMLEFT:
		px = rect.pos.x + pad;
		py = rect.pos.y + rect.size.height - h - pad;
		break;
	case WD_BOTTOMRIGHT:
		px = rect.pos.x + rect.size.width - w - pad;
		py = rect.pos.y + rect.size.height - h - pad;
		break;
	case WD_CENTER:
	default:
		px = rect.pos.x + (rect.size.width - w) / 2;
		py = rect.pos.y + (rect.size.height - h) / 2;
		break;
	}

	W_MoveView(osd->view, px, py);
}
