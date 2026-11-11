/*
 *  wview.c
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 1998 scottc
 *  Copyright (c) 1999-2006 Dan Pascu
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

#include "WINGsP.h"


/* the notifications about views */

const char *WMViewSizeDidChangeNotification = "WMViewSizeDidChangeNotification";
const char *WMViewFocusDidChangeNotification = "WMViewFocusDidChangeNotification";
const char *WMViewRealizedNotification = "WMViewRealizedNotification";

/* logical -> device pixel: view->pos and view->size are kept in
 * logical units and only the X window geometry is scaled here, so
 * every widget layout call site stays unchanged. */
#define VDEV(scr, v) ((int)((v) * (scr)->uiScale + 0.5))

#define EVENT_MASK  \
    XCB_EVENT_MASK_KEY_PRESS|XCB_EVENT_MASK_KEY_RELEASE|XCB_EVENT_MASK_BUTTON_PRESS|XCB_EVENT_MASK_BUTTON_RELEASE| \
    XCB_EVENT_MASK_ENTER_WINDOW|XCB_EVENT_MASK_LEAVE_WINDOW|XCB_EVENT_MASK_POINTER_MOTION|XCB_EVENT_MASK_EXPOSURE| \
    XCB_EVENT_MASK_VISIBILITY_CHANGE|XCB_EVENT_MASK_FOCUS_CHANGE|XCB_EVENT_MASK_PROPERTY_CHANGE|\
    XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY|XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT

static const WXSetWindowAttributes defAtts = {
	XCB_NONE,			/* background_pixmap */
	0,			/* background_pixel */
	XCB_COPY_FROM_PARENT,		/* border_pixmap */
	0,			/* border_pixel */
	XCB_GRAVITY_BIT_FORGET,		/* bit_gravity */
	XCB_GRAVITY_BIT_FORGET,		/* win_gravity */
	XCB_BACKING_STORE_NOT_USEFUL,		/* backing_store */
	(unsigned)~0,		/* backing_planes */
	0,			/* backing_pixel */
	false,			/* save_under */
	EVENT_MASK,		/* event_mask */
	0,			/* do_not_propagate_mask */
	false,			/* override_redirect */
	XCB_NONE,			/* colormap */
	XCB_NONE,			/* cursor */
	NULL			/* visual */
};

static WMHashTable *ViewTable = NULL;	/* window -> view map */

W_View *W_GetViewForXWindow(xcb_window_t window)
{
	W_View *view;

	if (ViewTable == NULL)
		return NULL;

	view = WMHashGet(ViewTable, (void *)(uintptr_t) window);
	if (view != NULL) {
		return view;
	}
	return NULL;
}

static void unparentView(W_View * view)
{
	/* remove from parent's children list */
	if (view->parent != NULL) {
		W_View *ptr;

		ptr = view->parent->childrenList;
		if (ptr == view) {
			view->parent->childrenList = view->nextSister;
		} else {
			while (ptr != NULL) {
				if (ptr->nextSister == view) {
					ptr->nextSister = view->nextSister;
					break;
				}
				ptr = ptr->nextSister;
			}
		}
	}
	view->parent = NULL;
}

static void adoptChildView(W_View * view, W_View * child)
{
	child->nextSister = NULL;

	/* add to end of children list of parent */
	if (view->childrenList == NULL) {
		view->childrenList = child;
	} else {
		W_View *ptr;

		ptr = view->childrenList;
		while (ptr->nextSister != NULL)
			ptr = ptr->nextSister;
		ptr->nextSister = child;
	}
	child->parent = view;
}

