/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
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
#include <string.h>

#include <wraster.h>
#include <cairo.h>
#include <cairo-xcb.h>
#include <WINGs/WINGsP.h>

#include "WindowMaker.h"
#include "GNUstep.h"
#include "texture.h"
#include "resources.h"
#include "screen.h"
#include "wcore.h"
#include "window.h"
#include "framewin.h"
#include "stacking.h"
#include "misc.h"
#include "event.h"


/* ----- shared frame texture cache ------------------------------------ *
 *
 * Titlebar and resizebar gradient Pixmaps were previously rendered and
 * owned per WFrameWindow, so N windows of the same width held N copies
 * of the same pixels (xrestop measured ~122 kB of server memory per
 * 600 px window with a gradient theme). Cache the rendered set keyed on
 * everything renderTexture()/renderResizebarTexture() reads, and let
 * frames share a refcounted entry instead.
 *
 * The Pixmap fields in WFrameWindow stay and hold copies of the cached
 * XIDs so existing read sites (updateTexture, the segment overlay
 * source-surface) are unchanged, only the create/free sites move to
 * acquire/release.
 */

struct WFrameTexCache {
	/* key */
	WTexture *texture;
	int width, height;
	int extra;                /* title: (nleft<<4)|nright, resizebar: cwidth */
	/* value */
	xcb_pixmap_t title;
	xcb_pixmap_t buttons[WTB_ROLE_COUNT];   /* positional: [0..nleft-1] then [nleft..nleft+nright-1] */
	int nbuttons;
	int refcount;
	struct WFrameTexCache *next;
};

/*
 * Per-role static metadata. The dontfit_k_* multipliers reproduce the
 * thresholds the per-slot code used (Miniaturize/Language dropped at
 * width < 4*top_width under TS_NEW or 3*top_width otherwise, Close at
 * 2*top_width). is_drawn roles paint directly on the titlebar surface
 * instead of owning a button core and so contribute neither to nleft/
 * nright nor to the texture-slice cache.
 */
static const struct {
	int flag_shift;
	short dontfit_k_new;
	short dontfit_k_old;
	bool from_xpm;
	bool is_drawn;
} wtb_role_info[WTB_ROLE_COUNT] = {
	[WTB_MINIATURIZE] = { 1, 4, 3, true,  false },
	[WTB_CLOSE]       = { 2, 2, 2, true,  false },
	[WTB_MAXIMIZE]    = { 8, 4, 3, true,  false },
	[WTB_ICON]        = { 9, 0, 0, false, true  },
#ifdef XKB_BUTTON_HINT
	[WTB_LANGUAGE]    = { 6, 4, 3, false, false },
#endif
};

const int wtb_role_flag_shift[WTB_ROLE_COUNT] = {
	[WTB_MINIATURIZE] = 1,
	[WTB_CLOSE]       = 2,
	[WTB_MAXIMIZE]    = 8,
	[WTB_ICON]        = 9,
#ifdef XKB_BUTTON_HINT
	[WTB_LANGUAGE]    = 6,
#endif
};

static inline const WTitlebarLayout *frameLayout(const WFrameWindow *fwin)
{
	return &fwin->screen_ptr->titlebar_layout;
}

static inline int hasButton(const WFrameWindow *fwin, int role)
{
	return (fwin->flags.has_button >> role) & 1;
}

static inline int hideButton(const WFrameWindow *fwin, int role)
{
	return (fwin->flags.hide_button >> role) & 1;
}

/* Appearance reads of the frame state go through this so an urgent-blink
 * phase maps to the focused look without touching flags.state, which the
 * border and focus code keeps reading raw. */
static int frameEffectiveState(const WFrameWindow *fwin)
{
	return (fwin->flags.urgent_blink && !fwin->flags.single_texture)
	     ? WS_FOCUSED : fwin->flags.state;
}

static inline int dontFit(const WFrameWindow *fwin, int role)
{
	return (fwin->flags.button_dont_fit >> role) & 1;
}

#define setRoleBit(bits, role, val) do {		\
		if (val)				\
			(bits) |= (1u << (role));	\
		else					\
			(bits) &= ~(1u << (role));	\
	} while (0)

static int buttonVisible(const WFrameWindow *fwin, int role)
{
	if (wtb_role_info[role].is_drawn)
		return hasButton(fwin, role) && !hideButton(fwin, role) && !dontFit(fwin, role);
	return fwin->buttons[role] && !hideButton(fwin, role) && !dontFit(fwin, role);
}

/* Icon dont-fit is computed from the surface left after the real
 * button cores have settled, the frame-width multiplier used for
 * the other roles is wrong here because in TS_NEW the titlebar core
 * is shrunk by (nleft+nright)*bsize and a 4-button layout can leave
 * the surface ~0 px wide while width >= 4*top_width still says
 * "fits". Called from reconfigure() on horizontal resize and from
 * wFrameWindowSetIcon() so a freshly installed titlebar_icon_size
 * does not draw into a surface narrower than icon_size+top_width.
 * Returns 1 when the dont-fit bit flipped. */
static int updateIconFit(WFrameWindow *fwin, int width)
{
	int r, occupied, surface, bsize, fits;

	if (!hasButton(fwin, WTB_ICON))
		return 0;

	occupied = 0;
	bsize = TS_NEW_GEOM(w_global.config->new_style)
		? fwin->top_width : fwin->top_width - 7;
	if (w_global.config->new_style == TS_NEXT)
		bsize -= 1;
	for (r = 0; r < WTB_ROLE_COUNT; r++) {
		if (wtb_role_info[r].is_drawn || !buttonVisible(fwin, r))
			continue;
		occupied += TS_NEW_GEOM(w_global.config->new_style)
				? bsize : bsize + 3;
	}
	surface = width - occupied;
	fits = (surface >= (int)fwin->titlebar_icon_size + (int)fwin->top_width);
	if (fits != dontFit(fwin, WTB_ICON))
		return 0;
	setRoleBit(fwin->flags.button_dont_fit, WTB_ICON, !fits);
	return 1;
}

/*
 * Walk the layout per side and fill rolepos[role] with the
 * left-to-right slot index among visible non-drawn roles on that
 * side. Returns the per-side counts in *nleft / *nright.
 */
static void countLayoutSlots(const WFrameWindow *fwin, int *nleft, int *nright,
			     short rolepos[WTB_ROLE_COUNT])
{
	const WTitlebarLayout *layout = frameLayout(fwin);
	int side, ord, r, n;

	*nleft = 0;
	*nright = 0;
	for (r = 0; r < WTB_ROLE_COUNT; r++)
		rolepos[r] = -1;

	for (side = WTB_SIDE_LEFT; side <= WTB_SIDE_RIGHT; side++) {
		n = 0;
		for (ord = 0; ord < WTB_ROLE_COUNT; ord++) {
			for (r = 0; r < WTB_ROLE_COUNT; r++) {
				if (layout->side[r] != side || layout->order[r] != ord)
					continue;
				if (wtb_role_info[r].is_drawn || !buttonVisible(fwin, r))
					continue;
				rolepos[r] = n++;
			}
		}
		if (side == WTB_SIDE_LEFT)
			*nleft = n;
		else
			*nright = n;
	}
}

/*
 * Single source of x-math for every visible button core. width is the
 * full frame width, bsize the button edge, theight the titlebar height.
 */
static void layoutButtonGeometry(WFrameWindow *fwin, int width, int bsize, int theight)
{
	const WTitlebarLayout *layout = frameLayout(fwin);
	short rolepos[WTB_ROLE_COUNT];
	int nleft, nright, r, x, y;

	countLayoutSlots(fwin, &nleft, &nright, rolepos);

	for (r = 0; r < WTB_ROLE_COUNT; r++) {
		if (!fwin->buttons[r] || rolepos[r] < 0)
			continue;
		if (TS_NEW_GEOM(w_global.config->new_style)) {
			y = 0;
			if (layout->side[r] == WTB_SIDE_LEFT)
				x = rolepos[r] * bsize;
			else
				x = width - (nright - rolepos[r]) * bsize + 1;
		} else {
			y = (theight - bsize) / 2;
			if (layout->side[r] == WTB_SIDE_LEFT)
				x = 3 + rolepos[r] * (bsize + 3);
			else
				x = width - (nright - rolepos[r]) * (bsize + 3);
		}
		wCoreConfigure(fwin->buttons[r], x, y, bsize, bsize);
	}
}

static struct WFrameTexCache *title_tex_cache;
static struct WFrameTexCache *resize_tex_cache;

static struct WFrameTexCache *texcache_find(struct WFrameTexCache *head, WTexture *tex,
					     int w, int h, int extra)
{
	struct WFrameTexCache *e;

	for (e = head; e; e = e->next)
		if (e->texture == tex && e->width == w && e->height == h && e->extra == extra)
			return e;
	return NULL;
}

static void texcache_release(struct WFrameTexCache **head, struct WFrameTexCache *e)
{
	struct WFrameTexCache **pp;
	int i;

	if (!e || --e->refcount > 0)
		return;

	for (pp = head; *pp; pp = &(*pp)->next) {
		if (*pp == e) {
			*pp = e->next;
			break;
		}
	}
	FREE_PIXMAP(e->title);
	for (i = 0; i < e->nbuttons; i++)
		FREE_PIXMAP(e->buttons[i]);
	wfree(e);
}

static void release_title_set(WFrameWindow *fwin, int state)
{
	int r;

	texcache_release(&title_tex_cache, fwin->title_cache[state]);
	fwin->title_cache[state] = NULL;
	fwin->title_back[state] = XCB_NONE;
	for (r = 0; r < WTB_ROLE_COUNT; r++)
		fwin->button_back[r][state] = XCB_NONE;
}

static void release_resizebar_set(WFrameWindow *fwin)
{
	texcache_release(&resize_tex_cache, fwin->resizebar_cache);
	fwin->resizebar_cache = NULL;
	fwin->resizebar_back[0] = XCB_NONE;
}

/*
 * Three InputOnly children of the resizebar, one per drag region, each
 * carrying only a cursor attribute so the X server switches the pointer
 * shape as it crosses regions with no event traffic. They select no
 * events so ButtonPress propagates to the resizebar parent and reaches
 * resizebarMouseDown() unchanged.
 */
static void updateResizebarCursors(WFrameWindow *fwin)
{
	static const int rcursor[3] = {
		WCUR_BOTTOMLEFTRESIZE, WCUR_VERTICALRESIZE, WCUR_BOTTOMRIGHTRESIZE
	};
	int cw, w, h, geom[3][2], i;

	if (!fwin->resizebar) {
		/* children went with the parent on XDestroyWindow */
		for (i = 0; i < 3; i++)
			fwin->resizebar_cursor[i] = XCB_NONE;
		return;
	}

	cw = fwin->resizebar_corner_width;
	w = fwin->resizebar->width;
	h = fwin->resizebar->height;
	geom[0][0] = 0;       geom[0][1] = cw;
	geom[1][0] = cw;      geom[1][1] = w - 2 * cw;
	geom[2][0] = w - cw;  geom[2][1] = cw;

	for (i = 0; i < 3; i++) {
		if (fwin->resizebar_cursor[i] == XCB_NONE) {
			fwin->resizebar_cursor[i] = xcb_generate_id(wdisp->conn);
			xcb_create_window(wdisp->conn, XCB_COPY_FROM_PARENT, fwin->resizebar_cursor[i],
					  fwin->resizebar->window, 0, 0, 1, h, 0,
					  XCB_WINDOW_CLASS_INPUT_ONLY, XCB_COPY_FROM_PARENT,
					  XCB_CW_CURSOR,
					  (const uint32_t[]){ wdisp->cursor[rcursor[i]] });
		}
		if (geom[i][1] > 0) {
			xcb_configure_window(wdisp->conn, fwin->resizebar_cursor[i], XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, (const uint32_t[]){ geom[i][0], 0, geom[i][1], h });
			xcb_map_window(wdisp->conn, fwin->resizebar_cursor[i]);
		} else {
			xcb_unmap_window(wdisp->conn, fwin->resizebar_cursor[i]);
		}
	}
}

