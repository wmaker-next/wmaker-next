/*
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

/* dialog_run.c - advanced run/input dialog with completion */

#include "wconfig.h"

#include <xkbcommon/xkbcommon-keysyms.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <dirent.h>
#include <time.h>

#include <cairo.h>
#include <WINGs/WINGsP.h>

#include "WindowMaker.h"
#include "GNUstep.h"
#include "screen.h"
#include "window.h"
#include "desktopfile.h"
#include "dialog_run.h"
#include "misc.h"
#include "wxkb.h"
#include "framewin.h"
#include "xinerama.h"
#include "calc.h"
#include "tabgroup.h"
#include "workspace.h"
#include "application.h"
#include "appicon.h"
#include "icon.h"
#include "event.h"

typedef enum {
	RUN_ACTION_EXEC = 0,
	RUN_ACTION_DISMISS,
	RUN_ACTION_FOCUS_WINDOW,
	RUN_ACTION_SWITCH_WORKSPACE
} RunVariantAction;

typedef struct RunVariant {
	char *text;	/* suffix to append, or full word when replace */
	char *label;	/* shown in mLbl while selected, may be NULL */
	char *icon_hint;/* "/abs/path", "apps:name" or "mimetypes:name" */
	const char *frec_key;	/* desktop-id or doc path, source-owned */
	const char *doc_path;	/* WXDGDoc.path for document rows */
	const char *doc_mime;	/* its MIME type, both source-owned */
	int rank;	/* sort band, lower first */
	int score;	/* match quality, higher first within a band */
	time_t mtime;	/* recency tiebreak within the document band */
	bool replace;	/* True: text replaces the current word */
	RunVariantAction action;
	xcb_window_t focus_xid;	/* client_win of a window row, re-resolved at use */
	int target_workspace;
} RunVariant;

/* rank bands, lower sorts first */
enum {
	RUN_RANK_APP_PREFIX = 0,
	RUN_RANK_APP_SUBSTR = 1,
	RUN_RANK_PATH = 2,
	RUN_RANK_DOC_PREFIX = 10,
	RUN_RANK_DOC_SUBSTR = 11,
	RUN_RANK_WORKSPACE = 10,
	RUN_RANK_WINDOW = 15,
	RUN_RANK_CALC = 20
};

typedef struct _WMInputPanelWithHistory {
	WMInputPanel *panel;
	WMArray *history;
	int histpos;
	char *prefix;
	char *suffix;
	char *rest;
	int word_off;		/* byte offset of current word in prefix */
	char *saved_msg;	/* original mLbl text, restored on reset */
	WMArray *variants;
	int varpos;
	WScreen *scr;
	WWindow *wwin;		/* the managed dialog window */
	WMWindow *dropWin;	/* override-redirect candidate popup */
	WMList *dropList;
	WMFont *dropBold;
	WMFont *dropNorm;
	WMColor *dropDim;
	WMHashTable *dropIcons;	/* icon_hint -> WMPixmap*, panel-owned */
	int dropItemH;
	int dropIconSize;
	bool dropMapped;
	bool filtering;		/* regenerate variants on every edit */
	bool rerun;		/* re-enter the modal loop after Escape */
	char *accepted_key;	/* frec_key of the variant Return picked */
	WMArray *outer_variants;/* saved during an open-with sub-cycle */
	int outer_varpos;
} WMInputPanelWithHistory;

static RunVariant *makeVariant(char *text, char *label, int rank, bool replace)
{
	RunVariant *v;

	v = wmalloc(sizeof(*v));
	v->text = text;
	v->label = label;
	v->rank = rank;
	v->replace = replace;
	return v;
}

static void freeVariant(void *item)
{
	RunVariant *v = item;

	wfree(v->text);
	wfree(v->label);
	wfree(v->icon_hint);
	wfree(v);
}

/* ---- frecency store ----------------------------------------------
 *
 * One proplist dictionary under $XDG_STATE_HOME/WindowMaker keyed
 * on desktop-file-id or absolute document path, each value is
 * { hits = N; last = epoch; }. Loaded lazily on the first match,
 * written when the dialog accepts a frecency-tracked variant.
 */

static WMPropList *frec_db;
static WMPropList *frec_hk, *frec_lk;
static time_t frec_now;

static char *frecencyFileName(void)
{
	const char *state, *home;
	char *path;

	state = getenv("XDG_STATE_HOME");
	if (state && state[0] == '/') {
		path = wstrconcat(state, "/" PACKAGE_TARNAME);
		mkdir(state, 0700);
	} else {
		home = getenv("HOME");
		path = wstrconcat(home ? home : "",
				  "/.local/state/" PACKAGE_TARNAME);
	}
	mkdir(path, 0700);
	return wstrappend(path, "/RunFrecency");
}

static void frecencyLoad(void)
{
	char *file;

	if (frec_db)
		return;
	file = frecencyFileName();
	frec_db = WMReadPropListFromFile(file);
	wfree(file);
	if (!frec_db || !WMIsPLDictionary(frec_db)) {
		if (frec_db)
			WMReleasePropList(frec_db);
		frec_db = WMCreatePLDictionary(NULL, NULL);
	}
	if (!frec_hk) {
		frec_hk = WMCreatePLString("hits");
		frec_lk = WMCreatePLString("last");
	}
	frec_now = time(NULL);
}

static int frecencyBonus(const char *key)
{
	WMPropList *e, *k, *h, *l;
	int hits;
	time_t last;

	if (!key)
		return 0;
	frecencyLoad();
	k = WMCreatePLString(key);
	e = WMGetFromPLDictionary(frec_db, k);
	WMReleasePropList(k);
	if (!e || !WMIsPLDictionary(e))
		return 0;
	h = WMGetFromPLDictionary(e, frec_hk);
	l = WMGetFromPLDictionary(e, frec_lk);
	hits = h && WMIsPLString(h) ? atoi(WMGetFromPLString(h)) : 0;
	last = l && WMIsPLString(l) ? (time_t)atol(WMGetFromPLString(l)) : 0;
	return wFrecencyScore(hits, last, frec_now);
}

static void frecencyRecord(const char *key)
{
	WMPropList *e, *k, *h, *v;
	WMPropList *keys, *ek, *ev;
	char buf[32], *file;
	int hits, i, ehits;
	time_t elast;

	if (!key)
		return;
	frecencyLoad();
	k = WMCreatePLString(key);
	e = WMGetFromPLDictionary(frec_db, k);
	hits = 0;
	if (e && WMIsPLDictionary(e)) {
		h = WMGetFromPLDictionary(e, frec_hk);
		if (h && WMIsPLString(h))
			hits = atoi(WMGetFromPLString(h));
	}
	hits++;
	e = WMCreatePLDictionary(NULL, NULL);
	snprintf(buf, sizeof(buf), "%d", hits);
	v = WMCreatePLString(buf);
	WMPutInPLDictionary(e, frec_hk, v);
	WMReleasePropList(v);
	snprintf(buf, sizeof(buf), "%ld", (long)time(NULL));
	v = WMCreatePLString(buf);
	WMPutInPLDictionary(e, frec_lk, v);
	WMReleasePropList(v);
	WMPutInPLDictionary(frec_db, k, e);
	WMReleasePropList(k);
	WMReleasePropList(e);

	/* prune one-hit entries older than 90 days so the file does
	 * not grow without bound */
	keys = WMGetPLDictionaryKeys(frec_db);
	for (i = WMGetPropListItemCount(keys) - 1; i >= 0; i--) {
		ek = WMGetFromPLArray(keys, i);
		ev = WMGetFromPLDictionary(frec_db, ek);
		if (!ev || !WMIsPLDictionary(ev))
			continue;
		h = WMGetFromPLDictionary(ev, frec_hk);
		ehits = h && WMIsPLString(h) ? atoi(WMGetFromPLString(h)) : 0;
		h = WMGetFromPLDictionary(ev, frec_lk);
		elast = h && WMIsPLString(h)
		      ? (time_t)atol(WMGetFromPLString(h)) : 0;
		if (ehits <= 1 && frec_now - elast > 90 * 86400)
			WMRemoveFromPLDictionary(frec_db, ek);
	}
	WMReleasePropList(keys);

	file = frecencyFileName();
	WMWritePropListToFile(frec_db, file);
	wfree(file);
}

static char *HistoryFileName(const char *name)
{
	const char *state;
	char *base = NULL;
	char *filename = NULL;

	/* XDG basedir spec requires the value to be absolute, otherwise it
	 * is ignored and we fall back to the legacy .AppInfo location */
	state = getenv("XDG_STATE_HOME");
	if (state && state[0] == '/')
		base = wexpandpath(state);

	if (base) {
		filename = wstrconcat(base, "/" PACKAGE_TARNAME);
		/* this tree lives outside wusergnusteppath() so the
		 * wmkdirhier() call inside WMWritePropListToFile() will not
		 * create it, the spec says the application creates the
		 * missing directories itself with mode 0700 */
		mkdir(base, 0700);
		mkdir(filename, 0700);
		wfree(base);
		filename = wstrappend(filename, "/History");
	} else {
		filename = wstrconcat(wusergnusteppath(),
				      "/.AppInfo/" PACKAGE_TARNAME "/History");
	}

	if (name && strlen(name)) {
		filename = wstrappend(filename, ".");
		filename = wstrappend(filename, name);
	}
	return filename;
}

