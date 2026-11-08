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

#ifdef USE_XSHAPE
#include <xcb/shape.h>
#endif

#include <WINGs/WINGsP.h>

#include "WindowMaker.h"
#include "screen.h"
#include "xinerama.h"
#include "config_values.h"
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

	/* whole-frame opacity applied in paint(), 1.0 unless the caller
	 * is running a fade (only the workspace-name OSD does). Values
	 * below 1.0 are only visually meaningful when view->flags.argb
	 * held after realize, otherwise the RGB24 blit just darkens. */
	double alpha;

	unsigned transparent:1;
	unsigned fixed_size:1;
};

static void handleEvents(xcb_generic_event_t *event, void *clientData);
static void paint(WOSDView *osd);

#ifdef USE_XSHAPE
static void applyTextShape(WOSDView *osd, int width, int height)
{
	cairo_surface_t *surface;
	cairo_t *cr;
	xcb_rectangle_t *rectangles;
	int rectangle_count;
	int x, y;

	if (!osd->transparent || osd->view->flags.argb) {
		xcb_shape_mask(wdisp->conn, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING,
			       W_VIEW_DRAWABLE(osd->view), 0, 0, XCB_NONE);
		return;
	}

	surface = cairo_image_surface_create(CAIRO_FORMAT_A8, width, height);
	cr = cairo_create(surface);
	cairo_set_source_rgba(cr, 0, 0, 0, 0);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	if (osd->text) {
		pango_layout_set_text(osd->font->layout, osd->text, -1);
		pango_cairo_update_layout(cr, osd->font->layout);
		cairo_set_source_rgb(cr, 1, 1, 1);
		cairo_move_to(cr,
			      (width - osd->text_size.width) / 2,
			      (height - osd->text_size.height) / 2);
		pango_cairo_show_layout(cr, osd->font->layout);
	}
	cairo_destroy(cr);
	cairo_surface_flush(surface);

	rectangles = wmalloc(sizeof(*rectangles) * (size_t)width * height);
	rectangle_count = 0;
	for (y = 0; y < height; y++) {
		const unsigned char *row = cairo_image_surface_get_data(surface)
			+ y * cairo_image_surface_get_stride(surface);
		for (x = 0; x < width;) {
			int start;

			while (x < width && row[x] == 0)
				x++;
			start = x;
			while (x < width && row[x] > 0)
				x++;
			if (x > start) {
				rectangles[rectangle_count].x = start;
				rectangles[rectangle_count].y = y;
				rectangles[rectangle_count].width = x - start;
				rectangles[rectangle_count].height = 1;
				rectangle_count++;
			}
		}
	}

	xcb_shape_rectangles(wdisp->conn, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING,
			     3, W_VIEW_DRAWABLE(osd->view),
			     0, 0, rectangle_count, rectangles);
	xcb_flush(wdisp->conn);
	wfree(rectangles);
	cairo_surface_destroy(surface);
}
#endif

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
	osd->alpha = 1.0;

	/* opt into the depth-32 visual so WSetOSDViewAlpha() can ramp a
	 * real alpha channel. Only when a compositor owns _NET_WM_CM_Sn,
	 * else alpha=0 pixels would render as opaque black. W_RealizeView()
	 * clears the flag again if the server has no depth-32 visual, and
	 * paint() reads it back to pick the surface format. */
	if (scr->rcontext->xvisual_argb && scr->compositor_running)
		osd->view->flags.argb = 1;

	/* The Floating MoveDisplay/ResizeDisplay mode moves this window
	 * (wMouseMoveWindow) on every motion step while the non-opaque
	 * move/resize loop
	 * holds wXGrabServer(), so client repaints for the area it vacates are
	 * blocked and a blue trail accumulates over any window the pointer
	 * crosses. With save_under the X server restores the obscured pixels
	 * itself on each move so no client repaint is needed under the grab,
	 * the window is small and never resized so the saved pixmap cost is
	 * negligible, and under a compositor save_under is a no-op. */
	osd->view->attribFlags |= XCB_CW_SAVE_UNDER;
	osd->view->attribs.save_under = true;

	osd->font = WMSystemFontOfSize(scr, 12);
	if (!osd->font) {
		W_DestroyView(osd->view);
		wfree(osd);
		return NULL;
	}

	osd->bgColor = WMCreateRGBColor(scr, 0x3333, 0x6666, 0x9999, true);
	osd->color = WMWhiteColor(scr);

	WMCreateEventHandler(osd->view, XCB_EVENT_MASK_EXPOSURE, handleEvents, osd);

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

