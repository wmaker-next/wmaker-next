/*
 *  dragdestination.c
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 2000-2001 Alfredo K. Kojima
 *  Copyright (c) 2000-2006 Dan Pascu
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

#include "WINGsP.h"

#define XDND_SOURCE_RESPONSE_MAX_DELAY 3000

#define XDND_SOURCE_VERSION(dragInfo) dragInfo->protocolVersion
#define XDND_DEST_INFO(dragInfo) dragInfo->destInfo
#define XDND_AWARE_VIEW(dragInfo) dragInfo->destInfo->xdndAwareView
#define XDND_SOURCE_WIN(dragInfo) dragInfo->destInfo->sourceWindow
#define XDND_DEST_VIEW(dragInfo) dragInfo->destInfo->destView
#define XDND_DEST_STATE(dragInfo) dragInfo->destInfo->state
#define XDND_SOURCE_ACTION_CHANGED(dragInfo) dragInfo->destInfo->sourceActionChanged
#define XDND_SOURCE_TYPES(dragInfo) dragInfo->destInfo->sourceTypes
#define XDND_TYPE_LIST_AVAILABLE(dragInfo) dragInfo->destInfo->typeListAvailable
#define XDND_REQUIRED_TYPES(dragInfo) dragInfo->destInfo->requiredTypes
#define XDND_SOURCE_ACTION(dragInfo) dragInfo->sourceAction
#define XDND_DEST_ACTION(dragInfo) dragInfo->destinationAction
#define XDND_DROP_DATAS(dragInfo) dragInfo->destInfo->dropDatas
#define XDND_DEST_VIEW_IS_REGISTERED(dragInfo) ((dragInfo->destInfo) != NULL)\
    && ((dragInfo->destInfo->destView->dragDestinationProcs) != NULL)

static const unsigned char XDNDversion = XDND_VERSION;
static WMHandlerID dndDestinationTimer = NULL;

static void *idleState(WMView * destView, xcb_client_message_event_t * event, WMDraggingInfo * info);
static void *waitEnterState(WMView * destView, xcb_client_message_event_t * event, WMDraggingInfo * info);
static void *inspectDropDataState(WMView * destView, xcb_client_message_event_t * event, WMDraggingInfo * info);
static void *dropAllowedState(WMView * destView, xcb_client_message_event_t * event, WMDraggingInfo * info);
static void *dropNotAllowedState(WMView * destView, xcb_client_message_event_t * event, WMDraggingInfo * info);
static void *waitForDropDataState(WMView * destView, xcb_client_message_event_t * event, WMDraggingInfo * info);

/* ----- Types & datas list ----- */
static void freeSourceTypeArrayItem(void *type)
{
	free(type);
}

static WMArray *createSourceTypeArray(int initialSize)
{
	return WMCreateArrayWithDestructor(initialSize, freeSourceTypeArrayItem);
}

/* wXGetAtomName() returns NULL when the server reports a bad atom. */
static char *safeGetAtomName(xcb_connection_t *dpy, xcb_atom_t atom)
{
	return wXGetAtomName(dpy, atom);
}

static void freeDropDataArrayItem(void *data)
{
	if (data != NULL)
		WMReleaseData((WMData *) data);
}

static WMArray *createDropDataArray(WMArray * requiredTypes)
{
	if (requiredTypes != NULL)
		return WMCreateArrayWithDestructor(WMGetArrayItemCount(requiredTypes), freeDropDataArrayItem);

	else
		return WMCreateArray(0);
}

/* XdndTypeList is owned by the drag source and every entry costs a
 * synchronous XGetAtomName round-trip in safeGetAtomName(), so bound the
 * read like the action list, real applications offer well under a dozen
 * MIME types */
#define XDND_MAX_TYPES 32

static WMArray *getTypesFromTypeList(WMScreen * scr, xcb_window_t sourceWin)
{
	xcb_atom_t *typeAtomList;
	WMArray *typeList;
	size_t i, count;

	typeAtomList = WXGetAtomList(scr->display, sourceWin, scr->xdndTypeListAtom,
				     XDND_MAX_TYPES, &count);
	if (typeAtomList == NULL)
		return createSourceTypeArray(0);

	typeList = createSourceTypeArray(count);
	for (i = 0; i < count; i++) {
		char *typeName = safeGetAtomName(scr->display, typeAtomList[i]);
		if (typeName != NULL)
			WMAddToArray(typeList, typeName);
	}

	wfree(typeAtomList);

	return typeList;
}

