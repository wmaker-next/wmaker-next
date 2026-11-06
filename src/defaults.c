/* defaults.c - manage configuration through defaults db
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 1998-2003 Dan Pascu
 *  Copyright (c) 2008-2026 Window Maker Team
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
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <signal.h>

#ifndef PATH_MAX
#define PATH_MAX DEFAULT_PATH_MAX
#endif

#include <xkbcommon/xkbcommon-keysyms.h>

#include <wraster.h>

#include "WindowMaker.h"
#include "framewin.h"
#include "window.h"
#include "texture.h"
#include "screen.h"
#include "event.h"
#include "resources.h"
#include "defaults.h"
#include "config_schema.h"
#include "wconfigmodel.h"
#include "keybind.h"
#include "wxkb.h"
#include "accent_picker.h"
#include "icon.h"
#include "main.h"
#include "actions.h"
#include "dock.h"
#include "workspace.h"
#include "properties.h"
#include "misc.h"
#include "winmenu.h"
#include "rootmenu.h"
#include "osdview.h"
#include "xsettings.h"

typedef int (WConfigUpdateCallback)(WScreen *scr, void *tdata,
				    void *extra_data);

/* value setting functions */
static WConfigUpdateCallback setWidgetColor;
static WConfigUpdateCallback setIconTile;
static WConfigUpdateCallback setWinTitleFont;
static WConfigUpdateCallback setMenuTitleFont;
static WConfigUpdateCallback setMenuTextFont;
static WConfigUpdateCallback setIconTitleFont;
static WConfigUpdateCallback setIconTitleColor;
static WConfigUpdateCallback setIconTitleBack;
static WConfigUpdateCallback setFrameBorderColor;
static WConfigUpdateCallback setFrameFocusedBorderColor;
static WConfigUpdateCallback setFrameSelectedBorderColor;
static WConfigUpdateCallback setFrameInnerBorderColor;
static WConfigUpdateCallback setFrameInnerFocusedBorderColor;
static WConfigUpdateCallback setLargeDisplayFont;
static WConfigUpdateCallback setWTitleColor;
static WConfigUpdateCallback setFTitleBack;
static WConfigUpdateCallback setPTitleBack;
static WConfigUpdateCallback setUTitleBack;
static WConfigUpdateCallback setTabInactiveTitleBack;
static WConfigUpdateCallback setResizebarBack;
static WConfigUpdateCallback setWorkspaceBack;
static time_t workspace_back_applied;
static WConfigUpdateCallback setWorkspaceSpecificBack;
static WConfigUpdateCallback setMenuTitleColor;
static WConfigUpdateCallback setMenuTextColor;
static WConfigUpdateCallback setMenuDisabledColor;
static WConfigUpdateCallback setMenuTitleBack;
static WConfigUpdateCallback setMenuTextBack;
static WConfigUpdateCallback setHightlight;
static WConfigUpdateCallback setHightlightText;
static WConfigUpdateCallback setWorkspaceMapBackground;

static WConfigUpdateCallback setClipTitleFont;
static WConfigUpdateCallback setClipTitleColor;

static WConfigUpdateCallback setCursor;

#define REFRESH_WINDOW_TEXTURES WCFG_REFRESH_WINDOW_TEXTURES
#define REFRESH_MENU_TEXTURE WCFG_REFRESH_MENU_TEXTURE
#define REFRESH_MENU_FONT WCFG_REFRESH_MENU_FONT
#define REFRESH_MENU_COLOR WCFG_REFRESH_MENU_COLOR
#define REFRESH_MENU_TITLE_TEXTURE WCFG_REFRESH_MENU_TITLE_TEXTURE
#define REFRESH_MENU_TITLE_FONT WCFG_REFRESH_MENU_TITLE_FONT
#define REFRESH_MENU_TITLE_COLOR WCFG_REFRESH_MENU_TITLE_COLOR
#define REFRESH_WINDOW_TITLE_COLOR WCFG_REFRESH_WINDOW_TITLE_COLOR
#define REFRESH_WINDOW_FONT WCFG_REFRESH_WINDOW_FONT
#define REFRESH_ICON_TILE WCFG_REFRESH_ICON_TILE
#define REFRESH_ICON_FONT WCFG_REFRESH_ICON_FONT
#define REFRESH_BUTTON_IMAGES WCFG_REFRESH_BUTTON_IMAGES
#define REFRESH_ICON_TITLE_COLOR WCFG_REFRESH_ICON_TITLE_COLOR
#define REFRESH_ICON_TITLE_BACK WCFG_REFRESH_ICON_TITLE_BACK
#define REFRESH_WORKSPACE_MENU WCFG_REFRESH_WORKSPACE_MENU
#define REFRESH_FRAME_BORDER WCFG_REFRESH_FRAME_BORDER
#define REFRESH_INNER_BORDER WCFG_REFRESH_INNER_BORDER
#define REFRESH_SYSTEM_COLOR WCFG_REFRESH_SYSTEM_COLOR

/* stat() the user file behind a domain and tell whether it changed since
 * the last successful read, comparing the POSIX-2008 st_mtim so two
 * writes inside the same wall-clock second are not coalesced. The
 * inotify path makes this matter: four wdwrite calls in a row fire
 * four reload checks, with second-granularity mtime only the first
 * compared newer and the rest were silently dropped */
static bool domain_file_changed(WDDomain *db)
{
	struct stat st;

	if (stat(db->path, &st) < 0)
		return false;
	if (st.st_mtim.tv_sec < db->timestamp.tv_sec)
		return false;
	if (st.st_mtim.tv_sec == db->timestamp.tv_sec &&
	    st.st_mtim.tv_nsec <= db->timestamp.tv_nsec)
		return false;
	db->timestamp = st.st_mtim;
	return true;
}

static WMPropList *readGlobalDomain(const char *domainName, bool requireDictionary)
{
	WMPropList *globalDict = NULL;
	char path[PATH_MAX];
	struct stat stbuf;

	snprintf(path, sizeof(path), "%s/%s", PKGCONFDIR, domainName);
	if (stat(path, &stbuf) >= 0) {
		globalDict = WMReadPropListFromFile(path);
		if (globalDict && requireDictionary && !WMIsPLDictionary(globalDict)) {
			wwarning(_("Domain %s (%s) of global defaults database is corrupted!"), domainName, path);
			WMReleasePropList(globalDict);
			globalDict = NULL;
		} else if (!globalDict) {
			wwarning(_("could not load domain %s from global defaults database"), domainName);
		}
	}

	return globalDict;
}

#if defined(GLOBAL_PREAMBLE_MENU_FILE) || defined(GLOBAL_EPILOGUE_MENU_FILE)
static void prependMenu(WMPropList * destarr, WMPropList * array)
{
	WMPropList *item;
	int i;

	for (i = 0; i < WMGetPropListItemCount(array); i++) {
		item = WMGetFromPLArray(array, i);
		if (item)
			WMInsertInPLArray(destarr, i + 1, item);
	}
}

static void appendMenu(WMPropList * destarr, WMPropList * array)
{
	WMPropList *item;
	int i;

	for (i = 0; i < WMGetPropListItemCount(array); i++) {
		item = WMGetFromPLArray(array, i);
		if (item)
			WMAddToPLArray(destarr, item);
	}
}
#endif

void wDefaultsMergeGlobalMenus(WDDomain * menuDomain)
{
	WMPropList *menu = menuDomain->dictionary;
	WMPropList *submenu;

	if (!menu || !WMIsPLArray(menu))
		return;

#ifdef GLOBAL_PREAMBLE_MENU_FILE
	submenu = WMReadPropListFromFile(PKGCONFDIR "/" GLOBAL_PREAMBLE_MENU_FILE);

	if (submenu && !WMIsPLArray(submenu)) {
		wwarning(_("invalid global menu file %s"), GLOBAL_PREAMBLE_MENU_FILE);
		WMReleasePropList(submenu);
		submenu = NULL;
	}
	if (submenu) {
		prependMenu(menu, submenu);
		WMReleasePropList(submenu);
	}
#endif

#ifdef GLOBAL_EPILOGUE_MENU_FILE
	submenu = WMReadPropListFromFile(PKGCONFDIR "/" GLOBAL_EPILOGUE_MENU_FILE);

	if (submenu && !WMIsPLArray(submenu)) {
		wwarning(_("invalid global menu file %s"), GLOBAL_EPILOGUE_MENU_FILE);
		WMReleasePropList(submenu);
		submenu = NULL;
	}
	if (submenu) {
		appendMenu(menu, submenu);
		WMReleasePropList(submenu);
	}
#endif

	menuDomain->dictionary = menu;
}

WDDomain *wDefaultsInitDomain(const char *domain, bool requireDictionary)
{
	WDDomain *db;
	static int inited = 0;
	WMPropList *shared_dict = NULL;

	if (!inited) {
		inited = 1;
		WMPLSetCaseSensitive(false);
	}

	db = wmalloc(sizeof(WDDomain));
	db->domain_name = domain;
	db->path = wdefaultspathfordomain(domain);

	if (domain_file_changed(db)) {
		db->dictionary = WMReadPropListFromFile(db->path);
		if (db->dictionary) {
			if (requireDictionary && !WMIsPLDictionary(db->dictionary)) {
				WMReleasePropList(db->dictionary);
				db->dictionary = NULL;
				wwarning(_("Domain %s (%s) of defaults database is corrupted!"), domain, db->path);
			}
		} else {
			wwarning(_("could not load domain %s from user defaults database"), domain);
		}
	}

	/* global system dictionary */
	shared_dict = readGlobalDomain(domain, requireDictionary);

	if (shared_dict && db->dictionary && WMIsPLDictionary(shared_dict) &&
	    WMIsPLDictionary(db->dictionary)) {
		WMMergePLDictionaries(shared_dict, db->dictionary, true);
		WMReleasePropList(db->dictionary);
		db->dictionary = shared_dict;
	} else if (!db->dictionary) {
		db->dictionary = shared_dict;
	} else if (shared_dict) {
		/* both user and global exist but are not both dictionaries
		 * (e.g. WMRootMenu arrays), keep the user one */
		WMReleasePropList(shared_dict);
	}

	return db;
}

/* Read the user file for a dictionary domain, merge the global copy on
 * top if present, validate the type and emit the standard warnings.
 * Returns the merged dictionary (owned by the caller) or NULL when the
 * file is missing or not a dictionary. */
