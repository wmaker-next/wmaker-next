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

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>

#include <WINGs/WUtil.h>
#include <WINGs/WIPC.h>
#include <WINGs/WXProp.h>

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
	Atom type;
	char *path;
	char *uri;
	Time timestamp;
	int owns;
} ClipBuffer;

static const char *prog;
static Display *dpy;
static Window owner_window;
static int xfixes_event_base;
static int run_clipboard;
static int run_screenshots;
static int verbose;
static volatile sig_atomic_t running = 1;

static Atom atom_clipboard;
static Atom atom_primary;
static Atom atom_targets;
static Atom atom_incr;
static Atom atom_utf8;
static Atom atom_text;
static Atom atom_compound_text;
static Atom atom_png;
static Atom atom_jpeg;
static Atom atom_uri_list;
static Atom atom_text_plain;
static Atom atom_text_plain_utf8;
static Atom atom_wmclipd_property;

static ClipBuffer clipboard_buffer;
static ClipBuffer primary_buffer;
static ClipIncrSend incr;
static ClipBuffer *incr_source;

static void handle_selection_request(XSelectionRequestEvent *req);
static void handle_selection_clear(XSelectionClearEvent *ev);
static void handle_property_notify(XPropertyEvent *ev);
static void handle_destroy_notify(XDestroyWindowEvent *ev);

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

