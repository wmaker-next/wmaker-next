/*
 *  ipc - Unix-socket control channel and event stream, server side
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  One AF_UNIX SOCK_STREAM listener per managed WScreen, framed with
 *  the WIPC header and proplist payloads, see doc/wmaker-ipc.txt for
 *  the protocol description.
 */

#include "wconfig.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <WINGs/WINGs.h>
#include <WINGs/WIPC.h>

#include "WindowMaker.h"
#include "screen.h"
#include "window.h"
#include "framewin.h"
#include "actions.h"
#include "appicon.h"
#include "application.h"
#include "defaults.h"
#include "dock.h"
#include "main.h"
#include "shutdown.h"
#include "workspace.h"
#include "ipc.h"

#define WIPC_DRAIN_TIMEOUT_MS  10000
#define WIPC_FINAL_DRAIN_MS    300
/* per-client queued-output cap, a stalled subscriber that exceeds this is
 * dropped immediately rather than waiting out the drain timer while the
 * window manager keeps reallocating its buffer */
#define WIPC_MAX_OUT_QUEUE     (4u << 20)

struct WIPCClient {
	int fd;
	WScreen *scr;
	uint32_t sub_mask;          /* bit n = subscribed to WIPC_EV_n */

	/* incremental read assembly */
	char hdr[WIPC_HEADER_LEN];
	uint32_t hdr_got;
	uint32_t in_type;
	char *in;
	uint32_t in_len, in_got;

	/* non-blocking write queue */
	char *out;
	uint32_t out_len, out_pos;

	WMHandlerID rd, wr, drain_timer;
	unsigned int dead:1;
	unsigned int busy:1;        /* inside ipc_read_cb dispatch, defer free */

	struct WIPCClient *next;
};

static const char *event_names[WIPC_EV_COUNT] = {
	"workspace", "window", "shutdown", "child_exit", "dialog", "tick",
	"screenshot"
};

/* set on first wIPCServerStart() so notification observers register once */
static int ipc_observers_registered;

/* deferred terminal action, set by exit/restart/kill-session verbs and
 * carried out after the reply for them has been queued */
static int pending_shutdown_mode = -1;
static char *pending_restart_wm;

/* ---- fd helpers ------------------------------------------------------ */

#if !defined(HAVE_ACCEPT4) || !(defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC))
static void set_cloexec(int fd)
{
	int f = fcntl(fd, F_GETFD);
	if (f >= 0)
		fcntl(fd, F_SETFD, f | FD_CLOEXEC);
}

static void set_nonblock(int fd)
{
	int f = fcntl(fd, F_GETFL);
	if (f >= 0)
		fcntl(fd, F_SETFL, f | O_NONBLOCK);
}
#endif

/* ---- server socket path --------------------------------------------- */

/* server-side path, derived from DisplayName + scr->screen so it matches
 * what wIPCSocketPath() computes on the client side, but ignoring any
 * inherited $WMAKER_IPC_SOCKET (that is a client override only) */
static char *ipc_server_path(WScreen *scr)
{
	const char *dpyname, *runtime, *colon;
	char raw[256], dir[256], *p;
	struct stat st;
	char *path;
	size_t len;

	dpyname = DisplayString(dpy);
	if (!dpyname || !*dpyname)
		dpyname = ":0";

	/* strip any existing screen suffix and append our own */
	snprintf(raw, sizeof(raw), "%s", dpyname);
	colon = strrchr(raw, ':');
	if (colon) {
		p = strchr((char *)colon, '.');
		if (p)
			*p = '\0';
	}
	wstrlcat(raw, ".", sizeof(raw));
	snprintf(raw + strlen(raw), sizeof(raw) - strlen(raw), "%d", scr->screen);

	for (p = raw; *p; p++) {
		if (!isalnum((unsigned char)*p))
			*p = '_';
	}

	runtime = getenv("XDG_RUNTIME_DIR");
	if (runtime && *runtime)
		snprintf(dir, sizeof(dir), "%s/wmaker", runtime);
	else
		snprintf(dir, sizeof(dir), "/tmp/wmaker-%lu", (unsigned long)getuid());

	if (mkdir(dir, 0700) < 0 && errno != EEXIST) {
		wwarning("cannot create IPC socket directory %s: %s", dir, strerror(errno));
		return NULL;
	}
	/* the /tmp fallback parent has a predictable name in a sticky
	 * world-writable directory, refuse it if another user got there
	 * first or replaced it with a symlink, and apply the same check
	 * to the XDG_RUNTIME_DIR case so a misconfigured runtime dir is
	 * caught the same way */
	if (lstat(dir, &st) < 0 || !S_ISDIR(st.st_mode) ||
	    st.st_uid != getuid() || (st.st_mode & 077) != 0) {
		wwarning("IPC socket directory %s has wrong owner or mode, not binding", dir);
		return NULL;
	}

	len = strlen(dir) + strlen(raw) + 6;
	path = wmalloc(len);
	snprintf(path, len, "%s/ipc.%s", dir, raw);
	return path;
}

/* ---- write queue ----------------------------------------------------- */

static void ipc_drop(struct WIPCClient *c);

