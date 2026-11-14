/*
 *  dragcommon.c
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 2004-2006 Dan Pascu
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

#include "wconfig.h"

#include "WINGsP.h"

#define XDND_SOURCE_VERSION(dragInfo) dragInfo->protocolVersion
#define XDND_DEST_INFO(dragInfo) dragInfo->destInfo
#define XDND_DEST_VIEW(dragInfo) dragInfo->destInfo->destView

static bool _WindowExists;

xcb_atom_t W_OperationToAction(WMScreen * scr, WMDragOperationType operation)
{
	switch (operation) {
	case WDOperationNone:
		return XCB_NONE;

	case WDOperationCopy:
		return scr->xdndActionCopy;

	case WDOperationMove:
		return scr->xdndActionMove;

	case WDOperationLink:
		return scr->xdndActionLink;

	case WDOperationAsk:
		return scr->xdndActionAsk;

	case WDOperationPrivate:
		return scr->xdndActionPrivate;

	default:
		return XCB_NONE;
	}
}

WMDragOperationType W_ActionToOperation(WMScreen * scr, xcb_atom_t action)
{
	if (action == scr->xdndActionCopy) {
		return WDOperationCopy;

	} else if (action == scr->xdndActionMove) {
		return WDOperationMove;

	} else if (action == scr->xdndActionLink) {
		return WDOperationLink;

	} else if (action == scr->xdndActionAsk) {
		return WDOperationAsk;

	} else if (action == scr->xdndActionPrivate) {
		return WDOperationPrivate;

	} else if (action == XCB_NONE) {

		return WDOperationNone;
	} else {
		wwarning(_("unknown XDND action %lu"), (unsigned long)action);

		return WDOperationCopy;
	}
}

static void freeDragOperationItem(void *item)
{
	wfree(item);
}

WMArray *WMCreateDragOperationArray(int initialSize)
{
	return WMCreateArrayWithDestructor(initialSize, freeDragOperationItem);
}

WMDragOperationItem *WMCreateDragOperationItem(WMDragOperationType type, char *text)
{
	W_DragOperationItem *result = wmalloc(sizeof(W_DragOperationItem));

	result->type = type;
	result->text = text;

	return (WMDragOperationItem *) result;
}

WMDragOperationType WMGetDragOperationItemType(WMDragOperationItem * item)
{
	return ((W_DragOperationItem *) item)->type;
}

char *WMGetDragOperationItemText(WMDragOperationItem * item)
{
	return ((W_DragOperationItem *) item)->text;
}

static bool windowExists(xcb_connection_t * dpy, xcb_window_t win)
{
	WXWindowAttributes attr;

	_WindowExists = true;

	/* can generate BadDrawable or BadWindow, consumed by the WXProp
	 * reader (returns False), no handler bracket needed */
	if (!wXGetWindowAttributes(dpy, win, &attr, NULL))
		_WindowExists = false;

	return _WindowExists;
}

bool
W_SendDnDClientMessage(xcb_connection_t * dpy, xcb_window_t win, xcb_atom_t message,
		       unsigned long data0,
		       unsigned long data1, unsigned long data2, unsigned long data3, unsigned long data4)
{
#ifdef XDND_DEBUG
	char *msgName = wXGetAtomName(dpy, message);

	printf("sending message %s ... ", msgName);
	free(msgName);
#endif

	if (!windowExists(dpy, win)) {
		wwarning(_("target %lu for XDND message no longer exists"), (unsigned long)win);
		return false;	/* message not sent */
	}

	WXSendClientMessage(dpy, win, 0, win, message, data0, data1, data2, data3, data4);
	wXFlush(dpy);

#ifdef XDND_DEBUG
	printf("sent\n");
#endif
	return true;		/* message sent */
}

static void handleLeaveMessage(WMDraggingInfo * info)
{
	if (XDND_DEST_INFO(info) != NULL) {
		/* XDND_DEST_VIEW is never NULL (it's the xdnd aware view) */
		wassertr(XDND_DEST_VIEW(info) != NULL);
		if (XDND_DEST_VIEW(info)->dragDestinationProcs != NULL) {
			XDND_DEST_VIEW(info)->dragDestinationProcs->concludeDragOperation(XDND_DEST_VIEW(info));
		}
		W_DragDestinationInfoClear(info);
	}
}

void W_HandleDNDClientMessage(WMView * toplevel, xcb_client_message_event_t * event)
{
	WMScreen *scr = W_VIEW_SCREEN(toplevel);
	WMDraggingInfo *info = &scr->dragInfo;
	xcb_atom_t messageType = event->type;
#ifdef XDND_DEBUG
	char *msgTypeName;
#endif

	/* every XDND client message carries 32-bit data fields, refuse
	 * format-8/16 spoofs before any handler reads data.l[] */
	if (event->format != 32)
		return;

#ifdef XDND_DEBUG
	msgTypeName = wXGetAtomName(scr->display, messageType);
	if (msgTypeName != NULL)
		printf("event type = %s\n", msgTypeName);
	else
		printf("pb with event type !\n");
#endif

	/* Messages from destination to source */
	if (messageType == scr->xdndStatusAtom || messageType == scr->xdndFinishedAtom) {
		W_DragSourceStopTimer();
		W_DragSourceStateHandler(info, event);
		return;
	}

	/* Messages from source to destination */
	if (messageType == scr->xdndEnterAtom) {
		bool positionSent = (XDND_DEST_INFO(info) != NULL);

		W_DragDestinationStopTimer();
		W_DragDestinationStoreEnterMsgInfo(info, toplevel, event);

		/* Xdnd version 3 and up are not compatible with version 1 or 2 */
		if (XDND_SOURCE_VERSION(info) > 2) {

			if (positionSent) {
				/* xdndPosition previously received on xdnd aware view */
				W_DragDestinationStateHandler(info, event);
				return;
			} else {
				W_DragDestinationStartTimer(info);
				return;
			}
		} else {
			wwarning(_("unsupported version %i for XDND enter message"), XDND_SOURCE_VERSION(info));
			W_DragDestinationCancelDropOnEnter(toplevel, info);
			return;
		}
	}

	if (messageType == scr->xdndPositionAtom) {
		W_DragDestinationStopTimer();
		W_DragDestinationStorePositionMsgInfo(info, toplevel, event);
		W_DragDestinationStateHandler(info, event);
		return;
	}

	if (messageType == scr->xdndSelectionAtom || messageType == scr->xdndDropAtom) {
		W_DragDestinationStopTimer();
		W_DragDestinationStateHandler(info, event);
		return;
	}

	if (messageType == scr->xdndLeaveAtom) {
		/* conclude drop operation, and clear dragging info */
		W_DragDestinationStopTimer();
		handleLeaveMessage(info);
	}
}

/* called in destroyView (wview.c) */
void W_FreeViewXdndPart(WMView * view)
{
	WMDraggingInfo *info = &W_VIEW_SCREEN(view)->dragInfo;

	WMUnregisterViewDraggedTypes(view);

	if ((info->sourceInfo && info->sourceInfo->sourceView == view) ||
	    (info->destInfo && info->destInfo->destView == view)) {
		W_DragSourceStopTimer();
		W_DragDestinationStopTimer();
		W_DragSourceInfoClear(info);
		W_DragDestinationInfoClear(info);
	}

	if (view->dragSourceProcs)
		wfree(view->dragSourceProcs);

	if (view->dragDestinationProcs)
		wfree(view->dragDestinationProcs);

	if (view->dragImage) {
		WMReleasePixmap(view->dragImage);
		view->dragImage = NULL;
	}
}
