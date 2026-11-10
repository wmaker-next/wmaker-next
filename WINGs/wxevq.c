
/*
 *  WINGs internal pending-event queue
 *
 *  Sits between the X connection and dispatch so WMNextEvent /
 *  WMMaskEvent and the modal loops in src/ can take, peek and put back
 *  without an extra queue. The queue stores raw xcb_generic_event_t
 *  events fed straight from xcb_poll_for_event() on the connection
 *  the queue operations receive (registered on the first
 *  drain, one queue per process). The delivery operations hand the
 *  malloc'd buffer to the caller (owned), Peek lends the queue's
 *  pointer, PutBack takes ownership of what it is given. Errors
 *  (response_type 0) go to the registered WXEvqErrorHook instead of
 *  being enqueued.
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 */

#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <poll.h>
#include <time.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include "WINGsP.h"

/* the connection the queue drains from, registered by the first drain
 * from the connection the queue operation received, NULL only in the
 * headless unit tests which inject events through W_XEvqPutRaw() */
static xcb_connection_t *evq_conn;
static xcb_connection_t *evq_dpy;
static WXEvqErrorHook *evq_error_hook;
static WXEvqDeadHook *evq_dead_hook;

typedef struct WXEvqNode {
	struct WXEvqNode *next;
	xcb_generic_event_t *xe;	/* malloc'd wire event, owned by the queue */
} WXEvqNode;

/* one queue per process, libWINGs has always assumed a single xcb_connection_t */
static WXEvqNode *evq_head;
static WXEvqNode *evq_tail;

/* table to map an event type to the select-mask bits that would have
 * caused it to be delivered, used by WXEvqTakeMask the same way
 * XCheckMaskEvent matches against Xlib's _Xevent_to_mask[], and by
 * wevent.c via W_MaskForEventType() to route events to per-view
 * handlers */
static const unsigned long eventMasks[] = {
	0,
	0,
	XCB_EVENT_MASK_KEY_PRESS,			/* KeyPress */
	XCB_EVENT_MASK_KEY_RELEASE,			/* KeyRelease */
	XCB_EVENT_MASK_BUTTON_PRESS,		/* ButtonPress */
	XCB_EVENT_MASK_BUTTON_RELEASE,		/* ButtonRelease */
	XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_POINTER_MOTION_HINT | XCB_EVENT_MASK_BUTTON_MOTION
	    | XCB_EVENT_MASK_BUTTON_1_MOTION | XCB_EVENT_MASK_BUTTON_2_MOTION | XCB_EVENT_MASK_BUTTON_3_MOTION
	    | XCB_EVENT_MASK_BUTTON_4_MOTION | XCB_EVENT_MASK_BUTTON_5_MOTION,
					/* MotionNotify */
	XCB_EVENT_MASK_ENTER_WINDOW,		/* EnterNotify */
	XCB_EVENT_MASK_LEAVE_WINDOW,		/* LeaveNotify */
	XCB_EVENT_MASK_FOCUS_CHANGE,		/* FocusIn */
	XCB_EVENT_MASK_FOCUS_CHANGE,		/* FocusOut */
	XCB_EVENT_MASK_KEYMAP_STATE,		/* KeymapNotify */
	XCB_EVENT_MASK_EXPOSURE,			/* Expose */
	XCB_EVENT_MASK_EXPOSURE,			/* GraphicsExpose */
	XCB_EVENT_MASK_EXPOSURE,			/* NoExpose */
	XCB_EVENT_MASK_VISIBILITY_CHANGE,		/* VisibilityNotify */
	XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,		/* CreateNotify */
	XCB_EVENT_MASK_STRUCTURE_NOTIFY,		/* DestroyNotify */
	XCB_EVENT_MASK_STRUCTURE_NOTIFY,		/* UnmapNotify */
	XCB_EVENT_MASK_STRUCTURE_NOTIFY,		/* MapNotify */
	XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,	/* MapRequest */
	XCB_EVENT_MASK_STRUCTURE_NOTIFY,		/* ReparentNotify */
	XCB_EVENT_MASK_STRUCTURE_NOTIFY,		/* ConfigureNotify */
	XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,	/* ConfigureRequest */
	XCB_EVENT_MASK_STRUCTURE_NOTIFY,		/* GravityNotify */
	XCB_EVENT_MASK_RESIZE_REDIRECT,		/* ResizeRequest */
	XCB_EVENT_MASK_STRUCTURE_NOTIFY,		/* CirculateNotify */
	XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,	/* CirculateRequest */
	XCB_EVENT_MASK_PROPERTY_CHANGE,		/* PropertyNotify */
	0,				/* SelectionClear */
	0,				/* SelectionRequest */
	0,				/* SelectionNotify */
	XCB_EVENT_MASK_COLOR_MAP_CHANGE,		/* ColormapNotify */
	ClientMessageMask,		/* ClientMessage */
	0,				/* MappingNotify */
};

