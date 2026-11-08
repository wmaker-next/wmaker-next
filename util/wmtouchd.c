/*
 *  wmtouchd - Window Maker touchpad gesture daemon
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
 *  Reads three- to five-finger swipes and two- to five-finger pinches
 *  from a libinput device and turns them into Window Maker actions by
 *  sending the generic `action NAME` IPC verb. The gesture-to-action
 *  mapping lives in the WMTouch defaults domain, never in the
 *  WindowMaker domain. Links libinput, libudev and libWUtil, no X11.
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAVE_INOTIFY
#include <sys/inotify.h>
#endif

#include <libinput.h>
#include <libudev.h>

#include <WINGs/WUtil.h>
#include <WINGs/WIPC.h>

#include "wmtouch_core.h"

#define PROG "wmtouchd"
#define RECONNECT_BACKOFF_MS 500
#define RECONNECT_MAX_MS 30000
#define RECONNECT_STABLE_MS 5000
#define CONNECT_GIVEUP_MS (5 * 60 * 1000)
#define REPLY_TIMEOUT_MS 2000
#define MAX_WARNED 64

/* one owed reply per sent action chain, consumed in send order */
struct pending_chain {
	char **names;
	int count;
	long long deadline;
	struct pending_chain *next;
};

static struct {
	WMTouchConfig cfg;
	WMPropList *domain;
	char *domain_path;
	WMTouchRecognizer rec;

	int verbose;
	int selfpipe[2];
	const char *config_override;

	int ipc_fd;
	struct pending_chain *pending_head;
	struct pending_chain *pending_tail;
	int sync_replay;

	char *warned[MAX_WARNED];
	int warned_count;

#ifdef HAVE_INOTIFY
	int inotify_fd;
	int inotify_wd;
#endif
} g = {
	.ipc_fd = -1,
#ifdef HAVE_INOTIFY
	.inotify_fd = -1,
	.inotify_wd = -1
#endif
};

static volatile sig_atomic_t shutdown_pending;
static volatile sig_atomic_t sighup_pending;

static long long monotonic_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void usage(FILE *out)
{
	fprintf(out,
		"Usage: " PROG " [options]\n"
		"  -s, --seat NAME        libinput seat to watch (default seat0)\n"
		"  -d, --device PATH      watch a single /dev/input/eventN path\n"
		"  -c, --config FILE      load the WMTouch domain from FILE\n"
		"  -S, --socket PATH      connect to PATH instead of the resolved socket\n"
		"      --source-events F  replay gesture events from F instead of a device\n"
		"      --check            validate configured action names and exit\n"
		"  -v, --verbose          print runtime diagnostics to stderr\n"
		"      --debug            same as --verbose\n"
		"  -h, --help             show this message\n"
		"      --version          print version information and exit\n");
}

static void warn_once(const char *name, const char *err)
{
	int i;

	for (i = 0; i < g.warned_count; i++)
		if (strcmp(g.warned[i], name) == 0)
			return;

	fprintf(stderr, PROG ": action '%s' failed: %s\n", name,
		err ? err : "unknown");
	if (g.warned_count < MAX_WARNED)
		g.warned[g.warned_count++] = wstrdup(name);
}

static void warned_reset(void)
{
	int i;

	for (i = 0; i < g.warned_count; i++)
		wfree(g.warned[i]);
	g.warned_count = 0;
}

/* ---- libinput ------------------------------------------------------- */

static int open_restricted(const char *path, int flags, void *user_data)
{
	int fd;

	(void)user_data;
	fd = open(path, flags);
	if (fd < 0)
		fprintf(stderr, PROG ": cannot open %s: %s\n", path,
			strerror(errno));
	return fd < 0 ? -errno : fd;
}

static void close_restricted(int fd, void *user_data)
{
	(void)user_data;
	close(fd);
}

static const struct libinput_interface li_interface = {
	open_restricted,
	close_restricted
};

static void li_log_handler(struct libinput *li,
			   enum libinput_log_priority priority,
			   const char *format, va_list args)
	__attribute__((format(printf, 3, 0)));

static void li_log_handler(struct libinput *li,
			   enum libinput_log_priority priority,
			   const char *format, va_list args)
{
	(void)li;
	(void)priority;
	fprintf(stderr, PROG ": ");
	vfprintf(stderr, format, args);
}

/* ---- domain --------------------------------------------------------- */

static void domain_apply(void)
{
	wmTouchConfigFromPropList(g.domain, &g.cfg);
	warned_reset();
}