static void ipc_drain_timeout(void *data)
{
	struct WIPCClient *c = data;

	c->drain_timer = NULL;
	wwarning("IPC client on fd %d stalled, dropping", c->fd);
	ipc_drop(c);
}

static void ipc_try_write(struct WIPCClient *c);

static void ipc_write_cb(int fd, int mask, void *data)
{
	(void)fd;
	(void)mask;
	ipc_try_write((struct WIPCClient *)data);
}

static void ipc_try_write(struct WIPCClient *c)
{
	ssize_t n;

	while (c->out_pos < c->out_len) {
		n = write(c->fd, c->out + c->out_pos, c->out_len - c->out_pos);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN)
				break;
			ipc_drop(c);
			return;
		}
		c->out_pos += (uint32_t)n;
	}

	if (c->out_pos == c->out_len) {
		wfree(c->out);
		c->out = NULL;
		c->out_len = c->out_pos = 0;
		if (c->wr) {
			WMDeleteInputHandler(c->wr);
			c->wr = NULL;
		}
		if (c->drain_timer) {
			WMDeleteTimerHandler(c->drain_timer);
			c->drain_timer = NULL;
		}
		return;
	}

	/* still pending, arm the write watcher and the kill timer */
	if (!c->wr)
		c->wr = WMAddInputHandler(c->fd, WIWriteMask, ipc_write_cb, c);
	if (!c->drain_timer)
		c->drain_timer = WMAddTimerHandler(WIPC_DRAIN_TIMEOUT_MS,
						   ipc_drain_timeout, c);
}

/* append a fully framed message to c's output buffer and kick a write */
static void ipc_queue(struct WIPCClient *c, uint32_t type,
		      const char *body, uint32_t len)
{
	uint32_t need;
	char *p;

	if (c->dead)
		return;

	need = c->out_len + WIPC_HEADER_LEN + len;
	if (need < c->out_len || need > WIPC_MAX_OUT_QUEUE) {
		wwarning("IPC client on fd %d output queue overflow, dropping", c->fd);
		ipc_drop(c);
		return;
	}
	c->out = wrealloc(c->out, need);
	p = c->out + c->out_len;
	memcpy(p, WIPC_MAGIC, 8);
	memcpy(p + 8, &len, 4);
	memcpy(p + 12, &type, 4);
	if (len)
		memcpy(p + WIPC_HEADER_LEN, body, len);
	c->out_len = need;

	ipc_try_write(c);
}

static void ipc_reply(struct WIPCClient *c, uint32_t type, WMPropList *pl)
{
	char *body = NULL;
	uint32_t len = 0;

	if (pl) {
		body = WMGetPropListDescription(pl, False);
		len = (uint32_t)strlen(body);
	}
	ipc_queue(c, type, body, len);
	if (body)
		wfree(body);
}

/* ---- client teardown ------------------------------------------------- */

static void ipc_drop(struct WIPCClient *c)
{
	struct WIPCClient **pp;

	if (c->dead)
		return;
	c->dead = 1;

	if (c->rd)
		WMDeleteInputHandler(c->rd);
	if (c->wr)
		WMDeleteInputHandler(c->wr);
	if (c->drain_timer)
		WMDeleteTimerHandler(c->drain_timer);
	if (c->fd >= 0)
		close(c->fd);

	for (pp = &c->scr->ipc_clients; *pp; pp = &(*pp)->next) {
		if (*pp == c) {
			*pp = c->next;
			break;
		}
	}

	wfree(c->in);
	wfree(c->out);
	c->in = NULL;
	c->out = NULL;
	/* when the drop is reached from inside this client's own request
	 * dispatch the read callback still holds c on its stack, let it
	 * do the free on the way out instead */
	if (!c->busy)
		wfree(c);
}

/* ---- broadcast ------------------------------------------------------- */

static void ipc_broadcast_buf(WScreen *only, uint32_t ev,
			      const char *body, uint32_t len)
{
	uint32_t type = WIPC_EVENT_BIT | ev;
	uint32_t bit = 1u << ev;
	struct WIPCClient *c, *next;
	WScreen *scr;
	int i;

	for (i = 0; i < w_global.screen_count; i++) {
		scr = wScreenWithNumber(i);
		if (!scr || (only && scr != only))
			continue;
		for (c = scr->ipc_clients; c; c = next) {
			next = c->next;
			if (c->sub_mask & bit)
				ipc_queue(c, type, body, len);
		}
	}
}

void wIPCBroadcast(WScreen *scr, uint32_t ev, WMPropList *pl)
{
	char *body = NULL;
	uint32_t len = 0;

	if (ev >= WIPC_EV_COUNT)
		return;
	if (pl) {
		body = WMGetPropListDescription(pl, False);
		len = (uint32_t)strlen(body);
	}
	ipc_broadcast_buf(scr, ev, body, len);
	if (body)
		wfree(body);
}

/* ---- proplist construction helpers ---------------------------------- */

static WMPropList *pl_string(const char *s)
{
	return WMCreatePLString(s ? s : "");
}

static WMPropList *pl_int(long v)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "%ld", v);
	return WMCreatePLString(buf);
}

