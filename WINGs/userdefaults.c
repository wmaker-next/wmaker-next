/*
 *  userdefaults.c
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 1998 scottc
 *  Copyright (c) 1998-2004 Dan Pascu
 *  Copyright (c) 1999 Alfredo K. Kojima
 *  Copyright (c) 2008-2026 Window Maker Team
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, see <https://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>

#include "wconfig.h"

#include "WINGs.h"
#include "WINGsP.h"
#include "userdefaults.h"


typedef struct W_UserDefaults {
	WMPropList *defaults;

	WMPropList *appDomain;

	WMPropList *searchListArray;
	WMPropList **searchList;	/* cache for searchListArray */

	char dirty;

	char dontSync;

	char *path;		/* where is db located */

	time_t timestamp;	/* last modification time */

	struct W_UserDefaults *next;

} UserDefaults;

static UserDefaults *sharedUserDefaults = NULL;

/* set by wusergnusteppath() so the data/cache/domain helpers know whether
 * to keep the legacy single-tree GNUstep layout or split across the XDG
 * config/data/cache basedirs */
static int xdg_layout = 0;

char *WMUserDefaultsDidChangeNotification = "WMUserDefaultsDidChangeNotification";

static void synchronizeUserDefaults(void *foo);

#ifndef HAVE_INOTIFY
/* Check defaults database for changes every this many milliseconds */
#define UD_SYNC_INTERVAL	2000
#endif

/*
 * Resolve the per-user configuration root. Order:
 *   1. $WMAKER_USER_ROOT (expanded), if set
 *   2. ~/GNUstep, if ~/GNUstep/Defaults already exists (legacy install)
 *   3. $XDG_CONFIG_HOME/WindowMaker, if XDG_CONFIG_HOME is set and absolute
 *   4. ~/.config/WindowMaker
 *
 * The result is cached for the process lifetime and exported back to the
 * environment as WMAKER_USER_ROOT (without overwriting an explicit value),
 * so wexpandpath("$WMAKER_USER_ROOT/...") and spawned children see the
 * resolved root regardless of which branch was taken. The XDG branches
 * additionally export WMAKER_USER_XDG=1 so children that inherit
 * WMAKER_USER_ROOT (branch 1) keep the XDG split layout instead of wrongly
 * falling back to the legacy Defaults/ subdirectory and Library/ data dir.
 * An explicit WMAKER_USER_ROOT without WMAKER_USER_XDG stays legacy.
 */
const char *wusergnusteppath(void)
{
	static char *path = NULL;
	struct stat st;
	const char *home, *xdg;
	char *gspath, *probe;

	if (path)
		/* Value have been already computed, re-use it */
		return path;

	gspath = GETENV("WMAKER_USER_ROOT");
	if (gspath) {
		gspath = wexpandpath(gspath);
		if (gspath) {
			path = gspath;
			if (GETENV("WMAKER_USER_XDG") != NULL)
				xdg_layout = 1;
			goto done;
		}
		wwarning(_("variable WMAKER_USER_ROOT defined with invalid path, not used"));
	}

	home = wgethomedir();
	if (!home)
		return NULL;

	/* legacy ~/GNUstep tree, keyed on the wmaker-specific Defaults dir */
	probe = wstrconcat(home, "/" GSUSER_SUBDIR "/" DEFAULTS_SUBDIR);
	if (stat(probe, &st) == 0 && S_ISDIR(st.st_mode)) {
		wfree(probe);
		path = wstrconcat(home, "/" GSUSER_SUBDIR);
		goto done;
	}
	wfree(probe);

	/* XDG basedir, spec requires the value to be absolute */
	xdg = GETENV("XDG_CONFIG_HOME");
	if (xdg && xdg[0] == '/')
		path = wstrconcat(xdg, "/" PACKAGE_TARNAME);
	else
		path = wstrconcat(home, "/.config/" PACKAGE_TARNAME);
	xdg_layout = 1;

done:
	setenv("WMAKER_USER_ROOT", path, 0);
	if (xdg_layout)
		setenv("WMAKER_USER_XDG", "1", 0);
	/* force the data and cache roots to be resolved and exported now
	 * so wexpandpath("$WMAKER_USER_DATA/...") works from the very
	 * first caller, the recursion terminates on the cache above */
	wuserdatapath();
	wusercachepath();
	return path;
}

