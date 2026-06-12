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

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef KEEP_XKB_LOCK_STATUS
#include <X11/XKBlib.h>
#endif				/* KEEP_XKB_LOCK_STATUS */

#include <stdlib.h>
#include <string.h>

#include <wraster.h>

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
 * XIDs so existing read sites (updateTexture, the title XCopyArea path)
 * are unchanged, only the create/free sites move to acquire/release.
 */

struct WFrameTexCache {
	/* key */
	WTexture *texture;
	int width, height;
	int extra;                /* title: (nleft<<4)|nright, resizebar: cwidth */
	/* value */
	Pixmap title;
	Pixmap buttons[WTB_ROLE_COUNT];   /* positional: [0..nleft-1] then [nleft..nleft+nright-1] */
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
	const char *name;
	int flag_shift;
	short dontfit_k_new;
	short dontfit_k_old;
	Bool from_xpm;
	Bool is_drawn;
} wtb_role_info[WTB_ROLE_COUNT] = {
	[WTB_MINIATURIZE] = { "Miniaturize", 1, 4, 3, True,  False },
	[WTB_CLOSE]       = { "Close",       2, 2, 2, True,  False },
#ifdef XKB_BUTTON_HINT
	[WTB_LANGUAGE]    = { "Language",    6, 4, 3, False, False },
#endif
};

const int wtb_role_flag_shift[WTB_ROLE_COUNT] = {
	[WTB_MINIATURIZE] = 1,
	[WTB_CLOSE]       = 2,
#ifdef XKB_BUTTON_HINT
	[WTB_LANGUAGE]    = 6,
#endif
};

static const WTitlebarLayout default_layout = {
	.side = {
		[WTB_MINIATURIZE] = WTB_SIDE_LEFT,
		[WTB_CLOSE]       = WTB_SIDE_RIGHT,
#ifdef XKB_BUTTON_HINT
		[WTB_LANGUAGE]    = WTB_SIDE_LEFT,
#endif
	},
	.order = {
		[WTB_MINIATURIZE] = 0,
		[WTB_CLOSE]       = 0,
#ifdef XKB_BUTTON_HINT
		[WTB_LANGUAGE]    = 1,
#endif
	},
};

static inline const WTitlebarLayout *frameLayout(const WFrameWindow *fwin)
{
	(void) fwin;
	return &default_layout;
}

static inline int hasButton(const WFrameWindow *fwin, int role)
{
	return (fwin->flags.has_button >> role) & 1;
}