static WMPropList *pl_bool(int v)
{
	return WMCreatePLString(v ? "YES" : "NO");
}

static WMPropList *pl_result(int ok, const char *err)
{
	WMPropList *d, *k, *v;

	d = WMCreatePLDictionary(NULL, NULL);
	k = WMCreatePLString("success");
	v = pl_bool(ok);
	WMPutInPLDictionary(d, k, v);
	WMReleasePropList(k);
	WMReleasePropList(v);
	if (err) {
		k = WMCreatePLString("error");
		v = pl_string(err);
		WMPutInPLDictionary(d, k, v);
		WMReleasePropList(k);
		WMReleasePropList(v);
	}
	return d;
}

static void pl_dict_put(WMPropList *d, const char *key, WMPropList *val)
{
	WMPropList *k = WMCreatePLString(key);
	WMPutInPLDictionary(d, k, val);
	WMReleasePropList(k);
	WMReleasePropList(val);
}

/* ---- COMMAND verbs --------------------------------------------------- */

typedef enum { VERB_OK, VERB_FAIL, VERB_FINAL } verb_rc;

/* a verb may set *reply to a dict it owns, in which case that dict is
 * sent verbatim instead of the default {success=YES} wrapper, this is
 * how `info` returns the version dict */
typedef verb_rc (*verb_fn)(WScreen *, WMPropList *args,
			   char **err, WMPropList **reply);

static verb_rc verb_reconfigure(WScreen *scr, WMPropList *args,
				char **err, WMPropList **reply)
{
	(void)scr;
	(void)args;
	(void)err;
	(void)reply;
	wDefaultsCheckDomains(NULL);
	return VERB_OK;
}

static verb_rc verb_restart(WScreen *scr, WMPropList *args,
			    char **err, WMPropList **reply)
{
	WMPropList *a;

	(void)scr;
	(void)err;
	(void)reply;
	pending_shutdown_mode = WSRestartPreparationMode;
	if (args && WMGetPropListItemCount(args) > 1) {
		a = WMGetFromPLArray(args, 1);
		if (a && WMIsPLString(a))
			pending_restart_wm = wstrdup(WMGetFromPLString(a));
	}
	return VERB_FINAL;
}

static verb_rc verb_exit(WScreen *scr, WMPropList *args,
			 char **err, WMPropList **reply)
{
	(void)scr;
	(void)args;
	(void)err;
	(void)reply;
	pending_shutdown_mode = WSExitMode;
	return VERB_FINAL;
}

static verb_rc verb_kill_session(WScreen *scr, WMPropList *args,
				 char **err, WMPropList **reply)
{
	(void)scr;
	(void)args;
	(void)err;
	(void)reply;
	pending_shutdown_mode = WSKillMode;
	return VERB_FINAL;
}

static verb_rc verb_workspace(WScreen *scr, WMPropList *args,
			      char **err, WMPropList **reply)
{
	WMPropList *a;
	const char *which;
	long n;
	char *end;

	(void)reply;
	if (!args || WMGetPropListItemCount(args) < 2) {
		*err = wstrdup("workspace: missing argument");
		return VERB_FAIL;
	}
	a = WMGetFromPLArray(args, 1);
	if (!a || !WMIsPLString(a)) {
		*err = wstrdup("workspace: argument must be a string");
		return VERB_FAIL;
	}
	which = WMGetFromPLString(a);

	if (strcmp(which, "next") == 0) {
		wWorkspaceRelativeChange(scr, 1);
	} else if (strcmp(which, "prev") == 0) {
		wWorkspaceRelativeChange(scr, -1);
	} else if (strcmp(which, "last") == 0) {
		wWorkspaceChange(scr, scr->last_workspace);
	} else {
		n = strtol(which, &end, 10);
		if (*end || n < 0 || n >= scr->workspace_count) {
			*err = wstrdup("workspace: invalid index");
			return VERB_FAIL;
		}
		wWorkspaceChange(scr, (int)n);
	}
	XSync(dpy, False);
	return VERB_OK;
}

static verb_rc verb_dock_launch(WScreen *scr, WMPropList *args,
				char **err, WMPropList **reply)
{
	WMPropList *a;
	WAppIcon *btn;
	long slot;
	char *end;
	int i;

	(void)reply;
	if (!args || WMGetPropListItemCount(args) < 2) {
		*err = wstrdup("dock-launch: missing slot index");
		return VERB_FAIL;
	}
	a = WMGetFromPLArray(args, 1);
	if (!a || !WMIsPLString(a)) {
		*err = wstrdup("dock-launch: slot must be a string");
		return VERB_FAIL;
	}
	slot = strtol(WMGetFromPLString(a), &end, 10);
	if (*end) {
		*err = wstrdup("dock-launch: invalid slot index");
		return VERB_FAIL;
	}
	if (!scr->dock) {
		*err = wstrdup("dock-launch: no dock on this screen");
		return VERB_FAIL;
	}

	btn = NULL;
	for (i = 0; i < scr->dock->max_icons; i++) {
		if (scr->dock->icon_array[i] &&
		    scr->dock->icon_array[i]->yindex == slot) {
			btn = scr->dock->icon_array[i];
			break;
		}
	}
	if (!btn || !btn->command) {
		*err = wstrdup("dock-launch: no docked command at that slot");
		return VERB_FAIL;
	}

	/* same path a double-click or auto-launch takes, so the death
	 * handler is registered with source=dock and slot=yindex */
	wDockLaunchWithState(btn, NULL);
	XSync(dpy, False);
	return VERB_OK;
}