static WMPropList *reload_domain_dict(WDDomain *domain, const char *name)
{
	WMPropList *shared = readGlobalDomain(name, true);
	WMPropList *dict = WMReadPropListFromFile(domain->path);

	if (!dict) {
		wwarning(_("could not load domain %s from user defaults database"), name);
		if (shared)
			WMReleasePropList(shared);
		return NULL;
	}
	if (!WMIsPLDictionary(dict)) {
		wwarning(_("Domain %s (%s) of defaults database is corrupted!"),
			 name, domain->path);
		WMReleasePropList(dict);
		if (shared)
			WMReleasePropList(shared);
		return NULL;
	}
	if (shared) {
		WMMergePLDictionaries(shared, dict, true);
		WMReleasePropList(dict);
		dict = shared;
	}
	return dict;
}

void wDefaultsCheckDomains(void* arg)
{
	WScreen *scr;
	WMPropList *dict;
	int i;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) arg;

	if (domain_file_changed(w_global.domain.wmaker)) {
		dict = reload_domain_dict(w_global.domain.wmaker, "WindowMaker");
		if (dict) {
			WConfig *old_config;
			WConfig *new_config;
			WConfigChanges changes;

			old_config = w_global.config;
			new_config = wConfigReadWindowMaker(dict);

			wConfigPrepareRuntimeDefaults(dict, old_config, new_config, &changes);
			/* adopt before applying so handlers reached
			 * from wReadDefaults() read the new values
			 * via w_global.config */
			w_global.config = new_config;
			wdisp->config = new_config;
			for (i = 0; i < wdisp->screen_count; i++) {
				scr = wScreenWithNumber(i);
				if (scr)
					wReadDefaults(scr, old_config, new_config, &changes);
			}
			wConfigDestroy(old_config);

			if (w_global.domain.wmaker->dictionary)
				WMReleasePropList(w_global.domain.wmaker->dictionary);

			w_global.domain.wmaker->dictionary = dict;
		}
	}

	if (domain_file_changed(w_global.domain.window_attr)) {
		/* WMWindowAttributes outer keys are X11 instance.class strings
		 * and are case-sensitive, keep "xterm" and "XTerm" distinct
		 * while reading and merging the global and user files */
		WMPLSetCaseSensitive(true);
		dict = reload_domain_dict(w_global.domain.window_attr, "WMWindowAttributes");
		if (dict) {
			if (w_global.domain.window_attr->dictionary)
				WMReleasePropList(w_global.domain.window_attr->dictionary);

			w_global.domain.window_attr->dictionary = dict;
			wWindowAttrsReload();
			for (i = 0; i < wdisp->screen_count; i++) {
				scr = wScreenWithNumber(i);
				if (scr) {
					wDefaultUpdateIcons(scr);

					/* Update the panel image if changed */
					/* Don't worry. If the image is the same these
					 * functions will have no performance impact. */
					create_logo_image(scr);
				}
			}
		}
		WMPLSetCaseSensitive(false);
	}

	if (domain_file_changed(w_global.domain.root_menu)) {
		dict = WMReadPropListFromFile(w_global.domain.root_menu->path);
		if (dict) {
			if (!WMIsPLArray(dict) && !WMIsPLString(dict)) {
				WMReleasePropList(dict);
				dict = NULL;
				wwarning(_("Domain %s (%s) of defaults database is corrupted!"),
					 "WMRootMenu", w_global.domain.root_menu->path);
			} else {
				if (w_global.domain.root_menu->dictionary)
					WMReleasePropList(w_global.domain.root_menu->dictionary);

				w_global.domain.root_menu->dictionary = dict;
				wDefaultsMergeGlobalMenus(w_global.domain.root_menu);
			}
		} else {
			wwarning(_("could not load domain %s from user defaults database"), "WMRootMenu");
		}

		/* Rebuild the root menu (without mapping) so that shortcuts take effect immediately. */
		for (i = 0; i < wdisp->screen_count; i++) {
			WScreen *s = wScreenWithNumber(i);
			if (s)
				wRootMenuReparse(s);
		}
	}

	if (w_global.domain.wmglobal && domain_file_changed(w_global.domain.wmglobal)) {
		/* WMGLOBAL holds WINGs-global options (AntialiasedText, ...).
		 * Re-read them, then run the per-screen apply with an empty
		 * change set: wConfigApplyRuntime no-ops for WindowMaker keys
		 * but its unconditional wConfigApplyAntialiasedText() detects
		 * the AA change, detaches the WINGs font cache and re-opens the
		 * WM-side fonts, returning the font refresh mask that
		 * wReadDefaults posts as appearance notifications. */
		WMReloadConfiguration();
		for (i = 0; i < wdisp->screen_count; i++) {
			scr = wScreenWithNumber(i);
			if (scr) {
				WConfigChanges no_changes;

				memset(&no_changes, 0, sizeof(no_changes));
				wReadDefaults(scr, w_global.config, w_global.config, &no_changes);
			}
		}
	}
#ifndef HAVE_INOTIFY
	if (!arg)
		WMAddTimerHandler(DEFAULTS_CHECK_INTERVAL, wDefaultsCheckDomains, arg);
#endif
}

void wReadDefaults(WScreen *scr, const WConfig *old_config,
		   const WConfig *new_config, const WConfigChanges *changes)
{
	unsigned int needs_refresh;

	needs_refresh = wConfigApplyRuntime(scr, old_config, new_config, changes);
#ifdef KEEP_XKB_LOCK_STATUS
	wAccentPickerApplyConfig(scr);
#endif

	if (needs_refresh != 0 && !scr->flags.startup) {
		int foo;

		foo = 0;
		if (needs_refresh & REFRESH_MENU_TITLE_TEXTURE)
			foo |= WTextureSettings;
		if (needs_refresh & REFRESH_MENU_TITLE_FONT)
			foo |= WFontSettings;
		if (needs_refresh & (REFRESH_MENU_TITLE_COLOR | REFRESH_SYSTEM_COLOR))
			foo |= WColorSettings;
		if (foo)
			WMPostNotificationName(WNMenuTitleAppearanceSettingsChanged, NULL,
					       (void *)(uintptr_t) foo);

		foo = 0;
		if (needs_refresh & REFRESH_MENU_TEXTURE)
			foo |= WTextureSettings;
		if (needs_refresh & REFRESH_MENU_FONT)
			foo |= WFontSettings;
		if (needs_refresh & (REFRESH_MENU_COLOR | REFRESH_SYSTEM_COLOR))
			foo |= WColorSettings;
		if (foo)
			WMPostNotificationName(WNMenuAppearanceSettingsChanged, NULL, (void *)(uintptr_t) foo);

		foo = 0;
		if (needs_refresh & REFRESH_WINDOW_FONT)
			foo |= WFontSettings;
		if (needs_refresh & REFRESH_WINDOW_TEXTURES)
			foo |= WTextureSettings;
		if (needs_refresh & (REFRESH_WINDOW_TITLE_COLOR | REFRESH_SYSTEM_COLOR))
			foo |= WColorSettings;
		if (foo)
			WMPostNotificationName(WNWindowAppearanceSettingsChanged, NULL, (void *)(uintptr_t) foo);

		if (!(needs_refresh & REFRESH_ICON_TILE)) {
			foo = 0;
			if (needs_refresh & REFRESH_ICON_FONT)
				foo |= WFontSettings;
			if (needs_refresh & REFRESH_ICON_TITLE_COLOR)
				foo |= WTextureSettings;
			if (needs_refresh & REFRESH_ICON_TITLE_BACK)
				foo |= WTextureSettings;
			if (foo)
				WMPostNotificationName(WNIconAppearanceSettingsChanged, NULL,
						       (void *)(uintptr_t) foo);
		}
		if (needs_refresh & REFRESH_ICON_TILE)
			WMPostNotificationName(WNIconTileSettingsChanged, NULL, NULL);

		if (needs_refresh & REFRESH_WORKSPACE_MENU) {
			if (scr->workspace_menu)
				wWorkspaceMenuUpdate(scr, scr->workspace_menu);
			if (scr->clip_ws_menu)
				wWorkspaceMenuUpdate(scr, scr->clip_ws_menu);
			if (scr->workspace_submenu)
				scr->workspace_submenu->flags.realized = 0;
			if (scr->clip_submenu)
				scr->clip_submenu->flags.realized = 0;
		}
		if (needs_refresh & REFRESH_INNER_BORDER) {
			WWindow *wwin = scr->focused_window;

			while (wwin) {
				wWindowApplyInnerBorder(wwin);
				wwin = wwin->prev;
			}
			wXFlush(wdisp->conn);
		}
	}

	/* XSETTINGS follows the WM font and rendering configuration, the
	 * refresh no-ops while the screen holds no XSETTINGS selection */
	wXSettingsRefresh(scr);
}

void wReadKeybindings(WScreen *scr, WMPropList *dict, bool *batch_changed)
{
	const WConfig *config;
	WConfig *owned_config = NULL;
	const WConfigEntry *schema;
	size_t i, count;
	bool changed = false;

	config = w_global.config;
	if (!config) {
		owned_config = wConfigReadWindowMaker(dict);
		config = owned_config;
	}

	schema = wConfigSchema();
	count = wConfigSchemaCount();
	for (i = 0; i < count; i++) {
		const WConfigKeyBinding *binding;
		bool binding_changed = false;

		if (schema[i].type != WCFG_KEY_BINDING)
			continue;

		binding = (const WConfigKeyBinding *)
		    ((const char *)config + schema[i].offset);
		(void)wConfigApplyKeyBinding(scr, schema[i].key, schema[i].min_value,
					     binding->spec, schema[i].default_value,
					     &binding_changed);
		changed = changed || binding_changed;
	}

	/* Only the first caller sees a difference, so share the result. */
	if (batch_changed) {
		*batch_changed = *batch_changed || changed;
		changed = *batch_changed;
	}

	if (changed) {
		wRegrabFocusedWindowKeys(scr);
		wKeyTreeRebuild(scr);
	}

	wConfigDestroy(owned_config);
}

void wDefaultUpdateIcons(WScreen *scr)
{
	WAppIcon *aicon = scr->app_icon_list;
	WDrawerChain *dc;
	WWindow *wwin = scr->focused_window;

	while (aicon) {
		/* Get the application icon, default included */
		wIconChangeImageFile(aicon->icon, NULL);
		wAppIconPaint(aicon);
		aicon = aicon->next;
	}

	if (!wdisp->flags.noclip || wdisp->flags.clip_merged_in_dock)
		wClipIconPaint(scr->clip_icon);

	for (dc = scr->drawers; dc != NULL; dc = dc->next)
		wDrawerIconPaint(dc->adrawer->icon_array[0]);

	while (wwin) {
		if (wwin->icon && wwin->flags.miniaturized)
			wIconChangeImageFile(wwin->icon, NULL);
		wwin = wwin->prev;
	}
}

