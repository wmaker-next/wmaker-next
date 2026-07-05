/* wireframe.c - override-redirect outline window for move/resize feedback
 *
 *  Window Maker window manager
 *
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

#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef USE_XSHAPE
#include <xcb/shape.h>
#endif

#include <cairo.h>
#include <cairo-xcb.h>
#include <WINGs/WINGsP.h>

#include "WindowMaker.h"
#include "screen.h"
#include "wireframe.h"

/* width of the outline band, a 1 px white core between 1 px black edges
 * so the outline reads on both light and dark content below it */
#define WIREFRAME_THICKNESS 3

typedef struct WireframeOutline {
	int x, y;		/* outer box origin, border included */
	int w, h;		/* outer box size, border included */
	int title_y;		/* root y of the titlebar separator, 0 when absent */
	int resize_y;		/* root y of the resizebar separator, 0 when absent */
} WireframeOutline;

/* polylines are stored as a flat XPoint array plus a per-polyline
 * point count so commitCairo() can walk them once */
#define POLY_MAX_RUNS 8

typedef struct WWireframe {
	WScreen *scr;

	/* one root-sized ARGB32 window painted with a SOURCE-operator
	 * clear then an opaque band per outline. Taken whenever the
	 * server offers a depth-32 visual and the Shape extension. When
	 * a compositing manager owns _NET_WM_CM_Sn the alpha channel is
	 * blended so the interior fill is drawn translucent and the
	 * window is left unshaped, otherwise ShapeBounding is set to the
	 * band rectangles (and a stroked polyline mask) so the alpha=0
	 * regions are cut out rather than showing as opaque black. */
	Bool use_cairo;
	Bool composited;	/* refreshed on every wWireframeBegin() */
	Bool shaped;		/* ShapeBounding currently applied */
	WCairoSurface *csurf;

	Window window;
	int win_width, win_height;	/* root size the window was created for */
	Bool mapped;

	WireframeOutline *outlines;	/* outlines accumulated since Begin */
	int count;
	int capacity;

	XPoint *poly_pts;
	int poly_run[POLY_MAX_RUNS];	/* npoints per polyline */
	int poly_nruns;
	int poly_npts;
	int poly_pts_capacity;
	Pixmap poly_mask;	/* depth-1 root-sized, lazily created */

	/* no-depth-32 fallback, one white window with a black border per band */
	Window *bars;
	int bars_mapped;
	int bars_capacity;
} WWireframe;

static void createCairoWindow(WWireframe *wf)
{
	WScreen *scr = wf->scr;
	RContext *rc = scr->rcontext;
	XSetWindowAttributes attribs;
	XClassHint class_hint;

	/* background None: the server must not fill on map, expose, or a
	 * ShapeBounding grow, otherwise it clears the bands to the
	 * background pixel right after cairo has painted them */
	attribs.override_redirect = True;
	attribs.colormap = rc->cmap_argb;
	attribs.border_pixel = 0;
	attribs.background_pixmap = None;
	wf->window = XCreateWindow(dpy, scr->root_win, 0, 0,
				   scr->scr_width, scr->scr_height, 0,
				   32, InputOutput, scr->wmscreen->visual_argb,
				   CWOverrideRedirect | CWColormap | CWBorderPixel
				   | CWBackPixmap, &attribs);
	wf->win_width = scr->scr_width;
	wf->win_height = scr->scr_height;

	class_hint.res_name = (char *)"wireframe";
	class_hint.res_class = (char *)"WindowMaker";
	XSetClassHint(dpy, wf->window, &class_hint);

#ifdef USE_XSHAPE
	/* empty ShapeInput so the band never eats pointer events, this is
	 * orthogonal to whether ShapeBounding is used for the visual cut */
	xcb_shape_rectangles(xconn, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_INPUT,
			     XCB_CLIP_ORDERING_UNSORTED, wf->window, 0, 0, 0, NULL);
#endif

	wf->csurf = WCairoSurfaceCreate(scr->wmscreen, wf->window,
					scr->scr_width, scr->scr_height,
					WCairoARGB);
}