static verb_rc verb_show_all(WScreen *scr, WMPropList *args,
			     char **err, WMPropList **reply)
{
	(void)args;
	(void)err;
	(void)reply;
	wShowAllWindows(scr);
	return VERB_OK;
}

static verb_rc verb_arrange_icons(WScreen *scr, WMPropList *args,
				  char **err, WMPropList **reply)
{
	(void)args;
	(void)err;
	(void)reply;
	wArrangeIcons(scr, True);
	return VERB_OK;
}

static int parse_xid(WMPropList *args, Window *out, char **err)
{
	WMPropList *a;
	unsigned long v;
	char *end;

	if (!args || WMGetPropListItemCount(args) < 2) {
		*err = wstrdup("missing window id");
		return 0;
	}
	a = WMGetFromPLArray(args, 1);
	if (!a || !WMIsPLString(a)) {
		*err = wstrdup("window id must be a string");
		return 0;
	}
	v = strtoul(WMGetFromPLString(a), &end, 0);
	if (*end) {
		*err = wstrdup("invalid window id");
		return 0;
	}
	*out = (Window)v;
	return 1;
}

static verb_rc verb_hide_app(WScreen *scr, WMPropList *args,
			     char **err, WMPropList **reply)
{
	Window xid;
	WWindow *wwin;
	WApplication *wapp;

	(void)scr;
	(void)reply;
	if (!parse_xid(args, &xid, err))
		return VERB_FAIL;
	wwin = wWindowFor(xid);
	if (!wwin) {
		*err = wstrdup("window not managed");
		return VERB_FAIL;
	}
	wapp = wApplicationOf(wwin->main_window);
	if (wapp)
		wHideApplication(wapp);
	return VERB_OK;
}

static verb_rc verb_hide_others(WScreen *scr, WMPropList *args,
				char **err, WMPropList **reply)
{
	Window xid;
	WWindow *wwin;

	(void)scr;
	(void)reply;
	if (!parse_xid(args, &xid, err))
		return VERB_FAIL;
	wwin = wWindowFor(xid);
	if (!wwin) {
		*err = wstrdup("window not managed");
		return VERB_FAIL;
	}
	wHideOtherApplications(wwin);
	return VERB_OK;
}

static WMPropList *build_version(WScreen *scr)
{
	WMPropList *d = WMCreatePLDictionary(NULL, NULL);

	pl_dict_put(d, "version", pl_string(VERSION));
	pl_dict_put(d, "display", pl_string(DisplayString(dpy)));
	pl_dict_put(d, "screen", pl_int(scr->screen));
	pl_dict_put(d, "pid", pl_int(getpid()));
	pl_dict_put(d, "socket", pl_string(scr->ipc_path));
	return d;
}

static verb_rc verb_info(WScreen *scr, WMPropList *args,
			 char **err, WMPropList **reply)
{
	(void)args;
	(void)err;
	*reply = build_version(scr);
	pl_dict_put(*reply, "success", pl_bool(1));
	return VERB_OK;
}

static const struct {
	const char *name;
	verb_fn fn;
} verbs[] = {
	{ "reconfigure",      verb_reconfigure },
	{ "restart",          verb_restart },
	{ "exit",             verb_exit },
	{ "kill-session",     verb_kill_session },
	{ "workspace",        verb_workspace },
	{ "dock-launch",      verb_dock_launch },
	{ "show-all",         verb_show_all },
	{ "arrange-icons",    verb_arrange_icons },
	{ "hide-application", verb_hide_app },
	{ "hide-others",      verb_hide_others },
	{ "info",             verb_info },
	{ NULL, NULL }
};

/* ---- request dispatch ------------------------------------------------ */

static WMPropList *handle_one_command(WScreen *scr, WMPropList *cmd, int *final)
{
	WMPropList *verb_pl, *res, *reply = NULL;
	const char *name;
	char *err = NULL;
	char msg[128];
	verb_rc rc;
	int i;

	if (!cmd || !WMIsPLArray(cmd) || WMGetPropListItemCount(cmd) < 1)
		return pl_result(0, "malformed command");

	verb_pl = WMGetFromPLArray(cmd, 0);
	if (!verb_pl || !WMIsPLString(verb_pl))
		return pl_result(0, "verb must be a string");
	name = WMGetFromPLString(verb_pl);

	for (i = 0; verbs[i].name; i++) {
		if (strcmp(verbs[i].name, name) == 0)
			break;
	}
	if (!verbs[i].name) {
		snprintf(msg, sizeof(msg), "unknown command '%s'", name);
		return pl_result(0, msg);
	}

	rc = verbs[i].fn(scr, cmd, &err, &reply);
	if (rc == VERB_FINAL)
		*final = 1;
	if (reply)
		res = reply;
	else
		res = pl_result(rc != VERB_FAIL, err);
	wfree(err);
	return res;
}