static WMArray *getTypesFromThreeTypes(WMScreen * scr, xcb_client_message_event_t * event)
{
	WMArray *typeList;
	xcb_atom_t atom;
	int i;

	typeList = createSourceTypeArray(3);
	for (i = 2; i < 5; i++) {
		if (event->data.data32[i] != XCB_NONE) {
			char *typeName;
			atom = (xcb_atom_t) event->data.data32[i];
			typeName = safeGetAtomName(scr->display, atom);
			if (typeName != NULL)
				WMAddToArray(typeList, typeName);
		}
	}

	return typeList;
}

static void storeRequiredTypeList(WMDraggingInfo * info)
{
	WMView *destView = XDND_DEST_VIEW(info);
	WMScreen *scr = W_VIEW_SCREEN(destView);
	WMArray *requiredTypes;

	/* First, see if the stored source types are enough for dest requirements */
	requiredTypes = destView->dragDestinationProcs->requiredDataTypes(destView,
									  W_ActionToOperation(scr,
											      XDND_SOURCE_ACTION
											      (info)),
									  XDND_SOURCE_TYPES(info));

	if (requiredTypes == NULL && XDND_TYPE_LIST_AVAILABLE(info)) {
		/* None of the stored source types fits, but the whole type list
		   hasn't been retrieved yet. */
		WMFreeArray(XDND_SOURCE_TYPES(info));
		XDND_SOURCE_TYPES(info) = getTypesFromTypeList(scr, XDND_SOURCE_WIN(info));
		/* Don't retrieve the type list again */
		XDND_TYPE_LIST_AVAILABLE(info) = false;

		requiredTypes =
		    destView->dragDestinationProcs->requiredDataTypes(destView,
								      W_ActionToOperation(scr,
											  XDND_SOURCE_ACTION
											  (info)),
								      XDND_SOURCE_TYPES(info));
	}

	XDND_REQUIRED_TYPES(info) = requiredTypes;
}

static char *getNextRequestedDataType(WMDraggingInfo * info)
{
	/* get the type of the first data not yet retrieved from selection */
	int nextTypeIndex;

	if (XDND_REQUIRED_TYPES(info) != NULL) {
		nextTypeIndex = WMGetArrayItemCount(XDND_DROP_DATAS(info));
		return WMGetFromArray(XDND_REQUIRED_TYPES(info), nextTypeIndex);
		/* NULL if no more type */
	} else
		return NULL;
}

/* ----- Action list ----- */

#define XDND_MAX_ACTIONS 32

static WMArray *sourceOperationList(WMScreen * scr, xcb_window_t sourceWin)
{
	xcb_atom_t *actionList;
	size_t i, atomCount, descLen, n;
	char *descriptionList, *p, *end;
	WMArray *operationArray;
	WMDragOperationItem *operationItem;

	actionList = WXGetAtomList(scr->display, sourceWin, scr->xdndActionListAtom,
				   XDND_MAX_ACTIONS, &atomCount);
	if (!actionList) {
		wwarning("Cannot read action list");
		return NULL;
	}

	descriptionList = WXGetLatin1String(scr->display, sourceWin,
					    scr->xdndActionDescriptionAtom, &descLen);
	if (!descriptionList) {
		wwarning("Cannot read action description list");
		wfree(actionList);
		return NULL;
	}

	operationArray = WMCreateDragOperationArray(atomCount);

	/* descriptions are NUL-separated within descLen bytes, walk by byte
	 * count so a missing terminator on the last entry cannot run past
	 * the buffer */
	p = descriptionList;
	end = descriptionList + descLen;
	for (i = 0; i < atomCount && p < end; i++) {
		n = strnlen(p, (size_t)(end - p));
		operationItem = WMCreateDragOperationItem(W_ActionToOperation(scr, actionList[i]),
							  wstrndup(p, n));
		WMAddToArray(operationArray, operationItem);
		p += n + 1;
	}

	wfree(actionList);
	wfree(descriptionList);

	return operationArray;
}

/* ----- Dragging Info ----- */
static void updateSourceWindow(WMDraggingInfo * info, xcb_client_message_event_t * event)
{
	XDND_SOURCE_WIN(info) = (xcb_window_t) event->data.data32[0];
}

