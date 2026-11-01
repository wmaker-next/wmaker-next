/*
 *  dragsource.c
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 2000 Alfredo K. Kojima
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

#include "wconfig.h"
#include "WINGsP.h"

#ifdef USE_XSHAPE
#include <xcb/shape.h>
#endif

#define XDND_DESTINATION_RESPONSE_MAX_DELAY 10000
#define MIN_X_MOVE_OFFSET 5
#define MIN_Y_MOVE_OFFSET 5
#define MAX_SLIDEBACK_ITER 15

#define XDND_DEST_VERSION(dragInfo) dragInfo->protocolVersion
#define XDND_SOURCE_INFO(dragInfo) dragInfo->sourceInfo
#define XDND_DEST_WIN(dragInfo) dragInfo->sourceInfo->destinationWindow
#define XDND_SOURCE_ACTION(dragInfo) dragInfo->sourceAction
#define XDND_DEST_ACTION(dragInfo) dragInfo->destinationAction
#define XDND_SOURCE_VIEW(dragInfo) dragInfo->sourceInfo->sourceView
#define XDND_SOURCE_STATE(dragInfo) dragInfo->sourceInfo->state
#define XDND_SELECTION_PROCS(dragInfo) dragInfo->sourceInfo->selectionProcs
#define XDND_DRAG_ICON(dragInfo) dragInfo->sourceInfo->icon
#define XDND_MOUSE_OFFSET(dragInfo) dragInfo->sourceInfo->mouseOffset
#define XDND_DRAG_CURSOR(dragInfo) dragInfo->sourceInfo->dragCursor
#define XDND_DRAG_ICON_POS(dragInfo) dragInfo->sourceInfo->imageLocation
#define XDND_NO_POS_ZONE(dragInfo) dragInfo->sourceInfo->noPositionMessageZone
#define XDND_TIMESTAMP(dragInfo) dragInfo->timestamp
#define XDND_3_TYPES(dragInfo) dragInfo->sourceInfo->firstThreeTypes
#define XDND_SOURCE_VIEW_STORED(dragInfo) ((dragInfo)->sourceInfo != NULL \
    && (dragInfo)->sourceInfo->sourceView != NULL)

static WMHandlerID dndSourceTimer = NULL;

static void *idleState(WMView * srcView, xcb_client_message_event_t * event, WMDraggingInfo * info);
static void *dropAllowedState(WMView * srcView, xcb_client_message_event_t * event, WMDraggingInfo * info);
static void *finishDropState(WMView * srcView, xcb_client_message_event_t * event, WMDraggingInfo * info);

#ifdef XDND_DEBUG
static const char *stateName(W_DndState * state)
{
	if (state == NULL)
		return "no state defined";

	if (state == idleState)
		return "idleState";

	if (state == dropAllowedState)
		return "dropAllowedState";

	if (state == finishDropState)
		return "finishDropState";

	return "unknown state";
}
#endif

static WMScreen *sourceScreen(WMDraggingInfo * info)
{
	return W_VIEW_SCREEN(XDND_SOURCE_VIEW(info));
}

static void endDragProcess(WMDraggingInfo * info, bool deposited)
{
	WMView *view = XDND_SOURCE_VIEW(info);
	WMScreen *scr = W_VIEW_SCREEN(XDND_SOURCE_VIEW(info));

	/* free selection handler while view exists */
	WMDeleteSelectionHandler(view, scr->xdndSelectionAtom, XCB_CURRENT_TIME);
	wfree(XDND_SELECTION_PROCS(info));

	if (XDND_DRAG_CURSOR(info) != XCB_NONE) {
		xcb_free_cursor(scr->display, XDND_DRAG_CURSOR(info));
		XDND_DRAG_CURSOR(info) = XCB_NONE;
	}

	if (view->dragSourceProcs->endedDrag != NULL) {
		/* this can destroy source view (with a "move" action for example) */
		view->dragSourceProcs->endedDrag(view, &XDND_DRAG_ICON_POS(info), deposited);
	}

	/* clear remaining draggging infos */
	wfree(XDND_SOURCE_INFO(info));
	XDND_SOURCE_INFO(info) = NULL;
}

/* ----- drag cursor ----- */
static void initDragCursor(WMDraggingInfo * info)
{
	WMScreen *scr = sourceScreen(info);
	WXColor cursorFgColor, cursorBgColor;

	/* green */
	cursorFgColor.red = 0x4500;
	cursorFgColor.green = 0xb000;
	cursorFgColor.blue = 0x4500;

	/* white */
	cursorBgColor.red = 0xffff;
	cursorBgColor.green = 0xffff;
	cursorBgColor.blue = 0xffff;

	XDND_DRAG_CURSOR(info) = wXLoadCursor(scr->display, 0, "left_ptr");
	wXRecolorCursor(scr->display, XDND_DRAG_CURSOR(info), &cursorFgColor, &cursorBgColor);

	wXFlush(scr->display);
}