/* Rebuild the global key-binding trie from scratch */
void wKeyTreeRebuild(WScreen *scr)
{
	int i;

	/* w_global.shortcut.curpos points into the old trie, so an active
	 * chain must be torn down before wKeyTreeDestroy() frees the nodes
	 * it references. wCancelKeyChain() also hides any visible prefix
	 * OSD and ungrab the keyboard. */
	if (w_global.shortcut.curpos != NULL) {
		for (i = 0; i < wdisp->screen_count; i++)
			wCancelKeyChain(wScreenWithNumber(i));
	}

	w_global.shortcut.action_generation++;

	/* Parameter not used */
	(void)scr;

	wKeyTreeDestroy(wKeyTreeRoot);
	wKeyTreeRoot = NULL;

	/* Insert all wKeyBindings[] entries */
	for (i = 0; i < WKBD_LAST; i++) {
		WShortKey *k = &wKeyBindings[i];
		WKeyAction *act;
		xcb_keycode_t *keys;
		WKeyNode *leaf;
		int len, j;
		unsigned int *mods;

		/* WKBD_KEYCHAIN_CANCEL is only meaningful while inside an active key chain */
		if (i == WKBD_KEYCHAIN_CANCEL)
			continue;

		if (k->keycode == 0)
			continue;

		len  = (k->chain_length > 1) ? k->chain_length : 1;
		mods = wmalloc(len * sizeof(unsigned int));
		keys = wmalloc(len * sizeof(xcb_keycode_t));
		mods[0] = k->modifier;
		keys[0] = k->keycode;

		for (j = 1; j < len; j++) {
			mods[j] = k->chain_modifiers[j - 1];
			keys[j] = k->chain_keycodes[j - 1];
		}

		leaf = wKeyTreeInsert(&wKeyTreeRoot, mods, keys, len);
		wfree(mods);
		wfree(keys);

		act = wKeyNodeAddAction(leaf, WKN_WKBD);
		if (act)
			act->u.wkbd_idx = i;
	}

	/* Insert root-menu shortcuts */
	wRootMenuInsertIntoTree();
}

/* --------------------------- Local ----------------------- */

/*
 * (solid <color>)
 * (hgradient <color> <color>)
 * (vgradient <color> <color>)
 * (dgradient <color> <color>)
 * (mhgradient <color> <color> ...)
 * (mvgradient <color> <color> ...)
 * (mdgradient <color> <color> ...)
 * (igradient <color1> <color1> <thickness1> <color2> <color2> <thickness2>)
 * (tpixmap <file> <color>)
 * (spixmap <file> <color>)
 * (cpixmap <file> <color>)
 * (thgradient <file> <opaqueness> <color> <color>)
 * (tvgradient <file> <opaqueness> <color> <color>)
 * (tdgradient <file> <opaqueness> <color> <color>)
 * (function <lib> <function> ...)
 */

static bool pl_array_xcolor(WScreen *scr, WMPropList *pl, int idx, WXColor *out)
{
	WMPropList *elem;
	const char *val;

	elem = WMGetFromPLArray(pl, idx);
	if (!elem || !WMIsPLString(elem))
		return false;
	val = WMGetFromPLString(elem);

	if (!wParseColor(scr->w_colormap, val, out)) {
		wwarning(_("\"%s\" is not a valid color name"), val);
		return false;
	}
	return true;
}

static bool pl_array_rcolor(WScreen *scr, WMPropList *pl, int idx, RColor *out)
{
	WXColor xcolor;

	if (!pl_array_xcolor(scr, pl, idx, &xcolor))
		return false;
	out->alpha = 255;
	out->red = xcolor.red >> 8;
	out->green = xcolor.green >> 8;
	out->blue = xcolor.blue >> 8;
	return true;
}

static WTexture *parse_texture(WScreen *scr, WMPropList *pl)
{
	WMPropList *elem;
	char *val;
	int nelem;
	WTexture *texture = NULL;

	nelem = WMGetPropListItemCount(pl);
	if (nelem < 1)
		return NULL;

	elem = WMGetFromPLArray(pl, 0);
	if (!elem || !WMIsPLString(elem))
		return NULL;
	val = WMGetFromPLString(elem);

	if (strcasecmp(val, "solid") == 0) {
		WXColor color;

		if (nelem != 2)
			return NULL;

		if (!pl_array_xcolor(scr, pl, 1, &color))
			return NULL;

		texture = (WTexture *) wTextureMakeSolid(scr, &color);
	} else if (strcasecmp(val, "dgradient") == 0
		   || strcasecmp(val, "vgradient") == 0 || strcasecmp(val, "hgradient") == 0) {
		RColor color1, color2;
		int type;

		if (nelem != 3) {
			wwarning(_("bad number of arguments in gradient specification"));
			return NULL;
		}

		if (val[0] == 'd' || val[0] == 'D')
			type = WTEX_DGRADIENT;
		else if (val[0] == 'h' || val[0] == 'H')
			type = WTEX_HGRADIENT;
		else
			type = WTEX_VGRADIENT;

		if (!pl_array_rcolor(scr, pl, 1, &color1) ||
		    !pl_array_rcolor(scr, pl, 2, &color2))
			return NULL;

		texture = (WTexture *) wTextureMakeGradient(scr, type, &color1, &color2);

	} else if (strcasecmp(val, "igradient") == 0) {
		RColor colors1[2], colors2[2];
		int th1, th2;
		int i;

		if (nelem != 7) {
			wwarning(_("bad number of arguments in gradient specification"));
			return NULL;
		}

		for (i = 0; i < 2; i++) {
			if (!pl_array_rcolor(scr, pl, 1 + i, &colors1[i]))
				return NULL;
		}
		elem = WMGetFromPLArray(pl, 3);
		if (!elem || !WMIsPLString(elem))
			return NULL;
		val = WMGetFromPLString(elem);
		th1 = atoi(val);
		if (th1 < 0)
			th1 = 0;

		for (i = 0; i < 2; i++) {
			if (!pl_array_rcolor(scr, pl, 4 + i, &colors2[i]))
				return NULL;
		}
		elem = WMGetFromPLArray(pl, 6);
		if (!elem || !WMIsPLString(elem))
			return NULL;
		val = WMGetFromPLString(elem);
		th2 = atoi(val);
		if (th2 < 0)
			th2 = 0;

		texture = (WTexture *) wTextureMakeIGradient(scr, th1, colors1, th2, colors2);

	} else if (strcasecmp(val, "mhgradient") == 0
		   || strcasecmp(val, "mvgradient") == 0 || strcasecmp(val, "mdgradient") == 0) {
		WXColor color;
		RColor **colors;
		int i, count;
		int type;

		if (nelem < 3) {
			wwarning(_("too few arguments in multicolor gradient specification"));
			return NULL;
		}

		if (val[1] == 'h' || val[1] == 'H')
			type = WTEX_MHGRADIENT;
		else if (val[1] == 'v' || val[1] == 'V')
			type = WTEX_MVGRADIENT;
		else
			type = WTEX_MDGRADIENT;

		count = nelem - 1;

		colors = wmalloc(sizeof(RColor *) * (count + 1));

		for (i = 0; i < count; i++) {
			if (!pl_array_xcolor(scr, pl, i + 1, &color)) {
				for (--i; i >= 0; --i)
					wfree(colors[i]);
				wfree(colors);
				return NULL;
			}
			colors[i] = wmalloc(sizeof(RColor));
			colors[i]->red = color.red >> 8;
			colors[i]->green = color.green >> 8;
			colors[i]->blue = color.blue >> 8;
		}
		colors[i] = NULL;

		texture = (WTexture *) wTextureMakeMGradient(scr, type, colors);
	} else if (strcasecmp(val, "spixmap") == 0 ||
		   strcasecmp(val, "cpixmap") == 0 || strcasecmp(val, "tpixmap") == 0) {
		WXColor color;
		int type;

		if (nelem != 3)
			return NULL;

		if (val[0] == 's' || val[0] == 'S')
			type = WTP_SCALE;
		else if (val[0] == 'c' || val[0] == 'C')
			type = WTP_CENTER;
		else
			type = WTP_TILE;

		if (!pl_array_xcolor(scr, pl, 2, &color))
			return NULL;

		/* file name */
		elem = WMGetFromPLArray(pl, 1);
		if (!elem || !WMIsPLString(elem))
			return NULL;
		val = WMGetFromPLString(elem);

		texture = (WTexture *) wTextureMakePixmap(scr, type, val, &color);
	} else if (strcasecmp(val, "thgradient") == 0
		   || strcasecmp(val, "tvgradient") == 0 || strcasecmp(val, "tdgradient") == 0) {
		RColor color1, color2;
		int opacity;
		int style;

		if (val[1] == 'h' || val[1] == 'H')
			style = WTEX_THGRADIENT;
		else if (val[1] == 'v' || val[1] == 'V')
			style = WTEX_TVGRADIENT;
		else
			style = WTEX_TDGRADIENT;

		if (nelem != 5) {
			wwarning(_("bad number of arguments in textured gradient specification"));
			return NULL;
		}

		if (!pl_array_rcolor(scr, pl, 3, &color1) ||
		    !pl_array_rcolor(scr, pl, 4, &color2))
			return NULL;

		/* get opacity */
		elem = WMGetFromPLArray(pl, 2);
		if (!elem || !WMIsPLString(elem)) {
			opacity = 128;
			val = NULL;
		} else
			val = WMGetFromPLString(elem);

		if (!val || (opacity = atoi(val)) < 0 || opacity > 255) {
			wwarning(_("bad opacity value for tgradient texture \"%s\". Should be [0..255]"), val);
			opacity = 128;
		}

		/* get file name */
		elem = WMGetFromPLArray(pl, 1);
		if (!elem || !WMIsPLString(elem))
			return NULL;
		val = WMGetFromPLString(elem);

		texture = (WTexture *) wTextureMakeTGradient(scr, style, &color1, &color2, val, opacity);
	} else if (strcasecmp(val, "function") == 0) {
		/* Leave this in to handle the unlikely case of
		 * someone actually having function textures configured */
		wwarning("function texture support has been removed");
		return NULL;
	} else {
		wwarning(_("invalid texture type %s"), val);
		return NULL;
	}
	return texture;
}

static bool parseOneKey(const char *key, const char *token,
			unsigned int *out_mod, xcb_keycode_t *out_code)
{
	char tmp[MAX_SHORTCUT_LENGTH];
	char *b, *k;
	xkb_keysym_t ksym;

	wstrlcpy(tmp, token, MAX_SHORTCUT_LENGTH);
	b = tmp;

	*out_mod = 0;
	while ((k = strchr(b, '+')) != NULL) {
		int mod;
		*k = 0;
		mod = wXModifierFromKey(b);
		if (mod < 0) {
			wwarning(_("%s: invalid key modifier \"%s\""), key, b);
			return false;
		}
		*out_mod |= mod;
		b = k + 1;
	}

	ksym = wXkbKeysymFromName(b);
	if (ksym == XKB_KEY_NoSymbol) {
		wwarning(_("%s: invalid kbd shortcut specification \"%s\""), key, token);
		return false;
	}

	*out_code = wXkbKeysymToKeycode(ksym);
	if (*out_code == 0) {
		wwarning(_("%s: invalid key in shortcut \"%s\""), key, token);
		return false;
	}

	return true;
}

