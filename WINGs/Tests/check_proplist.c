/*
 *  check_proplist - automated unit tests for the WUtil property-list parser
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Run by `make check`. Exit status 0 on pass, non-zero on the first
 *  failing assertion with the file and line printed to stderr.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>

#include <WINGs/WUtil.h>

static int failures;

#define CHECK(cond) do {                                                   \
	if (!(cond)) {                                                     \
	    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);\
	    failures++;                                                    \
	}                                                                  \
    } while (0)

static void test_roundtrip(const char *desc)
{
	WMPropList *a, *b;
	char *out;

	a = WMCreatePropListFromDescription(desc);
	CHECK(a != NULL);
	if (!a)
		return;

	out = WMGetPropListDescription(a, False);
	CHECK(out != NULL);

	b = WMCreatePropListFromDescription(out);
	CHECK(b != NULL);
	CHECK(WMIsPropListEqualTo(a, b));

	wfree(out);
	WMReleasePropList(a);
	if (b)
		WMReleasePropList(b);
}

static void test_string_escape(void)
{
	WMPropList *s, *r;
	char *out;

	/* a string with characters that must be quoted/escaped on output */
	s = WMCreatePLString("path/with \"quote\" and \\backslash");
	out = WMGetPropListDescription(s, False);
	CHECK(out != NULL);

	r = WMCreatePropListFromDescription(out);
	CHECK(r != NULL);
	CHECK(WMIsPLString(r));
	CHECK(strcmp(WMGetFromPLString(r), WMGetFromPLString(s)) == 0);

	wfree(out);
	WMReleasePropList(s);
	WMReleasePropList(r);
}

static void test_recursion_limit(void)
{
	char *deep;
	size_t i, n = 4096;
	WMPropList *p;

	/* ((((... )))) nested far past the parser depth limit, must be
	 * rejected cleanly rather than overflowing the stack */
	deep = wmalloc(2 * n + 1);
	for (i = 0; i < n; i++)
		deep[i] = '(';
	for (i = 0; i < n; i++)
		deep[n + i] = ')';
	deep[2 * n] = '\0';

	p = WMCreatePropListFromDescription(deep);
	CHECK(p == NULL);

	wfree(deep);
}

static void test_pipe_cap(void)
{
	WMPropList *p;

	/* a well-formed small pipe output parses */
	p = WMReadPropListFromPipe("printf '(a, b, c)'");
	CHECK(p != NULL);
	CHECK(p && WMIsPLArray(p));
	CHECK(p && WMGetPropListItemCount(p) == 3);
	if (p)
		WMReleasePropList(p);

	/* an oversized pipe output is truncated rather than growing without
	 * bound, the cap is 4 MiB so the closing ')' after 8 MiB never
	 * arrives and the unclosed array fails to parse, numeric bs because
	 * NetBSD dd rejects the M suffix and GNU dd rejects lowercase m */
	p = WMReadPropListFromPipe(
	    "sh -c \"printf '('; dd if=/dev/zero bs=1048576 count=8 2>/dev/null | tr '\\\\0' a; printf ')'\"");
	CHECK(p == NULL);
}

static void test_file_cap(void)
{
	WMPropList *p;
	char path[] = "/tmp/check_proplist_big_XXXXXX";
	int fd;
	FILE *f;

	fd = mkstemp(path);
	CHECK(fd >= 0);
	if (fd < 0)
		return;

	/* a well-formed small file parses */
	CHECK(write(fd, "(a, b, c)\n", 10) == 10);
	close(fd);
	p = WMReadPropListFromFile(path);
	CHECK(p != NULL);
	CHECK(p && WMIsPLArray(p));
	if (p)
		WMReleasePropList(p);

	/* a file larger than the 4 MiB input cap is rejected before being
	 * read into memory, regardless of whether it would parse */
	f = fopen(path, "wb");
	CHECK(f != NULL);
	if (f) {
		CHECK(fseek(f, 5L * 1024 * 1024, SEEK_SET) == 0);
		CHECK(fputc('a', f) == 'a');
		fclose(f);
	}
	p = WMReadPropListFromFile(path);
	CHECK(p == NULL);

	unlink(path);
}

static void test_wmkdirhier_confinement(const char *root)
{
	char buf[PATH_MAX];

	/* bare filename, parent is cwd which exists */
	CHECK(wmkdirhier("plain.out") == 1);

	/* existing parent outside the GNUstep root */
	CHECK(wmkdirhier("/tmp/plain.out") == 1);

	if (!root || strlen(root) + 64 >= sizeof(buf))
		return;

	/* sibling directory whose name shares the root as a prefix */
	snprintf(buf, sizeof(buf), "%s_evil/sub/file", root);
	CHECK(wmkdirhier(buf) == 0);

	/* dot-dot escape from inside the root */
	snprintf(buf, sizeof(buf), "%s/../wmk-escape/sub/file", root);
	CHECK(wmkdirhier(buf) == 0);

	/* legitimate descendant still creates */
	snprintf(buf, sizeof(buf), "%s/Library/WindowMaker/wmk-check-tmp/sub/file", root);
	CHECK(wmkdirhier(buf) == 1);
	snprintf(buf, sizeof(buf), "%s/Library/WindowMaker/wmk-check-tmp/sub", root);
	CHECK(rmdir(buf) == 0);
	snprintf(buf, sizeof(buf), "%s/Library/WindowMaker/wmk-check-tmp", root);
	CHECK(rmdir(buf) == 0);
}

int main(void)
{
	char rootbuf[] = "/tmp/check_proplist_root_XXXXXX";
	const char *root;

	/* run against a hermetic user root so the wmkdirhier checks do not
	 * depend on ~/GNUstep existing or write into it, the directory is
	 * removed again so wmkdirhier has to create the root itself */
	root = mkdtemp(rootbuf);
	if (root) {
		setenv("WMAKER_USER_ROOT", root, 1);
		rmdir(root);
	}

	test_roundtrip("hello");
	test_roundtrip("\"two words\"");
	test_roundtrip("(a, b, c)");
	test_roundtrip("{ key = value; other = (1, 2, 3); }");
	test_roundtrip("{ nested = { a = (x, y); }; }");

	test_string_escape();
	test_recursion_limit();
	test_pipe_cap();
	test_file_cap();
	test_wmkdirhier_confinement(root);

	if (root)
		wrmdirhier(root);

	if (failures) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	printf("ok - check_proplist\n");
	return 0;
}
