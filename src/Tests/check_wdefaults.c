/*
 *  check_wdefaults.c
 *
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

/*
 * check_wdefaults - tests for the typed WMWindowAttributes cache.
 */

#include "wconfig.h"
#include <stdbool.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "WindowMaker.h"
#include "screen.h"
#include "window.h"
#include "defaults.h"
#include "wdefaults.h"
#include "icon.h"

#include "check_common.h"

struct wmaker_global_variables w_global;

static WDisplay test_display;
WDisplay *wdisp = &test_display;

/* --- stubs for wdefaults.c externs not exercised here --- */

/* controllable by get_icon_filename() fallback tests */
const char *findimage_return;
const char *xdg_match_name;
const char *xdg_return_path;
int xdg_call_count;
int xdg_last_want;

const char *wGetIconCacheDir(void)
{
	return NULL;
}

char *get_name_for_instance_class(const char *wm_instance, const char *wm_class)
{
	(void)wm_instance; (void)wm_class;
	return NULL;
}

int WMPLGetBool(WMPropList *value)
{
	const char *val;

	if (!WMIsPLString(value))
		return 0;
	val = WMGetFromPLString(value);
	if (!val)
		return 0;
	return strcasecmp(val, "YES") == 0 || strcasecmp(val, "Y") == 0 ||
	       strcasecmp(val, "TRUE") == 0 || strcasecmp(val, "T") == 0 ||
	       strcmp(val, "1") == 0;
}

char *StrConcatDot(const char *a, const char *b)
{
	char *buf = wmalloc(strlen(a) + strlen(b) + 2);

	snprintf(buf, strlen(a) + strlen(b) + 2, "%s.%s", a, b);
	return buf;
}

bool UpdateDomainFile(WDDomain *domain)
{
	(void)domain;
	return true;
}

void wApplicationExtractDirPackIcon(const char *path, const char *wm_instance,
				     const char *wm_class)
{
	(void)path; (void)wm_instance; (void)wm_class;
}

RImage *wIconValidateIconSize(RImage *icon, int max_size)
{
	(void)max_size;
	return icon;
}

int wIconEffectiveSize(void)
{
	int size = w_global.config->icon_size;

	if (w_global.config->enforce_icon_margin) {
		size = (int)((double)w_global.config->icon_size * 0.75 + 0.5);
		size = (size >> 2) << 2;
	}
	return size;
}

char *FindImage(const char *paths, const char *file)
{
	(void)paths;
	/* controllable by tests that exercise get_icon_filename()'s
	 * fallback chain; NULL = "not found" (the historical behaviour) */
	if (findimage_return) {
		size_t n = strlen(findimage_return) + 1;
		char *path = wmalloc(n);

		snprintf(path, n, "%s", findimage_return);
		return path;
	}
	(void)file;
	return NULL;
}

/* controllable stub of the hicolor icon-theme resolver so the
 * get_icon_filename() fallback ordering can be tested in isolation
 * from the real filesystem walker in desktopfile.c */
char *wXDGFindThemeIcon(const char *name, const char *context, int want_size)
{
	(void)context;
	xdg_call_count++;
	xdg_last_want = want_size;
	if (name && xdg_match_name && strcmp(name, xdg_match_name) == 0
	    && xdg_return_path) {
		size_t n = strlen(xdg_return_path) + 1;
		char *p = wmalloc(n);

		snprintf(p, n, "%s", xdg_return_path);
		return p;
	}
	return NULL;
}

int wGetWorkspaceNumber(WScreen *scr, const char *value)
{
	(void)scr;
	/* test convention: workspace name is its 1-based number */
	return atoi(value) - 1;
}

/* --- test fixture --- */

static WDDomain test_db;

static void set_attr_dict(const char *desc)
{
	if (test_db.dictionary)
		WMReleasePropList(test_db.dictionary);
	/* instance.class outer keys are case-sensitive in X11, parse the
	 * test domain that way so "xterm" and "XTerm" stay distinct */
	WMPLSetCaseSensitive(true);
	test_db.dictionary = desc ? WMCreatePropListFromDescription(desc) : NULL;
	WMPLSetCaseSensitive(false);
	CHECK(!desc || test_db.dictionary != NULL);
	w_global.domain.window_attr = &test_db;
	wWindowAttrsReload();
}

/* --- tests --- */

static void test_global_default(void)
{
	WWindowAttributes attr, mask;

	set_attr_dict("{}");

	memset(&attr, 0xff, sizeof(attr));
	memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributes("xterm", "XTerm", &attr, &mask, true);
	/* useGlobalDefault writes every flag to its No default and marks
	 * every flag as defined */
	CHECK(attr.no_titlebar == 0);
	CHECK(attr.floating == 0);
	CHECK(attr.no_appicon == 0);
	CHECK(mask.no_titlebar == 1);
	CHECK(mask.floating == 1);
	CHECK(mask.no_appicon == 1);

	memset(&attr, 0, sizeof(attr));
	memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributes("xterm", "XTerm", &attr, &mask, false);
	/* without useGlobalDefault nothing is defined for an empty domain */
	CHECK(mask.no_titlebar == 0);
	CHECK(mask.floating == 0);
}

