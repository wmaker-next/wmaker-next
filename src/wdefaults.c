/* wdefaults.c - window specific defaults
 *
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

#include "wconfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <wraster.h>

#include "WindowMaker.h"
#include "window.h"
#include "appicon.h"
#include "screen.h"
#include "workspace.h"
#include "defaults.h"
#include "wdefaults.h"
#include "icon.h"
#include "misc.h"

_Static_assert(WATTR_BOOL_COUNT <= 32,
	       "WWindowAttrs bitset is uint32_t, raise the field width");

/*
 * Cache of parsed WMWindowAttributes entries, keyed by the proplist
 * dictionary key string ("instance.class", "instance", "class" or "*").
 * The proplist dictionary remains the source of truth for writes,
 * wWindowAttrsReload() rebuilds this cache from it.
 */
static WMHashTable *attrs_cache;
/* every boolean defaults to No, set covers all bits so merge_attrs()
 * writes every flag when useGlobalDefault is in effect */
static const WWindowAttrs attrs_global_default = { 0, ~(uint32_t)0, NULL, NULL };

/* proplist key atoms used by the write/maintenance helpers */
static WMPropList *AIcon;
static WMPropList *AnyWindow;

#define WATTR_NAME_ROW(K, name, flag) name,
static const char * const attr_bool_names[WATTR_BOOL_COUNT] = {
	WATTR_BOOLS(WATTR_NAME_ROW)
};
#undef WATTR_NAME_ROW

static void init_wdefaults(void)
{
	if (AIcon)
		return;
	AIcon = WMCreatePLString("Icon");
	AnyWindow = WMCreatePLString("*");
}

static char *get_entry_string(WMPropList *entry, const char *key)
{
	WMPropList *value;

	value = WMGetFromPLDictionaryByName(entry, key);
	if (!value)
		return NULL;
	if (!WMIsPLString(value)) {
		wwarning(_("Wrong option format for key \"%s\". Should be %s."),
			 key, "String");
		return NULL;
	}
	return WMGetFromPLString(value);
}

static WWindowAttrs *parse_attrs_entry(WMPropList *entry)
{
	WWindowAttrs *attrs;
	const char *str;
	int i;

	attrs = wmalloc(sizeof(*attrs));

	for (i = 0; i < WATTR_BOOL_COUNT; i++) {
		WMPropList *value;

		value = WMGetFromPLDictionaryByName(entry, attr_bool_names[i]);
		if (!value)
			continue;
		attrs->set |= (uint32_t)1 << i;
		if (WMPLGetBool(value))
			attrs->value |= (uint32_t)1 << i;
	}

	str = get_entry_string(entry, "Icon");
	if (str)
		attrs->icon = wstrdup(str);
	str = get_entry_string(entry, "StartWorkspace");
	if (str)
		attrs->start_workspace = wstrdup(str);

	return attrs;
}

static void free_attrs_entry(WWindowAttrs *attrs)
{
	if (!attrs)
		return;
	if (attrs->icon)
		wfree(attrs->icon);
	if (attrs->start_workspace)
		wfree(attrs->start_workspace);
	wfree(attrs);
}

static void free_attrs_cache(void)
{
	WMHashEnumerator e;
	WWindowAttrs *attrs;

	if (!attrs_cache)
		return;
	e = WMEnumerateHashTable(attrs_cache);
	while ((attrs = WMNextHashEnumeratorItem(&e)) != NULL)
		free_attrs_entry(attrs);
	WMFreeHashTable(attrs_cache);
	attrs_cache = NULL;
}