static bool parse_shortcut_spec(const char *key, const char *spec,
				WShortKey *shortcut)
{
	char buf[MAX_SHORTCUT_LENGTH];
	char *token, *saveptr;
	int step;

	memset(shortcut, 0, sizeof(*shortcut));
	if (!spec || strcasecmp(spec, "NONE") == 0) {
		shortcut->chain_length = 1;
		return true;
	}

	if (strlen(spec) >= sizeof(buf)) {
		wwarning(_("%s: kbd shortcut specification is too long"), key);
		return false;
	}
	wstrlcpy(buf, spec, sizeof(buf));

	step = 0;
	token = strtok_r(buf, " ", &saveptr);
	while (token != NULL) {
		unsigned int mod;
		xcb_keycode_t kcode;

		if (!parseOneKey(key, token, &mod, &kcode)) {
			wShortKeyFree(shortcut);
			return false;
		}

		if (step == 0) {
			shortcut->modifier = mod;
			shortcut->keycode = kcode;
		} else {
			shortcut->chain_modifiers = wrealloc(shortcut->chain_modifiers,
							    step * sizeof(unsigned int));
			shortcut->chain_keycodes = wrealloc(shortcut->chain_keycodes,
							  step * sizeof(xcb_keycode_t));
			shortcut->chain_modifiers[step - 1] = mod;
			shortcut->chain_keycodes[step - 1] = kcode;
		}
		step++;
		token = strtok_r(NULL, " ", &saveptr);
	}

	shortcut->chain_length = (step > 1) ? step : 1;
	return true;
}

typedef struct {
	const char *name;
} WCursorLookup;


static const WCursorLookup cursor_table[] = {
	{"X_cursor"},
	{"arrow"},
	{"based_arrow_down"},
	{"based_arrow_up"},
	{"boat"},
	{"bogosity"},
	{"bottom_left_corner"},
	{"bottom_right_corner"},
	{"bottom_side"},
	{"bottom_tee"},
	{"box_spiral"},
	{"center_ptr"},
	{"circle"},
	{"clock"},
	{"coffee_mug"},
	{"cross"},
	{"cross_reverse"},
	{"crosshair"},
	{"diamond_cross"},
	{"dot"},
	{"dotbox"},
	{"double_arrow"},
	{"draft_large"},
	{"draft_small"},
	{"draped_box"},
	{"exchange"},
	{"fleur"},
	{"gobbler"},
	{"gumby"},
	{"hand1"},
	{"hand2"},
	{"heart"},
	{"icon"},
	{"iron_cross"},
	{"left_ptr"},
	{"left_side"},
	{"left_tee"},
	{"leftbutton"},
	{"ll_angle"},
	{"lr_angle"},
	{"man"},
	{"middlebutton"},
	{"mouse"},
	{"pencil"},
	{"pirate"},
	{"plus"},
	{"question_arrow"},
	{"right_ptr"},
	{"right_side"},
	{"right_tee"},
	{"rightbutton"},
	{"rtl_logo"},
	{"sailboat"},
	{"sb_down_arrow"},
	{"sb_h_double_arrow"},
	{"sb_left_arrow"},
	{"sb_right_arrow"},
	{"sb_up_arrow"},
	{"sb_v_double_arrow"},
	{"shuttle"},
	{"sizing"},
	{"spider"},
	{"spraycan"},
	{"star"},
	{"target"},
	{"tcross"},
	{"top_left_arrow"},
	{"top_left_corner"},
	{"top_right_corner"},
	{"top_side"},
	{"top_tee"},
	{"trek"},
	{"ul_angle"},
	{"umbrella"},
	{"ur_angle"},
	{"watch"},
	{"xterm"},
	{NULL}
};

static void check_bitmap_status(WXBitmapStatus status, const char *filename, xcb_pixmap_t bitmap)
{
	switch (status) {
	case WXBitmapOpenFailed:
		wwarning(_("failed to open bitmap file \"%s\""), filename);
		break;
	case WXBitmapFileInvalid:
		wwarning(_("\"%s\" is not a valid bitmap file"), filename);
		break;
	case WXBitmapNoMemory:
		wwarning(_("out of memory reading bitmap file \"%s\""), filename);
		break;
	case WXBitmapSuccess:
		xcb_free_pixmap(wdisp->conn, bitmap);
		break;
	}
}

/*
 * (none)
 * (builtin, <cursor_name>)
 * (bitmap, <cursor_bitmap>, <cursor_mask>)
 */
static int parse_cursor(WScreen *scr, WMPropList *pl, xcb_cursor_t *cursor)
{
	WMPropList *elem;
	char *val;
	int nelem;
	int status = 0;

	nelem = WMGetPropListItemCount(pl);
	if (nelem < 1) {
		return (status);
	}
	elem = WMGetFromPLArray(pl, 0);
	if (!elem || !WMIsPLString(elem)) {
		return (status);
	}
	val = WMGetFromPLString(elem);

	if (strcasecmp(val, "none") == 0) {
		status = 1;
		*cursor = XCB_NONE;
	} else if (strcasecmp(val, "builtin") == 0) {
		int i;

		if (nelem != 2) {
			wwarning(_("bad number of arguments in cursor specification"));
			return (status);
		}
		elem = WMGetFromPLArray(pl, 1);
		if (!elem || !WMIsPLString(elem)) {
			return (status);
		}
		val = WMGetFromPLString(elem);

		/* the glyph names coincide with the theme cursor names, so
		 * wXLoadCursor matches the builtin name against the theme
		 * and falls back to the core cursor font */
		for (i = 0; cursor_table[i].name != NULL; i++) {
			if (strcasecmp(val, cursor_table[i].name) == 0) {
				*cursor = wXLoadCursor(wdisp->conn, 0, cursor_table[i].name);
				if (*cursor == XCB_CURSOR_NONE) {
					wwarning(_("could not load cursor \"%s\""), cursor_table[i].name);
					*cursor = XCB_NONE;
				}
				status = 1;
				break;
			}
		}
		if (status != 1)
			wwarning(_("unknown builtin cursor name \"%s\""), val);
	} else if (strcasecmp(val, "bitmap") == 0) {
		char *bitmap_name;
		char *mask_name;
		WXBitmapStatus bitmap_status;
		int mask_status;
		xcb_pixmap_t bitmap;
		xcb_pixmap_t mask;
		unsigned int w, h;
		int x, y;
		WXColor fg, bg;

		if (nelem != 3) {
			wwarning(_("bad number of arguments in cursor specification"));
			return (status);
		}
		elem = WMGetFromPLArray(pl, 1);
		if (!elem || !WMIsPLString(elem)) {
			return (status);
		}
		val = WMGetFromPLString(elem);
		bitmap_name = FindImage(w_global.config->pixmap_path, val);
		if (!bitmap_name) {
			wwarning(_("could not find cursor bitmap file \"%s\""), val);
			return (status);
		}
		elem = WMGetFromPLArray(pl, 2);
		if (!elem || !WMIsPLString(elem)) {
			wfree(bitmap_name);
			return (status);
		}
		val = WMGetFromPLString(elem);
		mask_name = FindImage(w_global.config->pixmap_path, val);
		if (!mask_name) {
			wfree(bitmap_name);
			wwarning(_("could not find cursor bitmap file \"%s\""), val);
			return (status);
		}
		mask_status = wXReadBitmapFile(wdisp->conn, scr->w_win, mask_name, &w, &h, &mask, &x, &y);
		bitmap_status = wXReadBitmapFile(wdisp->conn, scr->w_win, bitmap_name, &w, &h, &bitmap, &x, &y);
		if ((WXBitmapSuccess == bitmap_status) && (WXBitmapSuccess == mask_status)) {
			fg.pixel = scr->black_pixel;
			bg.pixel = scr->white_pixel;
			wXQueryColor(wdisp->conn, scr->w_colormap, &fg);
			wXQueryColor(wdisp->conn, scr->w_colormap, &bg);
			*cursor = wXCreatePixmapCursor(wdisp->conn, bitmap, mask, &fg, &bg, x, y);
			status = 1;
		}
		check_bitmap_status(bitmap_status, bitmap_name, bitmap);
		check_bitmap_status(mask_status, mask_name, mask);
		wfree(bitmap_name);
		wfree(mask_name);
	}
	return (status);
}

static bool resource_retry_default(WScreen *scr, int used_default,
				   const char *default_spec)
{
	return scr && scr->flags.startup && !used_default && default_spec;
}

static bool resource_array_first_is(WMPropList *pl, const char *expected)
{
	WMPropList *item;

	item = WMGetFromPLArray(pl, 0);
	return item && WMIsPLString(item) && WMGetFromPLString(item) &&
	    strcasecmp(WMGetFromPLString(item), expected) == 0;
}

static WMPropList *resource_spec_array(const char *key, const char *description,
				       const char *kind)
{
	WMPropList *pl;

	pl = WMCreatePropListFromDescription(description);
	if (!pl || !WMIsPLArray(pl)) {
		wwarning(_("Wrong option format for key \"%s\". Should be %s."),
			 key, kind);
		if (pl)
			WMReleasePropList(pl);
		return NULL;
	}

	return pl;
}

static WConfigUpdateCallback *texture_update_for_target(int target)
{
	switch (target) {
	case WCFG_TEXTURE_WIDGET_COLOR:
		return setWidgetColor;
	case WCFG_TEXTURE_ICON_BACK:
		return setIconTile;
	case WCFG_TEXTURE_F_TITLE_BACK:
		return setFTitleBack;
	case WCFG_TEXTURE_P_TITLE_BACK:
		return setPTitleBack;
	case WCFG_TEXTURE_U_TITLE_BACK:
		return setUTitleBack;
	case WCFG_TEXTURE_TAB_INACTIVE_TITLE_BACK:
		return setTabInactiveTitleBack;
	case WCFG_TEXTURE_RESIZEBAR_BACK:
		return setResizebarBack;
	case WCFG_TEXTURE_MENU_TITLE_BACK:
		return setMenuTitleBack;
	case WCFG_TEXTURE_MENU_TEXT_BACK:
		return setMenuTextBack;
	case WCFG_TEXTURE_WORKSPACE_MAP_BACK:
		return setWorkspaceMapBackground;
	}

	return NULL;
}