static void test_precedence(void)
{
	WWindowAttributes attr, mask;

	set_attr_dict("{"
		      " \"*\" = { NoTitlebar = YES; KeepOnTop = YES; "
		      "           Omnipresent = YES; Icon = \"any.png\"; };"
		      " XTerm = { NoTitlebar = NO; KeepOnBottom = YES; "
		      "           Icon = \"class.png\"; StartWorkspace = 3; };"
		      " xterm = { KeepOnTop = NO; Icon = \"inst.png\"; };"
		      " \"xterm.XTerm\" = { Omnipresent = NO; NoBorder = YES; };"
		      "}");

	memset(&attr, 0, sizeof(attr));
	memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributes("xterm", "XTerm", &attr, &mask, true);
	/* instance.class > instance > class > "*" */
	CHECK(attr.omnipresent == 0);   /* overridden by instance.class */
	CHECK(attr.no_border == 1);     /* from instance.class */
	CHECK(attr.floating == 0);      /* "*" YES, instance NO */
	CHECK(attr.sunken == 1);        /* from class */
	CHECK(attr.no_titlebar == 0);   /* "*" YES, class NO */
	CHECK(mask.no_titlebar == 1);
	CHECK(mask.no_border == 1);

	/* without the global default, "*" must not contribute */
	memset(&attr, 0, sizeof(attr));
	memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributes("xterm", "XTerm", &attr, &mask, false);
	CHECK(attr.omnipresent == 0);
	CHECK(mask.omnipresent == 1);   /* instance.class set it */
	CHECK(attr.no_titlebar == 0);   /* class set NO */
	CHECK(mask.no_titlebar == 1);
	CHECK(mask.no_appicon == 0);    /* nobody but "*"-default sets this */

	/* an unrelated window only sees "*" with useGlobalDefault */
	memset(&attr, 0, sizeof(attr));
	wDefaultFillAttributes("urxvt", "URxvt", &attr, NULL, true);
	CHECK(attr.no_titlebar == 1);
	CHECK(attr.floating == 1);
	CHECK(attr.omnipresent == 1);
	CHECK(attr.sunken == 0);
}

static void test_icon_and_workspace(void)
{
	char *icon;
	uint64_t mask;

	set_attr_dict("{"
		      " \"*\" = { Icon = \"any.png\"; StartWorkspace = 4; };"
		      " XTerm = { Icon = \"class.png\"; };"
		      " xterm = { StartWorkspace = 2; };"
		      " \"xterm.XTerm\" = { NoTitlebar = YES; };"
		      "}");

	icon = wDefaultGetIconFile("xterm", "XTerm", false);
	CHECK(icon && strcmp(icon, "class.png") == 0);
	icon = wDefaultGetIconFile("xterm", NULL, false);
	CHECK(icon == NULL);
	icon = wDefaultGetIconFile("xterm", NULL, true);
	CHECK(icon && strcmp(icon, "any.png") == 0);
	icon = wDefaultGetIconFile(NULL, NULL, true);
	CHECK(icon && strcmp(icon, "any.png") == 0);

	/* singular StartWorkspace falls back through the plural getter as a
	 * 1-bit mask (xterm=ws2->bit1, XTerm=ws4->bit3, urxvt via "*"=ws4->bit3) */
	mask = 0;
	CHECK(wDefaultGetStartWorkspaces(NULL, "xterm", "XTerm", &mask) == true);
	CHECK(mask == WS_BIT(1));
	mask = 0;
	CHECK(wDefaultGetStartWorkspaces(NULL, NULL, "XTerm", &mask) == true);
	CHECK(mask == WS_BIT(3));
	mask = 0;
	CHECK(wDefaultGetStartWorkspaces(NULL, "urxvt", "URxvt", &mask) == true);
	CHECK(mask == WS_BIT(3));
}

static void test_start_workspaces_plural(void)
{
	uint64_t mask;

	/* plural StartWorkspaces array resolves to a multi-bit mask */
	set_attr_dict("{"
		" XTerm = { StartWorkspaces = (\"2\", \"4\"); };"
		"}");
	mask = 0;
	CHECK(wDefaultGetStartWorkspaces(NULL, "xterm", "XTerm", &mask) == true);
	CHECK(mask == (WS_BIT(1) | WS_BIT(3)));

	/* a single-element array is a 1-bit mask */
	set_attr_dict("{ XTerm = { StartWorkspaces = (\"3\"); }; }");
	mask = 0;
	CHECK(wDefaultGetStartWorkspaces(NULL, "xterm", "XTerm", &mask) == true);
	CHECK(mask == WS_BIT(2));

	/* empty array = no hint */
	set_attr_dict("{ XTerm = { StartWorkspaces = (); }; }");
	mask = 0;
	CHECK(wDefaultGetStartWorkspaces(NULL, "xterm", "XTerm", &mask) == false);

	/* singular-only falls back to a 1-bit mask */
	set_attr_dict("{ XTerm = { StartWorkspace = 3; }; }");
	mask = 0;
	CHECK(wDefaultGetStartWorkspaces(NULL, "xterm", "XTerm", &mask) == true);
	CHECK(mask == WS_BIT(2));

	/* plural takes precedence over singular in the same entry */
	set_attr_dict("{ XTerm = { StartWorkspace = 3; StartWorkspaces = (\"1\"); }; }");
	mask = 0;
	CHECK(wDefaultGetStartWorkspaces(NULL, "xterm", "XTerm", &mask) == true);
	CHECK(mask == WS_BIT(0));

	/* a name that resolves to nothing yields no hint */
	set_attr_dict("{ XTerm = { StartWorkspaces = (\"nonexistent\"); }; }");
	mask = 0;
	CHECK(wDefaultGetStartWorkspaces(NULL, "xterm", "XTerm", &mask) == false);

	/* no key at all = no hint */
	set_attr_dict("{ XTerm = { NoTitlebar = YES; }; }");
	mask = 0;
	CHECK(wDefaultGetStartWorkspaces(NULL, "xterm", "XTerm", &mask) == false);

	/* "*" global default supplies the singular fallback */
	set_attr_dict("{ \"*\" = { StartWorkspace = 2; }; }");
	mask = 0;
	CHECK(wDefaultGetStartWorkspaces(NULL, "urxvt", "URxvt", &mask) == true);
	CHECK(mask == WS_BIT(1));

	/* instance.class plural wins over class singular */
	set_attr_dict("{"
		" XTerm = { StartWorkspace = 3; };"
		" \"xterm.XTerm\" = { StartWorkspaces = (\"1\", \"2\"); };"
		"}");
	mask = 0;
	CHECK(wDefaultGetStartWorkspaces(NULL, "xterm", "XTerm", &mask) == true);
	CHECK(mask == (WS_BIT(0) | WS_BIT(1)));
}