static WMView *findChildInView(WMView * parent, int x, int y)
{
	if (parent->childrenList == NULL)
		return parent;
	else {
		WMView *child = parent->childrenList;

		while (child != NULL
		       && (!child->flags.mapped
			   || x < WMGetViewPosition(child).x
			   || x > WMGetViewPosition(child).x + WMGetViewSize(child).width
			   || y < WMGetViewPosition(child).y
			   || y > WMGetViewPosition(child).y + WMGetViewSize(child).height))

			child = child->nextSister;

		if (child == NULL)
			return parent;
		else
			return findChildInView(child,
					       x - WMGetViewPosition(child).x, y - WMGetViewPosition(child).y);
	}
}

static WMView *findDestinationViewInToplevel(WMView * toplevel, int x, int y)
{
	WMScreen *scr = W_VIEW_SCREEN(toplevel);
	xcb_window_t toplevelWin = WMViewXID(toplevel);
	int xInToplevel, yInToplevel;
	xcb_window_t foo;

	wXTranslateCoordinates(scr->display, scr->rootWin, toplevelWin, x, y, &xInToplevel, &yInToplevel, &foo);
	return findChildInView(toplevel, xInToplevel, yInToplevel);
}

/* Clear datas only used by current destination view */
static void freeDestinationViewInfos(WMDraggingInfo * info)
{
	if (XDND_SOURCE_TYPES(info) != NULL) {
		WMFreeArray(XDND_SOURCE_TYPES(info));
		XDND_SOURCE_TYPES(info) = NULL;
	}

	if (XDND_DROP_DATAS(info) != NULL) {
		WMFreeArray(XDND_DROP_DATAS(info));
		XDND_DROP_DATAS(info) = NULL;
	}

	XDND_REQUIRED_TYPES(info) = NULL;
}

void W_DragDestinationInfoClear(WMDraggingInfo * info)
{
	W_DragDestinationStopTimer();

	if (XDND_DEST_INFO(info) != NULL) {
		freeDestinationViewInfos(info);

		wfree(XDND_DEST_INFO(info));
		XDND_DEST_INFO(info) = NULL;
	}
}

static void initDestinationDragInfo(WMDraggingInfo * info, WMView * destView)
{
	wassertr(destView != NULL);

	XDND_DEST_INFO(info) = (W_DragDestinationInfo *) wmalloc(sizeof(W_DragDestinationInfo));

	XDND_DEST_STATE(info) = idleState;
	XDND_DEST_VIEW(info) = destView;

	XDND_SOURCE_ACTION_CHANGED(info) = false;
	XDND_SOURCE_TYPES(info) = NULL;
	XDND_REQUIRED_TYPES(info) = NULL;
	XDND_DROP_DATAS(info) = NULL;
}

void W_DragDestinationStoreEnterMsgInfo(WMDraggingInfo * info, WMView * toplevel, xcb_client_message_event_t * event)
{
	WMScreen *scr = W_VIEW_SCREEN(toplevel);

	if (XDND_DEST_INFO(info) == NULL)
		initDestinationDragInfo(info, toplevel);

	XDND_SOURCE_VERSION(info) = (event->data.data32[1] >> 24);
	XDND_AWARE_VIEW(info) = toplevel;
	updateSourceWindow(info, event);

#if 0
    if (event->data.data32[1] & 1)
        /* XdndTypeList property is available */
        XDND_SOURCE_TYPES(info) = getTypesFromTypeList(scr, XDND_SOURCE_WIN(info));
    else
        XDND_SOURCE_TYPES(info) = getTypesFromThreeTypes(scr, event);
#endif
	if (XDND_SOURCE_TYPES(info))
		WMFreeArray(XDND_SOURCE_TYPES(info));
	XDND_SOURCE_TYPES(info) = getTypesFromThreeTypes(scr, event);

	/* to use if the 3 types are not enough */
	XDND_TYPE_LIST_AVAILABLE(info) = (event->data.data32[1] & 1);
}