static void recolorCursor(WMDraggingInfo * info, bool dropIsAllowed)
{
	WMScreen *scr = sourceScreen(info);

	if (dropIsAllowed) {
		wXDefineCursor(scr->display, scr->rootWin, XDND_DRAG_CURSOR(info));
	} else {
		wXDefineCursor(scr->display, scr->rootWin, scr->defaultCursor);
	}

	wXFlush(scr->display);
}

/* ----- end of drag cursor ----- */

/* ----- selection procs ----- */
static WMData *convertSelection(WMView * view, xcb_atom_t selection, xcb_atom_t target, void *cdata, xcb_atom_t * type)
{
	WMScreen *scr;
	WMData *data;
	char *typeName;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) selection;
	(void) cdata;

	scr = W_VIEW_SCREEN(view);
	typeName = wXGetAtomName(scr->display, target);

	*type = target;

	if (typeName == NULL)
		return NULL;

	if (view->dragSourceProcs->fetchDragData != NULL) {
		data = view->dragSourceProcs->fetchDragData(view, typeName);
	} else {
		data = NULL;
	}

	if (typeName != NULL)
		free(typeName);

	return data;
}

static void selectionLost(WMView * view, xcb_atom_t selection, void *cdata)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) view;
	(void) selection;
	(void) cdata;

	wwarning(_("XDND selection lost during drag operation..."));
}

static void selectionDone(WMView * view, xcb_atom_t selection, xcb_atom_t target, void *cdata)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) view;
	(void) selection;
	(void) target;
	(void) cdata;

#ifdef XDND_DEBUG
	printf("selection done\n");
#endif
}

/* ----- end of selection procs ----- */

/* ----- visual part ----- */

static xcb_window_t makeDragIcon(WMScreen * scr, WMPixmap * pixmap)
{
	xcb_window_t window;
	WMSize size;
	unsigned long flags;
	WXSetWindowAttributes attribs;
	uint32_t vals[15];

	if (!pixmap) {
		pixmap = scr->defaultObjectIcon;
	}

	size = WMGetPixmapSize(pixmap);

	flags = XCB_CW_SAVE_UNDER | XCB_CW_BACK_PIXMAP | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_COLORMAP;
	attribs.save_under = true;
	attribs.background_pixmap = pixmap->pixmap;
	attribs.override_redirect = true;
	attribs.colormap = scr->colormap;

	window = xcb_generate_id(scr->display);
	xcb_create_window(scr->display, scr->depth, window, scr->rootWin,
			  0, 0, size.width, size.height, 0,
			  XCB_WINDOW_CLASS_INPUT_OUTPUT,
			  scr->visual->visual_id,
			  wXWindowAttrValues(&attribs, flags, vals), vals);

#ifdef USE_XSHAPE
	if (pixmap->mask) {
		xcb_shape_mask(scr->display, XCB_SHAPE_SO_SET,
			       XCB_SHAPE_SK_BOUNDING, window, 0, 0, pixmap->mask);
	}
#endif

	return window;
}

static void slideWindow(xcb_connection_t * dpy, xcb_window_t win, int srcX, int srcY, int dstX, int dstY)
{
	double x, y, dx, dy;
	int i;
	int iterations;

	iterations = WMIN(MAX_SLIDEBACK_ITER, WMAX(abs(dstX - srcX), abs(dstY - srcY)));

	x = srcX;
	y = srcY;

	dx = (double)(dstX - srcX) / iterations;
	dy = (double)(dstY - srcY) / iterations;

	for (i = 0; i <= iterations; i++) {
		xcb_configure_window(dpy, win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ x, y });
		wXFlush(dpy);

		wusleep(800);

		x += dx;
		y += dy;
	}
}

static int getInitialDragImageCoord(int viewCoord, int mouseCoord, int viewSize, int iconSize)
{
	if (iconSize >= viewSize) {
		/* center icon coord on view */
		return viewCoord - iconSize / 2;
	} else {
		/* try to center icon on mouse pos */

		if (mouseCoord - iconSize / 2 <= viewCoord)
			/* if icon was centered on mouse, it would be off view
			   thus, put icon left (resp. top) side
			   at view (resp. top) side */
			return viewCoord;

		else if (mouseCoord + iconSize / 2 >= viewCoord + viewSize)
			/* if icon was centered on mouse, it would be off view
			   thus, put icon right (resp. bottom) side
			   at view right (resp. bottom) side */
			return viewCoord + viewSize - iconSize;

		else
			return mouseCoord - iconSize / 2;
	}

}

