#!/bin/sh
#
# check-manifest.sh - verify every file referenced from automake
# *_SOURCES and EXTRA_DIST lists actually exists in the source tree, so
# typo drift like wmiv.hi vs wmiv.h is caught before make distcheck.
#
# Copyright (c) 2026 Window Maker Team
#
# Redistribution and use is allowed according to the terms of the
# 2-clause BSD license. See COPYING.WTFPL for the project licence.

set -u

srcdir=${1:-.}
fail=0

for mf in $(find "$srcdir" -name Makefile.am); do
	dir=$(dirname "$mf")
	# pull the RHS of *_SOURCES / EXTRA_DIST assignments, joining
	# backslash continuations, dropping comment-only lines
	for f in $(awk '
	    /^[ \t]*#/ { next }
	    /(_SOURCES|^EXTRA_DIST)[ \t]*\+?=/ {
		grab = 1
		sub(/^[^=]*=/, "")
	    }
	    grab {
		sub(/#.*$/, "")
		cont = sub(/\\$/, "")
		print
		if (!cont) grab = 0
	    }
	' "$mf"); do
		# skip make variables, autoconf substitutions, function-call
		# fragments and absolute paths
		case "$f" in
		    *'$'*|*'@'*|*'('*|*')'*|*,*|/*)
			continue ;;
		esac
		if [ ! -e "$dir/$f" ] && [ ! -e "$srcdir/$f" ]; then
			echo "missing: $mf references '$f'" >&2
			fail=1
		fi
	done
done

exit $fail
