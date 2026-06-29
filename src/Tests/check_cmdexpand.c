/*
 * check_cmdexpand - tests for ExpandOptions() extracted to cmdexpand.c.
 *
 * Pattern follows check_wdefaults.c: compile the test + ../cmdexpand.c,
 * provide stubs for getselection/getuserinput, link against libWUtil.la.
 */

#include "wconfig.h"

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

/* stub: not exercised in these tests */
char *getuserinput(WScreen *scr, const char *line, int *ptr, Bool advanced)
{
	(void)scr; (void)line; (void)ptr; (void)advanced;
	return NULL;
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

	return check_report("check_cmdexpand");
}