void W_DragDestinationStorePositionMsgInfo(WMDraggingInfo * info, WMView * toplevel, xcb_client_message_event_t * event)
{
	int x = event->data.data32[2] >> 16;
	int y = event->data.data32[2] & 0xffff;
	WMView *newDestView;

	newDestView = findDestinationViewInToplevel(toplevel, x, y);

	if (XDND_DEST_INFO(info) == NULL) {
		initDestinationDragInfo(info, newDestView);
		XDND_AWARE_VIEW(info) = toplevel;
		updateSourceWindow(info, event);
	} else {
		if (newDestView != XDND_DEST_VIEW(info)) {
			updateSourceWindow(info, event);
			XDND_DEST_VIEW(info) = newDestView;
			XDND_SOURCE_ACTION_CHANGED(info) = false;

			if (XDND_DEST_STATE(info) != waitEnterState)
				XDND_DEST_STATE(info) = idleState;
		} else {
			XDND_SOURCE_ACTION_CHANGED(info) = (XDND_SOURCE_ACTION(info) != event->data.data32[4]);
		}
	}

	XDND_SOURCE_ACTION(info) = event->data.data32[4];

	/* note: source position is not stored */
}

/* ----- End of Dragging Info ----- */

/* ----- Messages ----- */

/* send a DnD message to the source window */
static void
sendDnDClientMessage(WMDraggingInfo * info, xcb_atom_t message,
		     unsigned long data1, unsigned long data2, unsigned long data3, unsigned long data4)
{
	if (!W_SendDnDClientMessage(W_VIEW_SCREEN(XDND_AWARE_VIEW(info))->display,
				    XDND_SOURCE_WIN(info),
				    message, WMViewXID(XDND_AWARE_VIEW(info)), data1, data2, data3, data4)) {
		/* drop failed */
		W_DragDestinationInfoClear(info);
	}
}

/* send a xdndStatus message to the source, with position and size
   of the destination if it has no subwidget (requesting a position message
   on every move otherwise) */
static void sendStatusMessage(WMView * destView, WMDraggingInfo * info, xcb_atom_t action)
{
	unsigned long data1;

	data1 = (action == XCB_NONE) ? 0 : 1;

	if (destView->childrenList == NULL) {
		WMScreen *scr = W_VIEW_SCREEN(destView);
		int destX, destY;
		WMSize destSize = WMGetViewSize(destView);
		xcb_window_t foo;

		wXTranslateCoordinates(scr->display, WMViewXID(destView), scr->rootWin, 0, 0, &destX, &destY, &foo);

		sendDnDClientMessage(info,
				     W_VIEW_SCREEN(destView)->xdndStatusAtom,
				     data1,
				     (destX << 16) | destY, (destSize.width << 16) | destSize.height, action);
	} else {
		/* set bit 1 to request explicitly position message on every move */
		data1 = data1 | 2;

		sendDnDClientMessage(info, W_VIEW_SCREEN(destView)->xdndStatusAtom, data1, 0, 0, action);
	}
}

static void
storeDropData(WMView * destView, xcb_atom_t selection, xcb_atom_t target, xcb_timestamp_t timestamp, void *cdata, WMData * data, bool refused)
{
	WMScreen *scr = W_VIEW_SCREEN(destView);
	WMDraggingInfo *info = &(scr->dragInfo);
	WMData *dataToStore = NULL;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) selection;
	(void) target;
	(void) timestamp;
	(void) cdata;
	(void) refused;

	if (data != NULL)
		dataToStore = WMRetainData(data);

	if (XDND_DEST_INFO(info) != NULL && XDND_DROP_DATAS(info) != NULL) {
		WMAddToArray(XDND_DROP_DATAS(info), dataToStore);
		W_SendDnDClientMessage(scr->display, WMViewXID(destView),
				       scr->xdndSelectionAtom, WMViewXID(destView), 0, 0, 0, 0);
	}
}

static bool requestDropDataInSelection(WMView * destView, const char *type)
{
	WMScreen *scr = W_VIEW_SCREEN(destView);

	if (type != NULL) {
		if (!WMRequestSelection(destView,
					scr->xdndSelectionAtom,
					WXInternAtom(scr->display, type),
					XCB_CURRENT_TIME, storeDropData, NULL)) {
			wwarning("could not request data for dropped data");
			return false;
		}

		return true;
	}

	return false;
}

