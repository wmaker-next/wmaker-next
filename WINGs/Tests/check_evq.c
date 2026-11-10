/*
 *  check_evq - automated unit tests for the WINGs internal event queue
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
 *
 *  Run by `make check`. The queue operations are exercised on
 *  hand-rolled raw xcb events injected through W_XEvqPutRaw, with no
 *  Display open (every WXEvq* call drains nothing when dpy==NULL), so
 *  this test needs the X client headers and link but no X server.
 *  Events handed out by the queue are owned by the caller, each
 *  delivered event is freed exactly once.  Exit status 0 on pass,
 *  non-zero on the first failing assertion with the file and line
 *  printed to stderr.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <WINGs/WINGs.h>

extern void W_XEvqPutRaw(void *xcb_ev);
extern xcb_window_t W_EventWindow(const xcb_generic_event_t *xe);

#include "check_common.h"

/* build a 36-byte xcb event the way xcb_poll_for_event returns it
 * (32 wire bytes + full_sequence), callers fill the typed fields */
static xcb_generic_event_t *mkraw(int type, unsigned long seq)
{
	xcb_generic_event_t *xe = malloc(sizeof(*xe));

	if (!xe) {
		perror("malloc");
		exit(2);
	}
	memset(xe, 0, sizeof(*xe));
	xe->response_type = type;
	xe->full_sequence = seq;
	return xe;
}

/* the dispatch window sits at wire offset 12 for the
 * key/button/motion/crossing family, at 8 for the selection family
 * (they carry a leading time field) and at the first body slot for
 * the rest, mirroring W_EventWindow's table */
static void setwin(xcb_generic_event_t *e, int type, uint32_t win)
{
	switch (type) {
	case XCB_KEY_PRESS:
	case XCB_KEY_RELEASE:
	case XCB_BUTTON_PRESS:
	case XCB_BUTTON_RELEASE:
	case XCB_MOTION_NOTIFY:
	case XCB_ENTER_NOTIFY:
	case XCB_LEAVE_NOTIFY:
		((xcb_key_press_event_t *)e)->event = win;
		break;
	case XCB_SELECTION_REQUEST:
		((xcb_selection_request_event_t *)e)->owner = win;
		break;
	case XCB_SELECTION_NOTIFY:
		((xcb_selection_notify_event_t *)e)->requestor = win;
		break;
	case XCB_SELECTION_CLEAR:
		((xcb_selection_clear_event_t *)e)->owner = win;
		break;
	default:
		((xcb_expose_event_t *)e)->window = win;
		break;
	}
}

static xcb_generic_event_t *push(int type, uint32_t win, unsigned long seq)
{
	xcb_generic_event_t *e = mkraw(type, seq);

	setwin(e, type, win);
	W_XEvqPutRaw(e);
	return e;
}

/* fill the queue head-to-tail in the order given, callers list events
 * in arrival order */
static void load(int t0, uint32_t w0, unsigned long s0,
                 int t1, uint32_t w1, unsigned long s1,
                 int t2, uint32_t w2, unsigned long s2,
                 int t3, uint32_t w3, unsigned long s3,
                 int t4, uint32_t w4, unsigned long s4)
{
	WXEvqClear();
	push(t0, w0, s0);
	if (t1)
		push(t1, w1, s1);
	if (t2)
		push(t2, w2, s2);
	if (t3)
		push(t3, w3, s3);
	if (t4)
		push(t4, w4, s4);
}

/* ---- FIFO order ------------------------------------------------------ */

static void test_fifo_order(void)
{
	xcb_generic_event_t *e;

	load(XCB_KEY_PRESS, 10, 1, XCB_EXPOSE, 11, 2, XCB_KEY_PRESS, 12, 3, 0, 0, 0, 0, 0, 0);

	CHECK(WXEvqPending(NULL));
	CHECK(WXEvqNext(NULL, &e) && e->full_sequence == 1
	      && W_EventWindow(e) == 10);
	free(e);
	CHECK(WXEvqNext(NULL, &e) && e->full_sequence == 2
	      && W_EventWindow(e) == 11);
	free(e);
	CHECK(WXEvqNext(NULL, &e) && e->full_sequence == 3
	      && W_EventWindow(e) == 12);
	free(e);
	CHECK(!WXEvqNext(NULL, &e));
	CHECK(!WXEvqPending(NULL));
}

/* ---- selection events: the xany.window alias is the owner/requestor
 *      at wire offset 8, never the leading timestamp ------------------- */