static inline int hideButton(const WFrameWindow *fwin, int role)
{
	return (fwin->flags.hide_button >> role) & 1;
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
	return fwin->buttons[role] && !hideButton(fwin, role) && !dontFit(fwin, role);
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
		if (w_global.config->new_style == TS_NEW) {
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
	fwin->title_back[state] = None;
	for (r = 0; r < WTB_ROLE_COUNT; r++)
		fwin->button_back[r][state] = None;
}

static void release_resizebar_set(WFrameWindow *fwin)
{
	texcache_release(&resize_tex_cache, fwin->resizebar_cache);
	fwin->resizebar_cache = NULL;
	fwin->resizebar_back[0] = None;
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
	XSetWindowAttributes attr;
	int cw, w, h, geom[3][2], i;

	if (!fwin->resizebar) {
		/* children went with the parent on XDestroyWindow */
		for (i = 0; i < 3; i++)
			fwin->resizebar_cursor[i] = None;
		return;
	}

	cw = fwin->resizebar_corner_width;
	w = fwin->resizebar->width;
	h = fwin->resizebar->height;
	geom[0][0] = 0;       geom[0][1] = cw;
	geom[1][0] = cw;      geom[1][1] = w - 2 * cw;
	geom[2][0] = w - cw;  geom[2][1] = cw;

	for (i = 0; i < 3; i++) {
		if (fwin->resizebar_cursor[i] == None) {
			attr.cursor = wPreferences.cursor[rcursor[i]];
			fwin->resizebar_cursor[i] =
				XCreateWindow(dpy, fwin->resizebar->window, 0, 0, 1, h, 0,
					      0, InputOnly, CopyFromParent, CWCursor, &attr);
		}
		if (geom[i][1] > 0) {
			XMoveResizeWindow(dpy, fwin->resizebar_cursor[i],
					  geom[i][0], 0, geom[i][1], h);
			XMapWindow(dpy, fwin->resizebar_cursor[i]);
		} else {
			XUnmapWindow(dpy, fwin->resizebar_cursor[i]);
		}
	}
}

static void handleExpose(WObjDescriptor * desc, XEvent * event);
static void handleButtonExpose(WObjDescriptor * desc, XEvent * event);

static void buttonMouseDown(WObjDescriptor * desc, XEvent * event);
static void titlebarMouseDown(WObjDescriptor * desc, XEvent * event);
static void resizebarMouseDown(WObjDescriptor * desc, XEvent * event);

static void checkTitleSize(WFrameWindow * fwin);

static void paintButton(WCoreWindow * button, WTexture * texture,
			unsigned long color, WPixmap * image, int pushed, int from_xpm);

static void updateTitlebar(WFrameWindow * fwin);

static void allocFrameBorderPixel(WFrameWindow *fwin, WMColor *color, unsigned long **pixel)
{
	XColor xcol;
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
				 int depth, Visual *visual, Colormap colormap)
{
	WFrameWindow *fwin;

	fwin = wmalloc(sizeof(WFrameWindow));

	fwin->screen_ptr = scr;

	fwin->flags.single_texture = (flags & WFF_SINGLE_STATE) ? 1 : 0;
	fwin->flags.is_menu = (flags & WFF_IS_MENU) ? 1 : 0;

	fwin->title_texture = title_texture;
	fwin->resizebar_texture = resize_texture;
	fwin->title_color = color;
	fwin->font = font;
#ifdef KEEP_XKB_LOCK_STATUS
	fwin->languagemode = XkbGroup1Index;
	fwin->last_languagemode = XkbGroup2Index;
	wWindowGetLanguageLabel(fwin->languagemode, fwin->language_label);
#endif

	fwin->depth = depth;
	fwin->visual = visual;
	fwin->colormap = colormap;

	fwin->core = wCoreCreateTopLevel(scr, x, y, width, height, (flags & WFF_BORDER)
					 ? scr->frame_border_width : 0, fwin->depth, fwin->visual, fwin->colormap, scr->frame_border_pixel);

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
	WScreen *scr = fwin->screen_ptr;

	width = fwin->core->width;
	if (flags & WFF_IS_SHADED)
		height = -1;
	else
		height = fwin->core->height - fwin->top_width - fwin->bottom_width;

	if (flags & WFF_TITLEBAR) {
		theight = WMFontHeight(*fwin->font) + (frame_title_clearance(fwin) + TITLEBAR_EXTEND_SPACE) * 2;

		if (theight > frame_title_max_height(fwin))
			theight = frame_title_max_height(fwin);

		if (theight < frame_title_min_height(fwin))
			theight = frame_title_min_height(fwin);
	} else {
		theight = 0;
		fwin->flags.titlebar = 0;
	}

	if (w_global.config->new_style == TS_NEW) {
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
			for (i = 0; i < (fwin->flags.single_texture ? 1 : 3); i++)
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
			WCoreWindow *parent;
			int r, k;

			fwin->top_width = theight;

			fwin->flags.titlebar = 1;
			fwin->titlebar = wCoreCreate(fwin->core, 0, 0, width + 1, theight);

			parent = (w_global.config->new_style == TS_NEW) ? fwin->core : fwin->titlebar;

			for (r = 0; r < WTB_ROLE_COUNT; r++) {
				if (!(flags & WFF_ROLE_BIT(r)))
					continue;
				setRoleBit(fwin->flags.has_button, r, 1);
				fwin->buttons[r] = wCoreCreate(parent, 0, 0, bsize, bsize);
				if (w_global.config->new_style == TS_OLD)
					XSetWindowBackground(dpy, fwin->buttons[r]->window,
							     scr->widget_texture->normal.pixel);
				else if (w_global.config->new_style == TS_NEXT)
					XSetWindowBackground(dpy, fwin->buttons[r]->window,
							     scr->widget_texture->dark.pixel);

				k = (w_global.config->new_style == TS_NEW)
					? wtb_role_info[r].dontfit_k_new
					: wtb_role_info[r].dontfit_k_old;
				if (width < theight * k)
					setRoleBit(fwin->flags.button_dont_fit, r, 1);
				else
					XMapRaised(dpy, fwin->buttons[r]->window);
			}

			layoutButtonGeometry(fwin, width, bsize, theight);

			if (w_global.config->new_style == TS_NEW)
				updateTitlebar(fwin);

			XMapRaised(dpy, fwin->titlebar->window);

			fwin->flags.need_texture_remake = 1;
		}
	}
	checkTitleSize(fwin);

	if (flags & WFF_RESIZEBAR) {
		fwin->bottom_width = RESIZEBAR_HEIGHT;

		if (!fwin->resizebar) {
			fwin->flags.resizebar = 1;
			fwin->resizebar = wCoreCreate(fwin->core, 0,
						      height + fwin->top_width, width, RESIZEBAR_HEIGHT);
			fwin->resizebar_corner_width = RESIZEBAR_CORNER_WIDTH;
			if (width < RESIZEBAR_CORNER_WIDTH * 2 + RESIZEBAR_MIN_WIDTH) {
				fwin->resizebar_corner_width = (width - RESIZEBAR_MIN_WIDTH) / 2;
				if (fwin->resizebar_corner_width < 0)
					fwin->resizebar_corner_width = 0;
			}

			XMapWindow(dpy, fwin->resizebar->window);
			XLowerWindow(dpy, fwin->resizebar->window);
			updateResizebarCursors(fwin);

			fwin->flags.need_texture_remake = 1;
		} else {
			if (height + fwin->top_width + fwin->bottom_width != fwin->core->height)
				wCoreConfigure(fwin->resizebar, 0, height + fwin->top_width,
					       width, RESIZEBAR_HEIGHT);
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

	if (height + fwin->top_width + fwin->bottom_width != fwin->core->height && !(flags & WFF_IS_SHADED))
		wFrameWindowResize(fwin, width, height + fwin->top_width + fwin->bottom_width);

	if (flags & WFF_BORDER)
		XSetWindowBorderWidth(dpy, fwin->core->window, scr->frame_border_width);
	else
		XSetWindowBorderWidth(dpy, fwin->core->window, 0);

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

	allocFrameBorderPixel(fwin, scr->frame_border_color, &fwin->border_pixel);
	allocFrameBorderPixel(fwin, scr->frame_focused_border_color, &fwin->focused_border_pixel);
	allocFrameBorderPixel(fwin, scr->frame_selected_border_color, &fwin->selected_border_pixel);

	if (flags & WFF_SELECTED) {
		if (fwin->selected_border_pixel)
			XSetWindowBorder(dpy, fwin->core->window, *fwin->selected_border_pixel);
	}
	else {
		if (fwin->flags.state == WS_FOCUSED) {
			if (fwin->focused_border_pixel)
				XSetWindowBorder(dpy, fwin->core->window, *fwin->focused_border_pixel);
		} else {
			if (fwin->border_pixel)
				XSetWindowBorder(dpy, fwin->core->window, *fwin->border_pixel);
		}
	}
}

void wFrameWindowDestroy(WFrameWindow * fwin)
{
	int i;

	for (i = 0; i < WTB_ROLE_COUNT; i++)
		if (fwin->buttons[i])
			wCoreDestroy(fwin->buttons[i]);

	if (fwin->resizebar)
		wCoreDestroy(fwin->resizebar);

	if (fwin->titlebar)
		wCoreDestroy(fwin->titlebar);

	RemoveFromStackList(fwin->core);

	wCoreDestroy(fwin->core);

	if (fwin->title)
		wfree(fwin->title);

	for (i = 0; i < (fwin->flags.single_texture ? 1 : 3); i++)
		release_title_set(fwin, i);
	release_resizebar_set(fwin);

	wfree(fwin->border_pixel);
	wfree(fwin->focused_border_pixel);
	wfree(fwin->selected_border_pixel);

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
			XSetWindowBorder(dpy, fwin->core->window, *fwin->focused_border_pixel);
	} else {
		if (fwin->border_pixel)
			XSetWindowBorder(dpy, fwin->core->window, *fwin->border_pixel);
	}
	wFrameWindowPaint(fwin);
}

static void updateTitlebar(WFrameWindow * fwin)
{
	short rolepos[WTB_ROLE_COUNT];
	int x, w, nleft, nright;
	int theight, bsize;

	theight = WMFontHeight(*fwin->font) + (frame_title_clearance(fwin) + TITLEBAR_EXTEND_SPACE) * 2;

	if (theight > frame_title_max_height(fwin))
		theight = frame_title_max_height(fwin);

	if (theight < frame_title_min_height(fwin))
		theight = frame_title_min_height(fwin);

	if (w_global.config->new_style == TS_NEW)
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

	if (w_global.config->new_style == TS_NEW) {
		x = nleft * bsize;
		w -= (nleft + nright) * bsize;
	}

	if (w_global.config->new_style == TS_NEW || fwin->titlebar->width != w)
		fwin->flags.need_texture_remake = 1;

	wCoreConfigure(fwin->titlebar, x, 0, w, theight);
}

void wFrameWindowHideButton(WFrameWindow * fwin, int flags)
{
	int r;

	for (r = 0; r < WTB_ROLE_COUNT; r++) {
		if (!(flags & WFF_ROLE_BIT(r)) || !hasButton(fwin, r))
			continue;
		if (fwin->buttons[r])
			XUnmapWindow(dpy, fwin->buttons[r]->window);
		setRoleBit(fwin->flags.hide_button, r, 1);
	}

	if (fwin->titlebar) {
		if (w_global.config->new_style == TS_NEW) {
			updateTitlebar(fwin);
		} else {
			updateTitlebar(fwin);
			XClearWindow(dpy, fwin->titlebar->window);
			wFrameWindowPaint(fwin);
		}
		checkTitleSize(fwin);
	}
}

void wFrameWindowShowButton(WFrameWindow * fwin, int flags)
{
	int r;

	for (r = 0; r < WTB_ROLE_COUNT; r++) {
		if (!(flags & WFF_ROLE_BIT(r)) || !hasButton(fwin, r) || !hideButton(fwin, r))
			continue;
		if (fwin->buttons[r] && !dontFit(fwin, r))
			XMapWindow(dpy, fwin->buttons[r]->window);
		setRoleBit(fwin->flags.hide_button, r, 0);
	}

	if (fwin->titlebar) {
		if (w_global.config->new_style == TS_NEW) {
			updateTitlebar(fwin);
		} else {
			updateTitlebar(fwin);
			XClearWindow(dpy, fwin->titlebar->window);
			wFrameWindowPaint(fwin);
		}
		checkTitleSize(fwin);
	}
}

static void
renderTexture(WScreen * scr, WTexture * texture, int width, int height,
	      int bwidth, int bheight, int nleft, int nright,
	      Pixmap * title, Pixmap slices[])
{
	RImage *img, *sub, *mimg;
	int x, w, i;

	*title = None;
	for (i = 0; i < nleft + nright; i++)
		slices[i] = None;

	img = wTextureRenderImage(texture, width, height, WREL_FLAT);
	if (!img) {
		wwarning(_("could not render texture: %s"), RMessageForError(RErrorCode));
		return;
	}

	if (w_global.config->new_style == TS_NEW) {
		x = 0;
		w = img->width;

		for (i = 0; i < nleft; i++) {
			sub = RGetSubImage(img, i * bwidth, 0, bwidth, bheight);
			if (sub) {
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
				RBevelImage(sub, RBEV_RAISED2);
				if (!RConvertImage(scr->rcontext, sub, &slices[nleft + i]))
					wwarning(_("error rendering image:%s"), RMessageForError(RErrorCode));
				w -= sub->width;
				RReleaseImage(sub);
			}
		}

		if (w != width) {
			mimg = RGetSubImage(img, x, 0, w, img->height);
			RBevelImage(mimg, RBEV_RAISED2);

			if (!RConvertImage(scr->rcontext, mimg, title))
				wwarning(_("error rendering image:%s"), RMessageForError(RErrorCode));

			RReleaseImage(mimg);
		} else {
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

static void
renderResizebarTexture(WScreen * scr, WTexture * texture, int width, int height, int cwidth, Pixmap * pmap)
{
	RImage *img;
	RColor light;
	RColor dark;

	*pmap = None;

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

	i = fwin->flags.state;
	if (fwin->titlebar) {
		if (fwin->title_texture[i]->any.type != WTEX_SOLID) {
			XSetWindowBackgroundPixmap(dpy, fwin->titlebar->window, fwin->title_back[i]);
			if (w_global.config->new_style == TS_NEW) {
				for (r = 0; r < WTB_ROLE_COUNT; r++)
					if (fwin->buttons[r] && fwin->button_back[r][i])
						XSetWindowBackgroundPixmap(dpy, fwin->buttons[r]->window,
									   fwin->button_back[r][i]);
			}
		} else {
			pixel = fwin->title_texture[i]->solid.normal.pixel;
			XSetWindowBackground(dpy, fwin->titlebar->window, pixel);
			if (w_global.config->new_style == TS_NEW) {
				for (r = 0; r < WTB_ROLE_COUNT; r++)
					if (fwin->buttons[r])
						XSetWindowBackground(dpy, fwin->buttons[r]->window, pixel);
			}
		}
		XClearWindow(dpy, fwin->titlebar->window);

		for (r = 0; r < WTB_ROLE_COUNT; r++) {
			if (!fwin->buttons[r])
				continue;
			XClearWindow(dpy, fwin->buttons[r]->window);
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
			extra = (w_global.config->new_style == TS_NEW)
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
			if (w_global.config->new_style == TS_NEW) {
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
			XSetWindowBackgroundPixmap(dpy, fwin->resizebar->window, fwin->resizebar_back[0]);
		else
			XSetWindowBackground(dpy, fwin->resizebar->window,
					     fwin->resizebar_texture[0]->solid.normal.pixel);

		XClearWindow(dpy, fwin->resizebar->window);
	}
}

void wFrameWindowPaint(WFrameWindow * fwin)
{
	WScreen *scr = fwin->screen_ptr;
	int state;

	state = fwin->flags.state;

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

			for (i = 0; i < 3; i++) {
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
	    && fwin->title_texture[state]->any.type == WTEX_SOLID) {
		wDrawBevel(fwin->titlebar->window, fwin->titlebar->width,
			   fwin->titlebar->height, (WTexSolid *) fwin->title_texture[state], WREL_RAISED);
	}

	if (fwin->resizebar && !fwin->flags.repaint_only_titlebar
	    && fwin->resizebar_texture[0]->any.type == WTEX_SOLID) {
		Window win;
		int w, h;
		int cw;
		GC light_gc, dim_gc;
		WTexSolid *texture = (WTexSolid *) fwin->resizebar_texture[0];

		w = fwin->resizebar->width;
		h = fwin->resizebar->height;
		cw = fwin->resizebar_corner_width;
		light_gc = texture->light_gc;
		dim_gc = texture->dim_gc;
		win = fwin->resizebar->window;

		XDrawLine(dpy, win, dim_gc, 0, 0, w, 0);
		XDrawLine(dpy, win, light_gc, 0, 1, w, 1);

		XDrawLine(dpy, win, dim_gc, cw, 2, cw, h);
		XDrawLine(dpy, win, light_gc, cw + 1, 2, cw + 1, h);

		XDrawLine(dpy, win, dim_gc, w - cw - 2, 2, w - cw - 2, h);
		XDrawLine(dpy, win, light_gc, w - cw - 1, 2, w - cw - 1, h);

#ifdef SHADOW_RESIZEBAR
		XDrawLine(dpy, win, light_gc, 0, 1, 0, h - 1);
		XDrawLine(dpy, win, dim_gc, w - 1, 2, w - 1, h - 1);
		XDrawLine(dpy, win, dim_gc, 1, h - 1, cw, h - 1);
		XDrawLine(dpy, win, dim_gc, cw + 2, h - 1, w - cw - 2, h - 1);
		XDrawLine(dpy, win, dim_gc, w - cw, h - 1, w - 1, h - 1);
#endif				/* SHADOW_RESIZEBAR */
	}

	if (fwin->titlebar && !fwin->flags.repaint_only_resizebar) {
		const WTitlebarLayout *layout = frameLayout(fwin);
		int x, y, w, h;
		int lofs = 6, rofs = 6;
		int titlelen;
		int r, all_visible = 1;

		for (r = 0; r < WTB_ROLE_COUNT; r++) {
			if (!hasButton(fwin, r))
				continue;
			if (!buttonVisible(fwin, r)) {
				all_visible = 0;
				continue;
			}
			if (w_global.config->new_style != TS_NEW) {
				if (layout->side[r] == WTB_SIDE_LEFT)
					lofs += fwin->buttons[r]->width + 3;
				else
					rofs += fwin->buttons[r]->width + 3;
			}
		}
#ifdef XKB_BUTTON_HINT
		if (hasButton(fwin, WTB_LANGUAGE) && !fwin->languagebutton_image[0])
			wFrameWindowUpdateLanguageButton(fwin);
#endif

		if (fwin->title) {
			Drawable buf;
			char *title;

			title = ShrinkString(*fwin->font, fwin->title, fwin->titlebar->width - lofs - rofs);
			titlelen = strlen(title);
			w = WMWidthOfString(*fwin->font, title, titlelen);

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
				break;
			}

			y = frame_title_clearance(fwin) + TITLEBAR_EXTEND_SPACE;
			h = WMFontHeight(*fwin->font);

			if (y*2 + h > frame_title_max_height(fwin))
				y = (frame_title_max_height(fwin) - h) / 2;

			if (y*2 + h < frame_title_min_height(fwin))
				y = (frame_title_min_height(fwin) - h) / 2;

			/* We use a w+2 buffer to have an extra pixel on the left and
			 * another one on the right. This is because for some odd reason,
			 * sometimes when using AA fonts (when libfreetype2 is compiled
			 * with bytecode interpreter turned off), some fonts are drawn
			 * starting from x = -1 not from 0 as requested. Observed with
			 * capital A letter on the bold 'trebuchet ms' font. -Dan
			 */
			buf = XCreatePixmap(dpy, fwin->titlebar->window, w + 2, h, scr->w_depth);

			XSetClipMask(dpy, scr->copy_gc, None);

			if (fwin->title_texture[state]->any.type != WTEX_SOLID) {
				XCopyArea(dpy, fwin->title_back[state], buf, scr->copy_gc,
					  x - 1, y, w + 2, h, 0, 0);
			} else {
				XSetForeground(dpy, scr->copy_gc, fwin->title_texture[state]->solid.normal.pixel);
				XFillRectangle(dpy, buf, scr->copy_gc, 0, 0, w + 2, h);
			}

			/*XDrawRectangle(dpy, buf, WMColorGC(scr->white),1,0,w,h-1); */
			WMDrawString(scr->wmscreen, buf, fwin->title_color[state],
				     *fwin->font, 1, 0, title, titlelen);

			XCopyArea(dpy, buf, fwin->titlebar->window, scr->copy_gc, 0, 0, w + 2, h, x - 1, y);

			XFreePixmap(dpy, buf);

			wfree(title);
		}

		for (r = 0; r < WTB_ROLE_COUNT; r++)
			if (fwin->buttons[r])
				handleButtonExpose(&fwin->buttons[r]->descriptor, NULL);
	}
}

static void reconfigure(WFrameWindow * fwin, int x, int y, int width, int height, Bool dontMove)
{
	int resizedHorizontally = 0;

	if (dontMove)
		XResizeWindow(dpy, fwin->core->window, width, height);
	else
		XMoveResizeWindow(dpy, fwin->core->window, x, y, width, height);

	if (fwin->core->width != width) {
		fwin->flags.need_texture_remake = 1;
		resizedHorizontally = 1;
	}

	fwin->core->width = width;
	fwin->core->height = height;

	if (fwin->titlebar && resizedHorizontally) {
		int r, k, fits;

		/* Check if the titlebar is wide enough to hold the buttons.
		 * Temporarily remove them if can't
		 */
		for (r = 0; r < WTB_ROLE_COUNT; r++) {
			if (!fwin->buttons[r])
				continue;
			k = (w_global.config->new_style == TS_NEW)
				? wtb_role_info[r].dontfit_k_new
				: wtb_role_info[r].dontfit_k_old;
			fits = (width >= fwin->top_width * k);
			if (!fits && !dontFit(fwin, r)) {
				if (!hideButton(fwin, r))
					XUnmapWindow(dpy, fwin->buttons[r]->window);
				setRoleBit(fwin->flags.button_dont_fit, r, 1);
			} else if (fits && dontFit(fwin, r)) {
				if (!hideButton(fwin, r))
					XMapWindow(dpy, fwin->buttons[r]->window);
				setRoleBit(fwin->flags.button_dont_fit, r, 0);
			}
		}

		updateTitlebar(fwin);
		checkTitleSize(fwin);
	}

	if (fwin->resizebar) {
		int prev_width = fwin->resizebar->width;

		wCoreConfigure(fwin->resizebar, 0,
			       fwin->core->height - fwin->resizebar->height,
			       fwin->core->width, fwin->resizebar->height);

		fwin->resizebar_corner_width = RESIZEBAR_CORNER_WIDTH;
		if (fwin->core->width < RESIZEBAR_CORNER_WIDTH * 2 + RESIZEBAR_MIN_WIDTH)
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
	reconfigure(fwin, x, y, width, height, False);
}

void wFrameWindowResize(WFrameWindow * fwin, int width, int height)
{
	reconfigure(fwin, 0, 0, width, height, True);
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

	if (fwin->titlebar) {
		XClearWindow(dpy, fwin->titlebar->window);

		wFrameWindowPaint(fwin);
	}
	checkTitleSize(fwin);

	return 1;
}

#ifdef XKB_BUTTON_HINT

static int wFrameWindowSetLanguageButtonImages(WFrameWindow *fwin, Pixmap *pixmaps, int count)
{
	int i;
	for (i = 0; i < count; i++) {
		WPixmap *wp = wPixmapCreate(pixmaps[i], None);
		if (!wp) {
			int j;
			XFreePixmap(dpy, pixmaps[i]);
			for (j = i + 1; j < count; j++)
				XFreePixmap(dpy, pixmaps[j]);
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

void wFrameWindowUpdateLanguageButton(WFrameWindow * fwin)
{
	WScreen *scr = fwin->screen_ptr;
	WCoreWindow *button = fwin->buttons[WTB_LANGUAGE];
	GC gc = scr->copy_gc;
	int i, text_width, text_height;
	int border_thickness = 3;
	int key_width, key_height;
	int group_index;
	Pixmap tmp[2];		/* focused, unfocused */

	if (!fwin->flags.titlebar || fwin->core->descriptor.parent_type != WCLASS_WINDOW)
		return;

	if (!button || fwin->language_label[0] == '\0')
		return;

	group_index = fwin->languagemode;
	if (group_index < 0 || group_index >= 4)
		return;

	/* Calculate text dimensions */
	int small_px = WMFontHeight(*fwin->font) * 55 / 100;
	WMFont *small = WMBoldSystemFontOfSize(scr->wmscreen, small_px);
	text_width = WMWidthOfString(small, fwin->language_label, strlen(fwin->language_label));
	text_height = WMFontHeight(small);

	key_width = button->width - border_thickness;
	key_height = button->height - border_thickness;

	/* Ensure dimensions are valid */
	if (key_width < 1 || key_height < 1) {
		WMReleaseFont(small);
		return;
	}

	/* Create temporary pixmaps for immediate drawing */
	tmp[0] = XCreatePixmap(dpy, button->window, 2*key_width, key_height, scr->w_depth);
	if (tmp[0] == None) {
		WMReleaseFont(small);
		return;
	}

	tmp[1] = None;
	if (w_global.config->new_style == TS_NEW) {
		tmp[1] = XCreatePixmap(dpy, button->window, 2*key_width, key_height, scr->w_depth);
		if (tmp[1] == None) {
			XFreePixmap(dpy, tmp[0]);
			WMReleaseFont(small);
			return;
		}
	}

	/* Reset GC to ensure clean state for drawing */
	XSetClipMask(dpy, gc, None);

	/* Draw the language label centered in the button */
	int text_x = (key_width - text_width) / 2;
	int text_y = (key_height - text_height) / 2;

	/* Fill the color pixmap depending on the style  */
	if (w_global.config->new_style == TS_NEW) {
		for (i = 0; i < 2; i++) {
			if (fwin->title_texture[i]->any.type != WTEX_SOLID && fwin->title_back[i] != None) {
				XCopyArea(dpy, fwin->button_back[WTB_LANGUAGE][i], tmp[i], scr->copy_gc,
						  0, 0, button->width, button->height, -1, -1);
			} else {
				unsigned long bg_pixel = fwin->title_texture[i]->solid.normal.pixel;
				XSetForeground(dpy, gc, bg_pixel);
				XFillRectangle(dpy, tmp[i], gc, 0, 0, key_width, key_height);
			}
			WMDrawString(scr->wmscreen, tmp[i], fwin->title_color[i],
						 small, text_x, text_y, fwin->language_label, strlen(fwin->language_label));
		}
	} else if (w_global.config->new_style == TS_OLD) {
		unsigned long bg_pixel = scr->widget_texture->normal.pixel;
		XSetForeground(dpy, gc, bg_pixel);
		XFillRectangle(dpy, tmp[0], gc, 0, 0, key_width, key_height);
		WMDrawString(scr->wmscreen, tmp[0], WMBlackColor(scr->wmscreen),
					 small, text_x, text_y, fwin->language_label, strlen(fwin->language_label));
	} else {
		unsigned long bg_pixel = scr->widget_texture->dark.pixel;
		XSetForeground(dpy, gc, bg_pixel);
		XFillRectangle(dpy, tmp[0], gc, 0, 0, key_width, key_height);
		WMColor *silver = WMCreateRGBColor(scr->wmscreen, 0xc0c0, 0xc0c0, 0xc0c0, True);
		WMDrawString(scr->wmscreen, tmp[0], silver,
					 small, text_x, text_y, fwin->language_label, strlen(fwin->language_label));
		WMReleaseColor(silver);
	}

	/* pushed button next to normal for easy access when painting */
	text_x = key_width + (key_width - text_width) / 2;
	if (w_global.config->new_style == TS_NEW) {
		XSetForeground(dpy, gc, scr->white_pixel);
		for (i = 0; i < 2; i++) {
			XFillRectangle(dpy, tmp[i], gc, key_width, 0, key_width, key_height);
			WMDrawString(scr->wmscreen, tmp[i], WMBlackColor(scr->wmscreen),
						 small, text_x, text_y, fwin->language_label, strlen(fwin->language_label));
		}
	} else if (w_global.config->new_style == TS_OLD) {
		XSetForeground(dpy, gc, scr->white_pixel);
		XFillRectangle(dpy, tmp[0], gc, key_width, 0, key_width, key_height);
		WMDrawString(scr->wmscreen, tmp[0], WMDarkGrayColor(scr->wmscreen),
			small, text_x, text_y, fwin->language_label, strlen(fwin->language_label));
	} else {
		unsigned long bg_pixel = scr->widget_texture->dark.pixel;
		WMColor *silver = WMCreateRGBColor(scr->wmscreen, 0xc0c0, 0xc0c0, 0xc0c0, True);
		XSetForeground(dpy, gc, bg_pixel);
		XFillRectangle(dpy, tmp[0], gc, key_width, 0, key_width, key_height);
		WMDrawString(scr->wmscreen, tmp[0], silver,
					 small, text_x, text_y, fwin->language_label, strlen(fwin->language_label));
		WMReleaseColor(silver);
	}

	WMReleaseFont(small);

	wFrameWindowSetLanguageButtonImages(fwin, tmp, (w_global.config->new_style == TS_NEW) ? 2 : 1);
}
#endif				/* XKB_BUTTON_HINT */

/*********************************************************************/

static void handleExpose(WObjDescriptor * desc, XEvent * event)
{
	WFrameWindow *fwin = (WFrameWindow *) desc->parent;

	if (fwin->titlebar && fwin->titlebar->window == event->xexpose.window)
		fwin->flags.repaint_only_titlebar = 1;
	if (fwin->resizebar && fwin->resizebar->window == event->xexpose.window)
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

	if (w_global.config->new_style != TS_NEW) {
		for (r = 0; r < WTB_ROLE_COUNT; r++)
			if (buttonVisible(fwin, r))
				width -= fwin->buttons[r]->width + 3;
	}

	if (WMWidthOfString(*fwin->font, fwin->title, strlen(fwin->title)) > width)
		fwin->flags.incomplete_title = 1;
	else
		fwin->flags.incomplete_title = 0;
}

static void paintButton(WCoreWindow * button, WTexture * texture, unsigned long color, WPixmap * image, int pushed, int from_xpm)
{
	WScreen *scr = button->screen_ptr;
	GC copy_gc = scr->copy_gc;
	int x = 0, y = 0, d = 0;
	int left = 0, width = 0;

	/* setup stuff according to the state */
	if (pushed) {
		if (image) {
			if (image->width >= image->height * 2) {
				/* the image contains 2 pictures: the second is for the
				 * pushed state */
				width = image->width / 2;
				left = image->width / 2;
			} else {
				width = image->width;
			}
		}
		XSetClipMask(dpy, copy_gc, None);
		if (w_global.config->new_style == TS_NEXT)
			XSetForeground(dpy, copy_gc, scr->black_pixel);
		else
			XSetForeground(dpy, copy_gc, scr->white_pixel);

		d = 1;
		if (w_global.config->new_style == TS_NEW) {
			XFillRectangle(dpy, button->window, copy_gc, 0, 0, button->width - 1, button->height - 1);
			XSetForeground(dpy, copy_gc, scr->black_pixel);
			XDrawRectangle(dpy, button->window, copy_gc, 0, 0, button->width - 1, button->height - 1);
		} else if (w_global.config->new_style == TS_OLD) {
			XFillRectangle(dpy, button->window, copy_gc, 0, 0, button->width, button->height);
			XSetForeground(dpy, copy_gc, scr->black_pixel);
			XDrawRectangle(dpy, button->window, copy_gc, 0, 0, button->width, button->height);
		} else {
			XFillRectangle(dpy, button->window, copy_gc, 0, 0, button->width-3, button->height-3);
			XSetForeground(dpy, copy_gc, scr->black_pixel);
			XDrawRectangle(dpy, button->window, copy_gc, 0, 0, button->width-3, button->height-3);
		}
	} else {
		XClearWindow(dpy, button->window);

		if (image) {
			if (image->width >= image->height * 2)
				width = image->width / 2;
			else
				width = image->width;
		}
		d = 0;

		if (w_global.config->new_style == TS_NEW) {
			if (texture->any.type == WTEX_SOLID || pushed)
				wDrawBevel(button->window, button->width, button->height,
					   (WTexSolid *) texture, WREL_RAISED);
		} else {
			wDrawBevel(button->window, button->width, button->height,
				   scr->widget_texture, WREL_RAISED);
		}
	}

	if (image) {
		/* display image */
		XSetClipMask(dpy, copy_gc, image->mask);
		x = (button->width - width) / 2 + d;
		y = (button->height - image->height) / 2 + d;
		XSetClipOrigin(dpy, copy_gc, x - left, y);
		if (w_global.config->new_style != TS_NEW) {
			XSetForeground(dpy, copy_gc, scr->black_pixel);
			if (!pushed) {
				if (image->depth == 1)
					XCopyPlane(dpy, image->image, button->window, copy_gc,
						   left, 0, width, image->height, x, y, 1);
				else
					XCopyArea(dpy, image->image, button->window, copy_gc,
						  left, 0, width, image->height, x, y);
			} else {
				if (w_global.config->new_style == TS_OLD) {
					XSetForeground(dpy, copy_gc, scr->dark_pixel);
					if (from_xpm)
						XFillRectangle(dpy, button->window, copy_gc, 0, 0,
								button->width, button->height);
					else
						XCopyArea(dpy, image->image, button->window, copy_gc,
							left, 0, width, image->height, x, y);
				} else {
					XSetForeground(dpy, copy_gc, scr->black_pixel);
					XCopyArea(dpy, image->image, button->window, copy_gc,
						  left, 0, width, image->height, x, y);
				}
			}
		} else {
			if (pushed) {
				XSetForeground(dpy, copy_gc, scr->black_pixel);
			} else {
				XSetForeground(dpy, copy_gc, color);
				XSetBackground(dpy, copy_gc, texture->any.color.pixel);
			}
			if (from_xpm)
				XFillRectangle(dpy, button->window, copy_gc, 0, 0, button->width, button->height);
			else
				XCopyArea(dpy, image->image, button->window, copy_gc,
					left, 0, width, image->height, x, y);
		}
	}
}

static void handleButtonExpose(WObjDescriptor * desc, XEvent * event)
{
	WFrameWindow *fwin = (WFrameWindow *) desc->parent;
	WCoreWindow *button = (WCoreWindow *) desc->self;
	int r;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) event;

	for (r = 0; r < WTB_ROLE_COUNT; r++)
		if (button == fwin->buttons[r])
			break;
	if (r >= WTB_ROLE_COUNT)
		return;

#ifdef XKB_BUTTON_HINT
	if (r == WTB_LANGUAGE) {
		if (!hideButton(fwin, WTB_LANGUAGE)) {
			/* map focused and pfocused states to focus language button image */
			int lb_index = w_global.config->new_style == TS_NEW && (fwin->flags.state == 1) ? 1 : 0;

			paintButton(button, fwin->title_texture[fwin->flags.state],
				    WMColorPixel(fwin->title_color[fwin->flags.state]),
				    fwin->languagebutton_image[lb_index],
				    False, False);
		}
		return;
	}
#endif
	paintButton(button, fwin->title_texture[fwin->flags.state],
		    WMColorPixel(fwin->title_color[fwin->flags.state]),
		    fwin->button_image[r], False, wtb_role_info[r].from_xpm);
}

static void titlebarMouseDown(WObjDescriptor * desc, XEvent * event)
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

static void resizebarMouseDown(WObjDescriptor * desc, XEvent * event)
{
	WFrameWindow *fwin = desc->parent;
	WCoreWindow *resizebar = desc->self;

	if (fwin->on_mousedown_resizebar)
		(*fwin->on_mousedown_resizebar) (resizebar, fwin->child, event);
}

static void buttonMouseDown(WObjDescriptor * desc, XEvent * event)
{
	WFrameWindow *fwin = desc->parent;
	WCoreWindow *button = desc->self;
	WPixmap *image;
	XEvent ev;
	int r, done = 0, execute = 1, from_xpm;
	WTexture *texture;
	unsigned long pixel;
	int clickButton = event->xbutton.button;

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
		int lb_index = w_global.config->new_style == TS_NEW && (fwin->flags.state == 1) ? 1 : 0;

		image = fwin->languagebutton_image[lb_index];
	}
#endif

	pixel = WMColorPixel(fwin->title_color[fwin->flags.state]);
	texture = fwin->title_texture[fwin->flags.state];
	paintButton(button, texture, pixel, image, True, from_xpm);

	while (!done) {
		WMMaskEvent(dpy, LeaveWindowMask | EnterWindowMask | ButtonReleaseMask
			    | ButtonPressMask | ExposureMask, &ev);
		switch (ev.type) {
		case LeaveNotify:
			execute = 0;
			paintButton(button, texture, pixel, image, False, from_xpm);
			break;

		case EnterNotify:
			execute = 1;
			paintButton(button, texture, pixel, image, True, from_xpm);
			break;

		case ButtonPress:
			break;

		case ButtonRelease:
			if (ev.xbutton.button == clickButton)
				done = 1;
			break;

		default:
			WMHandleEvent(&ev);
		}
	}
	paintButton(button, texture, pixel, image, False, from_xpm);

	if (execute && fwin->on_click_button[r])
		(*fwin->on_click_button[r]) (button, fwin->child, &ev);
}
