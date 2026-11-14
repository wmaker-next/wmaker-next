/* placecompute.c - pure window and icon placement policy
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
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

#include "placecompute.h"

#define X_ORIGIN WMAX(env->usable.x1,\
	env->origin_x)

#define Y_ORIGIN WMAX(env->usable.y1,\
	env->origin_y)

/* Computes the intersecting length of two line sections */
int calcIntersectionLength(int p1, int l1, int p2, int l2)
{
	int isect;
	int tmp;

	if (p1 > p2) {
		tmp = p1;
		p1 = p2;
		p2 = tmp;
		tmp = l1;
		l1 = l2;
		l2 = tmp;
	}

	if (p1 + l1 < p2)
		isect = 0;
	else if (p2 + l2 < p1 + l1)
		isect = l2;
	else
		isect = p1 + l1 - p2;

	return isect;
}

/* Computes the intersecting area of two rectangles */
int calcIntersectionArea(int x1, int y1, int w1, int h1, int x2, int y2, int w2, int h2)
{
	return calcIntersectionLength(x1, w1, x2, w2)
	    * calcIntersectionLength(y1, h1, y2, h2);
}

/* the deltas set_width_height() would add to the placed window */
static void expand_dims(const WPlaceEnv *env, unsigned int *width, unsigned int *height)
{
	*height += env->extra_h;
	*width += env->extra_w;
}

static int occ_sum_of_covered(const WPlaceOcc *occ, int n_occ, int x, int y, int w, int h)
{
	int sum_isect = 0;
	int i;

	for (i = 0; i < n_occ; i++) {
		if (occ[i].level < WMNormalLevel) {
			continue;
		}

		if (occ[i].mapped || (occ[i].shaded &&
		     occ[i].visible_any_head &&
		     !(occ[i].miniaturized || occ[i].hidden))) {
			sum_isect += calcIntersectionArea(occ[i].x, occ[i].y, occ[i].w, occ[i].h,
							  x, y, w, h);
		}
	}

	return sum_isect;
}

static bool
occ_overlaps(const WPlaceOcc *o, int x, int y, int w, int h, bool ignore_sunken)
{
	if (ignore_sunken &&
	    o->level < WMNormalLevel) {
		return false;
	}

	if ((o->x < (x + w)) && ((o->x + o->w) > x) &&
	    (o->y < (y + h)) && ((o->y + o->h) > y) &&
	    (o->mapped ||
	     (o->shaded &&
	      o->visible_any_head &&
	      !(o->miniaturized || o->hidden)))) {
		return true;
	}

	return false;
}

static bool
occ_has_space(const WPlaceOcc *occ, int n_occ, int x, int y, int w, int h, bool ignore_sunken)
{
	int i;

	for (i = 0; i < n_occ; i++) {
		if (occ_overlaps(&occ[i], x, y, w, h, ignore_sunken)) {
			return false;
		}
	}

	return true;
}

static void
smartPlace(const WPlaceEnv *env, const WPlaceOcc *occ, int n_occ, int *x_ret, int *y_ret,
	   unsigned int width, unsigned int height)
{
	int test_x = 0, test_y = Y_ORIGIN;
	int from_x, to_x, from_y, to_y;
	int sx;
	int min_isect, min_isect_x, min_isect_y;
	int sum_isect;

	expand_dims(env, &width, &height);

	sx = X_ORIGIN;
	min_isect = INT_MAX;
	min_isect_x = sx;
	min_isect_y = test_y;

	while (((test_y + height) < env->usable.y2)) {
		test_x = sx;
		while ((test_x + width) < env->usable.x2) {
			sum_isect = occ_sum_of_covered(occ, n_occ, test_x, test_y, width, height);

			if (sum_isect < min_isect) {
				min_isect = sum_isect;
				min_isect_x = test_x;
				min_isect_y = test_y;
			}

			test_x += PLACETEST_HSTEP;
		}
		test_y += PLACETEST_VSTEP;
	}

	from_x = min_isect_x - PLACETEST_HSTEP + 1;
	from_x = WMAX(from_x, X_ORIGIN);
	to_x = min_isect_x + PLACETEST_HSTEP;
	if (to_x + width > env->usable.x2)
		to_x = env->usable.x2 - width;

	from_y = min_isect_y - PLACETEST_VSTEP + 1;
	from_y = WMAX(from_y, Y_ORIGIN);
	to_y = min_isect_y + PLACETEST_VSTEP;
	if (to_y + height > env->usable.y2)
		to_y = env->usable.y2 - height;

	for (test_x = from_x; test_x < to_x; test_x++) {
		for (test_y = from_y; test_y < to_y; test_y++) {
			sum_isect = occ_sum_of_covered(occ, n_occ, test_x, test_y, width, height);

			if (sum_isect < min_isect) {
				min_isect = sum_isect;
				min_isect_x = test_x;
				min_isect_y = test_y;
			}
		}
	}

	*x_ret = min_isect_x;
	*y_ret = min_isect_y;
}

