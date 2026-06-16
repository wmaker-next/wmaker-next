
/*
 *  WINGs internal pending-event queue
 *
 *  Sits between the X connection and dispatch so WMNextEvent /
 *  WMMaskEvent and the modal loops in src/ can take, peek and put back
 *  without depending on Xlib's own queue. Today the queue is filled by
 *  XNextEvent so the WXEvq* operations are exact behavioural stand-ins
 *  for XCheckMaskEvent / XCheckTypedEvent / XCheckTypedWindowEvent /
 *  XCheckWindowEvent / XPeekEvent / XPutBackEvent, after the xcb queue
 *  flip the drain step becomes xcb_poll_for_event() plus the
 *  wire-to-XEvent cooker and nothing else in this file changes.
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 */

#include "WINGsP.h"

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

/* pull every event currently readable on the connection into the
 * internal queue, XPending flushes the output buffer and reads the
 * socket so this matches what XCheckMaskEvent and friends would have
 * buffered, the dpy==NULL case lets the unit test exercise the queue
 * operations on synthetic events with no X server */
static void drain(Display *dpy)
{
	WXEvqNode *n;

	if (!dpy)
		return;

	while (XPending(dpy)) {
		n = allocNode();
		XNextEvent(dpy, &n->ev);
		appendNode(n);
	}
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
