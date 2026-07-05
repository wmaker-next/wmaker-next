/* wcairo.c - cairo-xcb surface acquisition for WINGs
 *
 *  Window Maker WINGs library
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
	int width;
	int height;
	int flags;
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

void WMScreenNoteDrawableSize(WMScreen *scr, Drawable d, unsigned width, unsigned height)
{
	uintptr_t packed;

	if (!scr || !scr->drawableSize || d == None)
		return;

	packed = ((uintptr_t)(width & 0xffff) << 16) | (height & 0xffff);
	WMHashInsert(scr->drawableSize, (const void *)(uintptr_t)d, (void *)packed);
}

void WMScreenAbandonDrawable(WMScreen *scr, Drawable d)
{
	if (!scr || d == None)
		return;

	if (scr->drawableSize)
		WMHashRemove(scr->drawableSize, (const void *)(uintptr_t)d);
}

static int lookupDrawableSize(W_Screen *scr, Drawable d, int *w, int *h)
{
	xcb_connection_t *xc;
	xcb_get_geometry_reply_t *geom;
	void *val;

	val = WMHashGet(scr->drawableSize, (const void *)(uintptr_t)d);
	if (val) {
		uintptr_t packed = (uintptr_t)val;

		*w = (int)((packed >> 16) & 0xffff);
		*h = (int)(packed & 0xffff);
		return 1;
	}

	/* not fed by any create/resize chokepoint, ask the server once */
	xc = scr->rcontext->xc;
	geom = xcb_get_geometry_reply(xc, xcb_get_geometry(xc, (xcb_drawable_t)d), NULL);
	if (!geom)
		return 0;
	*w = geom->width;
	*h = geom->height;
	free(geom);

	WMScreenNoteDrawableSize(scr, d, (unsigned)*w, (unsigned)*h);
	return 1;
}

/* ---- surface lifecycle ----------------------------------------------- */

WCairoSurface *WCairoSurfaceCreate(W_Screen *scr, Drawable d, int width, int height, int flags)
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
	return cr;
}

/* Transient path for callers that only have a bare Drawable (chiefly
 * WMDrawString). Creates a surface for the duration of one paint, size
 * pulled from the cache above so there is no round trip on the window
 * path. WCairoEnd() destroys both the cairo_t and its surface. */
cairo_t *WCairoBeginForDrawable(W_Screen *scr, Drawable d)
{
	cairo_surface_t *surface;
	cairo_t *cr;
	int w, h;

	if (!lookupDrawableSize(scr, d, &w, &h))
		return NULL;

	surface = cairo_xcb_surface_create(scr->rcontext->xc, (xcb_drawable_t)d,
					   scr->rcontext->xvisual, w, h);
	cr = cairo_create(surface);
	/* cairo_t holds its own reference, drop ours so WCairoEnd()'s
	 * cairo_destroy() releases the last one */
	cairo_surface_destroy(surface);
	return cr;
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
}

/* ---- source colour helpers ------------------------------------------- */

void WCairoSetSourceColor(cairo_t *cr, WMColor *color)
{
	cairo_set_source_rgba(cr,
			      color->color.red   / 65535.0,
			      color->color.green / 65535.0,
			      color->color.blue  / 65535.0,
			      color->alpha       / 65535.0);
}

void WCairoSetSourceRColor(cairo_t *cr, const RColor *color)
{
	cairo_set_source_rgba(cr,
			      color->red   / 255.0,
			      color->green / 255.0,
			      color->blue  / 255.0,
			      color->alpha / 255.0);
}

void WCairoSetSourceXColor(cairo_t *cr, const XColor *color)
{
	cairo_set_source_rgb(cr,
			     color->red   / 65535.0,
			     color->green / 65535.0,
			     color->blue  / 65535.0);
}

/* ---- balloon path ---------------------------------------------------- */

/* Rounded-rectangle body with a triangular tail on one edge, built as
 * a single closed path so the caller can fill_preserve() the interior
 * and stroke() the outline in one pass. side is a bitmask of
 * WCairoBalloonBottom / WCairoBalloonRight naming which corner the
 * tail points from, tail_len is the tail's protrusion past the body
 * edge. */
void WCairoBalloonPath(cairo_t *cr, double x, double y, double w, double h,
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
void WCairoRenderBalloon(W_Screen *scr, unsigned width, unsigned height,
			 double bx, double by, double bw, double bh,
			 int side, double tail_len,
			 WMColor *fill, WMColor *border,
			 Pixmap *content, Pixmap *mask)
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
						 (xcb_pixmap_t)*mask, width, height);
	cr = cairo_create(ms);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(cr, 0, 0, 0, 0);
	cairo_paint(cr);
	cairo_set_source_rgba(cr, 0, 0, 0, 1);
	WCairoBalloonPath(cr, bx, by, bw, bh, side, tail_len);
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
	WCairoSetSourceColor(cr, border);
	cairo_paint(cr);
	WCairoBalloonPath(cr, bx, by, bw, bh, side, tail_len);
	WCairoSetSourceColor(cr, fill);
	cairo_fill_preserve(cr);
	WCairoSetSourceColor(cr, border);
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
	Atom sel;
	int running;

	xc = scr->rcontext->xc;
	snprintf(name, sizeof(name), "_NET_WM_CM_S%d", scr->screen);
	sel = XInternAtom(scr->display, name, False);

	owner = xcb_get_selection_owner_reply(xc,
			xcb_get_selection_owner(xc, (xcb_atom_t)sel), NULL);
	running = owner && owner->owner != XCB_NONE;
	free(owner);
	return running;
}
