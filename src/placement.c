/* placement.c - window and icon placement on screen
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
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
#include <stdio.h>
#include <string.h>

#include "WindowMaker.h"
#include "wcore.h"
#include "framewin.h"
#include "window.h"
#include "icon.h"
#include "appicon.h"
#include "actions.h"
#include "application.h"
#include "dock.h"
#include "xinerama.h"
#include "workspace.h"
#include "placecompute.h"
#include "placement.h"


/* Returns True if it is an icon and is in this workspace */
static bool
iconPosition(WCoreWindow *wcore, int sx1, int sy1, int sx2, int sy2,
	     int workspace, int *retX, int *retY)
{
	void *parent;
	int ok = 0;

	parent = wcore->descriptor.parent;

	/* if it is an application icon */
	if (wcore->descriptor.parent_type == WCLASS_APPICON && !((WAppIcon *) parent)->docked) {
		*retX = ((WAppIcon *) parent)->x_pos;
		*retY = ((WAppIcon *) parent)->y_pos;

		ok = 1;
	} else if (wcore->descriptor.parent_type == WCLASS_MINIWINDOW &&
		   (WWIN_ON_WORKSPACE(((WIcon *) parent)->owner, workspace)
		    || w_global.config->sticky_icons)
		   && ((WIcon *) parent)->mapped) {

		*retX = ((WIcon *) parent)->owner->icon_x;
		*retY = ((WIcon *) parent)->owner->icon_y;

		ok = 1;
	} else if (wcore->descriptor.parent_type == WCLASS_WINDOW
		   && ((WWindow *) parent)->flags.icon_moved
		   && (WWIN_ON_WORKSPACE((WWindow *) parent, workspace)
		       || w_global.config->sticky_icons)) {
		*retX = ((WWindow *) parent)->icon_x;
		*retY = ((WWindow *) parent)->icon_y;

		ok = 1;
	}

	/* Check if it is inside the screen */
	if (ok) {
		if (*retX < sx1 - w_global.config->icon_size)
			ok = 0;
		else if (*retX > sx2)
			ok = 0;

		if (*retY < sy1 - w_global.config->icon_size)
			ok = 0;
		else if (*retY > sy2)
			ok = 0;
	}

	return ok;
}

void PlaceIcon(WScreen *scr, int *x_ret, int *y_ret, int head)
{
	WCoreWindow *obj;
	WMBagIterator iter;
	WPlaceEnv env;
	WPlaceOcc *occ;
	int n_occ, cap;
	int isize = w_global.config->icon_size;
	WArea area = wGetUsableAreaForHead(scr, head, NULL, false);

	/* Do not place icons under the dock, but only on the head(s)
	 * its main tile actually touches. */
	if (scr->dock && wAppIconTouchesHead(scr->dock->icon_array[0], head))
		wDockInsetArea(scr, &area);

	memset(&env, 0, sizeof(env));
	env.usable = area;
	env.icon_size = isize;
	env.icon_yard = w_global.config->icon_yard;

	/* reduce the stacking list to the icon positions the model
	 * marks into its slot map */
	cap = 0;
	WM_ETARETI_BAG(scr->stacking_list, obj, iter) {
		while (obj) {
			int px, py;

			if (iconPosition(obj, area.x1, area.y1, area.x2, area.y2,
					 scr->current_workspace, &px, &py))
				cap++;
			obj = obj->stacking->under;
		}
	}

	occ = wmalloc(sizeof(WPlaceOcc) * (cap > 0 ? cap : 1));
	n_occ = 0;
	WM_ETARETI_BAG(scr->stacking_list, obj, iter) {
		while (obj) {
			int px, py;

			if (iconPosition(obj, area.x1, area.y1, area.x2, area.y2,
					 scr->current_workspace, &px, &py)) {
				occ[n_occ].x = px;
				occ[n_occ].y = py;
				n_occ++;
			}
			obj = obj->stacking->under;
		}
	}

	wPlaceIconCompute(&env, occ, n_occ, x_ret, y_ret);
	wfree(occ);
}

/* the titlebar/border deltas set_width_height() would add, reduced for
 * the model */
