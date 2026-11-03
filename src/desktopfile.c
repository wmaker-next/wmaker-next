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
#include <fcntl.h>
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

static bool keyIsTrue(const char *v)
{
	return strcmp(v, "true") == 0 || strcmp(v, "True") == 0 ||
	       strcmp(v, "1") == 0;
}

bool wDesktopEntryRead(const char *path, WDesktopEntry *out)
{
	FILE *fp;
	char line[4096];
	int in_section = 0, name_rank = -1, comment_rank = -1, rank;
	int saw_type = 0;
	const char *v;

	memset(out, 0, sizeof(*out));

	fp = fopen(path, "r");
	if (!fp)
		return false;

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
		if (!out->mimetypes && strncmp(line, "MimeType=", 9) == 0) {
			out->mimetypes = wstrdup(line + 9);
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
		return false;
	}

	wDesktopExecStrip(out->exec);
	return true;
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
	wfree(e->mimetypes);
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


/* ---- hicolor icon-theme resolver --------------------------------- */

static void probeThemeIcon(const char *dir, const char *name,
			   int base_score, char **best, int *score)
{
#ifdef USE_SVG
	static const char * const ext[] = { "png", "svg", "xpm" };
#else
	static const char * const ext[] = { "png", "xpm" };
#endif
	char path[PATH_MAX];
	int e, sc;

	for (e = 0; e < (int)(sizeof(ext) / sizeof(ext[0])); e++) {
		snprintf(path, sizeof(path), "%s/%s.%s", dir, name, ext[e]);
		if (access(path, R_OK) != 0)
			continue;
		sc = base_score + e;	/* PNG wins ties over XPM */
		if (sc < *score) {
			wfree(*best);
			*best = wstrdup(path);
			*score = sc;
		}
		return;
	}
}

struct iconScanState {
	const char *name;
	const char *context;
	int want;
	char *best;
	int score;
};

static bool scanHicolorDir(const char *base, void *ctx)
{
	struct iconScanState *st = ctx;
	char dir[PATH_MAX];
	DIR *dp;
	struct dirent *de;
	int w, h, delta, sc;

	snprintf(dir, sizeof(dir), "%s/hicolor", base);
	dp = opendir(dir);
	if (!dp)
		return true;
	while ((de = readdir(dp))) {
#ifdef USE_SVG
		if (strcmp(de->d_name, "scalable") == 0) {
			/* rank between an exact-size raster hit (score 0)
			 * and any resample, so a shipped NxN PNG at the
			 * wanted size still wins but a scalable render
			 * beats scaling a wrong-size raster */
			snprintf(dir, sizeof(dir), "%s/hicolor/scalable/%s",
				 base, st->context);
			probeThemeIcon(dir, st->name, 1, &st->best, &st->score);
			continue;
		}
#endif
		if (sscanf(de->d_name, "%dx%d", &w, &h) != 2 || w != h)
			continue;
		/* prefer the smallest raster at or above the target,
		 * else the largest below it, lower score wins */
		delta = w - st->want;
		sc = (delta >= 0) ? 2 * delta : 4096 - 2 * delta;
		snprintf(dir, sizeof(dir), "%s/hicolor/%s/%s",
			 base, de->d_name, st->context);
		probeThemeIcon(dir, st->name, sc, &st->best, &st->score);
	}
	closedir(dp);
	return true;
}

static bool scanPixmapDir(const char *base, void *ctx)
{
	struct iconScanState *st = ctx;

	/* legacy pixmaps directory, no size encoded so rank below any
	 * sized hicolor hit but above nothing */
	probeThemeIcon(base, st->name, 1 << 16, &st->best, &st->score);
	return true;
}

char *wXDGFindThemeIcon(const char *name, const char *context,
			int want_size)
{
	struct iconScanState st;

	if (!name || !context || strchr(name, '/') || strchr(context, '/'))
		return NULL;

	st.name = name;
	st.context = context;
	st.want = want_size;
	st.best = NULL;
	st.score = INT_MAX;

	wXDGForEachDataDir("icons", scanHicolorDir, &st);
	wXDGForEachDataDir("pixmaps", scanPixmapDir, &st);

	return st.best;
}


/* ---- XDG application cache --------------------------------------- */

#define XDG_MAX_APP_DIRS 16

static WMArray *app_cache;
static WMHashTable *app_by_id;
static WMHashTable *apps_for_mime;	/* type -> WMArray of WXDGApp* */
static WMArray *apps_for_mime_empty;
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
	wfree(a->icon);
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

static bool tryExecAvailable(const char *try_exec)
{
	char *found;

	if (!try_exec || !try_exec[0])
		return true;
	if (try_exec[0] == '/')
		return access(try_exec, X_OK) == 0;
	found = wfindfile(getenv("PATH"), try_exec);
	if (found) {
		wfree(found);
		return true;
	}
	return false;
}

static void appCacheIndexMime(WXDGApp *a, char *mimetypes)
{
	WMArray *list;
	char *p, *q;

	if (!mimetypes)
		return;
	for (p = mimetypes; *p; p = q) {
		q = strchr(p, ';');
		if (q)
			*q++ = 0;
		else
			q = p + strlen(p);
		if (!*p)
			continue;
		list = WMHashGet(apps_for_mime, p);
		if (!list) {
			list = WMCreateArray(2);
			WMHashInsert(apps_for_mime, p, list);
		}
		WMAddToArray(list, a);
	}
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
	a->icon = de.icon;
	a->terminal = de.terminal;
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

	WMAddToArray(app_cache, a);
	WMHashInsert(app_by_id, a->id, a);
	appCacheIndexMime(a, de.mimetypes);
	wfree(de.mimetypes);
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

static bool scanAppRoot(const char *dir, void *ctx)
{
	struct stat st;

	(void)ctx;

	if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode))
		return true;
	if (app_dir_count < XDG_MAX_APP_DIRS) {
		app_dir_path[app_dir_count] = wstrdup(dir);
		app_dir_mtime[app_dir_count] = st.st_mtime;
		app_dir_count++;
	}
	scanAppDir(dir, "");
	return true;
}