void WSetOSDViewTransparent(WOSDView *osd, bool transparent)
{
	osd->transparent = transparent != false;
	if (W_VIEW_MAPPED(osd->view))
		paint(osd);
}

void WSetOSDViewAlpha(WOSDView *osd, double alpha)
{
	if (alpha < 0.0)
		alpha = 0.0;
	else if (alpha > 1.0)
		alpha = 1.0;
	osd->alpha = alpha;
	if (W_VIEW_MAPPED(osd->view))
		paint(osd);
}

static void paint(WOSDView *osd)
{
	cairo_t *cr;
	int w, h, flags;

	if (!W_VIEW_REALIZED(osd->view))
		return;

	w = W_VIEW_WIDTH(osd->view);
	h = W_VIEW_HEIGHT(osd->view);

	/* the surface is bound to the view's window for its lifetime (a
	 * WOSDView is never destroyed), the resize below is a no-op when
	 * the view size has not changed */
	if (!osd->csurf) {
		flags = WCairoBuffered;
		if (osd->view->flags.argb)
			flags |= WCairoARGB;
		osd->csurf = WCairoSurfaceCreate(W_VIEW_SCREEN(osd->view),
						 W_VIEW_DRAWABLE(osd->view),
						 w, h, flags);
	} else {
		WCairoSurfaceResize(osd->csurf, w, h);
	}

	cr = WCairoBegin(osd->csurf);

	/* SOURCE so the fill replaces the previous frame instead of
	 * blending over its alpha, then back to OVER for text/relief. */
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	if (osd->transparent)
		cairo_set_source_rgba(cr, 0, 0, 0, 0);
	else
		W_SetSourceColor(cr, osd->bgColor);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	if (osd->text) {
		/* the layout was already measured with this text by
		 * WSetOSDViewText()/WSetOSDViewFont(), so this is a
		 * cheap same-string set */
		pango_layout_set_text(osd->font->layout, osd->text, -1);
		pango_cairo_update_layout(cr, osd->font->layout);
		W_SetSourceColor(cr, osd->color);
		cairo_move_to(cr,
			      (w - osd->text_size.width) / 2,
			      (h - osd->text_size.height) / 2);
		pango_cairo_show_layout(cr, osd->font->layout);
	}

	if (!osd->transparent)
		W_DrawRelief(cr, W_VIEW_SCREEN(osd->view), 0, 0, w, h, WRSimple);

	if (osd->alpha < 1.0) {
		/* scale the whole opaque frame by alpha in place: DEST_IN
		 * multiplies every channel of the destination by the source
		 * alpha. On the ARGB32 back buffer that yields premultiplied
		 * (r*a, g*a, b*a, a) and on the RGB24 fallback it darkens
		 * toward black, which is the intended non-composited look. */
		cairo_set_operator(cr, CAIRO_OPERATOR_DEST_IN);
		cairo_set_source_rgba(cr, 0, 0, 0, osd->alpha);
		cairo_paint(cr);
	}

	WCairoEnd(cr);

#ifdef USE_XSHAPE
	applyTextShape(osd, w, h);
#endif
}

static void handleEvents(xcb_generic_event_t *event, void *clientData)
{
	WOSDView *osd = (WOSDView *)clientData;

	switch ((event->response_type & 0x7f)) {
	case XCB_EXPOSE:
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
	case WD_LEFT:
		px = rect.pos.x + pad;
		py = rect.pos.y + (rect.size.height - h) / 2;
		break;
	case WD_RIGHT:
		px = rect.pos.x + rect.size.width - w - pad;
		py = rect.pos.y + (rect.size.height - h) / 2;
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
