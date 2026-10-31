/* xkb_label.h - pure XKB rules-name language-label extraction
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

#ifndef WMXKB_LABEL_H
#define WMXKB_LABEL_H

#include <stddef.h>

/* Extract the two-letter language label for group_index from an
 * _XKB_RULES_NAMES STRING property holding rules\0model\0layout\0
 * variant\0options. label must have room for three bytes and is set to
 * the empty string when no label can be resolved.
 */
void wXkbExtractLanguageLabel(const char *prop, size_t len, int group_index,
			      char *label);

#endif /* WMXKB_LABEL_H */
