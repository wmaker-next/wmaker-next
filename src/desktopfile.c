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
#include <ctype.h>
#include <limits.h>
#include <locale.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

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
			out->exec_raw = wstrdup(line + 5);
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
	wfree(e->exec_raw);
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


/* ---- XDG application cache --------------------------------------- */

#define XDG_MAX_APP_DIRS 16

static WMArray *app_cache;
static WMHashTable *app_by_id;
static char *app_dir_path[XDG_MAX_APP_DIRS];
static time_t app_dir_mtime[XDG_MAX_APP_DIRS];
static int app_dir_count;

static void freeXDGApp(void *item)
{
	WXDGApp *a = item;

	wfree(a->id);
	wfree(a->name);
	wfree(a->name_fold);
	wfree(a->exec);
	wfree(a->exec_raw);
	wfree(a->comment);
	wfree(a);
}

static char *asciiFold(const char *s)
{
	char *out, *p;

	out = wstrdup(s);
	for (p = out; *p; p++)
		*p = tolower((unsigned char)*p);
	return out;
}

static Bool tryExecAvailable(const char *try_exec)
{
	char *found;

	if (!try_exec || !try_exec[0])
		return True;
	if (try_exec[0] == '/')
		return access(try_exec, X_OK) == 0;
	found = wfindfile(getenv("PATH"), try_exec);
	if (found) {
		wfree(found);
		return True;
	}
	return False;
}

static void appCacheAdd(const char *path, const char *id)
{
	WDesktopEntry de;
	WXDGApp *a;
	const char *term;

	if (WMHashGet(app_by_id, id))
		return;

	if (!wDesktopEntryRead(path, &de))
		return;

	if (!de.is_application || de.hidden || de.no_display ||
	    !de.name || !de.name[0] || !tryExecAvailable(de.try_exec)) {
		wDesktopEntryFree(&de);
		return;
	}

	a = wmalloc(sizeof(*a));
	a->id = wstrdup(id);
	a->name = de.name;
	a->name_fold = asciiFold(de.name);
	a->exec_raw = de.exec_raw;
	a->comment = de.comment;
	if (de.terminal) {
		term = getenv("WMAKER_TERM");
		if (!term || !term[0])
			term = "xterm -e ";
		a->exec = wstrconcat(term, de.exec);
		wfree(de.exec);
	} else {
		a->exec = de.exec;
	}
	wfree(de.try_exec);
	wfree(de.wmclass);
	wfree(de.icon);

	WMAddToArray(app_cache, a);
	WMHashInsert(app_by_id, a->id, a);
}

static void scanAppDir(const char *dir, const char *idprefix)
{
	DIR *dp;
	struct dirent *ent;
	struct stat st;
	char path[PATH_MAX], id[PATH_MAX];
	size_t nlen;

	dp = opendir(dir);
	if (!dp)
		return;
	while ((ent = readdir(dp)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;
		if ((size_t)snprintf(path, sizeof(path), "%s/%s",
				     dir, ent->d_name) >= sizeof(path))
			continue;
		nlen = strlen(ent->d_name);
		if (nlen > 8 && strcmp(ent->d_name + nlen - 8, ".desktop") == 0) {
			snprintf(id, sizeof(id), "%s%s", idprefix, ent->d_name);
			appCacheAdd(path, id);
		} else if (!idprefix[0] && stat(path, &st) == 0 &&
			   S_ISDIR(st.st_mode)) {
			/* one level of recursion for kde4/, wine/, flatpak
			 * exports, the spec encodes the subdirectory in the
			 * desktop-file-id with '/' folded to '-' */
			snprintf(id, sizeof(id), "%s-", ent->d_name);
			scanAppDir(path, id);
		}
	}
	closedir(dp);
}

static Bool scanAppRoot(const char *dir, void *ctx)
{
	struct stat st;

	(void)ctx;

	if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode))
		return True;
	if (app_dir_count < XDG_MAX_APP_DIRS) {
		app_dir_path[app_dir_count] = wstrdup(dir);
		app_dir_mtime[app_dir_count] = st.st_mtime;
		app_dir_count++;
	}
	scanAppDir(dir, "");
	return True;
}

struct mtimeCheck {
	int idx;
	Bool stale;
};

static Bool checkAppRoot(const char *dir, void *ctx)
{
	struct mtimeCheck *mc = ctx;
	struct stat st;

	if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode))
		return True;
	/* an existing applications directory must line up with what
	 * the previous scan recorded, in the same order, with the same
	 * mtime, otherwise the cache is stale */
	if (mc->idx >= app_dir_count ||
	    strcmp(dir, app_dir_path[mc->idx]) != 0 ||
	    st.st_mtime != app_dir_mtime[mc->idx]) {
		mc->stale = True;
		return False;
	}
	mc->idx++;
	return True;
}

static int compareXDGApp(const void *a, const void *b)
{
	const WXDGApp *aa = *(const WXDGApp * const *)a;
	const WXDGApp *bb = *(const WXDGApp * const *)b;

	return strcmp(aa->name_fold, bb->name_fold);
}

WMArray *wXDGAppList(void)
{
	struct mtimeCheck mc;

	if (app_cache) {
		mc.idx = 0;
		mc.stale = False;
		wXDGForEachDataDir("applications", checkAppRoot, &mc);
		if (mc.idx != app_dir_count)
			mc.stale = True;
		if (!mc.stale)
			return app_cache;
		wXDGAppListInvalidate();
	}

	app_cache = WMCreateArrayWithDestructor(64, freeXDGApp);
	app_by_id = WMCreateHashTable(WMStringPointerHashCallbacks);
	app_dir_count = 0;
	wXDGForEachDataDir("applications", scanAppRoot, NULL);
	WMSortArray(app_cache, compareXDGApp);
	return app_cache;
}

WXDGApp *wXDGAppById(const char *desktop_id)
{
	if (!app_by_id)
		return NULL;
	return WMHashGet(app_by_id, desktop_id);
}

void wXDGAppListInvalidate(void)
{
	int i;

	if (app_by_id) {
		WMFreeHashTable(app_by_id);
		app_by_id = NULL;
	}
	if (app_cache) {
		WMFreeArray(app_cache);
		app_cache = NULL;
	}
	for (i = 0; i < app_dir_count; i++) {
		wfree(app_dir_path[i]);
		app_dir_path[i] = NULL;
	}
	app_dir_count = 0;
}
