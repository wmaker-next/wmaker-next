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

static int failures;

#define CHECK(cond) do { \
		if (!(cond)) { \
			fprintf(stderr, "FAIL %s:%d: %s\n", \
				__FILE__, __LINE__, #cond); \
			failures++; \
		} \
	} while (0)

#define CHECK_STR(got, want) do { \
		const char *g_ = (got), *w_ = (want); \
		if (!g_ || strcmp(g_, w_) != 0) { \
			fprintf(stderr, "FAIL %s:%d: got \"%s\" want \"%s\"\n", \
				__FILE__, __LINE__, g_ ? g_ : "(null)", w_); \
			failures++; \
		} \
	} while (0)

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
	CHECK(wXDGMimeTypeForPath("/tmp/noext") == NULL);
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
		failures++;
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
	snprintf(sub, sizeof(sub), "rm -rf '%s'", root);
	if (system(sub) != 0)
		perror("rm");
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
	test_default_app();
	test_open_command();
	test_doc_list();

	wXDGMimeInvalidate();
	wXDGAppListInvalidate();

	if (failures) {
		fprintf(stderr, "%d failure(s)\n", failures);
		return 1;
	}
	puts("ok");
	return 0;
}