static int domain_load_path(const char *path)
{
	WMPropList *new_domain;

	new_domain = path ? WMReadPropListFromFile(path) : NULL;
	if (!new_domain)
		return -1;
	if (g.domain)
		WMReleasePropList(g.domain);
	g.domain = new_domain;
	if (g.domain_path)
		wfree(g.domain_path);
	g.domain_path = path ? wstrdup(path) : NULL;
	domain_apply();
	return 0;
}

static void domain_load(const char *override)
{
	char *path, *gpath;

	if (override) {
		if (domain_load_path(override) < 0)
			fprintf(stderr, PROG ": cannot read %s\n", override);
		return;
	}

	path = wdefaultspathfordomain(WMTOUCH_DOMAIN);
	if (path && domain_load_path(path) == 0) {
		wfree(path);
		return;
	}
	gpath = wglobaldefaultspathfordomain(WMTOUCH_DOMAIN);
	if (gpath && domain_load_path(gpath) == 0) {
		wfree(path);
		wfree(gpath);
		return;
	}
	fprintf(stderr, PROG ": no %s domain, gestures are inert\n",
		WMTOUCH_DOMAIN);
	if (g.domain) {
		WMReleasePropList(g.domain);
		g.domain = NULL;
	}
	wmTouchConfigFree(&g.cfg);
	wmTouchConfigInit(&g.cfg);
	if (path)
		wfree(path);
	if (gpath)
		wfree(gpath);
}

#ifdef HAVE_INOTIFY
/* Watch the directory, not the file inode: WMWritePropListToFile()
 * rename()s a temporary file over the target, orphaning a file watch */
static void inotify_watch_update(void)
{
	char *dir;

	if (g.inotify_fd < 0 || !g.domain_path)
		return;
	if (g.inotify_wd >= 0) {
		inotify_rm_watch(g.inotify_fd, g.inotify_wd);
		g.inotify_wd = -1;
	}
	dir = wpathdir(g.domain_path);
	g.inotify_wd = inotify_add_watch(g.inotify_fd, dir,
					 IN_CLOSE_WRITE | IN_MOVED_TO);
	wfree(dir);
}

static int inotify_drain(void)
{
	union {
		char buf[4096];
		struct inotify_event align;
	} u;
	const struct inotify_event *ev;
	const char *name;
	char *p;
	ssize_t n;
	int reload = 0;

	name = g.domain_path ? strrchr(g.domain_path, '/') : NULL;
	name = name ? name + 1 : g.domain_path;
	if (!name || !*name)
		return 0;
	while ((n = read(g.inotify_fd, u.buf, sizeof(u.buf))) > 0) {
		p = u.buf;
		while (n >= (ssize_t)sizeof(*ev)) {
			ev = (const struct inotify_event *)p;
			if ((ev->mask & (IN_CLOSE_WRITE | IN_MOVED_TO))
			    && ev->len > 0
			    && strcmp(ev->name, name) == 0)
				reload = 1;
			n -= sizeof(*ev) + ev->len;
			p += sizeof(*ev) + ev->len;
		}
	}
	return reload;
}
#endif

/* Re-read the domain on SIGHUP or a watched-file change. */
static void domain_reload(void)
{
	warned_reset();
	domain_load(g.config_override);
#ifdef HAVE_INOTIFY
	inotify_watch_update();
#endif
	if (g.verbose)
		fprintf(stderr, PROG ": reloaded %s\n",
			g.domain_path ? g.domain_path : WMTOUCH_DOMAIN);
}

/* ---- signals -------------------------------------------------------- */

static void on_signal(int sig)
{
	unsigned char b = (unsigned char)sig;
	ssize_t r;

	if (sig == SIGHUP)
		sighup_pending = 1;
	else
		shutdown_pending = 1;
	/* async-signal-safe, the reader drains it */
	r = write(g.selfpipe[1], &b, 1);
	(void)r;
}

/* ---- IPC action delivery -------------------------------------------- */

/* Build the outer batch array with one ["action", name] command per
 * chain element. handle_command() runs the batch in order. */
static char *action_payload(const char *const *names, int count)
{
	WMPropList *inner, *outer, *s;
	char *body;
	int i;

	outer = WMCreatePLArray(NULL);
	for (i = 0; i < count; i++) {
		inner = WMCreatePLArray(NULL);
		s = WMCreatePLString("action");
		WMAddToPLArray(inner, s);
		WMReleasePropList(s);
		s = WMCreatePLString(names[i]);
		WMAddToPLArray(inner, s);
		WMReleasePropList(s);
		WMAddToPLArray(outer, inner);
		WMReleasePropList(inner);
	}
	body = WMGetPropListDescription(outer, false);
	WMReleasePropList(outer);
	return body;
}

