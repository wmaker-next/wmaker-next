/*
 *  check_calc.c
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
 *  check_calc - tests for the run dialog calculator parser in calc.c.
 *
 *  Pattern follows check_cmdexpand.c: compile the test + ../calc.c,
 *  link against libWUtil.la and libm.
 */

#include "wconfig.h"
#include <stdbool.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <WINGs/WUtil.h>

#include "calc.h"

#include "check_common.h"

static double ev(const char *expr, char **err)
{
	double out = 0.0;

	*err = NULL;
	CalcEvaluate(expr, &out, err);
	return out;
}

static bool near(double got, double want)
{
	return fabs(got - want) < 1e-12;
}

static void test_literals(void)
{
	char *e;

	CHECK(near(ev("42", &e), 42.0) && e == NULL);
	CHECK(near(ev("3.5", &e), 3.5) && e == NULL);
	CHECK(near(ev(".5", &e), 0.5) && e == NULL);
	CHECK(near(ev("1e3", &e), 1000.0) && e == NULL);
	CHECK(near(ev("0x10", &e), 16.0) && e == NULL);
	if (e) wfree(e);
}

static void test_operators(void)
{
	char *e;

	CHECK(near(ev("2+3", &e), 5.0) && e == NULL);
	CHECK(near(ev("10-4", &e), 6.0) && e == NULL);
	CHECK(near(ev("3*4", &e), 12.0) && e == NULL);
	CHECK(near(ev("15/4", &e), 3.75) && e == NULL);
	CHECK(near(ev("15%4", &e), 3.0) && e == NULL);
	CHECK(near(ev("2+3*4", &e), 14.0) && e == NULL);
	CHECK(near(ev("(2+3)*4", &e), 20.0) && e == NULL);
	CHECK(near(ev("3*100/45", &e), 20.0/3.0) && e == NULL);
	CHECK(near(ev("2*-3", &e), -6.0) && e == NULL);
	CHECK(near(ev("-2*-3", &e), 6.0) && e == NULL);
	if (e) wfree(e);
}

static void test_unary(void)
{
	char *e;

	CHECK(near(ev("-5", &e), -5.0) && e == NULL);
	CHECK(near(ev("--5", &e), 5.0) && e == NULL);
	CHECK(near(ev("-(3+2)", &e), -5.0) && e == NULL);
	CHECK(near(ev("+-5", &e), -5.0) && e == NULL);
	if (e) wfree(e);
}

static void test_power(void)
{
	char *e;

	CHECK(near(ev("2^3", &e), 8.0) && e == NULL);
	CHECK(near(ev("2^3^2", &e), 512.0) && e == NULL);	/* right-assoc */
	CHECK(near(ev("2^0.5", &e), sqrt(2.0)) && e == NULL);
	CHECK(near(ev("2^-3", &e), 0.125) && e == NULL);	/* unary minus in exponent */
	CHECK(near(ev("2^(-3)", &e), 0.125) && e == NULL);
	CHECK(near(ev("-2^3", &e), -8.0) && e == NULL);		/* -(2^3) = -8 */
	if (e) wfree(e);
}

static void test_functions(void)
{
	char *e;

	CHECK(near(ev("sin(0)", &e), 0.0) && e == NULL);
	CHECK(near(ev("cos(0)", &e), 1.0) && e == NULL);
	CHECK(near(ev("sqrt(16)", &e), 4.0) && e == NULL);
	CHECK(near(ev("cbrt(27)", &e), 3.0) && e == NULL);
	CHECK(near(ev("hypot(3,4)", &e), 5.0) && e == NULL);
	CHECK(near(ev("fmax(1,2)", &e), 2.0) && e == NULL);
	CHECK(near(ev("fmin(1,2)", &e), 1.0) && e == NULL);
	CHECK(near(ev("atan2(0,1)", &e), 0.0) && e == NULL);
	if (e) wfree(e);
}

static void test_constants(void)
{
	char *e;

	CHECK(near(ev("pi", &e), M_PI) && e == NULL);
	CHECK(near(ev("e", &e), M_E) && e == NULL);
	CHECK(near(ev("tau", &e), 2*M_PI) && e == NULL);
	CHECK(near(ev("2*pi", &e), 2*M_PI) && e == NULL);
	if (e) wfree(e);
}