static bool requestDropData(WMDraggingInfo * info)
{
	WMView *destView = XDND_DEST_VIEW(info);
	char *nextType = getNextRequestedDataType(info);

	while ((nextType != NULL)
	       && (!requestDropDataInSelection(destView, nextType))) {
		/* store NULL if request failed, and try with next type */
		WMAddToArray(XDND_DROP_DATAS(info), NULL);
		nextType = getNextRequestedDataType(info);
	}

	/* remains types to retrieve ? */
	return (nextType != NULL);
}

static void concludeDrop(WMView * destView)
{
	destView->dragDestinationProcs->concludeDragOperation(destView);
}

/* send cancel message to the source */
static void cancelDrop(WMView * destView, WMDraggingInfo * info)
{
	sendStatusMessage(destView, info, XCB_NONE);
	concludeDrop(destView);
	freeDestinationViewInfos(info);
}

/* suspend drop, when dragged icon enter an unregistered view
   or a register view that doesn't accept the drop */
static void suspendDropAuthorization(WMView * destView, WMDraggingInfo * info)
{
	sendStatusMessage(destView, info, XCB_NONE);

	/* Free datas that depend on destination behaviour */
	if (XDND_DROP_DATAS(info) != NULL) {
		WMFreeArray(XDND_DROP_DATAS(info));
		XDND_DROP_DATAS(info) = NULL;
	}

	XDND_REQUIRED_TYPES(info) = NULL;
}

/* cancel drop on Enter message, if protocol version is nok */
void W_DragDestinationCancelDropOnEnter(WMView * toplevel, WMDraggingInfo * info)
{
	if (XDND_DEST_VIEW_IS_REGISTERED(info))
		cancelDrop(XDND_DEST_VIEW(info), info);
	else
		sendStatusMessage(toplevel, info, XCB_NONE);

	W_DragDestinationInfoClear(info);
}

static void finishDrop(WMView * destView, WMDraggingInfo * info)
{
	sendDnDClientMessage(info, W_VIEW_SCREEN(destView)->xdndFinishedAtom, 0, 0, 0, 0);
	concludeDrop(destView);
	W_DragDestinationInfoClear(info);
}

static xcb_atom_t getAllowedAction(WMView * destView, WMDraggingInfo * info)
{
	WMScreen *scr = W_VIEW_SCREEN(destView);

	return W_OperationToAction(scr,
				   destView->dragDestinationProcs->allowedOperation(destView,
										    W_ActionToOperation(scr,
													XDND_SOURCE_ACTION
													(info)),
										    XDND_SOURCE_TYPES(info)));
}

static void *checkActionAllowed(WMView * destView, WMDraggingInfo * info)
{
	XDND_DEST_ACTION(info) = getAllowedAction(destView, info);

	if (XDND_DEST_ACTION(info) == XCB_NONE) {
		suspendDropAuthorization(destView, info);
		return dropNotAllowedState;
	}

	sendStatusMessage(destView, info, XDND_DEST_ACTION(info));
	return dropAllowedState;
}

static void *checkDropAllowed(WMView *destView, WMDraggingInfo *info)
{
	storeRequiredTypeList(info);

	if (destView->dragDestinationProcs->inspectDropData != NULL) {
		XDND_DROP_DATAS(info) = createDropDataArray(XDND_REQUIRED_TYPES(info));

		/* store first available data */
		if (requestDropData(info))
			return inspectDropDataState;

		/* no data retrieved, but inspect can allow it */
		if (destView->dragDestinationProcs->inspectDropData(destView, XDND_DROP_DATAS(info)))
			return checkActionAllowed(destView, info);

		suspendDropAuthorization(destView, info);
		return dropNotAllowedState;
	}

	return checkActionAllowed(destView, info);
}

static WMPoint *getDropLocationInView(WMView * view)
{
	xcb_window_t rootWin, childWin;
	int rootX, rootY;
	unsigned int mask;
	WMPoint *location;

	location = (WMPoint *) wmalloc(sizeof(WMPoint));

	wXQueryPointer(W_VIEW_SCREEN(view)->display,
		      WMViewXID(view), &rootWin, &childWin, &rootX, &rootY, &(location->x), &(location->y), &mask);

	return location;
}

