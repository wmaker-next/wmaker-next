/*
 *  iconscale.c
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

/* Icon image downscaling, kept apart from the icon machinery. */

#include "wconfig.h"

#include <stdbool.h>

#include "iconscale.h"

#define ICON_BORDER 3

static RImage *validate_icon_size(RImage *icon, int max_size, bool strict)
{
	RImage *nimage;
	long long target, width, height;

	if (!icon)
		return NULL;

	/* the scaled icon keeps its ICON_BORDER margin inside max_size */
	target = (long long)max_size - ICON_BORDER;
	if (target < 1)
		target = 1;

	/* hold ICON_BORDER pixels of margin around the scaled icon */
	if (((max_size + ICON_BORDER) >= icon->width) &&
	    ((max_size + ICON_BORDER) >= icon->height))
		return icon;

	if (icon->width >= icon->height) {
		width = target;
		height = (long long)icon->height * target / icon->width;
	} else {
		height = target;
		width = (long long)icon->width * target / icon->height;
	}
	if (width < 1)
		width = 1;
	if (height < 1)
		height = 1;

	nimage = RScaleImage(icon, width, height);
	if (!nimage && strict)
		return NULL;
	if (nimage) {
		RReleaseImage(icon);
		icon = nimage;
	}

	return icon;
}

RImage *wIconValidateIconSize(RImage *icon, int max_size)
{
	return validate_icon_size(icon, max_size, false);
}

RImage *wIconValidateIconSizeStrict(RImage *icon, int max_size)
{
	return validate_icon_size(icon, max_size, true);
}
