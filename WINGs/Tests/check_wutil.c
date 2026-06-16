/*
 *  check_wutil - automated unit tests for libWUtil containers and helpers
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include <WINGs/WUtil.h>

static int failures;

#define CHECK(cond) do {                                                   \
	if (!(cond)) {                                                     \
	    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);\
	    failures++;                                                    \
	}                                                                  \
    } while (0)

static void test_wstrlcpy(void)
{
	char buf[8];
	size_t r;

	r = wstrlcpy(buf, "abc", sizeof(buf));
	CHECK(r == 3);
	CHECK(strcmp(buf, "abc") == 0);

	/* truncation returns the source length, not the bytes copied */
	r = wstrlcpy(buf, "0123456789", sizeof(buf));
	CHECK(r == 10);
	CHECK(strlen(buf) == 7);
	CHECK(buf[7] == '\0');

	r = wstrlcat(buf, "X", sizeof(buf));
	CHECK(r >= sizeof(buf));        /* signals truncation */
	CHECK(strlen(buf) == 7);        /* unchanged, already full */
}

static void test_wstrndup(void)
{
	char *s;

	s = wstrndup("hello world", 5);
	CHECK(s != NULL);
	CHECK(strcmp(s, "hello") == 0);
	wfree(s);

	s = wstrndup("hi", 100);
	CHECK(strcmp(s, "hi") == 0);
	wfree(s);
}

static void test_intern(void)
{
	const char *a, *b, *c;
	char buf[8];

	CHECK(wintern(NULL) == NULL);
	wunintern(NULL);                /* must be a no-op */

	a = wintern("XTerm");
	/* equal content from a different buffer must yield the same pointer */
	wstrlcpy(buf, "XTerm", sizeof(buf));
	b = wintern(buf);
	CHECK(a == b);
	CHECK(strcmp(a, "XTerm") == 0);

	c = wintern("xterm");
	CHECK(c != a);

	/* a has refcount 2, dropping one keeps the storage alive */
	wunintern(b);
	CHECK(strcmp(a, "XTerm") == 0);
	CHECK(wintern("XTerm") == a);   /* still the same slot */
	wunintern(a);                   /* back to refcount 1 */

	wunintern(a);                   /* refcount hits 0, freed */
	wunintern(c);
}

static void test_array(void)
{
	WMArray *a = WMCreateArray(0);
	int i;

	CHECK(WMGetArrayItemCount(a) == 0);

	for (i = 0; i < 50; i++)
		WMAddToArray(a, (void *)(long)(i * 3));

	CHECK(WMGetArrayItemCount(a) == 50);
	CHECK((long)WMGetFromArray(a, 0) == 0);
	CHECK((long)WMGetFromArray(a, 49) == 147);
	CHECK(WMGetFromArray(a, 50) == NULL);   /* out of range */

	WMFreeArray(a);
}

static void test_hashtable(void)
{
	WMHashTable *t = WMCreateHashTable(WMStringHashCallbacks);

	CHECK(WMHashGet(t, "missing") == NULL);

	WMHashInsert(t, "one", (void *)1L);
	WMHashInsert(t, "two", (void *)2L);
	WMHashInsert(t, "three", (void *)3L);

	CHECK((long)WMHashGet(t, "one") == 1);
	CHECK((long)WMHashGet(t, "two") == 2);
	CHECK((long)WMHashGet(t, "three") == 3);
	CHECK(WMHashGet(t, "four") == NULL);

	/* overwrite */
	WMHashInsert(t, "two", (void *)22L);
	CHECK((long)WMHashGet(t, "two") == 22);

	WMFreeHashTable(t);
}

static void check_shellquote_roundtrip(const char *in)
{
	char *quoted, *cmd, *out;
	int st;

	quoted = wshellquote(in);
	CHECK(quoted != NULL);
	if (!quoted)
		return;

	/* feed the quoted form back through a real shell, the round-tripped
	 * argument must reproduce the input byte-for-byte */
	cmd = wstrconcat("printf %s ", quoted);
	out = wspawnvp_capture((char *[]){ "/bin/sh", "-c", cmd, NULL }, 256, &st);
	CHECK(st == 0);
	CHECK(out && strcmp(out, in) == 0);

	wfree(out);
	wfree(cmd);
	wfree(quoted);
}

