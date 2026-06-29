/*  startup_notify.c - freedesktop startup-notification launcher and monitor
 *
 *  Window Maker window manager
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

#include "wconfig.h"

#ifdef HAVE_STARTUP_NOTIFICATION

#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>

#define SN_API_NOT_YET_FROZEN 1
#include <libsn/sn.h>

#include "WindowMaker.h"
#include "screen.h"
#include "appicon.h"
#include "startup_notify.h"

/* awesome and openbox both use 20 s, i3 uses 60 s, the shorter value keeps
 * the busy cursor and the launching tile from sticking around when an app
 * crashes immediately after exec without ever mapping a window */
#define WSN_TIMEOUT_MS		(20 * 1000)

/* fallback for our own launches that never produced an INITIATED broadcast,
 * a little longer than the sequence timeout so the normal path always wins */
#define WSN_FALLBACK_MS		(25 * 1000)

struct WPendingLaunch {
	char *id;
	SnStartupSequence *seq;		/* ref'd, NULL until INITIATED arrives */
	SnLauncherContext *ctx;		/* ref'd, NULL when wmaker was not the launcher */
	WAppIcon *btn;			/* dock tile to repaint on completion, may be NULL */
	pid_t pid;			/* child pid, 0 until Spawned() */
	Time launch_time;
	int workspace;
	Bool has_launch_info;
	Bool sets_busy_cursor;		/* counts toward wStartupNotifyAppStarting() */
	WMHandlerID timeout;
	WMHandlerID fallback_timeout;	/* only while seq == NULL after spawn */
	WScreen *scr;
};

struct WStartupNotify {
	SnMonitorContext *monitor;
	WMArray *pending;		/* of WPendingLaunch * */
};

/* one shared SnDisplay across every WScreen, libsn routes monitor events
 * to the right per-screen context by the SCREEN= key in the broadcast */
static SnDisplay *sn_dpy;
static int sn_dpy_refs;

static void sn_event_cb(SnMonitorEvent *ev, void *data);
static void sn_seq_timeout_cb(void *data);
static void sn_fallback_timeout_cb(void *data);
static void pending_dispose(WPendingLaunch *pl, Bool clear_btn);

static WPendingLaunch *pending_find_id(WScreen *scr, const char *id)
{
	WPendingLaunch *pl;
	WMArrayIterator it;

	if (!scr->sn || !id)
		return NULL;

	WM_ITERATE_ARRAY(scr->sn->pending, pl, it) {
		if (pl->id && strcmp(pl->id, id) == 0)
			return pl;
	}
	return NULL;
}

static WPendingLaunch *pending_find_seq(WScreen *scr, SnStartupSequence *seq)
{
	WPendingLaunch *pl;
	WMArrayIterator it;

	if (!scr->sn || !seq)
		return NULL;

	WM_ITERATE_ARRAY(scr->sn->pending, pl, it) {
		if (pl->seq == seq)
			return pl;
	}
	return NULL;
}

void wStartupNotifyUpdateCursor(WScreen *scr)
{
	Cursor cur;

	cur = wStartupNotifyAppStarting(scr) ? wPreferences.cursor[WCUR_WAIT]
					     : wPreferences.cursor[WCUR_ROOT];
	XDefineCursor(dpy, scr->root_win, cur);
}

Bool wStartupNotifyAppStarting(WScreen *scr)
{
	WPendingLaunch *pl;
	WMArrayIterator it;

	if (!scr->sn)
		return False;
	WM_ITERATE_ARRAY(scr->sn->pending, pl, it) {
		if (pl->sets_busy_cursor)
			return True;
	}
	return False;
}

void wStartupNotifyInit(WScreen *scr)
{
	struct WStartupNotify *sn;

	if (!sn_dpy) {
		/* the Xlib-signature sn_display_new()/_process_event() are
		 * thin shims over the xcb code path (libsn imports a single
		 * Xlib symbol, XGetXCBConnection), going through
		 * sn_xcb_display_new() lets event_root.c hand over the raw
		 * xcb_client_message_event_t and drops the cooked-event
		 * dependency from the only ClientMessage consumer that does
		 * not need it */
		sn_dpy = sn_xcb_display_new(XGetXCBConnection(dpy), NULL, NULL);
		if (!sn_dpy)
			return;
	}

	sn = wmalloc(sizeof(*sn));
	sn->pending = WMCreateArray(4);
	sn->monitor = sn_monitor_context_new(sn_dpy, scr->screen, sn_event_cb, scr, NULL);
	if (!sn->monitor) {
		WMFreeArray(sn->pending);
		wfree(sn);
		return;
	}
	scr->sn = sn;
	sn_dpy_refs++;
}