unsigned int wConfigApplyTextureSpec(WScreen *scr, const char *key,
				     int target,
				     const WConfigResourceSpec *spec,
				     const char *default_spec)
{
	WConfigUpdateCallback *update;
	const char *candidate;
	WTexture *texture = NULL;
	WMPropList *pl;
	int used_default;

	if (!scr)
		return 0;

	candidate = spec && spec->spec ? spec->spec : default_spec;
	used_default = !(spec && spec->spec);

again:
	if (!candidate)
		return 0;
	/* the tab-inactive slot is allowed to stay empty so the
	 * renderer falls back to PTitleBack at draw time, dispatch
	 * a NULL through the setter so a runtime reload that clears
	 * the key drops a previously-set texture, every other target
	 * keeps the parse-or-warn behaviour */
	if (target == WCFG_TEXTURE_TAB_INACTIVE_TITLE_BACK &&
	    strcmp(candidate, "()") == 0) {
		texture = NULL;
		return setTabInactiveTitleBack(scr, &texture, NULL);
	}

	pl = resource_spec_array(key, candidate, "Texture");
	if (pl) {
		if (target == WCFG_TEXTURE_WIDGET_COLOR &&
		    !resource_array_first_is(pl, "solid")) {
			wwarning(_("Wrong option format for key \"%s\". Should be %s."),
				 key, "Solid Texture");
		} else {
			texture = parse_texture(scr, pl);
		}
		WMReleasePropList(pl);
	}

	if (!texture) {
		wwarning(_("Error in texture specification for key \"%s\""), key);
		if (resource_retry_default(scr, used_default, default_spec)) {
			candidate = default_spec;
			used_default = 1;
			wwarning(_("using default \"%s\" instead"), default_spec);
			goto again;
		}
		return 0;
	}

	update = texture_update_for_target(target);
	if (!update) {
		wTextureDestroy(scr, texture);
		return 0;
	}

	return update(scr, &texture, NULL);
}

static WConfigUpdateCallback *font_update_for_target(int target)
{
	switch (target) {
	case WCFG_FONT_WINDOW_TITLE:
		return setWinTitleFont;
	case WCFG_FONT_MENU_TITLE:
		return setMenuTitleFont;
	case WCFG_FONT_MENU_TEXT:
		return setMenuTextFont;
	case WCFG_FONT_ICON_TITLE:
		return setIconTitleFont;
	case WCFG_FONT_CLIP_TITLE:
		return setClipTitleFont;
	case WCFG_FONT_LARGE_DISPLAY:
		return setLargeDisplayFont;
	}

	return NULL;
}

unsigned int wConfigApplyFontSpec(WScreen *scr, const char *key,
				  int target,
				  const WConfigResourceSpec *spec,
				  const char *default_spec)
{
	WConfigUpdateCallback *update;
	const char *candidate;
	WMFont *font;
	int used_default;

	if (!scr)
		return 0;

	candidate = spec && spec->spec ? spec->spec : default_spec;
	used_default = !(spec && spec->spec);

again:
	if (!candidate)
		return 0;

	font = WMCreateFont(scr->wmscreen, candidate);
	if (!font && resource_retry_default(scr, used_default, default_spec)) {
		candidate = default_spec;
		used_default = 1;
		wwarning(_("using default \"%s\" instead"), default_spec);
		goto again;
	}
	if (!font && scr->flags.startup)
		font = WMCreateFont(scr->wmscreen, "fixed");
	if (!font) {
		if (scr->flags.startup) {
			wfatal(_("could not load any usable font!!!"));
			exit(1);
		}
		wwarning(_("could not load font for key \"%s\""), key);
		return 0;
	}

	update = font_update_for_target(target);
	if (!update) {
		WMReleaseFont(font);
		return 0;
	}

	return update(scr, font, NULL);
}

static WConfigUpdateCallback *color_update_for_target(int target, void **extra_data)
{
	*extra_data = NULL;
	switch (target) {
	case WCFG_COLOR_HIGHLIGHT:
		return setHightlight;
	case WCFG_COLOR_HIGHLIGHT_TEXT:
		return setHightlightText;
	case WCFG_COLOR_CLIP_TITLE:
		*extra_data = (void *)CLIP_NORMAL;
		return setClipTitleColor;
	case WCFG_COLOR_C_CLIP_TITLE:
		*extra_data = (void *)CLIP_COLLAPSED;
		return setClipTitleColor;
	case WCFG_COLOR_F_TITLE:
		*extra_data = (void *)WS_FOCUSED;
		return setWTitleColor;
	case WCFG_COLOR_P_TITLE:
		*extra_data = (void *)WS_PFOCUSED;
		return setWTitleColor;
	case WCFG_COLOR_U_TITLE:
		*extra_data = (void *)WS_UNFOCUSED;
		return setWTitleColor;
	case WCFG_COLOR_TAB_INACTIVE_TITLE:
		*extra_data = (void *)WS_TABINACTIVE;
		return setWTitleColor;
	case WCFG_COLOR_MENU_TITLE:
		return setMenuTitleColor;
	case WCFG_COLOR_MENU_TEXT:
		return setMenuTextColor;
	case WCFG_COLOR_MENU_DISABLED:
		return setMenuDisabledColor;
	case WCFG_COLOR_ICON_TITLE:
		return setIconTitleColor;
	case WCFG_COLOR_ICON_TITLE_BACK:
		return setIconTitleBack;
	case WCFG_COLOR_FRAME_BORDER:
		return setFrameBorderColor;
	case WCFG_COLOR_FRAME_FOCUSED_BORDER:
		return setFrameFocusedBorderColor;
	case WCFG_COLOR_FRAME_SELECTED_BORDER:
		return setFrameSelectedBorderColor;
	case WCFG_COLOR_FRAME_INNER_BORDER:
		return setFrameInnerBorderColor;
	case WCFG_COLOR_FRAME_INNER_FOCUSED_BORDER:
		return setFrameInnerFocusedBorderColor;
	}

	return NULL;
}

unsigned int wConfigApplyColorSpec(WScreen *scr, const char *key,
				   int target,
				   const WConfigResourceSpec *spec,
				   const char *default_spec)
{
	WConfigUpdateCallback *update;
	const char *candidate;
	void *extra_data;
	WXColor color;
	int used_default;

	if (!scr)
		return 0;

	candidate = spec && spec->spec ? spec->spec : default_spec;
	used_default = !(spec && spec->spec);

again:
	if (!candidate)
		return 0;
	/* the tab-inactive slot is allowed to stay empty so the
	 * renderer falls back to PTitleColor at draw time */
	if (!candidate[0] && target == WCFG_COLOR_TAB_INACTIVE_TITLE) {
		extra_data = (void *)WS_TABINACTIVE;
		return setWTitleColor(scr, NULL, extra_data);
	}
	if (!candidate[0] && !used_default && default_spec) {
		candidate = default_spec;
		used_default = 1;
		goto again;
	}

	if (!wGetColor(scr, candidate, &color)) {
		wwarning(_("could not get color for key \"%s\""), key);
		if (resource_retry_default(scr, used_default, default_spec)) {
			candidate = default_spec;
			used_default = 1;
			wwarning(_("using default \"%s\" instead"), default_spec);
			goto again;
		}
		return 0;
	}

	update = color_update_for_target(target, &extra_data);
	if (!update) {
		wFreeColor(scr, color.pixel);
		return 0;
	}

	return update(scr, &color, extra_data);
}

static int cursor_index_for_target(int target)
{
	switch (target) {
	case WCFG_CURSOR_NORMAL:
		return WCUR_ROOT;
	case WCFG_CURSOR_ARROW:
		return WCUR_ARROW;
	case WCFG_CURSOR_MOVE:
		return WCUR_MOVE;
	case WCFG_CURSOR_RESIZE:
		return WCUR_RESIZE;
	case WCFG_CURSOR_TOP_LEFT_RESIZE:
		return WCUR_TOPLEFTRESIZE;
	case WCFG_CURSOR_TOP_RIGHT_RESIZE:
		return WCUR_TOPRIGHTRESIZE;
	case WCFG_CURSOR_BOTTOM_LEFT_RESIZE:
		return WCUR_BOTTOMLEFTRESIZE;
	case WCFG_CURSOR_BOTTOM_RIGHT_RESIZE:
		return WCUR_BOTTOMRIGHTRESIZE;
	case WCFG_CURSOR_VERTICAL_RESIZE:
		return WCUR_VERTICALRESIZE;
	case WCFG_CURSOR_HORIZONTAL_RESIZE:
		return WCUR_HORIZONRESIZE;
	case WCFG_CURSOR_WAIT:
		return WCUR_WAIT;
	case WCFG_CURSOR_QUESTION:
		return WCUR_QUESTION;
	case WCFG_CURSOR_TEXT:
		return WCUR_TEXT;
	case WCFG_CURSOR_SELECT:
		return WCUR_SELECT;
	case WCFG_CURSOR_CAPTURE:
		return WCUR_CAPTURE;
	}

	return -1;
}

unsigned int wConfigApplyCursorSpec(WScreen *scr, const char *key,
				    int target,
				    const WConfigResourceSpec *spec,
				    const char *default_spec)
{
	const char *candidate;
	xcb_cursor_t cursor;
	WMPropList *pl;
	int cursor_index;
	int used_default;
	int status = 0;

	if (!scr)
		return 0;

	cursor_index = cursor_index_for_target(target);
	if (cursor_index < 0)
		return 0;

	candidate = spec && spec->spec ? spec->spec : default_spec;
	used_default = !(spec && spec->spec);

again:
	if (!candidate)
		return 0;

	pl = resource_spec_array(key, candidate, "cursor specification");
	if (pl) {
		status = parse_cursor(scr, pl, &cursor);
		WMReleasePropList(pl);
	}

	if (!status) {
		wwarning(_("Error in cursor specification for key \"%s\""), key);
		if (resource_retry_default(scr, used_default, default_spec)) {
			candidate = default_spec;
			used_default = 1;
			wwarning(_("using default \"%s\" instead"), default_spec);
			goto again;
		}
		return 0;
	}

	return setCursor(scr, &cursor, (void *)(long)cursor_index);
}

void wConfigApplyModifierKey(const char *spec, const char *default_spec)
{
	const char *candidate = spec ? spec : default_spec;
	int mask;

	if (!candidate)
		return;

	mask = wXModifierFromKey(candidate);
	if (mask < 0) {
		wwarning(_("%s: modifier key %s is not valid"), "ModifierKey",
			 candidate);
		return;
	}

	wdisp->modifier_mask = mask;
}

