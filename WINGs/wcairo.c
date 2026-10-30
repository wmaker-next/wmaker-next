/* wcairo.c - cairo-xcb surface acquisition for WINGs
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 2026 Window Maker Team
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
 */

#include "wconfig.h"

#include <math.h>

#include <cairo.h>
#include <cairo-xcb.h>

#include "WINGsP.h"

#include <wraster.h>

/*
 * One place that touches cairo_xcb_surface_* so callers deal only in
 * cairo_t *. The RContext already carries the shared xcb connection
 * and both the default and depth-32 visualtype pointers, so nothing
 * here holds its own copy of those.
 */
struct WCairoSurface {
	cairo_surface_t *xcb;   /* on-server surface bound to the drawable */
	cairo_surface_t *back;  /* NULL for direct, else client image buffer */
	int width;              /* device pixels */
	int height;
	int flags;
	double scale;           /* uiScale applied in WCairoBegin(), 1.0 if unscaled */
};

/* attached to the cairo_t returned by WCairoBegin() so WCairoEnd() can
 * find the on-server surface to blit onto without a signature change */
static const cairo_user_data_key_t wcairo_cs_key;

/* ---- drawable size cache ---------------------------------------------
 *
 * cairo_xcb_surface_create() wants the drawable's width and height for
 * its clip extents. Every WINGs and wmaker window is created and
 * resized through W_RealizeView/W_ResizeView or the wCoreCreate and
 * wCoreConfigure paths, all of which know the size at the point of the X
 * request, so they feed this cache and the paint path never issues an
 * xcb_get_geometry round trip. Pixmaps created ad hoc fall through to
 * one synchronous read that is then cached until the drawable is
 * abandoned. Both dimensions fit in 16 bits per the core protocol so
 * they pack into the hash value.
 */

/* bit 31 marks a drawable created at logical*uiScale device pixels so
 * WCairoBegin* apply cairo_scale() and paint code stays in logical
 * units. Width is capped to 15 bits (32767, above any real widget) so
 * the flag fits a 32-bit uintptr. */
#define DRAWABLE_SIZE_SCALED_BIT ((uintptr_t)1u << 31)

static void noteDrawableSize(WMScreen *scr, xcb_drawable_t d, unsigned int width,
			     unsigned int height, int scaled)
{
	uintptr_t packed;

	if (!scr || !scr->drawableSize || d == XCB_NONE)
		return;

	packed = ((uintptr_t)(width & 0x7fff) << 16) | (height & 0xffff);
	if (scaled)
		packed |= DRAWABLE_SIZE_SCALED_BIT;
	WMHashInsert(scr->drawableSize, (const void *)(uintptr_t)d, (void *)packed);
}

void WMScreenNoteDrawableSize(WMScreen *scr, xcb_drawable_t d, unsigned int width, unsigned int height)
{
	noteDrawableSize(scr, d, width, height, 0);
}

void WMScreenNoteDrawableSizeScaled(WMScreen *scr, xcb_drawable_t d, unsigned int width, unsigned int height)
{
	int dw, dh;

	dw = (int)(width * scr->uiScale + 0.5);
	dh = (int)(height * scr->uiScale + 0.5);
	noteDrawableSize(scr, d, dw, dh, scr->uiScale > 1.0001);
}

xcb_pixmap_t W_CreateScaledPixmap(WMScreen *scr, unsigned int width, unsigned int height)
{
	xcb_pixmap_t p;
	int dw, dh;

	dw = (int)(width * scr->uiScale + 0.5);
	dh = (int)(height * scr->uiScale + 0.5);
	p = xcb_generate_id(scr->display);
	xcb_create_pixmap(scr->display, scr->depth, p, scr->rootWin,
			  dw > 0 ? dw : 1, dh > 0 ? dh : 1);
	noteDrawableSize(scr, p, dw, dh, scr->uiScale > 1.0001);
	return p;
}

void WMScreenAbandonDrawable(WMScreen *scr, xcb_drawable_t d)
{
	if (!scr || d == XCB_NONE)
		return;

	if (scr->drawableSize)
		WMHashRemove(scr->drawableSize, (const void *)(uintptr_t)d);
}

