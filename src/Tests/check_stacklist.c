/*
 *  check_stacklist - unit tests for the pure stacking-list primitives in
 *  src/stacklist.c. No display needed, the screen state is fake.
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
#include <stdio.h>
#include <string.h>

#include "stacklist.h"

#include "check_common.h"

#define MAX_FRAMES 12
#define MAX_LOG 16

typedef struct {
	WStackCtx ctx;
	WMBag *levels;
	WMHashTable *by_window;
	int window_count;

	WCoreWindow frames[MAX_FRAMES];
	int nframes;

	int changed_calls;
	xcb_window_t changed_log[MAX_LOG];
	char last_detail[8];
	int reset_calls;
} TestStack;

static TestStack *cur;

static void rec_changed(WCoreWindow *frame, char *detail)
{
	if (cur->changed_calls < MAX_LOG)
		cur->changed_log[cur->changed_calls] = frame->window;
	cur->changed_calls++;
	snprintf(cur->last_detail, sizeof(cur->last_detail), "%s", detail);
}

static void rec_reset(WCoreWindow *frame)
{
	(void) frame;
	cur->reset_calls++;
}

static void ts_init(TestStack *ts)
{
	memset(ts, 0, sizeof(*ts));
	ts->levels = WMCreateBag(0);
	ts->by_window = WMCreateHashTable(WMIntHashCallbacks);
	ts->ctx.levels = ts->levels;
	ts->ctx.by_window = ts->by_window;
	ts->ctx.window_count = &ts->window_count;
	ts->ctx.changed_notify = rec_changed;
	ts->ctx.reset_notify = rec_reset;
	cur = ts;
}

static void ts_end(TestStack *ts)
{
	int i;

	for (i = 0; i < ts->nframes; i++)
		wfree(ts->frames[i].stacking);
	WMFreeBag(ts->levels);
	WMFreeHashTable(ts->by_window);
	cur = NULL;
}

/* stack-allocated core with a wmalloc'ed stacking record */
static WCoreWindow *mkwin(TestStack *ts, xcb_window_t wid, int level,
			  WCoreWindow *owner)
{
	WCoreWindow *f = &ts->frames[ts->nframes++];

	f->window = wid;
	f->screen_ptr = NULL;
	f->stacking = wmalloc(sizeof(WStacking));
	f->stacking->window_level = level;
	f->stacking->child_of = owner;
	return f;
}

/* seed a level by adding frames bottom-to-top: each add lands on top */
static void seed(TestStack *ts, WCoreWindow *const *wins, int n)
{
	int i;

	for (i = 0; i < n; i++)
		wStackAdd(&ts->ctx, wins[i]);
	ts->ctx.need_restack = 0;
	ts->changed_calls = 0;
	ts->reset_calls = 0;
}

/* read one level top-to-bottom into out, returns the count */
static int level_order(TestStack *ts, int level, xcb_window_t *out, int max)
{
	WCoreWindow *f = WMGetFromBag(ts->levels, level);
	int n = 0;

	while (f && n < max) {
		out[n++] = f->window;
		f = f->stacking->under;
	}
	return n;
}

static void expect_order(TestStack *ts, int level, WCoreWindow *const *want, int n)
{
	xcb_window_t got[MAX_FRAMES];
	int got_n = level_order(ts, level, got, MAX_FRAMES);
	int i;

	CHECK(got_n == n);
	for (i = 0; i < n && i < got_n; i++)
		CHECK(got[i] == want[i]->window);
}

static void test_raise(void)
{
	TestStack ts;
	WCoreWindow *a, *b, *c;

	ts_init(&ts);
	a = mkwin(&ts, 0x101, 1, NULL);
	b = mkwin(&ts, 0x102, 1, NULL);
	c = mkwin(&ts, 0x103, 1, NULL);
	seed(&ts, (WCoreWindow *const[]){ a, b, c }, 3);

	/* raise the bottom window: it takes the level top and the rest
	 * keep their relative order */
	wStackRaise(&ts.ctx, a);
	CHECK(ts.ctx.need_restack == 1);
	CHECK(ts.changed_calls == 1 && strcmp(ts.last_detail, "raise") == 0);
	{
		WCoreWindow *const want[] = { a, c, b };
		expect_order(&ts, 1, want, 3);
	}

	/* raising the current top is a no-op: no flush flag, no notify */
	ts.ctx.need_restack = 0;
	ts.changed_calls = 0;
	wStackRaise(&ts.ctx, a);
	CHECK(ts.ctx.need_restack == 0);
	CHECK(ts.changed_calls == 0);

	/* raise the bottom window again, the order of the rest is
	 * preserved */
	ts.ctx.need_restack = 0;
	wStackRaise(&ts.ctx, b);
	{
		WCoreWindow *const want[] = { b, a, c };
		expect_order(&ts, 1, want, 3);
	}
	ts_end(&ts);
}