static WWireframe *getWireframe(WScreen *scr)
{
	WWireframe *wf = scr->wireframe;

	if (!wf) {
		wf = wmalloc(sizeof(*wf));
		wf->scr = scr;
#ifdef USE_XSHAPE
		/* Shape is required either way: composited for the empty
		 * ShapeInput click-through, uncomposited for ShapeBounding
		 * as well. Without it fall through to the bars path. */
		wf->use_cairo = scr->rcontext->xvisual_argb
				&& scr->wmscreen->visual_argb
				&& w_global.xext.shape.supported;
#endif
		scr->wireframe = wf;
	}

	if (!wf->use_cairo)
		return wf;

	if (wf->window != None
	    && (wf->win_width != scr->scr_width || wf->win_height != scr->scr_height)) {
		WCairoSurfaceDestroy(wf->csurf);
		XDestroyWindow(dpy, wf->window);
		wf->window = None;
		wf->csurf = NULL;
		wf->mapped = False;
		wf->shaped = False;
		if (wf->poly_mask != None) {
			XFreePixmap(dpy, wf->poly_mask);
			wf->poly_mask = None;
		}
	}
	if (wf->window == None)
		createCairoWindow(wf);

	return wf;
}

static void setRect(XRectangle *r, int x, int y, int w, int h)
{
	r->x = x;
	r->y = y;
	r->width = w;
	r->height = h;
}

/* expand one outline into its band rectangles, at most 6 */
static int outlineRects(const WireframeOutline *o, XRectangle *rects)
{
	const int t = WIREFRAME_THICKNESS;
	int n = 0;

	if (o->w <= 2 * t || o->h <= 2 * t) {
		setRect(&rects[n++], o->x, o->y, o->w, o->h);
	} else {
		setRect(&rects[n++], o->x, o->y, o->w, t);
		setRect(&rects[n++], o->x, o->y + o->h - t, o->w, t);
		setRect(&rects[n++], o->x, o->y + t, t, o->h - 2 * t);
		setRect(&rects[n++], o->x + o->w - t, o->y + t, t, o->h - 2 * t);
	}
	if (o->title_y > 0)
		setRect(&rects[n++], o->x, o->title_y - 1, o->w, 3);
	if (o->resize_y > 0)
		setRect(&rects[n++], o->x, o->resize_y - 1, o->w, 3);

	return n;
}

static void commitBars(WWireframe *wf, const XRectangle *rects, int n)
{
	WScreen *scr = wf->scr;
	XSetWindowAttributes attribs;
	int i, w, h;

	if (n > wf->bars_capacity) {
		wf->bars = wrealloc(wf->bars, n * sizeof(*wf->bars));
		for (i = wf->bars_capacity; i < n; i++)
			wf->bars[i] = None;
		wf->bars_capacity = n;
	}

	for (i = 0; i < n; i++) {
		w = rects[i].width > 2 ? rects[i].width - 2 : 1;
		h = rects[i].height > 2 ? rects[i].height - 2 : 1;

		if (wf->bars[i] == None) {
			attribs.override_redirect = True;
			attribs.save_under = True;
			attribs.background_pixel = scr->white_pixel;
			attribs.border_pixel = scr->black_pixel;
			wf->bars[i] = XCreateWindow(dpy, scr->root_win,
						    rects[i].x, rects[i].y, w, h, 1,
						    CopyFromParent, InputOutput, CopyFromParent,
						    CWOverrideRedirect | CWSaveUnder
						    | CWBackPixel | CWBorderPixel, &attribs);
		} else {
			XMoveResizeWindow(dpy, wf->bars[i], rects[i].x, rects[i].y, w, h);
		}
		if (i >= wf->bars_mapped)
			XMapRaised(dpy, wf->bars[i]);
	}

	for (i = n; i < wf->bars_mapped; i++)
		XUnmapWindow(dpy, wf->bars[i]);
	wf->bars_mapped = n;
}

/* build every accumulated polyline as a single cairo path so one stroke
 * covers them, used both for the visible band/core and the shape mask */
static void addPolylinePath(cairo_t *cr, const WWireframe *wf)
{
	int i, j, off;

	off = 0;
	for (i = 0; i < wf->poly_nruns; i++) {
		cairo_move_to(cr, wf->poly_pts[off].x, wf->poly_pts[off].y);
		for (j = 1; j < wf->poly_run[i]; j++)
			cairo_line_to(cr, wf->poly_pts[off + j].x,
				      wf->poly_pts[off + j].y);
		off += wf->poly_run[i];
	}
}

