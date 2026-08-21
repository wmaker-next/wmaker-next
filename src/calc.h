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

#ifndef WMCALC_H_
#define WMCALC_H_

#include "wconfig.h"

/* Parse limits, referenced by the tests in src/Tests/check_calc.c. */
#define MAX_CALC_DEPTH 64
#define MAX_CALC_INPUT 256

/* Evaluate arithmetic expression. On success returns 1 and stores
 * the result in *out. On failure returns 0 and stores a freshly
 * wmalloc'd error message in *err (caller frees with wfree). */
int CalcEvaluate(const char *expr, double *out, char **err);

/* Return a freshly allocated display string for a result: plain integer
 * when it fits cleanly, else a trimmed %.12g so trailing-zero noise is
 * suppressed. Caller frees with wfree. */
char *CalcFormatResult(double v);

#endif /* WMCALC_H_ */
