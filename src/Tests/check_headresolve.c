/*
 *  check_headresolve.c
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

/* tests for the pure head resolver */

#include "wconfig.h"

#include <limits.h>
#include <stdio.h>

#include "headresolve.h"

#include "check_common.h"

static void test_index_parse(void)
{
	int idx;

	CHECK(wHeadIndexParse("#0", &idx) && idx == 0);
	CHECK(wHeadIndexParse("#5", &idx) && idx == 5);
	CHECK(wHeadIndexParse("#007", &idx) && idx == 7);
	CHECK(wHeadIndexParse("#2147483647", &idx) && idx == INT_MAX);

	CHECK(!wHeadIndexParse("#", &idx));
	CHECK(!wHeadIndexParse("#5abc", &idx));
	CHECK(!wHeadIndexParse("#5 ", &idx));
	CHECK(!wHeadIndexParse("#5#5", &idx));
	CHECK(!wHeadIndexParse("#+5", &idx));
	CHECK(!wHeadIndexParse("#-1", &idx));
	CHECK(!wHeadIndexParse(" #0", &idx));
	CHECK(!wHeadIndexParse("# 0", &idx));
	CHECK(!wHeadIndexParse("#0\t", &idx));
	CHECK(!wHeadIndexParse("#2147483648", &idx));
	CHECK(!wHeadIndexParse("#99999999999999999999", &idx));
	CHECK(!wHeadIndexParse("", &idx));
	CHECK(!wHeadIndexParse(NULL, &idx));
	CHECK(!wHeadIndexParse("0", &idx));
	CHECK(!wHeadIndexParse("HDMI-1", &idx));
}

static void test_exact_names(void)
{
	static const char * const names[] = { "HDMI-1", "DP-2", NULL };

	CHECK(wHeadResolveFromNames((const char *const *)names, 3, "HDMI-1") == 0);
	CHECK(wHeadResolveFromNames((const char *const *)names, 3, "DP-2") == 1);

	/* name matching is case-sensitive */
	CHECK(wHeadResolveFromNames((const char *const *)names, 3, "hdmi-1") == -1);
	CHECK(wHeadResolveFromNames((const char *const *)names, 3, "DP-2 ") == -1);

	CHECK(wHeadResolveFromNames((const char *const *)names, 3, "") == -1);
	CHECK(wHeadResolveFromNames((const char *const *)names, 3, NULL) == -1);
	CHECK(wHeadResolveFromNames((const char *const *)names, 3, "VGA-9") == -1);
}

static void test_hash_names_win(void)
{
	/* a name starting with '#' still wins over the index rule */
	static const char * const named[] = { NULL, "#0", NULL };
	static const char * const plain[] = { "HDMI-1", NULL };

	CHECK(wHeadResolveFromNames((const char *const *)named, 3, "#0") == 1);
	/* the index of a named head is refused */
	CHECK(wHeadResolveFromNames((const char *const *)named, 3, "#1") == -1);
	CHECK(wHeadResolveFromNames((const char *const *)named, 3, "#2") == 2);
	CHECK(wHeadResolveFromNames((const char *const *)named, 3, "#3") == -1);

	CHECK(wHeadResolveFromNames((const char *const *)plain, 2, "#0") == -1);
	CHECK(wHeadResolveFromNames((const char *const *)plain, 2, "#1") == 1);
}

static void test_unnamed_index(void)
{
	static const char * const names[] = { NULL, NULL, NULL };

	CHECK(wHeadResolveFromNames((const char *const *)names, 3, "#0") == 0);
	CHECK(wHeadResolveFromNames((const char *const *)names, 3, "#2") == 2);

	CHECK(wHeadResolveFromNames((const char *const *)names, 3, "#3") == -1);
	CHECK(wHeadResolveFromNames((const char *const *)names, 3, "#2147483647") == -1);

	CHECK(wHeadResolveFromNames((const char *const *)names, 3, "#") == -1);
	CHECK(wHeadResolveFromNames((const char *const *)names, 3, "#5abc") == -1);
	CHECK(wHeadResolveFromNames((const char *const *)names, 3, "#+1") == -1);
	CHECK(wHeadResolveFromNames((const char *const *)names, 3, "#-1") == -1);
	CHECK(wHeadResolveFromNames((const char *const *)names, 3, "#2147483648") == -1);

	CHECK(wHeadResolveFromNames((const char *const *)names, 0, "#0") == -1);
	CHECK(wHeadResolveFromNames(NULL, 3, "#0") == -1);
}