/* one closed rounded-rect outline: translucent interior when composited,
 * then a WIREFRAME_THICKNESS opaque black band with a 1 px white core,
 * and the optional titlebar/resizebar separator lines on top */
static void paintCairoOutline(cairo_t *cr, const WireframeOutline *o, Bool fill)
{
	if (fill) {
		cairo_set_source_rgba(cr, 0.20, 0.40, 0.60, 0.30);
		cairo_rectangle(cr, o->x, o->y, o->w, o->h);
		cairo_fill(cr);
	}

	cairo_set_line_width(cr, WIREFRAME_THICKNESS);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_rectangle(cr, o->x + 1.5, o->y + 1.5, o->w - 3, o->h - 3);
	cairo_stroke(cr);

	cairo_set_line_width(cr, 1.0);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_rectangle(cr, o->x + 1.5, o->y + 1.5, o->w - 3, o->h - 3);
	cairo_stroke(cr);

	if (o->title_y > 0) {
		cairo_move_to(cr, o->x + 1, o->title_y + 0.5);
		cairo_line_to(cr, o->x + o->w - 1, o->title_y + 0.5);
	}
	if (o->resize_y > 0) {
		cairo_move_to(cr, o->x + 1, o->resize_y + 0.5);
		cairo_line_to(cr, o->x + o->w - 1, o->resize_y + 0.5);
	}
	cairo_stroke(cr);
}

#ifdef USE_XSHAPE
/* set ShapeBounding to the union of the outline band rectangles and, when
 * polylines are present, a depth-1 mask stroked with the same paths so the
 * transparent regions of the ARGB window are cut out on the uncomposited
 * path. The mask is root-sized and reused across commits. */
static void applyShapeBounding(WWireframe *wf)
{
	RContext *rc = wf->scr->rcontext;
	cairo_surface_t *ms;
	cairo_t *cr;
	XRectangle *rects;
	int i, n, op;

	n = 0;
	rects = wf->count ? wmalloc(wf->count * 6 * sizeof(*rects)) : NULL;
	for (i = 0; i < wf->count; i++)
		n += outlineRects(&wf->outlines[i], rects + n);

	op = XCB_SHAPE_SO_SET;
	if (n > 0) {
		xcb_shape_rectangles(xconn, op, XCB_SHAPE_SK_BOUNDING,
				     XCB_CLIP_ORDERING_UNSORTED,
				     wf->window, 0, 0, n,
				     (xcb_rectangle_t *)rects);
		op = XCB_SHAPE_SO_UNION;
	}
	wfree(rects);

	if (wf->poly_nruns > 0) {
		if (wf->poly_mask == None) {
			wf->poly_mask = xcb_generate_id(rc->xc);
			xcb_create_pixmap(rc->xc, 1, wf->poly_mask,
					  rc->xscreen->root,
					  wf->win_width, wf->win_height);
		}
		ms = cairo_xcb_surface_create_for_bitmap(rc->xc, rc->xscreen,
							 (xcb_pixmap_t)wf->poly_mask,
							 wf->win_width,
							 wf->win_height);
		cr = cairo_create(ms);
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_rgba(cr, 0, 0, 0, 0);
		cairo_paint(cr);
		cairo_set_source_rgba(cr, 0, 0, 0, 1);
		cairo_set_line_width(cr, WIREFRAME_THICKNESS);
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
		cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
		addPolylinePath(cr, wf);
		cairo_stroke(cr);
		cairo_destroy(cr);
		cairo_surface_flush(ms);
		cairo_surface_destroy(ms);

		xcb_shape_mask(xconn, op, XCB_SHAPE_SK_BOUNDING,
			       wf->window, 0, 0, wf->poly_mask);
	}
	wf->shaped = True;
}
#endif