static void test_wshellquote(void)
{
	char *q;

	CHECK(wshellquote(NULL) == NULL);

	/* safe characters pass through unquoted */
	q = wshellquote("abc/DEF.0,1+2-3");
	CHECK(q && strcmp(q, "abc/DEF.0,1+2-3") == 0);
	wfree(q);

	/* the empty string still needs to reach the shell as one argument */
	q = wshellquote("");
	CHECK(q && strcmp(q, "''") == 0);
	wfree(q);

	check_shellquote_roundtrip("two words");
	check_shellquote_roundtrip("a'b");
	check_shellquote_roundtrip("'leading");
	check_shellquote_roundtrip("trailing'");
	check_shellquote_roundtrip("''");
	check_shellquote_roundtrip("$(rm -rf ~)");
	check_shellquote_roundtrip("`id`;\\\"");
}

static void test_wtoken(void)
{
	char in[] = "one \"two words\" 'sq arg' a\\ b";
	char **argv;
	char *joined;
	int argc;

	wtokensplit(in, &argv, &argc);
	CHECK(argc == 4);
	CHECK(argc >= 4 && strcmp(argv[0], "one") == 0);
	CHECK(argc >= 4 && strcmp(argv[1], "two words") == 0);
	CHECK(argc >= 4 && strcmp(argv[2], "sq arg") == 0);
	CHECK(argc >= 4 && strcmp(argv[3], "a b") == 0);

	/* join re-quotes whitespace-bearing tokens so split round-trips */
	joined = wtokenjoin(argv, argc);
	CHECK(joined && strcmp(joined, "one \"two words\" \"sq arg\" \"a b\"") == 0);
	wfree(joined);

	wtokenfree(argv, argc);
}

static void test_wtrimspace(void)
{
	char *s;

	s = wtrimspace("  hello  ");
	CHECK(s && strcmp(s, "hello") == 0);
	wfree(s);

	s = wtrimspace("\t\n mid dle \t");
	CHECK(s && strcmp(s, "mid dle") == 0);
	wfree(s);

	s = wtrimspace("none");
	CHECK(s && strcmp(s, "none") == 0);
	wfree(s);

	CHECK(wtrimspace(NULL) == NULL);
}

static void test_wexpandpath(void)
{
	const char *home = wgethomedir();
	char want[1024];
	char *p;

	p = wexpandpath("~");
	CHECK(p && strcmp(p, home) == 0);
	wfree(p);

	snprintf(want, sizeof(want), "%s/sub", home);
	p = wexpandpath("~/sub");
	CHECK(p && strcmp(p, want) == 0);
	wfree(p);

	setenv("WUTIL_TEST_VAR", "expanded", 1);
	p = wexpandpath("/a/$WUTIL_TEST_VAR/b");
	CHECK(p && strcmp(p, "/a/expanded/b") == 0);
	wfree(p);

	p = wexpandpath("/a/$(WUTIL_TEST_VAR)/b");
	CHECK(p && strcmp(p, "/a/expanded/b") == 0);
	wfree(p);
	unsetenv("WUTIL_TEST_VAR");

	/* unknown variables are preserved literally, not dropped */
	p = wexpandpath("/a/$WUTIL_NOPE/b");
	CHECK(p && strcmp(p, "/a/$WUTIL_NOPE/b") == 0);
	wfree(p);

	p = wexpandpath("/a/$(WUTIL_NOPE)/b");
	CHECK(p && strcmp(p, "/a/$(WUTIL_NOPE)/b") == 0);
	wfree(p);
}

static int int_compare(const void *a, const void *b)
{
	return (int)(long)a - (int)(long)b;
}

