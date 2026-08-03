/*
 *  wmclipd - X11 clipboard daemon with Window Maker screenshot IPC support
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <xcb/xcb.h>
#include <xcb/xfixes.h>

#include <WINGs/WUtil.h>
#include <WINGs/WIPC.h>

#include "config.h"
#include "clip-common.h"

#define IPC_RECONNECT_MS   1000
#define SELECTION_WAIT_MS  1000
#define INCR_WAIT_MS       5000

typedef enum {
	CONTENT_EMPTY,
	CONTENT_CAPTURED,
	CONTENT_SCREENSHOT
} ContentKind;

typedef struct {
	ContentKind kind;
	unsigned char *data;
	size_t size;
	xcb_atom_t type;
	char *path;
	char *uri;
	xcb_timestamp_t timestamp;
	int owns;
} ClipBuffer;

static const char *prog;
static xcb_connection_t *xc;
static xcb_window_t owner_window;
static xcb_window_t root_window;
static int xfixes_first_event;
static int run_clipboard;
static int run_screenshots;
static int verbose;
static volatile sig_atomic_t running = 1;

static xcb_atom_t atom_clipboard;
static xcb_atom_t atom_primary;
static xcb_atom_t atom_targets;
static xcb_atom_t atom_incr;
static xcb_atom_t atom_utf8;
static xcb_atom_t atom_text;
static xcb_atom_t atom_compound_text;
static xcb_atom_t atom_png;
static xcb_atom_t atom_jpeg;
static xcb_atom_t atom_uri_list;
static xcb_atom_t atom_text_plain;
static xcb_atom_t atom_text_plain_utf8;
static xcb_atom_t atom_wmclipd_property;

static ClipBuffer clipboard_buffer;
static ClipBuffer primary_buffer;
static ClipIncrSend incr;
static ClipBuffer *incr_source;

static void handle_selection_request(xcb_selection_request_event_t *req);
static void handle_selection_clear(xcb_selection_clear_event_t *ev);
static void handle_property_notify(xcb_property_notify_event_t *ev);
static void handle_destroy_notify(xcb_destroy_notify_event_t *ev);

static void usage(FILE *out)
{
	fprintf(out,
		"Usage: %s [--clipboard] [--screenshots] [--debug]\n"
		"       %s --help|-h\n"
		"       %s --version|-v\n"
		"Options:\n"
		"  --clipboard    Run as an X11 clipboard manager.\n"
		"  --debug        Print runtime diagnostics to stderr.\n"
		"  --help, -h     Show this message.\n"
		"  --screenshots  Subscribe to Window Maker screenshot IPC events and publish them to CLIPBOARD.\n"
		"                 The singular --screenshot is accepted as an alias.\n"
		"  --version, -v  Print version information and exit.\n",
		prog, prog, prog);
}

static void signal_handler(int signo)
{
	(void)signo;
	running = 0;
}

static void debug_log(const char *fmt, ...)
	__attribute__((__format__(printf, 1, 2)));

static void debug_log(const char *fmt, ...)
{
	va_list args;

	if (!verbose)
		return;

	fprintf(stderr, "%s: ", prog);
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fputc('\n', stderr);
}

static char *debug_atom_name(xcb_atom_t atom)
{
	xcb_get_atom_name_cookie_t ck;
	xcb_get_atom_name_reply_t *rep;
	char *copy;
	int len;

	if (atom == XCB_ATOM_NONE)
		return wstrdup("None");

	ck = xcb_get_atom_name(xc, atom);
	rep = xcb_get_atom_name_reply(xc, ck, NULL);
	if (!rep)
		return wstrdup("unknown");

	len = xcb_get_atom_name_name_length(rep);
	copy = wmalloc(len + 1);
	memcpy(copy, xcb_get_atom_name_name(rep), len);
	copy[len] = '\0';
	free(rep);
	return copy;
}

static const char *debug_selection_name(xcb_atom_t selection)
{
	if (selection == atom_clipboard)
		return "CLIPBOARD";
	if (selection == atom_primary)
		return "PRIMARY";
	return "selection";
}

static int buffer_has_payload(const ClipBuffer *buf)
{
	if (buf->kind == CONTENT_CAPTURED)
		return buf->data != NULL;
	if (buf->kind == CONTENT_SCREENSHOT)
		return buf->path != NULL || buf->data != NULL;
	return 0;
}

static int is_text_atom(xcb_atom_t atom)
{
	return atom == atom_utf8 ||
	       atom == XCB_ATOM_STRING ||
	       atom == atom_text ||
	       atom == atom_compound_text ||
	       atom == atom_text_plain ||
	       atom == atom_text_plain_utf8;
}

static int is_image_atom(xcb_atom_t atom)
{
	return atom == atom_png || atom == atom_jpeg;
}

static int selection_type_matches_target(xcb_atom_t target, xcb_atom_t actual)
{
	if (actual == target)
		return 1;
	return is_text_atom(target) && is_text_atom(actual);
}

static void clear_buffer(ClipBuffer *buf)
{
	if (incr.active && incr_source == buf) {
		clip_incr_send_cancel(xc, &incr);
		incr_source = NULL;
	}

	wfree(buf->data);
	wfree(buf->path);
	wfree(buf->uri);
	memset(buf, 0, sizeof(*buf));
}

static void dispatch_during_capture(xcb_generic_event_t *ev, void *udata)
{
	uint8_t rt;

	(void)udata;
	rt = ev->response_type & 0x7f;
	if (rt == XCB_SELECTION_REQUEST)
		handle_selection_request((xcb_selection_request_event_t *)ev);
	else if (rt == XCB_SELECTION_CLEAR)
		handle_selection_clear((xcb_selection_clear_event_t *)ev);
	else if (rt == XCB_PROPERTY_NOTIFY)
		handle_property_notify((xcb_property_notify_event_t *)ev);
	else if (rt == XCB_DESTROY_NOTIFY)
		handle_destroy_notify((xcb_destroy_notify_event_t *)ev);
	free(ev);
}

static int wait_selection_notify(xcb_atom_t selection, xcb_atom_t property,
				 xcb_selection_notify_event_t *out)
{
	xcb_generic_event_t *ev;
	long long deadline = clip_monotonic_ms() + SELECTION_WAIT_MS;
	int wait_ms;

	while (running) {
		wait_ms = (int)(deadline - clip_monotonic_ms());
		if (wait_ms <= 0)
			return 0;
		if (!clip_wait_event(xc, &ev, wait_ms))
			return 0;
		if ((ev->response_type & 0x7f) == XCB_SELECTION_NOTIFY) {
			xcb_selection_notify_event_t *sn =
				(xcb_selection_notify_event_t *)ev;
			if (sn->selection == selection &&
			    (sn->property == property ||
			     sn->property == XCB_ATOM_NONE)) {
				*out = *sn;
				free(ev);
				return 1;
			}
		}
		dispatch_during_capture(ev, NULL);
	}
	return 0;
}

static int query_targets(xcb_atom_t selection, xcb_timestamp_t timestamp,
			xcb_atom_t **targets_out, size_t *count_out)
{
	xcb_selection_notify_event_t sev;
	xcb_get_property_cookie_t ck;
	xcb_get_property_reply_t *rep;
	xcb_atom_t *copy;

	*targets_out = NULL;
	*count_out = 0;

	xcb_convert_selection(xc, owner_window, selection, atom_targets,
			      atom_wmclipd_property, timestamp);
	xcb_flush(xc);

	if (!wait_selection_notify(selection, atom_wmclipd_property, &sev) ||
	    sev.property == XCB_ATOM_NONE) {
		return 0;
	}

	ck = xcb_get_property(xc, 1, owner_window, atom_wmclipd_property,
			      XCB_ATOM_ATOM, 0, 1024);
	rep = xcb_get_property_reply(xc, ck, NULL);
	if (!rep)
		return 0;

	if (rep->type != XCB_ATOM_ATOM || rep->format != 32 ||
	    rep->value_len == 0 || rep->bytes_after != 0) {
		free(rep);
		return 0;
	}

	copy = wmalloc(sizeof(xcb_atom_t) * (size_t)rep->value_len);
	memcpy(copy, xcb_get_property_value(rep),
	       sizeof(xcb_atom_t) * (size_t)rep->value_len);
	*targets_out = copy;
	*count_out = rep->value_len;
	free(rep);
	return 1;
}

static int target_in_list(const xcb_atom_t *targets, size_t count,
			  xcb_atom_t atom)
{
	size_t i;

	for (i = 0; i < count; i++) {
		if (targets[i] == atom)
			return 1;
	}
	return 0;
}

static xcb_atom_t choose_preferred_target(const xcb_atom_t *targets,
					   size_t count)
{
	xcb_atom_t prefs[6];
	size_t i;

	prefs[0] = atom_png;
	prefs[1] = atom_jpeg;
	prefs[2] = atom_utf8;
	prefs[3] = XCB_ATOM_STRING;
	prefs[4] = atom_text;
	prefs[5] = atom_compound_text;

	for (i = 0; i < sizeof(prefs) / sizeof(prefs[0]); i++) {
		if (target_in_list(targets, count, prefs[i]))
			return prefs[i];
	}
	return XCB_ATOM_NONE;
}

static int request_selection_data(xcb_atom_t selection, xcb_atom_t target,
				  xcb_timestamp_t timestamp,
				  xcb_atom_t *type_out,
				  unsigned char **data_out, size_t *len_out)
{
	xcb_selection_notify_event_t sev;
	unsigned char *data;
	xcb_atom_t type;
	size_t len;

	*data_out = NULL;
	*len_out = 0;
	*type_out = XCB_ATOM_NONE;

	xcb_delete_property(xc, owner_window, atom_wmclipd_property);
	xcb_convert_selection(xc, owner_window, selection, target,
			      atom_wmclipd_property, timestamp);
	xcb_flush(xc);

	if (!wait_selection_notify(selection, atom_wmclipd_property, &sev) ||
	    sev.property == XCB_ATOM_NONE) {
		return 0;
	}

	data = clip_retrieve_property(xc, owner_window, atom_wmclipd_property,
				     atom_incr, CLIP_DEFAULT_MAX_BYTES,
				     INCR_WAIT_MS, dispatch_during_capture,
				     NULL, &type, &len);
	if (!data)
		return 0;
	if (!selection_type_matches_target(target, type)) {
		free(data);
		return 0;
	}

	*type_out = type == XCB_ATOM_NONE ? target : type;
	*data_out = data;
	*len_out = len;
	return 1;
}

static xcb_window_t selection_owner(xcb_atom_t selection)
{
	xcb_get_selection_owner_cookie_t ck;
	xcb_get_selection_owner_reply_t *rep;
	xcb_window_t owner;

	ck = xcb_get_selection_owner(xc, selection);
	rep = xcb_get_selection_owner_reply(xc, ck, NULL);
	owner = rep ? rep->owner : 0;
	free(rep);
	return owner;
}

static int claim_selection(xcb_atom_t selection, ClipBuffer *buf,
			   xcb_timestamp_t timestamp)
{
	xcb_set_selection_owner(xc, owner_window, selection, timestamp);
	xcb_flush(xc);

	if (selection_owner(selection) != owner_window) {
		buf->owns = 0;
		debug_log("failed to own %s", debug_selection_name(selection));
		return 0;
	}

	buf->owns = 1;
	buf->timestamp = timestamp;
	debug_log("owning %s", debug_selection_name(selection));
	return 1;
}

static int capture_selection(xcb_atom_t selection, ClipBuffer *buf,
			     xcb_timestamp_t timestamp, int claim_after_capture)
{
	xcb_atom_t *targets = NULL;
	size_t target_count = 0;
	xcb_atom_t target, type;
	unsigned char *data = NULL;
	size_t size = 0;
	xcb_atom_t fallbacks[6];
	size_t i;
	int ok = 0;

	debug_log("capturing %s", debug_selection_name(selection));

	if (selection_owner(selection) == owner_window)
		return 1;

	target = XCB_ATOM_NONE;
	if (query_targets(selection, timestamp, &targets, &target_count))
		target = choose_preferred_target(targets, target_count);
	wfree(targets);

	if (target != XCB_ATOM_NONE)
		ok = request_selection_data(selection, target, timestamp,
					    &type, &data, &size);

	if (!ok) {
		fallbacks[0] = atom_png;
		fallbacks[1] = atom_jpeg;
		fallbacks[2] = atom_utf8;
		fallbacks[3] = XCB_ATOM_STRING;
		fallbacks[4] = atom_text;
		fallbacks[5] = atom_compound_text;

		for (i = 0; i < sizeof(fallbacks) / sizeof(fallbacks[0]); i++) {
			if (fallbacks[i] == target)
				continue;
			if (request_selection_data(selection, fallbacks[i],
						   timestamp, &type, &data,
						   &size)) {
				ok = 1;
				break;
			}
		}
	}

	if (!ok) {
		debug_log("capture failed for %s",
			  debug_selection_name(selection));
		if (buffer_has_payload(buf))
			claim_selection(selection, buf, timestamp);
		return 0;
	}

	clear_buffer(buf);
	buf->kind = CONTENT_CAPTURED;
	buf->data = data;
	buf->size = size;
	buf->type = type == XCB_ATOM_NONE ? target : type;
	if (verbose) {
		char *type_name = debug_atom_name(buf->type);
		debug_log("captured %s: %zu bytes as %s",
			  debug_selection_name(selection), buf->size, type_name);
		wfree(type_name);
	}
	if (claim_after_capture) {
		claim_selection(selection, buf, timestamp);
	} else {
		buf->owns = 0;
		buf->timestamp = timestamp;
		debug_log("cached %s without owning it",
			  debug_selection_name(selection));
	}
	return 1;
}

static int copy_captured_payload(ClipBuffer *dst, const ClipBuffer *src)
{
	if (src->kind != CONTENT_CAPTURED || !src->data)
		return 0;

	clear_buffer(dst);
	dst->kind = CONTENT_CAPTURED;
	dst->data = wmalloc(src->size ? src->size : 1);
	if (src->size > 0)
		memcpy(dst->data, src->data, src->size);
	dst->size = src->size;
	dst->type = src->type;
	return 1;
}

static int mirror_primary_to_clipboard(xcb_timestamp_t timestamp,
				       const char *reason, int images_only)
{
	if (images_only && !is_image_atom(primary_buffer.type))
		return 0;
	if (!copy_captured_payload(&clipboard_buffer, &primary_buffer))
		return 0;

	debug_log("mirroring PRIMARY to CLIPBOARD (%s): %zu bytes",
		  reason, clipboard_buffer.size);
	return claim_selection(atom_clipboard, &clipboard_buffer, timestamp);
}

static int mirror_clipboard_text_to_primary(xcb_timestamp_t timestamp,
					    const char *reason)
{
	if (!is_text_atom(clipboard_buffer.type))
		return 0;
	if (!copy_captured_payload(&primary_buffer, &clipboard_buffer))
		return 0;

	primary_buffer.owns = 0;
	primary_buffer.timestamp = timestamp;
	debug_log("mirroring CLIPBOARD text to PRIMARY cache (%s): %zu bytes",
		  reason, primary_buffer.size);
	return 1;
}

static void add_atom_unique(xcb_atom_t *atoms, int *count, xcb_atom_t atom)
{
	int i;

	if (atom == XCB_ATOM_NONE)
		return;
	for (i = 0; i < *count; i++) {
		if (atoms[i] == atom)
			return;
	}
	atoms[(*count)++] = atom;
}

static int make_targets(const ClipBuffer *buf, xcb_atom_t *atoms)
{
	int count = 0;

	add_atom_unique(atoms, &count, atom_targets);

	if (buf->kind == CONTENT_SCREENSHOT) {
		add_atom_unique(atoms, &count, buf->type);
		add_atom_unique(atoms, &count, atom_uri_list);
		add_atom_unique(atoms, &count, atom_utf8);
		add_atom_unique(atoms, &count, XCB_ATOM_STRING);
		add_atom_unique(atoms, &count, atom_text_plain);
		add_atom_unique(atoms, &count, atom_text_plain_utf8);
		return count;
	}

	if (buf->kind == CONTENT_CAPTURED) {
		add_atom_unique(atoms, &count, buf->type);
		if (is_text_atom(buf->type)) {
			add_atom_unique(atoms, &count, atom_utf8);
			add_atom_unique(atoms, &count, XCB_ATOM_STRING);
			add_atom_unique(atoms, &count, atom_text);
			add_atom_unique(atoms, &count, atom_text_plain);
			add_atom_unique(atoms, &count, atom_text_plain_utf8);
		}
	}

	return count;
}

static int payload_for_request(ClipBuffer *buf, xcb_atom_t target,
			       const unsigned char **data, size_t *size,
			       xcb_atom_t *type)
{
	if (!buffer_has_payload(buf))
		return 0;

	if (buf->kind == CONTENT_SCREENSHOT) {
		if (target == buf->type && buf->data) {
			*data = buf->data;
			*size = buf->size;
			*type = buf->type;
			return 1;
		}
		if (target == atom_uri_list && buf->uri) {
			*data = (const unsigned char *)buf->uri;
			*size = strlen(buf->uri);
			*type = atom_uri_list;
			return 1;
		}
		if (is_text_atom(target) && buf->path) {
			*data = (const unsigned char *)buf->path;
			*size = strlen(buf->path);
			*type = target == XCB_ATOM_STRING ?
				XCB_ATOM_STRING : target;
			return 1;
		}
		return 0;
	}

	if (buf->kind == CONTENT_CAPTURED) {
		if (target == buf->type ||
		    (is_text_atom(target) && is_text_atom(buf->type))) {
			*data = buf->data;
			*size = buf->size;
			*type = target == buf->type ? buf->type : target;
			return 1;
		}
	}

	return 0;
}

static void send_selection_notify(xcb_selection_request_event_t *req,
				 xcb_atom_t property)
{
	xcb_selection_notify_event_t ev;

	memset(&ev, 0, sizeof(ev));
	ev.response_type = XCB_SELECTION_NOTIFY;
	ev.time = req->time;
	ev.requestor = req->requestor;
	ev.selection = req->selection;
	ev.target = req->target;
	ev.property = property;
	xcb_send_event(xc, 0, req->requestor, 0, (const char *)&ev);
	xcb_flush(xc);
}

static void handle_selection_request(xcb_selection_request_event_t *req)
{
	ClipBuffer *buf;
	xcb_atom_t property, targets[12], type;
	const unsigned char *data;
	size_t size;
	int count;

	property = req->property == XCB_ATOM_NONE ? req->target : req->property;
	if (verbose) {
		char *target_name = debug_atom_name(req->target);
		debug_log("selection request: %s target=%s requestor=0x%x",
			  debug_selection_name(req->selection), target_name,
			  (unsigned int)req->requestor);
		wfree(target_name);
	}
	buf = NULL;
	if (req->selection == atom_clipboard)
		buf = &clipboard_buffer;
	else if (req->selection == atom_primary)
		buf = &primary_buffer;

	if (!buf || !buf->owns || !buffer_has_payload(buf)) {
		send_selection_notify(req, XCB_ATOM_NONE);
		return;
	}

	if (req->target == atom_targets) {
		count = make_targets(buf, targets);
		xcb_change_property(xc, XCB_PROP_MODE_REPLACE, req->requestor,
				    property, XCB_ATOM_ATOM, 32, count, targets);
		send_selection_notify(req, property);
		return;
	}

	if (!payload_for_request(buf, req->target, &data, &size, &type)) {
		send_selection_notify(req, XCB_ATOM_NONE);
		return;
	}

	if (size > CLIP_DEFAULT_DIRECT_LIMIT) {
		if (!clip_incr_send_start(xc, &incr, req, atom_incr, type,
					   data, size, CLIP_DEFAULT_CHUNK_SIZE)) {
			send_selection_notify(req, XCB_ATOM_NONE);
			return;
		}
		incr_source = buf;
		debug_log("starting INCR transfer to 0x%x: %zu bytes",
			  (unsigned int)req->requestor, size);
		send_selection_notify(req, property);
		return;
	}

	xcb_change_property(xc, XCB_PROP_MODE_REPLACE, req->requestor,
			    property, type, 8, size, data);
	send_selection_notify(req, property);
}

static void handle_selection_clear(xcb_selection_clear_event_t *ev)
{
	ClipBuffer *buf;

	if (ev->selection == atom_clipboard)
		buf = &clipboard_buffer;
	else if (ev->selection == atom_primary)
		buf = &primary_buffer;
	else
		return;

	buf->owns = 0;
	debug_log("lost %s ownership", debug_selection_name(ev->selection));
	if (!run_clipboard)
		clear_buffer(buf);
}

static void handle_property_notify(xcb_property_notify_event_t *ev)
{
	if (clip_incr_send_property_notify(xc, &incr, ev)) {
		if (incr.active)
			debug_log("sent INCR chunk to 0x%x: %zu/%zu bytes",
				  (unsigned int)incr.requestor, incr.offset,
				  incr.size);
		else
			incr_source = NULL;
	}
}

static void handle_destroy_notify(xcb_destroy_notify_event_t *ev)
{
	if (clip_incr_send_destroy_notify(xc, &incr, ev)) {
		debug_log("cancelled INCR transfer: requestor 0x%x was destroyed",
			  (unsigned int)ev->window);
		incr_source = NULL;
	}
}

static void handle_selection_change(xcb_xfixes_selection_notify_event_t *ev)
{
	ClipBuffer *buf;
	xcb_window_t source_owner, clipboard_owner;
	int captured;

	if (ev->selection == atom_clipboard)
		buf = &clipboard_buffer;
	else if (ev->selection == atom_primary)
		buf = &primary_buffer;
	else
		return;

	if (ev->owner == owner_window)
		return;

	debug_log("selection changed: %s owner=0x%x",
		  debug_selection_name(ev->selection),
		  (unsigned int)ev->owner);
	buf->owns = 0;
	if (ev->owner == 0) {
		if (ev->selection == atom_clipboard && !buffer_has_payload(buf) &&
		    mirror_primary_to_clipboard(ev->timestamp,
						"clipboard owner cleared", 1))
			return;
		if (buffer_has_payload(buf))
			claim_selection(ev->selection, buf, ev->timestamp);
		return;
	}

	source_owner = ev->owner;
	if (ev->selection == atom_primary) {
		captured = capture_selection(ev->selection, buf, ev->timestamp, 0);
		if (!captured)
			return;
		clipboard_owner = selection_owner(atom_clipboard);
		if (clipboard_owner == source_owner) {
			mirror_primary_to_clipboard(ev->timestamp,
						    "same source owner", 0);
		} else if (clipboard_owner == 0) {
			mirror_primary_to_clipboard(ev->timestamp,
						    "clipboard empty", 1);
		}
	} else {
		captured = capture_selection(ev->selection, buf, ev->timestamp, 1);
		if (captured) {
			xcb_window_t primary_owner = selection_owner(atom_primary);

			if (primary_owner == source_owner)
				mirror_clipboard_text_to_primary(ev->timestamp,
								 "same source owner");
		}
	}
}

static void handle_x_event(xcb_generic_event_t *ev)
{
	uint8_t rt = ev->response_type & 0x7f;

	if (run_clipboard && rt == xfixes_first_event + XCB_XFIXES_SELECTION_NOTIFY) {
		handle_selection_change((xcb_xfixes_selection_notify_event_t *)ev);
		return;
	}

	switch (rt) {
	case XCB_SELECTION_REQUEST:
		handle_selection_request((xcb_selection_request_event_t *)ev);
		break;
	case XCB_SELECTION_CLEAR:
		handle_selection_clear((xcb_selection_clear_event_t *)ev);
		break;
	case XCB_PROPERTY_NOTIFY:
		handle_property_notify((xcb_property_notify_event_t *)ev);
		break;
	case XCB_DESTROY_NOTIFY:
		handle_destroy_notify((xcb_destroy_notify_event_t *)ev);
		break;
	default:
		break;
	}
}

static char *file_uri_from_path(const char *path)
{
	static const char hex[] = "0123456789ABCDEF";
	const unsigned char *p;
	char *uri, *out;
	size_t len;

	len = strlen(path);
	uri = wmalloc(7 + len * 3 + 3);
	out = uri;
	memcpy(out, "file://", 7);
	out += 7;

	for (p = (const unsigned char *)path; *p; p++) {
		if (isalnum(*p) || *p == '/' || *p == '-' || *p == '_' ||
		    *p == '.' || *p == '~') {
			*out++ = (char)*p;
		} else {
			*out++ = '%';
			*out++ = hex[*p >> 4];
			*out++ = hex[*p & 0x0f];
		}
	}
	*out++ = '\r';
	*out++ = '\n';
	*out = '\0';
	return uri;
}

static int read_file_bounded(const char *path, unsigned char **data_out,
			     size_t *size_out)
{
	struct stat st;
	unsigned char *data;
	size_t size, off;
	ssize_t n;
	int fd;

	*data_out = NULL;
	*size_out = 0;

	if (stat(path, &st) < 0 || !S_ISREG(st.st_mode) || st.st_size < 0)
		return 0;
	if ((uintmax_t)st.st_size > (uintmax_t)CLIP_DEFAULT_MAX_BYTES)
		return 0;

	size = (size_t)st.st_size;
	data = wmalloc(size == 0 ? 1 : size);
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		wfree(data);
		return 0;
	}

	off = 0;
	while (off < size) {
		n = read(fd, data + off, size - off);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			close(fd);
			wfree(data);
			return 0;
		}
		if (n == 0)
			break;
		off += (size_t)n;
	}
	close(fd);

	if (off != size) {
		wfree(data);
		return 0;
	}

	*data_out = data;
	*size_out = size;
	return 1;
}

static xcb_atom_t detect_image_target(const char *path,
				      const unsigned char *data, size_t size)
{
	const char *ext;
	static const unsigned char png_sig[8] = {
		0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'
	};

	if (size >= sizeof(png_sig) && memcmp(data, png_sig, sizeof(png_sig)) == 0)
		return atom_png;
	if (size >= 3 && data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff)
		return atom_jpeg;

	ext = strrchr(path, '.');
	if (!ext)
		return XCB_ATOM_NONE;
	if (strcasecmp(ext, ".png") == 0)
		return atom_png;
	if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0)
		return atom_jpeg;
	return XCB_ATOM_NONE;
}

static int publish_screenshot(const char *path)
{
	unsigned char *data = NULL;
	size_t size = 0;
	xcb_atom_t image_type;

	debug_log("publishing screenshot: %s", path ? path : "");

	if (!path || !*path || !read_file_bounded(path, &data, &size)) {
		fprintf(stderr, "%s: cannot read screenshot '%s'\n",
			prog, path ? path : "");
		return 0;
	}

	image_type = detect_image_target(path, data, size);
	if (verbose) {
		char *type_name = debug_atom_name(image_type);
		debug_log("loaded screenshot: %zu bytes, image target=%s",
			  size, type_name);
		wfree(type_name);
	}
	if (image_type == XCB_ATOM_NONE) {
		wfree(data);
		data = NULL;
		size = 0;
	}

	clear_buffer(&clipboard_buffer);
	clipboard_buffer.kind = CONTENT_SCREENSHOT;
	clipboard_buffer.data = data;
	clipboard_buffer.size = size;
	clipboard_buffer.type = image_type;
	clipboard_buffer.path = wstrdup(path);
	clipboard_buffer.uri = file_uri_from_path(path);

	return claim_selection(atom_clipboard, &clipboard_buffer,
			       XCB_TIME_CURRENT_TIME);
}

static char *screenshot_path_from_event(const char *body)
{
	WMPropList *pl, *key, *val;
	char *path = NULL;

	pl = WMCreatePropListFromDescription(body);
	if (!pl)
		return NULL;

	if (WMIsPLDictionary(pl)) {
		key = WMCreatePLString("path");
		val = WMGetFromPLDictionary(pl, key);
		WMReleasePropList(key);
		if (val && WMIsPLString(val))
			path = wstrdup(WMGetFromPLString(val));
	}

	WMReleasePropList(pl);
	return path;
}

static int subscribe_screenshots(int fd)
{
	const char *events[] = { "screenshot" };

	return wIPCSendStringArray(fd, WIPC_SUBSCRIBE, events, 1) == 0;
}

static int connect_ipc(void)
{
	char *path;
	int fd;

	path = wIPCSocketPath(NULL);
	debug_log("connecting to IPC socket: %s", path);
	fd = wIPCConnect(path);
	if (fd < 0) {
		debug_log("IPC connect failed: %s", strerror(errno));
		wfree(path);
		return -1;
	}
	wfree(path);

	if (!subscribe_screenshots(fd)) {
		debug_log("IPC screenshot subscription failed: %s", strerror(errno));
		close(fd);
		return -1;
	}
	debug_log("subscribed to screenshot IPC events");
	return fd;
}

static int handle_ipc_frame(int fd)
{
	uint32_t type, len;
	char *body, *path;

	if (wIPCRecv(fd, &type, &body, &len) < 0) {
		debug_log("IPC read failed: %s", strerror(errno));
		return 0;
	}

	if ((type & WIPC_EVENT_BIT) &&
	    ((type & ~WIPC_EVENT_BIT) == WIPC_EV_SCREENSHOT)) {
		path = screenshot_path_from_event(body);
		if (path) {
			debug_log("received screenshot IPC event: %s", path);
			publish_screenshot(path);
			wfree(path);
		} else {
			debug_log("received screenshot IPC event without a valid path");
		}
	}

	wfree(body);
	return 1;
}

static void intern_atoms(void)
{
	struct {
		const char *name;
		xcb_atom_t *out;
	} list[] = {
		{ "CLIPBOARD",                   &atom_clipboard },
		{ "TARGETS",                     &atom_targets },
		{ "INCR",                        &atom_incr },
		{ "UTF8_STRING",                 &atom_utf8 },
		{ "TEXT",                        &atom_text },
		{ "COMPOUND_TEXT",               &atom_compound_text },
		{ "image/png",                   &atom_png },
		{ "image/jpeg",                  &atom_jpeg },
		{ "text/uri-list",               &atom_uri_list },
		{ "text/plain",                  &atom_text_plain },
		{ "text/plain;charset=utf-8",    &atom_text_plain_utf8 },
		{ "WMCLIPD_SELECTION",           &atom_wmclipd_property },
	};
	xcb_intern_atom_cookie_t cookies[sizeof(list) / sizeof(list[0])];
	xcb_intern_atom_reply_t *rep;
	size_t i;

	for (i = 0; i < sizeof(list) / sizeof(list[0]); i++)
		cookies[i] = xcb_intern_atom(xc, 0,
					     strlen(list[i].name),
					     list[i].name);

	for (i = 0; i < sizeof(list) / sizeof(list[0]); i++) {
		rep = xcb_intern_atom_reply(xc, cookies[i], NULL);
		if (rep) {
			*list[i].out = rep->atom;
			free(rep);
		}
	}

	atom_primary = XCB_ATOM_PRIMARY;
}

static int init_x11(void)
{
	int screen_num;
	xcb_screen_iterator_t iter;
	xcb_screen_t *screen;
	uint32_t mask;

	xc = xcb_connect(NULL, &screen_num);
	if (xcb_connection_has_error(xc)) {
		fprintf(stderr, "%s: cannot open X display\n", prog);
		xcb_disconnect(xc);
		xc = NULL;
		return 0;
	}

	iter = xcb_setup_roots_iterator(xcb_get_setup(xc));
	for (; screen_num > 0; screen_num--)
		xcb_screen_next(&iter);
	screen = iter.data;
	root_window = screen->root;

	intern_atoms();

	owner_window = xcb_generate_id(xc);
	xcb_create_window(xc, XCB_COPY_FROM_PARENT, owner_window, root_window,
			 -1, -1, 1, 1, 0, XCB_WINDOW_CLASS_COPY_FROM_PARENT,
			 XCB_COPY_FROM_PARENT, 0, NULL);
	mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
	xcb_change_window_attributes(xc, owner_window, XCB_CW_EVENT_MASK,
				     &mask);
	xcb_flush(xc);
	debug_log("opened X display, owner window=0x%x",
		  (unsigned int)owner_window);
	return 1;
}

static int init_clipboard_monitor(void)
{
	xcb_xfixes_query_version_cookie_t vck;
	xcb_xfixes_query_version_reply_t *vrep;
	const xcb_query_extension_reply_t *ext;
	uint32_t mask;
	xcb_window_t clipboard_owner;
	xcb_window_t primary_owner;

	ext = xcb_get_extension_data(xc, &xcb_xfixes_id);
	if (!ext || !ext->present) {
		fprintf(stderr, "%s: XFixes extension is required for --clipboard\n", prog);
		return 0;
	}
	xfixes_first_event = ext->first_event;

	vck = xcb_xfixes_query_version(xc, XCB_XFIXES_MAJOR_VERSION,
				      XCB_XFIXES_MINOR_VERSION);
	vrep = xcb_xfixes_query_version_reply(xc, vck, NULL);
	if (!vrep) {
		fprintf(stderr, "%s: XFixes extension is required for --clipboard\n", prog);
		return 0;
	}
	free(vrep);

	mask = XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER |
	       XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY |
	       XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE;

	xcb_xfixes_select_selection_input(xc, root_window, atom_clipboard, mask);
	xcb_xfixes_select_selection_input(xc, root_window, atom_primary, mask);

	clipboard_owner = selection_owner(atom_clipboard);
	primary_owner = selection_owner(atom_primary);

	if (clipboard_owner != 0)
		capture_selection(atom_clipboard, &clipboard_buffer,
				  XCB_TIME_CURRENT_TIME, 1);
	if (primary_owner != 0 &&
	    capture_selection(atom_primary, &primary_buffer,
			      XCB_TIME_CURRENT_TIME, 0) &&
	    clipboard_owner == 0) {
		mirror_primary_to_clipboard(XCB_TIME_CURRENT_TIME,
					    "clipboard empty at startup", 1);
	}

	xcb_flush(xc);
	debug_log("monitoring CLIPBOARD and PRIMARY with XFixes");
	return 1;
}

static int parse_args(int argc, char **argv)
{
	enum {
		OPT_CLIPBOARD = 256,
		OPT_SCREENSHOTS,
		OPT_DEBUG,
	};
	int ch;
	struct option longopts[] = {
		{ "clipboard",   no_argument, NULL, OPT_CLIPBOARD },
		{ "screenshots", no_argument, NULL, OPT_SCREENSHOTS },
		{ "screenshot",  no_argument, NULL, OPT_SCREENSHOTS },
		{ "debug",       no_argument, NULL, OPT_DEBUG },
		{ "help",        no_argument, NULL, 'h' },
		{ "version",     no_argument, NULL, 'v' },
		{ NULL,          0,           NULL, 0 }
	};

	while ((ch = getopt_long(argc, argv, "hv", longopts, NULL)) != -1) {
		switch (ch) {
		case OPT_CLIPBOARD:
			run_clipboard = 1;
			break;
		case OPT_SCREENSHOTS:
			run_screenshots = 1;
			break;
		case OPT_DEBUG:
			verbose = 1;
			break;
		case 'h':
			usage(stdout);
			return 1;
		case 'v':
			printf("wmclipd version %s\n", VERSION);
			return 1;
		default:
			usage(stderr);
			return -1;
		}
	}

	if (!run_clipboard && !run_screenshots) {
		usage(stderr);
		return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct pollfd fds[2];
	int ipc_fd = -1;
	int parse_rc, nfds, x_index, ipc_index, rc, timeout;
	long long next_ipc_connect = 0, now;
	xcb_generic_event_t *ev;

	prog = argv[0];
	parse_rc = parse_args(argc, argv);
	if (parse_rc > 0)
		return 0;
	if (parse_rc < 0)
		return 2;

	debug_log("starting: clipboard=%s screenshots=%s",
		  run_clipboard ? "yes" : "no",
		  run_screenshots ? "yes" : "no");

	if (!init_x11())
		return 1;
	if (run_clipboard && !init_clipboard_monitor())
		return 1;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGPIPE, SIG_IGN);

	while (running) {
		while ((ev = xcb_poll_for_event(xc)) != NULL) {
			handle_x_event(ev);
			free(ev);
		}
		if (xcb_connection_has_error(xc))
			break;

		now = clip_monotonic_ms();
		if (run_screenshots && ipc_fd < 0 && now >= next_ipc_connect) {
			ipc_fd = connect_ipc();
			if (ipc_fd < 0)
				next_ipc_connect = now + IPC_RECONNECT_MS;
		}

		nfds = 0;
		x_index = nfds;
		fds[nfds].fd = xcb_get_file_descriptor(xc);
		fds[nfds].events = POLLIN;
		fds[nfds].revents = 0;
		nfds++;

		ipc_index = -1;
		if (ipc_fd >= 0) {
			ipc_index = nfds;
			fds[nfds].fd = ipc_fd;
			fds[nfds].events = POLLIN;
			fds[nfds].revents = 0;
			nfds++;
		}

		timeout = -1;
		if (run_screenshots && ipc_fd < 0) {
			now = clip_monotonic_ms();
			timeout = next_ipc_connect > now ?
				(int)(next_ipc_connect - now) : 0;
		}

		rc = poll(fds, nfds, timeout);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			break;
		}

		if (fds[x_index].revents & POLLIN) {
			while ((ev = xcb_poll_for_event(xc)) != NULL) {
				handle_x_event(ev);
				free(ev);
			}
		}
		if (fds[x_index].revents & (POLLERR | POLLHUP | POLLNVAL))
			break;

		if (ipc_index >= 0 && fds[ipc_index].revents) {
			if (fds[ipc_index].revents & POLLIN) {
				if (!handle_ipc_frame(ipc_fd)) {
					debug_log("IPC disconnected; will reconnect");
					close(ipc_fd);
					ipc_fd = -1;
					next_ipc_connect = clip_monotonic_ms() + IPC_RECONNECT_MS;
				}
			} else if (fds[ipc_index].revents & (POLLERR | POLLHUP | POLLNVAL)) {
				debug_log("IPC poll error; will reconnect");
				close(ipc_fd);
				ipc_fd = -1;
				next_ipc_connect = clip_monotonic_ms() + IPC_RECONNECT_MS;
			}
		}
	}

	debug_log("stopping");
	if (ipc_fd >= 0)
		close(ipc_fd);
	clip_incr_send_cancel(xc, &incr);
	clear_buffer(&clipboard_buffer);
	clear_buffer(&primary_buffer);
	if (xc)
		xcb_disconnect(xc);
	return 0;
}
