/*
 *  check_icon_scale_failure.c
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

#include <wraster.h>

#include "iconscale.h"
#include "check_common.h"

static int releases;

RImage *RScaleImage(RImage *image, unsigned int width, unsigned int height)
{
	(void)image;
	(void)width;
	(void)height;
	return NULL;
}

void RReleaseImage(RImage *image)
{
	(void)image;
	releases++;
}

int main(void)
{
	RImage image = { 0 };

	image.width = 100;
	image.height = 50;
	CHECK(wIconValidateIconSizeStrict(&image, 64) == NULL);
	CHECK(image.width == 100 && image.height == 50);
	CHECK(releases == 0);
	CHECK(wIconValidateIconSize(&image, 64) == &image);
	CHECK(releases == 0);
	return check_report("check_icon_scale_failure");
}