void wConfigApplyTabDragModifier(const char *spec)
{
	int mask;

	if (!spec || spec[0] == '\0' || strcasecmp(spec, "None") == 0) {
		wdisp->tab_drag_modifier_mask = 0;
		return;
	}

	mask = wXModifierFromKey(spec);
	if (mask < 0) {
		wwarning(_("%s: modifier key %s is not valid"),
			 "TabDragModifier", spec);
		wdisp->tab_drag_modifier_mask = 0;
		return;
	}

	wdisp->tab_drag_modifier_mask = (unsigned int)mask;
}

static bool workspace_background_spec_valid(const char *key, int target,
					    WMPropList *value)
{
	WMPropList *elem;
	int i, count;

	count = WMGetPropListItemCount(value);
	if (target == WCFG_BACKGROUND_WORKSPACE && count > 0) {
		elem = WMGetFromPLArray(value, 0);
		if (!elem || !WMIsPLString(elem)) {
			wwarning(_("Wrong type for workspace background. Should be a texture type."));
			return false;
		}
	}

	if (target == WCFG_BACKGROUND_WORKSPACE_SPECIFIC) {
		for (i = count - 1; i >= 0; i--) {
			elem = WMGetFromPLArray(value, i);
			if (!elem || !WMIsPLArray(elem))
				wwarning(_("Wrong type for background of workspace %i. Should be a texture."),
					 i);
		}
		return true;
	}

	if (target != WCFG_BACKGROUND_WORKSPACE) {
		wwarning(_("Invalid background target for key \"%s\""), key);
		return false;
	}

	return true;
}

unsigned int wConfigApplyWorkspaceBackgroundSpec(WScreen *scr, const char *key,
						 int target,
						 const WConfigResourceSpec *spec,
						 const char *default_spec)
{
	const char *kind;
	const char *candidate;
	WMPropList *value;
	int used_default;

	if (!scr)
		return 0;

	kind = (target == WCFG_BACKGROUND_WORKSPACE) ?
	    "Texture or None" : "an array of textures";
	candidate = spec && spec->spec ? spec->spec : default_spec;
	used_default = !(spec && spec->spec);

again:
	if (!candidate)
		return 0;

	value = resource_spec_array(key, candidate, kind);
	if (!value || !workspace_background_spec_valid(key, target, value)) {
		if (value)
			WMReleasePropList(value);
		if (resource_retry_default(scr, used_default, default_spec)) {
			candidate = default_spec;
			used_default = 1;
			wwarning(_("using default \"%s\" instead"), default_spec);
			goto again;
		}
		return 0;
	}

	if (target == WCFG_BACKGROUND_WORKSPACE_SPECIFIC)
		return setWorkspaceSpecificBack(scr, value, NULL);
	return setWorkspaceBack(scr, value, NULL);
}

#undef CURSOR_ID_NONE

/* ---------------- value setting functions --------------- */
static int setIconTile(WScreen *scr, void *tdata, void *foo)
{
	xcb_pixmap_t pixmap;
	RImage *img;
	WTexture ** texture = tdata;
	int reset = 0;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;

	img = wTextureRenderImage(*texture, w_global.config->icon_size,
				  w_global.config->icon_size, ((*texture)->any.type & WREL_BORDER_MASK)
				  ? WREL_ICON : WREL_FLAT);
	if (!img) {
		wwarning(_("could not render texture for icon background"));
		wTextureDestroy(scr, *texture);
		return 0;
	}
	RConvertImage(scr->rcontext, img, &pixmap);

	if (scr->icon_tile) {
		reset = 1;
		RReleaseImage(scr->icon_tile);
		xcb_free_pixmap(wdisp->conn, scr->icon_tile_pixmap);
	}

	scr->icon_tile = img;

	/* put the icon in the noticeboard hint */
	PropSetIconTileHint(scr, img);

	if (!wdisp->flags.noclip || wdisp->flags.clip_merged_in_dock) {
		if (scr->clip_tile) {
			RReleaseImage(scr->clip_tile);
		}
		scr->clip_tile = wClipMakeTile(img);
	}

	if (!wdisp->flags.nodrawer) {
		if (scr->drawer_tile) {
			RReleaseImage(scr->drawer_tile);
		}
		scr->drawer_tile = wDrawerMakeTile(scr, img);
	}

	scr->icon_tile_pixmap = pixmap;

	if (scr->def_icon_rimage) {
		RReleaseImage(scr->def_icon_rimage);
		scr->def_icon_rimage = NULL;
	}

	if (scr->icon_back_texture)
		wTextureDestroy(scr, (WTexture *) scr->icon_back_texture);

	scr->icon_back_texture = wTextureMakeSolid(scr, &((*texture)->any.color));

	/* Free the texture as nobody else will use it, nor refer to it.  */
	wTextureDestroy(scr, *texture);

	return (reset ? REFRESH_ICON_TILE : 0);
}

static int setWinTitleFont(WScreen *scr, void *tdata, void *foo)
{
	WMFont *font = tdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;

	if (scr->title_font) {
		WMReleaseFont(scr->title_font);
	}
	scr->title_font = font;

	return REFRESH_WINDOW_FONT | REFRESH_BUTTON_IMAGES;
}

static int setMenuTitleFont(WScreen *scr, void *tdata, void *foo)
{
	WMFont *font = tdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;

	if (scr->menu_title_font) {
		WMReleaseFont(scr->menu_title_font);
	}

	scr->menu_title_font = font;

	return REFRESH_MENU_TITLE_FONT;
}

static int setMenuTextFont(WScreen *scr, void *tdata, void *foo)
{
	WMFont *font = tdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;

	if (scr->menu_entry_font) {
		WMReleaseFont(scr->menu_entry_font);
	}
	scr->menu_entry_font = font;

	return REFRESH_MENU_FONT;
}

static int setIconTitleFont(WScreen *scr, void *tdata, void *foo)
{
	WMFont *font = tdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;

	if (scr->icon_title_font) {
		WMReleaseFont(scr->icon_title_font);
	}

	scr->icon_title_font = font;

	return REFRESH_ICON_FONT;
}

static int setClipTitleFont(WScreen *scr, void *tdata, void *foo)
{
	WMFont *font = tdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;

	if (scr->clip_title_font) {
		WMReleaseFont(scr->clip_title_font);
	}

	scr->clip_title_font = font;

	return REFRESH_ICON_FONT;
}

static int setLargeDisplayFont(WScreen *scr, void *tdata, void *foo)
{
	WMFont *font = tdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;

	if (scr->workspace_name_font)
		WMReleaseFont(scr->workspace_name_font);

	scr->workspace_name_font = font;

	/* On cold boot this runs from wReadDefaults() before wScreenInit()
	 * has created the OSD, wScreenInit() applies the initial font
	 * itself right after WCreateOSDView(). */
	if (scr->workspace_name_osd)
		WSetOSDViewFont(scr->workspace_name_osd, font);

	return 0;
}

static int setHightlight(WScreen *scr, void *tdata, void *foo)
{
	WXColor *color = tdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;

	if (scr->select_color)
		WMReleaseColor(scr->select_color);

	scr->select_color = WMCreateRGBColor(scr->wmscreen, color->red, color->green, color->blue, true);

	wFreeColor(scr, color->pixel);

	return REFRESH_MENU_COLOR;
}

static int setHightlightText(WScreen *scr, void *tdata, void *foo)
{
	WXColor *color = tdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;

	if (scr->select_text_color)
		WMReleaseColor(scr->select_text_color);

	scr->select_text_color = WMCreateRGBColor(scr->wmscreen, color->red, color->green, color->blue, true);

	wFreeColor(scr, color->pixel);

	return REFRESH_MENU_COLOR;
}

static int setClipTitleColor(WScreen *scr, void *tdata, void *extra_data)
{
	WXColor *color = tdata;
	long widx = (long) extra_data;

	/* Parameter not used, but tell the compiler that it is ok */

	if (scr->clip_title_color[widx])
		WMReleaseColor(scr->clip_title_color[widx]);

	scr->clip_title_color[widx] = WMCreateRGBColor(scr->wmscreen, color->red, color->green, color->blue, true);
	wFreeColor(scr, color->pixel);

	return REFRESH_ICON_TITLE_COLOR;
}

static int setWTitleColor(WScreen *scr, void *tdata, void *extra_data)
{
	WXColor *color = tdata;
	long widx = (long) extra_data;

	if (scr->window_title_color[widx])
		WMReleaseColor(scr->window_title_color[widx]);

	if (!color) {
		/* the tab-inactive slot is left NULL when the key is
		 * empty so the renderer falls back to PTitleColor */
		scr->window_title_color[widx] = NULL;
		return REFRESH_WINDOW_TITLE_COLOR;
	}

	scr->window_title_color[widx] =
	    WMCreateRGBColor(scr->wmscreen, color->red, color->green, color->blue, true);

	wFreeColor(scr, color->pixel);

	return REFRESH_WINDOW_TITLE_COLOR;
}

static int setMenuTitleColor(WScreen *scr, void *tdata, void *extra_data)
{
	WXColor *color = tdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) extra_data;

	if (scr->menu_title_color[0])
		WMReleaseColor(scr->menu_title_color[0]);

	scr->menu_title_color[0] = WMCreateRGBColor(scr->wmscreen, color->red, color->green, color->blue, true);

	wFreeColor(scr, color->pixel);

	return REFRESH_MENU_TITLE_COLOR;
}

static int setMenuTextColor(WScreen *scr, void *tdata, void *foo)
{
	WXColor *color = tdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;

	if (scr->mtext_color)
		WMReleaseColor(scr->mtext_color);

	scr->mtext_color = WMCreateRGBColor(scr->wmscreen, color->red, color->green, color->blue, true);

	if (WMColorPixel(scr->dtext_color) == WMColorPixel(scr->mtext_color)) {
		WMSetColorAlpha(scr->dtext_color, 0x7fff);
	} else {
		WMSetColorAlpha(scr->dtext_color, 0xffff);
	}

	wFreeColor(scr, color->pixel);

	return REFRESH_MENU_COLOR;
}

static int setMenuDisabledColor(WScreen *scr, void *tdata, void *foo)
{
	WXColor *color = tdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;

	if (scr->dtext_color)
		WMReleaseColor(scr->dtext_color);

	scr->dtext_color = WMCreateRGBColor(scr->wmscreen, color->red, color->green, color->blue, true);

	if (WMColorPixel(scr->dtext_color) == WMColorPixel(scr->mtext_color)) {
		WMSetColorAlpha(scr->dtext_color, 0x7fff);
	} else {
		WMSetColorAlpha(scr->dtext_color, 0xffff);
	}

	wFreeColor(scr, color->pixel);

	return REFRESH_MENU_COLOR;
}

