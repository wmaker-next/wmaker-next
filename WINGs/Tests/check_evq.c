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
 *  Run by `make check`. The queue operations are exercised on synthetic
 *  XEvent values with no Display open (every WXEvq* call drains nothing
 *  when dpy==NULL), so this test needs the X client headers and link
 *  but no X server. Exit status 0 on pass, non-zero on the first
 *  failing assertion with the file and line printed to stderr.
 */

#include <stdio.h>
#include <string.h>

#include <X11/Xlib.h>
#include <WINGs/WINGs.h>

static int failures;

#define CHECK(cond) do {                                                   \
	if (!(cond)) {                                                     \
	    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);\
	    failures++;                                                    \
	}                                                                  \
    } while (0)

static XEvent mk(int type, Window win, unsigned long serial)
{
	XEvent e;

	memset(&e, 0, sizeof(e));
	e.type = type;
	e.xany.window = win;
	e.xany.serial = serial;
	return e;
}

static void push(int type, Window win, unsigned long serial)
{
	XEvent e = mk(type, win, serial);
	/* WXEvqPutBack pushes at the head, so build a FIFO by pushing in
	 * reverse, the helper below does that for readability */
	WXEvqPutBack(&e);
}

/* fill the queue head-to-tail in the order given, callers list events
 * in arrival order and this pushes them back-to-front */
static void load(const XEvent *evs, int n)
{
	int i;

	WXEvqClear();
	for (i = n - 1; i >= 0; i--)
		WXEvqPutBack(&evs[i]);
}

/* ---- FIFO order ------------------------------------------------------ */

static void test_fifo_order(void)
{
	XEvent in[3] = {
		mk(KeyPress, 10, 1),
		mk(Expose,   11, 2),
		mk(KeyPress, 12, 3),
	};
	XEvent e;

	load(in, 3);

	CHECK(WXEvqPending(NULL));
	CHECK(WXEvqNext(NULL, &e) && e.type == KeyPress && e.xany.window == 10);
	CHECK(WXEvqNext(NULL, &e) && e.type == Expose   && e.xany.window == 11);
	CHECK(WXEvqNext(NULL, &e) && e.type == KeyPress && e.xany.window == 12);
	CHECK(!WXEvqNext(NULL, &e));
	CHECK(!WXEvqPending(NULL));
}

/* ---- put-back at head ------------------------------------------------ */

static void test_putback_head(void)
{
	XEvent e;

	WXEvqClear();
	push(Expose, 20, 1);
	/* queue is now [Expose/20], put back a KeyPress on top */
	e = mk(KeyPress, 21, 2);
	WXEvqPutBack(&e);

	CHECK(WXEvqNext(NULL, &e) && e.type == KeyPress && e.xany.window == 21);
	CHECK(WXEvqNext(NULL, &e) && e.type == Expose   && e.xany.window == 20);
	CHECK(!WXEvqPending(NULL));

	/* put-back onto an empty queue must set both head and tail so a
	 * later append (via a second put-back of something behind it)
	 * does not lose the node, exercise by put-back then put-back */
	WXEvqClear();
	e = mk(KeyPress, 30, 1);
	WXEvqPutBack(&e);
	e = mk(Expose, 31, 2);
	WXEvqPutBack(&e);
	CHECK(WXEvqNext(NULL, &e) && e.xany.window == 31);
	CHECK(WXEvqNext(NULL, &e) && e.xany.window == 30);
	CHECK(!WXEvqPending(NULL));
}

/* ---- peek is non-destructive ----------------------------------------- */

static void test_peek(void)
{
	XEvent in[2] = { mk(KeyPress, 40, 1), mk(Expose, 41, 2) };
	XEvent e;

	load(in, 2);

	CHECK(WXEvqPeek(NULL, &e) && e.type == KeyPress && e.xany.window == 40);
	/* still there */
	CHECK(WXEvqPeek(NULL, &e) && e.type == KeyPress && e.xany.window == 40);
	CHECK(WXEvqNext(NULL, &e) && e.type == KeyPress);
	CHECK(WXEvqNext(NULL, &e) && e.type == Expose);

	WXEvqClear();
	CHECK(!WXEvqPeek(NULL, &e));
}

/* ---- take-typed: first match unlinked, rest preserved ---------------- */

