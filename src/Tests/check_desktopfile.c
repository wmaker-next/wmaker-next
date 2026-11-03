/*
 *  check_desktopfile.c
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

/*
 * check_desktopfile - tests for the XDG MIME and matcher helpers in
 * src/desktopfile.c that have no X or window-manager dependency.
 *
 * The MIME resolver and default-handler lookup read their data from
 * XDG_DATA_HOME and XDG_CONFIG_HOME so the test points both at a
 * fixture tree under src/Tests/xdg/ and clears the system fallbacks.
 */

#include "wconfig.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <WINGs/WUtil.h>

#include "desktopfile.h"

#include "check_common.h"

static void test_mime_for_path(void)
{
	CHECK_STR(wXDGMimeTypeForPath("/tmp/x.pdf"), "application/pdf");
	CHECK_STR(wXDGMimeTypeForPath("/tmp/X.PDF"), "application/pdf");
	/* longest suffix wins on equal weight */
	CHECK_STR(wXDGMimeTypeForPath("/tmp/x.tar.gz"),
		  "application/x-compressed-tar");
	CHECK_STR(wXDGMimeTypeForPath("/tmp/x.gz"), "application/gzip");
	/* higher weight beats a longer suffix */
	CHECK_STR(wXDGMimeTypeForPath("/tmp/x.heavy.txt"), "text/heavy");
	/* case-sensitive glob does not match a lower-cased name */
	CHECK_STR(wXDGMimeTypeForPath("/tmp/x.C"), "text/x-c++src");
	CHECK(wXDGMimeTypeForPath("/tmp/x.c") == NULL);
	/* nonexistent file with no extension and nothing to sniff */
	CHECK(wXDGMimeTypeForPath("/tmp/noext") == NULL);
}

static void write_probe(const char *dir, const char *name, const char *data,
			char *path, size_t size)
{
	FILE *f;

	snprintf(path, size, "%s/%s", dir, name);
	f = fopen(path, "wb");
	if (!f) {
		perror(path);
		exit(2);
	}
	fputs(data, f);
	fclose(f);
}

static void test_mime_magic(void)
{
	char tmpl[] = "/tmp/wmaker-sniff-XXXXXX";
	char *dir, file[PATH_MAX + 64];

	/* the magic fixture lives alongside globs2, write a few
	 * extensionless probe files the sniffer can read */
	dir = mkdtemp(tmpl);
	if (!dir) {
		perror("mkdtemp");
		check_failures++;
		return;
	}

	write_probe(dir, "probe-pdf", "%PDF-1.5\nrest", file, sizeof(file));
	CHECK_STR(wXDGMimeTypeForPath(file), "application/pdf");

	write_probe(dir, "probe-script", "#!/bin/sh\necho hi\n", file, sizeof(file));
	CHECK_STR(wXDGMimeTypeForPath(file), "text/x-shellscript");

	write_probe(dir, "probe-misc", "plain text without a signature\n",
		    file, sizeof(file));
	CHECK(wXDGMimeTypeForPath(file) == NULL);

	/* extension match takes precedence over content */
	write_probe(dir, "probe.txt", "%PDF-1.5\n", file, sizeof(file));
	CHECK_STR(wXDGMimeTypeForPath(file), "text/plain");

	(void)wspawnvp_wait((char *[]){"rm", "-rf", dir, NULL});
}

static void test_apps_for_mime(void)
{
	WMArray *list;

	list = wXDGAppsForMime("application/pdf");
	CHECK(WMGetArrayItemCount(list) == 2);
	list = wXDGAppsForMime("image/png");
	CHECK(WMGetArrayItemCount(list) == 1);
	CHECK_STR(((WXDGApp *)WMGetFromArray(list, 0))->id, "viewer.desktop");
	list = wXDGAppsForMime("no/such");
	CHECK(WMGetArrayItemCount(list) == 0);
	CHECK(WMGetArrayItemCount(wXDGAppsForMime(NULL)) == 0);
}

