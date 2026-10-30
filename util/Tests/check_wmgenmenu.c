/*
 *  check_wmgenmenu.c
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
 * check_wmgenmenu - exec wmgenmenu, capture its plist stdout, parse it,
 * and assert structural anchors (root title "Window Maker", "Find..."
 * SHEXEC entry, "Exit Window Maker" EXIT entry, "Appearance" subtree).
 *
 * Links only libWUtil (for wspawnvp_capture and proplist helpers), no X.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <WINGs/WUtil.h>
#include "check_common.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* A submenu is an array whose first element is the title string. */
static WMPropList *find_submenu(WMPropList *root, const char *title)
{
	int i, count;
	if (!root || !WMIsPLArray(root))
		return NULL;
	count = WMGetPropListItemCount(root);
	for (i = 0; i < count; i++) {
		WMPropList *elem = WMGetFromPLArray(root, i);
		WMPropList *first;
		if (!elem || !WMIsPLArray(elem))
			continue;
		first = WMGetFromPLArray(elem, 0);
		if (first && WMIsPLString(first) &&
		    strcmp(WMGetFromPLString(first), title) == 0)
			return elem;
	}
	return NULL;
}

/* Check if a sub-array with the given title exists at the top level. */
static int array_has_string(WMPropList *arr, const char *name)
{
	return find_submenu(arr, name) != NULL;
}

/* Check that a submenu array contains the given command token string
 * (e.g. "SHEXEC", "EXIT", "EXEC", "RESTART") as a direct element. */
static int submenu_has_token(WMPropList *submenu, const char *token)
{
	int i, count;
	if (!submenu || !WMIsPLArray(submenu))
		return 0;
	count = WMGetPropListItemCount(submenu);
	for (i = 1; i < count; i++) {
		WMPropList *elem = WMGetFromPLArray(submenu, i);
		if (elem && WMIsPLString(elem) &&
		    strcmp(WMGetFromPLString(elem), token) == 0)
			return 1;
	}
	return 0;
}

int main(void)
{
	const char *top_builddir;
	char wmgenmenu_path[PATH_MAX];
	char cmd[PATH_MAX + 16];
	char abs[PATH_MAX];
	WMPropList *plist;
	WMPropList *first;
	WMPropList *submenu;

	top_builddir = getenv("top_builddir");
	if (!top_builddir)
		top_builddir = ".";
	snprintf(wmgenmenu_path, sizeof(wmgenmenu_path),
		 "%s/util/.libs/wmgenmenu", top_builddir);

	/* Resolve to an absolute path so the shell run by WMReadPropListFromPipe
	 * finds the binary regardless of the test harness CWD. */
	if (realpath(wmgenmenu_path, abs))
		snprintf(wmgenmenu_path, sizeof(wmgenmenu_path), "%s", abs);

	/* wmgenmenu only reads $PATH (and $NLSPATH); set a minimal PATH
	 * so the Applications submenu is empty or absent (environment-
	 * dependent) and the always-present entries are the anchors. */
	setenv("PATH", "/bin", 1);

	/* WMReadPropListFromPipe runs the command via /bin/sh -c and
	 * captures stdout, parsing it as a plist. */
	snprintf(cmd, sizeof(cmd), "PATH=/bin '%s'", wmgenmenu_path);
	plist = WMReadPropListFromPipe(cmd);
	CHECK(plist != NULL);
	CHECK(WMIsPLArray(plist));
	if (!plist || !WMIsPLArray(plist))
		return check_report("check_wmgenmenu");

	/* First element is the root title "Window Maker". */
	first = WMGetFromPLArray(plist, 0);
	CHECK(first != NULL);
	CHECK(WMIsPLString(first));
	CHECK_STR(WMGetFromPLString(first), "Window Maker");

	/* "Find..." entry with SHEXEC is always present. */
	submenu = find_submenu(plist, "Find...");
	CHECK(submenu != NULL);
	CHECK(submenu_has_token(submenu, "SHEXEC"));

	/* "Appearance" subtree is always present. */
	submenu = find_submenu(plist, "Appearance");
	CHECK(submenu != NULL);

	/* "Exit Window Maker" with EXIT is always present. */
	submenu = find_submenu(plist, "Exit Window Maker");
	CHECK(submenu != NULL);
	CHECK(submenu_has_token(submenu, "EXIT"));

	/* "Workspaces" is always present. */
	CHECK(array_has_string(plist, "Workspaces"));

	/* "Info Panel" is always present. */
	CHECK(array_has_string(plist, "Info Panel"));

	/* "Restart Window Maker" with RESTART is always present. */
	submenu = find_submenu(plist, "Restart Window Maker");
	CHECK(submenu != NULL);
	CHECK(submenu_has_token(submenu, "RESTART"));

	/* "Configure Window Maker" with EXEC is always present. */
	submenu = find_submenu(plist, "Configure Window Maker");
	CHECK(submenu != NULL);

	WMReleasePropList(plist);

	return check_report("check_wmgenmenu");
}