static void handleExpose(WObjDescriptor * desc, xcb_generic_event_t * event);
static void handleButtonExpose(WObjDescriptor * desc, xcb_generic_event_t * event);

static void buttonMouseDown(WObjDescriptor * desc, xcb_generic_event_t * event);
static void titlebarMouseDown(WObjDescriptor * desc, xcb_generic_event_t * event);
static void resizebarMouseDown(WObjDescriptor * desc, xcb_generic_event_t * event);

static void checkTitleSize(WFrameWindow * fwin);
static void updateTexture(WFrameWindow *fwin);

static void paintInnerBorderStrips(WFrameWindow *fwin)
{
	WScreen *scr = fwin->screen_ptr;
	int width = fwin->core->width;
	int height = fwin->core->height;
	int il = 0, ir = 0, it = 0, ib = 0;
	int middle = height - fwin->top_width - fwin->bottom_width;
	int inner_h;
	unsigned long pixel;
	xcb_rectangle_t rectangles[4];
	int nrects = 0;

	if (fwin->flags.is_client_window_frame && fwin->child)
		wFrameInnerBorderWidths((WWindow *)fwin->child, &il, &ir, &it, &ib);
	else
		il = ir = it = ib = scr->frame_inner_border_width;

	inner_h = il + ir;
	if (!fwin->inner_border_gc || middle <= 0 || inner_h > width)
		return;
	if (il <= 0 && ir <= 0 && it <= 0 && ib <= 0)
		return;

	if (frameEffectiveState(fwin) == WS_FOCUSED && fwin->inner_focused_border_pixel)
		pixel = *fwin->inner_focused_border_pixel;
	else if (fwin->inner_border_pixel)
		pixel = *fwin->inner_border_pixel;
	else
		return;

	xcb_change_window_attributes(wdisp->conn, fwin->core->window, XCB_CW_BACK_PIXEL, (const uint32_t[]){ pixel });
	xcb_change_gc(wdisp->conn, fwin->inner_border_gc, XCB_GC_FOREGROUND, (const uint32_t[]){ pixel });

	if (il > 0) {
		rectangles[nrects].x = 0;
		rectangles[nrects].y = fwin->top_width;
		rectangles[nrects].width = il;
		rectangles[nrects].height = middle;
		nrects++;
	}
	if (ir > 0) {
		rectangles[nrects].x = width - ir;
		rectangles[nrects].y = fwin->top_width;
		rectangles[nrects].width = ir;
		rectangles[nrects].height = middle;
		nrects++;
	}
	if (it > 0) {
		rectangles[nrects].x = il;
		rectangles[nrects].y = fwin->top_width;
		rectangles[nrects].width = width - inner_h;
		rectangles[nrects].height = it;
		nrects++;
	}
	if (ib > 0) {
		rectangles[nrects].x = il;
		rectangles[nrects].y = height - fwin->bottom_width - ib;
		rectangles[nrects].width = width - inner_h;
		rectangles[nrects].height = ib;
		nrects++;
	}

	if (nrects > 0)
		xcb_poly_fill_rectangle(wdisp->conn, fwin->core->window, fwin->inner_border_gc,
					nrects, rectangles);
}

static int paintTabSegments(WFrameWindow *fwin, int state, int lofs, int rofs, int y, int h);

static void paintButton(WCoreWindow *button, WTexture *texture,
			WMColor *color, WPixmap *image, int pushed, int from_xpm);

static void updateTitlebar(WFrameWindow * fwin);

/* Paint a screen-depth server pixmap at (x,y) shaped by an optional
 * depth-1 server mask, both wrapped in transient xcb surfaces so
 * nothing outlives the paint. Used for the titlebar app-icon blit
 * where the source is already an RConvertImageMask() pair and there is
 * no RImage to reach through RImageCairoSurface(). */
static void maskedBlit(cairo_t *cr, RContext *rc, xcb_pixmap_t image, xcb_pixmap_t mask,
		       int iw, int ih, int x, int y)
{
	cairo_surface_t *isurf, *msurf;

	isurf = cairo_xcb_surface_create(rc->xc, (xcb_drawable_t)image,
					 rc->xvisual, iw, ih);
	cairo_set_source_surface(cr, isurf, x, y);
	if (mask != XCB_NONE) {
		msurf = cairo_xcb_surface_create_for_bitmap(rc->xc, rc->xscreen,
							    (xcb_pixmap_t)mask, iw, ih);
		cairo_mask_surface(cr, msurf, x, y);
		cairo_surface_destroy(msurf);
	} else {
		cairo_rectangle(cr, x, y, iw, ih);
		cairo_fill(cr);
	}
	cairo_surface_destroy(isurf);
}

static void allocFrameBorderPixel(WFrameWindow *fwin, WMColor *color, unsigned long **pixel)
{
	WXColor xcol;
	char *desc;

	/* this is called on every wFrameWindowUpdateBorders, drop the
	 * previous allocation before recomputing */
	wfree(*pixel);
	*pixel = NULL;

	desc = WMGetColorRGBDescription(color);
	if (!desc)
		return;

	if (wGetColorForColormap(fwin->screen_ptr, fwin->colormap, desc, &xcol)) {
		*pixel = wmalloc(sizeof(unsigned long));
		**pixel = xcol.pixel | (0xffUL << 24);
	}
	wfree(desc);
}

/* WindowTitle{ExtendSpace,Min/MaxHeight} vs MenuTitle{ExtendSpace,Min/MaxHeight}
 * read live from w_global.config so the address stays valid across the
 * swap-and-destroy reload in wDefaultsCheckDomains, the previous int *
 * fields pointed into the freed old WConfig after a reload */
static inline int frame_title_clearance(const WFrameWindow *fwin)
{
	return fwin->flags.is_menu ? w_global.config->menu_title_clearance
				   : w_global.config->window_title_clearance;
}

static inline int frame_title_min_height(const WFrameWindow *fwin)
{
	return fwin->flags.is_menu ? w_global.config->menu_title_min_height
				   : w_global.config->window_title_min_height;
}

static inline int frame_title_max_height(const WFrameWindow *fwin)
{
	return fwin->flags.is_menu ? w_global.config->menu_title_max_height
				   : w_global.config->window_title_max_height;
}

WFrameWindow *wFrameWindowCreate(WScreen * scr, int wlevel, int x, int y,
				 int width, int height, int flags,
				 WTexture ** title_texture, WTexture ** resize_texture,
				 WMColor ** color, WMFont ** font,
				 int depth, xcb_visualtype_t *visual, xcb_colormap_t colormap)
{
	WFrameWindow *fwin;

	fwin = wmalloc(sizeof(WFrameWindow));

	fwin->screen_ptr = scr;

	fwin->flags.single_texture = (flags & WFF_SINGLE_STATE) ? 1 : 0;
	fwin->flags.is_menu = (flags & WFF_IS_MENU) ? 1 : 0;
	fwin->flags.is_client_window_frame = (flags & WFF_IS_CLIENT_WINDOW_FRAME) ? 1 : 0;

	fwin->title_texture = title_texture;
	fwin->resizebar_texture = resize_texture;
	fwin->title_color = color;
	fwin->font = font;
#ifdef KEEP_XKB_LOCK_STATUS
	fwin->languagemode = 0;
	fwin->last_languagemode = 1;
	wWindowGetLanguageLabel(fwin->languagemode, fwin->language_label);
#endif

	fwin->depth = depth;
	fwin->visual = visual;
	fwin->colormap = colormap;

	fwin->core = wCoreCreateTopLevel(scr, x, y, width, height, (flags & WFF_BORDER)
					 ? scr->frame_border_width : 0, fwin->depth, fwin->visual, fwin->colormap, scr->frame_border_pixel);
	fwin->inner_border_gc = xcb_generate_id(wdisp->conn);
	xcb_create_gc(wdisp->conn, fwin->inner_border_gc, fwin->core->window, 0, NULL);

	/* setup stacking information */
	fwin->core->stacking = wmalloc(sizeof(WStacking));
	fwin->core->stacking->above = NULL;
	fwin->core->stacking->under = NULL;
	fwin->core->stacking->child_of = NULL;
	fwin->core->stacking->window_level = wlevel;

	AddToStackList(fwin->core);

	wFrameWindowUpdateBorders(fwin, flags);

	return fwin;
}