static void test_default_app(void)
{
	/* default from [Default Applications] in mimeapps.list */
	CHECK_STR(wXDGDefaultAppFor("application/pdf"), "viewer.desktop");
	/* only listed in mimeinfo.cache */
	CHECK_STR(wXDGDefaultAppFor("text/plain"), "editor.desktop");
	/* listed but the desktop file does not exist */
	CHECK(wXDGDefaultAppFor("image/png") == NULL);
	CHECK(wXDGDefaultAppFor("no/such") == NULL);
}

static void test_open_command(void)
{
	char *cmd;

	cmd = wXDGOpenCommandFor("/tmp/Annual report.pdf");
	CHECK_STR(cmd, "viewpdf '/tmp/Annual report.pdf'");
	wfree(cmd);

	/* no %f token, path is appended */
	cmd = wXDGOpenCommandFor("/tmp/notes.txt");
	CHECK_STR(cmd, "edit --new /tmp/notes.txt");
	wfree(cmd);

	/* unknown extension falls back to xdg-open */
	cmd = wXDGOpenCommandFor("/tmp/blob.unknown");
	CHECK_STR(cmd, "xdg-open /tmp/blob.unknown");
	wfree(cmd);

	/* an Exec line carrying every file field code substitutes
	 * the quoted path at each one, this used to size the output
	 * for at most two codes and overflow on the third */
	cmd = wXDGOpenCommandFor("/tmp/a b.gz");
	CHECK_STR(cmd, "multi '/tmp/a b.gz' and '/tmp/a b.gz' "
		       "then '/tmp/a b.gz' also '/tmp/a b.gz' done");
	wfree(cmd);

	/* a field code inside quotes is rejected instead of being
	 * expanded into the surrounding shell quoting */
	cmd = wXDGOpenCommandFor("/tmp/$(touch pwned).qtd");
	CHECK_STR(cmd, "xdg-open '/tmp/$(touch pwned).qtd'");
	wfree(cmd);
}

static void touch(const char *dir, const char *name)
{
	char path[PATH_MAX + 64];
	FILE *f;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	f = fopen(path, "w");
	if (f)
		fclose(f);
}

static int docFind(WMArray *docs, const char *base)
{
	WXDGDoc *d;
	int i;

	for (i = 0; i < WMGetArrayItemCount(docs); i++) {
		d = WMGetFromArray(docs, i);
		if (strcmp(d->base, base) == 0)
			return 1;
	}
	return 0;
}

static void test_doc_list(void)
{
	WMArray *docs;
	char tmpl[] = "/tmp/wmaker-docidx-XXXXXX";
	char *root, *roots[1];
	char sub[PATH_MAX + 32], deep[PATH_MAX + 48];
	char dot[PATH_MAX + 32], gitdir[PATH_MAX + 32];

	root = mkdtemp(tmpl);
	if (!root) {
		perror("mkdtemp");
		check_failures++;
		return;
	}
	snprintf(sub, sizeof(sub), "%s/sub", root);
	snprintf(deep, sizeof(deep), "%s/sub/deep", root);
	snprintf(dot, sizeof(dot), "%s/.hidden", root);
	snprintf(gitdir, sizeof(gitdir), "%s/repo", root);
	mkdir(sub, 0700);
	mkdir(deep, 0700);
	mkdir(dot, 0700);
	mkdir(gitdir, 0700);
	touch(root, "Report.pdf");
	touch(root, ".dotfile");
	touch(sub, "notes.txt");
	touch(deep, "buried.txt");
	touch(dot, "secret.txt");
	touch(gitdir, ".git");
	touch(gitdir, "tracked.c");

	roots[0] = root;
	docs = wXDGDocList(roots, 1, 2, 100);

	CHECK(docFind(docs, "Report.pdf"));
	CHECK(docFind(docs, "notes.txt"));
	/* depth cap of 2 excludes sub/deep */
	CHECK(!docFind(docs, "buried.txt"));
	/* dot-directories and dot-files are skipped */
	CHECK(!docFind(docs, "secret.txt"));
	CHECK(!docFind(docs, ".dotfile"));
	/* a .git marker prunes its directory */
	CHECK(!docFind(docs, "tracked.c"));

	/* depth 3 reaches the deep file, the parameter change must
	 * trigger a rebuild without explicit invalidation */
	docs = wXDGDocList(roots, 1, 3, 100);
	CHECK(docFind(docs, "buried.txt"));

	/* hard cap */
	docs = wXDGDocList(roots, 1, 3, 1);
	CHECK(WMGetArrayItemCount(docs) == 1);

	/* a changed extra-roots set rebuilds without an mtime bump */
	roots[0] = sub;
	snprintf(sub, sizeof(sub), "%s/sub", root);
	docs = wXDGDocList(roots, 1, 1, 100);
	CHECK(docFind(docs, "notes.txt"));
	CHECK(!docFind(docs, "Report.pdf"));

	wXDGDocListInvalidate();

	/* best-effort cleanup of the fixture tree */
	(void)wspawnvp_wait((char *[]){"rm", "-rf", root, NULL});
}