static int strmatch(const void *str1, const void *str2)
{
	return !strcmp((const char *)str1, (const char *)str2);
}

static WMArray *LoadHistory(const char *filename, int max)
{
	WMPropList *plhistory;
	WMPropList *plitem;
	WMArray *history;
	int i, num;
	char *str;

	history = WMCreateArrayWithDestructor(1, wfree);
	WMAddToArray(history, wstrdup(""));

	plhistory = WMReadPropListFromFile(filename);

	if (plhistory) {
		if (WMIsPLArray(plhistory)) {
			num = WMGetPropListItemCount(plhistory);

			for (i = 0; i < num; ++i) {
				plitem = WMGetFromPLArray(plhistory, i);
				if (WMIsPLString(plitem)) {
					str = WMGetFromPLString(plitem);
					if (WMFindInArray(history, strmatch, str) == WANotFound) {
						/*
						 * The string here is duplicated because it will be freed
						 * automatically when the array is deleted. This is not really
						 * great because it is already an allocated string,
						 * unfortunately we cannot re-use it because it will be freed
						 * when we discard the PL (and we don't want to waste the PL's
						 * memory either)
						 */
						WMAddToArray(history, wstrdup(str));
						if (--max <= 0)
							break;
					}
				}
			}
		}
		WMReleasePropList(plhistory);
	}

	return history;
}

static void SaveHistoryFiltered(WMArray *history, const char *filename, char prefix_char)
{
	int i;
	WMPropList *plhistory, *item;
	const char *s;

	plhistory = WMCreatePLArray(NULL);
	for (i = 0; i < WMGetArrayItemCount(history); ++i) {
		s = WMGetFromArray(history, i);
		if (prefix_char != 0) {
			if (s[0] != prefix_char)
				continue;
		} else {
			if (s[0] == '=' && w_global.config->run_calc)
				continue;
		}
		item = WMCreatePLString(s);
		WMAddToPLArray(plhistory, item);
		WMReleasePropList(item);
	}
	WMWritePropListToFile(plhistory, filename);
	WMReleasePropList(plhistory);
}

static void mergeCalcHistory(WMArray *history)
{
	char *calcfile = HistoryFileName("Calc");
	WMArray *calchist;
	int ci, cn, cap;

	cap = w_global.config->history_lines;
	calchist = LoadHistory(calcfile, cap);
	cn = WMGetArrayItemCount(calchist);
	for (ci = 0; ci < cn; ci++) {
		const char *entry = WMGetFromArray(calchist, ci);

		if (!entry || !entry[0])
			continue;
		if (WMGetArrayItemCount(history) >= cap)
			break;
		if (WMFindInArray(history, strmatch, (void *)entry) == WANotFound)
			WMAddToArray(history, wstrdup(entry));
	}
	WMFreeArray(calchist);
	wfree(calcfile);
}

static void noteCalcLine(WMInputPanelWithHistory *p)
{
	char *text, *calcfile;
	int i;

	if (!p->panel || !p->panel->text)
		return;
	text = WMGetTextFieldText(p->panel->text);
	if (!text || text[0] != '=') {
		if (text)
			wfree(text);
		return;
	}
	for (i = 0; i < WMGetArrayItemCount(p->history); i++)
		if (strcmp(WMGetFromArray(p->history, i), text) == 0)
			break;
	if (i >= WMGetArrayItemCount(p->history) &&
	    WMGetArrayItemCount(p->history) < w_global.config->history_lines) {
		wfree(WMReplaceInArray(p->history, 0, wstrdup(text)));
	}
	calcfile = HistoryFileName("Calc");
	SaveHistoryFiltered(p->history, calcfile, '=');
	wfree(calcfile);
	wfree(text);
}

static int compareVariants(const void *a, const void *b)
{
	const RunVariant *va = *(const RunVariant * const *)a;
	const RunVariant *vb = *(const RunVariant * const *)b;
	const char *ka, *kb;

	if (va->rank != vb->rank)
		return va->rank - vb->rank;
	if (va->score != vb->score)
		return vb->score - va->score;
	/* within the document band newer files come first */
	if (va->mtime != vb->mtime)
		return va->mtime > vb->mtime ? -1 : 1;
	ka = va->label ? va->label : va->text;
	kb = vb->label ? vb->label : vb->text;
	return strcasecmp(ka, kb);
}

static int matchVariantText(const void *item, const void *cdata)
{
	const RunVariant *v = item;

	return strcmp(v->text, cdata) == 0;
}

static void ScanFiles(const char *dir, const char *prefix, WMArray *result)
{
	int prefixlen;
	DIR *d;
	struct dirent *de;
	struct stat sb;
	char *fullfilename, *suffix;

	prefixlen = strlen(prefix);
	d = opendir(dir);
	if (d != NULL) {
		while ((de = readdir(d)) != NULL) {
			if (strlen(de->d_name) > prefixlen &&
			    !strncmp(prefix, de->d_name, prefixlen) &&
			    strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..")) {
				fullfilename = wstrconcat((char *)dir, "/");
				fullfilename = wstrappend(fullfilename, de->d_name);

				if (stat(fullfilename, &sb) == 0 &&
				    WMFindInArray(result, matchVariantText,
						  de->d_name + prefixlen) == WANotFound) {
					suffix = wstrdup(de->d_name + prefixlen);
					if (sb.st_mode & S_IFDIR)
						suffix = wstrappend(suffix, "/");
					WMAddToArray(result, makeVariant(suffix, NULL,
									 RUN_RANK_PATH, false));
				}
				wfree(fullfilename);
			}
		}
		closedir(d);
	}
}

/* match needle against a candidate according to RunDialogMatching,
 * returns False on no match, else writes the rank-band offset (0
 * for a prefix hit, 1 otherwise) and a higher-is-better score */
static bool matchName(const char *needle, const char *hay_fold,
		      const char *hay_orig, int *band, int *score)
{
	const char *hit;

	switch (w_global.config->run_matching) {
	case RUN_MATCH_PREFIX:
		if (strncmp(hay_fold, needle, strlen(needle)) != 0)
			return false;
		*band = 0;
		*score = 0;
		return true;
	case RUN_MATCH_FUZZY:
		*score = wFuzzyScore(needle, hay_fold, hay_orig);
		if (*score < 0)
			return false;
		*band = strncmp(hay_fold, needle, strlen(needle)) == 0 ? 0 : 1;
		return true;
	case RUN_MATCH_SUBSTRING:
	default:
		hit = strstr(hay_fold, needle);
		if (!hit)
			return false;
		*band = (hit == hay_fold) ? 0 : 1;
		*score = (hit == hay_fold) ? 1 : 0;
		return true;
	}
}

static bool matchExecName(const char *needle, const char *exec,
			  int *band, int *score)
{
	char name[256], fold[256];
	const char *start, *base;
	size_t i, n;

	if (!exec)
		return false;
	while (*exec == ' ' || *exec == '\t')
		exec++;
	start = exec;
	if (*exec == '\'' || *exec == '"') {
		char quote = *exec++;

		start = exec;
		while (*exec && *exec != quote)
			exec++;
	} else {
		while (*exec && *exec != ' ' && *exec != '\t')
			exec++;
	}

	n = (size_t)(exec - start);
	if (n == 0 || n >= sizeof(name))
		return false;
	memcpy(name, start, n);
	name[n] = 0;
	base = strrchr(name, '/');
	if (base)
		base++;
	else
		base = name;
	n = strlen(base);
	if (n == 0)
		return false;
	for (i = 0; i < n; i++)
		fold[i] = tolower((unsigned char)base[i]);
	fold[n] = 0;

	return matchName(needle, fold, base, band, score);
}

static char *makeAppLabel(const WXDGApp *a)
{
	char *label;

	if (!a->comment || !a->comment[0])
		return wstrdup(a->name);
	label = wstrconcat(a->name, " \xe2\x80\x94 ");
	return wstrappend(label, a->comment);
}

