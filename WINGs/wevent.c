/*
 *  wevent.c
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 1998 scottc
 *  Copyright (c) 1998-2004 Dan Pascu
 *  Copyright (c) 1999-2001 Alfredo K. Kojima
 *  Copyright (c) 2009-2026 Window Maker Team
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, see <https://www.gnu.org/licenses/>.
 */

/*
 * This event handling stuff was inspired on Tk.
 */

#include "WINGsP.h"

/* hook for other toolkits or wmaker process their events */
static WMEventHook *extraEventHandler = NULL;

/*
 * WMCreateEventHandler--
 * 	Create an event handler and put it in the event handler list for the
 * view. If the same callback and clientdata are already used in another
 * handler, the masks are OR'ed.
 *
 */
void WMCreateEventHandler(WMView * view, uint32_t mask, WMEventProc * eventProc, void *clientData)
{
	W_EventHandler *hPtr;
	WMArrayIterator iter;

	WM_ITERATE_ARRAY(view->eventHandlers, hPtr, iter) {
		if (hPtr->clientData == clientData && hPtr->proc == eventProc) {
			hPtr->eventMask |= mask;
			return;
		}
	}

	hPtr = wmalloc(sizeof(W_EventHandler));

	/* select events for window */
	hPtr->eventMask = mask;
	hPtr->proc = eventProc;
	hPtr->clientData = clientData;

	WMAddToArray(view->eventHandlers, hPtr);
}

static int matchHandler(const void *item, const void *cdata)
{
	const W_EventHandler *h1 = item;
	const W_EventHandler *h2 = cdata;

	return ((h1->eventMask == h2->eventMask) &&
			  (h1->proc == h2->proc) &&
			  (h1->clientData == h2->clientData));
}

/*
 * WMDeleteEventHandler--
 * 	Delete event handler matching arguments from windows
 * event handler list.
 *
 */
void WMDeleteEventHandler(WMView * view, uint32_t mask, WMEventProc * eventProc, void *clientData)
{
	W_EventHandler tmp;

	tmp.eventMask = mask;
	tmp.proc = eventProc;
	tmp.clientData = clientData;
	WMRemoveFromArrayMatching(view->eventHandlers, matchHandler, (void *)&tmp);
}

static xcb_timestamp_t getEventTime(WMScreen * screen, xcb_generic_event_t * event)
{
	switch (event->response_type & 0x7f) {
	case XCB_BUTTON_PRESS:
	case XCB_BUTTON_RELEASE:
		return ((xcb_button_press_event_t *)event)->time;
	case XCB_KEY_PRESS:
	case XCB_KEY_RELEASE:
		return ((xcb_key_press_event_t *)event)->time;
	case XCB_MOTION_NOTIFY:
		return ((xcb_motion_notify_event_t *)event)->time;
	case XCB_ENTER_NOTIFY:
	case XCB_LEAVE_NOTIFY:
		return ((xcb_enter_notify_event_t *)event)->time;
	case XCB_PROPERTY_NOTIFY:
		return ((xcb_property_notify_event_t *)event)->time;
	case XCB_SELECTION_CLEAR:
		return ((xcb_selection_clear_event_t *)event)->time;
	case XCB_SELECTION_REQUEST:
		return ((xcb_selection_request_event_t *)event)->time;
	case XCB_SELECTION_NOTIFY:
		return ((xcb_selection_notify_event_t *)event)->time;
	default:
		return screen->lastEventTime;
	}
}

void W_CallDestroyHandlers(W_View * view)
{
	xcb_generic_event_t *event;
	xcb_destroy_notify_event_t *de;
	WMArrayIterator iter;
	W_EventHandler *hPtr;

	/* synthesized destroy notify, the handlers only borrow it */
	event = W_SynthEvent(XCB_DESTROY_NOTIFY);
	de = (xcb_destroy_notify_event_t *)event;
	de->event = view->window;
	de->window = view->window;

	WM_ITERATE_ARRAY(view->eventHandlers, hPtr, iter) {
		if (hPtr->eventMask & XCB_EVENT_MASK_STRUCTURE_NOTIFY) {
			(*hPtr->proc) (event, hPtr->clientData);
		}
	}

	free(event);
}

void WMSetViewNextResponder(WMView * view, WMView * responder)
{
	/* set the widget to receive keyboard events that aren't handled
	 * by this widget */

	view->nextResponder = responder;
}