static void initDragImagePos(WMView * view, WMDraggingInfo * info, xcb_generic_event_t * event)
{
	WMSize iconSize = WMGetPixmapSize(view->dragImage);
	WMSize viewSize = WMGetViewSize(view);
	WMPoint viewPos;
	xcb_window_t foo;

	wXTranslateCoordinates(W_VIEW_SCREEN(view)->display, WMViewXID(view), W_VIEW_SCREEN(view)->rootWin,
			      0, 0, &(viewPos.x), &(viewPos.y), &foo);

	/* set icon pos */
	XDND_DRAG_ICON_POS(info).x =
	    getInitialDragImageCoord(viewPos.x, ((xcb_motion_notify_event_t *)event)->root_x, viewSize.width, iconSize.width);

	XDND_DRAG_ICON_POS(info).y =
	    getInitialDragImageCoord(viewPos.y, ((xcb_motion_notify_event_t *)event)->root_y, viewSize.height, iconSize.height);

	/* set mouse offset relative to icon */
	XDND_MOUSE_OFFSET(info).x = ((xcb_motion_notify_event_t *)event)->root_x - XDND_DRAG_ICON_POS(info).x;
	XDND_MOUSE_OFFSET(info).y = ((xcb_motion_notify_event_t *)event)->root_y - XDND_DRAG_ICON_POS(info).y;
}

static void refreshDragImage(WMView * view, WMDraggingInfo * info)
{
	WMScreen *scr = W_VIEW_SCREEN(view);

	xcb_configure_window(scr->display, XDND_DRAG_ICON(info), XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ XDND_DRAG_ICON_POS(info).x, XDND_DRAG_ICON_POS(info).y });
}

static void startDragImage(WMView * view, WMDraggingInfo * info, xcb_generic_event_t * event)
{
	WMScreen *scr = W_VIEW_SCREEN(view);

	XDND_DRAG_ICON(info) = makeDragIcon(scr, view->dragImage);
	initDragImagePos(view, info, event);
	refreshDragImage(view, info);
	wXMapRaised(scr->display, XDND_DRAG_ICON(info));

	initDragCursor(info);
}

static void endDragImage(WMDraggingInfo * info, bool slideBack)
{
	WMView *view = XDND_SOURCE_VIEW(info);
	xcb_connection_t *dpy = W_VIEW_SCREEN(view)->display;

	if (slideBack) {
		WMPoint toLocation;
		xcb_window_t foo;

		wXTranslateCoordinates(W_VIEW_SCREEN(view)->display, WMViewXID(view), W_VIEW_SCREEN(view)->rootWin,
				      0, 0, &(toLocation.x), &(toLocation.y), &foo);

		slideWindow(dpy, XDND_DRAG_ICON(info),
			    XDND_DRAG_ICON_POS(info).x, XDND_DRAG_ICON_POS(info).y, toLocation.x, toLocation.y);
	}

	xcb_destroy_window(dpy, XDND_DRAG_ICON(info));
}

/* ----- end of visual part ----- */

/* ----- messages ----- */

/* send a DnD message to the destination window */
static bool
sendDnDClientMessage(WMDraggingInfo * info, xcb_atom_t message,
		     unsigned long data1, unsigned long data2, unsigned long data3, unsigned long data4)
{
	xcb_connection_t *dpy = sourceScreen(info)->display;
	xcb_window_t srcWin = WMViewXID(XDND_SOURCE_VIEW(info));
	xcb_window_t destWin = XDND_DEST_WIN(info);

	if (!W_SendDnDClientMessage(dpy, destWin, message, srcWin, data1, data2, data3, data4)) {
		/* drop failed */
		recolorCursor(info, false);
		endDragImage(info, true);
		endDragProcess(info, false);
		return false;
	}

	return true;
}

static bool sendEnterMessage(WMDraggingInfo * info)
{
	WMScreen *scr = sourceScreen(info);
	unsigned long version;

	if (XDND_DEST_VERSION(info) > 2) {
		if (XDND_DEST_VERSION(info) < XDND_VERSION)
			version = XDND_DEST_VERSION(info);
		else
			version = XDND_VERSION;
	} else {
		version = 3;
	}

	return sendDnDClientMessage(info, scr->xdndEnterAtom, (version << 24) | 1,	/* 1: support of type list */
				    XDND_3_TYPES(info)[0], XDND_3_TYPES(info)[1], XDND_3_TYPES(info)[2]);
}

static bool sendPositionMessage(WMDraggingInfo * info, WMPoint * mousePos)
{
	WMScreen *scr = sourceScreen(info);
	WMRect *noPosZone = &(XDND_NO_POS_ZONE(info));

	if (noPosZone->size.width != 0 || noPosZone->size.height != 0) {
		if (mousePos->x < noPosZone->pos.x || mousePos->x > (noPosZone->pos.x + noPosZone->size.width)
		    || mousePos->y < noPosZone->pos.y || mousePos->y > (noPosZone->pos.y + noPosZone->size.height)) {
			/* send position if out of zone defined by destination */
			return sendDnDClientMessage(info, scr->xdndPositionAtom,
						    0,
						    mousePos->x << 16 | mousePos->y,
						    XDND_TIMESTAMP(info), XDND_SOURCE_ACTION(info));
		}

		/* Nothing to send, always succeed */
		return true;

	}

	/* send position on each move */
	return sendDnDClientMessage(info, scr->xdndPositionAtom,
				    0,
				    mousePos->x << 16 | mousePos->y,
				    XDND_TIMESTAMP(info), XDND_SOURCE_ACTION(info));
}

