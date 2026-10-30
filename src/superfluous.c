/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 1998-2003 Dan Pascu
 *  Copyright (c) 2009-2026 Window Maker Team
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
#include <unistd.h>
#include <math.h>
#include <time.h>

#include <cairo-xcb.h>

#include <wraster.h>

#include "WindowMaker.h"
#include "screen.h"
#include "superfluous.h"
#include "framewin.h"
#include "window.h"
#include "actions.h"
#include "xinerama.h"
#include "stacking.h"

#define PIECES ((64/ICON_KABOOM_PIECE_SIZE)*(64/ICON_KABOOM_PIECE_SIZE))
#define KAB_PRECISION		4
#define BOUNCE_HZ		25
#define BOUNCE_DELAY		(1000/BOUNCE_HZ)
#define BOUNCE_HEIGHT		24
#define BOUNCE_LENGTH		0.3
#define BOUNCE_DAMP		0.6
#define URGENT_BOUNCE_DELAY	3000


void DoKaboom(WScreen * scr, xcb_window_t win, int x, int y)
{
#ifdef NORMAL_ICON_KABOOM
	RContext *rc = scr->rcontext;
	int i, j, k;
	int sw = scr->scr_width, sh = scr->scr_height;
	int isize = w_global.config->icon_size;
	int px[PIECES];
	short py[PIECES];
	char pvx[PIECES], pvy[PIECES];
	/* in MkLinux/PPC gcc seems to think that char is unsigned? */
	signed char ax[PIECES], ay[PIECES];
	cairo_surface_t *snap, *ws, *root;
	cairo_t *cr;

	/* the icon core window is at rc->xvisual, and painting the root
	 * through the same visual works whenever its depth matches the
	 * root depth (the normal case). A depth mismatch would BadMatch
	 * on the first Composite, so skip the decoration then just as
	 * the previous XGetImage fallback effectively did. */
	if (rc->xdepth != scr->depth) {
		xcb_unmap_window(wdisp->conn, win);
		return;
	}

	/* pull a client-side copy while the window is still mapped so
	 * the pieces stay valid after the unmap below */
	ws = cairo_xcb_surface_create(rc->xc, (xcb_drawable_t)win,
				      rc->xvisual, isize, isize);
	snap = cairo_image_surface_create(CAIRO_FORMAT_RGB24, isize, isize);
	cr = cairo_create(snap);
	cairo_set_source_surface(cr, ws, 0, 0);
	cairo_paint(cr);
	cairo_destroy(cr);
	cairo_surface_destroy(ws);
	if (cairo_surface_status(snap) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(snap);
		xcb_unmap_window(wdisp->conn, win);
		return;
	}

	root = cairo_xcb_surface_create(rc->xc, (xcb_drawable_t)scr->root_win,
					rc->xvisual, sw, sh);
	cr = cairo_create(root);

	for (i = 0, k = 0; i < isize / ICON_KABOOM_PIECE_SIZE && k < PIECES; i++) {
		for (j = 0; j < isize / ICON_KABOOM_PIECE_SIZE && k < PIECES; j++) {
			if (rand() % 2) {
				ax[k] = i;
				ay[k] = j;
				/* a multiply, not a shift: the icon's x can
				 * be negative after a RandR change and
				 * shifting a negative value is undefined */
				px[k] = (x + i * ICON_KABOOM_PIECE_SIZE)
				    * (1 << KAB_PRECISION);
				py[k] = y + j * ICON_KABOOM_PIECE_SIZE;
				pvx[k] = rand() % (1 << (KAB_PRECISION + 3)) - (1 << (KAB_PRECISION + 3)) / 2;
				pvy[k] = -15 - rand() % 7;
				k++;
			} else {
				ax[k] = -1;
			}
		}
	}

	xcb_unmap_window(wdisp->conn, win);

	j = k;
	while (k > 0) {
		xcb_generic_event_t *foo = NULL;

		if (WXEvqTakeTyped(wdisp->conn, XCB_BUTTON_PRESS, XCB_NONE, &foo)) {
			WXEvqPutBack(foo);
			xcb_clear_area(wdisp->conn, 0, scr->root_win, 0, 0, 0, 0);
			break;
		}

		for (i = 0; i < j; i++) {
			if (ax[i] >= 0) {
				int _px = px[i] >> KAB_PRECISION;
				xcb_clear_area(wdisp->conn, false, scr->root_win, _px, py[i], ICON_KABOOM_PIECE_SIZE, ICON_KABOOM_PIECE_SIZE);
				px[i] += pvx[i];
				py[i] += pvy[i];
				_px = px[i] >> KAB_PRECISION;
				pvy[i]++;
				if (_px < -isize || _px > sw || py[i] >= sh) {
					ax[i] = -1;
					k--;
				} else {
					cairo_set_source_surface(cr, snap,
						_px - ax[i] * ICON_KABOOM_PIECE_SIZE,
						py[i] - ay[i] * ICON_KABOOM_PIECE_SIZE);
					cairo_rectangle(cr, _px, py[i],
						ICON_KABOOM_PIECE_SIZE, ICON_KABOOM_PIECE_SIZE);
					cairo_fill(cr);
				}
			}
		}

		cairo_surface_flush(root);
		wXFlush(wdisp->conn);
		wusleep(MINIATURIZE_ANIMATION_DELAY_Z * 2);
	}

	cairo_destroy(cr);
	cairo_surface_destroy(root);
	cairo_surface_destroy(snap);
#endif	/* NORMAL_ICON_KABOOM */
}

