/*
 *  WINGs X selection ownership and request handling
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2009-2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <time.h>


#include "WINGsP.h"
#include "WINGs/WXProp.h"

/* Maximum total selection payload accepted, both for a single direct
 * property read and for the accumulated INCR transfer. Large enough for
 * realistic text and small image pastes while still bounding what an
 * untrusted owner can make us allocate. */
#define MAX_SELECTION_SIZE ((size_t)4 * 1024 * 1024)

/* How long to wait for the next INCR chunk before giving up. */
#define INCR_CHUNK_TIMEOUT_MS 5000

/* How long the whole INCR transfer may take regardless of the chunk
 * cadence: a per-gap timeout alone lets an owner dribble one byte just
 * under the gap forever while the caller's event loop is blocked. */
#define INCR_TOTAL_TIMEOUT_MS 30000

const char *WMSelectionOwnerDidChangeNotification = "WMSelectionOwnerDidChange";

typedef struct SelectionHandler {
	WMView *view;
	xcb_atom_t selection;
	xcb_timestamp_t timestamp;
	WMSelectionProcs procs;
	void *data;

	struct {
		unsigned delete_pending:1;
		unsigned done_pending:1;
	} flags;
} SelectionHandler;

typedef struct SelectionCallback {
	WMView *view;
	xcb_atom_t selection;
	xcb_atom_t target;
	xcb_timestamp_t timestamp;
	WMSelectionCallback *callback;
	void *data;

	struct {
		unsigned delete_pending:1;
		unsigned done_pending:1;
	} flags;
} SelectionCallback;

static WMArray *selCallbacks = NULL;

static WMArray *selHandlers = NULL;

void WMDeleteSelectionHandler(WMView * view, xcb_atom_t selection, xcb_timestamp_t timestamp)
{
	SelectionHandler *handler;
	xcb_connection_t *dpy = W_VIEW_SCREEN(view)->display;
	xcb_window_t win = W_VIEW_DRAWABLE(view);
	WMArrayIterator iter;

	if (!selHandlers)
		return;

	WM_ITERATE_ARRAY(selHandlers, handler, iter) {
		if (handler->view == view && (handler->selection == selection || selection == XCB_NONE)
		    && (handler->timestamp == timestamp || timestamp == XCB_CURRENT_TIME)) {

			if (handler->flags.done_pending) {
				handler->flags.delete_pending = 1;
				return;
			}
			WMRemoveFromArray(selHandlers, handler);
			break;
		}
	}

	wXGrabServer(dpy);
	if (wXGetSelectionOwner(dpy, selection) == win) {
		wXSetSelectionOwner(dpy, selection, XCB_NONE, timestamp);
	}
	wXUngrabServer(dpy);
}

static void WMDeleteSelectionCallback(WMView * view, xcb_atom_t selection, xcb_timestamp_t timestamp)
{
	SelectionCallback *handler;
	WMArrayIterator iter;

	if (!selCallbacks)
		return;

	WM_ITERATE_ARRAY(selCallbacks, handler, iter) {
		if (handler->view == view && (handler->selection == selection || selection == XCB_NONE)
		    && (handler->timestamp == timestamp || timestamp == XCB_CURRENT_TIME)) {

			if (handler->flags.done_pending) {
				handler->flags.delete_pending = 1;
				return;
			}
			WMRemoveFromArray(selCallbacks, handler);
			break;
		}
	}
}

/* Pure core of writeSelection's format-32 arm: a format-32 WMData is
 * an array of longs (an item is sizeof(long) bytes on LP64), the wire
 * wants one uint32 per item. The repack is truncating on a trailing
 * partial long, matching the byte-length-aware sizing. Returns the
 * item count and a wmalloc'd array (free with wfree()), 0 with *out
 * NULL when there is nothing to send. */
size_t W_XSelectionFormat32Repack(const unsigned char *bytes, size_t len, uint32_t **out)
{
	const long *l = (const long *) bytes;
	size_t n = len / sizeof(long);
	size_t i;

	*out = NULL;
	if (n == 0)
		return 0;
	*out = wmalloc(n * sizeof(uint32_t));
	for (i = 0; i < n; i++)
		(*out)[i] = (uint32_t) l[i];
	return n;
}

