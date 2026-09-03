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

#ifndef WMDESKTOPFILE_H_
#define WMDESKTOPFILE_H_

/*
 * Minimal freedesktop .desktop entry reader and XDG data directory
 * enumeration shared by the dock launcher drop handler and the run
 * dialog application completion.
 */

typedef struct WDesktopEntry {
	char *name;		/* locale-ranked Name= */
	char *comment;		/* locale-ranked Comment=, may be NULL */
	char *exec;		/* Exec= with %fFuUick field codes stripped */
	char *exec_raw;		/* Exec= verbatim, for %f/%u substitution */
	char *try_exec;		/* TryExec=, may be NULL */
	char *wmclass;		/* StartupWMClass=, may be NULL */
	char *icon;		/* raw Icon= value, theme name or path, may be NULL */
	char *mimetypes;	/* raw MimeType= ;-separated list, may be NULL */
	unsigned no_display:1;
	unsigned hidden:1;
	unsigned terminal:1;
	unsigned is_application:1;	/* Type=Application or Type= absent */
} WDesktopEntry;

/* fills *out from the [Desktop Entry] group of path, returns True when
 * a non-empty Exec= was found, *out fields are zeroed first so a False
 * return needs no wDesktopEntryFree() */
bool wDesktopEntryRead(const char *path, WDesktopEntry *out);

/* frees the malloc'd fields of *e, does not free e itself */
void wDesktopEntryFree(WDesktopEntry *e);

/* in-place strip of freedesktop %f/%F/%u/%U/%i/%c/%k field codes,
 * collapses %% to % and trims trailing whitespace */
void wDesktopExecStrip(char *exec);

/* called once per XDG data directory, dir has suffix appended and no
 * trailing slash, return False to stop the walk early */
typedef bool (*WXDGDirFn)(const char *dir, void *ctx);

/* calls fn with $XDG_DATA_HOME/suffix (fallback ~/.local/share/suffix)
 * then each $XDG_DATA_DIRS element + /suffix in order (fallback
 * /usr/local/share:/usr/share) */
void wXDGForEachDataDir(const char *suffix, WXDGDirFn fn, void *ctx);

/* resolve a freedesktop icon-theme name to an absolute file by
 * walking the spec-mandated hicolor fallback theme and the legacy
 * pixmaps directory, context is the hicolor subdirectory ("apps",
 * "mimetypes", ...), sizes nearest want_size win preferring an
 * exact-size raster then a scalable render then the next-larger
 * raster over an upscale, and PNG over SVG over XPM, returns a
 * malloc'd path or NULL. When the returned path is a vector format,
 * load it with RLoadImageAtSize(..., want_size, want_size). */
char *wXDGFindThemeIcon(const char *name, const char *context,
			int want_size);


typedef struct WXDGApp {
	char *id;		/* desktop-file-id with .desktop suffix */
	char *name;		/* locale-ranked display name */
	char *name_fold;	/* name lower-cased for ASCII matching */
	char *exec;		/* shell-ready command, terminal-wrapped */
	char *exec_raw;		/* original Exec= for %f/%u substitution */
	char *comment;		/* may be NULL */
	char *icon;		/* raw Icon= value, theme name or path */
	unsigned terminal:1;	/* Terminal=true, exec is wrapped */
} WXDGApp;

/* lazily built list of visible Type=Application entries from every
 * XDG_DATA_HOME and XDG_DATA_DIRS applications directory, deduplicated
 * by desktop-file-id with the user directory winning, revalidated
 * against the source directory mtimes on each call, the array and its
 * elements are owned by the cache and live until invalidation */
WMArray *wXDGAppList(void);

/* lookup by desktop-file-id in the cached list, does not rebuild */
WXDGApp *wXDGAppById(const char *desktop_id);

/* visible applications whose MimeType= list contains type, the
 * array and its WXDGApp* elements are owned by the cache, never
 * NULL but may be empty */
WMArray *wXDGAppsForMime(const char *type);

/* free the cached list, called from Exit() so leak checks stay quiet */
void wXDGAppListInvalidate(void);


/* MIME type by extension via the shared-mime-info globs2 database,
 * longest literal *.ext suffix wins, return is owned by the cache */
const char *wXDGMimeTypeForPath(const char *path);

/* desktop-file-id of the default handler for mimetype from the
 * mimeapps.list and mimeinfo.cache association files, return is
 * owned by the cache, NULL when no association is found */
const char *wXDGDefaultAppFor(const char *mimetype);

/* malloc'd shell command that opens path with its default handler,
 * the file's %f or %u Exec field code is substituted with the
 * shell-quoted path, falls back to "xdg-open '<path>'" when the
 * type or handler cannot be resolved */
char *wXDGOpenCommandFor(const char *path);

/* malloc'd shell command that opens path with the given application,
 * same field-code substitution and terminal wrap as above */
char *wXDGAppOpenCommand(const WXDGApp *app, const char *path);

/* free the globs2 and mimeapps caches, called from Exit() */
void wXDGMimeInvalidate(void);


typedef struct WXDGDoc {
	char *path;		/* absolute path */
	const char *base;	/* basename, points into path */
	char *base_fold;	/* lower-cased basename for matching */
	time_t mtime;		/* file mtime for recency ranking */
} WXDGDoc;

/* lazily built list of regular files under XDG_DOCUMENTS_DIR,
 * XDG_DOWNLOAD_DIR and XDG_DESKTOP_DIR (read from user-dirs.dirs
 * with the usual fallbacks) plus each of extra_roots, walked at
 * most depth levels deep skipping dot-directories and any tree
 * containing a .git or .nomedia marker, capped at max_files,
 * the array and its elements are owned by the cache and live until
 * invalidation, parameters are remembered so a changed depth, cap
 * or root mtime triggers a rebuild on the next call */
WMArray *wXDGDocList(char **extra_roots, int n_extra,
		     int depth, int max_files);

void wXDGDocListInvalidate(void);

/* install file-watch hooks the document index uses to invalidate
 * itself when a root changes, NULL hooks (the default) fall back
 * to the per-call root-mtime check so the unit test and any
 * non-inotify build keep working */
void wXDGSetDocWatchHooks(int (*add)(const char *, unsigned,
				     void (*)(void)),
			  void (*remove)(int));


/* greedy fzf-v1 / rofi-style subsequence matcher with positional
 * bonuses, returns <0 when needle is not a subsequence of hay_fold,
 * otherwise a higher-is-better score, hay_orig is the original-case
 * haystack used to spot word boundaries and CamelCase humps */
int wFuzzyScore(const char *needle_fold, const char *hay_fold,
		const char *hay_orig);

/* hits decayed by age in days, capped at 100 so a frequently-used
 * entry can lift itself within a rank band but not jump bands */
int wFrecencyScore(int hits, time_t last, time_t now);

#endif /* WMDESKTOPFILE_H_ */
