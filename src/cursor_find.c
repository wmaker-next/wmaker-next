/* cursor_find.c - Find My Mouse pointer highlight
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

#ifdef USE_XSHAPE
#include <xcb/shape.h>
#endif

#include <cairo.h>
#include <cairo-xcb.h>
#include <WINGs/WINGsP.h>

#include "WindowMaker.h"
#include "screen.h"
#include "texture.h"
#include "xinerama.h"
#include "cursor.h"
#include "cursor_find.h"

#define FM_CORE_WIDTH	4
#define FM_EDGE_WIDTH	1
#define FM_MARGIN	1
#define FM_RING_HALF	(FM_CORE_WIDTH / 2 + FM_EDGE_WIDTH)
#define FM_SHAPE_HALF	(FM_RING_HALF + 1)
#define FM_WINDOW_HALF	(FM_SHAPE_HALF + FM_MARGIN)
#define FM_TRACK_MS	50
#define FM_MIN_RADIUS	32
#define FM_MAX_RADIUS	1024
#define FM_TWO_PI	(2.0 * 3.14159265358979323846)

struct WFindMouse {
	WScreen *scr;
	xcb_window_t win;
	int win_size;
	int radius;
	bool active;
	WMHandlerID track_timer;
	WMHandlerID end_timer;
	WCairoSurface *csurf;
};

typedef struct WFindMouse WFindMouse;

static void stopEffect(struct WFindMouse *fm);
static void destroyRing(struct WFindMouse *fm);
static void createRing(struct WFindMouse *fm, int radius);

static bool warned_unavailable;

static bool findMouseAvailable(void)
{
#ifdef USE_XSHAPE
	if (!wdisp->xext.shape.supported) {
		if (!warned_unavailable) {
			wwarning(_("find mouse: the XShape extension is not available"));
			warned_unavailable = true;
		}
		return false;
	}
	if (wdisp->xext.shape.minor_version < 1) {
		if (!warned_unavailable) {
			wwarning(_("find mouse: XShape 1.1 is required for a click-through ring"));
			warned_unavailable = true;
		}
		return false;
	}
	return true;
#else
	if (!warned_unavailable) {
		wwarning(_("find mouse: this build has no XShape support"));
		warned_unavailable = true;
	}
	return false;
#endif
}

static int autoRadius(WScreen *scr)
{
	WMRect rect;
	int head;
	int side;
	int radius;

	head = wGetHeadForPointerLocation(scr);
	rect = wGetRectForHead(scr, head);
	side = rect.size.width < rect.size.height ? rect.size.width
						  : rect.size.height;
	radius = side / 12;

	if (radius < FM_MIN_RADIUS)
		radius = FM_MIN_RADIUS;
	else if (radius > FM_MAX_RADIUS)
		radius = FM_MAX_RADIUS;

	return radius;
}

static int ringRadius(WScreen *scr)
{
	int radius = w_global.config->find_mouse_radius;

	/* 0 sizes the ring from the monitor under the pointer */
	if (radius <= 0)
		return autoRadius(scr);
	if (radius < FM_MIN_RADIUS)
		radius = FM_MIN_RADIUS;
	else if (radius > FM_MAX_RADIUS)
		radius = FM_MAX_RADIUS;

	return radius;
}

static int windowSizeForRadius(int radius)
{
	return 2 * (radius + FM_WINDOW_HALF);
}

static WFindMouse *getState(WScreen *scr)
{
	WFindMouse *fm = scr->findmouse;

	if (!fm) {
		fm = wmalloc(sizeof(*fm));
		fm->scr = scr;
		fm->win = XCB_NONE;
		scr->findmouse = fm;
	}

	return fm;
}

/* the focused titlebar's text and background keep the ring on style
 * and give it a contrasting pair, with white on black as the fallback */
static void setRingEdgeColor(cairo_t *cr, WScreen *scr)
{
	union WTexture *texture = scr->window_title_texture[WS_FOCUSED];
	WMColor *border = scr->frame_focused_border_color;

	if (texture && texture->any.type == WTEX_SOLID) {
		const WXColor *color = &texture->solid.normal;

		cairo_set_source_rgb(cr, color->red / 65535.0,
				     color->green / 65535.0,
				     color->blue / 65535.0);
	} else if (border) {
		cairo_set_source_rgb(cr, border->color.red / 65535.0,
				     border->color.green / 65535.0,
				     border->color.blue / 65535.0);
	} else {
		cairo_set_source_rgb(cr, 0, 0, 0);
	}
}