static void commitCairo(WWireframe *wf)
{
	cairo_t *cr;
	int i;

#ifdef USE_XSHAPE
	if (!wf->composited) {
		/* shape before painting: growing ShapeBounding exposes the
		 * newly-included bands and would clear them to the window
		 * background if it ran after the paint, and shaping before
		 * the first MapRaised keeps the window from ever appearing
		 * at its unshaped root-sized extent */
		applyShapeBounding(wf);
	} else if (wf->shaped) {
		/* a compositor started mid-session, drop the bounding shape
		 * so the translucent interior fill shows */
		xcb_shape_mask(xconn, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING,
			       wf->window, 0, 0, XCB_NONE);
		wf->shaped = False;
	}
#endif

	cr = WCairoBegin(wf->csurf);

	/* SOURCE so the clear replaces rather than blends over the
	 * previous frame's alpha, then back to OVER for the outlines so
	 * the white core sits on top of the black band */
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(cr, 0, 0, 0, 0);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	for (i = 0; i < wf->count; i++)
		paintCairoOutline(cr, &wf->outlines[i], wf->composited);

	if (wf->poly_nruns > 0) {
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
		cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
		addPolylinePath(cr, wf);
		cairo_set_line_width(cr, WIREFRAME_THICKNESS);
		cairo_set_source_rgb(cr, 0, 0, 0);
		cairo_stroke_preserve(cr);
		cairo_set_line_width(cr, 1.0);
		cairo_set_source_rgb(cr, 1, 1, 1);
		cairo_stroke(cr);
	}

	WCairoEnd(cr);

	if (wf->mapped)
		XRaiseWindow(dpy, wf->window);
	else {
		XMapRaised(dpy, wf->window);
		wf->mapped = True;
	}
}

void wWireframeBegin(WScreen *scr)
{
	WWireframe *wf = getWireframe(scr);

	wf->count = 0;
	wf->poly_nruns = 0;
	wf->poly_npts = 0;
	/* only re-probe the compositor at the start of a drag, not on
	 * every motion step, the check is a synchronous round trip */
	if (wf->use_cairo && !wf->mapped)
		wf->composited = W_ScreenCheckCompositor(scr->wmscreen);
}

void wWireframeAddPolyline(WScreen *scr, const XPoint *pts, int npts)
{
	WWireframe *wf = getWireframe(scr);
	int i;

	if (npts < 2 || !wf->use_cairo || wf->poly_nruns >= POLY_MAX_RUNS)
		return;

	if (wf->poly_npts + npts > wf->poly_pts_capacity) {
		wf->poly_pts_capacity = wf->poly_npts + npts + 16;
		wf->poly_pts = wrealloc(wf->poly_pts,
					wf->poly_pts_capacity * sizeof(*wf->poly_pts));
	}
	for (i = 0; i < npts; i++)
		wf->poly_pts[wf->poly_npts + i] = pts[i];
	wf->poly_npts += npts;
	wf->poly_run[wf->poly_nruns++] = npts;
}

void wWireframeAddOutline(WScreen *scr, int x, int y, int width, int height,
			  int titlebar_h, int resizebar_h, int border)
{
	WWireframe *wf = getWireframe(scr);
	WireframeOutline *o;

	if (wf->count == wf->capacity) {
		wf->capacity = wf->capacity ? wf->capacity * 2 : 8;
		wf->outlines = wrealloc(wf->outlines, wf->capacity * sizeof(*wf->outlines));
	}

	o = &wf->outlines[wf->count++];
	o->x = x;
	o->y = y;
	o->w = width + 2 * border;
	o->h = height + 2 * border;
	o->title_y = titlebar_h > 0 ? y + border + titlebar_h - 1 : 0;
	o->resize_y = resizebar_h > 0 ? y + border + height - resizebar_h : 0;
}

void wWireframeCommit(WScreen *scr)
{
	WWireframe *wf = getWireframe(scr);
	XRectangle *rects;
	int i, n;

	if (wf->count == 0 && wf->poly_nruns == 0) {
		wWireframeHide(scr);
		return;
	}

	if (wf->use_cairo) {
		commitCairo(wf);
		return;
	}

	n = 0;
	rects = wmalloc(wf->count * 6 * sizeof(*rects));
	for (i = 0; i < wf->count; i++)
		n += outlineRects(&wf->outlines[i], rects + n);
	commitBars(wf, rects, n);
	wfree(rects);
}

void wWireframeHide(WScreen *scr)
{
	WWireframe *wf = scr->wireframe;
	int i;

	if (!wf)
		return;

	wf->count = 0;
	wf->poly_nruns = 0;
	wf->poly_npts = 0;

	if (wf->use_cairo) {
		if (wf->mapped) {
			XUnmapWindow(dpy, wf->window);
			wf->mapped = False;
		}
		return;
	}

	for (i = 0; i < wf->bars_mapped; i++)
		XUnmapWindow(dpy, wf->bars[i]);
	wf->bars_mapped = 0;
}
