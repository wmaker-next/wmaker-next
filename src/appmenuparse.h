/*
 *  appmenuparse - parse/validate core for the client-owned WMMenu text
 *  protocol (`_WINDOWMAKER_MENU`). Turns the "WMMenu 0" command list
 *  into a plain node tree, with no display or widget involvement, so
 *  the parser can be exercised standalone (unit tests, fuzz harness).
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

#ifndef WM_APPMENUPARSE_H_
#define WM_APPMENUPARSE_H_

/* WMMenu command codes, the wire format of the menu property */
enum {
	WAM_BEGIN_MENU = 1,
	WAM_END_MENU = 2,
	WAM_NORMAL_ITEM = 10,
	WAM_DOUBLE_ITEM = 11,
	WAM_SUBMENU_ITEM = 12
};

/* Message code the WM sends back to the client when one of its menu
 * entries is selected, a different direction from the property codes
 * above so the value 1 does not collide with WAM_BEGIN_MENU. */
enum {
	WAM_SELECT_ITEM = 1
};

/* Node kinds. WAM_KIND_MENU is the begin-menu line of a menu level,
 * the other kinds are the entries attached to it. A WAM_KIND_SUBMENU
 * entry's child list holds exactly one WAM_KIND_MENU node. */
enum {
	WAM_KIND_MENU,
	WAM_KIND_ITEM,
	WAM_KIND_DOUBLE_ITEM,
	WAM_KIND_SUBMENU
};

typedef struct WAppMenuNode {
	short kind;
	short code;		/* menu code, on WAM_MENU nodes */
	int tag;
	char *title;
	char *rtext;		/* double-item right text, NULL otherwise */
	struct WAppMenuNode *first_child;
	struct WAppMenuNode *last_child;
	struct WAppMenuNode *next;
} WAppMenuNode;

typedef enum {
	WAPPMENU_OK = 0,
	WAPPMENU_BAD_ENTRY,	/* *bad_line set to the offending line */
	WAPPMENU_TITLE_TOO_LONG,
	WAPPMENU_TOO_DEEP
} WAppMenuParseError;

/* Parse `count` command lines (the property payload after the
 * "WMMenu 0" header). Returns WAPPMENU_OK and sets *root to the node
 * tree on success, or an error code with *bad_line pointing at the
 * offending input line where one applies. An unterminated final menu
 * is accepted, trailing lines after it are ignored. */
WAppMenuParseError wAppMenuParse(const char *const *lines, int count,
				 const char **bad_line, WAppMenuNode **root);

/* Release a node tree returned by wAppMenuParse(). */
void wAppMenuFreeNode(WAppMenuNode *node);

#endif
