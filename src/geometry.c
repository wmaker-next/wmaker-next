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

#include "wconfig.h"

#include <stdbool.h>
#include <stdlib.h>

#include "geometry.h"

/* strict digit scan: no leading-whitespace skipping, no sign; the
 * accumulator wraps modulo 2^64, exactly the semantics the huge-value
 * cases pin against libX11 */
static const char *scan_digits(const char *s, unsigned long *out)
{
	unsigned long v = 0;

	while (*s >= '0' && *s <= '9') {
		v = v * 10UL + (unsigned long)(*s - '0');
		s++;
	}
	*out = v;
	return s;
}

/* one position component: an outer component sign (which also sets
 * the negative flag), then an optional inner value sign — the "+-5"
 * form libX11 parses as x = -5 without the negative flag, and two
 * signs flip twice so "--5" is +5. A component with an inner sign
 * always commits (0 when no digits follow, the value's own sign
 * otherwise); a single sign with no digits does not commit and makes
 * the whole spec invalid. Returns the new mask, or -1 when the
 * component is malformed. */
static int parse_position(const char **sp, int mask, int value_bit,
			  int negative_bit, int *out)
{
	const char *s = *sp;
	bool neg = (*s == '-');
	bool vneg = false;
	bool committed = false;
	unsigned long v = 0;

	s++;
	if (*s == '+' || *s == '-') {
		vneg = (*s == '-');
		s++;
		committed = true;
	}
	if (*s >= '0' && *s <= '9') {
		s = scan_digits(s, &v);
		committed = true;
	}
	if (!committed)
		return -1;

	*out = (int)(unsigned long)(neg != vneg ? 0UL - v : v);
	*sp = s;
	mask |= value_bit;
	if (neg)
		mask |= negative_bit;
	return mask;
}

int wParseGeometry(const char *string, int *x, int *y,
		   unsigned int *width, unsigned int *height)
{
	int mask = 0;
	int xval = 0, yval = 0;
	unsigned int wval = 0, hval = 0;
	bool have_x = false, have_y = false, have_w = false, have_h = false;
	const char *s;

	if (string == NULL || *string == '\0')
		return 0;

	if (*string == '=')
		string++;

	s = string;

	if (*s >= '0' && *s <= '9') {
		unsigned long v;

		s = scan_digits(s, &v);
		wval = (unsigned int)v;
		have_w = true;
		mask |= WidthValue;
	}

	if (*s == 'x' || (have_w && *s == 'X')) {
		unsigned long v;

		s++;
		if (*s < '0' || *s > '9')
			return 0;
		s = scan_digits(s, &v);
		hval = (unsigned int)v;
		have_h = true;
		mask |= HeightValue;
	}

	if (*s == '+' || *s == '-') {
		mask = parse_position(&s, mask, XValue, XNegative, &xval);
		if (mask < 0)
			return 0;
		have_x = true;
	}

	if (*s == '+' || *s == '-') {
		mask = parse_position(&s, mask, YValue, YNegative, &yval);
		if (mask < 0)
			return 0;
		have_y = true;
	}

	/* anything left over invalidates the whole spec */
	if (*s != '\0')
		return 0;

	if (have_w)
		*width = wval;
	if (have_h)
		*height = hval;
	if (have_x)
		*x = xval;
	if (have_y)
		*y = yval;
	return mask;
}
