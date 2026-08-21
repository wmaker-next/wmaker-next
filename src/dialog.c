/* dialog.c - dialog windows for internal use
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 1998-2003 Dan Pascu
 *  Copyright (c) 2014-2023 Window Maker Team
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

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <errno.h>
#include <time.h>
#include <sys/utsname.h>

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#include <signal.h>

#include <cairo.h>
#include <WINGs/WINGsP.h>
#ifdef __FreeBSD__
#include <sys/signal.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX DEFAULT_PATH_MAX
#endif

#include "WindowMaker.h"
#include "GNUstep.h"
#include "screen.h"
#include "window.h"
#include "desktopfile.h"
#include "dialog.h"
#include "misc.h"
#include "wxkb.h"
#include "stacking.h"
#include "framewin.h"
#include "window.h"
#include "actions.h"
#include "ipc.h"
#include "xinerama.h"
#include "calc.h"
#include "switchmenu.h"
#include "tabgroup.h"
#include "workspace.h"
#include "application.h"
#include "appicon.h"
#include "icon.h"
#include "defaults.h"


static WMPoint getCenter(WScreen * scr, int width, int height)
{
	return wGetPointToCenterRectInHead(scr, wGetHeadForPointerLocation(scr), width, height);
}

int wMessageDialog(WScreen *scr, const char *title, const char *message, const char *defBtn, const char *altBtn, const char *othBtn)
{
	WMAlertPanel *panel;
	Window parent;
	WWindow *wwin;
	int result;
	WMPoint center;
	int pwidth, pheight;

	wIPCEmitDialog(scr, "message", title, message);

	panel = WMCreateScaledAlertPanel(scr->wmscreen, NULL, title, message, defBtn, altBtn, othBtn);
	pwidth = WMWidgetWidth(panel->win);
	pheight = WMWidgetHeight(panel->win);

	parent = XCreateSimpleWindow(dpy, scr->root_win, 0, 0, pwidth, pheight, 0, 0, 0);

	XReparentWindow(dpy, WMWidgetXID(panel->win), parent, 0, 0);

	center = getCenter(scr, pwidth, pheight);
	wwin = wManageInternalWindow(scr, parent, None, NULL, center.x, center.y, pwidth, pheight);
	wwin->client_leader = WMWidgetXID(panel->win);

	WMMapWidget(panel->win);

	wWindowMap(wwin);

	WMRunModalLoop(WMWidgetScreen(panel->win), WMWidgetView(panel->win));

	result = panel->result;

	WMUnmapWidget(panel->win);

	wUnmanageWindow(wwin, False, False);

	WMDestroyAlertPanel(panel);

	XDestroyWindow(dpy, parent);

	return result;
}

static void toggleSaveSession(WMWidget *w, void *data)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) data;

	w_global.config->save_session_on_exit = WMGetButtonSelected((WMButton *) w);
}

int wExitDialog(WScreen *scr, const char *title, const char *message, const char *defBtn, const char *altBtn, const char *othBtn)
{
	WMAlertPanel *panel;
	WMButton *saveSessionBtn;
	Window parent;
	WWindow *wwin;
	WMPoint center;
	int result;
	int pwidth, pheight;

	panel = WMCreateScaledAlertPanel(scr->wmscreen, NULL, title, message, defBtn, altBtn, othBtn);
	pwidth = WMWidgetWidth(panel->win);
	pheight = WMWidgetHeight(panel->win);

	/* add save session button */
	saveSessionBtn = WMCreateSwitchButton(panel->hbox);
	WMSetButtonAction(saveSessionBtn, toggleSaveSession, NULL);
	WMAddBoxSubview(panel->hbox, WMWidgetView(saveSessionBtn), False, True, pwidth / 2, 0, 0);
	WMSetButtonText(saveSessionBtn, _("Save workspace state"));
	WMSetButtonSelected(saveSessionBtn, w_global.config->save_session_on_exit);
	WMRealizeWidget(saveSessionBtn);
	WMMapWidget(saveSessionBtn);

	parent = XCreateSimpleWindow(dpy, scr->root_win, 0, 0, pwidth, pheight, 0, 0, 0);

	XReparentWindow(dpy, WMWidgetXID(panel->win), parent, 0, 0);

	center = getCenter(scr, pwidth, pheight);
	wwin = wManageInternalWindow(scr, parent, None, NULL, center.x, center.y, pwidth, pheight);

	wwin->client_leader = WMWidgetXID(panel->win);

	WMMapWidget(panel->win);

	wWindowMap(wwin);

	WMRunModalLoop(WMWidgetScreen(panel->win), WMWidgetView(panel->win));

	result = panel->result;

	WMUnmapWidget(panel->win);

	wUnmanageWindow(wwin, False, False);

	WMDestroyAlertPanel(panel);

	XDestroyWindow(dpy, parent);

	return result;
}

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
	Bool replace;	/* True: text replaces the current word */
	RunVariantAction action;
	Window focus_xid;	/* client_win of a window row, re-resolved at use */
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
	Bool dropMapped;
	Bool filtering;		/* regenerate variants on every edit */
	Bool rerun;		/* re-enter the modal loop after Escape */
	char *accepted_key;	/* frec_key of the variant Return picked */
	WMArray *outer_variants;/* saved during an open-with sub-cycle */
	int outer_varpos;
} WMInputPanelWithHistory;

static RunVariant *makeVariant(char *text, char *label, int rank, Bool replace)
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

static void SaveHistory(WMArray * history, const char *filename)
{
	int i;
	WMPropList *plhistory;

	plhistory = WMCreatePLArray(NULL);

	for (i = 0; i < WMGetArrayItemCount(history); ++i)
		WMAddToPLArray(plhistory, WMCreatePLString(WMGetFromArray(history, i)));

	WMWritePropListToFile(plhistory, filename);
	WMReleasePropList(plhistory);
}

static void SaveHistoryFiltered(WMArray *history, const char *filename, char prefix_char)
{
	int i;
	WMPropList *plhistory;
	const char *s;

	plhistory = WMCreatePLArray(NULL);
	for (i = 0; i < WMGetArrayItemCount(history); ++i) {
		s = WMGetFromArray(history, i);
		if (prefix_char != 0) {
			if (s[0] == prefix_char)
				WMAddToPLArray(plhistory, WMCreatePLString(s));
		} else {
			if (s[0] != '=' || !w_global.config->run_calc)
				WMAddToPLArray(plhistory, WMCreatePLString(s));
		}
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
									 RUN_RANK_PATH, False));
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
static Bool matchName(const char *needle, const char *hay_fold,
		      const char *hay_orig, int *band, int *score)
{
	const char *hit;

	switch (w_global.config->run_matching) {
	case RUN_MATCH_PREFIX:
		if (strncmp(hay_fold, needle, strlen(needle)) != 0)
			return False;
		*band = 0;
		*score = 0;
		return True;
	case RUN_MATCH_FUZZY:
		*score = wFuzzyScore(needle, hay_fold, hay_orig);
		if (*score < 0)
			return False;
		*band = strncmp(hay_fold, needle, strlen(needle)) == 0 ? 0 : 1;
		return True;
	case RUN_MATCH_SUBSTRING:
	default:
		hit = strstr(hay_fold, needle);
		if (!hit)
			return False;
		*band = (hit == hay_fold) ? 0 : 1;
		*score = (hit == hay_fold) ? 1 : 0;
		return True;
	}
}

static Bool matchExecName(const char *needle, const char *exec,
			  int *band, int *score)
{
	char name[256], fold[256];
	const char *start, *base;
	size_t i, n;

	if (!exec)
		return False;
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
		return False;
	memcpy(name, start, n);
	name[n] = 0;
	base = strrchr(name, '/');
	if (base)
		base++;
	else
		base = name;
	n = strlen(base);
	if (n == 0)
		return False;
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
				RUN_RANK_APP_PREFIX + band, True);
		v->frec_key = a->id;
		v->score = score + frecencyBonus(a->id);
		if (a->icon)
			v->icon_hint = a->icon[0] == '/'
				     ? wstrdup(a->icon)
				     : wstrconcat("apps:", a->icon);
		WMAddToArray(result, v);
	}
}

