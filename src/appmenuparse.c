/*
 *  appmenuparse - parse/validate core for the client-owned WMMenu text
 *  protocol. See appmenuparse.h.
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

#include <stdio.h>
#include <string.h>

#include <WINGs/WUtil.h>

#include "appmenuparse.h"

#define MAX_APPMENU_DEPTH 64

typedef struct {
	const char *const *slist;
	int count;
	int index;
	const char *bad_line;
	int err;
} WAppMenuParseCtx;

/* An unterminated menu is accepted, matching the historical reader, so
 * only the per-entry limits bound hostile input. */
static void append_child(WAppMenuNode *menu, WAppMenuNode *entry)
{
	if (menu->last_child)
		menu->last_child->next = entry;
	else
		menu->first_child = entry;
	menu->last_child = entry;
}

static WAppMenuNode *parse_menu(WAppMenuParseCtx *ctx, int depth)
{
	char title[300];
	char rtext[300];
	WAppMenuNode *menu;
	int command, code, pos = 0;

	if (ctx->index >= ctx->count) {
		ctx->err = WAPPMENU_BAD_ENTRY;
		return NULL;
	}

	if (depth > MAX_APPMENU_DEPTH) {
		ctx->err = WAPPMENU_TOO_DEEP;
		return NULL;
	}

	if (sscanf(ctx->slist[ctx->index], "%i %i %n", &command, &code, &pos) < 2 ||
	    command != WAM_BEGIN_MENU) {
		ctx->err = WAPPMENU_BAD_ENTRY;
		ctx->bad_line = ctx->slist[ctx->index];
		return NULL;
	}
	if (wstrlcpy(title, &ctx->slist[ctx->index][pos], sizeof(title)) >= sizeof(title)) {
		ctx->err = WAPPMENU_TITLE_TOO_LONG;
		ctx->bad_line = ctx->slist[ctx->index];
		return NULL;
	}
	menu = wmalloc(sizeof(WAppMenuNode));
	menu->kind = WAM_KIND_MENU;
	menu->code = (short)code;
	menu->title = wstrdup(title);
	ctx->index++;

	while (ctx->index < ctx->count) {
		const char *line = ctx->slist[ctx->index];
		int ecode, etag, enab;

		if (sscanf(line, "%i", &command) != 1) {
			ctx->err = WAPPMENU_BAD_ENTRY;
			ctx->bad_line = line;
			wAppMenuFreeNode(menu);
			return NULL;
		}

		if (command == WAM_END_MENU) {
			ctx->index++;
			break;

		} else if (command == WAM_NORMAL_ITEM || command == WAM_DOUBLE_ITEM) {
			WAppMenuNode *entry;

			if (command == WAM_NORMAL_ITEM) {
				if (sscanf(line, "%i %i %i %i %n",
					   &command, &ecode, &etag, &enab, &pos) != 4 || ecode != code) {
					ctx->err = WAPPMENU_BAD_ENTRY;
					ctx->bad_line = line;
					wAppMenuFreeNode(menu);
					return NULL;
				}
				rtext[0] = 0;
			} else {
				if (sscanf(line, "%i %i %i %i %299s %n",
					   &command, &ecode, &etag, &enab, rtext, &pos) != 5 || ecode != code) {
					ctx->err = WAPPMENU_BAD_ENTRY;
					ctx->bad_line = line;
					wAppMenuFreeNode(menu);
					return NULL;
				}
			}
			if (wstrlcpy(title, &line[pos], sizeof(title)) >= sizeof(title)) {
				ctx->err = WAPPMENU_TITLE_TOO_LONG;
				ctx->bad_line = line;
				wAppMenuFreeNode(menu);
				return NULL;
			}
			entry = wmalloc(sizeof(WAppMenuNode));
			entry->kind = (command == WAM_NORMAL_ITEM) ? WAM_KIND_ITEM : WAM_KIND_DOUBLE_ITEM;
			entry->tag = etag;
			entry->title = wstrdup(title);
			entry->rtext = rtext[0] ? wstrdup(rtext) : NULL;
			append_child(menu, entry);
			ctx->index++;

		} else if (command == WAM_SUBMENU_ITEM) {
			WAppMenuNode *entry, *submenu;
			int ncode;

			if (sscanf(line, "%i %i %i %i %i %n",
				   &command, &ecode, &etag, &enab, &ncode, &pos) != 5 || ecode != code) {
				ctx->err = WAPPMENU_BAD_ENTRY;
				ctx->bad_line = line;
				wAppMenuFreeNode(menu);
				return NULL;
			}
			if (wstrlcpy(title, &line[pos], sizeof(title)) >= sizeof(title)) {
				ctx->err = WAPPMENU_TITLE_TOO_LONG;
				ctx->bad_line = line;
				wAppMenuFreeNode(menu);
				return NULL;
			}
			ctx->index++;

			if (ctx->index >= ctx->count) {
				ctx->err = WAPPMENU_BAD_ENTRY;
				ctx->bad_line = line;
				wAppMenuFreeNode(menu);
				return NULL;
			}

			submenu = parse_menu(ctx, depth + 1);
			if (!submenu) {
				wAppMenuFreeNode(menu);
				return NULL;
			}

			entry = wmalloc(sizeof(WAppMenuNode));
			entry->kind = WAM_KIND_SUBMENU;
			entry->tag = etag;
			entry->title = wstrdup(title);
			entry->first_child = submenu;
			entry->last_child = submenu;
			append_child(menu, entry);

		} else {
			ctx->err = WAPPMENU_BAD_ENTRY;
			ctx->bad_line = line;
			wAppMenuFreeNode(menu);
			return NULL;
		}
	}

	return menu;
}

WAppMenuParseError wAppMenuParse(const char *const *lines, int count,
				 const char **bad_line, WAppMenuNode **root)
{
	WAppMenuParseCtx ctx;
	WAppMenuNode *menu;

	ctx.slist = lines;
	ctx.count = count;
	ctx.index = 0;
	ctx.bad_line = NULL;
	ctx.err = WAPPMENU_OK;

	if (count < 1)
		ctx.err = WAPPMENU_BAD_ENTRY;

	menu = ctx.err ? NULL : parse_menu(&ctx, 0);
	if (!menu) {
		*root = NULL;
		if (bad_line)
			*bad_line = ctx.bad_line;
		return (WAppMenuParseError)ctx.err;
	}

	*root = menu;
	if (bad_line)
		*bad_line = NULL;
	return WAPPMENU_OK;
}

void wAppMenuFreeNode(WAppMenuNode *node)
{
	WAppMenuNode *child, *next;

	if (!node)
		return;

	for (child = node->first_child; child; child = next) {
		next = child->next;
		wAppMenuFreeNode(child);
	}
	wfree(node->title);
	wfree(node->rtext);
	wfree(node);
}