void wStartupNotifyShutdown(WScreen *scr)
{
	struct WStartupNotify *sn = scr->sn;
	WPendingLaunch *pl;
	WMArrayIterator it;

	if (!sn)
		return;

	/* drop entries first so AppStarting() returns False for the cursor
	 * restore below, but keep the array itself alive until after that
	 * call because UpdateCursor() reads scr->sn->pending */
	WM_ITERATE_ARRAY(sn->pending, pl, it) {
		pending_dispose(pl, False);
	}
	WMEmptyArray(sn->pending);
	wStartupNotifyUpdateCursor(scr);

	WMFreeArray(sn->pending);
	sn_monitor_context_unref(sn->monitor);
	wfree(sn);
	scr->sn = NULL;

	if (sn_dpy_refs > 0 && --sn_dpy_refs == 0) {
		sn_display_unref(sn_dpy);
		sn_dpy = NULL;
	}
}

Bool wStartupNotifyProcessXcbEvent(const xcb_generic_event_t *xe)
{
	if (!sn_dpy)
		return False;
	/* libsn never modifies the event it is handed and the parameter
	 * is only non-const for historical reasons, the cast is safe */
	return sn_xcb_display_process_event(sn_dpy, (xcb_generic_event_t *)xe) ? True : False;
}

/* tear down a pending entry without removing it from the array, callers
 * that own array membership do the WMRemoveFromArray() themselves */
static void pending_dispose(WPendingLaunch *pl, Bool clear_btn)
{
	if (!pl)
		return;

	WMDeleteTimerHandlerPtr(&pl->timeout);
	WMDeleteTimerHandlerPtr(&pl->fallback_timeout);

	if (clear_btn && pl->btn) {
		pl->btn->launching = 0;
		wAppIconPaint(pl->btn);
	}

	if (pl->ctx)
		sn_launcher_context_unref(pl->ctx);
	if (pl->seq)
		sn_startup_sequence_unref(pl->seq);
	wfree(pl->id);
	wfree(pl);
}

static void pending_remove(WScreen *scr, WPendingLaunch *pl, Bool clear_btn)
{
	if (!pl || !scr->sn)
		return;
	WMRemoveFromArray(scr->sn->pending, pl);
	pending_dispose(pl, clear_btn);
	wStartupNotifyUpdateCursor(scr);
}

static void sn_event_cb(SnMonitorEvent *ev, void *data)
{
	WScreen *scr = data;
	SnStartupSequence *seq;
	WPendingLaunch *pl;
	const char *id;

	if (!scr->sn)
		return;

	seq = sn_monitor_event_get_startup_sequence(ev);
	id = seq ? sn_startup_sequence_get_id(seq) : NULL;

	switch (sn_monitor_event_get_type(ev)) {
	case SN_MONITOR_EVENT_INITIATED:
		pl = pending_find_id(scr, id);
		if (!pl) {
			/* sequence initiated by another launcher, not wmaker,
			 * the foreign launcher is responsible for completing
			 * it so always count it toward the busy cursor */
			pl = wmalloc(sizeof(*pl));
			pl->id = id ? wstrdup(id) : NULL;
			pl->scr = scr;
			pl->workspace = -1;
			pl->sets_busy_cursor = True;
			WMAddToArray(scr->sn->pending, pl);
		}
		if (!pl->seq && seq) {
			sn_startup_sequence_ref(seq);
			pl->seq = seq;
		}
		WMDeleteTimerHandlerPtr(&pl->fallback_timeout);
		if (seq) {
			Time seq_ts = sn_startup_sequence_get_timestamp(seq);
			int seq_ws = sn_startup_sequence_get_workspace(seq);

			/* keep the launcher-supplied values when libsn could
			 * not extract them from the broadcast, get_timestamp()
			 * returns 0 for IDs without the canonical _TIME suffix */
			if (seq_ts != 0 || !pl->has_launch_info)
				pl->launch_time = seq_ts;
			if (seq_ws >= 0 || !pl->has_launch_info)
				pl->workspace = seq_ws;
			pl->has_launch_info = True;
		}
		if (!pl->timeout)
			pl->timeout = WMAddTimerHandler(WSN_TIMEOUT_MS, sn_seq_timeout_cb, pl);
		wStartupNotifyUpdateCursor(scr);
		break;

	case SN_MONITOR_EVENT_COMPLETED:
	case SN_MONITOR_EVENT_CANCELED:
		pl = pending_find_id(scr, id);
		if (!pl)
			pl = pending_find_seq(scr, seq);
		pending_remove(scr, pl, True);
		break;

	case SN_MONITOR_EVENT_CHANGED:
	default:
		break;
	}
}