static W_View *createView(W_Screen * screen, W_View * parent)
{
	W_View *view;

	if (ViewTable == NULL)
		ViewTable = WMCreateHashTable(WMIntHashCallbacks);

	view = wmalloc(sizeof(W_View));
	view->screen = screen;

	if (parent != NULL) {
		/* attributes are not valid for root window */
		view->attribFlags = XCB_CW_EVENT_MASK | XCB_CW_BIT_GRAVITY;
		view->attribs = defAtts;

                view->attribFlags |= XCB_CW_BACK_PIXEL | XCB_CW_COLORMAP | XCB_CW_BORDER_PIXEL | XCB_CW_BACK_PIXMAP;
                view->attribs.background_pixmap = XCB_NONE;
		view->attribs.background_pixel = W_PIXEL(screen->gray);
		view->attribs.border_pixel = W_PIXEL(screen->black);
		view->attribs.colormap = screen->colormap;

		view->backColor = WMRetainColor(screen->gray);

		adoptChildView(parent, view);
	}

	view->refCount = 1;

	view->eventHandlers = WMCreateArrayWithDestructor(4, wfree);

	return view;
}

W_View *W_CreateView(W_View * parent)
{
	return createView(parent->screen, parent);
}

W_View *W_CreateRootView(W_Screen * screen)
{
	W_View *view;

	view = createView(screen, NULL);

	view->window = screen->rootWin;

	view->flags.realized = 1;
	view->flags.mapped = 1;
	view->flags.root = 1;

	view->size.width = wXDisplayWidth(screen->display, screen->screen);
	view->size.height = wXDisplayHeight(screen->display, screen->screen);

	return view;
}

W_View *W_CreateTopView(W_Screen * screen)
{
	W_View *view;

	view = createView(screen, screen->rootView);
	if (!view)
		return NULL;

	view->flags.topLevel = 1;
	view->attribs.event_mask |= XCB_EVENT_MASK_STRUCTURE_NOTIFY;

	return view;
}

W_View *W_CreateUnmanagedTopView(W_Screen * screen)
{
	W_View *view;

	view = createView(screen, screen->rootView);
	if (!view)
		return NULL;

	view->flags.topLevel = 1;
	view->attribs.event_mask |= XCB_EVENT_MASK_STRUCTURE_NOTIFY;

	view->attribFlags |= XCB_CW_OVERRIDE_REDIRECT;
	view->attribs.override_redirect = true;

	return view;
}

static bool realize_view(W_View *view, bool checked)
{
	xcb_window_t parentWID;
	xcb_connection_t *dpy = view->screen->display;
	W_View *ptr;
	xcb_visualtype_t *visual;
	xcb_generic_error_t *error;
	xcb_void_cookie_t cookie;
	int depth;
	uint32_t vals[15];

	assert(view->size.width > 0);
	assert(view->size.height > 0);

	if (view->parent && !view->parent->flags.realized) {
		wwarning("trying to realize widget of unrealized parent");
		return false;
	}

	if (!view->flags.realized) {

		if (view->parent == NULL) {
			wwarning("trying to realize widget without parent");
			return false;
		}

		visual = view->screen->visual;
		depth = view->screen->depth;
		if (view->flags.argb && view->screen->visual_argb) {
			/* the depth-32 visual differs from the parent's, so
			 * XCreateWindow requires an explicit colormap and
			 * literal border/background pixels (createView()
			 * already set CWColormap|CWBorderPixel|CWBackPixel).
			 * The WMColor pixels createView() stored were
			 * allocated on the default colormap and read as
			 * near-transparent noise on a direct-colour ARGB
			 * visual, so replace them with 0: an argb view
			 * repaints its whole surface through cairo anyway. */
			visual = view->screen->visual_argb;
			depth = 32;
			view->attribs.colormap = view->screen->rcontext->cmap_argb;
			view->attribs.border_pixel = 0;
			view->attribs.background_pixel = 0;
			view->attribFlags &= ~XCB_CW_BACK_PIXMAP;
		} else {
			view->flags.argb = 0;
		}

		parentWID = view->parent->window;
		view->window = xcb_generate_id(dpy);
		if (checked) {
			cookie = xcb_create_window_checked(dpy, depth, view->window, parentWID,
					 VDEV(view->screen, view->pos.x),
					 VDEV(view->screen, view->pos.y),
					 VDEV(view->screen, view->size.width),
					 VDEV(view->screen, view->size.height), 0,
					 XCB_WINDOW_CLASS_INPUT_OUTPUT,
					 visual ? visual->visual_id : XCB_COPY_FROM_PARENT,
					 wXWindowAttrValues(&view->attribs, view->attribFlags, vals), vals);
			error = xcb_request_check(dpy, cookie);
			if (error || xcb_connection_has_error(dpy)) {
				wwarning("could not create widget window");
				free(error);
				view->window = XCB_NONE;
				return false;
			}
		} else {
			xcb_create_window(dpy, depth, view->window, parentWID,
					  VDEV(view->screen, view->pos.x),
					  VDEV(view->screen, view->pos.y),
					  VDEV(view->screen, view->size.width),
					  VDEV(view->screen, view->size.height), 0,
					  XCB_WINDOW_CLASS_INPUT_OUTPUT,
					  visual ? visual->visual_id : XCB_COPY_FROM_PARENT,
					  wXWindowAttrValues(&view->attribs, view->attribFlags, vals), vals);
		}

		WMHashInsert(ViewTable, (void *)(uintptr_t) view->window, view);
		WMScreenNoteDrawableSizeScaled(view->screen, view->window,
					       view->size.width, view->size.height);

		view->flags.realized = 1;

		if (view->flags.mapWhenRealized) {
			W_MapView(view);
			view->flags.mapWhenRealized = 0;
		}

		WMPostNotificationName(WMViewRealizedNotification, view, NULL);
	}

	/* realize children */
	ptr = view->childrenList;
	while (ptr != NULL) {
		if (!realize_view(ptr, checked))
			return false;

		ptr = ptr->nextSister;
	}
	return true;
}