static bool sendLeaveMessage(WMDraggingInfo * info)
{
	WMScreen *scr = sourceScreen(info);

	return sendDnDClientMessage(info, scr->xdndLeaveAtom, 0, 0, 0, 0);
}

static bool sendDropMessage(WMDraggingInfo * info)
{
	WMScreen *scr = sourceScreen(info);

	return sendDnDClientMessage(info, scr->xdndDropAtom, 0, XDND_TIMESTAMP(info), 0, 0);
}

/* ----- end of messages ----- */

static xcb_atom_t *getTypeAtomList(WMScreen * scr, WMView * view, int *count)
{
	WMArray *types;
	xcb_atom_t *typeAtoms;
	int i;

	types = view->dragSourceProcs->dropDataTypes(view);

	if (types != NULL) {
		*count = WMGetArrayItemCount(types);
		if (*count > 0) {
			typeAtoms = wmalloc((*count) * sizeof(xcb_atom_t));
			for (i = 0; i < *count; i++) {
				typeAtoms[i] = WXInternAtom(scr->display, WMGetFromArray(types, i));
			}

			/* WMFreeArray(types); */
			return typeAtoms;
		}

		/* WMFreeArray(types); */
	}

	*count = 1;
	typeAtoms = wmalloc(sizeof(xcb_atom_t));
	*typeAtoms = XCB_NONE;

	return typeAtoms;
}

static void registerDropTypes(WMScreen * scr, WMView * view, WMDraggingInfo * info)
{
	xcb_atom_t *typeList;
	int i, count;

	typeList = getTypeAtomList(scr, view, &count);

	/* store the first 3 types */
	for (i = 0; i < 3 && i < count; i++)
		XDND_3_TYPES(info)[i] = typeList[i];

	for (; i < 3; i++)
		XDND_3_TYPES(info)[i] = XCB_NONE;

	/* store the entire type list */
	WXSetAtomN(scr->display, WMViewXID(view), scr->xdndTypeListAtom, typeList, count);
}

static void registerOperationList(WMScreen * scr, WMView * view, WMArray * operationArray)
{
	xcb_atom_t *actionList;
	WMDragOperationType operation;
	int count = WMGetArrayItemCount(operationArray);
	int i;

	actionList = wmalloc(sizeof(xcb_atom_t) * count);

	for (i = 0; i < count; i++) {
		operation = WMGetDragOperationItemType(WMGetFromArray(operationArray, i));
		actionList[i] = W_OperationToAction(scr, operation);
	}

	WXSetAtomN(scr->display, WMViewXID(view), scr->xdndActionListAtom, actionList, count);
}

static void registerDescriptionList(WMScreen * scr, WMView * view, WMArray * operationArray)
{
	char *text, *textListItem, *textList;
	int count = WMGetArrayItemCount(operationArray);
	int i;
	int size = 0;

	/* size of XCB_ATOM_STRING info */
	for (i = 0; i < count; i++) {
		size += strlen(WMGetDragOperationItemText(WMGetFromArray(operationArray, i))) + 1 /* NULL */;
	}

	/* create text list */
	textList = wmalloc(size);
	textListItem = textList;

	for (i = 0; i < count; i++) {
		text = WMGetDragOperationItemText(WMGetFromArray(operationArray, i));
		wstrlcpy(textListItem, text, size);

		/* to next text offset */
		textListItem = &(textListItem[strlen(textListItem) + 1]);
	}

	WXSetLatin1String(scr->display, WMViewXID(view), scr->xdndActionDescriptionAtom, textList, size);
}

/* called if wanted operation is WDOperationAsk */
static void registerSupportedOperations(WMView * view)
{
	WMScreen *scr = W_VIEW_SCREEN(view);
	WMArray *operationArray;

	operationArray = view->dragSourceProcs->askedOperations(view);

	registerOperationList(scr, view, operationArray);
	registerDescriptionList(scr, view, operationArray);

	/* WMFreeArray(operationArray); */
}

static void initSourceDragInfo(WMView * sourceView, WMDraggingInfo * info)
{
	WMRect emptyZone;

	XDND_SOURCE_INFO(info) = (W_DragSourceInfo *) wmalloc(sizeof(W_DragSourceInfo));

	XDND_SOURCE_VIEW(info) = sourceView;
	XDND_DEST_WIN(info) = XCB_NONE;
	XDND_DRAG_ICON(info) = XCB_NONE;

	XDND_SOURCE_ACTION(info) = W_OperationToAction(W_VIEW_SCREEN(sourceView),
						       sourceView->dragSourceProcs->
						       wantedDropOperation(sourceView));

	XDND_DEST_ACTION(info) = XCB_NONE;

	XDND_SOURCE_STATE(info) = idleState;

	emptyZone.pos = wmkpoint(0, 0);
	emptyZone.size = wmksize(0, 0);
	XDND_NO_POS_ZONE(info) = emptyZone;
}

/*
 Returned array is destroyed after dropDataTypes call
 */
static WMArray *defDropDataTypes(WMView * self)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;

	return NULL;
}

static WMDragOperationType defWantedDropOperation(WMView * self)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;

	return WDOperationNone;
}