static void matchXDGApps(const char *word, WMArray *result)
{
	WMArray *apps;
	WXDGApp *a;
	RunVariant *v;
	char fold[256];
	size_t i, n;
	int band, score;

	n = strlen(word);
	if (n == 0 || n >= sizeof(fold))
		return;
	for (i = 0; i < n; i++)
		fold[i] = tolower((unsigned char)word[i]);
	fold[n] = 0;

	apps = wXDGAppList();
	for (i = 0; i < (size_t)WMGetArrayItemCount(apps); i++) {
		a = WMGetFromArray(apps, i);
		if (matchName(fold, a->name_fold, a->name, &band, &score)) {
			/* Name= is preferred because it is shown in the candidate label. */
		} else if (!matchExecName(fold, a->exec_raw, &band, &score)) {
			continue;
		}
		v = makeVariant(wstrdup(a->exec), makeAppLabel(a),
				RUN_RANK_APP_PREFIX + band, true);
		v->frec_key = a->id;
		v->score = score + frecencyBonus(a->id);
		if (a->icon)
			v->icon_hint = a->icon[0] == '/'
				     ? wstrdup(a->icon)
				     : wstrconcat("apps:", a->icon);
		WMAddToArray(result, v);
	}
}

/* ---- $PATH executable index --------------------------------------
 *
 * matchPathDir used to opendir and stat every $PATH directory on
 * every Tab and on every live-filter keystroke. Build the executable
 * name set once instead, and invalidate it through inotify on the
 * directories with a TTL as the fallback for missed events.
 */

#define RUN_PATH_INDEX_TTL 30
#define RUN_PATH_WATCH_MAX 64
/* IN_ATTRIB | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO,
 * expanded so this file needs no <sys/inotify.h> */
#define RUN_PATH_WATCH_MASK (0x100u | 0x200u | 0x40u | 0x80u | 0x4u)

static WMHashTable *path_index;
static char *path_index_env;
static time_t path_index_built;
static bool path_index_dirty = true;
static int path_wd[RUN_PATH_WATCH_MAX];
static int path_wd_count;

static void pathIndexInvalidated(void)
{
	path_index_dirty = true;
}

static void freePathIndex(void)
{
	int i;

	if (path_index) {
		WMFreeHashTable(path_index);
		path_index = NULL;
	}
	if (path_index_env) {
		wfree(path_index_env);
		path_index_env = NULL;
	}
	for (i = 0; i < path_wd_count; i++)
		wInotifyRemoveWatch(path_wd[i]);
	path_wd_count = 0;
}

static void watchPathDir(const char *dir, WMHashTable *watched)
{
	int wd;

	if (path_wd_count >= RUN_PATH_WATCH_MAX || WMHashGet(watched, dir))
		return;
	wd = wInotifyAddWatch(dir, RUN_PATH_WATCH_MASK, pathIndexInvalidated);
	if (wd >= 0) {
		path_wd[path_wd_count++] = wd;
		WMHashInsert(watched, dir, watched);
	}
}

static void scanPathIndexDir(const char *dir, WMHashTable *index)
{
	DIR *dp;
	struct dirent *de;
	struct stat sb;
	char *full;
	size_t dlen;

	dp = opendir(dir);
	if (!dp)
		return;
	while ((de = readdir(dp)) != NULL) {
		dlen = strlen(de->d_name);
		if (dlen == 0 || dlen >= 256)
			continue;
		/* first executable occurrence in PATH order wins */
		if (WMHashGet(index, de->d_name))
			continue;
		full = wstrconcat(dir, "/");
		full = wstrappend(full, de->d_name);
		if (stat(full, &sb) == 0 && (sb.st_mode & 0111) &&
		    !S_ISDIR(sb.st_mode))
			WMHashInsert(index, de->d_name, index);
		wfree(full);
	}
	closedir(dp);
}

static void buildPathIndex(void)
{
	WMHashTable *watched;
	const char *pos, *next;
	char *dir;

	freePathIndex();

	path_index = WMCreateHashTable(WMStringHashCallbacks);
	path_index_env = wstrdup(getenv("PATH") ? getenv("PATH") : "");
	watched = WMCreateHashTable(WMStringHashCallbacks);

	pos = path_index_env;
	while (pos && *pos) {
		next = strchr(pos, ':');
		if (next) {
			dir = wstrndup(pos, next - pos);
			pos = next + 1;
		} else {
			dir = wstrdup(pos);
			pos = NULL;
		}
		if (dir[0] != '\0') {
			watchPathDir(dir, watched);
			scanPathIndexDir(dir, path_index);
		}
		wfree(dir);
	}

	WMFreeHashTable(watched);
	path_index_built = time(NULL);
	path_index_dirty = false;
}

static void matchPathIndex(const char *word, const char *fold, WMArray *result)
{
	WMHashEnumerator e;
	const char *name;
	RunVariant *v;
	char namefold[256];
	size_t k, dlen, wlen = strlen(word);
	int band, score;

	e = WMEnumerateHashTable(path_index);
	while ((name = WMNextHashEnumeratorKey(&e)) != NULL) {
		dlen = strlen(name);
		if (dlen == 0 || dlen >= sizeof(namefold))
			continue;
		for (k = 0; k <= dlen; k++)
			namefold[k] = tolower((unsigned char)name[k]);
		if (!matchName(fold, namefold, name, &band, &score))
			continue;
		/* the suffix variant only works when the case the
		 * user typed matches the binary on disk, otherwise
		 * the rebuilt field would not run, so fall through
		 * to a replacing variant carrying the real name */
		if (band == 0 && strncmp(name, word, wlen) != 0)
			band = 1;
		/* a prefix hit stays a non-replacing suffix so the
		 * pre-series Tab feel is preserved in every mode. An
		 * exact name match completes to itself, which is the
		 * no-op Tab already performs at varpos 0, so skip it
		 * rather than show an empty row in the popup */
		if (band == 0) {
			if (name[wlen] == '\0')
				continue;
			v = makeVariant(wstrdup(name + wlen), NULL,
					RUN_RANK_PATH, false);
		} else {
			v = makeVariant(wstrdup(name), NULL,
					RUN_RANK_PATH + 1, true);
		}
		v->score = score;
		v->icon_hint = wstrdup("mimetypes:application-x-executable");
		WMAddToArray(result, v);
	}
}

static char *prettyDir(const char *path)
{
	const char *home, *slash;
	char *dir;
	size_t hlen;

	slash = strrchr(path, '/');
	dir = slash && slash != path ? wstrndup(path, slash - path)
				     : wstrdup("/");
	home = getenv("HOME");
	hlen = home ? strlen(home) : 0;
	if (hlen && strncmp(dir, home, hlen) == 0 &&
	    (dir[hlen] == '/' || dir[hlen] == 0)) {
		char *tilde = wstrconcat("~", dir + hlen);
		wfree(dir);
		dir = tilde;
	}
	return dir;
}

static void matchDocuments(const char *word, WMArray *result)
{
	WMArray *docs;
	WXDGDoc *d;
	RunVariant *v;
	const char *mt;
	char fold[256], hint[128], *cmd, *dir, *label, *hp;
	size_t i, n;
	int band, score;

	n = strlen(word);
	if (n == 0 || n >= sizeof(fold))
		return;
	for (i = 0; i < n; i++)
		fold[i] = tolower((unsigned char)word[i]);
	fold[n] = 0;

	docs = wXDGDocList(w_global.config->run_doc_paths.items,
			   (int)w_global.config->run_doc_paths.count,
			   w_global.config->run_doc_depth,
			   w_global.config->run_doc_max);
	for (i = 0; i < (size_t)WMGetArrayItemCount(docs); i++) {
		d = WMGetFromArray(docs, i);
		if (!matchName(fold, d->base_fold, d->base, &band, &score))
			continue;
		cmd = wXDGOpenCommandFor(d->path);
		dir = prettyDir(d->path);
		if (strncmp(cmd, "xdg-open ", 9) == 0) {
			label = wstrconcat(d->base,
					   " \xe2\x80\x94 open with default handler");
		} else {
			label = wstrconcat(d->base, " \xe2\x80\x94 ");
			label = wstrappend(label, dir);
		}
		wfree(dir);
		v = makeVariant(cmd, label, RUN_RANK_DOC_PREFIX + band, true);
		v->frec_key = d->path;
		v->score = score + frecencyBonus(d->path);
		v->mtime = d->mtime;
		v->doc_path = d->path;
		mt = wXDGMimeTypeForPath(d->path);
		v->doc_mime = mt;
		if (mt && strncmp(mt, "image/", 6) == 0) {
			v->icon_hint = wstrdup(d->path);
		} else if (mt) {
			snprintf(hint, sizeof(hint), "mimetypes:%s", mt);
			for (hp = hint; *hp; hp++)
				if (*hp == '/')
					*hp = '-';
			v->icon_hint = wstrdup(hint);
		} else {
			v->icon_hint = wstrdup("mimetypes:text-x-generic");
		}
		WMAddToArray(result, v);
	}
}

#define CALC_ICON "apps:accessories-calculator"

static RunVariant *makeCalcVariant(const char *result, const char *err)
{
	RunVariant *v;
	char *label;

	if (err)
		label = wstrconcat("= ! ", err);
	else
		label = wstrconcat("= ", result);
	v = makeVariant(wstrdup(""), label, RUN_RANK_CALC, false);
	v->icon_hint = wstrdup(CALC_ICON);
	v->action = RUN_ACTION_DISMISS;
	return v;
}