void W_RealizeView(W_View *view)
{
	(void)realize_view(view, false);
}

bool W_RealizeViewChecked(W_View *view)
{
	return realize_view(view, true);
}

void W_ReparentView(W_View * view, W_View * newParent, int x, int y)
{
	xcb_connection_t *dpy = view->screen->display;

	assert(!view->flags.topLevel);

	unparentView(view);
	adoptChildView(newParent, view);

	if (view->flags.realized) {
		if (newParent->flags.realized) {
			xcb_reparent_window(dpy, view->window, newParent->window, VDEV(view->screen, x), VDEV(view->screen, y));
		} else {
			wwarning("trying to reparent realized view to unrealized parent");
			return;
		}
	}

	view->pos.x = x;
	view->pos.y = y;
}

void W_RaiseView(W_View * view)
{
	if (W_VIEW_REALIZED(view))
		wXRaiseWindow(W_VIEW_DISPLAY(view), W_VIEW_DRAWABLE(view));
}

void W_LowerView(W_View * view)
{
	if (W_VIEW_REALIZED(view))
		wXLowerWindow(W_VIEW_DISPLAY(view), W_VIEW_DRAWABLE(view));
}

void W_MapView(W_View * view)
{
	if (!view->flags.mapped) {
		if (view->flags.realized) {
			wXMapRaised(view->screen->display, view->window);
			wXFlush(view->screen->display);
			view->flags.mapped = 1;
		} else {
			view->flags.mapWhenRealized = 1;
		}
	}
}

/*
 * W_MapSubviews-
 *     maps all children of the current view that where already realized.
 */
void W_MapSubviews(W_View * view)
{
	wXMapSubwindows(view->screen->display, view->window);
	wXFlush(view->screen->display);

	view = view->childrenList;
	while (view) {
		view->flags.mapped = 1;
		view->flags.mapWhenRealized = 0;
		view = view->nextSister;
	}
}

void W_UnmapSubviews(W_View * view)
{
	wXUnmapSubwindows(view->screen->display, view->window);
	wXFlush(view->screen->display);

	view = view->childrenList;
	while (view) {
		view->flags.mapped = 0;
		view->flags.mapWhenRealized = 0;
		view = view->nextSister;
	}
}