static int lookupDrawableSize(W_Screen *scr, xcb_drawable_t d, int *w, int *h, int *scaled)
{
	xcb_connection_t *xc;
	xcb_get_geometry_reply_t *geom;
	void *val;

	val = WMHashGet(scr->drawableSize, (const void *)(uintptr_t)d);
	if (val) {
		uintptr_t packed = (uintptr_t)val;

		*w = (int)((packed >> 16) & 0x7fff);
		*h = (int)(packed & 0xffff);
		if (scaled)
			*scaled = (packed & DRAWABLE_SIZE_SCALED_BIT) != 0;
		return 1;
	}

	/* not fed by any create/resize chokepoint, ask the server once */
	xc = scr->rcontext->xc;
	geom = xcb_get_geometry_reply(xc, xcb_get_geometry(xc, (xcb_drawable_t)d), NULL);
	if (!geom)
		return 0;
	*w = geom->width;
	*h = geom->height;
	if (scaled)
		*scaled = 0;
	free(geom);

	noteDrawableSize(scr, d, (unsigned)*w, (unsigned)*h, 0);
	return 1;
}

/* ---- surface lifecycle ----------------------------------------------- */

WCairoSurface *WCairoSurfaceCreate(W_Screen *scr, xcb_drawable_t d, int width, int height, int flags)
{
	WCairoSurface *cs;
	RContext *rc;
	xcb_visualtype_t *visual;

	rc = scr->rcontext;
	visual = ((flags & WCairoARGB) && rc->xvisual_argb) ? rc->xvisual_argb : rc->xvisual;

	cs = wmalloc(sizeof(*cs));
	cs->width = width;
	cs->height = height;
	cs->flags = flags;
	cs->scale = (flags & WCairoScaled) ? scr->uiScale : 1.0;
	cs->xcb = cairo_xcb_surface_create(rc->xc, (xcb_drawable_t)d,
					   visual, width, height);
	if (cairo_surface_status(cs->xcb) != CAIRO_STATUS_SUCCESS) {
		wwarning("cairo_xcb_surface_create: %s",
			 cairo_status_to_string(cairo_surface_status(cs->xcb)));
		cairo_surface_destroy(cs->xcb);
		wfree(cs);
		return NULL;
	}
	if (flags & WCairoBuffered)
		cs->back = cairo_image_surface_create(
				(flags & WCairoARGB) ? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24,
				width, height);
	return cs;
}

void WCairoSurfaceResize(WCairoSurface *cs, int width, int height)
{
	if (!cs || (cs->width == width && cs->height == height))
		return;
	cairo_xcb_surface_set_size(cs->xcb, width, height);
	if (cs->back) {
		/* image surfaces cannot be resized, so replace it */
		cairo_surface_destroy(cs->back);
		cs->back = cairo_image_surface_create(
				(cs->flags & WCairoARGB) ? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24,
				width, height);
	}
	cs->width = width;
	cs->height = height;
}

void WCairoSurfaceDestroy(WCairoSurface *cs)
{
	if (!cs)
		return;
	if (cs->back)
		cairo_surface_destroy(cs->back);
	cairo_surface_destroy(cs->xcb);
	wfree(cs);
}

cairo_t *WCairoBegin(WCairoSurface *cs)
{
	cairo_t *cr;

	cr = cairo_create(cs->back ? cs->back : cs->xcb);
	cairo_set_user_data(cr, &wcairo_cs_key, cs, NULL);
	if (cs->scale > 1.0001)
		cairo_scale(cr, cs->scale, cs->scale);
	return cr;
}

/* Transient path for callers that only have a bare Drawable (chiefly
 * WMDrawString). Creates a surface for the duration of one paint, size
 * pulled from the cache above so there is no round trip on the window
 * path. WCairoEnd() destroys both the cairo_t and its surface. */