static bool
center_place(const WPlaceEnv *env, int *x_ret, int *y_ret,
	     unsigned int width, unsigned int height)
{
	int swidth, sheight;

	expand_dims(env, &width, &height);
	swidth = env->usable.x2 - env->usable.x1;
	sheight = env->usable.y2 - env->usable.y1;

	if (width > swidth || height > sheight)
		return false;

	*x_ret = (env->usable.x1 + env->usable.x2 - width) / 2;
	*y_ret = (env->usable.y1 + env->usable.y2 - height) / 2;

	return true;
}

static bool
autoPlace(const WPlaceEnv *env, const WPlaceOcc *occ, int n_occ, int *x_ret, int *y_ret,
	  unsigned int width, unsigned int height,
	  bool ignore_sunken)
{
	int x, y;
	unsigned int fw = width, fh = height;	/* frame-inclusive */

	expand_dims(env, &fw, &fh);
	if (fw > INT_MAX || fh > INT_MAX)
		return false;

	/* try placing at center first, passing the raw dims so the
	 * expansion happens exactly once inside center_place */
	if (center_place(env, &x, &y, width, height) &&
	    occ_has_space(occ, n_occ, x, y, fw, fh, false)) {
		*x_ret = x;
		*y_ret = y;
		return true;
	}

	/* this was based on fvwm2's smart placement */
	for (y = Y_ORIGIN; y < env->usable.y2 - (int)fh; y += PLACETEST_VSTEP) {
		for (x = X_ORIGIN; x < env->usable.x2 - (int)fw; x += PLACETEST_HSTEP) {
			if (occ_has_space(occ, n_occ, x, y,
					  fw, fh, ignore_sunken)) {
				*x_ret = x;
				*y_ret = y;
				return true;
			}
		}
	}

	return false;
}

static void
cascadePlace(const WPlaceEnv *env, int cascade_index, int *x_ret, int *y_ret,
	     unsigned int width, unsigned int height, int h, bool *reset)
{
	expand_dims(env, &width, &height);

	*x_ret = h * cascade_index + X_ORIGIN;
	*y_ret = h * cascade_index + Y_ORIGIN;

	if (width + *x_ret > env->usable.x2 || height + *y_ret > env->usable.y2) {
		*reset = true;
		*x_ret = X_ORIGIN;
		*y_ret = Y_ORIGIN;
	}
}

static void randomPlace(const WPlaceEnv *env, int *x_ret, int *y_ret,
			unsigned int width, unsigned int height)
{
	int w, h;

	expand_dims(env, &width, &height);

	w = ((env->usable.x2 - X_ORIGIN) - width);
	h = ((env->usable.y2 - Y_ORIGIN) - height);
	if (w < 1)
		w = 1;
	if (h < 1)
		h = 1;
	*x_ret = X_ORIGIN + rand() % w;
	*y_ret = Y_ORIGIN + rand() % h;
}

void wPlaceWindowCompute(WPlaceEnv *env, const WPlaceOcc *occ, int n_occ,
			 unsigned int width, unsigned int height,
			 int *x_ret, int *y_ret)
{
	env->cascade_next = env->cascade_index;

	switch (env->mode) {
	case WPM_SMART:
		smartPlace(env, occ, n_occ, x_ret, y_ret, width, height);
		break;

	case WPM_CENTER:
		if (center_place(env, x_ret, y_ret, width, height))
			break;
		/* Fall through. */

	case WPM_AUTO:
		if (autoPlace(env, occ, n_occ, x_ret, y_ret, width, height, false)) {
			break;
		} else if (autoPlace(env, occ, n_occ, x_ret, y_ret, width, height, true)) {
			break;
		}
		/* there isn't a break here, because if we fail, it should fall
		   through to cascade placement, as people who want tiling want
		   automagicness aren't going to want to place their window */

		/* Fall through. */

	case WPM_CASCADE: {
		bool reset = false;
		int idx = env->cascade_index;

		if (env->mode == WPM_AUTO || env->mode == WPM_CENTER)
			idx++;

		cascadePlace(env, idx, x_ret, y_ret, width, height, env->cascade_h, &reset);

		if (env->mode == WPM_AUTO || env->mode == WPM_CENTER)
			env->cascade_next = reset ? 0 : idx;
		else
			env->cascade_next = reset ? 1 : idx + 1;
		break;
	}

	case WPM_RANDOM:
		randomPlace(env, x_ret, y_ret, width, height);
		break;
	}
}

