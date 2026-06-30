/*
 *  clip-common.c - shared X selection / INCR transfer helpers
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "clip-common.h"

long long clip_monotonic_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int clip_wait_event(Display *dpy, XEvent *ev, int timeout_ms)
{
	struct pollfd pfd;
	int rc;

	while (!XPending(dpy)) {
		pfd.fd = ConnectionNumber(dpy);
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

	XNextEvent(dpy, ev);
	return 1;
}

unsigned char *clip_read_property8(Display *dpy, Window win, Atom property,
				     size_t max_bytes, Atom *type_out,
				     size_t *len_out)
{
	Atom type, type2;
	int format, format2;
	unsigned long nitems, bytes_after, nitems2, bytes_after2;
	unsigned char *prop = NULL, *prop2 = NULL, *copy;
	long length;

	*type_out = None;
	*len_out = 0;

	if (XGetWindowProperty(dpy, win, property, 0, 0, False,
			       AnyPropertyType, &type, &format, &nitems,
			       &bytes_after, &prop) != Success) {
		return NULL;
	}
	if (prop)
		XFree(prop);

	*type_out = type;
	if (type == None)
		return NULL;
	if (format != 8 || bytes_after > max_bytes)
		return NULL;

	length = (long)((bytes_after + 3) / 4);
	if (length == 0)
		length = 1;

	if (XGetWindowProperty(dpy, win, property, 0, length, True,
			       AnyPropertyType, &type2, &format2, &nitems2,
			       &bytes_after2, &prop2) != Success) {
		return NULL;
	}

	if (type2 == None || format2 != 8 || bytes_after2 != 0 || nitems2 > max_bytes) {
		if (prop2)
			XFree(prop2);
		return NULL;
	}

	copy = malloc((size_t)nitems2 + 1);
	if (!copy) {
		XFree(prop2);
		return NULL;
	}
	if (nitems2 > 0)
		memcpy(copy, prop2, (size_t)nitems2);
	copy[nitems2] = '\0';
	if (prop2)
		XFree(prop2);

	*type_out = type2;
	*len_out = (size_t)nitems2;
	return copy;
}

static unsigned char *receive_incr(Display *dpy, Window win, Atom property,
				   size_t max_bytes, int chunk_timeout_ms,
				   ClipDispatchFn dispatch, void *udata,
				   Atom *type_out, size_t *len_out)
{
	unsigned char *data, *grown, *chunk;
	size_t size, chunk_len;
	Atom chunk_type;
	XEvent ev;

	*type_out = None;
	*len_out = 0;

	data = malloc(1);
	if (!data)
		return NULL;
	data[0] = '\0';
	size = 0;

	XDeleteProperty(dpy, win, property);
	XFlush(dpy);

	for (;;) {
		if (!clip_wait_event(dpy, &ev, chunk_timeout_ms)) {
			free(data);
			return NULL;
		}
		if (ev.type != PropertyNotify ||
		    ev.xproperty.window != win ||
		    ev.xproperty.atom != property ||
		    ev.xproperty.state != PropertyNewValue) {
			if (dispatch)
				dispatch(&ev, udata);
			continue;
		}

		chunk = clip_read_property8(dpy, win, property, max_bytes,
					     &chunk_type, &chunk_len);
		if (!chunk) {
			/* A PropertyNotify for the INCR placeholder write or
			 * for a since-deleted chunk can arrive before the
			 * sender's next write lands, in which case the probe
			 * sees no property. Treat that as not-yet rather
			 * than as a failure. */
			if (chunk_type == None)
				continue;
			free(data);
			return NULL;
		}

		if (chunk_len == 0) {
			free(chunk);
			*len_out = size;
			return data;
		}

		if (*type_out == None)
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

unsigned char *clip_retrieve_property(Display *dpy, Window win, Atom property,
					Atom incr_atom, size_t max_bytes,
					int incr_timeout_ms,
					ClipDispatchFn dispatch, void *udata,
					Atom *type_out, size_t *len_out)
{
	Atom type;
	int format;
	unsigned long nitems, bytes_after;
	unsigned char *prop = NULL;

	*type_out = None;
	*len_out = 0;

	if (XGetWindowProperty(dpy, win, property, 0, 0, False,
			       AnyPropertyType, &type, &format, &nitems,
			       &bytes_after, &prop) != Success) {
		return NULL;
	}
	if (prop)
		XFree(prop);

	if (type == None)
		return NULL;

	if (incr_atom != None && type == incr_atom)
		return receive_incr(dpy, win, property, max_bytes,
				    incr_timeout_ms, dispatch, udata,
				    type_out, len_out);

	return clip_read_property8(dpy, win, property, max_bytes,
				    type_out, len_out);
}

int clip_incr_send_start(Display *dpy, ClipIncrSend *xfer,
			 const XSelectionRequestEvent *req, Atom incr_atom,
			 Atom type, const unsigned char *data, size_t size,
			 size_t chunk_size)
{
	XWindowAttributes attr;
	unsigned long size32;

	if (xfer->active)
		return 0;
	if (!XGetWindowAttributes(dpy, req->requestor, &attr))
		return 0;

	xfer->active = 1;
	xfer->requestor = req->requestor;
	xfer->property = req->property == None ? req->target : req->property;
	xfer->type = type;
	xfer->data = data;
	xfer->size = size;
	xfer->offset = 0;
	xfer->chunk_size = chunk_size > 0 ? chunk_size : CLIP_DEFAULT_CHUNK_SIZE;
	xfer->saved_event_mask = attr.your_event_mask;

	XSelectInput(dpy, req->requestor,
		     attr.your_event_mask | PropertyChangeMask | StructureNotifyMask);
	size32 = (unsigned long)size;
	XChangeProperty(dpy, req->requestor, xfer->property, incr_atom, 32,
			PropModeReplace, (unsigned char *)&size32, 1);
	XFlush(dpy);
	return 1;
}

void clip_incr_send_cancel(Display *dpy, ClipIncrSend *xfer)
{
	if (!xfer->active)
		return;
	if (xfer->requestor != None)
		XSelectInput(dpy, xfer->requestor, xfer->saved_event_mask);
	memset(xfer, 0, sizeof(*xfer));
}

int clip_incr_send_property_notify(Display *dpy, ClipIncrSend *xfer,
				    const XPropertyEvent *ev)
{
	size_t remaining, to_send;

	if (!xfer->active ||
	    ev->window != xfer->requestor ||
	    ev->atom != xfer->property ||
	    ev->state != PropertyDelete) {
		return 0;
	}

	if (xfer->offset >= xfer->size) {
		XChangeProperty(dpy, xfer->requestor, xfer->property,
				xfer->type, 8, PropModeReplace, NULL, 0);
		XFlush(dpy);
		clip_incr_send_cancel(dpy, xfer);
		return 1;
	}

	remaining = xfer->size - xfer->offset;
	to_send = remaining > xfer->chunk_size ? xfer->chunk_size : remaining;

	XChangeProperty(dpy, xfer->requestor, xfer->property, xfer->type, 8,
			PropModeReplace, xfer->data + xfer->offset, (int)to_send);
	XFlush(dpy);
	xfer->offset += to_send;
	return 1;
}

int clip_incr_send_destroy_notify(Display *dpy, ClipIncrSend *xfer,
				  const XDestroyWindowEvent *ev)
{
	if (!xfer->active || ev->window != xfer->requestor)
		return 0;
	clip_incr_send_cancel(dpy, xfer);
	return 1;
}
