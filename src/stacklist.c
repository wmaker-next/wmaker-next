/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 1998-2003 Dan Pascu
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
 * Pure stacking-list primitives. The mutators only update the bag and
 * the by-window hash and record need_restack. Flushing the batch to
 * the server is CommitStacking()'s job. Notification posts ride the
 * injected hooks at the exact points the original code posted them.
 */

#include "wconfig.h"

#include <stdint.h>

#include "stacklist.h"

void wStackRaise(WStackCtx *ctx, WCoreWindow *frame)
{
	WCoreWindow *wlist = frame;
	int level = frame->stacking->window_level;

	/* already on top */
	if (frame->stacking->above == NULL) {
		return;
	}

	/* insert it on top of other windows on the same level */
	if (frame->stacking->under)
		frame->stacking->under->stacking->above = frame->stacking->above;
	if (frame->stacking->above)
		frame->stacking->above->stacking->under = frame->stacking->under;

	frame->stacking->above = NULL;
	frame->stacking->under = WMGetFromBag(ctx->levels, level);
	if (frame->stacking->under) {
		frame->stacking->under->stacking->above = frame;
	}
	WMSetInBag(ctx->levels, level, frame);

	/* raise transients under us from bottom to top
	 * so that the order is kept */
 again:
	wlist = frame->stacking->under;
	while (wlist && wlist->stacking->under)
		wlist = wlist->stacking->under;
	while (wlist && wlist != frame) {
		if (wlist->stacking->child_of == frame) {
			wStackRaise(ctx, wlist);
			goto again;
		}
		wlist = wlist->stacking->above;
	}

	ctx->need_restack = 1;

	if (ctx->changed_notify)
		ctx->changed_notify(frame, "raise");
}

void wStackLower(WStackCtx *ctx, WCoreWindow *frame)
{
	WCoreWindow *wlist = frame;
	int level = frame->stacking->window_level;

	/* already in bottom */
	if (wlist->stacking->under == NULL) {
		return;
	}
	/* can't lower transient below below its owner */
	if (wlist->stacking->under == wlist->stacking->child_of) {
		return;
	}
	/* remove from the list */
	if (WMGetFromBag(ctx->levels, level) == frame) {
		/* it was the top window */
		WMSetInBag(ctx->levels, level, frame->stacking->under);
		frame->stacking->under->stacking->above = NULL;
	} else {
		if (frame->stacking->under)
			frame->stacking->under->stacking->above = frame->stacking->above;
		if (frame->stacking->above)
			frame->stacking->above->stacking->under = frame->stacking->under;
	}
	wlist = WMGetFromBag(ctx->levels, level);

	/* look for place to put this window */
	if (wlist) {
		WCoreWindow *owner = frame->stacking->child_of;

		if (owner != wlist) {
			while (wlist->stacking->under) {
				/* if this is a transient, it should not be placed under
				 * its owner */
				if (owner == wlist->stacking->under)
					break;
				wlist = wlist->stacking->under;
			}
		}
	}
	/* insert under the place found */
	frame->stacking->above = wlist;
	if (wlist) {
		frame->stacking->under = wlist->stacking->under;
		if (wlist->stacking->under)
			wlist->stacking->under->stacking->above = frame;
		wlist->stacking->under = frame;
	} else {
		frame->stacking->under = NULL;
	}

	ctx->need_restack = 1;

	if (ctx->changed_notify)
		ctx->changed_notify(frame, "lower");
}

void wStackAdd(WStackCtx *ctx, WCoreWindow *frame)
{
	WCoreWindow *curtop, *wlist;
	int index = frame->stacking->window_level;
	WCoreWindow *trans = NULL;

	(*ctx->window_count)++;
	WMHashInsert(ctx->by_window, (void *)(uintptr_t) frame->window, frame);
	curtop = WMGetFromBag(ctx->levels, index);

	/* first window in this level */
	if (curtop == NULL) {
		WMSetInBag(ctx->levels, index, frame);
		frame->stacking->above = NULL;
		frame->stacking->under = NULL;
		ctx->need_restack = 1;
		return;
	}

	/* check if this is a transient owner */
	wlist = curtop;
	while (wlist) {
		if (wlist->stacking->child_of == frame)
			trans = wlist;
		wlist = wlist->stacking->under;
	}
	/* trans will hold the transient in the lowest position
	 * in stacking list */

	frame->stacking->above = trans;
	if (trans != NULL) {
		/* window is owner of a transient.. put it below
		 * the lowest transient */
		frame->stacking->under = trans->stacking->under;
		if (trans->stacking->under) {
			trans->stacking->under->stacking->above = frame;
		}
		trans->stacking->under = frame;
	} else {
		/* window is not owner of transients.. just put it in the
		 * top of other windows */
		frame->stacking->under = curtop;
		curtop->stacking->above = frame;
		WMSetInBag(ctx->levels, index, frame);
	}
	ctx->need_restack = 1;
}