static void callPerformDragOperation(WMView * destView, WMDraggingInfo * info)
{
	WMArray *operationList = NULL;
	WMScreen *scr = W_VIEW_SCREEN(destView);
	WMPoint *dropLocation;

	if (XDND_SOURCE_ACTION(info) == scr->xdndActionAsk)
		operationList = sourceOperationList(scr, XDND_SOURCE_WIN(info));

	dropLocation = getDropLocationInView(destView);
	destView->dragDestinationProcs->performDragOperation(destView,
							     XDND_DROP_DATAS(info), operationList, dropLocation);

	wfree(dropLocation);
	if (operationList != NULL)
		WMFreeArray(operationList);
}

/* ----- Destination timer ----- */
static void dragSourceResponseTimeOut(void *destView)
{
	WMView *view = (WMView *) destView;
	WMDraggingInfo *info;

	wwarning("delay for drag source response expired");
	info = &(W_VIEW_SCREEN(view)->dragInfo);
	if (XDND_DEST_VIEW_IS_REGISTERED(info))
		cancelDrop(view, info);
	else {
		sendStatusMessage(view, info, XCB_NONE);
	}

	W_DragDestinationInfoClear(info);
}

void W_DragDestinationStopTimer(void)
{
	WMDeleteTimerHandlerPtr(&dndDestinationTimer);
}

void W_DragDestinationStartTimer(WMDraggingInfo * info)
{
	W_DragDestinationStopTimer();

	if (XDND_DEST_STATE(info) != idleState)
		dndDestinationTimer = WMAddTimerHandler(XDND_SOURCE_RESPONSE_MAX_DELAY,
							dragSourceResponseTimeOut, XDND_DEST_VIEW(info));
}

/* ----- End of Destination timer ----- */

/* ----- Destination states ----- */

#ifdef XDND_DEBUG
static const char *stateName(W_DndState * state)
{
	if (state == NULL)
		return "no state defined";

	if (state == idleState)
		return "idleState";

	if (state == waitEnterState)
		return "waitEnterState";

	if (state == inspectDropDataState)
		return "inspectDropDataState";

	if (state == dropAllowedState)
		return "dropAllowedState";

	if (state == dropNotAllowedState)
		return "dropNotAllowedState";

	if (state == waitForDropDataState)
		return "waitForDropDataState";

	return "unknown state";
}
#endif

static void *idleState(WMView * destView, xcb_client_message_event_t * event, WMDraggingInfo * info)
{
	WMScreen *scr;
	xcb_atom_t sourceMsg;

	if (destView->dragDestinationProcs != NULL) {
		scr = W_VIEW_SCREEN(destView);
		sourceMsg = event->type;

		if (sourceMsg == scr->xdndPositionAtom) {
			destView->dragDestinationProcs->prepareForDragOperation(destView);

			if (XDND_SOURCE_TYPES(info) != NULL) {
				/* enter message infos are available */
				return checkDropAllowed(destView, info);
			}

			/* waiting for enter message */
			return waitEnterState;
		}
	}

	suspendDropAuthorization(destView, info);
	return idleState;
}

/*  Source position and action are stored,
 waiting for xdnd protocol version and source type */
static void *waitEnterState(WMView * destView, xcb_client_message_event_t * event, WMDraggingInfo * info)
{
	WMScreen *scr = W_VIEW_SCREEN(destView);
	xcb_atom_t sourceMsg = event->type;

	if (sourceMsg == scr->xdndEnterAtom) {
		W_DragDestinationStoreEnterMsgInfo(info, destView, event);
		return checkDropAllowed(destView, info);
	}

	return waitEnterState;
}

/* We have requested a data, and have received it */
static void *inspectDropDataState(WMView * destView, xcb_client_message_event_t * event, WMDraggingInfo * info)
{
	WMScreen *scr;
	xcb_atom_t sourceMsg;

	scr = W_VIEW_SCREEN(destView);
	sourceMsg = event->type;

	if (sourceMsg == scr->xdndSelectionAtom) {
		/* a data has been retrieved, store next available */
		if (requestDropData(info))
			return inspectDropDataState;

		/* all required (and available) datas are stored */
		if (destView->dragDestinationProcs->inspectDropData(destView, XDND_DROP_DATAS(info)))
			return checkActionAllowed(destView, info);

		suspendDropAuthorization(destView, info);
		return dropNotAllowedState;
	}

	return inspectDropDataState;
}