void wWindowAttrsReload(void)
{
	WMPropList *dict, *keys;
	int i, count;

	init_wdefaults();
	free_attrs_cache();
	attrs_cache = WMCreateHashTable(WMStringHashCallbacks);

	if (!w_global.domain.window_attr)
		return;
	dict = w_global.domain.window_attr->dictionary;
	if (!dict || !WMIsPLDictionary(dict))
		return;

	/* both the outer instance.class key and the inner attribute keys
	 * are matched case-sensitively, same as the original per-call
	 * proplist walk */
	WMPLSetCaseSensitive(True);

	keys = WMGetPLDictionaryKeys(dict);
	count = keys ? WMGetPropListItemCount(keys) : 0;
	for (i = 0; i < count; i++) {
		WMPropList *plkey = WMGetFromPLArray(keys, i);
		WMPropList *entry;
		const char *key;

		if (!plkey || !WMIsPLString(plkey))
			continue;
		key = WMGetFromPLString(plkey);
		entry = WMGetFromPLDictionary(dict, plkey);
		if (!entry || !WMIsPLDictionary(entry))
			continue;
		WMHashInsert(attrs_cache, key, parse_attrs_entry(entry));
	}
	if (keys)
		WMReleasePropList(keys);

	WMPLSetCaseSensitive(False);
}

static const WWindowAttrs *attrs_lookup(const char *key)
{
	if (!attrs_cache)
		wWindowAttrsReload();
	if (!key)
		return NULL;
	return WMHashGet(attrs_cache, key);
}

/* Look up the parsed entries for instance.class/instance/class/"*" in
 * precedence order. Returns the number of levels filled (up to 4),
 * level[0] is the highest-precedence match. */
static int attrs_lookup_levels(const char *instance, const char *class,
			       Bool useGlobalDefault, const WWindowAttrs *level[4])
{
	int n = 0;

	/* A client may set WM_CLASS with one or both components empty.
	 * An empty string identifies nothing, so skip that lookup level
	 * and fall through to the less specific ones, the same way an
	 * absent component would. */
	if (instance && !*instance)
		instance = NULL;
	if (class && !*class)
		class = NULL;

	if (instance && class) {
		char *both = StrConcatDot(instance, class);
		level[n] = attrs_lookup(both);
		wfree(both);
		if (level[n])
			n++;
	}
	if (instance) {
		level[n] = attrs_lookup(instance);
		if (level[n])
			n++;
	}
	if (class) {
		level[n] = attrs_lookup(class);
		if (level[n])
			n++;
	}
	if (useGlobalDefault) {
		level[n] = attrs_lookup("*");
		if (level[n])
			n++;
	}
	return n;
}