static void test_change_icon_reloads_cache(void)
{
	char *icon;

	set_attr_dict("{ \"xterm.XTerm\" = { Icon = \"old.png\"; }; }");

	icon = wDefaultGetIconFile("xterm", "XTerm", false);
	CHECK(icon && strcmp(icon, "old.png") == 0);

	wDefaultChangeIcon("xterm", "XTerm", "new.png");
	icon = wDefaultGetIconFile("xterm", "XTerm", false);
	CHECK(icon && strcmp(icon, "new.png") == 0);

	wDefaultChangeIcon("xterm", "XTerm", NULL);
	icon = wDefaultGetIconFile("xterm", "XTerm", false);
	CHECK(icon == NULL);
}

static void test_empty_class(void)
{
	WWindowAttributes attr;
	char *icon;

	/* A WM_CLASS with one or both components set to "" must behave
	 * like that component being absent, the lookup falls through to
	 * the next less specific level rather than matching a "."/"" key. */
	set_attr_dict("{"
		      " \"*\" = { Icon = \"any.png\"; NoTitlebar = YES; };"
		      " XTerm = { NoTitlebar = NO; KeepOnTop = YES; };"
		      " \".XTerm\" = { NoBorder = YES; };"
		      " \"\" = { Omnipresent = YES; };"
		      " \".\" = { Icon = \"dot.png\"; };"
		      "}");

	memset(&attr, 0, sizeof(attr));
	wDefaultFillAttributes("", "XTerm", &attr, NULL, true);
	CHECK(attr.no_titlebar == 0);     /* class entry, not "*" */
	CHECK(attr.floating == 1);        /* class entry */
	CHECK(attr.no_border == 0);       /* ".XTerm" must NOT match */
	CHECK(attr.omnipresent == 0);     /* "" must NOT match */

	memset(&attr, 0, sizeof(attr));
	wDefaultFillAttributes("", "", &attr, NULL, true);
	CHECK(attr.no_titlebar == 1);     /* falls through to "*" */
	CHECK(attr.omnipresent == 0);     /* "" must NOT match */

	icon = wDefaultGetIconFile("", "", true);
	CHECK(icon && strcmp(icon, "any.png") == 0);   /* not "dot.png" */
	icon = wDefaultGetIconFile("", "", false);
	CHECK(icon == NULL);

	/* writing under ("","") goes to "*", and ("","XTerm") to "XTerm",
	 * symmetric with the read side */
	wDefaultChangeIcon("", "XTerm", "class.png");
	icon = wDefaultGetIconFile(NULL, "XTerm", false);
	CHECK(icon && strcmp(icon, "class.png") == 0);
}

static void test_case_sensitivity(void)
{
	WWindowAttributes attr;

	set_attr_dict("{ XTerm = { NoTitlebar = YES; }; }");

	memset(&attr, 0, sizeof(attr));
	wDefaultFillAttributes(NULL, "XTerm", &attr, NULL, true);
	CHECK(attr.no_titlebar == 1);

	memset(&attr, 0, sizeof(attr));
	wDefaultFillAttributes(NULL, "xterm", &attr, NULL, true);
	CHECK(attr.no_titlebar == 0);
}

static void test_schema_count(void)
{
	/* uint64_t bitset, the static assert in wdefaults.c already
	 * covers the upper bound, this catches accidental empty list */
	CHECK(WATTR_BOOL_COUNT >= 28);
	CHECK(WATTR_BOOL_COUNT <= 64);
}

static void reset_icon_stubs(void)
{
	findimage_return = NULL;
	xdg_match_name = NULL;
	xdg_return_path = NULL;
	xdg_call_count = 0;
	xdg_last_want = -1;
}

/* exercise the hicolor icon-theme fallback wired into
 * get_icon_filename(): precedence over the generic default icon,
 * fallback to wXDGFindThemeIcon(res_class) when no Icon= is set,
 * and the lower-cased retry for mixed-case WM_CLASS strings */