static void test_fuzzy_score(void)
{
	/* subsequence required, order matters */
	CHECK(wFuzzyScore("abc", "axbxc", "axbxc") >= 0);
	CHECK(wFuzzyScore("abc", "acb", "acb") < 0);
	CHECK(wFuzzyScore("fxo", "firefox", "firefox") < 0);
	/* empty needle never matches, the call site already
	 * short-circuits empty input but the function stays total */
	CHECK(wFuzzyScore("", "anything", "anything") < 0);

	/* tighter packing beats a spread-out match */
	CHECK(wFuzzyScore("abc", "abc", "abc") >
	      wFuzzyScore("abc", "axbxc", "axbxc"));
	CHECK(wFuzzyScore("ffx", "firefox", "firefox") >
	      wFuzzyScore("ffx", "fortifyzx", "fortifyzx"));

	/* word-start bonus on the capital F lifts FireFox over a
	 * mid-word ff in stuff */
	CHECK(wFuzzyScore("ff", "firefox", "FireFox") >
	      wFuzzyScore("ff", "stuff", "stuff"));

	/* anchored prefix beats a later occurrence */
	CHECK(wFuzzyScore("fir", "firefox", "firefox") >
	      wFuzzyScore("fir", "campfire", "campfire"));

	/* word-start bonus on the L of LibreOffice for the lboff
	 * style abbreviation */
	CHECK(wFuzzyScore("loff", "libreoffice", "LibreOffice") >
	      wFuzzyScore("loff", "killoffender", "killoffender"));
}

static void test_frecency_score(void)
{
	time_t now = 1000000000, day = 86400;

	CHECK(wFrecencyScore(0, now, now) == 0);
	/* same hits, fresher wins */
	CHECK(wFrecencyScore(27, now, now) >
	      wFrecencyScore(27, now - 60 * day, now));
	/* more hits beats fewer at equal age */
	CHECK(wFrecencyScore(27, now - 60 * day, now) >
	      wFrecencyScore(3, now - 60 * day, now));
	/* a few recent hits beat one stale hit */
	CHECK(wFrecencyScore(3, now, now) >
	      wFrecencyScore(1, now - 90 * day, now));
	/* capped at 100 */
	CHECK(wFrecencyScore(10000, now, now) == 100);
}

/* ---- hicolor icon-theme resolver ---- */

static void mkicon(const char *dir, const char *name)
{
	char path[PATH_MAX + 64];
	FILE *f;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	f = fopen(path, "w");
	if (f)
		fclose(f);
}