static void matchCalc(const char *expr, WMArray *result)
{
	double val;
	char *err = NULL, *fmt;

	if (!CalcEvaluate(expr, &val, &err)) {
		WMAddToArray(result, makeCalcVariant(NULL, err ? err : "error"));
		if (err)
			wfree(err);
		return;
	}
	fmt = CalcFormatResult(val);
	WMAddToArray(result, makeCalcVariant(fmt, NULL));
	wfree(fmt);
}

#define IS_GNUSTEP_MENU_WIN(w) ((w)->wm_gnustep_attr && \
	((w)->wm_gnustep_attr->flags & GSWindowLevelAttr) && \
	((w)->wm_gnustep_attr->window_level == WMMainMenuWindowLevel || \
	 (w)->wm_gnustep_attr->window_level == WMSubmenuWindowLevel))

static bool ws_in_scope(WWindow *wwin, const int *scope, int count)
{
	int i;

	if (count == 0)
		return true;
	if (count < 0)	/* requested scope matched nothing */
		return false;
	for (i = 0; i < count; i++)
		if (WWIN_ON_WORKSPACE(wwin, scope[i]))
			return true;
	return false;
}

static int resolve_ws_scope(WScreen *scr, const char *ws, int *out, int max)
{
	int n, w, i, count;
	char nfld[256], fold[256];
	int band, score;
	size_t wlen;

	if (!ws || !*ws)
		return 0;
	if (sscanf(ws, "%i", &n) == 1) {
		w = n - 1;
		if (w >= 0 && w < scr->workspace_count && max >= 1) {
			out[0] = w;
			return 1;
		}
	}
	wlen = strlen(ws);
	if (wlen >= sizeof(nfld))
		return 0;
	for (i = 0; i < (int)wlen; i++)
		nfld[i] = tolower((unsigned char)ws[i]);
	nfld[wlen] = 0;
	count = 0;
	for (i = 0; i < scr->workspace_count && count < max; i++) {
		const char *name = scr->workspaces[i]->name;
		size_t ln;

		if (!name)
			continue;
		ln = strlen(name);
		if (ln >= sizeof(fold))
			continue;
		for (size_t k = 0; k <= ln; k++)
			fold[k] = tolower((unsigned char)name[k]);
		if (!matchName(nfld, fold, name, &band, &score))
			continue;
		out[count++] = i;
	}
	return count > 0 ? count : -1;
}

static RunVariant *makeWindowVariant(WWindow *wwin, int rank, int score)
{
	RunVariant *v;
	WScreen *scr = wwin->screen_ptr;
	const char *title, *wsname, *plus;
	char *label, tag[64], hint[32];

	title = (wwin->frame && wwin->frame->title) ? wwin->frame->title
		: (wwin->wm_title ? wwin->wm_title : "");
	tag[0] = 0;
	if (IS_OMNIPRESENT(wwin)) {
		snprintf(tag, sizeof(tag), "  [*]");
	} else if (WWIN_PRIMARY_WS(wwin) != scr->current_workspace) {
		wsname = scr->workspaces[WWIN_PRIMARY_WS(wwin)]->name;
		plus = (WWIN_WS_MASK(wwin) != WS_BIT(WWIN_PRIMARY_WS(wwin))) ? "+" : "";
		snprintf(tag, sizeof(tag), "  [%s%s]", wsname, plus);
	}
	label = wstrconcat(title, tag);
	v = makeVariant(wstrdup(""), label, rank, false);
	snprintf(hint, sizeof(hint), "win:0x%lx", (unsigned long)wwin->client_win);
	v->icon_hint = wstrdup(hint);
	v->score = score;
	v->action = RUN_ACTION_FOCUS_WINDOW;
	v->focus_xid = wwin->client_win;
	return v;
}

static int matchWindowFields(const char *fold, WWindow *wwin,
			     int *out_band, int *out_score)
{
	const char *fields[3];
	int band, score, best_band = -1, best_score = 0, fi;

	fields[0] = (wwin->frame && wwin->frame->title) ? wwin->frame->title
		  : (wwin->wm_title ? wwin->wm_title : NULL);
	fields[1] = wwin->wm_class;
	fields[2] = wwin->wm_instance;
	for (fi = 0; fi < 3; fi++) {
		char ff[256];
		const char *f = fields[fi];
		size_t fl;

		if (!f)
			continue;
		fl = strlen(f);
		if (fl >= sizeof(ff))
			continue;
		for (size_t k = 0; k <= fl; k++)
			ff[k] = tolower((unsigned char)f[k]);
		if (matchName(fold, ff, f, &band, &score)) {
			if (best_band < 0 || band < best_band ||
			    (band == best_band && score > best_score)) {
				best_band = band;
				best_score = score;
			}
		}
	}
	if (best_band < 0)
		return 0;
	*out_band = best_band;
	*out_score = best_score;
	return 1;
}

static void matchWindows(WScreen *scr, const char *complete, WMArray *result)
{
	WWindow *wwin;
	const char *needle, *hash;
	char wsn[256], fold[256];
	int scope[MAX_WORKSPACES];
	int scope_count = 0, band, score;
	size_t i, nlen, wlen;

	if (!scr)
		return;
	if (*complete == '@') {
		hash = strchr(complete, '#');
		if (!hash)
			return;
		wlen = (size_t)(hash - (complete + 1));
		if (wlen >= sizeof(wsn))
			return;
		memcpy(wsn, complete + 1, wlen);
		wsn[wlen] = 0;
		scope_count = resolve_ws_scope(scr, wsn, scope, scr->workspace_count);
		needle = hash + 1;
	} else if (complete[0] == '#' && complete[1] == '#') {
		scope[0] = scr->current_workspace;
		scope_count = 1;
		needle = complete + 2;
	} else {
		needle = complete + 1;
	}
	nlen = strlen(needle);
	if (nlen >= sizeof(fold))
		return;
	for (i = 0; i < nlen; i++)
		fold[i] = tolower((unsigned char)needle[i]);
	fold[nlen] = 0;
	for (wwin = scr->focused_window; wwin; wwin = wwin->prev) {
		if (wwin->flags.internal_window)
			continue;
		if (WFLAGP(wwin, skip_window_list))
			continue;
		if (IS_GNUSTEP_MENU_WIN(wwin))
			continue;
		if (!w_global.config->show_all_tabs_in_switch_panel
		    && wGroupIsInactiveMember(wwin))
			continue;
		if (!ws_in_scope(wwin, scope, scope_count))
			continue;
		if (!matchWindowFields(fold, wwin, &band, &score))
			continue;
		WMAddToArray(result, makeWindowVariant(wwin,
				RUN_RANK_WINDOW + band, score));
	}
}

static RunVariant *makeWorkspaceVariant(WScreen *scr, int ws_index, int rank, int score)
{
	RunVariant *v;
	const char *name;
	char *text;

	if (!scr || ws_index < 0 || ws_index >= scr->workspace_count)
		return NULL;
	name = scr->workspaces[ws_index]->name;
	text = wstrconcat("@", name ? name : "");
	v = makeVariant(text, wstrdup(name ? name : ""), rank, true);
	v->score = score;
	v->action = RUN_ACTION_SWITCH_WORKSPACE;
	v->target_workspace = ws_index;
	return v;
}

static void matchWorkspaces(WScreen *scr, const char *needle, WMArray *result)
{
	RunVariant *v;
	int n, w, i;
	char fold[256], ff[256];
	int band, score;
	size_t nlen, fl;

	if (!scr || scr->workspace_count <= 0)
		return;
	if (needle && *needle && sscanf(needle, "%i", &n) == 1) {
		w = n - 1;
		if (w >= 0 && w < scr->workspace_count) {
			v = makeWorkspaceVariant(scr, w, RUN_RANK_WORKSPACE, 0);
			if (v)
				WMAddToArray(result, v);
			return;
		}
	}
	nlen = needle ? strlen(needle) : 0;
	if (nlen >= sizeof(fold))
		return;
	for (i = 0; i < (int)nlen; i++)
		fold[i] = tolower((unsigned char)needle[i]);
	fold[nlen] = 0;
	for (i = 0; i < scr->workspace_count; i++) {
		const char *name = scr->workspaces[i]->name;

		if (!name)
			continue;
		fl = strlen(name);
		if (fl >= sizeof(ff))
			continue;
		for (size_t k = 0; k <= fl; k++)
			ff[k] = tolower((unsigned char)name[k]);
		if (!matchName(fold, ff, name, &band, &score))
			continue;
		v = makeWorkspaceVariant(scr, i, RUN_RANK_WORKSPACE + band, score);
		if (v)
			WMAddToArray(result, v);
	}
}