/*
 Must be defined if wantedDropOperation return WDOperationAsk
 (useless otherwise).
 Return a WMDragOperationItem array (destroyed after call).
 A WMDragOperationItem links a label to an operation.
 static WMArray*
 defAskedOperations(WMView *self); */

static bool defAcceptDropOperation(WMView * self, WMDragOperationType allowedOperation)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;
	(void) allowedOperation;

	return false;
}

static void defBeganDrag(WMView * self, WMPoint * point)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;
	(void) point;
}

static void defEndedDrag(WMView * self, WMPoint * point, bool deposited)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;
	(void) point;
	(void) deposited;
}

/*
 Returned data is not destroyed
 */
static WMData *defFetchDragData(WMView * self, char *type)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;
	(void) type;

	return NULL;
}

void WMSetViewDragSourceProcs(WMView * view, WMDragSourceProcs * procs)
{
	if (view->dragSourceProcs)
		wfree(view->dragSourceProcs);
	view->dragSourceProcs = wmalloc(sizeof(WMDragSourceProcs));

	*view->dragSourceProcs = *procs;

	if (procs->dropDataTypes == NULL)
		view->dragSourceProcs->dropDataTypes = defDropDataTypes;

	if (procs->wantedDropOperation == NULL)
		view->dragSourceProcs->wantedDropOperation = defWantedDropOperation;

	/*
	   Note: askedOperations can be NULL, if wantedDropOperation never returns
	   WDOperationAsk.
	 */

	if (procs->acceptDropOperation == NULL)
		view->dragSourceProcs->acceptDropOperation = defAcceptDropOperation;

	if (procs->beganDrag == NULL)
		view->dragSourceProcs->beganDrag = defBeganDrag;

	if (procs->endedDrag == NULL)
		view->dragSourceProcs->endedDrag = defEndedDrag;

	if (procs->fetchDragData == NULL)
		view->dragSourceProcs->fetchDragData = defFetchDragData;
}

static bool isXdndAware(WMScreen * scr, xcb_window_t win)
{
	xcb_atom_t winXdndVersion;

	if (win == XCB_NONE)
		return false;

	return WXGetAtom(scr->display, win, scr->xdndAwareAtom, &winXdndVersion);
}

static xcb_window_t *windowChildren(xcb_connection_t * dpy, xcb_window_t win, unsigned int *nchildren)
{
	xcb_window_t *children;
	xcb_window_t foo, bar;

	if (!wXQueryTree(dpy, win, &foo, &bar, &children, nchildren)) {
		*nchildren = 0;
		return NULL;
	} else
		return children;
}

static xcb_window_t lookForAwareWindow(WMScreen * scr, WMPoint * mousePos, xcb_window_t win)
{
	int tmpx, tmpy;
	xcb_window_t child;

	/* Since xdnd v3, only the toplevel window should be aware */
	if (isXdndAware(scr, win))
		return win;

	/* inspect child under pointer */
	if (wXTranslateCoordinates(scr->display, scr->rootWin, win, mousePos->x, mousePos->y, &tmpx, &tmpy, &child)) {
		if (child == XCB_NONE)
			return XCB_NONE;
		else
			return lookForAwareWindow(scr, mousePos, child);
	}

	return XCB_NONE;
}

static xcb_window_t findDestination(WMDraggingInfo * info, WMPoint * mousePos)
{
	WMScreen *scr = sourceScreen(info);
	unsigned nchildren;
	xcb_window_t *children = windowChildren(scr->display, scr->rootWin, &nchildren);
	xcb_window_t dest;
	int i;
	WXWindowAttributes attr;

	if (isXdndAware(scr, scr->rootWin)) {
		wfree(children);
		return scr->rootWin;
	}

	/* exclude drag icon (and upper) from search */
	for (i = nchildren - 1; i >= 0; i--) {
		if (children[i] == XDND_DRAG_ICON(info)) {
			i--;
			break;
		}
	}

	if (i < 0) {
		/* root window has no child under drag icon, and is not xdnd aware. */
		wfree(children);
		return XCB_NONE;
	}

	/* inspecting children, from upper to lower */
	for (; i >= 0; i--) {
		if (wXGetWindowAttributes(scr->display, children[i], &attr, NULL)
		    && attr.map_state == XCB_MAP_STATE_VIEWABLE
		    && mousePos->x >= attr.x
		    && mousePos->y >= attr.y
		    && mousePos->x < attr.x + attr.width && mousePos->y < attr.y + attr.height) {
			dest = lookForAwareWindow(scr, mousePos, children[i]);
			wfree(children);
			return dest;
		}
	}

	/* No child window under drag pointer */
	wfree(children);
	return XCB_NONE;
}

