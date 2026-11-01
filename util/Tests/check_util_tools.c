/*
 *  check_util_tools - regression tests for non-X utility programs
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

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <WINGs/WUtil.h>

#include "check_common.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static char tmpdir[] = "/tmp/check_util_tools_XXXXXX";
static const char *top_builddir;

static const char *tool_path(const char *name)
{
	static char path[PATH_MAX];
	char abs[PATH_MAX];

	snprintf(path, sizeof(path), "%s/util/.libs/%s", top_builddir, name);
	if (realpath(path, abs))
		snprintf(path, sizeof(path), "%s", abs);
	return path;
}

static int write_file(const char *path, const char *data)
{
	FILE *f = fopen(path, "wb");

	if (!f)
		return 0;
	if (fputs(data, f) == EOF) {
		fclose(f);
		return 0;
	}
	return fclose(f) == 0;
}

static char *read_file(const char *path)
{
	FILE *f = fopen(path, "rb");
	long len;
	char *buf;

	if (!f)
		return NULL;
	if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 ||
	    fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return NULL;
	}
	buf = malloc((size_t)len + 1);
	if (!buf) {
		fclose(f);
		return NULL;
	}
	if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
		free(buf);
		fclose(f);
		return NULL;
	}
	buf[len] = '\0';
	fclose(f);
	return buf;
}

static int run_tool_timeout(char *const argv[], int seconds)
{
	pid_t pid;
	int i, status;

	pid = wspawnvp_setup(argv, NULL, NULL);
	if (pid < 0)
		return -1;

	for (i = 0; i < seconds * 10; i++) {
		pid_t r = waitpid(pid, &status, WNOHANG);

		if (r == pid)
			return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
		if (r < 0)
			return -1;
		usleep(100000);
	}

	kill(pid, SIGKILL);
	waitpid(pid, &status, 0);
	return -2;
}

static void test_wdwrite_non_dictionary(void)
{
	char *path, *before, *after;
	int rc;

	path = wdefaultspathfordomain("TestDomain");
	CHECK(path != NULL);
	if (!path)
		return;
	CHECK(wmkdirhier(path) == 1);
	CHECK(write_file(path, "\"not a dictionary\"\n"));
	before = read_file(path);
	CHECK(before != NULL);

	rc = wspawnvp_wait((char *[]){ (char *)tool_path("wdwrite"),
				       "TestDomain", "Key", "Value", NULL });
	CHECK(rc != 0);

	after = read_file(path);
	CHECK(after != NULL);
	CHECK(before && after && strcmp(before, after) == 0);
	free(before);
	free(after);

	rc = wspawnvp_wait((char *[]){ (char *)tool_path("wdread"),
				       "TestDomain", "Key", NULL });
	CHECK(rc != 0);
	wfree(path);
}

static void test_icons_non_dictionary(void)
{
	char iconset[PATH_MAX];
	char *conf;
	int rc;

	conf = wdefaultspathfordomain("WMWindowAttributes");
	CHECK(conf != NULL);
	if (!conf)
		return;
	CHECK(wmkdirhier(conf) == 1);
	snprintf(iconset, sizeof(iconset), "%s/iconset", tmpdir);

	CHECK(write_file(conf, "\"not a dictionary\"\n"));
	rc = wspawnvp_wait((char *[]){ (char *)tool_path("geticonset"), NULL });
	CHECK(rc != 0);
	rc = wspawnvp_wait((char *[]){ (char *)tool_path("seticons"), iconset, NULL });
	CHECK(rc != 0);

	CHECK(write_file(conf, "{ Key = Value; }\n"));
	CHECK(write_file(iconset, "\"not a dictionary\"\n"));
	rc = wspawnvp_wait((char *[]){ (char *)tool_path("seticons"), iconset, NULL });
	CHECK(rc != 0);
	wfree(conf);
}

static void test_wmmenugen_fifo(void)
{
	char *dir = wstrconcat(tmpdir, "/fifo-dir");
	char *fifo = wstrconcat(dir, "/menu.desktop");
	int rc;

	CHECK(dir != NULL && fifo != NULL);
	if (!dir || !fifo) {
		wfree(dir);
		wfree(fifo);
		return;
	}
	CHECK(mkdir(dir, 0700) == 0);
	CHECK(mkfifo(fifo, 0600) == 0);

	rc = run_tool_timeout((char *[]){ (char *)tool_path("wmmenugen"),
					  "-parser:xdg", dir, NULL }, 3);
	CHECK(rc != -1);
	CHECK(rc != -2);

	unlink(fifo);
	rmdir(dir);
	wfree(fifo);
	wfree(dir);
}

int main(void)
{
	top_builddir = getenv("top_builddir");
	if (!top_builddir)
		top_builddir = ".";

	if (!mkdtemp(tmpdir)) {
		perror("mkdtemp");
		return 2;
	}
	setenv("WMAKER_USER_ROOT", tmpdir, 1);

	test_wdwrite_non_dictionary();
	test_icons_non_dictionary();
	test_wmmenugen_fifo();

	wspawnvp_wait((char *[]){ "rm", "-rf", tmpdir, NULL });

	return check_report("check_util_tools");
}