static void test_errors(void)
{
	char *e;

	CHECK(near(ev("2+", &e), 0.0) && e != NULL);	if (e) wfree(e);
	CHECK(near(ev("(2+3", &e), 0.0) && e != NULL);	if (e) wfree(e);
	CHECK(near(ev("foo", &e), 0.0) && e != NULL);	if (e) wfree(e);
	CHECK(near(ev("", &e), 0.0) && e != NULL);	if (e) wfree(e);
	CHECK(near(ev("2 3", &e), 0.0) && e != NULL);	if (e) wfree(e);
	CHECK(near(ev("sin()", &e), 0.0) && e != NULL);	if (e) wfree(e);
	CHECK(near(ev("1/0*0", &e), 0.0) && e != NULL);	if (e) wfree(e);
}

/* the descriptive messages must not be shadowed by the generic
   "syntax error at pos N" fallback */
static void test_error_messages(void)
{
	char *e;

	e = NULL;
	ev("1/0", &e);
	CHECK_STR(e, "division by zero at pos 3");
	if (e) wfree(e);

	e = NULL;
	ev("1%0", &e);
	CHECK_STR(e, "modulo by zero at pos 3");
	if (e) wfree(e);

	e = NULL;
	ev("foo", &e);
	CHECK_STR(e, "unknown identifier at pos 1");
	if (e) wfree(e);
}

/* parse caps and the special-value guards */
static void test_limits(void)
{
	char *e;
	char buf[MAX_CALC_INPUT + 2];
	size_t i;

	/* one nesting level over MAX_CALC_DEPTH is rejected */
	for (i = 0; i < MAX_CALC_DEPTH + 1; i++)
		buf[i] = '(';
	buf[MAX_CALC_DEPTH + 1] = '1';
	for (i = 0; i < MAX_CALC_DEPTH + 1; i++)
		buf[MAX_CALC_DEPTH + 2 + i] = ')';
	buf[2 * MAX_CALC_DEPTH + 3] = 0;
	e = NULL;
	ev(buf, &e);
	CHECK(e != NULL && strcmp(e, "nesting too deep at pos 65") == 0);
	if (e) wfree(e);

	/* exactly MAX_CALC_DEPTH nesting still evaluates */
	for (i = 0; i < MAX_CALC_DEPTH; i++)
		buf[i] = '(';
	buf[MAX_CALC_DEPTH] = '1';
	for (i = 0; i < MAX_CALC_DEPTH; i++)
		buf[MAX_CALC_DEPTH + 1 + i] = ')';
	buf[2 * MAX_CALC_DEPTH + 1] = 0;
	CHECK(near(ev(buf, &e), 1.0) && e == NULL);
	if (e) wfree(e);

	/* a literal one char over MAX_CALC_INPUT is rejected */
	for (i = 0; i < MAX_CALC_INPUT + 1; i++)
		buf[i] = '1';
	buf[MAX_CALC_INPUT + 1] = 0;
	e = NULL;
	ev(buf, &e);
	CHECK_STR(e, "expression too long");
	if (e) wfree(e);

	/* exactly MAX_CALC_INPUT chars still evaluates */
	for (i = 0; i < MAX_CALC_INPUT; i++)
		buf[i] = '1';
	buf[MAX_CALC_INPUT] = 0;
	CHECK(ev(buf, &e) > 1e250 && e == NULL);
	if (e) wfree(e);
}

static void test_special_results(void)
{
	char *e;

	e = NULL;
	ev("1e308*10", &e);
	CHECK_STR(e, "result is infinite");
	if (e) wfree(e);

	e = NULL;
	ev("sqrt(-1)", &e);
	CHECK_STR(e, "result is NaN");
	if (e) wfree(e);
}

static void test_format(void)
{
	char *s;

	s = CalcFormatResult(5.0);
	CHECK_STR(s, "5");
	if (s) wfree(s);

	s = CalcFormatResult(3.5);
	CHECK_STR(s, "3.5");
	if (s) wfree(s);

	s = CalcFormatResult(0.0);
	CHECK_STR(s, "0");
	if (s) wfree(s);

	s = CalcFormatResult(-0.0);
	CHECK_STR(s, "0");
	if (s) wfree(s);

	s = CalcFormatResult(1000000.0);
	CHECK_STR(s, "1000000");
	if (s) wfree(s);
}

int main(void)
{
	test_literals();
	test_operators();
	test_unary();
	test_power();
	test_functions();
	test_constants();
	test_errors();
	test_error_messages();
	test_limits();
	test_special_results();
	test_format();

	return check_report("check_calc");
}
