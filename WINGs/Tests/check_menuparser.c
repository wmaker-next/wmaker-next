/*
 *  check_menuparser - regression tests for the WINGs menu parser
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
 *
 *  Run by `make check`. Exit status 0 on pass, non-zero on failure.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <WINGs/WUtil.h>

#include "check_common.h"

static char tmp_path[] = "/tmp/wmaker-check-menuparser-XXXXXX";

/*
 * Regression test for the stack buffer overflow in
 * menu_parser_isolate_token.
 *
 * A #define macro that expands to a long string, followed by a token
 * with ~500 chars of prefix + the macro + ~500 chars of suffix, causes
 * the expanded content to be appended at the token pointer's current
 * offset. Without bounds checks the write overflows the 1024-byte
 * stack buffer. The fix truncates writes at buffer_end, so the token
 * must never exceed MAXLINE (1024) bytes including the NUL.
 */
static void test_macro_expansion_overflow(void)
{
	FILE *f;
	char *title, *command, *params, *shortcut;
	WMenuParser parser;
	char prefix[501], suffix[501];

	/* Build a line: aaaa...500...LONGMACRO bbbb...500... */
	memset(prefix, 'a', 500);
	prefix[500] = '\0';
	memset(suffix, 'b', 500);
	suffix[500] = '\0';

	f = fopen(tmp_path, "w");
	CHECK(f != NULL);
	if (!f)
		return;
	fprintf(f, "#define LONGMACRO ");
	for (int i = 0; i < 600; i++)
		fputc('x', f);
	fprintf(f, "\n");
	fprintf(f, "%sLONGMACRO%s EXEC /bin/true\n", prefix, suffix);
	fclose(f);

	f = fopen(tmp_path, "r");
	CHECK(f != NULL);
	if (!f)
		return;
	parser = WMenuParserCreate(tmp_path, f, NULL);
	CHECK(parser != NULL);
	if (!parser)
		return;

	/* The parser should not crash. The title token should be at most
	 * MAXLINE-1 chars (truncated by the bounds check). */
	if (WMenuParserGetLine(parser, &title, &command, &params, &shortcut)) {
		CHECK(title != NULL);
		if (title) {
			/* Token must fit in the 1024-byte buffer (1023 + NUL) */
			CHECK(strlen(title) < 1024);
		}
		wfree(title);
		wfree(command);
		wfree(params);
		wfree(shortcut);
	}

	WMenuParserDelete(parser);
	fclose(f);
}

/*
 * Regression test for normal macro expansion (not an overflow test).
 * Verifies that a simple #define + macro usage produces the expected
 * token, so the overflow fix doesn't break the normal path.
 */
static void test_macro_expansion_basic(void)
{
	FILE *f;
	char *title, *command, *params, *shortcut;
	WMenuParser parser;

	f = fopen(tmp_path, "w");
	CHECK(f != NULL);
	if (!f)
		return;
	fprintf(f, "#define GREETING hello\n");
	fprintf(f, "GREETING EXEC /bin/true\n");
	fclose(f);

	f = fopen(tmp_path, "r");
	CHECK(f != NULL);
	if (!f)
		return;
	parser = WMenuParserCreate(tmp_path, f, NULL);
	CHECK(parser != NULL);
	if (!parser)
		return;

	if (WMenuParserGetLine(parser, &title, &command, &params, &shortcut)) {
		CHECK(title != NULL);
		if (title) {
			CHECK(strcmp(title, "hello") == 0);
		}
		wfree(title);
		wfree(command);
		wfree(params);
		wfree(shortcut);
	}

	WMenuParserDelete(parser);
	fclose(f);
}

int main(void)
{
	int fd = mkstemp(tmp_path);

	if (fd < 0) {
		perror("mkstemp");
		return 1;
	}
	close(fd);

	test_macro_expansion_basic();
	test_macro_expansion_overflow();

	/* cleanup */
	unlink(tmp_path);

	return check_report("check_menuparser");
}