static void storeDestinationProtocolVersion(WMDraggingInfo * info)
{
	xcb_atom_t winXdndVersion;
	WMScreen *scr = W_VIEW_SCREEN(XDND_SOURCE_VIEW(info));

	wassertr(XDND_DEST_WIN(info) != XCB_NONE);

	if (WXGetAtom(scr->display, XDND_DEST_WIN(info), scr->xdndAwareAtom, &winXdndVersion)) {
		XDND_DEST_VERSION(info) = (unsigned char)winXdndVersion;
	} else {
		XDND_DEST_VERSION(info) = 0;
		wwarning(_("could not get XDND version for target of drop"));
	}
}

static void initMotionProcess(WMView * view, WMDraggingInfo * info, xcb_generic_event_t * event, WMPoint * startLocation)
{
	WMScreen *scr = W_VIEW_SCREEN(view);

	/* take ownership of XdndSelection */
	XDND_SELECTION_PROCS(info) = (WMSelectionProcs *) wmalloc(sizeof(WMSelectionProcs));
	XDND_SELECTION_PROCS(info)->convertSelection = convertSelection;
	XDND_SELECTION_PROCS(info)->selectionLost = selectionLost;
	XDND_SELECTION_PROCS(info)->selectionDone = selectionDone;
	XDND_TIMESTAMP(info) = ((xcb_motion_notify_event_t *)event)->time;

	if (!WMCreateSelectionHandler(view, scr->xdndSelectionAtom, XCB_CURRENT_TIME, XDND_SELECTION_PROCS(info), NULL)) {
		wwarning(_("could not get ownership of XDND selection"));
		return;
	}

	registerDropTypes(scr, view, info);

	if (XDND_SOURCE_ACTION(info) == W_VIEW_SCREEN(view)->xdndActionAsk)
		registerSupportedOperations(view);

	if (view->dragSourceProcs->beganDrag != NULL) {
		view->dragSourceProcs->beganDrag(view, startLocation);
	}
}

static void processMotion(WMDraggingInfo * info, WMPoint * mousePos)
{
	xcb_window_t newDestination;

	if (!XDND_SOURCE_VIEW_STORED(info))
		return;

	newDestination = findDestination(info, mousePos);

	W_DragSourceStopTimer();

	if (newDestination != XDND_DEST_WIN(info)) {
		recolorCursor(info, false);

		if (XDND_DEST_WIN(info) != XCB_NONE) {
			/* leaving a xdnd window */
			sendLeaveMessage(info);
		}

		XDND_DEST_WIN(info) = newDestination;
		XDND_DEST_ACTION(info) = XCB_NONE;
		XDND_NO_POS_ZONE(info).size.width = 0;
		XDND_NO_POS_ZONE(info).size.height = 0;

		if (newDestination != XCB_NONE) {
			/* entering a xdnd window */
			XDND_SOURCE_STATE(info) = idleState;
			storeDestinationProtocolVersion(info);

			if (!sendEnterMessage(info)) {
				XDND_DEST_WIN(info) = XCB_NONE;
				return;
			}

			W_DragSourceStartTimer(info);
		} else {
			XDND_SOURCE_STATE(info) = NULL;
		}
	} else {
		if (XDND_DEST_WIN(info) != XCB_NONE) {
			if (!sendPositionMessage(info, mousePos)) {
				XDND_DEST_WIN(info) = XCB_NONE;
				return;
			}

			W_DragSourceStartTimer(info);
		}
	}
}

static bool processButtonRelease(WMDraggingInfo * info)
{
	if (XDND_SOURCE_STATE(info) == dropAllowedState) {
		/* begin drop */
		W_DragSourceStopTimer();

		if (!sendDropMessage(info))
			return false;

		W_DragSourceStartTimer(info);
		return true;
	} else {
		if (XDND_DEST_WIN(info) != XCB_NONE)
			sendLeaveMessage(info);

		W_DragSourceStopTimer();
		return false;
	}
}

bool WMIsDraggingFromView(WMView * view)
{
	WMDraggingInfo *info = &W_VIEW_SCREEN(view)->dragInfo;

	return (XDND_SOURCE_INFO(info) != NULL && XDND_SOURCE_STATE(info) != finishDropState);
	/* return W_VIEW_SCREEN(view)->dragInfo.sourceInfo != NULL; */
}

