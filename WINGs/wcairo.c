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
	cairo_surface_t *surface;
	int width;
	int height;
};

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

	/* the shared XftDraw may still be pointed at d, retarget it to
	 * the scratch drawable so the next XftDrawChange() does not try
	 * to free a Render Picture the server already auto-freed when
	 * the caller destroys d. Goes away with the XftDraw itself when
	 * the pangocairo render sink lands. */
	W_XftAbandonDrawable(scr, d);
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

WCairoSurface *WCairoSurfaceCreate(W_Screen *scr, Drawable d, int width, int height, Bool argb)
{
	WCairoSurface *cs;
	RContext *rc;
	xcb_visualtype_t *visual;

	rc = scr->rcontext;
	visual = (argb && rc->xvisual_argb) ? rc->xvisual_argb : rc->xvisual;

	cs = wmalloc(sizeof(*cs));
	cs->width = width;
	cs->height = height;
	cs->surface = cairo_xcb_surface_create(rc->xc, (xcb_drawable_t)d,
					       visual, width, height);
	if (cairo_surface_status(cs->surface) != CAIRO_STATUS_SUCCESS) {
		wwarning("cairo_xcb_surface_create: %s",
			 cairo_status_to_string(cairo_surface_status(cs->surface)));
		cairo_surface_destroy(cs->surface);
		wfree(cs);
		return NULL;
	}
	return cs;
}

void WCairoSurfaceResize(WCairoSurface *cs, int width, int height)
{
	if (!cs || (cs->width == width && cs->height == height))
		return;
	cairo_xcb_surface_set_size(cs->surface, width, height);
	cs->width = width;
	cs->height = height;
}

void WCairoSurfaceDestroy(WCairoSurface *cs)
{
	if (!cs)
		return;
	cairo_surface_destroy(cs->surface);
	wfree(cs);
}

cairo_t *WCairoBegin(WCairoSurface *cs)
{
	return cairo_create(cs->surface);
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
	cairo_surface_t *surface;
	cairo_status_t st;

	if (!cr)
		return;

	surface = cairo_get_target(cr);
	cairo_surface_flush(surface);
	st = cairo_status(cr);
	if (st != CAIRO_STATUS_SUCCESS)
		wwarning("cairo: %s", cairo_status_to_string(st));
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
