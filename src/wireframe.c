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

#include <limits.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef USE_XSHAPE
#include <xcb/shape.h>
#endif

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
 * point count so Commit can walk them with one XDrawLines each */
#define POLY_MAX_RUNS 8

typedef struct WWireframe {
	WScreen *scr;
	Bool use_shape;

	/* shape path, one root-sized window whose ShapeBounding is the
	 * union of the outline bands */
	Window window;
	GC gc;			/* white pen for the outline cores */
	int win_width, win_height;	/* root size the window was created for */
	Bool mapped;

	WireframeOutline *outlines;	/* outlines accumulated since Begin */
	int count;
	int capacity;

	/* non-axis-aligned polylines rendered via a 1-bit shape mask,
	 * poly_mask is kept across commits and only the previous
	 * frame's dirty box is cleared */
	Pixmap poly_mask;
	GC poly_set_gc;		/* fg=1, band-width lines onto the mask */
	GC poly_clr_gc;		/* fg=0 fill for the dirty-box clear */
	XPoint *poly_pts;
	int poly_run[POLY_MAX_RUNS];	/* npoints per polyline */
	int poly_nruns;
	int poly_npts;
	int poly_pts_capacity;
	int dirty_x, dirty_y, dirty_w, dirty_h;	/* mask area to clear */

	/* no-XShape fallback, one white window with a black border per band */
	Window *bars;
	int bars_mapped;
	int bars_capacity;
} WWireframe;

