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
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>

#include <WINGs/WUtil.h>

#include "check_common.h"

static void test_roundtrip(const char *desc)
{
	WMPropList *a, *b;
	char *out;

	a = WMCreatePropListFromDescription(desc);
	CHECK(a != NULL);
	if (!a)
		return;

	out = WMGetPropListDescription(a, false);
	CHECK(out != NULL);

	b = WMCreatePropListFromDescription(out);
	CHECK(b != NULL);
	CHECK(WMIsPropListEqualTo(a, b));

	wfree(out);
	WMReleasePropList(a);
	if (b)
		WMReleasePropList(b);
}

static void test_indented_roundtrip(const char *desc)
{
	WMPropList *a, *b;
	char *out1, *out2;

	a = WMCreatePropListFromDescription(desc);
	CHECK(a != NULL);
	if (!a)
		return;

	out1 = WMGetPropListDescription(a, true);
	CHECK(out1 != NULL);

	b = out1 ? WMCreatePropListFromDescription(out1) : NULL;
	CHECK(b != NULL);
	CHECK(b && WMIsPropListEqualTo(a, b));

	/* a second indented pass over the re-parsed tree must be
	 * byte-identical, the writer is idempotent */
	out2 = b ? WMGetPropListDescription(b, true) : NULL;
	CHECK(out2 && strcmp(out1, out2) == 0);

	wfree(out1);
	wfree(out2);
	WMReleasePropList(a);
	if (b)
		WMReleasePropList(b);
}

static void test_indented_array_layout(void)
{
	WMPropList *small, *wide, *elem;
	char *out;
	int i;

	/* a short array stays on one line in indented output */
	small = WMCreatePropListFromDescription("(a, b, c)");
	out = WMGetPropListDescription(small, true);
	CHECK(out && strchr(out, '\n') == NULL);
	CHECK(out && strcmp(out, "(a, b, c)") == 0);
	wfree(out);
	WMReleasePropList(small);

	/* an array too wide for one line is broken across lines, this also
	 * exercises the rollback after the compact-form attempt and the
	 * append buffer growing well past its initial allocation */
	wide = WMCreatePLArray(NULL);
	for (i = 0; i < 200; i++) {
		elem = WMCreatePLString("entry");
		WMAddToPLArray(wide, elem);
		WMReleasePropList(elem);
	}
	out = WMGetPropListDescription(wide, true);
	CHECK(out && out[0] == '(' && out[1] == '\n');
	CHECK(out && strstr(out, ",\n  entry") != NULL);
	CHECK(out && out[strlen(out) - 1] == ')');
	wfree(out);

	/* and the compact form of the same wide array round-trips */
	out = WMGetPropListDescription(wide, false);
	CHECK(out && strchr(out, '\n') == NULL);
	small = WMCreatePropListFromDescription(out);
	CHECK(small && WMIsPropListEqualTo(wide, small));
	wfree(out);
	if (small)
		WMReleasePropList(small);
	WMReleasePropList(wide);
}

static void test_string_escape(void)
{
	WMPropList *s, *r;
	char *out;

	/* a string with characters that must be quoted/escaped on output */
	s = WMCreatePLString("path/with \"quote\" and \\backslash");
	out = WMGetPropListDescription(s, false);
	CHECK(out != NULL);

	r = WMCreatePropListFromDescription(out);
	CHECK(r != NULL);
	CHECK(WMIsPLString(r));
	CHECK(strcmp(WMGetFromPLString(r), WMGetFromPLString(s)) == 0);

	wfree(out);
	WMReleasePropList(s);
	WMReleasePropList(r);
}