static int setIconTitleColor(WScreen *scr, void *tdata, void *foo)
{
	WXColor *color = tdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;

	if (scr->icon_title_color)
		WMReleaseColor(scr->icon_title_color);
	scr->icon_title_color = WMCreateRGBColor(scr->wmscreen, color->red, color->green, color->blue, true);

	wFreeColor(scr, color->pixel);

	return REFRESH_ICON_TITLE_COLOR;
}

static int setIconTitleBack(WScreen *scr, void *tdata, void *foo)
{
	WXColor *color = tdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;

	if (scr->icon_title_texture) {
		wTextureDestroy(scr, (WTexture *) scr->icon_title_texture);
	}
	scr->icon_title_texture = wTextureMakeSolid(scr, color);

	return REFRESH_ICON_TITLE_BACK;
}

static int setFrameBorderColor(WScreen *scr, void *tdata, void *foo)
{
	WXColor *color = tdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;

	if (scr->frame_border_color)
		WMReleaseColor(scr->frame_border_color);
	scr->frame_border_color = WMCreateRGBColor(scr->wmscreen, color->red, color->green, color->blue, true);

	wFreeColor(scr, color->pixel);

	return REFRESH_FRAME_BORDER;
}

static int setFrameFocusedBorderColor(WScreen *scr, void *tdata, void *foo)
{
	WXColor *color = tdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;

	if (scr->frame_focused_border_color)
		WMReleaseColor(scr->frame_focused_border_color);
	scr->frame_focused_border_color = WMCreateRGBColor(scr->wmscreen, color->red, color->green, color->blue, true);

	wFreeColor(scr, color->pixel);

	return REFRESH_FRAME_BORDER;
}

static int setFrameSelectedBorderColor(WScreen *scr, void *tdata, void *foo)
{
	WXColor *color = tdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;

	if (scr->frame_selected_border_color)
		WMReleaseColor(scr->frame_selected_border_color);
	scr->frame_selected_border_color = WMCreateRGBColor(scr->wmscreen, color->red, color->green, color->blue, true);

	wFreeColor(scr, color->pixel);

	return REFRESH_FRAME_BORDER;
}

static int setFrameInnerBorderColor(WScreen *scr, void *tdata, void *foo)
{
	WXColor *color = tdata;

	(void) foo;

	if (scr->frame_inner_border_color)
		WMReleaseColor(scr->frame_inner_border_color);
	scr->frame_inner_border_color = WMCreateRGBColor(scr->wmscreen, color->red, color->green, color->blue, true);
	scr->frame_inner_border_pixel = WMColorPixel(scr->frame_inner_border_color);
	wFreeColor(scr, color->pixel);

	return REFRESH_INNER_BORDER;
}

static int setFrameInnerFocusedBorderColor(WScreen *scr, void *tdata, void *foo)
{
	WXColor *color = tdata;

	(void) foo;

	if (scr->frame_inner_focused_border_color)
		WMReleaseColor(scr->frame_inner_focused_border_color);
	scr->frame_inner_focused_border_color = WMCreateRGBColor(scr->wmscreen, color->red, color->green, color->blue, true);
	scr->frame_inner_focused_border_pixel = WMColorPixel(scr->frame_inner_focused_border_color);
	wFreeColor(scr, color->pixel);

	return REFRESH_INNER_BORDER;
}

static int setWorkspaceSpecificBack(WScreen *scr, void *tdata, void *bar)
{
	WMPropList *value = tdata;
	WMPropList *val;
	char *str;
	int i;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) bar;

	if (scr->flags.backimage_helper_launched) {
		if (WMGetPropListItemCount(value) == 0) {
			SendHelperMessage(scr, 'C', 0, NULL);
			SendHelperMessage(scr, 'K', 0, NULL);

			WMReleasePropList(value);
			return 0;
		}
	} else {
		if (WMGetPropListItemCount(value) == 0) {
			WMReleasePropList(value);
			return 0;
		}

		if (!start_bg_helper(scr)) {
			WMReleasePropList(value);
			return 0;
		}

		SendHelperMessage(scr, 'P', -1, w_global.config->pixmap_path);
	}

	for (i = 0; i < WMGetPropListItemCount(value); i++) {
		val = WMGetFromPLArray(value, i);
		if (val && WMIsPLArray(val) && WMGetPropListItemCount(val) > 0) {
			str = WMGetPropListDescription(val, false);

			SendHelperMessage(scr, 'S', i + 1, str);

			wfree(str);
		} else {
			SendHelperMessage(scr, 'U', i + 1, NULL);
		}
	}

	/* the helper reads the pipe at its own pace, the kernel buffers
	 * the messages and order is preserved, no acknowledgment and
	 * no wait is needed here */

	WMReleasePropList(value);
	return 0;
}

static void background_child_env(void *data)
{
	SetupEnvironment(data);
}

static int setWorkspaceBack(WScreen *scr, void *tdata, void *bar)
{
	WMPropList *value = tdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) bar;

	workspace_back_applied = time(NULL);

	if (scr->flags.backimage_helper_launched) {
		char *str;

		if (WMGetPropListItemCount(value) == 0) {
			SendHelperMessage(scr, 'U', 0, NULL);
		} else {
			/* set the default workspace background to this one */
			str = WMGetPropListDescription(value, false);
			if (str) {
				SendHelperMessage(scr, 'S', 0, str);
				wfree(str);
				SendHelperMessage(scr, 'C', scr->current_workspace + 1, NULL);
			} else {
				SendHelperMessage(scr, 'U', 0, NULL);
			}
		}
	} else if (WMGetPropListItemCount(value) > 0) {
		char *text;
		pid_t pid;

		text = WMGetPropListDescription(value, false);
		if (text) {
			if (w_global.config->smooth_workspace_back)
				pid = wspawnvp_setup((char *[]){ "wmsetbg", "-S", "-p", text, NULL },
						     background_child_env, scr);
			else
				pid = wspawnvp_setup((char *[]){ "wmsetbg", "-p", text, NULL },
						     background_child_env, scr);
			if (pid < 0)
				werror(_("could not execute wmsetbg"));
			wfree(text);
		}
	}
	WMReleasePropList(value);

	return 0;
}

void wDefaultsReconfigure(void)
{
	int i;

	wDefaultsCheckDomains(NULL);

	if (time(NULL) - workspace_back_applied <= 1)
		return;

	for (i = 0; i < wdisp->screen_count; i++) {
		WScreen *scr = wScreenWithNumber(i);

		if (!scr)
			continue;
		(void) wConfigApplyWorkspaceBackgroundSpec(scr, "WorkspaceBack",
		    WCFG_BACKGROUND_WORKSPACE, &w_global.config->workspace_back,
		    "(solid, \"rgb:50/50/75\")");
	}
}

static int setWidgetColor(WScreen *scr, void *tdata, void *foo)
{
	WTexture **texture = tdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;

	if (scr->widget_texture) {
		wTextureDestroy(scr, (WTexture *) scr->widget_texture);
	}
	scr->widget_texture = *(WTexSolid **) texture;

	return REFRESH_WINDOW_TEXTURES | REFRESH_MENU_TITLE_TEXTURE;
}

static int setFTitleBack(WScreen *scr, void *tdata, void *foo)
{
	WTexture **texture = tdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;

	if (scr->window_title_texture[WS_FOCUSED]) {
		wTextureDestroy(scr, scr->window_title_texture[WS_FOCUSED]);
	}
	scr->window_title_texture[WS_FOCUSED] = *texture;

	return REFRESH_WINDOW_TEXTURES;
}

static int setPTitleBack(WScreen *scr, void *tdata, void *foo)
{
	WTexture **texture = tdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;

	if (scr->window_title_texture[WS_PFOCUSED]) {
		wTextureDestroy(scr, scr->window_title_texture[WS_PFOCUSED]);
	}
	scr->window_title_texture[WS_PFOCUSED] = *texture;

	return REFRESH_WINDOW_TEXTURES;
}

static int setUTitleBack(WScreen *scr, void *tdata, void *foo)
{
	WTexture **texture = tdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;

	if (scr->window_title_texture[WS_UNFOCUSED]) {
		wTextureDestroy(scr, scr->window_title_texture[WS_UNFOCUSED]);
	}
	scr->window_title_texture[WS_UNFOCUSED] = *texture;

	return REFRESH_WINDOW_TEXTURES;
}

static int setTabInactiveTitleBack(WScreen *scr, void *tdata, void *foo)
{
	WTexture **texture = tdata;

	(void) foo;

	if (scr->window_title_texture[WS_TABINACTIVE])
		wTextureDestroy(scr, scr->window_title_texture[WS_TABINACTIVE]);
	/* may be NULL when the key is empty, the segment renderer
	 * then falls back to the PTitleBack slot */
	scr->window_title_texture[WS_TABINACTIVE] = *texture;

	return REFRESH_WINDOW_TEXTURES;
}

static int setResizebarBack(WScreen *scr, void *tdata, void *foo)
{
	WTexture **texture = tdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;

	if (scr->resizebar_texture[0]) {
		wTextureDestroy(scr, scr->resizebar_texture[0]);
	}
	scr->resizebar_texture[0] = *texture;

	return REFRESH_WINDOW_TEXTURES;
}

static int setMenuTitleBack(WScreen *scr, void *tdata, void *foo)
{
	WTexture **texture = tdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;

	if (scr->menu_title_texture[0]) {
		wTextureDestroy(scr, scr->menu_title_texture[0]);
	}
	scr->menu_title_texture[0] = *texture;

	return REFRESH_MENU_TITLE_TEXTURE;
}

static int setMenuTextBack(WScreen *scr, void *tdata, void *foo)
{
	WTexture **texture = tdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;

	if (scr->menu_item_texture) {
		wTextureDestroy(scr, scr->menu_item_texture);
		wTextureDestroy(scr, (WTexture *) scr->menu_item_auxtexture);
	}
	scr->menu_item_texture = *texture;

	scr->menu_item_auxtexture = wTextureMakeSolid(scr, &scr->menu_item_texture->any.color);

	return REFRESH_MENU_TEXTURE;
}

static bool shortcut_values_equal(const WShortKey *old_key,
				  const WShortKey *new_key)
{
	int count;

	if (old_key->modifier != new_key->modifier ||
	    old_key->keycode != new_key->keycode ||
	    old_key->chain_length != new_key->chain_length)
		return false;

	if (new_key->chain_length <= 1)
		return true;

	if (!old_key->chain_modifiers || !old_key->chain_keycodes ||
	    !new_key->chain_modifiers || !new_key->chain_keycodes)
		return false;

	count = new_key->chain_length - 1;
	return memcmp(old_key->chain_modifiers, new_key->chain_modifiers,
		      count * sizeof(unsigned int)) == 0 &&
	    memcmp(old_key->chain_keycodes, new_key->chain_keycodes,
		   count * sizeof(xcb_keycode_t)) == 0;
}

