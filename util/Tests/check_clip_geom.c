/*
 *  check_clip_geom - unit tests for the head-rect helpers in
 *  util/clip_geom.c. No display needed.
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>

#include "clip_geom.h"

#include "check_common.h"

static void test_head_for_rect(void)
{
	ClipHead heads[2] = {
		{ 0, 0, 800, 600 },
		{ 800, 0, 800, 600 },
	};
	ClipHead gapped[2] = {
		{ 0, 0, 800, 600 },
		{ 900, 0, 800, 600 },
	};

	CHECK(clipHeadForRect(heads, 2, 10, 10, 100, 100) == 0);
	CHECK(clipHeadForRect(heads, 2, 810, 10, 100, 100) == 1);

	/* straddling rect goes to the head with the larger overlap */
	CHECK(clipHeadForRect(heads, 2, 700, 10, 300, 100) == 1);
	CHECK(clipHeadForRect(heads, 2, 500, 10, 400, 100) == 0);

	/* a zero-size rect is a point */
	CHECK(clipHeadForRect(heads, 2, 900, 100, 0, 0) == 1);
	CHECK(clipHeadForRect(heads, 2, 100, 100, 0, 0) == 0);

	/* the shared edge belongs to the head that starts there */
	CHECK(clipHeadForRect(heads, 2, 800, 0, 0, 0) == 1);

	/* dead space between and below heads */
	CHECK(clipHeadForRect(gapped, 2, 820, 10, 40, 40) == -1);
	CHECK(clipHeadForRect(heads, 2, 100, 700, 10, 10) == -1);

	CHECK(clipHeadForRect(heads, 0, 10, 10, 10, 10) == -1);
}

static void test_head_for_rect_stacked(void)
{
	ClipHead heads[2] = {
		{ 0, 0, 1024, 768 },
		{ 0, 768, 1024, 768 },
	};

	CHECK(clipHeadForRect(heads, 2, 10, 10, 10, 10) == 0);
	CHECK(clipHeadForRect(heads, 2, 10, 778, 10, 10) == 1);
	CHECK(clipHeadForRect(heads, 2, 10, 700, 10, 200) == 1);
}

int main(void)
{
	test_head_for_rect();
	test_head_for_rect_stacked();

	return check_report("check_clip_geom");
}
