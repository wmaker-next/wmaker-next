
/*
 *  WINGs internal pending-event queue
 *
 *  Sits between the X connection and dispatch so WMNextEvent /
 *  WMMaskEvent and the modal loops in src/ can take, peek and put back
 *  without depending on Xlib's own queue.  Until WXEvqFlipToXcb() is
 *  called the queue is filled by XNextEvent so the WXEvq* operations
 *  are exact behavioural stand-ins for XCheckMaskEvent /
 *  XCheckTypedEvent / XCheckTypedWindowEvent / XCheckWindowEvent /
 *  XPeekEvent / XPutBackEvent.  After the flip, drain() reads
 *  xcb_poll_for_event() and W_CookXcbEvent() field-copies each wire
 *  event into an XEvent so every consumer keeps its existing
 *  signature, errors (response_type 0) go to the registered
 *  WXEvqErrorHook instead of being enqueued.  Processes that want to
 *  keep libX11's protocol-XIM transport (WPrefs) never call the flip
 *  and stay on the XNextEvent path.
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 */

#include <stdlib.h>

#include <X11/Xlib-xcb.h>
#include <xcb/xcb.h>

#include "WINGsP.h"

/* set by WXEvqFlipToXcb(), NULL while Xlib owns the event queue */
static xcb_connection_t *evq_conn;
static WXEvqErrorHook *evq_error_hook;

typedef struct WXEvqNode {
	XEvent ev;
	struct WXEvqNode *next;
} WXEvqNode;

/* one queue per process, libWINGs has always assumed a single Display */
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
	KeyPressMask,			/* KeyPress */
	KeyReleaseMask,			/* KeyRelease */
	ButtonPressMask,		/* ButtonPress */
	ButtonReleaseMask,		/* ButtonRelease */
	PointerMotionMask | PointerMotionHintMask | ButtonMotionMask
	    | Button1MotionMask | Button2MotionMask | Button3MotionMask
	    | Button4MotionMask | Button5MotionMask,
					/* MotionNotify */
	EnterWindowMask,		/* EnterNotify */
	LeaveWindowMask,		/* LeaveNotify */
	FocusChangeMask,		/* FocusIn */
	FocusChangeMask,		/* FocusOut */
	KeymapStateMask,		/* KeymapNotify */
	ExposureMask,			/* Expose */
	ExposureMask,			/* GraphicsExpose */
	ExposureMask,			/* NoExpose */
	VisibilityChangeMask,		/* VisibilityNotify */
	SubstructureNotifyMask,		/* CreateNotify */
	StructureNotifyMask,		/* DestroyNotify */
	StructureNotifyMask,		/* UnmapNotify */
	StructureNotifyMask,		/* MapNotify */
	SubstructureRedirectMask,	/* MapRequest */
	StructureNotifyMask,		/* ReparentNotify */
	StructureNotifyMask,		/* ConfigureNotify */
	SubstructureRedirectMask,	/* ConfigureRequest */
	StructureNotifyMask,		/* GravityNotify */
	ResizeRedirectMask,		/* ResizeRequest */
	StructureNotifyMask,		/* CirculateNotify */
	SubstructureRedirectMask,	/* CirculateRequest */
	PropertyChangeMask,		/* PropertyNotify */
	0,				/* SelectionClear */
	0,				/* SelectionRequest */
	0,				/* SelectionNotify */
	ColormapChangeMask,		/* ColormapNotify */
	ClientMessageMask,		/* ClientMessage */
	0,				/* MappingNotify */
};

unsigned long W_MaskForEventType(int type)
{
	if (type < 0 || (size_t)type >= wlengthof(eventMasks))
		return 0;
	return eventMasks[type];
}

static WXEvqNode *allocNode(void)
{
	WXEvqNode *n = wmalloc(sizeof(*n));

	n->next = NULL;
	return n;
}