void WMDragImageFromView(WMView * view, xcb_generic_event_t * event)
{
	WMDraggingInfo *info = &W_VIEW_SCREEN(view)->dragInfo;
	WMPoint mouseLocation;

	switch ((event->response_type & 0x7f)) {
	case XCB_BUTTON_PRESS:
		if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_1) {
			xcb_generic_event_t *nextEvent = NULL;

			/* XPeekEvent blocked until the next event was readable
			 * and left it at the head of the queue, do the same on
			 * the WINGs queue so a slow drag start (button down,
			 * no motion buffered yet) is still recognised */
			WMNextEvent(W_XEvqConnection(), &nextEvent);
			if (!nextEvent)
				/* dead connection: no drag can start, the
				 * modal loop's next fetch runs the dead
				 * hook */
				break;
			WXEvqPutBack(nextEvent);

			/* Initialize only if a drag really begins (avoid clicks) */
			if ((nextEvent->response_type & 0x7f) == XCB_MOTION_NOTIFY) {
				initSourceDragInfo(view, info);
			}
		}

		break;

	case XCB_BUTTON_RELEASE:
		if (WMIsDraggingFromView(view)) {
			bool dropBegan = processButtonRelease(info);

			recolorCursor(info, false);
			if (dropBegan) {
				endDragImage(info, false);
				XDND_SOURCE_STATE(info) = finishDropState;
			} else {
				/* drop failed */
				endDragImage(info, true);
				endDragProcess(info, false);
			}
		}
		break;

	case XCB_MOTION_NOTIFY:
		if (WMIsDraggingFromView(view)) {
			mouseLocation = wmkpoint(((xcb_motion_notify_event_t *)event)->root_x, ((xcb_motion_notify_event_t *)event)->root_y);

			if (abs(XDND_DRAG_ICON_POS(info).x - mouseLocation.x) >=
			    MIN_X_MOVE_OFFSET
			    || abs(XDND_DRAG_ICON_POS(info).y - mouseLocation.y) >= MIN_Y_MOVE_OFFSET) {
				if (XDND_DRAG_ICON(info) == XCB_NONE) {
					initMotionProcess(view, info, event, &mouseLocation);
					startDragImage(view, info, event);
				} else {
					XDND_DRAG_ICON_POS(info).x = mouseLocation.x - XDND_MOUSE_OFFSET(info).x;
					XDND_DRAG_ICON_POS(info).y = mouseLocation.y - XDND_MOUSE_OFFSET(info).y;

					refreshDragImage(view, info);
					processMotion(info, &mouseLocation);
				}
			}
		}
		break;
	}
}

/* Minimal mouse events handler: no right or double-click detection,
 only drag is supported */
static void dragImageHandler(xcb_generic_event_t * event, void *cdata)
{
	WMView *view = (WMView *) cdata;

	WMDragImageFromView(view, event);
}

/* ----- source states ----- */

#ifdef XDND_DEBUG
static void traceStatusMsg(xcb_connection_t * dpy, xcb_client_message_event_t * statusEvent)
{
	printf("Xdnd status message:\n");

	if (statusEvent->data.data32[1] & 0x2UL)
		printf("\tsend position on every move\n");
	else {
		int x, y, w, h;
		x = statusEvent->data.data32[2] >> 16;
		y = statusEvent->data.data32[2] & 0xFFFFL;
		w = statusEvent->data.data32[3] >> 16;
		h = statusEvent->data.data32[3] & 0xFFFFL;

		printf("\tsend position out of ((%d,%d) , (%d,%d))\n", x, y, x + w, y + h);
	}

	if (statusEvent->data.data32[1] & 0x1L)
		{
			char *actname = wXGetAtomName(dpy, statusEvent->data.data32[4]);
			printf("\tallowed action: %s\n", actname ? actname : "?");
			free(actname);
		}
	else
		printf("\tno action allowed\n");
}
#endif

static void storeDropAction(WMDraggingInfo * info, xcb_atom_t destAction)
{
	WMView *sourceView = XDND_SOURCE_VIEW(info);
	WMScreen *scr = W_VIEW_SCREEN(sourceView);

	if (sourceView->dragSourceProcs->acceptDropOperation != NULL) {
		if (sourceView->dragSourceProcs->acceptDropOperation(sourceView,
								     W_ActionToOperation(scr, destAction)))
			XDND_DEST_ACTION(info) = destAction;
		else
			XDND_DEST_ACTION(info) = XCB_NONE;
	} else {
		XDND_DEST_ACTION(info) = destAction;
	}
}

static void storeStatusMessageInfos(WMDraggingInfo * info, xcb_client_message_event_t * statusEvent)
{
	WMRect *noPosZone = &(XDND_NO_POS_ZONE(info));

#ifdef XDND_DEBUG

	traceStatusMsg(sourceScreen(info)->display, statusEvent);
#endif

	if (statusEvent->data.data32[1] & 0x2UL) {
		/* bit 1 set: destination wants position messages on every move */
		noPosZone->size.width = 0;
		noPosZone->size.height = 0;
	} else {
		/* don't send another position message while in given rectangle */
		noPosZone->pos.x = statusEvent->data.data32[2] >> 16;
		noPosZone->pos.y = statusEvent->data.data32[2] & 0xFFFFL;
		noPosZone->size.width = statusEvent->data.data32[3] >> 16;
		noPosZone->size.height = statusEvent->data.data32[3] & 0xFFFFL;
	}

	if ((statusEvent->data.data32[1] & 0x1L) || statusEvent->data.data32[4] != XCB_NONE) {
		/* destination accept drop */
		storeDropAction(info, statusEvent->data.data32[4]);
	} else {
		XDND_DEST_ACTION(info) = XCB_NONE;
	}
}