static void chain_free(struct pending_chain *c)
{
	int i;

	for (i = 0; i < c->count; i++)
		wfree(c->names[i]);
	if (c->names)
		wfree(c->names);
	wfree(c);
}

static void pending_clear(void)
{
	struct pending_chain *c, *next;

	for (c = g.pending_head; c; c = next) {
		next = c->next;
		chain_free(c);
	}
	g.pending_head = NULL;
	g.pending_tail = NULL;
}

/* one entry per sent chain, wmaker answers in send order */
static void pending_push(const char *const *names, int count)
{
	struct pending_chain *c;
	int i;

	if (count <= 0)
		return;
	c = wmalloc(sizeof(*c));
	c->names = wmalloc((size_t)count * sizeof(char *));
	for (i = 0; i < count; i++)
		c->names[i] = wstrdup(names[i]);
	c->count = count;
	c->deadline = monotonic_ms() + REPLY_TIMEOUT_MS;
	c->next = NULL;
	if (g.pending_tail)
		g.pending_tail->next = c;
	else
		g.pending_head = c;
	g.pending_tail = c;
}

static void pending_pop(void)
{
	struct pending_chain *c = g.pending_head;

	if (!c)
		return;
	g.pending_head = c->next;
	if (!g.pending_head)
		g.pending_tail = NULL;
	chain_free(c);
}

static void pending_expire(void)
{
	long long now = monotonic_ms();

	while (g.pending_head && now >= g.pending_head->deadline) {
		if (g.verbose)
			fprintf(stderr, PROG ": dropping reply for '%s'\n",
				g.pending_head->count > 0
				? g.pending_head->names[0] : "?");
		pending_pop();
	}
}

/* NULL when the result dict reports success, otherwise the error text */
static const char *reply_error(WMPropList *d)
{
	WMPropList *k, *v;
	const char *err = "unknown";

	if (!d || !WMIsPLDictionary(d))
		return err;
	k = WMCreatePLString("success");
	v = WMGetFromPLDictionary(d, k);
	WMReleasePropList(k);
	if (v && WMIsPLString(v)
	    && strcasecmp(WMGetFromPLString(v), "YES") == 0)
		return NULL;
	k = WMCreatePLString("error");
	v = WMGetFromPLDictionary(d, k);
	WMReleasePropList(k);
	if (v && WMIsPLString(v) && *WMGetFromPLString(v))
		err = WMGetFromPLString(v);
	return err;
}

static void warn_failed(const char *name, const char *err)
{
	if (name)
		warn_once(name, err);
	else if (g.verbose)
		fprintf(stderr, PROG ": action failed: %s\n",
			err ? err : "unknown");
}

/* A batch reply is one result dict for a single-command send, or an
 * array of per-command results in batch order for a chain. Each failed
 * element is correlated with the names of the oldest owed chain and
 * warned once. */
static void handle_reply(const char *body)
{
	WMPropList *d, *item;
	struct pending_chain *c;
	const char *err, *name;
	int i, n;

	c = g.pending_head;
	if (!body || !c)
		return;
	d = WMCreatePropListFromDescription(body);
	if (!d)
		return;

	if (WMIsPLArray(d)) {
		n = WMGetPropListItemCount(d);
		for (i = 0; i < n; i++) {
			item = WMGetFromPLArray(d, i);
			err = reply_error(item);
			if (!err)
				continue;
			name = i < c->count ? c->names[i] : NULL;
			warn_failed(name, err);
		}
	} else {
		err = reply_error(d);
		if (err)
			warn_failed(c->count > 0 ? c->names[0] : NULL, err);
	}
	WMReleasePropList(d);
}

/* one record per send, only its reply or its deadline removes it */
static void queue_action(const char *const *names, int count)
{
	char *body;

	if (g.ipc_fd < 0) {
		if (g.verbose)
			fprintf(stderr, PROG ": no connection, dropping '%s'\n",
				names[0]);
		return;
	}

	body = action_payload(names, count);
	if (body) {
		if (wIPCSend(g.ipc_fd, WIPC_COMMAND, body, strlen(body)) >= 0) {
			pending_push(names, count);
		} else if (g.verbose) {
			fprintf(stderr, PROG ": send failed: %s\n",
				strerror(errno));
		}
		wfree(body);
	}
}