uint32_t W_MaskForEventType(int type)
{
	if (type < 0 || (size_t)type >= wlengthof(eventMasks))
		return 0;
	return eventMasks[type];
}

/* the field that used to land in XEvent's xany.window sits at wire
 * offset 12 for the key/button/motion/crossing family (after time and
 * root), at offset 8 for the selection family (they carry a leading
 * time field, xany.window aliases the owner/requestor) and at the
 * first body slot for every other windowed core event; KeymapNotify
 * and MappingNotify carry none. Extension events have no window
 * either (offset 4 is a timestamp or a pad), so they are never
 * window-matched and reach their owning module unfiltered */
static int eventWindowWireOffset(int type)
{
	switch (type) {
	case XCB_KEY_PRESS:
	case XCB_KEY_RELEASE:
	case XCB_BUTTON_PRESS:
	case XCB_BUTTON_RELEASE:
	case XCB_MOTION_NOTIFY:
	case XCB_ENTER_NOTIFY:
	case XCB_LEAVE_NOTIFY:
		return 12;
	case XCB_SELECTION_CLEAR:
	case XCB_SELECTION_REQUEST:
	case XCB_SELECTION_NOTIFY:
		return 8;
	case XCB_KEYMAP_NOTIFY:
	case XCB_MAPPING_NOTIFY:
		return -1;
	default:
		if (type >= wlengthof(eventMasks))
			return -1;
		return 4;
	}
}

/* the window the dispatchers match on, the xany.window equivalent for
 * a wire event */
xcb_window_t W_EventWindow(const xcb_generic_event_t *xe)
{
	uint32_t w;
	int off;

	off = eventWindowWireOffset(xe->response_type & 0x7f);
	if (off < 0)
		return XCB_NONE;
	memcpy(&w, (const unsigned char *)xe + off, sizeof(w));
	return w;
}

static WXEvqNode *allocNode(void)
{
	return wmalloc(sizeof(WXEvqNode));
}

static void appendNode(WXEvqNode *n)
{
	if (evq_tail)
		evq_tail->next = n;
	else
		evq_head = n;
	evq_tail = n;
}

/* queue one raw xcb event, taking ownership of the malloc'd buffer
 * (xcb_poll_for_event allocation, free()d by the queue when delivered
 * or dropped), drain()'s xcb branch and the check_evq seam both come
 * through here */
void W_XEvqPutRaw(void *xe_)
{
	xcb_generic_event_t *xe = xe_;
	WXEvqNode *n = allocNode();

	n->xe = xe;
	appendNode(n);
}

void WXEvqSetErrorHook(WXEvqErrorHook *hook)
{
	evq_error_hook = hook;
}

/* The hook for a connection that died (xcb_connection_has_error()).
 * Xlib's IO-error handler used to die here; the owner of the
 * connection decides how (wmaker shuts down cleanly). */
void WXEvqSetDeadHook(WXEvqDeadHook *hook)
{
	evq_dead_hook = hook;
}

/* Run the dead hook outside drain(): the connection error can surface
 * in the fetchers after W_HandleInputEvents marked it inside the
 * blocked wait. The connection owner's hook decides how to die
 * (wmaker's is noreturn, the daemons' hooks return and their loops
 * check the NULL fetch result). With no hook installed there is
 * nothing to hand the failure to, so exit the same way drain() does. */
void WXEvqConnectionDead(xcb_connection_t *dpy)
{
	if (evq_dead_hook)
		(*evq_dead_hook)(dpy);
	else
		exit(1);
}

/* the connection the queue drains from, the display pointer that used
 * to travel in the cooked event's xany prefix */
xcb_connection_t *W_XEvqConnection(void)
{
	return evq_dpy;
}

/* ---- drain ------------------------------------------------------------ */

/* pull every event currently readable on the connection into the
 * internal queue, the dpy==NULL case lets the unit test exercise the
 * queue operations on synthetic events with no X server */
