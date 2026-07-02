/*
 * check_xsmp - test wXSMPFilterArgv() argv filtering.
 *
 * The libSM half of xsmp.c drags in wmaker screen/session symbols and
 * needs a running session manager to exercise, so this TU compiles the
 * source with USE_XSMP forced off and covers only the always-built
 * filter that produces SmRestartCommand/SmCloneCommand.
 */

#include "wconfig.h"
#undef USE_XSMP
#include "../xsmp.c"

#include "check_common.h"

static void free_argv(char **v, int n)
{
	int i;

	for (i = 0; i < n; i++)
		wfree(v[i]);
	wfree(v);
}

static void test_passthrough(void)
{
	char *in[] = { "wmaker", "--no-dock", "-display", ":1" };
	char **out;
	int n;

	out = wXSMPFilterArgv(4, in, &n);
	CHECK(n == 4);
	CHECK_STR(out[0], "wmaker");
	CHECK_STR(out[1], "--no-dock");
	CHECK_STR(out[2], "-display");
	CHECK_STR(out[3], ":1");
	CHECK(out[4] == NULL);
	free_argv(out, n);
}

static void test_strip_for_real(void)
{
	char *in[] = { "wmaker", "--for-real", "--no-clip" };
	char **out;
	int n;

	out = wXSMPFilterArgv(3, in, &n);
	CHECK(n == 2);
	CHECK_STR(out[0], "wmaker");
	CHECK_STR(out[1], "--no-clip");
	CHECK(out[2] == NULL);
	free_argv(out, n);
}

static void test_strip_for_real_variants(void)
{
	char *in[] = { "wmaker", "--for-real=", "--for-real-", "-static" };
	char **out;
	int n;

	out = wXSMPFilterArgv(4, in, &n);
	CHECK(n == 2);
	CHECK_STR(out[0], "wmaker");
	CHECK_STR(out[1], "-static");
	free_argv(out, n);
}

static void test_strip_clientid_pair(void)
{
	char *in[] = { "wmaker", "-clientid", "abc123", "--no-dock" };
	char **out;
	int n;

	out = wXSMPFilterArgv(4, in, &n);
	CHECK(n == 2);
	CHECK_STR(out[0], "wmaker");
	CHECK_STR(out[1], "--no-dock");
	free_argv(out, n);
}

static void test_strip_clientid_double_dash(void)
{
	char *in[] = { "wmaker", "--clientid", "abc123" };
	char **out;
	int n;

	out = wXSMPFilterArgv(3, in, &n);
	CHECK(n == 1);
	CHECK_STR(out[0], "wmaker");
	CHECK(out[1] == NULL);
	free_argv(out, n);
}

static void test_clientid_missing_value(void)
{
	char *in[] = { "wmaker", "-clientid" };
	char **out;
	int n;

	/* trailing -clientid with no value must not read past argv */
	out = wXSMPFilterArgv(2, in, &n);
	CHECK(n == 1);
	CHECK_STR(out[0], "wmaker");
	CHECK(out[1] == NULL);
	free_argv(out, n);
}

static void test_combined(void)
{
	char *in[] = { "/usr/bin/wmaker", "-display", ":0",
		       "--for-real", "-clientid", "10a4", "--no-clip" };
	char **out;
	int n;

	out = wXSMPFilterArgv(7, in, &n);
	CHECK(n == 4);
	CHECK_STR(out[0], "/usr/bin/wmaker");
	CHECK_STR(out[1], "-display");
	CHECK_STR(out[2], ":0");
	CHECK_STR(out[3], "--no-clip");
	CHECK(out[4] == NULL);
	free_argv(out, n);
}

int main(void)
{
	test_passthrough();
	test_strip_for_real();
	test_strip_for_real_variants();
	test_strip_clientid_pair();
	test_strip_clientid_double_dash();
	test_clientid_missing_value();
	test_combined();
	return check_report("check_xsmp");
}
