/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2004 Alfredo K. Kojima
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

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "WindowMaker.h"
#include "screen.h"
#include "framewin.h"
#include "icon.h"
#include "window.h"
#include "defaults.h"
#include "geometry.h"
#include "headresolve.h"
#include "switchpanel.h"
#include "swp_label.h"
#include "swp_size.h"
#include "tabgroup.h"
#include "misc.h"
#include "xinerama.h"
#include "workspace.h"

#include <WINGs/WXProp.h>

#ifdef USE_XSHAPE
#include <xcb/shape.h>
#endif

struct SwitchPanel {
	WScreen *scr;
	WMWindow *win;
	WMFrame *iconViewport;
	WMFrame *iconBox;

	WMArray *icons;
	WMArray *images;
	WMArray *windows;
	WMArray *flags;
	RImage *bg;
	int current;
	int firstVisible;
	int visibleCount;
	int extra_h;
	CycleMode mode;

	/* sub-strip (drilled group) state */
	WMFrame *subViewport;
	WMFrame *subBox;
	WMArray *subIcons;
	WMArray *subImages;
	WMArray *subFlags;
	WMArray *subWindows;
	int subCurrent;
	int subFirstVisible;
	int subVisibleCount;
	int subSlotCount;
	bool drilled;
	bool drillPreview;
	int subHeightExtra;

	WMLabel *label;

	RImage *tileTmp;
	RImage *tile;

	/* creation-time copies of the screen images, so a mid-cycle
	 * reconfigure cannot pull them away from an open panel */
	RImage *back_pieces[9];
	bool snap_tile_image;
	WSwitchPanelBudget budget;

	WMFont *font;
	WMColor *white;
	WMColor *gray;

	/* placement snapshot for the whole cycle */
	WMRect target_rect;
	int wd_pos;
	int dev_pad;
	bool opt_in_fit;
};

/* these values will be updated whenever the switch panel
 * is created to to match the size defined in switch_panel_icon_size
 * and the selected font size */
static int icon_size;
static int border_space;
static int icon_tile_size;
static int label_height;

#define ICON_SELECTED (1<<1)
#define ICON_DIM (1<<2)

static WMArray **icons_of(WSwitchPanel *p, Strip s)      { return s == STRIP_SUB ? &p->subIcons       : &p->icons; }
static WMArray **images_of(WSwitchPanel *p, Strip s)     { return s == STRIP_SUB ? &p->subImages      : &p->images; }
static WMArray **flags_of(WSwitchPanel *p, Strip s)      { return s == STRIP_SUB ? &p->subFlags        : &p->flags; }
static WMArray **windows_of(WSwitchPanel *p, Strip s)    { return s == STRIP_SUB ? &p->subWindows      : &p->windows; }
static int *current_of(WSwitchPanel *p, Strip s)         { return s == STRIP_SUB ? &p->subCurrent       : &p->current; }
static int *firstVisible_of(WSwitchPanel *p, Strip s)    { return s == STRIP_SUB ? &p->subFirstVisible  : &p->firstVisible; }
static int *visibleCount_of(WSwitchPanel *p, Strip s)    { return s == STRIP_SUB ? &p->subVisibleCount  : &p->visibleCount; }
static WMFrame *iconbox_of(WSwitchPanel *p, Strip s)     { return s == STRIP_SUB ? p->subBox             : p->iconBox; }

/* logical to device pixels, WINGs VDEV rounding */
static int to_device(WScreen *scr, int logical)
{
	int dev;

	if (!wSwitchPanelDevicePx(logical, WMScreenUIScale(scr->wmscreen), &dev))
		return 0;
	return dev;
}

/* one diagnostic per distinct unresolved monitor value */
static void warn_unresolved_monitor(const char *value)
{
	static char *last_warned;

	if (last_warned && strcmp(last_warned, value) == 0)
		return;
	wfree(last_warned);
	last_warned = wstrdup(value);
	wwarning(_("switch panel: monitor \"%s\" matches no output, using the pointer head"), value);
}

/* head of the window focused when the cycle starts, -1 without an
 * eligible frame. Read once, before cycling changes focus. */
static int head_for_focused_window(WScreen *scr)
{
	WMRect heads[WM_MAX_HEADS];
	WWindow *wwin = scr->focused_window;
	WMRect frame;
	int i, count;

	if (!wwin || !wwin->flags.focused || !wwin->frame || !wwin->frame->core)
		return -1;
	if (!wwin->flags.mapped && !wwin->flags.shaded)
		return -1;

	frame.pos.x = wwin->frame_x;
	frame.pos.y = wwin->frame_y;
	frame.size.width = wwin->frame->core->width;
	frame.size.height = wwin->frame->core->height;

	count = wHeadCount(scr);
	if (count > WM_MAX_HEADS)
		count = WM_MAX_HEADS;
	for (i = 0; i < count; i++)
		heads[i] = wGetRectForHead(scr, i);

	return wHeadForFrameRect(heads, count, frame);
}

/* resolve the monitor once per panel, the effective selection also
 * deciding the default versus opt-in fit */
static WMRect resolve_target_rect(WScreen *scr, bool *opt_in_fit)
{
	const char *monitor = w_global.config->switch_panel_monitor;
	WHeadSelection sel;
	int head = -1;

	*opt_in_fit = w_global.config->switch_panel_placement != WD_CENTER;

	sel = wHeadSelectFromNames((const char *const *)scr->head_output_name,
				   wHeadCount(scr), monitor);
	switch (sel.kind) {
	case WHEAD_SEL_HEAD:
		head = sel.head;
		*opt_in_fit = true;
		break;
	case WHEAD_SEL_WINDOW:
		head = head_for_focused_window(scr);
		if (head >= 0)
			*opt_in_fit = true;
		break;
	case WHEAD_SEL_UNRESOLVED:
		warn_unresolved_monitor(monitor);
		break;
	case WHEAD_SEL_POINTER:
	default:
		break;
	}

	if (head < 0 || head >= wXineramaHeads(scr))
		head = wGetHeadForPointerLocation(scr);
	if (head < 0 || head >= wXineramaHeads(scr))
		head = scr->xine_info.primary_head;

	return wGetRectForHead(scr, head);
}

/* re-anchor against the stored target rectangle, never a fresh head
 * lookup. A no-op for the panel-less result. */
static void place_panel(WSwitchPanel *panel)
{
	WMPoint p;
	int w, h;

	if (!panel->win)
		return;
	if (!wSwitchPanelDevicePx(WMWidgetWidth(panel->win),
				  WMScreenUIScale(panel->scr->wmscreen), &w))
		return;
	if (!wSwitchPanelDevicePx(WMWidgetHeight(panel->win),
				  WMScreenUIScale(panel->scr->wmscreen), &h))
		return;

	p = wGetPointToPlaceRect(panel->target_rect, panel->wd_pos, w, h, panel->dev_pad);
	p = wClampPointToArea(panel->target_rect, p.x, p.y, w, h);
	WMMoveWidget(panel->win, p.x, p.y);
}

static int canReceiveFocus(WWindow *wwin)
{
	if (!w_global.config->cycle_all_workspaces && !wWindowIsVisibleOnAnyHead(wwin))
		return 0;

	if (w_global.config->cycle_active_head_only &&
	    wGetHeadForWindow(wwin) != wGetHeadForPointerLocation(wwin->screen_ptr))
		return 0;

	if (WFLAGP(wwin, no_focusable))
		return 0;

	if (!wwin->flags.mapped) {
		if (wGroupIsInactiveMember(wwin))
			return -1;
		if (!wwin->flags.shaded && !wwin->flags.miniaturized && !wwin->flags.hidden && !w_global.config->cycle_all_workspaces)
			return 0;
		else
			return -1;
	}

	return 1;
}

static bool sameWindowClass(WWindow *wwin, WWindow *curwin)
{
	if (!wwin->wm_class || !curwin->wm_class)
		return false;
	if (strcmp(wwin->wm_class, curwin->wm_class))
		return false;

	return true;
}

static bool modalReachableThroughParent(WWindow *ref, WWindow *wwin, int include_unmapped, CycleMode mode)
{
	WWindow *parent;

	if (!wwin->flags.net_modal)
		return false;
	if (wwin->transient_for == XCB_NONE || wwin->transient_for == ref->screen_ptr->root_win)
		return false;

	parent = wWindowFor(wwin->transient_for);
	if (!parent || parent == wwin)
		return false;

	/* Keep this predicate in sync with makeWindowListArray()'s list filter. */
	if (WFLAGP(parent, skip_switchpanel))
		return false;
	if (canReceiveFocus(parent) == 0)
		return false;
	if (!(parent->flags.mapped || parent->flags.shaded || include_unmapped))
		return false;
	/* A modal belongs to its parent's group for both non-flat modes. */
	if (mode != CYCLE_FLAT && !sameWindowClass(ref, parent))
		return false;

	return true;
}

static bool classAlreadyInArray(WMArray *windows, WWindow *wwin)
{
	int j;
	WWindow *rep;

	WM_ITERATE_ARRAY(windows, rep, j) {
		if (sameWindowClass(rep, wwin))
			return true;
	}
	return false;
}

static int compareCreationOrder(const void *a, const void *b)
{
	const WWindow *wa = *(WWindow *const *)a;
	const WWindow *wb = *(WWindow *const *)b;

	if (wa->creation_id < wb->creation_id)
		return -1;
	if (wa->creation_id > wb->creation_id)
		return 1;
	return 0;
}