static void test_hicolor_fallback(void)
{
	char *path;

	/* case 1: no Icon= entry, no cache hit -> hicolor resolver is
	 * consulted with res_class and its result wins over the generic
	 * default icon */
	set_attr_dict("{}");
	reset_icon_stubs();
	xdg_match_name = "myapp";
	xdg_return_path = "/hicolor/myapp.png";
	path = get_icon_filename("myapp", "myapp", NULL, false);
	CHECK_STR(path, "/hicolor/myapp.png");
	wfree(path);
	CHECK(xdg_call_count >= 1);

	/* case 2: a user-configured Icon= that resolves via FindImage
	 * takes precedence, the hicolor resolver must not be reached */
	set_attr_dict("{ myapp = { Icon = \"user.png\"; }; }");
	reset_icon_stubs();
	findimage_return = "/user/user.png";
	xdg_match_name = "myapp";
	xdg_return_path = "/hicolor/myapp.png";
	path = get_icon_filename("myapp", "myapp", NULL, false);
	CHECK_STR(path, "/user/user.png");
	wfree(path);
	/* user Icon= resolved before any hicolor lookup */
	CHECK(xdg_call_count == 0);

	/* case 3: mixed-case WM_CLASS (res_class = "MyApp") with a
	 * hicolor asset deployed as myapp.png. The literal lookup
	 * misses, then the lower-cased retry hits */
	set_attr_dict("{}");
	reset_icon_stubs();
	/* literal "MyApp" misses */
	xdg_match_name = "myapp";
	xdg_return_path = "/hicolor/myapp.png";
	path = get_icon_filename(NULL, "MyApp", NULL, false);
	CHECK_STR(path, "/hicolor/myapp.png");
	wfree(path);

	/* case 4: nothing resolves -> NULL, not the default icon when
	 * default_icon is false; and the generic default icon when true
	 * (here get_default_image_path also calls FindImage, which is NULL,
	 *  so even true returns NULL) */
	set_attr_dict("{}");
	reset_icon_stubs();
	path = get_icon_filename("x", "X", NULL, false);
	CHECK(path == NULL);
}

/* EnforceIconMargin must steer the hicolor resolver at the margin-adjusted size */
static void test_hicolor_enforce_margin(void)
{
	struct WConfig *cfg = w_global.config;
	char *path;
	int saved_size, saved_margin;

	saved_size = cfg->icon_size;
	saved_margin = cfg->enforce_icon_margin;

	/* icon_size 64 -> effective 48 when the margin is enforced */
	set_attr_dict("{}");
	cfg->icon_size = 64;
	cfg->enforce_icon_margin = 1;
	reset_icon_stubs();
	xdg_match_name = "MyApp";
	xdg_return_path = "/hicolor/myapp.png";
	path = get_icon_filename(NULL, "MyApp", NULL, false);
	CHECK_STR(path, "/hicolor/myapp.png");
	wfree(path);
	CHECK(xdg_last_want == 48);

	/* with the margin off, the resolver is asked for the full icon_size */
	cfg->enforce_icon_margin = 0;
	reset_icon_stubs();
	xdg_match_name = "MyApp";
	xdg_return_path = "/hicolor/myapp.png";
	path = get_icon_filename(NULL, "MyApp", NULL, false);
	CHECK_STR(path, "/hicolor/myapp.png");
	wfree(path);
	CHECK(xdg_last_want == 64);

	cfg->icon_size = saved_size;
	cfg->enforce_icon_margin = saved_margin;
}

/* --- DynamicRules tests --- */

/* Build a stack WWindow with the seven predicate sources populated. The
 * frame is left NULL (wDefaultFillAttributesForWindow never dereferences
 * it), so the matcher can be exercised without an open display. */
static WWindow make_wwin(const char *instance, const char *class,
			 const char *title, const char *role,
			 const char *command, const char *machine,
			 const char * const *types, int ntypes)
{
	WWindow w;

	memset(&w, 0, sizeof(w));
	w.wm_instance = (char *)instance;
	w.wm_class = (char *)class;
	w.wm_title = (char *)title;
	w.wm_role = (char *)role;
	w.wm_command = (char *)command;
	w.wm_machine = (char *)machine;
	w.wm_type_names = (char **)types;
	w.wm_type_count = ntypes;
	return w;
}

static void test_dynamic_single_predicate(void)
{
	WWindowAttributes attr, mask;
	WWindow w;

	set_attr_dict("{"
		      " DynamicRules = ("
		      "  { MatchClass = \"^Firefox$\";"
		      "    Attributes = { NoTitlebar = YES; }; },"
		      "  { MatchInstance = \"^xterm$\";"
		      "    Attributes = { KeepOnTop = YES; }; },"
		      "  { MatchTitle = \".*YouTube.*\";"
		      "    Attributes = { SkipWindowList = YES; }; },"
		      "  { MatchRole = \"^browser$\";"
		      "    Attributes = { NoBorder = YES; }; },"
		      "  { MatchCommand = \".*firefox.*\";"
		      "    Attributes = { Omnipresent = YES; }; },"
		      "  { MatchMachine = \"^build-cluster$\";"
		      "    Attributes = { Unfocusable = YES; }; }"
		      " );"
		      "}");

	w = make_wwin(NULL, "Firefox", NULL, NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 1);
	CHECK(mask.no_titlebar == 1);

	w = make_wwin("xterm", NULL, NULL, NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.floating == 1);

	w = make_wwin(NULL, NULL, "watching YouTube", NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.skip_window_list == 1);

	w = make_wwin(NULL, NULL, NULL, "browser", NULL, NULL, NULL, 0);
	memset(&attr, 0, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_border == 1);

	w = make_wwin(NULL, NULL, NULL, NULL, "/usr/bin/firefox", NULL, NULL, 0);
	memset(&attr, 0, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.omnipresent == 1);

	w = make_wwin(NULL, NULL, NULL, NULL, NULL, "build-cluster", NULL, 0);
	memset(&attr, 0, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_focusable == 1);

	w = make_wwin("urxvt", "URxvt", "plain", "term", "urxvt", "laptop", NULL, 0);
	memset(&attr, 0xff, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 0);
	CHECK(attr.floating == 0);
	CHECK(attr.skip_window_list == 0);
	CHECK(attr.no_border == 0);
	CHECK(attr.omnipresent == 0);
	CHECK(attr.no_focusable == 0);
}

