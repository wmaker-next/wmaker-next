/* accent_data.h - built-in accent picker character sets
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

#ifndef WMACCENT_DATA_H
#define WMACCENT_DATA_H

#include "wconfig.h"

#ifdef KEEP_XKB_LOCK_STATUS

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AP_MAX_VARIANTS 64
#define AP_MAX_VARIANT_BYTES 256

typedef struct {
	const char *base;
	const char *const *variants;
	size_t variant_count;
} APMapping;

typedef struct {
	const char *name;
	const char *group;
	const char *const *layouts;
	const APMapping *mappings;
	size_t mapping_count;
} APSet;

size_t apSetCount(void);
const APSet *apSetAt(size_t index);
const APSet *apSetFind(const char *name);

size_t apResolveSetsCount(const APSet *const *out, size_t out_cap);
size_t apResolveSets(const char *const *tokens, size_t token_count,
		     const char *const *layouts, size_t layout_count,
		     int active_layout, const APSet **out, size_t out_cap,
		     bool *unknown_name);

char **apMergeVariants(const APSet *const *sets, size_t set_count,
		       const APMapping *user, size_t user_count,
		       const char *base, bool uppercase, size_t *out_count);
void apFreeVariants(char **variants, size_t count);

bool apValidUtf8(const char *s);
size_t apUtf8Codepoints(const char *s);
char *apUppercaseCopy(const char *s);
char *apNormalizeBaseCopy(const char *s, bool *is_letter);

uint32_t apToUpper(uint32_t cp);
uint32_t apToLower(uint32_t cp);

#endif /* KEEP_XKB_LOCK_STATUS */

#endif /* WMACCENT_DATA_H */