static void test_comments(void)
{
	WMPropList *dict, *key, *val;

	/* the shipped WMGLOBAL template documents keys in C-style
	 * comments between tokens, the reader must skip them */
	dict = WMCreatePropListFromDescription(
		"{\n"
		"  /* block comment\n"
		"   * spanning lines */\n"
		"  key = value; // line comment\n"
		"  /* another */ num = 1; /* tail */\n"
		"}");
	CHECK(dict != NULL);
	if (dict) {
		key = WMCreatePLString("key");
		val = WMGetFromPLDictionary(dict, key);
		CHECK(val != NULL && WMIsPLString(val));
		CHECK(val && strcmp(WMGetFromPLString(val), "value") == 0);
		WMReleasePropList(key);
		WMReleasePropList(dict);
	}

	/* a trailing comment after the top-level value is not garbage */
	dict = WMCreatePropListFromDescription("{ k = v; } // done\n");
	CHECK(dict != NULL);
	WMReleasePropList(dict);

	/* comments are not recognized inside quoted strings */
	dict = WMCreatePropListFromDescription("{ url = \"http://x/*y\"; }");
	CHECK(dict != NULL);
	if (dict) {
		key = WMCreatePLString("url");
		val = WMGetFromPLDictionary(dict, key);
		CHECK(val != NULL && WMIsPLString(val));
		CHECK(val && strcmp(WMGetFromPLString(val), "http://x/*y") == 0);
		WMReleasePropList(key);
		WMReleasePropList(dict);
	}

	/* a lone slash between stringable characters is part of the
	 * unquoted token, not a comment start */
	dict = WMCreatePropListFromDescription("{ path = /usr/bin; }");
	CHECK(dict != NULL);
	if (dict) {
		key = WMCreatePLString("path");
		val = WMGetFromPLDictionary(dict, key);
		CHECK(val != NULL && WMIsPLString(val));
		CHECK(val && strcmp(WMGetFromPLString(val), "/usr/bin") == 0);
		WMReleasePropList(key);
		WMReleasePropList(dict);
	}

	/* a value beginning with // is data, not a comment, since the
	 * previous non-space character is = */
	dict = WMCreatePropListFromDescription("{ url = //server/share; }");
	CHECK(dict != NULL);
	if (dict) {
		key = WMCreatePLString("url");
		val = WMGetFromPLDictionary(dict, key);
		CHECK(val != NULL && WMIsPLString(val));
		CHECK(val && strcmp(WMGetFromPLString(val), "//server/share") == 0);
		WMReleasePropList(key);
		WMReleasePropList(dict);
	}

	/* an unterminated block comment ends the parse cleanly */
	dict = WMCreatePropListFromDescription("{ k = v; /* no end");
	CHECK(dict == NULL);
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

static void test_description_cap(void)
{
	WMPropList *p;
	char *big;
	size_t cap = 4u * 1024u * 1024u;
	size_t n = cap + 1;
	size_t i;

	/* a string literal larger than the 4 MiB description cap is
	 * rejected without being parsed */
	big = wmalloc(n + 1);
	big[0] = '(';
	for (i = 1; i < n - 1; i++)
		big[i] = 'a';
	big[n - 1] = ')';
	big[n] = '\0';
	p = WMCreatePropListFromDescription(big);
	CHECK(p == NULL);
	wfree(big);

	/* NULL description returns NULL */
	p = WMCreatePropListFromDescription(NULL);
	CHECK(p == NULL);

	/* a description just under the cap still parses */
	big = wmalloc(cap);
	big[0] = '(';
	for (i = 1; i < cap - 2; i++)
		big[i] = 'a';
	big[cap - 2] = ')';
	big[cap - 1] = '\0';
	p = WMCreatePropListFromDescription(big);
	CHECK(p != NULL);
	if (p)
		WMReleasePropList(p);
	wfree(big);
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

static void test_utf8_roundtrip(void)
{
	/* h<e-acute>llo <em-dash> <han><zi> */
	const char *input = "h\303\251llo \342\200\224 \346\274\242\345\255\227";
	WMPropList *orig, *parsed;
	char *desc;

	orig = WMCreatePLString(input);
	desc = WMGetPropListDescription(orig, false);

	CHECK(desc != NULL);
	/* multibyte chars must pass through, not become \3nn octal runs */
	CHECK(strstr(desc, "\\3") == NULL);
	CHECK(strstr(desc, "\303\251") != NULL);

	parsed = WMCreatePropListFromDescription(desc);
	CHECK(parsed && WMIsPLString(parsed));
	CHECK(parsed && strcmp(WMGetFromPLString(parsed), input) == 0);
	CHECK(parsed && WMIsPropListEqualTo(orig, parsed));

	if (parsed)
		WMReleasePropList(parsed);
	WMReleasePropList(orig);
	wfree(desc);
}

static void check_escaped_roundtrip(const char *label, const char *input)
{
	WMPropList *orig, *parsed;
	char *desc;

	(void)label;
	orig = WMCreatePLString(input);
	desc = WMGetPropListDescription(orig, false);

	/* invalid sequences must be octal-escaped, not passed through raw */
	CHECK(strchr(desc, '\\') != NULL);
	CHECK(strstr(desc, input) == NULL);

	parsed = WMCreatePropListFromDescription(desc);
	CHECK(parsed && strcmp(WMGetFromPLString(parsed), input) == 0);

	if (parsed)
		WMReleasePropList(parsed);
	WMReleasePropList(orig);
	wfree(desc);
}

static void test_merge_subtract(void)
{
	WMPropList *dest, *src, *want, *kc, *kn, *nest;

	dest = WMCreatePropListFromDescription(
		"{ a = 1; b = old; nest = { x = 1; y = 2; }; }");
	src = WMCreatePropListFromDescription(
		"{ b = new; c = 3; nest = { y = 22; z = 9; }; }");
	CHECK(dest && src);
	if (!dest || !src)
		return;

	CHECK(WMMergePLDictionaries(dest, src, true) == dest);

	want = WMCreatePropListFromDescription(
		"{ a = 1; b = new; c = 3; nest = { x = 1; y = 22; z = 9; }; }");
	CHECK(want && WMIsPropListEqualTo(dest, want));
	WMReleasePropList(want);

	/* subtract removes only keys whose values match, recursing into
	 * nested dictionaries */
	WMSubtractPLDictionaries(dest, src, true);

	kc = WMCreatePLString("c");
	CHECK(WMGetFromPLDictionary(dest, kc) == NULL);

	kn = WMCreatePLString("nest");
	nest = WMGetFromPLDictionary(dest, kn);
	CHECK(nest && WMGetPropListItemCount(nest) == 1);

	WMReleasePropList(kc);
	WMReleasePropList(kn);
	WMReleasePropList(src);

	/* subtracting a dictionary from itself empties it */
	WMSubtractPLDictionaries(dest, dest, false);
	CHECK(WMGetPropListItemCount(dest) == 0);

	WMReleasePropList(dest);
}

static void test_copy_depth(void)
{
	WMPropList *orig, *shallow, *deep, *extra;

	orig = WMCreatePropListFromDescription("(a, (b, c))");
	CHECK(orig && WMIsPLArray(orig));
	if (!orig)
		return;

	shallow = WMShallowCopyPropList(orig);
	deep = WMDeepCopyPropList(orig);
	CHECK(shallow && WMIsPropListEqualTo(orig, shallow));
	CHECK(deep && WMIsPropListEqualTo(orig, deep));

	/* shallow copy shares the inner array, deep copy does not */
	CHECK(WMGetFromPLArray(shallow, 1) == WMGetFromPLArray(orig, 1));
	CHECK(WMGetFromPLArray(deep, 1) != WMGetFromPLArray(orig, 1));

	/* mutating the original's inner array is visible through the
	 * shallow copy and invisible through the deep one */
	extra = WMCreatePLString("d");
	WMAddToPLArray(WMGetFromPLArray(orig, 1), extra);
	WMReleasePropList(extra);

	CHECK(WMGetPropListItemCount(WMGetFromPLArray(shallow, 1)) == 3);
	CHECK(WMGetPropListItemCount(WMGetFromPLArray(deep, 1)) == 2);

	WMReleasePropList(shallow);
	WMReleasePropList(deep);
	WMReleasePropList(orig);
}

static void test_invalid_high_byte(void)
{
	check_escaped_roundtrip("lone-latin1", "caf\351");
	check_escaped_roundtrip("overlong-3", "x\340\200\200y");
	check_escaped_roundtrip("surrogate", "x\355\240\200y");
	check_escaped_roundtrip("past-10FFFF", "x\364\220\200\200y");
	check_escaped_roundtrip("overlong-4", "x\360\200\200\200y");
}

static void check_json_str(const char *input, const char *want)
{
	WMPropList *pl;
	char *js;

	pl = WMCreatePLString(input);
	js = WMGetPropListJSON(pl, false);
	CHECK_STR(js, want);
	wfree(js);
	WMReleasePropList(pl);
}

static void check_json_pl(WMPropList *pl, const char *want)
{
	char *js;

	js = WMGetPropListJSON(pl, false);
	CHECK_STR(js, want);
	wfree(js);
}

static void test_json_string(void)
{
	char *js;

	js = WMGetPropListJSON(NULL, false);
	CHECK_STR(js, "null");
	wfree(js);

	check_json_str("hello", "\"hello\"");
	check_json_str("", "\"\"");
	check_json_str("42", "\"42\"");
	check_json_str("a\"b\\c", "\"a\\\"b\\\\c\"");
	check_json_str("t\tn\rb\fu\vd\a", "\"t\\tn\\rb\\fu\\u000bd\\u0007\"");
	check_json_str("\001\010\037", "\"\\u0001\\b\\u001f\"");
	check_json_str("h\303\251 \346\274\242", "\"h\303\251 \346\274\242\"");
	/* each invalid byte becomes one U+FFFD (ef bf bd) */
	check_json_str("caf\351", "\"caf\357\277\275\"");
	check_json_str("x\355\240\200y", "\"x\357\277\275\357\277\275\357\277\275y\"");
}

static void test_json_data(void)
{
	WMPropList *pl;

	pl = WMCreatePLDataWithBytes((const unsigned char *)"Man", 3);
	check_json_pl(pl, "\"TWFu\"");
	WMReleasePropList(pl);

	pl = WMCreatePLDataWithBytes((const unsigned char *)"Ma", 2);
	check_json_pl(pl, "\"TWE=\"");
	WMReleasePropList(pl);

	pl = WMCreatePLDataWithBytes((const unsigned char *)"M", 1);
	check_json_pl(pl, "\"TQ==\"");
	WMReleasePropList(pl);

	pl = WMCreatePLDataWithBytes((const unsigned char *)"", 0);
	check_json_pl(pl, "\"\"");
	WMReleasePropList(pl);
}

static void test_json_nesting(void)
{
	WMPropList *empty_arr, *empty_dict, *arr, *dict, *k, *v, *e;

	empty_arr = WMCreatePLArray(NULL);
	check_json_pl(empty_arr, "[]");
	WMReleasePropList(empty_arr);

	empty_dict = WMCreatePLDictionary(NULL, NULL);
	check_json_pl(empty_dict, "{}");
	WMReleasePropList(empty_dict);

	arr = WMCreatePLArray(NULL);
	e = WMCreatePLString("a");
	WMAddToPLArray(arr, e);
	WMReleasePropList(e);
	e = WMCreatePLString("b");
	WMAddToPLArray(arr, e);
	WMReleasePropList(e);
	check_json_pl(arr, "[\"a\",\"b\"]");
	WMReleasePropList(arr);

	/* keys are emitted sorted, not in insertion order */
	dict = WMCreatePLDictionary(NULL, NULL);
	k = WMCreatePLString("z");
	v = WMCreatePLString("1");
	WMPutInPLDictionary(dict, k, v);
	WMReleasePropList(k);
	WMReleasePropList(v);
	k = WMCreatePLString("a");
	v = WMCreatePLArray(NULL);
	e = WMCreatePLString("x");
	WMAddToPLArray(v, e);
	WMReleasePropList(e);
	WMPutInPLDictionary(dict, k, v);
	WMReleasePropList(k);
	WMReleasePropList(v);
	check_json_pl(dict, "{\"a\":[\"x\"],\"z\":\"1\"}");
	WMReleasePropList(dict);
}

static void test_json_indent(void)
{
	WMPropList *pl;
	char *js;

	pl = WMCreatePropListFromDescription("{ a = (x, y); b = { }; }");
	CHECK(pl != NULL);
	if (!pl)
		return;

	js = WMGetPropListJSON(pl, true);
	CHECK_STR(js, "{\n  \"a\": [\n    \"x\",\n    \"y\"\n  ],\n  \"b\": {}\n}");
	wfree(js);
	WMReleasePropList(pl);
}

static void test_json_ipc_shape(void)
{
	WMPropList *pl;
	char *js;

	pl = WMCreatePropListFromDescription("({ id = \"0x1\"; }, { id = \"0x2\"; })");
	CHECK(pl != NULL);
	if (!pl)
		return;

	js = WMGetPropListJSON(pl, false);
	CHECK_STR(js, "[{\"id\":\"0x1\"},{\"id\":\"0x2\"}]");
	wfree(js);
	WMReleasePropList(pl);
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

	test_indented_roundtrip("{ key = value; other = (1, 2, 3); }");
	test_indented_roundtrip("{ nested = { a = (x, y); b = <0a0b 0c0d>; }; }");
	test_indented_array_layout();

	test_merge_subtract();
	test_copy_depth();
	test_string_escape();
	test_comments();
	test_utf8_roundtrip();
	test_invalid_high_byte();
	test_json_string();
	test_json_data();
	test_json_nesting();
	test_json_indent();
	test_json_ipc_shape();
	test_recursion_limit();
	test_description_cap();
	test_pipe_cap();
	test_file_cap();
	test_wmkdirhier_confinement(root);

	if (root)
		wrmdirhier(root);

	return check_report("check_proplist");
}
