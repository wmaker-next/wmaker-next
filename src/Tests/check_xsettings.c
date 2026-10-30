/*
 *  check_xsettings.c
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

/*
 *  check_xsettings - tests for the _XSETTINGS_SETTINGS wire codec in
 *  xsettingswire.c.
 *
 *  Pattern follows check_calc.c: compile the test + ../xsettingswire.c
 *  and link against libWUtil.la, no X server involved.
 */

#include "wconfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <WINGs/WUtil.h>

#include "xsettings.h"

#include "check_common.h"

static void put_le16(unsigned char *p, uint16_t v)
{
	p[0] = (unsigned char)(v & 0xff);
	p[1] = (unsigned char)(v >> 8);
}

static void put_le32(unsigned char *p, uint32_t v)
{
	put_le16(p, (uint16_t)(v & 0xffff));
	put_le16(p + 2, (uint16_t)(v >> 16));
}

static void put_be16(unsigned char *p, uint16_t v)
{
	p[0] = (unsigned char)(v >> 8);
	p[1] = (unsigned char)(v & 0xff);
}

static void put_be32(unsigned char *p, uint32_t v)
{
	put_be16(p, (uint16_t)(v >> 16));
	put_be16(p + 2, (uint16_t)(v & 0xffff));
}

static uint32_t le32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void test_encode_layout(void)
{
	WXSetting set[1];
	unsigned char *buf;
	size_t len;

	set[0].name = (char *)"Xft/DPI";
	set[0].type = WXSETTINGS_TYPE_INTEGER;
	set[0].ival = 96;
	set[0].sval = NULL;
	set[0].last_change = 7;

	/* header 12 + (4 + 7 + pad 1 + 4 + 4) */
	CHECK(wXSettingsEncodedSize(set, 1) == 32);
	CHECK(wXSettingsEncode(set, 1, 7, &buf, &len) == WXSETTINGS_OK);
	CHECK(buf != NULL && len == 32);
	/* little-endian wire form, byte-order field LSBFirst */
	CHECK(buf[0] == 0);
	CHECK(buf[1] == 0 && buf[2] == 0 && buf[3] == 0);
	CHECK(le32(buf + 4) == 7);
	CHECK(le32(buf + 8) == 1);
	CHECK(buf[12] == WXSETTINGS_TYPE_INTEGER);
	CHECK(buf[13] == 0);
	CHECK(buf[14] == 7 && buf[15] == 0);
	CHECK(memcmp(buf + 16, "Xft/DPI", 7) == 0);
	/* pad to the 4-byte boundary, then the per-setting serial */
	CHECK(buf[23] == 0);
	CHECK(le32(buf + 24) == 7);
	CHECK(le32(buf + 28) == 96);
	wfree(buf);
}

static void test_encode_string_layout(void)
{
	WXSetting set[1];
	unsigned char *buf;
	size_t len;

	set[0].name = (char *)"Gtk/FontName";
	set[0].type = WXSETTINGS_TYPE_STRING;
	set[0].ival = 0;
	set[0].sval = (char *)"Sans Bold 9";
	set[0].last_change = 3;

	/* 12 + (4 + 12 + pad 0 + 4) + (4 + 11 + pad 1) */
	CHECK(wXSettingsEncodedSize(set, 1) == 48);
	CHECK(wXSettingsEncode(set, 1, 3, &buf, &len) == WXSETTINGS_OK);
	CHECK(buf != NULL && len == 48);
	CHECK(buf[12] == WXSETTINGS_TYPE_STRING);
	CHECK(buf[14] == 12 && buf[15] == 0);
	CHECK(memcmp(buf + 16, "Gtk/FontName", 12) == 0);
	CHECK(le32(buf + 28) == 3);
	/* value length after the serial, then the value itself */
	CHECK(le32(buf + 32) == 11);
	CHECK(memcmp(buf + 36, "Sans Bold 9", 11) == 0);
	wfree(buf);
}

static void test_roundtrip(void)
{
	WXSetting set[2];
	WXSetting *out;
	uint32_t serial;
	size_t n, i;
	unsigned char *buf;
	size_t len;

	set[0].name = (char *)"Xft/DPI";
	set[0].type = WXSETTINGS_TYPE_INTEGER;
	set[0].ival = 144;
	set[0].sval = NULL;
	set[0].last_change = 2;
	set[1].name = (char *)"Gtk/FontName";
	set[1].type = WXSETTINGS_TYPE_STRING;
	set[1].ival = 0;
	set[1].sval = (char *)"Liberation Sans Bold 15";
	set[1].last_change = 2;

	CHECK(wXSettingsEncode(set, 2, 2, &buf, &len) == WXSETTINGS_OK);
	CHECK(wXSettingsDecode(buf, len, &serial, &out, &n) == WXSETTINGS_OK);
	CHECK(serial == 2);
	CHECK(n == 2);
	for (i = 0; i < 2; i++) {
		CHECK_STR(out[i].name, set[i].name);
		CHECK(out[i].type == set[i].type);
		CHECK(out[i].last_change == set[i].last_change);
		if (set[i].type == WXSETTINGS_TYPE_INTEGER)
			CHECK(out[i].ival == set[i].ival);
		else
			CHECK_STR(out[i].sval, set[i].sval);
	}
	wXSettingsFreeDecoded(out, n);
	wfree(buf);
}

