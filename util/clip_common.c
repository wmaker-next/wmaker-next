/*
 *  clip_common.c - shared X selection / INCR transfer helpers
 *
 *  Copyright (c) 2026 Window Maker Team
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "clip_common.h"

long long clip_monotonic_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int clip_wait_event(xcb_connection_t *xc, xcb_generic_event_t **ev,
		    int timeout_ms)
{
	struct pollfd pfd;
	long long deadline;
	int rc;

	if (timeout_ms < 0)
		deadline = -1;
	else
		deadline = clip_monotonic_ms() + timeout_ms;

	for (;;) {
		*ev = xcb_poll_for_event(xc);
		if (*ev)
			return 1;
		if (xcb_connection_has_error(xc))
			return 0;

		if (deadline >= 0) {
			/* an interruption must consume the deadline,
			 * not restart the full timeout */
			timeout_ms = (int)(deadline - clip_monotonic_ms());
			if (timeout_ms <= 0)
				return 0;
		}
		pfd.fd = xcb_get_file_descriptor(xc);
		pfd.events = POLLIN;
		pfd.revents = 0;
		rc = poll(&pfd, 1, timeout_ms);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			return 0;
		}
		if (rc == 0)
			return 0;
		if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
			return 0;
	}
}

unsigned char *clip_read_property8(xcb_connection_t *xc, xcb_window_t win,
				    xcb_atom_t property, size_t max_bytes,
				    xcb_atom_t *type_out, size_t *len_out)
{
	xcb_get_property_cookie_t ck, ck2;
	xcb_get_property_reply_t *rep, *rep2;
	unsigned char *copy;
	uint32_t length;
	size_t value_len;

	*type_out = XCB_ATOM_NONE;
	*len_out = 0;

	/* Probe with zero length to learn the type and remaining size. */
	ck = xcb_get_property(xc, 0, win, property,
			      XCB_GET_PROPERTY_TYPE_ANY, 0, 0);
	rep = xcb_get_property_reply(xc, ck, NULL);
	if (!rep)
		return NULL;

	*type_out = rep->type;
	if (rep->type == XCB_ATOM_NONE) {
		free(rep);
		return NULL;
	}
	if (rep->format != 8 || rep->bytes_after > max_bytes) {
		free(rep);
		return NULL;
	}

	length = (rep->bytes_after + 3) / 4;
	if (length == 0)
		length = 1;
	free(rep);

	/* Full read with delete so the sender sees PropertyDelete. */
	ck2 = xcb_get_property(xc, 1, win, property,
			       XCB_GET_PROPERTY_TYPE_ANY, 0, length);
	rep2 = xcb_get_property_reply(xc, ck2, NULL);
	if (!rep2)
		return NULL;

	if (rep2->type == XCB_ATOM_NONE || rep2->format != 8 ||
	    rep2->bytes_after != 0 || rep2->value_len > max_bytes) {
		free(rep2);
		return NULL;
	}

	value_len = rep2->value_len;
	copy = malloc(value_len + 1);
	if (!copy) {
		free(rep2);
		return NULL;
	}
	if (value_len > 0)
		memcpy(copy, xcb_get_property_value(rep2), value_len);
	copy[value_len] = '\0';

	*type_out = rep2->type;
	*len_out = value_len;
	free(rep2);
	return copy;
}