void wStackRemove(WStackCtx *ctx, WCoreWindow *frame)
{
	int index = frame->stacking->window_level;

	if (!WMHashGetItemAndKey(ctx->by_window, (void *)(uintptr_t) frame->window,
				 NULL, NULL)) {
		wwarning("wStackRemove(): window not in list ");
		return;
	}
	WMHashRemove(ctx->by_window, (void *)(uintptr_t) frame->window);
	/* remove from the window stack list */
	if (frame->stacking->under)
		frame->stacking->under->stacking->above = frame->stacking->above;
	if (frame->stacking->above)
		frame->stacking->above->stacking->under = frame->stacking->under;
	else			/* this was the first window on the list */
		WMSetInBag(ctx->levels, index, frame->stacking->under);

	(*ctx->window_count)--;

	if (ctx->reset_notify)
		ctx->reset_notify(frame);
}

void wStackMoveAbove(WStackCtx *ctx, WCoreWindow *next, WCoreWindow *frame)
{
	WCoreWindow *tmpw;
	int index;

	if (!next || frame->stacking->under == next)
		return;

	if (frame->stacking->window_level != next->stacking->window_level)
		wStackChangeLevel(ctx, frame, next->stacking->window_level);

	index = frame->stacking->window_level;

	tmpw = WMGetFromBag(ctx->levels, index);
	if (tmpw == frame)
		WMSetInBag(ctx->levels, index, frame->stacking->under);
	if (frame->stacking->under)
		frame->stacking->under->stacking->above = frame->stacking->above;
	if (frame->stacking->above)
		frame->stacking->above->stacking->under = frame->stacking->under;
	if (next->stacking->above)
		next->stacking->above->stacking->under = frame;
	frame->stacking->under = next;
	frame->stacking->above = next->stacking->above;
	next->stacking->above = frame;
	if (tmpw == next)
		WMSetInBag(ctx->levels, index, frame);

	/* WMNResetStacking rides the CommitStacking() flush */
	ctx->need_restack = 1;
}

void wStackMoveUnder(WStackCtx *ctx, WCoreWindow *prev, WCoreWindow *frame)
{
	WCoreWindow *tmpw;
	int index;

	if (!prev || frame->stacking->above == prev)
		return;

	if (frame->stacking->window_level != prev->stacking->window_level)
		wStackChangeLevel(ctx, frame, prev->stacking->window_level);

	index = frame->stacking->window_level;

	tmpw = WMGetFromBag(ctx->levels, index);
	if (tmpw == frame)
		WMSetInBag(ctx->levels, index, frame->stacking->under);
	if (frame->stacking->under)
		frame->stacking->under->stacking->above = frame->stacking->above;
	if (frame->stacking->above)
		frame->stacking->above->stacking->under = frame->stacking->under;
	if (prev->stacking->under)
		prev->stacking->under->stacking->above = frame;
	frame->stacking->above = prev;
	frame->stacking->under = prev->stacking->under;
	prev->stacking->under = frame;
	ctx->need_restack = 1;
}

void wStackChangeLevel(WStackCtx *ctx, WCoreWindow *frame, int new_level)
{
	if (frame->stacking->window_level == new_level)
		return;

	/* the re-add places the frame at the top of the new level,
	 * below any transients it owns, in both directions */
	wStackRemove(ctx, frame);
	frame->stacking->window_level = new_level;
	wStackAdd(ctx, frame);

	if (ctx->changed_notify)
		ctx->changed_notify(frame, "level");
}

void wStackReconcile(WStackCtx *ctx, const xcb_window_t *order, int count)
{
	WCoreWindow *frame, *tmp;
	int level;
	int i, c;

	/* verify list integrity */
	c = 0;
	for (i = 0; i < count; i++) {
		if (!WMHashGetItemAndKey(ctx->by_window, (void *)(uintptr_t) order[i],
					 (void **)&frame, NULL)) {
			continue;
		}
		if (!frame)
			continue;
		c++;
		level = frame->stacking->window_level;
		tmp = WMGetFromBag(ctx->levels, level);
		if (tmp)
			tmp->stacking->above = frame;
		frame->stacking->under = tmp;
		frame->stacking->above = NULL;
		WMSetInBag(ctx->levels, level, frame);
	}
	*ctx->window_count = c;
}

int wStackCollectWindows(WStackCtx *ctx, xcb_window_t *out, int max)
{
	WCoreWindow *tmp;
	WMBagIterator iter;
	int i = 0;

	WM_ETARETI_BAG(ctx->levels, tmp, iter) {
		while (tmp) {
			if (i >= max)
				return i;
			out[i++] = tmp->window;
			tmp = tmp->stacking->under;
		}
	}
	return i;
}