/* Mark one slot cell, skipping cells whose linear index falls
 * outside the map. Unreachable on heads with a non-negative
 * origin, where negative-origin heads would otherwise index
 * before the buffer. */
static void mark_slot(char *map, int map_size, int stride, int x, int y)
{
	int idx = (y + 1) * stride + x + 1;

	if (idx >= 0 && idx < map_size)
		map[idx] = 1;
}

void wPlaceIconCompute(const WPlaceEnv *env, const WPlaceOcc *occ, int n_occ,
		       int *x_ret, int *y_ret)
{
	int pf;			/* primary axis */
	int sf;			/* secondary axis */
	int fullW;
	int fullH;
	char *map;
	int map_size;
	int pi, si;
	int sx1, sx2, sy1, sy2;	/* screen boundary */
	int sw, sh;
	int xo, yo;
	int xs, ys;
	int x, y;
	int isize = env->icon_size;
	int done = 0;
	int i;

	/* Find out screen boundaries. */

	/* Allows each head to have miniwindows */
	sx1 = env->usable.x1;
	sy1 = env->usable.y1;
	sx2 = env->usable.x2;
	sy2 = env->usable.y2;
	sw = sx2 - sx1;
	sh = sy2 - sy1;

	sw = isize * (sw / isize);
	sh = isize * (sh / isize);
	fullW = (sx2 - sx1) / isize;
	fullH = (sy2 - sy1) / isize;

	/* icon yard boundaries */
	if (env->icon_yard & IY_VERT) {
		pf = fullH;
		sf = fullW;
	} else {
		pf = fullW;
		sf = fullH;
	}
	if (env->icon_yard & IY_RIGHT) {
		xo = sx2 - isize;
		xs = -1;
	} else {
		xo = sx1;
		xs = 1;
	}
	if (env->icon_yard & IY_TOP) {
		yo = sy1;
		ys = 1;
	} else {
		yo = sy2 - isize;
		ys = -1;
	}

	/*
	 * Create a map with the occupied slots. 1 means the slot is used
	 * or at least partially used.
	 */
	map_size = (sw + 2) * (sh + 2);
	map = wmalloc(map_size);

#define INDEX(x,y)	(((y)+1)*(sw+2) + (x) + 1)

	for (i = 0; i < n_occ; i++) {
		int xdi, ydi;	/* rounded down */
		int xui, yui;	/* rounded up */

		xdi = occ[i].x / isize;
		ydi = occ[i].y / isize;
		xui = (occ[i].x + isize / 2) / isize;
		yui = (occ[i].y + isize / 2) / isize;
		mark_slot(map, map_size, sw + 2, xdi, ydi);
		mark_slot(map, map_size, sw + 2, xdi, yui);
		mark_slot(map, map_size, sw + 2, xui, ydi);
		mark_slot(map, map_size, sw + 2, xui, yui);
	}
	/* Default position */
	*x_ret = 0;
	*y_ret = 0;

	/* Look for an empty slot */
	for (si = 0; si < sf; si++) {
		for (pi = 0; pi < pf; pi++) {
			int idx;

			if (env->icon_yard & IY_VERT) {
				x = xo + xs * (si * isize);
				y = yo + ys * (pi * isize);
			} else {
				x = xo + xs * (pi * isize);
				y = yo + ys * (si * isize);
			}
			idx = INDEX(x / isize, y / isize);
			/* cells the map cannot address count as
			 * occupied so no icon is placed on them */
			if (idx >= 0 && idx < map_size && !map[idx]) {
				*x_ret = x;
				*y_ret = y;
				done = 1;
				break;
			}
		}
		if (done)
			break;
	}

	wfree(map);
#undef INDEX
}