static WWireframe *getWireframe(WScreen *scr)
{
	WWireframe *wf = scr->wireframe;

	if (!wf) {
		wf = wmalloc(sizeof(*wf));
		wf->scr = scr;
#ifdef USE_XSHAPE
		wf->use_shape = w_global.xext.shape.supported;
#endif
		scr->wireframe = wf;
	}

#ifdef USE_XSHAPE
	if (wf->use_shape) {
		if (wf->window != None
		    && (wf->win_width != scr->scr_width || wf->win_height != scr->scr_height)) {
			/* the root was resized under us, recreate at the new size */
			XFreeGC(dpy, wf->gc);
			XDestroyWindow(dpy, wf->window);
			wf->window = None;
			wf->mapped = False;
			if (wf->poly_mask != None) {
				XFreeGC(dpy, wf->poly_set_gc);
				XFreeGC(dpy, wf->poly_clr_gc);
				XFreePixmap(dpy, wf->poly_mask);
				wf->poly_mask = None;
				wf->dirty_w = 0;
			}
		}

		if (wf->window == None) {
			XSetWindowAttributes attribs;
			XClassHint class_hint;
			XGCValues gcv;

			attribs.override_redirect = True;
			attribs.save_under = True;
			attribs.background_pixel = scr->black_pixel;
			wf->window = XCreateWindow(dpy, scr->root_win, 0, 0,
						   scr->scr_width, scr->scr_height, 0,
						   CopyFromParent, InputOutput, CopyFromParent,
						   CWOverrideRedirect | CWSaveUnder | CWBackPixel,
						   &attribs);
			wf->win_width = scr->scr_width;
			wf->win_height = scr->scr_height;

			class_hint.res_name = (char *)"wireframe";
			class_hint.res_class = (char *)"WindowMaker";
			XSetClassHint(dpy, wf->window, &class_hint);

			/* never take input, the band must not generate
			 * crossing events or swallow clicks while it is up */
			xcb_shape_rectangles(xconn, XCB_SHAPE_SO_SET,
					     XCB_SHAPE_SK_INPUT,
					     XCB_CLIP_ORDERING_UNSORTED,
					     wf->window, 0, 0, 0, NULL);

			gcv.foreground = scr->white_pixel;
			gcv.line_width = 1;
			gcv.graphics_exposures = False;
			wf->gc = XCreateGC(dpy, wf->window,
					   GCForeground | GCLineWidth | GCGraphicsExposures, &gcv);
		}
	}
#endif

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

#ifdef USE_XSHAPE
static void drawOutlineCore(WWireframe *wf, const WireframeOutline *o)
{
	if (o->w >= 3 && o->h >= 3)
		XDrawRectangle(dpy, wf->window, wf->gc, o->x + 1, o->y + 1, o->w - 3, o->h - 3);
	if (o->title_y > 0)
		XDrawLine(dpy, wf->window, wf->gc, o->x + 1, o->title_y, o->x + o->w - 2, o->title_y);
	if (o->resize_y > 0)
		XDrawLine(dpy, wf->window, wf->gc, o->x + 1, o->resize_y, o->x + o->w - 2, o->resize_y);
}
#endif

static void commitBars(WWireframe *wf, const XRectangle *rects, int n)
{
	WScreen *scr = wf->scr;
	int i;

	if (n > wf->bars_capacity) {
		wf->bars = wrealloc(wf->bars, n * sizeof(*wf->bars));
		for (i = wf->bars_capacity; i < n; i++)
			wf->bars[i] = None;
		wf->bars_capacity = n;
	}

	for (i = 0; i < n; i++) {
		int w = rects[i].width > 2 ? rects[i].width - 2 : 1;
		int h = rects[i].height > 2 ? rects[i].height - 2 : 1;

		if (wf->bars[i] == None) {
			XSetWindowAttributes attribs;

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

void wWireframeBegin(WScreen *scr)
{
	WWireframe *wf = getWireframe(scr);

	wf->count = 0;
	wf->poly_nruns = 0;
	wf->poly_npts = 0;
}

#ifdef USE_XSHAPE
static void ensurePolyMask(WWireframe *wf)
{
	XGCValues gcv;

	if (wf->poly_mask != None)
		return;

	wf->poly_mask = XCreatePixmap(dpy, wf->scr->root_win,
				      wf->win_width, wf->win_height, 1);

	gcv.foreground = 1;
	gcv.line_width = WIREFRAME_THICKNESS;
	gcv.cap_style = CapRound;
	gcv.join_style = JoinRound;
	gcv.graphics_exposures = False;
	wf->poly_set_gc = XCreateGC(dpy, wf->poly_mask,
				    GCForeground | GCLineWidth | GCCapStyle |
				    GCJoinStyle | GCGraphicsExposures, &gcv);

	gcv.foreground = 0;
	wf->poly_clr_gc = XCreateGC(dpy, wf->poly_mask,
				    GCForeground | GCGraphicsExposures, &gcv);

	/* fresh pixmap contents are undefined, so the first commit must
	 * clear the whole thing, subsequent commits clear only the
	 * previous frame's bounding box */
	wf->dirty_x = 0;
	wf->dirty_y = 0;
	wf->dirty_w = wf->win_width;
	wf->dirty_h = wf->win_height;
}

static void renderPolyMask(WWireframe *wf)
{
	int i, off, x0, y0, x1, y1;

	ensurePolyMask(wf);

	if (wf->dirty_w > 0)
		XFillRectangle(dpy, wf->poly_mask, wf->poly_clr_gc,
			       wf->dirty_x, wf->dirty_y, wf->dirty_w, wf->dirty_h);

	x0 = y0 = INT_MAX;
	x1 = y1 = INT_MIN;
	for (i = 0; i < wf->poly_npts; i++) {
		if (wf->poly_pts[i].x < x0) x0 = wf->poly_pts[i].x;
		if (wf->poly_pts[i].y < y0) y0 = wf->poly_pts[i].y;
		if (wf->poly_pts[i].x > x1) x1 = wf->poly_pts[i].x;
		if (wf->poly_pts[i].y > y1) y1 = wf->poly_pts[i].y;
	}

	off = 0;
	for (i = 0; i < wf->poly_nruns; i++) {
		XDrawLines(dpy, wf->poly_mask, wf->poly_set_gc,
			   wf->poly_pts + off, wf->poly_run[i], CoordModeOrigin);
		off += wf->poly_run[i];
	}

	/* record the box (padded for line width and rounded caps) so the
	 * next commit clears only what this one drew */
	wf->dirty_x = x0 - WIREFRAME_THICKNESS;
	wf->dirty_y = y0 - WIREFRAME_THICKNESS;
	wf->dirty_w = (x1 - x0) + 2 * WIREFRAME_THICKNESS;
	wf->dirty_h = (y1 - y0) + 2 * WIREFRAME_THICKNESS;
}

static void drawPolyCores(WWireframe *wf)
{
	int i, off;

	off = 0;
	for (i = 0; i < wf->poly_nruns; i++) {
		XDrawLines(dpy, wf->window, wf->gc,
			   wf->poly_pts + off, wf->poly_run[i], CoordModeOrigin);
		off += wf->poly_run[i];
	}
}
#endif

void wWireframeAddPolyline(WScreen *scr, const XPoint *pts, int npts)
{
	WWireframe *wf = getWireframe(scr);
	int i;

	if (npts < 2 || !wf->use_shape || wf->poly_nruns >= POLY_MAX_RUNS)
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
	XRectangle *rects = NULL;
	int i, n;

	if (wf->count == 0 && wf->poly_nruns == 0) {
		wWireframeHide(scr);
		return;
	}

	n = 0;
	if (wf->count > 0) {
		rects = wmalloc(wf->count * 6 * sizeof(*rects));
		for (i = 0; i < wf->count; i++)
			n += outlineRects(&wf->outlines[i], rects + n);
	}

#ifdef USE_XSHAPE
	if (wf->use_shape) {
		/* set the full ShapeBounding before the first MapRaised so
		 * the window never appears at its default (unshaped, root
		 * sized) extent, when both sources are present the mask is
		 * unioned onto the rectangle region */
		if (wf->poly_nruns > 0)
			renderPolyMask(wf);
		if (n > 0) {
			xcb_shape_rectangles(xconn, XCB_SHAPE_SO_SET,
					     XCB_SHAPE_SK_BOUNDING,
					     XCB_CLIP_ORDERING_UNSORTED,
					     wf->window, 0, 0, n,
					     (xcb_rectangle_t *)rects);
			if (wf->poly_nruns > 0)
				xcb_shape_mask(xconn, XCB_SHAPE_SO_UNION,
					       XCB_SHAPE_SK_BOUNDING,
					       wf->window, 0, 0, wf->poly_mask);
		} else {
			xcb_shape_mask(xconn, XCB_SHAPE_SO_SET,
				       XCB_SHAPE_SK_BOUNDING,
				       wf->window, 0, 0, wf->poly_mask);
		}
		if (wf->mapped) {
			/* a workspace switch during the drag maps client
			 * frames on top of the stack, stay above them */
			XRaiseWindow(dpy, wf->window);
		} else {
			XMapRaised(dpy, wf->window);
			wf->mapped = True;
		}
		/* regions that stayed inside the new shape keep their
		 * contents, clear the previous step's cores and repaint */
		XClearWindow(dpy, wf->window);
		for (i = 0; i < wf->count; i++)
			drawOutlineCore(wf, &wf->outlines[i]);
		if (wf->poly_nruns > 0)
			drawPolyCores(wf);
	} else
#endif
	{
		commitBars(wf, rects, n);
	}

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

#ifdef USE_XSHAPE
	if (wf->use_shape) {
		if (wf->mapped) {
			XUnmapWindow(dpy, wf->window);
			wf->mapped = False;
		}
		return;
	}
#endif

	for (i = 0; i < wf->bars_mapped; i++)
		XUnmapWindow(dpy, wf->bars[i]);
	wf->bars_mapped = 0;
}