static int ignore_x_errors(Display *display, XErrorEvent *event)
{
	(void)display;
	(void)event;
	return 0;
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

static char *debug_atom_name(Atom atom)
{
	char *name, *copy;

	if (atom == None)
		return wstrdup("None");

	name = XGetAtomName(dpy, atom);
	copy = wstrdup(name ? name : "unknown");
	if (name)
		XFree(name);
	return copy;
}

static const char *debug_selection_name(Atom selection)
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

static int is_text_atom(Atom atom)
{
	return atom == atom_utf8 ||
	       atom == XA_STRING ||
	       atom == atom_text ||
	       atom == atom_compound_text ||
	       atom == atom_text_plain ||
	       atom == atom_text_plain_utf8;
}

static int is_image_atom(Atom atom)
{
	return atom == atom_png || atom == atom_jpeg;
}

static int selection_type_matches_target(Atom target, Atom actual)
{
	if (actual == target)
		return 1;
	return is_text_atom(target) && is_text_atom(actual);
}

static void clear_buffer(ClipBuffer *buf)
{
	if (incr.active && incr_source == buf) {
		clip_incr_send_cancel(dpy, &incr);
		incr_source = NULL;
	}

	wfree(buf->data);
	wfree(buf->path);
	wfree(buf->uri);
	memset(buf, 0, sizeof(*buf));
}

static void dispatch_during_capture(XEvent *ev, void *udata)
{
	(void)udata;
	if (ev->type == SelectionRequest)
		handle_selection_request(&ev->xselectionrequest);
	else if (ev->type == SelectionClear)
		handle_selection_clear(&ev->xselectionclear);
	else if (ev->type == PropertyNotify)
		handle_property_notify(&ev->xproperty);
	else if (ev->type == DestroyNotify)
		handle_destroy_notify(&ev->xdestroywindow);
}

static int wait_selection_notify(Atom selection, Atom property, XSelectionEvent *out)
{
	XEvent ev;
	long long deadline = clip_monotonic_ms() + SELECTION_WAIT_MS;
	int wait_ms;

	while (running) {
		wait_ms = (int)(deadline - clip_monotonic_ms());
		if (wait_ms <= 0)
			return 0;
		if (!clip_wait_event(dpy, &ev, wait_ms))
			return 0;
		if (ev.type == SelectionNotify &&
		    ev.xselection.selection == selection &&
		    (ev.xselection.property == property || ev.xselection.property == None)) {
			*out = ev.xselection;
			return 1;
		}
		dispatch_during_capture(&ev, NULL);
	}
	return 0;
}

static int query_targets(Atom selection, Time timestamp, Atom **targets_out,
			 unsigned long *count_out)
{
	XSelectionEvent sev;
	Atom type;
	int format;
	unsigned long nitems, bytes_after;
	unsigned char *prop = NULL;
	Atom *copy;

	*targets_out = NULL;
	*count_out = 0;

	XConvertSelection(dpy, selection, atom_targets, atom_wmclipd_property,
			  owner_window, timestamp);
	XFlush(dpy);

	if (!wait_selection_notify(selection, atom_wmclipd_property, &sev) ||
	    sev.property == None) {
		return 0;
	}

	if (XGetWindowProperty(dpy, owner_window, atom_wmclipd_property,
			       0, 1024, True, XA_ATOM, &type, &format,
			       &nitems, &bytes_after, &prop) != Success) {
		return 0;
	}

	if (type != XA_ATOM || format != 32 || nitems == 0 || bytes_after != 0) {
		if (prop)
			XFree(prop);
		return 0;
	}

	copy = wmalloc(sizeof(Atom) * (size_t)nitems);
	memcpy(copy, prop, sizeof(Atom) * (size_t)nitems);
	XFree(prop);

	*targets_out = copy;
	*count_out = nitems;
	return 1;
}

static int target_in_list(const Atom *targets, unsigned long count, Atom atom)
{
	unsigned long i;

	for (i = 0; i < count; i++) {
		if (targets[i] == atom)
			return 1;
	}
	return 0;
}

static Atom choose_preferred_target(const Atom *targets, unsigned long count)
{
	Atom prefs[6];
	size_t i;

	prefs[0] = atom_png;
	prefs[1] = atom_jpeg;
	prefs[2] = atom_utf8;
	prefs[3] = XA_STRING;
	prefs[4] = atom_text;
	prefs[5] = atom_compound_text;

	for (i = 0; i < sizeof(prefs) / sizeof(prefs[0]); i++) {
		if (target_in_list(targets, count, prefs[i]))
			return prefs[i];
	}
	return None;
}

static int request_selection_data(Atom selection, Atom target, Time timestamp,
				  Atom *type_out, unsigned char **data_out,
				  size_t *len_out)
{
	XSelectionEvent sev;
	unsigned char *data;
	Atom type;
	size_t len;

	*data_out = NULL;
	*len_out = 0;
	*type_out = None;

	XDeleteProperty(dpy, owner_window, atom_wmclipd_property);
	XConvertSelection(dpy, selection, target, atom_wmclipd_property,
			  owner_window, timestamp);
	XFlush(dpy);

	if (!wait_selection_notify(selection, atom_wmclipd_property, &sev) ||
	    sev.property == None) {
		return 0;
	}

	data = clip_retrieve_property(dpy, owner_window, atom_wmclipd_property,
					 atom_incr, CLIP_DEFAULT_MAX_BYTES,
					 INCR_WAIT_MS, dispatch_during_capture,
					 NULL, &type, &len);
	if (!data)
		return 0;
	if (!selection_type_matches_target(target, type)) {
		free(data);
		return 0;
	}

	*type_out = type == None ? target : type;
	*data_out = data;
	*len_out = len;
	return 1;
}

static int claim_selection(Atom selection, ClipBuffer *buf, Time timestamp)
{
	XSetSelectionOwner(dpy, selection, owner_window, timestamp);
	XFlush(dpy);

	if (XGetSelectionOwner(dpy, selection) != owner_window) {
		buf->owns = 0;
		debug_log("failed to own %s", debug_selection_name(selection));
		return 0;
	}

	buf->owns = 1;
	buf->timestamp = timestamp;
	debug_log("owning %s", debug_selection_name(selection));
	return 1;
}

static int capture_selection(Atom selection, ClipBuffer *buf, Time timestamp,
			     int claim_after_capture)
{
	Atom *targets = NULL;
	unsigned long target_count = 0;
	Atom target, type;
	unsigned char *data = NULL;
	size_t size = 0;
	Atom fallbacks[6];
	size_t i;
	int ok = 0;

	debug_log("capturing %s", debug_selection_name(selection));

	if (XGetSelectionOwner(dpy, selection) == owner_window)
		return 1;

	target = None;
	if (query_targets(selection, timestamp, &targets, &target_count))
		target = choose_preferred_target(targets, target_count);
	wfree(targets);

	if (target != None)
		ok = request_selection_data(selection, target, timestamp, &type, &data, &size);

	if (!ok) {
		fallbacks[0] = atom_png;
		fallbacks[1] = atom_jpeg;
		fallbacks[2] = atom_utf8;
		fallbacks[3] = XA_STRING;
		fallbacks[4] = atom_text;
		fallbacks[5] = atom_compound_text;

		for (i = 0; i < sizeof(fallbacks) / sizeof(fallbacks[0]); i++) {
			if (fallbacks[i] == target)
				continue;
			if (request_selection_data(selection, fallbacks[i], timestamp,
						   &type, &data, &size)) {
				ok = 1;
				break;
			}
		}
	}

	if (!ok) {
		debug_log("capture failed for %s", debug_selection_name(selection));
		if (buffer_has_payload(buf))
			claim_selection(selection, buf, timestamp);
		return 0;
	}

	clear_buffer(buf);
	buf->kind = CONTENT_CAPTURED;
	buf->data = data;
	buf->size = size;
	buf->type = type == None ? target : type;
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
		debug_log("cached %s without owning it", debug_selection_name(selection));
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

static int mirror_primary_to_clipboard(Time timestamp, const char *reason,
				       int images_only)
{
	if (images_only && !is_image_atom(primary_buffer.type))
		return 0;
	if (!copy_captured_payload(&clipboard_buffer, &primary_buffer))
		return 0;

	debug_log("mirroring PRIMARY to CLIPBOARD (%s): %zu bytes",
		  reason, clipboard_buffer.size);
	return claim_selection(atom_clipboard, &clipboard_buffer, timestamp);
}

static int mirror_clipboard_text_to_primary(Time timestamp, const char *reason)
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

static void add_atom_unique(Atom *atoms, int *count, Atom atom)
{
	int i;

	if (atom == None)
		return;
	for (i = 0; i < *count; i++) {
		if (atoms[i] == atom)
			return;
	}
	atoms[(*count)++] = atom;
}

static int make_targets(const ClipBuffer *buf, Atom *atoms)
{
	int count = 0;

	add_atom_unique(atoms, &count, atom_targets);

	if (buf->kind == CONTENT_SCREENSHOT) {
		add_atom_unique(atoms, &count, buf->type);
		add_atom_unique(atoms, &count, atom_uri_list);
		add_atom_unique(atoms, &count, atom_utf8);
		add_atom_unique(atoms, &count, XA_STRING);
		add_atom_unique(atoms, &count, atom_text_plain);
		add_atom_unique(atoms, &count, atom_text_plain_utf8);
		return count;
	}

	if (buf->kind == CONTENT_CAPTURED) {
		add_atom_unique(atoms, &count, buf->type);
		if (is_text_atom(buf->type)) {
			add_atom_unique(atoms, &count, atom_utf8);
			add_atom_unique(atoms, &count, XA_STRING);
			add_atom_unique(atoms, &count, atom_text);
			add_atom_unique(atoms, &count, atom_text_plain);
			add_atom_unique(atoms, &count, atom_text_plain_utf8);
		}
	}

	return count;
}

static int payload_for_request(ClipBuffer *buf, Atom target,
			       const unsigned char **data, size_t *size,
			       Atom *type)
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
			*type = target == XA_STRING ? XA_STRING : target;
			return 1;
		}
		return 0;
	}

	if (buf->kind == CONTENT_CAPTURED) {
		if (target == buf->type || (is_text_atom(target) && is_text_atom(buf->type))) {
			*data = buf->data;
			*size = buf->size;
			*type = target == buf->type ? buf->type : target;
			return 1;
		}
	}

	return 0;
}