/* one retained value across absent, appearing and reordered names */
static void test_topology_changes(void)
{
	static const char * const before[] = { "HDMI-1", "DP-2", NULL };
	static const char * const after[] = { "DP-2", "HDMI-1", NULL };
	static const char * const gone[] = { "eDP-1", NULL };
	static const char * const empty[] = { NULL };

	CHECK(wHeadResolveFromNames((const char *const *)before, 3, "DP-2") == 1);
	CHECK(wHeadResolveFromNames((const char *const *)after, 3, "DP-2") == 0);
	CHECK(wHeadResolveFromNames((const char *const *)gone, 2, "DP-2") == -1);
	CHECK(wHeadResolveFromNames((const char *const *)empty, 1, "DP-2") == -1);
}

static void test_select_keywords(void)
{
	static const char * const plain[] = { "HDMI-1", "DP-2", NULL };
	static const char * const collide[] = { "Cursor", "Window", "cursor", NULL };
	WHeadSelection sel;

	sel = wHeadSelectFromNames((const char *const *)plain, 3, "");
	CHECK(sel.kind == WHEAD_SEL_POINTER);
	sel = wHeadSelectFromNames((const char *const *)plain, 3, NULL);
	CHECK(sel.kind == WHEAD_SEL_POINTER);

	sel = wHeadSelectFromNames((const char *const *)plain, 3, "Cursor");
	CHECK(sel.kind == WHEAD_SEL_POINTER);
	sel = wHeadSelectFromNames((const char *const *)plain, 3, "cursor");
	CHECK(sel.kind == WHEAD_SEL_POINTER);
	sel = wHeadSelectFromNames((const char *const *)plain, 3, "CURSOR");
	CHECK(sel.kind == WHEAD_SEL_POINTER);
	sel = wHeadSelectFromNames((const char *const *)plain, 3, "Window");
	CHECK(sel.kind == WHEAD_SEL_WINDOW);
	sel = wHeadSelectFromNames((const char *const *)plain, 3, "window");
	CHECK(sel.kind == WHEAD_SEL_WINDOW);
	sel = wHeadSelectFromNames((const char *const *)plain, 3, "WINDOW");
	CHECK(sel.kind == WHEAD_SEL_WINDOW);

	/* the keywords stay dynamic against colliding monitor names */
	sel = wHeadSelectFromNames((const char *const *)collide, 3, "Cursor");
	CHECK(sel.kind == WHEAD_SEL_POINTER);
	sel = wHeadSelectFromNames((const char *const *)collide, 3, "Window");
	CHECK(sel.kind == WHEAD_SEL_WINDOW);
	sel = wHeadSelectFromNames((const char *const *)collide, 3, "cursor");
	CHECK(sel.kind == WHEAD_SEL_POINTER);
	sel = wHeadSelectFromNames((const char *const *)plain, 3, "Window");
	CHECK(sel.kind == WHEAD_SEL_WINDOW);
}

static void test_select_name_escape(void)
{
	static const char * const names[] = { "Cursor", "#0", "name:foo", NULL };
	WHeadSelection sel;

	sel = wHeadSelectFromNames((const char *const *)names, 3, "name:Cursor");
	CHECK(sel.kind == WHEAD_SEL_HEAD && sel.head == 0);
	sel = wHeadSelectFromNames((const char *const *)names, 3, "name:#0");
	CHECK(sel.kind == WHEAD_SEL_HEAD && sel.head == 1);
	sel = wHeadSelectFromNames((const char *const *)names, 3, "name:name:foo");
	CHECK(sel.kind == WHEAD_SEL_HEAD && sel.head == 2);
	/* name:foo means the monitor foo, not one called name:foo */
	sel = wHeadSelectFromNames((const char *const *)names, 3, "name:foo");
	CHECK(sel.kind == WHEAD_SEL_UNRESOLVED);

	sel = wHeadSelectFromNames((const char *const *)names, 3, "name:cursor");
	CHECK(sel.kind == WHEAD_SEL_UNRESOLVED);

	sel = wHeadSelectFromNames((const char *const *)names, 3, "name:");
	CHECK(sel.kind == WHEAD_SEL_UNRESOLVED);

	sel = wHeadSelectFromNames((const char *const *)names, 3, "Name:Cursor");
	CHECK(sel.kind == WHEAD_SEL_UNRESOLVED);
}