static WMArray *makeWindowListArray(WWindow *ref, int include_unmapped, CycleMode mode)
{
	WMArray *candidates = WMCreateArray(10);
	WMArray *windows = WMCreateArray(10);
	WWindow *wwin = ref;
	int i;

	while (wwin) {
		if ((canReceiveFocus(wwin) != 0) &&
		    (wwin->flags.mapped || wwin->flags.shaded || include_unmapped)) {
			if (!w_global.config->show_all_tabs_in_switch_panel
			    && wGroupIsInactiveMember(wwin)) {
				wwin = wwin->prev;
				continue;
			}
			/* A direct modal transient is redundant only when its
			 * parent would be listed and can redirect focus to it. */
			if (!WFLAGP(wwin, skip_switchpanel) &&
			    !modalReachableThroughParent(ref, wwin, include_unmapped, mode))
				WMAddToArray(candidates, wwin);
		}
		wwin = wwin->prev;
	}

	if (w_global.config->switcher_order == SWO_AGE)
		WMSortArray(candidates, compareCreationOrder);

	WM_ITERATE_ARRAY(candidates, wwin, i) {
		if (mode == CYCLE_CLASS_ONLY && !sameWindowClass(ref, wwin))
			continue;
		if (mode == CYCLE_GROUP && classAlreadyInArray(windows, wwin))
			continue;
		WMAddToArray(windows, wwin);
	}
	WMFreeArray(candidates);
	return windows;
}

static WMArray *makeWindowFlagsArray(int count)
{
	WMArray *flags = WMCreateArray(count);
	int i;

	for (i = 0; i < count; i++)
		WMAddToArray(flags, (void *) 0);

	return flags;
}

/* the sub-strip list uses the panel's creation-time image state */
static bool isGroupDrillable(WSwitchPanel *panel, WWindow *rep)
{
	WMArray *sub;
	int n;

	if (!rep)
		return false;
	sub = makeWindowListArray(rep, panel->snap_tile_image, CYCLE_CLASS_ONLY);
	n = WMGetArrayItemCount(sub);
	WMFreeArray(sub);
	return n > 1;
}

/* Solid white down-pointing triangle with a black outline at the
 * bottom edge of the selected tile, in the style of the drawer icon
 * arrow, marking a group whose windows the sub-strip can show. */
static void draw_drill_arrow(RImage *img)
{
	RColor white, dark;
	int w = img->width / 6;
	int h = w / 2;
	int cx = img->width / 2;
	int y0 = img->height - 2 - h;
	int i;

	if (w < 4)
		return;

	white.red = 255;
	white.green = 255;
	white.blue = 255;
	white.alpha = 255;
	dark.red = 0;
	dark.green = 0;
	dark.blue = 0;
	dark.alpha = 255;

	for (i = -1; i <= h; i++) {
		int half = w * (h - i) / (2 * h) + 1;

		RDrawLine(img, cx - half, y0 + i, cx + half, y0 + i, &dark);
	}
	for (i = 0; i < h; i++) {
		int half = w * (h - i) / (2 * h);

		RDrawLine(img, cx - half, y0 + i, cx + half, y0 + i, &white);
	}
}

static bool changeImage(WSwitchPanel *panel, int idecks, int selected, bool dim, bool force, Strip strip)
{
	WMArray *winArr = *windows_of(panel, strip);
	WMArray *iconArr = *icons_of(panel, strip);
	WMArray *imageArr = *images_of(panel, strip);
	WMArray *flagArr = *flags_of(panel, strip);
	int firstVisible = *firstVisible_of(panel, strip);
	WMFrame *icon = NULL;
	RImage *image = NULL;
	int flags;
	int desired = 0;
	bool arrow = false;

	/* a sizing failure returns a panel-less object with the images
	 * still loaded, so the window is the real precondition */
	if (!panel->win)
		return true;

	icon = WMGetFromArray(iconArr, idecks);
	image = WMGetFromArray(imageArr, idecks);
	flags = (int) (uintptr_t) WMGetFromArray(flagArr, idecks);

	if (selected)
		desired |= ICON_SELECTED;
	if (dim)
		desired |= ICON_DIM;

	if (flags == desired && !force)
		return true;

	if (!panel->bg && !panel->tile && !selected)
		WMSetFrameRelief(icon, WRFlat);

	if (image && icon) {
		RImage *back;
		RImage *out;
		int opaq = (dim) ? 75 : 255;
		WMPoint pos;
		int bg_y;
		xcb_pixmap_t p;

		if (!panel->tileTmp) {
			wwarning(_("switch panel: missing temporary tile"));
			return false;
		}
		if (canReceiveFocus(WMGetFromArray(winArr, idecks)) < 0)
			opaq = 50;

		pos = WMGetViewPosition(WMWidgetView(icon));
		back = panel->tileTmp;
		if (panel->bg) {
			/* the sub viewport sits one tile height below the
			 * main strip inside the panel window */
			bg_y = border_space + pos.y;
			if (strip == STRIP_SUB)
				bg_y += border_space + icon_tile_size;
			RCopyArea(back, panel->bg,
				  border_space + pos.x - firstVisible * icon_tile_size,
				  bg_y, back->width, back->height, 0, 0);
		} else {
			RColor color;
			color.red = WMRedComponentOfColor(panel->gray) >> 8;
			color.green = WMGreenComponentOfColor(panel->gray) >> 8;
			color.blue = WMBlueComponentOfColor(panel->gray) >> 8;
			color.alpha = 255;
			RFillImage(back, &color);
		}

		if (selected && panel->tile) {
			RCombineArea(back, panel->tile, 0, 0, panel->tile->width, panel->tile->height,
				     (back->width - panel->tile->width) / 2,
				     (back->height - panel->tile->height) / 2);
			arrow = (strip == STRIP_MAIN && panel->mode == CYCLE_GROUP &&
				 !panel->drilled &&
				 isGroupDrillable(panel, WMGetFromArray(winArr, idecks)));
		}

		RCombineAreaWithOpaqueness(back, image, 0, 0, image->width, image->height,
					   (back->width - image->width) / 2, (back->height - image->height) / 2,
					   opaq);

		if (arrow)
			draw_drill_arrow(back);

		out = back;
		if (panel->scr->ui_scale > 1.0001) {
			int dev = (int)(icon_tile_size * panel->scr->ui_scale + 0.5);

			out = RScaleImage(back, dev, dev);
			if (!out) {
				wwarning(_("switch panel: cannot scale an icon tile"));
				return false;
			}
		}
		if (!RConvertImage(panel->scr->rcontext, out, &p)) {
			wwarning(_("switch panel: cannot convert an icon tile"));
			if (out != back)
				RReleaseImage(out);
			return false;
		}
		xcb_change_window_attributes(wdisp->conn, WMWidgetXID(icon), XCB_CW_BACK_PIXMAP, (const uint32_t[]){ p });
		xcb_clear_area(wdisp->conn, 0, WMWidgetXID(icon), 0, 0, 0, 0);
		xcb_free_pixmap(wdisp->conn, p);
		if (out != back)
			RReleaseImage(out);
	}

	if (!panel->bg && !panel->tile && selected)
		WMSetFrameRelief(icon, WRSimple);
	WMReplaceInArray(flagArr, idecks, (void *) (uintptr_t) desired);
	return true;
}

/* NULL with *ok = false on failure, NULL with *ok = true when the window
 * has no icon source at all */
static RImage *tile_image_for_window(WSwitchPanel *panel, WWindow *wwin, bool *ok)
{
	RImage *image = NULL;
	WApplication *wapp;
	RImage *original;
	char *path;
	long long available;

	*ok = true;

	/* Prefer the per-window _NET_WM_ICON so windows that share a
	 * group leader but ship distinct icons (a browser and its addon
	 * windows for instance) each show their own image, then fall
	 * back to the shared application icon so a client with no
	 * _NET_WM_ICON still picks up whatever the appicon derived from
	 * WM_HINTS or the user icon database. */
	if (!WFLAGP(wwin, always_user_icon) && wwin->net_icon_image)
		image = RRetainImage(wwin->net_icon_image);

	wapp = wApplicationOf(wwin->main_window);
	if (!image && !WFLAGP(wwin, always_user_icon) && wapp && wapp->app_icon &&
			wapp->app_icon->icon && wapp->app_icon->icon->file_image)
		image = RRetainImage(wapp->app_icon->icon->file_image);

	/* get_icon_image() falls back to the default icon image. The
	 * final size is requested directly so an SVG decodes sharp */
	if (!image) {
		path = get_icon_filename(wwin->wm_instance, wwin->wm_class, NULL, true);
		if (path) {
			available = (WSP_PEAK_LIMIT - panel->budget.live) / 2;
			if (available > WSP_ALLOC_LIMIT)
				available = WSP_ALLOC_LIMIT;
			if (available > 0)
				image = RLoadImageAtSizeLimited(panel->scr->rcontext, path, 0,
							icon_size, icon_size, (size_t)available);
			if (!image) {
				wwarning(_("switch panel: cannot load icon \"%s\" within its resource budget"), path);
				wfree(path);
				*ok = false;
				return NULL;
			}
			wfree(path);
		}
	}

	/* _NET_WM_ICON and appicon sources can exceed the tile */
	original = image;
	image = wIconValidateIconSizeStrict(image, icon_size);
	if (original && !image) {
		wwarning(_("switch panel: cannot scale an icon"));
		RReleaseImage(original);
		*ok = false;
		return NULL;
	}

	/* the validator can keep an image larger than the tile at small
	 * sizes, fit it into the tile with positive dimensions */
	if (image) {
		int fit_w, fit_h;

		if (wSwitchPanelIconDims(image->width, image->height, icon_tile_size,
					&fit_w, &fit_h) &&
		    (fit_w != image->width || fit_h != image->height)) {
			RImage *fitted = RScaleImage(image, fit_w, fit_h);

			if (!fitted) {
				wwarning(_("switch panel: cannot scale an icon"));
				RReleaseImage(image);
				*ok = false;
				return NULL;
			}
			RReleaseImage(image);
			image = fitted;
		}
	}

	return image;
}