static bool writeSelection(xcb_connection_t * dpy, xcb_window_t requestor, xcb_atom_t property, xcb_atom_t type, WMData * data)
{
	int format;

	format = WMGetDataFormat(data);
	/* X selections only carry format 8, 16 or 32, anything else is a
	 * caller bug and would make the element size zero below */
	if (format != 8 && format != 16 && format != 32)
		format = 8;

	if (format == 32) {
		/* Xlib stores format-32 items as longs (an item is 8
		 * bytes on LP64), the wire wants one uint32 per item,
		 * repack so the LP64 sizing stays byte-length aware */
		uint32_t *v;
		size_t n = W_XSelectionFormat32Repack(WMDataBytes(data),
						      WMGetDataLength(data), &v);
		bool ok = WXCheckVoid(dpy,
				      xcb_change_property_checked(dpy, XCB_PROP_MODE_REPLACE,
								  requestor, property, type,
								  32, n, v));

		wfree(v);
		return ok;
	}

	{
		const unsigned char *bytes = WMDataBytes(data);
		size_t len = WMGetDataLength(data);
		size_t n = len / (size_t)(format / 8);

		if (!WXCheckVoid(dpy,
				 xcb_change_property_checked(dpy, XCB_PROP_MODE_REPLACE,
							     requestor, property, type,
							     format, n, bytes)))
			return false;
	}
	xcb_flush(dpy);
	return true;
}

static void notifySelection(xcb_generic_event_t * event, xcb_atom_t prop)
{
	WXSendSelectionNotify(W_XEvqConnection(),
			      (const xcb_selection_request_event_t *)event, prop);
}

static void handleRequestEvent(xcb_generic_event_t * event)
{
	SelectionHandler *handler;
	WMArrayIterator iter;
	WMArray *copy;
	bool handledRequest;

	WM_ITERATE_ARRAY(selHandlers, handler, iter) {

		switch ((event->response_type & 0x7f)) {
		case XCB_SELECTION_CLEAR:
			if (W_VIEW_DRAWABLE(handler->view)
			    != ((xcb_selection_clear_event_t *)event)->owner) {
				break;
			}

			handler->flags.done_pending = 1;
			if (handler->procs.selectionLost)
				handler->procs.selectionLost(handler->view, handler->selection, handler->data);
			handler->flags.done_pending = 0;
			handler->flags.delete_pending = 1;
			break;

		case XCB_SELECTION_REQUEST:
			if (W_VIEW_DRAWABLE(handler->view) != ((xcb_selection_request_event_t *)event)->owner) {
				break;
			}

			if (handler->procs.convertSelection != NULL
			    && handler->selection == ((xcb_selection_request_event_t *)event)->selection) {
				xcb_atom_t atom;
				WMData *data;
				xcb_atom_t prop;

				/* they're requesting for something old.. maybe another handler
				 * can handle it */
				if (((xcb_selection_request_event_t *)event)->time < handler->timestamp
				    && ((xcb_selection_request_event_t *)event)->time != XCB_CURRENT_TIME) {
					break;
				}

				handledRequest = false;

				handler->flags.done_pending = 1;

				data = handler->procs.convertSelection(handler->view,
								       handler->selection,
								       ((xcb_selection_request_event_t *)event)->target,
								       handler->data, &atom);

				prop = ((xcb_selection_request_event_t *)event)->property;
				/* obsolete clients that don't set the property field */
				if (prop == XCB_NONE)
					prop = ((xcb_selection_request_event_t *)event)->target;

				if (data) {
					if (writeSelection(W_XEvqConnection(),
							   ((xcb_selection_request_event_t *)event)->requestor, prop, atom, data)) {
						handledRequest = true;
					}
					WMReleaseData(data);
				}

				notifySelection(event, (handledRequest == true ? prop : XCB_NONE));

				if (handler->procs.selectionDone != NULL) {
					handler->procs.selectionDone(handler->view,
								     handler->selection,
								     ((xcb_selection_request_event_t *)event)->target,
								     handler->data);
				}

				handler->flags.done_pending = 0;
			}
			break;
		}
	}

	/* delete handlers. A screen that never owned a selection has no
	 * array and a forged SelectionRequest would otherwise deref
	 * NULL here, so guard the duplicate */
	if (selHandlers) {
		copy = WMDuplicateArray(selHandlers);
		WM_ITERATE_ARRAY(copy, handler, iter) {
			if (handler && handler->flags.delete_pending) {
				WMDeleteSelectionHandler(handler->view, handler->selection, handler->timestamp);
			}
		}
		WMFreeArray(copy);
	}
}

/* status codes kept from the Xlib XGetWindowProperty contract the
 * INCR/INCR-less read paths below were written against */
enum {
	WXPropSuccess = 0,
	WXPropBadAlloc = 1
};

/* XGetWindowProperty shim over xcb_get_property: the returned buffer
 * keeps the Xlib LP64 shape (format-32 items widened to unsigned long,
 * 8 and 16 bit items raw), is malloc()ed and released with free().
 * Returns WXPropSuccess or WXPropBadAlloc on a wire failure. */