/* Read one frame from the IPC socket. Replies to our commands come back
 * as WIPC_COMMAND frames; anything carrying the event bit is an
 * unsolicited broadcast this daemon never subscribed to, so it is
 * dropped without ending the pending-reply wait. Returns 1 for a reply
 * frame, 0 for a dropped event frame, -1 on a read error. */
static int read_reply_frame(void)
{
	uint32_t type, len;
	char *body = NULL;

	if (wIPCRecv(g.ipc_fd, &type, &body, &len) < 0) {
		if (g.verbose)
			fprintf(stderr, PROG ": recv failed: %s\n",
				strerror(errno));
		return -1;
	}
	if (type & WIPC_EVENT_BIT) {
		if (g.verbose)
			fprintf(stderr, PROG ": ignoring unsolicited event frame\n");
		wfree(body);
		return 0;
	}
	if (g.pending_head)
		handle_reply(body);
	wfree(body);
	return 1;
}

static void service_reply(void)
{
	int rc = read_reply_frame();

	if (rc > 0)
		pending_pop();
	else if (rc < 0)
		pending_clear();
}

/* Blocking send-and-wait used by the deterministic replay path only */
static void send_action_sync(const char *const *names, int count)
{
	char *body;
	long long deadline;

	if (g.ipc_fd < 0)
		return;
	body = action_payload(names, count);
	if (!body)
		return;
	if (wIPCSend(g.ipc_fd, WIPC_COMMAND, body, strlen(body)) >= 0) {
		pending_push(names, count);
		deadline = monotonic_ms() + REPLY_TIMEOUT_MS;
		while (g.pending_head && monotonic_ms() < deadline) {
			struct pollfd pfd = { .fd = g.ipc_fd, .events = POLLIN };
			long long rem = deadline - monotonic_ms();
			int pr;

			pr = poll(&pfd, 1, rem <= 0 ? 0 : (int)rem);
			if (pr <= 0 || (pfd.revents & (POLLERR | POLLHUP)))
				break;
			if (pfd.revents & POLLIN)
				service_reply();
		}
		if (g.pending_head && g.verbose)
			fprintf(stderr, PROG ": no reply for '%s'\n", names[0]);
	}
	pending_clear();
	wfree(body);
}

/* ---- gesture processing --------------------------------------------- */

static void deliver_gesture(enum WMGestureDirection dir)
{
	const char *const *names;
	char joined[256];
	int count, i, n;

	if (dir == WMT_DIR_NONE)
		return;
	count = wmTouchConfigActions(&g.cfg, g.rec.fingers, dir, &names);
	if (g.verbose) {
		n = snprintf(joined, sizeof(joined), "%d-finger %s -> ",
			     g.rec.fingers, wmTouchDirectionName(dir));
		for (i = 0; i < count && n < (int)sizeof(joined) - 1; i++)
			n += snprintf(joined + n, sizeof(joined) - n, "%s%s",
				      i ? ", " : "", names[i]);
		fprintf(stderr, PROG ": %s\n", joined);
	}
	if (count) {
		if (g.sync_replay)
			send_action_sync(names, count);
		else
			queue_action(names, count);
	}
}

static void process_gesture_event(const WMGestureEvent *ev)
{
	enum WMGestureDirection dir;

	dir = wmTouchRecognizerFeed(&g.rec, ev, &g.cfg);
	deliver_gesture(dir);
}

static void feed_swipe(enum WMGestureKind kind, int fingers,
		       double dx, double dy)
{
	WMGestureEvent ev;

	ev.kind = kind;
	ev.type = WMT_GESTURE_SWIPE;
	ev.fingers = fingers;
	ev.dx = dx;
	ev.dy = dy;
	ev.scale = 1.0;
	process_gesture_event(&ev);
}

static void feed_pinch(enum WMGestureKind kind, int fingers, double scale)
{
	WMGestureEvent ev;

	ev.kind = kind;
	ev.type = WMT_GESTURE_PINCH;
	ev.fingers = fingers;
	ev.dx = 0.0;
	ev.dy = 0.0;
	ev.scale = scale;
	process_gesture_event(&ev);
}