static void test_dynamic_multi_predicate_and(void)
{
	WWindowAttributes attr, mask;
	WWindow w;

	set_attr_dict("{"
		      " DynamicRules = ("
		      "  { MatchClass = \"^Firefox$\";"
		      "    MatchTitle = \".*YouTube.*\";"
		      "    Attributes = { NoTitlebar = YES; KeepOnTop = YES; }; }"
		      " );"
		      "}");

	w = make_wwin(NULL, "Firefox", "watching YouTube", NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 1);
	CHECK(attr.floating == 1);

	w = make_wwin(NULL, "Firefox", "about:blank", NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 0);
	CHECK(attr.floating == 0);

	w = make_wwin(NULL, "Chrome", "watching YouTube", NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 0);
	CHECK(attr.floating == 0);
}

static void test_dynamic_first_match_wins(void)
{
	WWindowAttributes attr, mask;
	WWindow w;

	set_attr_dict("{"
		      " DynamicRules = ("
		      "  { MatchClass = \"^Firefox$\";"
		      "    Attributes = { NoTitlebar = YES; }; },"
		      "  { MatchClass = \"^Firefox$\";"
		      "    Attributes = { NoBorder = YES; }; }"
		      " );"
		      "}");

	w = make_wwin(NULL, "Firefox", NULL, NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 1);
	CHECK(attr.no_border == 0);
}

static void test_dynamic_empty_block_rejected(void)
{
	WWindowAttributes attr, mask;
	WWindow w;

	set_attr_dict("{"
		      " DynamicRules = ("
		      "  { Attributes = { NoTitlebar = YES; }; }"
		      " );"
		      "}");

	w = make_wwin("urxvt", "URxvt", NULL, NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0xff, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 0);

	set_attr_dict("{"
		      " DynamicRules = ("
		      "  { MatchClass = \"^x$\"; }"
		      " );"
		      "}");
	w = make_wwin(NULL, "x", NULL, NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0xff, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 0);
}

static void test_dynamic_missing_attributes_no_pollution(void)
{
	WWindowAttributes attr, mask;
	WWindow w;

	/* Rule 1 has no Attributes block and rule 2 supplies one. Slot
	 * reuse must not leak rule 1's compiled Class regex into rule 2. */
	set_attr_dict("{"
		      " DynamicRules = ("
		      "  { MatchClass = \"^Foo$\"; },"
		      "  { MatchInstance = \"^bar$\";"
		      "    Attributes = { NoTitlebar = YES; }; }"
		      " );"
		      "}");

	/* rule 2 matches on instance, class is unrelated */
	w = make_wwin("bar", "Other", NULL, NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 1);
	CHECK(mask.no_titlebar == 1);

	/* a window that satisfies rule 1's class but not rule 2's
	 * instance must not get the attributes either */
	w = make_wwin("notbar", "Foo", NULL, NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0xff, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 0);
}

static void test_dynamic_malformed_regex_skipped(void)
{
	WWindowAttributes attr, mask;
	WWindow w;

	set_attr_dict("{"
		      " DynamicRules = ("
		      "  { MatchClass = \"[\";"
		      "    Attributes = { NoTitlebar = YES; }; },"
		      "  { MatchClass = \"^Firefox$\";"
		      "    Attributes = { NoBorder = YES; }; }"
		      " );"
		      "}");

	w = make_wwin(NULL, "Firefox", NULL, NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 0);
	CHECK(attr.no_border == 1);
}

static void test_dynamic_window_type_multi(void)
{
	WWindowAttributes attr, mask;
	static const char * const types_a[] = {
		"_NET_WM_WINDOW_TYPE_NORMAL",
		"_NET_WM_WINDOW_TYPE_UTILITY"
	};
	static const char * const types_b[] = {
		"_NET_WM_WINDOW_TYPE_NORMAL"
	};
	WWindow w;

	set_attr_dict("{"
		      " DynamicRules = ("
		      "  { MatchWindowType = \"_NET_WM_WINDOW_TYPE_UTILITY\";"
		      "    Attributes = { KeepOnTop = YES; }; }"
		      " );"
		      "}");

	w = make_wwin(NULL, NULL, NULL, NULL, NULL, NULL, types_a, 2);
	memset(&attr, 0, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.floating == 1);

	w = make_wwin(NULL, NULL, NULL, NULL, NULL, NULL, types_b, 1);
	memset(&attr, 0, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.floating == 0);

	w = make_wwin(NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0xff, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.floating == 0);
}