struct mtimeCheck {
	int idx;
	bool stale;
};

static bool checkAppRoot(const char *dir, void *ctx)
{
	struct mtimeCheck *mc = ctx;
	struct stat st;

	if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode))
		return true;
	/* an existing applications directory must line up with what
	 * the previous scan recorded, in the same order, with the same
	 * mtime, otherwise the cache is stale */
	if (mc->idx >= app_dir_count ||
	    strcmp(dir, app_dir_path[mc->idx]) != 0 ||
	    st.st_mtime != app_dir_mtime[mc->idx]) {
		mc->stale = true;
		return false;
	}
	mc->idx++;
	return true;
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
		mc.stale = false;
		wXDGForEachDataDir("applications", checkAppRoot, &mc);
		if (mc.idx != app_dir_count)
			mc.stale = true;
		if (!mc.stale)
			return app_cache;
		wXDGAppListInvalidate();
	}

	app_cache = WMCreateArrayWithDestructor(64, freeXDGApp);
	app_by_id = WMCreateHashTable(WMStringPointerHashCallbacks);
	apps_for_mime = WMCreateHashTable(WMStringHashCallbacks);
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

WMArray *wXDGAppsForMime(const char *type)
{
	WMArray *list;

	wXDGAppList();
	list = type ? WMHashGet(apps_for_mime, type) : NULL;
	if (list)
		return list;
	if (!apps_for_mime_empty)
		apps_for_mime_empty = WMCreateArray(0);
	return apps_for_mime_empty;
}