static void process_libinput(struct libinput *li)
{
	struct libinput_event *ev;

	libinput_dispatch(li);
	while ((ev = libinput_get_event(li))) {
		enum libinput_event_type type = libinput_event_get_type(ev);
		struct libinput_device *dev = libinput_event_get_device(ev);

		if (dev && !libinput_device_has_capability(dev,
				LIBINPUT_DEVICE_CAP_GESTURE)) {
			libinput_event_destroy(ev);
			continue;
		}

		switch (type) {
		case LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN:
		case LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE:
		case LIBINPUT_EVENT_GESTURE_SWIPE_END:
			{
				struct libinput_event_gesture *ge;
				enum WMGestureKind kind;
				double dx = 0.0, dy = 0.0;
				int fingers;

				ge = libinput_event_get_gesture_event(ev);
				fingers = libinput_event_gesture_get_finger_count(ge);
				if (type == LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN) {
					kind = WMT_GESTURE_BEGIN;
				} else if (type == LIBINPUT_EVENT_GESTURE_SWIPE_END) {
					kind = libinput_event_gesture_get_cancelled(ge)
					       ? WMT_GESTURE_CANCEL
					       : WMT_GESTURE_END;
				} else {
					kind = WMT_GESTURE_UPDATE;
					dx = libinput_event_gesture_get_dx_unaccelerated(ge);
					dy = libinput_event_gesture_get_dy_unaccelerated(ge);
				}
				feed_swipe(kind, fingers, dx, dy);
			}
			break;
		case LIBINPUT_EVENT_GESTURE_PINCH_BEGIN:
		case LIBINPUT_EVENT_GESTURE_PINCH_UPDATE:
		case LIBINPUT_EVENT_GESTURE_PINCH_END:
			{
				struct libinput_event_gesture *ge;
				enum WMGestureKind kind;
				double scale;

				ge = libinput_event_get_gesture_event(ev);
				if (type == LIBINPUT_EVENT_GESTURE_PINCH_BEGIN) {
					kind = WMT_GESTURE_BEGIN;
				} else if (type == LIBINPUT_EVENT_GESTURE_PINCH_END) {
					kind = libinput_event_gesture_get_cancelled(ge)
					       ? WMT_GESTURE_CANCEL
					       : WMT_GESTURE_END;
				} else {
					kind = WMT_GESTURE_UPDATE;
				}
				scale = libinput_event_gesture_get_scale(ge);
				feed_pinch(kind,
					   libinput_event_gesture_get_finger_count(ge),
					   scale);
			}
			break;
		case LIBINPUT_EVENT_GESTURE_HOLD_BEGIN:
		case LIBINPUT_EVENT_GESTURE_HOLD_END:
			if (g.verbose) {
				struct libinput_event_gesture *ge =
					libinput_event_get_gesture_event(ev);

				fprintf(stderr, PROG ": ignoring gesture type %d "
					"(%d fingers)\n", (int)type,
					libinput_event_gesture_get_finger_count(ge));
			}
			break;
		default:
			break;
		}
		libinput_event_destroy(ev);
	}
}

static void process_replay(const char *path)
{
	FILE *f;
	char *line = NULL;
	size_t cap = 0;
	ssize_t n;

	f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, PROG ": cannot read %s: %s\n", path,
			strerror(errno));
		return;
	}
	g.sync_replay = 1;
	while ((n = getline(&line, &cap, f)) >= 0) {
		WMGestureEvent ev;
		int rc = wmTouchParseEventLine(line, &ev);

		if (rc < 0) {
			fprintf(stderr, PROG ": malformed replay line: %s",
				line);
			continue;
		}
		if (rc == 0)
			continue;
		process_gesture_event(&ev);
	}
	g.sync_replay = 0;
	free(line);
	fclose(f);
}

/* ---- --check -------------------------------------------------------- */