static void test_roundtrip_empty(void)
{
	WXSetting set[1];
	WXSetting *out;
	unsigned char *buf;
	uint32_t serial;
	size_t len, n;

	set[0].name = (char *)"k";
	set[0].type = WXSETTINGS_TYPE_INTEGER;
	set[0].ival = 0;
	set[0].sval = NULL;
	set[0].last_change = 0;
	CHECK(wXSettingsEncodedSize(set, 0) == 0);
	CHECK(wXSettingsEncode(set, 0, 1, &buf, &len) == WXSETTINGS_E_ARGS);

	/* a hand-built count=0 payload decodes to an empty table */
	buf = wmalloc(12);
	memset(buf, 0, 12);
	buf[0] = 0;
	put_le32(buf + 4, 5);
	put_le32(buf + 8, 0);
	CHECK(wXSettingsDecode(buf, 12, &serial, &out, &n) == WXSETTINGS_OK);
	CHECK(serial == 5 && n == 0);
	wXSettingsFreeDecoded(out, n);
	wfree(buf);
}

static void test_decode_bigendian(void)
{
	/* hand-build a big-endian payload with one integer setting */
	unsigned char buf[12 + 16];
	WXSetting *out;
	uint32_t serial;
	size_t n;

	memset(buf, 0, sizeof(buf));
	buf[0] = 1;
	put_be32(buf + 4, 42);
	put_be32(buf + 8, 1);
	buf[12] = WXSETTINGS_TYPE_INTEGER;
	put_be16(buf + 14, 4);
	memcpy(buf + 16, "abcd", 4);
	/* the name ends 4-aligned, no padding, then the per-setting serial
	 * at 20 and the value at 24 */
	put_be32(buf + 20, 9);		/* last change */
	put_be32(buf + 24, 300);	/* value */

	CHECK(wXSettingsDecode(buf, sizeof(buf), &serial, &out, &n) == WXSETTINGS_OK);
	CHECK(serial == 42);
	CHECK(n == 1);
	CHECK_STR(out[0].name, "abcd");
	CHECK(out[0].type == WXSETTINGS_TYPE_INTEGER);
	CHECK(out[0].ival == 300);
	CHECK(out[0].last_change == 9);
	wXSettingsFreeDecoded(out, n);
}