void wFrameWindowUpdateBorders(WFrameWindow * fwin, int flags)
{
	int theight;
	int bsize;
	int width, height;
	int i;
	int it = 0, ib = 0, il = 0, ir = 0;
	int iv, ih;
	WScreen *scr = fwin->screen_ptr;

	width = fwin->core->width;
	if (flags & WFF_IS_SHADED)
		height = -1;
	else
		height = fwin->core->height - fwin->top_width - fwin->bottom_width;

	if (fwin->flags.is_client_window_frame && (flags & WFF_BORDER) && fwin->child)
		wFrameInnerBorderWidths((WWindow *)fwin->child, &il, &ir, &it, &ib);
	iv = it + ib;
	ih = il + ir;
	fwin->flags.border = (flags & WFF_BORDER) != 0;
	if (height > 0)
		height -= iv;

	if (flags & WFF_TITLEBAR) {
		theight = WSCALE(scr, WMFontHeight(*fwin->font) + (frame_title_clearance(fwin) + TITLEBAR_EXTEND_SPACE) * 2);

		if (theight > frame_title_max_height(fwin))
			theight = frame_title_max_height(fwin);

		if (theight < frame_title_min_height(fwin))
			theight = frame_title_min_height(fwin);
	} else {
		theight = 0;
		fwin->flags.titlebar = 0;
	}

	if (TS_NEW_GEOM(w_global.config->new_style)) {
		bsize = theight;
	} else if (w_global.config->new_style == TS_OLD) {
		bsize = theight - 7;
	} else {
		bsize = theight - 8;
	}

	if (fwin->titlebar) {
		/* if we had a titlebar and is requesting for one,
		 * check if the size has changed and resize it */
		if (flags & WFF_TITLEBAR) {
			fwin->top_width = theight;

			fwin->flags.need_texture_remake = 1;

			layoutButtonGeometry(fwin, width, bsize, theight);
			updateTitlebar(fwin);
		} else {
			/* we had a titlebar, but now we don't need it anymore */
			for (i = 0; i < (fwin->flags.single_texture ? 1 : WS_STATE_COUNT); i++)
				release_title_set(fwin, i);
			for (i = 0; i < WTB_ROLE_COUNT; i++) {
				if (fwin->buttons[i])
					wCoreDestroy(fwin->buttons[i]);
				fwin->buttons[i] = NULL;
			}

			wCoreDestroy(fwin->titlebar);
			fwin->titlebar = NULL;

			fwin->top_width = 0;
		}
	} else {
		/* if we didn't have a titlebar and are being requested for
		 * one, create it */
		if (flags & WFF_TITLEBAR) {
			const WTitlebarLayout *layout = frameLayout(fwin);
			WCoreWindow *parent;
			int r, k;

			fwin->top_width = theight;

			fwin->flags.titlebar = 1;
			fwin->titlebar = wCoreCreate(fwin->core, 0, 0, width + 1, theight);

			parent = TS_NEW_GEOM(w_global.config->new_style) ? fwin->core : fwin->titlebar;

			for (r = 0; r < WTB_ROLE_COUNT; r++) {
				if (!(flags & WFF_ROLE_BIT(r)) || layout->side[r] == WTB_SIDE_NONE)
					continue;
				setRoleBit(fwin->flags.has_button, r, 1);
				if (wtb_role_info[r].is_drawn) {
					/* drawn roles own no button core, the
					 * slot is hidden until SetIcon()
					 * installs a pixmap and clears it */
					setRoleBit(fwin->flags.hide_button, r, 1);
					continue;
				}
				fwin->buttons[r] = wCoreCreate(parent, 0, 0, bsize, bsize);
				if (w_global.config->new_style == TS_OLD)
					xcb_change_window_attributes(wdisp->conn, fwin->buttons[r]->window, XCB_CW_BACK_PIXEL, (const uint32_t[]){ scr->widget_texture->normal.pixel });
				else if (w_global.config->new_style == TS_NEXT)
					xcb_change_window_attributes(wdisp->conn, fwin->buttons[r]->window, XCB_CW_BACK_PIXEL, (const uint32_t[]){ scr->widget_texture->dark.pixel });

				k = TS_NEW_GEOM(w_global.config->new_style)
					? wtb_role_info[r].dontfit_k_new
					: wtb_role_info[r].dontfit_k_old;
				if (width < theight * k) {
					setRoleBit(fwin->flags.button_dont_fit, r, 1);
				} else {
					/* frame child (button), not a frame, direct configure */
					xcb_configure_window(wdisp->conn, fwin->buttons[r]->window, XCB_CONFIG_WINDOW_STACK_MODE, (const uint32_t[]){ XCB_STACK_MODE_ABOVE });
					xcb_map_window(wdisp->conn, fwin->buttons[r]->window);
				}
			}

			layoutButtonGeometry(fwin, width, bsize, theight);

			if (TS_NEW_GEOM(w_global.config->new_style))
				updateTitlebar(fwin);

			/* frame child (titlebar), not a frame, direct configure */
			xcb_configure_window(wdisp->conn, fwin->titlebar->window, XCB_CONFIG_WINDOW_STACK_MODE, (const uint32_t[]){ XCB_STACK_MODE_ABOVE });
			xcb_map_window(wdisp->conn, fwin->titlebar->window);

			fwin->flags.need_texture_remake = 1;
		}
	}
	checkTitleSize(fwin);

	if (flags & WFF_RESIZEBAR) {
		fwin->bottom_width = scr->resizebar_height;

		if (!fwin->resizebar) {
			fwin->flags.resizebar = 1;
			fwin->resizebar = wCoreCreate(fwin->core, 0,
						  height + fwin->top_width + iv,
						  width, scr->resizebar_height);
			fwin->resizebar_corner_width = scr->resizebar_corner_width;
			if (width < scr->resizebar_corner_width * 2 + RESIZEBAR_MIN_WIDTH) {
				fwin->resizebar_corner_width = (width - RESIZEBAR_MIN_WIDTH) / 2;
				if (fwin->resizebar_corner_width < 0)
					fwin->resizebar_corner_width = 0;
			}

			xcb_map_window(wdisp->conn, fwin->resizebar->window);
			/* frame child (resizebar), not a frame, direct configure */
			xcb_configure_window(wdisp->conn, fwin->resizebar->window, XCB_CONFIG_WINDOW_STACK_MODE, (const uint32_t[]){ XCB_STACK_MODE_BELOW });
			updateResizebarCursors(fwin);

			fwin->flags.need_texture_remake = 1;
		} else {
			if (height + fwin->top_width + fwin->bottom_width + iv != fwin->core->height)
				wCoreConfigure(fwin->resizebar, 0, height + fwin->top_width + iv,
						   width, scr->resizebar_height);
		}
	} else {
		fwin->bottom_width = 0;

		if (fwin->resizebar) {
			fwin->bottom_width = 0;
			release_resizebar_set(fwin);
			wCoreDestroy(fwin->resizebar);
			fwin->resizebar = NULL;
			updateResizebarCursors(fwin);
		}
	}

	if (height + fwin->top_width + fwin->bottom_width + iv != fwin->core->height && !(flags & WFF_IS_SHADED))
		wFrameWindowResize(fwin, width, height + fwin->top_width + fwin->bottom_width + iv);

	if (flags & WFF_BORDER)
		xcb_configure_window(wdisp->conn, fwin->core->window, XCB_CONFIG_WINDOW_BORDER_WIDTH, (const uint32_t[]){ (fwin->flags.is_client_window_frame && fwin->child)
				      ? wFrameBorderWidth((WWindow *)fwin->child) : scr->frame_border_width });
	else
		xcb_configure_window(wdisp->conn, fwin->core->window, XCB_CONFIG_WINDOW_BORDER_WIDTH, (const uint32_t[]){ 0 });

	/* setup object descriptors */
	if (fwin->titlebar) {
		fwin->titlebar->descriptor.handle_expose = handleExpose;
		fwin->titlebar->descriptor.parent = fwin;
		fwin->titlebar->descriptor.parent_type = WCLASS_FRAME;
		fwin->titlebar->descriptor.handle_mousedown = titlebarMouseDown;
	}

	if (fwin->resizebar) {
		fwin->resizebar->descriptor.handle_expose = handleExpose;
		fwin->resizebar->descriptor.parent = fwin;
		fwin->resizebar->descriptor.parent_type = WCLASS_FRAME;
		fwin->resizebar->descriptor.handle_mousedown = resizebarMouseDown;
	}

	for (i = 0; i < WTB_ROLE_COUNT; i++) {
		if (!fwin->buttons[i])
			continue;
		fwin->buttons[i]->descriptor.handle_expose = handleButtonExpose;
		fwin->buttons[i]->descriptor.parent = fwin;
		fwin->buttons[i]->descriptor.parent_type = WCLASS_FRAME;
		fwin->buttons[i]->descriptor.handle_mousedown = buttonMouseDown;
	}

	checkTitleSize(fwin);

	if (fwin->flags.is_client_window_frame && fwin->child) {
		WWindow *wwin = (WWindow *)fwin->child;

		allocFrameBorderPixel(fwin, wFrameBorderColor(wwin), &fwin->border_pixel);
		allocFrameBorderPixel(fwin, wFrameFocusedBorderColor(wwin), &fwin->focused_border_pixel);
		allocFrameBorderPixel(fwin, wFrameSelectedBorderColor(wwin), &fwin->selected_border_pixel);
		allocFrameBorderPixel(fwin, wFrameInnerBorderColor(wwin), &fwin->inner_border_pixel);
		allocFrameBorderPixel(fwin, wFrameInnerFocusedBorderColor(wwin), &fwin->inner_focused_border_pixel);
	} else {
		allocFrameBorderPixel(fwin, scr->frame_border_color, &fwin->border_pixel);
		allocFrameBorderPixel(fwin, scr->frame_focused_border_color, &fwin->focused_border_pixel);
		allocFrameBorderPixel(fwin, scr->frame_selected_border_color, &fwin->selected_border_pixel);
		allocFrameBorderPixel(fwin, scr->frame_inner_border_color, &fwin->inner_border_pixel);
		allocFrameBorderPixel(fwin, scr->frame_inner_focused_border_color, &fwin->inner_focused_border_pixel);
	}

	if (flags & WFF_SELECTED) {
		if (fwin->selected_border_pixel)
			xcb_change_window_attributes(wdisp->conn, fwin->core->window, XCB_CW_BORDER_PIXEL, (const uint32_t[]){ *fwin->selected_border_pixel });
	}
	else {
		if (fwin->flags.state == WS_FOCUSED) {
			if (fwin->focused_border_pixel)
				xcb_change_window_attributes(wdisp->conn, fwin->core->window, XCB_CW_BORDER_PIXEL, (const uint32_t[]){ *fwin->focused_border_pixel });
		} else {
			if (fwin->border_pixel)
				xcb_change_window_attributes(wdisp->conn, fwin->core->window, XCB_CW_BORDER_PIXEL, (const uint32_t[]){ *fwin->border_pixel });
		}

		if (fwin->flags.is_client_window_frame && (iv + ih) > 0)
			paintInnerBorderStrips(fwin);
	}
}

void wFrameWindowDestroy(WFrameWindow * fwin)
{
	int i;

	if (fwin->inner_border_gc) {
		xcb_free_gc(wdisp->conn, fwin->inner_border_gc);
		fwin->inner_border_gc = 0;
	}

	for (i = 0; i < WTB_ROLE_COUNT; i++)
		if (fwin->buttons[i])
			wCoreDestroy(fwin->buttons[i]);

	if (fwin->titlebar_icon != XCB_NONE)
		xcb_free_pixmap(wdisp->conn, fwin->titlebar_icon);
	if (fwin->titlebar_icon_mask != XCB_NONE)
		xcb_free_pixmap(wdisp->conn, fwin->titlebar_icon_mask);

#ifdef XKB_BUTTON_HINT
	for (i = 0; i < 2; i++) {
		if (fwin->languagebutton_image[i]) {
			if (!fwin->languagebutton_image[i]->shared)
				wPixmapDestroy(fwin->languagebutton_image[i]);
			fwin->languagebutton_image[i] = NULL;
		}
	}
#endif

	if (fwin->resizebar)
		wCoreDestroy(fwin->resizebar);

	if (fwin->titlebar)
		wCoreDestroy(fwin->titlebar);

	RemoveFromStackList(fwin->core);

	wCoreDestroy(fwin->core);

	if (fwin->title)
		wfree(fwin->title);

	wfree(fwin->tab_seg_x);

	for (i = 0; i < (fwin->flags.single_texture ? 1 : WS_STATE_COUNT); i++)
		release_title_set(fwin, i);
	release_resizebar_set(fwin);

	wfree(fwin->border_pixel);
	wfree(fwin->focused_border_pixel);
	wfree(fwin->selected_border_pixel);
	wfree(fwin->inner_border_pixel);
	wfree(fwin->inner_focused_border_pixel);

	wfree(fwin);
}

void wFrameWindowChangeState(WFrameWindow * fwin, int state)
{
	if (fwin->flags.state == state)
		return;

	fwin->flags.state = state;
	fwin->flags.need_texture_change = 1;

	if (fwin->flags.state == WS_FOCUSED) {
		if (fwin->focused_border_pixel)
			xcb_change_window_attributes(wdisp->conn, fwin->core->window, XCB_CW_BORDER_PIXEL, (const uint32_t[]){ *fwin->focused_border_pixel });
	} else {
		if (fwin->border_pixel)
			xcb_change_window_attributes(wdisp->conn, fwin->core->window, XCB_CW_BORDER_PIXEL, (const uint32_t[]){ *fwin->border_pixel });
	}
	wFrameWindowPaint(fwin);
}

/* Re-point the titlebar and button backgrounds at the current phase's
 * pre-rendered pixmaps and repaint. Skips the outer border swap when the
 * frame's child is selected, nothing re-applies selected_border_pixel
 * periodically so a tick would stomp the selection border for good, and
 * when both border pixels are equal, a per-tick check because a theme
 * reload re-allocates them mid-blink. */
static void wFrameWindowBlinkRefresh(WFrameWindow *fwin)
{
	int state;

	if (!fwin->titlebar)
		return;

	updateTexture(fwin);

	state = frameEffectiveState(fwin);
	if (fwin->flags.is_client_window_frame && fwin->child
	    && !((WWindow *)fwin->child)->flags.selected
	    && fwin->border_pixel && fwin->focused_border_pixel
	    && *fwin->border_pixel != *fwin->focused_border_pixel)
		xcb_change_window_attributes(wdisp->conn, fwin->core->window, XCB_CW_BORDER_PIXEL, (const uint32_t[]){ state == WS_FOCUSED ? *fwin->focused_border_pixel
						      : *fwin->border_pixel });

	wFrameWindowPaint(fwin);
}

void wFrameWindowUrgentBlink(WFrameWindow *fwin, int on)
{
	if (fwin->flags.urgent_blink == (on != 0))
		return;
	fwin->flags.urgent_blink = on ? 1 : 0;
	wFrameWindowBlinkRefresh(fwin);
}

void wFrameWindowBlinkTick(WFrameWindow *fwin)
{
	fwin->flags.urgent_blink = !fwin->flags.urgent_blink;
	wFrameWindowBlinkRefresh(fwin);
}