static bool addIconForWindow(WSwitchPanel *panel, WMWidget *parent, WWindow *wwin, int x, int y, Strip strip)
{
	WMFrame *icon = WMCreateFrame(parent);
	RImage *image;
	bool ok;

	WMSetFrameRelief(icon, WRFlat);
	WMResizeWidget(icon, icon_tile_size, icon_tile_size);
	WMMoveWidget(icon, x, y);

	image = tile_image_for_window(panel, wwin, &ok);
	if (!ok) {
		WMDestroyWidget(icon);
		return false;
	}

	WMAddToArray(*images_of(panel, strip), image);
	WMAddToArray(*icons_of(panel, strip), icon);
	return true;
}

static void scrollIcons(WSwitchPanel *panel, int delta, Strip strip)
{
	WMArray *winArr = *windows_of(panel, strip);
	WMArray *flagArr = *flags_of(panel, strip);
	WMFrame *iconBox = iconbox_of(panel, strip);
	int *firstVisible = firstVisible_of(panel, strip);
	int *current = current_of(panel, strip);
	int visibleCount = *visibleCount_of(panel, strip);
	int nfirst = *firstVisible + delta;
	int i;
	int count = WMGetArrayItemCount(winArr);
	bool dim;

	if (!iconBox || count <= visibleCount)
		return;

	if (nfirst < 0)
		nfirst = 0;
	else if (nfirst >= count - visibleCount)
		nfirst = count - visibleCount;

	if (nfirst == *firstVisible)
		return;

	WMMoveWidget(iconBox, -nfirst * icon_tile_size, 0);

	*firstVisible = nfirst;

	for (i = *firstVisible; i < *firstVisible + visibleCount; i++) {
		if (i == *current)
			continue;
		dim = ((int) (uintptr_t) WMGetFromArray(flagArr, i) & ICON_DIM);
		changeImage(panel, i, 0, dim, true, strip);
	}
}

/*
 *  0 1 2
 *  3 4 5
 *  6 7 8
 */
static RImage *assemblePuzzleImage(RImage **images, int width, int height)
{
	RImage *img;
	RImage *tmp;
	int tw, th;
	RColor color;

	tw = width - images[0]->width - images[2]->width;
	th = height - images[0]->height - images[6]->height;

	if (tw <= 0 || th <= 0)
		return NULL;

	img = RCreateImage(width, height, 1);
	if (!img)
		return NULL;

	color.red   =   0;
	color.green =   0;
	color.blue  =   0;
	color.alpha = 255;
	RFillImage(img, &color);

	/* top */
	tmp = RSmoothScaleImage(images[1], tw, images[1]->height);
	if (!tmp)
		goto failed;
	RCopyArea(img, tmp, 0, 0, tmp->width, tmp->height, images[0]->width, 0);
	RReleaseImage(tmp);

	/* bottom */
	tmp = RSmoothScaleImage(images[7], tw, images[7]->height);
	if (!tmp)
		goto failed;
	RCopyArea(img, tmp, 0, 0, tmp->width, tmp->height, images[6]->width, height - images[6]->height);
	RReleaseImage(tmp);

	/* left */
	tmp = RSmoothScaleImage(images[3], images[3]->width, th);
	if (!tmp)
		goto failed;
	RCopyArea(img, tmp, 0, 0, tmp->width, tmp->height, 0, images[0]->height);
	RReleaseImage(tmp);

	/* right */
	tmp = RSmoothScaleImage(images[5], images[5]->width, th);
	if (!tmp)
		goto failed;
	RCopyArea(img, tmp, 0, 0, tmp->width, tmp->height, width - images[5]->width, images[2]->height);
	RReleaseImage(tmp);

	/* center */
	tmp = RSmoothScaleImage(images[4], tw, th);
	if (!tmp)
		goto failed;
	RCopyArea(img, tmp, 0, 0, tmp->width, tmp->height, images[0]->width, images[0]->height);
	RReleaseImage(tmp);

	/* corners */
	RCopyArea(img, images[0], 0, 0, images[0]->width, images[0]->height, 0, 0);
	RCopyArea(img, images[2], 0, 0, images[2]->width, images[2]->height, width - images[2]->width, 0);
	RCopyArea(img, images[6], 0, 0, images[6]->width, images[6]->height, 0, height - images[6]->height);
	RCopyArea(img, images[8], 0, 0, images[8]->width, images[8]->height,
		  width - images[8]->width, height - images[8]->height);

	return img;

failed:
	RReleaseImage(img);
	return NULL;
}

/* build a background from the panel's own copy of the pieces */
static RImage *createBackImage(WSwitchPanel *panel, int width, int height)
{
	int i;

	for (i = 0; i < 9; i++) {
		if (!panel->back_pieces[i])
			return NULL;
	}
	return assemblePuzzleImage(panel->back_pieces, width, height);
}

static RImage *getTile(WScreen *scr)
{
	if (!scr->swtileImage)
		return NULL;

	if (scr->swtileScaled && scr->swtileScaledSize == icon_tile_size)
		return RRetainImage(scr->swtileScaled);

	if (scr->swtileScaled)
		RReleaseImage(scr->swtileScaled);
	scr->swtileScaled = RScaleImage(scr->swtileImage, icon_tile_size, icon_tile_size);
	scr->swtileScaledSize = icon_tile_size;
	if (!scr->swtileScaled)
		return NULL;

	return RRetainImage(scr->swtileScaled);
}

static bool prepare_panel_bg(WSwitchPanel *panel, RImage *image,
			     xcb_pixmap_t *pixmap, xcb_pixmap_t *mask)
{
	RImage *bg;

	*pixmap = XCB_NONE;
	*mask = XCB_NONE;
	bg = image;
	if (panel->scr->ui_scale > 1.0001) {
		/* scale from the raster size, the rebuild swaps it
		 * before the widget moves */
		int w = (int)(bg->width * panel->scr->ui_scale + 0.5);
		int h = (int)(bg->height * panel->scr->ui_scale + 0.5);

		bg = RScaleImage(image, w, h);
		if (!bg)
			return false;
	}
	if (!RConvertImageMask(panel->scr->rcontext, bg, pixmap, mask, 250)) {
		if (bg != image)
			RReleaseImage(bg);
		if (*pixmap)
			xcb_free_pixmap(wdisp->conn, *pixmap);
		if (*mask)
			xcb_free_pixmap(wdisp->conn, *mask);
		return false;
	}
	if (bg != image)
		RReleaseImage(bg);
	return true;
}

static void apply_panel_bg(WSwitchPanel *panel, xcb_pixmap_t pixmap, xcb_pixmap_t mask)
{
	xcb_change_window_attributes(wdisp->conn, WMWidgetXID(panel->win), XCB_CW_BACK_PIXMAP, (const uint32_t[]){ pixmap });

#ifdef USE_XSHAPE
	if (mask && wdisp->xext.shape.supported)
		xcb_shape_mask(wdisp->conn, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING,
			       WMWidgetXID(panel->win), 0, 0, mask);
#endif
	if (pixmap)
		xcb_free_pixmap(wdisp->conn, pixmap);

	if (mask)
		xcb_free_pixmap(wdisp->conn, mask);
}

static bool budget_raster(WSwitchPanelBudget *b, int w, int h)
{
	long long bytes;

	return wSwitchPanelRasterBytes(w, h, &bytes) && wSwitchPanelBudgetAdd(b, bytes);
}

/* one decoded icon image plus its backing pixmap */
static bool budget_icon_slot(WSwitchPanelBudget *b, int tile, int dev_tile)
{
	int bound = WMIN(icon_size + WSP_ICON_IMAGE_SLACK, tile);

	return budget_raster(b, bound, bound) && budget_raster(b, dev_tile, dev_tile);
}

static void unbudget_icon_slot(WSwitchPanelBudget *b, int tile, int dev_tile)
{
	long long bytes;
	int bound = WMIN(icon_size + WSP_ICON_IMAGE_SLACK, tile);

	if (wSwitchPanelRasterBytes(bound, bound, &bytes))
		wSwitchPanelBudgetRemove(b, bytes);
	if (wSwitchPanelRasterBytes(dev_tile, dev_tile, &bytes))
		wSwitchPanelBudgetRemove(b, bytes);
}