static void *idleState(WMView * view, xcb_client_message_event_t * event, WMDraggingInfo * info)
{
	WMScreen *scr;
	xcb_atom_t destMsg = event->type;

	scr = W_VIEW_SCREEN(view);

	if (destMsg == scr->xdndStatusAtom) {
		storeStatusMessageInfos(info, event);

		if (XDND_DEST_ACTION(info) != XCB_NONE) {
			recolorCursor(info, true);
			W_DragSourceStartTimer(info);
			return dropAllowedState;
		} else {
			/* drop denied */
			recolorCursor(info, false);
			return idleState;
		}
	}

	if (destMsg == scr->xdndFinishedAtom) {
		wwarning("received xdndFinishedAtom before drop began");
	}

	W_DragSourceStartTimer(info);
	return idleState;
}

static void *dropAllowedState(WMView * view, xcb_client_message_event_t * event, WMDraggingInfo * info)
{
	WMScreen *scr = W_VIEW_SCREEN(view);
	xcb_atom_t destMsg = event->type;

	if (destMsg == scr->xdndStatusAtom) {
		storeStatusMessageInfos(info, event);

		if (XDND_DEST_ACTION(info) == XCB_NONE) {
			/* drop denied */
			recolorCursor(info, false);
			return idleState;
		}
	}

	W_DragSourceStartTimer(info);
	return dropAllowedState;
}

static void *finishDropState(WMView * view, xcb_client_message_event_t * event, WMDraggingInfo * info)
{
	WMScreen *scr = W_VIEW_SCREEN(view);
	xcb_atom_t destMsg = event->type;

	if (destMsg == scr->xdndFinishedAtom) {
		endDragProcess(info, true);
		return NULL;
	}

	W_DragSourceStartTimer(info);
	return finishDropState;
}

/* ----- end of source states ----- */

/* ----- source timer ----- */
static void dragSourceResponseTimeOut(void *source)
{
	WMView *view = (WMView *) source;
	WMDraggingInfo *info = &(W_VIEW_SCREEN(view)->dragInfo);

	wwarning(_("delay for drag destination response expired"));
	sendLeaveMessage(info);

	recolorCursor(info, false);
	if (XDND_SOURCE_STATE(info) == finishDropState) {
		/* drop failed */
		endDragImage(info, true);
		endDragProcess(info, false);
	} else {
		XDND_SOURCE_STATE(info) = idleState;
	}
}

void W_DragSourceStopTimer(void)
{
	WMDeleteTimerHandlerPtr(&dndSourceTimer);
}

void W_DragSourceStartTimer(WMDraggingInfo * info)
{
	W_DragSourceStopTimer();

	dndSourceTimer = WMAddTimerHandler(XDND_DESTINATION_RESPONSE_MAX_DELAY,
					   dragSourceResponseTimeOut, XDND_SOURCE_VIEW(info));
}

/* ----- End of Destination timer ----- */

void W_DragSourceStateHandler(WMDraggingInfo * info, xcb_client_message_event_t * event)
{
	WMView *view;
	W_DndState *newState;

	if (XDND_SOURCE_VIEW_STORED(info)) {
		if (XDND_SOURCE_STATE(info) != NULL) {
			view = XDND_SOURCE_VIEW(info);
#ifdef XDND_DEBUG

			printf("current source state: %s\n", stateName(XDND_SOURCE_STATE(info)));
#endif

			newState = (W_DndState *) XDND_SOURCE_STATE(info) (view, event, info);

#ifdef XDND_DEBUG

			printf("new source state: %s\n", stateName(newState));
#endif

			if (newState != NULL)
				XDND_SOURCE_STATE(info) = newState;
			/* else drop finished, and info has been flushed */
		}

	} else {
		wwarning("received DnD message without having a target");
	}
}

void WMSetViewDragImage(WMView * view, WMPixmap * dragImage)
{
	if (view->dragImage != NULL)
		WMReleasePixmap(view->dragImage);

	view->dragImage = WMRetainPixmap(dragImage);
}

void WMReleaseViewDragImage(WMView * view)
{
	if (view->dragImage != NULL) {
		WMReleasePixmap(view->dragImage);
		view->dragImage = NULL;
	}
}

/* Create a drag handler, associating drag event masks with dragEventProc */
void WMCreateDragHandler(WMView * view, WMEventProc * dragEventProc, void *clientData)
{
	WMCreateEventHandler(view,
			     XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_1_MOTION, dragEventProc, clientData);
}

void WMDeleteDragHandler(WMView * view, WMEventProc * dragEventProc, void *clientData)
{
	WMDeleteEventHandler(view,
			     XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_1_MOTION, dragEventProc, clientData);
}

/* set default drag handler for view */
void WMSetViewDraggable(WMView * view, WMDragSourceProcs * dragSourceProcs, WMPixmap * dragImage)
{
	wassertr(dragImage != NULL);
	view->dragImage = WMRetainPixmap(dragImage);

	WMSetViewDragSourceProcs(view, dragSourceProcs);

	WMCreateDragHandler(view, dragImageHandler, view);
}

void WMUnsetViewDraggable(WMView * view)
{
	if (view->dragSourceProcs) {
		wfree(view->dragSourceProcs);
		view->dragSourceProcs = NULL;
	}

	WMReleaseViewDragImage(view);

	WMDeleteDragHandler(view, dragImageHandler, view);
}
