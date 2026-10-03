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

/* Recursive-descent calculator for the run dialog '=' prefix mode.
 * X11-free, WUtil-only, unit-tested under src/Tests/check_calc.c. */
#include "wconfig.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>

#include <WINGs/WUtil.h>

#include "calc.h"

typedef struct {
	const char *p;		/* current parse position */
	const char *start;	/* expression start, for error offsets */
	int depth;
	char err[80];		/* last error message */
	int has_err;
} Calc;

static void calc_seterr(Calc *c, const char *msg, const char *pos)
{
	int off;

	if (c->has_err)
		return;
	off = pos ? (int)(pos - c->start) + 1 : 0;
	if (off > 0)
		snprintf(c->err, sizeof(c->err), "%s at pos %d", msg, off);
	else
		snprintf(c->err, sizeof(c->err), "%s", msg);
	c->has_err = 1;
	return;
}

static void skip_ws(Calc *c)
{
	while (*c->p == ' ' || *c->p == '\t')
		c->p++;
}

static int parse_expr(Calc *c, double *out);

/* parse_ident: copy an [a-zA-Z_][a-zA-Z0-9_]* into buf */
static int parse_ident(Calc *c, char *buf, size_t bufsz)
{
	size_t n = 0;

	if (!(isalpha((unsigned char)*c->p) || *c->p == '_'))
		return 0;
	while (isalnum((unsigned char)*c->p) || *c->p == '_') {
		if (n < bufsz - 1)
			buf[n++] = *c->p;
		c->p++;
	}
	buf[n] = 0;
	return 1;
}

/* apply a named 1-arg function, return 1 on success */
static int apply_fn1(const char *name, double a, double *out)
{
	if (strcmp(name, "sin") == 0)	{ *out = sin(a);		return 1; }
	if (strcmp(name, "cos") == 0)	{ *out = cos(a);		return 1; }
	if (strcmp(name, "tan") == 0)	{ *out = tan(a);		return 1; }
	if (strcmp(name, "asin") == 0)	{ *out = asin(a);	return 1; }
	if (strcmp(name, "acos") == 0)	{ *out = acos(a);	return 1; }
	if (strcmp(name, "atan") == 0)	{ *out = atan(a);	return 1; }
	if (strcmp(name, "sqrt") == 0)	{ *out = sqrt(a);	return 1; }
	if (strcmp(name, "cbrt") == 0)	{ *out = cbrt(a);	return 1; }
	if (strcmp(name, "exp") == 0)	{ *out = exp(a);		return 1; }
	if (strcmp(name, "log") == 0)	{ *out = log(a);		return 1; }
	if (strcmp(name, "log2") == 0)	{ *out = log2(a);	return 1; }
	if (strcmp(name, "log10") == 0)	{ *out = log10(a);	return 1; }
	if (strcmp(name, "fabs") == 0)	{ *out = fabs(a);	return 1; }
	if (strcmp(name, "floor") == 0)	{ *out = floor(a);	return 1; }
	if (strcmp(name, "ceil") == 0)	{ *out = ceil(a);		return 1; }
	if (strcmp(name, "round") == 0)	{ *out = round(a);	return 1; }
	return 0;
}

/* apply a named 2-arg function */
static int apply_fn2(const char *name, double a, double b, double *out)
{
	if (strcmp(name, "atan2") == 0)	{ *out = atan2(a, b);	return 1; }
	if (strcmp(name, "pow") == 0)	{ *out = pow(a, b);	return 1; }
	if (strcmp(name, "hypot") == 0)	{ *out = hypot(a, b);	return 1; }
	if (strcmp(name, "fmin") == 0)	{ *out = fmin(a, b);	return 1; }
	if (strcmp(name, "fmax") == 0)	{ *out = fmax(a, b);	return 1; }
	return 0;
}

static int parse_unary(Calc *c, double *out)
{
	char ident[32];
	double v;
	const char *mark;

	skip_ws(c);
	mark = c->p;

	/* unary minus/plus so 2^-3 works without parentheses */
	if (*c->p == '-') {
		c->p++;
		if (!parse_unary(c, out))
			return 0;
		*out = -*out;
		return 1;
	}
	if (*c->p == '+') {
		c->p++;
		return parse_unary(c, out);
	}

	/* parenthesized expression */
	if (*c->p == '(') {
		if (c->depth >= MAX_CALC_DEPTH) {
			calc_seterr(c, "nesting too deep", mark);
			return 0;
		}
		c->depth++;
		c->p++;
		skip_ws(c);
		if (!parse_expr(c, out))
			return 0;
		skip_ws(c);
		if (*c->p != ')') {
			calc_seterr(c, "missing )", c->p);
			return 0;
		}
		c->p++;
		c->depth--;
		return 1;
	}

	/* number */
	if (isdigit((unsigned char)*c->p) || *c->p == '.') {
		char *end;
		v = wstrtod(c->p, &end);
		if (end == c->p) {
			calc_seterr(c, "bad number", mark);
			return 0;
		}
		c->p = end;
		*out = v;
		return 1;
	}

	/* identifier: constant or function call */
	if (isalpha((unsigned char)*c->p) || *c->p == '_') {
		if (!parse_ident(c, ident, sizeof(ident)))
			return 0;
		skip_ws(c);
		if (*c->p == '(') {
			double a, b;

			if (c->depth >= MAX_CALC_DEPTH) {
				calc_seterr(c, "nesting too deep", mark);
				return 0;
			}
			c->depth++;
			c->p++;
			skip_ws(c);
			if (!parse_expr(c, &a))
				return 0;
			skip_ws(c);
			if (*c->p == ',') {
				c->p++;
				skip_ws(c);
				if (!parse_expr(c, &b))
					return 0;
				skip_ws(c);
				if (!apply_fn2(ident, a, b, out)) {
					calc_seterr(c, "unknown function", mark);
					return 0;
				}
			} else if (!apply_fn1(ident, a, out)) {
				calc_seterr(c, "unknown function", mark);
				return 0;
			}
			if (*c->p != ')') {
				calc_seterr(c, "missing )", c->p);
				return 0;
			}
			c->p++;
			c->depth--;
			return 1;
		}
		/* constants */
		if (strcmp(ident, "pi") == 0)	{ *out = M_PI;	return 1; }
		if (strcmp(ident, "e") == 0)	{ *out = M_E;	return 1; }
		if (strcmp(ident, "tau") == 0)	{ *out = 2*M_PI;return 1; }
		calc_seterr(c, "unknown identifier", mark);
		return 0;
	}

	calc_seterr(c, "unexpected token", mark);
	return 0;
}

