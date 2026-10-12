/*
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

#ifndef WMSTACKLIST_H_
#define WMSTACKLIST_H_

#include "wcore.h"

/*
 * Pure stacking-list policy core, extracted from stacking.c. The context
 * injects the screen state the primitives mutate so unit tests can drive
 * them without an X connection. Appliers build it from the WScreen and
 * translate need_restack into scr->flags.need_restack on return.
 */
typedef struct {
	WMBag *levels;			/* scr->stacking_list */
	WMHashTable *by_window;		/* wdisp->context.stack */
	int *window_count;		/* scr->window_count */
	unsigned need_restack;		/* set where the caller must flag a flush */

	/* Notification post points, injected because the WMN* names and
	 * the wwin lookup live in the wmaker binary */
	void (*changed_notify)(WCoreWindow *frame, char *detail);
	void (*reset_notify)(WCoreWindow *frame);
} WStackCtx;

/* pure list primitives, bodies moved verbatim from stacking.c */
void wStackRaise(WStackCtx *ctx, WCoreWindow *frame);
void wStackLower(WStackCtx *ctx, WCoreWindow *frame);
void wStackAdd(WStackCtx *ctx, WCoreWindow *frame);
void wStackRemove(WStackCtx *ctx, WCoreWindow *frame);
void wStackMoveAbove(WStackCtx *ctx, WCoreWindow *next, WCoreWindow *frame);
void wStackMoveUnder(WStackCtx *ctx, WCoreWindow *prev, WCoreWindow *frame);
void wStackChangeLevel(WStackCtx *ctx, WCoreWindow *frame, int new_level);

/* rebuild the levels bag against a server-observed bottom-to-top
 * window order (the caller fetches the order, then calls this) */
void wStackReconcile(WStackCtx *ctx, const xcb_window_t *order, int count);

/* flatten the bag to a top-down window array (out[0] is the topmost
 * window, the order wXRestackWindows expects), returning the entry
 * count */
int wStackCollectWindows(WStackCtx *ctx, xcb_window_t *out, int max);
#endif
