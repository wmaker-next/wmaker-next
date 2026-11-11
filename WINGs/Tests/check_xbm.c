/*
 *  WindowMaker headless unit tests for the XBM parser core
 *  (WXBitmapParseFile) and the XYPixmap wire repacker (WXBMPackWire)
 *  extracted from wxcore.c's wXReadBitmapFile/wXCreateBitmapFromData.
 *
 *  Never opens a Display or a connection.
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

#include "wconfig.h"
#include <stdbool.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <WINGs/WINGs.h>
#include <WINGs/WXCore.h>

static int failures;

static void check(const char *what, bool ok)
{
	printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok)
		failures++;
}

/* build a FILE* over a string via tmpfile, the fully portable way */
static FILE *bitmap_file(const char *content)
{
	FILE *f = tmpfile();
	size_t len = strlen(content);

	if (!f)
		return NULL;
	if (fwrite(content, 1, len, f) != len) {
		fclose(f);
		return NULL;
	}
	rewind(f);
	return f;
}

static void free_bitmap(WXBitmapData *bd)
{
	wfree(bd->data);
	bd->data = NULL;
}

static void test_valid_xbm(void)
{
	static const char file[] =
		"#define test_xbm_width 13\n"
		"#define test_xbm_height 2\n"
		"#define test_xbm_x_hot 3\n"
		"#define test_xbm_y_hot 1\n"
		"static unsigned char test_xbm_bits[] = {\n"
		"0x11, 0x02, 0xf0, 0x05};\n";
	WXBitmapData bd;
	FILE *f = bitmap_file(file);
	WXBitmapStatus st;

	check("valid xbm: parse", f != NULL);
	st = WXBitmapParseFile(f, &bd);
	fclose(f);
	check("valid xbm: status Success", st == WXBitmapSuccess);
	check("valid xbm: width", bd.width == 13);
	check("valid xbm: height", bd.height == 2);
	check("valid xbm: x_hot", bd.x_hot == 3);
	check("valid xbm: y_hot", bd.y_hot == 1);
	check("valid xbm: data_len", bd.data_len == 4);
	/* the 0x prefix must not double every value with a leading 0 */
	check("valid xbm: byte 0", bd.data && bd.data[0] == 0x11);
	check("valid xbm: byte 1", bd.data && bd.data[1] == 0x02);
	check("valid xbm: byte 2", bd.data && bd.data[2] == 0xf0);
	check("valid xbm: byte 3", bd.data && bd.data[3] == 0x05);
	free_bitmap(&bd);
}

static void test_no_hotspots(void)
{
	static const char file[] =
		"#define nh_width 8\n"
		"#define nh_height 1\n"
		"static char nh_bits[] = {\n"
		"0xaa};\n";
	WXBitmapData bd;
	FILE *f = bitmap_file(file);
	WXBitmapStatus st;

	st = WXBitmapParseFile(f, &bd);
	fclose(f);
	check("no hotspots: status Success", st == WXBitmapSuccess);
	check("no hotspots: x_hot is -1", bd.x_hot == -1);
	check("no hotspots: y_hot is -1", bd.y_hot == -1);
	check("no hotspots: byte", bd.data && bd.data[0] == 0xaa);
	check("no hotspots: data_len", bd.data_len == 1);
	free_bitmap(&bd);
}

static void test_x10_short_rejected(void)
{
	static const char file[] =
		"#define old_width 8\n"
		"#define old_height 1\n"
		"static short old_bits[] = {0x00aa};\n";
	WXBitmapData bd;
	FILE *f = bitmap_file(file);
	WXBitmapStatus st;

	st = WXBitmapParseFile(f, &bd);
	fclose(f);
	check("x10 short: rejected", st == WXBitmapFileInvalid);
	check("x10 short: no data", bd.data == NULL);
}