static WMArray *GenerateVariants(WScreen *scr, const char *complete, int *word_off)
{
	bool firstWord = true;
	WMArray *variants = NULL;
	const char *start, *pos = NULL, *pathenv = NULL;
	char *tmp = NULL, *dir = NULL, *prefix = NULL;
	char fold[256];
	size_t i, flen;

	variants = WMCreateArrayWithDestructor(0, freeVariant);

	start = complete;
	while (*complete == ' ')
		++complete;

	if (*complete != '=' && *complete != '#' && *complete != '@' && *complete != '?') {
		pos = strrchr(complete, ' ');
		if (pos != NULL) {
			complete = pos + 1;
			firstWord = false;
		}
	}
	*word_off = complete - start;

	pos = NULL;
	if (*complete != '=' && *complete != '#' && *complete != '@' && *complete != '?') {
		pos = strrchr(complete, '/');
	}
	if (pos != NULL) {
		tmp = wstrndup(complete, pos - complete + 1);
		if (*tmp == '~' && *(tmp + 1) == '/' && getenv("HOME")) {
			dir = wstrdup(getenv("HOME"));
			dir = wstrappend(dir, tmp + 1);
			wfree(tmp);
		} else {
			dir = tmp;
		}
		prefix = wstrdup(pos + 1);
		ScanFiles(dir, prefix, variants);
		wfree(dir);
		wfree(prefix);
	} else if (*complete == '~') {
		WMAddToArray(variants, makeVariant(wstrdup("/"), NULL,
						   RUN_RANK_PATH, false));
	} else if (firstWord && *complete == '?') {
		if (w_global.config->run_doc_search)
			matchDocuments(complete + 1, variants);
	} else if (firstWord && *complete == '@') {
		const char *hash = strchr(complete, '#');

		if (hash) {
			if (w_global.config->run_window_switcher)
				matchWindows(scr, complete, variants);
		} else {
			if (w_global.config->run_workspace_jumper)
				matchWorkspaces(scr, complete + 1, variants);
		}
	} else if (firstWord && *complete == '=') {
		if (w_global.config->run_calc)
			matchCalc(complete + 1, variants);
	} else if (firstWord && *complete == '#') {
		if (w_global.config->run_window_switcher)
			matchWindows(scr, complete, variants);
	} else if (firstWord) {
		flen = strlen(complete);
		if (flen >= sizeof(fold))
			flen = sizeof(fold) - 1;
		for (i = 0; i < flen; i++)
			fold[i] = tolower((unsigned char)complete[i]);
		fold[flen] = 0;
		if (w_global.config->run_xdg_completion)
			matchXDGApps(complete, variants);
		if (flen > 0) {
			pathenv = getenv("PATH");
			if (!path_index || path_index_dirty ||
			    strcmp(path_index_env, pathenv ? pathenv : "") != 0 ||
			    time(NULL) - path_index_built > RUN_PATH_INDEX_TTL)
				buildPathIndex();
			matchPathIndex(complete, fold, variants);
		}
		/* documents only when the command interpretation came
		 * up empty so fir<Tab> does not have to wade through
		 * firmware-notes.txt before reaching Firefox */
		if (w_global.config->run_doc_search &&
		    WMGetArrayItemCount(variants) == 0)
			matchDocuments(complete, variants);
	}

	WMSortArray(variants, compareVariants);
	return variants;
}

static void setVariantLabel(WMInputPanelWithHistory *p, RunVariant *v, int n)
{
	char buf[256];
	const char *label;

	if (!p->panel->mLbl)
		return;
	if (!p->saved_msg)
		p->saved_msg = wstrdup(WMGetLabelText(p->panel->mLbl));
	if (!v) {
		WMSetLabelText(p->panel->mLbl, p->saved_msg);
		return;
	}
	label = v->label;
	if (!label && n <= 1)
		return;
	if (!label)
		label = p->saved_msg;
	if (n > 1)
		snprintf(buf, sizeof(buf), "[%d/%d] %s", p->varpos, n, label);
	else
		snprintf(buf, sizeof(buf), "%s", label);
	WMSetLabelText(p->panel->mLbl, buf);
}

static void applyVariant(WMInputPanelWithHistory *p)
{
	RunVariant *v;
	char *text;
	unsigned pos;
	int n;

	n = WMGetArrayItemCount(p->variants);
	if (p->varpos > 0) {
		v = WMGetFromArray(p->variants, p->varpos - 1);
		if (v->replace) {
			text = wstrndup(p->prefix, p->word_off);
			text = wstrappend(text, v->text);
		} else {
			text = wstrconcat(p->prefix, v->text);
		}
		setVariantLabel(p, v, n);
	} else {
		text = wstrdup(p->prefix);
		setVariantLabel(p, NULL, n);
	}
	pos = strlen(text);
	text = wstrappend(text, p->suffix);
	WMSetTextFieldText(p->panel->text, text);
	WMSetTextFieldCursorPosition(p->panel->text, pos);
	wfree(text);
}

/* sentinel stored under hints whose theme lookup failed so the walk
 * is not redone on every repaint, never released */
static int dropIconMiss;

/* Same faded-icon rule as the window list panel: unmapped windows that
 * are shaded, miniaturized or hidden wash out against the tile */
static bool rowIconIsFaded(WWindow *wwin)
{
	if (wwin->flags.mapped)
		return false;
	if (wGroupIsInactiveMember(wwin))
		return true;
	if (!wwin->flags.shaded && !wwin->flags.miniaturized &&
	    !wwin->flags.hidden && !w_global.config->cycle_all_workspaces)
		return false;

	return true;
}

static WMPixmap *loadRowIcon(WMInputPanelWithHistory *p, const char *hint,
			     WMColor *back)
{
	WMPixmap *pix;
	RColor color;
	const char *colon, *key;
	char ctx[32], *path, *fkey = NULL;
	RImage *image;
	WWindow *wwin = NULL;
	int opaq = 255;
	bool is_win;

	if (!hint || !hint[0])
		return NULL;

	is_win = strncmp(hint, "win:", 4) == 0;
	if (is_win)
		wwin = wWindowFor(strtoul(hint + 4, NULL, 0));

	/* distinct cache slot so unshading/restoring repaints at full
	 * strength instead of reusing the faded pixels */
	key = hint;
	if (wwin && rowIconIsFaded(wwin)) {
		opaq = 50;
		fkey = wstrconcat(hint, "\x01");
		key = fkey;
	}

	pix = WMHashGet(p->dropIcons, key);
	if (pix) {
		wfree(fkey);
		return pix == (WMPixmap *)&dropIconMiss ? NULL : pix;
	}

	color.red = WMRedComponentOfColor(back) >> 8;
	color.green = WMGreenComponentOfColor(back) >> 8;
	color.blue = WMBlueComponentOfColor(back) >> 8;
	color.alpha = WMGetColorAlpha(back) >> 8;

	image = NULL;

	if (is_win) {
		if (wwin) {
			WApplication *wapp;

			wapp = wApplicationOf(wwin->main_window);
			if (wapp && wapp->app_icon && wapp->app_icon->icon &&
			    wapp->app_icon->icon->file_image)
				image = RRetainImage(wapp->app_icon->icon->file_image);
			if (!image && !WFLAGP(wwin, always_user_icon) &&
			    wwin->net_icon_image)
				image = RRetainImage(wwin->net_icon_image);
			if (!image)
				image = get_icon_image(p->scr, wwin->wm_instance,
						       wwin->wm_class, p->dropIconSize);
			if (image)
				image = wIconValidateIconSize(image, p->dropIconSize);
		}
	} else if (hint[0] == '/') {
		image = RLoadImage(p->scr->rcontext, hint, 0);
	} else {
		colon = strchr(hint, ':');
		if (!colon || (size_t)(colon - hint) >= sizeof(ctx)) {
			wfree(fkey);
			return NULL;
		}
		memcpy(ctx, hint, colon - hint);
		ctx[colon - hint] = 0;
		path = wXDGFindThemeIcon(colon + 1, ctx, p->dropIconSize);
		if (path) {
			image = RLoadImage(p->scr->rcontext, path, 0);
			wfree(path);
		}
	}

	if (image) {
		if (image->width > p->dropIconSize ||
		    image->height > p->dropIconSize) {
			int nw, nh;
			RImage *scaled;

			nw = image->width;
			nh = image->height;
			if (nw > p->dropIconSize) {
				nw = p->dropIconSize;
				nh = p->dropIconSize * image->height / image->width;
			}
			if (nh > p->dropIconSize) {
				nw = p->dropIconSize * image->width / image->height;
				nh = p->dropIconSize;
			}
			scaled = RSmoothScaleImage(image, nw, nh);
			RReleaseImage(image);
			image = scaled;
		}
		if (image) {
			if (opaq != 255) {
				RImage *flat;

				flat = RCreateImage(image->width, image->height,
						    false);
				if (flat) {
					RFillImage(flat, &color);
					RCombineAreaWithOpaqueness(flat, image,
								   0, 0, image->width,
								   image->height, 0, 0, opaq);
					RReleaseImage(image);
					image = flat;
				} else {
					RCombineImageWithColor(image, &color);
				}
			} else {
				RCombineImageWithColor(image, &color);
			}
			pix = WMCreatePixmapFromRImage(p->scr->wmscreen, image, 0);
			RReleaseImage(image);
		}
	}

	if (!pix) {
		WMHashInsert(p->dropIcons, key, &dropIconMiss);
		wfree(fkey);
		return NULL;
	}
	WMHashInsert(p->dropIcons, key, pix);
	wfree(fkey);
	return pix;
}

