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
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <https://www.gnu.org/licenses/>.
 *
 *  One AF_UNIX SOCK_STREAM listener per managed WScreen, framed with
 *  the WIPC header and proplist payloads, see doc/wmaker-ipc.txt for
 *  the protocol description.
 */

#include "wconfig.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <math.h>
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
#include "xinerama.h"
#include "framewin.h"
#include "actions.h"
#include "appicon.h"
#include "application.h"
#include "client.h"
#include "defaults.h"
#include "dock.h"
#include "event.h"
#include "keybind.h"
#include "main.h"
#include "shutdown.h"
#include "workspace.h"
#include "wmspec.h"
#include "layout.h"
#ifdef VIRTUAL_DESKTOP
#include "vdesk.h"
#endif
#include "xsmp.h"
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
	"screenshot", "display_power", "sound"
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

	dpyname = w_global.display_name;
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
		WMDeleteTimerHandlerPtr(&c->drain_timer);
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
		      const char *body, size_t len)
{
	uint32_t need, len32;
	char *p;

	if (c->dead)
		return;

	/* independent cap on the size_t len before narrowing or adding so
	 * a >4 GiB body on LP64 is rejected rather than silently truncated
	 * and so WIPC_HEADER_LEN + len cannot wrap to a small value */
	if (len > WIPC_MAX_PAYLOAD) {
		wwarning("IPC client on fd %d payload exceeds %u bytes, dropping", c->fd,
			 WIPC_MAX_PAYLOAD);
		ipc_drop(c);
		return;
	}
	len32 = (uint32_t)len;

	need = c->out_len + WIPC_HEADER_LEN + len32;
	if (need < c->out_len || need > WIPC_MAX_OUT_QUEUE) {
		wwarning("IPC client on fd %d output queue overflow, dropping", c->fd);
		ipc_drop(c);
		return;
	}
	c->out = wrealloc(c->out, need);
	p = c->out + c->out_len;
	memcpy(p, WIPC_MAGIC, 8);
	memcpy(p + 8, &len32, 4);
	memcpy(p + 12, &type, 4);
	if (len32)
		memcpy(p + WIPC_HEADER_LEN, body, len32);
	c->out_len = need;

	ipc_try_write(c);
}