cairo_t *WCairoBeginForDrawable(W_Screen *scr, xcb_drawable_t d)
{
	cairo_surface_t *surface;
	cairo_t *cr;
	int w, h, scaled;

	if (!lookupDrawableSize(scr, d, &w, &h, &scaled))
		return NULL;

	surface = cairo_xcb_surface_create(scr->rcontext->xc, (xcb_drawable_t)d,
					   scr->rcontext->xvisual, w, h);
	cr = cairo_create(surface);
	/* cairo_t holds its own reference, drop ours so WCairoEnd()'s
	 * cairo_destroy() releases the last one */
	cairo_surface_destroy(surface);
	if (scaled)
		cairo_scale(cr, scr->uiScale, scr->uiScale);
	return cr;
}

/* Like WCairoBeginForDrawable() but the cairo_t targets a client-side
 * image surface and WCairoEnd() blits the finished frame onto the
 * drawable in one SOURCE-operator paint. Drop-in replacement for the
 * XCreatePixmap + draw + XCopyArea + XFreePixmap pattern the widget
 * paint paths used to avoid flicker on a full repaint. */
cairo_t *WCairoBeginBuffered(W_Screen *scr, xcb_drawable_t d)
{
	WCairoSurface *cs;
	int w, h, scaled, flags;

	if (!lookupDrawableSize(scr, d, &w, &h, &scaled))
		return NULL;
	flags = WCairoBuffered | WCairoTransient | (scaled ? WCairoScaled : 0);
	cs = WCairoSurfaceCreate(scr, d, w, h, flags);
	if (!cs)
		return NULL;
	return WCairoBegin(cs);
}

