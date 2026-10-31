/*
 *  check_xkb_label.c
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

/*
 *  check_xkb_label - tests for the pure _XKB_RULES_NAMES field walk in
 *  xkb_label.c.
 */

#include <stdio.h>
#include <string.h>

#include "xkb_label.h"

#include "check_common.h"

static void label(const char *prop, size_t len, int group, char *out)
{
	wXkbExtractLanguageLabel(prop, len, group, out);
}

static void test_groups(void)
{
	static const char rules[] = "evdev\0pc105\0us,de\0\0";
	char got[3];

	label(rules, sizeof(rules) - 1, 0, got);
	CHECK_STR(got, "Us");
	label(rules, sizeof(rules) - 1, 1, got);
	CHECK_STR(got, "De");
	label(rules, sizeof(rules) - 1, 2, got);
	CHECK_STR(got, "");
	label(rules, sizeof(rules) - 1, 9, got);
	CHECK_STR(got, "");
}

static void test_single_field(void)
{
	static const char rules[] = "x\0y\0fr\0\0";
	char got[3];

	label(rules, sizeof(rules) - 1, 0, got);
	CHECK_STR(got, "Fr");
	label(rules, sizeof(rules) - 1, 1, got);
	CHECK_STR(got, "");
}

static void test_missing_fields(void)
{
	char got[3];

	label(NULL, 0, 0, got);
	CHECK_STR(got, "");

	label("", 0, 0, got);
	CHECK_STR(got, "");

	label("evdev", 5, 0, got);
	CHECK_STR(got, "");

	label("evdev\0pc105", 11, 0, got);
	CHECK_STR(got, "");

	label("evdev\0pc105\0", 12, 0, got);
	CHECK_STR(got, "");
}

static void test_truncated_token(void)
{
	static const char rules[] = "evdev\0pc105\0u";
	char got[3];

	label(rules, sizeof(rules) - 1, 0, got);
	CHECK_STR(got, "U");

	label(rules, 12, 0, got);
	CHECK_STR(got, "");
}

static void test_edge_punctuation(void)
{
	static const char rules[] = "evdev\0pc105\0,us,de\0\0";
	char got[3];

	label(rules, sizeof(rules) - 1, 0, got);
	CHECK_STR(got, "");
	label(rules, sizeof(rules) - 1, 1, got);
	CHECK_STR(got, "Us");
	label(rules, sizeof(rules) - 1, 2, got);
	CHECK_STR(got, "De");
}

int main(void)
{
	test_groups();
	test_single_field();
	test_missing_fields();
	test_truncated_token();
	test_edge_punctuation();

	return check_report("check_xkb_label");
}