static void appendNode(WXEvqNode *n)
{
	if (evq_tail)
		evq_tail->next = n;
	else
		evq_head = n;
	evq_tail = n;
}

void WXEvqFlipToXcb(Display *dpy)
{
	if (evq_conn)
		return;
	evq_conn = XGetXCBConnection(dpy);
	/* from here on Xlib's _XReadEvents and _XEventsQueued return
	 * immediately, _XReply still picks up errors for Xlib-issued
	 * requests because xcb_io.c require_socket() passes
	 * XCB_REQUEST_CHECKED and _XSend() tracks every sequence, so
	 * the existing XSetErrorHandler trap brackets keep working and
	 * Xlib never enqueues a server event behind our back */
	XSetEventQueueOwner(dpy, XCBOwnsEventQueue);
}

Bool WXEvqXcbOwned(void)
{
	return evq_conn != NULL;
}

void WXEvqSetErrorHook(WXEvqErrorHook *hook)
{
	evq_error_hook = hook;
}

/* pull every event currently readable on the connection into the
 * internal queue, the dpy==NULL case lets the unit test exercise the
 * queue operations on synthetic events with no X server */
static void drain(Display *dpy)
{
	WXEvqNode *n;
	xcb_generic_event_t *xe;

	if (!dpy)
		return;

	if (!evq_conn) {
		/* queue not flipped, XPending flushes the output buffer
		 * and reads the socket so this matches what XCheckMaskEvent
		 * and friends would have buffered */
		while (XPending(dpy)) {
			n = allocNode();
			XNextEvent(dpy, &n->ev);
			appendNode(n);
		}
		/* WMNextEvent and WMMaskEvent no longer XSync before
		 * blocking, and XPending on a dead connection returns 0
		 * without tripping the IO-error handler while the X fd
		 * stays readable on EOF, so the caller would spin, route
		 * through _XReply via one XSync so the registered
		 * XIOErrorHandler fires and the process exits the way it
		 * always has, _XIOError never returns */
		if (xcb_connection_has_error(XGetXCBConnection(dpy)))
			XSync(dpy, False);
		return;
	}

	/* flush Xlib's outgoing buffer the same way XCheckMaskEvent
	 * would have, Xlib routes the write through the shared xcb
	 * socket so request ordering against xcb-issued requests holds */
	XFlush(dpy);

	while ((xe = xcb_poll_for_event(evq_conn)) != NULL) {
		if ((xe->response_type & 0x7f) == 0) {
			/* in-stream error from an xcb-issued reply-less
			 * request, Xlib-issued errors are still collected
			 * by _XReply via the pending_requests list so
			 * never reach here */
			xcb_generic_error_t *err = (xcb_generic_error_t *)xe;
			if (evq_error_hook)
				(*evq_error_hook)(dpy, err->error_code,
						  err->major_code, err->minor_code,
						  err->resource_id, xe->full_sequence);
			free(xe);
			continue;
		}
		n = allocNode();
		W_CookXcbEvent(dpy, xe, &n->ev);
		appendNode(n);
		free(xe);
	}

	/* recover anything libX11 internals XPutBackEvent()ed onto
	 * Xlib's own queue, the @im=local compose-abort replay being
	 * the one known producer, those are already-cooked XEvents so
	 * append them as-is, XQLength reads dpy->qlen directly so sees
	 * them where XPending would return 0 under XCBOwnsEventQueue,
	 * and XNextEvent dequeues without a socket read while qlen>0 */
	while (XQLength(dpy)) {
		n = allocNode();
		XNextEvent(dpy, &n->ev);
		appendNode(n);
	}

	/* same dead-connection guard as the Xlib branch above,
	 * xcb_poll_for_event returns NULL on a closed socket without
	 * raising anything */
	if (xcb_connection_has_error(evq_conn))
		XSync(dpy, False);
}

Bool WXEvqPending(Display *dpy)
{
	drain(dpy);
	return evq_head != NULL;
}

