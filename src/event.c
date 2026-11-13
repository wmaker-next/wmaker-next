/* event.c- event loop and dispatch table
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2008-2026 Window Maker Team
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

#ifdef HAVE_INOTIFY
#include <sys/inotify.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include "WindowMaker.h"
#include "window.h"
#include "main.h"
#include "keybind.h"
#include "application.h"
#include "defaults.h"
#include "icon.h"
#include "screen.h"
#include "shutdown.h"
#include "stacking.h"
#include "event.h"
#include "wxkb.h"
#include "accent_picker.h"
#include "ipc.h"
#include <WINGs/WINGsP.h>

/* ------------------------------------------------------------------ *
 * dispatch table                                                     *
 *                                                                    *
 * One slot per response_type & 0x7f. Core events register at fixed  *
 * indices, extension events at xext.*.event_base. WMHandleEvent()   *
 * is hooked as the WINGs extraEventHandler so events on WINGs view  *
 * windows go through the WINGs widget path and only events on       *
 * non-view windows (root, frames, client windows, dock and clip     *
 * tiles) reach the table via DispatchEvent(). The event is the raw   *
 * wire buffer end to end, so the same pointer survives the trip      *
 * through WMHandleEvent and the modal loops' default arms.           *
 * ------------------------------------------------------------------ */

static WEventHandler w_event_handler[128];

void wEventRegisterHandler(unsigned int response_type, WEventHandler h)
{
	wassertr(response_type < wlengthof(w_event_handler));
	w_event_handler[response_type] = h;
}

void wEventInitDispatch(void)
{
	wEventClientRegister();
	wEventInputRegister();
	wEventRootRegister();
	wEventExtRegister();
}

/* ------------------------------------------------------------------ *
 * dead-process handling                                             *
 * ------------------------------------------------------------------ */

static void handleDeadProcess(void);

typedef struct DeadProcesses {
	pid_t pid;
	int wait_status;
} DeadProcesses;

/* stack of dead processes */
static DeadProcesses deadProcesses[MAX_DEAD_PROCESSES];
static volatile sig_atomic_t deadProcessPtr;

typedef struct DeathHandler {
	WDeathHandler *callback;
	pid_t pid;
	void *client_data;
	/* tags carried into the IPC child_exit event so subscribers
	 * can tell which subsystem launched the process, source is a
	 * string literal so only the pointer is stored */
	const char *source;
	int slot;
} DeathHandler;

static WMArray *deathHandlers = NULL;

WMagicNumber wAddDeathHandler(pid_t pid, WDeathHandler *callback, void *cdata,
			      const char *source, int slot)
{
	DeathHandler *handler;

	handler = malloc(sizeof(DeathHandler));
	if (!handler)
		return 0;

	handler->pid = pid;
	handler->callback = callback;
	handler->client_data = cdata;
	handler->source = source ? source : "unknown";
	handler->slot = slot;

	if (!deathHandlers)
		deathHandlers = WMCreateArrayWithDestructor(8, free);

	WMAddToArray(deathHandlers, handler);

	return handler;
}

static void wdelete_death_handler(WMagicNumber id)
{
	DeathHandler *handler = (DeathHandler *) id;

	if (!handler || !deathHandlers)
		return;

	/* array destructor will call free(handler) */
	WMRemoveFromArray(deathHandlers, handler);
}

void NotifyDeadProcess(pid_t pid, int wait_status)
{
	/* called from the SIGCHLD handler, so only async-signal-safe
	 * operations here: silently drop overflow rather than wwarning */
	if (deadProcessPtr >= MAX_DEAD_PROCESSES - 1)
		return;
	/* stack the process to be handled later,
	 * as this is called from the signal handler */
	deadProcesses[deadProcessPtr].pid = pid;
	deadProcesses[deadProcessPtr].wait_status = wait_status;
	deadProcessPtr++;
}