xcb_pixmap_t MakeGhostIcon(WScreen * scr, xcb_drawable_t drawable)
{
	RImage *back;
	RColor color;
	xcb_pixmap_t pixmap;

	if (!drawable)
		return XCB_NONE;

	back = RCreateImageFromDrawable(scr->rcontext, drawable, XCB_NONE);
	if (!back)
		return XCB_NONE;

	color.red = 0xff;
	color.green = 0xff;
	color.blue = 0xff;
	color.alpha = 200;

	RClearImage(back, &color);
	RConvertImage(scr->rcontext, back, &pixmap);

	RReleaseImage(back);

	return pixmap;
}

void DoWindowBirth(WWindow *wwin)
{
#ifdef WINDOW_BIRTH_ZOOM
	int center_x, center_y;
	int width = wwin->frame->core->width;
	int height = wwin->frame->core->height;
	int w = WMIN(width, 20);
	int h = WMIN(height, 20);
	WScreen *scr = wwin->screen_ptr;

	center_x = wwin->frame_x + (width - w) / 2;
	center_y = wwin->frame_y + (height - h) / 2;

	animateResize(scr, center_x, center_y, 1, 1, wwin->frame_x, wwin->frame_y, width, height);
#else
	/* Parameter not used, but tell the compiler that it is ok */
	(void) wwin;
#endif
}

typedef struct AppBouncerData {
	WApplication *wapp;
	int count;
	int pow;
	int dir;
	WMHandlerID timer;
} AppBouncerData;

static void doAppBounce(void *arg)
{
	AppBouncerData *data = (AppBouncerData*)arg;
	WAppIcon *aicon = data->wapp->app_icon;

	if (!aicon) {
		/* the app icon vanished under the bounce, finish the
		 * lifecycle the normal end would have run */
		data->wapp->flags.bouncing = 0;
		WMDeleteTimerHandler(data->timer);
		wApplicationDestroy(data->wapp);
		free(data);
		return;
	}

reinit:
	if (data->wapp->refcount > 1) {
		if (w_global.config->raise_appicons_when_bouncing) {
			/* only update the stacking list, the event loop
			 * flushes it once per iteration */
			wRaiseFrame(aicon->icon->core);
		}

		const double ticks = BOUNCE_HZ * BOUNCE_LENGTH;
		const double s = sqrt(BOUNCE_HEIGHT)/(ticks/2);
		double h = BOUNCE_HEIGHT*pow(BOUNCE_DAMP, data->pow);
		double sqrt_h = sqrt(h);
		if (h > 3) {
			double offset, x = s * data->count - sqrt_h;
			if (x > sqrt_h) {
				++data->pow;
				data->count = 0;
				goto reinit;
			} else ++data->count;
			offset = h - x*x;

			switch (data->dir) {
			case 0: /* left, bounce to right */
				xcb_configure_window(wdisp->conn, aicon->icon->core->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ aicon->x_pos + (int)offset, aicon->y_pos });
				break;
			case 1: /* right, bounce to left */
				xcb_configure_window(wdisp->conn, aicon->icon->core->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ aicon->x_pos - (int)offset, aicon->y_pos });
				break;
			case 2: /* top, bounce down */
				xcb_configure_window(wdisp->conn, aicon->icon->core->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ aicon->x_pos, aicon->y_pos + (int)offset });
				break;
			case 3: /* bottom, bounce up */
				xcb_configure_window(wdisp->conn, aicon->icon->core->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ aicon->x_pos, aicon->y_pos - (int)offset });
				break;
			}
			return;
		}
	}

	xcb_configure_window(wdisp->conn, aicon->icon->core->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ aicon->x_pos, aicon->y_pos });
	aicon->icon->core->screen_ptr->flags.need_restack = 1;
	data->wapp->flags.bouncing = 0;
	WMDeleteTimerHandler(data->timer);
	wApplicationDestroy(data->wapp);
	free(data);
}