static void handle_command(struct WIPCClient *c, const char *body)
{
	WMPropList *outer, *replies, *r;
	int n, i, final = 0;

	outer = body ? WMCreatePropListFromDescription(body) : NULL;
	if (!outer || !WMIsPLArray(outer)) {
		r = pl_result(0, "payload must be an array of command arrays");
		ipc_reply(c, WIPC_COMMAND, r);
		WMReleasePropList(r);
		if (outer)
			WMReleasePropList(outer);
		return;
	}

	n = WMGetPropListItemCount(outer);
	replies = WMCreatePLArray(NULL);
	for (i = 0; i < n; i++) {
		r = handle_one_command(c->scr, WMGetFromPLArray(outer, i), &final);
		WMAddToPLArray(replies, r);
		WMReleasePropList(r);
		if (final)
			break;
	}

	/* single command, unwrap the array so wmakermsg's exit-status
	 * mapping has a stable shape to look at */
	if (n == 1)
		ipc_reply(c, WIPC_COMMAND, WMGetFromPLArray(replies, 0));
	else
		ipc_reply(c, WIPC_COMMAND, replies);

	WMReleasePropList(replies);
	WMReleasePropList(outer);
}

static void handle_subscribe(struct WIPCClient *c, const char *body)
{
	WMPropList *arr, *e, *r, *tick;
	const char *name;
	char *tbody;
	int n, i, j, want_tick = 0;

	arr = body ? WMCreatePropListFromDescription(body) : NULL;
	if (!arr || !WMIsPLArray(arr)) {
		r = pl_result(0, "payload must be an array of event names");
		ipc_reply(c, WIPC_SUBSCRIBE, r);
		WMReleasePropList(r);
		if (arr)
			WMReleasePropList(arr);
		return;
	}

	n = WMGetPropListItemCount(arr);
	for (i = 0; i < n; i++) {
		e = WMGetFromPLArray(arr, i);
		if (!e || !WMIsPLString(e))
			continue;
		name = WMGetFromPLString(e);
		for (j = 0; j < WIPC_EV_COUNT; j++) {
			if (strcmp(event_names[j], name) == 0) {
				c->sub_mask |= (1u << j);
				if (j == WIPC_EV_TICK)
					want_tick = 1;
				break;
			}
		}
	}
	WMReleasePropList(arr);

	r = pl_result(1, NULL);
	ipc_reply(c, WIPC_SUBSCRIBE, r);
	WMReleasePropList(r);

	if (want_tick) {
		tick = WMCreatePLDictionary(NULL, NULL);
		pl_dict_put(tick, "first", pl_bool(1));
		pl_dict_put(tick, "payload", pl_string(""));
		tbody = WMGetPropListDescription(tick, False);
		ipc_queue(c, WIPC_EVENT_BIT | WIPC_EV_TICK,
			  tbody, (uint32_t)strlen(tbody));
		wfree(tbody);
		WMReleasePropList(tick);
	}
}

static void handle_get_workspaces(struct WIPCClient *c)
{
	WMPropList *arr, *d;
	int i;

	arr = WMCreatePLArray(NULL);
	for (i = 0; i < c->scr->workspace_count; i++) {
		d = WMCreatePLDictionary(NULL, NULL);
		pl_dict_put(d, "name", pl_string(c->scr->workspaces[i]->name));
		pl_dict_put(d, "index", pl_int(i));
		pl_dict_put(d, "current", pl_bool(i == c->scr->current_workspace));
		WMAddToPLArray(arr, d);
		WMReleasePropList(d);
	}
	ipc_reply(c, WIPC_GET_WORKSPACES, arr);
	WMReleasePropList(arr);
}

static void handle_get_windows(struct WIPCClient *c)
{
	WMPropList *arr, *d;
	WWindow *wwin;
	char xid[32];

	arr = WMCreatePLArray(NULL);
	for (wwin = c->scr->focused_window; wwin; wwin = wwin->prev) {
		d = WMCreatePLDictionary(NULL, NULL);
		snprintf(xid, sizeof(xid), "0x%lx", (unsigned long)wwin->client_win);
		pl_dict_put(d, "id", pl_string(xid));
		pl_dict_put(d, "class", pl_string(wwin->wm_class));
		pl_dict_put(d, "instance", pl_string(wwin->wm_instance));
		pl_dict_put(d, "title", pl_string(wwin->frame ? wwin->frame->title : NULL));
		pl_dict_put(d, "workspace", pl_int(wwin->frame ? wwin->frame->workspace : 0));
		WMAddToPLArray(arr, d);
		WMReleasePropList(d);
	}
	ipc_reply(c, WIPC_GET_WINDOWS, arr);
	WMReleasePropList(arr);
}

