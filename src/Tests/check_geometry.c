/*
 *  check_geometry.c
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
 *  check_geometry - tests for the wParseGeometry() grammar in
 *  geometry.c, the XParseGeometry stand-in from the C2 step.
 *
 *  Pattern follows check_calc.c: compile the test + ../geometry.c,
 *  link against libWUtil.la.
 */

#include "wconfig.h"
#include <stdbool.h>

#include <stdio.h>
#include <string.h>

#include <WINGs/WUtil.h>

#include "geometry.h"

#include "check_common.h"

/* parse with sentinel outputs so untouched fields are detectable */
static int pg(const char *spec, int *x, int *y, unsigned *w, unsigned *h)
{
	*x = -777;
	*y = -777;
	*w = 777;
	*h = 777;
	return wParseGeometry(spec, x, y, w, h);
}

static void test_full_spec(void)
{
	int x, y, mask;
	unsigned w, h;

	mask = pg("640x480+100+50", &x, &y, &w, &h);
	CHECK(mask == (WidthValue | HeightValue | XValue | YValue));
	CHECK(w == 640 && h == 480 && x == 100 && y == 50);
}

static void test_prefix_and_signs(void)
{
	int x, y, mask;
	unsigned w, h;

	/* the optional '=' prefix is skipped */
	mask = pg("=640x480-100-50", &x, &y, &w, &h);
	CHECK(mask == (WidthValue | HeightValue | XValue | YValue
		       | XNegative | YNegative));
	CHECK(w == 640 && h == 480 && x == -100 && y == -50);

	/* offsets only; the sign trick yields negative x */
	mask = pg("-100+50", &x, &y, &w, &h);
	CHECK(mask == (XValue | YValue | XNegative));
	CHECK(x == -100 && y == 50);
	CHECK(w == 777 && h == 777);

	/* zero offsets still set the flags, only the sign flips */
	mask = pg("+0-0", &x, &y, &w, &h);
	CHECK(mask == (XValue | YValue | YNegative));
	CHECK(x == 0 && y == 0);

	/* the strtoul sign trick on a lone negative offset */
	mask = pg("-10", &x, &y, &w, &h);
	CHECK(mask == (XValue | XNegative));
	CHECK(x == -10);

	/* libX11's "+-5" form: the value's own sign applies, XNegative
	 * does not (the component sign was '+') */
	mask = pg("+-5", &x, &y, &w, &h);
	CHECK(mask == XValue);
	CHECK(x == -5);

	mask = pg("10x20+-5+2", &x, &y, &w, &h);
	CHECK(mask == (WidthValue | HeightValue | XValue | YValue));
	CHECK(w == 10 && h == 20 && x == -5 && y == 2);

	/* a doubled sign flips the value but XNegative rides the
	 * component sign */
	mask = pg("--5", &x, &y, &w, &h);
	CHECK(mask == (XValue | XNegative));
	CHECK(x == 5);

	/* X-only commit is fine when the spec ends there */
	mask = pg("10x20+1", &x, &y, &w, &h);
	CHECK(mask == (WidthValue | HeightValue | XValue));
	CHECK(x == 1);

	/* a position sign with no value kills the whole spec */
	mask = pg("10x20-5-", &x, &y, &w, &h);
	CHECK(mask == NoValue);
}

static void test_partial_and_missing(void)
{
	int x, y, mask;
	unsigned w, h;

	mask = pg("x480", &x, &y, &w, &h);
	CHECK(mask == HeightValue);
	CHECK(h == 480);

	mask = pg("640", &x, &y, &w, &h);
	CHECK(mask == WidthValue);

	/* the strict libX11 grammar: a trailing tail or an unconsumable
	 * middle invalidates the whole spec, no output is written */
	mask = pg("640x", &x, &y, &w, &h);
	CHECK(mask == NoValue);
	CHECK(w == 777 && h == 777 && x == -777 && y == -777);

	mask = pg("10x20abc", &x, &y, &w, &h);
	CHECK(mask == NoValue);
	CHECK(w == 777 && h == 777);

	mask = pg("10x 20+1+2", &x, &y, &w, &h);
	CHECK(mask == NoValue);

	mask = pg("  10x20", &x, &y, &w, &h);
	CHECK(mask == NoValue);

	mask = pg("1x2x3", &x, &y, &w, &h);
	CHECK(mask == NoValue);

	/* nothing parseable: NoValue, outputs untouched */
	mask = pg("", &x, &y, &w, &h);
	CHECK(mask == NoValue);
	mask = pg("garbage", &x, &y, &w, &h);
	CHECK(mask == NoValue);
	mask = pg("=", &x, &y, &w, &h);
	CHECK(mask == NoValue);
	CHECK(w == 777 && h == 777 && x == -777 && y == -777);

	CHECK(wParseGeometry(NULL, &x, &y, &w, &h) == NoValue);
}

static void test_mask_bits(void)
{
	/* the bit values are the Xutil.h XParseGeometry contract */
	CHECK(NoValue == 0x0000);
	CHECK(XValue == 0x0001);
	CHECK(YValue == 0x0002);
	CHECK(WidthValue == 0x0004);
	CHECK(HeightValue == 0x0008);
	CHECK(AllValues == 0x000f);
	CHECK(XNegative == 0x0010);
	CHECK(YNegative == 0x0020);
}

int main(void)
{
	test_full_spec();
	test_prefix_and_signs();
	test_partial_and_missing();
	test_mask_bits();

	return check_report("check_geometry");
}
