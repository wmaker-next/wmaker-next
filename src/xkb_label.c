/* xkb_label.c - pure XKB rules-name language-label extraction
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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <ctype.h>
#include <string.h>

#include "xkb_label.h"

void wXkbExtractLanguageLabel(const char *prop, size_t len, int group_index,
			      char *label)
{
	const char *end, *layout, *tok;
	size_t f, n;
	int g;

	label[0] = '\0';
	if (!prop || len == 0)
		return;
	end = prop + len;

	/* the third NUL-separated field is the layout list */
	layout = prop;
	for (f = 0; f < 2 && layout < end; f++) {
		n = strnlen(layout, (size_t)(end - layout));
		if (n == (size_t)(end - layout))
			return;
		layout += n + 1;
	}

	if (layout >= end || !*layout)
		return;

	tok = layout;
	for (g = 0; g < group_index && tok; g++) {
		tok = memchr(tok, ',', (size_t)(end - tok));
		if (tok)
			tok++;
	}
	if (tok && tok < end && *tok && *tok != ',') {
		label[0] = (char)toupper((unsigned char)tok[0]);
		label[1] = (tok + 1 < end && tok[1] && tok[1] != ',')
			   ? (char)tolower((unsigned char)tok[1]) : '\0';
		label[2] = '\0';
	}
}