static void test_dynamic_reeval_and_reversion(void)
{
	WWindowAttributes attr, mask, base_attr, base_mask;
	WWindow w;

	set_attr_dict("{"
		      " \"xterm.XTerm\" = { NoTitlebar = YES; };"
		      " DynamicRules = ("
		      "  { MatchInstance = \"^xterm$\";"
		      "    MatchTitle = \".*Special.*\";"
		      "    Attributes = { NoTitlebar = NO; }; }"
		      " );"
		      "}");

	w = make_wwin("xterm", "XTerm", "Special Edit", NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 0);

	memset(&base_attr, 0, sizeof(base_attr));
	memset(&base_mask, 0, sizeof(base_mask));
	wDefaultFillAttributes("xterm", "XTerm", &base_attr, &base_mask, false);
	CHECK(base_attr.no_titlebar == 1);
	CHECK(base_mask.no_titlebar == 1);

	w = make_wwin("xterm", "XTerm", "Plain", NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 1);

	set_attr_dict("{"
		      " DynamicRules = ("
		      "  { MatchTitle = \".*Special.*\";"
		      "    Attributes = { NoTitlebar = YES; }; }"
		      " );"
		      "}");
	w = make_wwin(NULL, NULL, "Special Edit", NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 1);

	w = make_wwin(NULL, NULL, "Plain", NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0xff, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 0);
}

static void test_dynamic_env_substitution(void)
{
	WWindowAttributes attr, mask;
	WWindow w;
	char *icon;

	setenv("WM_TEST_FOO", "bar", 1);
	set_attr_dict("{"
		      " DynamicRules = ("
		      "  { MatchTitle = \"^$_WM_TEST_FOO$\";"
		      "    Attributes = { NoTitlebar = YES; }; }"
		      " );"
		      "}");

	w = make_wwin(NULL, NULL, "bar", NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 1);

	w = make_wwin(NULL, NULL, "baz", NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0xff, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 0);

	setenv("WM_TEST_FOO", "a.b*c", 1);
	set_attr_dict("{"
		      " DynamicRules = ("
		      "  { MatchTitle = \"^$_WM_TEST_FOO$\";"
		      "    Attributes = { NoTitlebar = YES; }; }"
		      " );"
		      "}");
	w = make_wwin(NULL, NULL, "a.b*c", NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 1);
	w = make_wwin(NULL, NULL, "abc", NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0xff, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 0);
	w = make_wwin(NULL, NULL, "axbxc", NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0xff, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 0);

	setenv("WM_TEST_HOST", "build-1.example.com", 1);
	set_attr_dict("{"
		      " DynamicRules = ("
		      "  { MatchTitle = \".*@$_WM_TEST_HOST:.*\";"
		      "    Attributes = { NoTitlebar = YES; }; }"
		      " );"
		      "}");
	w = make_wwin(NULL, NULL, "user@build-1.example.com: ~", NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 1);
	w = make_wwin(NULL, NULL, "user@other-host: ~", NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0xff, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 0);

	unsetenv("WM_TEST_UNSET");
	set_attr_dict("{"
		      " DynamicRules = ("
		      "  { MatchTitle = \"^$_WM_TEST_UNSET$\";"
		      "    Attributes = { NoTitlebar = YES; }; }"
		      " );"
		      "}");
	w = make_wwin(NULL, NULL, "", NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 1);
	w = make_wwin(NULL, NULL, "x", NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0xff, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 0);

	set_attr_dict("{"
		      " DynamicRules = ("
		      "  { MatchTitle = \"^ab$\";"
		      "    Attributes = { NoTitlebar = YES; }; }"
		      " );"
		      "}");
	w = make_wwin(NULL, NULL, "ab", NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 1);
	w = make_wwin(NULL, NULL, "abc", NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0xff, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 0);

	setenv("WM_TEST_HOME", "/home", 1);
	set_attr_dict("{"
		      " \"*\" = { Icon = \"$_WM_TEST_HOME/myicon.png\"; };"
		      "}");
	icon = wDefaultGetIconFile(NULL, NULL, true);

	CHECK_STR(icon, "$_WM_TEST_HOME/myicon.png");

	unsetenv("WM_TEST_FOO");
	unsetenv("WM_TEST_HOST");
	unsetenv("WM_TEST_HOME");
}

