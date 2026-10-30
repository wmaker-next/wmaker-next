/*
 *  check_xlibfree.c
 *
 *  Window Maker window manager
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
 */

/* check_xlibfree.c - the xcb conversion's link gates as a make check suite.
 *
 * The converted binaries must carry no undefined Xlib symbol, no
 * libX11/libXext dependency, and the tree no X11 include outside xsmp's
 * SM/ICE pair.  Promoted from the E2 recovery verification, where a
 * reconstructed wxhints.c smuggled an XInternAtom back into libWINGs and
 * only the manual readelf pass caught it.
 *
 * Skips (77) when readelf or an artifact is missing, so a partial build
 * stays green, like wrlib's check_xside.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *artifacts[] = {
	"src/.libs/wmaker",
	"WINGs/.libs/libWINGs.so",
	"WINGs/.libs/libWUtil.so",
	"WPrefs.app/.libs/WPrefs",
	"wrlib/.libs/libwraster.so",
	NULL
};

static const char *top;
static const char *src;

/* Every hit the awk/grep pipelines report is one failure. */

static int check_und(const char *path)
{
	char cmd[8192], line[256];
	FILE *f;
	int bad = 0;

	snprintf(cmd, sizeof(cmd),
		 "readelf --dyn-syms -W '%.4000s' 2>/dev/null"
		 " | awk '$7 == \"UND\" { n = $8; sub(/@.*/, \"\", n);"
		 " if (n ~ /^X[A-Z]/) print n }' | sort -u", path);
	f = popen(cmd, "r");
	if (!f)
		return 0;
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\n")] = '\0';
		printf("FAIL: %s: undefined Xlib symbol %s\n", path, line);
		bad++;
	}
	pclose(f);
	return bad;
}

static int check_needed(const char *path)
{
	char cmd[8192], line[256];
	FILE *f;
	int bad = 0;

	snprintf(cmd, sizeof(cmd),
		 "readelf -d '%.4000s' 2>/dev/null"
		 " | grep -oE 'libX(11|ext)\\.so' | sort -u", path);
	f = popen(cmd, "r");
	if (!f)
		return 0;
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\n")] = '\0';
		printf("FAIL: %s: DT_NEEDED %s\n", path, line);
		bad++;
	}
	pclose(f);
	return bad;
}

static int check_includes(void)
{
	char cmd[8192], line[4096];
	FILE *f;
	int bad = 0;

	/* the source tree is scanned, not the build tree: a VPATH
	 * build's src/ holds only generated files and the gate would
	 * silently false-PASS exactly in the dist scenario it exists
	 * for */
	snprintf(cmd, sizeof(cmd),
		 "grep -rn --include='*.[ch]' --exclude-dir=Tests"
		 " 'include <X11/'"
		 " '%.1000s/src' '%.1000s/WINGs' '%.1000s/WPrefs.app'"
		 " 2>/dev/null | grep -v '%.1000s/src/xsmp.c:'",
		 src, src, src, src);
	f = popen(cmd, "r");
	if (!f)
		return 0;
	while (fgets(line, sizeof(line), f)) {
		printf("FAIL: X11 include outside xsmp: %s", line);
		bad++;
	}
	pclose(f);
	return bad;
}

static int check_pcs(void)
{
	static const char *pcs[] = {
		"wrlib/wrlib.pc",
		"WINGs/WINGs.pc",
		"WINGs/WUtil.pc",
		NULL
	};
	char buf[8192], cmd[8192], line[256];
	FILE *f;
	int i, bad = 0;

	/* the generated .pc files are the install-facing surface: a
	 * template or a configure substitution smuggling Xlib back in
	 * would only ever surface here */
	for (i = 0; pcs[i]; i++) {
		snprintf(buf, sizeof(buf), "%s/%s", top, pcs[i]);
		if (access(buf, F_OK) != 0)
			continue;
		snprintf(cmd, sizeof(cmd),
			 "grep -nE 'X11|libXext' '%.4000s' 2>/dev/null", buf);
		f = popen(cmd, "r");
		if (!f)
			continue;
		while (fgets(line, sizeof(line), f)) {
			line[strcspn(line, "\n")] = '\0';
			printf("FAIL: %s: %s\n", buf, line);
			bad++;
		}
		pclose(f);
	}
	return bad;
}

int main(void)
{
	char buf[8192];
	int i, bad = 0;

	top = getenv("CHECK_XLIBFREE_TOP");
	if (!top || !top[0])
		top = "../..";
	src = getenv("CHECK_XLIBFREE_SRC");
	if (!src || !src[0])
		src = top;

	if (system("command -v readelf >/dev/null 2>&1") != 0) {
		printf("SKIP: readelf not found\n");
		return 77;
	}

	for (i = 0; artifacts[i]; i++) {
		snprintf(buf, sizeof(buf), "%s/%s", top, artifacts[i]);
		if (access(buf, F_OK) != 0) {
			printf("SKIP: %s not built\n", artifacts[i]);
			return 77;
		}
	}

	for (i = 0; artifacts[i]; i++) {
		snprintf(buf, sizeof(buf), "%s/%s", top, artifacts[i]);
		bad += check_und(buf);
		bad += check_needed(buf);
	}
	bad += check_includes();
	bad += check_pcs();

	if (bad) {
		fprintf(stderr, "%d violation(s)\n", bad);
		return 1;
	}
	printf("ok - check_xlibfree\n");
	return 0;
}
