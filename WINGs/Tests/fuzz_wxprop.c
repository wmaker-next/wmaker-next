/*
 *  fuzz_wxprop.c
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, see <https://www.gnu.org/licenses/>.
 */

/* fuzz_wxprop - libFuzzer harness for the WXProp property decode core
 *
 * The WXRcv* readers split into a fetch half bound to the connection
 * and a pure decode half over an already-fetched payload. The harness
 * feeds arbitrary bytes as a synthetic WXPropValue (type, format,
 * bytes_after, value) and drives the validate/decode half the same way
 * the checked reply path does, with no X server involved.
 */

#include <WINGs/WUtil.h>
#include <WINGs/WXProp.h>

#include <stdint.h>
#include <string.h>

static volatile unsigned sink;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	WXPropValue v;
	const uint8_t *payload;
	size_t payload_len;
	uint32_t nitems;
	uint32_t out[4];

	if (size < 13)
		return 0;

	memcpy(&v.type, data, 4);
	v.format = data[4] % 33;
	memcpy(&v.bytes_after, data + 5, 4);
	memcpy(&v.value_len, data + 9, 4);
	payload = data + 13;
	payload_len = size - 13;

	if (v.value_len > payload_len)
		v.value_len = (uint32_t)payload_len;
	v.value = payload;

	/* the checks the reply path runs, over hostile shapes */
	if (WXPropValidate(&v, 32, 1, &nitems))
		sink += nitems;
	if (WXPropValidate(&v, 8, 0, &nitems))
		sink += nitems;
	if (WXPropValidate(&v, 0, 0, &nitems))
		sink += nitems;

	/* format-32 fixed read: the decoder itself rejects short payloads */
	if (WXDecodeFormat32N(&v, out, 4))
		sink += out[0] + out[1] + out[2] + out[3];

	/* list and string allocations round-trip under ASan/LSan */
	{
		size_t count = 0;
		uint32_t *list = WXDecodeFormat32List(&v, &count);

		if (list) {
			size_t i;

			for (i = 0; i < count; i++)
				sink += list[i];
			wfree(list);
		}
	}
	{
		size_t len = 0;
		char *s = WXDecodeString(&v, &len);

		if (s) {
			sink += (unsigned)strlen(s) + len;
			wfree(s);
		}
	}

	return 0;
}