static void test_lower_refusal(void)
{
	TestStack ts;
	WCoreWindow *o, *t, *m;

	/* the transient sits directly above its owner: lowering it is
	 * refused, nothing moves and nothing is flagged */
	ts_init(&ts);
	o = mkwin(&ts, 0x201, 0, NULL);
	t = mkwin(&ts, 0x202, 0, o);
	seed(&ts, (WCoreWindow *const[]){ o, t }, 2);

	wStackLower(&ts.ctx, t);
	CHECK(ts.ctx.need_restack == 0);
	CHECK(ts.changed_calls == 0);
	{
		WCoreWindow *const want[] = { t, o };
		expect_order(&ts, 0, want, 2);
	}
	ts_end(&ts);

	/* with a window between owner and transient the transient is
	 * lowered, but it stops just above the owner */
	ts_init(&ts);
	o = mkwin(&ts, 0x201, 0, NULL);
	t = mkwin(&ts, 0x202, 0, o);
	m = mkwin(&ts, 0x203, 0, NULL);
	seed(&ts, (WCoreWindow *const[]){ o, m, t }, 3);

	wStackLower(&ts.ctx, t);
	CHECK(ts.ctx.need_restack == 1);
	CHECK(ts.changed_calls == 1 && strcmp(ts.last_detail, "lower") == 0);
	{
		WCoreWindow *const want[] = { m, t, o };
		expect_order(&ts, 0, want, 3);
	}
	ts_end(&ts);
}

static void test_raise_pulls_transients(void)
{
	TestStack ts;
	WCoreWindow *o, *x, *t;

	ts_init(&ts);
	o = mkwin(&ts, 0x301, 0, NULL);
	x = mkwin(&ts, 0x302, 0, NULL);
	t = mkwin(&ts, 0x303, 0, o);
	seed(&ts, (WCoreWindow *const[]){ o, x, t }, 3);

	/* raising the owner pulls the transient along, from the bottom
	 * up, and the unrelated window stays below both */
	wStackRaise(&ts.ctx, o);
	{
		WCoreWindow *const want[] = { t, o, x };
		expect_order(&ts, 0, want, 3);
	}
	/* the transient's own raise notification comes first, then the
	 * owner's */
	CHECK(ts.changed_calls == 2);
	CHECK(ts.changed_log[0] == t->window);
	CHECK(ts.changed_log[1] == o->window);
	CHECK(strcmp(ts.last_detail, "raise") == 0);
	ts_end(&ts);
}

static void test_move_above_under(void)
{
	TestStack ts;
	WCoreWindow *a, *b, *c;

	ts_init(&ts);
	a = mkwin(&ts, 0x401, 1, NULL);
	b = mkwin(&ts, 0x402, 1, NULL);
	c = mkwin(&ts, 0x403, 1, NULL);
	seed(&ts, (WCoreWindow *const[]){ a, b, c }, 3);

	/* move the bottom window above the middle one */
	wStackMoveAbove(&ts.ctx, b, a);
	CHECK(ts.ctx.need_restack == 1);
	{
		WCoreWindow *const want[] = { c, a, b };
		expect_order(&ts, 1, want, 3);
	}

	/* moving it above the head makes it the head */
	ts.ctx.need_restack = 0;
	wStackMoveAbove(&ts.ctx, c, a);
	{
		WCoreWindow *const want[] = { a, c, b };
		expect_order(&ts, 1, want, 3);
	}

	/* already directly above next: no-op */
	ts.ctx.need_restack = 0;
	wStackMoveAbove(&ts.ctx, c, a);
	CHECK(ts.ctx.need_restack == 0);

	/* move back under the bottom window */
	wStackMoveUnder(&ts.ctx, b, a);
	CHECK(ts.ctx.need_restack == 1);
	{
		WCoreWindow *const want[] = { c, b, a };
		expect_order(&ts, 1, want, 3);
	}
	ts_end(&ts);
}