void WMRelayToNextResponder(WMView * view, xcb_generic_event_t * event)
{
	uint32_t mask = W_MaskForEventType(event->response_type & 0x7f);

	if (view->nextResponder) {
		WMView *next = view->nextResponder;
		W_EventHandler *hPtr;
		WMArrayIterator iter;

		WM_ITERATE_ARRAY(next->eventHandlers, hPtr, iter) {
			if ((hPtr->eventMask & mask)) {
				(*hPtr->proc) (event, hPtr->clientData);
			}
		}
	}
}

/* dispatch one event. The caller keeps ownership of the malloc'd
 * wire buffer and frees it exactly once when done; *event is an
 * in-out so motion/expose compression can replace it with the newest
 * queued event of the same family, the owner then frees that one
 * instead. Handlers only borrow. */
/* Motion/expose compression at pop time: the fetchers (WMNextEvent,
 * WMMaskEvent) collapse matching queued successors so every dispatch
 * loop frees exactly the pointer it popped. Only WINGs-view windows
 * compress, mirroring the dispatch rules; a viewless event passes
 * through untouched. Returns the surviving buffer. */
static xcb_generic_event_t *compressPoppedEvent(xcb_generic_event_t *event)
{
	W_View *view;
	int type = event->response_type & 0x7f;

	if (type != XCB_MOTION_NOTIFY && type != XCB_EXPOSE)
		return event;

	view = W_GetViewForXWindow(W_EventWindow(event));
	if (!view)
		return event;

	if (type == XCB_MOTION_NOTIFY && !view->flags.dontCompressMotion) {
		xcb_generic_event_t *ev;
		while (WXEvqPeek(W_XEvqConnection(), &ev)) {
			if ((ev->response_type & 0x7f) == XCB_MOTION_NOTIFY) {
				const xcb_motion_notify_event_t *om =
					(const xcb_motion_notify_event_t *)event;
				const xcb_motion_notify_event_t *nm =
					(const xcb_motion_notify_event_t *)ev;
				if (om->event != nm->event
				    || om->child != nm->child)
					break;
				/* consume the newer motion and keep it,
				 * the caller frees what it is handed */
				if (!WXEvqNext(W_XEvqConnection(), &ev))
					break;
				free(event);
				event = ev;
			} else
				break;
		}
	}

	if (type == XCB_EXPOSE && !view->flags.dontCompressExpose) {
		xcb_generic_event_t *ev;
		xcb_window_t win = view->window;

		while (WXEvqTakeTyped(W_XEvqConnection(), XCB_EXPOSE, win, &ev)) {
			free(event);
			event = ev;
		}
	}

	return event;
}

/* the pop-time motion/expose compression the fetchers apply, for
 * ProcessPendingEvents' raw WXEvqNext pops which skip re-draining;
 * void * so this header does not pull xcb.h into every WINGs source */
void W_XEvqCompressPop(void *slot)
{
	xcb_generic_event_t **event = slot;

	if (*event)
		*event = compressPoppedEvent(*event);
}