void W_UnmapView(W_View * view)
{
	view->flags.mapWhenRealized = 0;
	if (!view->flags.mapped)
		return;

	xcb_unmap_window(view->screen->display, view->window);
	wXFlush(view->screen->display);

	view->flags.mapped = 0;
}

W_View *W_TopLevelOfView(W_View * view)
{
	W_View *toplevel;

	for (toplevel = view; toplevel && !toplevel->flags.topLevel; toplevel = toplevel->parent) ;

	return toplevel;
}

static void destroyView(W_View * view)
{
	W_View *ptr;

	if (view->flags.alreadyDead)
		return;
	view->flags.alreadyDead = 1;

	/* delete the balloon text for the view, if there's any */
	WMSetBalloonTextForView(NULL, view);

	if (view->nextFocusChain)
		view->nextFocusChain->prevFocusChain = view->prevFocusChain;
	if (view->prevFocusChain)
		view->prevFocusChain->nextFocusChain = view->nextFocusChain;

	/* Do not leave focus in a inexisting control */
	if (W_FocusedViewOfToplevel(W_TopLevelOfView(view)) == view)
		W_SetFocusOfTopLevel(W_TopLevelOfView(view), NULL);

	if (view->flags.topLevel) {
		W_FocusInfo *info = view->screen->focusInfo;
		/* remove focus information associated to this toplevel */

		if (info) {
			if (info->toplevel == view) {
				view->screen->focusInfo = info->next;
				wfree(info);
			} else {
				while (info->next) {
					if (info->next->toplevel == view)
						break;
					info = info->next;
				}
				if (info->next) {
					W_FocusInfo *next = info->next->next;
					wfree(info->next);
					info->next = next;
				}
				/* else the toplevel did not have any focused subview */
			}
		}
	}

	/* destroy children recursively */
	while (view->childrenList != NULL) {
		ptr = view->childrenList;
		ptr->flags.parentDying = 1;

		W_DestroyView(ptr);

		if (ptr == view->childrenList) {
			view->childrenList = ptr->nextSister;
			ptr->parent = NULL;
		}
	}

	W_CallDestroyHandlers(view);

	if (view->flags.realized) {
		WMHashRemove(ViewTable, (void *)(uintptr_t) view->window);
		WMScreenAbandonDrawable(view->screen, view->window);

		/* if parent is being destroyed, it will die naturaly */
		if (!view->flags.parentDying || view->flags.topLevel)
			xcb_destroy_window(view->screen->display, view->window);
	}

	/* remove self from parent's children list */
	unparentView(view);

	/* the array has a wfree() destructor that will be called automatically */
	WMFreeArray(view->eventHandlers);
	view->eventHandlers = NULL;

	WMRemoveNotificationObserver(view);

	/* drop pending selection-owner and request-callback records that
	 * still point at this view, so a SelectionNotify that arrives
	 * after the view is freed cannot dereference it */
	W_ClearSelectionsForView(view);

	W_FreeViewXdndPart(view);

	if (view->backColor)
		WMReleaseColor(view->backColor);

	wfree(view);
}

void W_DestroyView(W_View * view)
{
	view->refCount--;

	if (view->refCount < 1) {
		destroyView(view);
	}
}

void W_MoveView(W_View * view, int x, int y)
{
	assert(view->flags.root == 0);

	if (view->delegate && view->delegate->willMove) {
		(*view->delegate->willMove) (view->delegate, view, &x, &y);
	}

	if (view->pos.x == x && view->pos.y == y)
		return;

	if (view->flags.realized) {
		/* a top-level's parent is root, and root coordinates are
		 * device pixels already, so only child-view positions are
		 * scaled inside their (scaled) parent */
		if (view->flags.topLevel)
			xcb_configure_window(view->screen->display, view->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ x, y });
		else
			xcb_configure_window(view->screen->display, view->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ VDEV(view->screen, x), VDEV(view->screen, y) });
	}
	view->pos.x = x;
	view->pos.y = y;

	if (view->delegate && view->delegate->didMove) {
		(*view->delegate->didMove) (view->delegate, view);
	}
}