static int xgetprop(xcb_connection_t *dpy, xcb_window_t win, xcb_atom_t prop, long offset, long length,
		    bool del, xcb_atom_t req_type, xcb_atom_t *rtype, int *bits,
		    unsigned long *len, unsigned long *bytes, unsigned char **data)
{
	xcb_get_property_cookie_t ck;
	xcb_get_property_reply_t *rep;

	ck = xcb_get_property(dpy, del, win, prop, req_type,
			      offset, length);
	rep = xcb_get_property_reply(dpy, ck, NULL);
	if (!rep)
		return WXPropBadAlloc;

	*rtype = rep->type;
	*bits = rep->format;
	*len = rep->value_len;
	*bytes = rep->bytes_after;
	*data = NULL;

	if (rep->value_len > 0) {
		if (rep->format == 32) {
			/* the wire packs one uint32 per item, the callers
			 * below keep treating format-32 data as one long
			 * per item (the Xlib LP64 shape) */
			const uint32_t *wire = xcb_get_property_value(rep);
			unsigned long *out = malloc(rep->value_len
						    * sizeof(unsigned long));
			uint32_t i;

			if (!out) {
				free(rep);
				return WXPropBadAlloc;
			}
			for (i = 0; i < rep->value_len; i++)
				out[i] = wire[i];
			*data = (unsigned char *) out;
		} else {
			size_t n = xcb_get_property_value_length(rep);

			*data = malloc(n);
			if (!*data) {
				free(rep);
				return WXPropBadAlloc;
			}
			memcpy(*data, xcb_get_property_value(rep), n);
		}
	}
	free(rep);
	return WXPropSuccess;
}

/* the fields a PropertyNotify must carry to be the owner's answer,
 * anything else stays queued for the normal dispatch */
struct IncrPropMatch {
	xcb_atom_t where;
	uint8_t state;
};

static bool matchIncrProp(const xcb_generic_event_t *event, void *cdata)
{
	const xcb_property_notify_event_t *pe = (const xcb_property_notify_event_t *)event;
	const struct IncrPropMatch *m = cdata;

	return pe->atom == m->where && pe->state == m->state;
}

static bool waitPropertyNewValue(xcb_connection_t *dpy, xcb_window_t win, xcb_atom_t where)
{
	struct IncrPropMatch match;
	xcb_generic_event_t *ev = NULL;

	/* take the property events from the WINGs queue so the drain is
	 * not flipped and non-matching events stay queued for the
	 * normal dispatch after we return */
	match.where = where;
	match.state = XCB_PROPERTY_NEW_VALUE;
	if (!WXEvqTakeTypedTimed(dpy, XCB_PROPERTY_NOTIFY, win, matchIncrProp, &match, &ev, INCR_CHUNK_TIMEOUT_MS))
		return false;
	free(ev);
	return true;
}

static WMData *readIncrSelection(xcb_connection_t *dpy, xcb_window_t win, xcb_atom_t where)
{
	WXWindowAttributes attr;
	WMData *wdata = NULL;
	unsigned char *chunk;
	xcb_atom_t rtype;
	int bits, chunk_bits = 0;
	unsigned long len, bytes;
	long old_mask;
	size_t total = 0;
	bool failed = false;
	struct timespec started;

	/* the requestor window is a WINGs view that may not have asked
	 * for PropertyNotify, add it for the duration of the transfer
	 * and restore the previous mask afterwards so the view's normal
	 * event handling is unaffected */
	if (!wXGetWindowAttributes(dpy, win, &attr, NULL))
		return NULL;
	old_mask = attr.your_event_mask;
	xcb_change_window_attributes(dpy, win, XCB_CW_EVENT_MASK, (const uint32_t[]){ old_mask | XCB_EVENT_MASK_PROPERTY_CHANGE });

	/* deleting the INCR property tells the owner we are ready for
	 * the first chunk */
	wXDeleteProperty(dpy, win, where);

	clock_gettime(CLOCK_MONOTONIC, &started);

	wdata = WMCreateDataWithCapacity(0);

	while (!failed) {
		struct timespec now;
		long elapsed_ms;

		clock_gettime(CLOCK_MONOTONIC, &now);
		elapsed_ms = (long)(now.tv_sec - started.tv_sec) * 1000
			   + (now.tv_nsec - started.tv_nsec) / 1000000;
		if (elapsed_ms > INCR_TOTAL_TIMEOUT_MS) {
			wwarning("gave up on the INCR selection after %ld ms", elapsed_ms);
			failed = true;
			break;
		}
		if (!waitPropertyNewValue(dpy, win, where)) {
			failed = true;
			break;
		}
		chunk = NULL;
		if (xgetprop(dpy, win, where, 0,
			     (long)(MAX_SELECTION_SIZE / 4), true,
			     XCB_ATOM_ANY, &rtype, &bits, &len,
			     &bytes, &chunk) != WXPropSuccess) {
			failed = true;
			break;
		}
		if (rtype == XCB_NONE || (bits != 8 && bits != 16 && bits != 32) ||
		    bytes != 0) {
			if (chunk)
				free(chunk);
			failed = true;
			break;
		}
		if (len == 0) {
			/* zero-length chunk terminates the transfer */
			if (chunk)
				free(chunk);
			break;
		}
		/* format-32 replies are LP64 longs, not 4-byte units */
		len *= (bits == 32) ? sizeof(long) : (size_t)(bits / 8);
		if (len > MAX_SELECTION_SIZE || total > MAX_SELECTION_SIZE - len) {
			free(chunk);
			failed = true;
			break;
		}
		WMAppendDataBytes(wdata, chunk, (unsigned)len);
		total += len;
		chunk_bits = bits;
		free(chunk);
	}

	xcb_change_window_attributes(dpy, win, XCB_CW_EVENT_MASK, (const uint32_t[]){ old_mask });

	if (failed || total == 0) {
		WMReleaseData(wdata);
		return NULL;
	}
	WMSetDataFormat(wdata, chunk_bits);
	return wdata;
}