int WMHandleEvent(xcb_generic_event_t *event)
{
	W_EventHandler *hPtr;
	W_View *view, *toplevel;
	uint32_t mask;
	xcb_window_t window;
	int type = event->response_type & 0x7f;
	WMArrayIterator iter;

	if (type == XCB_MAPPING_NOTIFY) {
		/* core MappingNotify has no view to dispatch to and
		 * remaps surface as XKB MapNotify (the extension
		 * opcode), which drives the keymap refreshes, so the
		 * core event is a no-op here */
		return true;
	}

	/* refresh the per-screen xkbcommon keymap/state from the XKB
	 * event stream, the event keeps flowing so the process's own
	 * extension dispatch (wmaker's w_event_handler table) still
	 * sees it */
	W_XkbHandleEvent(event);

	/* route XIM protocol ClientMessages / PropertyNotify /
	 * DestroyNotify through xcb-imdkit. Key events are NOT filtered
	 * here, they reach wtextfield via W_IMForwardKey instead. */
	if (W_IMFilterEventGlobal(event)) {
		return false;
	}

	mask = W_MaskForEventType(type);

	window = W_EventWindow(event);

	/* diferentiate SubstructureNotify with StructureNotify */
	if (mask == XCB_EVENT_MASK_STRUCTURE_NOTIFY) {
		const xcb_map_notify_event_t *me =
			(const xcb_map_notify_event_t *)event;
		if (me->event != me->window) {
			mask = XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;
			window = me->event;
		}
	}
	view = W_GetViewForXWindow(window);

	if (!view) {
		if (extraEventHandler)
			(extraEventHandler) (event);

		return false;
	}

	view->screen->lastEventTime = getEventTime(view->screen, event);

	toplevel = W_TopLevelOfView(view);

	/* every callback path below (selection handlers, balloon, the
	 * eventHandlers loop, DND) can destroy the widget that received
	 * the event, retain both the toplevel and the view now so the
	 * pointers stay valid through to the matching releases at the
	 * end of this function */
	W_RetainView(toplevel);
	W_RetainView(view);

	if (type == XCB_SELECTION_NOTIFY || type == XCB_SELECTION_CLEAR || type == XCB_SELECTION_REQUEST) {
		/* handle selection related events */
		W_HandleSelectionEvent(event);

	}

	/* if it's a key event, redispatch it to the focused control */
	if (mask & (XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE)) {
		W_View *focused = W_FocusedViewOfToplevel(toplevel);

		if (focused) {
			W_ReleaseView(view);
			view = focused;
			W_RetainView(view);
		}
	}

	/* Events on a scaled W_View window carry device-pixel coordinates,
	 * translate to the logical units the widget handlers were written
	 * for. Compression has already picked the surviving event at pop
	 * time, so it is scaled once. x_root/y_root are left in device px
	 * since they are only used for screen-relative placement. */
	if (view->screen->uiScale > 1.0001) {
		double is = 1.0 / view->screen->uiScale;
		switch (type) {
		case XCB_BUTTON_PRESS:
		case XCB_BUTTON_RELEASE: {
			xcb_button_press_event_t *be =
				(xcb_button_press_event_t *)event;
			be->event_x = (int)(be->event_x * is);
			be->event_y = (int)(be->event_y * is);
			break;
		}
		case XCB_MOTION_NOTIFY: {
			xcb_motion_notify_event_t *me =
				(xcb_motion_notify_event_t *)event;
			me->event_x = (int)(me->event_x * is);
			me->event_y = (int)(me->event_y * is);
			break;
		}
		case XCB_ENTER_NOTIFY:
		case XCB_LEAVE_NOTIFY: {
			xcb_enter_notify_event_t *en =
				(xcb_enter_notify_event_t *)event;
			en->event_x = (int)(en->event_x * is);
			en->event_y = (int)(en->event_y * is);
			break;
		}
		case XCB_EXPOSE: {
			xcb_expose_event_t *ee = (xcb_expose_event_t *)event;
			ee->x = (int)(ee->x * is);
			ee->y = (int)(ee->y * is);
			ee->width = (int)(ee->width * is + 0.999);
			ee->height = (int)(ee->height * is + 0.999);
			break;
		}
		case XCB_CONFIGURE_NOTIFY: {
			xcb_configure_notify_event_t *ce =
				(xcb_configure_notify_event_t *)event;
			ce->x = (int)(ce->x * is);
			ce->y = (int)(ce->y * is);
			ce->width = (int)(ce->width * is + 0.5);
			ce->height = (int)(ce->height * is + 0.5);
			break;
		}
		}
	}

	if (view->screen->modalLoop && toplevel != view->screen->modalView && !toplevel->flags.worksWhenModal) {
		if (type == XCB_KEY_PRESS || type == XCB_KEY_RELEASE
		    || type == XCB_MOTION_NOTIFY || type == XCB_BUTTON_PRESS
		    || type == XCB_BUTTON_RELEASE || type == XCB_FOCUS_IN || type == XCB_FOCUS_OUT) {
			W_ReleaseView(view);
			W_ReleaseView(toplevel);
			return true;
		}
	}

	/* do balloon stuffs */
	if (type == XCB_ENTER_NOTIFY)
		W_BalloonHandleEnterView(view);
	else if (type == XCB_LEAVE_NOTIFY)
		W_BalloonHandleLeaveView(view);

	WM_ITERATE_ARRAY(view->eventHandlers, hPtr, iter) {
		if ((hPtr->eventMask & mask)) {
			(*hPtr->proc) (event, hPtr->clientData);
		}
	}

	/* Key events are redirected to the focused control above, so
	 * panel-wide handlers on the toplevel view never see them.
	 * Run the toplevel handlers after the focused view. */
	if ((mask & (XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE)) && view != toplevel) {
		WM_ITERATE_ARRAY(toplevel->eventHandlers, hPtr, iter) {
			if (hPtr->eventMask & mask)
				(*hPtr->proc) (event, hPtr->clientData);
		}
	}
#if 0
	/* pass the event to the top level window of the widget */
	/* TODO: change this to a responder chain */
	if (view->parent != NULL) {
		vPtr = view;
		while (vPtr->parent != NULL)
			vPtr = vPtr->parent;

		WM_ITERATE_ARRAY(vPtr->eventHandlers, hPtr, iter) {
			if (hPtr->eventMask & mask) {
				(*hPtr->proc) (event, hPtr->clientData);
			}
		}
	}
#endif
	/* save button click info to track double-clicks */
	if (view->screen->ignoreNextDoubleClick) {
		view->screen->ignoreNextDoubleClick = 0;
	} else {
		if (type == XCB_BUTTON_PRESS) {
			const xcb_button_press_event_t *be =
				(const xcb_button_press_event_t *)event;
			view->screen->lastClickWindow = be->event;
			view->screen->lastClickTime = be->time;
		}
	}

	W_ReleaseView(view);

	if (type == XCB_CLIENT_MESSAGE) {
		/* must be handled at the end, for such message can destroy the view */
		W_HandleDNDClientMessage(toplevel, (xcb_client_message_event_t *)event);
	}

	W_ReleaseView(toplevel);

	return true;
}


