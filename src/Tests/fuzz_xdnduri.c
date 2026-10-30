/*
 *  fuzz_xdnduri.c
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

/* fuzz_xdnduri - libFuzzer harness for the XDND text/uri-list parse core
 *
 * Drives wXDNDParseURIList()/wXDNDJoinQuotedList() the way
 * wXDNDProcessSelection() does on a selection payload from the
 * dragging client, asserting the re-quoted output stays within the
 * size the split reported, under ASan/LSan.
 */

#include "xdnduri.h"

#include <WINGs/WUtil.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static volatile unsigned sink;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	WMArray *items;
	char *payload;
	char *out;
	char *single = NULL;
	int file_count = 0;
	size_t quoted;

	if (size > 64 * 1024 || memchr(data, 0, size))
		return 0;

	/* the selection payload arrives NUL-terminated from the
	 * property reader, build the same shape over the read-only
	 * fuzzer buffer */
	payload = wmalloc(size + 1);
	memcpy(payload, data, size);
	payload[size] = '\0';
	items = WMCreateArray(4);

	quoted = wXDNDParseURIList(payload, items);
	wfree(payload);

	out = wmalloc(quoted + 1);
	out[0] = '\0';
	wXDNDJoinQuotedList(items, out, &file_count, &single);
	WMFreeArray(items);

	/* the quoted rebuild must never grow past the reported size */
	if (strlen(out) > quoted)
		abort();
	sink += (unsigned)file_count + (unsigned)strlen(out);
	wfree(out);
	if (single) {
		sink += (unsigned)strlen(single);
		wfree(single);
	}

	return 0;
}