static void test_xdg_find_theme_icon(void)
{
	char tmpl[] = "/tmp/wmaker-hicolor-XXXXXX";
	char root[PATH_MAX], dir48[PATH_MAX + 32], dir256[PATH_MAX + 32];
	char dirscal[PATH_MAX + 32], dir64[PATH_MAX + 32];
	char *got;
	const char *prev;

	prev = getenv("XDG_DATA_HOME");

	root[0] = '\0';
	if (!mkdtemp(tmpl)) {
		perror("mkdtemp");
		check_failures++;
		return;
	}
	snprintf(root, sizeof(root), "%s", tmpl);

	snprintf(dir48, sizeof(dir48), "%s/icons/hicolor/48x48/apps", root);
	snprintf(dir256, sizeof(dir256), "%s/icons/hicolor/256x256/apps", root);
	snprintf(dir64, sizeof(dir64), "%s/icons/hicolor/64x64/apps", root);
	snprintf(dirscal, sizeof(dirscal), "%s/icons/hicolor/scalable/apps", root);
	/* mkdir -p each leaf */
	(void)wspawnvp_wait((char *[]){"mkdir", "-p", dir48, dir256,
				       dir64, dirscal, NULL});

	mkicon(dir48, "myapp.png");
	mkicon(dir256, "myapp.png");
	mkicon(dirscal, "vector.svg");
	mkicon(dir64, "vector.png");
	/* an asset that exists only as SVG, to test USE_SVG gating */
	mkicon(dirscal, "onlysvg.svg");

	setenv("XDG_DATA_HOME", root, 1);

	/* exact-size raster hit */
	got = wXDGFindThemeIcon("myapp", "apps", 48);
	CHECK(got != NULL);
	CHECK(strstr(got, "48x48") != NULL);
	wfree(got);

	/* no exact size, nearest-larger raster wins over a smaller one */
	got = wXDGFindThemeIcon("myapp", "apps", 64);
	CHECK(got != NULL);
	CHECK(strstr(got, "256x256") != NULL);
	wfree(got);

	/* missing asset returns NULL cleanly */
	got = wXDGFindThemeIcon("nope", "apps", 48);
	CHECK(got == NULL);

	mkicon(root, "secret.png");
	got = wXDGFindThemeIcon("../../../../secret", "apps", 48);
	CHECK(got == NULL);
	got = wXDGFindThemeIcon("myapp", "../../apps", 48);
	CHECK(got == NULL);

#ifdef USE_SVG
	/* a scalable hit with no competing raster */
	got = wXDGFindThemeIcon("onlysvg", "apps", 128);
	CHECK(got != NULL);
	CHECK(strstr(got, "scalable") != NULL);
	wfree(got);

	/* exact-size raster beats a scalable render at the same want */
	got = wXDGFindThemeIcon("vector", "apps", 64);
	CHECK(got != NULL);
	CHECK(strstr(got, "64x64") != NULL);
	wfree(got);
#else
	/* without USE_SVG an SVG-only asset is not found, no crash */
	got = wXDGFindThemeIcon("onlysvg", "apps", 128);
	CHECK(got == NULL);
#endif

	/* restore the harness-wide XDG_DATA_HOME for the MIME tests */
	if (prev)
		setenv("XDG_DATA_HOME", prev, 1);
	else
		unsetenv("XDG_DATA_HOME");

	(void)wspawnvp_wait((char *[]){"rm", "-rf", root, NULL});
}

int main(void)
{
	const char *src;
	char base[PATH_MAX], data[PATH_MAX + 16], conf[PATH_MAX + 16];

	src = getenv("CHECK_DESKTOPFILE_SRCDIR");
	if (!src)
		src = ".";
	/* wXDGForEachDataDir only honours absolute XDG_*_HOME values */
	if (!realpath(src, base)) {
		perror(src);
		return 1;
	}
	snprintf(data, sizeof(data), "%s/xdg/share", base);
	snprintf(conf, sizeof(conf), "%s/xdg/config", base);
	setenv("XDG_DATA_HOME", data, 1);
	setenv("XDG_DATA_DIRS", "/nonexistent", 1);
	setenv("XDG_CONFIG_HOME", conf, 1);
	setenv("XDG_CONFIG_DIRS", "/nonexistent", 1);

	/* the standard XDG user dirs likely do not exist under a
	 * build sandbox, point HOME away so only the explicit extra
	 * root passed by test_doc_list is scanned */
	setenv("HOME", "/nonexistent", 1);

	test_mime_for_path();
	test_mime_magic();
	test_apps_for_mime();
	test_default_app();
	test_open_command();
	test_doc_list();
	test_fuzzy_score();
	test_frecency_score();
	test_xdg_find_theme_icon();

	wXDGMimeInvalidate();
	wXDGAppListInvalidate();

	return check_report("check_desktopfile");
}