static int bounceDirection(WAppIcon *aicon)
{
	enum { left_e = 1, right_e = 2, top_e = 4, bottom_e = 8 };

	WScreen *scr = aicon->icon->core->screen_ptr;
	WMRect rr, sr;
	int l, r, t, b, h, v;
	int dir = 0;

	rr.pos.x = aicon->x_pos;
	rr.pos.y = aicon->y_pos;
	rr.size.width = rr.size.height = 64;

	sr = wGetRectForHead(scr, wGetHeadForRect(scr, rr));

	l = rr.pos.x - sr.pos.x;
	r = sr.pos.x + sr.size.width - rr.pos.x - rr.size.width;
	t = rr.pos.y - sr.pos.y;
	b = sr.pos.y + sr.size.height - rr.pos.y - rr.size.height;

	if (l < r) {
		dir |= left_e;
		h = l;
	} else {
		dir |= right_e;
		h = r;
	}

	if (t < b) {
		dir |= top_e;
		v = t;
	} else {
		dir |= bottom_e;
		v = b;
	}

	if (aicon->dock && abs(aicon->xindex) != abs(aicon->yindex)) {
		if (abs(aicon->xindex) < abs(aicon->yindex)) dir &= ~(top_e | bottom_e);
		else dir &= ~(left_e | right_e);
	} else {
		if (h < v) dir &= ~(top_e | bottom_e);
		else dir &= ~(left_e | right_e);
	}

	switch (dir) {
	case left_e:
		dir = 0;
		break;

	case right_e:
		dir = 1;
		break;

	case top_e:
		dir = 2;
		break;

	case bottom_e:
		dir = 3;
		break;

	default:
		wwarning(_("Impossible direction: %d"), dir);
		dir = 3;
		break;
	}

	return dir;
}

void wAppBounce(WApplication *wapp)
{
	if (!w_global.config->no_animations && !w_global.display_off
		&& wapp->app_icon && !wapp->flags.bouncing
		&& !w_global.config->do_not_make_appicons_bounce) {
		AppBouncerData *data = (AppBouncerData *)malloc(sizeof(AppBouncerData));
		if (!data)
			return;

		++wapp->refcount;
		wapp->flags.bouncing = 1;

		data->wapp = wapp;
		data->count = data->pow = 0;
		data->dir = bounceDirection(wapp->app_icon);
		data->timer = WMAddPersistentTimerHandler(BOUNCE_DELAY, doAppBounce, data);
	}
}

static int appIsUrgent(WApplication *wapp)
{
	if (!wapp->main_window_desc) {
		wwarning("group leader not found for window group");
		return 0;
	}

	return wApplicationUrgentWindow(wapp) != NULL;
}

static void doAppUrgentBounce(void *arg)
{
	WApplication *wapp = (WApplication *)arg;

	/* do not keep waking every URGENT_BOUNCE_DELAY into a dark
	 * panel, wDisplaySetOff(False) re-arms via
	 * wAppBounceWhileUrgent() when the output comes back */
	if (w_global.display_off) {
		WMDeleteTimerHandlerPtr(&wapp->urgent_bounce_timer);
		return;
	}

	if (appIsUrgent(wapp)) {
		if(w_global.config->bounce_appicons_when_urgent) wAppBounce(wapp);
	} else {
		WMDeleteTimerHandlerPtr(&wapp->urgent_bounce_timer);
	}
}

void wAppBounceWhileUrgent(WApplication *wapp)
{
	if (!wapp) return;
	if (appIsUrgent(wapp)) {
		if (!wapp->urgent_bounce_timer) {
			wapp->urgent_bounce_timer = WMAddPersistentTimerHandler(URGENT_BOUNCE_DELAY, doAppUrgentBounce, wapp);
			doAppUrgentBounce(wapp);
		}
	} else {
		WMDeleteTimerHandlerPtr(&wapp->urgent_bounce_timer);
	}
}
