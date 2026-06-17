/* event.c- event loop and dispatch table
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2014-2026 Window Maker Team
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

#include <X11/Xlib.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include "WindowMaker.h"
#include "window.h"
#include "main.h"
#include "keybind.h"
#include "defaults.h"
#include "screen.h"
#include "shutdown.h"
#include "event.h"
#include "wxkb.h"
#include "ipc.h"

/* ------------------------------------------------------------------ *
 * dispatch table                                                     *
 *                                                                    *
 * One slot per response_type & 0x7f.  Core events register at fixed *
 * indices, extension events at xext.*.event_base.  WMHandleEvent()  *
 * is hooked as the WINGs extraEventHandler so events on WINGs view  *
 * windows go through the WINGs widget path and only events on       *
 * non-view windows (root, frames, client windows, dock and clip     *
 * tiles) reach the table via DispatchEvent().  The wire bytes are   *
 * carried inside the XEvent at WXWireEvent.wire so they survive the *
 * trip through WMHandleEvent and the modal loops' default arms.     *
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
static int deadProcessPtr = 0;

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
	if (deadProcessPtr >= MAX_DEAD_PROCESSES - 1) {
		wwarning("stack overflow: too many dead processes");
		return;
	}
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
	int i, ws, slot;
	unsigned int es;

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
		w_global.timestamp.last_event = t;
}

void wRefreshKeybindings(void)
{
	int j;

	wXkbRefresh();
	for (j = 0; j < w_global.screen_count; j++) {
		WScreen *scr = wScreenWithNumber(j);
		if (scr)
			wReadKeybindings(scr, w_global.domain.wmaker->dictionary);
	}
}

void DispatchEvent(XEvent *event)
{
	const xcb_generic_event_t *xe;
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
		Shutdown(WSExitMode, True);

	} else if (WCHECK_STATE(WSTATE_NEED_RESTART)) {
		WCHANGE_STATE(WSTATE_RESTARTING);

		Shutdown(WSRestartPreparationMode, False);
		/* received SIGUSR1 */
		Restart(NULL, True);
	} else if (WCHECK_STATE(WSTATE_NEED_REREAD)) {
		WCHANGE_STATE(WSTATE_NORMAL);
		wDefaultsCheckDomains(NULL);
	}

	/* for the case that all that is wanted to be dispatched is
	 * the stuff above */
	if (!event)
		return;

	xe = (const xcb_generic_event_t *)((WXWireEvent *)event)->wire;
	type = xe->response_type & 0x7f;
	wSaveTimestamp(xe);

	/* the wire bytes are zero for events that did not come off the
	 * xcb stream (the @im=local compose replay XPutBackEvent path),
	 * those are KeyPress events targeted at a WINGs textfield so
	 * WMHandleEvent has already dispatched them by the time we are
	 * called and the table is correctly skipped */
	if (type < wlengthof(w_event_handler)) {
		h = w_event_handler[type];
		if (h)
			(*h)(xe, event);
	}
}

/* ------------------------------------------------------------------ *
 * inotify defaults watcher                                          *
 * ------------------------------------------------------------------ */

#ifdef HAVE_INOTIFY
static void handle_inotify_events(void)
{
	ssize_t eventQLength;
	size_t i = 0;
	/* Make room for at lease 5 simultaneous events, with path + filenames */
	char buff[ (sizeof(struct inotify_event) + NAME_MAX + 1) * 5 ];
	/* Check config only once per read of the event queue */
	int oneShotFlag = 0;

	/*
	 * Read off the queued events
	 * queue overflow is not checked (IN_Q_OVERFLOW). In practise this should
	 * not occur; the block is on Xevents, but a config file change will normally
	 * occur as a result of an Xevent - so the event queue should never have more than
	 * a few entries before a read().
	 */
	eventQLength = read(w_global.inotify.fd_event_queue,
	                    buff, sizeof(buff) );

	if (eventQLength < 0) {
		wwarning(_("read problem when trying to get INotify event: %s"), strerror(errno));
		return;
	}

	/* check what events occurred */
	/* Should really check wd here too, but for now we only have one watch! */
	while (i < eventQLength) {
		struct inotify_event *pevent = (struct inotify_event *)&buff[i];

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

			wPreferences.flags.noupdates = 1;
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
				if ((pevent->mask & (IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE)) && oneShotFlag == 0) {
					wwarning(_("Inotify: Reading config files in defaults database."));
					wDefaultsCheckDomains(NULL);
					oneShotFlag = 1;
				}
			}
		}

		/* move to next event in the buffer */
		i += sizeof(struct inotify_event) + pevent->len;
	}

	for (i = 0; i < w_global.screen_count; i++) {
		WScreen *scr = wScreenWithNumber(i);
		if (scr)
			wKeyTreeRebuild(scr);
	}
}

void wInotifyCleanup(void)
{
	if (w_global.inotify.input_handler) {
		WMDeleteInputHandler(w_global.inotify.input_handler);
		w_global.inotify.input_handler = NULL;
	}

	if (w_global.inotify.fd_event_queue >= 0)
		close(w_global.inotify.fd_event_queue);

	w_global.inotify.fd_event_queue = -1;
	w_global.inotify.wd_defaults = -1;
}

static void inotify_input(int fd, int mask, void *clientData)
{
	(void) mask;
	(void) clientData;

	if (fd != w_global.inotify.fd_event_queue)
		return;

	handle_inotify_events();
}
#endif /* HAVE_INOTIFY */

/* ------------------------------------------------------------------ *
 * main loop                                                          *
 * ------------------------------------------------------------------ */

noreturn void EventLoop(void)
{
	XEvent event;

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

	for (;;) {
		WMNextEvent(dpy, &event);	/* Blocks here */
		WMHandleEvent(&event);
	}
}

void ProcessPendingEvents(void)
{
	XEvent event;

	/* drain everything currently readable, then dispatch only that
	 * snapshot so a handler that round-trips and generates more
	 * traffic cannot turn this into an unbounded loop, the NULL dpy
	 * makes WXEvqNext pop without re-draining */
	XSync(dpy, False);
	WXEvqPending(dpy);

	while (WXEvqNext(NULL, &event))
		WMHandleEvent(&event);
}

Bool IsDoubleClick(WScreen *scr, XEvent *event)
{
	if ((scr->last_click_time > 0) &&
	    (event->xbutton.time - scr->last_click_time <= w_global.config->double_click_time)
	    && (event->xbutton.button == scr->last_click_button)
	    && (event->xbutton.window == scr->last_click_window)) {

		scr->flags.next_click_is_not_double = 1;
		scr->last_click_time = 0;
		scr->last_click_window = event->xbutton.window;

		return True;
	}
	return False;
}
