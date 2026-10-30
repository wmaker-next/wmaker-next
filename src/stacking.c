/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 1998-2003 Dan Pascu
 *  Copyright (c) 2009-2026 Window Maker Team
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
 * Applier layer over the pure stacking-list primitives in stacklist.c.
 * The list mutators here only build the context from the screen state
 * and translate the model's need_restack into scr->flags.need_restack.
 */

#include "wconfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "WindowMaker.h"
#include "screen.h"
#include "window.h"
#include "actions.h"
#include "properties.h"
#include "stacking.h"
#include "stacklist.h"
#include "workspace.h"
#include "hotcorner.h"
#ifdef VIRTUAL_DESKTOP
#include "vdesk.h"
#endif


static void notifyStackChange(WCoreWindow * frame, char *detail)
{
	WWindow *wwin = wWindowFor(frame->window);

	WMPostNotificationName(WMNChangedStacking, wwin, detail);
}

static void notifyStackReset(WCoreWindow *frame)
{
	WMPostNotificationName(WMNResetStacking, frame->screen_ptr, NULL);
}

static void stack_ctx(WStackCtx *ctx, WScreen *scr)
{
	ctx->levels = scr->stacking_list;
	ctx->by_window = wdisp->context.stack;
	ctx->window_count = &scr->window_count;
	ctx->need_restack = 0;
	ctx->changed_notify = notifyStackChange;
	ctx->reset_notify = notifyStackReset;
}

/*
 *----------------------------------------------------------------------
 * RemakeStackList--
 * 	Remakes the stacking_list for the screen, getting the real
 * stacking order from the server and reordering windows that are not
 * in the correct stacking.
 *
 * Side effects:
 * 	The stacking order list and the actual window stacking
 * may be changed (corrected)
 *
 *----------------------------------------------------------------------
 */
void RemakeStackList(WScreen * scr)
{
	xcb_window_t *windows;
	unsigned int nwindows;
	xcb_window_t junkr, junkp;
	WStackCtx ctx;

	if (!wXQueryTree(wdisp->conn, scr->root_win, &junkr, &junkp, &windows, &nwindows)) {
		wwarning(_("could not get window list!!"));
		return;
	}

	stack_ctx(&ctx, scr);
	WMEmptyBag(scr->stacking_list);
	wStackReconcile(&ctx, windows, (int) nwindows);
	wfree(windows);

	CommitStacking(scr);
}

/*
 *----------------------------------------------------------------------
 * CommitStacking--
 * 	Reorders the actual window stacking, so that it has the stacking
 * order in the internal window stacking lists. It does the opposite
 * of RemakeStackList().
 *
 * This is the flush point for the deferred restack: the mutators only
 * update the internal list and set scr->flags.need_restack, and
 * one call per event-loop iteration (from EventLoop's prepare hook)
 * applies the whole batch as a single wXRestackWindows. The
 * WMNResetStacking notification is posted here, once per batch.
 *
 * Side effects:
 * 	Windows may be restacked.
 *----------------------------------------------------------------------
 */
void CommitStacking(WScreen * scr)
{
	xcb_window_t *windows;
	int nwindows, i;
	WStackCtx ctx;

	scr->flags.need_restack = 0;

	stack_ctx(&ctx, scr);

	nwindows = scr->window_count;
	if (nwindows == 0)
		return;
	windows = wmalloc(sizeof(xcb_window_t) * nwindows);

	i = wStackCollectWindows(&ctx, windows, nwindows);
	wXRestackWindows(wdisp->conn, windows, i);
	wfree(windows);
	wHotCornersRaise(scr);
#ifdef VIRTUAL_DESKTOP
	wWorkspaceRaiseEdge(scr);
#endif
	WMPostNotificationName(WMNResetStacking, scr, NULL);
}

/*
 *----------------------------------------------------------------------
 * wRaiseFrame--
 * 	Raises a frame taking the window level into account.
 *
 * Returns:
 * 	None
 *
 * Side effects:
 * 	Window stacking order and stacking list are changed.
 *
 *----------------------------------------------------------------------
 */