static void store_key_binding(int binding_index, const WShortKey *shortcut)
{
	wShortKeyFree(&wKeyBindings[binding_index]);
	wKeyBindings[binding_index] = *shortcut;
	if (shortcut->chain_length > 1) {
		int count = shortcut->chain_length - 1;

		wKeyBindings[binding_index].chain_modifiers =
		    wmalloc(count * sizeof(unsigned int));
		wKeyBindings[binding_index].chain_keycodes =
		    wmalloc(count * sizeof(xcb_keycode_t));
		memcpy(wKeyBindings[binding_index].chain_modifiers,
		       shortcut->chain_modifiers, count * sizeof(unsigned int));
		memcpy(wKeyBindings[binding_index].chain_keycodes,
		       shortcut->chain_keycodes, count * sizeof(xcb_keycode_t));
	} else {
		wKeyBindings[binding_index].chain_modifiers = NULL;
		wKeyBindings[binding_index].chain_keycodes = NULL;
	}
}

void wRegrabFocusedWindowKeys(WScreen *scr)
{
	WWindow *wwin;

	if (!scr)
		return;

	wwin = scr->focused_window;
	while (wwin != NULL) {
		wXUngrabKey(wdisp->conn, XCB_GRAB_ANY, XCB_MOD_MASK_ANY, wwin->frame->core->window);
		if (!WFLAGP(wwin, no_bind_keys))
			wWindowSetKeyGrabs(wwin);
		wwin = wwin->prev;
	}
}

void wRegrabWindowMouseGrabs(WScreen *scr)
{
	WWindow *wwin;

	if (!scr)
		return;

	/* scr->focused_window heads the full managed-window list. wWindow-
	 * ResetMouseGrabs starts by wXUngrabButton(AnyButton, AnyModifier) on
	 * the client window so it self-clears before re-issuing the
	 * modifier-bound button grabs that depend on MOD_MASK. */
	wwin = scr->focused_window;
	while (wwin != NULL) {
		wWindowResetMouseGrabs(wwin);
		wwin = wwin->prev;
	}
}

static unsigned int key_binding_refresh_mask(int binding_index)
{
	if (binding_index >= WKBD_WORKSPACE1 && binding_index <= WKBD_WORKSPACE10)
		return REFRESH_WORKSPACE_MENU;
	if (binding_index == WKBD_LASTWORKSPACE)
		return REFRESH_WORKSPACE_MENU;
	if (binding_index >= WKBD_MOVE_WORKSPACE1 &&
	    binding_index <= WKBD_MOVE_WORKSPACE10)
		return REFRESH_WORKSPACE_MENU;

	return 0;
}

unsigned int wConfigApplyKeyBinding(WScreen *scr, const char *key,
				    int binding_index, const char *spec,
				    const char *default_spec,
				    bool *binding_changed)
{
	WShortKey shortcut;
	bool changed;

	(void)scr;

	if (binding_changed)
		*binding_changed = false;
	if (binding_index < 0 || binding_index >= WKBD_LAST)
		return 0;

	if (!parse_shortcut_spec(key, spec, &shortcut)) {
		wwarning(_("using default \"%s\" instead"), default_spec);
		if (!parse_shortcut_spec(key, default_spec, &shortcut))
			return 0;
	}

	changed = !shortcut_values_equal(&wKeyBindings[binding_index], &shortcut);
	if (changed)
		store_key_binding(binding_index, &shortcut);
	if (binding_changed)
		*binding_changed = changed;

	wShortKeyFree(&shortcut);

	return key_binding_refresh_mask(binding_index);
}

static int setWorkspaceMapBackground(WScreen *scr, void *tdata, void *foo)
{
	WTexture **texture = tdata;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;

	if (scr->wsmbackTexture)
		wTextureDestroy(scr, scr->wsmbackTexture);

	scr->wsmbackTexture = *texture;

	return REFRESH_WINDOW_TEXTURES;
}

static RImage *chopOffImage(RImage * image, int x, int y, int w, int h)
{
	RImage *img = RCreateImage(w, h, image->format == RRGBAFormat);

	RCopyArea(img, image, x, y, w, h, 0, 0);

	return img;
}

static void release_switch_panel_back_images(WScreen *scr)
{
	int i;

	for (i = 0; i < 9; i++) {
		if (scr->swbackImage[i])
			RReleaseImage(scr->swbackImage[i]);
		scr->swbackImage[i] = NULL;
	}
}

static void release_switch_panel_scaled_tile(WScreen *scr)
{
	if (scr->swtileScaled)
		RReleaseImage(scr->swtileScaled);
	scr->swtileScaled = NULL;
	scr->swtileScaledSize = 0;
}

void wConfigApplySwitchPanelImages(WScreen *scr,
				   const WConfigSwitchPanelImages *images)
{
	char *path;
	RImage *bgimage;

	if (!scr)
		return;

	if (!images || !images->tile) {
		release_switch_panel_scaled_tile(scr);
		if (scr->swtileImage)
			RReleaseImage(scr->swtileImage);
		scr->swtileImage = NULL;
		release_switch_panel_back_images(scr);
		return;
	}

	if (images->background) {
		path = FindImage(w_global.config->pixmap_path, images->background);
		if (!path) {
			wwarning(_("Could not find image \"%s\" for option \"%s\""),
				 images->background, "SwitchPanelImages");
		} else {
			bgimage = RLoadImage(scr->rcontext, path, 0);
			if (!bgimage) {
				wwarning(_("Could not load image \"%s\" for option \"%s\""),
					 path, "SwitchPanelImages");
				wfree(path);
			} else {
				int i;
				int swidth, theight;
				int cwidth = images->center_width;
				int cheight = images->center_height;

				wfree(path);
				if (cwidth <= 0 || cheight <= 0 ||
				    cwidth >= bgimage->width - 2 || cheight >= bgimage->height - 2) {
					wwarning(_("Invalid split sizes for switch panel back image."));
				} else {
					release_switch_panel_back_images(scr);
					swidth = (bgimage->width - cwidth) / 2;
					theight = (bgimage->height - cheight) / 2;

					scr->swbackImage[0] = chopOffImage(bgimage, 0, 0, swidth, theight);
					scr->swbackImage[1] = chopOffImage(bgimage, swidth, 0, cwidth, theight);
					scr->swbackImage[2] = chopOffImage(bgimage, swidth + cwidth, 0,
									     swidth, theight);
					scr->swbackImage[3] = chopOffImage(bgimage, 0, theight, swidth, cheight);
					scr->swbackImage[4] = chopOffImage(bgimage, swidth, theight,
									     cwidth, cheight);
					scr->swbackImage[5] = chopOffImage(bgimage, swidth + cwidth, theight,
									     swidth, cheight);
					scr->swbackImage[6] = chopOffImage(bgimage, 0, theight + cheight,
									     swidth, theight);
					scr->swbackImage[7] = chopOffImage(bgimage, swidth, theight + cheight,
									     cwidth, theight);
					scr->swbackImage[8] =
					    chopOffImage(bgimage, swidth + cwidth, theight + cheight,
							 swidth, theight);

					for (i = 0; i < 9; i++) {
						if (!scr->swbackImage[i]) {
							release_switch_panel_back_images(scr);
							break;
						}
					}
				}
				RReleaseImage(bgimage);
			}
		}
	}

	path = FindImage(w_global.config->pixmap_path, images->tile);
	if (!path) {
		wwarning(_("Could not find image \"%s\" for option \"%s\""),
			 images->tile, "SwitchPanelImages");
	} else {
		release_switch_panel_scaled_tile(scr);
		if (scr->swtileImage)
			RReleaseImage(scr->swtileImage);
		scr->swtileImage = RLoadImage(scr->rcontext, path, 0);
		if (!scr->swtileImage)
			wwarning(_("Could not load image \"%s\" for option \"%s\""),
				 path, "SwitchPanelImages");
		wfree(path);
	}
}

void wConfigApplyModifierKeyLabels(WScreen *scr,
				   const WConfigStringArray *labels)
{
	size_t i;

	if (!labels || labels->count != 7) {
		wwarning(_("Value for option \"%s\" must be an array of 7 strings"),
			 "ModifierKeyLabels");
		return;
	}

	if (scr)
		DestroyWindowMenu(scr);

	for (i = 0; i < 7; i++) {
		if (wdisp->modifier_labels[i])
			wfree(wdisp->modifier_labels[i]);
		wdisp->modifier_labels[i] = wstrdup(labels->items[i]);
	}
}

void wConfigApplyModifierKeyShortLabels(WScreen *scr,
					const WConfigStringArray *labels)
{
	size_t i;

	if (!labels || labels->count != 9) {
		wwarning(_("Value for option \"%s\" must be an array of 9 strings"),
			 "ModifierKeyShortLabels");
		return;
	}

	if (scr)
		DestroyWindowMenu(scr);

	for (i = 0; i < 9; i++) {
		if (wdisp->modifier_short_labels[i])
			wfree(wdisp->modifier_short_labels[i]);
		if (labels->items[i][0] == '\0') {
			wwarning(_("Invalid argument for option \"%s\" item %zu, cannot be empty"),
				 "ModifierKeyShortLabels", i);
			wdisp->modifier_short_labels[i] = NULL;
		} else {
			wdisp->modifier_short_labels[i] = wstrdup(labels->items[i]);
		}
	}
}

void wConfigApplyHotCornerActions(WScreen *scr,
				  const WConfigStringArray *actions)
{
	size_t i;

	if (!actions || actions->count != 4) {
		wwarning(_("Value for option \"%s\" must be an array of 4 strings"),
			 "HotCornerActions");
		return;
	}

	if (!scr)
		return;

	DestroyWindowMenu(scr);

	for (i = 0; i < 4; i++) {
		if (scr->hot_corner_actions[i])
			wfree(scr->hot_corner_actions[i]);
		if (strcasecmp(actions->items[i], "NONE") != 0)
			scr->hot_corner_actions[i] = wstrdup(actions->items[i]);
		else
			scr->hot_corner_actions[i] = NULL;
	}
}

static int setCursor(WScreen *scr, void *tdata, void *extra_data)
{
	xcb_cursor_t *cursor = tdata;
	long widx = (long) extra_data;

	/* Parameter not used, but tell the compiler that it is ok */

	if (wdisp->cursor[widx] != XCB_NONE)
		xcb_free_cursor(wdisp->conn, wdisp->cursor[widx]);

	wdisp->cursor[widx] = *cursor;

	if (widx == WCUR_ROOT && *cursor != XCB_NONE) {
		wXDefineCursor(wdisp->conn, scr->root_win, *cursor);
	}

	return 0;
}