static void updateTitlebar(WFrameWindow * fwin)
{
	short rolepos[WTB_ROLE_COUNT];
	int x, w, nleft, nright;
	int theight, bsize;

	theight = WSCALE(fwin->screen_ptr, WMFontHeight(*fwin->font) + (frame_title_clearance(fwin) + TITLEBAR_EXTEND_SPACE) * 2);

	if (theight > frame_title_max_height(fwin))
		theight = frame_title_max_height(fwin);

	if (theight < frame_title_min_height(fwin))
		theight = frame_title_min_height(fwin);

	if (TS_NEW_GEOM(w_global.config->new_style))
		bsize = theight;
	else if (w_global.config->new_style == TS_OLD)
		bsize = theight - 7;
	else
		bsize = theight - 8;

	x = 0;
	w = fwin->core->width + 1;

	countLayoutSlots(fwin, &nleft, &nright, rolepos);
	/* hiding/showing one role can shift the others on the same
	 * side, redo the geometry walk every time */
	layoutButtonGeometry(fwin, fwin->core->width, bsize, theight);

	if (TS_NEW_GEOM(w_global.config->new_style)) {
		x = nleft * bsize;
		w -= (nleft + nright) * bsize;
	}

	if (TS_NEW_GEOM(w_global.config->new_style) || fwin->titlebar->width != w)
		fwin->flags.need_texture_remake = 1;

	wCoreConfigure(fwin->titlebar, x, 0, w, theight);
}

/* a bare wFrameWindowPaint() does not clear the titlebar surface, so
 * hiding a drawn role (icon) or shrinking offsets would leave stale
 * pixels behind. checkTitleSize() must run before paint so the
 * incomplete-title flag reflects the new lofs/rofs */
static void redrawTitlebar(WFrameWindow *fwin)
{
	if (!fwin->titlebar)
		return;
	xcb_clear_area(wdisp->conn, 0, fwin->titlebar->window, 0, 0, 0, 0);
	checkTitleSize(fwin);
	wFrameWindowPaint(fwin);
}

void wFrameWindowHideButton(WFrameWindow * fwin, int flags)
{
	int r, drawn_changed = 0;

	for (r = 0; r < WTB_ROLE_COUNT; r++) {
		if (!(flags & WFF_ROLE_BIT(r)) || !hasButton(fwin, r))
			continue;
		if (wtb_role_info[r].is_drawn) {
			if (!hideButton(fwin, r))
				drawn_changed = 1;
			setRoleBit(fwin->flags.hide_button, r, 1);
			continue;
		}
		if (fwin->buttons[r])
			xcb_unmap_window(wdisp->conn, fwin->buttons[r]->window);
		setRoleBit(fwin->flags.hide_button, r, 1);
	}

	/* hiding a non-drawn core grows the available surface, the
	 * Icon dont-fit bit was derived from the old occupied count */
	if (updateIconFit(fwin, fwin->core->width))
		drawn_changed = 1;

	if (fwin->titlebar) {
		if (TS_NEW_GEOM(w_global.config->new_style)) {
			updateTitlebar(fwin);
		} else {
			updateTitlebar(fwin);
			xcb_clear_area(wdisp->conn, 0, fwin->titlebar->window, 0, 0, 0, 0);
			wFrameWindowPaint(fwin);
		}
		checkTitleSize(fwin);
		if (drawn_changed)
			redrawTitlebar(fwin);
	}
}

void wFrameWindowShowButton(WFrameWindow * fwin, int flags)
{
	int r, drawn_changed = 0;

	for (r = 0; r < WTB_ROLE_COUNT; r++) {
		if (!(flags & WFF_ROLE_BIT(r)) || !hasButton(fwin, r) || !hideButton(fwin, r))
			continue;
		if (wtb_role_info[r].is_drawn) {
			drawn_changed = 1;
			setRoleBit(fwin->flags.hide_button, r, 0);
			continue;
		}
		if (fwin->buttons[r] && !dontFit(fwin, r))
			xcb_map_window(wdisp->conn, fwin->buttons[r]->window);
		setRoleBit(fwin->flags.hide_button, r, 0);
	}

	/* showing a non-drawn core shrinks the available surface, the
	 * Icon dont-fit bit was derived from the old occupied count */
	if (updateIconFit(fwin, fwin->core->width))
		drawn_changed = 1;

	if (fwin->titlebar) {
		if (TS_NEW_GEOM(w_global.config->new_style)) {
			updateTitlebar(fwin);
		} else {
			updateTitlebar(fwin);
			xcb_clear_area(wdisp->conn, 0, fwin->titlebar->window, 0, 0, 0, 0);
			wFrameWindowPaint(fwin);
		}
		checkTitleSize(fwin);
		if (drawn_changed)
			redrawTitlebar(fwin);
	}
}

void wFrameWindowSetIcon(WFrameWindow *fwin, RImage *image)
{
	WScreen *scr = fwin->screen_ptr;
	RImage *scaled;
	xcb_pixmap_t pix = XCB_NONE, mask = XCB_NONE;
	int sz;

	if (fwin->titlebar_icon != XCB_NONE)
		xcb_free_pixmap(wdisp->conn, fwin->titlebar_icon);
	if (fwin->titlebar_icon_mask != XCB_NONE)
		xcb_free_pixmap(wdisp->conn, fwin->titlebar_icon_mask);
	fwin->titlebar_icon = XCB_NONE;
	fwin->titlebar_icon_mask = XCB_NONE;
	fwin->titlebar_icon_size = 0;

	if (!fwin->titlebar) {
		/* no surface to redraw, fullscreen/NoTitlebar */
		setRoleBit(fwin->flags.hide_button, WTB_ICON, 1);
		return;
	}

	if (fwin->top_width <= 4 || image == NULL) {
		/* RScaleImage(...,-ve,-ve) guard, reachable with a live
		 * titlebar via WindowTitleMaxHeight <= 4, must clear
		 * stale icon pixels */
		setRoleBit(fwin->flags.hide_button, WTB_ICON, 1);
		redrawTitlebar(fwin);
		return;
	}

	sz = fwin->top_width - 4;
	scaled = RScaleImage(image, sz, sz);
	if (!scaled) {
		setRoleBit(fwin->flags.hide_button, WTB_ICON, 1);
		redrawTitlebar(fwin);
		return;
	}

	if (!RConvertImageMask(scr->rcontext, scaled, &pix, &mask, 128)) {
		/* RConvertImageMask() can fail before writing *pixmap, or
		 * after writing *pixmap but before *mask, both locals
		 * started at None so the conditional frees are safe */
		if (pix != XCB_NONE)
			xcb_free_pixmap(wdisp->conn, pix);
		if (mask != XCB_NONE)
			xcb_free_pixmap(wdisp->conn, mask);
		RReleaseImage(scaled);
		setRoleBit(fwin->flags.hide_button, WTB_ICON, 1);
		redrawTitlebar(fwin);
		return;
	}
	RReleaseImage(scaled);

	fwin->titlebar_icon = pix;
	fwin->titlebar_icon_mask = mask;
	fwin->titlebar_icon_size = sz;
	updateIconFit(fwin, fwin->core->width);
	setRoleBit(fwin->flags.hide_button, WTB_ICON, 0);
	redrawTitlebar(fwin);
}

/* Paint texture as a cairo pattern into a fresh width x height Pixmap
 * with an optional RBEV_RAISED2 bevel on the (bx,0,bw,height) span.
 * Returns None when the texture has no pattern equivalent so the
 * caller falls back to the RImage renderer. */
static xcb_pixmap_t paintTextureSlice(WScreen *scr, cairo_pattern_t *pat,
				int width, int height, int bx, int bw, int bevel)
{
	RContext *rc = scr->rcontext;
	cairo_surface_t *surf;
	cairo_t *cr;
	xcb_pixmap_t pix;

	pix = xcb_generate_id(rc->xc);
	xcb_create_pixmap(rc->xc, rc->xdepth, pix, rc->xscreen->root, width, height);

	surf = cairo_xcb_surface_create(rc->xc, (xcb_drawable_t)pix,
					rc->xvisual, width, height);
	cr = cairo_create(surf);
	cairo_set_source(cr, pat);
	cairo_paint(cr);
	if (bevel)
		wTextureBevel(cr, bx, 0, bw, height);
	cairo_destroy(cr);
	cairo_surface_flush(surf);
	cairo_surface_destroy(surf);

	WMScreenNoteDrawableSize(scr->wmscreen, pix, width, height);
	return pix;
}

static void
renderTexture(WScreen * scr, WTexture * texture, int width, int height,
	      int bwidth, int bheight, int nleft, int nright,
	      xcb_pixmap_t * title, xcb_pixmap_t slices[])
{
	RImage *img, *sub, *mimg;
	cairo_pattern_t *pat;
	cairo_matrix_t m;
	int x, w, i, bevel;

	*title = XCB_NONE;
	for (i = 0; i < nleft + nright; i++)
		slices[i] = XCB_NONE;

	pat = wTexturePattern(texture, width, height);
	if (pat) {
		bevel = (w_global.config->new_style == TS_NEW);
		if (TS_NEW_GEOM(w_global.config->new_style)) {
			/* button slices: shift the pattern origin so each
			 * slice samples the same span of the full-width
			 * gradient it would have been carved from */
			for (i = 0; i < nleft; i++) {
				cairo_matrix_init_translate(&m, i * bwidth, 0);
				cairo_pattern_set_matrix(pat, &m);
				slices[i] = paintTextureSlice(scr, pat,
							      bwidth, bheight,
							      0, bwidth, bevel);
			}
			for (i = 0; i < nright; i++) {
				cairo_matrix_init_translate(&m,
							    width - (i + 1) * bwidth, 0);
				cairo_pattern_set_matrix(pat, &m);
				slices[nleft + i] = paintTextureSlice(scr, pat,
								      bwidth, bheight,
								      0, bwidth, bevel);
			}
			x = nleft * bwidth;
			w = width - (nleft + nright) * bwidth;
			if (w <= 0) {
				w = width;
				x = 0;
			}
			cairo_matrix_init_translate(&m, x, 0);
			cairo_pattern_set_matrix(pat, &m);
			*title = paintTextureSlice(scr, pat, w, height, 0, w, bevel);
		} else {
			*title = paintTextureSlice(scr, pat, width, height,
						   0, width, 1);
		}
		cairo_pattern_destroy(pat);
		return;
	}

	img = wTextureRenderImage(texture, width, height, WREL_FLAT);
	if (!img) {
		wwarning(_("could not render texture: %s"), RMessageForError(RErrorCode));
		return;
	}

	if (TS_NEW_GEOM(w_global.config->new_style)) {
		/* TS_FLAT carves the same per-core slices but skips the
		 * per-slice raised bevel so the gradient runs unbroken
		 * across the button group and the title core, the seam
		 * that TS_NEW shows is exactly the RBevelImage edge */
		bevel = (w_global.config->new_style == TS_NEW);
		x = 0;
		w = img->width;

		for (i = 0; i < nleft; i++) {
			sub = RGetSubImage(img, i * bwidth, 0, bwidth, bheight);
			if (sub) {
				if (bevel)
					RBevelImage(sub, RBEV_RAISED2);
				if (!RConvertImage(scr->rcontext, sub, &slices[i]))
					wwarning(_("error rendering image:%s"), RMessageForError(RErrorCode));
				x += sub->width;
				w -= sub->width;
				RReleaseImage(sub);
			}
		}
		for (i = 0; i < nright; i++) {
			sub = RGetSubImage(img, width - (i + 1) * bwidth, 0, bwidth, bheight);
			if (sub) {
				if (bevel)
					RBevelImage(sub, RBEV_RAISED2);
				if (!RConvertImage(scr->rcontext, sub, &slices[nleft + i]))
					wwarning(_("error rendering image:%s"), RMessageForError(RErrorCode));
				w -= sub->width;
				RReleaseImage(sub);
			}
		}

		if (w != width) {
			mimg = RGetSubImage(img, x, 0, w, img->height);
			if (bevel)
				RBevelImage(mimg, RBEV_RAISED2);

			if (!RConvertImage(scr->rcontext, mimg, title))
				wwarning(_("error rendering image:%s"), RMessageForError(RErrorCode));

			RReleaseImage(mimg);
		} else {
			if (bevel)
				RBevelImage(img, RBEV_RAISED2);

			if (!RConvertImage(scr->rcontext, img, title))
				wwarning(_("error rendering image:%s"), RMessageForError(RErrorCode));
		}
	} else {
		RBevelImage(img, RBEV_RAISED2);

		if (!RConvertImage(scr->rcontext, img, title))
			wwarning(_("error rendering image:%s"), RMessageForError(RErrorCode));
	}

	RReleaseImage(img);
}