static unsigned char *receive_incr(xcb_connection_t *xc, xcb_window_t win,
				  xcb_atom_t property, size_t max_bytes,
				  int chunk_timeout_ms,
				  ClipDispatchFn dispatch, void *udata,
				  xcb_atom_t *type_out, size_t *len_out)
{
	unsigned char *data, *grown, *chunk;
	long long deadline = 0, now;
	size_t size, chunk_len;
	xcb_atom_t chunk_type;
	xcb_generic_event_t *ev;
	int wait_ms;

	*type_out = XCB_ATOM_NONE;
	*len_out = 0;

	data = malloc(1);
	if (!data)
		return NULL;
	data[0] = '\0';
	size = 0;

	xcb_delete_property(xc, win, property);
	xcb_flush(xc);

	/* the chunk timeout is a deadline for the next chunk, events
	 * dispatched below must not postpone it, only a received chunk
	 * does */
	if (chunk_timeout_ms >= 0)
		deadline = clip_monotonic_ms() + chunk_timeout_ms;

	for (;;) {
		if (chunk_timeout_ms < 0) {
			wait_ms = -1;
		} else {
			now = clip_monotonic_ms();
			if (now >= deadline) {
				free(data);
				return NULL;
			}
			wait_ms = (int)(deadline - now);
		}
		if (!clip_wait_event(xc, &ev, wait_ms)) {
			free(data);
			return NULL;
		}
		if ((ev->response_type & 0x7f) != XCB_PROPERTY_NOTIFY ||
		    ((xcb_property_notify_event_t *)ev)->window != win ||
		    ((xcb_property_notify_event_t *)ev)->atom != property ||
		    ((xcb_property_notify_event_t *)ev)->state != XCB_PROPERTY_NEW_VALUE) {
			if (dispatch)
				dispatch(ev, udata);
			else
				free(ev);
			continue;
		}
		free(ev);

		chunk = clip_read_property8(xc, win, property, max_bytes,
					    &chunk_type, &chunk_len);
		if (!chunk) {
			/* A PropertyNotify for the INCR placeholder write or
			 * for a since-deleted chunk can arrive before the
			 * sender's next write lands, in which case the probe
			 * sees no property. Treat that as not-yet rather
			 * than as a failure. */
			if (chunk_type == XCB_ATOM_NONE)
				continue;
			free(data);
			return NULL;
		}

		if (chunk_len == 0) {
			free(chunk);
			*len_out = size;
			return data;
		}

		/* the sender is alive, the next chunk gets a fresh deadline */
		if (chunk_timeout_ms >= 0)
			deadline = clip_monotonic_ms() + chunk_timeout_ms;

		if (*type_out == XCB_ATOM_NONE)
			*type_out = chunk_type;

		if (chunk_len > max_bytes || size > max_bytes - chunk_len) {
			free(chunk);
			free(data);
			return NULL;
		}

		grown = realloc(data, size + chunk_len + 1);
		if (!grown) {
			free(chunk);
			free(data);
			return NULL;
		}
		data = grown;
		memcpy(data + size, chunk, chunk_len);
		size += chunk_len;
		data[size] = '\0';
		free(chunk);
	}
}

unsigned char *clip_retrieve_property(xcb_connection_t *xc, xcb_window_t win,
				      xcb_atom_t property, xcb_atom_t incr_atom,
				      size_t max_bytes, int incr_timeout_ms,
				      ClipDispatchFn dispatch, void *udata,
				      xcb_atom_t *type_out, size_t *len_out)
{
	xcb_get_property_cookie_t ck;
	xcb_get_property_reply_t *rep;

	*type_out = XCB_ATOM_NONE;
	*len_out = 0;

	ck = xcb_get_property(xc, 0, win, property,
			      XCB_GET_PROPERTY_TYPE_ANY, 0, 0);
	rep = xcb_get_property_reply(xc, ck, NULL);
	if (!rep)
		return NULL;

	if (rep->type == XCB_ATOM_NONE) {
		free(rep);
		return NULL;
	}

	if (incr_atom != XCB_ATOM_NONE && rep->type == incr_atom) {
		free(rep);
		return receive_incr(xc, win, property, max_bytes,
				    incr_timeout_ms, dispatch, udata,
				    type_out, len_out);
	}

	free(rep);
	return clip_read_property8(xc, win, property, max_bytes,
				    type_out, len_out);
}

