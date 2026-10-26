/*
 *  clip_common.h - shared X selection / INCR transfer helpers
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
 *
 *  These helpers carry the ICCCM selection probe-then-read pattern and
 *  the INCR send/receive state machine used by wmclipd, wmiv and the
 *  wmclipd test client, so the bounds checks live in one place. They
 *  depend only on libxcb and libc so test binaries that do not link
 *  libWUtil can use them. All returned data buffers are malloc'd and
 *  must be released with free().
 */

#ifndef WMUTIL_CLIP_COMMON_H
#define WMUTIL_CLIP_COMMON_H

#include <xcb/xcb.h>
#include <stddef.h>
#include <stdint.h>

typedef struct xcb_selection_request_event_t xcb_selection_request_event_t;
typedef struct xcb_property_notify_event_t   xcb_property_notify_event_t;
typedef struct xcb_destroy_notify_event_t    xcb_destroy_notify_event_t;

/* Cap on a single received selection, matching the production
 * clipboard daemon. Callers may pass a smaller value. */
#define CLIP_DEFAULT_MAX_BYTES   ((size_t)256 * 1024 * 1024)

/* Direct-vs-INCR send threshold and chunk size used by the daemon. */
#define CLIP_DEFAULT_DIRECT_LIMIT (64 * 1024)
#define CLIP_DEFAULT_CHUNK_SIZE   (64 * 1024)

/* Silence timeout for an outgoing INCR transfer, matching the
 * per-chunk wait the receive side gives its sender. */
#define CLIP_DEFAULT_INCR_SEND_TIMEOUT_MS 5000

/* Dispatch for events arriving during an INCR receive that are not the
 * awaited PropertyNotify. May be NULL. The dispatch takes ownership of
 * the event and must free it or retain it for later processing. */
typedef void (*ClipDispatchFn)(xcb_generic_event_t *ev, void *udata);

/* CLOCK_MONOTONIC in milliseconds, for callers that build their own
 * deadlines around clip_wait_event. */
long long clip_monotonic_ms(void);

/* Bounded wait for the next xcb event. Returns 1 with *ev filled, 0 on
 * timeout or connection error. timeout_ms < 0 waits indefinitely. The
 * returned event is malloc'd by xcb and must be freed with free(). */
int clip_wait_event(xcb_connection_t *xc, xcb_generic_event_t **ev,
		    int timeout_ms);

/* Probe-then-read-then-delete a format-8 property in full. Returns a
 * malloc'd buffer NUL-terminated at [*len_out] with the actual property
 * type in *type_out, or NULL on failure (missing property, format != 8,
 * size > max_bytes, partial read, allocation failure). */
unsigned char *clip_read_property8(xcb_connection_t *xc, xcb_window_t win,
				   xcb_atom_t property, size_t max_bytes,
				   xcb_atom_t *type_out, size_t *len_out);

/* Read the converted selection at `property`. If its type is
 * `incr_atom`, delete the placeholder and accumulate format-8 chunks
 * until the sender writes a zero-length chunk, with the total capped
 * at max_bytes and a per-chunk wait of incr_timeout_ms (< 0 to wait
 * indefinitely). Events arriving during the INCR receive that are not
 * the awaited PropertyNotify are passed to `dispatch`. Otherwise the
 * property is read directly. Returns malloc'd data NUL-terminated at
 * [*len_out] with the actual content type in *type_out, or NULL on
 * failure. */
unsigned char *clip_retrieve_property(xcb_connection_t *xc, xcb_window_t win,
				      xcb_atom_t property, xcb_atom_t incr_atom,
				      size_t max_bytes, int incr_timeout_ms,
				      ClipDispatchFn dispatch, void *udata,
				      xcb_atom_t *type_out, size_t *len_out);

/* --- INCR send ----------------------------------------------------- */

typedef struct {
	int		    active;
	xcb_window_t	    requestor;
	xcb_atom_t	    property;
	xcb_atom_t	    type;
	const unsigned char *data;
	size_t		    size;
	size_t		    offset;
	size_t		    chunk_size;
	uint32_t	    saved_event_mask;
	long long	    last_progress_ms;
} ClipIncrSend;

/* Begin an INCR transfer in reply to `req`: capture the requestor's
 * current event mask, select PropertyChange + StructureNotify on it,
 * write the INCR placeholder property, and populate *xfer. The caller
 * then sends the SelectionNotify and keeps `data` alive until
 * xfer->active becomes 0. Returns 0 if a transfer is already active
 * in *xfer or the requestor window is gone. */
int clip_incr_send_start(xcb_connection_t *xc, ClipIncrSend *xfer,
			 const xcb_selection_request_event_t *req,
			 xcb_atom_t incr_atom, xcb_atom_t type,
			 const unsigned char *data, size_t size,
			 size_t chunk_size);

/* Drive the transfer on a PropertyNotify. If `ev` is a PropertyDelete
 * on the transfer's requestor and property, write the next chunk (or
 * the terminating zero-length write and finish). Returns 1 if `ev`
 * was consumed by this transfer. */
int clip_incr_send_property_notify(xcb_connection_t *xc, ClipIncrSend *xfer,
				   const xcb_property_notify_event_t *ev);

/* If `ev` names the transfer's requestor, cancel the transfer.
 * Returns 1 if `ev` was consumed. */
int clip_incr_send_destroy_notify(xcb_connection_t *xc, ClipIncrSend *xfer,
				  const xcb_destroy_notify_event_t *ev);

/* Cancel the transfer when it has made no progress for timeout_ms
 * (now_ms is clip_monotonic_ms()). Covers a requestor that died
 * between the attribute probe and the StructureNotify mask change,
 * where no DestroyNotify can ever arrive. Returns 1 if it cancelled
 * a transfer. */
int clip_incr_send_check(xcb_connection_t *xc, ClipIncrSend *xfer,
			 long long now_ms, long long timeout_ms);

/* Consume an X protocol error: a resource_id matching the transfer's
 * requestor cancels the transfer without touching the dead window.
 * Returns 1 if the error was consumed. */
int clip_incr_send_handle_error(ClipIncrSend *xfer, unsigned long resource_id);

/* Cancel an active transfer and restore the requestor's event mask. */
void clip_incr_send_cancel(xcb_connection_t *xc, ClipIncrSend *xfer);

#endif /* WMUTIL_CLIP_COMMON_H */