static void drain(xcb_connection_t *dpy)
{
	xcb_generic_event_t *xe;

	if (!dpy)
		return;

	/* the connection registers itself on the first drain, the caller
	 * that owns the queue is the connection owner */
	if (!evq_conn) {
		evq_conn = dpy;
		evq_dpy = dpy;
		if (!evq_conn)
			return;
	}

	/* flush the outgoing buffer the same way XCheckMaskEvent would
	 * have pushed its requests, so events answering them are
	 * readable on the poll below */
	wXFlush(dpy);

	while ((xe = xcb_poll_for_event(evq_conn)) != NULL) {
		if ((xe->response_type & 0x7f) == 0) {
			/* in-stream error from a reply-less xcb request */
			xcb_generic_error_t *err = (xcb_generic_error_t *)xe;
			if (evq_error_hook)
				(*evq_error_hook)(dpy, err->error_code,
						  err->major_code, err->minor_code,
						  err->resource_id, xe->full_sequence);
			free(xe);
			continue;
		}
		W_XEvqPutRaw(xe);
	}

	/* dead-connection guard: xcb_poll_for_event returns NULL on a
	 * closed socket without raising anything, so this is the one
	 * place a lost connection surfaces. The registered hook dies
	 * (the connection owner decides how); with no hook installed
	 * there is nothing to hand the failure to, so exit. */
	if (xcb_connection_has_error(evq_conn)) {
		if (evq_dead_hook)
			(*evq_dead_hook)(dpy);
		else
			exit(1);
	}
}

bool WXEvqPending(xcb_connection_t *dpy)
{
	drain(dpy);
	return evq_head != NULL;
}

/* pop the head event into *event_ret, draining first so an event that
 * arrived since the last queue inspection is not missed, returns False
 * with *event_ret untouched when the connection has nothing for us, in
 * which case the caller blocks on the X fd via W_HandleInputEvents().
 * The malloc'd wire buffer becomes the caller's responsibility. */
bool WXEvqNext(xcb_connection_t *dpy, xcb_generic_event_t **event_ret)
{
	WXEvqNode *n;

	if (!evq_head)
		drain(dpy);

	n = evq_head;
	if (!n)
		return false;

	evq_head = n->next;
	if (!evq_head)
		evq_tail = NULL;

	*event_ret = n->xe;
	wfree(n);
	return true;
}

/* lend the head event without removing it, the pointer stays owned by
 * the queue and is valid until the next queue mutation, all three
 * callers in the tree (motion compression in wevent.c, dragsource.c,
 * wcolorpanel.c) peek inside a while-WXEvqPending loop so the queue is
 * known non-empty, the Bool return keeps that contract explicit
 * instead of mirroring XPeekEvent's blocking semantics */
bool WXEvqPeek(xcb_connection_t *dpy, xcb_generic_event_t **event_ret)
{
	drain(dpy);

	if (!evq_head)
		return false;

	*event_ret = evq_head->xe;
	return true;
}

/* push *event onto the head of the queue so the next WXEvqNext or
 * matching take returns it first, no drain since this is a pure local
 * insert and the caller has just pulled the event it is putting back,
 * ownership of the malloc'd buffer moves to the queue */
void WXEvqPutBack(xcb_generic_event_t *event)
{
	WXEvqNode *n = allocNode();

	n->xe = event;
	n->next = evq_head;
	evq_head = n;
	if (!evq_tail)
		evq_tail = n;
}

static bool takeMatching(WXEvqNode **pp, xcb_generic_event_t **event_ret)
{
	WXEvqNode *n = *pp;

	*pp = n->next;
	if (n == evq_tail) {
		/* unlinked the last node, the new tail is whatever now has a
		 * NULL next, walk for it only in this case so the common
		 * head/middle take stays O(1) past the search */
		WXEvqNode *t = evq_head;
		while (t && t->next)
			t = t->next;
		evq_tail = t;
	}
	*event_ret = n->xe;
	wfree(n);
	return true;
}

/* unlink and return the first queued event whose type maps to a bit in
 * mask, restricted to window when window!=None, leaving every other
 * event in arrival order, this is XCheckMaskEvent (window==None) and
 * XCheckWindowEvent (window set) on the internal queue, the taken
 * event becomes the caller's responsibility */
bool WXEvqTakeMask(xcb_connection_t *dpy, uint32_t mask, xcb_window_t window,
                   xcb_generic_event_t **event_ret)
{
	WXEvqNode **pp;

	drain(dpy);

	for (pp = &evq_head; *pp; pp = &(*pp)->next) {
		xcb_generic_event_t *e = (*pp)->xe;
		if ((W_MaskForEventType(e->response_type & 0x7f) & mask) == 0)
			continue;
		if (window != XCB_NONE && W_EventWindow(e) != window)
			continue;
		return takeMatching(pp, event_ret);
	}
	return false;
}

/* unlink and return the first queued event of the given type,
 * restricted to window when window!=None, this is XCheckTypedEvent
 * (window==None) and XCheckTypedWindowEvent (window set), the taken
 * event becomes the caller's responsibility */
bool WXEvqTakeTyped(xcb_connection_t *dpy, int type, xcb_window_t window,
                    xcb_generic_event_t **event_ret)
{
	WXEvqNode **pp;

	drain(dpy);

	for (pp = &evq_head; *pp; pp = &(*pp)->next) {
		xcb_generic_event_t *e = (*pp)->xe;
		if ((e->response_type & 0x7f) != type)
			continue;
		if (window != XCB_NONE && W_EventWindow(e) != window)
			continue;
		return takeMatching(pp, event_ret);
	}
	return false;
}

