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

	if (wf->count == 0) {
		wWireframeHide(scr);
		return;
	}

	rects = wmalloc(wf->count * 6 * sizeof(*rects));
	n = 0;
	for (i = 0; i < wf->count; i++)
		n += outlineRects(&wf->outlines[i], rects + n);

#ifdef USE_XSHAPE
	if (wf->use_shape) {
		xcb_shape_rectangles(xconn, XCB_SHAPE_SO_SET,
				     XCB_SHAPE_SK_BOUNDING,
				     XCB_CLIP_ORDERING_UNSORTED,
				     wf->window, 0, 0, n,
				     (xcb_rectangle_t *)rects);
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