void WCairoEnd(cairo_t *cr)
{
	WCairoSurface *cs;
	cairo_surface_t *surface;
	cairo_status_t st;
	cairo_t *front;

	if (!cr)
		return;

	st = cairo_status(cr);
	if (st != CAIRO_STATUS_SUCCESS)
		wwarning("cairo: %s", cairo_status_to_string(st));

	cs = cairo_get_user_data(cr, &wcairo_cs_key);
	if (cs && cs->back) {
		/* buffered: the caller drew into cs->back, blit the whole
		 * frame onto the on-server surface in one operation so no
		 * intermediate paint stage is ever visible */
		cairo_surface_flush(cs->back);
		front = cairo_create(cs->xcb);
		cairo_set_operator(front, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_surface(front, cs->back, 0, 0);
		cairo_paint(front);
		cairo_destroy(front);
		cairo_surface_flush(cs->xcb);
	} else {
		surface = cairo_get_target(cr);
		cairo_surface_flush(surface);
	}
	cairo_destroy(cr);
	if (cs && (cs->flags & WCairoTransient))
		WCairoSurfaceDestroy(cs);
}

/* ---- source colour helpers ------------------------------------------- */

void W_SetSourceColor(cairo_t *cr, WMColor *color)
{
	cairo_set_source_rgba(cr,
			      color->color.red   / 65535.0,
			      color->color.green / 65535.0,
			      color->color.blue  / 65535.0,
			      color->alpha       / 65535.0);
}

void W_SetSourceRColor(cairo_t *cr, const RColor *color)
{
	cairo_set_source_rgba(cr,
			      color->red   / 255.0,
			      color->green / 255.0,
			      color->blue  / 255.0,
			      color->alpha / 255.0);
}

void W_SetSourceXColor(cairo_t *cr, const WXColor *color)
{
	cairo_set_source_rgb(cr,
			     color->red   / 65535.0,
			     color->green / 65535.0,
			     color->blue  / 65535.0);
}

/* the FillStippled darkGray-over-gray checkerboard the widget-slot
 * and progress-bar interiors use: mask darkGray through a 2x2 A8
 * checkerboard tiled with EXTEND_REPEAT so bits=1 land darkGray and
 * bits=0 leave whatever gray fill sits below untouched. Built as a
 * client-side image surface (not the server-side scr->stipple bitmap)
 * so a WCairoBuffered target masks with pixman rather than reading
 * back from the X server. */
void W_StippleFill(cairo_t *cr, W_Screen *scr, int x, int y, int w, int h)
{
	static cairo_pattern_t *pat;
	cairo_surface_t *stip;
	unsigned char *data;
	int stride;

	if (!pat) {
		stip = cairo_image_surface_create(CAIRO_FORMAT_A8, 2, 2);
		data = cairo_image_surface_get_data(stip);
		stride = cairo_image_surface_get_stride(stip);
		data[0] = 0xff; data[1] = 0x00;
		data[stride] = 0x00; data[stride + 1] = 0xff;
		cairo_surface_mark_dirty(stip);
		pat = cairo_pattern_create_for_surface(stip);
		cairo_surface_destroy(stip);
		cairo_pattern_set_extend(pat, CAIRO_EXTEND_REPEAT);
		cairo_pattern_set_filter(pat, CAIRO_FILTER_NEAREST);
	}

	cairo_save(cr);
	cairo_rectangle(cr, x, y, w, h);
	cairo_clip(cr);
	W_SetSourceColor(cr, scr->darkGray);
	cairo_mask(cr, pat);
	cairo_restore(cr);
}

/* copy a w x h rectangle from (sx, sy) on a screen-depth server
 * drawable src (whose full extents are srcW x srcH) onto the caller's
 * cairo target at (dx, dy). srcW/srcH are passed explicitly rather than
 * looked up in drawableSize because most callers hold a short-lived
 * pixmap from RConvertImage or XCreatePixmap and already know the
 * answer, so a cache miss and the associated round trip is avoided.
 *
 * When src was created via W_CreateScaledPixmap() (or is a scaled
 * W_View window) and cr's CTM is the same uiScale, both endpoints hold
 * device pixels at the same density, so blit under identity CTM to
 * avoid cairo up-scaling the already-scaled source a second time.
 * When only cr is scaled and src is a raw device-px pixmap the CTM
 * up-scale is what places a 1x thumbnail correctly on a 2x canvas, so
 * that case is left alone. */
void W_PaintArea(cairo_t *cr, W_Screen *scr, xcb_drawable_t src, int srcW, int srcH,
		     int sx, int sy, int w, int h, int dx, int dy)
{
	RContext *rc = scr->rcontext;
	cairo_surface_t *ss;
	uintptr_t v;
	double s;

	v = (uintptr_t)WMHashGet(scr->drawableSize, (const void *)(uintptr_t)src);
	if ((v & DRAWABLE_SIZE_SCALED_BIT) && scr->uiScale > 1.0001) {
		s = scr->uiScale;
		ss = cairo_xcb_surface_create(rc->xc, (xcb_drawable_t)src,
					      rc->xvisual,
					      (int)((v >> 16) & 0x7fff),
					      (int)(v & 0xffff));
		cairo_save(cr);
		cairo_identity_matrix(cr);
		cairo_set_source_surface(cr, ss, (dx - sx) * s, (dy - sy) * s);
		cairo_rectangle(cr, dx * s, dy * s, w * s, h * s);
		cairo_fill(cr);
		cairo_restore(cr);
		cairo_surface_destroy(ss);
		return;
	}

	ss = cairo_xcb_surface_create(rc->xc, (xcb_drawable_t)src,
				      rc->xvisual, srcW, srcH);
	cairo_set_source_surface(cr, ss, dx - sx, dy - sy);
	cairo_rectangle(cr, dx, dy, w, h);
	cairo_fill(cr);
	cairo_surface_destroy(ss);
}

/* one-shot server-side copy: open a transient cairo_t on dst, blit the
 * rectangle, close. Drop-in for the XCreateGC + XCopyArea + XFreeGC
 * pattern used by preview compositors that shuffle rendered pixmaps
 * between scratch surfaces. */
void W_CopyArea(W_Screen *scr, xcb_drawable_t src, int srcW, int srcH,
		    int sx, int sy, int w, int h, xcb_drawable_t dst, int dx, int dy)
{
	cairo_t *cr;

	cr = WCairoBeginForDrawable(scr, dst);
	if (!cr)
		return;
	W_PaintArea(cr, scr, src, srcW, srcH, sx, sy, w, h, dx, dy);
	WCairoEnd(cr);
}

/* paint a WMPixmap at (x, y). A screen-depth image half is wrapped as
 * a source surface at the screen visual, a depth-1 image half is
 * wrapped for_bitmap and painted as a black shape (bit=1 lands, bit=0
 * leaves the destination) which is what the button and label glyph
 * callers want. When the pixmap has a depth-1 mask it is wrapped
 * for_bitmap and used as the cairo mask so only the shape lands,
 * otherwise a rectangle at the pixmap extents bounds the fill. */
void W_PaintPixmap(cairo_t *cr, WMPixmap *pix, int x, int y)
{
	RContext *rc = pix->screen->rcontext;
	cairo_surface_t *ps, *ms;

	if (pix->depth == 1) {
		ps = cairo_xcb_surface_create_for_bitmap(rc->xc, rc->xscreen,
							 pix->pixmap,
							 pix->width, pix->height);
		cairo_set_source_rgb(cr, 0, 0, 0);
	} else {
		ps = cairo_xcb_surface_create(rc->xc, (xcb_drawable_t)pix->pixmap,
					      rc->xvisual, pix->width, pix->height);
		cairo_set_source_surface(cr, ps, x, y);
	}

	if (pix->mask != XCB_NONE) {
		ms = cairo_xcb_surface_create_for_bitmap(rc->xc, rc->xscreen,
							 pix->mask,
							 pix->width, pix->height);
		cairo_mask_surface(cr, ms, x, y);
		cairo_surface_destroy(ms);
	} else if (pix->depth == 1) {
		cairo_mask_surface(cr, ps, x, y);
	} else {
		cairo_rectangle(cr, x, y, pix->width, pix->height);
		cairo_fill(cr);
	}
	cairo_surface_destroy(ps);
}

/* fill the cairo target with the view's background: an explicit
 * WMColor if the caller passes one, else the view's background pixmap
 * tiled with EXTEND_REPEAT so it matches what the server would show,
 * else the view's background WMColor. Shared by W_PaintTextAndImage
 * and any per-widget buffered painter that starts from a bare row. */
void W_PaintViewBackground(cairo_t *cr, W_View *view, WMColor *backColor)
{
	RContext *rc = view->screen->rcontext;
	cairo_surface_t *bg;
	cairo_pattern_t *pat;

	if (backColor) {
		W_SetSourceColor(cr, backColor);
		cairo_paint(cr);
	} else if (view->backImage) {
		bg = cairo_xcb_surface_create(rc->xc,
					      (xcb_drawable_t)view->backImage->pixmap,
					      rc->xvisual,
					      view->backImage->width,
					      view->backImage->height);
		pat = cairo_pattern_create_for_surface(bg);
		cairo_pattern_set_extend(pat, CAIRO_EXTEND_REPEAT);
		cairo_set_source(cr, pat);
		cairo_paint(cr);
		cairo_pattern_destroy(pat);
		cairo_surface_destroy(bg);
	} else {
		W_SetSourceColor(cr, view->backColor ? view->backColor
							 : view->screen->gray);
		cairo_paint(cr);
	}
}

/* ---- balloon path ---------------------------------------------------- */

/* Rounded-rectangle body with a triangular tail on one edge, built as
 * a single closed path so the caller can fill_preserve() the interior
 * and stroke() the outline in one pass. side is a bitmask of
 * WCairoBalloonBottom / WCairoBalloonRight naming which corner the
 * tail points from, tail_len is the tail's protrusion past the body
 * edge. */
void W_BalloonPath(cairo_t *cr, double x, double y, double w, double h,
		       int side, double tail_len)
{
	double r, w1, base0, base1, apex, edge;

	r = h * 3.0 / 20.0;		/* corner radius */
	w1 = (h > 21.0) ? h : 21.0;	/* tail base scale */

	/* tail base points and apex on the body-edge axis, mirrored by
	 * the RIGHT bit so the tail sits near the pointed-at corner */
	if (side & WCairoBalloonRight) {
		base0 = x + w - w1 + 2.0 * w1 / 16.0;
		apex  = x + w - w1 + 11.0 * w1 / 16.0;
		base1 = x + w - w1 + 7.0 * w1 / 16.0;
	} else {
		base0 = x + w1 - 2.0 * w1 / 16.0;
		apex  = x + w1 - 11.0 * w1 / 16.0;
		base1 = x + w1 - 7.0 * w1 / 16.0;
	}

	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r,     r, -M_PI / 2, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);

	if (side & WCairoBalloonBottom) {
		edge = y + h;
		if (side & WCairoBalloonRight) {
			cairo_line_to(cr, base1, edge);
			cairo_line_to(cr, apex,  edge + tail_len);
			cairo_line_to(cr, base0, edge);
		} else {
			cairo_line_to(cr, base0, edge);
			cairo_line_to(cr, apex,  edge + tail_len);
			cairo_line_to(cr, base1, edge);
		}
	}

	cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
	cairo_arc(cr, x + r, y + r,     r, M_PI, 3 * M_PI / 2);

	if (!(side & WCairoBalloonBottom)) {
		edge = y;
		if (side & WCairoBalloonRight) {
			cairo_line_to(cr, base0, edge);
			cairo_line_to(cr, apex,  edge - tail_len);
			cairo_line_to(cr, base1, edge);
		} else {
			cairo_line_to(cr, base1, edge);
			cairo_line_to(cr, apex,  edge - tail_len);
			cairo_line_to(cr, base0, edge);
		}
	}

	cairo_close_path(cr);
}