static void test_change_level(void)
{
	TestStack ts;
	WCoreWindow *a, *b, *z;

	ts_init(&ts);
	a = mkwin(&ts, 0x501, 0, NULL);
	b = mkwin(&ts, 0x503, 0, NULL);
	z = mkwin(&ts, 0x502, 1, NULL);
	seed(&ts, (WCoreWindow *const[]){ a, b }, 2);
	seed(&ts, (WCoreWindow *const[]){ z }, 1);

	/* same level: no-op */
	ts.reset_calls = 0;
	wStackChangeLevel(&ts.ctx, a, 0);
	CHECK(ts.reset_calls == 0);
	CHECK(ts.window_count == 3);

	/* promotion: the re-add lands the frame at the top of the new
	 * level, above the windows already there */
	wStackChangeLevel(&ts.ctx, a, 1);
	CHECK(ts.window_count == 3);
	{
		WCoreWindow *const want[] = { a, z };
		expect_order(&ts, 1, want, 2);
	}
	CHECK(WMGetFromBag(ts.levels, 0) == b);
	CHECK(WMHashGet(ts.by_window, (void *)(uintptr_t) a->window) == a);
	/* the internal remove posts a reset and the level change posts a
	 * change so the state hint is refreshed */
	CHECK(ts.reset_calls == 1);
	CHECK(ts.changed_calls == 1);
	CHECK(strcmp(ts.last_detail, "level") == 0);

	/* demotion: same, the top of the now nonempty lower level */
	wStackChangeLevel(&ts.ctx, a, 0);
	{
		WCoreWindow *const want[] = { a, b };
		expect_order(&ts, 0, want, 2);
	}
	CHECK(WMGetFromBag(ts.levels, 1) == z);
	CHECK(ts.reset_calls == 2);
	ts_end(&ts);
}

static void test_reconcile_and_collect(void)
{
	TestStack ts;
	WCoreWindow *a, *b, *c, *d;
	xcb_window_t order[4];
	xcb_window_t out[4];
	int n;

	ts_init(&ts);
	a = mkwin(&ts, 0x601, 1, NULL);
	b = mkwin(&ts, 0x602, 1, NULL);
	c = mkwin(&ts, 0x603, 1, NULL);
	d = mkwin(&ts, 0x604, 0, NULL);

	/* register the frames in the hash the way wStackAdd does */
	wStackAdd(&ts.ctx, a);
	wStackAdd(&ts.ctx, b);
	wStackAdd(&ts.ctx, c);
	wStackAdd(&ts.ctx, d);
	CHECK(ts.window_count == 4);

	/* a shuffled server order (bottom-to-top) re-partitions the
	 * levels and rebuilds the within-level order */
	WMEmptyBag(ts.levels);
	order[0] = c->window;
	order[1] = a->window;
	order[2] = d->window;
	order[3] = b->window;
	wStackReconcile(&ts.ctx, order, 4);
	CHECK(ts.window_count == 4);
	{
		WCoreWindow *const want1[] = { b, a, c };
		WCoreWindow *const want0[] = { d };
		expect_order(&ts, 1, want1, 3);
		expect_order(&ts, 0, want0, 1);
	}

	/* collect: out[0] is the topmost window (the order
	 * wXRestackWindows consumes), count matches window_count */
	n = wStackCollectWindows(&ts.ctx, out, 4);
	CHECK(n == 4);
	CHECK(out[0] == b->window);
	CHECK(out[1] == a->window);
	CHECK(out[2] == c->window);
	CHECK(out[3] == d->window);
	ts_end(&ts);
}

static void test_add_remove_consistency(void)
{
	TestStack ts;
	WCoreWindow *a, *b;

	ts_init(&ts);
	a = mkwin(&ts, 0x701, 2, NULL);
	b = mkwin(&ts, 0x702, 2, NULL);

	wStackAdd(&ts.ctx, a);
	CHECK(ts.window_count == 1);
	CHECK(WMHashGet(ts.by_window, (void *)(uintptr_t) a->window) == a);

	wStackAdd(&ts.ctx, b);
	CHECK(ts.window_count == 2);
	{
		WCoreWindow *const want[] = { b, a };
		expect_order(&ts, 2, want, 2);
	}

	/* remove the head */
	ts.reset_calls = 0;
	wStackRemove(&ts.ctx, b);
	CHECK(ts.window_count == 1);
	CHECK(WMHashGet(ts.by_window, (void *)(uintptr_t) b->window) == NULL);
	CHECK(WMGetFromBag(ts.levels, 2) == a);
	CHECK(a->stacking->above == NULL);
	CHECK(ts.reset_calls == 1);

	/* remove of an absent window warns and no-ops */
	ts.reset_calls = 0;
	wStackRemove(&ts.ctx, b);
	CHECK(ts.window_count == 1);
	CHECK(ts.reset_calls == 0);

	/* remove the last window */
	wStackRemove(&ts.ctx, a);
	CHECK(ts.window_count == 0);
	CHECK(WMGetFromBag(ts.levels, 2) == NULL);
	ts_end(&ts);
}

int main(void)
{
	test_raise();
	test_lower_refusal();
	test_raise_pulls_transients();
	test_move_above_under();
	test_change_level();
	test_reconcile_and_collect();
	test_add_remove_consistency();

	return check_report("check_stacklist");
}
