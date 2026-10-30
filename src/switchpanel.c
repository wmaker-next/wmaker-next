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
#include "switchpanel.h"
#include "swlabel.h"
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
	WMFrame *iconBox;

	WMArray *icons;
	WMArray *images;
	WMArray *windows;
	WMArray *flags;
	RImage *bg;
	int current;
	int firstVisible;
	int visibleCount;
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
	bool drilled;
	bool drillPreview;
	int subHeightExtra;

	WMLabel *label;

	RImage *tileTmp;
	RImage *tile;

	WMFont *font;
	WMColor *white;
	WMColor *gray;
};

/* these values will be updated whenever the switch panel
 * is created to to match the size defined in switch_panel_icon_size
 * and the selected font size */
static short int icon_size;
static short int border_space;
static short int icon_tile_size;
static short int label_height;

#define SCREEN_BORDER_SPACING 2*20

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

static bool isGroupDrillable(WWindow *rep)
{
	WMArray *sub;
	int n;

	if (!rep)
		return false;
	sub = makeWindowListArray(rep, rep->screen_ptr->swtileImage != NULL, CYCLE_CLASS_ONLY);
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

static void changeImage(WSwitchPanel *panel, int idecks, int selected, bool dim, bool force, Strip strip)
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

	/* This whole function is a no-op if we aren't drawing the panel */
	if (!panel->scr->swtileImage)
		return;

	icon = WMGetFromArray(iconArr, idecks);
	image = WMGetFromArray(imageArr, idecks);
	flags = (int) (uintptr_t) WMGetFromArray(flagArr, idecks);

	if (selected)
		desired |= ICON_SELECTED;
	if (dim)
		desired |= ICON_DIM;

	if (flags == desired && !force)
		return;

	WMReplaceInArray(flagArr, idecks, (void *) (uintptr_t) desired);

	if (!panel->bg && !panel->tile && !selected)
		WMSetFrameRelief(icon, WRFlat);

	if (image && icon) {
		RImage *back;
		int opaq = (dim) ? 75 : 255;
		WMPoint pos;
		int bg_y;
		xcb_pixmap_t p;

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
				 isGroupDrillable(WMGetFromArray(winArr, idecks)));
		}

		RCombineAreaWithOpaqueness(back, image, 0, 0, image->width, image->height,
					   (back->width - image->width) / 2, (back->height - image->height) / 2,
					   opaq);

		if (arrow)
			draw_drill_arrow(back);

		RConvertImage(panel->scr->rcontext, back, &p);
		xcb_change_window_attributes(wdisp->conn, WMWidgetXID(icon), XCB_CW_BACK_PIXMAP, (const uint32_t[]){ p });
		xcb_clear_area(wdisp->conn, 0, WMWidgetXID(icon), 0, 0, 0, 0);
		xcb_free_pixmap(wdisp->conn, p);
	}

	if (!panel->bg && !panel->tile && selected)
		WMSetFrameRelief(icon, WRSimple);
}

static void addIconForWindow(WSwitchPanel *panel, WMWidget *parent, WWindow *wwin, int x, int y, Strip strip)
{
	WMFrame *icon = WMCreateFrame(parent);
	RImage *image = NULL;
	WApplication *wapp;

	WMSetFrameRelief(icon, WRFlat);
	WMResizeWidget(icon, icon_tile_size, icon_tile_size);
	WMMoveWidget(icon, x, y);

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
	if (!image)
		image = get_icon_image(panel->scr, wwin->wm_instance, wwin->wm_class, icon_size);

	/* _NET_WM_ICON and appicon sources can exceed the tile */
	image = wIconValidateIconSize(image, icon_size);

	WMAddToArray(*images_of(panel, strip), image);
	WMAddToArray(*icons_of(panel, strip), icon);
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
	RCopyArea(img, tmp, 0, 0, tmp->width, tmp->height, images[0]->width, 0);
	RReleaseImage(tmp);

	/* bottom */
	tmp = RSmoothScaleImage(images[7], tw, images[7]->height);
	RCopyArea(img, tmp, 0, 0, tmp->width, tmp->height, images[6]->width, height - images[6]->height);
	RReleaseImage(tmp);

	/* left */
	tmp = RSmoothScaleImage(images[3], images[3]->width, th);
	RCopyArea(img, tmp, 0, 0, tmp->width, tmp->height, 0, images[0]->height);
	RReleaseImage(tmp);

	/* right */
	tmp = RSmoothScaleImage(images[5], images[5]->width, th);
	RCopyArea(img, tmp, 0, 0, tmp->width, tmp->height, width - images[5]->width, images[2]->height);
	RReleaseImage(tmp);

	/* center */
	tmp = RSmoothScaleImage(images[4], tw, th);
	RCopyArea(img, tmp, 0, 0, tmp->width, tmp->height, images[0]->width, images[0]->height);
	RReleaseImage(tmp);

	/* corners */
	RCopyArea(img, images[0], 0, 0, images[0]->width, images[0]->height, 0, 0);
	RCopyArea(img, images[2], 0, 0, images[2]->width, images[2]->height, width - images[2]->width, 0);
	RCopyArea(img, images[6], 0, 0, images[6]->width, images[6]->height, 0, height - images[6]->height);
	RCopyArea(img, images[8], 0, 0, images[8]->width, images[8]->height,
		  width - images[8]->width, height - images[8]->height);

	return img;
}