/* budget the pixel allocations of a new panel before requesting any */
static bool budget_creation(WSwitchPanel *panel, const WSwitchPanelSize *size,
			    int count, bool has_background)
{
	return wSwitchPanelBudgetCreate(&panel->budget, size, count, icon_size, has_background);
}

/* budget the icon slots a drill expansion adds, all or nothing */
static bool budget_strip_slots(WSwitchPanelBudget *b, int count, int tile,
			       int dev_tile)
{
	WSwitchPanelBudget scratch = *b;
	int i;

	for (i = 0; i < count; i++) {
		if (!budget_icon_slot(&scratch, tile, dev_tile))
			return false;
	}
	*b = scratch;
	return true;
}

typedef struct {
	RImage *image;
	xcb_pixmap_t pixmap;
	xcb_pixmap_t mask;
	WSwitchPanelBudget budget;
} PanelBackground;

static void discard_panel_bg(PanelBackground *bg)
{
	if (bg->image)
		RReleaseImage(bg->image);
	if (bg->pixmap)
		xcb_free_pixmap(wdisp->conn, bg->pixmap);
	if (bg->mask)
		xcb_free_pixmap(wdisp->conn, bg->mask);
}

static bool prepare_replacement_bg(WSwitchPanel *panel, int pw, int ph, PanelBackground *bg)
{
	double scale;

	memset(bg, 0, sizeof(*bg));
	if (!panel->bg)
		return true;
	scale = WMScreenUIScale(panel->scr->wmscreen);
	if (ph < 1 || !wSwitchPanelImageSizeOk(pw, ph) ||
	    !wSwitchPanelWidgetSizeOk(pw, ph, scale))
		return false;
	bg->budget = panel->budget;
	if (!wSwitchPanelBudgetRebuild(&bg->budget, panel->bg->width, panel->bg->height,
				       pw, ph, scale))
		return false;

	bg->image = createBackImage(panel, pw, ph);
	if (!bg->image)
		return false;
	if (!prepare_panel_bg(panel, bg->image, &bg->pixmap, &bg->mask)) {
		RReleaseImage(bg->image);
		bg->image = NULL;
		return false;
	}
	return true;
}

static void commit_panel_bg(WSwitchPanel *panel, PanelBackground *bg)
{
	if (!bg->image)
		return;
	panel->budget = bg->budget;
	RReleaseImage(panel->bg);
	panel->bg = bg->image;
	apply_panel_bg(panel, bg->pixmap, bg->mask);
}

static bool replace_panel_bg(WSwitchPanel *panel, int pw, int ph)
{
	PanelBackground bg;

	if (!prepare_replacement_bg(panel, pw, ph, &bg))
		return false;
	commit_panel_bg(panel, &bg);
	return true;
}

static void retain_back_pieces(WSwitchPanel *panel, WScreen *scr)
{
	int i;

	for (i = 0; i < 9; i++) {
		if (scr->swbackImage[i])
			panel->back_pieces[i] = RRetainImage(scr->swbackImage[i]);
	}
}

static void release_back_pieces(WSwitchPanel *panel)
{
	int i;

	for (i = 0; i < 9; i++) {
		if (panel->back_pieces[i]) {
			RReleaseImage(panel->back_pieces[i]);
			panel->back_pieces[i] = NULL;
		}
	}
}

static void drawTitle(WSwitchPanel *panel, int idecks, char *title, Strip strip)
{
	char *ntitle;
	int width = WMWidgetWidth(panel->win);
	int x;
	int firstVisible = *firstVisible_of(panel, strip);

	if (title)
		ntitle = ShrinkString(panel->font, title, width - 2 * border_space);
	else
		ntitle = NULL;

	if (panel->bg) {
		if (ntitle) {
			if (strcmp(ntitle, title) != 0) {
				x = border_space;
			} else {
				int w = WMWidthOfString(panel->font, ntitle, strlen(ntitle));

				x = border_space + (idecks - firstVisible) * icon_tile_size+
				    icon_tile_size/ 2 - w / 2;
				if (x < border_space)
					x = border_space;
				else if (x + w > width - border_space)
					x = width - border_space - w;
			}
		}

		xcb_clear_area(wdisp->conn, 0, WMWidgetXID(panel->win), 0, 0, 0, 0);
		if (ntitle)
			WMDrawString(panel->scr->wmscreen,
				     WMWidgetXID(panel->win),
				     panel->white, panel->font,
				     x,
				     WMWidgetHeight(panel->win) - border_space - label_height +
				     WMFontHeight(panel->font) / 2, ntitle, strlen(ntitle));
	} else {
		WMSetLabelText(panel->label, ntitle ? ntitle : "");
	}

	if (ntitle)
		free(ntitle);
	if (title)
		wfree(title);
}

static void fillLabelFields(WSwitchPanel *panel, WWindow *wwin, WSwitchLabelFields *fields)
{
	int workspace = WWIN_PRIMARY_WS(wwin);

	fields->title = wwin->frame->title;
	fields->wm_class = wwin->wm_class;
	fields->wm_instance = wwin->wm_instance;
	fields->workspace = workspace >= 0 && workspace < panel->scr->workspace_count
		? panel->scr->workspaces[workspace]->name : NULL;
	fields->workspace_count = panel->scr->workspace_count;
	if (wwin->flags.fullscreen) {
		fields->state_long = "Full";
		fields->state_short = "F";
	} else if (wwin->flags.maximized) {
		fields->state_long = "Max";
		fields->state_short = "M";
	} else if (wwin->flags.shaded) {
		fields->state_long = "Shade";
		fields->state_short = "S";
	} else if (wwin->flags.miniaturized) {
		fields->state_long = "Min";
		fields->state_short = "m";
	} else {
		fields->state_long = "-";
		fields->state_short = "-";
	}
}

static char *panelLabelForWindow(WSwitchPanel *panel, WWindow *wwin, Strip strip)
{
	WSwitchLabelFields fields;
	const char *format;
	char *label;
	bool group = panel->mode == CYCLE_GROUP && strip == STRIP_MAIN;

	fillLabelFields(panel, wwin, &fields);
	format = group ? w_global.config->switch_panel_group_label_format
		       : w_global.config->switch_panel_label_format;
	if (!format || !*format)
		format = group ? "%I" : "%T";
	label = wSwitchPanelFormatLabel(format, &fields);
	if (group && label && !*label) {
		wfree(label);
		format = w_global.config->switch_panel_label_format;
		if (!format || !*format)
			format = "%T";
		label = wSwitchPanelFormatLabel(format, &fields);
	}
	return label;
}

static void free_sub_strip(WSwitchPanel *panel)
{
	int i;
	RImage *image;
	WMFrame *icon;

	if (panel->subImages) {
		WM_ITERATE_ARRAY(panel->subImages, image, i) {
			if (image)
				RReleaseImage(image);
		}
		WMFreeArray(panel->subImages);
		panel->subImages = NULL;
	}
	if (panel->subIcons) {
		WM_ITERATE_ARRAY(panel->subIcons, icon, i) {
			if (icon)
				WMDestroyWidget(icon);
		}
		WMFreeArray(panel->subIcons);
		panel->subIcons = NULL;
	}
	WMFreeArray(panel->subFlags); panel->subFlags = NULL;
	WMFreeArray(panel->subWindows); panel->subWindows = NULL;
	panel->subCurrent = 0;
	panel->subFirstVisible = 0;
	panel->subVisibleCount = 0;

	for (i = 0; i < panel->subSlotCount; i++)
		unbudget_icon_slot(&panel->budget, icon_tile_size,
				   to_device(panel->scr, icon_tile_size));
	panel->subSlotCount = 0;
}

static WWindow *populate_sub_strip_new(WSwitchPanel *panel, WWindow *groupRep)
{
	WWindow *wwin;
	int subCount, i;

	free_sub_strip(panel);
	panel->subWindows = makeWindowListArray(groupRep, panel->snap_tile_image, CYCLE_CLASS_ONLY);
	subCount = WMGetArrayItemCount(panel->subWindows);
	if (subCount == 0) {
		WMFreeArray(panel->subWindows);
		panel->subWindows = NULL;
		return NULL;
	}
	if (panel->subBox) {
		int sub_width;
		int dev_tile = to_device(panel->scr, icon_tile_size);

		panel->subIcons = WMCreateArray(subCount);
		panel->subImages = WMCreateArray(subCount);
		panel->subFlags = makeWindowFlagsArray(subCount);
		if (!wSwitchPanelStripWidth(subCount, icon_tile_size,
					   WMScreenUIScale(panel->scr->wmscreen),
					   &sub_width)) {
			wwarning(_("switch panel: sub-strip is out of range, skipping it"));
			free_sub_strip(panel);
			return NULL;
		}
		if (!budget_strip_slots(&panel->budget, subCount, icon_tile_size,
					dev_tile)) {
			wwarning(_("switch panel: sub-strip resources exceed the budget, skipping it"));
			free_sub_strip(panel);
			return NULL;
		}
		panel->subSlotCount = subCount;
		WMResizeWidget(panel->subBox, sub_width, icon_tile_size);
		WM_ITERATE_ARRAY(panel->subWindows, wwin, i) {
			int x;

			if (!wSwitchPanelIconPos(i, icon_tile_size, &x)) {
				wwarning(_("switch panel: sub-strip is out of range, skipping it"));
				free_sub_strip(panel);
				return NULL;
			}
			if (!addIconForWindow(panel, panel->subBox, wwin, x, 0, STRIP_SUB)) {
				free_sub_strip(panel);
				return NULL;
			}
		}
	}
	panel->subVisibleCount = WMIN(subCount, panel->visibleCount);
	panel->subFirstVisible = 0;
	panel->subCurrent = WMGetFirstInArray(panel->subWindows, groupRep);
	if (panel->subCurrent < 0)
		panel->subCurrent = 0;
	return WMGetFromArray(panel->subWindows, panel->subCurrent);
}

