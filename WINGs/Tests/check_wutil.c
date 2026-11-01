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
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <https://www.gnu.org/licenses/>.
 *
 *  Run by `make check`. Exit status 0 on pass, non-zero on the first
 *  failing assertion with the file and line printed to stderr.
 */

#include <errno.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

#include <WINGs/WUtil.h>
#include <WINGs/WINGsP.h>

#include "check_common.h"

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

/* '.' must be the decimal separator regardless of LC_NUMERIC, since the
 * numbers come from serialized IPC/state/config text. The exact
 * comparisons are the point: wstrtod must yield the same double the
 * compiler stores for the literal. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
static void test_wstrtod(void)
{
	char *end, *saved;
	const char *current;
	double d;

	d = wstrtod("0.7", &end);
	CHECK(d == 0.7 && *end == '\0');

	d = wstrtod("0.5x", &end);
	CHECK(d == 0.5 && *end == 'x');

	d = wstrtod(" 12.5", &end);
	CHECK(d == 12.5 && *end == '\0');

	/* ',' is not a decimal separator in the C locale */
	d = wstrtod("0,7", &end);
	CHECK(d == 0.0 && *end == ',');

	d = wstrtod("abc", &end);
	CHECK(d == 0.0 && strcmp(end, "abc") == 0);

	CHECK(isnan(wstrtod("nan", &end)) && *end == '\0');
	CHECK(isinf(wstrtod("-inf", &end)) && *end == '\0');

	/* '.' stays the separator when LC_NUMERIC uses ',' */
	saved = NULL;
	current = setlocale(LC_ALL, NULL);
	if (current)
		saved = wstrdup(current);
	if ((setlocale(LC_ALL, "de_DE.UTF-8")
	     || setlocale(LC_ALL, "fr_FR.UTF-8")
	     || setlocale(LC_ALL, "en_DK.UTF-8"))
	    && localeconv()->decimal_point[0] == ',') {
		d = wstrtod("0.7", &end);
		CHECK(d == 0.7 && *end == '\0');
		d = wstrtod("0,7", &end);
		CHECK(d == 0.0 && *end == ',');
	}
	setlocale(LC_ALL, saved ? saved : "C");
	if (saved)
		wfree(saved);
}
#pragma GCC diagnostic pop

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

static void test_wpathdir(void)
{
	char *d;

	/* no slash -> current directory */
	d = wpathdir("file");
	CHECK(d && strcmp(d, ".") == 0);
	wfree(d);

	d = wpathdir("");
	CHECK(d && strcmp(d, ".") == 0);
	wfree(d);

	d = wpathdir(NULL);
	CHECK(d && strcmp(d, ".") == 0);
	wfree(d);

	/* only the leading slash -> root */
	d = wpathdir("/file");
	CHECK(d && strcmp(d, "/") == 0);
	wfree(d);

	d = wpathdir("/");
	CHECK(d && strcmp(d, "/") == 0);
	wfree(d);

	/* prefix before the last slash */
	d = wpathdir("a/b");
	CHECK(d && strcmp(d, "a") == 0);
	wfree(d);

	d = wpathdir("/a/b/c");
	CHECK(d && strcmp(d, "/a/b") == 0);
	wfree(d);
}

static int int_compare(const void *a, const void *b)
{
	return (int)(long)a - (int)(long)b;
}

static void free_bag_item(void *data)
{
	wfree(data);
}

static void fill_bag_with_values(WMBag *bag, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		int *value = wmalloc(sizeof(int));

		*value = i;
		WMPutInBag(bag, value);
	}
}

static void test_bag_two_child_delete(void)
{
	WMBag *bag;
	int *value;

	bag = WMCreateTreeBagWithDestructor(free_bag_item);
	fill_bag_with_values(bag, 3);
	CHECK(WMDeleteFromBag(bag, 1) == 1);
	CHECK(WMGetBagItemCount(bag) == 2);
	value = WMGetFromBag(bag, 0);
	CHECK(value && *value == 0);
	value = WMGetFromBag(bag, 1);
	CHECK(value && *value == 2);
	WMFreeBag(bag);

	bag = WMCreateTreeBagWithDestructor(free_bag_item);
	fill_bag_with_values(bag, 3);
	CHECK(WMRemoveFromBag(bag, WMGetFromBag(bag, 1)) == 1);
	CHECK(WMGetBagItemCount(bag) == 2);
	value = WMGetFromBag(bag, 0);
	CHECK(value && *value == 0);
	value = WMGetFromBag(bag, 1);
	CHECK(value && *value == 2);
	WMFreeBag(bag);

	bag = WMCreateTreeBagWithDestructor(free_bag_item);
	fill_bag_with_values(bag, 3);
	CHECK(WMEraseFromBag(bag, 1) == 1);
	CHECK(WMGetBagItemCount(bag) == 2);
	value = WMGetFromBag(bag, 0);
	CHECK(value && *value == 0);
	CHECK(WMGetFromBag(bag, 1) == NULL);
	value = WMGetFromBag(bag, 2);
	CHECK(value && *value == 2);
	WMFreeBag(bag);

	bag = WMCreateTreeBagWithDestructor(free_bag_item);
	fill_bag_with_values(bag, 3);
	CHECK(WMReplaceInBag(bag, 1, NULL) == NULL);
	CHECK(WMGetBagItemCount(bag) == 2);
	value = WMGetFromBag(bag, 0);
	CHECK(value && *value == 0);
	value = WMGetFromBag(bag, 2);
	CHECK(value && *value == 2);
	WMFreeBag(bag);
}

