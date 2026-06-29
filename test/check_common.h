/*
 *  check_common - shared assertion macros for the `make check` test programs
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Header-only so it can be included from the WINGs, src and wrlib test
 *  trees without adding a shared object file to each Makefile.
 */

#ifndef WMAKER_CHECK_COMMON_H
#define WMAKER_CHECK_COMMON_H

#include <stdio.h>
#include <string.h>

static int check_failures;

#define CHECK(cond) do {                                                   \
	if (!(cond)) {                                                     \
	    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);\
	    check_failures++;                                              \
	}                                                                  \
    } while (0)

#define CHECK_STR(got, want) do {                                          \
	const char *g_ = (got), *w_ = (want);                              \
	if (!g_ || strcmp(g_, w_) != 0) {                                  \
	    fprintf(stderr, "FAIL %s:%d: got \"%s\" want \"%s\"\n",        \
		    __FILE__, __LINE__, g_ ? g_ : "(null)", w_);           \
	    check_failures++;                                              \
	}                                                                  \
    } while (0)

static inline int check_report(const char *name)
{
	if (check_failures) {
		fprintf(stderr, "%d check(s) failed\n", check_failures);
		return 1;
	}
	printf("ok - %s\n", name);
	return 0;
}

#endif /* WMAKER_CHECK_COMMON_H */