static bool render_sub_strip(WSwitchPanel *panel, bool selected)
{
	int i;

	if (!panel->subBox)
		return true;
	if (!WMRealizeWidgetChecked(panel->subViewport) ||
	    !WMRealizeWidgetChecked(panel->subBox))
		return false;
	for (i = 0; i < WMGetArrayItemCount(panel->subWindows); i++) {
		if (!changeImage(panel, i, selected && i == panel->subCurrent,
				 false, true, STRIP_SUB))
			return false;
	}
	return true;
}

static WWindow *populate_sub_strip(WSwitchPanel *panel, WWindow *groupRep)
{
	WSwitchPanel pending = *panel;
	WMFrame *old_box = panel->subBox;
	WWindow *selected;
	int i;

	pending.subWindows = NULL;
	pending.subIcons = NULL;
	pending.subImages = NULL;
	pending.subFlags = NULL;
	pending.subSlotCount = 0;
	if (panel->subViewport) {
		pending.subBox = WMCreateFrame(panel->subViewport);
		WMMoveWidget(pending.subBox, 0, 0);
		WMResizeWidget(pending.subBox, icon_tile_size, icon_tile_size);
		WMSetFrameRelief(pending.subBox, WRFlat);
	}
	selected = populate_sub_strip_new(&pending, groupRep);
	if (!selected ||
	    ((panel->drilled || panel->drillPreview) &&
	     !render_sub_strip(&pending, panel->drilled))) {
		free_sub_strip(&pending);
		if (pending.subBox)
			WMDestroyWidget(pending.subBox);
		return NULL;
	}
	for (i = 0; i < panel->subSlotCount; i++)
		unbudget_icon_slot(&pending.budget, icon_tile_size,
				   to_device(panel->scr, icon_tile_size));
	free_sub_strip(panel);
	if (old_box)
		WMDestroyWidget(old_box);
	panel->subBox = pending.subBox;
	panel->subWindows = pending.subWindows;
	panel->subIcons = pending.subIcons;
	panel->subImages = pending.subImages;
	panel->subFlags = pending.subFlags;
	panel->subCurrent = pending.subCurrent;
	panel->subFirstVisible = pending.subFirstVisible;
	panel->subVisibleCount = pending.subVisibleCount;
	panel->subSlotCount = pending.subSlotCount;
	panel->budget = pending.budget;
	return selected;
}

WWindow *wSwitchPanelUndrill(WSwitchPanel *panel)
{
	WWindow *rep;
	int pw, ph, origHeight;

	if (!panel->drilled && !panel->drillPreview)
		return NULL;
	rep = WMGetFromArray(panel->windows, panel->current);
	if (panel->win) {
		pw = WMWidgetWidth(panel->win);
		ph = WMWidgetHeight(panel->win);
		origHeight = ph - panel->subHeightExtra;
		/* a failed rebuild keeps the panel and its interaction
		 * state as they are */
		if (origHeight >= 1 && replace_panel_bg(panel, pw, origHeight)) {
			WMResizeWidget(panel->win, pw, origHeight);
			if (panel->label)
				WMMoveWidget(panel->label, border_space,
					     border_space + icon_tile_size + 5);
			place_panel(panel);
		} else {
			wwarning(_("switch panel: cannot rebuild the panel background, keeping its size"));
			return NULL;
		}
	}
	if (panel->subViewport)
		WMUnmapWidget(panel->subViewport);
	free_sub_strip(panel);
	panel->drilled = false;
	panel->drillPreview = false;
	if (panel->win && panel->current >= 0)
		changeImage(panel, panel->current, 1, false, true, STRIP_MAIN);
	if (panel->win && rep)
		drawTitle(panel, panel->current, panelLabelForWindow(panel, rep, STRIP_MAIN), STRIP_MAIN);
	return rep;
}

static WWindow *show_sub_strip(WSwitchPanel *panel, bool set_drilled)
{
	WWindow *rep, *firstSub;
	RImage *old_bg;
	PanelBackground bg;
	int pw, ph, newHeight;
	bool grow;

	if (panel->mode != CYCLE_GROUP)
		return NULL;

	rep = WMGetFromArray(panel->windows, panel->current);
	if (!rep)
		return NULL;

	if (!isGroupDrillable(panel, rep)) {
		if (panel->drilled || panel->drillPreview)
			return wSwitchPanelUndrill(panel);
		return rep;
	}
	if (panel->drillPreview && set_drilled) {
		if (!changeImage(panel, panel->subCurrent, 1, false, true, STRIP_SUB))
			return NULL;
		panel->drillPreview = false;
		panel->drilled = true;
		changeImage(panel, panel->current, 1, false, true, STRIP_MAIN);
		return WMGetFromArray(panel->subWindows, panel->subCurrent);
	}

	/* refresh preview in place without re-growing the panel */
	if (panel->drillPreview && !set_drilled) {
		firstSub = populate_sub_strip(panel, rep);
		if (!firstSub)
			return NULL;
		if (panel->subBox && WMGetArrayItemCount(panel->subWindows) > 0) {
			if (panel->subViewport)
				WMRealizeWidget(panel->subViewport);
			WMRealizeWidget(panel->subBox);
			WMMapWidget(panel->subBox);
			WMMapSubwidgets(panel->subBox);
		}
		return WMGetFromArray(panel->windows, panel->current);
	}

	if (!wSwitchPanelDrillExtra(border_space, icon_tile_size,
				    &panel->subHeightExtra)) {
		wwarning(_("switch panel: sub-strip height is out of range, skipping it"));
		return NULL;
	}

	if (panel->win && !panel->subViewport) {
		panel->subViewport = WMCreateFrame(panel->win);
		WMResizeWidget(panel->subViewport, WMWidgetWidth(panel->win) - 2 * border_space, icon_tile_size);
		WMMoveWidget(panel->subViewport, border_space, 2 * border_space + icon_tile_size);
		WMSetFrameRelief(panel->subViewport, WRFlat);
		panel->subBox = WMCreateFrame(panel->subViewport);
		WMMoveWidget(panel->subBox, 0, 0);
		WMResizeWidget(panel->subBox, icon_tile_size, icon_tile_size);
		WMSetFrameRelief(panel->subBox, WRFlat);
	}

	firstSub = populate_sub_strip(panel, rep);
	if (!firstSub)
		return NULL;

	if (WMGetArrayItemCount(panel->subWindows) <= 1) {
		free_sub_strip(panel);
		return NULL;
	}

	/* grow only when the sub-strip was not visible, and validate
	 * the growth before anything is mapped or resized */
	grow = panel->win && !panel->drilled && !panel->drillPreview;
	if (grow) {
		pw = WMWidgetWidth(panel->win);
		ph = WMWidgetHeight(panel->win);
		newHeight = ph + panel->subHeightExtra;
		if (newHeight < 1 ||
		    !wSwitchPanelWidgetSizeOk(pw, newHeight,
					     WMScreenUIScale(panel->scr->wmscreen)) ||
		    !prepare_replacement_bg(panel, pw, newHeight, &bg)) {
			wwarning(_("switch panel: sub-strip does not fit the panel, skipping it"));
			free_sub_strip(panel);
			if (panel->subViewport)
				WMUnmapWidget(panel->subViewport);
			return NULL;
		}
		old_bg = panel->bg;
		if (bg.image)
			panel->bg = bg.image;
		if (!render_sub_strip(panel, set_drilled)) {
			panel->bg = old_bg;
			discard_panel_bg(&bg);
			free_sub_strip(panel);
			WMUnmapWidget(panel->subViewport);
			return NULL;
		}
		panel->bg = old_bg;
		commit_panel_bg(panel, &bg);
	}

	if (panel->subViewport) {
		WMRealizeWidget(panel->subViewport);
		WMMapSubwidgets(panel->subViewport);
		WMMapSubwidgets(panel->subBox);
		WMMapWidget(panel->subViewport);

		if (grow) {
			WMResizeWidget(panel->win, pw, newHeight);
			if (panel->label)
				WMMoveWidget(panel->label, border_space,
					     border_space + icon_tile_size + 5 +
					     panel->subHeightExtra);
			place_panel(panel);
		}
	}

	if (set_drilled) {
		panel->drilled = true;
		if (panel->current >= 0)
			changeImage(panel, panel->current, 1, false, true, STRIP_MAIN);
	}
	if (panel->win)
		drawTitle(panel, panel->subCurrent, panelLabelForWindow(panel, firstSub, STRIP_SUB), STRIP_SUB);
	return firstSub;
}

WWindow *wSwitchPanelDrill(WSwitchPanel *panel)
{
	return show_sub_strip(panel, true);
}