static void test_userdefaults_sync(void)
{
	char tmpl[] = "/tmp/check_wutil_ud_XXXXXX";
	char path[1024];
	char *dir;
	struct utimbuf ut;
	WMUserDefaults *ud;
	WMPropList *key, *value, *key2, *value2, *dict;
	time_t newer;

	dir = mkdtemp(tmpl);
	CHECK(dir != NULL);
	snprintf(path, sizeof(path), "%s/domain", dir);

	key = WMCreatePLString("Disk");
	value = WMCreatePLString("initial");
	dict = WMCreatePLDictionary(key, value, NULL);
	CHECK(dict != NULL);
	CHECK(WMWritePropListToFile(dict, path));
	WMReleasePropList(dict);
	WMReleasePropList(key);
	WMReleasePropList(value);

	ud = WMGetDefaultsFromPath(path);
	CHECK(ud != NULL);
	WMSetUDStringForKey(ud, "LocalValue", "Local");

	key = WMCreatePLString("Disk");
	value = WMCreatePLString("changed");
	key2 = WMCreatePLString("External");
	value2 = WMCreatePLString("ExternalValue");
	dict = WMCreatePLDictionary(key, value, key2, value2, NULL);
	CHECK(dict != NULL);
	CHECK(WMWritePropListToFile(dict, path));
	WMReleasePropList(dict);
	WMReleasePropList(key);
	WMReleasePropList(value);
	WMReleasePropList(key2);
	WMReleasePropList(value2);

	newer = time(NULL) + 5;
	ut.actime = newer;
	ut.modtime = newer;
	CHECK(utime(path, &ut) == 0);

	W_InitNotificationCenter();
	WMSynchronizeUserDefaults(ud);
	W_ReleaseNotificationCenter();

	dict = WMReadPropListFromFile(path);
	CHECK(dict != NULL);
	if (dict) {
		key = WMCreatePLString("Local");
		value = WMGetFromPLDictionary(dict, key);
		CHECK(value && WMIsPLString(value));
		if (value && WMIsPLString(value))
			CHECK(strcmp(WMGetFromPLString(value), "LocalValue") == 0);
		WMReleasePropList(key);

		key = WMCreatePLString("External");
		value = WMGetFromPLDictionary(dict, key);
		CHECK(value && WMIsPLString(value));
		if (value && WMIsPLString(value))
			CHECK(strcmp(WMGetFromPLString(value), "ExternalValue") == 0);
		WMReleasePropList(key);

		WMReleasePropList(dict);
	}

	wspawnvp_wait((char *[]){ "rm", "-rf", dir, NULL });
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

static int child_eq(const char *got, const char *want)
{
	if (got && strcmp(got, want) == 0)
		return 1;
	fprintf(stderr, "  got %s, want %s\n", got ? got : "(null)", want);
	return 0;
}

/* wusergnusteppath()/wuserdatapath()/wusercachepath() cache their result
 * for the process lifetime, so each layout is exercised in its own child. */
static void check_userpaths(int xdg, const char *cfg, const char *dom,
			    const char *data, const char *cache)
{
	pid_t pid;
	int st, ok;
	char *d;

	pid = fork();
	CHECK(pid >= 0);
	if (pid > 0) {
		CHECK(waitpid(pid, &st, 0) == pid);
		CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);
		return;
	}

	ok = child_eq(wusergnusteppath(), cfg);
	ok &= child_eq(wuserdatapath(), data);
	ok &= child_eq(wusercachepath(), cache);
	d = wdefaultspathfordomain("WMState");
	ok &= child_eq(d, dom);
	wfree(d);
	/* the config-root call must have exported the derived roots */
	ok &= child_eq(getenv("WMAKER_USER_DATA"), data);
	ok &= child_eq(getenv("WMAKER_USER_CACHE"), cache);
	/* wmkdirhier() accepts creation under each root */
	d = wstrconcat(data, "/wmk-check/sub/leaf");
	ok &= wmkdirhier(d) == 1;
	wfree(d);
	if (xdg) {
		/* three separate trees, cache root is not under data root */
		d = wstrconcat(cache, "/wmk-check/sub/leaf");
		ok &= wmkdirhier(d) == 1;
		wfree(d);
	}
	_exit(ok ? 0 : 1);
}