static void resizebarGrips(cairo_t *cr, int width, int height, int cwidth)
{
	cairo_set_line_width(cr, 1.0);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);

	cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
	cairo_move_to(cr, 0.5, 0.5);
	cairo_line_to(cr, width - 0.5, 0.5);
	cairo_move_to(cr, cwidth + 0.5, 2.5);
	cairo_line_to(cr, cwidth + 0.5, height - 0.5);
	if (width > 1) {
		cairo_move_to(cr, width - cwidth - 1.5, 2.5);
		cairo_line_to(cr, width - cwidth - 1.5, height - 0.5);
	}
	cairo_stroke(cr);

	cairo_set_source_rgba(cr, 1, 1, 1, 0.45);
	cairo_move_to(cr, 0.5, 1.5);
	cairo_line_to(cr, width - 0.5, 1.5);
	cairo_move_to(cr, cwidth + 1.5, 2.5);
	cairo_line_to(cr, cwidth + 1.5, height - 0.5);
	cairo_move_to(cr, width - cwidth - 0.5, 2.5);
	cairo_line_to(cr, width - cwidth - 0.5, height - 0.5);
	cairo_stroke(cr);
}

static void
renderResizebarTexture(WScreen * scr, WTexture * texture, int width, int height, int cwidth, xcb_pixmap_t * pmap)
{
	RImage *img;
	RColor light;
	RColor dark;
	cairo_pattern_t *pat;
	cairo_surface_t *surf;
	cairo_t *cr;

	*pmap = XCB_NONE;

	pat = wTexturePattern(texture, width, height);
	if (pat) {
		*pmap = paintTextureSlice(scr, pat, width, height, 0, 0, 0);
		cairo_pattern_destroy(pat);
		surf = cairo_xcb_surface_create(scr->rcontext->xc,
						(xcb_drawable_t)*pmap,
						scr->rcontext->xvisual,
						width, height);
		cr = cairo_create(surf);
		resizebarGrips(cr, width, height, cwidth);
		cairo_destroy(cr);
		cairo_surface_flush(surf);
		cairo_surface_destroy(surf);
		return;
	}

	img = wTextureRenderImage(texture, width, height, WREL_FLAT);
	if (!img) {
		wwarning(_("could not render texture: %s"), RMessageForError(RErrorCode));
		return;
	}

	light.alpha = 0;
	light.red = light.green = light.blue = 80;

	dark.alpha = 0;
	dark.red = dark.green = dark.blue = 40;

	ROperateLine(img, RSubtractOperation, 0, 0, width - 1, 0, &dark);
	ROperateLine(img, RAddOperation, 0, 1, width - 1, 1, &light);

	ROperateLine(img, RSubtractOperation, cwidth, 2, cwidth, height - 1, &dark);
	ROperateLine(img, RAddOperation, cwidth + 1, 2, cwidth + 1, height - 1, &light);

	if (width > 1)
		ROperateLine(img, RSubtractOperation, width - cwidth - 2, 2,
			     width - cwidth - 2, height - 1, &dark);
	ROperateLine(img, RAddOperation, width - cwidth - 1, 2, width - cwidth - 1, height - 1, &light);

#ifdef SHADOW_RESIZEBAR
	ROperateLine(img, RAddOperation, 0, 1, 0, height - 1, &light);
	ROperateLine(img, RSubtractOperation, width - 1, 1, width - 1, height - 1, &dark);
	ROperateLine(img, RSubtractOperation, 0, height - 1, width - 1, height - 1, &dark);
#endif				/* SHADOW_RESIZEBAR */

	if (!RConvertImage(scr->rcontext, img, pmap))
		wwarning(_("error rendering image: %s"), RMessageForError(RErrorCode));

	RReleaseImage(img);
}

static void updateTexture(WFrameWindow * fwin)
{
	int i, r;
	unsigned long pixel;

	i = frameEffectiveState(fwin);
	if (fwin->titlebar) {
		if (fwin->title_texture[i]->any.type != WTEX_SOLID) {
			xcb_change_window_attributes(wdisp->conn, fwin->titlebar->window, XCB_CW_BACK_PIXMAP, (const uint32_t[]){ fwin->title_back[i] });
			if (TS_NEW_GEOM(w_global.config->new_style)) {
				for (r = 0; r < WTB_ROLE_COUNT; r++)
					if (fwin->buttons[r] && fwin->button_back[r][i])
						xcb_change_window_attributes(wdisp->conn, fwin->buttons[r]->window, XCB_CW_BACK_PIXMAP, (const uint32_t[]){ fwin->button_back[r][i] });
			}
		} else {
			pixel = fwin->title_texture[i]->solid.normal.pixel;
			xcb_change_window_attributes(wdisp->conn, fwin->titlebar->window, XCB_CW_BACK_PIXEL, (const uint32_t[]){ pixel });
			if (TS_NEW_GEOM(w_global.config->new_style)) {
				for (r = 0; r < WTB_ROLE_COUNT; r++)
					if (fwin->buttons[r])
						xcb_change_window_attributes(wdisp->conn, fwin->buttons[r]->window, XCB_CW_BACK_PIXEL, (const uint32_t[]){ pixel });
			}
		}
		xcb_clear_area(wdisp->conn, 0, fwin->titlebar->window, 0, 0, 0, 0);

		for (r = 0; r < WTB_ROLE_COUNT; r++) {
			if (!fwin->buttons[r])
				continue;
			xcb_clear_area(wdisp->conn, 0, fwin->buttons[r]->window, 0, 0, 0, 0);
			handleButtonExpose(&fwin->buttons[r]->descriptor, NULL);
		}
	}
}

static void remakeTexture(WFrameWindow * fwin, int state)
{
	struct WFrameTexCache *e;

	if (fwin->title_texture[state] && fwin->titlebar) {
		release_title_set(fwin, state);

		if (fwin->title_texture[state]->any.type != WTEX_SOLID) {
			const WTitlebarLayout *layout = frameLayout(fwin);
			WTexture *tex = fwin->title_texture[state];
			short rolepos[WTB_ROLE_COUNT];
			int nleft, nright, r, pos;
			int width, height, extra;

			countLayoutSlots(fwin, &nleft, &nright, rolepos);

			width = fwin->core->width + 1;
			height = fwin->titlebar->height;
			/* button presence only changes the output in TS_NEW,
			 * fold it out of the key for the other styles so they
			 * share more */
			extra = TS_NEW_GEOM(w_global.config->new_style)
				? ((nleft << 4) | nright)
				: 0;

			e = texcache_find(title_tex_cache, tex, width, height, extra);
			if (!e) {
				e = wmalloc(sizeof(*e));
				e->texture = tex;
				e->width = width;
				e->height = height;
				e->extra = extra;
				e->nbuttons = nleft + nright;
				renderTexture(fwin->screen_ptr, tex, width, height,
					      height, height, nleft, nright,
					      &e->title, e->buttons);
				e->next = title_tex_cache;
				title_tex_cache = e;
			}
			e->refcount++;
			fwin->title_cache[state] = e;
			fwin->title_back[state] = e->title;
			if (TS_NEW_GEOM(w_global.config->new_style)) {
				/* map positional cache slices back to roles,
				 * right-side slices were carved from the far
				 * edge inward so the last slot in the array
				 * is the leftmost right-side button */
				for (r = 0; r < WTB_ROLE_COUNT; r++) {
					if (rolepos[r] < 0)
						continue;
					if (layout->side[r] == WTB_SIDE_LEFT)
						pos = rolepos[r];
					else
						pos = nleft + (nright - 1 - rolepos[r]);
					fwin->button_back[r][state] = e->buttons[pos];
				}
			}
		}
	}
	if (fwin->resizebar_texture && fwin->resizebar_texture[0]
	    && fwin->resizebar && state == 0) {

		release_resizebar_set(fwin);

		if (fwin->resizebar_texture[0]->any.type != WTEX_SOLID) {
			WTexture *tex = fwin->resizebar_texture[0];
			int width = fwin->resizebar->width;
			int height = fwin->resizebar->height;
			int cwidth = fwin->resizebar_corner_width;

			e = texcache_find(resize_tex_cache, tex, width, height, cwidth);
			if (!e) {
				e = wmalloc(sizeof(*e));
				e->texture = tex;
				e->width = width;
				e->height = height;
				e->extra = cwidth;
				renderResizebarTexture(fwin->screen_ptr, tex,
						       width, height, cwidth, &e->title);
				e->next = resize_tex_cache;
				resize_tex_cache = e;
			}
			e->refcount++;
			fwin->resizebar_cache = e;
			fwin->resizebar_back[0] = e->title;
		}

		/* this part should be in updateTexture() */
		if (fwin->resizebar_texture[0]->any.type != WTEX_SOLID)
			xcb_change_window_attributes(wdisp->conn, fwin->resizebar->window, XCB_CW_BACK_PIXMAP, (const uint32_t[]){ fwin->resizebar_back[0] });
		else
			xcb_change_window_attributes(wdisp->conn, fwin->resizebar->window, XCB_CW_BACK_PIXEL, (const uint32_t[]){ fwin->resizebar_texture[0]->solid.normal.pixel });

		xcb_clear_area(wdisp->conn, 0, fwin->resizebar->window, 0, 0, 0, 0);
	}
}