WWindow *wSwitchPanelPreviewDrill(WSwitchPanel *panel)
{
	WWindow *rep, *firstSub;

	rep = WMGetFromArray(panel->windows, panel->current);
	if (!rep)
		return NULL;
	if (!isGroupDrillable(panel, rep)) {
		if (panel->drilled || panel->drillPreview)
			return wSwitchPanelUndrill(panel);
		return rep;
	}
	firstSub = show_sub_strip(panel, false);
	if (!firstSub)
		return rep;
	panel->drillPreview = true;
	if (panel->win && panel->current >= 0)
		changeImage(panel, panel->current, 1, false, true, STRIP_MAIN);
	return rep;
}

WWindow *wSwitchPanelRefreshSub(WSwitchPanel *panel)
{
	if (!panel->drilled && !panel->drillPreview)
		return NULL;
	if (panel->drilled)
		return show_sub_strip(panel, true);
	return show_sub_strip(panel, false);
}

bool wSwitchPanelIsDrilled(WSwitchPanel *panel)
{
	return panel->drilled;
}

bool wSwitchPanelIsDrillPreview(WSwitchPanel *panel)
{
	return panel->drillPreview;
}

static WSwitchPanel *abandon_panel(WSwitchPanel *panel, int count)
{
	WScreen *scr = panel->scr;
	CycleMode mode = panel->mode;
	bool snap_tile_image = panel->snap_tile_image;
	WMArray *windows = panel->windows;
	WMArray *flags = panel->flags;

	panel->windows = NULL;
	panel->flags = NULL;
	wSwitchPanelDestroy(panel);
	panel = wmalloc(sizeof(*panel));
	panel->scr = scr;
	panel->mode = mode;
	panel->snap_tile_image = snap_tile_image;
	panel->windows = windows;
	panel->flags = flags;
	panel->visibleCount = count;
	return panel;
}

WSwitchPanel *wInitSwitchPanel(WScreen *scr, WWindow *curwin, CycleMode mode)
{
	int wmScaleWidth, wmScaleHeight;
	WWindow *wwin;
	WSwitchPanel *panel = wmalloc(sizeof(WSwitchPanel));
	WMFrame *viewport;
	WSwitchPanelSizing siz;
	WSwitchPanelSize size;
	WSwitchPanelError serr;
	int i, count, height, panel_w, panel_h;
	bool want_bg;
	xcb_pixmap_t pixmap, mask;

	WMGetScaleBaseFromSystemFont(scr->wmscreen, &wmScaleWidth, &wmScaleHeight);

	panel->scr = scr;
	panel->mode = mode;
	panel->windows = makeWindowListArray(curwin, panel->scr->swtileImage != NULL, mode);
	count = WMGetArrayItemCount(panel->windows);
	if (count)
		panel->flags = makeWindowFlagsArray(count);

	if (count == 0) {
		WMFreeArray(panel->windows);
		wfree(panel);
		return NULL;
	}

	icon_size = w_global.config->switch_panel_icon_size;
	icon_tile_size = wSwitchPanelTileSize(icon_size);
	if (icon_tile_size <= 0) {
		/* the schema bounds this key, so cycle panel-less */
		wwarning(_("switch panel: icon size %d is unusable, cycling without a panel"),
			 icon_size);
		panel->visibleCount = count;
		return panel;
	}
	border_space = WMScaleY(10);
	label_height = WMAX(WMScaleY(25), 1);

	panel->snap_tile_image = (scr->swtileImage != NULL);
	panel->target_rect = resolve_target_rect(scr, &panel->opt_in_fit);
	panel->wd_pos = w_global.config->switch_panel_placement;
	panel->dev_pad = to_device(scr, border_space);

	/* the fallback label row sits 5 logical pixels under the icons */
	want_bg = (scr->swtileImage && scr->swbackImage[8]);
	memset(&siz, 0, sizeof(siz));
	siz.icon_size = icon_size;
	siz.count = count;
	siz.border = border_space;
	siz.label_height = label_height;
	siz.extra_h = want_bg ? 0 : 5;
	siz.head_w = panel->target_rect.size.width;
	siz.head_h = panel->target_rect.size.height;
	siz.ui_scale = WMScreenUIScale(scr->wmscreen);
	siz.opt_in_fit = panel->opt_in_fit;

	if (wSwitchPanelCalcSize(&siz, &size, &serr) != 0) {
		switch (serr) {
		case WSP_ERR_STRIP:
			wwarning(_("switch panel: the icon strip for %d windows exceeds the X width limit, cycling without a panel"),
				 count);
			break;
		case WSP_ERR_IMAGE:
			wwarning(_("switch panel: the panel geometry exceeds the image size limit, cycling without a panel"));
			break;
		case WSP_ERR_WIDGET:
			wwarning(_("switch panel: the panel geometry exceeds the window size limits, cycling without a panel"));
			break;
		default:
			wwarning(_("switch panel: cannot size a panel for %d windows, cycling without a panel"),
				 count);
			break;
		}
		panel->visibleCount = count;
		return panel;
	}

	if (want_bg &&
	    (size.panel_width <= scr->swbackImage[0]->width + scr->swbackImage[2]->width ||
	     size.panel_height <= scr->swbackImage[0]->height + scr->swbackImage[6]->height)) {
		want_bg = false;
		siz.extra_h = 5;
		if (wSwitchPanelCalcSize(&siz, &size, &serr) != 0) {
			wwarning(_("switch panel: fallback geometry is unusable, cycling without a panel"));
			panel->visibleCount = count;
			return panel;
		}
	}

	icon_tile_size = size.tile;
	panel->visibleCount = size.visible;
	panel->extra_h = siz.extra_h;

	if (!panel->snap_tile_image)
		return panel;

	if (!budget_creation(panel, &size, count, want_bg)) {
		wwarning(_("switch panel: the panel exceeds the switch panel resource budget, cycling without a panel"));
		panel->visibleCount = count;
		return panel;
	}

	panel->tileTmp = RCreateImage(size.tile, size.tile, 1);
	panel->tile = getTile(panel->scr);
	if (panel->tile && want_bg) {
		retain_back_pieces(panel, scr);
		panel->bg = createBackImage(panel, size.panel_width, size.panel_height);
	}

	if (!panel->tileTmp || !panel->tile || (want_bg && !panel->bg)) {
		wwarning(_("switch panel: cannot allocate its images, cycling without a panel"));
		return abandon_panel(panel, count);
	}

	height = size.content_height;
	panel_w = size.panel_width;
	panel_h = height + 2 * border_space;

	if (panel_h != size.panel_height &&
	    (!wSwitchPanelWidgetSizeOk(panel_w, panel_h,
				       WMScreenUIScale(scr->wmscreen)) ||
	     !wSwitchPanelImageSizeOk(panel_w, panel_h))) {
		wwarning(_("switch panel: panel geometry is unusable, cycling without a panel"));
		if (panel->bg)
			RReleaseImage(panel->bg);
		if (panel->tile)
			RReleaseImage(panel->tile);
		if (panel->tileTmp)
			RReleaseImage(panel->tileTmp);
		panel->bg = NULL;
		panel->tile = NULL;
		panel->tileTmp = NULL;
		return panel;
	}

	panel->white = WMWhiteColor(scr->wmscreen);
	panel->gray = WMGrayColor(scr->wmscreen);
	panel->font = WMBoldSystemFontOfSize(scr->wmscreen, WMScaleY(12));
	panel->icons = WMCreateArray(count);
	panel->images = WMCreateArray(count);

	panel->win = WMCreateWindow(scr->wmscreen, "");
	WMSetWindowTitle(panel->win, "Window Maker Switch Panel");

	if (!panel->bg) {
		WMFrame *frame = WMCreateFrame(panel->win);
		WMColor *darkGray = WMDarkGrayColor(scr->wmscreen);
		WMSetFrameRelief(frame, WRSimple);
		WMSetViewExpandsToParent(WMWidgetView(frame), 0, 0, 0, 0);

		panel->label = WMCreateLabel(panel->win);
		WMResizeWidget(panel->label, size.strip_width, label_height);
		WMMoveWidget(panel->label, border_space, border_space + icon_tile_size + 5);
		WMSetLabelRelief(panel->label, WRSimple);
		WMSetWidgetBackgroundColor(panel->label, darkGray);
		WMSetLabelFont(panel->label, panel->font);
		WMSetLabelTextColor(panel->label, panel->white);

		WMReleaseColor(darkGray);
	}

	WMResizeWidget(panel->win, panel_w, panel_h);

	viewport = WMCreateFrame(panel->win);
	WMResizeWidget(viewport, size.strip_width, icon_tile_size);
	WMMoveWidget(viewport, border_space, border_space);
	WMSetFrameRelief(viewport, WRFlat);
	panel->iconViewport = viewport;

	panel->iconBox = WMCreateFrame(viewport);
	WMMoveWidget(panel->iconBox, 0, 0);
	WMResizeWidget(panel->iconBox, size.full_width, icon_tile_size);
	WMSetFrameRelief(panel->iconBox, WRFlat);

	WM_ITERATE_ARRAY(panel->windows, wwin, i) {
		int x = 0;

		/* never skip an icon, the windows, icons and images
		 * arrays would desync */
		wSwitchPanelIconPos(i, icon_tile_size, &x);
		if (!addIconForWindow(panel, panel->iconBox, wwin, x, 0, STRIP_MAIN))
			return abandon_panel(panel, count);
	}

	WMMapSubwidgets(panel->win);
	if (!WMRealizeWidgetChecked(panel->win))
		return abandon_panel(panel, count);

	WM_ITERATE_ARRAY(panel->windows, wwin, i) {
		if (!changeImage(panel, i, 0, false, true, STRIP_MAIN))
			return abandon_panel(panel, count);
	}

	if (panel->bg) {
		if (!prepare_panel_bg(panel, panel->bg, &pixmap, &mask)) {
			wwarning(_("switch panel: cannot convert its background, cycling without a panel"));
			return abandon_panel(panel, count);
		}
		apply_panel_bg(panel, pixmap, mask);
	}

	place_panel(panel);

	panel->current = WMGetFirstInArray(panel->windows, curwin);
	if (panel->current >= 0 &&
	    !changeImage(panel, panel->current, 1, false, false, STRIP_MAIN))
		return abandon_panel(panel, count);

	WMMapWidget(panel->win);

	return panel;
}