/* Render the balloon shape into a fresh 1-bit server pixmap for
 * ShapeBounding and a fresh depth-N pixmap for the window background,
 * both width x height (which already includes the tail-length pad).
 * Caller draws text on top of *content and frees both. */
void W_RenderBalloon(W_Screen *scr, unsigned int width, unsigned int height,
			 double bx, double by, double bw, double bh,
			 int side, double tail_len,
			 WMColor *fill, WMColor *border,
			 xcb_pixmap_t *content, xcb_pixmap_t *mask)
{
	RContext *rc = scr->rcontext;
	cairo_surface_t *ms, *ps;
	cairo_t *cr;

	*mask = xcb_generate_id(rc->xc);
	xcb_create_pixmap(rc->xc, 1, *mask, rc->xscreen->root, width, height);
	*content = xcb_generate_id(rc->xc);
	xcb_create_pixmap(rc->xc, rc->xdepth, *content, rc->xscreen->root, width, height);

	/* mask: A1 surface on the depth-1 pixmap, clear then fill the
	 * path solid, stroke widens by half a pixel so the content
	 * outline is not clipped by the shape edge */
	ms = cairo_xcb_surface_create_for_bitmap(rc->xc, rc->xscreen,
						 *mask, width, height);
	cr = cairo_create(ms);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(cr, 0, 0, 0, 0);
	cairo_paint(cr);
	cairo_set_source_rgba(cr, 0, 0, 0, 1);
	W_BalloonPath(cr, bx, by, bw, bh, side, tail_len);
	cairo_fill_preserve(cr);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);
	cairo_destroy(cr);
	cairo_surface_flush(ms);
	cairo_surface_destroy(ms);

	/* content: fill in the border colour first so any pixel the
	 * shape mask exposes but the interior fill misses (sub-pixel
	 * rounding at the arcs) shows as border rather than junk, then
	 * fill the interior and stroke the outline */
	ps = cairo_xcb_surface_create(rc->xc, (xcb_drawable_t)*content,
				      rc->xvisual, width, height);
	cr = cairo_create(ps);
	W_SetSourceColor(cr, border);
	cairo_paint(cr);
	W_BalloonPath(cr, bx, by, bw, bh, side, tail_len);
	W_SetSourceColor(cr, fill);
	cairo_fill_preserve(cr);
	W_SetSourceColor(cr, border);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);
	cairo_destroy(cr);
	cairo_surface_flush(ps);
	cairo_surface_destroy(ps);

	WMScreenNoteDrawableSize(scr, *content, width, height);
}

/* ---- compositor probe ------------------------------------------------ */

int W_ScreenCheckCompositor(W_Screen *scr)
{
	xcb_connection_t *xc;
	xcb_get_selection_owner_reply_t *owner;
	char name[32];
	xcb_atom_t sel;
	int running;

	xc = scr->rcontext->xc;
	snprintf(name, sizeof(name), "_NET_WM_CM_S%d", scr->screen);
	sel = WXInternAtom(scr->display, name);

	owner = xcb_get_selection_owner_reply(xc,
			xcb_get_selection_owner(xc, (xcb_atom_t)sel), NULL);
	running = owner && owner->owner != XCB_NONE;
	free(owner);
	return running;
}