/* Must run before anything else calls wgethomedir()/wusergnusteppath()
 * so the forked children start with unpopulated static caches. */
static void test_userpaths(void)
{
	char home[] = "/tmp/check_wutil_home_XXXXXX";
	char cfg[256], dom[512], data[512], cache[512], sub[512];
	char *saved_home;

	if (!mkdtemp(home))
		return;
	saved_home = getenv("HOME");
	saved_home = saved_home ? wstrdup(saved_home) : NULL;
	unsetenv("WMAKER_USER_ROOT");
	unsetenv("WMAKER_USER_DATA");
	unsetenv("WMAKER_USER_CACHE");
	unsetenv("WMAKER_USER_XDG");
	unsetenv("XDG_CONFIG_HOME");
	unsetenv("XDG_DATA_HOME");
	unsetenv("XDG_CACHE_HOME");
	setenv("HOME", home, 1);

	/* explicit root -> legacy single-tree layout */
	snprintf(cfg, sizeof(cfg), "%s/R", home);
	snprintf(dom, sizeof(dom), "%s/Defaults/WMState", cfg);
	snprintf(data, sizeof(data), "%s/Library/WindowMaker", cfg);
	setenv("WMAKER_USER_ROOT", cfg, 1);
	check_userpaths(0, cfg, dom, data, data);
	unsetenv("WMAKER_USER_ROOT");
	unsetenv("WMAKER_USER_XDG");

	/* legacy ~/GNUstep tree present -> single-tree layout */
	snprintf(cfg, sizeof(cfg), "%s/GNUstep", home);
	snprintf(sub, sizeof(sub), "%s/Defaults", cfg);
	mkdir(cfg, 0700);
	mkdir(sub, 0700);
	snprintf(dom, sizeof(dom), "%s/Defaults/WMState", cfg);
	snprintf(data, sizeof(data), "%s/Library/WindowMaker", cfg);
	check_userpaths(0, cfg, dom, data, data);
	rmdir(sub);
	rmdir(cfg);

	/* fresh account, no XDG vars set -> XDG defaults, split layout,
	 * wmkdirhier() creates the missing ~/.local/share and ~/.cache */
	snprintf(cfg, sizeof(cfg), "%s/.config/WindowMaker", home);
	snprintf(dom, sizeof(dom), "%s/WMState", cfg);
	snprintf(data, sizeof(data), "%s/.local/share/WindowMaker", home);
	snprintf(cache, sizeof(cache), "%s/.cache/WindowMaker", home);
	check_userpaths(1, cfg, dom, data, cache);

	/* explicit XDG_*_HOME -> honoured, split layout */
	snprintf(sub, sizeof(sub), "%s/xc", home);
	setenv("XDG_CONFIG_HOME", sub, 1);
	snprintf(sub, sizeof(sub), "%s/xd", home);
	setenv("XDG_DATA_HOME", sub, 1);
	snprintf(sub, sizeof(sub), "%s/xh", home);
	setenv("XDG_CACHE_HOME", sub, 1);
	snprintf(cfg, sizeof(cfg), "%s/xc/WindowMaker", home);
	snprintf(dom, sizeof(dom), "%s/WMState", cfg);
	snprintf(data, sizeof(data), "%s/xd/WindowMaker", home);
	snprintf(cache, sizeof(cache), "%s/xh/WindowMaker", home);
	check_userpaths(1, cfg, dom, data, cache);

	/* child inheriting WMAKER_USER_ROOT + WMAKER_USER_XDG from an XDG
	 * parent must keep the split layout, not fall back to Defaults/ */
	snprintf(cfg, sizeof(cfg), "%s/xc/WindowMaker", home);
	snprintf(dom, sizeof(dom), "%s/WMState", cfg);
	snprintf(data, sizeof(data), "%s/xd/WindowMaker", home);
	snprintf(cache, sizeof(cache), "%s/xh/WindowMaker", home);
	setenv("WMAKER_USER_ROOT", cfg, 1);
	setenv("WMAKER_USER_XDG", "1", 1);
	check_userpaths(1, cfg, dom, data, cache);
	unsetenv("WMAKER_USER_ROOT");
	unsetenv("WMAKER_USER_XDG");

	unsetenv("XDG_CONFIG_HOME");
	unsetenv("XDG_DATA_HOME");
	unsetenv("XDG_CACHE_HOME");
	if (saved_home) {
		setenv("HOME", saved_home, 1);
		wfree(saved_home);
	}
	wspawnvp_wait((char *[]){ "rm", "-rf", home, NULL });
}

int main(void)
{
	test_userpaths();
	test_wstrlcpy();
	test_wstrndup();
	test_wshellquote();
	test_wtoken();
	test_wtrimspace();
	test_wstrtod();
	test_wexpandpath();
	test_wpathdir();
	test_intern();
	test_array();
	test_userdefaults_sync();
	test_bag();
	test_bag_two_child_delete();
	test_hashtable();
	test_data();
	test_spawn();

	return check_report("check_wutil");
}
