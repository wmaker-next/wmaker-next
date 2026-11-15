/*
 *  check_keytree - unit tests for the key-chain trie in src/keytree.c.
 *  No display needed, the key sequences and menus are fabricated.
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

#include "wconfig.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "keytree.h"

#include "check_common.h"

static int menu_a, menu_b, menu_c, menu_d;

static void add_menu_action(WKeyNode *leaf, const void *menu, void *entry)
{
	WKeyAction *act;

	act = wKeyNodeAddAction(leaf, WKN_MENU);
	act->u.menu.menu = (void *)menu;
	act->u.menu.entry = entry;
}

static int action_count(WKeyNode *leaf)
{
	WKeyAction *act;
	int n = 0;

	for (act = leaf->actions; act != NULL; act = act->next)
		n++;
	return n;
}

static int compareMenuPtr(const void *a, const void *b)
{
	uintptr_t x = (uintptr_t)*(void *const *)a;
	uintptr_t y = (uintptr_t)*(void *const *)b;

	return (x > y) - (x < y);
}

static void sortMenuSet(void **set, int n)
{
	qsort(set, n, sizeof(set[0]), compareMenuPtr);
}

static void reset_tree(void)
{
	wKeyTreeDestroy(wKeyTreeRoot);
	wKeyTreeRoot = NULL;
}

static void test_add_action_order(void)
{
	unsigned int mod = 0;
	xcb_keycode_t key = 30;
	WKeyNode *leaf;

	reset_tree();
	leaf = wKeyTreeInsert(&wKeyTreeRoot, &mod, &key, 1);
	add_menu_action(leaf, &menu_a, (void *)1);
	add_menu_action(leaf, &menu_a, (void *)2);
	add_menu_action(leaf, &menu_b, (void *)3);

	CHECK(action_count(leaf) == 3);
	CHECK(leaf->actions->u.menu.entry == (void *)1);
	CHECK(leaf->actions->next->u.menu.entry == (void *)2);
	CHECK(leaf->actions->next->next->u.menu.entry == (void *)3);
}

static void test_remove_other_menus_untouched(void)
{
	unsigned int mod = 0;
	xcb_keycode_t key = 30;
	WKeyNode *leaf;

	reset_tree();
	leaf = wKeyTreeInsert(&wKeyTreeRoot, &mod, &key, 1);
	add_menu_action(leaf, &menu_a, (void *)1);
	add_menu_action(leaf, &menu_b, (void *)2);
	add_menu_action(leaf, &menu_a, (void *)3);

	wKeyTreeRemoveMenu(&wKeyTreeRoot, &menu_a, NULL);

	CHECK(action_count(leaf) == 1);
	CHECK(leaf->actions->u.menu.menu == &menu_b);
	CHECK(leaf->actions->u.menu.entry == (void *)2);
}

static void test_remove_keeps_wkbd_actions(void)
{
	unsigned int mod = 0;
	xcb_keycode_t key = 30;
	WKeyNode *leaf;
	WKeyAction *act;

	reset_tree();
	leaf = wKeyTreeInsert(&wKeyTreeRoot, &mod, &key, 1);
	act = wKeyNodeAddAction(leaf, WKN_WKBD);
	act->u.wkbd_idx = 7;
	add_menu_action(leaf, &menu_a, (void *)1);

	wKeyTreeRemoveMenu(&wKeyTreeRoot, &menu_a, NULL);

	CHECK(action_count(leaf) == 1);
	CHECK(leaf->actions->type == WKN_WKBD);
	CHECK(leaf->actions->u.wkbd_idx == 7);
}

static void test_remove_prunes_empty_nodes(void)
{
	unsigned int mods[2] = { 0, 0 };
	xcb_keycode_t keys[2];
	WKeyNode *leaf;

	reset_tree();
	keys[0] = 30;
	keys[1] = 31;
	leaf = wKeyTreeInsert(&wKeyTreeRoot, mods, keys, 2);
	add_menu_action(leaf, &menu_a, (void *)1);

	wKeyTreeRemoveMenu(&wKeyTreeRoot, &menu_a, NULL);

	/* the whole two-key chain is gone, not just its leaf */
	CHECK(wKeyTreeRoot == NULL);
	CHECK(wKeyTreeFind(wKeyTreeRoot, mods[0], keys[0]) == NULL);
}

