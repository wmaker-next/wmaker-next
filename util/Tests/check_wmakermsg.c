/*
 *  check_wmakermsg.c
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
 * check_wmakermsg - exec `wmakermsg --get-socketpath` under a private
 * XDG_RUNTIME_DIR and DISPLAY, and assert the printed path matches the
 * one returned by wIPCSocketPath(NULL) with the same environment.
 *
 * Links only libWUtil (for wIPCSocketPath and wspawnvp_capture), no X.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <WINGs/WUtil.h>
#include <WINGs/WIPC.h>
#include "check_common.h"

static int has_suffix(const char *s, const char *suffix)
{
	size_t sl = strlen(s), xl = strlen(suffix);
	return sl >= xl && strcmp(s + sl - xl, suffix) == 0;
}

static void chomp(char *s)
{
	size_t len = strlen(s);
	while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r'))
		s[--len] = '\0';
}

int main(void)
{
	const char *top_builddir;
	char wmakermsg_path[1024];
	char *expected, *out;
	int st;

	top_builddir = getenv("top_builddir");
	if (!top_builddir)
		top_builddir = ".";
	snprintf(wmakermsg_path, sizeof(wmakermsg_path),
		 "%s/util/wmakermsg", top_builddir);

	/* Set a private XDG_RUNTIME_DIR and DISPLAY so wIPCSocketPath
	 * resolves deterministically without touching the real user state. */
	setenv("XDG_RUNTIME_DIR", "/tmp/check_wmakermsg_runtime", 1);
	setenv("DISPLAY", ":31", 1);
	unsetenv("WMAKER_IPC_SOCKET");

	/* Compute the expected path with the same environment. */
	expected = wIPCSocketPath(NULL);
	CHECK(expected != NULL);
	CHECK(strlen(expected) > 0);
	CHECK(has_suffix(expected, "/wmaker/ipc._31_0"));
	CHECK(strncmp(expected, "/tmp/check_wmakermsg_runtime/", 29) == 0);

	/* Exec the CLI binary and capture its stdout. */
	out = wspawnvp_capture(
		(char *const[]){ wmakermsg_path, "--get-socketpath", NULL },
		1024, &st);
	CHECK(st == 0);
	CHECK(out != NULL);
	chomp(out);
	CHECK_STR(out, expected);

	wfree(expected);
	wfree(out);

	/* WMAKER_IPC_SOCKET override beats everything. */
	setenv("WMAKER_IPC_SOCKET", "/tmp/check_wmakermsg.override", 1);
	expected = wIPCSocketPath(NULL);
	CHECK_STR(expected, "/tmp/check_wmakermsg.override");

	out = wspawnvp_capture(
		(char *const[]){ wmakermsg_path, "--get-socketpath", NULL },
		1024, &st);
	CHECK(st == 0);
	CHECK(out != NULL);
	chomp(out);
	CHECK_STR(out, "/tmp/check_wmakermsg.override");

	wfree(expected);
	wfree(out);

	return check_report("check_wmakermsg");
}