static void *dropNotAllowedState(WMView * destView, xcb_client_message_event_t * event, WMDraggingInfo * info)
{
	WMScreen *scr = W_VIEW_SCREEN(destView);
	xcb_atom_t sourceMsg = event->type;

	if (sourceMsg == scr->xdndDropAtom) {
		finishDrop(destView, info);
		return idleState;
	}

	if (sourceMsg == scr->xdndPositionAtom) {
		if (XDND_SOURCE_ACTION_CHANGED(info)) {
			return checkDropAllowed(destView, info);
		} else {
			sendStatusMessage(destView, info, XCB_NONE);
			return dropNotAllowedState;
		}
	}

	return dropNotAllowedState;
}

static void *dropAllowedState(WMView * destView, xcb_client_message_event_t * event, WMDraggingInfo * info)
{
	WMScreen *scr = W_VIEW_SCREEN(destView);
	xcb_atom_t sourceMsg = event->type;

	if (sourceMsg == scr->xdndDropAtom) {
		if (XDND_DROP_DATAS(info) != NULL) {
			/* drop datas were cached with inspectDropData call */
			callPerformDragOperation(destView, info);
		} else {
			XDND_DROP_DATAS(info) = createDropDataArray(XDND_REQUIRED_TYPES(info));

			/* store first available data */
			if (requestDropData(info))
				return waitForDropDataState;

			/* no data retrieved */
			callPerformDragOperation(destView, info);
		}

		finishDrop(destView, info);
		return idleState;
	}

	if (sourceMsg == scr->xdndPositionAtom) {
		if (XDND_SOURCE_ACTION_CHANGED(info)) {
			return checkDropAllowed(destView, info);
		} else {
			sendStatusMessage(destView, info, XDND_DEST_ACTION(info));
			return dropAllowedState;
		}
	}

	return dropAllowedState;
}

static void *waitForDropDataState(WMView * destView, xcb_client_message_event_t * event, WMDraggingInfo * info)
{
	WMScreen *scr = W_VIEW_SCREEN(destView);
	xcb_atom_t sourceMsg = event->type;

	if (sourceMsg == scr->xdndSelectionAtom) {
		/* store next data */
		if (requestDropData(info))
			return waitForDropDataState;

		/* all required (and available) datas are stored */
		callPerformDragOperation(destView, info);

		finishDrop(destView, info);
		return idleState;
	}

	return waitForDropDataState;
}

/* ----- End of Destination states ----- */

void W_DragDestinationStateHandler(WMDraggingInfo * info, xcb_client_message_event_t * event)
{
	WMView *destView;
	W_DndState *newState;

	wassertr(XDND_DEST_INFO(info) != NULL);
	wassertr(XDND_DEST_VIEW(info) != NULL);

	destView = XDND_DEST_VIEW(info);
	if (XDND_DEST_STATE(info) == NULL)
		XDND_DEST_STATE(info) = idleState;

#ifdef XDND_DEBUG

	printf("current dest state: %s\n", stateName(XDND_DEST_STATE(info)));
#endif

	newState = (W_DndState *) XDND_DEST_STATE(info) (destView, event, info);

#ifdef XDND_DEBUG

	printf("new dest state: %s\n", stateName(newState));
#endif

	if (XDND_DEST_INFO(info) != NULL) {
		XDND_DEST_STATE(info) = newState;
		if (XDND_DEST_STATE(info) != idleState)
			W_DragDestinationStartTimer(info);
	}
}

static void realizedObserver(void *self, WMNotification * notif)
{
	WMView *view = (WMView *) WMGetNotificationObject(notif);
	WMScreen *scr = W_VIEW_SCREEN(view);

	WXSetAtom(scr->display, W_VIEW_DRAWABLE(view), scr->xdndAwareAtom, XDNDversion);

	WMRemoveNotificationObserver(self);
}

static void W_SetXdndAwareProperty(WMScreen *scr, WMView *view)
{
	WMView *toplevel = W_TopLevelOfView(view);

	if (!toplevel->flags.xdndHintSet) {
		toplevel->flags.xdndHintSet = 1;

		if (toplevel->flags.realized) {
			WXSetAtom(scr->display, W_VIEW_DRAWABLE(toplevel), scr->xdndAwareAtom, XDNDversion);
		} else {
			WMAddNotificationObserver(realizedObserver,
						  /* just use as an id */
						  &view->dragDestinationProcs,
						  WMViewRealizedNotification, toplevel);
		}
	}
}

