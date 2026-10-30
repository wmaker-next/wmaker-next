/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
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

#ifndef WMDEFAULTS_H_
#define WMDEFAULTS_H_

#include <time.h>

typedef struct WDDomain {
	const char *domain_name;
	WMPropList *dictionary;
	const char *path;
	/* st_mtim of the user file when last read, compared at nanosecond
	 * resolution so two writes within the same wall-clock second (e.g.
	 * back-to-back wdwrite calls) are still picked up by the inotify
	 * reload, every section-16 target carries POSIX-2008 st_mtim */
	struct timespec timestamp;
} WDDomain;

struct WConfig;
struct WConfigChanges;

WDDomain * wDefaultsInitDomain(const char *domain, bool requireDictionary);

void wDefaultsMergeGlobalMenus(WDDomain *menuDomain);

void wReadDefaults(WScreen *scr, const struct WConfig *old_config,
                   const struct WConfig *new_config,
                   const struct WConfigChanges *changes);
void wReadKeybindings(WScreen *scr, WMPropList *dict, bool *batch_changed);
void wDefaultUpdateIcons(WScreen *scr);
void wDefaultsCheckDomains(void *arg);
void wDefaultFillAttributes(const char *instance, const char *class,
                            WWindowAttributes *attr, WWindowAttributes *mask,
                            bool useGlobalDefault);
/* Same as wDefaultFillAttributes but additionally layers the first matching
 * DynamicRules entry on top. Pulls the title, role, command, machine and
 * _NET_WM_WINDOW_TYPE atom-name list directly from WWindow so the lookup
 * never calls XGetAtomName() itself. Pass a WWindow with the relevant
 * fields populated (the test harness does so without an open display). */
void wDefaultFillAttributesForWindow(const WWindow *wwin,
				     WWindowAttributes *attr,
				     WWindowAttributes *mask,
				     bool useGlobalDefault);
/* Reparse w_global.domain.window_attr->dictionary into the lookup cache.
 * Call after the domain is loaded, after a runtime reload, and after any
 * in-process mutation of the dictionary. */
void wWindowAttrsReload(void);

char *get_default_image_path(void);
RImage *get_default_image(WScreen *scr);

char *wDefaultGetIconFile(const char *instance, const char *class, bool default_icon);

RImage *get_icon_image(WScreen *scr, const char *winstance, const char *wclass, int max_size);
char *get_icon_filename(const char *winstance, const char *wclass, const char *command,
			bool default_icon);


bool wDefaultGetStartWorkspaces(WScreen *scr, const char *instance,
				const char *class, uint64_t *out_mask);
void wDefaultChangeIcon(const char *instance, const char* class, const char *file);
RImage *get_rimage_from_file(WScreen *scr, const char *file_name, int max_size);

void wDefaultFillFrameBorders(WScreen *scr, const char *instance,
			      const char *class, WWindow *wwin);
const char *wDefaultGetStartGeometry(const char *instance, const char *class);
/* Resolve the MarkKey attribute for wwin through the four-level per-app
 * merge and the first matching DynamicRules entry (dynamic wins). NULL
 * when neither supplies MarkKey, an empty string means local-unmark and
 * is returned as such. The pointer is owned by the attrs cache and is
 * invalidated by wWindowAttrsReload(). */
const char *wDefaultGetMarkKeyForWindow(const WWindow *wwin);

void wDefaultPurgeInfo(const char *instance, const char *class);
void wKeyTreeRebuild(WScreen *scr);	/* Rebuild the key-chain trie from the current key bindings */
#endif /* WMDEFAULTS_H_ */