static void setRingCoreColor(cairo_t *cr, WScreen *scr)
{
	WMColor *text = scr->window_title_color[WS_FOCUSED];

	if (text) {
		cairo_set_source_rgb(cr, text->color.red / 65535.0,
				     text->color.green / 65535.0,
				     text->color.blue / 65535.0);
	} else {
		cairo_set_source_rgb(cr, 1, 1, 1);
	}
}

static void paintRing(WFindMouse *fm)
{
	double center = fm->win_size / 2.0;
	cairo_t *cr;

	if (fm->win == XCB_NONE)
		return;

	if (!fm->csurf)
		fm->csurf = WCairoSurfaceCreate(fm->scr->wmscreen, fm->win,
						fm->win_size, fm->win_size, 0);
	if (!fm->csurf)
		return;

	cr = WCairoBegin(fm->csurf);

	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	setRingEdgeColor(cr, fm->scr);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	setRingEdgeColor(cr, fm->scr);
	cairo_set_line_width(cr, FM_CORE_WIDTH + 2 * FM_EDGE_WIDTH);
	cairo_arc(cr, center, center, fm->radius, 0, FM_TWO_PI);
	cairo_stroke(cr);

	setRingCoreColor(cr, fm->scr);
	cairo_set_line_width(cr, FM_CORE_WIDTH);
	cairo_arc(cr, center, center, fm->radius, 0, FM_TWO_PI);
	cairo_stroke(cr);

	WCairoEnd(cr);
}

#ifdef USE_XSHAPE
static void applyRingShape(WFindMouse *fm)
{
	RContext *rc = fm->scr->rcontext;
	double center = fm->win_size / 2.0;
	xcb_pixmap_t mask;
	cairo_surface_t *ms;
	cairo_t *cr;

	mask = xcb_generate_id(wdisp->conn);
	xcb_create_pixmap(wdisp->conn, 1, mask, rc->xscreen->root,
			  fm->win_size, fm->win_size);

	ms = cairo_xcb_surface_create_for_bitmap(rc->xc, rc->xscreen, mask,
						 fm->win_size, fm->win_size);
	cr = cairo_create(ms);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(cr, 0, 0, 0, 0);
	cairo_paint(cr);
	cairo_set_source_rgba(cr, 0, 0, 0, 1);
	cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
	cairo_arc(cr, center, center, fm->radius + FM_SHAPE_HALF, 0,
		  FM_TWO_PI);
	cairo_arc(cr, center, center, fm->radius - FM_SHAPE_HALF, 0,
		  FM_TWO_PI);
	cairo_fill(cr);
	cairo_destroy(cr);
	cairo_surface_flush(ms);
	cairo_surface_destroy(ms);

	xcb_shape_mask(wdisp->conn, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING,
		       fm->win, 0, 0, mask);
	xcb_free_pixmap(wdisp->conn, mask);
}
#endif

static void destroyRing(WFindMouse *fm)
{
	if (fm->csurf) {
		WCairoSurfaceDestroy(fm->csurf);
		fm->csurf = NULL;
	}
	if (fm->win != XCB_NONE) {
		xcb_destroy_window(wdisp->conn, fm->win);
		fm->win = XCB_NONE;
	}
	fm->win_size = 0;
	fm->radius = 0;
}

static void createRing(WFindMouse *fm, int radius)
{
	int size = windowSizeForRadius(radius);
	uint32_t values[4];

	destroyRing(fm);

	fm->win = xcb_generate_id(wdisp->conn);
	fm->win_size = size;
	fm->radius = radius;

	/* value order follows the XCB_CW_* bit order */
	values[0] = XCB_BACK_PIXMAP_NONE;
	values[1] = 0;
	values[2] = true;
	values[3] = true;

	xcb_create_window(wdisp->conn, XCB_COPY_FROM_PARENT, fm->win,
			  fm->scr->root_win, 0, 0, size, size, 0,
			  XCB_WINDOW_CLASS_INPUT_OUTPUT,
			  XCB_COPY_FROM_PARENT,
			  XCB_CW_BACK_PIXMAP | XCB_CW_BORDER_PIXEL
			  | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_SAVE_UNDER,
			  values);

	wXSetClassHint(wdisp->conn, fm->win, "findmouse", "WindowMaker");

#ifdef USE_XSHAPE
	/* empty ShapeInput so the ring never eats pointer events */
	xcb_shape_rectangles(wdisp->conn, XCB_SHAPE_SO_SET,
			     XCB_SHAPE_SK_INPUT, XCB_CLIP_ORDERING_UNSORTED,
			     fm->win, 0, 0, 0, NULL);
	applyRingShape(fm);
#endif
}