/* ${var:-~/.fallback}/WindowMaker on the XDG branch, or the given legacy
 * subdirectory of the config root otherwise, exported as env for
 * wexpandpath() and spawned children. */
static char *xdg_or_legacy(const char *var, const char *fallback,
			   const char *legacy_sub, const char *env)
{
	const char *root, *dir;
	char *path;

	root = wusergnusteppath();
	if (!xdg_layout) {
		path = wstrconcat(root, legacy_sub);
	} else {
		dir = GETENV(var);
		if (dir && dir[0] == '/')
			path = wstrconcat(dir, "/" PACKAGE_TARNAME);
		else
			path = wstrconcat(wgethomedir(), fallback);
	}
	setenv(env, path, 0);
	return path;
}

const char *wuserdatapath(void)
{
	static char *path = NULL;

	if (path)
		/* Value have been already computed, re-use it */
		return path;

	path = xdg_or_legacy("XDG_DATA_HOME", "/.local/share/" PACKAGE_TARNAME,
			     "/" USERDATA_SUBDIR "/" PACKAGE_TARNAME,
			     "WMAKER_USER_DATA");
	return path;
}

const char *wusercachepath(void)
{
	static char *path = NULL;

	if (path)
		return path;

	path = xdg_or_legacy("XDG_CACHE_HOME", "/.cache/" PACKAGE_TARNAME,
			     "/" USERDATA_SUBDIR "/" PACKAGE_TARNAME,
			     "WMAKER_USER_CACHE");
	return path;
}

/*
 * A defaults domain is a plain name, never a path. Reject anything that
 * would let the caller escape the Defaults directory. The empty string is
 * accepted so callers can obtain the directory itself (used for inotify).
 */
static int domain_name_is_valid(const char *domain)
{
	const char *p;

	if (!domain)
		return 0;
	if (strcmp(domain, ".") == 0 || strcmp(domain, "..") == 0)
		return 0;
	for (p = domain; *p; p++)
		if (*p == '/' || *p == '\\')
			return 0;
	return 1;
}

char *wdefaultspathfordomain(const char *domain)
{
	char *path;
	const char *gspath, *sub;
	size_t slen;

	if (!domain_name_is_valid(domain)) {
		wwarning(_("invalid defaults domain name: %s"), domain ? domain : "(null)");
		return NULL;
	}

	gspath = wusergnusteppath();
	sub = xdg_layout ? "/" : "/" DEFAULTS_SUBDIR "/";
	slen = strlen(gspath) + strlen(sub) + strlen(domain) + 1;
	path = wmalloc(slen);

	snprintf(path, slen, "%s%s%s", gspath, sub, domain);

	return path;
}

/* XXX: doesn't quite belong to *user*defaults.c */
char *wglobaldefaultspathfordomain(const char *domain)
{
	if (!domain_name_is_valid(domain)) {
		wwarning(_("invalid defaults domain name: %s"), domain ? domain : "(null)");
		return NULL;
	}
	return wstrconcat(PKGCONFDIR "/", domain);
}

void w_save_defaults_changes(void)
{
	if (WMApplication.applicationName == NULL) {
		/*
		 * This means that the user has properly exited by calling the
		 * function 'WMReleaseApplication' (which has already called us)
		 * but we're being called again by the fallback 'atexit' method
		 * (the legacy way of saving changes on exit which is kept for
		 * application that would forget to call 'WMReleaseApplication')
		 */
		return;
	}

	/* save the user defaults databases */
	synchronizeUserDefaults(NULL);
}

/* set to save changes in defaults when program is exited */
static void registerSaveOnExit(void)
{
	static bool registeredSaveOnExit = false;

	if (!registeredSaveOnExit) {
		atexit(w_save_defaults_changes);
		registeredSaveOnExit = true;
	}
}

static void synchronizeUserDefaults(void *foo)
{
	UserDefaults *database = sharedUserDefaults;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;

	while (database) {
		if (!database->dontSync)
			WMSynchronizeUserDefaults(database);
		database = database->next;
	}
}

#ifndef HAVE_INOTIFY
static void addSynchronizeTimerHandler(void)
{
	static bool initialized = false;

	if (!initialized) {
		WMAddPersistentTimerHandler(UD_SYNC_INTERVAL,
		    synchronizeUserDefaults, NULL);
		initialized = true;
	}
}
#endif

void WMEnableUDPeriodicSynchronization(WMUserDefaults * database, bool enable)
{
	database->dontSync = !enable;
}