static RImage *createBackImage(WScreen *scr, int width, int height)
{
	return assemblePuzzleImage(scr->swbackImage, width, height);
}

static RImage *getTile(WScreen *scr)
{
	RImage *stile;

	if (!scr->swtileImage)
		return NULL;

	stile = RScaleImage(scr->swtileImage, icon_tile_size, icon_tile_size);
	if (!stile)
		return RRetainImage(scr->swtileImage);

	return stile;
}

static void apply_panel_bg(WSwitchPanel *panel)
{
	xcb_pixmap_t pixmap, mask;

	if (!panel->bg || !panel->win)
		return;

	RConvertImageMask(panel->scr->rcontext, panel->bg, &pixmap, &mask, 250);
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
}

static WWindow *populate_sub_strip(WSwitchPanel *panel, WWindow *groupRep)
{
	WWindow *wwin;
	int subCount, i;

	free_sub_strip(panel);
	panel->subWindows = makeWindowListArray(groupRep, panel->scr->swtileImage != NULL, CYCLE_CLASS_ONLY);
	subCount = WMGetArrayItemCount(panel->subWindows);
	if (subCount == 0) {
		WMFreeArray(panel->subWindows);
		panel->subWindows = NULL;
		return NULL;
	}
	if (panel->subBox) {
		panel->subIcons = WMCreateArray(subCount);
		panel->subImages = WMCreateArray(subCount);
		panel->subFlags = makeWindowFlagsArray(subCount);
		WMResizeWidget(panel->subBox, icon_tile_size * subCount, icon_tile_size);
		WM_ITERATE_ARRAY(panel->subWindows, wwin, i) {
			addIconForWindow(panel, panel->subBox, wwin, i * icon_tile_size, 0, STRIP_SUB);
		}
	}
	panel->subVisibleCount = WMIN(subCount, panel->visibleCount);
	panel->subFirstVisible = 0;
	panel->subCurrent = WMGetFirstInArray(panel->subWindows, groupRep);
	if (panel->subCurrent < 0)
		panel->subCurrent = 0;
	return WMGetFromArray(panel->subWindows, panel->subCurrent);
}