static void handle_send_tick(struct WIPCClient *c, const char *body)
{
	WMPropList *r, *ev, *pl;
	const char *payload = "";

	/* the wire body is a proplist description like every other frame,
	 * the documented payload type is a single proplist string, accept
	 * a missing or non-string body as the empty payload */
	pl = (body && *body) ? WMCreatePropListFromDescription(body) : NULL;
	if (pl && WMIsPLString(pl))
		payload = WMGetFromPLString(pl);

	ev = WMCreatePLDictionary(NULL, NULL);
	pl_dict_put(ev, "first", pl_bool(0));
	pl_dict_put(ev, "payload", pl_string(payload));
	wIPCBroadcast(NULL, WIPC_EV_TICK, ev);
	WMReleasePropList(ev);
	if (pl)
		WMReleasePropList(pl);

	r = pl_result(1, NULL);
	ipc_reply(c, WIPC_SEND_TICK, r);
	WMReleasePropList(r);
}

static void ipc_dispatch(struct WIPCClient *c, uint32_t type,
			 const char *body, uint32_t len)
{
	WMPropList *r;

	(void)len;

	switch (type) {
	case WIPC_COMMAND:
		handle_command(c, body);
		break;
	case WIPC_SUBSCRIBE:
		handle_subscribe(c, body);
		break;
	case WIPC_GET_WORKSPACES:
		handle_get_workspaces(c);
		break;
	case WIPC_GET_WINDOWS:
		handle_get_windows(c);
		break;
	case WIPC_GET_VERSION:
		r = build_version(c->scr);
		ipc_reply(c, WIPC_GET_VERSION, r);
		WMReleasePropList(r);
		break;
	case WIPC_SEND_TICK:
		handle_send_tick(c, body);
		break;
	default:
		r = pl_result(0, "unknown message type");
		ipc_reply(c, type, r);
		WMReleasePropList(r);
		break;
	}
}

/* ---- read assembly --------------------------------------------------- */

static int ipc_validate_header(struct WIPCClient *c)
{
	uint32_t l;

	if (memcmp(c->hdr, WIPC_MAGIC, 8) != 0)
		return 0;
	memcpy(&l, c->hdr + 8, 4);
	memcpy(&c->in_type, c->hdr + 12, 4);
	if (l > WIPC_MAX_PAYLOAD)
		return 0;
	c->in_len = l;
	c->in_got = 0;
	c->in = wmalloc((size_t)l + 1);
	return 1;
}

static void ipc_read_cb(int fd, int mask, void *data)
{
	struct WIPCClient *c = data;
	ssize_t n;
	uint32_t i, btype, blen;
	int mode;
	char *body, *wm;

	(void)mask;

	for (;;) {
		if (c->hdr_got < WIPC_HEADER_LEN) {
			n = read(fd, c->hdr + c->hdr_got,
				 WIPC_HEADER_LEN - c->hdr_got);
			if (n < 0) {
				if (errno == EINTR)
					continue;
				if (errno == EAGAIN)
					return;
				ipc_drop(c);
				return;
			}
			if (n == 0) {
				ipc_drop(c);
				return;
			}
			c->hdr_got += (uint32_t)n;
			if (c->hdr_got < WIPC_HEADER_LEN)
				continue;
			if (!ipc_validate_header(c)) {
				ipc_drop(c);
				return;
			}
		}

		while (c->in_got < c->in_len) {
			n = read(fd, c->in + c->in_got, c->in_len - c->in_got);
			if (n < 0) {
				if (errno == EINTR)
					continue;
				if (errno == EAGAIN)
					return;
				ipc_drop(c);
				return;
			}
			if (n == 0) {
				ipc_drop(c);
				return;
			}
			c->in_got += (uint32_t)n;
		}

		c->in[c->in_len] = '\0';
		/* embedded NUL would let strlen-based parsing diverge from
		 * the wire length, treat it as a protocol violation */
		for (i = 0; i < c->in_len; i++) {
			if (c->in[i] == '\0') {
				ipc_drop(c);
				return;
			}
		}

		/* detach the assembled frame from the client before dispatch
		 * so a drop reached from inside the handler (write error on
		 * the reply, output-queue overflow) cannot double-free it,
		 * and mark the client busy so the drop defers wfree(c) */
		body = c->in;
		btype = c->in_type;
		blen = c->in_len;
		c->in = NULL;
		c->in_len = c->in_got = 0;
		c->hdr_got = 0;

		c->busy = 1;
		ipc_dispatch(c, btype, blen ? body : NULL, blen);
		c->busy = 0;
		wfree(body);

		if (c->dead) {
			wfree(c);
			return;
		}

		if (pending_shutdown_mode >= 0) {
			mode = pending_shutdown_mode;
			wm = pending_restart_wm;
			pending_shutdown_mode = -1;
			pending_restart_wm = NULL;
			/* wIPCServerStop() runs from Shutdown() and does the
			 * final drain, so the success reply queued above is
			 * delivered there */
			Shutdown(mode);
			if (mode == WSRestartPreparationMode)
				Restart(wm, False);
			wfree(wm);
			return;
		}
	}
}

/* ---- accept ---------------------------------------------------------- */