static void test_dynamic_env_bracket_passthrough(void)
{
	WWindowAttributes attr, mask;
	WWindow w;

	setenv("WM_TEST_FOO", "abc", 1);
	set_attr_dict("{"
		      " DynamicRules = ("
		      "  { MatchTitle = \"[$_WM_TEST_FOO]\";"
		      "    Attributes = { NoTitlebar = YES; }; }"
		      " );"
		      "}");

	w = make_wwin(NULL, NULL, "$", NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 1);
	w = make_wwin(NULL, NULL, "a", NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0xff, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 0);

	/* []]: ] at first position is literal, so $_WM_TEST_FOO
	 * inside must not be substituted */
	set_attr_dict("{"
		      " DynamicRules = ("
		      "  { MatchTitle = \"[]$_WM_TEST_FOO]\";"
		      "    Attributes = { NoTitlebar = YES; }; }"
		      " );"
		      "}");
	/* unsubstituted set is ], $, _, W, M, T, E, S, F, O */
	w = make_wwin(NULL, NULL, "]", NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 1);
	w = make_wwin(NULL, NULL, "a", NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0xff, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 0);

	/* [^]]: ] right after ^ is literal. Without the fix ^
	 * clears bracket_first, the ] closes early, and
	 * $_WM_TEST_FOO is substituted outside the bracket */
	set_attr_dict("{"
		      " DynamicRules = ("
		      "  { MatchTitle = \"[^]$_WM_TEST_FOO]\";"
		      "    Attributes = { NoTitlebar = YES; }; }"
		      " );"
		      "}");
	/* unsubstituted [^]...] excludes ], $, _, W, M, T, E, S, F, O.
	 * If substituted (bug) it becomes [^]abc] which excludes "a" */
	w = make_wwin(NULL, NULL, "a", NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 1);
	w = make_wwin(NULL, NULL, "]", NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0xff, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 0);

	/* [[:alpha:]$_WM_TEST_FOO]: the ] in :] is part of the
	 * character class syntax, not a bracket closer. Without
	 * the fix it closes the bracket and $_WM_TEST_FOO is
	 * substituted outside */
	set_attr_dict("{"
		      " DynamicRules = ("
		      "  { MatchTitle = \"[[:alpha:]$_WM_TEST_FOO]\";"
		      "    Attributes = { NoTitlebar = YES; }; }"
		      " );"
		      "}");
	w = make_wwin(NULL, NULL, "$", NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 1);
	w = make_wwin(NULL, NULL, "!", NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0xff, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 0);

	unsetenv("WM_TEST_FOO");
}

static void test_dynamic_mark_key(void)
{
	WWindow w;
	const char *result;

	/* case 1: DynamicRules supplies a non-empty MarkKey */
	set_attr_dict("{"
		      " DynamicRules = ("
		      "  { MatchClass = \"^XTerm$\";"
		      "    Attributes = { MarkKey = \"foo\"; }; }"
		      " );"
		      "}");
	w = make_wwin("xterm", "XTerm", NULL, NULL, NULL, NULL, NULL, 0);
	result = wDefaultGetMarkKeyForWindow(&w);
	CHECK_STR(result, "foo");

	/* case 2: empty MarkKey means local-unmark, returned as "" not NULL */
	set_attr_dict("{"
		      " DynamicRules = ("
		      "  { MatchClass = \"^XTerm$\";"
		      "    Attributes = { MarkKey = \"\"; }; }"
		      " );"
		      "}");
	w = make_wwin("xterm", "XTerm", NULL, NULL, NULL, NULL, NULL, 0);
	result = wDefaultGetMarkKeyForWindow(&w);
	CHECK(result != NULL && result[0] == '\0');

	/* case 3: a matching rule that does not supply MarkKey returns NULL */
	set_attr_dict("{"
		      " DynamicRules = ("
		      "  { MatchClass = \"^XTerm$\";"
		      "    Attributes = { NoTitlebar = YES; }; }"
		      " );"
		      "}");
	w = make_wwin("xterm", "XTerm", NULL, NULL, NULL, NULL, NULL, 0);
	result = wDefaultGetMarkKeyForWindow(&w);
	CHECK(result == NULL);

	/* case 4: per-app static entry (keys directly on the per-app dict,
	 * not inside an Attributes sub-dictionary) exercises the four-level
	 * merge path */
	set_attr_dict("{"
		      " \"xterm.XTerm\" = { MarkKey = \"bar\"; };"
		      "}");
	w = make_wwin("xterm", "XTerm", NULL, NULL, NULL, NULL, NULL, 0);
	result = wDefaultGetMarkKeyForWindow(&w);
	CHECK_STR(result, "bar");

	/* case 5: a dynamic rule wins over the per-app static entry */
	set_attr_dict("{"
		      " \"xterm.XTerm\" = { MarkKey = \"bar\"; };"
		      " DynamicRules = ("
		      "  { MatchClass = \"^XTerm$\";"
		      "    Attributes = { MarkKey = \"baz\"; }; }"
		      " );"
		      "}");
	w = make_wwin("xterm", "XTerm", NULL, NULL, NULL, NULL, NULL, 0);
	result = wDefaultGetMarkKeyForWindow(&w);
	CHECK_STR(result, "baz");

	/* case 6: no matching rule and no per-app entry returns NULL */
	set_attr_dict("{}");
	w = make_wwin("urxvt", "URxvt", NULL, NULL, NULL, NULL, NULL, 0);
	result = wDefaultGetMarkKeyForWindow(&w);
	CHECK(result == NULL);
}

/* MatchTitle's source is capped at DYNRULE_MAX_TITLE_LEN bytes so a
 * user-authored ERE cannot be driven into pathological backtracking by
 * a multi-megabyte WM_NAME. Verify the cap: a marker past the
 * truncation point is invisible to the regex, a marker within the
 * first DYNRULE_MAX_TITLE_LEN bytes still matches. */