static void handleDeadProcess(void)
{
	DeathHandler *tmp;
	const char *source;
	pid_t dead_pid;
	sigset_t sigchld, oldmask;
	int i, ws, slot;
	unsigned int es;

	/* the queue is filled from the SIGCHLD handler, keep it from
	 * appending while the index is drained */
	sigemptyset(&sigchld);
	sigaddset(&sigchld, SIGCHLD);
	sigprocmask(SIG_BLOCK, &sigchld, &oldmask);

	for (i = 0; i < deadProcessPtr; i++) {
		wWindowDeleteSavedStatesForPID(deadProcesses[i].pid);
	}

	/* get the pids on the queue and call handlers */
	while (deadProcessPtr > 0) {
		deadProcessPtr--;
		dead_pid = deadProcesses[deadProcessPtr].pid;
		ws = deadProcesses[deadProcessPtr].wait_status;
		es = WIFEXITED(ws) ? WEXITSTATUS(ws) : 0;

		/* peek at the registered handler (without calling it) so
		 * the IPC event can carry which subsystem launched the
		 * child, then emit before the handler runs so a test
		 * driver sees child_exit strictly before any dialog the
		 * handler raises */
		source = "unknown";
		slot = -1;
		if (deathHandlers) {
			for (i = WMGetArrayItemCount(deathHandlers) - 1; i >= 0; i--) {
				tmp = WMGetFromArray(deathHandlers, i);
				if (tmp && tmp->pid == dead_pid) {
					source = tmp->source;
					slot = tmp->slot;
					break;
				}
			}
		}
		wIPCEmitChildExit(dead_pid, ws, source, slot);

		if (!deathHandlers)
			continue;

		for (i = WMGetArrayItemCount(deathHandlers) - 1; i >= 0; i--) {
			tmp = WMGetFromArray(deathHandlers, i);
			if (!tmp)
				continue;

			if (tmp->pid == dead_pid) {
				(*tmp->callback) (tmp->pid, es, tmp->client_data);
				wdelete_death_handler(tmp);
			}
		}
	}

	sigprocmask(SIG_SETMASK, &oldmask, NULL);
}

/* ------------------------------------------------------------------ *
 * top of dispatch                                                    *
 * ------------------------------------------------------------------ */

void wSaveTimestamp(const xcb_generic_event_t *xe)
{
	/* never save CurrentTime as LastTimestamp, CurrentTime is the 0L
	 * constant so a zero wire field is also rejected here */
	xcb_timestamp_t t = 0;

	switch (xe->response_type & 0x7f) {
	case XCB_KEY_PRESS:
	case XCB_KEY_RELEASE:
	case XCB_BUTTON_PRESS:
	case XCB_BUTTON_RELEASE:
	case XCB_MOTION_NOTIFY:
		/* Key/Button/Motion share the same wire layout up to and
		 * including the timestamp, time sits at offset 4 */
		t = ((const xcb_key_press_event_t *)xe)->time;
		break;
	case XCB_ENTER_NOTIFY:
	case XCB_LEAVE_NOTIFY:
		t = ((const xcb_enter_notify_event_t *)xe)->time;
		break;
	case XCB_PROPERTY_NOTIFY:
		t = ((const xcb_property_notify_event_t *)xe)->time;
		break;
	case XCB_SELECTION_CLEAR:
		t = ((const xcb_selection_clear_event_t *)xe)->time;
		break;
	case XCB_SELECTION_REQUEST:
		t = ((const xcb_selection_request_event_t *)xe)->time;
		break;
	case XCB_SELECTION_NOTIFY:
		t = ((const xcb_selection_notify_event_t *)xe)->time;
		break;
	}

	if (t != 0)
		wdisp->timestamp.last_event = t;
}

/* ------------------------------------------------------------------ *
 * sequence-range Enter/Leave suppression                             *
 *                                                                    *
 * The opaque-move loop issues an XMoveWindow per MotionNotify and    *
 * each one generates Enter/Leave on whatever the frame edge sweeps   *
 * over. Rather than draining the queue by event type after the       *
 * ButtonRelease (which can swallow a legitimate crossing that        *
 * arrived in the same read), the caller brackets the burst with a    *
 * pair of xcb_no_operation()s, the [begin,end) sequence range is     *
 * recorded in a small ring, and the crossing handlers drop any       *
 * event whose serial falls inside it. The no-op request is a         *
 * sequence marker: every request issued between the two no-ops       *
 * gets a serial inside the recorded range. No server grab is         *
 * taken, which is fine for the move loop since the active            *
 * pointer grab already prevents user-driven crossings on             *
 * other windows.                                                      *
 * ------------------------------------------------------------------ */