int clip_incr_send_start(xcb_connection_t *xc, ClipIncrSend *xfer,
			 const xcb_selection_request_event_t *req,
			 xcb_atom_t incr_atom, xcb_atom_t type,
			 const unsigned char *data, size_t size,
			 size_t chunk_size)
{
	xcb_get_window_attributes_cookie_t ck;
	xcb_get_window_attributes_reply_t *attr;
	uint32_t size32, mask;

	if (xfer->active)
		return 0;

	ck = xcb_get_window_attributes(xc, req->requestor);
	attr = xcb_get_window_attributes_reply(xc, ck, NULL);
	if (!attr)
		return 0;

	xfer->active = 1;
	xfer->requestor = req->requestor;
	xfer->property = req->property == XCB_ATOM_NONE ? req->target : req->property;
	xfer->type = type;
	xfer->data = data;
	xfer->size = size;
	xfer->offset = 0;
	xfer->chunk_size = chunk_size > 0 ? chunk_size : CLIP_DEFAULT_CHUNK_SIZE;
	xfer->saved_event_mask = attr->your_event_mask;
	xfer->last_progress_ms = clip_monotonic_ms();

	mask = attr->your_event_mask |
	       XCB_EVENT_MASK_PROPERTY_CHANGE |
	       XCB_EVENT_MASK_STRUCTURE_NOTIFY;
	free(attr);

	xcb_change_window_attributes(xc, req->requestor,
				     XCB_CW_EVENT_MASK, &mask);
	size32 = (uint32_t)size;
	xcb_change_property(xc, XCB_PROP_MODE_REPLACE, req->requestor,
			    xfer->property, incr_atom, 32, 1, &size32);
	xcb_flush(xc);
	return 1;
}

void clip_incr_send_cancel(xcb_connection_t *xc, ClipIncrSend *xfer)
{
	if (!xfer->active)
		return;
	if (xfer->requestor != 0)
		xcb_change_window_attributes(xc, xfer->requestor,
					      XCB_CW_EVENT_MASK,
					      &xfer->saved_event_mask);
	memset(xfer, 0, sizeof(*xfer));
}

int clip_incr_send_property_notify(xcb_connection_t *xc, ClipIncrSend *xfer,
				   const xcb_property_notify_event_t *ev)
{
	size_t remaining, to_send;

	if (!xfer->active ||
	    ev->window != xfer->requestor ||
	    ev->atom != xfer->property ||
	    ev->state != XCB_PROPERTY_DELETE) {
		return 0;
	}

	if (xfer->offset >= xfer->size) {
		xcb_change_property(xc, XCB_PROP_MODE_REPLACE, xfer->requestor,
				    xfer->property, xfer->type, 8, 0, NULL);
		xcb_flush(xc);
		clip_incr_send_cancel(xc, xfer);
		return 1;
	}

	remaining = xfer->size - xfer->offset;
	to_send = remaining > xfer->chunk_size ? xfer->chunk_size : remaining;

	xcb_change_property(xc, XCB_PROP_MODE_REPLACE, xfer->requestor,
			    xfer->property, xfer->type, 8, to_send,
			    xfer->data + xfer->offset);
	xcb_flush(xc);
	xfer->offset += to_send;
	xfer->last_progress_ms = clip_monotonic_ms();
	return 1;
}

int clip_incr_send_check(xcb_connection_t *xc, ClipIncrSend *xfer,
			 long long now_ms, long long timeout_ms)
{
	if (!xfer->active || now_ms - xfer->last_progress_ms < timeout_ms)
		return 0;
	clip_incr_send_cancel(xc, xfer);
	return 1;
}

int clip_incr_send_handle_error(ClipIncrSend *xfer, unsigned long resource_id)
{
	if (!xfer->active || resource_id != xfer->requestor)
		return 0;
	/* the window is gone, do not restore its event mask */
	memset(xfer, 0, sizeof(*xfer));
	return 1;
}

int clip_incr_send_destroy_notify(xcb_connection_t *xc, ClipIncrSend *xfer,
				  const xcb_destroy_notify_event_t *ev)
{
	if (!xfer->active || ev->window != xfer->requestor)
		return 0;
	clip_incr_send_cancel(xc, xfer);
	return 1;
}
