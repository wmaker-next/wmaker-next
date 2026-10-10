/*
 *  xsettingswire - pure _XSETTINGS_SETTINGS wire codec. The payload is
 *  a byte-order marker, a serial, a count and NUL-free length-prefixed
 *  settings, all untrusted as soon as a foreign manager's window is
 *  read, so every length here is bounds-checked before it is used and
 *  the split keeps the codec free of any display dependency so it can
 *  be exercised standalone (unit tests, fuzz harness).
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

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>

#include <WINGs/WUtil.h>

#include "xsettings.h"

/* header: byte order + 3 pad + serial + count */
#define HDR_SIZE 12

static size_t pad4(size_t n)
{
	return (4 - (n & 3)) & 3;
}

static void put16(unsigned char *p, uint16_t v)
{
	p[0] = (unsigned char)(v & 0xff);
	p[1] = (unsigned char)(v >> 8);
}

static void put32(unsigned char *p, uint32_t v)
{
	p[0] = (unsigned char)(v & 0xff);
	p[1] = (unsigned char)((v >> 8) & 0xff);
	p[2] = (unsigned char)((v >> 16) & 0xff);
	p[3] = (unsigned char)(v >> 24);
}

static uint16_t get16(const unsigned char *p, bool be)
{
	if (be)
		return (uint16_t)((p[0] << 8) | p[1]);
	return (uint16_t)((p[1] << 8) | p[0]);
}

static uint32_t get32(const unsigned char *p, bool be)
{
	if (be)
		return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
		       ((uint32_t)p[2] << 8) | (uint32_t)p[3];
	return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[1] << 8) | (uint32_t)p[0];
}

size_t wXSettingsEncodedSize(const WXSetting *set, size_t n)
{
	size_t total, i;

	if (!set || n == 0 || n > WXSETTINGS_MAX)
		return 0;

	total = HDR_SIZE;
	for (i = 0; i < n; i++) {
		size_t nlen, vlen;

		if (!set[i].name)
			return 0;
		nlen = strlen(set[i].name);
		if (nlen == 0 || nlen > UINT16_MAX)
			return 0;

		total += 4 + nlen + pad4(4 + nlen) + 4;
		switch (set[i].type) {
		case WXSETTINGS_TYPE_INTEGER:
			total += 4;
			break;
		case WXSETTINGS_TYPE_STRING:
			if (!set[i].sval)
				return 0;
			vlen = strlen(set[i].sval);
			if (vlen > WXSETTINGS_MAX_BYTES)
				return 0;
			/* value-len on the wire is a CARD32 */
			total += 4 + vlen + pad4(4 + vlen);
			break;
		case WXSETTINGS_TYPE_COLOR:
			total += 8;
			break;
		default:
			return 0;
		}

		if (total > WXSETTINGS_MAX_BYTES)
			return 0;
	}

	return total;
}

int wXSettingsEncode(const WXSetting *set, size_t n, uint32_t serial,
		     unsigned char **out, size_t *outlen)
{
	size_t size, i, o;

	size = wXSettingsEncodedSize(set, n);
	if (size == 0)
		return WXSETTINGS_E_ARGS;

	*out = wmalloc(size);
	*outlen = size;

	/* little-endian wire form, the byte-order field carries the X
	 * protocol byte-order constants, LSBFirst(0) here */
	(*out)[0] = 0;
	(*out)[1] = 0;
	(*out)[2] = 0;
	(*out)[3] = 0;
	put32(*out + 4, serial);
	put32(*out + 8, (uint32_t)n);

	o = HDR_SIZE;
	for (i = 0; i < n; i++) {
		size_t nlen = strlen(set[i].name);

		(*out)[o++] = set[i].type;
		(*out)[o++] = 0;
		put16(*out + o, (uint16_t)nlen);
		o += 2;
		memcpy(*out + o, set[i].name, nlen);
		o += nlen;
		o += pad4(o);
		put32(*out + o, set[i].last_change);
		o += 4;

		switch (set[i].type) {
		case WXSETTINGS_TYPE_INTEGER:
			put32(*out + o, set[i].ival);
			o += 4;
			break;
		case WXSETTINGS_TYPE_STRING:
			{
				size_t vlen = strlen(set[i].sval);

				/* value-len on the wire is a CARD32 */
				put32(*out + o, (uint32_t)vlen);
				o += 4;
				memcpy(*out + o, set[i].sval, vlen);
				o += vlen;
				o += pad4(o);
			}
			break;
		case WXSETTINGS_TYPE_COLOR:
			/* opaque black: 4 16-bit channels */
			memset(*out + o, 0, 8);
			o += 8;
			break;
		}
	}

	return WXSETTINGS_OK;
}