static int check_actions(const char *override, const char *socket_path)
{
	char *path, *body;
	int fd, rc, f, d, i, j, unknown = 0, checked = 0;
	uint32_t type, len;
	WMPropList *inner, *outer, *d_pl, *k, *arr, *s;

	domain_load(override);

	path = socket_path ? wstrdup(socket_path) : wIPCSocketPath(NULL);
	fd = wIPCConnect(path);
	if (fd < 0) {
		fprintf(stderr, PROG ": cannot connect to %s: %s\n",
			path ? path : "(null)", strerror(errno));
		wfree(path);
		return 1;
	}
	wfree(path);

	inner = WMCreatePLArray(NULL);
	s = WMCreatePLString("info");
	WMAddToPLArray(inner, s);
	WMReleasePropList(s);
	outer = WMCreatePLArray(inner, NULL);
	WMReleasePropList(inner);
	body = WMGetPropListDescription(outer, false);
	WMReleasePropList(outer);

	if (wIPCSend(fd, WIPC_COMMAND, body, strlen(body)) < 0) {
		fprintf(stderr, PROG ": send: %s\n", strerror(errno));
		wfree(body);
		close(fd);
		return 1;
	}
	wfree(body);

	if (wIPCRecv(fd, &type, &body, &len) < 0) {
		fprintf(stderr, PROG ": recv: %s\n", strerror(errno));
		close(fd);
		return 1;
	}
	close(fd);

	d_pl = WMCreatePropListFromDescription(body);
	wfree(body);
	arr = NULL;
	if (d_pl) {
		k = WMCreatePLString("actions");
		arr = WMGetFromPLDictionary(d_pl, k);
		WMReleasePropList(k);
	}
	if (!arr || !WMIsPLArray(arr)) {
		fprintf(stderr, PROG ": server did not report action names\n");
		if (d_pl)
			WMReleasePropList(d_pl);
		return 1;
	}

	for (f = 0; f < 4; f++) {
		for (d = 0; d < WMT_DIR_COUNT; d++) {
			const char *const *names = NULL;
			int count;

			count = wmTouchConfigActions(&g.cfg, f + 2, d, &names);
			for (i = 0; i < count; i++) {
				const char *name = names[i];

				checked++;
				rc = 0;
				for (j = 0; j < WMGetPropListItemCount(arr); j++) {
					WMPropList *item = WMGetFromPLArray(arr, j);

					if (item && WMIsPLString(item)
					    && strcmp(WMGetFromPLString(item), name) == 0) {
						rc = 1;
						break;
					}
				}
				if (rc)
					printf("OK %s\n", name);
				else {
					printf("unknown action '%s'\n", name);
					unknown = 1;
				}
			}
		}
	}
	if (!checked)
		printf("no actions configured\n");
	WMReleasePropList(d_pl);
	return unknown ? 1 : 0;
}

/* ---- main loop ------------------------------------------------------ */

static void drop_pending_reply(void)
{
	pending_clear();
}

static void drain_selfpipe(void)
{
	unsigned char buf[64];

	while (read(g.selfpipe[0], buf, sizeof(buf)) > 0)
		;
}

static int event_loop(int fd, struct libinput *li)
{
	g.ipc_fd = fd;

	for (;;) {
		struct pollfd pfd[4];
		int nfds = 0, rc, self_idx, inot_idx = -1, li_idx = -1, ipc_idx;
		int timeout;
		long long remaining;

		if (g.pending_head)
			pending_expire();

		self_idx = nfds;
		pfd[nfds].fd = g.selfpipe[0];
		pfd[nfds].events = POLLIN;
		nfds++;

#ifdef HAVE_INOTIFY
		if (g.inotify_fd >= 0) {
			inot_idx = nfds;
			pfd[nfds].fd = g.inotify_fd;
			pfd[nfds].events = POLLIN;
			nfds++;
		}
#endif
		if (li) {
			li_idx = nfds;
			pfd[nfds].fd = libinput_get_fd(li);
			pfd[nfds].events = POLLIN;
			nfds++;
		}
		ipc_idx = nfds;
		pfd[nfds].fd = fd;
		pfd[nfds].events = POLLIN;
		nfds++;

		timeout = -1;
		if (g.pending_head) {
			remaining = g.pending_head->deadline - monotonic_ms();
			timeout = remaining > 0 ? (int)remaining : 0;
		}

		rc = poll(pfd, nfds, timeout);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}

		if (pfd[self_idx].revents & POLLIN) {
			drain_selfpipe();
			if (sighup_pending) {
				sighup_pending = 0;
				domain_reload();
			}
			if (shutdown_pending)
				return 0;
		}
#ifdef HAVE_INOTIFY
		if (inot_idx >= 0 && (pfd[inot_idx].revents & POLLIN)) {
			if (inotify_drain())
				domain_reload();
		}
#endif
		if (li_idx >= 0 && (pfd[li_idx].revents & POLLIN))
			process_libinput(li);

		if (pfd[ipc_idx].revents & (POLLERR | POLLHUP | POLLNVAL))
			return -1;
		if (pfd[ipc_idx].revents & POLLIN) {
			int rc = read_reply_frame();

			if (rc < 0)
				return -1;
			if (rc > 0)
				pending_pop();
		}
	}
}

