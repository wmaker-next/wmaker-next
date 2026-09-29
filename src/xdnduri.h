/*
 *  xdnduri - pure text/uri-list parsing for the XDND dock drop. The
 *  payload arrives through a selection owned by the dragging client,
 *  so the split is bounded and the decoded entries are re-quoted for
 *  the dock command line here, with no display involvement, so the
 *  parser can be exercised standalone (unit tests, fuzz harness).
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

#ifndef WM_XDNDURI_H_
#define WM_XDNDURI_H_

#include <WINGs/WUtil.h>

/* The URI split is the only unbounded loop over a hostile payload and
 * no real drag carries more than a few hundred entries, while the dock
 * consumption only needs the first one. */
#define XDND_MAX_URI_ENTRIES 256

/* Split a text/uri-list payload into entries. The payload is clobbered
 * (CR stripped, cut at LF). Appends wstrdup'd entries to items in
 * reverse payload order, decodes %xx escapes after a file:// prefix,
 * and stops with a warning after XDND_MAX_URI_ENTRIES entries. Returns
 * the buffer size the entries need once re-quoted, excluding the
 * terminator. */
size_t wXDNDParseURIList(char *payload, WMArray *items);

/* Append the file:// entries from items to out, in payload order (the
 * array is iterated in reverse). out must have room for the size
 * wXDNDParseURIList() returned plus one and is NUL-terminated. Every
 * entry is freed. Sets *file_count to the number of file:// entries
 * and *single_path to a wstrdup'd copy of the decoded path when that
 * count is exactly one, NULL otherwise. */
void wXDNDJoinQuotedList(WMArray *items, char *out, int *file_count,
			 char **single_path);

#endif