void wFrameWindowPaint(WFrameWindow * fwin)
{
	WScreen *scr = fwin->screen_ptr;
	int state;

	state = frameEffectiveState(fwin);

	if (fwin->flags.is_client_window_frame)
		fwin->flags.justification = w_global.config->title_justification;

	if (fwin->flags.need_texture_remake) {
		int i;

		fwin->flags.need_texture_remake = 0;
		fwin->flags.need_texture_change = 0;

		if (fwin->flags.single_texture) {
			remakeTexture(fwin, 0);
			updateTexture(fwin);
		} else {
			/* first render the texture for the current state... */
			remakeTexture(fwin, state);
			/* ... and paint it */
			updateTexture(fwin);

			for (i = 0; i < WS_STATE_COUNT; i++) {
				if (i != state)
					remakeTexture(fwin, i);
			}
		}
#ifdef XKB_BUTTON_HINT
		if (w_global.config->modelock)
			wFrameWindowUpdateLanguageButton(fwin);
#endif
	}

	if (fwin->flags.need_texture_change) {
		fwin->flags.need_texture_change = 0;

		updateTexture(fwin);
	}

	if (fwin->titlebar && !fwin->flags.repaint_only_resizebar
	    && fwin->title_texture[state]->any.type == WTEX_SOLID
	    && w_global.config->new_style != TS_FLAT) {
		cairo_t *cr;

		cr = WCairoBeginForDrawable(scr->wmscreen, fwin->titlebar->window);
		if (cr) {
			wDrawBevel(cr, fwin->titlebar->width, fwin->titlebar->height,
				   (WTexSolid *)fwin->title_texture[state], WREL_RAISED);
			WCairoEnd(cr);
		}
	}

	if (fwin->resizebar && !fwin->flags.repaint_only_titlebar
	    && fwin->resizebar_texture[0]->any.type == WTEX_SOLID) {
		WTexSolid *texture = (WTexSolid *)fwin->resizebar_texture[0];
		cairo_t *cr;
		int w, h, cw;

		w = fwin->resizebar->width;
		h = fwin->resizebar->height;
		cw = fwin->resizebar_corner_width;

		cr = WCairoBeginForDrawable(scr->wmscreen, fwin->resizebar->window);
		if (cr) {
			cairo_set_line_width(cr, 1.0);
			cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);

			W_SetSourceXColor(cr, &texture->dim);
			cairo_move_to(cr, 0.5, 0.5);
			cairo_line_to(cr, w + 0.5, 0.5);
			cairo_move_to(cr, cw + 0.5, 2.5);
			cairo_line_to(cr, cw + 0.5, h + 0.5);
			cairo_move_to(cr, w - cw - 1.5, 2.5);
			cairo_line_to(cr, w - cw - 1.5, h + 0.5);
			cairo_stroke(cr);

			W_SetSourceXColor(cr, &texture->light);
			cairo_move_to(cr, 0.5, 1.5);
			cairo_line_to(cr, w + 0.5, 1.5);
			cairo_move_to(cr, cw + 1.5, 2.5);
			cairo_line_to(cr, cw + 1.5, h + 0.5);
			cairo_move_to(cr, w - cw - 0.5, 2.5);
			cairo_line_to(cr, w - cw - 0.5, h + 0.5);
			cairo_stroke(cr);

#ifdef SHADOW_RESIZEBAR
			cairo_move_to(cr, 0.5, 1.5);
			cairo_line_to(cr, 0.5, h - 0.5);
			cairo_stroke(cr);
			W_SetSourceXColor(cr, &texture->dim);
			cairo_move_to(cr, w - 0.5, 2.5);
			cairo_line_to(cr, w - 0.5, h - 0.5);
			cairo_move_to(cr, 1.5, h - 0.5);
			cairo_line_to(cr, cw + 0.5, h - 0.5);
			cairo_move_to(cr, cw + 2.5, h - 0.5);
			cairo_line_to(cr, w - cw - 1.5, h - 0.5);
			cairo_move_to(cr, w - cw + 0.5, h - 0.5);
			cairo_line_to(cr, w - 0.5, h - 0.5);
			cairo_stroke(cr);
#endif				/* SHADOW_RESIZEBAR */
			WCairoEnd(cr);
		}
	}

	if (fwin->titlebar && !fwin->flags.repaint_only_resizebar) {
		const WTitlebarLayout *layout = frameLayout(fwin);
		int x, y, w, h;
		int lofs = 6, rofs = 6;
		int titlelen;
		int r, all_visible = 1;
		int icon_visible = buttonVisible(fwin, WTB_ICON) && fwin->titlebar_icon != XCB_NONE;
		int icon_x = 0;

		for (r = 0; r < WTB_ROLE_COUNT; r++) {
			if (!hasButton(fwin, r) || wtb_role_info[r].is_drawn)
				continue;
			if (!buttonVisible(fwin, r)) {
				all_visible = 0;
				continue;
			}
			if (!TS_NEW_GEOM(w_global.config->new_style)) {
				if (layout->side[r] == WTB_SIDE_LEFT)
					lofs += fwin->buttons[r]->width + 3;
				else
					rofs += fwin->buttons[r]->width + 3;
			}
		}

		y = frame_title_clearance(fwin) + TITLEBAR_EXTEND_SPACE;
		h = WSCALE(scr, WMFontHeight(*fwin->font));
		if (y * 2 + h > frame_title_max_height(fwin))
			y = (frame_title_max_height(fwin) - h) / 2;
		if (y * 2 + h < frame_title_min_height(fwin))
			y = (frame_title_min_height(fwin) - h) / 2;

		/* tab-group segmented titlebar branches before the
		 * single-title icon and text path, each segment owns its
		 * own icon slot so the WTB_ICON adjustment below is skipped
		 * when paintTabSegments() handles the surface */
		fwin->tab_seg_count = 0;
		if (paintTabSegments(fwin, state, lofs, rofs, y, h)) {
			for (r = 0; r < WTB_ROLE_COUNT; r++)
				if (fwin->buttons[r])
					handleButtonExpose(&fwin->buttons[r]->descriptor, NULL);
			return;
		}

		/* Icon lives on the titlebar surface in every style, in
		 * TS_NEW the surface is already shrunk between the button
		 * cores so the icon always renders adjacent to the title
		 * text on whichever side's array contains it */
		if (icon_visible) {
			if (layout->side[WTB_ICON] == WTB_SIDE_RIGHT) {
				icon_x = fwin->titlebar->width - rofs + 3 - fwin->titlebar_icon_size;
				rofs += fwin->titlebar_icon_size + 3;
			} else {
				icon_x = lofs - 3;
				lofs += fwin->titlebar_icon_size + 3;
			}
		}
#ifdef XKB_BUTTON_HINT
		if (hasButton(fwin, WTB_LANGUAGE) && !fwin->languagebutton_image[0])
			wFrameWindowUpdateLanguageButton(fwin);
#endif

		if (icon_visible) {
			cairo_t *cr;
			int iy = (fwin->titlebar->height - fwin->titlebar_icon_size) / 2;

			cr = WCairoBeginForDrawable(scr->wmscreen, fwin->titlebar->window);
			if (cr) {
				maskedBlit(cr, scr->rcontext, fwin->titlebar_icon,
					   fwin->titlebar_icon_mask,
					   fwin->titlebar_icon_size, fwin->titlebar_icon_size,
					   icon_x, iy);
				WCairoEnd(cr);
			}
		}

		if (fwin->title) {
			char *title;

			title = ShrinkString(*fwin->font, fwin->title, WUNSCALE(scr, fwin->titlebar->width - lofs - rofs));
			titlelen = strlen(title);
			w = WSCALE(scr, WMWidthOfString(*fwin->font, title, titlelen));

			switch (fwin->flags.justification) {
			case WTJ_LEFT:
				x = lofs;
				break;

			case WTJ_RIGHT:
				x = fwin->titlebar->width - w - rofs;
				break;

			default:
				if (!all_visible)
					x = lofs + (fwin->titlebar->width - w - lofs - rofs) / 2;
				else
					x = (fwin->titlebar->width - w) / 2;
				/* one-sided user layouts (and the existing
				 * modelock two-left/one-right case) can put
				 * a long centred title under the cluster,
				 * clamp it into the free span */
				if (x + w > fwin->titlebar->width - rofs)
					x = fwin->titlebar->width - rofs - w;
				if (x < lofs)
					x = lofs;
				break;
			}

			/* pangocairo lays glyphs out with a correct sub-pixel
			 * origin so the old off-screen buffer that padded a
			 * pixel either side for an Xft x=-1 quirk is gone,
			 * but a repeat paint without an intervening clear (a
			 * partial Expose after a state-change paint) would
			 * OVER-blend the AA fringe onto itself, so restore
			 * the window background under the text span first
			 * and the draw stays idempotent */
			xcb_clear_area(wdisp->conn, false, fwin->titlebar->window, x - 1, y, w + 2, h);
			WMDrawString(scr->wmscreen, fwin->titlebar->window,
				     fwin->title_color[state], *fwin->font,
				     x, y, title, titlelen);

			wfree(title);
		}

		for (r = 0; r < WTB_ROLE_COUNT; r++)
			if (fwin->buttons[r])
				handleButtonExpose(&fwin->buttons[r]->descriptor, NULL);
	}

	if (fwin->flags.is_client_window_frame && fwin->flags.border)
		paintInnerBorderStrips(fwin);
}

/*
 * Tab-group segmented titlebar renderer.
 *
 * lofs and rofs delimit the free title span between button cores in
 * titlebar-core coordinates, the same span the single-title path uses.
 * The span is split into n equal segments with a 1-pixel separator
 * between each, the active segment keeps the titlebar's own background
 * and the inactive ones are overlaid with the alternate-state pixmap so
 * they read as unfocused tabs.
 *
 * Segment edges are stored in fwin->tab_seg_x[0..n] for the click
 * hit-test in window.c. Returns 1 when segments were drawn, 0 when the
 * frame is not the active member of a multi-member group or
 * TabsInTitlebar is off so the caller continues into the single-title
 * renderer.
 */
static int paintTabSegments(WFrameWindow *fwin, int state, int lofs, int rofs, int y, int h)
{
	WScreen *scr = fwin->screen_ptr;
	RContext *rc = scr->rcontext;
	WWindow *owner, *member;
	WWindowGroup *grp;
	WFrameWindow *mframe;
	WTexture *seg_tex;
	cairo_surface_t *back;
	cairo_t *cr;
	const char *base;
	char *txt;
	int span, avail, tab_w, rem, sep_w;
	int n, i, sx, sw, tx, tw, len;
	int seg_state, col_state, th, iy, isz;
	int itex_slot, icol_slot, show_icon;

	if (!w_global.config->tabs_in_titlebar || !fwin->flags.is_client_window_frame)
		return 0;

	owner = (WWindow *) fwin->child;
	if (!owner || !owner->tab_group)
		return 0;
	grp = owner->tab_group;
	n = grp->member_count;
	if (n < 2 || grp->active_index != owner->tab_group_index)
		return 0;

	span = (int)fwin->titlebar->width - lofs - rofs;
	if (span <= n)
		return 0;

	sep_w = 1;
	avail = span - (n - 1) * sep_w;
	tab_w = avail / n;
	rem = avail % n;

	if (fwin->tab_seg_x == NULL || fwin->tab_seg_count != n)
		fwin->tab_seg_x = wrealloc(fwin->tab_seg_x, sizeof(int) * (size_t)(n + 1));
	fwin->tab_seg_count = n;

	sx = lofs;
	for (i = 0; i <= n; i++) {
		fwin->tab_seg_x[i] = sx;
		if (i < n)
			sx += tab_w + (i < rem ? 1 : 0) + (i < n - 1 ? sep_w : 0);
	}
	/* lofs and rofs both carry the single-title 6-px text padding on
	 * top of any button extent, that padding leaks the frame's own
	 * background colour between the outer segments and the buttons,
	 * widen the first and last segment to absorb it so the segment
	 * background runs flush to the button edge */
	fwin->tab_seg_x[0] = (lofs > 6) ? lofs - 6 : 0;
	fwin->tab_seg_x[n] = (int)fwin->titlebar->width - ((rofs > 6) ? rofs - 6 : 0);

	th = (int)fwin->titlebar->height;
	show_icon = buttonVisible(fwin, WTB_ICON);

	/* themes that set TabInactiveTitleBack/Color get a dedicated
	 * slot for inactive segments, otherwise borrow PTitleBack and
	 * PTitleColor exactly as before */
	itex_slot = fwin->title_texture[WS_TABINACTIVE]
		  ? WS_TABINACTIVE : WS_PFOCUSED;
	icol_slot = fwin->title_color[WS_TABINACTIVE]
		  ? WS_TABINACTIVE : WS_PFOCUSED;

	cr = WCairoBeginForDrawable(scr->wmscreen, fwin->titlebar->window);
	if (!cr)
		return 0;
	cairo_set_line_width(cr, 1.0);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);

	for (i = 0; i < n; i++) {
		sx = fwin->tab_seg_x[i];
		sw = fwin->tab_seg_x[i + 1] - sx - (i < n - 1 ? sep_w : 0);
		if (sw <= 0)
			continue;

		member = grp->members[i];
		mframe = member ? member->frame : NULL;

		/* the active segment uses WS_FOCUSED on a focused frame
		 * and WS_PFOCUSED on an unfocused one so it always stands
		 * out, inactive segments use the dedicated tab slot (or
		 * its PFOCUSED fallback) on a focused frame and
		 * WS_UNFOCUSED otherwise */
		if (i == grp->active_index) {
			seg_state = (state == WS_UNFOCUSED) ? WS_PFOCUSED : state;
			col_state = seg_state;
		} else {
			seg_state = (state == WS_FOCUSED) ? itex_slot
							  : WS_UNFOCUSED;
			col_state = (state == WS_FOCUSED) ? icol_slot
							  : WS_UNFOCUSED;
		}

		/* paint every segment's background even when seg_state
		 * matches the titlebar's own state, so a repeat paint
		 * without an intervening XClearWindow does not OVER-blend
		 * the AA-rendered label onto its previous self */
		seg_tex = fwin->title_texture[seg_state];
		if (seg_tex->any.type != WTEX_SOLID
		    && fwin->title_back[seg_state] != XCB_NONE) {
			back = cairo_xcb_surface_create(rc->xc,
					(xcb_drawable_t)fwin->title_back[seg_state],
					rc->xvisual, fwin->titlebar->width, th);
			cairo_set_source_surface(cr, back, 0, 0);
			cairo_rectangle(cr, sx, 0, sw, th);
			cairo_fill(cr);
			cairo_surface_destroy(back);
		} else {
			W_SetSourceXColor(cr, &seg_tex->solid.normal);
			cairo_rectangle(cr, sx, 0, sw, th);
			cairo_fill(cr);
			/* restore the top/bottom relief lines the fill just
			 * overpainted, interior side edges are the separator
			 * lines drawn below, the outermost segments redraw
			 * the titlebar's own left/right bevel */
			W_SetSourceXColor(cr, &seg_tex->solid.light);
			cairo_move_to(cr, sx + 0.5, 0.5);
			cairo_line_to(cr, sx + sw - 0.5, 0.5);
			if (sx == 0) {
				cairo_move_to(cr, 0.5, 0.5);
				cairo_line_to(cr, 0.5, th - 1.5);
			}
			cairo_stroke(cr);
			W_SetSourceXColor(cr, &seg_tex->solid.dim);
			cairo_move_to(cr, sx + 0.5, th - 1.5);
			cairo_line_to(cr, sx + sw - 0.5, th - 1.5);
			if (sx + sw >= (int)fwin->titlebar->width) {
				cairo_move_to(cr, sx + sw - 1.5, 1.5);
				cairo_line_to(cr, sx + sw - 1.5, th - 1.5);
			}
			cairo_stroke(cr);
			W_SetSourceXColor(cr, &seg_tex->solid.dark);
			cairo_move_to(cr, sx + 0.5, th - 0.5);
			cairo_line_to(cr, sx + sw - 0.5, th - 0.5);
			if (sx + sw >= (int)fwin->titlebar->width) {
				cairo_move_to(cr, sx + sw - 0.5, 0.5);
				cairo_line_to(cr, sx + sw - 0.5, th - 0.5);
			}
			cairo_stroke(cr);
		}

		tx = sx + 3;
		tw = sw - 6;

		isz = mframe ? (int)mframe->titlebar_icon_size : 0;
		if (show_icon && mframe && mframe->titlebar_icon != XCB_NONE
		    && isz > 0 && sw >= isz * 2) {
			iy = (th - isz) / 2;
			maskedBlit(cr, rc, mframe->titlebar_icon,
				   mframe->titlebar_icon_mask, isz, isz, tx, iy);
			tx += isz + 3;
			tw -= isz + 3;
		}

		if (tw <= 0)
			continue;

		base = (mframe && mframe->title) ? mframe->title : DEF_WINDOW_TITLE;
		txt = ShrinkString(*fwin->font, base, WUNSCALE(scr, tw));
		len = strlen(txt);
		if (WMWidthOfString(*fwin->font, txt, len) > 0) {
			cairo_save(cr);
			cairo_rectangle(cr, tx, y, tw, h);
			cairo_clip(cr);
			W_DrawString(cr, fwin->title_color[col_state],
					  *fwin->font, tx, y, txt, len);
			cairo_restore(cr);
		}
		wfree(txt);
	}

	/* 1-pixel relief separator between segments, dark/light pair so it
	 * reads on both gradient and solid backgrounds */
	for (i = 1; i < n; i++) {
		sx = fwin->tab_seg_x[i];
		W_SetSourceColor(cr, scr->darkGray);
		cairo_move_to(cr, sx - 0.5, 1.5);
		cairo_line_to(cr, sx - 0.5, th - 1.5);
		cairo_stroke(cr);
		W_SetSourceColor(cr, scr->gray);
		cairo_move_to(cr, sx + 0.5, 1.5);
		cairo_line_to(cr, sx + 0.5, th - 1.5);
		cairo_stroke(cr);
	}

	WCairoEnd(cr);
	return 1;
}