static WMData *getSelectionData(xcb_connection_t *dpy, xcb_window_t win, xcb_atom_t where, xcb_atom_t expected_type)
{
	WMData *wdata;
	unsigned char *data;
	xcb_atom_t rtype;
	int bits, bpi;
	unsigned long len, bytes;
	long want;

	/* probe the property first so an INCR placeholder or an oversized
	 * direct value can be handled before committing to a fixed-length
	 * read */
	if (xgetprop(dpy, win, where, 0, 0, false, XCB_ATOM_ANY,
		     &rtype, &bits, &len, &bytes, &data) != WXPropSuccess) {
		return NULL;
	}
	if (data)
		free(data);

	if (rtype == XCB_NONE)
		return NULL;

	if (rtype == wx_static_atoms[W_ATOM_INCR])
		return readIncrSelection(dpy, win, where);

	if (bytes > MAX_SELECTION_SIZE) {
		wXDeleteProperty(dpy, win, where);
		return NULL;
	}

	want = (long)((bytes + 3) / 4);
	if (want == 0)
		want = 1;

	if (xgetprop(dpy, win, where, 0, want, true, expected_type,
		     &rtype, &bits, &len, &bytes, &data) != WXPropSuccess) {
		return NULL;
	}

	/* the property is owner-supplied, only accept the formats X
	 * actually defines and refuse a partial read so callbacks never see
	 * a truncated value. Requesting expected_type means a type mismatch
	 * comes back as len == 0 with bytes_after set, which the bytes != 0
	 * test rejects, so the callback is never handed data of a type it
	 * did not ask for. */
	if (rtype == XCB_NONE || (bits != 8 && bits != 16 && bits != 32) || bytes != 0) {
		if (data)
			free(data);
		return NULL;
	}

	/* xgetprop keeps the Xlib shape: format-32 items are longs (8
	 * bytes on LP64), not 4 */
	bpi = (bits == 32) ? sizeof(long) : bits / 8;

	wdata = WMCreateDataWithBytesNoCopy(data, len * bpi, free);
	WMSetDataFormat(wdata, bits);

	return wdata;
}

static void handleNotifyEvent(xcb_generic_event_t * event)
{
	SelectionCallback *handler;
	WMArrayIterator iter;
	WMArray *copy;
	WMData *data;
	bool refused;

	WM_ITERATE_ARRAY(selCallbacks, handler, iter) {

		if (W_VIEW_DRAWABLE(handler->view) != ((xcb_selection_notify_event_t *)event)->requestor
		    || handler->selection != ((xcb_selection_notify_event_t *)event)->selection
		    || handler->target != ((xcb_selection_notify_event_t *)event)->target) {
			continue;
		}
		handler->flags.done_pending = 1;

		refused = (((xcb_selection_notify_event_t *)event)->property == XCB_NONE);
		if (refused) {
			data = NULL;
		} else {
			data = getSelectionData(W_XEvqConnection(),
						((xcb_selection_notify_event_t *)event)->requestor,
						((xcb_selection_notify_event_t *)event)->property,
						handler->target);
		}

		(*handler->callback) (handler->view, handler->selection,
				      handler->target, handler->timestamp, handler->data, data,
				      refused);

		if (data != NULL) {
			WMReleaseData(data);
		}
		handler->flags.done_pending = 0;
		handler->flags.delete_pending = 1;
	}

	/* delete callbacks, same forged-event guard: no array means
	 * nothing was ever registered */
	if (selCallbacks) {
		copy = WMDuplicateArray(selCallbacks);
		WM_ITERATE_ARRAY(copy, handler, iter) {
			if (handler && handler->flags.delete_pending) {
				WMDeleteSelectionCallback(handler->view, handler->selection, handler->timestamp);
			}
		}
		WMFreeArray(copy);
	}
}