void wXDGAppListInvalidate(void)
{
	WMHashEnumerator e;
	void *val, *key;
	int i;

	if (app_by_id) {
		WMFreeHashTable(app_by_id);
		app_by_id = NULL;
	}
	if (apps_for_mime) {
		e = WMEnumerateHashTable(apps_for_mime);
		while (WMNextHashEnumeratorItemAndKey(&e, &val, &key))
			WMFreeArray(val);
		WMFreeHashTable(apps_for_mime);
		apps_for_mime = NULL;
	}
	if (apps_for_mime_empty) {
		WMFreeArray(apps_for_mime_empty);
		apps_for_mime_empty = NULL;
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


/* ---- MIME type and default handler resolver ---------------------- */

/* same shape as wXDGForEachDataDir but over the XDG_CONFIG_HOME and
 * XDG_CONFIG_DIRS base directories */
static void forEachConfigDir(const char *suffix, WXDGDirFn fn, void *ctx)
{
	const char *home, *dirs, *p, *q;
	char dir[PATH_MAX];
	size_t n, slen;

	slen = strlen(suffix);

	home = getenv("XDG_CONFIG_HOME");
	if (home && home[0] == '/') {
		snprintf(dir, sizeof(dir), "%s/%s", home, suffix);
	} else {
		home = getenv("HOME");
		snprintf(dir, sizeof(dir), "%s/.config/%s",
			 home ? home : "", suffix);
	}
	if (!fn(dir, ctx))
		return;

	dirs = getenv("XDG_CONFIG_DIRS");
	if (!dirs || !dirs[0])
		dirs = "/etc/xdg";
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

struct mimeGlob {
	char *type;
	char *ext;	/* original-case extension when cs is set */
	int weight;
	int cs;		/* case-sensitive flag */
};

struct mimeMagic {
	char *type;
	unsigned char *bytes;
	int len;
	int priority;
};

static WMHashTable *mime_by_ext;	/* lower-cased ext -> struct mimeGlob* */
static WMHashTable *mime_default;	/* mimetype -> desktop-file-id */
static WMArray *mime_magic;		/* struct mimeMagic*, prio desc */
static int mime_globs_loaded;

static void mimeGlobAdd(const char *ext, const char *type, int weight, int cs)
{
	struct mimeGlob *g;
	char *key, *p;

	key = wstrdup(ext);
	for (p = key; *p; p++)
		*p = tolower((unsigned char)*p);
	g = WMHashGet(mime_by_ext, key);
	if (g) {
		/* earlier directories (XDG_DATA_HOME) win on equal weight */
		if (weight <= g->weight) {
			wfree(key);
			return;
		}
		wfree(g->type);
		wfree(g->ext);
	} else {
		g = wmalloc(sizeof(*g));
		/* WMStringHashCallbacks dups the key on insert */
		WMHashInsert(mime_by_ext, key, g);
	}
	g->type = wstrdup(type);
	g->ext = cs ? wstrdup(ext) : NULL;
	g->weight = weight;
	g->cs = cs;
	wfree(key);
}

static void freeMimeMagic(void *item)
{
	struct mimeMagic *m = item;

	wfree(m->type);
	wfree(m->bytes);
	wfree(m);
}

static int compareMimeMagic(const void *a, const void *b)
{
	const struct mimeMagic *ma = *(const struct mimeMagic * const *)a;
	const struct mimeMagic *mb = *(const struct mimeMagic * const *)b;

	if (ma->priority != mb->priority)
		return mb->priority - ma->priority;
	/* longer signatures first at equal priority so a 4-byte
	 * match is not shadowed by a 1-byte one */
	return mb->len - ma->len;
}

/* shared-mime-info magic file: "MIME-Magic\0\n" then a sequence of
 * "[prio:type]\n" section headers each followed by one or more
 * match lines "<indent>'>'<offset>'='<2-byte BE len><len bytes>
 * ['&'<len bytes mask>]['~'<digits>]['+'<digits>]'\n'", values are
 * length-prefixed binary so the parser reads byte-by-byte rather
 * than by line, only top-level zero-offset unmasked entries are
 * kept which covers PDF, PNG, ELF, gzip, zip, JPEG, '#!' and the
 * other common signatures without implementing the nested-rule
 * tree */
static bool loadMimeMagic(const char *dir, void *ctx)
{
	FILE *fp;
	char path[PATH_MAX], type[256], hdr[16];
	struct mimeMagic *m;
	int c, prio, indent, offset, len, keep, i;
	unsigned char *val;

	(void)ctx;

	snprintf(path, sizeof(path), "%s/magic", dir);
	fp = fopen(path, "rb");
	if (!fp)
		return true;
	if (fread(hdr, 1, 12, fp) != 12 ||
	    memcmp(hdr, "MIME-Magic\0\n", 12) != 0) {
		fclose(fp);
		return true;
	}
	c = getc(fp);
	while (c == '[') {
		prio = 0;
		while ((c = getc(fp)) != EOF && isdigit(c))
			prio = prio * 10 + (c - '0');
		i = 0;
		if (c == ':')
			while ((c = getc(fp)) != EOF && c != ']' &&
			       i < (int)sizeof(type) - 1)
				type[i++] = (char)c;
		type[i] = 0;
		if (c != ']' || getc(fp) != '\n')
			break;
		c = getc(fp);
		while (c != '[' && c != EOF) {
			indent = 0;
			while (isdigit(c)) {
				indent = indent * 10 + (c - '0');
				c = getc(fp);
			}
			if (c != '>')
				goto out;
			offset = 0;
			while ((c = getc(fp)) != EOF && isdigit(c))
				offset = offset * 10 + (c - '0');
			if (c != '=')
				goto out;
			len = getc(fp) << 8;
			len |= getc(fp);
			if (len < 0 || len > 4096)
				goto out;
			val = wmalloc(len ? len : 1);
			if ((int)fread(val, 1, len, fp) != len) {
				wfree(val);
				goto out;
			}
			keep = indent == 0 && offset == 0;
			c = getc(fp);
			if (c == '&') {
				keep = 0;
				for (i = 0; i < len; i++)
					getc(fp);
				c = getc(fp);
			}
			while (c == '~' || c == '+') {
				keep = 0;
				while ((c = getc(fp)) != EOF && isdigit(c))
					;
			}
			if (c != '\n') {
				wfree(val);
				goto out;
			}
			if (keep && type[0] && len > 0) {
				m = wmalloc(sizeof(*m));
				m->type = wstrdup(type);
				m->bytes = val;
				m->len = len;
				m->priority = prio;
				WMAddToArray(mime_magic, m);
			} else {
				wfree(val);
			}
			c = getc(fp);
		}
	}
out:
	fclose(fp);
	return true;
}

static const char *mimeMagicSniff(const char *path)
{
	struct mimeMagic *m;
	unsigned char head[512];
	int fd, n, i;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return NULL;
	n = (int)read(fd, head, sizeof(head));
	close(fd);
	if (n <= 0)
		return NULL;
	for (i = 0; i < WMGetArrayItemCount(mime_magic); i++) {
		m = WMGetFromArray(mime_magic, i);
		if (m->len <= n && memcmp(head, m->bytes, m->len) == 0)
			return m->type;
	}
	return NULL;
}

static bool loadMimeGlobs(const char *dir, void *ctx)
{
	FILE *fp;
	char path[PATH_MAX], line[512];
	char *type, *glob, *flags, *p;
	int weight, cs;

	(void)ctx;

	snprintf(path, sizeof(path), "%s/globs2", dir);
	fp = fopen(path, "r");
	if (!fp)
		return true;
	while (fgets(line, sizeof(line), fp)) {
		p = strpbrk(line, "\r\n");
		if (p)
			*p = 0;
		if (line[0] == '#' || line[0] == 0)
			continue;
		/* weight:type:glob[:flags] */
		type = strchr(line, ':');
		if (!type)
			continue;
		*type++ = 0;
		weight = atoi(line);
		glob = strchr(type, ':');
		if (!glob)
			continue;
		*glob++ = 0;
		flags = strchr(glob, ':');
		if (flags)
			*flags++ = 0;
		cs = flags && strstr(flags, "cs") != NULL;
		/* only literal-suffix globs *.ext, no [], ?, or extra * */
		if (glob[0] != '*' || glob[1] != '.' || glob[2] == 0)
			continue;
		if (strpbrk(glob + 1, "*?["))
			continue;
		mimeGlobAdd(glob + 2, type, weight, cs);
	}
	fclose(fp);
	return true;
}

const char *wXDGMimeTypeForPath(const char *path)
{
	const char *base, *p, *best;
	struct mimeGlob *g, *hit;
	char fold[256];
	size_t n, i;
	int blen;

	if (!mime_globs_loaded) {
		mime_by_ext = WMCreateHashTable(WMStringHashCallbacks);
		mime_magic = WMCreateArrayWithDestructor(64, freeMimeMagic);
		wXDGForEachDataDir("mime", loadMimeGlobs, NULL);
		wXDGForEachDataDir("mime", loadMimeMagic, NULL);
		WMSortArray(mime_magic, compareMimeMagic);
		mime_globs_loaded = 1;
	}

	base = strrchr(path, '/');
	base = base ? base + 1 : path;
	n = strlen(base);
	if (n == 0)
		return NULL;
	if (n >= sizeof(fold))
		return mimeMagicSniff(path);
	for (i = 0; i < n; i++)
		fold[i] = tolower((unsigned char)base[i]);
	fold[n] = 0;

	/* longest matching suffix wins, walk every dot in the basename
	 * from the leftmost so .tar.gz beats .gz when both exist with
	 * equal weight, but a higher-weight shorter match still wins */
	best = NULL;
	hit = NULL;
	blen = -1;
	for (p = fold; (p = strchr(p, '.')) != NULL; p++) {
		g = WMHashGet(mime_by_ext, p + 1);
		if (!g)
			continue;
		if (g->cs && strcmp(g->ext, base + (p + 1 - fold)) != 0)
			continue;
		if (!hit || g->weight > hit->weight ||
		    (g->weight == hit->weight &&
		     (int)strlen(p + 1) > blen)) {
			hit = g;
			best = g->type;
			blen = (int)strlen(p + 1);
		}
	}
	/* fall back to a bounded content sniff for extensionless
	 * files (README, downloaded blobs) so the document search
	 * can still pick a handler */
	if (!best)
		best = mimeMagicSniff(path);
	return best;
}

/* split a semicolon-separated desktop-id list and return the first
 * id whose application is present in the visible cache, or NULL */
static const char *firstAvailableId(char *list)
{
	char *p, *q;

	for (p = list; p && *p; p = q) {
		q = strchr(p, ';');
		if (q)
			*q++ = 0;
		if (*p && wXDGAppById(p))
			return p;
	}
	return NULL;
}

enum { ASSOC_DEFAULT, ASSOC_ADDED, ASSOC_CACHE };

static void loadAssocFile(const char *path, int kind)
{
	FILE *fp;
	char line[1024];
	char *eq, *p;
	const char *id;
	int section = -1;

	fp = fopen(path, "r");
	if (!fp)
		return;
	while (fgets(line, sizeof(line), fp)) {
		p = strpbrk(line, "\r\n");
		if (p)
			*p = 0;
		if (line[0] == '#' || line[0] == 0)
			continue;
		if (line[0] == '[') {
			if (strcmp(line, "[Default Applications]") == 0)
				section = ASSOC_DEFAULT;
			else if (strcmp(line, "[Added Associations]") == 0)
				section = ASSOC_ADDED;
			else if (strcmp(line, "[MIME Cache]") == 0)
				section = ASSOC_CACHE;
			else
				section = -1;
			continue;
		}
		if (section != kind)
			continue;
		eq = strchr(line, '=');
		if (!eq || eq == line)
			continue;
		*eq++ = 0;
		if (WMHashGet(mime_default, line))
			continue;
		id = firstAvailableId(eq);
		if (id)
			WMHashInsert(mime_default, line, wstrdup(id));
	}
	fclose(fp);
}

static bool loadConfigAssoc(const char *path, void *ctx)
{
	loadAssocFile(path, *(int *)ctx);
	return true;
}

static bool loadDataAssoc(const char *dir, void *ctx)
{
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/mimeapps.list", dir);
	loadAssocFile(path, *(int *)ctx);
	return true;
}

static bool loadMimeCache(const char *dir, void *ctx)
{
	char path[PATH_MAX];

	(void)ctx;
	snprintf(path, sizeof(path), "%s/mimeinfo.cache", dir);
	loadAssocFile(path, ASSOC_CACHE);
	return true;
}

const char *wXDGDefaultAppFor(const char *mimetype)
{
	static WMArray *built_against;
	WMHashEnumerator e;
	void *val, *key;
	int kind;

	if (!mimetype)
		return NULL;
	/* the association files reference desktop ids so the
	 * application cache must be populated first, and the
	 * default-handler table is only valid against the cache it
	 * was built from, drop it when the app list has been rebuilt */
	wXDGAppList();
	if (mime_default && built_against != app_cache) {
		e = WMEnumerateHashTable(mime_default);
		while (WMNextHashEnumeratorItemAndKey(&e, &val, &key))
			wfree(val);
		WMFreeHashTable(mime_default);
		mime_default = NULL;
	}
	if (!mime_default) {
		built_against = app_cache;
		mime_default = WMCreateHashTable(WMStringHashCallbacks);
		/* precedence per the mime-apps spec, defaults before
		 * added associations, config dirs before data dirs,
		 * mimeinfo.cache last */
		kind = ASSOC_DEFAULT;
		forEachConfigDir("mimeapps.list", loadConfigAssoc, &kind);
		wXDGForEachDataDir("applications", loadDataAssoc, &kind);
		kind = ASSOC_ADDED;
		forEachConfigDir("mimeapps.list", loadConfigAssoc, &kind);
		wXDGForEachDataDir("applications", loadDataAssoc, &kind);
		wXDGForEachDataDir("applications", loadMimeCache, NULL);
	}
	return WMHashGet(mime_default, mimetype);
}

static bool execHasQuotedFieldCode(const char *exec)
{
	bool squote = false, dquote = false;

	while (*exec) {
		if (squote) {
			if (*exec == '%' && exec[1] && strchr("fFuUc", exec[1]))
				return true;
			if (*exec == '\'')
				squote = false;
			exec++;
			continue;
		}
		if (dquote) {
			if (*exec == '%' && exec[1] && strchr("fFuUc", exec[1]))
				return true;
			if (*exec == '\\' && exec[1] &&
			    strchr("\"\\$`", exec[1])) {
				exec += 2;
				continue;
			}
			if (*exec == '"')
				dquote = false;
			exec++;
			continue;
		}
		if (*exec == '\\' && exec[1]) {
			exec += 2;
			continue;
		}
		if (*exec == '\'')
			squote = true;
		else if (*exec == '"')
			dquote = true;
		exec++;
	}
	return false;
}

static char *substituteExecPath(const char *exec_raw, const char *path,
				const char *name)
{
	const char *src, *run;
	char *out, *qpath, *qname, *seg;
	int saw_file = 0;

	if (execHasQuotedFieldCode(exec_raw))
		return NULL;

	qpath = wshellquote(path);
	qname = name ? wshellquote(name) : NULL;
	out = wstrdup("");
	src = exec_raw;
	while (*src) {
		run = src;
		while (*src && !(*src == '%' && src[1]))
			src++;
		if (src != run) {
			seg = wstrndup(run, src - run);
			out = wstrappend(out, seg);
			wfree(seg);
		}
		if (!*src)
			break;
		switch (src[1]) {
		case 'f': case 'F': case 'u': case 'U':
			out = wstrappend(out, qpath);
			saw_file = 1;
			break;
		case 'c':
			if (qname)
				out = wstrappend(out, qname);
			break;
		case '%':
			out = wstrappend(out, "%");
			break;
		default:
			/* %i %k and unknown codes drop */
			break;
		}
		src += 2;
	}
	if (!saw_file) {
		out = wstrappend(out, " ");
		out = wstrappend(out, qpath);
	}
	wfree(qpath);
	wfree(qname);
	return out;
}

char *wXDGAppOpenCommand(const WXDGApp *app, const char *path)
{
	const char *term;
	char *cmd, *wrapped;

	cmd = substituteExecPath(app->exec_raw, path, app->name);
	if (!cmd)
		return NULL;
	if (app->terminal) {
		term = getenv("WMAKER_TERM");
		if (!term || !term[0])
			term = "xterm -e ";
		wrapped = wstrconcat(term, cmd);
		wfree(cmd);
		cmd = wrapped;
	}
	return cmd;
}

char *wXDGOpenCommandFor(const char *path)
{
	const char *type, *id;
	WXDGApp *app;
	char *cmd, *qpath;

	type = wXDGMimeTypeForPath(path);
	id = type ? wXDGDefaultAppFor(type) : NULL;
	app = id ? wXDGAppById(id) : NULL;

	if (app && app->exec_raw) {
		cmd = wXDGAppOpenCommand(app, path);
		if (cmd)
			return cmd;
	}
	qpath = wshellquote(path);
	cmd = wstrconcat("xdg-open ", qpath);
	wfree(qpath);
	return cmd;
}

/* ---- fuzzy matcher ----------------------------------------------- */

static int isWordStart(const char *orig, int i)
{
	unsigned char prev, cur;

	if (i == 0)
		return 1;
	prev = (unsigned char)orig[i - 1];
	cur = (unsigned char)orig[i];
	if (prev == ' ' || prev == '-' || prev == '_' ||
	    prev == '.' || prev == '/')
		return 1;
	/* lower -> upper CamelCase hump */
	if (islower(prev) && isupper(cur))
		return 1;
	return 0;
}

int wFuzzyScore(const char *needle_fold, const char *hay_fold,
		const char *hay_orig)
{
	int i, h, prev, gap_total, run, score;

	if (!needle_fold[0])
		return -1;

	score = 100;
	gap_total = 0;
	prev = -2;
	run = 0;
	h = 0;
	for (i = 0; needle_fold[i]; i++) {
		while (hay_fold[h] && hay_fold[h] != needle_fold[i])
			h++;
		if (!hay_fold[h])
			return -1;
		if (i == 0 && h == 0)
			score += 15;
		if (isWordStart(hay_orig, h))
			score += 8;
		if (h == prev + 1) {
			run++;
			if (run == 1)
				score += 4;
		} else {
			if (i > 0)
				gap_total += h - prev - 1;
			run = 0;
		}
		prev = h;
		h++;
	}
	/* cap the gap penalty so very long names do not underflow */
	if (gap_total > 80)
		gap_total = 80;
	return score - gap_total;
}

int wFrecencyScore(int hits, time_t last, time_t now)
{
	double age_days, s;

	if (hits <= 0)
		return 0;
	age_days = (double)(now > last ? now - last : 0) / 86400.0;
	s = (double)hits / (1.0 + age_days / 30.0);
	if (s > 100.0)
		s = 100.0;
	return (int)(s + 0.5);
}


/* ---- document index ---------------------------------------------- */

#define XDG_MAX_DOC_ROOTS 32
/* IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO, expanded
 * here so this file does not need <sys/inotify.h> on platforms
 * that lack it, the watch hook is a no-op there anyway */
#define XDG_DOC_WATCH_MASK (0x100u | 0x200u | 0x40u | 0x80u)

static WMArray *doc_cache;
static char *doc_root_path[XDG_MAX_DOC_ROOTS];
static time_t doc_root_mtime[XDG_MAX_DOC_ROOTS];
static int doc_root_wd[XDG_MAX_DOC_ROOTS];
static int doc_root_count;
static int doc_depth_cached, doc_max_cached;
static char *doc_extra_cached;
static bool doc_watch_dirty;

static int (*doc_watch_add)(const char *, unsigned, void (*)(void));
static void (*doc_watch_remove)(int);

void wXDGSetDocWatchHooks(int (*add)(const char *, unsigned,
				     void (*)(void)),
			  void (*remove)(int))
{
	doc_watch_add = add;
	doc_watch_remove = remove;
}

static void docWatchFired(void)
{
	/* defer the rebuild to the next wXDGDocList() call so a
	 * burst of moves only rescans once and the scan happens on
	 * the dialog path rather than inside the inotify handler */
	doc_watch_dirty = true;
}

static void freeXDGDoc(void *item)
{
	WXDGDoc *d = item;

	wfree(d->path);
	wfree(d->base_fold);
	wfree(d);
}

static void scanDocDir(const char *dir, int depth, int max_files)
{
	DIR *dp;
	struct dirent *ent;
	struct stat st;
	WXDGDoc *d;
	char path[PATH_MAX];
	const char *base;

	if (WMGetArrayItemCount(doc_cache) >= max_files)
		return;
	dp = opendir(dir);
	if (!dp)
		return;
	/* skip whole subtrees that carry a no-index marker */
	snprintf(path, sizeof(path), "%s/.nomedia", dir);
	if (access(path, F_OK) == 0) {
		closedir(dp);
		return;
	}
	snprintf(path, sizeof(path), "%s/.git", dir);
	if (access(path, F_OK) == 0) {
		closedir(dp);
		return;
	}
	while ((ent = readdir(dp)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;
		if ((size_t)snprintf(path, sizeof(path), "%s/%s",
				     dir, ent->d_name) >= sizeof(path))
			continue;
		if (stat(path, &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode)) {
			if (depth > 1)
				scanDocDir(path, depth - 1, max_files);
			continue;
		}
		if (!S_ISREG(st.st_mode))
			continue;
		if (WMGetArrayItemCount(doc_cache) >= max_files)
			break;
		d = wmalloc(sizeof(*d));
		d->path = wstrdup(path);
		base = strrchr(d->path, '/');
		d->base = base ? base + 1 : d->path;
		d->base_fold = asciiFold(d->base);
		d->mtime = st.st_mtime;
		WMAddToArray(doc_cache, d);
	}
	closedir(dp);
}

static void addDocRoot(char *root, int depth, int max_files)
{
	struct stat st;
	int i;

	if (!root)
		return;
	for (i = 0; i < doc_root_count; i++) {
		if (strcmp(root, doc_root_path[i]) == 0) {
			wfree(root);
			return;
		}
	}
	if (stat(root, &st) != 0 || !S_ISDIR(st.st_mode)) {
		wfree(root);
		return;
	}
	if (doc_root_count < XDG_MAX_DOC_ROOTS) {
		doc_root_path[doc_root_count] = root;
		doc_root_mtime[doc_root_count] = st.st_mtime;
		doc_root_wd[doc_root_count] = doc_watch_add
			? (*doc_watch_add)(root, XDG_DOC_WATCH_MASK,
					   docWatchFired)
			: -1;
		doc_root_count++;
	} else {
		wfree(root);
		return;
	}
	scanDocDir(doc_root_path[doc_root_count - 1], depth, max_files);
}

WMArray *wXDGDocList(char **extra_roots, int n_extra,
		     int depth, int max_files)
{
	struct stat st;
	char *extra_key;
	int i, stale;

	if (depth < 1)
		depth = 1;
	if (max_files < 1)
		max_files = 1;

	/* fold the caller-supplied extra roots into a single key so a
	 * RunDialogDocumentPaths edit invalidates the cache without
	 * waiting for an mtime bump */
	extra_key = wstrdup("");
	for (i = 0; i < n_extra; i++) {
		extra_key = wstrappend(extra_key, extra_roots[i]);
		extra_key = wstrappend(extra_key, ":");
	}

	if (doc_cache) {
		stale = doc_watch_dirty ||
			depth != doc_depth_cached ||
			max_files != doc_max_cached ||
			strcmp(extra_key, doc_extra_cached) != 0;
		for (i = 0; !stale && i < doc_root_count; i++) {
			if (stat(doc_root_path[i], &st) != 0 ||
			    st.st_mtime != doc_root_mtime[i])
				stale = 1;
		}
		if (!stale) {
			wfree(extra_key);
			return doc_cache;
		}
		wXDGDocListInvalidate();
	}

	doc_cache = WMCreateArrayWithDestructor(64, freeXDGDoc);
	doc_root_count = 0;
	doc_watch_dirty = false;
	doc_depth_cached = depth;
	doc_max_cached = max_files;
	doc_extra_cached = extra_key;

	for (i = 0; i < n_extra; i++)
		addDocRoot(wexpandpath(extra_roots[i]), depth, max_files);

	return doc_cache;
}

void wXDGDocListInvalidate(void)
{
	int i;

	if (doc_cache) {
		WMFreeArray(doc_cache);
		doc_cache = NULL;
	}
	for (i = 0; i < doc_root_count; i++) {
		if (doc_watch_remove && doc_root_wd[i] >= 0)
			(*doc_watch_remove)(doc_root_wd[i]);
		doc_root_wd[i] = -1;
		wfree(doc_root_path[i]);
		doc_root_path[i] = NULL;
	}
	doc_root_count = 0;
	wfree(doc_extra_cached);
	doc_extra_cached = NULL;
}

void wXDGMimeInvalidate(void)
{
	WMHashEnumerator e;
	struct mimeGlob *g;
	void *val, *key;

	if (mime_by_ext) {
		e = WMEnumerateHashTable(mime_by_ext);
		while (WMNextHashEnumeratorItemAndKey(&e, &val, &key)) {
			g = val;
			wfree(g->type);
			wfree(g->ext);
			wfree(g);
		}
		WMFreeHashTable(mime_by_ext);
		mime_by_ext = NULL;
	}
	if (mime_magic) {
		WMFreeArray(mime_magic);
		mime_magic = NULL;
	}
	if (mime_default) {
		e = WMEnumerateHashTable(mime_default);
		while (WMNextHashEnumeratorItemAndKey(&e, &val, &key))
			wfree(val);
		WMFreeHashTable(mime_default);
		mime_default = NULL;
	}
	mime_globs_loaded = 0;
}