static void test_remove_keeps_shared_prefix(void)
{
	unsigned int mods[2] = { 0, 0 };
	xcb_keycode_t keys[2];
	WKeyNode *branch, *prefix;

	reset_tree();
	keys[0] = 30;
	keys[1] = 31;
	branch = wKeyTreeInsert(&wKeyTreeRoot, mods, keys, 2);
	add_menu_action(branch, &menu_a, (void *)1);

	keys[1] = 32;
	branch = wKeyTreeInsert(&wKeyTreeRoot, mods, keys, 2);
	add_menu_action(branch, &menu_b, (void *)2);

	wKeyTreeRemoveMenu(&wKeyTreeRoot, &menu_a, NULL);

	prefix = wKeyTreeFind(wKeyTreeRoot, mods[0], keys[0]);
	CHECK(prefix != NULL);
	CHECK(wKeyTreeFind(prefix->first_child, mods[1], 31) == NULL);
	branch = wKeyTreeFind(prefix->first_child, mods[1], 32);
	CHECK(branch != NULL);
	CHECK(branch && branch->actions->u.menu.menu == &menu_b);
}

static void test_remove_keeps_pinned_node(void)
{
	unsigned int mods[2] = { 0, 0 };
	xcb_keycode_t keys[2];
	WKeyNode *chain, *keep;

	reset_tree();
	keys[0] = 30;
	keys[1] = 31;
	chain = wKeyTreeInsert(&wKeyTreeRoot, mods, keys, 2);
	add_menu_action(chain, &menu_a, (void *)1);

	/* a key chain positioned at the prefix must stay valid across
	 * the removal even though the prefix becomes empty */
	keep = wKeyTreeFind(wKeyTreeRoot, mods[0], keys[0]);
	CHECK(keep != NULL);

	wKeyTreeRemoveMenu(&wKeyTreeRoot, &menu_a, keep);

	CHECK(wKeyTreeFind(wKeyTreeRoot, mods[0], keys[0]) == keep);
	CHECK(action_count(keep) == 0);
	CHECK(keep->first_child == NULL);
}

static void test_pinned_chain_unmasks_shorter_binding(void)
{
	unsigned int mods[3] = { 0, 0, 0 };
	xcb_keycode_t keys[3];
	WKeyNode *short_leaf, *pinned, *deep;

	reset_tree();
	keys[0] = 30;
	keys[1] = 31;
	keys[2] = 32;

	/* "30" carries a shorter binding while "30 31 32" is a chain
	 * sharing its prefix */
	short_leaf = wKeyTreeInsert(&wKeyTreeRoot, mods, keys, 1);
	add_menu_action(short_leaf, &menu_b, (void *)1);
	deep = wKeyTreeInsert(&wKeyTreeRoot, mods, keys, 3);
	add_menu_action(deep, &menu_a, (void *)2);

	/* the chain is armed at "30 31" when menu_a is torn down */
	pinned = wKeyTreeFind(short_leaf->first_child, mods[1], keys[1]);
	CHECK(pinned != NULL);

	wKeyTreeRemoveMenu(&wKeyTreeRoot, &menu_a, pinned);

	/* nothing is left to chain into: the stalled path cancels and
	 * prunes the empty node back toward the root */
	CHECK(pinned->first_child == NULL);
	wKeyTreePruneEmpty(pinned);

	CHECK(wKeyTreeRoot == short_leaf);
	CHECK(short_leaf->first_child == NULL);
	CHECK(action_count(short_leaf) == 1);
	CHECK(short_leaf->actions->u.menu.menu == &menu_b);
}