static void send_selection_notify(XSelectionRequestEvent *req, Atom property)
{
	WXSendSelectionNotify(dpy, req, property);
}

static void handle_selection_request(XSelectionRequestEvent *req)
{
	ClipBuffer *buf;
	Atom property, targets[12], type;
	const unsigned char *data;
	size_t size;
	int count;

	property = req->property == None ? req->target : req->property;
	if (verbose) {
		char *target_name = debug_atom_name(req->target);
		debug_log("selection request: %s target=%s requestor=0x%lx",
			  debug_selection_name(req->selection), target_name,
			  (unsigned long)req->requestor);
		wfree(target_name);
	}
	buf = NULL;
	if (req->selection == atom_clipboard)
		buf = &clipboard_buffer;
	else if (req->selection == atom_primary)
		buf = &primary_buffer;

	if (!buf || !buf->owns || !buffer_has_payload(buf)) {
		send_selection_notify(req, None);
		return;
	}

	if (req->target == atom_targets) {
		count = make_targets(buf, targets);
		XChangeProperty(dpy, req->requestor, property, XA_ATOM, 32,
				PropModeReplace, (unsigned char *)targets, count);
		send_selection_notify(req, property);
		return;
	}

	if (!payload_for_request(buf, req->target, &data, &size, &type)) {
		send_selection_notify(req, None);
		return;
	}

	if (size > CLIP_DEFAULT_DIRECT_LIMIT) {
		if (!clip_incr_send_start(dpy, &incr, req, atom_incr, type,
					   data, size, CLIP_DEFAULT_CHUNK_SIZE)) {
			send_selection_notify(req, None);
			return;
		}
		incr_source = buf;
		debug_log("starting INCR transfer to 0x%lx: %zu bytes",
			  (unsigned long)req->requestor, size);
		send_selection_notify(req, property);
		return;
	}

	XChangeProperty(dpy, req->requestor, property, type, 8,
			PropModeReplace, data, (int)size);
	send_selection_notify(req, property);
}