static void merge_attrs(WWindowAttributes *attr, WWindowAttributes *mask,
			const WWindowAttrs *src)
{
#define WATTR_MERGE_ROW(K, name, flag) \
	if (src->set & ((uint32_t)1 << WATTR_##K)) { \
		attr->flag = (src->value >> WATTR_##K) & 1u; \
		if (mask) mask->flag = 1; \
	}
	WATTR_BOOLS(WATTR_MERGE_ROW)
#undef WATTR_MERGE_ROW
}

/*
 *----------------------------------------------------------------------
 * wDefaultFillAttributes--
 * 	Retrieves attributes for the specified instance/class and
 * fills attr with it. Values that are actually defined are also
 * set in mask. If useGlobalDefault is True, the default for
 * all windows ("*") will be used for when no values are found
 * for that instance/class.
 *
 *----------------------------------------------------------------------
 */
void wDefaultFillAttributes(const char *instance, const char *class,
			    WWindowAttributes *attr, WWindowAttributes *mask,
			    Bool useGlobalDefault)
{
	const WWindowAttrs *level[4];
	int n, i;

	n = attrs_lookup_levels(instance, class, useGlobalDefault, level);

	/* with useGlobalDefault every key falls back to No, so apply the
	 * all-zero/all-set global default first, then layer the parsed
	 * entries on top from lowest precedence to highest */
	if (useGlobalDefault)
		merge_attrs(attr, mask, &attrs_global_default);
	for (i = n - 1; i >= 0; i--)
		merge_attrs(attr, mask, level[i]);
}

/*
 * Remove a stale Icon entry from the WMWindowAttributes domain so the
 * missing-icon warning is emitted only once and the application falls
 * back to the default icon. The entry is always dropped from the
 * in-memory dictionary, the on-disk user defaults are rewritten only
 * when the stale entry actually came from there, entries inherited from
 * the global system defaults are left on disk and merely suppressed in
 * memory until the domain is next reloaded.
 */
static void drop_stale_icon(const char *instance, const char *class, const char *file_name)
{
	WDDomain *db = w_global.domain.window_attr;
	WMPropList *dict, *attr, *value, *user_dict, *user_attr;
	WMPropList *keys[3];
	WMPropList *match_key = NULL, *match_attr = NULL;
	Bool from_user = False;
	Bool persist;
	int i, nkeys = 0;

	if (!db || !db->dictionary || !file_name || file_name[0] == '\0')
		return;

	/*
	 * For absolute paths only prune when the file is really gone, do not
	 * drop the entry on transient errors such as EACCES on a network mount
	 * that is briefly unavailable. Relative names are searched through
	 * IconPath by FindImage(), which loses errno, so for those we only
	 * suppress in memory and never rewrite the user file, a transient
	 * search-path failure then self-heals on the next domain reload.
	 */
	if (file_name[0] == '/') {
		if (access(file_name, F_OK) < 0 && errno != ENOENT)
			return;
		persist = True;
	} else {
		persist = False;
	}

	dict = db->dictionary;

	WMPLSetCaseSensitive(True);

	/* same empty-is-absent rule as attrs_lookup_levels() */
	if (instance && !*instance)
		instance = NULL;
	if (class && !*class)
		class = NULL;

	if (instance && class) {
		char *both = StrConcatDot(instance, class);
		keys[nkeys++] = WMCreatePLString(both);
		wfree(both);
	}
	if (instance)
		keys[nkeys++] = WMCreatePLString(instance);
	if (class)
		keys[nkeys++] = WMCreatePLString(class);

	/* Find which per-app entry actually holds the stale Icon value */
	for (i = 0; i < nkeys; i++) {
		attr = WMGetFromPLDictionary(dict, keys[i]);
		if (!attr || !WMIsPLDictionary(attr))
			continue;
		value = WMGetFromPLDictionary(attr, AIcon);
		if (value && WMIsPLString(value) &&
		    strcmp(WMGetFromPLString(value), file_name) == 0) {
			match_key = keys[i];
			match_attr = attr;
			break;
		}
	}

	if (!match_attr)
		goto out;

	/*
	 * Decide whether the stale entry came from the user's own defaults
	 * file or was merged in from the global system defaults, we only
	 * rewrite the user file in the former case.
	 */
	user_dict = db->path ? WMReadPropListFromFile(db->path) : NULL;
	if (user_dict) {
		if (WMIsPLDictionary(user_dict)) {
			user_attr = WMGetFromPLDictionary(user_dict, match_key);
			if (user_attr && WMIsPLDictionary(user_attr) &&
			    WMGetFromPLDictionary(user_attr, AIcon))
				from_user = True;
		}
		WMReleasePropList(user_dict);
	}

	WMRemoveFromPLDictionary(match_attr, AIcon);
	if (WMGetPropListItemCount(match_attr) == 0)
		WMRemoveFromPLDictionary(dict, match_key);

	if (persist && from_user && !wPreferences.flags.noupdates)
		UpdateDomainFile(db);

	wWindowAttrsReload();

out:
	for (i = 0; i < nkeys; i++)
		WMReleasePropList(keys[i]);

	WMPLSetCaseSensitive(False);
}

/* Search the icon cache directory for an icon. When file_name is given
 * (a per-app Icon entry from WMWindowAttributes) look it up there. When
 * it is NULL, fall back to the standard <instance>.<class>.xpm naming
 * that wIconStore() uses, so an entry dropped after a stale-IconPath
 * lookup self-heals from the cached file. */
static char *find_in_icon_cache(const char *file_name,
			       const char *winstance, const char *wclass)
{
	const char *cache_dir = wGetIconCacheDir();
	char *constructed = NULL;
	char *path;

	if (!cache_dir)
		return NULL;

	if (!file_name) {
		char *base = get_name_for_instance_class(winstance, wclass);

		if (!base)
			return NULL;
		constructed = wstrconcat(base, ".xpm");
		wfree(base);
		file_name = constructed;
	}

	path = FindImage(cache_dir, file_name);
	wfree(constructed);
	return path;
}

/* Get the file name of the image, using instance and class */
char *get_icon_filename(const char *winstance, const char *wclass, const char *command,
			Bool default_icon)
{
	char *file_name;
	char *file_path;

	/* Look up the per-app icon file name, using instance and class */
	file_name = wDefaultGetIconFile(winstance, wclass, False);

	/* Check if the file really exists in the disk */
	if (file_name)
		file_path = FindImage(w_global.config->icon_path, file_name);
	else
		file_path = NULL;

	/* wIconStore() writes cached icons to <cache-root>/CachedPixmaps,
	 * which the default IconPath includes via $WMAKER_USER_CACHE. A user
	 * IconPath inherited from a pre-XDG-cache layout may still point at
	 * the old (now unused) location and miss the cache dir entirely, so
	 * search it directly to keep the cache write and the cache lookup
	 * consistent regardless of the configured IconPath. When no per-app
	 * Icon entry exists at all, the cache is also searched by its
	 * standard naming so a dropped entry self-heals. */
	if (!file_path)
		file_path = find_in_icon_cache(file_name, winstance, wclass);

	/* If the specific icon filename is not found, and command is specified,
	 * then include the .app icons and re-do the search. */
	if (!file_path && command) {
		wApplicationExtractDirPackIcon(command, winstance, wclass);
		file_name = wDefaultGetIconFile(winstance, wclass, False);
		if (file_name) {
			file_path = FindImage(w_global.config->icon_path, file_name);
			if (!file_path)
				file_path = find_in_icon_cache(file_name, winstance, wclass);
		}
	}

	/*
	 * If a per-app Icon entry exists but the image file is gone, warn once
	 * and drop the stale entry from WMWindowAttributes so we fall back to
	 * the default icon instead of repeating the warning on every lookup.
	 */
	if (file_name && !file_path) {
		wwarning(_("icon \"%s\" doesn't exist, check your config files"), file_name);
		drop_stale_icon(winstance, wclass, file_name);
		/* file_name pointed into the cache entry we just removed */
		file_name = NULL;
	}

	/*
	 * Don't wfree(file_name), it points into the parsed attrs cache and
	 * is owned by it, not allocated for the caller.
	 */

	if (!file_path && default_icon)
		file_path = get_default_image_path();

	return file_path;
}

/* This function returns the image picture for the file_name file */
RImage *get_rimage_from_file(WScreen *scr, const char *file_name, int max_size)
{
	RImage *image = NULL;

	if (!file_name)
		return NULL;

	image = RLoadImage(scr->rcontext, file_name, 0);
	if (!image)
		wwarning(_("error loading image file \"%s\": %s"), file_name,
			 RMessageForError(RErrorCode));

	image = wIconValidateIconSize(image, max_size);

	return image;
}

/* This function returns the default icon's full path
 * If the path for an icon is not found, returns NULL */
char *get_default_image_path(void)
{
	char *path = NULL, *file = NULL;

	/* Get the default icon */
	file = wDefaultGetIconFile(NULL, NULL, True);
	if (file)
		path = FindImage(w_global.config->icon_path, file);

	return path;
}

/* This function creates the RImage using the default icon */
RImage *get_default_image(WScreen *scr)
{
	RImage *image = NULL;
	char *path = NULL;

	/* Get the filename full path */
	path = get_default_image_path();
	if (!path)
		return NULL;

	/* Get the default icon */
	image = get_rimage_from_file(scr, path, w_global.config->icon_size);
	if (!image)
		wwarning(_("could not find default icon \"%s\""), path);

	wfree(path);

	/* Resize the icon to the w_global.config->icon_size size
	 * usually this function will return early, because size is right */
	image = wIconValidateIconSize(image, w_global.config->icon_size);

	return image;
}

RImage *get_icon_image(WScreen *scr, const char *winstance, const char *wclass, int max_size)
{
	char *file_name;
	RImage *image;

	/* Get the file name of the image, using instance and class */
	file_name = get_icon_filename(winstance, wclass, NULL, True);
	image = get_rimage_from_file(scr, file_name, max_size);
	wfree(file_name);

	return image;
}

int wDefaultGetStartWorkspace(WScreen *scr, const char *instance, const char *class)
{
	const WWindowAttrs *level[4];
	int n, i;

	n = attrs_lookup_levels(instance, class, True, level);
	for (i = 0; i < n; i++) {
		const char *ws = level[i]->start_workspace;
		if (ws && ws[0] != '\0')
			return wGetWorkspaceNumber(scr, ws);
	}
	return -1;
}

/* Get the name of the Icon File. If default_icon is True, then, default value included.
 * The returned pointer is owned by the attrs cache, do not free it and do
 * not keep it across a domain reload or a wDefaultChangeIcon() call. */
char *wDefaultGetIconFile(const char *instance, const char *class, Bool default_icon)
{
	const WWindowAttrs *level[4];
	int n, i;

	n = attrs_lookup_levels(instance, class, default_icon, level);
	for (i = 0; i < n; i++)
		if (level[i]->icon)
			return level[i]->icon;
	return NULL;
}

void wDefaultChangeIcon(const char *instance, const char *class, const char *file)
{
	WDDomain *db = w_global.domain.window_attr;
	WMPropList *icon_value = NULL, *value, *attr, *key, *def_win, *def_icon = NULL;
	WMPropList *dict = db->dictionary;
	int same = 0;

	init_wdefaults();

	if (!dict) {
		dict = WMCreatePLDictionary(NULL, NULL);
		if (dict)
			db->dictionary = dict;
		else
			return;
	}

	WMPLSetCaseSensitive(True);

	/* same empty-is-absent rule as attrs_lookup_levels() */
	if (instance && !*instance)
		instance = NULL;
	if (class && !*class)
		class = NULL;

	if (instance && class) {
		char *buffer;

		buffer = StrConcatDot(instance, class);
		key = WMCreatePLString(buffer);
		wfree(buffer);
	} else if (instance) {
		key = WMCreatePLString(instance);
	} else if (class) {
		key = WMCreatePLString(class);
	} else {
		key = WMRetainPropList(AnyWindow);
	}

	if (file) {
		value = WMCreatePLString(file);
		icon_value = WMCreatePLDictionary(AIcon, value, NULL);
		WMReleasePropList(value);

		def_win = WMGetFromPLDictionary(dict, AnyWindow);
		if (def_win != NULL)
			def_icon = WMGetFromPLDictionary(def_win, AIcon);

		if (def_icon && !strcmp(WMGetFromPLString(def_icon), file))
			same = 1;
	}

	attr = WMGetFromPLDictionary(dict, key);
	if (attr != NULL) {
		if (WMIsPLDictionary(attr)) {
			if (icon_value != NULL && !same)
				WMMergePLDictionaries(attr, icon_value, False);
			else
				WMRemoveFromPLDictionary(attr, AIcon);
		}
	} else if (icon_value != NULL && !same) {
		WMPutInPLDictionary(dict, key, icon_value);
	}

	if (!wPreferences.flags.noupdates)
		UpdateDomainFile(db);

	WMReleasePropList(key);
	if (icon_value)
		WMReleasePropList(icon_value);

	WMPLSetCaseSensitive(False);

	wWindowAttrsReload();
}

void wDefaultPurgeInfo(const char *instance, const char *class)
{
	WMPropList *value, *key, *dict;
	char *buffer;

	init_wdefaults();

	WMPLSetCaseSensitive(True);

	buffer = StrConcatDot(instance, class);
	key = WMCreatePLString(buffer);

	dict = WMGetFromPLDictionary(w_global.domain.window_attr->dictionary, key);

	if (dict) {
		value = WMGetFromPLDictionary(dict, AIcon);
		if (value) {
			WMRemoveFromPLDictionary(dict, AIcon);
		}
		WMRemoveFromPLDictionary(w_global.domain.window_attr->dictionary, key);
		UpdateDomainFile(w_global.domain.window_attr);
		wWindowAttrsReload();
	}

	wfree(buffer);
	WMReleasePropList(key);
	WMPLSetCaseSensitive(False);
}