unsigned long wEventIgnoreCrossingBegin(void)
{
	return xcb_no_operation(wdisp->conn).sequence;
}

void wEventIgnoreCrossingEnd(unsigned long begin)
{
	unsigned long end = xcb_no_operation(wdisp->conn).sequence;
	unsigned int i = wdisp->ignore_crossing.head;

	wdisp->ignore_crossing.range[i].begin = begin;
	wdisp->ignore_crossing.range[i].end = end;
	wdisp->ignore_crossing.head =
		(i + 1) % wlengthof(wdisp->ignore_crossing.range);

	/* anything already drained into the WINGs queue can go now,
	 * late arrivals are caught by wEventCrossingIgnored() at
	 * dispatch time so no XSync is needed */
	WXEvqDropRange(wdisp->conn, begin, end, XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW);
}

bool wEventCrossingIgnored(unsigned long sequence)
{
	unsigned int i;

	for (i = 0; i < wlengthof(wdisp->ignore_crossing.range); i++) {
		if (sequence >= wdisp->ignore_crossing.range[i].begin &&
		    sequence <  wdisp->ignore_crossing.range[i].end)
			return true;
	}
	return false;
}

void wDisplaySetOff(bool off, WDisplayPowerSource source)
{
	static const char *src_name[] = { "ipc", "randr", "screensaver" };
	WApplication *wapp;
	const char *cmd;
	WScreen *scr;
	WWindow *wwin;
	int j;

	if (w_global.display_off == off)
		return;
	w_global.display_off = off;

	for (j = 0; j < wdisp->screen_count; j++) {
		scr = wScreenWithNumber(j);
		if (!scr)
			continue;
		wapp = scr->wapp_list;
		while (wapp) {
			wAppBounceWhileUrgent(wapp);
			wapp = wapp->next;
		}
		if (!off) {
			wIconResumeBlink(scr);
			wwin = scr->focused_window;
			while (wwin) {
				wWindowUrgentBlink(wwin);
				wwin = wwin->prev;
			}
		}
	}

	wIPCEmitDisplayPower(off, src_name[source]);

	/* DisplayOffCommand and DisplayOnCommand fire only on detected
	 * transitions (RandR output loss, ScreenSaverNotify) so a
	 * scripted `wmakermsg display-power off' does not recursively
	 * launch the locker the script is coordinating with, the
	 * display_power IPC event above is emitted for every source so
	 * a monitor consumer still sees the IPC-driven change */
	if (source == WDisplayPowerSourceIPC)
		return;

	cmd = off ? w_global.config->display_off_command
		  : w_global.config->display_on_command;
	if (cmd && cmd[0]) {
		scr = wScreenWithNumber(0);
		if (scr)
			ExecuteShellCommand(scr, cmd);
	}
}

/* bindings-only re-read: a group switch changes the effective layout
 * the resolution prefers while the keymap is unchanged, and a reload
 * would reset the tracked group */
void wRereadKeybindings(void)
{
	int j;
	bool changed = false;

	for (j = 0; j < wdisp->screen_count; j++) {
		WScreen *scr = wScreenWithNumber(j);
		if (scr) {
			wReadKeybindings(scr, w_global.domain.wmaker->dictionary,
					 &changed);
#ifdef KEEP_XKB_LOCK_STATUS
			wAccentPickerLayoutChanged(scr);
#endif
		}
	}
}

void wRefreshKeybindings(void)
{
	wXkbRefresh();
	wRereadKeybindings();
}