static void handle_selection_clear(XSelectionClearEvent *ev)
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

static void handle_property_notify(XPropertyEvent *ev)
{
	if (clip_incr_send_property_notify(dpy, &incr, ev)) {
		if (incr.active)
			debug_log("sent INCR chunk to 0x%lx: %zu/%zu bytes",
				  (unsigned long)incr.requestor, incr.offset, incr.size);
		else
			incr_source = NULL;
	}
}

static void handle_destroy_notify(XDestroyWindowEvent *ev)
{
	if (clip_incr_send_destroy_notify(dpy, &incr, ev)) {
		debug_log("cancelled INCR transfer: requestor 0x%lx was destroyed",
			  (unsigned long)ev->window);
		incr_source = NULL;
	}
}

static void handle_selection_change(XFixesSelectionNotifyEvent *ev)
{
	ClipBuffer *buf;
	Window source_owner, clipboard_owner;
	int captured;

	if (ev->selection == atom_clipboard)
		buf = &clipboard_buffer;
	else if (ev->selection == atom_primary)
		buf = &primary_buffer;
	else
		return;

	if (ev->owner == owner_window)
		return;

	debug_log("selection changed: %s owner=0x%lx",
		  debug_selection_name(ev->selection), (unsigned long)ev->owner);
	buf->owns = 0;
	if (ev->owner == None) {
		if (ev->selection == atom_clipboard && !buffer_has_payload(buf) &&
		    mirror_primary_to_clipboard(ev->timestamp, "clipboard owner cleared", 1))
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
		clipboard_owner = XGetSelectionOwner(dpy, atom_clipboard);
		if (clipboard_owner == source_owner) {
			mirror_primary_to_clipboard(ev->timestamp, "same source owner", 0);
		} else if (clipboard_owner == None) {
			mirror_primary_to_clipboard(ev->timestamp, "clipboard empty", 1);
		}
	} else {
		captured = capture_selection(ev->selection, buf, ev->timestamp, 1);
		if (captured) {
			Window primary_owner = XGetSelectionOwner(dpy, atom_primary);

			if (primary_owner == source_owner)
				mirror_clipboard_text_to_primary(ev->timestamp,
								 "same source owner");
		}
	}
}

static void handle_x_event(XEvent *ev)
{
	if (run_clipboard &&
	    ev->type == xfixes_event_base + XFixesSelectionNotify) {
		handle_selection_change((XFixesSelectionNotifyEvent *)ev);
		return;
	}

	switch (ev->type) {
	case SelectionRequest:
		handle_selection_request(&ev->xselectionrequest);
		break;
	case SelectionClear:
		handle_selection_clear(&ev->xselectionclear);
		break;
	case PropertyNotify:
		handle_property_notify(&ev->xproperty);
		break;
	case DestroyNotify:
		handle_destroy_notify(&ev->xdestroywindow);
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

static Atom detect_image_target(const char *path, const unsigned char *data, size_t size)
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
		return None;
	if (strcasecmp(ext, ".png") == 0)
		return atom_png;
	if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0)
		return atom_jpeg;
	return None;
}