static void set_frame_deltas(WWindow *wwin, int *extra_w, int *extra_h)
{
	*extra_h = 0;
	*extra_w = 0;

	if (wwin->frame) {
		*extra_h += wwin->frame->top_width + wwin->frame->bottom_width;
	} else {
		if (HAS_TITLEBAR(wwin))
			*extra_h += TITLEBAR_HEIGHT;
		if (HAS_RESIZEBAR(wwin))
			*extra_h += RESIZEBAR_HEIGHT;
	}
	if (HAS_BORDER(wwin)) {
		*extra_h += 2 * wFrameBorderWidth(wwin);
		*extra_w += 2 * wFrameBorderWidth(wwin);
	}
}

void PlaceWindow(WWindow *wwin, int *x_ret, int *y_ret, unsigned width, unsigned height)
{
	WScreen *scr = wwin->screen_ptr;
	int head;
	int h = WSCALE(scr, WMFontHeight(scr->title_font)
		+ (w_global.config->window_title_clearance + TITLEBAR_EXTEND_SPACE) * 2);

	if (h > w_global.config->window_title_max_height)
		h = w_global.config->window_title_max_height;

	if (h < w_global.config->window_title_min_height)
		h = w_global.config->window_title_min_height;

	/* In per-head mode the manage path seeds wwin->last_head with the
	 * head already showing the target workspace, so place there
	 * rather than under the pointer (which may be on a head where
	 * the target workspace is not visible). */
	if (w_global.config->per_head_workspaces
	    && wwin->last_head >= 0 && wwin->last_head < wHeadCount(scr))
		head = wwin->last_head;
	else
		head = wGetHeadForPointerLocation(scr);

	WArea usableArea = wGetUsableAreaForHead(scr, head, NULL, true);

	if (w_global.config->window_placement == WPM_MANUAL) {
		InteractivePlaceWindow(wwin, x_ret, y_ret, width, height);
	} else {
		WPlaceEnv env;
		WPlaceOcc *occ;
		WWindow *test, *first;
		int n_occ, total;

		memset(&env, 0, sizeof(env));
		env.usable = usableArea;
		env.origin_x = WMAX(usableArea.x1, scr->window_place_origin.x);
		env.origin_y = WMAX(usableArea.y1, scr->window_place_origin.y);
		env.cascade_h = h;
		env.mode = w_global.config->window_placement;
		env.cascade_index = scr->cascade_index;
		set_frame_deltas(wwin, &env.extra_w, &env.extra_h);

		for (first = scr->focused_window; first != NULL && first->prev != NULL;)
			first = first->prev;

		total = 0;
		for (test = first; test != NULL; test = test->next)
			total++;

		occ = wmalloc(sizeof(WPlaceOcc) * (total > 0 ? total : 1));
		n_occ = 0;
		for (test = first; test != NULL; test = test->next) {
			occ[n_occ].x = test->frame_x;
			occ[n_occ].y = test->frame_y;
			occ[n_occ].w = test->frame->core->width;
			occ[n_occ].h = test->frame->core->height;
			if (HAS_BORDER(test)) {
				occ[n_occ].w += 2 * wFrameBorderWidth(test);
				occ[n_occ].h += 2 * wFrameBorderWidth(test);
			}
			occ[n_occ].level = test->frame->core->stacking->window_level;
			occ[n_occ].mapped = test->flags.mapped;
			occ[n_occ].shaded = test->flags.shaded;
			occ[n_occ].miniaturized = test->flags.miniaturized;
			occ[n_occ].hidden = test->flags.hidden;
			occ[n_occ].visible_any_head = wWindowIsVisibleOnAnyHead(test);
			n_occ++;
		}

		wPlaceWindowCompute(&env, occ, n_occ, width, height, x_ret, y_ret);
		wfree(occ);

		if (w_global.config->window_placement == WPM_AUTO ||
		    w_global.config->window_placement == WPM_CENTER ||
		    w_global.config->window_placement == WPM_CASCADE)
			scr->cascade_index = env.cascade_next;
	}

	/*
	 * clip to usableArea instead of full screen
	 * this will also take dock/clip etc.. into account
	 * as well as being xinerama friendly
	 */
	if (*x_ret + width > usableArea.x2)
		*x_ret = usableArea.x2 - width;
	if (*x_ret < usableArea.x1)
		*x_ret = usableArea.x1;

	if (*y_ret + height > usableArea.y2)
		*y_ret = usableArea.y2 - height;
	if (*y_ret < usableArea.y1)
		*y_ret = usableArea.y1;
}