/* Poll everything except the IPC socket during a reconnect backoff:
 * drain libinput so gestures are recognised and dropped (queue_action
 * logs them) instead of piling up for delivery on reconnect, and react
 * to SIGHUP and shutdown without waiting out the backoff. Returns 0
 * when it is time to retry the connection, 1 on shutdown, -1 on error. */
static int idle_wait(struct libinput *li, long long wake_at)
{
	struct pollfd pfd[3];
	long long now, slice;
	int nfds, self_idx, inot_idx, li_idx, rc;

	while (monotonic_ms() < wake_at) {
		if (shutdown_pending)
			return 1;

		now = monotonic_ms();
		slice = wake_at - now;
		nfds = 0;
		self_idx = nfds;
		pfd[nfds].fd = g.selfpipe[0];
		pfd[nfds].events = POLLIN;
		nfds++;
		inot_idx = -1;
#ifdef HAVE_INOTIFY
		if (g.inotify_fd >= 0) {
			inot_idx = nfds;
			pfd[nfds].fd = g.inotify_fd;
			pfd[nfds].events = POLLIN;
			nfds++;
		}
#endif
		li_idx = -1;
		if (li) {
			li_idx = nfds;
			pfd[nfds].fd = libinput_get_fd(li);
			pfd[nfds].events = POLLIN;
			nfds++;
		}

		rc = poll(pfd, nfds, (int)slice);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}

		if (pfd[self_idx].revents & POLLIN) {
			drain_selfpipe();
			if (sighup_pending) {
				sighup_pending = 0;
				domain_reload();
			}
			if (shutdown_pending)
				return 1;
		}
#ifdef HAVE_INOTIFY
		if (inot_idx >= 0 && (pfd[inot_idx].revents & POLLIN)) {
			if (inotify_drain())
				domain_reload();
		}
#endif
		if (li_idx >= 0 && (pfd[li_idx].revents & POLLIN))
			process_libinput(li);
	}

	return 0;
}

int main(int argc, char **argv)
{
	enum { OPT_DEBUG = 256, OPT_SOURCE, OPT_CHECK, OPT_VERSION };
	const char *seat = NULL;
	const char *device = NULL;
	const char *config = NULL;
	const char *socket_path = NULL;
	const char *source_events = NULL;
	struct libinput *li = NULL;
	struct udev *udev = NULL;
	char *path;
	unsigned int backoff;
	long long started, giveup_deadline;
	int status = 0, rc, stable, ch, check = 0;

	struct option longopts[] = {
		{ "seat",          required_argument, NULL, 's' },
		{ "device",        required_argument, NULL, 'd' },
		{ "config",        required_argument, NULL, 'c' },
		{ "socket",        required_argument, NULL, 'S' },
		{ "source-events", required_argument, NULL, OPT_SOURCE },
		{ "check",         no_argument,       NULL, OPT_CHECK },
		{ "verbose",       no_argument,       NULL, 'v' },
		{ "debug",         no_argument,       NULL, OPT_DEBUG },
		{ "help",          no_argument,       NULL, 'h' },
		{ "version",       no_argument,       NULL, OPT_VERSION },
		{ NULL,            0,                 NULL, 0 }
	};

	while ((ch = getopt_long(argc, argv, "s:d:c:S:vh", longopts, NULL)) != -1) {
		switch (ch) {
		case 's':
			seat = optarg;
			break;
		case 'd':
			device = optarg;
			break;
		case 'c':
			config = optarg;
			break;
		case 'S':
			socket_path = optarg;
			break;
		case OPT_SOURCE:
			source_events = optarg;
			break;
		case OPT_CHECK:
			check = 1;
			break;
		case 'v':
		case OPT_DEBUG:
			g.verbose = 1;
			break;
		case OPT_VERSION:
			printf(PROG " version %s\n", VERSION);
			return 0;
		case 'h':
			usage(stdout);
			return 0;
		default:
			usage(stderr);
			return 2;
		}
	}

	if (check)
		return check_actions(config, socket_path);

	g.config_override = config;

	if (pipe(g.selfpipe) < 0) {
		fprintf(stderr, PROG ": pipe: %s\n", strerror(errno));
		return 1;
	}
	fcntl(g.selfpipe[0], F_SETFL, O_NONBLOCK);
	fcntl(g.selfpipe[1], F_SETFL, O_NONBLOCK);

	signal(SIGPIPE, SIG_IGN);
	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);
	signal(SIGHUP, on_signal);

	wmTouchConfigInit(&g.cfg);
	wmTouchRecognizerInit(&g.rec);

	if (source_events) {
		domain_load(config);
		if (!g.domain) {
			fprintf(stderr, PROG ": no %s domain, nothing to do\n",
				WMTOUCH_DOMAIN);
			return 1;
		}
	} else if (device) {
		li = libinput_path_create_context(&li_interface, NULL);
		if (!li) {
			fprintf(stderr, PROG ": cannot create the libinput context\n");
			return 1;
		}
		libinput_log_set_handler(li, li_log_handler);
		if (!libinput_path_add_device(li, device)) {
			fprintf(stderr, PROG ": cannot add device %s\n", device);
			libinput_unref(li);
			return 1;
		}
	} else {
		udev = udev_new();
		if (!udev) {
			fprintf(stderr, PROG ": cannot create the udev context\n");
			return 1;
		}
		li = libinput_udev_create_context(&li_interface, NULL, udev);
		if (!li) {
			fprintf(stderr, PROG ": cannot create the libinput context\n");
			udev_unref(udev);
			return 1;
		}
		libinput_log_set_handler(li, li_log_handler);
		if (libinput_udev_assign_seat(li, seat ? seat : "seat0") != 0) {
			fprintf(stderr, PROG ": cannot assign seat %s\n",
				seat ? seat : "seat0");
			libinput_unref(li);
			udev_unref(udev);
			return 1;
		}
	}

	if (!source_events) {
		domain_load(config);
		if (!g.domain && g.verbose)
			fprintf(stderr, PROG ": no %s domain, gestures are inert\n",
				WMTOUCH_DOMAIN);
	}

