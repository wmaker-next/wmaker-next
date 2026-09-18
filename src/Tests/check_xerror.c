/*
 *  WindowMaker headless unit tests for FormatXError() in src/xutil.c,
 *  the XErrorDB/XGetErrorText stand-in: the core error name table and
 *  the by-number fallback for extension errors.
 *
 *  Never opens a Display or a connection.
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include "wconfig.h"
#include <stdbool.h>

#include <stdio.h>
#include <string.h>

#include "xutil.h"

static int failures;

static void check(const char *what, bool ok)
{
	printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok)
		failures++;
}

static bool contains(const char *hay, const char *needle)
{
	return hay && strstr(hay, needle) != NULL;
}

static void fmt(unsigned int code, unsigned int major, char *buf, int size)
{
	FormatXError(code, major, 7, 0x1234, 77, buf, size);
}

static void test_core_names(void)
{
	char buf[512];

	fmt(1, 0, buf, sizeof(buf));
	check("core 1 is BadRequest", contains(buf, "BadRequest"));

	fmt(3, 0, buf, sizeof(buf));
	check("core 3 is BadWindow", contains(buf, "BadWindow"));

	fmt(8, 0, buf, sizeof(buf));
	check("core 8 is BadMatch", contains(buf, "BadMatch"));

	fmt(10, 0, buf, sizeof(buf));
	check("core 10 is BadAccess", contains(buf, "BadAccess"));

	fmt(17, 0, buf, sizeof(buf));
	check("core 17 is BadImplementation", contains(buf, "BadImplementation"));

	fmt(0, 0, buf, sizeof(buf));
	check("code 0 prints by number", contains(buf, "error code 0"));

	fmt(200, 0, buf, sizeof(buf));
	check("extension code prints by number", contains(buf, "error code 200"));
	check("no name for code 200", !contains(buf, "BadWindow"));
}

static void test_request_codes(void)
{
	char buf[512];

	fmt(3, 1, buf, sizeof(buf));
	check("major 1 is X_CreateWindow", contains(buf, "X_CreateWindow"));

	fmt(3, 42, buf, sizeof(buf));
	check("major 42 is X_SetInputFocus", contains(buf, "Request code: 42 X_SetInputFocus"));

	fmt(3, 999, buf, sizeof(buf));
	check("unknown major prints by number", contains(buf, "Request code: 999"));
	check("unknown major has no name", !contains(buf, "X_SetInputFocus"));
}

static void test_detail_fields(void)
{
	char buf[512];

	fmt(3, 14, buf, sizeof(buf));
	check("minor code present", contains(buf, "minor code: 7"));
	check("resource id present", contains(buf, "Resource ID: 0x1234"));
	check("error serial present", contains(buf, "Error serial: 77"));
}

static void test_small_buffer(void)
{
	char tiny[8];

	/* must not overrun or crash, whatever fits is fine */
	fmt(3, 14, tiny, (int) sizeof(tiny));
	check("small buffer NUL-terminated", memchr(tiny, 0, sizeof(tiny)) != NULL);

	fmt(3, 14, tiny, 1);
	check("1 byte buffer NUL-terminated", tiny[0] == '\0');
}

int main(void)
{
	test_core_names();
	test_request_codes();
	test_detail_fields();
	test_small_buffer();

	if (failures) {
		printf("%d FAILURES\n", failures);
		return 1;
	}
	printf("check_xerror: all tests passed\n");
	return 0;
}
