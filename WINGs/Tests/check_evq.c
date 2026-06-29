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
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <WINGs/WINGs.h>

extern void W_CookXcbEvent(Display *dpy, void *xcb_ev, XEvent *out);

#include "check_common.h"

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

/* ---- xcb wire-to-XEvent cooker --------------------------------------- */

/* hand-roll a 36-byte xcb event (32 wire bytes + full_sequence) and
 * verify W_CookXcbEvent fills the matching XEvent fields, this is the
 * code path drain() takes after WXEvqFlipToXcb so getting it wrong
 * would mis-route every event in the window manager */
static void test_cook(void)
{
	xcb_generic_event_t raw;
	XEvent o;

	/* ButtonPress with the send_event bit set: detail/state/window
	 * map to button/state/window, root/child swap to root/subwindow,
	 * event_x/y to x/y and root_x/y to x_root/y_root */
	xcb_button_press_event_t *bp = (xcb_button_press_event_t *)&raw;
	memset(&raw, 0, sizeof(raw));
	bp->response_type = XCB_BUTTON_PRESS | 0x80;
	bp->detail = 3;
	bp->time = 0x12345678;
	bp->root = 0x111; bp->event = 0x222; bp->child = 0x333;
	bp->root_x = -5; bp->root_y = 7;
	bp->event_x = 10; bp->event_y = 20;
	bp->state = 0x0104;
	bp->same_screen = 1;
	raw.full_sequence = 0xabcd1234;
	W_CookXcbEvent(NULL, &raw, &o);
	CHECK(o.type == ButtonPress);
	CHECK(o.xany.send_event == True);
	CHECK(o.xany.serial == 0xabcd1234);
	CHECK(o.xbutton.window == 0x222 && o.xany.window == 0x222);
	CHECK(o.xbutton.root == 0x111 && o.xbutton.subwindow == 0x333);
	CHECK(o.xbutton.x == 10 && o.xbutton.y == 20);
	CHECK(o.xbutton.x_root == -5 && o.xbutton.y_root == 7);
	CHECK(o.xbutton.button == 3 && o.xbutton.state == 0x0104);
	CHECK(o.xbutton.time == 0x12345678 && o.xbutton.same_screen == True);

	/* EnterNotify: same_screen and focus share one wire byte */
	xcb_enter_notify_event_t *en = (xcb_enter_notify_event_t *)&raw;
	memset(&raw, 0, sizeof(raw));
	en->response_type = XCB_ENTER_NOTIFY;
	en->detail = XCB_NOTIFY_DETAIL_INFERIOR;
	en->mode = XCB_NOTIFY_MODE_GRAB;
	en->event = 0x444;
	en->same_screen_focus = 0x03;
	W_CookXcbEvent(NULL, &raw, &o);
	CHECK(o.type == EnterNotify && !o.xany.send_event);
	CHECK(o.xcrossing.window == 0x444);
	CHECK(o.xcrossing.detail == NotifyInferior);
	CHECK(o.xcrossing.mode == NotifyGrab);
	CHECK(o.xcrossing.same_screen == True && o.xcrossing.focus == True);
	en->same_screen_focus = 0x02;
	W_CookXcbEvent(NULL, &raw, &o);
	CHECK(o.xcrossing.same_screen == True && o.xcrossing.focus == False);

	/* ConfigureRequest: stack_mode is in the detail byte not a body
	 * field, sibling renames to above, value_mask widens to long */
	xcb_configure_request_event_t *cr = (xcb_configure_request_event_t *)&raw;
	memset(&raw, 0, sizeof(raw));
	cr->response_type = XCB_CONFIGURE_REQUEST;
	cr->stack_mode = XCB_STACK_MODE_BELOW;
	cr->parent = 0x10; cr->window = 0x20; cr->sibling = 0x30;
	cr->x = -1; cr->y = -2; cr->width = 640; cr->height = 480;
	cr->border_width = 2;
	cr->value_mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_STACK_MODE;
	W_CookXcbEvent(NULL, &raw, &o);
	CHECK(o.type == ConfigureRequest);
	CHECK(o.xconfigurerequest.parent == 0x10 && o.xany.window == 0x10);
	CHECK(o.xconfigurerequest.window == 0x20);
	CHECK(o.xconfigurerequest.above == 0x30);
	CHECK(o.xconfigurerequest.detail == Below);
	CHECK(o.xconfigurerequest.x == -1 && o.xconfigurerequest.height == 480);
	CHECK(o.xconfigurerequest.value_mask == (CWX | CWStackMode));

	/* ClientMessage format 32: data32 is uint32_t[5], data.l is
	 * long[5], element-wise widening must preserve all five */
	xcb_client_message_event_t *cm = (xcb_client_message_event_t *)&raw;
	memset(&raw, 0, sizeof(raw));
	cm->response_type = XCB_CLIENT_MESSAGE;
	cm->format = 32;
	cm->window = 0x55; cm->type = 0x66;
	cm->data.data32[0] = 0xdeadbeef;
	cm->data.data32[4] = 0x7fffffff;
	W_CookXcbEvent(NULL, &raw, &o);
	CHECK(o.type == ClientMessage && o.xclient.format == 32);
	CHECK(o.xclient.window == 0x55 && o.xclient.message_type == 0x66);
	CHECK((unsigned long)o.xclient.data.l[0] == 0xdeadbeef);
	CHECK(o.xclient.data.l[4] == 0x7fffffff);

	/* extension event: type past LASTEvent falls through to the
	 * WXWireEvent overlay, xany.window is whatever sits at wire
	 * offset 4 and the 32 wire bytes are copied verbatim */
	memset(&raw, 0, sizeof(raw));
	raw.response_type = 64;
	raw.full_sequence = 999;
	((unsigned char *)&raw)[1]  = 0xab;
	((unsigned char *)&raw)[4]  = 0x21;
	((unsigned char *)&raw)[5]  = 0x43;
	((unsigned char *)&raw)[6]  = 0x65;
	((unsigned char *)&raw)[7]  = 0x87;
	((unsigned char *)&raw)[20] = 0x01;
	W_CookXcbEvent(NULL, &raw, &o);
	CHECK(o.type == 64 && o.xany.serial == 999);
	CHECK(o.xany.window == 0x87654321);
	CHECK(((WXWireEvent *)&o)->wire[1]  == 0xab);
	CHECK(((WXWireEvent *)&o)->wire[20] == 0x01);
}

int main(void)
{
	test_fifo_order();
	test_putback_head();
	test_peek();
	test_take_typed();
	test_take_mask();
	test_drop_range();
	test_cook();

	WXEvqClear();

	return check_report("check_evq");
}