WWindow *wSwitchPanelUndrill(WSwitchPanel *panel)
{
	WWindow *rep;
	int pw, ph, origHeight;

	if (!panel->drilled && !panel->drillPreview)
		return NULL;
	rep = WMGetFromArray(panel->windows, panel->current);
	if (panel->subViewport)
		WMUnmapWidget(panel->subViewport);
	free_sub_strip(panel);
	if (panel->win) {
		pw = WMWidgetWidth(panel->win);
		ph = WMWidgetHeight(panel->win);
		origHeight = ph - panel->subHeightExtra;
		WMResizeWidget(panel->win, pw, origHeight);
		if (panel->label)
			WMMoveWidget(panel->label, border_space, border_space + icon_tile_size + 5);
		if (panel->bg) {
			RReleaseImage(panel->bg);
			panel->bg = createBackImage(panel->scr, pw, origHeight);
			apply_panel_bg(panel);
		}
	}
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
	int pw, ph, newHeight, i;

	if (panel->mode != CYCLE_GROUP)
		return NULL;

	rep = WMGetFromArray(panel->windows, panel->current);
	if (!rep)
		return NULL;

	if (!isGroupDrillable(rep)) {
		if (panel->drilled || panel->drillPreview)
			return wSwitchPanelUndrill(panel);
		return rep;
	}

	/* refresh preview in place without re-growing the panel */
	if (panel->drillPreview && !set_drilled) {
		firstSub = populate_sub_strip(panel, rep);
		if (!firstSub)
			return wSwitchPanelUndrill(panel);
		if (panel->subBox && WMGetArrayItemCount(panel->subWindows) > 0) {
			if (panel->subViewport)
				WMRealizeWidget(panel->subViewport);
			WMRealizeWidget(panel->subBox);
			WMMapSubwidgets(panel->subBox);
			WM_ITERATE_ARRAY(panel->subWindows, rep, i)
				changeImage(panel, i, 0, false, true, STRIP_SUB);
			changeImage(panel, panel->subCurrent, 0, false, false, STRIP_SUB);
		}
		return WMGetFromArray(panel->windows, panel->current);
	}

	panel->subHeightExtra = border_space + icon_tile_size;

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
		return rep;
	}

	if (panel->subViewport) {
		WMRealizeWidget(panel->subViewport);
		WMMapSubwidgets(panel->subViewport);
		WMMapSubwidgets(panel->subBox);
		WMMapWidget(panel->subViewport);

		WM_ITERATE_ARRAY(panel->subWindows, rep, i)
			changeImage(panel, i, 0, false, true, STRIP_SUB);
		changeImage(panel, panel->subCurrent, set_drilled, false, false, STRIP_SUB);

		/* grow panel only when sub-strip was not visible */
		if (!panel->drilled && !panel->drillPreview) {
			pw = WMWidgetWidth(panel->win);
			ph = WMWidgetHeight(panel->win);
			newHeight = ph + panel->subHeightExtra;
			WMResizeWidget(panel->win, pw, newHeight);
			if (panel->label)
				WMMoveWidget(panel->label, border_space, border_space + icon_tile_size + 5 + panel->subHeightExtra);
			if (panel->bg) {
				RReleaseImage(panel->bg);
				panel->bg = createBackImage(panel->scr, pw, newHeight);
				apply_panel_bg(panel);
			}
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
	if (!isGroupDrillable(rep)) {
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

WSwitchPanel *wInitSwitchPanel(WScreen *scr, WWindow *curwin, CycleMode mode)
{
	int wmScaleWidth, wmScaleHeight;
	WWindow *wwin;
	WSwitchPanel *panel = wmalloc(sizeof(WSwitchPanel));
	WMFrame *viewport;
	int i, width, height, iconsThatFitCount, count;
	WMRect rect = wGetRectForHead(scr, wGetHeadForPointerLocation(scr));
	WMPoint center;

	WMGetScaleBaseFromSystemFont(scr->wmscreen, &wmScaleWidth, &wmScaleHeight);

	icon_size = w_global.config->switch_panel_icon_size;
	icon_tile_size = (short int)(((float)icon_size * 1.2F) + 0.5F);
	border_space = WMScaleY(10);
	label_height = WMScaleY(25);

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

	width = icon_tile_size* count;
	iconsThatFitCount = count;

	if (width > (int)rect.size.width) {
		iconsThatFitCount = (rect.size.width - SCREEN_BORDER_SPACING) / icon_tile_size;
		width = iconsThatFitCount * icon_tile_size;
	}

	panel->visibleCount = iconsThatFitCount;

	if (!panel->scr->swtileImage)
		return panel;

	height = label_height + icon_tile_size;

	panel->tileTmp = RCreateImage(icon_tile_size, icon_tile_size, 1);
	panel->tile = getTile(panel->scr);
	if (panel->tile && panel->scr->swbackImage[8])
		panel->bg = createBackImage(panel->scr, width + 2 * border_space, height + 2 * border_space);

	if (!panel->tileTmp || !panel->tile) {
		if (panel->bg)
			RReleaseImage(panel->bg);
		panel->bg = NULL;
		if (panel->tile)
			RReleaseImage(panel->tile);
		panel->tile = NULL;
		if (panel->tileTmp)
			RReleaseImage(panel->tileTmp);
		panel->tileTmp = NULL;
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
		WMResizeWidget(panel->label, width, label_height);
		WMMoveWidget(panel->label, border_space, border_space + icon_tile_size+ 5);
		WMSetLabelRelief(panel->label, WRSimple);
		WMSetWidgetBackgroundColor(panel->label, darkGray);
		WMSetLabelFont(panel->label, panel->font);
		WMSetLabelTextColor(panel->label, panel->white);

		WMReleaseColor(darkGray);
		height += 5;
	}

	WMResizeWidget(panel->win, width + 2 * border_space, height + 2 * border_space);

	viewport = WMCreateFrame(panel->win);
	WMResizeWidget(viewport, width, icon_tile_size);
	WMMoveWidget(viewport, border_space, border_space);
	WMSetFrameRelief(viewport, WRFlat);

	panel->iconBox = WMCreateFrame(viewport);
	WMMoveWidget(panel->iconBox, 0, 0);
	WMResizeWidget(panel->iconBox, icon_tile_size* count, icon_tile_size);
	WMSetFrameRelief(panel->iconBox, WRFlat);

	WM_ITERATE_ARRAY(panel->windows, wwin, i) {
		addIconForWindow(panel, panel->iconBox, wwin, i * icon_tile_size, 0, STRIP_MAIN);
	}

	WMMapSubwidgets(panel->win);
	WMRealizeWidget(panel->win);

	WM_ITERATE_ARRAY(panel->windows, wwin, i) {
		changeImage(panel, i, 0, false, true, STRIP_MAIN);
	}

	apply_panel_bg(panel);

	center = wGetPointToCenterRectInHead(scr, wGetHeadForPointerLocation(scr),
					     width + 2 * border_space, height + 2 * border_space);
	WMMoveWidget(panel->win, center.x, center.y);

	panel->current = WMGetFirstInArray(panel->windows, curwin);
	if (panel->current >= 0)
		changeImage(panel, panel->current, 1, false, false, STRIP_MAIN);

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