static void test_selection_windows(void)
{
	xcb_generic_event_t *e;

	WXEvqClear();
	e = mkraw(XCB_SELECTION_REQUEST, 1);
	((xcb_selection_request_event_t *)e)->time = 12345;
	((xcb_selection_request_event_t *)e)->owner = 31;
	W_XEvqPutRaw(e);
	e = mkraw(XCB_SELECTION_NOTIFY, 2);
	((xcb_selection_notify_event_t *)e)->time = 67890;
	((xcb_selection_notify_event_t *)e)->requestor = 32;
	W_XEvqPutRaw(e);
	e = mkraw(XCB_SELECTION_CLEAR, 3);
	((xcb_selection_clear_event_t *)e)->time = 9876;
	((xcb_selection_clear_event_t *)e)->owner = 33;
	W_XEvqPutRaw(e);

	CHECK(WXEvqNext(NULL, &e) && (e->response_type & 0x7f) == XCB_SELECTION_REQUEST
	      && W_EventWindow(e) == 31);
	free(e);
	e = NULL;
	CHECK(WXEvqNext(NULL, &e) && (e->response_type & 0x7f) == XCB_SELECTION_NOTIFY
	      && W_EventWindow(e) == 32);
	free(e);
	e = NULL;
	CHECK(WXEvqNext(NULL, &e) && (e->response_type & 0x7f) == XCB_SELECTION_CLEAR
	      && W_EventWindow(e) == 33);
	free(e);
	e = NULL;
	CHECK(!WXEvqPending(NULL));
}

/* ---- put-back at head, ownership to the queue ------------------------- */

static void test_putback_head(void)
{
	xcb_generic_event_t *e;

	WXEvqClear();
	push(XCB_EXPOSE, 20, 1);
	/* queue is now [XCB_EXPOSE/20], put back a XCB_KEY_PRESS on top */
	e = mkraw(XCB_KEY_PRESS, 2);
	((xcb_key_press_event_t *)e)->event = 21;
	WXEvqPutBack(e);

	CHECK(WXEvqNext(NULL, &e) && e->full_sequence == 2
	      && W_EventWindow(e) == 21);
	free(e);
	e = NULL;
	CHECK(WXEvqNext(NULL, &e) && e->full_sequence == 1
	      && W_EventWindow(e) == 20);
	free(e);
	e = NULL;
	CHECK(!WXEvqPending(NULL));

	/* put-back onto an empty queue must set both head and tail so a
	 * later append does not lose the node */
	WXEvqClear();
	e = mkraw(XCB_KEY_PRESS, 1);
	((xcb_key_press_event_t *)e)->event = 30;
	WXEvqPutBack(e);
	e = mkraw(XCB_EXPOSE, 2);
	((xcb_expose_event_t *)e)->window = 31;
	WXEvqPutBack(e);
	CHECK(WXEvqNext(NULL, &e) && W_EventWindow(e) == 31);
	free(e);
	e = NULL;
	CHECK(WXEvqNext(NULL, &e) && W_EventWindow(e) == 30);
	free(e);
	e = NULL;
	CHECK(!WXEvqPending(NULL));
}

/* ---- peek is non-destructive and lends, not hands over ---------------- */

static void test_peek(void)
{
	xcb_generic_event_t *e;

	load(XCB_KEY_PRESS, 40, 1, XCB_EXPOSE, 41, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0);

	CHECK(WXEvqPeek(NULL, &e) && e->full_sequence == 1);
	/* still there, and the same buffer the next op hands over */
	CHECK(WXEvqPeek(NULL, &e) && e->full_sequence == 1);
	CHECK(WXEvqNext(NULL, &e) && e->full_sequence == 1);
	free(e);
	e = NULL;
	CHECK(WXEvqNext(NULL, &e) && e->full_sequence == 2);
	free(e);

	WXEvqClear();
	CHECK(!WXEvqPeek(NULL, &e));
}

/* ---- take-typed: first match unlinked, rest preserved ---------------- */