/*
 * Drop every pending selection-owner record and request callback that
 * still points at this view. Called from destroyView() so a delayed
 * or forged SelectionNotify cannot reach a freed widget. Entries that
 * are in-flight (done_pending set, the callback for this very view is
 * on the stack) are left for the post-callback sweep, which keys on
 * the handler struct itself and so does not need the view to be live.
 */
void W_ClearSelectionsForView(WMView *view)
{
	SelectionHandler *h;
	SelectionCallback *cb;
	int i;

	if (selHandlers) {
		for (i = WMGetArrayItemCount(selHandlers) - 1; i >= 0; i--) {
			h = WMGetFromArray(selHandlers, i);
			if (h && h->view == view && !h->flags.done_pending)
				WMDeleteFromArray(selHandlers, i);
		}
	}
	if (selCallbacks) {
		for (i = WMGetArrayItemCount(selCallbacks) - 1; i >= 0; i--) {
			cb = WMGetFromArray(selCallbacks, i);
			if (cb && cb->view == view && !cb->flags.done_pending)
				WMDeleteFromArray(selCallbacks, i);
		}
	}
}

void WMCancelSelectionRequests(WMView *view)
{
	SelectionCallback *cb;
	int i;

	if (!selCallbacks)
		return;

	for (i = WMGetArrayItemCount(selCallbacks) - 1; i >= 0; i--) {
		cb = WMGetFromArray(selCallbacks, i);
		if (cb && cb->view == view && !cb->flags.done_pending)
			WMDeleteFromArray(selCallbacks, i);
	}
}

void W_HandleSelectionEvent(xcb_generic_event_t * event)
{
	if ((event->response_type & 0x7f) == XCB_SELECTION_NOTIFY) {
		handleNotifyEvent(event);
	} else {
		handleRequestEvent(event);
	}
}

bool WMCreateSelectionHandler(WMView * view, xcb_atom_t selection, xcb_timestamp_t timestamp, WMSelectionProcs * procs, void *cdata)
{
	SelectionHandler *handler;
	xcb_connection_t *dpy = W_VIEW_SCREEN(view)->display;

	wXSetSelectionOwner(dpy, selection, W_VIEW_DRAWABLE(view), timestamp);
	if (wXGetSelectionOwner(dpy, selection) != W_VIEW_DRAWABLE(view)) {
		return false;
	}

	WMPostNotificationName(WMSelectionOwnerDidChangeNotification, (void *)(uintptr_t) selection, (void *)(uintptr_t) view);

	handler = wmalloc(sizeof(SelectionHandler));
	handler->view = view;
	handler->selection = selection;
	handler->timestamp = timestamp;
	handler->procs = *procs;
	handler->data = cdata;

	if (selHandlers == NULL) {
		selHandlers = WMCreateArrayWithDestructor(4, wfree);
	}

	WMAddToArray(selHandlers, handler);

	return true;
}

bool
WMRequestSelection(WMView * view, xcb_atom_t selection, xcb_atom_t target, xcb_timestamp_t timestamp,
		   WMSelectionCallback * callback, void *cdata)
{
	SelectionCallback *handler;

	if (wXGetSelectionOwner(W_VIEW_SCREEN(view)->display, selection) == XCB_NONE)
		return false;

	if (!wXConvertSelection(W_VIEW_SCREEN(view)->display, selection, target,
			       W_VIEW_SCREEN(view)->clipboardAtom, W_VIEW_DRAWABLE(view), timestamp)) {
		return false;
	}

	handler = wmalloc(sizeof(SelectionCallback));
	handler->view = view;
	handler->selection = selection;
	handler->target = target;
	handler->timestamp = timestamp;
	handler->callback = callback;
	handler->data = cdata;

	if (selCallbacks == NULL) {
		selCallbacks = WMCreateArrayWithDestructor(4, wfree);
	}

	WMAddToArray(selCallbacks, handler);

	return true;
}