static void drawCandidate(WMList *lPtr, int index, xcb_drawable_t d,
			  char *text, int state, WMRect *rect)
{
	WMInputPanelWithHistory *p = WMGetHangedData(lPtr);
	WMScreen *wscr = p->scr->wmscreen;
	RunVariant *v;
	WMColor *back;
	WMPixmap *icon;
	WMSize isz;
	cairo_t *cr;
	const char *line1, *line2;
	bool selected;
	int fh, tx, iy;

	(void)index;

	v = WMGetListItem(lPtr, index)->clientData;
	selected = (state & WLDSSelected) != 0;
	back = selected ? W_SelectedBackColor(wscr) : p->scr->gray;

	cr = WCairoBeginForDrawable(wscr, d);
	W_SetSourceColor(cr, back);
	cairo_rectangle(cr, rect->pos.x, rect->pos.y,
			rect->size.width, rect->size.height);
	cairo_fill(cr);

	tx = rect->pos.x + p->dropItemH + 4;
	icon = loadRowIcon(p, v->icon_hint, p->scr->gray);
	if (icon) {
		isz = WMGetPixmapSize(icon);
		iy = rect->pos.y + (p->dropItemH - isz.height) / 2;
		W_PaintPixmap(cr, icon,
				  rect->pos.x + 2 +
				  (p->dropIconSize - isz.width) / 2, iy);
	}

	line1 = v->label ? v->label : text;
	line2 = v->label ? v->text : NULL;
	fh = WMFontHeight(p->dropBold);
	W_DrawString(cr, p->scr->black, p->dropBold,
			  tx, rect->pos.y + 2, line1, strlen(line1));
	if (line2)
		W_DrawString(cr, p->dropDim, p->dropNorm,
				  tx, rect->pos.y + 2 + fh,
				  line2, strlen(line2));
	WCairoEnd(cr);

	if (selected)
		WMReleaseColor(back);
}

static void hideDropList(WMInputPanelWithHistory *p);
static void resetVariants(WMInputPanelWithHistory *p);

static void candidateClicked(WMWidget *w, void *cdata)
{
	WMInputPanelWithHistory *p = cdata;
	int row;

	row = WMGetListSelectedItemRow((WMList *)w);
	if (row < 0 || !p->variants)
		return;
	p->varpos = row + 1;
	applyVariant(p);
	WMSetFocusToWidget(p->panel->text);
}

static void candidateActivate(WMWidget *w, void *cdata)
{
	WMInputPanelWithHistory *p = cdata;
	RunVariant *v;
	WWindow *twin;

	candidateClicked(w, cdata);
	if (p->variants && p->varpos > 0) {
		v = WMGetFromArray(p->variants, p->varpos - 1);
		if (v->frec_key) {
			wfree(p->accepted_key);
			p->accepted_key = wstrdup(v->frec_key);
		}
		if (v->action == RUN_ACTION_FOCUS_WINDOW) {
			twin = wWindowFor(v->focus_xid);
			if (twin)
				wWindowSingleFocus(twin);
			hideDropList(p);
			WMPerformButtonClick(p->panel->altBtn);
			return;
		}
		if (v->action == RUN_ACTION_SWITCH_WORKSPACE) {
			wWorkspaceChange(p->scr, v->target_workspace);
			hideDropList(p);
			WMPerformButtonClick(p->panel->altBtn);
			return;
		}
		if (v->action == RUN_ACTION_DISMISS) {
			noteCalcLine(p);
			hideDropList(p);
			WMPerformButtonClick(p->panel->altBtn);
			return;
		}
	}
	hideDropList(p);
	if (p->panel->defBtn)
		WMPerformButtonClick(p->panel->defBtn);
}

static void createDropList(WMInputPanelWithHistory *p)
{
	WMScreen *wscr = p->scr->wmscreen;
	int wmScaleWidth, wmScaleHeight;

	WMGetScaleBaseFromSystemFont(wscr, &wmScaleWidth, &wmScaleHeight);
	(void)wmScaleWidth;
	p->dropBold = WMBoldSystemFontOfSize(wscr, WMScaleY(12));
	p->dropNorm = WMSystemFontOfSize(wscr, WMScaleY(11));
	p->dropDim = WMDarkGrayColor(wscr);
	p->dropItemH = WMFontHeight(p->dropBold) +
		       WMFontHeight(p->dropNorm) + 6;
	p->dropIconSize = p->dropItemH - 4;
	p->dropIcons = WMCreateHashTable(WMStringHashCallbacks);

	p->dropWin = WMCreateWindowWithStyle(wscr, "runDrop",
					     WMBorderlessWindowMask);
	WMSetWindowLevel(p->dropWin, WMPopUpMenuWindowLevel);
	WMResizeWidget(p->dropWin, 100, p->dropItemH);

	p->dropList = WMCreateList(p->dropWin);
	WMHangData(p->dropList, p);
	WMSetListAllowEmptySelection(p->dropList, false);
	WMSetListUserDrawItemHeight(p->dropList, p->dropItemH);
	WMSetListUserDrawProc(p->dropList, drawCandidate);
	WMSetListAction(p->dropList, candidateClicked, p);
	WMSetListDoubleAction(p->dropList, candidateActivate, p);
	WMMoveWidget(p->dropList, 0, 0);

	WMMapSubwidgets(p->dropWin);
	WMRealizeWidget(p->dropWin);

	WMWidgetView(p->dropWin)->flags.worksWhenModal = 1;

	/* override-redirect so wmaker does not try to manage it,
	 * and save-under so the area below repaints cheaply */
	xcb_change_window_attributes(wdisp->conn, WMWidgetXID(p->dropWin),
				     XCB_CW_SAVE_UNDER | XCB_CW_OVERRIDE_REDIRECT,
				     (const uint32_t[]){ true, true });
}

static void placeDropList(WMInputPanelWithHistory *p, int rows)
{
	WArea ua;
	int x, y, w, h, head, max_rows, fh, fy, bw, ow;
	unsigned long *bpixel;

	max_rows = w_global.config->run_popup_rows;
	if (max_rows < 3)
		max_rows = 3;
	if (max_rows > 20)
		max_rows = 20;
	if (rows > max_rows)
		rows = max_rows;

	/* Replicate the dialog's outer border on the popup, matching the
	 * frame's focused/unfocused pixel. The popup interior is sized to
	 * core->width and placed at frame_x so border and content align. */
	bw = 0;
	bpixel = NULL;
	if (p->wwin->frame) {
		bw = HAS_BORDER(p->wwin) ? wFrameBorderWidth(p->wwin) : 0;
		fh = p->wwin->frame->core->height;
		bpixel = (p->wwin->frame->flags.state == WS_FOCUSED)
				? p->wwin->frame->focused_border_pixel
				: p->wwin->frame->border_pixel;
	} else {
		fh = WMWidgetHeight(p->panel->win);
	}
	fy = p->wwin->frame_y;

	w = p->wwin->frame ? p->wwin->frame->core->width
			   : WMWidgetWidth(p->panel->win);
	ow = w + 2 * bw;

	if (bw > 0) {
		xcb_configure_window(wdisp->conn, WMWidgetXID(p->dropWin), XCB_CONFIG_WINDOW_BORDER_WIDTH, (const uint32_t[]){ bw });
		if (bpixel)
			xcb_change_window_attributes(wdisp->conn, WMWidgetXID(p->dropWin), XCB_CW_BORDER_PIXEL, (const uint32_t[]){ *bpixel });
	} else {
		xcb_configure_window(wdisp->conn, WMWidgetXID(p->dropWin), XCB_CONFIG_WINDOW_BORDER_WIDTH, (const uint32_t[]){ 0 });
	}

	h = rows * p->dropItemH + 4;
	x = p->wwin->frame_x;
	y = fy + fh + 8;

	head = wGetHeadForWindow(p->wwin);
	ua = wGetUsableAreaForHead(p->scr, head, NULL, true);
	if (y + h > ua.y2 && fy - h - 8 >= ua.y1)
		y = fy - h - 8;
	else if (y + h > ua.y2)
		h = ua.y2 - y - 8 > p->dropItemH + 4 ? ua.y2 - y - 8 : h;
	if (x + ow > ua.x2)
		x = ua.x2 - ow;
	if (x < ua.x1)
		x = ua.x1;

	WMResizeWidget(p->dropWin, w, h);
	WMResizeWidget(p->dropList, w, h);
	WMMoveWidget(p->dropWin, x, y);
}