void WMSynchronizeUserDefaults(WMUserDefaults * database)
{
	bool fileIsNewer = false, release = false, notify = false;
	WMPropList *plF, *key;
	char *path;
	struct stat stbuf;

	if (!database->path) {
		path = wdefaultspathfordomain(WMGetApplicationName());
		release = true;
	} else {
		path = database->path;
	}

	if (stat(path, &stbuf) >= 0 && stbuf.st_mtime > database->timestamp)
		fileIsNewer = true;

	if (database->appDomain && (database->dirty || fileIsNewer)) {
		if (database->dirty && fileIsNewer) {
			plF = WMReadPropListFromFile(path);
			if (plF) {
				plF = WMMergePLDictionaries(plF, database->appDomain, false);
				WMReleasePropList(database->appDomain);
				database->appDomain = plF;
				key = database->searchList[0];
				WMPutInPLDictionary(database->defaults, key, plF);
				notify = true;
			} else {
				/* something happened with the file. just overwrite it */
				wwarning(_("cannot read domain from file '%s' when syncing"), path);
				WMWritePropListToFile(database->appDomain, path);
			}
		} else if (database->dirty) {
			WMWritePropListToFile(database->appDomain, path);
		} else if (fileIsNewer) {
			plF = WMReadPropListFromFile(path);
			if (plF) {
				WMReleasePropList(database->appDomain);
				database->appDomain = plF;
				key = database->searchList[0];
				WMPutInPLDictionary(database->defaults, key, plF);
				notify = true;
			} else {
				/* something happened with the file. just overwrite it */
				wwarning(_("cannot read domain from file '%s' when syncing"), path);
				WMWritePropListToFile(database->appDomain, path);
			}
		}

		database->dirty = 0;

		if (stat(path, &stbuf) >= 0)
			database->timestamp = stbuf.st_mtime;

		if (notify) {
			WMPostNotificationName(WMUserDefaultsDidChangeNotification, database, NULL);
		}
	}

	if (release)
		wfree(path);

}

void WMSaveUserDefaults(WMUserDefaults * database)
{
	if (database->appDomain) {
		struct stat stbuf;
		char *path;
		bool release = false;

		if (!database->path) {
			path = wdefaultspathfordomain(WMGetApplicationName());
			release = true;
		} else {
			path = database->path;
		}
		WMWritePropListToFile(database->appDomain, path);
		database->dirty = 0;
		if (stat(path, &stbuf) >= 0)
			database->timestamp = stbuf.st_mtime;
		if (release)
			wfree(path);
	}
}

void WMReloadUserDefaultsGlobalDomain(void)
{
	UserDefaults *database = sharedUserDefaults;
	WMPropList *key, *dom;
	char *path;

	while (database && database->path != NULL)
		database = database->next;
	if (!database)
		return;

	key = WMCreatePLString("WMGLOBAL");
	path = wdefaultspathfordomain("WMGLOBAL");
	dom = WMReadPropListFromFile(path);
	wfree(path);
	if (!dom)
		dom = WMCreatePLDictionary(NULL, NULL);

	WMPutInPLDictionary(database->defaults, key, dom);
	WMReleasePropList(dom);
	WMReleasePropList(key);
}