void WMRegisterViewForDraggedTypes(WMView * view, WMArray * acceptedTypes)
{
	xcb_atom_t *types;
	int typeCount;
	int i;

	typeCount = WMGetArrayItemCount(acceptedTypes);
	types = wmalloc(sizeof(xcb_atom_t) * (typeCount + 1));

	for (i = 0; i < typeCount; i++) {
		types[i] = WXInternAtom(W_VIEW_SCREEN(view)->display, WMGetFromArray(acceptedTypes, i));
	}
	types[i] = 0;

	view->droppableTypes = types;
	/* WMFreeArray(acceptedTypes); */

	W_SetXdndAwareProperty(W_VIEW_SCREEN(view), view);
}

void WMUnregisterViewDraggedTypes(WMView * view)
{
	if (view->droppableTypes != NULL)
		wfree(view->droppableTypes);
	view->droppableTypes = NULL;
}

/*
 requestedOperation: operation requested by the source
 sourceDataTypes:  data types (mime-types strings) supported by the source
 (never NULL, destroyed when drop ends)
 return operation allowed by destination (self)
 */
static WMDragOperationType
defAllowedOperation(WMView * self, WMDragOperationType requestedOperation, WMArray * sourceDataTypes)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;
	(void) requestedOperation;
	(void) sourceDataTypes;

	/* no operation allowed */
	return WDOperationNone;
}

/*
 requestedOperation: operation requested by the source
 sourceDataTypes:  data types (mime-types strings) supported by the source
 (never NULL, destroyed when drop ends)
 return data types (mime-types strings) required by destination (self)
 or NULL if no suitable data type is available (force
 to 2nd pass with full source type list).
 */
static WMArray *defRequiredDataTypes(WMView * self,
				     WMDragOperationType requestedOperation, WMArray * sourceDataTypes)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;
	(void) requestedOperation;
	(void) sourceDataTypes;

	/* no data type allowed (NULL even at 2nd pass) */
	return NULL;
}

/*
 Executed when the drag enters destination (self)
 */
static void defPrepareForDragOperation(WMView * self)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;
}

/*
 Checks datas to be dropped (optional).
 dropDatas: datas (WMData*) required by destination (self)
 (given in same order as returned by requiredDataTypes).
 A NULL data means it couldn't be retreived.
 Destroyed when drop ends.
 return True if data array is ok
 */
/* Bool (*inspectDropData)(WMView *self, WMArray *dropDatas); */

/*
 Process drop
 dropDatas: datas (WMData*) required by destination (self)
 (given in same order as returned by requiredDataTypes).
 A NULL data means it couldn't be retrieved.
 Destroyed when drop ends.
 operationList: if source operation is WDOperationAsk, contains
 operations (and associated texts) that can be asked
 to source. (destroyed after performDragOperation call)
 Otherwise this parameter is NULL.
 */
static void
defPerformDragOperation(WMView * self, WMArray * dropDatas, WMArray * operationList, WMPoint * dropLocation)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;
	(void) dropDatas;
	(void) operationList;
	(void) dropLocation;
}

/* Executed after drop */
static void defConcludeDragOperation(WMView * self)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;
}

void WMSetViewDragDestinationProcs(WMView * view, WMDragDestinationProcs * procs)
{
	if (view->dragDestinationProcs == NULL) {
		view->dragDestinationProcs = wmalloc(sizeof(WMDragDestinationProcs));
	}

	*view->dragDestinationProcs = *procs;

	/* fill in defaults for callbacks the caller did not supply */
	if (procs->allowedOperation == NULL) {
		view->dragDestinationProcs->allowedOperation = defAllowedOperation;
	}
	if (procs->requiredDataTypes == NULL) {
		view->dragDestinationProcs->requiredDataTypes = defRequiredDataTypes;
	}

	/* note: inspectDropData can be NULL, if data consultation is not needed
	   to give drop permission */

	if (procs->prepareForDragOperation == NULL) {
		view->dragDestinationProcs->prepareForDragOperation = defPrepareForDragOperation;
	}
	if (procs->performDragOperation == NULL) {
		view->dragDestinationProcs->performDragOperation = defPerformDragOperation;
	}
	if (procs->concludeDragOperation == NULL) {
		view->dragDestinationProcs->concludeDragOperation = defConcludeDragOperation;
	}
}