static void test_take_typed(void)
{
	XEvent in[5] = {
		mk(KeyPress,   50, 1),
		mk(Expose,     51, 2),
		mk(Expose,     52, 3),
		mk(ButtonPress,51, 4),
		mk(Expose,     53, 5),
	};
	XEvent e;

	load(in, 5);

	/* any-window typed take returns the first Expose and leaves the
	 * other two in place in arrival order */
	CHECK(WXEvqTakeTyped(NULL, Expose, None, &e) && e.xany.window == 51);
	CHECK(WXEvqTakeTyped(NULL, Expose, None, &e) && e.xany.window == 52);

	/* window-restricted take skips the remaining Expose at 53 and
	 * fails for window 51 */
	CHECK(!WXEvqTakeTyped(NULL, Expose, 51, &e));
	CHECK(WXEvqTakeTyped(NULL, Expose, 53, &e) && e.xany.serial == 5);

	/* the non-matching events are still there in order */
	CHECK(WXEvqNext(NULL, &e) && e.type == KeyPress    && e.xany.serial == 1);
	CHECK(WXEvqNext(NULL, &e) && e.type == ButtonPress && e.xany.serial == 4);
	CHECK(!WXEvqPending(NULL));

	/* tail-unlink case: take the last node and verify a subsequent
	 * put-back-then-next round-trips, which would fail if evq_tail
	 * were left dangling */
	load(in, 5);
	CHECK(WXEvqTakeTyped(NULL, Expose, 53, &e) && e.xany.serial == 5);
	e = mk(FocusIn, 99, 9);
	WXEvqPutBack(&e);
	CHECK(WXEvqNext(NULL, &e) && e.type == FocusIn);
	CHECK(WXEvqNext(NULL, &e) && e.xany.serial == 1);
	CHECK(WXEvqNext(NULL, &e) && e.xany.serial == 2);
	CHECK(WXEvqNext(NULL, &e) && e.xany.serial == 3);
	CHECK(WXEvqNext(NULL, &e) && e.xany.serial == 4);
	CHECK(!WXEvqPending(NULL));
}

/* ---- take-mask: select-mask matching --------------------------------- */

static void test_take_mask(void)
{
	XEvent in[4] = {
		mk(EnterNotify,  60, 1),
		mk(MotionNotify, 60, 2),
		mk(KeyPress,     60, 3),
		mk(MotionNotify, 61, 4),
	};
	XEvent e;

	load(in, 4);

	/* ButtonMotionMask maps to MotionNotify only, the EnterNotify at
	 * the head must be skipped not consumed */
	CHECK(WXEvqTakeMask(NULL, ButtonMotionMask, None, &e)
	      && e.type == MotionNotify && e.xany.serial == 2);
	/* window-restricted */
	CHECK(WXEvqTakeMask(NULL, ButtonMotionMask, 61, &e)
	      && e.xany.serial == 4);
	CHECK(!WXEvqTakeMask(NULL, ButtonMotionMask, None, &e));

	/* what is left, in order */
	CHECK(WXEvqNext(NULL, &e) && e.type == EnterNotify);
	CHECK(WXEvqNext(NULL, &e) && e.type == KeyPress);
	CHECK(!WXEvqPending(NULL));
}

/* ---- drop-range by serial -------------------------------------------- */

static void test_drop_range(void)
{
	XEvent in[6] = {
		mk(EnterNotify, 70, 100),
		mk(LeaveNotify, 70, 101),
		mk(KeyPress,    70, 102),
		mk(EnterNotify, 71, 103),
		mk(LeaveNotify, 71, 200),	/* outside [100,200) */
		mk(EnterNotify, 72, 104),
	};
	XEvent e;
	int dropped;

	load(in, 6);

	dropped = WXEvqDropRange(NULL, 100, 200,
				 EnterWindowMask | LeaveWindowMask);
	/* serials 100,101,103,104 match mask+range, 102 is KeyPress so
	 * survives the mask, 200 is at the upper bound so survives the
	 * half-open range */
	CHECK(dropped == 4);
	CHECK(WXEvqNext(NULL, &e) && e.type == KeyPress    && e.xany.serial == 102);
	CHECK(WXEvqNext(NULL, &e) && e.type == LeaveNotify && e.xany.serial == 200);
	CHECK(!WXEvqPending(NULL));

	/* dropping the tail must leave evq_tail correct for a following
	 * put-back, which appendNode would corrupt if tail dangled */
	load(in, 6);
	dropped = WXEvqDropRange(NULL, 104, 105,
				 EnterWindowMask | LeaveWindowMask);
	CHECK(dropped == 1);
	e = mk(FocusIn, 99, 999);
	WXEvqPutBack(&e);
	CHECK(WXEvqNext(NULL, &e) && e.type == FocusIn);
	CHECK(WXEvqNext(NULL, &e) && e.xany.serial == 100);
	CHECK(WXEvqNext(NULL, &e) && e.xany.serial == 101);
	CHECK(WXEvqNext(NULL, &e) && e.xany.serial == 102);
	CHECK(WXEvqNext(NULL, &e) && e.xany.serial == 103);
	CHECK(WXEvqNext(NULL, &e) && e.xany.serial == 200);
	CHECK(!WXEvqPending(NULL));
}

int main(void)
{
	test_fifo_order();
	test_putback_head();
	test_peek();
	test_take_typed();
	test_take_mask();
	test_drop_range();

	WXEvqClear();

	if (failures) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	printf("ok - check_evq\n");
	return 0;
}
