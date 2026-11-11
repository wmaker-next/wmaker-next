/*
 *  headresolve.c
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

/*
 * Pure head lookup over a names array. Exact names win first,
 * case-sensitively, then a strict "#N" addresses an unnamed head.
 */

#include "wconfig.h"

#include <limits.h>
#include <string.h>
#include <strings.h>

#include "headresolve.h"

bool wHeadIndexParse(const char *token, int *index)
{
	const char *p;
	long long v = 0;

	if (!token || *token != '#')
		return false;

	p = token + 1;
	if (*p < '0' || *p > '9')
		return false;

	while (*p) {
		if (*p < '0' || *p > '9')
			return false;
		v = v * 10 + (*p - '0');
		if (v > INT_MAX)
			return false;
		p++;
	}

	*index = (int)v;
	return true;
}

int wHeadResolveFromNames(const char *const *names, int count, const char *value)
{
	int i, index;

	if (!value || !*value || count <= 0 || !names)
		return -1;

	for (i = 0; i < count; i++) {
		if (names[i] && strcmp(names[i], value) == 0)
			return i;
	}

	if (!wHeadIndexParse(value, &index))
		return -1;
	if (index >= count || names[index])
		return -1;

	return index;
}

WHeadSelection wHeadSelectFromNames(const char *const *names, int count,
				    const char *value)
{
	WHeadSelection sel;
	int i;

	sel.kind = WHEAD_SEL_UNRESOLVED;
	sel.head = -1;

	if (!value || !*value) {
		sel.kind = WHEAD_SEL_POINTER;
		return sel;
	}

	/* keywords win over an identically named monitor */
	if (strcasecmp(value, "cursor") == 0) {
		sel.kind = WHEAD_SEL_POINTER;
		return sel;
	}
	if (strcasecmp(value, "window") == 0) {
		sel.kind = WHEAD_SEL_WINDOW;
		return sel;
	}

	/* "name:" selects an exact name only, never a keyword or index */
	if (strncmp(value, "name:", 5) == 0) {
		const char *name = value + 5;

		if (!*name || count <= 0 || !names)
			return sel;
		for (i = 0; i < count; i++) {
			if (names[i] && strcmp(names[i], name) == 0) {
				sel.kind = WHEAD_SEL_HEAD;
				sel.head = i;
				return sel;
			}
		}
		return sel;
	}

	i = wHeadResolveFromNames(names, count, value);
	if (i >= 0) {
		sel.kind = WHEAD_SEL_HEAD;
		sel.head = i;
	}
	return sel;
}

static unsigned long long frame_overlap(long long x1, long long y1,
					long long w1, long long h1,
					long long x2, long long y2,
					long long w2, long long h2)
{
	long long l = (x1 > x2) ? x1 : x2;
	long long t = (y1 > y2) ? y1 : y2;
	long long r1 = x1 + w1, r2 = x2 + w2;
	long long b1 = y1 + h1, b2 = y2 + h2;
	long long r = (r1 < r2) ? r1 : r2;
	long long b = (b1 < b2) ? b1 : b2;

	if (r <= l || b <= t)
		return 0;
	return (unsigned long long)(r - l) * (unsigned long long)(b - t);
}

int wHeadForFrameRect(const WMRect *heads, int count, WMRect frame)
{
	unsigned long long best_area = 0;
	long long fw = frame.size.width, fh = frame.size.height;
	int best = -1;
	int i;

	if (!heads || count <= 0 || fw <= 0 || fh <= 0)
		return -1;

	for (i = 0; i < count; i++) {
		unsigned long long a;

		a = frame_overlap(frame.pos.x, frame.pos.y, fw, fh,
				  heads[i].pos.x, heads[i].pos.y,
				  heads[i].size.width, heads[i].size.height);
		if (a > best_area) {
			best_area = a;
			best = i;
		}
	}

	return best;
}