static bool prefixIsModal(const WMInputPanelWithHistory *p)
{
	const char *w;

	if (!p->prefix || p->word_off >= (int)strlen(p->prefix))
		return false;
	w = p->prefix + p->word_off;
	return p->word_off == 0 && (*w == '=' || *w == '#' || *w == '@');
}

static void refreshDropList(WMInputPanelWithHistory *p)
{
	RunVariant *v;
	WMListItem *it;
	int i, n;

	if (!w_global.config->run_popup_list || !p->wwin)
		return;
	n = p->variants ? WMGetArrayItemCount(p->variants) : 0;
	if (n < 2 && !(n == 1 && prefixIsModal(p))) {
		hideDropList(p);
		resetVariants(p);
		return;
	}
	if (!p->dropWin)
		createDropList(p);

	WMClearList(p->dropList);
	for (i = 0; i < n; i++) {
		v = WMGetFromArray(p->variants, i);
		it = WMInsertListItem(p->dropList, -1,
				      v->label ? v->label : v->text);
		it->clientData = v;
	}
	placeDropList(p, n);
	if (p->varpos > 0) {
		WMSelectListItem(p->dropList, p->varpos - 1);
		WMSetListPosition(p->dropList, p->varpos - 1);
	}
	if (!p->dropMapped) {
		WMMapWidget(p->dropWin);
		/* file drop widget, never in the stacking list, direct configure */
		xcb_configure_window(wdisp->conn, WMWidgetXID(p->dropWin), XCB_CONFIG_WINDOW_STACK_MODE, (const uint32_t[]){ XCB_STACK_MODE_ABOVE });
		p->dropMapped = true;
	}
	p->filtering = true;
}

static void hideDropList(WMInputPanelWithHistory *p)
{
	if (p->dropMapped) {
		WMUnmapWidget(p->dropWin);
		p->dropMapped = false;
	}
	p->filtering = false;
}

/* Keep the completion popup glued to the dialog on move/resize. */
static void dropListFollowDialog(WWindow *wwin, void *data)
{
	WMInputPanelWithHistory *p = data;
	int n;

	(void)wwin;
	if (!p->dropMapped || !p->dropWin || !p->variants)
		return;
	n = WMGetArrayItemCount(p->variants);
	if (n < 2)
		return;
	placeDropList(p, n);
}

/* Recolour the popup border when the dialog focus changes. */
static void dropListFocusObserver(void *self, WMNotification *notification)
{
	WMInputPanelWithHistory *p = self;
	int n;

	(void)notification;
	if (!p->dropMapped || !p->dropWin || !p->variants)
		return;
	n = WMGetArrayItemCount(p->variants);
	if (n < 2)
		return;
	placeDropList(p, n);
}

static void destroyDropList(WMInputPanelWithHistory *p)
{
	WMHashEnumerator e;
	void *val, *key;

	hideDropList(p);
	if (p->dropWin) {
		WMDestroyWidget(p->dropWin);
		p->dropWin = NULL;
		p->dropList = NULL;
	}
	if (p->dropIcons) {
		e = WMEnumerateHashTable(p->dropIcons);
		while (WMNextHashEnumeratorItemAndKey(&e, &val, &key))
			if (val != &dropIconMiss)
				WMReleasePixmap(val);
		WMFreeHashTable(p->dropIcons);
		p->dropIcons = NULL;
	}
	if (p->dropBold)
		WMReleaseFont(p->dropBold);
	if (p->dropNorm)
		WMReleaseFont(p->dropNorm);
	if (p->dropDim)
		WMReleaseColor(p->dropDim);
}

static void resetVariants(WMInputPanelWithHistory *p)
{
	if (p->prefix) {
		wfree(p->prefix);
		p->prefix = NULL;
	}
	if (p->suffix) {
		wfree(p->suffix);
		p->suffix = NULL;
	}
	if (p->variants) {
		WMFreeArray(p->variants);
		p->variants = NULL;
	}
	if (p->outer_variants) {
		WMFreeArray(p->outer_variants);
		p->outer_variants = NULL;
	}
	if (p->saved_msg) {
		if (p->panel->mLbl)
			WMSetLabelText(p->panel->mLbl, p->saved_msg);
		wfree(p->saved_msg);
		p->saved_msg = NULL;
	}
}

static void stepVariant(WMInputPanelWithHistory *p, int dir)
{
	int n;

	n = WMGetArrayItemCount(p->variants);
	p->varpos += dir;
	if (p->varpos > n)
		p->varpos = 0;
	if (p->varpos < 0)
		p->varpos = n;
	applyVariant(p);
	if (p->dropMapped && p->varpos > 0) {
		WMSelectListItem(p->dropList, p->varpos - 1);
		WMSetListPosition(p->dropList, p->varpos - 1);
	}
}

static bool enterOpenWith(WMInputPanelWithHistory *p)
{
	RunVariant *cur, *v;
	WMArray *handlers, *sub;
	WXDGApp *a;
	const char *base;
	char *label;
	int i, n;

	if (p->outer_variants || !p->variants || p->varpos <= 0)
		return false;
	cur = WMGetFromArray(p->variants, p->varpos - 1);
	if (!cur->doc_path || !cur->doc_mime)
		return false;
	handlers = wXDGAppsForMime(cur->doc_mime);
	n = WMGetArrayItemCount(handlers);
	if (n < 2)
		return false;

	base = strrchr(cur->doc_path, '/');
	base = base ? base + 1 : cur->doc_path;
	sub = WMCreateArrayWithDestructor(n, freeVariant);
	for (i = 0; i < n; i++) {
		a = WMGetFromArray(handlers, i);
		label = wstrconcat(base, " \xe2\x80\x94 ");
		label = wstrappend(label, a->name);
		v = makeVariant(wXDGAppOpenCommand(a, cur->doc_path),
				label, RUN_RANK_DOC_PREFIX, true);
		v->frec_key = cur->doc_path;
		if (a->icon)
			v->icon_hint = a->icon[0] == '/'
				     ? wstrdup(a->icon)
				     : wstrconcat("apps:", a->icon);
		WMAddToArray(sub, v);
	}
	p->outer_variants = p->variants;
	p->outer_varpos = p->varpos;
	p->variants = sub;
	p->varpos = 0;
	stepVariant(p, 1);
	refreshDropList(p);
	return true;
}

static bool leaveOpenWith(WMInputPanelWithHistory *p)
{
	if (!p->outer_variants)
		return false;
	WMFreeArray(p->variants);
	p->variants = p->outer_variants;
	p->varpos = p->outer_varpos;
	p->outer_variants = NULL;
	applyVariant(p);
	refreshDropList(p);
	return true;
}