#ifdef HAVE_INOTIFY
	g.inotify_fd = inotify_init1(IN_NONBLOCK);
	if (g.inotify_fd >= 0)
		inotify_watch_update();
#endif

	backoff = RECONNECT_BACKOFF_MS;
	giveup_deadline = monotonic_ms() + CONNECT_GIVEUP_MS;
	for (;;) {
		if (shutdown_pending)
			break;
		path = socket_path ? wstrdup(socket_path) : wIPCSocketPath(NULL);
		g.ipc_fd = wIPCConnect(path);
		if (g.verbose) {
			if (g.ipc_fd >= 0)
				fprintf(stderr, PROG ": connected to %s\n",
					path ? path : "(null)");
			else
				fprintf(stderr, PROG ": connect to %s failed: %s\n",
					path ? path : "(null)", strerror(errno));
		}
		wfree(path);
		stable = 0;
		rc = -1;
		if (g.ipc_fd >= 0) {
			if (source_events) {
				/* one-shot replay: process the file and exit */
				process_replay(source_events);
				close(g.ipc_fd);
				g.ipc_fd = -1;
				break;
			}
			started = monotonic_ms();
			rc = event_loop(g.ipc_fd, li);
			stable = monotonic_ms() - started >= RECONNECT_STABLE_MS;
			close(g.ipc_fd);
			g.ipc_fd = -1;
			drop_pending_reply();
		}
		if (rc == 0 || shutdown_pending)
			break;
		if (stable) {
			backoff = RECONNECT_BACKOFF_MS;
			giveup_deadline = monotonic_ms() + CONNECT_GIVEUP_MS;
		} else if (monotonic_ms() >= giveup_deadline) {
			fprintf(stderr, PROG ": no usable IPC socket after %d min, "
				"giving up\n", CONNECT_GIVEUP_MS / 60000);
			status = 1;
			break;
		}
		if (backoff < RECONNECT_MAX_MS)
			backoff *= 2;
		if (li) {
			rc = idle_wait(li, monotonic_ms() + backoff);
			if (rc != 0) {
				if (rc < 0)
					status = 1;
				break;
			}
		} else {
			usleep(backoff * 1000);
		}
	}

#ifdef HAVE_INOTIFY
	if (g.inotify_wd >= 0)
		inotify_rm_watch(g.inotify_fd, g.inotify_wd);
	if (g.inotify_fd >= 0)
		close(g.inotify_fd);
#endif
	if (li)
		libinput_unref(li);
	if (udev)
		udev_unref(udev);
	wmTouchConfigFree(&g.cfg);
	if (g.domain)
		WMReleasePropList(g.domain);
	if (g.domain_path)
		wfree(g.domain_path);
	warned_reset();
	pending_clear();
	close(g.selfpipe[0]);
	close(g.selfpipe[1]);
	return status;
}
