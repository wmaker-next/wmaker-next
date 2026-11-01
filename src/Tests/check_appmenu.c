/*
 *  check_appmenu - unit tests for the client-owned WMMenu protocol parser
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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <string.h>

#include "appmenuparse.h"
#include "check_common.h"

static void test_normal_and_double_items(void)
{
	static const char *const lines[] = {
		"1 0 Test",
		"10 0 1 1 Plain",
		"11 0 2 1 Ctrl+W Double",
		"2 0"
	};
	const char *bad = NULL;
	WAppMenuNode *root = NULL;
	WAppMenuNode *child;

	CHECK(wAppMenuParse(lines, 4, &bad, &root) == WAPPMENU_OK);
	CHECK(root != NULL);
	if (!root)
		return;

	CHECK(root->kind == WAM_KIND_MENU);
	CHECK(strcmp(root->title, "Test") == 0);

	child = root->first_child;
	CHECK(child != NULL);
	if (child) {
		CHECK(child->kind == WAM_KIND_ITEM);
		CHECK(child->tag == 1);
		CHECK(strcmp(child->title, "Plain") == 0);
		CHECK(child->rtext == NULL);
	}

	child = child ? child->next : NULL;
	CHECK(child != NULL);
	if (child) {
		CHECK(child->kind == WAM_KIND_DOUBLE_ITEM);
		CHECK(child->tag == 2);
		CHECK(strcmp(child->title, "Double") == 0);
		CHECK(child->rtext != NULL);
		if (child->rtext)
			CHECK(strcmp(child->rtext, "Ctrl+W") == 0);
	}

	wAppMenuFreeNode(root);
}

int main(void)
{
	test_normal_and_double_items();

	return check_report("appmenu parse");
}