static void test_missing_define(void)
{
	static const char file[] =
		"static unsigned char bits[] = {0x00};\n";
	WXBitmapData bd;
	FILE *f = bitmap_file(file);
	WXBitmapStatus st;

	st = WXBitmapParseFile(f, &bd);
	fclose(f);
	check("missing #define: rejected", st == WXBitmapFileInvalid);
	check("missing #define: no data", bd.data == NULL);
}

static void test_missing_data(void)
{
	static const char file[] =
		"#define md_width 8\n"
		"#define md_height 1\n";
	WXBitmapData bd;
	FILE *f = bitmap_file(file);
	WXBitmapStatus st;

	st = WXBitmapParseFile(f, &bd);
	fclose(f);
	check("missing data block: rejected", st == WXBitmapFileInvalid);
	check("missing data block: no data", bd.data == NULL);
}

static void test_dimension_cap(void)
{
	static const char file[] =
		"#define huge_width 40000\n"
		"#define huge_height 1\n"
		"static unsigned char huge_bits[] = {0x00};\n";
	WXBitmapData bd;
	FILE *f = bitmap_file(file);
	WXBitmapStatus st;

	st = WXBitmapParseFile(f, &bd);
	fclose(f);
	check("dimension cap: rejected", st == WXBitmapFileInvalid);
	check("dimension cap: no data", bd.data == NULL);
}

static void test_malformed_define_skipped(void)
{
	/* the malformed #define line is skipped, the good one counts */
	static const char file[] =
		"#define\n"
		"#define bad_width x\n"
		"#define good_width 8\n"
		"#define good_height 1\n"
		"static unsigned char bits[] = {\n"
		"0x0f};\n";
	WXBitmapData bd;
	FILE *f = bitmap_file(file);
	WXBitmapStatus st;

	st = WXBitmapParseFile(f, &bd);
	fclose(f);
	check("malformed define: status Success", st == WXBitmapSuccess);
	check("malformed define: width", bd.width == 8);
	check("malformed define: byte", bd.data && bd.data[0] == 0x0f);
	free_bitmap(&bd);
}

static void test_pack_wire(void)
{
	static const char bits13[4] = { 0x11, 0x02, 0xf0, 0x05 };
	static const char bits8[1] = { 0x81 };
	size_t len;
	uint8_t *w;

	/* 13 bits = 2 byte-packed, 4 byte wire rows */
	w = WXBMPackWire(bits13, 13, 2, &len);
	check("pack wire: length", w != NULL && len == 8);
	check("pack wire: row 0 byte 0", w && w[0] == 0x11);
	check("pack wire: row 0 byte 1", w && w[1] == 0x02);
	check("pack wire: row 0 pad", w && w[2] == 0 && w[3] == 0);
	check("pack wire: row 1 byte 0", w && w[4] == 0xf0);
	check("pack wire: row 1 byte 1", w && w[5] == 0x05);
	check("pack wire: row 1 pad", w && w[6] == 0 && w[7] == 0);
	wfree(w);

	/* 8 bits still pad to a 4 byte wire row */
	w = WXBMPackWire(bits8, 8, 1, &len);
	check("pack wire 8bit: length", w != NULL && len == 4);
	check("pack wire 8bit: byte", w && w[0] == 0x81);
	check("pack wire 8bit: pad", w && w[1] == 0 && w[2] == 0 && w[3] == 0);
	wfree(w);

	/* degenerate inputs pack nothing */
	w = WXBMPackWire(bits8, 8, 0, &len);
	check("pack wire zero rows: NULL", w == NULL && len == 0);
	w = WXBMPackWire(bits8, 0, 1, &len);
	check("pack wire zero width: NULL", w == NULL && len == 0);
}

int main(void)
{
	test_valid_xbm();
	test_no_hotspots();
	test_x10_short_rejected();
	test_missing_define();
	test_missing_data();
	test_dimension_cap();
	test_malformed_define_skipped();
	test_pack_wire();

	if (failures) {
		printf("%d FAILURES\n", failures);
		return 1;
	}
	printf("check_xbm: all tests passed\n");
	return 0;
}
