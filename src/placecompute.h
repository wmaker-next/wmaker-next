/*
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

#ifndef WMPLACECOMPUTE_H_
#define WMPLACECOMPUTE_H_

#include "WindowMaker.h"

/*
 * Pure placement policy core, extracted from placement.c. The applier
 * reduces the live screen state into the env/occ inputs and applies the
 * returned position. The usable-area clamp stays in the applier.
 */
typedef struct {
	WArea usable;		/* head usable area, dock-inset */
	int origin_x, origin_y;	/* scr->window_place_origin, usable-clamped */
	int cascade_h;		/* computed titlebar step */
	int mode;		/* WPM_* */
	int icon_yard;		/* IY_* bitmask */
	int icon_size;
	int extra_w, extra_h;	/* titlebar/border deltas of the placed window */
	int cascade_index;	/* in */
	int cascade_next;	/* out, only written by wPlaceWindowCompute */
} WPlaceEnv;

/* one existing window, pre-reduced by the applier */
typedef struct {
	int x, y, w, h;		/* border-inclusive frame rect */
	int level;		/* window_level, for the WMNormalLevel filter */
	bool mapped, shaded, miniaturized, hidden, visible_any_head;
} WPlaceOcc;

/* window placement, all modes except WPM_MANUAL */
void wPlaceWindowCompute(WPlaceEnv *env, const WPlaceOcc *occ, int n_occ,
			 unsigned int width, unsigned int height,
			 int *x_ret, int *y_ret);

/* icon yard placement: occ entries carry the positions of the existing
 * icons/miniwindows the applier accepted */
void wPlaceIconCompute(const WPlaceEnv *env, const WPlaceOcc *occ, int n_occ,
		       int *x_ret, int *y_ret);
#endif
