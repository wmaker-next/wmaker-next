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

	test_mime_for_path();
	test_default_app();
	test_open_command();

	wXDGMimeInvalidate();
	wXDGAppListInvalidate();

	if (failures) {
		fprintf(stderr, "%d failure(s)\n", failures);
		return 1;
	}
	puts("ok");
	return 0;
}
