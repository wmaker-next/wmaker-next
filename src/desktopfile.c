/* desktopfile.c - freedesktop .desktop entry reader and XDG dir walk
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 2009-2026 Window Maker Team
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

#include "wconfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <locale.h>

#include <WINGs/WUtil.h>

#ifndef PATH_MAX
#define PATH_MAX DEFAULT_PATH_MAX
#endif

#include "desktopfile.h"

/*
 * Rank a .desktop key line against the LC_MESSAGES locale. Returns a
 * pointer to the value when the line is "<key>=", "<key>[ll]=" or
 * "<key>[ll_CC]=" with rank 0/1/2 written to *rank, NULL otherwise.
 * Modifier and encoding suffixes on the locale are ignored.
 */
static const char *matchLocaleKey(const char *line, const char *key,
				  size_t klen, int *rank)
{
	static int have_loc = -1;
	static char ll[3], ll_cc[6];
	const char *p, *end;
	size_t n;

	if (have_loc < 0) {
		const char *loc = setlocale(LC_MESSAGES, NULL);

		if (!loc)
			loc = getenv("LANG");
		have_loc = 0;
		if (loc && loc[0] && loc[1] &&
		    strcmp(loc, "C") != 0 && strcmp(loc, "POSIX") != 0) {
			ll[0] = loc[0]; ll[1] = loc[1]; ll[2] = 0;
			have_loc = 1;
			if (loc[2] == '_' && loc[3] && loc[4]) {
				memcpy(ll_cc, loc, 5); ll_cc[5] = 0;
				have_loc = 2;
			}
		}
	}

	if (strncmp(line, key, klen) != 0)
		return NULL;
	p = line + klen;
	if (*p == '=') {
		*rank = 0;
		return p + 1;
	}
	if (*p != '[' || !have_loc)
		return NULL;
	p++;
	end = strchr(p, ']');
	if (!end || end[1] != '=')
		return NULL;
	n = (size_t)(end - p);
	if (have_loc >= 2 && n == 5 && memcmp(p, ll_cc, 5) == 0) {
		*rank = 2;
		return end + 2;
	}
	if (n == 2 && memcmp(p, ll, 2) == 0) {
		*rank = 1;
		return end + 2;
	}
	return NULL;
}

void wDesktopExecStrip(char *exec)
{
	char *src = exec, *dst = exec;

	/* drop freedesktop %f/%F/%u/%U/%i/%c/%k field codes, keep %% as % */
	while (*src) {
		if (*src == '%' && src[1]) {
			if (src[1] == '%')
				*dst++ = '%';
			src += 2;
			continue;
		}
		*dst++ = *src++;
	}
	*dst = 0;
	while (dst > exec && (dst[-1] == ' ' || dst[-1] == '\t'))
		*--dst = 0;
}

static Bool keyIsTrue(const char *v)
{
	return strcmp(v, "true") == 0 || strcmp(v, "True") == 0 ||
	       strcmp(v, "1") == 0;
}

Bool wDesktopEntryRead(const char *path, WDesktopEntry *out)
{
	FILE *fp;
	char line[4096];
	int in_section = 0, name_rank = -1, comment_rank = -1, rank;
	int saw_type = 0;
	const char *v;

	memset(out, 0, sizeof(*out));

	fp = fopen(path, "r");
	if (!fp)
		return False;

	while (fgets(line, sizeof(line), fp)) {
		char *eol = strpbrk(line, "\r\n");
		if (eol)
			*eol = 0;
		if (line[0] == '#' || line[0] == 0)
			continue;
		if (line[0] == '[') {
			if (in_section)
				break;
			if (strcmp(line, "[Desktop Entry]") == 0)
				in_section = 1;
			continue;
		}
		if (!in_section)
			continue;
		if (!out->exec && strncmp(line, "Exec=", 5) == 0) {
			out->exec = wstrdup(line + 5);
			continue;
		}
		if (!out->try_exec && strncmp(line, "TryExec=", 8) == 0) {
			out->try_exec = wstrdup(line + 8);
			continue;
		}
		if (!out->icon && strncmp(line, "Icon=", 5) == 0) {
			out->icon = wstrdup(line + 5);
			continue;
		}
		if (!out->wmclass && strncmp(line, "StartupWMClass=", 15) == 0) {
			out->wmclass = wstrdup(line + 15);
			continue;
		}
		if (strncmp(line, "Type=", 5) == 0) {
			saw_type = 1;
			out->is_application = strcmp(line + 5, "Application") == 0;
			continue;
		}
		if (strncmp(line, "NoDisplay=", 10) == 0) {
			out->no_display = keyIsTrue(line + 10);
			continue;
		}
		if (strncmp(line, "Hidden=", 7) == 0) {
			out->hidden = keyIsTrue(line + 7);
			continue;
		}
		if (strncmp(line, "Terminal=", 9) == 0) {
			out->terminal = keyIsTrue(line + 9);
			continue;
		}
		v = matchLocaleKey(line, "Name", 4, &rank);
		if (v) {
			if (rank > name_rank) {
				wfree(out->name);
				out->name = wstrdup(v);
				name_rank = rank;
			}
			continue;
		}
		v = matchLocaleKey(line, "Comment", 7, &rank);
		if (v && rank > comment_rank) {
			wfree(out->comment);
			out->comment = wstrdup(v);
			comment_rank = rank;
		}
	}
	fclose(fp);

	if (!saw_type)
		out->is_application = 1;

	if (!out->exec || !out->exec[0]) {
		wDesktopEntryFree(out);
		return False;
	}

	wDesktopExecStrip(out->exec);
	return True;
}

void wDesktopEntryFree(WDesktopEntry *e)
{
	wfree(e->name);
	wfree(e->comment);
	wfree(e->exec);
	wfree(e->try_exec);
	wfree(e->wmclass);
	wfree(e->icon);
	memset(e, 0, sizeof(*e));
}

void wXDGForEachDataDir(const char *suffix, WXDGDirFn fn, void *ctx)
{
	const char *home, *dirs, *p, *q;
	char dir[PATH_MAX];
	size_t n, slen;

	slen = strlen(suffix);

	home = getenv("XDG_DATA_HOME");
	if (home && home[0] == '/') {
		snprintf(dir, sizeof(dir), "%s/%s", home, suffix);
	} else {
		home = getenv("HOME");
		snprintf(dir, sizeof(dir), "%s/.local/share/%s",
			 home ? home : "", suffix);
	}
	if (!fn(dir, ctx))
		return;

	dirs = getenv("XDG_DATA_DIRS");
	if (!dirs || !dirs[0])
		dirs = "/usr/local/share:/usr/share";
	for (p = dirs; *p; p = *q ? q + 1 : q) {
		q = strchr(p, ':');
		if (!q)
			q = p + strlen(p);
		n = (size_t)(q - p);
		while (n > 1 && p[n - 1] == '/')
			n--;
		if (n == 0 || n + 1 + slen + 1 > sizeof(dir))
			continue;
		memcpy(dir, p, n);
		dir[n] = '/';
		memcpy(dir + n + 1, suffix, slen + 1);
		if (!fn(dir, ctx))
			return;
	}
}