static void test_take_typed(void)
{
	xcb_generic_event_t *e;

	load(XCB_KEY_PRESS, 50, 1, XCB_EXPOSE, 51, 2, XCB_EXPOSE, 52, 3, 0, 0, 0, 0, 0, 0);
	push(XCB_BUTTON_PRESS, 51, 4);
	push(XCB_EXPOSE, 53, 5);

	/* any-window typed take returns the first XCB_EXPOSE and leaves the
	 * other two in place in arrival order */
	CHECK(WXEvqTakeTyped(NULL, XCB_EXPOSE, XCB_NONE, &e) && W_EventWindow(e) == 51);
	free(e);
	e = NULL;
	CHECK(WXEvqTakeTyped(NULL, XCB_EXPOSE, XCB_NONE, &e) && W_EventWindow(e) == 52);
	free(e);

	/* window-restricted take skips the remaining XCB_EXPOSE at 53 and
	 * fails for window 51 */
	CHECK(!WXEvqTakeTyped(NULL, XCB_EXPOSE, 51, &e));
	CHECK(WXEvqTakeTyped(NULL, XCB_EXPOSE, 53, &e) && e->full_sequence == 5);
	free(e);

	/* the non-matching events are still there in order */
	CHECK(WXEvqNext(NULL, &e) && (e->response_type & 0x7f) == XCB_KEY_PRESS
	      && e->full_sequence == 1);
	free(e);
	e = NULL;
	CHECK(WXEvqNext(NULL, &e) && (e->response_type & 0x7f) == XCB_BUTTON_PRESS
	      && e->full_sequence == 4);
	free(e);
	e = NULL;
	CHECK(!WXEvqPending(NULL));

	/* tail-unlink case: take the last node and verify a subsequent
	 * put-back-then-next round-trips, which would fail if evq_tail
	 * were left dangling */
	load(XCB_KEY_PRESS, 50, 1, XCB_EXPOSE, 51, 2, XCB_EXPOSE, 52, 3, 0, 0, 0, 0, 0, 0);
	push(XCB_BUTTON_PRESS, 51, 4);
	push(XCB_EXPOSE, 53, 5);
	CHECK(WXEvqTakeTyped(NULL, XCB_EXPOSE, 53, &e) && e->full_sequence == 5);
	free(e);
	e = mkraw(XCB_FOCUS_IN, 9);
	WXEvqPutBack(e);
	CHECK(WXEvqNext(NULL, &e) && (e->response_type & 0x7f) == XCB_FOCUS_IN);
	free(e);
	e = NULL;
	CHECK(WXEvqNext(NULL, &e) && e->full_sequence == 1);
	free(e);
	e = NULL;
	CHECK(WXEvqNext(NULL, &e) && e->full_sequence == 2);
	free(e);
	e = NULL;
	CHECK(WXEvqNext(NULL, &e) && e->full_sequence == 3);
	free(e);
	e = NULL;
	CHECK(WXEvqNext(NULL, &e) && e->full_sequence == 4);
	free(e);
	e = NULL;
	CHECK(!WXEvqPending(NULL));
}

/* ---- take-mask: select-mask matching --------------------------------- */

static void test_take_mask(void)
{
	xcb_generic_event_t *e;

	load(XCB_ENTER_NOTIFY, 60, 1, XCB_MOTION_NOTIFY, 60, 2, XCB_KEY_PRESS, 60, 3, 0, 0, 0, 0, 0, 0);
	push(XCB_MOTION_NOTIFY, 61, 4);

	/* XCB_EVENT_MASK_BUTTON_MOTION maps to XCB_MOTION_NOTIFY only, the XCB_ENTER_NOTIFY at
	 * the head must be skipped not consumed */
	CHECK(WXEvqTakeMask(NULL, XCB_EVENT_MASK_BUTTON_MOTION, XCB_NONE, &e)
	      && (e->response_type & 0x7f) == XCB_MOTION_NOTIFY
	      && e->full_sequence == 2);
	free(e);
	/* window-restricted */
	CHECK(WXEvqTakeMask(NULL, XCB_EVENT_MASK_BUTTON_MOTION, 61, &e)
	      && e->full_sequence == 4);
	free(e);
	e = NULL;
	CHECK(!WXEvqTakeMask(NULL, XCB_EVENT_MASK_BUTTON_MOTION, XCB_NONE, &e));

	/* what is left, in order */
	CHECK(WXEvqNext(NULL, &e) && (e->response_type & 0x7f) == XCB_ENTER_NOTIFY);
	free(e);
	e = NULL;
	CHECK(WXEvqNext(NULL, &e) && (e->response_type & 0x7f) == XCB_KEY_PRESS);
	free(e);
	e = NULL;
	CHECK(!WXEvqPending(NULL));
}

/* ---- timed takes: headless coverage, only the queue checks and the
 *      zero-timeout return are reachable without an X server, the
 *      blocking poll path needs a live connection ---------------------- */

static xcb_atom_t match_want_property;