static void sn_seq_timeout_cb(void *data)
{
	WPendingLaunch *pl = data;

	pl->timeout = NULL;
	if (pl->seq) {
		/* this triggers COMPLETED via sn_event_cb which will free pl */
		sn_startup_sequence_complete(pl->seq);
		return;
	}
	/* no sequence ever arrived, dispose directly */
	pending_remove(pl->scr, pl, True);
}

static void sn_fallback_timeout_cb(void *data)
{
	WPendingLaunch *pl = data;

	pl->fallback_timeout = NULL;
	if (pl->seq) {
		/* INITIATED raced in after Spawned() armed us but before we
		 * fired, the regular timeout owns lifetime now */
		return;
	}
	if (pl->ctx)
		sn_launcher_context_complete(pl->ctx);
	pending_remove(pl->scr, pl, True);
}

Bool wStartupNotifyMatchWindow(WScreen *scr, const char *startup_id,
			       const char *wm_class, const char *wm_instance,
			       pid_t pid, Time *launch_time_out, int *workspace_out)
{
	WPendingLaunch *pl;
	WPendingLaunch *hit = NULL;
	WMArrayIterator it;
	const char *seq_class;

	if (!scr->sn || WMGetArrayItemCount(scr->sn->pending) == 0)
		return False;

	/* pass 1, exact startup id, so a window carrying id X never consumes
	 * an earlier same-class launch sitting before X in the array */
	if (startup_id) {
		WM_ITERATE_ARRAY(scr->sn->pending, pl, it) {
			if (pl->id && strcmp(pl->id, startup_id) == 0) {
				hit = pl;
				break;
			}
		}
	}

	/* pass 2, wmclass fallback, only on entries that have a sequence
	 * because that is where libsn stores WMCLASS= */
	if (!hit && (wm_class || wm_instance)) {
		WM_ITERATE_ARRAY(scr->sn->pending, pl, it) {
			if (!pl->seq)
				continue;
			seq_class = sn_startup_sequence_get_wmclass(pl->seq);
			if (!seq_class)
				continue;
			if ((wm_class && strcmp(seq_class, wm_class) == 0)
			    || (wm_instance && strcmp(seq_class, wm_instance) == 0)) {
				hit = pl;
				break;
			}
		}
	}

	/* pass 3, pid fallback for launches we initiated ourselves, so an
	 * app that neither speaks startup-notification nor maps with the
	 * WMCLASS we guessed (a .desktop without StartupWMClass=, or a
	 * toolkit that sets a generic class) still drops the busy cursor
	 * as soon as its first window appears */
	if (!hit && pid > 0) {
		WM_ITERATE_ARRAY(scr->sn->pending, pl, it) {
			if (pl->pid == pid) {
				hit = pl;
				break;
			}
		}
	}

	if (!hit)
		return False;

	if (hit->has_launch_info) {
		*launch_time_out = hit->launch_time;
		*workspace_out = hit->workspace;
	} else if (hit->seq) {
		*launch_time_out = sn_startup_sequence_get_timestamp(hit->seq);
		*workspace_out = sn_startup_sequence_get_workspace(hit->seq);
	} else {
		*launch_time_out = 0;
		*workspace_out = -1;
	}

	/* the dock tile's launching flag is cleared by wDockTrackWindowLaunch()
	 * which runs later in wManageWindow() and needs (launching || !running)
	 * to associate this window with its dock icon, so detach the tile from
	 * the entry now and let the existing dock path own that flag, the only
	 * cases where startup_notify itself should clear it are timeout and
	 * abort, and those still hold pl->btn */
	hit->btn = NULL;

	if (hit->seq) {
		/* COMPLETED via sn_event_cb does the actual teardown */
		sn_startup_sequence_complete(hit->seq);
	} else {
		if (hit->ctx)
			sn_launcher_context_complete(hit->ctx);
		pending_remove(scr, hit, False);
	}
	return True;
}