static void test_decode_malformed(void)
{
	WXSetting set[1];
	WXSetting *out;
	unsigned char *buf;
	uint32_t serial;
	size_t len, n;
	char *big;

	/* integer setting "Xft/DPI" = 96, serial 1 */
	set[0].name = (char *)"Xft/DPI";
	set[0].type = WXSETTINGS_TYPE_INTEGER;
	set[0].ival = 96;
	set[0].sval = NULL;
	set[0].last_change = 0;
	CHECK(wXSettingsEncode(set, 1, 1, &buf, &len) == WXSETTINGS_OK);
	CHECK(len == 32);

	/* truncated header, bad marker, count overflow, unknown type */
	CHECK(wXSettingsDecode(buf, 11, &serial, &out, &n) == WXSETTINGS_E_MALF);
	buf[0] = 'X';
	CHECK(wXSettingsDecode(buf, len, &serial, &out, &n) == WXSETTINGS_E_MALF);
	buf[0] = 0;
	put_le32(buf + 8, WXSETTINGS_MAX + 1);
	CHECK(wXSettingsDecode(buf, len, &serial, &out, &n) == WXSETTINGS_E_MALF);
	put_le32(buf + 8, 1);
	buf[12] = 3;
	CHECK(wXSettingsDecode(buf, len, &serial, &out, &n) == WXSETTINGS_E_MALF);

	/* name runs past the payload */
	buf[12] = WXSETTINGS_TYPE_INTEGER;
	buf[14] = 0xff;
	buf[15] = 0xff;
	CHECK(wXSettingsDecode(buf, len, &serial, &out, &n) == WXSETTINGS_E_MALF);
	wfree(buf);

	/* the integer value truncated away */
	set[0].name = (char *)"ab";
	CHECK(wXSettingsEncode(set, 1, 1, &buf, &len) == WXSETTINGS_OK);
	CHECK(len == 28);
	CHECK(wXSettingsDecode(buf, 27, &serial, &out, &n) == WXSETTINGS_E_MALF);
	CHECK(wXSettingsDecode(buf, len, &serial, &out, &n) == WXSETTINGS_OK);
	CHECK(out[0].ival == 96);
	wXSettingsFreeDecoded(out, n);
	wfree(buf);

	/* string setting with a value length claiming past the payload */
	set[0].name = (char *)"ab";
	set[0].type = WXSETTINGS_TYPE_STRING;
	set[0].sval = (char *)"xyz";
	CHECK(wXSettingsEncode(set, 1, 1, &buf, &len) == WXSETTINGS_OK);
	/* 12 + (4 + 2 + pad 2 + 4) + (4 + 3 + pad 1) */
	CHECK(len == 32);
	/* 27 truncates the value: name 16..17, pad to 20, serial 20..23,
	 * value length 24..27, value 28..30 */
	CHECK(wXSettingsDecode(buf, 27, &serial, &out, &n) == WXSETTINGS_E_MALF);
	/* 31 only cuts the trailing padding byte, that still parses */
	CHECK(wXSettingsDecode(buf, 31, &serial, &out, &n) == WXSETTINGS_OK);
	CHECK_STR(out[0].sval, "xyz");
	wXSettingsFreeDecoded(out, n);
	/* the value length sits at offset 24, last change 20..23 */
	buf[24] = 0x10;
	CHECK(wXSettingsDecode(buf, len, &serial, &out, &n) == WXSETTINGS_E_MALF);
	wfree(buf);

	/* oversized payload and name rejected by the size bound */
	set[0].name = (char *)"k";
	set[0].type = WXSETTINGS_TYPE_STRING;
	big = wmalloc(70001);
	memset(big, 'x', 70000);
	big[70000] = '\0';
	set[0].sval = big;
	CHECK(wXSettingsEncodedSize(set, 1) == 0);
	set[0].sval = (char *)"x";
	set[0].name = big;
	CHECK(wXSettingsEncodedSize(set, 1) == 0);
	wfree(big);

	/* bad arguments: an integer setting encodes, a NULL name does not */
	set[0].name = (char *)"k";
	set[0].type = WXSETTINGS_TYPE_INTEGER;
	set[0].sval = NULL;
	CHECK(wXSettingsEncode(NULL, 1, 1, &buf, &len) == WXSETTINGS_E_ARGS);
	CHECK(wXSettingsDecode(NULL, 10, &serial, &out, &n) == WXSETTINGS_E_ARGS);
	CHECK(wXSettingsEncode(set, 1, 1, &buf, &len) == WXSETTINGS_OK);
	CHECK(wXSettingsDecode(buf, len, NULL, &out, &n) == WXSETTINGS_E_ARGS);
	wfree(buf);
}

static void test_pango_font_name(void)
{
	char buf[128];

	CHECK(wXSettingsPangoFontName("Sans:bold:pixelsize=12", 96.0, buf, sizeof(buf)));
	CHECK_STR(buf, "Sans Bold 9");
	CHECK(wXSettingsPangoFontName("sans serif", 96.0, buf, sizeof(buf)));
	CHECK_STR(buf, "sans serif");
	CHECK(wXSettingsPangoFontName("DejaVu Sans Mono", 120.0, buf, sizeof(buf)));
	CHECK_STR(buf, "DejaVu Sans Mono");
	CHECK(wXSettingsPangoFontName("Liberation Sans:pixelsize=14", 96.0, buf, sizeof(buf)));
	CHECK_STR(buf, "Liberation Sans 11");
	CHECK(wXSettingsPangoFontName("Foo:medium:pixelsize=10", 96.0, buf, sizeof(buf)));
	CHECK_STR(buf, "Foo 8");
	CHECK(wXSettingsPangoFontName("Foo:Pixelsize=8", 96.0, buf, sizeof(buf)));
	CHECK_STR(buf, "Foo 6");
	CHECK(wXSettingsPangoFontName("Foo:antialias=false", 96.0, buf, sizeof(buf)));
	CHECK_STR(buf, "Foo");
	CHECK(wXSettingsPangoFontName("Foo", 0.0, buf, sizeof(buf)));
	CHECK_STR(buf, "Foo");
	CHECK(wXSettingsPangoFontName("  Helvetica  ", 96.0, buf, sizeof(buf)));
	CHECK_STR(buf, "Helvetica");

	/* no usable family, buffer too small */
	CHECK(!wXSettingsPangoFontName(":bold:pixelsize=12", 96.0, buf, sizeof(buf)));
	CHECK(!wXSettingsPangoFontName("   ", 96.0, buf, sizeof(buf)));
	CHECK(!wXSettingsPangoFontName("SomeVeryLongFamilyNameThatDoesNotFit", 96.0,
				       buf, 8));
}

int main(void)
{
	test_encode_layout();
	test_encode_string_layout();
	test_roundtrip();
	test_roundtrip_empty();
	test_decode_bigendian();
	test_decode_malformed();
	test_pango_font_name();

	return check_report("check_xsettings");
}