static void test_dynamic_title_cap(void)
{
	WWindowAttributes attr, mask;
	WWindow w;
	char long_title[700];

	set_attr_dict("{"
		      " DynamicRules = ("
		      "  { MatchTitle = \".*FOUNDME.*\";"
		      "    Attributes = { NoTitlebar = YES; }; }"
		      " );"
		      "}");

	/* title longer than the cap, marker placed past byte 512:
	 * the truncated match input is all 'x' and the rule must not fire */
	memset(long_title, 'x', 600);
	memcpy(long_title + 600, "FOUNDME", 7);
	memset(long_title + 607, 'x', sizeof(long_title) - 607 - 1);
	long_title[sizeof(long_title) - 1] = '\0';
	w = make_wwin(NULL, NULL, long_title, NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0xff, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 0);

	/* same length, marker placed inside the first 512 bytes:
	 * truncation keeps it and the rule fires */
	memset(long_title, 'x', 10);
	memcpy(long_title + 10, "FOUNDME", 7);
	memset(long_title + 17, 'x', sizeof(long_title) - 17 - 1);
	long_title[sizeof(long_title) - 1] = '\0';
	w = make_wwin(NULL, NULL, long_title, NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0xff, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 1);
	CHECK(mask.no_titlebar == 1);

	/* a title exactly at the cap is not truncated, so a marker
	 * sitting at the last byte still matches */
	memset(long_title, 'x', DYNRULE_MAX_TITLE_LEN - 7);
	memcpy(long_title + DYNRULE_MAX_TITLE_LEN - 7, "FOUNDME", 7);
	long_title[DYNRULE_MAX_TITLE_LEN] = '\0';
	w = make_wwin(NULL, NULL, long_title, NULL, NULL, NULL, NULL, 0);
	memset(&attr, 0xff, sizeof(attr)); memset(&mask, 0, sizeof(mask));
	wDefaultFillAttributesForWindow(&w, &attr, &mask, true);
	CHECK(attr.no_titlebar == 1);
}

static void test_frame_inner_border_sides(void)
{
	static WScreen scr;
	WWindow w;

	memset(&scr, 0, sizeof(scr));

	/* per-app static entry with a subset of sides */
	set_attr_dict("{"
		      " \"xterm.XTerm\" = { FrameInnerBorderWidth = 4;"
		      "                    FrameInnerBorderSides = (\"left\", \"top\"); };"
		      "}");
	w = make_wwin("xterm", "XTerm", NULL, NULL, NULL, NULL, NULL, 0);
	wDefaultFillFrameBorders(&scr, "xterm", "XTerm", &w);
	CHECK(w.frame_border.frame_inner_border_width == 4);
	CHECK(w.frame_border.frame_inner_border_width_set == 1);
	CHECK(w.frame_border.frame_inner_border_sides == (INNER_SIDE_LEFT | INNER_SIDE_TOP));
	CHECK(w.frame_border.frame_inner_border_sides_set == 1);

	/* a dynamic rule overrides the static entry; the dynamic lookup
	 * only runs when the window has a title/role/command/machine/type,
	 * so give it a title and match on that */
	set_attr_dict("{"
		      " \"xterm.XTerm\" = { FrameInnerBorderSides = (\"bottom\"); };"
		      " DynamicRules = ("
		      "  { MatchTitle = \".*term.*\";"
		      "    Attributes = { FrameInnerBorderSides = (\"right\"); }; }"
		      " );"
		      "}");
	w = make_wwin("xterm", "XTerm", "myterm", NULL, NULL, NULL, NULL, 0);
	wDefaultFillFrameBorders(&scr, "xterm", "XTerm", &w);
	CHECK(w.frame_border.frame_inner_border_sides == INNER_SIDE_RIGHT);
	CHECK(w.frame_border.frame_inner_border_sides_set == 1);

	/* an empty sides array resolves to all four */
	set_attr_dict("{"
		      " \"xterm.XTerm\" = { FrameInnerBorderSides = (); };"
		      "}");
	w = make_wwin("xterm", "XTerm", NULL, NULL, NULL, NULL, NULL, 0);
	wDefaultFillFrameBorders(&scr, "xterm", "XTerm", &w);
	CHECK(w.frame_border.frame_inner_border_sides == INNER_SIDE_ALL);
	CHECK(w.frame_border.frame_inner_border_sides_set == 1);

	/* no entry leaves the override unset, falling back to the screen */
	set_attr_dict("{}");
	w = make_wwin("urxvt", "URxvt", NULL, NULL, NULL, NULL, NULL, 0);
	wDefaultFillFrameBorders(&scr, "urxvt", "URxvt", &w);
	CHECK(w.frame_border.frame_inner_border_sides_set == 0);
}

int main(void)
{
	/* w_global.config is a pointer the real WM initializes; give the
	 * test a zeroed WConfig so get_icon_filename() can read icon_size
	 * without dereferencing NULL */
	static struct WConfig test_config;

	w_global.config = &test_config;

	test_schema_count();
	test_global_default();
	test_precedence();
	test_icon_and_workspace();
	test_start_workspaces_plural();
	test_change_icon_reloads_cache();
	test_empty_class();
	test_case_sensitivity();
	test_hicolor_fallback();
	test_hicolor_enforce_margin();
	test_dynamic_single_predicate();
	test_dynamic_multi_predicate_and();
	test_dynamic_first_match_wins();
	test_dynamic_empty_block_rejected();
	test_dynamic_missing_attributes_no_pollution();
	test_dynamic_malformed_regex_skipped();
	test_dynamic_window_type_multi();
	test_dynamic_reeval_and_reversion();
	test_dynamic_env_substitution();
	test_dynamic_env_bracket_passthrough();
	test_dynamic_mark_key();
	test_dynamic_title_cap();
	test_frame_inner_border_sides();

	set_attr_dict(NULL);

	return check_report("check_wdefaults");
}