/*
 * Return the segment index under titlebar-relative x, or -1 when this
 * frame has no segments or x is outside the segmented span.
 */
int wFrameWindowTabSegmentAt(WFrameWindow *fwin, int x)
{
	int i;

	if (!fwin || fwin->tab_seg_count <= 0 || !fwin->tab_seg_x)
		return -1;
	if (x < fwin->tab_seg_x[0] || x >= fwin->tab_seg_x[fwin->tab_seg_count])
		return -1;
	for (i = 0; i < fwin->tab_seg_count; i++) {
		if (x < fwin->tab_seg_x[i + 1])
			return i;
	}
	return -1;
}

void wFrameWindowRepaintTitle(WFrameWindow *fwin)
{
	redrawTitlebar(fwin);
}

static void reconfigure(WFrameWindow * fwin, int x, int y, int width, int height, bool dontMove)
{
	int resizedHorizontally = 0;

	if (dontMove)
		xcb_configure_window(wdisp->conn, fwin->core->window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, (const uint32_t[]){ width, height });
	else
		xcb_configure_window(wdisp->conn, fwin->core->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, (const uint32_t[]){ x, y, width, height });

	if (fwin->core->width != width) {
		fwin->flags.need_texture_remake = 1;
		resizedHorizontally = 1;
	}

	fwin->core->width = width;
	fwin->core->height = height;

	if (fwin->titlebar && resizedHorizontally) {
		int r, k, fits, drawn_role_changed;

		/* Check if the titlebar is wide enough to hold the buttons.
		 * Temporarily remove them if can't
		 */
		for (r = 0; r < WTB_ROLE_COUNT; r++) {
			if (!fwin->buttons[r])
				continue;
			k = TS_NEW_GEOM(w_global.config->new_style)
				? wtb_role_info[r].dontfit_k_new
				: wtb_role_info[r].dontfit_k_old;
			fits = (width >= fwin->top_width * k);
			if (!fits && !dontFit(fwin, r)) {
				if (!hideButton(fwin, r))
					xcb_unmap_window(wdisp->conn, fwin->buttons[r]->window);
				setRoleBit(fwin->flags.button_dont_fit, r, 1);
			} else if (fits && dontFit(fwin, r)) {
				if (!hideButton(fwin, r))
					xcb_map_window(wdisp->conn, fwin->buttons[r]->window);
				setRoleBit(fwin->flags.button_dont_fit, r, 0);
			}
		}

		/* Icon dont-fit is surface-based, see updateIconFit() */
		drawn_role_changed = updateIconFit(fwin, width);

		updateTitlebar(fwin);
		checkTitleSize(fwin);
		/* child button cores get an Expose from map/unmap, the
		 * drawn icon does not so force a repaint when it flips */
		if (drawn_role_changed) {
			xcb_clear_area(wdisp->conn, 0, fwin->titlebar->window, 0, 0, 0, 0);
			wFrameWindowPaint(fwin);
		}
	}

	if (fwin->resizebar) {
		int prev_width = fwin->resizebar->width;

		wCoreConfigure(fwin->resizebar, 0,
			       fwin->core->height - fwin->resizebar->height,
			       fwin->core->width, fwin->resizebar->height);

		fwin->resizebar_corner_width = fwin->screen_ptr->resizebar_corner_width;
		if (fwin->core->width < fwin->resizebar_corner_width * 2 + RESIZEBAR_MIN_WIDTH)
			fwin->resizebar_corner_width = fwin->core->width / 2;

		/* opaque resize calls reconfigure() per motion step, the
		 * cursor regions only depend on bar width so skip the
		 * three XMoveResizeWindow round-trips when only height
		 * is changing */
		if (fwin->resizebar->width != prev_width)
			updateResizebarCursors(fwin);
	}
}

void wFrameWindowConfigure(WFrameWindow * fwin, int x, int y, int width, int height)
{
	reconfigure(fwin, x, y, width, height, false);
}

void wFrameWindowResize(WFrameWindow * fwin, int width, int height)
{
	reconfigure(fwin, 0, 0, width, height, true);
}

int wFrameWindowChangeTitle(WFrameWindow *fwin, const char *new_title)
{
	if (new_title == NULL)
		return 0;

	/* check if the title is the same as before */
	if (fwin->title) {
		if (strcmp(fwin->title, new_title) == 0)
			return 0;
	}

	if (fwin->title)
		wfree(fwin->title);

	fwin->title = wstrdup(new_title);

	redrawTitlebar(fwin);

	return 1;
}

#ifdef XKB_BUTTON_HINT

static int wFrameWindowSetLanguageButtonImages(WFrameWindow *fwin, xcb_pixmap_t *pixmaps, int count)
{
	int i;
	for (i = 0; i < count; i++) {
		WPixmap *wp = wPixmapCreate(pixmaps[i], XCB_NONE);
		if (!wp) {
			int j;
			xcb_free_pixmap(wdisp->conn, pixmaps[i]);
			for (j = i + 1; j < count; j++)
				xcb_free_pixmap(wdisp->conn, pixmaps[j]);
			return 0;
		}
		wp->client_owned = 0;
		wp->client_owned_mask = 0;
		if (fwin->languagebutton_image[i] && !fwin->languagebutton_image[i]->shared)
			wPixmapDestroy(fwin->languagebutton_image[i]);
		fwin->languagebutton_image[i] = wp;
	}
	return 1;
}

/* left half of tmp[i] is the normal-state background, right half the
 * pushed state, then the language label centred in each half */
static void paintLanguageHalf(cairo_t *cr, RContext *rc, int x, int kw, int kh,
			      xcb_pixmap_t back, const WXColor *fill,
			      WMColor *text, WMFont *font, const char *label,
			      int tx, int ty)
{
	cairo_surface_t *bs;

	if (back != XCB_NONE) {
		bs = cairo_xcb_surface_create(rc->xc, (xcb_drawable_t)back,
					      rc->xvisual, kw + 1, kh + 1);
		cairo_set_source_surface(cr, bs, x - 1, -1);
		cairo_rectangle(cr, x, 0, kw, kh);
		cairo_fill(cr);
		cairo_surface_destroy(bs);
	} else {
		W_SetSourceXColor(cr, fill);
		cairo_rectangle(cr, x, 0, kw, kh);
		cairo_fill(cr);
	}
	W_DrawString(cr, text, font, x + tx, ty, label, strlen(label));
}

void wFrameWindowUpdateLanguageButton(WFrameWindow * fwin)
{
	WScreen *scr = fwin->screen_ptr;
	RContext *rc = scr->rcontext;
	WCoreWindow *button = fwin->buttons[WTB_LANGUAGE];
	WMFont *small;
	WMColor *silver, *ntext, *ptext;
	const WXColor *nfill, *pfill;
	xcb_pixmap_t nback;
	cairo_surface_t *surf;
	cairo_t *cr;
	int i, count, small_px, text_width, text_height, tx, ty;
	int border_thickness = 3;
	int key_width, key_height;
	int group_index;
	xcb_pixmap_t tmp[2];		/* focused, unfocused */

	if (!fwin->flags.titlebar || fwin->core->descriptor.parent_type != WCLASS_WINDOW)
		return;

	if (!button || fwin->language_label[0] == '\0')
		return;

	group_index = fwin->languagemode;
	if (group_index < 0 || group_index >= 4)
		return;

	small_px = WMFontHeight(*fwin->font) * 55 / 100;
	small = WMBoldSystemFontOfSize(scr->wmscreen, small_px);
	text_width = WSCALE(scr, WMWidthOfString(small, fwin->language_label, strlen(fwin->language_label)));
	text_height = WSCALE(scr, WMFontHeight(small));

	key_width = button->width - border_thickness;
	key_height = button->height - border_thickness;

	if (key_width < 1 || key_height < 1) {
		WMReleaseFont(small);
		return;
	}

	count = TS_NEW_GEOM(w_global.config->new_style) ? 2 : 1;
	silver = NULL;
	tx = (key_width - text_width) / 2;
	ty = (key_height - text_height) / 2;

	for (i = 0; i < count; i++) {
		tmp[i] = xcb_generate_id(wdisp->conn);
		xcb_create_pixmap(wdisp->conn, scr->w_depth, tmp[i], button->window, 2 * key_width, key_height);
		WMScreenNoteDrawableSize(scr->wmscreen, tmp[i], 2 * key_width, key_height);

		nback = XCB_NONE;
		if (TS_NEW_GEOM(w_global.config->new_style)) {
			if (fwin->title_texture[i]->any.type != WTEX_SOLID
			    && fwin->title_back[i] != XCB_NONE)
				nback = fwin->button_back[WTB_LANGUAGE][i];
			nfill = &fwin->title_texture[i]->solid.normal;
			ntext = fwin->title_color[i];
			pfill = &scr->wmscreen->white->color;
			ptext = WMBlackColor(scr->wmscreen);
		} else if (w_global.config->new_style == TS_OLD) {
			nfill = &scr->widget_texture->normal;
			ntext = WMBlackColor(scr->wmscreen);
			pfill = &scr->wmscreen->white->color;
			ptext = WMDarkGrayColor(scr->wmscreen);
		} else {
			if (!silver)
				silver = WMCreateRGBColor(scr->wmscreen, 0xc0c0,
							  0xc0c0, 0xc0c0, true);
			nfill = pfill = &scr->widget_texture->dark;
			ntext = ptext = silver;
		}

		surf = cairo_xcb_surface_create(rc->xc, (xcb_drawable_t)tmp[i],
						rc->xvisual, 2 * key_width, key_height);
		cr = cairo_create(surf);
		paintLanguageHalf(cr, rc, 0, key_width, key_height, nback, nfill,
				  ntext, small, fwin->language_label, tx, ty);
		paintLanguageHalf(cr, rc, key_width, key_width, key_height, XCB_NONE, pfill,
				  ptext, small, fwin->language_label, tx, ty);
		cairo_destroy(cr);
		cairo_surface_flush(surf);
		cairo_surface_destroy(surf);
	}

	if (silver)
		WMReleaseColor(silver);
	WMReleaseFont(small);

	wFrameWindowSetLanguageButtonImages(fwin, tmp, count);
}
#endif				/* XKB_BUTTON_HINT */