/* power: right-associative ('^') */
static int parse_power(Calc *c, double *out)
{
	double base, expo;

	if (!parse_unary(c, &base))
		return 0;
	skip_ws(c);
	if (*c->p == '^') {
		c->p++;
		if (!parse_power(c, &expo))	/* right-assoc recursion */
			return 0;
		*out = pow(base, expo);
		return 1;
	}
	*out = base;
	return 1;
}

/* factor: unary minus/plus then power */
static int parse_factor(Calc *c, double *out)
{
	skip_ws(c);
	if (*c->p == '-') {
		c->p++;
		if (!parse_factor(c, out))
			return 0;
		*out = -*out;
		return 1;
	}
	if (*c->p == '+') {
		c->p++;
		return parse_factor(c, out);
	}
	return parse_power(c, out);
}

/* term: factor (('*'|'/'|'%') factor)* */
static int parse_term(Calc *c, double *out)
{
	double lhs, rhs;
	const char *mark;

	if (!parse_factor(c, &lhs))
		return 0;
	for (;;) {
		skip_ws(c);
		if (*c->p == '*') {
			c->p++;
			if (!parse_factor(c, &rhs)) return 0;
			lhs *= rhs;
		} else if (*c->p == '/') {
			c->p++;
			mark = c->p;
			if (!parse_factor(c, &rhs)) return 0;
			if (fpclassify(rhs) == FP_ZERO) {
				calc_seterr(c, "division by zero", mark);
				return 0;
			}
			lhs /= rhs;
		} else if (*c->p == '%') {
			c->p++;
			mark = c->p;
			if (!parse_factor(c, &rhs)) return 0;
			if (fpclassify(rhs) == FP_ZERO) {
				calc_seterr(c, "modulo by zero", mark);
				return 0;
			}
			lhs = fmod(lhs, rhs);
		} else {
			break;
		}
	}
	*out = lhs;
	return 1;
}

/* expr: term (('+'|'-') term)* */
static int parse_expr(Calc *c, double *out)
{
	double lhs, rhs;

	if (!parse_term(c, &lhs))
		return 0;
	for (;;) {
		skip_ws(c);
		if (*c->p == '+') {
			c->p++;
			if (!parse_term(c, &rhs)) return 0;
			lhs += rhs;
		} else if (*c->p == '-') {
			c->p++;
			if (!parse_term(c, &rhs)) return 0;
			lhs -= rhs;
		} else {
			break;
		}
	}
	*out = lhs;
	return 1;
}

int CalcEvaluate(const char *expr, double *out, char **err)
{
	Calc c;
	double v;

	if (!expr) {
		*err = wstrdup("empty expression");
		return 0;
	}
	if (strlen(expr) > MAX_CALC_INPUT) {
		*err = wstrdup("expression too long");
		return 0;
	}

	memset(&c, 0, sizeof(c));
	c.p = expr;
	c.start = expr;
	c.depth = 0;

	skip_ws(&c);
	if (*c.p == '\0') {
		*err = wstrdup("empty expression");
		return 0;
	}
	if (!parse_expr(&c, &v)) {
		*err = wstrdup(c.err[0] ? c.err : "syntax error");
		return 0;
	}
	skip_ws(&c);
	if (*c.p != '\0') {
		*err = wstrdup("trailing characters");
		return 0;
	}
	if (isnan(v)) {
		*err = wstrdup("result is NaN");
		return 0;
	}
	if (isinf(v)) {
		*err = wstrdup("result is infinite");
		return 0;
	}
	*out = v;
	return 1;
}

char *CalcFormatResult(double v)
{
	char buf[64];
	char *s, *t;

	/* adding +0.0 normalizes -0.0 so a zero result never prints as "-0" */
	v += 0.0;
	/* integer that fits cleanly: print without decimal noise */
	if (fabs(v - floor(v)) < 1e-12 && fabs(v) < 1e15)
		snprintf(buf, sizeof(buf), "%.0f", v);
	else
		/* %.12g trims trailing zeros and keeps full double precision */
		snprintf(buf, sizeof(buf), "%.12g", v);
	s = wstrdup(buf);
	/* comma-decimal LC_NUMERIC locales would print "3,5" */
	for (t = s; *t; t++) {
		if (*t == ',')
			*t = '.';
	}
	return s;
}