int WMIsDoubleClick(xcb_generic_event_t * event)
{
	W_View *view;
	const xcb_button_press_event_t *be = (const xcb_button_press_event_t *)event;

	if ((event->response_type & 0x7f) != XCB_BUTTON_PRESS)
		return false;

	view = W_GetViewForXWindow(be->event);

	if (!view)
		return false;

	if (view->screen->lastClickWindow != be->event)
		return false;

	if (be->time - view->screen->lastClickTime < WINGsConfiguration.doubleClickDelay) {
		view->screen->lastClickTime = 0;
		view->screen->lastClickWindow = XCB_NONE;
		view->screen->ignoreNextDoubleClick = 1;
		return true;
	} else
		return false;
}

/*
 * One blocking ev_run() iteration via W_HandleInputEvents() dispatches
 * everything the old peek-idle-peek-block ladder did in one pass: due
 * timers, idle handlers when nothing else is pending, registered input
 * handlers, and the X connection fd. The X-fd watcher ev_break()s so
 * control returns here to drain the connection into the WINGs internal
 * event queue. The ASAP and WhenIdle notification queues flush from the
 * loop's prepare and check watchers, so there is no separate timer/idle
 * pump.
 *
 * No XSync before blocking: WXEvqPending and WXEvqTakeMask both flush
 * the output buffer (XPending == QueuedAfterFlush on the Xlib path,
 * explicit XFlush on the xcb path) and read the socket dry, so when
 * they return False there is nothing more to ask the server and
 * ev_run() can sleep on the X fd. drain() carries an
 * xcb_connection_has_error() check so a dead connection still reaches
 * the XIOErrorHandler instead of spinning on a permanently-readable
 * EOF fd.
 */

void WMNextEvent(xcb_connection_t * dpy, xcb_generic_event_t ** event)
{
	while (!WXEvqPending(dpy)) {
		W_HandleInputEvents(true, xcb_get_file_descriptor(dpy));
		if (xcb_connection_has_error(dpy)) {
			/* the error can surface here, outside drain(): a
			 * due timer or the prepare hook flushed requests
			 * down the just-dead socket. Run the same dead
			 * hook: a noreturn owner never hands the NULL to
			 * a consumer, a returning owner hands the NULL
			 * result to its loop's check instead. */
			WXEvqConnectionDead(dpy);
			*event = NULL;
			return;
		}
	}

	WXEvqNext(dpy, event);
	if (*event)
		*event = compressPoppedEvent(*event);
}

void WMMaskEvent(xcb_connection_t * dpy, uint32_t mask, xcb_generic_event_t ** event)
{
	/* WXEvqTakeMask drains everything readable into the internal
	 * queue and returns the first match, the non-matching events
	 * stay queued in arrival order for the next WMNextEvent or the
	 * modal loop's WMHandleEvent default arm to dispatch */
	*event = NULL;
	while (!WXEvqTakeMask(dpy, mask, XCB_NONE, event)) {
		W_HandleInputEvents(true, xcb_get_file_descriptor(dpy));
		if (xcb_connection_has_error(dpy)) {
			/* see WMNextEvent: run the dead hook before the
			 * NULL result reaches a consumer */
			WXEvqConnectionDead(dpy);
			return;
		}
	}
	if (*event)
		*event = compressPoppedEvent(*event);
}

bool WMScreenPending(WMScreen * scr)
{
	return WXEvqPending(scr->display);
}

WMEventHook *WMHookEventHandler(WMEventHook * handler)
{
	WMEventHook *oldHandler = extraEventHandler;

	extraEventHandler = handler;

	return oldHandler;
}
