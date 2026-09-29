/*
 *  xdnduri - pure text/uri-list parsing for the XDND dock drop.
 *  See xdnduri.h.
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
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include "wconfig.h"

#include <string.h>

#include <WINGs/WUtil.h>
#include <WINGs/WXProp.h>

#include "xdnduri.h"

static size_t wXDNDQuotedLength(const char *string)
{
	size_t length = 3;

	while (*string) {
		if (*string == '"' || *string == '\\')
			length++;
		length++;
		string++;
	}

	return length;
}

static void wXDNDAppendQuoted(char **dest, const char *string)
{
	char *p = *dest;

	*p++ = ' ';
	*p++ = '"';
	while (*string) {
		if (*string == '"' || *string == '\\')
			*p++ = '\\';
		*p++ = *string++;
	}
	*p++ = '"';
	*p = '\0';
	*dest = p;
}

size_t wXDNDParseURIList(char *payload, WMArray *items)
{
	size_t quoted = 0;
	int length;
	char *tmp;

	length = (int)strlen(payload);

	/* search in string */
	while (length--) {
		if (payload[length] == '\r') {	/* useless char, nuke it */
			payload[length] = 0;
		}
		if (payload[length] == '\n') {
			if (payload[length + 1] != '\0') {
				if (WMGetArrayItemCount(items) >= XDND_MAX_URI_ENTRIES) {
					wwarning("dropping the URI list after %d entries",
						 XDND_MAX_URI_ENTRIES);
					break;
				}
				tmp = wstrdup(&payload[length + 1]);
				if (!strncmp(tmp, "file://", 7)) {
					WXDndDecodeURI(&tmp[7]);
					quoted += wXDNDQuotedLength(&tmp[7]);
				}
				WMAddToArray(items, tmp);
			}
			payload[length] = 0;
		}
	}
	/* final one */
	if (WMGetArrayItemCount(items) < XDND_MAX_URI_ENTRIES) {
		tmp = wstrdup(payload);
		if (!strncmp(tmp, "file://", 7)) {
			WXDndDecodeURI(&tmp[7]);
			quoted += wXDNDQuotedLength(&tmp[7]);
		}
		WMAddToArray(items, tmp);
	}

	return quoted;
}

void wXDNDJoinQuotedList(WMArray *items, char *out, int *file_count,
			 char **single_path)
{
	WMArrayIterator iter;
	char *tmp;
	char *first = NULL;
	int count = 0;

	WM_ETARETI_ARRAY(items, tmp, iter) {
		/* only supporting file: URI objects */
		if (!strncmp(tmp, "file://", 7)) {
			wXDNDAppendQuoted(&out, &tmp[7]);
			count++;
			if (count == 1)
				first = wstrdup(&tmp[7]);
		}
		wfree(tmp);
	}

	*file_count = count;
	if (count == 1) {
		*single_path = first;
	} else {
		wfree(first);
		*single_path = NULL;
	}
}