static void matchPathDir(const char *dir, const char *word,
			 const char *fold, WMHashTable *seen,
			 WMArray *result)
{
	DIR *dp;
	struct dirent *de;
	struct stat sb;
	RunVariant *v;
	char namefold[256], *full;
	size_t k, dlen;
	int band, score;

	dp = opendir(dir);
	if (!dp)
		return;
	while ((de = readdir(dp)) != NULL) {
		dlen = strlen(de->d_name);
		if (dlen == 0 || dlen >= sizeof(namefold))
			continue;
		for (k = 0; k <= dlen; k++)
			namefold[k] = tolower((unsigned char)de->d_name[k]);
		if (!matchName(fold, namefold, de->d_name, &band, &score))
			continue;
		/* the suffix variant only works when the case the
		 * user typed matches the binary on disk, otherwise
		 * the rebuilt field would not run, so fall through
		 * to a replacing variant carrying the real name */
		if (band == 0 && strncmp(de->d_name, word, strlen(word)) != 0)
			band = 1;
		full = wstrconcat(dir, "/");
		full = wstrappend(full, de->d_name);
		if (stat(full, &sb) != 0 ||
		    !(sb.st_mode & (S_IXOTH | S_IXGRP | S_IXUSR)) ||
		    (sb.st_mode & S_IFDIR)) {
			wfree(full);
			continue;
		}
		wfree(full);
		/* dedup across $PATH directories on the binary name
		 * itself, not the variant text, so an XDG app whose
		 * Exec= happens to equal a suffix cannot mask a hit */
		if (WMHashGet(seen, de->d_name))
			continue;
		WMHashInsert(seen, de->d_name, seen);
		/* a prefix hit stays a non-replacing suffix so the
		 * pre-series Tab feel is preserved in every mode. An
		 * exact name match completes to itself, which is the
		 * no-op Tab already performs at varpos 0, so skip it
		 * rather than show an empty row in the popup */
		if (band == 0) {
			if (de->d_name[strlen(word)] == '\0')
				continue;
			v = makeVariant(wstrdup(de->d_name + strlen(word)),
					NULL, RUN_RANK_PATH, False);
		} else {
			v = makeVariant(wstrdup(de->d_name), NULL,
					RUN_RANK_PATH + 1, True);
		}
		v->score = score;
		v->icon_hint = wstrdup("mimetypes:application-x-executable");
		WMAddToArray(result, v);
	}
	closedir(dp);
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
		v = makeVariant(cmd, label, RUN_RANK_DOC_PREFIX + band, True);
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
	v = makeVariant(wstrdup(""), label, RUN_RANK_CALC, False);
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

static Bool ws_in_scope(WWindow *wwin, const int *scope, int count)
{
	int i;

	if (count == 0)
		return True;
	if (count < 0)	/* requested scope matched nothing */
		return False;
	for (i = 0; i < count; i++)
		if (WWIN_ON_WORKSPACE(wwin, scope[i]))
			return True;
	return False;
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
	v = makeVariant(wstrdup(""), label, rank, False);
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

static void matchWindows(const char *complete, WMArray *result)
{
	WScreen *scr;
	WWindow *wwin;
	const char *needle, *hash;
	char wsn[256], fold[256];
	int scope[MAX_WORKSPACES];
	int scope_count = 0, band, score;
	size_t i, nlen, wlen;

	scr = wScreenWithNumber(0);
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

static RunVariant *makeWorkspaceVariant(int ws_index, int rank, int score)
{
	RunVariant *v;
	WScreen *scr = wScreenWithNumber(0);
	const char *name;
	char *text;

	if (!scr || ws_index < 0 || ws_index >= scr->workspace_count)
		return NULL;
	name = scr->workspaces[ws_index]->name;
	text = wstrconcat("@", name ? name : "");
	v = makeVariant(text, wstrdup(name ? name : ""), rank, True);
	v->score = score;
	v->action = RUN_ACTION_SWITCH_WORKSPACE;
	v->target_workspace = ws_index;
	return v;
}

static void matchWorkspaces(const char *needle, WMArray *result)
{
	WScreen *scr;
	RunVariant *v;
	int n, w, i;
	char fold[256], ff[256];
	int band, score;
	size_t nlen, fl;

	scr = wScreenWithNumber(0);
	if (!scr || scr->workspace_count <= 0)
		return;
	if (needle && *needle && sscanf(needle, "%i", &n) == 1) {
		w = n - 1;
		if (w >= 0 && w < scr->workspace_count) {
			v = makeWorkspaceVariant(w, RUN_RANK_WORKSPACE, 0);
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
		v = makeWorkspaceVariant(i, RUN_RANK_WORKSPACE + band, score);
		if (v)
			WMAddToArray(result, v);
	}
}

static WMArray *GenerateVariants(const char *complete, int *word_off)
{
	Bool firstWord = True;
	WMArray *variants = NULL;
	const char *start, *pos = NULL, *path = NULL;
	char *tmp = NULL, *dir = NULL, *prefix = NULL;
	char fold[256];
	size_t i, flen;
	WMHashTable *seen;

	variants = WMCreateArrayWithDestructor(0, freeVariant);

	start = complete;
	while (*complete == ' ')
		++complete;

	if (*complete != '=' && *complete != '#' && *complete != '@' && *complete != '?') {
		pos = strrchr(complete, ' ');
		if (pos != NULL) {
			complete = pos + 1;
			firstWord = False;
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
						   RUN_RANK_PATH, False));
	} else if (firstWord && *complete == '?') {
		if (w_global.config->run_doc_search)
			matchDocuments(complete + 1, variants);
	} else if (firstWord && *complete == '=') {
		if (w_global.config->run_calc)
			matchCalc(complete + 1, variants);
	} else if (firstWord && *complete == '#') {
		if (w_global.config->run_window_switcher)
			matchWindows(complete, variants);
	} else if (firstWord) {
		flen = strlen(complete);
		if (flen >= sizeof(fold))
			flen = sizeof(fold) - 1;
		for (i = 0; i < flen; i++)
			fold[i] = tolower((unsigned char)complete[i]);
		fold[flen] = 0;
		if (w_global.config->run_xdg_completion)
			matchXDGApps(complete, variants);
		seen = WMCreateHashTable(WMStringHashCallbacks);
		path = flen ? getenv("PATH") : NULL;
		while (path) {
			pos = strchr(path, ':');
			if (pos) {
				tmp = wstrndup(path, pos - path);
				path = pos + 1;
			} else if (*path != '\0') {
				tmp = wstrdup(path);
				path = NULL;
			} else
				break;
			matchPathDir(tmp, complete, fold, seen, variants);
			wfree(tmp);
		}
		WMFreeHashTable(seen);
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

static WMPixmap *loadRowIcon(WMInputPanelWithHistory *p, const char *hint,
			     WMColor *back)
{
	WMPixmap *pix;
	RColor color;
	const char *colon;
	char ctx[32], *path;
	RImage *image;

	if (!hint || !hint[0])
		return NULL;
	pix = WMHashGet(p->dropIcons, hint);
	if (pix)
		return pix == (WMPixmap *)&dropIconMiss ? NULL : pix;

	color.red = WMRedComponentOfColor(back) >> 8;
	color.green = WMGreenComponentOfColor(back) >> 8;
	color.blue = WMBlueComponentOfColor(back) >> 8;
	color.alpha = WMGetColorAlpha(back) >> 8;

	image = NULL;

	if (strncmp(hint, "win:", 4) == 0) {
		Window xid = strtoul(hint + 4, NULL, 0);
		WWindow *w = wWindowFor(xid);

		if (w) {
			WApplication *wapp;

			wapp = wApplicationOf(w->main_window);
			if (wapp && wapp->app_icon && wapp->app_icon->icon &&
			    wapp->app_icon->icon->file_image)
				image = RRetainImage(wapp->app_icon->icon->file_image);
			if (!image && !WFLAGP(w, always_user_icon) && w->net_icon_image)
				image = RRetainImage(w->net_icon_image);
			if (!image)
				image = get_icon_image(p->scr, w->wm_instance,
						       w->wm_class, p->dropIconSize);
			if (image)
				image = wIconValidateIconSize(image, p->dropIconSize);
		}
	} else if (hint[0] == '/') {
		image = RLoadImage(p->scr->rcontext, hint, 0);
	} else {
		colon = strchr(hint, ':');
		if (!colon || (size_t)(colon - hint) >= sizeof(ctx))
			return NULL;
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
			RCombineImageWithColor(image, &color);
			pix = WMCreatePixmapFromRImage(p->scr->wmscreen, image, 0);
			RReleaseImage(image);
		}
	}

	if (!pix) {
		WMHashInsert(p->dropIcons, hint, &dropIconMiss);
		return NULL;
	}
	WMHashInsert(p->dropIcons, hint, pix);
	return pix;
}

static void drawCandidate(WMList *lPtr, int index, Drawable d,
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
	int fh, tx, iy;

	(void)index;

	v = WMGetListItem(lPtr, index)->clientData;
	back = (state & WLDSSelected) ? p->scr->white : p->scr->gray;

	cr = WCairoBeginForDrawable(wscr, d);
	WCairoSetSourceColor(cr, back);
	cairo_rectangle(cr, rect->pos.x, rect->pos.y,
			rect->size.width, rect->size.height);
	cairo_fill(cr);

	tx = rect->pos.x + p->dropItemH + 4;
	icon = loadRowIcon(p, v->icon_hint, p->scr->gray);
	if (icon) {
		isz = WMGetPixmapSize(icon);
		iy = rect->pos.y + (p->dropItemH - isz.height) / 2;
		WCairoPaintPixmap(cr, icon,
				  rect->pos.x + 2 +
				  (p->dropIconSize - isz.width) / 2, iy);
	}

	line1 = v->label ? v->label : text;
	line2 = v->label ? v->text : NULL;
	fh = WMFontHeight(p->dropBold);
	WMDrawStringCairo(cr, p->scr->black, p->dropBold,
			  tx, rect->pos.y + 2, line1, strlen(line1));
	if (line2)
		WMDrawStringCairo(cr, p->dropDim, p->dropNorm,
				  tx, rect->pos.y + 2 + fh,
				  line2, strlen(line2));
	WCairoEnd(cr);
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
	XSetWindowAttributes attr;
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
	WMSetListAllowEmptySelection(p->dropList, False);
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
	attr.override_redirect = True;
	attr.save_under = True;
	XChangeWindowAttributes(dpy, WMWidgetXID(p->dropWin),
				CWOverrideRedirect | CWSaveUnder, &attr);
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
		XSetWindowBorderWidth(dpy, WMWidgetXID(p->dropWin), bw);
		if (bpixel)
			XSetWindowBorder(dpy, WMWidgetXID(p->dropWin), *bpixel);
	} else {
		XSetWindowBorderWidth(dpy, WMWidgetXID(p->dropWin), 0);
	}

	h = rows * p->dropItemH + 4;
	x = p->wwin->frame_x;
	y = fy + fh + 8;

	head = wGetHeadForWindow(p->wwin);
	ua = wGetUsableAreaForHead(p->scr, head, NULL, True);
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

static Bool prefixIsModal(const WMInputPanelWithHistory *p)
{
	const char *w;

	if (!p->prefix || p->word_off >= (int)strlen(p->prefix))
		return False;
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
		XRaiseWindow(dpy, WMWidgetXID(p->dropWin));
		p->dropMapped = True;
	}
	p->filtering = True;
}

static void hideDropList(WMInputPanelWithHistory *p)
{
	if (p->dropMapped) {
		WMUnmapWidget(p->dropWin);
		p->dropMapped = False;
	}
	p->filtering = False;
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

static Bool enterOpenWith(WMInputPanelWithHistory *p)
{
	RunVariant *cur, *v;
	WMArray *handlers, *sub;
	WXDGApp *a;
	const char *base;
	char *label;
	int i, n;

	if (p->outer_variants || !p->variants || p->varpos <= 0)
		return False;
	cur = WMGetFromArray(p->variants, p->varpos - 1);
	if (!cur->doc_path || !cur->doc_mime)
		return False;
	handlers = wXDGAppsForMime(cur->doc_mime);
	n = WMGetArrayItemCount(handlers);
	if (n < 2)
		return False;

	base = strrchr(cur->doc_path, '/');
	base = base ? base + 1 : cur->doc_path;
	sub = WMCreateArrayWithDestructor(n, freeVariant);
	for (i = 0; i < n; i++) {
		a = WMGetFromArray(handlers, i);
		label = wstrconcat(base, " \xe2\x80\x94 ");
		label = wstrappend(label, a->name);
		v = makeVariant(wXDGAppOpenCommand(a, cur->doc_path),
				label, RUN_RANK_DOC_PREFIX, True);
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
	return True;
}

static Bool leaveOpenWith(WMInputPanelWithHistory *p)
{
	if (!p->outer_variants)
		return False;
	WMFreeArray(p->variants);
	p->variants = p->outer_variants;
	p->varpos = p->outer_varpos;
	p->outer_variants = NULL;
	applyVariant(p);
	refreshDropList(p);
	return True;
}

static void handleHistoryKeyPress(XEvent * event, void *clientData)
{
	char *text;
	unsigned pos;
	WMInputPanelWithHistory *p = (WMInputPanelWithHistory *) clientData;
	RunVariant *av;
	WWindow *twin;
	KeySym ksym;
	Bool wasFiltering;

	ksym = wXkbKeycodeToKeysym(event->xkey.keycode, 0);

	switch (ksym) {
	case XK_Shift_L:
	case XK_Shift_R:
	case XK_Control_L:
	case XK_Control_R:
		/* bare modifier presses neither cycle nor reset */
		return;
	case XK_Up:
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
	case XK_Down:
		if (p->dropMapped) {
			stepVariant(p, 1);
			return;
		}
		if (p->histpos > 0) {
			p->histpos--;
			WMSetTextFieldText(p->panel->text, WMGetFromArray(p->history, p->histpos));
		}
		break;
	case XK_Right:
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
	case XK_Left:
		if (p->dropMapped) {
			leaveOpenWith(p);
			return;
		}
		break;
	case XK_Escape:
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
			p->rerun = True;
			return;
		}
		break;
	case XK_Return:
	case XK_KP_Enter:
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
	case XK_ISO_Left_Tab:
		if (p->variants) {
			stepVariant(p, -1);
			return;
		}
		break;
	case XK_Tab:
		if (!p->variants) {
			text = WMGetTextFieldText(p->panel->text);
			pos = WMGetTextFieldCursorPosition(p->panel->text);
			p->prefix = wstrndup(text, pos);
			p->suffix = wstrdup(text + pos);
			wfree(text);
			p->variants = GenerateVariants(p->prefix, &p->word_off);
			p->varpos = 0;
			stepVariant(p, 1);
			refreshDropList(p);
		} else if (event->xkey.state & ShiftMask) {
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
	 * tracking it, this is the rofi narrow-as-you-type behaviour
	 * and is cheap because the source caches do no I/O */
	if (wasFiltering && ksym != XK_Escape && ksym != XK_Return &&
	    ksym != XK_KP_Enter && ksym != XK_Up && ksym != XK_Down) {
		text = WMGetTextFieldText(p->panel->text);
		pos = WMGetTextFieldCursorPosition(p->panel->text);
		p->prefix = wstrndup(text, pos);
		p->suffix = wstrdup(text + pos);
		wfree(text);
		p->variants = GenerateVariants(p->prefix, &p->word_off);
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
	ua = wGetUsableAreaForHead(scr, head, NULL, True);
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
	Window parent;
	char *result;
	WMPoint center;
	WMInputPanelWithHistory *p;
	char *filename;
	int pwidth, pheight;

	filename = HistoryFileName(name);
	p = wmalloc(sizeof(WMInputPanelWithHistory));
	p->panel = WMCreateScaledInputPanel(scr->wmscreen, NULL, title, message, *text, _("OK"), _("Cancel"));
	p->history = LoadHistory(filename, w_global.config->history_lines);
	mergeCalcHistory(p->history);
	p->scr = scr;
	WMCreateEventHandler(WMWidgetView(p->panel->text), KeyPressMask, handleHistoryKeyPress, p);
	widenInputDialog(scr, p->panel);
	pwidth = WMWidgetWidth(p->panel->win);
	pheight = WMWidgetHeight(p->panel->win);

	parent = XCreateSimpleWindow(dpy, scr->root_win, 0, 0, pwidth, pheight, 0, 0, 0);
	XSelectInput(dpy, parent, KeyPressMask | KeyReleaseMask);

	XReparentWindow(dpy, WMWidgetXID(p->panel->win), parent, 0, 0);

	center = getCenter(scr, pwidth, pheight);
	wwin = wManageInternalWindow(scr, parent, None, NULL, center.x, center.y, pwidth, pheight);

	wwin->client_leader = WMWidgetXID(p->panel->win);
	p->wwin = wwin;
	wwin->on_geometry_change = dropListFollowDialog;
	wwin->geometry_data = p;
	WMAddNotificationObserver(dropListFocusObserver, p, WMNChangedFocus, wwin);

	WMMapWidget(p->panel->win);

	wWindowMap(wwin);

	do {
		p->rerun = False;
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

	wUnmanageWindow(wwin, False, False);

	destroyDropList(p);
	resetVariants(p);
	WMDestroyInputPanel(p->panel);
	WMFreeArray(p->history);
	wfree(p);
	wfree(filename);

	XDestroyWindow(dpy, parent);

	if (result == NULL)
		return False;
	else {
		if (*text)
			wfree(*text);
		*text = result;

		return True;
	}
}

int wInputDialog(WScreen *scr, const char *title, const char *message, char **text)
{
	WWindow *wwin;
	Window parent;
	WMInputPanel *panel;
	char *result;
	WMPoint center;

	wIPCEmitDialog(scr, "input", title, message);
	int pwidth, pheight;

	panel = WMCreateScaledInputPanel(scr->wmscreen, NULL, title, message, *text, _("OK"), _("Cancel"));
	pwidth = WMWidgetWidth(panel->win);
	pheight = WMWidgetHeight(panel->win);

	parent = XCreateSimpleWindow(dpy, scr->root_win, 0, 0, pwidth, pheight, 0, 0, 0);
	XSelectInput(dpy, parent, KeyPressMask | KeyReleaseMask);

	XReparentWindow(dpy, WMWidgetXID(panel->win), parent, 0, 0);

	center = getCenter(scr, pwidth, pheight);
	wwin = wManageInternalWindow(scr, parent, None, NULL, center.x, center.y, pwidth, pheight);

	wwin->client_leader = WMWidgetXID(panel->win);

	WMMapWidget(panel->win);

	wWindowMap(wwin);

	WMRunModalLoop(WMWidgetScreen(panel->win), WMWidgetView(panel->win));

	if (panel->result == WAPRDefault)
		result = WMGetTextFieldText(panel->text);
	else
		result = NULL;

	wUnmanageWindow(wwin, False, False);

	WMDestroyInputPanel(panel);

	XDestroyWindow(dpy, parent);

	if (result == NULL)
		return False;
	else {
		if (*text)
			wfree(*text);
		*text = result;

		return True;
	}
}

/*
 *****************************************************************
 * Icon Selection Panel
 *****************************************************************
 */

typedef struct IconPanel {

	WScreen *scr;

	WMWindow *win;

	WMLabel *dirLabel;
	WMLabel *iconLabel;

	WMList *dirList;
	WMList *iconList;
	WMFont *normalfont;

	WMButton *previewButton;

	WMLabel *iconView;

	WMLabel *fileLabel;
	WMTextField *fileField;

	WMButton *okButton;
	WMButton *cancelButton;
#if 0
	WMButton *chooseButton;
#endif
	short done;
	short result;
	short preview;
} IconPanel;

static void listPixmaps(WScreen *scr, WMList *lPtr, const char *path)
{
	struct dirent *dentry;
	DIR *dir;
	char pbuf[PATH_MAX + 16];
	char *apath;
	IconPanel *panel = WMGetHangedData(lPtr);

	panel->preview = False;

	apath = wexpandpath(path);
	dir = opendir(apath);

	if (!dir) {
		wfree(apath);
		snprintf(pbuf, sizeof(pbuf),
		         _("Could not open directory \"%s\":\n%s"),
		         path, strerror(errno));
		wMessageDialog(scr, _("Error"), pbuf, _("OK"), NULL, NULL);
		return;
	}

	/* list contents in the column */
	while ((dentry = readdir(dir))) {
		struct stat statb;

		if (strcmp(dentry->d_name, ".") == 0 || strcmp(dentry->d_name, "..") == 0)
			continue;

		if (wstrlcpy(pbuf, apath, sizeof(pbuf)) >= sizeof(pbuf) ||
		    wstrlcat(pbuf, "/", sizeof(pbuf)) >= sizeof(pbuf) ||
		    wstrlcat(pbuf, dentry->d_name, sizeof(pbuf)) >= sizeof(pbuf)) {
			wwarning(_("full path for file \"%s\" in \"%s\" is longer than %d bytes, skipped"),
			         dentry->d_name, path, (int) (sizeof(pbuf) - 1) );
			continue;
		}

		if (stat(pbuf, &statb) < 0)
			continue;

		if (statb.st_mode & (S_IRUSR | S_IRGRP | S_IROTH)
		    && statb.st_mode & (S_IFREG | S_IFLNK)) {
			WMAddListItem(lPtr, dentry->d_name);
		}
	}
	WMSortListItems(lPtr);

	closedir(dir);
	wfree(apath);
	panel->preview = True;
}

static void setViewedImage(IconPanel *panel, const char *file)
{
	WMPixmap *pixmap;
	RColor color;
	int iwidth, iheight;

	color.red = 0xae;
	color.green = 0xaa;
	color.blue = 0xae;
	color.alpha = 0;
	iwidth = WMWidgetWidth(panel->iconView);
	iheight = WMWidgetHeight(panel->iconView);
	pixmap = WMCreateScaledBlendedPixmapFromFile(WMWidgetScreen(panel->win), file, &color, iwidth, iheight);

	if (!pixmap) {
		WMSetButtonEnabled(panel->okButton, False);

		WMSetLabelText(panel->iconView, _("Could not load image file "));

		WMSetLabelImage(panel->iconView, NULL);
	} else {
		WMSetButtonEnabled(panel->okButton, True);

		WMSetLabelText(panel->iconView, NULL);
		WMSetLabelImage(panel->iconView, pixmap);
		WMReleasePixmap(pixmap);
	}
}

static void listCallback(void *self, void *data)
{
	WMList *lPtr = (WMList *) self;
	IconPanel *panel = (IconPanel *) data;
	char *path;

	if (lPtr == panel->dirList) {
		WMListItem *item = WMGetListSelectedItem(lPtr);

		if (item == NULL)
			return;
		path = item->text;

		WMSetTextFieldText(panel->fileField, path);

		WMSetLabelImage(panel->iconView, NULL);

		WMSetButtonEnabled(panel->okButton, False);

		WMClearList(panel->iconList);
		listPixmaps(panel->scr, panel->iconList, path);
	} else {
		char *tmp, *iconFile;
		WMListItem *item = WMGetListSelectedItem(panel->dirList);

		if (item == NULL)
			return;
		path = item->text;

		item = WMGetListSelectedItem(panel->iconList);
		if (item == NULL)
			return;
		iconFile = item->text;

		tmp = wexpandpath(path);
		path = wmalloc(strlen(tmp) + strlen(iconFile) + 2);
		snprintf(path, strlen(tmp) + strlen(iconFile) + 2, "%s/%s", tmp, iconFile);
		wfree(tmp);
		WMSetTextFieldText(panel->fileField, path);
		setViewedImage(panel, path);
		wfree(path);
	}
}

static void listIconPaths(WMList * lPtr)
{
	char *paths;
	char *path;

	paths = wstrdup(w_global.config->icon_path);

	path = strtok(paths, ":");

	do {
		char *tmp;

		tmp = wexpandpath(path);
		/* do not sort, because the order implies the order of
		 * directories searched */
		if (access(tmp, X_OK) == 0)
			WMAddListItem(lPtr, path);
		wfree(tmp);
	} while ((path = strtok(NULL, ":")) != NULL);

	wfree(paths);
}

static void drawIconProc(WMList * lPtr, int index, Drawable d, char *text, int state, WMRect * rect)
{
	IconPanel *panel = WMGetHangedData(lPtr);
	WScreen *scr = panel->scr;
	char *file, *dirfile;
	WMPixmap *pixmap;
	WMColor *back;
	WMSize size;
	WMScreen *wmscr = WMWidgetScreen(panel->win);
	RColor color;
	cairo_t *cr;
	int x, y, width, height, len;
	int i, j;
	int fheight, tlen, twidth;
	int ofx, ofy;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) index;

	if (!panel->preview)
		return;

	x = rect->pos.x;
	y = rect->pos.y;
	width = rect->size.width;
	height = rect->size.height;

	back = (state & WLDSSelected) ? scr->white : scr->gray;

	dirfile = wexpandpath(WMGetListSelectedItem(panel->dirList)->text);
	len = strlen(dirfile) + strlen(text) + 4;
	file = wmalloc(len);
	snprintf(file, len, "%s/%s", dirfile, text);
	wfree(dirfile);

	color.red = WMRedComponentOfColor(back) >> 8;
	color.green = WMGreenComponentOfColor(back) >> 8;
	color.blue = WMBlueComponentOfColor(back) >> 8;
	color.alpha = WMGetColorAlpha(back) >> 8;

	pixmap = WMCreateScaledBlendedPixmapFromFile(wmscr, file, &color, width - 2, height - 2);
	wfree(file);

	if (!pixmap) {
		/*WMRemoveListItem(lPtr, index); */
		return;
	}

	cr = WCairoBeginForDrawable(wmscr, d);
	WCairoSetSourceColor(cr, back);
	cairo_rectangle(cr, x, y, width, height);
	cairo_fill(cr);

	WCairoSetSourceColor(cr, scr->white);
	cairo_rectangle(cr, x, y + height - 1, width, 1);
	cairo_fill(cr);

	size = WMGetPixmapSize(pixmap);

	/* WMCreateScaledBlendedPixmapFromFile already fits the image to
	 * the row cell so the extra 100x64 clip only ever bites when the
	 * loader could not scale, keep it as a guard */
	cairo_save(cr);
	cairo_rectangle(cr, x + (width - size.width) / 2, y + 2,
			size.width > 100 ? 100 : size.width,
			size.height > 64 ? 64 : size.height);
	cairo_clip(cr);
	WCairoPaintPixmap(cr, pixmap, x + (width - size.width) / 2, y + 2);
	cairo_restore(cr);

	fheight = WMFontHeight(panel->normalfont);
	tlen = strlen(text);
	twidth = WMWidthOfString(panel->normalfont, text, tlen);
	ofx = x + (width - twidth) / 2;
	ofy = y + 64 - fheight;

	for (i = -1; i < 2; i++)
		for (j = -1; j < 2; j++)
			WMDrawStringCairo(cr, scr->white, panel->normalfont,
					  ofx + i, ofy + j, text, tlen);

	WMDrawStringCairo(cr, scr->black, panel->normalfont, ofx, ofy, text, tlen);
	WCairoEnd(cr);

	WMReleasePixmap(pixmap);
	/* I hope it is better to do not use cache / on my box it is fast nuff */
	XFlush(dpy);
}

static void buttonCallback(void *self, void *clientData)
{
	WMButton *bPtr = (WMButton *) self;
	IconPanel *panel = (IconPanel *) clientData;

	if (bPtr == panel->okButton) {
		panel->done = True;
		panel->result = True;
	} else if (bPtr == panel->cancelButton) {
		panel->done = True;
		panel->result = False;
	} else if (bPtr == panel->previewButton) {
	/**** Previewer ****/
		WMSetButtonEnabled(bPtr, False);
		WMSetListUserDrawItemHeight(panel->iconList, 68);
		WMSetListUserDrawProc(panel->iconList, drawIconProc);
		WMRedisplayWidget(panel->iconList);
		/* for draw proc to access screen/gc */
	/*** end preview ***/
	}
#if 0
	else if (bPtr == panel->chooseButton) {
		WMOpenPanel *op;

		op = WMCreateOpenPanel(WMWidgetScreen(bPtr));

		if (WMRunModalFilePanelForDirectory(op, NULL, "/usr/local", NULL, NULL)) {
			char *path;
			path = WMGetFilePanelFile(op);
			WMSetTextFieldText(panel->fileField, path);
			setViewedImage(panel, path);
			wfree(path);
		}
		WMDestroyFilePanel(op);
	}
#endif
}

static void keyPressHandler(XEvent * event, void *data)
{
	IconPanel *panel = (IconPanel *) data;
	char buffer[32];
	KeySym ksym;
	int iidx;
	int didx;
	int item = 0;
	WMList *list = NULL;

	if (event->type == KeyRelease)
		return;

	buffer[0] = 0;
	wXkbStateUtf8(event->xkey.keycode, buffer, sizeof(buffer));
	ksym = wXkbKeycodeToKeysym(event->xkey.keycode, 0);

	iidx = WMGetListSelectedItemRow(panel->iconList);
	didx = WMGetListSelectedItemRow(panel->dirList);

	switch (ksym) {
	case XK_Up:
		if (iidx > 0)
			item = iidx - 1;
		else
			item = iidx;
		list = panel->iconList;
		break;
	case XK_Down:
		if (iidx < WMGetListNumberOfRows(panel->iconList) - 1)
			item = iidx + 1;
		else
			item = iidx;
		list = panel->iconList;
		break;
	case XK_Home:
		item = 0;
		list = panel->iconList;
		break;
	case XK_End:
		item = WMGetListNumberOfRows(panel->iconList) - 1;
		list = panel->iconList;
		break;
	case XK_Next:
		if (didx < WMGetListNumberOfRows(panel->dirList) - 1)
			item = didx + 1;
		else
			item = didx;
		list = panel->dirList;
		break;
	case XK_Prior:
		if (didx > 0)
			item = didx - 1;
		else
			item = 0;
		list = panel->dirList;
		break;
	case XK_Return:
		WMPerformButtonClick(panel->okButton);
		break;
	case XK_Escape:
		WMPerformButtonClick(panel->cancelButton);
		break;
	}

	if (list) {
		WMSelectListItem(list, item);
		WMSetListPosition(list, item - 5);
		listCallback(list, panel);
	}
}

Bool wIconChooserDialog(WScreen *scr, char **file, const char *instance, const char *class)
{
	WWindow *wwin;
	Window parent;
	IconPanel *panel;
	WMColor *color;
	WMFont *boldFont;
	Bool result;
	int wmScaleWidth, wmScaleHeight;
	int pwidth, pheight;
	static const char *prefix = NULL;
	char *title;
	int len;
	WMPoint center;

	panel = wmalloc(sizeof(IconPanel));

	panel->scr = scr;

	panel->win = WMCreateWindow(scr->wmscreen, "iconChooser");
	WMGetScaleBaseFromSystemFont(scr->wmscreen, &wmScaleWidth, &wmScaleHeight);
	pwidth = WMScaleX(450);
	pheight = WMScaleY(280);
	WMResizeWidget(panel->win, pwidth, pheight);

	WMCreateEventHandler(WMWidgetView(panel->win), KeyPressMask | KeyReleaseMask, keyPressHandler, panel);

	boldFont = WMBoldSystemFontOfSize(scr->wmscreen, WMScaleY(12));
	panel->normalfont = WMSystemFontOfSize(WMWidgetScreen(panel->win), WMScaleY(12));

	panel->dirLabel = WMCreateLabel(panel->win);
	WMResizeWidget(panel->dirLabel, WMScaleX(200), WMScaleY(20));
	WMMoveWidget(panel->dirLabel, WMScaleX(10), WMScaleY(7));
	WMSetLabelText(panel->dirLabel, _("Directories"));
	WMSetLabelFont(panel->dirLabel, boldFont);
	WMSetLabelTextAlignment(panel->dirLabel, WACenter);

	WMSetLabelRelief(panel->dirLabel, WRSunken);

	panel->iconLabel = WMCreateLabel(panel->win);
	WMResizeWidget(panel->iconLabel, WMScaleX(140), WMScaleY(20));
	WMMoveWidget(panel->iconLabel, WMScaleX(215), WMScaleY(7));
	WMSetLabelText(panel->iconLabel, _("Icons"));
	WMSetLabelFont(panel->iconLabel, boldFont);
	WMSetLabelTextAlignment(panel->iconLabel, WACenter);

	WMReleaseFont(boldFont);

	color = WMWhiteColor(scr->wmscreen);
	WMSetLabelTextColor(panel->dirLabel, color);
	WMSetLabelTextColor(panel->iconLabel, color);
	WMReleaseColor(color);

	color = WMDarkGrayColor(scr->wmscreen);
	WMSetWidgetBackgroundColor(panel->iconLabel, color);
	WMSetWidgetBackgroundColor(panel->dirLabel, color);
	WMReleaseColor(color);

	WMSetLabelRelief(panel->iconLabel, WRSunken);

	panel->dirList = WMCreateList(panel->win);
	WMResizeWidget(panel->dirList, WMScaleX(200), WMScaleY(170));
	WMMoveWidget(panel->dirList, WMScaleX(10), WMScaleY(30));
	WMSetListAction(panel->dirList, listCallback, panel);

	panel->iconList = WMCreateList(panel->win);
	WMResizeWidget(panel->iconList, WMScaleX(140), WMScaleY(170));
	WMMoveWidget(panel->iconList, WMScaleX(215), WMScaleY(30));
	WMSetListAction(panel->iconList, listCallback, panel);

	WMHangData(panel->iconList, panel);

	panel->previewButton = WMCreateCommandButton(panel->win);
	WMResizeWidget(panel->previewButton, WMScaleX(75), WMScaleY(26));
	WMMoveWidget(panel->previewButton, WMScaleX(365), WMScaleY(130));
	WMSetButtonText(panel->previewButton, _("Preview"));
	WMSetButtonAction(panel->previewButton, buttonCallback, panel);

	panel->iconView = WMCreateLabel(panel->win);
	WMResizeWidget(panel->iconView, WMScaleX(75), WMScaleY(75));
	WMMoveWidget(panel->iconView, WMScaleX(365), WMScaleY(40));
	WMSetLabelImagePosition(panel->iconView, WIPOverlaps);
	WMSetLabelRelief(panel->iconView, WRSunken);
	WMSetLabelTextAlignment(panel->iconView, WACenter);

	panel->fileLabel = WMCreateLabel(panel->win);
	WMResizeWidget(panel->fileLabel, WMScaleX(80), WMScaleY(20));
	WMMoveWidget(panel->fileLabel, WMScaleX(10), WMScaleY(210));
	WMSetLabelText(panel->fileLabel, _("File Name:"));

	panel->fileField = WMCreateTextField(panel->win);
	WMSetViewNextResponder(WMWidgetView(panel->fileField), WMWidgetView(panel->win));
	WMResizeWidget(panel->fileField, WMScaleX(345), WMScaleY(20));
	WMMoveWidget(panel->fileField, WMScaleX(95), WMScaleY(210));
	WMSetTextFieldEditable(panel->fileField, False);

	panel->okButton = WMCreateCommandButton(panel->win);
	WMResizeWidget(panel->okButton, WMScaleX(80), WMScaleY(26));
	WMMoveWidget(panel->okButton, WMScaleX(360), WMScaleY(242));
	WMSetButtonText(panel->okButton, _("OK"));
	WMSetButtonEnabled(panel->okButton, False);
	WMSetButtonAction(panel->okButton, buttonCallback, panel);

	panel->cancelButton = WMCreateCommandButton(panel->win);
	WMResizeWidget(panel->cancelButton, WMScaleX(80), WMScaleY(26));
	WMMoveWidget(panel->cancelButton, WMScaleX(270), WMScaleY(242));
	WMSetButtonText(panel->cancelButton, _("Cancel"));
	WMSetButtonAction(panel->cancelButton, buttonCallback, panel);
#if 0
	panel->chooseButton = WMCreateCommandButton(panel->win);
	WMResizeWidget(panel->chooseButton, WMScaleX(110), WMScaleY(26));
	WMMoveWidget(panel->chooseButton, WMScaleX(150), WMScaleY(242));
	WMSetButtonText(panel->chooseButton, _("Choose File"));
	WMSetButtonAction(panel->chooseButton, buttonCallback, panel);
#endif
	WMRealizeWidget(panel->win);
	WMMapSubwidgets(panel->win);

	parent = XCreateSimpleWindow(dpy, scr->root_win, 0, 0, pwidth, pheight, 0, 0, 0);

	XReparentWindow(dpy, WMWidgetXID(panel->win), parent, 0, 0);

	if (prefix == NULL)
		prefix = _("Icon Chooser");

	len = strlen(prefix)
		+ 2	// " ["
		+ (instance ? strlen(instance) : 0)
		+ 1	// "."
		+ (class ? strlen(class) : 0)
		+ 1	// "]"
		+ 1;	// final NUL

	title = wmalloc(len);
	if (instance || class)
		snprintf(title, len, "%s [%s%s%s]", prefix,
		         instance ? instance : "", instance && class ? "." : "", class ? class : "");
	else
		snprintf(title, len, "%s", prefix);

	center = getCenter(scr, pwidth, pheight);

	wwin = wManageInternalWindow(scr, parent, None, title, center.x, center.y, pwidth, pheight);
	wfree(title);

	/* put icon paths in the list */
	listIconPaths(panel->dirList);

	WMMapWidget(panel->win);

	wWindowMap(wwin);
	wSetFocusTo(scr, wwin);

	while (!panel->done) {
		XEvent event;

		WMNextEvent(dpy, &event);
		WMHandleEvent(&event);
	}

	if (panel->result) {
		char *defaultPath, *wantedPath;

		/* check if the file the user selected is not the one that
		 * would be loaded by default with the current search path */
		*file = WMGetListSelectedItem(panel->iconList)->text;
		if (**file == 0) {
			wfree(*file);
			*file = NULL;
		} else {
			defaultPath = FindImage(w_global.config->icon_path, *file);
			wantedPath = WMGetTextFieldText(panel->fileField);
			if (defaultPath) {
				/* if the file is not the default, use full path */
				if (strcmp(wantedPath, defaultPath) != 0) {
					*file = wstrdup(wantedPath);
				} else {
					*file = wstrdup(*file);
				}
				wfree(defaultPath);
			}
			wfree(wantedPath);
		}
	} else {
		*file = NULL;
	}

	result = panel->result;

	WMReleaseFont(panel->normalfont);

	WMUnmapWidget(panel->win);

	WMDestroyWidget(panel->win);

	wUnmanageWindow(wwin, False, False);

	wfree(panel);

	XDestroyWindow(dpy, parent);

	return result;
}

/*
 ***********************************************************************
 * Info Panel
 ***********************************************************************
 */

typedef struct {
	WScreen *scr;
	WWindow *wwin;
	WMWindow *win;
	WMLabel *logoL;
	WMLabel *name1L;
	WMFrame *lineF;
	WMLabel *name2L;
	WMLabel *versionL;
	WMLabel *infoL;
	WMLabel *copyrL;
} InfoPanel;

#define COPYRIGHT_TEXT  \
    "Copyright \xc2\xa9 1997-2006 Alfredo K. Kojima\n"\
    "Copyright \xc2\xa9 1998-2006 Dan Pascu\n"\
    "Copyright \xc2\xa9 2013-%hu Window Maker Developers Team"

static InfoPanel *infoPanel = NULL;

static void destroyInfoPanel(WCoreWindow *foo, void *data, XEvent *event)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;
	(void) data;
	(void) event;

	WMUnmapWidget(infoPanel);
	wUnmanageWindow(infoPanel->wwin, False, False);
	WMDestroyWidget(infoPanel->win);
	wfree(infoPanel);
	infoPanel = NULL;
}

char *getPrettyOSName(void)
{
	char line[200];
	char *token;
	char *posn = NULL;
	const char s[2] = "=";
	FILE *fp;

	fp = fopen("/etc/os-release", "r");
	if (!fp) {
		fp = fopen("/usr/lib/os-release", "r");
		if (!fp) {
			wwarning(_("no os-release file on the system"));
			return NULL;
		}
	}

	while (fgets(line, sizeof(line), fp) != NULL) {
		if (strncmp(line, "PRETTY_NAME", strlen("PRETTY_NAME")) == 0) {
			line[strcspn(line, "\r\n")] = 0;
			token = strtok(line, s);
			if (token) {
				//get straight to the second part of the line
				token = strtok(NULL, s);
				if (token) {
					//removing potential quotes
					char *dst = token;
					char *src = token;
					char c;

					while ((c = *src++) != '\0') {
						if (c == '\\') {
							*dst++ = c;
							if ((c = *src++) == '\0')
								break;
							*dst++ = c;
						} else if (c != '"' && c != '\'')
							*dst++ = c;
					}
					*dst = '\0';
					posn = wstrdup(token);
				}
			}
			break;
		}
	}
	fclose(fp);
	return posn;
}

void wShowInfoPanel(WScreen *scr)
{
	InfoPanel *panel;
	WMPixmap *logo;
	WMFont *font;
	char *strbuf = NULL;
	const char *separator;
	/* sized for two utsname fields (SYS_NMLN is 256 on FreeBSD) plus
	 * the surrounding format text, so gcc -Wformat-truncation does not
	 * flag the "Running on: %s (%s)" line below */
	char buffer[640];
	Window parent;
	WWindow *wwin;
	WMPoint center;
	char **strl;
	int i, width = 50, sepHeight;
	char *visuals[] = {
		"StaticGray",
		"GrayScale",
		"StaticColor",
		"PseudoColor",
		"TrueColor",
		"DirectColor"
	};
	int wmScaleWidth, wmScaleHeight;
	int pwidth, pheight;
	int current_year = 2020;
	time_t s;
	struct tm *current_time;
	struct utsname uts;

	s = time(NULL);
	if (s) {
		current_time = localtime(&s);
		if (current_time->tm_year > (current_year - 1900))
			current_year = current_time->tm_year + 1900;
	}

	if (infoPanel) {
		if (infoPanel->scr == scr) {
			wRaiseFrame(infoPanel->wwin->frame->core);
			wSetFocusTo(scr, infoPanel->wwin);
		}
		return;
	}

	panel = wmalloc(sizeof(InfoPanel));

	panel->scr = scr;

	panel->win = WMCreateWindow(scr->wmscreen, "info");
	WMGetScaleBaseFromSystemFont(scr->wmscreen, &wmScaleWidth, &wmScaleHeight);
#if defined(HAVE_MALLOC_H) && defined(HAVE_MALLINFO2) && defined(DEBUG)
	pwidth = WMScaleX(432);
#else
	pwidth = WMScaleX(392);
#endif
	pheight = WMScaleY(270);
	WMResizeWidget(panel->win, pwidth, pheight);

	logo = WMCreateApplicationIconBlendedPixmap(scr->wmscreen, (RColor *) NULL);
	if (!logo) {
		logo = WMRetainPixmap(WMGetApplicationIconPixmap(scr->wmscreen));
	}
	if (logo) {
		panel->logoL = WMCreateLabel(panel->win);
		WMResizeWidget(panel->logoL, WMScaleX(64), WMScaleY(64));
		WMMoveWidget(panel->logoL, WMScaleX(30), WMScaleY(20));
		WMSetLabelImagePosition(panel->logoL, WIPImageOnly);
		WMSetLabelImage(panel->logoL, logo);
		WMReleasePixmap(logo);
	}

	sepHeight = WMScaleY(3);
	panel->name1L = WMCreateLabel(panel->win);
	WMResizeWidget(panel->name1L, WMScaleX(240), WMScaleY(30) + WMScaleY(2));
	WMMoveWidget(panel->name1L, WMScaleX(100), WMScaleY(30) - WMScaleY(2) - sepHeight);

	snprintf(buffer,
		sizeof(buffer),
		"Lucida Sans,Comic Sans MS,URW Gothic L,Trebuchet MS:italic:pixelsize=%d:antialias=true",
		WMScaleY(24));
	font = WMCreateFont(scr->wmscreen, buffer);
	strbuf = "Window Maker";
	if (font) {
		width = WMWidthOfString(font, strbuf, strlen(strbuf));
		WMSetLabelFont(panel->name1L, font);
		WMReleaseFont(font);
	}
	WMSetLabelTextAlignment(panel->name1L, WACenter);
	WMSetLabelText(panel->name1L, strbuf);

	panel->lineF = WMCreateFrame(panel->win);
	WMResizeWidget(panel->lineF, width, sepHeight);
	WMMoveWidget(panel->lineF, WMScaleX(100) + (WMScaleX(240) - width) / 2, WMScaleY(60) - sepHeight);
	WMSetFrameRelief(panel->lineF, WRSimple);
	WMSetWidgetBackgroundColor(panel->lineF, scr->black);

	panel->name2L = WMCreateLabel(panel->win);
	WMResizeWidget(panel->name2L, WMScaleX(240), WMScaleY(24));
	WMMoveWidget(panel->name2L, WMScaleX(100), WMScaleY(60));
	snprintf(buffer, sizeof(buffer), "URW Gothic L,Nimbus Sans L:pixelsize=%d:antialias=true", WMScaleY(16));
	font = WMCreateFont(scr->wmscreen, buffer);
	if (font) {
		WMSetLabelFont(panel->name2L, font);
		WMReleaseFont(font);
		font = NULL;
	}
	WMSetLabelTextAlignment(panel->name2L, WACenter);
	WMSetLabelText(panel->name2L, _("Window Manager for X"));

	snprintf(buffer, sizeof(buffer), _("Version %s"), VERSION);
	panel->versionL = WMCreateLabel(panel->win);
	WMResizeWidget(panel->versionL, WMScaleX(310), WMScaleY(16));
	WMMoveWidget(panel->versionL, WMScaleX(30), WMScaleY(95));
	WMSetLabelTextAlignment(panel->versionL, WARight);
	WMSetLabelText(panel->versionL, buffer);
	WMSetLabelWraps(panel->versionL, False);

	panel->copyrL = WMCreateLabel(panel->win);
	WMResizeWidget(panel->copyrL, WMScaleX(360), WMScaleY(60));
	WMMoveWidget(panel->copyrL, WMScaleX(15), WMScaleY(215));
	WMSetLabelTextAlignment(panel->copyrL, WALeft);

	snprintf(buffer, sizeof(buffer), COPYRIGHT_TEXT, current_year);
	WMSetLabelText(panel->copyrL, buffer);
	font = WMSystemFontOfSize(scr->wmscreen, WMScaleY(11));
	if (font) {
		WMSetLabelFont(panel->copyrL, font);
		WMReleaseFont(font);
		font = NULL;
	}

	strbuf = NULL;
	if (uname(&uts) != -1) {
		char *posn = getPrettyOSName();
		if (posn) {
			snprintf(buffer, sizeof(buffer), _("Running on: %s (%s)\n"), posn, uts.machine);
			free(posn);
		}
		else
			snprintf(buffer, sizeof(buffer), _("Running on: %s (%s)\n"), uts.sysname, uts.machine);
		strbuf = wstrappend(strbuf, buffer);
	}

	snprintf(buffer, sizeof(buffer), _("Using visual 0x%x: %s %ibpp "),
		 (unsigned)scr->w_visual->visualid, visuals[scr->w_visual->class], scr->w_depth);
	strbuf = wstrappend(strbuf, buffer);

	switch (scr->w_depth) {
	case 15:
		strbuf = wstrappend(strbuf, _("(32 thousand colors)\n"));
		break;
	case 16:
		strbuf = wstrappend(strbuf, _("(64 thousand colors)\n"));
		break;
	case 24:
	case 32:
		strbuf = wstrappend(strbuf, _("(16 million colors)\n"));
		break;
	case 30:
	case 40:
		strbuf = wstrappend(strbuf, _("(1 billion colors)\n"));
		break;
	default:
		snprintf(buffer, sizeof(buffer), _("(%d colors)\n"), 1 << scr->w_depth);
		strbuf = wstrappend(strbuf, buffer);
		break;
	}

#if defined(HAVE_MALLOC_H) && defined(HAVE_MALLINFO2)
	{
		struct mallinfo2 ma = mallinfo2();
		snprintf(buffer, sizeof(buffer),
#ifdef DEBUG
					_("Total memory allocated: %lu kB (in use: %lu kB, %lu free chunks)\n"),
#else
					_("Total memory allocated: %lu kB (in use: %lu kB)\n"),
#endif
					(ma.arena + ma.hblkhd) / 1024,
					(ma.uordblks + ma.hblkhd) / 1024
#ifdef DEBUG
					/*
					 * This information is representative of the memory
					 * fragmentation. In ideal case it should be 1, but
					 * that is never possible
					 */
					, ma.ordblks
#endif
					);

		strbuf = wstrappend(strbuf, buffer);
	}
#endif

	strbuf = wstrappend(strbuf, _("Image formats: "));
	strl = RSupportedFileFormats();
	separator = NULL;
	for (i = 0; strl[i] != NULL; i++) {
		if (separator != NULL)
			strbuf = wstrappend(strbuf, separator);
		strbuf = wstrappend(strbuf, strl[i]);
		separator = ", ";
	}

	strbuf = wstrappend(strbuf, _("\nAdditional support for: "));
	strbuf = wstrappend(strbuf, "WMSPEC");

#ifdef USE_MWM_HINTS
	strbuf = wstrappend(strbuf, ", MWM");
#endif

#ifdef USE_DOCK_XDND
	strbuf = wstrappend(strbuf, ", XDnD");
#endif

#ifdef USE_XRES
	strbuf = wstrappend(strbuf, ", XRes");
#endif

#ifdef USE_MAGICK
	strbuf = wstrappend(strbuf, ", ImageMagick");
#endif

	strbuf = wstrappend(strbuf, _("\n"));
	strbuf = wstrappend(strbuf, "RandR: ");
	snprintf(buffer, sizeof(buffer), scr->xine_info.count > 1 ? _("%d head(s) found") : _("%d head found"), scr->xine_info.count);
	strbuf = wstrappend(strbuf, buffer);

	panel->infoL = WMCreateLabel(panel->win);
	WMMoveWidget(panel->infoL, WMScaleX(15), WMScaleY(115));
	WMSetLabelText(panel->infoL, strbuf);
	font = WMSystemFontOfSize(scr->wmscreen, WMScaleY(11));
	if (font) {
		const char *line = strbuf;
		int infoWidth = 0;

		while (line && *line) {
			const char *lineEnd = strchr(line, '\n');
			size_t lineLength = lineEnd ? (size_t)(lineEnd - line) : strlen(line);
			int lineWidth = WMWidthOfString(font, line, lineLength);

			if (lineWidth > infoWidth)
				infoWidth = lineWidth;
			line = lineEnd ? lineEnd + 1 : NULL;
		}

		if (infoWidth + WMScaleX(36) > pwidth)
			pwidth = infoWidth + WMScaleX(36);
		WMResizeWidget(panel->win, pwidth, pheight);
		WMResizeWidget(panel->infoL, pwidth - WMScaleX(30), WMScaleY(105));
		WMResizeWidget(panel->copyrL, pwidth - WMScaleX(30), WMScaleY(60));
		WMSetLabelFont(panel->infoL, font);
		WMReleaseFont(font);
		font = NULL;
	}
	wfree(strbuf);

	WMRealizeWidget(panel->win);
	WMMapSubwidgets(panel->win);

	parent = XCreateSimpleWindow(dpy, scr->root_win, 0, 0, pwidth, pheight, 0, 0, 0);

	XReparentWindow(dpy, WMWidgetXID(panel->win), parent, 0, 0);

	WMMapWidget(panel->win);

	center = getCenter(scr, pwidth, pheight);
	wwin = wManageInternalWindow(scr, parent, None, _("Info"), center.x, center.y, pwidth, pheight);

	WSETUFLAG(wwin, no_closable, 0);
	WSETUFLAG(wwin, no_close_button, 0);
#ifdef XKB_BUTTON_HINT
	wFrameWindowHideButton(wwin->frame, WFF_LANGUAGE_BUTTON);
#endif
	wWindowUpdateButtonImages(wwin);
	wFrameWindowShowButton(wwin->frame, WFF_CLOSE_BUTTON);
	wwin->frame->on_click_button[WTB_CLOSE] = destroyInfoPanel;

	wWindowMap(wwin);

	panel->wwin = wwin;
	infoPanel = panel;
}

/*
 ***********************************************************************
 * Legal Panel
 ***********************************************************************
 */

typedef struct {
	WScreen *scr;
	WWindow *wwin;
	WMWindow *win;
	WMFrame *frame;
	WMLabel *licenseL;
} LegalPanel;

static LegalPanel *legalPanel = NULL;

static void destroyLegalPanel(WCoreWindow * foo, void *data, XEvent * event)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;
	(void) data;
	(void) event;

	WMUnmapWidget(legalPanel->win);
	WMDestroyWidget(legalPanel->win);
	wUnmanageWindow(legalPanel->wwin, False, False);
	wfree(legalPanel);
	legalPanel = NULL;
}

void wShowLegalPanel(WScreen *scr)
{
	LegalPanel *panel;
	Window parent;
	WWindow *wwin;
	WMPoint center;
	int wmScaleWidth, wmScaleHeight;
	int pwidth, pheight;

	if (legalPanel) {
		if (legalPanel->scr == scr) {
			wRaiseFrame(legalPanel->wwin->frame->core);
			wSetFocusTo(scr, legalPanel->wwin);
		}
		return;
	}

	panel = wmalloc(sizeof(LegalPanel));
	panel->scr = scr;
	panel->win = WMCreateWindow(scr->wmscreen, "legal");
	WMGetScaleBaseFromSystemFont(scr->wmscreen, &wmScaleWidth, &wmScaleHeight);
	pwidth = WMScaleX(440);
	pheight = WMScaleY(270);
	WMResizeWidget(panel->win, pwidth, pheight);

	panel->frame = WMCreateFrame(panel->win);
	WMResizeWidget(panel->frame, pwidth - (2 * WMScaleX(10)), pheight - (2 * WMScaleY(10)));
	WMMoveWidget(panel->frame, WMScaleX(10), WMScaleY(10));
	WMSetFrameTitle(panel->frame, NULL);

	panel->licenseL = WMCreateLabel(panel->frame);
	WMSetLabelWraps(panel->licenseL, True);
	WMResizeWidget(panel->licenseL, pwidth - (4 * WMScaleX(10)), pheight - (4 * WMScaleY(10)));
	WMMoveWidget(panel->licenseL, WMScaleX(8), WMScaleY(8));
	WMSetLabelTextAlignment(panel->licenseL, WALeft);
	WMSetLabelText(panel->licenseL,
		       _("    Window Maker is free software; you can redistribute it and/or "
			 "modify it under the terms of the GNU General Public License as "
			 "published by the Free Software Foundation; either version 2 of the "
			 "License, or (at your option) any later version.\n\n"
			 "    Window Maker is distributed in the hope that it will be useful, "
			 "but WITHOUT ANY WARRANTY; without even the implied warranty "
			 "of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. "
			 "See the GNU General Public License for more details.\n\n"
			 "    You should have received a copy of the GNU General Public "
			 "License along with this program; if not, see <https://www.gnu.org/licenses/>."));

	WMRealizeWidget(panel->win);
	WMMapSubwidgets(panel->win);
	WMMapSubwidgets(panel->frame);

	parent = XCreateSimpleWindow(dpy, scr->root_win, 0, 0, pwidth, pheight, 0, 0, 0);
	XReparentWindow(dpy, WMWidgetXID(panel->win), parent, 0, 0);
	center = getCenter(scr, pwidth, pheight);
	wwin = wManageInternalWindow(scr, parent, None, _("Legal"), center.x, center.y, pwidth, pheight);

	WSETUFLAG(wwin, no_closable, 0);
	WSETUFLAG(wwin, no_close_button, 0);
	wWindowUpdateButtonImages(wwin);
	wFrameWindowShowButton(wwin->frame, WFF_CLOSE_BUTTON);
#ifdef XKB_BUTTON_HINT
	wFrameWindowHideButton(wwin->frame, WFF_LANGUAGE_BUTTON);
#endif
	wwin->frame->on_click_button[WTB_CLOSE] = destroyLegalPanel;
	panel->wwin = wwin;

	WMMapWidget(panel->win);
	wWindowMap(wwin);
	legalPanel = panel;
}

/*
 ***********************************************************************
 * Crashing Dialog Panel
 ***********************************************************************
 */

typedef struct _CrashPanel {
	WMWindow *win;		/* main window */

	WMLabel *iconL;		/* application icon */
	WMLabel *nameL;		/* title of panel */

	WMFrame *sepF;		/* separator frame */

	WMLabel *noteL;		/* Title of note */
	WMLabel *note2L;	/* body of note with what happened */

	WMFrame *whatF;		/* "what to do next" frame */
	WMPopUpButton *whatP;	/* action selection popup button */

	WMButton *okB;		/* ok button */

	Bool done;		/* if finished with this dialog */
	int action;		/* what to do after */

	KeyCode retKey;
	KeyCode upKey;
	KeyCode downKey;

} CrashPanel;

static void handleKeyPress(XEvent * event, void *clientData)
{
	CrashPanel *panel = (CrashPanel *) clientData;

	if (event->xkey.keycode == panel->retKey) {
		WMPerformButtonClick(panel->okB);
		return;
	}

	if (event->xkey.keycode == panel->upKey) {
		WMSelectPopUpButtonPreviousItem(panel->whatP);
		return;
	}

	if (event->xkey.keycode == panel->downKey) {
		WMSelectPopUpButtonNextItem(panel->whatP);
		return;
	}
}

static void okButtonCallback(void *self, void *clientData)
{
	CrashPanel *panel = (CrashPanel *) clientData;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;

	panel->done = True;
}

static void setCrashAction(void *self, void *clientData)
{
	WMPopUpButton *pop = (WMPopUpButton *) self;
	CrashPanel *panel = (CrashPanel *) clientData;

	panel->action = WMGetPopUpButtonSelectedItem(pop);
}

/* Make this read the logo from a compiled in pixmap -Dan */
static WMPixmap *getWindowMakerIconImage(WMScreen *scr)
{
	WMPixmap *pix = NULL;
	char *path = NULL;

	/* Get the Logo icon, without the default icon */
	path = get_icon_filename("Logo", "WMPanel", NULL, False);

	if (path) {
		RColor gray;

		gray.red = 0xae;
		gray.green = 0xaa;
		gray.blue = 0xae;
		gray.alpha = 0;

		pix = WMCreateBlendedPixmapFromFile(scr, path, &gray);
		wfree(path);
	}

	return pix;
}

#define PWIDTH	295
#define PHEIGHT	345

int wShowCrashingDialogPanel(int whatSig)
{
	CrashPanel *panel;
	WMScreen *scr;
	WMFont *font;
	WMPixmap *logo;
	int screen_no, scr_width, scr_height;
	int action;
	char buf[256];

	screen_no = DefaultScreen(dpy);
	scr_width = WidthOfScreen(ScreenOfDisplay(dpy, screen_no));
	scr_height = HeightOfScreen(ScreenOfDisplay(dpy, screen_no));

	scr = WMCreateScreen(dpy, screen_no);
	if (!scr) {
		werror(_("cannot open connection for crashing dialog panel. Aborting."));
		return WMAbort;
	}

	panel = wmalloc(sizeof(CrashPanel));

	panel->retKey = XKeysymToKeycode(dpy, XK_Return);
	panel->upKey = XKeysymToKeycode(dpy, XK_Up);
	panel->downKey = XKeysymToKeycode(dpy, XK_Down);

	panel->win = WMCreateWindow(scr, "crashingDialog");
	WMResizeWidget(panel->win, PWIDTH, PHEIGHT);
	WMMoveWidget(panel->win, (scr_width - PWIDTH) / 2, (scr_height - PHEIGHT) / 2);

	logo = getWindowMakerIconImage(scr);
	if (logo) {
		panel->iconL = WMCreateLabel(panel->win);
		WMResizeWidget(panel->iconL, 64, 64);
		WMMoveWidget(panel->iconL, 10, 10);
		WMSetLabelImagePosition(panel->iconL, WIPImageOnly);
		WMSetLabelImage(panel->iconL, logo);
	}

	panel->nameL = WMCreateLabel(panel->win);
	WMResizeWidget(panel->nameL, 200, 30);
	WMMoveWidget(panel->nameL, 80, 25);
	WMSetLabelTextAlignment(panel->nameL, WALeft);
	font = WMBoldSystemFontOfSize(scr, 24);
	WMSetLabelFont(panel->nameL, font);
	WMReleaseFont(font);
	WMSetLabelText(panel->nameL, _("Fatal error"));

	panel->sepF = WMCreateFrame(panel->win);
	WMResizeWidget(panel->sepF, PWIDTH + 4, 2);
	WMMoveWidget(panel->sepF, -2, 80);

	panel->noteL = WMCreateLabel(panel->win);
	WMResizeWidget(panel->noteL, PWIDTH - 20, 40);
	WMMoveWidget(panel->noteL, 10, 90);
	WMSetLabelTextAlignment(panel->noteL, WAJustified);
	snprintf(buf, sizeof(buf), _("Window Maker received signal %i."), whatSig);
	WMSetLabelText(panel->noteL, buf);

	panel->note2L = WMCreateLabel(panel->win);
	WMResizeWidget(panel->note2L, PWIDTH - 20, 100);
	WMMoveWidget(panel->note2L, 10, 130);
	WMSetLabelTextAlignment(panel->note2L, WALeft);
	snprintf(buf, sizeof(buf), /* Comment for the PO file: the %s is an email address */
	         _(" This fatal error occurred probably due to a bug."
	           " Please fill the included BUGFORM and report it to %s."),
	         PACKAGE_BUGREPORT);
	WMSetLabelText(panel->note2L, buf);
	WMSetLabelWraps(panel->note2L, True);

	panel->whatF = WMCreateFrame(panel->win);
	WMResizeWidget(panel->whatF, PWIDTH - 20, 50);
	WMMoveWidget(panel->whatF, 10, 240);
	WMSetFrameTitle(panel->whatF, _("What do you want to do now?"));

	panel->whatP = WMCreatePopUpButton(panel->whatF);
	WMResizeWidget(panel->whatP, PWIDTH - 20 - 70, 20);
	WMMoveWidget(panel->whatP, 35, 20);
	WMSetPopUpButtonPullsDown(panel->whatP, False);
	WMSetPopUpButtonText(panel->whatP, _("Select action"));
	WMAddPopUpButtonItem(panel->whatP, _("Abort and leave a core file"));
	WMAddPopUpButtonItem(panel->whatP, _("Restart Window Maker"));
	WMAddPopUpButtonItem(panel->whatP, _("Start alternate window manager"));
	WMSetPopUpButtonAction(panel->whatP, setCrashAction, panel);
	WMSetPopUpButtonSelectedItem(panel->whatP, WMRestart);
	panel->action = WMRestart;

	WMMapSubwidgets(panel->whatF);

	panel->okB = WMCreateCommandButton(panel->win);
	WMResizeWidget(panel->okB, 80, 26);
	WMMoveWidget(panel->okB, 205, 309);
	WMSetButtonText(panel->okB, _("OK"));
	WMSetButtonImage(panel->okB, WMGetSystemPixmap(scr, WSIReturnArrow));
	WMSetButtonAltImage(panel->okB, WMGetSystemPixmap(scr, WSIHighlightedReturnArrow));
	WMSetButtonImagePosition(panel->okB, WIPRight);
	WMSetButtonAction(panel->okB, okButtonCallback, panel);

	panel->done = 0;

	WMCreateEventHandler(WMWidgetView(panel->win), KeyPressMask, handleKeyPress, panel);

	WMRealizeWidget(panel->win);
	WMMapSubwidgets(panel->win);

	WMMapWidget(panel->win);

	XSetInputFocus(dpy, WMWidgetXID(panel->win), RevertToParent, CurrentTime);

	while (!panel->done) {
		XEvent event;

		WMNextEvent(dpy, &event);
		WMHandleEvent(&event);
	}

	action = panel->action;

	WMUnmapWidget(panel->win);
	WMDestroyWidget(panel->win);
	wfree(panel);

	return action;
}
