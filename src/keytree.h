/* keytree.h - Trie (prefix tree) for key-chain bindings
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

#ifndef WMKEYTREE_H
#define WMKEYTREE_H

#include <xcb/xcb.h>


/*
 * Each key in a binding sequence occupies one node in the trie.
 * Internal nodes (first_child != NULL) represent a prefix that has been
 * typed so far, leaf nodes carry the action payload.
 */

typedef enum {
	WKN_WKBD,   /* action: wKeyBindings command (WKBD_* index) */
	WKN_MENU    /* action: root-menu entry callback */
} WKeyActionType;

/*
 * A single action attached to a trie leaf node. Multiple actions may share
 * the same key sequence and are chained through the 'next' pointer, allowing
 * one key press to trigger several commands simultaneously.
 */
typedef struct WKeyAction {
	WKeyActionType type;
	union {
		int wkbd_idx;   /* WKN_WKBD: WKBD_* enum value */
		struct {
			void *menu;   /* WKN_MENU: cast to WMenu */
			void *entry;  /* WKN_MENU: cast to WMenuEntry */
		} menu;
	} u;
	struct WKeyAction *next;  /* next action for this key sequence, or NULL */
} WKeyAction;

typedef struct WKeyNode {
	unsigned int  modifier;
	xcb_keycode_t keycode;

	WKeyAction *actions;   /* non-NULL only for leaf nodes (first_child == NULL) */

	struct WKeyNode *parent;
	struct WKeyNode *first_child;   /* first key of next step in chain */
	struct WKeyNode *next_sibling;  /* alternative binding at same depth */
} WKeyNode;

/* Global trie root */
extern WKeyNode *wKeyTreeRoot;

/*
 * Insert a key sequence into *root.
 *   mods[0]/keys[0] - root (leader) key
 *   mods[1..n-1]/keys[1..n-1] - follower keys
 * Shared prefixes are merged automatically.
 * Returns the leaf node (caller must set its type/payload).
 * Returns NULL if nkeys <= 0.
 */
WKeyNode *wKeyTreeInsert(WKeyNode **root, unsigned int *mods, xcb_keycode_t *keys, int nkeys);

/*
 * Find the first sibling in the list starting at 'siblings' that matches
 * (mod, key). Returns NULL if not found.
 */
WKeyNode *wKeyTreeFind(WKeyNode *siblings, unsigned int mod, xcb_keycode_t key);

/*
 * Recursively free the entire subtree rooted at 'node'.
 */
void wKeyTreeDestroy(WKeyNode *node);

/*
 * Allocate a new WKeyAction of the given type, append it to leaf->actions,
 * and return it for the caller to set the payload (wkbd_idx or menu.*).
 * Multiple calls with the same leaf accumulate actions in insertion order.
 */
WKeyAction *wKeyNodeAddAction(WKeyNode *leaf, WKeyActionType type);

/*
 * Remove every WKN_MENU action bound to 'menu' from the sibling list at
 * *list, recursing into children, then drop nodes left with neither
 * actions nor children so removed key sequences stop matching.  'keep'
 * is never freed even when empty: callers pass the current key-chain
 * position, which must stay valid across the removal.
 */
void wKeyTreeRemoveMenu(WKeyNode **list, const void *menu, const WKeyNode *keep);

/*
 * Same as wKeyTreeRemoveMenu() for a whole set of menus in a single
 * traversal, so tearing down many shortcut-bearing menus at once does
 * not re-walk the trie per menu.  'menus' points to 'nmenus' menu
 * pointers sorted in ascending address order.  Surviving actions keep
 * their relative order and 'keep' is never freed even when empty.
 */
void wKeyTreeRemoveMenus(WKeyNode **list, void **menus, int nmenus, const WKeyNode *keep);

/*
 * Drop 'node' when it holds neither actions nor children and repeat on
 * its parent, pruning toward the root so a torn-down chain cannot keep
 * masking a shorter binding stored further up.  Nodes holding an
 * action survive, even without children.
 */
void wKeyTreePruneEmpty(WKeyNode *node);

#endif /* WMKEYTREE_H */