/*********************************************************************/

static void handleExpose(WObjDescriptor * desc, xcb_generic_event_t * event)
{
	WFrameWindow *fwin = (WFrameWindow *) desc->parent;

	if (fwin->titlebar && fwin->titlebar->window == ((xcb_expose_event_t *)event)->window)
		fwin->flags.repaint_only_titlebar = 1;
	if (fwin->resizebar && fwin->resizebar->window == ((xcb_expose_event_t *)event)->window)
		fwin->flags.repaint_only_resizebar = 1;
	wFrameWindowPaint(fwin);
	fwin->flags.repaint_only_titlebar = 0;
	fwin->flags.repaint_only_resizebar = 0;
}

static void checkTitleSize(WFrameWindow * fwin)
{
	int width, r;

	if (!fwin->title) {
		fwin->flags.incomplete_title = 0;
		return;
	}

	if (!fwin->titlebar) {
		fwin->flags.incomplete_title = 1;
		return;
	} else {
		width = fwin->titlebar->width - 6 - 6;
	}

	if (!TS_NEW_GEOM(w_global.config->new_style)) {
		for (r = 0; r < WTB_ROLE_COUNT; r++)
			if (!wtb_role_info[r].is_drawn && buttonVisible(fwin, r))
				width -= fwin->buttons[r]->width + 3;
	}
	if (buttonVisible(fwin, WTB_ICON) && fwin->titlebar_icon != XCB_NONE)
		width -= fwin->titlebar_icon_size + 3;

	if (WSCALE(fwin->screen_ptr, WMWidthOfString(*fwin->font, fwin->title, strlen(fwin->title))) > width)
		fwin->flags.incomplete_title = 1;
	else
		fwin->flags.incomplete_title = 0;
}

static void paintButton(WCoreWindow *button, WTexture *texture, WMColor *color,
			WPixmap *image, int pushed, int from_xpm)
{
	WScreen *scr = button->screen_ptr;
	RContext *rc = scr->rcontext;
	cairo_surface_t *msurf = NULL;
	cairo_t *cr;
	int x, y, d = 0;
	int left = 0, width = 0;
	int bw = button->width, bh = button->height;
	int rw, rh, stencil;

	/* a theme-supplied colour pixmap must keep its own colours, the
	 * from_xpm path stencils the mask in title_color which would turn
	 * a red close circle into a title-coloured silhouette, so route
	 * themed images through the pixmap-source branch and let the
	 * press feedback come from the d=1 offset and the optional second
	 * half of a double-wide image */
	if (image && image->themed)
		from_xpm = 0;

	if (image) {
		if (image->width >= image->height * 2) {
			/* the image contains 2 pictures: the second is for the
			 * pushed state */
			width = image->width / 2;
			if (pushed)
				left = width;
		} else {
			width = image->width;
		}
	}

	if (!pushed || w_global.config->new_style == TS_FLAT)
		xcb_clear_area(wdisp->conn, 0, button->window, 0, 0, 0, 0);

	cr = WCairoBeginForDrawable(scr->wmscreen, button->window);
	if (!cr)
		return;

	if (pushed) {
		d = 1;
		/* keep the texture slice as the pushed background under
		 * TS_FLAT so a round pixmap stays flush, the press feedback
		 * is the darkened glyph and the 1 px offset only */
		if (w_global.config->new_style != TS_FLAT) {
			if (w_global.config->new_style == TS_NEXT)
				cairo_set_source_rgb(cr, 0, 0, 0);
			else
				cairo_set_source_rgb(cr, 1, 1, 1);
			if (w_global.config->new_style == TS_NEW) {
				rw = bw - 1;
				rh = bh - 1;
			} else if (w_global.config->new_style == TS_OLD) {
				rw = bw;
				rh = bh;
			} else {
				rw = bw - 3;
				rh = bh - 3;
			}
			cairo_rectangle(cr, 0, 0, rw, rh);
			cairo_fill(cr);
			cairo_set_source_rgb(cr, 0, 0, 0);
			cairo_set_line_width(cr, 1.0);
			cairo_rectangle(cr, 0.5, 0.5, rw, rh);
			cairo_stroke(cr);
		}
	} else {
		if (w_global.config->new_style == TS_NEW) {
			if (texture->any.type == WTEX_SOLID)
				wDrawBevel(cr, bw, bh, (WTexSolid *)texture, WREL_RAISED);
		} else if (w_global.config->new_style != TS_FLAT) {
			wDrawBevel(cr, bw, bh, scr->widget_texture, WREL_RAISED);
		}
	}

	if (image) {
		cairo_surface_t *isurf = NULL;

		x = (bw - width) / 2 + d;
		y = (bh - image->height) / 2 + d;

		/* built-in glyphs are stencilled through the mask in a
		 * flat colour under TS_NEW/TS_FLAT (title colour normally,
		 * black when pushed) and under TS_OLD when pushed (dark
		 * grey), every other combination blits the pixmap itself */
		stencil = from_xpm && (TS_NEW_GEOM(w_global.config->new_style)
				       || (pushed && w_global.config->new_style == TS_OLD));

		/* the mask (and image, when double-wide) covers both halves,
		 * offset the source origin so the wanted half sits under
		 * (x,y) and clip to that half so the other never bleeds */
		cairo_rectangle(cr, x, y, width, image->height);
		cairo_clip(cr);

		if (stencil) {
			if (!TS_NEW_GEOM(w_global.config->new_style))
				W_SetSourceColor(cr, scr->darkGray);
			else if (pushed)
				cairo_set_source_rgb(cr, 0, 0, 0);
			else
				W_SetSourceColor(cr, color);
		} else {
			isurf = cairo_xcb_surface_create(rc->xc,
					(xcb_drawable_t)image->image, rc->xvisual,
					image->width, image->height);
			cairo_set_source_surface(cr, isurf, x - left, y);
		}

		if (image->mask != XCB_NONE) {
			msurf = cairo_xcb_surface_create_for_bitmap(rc->xc, rc->xscreen,
					(xcb_pixmap_t)image->mask,
					image->width, image->height);
			cairo_mask_surface(cr, msurf, x - left, y);
			cairo_surface_destroy(msurf);
		} else {
			cairo_paint(cr);
		}
		if (isurf)
			cairo_surface_destroy(isurf);
	}

	WCairoEnd(cr);
}

static void handleButtonExpose(WObjDescriptor * desc, xcb_generic_event_t * event)
{
	WFrameWindow *fwin = (WFrameWindow *) desc->parent;
	WCoreWindow *button = (WCoreWindow *) desc->self;
	int state;
	int r;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) event;

	state = frameEffectiveState(fwin);

	for (r = 0; r < WTB_ROLE_COUNT; r++)
		if (button == fwin->buttons[r])
			break;
	if (r >= WTB_ROLE_COUNT)
		return;

#ifdef XKB_BUTTON_HINT
	if (r == WTB_LANGUAGE) {
		if (!hideButton(fwin, WTB_LANGUAGE)) {
			/* map focused and pfocused states to focus language button image */
			int lb_index = TS_NEW_GEOM(w_global.config->new_style) && (state == 1) ? 1 : 0;

			paintButton(button, fwin->title_texture[state],
				    fwin->title_color[state],
				    fwin->languagebutton_image[lb_index],
				    false, false);
		}
		return;
	}
#endif
	paintButton(button, fwin->title_texture[state],
		    fwin->title_color[state],
		    fwin->button_image[r], false, wtb_role_info[r].from_xpm);
}

static void titlebarMouseDown(WObjDescriptor * desc, xcb_generic_event_t * event)
{
	WFrameWindow *fwin = desc->parent;
	WCoreWindow *titlebar = desc->self;

	if (IsDoubleClick(fwin->core->screen_ptr, event)) {
		if (fwin->on_dblclick_titlebar)
			(*fwin->on_dblclick_titlebar) (titlebar, fwin->child, event);
	} else {
		if (fwin->on_mousedown_titlebar)
			(*fwin->on_mousedown_titlebar) (titlebar, fwin->child, event);
	}
}

static void resizebarMouseDown(WObjDescriptor * desc, xcb_generic_event_t * event)
{
	WFrameWindow *fwin = desc->parent;
	WCoreWindow *resizebar = desc->self;

	if (fwin->on_mousedown_resizebar)
		(*fwin->on_mousedown_resizebar) (resizebar, fwin->child, event);
}

static void buttonMouseDown(WObjDescriptor * desc, xcb_generic_event_t * event)
{
	WFrameWindow *fwin = desc->parent;
	WCoreWindow *button = desc->self;
	WPixmap *image;
	xcb_generic_event_t *ev = NULL;
	int r, done = 0, execute = 1, from_xpm;
	WTexture *texture;
	WMColor *color;
	int clickButton = ((xcb_button_press_event_t *)event)->detail;

	for (r = 0; r < WTB_ROLE_COUNT; r++)
		if (button == fwin->buttons[r])
			break;
	if (r >= WTB_ROLE_COUNT)
		return;

	if (IsDoubleClick(fwin->core->screen_ptr, event)) {
		if (fwin->on_dblclick_button[r])
			(*fwin->on_dblclick_button[r]) (button, fwin->child, event);

		return;
	}

	image = fwin->button_image[r];
	from_xpm = wtb_role_info[r].from_xpm;

#ifdef XKB_BUTTON_HINT
	if (r == WTB_LANGUAGE) {
		if (!w_global.config->modelock)
			return;

		/* map focused and pfocused states to focus language button image */
		int lb_index = TS_NEW_GEOM(w_global.config->new_style) && (fwin->flags.state == 1) ? 1 : 0;

		image = fwin->languagebutton_image[lb_index];
	}
#endif

	color = fwin->title_color[frameEffectiveState(fwin)];
	texture = fwin->title_texture[frameEffectiveState(fwin)];
	paintButton(button, texture, color, image, true, from_xpm);

	while (!done) {
		free(ev);
		ev = NULL;
		WMMaskEvent(wdisp->conn, XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_BUTTON_RELEASE
			    | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_EXPOSURE, &ev);
		if (!ev)
			break;
		switch ((ev->response_type & 0x7f)) {
		case XCB_LEAVE_NOTIFY:
			execute = 0;
			paintButton(button, texture, color, image, false, from_xpm);
			break;

		case XCB_ENTER_NOTIFY:
			execute = 1;
			paintButton(button, texture, color, image, true, from_xpm);
			break;

		case XCB_BUTTON_PRESS:
			break;

		case XCB_BUTTON_RELEASE:
			if ((((xcb_button_press_event_t *)ev)->detail) == clickButton)
				done = 1;
			break;

		default:
			WMHandleEvent(ev);
		}
	}
	paintButton(button, texture, color, image, false, from_xpm);

	if (execute && fwin->on_click_button[r])
		(*fwin->on_click_button[r]) (button, fwin->child, ev);
	free(ev);
}