static void test_pinned_chain_keeps_shorter_binding(void)
{
	unsigned int mods[3] = { 0, 0, 0 };
	xcb_keycode_t keys[3];
	WKeyNode *prefix, *pinned, *deep;

	reset_tree();
	keys[0] = 30;
	keys[1] = 31;
	keys[2] = 32;

	/* the shorter binding sits on the pinned node itself */
	pinned = wKeyTreeInsert(&wKeyTreeRoot, mods, keys, 2);
	add_menu_action(pinned, &menu_b, (void *)1);
	deep = wKeyTreeInsert(&wKeyTreeRoot, mods, keys, 3);
	add_menu_action(deep, &menu_a, (void *)2);

	wKeyTreeRemoveMenu(&wKeyTreeRoot, &menu_a, pinned);
	wKeyTreePruneEmpty(pinned);

	/* pruning stops at the action, the shorter binding stays */
	CHECK(action_count(pinned) == 1);
	CHECK(pinned->actions->u.menu.menu == &menu_b);
	CHECK(pinned->first_child == NULL);
	prefix = wKeyTreeFind(wKeyTreeRoot, mods[0], keys[0]);
	CHECK(prefix != NULL && prefix->first_child == pinned);
}

static void test_remove_menus_one_traversal(void)
{
	unsigned int mods[2] = { 0, 0 };
	xcb_keycode_t keys[2];
	WKeyNode *leaf, *mixed, *kept;
	WKeyAction *act;
	void *set[3];

	reset_tree();

	/* two shortcut-bearing submenus share one key sequence and a
	 * third sits under the same prefix */
	keys[0] = 30;
	keys[1] = 31;
	leaf = wKeyTreeInsert(&wKeyTreeRoot, mods, keys, 2);
	add_menu_action(leaf, &menu_a, (void *)1);
	add_menu_action(leaf, &menu_b, (void *)2);
	keys[1] = 32;
	leaf = wKeyTreeInsert(&wKeyTreeRoot, mods, keys, 2);
	add_menu_action(leaf, &menu_c, (void *)3);

	/* mixed leaf: doomed action first, surviving ones after */
	keys[0] = 33;
	keys[1] = 0;
	mixed = wKeyTreeInsert(&wKeyTreeRoot, mods, keys, 1);
	add_menu_action(mixed, &menu_a, (void *)4);
	act = wKeyNodeAddAction(mixed, WKN_WKBD);
	act->u.wkbd_idx = 7;
	add_menu_action(mixed, &menu_d, (void *)5);

	/* untouched subtree */
	keys[0] = 34;
	kept = wKeyTreeInsert(&wKeyTreeRoot, mods, keys, 1);
	add_menu_action(kept, &menu_d, (void *)6);

	set[0] = &menu_a;
	set[1] = &menu_b;
	set[2] = &menu_c;
	sortMenuSet(set, 3);

	wKeyTreeRemoveMenus(&wKeyTreeRoot, set, 3, NULL);

	/* the whole shared-prefix subtree is gone */
	CHECK(wKeyTreeFind(wKeyTreeRoot, mods[0], 30) == NULL);

	/* surviving actions keep their relative order */
	CHECK(action_count(mixed) == 2);
	CHECK(mixed->actions->type == WKN_WKBD);
	CHECK(mixed->actions->u.wkbd_idx == 7);
	CHECK(mixed->actions->next->type == WKN_MENU);
	CHECK(mixed->actions->next->u.menu.menu == &menu_d);

	/* menus outside the set are untouched */
	CHECK(action_count(kept) == 1);
	CHECK(kept->actions->u.menu.menu == &menu_d);
}

int main(void)
{
	test_add_action_order();
	test_remove_other_menus_untouched();
	test_remove_keeps_wkbd_actions();
	test_remove_prunes_empty_nodes();
	test_remove_keeps_shared_prefix();
	test_remove_keeps_pinned_node();
	test_pinned_chain_unmasks_shorter_binding();
	test_pinned_chain_keeps_shorter_binding();
	test_remove_menus_one_traversal();

	reset_tree();
	return check_report("keytree");
}