void wSwitchPanelDestroy(WSwitchPanel *panel)
{
	int i;
	RImage *image;

	if (panel->win) {
		xcb_window_t info_win = panel->scr->info_window;

		WXSendClientMessage(wdisp->conn, info_win, XCB_EVENT_MASK_ENTER_WINDOW, info_win,
				    wdisp->atom.wm.ignore_focus_events,
				    true, 0, 0, 0, 0);
		WMUnmapWidget(panel->win);

		WXSendClientMessage(wdisp->conn, info_win, XCB_EVENT_MASK_ENTER_WINDOW, info_win,
				    wdisp->atom.wm.ignore_focus_events,
				    false, 0, 0, 0, 0);
	}

	if (panel->images) {
		WM_ITERATE_ARRAY(panel->images, image, i) {
			if (image)
				RReleaseImage(image);
		}
		WMFreeArray(panel->images);
	}
	if (panel->subImages) {
		WM_ITERATE_ARRAY(panel->subImages, image, i) {
			if (image)
				RReleaseImage(image);
		}
		WMFreeArray(panel->subImages);
	}

	if (panel->win)
		WMDestroyWidget(panel->win);

	if (panel->icons)
		WMFreeArray(panel->icons);
	if (panel->subIcons)
		WMFreeArray(panel->subIcons);

	if (panel->flags)
		WMFreeArray(panel->flags);
	if (panel->subFlags)
		WMFreeArray(panel->subFlags);

	WMFreeArray(panel->windows);
	if (panel->subWindows)
		WMFreeArray(panel->subWindows);

	if (panel->tile)
		RReleaseImage(panel->tile);

	if (panel->tileTmp)
		RReleaseImage(panel->tileTmp);

	if (panel->bg)
		RReleaseImage(panel->bg);

	release_back_pieces(panel);

	if (panel->font)
		WMReleaseFont(panel->font);

	if (panel->white)
		WMReleaseColor(panel->white);

	if (panel->gray)
		WMReleaseColor(panel->gray);

	wfree(panel);
}

WWindow *wSwitchPanelSelectNext(WSwitchPanel *panel, int back, int ignore_minimized, CycleMode mode, Strip strip)
{
	WMArray *winArr = *windows_of(panel, strip);
	int *current = current_of(panel, strip);
	int *firstVisible = firstVisible_of(panel, strip);
	int visibleCount = *visibleCount_of(panel, strip);
	WWindow *wwin, *curwin, *tmpwin;
	int count = WMGetArrayItemCount(winArr);
	int orig = *current;
	int i;
	bool dim = false;

	if (count == 0)
		return NULL;

	if (!w_global.config->cycle_ignore_minimized)
		ignore_minimized = false;

	if (ignore_minimized && canReceiveFocus(WMGetFromArray(winArr, (count + *current) % count)) < 0)
		ignore_minimized = false;

	curwin = WMGetFromArray(winArr, orig);
	do {
		do {
			if (back)
				(*current)--;
			else
				(*current)++;

			*current = (count + *current) % count;
			wwin = WMGetFromArray(winArr, *current);

			if (mode != CYCLE_CLASS_ONLY)
				break;
			if (*current == orig)
				break;
		} while (!sameWindowClass(wwin, curwin));
	} while (ignore_minimized && *current != orig && canReceiveFocus(wwin) < 0);

	WM_ITERATE_ARRAY(winArr, tmpwin, i) {
		if (i == *current)
			continue;
		if (mode != CYCLE_CLASS_ONLY || sameWindowClass(tmpwin, curwin))
			changeImage(panel, i, 0, false, false, strip);
		else {
			if (i == orig)
				dim = true;
			changeImage(panel, i, 0, true, false, strip);
		}

	}

	if (*current < *firstVisible)
		scrollIcons(panel, *current - *firstVisible, strip);
	else if (*current - *firstVisible >= visibleCount)
		scrollIcons(panel, *current - *firstVisible - visibleCount + 1, strip);

	if (panel->win) {
		drawTitle(panel, *current, panelLabelForWindow(panel, wwin, strip), strip);
		if (*current != orig)
			changeImage(panel, orig, 0, dim, false, strip);
		changeImage(panel, *current, 1, false, false, strip);
	}

	return wwin;
}

WWindow *wSwitchPanelSelectFirst(WSwitchPanel *panel, int back, Strip strip)
{
	WMArray *winArr = *windows_of(panel, strip);
	int *current = current_of(panel, strip);
	WWindow *wwin;
	int count = WMGetArrayItemCount(winArr);
	int i;

	if (count == 0)
		return NULL;

	if (back) {
		*current = count - 1;
		scrollIcons(panel, count, strip);
	} else {
		*current = 0;
		scrollIcons(panel, -count, strip);
	}

	wwin = WMGetFromArray(winArr, *current);

	if (panel->win) {
		for (WMArrayFirst(winArr, &(i)); (i) != WANotFound; WMArrayNext(winArr, &(i)))
			changeImage(panel, i, i == *current, false, false, strip);

		drawTitle(panel, *current, panelLabelForWindow(panel, wwin, strip), strip);
	}

	return wwin;
}

WWindow *wSwitchPanelSelectByKey(WSwitchPanel *panel, int key_char, int back, Strip strip)
{
	WMArray *winArr = *windows_of(panel, strip);
	int *current = current_of(panel, strip);
	int *firstVisible = firstVisible_of(panel, strip);
	int visibleCount = *visibleCount_of(panel, strip);
	WWindow *wwin;
	int count = WMGetArrayItemCount(winArr);
	int orig = *current;
	int i, n, ch;

	if (count == 0)
		return NULL;

	ch = toupper((unsigned char) key_char);
	if (!isalpha(ch))
		return NULL;

	/* scan from the entry after/before the current one, wrapping */
	n = -1;
	for (i = 1; i <= count; i++) {
		int idx = back ? (orig - i + count) % count
			       : (orig + i) % count;
		WWindow *w = WMGetFromArray(winArr, idx);
		const char *match;

		if (strip == STRIP_MAIN && panel->mode == CYCLE_GROUP && w->wm_class)
			match = w->wm_class;
		else
			match = w->frame->title;

		if (match && toupper((unsigned char) match[0]) == ch) {
			n = idx;
			break;
		}
	}
	if (n < 0)
		return NULL;	/* no match: leave current unchanged */

	*current = n;
	wwin = WMGetFromArray(winArr, n);

	/* re-render — mirrors wSwitchPanelSelectFirst, no dimming */
	if (panel->win) {
		for (WMArrayFirst(winArr, &(i)); (i) != WANotFound; WMArrayNext(winArr, &(i)))
			changeImage(panel, i, i == *current, false, false, strip);
	}

	if (*current < *firstVisible)
		scrollIcons(panel, *current - *firstVisible, strip);
	else if (*current - *firstVisible >= visibleCount)
		scrollIcons(panel, *current - *firstVisible - visibleCount + 1, strip);

	if (panel->win) {
		drawTitle(panel, *current, panelLabelForWindow(panel, wwin, strip), strip);
		changeImage(panel, *current, 1, false, false, strip);
	}

	return wwin;
}

WWindow *wSwitchPanelSelected(WSwitchPanel *panel, Strip strip)
{
	WMArray *winArr = *windows_of(panel, strip);
	int *current = current_of(panel, strip);

	if (!winArr || *current < 0 || *current >= WMGetArrayItemCount(winArr))
		return NULL;
	return WMGetFromArray(winArr, *current);
}

bool wSwitchPanelHolds(WSwitchPanel *panel, WWindow *wwin)
{
	if (!panel || !wwin)
		return false;
	return WMGetFirstInArray(panel->windows, wwin) != WANotFound ||
	       (panel->subWindows && WMGetFirstInArray(panel->subWindows, wwin) != WANotFound);
}

static void reflow_strip_icons(WSwitchPanel *panel, Strip strip)
{
	WMArray *iconArr = *icons_of(panel, strip);
	WMFrame *icon;
	int i, x;

	if (!iconArr)
		return;
	WM_ITERATE_ARRAY(iconArr, icon, i) {
		if (wSwitchPanelIconPos(i, icon_tile_size, &x))
			WMMoveWidget(icon, x, 0);
	}
}