void DispatchEvent(xcb_generic_event_t *event)
{
	WEventHandler h;
	unsigned int type;

	if (deathHandlers)
		handleDeadProcess();

	if (WCHECK_STATE(WSTATE_NEED_EXIT)) {
		WCHANGE_STATE(WSTATE_EXITING);
		/* received SIGTERM, SIGINT or SIGHUP */
		/*
		 * WMHandleEvent() can't be called from anything
		 * executed inside here, or we can get in a infinite
		 * recursive loop.
		 */
		Shutdown(WSExitMode, true);

	} else if (WCHECK_STATE(WSTATE_NEED_RESTART)) {
		WCHANGE_STATE(WSTATE_RESTARTING);

		Shutdown(WSRestartPreparationMode, false);
		/* received SIGUSR1 */
		Restart(NULL, true);
	} else if (WCHECK_STATE(WSTATE_NEED_REREAD)) {
		WCHANGE_STATE(WSTATE_NORMAL);
		wDefaultsCheckDomains(NULL);
	}

	/* for the case that all that is wanted to be dispatched is
	 * the stuff above */
	if (!event)
		return;

	type = event->response_type & 0x7f;

	/* XSendEvent forgeries must not drive bindings or button
	 * actions. Legitimately synthetic protocol traffic passes */
	if ((event->response_type & 0x80) &&
	    (type == XCB_KEY_PRESS || type == XCB_KEY_RELEASE ||
	     type == XCB_BUTTON_PRESS || type == XCB_BUTTON_RELEASE))
		return;

	wSaveTimestamp(event);

	/* events are the raw wire buffers the queue drained, handlers
	 * reached from the modal loops' WMHandleEvent default arms see
	 * the same buffer the main loop would dispatch */
	if (type < wlengthof(w_event_handler)) {
		h = w_event_handler[type];
		if (h)
			(*h)(event);
	}
}

/* ------------------------------------------------------------------ *
 * inotify defaults watcher                                          *
 * ------------------------------------------------------------------ */

#ifdef HAVE_INOTIFY

#define WINOTIFY_MAX_EXTRA 64

static struct {
	int wd;
	void (*cb)(void);
} inotify_extra[WINOTIFY_MAX_EXTRA];
static int inotify_extra_count;

int wInotifyAddWatch(const char *path, unsigned mask, void (*cb)(void))
{
	int wd;

	if (w_global.inotify.fd_event_queue < 0 ||
	    inotify_extra_count >= WINOTIFY_MAX_EXTRA)
		return -1;
	wd = inotify_add_watch(w_global.inotify.fd_event_queue, path, mask);
	if (wd < 0)
		return -1;
	inotify_extra[inotify_extra_count].wd = wd;
	inotify_extra[inotify_extra_count].cb = cb;
	inotify_extra_count++;
	return wd;
}

void wInotifyRemoveWatch(int wd)
{
	int i;

	if (wd < 0 || w_global.inotify.fd_event_queue < 0)
		return;
	inotify_rm_watch(w_global.inotify.fd_event_queue, wd);
	for (i = 0; i < inotify_extra_count; i++) {
		if (inotify_extra[i].wd == wd) {
			inotify_extra[i] = inotify_extra[--inotify_extra_count];
			return;
		}
	}
}

#define DEFAULTS_RELOAD_SETTLE_MS 50
#define DEFAULTS_RELOAD_MAX_MS 250

static struct {
	WMHandlerID settle;
	WMHandlerID max;
} defaults_reload_timer;

static void run_defaults_reload(void)
{
	if (defaults_reload_timer.settle) {
		WMDeleteTimerHandler(defaults_reload_timer.settle);
		defaults_reload_timer.settle = NULL;
	}
	if (defaults_reload_timer.max) {
		WMDeleteTimerHandler(defaults_reload_timer.max);
		defaults_reload_timer.max = NULL;
	}

	wwarning(_("Inotify: Reading config files in defaults database."));
	wDefaultsCheckDomains(NULL);
}

static void defaults_reload_settled(void *arg)
{
	(void)arg;
	defaults_reload_timer.settle = NULL;
	run_defaults_reload();
}

static void defaults_reload_capped(void *arg)
{
	(void)arg;
	defaults_reload_timer.max = NULL;
	run_defaults_reload();
}

