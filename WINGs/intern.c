/*
 *  Window Maker miscellaneous function library
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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 *  MA 02110-1301, USA.
 */

/*
 * Tiny refcounted string-intern table. The window manager keeps the
 * WM_CLASS instance and class strings on every managed window, app icon
 * and dock entry, and most clients of a given application share the same
 * pair. Interning collapses N copies of "XTerm" to one heap allocation
 * and lets later work compare class strings by pointer.
 *
 * Not thread-safe, which is fine for wmaker and the WINGs toolkit.
 */

#include "wconfig.h"

#include <string.h>

#include "WUtil.h"

typedef struct {
	char *str;
	unsigned refcnt;
} WInternedString;

static WMHashTable *intern_table;

const char *wintern(const char *s)
{
	WInternedString *is;

	if (s == NULL)
		return NULL;

	if (intern_table == NULL)
		intern_table = WMCreateHashTable(WMStringPointerHashCallbacks);

	is = WMHashGet(intern_table, s);
	if (is == NULL) {
		is = wmalloc(sizeof(*is));
		is->str = wstrdup(s);
		is->refcnt = 0;
		WMHashInsert(intern_table, is->str, is);
	}
	is->refcnt++;
	return is->str;
}

void wunintern(const char *s)
{
	WInternedString *is;

	if (s == NULL || intern_table == NULL)
		return;

	is = WMHashGet(intern_table, s);
	if (is == NULL)
		return;

	if (--is->refcnt == 0) {
		WMHashRemove(intern_table, is->str);
		wfree(is->str);
		wfree(is);
	}
}