static void strip_shrink_fixup(WSwitchPanel *panel, Strip strip)
{
	WMArray *winArr = *windows_of(panel, strip);
	int *current = current_of(panel, strip);
	int *firstVisible = firstVisible_of(panel, strip);
	int *visibleCount = visibleCount_of(panel, strip);
	int count = WMGetArrayItemCount(winArr);

	if (*visibleCount > count)
		*visibleCount = count;
	if (count == 0) {
		*current = 0;
		*firstVisible = 0;
		return;
	}
	if (*current >= count)
		*current = count - 1;
	if (*firstVisible > *current)
		*firstVisible = *current;
	if (*visibleCount > 0 && *firstVisible > count - *visibleCount)
		*firstVisible = count - *visibleCount;
	if (panel->win && iconbox_of(panel, strip))
		WMMoveWidget(iconbox_of(panel, strip), -*firstVisible * icon_tile_size, 0);
}

static void redraw_strip(WSwitchPanel *panel, Strip strip)
{
	WMArray *winArr = *windows_of(panel, strip);
	int *current = current_of(panel, strip);
	int i;

	if (!panel->win)
		return;
	for (i = 0; i < WMGetArrayItemCount(winArr); i++)
		changeImage(panel, i, i == *current, false, true, strip);
	if (*current >= 0 && *current < WMGetArrayItemCount(winArr))
		drawTitle(panel, *current,
			  panelLabelForWindow(panel, WMGetFromArray(winArr, *current), strip), strip);
}

static void strip_delete_at(WSwitchPanel *panel, int idx, Strip strip)
{
	WMArray *iconArr = *icons_of(panel, strip);
	WMArray *imageArr = *images_of(panel, strip);
	WMFrame *icon = NULL;
	RImage *image = NULL;

	if (iconArr) {
		icon = WMGetFromArray(iconArr, idx);
		WMDeleteFromArray(iconArr, idx);
	}
	if (imageArr) {
		image = WMGetFromArray(imageArr, idx);
		WMDeleteFromArray(imageArr, idx);
	}
	WMDeleteFromArray(*flags_of(panel, strip), idx);
	WMDeleteFromArray(*windows_of(panel, strip), idx);
	if (icon)
		WMDestroyWidget(icon);
	if (image)
		RReleaseImage(image);
}

static WWindow *group_replacement_rep(WSwitchPanel *panel, WWindow *rep)
{
	WMArray *group;
	WWindow *wwin, *found = NULL;
	int i;

	group = makeWindowListArray(rep, panel->snap_tile_image, CYCLE_CLASS_ONLY);
	WM_ITERATE_ARRAY(group, wwin, i) {
		if (wwin != rep) {
			found = wwin;
			break;
		}
	}
	WMFreeArray(group);
	return found;
}

static bool replace_window_at(WSwitchPanel *panel, int idx, WWindow *wwin, Strip strip)
{
	WMArray *winArr = *windows_of(panel, strip);
	WMArray *imageArr = *images_of(panel, strip);
	RImage *image, *old_image;
	bool ok;

	if (!panel->win || !imageArr) {
		WMReplaceInArray(winArr, idx, wwin);
		return true;
	}

	image = tile_image_for_window(panel, wwin, &ok);
	if (!ok)
		return false;

	old_image = WMGetFromArray(imageArr, idx);
	WMReplaceInArray(imageArr, idx, image);
	WMReplaceInArray(winArr, idx, wwin);
	if (old_image)
		RReleaseImage(old_image);
	return true;
}

static void resize_panel_to_count(WSwitchPanel *panel, int count)
{
	WSwitchPanelSizing siz;
	WSwitchPanelSize size;
	int pw, ph;

	if (!panel->win || count < 1)
		return;

	memset(&siz, 0, sizeof(siz));
	siz.icon_size = icon_size;
	siz.count = count;
	siz.border = border_space;
	siz.label_height = label_height;
	siz.extra_h = panel->extra_h;
	siz.head_w = panel->target_rect.size.width;
	siz.head_h = panel->target_rect.size.height;
	siz.ui_scale = WMScreenUIScale(panel->scr->wmscreen);
	siz.opt_in_fit = panel->opt_in_fit;

	if (wSwitchPanelCalcSize(&siz, &size, NULL) != 0)
		return;
	if (size.tile != icon_tile_size)
		return;

	pw = size.panel_width;
	ph = size.panel_height;
	if (panel->drilled || panel->drillPreview)
		ph += panel->subHeightExtra;
	if (pw == WMWidgetWidth(panel->win) && ph == WMWidgetHeight(panel->win))
		return;
	if (!replace_panel_bg(panel, pw, ph))
		return;

	WMResizeWidget(panel->win, pw, ph);
	if (panel->iconViewport)
		WMResizeWidget(panel->iconViewport, size.strip_width, icon_tile_size);
	WMResizeWidget(panel->iconBox, size.full_width, icon_tile_size);
	if (panel->label) {
		WMResizeWidget(panel->label, size.strip_width, label_height);
		WMMoveWidget(panel->label, border_space,
			     border_space + icon_tile_size + 5 +
			     (panel->drilled || panel->drillPreview ? panel->subHeightExtra : 0));
	}
	panel->visibleCount = size.visible;
	place_panel(panel);
}

static void shrink_sub_box(WSwitchPanel *panel)
{
	int width;

	if (!panel->subBox || !panel->subWindows)
		return;
	if (!wSwitchPanelStripWidth(WMGetArrayItemCount(panel->subWindows),
				    icon_tile_size,
				    WMScreenUIScale(panel->scr->wmscreen), &width))
		return;
	WMResizeWidget(panel->subBox, width, icon_tile_size);
}

WWindow *wSwitchPanelRemoveWindow(WSwitchPanel *panel, WWindow *wwin, Strip strip)
{
	WWindow *sel;
	bool main_removed = false;
	int idx;

	if (!panel || !wwin)
		return wSwitchPanelSelected(panel, strip);

	if (strip == STRIP_SUB && panel->subWindows) {
		idx = WMGetFirstInArray(panel->subWindows, wwin);
		if (idx >= 0) {
			strip_delete_at(panel, idx, STRIP_SUB);
			strip_shrink_fixup(panel, STRIP_SUB);
			reflow_strip_icons(panel, STRIP_SUB);
			redraw_strip(panel, STRIP_SUB);
			shrink_sub_box(panel);
		}
	}

	idx = WMGetFirstInArray(panel->windows, wwin);
	if (idx >= 0) {
		WWindow *replacement = NULL;

		if (panel->mode == CYCLE_GROUP)
			replacement = group_replacement_rep(panel, wwin);
		if (!replacement || !replace_window_at(panel, idx, replacement, STRIP_MAIN)) {
			strip_delete_at(panel, idx, STRIP_MAIN);
			strip_shrink_fixup(panel, STRIP_MAIN);
			reflow_strip_icons(panel, STRIP_MAIN);
			main_removed = true;
		}
		redraw_strip(panel, STRIP_MAIN);
	}

	if (panel->drilled || panel->drillPreview) {
		if (strip == STRIP_MAIN && idx >= 0) {
			if (main_removed) {
				if (WMGetArrayItemCount(panel->windows) > 0)
					wSwitchPanelUndrill(panel);
			} else {
				wSwitchPanelRefreshSub(panel);
			}
		} else if (panel->subWindows && WMGetArrayItemCount(panel->subWindows) < 2 &&
			   WMGetArrayItemCount(panel->windows) > 0) {
			wSwitchPanelUndrill(panel);
		}
	}

	if (main_removed) {
		resize_panel_to_count(panel, WMGetArrayItemCount(panel->windows));
		strip_shrink_fixup(panel, STRIP_MAIN);
		redraw_strip(panel, STRIP_MAIN);
	}

	sel = wSwitchPanelSelected(panel, strip);
	if (!sel)
		sel = wSwitchPanelSelected(panel, STRIP_MAIN);
	return sel;
}

WWindow *wSwitchPanelHandleEvent(WSwitchPanel *panel, xcb_generic_event_t *event)
{
	WMArray *winArr;
	WMFrame *icon;
	WWindow *wwin;
	int *current;
	int i;
	int focus = -1;
	Strip strip = STRIP_MAIN;

	if (!panel->win)
		return NULL;

	if ((event->response_type & 0x7f) == XCB_MOTION_NOTIFY) {
		WM_ITERATE_ARRAY(panel->icons, icon, i) {
			if (WMWidgetXID(icon) == ((xcb_motion_notify_event_t *)event)->event) {
				focus = i;
				strip = STRIP_MAIN;
				break;
			}
		}
		if (focus < 0 && panel->subIcons) {
			WM_ITERATE_ARRAY(panel->subIcons, icon, i) {
				if (WMWidgetXID(icon) == ((xcb_motion_notify_event_t *)event)->event) {
					focus = i;
					strip = STRIP_SUB;
					break;
				}
			}
		}
	}

	if (focus >= 0) {
		current = current_of(panel, strip);
		if (*current != focus) {
			winArr = *windows_of(panel, strip);
			WM_ITERATE_ARRAY(winArr, wwin, i)
				changeImage(panel, i, i == focus, false, false, strip);
			*current = focus;

			wwin = WMGetFromArray(winArr, focus);

			drawTitle(panel, *current, panelLabelForWindow(panel, wwin, strip), strip);

			return wwin;
		}
	}

	return NULL;
}

xcb_window_t wSwitchPanelGetWindow(WSwitchPanel *swpanel)
{
	if (!swpanel->win)
		return XCB_NONE;

	return WMWidgetXID(swpanel->win);
}