static void schedule_defaults_reload(void)
{
	if (!defaults_reload_timer.settle && !defaults_reload_timer.max) {
		defaults_reload_timer.settle =
			WMAddTimerHandler(DEFAULTS_RELOAD_SETTLE_MS, defaults_reload_settled, NULL);
		defaults_reload_timer.max =
			WMAddTimerHandler(DEFAULTS_RELOAD_MAX_MS, defaults_reload_capped, NULL);
	} else if (defaults_reload_timer.settle) {
		WMDeleteTimerHandler(defaults_reload_timer.settle);
		defaults_reload_timer.settle =
			WMAddTimerHandler(DEFAULTS_RELOAD_SETTLE_MS, defaults_reload_settled, NULL);
	}
}

static void handle_inotify_events(void)
{
	ssize_t eventQLength;
	size_t i = 0;
	/* Make room for at lease 5 simultaneous events, with path + filenames */
	char buff[ (sizeof(struct inotify_event) + NAME_MAX + 1) * 5 ];
	int j, fire_n, fired[WINOTIFY_MAX_EXTRA] = {0};
	void (*fire_cb[WINOTIFY_MAX_EXTRA])(void);

	/*
	 * Read off the queued events
	 * queue overflow is not checked (IN_Q_OVERFLOW). In practise this should
	 * not occur, the block is on Xevents, but a config file change will normally
	 * occur as a result of an Xevent - so the event queue should never have more than
	 * a few entries before a read().
	 */
	eventQLength = read(w_global.inotify.fd_event_queue,
	                    buff, sizeof(buff) );

	if (eventQLength < 0) {
#if EAGAIN != EWOULDBLOCK
		if (errno != EAGAIN && errno != EWOULDBLOCK)
#else
		if (errno != EAGAIN)
#endif
			wwarning(_("read problem when trying to get INotify event: %s"), strerror(errno));
		return;
	}
	if (eventQLength == 0)
		return;

	/* check what events occurred */
	while (i < eventQLength) {
		struct inotify_event *pevent = (struct inotify_event *)&buff[i];

		/* extra watches registered through wInotifyAddWatch are
		 * dispatched by wd, the defaults watch keeps its
		 * special-case handling below */
		if (pevent->wd != w_global.inotify.wd_defaults) {
			for (j = 0; j < inotify_extra_count; j++)
				if (inotify_extra[j].wd == pevent->wd)
					fired[j] = 1;
			i += sizeof(struct inotify_event) + pevent->len;
			continue;
		}

		/*
		 * see inotify.h for event types.
		 */
		if (pevent->mask & IN_DELETE_SELF) {
			wwarning(_("the defaults database has been deleted!"
				   " Restart Window Maker to create the database" " with the default settings"));
			wInotifyCleanup();
		}
		if (pevent->mask & IN_UNMOUNT) {
			wwarning(_("the unit containing the defaults database has"
				   " been unmounted. Setting --static mode." " Any changes will not be saved."));
			wInotifyCleanup();

			wdisp->flags.noupdates = 1;
		}
		/* Only react when a known config file inside the Defaults directory is affected */
		if (pevent->len > 0) {
			char *fname = pevent->name;
			const char *allowed[] = { "WMRootMenu", "WMWindowAttributes",
						  "WindowMaker", "WMState", "WMGLOBAL", NULL };
			int k, matched = 0;

			for (k = 0; allowed[k]; k++) {
				if (strcmp(fname, allowed[k]) == 0) {
					matched = 1;
					break;
				}
			}

			if (matched) {
				/* react to events that indicate a file was created/moved/written */
				if (pevent->mask & (IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE))
					schedule_defaults_reload();
			}
		}

		/* move to next event in the buffer */
		i += sizeof(struct inotify_event) + pevent->len;
	}

	/* fire each extra callback at most once per read batch,
	 * snapshotted first so a callback that removes its own
	 * watch does not shuffle the table under the iterator */
	for (j = 0; j < inotify_extra_count; j++)
		fire_cb[j] = fired[j] ? inotify_extra[j].cb : NULL;
	fire_n = inotify_extra_count;
	for (j = 0; j < fire_n; j++)
		if (fire_cb[j])
			(*fire_cb[j])();

	for (i = 0; i < wdisp->screen_count; i++) {
		WScreen *scr = wScreenWithNumber(i);
		if (scr)
			wKeyTreeRebuild(scr);
	}
}

