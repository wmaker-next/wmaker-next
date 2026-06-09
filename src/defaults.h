/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
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

WDDomain * wDefaultsInitDomain(const char *domain, Bool requireDictionary);

void wDefaultsMergeGlobalMenus(WDDomain *menuDomain);

void wReadDefaults(WScreen *scr, const struct WConfig *old_config,
                   const struct WConfig *new_config,
                   const struct WConfigChanges *changes);
void wReadKeybindings(WScreen *scr, WMPropList *dict);
void wDefaultUpdateIcons(WScreen *scr);
void wDefaultsCheckDomains(void *arg);
void wDefaultFillAttributes(const char *instance, const char *class,
                            WWindowAttributes *attr, WWindowAttributes *mask,
                            Bool useGlobalDefault);
/* Reparse w_global.domain.window_attr->dictionary into the lookup cache.
 * Call after the domain is loaded, after a runtime reload, and after any
 * in-process mutation of the dictionary. */
void wWindowAttrsReload(void);

char *get_default_image_path(void);
RImage *get_default_image(WScreen *scr);

char *wDefaultGetIconFile(const char *instance, const char *class, Bool default_icon);

RImage *get_icon_image(WScreen *scr, const char *winstance, const char *wclass, int max_size);
char *get_icon_filename(const char *winstance, const char *wclass, const char *command,
			Bool default_icon);


int wDefaultGetStartWorkspace(WScreen *scr, const char *instance, const char *class);
void wDefaultChangeIcon(const char *instance, const char* class, const char *file);
RImage *get_rimage_from_file(WScreen *scr, const char *file_name, int max_size);

void wDefaultPurgeInfo(const char *instance, const char *class);
void wKeyTreeRebuild(WScreen *scr);	/* Rebuild the key-chain trie from the current key bindings */
#endif /* WMDEFAULTS_H_ */
