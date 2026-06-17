
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
void WMCreateEventHandler(WMView * view, unsigned long mask, WMEventProc * eventProc, void *clientData)
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
void WMDeleteEventHandler(WMView * view, unsigned long mask, WMEventProc * eventProc, void *clientData)
{
	W_EventHandler tmp;

	tmp.eventMask = mask;
	tmp.proc = eventProc;
	tmp.clientData = clientData;
	WMRemoveFromArrayMatching(view->eventHandlers, matchHandler, (void *)&tmp);
}

static Time getEventTime(WMScreen * screen, XEvent * event)
{
	switch (event->type) {
	case ButtonPress:
	case ButtonRelease:
		return event->xbutton.time;
	case KeyPress:
	case KeyRelease:
		return event->xkey.time;
	case MotionNotify:
		return event->xmotion.time;
	case EnterNotify:
	case LeaveNotify:
		return event->xcrossing.time;
	case PropertyNotify:
		return event->xproperty.time;
	case SelectionClear:
		return event->xselectionclear.time;
	case SelectionRequest:
		return event->xselectionrequest.time;
	case SelectionNotify:
		return event->xselection.time;
	default:
		return screen->lastEventTime;
	}
}

void W_CallDestroyHandlers(W_View * view)
{
	XEvent event;
	WMArrayIterator iter;
	W_EventHandler *hPtr;

	event.type = DestroyNotify;
	event.xdestroywindow.window = view->window;
	event.xdestroywindow.event = view->window;

	WM_ITERATE_ARRAY(view->eventHandlers, hPtr, iter) {
		if (hPtr->eventMask & StructureNotifyMask) {
			(*hPtr->proc) (&event, hPtr->clientData);
		}
	}
}

void WMSetViewNextResponder(WMView * view, WMView * responder)
{
	/* set the widget to receive keyboard events that aren't handled
	 * by this widget */

	view->nextResponder = responder;
}

void WMRelayToNextResponder(WMView * view, XEvent * event)
{
	unsigned long mask = W_MaskForEventType(event->xany.type);

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

int WMHandleEvent(XEvent * event)
{
	W_EventHandler *hPtr;
	W_View *view, *toplevel;
	unsigned long mask;
	Window window;
	WMArrayIterator iter;

	if (event->type == MappingNotify) {
		XRefreshKeyboardMapping(&event->xmapping);
		return True;
	}

	if (XFilterEvent(event, None) == True) {
		return False;
	}

	mask = W_MaskForEventType(event->xany.type);

	window = event->xany.window;

	/* diferentiate SubstructureNotify with StructureNotify */
	if (mask == StructureNotifyMask) {
		if (event->xmap.event != event->xmap.window) {
			mask = SubstructureNotifyMask;
			window = event->xmap.event;
		}
	}
	view = W_GetViewForXWindow(event->xany.display, window);

	if (!view) {
		if (extraEventHandler)
			(extraEventHandler) (event);

		return False;
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

	if (event->type == SelectionNotify || event->type == SelectionClear || event->type == SelectionRequest) {
		/* handle selection related events */
		W_HandleSelectionEvent(event);

	}

	/* if it's a key event, redispatch it to the focused control */
	if (mask & (KeyPressMask | KeyReleaseMask)) {
		W_View *focused = W_FocusedViewOfToplevel(toplevel);

		if (focused) {
			W_ReleaseView(view);
			view = focused;
			W_RetainView(view);
		}
	}

	/* compress Motion events */
	if (event->type == MotionNotify && !view->flags.dontCompressMotion) {
		XEvent ev;
		while (WXEvqPeek(event->xmotion.display, &ev)) {
			if (ev.type == MotionNotify
			    && event->xmotion.window == ev.xmotion.window
			    && event->xmotion.subwindow == ev.xmotion.subwindow) {
				/* replace events */
				WXEvqNext(event->xmotion.display, event);
			} else
				break;
		}
	}

	/* compress expose events */
	if (event->type == Expose && !view->flags.dontCompressExpose) {
		while (WXEvqTakeTyped(event->xexpose.display, Expose, view->window, event)) ;
	}

	if (view->screen->modalLoop && toplevel != view->screen->modalView && !toplevel->flags.worksWhenModal) {
		if (event->type == KeyPress || event->type == KeyRelease
		    || event->type == MotionNotify || event->type == ButtonPress
		    || event->type == ButtonRelease || event->type == FocusIn || event->type == FocusOut) {
			W_ReleaseView(view);
			W_ReleaseView(toplevel);
			return True;
		}
	}

	/* do balloon stuffs */
	if (event->type == EnterNotify)
		W_BalloonHandleEnterView(view);
	else if (event->type == LeaveNotify)
		W_BalloonHandleLeaveView(view);

	WM_ITERATE_ARRAY(view->eventHandlers, hPtr, iter) {
		if ((hPtr->eventMask & mask)) {
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
		if (event->type == ButtonPress) {
			view->screen->lastClickWindow = event->xbutton.window;
			view->screen->lastClickTime = event->xbutton.time;
		}
	}

	W_ReleaseView(view);

	if (event->type == ClientMessage) {
		/* must be handled at the end, for such message can destroy the view */
		W_HandleDNDClientMessage(toplevel, &event->xclient);
	}

	W_ReleaseView(toplevel);

	return True;
}

int WMIsDoubleClick(XEvent * event)
{
	W_View *view;

	if (event->type != ButtonPress)
		return False;

	view = W_GetViewForXWindow(event->xany.display, event->xbutton.window);

	if (!view)
		return False;

	if (view->screen->lastClickWindow != event->xbutton.window)
		return False;

	if (event->xbutton.time - view->screen->lastClickTime < WINGsConfiguration.doubleClickDelay) {
		view->screen->lastClickTime = 0;
		view->screen->lastClickWindow = None;
		view->screen->ignoreNextDoubleClick = 1;
		return True;
	} else
		return False;
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

void WMNextEvent(Display * dpy, XEvent * event)
{
	while (!WXEvqPending(dpy))
		W_HandleInputEvents(True, ConnectionNumber(dpy));

	WXEvqNext(dpy, event);
}

void WMMaskEvent(Display * dpy, long mask, XEvent * event)
{
	/* WXEvqTakeMask drains everything readable into the internal
	 * queue and returns the first match, the non-matching events
	 * stay queued in arrival order for the next WMNextEvent or the
	 * modal loop's WMHandleEvent default arm to dispatch */
	while (!WXEvqTakeMask(dpy, mask, None, event))
		W_HandleInputEvents(True, ConnectionNumber(dpy));
}

Bool WMScreenPending(WMScreen * scr)
{
	return WXEvqPending(scr->display);
}

WMEventHook *WMHookEventHandler(WMEventHook * handler)
{
	WMEventHook *oldHandler = extraEventHandler;

	extraEventHandler = handler;

	return oldHandler;
}
