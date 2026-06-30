/*
 *  clip-common.h - shared X selection / INCR transfer helpers
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  These helpers carry the ICCCM selection probe-then-read pattern and
 *  the INCR send/receive state machine used by wmclipd, wmiv and the
 *  wmclipd test client, so the bounds checks live in one place. They
 *  depend only on Xlib and libc so test binaries that do not link
 *  libWUtil can use them. All returned data buffers are malloc'd and
 *  must be released with free().
 */

#ifndef WMUTIL_CLIP_COMMON_H
#define WMUTIL_CLIP_COMMON_H

#include <X11/Xlib.h>
#include <stddef.h>

/* Cap on a single received selection, matching the production
 * clipboard daemon. Callers may pass a smaller value. */
#define CLIP_DEFAULT_MAX_BYTES   ((size_t)256 * 1024 * 1024)

/* Direct-vs-INCR send threshold and chunk size used by the daemon. */
#define CLIP_DEFAULT_DIRECT_LIMIT (64 * 1024)
#define CLIP_DEFAULT_CHUNK_SIZE   (64 * 1024)

/* Called for events that arrive while waiting for an INCR chunk but
 * are not the awaited PropertyNotify, so a selection owner can keep
 * serving other requests during a receive. May be NULL. */
typedef void (*ClipDispatchFn)(XEvent *ev, void *udata);

/* CLOCK_MONOTONIC in milliseconds, for callers that build their own
 * deadlines around clip_wait_event. */
long long clip_monotonic_ms(void);

/* Bounded wait for the next X event. Returns 1 with *ev filled, 0 on
 * timeout or poll error. timeout_ms < 0 waits indefinitely. */
int clip_wait_event(Display *dpy, XEvent *ev, int timeout_ms);

/* Probe-then-read-then-delete a format-8 property in full. Returns a
 * malloc'd buffer NUL-terminated at [*len_out] with the actual property
 * type in *type_out, or NULL on failure (missing property, format != 8,
 * size > max_bytes, partial read, allocation failure). */
unsigned char *clip_read_property8(Display *dpy, Window win, Atom property,
				    size_t max_bytes, Atom *type_out,
				    size_t *len_out);

/* Read the converted selection at `property`. If its type is
 * `incr_atom`, delete the placeholder and accumulate format-8 chunks
 * until the sender writes a zero-length chunk, with the total capped
 * at max_bytes and a per-chunk wait of incr_timeout_ms (< 0 to wait
 * indefinitely). Events arriving during the INCR receive that are not
 * the awaited PropertyNotify are passed to `dispatch`. Otherwise the
 * property is read directly. Returns malloc'd data NUL-terminated at
 * [*len_out] with the actual content type in *type_out, or NULL on
 * failure. */
unsigned char *clip_retrieve_property(Display *dpy, Window win, Atom property,
				       Atom incr_atom, size_t max_bytes,
				       int incr_timeout_ms,
				       ClipDispatchFn dispatch, void *udata,
				       Atom *type_out, size_t *len_out);

/* --- INCR send ----------------------------------------------------- */

typedef struct {
	int active;
	Window requestor;
	Atom property;
	Atom type;
	const unsigned char *data;
	size_t size;
	size_t offset;
	size_t chunk_size;
	long saved_event_mask;
} ClipIncrSend;

/* Begin an INCR transfer in reply to `req`: capture the requestor's
 * current event mask, select PropertyChange + StructureNotify on it,
 * write the INCR placeholder property, and populate *xfer. The caller
 * then sends the SelectionNotify (e.g. via WXSendSelectionNotify) and
 * keeps `data` alive until xfer->active becomes 0. Returns 0 if a
 * transfer is already active in *xfer or the requestor window is
 * gone. */
int clip_incr_send_start(Display *dpy, ClipIncrSend *xfer,
			 const XSelectionRequestEvent *req, Atom incr_atom,
			 Atom type, const unsigned char *data, size_t size,
			 size_t chunk_size);

/* Drive the transfer on a PropertyNotify. If `ev` is a PropertyDelete
 * on the transfer's requestor and property, write the next chunk (or
 * the terminating zero-length write and finish). Returns 1 if `ev`
 * was consumed by this transfer. */
int clip_incr_send_property_notify(Display *dpy, ClipIncrSend *xfer,
				   const XPropertyEvent *ev);

/* If `ev` names the transfer's requestor, cancel the transfer.
 * Returns 1 if `ev` was consumed. */
int clip_incr_send_destroy_notify(Display *dpy, ClipIncrSend *xfer,
				  const XDestroyWindowEvent *ev);

/* Cancel an active transfer and restore the requestor's event mask. */
void clip_incr_send_cancel(Display *dpy, ClipIncrSend *xfer);

#endif /* WMUTIL_CLIP_COMMON_H */