WMUserDefaults *WMGetStandardUserDefaults(void)
{
	WMUserDefaults *defaults;
	WMPropList *domain;
	WMPropList *key;
	struct stat stbuf;
	char *path;
	int i;

	if (sharedUserDefaults) {
		defaults = sharedUserDefaults;
		while (defaults) {
			/* path == NULL only for StandardUserDefaults db */
			if (defaults->path == NULL)
				return defaults;
			defaults = defaults->next;
		}
	}

	/* the database we are looking for is not loaded yet, read it now */
	defaults = wmalloc(sizeof(WMUserDefaults));
	defaults->defaults = WMCreatePLDictionary(NULL, NULL);
	defaults->searchList = wmalloc(sizeof(WMPropList *) * 3);

	/* application domain */
	key = WMCreatePLString(WMGetApplicationName());
	defaults->searchList[0] = key;

	/* temporary kluge. wmaker handles synchronization itself */
	if (strcmp(WMGetApplicationName(), "WindowMaker") == 0) {
		defaults->dontSync = 1;
	}

	path = wdefaultspathfordomain(WMGetFromPLString(key));

	if (stat(path, &stbuf) >= 0)
		defaults->timestamp = stbuf.st_mtime;

	domain = WMReadPropListFromFile(path);

	if (!domain)
		domain = WMCreatePLDictionary(NULL, NULL);

	wfree(path);

	defaults->appDomain = domain;

	if (domain)
		WMPutInPLDictionary(defaults->defaults, key, domain);

	/* global domain */
	key = WMCreatePLString("WMGLOBAL");
	defaults->searchList[1] = key;

	path = wdefaultspathfordomain(WMGetFromPLString(key));

	domain = WMReadPropListFromFile(path);

	wfree(path);

	if (!domain)
		domain = WMCreatePLDictionary(NULL, NULL);

	if (domain) {
		WMPutInPLDictionary(defaults->defaults, key, domain);
		WMReleasePropList(domain);
	}

	/* terminate list */
	defaults->searchList[2] = NULL;

	defaults->searchListArray = WMCreatePLArray(NULL, NULL);

	i = 0;
	while (defaults->searchList[i]) {
		WMAddToPLArray(defaults->searchListArray, defaults->searchList[i]);
		i++;
	}

	if (sharedUserDefaults)
		defaults->next = sharedUserDefaults;
	sharedUserDefaults = defaults;

#ifndef HAVE_INOTIFY
	addSynchronizeTimerHandler();
#endif
	registerSaveOnExit();

	return defaults;
}

WMUserDefaults *WMGetDefaultsFromPath(const char *path)
{
	WMUserDefaults *defaults;
	WMPropList *domain;
	WMPropList *key;
	struct stat stbuf;
	const char *name;
	int i;

	assert(path != NULL);

	if (sharedUserDefaults) {
		defaults = sharedUserDefaults;
		while (defaults) {
			if (defaults->path && strcmp(defaults->path, path) == 0)
				return defaults;
			defaults = defaults->next;
		}
	}

	/* the database we are looking for is not loaded yet, read it now */
	defaults = wmalloc(sizeof(WMUserDefaults));
	defaults->defaults = WMCreatePLDictionary(NULL, NULL);
	defaults->searchList = wmalloc(sizeof(WMPropList *) * 2);

	/* the domain we want, go in the first position */
	name = strrchr(path, '/');
	if (!name)
		name = path;
	else
		name++;

	key = WMCreatePLString(name);
	defaults->searchList[0] = key;

	if (stat(path, &stbuf) >= 0)
		defaults->timestamp = stbuf.st_mtime;

	domain = WMReadPropListFromFile(path);

	if (!domain)
		domain = WMCreatePLDictionary(NULL, NULL);

	defaults->path = wstrdup(path);

	defaults->appDomain = domain;

	if (domain)
		WMPutInPLDictionary(defaults->defaults, key, domain);

	/* terminate list */
	defaults->searchList[1] = NULL;

	defaults->searchListArray = WMCreatePLArray(NULL, NULL);

	i = 0;
	while (defaults->searchList[i]) {
		WMAddToPLArray(defaults->searchListArray, defaults->searchList[i]);
		i++;
	}

	if (sharedUserDefaults)
		defaults->next = sharedUserDefaults;
	sharedUserDefaults = defaults;

#ifndef HAVE_INOTIFY
	addSynchronizeTimerHandler();
#endif
	registerSaveOnExit();

	return defaults;
}

/* Returns a WMPropList array with all keys in the user defaults database.
 * Free the array with WMReleasePropList() when no longer needed,
 * but do not free the elements of the array! They're just references. */
WMPropList *WMGetUDKeys(WMUserDefaults * database)
{
	return WMGetPLDictionaryKeys(database->appDomain);
}

WMPropList *WMGetUDObjectForKey(WMUserDefaults * database, const char *defaultName)
{
	WMPropList *domainName, *domain;
	WMPropList *object = NULL;
	WMPropList *key = WMCreatePLString(defaultName);
	int i = 0;

	while (database->searchList[i] && !object) {
		domainName = database->searchList[i];
		domain = WMGetFromPLDictionary(database->defaults, domainName);
		if (domain) {
			object = WMGetFromPLDictionary(domain, key);
		}
		i++;
	}
	WMReleasePropList(key);

	return object;
}

void WMSetUDObjectForKey(WMUserDefaults * database, WMPropList * object, const char *defaultName)
{
	WMPropList *key = WMCreatePLString(defaultName);

	database->dirty = 1;

	WMPutInPLDictionary(database->appDomain, key, object);
	WMReleasePropList(key);
}