static bool matchSelProperty(const xcb_generic_event_t *event, void *cdata)
{
	const xcb_selection_notify_event_t *sn = (const xcb_selection_notify_event_t *)event;

	(void) cdata;
	return sn->property == match_want_property;
}

static void test_take_typed_timed(void)
{
	xcb_selection_notify_event_t *stale, *fresh;
	xcb_generic_event_t *e;

	/* an already-queued event is returned without reaching the
	 * poll path, even with an infinite timeout */
	load(XCB_EXPOSE, 11, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	CHECK(WXEvqTakeTypedTimed(NULL, XCB_EXPOSE, XCB_NONE, NULL, NULL, &e, -1)
	      && e->full_sequence == 2);
	free(e);
	e = NULL;
	CHECK(!WXEvqPending(NULL));

	/* no match and a zero timeout: false without blocking */
	CHECK(!WXEvqTakeTypedTimed(NULL, XCB_EXPOSE, XCB_NONE, NULL, NULL, &e, 0));

	/* the match callback drops non-matching events and keeps
	 * waiting, here the first SELECTION_NOTIFY is a stale reply
	 * from an earlier timed-out request naming another property */
	stale = (xcb_selection_notify_event_t *)mkraw(XCB_SELECTION_NOTIFY, 1);
	stale->requestor = 77;
	stale->property = 1000;
	W_XEvqPutRaw(stale);
	fresh = (xcb_selection_notify_event_t *)mkraw(XCB_SELECTION_NOTIFY, 2);
	fresh->requestor = 77;
	fresh->property = 1001;
	W_XEvqPutRaw(fresh);

	match_want_property = 1001;
	CHECK(WXEvqTakeTypedTimed(NULL, XCB_SELECTION_NOTIFY, 77, matchSelProperty, NULL, &e, 0)
	      && e->full_sequence == 2);
	free(e);
	e = NULL;
	CHECK(!WXEvqPending(NULL));
}

static int pipe_hits;

static void pipeReadable(int fd, int mask, void *cdata)
{
	char buf[8];

	(void)mask;
	(void)cdata;
	CHECK(read(fd, buf, sizeof(buf)) > 0);
	pipe_hits++;
}

static void test_take_mask_timed_defers_input(void)
{
	WMHandlerID hid;
	xcb_generic_event_t *e = NULL;
	int fds[2], rc;

	rc = pipe(fds);
	CHECK(rc == 0);
	if (rc != 0)
		return;
	pipe_hits = 0;
	hid = WMAddInputHandler(fds[0], WIReadMask, pipeReadable, NULL);
	CHECK(write(fds[1], "x", 1) == 1);

	CHECK(!WXEvqTakeMaskTimed(NULL, XCB_EVENT_MASK_EXPOSURE, XCB_NONE, &e, 200));
	CHECK(pipe_hits == 0);
	CHECK(e == NULL);

	WMDeleteInputHandler(hid);
	close(fds[0]);
	close(fds[1]);
}

static void test_take_mask_timed(void)
{
	xcb_generic_event_t *e;

	load(XCB_KEY_PRESS, 10, 1, XCB_EXPOSE, 11, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	CHECK(WXEvqTakeMaskTimed(NULL, XCB_EVENT_MASK_EXPOSURE, XCB_NONE, &e, -1)
	      && e->full_sequence == 2);
	free(e);
	e = NULL;

	/* zero timeout with a non-matching event queued: false without
	 * blocking */
	CHECK(!WXEvqTakeMaskTimed(NULL, XCB_EVENT_MASK_EXPOSURE, XCB_NONE, &e, 0));
	CHECK(WXEvqTakeMaskTimed(NULL, XCB_EVENT_MASK_KEY_PRESS, XCB_NONE, &e, 0)
	      && e->full_sequence == 1);
	free(e);
	e = NULL;
	CHECK(!WXEvqPending(NULL));
}

/* ---- drop-range by serial -------------------------------------------- */

static void test_drop_range(void)
{
	xcb_generic_event_t *e;
	int dropped;

	load(XCB_ENTER_NOTIFY, 70, 100, XCB_LEAVE_NOTIFY, 70, 101, XCB_KEY_PRESS, 70, 102, 0, 0, 0, 0, 0, 0);
	push(XCB_ENTER_NOTIFY, 71, 103);
	push(XCB_LEAVE_NOTIFY, 71, 200);	/* outside [100,200) */
	push(XCB_ENTER_NOTIFY, 72, 104);

	dropped = WXEvqDropRange(NULL, 100, 200,
				 XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW);
	/* serials 100,101,103,104 match mask+range, 102 is XCB_KEY_PRESS so
	 * survives the mask, 200 is at the upper bound so survives the
	 * half-open range */
	CHECK(dropped == 4);
	CHECK(WXEvqNext(NULL, &e) && (e->response_type & 0x7f) == XCB_KEY_PRESS
	      && e->full_sequence == 102);
	free(e);
	e = NULL;
	CHECK(WXEvqNext(NULL, &e) && (e->response_type & 0x7f) == XCB_LEAVE_NOTIFY
	      && e->full_sequence == 200);
	free(e);
	e = NULL;
	CHECK(!WXEvqPending(NULL));

	/* dropping the tail must leave evq_tail correct for a following
	 * put-back, which appendNode would corrupt if tail dangled */
	load(XCB_ENTER_NOTIFY, 70, 100, XCB_LEAVE_NOTIFY, 70, 101, XCB_KEY_PRESS, 70, 102, 0, 0, 0, 0, 0, 0);
	push(XCB_ENTER_NOTIFY, 71, 103);
	push(XCB_LEAVE_NOTIFY, 71, 200);
	push(XCB_ENTER_NOTIFY, 72, 104);
	dropped = WXEvqDropRange(NULL, 104, 105,
				 XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW);
	CHECK(dropped == 1);
	e = mkraw(XCB_FOCUS_IN, 999);
	WXEvqPutBack(e);
	CHECK(WXEvqNext(NULL, &e) && (e->response_type & 0x7f) == XCB_FOCUS_IN);
	free(e);
	e = NULL;
	CHECK(WXEvqNext(NULL, &e) && e->full_sequence == 100);
	free(e);
	e = NULL;
	CHECK(WXEvqNext(NULL, &e) && e->full_sequence == 101);
	free(e);
	e = NULL;
	CHECK(WXEvqNext(NULL, &e) && e->full_sequence == 102);
	free(e);
	e = NULL;
	CHECK(WXEvqNext(NULL, &e) && e->full_sequence == 103);
	free(e);
	e = NULL;
	CHECK(WXEvqNext(NULL, &e) && e->full_sequence == 200);
	free(e);
	e = NULL;
	CHECK(!WXEvqPending(NULL));
}

/* ---- extension events: no wire window, never window-matched ---------- */

static void test_extension_no_window(void)
{
	xcb_generic_event_t *e;

	WXEvqClear();
	/* an XKB-shaped classic extension event: the offset-4 word is the
	 * timestamp, a real view XID parked there must never be read as
	 * the dispatch window */
	e = mkraw(0x55, 7);
	((xcb_key_press_event_t *)e)->root = 10;
	((xcb_key_press_event_t *)e)->event = 71;
	W_XEvqPutRaw(e);

	CHECK(W_EventWindow(e) == XCB_NONE);

	/* the queue does not window-match extension events even when the
	 * offset-4 word happens to name a live window */
	CHECK(!WXEvqTakeTyped(NULL, 0x55, 10, &e));
	CHECK(!WXEvqTakeMask(NULL, XCB_EVENT_MASK_KEY_PRESS, 10, &e));

	/* ... and core-type takes do not flush it either */
	CHECK(!WXEvqTakeTyped(NULL, XCB_KEY_PRESS, 71, &e));

	CHECK(WXEvqNext(NULL, &e));
	free(e);
	e = NULL;
	CHECK(!WXEvqPending(NULL));
}

/* the fetchers run the dead hook when the connection error surfaces
 * outside drain(): a noreturn owner keeps NULL fetch results away from
 * the consumers, a returning owner hands the NULL result to its
 * loop's check instead, and with no hook installed the process
 * exits like drain() does */
static int dead_hook_calls;

static void returning_dead_hook(xcb_connection_t *dpy)
{
	(void) dpy;
	dead_hook_calls++;
}

static void test_connection_dead(void)
{
	WXEvqSetDeadHook(returning_dead_hook);
	WXEvqConnectionDead(NULL);
	CHECK(dead_hook_calls == 1);
	WXEvqConnectionDead(NULL);
	CHECK(dead_hook_calls == 2);
	WXEvqSetDeadHook(NULL);
}

int main(void)
{
	test_fifo_order();
	test_selection_windows();
	test_putback_head();
	test_peek();
	test_take_typed();
	test_take_mask();
	test_take_typed_timed();
	test_take_mask_timed_defers_input();
	test_take_mask_timed();
	test_drop_range();
	test_extension_no_window();
	test_connection_dead();

	WXEvqClear();

	return check_report("check_evq");
}