/* ---- timed takes ------------------------------------------------------ */

/* one monotonic millisecond clock for the timed waits, deadlines are
 * absolute so a poll interruption shrinks the remaining wait instead
 * of restarting the full timeout */
static long long evq_monotonic_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Timed takes do not dispatch unrelated callbacks while their caller
 * holds modal state. */
typedef bool WXEvqTakeStep(xcb_connection_t *dpy, void *arg, xcb_generic_event_t **event_ret);

static bool waitTakeTimed(xcb_connection_t *dpy, WXEvqTakeStep *take, void *arg, xcb_generic_event_t **event_ret, int timeout_ms)
{
	struct pollfd pfd;
	long long deadline;
	int rc;

	deadline = (timeout_ms < 0) ? -1 : evq_monotonic_ms() + timeout_ms;
	while (!take(dpy, arg, event_ret)) {
		if (deadline >= 0) {
			timeout_ms = (int)(deadline - evq_monotonic_ms());
			if (timeout_ms <= 0)
				return false;
		}
		pfd.fd = dpy ? xcb_get_file_descriptor(dpy) : -1;
		pfd.events = POLLIN;
		rc = poll(&pfd, 1, timeout_ms);
		if (rc < 0 && errno == EINTR)
			continue;
		if (rc <= 0)
			return false;
		if (dpy && xcb_connection_has_error(dpy))
			return false;
	}
	return true;
}

struct TakeMaskArg {
	uint32_t mask;
	xcb_window_t window;
};

static bool takeMasked(xcb_connection_t *dpy, void *arg_, xcb_generic_event_t **event_ret)
{
	struct TakeMaskArg *arg = arg_;

	return WXEvqTakeMask(dpy, arg->mask, arg->window, event_ret);
}

bool WXEvqTakeMaskTimed(xcb_connection_t *dpy, uint32_t mask, xcb_window_t window, xcb_generic_event_t **event_ret, int timeout_ms)
{
	struct TakeMaskArg arg;

	arg.mask = mask;
	arg.window = window;
	return waitTakeTimed(dpy, takeMasked, &arg, event_ret, timeout_ms);
}

struct TakeTypedArg {
	int type;
	xcb_window_t window;
	WXEvqMatchProc *match;
	void *cdata;
};

/* take typed events until one passes match, a NULL match takes the
 * first typed event, non-matching events are freed */
static bool takeTypedMatched(xcb_connection_t *dpy, void *arg_, xcb_generic_event_t **event_ret)
{
	struct TakeTypedArg *arg = arg_;
	xcb_generic_event_t *e;

	while (WXEvqTakeTyped(dpy, arg->type, arg->window, &e)) {
		if (!arg->match || arg->match(e, arg->cdata)) {
			*event_ret = e;
			return true;
		}
		free(e);
	}
	return false;
}

bool WXEvqTakeTypedTimed(xcb_connection_t *dpy, int type, xcb_window_t window, WXEvqMatchProc *match, void *cdata, xcb_generic_event_t **event_ret, int timeout_ms)
{
	struct TakeTypedArg arg;

	arg.type = type;
	arg.window = window;
	arg.match = match;
	arg.cdata = cdata;
	return waitTakeTimed(dpy, takeTypedMatched, &arg, event_ret, timeout_ms);
}

/* drop every queued event whose type maps to a bit in mask and whose
 * serial falls in [begin, end), the sequence-range Enter/Leave
 * suppression bracket from the awesome design sweep, callers record
 * begin = NextRequest(dpy) before the map/unmap burst and end after,
 * then call this once instead of looping XCheckTypedEvent and risking
 * a swallow of a legitimate crossing that arrived in the same read */
int WXEvqDropRange(xcb_connection_t *dpy, unsigned long begin, unsigned long end, uint32_t mask)
{
	WXEvqNode **pp = &evq_head;
	WXEvqNode *last = NULL;
	int dropped = 0;

	drain(dpy);

	while (*pp) {
		WXEvqNode *n = *pp;
		unsigned long serial = n->xe->full_sequence;
		if (serial >= begin && serial < end &&
		    (W_MaskForEventType(n->xe->response_type & 0x7f) & mask)) {
			*pp = n->next;
			free(n->xe);
			wfree(n);
			dropped++;
		} else {
			last = n;
			pp = &n->next;
		}
	}
	evq_tail = last;
	return dropped;
}

void WXEvqClear(void)
{
	WXEvqNode *n;

	while ((n = evq_head) != NULL) {
		evq_head = n->next;
		free(n->xe);
		wfree(n);
	}
	evq_tail = NULL;
}
