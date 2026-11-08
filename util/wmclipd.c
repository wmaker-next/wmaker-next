/*
 *  wmclipd - X11 clipboard daemon with Window Maker screenshot IPC support
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
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <xcb/xcb.h>
#include <xcb/xfixes.h>

#include <xkbcommon/xkbcommon-keysyms.h>
#include <xcb/xtest.h>

#include <WINGs/WUtil.h>
#include <WINGs/WIPC.h>
#include <WINGs/WINGs.h>
#include <WINGs/WXProp.h>
#include <WINGs/WINGsP.h>
#include <wraster.h>

#include "config.h"
#include "clip_common.h"
#include "xcconn.h"

#define IPC_RECONNECT_MS   1000
#define SELECTION_WAIT_MS  1000
#define INCR_WAIT_MS       5000
#define PRIMARY_DEBOUNCE_MS 300

#define CLIP_HISTORY_DEFAULT_CAPACITY 10
#define CLIP_HISTORY_MAX_BYTES   ((size_t)16 * 1024 * 1024)
#define CLIP_TTL_MIN_SECONDS     5
#define CLIP_TTL_MAX_SECONDS     (365L * 24 * 60 * 60)
#define CLIP_POPUP_TICK          "wmclipd-popup"
#define CLIP_POPUP_POLL_MS       250
#define CLIP_POPUP_PREVIEW_LEN   64
#define CLIP_POPUP_PIN_CHAR      "\xF0\x9F\x93\x8C"  /* U+1F4CC pushpin */
#define CLIP_POPUP_PIN_LEN       4

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
	int owns;
} ClipBuffer;

typedef struct ClipHistoryEntry {
	ContentKind kind;
	unsigned char *data;
	size_t size;
	xcb_atom_t type;
	time_t wall_time;
	long long mono_ms;
	char *text_preview;
	char *image_path;
	char *uri;
	int pinned;
	int width;
	int height;
} ClipHistoryEntry;

typedef struct {
	ClipHistoryEntry **entries;
	int count;
	int capacity;
	int pinned_count;
} ClipHistory;

static const char *prog;
static xcb_connection_t *xc;
static int screen_num = 0;
static xcb_window_t owner_window;
static xcb_window_t root_window;
static int xfixes_first_event;
static int run_clipboard;
static int run_screenshots;
static int history_capacity = CLIP_HISTORY_DEFAULT_CAPACITY;
static int history_enabled;
static int persist_enabled;
static char *history_file_override;
static long long ttl_seconds;
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

static xcb_timestamp_t primary_pending_ts;
static long long primary_pending_deadline;

static ClipHistory history;
static int popup_active;
/* pending popup tick, coalesced because keyboard auto-repeat fires the
 * binding repeatedly while the chord is held */
static int popup_tick_pending;
static bool popup_tick_keyed;
/* end of the last popup, used to spot auto-repeat echoes of the same
 * gesture that arrive after it closed */
static long long popup_last_close_ms;
/* set when history_add_from_buffer mutates the ring while the popup
 * loop is running, so it can rebuild its list */
static int popup_history_dirty;
static int popup_ipc_fd = -1;
static WMScreen *popup_wscr;
static WMWindow *popup_win;
static WMList *popup_list;
static int popup_mapped;
static WMFont *popup_bold_font;
static WMFont *popup_norm_font;
static WMColor *popup_dim_color;
static WMColor *popup_sel_color;
static WMColor *popup_bg_color;
static int popup_item_h;
static int popup_icon_size;
static WMHashTable *popup_thumbs;

static void handle_selection_request(xcb_selection_request_event_t *req);
static void handle_selection_clear(xcb_selection_clear_event_t *ev);
static void handle_property_notify(xcb_property_notify_event_t *ev);
static void handle_destroy_notify(xcb_destroy_notify_event_t *ev);
static void show_clipboard_popup(bool keyed);
static void history_add_from_buffer(const ClipBuffer *buf);
static void history_evict_over_capacity(void);
static void flush_primary_pending(void);