static void ipc_reply(struct WIPCClient *c, uint32_t type, WMPropList *pl)
{
	char *body = NULL;
	size_t len = 0;

	if (pl) {
		body = WMGetPropListDescription(pl, false);
		len = strlen(body);
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

bool wIPCIsSubscribed(WScreen *only, uint32_t ev)
{
	uint32_t bit;
	struct WIPCClient *c;
	WScreen *scr;
	int i;

	if (ev >= WIPC_EV_COUNT)
		return false;
	bit = 1u << ev;

	for (i = 0; i < wdisp->screen_count; i++) {
		scr = wScreenWithNumber(i);
		if (!scr || (only && scr != only))
			continue;
		for (c = scr->ipc_clients; c; c = c->next) {
			if (c->sub_mask & bit)
				return true;
		}
	}
	return false;
}

static void ipc_broadcast_buf(WScreen *only, uint32_t ev,
			      const char *body, size_t len)
{
	uint32_t type = WIPC_EVENT_BIT | ev;
	uint32_t bit = 1u << ev;
	struct WIPCClient *c, *next;
	WScreen *scr;
	int i;

	for (i = 0; i < wdisp->screen_count; i++) {
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
	size_t len = 0;

	if (ev >= WIPC_EV_COUNT)
		return;
	if (!wIPCIsSubscribed(scr, ev))
		return;
	if (pl) {
		body = WMGetPropListDescription(pl, false);
		len = strlen(body);
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

#define WIPC_TREE_FIELD_MAX 1024

static WMPropList *pl_string_capped(const char *s)
{
	char buf[WIPC_TREE_FIELD_MAX + 1];
	size_t n;

	if (!s)
		return pl_string(NULL);
	n = strlen(s);
	if (n > WIPC_TREE_FIELD_MAX)
		n = WIPC_TREE_FIELD_MAX;
	memcpy(buf, s, n);
	buf[n] = '\0';
	return WMCreatePLString(buf);
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
	WMPropList *k;

	if (!val)
		return;
	k = WMCreatePLString(key);
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
		if (a && WMIsPLString(a)) {
			wfree(pending_restart_wm);
			pending_restart_wm = wstrdup(WMGetFromPLString(a));
		}
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
	if (!*which) {
		*err = wstrdup("workspace: invalid index");
		return VERB_FAIL;
	}

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
	wXSync(wdisp->conn);
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
		    wDockIconSlot(scr->dock->icon_array[i]) == slot) {
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
	wXSync(wdisp->conn);
	return VERB_OK;
}

static int parse_xid_arg(WMPropList *args, int idx, xcb_window_t *out, char **err)
{
	WMPropList *a;
	unsigned long v;
	char *end;

	if (!args || WMGetPropListItemCount(args) <= idx) {
		*err = wstrdup("missing window id");
		return 0;
	}
	a = WMGetFromPLArray(args, idx);
	if (!a || !WMIsPLString(a)) {
		*err = wstrdup("window id must be a string");
		return 0;
	}
	v = strtoul(WMGetFromPLString(a), &end, 0);
	if (*end) {
		*err = wstrdup("invalid window id");
		return 0;
	}
	*out = (xcb_window_t)v;
	return 1;
}

static int parse_xid(WMPropList *args, xcb_window_t *out, char **err)
{
	return parse_xid_arg(args, 1, out, err);
}

static verb_rc verb_window_workspace(WScreen *scr, WMPropList *args,
				     char **err, WMPropList **reply)
{
	WMPropList *a;
	xcb_window_t xid;
	WWindow *wwin;
	const char *spec;
	const char *number;
	long n;
	char *end;

	(void)scr;
	(void)reply;
	if (!parse_xid(args, &xid, err))
		return VERB_FAIL;
	wwin = wWindowFor(xid);
	if (!wwin || !wwin->frame) {
		*err = wstrdup("window not managed");
		return VERB_FAIL;
	}
	if (WMGetPropListItemCount(args) < 3
	    || !(a = WMGetFromPLArray(args, 2)) || !WMIsPLString(a)) {
		*err = wstrdup("window-workspace: missing +N/-N/N");
		return VERB_FAIL;
	}
	spec = WMGetFromPLString(a);
	number = (*spec == '+' || *spec == '-') ? spec + 1 : spec;
	n = strtol(number, &end, 10);
	if (end == number || *end || n < 0 || n >= wwin->screen_ptr->workspace_count) {
		*err = wstrdup("window-workspace: invalid index");
		return VERB_FAIL;
	}
	if (*spec == '+')
		wWindowAddToWorkspace(wwin, (int)n);
	else if (*spec == '-')
		wWindowRemoveFromWorkspace(wwin, (int)n);
	else
		wWindowChangeWorkspace(wwin, (int)n);
	return VERB_OK;
}

static verb_rc verb_display_power(WScreen *scr, WMPropList *args,
				   char **err, WMPropList **reply)
{
	WMPropList *a;
	const char *arg;

	(void)scr;
	(void)reply;
	if (!args || WMGetPropListItemCount(args) < 2) {
		*err = wstrdup("display-power: missing argument (on|off)");
		return VERB_FAIL;
	}
	a = WMGetFromPLArray(args, 1);
	if (!a || !WMIsPLString(a)) {
		*err = wstrdup("display-power: argument must be a string");
		return VERB_FAIL;
	}
	arg = WMGetFromPLString(a);
	if (strcasecmp(arg, "on") == 0) {
		wDisplaySetOff(false, WDisplayPowerSourceIPC);
	} else if (strcasecmp(arg, "off") == 0) {
		wDisplaySetOff(true, WDisplayPowerSourceIPC);
	} else {
		*err = wstrdup("display-power: argument must be on|off");
		return VERB_FAIL;
	}
	return VERB_OK;
}

static WMPropList *build_version(WScreen *scr)
{
	WMPropList *d = WMCreatePLDictionary(NULL, NULL);

	pl_dict_put(d, "version", pl_string(VERSION));
	pl_dict_put(d, "display", pl_string(w_global.display_name));
	pl_dict_put(d, "screen", pl_int(scr->screen));
	pl_dict_put(d, "pid", pl_int(getpid()));
	pl_dict_put(d, "socket", pl_string(scr->ipc_path));
	pl_dict_put(d, "display_off", pl_bool(w_global.display_off));
	return d;
}

static verb_rc verb_info(WScreen *scr, WMPropList *args,
			 char **err, WMPropList **reply)
{
	WMPropList *actions;
	const char *action;
	int i, n;

	(void)args;
	(void)err;
	*reply = build_version(scr);
	pl_dict_put(*reply, "success", pl_bool(1));

	actions = WMCreatePLArray(NULL);
	n = wKeybindActionCount();
	for (i = 0; i < n; i++) {
		WMPropList *s;

		action = wKeybindActionName(i);
		if (!action)
			continue;
		s = WMCreatePLString(action);
		WMAddToPLArray(actions, s);
		WMReleasePropList(s);
	}
	pl_dict_put(*reply, "actions", actions);
	return VERB_OK;
}

static verb_rc verb_get_keybinds(WScreen *scr, WMPropList *args,
				 char **err, WMPropList **reply)
{
	WMPropList *keybinds, *d;
	WKeybindInfo *items;
	int count, i, show_all = 0;

	(void)scr;
	if (args && WMGetPropListItemCount(args) >= 2) {
		WMPropList *a = WMGetFromPLArray(args, 1);

		if (a && WMIsPLString(a)) {
			if (strcmp(WMGetFromPLString(a), "all") == 0)
				show_all = 1;
			else {
				*err = wstrdup("get-keybinds: unknown argument");
				return VERB_FAIL;
			}
		}
	}

	items = wKeybindCollect(show_all, &count);
	keybinds = WMCreatePLArray(NULL);
	for (i = 0; i < count; i++) {
		d = WMCreatePLDictionary(NULL, NULL);
		pl_dict_put(d, "action", pl_string(items[i].action));
		pl_dict_put(d, "config_key", pl_string(items[i].config_key));
		pl_dict_put(d, "spec", pl_string(items[i].spec));
		pl_dict_put(d, "category", pl_string(items[i].category));
		pl_dict_put(d, "description", pl_string(items[i].description));
		pl_dict_put(d, "bound", pl_bool(items[i].bound));
		if (items[i].bound)
			pl_dict_put(d, "binding", pl_string(items[i].label));
		WMAddToPLArray(keybinds, d);
		WMReleasePropList(d);
	}
	wKeybindFree(items, count);

	*reply = WMCreatePLDictionary(NULL, NULL);
	pl_dict_put(*reply, "success", pl_bool(1));
	pl_dict_put(*reply, "keybinds", keybinds);
	return VERB_OK;
}

#ifdef VIRTUAL_DESKTOP
static verb_rc verb_set_viewport(WScreen *scr, WMPropList *args,
				 char **err, WMPropList **reply)
{
	WMPropList *ax, *ay;
	long vx, vy;
	char *end;

	(void)reply;
	if (!args || WMGetPropListItemCount(args) < 3) {
		*err = wstrdup("set-viewport: usage: set-viewport X Y");
		return VERB_FAIL;
	}
	ax = WMGetFromPLArray(args, 1);
	ay = WMGetFromPLArray(args, 2);
	if (!ax || !WMIsPLString(ax) || !ay || !WMIsPLString(ay)) {
		*err = wstrdup("set-viewport: X and Y must be integers");
		return VERB_FAIL;
	}
	vx = strtol(WMGetFromPLString(ax), &end, 10);
	if (*end) {
		*err = wstrdup("set-viewport: X is not an integer");
		return VERB_FAIL;
	}
	vy = strtol(WMGetFromPLString(ay), &end, 10);
	if (*end) {
		*err = wstrdup("set-viewport: Y is not an integer");
		return VERB_FAIL;
	}
	if (!wVdeskSetViewport(scr, scr->current_workspace, (int)vx, (int)vy)) {
		*err = wstrdup("set-viewport: workspace not pannable "
			       "(tiled or no change)");
		return VERB_FAIL;
	}
	wXSync(wdisp->conn);
	return VERB_OK;
}
#endif

static verb_rc set_layout_pairs(WScreen *scr, WMPropList *args, long *widx,
				bool apply, char **err)
{
	WMPropList *k, *v;
	char *end;
	int i;

	for (i = 2; i + 1 < WMGetPropListItemCount(args); i += 2) {
		k = WMGetFromPLArray(args, i);
		v = WMGetFromPLArray(args, i + 1);
		if (!k || !WMIsPLString(k) || !v || !WMIsPLString(v))
			continue;
		if (strcmp(WMGetFromPLString(k), "workspace") == 0) {
			*widx = strtol(WMGetFromPLString(v), &end, 10);
			if (*end || *widx < 0 || *widx >= scr->workspace_count) {
				*err = wstrdup("set-layout: invalid workspace index");
				return VERB_FAIL;
			}
		} else if (strcmp(WMGetFromPLString(k), "master_count") == 0) {
			if (apply)
				scr->workspaces[*widx]->master_count =
					(int)strtol(WMGetFromPLString(v), NULL, 10);
		} else if (strcmp(WMGetFromPLString(k), "master_ratio") == 0) {
			double ratio = wstrtod(WMGetFromPLString(v), &end);

			if (end == WMGetFromPLString(v) || *end || !isfinite(ratio)
			    || ratio < -(double)FLT_MAX || ratio > (double)FLT_MAX) {
				*err = wstrdup("set-layout: invalid master_ratio");
				return VERB_FAIL;
			}
			if (apply)
				scr->workspaces[*widx]->master_ratio = (float)ratio;
		}
	}
	return VERB_OK;
}

static verb_rc verb_set_layout(WScreen *scr, WMPropList *args,
			       char **err, WMPropList **reply)
{
	WMPropList *a;
	WWorkspace *ws;
	long widx;
	verb_rc rc;

	(void)reply;
	if (!args || WMGetPropListItemCount(args) < 2) {
		*err = wstrdup("set-layout: missing layout name");
		return VERB_FAIL;
	}
	a = WMGetFromPLArray(args, 1);
	if (!a || !WMIsPLString(a)) {
		*err = wstrdup("set-layout: layout must be a string");
		return VERB_FAIL;
	}

	widx = scr->current_workspace;
	rc = set_layout_pairs(scr, args, &widx, false, err);
	if (rc != VERB_OK)
		return rc;
	widx = scr->current_workspace;
	rc = set_layout_pairs(scr, args, &widx, true, err);
	if (rc != VERB_OK)
		return rc;

	ws = scr->workspaces[widx];
	if (ws->layout == wLayoutFromName(WMGetFromPLString(a))
	    && strcasecmp(WMGetFromPLString(a), "float") != 0)
		wLayoutApply(scr, (int)widx);
	else
		wLayoutSetWorkspace(scr, (int)widx, wLayoutFromName(WMGetFromPLString(a)));
	wXSync(wdisp->conn);
	return VERB_OK;
}

static verb_rc verb_workspace_new(WScreen *scr, WMPropList *args,
				   char **err, WMPropList **reply)
{
	WMPropList *a;
	long n;
	char *end;

	(void)reply;
	n = 1;
	if (args && WMGetPropListItemCount(args) > 1) {
		a = WMGetFromPLArray(args, 1);
		if (!a || !WMIsPLString(a)) {
			*err = wstrdup("workspace-new: count must be a string");
			return VERB_FAIL;
		}
		n = strtol(WMGetFromPLString(a), &end, 10);
		if (*end || n < 1) {
			*err = wstrdup("workspace-new: invalid count");
			return VERB_FAIL;
		}
		if (n > MAX_WORKSPACES - scr->workspace_count)
			n = MAX_WORKSPACES - scr->workspace_count;
	}
	if (n > 0)
		wWorkspaceMake(scr, (int)n);
	wXSync(wdisp->conn);
	return VERB_OK;
}

static verb_rc verb_workspace_delete(WScreen *scr, WMPropList *args,
				     char **err, WMPropList **reply)
{
	WMPropList *a;
	long n;
	char *end;
	WWindow *tmp;

	(void)reply;
	if (!args || WMGetPropListItemCount(args) < 2) {
		*err = wstrdup("workspace-delete: missing workspace index");
		return VERB_FAIL;
	}
	a = WMGetFromPLArray(args, 1);
	if (!a || !WMIsPLString(a)) {
		*err = wstrdup("workspace-delete: index must be a string");
		return VERB_FAIL;
	}
	n = strtol(WMGetFromPLString(a), &end, 10);
	if (*end || n <= 0 || n >= scr->workspace_count) {
		*err = wstrdup("workspace-delete: invalid index");
		return VERB_FAIL;
	}
	/* wWorkspaceDelete() pops a modal "in use" dialog when the workspace
	 * is the sole membership of some non-omnipresent window, which would
	 * block the IPC reply forever. Pre-check the same sole-membership
	 * condition here and reject without invoking the dialog. */
	tmp = scr->focused_window;

	while (tmp) {
		if (!IS_OMNIPRESENT(tmp) && WWIN_WS_MASK(tmp) == WS_BIT(n)) {
			*err = wstrdup("workspace-delete: workspace in use");
			return VERB_FAIL;
		}
		tmp = tmp->prev;
	}
	if (!wWorkspaceDelete(scr, (int)n)) {
		*err = wstrdup("workspace-delete: workspace in use");
		return VERB_FAIL;
	}
	wXSync(wdisp->conn);
	return VERB_OK;
}

static verb_rc verb_action(WScreen *scr, WMPropList *args,
			   char **err, WMPropList **reply)
{
	WMPropList *a;
	const char *name;
	xcb_window_t xid;
	WWindow *wwin = scr->focused_window;
	int command;

	(void)reply;
	if (!args || WMGetPropListItemCount(args) < 2) {
		*err = wstrdup("action: missing action name");
		return VERB_FAIL;
	}
	a = WMGetFromPLArray(args, 1);
	if (!a || !WMIsPLString(a)) {
		*err = wstrdup("action: missing action name");
		return VERB_FAIL;
	}
	name = WMGetFromPLString(a);
	command = wKeybindForAction(name);
	if (command < 0) {
		*err = wstrdup("unknown action");
		return VERB_FAIL;
	}
	if (WMGetPropListItemCount(args) >= 3) {
		if (!parse_xid_arg(args, 2, &xid, err))
			return VERB_FAIL;
		wwin = wWindowFor(xid);
		if (!wwin) {
			/* wmlib targets the group leader for Hide, which is
			 * not a managed window */
			WApplication *wapp = wApplicationOf(xid);

			if (!wapp || !wapp->main_window_desc) {
				*err = wstrdup("window not managed");
				return VERB_FAIL;
			}
			wwin = wapp->main_window_desc;
		}
	}
	wKeybindDispatch(command, scr, wwin, NULL);
	wXSync(wdisp->conn);
	return VERB_OK;
}

static const char *const sound_event_names[] = {
#define WSOUND_ITEM(id, wire, key) wire,
	WSOUND_EVENTS(WSOUND_ITEM)
#undef WSOUND_ITEM
};

static verb_rc verb_play_sound(WScreen *scr, WMPropList *args,
			       char **err, WMPropList **reply)
{
	WMPropList *ev;
	const char *name;
	size_t i;
	bool valid = false;

	(void)reply;
	ev = WMGetFromPLArray(args, 1);
	if (!ev || !WMIsPLString(ev)) {
		*err = wstrdup("play-sound requires an event name");
		return VERB_FAIL;
	}
	name = WMGetFromPLString(ev);
	if (!*name) {
		*err = wstrdup("play-sound requires a non-empty event name");
		return VERB_FAIL;
	}
	for (i = 0; i < sizeof(sound_event_names) / sizeof(sound_event_names[0]); i++)
		if (strcmp(name, sound_event_names[i]) == 0) {
			valid = true;
			break;
		}
	if (!valid) {
		*err = wstrdup("play-sound: unknown event name");
		return VERB_FAIL;
	}
	wIPCEmitSound(scr, name);
	wXSync(wdisp->conn);
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
	{ "workspace-new",    verb_workspace_new },
	{ "workspace-delete", verb_workspace_delete },
	{ "window-workspace", verb_window_workspace },
	{ "action",            verb_action },
	{ "set-layout",       verb_set_layout },
#ifdef VIRTUAL_DESKTOP
	{ "set-viewport",     verb_set_viewport },
#endif
	{ "dock-launch",      verb_dock_launch },
	{ "display-power",    verb_display_power },
	{ "play-sound",       verb_play_sound },
	{ "info",             verb_info },
	{ "get-keybinds",     verb_get_keybinds },
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
		tbody = WMGetPropListDescription(tick, false);
		ipc_queue(c, WIPC_EVENT_BIT | WIPC_EV_TICK,
			  tbody, strlen(tbody));
		wfree(tbody);
		WMReleasePropList(tick);
	}
}

static void handle_get_workspaces(struct WIPCClient *c)
{
	WMPropList *arr, *d;
	WWorkspace *ws;
	char rbuf[16];
	int i;

	arr = WMCreatePLArray(NULL);
	for (i = 0; i < c->scr->workspace_count; i++) {
		ws = c->scr->workspaces[i];
		d = WMCreatePLDictionary(NULL, NULL);
		pl_dict_put(d, "name", pl_string(ws->name));
		pl_dict_put(d, "index", pl_int(i));
		pl_dict_put(d, "current", pl_bool(i == c->scr->current_workspace));
		pl_dict_put(d, "layout", pl_string(wLayoutName(ws->layout)));
		pl_dict_put(d, "master_count", pl_int(ws->master_count));
		snprintf(rbuf, sizeof(rbuf), "%.2f", (double)ws->master_ratio);
		pl_dict_put(d, "master_ratio", pl_string(rbuf));
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
		pl_dict_put(d, "skip_window_list", pl_bool(WFLAGP(wwin, skip_window_list)));
		pl_dict_put(d, "skip_switch_panel", pl_bool(WFLAGP(wwin, skip_switchpanel)));
		pl_dict_put(d, "workspace", pl_int(wwin->frame ? WWIN_PRIMARY_WS(wwin) : 0));
		if (wwin->frame) {
			WMPropList *wl = WMCreatePLArray(NULL);
			WMPropList *wsi;
			int i;

			for (i = 0; i < c->scr->workspace_count; i++)
				if (WWIN_WS_MASK(wwin) & WS_BIT(i)) {
					wsi = pl_int(i);
					WMAddToPLArray(wl, wsi);
					WMReleasePropList(wsi);
				}
			pl_dict_put(d, "workspaces", wl);
		}
		WMAddToPLArray(arr, d);
		WMReleasePropList(d);
	}
	ipc_reply(c, WIPC_GET_WINDOWS, arr);
	WMReleasePropList(arr);
}

/* ---- get_window_tree -------------------------------------------------- */

static const char *const dock_type_names[] = { "dock", "clip", "drawer" };

static const char *const ipc_edge_names[] = {
	"left", "right", "top", "bottom"
};

static void tree_stack_index(WScreen *scr, WMHashTable *map)
{
	WMBagIterator it;
	WCoreWindow *top, *frame;
	int idx;

	top = WMBagFirst(scr->stacking_list, &it);
	while (it != NULL) {
		idx = 0;
		for (frame = top; frame != NULL; frame = frame->stacking->under) {
			WMHashInsert(map, (const void *)(uintptr_t)frame->window,
				     (const void *)(uintptr_t)(idx + 1));
			idx++;
		}
		top = WMBagNext(scr->stacking_list, &it);
	}
}

static void tree_put_state(WMPropList *d, WWindow *wwin)
{
	WMPropList *st, *axes, *item;

	st = WMCreatePLDictionary(NULL, NULL);
	pl_dict_put(st, "focused", pl_bool(wwin->flags.focused));
	pl_dict_put(st, "semi_focused", pl_bool(wwin->flags.semi_focused));
	pl_dict_put(st, "mapped", pl_bool(wwin->flags.mapped));
	pl_dict_put(st, "miniaturized", pl_bool(wwin->flags.miniaturized));
	pl_dict_put(st, "hidden", pl_bool(wwin->flags.hidden));
	pl_dict_put(st, "shaded", pl_bool(wwin->flags.shaded));
	pl_dict_put(st, "fullscreen", pl_bool(wwin->flags.fullscreen));
	pl_dict_put(st, "tiled", pl_bool(wwin->flags.tiled));
	pl_dict_put(st, "urgent", pl_bool(wwin->flags.urgent));
	pl_dict_put(st, "dockapp", pl_bool(wwin->flags.is_dockapp));
	pl_dict_put(st, "gnustep", pl_bool(wwin->flags.is_gnustep));
	pl_dict_put(st, "omnipresent", pl_bool(WFLAGP(wwin, omnipresent)));
	pl_dict_put(st, "skip_window_list", pl_bool(WFLAGP(wwin, skip_window_list)));
	pl_dict_put(st, "skip_switch_panel", pl_bool(WFLAGP(wwin, skip_switchpanel)));
	pl_dict_put(st, "modal", pl_bool(wwin->flags.net_modal));
	pl_dict_put(st, "demands_attention", pl_bool(wwin->flags.net_demands_attention));

	axes = WMCreatePLArray(NULL);
	if (wwin->flags.maximized & MAX_VERTICAL) {
		item = pl_string("vertical");
		WMAddToPLArray(axes, item);
		WMReleasePropList(item);
	}
	if (wwin->flags.maximized & MAX_HORIZONTAL) {
		item = pl_string("horizontal");
		WMAddToPLArray(axes, item);
		WMReleasePropList(item);
	}
	if (wwin->flags.maximized & MAX_LEFTHALF) {
		item = pl_string("left_half");
		WMAddToPLArray(axes, item);
		WMReleasePropList(item);
	}
	if (wwin->flags.maximized & MAX_RIGHTHALF) {
		item = pl_string("right_half");
		WMAddToPLArray(axes, item);
		WMReleasePropList(item);
	}
	if (wwin->flags.maximized & MAX_TOPHALF) {
		item = pl_string("top_half");
		WMAddToPLArray(axes, item);
		WMReleasePropList(item);
	}
	if (wwin->flags.maximized & MAX_BOTTOMHALF) {
		item = pl_string("bottom_half");
		WMAddToPLArray(axes, item);
		WMReleasePropList(item);
	}
	if (wwin->flags.maximized & MAX_MAXIMUS) {
		item = pl_string("maximus");
		WMAddToPLArray(axes, item);
		WMReleasePropList(item);
	}
	pl_dict_put(st, "maximized", axes);

	pl_dict_put(d, "state", st);
}

static WMPropList *tree_appicon_dict(WAppIcon *aicon)
{
	WMPropList *d;

	d = WMCreatePLDictionary(NULL, NULL);
	pl_dict_put(d, "docked", pl_bool(aicon->docked));
	if (aicon->dock && aicon->dock->type >= 0
	    && (size_t)aicon->dock->type < wlengthof(dock_type_names))
		pl_dict_put(d, "dock", pl_string(dock_type_names[aicon->dock->type]));
	/* the slot is the along-axis index on the dock, so a client can
	 * tell a horizontal slot from a vertical one via edge */
	pl_dict_put(d, "slot", pl_int(wDockIconSlot(aicon)));
	if (aicon->dock && aicon->dock->type == WM_DOCK
	    && aicon->dock->edge >= WDOCK_EDGE_LEFT
	    && aicon->dock->edge <= WDOCK_EDGE_BOTTOM)
		pl_dict_put(d, "edge",
			    pl_string(ipc_edge_names[aicon->dock->edge]));
	pl_dict_put(d, "running", pl_bool(aicon->running));
	pl_dict_put(d, "launching", pl_bool(aicon->launching));
	pl_dict_put(d, "omnipresent", pl_bool(aicon->omnipresent));
	return d;
}

static WMPropList *tree_window_dict(WWindow *wwin, WMHashTable *stack_idx,
				    WMHashTable *group_ids)
{
	WMPropList *d, *t, *g, *wl, *wsi, *v;
	void *m;
	int i;
	char xid[32];

	d = WMCreatePLDictionary(NULL, NULL);

	snprintf(xid, sizeof(xid), "0x%lx", (unsigned long)wwin->client_win);
	pl_dict_put(d, "id", pl_string(xid));

	if (wwin->frame) {
		snprintf(xid, sizeof(xid), "0x%lx", (unsigned long)wwin->frame->core->window);
		pl_dict_put(d, "frame", pl_string(xid));
	}
	if (wwin->client_pid)
		pl_dict_put(d, "pid", pl_int((long)wwin->client_pid));

	pl_dict_put(d, "class", pl_string_capped(wwin->wm_class));
	pl_dict_put(d, "instance", pl_string_capped(wwin->wm_instance));
	pl_dict_put(d, "role", pl_string_capped(wwin->wm_role));
	pl_dict_put(d, "machine", pl_string_capped(wwin->wm_machine));
	pl_dict_put(d, "title", pl_string_capped(wwin->frame ? wwin->frame->title : wwin->wm_title));

	if (wwin->wm_type_names) {
		t = WMCreatePLArray(NULL);
		for (i = 0; i < wwin->wm_type_count; i++) {
			if (!wwin->wm_type_names[i])
				continue;
			v = pl_string_capped(wwin->wm_type_names[i]);
			WMAddToPLArray(t, v);
			WMReleasePropList(v);
		}
		pl_dict_put(d, "types", t);
	}

	if (wwin->frame) {
		g = WMCreatePLDictionary(NULL, NULL);
		pl_dict_put(g, "x", pl_int(wwin->frame_x));
		pl_dict_put(g, "y", pl_int(wwin->frame_y));
		pl_dict_put(g, "width", pl_int(wwin->frame->core->width));
		pl_dict_put(g, "height", pl_int(wwin->frame->core->height));
		pl_dict_put(d, "geometry", g);
		pl_dict_put(d, "workspace", pl_int(WWIN_PRIMARY_WS(wwin)));

		wl = WMCreatePLArray(NULL);
		for (i = 0; i < wwin->screen_ptr->workspace_count; i++)
			if (WWIN_WS_MASK(wwin) & WS_BIT(i)) {
				wsi = pl_int(i);
				WMAddToPLArray(wl, wsi);
				WMReleasePropList(wsi);
			}
		pl_dict_put(d, "workspaces", wl);

		pl_dict_put(d, "layer", pl_int(WINDOW_LEVEL(wwin)));
		m = WMHashGet(stack_idx, (const void *)(uintptr_t)wwin->frame->core->window);
		if (m)
			pl_dict_put(d, "stacking", pl_int((long)(uintptr_t)m - 1));
	}

	if (wwin->transient_for != XCB_NONE) {
		snprintf(xid, sizeof(xid), "0x%lx", (unsigned long)wwin->transient_for);
		pl_dict_put(d, "transient_for", pl_string(xid));
	}
	if (wwin->group_id != XCB_NONE) {
		snprintf(xid, sizeof(xid), "0x%lx", (unsigned long)wwin->group_id);
		pl_dict_put(d, "group_leader", pl_string(xid));
	}
	if (wwin->client_leader != XCB_NONE) {
		snprintf(xid, sizeof(xid), "0x%lx", (unsigned long)wwin->client_leader);
		pl_dict_put(d, "client_leader", pl_string(xid));
	}

	if (wwin->tab_group) {
		m = WMHashGet(group_ids, wwin->tab_group);
		t = WMCreatePLDictionary(NULL, NULL);
		pl_dict_put(t, "id", pl_int(m ? (long)(uintptr_t)m : 0));
		pl_dict_put(t, "index", pl_int(wwin->tab_group_index));
		pl_dict_put(t, "active", pl_bool(wwin->tab_group->active_index ==
						 wwin->tab_group_index));
		pl_dict_put(t, "size", pl_int(wwin->tab_group->member_count));
		pl_dict_put(d, "tab_group", t);
	}

	pl_dict_put(d, "creation_id", pl_int((long)wwin->creation_id));

	tree_put_state(d, wwin);

	return d;
}

static void handle_get_window_tree(struct WIPCClient *c)
{
	WScreen *scr = c->scr;
	WMPropList *tree, *apps, *d, *windows, *orphan, *err, *ow, *id;
	WMHashTable *stack_idx, *group_ids, *app_windows;
	WApplication *wapp;
	WWindowGroup *grp;
	WWindow *wwin;
	unsigned int n, win_count;
	size_t len;
	char *body;
	char xid[32];

	stack_idx = WMCreateHashTable(WMIntHashCallbacks);
	group_ids = WMCreateHashTable(WMIntHashCallbacks);
	app_windows = WMCreateHashTable(WMIntHashCallbacks);

	tree_stack_index(scr, stack_idx);

	n = 0;
	for (grp = scr->window_groups; grp != NULL; grp = grp->next)
		WMHashInsert(group_ids, grp, (const void *)(uintptr_t)(++n));

	apps = WMCreatePLArray(NULL);

	for (wapp = scr->wapp_list; wapp != NULL; wapp = wapp->next) {
		d = WMCreatePLDictionary(NULL, NULL);
		snprintf(xid, sizeof(xid), "0x%x", wapp->main_window);
		pl_dict_put(d, "main_window", pl_string(xid));
		pl_dict_put(d, "hidden", pl_bool(wapp->flags.hidden));
		if (wapp->app_icon && wapp->app_icon->docked)
			pl_dict_put(d, "appicon", tree_appicon_dict(wapp->app_icon));
		windows = WMCreatePLArray(NULL);
		pl_dict_put(d, "windows", windows);
		WMHashInsert(app_windows, (const void *)(uintptr_t)wapp->main_window,
			     windows);
		WMAddToPLArray(apps, d);
		WMReleasePropList(d);
	}

	orphan = WMCreatePLArray(NULL);
	win_count = 0;
	for (wwin = scr->focused_window; wwin != NULL; wwin = wwin->prev) {
		d = tree_window_dict(wwin, stack_idx, group_ids);
		windows = WMHashGet(app_windows, (const void *)(uintptr_t)wwin->main_window);
		if (windows)
			WMAddToPLArray(windows, d);
		else
			WMAddToPLArray(orphan, d);
		WMReleasePropList(d);
		win_count++;
	}

	for (n = 0; n < (unsigned int)WMGetPropListItemCount(orphan); n++) {
		ow = WMGetFromPLArray(orphan, n);
		id = WMGetFromPLDictionaryByName(ow, "id");

		d = WMCreatePLDictionary(NULL, NULL);
		pl_dict_put(d, "main_window",
			    pl_string(id ? WMGetFromPLString(id) : NULL));
		windows = WMCreatePLArray(ow, NULL);
		pl_dict_put(d, "windows", windows);
		WMAddToPLArray(apps, d);
		WMReleasePropList(d);
	}
	WMReleasePropList(orphan);

	tree = WMCreatePLDictionary(NULL, NULL);
	pl_dict_put(tree, "screen", pl_int(scr->screen));
	pl_dict_put(tree, "version", pl_string(VERSION));
	pl_dict_put(tree, "workspace_count", pl_int(scr->workspace_count));
	pl_dict_put(tree, "current_workspace", pl_int(scr->current_workspace));
	pl_dict_put(tree, "window_count", pl_int(scr->window_count));
	pl_dict_put(tree, "applications", apps);

	body = WMGetPropListDescription(tree, false);
	len = strlen(body);
	if (len > WIPC_MAX_PAYLOAD) {
		wfree(body);
		err = WMCreatePLDictionary(NULL, NULL);
		pl_dict_put(err, "error", pl_string("window tree exceeds 1 MiB"));
		pl_dict_put(err, "window_count", pl_int(win_count));
		ipc_reply(c, WIPC_GET_WINDOW_TREE, err);
		WMReleasePropList(err);
	} else {
		ipc_queue(c, WIPC_GET_WINDOW_TREE, body, len);
		wfree(body);
	}
	WMReleasePropList(tree);

	WMFreeHashTable(app_windows);
	WMFreeHashTable(group_ids);
	WMFreeHashTable(stack_idx);
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

	if (wIPCIsSubscribed(NULL, WIPC_EV_TICK)) {
		ev = WMCreatePLDictionary(NULL, NULL);
		pl_dict_put(ev, "first", pl_bool(0));
		pl_dict_put(ev, "payload", pl_string(payload));
		if (wClipboardTakeKeyHint())
			pl_dict_put(ev, "keyed", pl_bool(1));
		wIPCBroadcast(NULL, WIPC_EV_TICK, ev);
		WMReleasePropList(ev);
	}
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
	case WIPC_GET_WINDOW_TREE:
		handle_get_window_tree(c);
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

	/* a modal dispatch re-enters this callback, let the outer frame
	 * drain the pending frame once busy clears */
	if (c->busy)
		return;

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
			/* mirror the root-menu Exit / Kill-session paths: when a
			 * session manager is present, hand it the shutdown so
			 * peers get an orderly SaveYourself and wmaker exits
			 * from the SM's Die callback rather than tearing the
			 * session down itself, the queued success reply flushes
			 * on the next loop iteration and the connection stays
			 * open until Die arrives */
			if ((mode == WSExitMode || mode == WSKillMode) &&
			    wXSMPRequestShutdown(w_global.config->save_session_on_exit)) {
				ipc_try_write(c);
				return;
			}
			/* wIPCServerStop() runs from Shutdown() and does the
			 * final drain, so the success reply queued above is
			 * delivered there */
			Shutdown(mode, false);
			if (mode == WSRestartPreparationMode) {
				Restart(wm, false);
				/* the restart destroyed the display, so there
				 * is no live loop to return to */
				wfree(wm);
				Exit(1);
			}
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
	if (!c->rd) {
		close(cfd);
		wfree(c);
		return;
	}

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

	if (wIPCIsSubscribed(scr, WIPC_EV_SHUTDOWN)) {
		ev = WMCreatePLDictionary(NULL, NULL);
		pl_dict_put(ev, "change", pl_string(mode));
		/* screen-scoped here, every screen's stop is called separately
		 * from Shutdown() so subscribers on each socket see exactly one */
		wIPCBroadcast(scr, WIPC_EV_SHUTDOWN, ev);
		WMReleasePropList(ev);
	}

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

	if (!wIPCIsSubscribed(NULL, WIPC_EV_CHILD_EXIT))
		return;
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
	if (!wIPCIsSubscribed(NULL, WIPC_EV_DIALOG))
		return;
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

void wIPCEmitDisplayPower(int off, const char *source)
{
	WMPropList *d;

	if (!wIPCIsSubscribed(NULL, WIPC_EV_DISPLAY_POWER))
		return;
	d = WMCreatePLDictionary(NULL, NULL);
	pl_dict_put(d, "state", pl_string(off ? "off" : "on"));
	pl_dict_put(d, "source", pl_string(source));
	/* process-scoped, w_global.display_off is not per-screen */
	wIPCBroadcast(NULL, WIPC_EV_DISPLAY_POWER, d);
	WMReleasePropList(d);
}

void wIPCEmitScreenshot(WScreen *scr, const char *path, int mode)
{
	WMPropList *d;

	if (!wIPCIsSubscribed(scr, WIPC_EV_SCREENSHOT))
		return;
	d = WMCreatePLDictionary(NULL, NULL);
	pl_dict_put(d, "path", pl_string(path));
	pl_dict_put(d, "mode", pl_string(screenshot_mode_name(mode)));
	pl_dict_put(d, "screen", pl_int(scr->screen));
	pl_dict_put(d, "workspace", pl_int(scr->current_workspace));
	wIPCBroadcast(scr, WIPC_EV_SCREENSHOT, d);
	WMReleasePropList(d);
}

void wIPCEmitSound(WScreen *scr, const char *event)
{
	WMPropList *d;

	if (!wIPCIsSubscribed(scr, WIPC_EV_SOUND))
		return;
	d = WMCreatePLDictionary(NULL, NULL);
	pl_dict_put(d, "event", pl_string(event));
	pl_dict_put(d, "screen", pl_int(scr->screen));
	pl_dict_put(d, "workspace", pl_int(scr->current_workspace));
	wIPCBroadcast(scr, WIPC_EV_SOUND, d);
	WMReleasePropList(d);
}

/* ---- notification observers ----------------------------------------- */

static void emit_workspace(WScreen *scr, const char *change, int idx)
{
	WMPropList *d;

	if (!wIPCIsSubscribed(scr, WIPC_EV_WORKSPACE))
		return;
	d = WMCreatePLDictionary(NULL, NULL);
	pl_dict_put(d, "change", pl_string(change));
	pl_dict_put(d, "index", pl_int(idx));
	if (idx >= 0 && idx < scr->workspace_count && scr->workspaces[idx]) {
		pl_dict_put(d, "name", pl_string(scr->workspaces[idx]->name));
		pl_dict_put(d, "layout",
			    pl_string(wLayoutName(scr->workspaces[idx]->layout)));
	}
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
	else if (strcmp(name, WMNWorkspaceLayoutChanged) == 0)
		emit_workspace(scr, "layout", idx);
}

static void emit_window(WWindow *wwin, const char *change)
{
	WMPropList *d;
	char xid[32];

	if (!wwin || !wIPCIsSubscribed(wwin->screen_ptr, WIPC_EV_WINDOW))
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
	WMAddNotificationObserver(obs_workspace, &sentinel, WMNWorkspaceLayoutChanged, NULL);
	WMAddNotificationObserver(obs_window, &sentinel, WMNManaged, NULL);
	WMAddNotificationObserver(obs_window, &sentinel, WMNUnmanaged, NULL);
	WMAddNotificationObserver(obs_window, &sentinel, WMNChangedFocus, NULL);
	WMAddNotificationObserver(obs_window, &sentinel, WMNChangedName, NULL);
}