void WMRemoveUDObjectForKey(WMUserDefaults * database, const char *defaultName)
{
	WMPropList *key = WMCreatePLString(defaultName);

	database->dirty = 1;

	WMRemoveFromPLDictionary(database->appDomain, key);

	WMReleasePropList(key);
}

char *WMGetUDStringForKey(WMUserDefaults * database, const char *defaultName)
{
	WMPropList *val;

	val = WMGetUDObjectForKey(database, defaultName);

	if (!val)
		return NULL;

	if (!WMIsPLString(val))
		return NULL;

	return WMGetFromPLString(val);
}

int WMGetUDIntegerForKey(WMUserDefaults * database, const char *defaultName)
{
	WMPropList *val;
	char *str;
	int value;

	val = WMGetUDObjectForKey(database, defaultName);

	if (!val)
		return 0;

	if (!WMIsPLString(val))
		return 0;

	str = WMGetFromPLString(val);
	if (!str)
		return 0;

	if (sscanf(str, "%i", &value) != 1)
		return 0;

	return value;
}

float WMGetUDFloatForKey(WMUserDefaults * database, const char *defaultName)
{
	WMPropList *val;
	char *str;
	float value;

	val = WMGetUDObjectForKey(database, defaultName);

	if (!val || !WMIsPLString(val))
		return 0.0;

	if (!(str = WMGetFromPLString(val)))
		return 0.0;

	if (sscanf(str, "%f", &value) != 1)
		return 0.0;

	return value;
}

bool WMGetUDBoolForKey(WMUserDefaults * database, const char *defaultName)
{
	WMPropList *val;
	int value;
	char *str;

	val = WMGetUDObjectForKey(database, defaultName);

	if (!val)
		return false;

	if (!WMIsPLString(val))
		return false;

	str = WMGetFromPLString(val);
	if (!str)
		return false;

	if (sscanf(str, "%i", &value) == 1 && value != 0)
		return true;

	if (strcasecmp(str, "YES") == 0)
		return true;

	if (strcasecmp(str, "Y") == 0)
		return true;

	return false;
}

void WMSetUDIntegerForKey(WMUserDefaults * database, int value, const char *defaultName)
{
	WMPropList *object;
	char buffer[128];

	snprintf(buffer, sizeof(buffer), "%i", value);
	object = WMCreatePLString(buffer);

	WMSetUDObjectForKey(database, object, defaultName);
	WMReleasePropList(object);
}

void WMSetUDStringForKey(WMUserDefaults * database, const char *value, const char *defaultName)
{
	WMPropList *object;

	object = WMCreatePLString(value);

	WMSetUDObjectForKey(database, object, defaultName);
	WMReleasePropList(object);
}

void WMSetUDFloatForKey(WMUserDefaults * database, float value, const char *defaultName)
{
	WMPropList *object;
	char buffer[128];

	snprintf(buffer, sizeof(buffer), "%f", (double)value);
	object = WMCreatePLString(buffer);

	WMSetUDObjectForKey(database, object, defaultName);
	WMReleasePropList(object);
}

void WMSetUDBoolForKey(WMUserDefaults * database, bool value, const char *defaultName)
{
	static WMPropList *yes = NULL, *no = NULL;

	if (!yes) {
		yes = WMCreatePLString("YES");
		no = WMCreatePLString("NO");
	}

	WMSetUDObjectForKey(database, value ? yes : no, defaultName);
}

WMPropList *WMGetUDSearchList(WMUserDefaults * database)
{
	return database->searchListArray;
}

void WMSetUDSearchList(WMUserDefaults * database, WMPropList * list)
{
	int i, c;

	if (database->searchList) {
		i = 0;
		while (database->searchList[i]) {
			WMReleasePropList(database->searchList[i]);
			i++;
		}
		wfree(database->searchList);
	}
	if (database->searchListArray) {
		WMReleasePropList(database->searchListArray);
	}

	c = WMGetPropListItemCount(list);
	database->searchList = wmalloc(sizeof(WMPropList *) * (c + 1));

	for (i = 0; i < c; i++) {
		database->searchList[i] = WMGetFromPLArray(list, i);
	}
	database->searchList[c] = NULL;

	database->searchListArray = WMDeepCopyPropList(list);
}
