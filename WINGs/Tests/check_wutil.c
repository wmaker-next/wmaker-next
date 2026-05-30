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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void test_data(void)
{
	unsigned char raw[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	WMData *d;

	d = WMCreateDataWithBytes(raw, sizeof(raw));
	CHECK(WMGetDataLength(d) == sizeof(raw));
	CHECK(memcmp(WMDataBytes(d), raw, sizeof(raw)) == 0);

	WMSetDataFormat(d, 32);
	CHECK(WMGetDataFormat(d) == 32);

	WMReleaseData(d);
}

int main(void)
{
	test_wstrlcpy();
	test_wstrndup();
	test_array();
	test_hashtable();
	test_data();

	if (failures) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	printf("ok - check_wutil\n");
	return 0;
}