static void usage(FILE *out)
{
	fprintf(out,
		"Usage: %s [--clipboard] [--screenshots] [--history N] [--ttl DURATION] [--persist]\n"
		"          [--history-file PATH] [--debug]\n"
		"       %s --help|-h\n"
		"       %s --version|-v\n"
		"Options:\n"
		"  --clipboard    Run as an X11 clipboard manager.\n"
		"  --debug        Print runtime diagnostics to stderr.\n"
		"  --help, -h     Show this message.\n"
		"  --history N    Keep the last N clipboard entries in a history ring (default %d, 0 disables).\n"
		"  --history-file PATH\n"
		"                 Store the persistent history in PATH instead of the default\n"
		"                 location. Implies --persist.\n"
		"  --persist      Save the history ring to disk on clean exit (SIGTERM/SIGINT\n"
		"                 or X disconnect) and restore it on the next start. History\n"
		"                 can contain secrets, so this is opt-in. Default file:\n"
		"                 $XDG_STATE_HOME/WindowMaker/wmclipd-history, mode 0600.\n"
		"  --screenshots  Subscribe to Window Maker screenshot IPC events and publish them to CLIPBOARD.\n"
		"                 The singular --screenshot is accepted as an alias.\n"
		"  --ttl DURATION Expire history entries after DURATION, e.g. 30s, 10m, 2h, 1d\n"
		"                 (combinations like 1h30m are accepted, a bare number means\n"
		"                 seconds; minimum 5s, maximum 365d). 0 disables (default).\n"
		"  --version, -v  Print version information and exit.\n",
		prog, prog, prog, CLIP_HISTORY_DEFAULT_CAPACITY);
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

/* lost X connection: unwind through the graceful shutdown */
static void clip_dead_hook(xcb_connection_t *dconn)
{
	(void)dconn;

	debug_log("X connection lost");
	running = 0;
}

/* X errors are the only signal a requestor can give when it dies
 * between the attribute probe and the StructureNotify mask change */
static void clip_error_hook(xcb_connection_t *dconn, unsigned int error_code,
			    unsigned int major_opcode, unsigned int minor_opcode,
			    unsigned long resource_id, unsigned long serial)
{
	(void)dconn;
	(void)error_code;
	(void)major_opcode;
	(void)minor_opcode;
	(void)serial;

	if (clip_incr_send_handle_error(&incr, resource_id)) {
		incr_source = NULL;
		debug_log("cancelled INCR transfer: X error on requestor 0x%lx",
			  resource_id);
	}
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

	if (!ok || size == 0) {
		free(data);
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
		debug_log("cached %s without owning it",
			  debug_selection_name(selection));
	}
	history_add_from_buffer(buf);
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

/* --- History ring ----------------------------------------------------- */

static void history_free_entry(ClipHistoryEntry *e)
{
	WMPixmap *thumb;

	if (!e)
		return;
	if (popup_thumbs) {
		thumb = WMHashGet(popup_thumbs, (void *)(intptr_t)e);
		if (thumb) {
			WMReleasePixmap(thumb);
			WMHashRemove(popup_thumbs, (void *)(intptr_t)e);
		}
	}
	wfree(e->data);
	wfree(e->text_preview);
	wfree(e->image_path);
	wfree(e->uri);
	wfree(e);
}

/* true if utf8 starts with a combining mark (Mn/Mc/Me). Only the
 * dedicated combining blocks are checked. */
static int first_cp_is_combining(const char *utf8)
{
	unsigned char c = (unsigned char)utf8[0];
	uint32_t cp;

	if (c < 0x80)
		return 0;
	if ((c & 0xE0) == 0xC0)
		cp = ((uint32_t)(c & 0x1F) << 6) | (uint32_t)(utf8[1] & 0x3F);
	else if ((c & 0xF0) == 0xE0)
		cp = ((uint32_t)(c & 0x0F) << 12) |
		     ((uint32_t)(utf8[1] & 0x3F) << 6) |
		     (uint32_t)(utf8[2] & 0x3F);
	else if ((c & 0xF8) == 0xF0)
		cp = ((uint32_t)(c & 0x07) << 18) |
		     ((uint32_t)(utf8[1] & 0x3F) << 12) |
		     ((uint32_t)(utf8[2] & 0x3F) << 6) |
		     (uint32_t)(utf8[3] & 0x3F);
	else
		return 0;

	return (cp >= 0x0300 && cp <= 0x036F) ||  /* Combining Diacritical Marks */
	       (cp >= 0x1AB0 && cp <= 0x1AFF) ||  /* Comb. Diacritical Marks Extended */
	       (cp >= 0x1DC0 && cp <= 0x1DFF) ||  /* Comb. Diacritical Marks Supp. */
	       (cp >= 0x20D0 && cp <= 0x20FF) ||  /* Comb. Diacritical Marks for Symbols */
	       (cp >= 0xFE20 && cp <= 0xFE2F);    /* Combining Half Marks */
}

static char *make_text_preview(const unsigned char *data, size_t size)
{
	size_t i, len, out;
	char *preview;
	unsigned char c;

	if (!data || size == 0)
		return wstrdup("");

	len = size < CLIP_POPUP_PREVIEW_LEN ? size : CLIP_POPUP_PREVIEW_LEN;
	/* Truncate at a UTF-8 boundary: if the last bytes are a partial
	 * multi-byte sequence, drop it so pango_layout_set_text() does not
	 * receive invalid UTF-8. Walk backward skipping continuation
	 * bytes until a lead byte is found; if that lead byte needs more
	 * bytes than remain, drop it too. */
	if (len > 0 && (data[len - 1] & 0xC0) == 0x80) {
		size_t back = len;
		while (back > 0 && (data[back - 1] & 0xC0) == 0x80)
			back--;
		if (back > 0) {
			unsigned char lead = data[back - 1];
			size_t need;
			if (lead < 0x80)       need = 1;
			else if ((lead & 0xE0) == 0xC0) need = 2;
			else if ((lead & 0xF0) == 0xE0) need = 3;
			else if ((lead & 0xF8) == 0xF0) need = 4;
			else need = 1; /* invalid lead */
			if (len - (back - 1) < need)
				len = back - 1; /* sequence is incomplete */
		}
	}
	if (len == 0)
		len = size < CLIP_POPUP_PREVIEW_LEN ? size : CLIP_POPUP_PREVIEW_LEN;

	/* Validate the UTF-8 and replace any invalid byte with U+FFFD
	 * (REPLACEMENT CHARACTER) so pango never receives bad sequences. */
	preview = wmalloc(len * 3 + 7);	/* room for U+25CC, "...", NUL */
	out = 0;
	i = 0;
	while (i < len) {
		c = data[i];
		if (c < 0x80) {
			if (c == '\n' || c == '\r' || c == '\t' || c == '\0' ||
			    (c < 0x20 && c != 0))
				c = ' ';
			preview[out++] = (char)c;
			i++;
		} else if ((c & 0xE0) == 0xC0 && i + 1 < len &&
			   (data[i + 1] & 0xC0) == 0x80) {
			preview[out++] = (char)c;
			preview[out++] = (char)data[i + 1];
			i += 2;
		} else if ((c & 0xF0) == 0xE0 && i + 2 < len &&
			   (data[i + 1] & 0xC0) == 0x80 &&
			   (data[i + 2] & 0xC0) == 0x80) {
			preview[out++] = (char)c;
			preview[out++] = (char)data[i + 1];
			preview[out++] = (char)data[i + 2];
			i += 3;
		} else if ((c & 0xF8) == 0xF0 && i + 3 < len &&
			   (data[i + 1] & 0xC0) == 0x80 &&
			   (data[i + 2] & 0xC0) == 0x80 &&
			   (data[i + 3] & 0xC0) == 0x80) {
			preview[out++] = (char)c;
			preview[out++] = (char)data[i + 1];
			preview[out++] = (char)data[i + 2];
			preview[out++] = (char)data[i + 3];
			i += 4;
		} else {
			/* invalid lead or truncated sequence: U+FFFD */
			preview[out++] = (char)0xEF;
			preview[out++] = (char)0xBF;
			preview[out++] = (char)0xBD;
			i++;
		}
	}
	if (size > len)
		memcpy(preview + out, "...", 3), out += 3;
	preview[out] = '\0';

	/* Prepend U+25CC DOTTED CIRCLE if the preview starts with a
	 * combining mark, so Pango has a base to attach it to. */
	if (out > 0 && first_cp_is_combining(preview)) {
		memmove(preview + 3, preview, out + 1);
		memcpy(preview, "\xE2\x97\x8C", 3);	/* U+25CC */
	}

	return preview;
}

static int history_entry_matches(const ClipHistoryEntry *e, const ClipBuffer *buf)
{
	if (e->kind != buf->kind)
		return 0;
	if (e->type != buf->type)
		return 0;
	if (e->size != buf->size)
		return 0;
	if (e->size > 0 && memcmp(e->data, buf->data, e->size) != 0)
		return 0;
	return 1;
}

static void history_add_from_buffer(const ClipBuffer *buf)
{
	ClipHistoryEntry *e;
	ClipHistoryEntry **new_entries;

	if (!history_enabled || buf->kind == CONTENT_EMPTY)
		return;
	if (buf->size == 0 || buf->size > CLIP_HISTORY_MAX_BYTES)
		return;

	/* dedup consecutive identical entries */
	if (history.count > 0 && history.entries[0] &&
	    history_entry_matches(history.entries[0], buf)) {
		/* re-copying the same content restarts its TTL */
		history.entries[0]->wall_time = time(NULL);
		history.entries[0]->mono_ms = clip_monotonic_ms();
		return;
	}

	e = wmalloc(sizeof(*e));
	e->kind = buf->kind;
	e->size = buf->size;
	e->type = buf->type;
	e->wall_time = time(NULL);
	e->mono_ms = clip_monotonic_ms();
	e->pinned = 0;

	if (buf->data && buf->size > 0) {
		e->data = wmalloc(buf->size);
		memcpy(e->data, buf->data, buf->size);
	} else {
		e->data = NULL;
	}

	if (buf->kind == CONTENT_CAPTURED && is_text_atom(buf->type))
		e->text_preview = make_text_preview(buf->data, buf->size);
	else
		e->text_preview = NULL;

	if (buf->path)
		e->image_path = wstrdup(buf->path);
	else
		e->image_path = NULL;

	if (buf->uri)
		e->uri = wstrdup(buf->uri);
	else
		e->uri = NULL;

	/* prepend */
	new_entries = wmalloc(sizeof(ClipHistoryEntry *) * (size_t)(history.count + 1));
	new_entries[0] = e;
	if (history.count > 0)
		memcpy(new_entries + 1, history.entries,
		       sizeof(ClipHistoryEntry *) * (size_t)history.count);
	wfree(history.entries);
	history.entries = new_entries;
	history.count++;

	history_evict_over_capacity();
	popup_history_dirty = 1;

	debug_log("history: added entry (%d total)", history.count);
}

/* Evict oldest non-pinned entries while over capacity. */
static void history_evict_over_capacity(void)
{
	while (history.count > history.capacity) {
		int i, found = -1;
		for (i = history.count - 1; i >= 0; i--) {
			if (!history.entries[i]->pinned) {
				found = i;
				break;
			}
		}
		if (found < 0)
			break;
		history_free_entry(history.entries[found]);
		memmove(history.entries + found, history.entries + found + 1,
			sizeof(ClipHistoryEntry *) * (size_t)(history.count - found - 1));
		history.count--;
	}
}

static void history_clear_non_pinned(void)
{
	int i, j;

	j = 0;
	for (i = 0; i < history.count; i++) {
		if (history.entries[i]->pinned) {
			history.entries[j++] = history.entries[i];
		} else {
			history_free_entry(history.entries[i]);
		}
	}
	history.count = j;
}

static void history_remove_entry(int row)
{
	ClipHistoryEntry *e;
	int i;

	if (row < 0 || row >= history.count)
		return;
	e = history.entries[row];
	if (e->pinned)
		history.pinned_count--;
	history_free_entry(e);
	for (i = row; i < history.count - 1; i++)
		history.entries[i] = history.entries[i + 1];
	history.count--;
}

static void history_clear_all(void)
{
	int i;

	for (i = 0; i < history.count; i++)
		history_free_entry(history.entries[i]);
	wfree(history.entries);
	memset(&history, 0, sizeof(history));
	if (popup_thumbs) {
		WMFreeHashTable(popup_thumbs);
		popup_thumbs = NULL;
	}
}

static long long entry_expiry_ms(const ClipHistoryEntry *e)
{
	return e->mono_ms + ttl_seconds * 1000;
}

/* Free non-pinned entries whose TTL has passed. */
static void history_expire_entries(void)
{
	int i, j, expired = 0;

	if (ttl_seconds <= 0)
		return;

	j = 0;
	for (i = 0; i < history.count; i++) {
		ClipHistoryEntry *e = history.entries[i];

		if (!e->pinned && clip_monotonic_ms() >= entry_expiry_ms(e)) {
			history_free_entry(e);
			expired++;
			continue;
		}
		history.entries[j++] = e;
	}
	if (expired > 0) {
		history.count = j;
		debug_log("history: expired %d entr%s (ttl %llds)",
			  expired, expired == 1 ? "y" : "ies", ttl_seconds);
	}
}

/* Soonest expiry deadline among non-pinned entries, in monotonic ms.
 * Returns 0 when nothing is tracked and `now` when already due. */
static long long history_next_expiry(long long now)
{
	long long next = 0;
	int i;

	if (ttl_seconds <= 0)
		return 0;

	for (i = 0; i < history.count; i++) {
		ClipHistoryEntry *e = history.entries[i];
		long long dl;

		if (e->pinned)
			continue;
		dl = entry_expiry_ms(e);
		if (dl <= now)
			return now;
		if (next == 0 || dl < next)
			next = dl;
	}
	return next;
}

static int history_pinned_cap(void)
{
	return history.capacity;
}

static void history_toggle_pin(int row)
{
	ClipHistoryEntry *e;
	int i, oldest_pinned = -1;

	if (row < 0 || row >= history.count)
		return;
	e = history.entries[row];

	if (!e->pinned) {
		if (history.pinned_count >= history_pinned_cap()) {
			/* unpin the oldest pinned entry (FIFO) first */
			for (i = history.count - 1; i >= 0; i--) {
				if (history.entries[i]->pinned) {
					oldest_pinned = i;
					break;
				}
			}
			if (oldest_pinned >= 0) {
				history.entries[oldest_pinned]->pinned = 0;
				history.pinned_count--;
			}
		}
		e->pinned = 1;
		history.pinned_count++;
	} else {
		e->pinned = 0;
		history.pinned_count--;
	}
}

/* --- History persistence ---------------------------------------------- */

#define CLIP_HISTORY_MAGIC       "WMCLPHST"
#define CLIP_HISTORY_VERSION     1u
#define CLIP_HISTORY_MAX_ENTRIES 1000
#define CLIP_HISTORY_NAME_MAX    256

static void put_u16(unsigned char *p, uint16_t v)
{
	p[0] = (unsigned char)(v & 0xff);
	p[1] = (unsigned char)((v >> 8) & 0xff);
}

static void put_u32(unsigned char *p, uint32_t v)
{
	p[0] = (unsigned char)(v & 0xff);
	p[1] = (unsigned char)((v >> 8) & 0xff);
	p[2] = (unsigned char)((v >> 16) & 0xff);
	p[3] = (unsigned char)((v >> 24) & 0xff);
}

static void put_u64(unsigned char *p, uint64_t v)
{
	int i;

	for (i = 0; i < 8; i++)
		p[i] = (unsigned char)((v >> (8 * i)) & 0xff);
}

static uint16_t get_u16(const unsigned char *p)
{
	return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_u32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t get_u64(const unsigned char *p)
{
	uint64_t v = 0;
	int i;

	for (i = 7; i >= 0; i--)
		v = (v << 8) | p[i];
	return v;
}

static const struct {
	xcb_atom_t *atom;
	const char *name;
} history_atom_names[] = {
	{ &atom_utf8,            "UTF8_STRING" },
	{ &atom_png,             "image/png" },
	{ &atom_jpeg,            "image/jpeg" },
	{ &atom_uri_list,        "text/uri-list" },
	{ &atom_text_plain,      "text/plain" },
	{ &atom_text_plain_utf8, "text/plain;charset=utf-8" },
	{ &atom_text,            "TEXT" },
	{ &atom_compound_text,   "COMPOUND_TEXT" },
};

#define HISTORY_N_ATOM_NAMES \
	((int)(sizeof(history_atom_names) / sizeof(history_atom_names[0])))

static struct {
	xcb_atom_t atom;
	char *name;
} history_name_cache[8];
static int history_name_cache_count;

/* Fallback for atoms outside the interned pool, queried once per atom
 * while the X connection is alive (at save time it may already be gone). */
static const char *history_cached_name(xcb_atom_t atom)
{
	xcb_get_atom_name_cookie_t ck;
	xcb_get_atom_name_reply_t *rep;
	int i, len, cache_max;

	cache_max = (int)(sizeof(history_name_cache) / sizeof(history_name_cache[0]));
	for (i = 0; i < cache_max; i++) {
		if (history_name_cache[i].name && history_name_cache[i].atom == atom)
			return history_name_cache[i].name;
	}

	if (!xc || xcb_connection_has_error(xc))
		return NULL;
	if (history_name_cache_count >= cache_max)
		return NULL;

	ck = xcb_get_atom_name(xc, atom);
	rep = xcb_get_atom_name_reply(xc, ck, NULL);
	if (!rep)
		return NULL;
	len = xcb_get_atom_name_name_length(rep);
	if (len < 0 || len >= CLIP_HISTORY_NAME_MAX) {
		free(rep);
		return NULL;
	}
	i = history_name_cache_count++;
	history_name_cache[i].atom = atom;
	history_name_cache[i].name = wmalloc((size_t)len + 1);
	memcpy(history_name_cache[i].name, xcb_get_atom_name_name(rep), (size_t)len);
	history_name_cache[i].name[len] = '\0';
	free(rep);
	return history_name_cache[i].name;
}

static int history_type_to_name(xcb_atom_t atom, char *out, size_t outsz)
{
	const char *name = NULL;
	size_t len;
	int i;

	if (atom == XCB_ATOM_STRING)
		name = "STRING";
	for (i = 0; !name && i < HISTORY_N_ATOM_NAMES; i++) {
		if (*history_atom_names[i].atom == atom)
			name = history_atom_names[i].name;
	}
	if (!name)
		name = history_cached_name(atom);
	if (!name)
		return -1;
	len = strlen(name);
	if (len + 1 > outsz)
		return -1;
	memcpy(out, name, len + 1);
	return (int)len;
}

static xcb_atom_t history_name_to_type(const char *name)
{
	int i;

	if (strcmp(name, "STRING") == 0)
		return XCB_ATOM_STRING;
	for (i = 0; i < HISTORY_N_ATOM_NAMES; i++) {
		if (strcmp(name, history_atom_names[i].name) == 0)
			return *history_atom_names[i].atom;
	}
	for (i = 0; i < history_name_cache_count; i++) {
		if (history_name_cache[i].name &&
		    strcmp(name, history_name_cache[i].name) == 0)
			return history_name_cache[i].atom;
	}
	return XCB_ATOM_NONE;
}

static char *history_state_path(void)
{
	const char *state, *home;
	char *dir, *path;

	if (history_file_override)
		return wstrdup(history_file_override);

	state = getenv("XDG_STATE_HOME");
	if (state && state[0] == '/') {
		dir = wstrconcat(state, "/" PACKAGE_TARNAME);
	} else {
		home = getenv("HOME");
		dir = wstrconcat(home ? home : "",
				 "/.local/state/" PACKAGE_TARNAME);
	}
	path = wstrappend(dir, "/wmclipd-history");
	return path;
}

/* Create each missing component of the parent directory of `path`,
 * mode 0700. */
static int history_mkdir_parents(const char *path)
{
	const char *slash;
	char *dir, *p;
	size_t len;
	int rc;

	slash = strrchr(path, '/');
	if (!slash || slash == path)
		return 0;
	len = (size_t)(slash - path);
	dir = wmalloc(len + 1);
	memcpy(dir, path, len);
	dir[len] = '\0';

	rc = 0;
	for (p = dir + 1; ; p++) {
		if (*p == '/' || *p == '\0') {
			char saved = *p;
			*p = '\0';
			if (mkdir(dir, 0700) < 0 && errno != EEXIST) {
				debug_log("history: cannot create %s: %s",
					  dir, strerror(errno));
				rc = -1;
			}
			*p = saved;
			if (rc < 0 || saved == '\0')
				break;
		}
	}
	wfree(dir);
	return rc;
}

/* Write via a same-directory temp file so a partial save can never
 * replace a good file. */
static int history_write_file(const char *path, const unsigned char *data,
			      size_t len)
{
	char *tmp;
	const unsigned char *p;
	size_t remaining;
	ssize_t w;
	struct stat st;
	int fd, ok, closed;

	tmp = wmalloc(strlen(path) + 5);
	sprintf(tmp, "%s.tmp", path);

	if (history_mkdir_parents(path) < 0) {
		wfree(tmp);
		return -1;
	}

	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
	if (fd < 0) {
		debug_log("history: cannot create %s: %s", tmp, strerror(errno));
		wfree(tmp);
		return -1;
	}

	ok = 1;
	if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || fchmod(fd, 0600) < 0) {
		debug_log("history: cannot secure %s: %s", tmp, strerror(errno));
		close(fd);
		unlink(tmp);
		wfree(tmp);
		return -1;
	}

	p = data;
	remaining = len;
	while (remaining > 0) {
		w = write(fd, p, remaining);
		if (w < 0) {
			if (errno == EINTR)
				continue;
			ok = 0;
			break;
		}
		p += w;
		remaining -= w;
	}
	if (ok && fsync(fd) < 0)
		ok = 0;
	closed = close(fd) == 0;
	if (ok && !closed)
		ok = 0;

	if (ok && rename(tmp, path) < 0) {
		debug_log("history: cannot rename %s: %s", tmp, strerror(errno));
		ok = 0;
	}
	if (!ok) {
		unlink(tmp);
		wfree(tmp);
		return -1;
	}
	wfree(tmp);
	return 0;
}

static int history_buf_reserve(unsigned char **pbuf, size_t *pcap, size_t need)
{
	unsigned char *nb;
	size_t ncap;

	if (need <= *pcap)
		return 0;
	ncap = *pcap > 0 ? *pcap : 512;
	while (ncap < need) {
		if (ncap > SIZE_MAX / 2) {
			ncap = need;
			break;
		}
		ncap *= 2;
	}
	nb = wrealloc(*pbuf, ncap);
	if (!nb)
		return -1;
	*pbuf = nb;
	*pcap = ncap;
	return 0;
}

static void history_save(void)
{
	unsigned char *buf, *p;
	size_t cap, len, namelen, pathlen, urilen;
	char *path, name[CLIP_HISTORY_NAME_MAX];
	ClipHistoryEntry *e;
	int i, nsaved, nlen;

	if (!persist_enabled || !history_enabled)
		return;

	path = history_state_path();
	buf = NULL;
	cap = 0;
	len = 0;
	nsaved = 0;

	if (history_buf_reserve(&buf, &cap, 16) == 0) {
		memcpy(buf, CLIP_HISTORY_MAGIC, 8);
		put_u32(buf + 8, CLIP_HISTORY_VERSION);
		put_u32(buf + 12, 0);
		len = 16;

		/* entries are stored newest first, same order as memory */
		for (i = 0; i < history.count; i++) {
			e = history.entries[i];
			if (!e->data || e->size == 0 || e->size > CLIP_HISTORY_MAX_BYTES)
				continue;
			nlen = history_type_to_name(e->type, name, sizeof(name));
			if (nlen < 0) {
				debug_log("history: cannot resolve type atom %u, entry skipped",
					  (unsigned int)e->type);
				continue;
			}
			namelen = (size_t)nlen;
			pathlen = e->kind == CONTENT_SCREENSHOT && e->image_path ?
				  strlen(e->image_path) : 0;
			urilen = e->kind == CONTENT_SCREENSHOT && e->uri ?
				 strlen(e->uri) : 0;
			if (history_buf_reserve(&buf, &cap, len + 4 + namelen + 16 +
						e->size + 4 + pathlen + 4 + urilen) < 0)
				break;
			p = buf + len;
			p[0] = (unsigned char)e->kind;
			p[1] = e->pinned ? 1 : 0;
			put_u16(p + 2, (uint16_t)namelen);
			memcpy(p + 4, name, namelen);
			p += 4 + namelen;
			put_u64(p, (uint64_t)e->wall_time);
			put_u64(p + 8, (uint64_t)e->size);
			p += 16;
			memcpy(p, e->data, e->size);
			p += e->size;
			if (e->kind == CONTENT_SCREENSHOT) {
				put_u32(p, (uint32_t)pathlen);
				p += 4;
				if (pathlen > 0) {
					memcpy(p, e->image_path, pathlen);
					p += pathlen;
				}
				put_u32(p, (uint32_t)urilen);
				p += 4;
				if (urilen > 0) {
					memcpy(p, e->uri, urilen);
					p += urilen;
				}
			}
			len = (size_t)(p - buf);
			nsaved++;
		}
		put_u32(buf + 12, (uint32_t)nsaved);
	}

	if (len == 0) {
		debug_log("history: cannot build the state buffer");
		wfree(buf);
		wfree(path);
		return;
	}

	if (history_write_file(path, buf, len) == 0) {
		debug_log("history: saved %d entr%s to %s", nsaved,
			  nsaved == 1 ? "y" : "ies", path);
	}

	wfree(buf);
	wfree(path);
}

static void history_load(void)
{
	ClipHistoryEntry **loaded, *e;
	unsigned char hdr[16], *buf;
	const unsigned char *payload;
	char *path, *imgpath, *uri, tname[CLIP_HISTORY_NAME_MAX];
	struct stat st, pst;
	uint64_t limit;
	ssize_t got;
	size_t dlen, off, pos, namelen, pathlen, urilen, size;
	long long age, mono_now, left;
	time_t now, wall;
	int fd, n, i, kind, pinned, loaded_count, broken;
	xcb_atom_t type;

	if (!persist_enabled || !history_enabled)
		return;

	path = history_state_path();
	fd = open(path, O_RDONLY | O_NOFOLLOW);
	if (fd < 0) {
		if (errno != ENOENT)
			debug_log("history: cannot open %s: %s", path, strerror(errno));
		wfree(path);
		return;
	}
	if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
		debug_log("history: %s is not a regular file", path);
		close(fd);
		wfree(path);
		return;
	}
	limit = (uint64_t)history.capacity *
		((uint64_t)CLIP_HISTORY_MAX_BYTES + 4096) + 65536;
	if ((uint64_t)st.st_size > limit) {
		debug_log("history: %s is too large, ignoring", path);
		close(fd);
		wfree(path);
		return;
	}

	dlen = (size_t)st.st_size;
	if (dlen < sizeof(hdr)) {
		debug_log("history: %s has no valid header", path);
		close(fd);
		wfree(path);
		return;
	}

	off = 0;
	while (off < sizeof(hdr)) {
		got = read(fd, hdr + off, sizeof(hdr) - off);
		if (got < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (got == 0)
			break;
		off += (size_t)got;
	}
	if (off != sizeof(hdr) ||
	    memcmp(hdr, CLIP_HISTORY_MAGIC, 8) != 0) {
		debug_log("history: %s has no valid header", path);
		close(fd);
		wfree(path);
		return;
	}
	if (get_u32(hdr + 8) != CLIP_HISTORY_VERSION) {
		debug_log("history: %s has unsupported version %u",
			  path, get_u32(hdr + 8));
		close(fd);
		wfree(path);
		return;
	}
	n = (int)get_u32(hdr + 12);
	if (n > CLIP_HISTORY_MAX_ENTRIES) {
		debug_log("history: %s declares too many entries", path);
		close(fd);
		wfree(path);
		return;
	}

	buf = wmalloc(dlen);
	memcpy(buf, hdr, sizeof(hdr));
	off = sizeof(hdr);
	while (off < dlen) {
		got = read(fd, buf + off, dlen - off);
		if (got < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (got == 0)
			break;
		off += (size_t)got;
	}
	close(fd);

	loaded = wmalloc(sizeof(ClipHistoryEntry *) * (size_t)(n > 0 ? n : 1));
	loaded_count = 0;
	broken = 0;
	mono_now = clip_monotonic_ms();
	now = time(NULL);
	pos = 16;

	for (i = 0; i < n; i++) {
		imgpath = NULL;
		uri = NULL;

		if (dlen - pos < 4) {
			broken = 1;
			break;
		}
		kind = buf[pos];
		pinned = buf[pos + 1] ? 1 : 0;
		namelen = get_u16(buf + pos + 2);
		if ((kind != CONTENT_CAPTURED && kind != CONTENT_SCREENSHOT) ||
		    namelen == 0 || namelen >= sizeof(tname) ||
		    dlen - pos < 4 + namelen) {
			broken = 1;
			break;
		}
		memcpy(tname, buf + pos + 4, namelen);
		tname[namelen] = '\0';
		type = history_name_to_type(tname);
		pos += 4 + namelen;

		if (dlen - pos < 16) {
			broken = 1;
			break;
		}
		wall = (time_t)get_u64(buf + pos);
		size = (size_t)get_u64(buf + pos + 8);
		pos += 16;
		if (size > CLIP_HISTORY_MAX_BYTES || dlen - pos < size) {
			broken = 1;
			break;
		}
		payload = buf + pos;
		pos += size;

		if (kind == CONTENT_SCREENSHOT) {
			if (dlen - pos < 4) {
				broken = 1;
				break;
			}
			pathlen = get_u32(buf + pos);
			pos += 4;
			if (pathlen > dlen - pos) {
				broken = 1;
				break;
			}
			if (pathlen > 0) {
				imgpath = wmalloc(pathlen + 1);
				memcpy(imgpath, buf + pos, pathlen);
				imgpath[pathlen] = '\0';
			}
			pos += pathlen;
			if (dlen - pos < 4) {
				broken = 1;
				break;
			}
			urilen = get_u32(buf + pos);
			pos += 4;
			if (urilen > dlen - pos) {
				broken = 1;
				break;
			}
			if (urilen > 0) {
				uri = wmalloc(urilen + 1);
				memcpy(uri, buf + pos, urilen);
				uri[urilen] = '\0';
			}
			pos += urilen;
		}

		if (type == XCB_ATOM_NONE) {
			debug_log("history: entry type '%s' is unknown here, dropped",
				  tname);
			wfree(imgpath);
			wfree(uri);
			continue;
		}
		if (size == 0) {
			wfree(imgpath);
			wfree(uri);
			continue;
		}
		if (kind == CONTENT_SCREENSHOT) {
			if (!imgpath || stat(imgpath, &pst) < 0) {
				debug_log("history: screenshot file is gone, dropped");
				wfree(imgpath);
				wfree(uri);
				continue;
			}
		}

		if (ttl_seconds > 0 && !pinned) {
			left = (long long)wall + ttl_seconds - (long long)now;
			if (left <= 0) {
				debug_log("history: dropped expired entry");
				wfree(imgpath);
				wfree(uri);
				continue;
			}
			age = (long long)now - (long long)wall;
			if (age < 0)
				age = 0;
		} else {
			age = 0;
		}

		e = wmalloc(sizeof(*e));
		e->kind = (ContentKind)kind;
		e->size = size;
		e->type = type;
		e->wall_time = wall;
		e->mono_ms = mono_now - age * 1000;
		e->pinned = pinned;
		e->data = wmalloc(size);
		memcpy(e->data, payload, size);
		if (kind == CONTENT_CAPTURED && is_text_atom(type))
			e->text_preview = make_text_preview(e->data, size);
		e->image_path = imgpath;
		e->uri = uri;
		imgpath = NULL;
		uri = NULL;
		loaded[loaded_count++] = e;
	}

	if (broken) {
		debug_log("history: %s is corrupt or truncated after %d entries",
			  path, loaded_count);
		wfree(imgpath);
		wfree(uri);
	}

	if (loaded_count > 0) {
		for (i = 0; i < loaded_count; i++) {
			if (loaded[i]->pinned)
				history.pinned_count++;
		}
		history.entries = loaded;
		history.count = loaded_count;
		history_evict_over_capacity();
	} else {
		wfree(loaded);
	}
	debug_log("history: loaded %d entr%s from %s", history.count,
		  history.count == 1 ? "y" : "ies", path);
	wfree(buf);
	wfree(path);
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

static int mirror_clipboard_text_to_primary(const char *reason)
{
	if (!is_text_atom(clipboard_buffer.type))
		return 0;
	if (!copy_captured_payload(&primary_buffer, &clipboard_buffer))
		return 0;

	primary_buffer.owns = 0;
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
	WXSendEvent32(xc, &ev, sizeof(ev), 0, req->requestor, 0);
	wXFlush(xc);
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
	xcb_window_t source_owner, primary_owner;
	int captured;

	if (ev->selection == atom_clipboard)
		buf = &clipboard_buffer;
	else if (ev->selection == atom_primary)
		buf = &primary_buffer;
	else
		return;

	if (ev->owner == owner_window) {
		if (ev->selection == atom_primary)
			primary_pending_deadline = 0;
		return;
	}

	debug_log("selection changed: %s owner=0x%x",
		  debug_selection_name(ev->selection),
		  (unsigned int)ev->owner);
	buf->owns = 0;
	if (ev->owner == 0) {
		if (ev->selection == atom_primary)
			primary_pending_deadline = 0;
		if (ev->selection == atom_clipboard && !buffer_has_payload(buf) &&
		    mirror_primary_to_clipboard(ev->timestamp,
						"clipboard owner cleared", 1))
			return;
		if (buffer_has_payload(buf))
			claim_selection(ev->selection, buf, ev->timestamp);
		return;
	}

	if (ev->selection == atom_primary) {
		primary_pending_ts = ev->timestamp;
		primary_pending_deadline = clip_monotonic_ms() + PRIMARY_DEBOUNCE_MS;
		debug_log("deferring PRIMARY capture (debounce %dms)",
			  PRIMARY_DEBOUNCE_MS);
		return;
	}

	source_owner = ev->owner;
	captured = capture_selection(ev->selection, buf, ev->timestamp, 1);
	if (captured) {
		primary_owner = selection_owner(atom_primary);

		if (primary_owner == source_owner)
			mirror_clipboard_text_to_primary("same source owner");
	}
}

static void flush_primary_pending(void)
{
	xcb_window_t source_owner, clipboard_owner;
	int captured;

	if (primary_pending_deadline == 0)
		return;

	primary_pending_deadline = 0;
	primary_buffer.owns = 0;

	source_owner = selection_owner(atom_primary);
	if (source_owner == 0 || source_owner == owner_window)
		return;

	captured = capture_selection(atom_primary, &primary_buffer,
				     primary_pending_ts, 0);
	if (!captured)
		return;
	clipboard_owner = selection_owner(atom_clipboard);
	if (clipboard_owner == source_owner)
		mirror_primary_to_clipboard(primary_pending_ts,
					    "same source owner", 0);
	else if (clipboard_owner == 0)
		mirror_primary_to_clipboard(primary_pending_ts,
					    "clipboard empty", 1);
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

static void dispatch_xevent(xcb_generic_event_t **xev)
{
	uint8_t rt = (uint8_t)((*xev)->response_type & 0x7f);

	if (rt == (uint8_t)(xfixes_first_event + XCB_XFIXES_SELECTION_NOTIFY) ||
	    rt == XCB_SELECTION_REQUEST || rt == XCB_SELECTION_CLEAR ||
	    rt == XCB_PROPERTY_NOTIFY || rt == XCB_DESTROY_NOTIFY) {
		handle_x_event(*xev);
		return;
	}

	/* WMHandleEvent only borrows the event, the caller frees it */
	if (popup_wscr)
		WMHandleEvent(*xev);
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
	if (size == 0) {
		fprintf(stderr, "%s: ignoring empty or unrecognized screenshot '%s'\n",
			prog, path ? path : "");
		wfree(data);
		return 0;
	}

	clear_buffer(&clipboard_buffer);
	clipboard_buffer.kind = CONTENT_SCREENSHOT;
	clipboard_buffer.data = data;
	clipboard_buffer.size = size;
	clipboard_buffer.type = image_type;
	clipboard_buffer.path = wstrdup(path);
	clipboard_buffer.uri = file_uri_from_path(path);

	history_add_from_buffer(&clipboard_buffer);

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

static char *tick_payload_from_event(const char *body, bool *keyed)
{
	WMPropList *pl, *key, *val;
	char *payload = NULL;

	*keyed = false;
	pl = WMCreatePropListFromDescription(body);
	if (!pl)
		return NULL;

	if (WMIsPLDictionary(pl)) {
		key = WMCreatePLString("payload");
		val = WMGetFromPLDictionary(pl, key);
		WMReleasePropList(key);
		if (val && WMIsPLString(val))
			payload = wstrdup(WMGetFromPLString(val));
		key = WMCreatePLString("keyed");
		val = WMGetFromPLDictionary(pl, key);
		WMReleasePropList(key);
		if (val && WMIsPLString(val) &&
		    strcmp(WMGetFromPLString(val), "YES") == 0)
			*keyed = true;
	}

	WMReleasePropList(pl);
	return payload;
}

static int subscribe_events(int fd)
{
	const char *events[2];
	int n = 0;

	if (run_screenshots)
		events[n++] = "screenshot";
	if (history_enabled)
		events[n++] = "tick";

	if (n == 0)
		return 1;

	return wIPCSendStringArray(fd, WIPC_SUBSCRIBE, events, n) == 0;
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

	if (!subscribe_events(fd)) {
		debug_log("IPC subscription failed: %s", strerror(errno));
		close(fd);
		return -1;
	}
	debug_log("subscribed to IPC events (screenshots=%s, tick=%s)",
		  run_screenshots ? "yes" : "no",
		  history_enabled ? "yes" : "no");
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
	} else if ((type & WIPC_EVENT_BIT) &&
		   ((type & ~WIPC_EVENT_BIT) == WIPC_EV_TICK)) {
		bool keyed = false;
		char *payload = tick_payload_from_event(body, &keyed);
		if (payload) {
			if (strcmp(payload, CLIP_POPUP_TICK) == 0) {
				debug_log("received clipboard popup tick%s",
					  keyed ? " (keyed)" : "");
				popup_tick_pending = 1;
				popup_tick_keyed = popup_tick_keyed || keyed;
			}
			wfree(payload);
		}
	}

	wfree(body);
	return 1;
}

static void intern_atoms(void)
{
	WXStaticAtomsInit(xc);

	atom_clipboard = wx_static_atoms[W_ATOM_CLIPBOARD];
	atom_targets = wx_static_atoms[W_ATOM_TARGETS];
	atom_incr = wx_static_atoms[W_ATOM_INCR];
	atom_utf8 = wx_static_atoms[W_ATOM_UTF8_STRING];
	atom_text = wx_static_atoms[W_ATOM_TEXT];
	atom_compound_text = wx_static_atoms[W_ATOM_COMPOUND_TEXT];
	atom_uri_list = wx_static_atoms[W_ATOM_TEXT_URI_LIST];

	/* wmclipd's own clipboard vocabulary, interned locally */
	atom_png = WXInternAtom(xc, "image/png");
	atom_jpeg = WXInternAtom(xc, "image/jpeg");
	atom_text_plain = WXInternAtom(xc, "text/plain");
	atom_text_plain_utf8 = WXInternAtom(xc, "text/plain;charset=utf-8");
	atom_wmclipd_property = WXInternAtom(xc, "WMCLIPD_SELECTION");

	atom_primary = XCB_ATOM_PRIMARY;
}

static int init_x11(void)
{
	uint32_t mask;

	xc = xcconn_open(NULL, prog, 0, &screen_num);
	if (!xc)
		return 0;
	root_window = xcconn_root(xc, screen_num);

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

/* Parse "30s", "10m", "2h", "1d" or "1h30m"; a bare number means
 * seconds and 0 disables. Returns 0 with the total in *out, or -1 on
 * malformed input or an out-of-range total. */
static int parse_ttl_duration(const char *arg, long long *out)
{
	static const struct {
		char unit;
		long long mult;
	} units[] = {
		{ 's', 1 }, { 'm', 60 }, { 'h', 3600 }, { 'd', 86400 },
	};
	const char *p = arg;
	long long total = 0;
	int segments = 0;

	if (!arg || !*arg)
		return -1;

	while (*p) {
		char *end;
		long long val, mult;
		size_t u;

		if (!isdigit((unsigned char)*p))
			return -1;
		errno = 0;
		val = strtoll(p, &end, 10);
		if (errno == ERANGE || val < 0)
			return -1;
		p = end;

		if (*p == '\0') {
			if (segments > 0)
				return -1;
			mult = 1;
		} else {
			mult = 0;
			for (u = 0; u < sizeof(units) / sizeof(units[0]); u++) {
				if (units[u].unit == *p) {
					mult = units[u].mult;
					break;
				}
			}
			if (mult == 0)
				return -1;
			p++;
		}

		if (val > CLIP_TTL_MAX_SECONDS)
			return -1;
		total += val * mult;
		if (total > CLIP_TTL_MAX_SECONDS)
			return -1;
		segments++;
	}

	if (total > 0 && total < CLIP_TTL_MIN_SECONDS)
		return -1;

	*out = total;
	return 0;
}

static int parse_args(int argc, char **argv)
{
	enum {
		OPT_CLIPBOARD = 256,
		OPT_SCREENSHOTS,
		OPT_HISTORY,
		OPT_HISTORY_FILE,
		OPT_TTL,
		OPT_PERSIST,
		OPT_DEBUG,
	};
	int ch;
	char *hend;
	long long hval;
	struct option longopts[] = {
		{ "clipboard",     no_argument,       NULL, OPT_CLIPBOARD },
		{ "screenshots",   no_argument,       NULL, OPT_SCREENSHOTS },
		{ "screenshot",    no_argument,       NULL, OPT_SCREENSHOTS },
		{ "history",       required_argument, NULL, OPT_HISTORY },
		{ "history-file",  required_argument, NULL, OPT_HISTORY_FILE },
		{ "ttl",           required_argument, NULL, OPT_TTL },
		{ "persist",       no_argument,       NULL, OPT_PERSIST },
		{ "debug",         no_argument,       NULL, OPT_DEBUG },
		{ "help",          no_argument,       NULL, 'h' },
		{ "version",       no_argument,       NULL, 'v' },
		{ NULL,            0,                 NULL, 0 }
	};

	while ((ch = getopt_long(argc, argv, "hv", longopts, NULL)) != -1) {
		switch (ch) {
		case OPT_CLIPBOARD:
			run_clipboard = 1;
			break;
		case OPT_SCREENSHOTS:
			run_screenshots = 1;
			break;
		case OPT_HISTORY:
			errno = 0;
			hval = strtoll(optarg, &hend, 10);
			if (*optarg == '\0' || *hend != '\0' ||
			    errno == ERANGE || hval < 0 || hval > INT_MAX) {
				fprintf(stderr,
					"%s: invalid --history count '%s'\n",
					prog, optarg);
				return -1;
			}
			history_capacity = (int)hval;
			if (history_capacity > CLIP_HISTORY_MAX_ENTRIES) {
				fprintf(stderr,
					"%s: --history %d exceeds the persist limit of %d; clamping\n",
					prog, history_capacity, CLIP_HISTORY_MAX_ENTRIES);
				history_capacity = CLIP_HISTORY_MAX_ENTRIES;
			}
			break;
		case OPT_HISTORY_FILE:
			history_file_override = optarg;
			persist_enabled = 1;
			break;
		case OPT_PERSIST:
			persist_enabled = 1;
			break;
		case OPT_TTL:
			if (parse_ttl_duration(optarg, &ttl_seconds) < 0) {
				fprintf(stderr,
					"%s: invalid --ttl duration '%s' (use e.g. 30s, 10m, 2h, 365d; minimum 5s, maximum 365d)\n",
					prog, optarg);
				return -1;
			}
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
	history_enabled = history_capacity > 0;
	return 0;
}

/* --- Clipboard popup -------------------------------------------------- */

static int popup_clicked_row = -1;
static int popup_x, popup_y, popup_w, popup_h;

static const char *entry_type_label(xcb_atom_t type)
{
	if (type == atom_png)
		return "image/png";
	if (type == atom_jpeg)
		return "image/jpeg";
	if (type == atom_utf8)
		return "UTF8 text";
	if (type == XCB_ATOM_STRING)
		return "text";
	if (type == atom_text || type == atom_compound_text)
		return "text";
	if (type == atom_text_plain || type == atom_text_plain_utf8)
		return "text/plain";
	return "data";
}

static void popup_list_action(WMWidget *w, void *data)
{
	(void)data;
	popup_clicked_row = WMGetListSelectedItemRow((WMList *)w);
}

static WMPixmap *load_thumbnail(ClipHistoryEntry *e)
{
	WMPixmap *pix;
	RContext *rctx;
	RImage *img, *scaled;
	int icon;

	if (!popup_thumbs)
		popup_thumbs = WMCreateHashTable(WMIntHashCallbacks);

	pix = WMHashGet(popup_thumbs, (void *)(intptr_t)e);
	if (pix)
		return pix;

	if (!is_image_atom(e->type))
		return NULL;

	rctx = WMScreenRContext(popup_wscr);
	if (!rctx)
		return NULL;

	if (e->image_path) {
		img = RLoadImage(rctx, e->image_path, 0);
	} else if (e->data && e->size > 0) {
		char tmppath[] = "/tmp/wmclipd-thumb-XXXXXX";
		int fd = mkstemp(tmppath);
		if (fd < 0)
			return NULL;
		if (write(fd, e->data, e->size) != (ssize_t)e->size) {
			close(fd);
			unlink(tmppath);
			return NULL;
		}
		close(fd);
		img = RLoadImage(rctx, tmppath, 0);
		unlink(tmppath);
	} else {
		return NULL;
	}

	if (!img)
		return NULL;

	e->width = img->width;
	e->height = img->height;

	icon = popup_icon_size;
	if (img->width > icon || img->height > icon) {
		int nw, nh;
		if (img->width >= img->height) {
			nw = icon;
			nh = (icon * img->height + img->width / 2) / img->width;
		} else {
			nh = icon;
			nw = (icon * img->width + img->height / 2) / img->height;
		}
		if (nw < 1) nw = 1;
		if (nh < 1) nh = 1;
		scaled = RSmoothScaleImage(img, nw, nh);
		RReleaseImage(img);
		if (!scaled)
			return NULL;
		img = scaled;
	}

	pix = WMCreatePixmapFromRImage(popup_wscr, img, 128);
	RReleaseImage(img);
	if (pix)
		WMHashInsert(popup_thumbs, (void *)(intptr_t)e, pix);
	return pix;
}

static void draw_history_entry(WMList *lPtr, int index, xcb_drawable_t d,
			       char *text, int state, WMRect *rect)
{
	WMListItem *item;
	ClipHistoryEntry *e;
	WMColor *back;
	char detail[64];
	char capbuf[256];
	char tbuf[16];
	char sbuf[32];
	struct tm tm_now, tm_entry;
	time_t now_time;
	const char *label;
	int tx, fh, ix, iy;
	int tm_ok;
	WMPixmap *thumb;
	WMSize isz;

	(void)text;
	item = WMGetListItem(lPtr, index);
	if (!item || !item->clientData)
		return;
	e = (ClipHistoryEntry *)item->clientData;

	back = (state & WLDSSelected) ? popup_sel_color : popup_bg_color;
	WMPaintColorSwatch(back, d, rect->pos.x, rect->pos.y,
			   rect->size.width, rect->size.height);

	ix = rect->pos.x + 2;

	thumb = load_thumbnail(e);
	if (thumb) {
		isz = WMGetPixmapSize(thumb);
		iy = rect->pos.y + (popup_item_h - isz.height) / 2;
		WMDrawPixmap(thumb, d, ix + (popup_icon_size - isz.width) / 2, iy);
	}

	tx = ix + popup_icon_size + 4;
	int text_x = tx;

	if (e->kind == CONTENT_SCREENSHOT && e->image_path) {
		const char *base = strrchr(e->image_path, '/');
		const char *fname = base ? base + 1 : e->image_path;
		if (is_image_atom(e->type) && e->width > 0)
			snprintf(capbuf, sizeof(capbuf), "%s  %d\xc3\x97%d",
				 fname, e->width, e->height);
		else
			snprintf(capbuf, sizeof(capbuf), "%s", fname);
		label = capbuf;
	} else if (is_image_atom(e->type) && e->width > 0) {
		snprintf(capbuf, sizeof(capbuf), "%d\xc3\x97%d",
			 e->width, e->height);
		label = capbuf;
	} else if (e->text_preview) {
		label = e->text_preview;
	} else {
		label = entry_type_label(e->type);
	}

	fh = WMFontHeight(popup_bold_font);
	WMDrawString(popup_wscr, d, WMBlackColor(popup_wscr), popup_bold_font,
		     tx, rect->pos.y + 2, label, strlen(label));
	tx += WMWidthOfString(popup_bold_font, label, strlen(label));

	if (e->pinned) {
		tx += 4;
		WMDrawString(popup_wscr, d, WMBlackColor(popup_wscr),
			     popup_bold_font, tx, rect->pos.y + 2,
			     CLIP_POPUP_PIN_CHAR, CLIP_POPUP_PIN_LEN);
	}

	if (e->size >= 1024 * 1024) {
		snprintf(sbuf, sizeof(sbuf), "%.1f MiB",
			 (double)e->size / (1024 * 1024));
	} else if (e->size >= 1024) {
		snprintf(sbuf, sizeof(sbuf), "%.1f KiB",
			 (double)e->size / 1024);
	} else {
		snprintf(sbuf, sizeof(sbuf), "%zu B", e->size);
	}

	now_time = time(NULL);
	tm_ok = localtime_r(&now_time, &tm_now) != NULL &&
		localtime_r(&e->wall_time, &tm_entry) != NULL;
	if (tm_ok && (tm_now.tm_year != tm_entry.tm_year ||
		      tm_now.tm_yday != tm_entry.tm_yday))
		strftime(tbuf, sizeof(tbuf), "%a %d %H:%M", &tm_entry);
	else if (tm_ok)
		strftime(tbuf, sizeof(tbuf), "%H:%M", &tm_entry);
	else
		tbuf[0] = '\0';

	snprintf(detail, sizeof(detail), "%s  %s  %s",
		 entry_type_label(e->type), sbuf, tbuf);
	WMDrawString(popup_wscr, d, popup_dim_color, popup_norm_font,
		     text_x, rect->pos.y + 2 + fh, detail, strlen(detail));
}

static void get_work_area(int *ax, int *ay, int *aw, int *ah)
{
	xcb_atom_t net_workarea = wx_static_atoms[W_ATOM_NET_WORKAREA];
	xcb_atom_t net_current_desktop = wx_static_atoms[W_ATOM_NET_CURRENT_DESKTOP];
	uint32_t desktop = 0;
	size_t count;
	uint32_t *areas;

	*ax = 0;
	*ay = 0;
	*aw = (int)wXDisplayWidth(xc, screen_num);
	*ah = (int)wXDisplayHeight(xc, screen_num);

	if (net_workarea == XCB_NONE)
		return;

	if (net_current_desktop != XCB_NONE)
		WXGetCardinal(xc, root_window, net_current_desktop, &desktop);

	areas = WXGetCardinalList(xc, root_window, net_workarea,
				  1024, &count);
	if (areas && ((uint64_t)desktop + 1) * 4 <= count) {
		*ax = (int)areas[desktop * 4];
		*ay = (int)areas[desktop * 4 + 1];
		*aw = (int)areas[desktop * 4 + 2];
		*ah = (int)areas[desktop * 4 + 3];
	}
	wfree(areas);
}

static void place_popup(int rows)
{
	int rx, ry;
	int ax, ay, aw, ah;

	get_work_area(&ax, &ay, &aw, &ah);

	popup_w = aw * 40 / 100;
	if (popup_w > 400)
		popup_w = 400;
	if (popup_w < 200)
		popup_w = 200;

	popup_h = rows * popup_item_h + 4;
	if (popup_h > ah)
		popup_h = ah;

	if (wXQueryPointer(xc, root_window, NULL, NULL,
			   &rx, &ry, NULL, NULL, NULL)) {
		popup_x = rx - popup_w / 2;
		popup_y = ry - popup_item_h / 2;
	} else {
		popup_x = ax + (aw - popup_w) / 2;
		popup_y = ay + (ah - popup_h) / 2;
	}

	if (popup_x < ax)
		popup_x = ax;
	if (popup_x + popup_w > ax + aw)
		popup_x = ax + aw - popup_w;
	if (popup_y < ay)
		popup_y = ay;
	if (popup_y + popup_h > ay + ah)
		popup_y = ay + ah - popup_h;
}

static void refresh_popup_list(void)
{
	int i, rows;

	WMClearList(popup_list);
	rows = history.count;
	if (rows > 20)
		rows = 20;
	for (i = 0; i < rows; i++) {
		WMListItem *it = WMInsertListItem(popup_list, -1, "");
		it->clientData = history.entries[i];
	}
	place_popup(rows);
	WMResizeWidget(popup_win, popup_w, popup_h);
	WMResizeWidget(popup_list, popup_w, popup_h);
	WMMoveWidget(popup_win, popup_x, popup_y);
	WMSelectListItem(popup_list, 0);
	WMSetListPosition(popup_list, 0);
}

static void refresh_popup_list_keep_selection(void)
{
	WMListItem *sel = WMGetListSelectedItem(popup_list);
	ClipHistoryEntry *keep = sel ? sel->clientData : NULL;
	int i, rows;

	refresh_popup_list();
	if (!keep)
		return;
	rows = WMGetListNumberOfRows(popup_list);
	for (i = 0; i < rows; i++) {
		WMListItem *it = WMGetListItem(popup_list, i);

		if (it->clientData == keep) {
			WMSelectListItem(popup_list, i);
			WMSetListPosition(popup_list, i);
			break;
		}
	}
}

static void hide_popup(void)
{
	if (popup_mapped) {
		WMUnmapWidget(popup_win);
		wXFlush(xc);
		popup_mapped = 0;
	}
}

static void copy_entry_to_buffer(const ClipHistoryEntry *e, ClipBuffer *buf)
{
	clear_buffer(buf);
	buf->kind = e->kind;
	buf->type = e->type;
	buf->size = e->size;
	if (e->data && e->size > 0) {
		buf->data = wmalloc(e->size);
		memcpy(buf->data, e->data, e->size);
	}
	if (e->image_path)
		buf->path = wstrdup(e->image_path);
	if (e->uri)
		buf->uri = wstrdup(e->uri);
}

static void activate_entry(int row, xcb_timestamp_t ts)
{
	ClipHistoryEntry *e;

	if (row < 0 || row >= history.count)
		return;
	e = history.entries[row];
	copy_entry_to_buffer(e, &clipboard_buffer);
	/* wXSetSelectionOwner is a one-way xcb request, so the trailing
	 * wXFlush below pushes it out immediately (the old Xlib call
	 * flushed through the shared socket in one go). Claim both
	 * CLIPBOARD and PRIMARY for text entries so that terminals
	 * which paste from either selection on Shift+Insert receive
	 * the activated content. */
	wXSetSelectionOwner(xc, atom_clipboard, owner_window, ts);
	if (is_text_atom(e->type)) {
		copy_entry_to_buffer(e, &primary_buffer);
		wXSetSelectionOwner(xc, atom_primary, owner_window, ts);
		primary_buffer.owns = 1;
	}
	wXFlush(xc);
	clipboard_buffer.owns = 1;
	debug_log("activated history entry %d", row);
}

static void release_popup_thumbnails(void)
{
	WMHashEnumerator e;
	WMPixmap *pix;

	if (!popup_thumbs)
		return;
	e = WMEnumerateHashTable(popup_thumbs);
	while ((pix = WMNextHashEnumeratorItem(&e)) != NULL)
		WMReleasePixmap(pix);
	WMFreeHashTable(popup_thumbs);
	popup_thumbs = NULL;
}

static void destroy_popup(void)
{
	if (!popup_wscr)
		return;

	release_popup_thumbnails();
	if (popup_bold_font) {
		WMReleaseFont(popup_bold_font);
		popup_bold_font = NULL;
	}
	if (popup_norm_font) {
		WMReleaseFont(popup_norm_font);
		popup_norm_font = NULL;
	}
	WMShutdownScreen(popup_wscr);
	popup_wscr = NULL;
	popup_win = NULL;
	popup_list = NULL;
	popup_mapped = 0;
	popup_clicked_row = -1;
}

static void ensure_popup(void)
{
	uint32_t attr[2];
	int wmScaleW, wmScaleH;
	static int app_inited = 0;
	double applied, configured;

	if (popup_wscr) {
		applied = WMScreenUIScale(popup_wscr);
		configured = WMScreenPeekUIScale(popup_wscr);
		if ((applied - configured) * (applied - configured) <= 0.0001)
			return;
		debug_log("UI scale changed (%.2f -> %.2f), rebuilding popup",
			  applied, configured);
		destroy_popup();
	}

	if (!app_inited) {
		int ac = 1;
		char *av = (char *)"wmclipd";
		WMInitializeApplication("wmclipd", &ac, &av);
		app_inited = 1;
	}

	popup_wscr = WMCreateScreen(xc, screen_num);
	if (!popup_wscr) {
		debug_log("cannot create WINGs screen for popup");
		return;
	}

	WMGetScaleBaseFromSystemFont(popup_wscr, &wmScaleW, &wmScaleH);
	(void)wmScaleW;
	popup_bold_font = WMCreateFont(popup_wscr, "Sans:pixelsize=12:weight=bold");
	if (!popup_bold_font)
		popup_bold_font = WMBoldSystemFontOfSize(popup_wscr, 12);
	popup_norm_font = WMCreateFont(popup_wscr, "Sans:pixelsize=11");
	if (!popup_norm_font)
		popup_norm_font = WMSystemFontOfSize(popup_wscr, 11);
	popup_dim_color = WMDarkGrayColor(popup_wscr);
	popup_sel_color = W_SelectedBackColor(popup_wscr);
	popup_bg_color = WMGrayColor(popup_wscr);
	popup_item_h = WMFontHeight(popup_bold_font) +
		       WMFontHeight(popup_norm_font) + 6;
	popup_icon_size = popup_item_h - 4;

	popup_win = WMCreateWindowWithStyle(popup_wscr, "clipPopup",
					    WMBorderlessWindowMask);
	WMSetWindowLevel(popup_win, WMPopUpMenuWindowLevel);
	WMResizeWidget(popup_win, 100, popup_item_h);

	popup_list = WMCreateList(popup_win);
	WMHangData(popup_list, NULL);
	WMSetListAllowEmptySelection(popup_list, false);
	WMSetListUserDrawItemHeight(popup_list, (unsigned short)popup_item_h);
	WMSetListUserDrawProc(popup_list, draw_history_entry);
	WMSetListAction(popup_list, popup_list_action, NULL);
	WMMoveWidget(popup_list, 0, 0);

	WMMapSubwidgets(popup_win);
	WMRealizeWidget(popup_win);

	/* ascending CW_ bit order: override_redirect (bit 9), save_under
	 * (bit 10) */
	attr[0] = 1;	/* override_redirect */
	attr[1] = 1;	/* save_under */
	xcb_change_window_attributes(xc, WMWidgetXID(popup_win),
				     XCB_CW_OVERRIDE_REDIRECT | XCB_CW_SAVE_UNDER,
				     attr);
}

static int modifier_is_down(void)
{
	char keys[32];
	unsigned int i;
	xcb_query_keymap_cookie_t kck;
	xcb_query_keymap_reply_t *krep;
	static const xkb_keysym_t mods[] = {
		XKB_KEY_Super_L, XKB_KEY_Super_R,
		XKB_KEY_Alt_L, XKB_KEY_Alt_R,
		XKB_KEY_Control_L, XKB_KEY_Control_R,
		XKB_KEY_Shift_L, XKB_KEY_Shift_R,
		XKB_KEY_Meta_L, XKB_KEY_Meta_R,
	};

	kck = xcb_query_keymap(xc);
	krep = xcb_query_keymap_reply(xc, kck, NULL);
	if (!krep)
		return 0;
	memcpy(keys, krep->keys, sizeof(keys));
	free(krep);

	for (i = 0; i < sizeof(mods) / sizeof(mods[0]); i++) {
		xcb_keycode_t code = W_KeysymToKeycode(popup_wscr, mods[i]);
		if (code > 0 && (keys[code >> 3] & (1 << (code & 7))))
			return 1;
	}
	return 0;
}

static void drain_popup_ipc(void)
{
	struct pollfd pfd;

	if (popup_ipc_fd < 0)
		return;
	pfd.fd = popup_ipc_fd;
	pfd.events = POLLIN;
	while (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
		if (!handle_ipc_frame(popup_ipc_fd)) {
			debug_log("IPC disconnected during popup drain");
			close(popup_ipc_fd);
			popup_ipc_fd = -1;
			return;
		}
		pfd.revents = 0;
	}
}

static void service_popup_tick(void)
{
	bool keyed = popup_tick_keyed;

	if (!popup_tick_pending || popup_active)
		return;
	popup_tick_pending = 0;
	popup_tick_keyed = false;
	/* a keyed tick with the modifier already up right after a keyed
	 * popup closed is an auto-repeat echo of the same gesture */
	if (keyed && popup_wscr && popup_last_close_ms &&
	    clip_monotonic_ms() - popup_last_close_ms < 400 &&
	    !modifier_is_down()) {
		debug_log("dropped repeat echo of a committed popup tick");
		return;
	}
	show_clipboard_popup(keyed);
}

static int xtest_available = -1;

static int have_xtest(void)
{
	xcb_test_get_version_cookie_t vck;
	xcb_test_get_version_reply_t *vrep;
	const xcb_query_extension_reply_t *ext;

	if (xtest_available >= 0)
		return xtest_available;
	ext = xcb_get_extension_data(xc, &xcb_test_id);
	if (!ext || !ext->present)
		xtest_available = 0;
	else {
		vck = xcb_test_get_version(xc, XCB_TEST_MAJOR_VERSION,
					   XCB_TEST_MINOR_VERSION);
		vrep = xcb_test_get_version_reply(xc, vck, NULL);
		xtest_available = vrep ? 1 : 0;
		free(vrep);
	}
	debug_log("XTest extension: %s", xtest_available ? "yes" : "no");
	return xtest_available;
}

/* Send a paste keystroke to the target window. XTest injects at the
 * server level so events appear as real hardware keypresses (no
 * send_event flag), which terminals like xterm accept. xcb_send_event
 * is the fallback for servers without XTest. */
static void send_paste_key(xcb_window_t target, xcb_keycode_t key,
			   unsigned int modifiers)
{
	if (have_xtest()) {
		/* press modifier, press key, release key, release modifier */
		if (modifiers & XCB_MOD_MASK_SHIFT) {
			xcb_keycode_t shift = W_KeysymToKeycode(popup_wscr,
							 XKB_KEY_Shift_L);
			xcb_test_fake_input(xc, XCB_KEY_PRESS, shift,
					    XCB_CURRENT_TIME, 0, 0, 0, 0);
		}
		if (modifiers & XCB_MOD_MASK_CONTROL) {
			xcb_keycode_t ctrl = W_KeysymToKeycode(popup_wscr,
							 XKB_KEY_Control_L);
			xcb_test_fake_input(xc, XCB_KEY_PRESS, ctrl,
					    XCB_CURRENT_TIME, 0, 0, 0, 0);
		}
		xcb_test_fake_input(xc, XCB_KEY_PRESS, key,
				    XCB_CURRENT_TIME, 0, 0, 0, 0);
		xcb_test_fake_input(xc, XCB_KEY_RELEASE, key,
				    XCB_CURRENT_TIME, 0, 0, 0, 0);
		if (modifiers & XCB_MOD_MASK_CONTROL) {
			xcb_keycode_t ctrl = W_KeysymToKeycode(popup_wscr,
							 XKB_KEY_Control_L);
			xcb_test_fake_input(xc, XCB_KEY_RELEASE, ctrl,
					    XCB_CURRENT_TIME, 0, 0, 0, 0);
		}
		if (modifiers & XCB_MOD_MASK_SHIFT) {
			xcb_keycode_t shift = W_KeysymToKeycode(popup_wscr,
							 XKB_KEY_Shift_L);
			xcb_test_fake_input(xc, XCB_KEY_RELEASE, shift,
					    XCB_CURRENT_TIME, 0, 0, 0, 0);
		}
		xcb_flush(xc);
	} else {
		union {
			xcb_key_press_event_t k;
			char buf[32];
		} kev;

		memset(&kev, 0, sizeof(kev));
		kev.k.response_type = XCB_KEY_PRESS;
		kev.k.event = target;
		kev.k.root = root_window;
		kev.k.time = XCB_CURRENT_TIME;
		kev.k.detail = key;
		kev.k.state = modifiers;
		xcb_send_event(xc, 1, target, XCB_EVENT_MASK_KEY_PRESS, kev.buf);

		kev.k.response_type = XCB_KEY_RELEASE;
		xcb_send_event(xc, 1, target, XCB_EVENT_MASK_KEY_RELEASE,
			       kev.buf);

		xcb_flush(xc);
	}
}

static void show_clipboard_popup(bool keyed)
{
	xcb_keycode_t vKey, upKey, downKey, homeKey, endKey, pgupKey, pgdnKey;
	xcb_keycode_t returnKey, escapeKey, shiftLKey, shiftRKey;
	xcb_keycode_t jKey, kKey, pKey, delKey;
	bool done = false, esc_cancel = false, commit = false;
	bool release_commits, chord_handoff, grab_failed;
	int commit_row = -1;
	int i;
	int row, nrows;
	int rx, ry;
	long long last_mod_check = 0;
	xcb_timestamp_t commit_ts = XCB_CURRENT_TIME;
	struct pollfd pfd;
	xcb_generic_event_t *ev = NULL;
	xcb_window_t focus_save = XCB_NONE;
	xcb_window_t focus_target = XCB_NONE;
	xcb_get_input_focus_cookie_t fck;
	xcb_get_input_focus_reply_t *frep;

	if (popup_active)
		return;
	if (!history_enabled || history.count == 0) {
		debug_log("popup requested but history is empty");
		return;
	}

	popup_active = 1;
	ensure_popup();
	if (!popup_wscr) {
		popup_active = 0;
		return;
	}

	refresh_popup_list();
	WMMapWidget(popup_win);
	wXRaiseWindow(xc, WMWidgetXID(popup_win));
	wXFlush(xc);
	popup_mapped = 1;

	vKey = W_KeysymToKeycode(popup_wscr, XKB_KEY_v);
	upKey = W_KeysymToKeycode(popup_wscr, XKB_KEY_Up);
	downKey = W_KeysymToKeycode(popup_wscr, XKB_KEY_Down);
	homeKey = W_KeysymToKeycode(popup_wscr, XKB_KEY_Home);
	endKey = W_KeysymToKeycode(popup_wscr, XKB_KEY_End);
	pgupKey = W_KeysymToKeycode(popup_wscr, XKB_KEY_Prior);
	pgdnKey = W_KeysymToKeycode(popup_wscr, XKB_KEY_Next);
	returnKey = W_KeysymToKeycode(popup_wscr, XKB_KEY_Return);
	escapeKey = W_KeysymToKeycode(popup_wscr, XKB_KEY_Escape);
	shiftLKey = W_KeysymToKeycode(popup_wscr, XKB_KEY_Shift_L);
	shiftRKey = W_KeysymToKeycode(popup_wscr, XKB_KEY_Shift_R);
	jKey = W_KeysymToKeycode(popup_wscr, XKB_KEY_j);
	kKey = W_KeysymToKeycode(popup_wscr, XKB_KEY_k);
	pKey = W_KeysymToKeycode(popup_wscr, XKB_KEY_p);
	delKey = W_KeysymToKeycode(popup_wscr, XKB_KEY_Delete);

	/* a keyed tick means the chord was held at dispatch, even if it
	 * was already released before the popup started */
	release_commits = keyed || modifier_is_down();

	/* Save the currently focused window so we can send a Ctrl+V
	 * paste keystroke to it after committing the selection. */
	fck = xcb_get_input_focus(xc);
	frep = xcb_get_input_focus_reply(xc, fck, NULL);
	if (frep) {
		focus_save = frep->focus;
		free(frep);
	}
	focus_target = focus_save;

	/* The wmaker keybinding uses a passive XGrabKey that stays active
	 * while the modifier (e.g. Super) is held. Retry until the user
	 * releases it, then our active grab can take over. */
	for (i = 0; i < 100; i++) {
		int gkr = wXGrabKeyboard(xc, root_window, false,
					 XCB_GRAB_MODE_ASYNC,
					 XCB_GRAB_MODE_ASYNC,
					 XCB_CURRENT_TIME);
		if (gkr == WXGrabSuccess)
			break;
		usleep(10000);
	}
	chord_handoff = i > 0 && i < 100;
	/* on grab failure keep release_commits, without a keyboard grab
	 * no KEY_RELEASE is ever delivered so the keymap polling below
	 * is the only way to notice the release */
	grab_failed = i >= 100;
	if (grab_failed)
		debug_log("keyboard grab failed after retry, tracking release by keymap");
	else
		debug_log("keyboard grab succeeded after %d retries", i);
	wXGrabPointer(xc, root_window, true,
		      XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE
		      | XCB_EVENT_MASK_POINTER_MOTION,
		      XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
		      XCB_NONE, XCB_NONE, XCB_CURRENT_TIME);

	/* a release during the handoff is delivered to wmaker, not to us */
	if ((release_commits || chord_handoff) && !modifier_is_down()) {
		commit_row = WMGetListSelectedItemRow(popup_list);
		commit = true;
		done = true;
	}

	while (!done && running) {
		xcb_generic_event_t *sev = NULL;

		/* without a keyboard grab no KEY_RELEASE is ever delivered
		 * and an event stream would starve the poll check below,
		 * so track the keymap here */
		if (grab_failed && release_commits &&
		    clip_monotonic_ms() - last_mod_check >= CLIP_POPUP_POLL_MS) {
			last_mod_check = clip_monotonic_ms();
			if (!modifier_is_down()) {
				commit_row = WMGetListSelectedItemRow(popup_list);
				commit = true;
				done = true;
				break;
			}
		}

		/* Extract only selection/property/destroy events that
		 * WMMaskEvent would strand (they don't match its mask).
		 * Key, button, motion and expose events stay queued for
		 * WMMaskEvent to return. Include XFixes selection-notify so
		 * clipboard changes made while the popup is open reach the
		 * history instead of being silently dropped. */
		while (WXEvqTakeTyped(xc, XCB_SELECTION_REQUEST, XCB_NONE, &sev)) {
			dispatch_xevent(&sev);
			free(sev);
		}
		while (WXEvqTakeTyped(xc, XCB_SELECTION_CLEAR, XCB_NONE, &sev)) {
			dispatch_xevent(&sev);
			free(sev);
		}
		while (WXEvqTakeTyped(xc, XCB_PROPERTY_NOTIFY, XCB_NONE, &sev)) {
			dispatch_xevent(&sev);
			free(sev);
		}
		while (WXEvqTakeTyped(xc, XCB_DESTROY_NOTIFY, XCB_NONE, &sev)) {
			dispatch_xevent(&sev);
			free(sev);
		}
		if (run_clipboard) {
			int xfixes_type = xfixes_first_event + XCB_XFIXES_SELECTION_NOTIFY;
			while (WXEvqTakeTyped(xc, xfixes_type, XCB_NONE, &sev)) {
				dispatch_xevent(&sev);
				free(sev);
			}
		}

		if (popup_history_dirty) {
			popup_history_dirty = 0;
			refresh_popup_list_keep_selection();
		}

		if (incr.active &&
		    clip_incr_send_check(xc, &incr, clip_monotonic_ms(),
					 CLIP_DEFAULT_INCR_SEND_TIMEOUT_MS)) {
			incr_source = NULL;
			debug_log("cancelled INCR transfer: no progress in %d ms",
				  (int)CLIP_DEFAULT_INCR_SEND_TIMEOUT_MS);
		}

		/* bounded wait so a SIGTERM that only clears `running`
		 * unwedges the popup instead of blocking forever */
		if (!WXEvqPending(xc)) {
			pfd.fd = xcb_get_file_descriptor(xc);
			pfd.events = POLLIN;
			pfd.revents = 0;
			if (poll(&pfd, 1, CLIP_POPUP_POLL_MS) < 0 && errno != EINTR)
				done = true;
			else if ((release_commits || chord_handoff) &&
				 !modifier_is_down()) {
				commit_row = WMGetListSelectedItemRow(popup_list);
				commit = true;
				done = true;
			}
			continue;
		}

		if (!WXEvqTakeMask(xc, XCB_EVENT_MASK_KEY_PRESS |
				   XCB_EVENT_MASK_KEY_RELEASE |
				   XCB_EVENT_MASK_EXPOSURE |
				   XCB_EVENT_MASK_POINTER_MOTION |
				   XCB_EVENT_MASK_BUTTON_PRESS |
				   XCB_EVENT_MASK_BUTTON_RELEASE |
				   XCB_EVENT_MASK_ENTER_WINDOW,
				   XCB_NONE, &ev)) {
			/* only events outside the mask are queued and
			 * WMMaskEvent would block waiting for a match,
			 * which may never come without a keyboard grab,
			 * so dispatch one parked event instead */
			if (WXEvqNext(xc, &sev)) {
				WMHandleEvent(sev);
				free(sev);
			}
			continue;
		}

		switch ((ev->response_type & 0x7f)) {
		case XCB_KEY_PRESS:
			row = WMGetListSelectedItemRow(popup_list);
			nrows = WMGetListNumberOfRows(popup_list);

			if ((((xcb_key_press_event_t *)ev)->detail) == vKey) {
				if (row < 0)
					row = 0;
				else
					row = (row + 1) % nrows;
				WMSelectListItem(popup_list, row);
				WMSetListPosition(popup_list, row);
				wXFlush(xc);
			} else if ((((xcb_key_press_event_t *)ev)->detail) == downKey ||
				   (((xcb_key_press_event_t *)ev)->detail) == jKey) {
				if (row < 0)
					row = 0;
				else
					row = (row + 1) % nrows;
				WMSelectListItem(popup_list, row);
				WMSetListPosition(popup_list, row);
				wXFlush(xc);
			} else if ((((xcb_key_press_event_t *)ev)->detail) == upKey ||
				   (((xcb_key_press_event_t *)ev)->detail) == kKey) {
				if (row < 0)
					row = nrows - 1;
				else
					row = row > 0 ? row - 1 : nrows - 1;
				WMSelectListItem(popup_list, row);
				WMSetListPosition(popup_list, row);
				wXFlush(xc);
			} else if ((((xcb_key_press_event_t *)ev)->detail) == homeKey) {
				WMSelectListItem(popup_list, 0);
				WMSetListPosition(popup_list, 0);
				wXFlush(xc);
			} else if ((((xcb_key_press_event_t *)ev)->detail) == endKey) {
				WMSelectListItem(popup_list, nrows - 1);
				WMSetListPosition(popup_list, nrows - 1);
				wXFlush(xc);
			} else if ((((xcb_key_press_event_t *)ev)->detail) == pgupKey ||
				   (((xcb_key_press_event_t *)ev)->detail) == pgdnKey) {
				int vis = popup_h / popup_item_h;
				if (vis < 1)
					vis = 1;
				if ((((xcb_key_press_event_t *)ev)->detail) == pgupKey)
					row = row > vis ? row - vis : 0;
				else
					row = row + vis < nrows ? row + vis : nrows - 1;
				WMSelectListItem(popup_list, row);
				WMSetListPosition(popup_list, row);
				wXFlush(xc);
			} else if ((((xcb_key_press_event_t *)ev)->detail) == pKey) {
				history_toggle_pin(row);
				WMRedisplayWidget(popup_list);
				wXFlush(xc);
			} else if ((((xcb_key_press_event_t *)ev)->detail) == delKey &&
			   ((((xcb_key_press_event_t *)ev)->state) & XCB_MOD_MASK_SHIFT)) {
				history_clear_non_pinned();
				if (history.count == 0) {
					WMClearList(popup_list);
					done = true;
					esc_cancel = true;
				} else {
					refresh_popup_list();
					WMMapWidget(popup_win);
					wXFlush(xc);
				}
			} else if ((((xcb_key_press_event_t *)ev)->detail) == delKey) {
				int next_row = row;

				history_remove_entry(row);
				if (history.count == 0) {
					WMClearList(popup_list);
					done = true;
					esc_cancel = true;
				} else {
					if (next_row >= history.count)
						next_row = history.count - 1;
					refresh_popup_list();
					WMSelectListItem(popup_list, next_row);
					WMSetListPosition(popup_list, next_row);
					WMMapWidget(popup_win);
					wXFlush(xc);
				}
			} else if ((((xcb_key_press_event_t *)ev)->detail) == returnKey) {
				commit_row = row;
				commit_ts = ((xcb_key_press_event_t *)ev)->time;
				commit = true;
				done = true;
			} else if ((((xcb_key_press_event_t *)ev)->detail) == escapeKey) {
				done = true;
				esc_cancel = true;
			} else if ((((xcb_key_press_event_t *)ev)->detail) != shiftLKey &&
				   (((xcb_key_press_event_t *)ev)->detail) != shiftRKey) {
				commit_row = row;
				commit_ts = ((xcb_key_press_event_t *)ev)->time;
				commit = true;
				done = true;
			}
			break;
		case XCB_KEY_RELEASE:
			if (!release_commits)
				break;
			if ((((xcb_key_press_event_t *)ev)->detail) == vKey ||
			    (((xcb_key_press_event_t *)ev)->detail) == upKey ||
			    (((xcb_key_press_event_t *)ev)->detail) == downKey ||
			    (((xcb_key_press_event_t *)ev)->detail) == homeKey ||
			    (((xcb_key_press_event_t *)ev)->detail) == endKey ||
			    (((xcb_key_press_event_t *)ev)->detail) == pgupKey ||
			    (((xcb_key_press_event_t *)ev)->detail) == pgdnKey ||
			    (((xcb_key_press_event_t *)ev)->detail) == returnKey ||
			    (((xcb_key_press_event_t *)ev)->detail) == escapeKey ||
			    (((xcb_key_press_event_t *)ev)->detail) == jKey ||
			    (((xcb_key_press_event_t *)ev)->detail) == kKey ||
			    (((xcb_key_press_event_t *)ev)->detail) == pKey ||
			    (((xcb_key_press_event_t *)ev)->detail) == delKey ||
			    (((xcb_key_press_event_t *)ev)->detail) == shiftLKey ||
			    (((xcb_key_press_event_t *)ev)->detail) == shiftRKey)
				break;
			row = WMGetListSelectedItemRow(popup_list);
			commit_row = row;
			commit_ts = ((xcb_key_release_event_t *)ev)->time;
			commit = true;
			done = true;
			break;
		case XCB_BUTTON_PRESS:
			rx = (((xcb_button_press_event_t *)ev)->root_x); ry = (((xcb_button_press_event_t *)ev)->root_y);
			if (rx < popup_x || rx >= popup_x + popup_w ||
			    ry < popup_y || ry >= popup_y + popup_h) {
				done = true;
				esc_cancel = true;
			} else {
				popup_clicked_row = -1;
				WMHandleEvent(ev);
			}
			break;
		case XCB_BUTTON_RELEASE:
			WMHandleEvent(ev);
			if (popup_clicked_row >= 0) {
				commit_row = popup_clicked_row;
				commit_ts = ((xcb_button_release_event_t *)ev)->time;
				commit = true;
				done = true;
			}
			break;
		case XCB_MOTION_NOTIFY:
		case XCB_EXPOSE:
			WMHandleEvent(ev);
			break;
		default:
			WMHandleEvent(ev);
			break;
		}

		free(ev);
		ev = NULL;
	}

	free(ev);
	wXUngrabPointer(xc, XCB_CURRENT_TIME);
	wXUngrabKeyboard(xc, XCB_CURRENT_TIME);
	hide_popup();
	wXFlush(xc);

	if (commit && !esc_cancel && commit_row >= 0 && commit_row < history.count) {
		activate_entry(commit_row, commit_ts);

		/* Simulate a paste into the previously focused window using
		 * XTest (real keypresses, not synthetic events). For image
		 * entries send Ctrl+V, which TUI applications that support
		 * image paste (e.g. opencode) intercept to read CLIPBOARD as
		 * an image; Shift+Insert only triggers a terminal text paste
		 * which cannot carry image data. For text entries send
		 * Shift+Insert, the standard X11 paste shortcut that works in
		 * both terminals and GUI apps; Ctrl+V in a terminal enters
		 * readline's quoted-insert mode rather than pasting. */
		if (focus_target != XCB_NONE &&
		    focus_target != XCB_INPUT_FOCUS_POINTER_ROOT &&
		    focus_target != WMWidgetXID(popup_win)) {
			ClipHistoryEntry *ce = history.entries[commit_row];
			int is_image = is_image_atom(ce->type);

			xcb_set_input_focus(xc, XCB_INPUT_FOCUS_POINTER_ROOT,
					    focus_target, commit_ts);
			wXFlush(xc);

			if (is_image) {
				xcb_keycode_t v_kc = W_KeysymToKeycode(popup_wscr, XKB_KEY_v);
				if (v_kc > 0) {
					send_paste_key(focus_target, v_kc, XCB_MOD_MASK_CONTROL);
					debug_log("sent Ctrl+V paste to 0x%x",
						  (unsigned int)focus_target);
				}
			} else {
				xcb_keycode_t insert_kc = W_KeysymToKeycode(popup_wscr, XKB_KEY_Insert);
				if (insert_kc > 0) {
					send_paste_key(focus_target, insert_kc, XCB_MOD_MASK_SHIFT);
					debug_log("sent Shift+Insert paste to 0x%x",
						  (unsigned int)focus_target);
				}
			}
		}
	}

	drain_popup_ipc();
	/* drop ticks queued while the popup was open, they are repeat
	 * echoes of the same gesture */
	popup_tick_pending = 0;
	popup_tick_keyed = false;
	popup_last_close_ms = clip_monotonic_ms();
	popup_active = 0;
}

int main(int argc, char **argv)
{
	struct pollfd fds[2];
	int parse_rc, nfds, x_index, ipc_index, rc, timeout;
	long long next_ipc_connect = 0, now;
	xcb_generic_event_t *xev = NULL;

	prog = argv[0];
	parse_rc = parse_args(argc, argv);
	if (parse_rc > 0)
		return 0;
	if (parse_rc < 0)
		return 2;

	debug_log("starting: clipboard=%s screenshots=%s history=%d",
		  run_clipboard ? "yes" : "no",
		  run_screenshots ? "yes" : "no",
		  history_capacity);
	if (ttl_seconds > 0 && !history_enabled)
		debug_log("--ttl ignored: history is disabled");

	if (!init_x11())
		return 1;

	WXEvqSetDeadHook(clip_dead_hook);
	WXEvqSetErrorHook(clip_error_hook);

	history.capacity = history_capacity;
	history_load();

	if (run_clipboard && !init_clipboard_monitor())
		return 1;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGPIPE, SIG_IGN);

	while (running) {
		while (WXEvqPending(xc)) {
			if (!WXEvqNext(xc, &xev))
				break;
			dispatch_xevent(&xev);
			free(xev);
		}
		if (xcb_connection_has_error(xc))
			break;

		if (primary_pending_deadline > 0 &&
		    clip_monotonic_ms() >= primary_pending_deadline)
			flush_primary_pending();
		history_expire_entries();

		if (incr.active &&
		    clip_incr_send_check(xc, &incr, clip_monotonic_ms(),
					 CLIP_DEFAULT_INCR_SEND_TIMEOUT_MS)) {
			incr_source = NULL;
			debug_log("cancelled INCR transfer: no progress in %d ms",
				  (int)CLIP_DEFAULT_INCR_SEND_TIMEOUT_MS);
		}

		now = clip_monotonic_ms();
		if ((run_screenshots || history_enabled) &&
		    popup_ipc_fd < 0 && now >= next_ipc_connect) {
			popup_ipc_fd = connect_ipc();
			if (popup_ipc_fd < 0)
				next_ipc_connect = now + IPC_RECONNECT_MS;
		}

		nfds = 0;
		x_index = nfds;
		fds[nfds].fd = xcb_get_file_descriptor(xc);
		fds[nfds].events = POLLIN;
		fds[nfds].revents = 0;
		nfds++;

		ipc_index = -1;
		if (popup_ipc_fd >= 0) {
			ipc_index = nfds;
			fds[nfds].fd = popup_ipc_fd;
			fds[nfds].events = POLLIN;
			fds[nfds].revents = 0;
			nfds++;
		}

		timeout = -1;
		if ((run_screenshots || history_enabled) && popup_ipc_fd < 0) {
			now = clip_monotonic_ms();
			timeout = next_ipc_connect > now ?
				(int)(next_ipc_connect - now) : 0;
		}
		if (primary_pending_deadline > 0) {
			long long dl = primary_pending_deadline -
				       clip_monotonic_ms();
			int dl_ms = dl < 0 ? 0 : (int)dl;

			if (timeout < 0 || dl_ms < timeout)
				timeout = dl_ms;
		}
		if (incr.active) {
			int st_ms = (int)CLIP_DEFAULT_INCR_SEND_TIMEOUT_MS;

			if (timeout < 0 || st_ms < timeout)
				timeout = st_ms;
		}
		if (history_enabled) {
			long long dl = history_next_expiry(now);
			long long dl_ms;
			int dl_int;

			if (dl > 0) {
				dl_ms = dl - now;
				dl_int = dl_ms <= 0 ? 0 :
					 dl_ms > INT_MAX ? INT_MAX : (int)dl_ms;
				if (timeout < 0 || dl_int < timeout)
					timeout = dl_int;
			}
		}

		rc = poll(fds, nfds, timeout);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			break;
		}

		if (fds[x_index].revents & POLLIN) {
			while (WXEvqPending(xc)) {
				if (!WXEvqNext(xc, &xev))
					break;
				dispatch_xevent(&xev);
				free(xev);
			}
		}
		if (fds[x_index].revents & (POLLERR | POLLHUP | POLLNVAL))
			break;

		if (primary_pending_deadline > 0 &&
		    clip_monotonic_ms() >= primary_pending_deadline)
			flush_primary_pending();
		history_expire_entries();

		if (ipc_index >= 0 && fds[ipc_index].revents) {
			if (fds[ipc_index].revents & POLLIN) {
				if (!handle_ipc_frame(popup_ipc_fd)) {
					debug_log("IPC disconnected; will reconnect");
					close(popup_ipc_fd);
					popup_ipc_fd = -1;
					next_ipc_connect = clip_monotonic_ms() + IPC_RECONNECT_MS;
				}
			} else if (fds[ipc_index].revents & (POLLERR | POLLHUP | POLLNVAL)) {
				debug_log("IPC poll error; will reconnect");
				close(popup_ipc_fd);
				popup_ipc_fd = -1;
				next_ipc_connect = clip_monotonic_ms() + IPC_RECONNECT_MS;
			}
			service_popup_tick();
		}
	}

	debug_log("stopping");
	history_save();
	history_clear_all();
	if (popup_ipc_fd >= 0)
		close(popup_ipc_fd);
	clip_incr_send_cancel(xc, &incr);
	clear_buffer(&clipboard_buffer);
	clear_buffer(&primary_buffer);
	if (popup_wscr) {
		destroy_popup();
		WMShutdownFonts();
	}
	if (xc)
		xcb_disconnect(xc);
	return 0;
}