static void moveRing(WFindMouse *fm, int x, int y)
{
	int half = fm->win_size / 2;

	xcb_configure_window(wdisp->conn, fm->win,
			     XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
			     (const uint32_t[]){ (uint32_t)(x - half),
						 (uint32_t)(y - half) });
	xcb_configure_window(wdisp->conn, fm->win,
			     XCB_CONFIG_WINDOW_STACK_MODE,
			     (const uint32_t[]){ XCB_STACK_MODE_ABOVE });
}

static void stopTimerHandlers(WFindMouse *fm)
{
	WMDeleteTimerHandlerPtr(&fm->track_timer);
	WMDeleteTimerHandlerPtr(&fm->end_timer);
}

static void stopEffect(WFindMouse *fm)
{
	fm->active = false;
	stopTimerHandlers(fm);
	if (fm->win != XCB_NONE)
		xcb_unmap_window(wdisp->conn, fm->win);
}

static void trackTimerCallback(void *data)
{
	WFindMouse *fm = data;
	int x, y;

	fm->track_timer = NULL;
	if (!fm->active)
		return;

	if (!wCursorQueryPointer(fm->scr, &x, &y, NULL)) {
		stopEffect(fm);
		return;
	}

	moveRing(fm, x, y);
	paintRing(fm);
	xcb_flush(wdisp->conn);

	fm->track_timer = WMAddTimerHandler(FM_TRACK_MS, trackTimerCallback,
					    fm);
}

static void endTimerCallback(void *data)
{
	WFindMouse *fm = data;

	fm->end_timer = NULL;
	stopEffect(fm);
	xcb_flush(wdisp->conn);
}

static void destroyState(WScreen *scr)
{
	WFindMouse *fm = scr->findmouse;

	if (!fm)
		return;

	stopEffect(fm);
	destroyRing(fm);
	wfree(fm);
	scr->findmouse = NULL;
}

void wFindMouseShow(WScreen *scr)
{
	WFindMouse *fm;
	int x = 0, y = 0;
	int duration;
	int radius;
	int i;

	if (!scr)
		return;
	if (!findMouseAvailable())
		return;

	if (!wCursorQueryPointer(scr, &x, &y, NULL)) {
		WScreen *owner = NULL;

		for (i = 0; i < wdisp->screen_count; i++) {
			WScreen *candidate = wScreenWithNumber(i);

			if (candidate && wCursorQueryPointer(candidate, &x, &y, NULL)) {
				owner = candidate;
				break;
			}
		}
		if (!owner)
			return;
		scr = owner;
	}

	fm = getState(scr);
	radius = ringRadius(scr);
	if (fm->win == XCB_NONE || fm->win_size != windowSizeForRadius(radius))
		createRing(fm, radius);
	if (fm->win == XCB_NONE)
		return;

	fm->active = true;
	stopTimerHandlers(fm);

	moveRing(fm, x, y);
	paintRing(fm);
	xcb_map_window(wdisp->conn, fm->win);
	xcb_configure_window(wdisp->conn, fm->win,
			     XCB_CONFIG_WINDOW_STACK_MODE,
			     (const uint32_t[]){ XCB_STACK_MODE_ABOVE });
	xcb_flush(wdisp->conn);

	duration = w_global.config->find_mouse_duration;
	if (duration < 250)
		duration = 250;
	else if (duration > 30000)
		duration = 30000;

	fm->track_timer = WMAddTimerHandler(FM_TRACK_MS, trackTimerCallback,
					    fm);
	fm->end_timer = WMAddTimerHandler(duration, endTimerCallback, fm);
}

void wFindMouseApplyConfig(WScreen *scr)
{
	if (scr && scr->findmouse)
		destroyState(scr);
}

void wFindMouseShutdown(void)
{
	int i;

	for (i = 0; i < wdisp->screen_count; i++) {
		WScreen *scr = wScreenWithNumber(i);

		if (scr)
			destroyState(scr);
	}
	xcb_flush(wdisp->conn);
}