static void handleHistoryKeyPress(xcb_generic_event_t * event, void *clientData)
{
	char *text;
	unsigned pos;
	WMInputPanelWithHistory *p = (WMInputPanelWithHistory *) clientData;
	RunVariant *av;
	WWindow *twin;
	xkb_keysym_t ksym;
	bool wasFiltering;

	ksym = wXkbKeycodeToKeysym(((xcb_key_press_event_t *)event)->detail, 0);

	switch (ksym) {
	case XKB_KEY_Shift_L:
	case XKB_KEY_Shift_R:
	case XKB_KEY_Control_L:
	case XKB_KEY_Control_R:
		/* bare modifier presses neither cycle nor reset */
		return;
	case XKB_KEY_Up:
		if (p->dropMapped) {
			stepVariant(p, -1);
			return;
		}
		if (p->histpos < WMGetArrayItemCount(p->history) - 1) {
			if (p->histpos == 0)
				wfree(WMReplaceInArray(p->history, 0, WMGetTextFieldText(p->panel->text)));
			p->histpos++;
			WMSetTextFieldText(p->panel->text, WMGetFromArray(p->history, p->histpos));
		}
		break;
	case XKB_KEY_Down:
		if (p->dropMapped) {
			stepVariant(p, 1);
			return;
		}
		if (p->histpos > 0) {
			p->histpos--;
			WMSetTextFieldText(p->panel->text, WMGetFromArray(p->history, p->histpos));
		}
		break;
	case XKB_KEY_Right:
		/* the text field has already moved the cursor by the
		 * time this handler runs, applyVariant() will rewrite
		 * the field and cursor so that does not matter, when
		 * the popup is mapped Right and Left only drill in and
		 * out of the open-with handler list and never reset */
		if (p->dropMapped) {
			enterOpenWith(p);
			return;
		}
		break;
	case XKB_KEY_Left:
		if (p->dropMapped) {
			leaveOpenWith(p);
			return;
		}
		break;
	case XKB_KEY_Escape:
		if (p->dropMapped || p->filtering) {
			/* the text field has already processed Escape
			 * and broken the modal loop by the time this
			 * handler runs, the wAdvancedInputDialog caller
			 * re-enters it when rerun is set so the first
			 * Escape only dismisses the popup and restores
			 * the pre-Tab text, the second one closes */
			if (p->variants && p->prefix) {
				p->varpos = 0;
				applyVariant(p);
			}
			hideDropList(p);
			resetVariants(p);
			p->rerun = true;
			return;
		}
		break;
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
		if (p->variants && p->varpos > 0) {
			av = WMGetFromArray(p->variants, p->varpos - 1);
			if (av->frec_key) {
				wfree(p->accepted_key);
				p->accepted_key = wstrdup(av->frec_key);
			}
			if (av->action == RUN_ACTION_FOCUS_WINDOW) {
				twin = wWindowFor(av->focus_xid);
				if (twin)
					wWindowSingleFocus(twin);
				hideDropList(p);
				WMPerformButtonClick(p->panel->altBtn);
				return;
			}
			if (av->action == RUN_ACTION_SWITCH_WORKSPACE) {
				wWorkspaceChange(p->scr, av->target_workspace);
				hideDropList(p);
				WMPerformButtonClick(p->panel->altBtn);
				return;
			}
			if (av->action == RUN_ACTION_DISMISS) {
				noteCalcLine(p);
				hideDropList(p);
				WMPerformButtonClick(p->panel->altBtn);
				return;
			}
		}
		hideDropList(p);
		break;
	case XKB_KEY_ISO_Left_Tab:
		if (p->variants) {
			stepVariant(p, -1);
			return;
		}
		break;
	case XKB_KEY_Tab:
		if (!p->variants) {
			text = WMGetTextFieldText(p->panel->text);
			pos = WMGetTextFieldCursorPosition(p->panel->text);
			p->prefix = wstrndup(text, pos);
			p->suffix = wstrdup(text + pos);
			wfree(text);
			p->variants = GenerateVariants(p->scr, p->prefix, &p->word_off);
			p->varpos = 0;
			stepVariant(p, 1);
			refreshDropList(p);
		} else if (((xcb_key_press_event_t *)event)->state & XCB_MOD_MASK_SHIFT) {
			stepVariant(p, -1);
		} else {
			stepVariant(p, 1);
		}
		return;
	}

	wasFiltering = p->filtering;
	hideDropList(p);
	resetVariants(p);

	/* live filtering once the popup has been opened, regenerate
	 * the variant set from the edited text and keep the popup
	 * tracking it, this is the narrow-as-you-type behaviour
	 * and is cheap because the source caches do no I/O */
	if (wasFiltering && ksym != XKB_KEY_Escape && ksym != XKB_KEY_Return &&
	    ksym != XKB_KEY_KP_Enter && ksym != XKB_KEY_Up && ksym != XKB_KEY_Down) {
		text = WMGetTextFieldText(p->panel->text);
		pos = WMGetTextFieldCursorPosition(p->panel->text);
		p->prefix = wstrndup(text, pos);
		p->suffix = wstrdup(text + pos);
		wfree(text);
		p->variants = GenerateVariants(p->scr, p->prefix, &p->word_off);
		p->varpos = 0;
		refreshDropList(p);
	}
}

static int widenInputDialog(WScreen *scr, WMInputPanel *panel)
{
	int wmScaleWidth, wmScaleHeight;
	int head, nat_w, target_w, max_w, margin, gap, right_edge, w, x;
	WArea ua;

	WMGetScaleBaseFromSystemFont(scr->wmscreen, &wmScaleWidth, &wmScaleHeight);

	nat_w = WMWidgetWidth(panel->win);
	max_w = WMScaleX(400);
	if (max_w < nat_w)
		max_w = nat_w;

	head = wGetHeadForPointerLocation(scr);
	ua = wGetUsableAreaForHead(scr, head, NULL, true);
	target_w = (ua.x2 - ua.x1) * 40 / 100;
	if (target_w < nat_w)
		target_w = nat_w;
	if (target_w > max_w)
		target_w = max_w;
	if (target_w <= nat_w)
		return nat_w;

	margin = WMScaleX(20);
	gap = WMScaleX(10);

	WMResizeWidget(panel->win, target_w, WMWidgetHeight(panel->win));
	if (panel->tLbl)
		WMResizeWidget(panel->tLbl, target_w - 2 * margin, WMWidgetHeight(panel->tLbl));
	if (panel->mLbl)
		WMResizeWidget(panel->mLbl, target_w - 2 * margin, WMWidgetHeight(panel->mLbl));
	if (panel->text)
		WMResizeWidget(panel->text, target_w - 2 * margin, WMWidgetHeight(panel->text));

	right_edge = target_w - margin;
	if (panel->defBtn) {
		w = WMWidgetWidth(panel->defBtn);
		x = right_edge - w;
		WMMoveWidget(panel->defBtn, x, WMWidgetView(panel->defBtn)->pos.y);
		right_edge = x - gap;
	}
	if (panel->altBtn) {
		w = WMWidgetWidth(panel->altBtn);
		x = right_edge - w;
		WMMoveWidget(panel->altBtn, x, WMWidgetView(panel->altBtn)->pos.y);
	}
	return target_w;
}

int wAdvancedInputDialog(WScreen *scr, const char *title, const char *message, const char *name, char **text)
{
	WWindow *wwin;
	xcb_window_t parent;
	char *result;
	WMPoint center;
	WMInputPanelWithHistory *p;
	char *filename;
	int pwidth, pheight;

	filename = HistoryFileName(name);
	p = wmalloc(sizeof(WMInputPanelWithHistory));
	p->panel = WMCreateScaledInputPanel(scr->wmscreen, NULL, title, message, *text, _("OK"), _("Cancel"));
	WMSetTextFieldPlaceholder(p->panel->text, _("Search or enter command..."));
	p->history = LoadHistory(filename, w_global.config->history_lines);
	mergeCalcHistory(p->history);
	p->scr = scr;
	WMCreateEventHandler(WMWidgetView(p->panel->text), XCB_EVENT_MASK_KEY_PRESS, handleHistoryKeyPress, p);
	widenInputDialog(scr, p->panel);
	pwidth = WSCALE(scr, WMWidgetWidth(p->panel->win));
	pheight = WSCALE(scr, WMWidgetHeight(p->panel->win));

	parent = xcb_generate_id(wdisp->conn);
	xcb_create_window(wdisp->conn, XCB_COPY_FROM_PARENT, parent, scr->root_win, 0, 0, pwidth, pheight, 0,
			  XCB_WINDOW_CLASS_COPY_FROM_PARENT, XCB_COPY_FROM_PARENT,
			  XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL, (const uint32_t[]){ 0, 0});
	xcb_reparent_window(wdisp->conn, WMWidgetXID(p->panel->win), parent, 0, 0);

	center = wGetCenter(scr, pwidth, pheight);
	wwin = wManageInternalWindow(scr, parent, XCB_NONE, NULL, center.x, center.y, pwidth, pheight);

	wwin->client_leader = WMWidgetXID(p->panel->win);
	p->wwin = wwin;
	wwin->on_geometry_change = dropListFollowDialog;
	wwin->geometry_data = p;
	WMAddNotificationObserver(dropListFocusObserver, p, WMNChangedFocus, wwin);

	WMMapWidget(p->panel->win);

	wWindowMap(wwin);

	do {
		p->rerun = false;
		WMRunModalLoop(WMWidgetScreen(p->panel->win),
			       WMWidgetView(p->panel->win));
		/* the panel's own Escape handler clicked the Cancel
		 * button before our key handler asked for a rerun,
		 * undo the result and put focus back in the field */
		if (p->rerun) {
			p->panel->result = -1;
			WMSetFocusToWidget(p->panel->text);
		}
	} while (p->rerun);

	/* detach hooks before teardown */
	wwin->on_geometry_change = NULL;
	wwin->geometry_data = NULL;
	WMRemoveNotificationObserverWithName(p, WMNChangedFocus, wwin);

	if (p->panel->result == WAPRDefault) {
		result = WMGetTextFieldText(p->panel->text);
		if (result &&
		    ((result[0] == '=' && w_global.config->run_calc) ||
		     (result[0] == '#' && w_global.config->run_window_switcher) ||
		     (result[0] == '@' &&
		      (strchr(result, '#') ? w_global.config->run_window_switcher
					   : w_global.config->run_workspace_jumper)))) {
			if (result[0] == '=')
				noteCalcLine(p);
			wfree(result);
			result = NULL;
		}
		if (result) {
			wfree(WMReplaceInArray(p->history, 0, wstrdup(result)));
			SaveHistoryFiltered(p->history, filename, '\0');
			if (p->accepted_key)
				frecencyRecord(p->accepted_key);
		}
	} else
		result = NULL;
	wfree(p->accepted_key);

	wUnmanageWindow(wwin, false, false);

	destroyDropList(p);
	resetVariants(p);
	WMDestroyInputPanel(p->panel);
	WMFreeArray(p->history);
	wfree(p);
	wfree(filename);

	xcb_destroy_window(wdisp->conn, parent);

	if (result == NULL)
		return false;

	if (*text)
		wfree(*text);
	*text = result;

	return true;
}