/* pop the head event into *event_ret, draining first so an event that
 * arrived since the last queue inspection is not missed, returns False
 * with *event_ret untouched when the connection has nothing for us, in
 * which case the caller blocks on the X fd via W_HandleInputEvents() */
Bool WXEvqNext(Display *dpy, XEvent *event_ret)
{
	WXEvqNode *n;

	if (!evq_head)
		drain(dpy);

	n = evq_head;
	if (!n)
		return False;

	evq_head = n->next;
	if (!evq_head)
		evq_tail = NULL;

	*event_ret = n->ev;
	wfree(n);
	return True;
}

/* copy the head event without removing it, all three callers in the
 * tree (motion compression in wevent.c, dragsource.c, wcolorpanel.c)
 * peek inside a while-WXEvqPending loop so the queue is known
 * non-empty, the Bool return keeps that contract explicit instead of
 * mirroring XPeekEvent's blocking semantics */
Bool WXEvqPeek(Display *dpy, XEvent *event_ret)
{
	drain(dpy);

	if (!evq_head)
		return False;

	*event_ret = evq_head->ev;
	return True;
}

/* push *event onto the head of the queue so the next WXEvqNext or
 * matching take returns it first, no drain since this is a pure local
 * insert and the caller has just pulled the event it is putting back */
void WXEvqPutBack(const XEvent *event)
{
	WXEvqNode *n = allocNode();

	n->ev = *event;
	n->next = evq_head;
	evq_head = n;
	if (!evq_tail)
		evq_tail = n;
}

static Bool takeMatching(WXEvqNode **pp, XEvent *event_ret)
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
	*event_ret = n->ev;
	wfree(n);
	return True;
}

/* unlink and return the first queued event whose type maps to a bit in
 * mask, restricted to window when window!=None, leaving every other
 * event in arrival order, this is XCheckMaskEvent (window==None) and
 * XCheckWindowEvent (window set) on the internal queue */
Bool WXEvqTakeMask(Display *dpy, long mask, Window window, XEvent *event_ret)
{
	WXEvqNode **pp;

	drain(dpy);

	for (pp = &evq_head; *pp; pp = &(*pp)->next) {
		XEvent *e = &(*pp)->ev;
		if ((W_MaskForEventType(e->type) & mask) == 0)
			continue;
		if (window != None && e->xany.window != window)
			continue;
		return takeMatching(pp, event_ret);
	}
	return False;
}

/* unlink and return the first queued event of the given type,
 * restricted to window when window!=None, this is XCheckTypedEvent
 * (window==None) and XCheckTypedWindowEvent (window set) */
Bool WXEvqTakeTyped(Display *dpy, int type, Window window, XEvent *event_ret)
{
	WXEvqNode **pp;

	drain(dpy);

	for (pp = &evq_head; *pp; pp = &(*pp)->next) {
		XEvent *e = &(*pp)->ev;
		if (e->type != type)
			continue;
		if (window != None && e->xany.window != window)
			continue;
		return takeMatching(pp, event_ret);
	}
	return False;
}

/* drop every queued event whose type maps to a bit in mask and whose
 * serial falls in [begin, end), the sequence-range Enter/Leave
 * suppression bracket from the awesome design sweep, callers record
 * begin = NextRequest(dpy) before the map/unmap burst and end after,
 * then call this once instead of looping XCheckTypedEvent and risking
 * a swallow of a legitimate crossing that arrived in the same read */
int WXEvqDropRange(Display *dpy, unsigned long begin, unsigned long end, long mask)
{
	WXEvqNode **pp = &evq_head;
	WXEvqNode *last = NULL;
	int dropped = 0;

	drain(dpy);

	while (*pp) {
		WXEvqNode *n = *pp;
		if (n->ev.xany.serial >= begin && n->ev.xany.serial < end &&
		    (W_MaskForEventType(n->ev.type) & mask)) {
			*pp = n->next;
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
		wfree(n);
	}
	evq_tail = NULL;
}