static void ipc_accept_cb(int fd, int mask, void *data)
{
	WScreen *scr = data;
	struct WIPCClient *c;
	int cfd;

	(void)mask;

#ifdef HAVE_ACCEPT4
	cfd = accept4(fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
#else
	cfd = accept(fd, NULL, NULL);
#endif
	if (cfd < 0)
		return;
#ifndef HAVE_ACCEPT4
	set_cloexec(cfd);
	set_nonblock(cfd);
#endif

	c = wmalloc(sizeof(*c));
	c->fd = cfd;
	c->scr = scr;
	c->rd = WMAddInputHandler(cfd, WIReadMask, ipc_read_cb, c);

	c->next = scr->ipc_clients;
	scr->ipc_clients = c;
}

/* ---- start / stop ---------------------------------------------------- */

static void ipc_register_observers(void);

void wIPCServerStart(WScreen *scr)
{
	struct sockaddr_un addr;
	char *path;
	int fd, probe;

	scr->ipc_fd = -1;
	scr->ipc_path = NULL;
	scr->ipc_listen = NULL;
	scr->ipc_clients = NULL;

	path = ipc_server_path(scr);
	if (!path)
		return;
	if (strlen(path) >= sizeof(addr.sun_path)) {
		wwarning("IPC socket path too long: %s", path);
		wfree(path);
		return;
	}

	/* probe-connect, refuse to steal a live holder, unlink if stale */
	probe = wIPCConnect(path);
	if (probe >= 0) {
		close(probe);
		wwarning("IPC socket %s is held by another process, not binding", path);
		wfree(path);
		return;
	}
	unlink(path);

#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
#else
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd >= 0) {
		set_cloexec(fd);
		set_nonblock(fd);
	}
#endif
	if (fd < 0) {
		wwarning("cannot create IPC socket: %s", strerror(errno));
		wfree(path);
		return;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		wwarning("cannot bind IPC socket %s: %s", path, strerror(errno));
		close(fd);
		wfree(path);
		return;
	}
	chmod(path, 0600);

	if (listen(fd, 8) < 0) {
		wwarning("cannot listen on IPC socket %s: %s", path, strerror(errno));
		close(fd);
		unlink(path);
		wfree(path);
		return;
	}

	scr->ipc_fd = fd;
	scr->ipc_path = path;
	scr->ipc_listen = WMAddInputHandler(fd, WIReadMask, ipc_accept_cb, scr);

	if (!ipc_observers_registered) {
		ipc_register_observers();
		ipc_observers_registered = 1;
	}
}

static void ipc_final_drain(WScreen *scr)
{
	struct WIPCClient *c;
	struct pollfd pfd;
	int rounds = WIPC_FINAL_DRAIN_MS / 10;
	int pending;
	ssize_t n;

	while (rounds-- > 0) {
		pending = 0;
		for (c = scr->ipc_clients; c; c = c->next) {
			while (c->out_pos < c->out_len) {
				n = write(c->fd, c->out + c->out_pos,
					  c->out_len - c->out_pos);
				if (n <= 0)
					break;
				c->out_pos += (uint32_t)n;
			}
			if (c->out_pos < c->out_len)
				pending = 1;
		}
		if (!pending)
			return;
		pfd.fd = -1;
		pfd.events = 0;
		poll(&pfd, 0, 10);
	}
}

void wIPCServerStop(WScreen *scr, const char *mode)
{
	WMPropList *ev;

	if (!scr || scr->ipc_fd < 0)
		return;

	if (scr->ipc_listen) {
		WMDeleteInputHandler(scr->ipc_listen);
		scr->ipc_listen = NULL;
	}

	ev = WMCreatePLDictionary(NULL, NULL);
	pl_dict_put(ev, "change", pl_string(mode));
	/* screen-scoped here, every screen's stop is called separately
	 * from Shutdown() so subscribers on each socket see exactly one */
	wIPCBroadcast(scr, WIPC_EV_SHUTDOWN, ev);
	WMReleasePropList(ev);

	ipc_final_drain(scr);

	while (scr->ipc_clients)
		ipc_drop(scr->ipc_clients);

	close(scr->ipc_fd);
	scr->ipc_fd = -1;
	unlink(scr->ipc_path);
	wfree(scr->ipc_path);
	scr->ipc_path = NULL;
}

/* ---- event emitters -------------------------------------------------- */

void wIPCEmitChildExit(int pid, int wait_status, const char *source, int slot)
{
	WMPropList *d;

	d = WMCreatePLDictionary(NULL, NULL);
	pl_dict_put(d, "pid", pl_int(pid));
	pl_dict_put(d, "wait_status", pl_int(wait_status));
	if (WIFEXITED(wait_status))
		pl_dict_put(d, "status", pl_int(WEXITSTATUS(wait_status)));
	if (WIFSIGNALED(wait_status))
		pl_dict_put(d, "signal", pl_int(WTERMSIG(wait_status)));
	pl_dict_put(d, "source", pl_string(source));
	if (slot >= 0)
		pl_dict_put(d, "slot", pl_int(slot));
	wIPCBroadcast(NULL, WIPC_EV_CHILD_EXIT, d);
	WMReleasePropList(d);
}

