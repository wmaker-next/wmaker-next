#!/bin/sh
# cppcheck gate over the analyzer-relevant sources. Deliberately not
# part of `make check` (which stays X-free and fast without requiring
# cppcheck); run it on demand with `make check-cppcheck`.
#
# Exit conventions follow the drive scripts: 77 when the tool is
# missing, nonzero when a finding survives test/cppcheck-suppressions.
set -e

command -v cppcheck >/dev/null 2>&1 || exit 77

# analysis needs the top build directory so -I. resolves config.h and
# the generated WINGsP.h
if [ ! -f config.h ]; then
	echo "check-cppcheck: run from the top build directory" >&2
	exit 1
fi

nproc=$(nproc 2>/dev/null || echo 4)

exec cppcheck \
	--enable=all \
	--inline-suppr \
	--std=c11 \
	--platform=unix64 \
	-DHAVE_CONFIG_H \
	-I. \
	--suppress=missingIncludeSystem \
	--suppress=missingInclude \
	--suppress=unusedFunction \
	--suppress=checkLevelNormal \
	--suppressions-list="$(dirname "$0")/cppcheck-suppressions" \
	--error-exitcode=1 \
	-j"$nproc" \
	src WINGs util wrlib WPrefs.app
