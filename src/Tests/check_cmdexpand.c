/*
 *  check_cmdexpand.c
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
 * check_cmdexpand - tests for ExpandOptions() extracted to cmdexpand.c.
 *
 * Pattern follows check_wdefaults.c: compile the test + ../cmdexpand.c,
 * provide stubs for getselection/getuserinput, link against libWUtil.la.
 */

#include "wconfig.h"
#include <stdbool.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "WindowMaker.h"
#include "screen.h"
#include "window.h"
#include "misc.h"

#include "check_common.h"

/* --- stubs for cmdexpand.c externs not exercised here --- */

/* stub: returns a fixed selection string for %s testing */
char *getselection(WScreen *scr)
{
	(void)scr;
	return wstrdup("/test/selection/path");
}

/* stub: returns a heap string so the %a ownership transfer is testable;
 * ASAN reports it as leaked if ExpandOptions fails to free it. A prompt
 * containing "cancel" simulates the user aborting the dialog (NULL).
 * Like the real getuserinput(), leave *ptr on the closing parenthesis. */
char *getuserinput(WScreen *scr, const char *line, int *ptr, bool advanced)
{
	(void)scr;
	(void)advanced;
	while (line[*ptr] != '\0' && line[*ptr] != ')')
		(*ptr)++;
	if (strstr(line, "cancel") != NULL)
		return NULL;
	return wstrdup("typed input");
}

/* --- test fixture --- */

static WScreen scr;

static void reset_scr(void)
{
	memset(&scr, 0, sizeof(scr));
}

/* --- tests --- */

static void test_plain_text(void)
{
	char *out;

	reset_scr();
	out = ExpandOptions(&scr, "hello world");
	CHECK(out != NULL);
	CHECK(out && strcmp(out, "hello world") == 0);
	if (out)
		free(out);
}

#ifdef USE_DOCK_XDND
static void test_dnd_without_gnustep(void)
{
	char *out;

	reset_scr();
	scr.xdestring = " \"file1\" \"file2\"";
	out = ExpandOptions(&scr, "app %d");
	CHECK(out != NULL);
	CHECK(out && strcmp(out, "app  \"file1\" \"file2\"") == 0);
	if (out)
		free(out);
}

static void test_dnd_with_gnustep(void)
{
	char *out;

	reset_scr();
	scr.xdestring = " \"file1\" \"file2\"";
	scr.xdest_is_gnustep = 1;
	out = ExpandOptions(&scr, "app %d");
	CHECK(out != NULL);
	CHECK(out && strcmp(out, "app -GSFilePath  \"file1\" \"file2\"") == 0);
	if (out)
		free(out);
}

static void test_dnd_single_file_gnustep(void)
{
	char *out;

	reset_scr();
	scr.xdestring = " \"file.txt\"";
	scr.xdest_is_gnustep = 1;
	out = ExpandOptions(&scr, "app %d");
	CHECK(out != NULL);
	CHECK(out && strcmp(out, "app -GSFilePath  \"file.txt\"") == 0);
	if (out)
		free(out);
}

static void test_dnd_null_xdestring(void)
{
	char *out;

	reset_scr();
	scr.xdestring = NULL;
	out = ExpandOptions(&scr, "app %d");
	CHECK(out == NULL);
	CHECK(scr.flags.dnd_data_convertion_status == 1);
	if (out)
		free(out);
}
#endif	/* USE_DOCK_XDND */

static void test_selection_without_gnustep(void)
{
	char *out;

	reset_scr();
	out = ExpandOptions(&scr, "app %s");
	CHECK(out != NULL);
	CHECK(out && strcmp(out, "app /test/selection/path") == 0);
	if (out)
		free(out);
}

#ifdef USE_DOCK_XDND
static void test_selection_with_gnustep(void)
{
	char *out;

	reset_scr();
	scr.xdest_is_gnustep = 1;
	out = ExpandOptions(&scr, "app %s");
	CHECK(out != NULL);
	CHECK(out && strcmp(out, "app -GSFilePath /test/selection/path") == 0);
	if (out)
		free(out);
}
#endif	/* USE_DOCK_XDND */

static void test_escaped_percent(void)
{
	char *out;

	reset_scr();
	out = ExpandOptions(&scr, "app \\%d");
	CHECK(out != NULL);
	CHECK(out && strcmp(out, "app %d") == 0);
	if (out)
		free(out);
}

static void test_workspace(void)
{
	char *out;

	reset_scr();
	scr.current_workspace = 0;
	out = ExpandOptions(&scr, "app %W");
	CHECK(out != NULL);
	CHECK(out && strcmp(out, "app 0x1") == 0);
	if (out)
		free(out);
}

/* the string handed back by getuserinput() must be freed by
 * ExpandOptions once it has been copied into the output */
static void test_user_input(void)
{
	char *out;

	reset_scr();
	out = ExpandOptions(&scr, "app %a(Find)");
	CHECK(out != NULL);
	CHECK(out && strcmp(out, "app typed input") == 0);
	if (out)
		free(out);
}

/* each %a occurrence gets its own string from getuserinput(); none may
 * be dropped when the pointer is overwritten by the next one */
static void test_user_input_twice(void)
{
	char *out;

	reset_scr();
	out = ExpandOptions(&scr, "app %a(A) %a(B)");
	CHECK(out != NULL);
	CHECK(out && strcmp(out, "app typed input typed input") == 0);
	if (out)
		free(out);
}

/* a canceled prompt aborts the whole command: ExpandOptions returns
 * NULL, still without leaking anything allocated before the cancel */
static void test_user_input_canceled(void)
{
	char *out;

	reset_scr();
	out = ExpandOptions(&scr, "app %a(cancel me)");
	CHECK(out == NULL);
}

int main(void)
{
	test_plain_text();
#ifdef USE_DOCK_XDND
	test_dnd_without_gnustep();
	test_dnd_with_gnustep();
	test_dnd_single_file_gnustep();
	test_dnd_null_xdestring();
#endif
	test_selection_without_gnustep();
#ifdef USE_DOCK_XDND
	test_selection_with_gnustep();
#endif
	test_escaped_percent();
	test_workspace();
	test_user_input();
	test_user_input_twice();
	test_user_input_canceled();

	return check_report("check_cmdexpand");
}