void wIPCEmitDialog(WScreen *scr, const char *kind,
		    const char *title, const char *text)
{
	WMPropList *d;

	(void)scr;
	d = WMCreatePLDictionary(NULL, NULL);
	pl_dict_put(d, "kind", pl_string(kind));
	pl_dict_put(d, "title", pl_string(title));
	pl_dict_put(d, "text", pl_string(text));
	/* process-scoped, a dialog is not tied to the screen that
	 * happened to raise it */
	wIPCBroadcast(NULL, WIPC_EV_DIALOG, d);
	WMReleasePropList(d);
}

static const char *screenshot_mode_name(int mode)
{
	switch (mode) {
	case PRINT_WINDOW:
		return "window";
	case PRINT_PARTIAL:
		return "partial";
	case PRINT_SCREEN:
	default:
		return "screen";
	}
}

void wIPCEmitScreenshot(WScreen *scr, const char *path, int mode)
{
	WMPropList *d;

	d = WMCreatePLDictionary(NULL, NULL);
	pl_dict_put(d, "path", pl_string(path));
	pl_dict_put(d, "mode", pl_string(screenshot_mode_name(mode)));
	pl_dict_put(d, "screen", pl_int(scr->screen));
	pl_dict_put(d, "workspace", pl_int(scr->current_workspace));
	wIPCBroadcast(scr, WIPC_EV_SCREENSHOT, d);
	WMReleasePropList(d);
}

/* ---- notification observers ----------------------------------------- */

static void emit_workspace(WScreen *scr, const char *change, int idx)
{
	WMPropList *d;

	d = WMCreatePLDictionary(NULL, NULL);
	pl_dict_put(d, "change", pl_string(change));
	pl_dict_put(d, "index", pl_int(idx));
	if (idx >= 0 && idx < scr->workspace_count && scr->workspaces[idx])
		pl_dict_put(d, "name", pl_string(scr->workspaces[idx]->name));
	wIPCBroadcast(scr, WIPC_EV_WORKSPACE, d);
	WMReleasePropList(d);
}

static void obs_workspace(void *self, WMNotification *notif)
{
	WScreen *scr = WMGetNotificationObject(notif);
	const char *name = WMGetNotificationName(notif);
	int idx = (int)(uintptr_t)WMGetNotificationClientData(notif);

	(void)self;
	if (!scr)
		return;
	if (strcmp(name, WMNWorkspaceChanged) == 0)
		emit_workspace(scr, "focus", idx);
	else if (strcmp(name, WMNWorkspaceCreated) == 0)
		emit_workspace(scr, "create", idx);
	else if (strcmp(name, WMNWorkspaceDestroyed) == 0)
		emit_workspace(scr, "destroy", idx);
	else if (strcmp(name, WMNWorkspaceNameChanged) == 0)
		emit_workspace(scr, "rename", idx);
}

static void emit_window(WWindow *wwin, const char *change)
{
	WMPropList *d;
	char xid[32];

	if (!wwin)
		return;
	d = WMCreatePLDictionary(NULL, NULL);
	pl_dict_put(d, "change", pl_string(change));
	snprintf(xid, sizeof(xid), "0x%lx", (unsigned long)wwin->client_win);
	pl_dict_put(d, "id", pl_string(xid));
	wIPCBroadcast(wwin->screen_ptr, WIPC_EV_WINDOW, d);
	WMReleasePropList(d);
}

static void obs_window(void *self, WMNotification *notif)
{
	WWindow *wwin = WMGetNotificationObject(notif);
	const char *name = WMGetNotificationName(notif);
	int gained;

	(void)self;
	if (strcmp(name, WMNManaged) == 0) {
		emit_window(wwin, "manage");
	} else if (strcmp(name, WMNUnmanaged) == 0) {
		emit_window(wwin, "unmanage");
	} else if (strcmp(name, WMNChangedFocus) == 0) {
		/* the notification carries True for focus-in and False for
		 * focus-out, only the gain is interesting to subscribers,
		 * otherwise the losing window can be reported as "focus"
		 * after the gaining one */
		gained = WMGetNotificationClientData(notif) != NULL;
		if (gained)
			emit_window(wwin, "focus");
	} else if (strcmp(name, WMNChangedName) == 0) {
		emit_window(wwin, "title");
	}
}

static void ipc_register_observers(void)
{
	static int sentinel;

	WMAddNotificationObserver(obs_workspace, &sentinel, WMNWorkspaceChanged, NULL);
	WMAddNotificationObserver(obs_workspace, &sentinel, WMNWorkspaceCreated, NULL);
	WMAddNotificationObserver(obs_workspace, &sentinel, WMNWorkspaceDestroyed, NULL);
	WMAddNotificationObserver(obs_workspace, &sentinel, WMNWorkspaceNameChanged, NULL);
	WMAddNotificationObserver(obs_window, &sentinel, WMNManaged, NULL);
	WMAddNotificationObserver(obs_window, &sentinel, WMNUnmanaged, NULL);
	WMAddNotificationObserver(obs_window, &sentinel, WMNChangedFocus, NULL);
	WMAddNotificationObserver(obs_window, &sentinel, WMNChangedName, NULL);
}