static void test_select_plain_and_index(void)
{
	static const char * const named[] = { "HDMI-1", NULL, "#0" };
	static const char * const none[] = { NULL, NULL };
	WHeadSelection sel;

	sel = wHeadSelectFromNames((const char *const *)named, 3, "HDMI-1");
	CHECK(sel.kind == WHEAD_SEL_HEAD && sel.head == 0);
	sel = wHeadSelectFromNames((const char *const *)named, 3, "#0");
	CHECK(sel.kind == WHEAD_SEL_HEAD && sel.head == 2);
	sel = wHeadSelectFromNames((const char *const *)named, 3, "#1");
	CHECK(sel.kind == WHEAD_SEL_HEAD && sel.head == 1);
	sel = wHeadSelectFromNames((const char *const *)named, 3, "#2");
	CHECK(sel.kind == WHEAD_SEL_UNRESOLVED);

	sel = wHeadSelectFromNames((const char *const *)named, 3, "VGA-9");
	CHECK(sel.kind == WHEAD_SEL_UNRESOLVED);
	sel = wHeadSelectFromNames((const char *const *)named, 3, "#5abc");
	CHECK(sel.kind == WHEAD_SEL_UNRESOLVED);
	sel = wHeadSelectFromNames((const char *const *)named, 3, "#2147483648");
	CHECK(sel.kind == WHEAD_SEL_UNRESOLVED);

	sel = wHeadSelectFromNames((const char *const *)none, 2, "#0");
	CHECK(sel.kind == WHEAD_SEL_HEAD && sel.head == 0);
	sel = wHeadSelectFromNames((const char *const *)none, 2, "#1");
	CHECK(sel.kind == WHEAD_SEL_HEAD && sel.head == 1);
	sel = wHeadSelectFromNames((const char *const *)none, 2, "#2");
	CHECK(sel.kind == WHEAD_SEL_UNRESOLVED);
}

static void test_frame_rect(void)
{
	WMRect heads[2];
	WMRect frame;

	heads[0].pos.x = 0;
	heads[0].pos.y = 0;
	heads[0].size.width = 512;
	heads[0].size.height = 768;
	heads[1].pos.x = 512;
	heads[1].pos.y = 0;
	heads[1].size.width = 512;
	heads[1].size.height = 768;

	frame.pos.x = 100;
	frame.pos.y = 100;
	frame.size.width = 200;
	frame.size.height = 200;
	CHECK(wHeadForFrameRect(heads, 2, frame) == 0);
	frame.pos.x = 600;
	CHECK(wHeadForFrameRect(heads, 2, frame) == 1);

	/* a spanning frame takes the greater intersection */
	frame.pos.x = 312;
	frame.size.width = 200;
	CHECK(wHeadForFrameRect(heads, 2, frame) == 0);
	frame.pos.x = 413;
	CHECK(wHeadForFrameRect(heads, 2, frame) == 1);

	frame.pos.x = -500;
	frame.pos.y = -500;
	CHECK(wHeadForFrameRect(heads, 2, frame) == -1);

	frame.pos.x = 10;
	frame.pos.y = 10;
	frame.size.width = 0;
	frame.size.height = 100;
	CHECK(wHeadForFrameRect(heads, 2, frame) == -1);
	frame.size.width = 100;
	frame.size.height = 0;
	CHECK(wHeadForFrameRect(heads, 2, frame) == -1);

	frame.size.width = 100;
	frame.size.height = 100;
	CHECK(wHeadForFrameRect(heads, 0, frame) == -1);
	CHECK(wHeadForFrameRect(NULL, 2, frame) == -1);

	heads[0].pos.x = -1024;
	heads[0].pos.y = -768;
	heads[0].size.width = 1024;
	heads[0].size.height = 768;
	frame.pos.x = -900;
	frame.pos.y = -700;
	frame.size.width = 100;
	frame.size.height = 100;
	CHECK(wHeadForFrameRect(heads, 1, frame) == 0);
}

int main(void)
{
	test_index_parse();
	test_exact_names();
	test_hash_names_win();
	test_unnamed_index();
	test_topology_changes();
	test_select_keywords();
	test_select_name_escape();
	test_select_plain_and_index();
	test_frame_rect();

	return check_report("check_headresolve");
}