void W_ResizeView(W_View * view, unsigned int width, unsigned int height)
{
	/*int shrinked; */

	if (view->delegate && view->delegate->willResize) {
		(*view->delegate->willResize) (view->delegate, view, &width, &height);
	}

	assert(width > 0);
	assert(height > 0);

	if (view->size.width == width && view->size.height == height)
		return;

	/*shrinked = width < view->size.width || height < view->size.height; */

	if (view->flags.realized) {
		xcb_configure_window(view->screen->display, view->window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, (const uint32_t[]){ VDEV(view->screen, width), VDEV(view->screen, height) });
		WMScreenNoteDrawableSizeScaled(view->screen, view->window, width, height);
	}
	view->size.width = width;
	view->size.height = height;

	if (view->delegate && view->delegate->didResize) {
		(*view->delegate->didResize) (view->delegate, view);
	}

	if (view->flags.notifySizeChanged)
		WMPostNotificationName(WMViewSizeDidChangeNotification, view, NULL);
}

void W_RedisplayView(W_View * view)
{
	xcb_generic_event_t *wire;

	if (!view->flags.mapped)
		return;

	/* synthesized expose, WMHandleEvent borrows it through dispatch */
	wire = W_SynthEvent(XCB_EXPOSE);
	((xcb_expose_event_t *)wire)->window = view->window;
	((xcb_expose_event_t *)wire)->count = 0;

	WMHandleEvent(wire);
	free(wire);
}

void W_SetViewBackgroundColor(W_View * view, WMColor * color)
{
	if (view->backColor)
		WMReleaseColor(view->backColor);
	view->backColor = WMRetainColor(color);

	view->attribFlags |= XCB_CW_BACK_PIXEL;
	view->attribFlags &= ~XCB_CW_BACK_PIXMAP;
	view->attribs.background_pixel = W_PIXEL(color);
	if (view->flags.realized) {
		xcb_change_window_attributes(view->screen->display, view->window, XCB_CW_BACK_PIXEL, (const uint32_t[]){ W_PIXEL(color) });
		xcb_clear_area(view->screen->display, 0, view->window, 0, 0, 0, 0);
	}
}

void W_SetViewBackgroundPixmap(W_View *view, WMPixmap *pix)
{
	if (!pix)
		return;

	if (view->backImage)
		WMReleasePixmap(view->backImage);
	view->backImage = WMRetainPixmap(pix);

	view->attribFlags |= XCB_CW_BACK_PIXMAP;
	view->attribFlags &= ~XCB_CW_BACK_PIXEL;
	view->attribs.background_pixmap = pix->pixmap;
	if (view->flags.realized) {
		xcb_change_window_attributes(view->screen->display, view->window, XCB_CW_BACK_PIXMAP, (const uint32_t[]){ pix->pixmap });
		xcb_clear_area(view->screen->display, 0, view->window, 0, 0, 0, 0);
	}
}

void W_SetViewCursor(W_View * view, xcb_cursor_t cursor)
{
	view->cursor = cursor;
	if (W_VIEW_REALIZED(view)) {
		wXDefineCursor(W_VIEW_DISPLAY(view), W_VIEW_DRAWABLE(view), cursor);
	} else {
		view->attribFlags |= XCB_CW_CURSOR;
		view->attribs.cursor = cursor;
	}
}

W_View *W_FocusedViewOfToplevel(W_View * view)
{
	WMScreen *scr = view->screen;
	W_FocusInfo *info;

	for (info = scr->focusInfo; info != NULL; info = info->next)
		if (view == info->toplevel)
			break;

	if (!info)
		return NULL;

	return info->focused;
}

/* synthesize a Focus{In,Out} wire event and dispatch it to the view's
 * handlers */