void wRaiseFrame(WCoreWindow * frame)
{
	WStackCtx ctx;

	stack_ctx(&ctx, frame->screen_ptr);
	wStackRaise(&ctx, frame);
	if (ctx.need_restack)
		frame->screen_ptr->flags.need_restack = 1;
}

void wRaiseLowerFrame(WCoreWindow * frame)
{
	if (!frame->stacking->above
	    || (frame->stacking->window_level != frame->stacking->above->stacking->window_level)) {

		wLowerFrame(frame);
	} else {
		WCoreWindow *scan = frame->stacking->above;
		WWindow *frame_wwin = (WWindow *) frame->descriptor.parent;

		while (scan) {

			if (scan->descriptor.parent_type == WCLASS_WINDOW) {
				WWindow *scan_wwin = (WWindow *) scan->descriptor.parent;

				if (wWindowObscuresWindow(scan_wwin, frame_wwin)
				    && scan_wwin->flags.mapped) {
					break;
				}
			}
			scan = scan->stacking->above;
		}

		if (scan) {
			wRaiseFrame(frame);
		} else {
			wLowerFrame(frame);
		}
	}
}

void wLowerFrame(WCoreWindow * frame)
{
	WStackCtx ctx;

	stack_ctx(&ctx, frame->screen_ptr);
	wStackLower(&ctx, frame);
	if (ctx.need_restack)
		frame->screen_ptr->flags.need_restack = 1;
}

/*
 *----------------------------------------------------------------------
 * AddToStackList--
 * 	Inserts the frame in the top of the stacking list. The
 * stacking precedence is obeyed.
 *
 * Returns:
 * 	None
 *
 * Side effects:
 * 	The frame is added to its screen's window list.
 *----------------------------------------------------------------------
 */
void AddToStackList(WCoreWindow * frame)
{
	WStackCtx ctx;

	stack_ctx(&ctx, frame->screen_ptr);
	wStackAdd(&ctx, frame);
	if (ctx.need_restack)
		frame->screen_ptr->flags.need_restack = 1;
}

/*
 *----------------------------------------------------------------------
 * MoveInStackListAbove--
 * 	Moves the frame above "next".
 *
 * Returns:
 * 	None
 *
 * Side effects:
 * 	Stacking order may be changed.
 *      Window level for frame may be changed.
 *----------------------------------------------------------------------
 */
void MoveInStackListAbove(WCoreWindow * next, WCoreWindow * frame)
{
	WStackCtx ctx;

	stack_ctx(&ctx, frame->screen_ptr);
	wStackMoveAbove(&ctx, next, frame);
	if (ctx.need_restack)
		frame->screen_ptr->flags.need_restack = 1;
}

/*
 *----------------------------------------------------------------------
 * MoveInStackListUnder--
 * 	Moves the frame to under "prev".
 *
 * Returns:
 * 	None
 *
 * Side effects:
 * 	Stacking order may be changed.
 *      Window level for frame may be changed.
 *----------------------------------------------------------------------
 */
void MoveInStackListUnder(WCoreWindow * prev, WCoreWindow * frame)
{
	WStackCtx ctx;

	stack_ctx(&ctx, frame->screen_ptr);
	wStackMoveUnder(&ctx, prev, frame);
	if (ctx.need_restack)
		frame->screen_ptr->flags.need_restack = 1;
}

void RemoveFromStackList(WCoreWindow * frame)
{
	WStackCtx ctx;

	stack_ctx(&ctx, frame->screen_ptr);
	wStackRemove(&ctx, frame);
	if (ctx.need_restack)
		frame->screen_ptr->flags.need_restack = 1;
}

void ChangeStackingLevel(WCoreWindow * frame, int new_level)
{
	WStackCtx ctx;

	stack_ctx(&ctx, frame->screen_ptr);
	wStackChangeLevel(&ctx, frame, new_level);
	if (ctx.need_restack)
		frame->screen_ptr->flags.need_restack = 1;
}
