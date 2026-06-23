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
	char *try_exec;		/* TryExec=, may be NULL */
	char *wmclass;		/* StartupWMClass=, may be NULL */
	char *icon;		/* raw Icon= value, theme name or path, may be NULL */
	unsigned no_display:1;
	unsigned hidden:1;
	unsigned terminal:1;
	unsigned is_application:1;	/* Type=Application or Type= absent */
} WDesktopEntry;

/* fills *out from the [Desktop Entry] group of path, returns True when
 * a non-empty Exec= was found, *out fields are zeroed first so a False
 * return needs no wDesktopEntryFree() */
Bool wDesktopEntryRead(const char *path, WDesktopEntry *out);

/* frees the malloc'd fields of *e, does not free e itself */
void wDesktopEntryFree(WDesktopEntry *e);

/* in-place strip of freedesktop %f/%F/%u/%U/%i/%c/%k field codes,
 * collapses %% to % and trims trailing whitespace */
void wDesktopExecStrip(char *exec);

/* called once per XDG data directory, dir has suffix appended and no
 * trailing slash, return False to stop the walk early */
typedef Bool (*WXDGDirFn)(const char *dir, void *ctx);

/* calls fn with $XDG_DATA_HOME/suffix (fallback ~/.local/share/suffix)
 * then each $XDG_DATA_DIRS element + /suffix in order (fallback
 * /usr/local/share:/usr/share) */
void wXDGForEachDataDir(const char *suffix, WXDGDirFn fn, void *ctx);

#endif /* WMDESKTOPFILE_H_ */
