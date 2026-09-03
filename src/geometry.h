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

#ifndef WMGEOMETRY_H_
#define WMGEOMETRY_H_

/* XParseGeometry mask bits, the Xutil.h spellings under project
 * ownership (referenced by the tests in src/Tests/check_geometry.c). */
enum {
	NoValue = 0x0000,
	XValue = 0x0001,
	YValue = 0x0002,
	WidthValue = 0x0004,
	HeightValue = 0x0008,
	AllValues = 0x000f,
	XNegative = 0x0010,
	YNegative = 0x0020
};

/* XParseGeometry equivalent, the strict libX11 grammar: [width]
 * [xheight][+-xoff][+-yoff] with an optional leading '='. Every
 * component is all-or-nothing: a position sign without a value, an
 * 'x' without a height, or any unconsumed tail makes the whole spec
 * invalid (NoValue). A position value may carry its own sign after
 * the component sign ("+-5" parses x = -5 without the XNegative bit),
 * digits wrap modulo 2^64 like libX11's accumulator. Mask bits are
 * the XValue/YValue/WidthValue/HeightValue/XNegative/YNegative set;
 * unset out-params are left untouched. */
int wParseGeometry(const char *string, int *x, int *y,
		   unsigned int *width, unsigned int *height);

#endif /* WMGEOMETRY_H_ */