static void WSendFocusEvent(W_View * target, int type)
{
	xcb_generic_event_t *wire;

	if (target->window == XCB_NONE)
		return;

	wire = W_SynthEvent(type);
	((xcb_focus_in_event_t *)wire)->event = target->window;
	((xcb_focus_in_event_t *)wire)->mode = XCB_NOTIFY_MODE_NORMAL;
	((xcb_focus_in_event_t *)wire)->detail = XCB_NOTIFY_DETAIL_NONE;

	WMHandleEvent(wire);
	free(wire);
}

void W_SetFocusOfTopLevel(W_View * toplevel, W_View * view)
{
	WMScreen *scr = toplevel->screen;
	W_FocusInfo *info;

	for (info = scr->focusInfo; info != NULL; info = info->next)
		if (toplevel == info->toplevel)
			break;

	if (!info) {
		info = wmalloc(sizeof(W_FocusInfo));
		info->toplevel = toplevel;
		info->focused = view;
		info->next = scr->focusInfo;
		scr->focusInfo = info;
	} else {
		W_View *old = info->focused;

		/* update before dispatching FocusOut, a handler reached
		 * from there can destroy the toplevel and free this
		 * focus-info node, so info must not be written through
		 * after the callback */
		info->focused = view;
		if (old)
			WSendFocusEvent(old, XCB_FOCUS_OUT);
	}
	if (view) {
		/* simulate FocusIn event */
		WSendFocusEvent(view, XCB_FOCUS_IN);
	}
}

WMView *W_RetainView(WMView * view)
{
	view->refCount++;

	return view;
}

void W_ReleaseView(WMView * view)
{
	view->refCount--;

	if (view->refCount < 1) {
		destroyView(view);
	}
}

WMWidget *WMWidgetOfView(WMView * view)
{
	return view->self;
}

WMSize WMGetViewSize(WMView * view)
{
	return view->size;
}

WMPoint WMGetViewPosition(WMView * view)
{
	return view->pos;
}

void WMSetViewNotifySizeChanges(WMView * view, bool flag)
{
	view->flags.notifySizeChanged = ((flag == 0) ? 0 : 1);
}

xcb_window_t WMViewXID(WMView * view)
{
	return view->window;
}

WMPoint WMGetViewScreenPosition(WMView * view)
{
	WMScreen *scr = W_VIEW_SCREEN(view);
	xcb_window_t foo;
	int x, y, topX, topY;
	WXGeometry geo;
	WMView *topView;

	topView = view;
	while (topView->parent && topView->parent != scr->rootView)
		topView = topView->parent;

	if (!WXRcvGeometry(scr->display, WXReqGeometry(scr->display, W_VIEW_DRAWABLE(topView)), &geo)) {
		topX = topY = 0;
	} else {
		topX = geo.x;
		topY = geo.y;
	}

	wXTranslateCoordinates(scr->display, W_VIEW_DRAWABLE(view), scr->rootWin, 0, 0, &x, &y, &foo);

	return wmkpoint(x - topX, y - topY);
}

static void resizedParent(void *self, WMNotification * notif)
{
	WMSize size = WMGetViewSize((WMView *) WMGetNotificationObject(notif));
	WMView *view = (WMView *) self;

	W_MoveView(view, view->leftOffs, view->topOffs);
	W_ResizeView(view, size.width - (view->leftOffs + view->rightOffs),
		     size.height - (view->topOffs + view->bottomOffs));
}

void WMSetViewExpandsToParent(WMView * view, int leftOffs, int topOffs, int rightOffs, int bottomOffs)
{
	WMSize size = view->parent->size;

	view->topOffs = topOffs;
	view->bottomOffs = bottomOffs;
	view->leftOffs = leftOffs;
	view->rightOffs = rightOffs;

	WMAddNotificationObserver(resizedParent, view, WMViewSizeDidChangeNotification, view->parent);
	WMSetViewNotifySizeChanges(view->parent, true);

	W_MoveView(view, leftOffs, topOffs);
	W_ResizeView(view, size.width - (leftOffs + rightOffs), size.height - (topOffs + bottomOffs));
}