WStartupLaunch *wStartupLaunchBegin(WScreen *scr, const char *bin,
				    const char *name, const char *wmclass,
				    int workspace, WAppIcon *btn)
{
	SnLauncherContext *ctx;
	WPendingLaunch *pl;
	const char *id;
	Time ts;

	if (!sn_dpy || !scr->sn)
		return NULL;

	ctx = sn_launcher_context_new(sn_dpy, scr->screen);
	if (!ctx)
		return NULL;

	ts = w_global.timestamp.last_event;
	if (name)
		sn_launcher_context_set_name(ctx, name);
	if (bin)
		sn_launcher_context_set_binary_name(ctx, bin);
	if (wmclass)
		sn_launcher_context_set_wmclass(ctx, wmclass);
	if (workspace >= 0)
		sn_launcher_context_set_workspace(ctx, workspace);
	sn_launcher_context_initiate(ctx, "wmaker", bin ? bin : "unknown", ts);

	id = sn_launcher_context_get_startup_id(ctx);

	pl = wmalloc(sizeof(*pl));
	pl->id = id ? wstrdup(id) : NULL;
	pl->ctx = ctx;
	pl->btn = btn;
	pl->launch_time = ts;
	pl->workspace = workspace;
	pl->has_launch_info = True;
	/* a launch with no wmclass cannot be matched on window-map by a
	 * client that does not speak startup-notification, so it would hold
	 * the busy cursor for the full 20 s timeout for every such app
	 * launched from the root menu or a hot corner, only count this
	 * entry toward the busy cursor when the WMCLASS= fallback in
	 * wStartupNotifyMatchWindow() can fire */
	pl->sets_busy_cursor = (wmclass != NULL);
	pl->scr = scr;

	WMAddToArray(scr->sn->pending, pl);
	/* set the busy cursor immediately, before the looped-back INITIATED
	 * broadcast reaches our own monitor on the next event loop pass */
	wStartupNotifyUpdateCursor(scr);

	return pl;
}

void wStartupLaunchSetupChild(WStartupLaunch *l)
{
	/* runs in the forked child via the wspawnvp_setup pre_exec hook,
	 * sn_launcher_context_setup_child_process() is just a setenv() of
	 * DESKTOP_STARTUP_ID, setenv() is not in the POSIX async-signal-safe
	 * list but is acceptable here because wmaker is single-threaded at
	 * fork time and the child execvp()s immediately, the same assumption
	 * SetupEnvironment() already relies on */
	if (l && l->ctx)
		sn_launcher_context_setup_child_process(l->ctx);
}

void wStartupLaunchSpawned(WStartupLaunch *l, pid_t pid)
{
	if (!l)
		return;

	l->pid = pid;

	/* the monitor INITIATED callback arms the regular timeout once it
	 * sees our own broadcast looped back, this fallback only exists for
	 * the case where that never happens so the entry cannot leak */
	if (!l->seq && !l->fallback_timeout)
		l->fallback_timeout = WMAddTimerHandler(WSN_FALLBACK_MS,
							sn_fallback_timeout_cb, l);
}

void wStartupLaunchChildExited(WScreen *scr, pid_t pid)
{
	WPendingLaunch *pl;
	WMArrayIterator it;

	if (!scr || !scr->sn || pid <= 0)
		return;

	WM_ITERATE_ARRAY(scr->sn->pending, pl, it) {
		if (pl->pid != pid)
			continue;
		/* the child exited before mapping a window, cancel the
		 * sequence now so the busy cursor and the launching tile
		 * do not have to wait for the 20 s timeout, the dock
		 * death handler clears btn->launching itself */
		pl->btn = NULL;
		if (pl->seq) {
			sn_startup_sequence_complete(pl->seq);
		} else {
			if (pl->ctx)
				sn_launcher_context_complete(pl->ctx);
			pending_remove(scr, pl, False);
		}
		return;
	}
}

void wStartupLaunchAbort(WStartupLaunch *l)
{
	if (!l)
		return;

	/* fork failed, the entry was already pushed in Begin so pop it,
	 * delete any timers defensively even though Spawned() never ran */
	if (l->ctx)
		sn_launcher_context_complete(l->ctx);
	pending_remove(l->scr, l, True);
}

void wStartupNotifyForgetIcon(WAppIcon *btn)
{
	WPendingLaunch *pl;
	WMArrayIterator it;
	WScreen *scr;
	int i;

	if (!btn)
		return;

	for (i = 0; i < w_global.screen_count; i++) {
		scr = wScreenWithNumber(i);
		if (!scr || !scr->sn)
			continue;
		WM_ITERATE_ARRAY(scr->sn->pending, pl, it) {
			if (pl->btn == btn)
				pl->btn = NULL;
		}
	}
}

#else  /* !HAVE_STARTUP_NOTIFICATION */

/* every public symbol is provided as a static inline no-op stub in the
 * header so call sites stay ifdef-free, this translation unit only needs
 * to exist so the build does not have to special-case the source list */
typedef int wmaker_startup_notify_dummy;

#endif  /* HAVE_STARTUP_NOTIFICATION */