static int publish_screenshot(const char *path)
{
	unsigned char *data = NULL;
	size_t size = 0;
	Atom image_type;

	debug_log("publishing screenshot: %s", path ? path : "");

	if (!path || !*path || !read_file_bounded(path, &data, &size)) {
		fprintf(stderr, "%s: cannot read screenshot '%s'\n",
			prog, path ? path : "");
		return 0;
	}

	image_type = detect_image_target(path, data, size);
	if (verbose) {
		char *type_name = debug_atom_name(image_type);
		debug_log("loaded screenshot: %zu bytes, image target=%s", size, type_name);
		wfree(type_name);
	}
	if (image_type == None) {
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

	return claim_selection(atom_clipboard, &clipboard_buffer, CurrentTime);
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

static int init_x11(void)
{
	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "%s: cannot open X display\n", prog);
		return 0;
	}

	XSetErrorHandler(ignore_x_errors);

	atom_clipboard = XInternAtom(dpy, "CLIPBOARD", False);
	atom_primary = XA_PRIMARY;
	atom_targets = XInternAtom(dpy, "TARGETS", False);
	atom_incr = XInternAtom(dpy, "INCR", False);
	atom_utf8 = XInternAtom(dpy, "UTF8_STRING", False);
	atom_text = XInternAtom(dpy, "TEXT", False);
	atom_compound_text = XInternAtom(dpy, "COMPOUND_TEXT", False);
	atom_png = XInternAtom(dpy, "image/png", False);
	atom_jpeg = XInternAtom(dpy, "image/jpeg", False);
	atom_uri_list = XInternAtom(dpy, "text/uri-list", False);
	atom_text_plain = XInternAtom(dpy, "text/plain", False);
	atom_text_plain_utf8 = XInternAtom(dpy, "text/plain;charset=utf-8", False);
	atom_wmclipd_property = XInternAtom(dpy, "WMCLIPD_SELECTION", False);

	owner_window = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy),
					   -1, -1, 1, 1, 0, 0, 0);
	XSelectInput(dpy, owner_window, PropertyChangeMask);
	XFlush(dpy);
	debug_log("opened X display %s, owner window=0x%lx",
		  DisplayString(dpy), (unsigned long)owner_window);
	return 1;
}

static int init_clipboard_monitor(void)
{
	int error_base;
	unsigned long mask;
	Window clipboard_owner;
	Window primary_owner;

	if (!XFixesQueryExtension(dpy, &xfixes_event_base, &error_base)) {
		fprintf(stderr, "%s: XFixes extension is required for --clipboard\n", prog);
		return 0;
	}

	mask = XFixesSetSelectionOwnerNotifyMask |
	       XFixesSelectionWindowDestroyNotifyMask |
	       XFixesSelectionClientCloseNotifyMask;

	XFixesSelectSelectionInput(dpy, DefaultRootWindow(dpy), atom_clipboard, mask);
	XFixesSelectSelectionInput(dpy, DefaultRootWindow(dpy), atom_primary, mask);

	clipboard_owner = XGetSelectionOwner(dpy, atom_clipboard);
	primary_owner = XGetSelectionOwner(dpy, atom_primary);

	if (clipboard_owner != None)
		capture_selection(atom_clipboard, &clipboard_buffer, CurrentTime, 1);
	if (primary_owner != None &&
	    capture_selection(atom_primary, &primary_buffer, CurrentTime, 0) &&
	    clipboard_owner == None) {
		mirror_primary_to_clipboard(CurrentTime,
					    "clipboard empty at startup", 1);
	}

	XFlush(dpy);
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
	XEvent ev;

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
		while (XPending(dpy)) {
			XNextEvent(dpy, &ev);
			handle_x_event(&ev);
		}

		now = clip_monotonic_ms();
		if (run_screenshots && ipc_fd < 0 && now >= next_ipc_connect) {
			ipc_fd = connect_ipc();
			if (ipc_fd < 0)
				next_ipc_connect = now + IPC_RECONNECT_MS;
		}

		nfds = 0;
		x_index = nfds;
		fds[nfds].fd = ConnectionNumber(dpy);
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
			timeout = next_ipc_connect > now ? (int)(next_ipc_connect - now) : 0;
		}

		rc = poll(fds, nfds, timeout);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			break;
		}

		if (fds[x_index].revents & POLLIN) {
			while (XPending(dpy)) {
				XNextEvent(dpy, &ev);
				handle_x_event(&ev);
			}
		}

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
	clip_incr_send_cancel(dpy, &incr);
	clear_buffer(&clipboard_buffer);
	clear_buffer(&primary_buffer);
	if (dpy)
		XCloseDisplay(dpy);
	return 0;
}
