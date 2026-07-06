/*
 * check_wdefaults - tests for the typed WMWindowAttributes cache.
 */

#include "wconfig.h"

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

struct WPreferences wPreferences;
struct wmaker_global_variables w_global;

/* --- stubs for wdefaults.c externs not exercised here --- */

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

Bool UpdateDomainFile(WDDomain *domain)
{
	(void)domain;
	return True;
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

char *FindImage(const char *paths, const char *file)
{
	(void)paths; (void)file;
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
	WMPLSetCaseSensitive(True);
	test_db.dictionary = desc ? WMCreatePropListFromDescription(desc) : NULL;
	WMPLSetCaseSensitive(False);
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
	wDefaultFillAttributes("xterm", "XTerm", &attr, &mask, True);
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
	wDefaultFillAttributes("xterm", "XTerm", &attr, &mask, False);
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
	wDefaultFillAttributes("xterm", "XTerm", &attr, &mask, True);
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
	wDefaultFillAttributes("xterm", "XTerm", &attr, &mask, False);
	CHECK(attr.omnipresent == 0);
	CHECK(mask.omnipresent == 1);   /* instance.class set it */
	CHECK(attr.no_titlebar == 0);   /* class set NO */
	CHECK(mask.no_titlebar == 1);
	CHECK(mask.no_appicon == 0);    /* nobody but "*"-default sets this */

	/* an unrelated window only sees "*" with useGlobalDefault */
	memset(&attr, 0, sizeof(attr));
	wDefaultFillAttributes("urxvt", "URxvt", &attr, NULL, True);
	CHECK(attr.no_titlebar == 1);
	CHECK(attr.floating == 1);
	CHECK(attr.omnipresent == 1);
	CHECK(attr.sunken == 0);
}

static void test_icon_and_workspace(void)
{
	char *icon;

	set_attr_dict("{"
		      " \"*\" = { Icon = \"any.png\"; StartWorkspace = 4; };"
		      " XTerm = { Icon = \"class.png\"; };"
		      " xterm = { StartWorkspace = 2; };"
		      " \"xterm.XTerm\" = { NoTitlebar = YES; };"
		      "}");

	icon = wDefaultGetIconFile("xterm", "XTerm", False);
	CHECK(icon && strcmp(icon, "class.png") == 0);
	icon = wDefaultGetIconFile("xterm", NULL, False);
	CHECK(icon == NULL);
	icon = wDefaultGetIconFile("xterm", NULL, True);
	CHECK(icon && strcmp(icon, "any.png") == 0);
	icon = wDefaultGetIconFile(NULL, NULL, True);
	CHECK(icon && strcmp(icon, "any.png") == 0);

	CHECK(wDefaultGetStartWorkspace(NULL, "xterm", "XTerm") == 1);
	CHECK(wDefaultGetStartWorkspace(NULL, NULL, "XTerm") == 3);
	CHECK(wDefaultGetStartWorkspace(NULL, "urxvt", "URxvt") == 3);
}

static void test_change_icon_reloads_cache(void)
{
	char *icon;

	set_attr_dict("{ \"xterm.XTerm\" = { Icon = \"old.png\"; }; }");

	icon = wDefaultGetIconFile("xterm", "XTerm", False);
	CHECK(icon && strcmp(icon, "old.png") == 0);

	wDefaultChangeIcon("xterm", "XTerm", "new.png");
	icon = wDefaultGetIconFile("xterm", "XTerm", False);
	CHECK(icon && strcmp(icon, "new.png") == 0);

	wDefaultChangeIcon("xterm", "XTerm", NULL);
	icon = wDefaultGetIconFile("xterm", "XTerm", False);
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
	wDefaultFillAttributes("", "XTerm", &attr, NULL, True);
	CHECK(attr.no_titlebar == 0);     /* class entry, not "*" */
	CHECK(attr.floating == 1);        /* class entry */
	CHECK(attr.no_border == 0);       /* ".XTerm" must NOT match */
	CHECK(attr.omnipresent == 0);     /* "" must NOT match */

	memset(&attr, 0, sizeof(attr));
	wDefaultFillAttributes("", "", &attr, NULL, True);
	CHECK(attr.no_titlebar == 1);     /* falls through to "*" */
	CHECK(attr.omnipresent == 0);     /* "" must NOT match */

	icon = wDefaultGetIconFile("", "", True);
	CHECK(icon && strcmp(icon, "any.png") == 0);   /* not "dot.png" */
	icon = wDefaultGetIconFile("", "", False);
	CHECK(icon == NULL);

	/* writing under ("","") goes to "*", and ("","XTerm") to "XTerm",
	 * symmetric with the read side */
	wDefaultChangeIcon("", "XTerm", "class.png");
	icon = wDefaultGetIconFile(NULL, "XTerm", False);
	CHECK(icon && strcmp(icon, "class.png") == 0);
}

static void test_case_sensitivity(void)
{
	WWindowAttributes attr;

	set_attr_dict("{ XTerm = { NoTitlebar = YES; }; }");

	memset(&attr, 0, sizeof(attr));
	wDefaultFillAttributes(NULL, "XTerm", &attr, NULL, True);
	CHECK(attr.no_titlebar == 1);

	memset(&attr, 0, sizeof(attr));
	wDefaultFillAttributes(NULL, "xterm", &attr, NULL, True);
	CHECK(attr.no_titlebar == 0);
}

static void test_schema_count(void)
{
	/* uint32_t bitset, the static assert in wdefaults.c already
	 * covers the upper bound, this catches accidental empty list */
	CHECK(WATTR_BOOL_COUNT >= 28);
	CHECK(WATTR_BOOL_COUNT <= 32);
}

int main(void)
{
	test_schema_count();
	test_global_default();
	test_precedence();
	test_icon_and_workspace();
	test_change_icon_reloads_cache();
	test_empty_class();
	test_case_sensitivity();

	set_attr_dict(NULL);

	return check_report("check_wdefaults");
}