int wXSettingsDecode(const unsigned char *buf, size_t len,
		     uint32_t *serial, WXSetting **set, size_t *n)
{
	bool be;
	uint32_t count, i;
	size_t o, k;
	WXSetting *out;

	if (!buf || !serial || !set || !n)
		return WXSETTINGS_E_ARGS;

	*set = NULL;
	*n = 0;

	if (len < HDR_SIZE || len > WXSETTINGS_MAX_BYTES)
		return WXSETTINGS_E_MALF;

	/* the byte-order field carries the X protocol constants,
	 * LSBFirst(0) / MSBFirst(1) */
	if (buf[0] == 0)
		be = false;
	else if (buf[0] == 1)
		be = true;
	else
		return WXSETTINGS_E_MALF;

	*serial = get32(buf + 4, be);
	count = get32(buf + 8, be);
	if (count > WXSETTINGS_MAX)
		return WXSETTINGS_E_MALF;

	out = wmalloc((count + 1) * sizeof(WXSetting));
	o = HDR_SIZE;
	for (i = 0; i < count; i++) {
		unsigned int type;
		uint16_t nlen;
		uint32_t vlen;

		if (o + 4 > len)
			goto malformed;
		type = buf[o];
		nlen = get16(buf + o + 2, be);
		o += 4;
		if (o + nlen + pad4(o + nlen) > len || o + nlen > len)
			goto malformed;
		out[i].name = wmalloc((size_t)nlen + 1);
		memcpy(out[i].name, buf + o, nlen);
		out[i].name[nlen] = '\0';
		o += nlen;
		o += pad4(o);

		if (o + 4 > len)
			goto malformed;
		out[i].last_change = get32(buf + o, be);
		o += 4;

		switch (type) {
		case WXSETTINGS_TYPE_INTEGER:
			if (o + 4 > len)
				goto malformed;
			out[i].ival = get32(buf + o, be);
			o += 4;
			break;
		case WXSETTINGS_TYPE_STRING:
			/* value-len on the wire is a CARD32 */
			if (o + 4 > len)
				goto malformed;
			vlen = get32(buf + o, be);
			o += 4;
			if (vlen > len - o)
				goto malformed;
			out[i].sval = wmalloc((size_t)vlen + 1);
			memcpy(out[i].sval, buf + o, vlen);
			out[i].sval[vlen] = '\0';
			o += vlen;
			o += pad4(o);
			break;
		case WXSETTINGS_TYPE_COLOR:
			if (o + 8 > len)
				goto malformed;
			o += 8;
			break;
		default:
			goto malformed;
		}

		out[i].type = (uint8_t)type;
	}

	*set = out;
	*n = count;
	return WXSETTINGS_OK;

malformed:
	for (k = 0; k <= i; k++) {
		wfree(out[k].name);
		wfree(out[k].sval);
	}
	wfree(out);
	return WXSETTINGS_E_MALF;
}

void wXSettingsFreeDecoded(WXSetting *set, size_t n)
{
	size_t i;

	if (!set)
		return;
	for (i = 0; i < n; i++) {
		wfree(set[i].name);
		wfree(set[i].sval);
	}
	wfree(set);
}

bool wXSettingsPangoFontName(const char *spec, double dpi,
			     char *out, size_t outlen)
{
	char family[256];
	size_t i, flen;
	bool bold = false;
	double px = 0.0;
	const char *p;
	int n, pt;

	if (!spec || !out || outlen < 2)
		return false;

	/* family runs to the first ':', trimmed */
	p = spec;
	while (*p == ' ' || *p == '\t')
		p++;
	flen = 0;
	while (p[flen] && p[flen] != ':' && flen < sizeof(family) - 1) {
		family[flen] = p[flen];
		flen++;
	}
	while (flen > 0 && (family[flen - 1] == ' ' || family[flen - 1] == '\t'))
		flen--;
	family[flen] = '\0';
	if (flen == 0)
		return false;

	/* scan the remaining components for a weight and a size */
	p = spec;
	while ((p = strchr(p, ':')) != NULL) {
		p++;
		if (strncasecmp(p, "bold", 4) == 0 && (p[4] == ':' || p[4] == '\0'))
			bold = true;
		else if (strncasecmp(p, "pixelsize=", 10) == 0)
			px = strtod(p + 10, NULL);
	}

	n = snprintf(out, outlen, "%s%s", family, bold ? " Bold" : "");
	if (n < 0 || (size_t)n >= outlen)
		return false;

	if (px <= 0.0)
		return true;

	if (dpi <= 0.0)
		dpi = 96.0;
	pt = (int)(px * 72.0 / dpi + 0.5);
	if (pt <= 0)
		return false;

	i = (size_t)n;
	n = snprintf(out + i, outlen - i, " %d", pt);
	if (n < 0 || (size_t)n >= outlen - i)
		return false;

	return true;
}