void wInotifyCleanup(void)
{
	/* flush a pending reload so late writes are honoured */
	if (defaults_reload_timer.settle || defaults_reload_timer.max)
		run_defaults_reload();

	if (w_global.inotify.input_handler) {
		WMDeleteInputHandler(w_global.inotify.input_handler);
		w_global.inotify.input_handler = NULL;
	}

	if (w_global.inotify.fd_event_queue >= 0)
		close(w_global.inotify.fd_event_queue);

	w_global.inotify.fd_event_queue = -1;
	w_global.inotify.wd_defaults = -1;
	inotify_extra_count = 0;
}

static void inotify_input(int fd, int mask, void *clientData)
{
	(void) mask;
	(void) clientData;

	if (fd != w_global.inotify.fd_event_queue)
		return;

	handle_inotify_events();
}
#else /* !HAVE_INOTIFY */
int wInotifyAddWatch(const char *path, unsigned mask, void (*cb)(void))
{
	(void)path; (void)mask; (void)cb;
	return -1;
}

void wInotifyRemoveWatch(int wd)
{
	(void)wd;
}
#endif /* HAVE_INOTIFY */

/* ------------------------------------------------------------------ *
 * main loop                                                          *
 * ------------------------------------------------------------------ */

static void commitPendingStacking(void *unused)
{
	int i;

	(void) unused;

	for (i = 0; i < wdisp->screen_count; i++) {
		WScreen *scr = wScreenWithNumber(i);
		if (scr && scr->flags.need_restack)
			CommitStacking(scr);
	}
}

noreturn void EventLoop(void)
{
	xcb_generic_event_t *event = NULL;

#ifdef HAVE_INOTIFY
	/* register the inotify fd with the WINGs input-handler set so it
	 * shares the same select() as the X connection inside WMNextEvent,
	 * otherwise an inotify event on an idle display would not be seen
	 * until the next X event arrived */
	if (w_global.inotify.fd_event_queue >= 0 &&
	    w_global.inotify.wd_defaults >= 0 &&
	    !w_global.inotify.input_handler)
		w_global.inotify.input_handler = WMAddInputHandler(w_global.inotify.fd_event_queue,
							      WIReadMask, inotify_input, NULL);
#endif

	WMSetPrepareHook(commitPendingStacking, NULL);

	for (;;) {
		WMNextEvent(wdisp->conn, &event);	/* Blocks here */
		if (!event)
			continue;
		WMHandleEvent(event);
		free(event);
	}
}

void ProcessPendingEvents(void)
{
	xcb_generic_event_t *event = NULL;

	/* drain everything currently readable, then dispatch only that
	 * snapshot so a handler that round-trips and generates more
	 * traffic cannot turn this into an unbounded loop, the NULL dpy
	 * makes WXEvqNext pop without re-draining */
	wXSync(wdisp->conn);
	WXEvqPending(wdisp->conn);

	while (WXEvqNext(NULL, &event)) {
		W_XEvqCompressPop(&event);
		WMHandleEvent(event);
		free(event);
	}

	commitPendingStacking(NULL);
}

bool IsDoubleClick(WScreen *scr, xcb_generic_event_t *event)
{
	if ((scr->last_click_time > 0) &&
	    (((xcb_button_press_event_t *)event)->time - scr->last_click_time <= w_global.config->double_click_time)
	    && (((xcb_button_press_event_t *)event)->detail == scr->last_click_button)
	    && (((xcb_button_press_event_t *)event)->event == scr->last_click_window)) {

		scr->flags.next_click_is_not_double = 1;
		scr->last_click_time = 0;
		scr->last_click_window = ((xcb_button_press_event_t *)event)->event;

		return true;
	}
	return false;
}