static void test_bag(void)
{
	WMBag *bag = WMCreateTreeBag();
	WMBagIterator it;
	void *item;
	long sum;
	int i;

	CHECK(WMGetBagItemCount(bag) == 0);

	for (i = 0; i < 8; i++)
		WMPutInBag(bag, (void *)(long)(100 + i));

	CHECK(WMGetBagItemCount(bag) == 8);
	CHECK((long)WMGetFromBag(bag, 0) == 100);
	CHECK((long)WMGetFromBag(bag, 7) == 107);
	CHECK(WMGetFromBag(bag, 50) == NULL);

	CHECK(WMGetFirstInBag(bag, (void *)103L) == 3);
	CHECK(WMGetFirstInBag(bag, (void *)999L) == WBNotFound);

	/* delete shifts later indices down */
	CHECK(WMDeleteFromBag(bag, 3) == 1);
	CHECK(WMGetBagItemCount(bag) == 7);
	CHECK((long)WMGetFromBag(bag, 3) == 104);
	CHECK((long)WMGetFromBag(bag, 6) == 107);

	/* erase leaves later indices in place */
	CHECK(WMEraseFromBag(bag, 3) == 1);
	CHECK(WMGetBagItemCount(bag) == 6);
	CHECK(WMGetFromBag(bag, 3) == NULL);
	CHECK((long)WMGetFromBag(bag, 4) == 105);

	/* iteration visits every remaining item exactly once */
	sum = 0; i = 0;
	for (item = WMBagFirst(bag, &it); item; item = WMBagNext(bag, &it)) {
		sum += (long)item;
		i++;
	}
	CHECK(i == 6);
	CHECK(sum == 100 + 101 + 102 + 105 + 106 + 107);

	/* sort compacts to contiguous 0-based indices */
	WMSortBag(bag, int_compare);
	CHECK(WMGetFromBag(bag, 3) != NULL);
	CHECK((long)WMGetFromBag(bag, 0) == 100);
	CHECK((long)WMGetFromBag(bag, 5) == 107);

	WMFreeBag(bag);
}

static void test_data(void)
{
	unsigned char raw[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	unsigned char more[] = { 9, 10 };
	WMData *d;

	d = WMCreateDataWithBytes(raw, sizeof(raw));
	CHECK(WMGetDataLength(d) == sizeof(raw));
	CHECK(memcmp(WMDataBytes(d), raw, sizeof(raw)) == 0);

	WMSetDataFormat(d, 32);
	CHECK(WMGetDataFormat(d) == 32);

	WMAppendDataBytes(d, more, sizeof(more));
	CHECK(WMGetDataLength(d) == sizeof(raw) + sizeof(more));
	CHECK(((const unsigned char *)WMDataBytes(d))[8] == 9);
	CHECK(((const unsigned char *)WMDataBytes(d))[9] == 10);

	WMReleaseData(d);
}

static int child_exit_status(pid_t pid)
{
	int status;

	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR)
			return -1;
	}

	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static void set_probe_env(void *data)
{
	(void)data;
	setenv("WSPAWN_PROBE", "ok", 1);
}

static void test_spawn(void)
{
	int st;
	char *out;
	pid_t pid;

	CHECK(wspawnvp_wait((char *[]){ "true", NULL }) == 0);
	CHECK(wspawnvp_wait((char *[]){ "false", NULL }) != 0);

	unsetenv("WSPAWN_PROBE");
	pid = wspawnvp_setup((char *[]){ "/bin/sh", "-c", "test \"$WSPAWN_PROBE\" = ok", NULL },
			     set_probe_env, NULL);
	CHECK(pid > 0);
	if (pid > 0)
		CHECK(child_exit_status(pid) == 0);
	unsetenv("WSPAWN_PROBE");

	out = wspawnvp_capture((char *[]){ "printf", "ab%s", "cd", NULL }, 64, &st);
	CHECK(out != NULL);
	CHECK(st == 0);
	CHECK(out && strcmp(out, "abcd") == 0);
	wfree(out);

	/* maxlen is a hard cap and the result is NUL-terminated */
	out = wspawnvp_capture((char *[]){ "printf", "0123456789", NULL }, 4, &st);
	CHECK(out != NULL);
	CHECK(st == -1);
	CHECK(out && strlen(out) == 4);
	CHECK(out && strncmp(out, "0123", 4) == 0);
	wfree(out);

	/* hostile writers that ignore SIGPIPE/SIGTERM are still reaped */
	out = wspawnvp_capture((char *[]){ "/bin/sh", "-c",
					"trap '' TERM PIPE; while :; do printf x; done",
					NULL }, 4, &st);
	CHECK(out != NULL);
	CHECK(st == -1);
	CHECK(out && strlen(out) == 4);
	wfree(out);
}

int main(void)
{
	test_wstrlcpy();
	test_wstrndup();
	test_wshellquote();
	test_wtoken();
	test_wtrimspace();
	test_wexpandpath();
	test_intern();
	test_array();
	test_bag();
	test_hashtable();
	test_data();
	test_spawn();

	if (failures) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	printf("ok - check_wutil\n");
	return 0;
}
