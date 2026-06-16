/* event.c- event loop and handling
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
#include <strings.h>
#include <time.h>
#include <errno.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#ifdef USE_XSHAPE
# include <X11/extensions/shape.h>
# include <xcb/shape.h>
#endif
#ifdef USE_DOCK_XDND
#include "xdnd.h"
#endif

#ifdef USE_RANDR
#include <xcb/randr.h>
#include "randr.h"
#endif

#include <X11/XKBlib.h>
#include <xcb/xkb.h>

#include "WindowMaker.h"
#include "window.h"
#include "actions.h"
#include "client.h"
#include "main.h"
#include "cycling.h"
#include "tabgroup.h"
#include "keybind.h"
#include "application.h"
#include "stacking.h"
#include "defaults.h"
#include "workspace.h"
#include "dock.h"
#include "framewin.h"
#include "properties.h"
#include "balloon.h"
#include "xinerama.h"
#include "wmspec.h"
#include "rootmenu.h"
#include "colormap.h"
#include "screen.h"
#include "shutdown.h"
#include "misc.h"
#include "event.h"
#include "osdview.h"
#include "ipc.h"

#include <sys/wait.h>
#include "winmenu.h"
#include "switchmenu.h"
#include "wsmap.h"
#include "hotcorner.h"
#include "startup_notify.h"


#define MOD_MASK wPreferences.modifier_mask

/************ Local stuff ***********/

static void saveTimestamp(XEvent *event);
static void handleColormapNotify(XEvent *event);
static void handleMapNotify(XEvent *event);
static void handleUnmapNotify(XEvent *event);
static void handleButtonPress(XEvent *event);
static void handleExpose(XEvent *event);
static void handleDestroyNotify(XEvent *event);
static void handleConfigureRequest(XEvent *event);
static void handleMapRequest(XEvent *event);
static void handlePropertyNotify(XEvent *event);
static void handleEnterNotify(XEvent *event);
static void handleLeaveNotify(XEvent *event);
static void handleExtensions(XEvent *event);
static void handleClientMessage(XEvent *event);
static void handleKeyPress(XEvent *event);
static void handleFocusIn(XEvent *event);
static void handleMotionNotify(XEvent *event);
static void handleVisibilityNotify(XEvent *event);
#ifdef HAVE_INOTIFY
static void handle_inotify_events(void);
#endif
static void handle_selection_request(XSelectionRequestEvent *event);
static void handle_selection_clear(XSelectionClearEvent *event);
static void wdelete_death_handler(WMagicNumber id);


#ifdef USE_XSHAPE
static void handleShapeNotify(XEvent *event);
#endif

#ifdef KEEP_XKB_LOCK_STATUS
static void handleXkbIndicatorStateNotify(XEvent *event);
#endif

/* real dead process handler */
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

void DispatchEvent(XEvent * event)
{
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

	saveTimestamp(event);
	switch (event->type) {
	case MapRequest:
		handleMapRequest(event);
		break;

	case KeyPress:
		handleKeyPress(event);
		break;

	case MotionNotify:
		handleMotionNotify(event);
		break;

	case ConfigureRequest:
		handleConfigureRequest(event);
		break;

	case DestroyNotify:
		handleDestroyNotify(event);
		break;

	case MapNotify:
		handleMapNotify(event);
		break;

	case UnmapNotify:
		handleUnmapNotify(event);
		break;

	case ButtonPress:
		handleButtonPress(event);
		break;

	case Expose:
		handleExpose(event);
		break;

	case PropertyNotify:
		handlePropertyNotify(event);
		break;

	case EnterNotify:
		handleEnterNotify(event);
		break;

	case LeaveNotify:
		handleLeaveNotify(event);
		break;

	case ClientMessage:
		handleClientMessage(event);
		break;

	case ColormapNotify:
		handleColormapNotify(event);
		break;

	case MappingNotify:
		if (event->xmapping.request == MappingKeyboard || event->xmapping.request == MappingModifier)
			XRefreshKeyboardMapping(&event->xmapping);
		break;

	case FocusIn:
		handleFocusIn(event);
		break;

	case VisibilityNotify:
		handleVisibilityNotify(event);
		break;

	case ConfigureNotify:
		break;

	case SelectionRequest:
		handle_selection_request(&event->xselectionrequest);
		break;

	case SelectionClear:
		handle_selection_clear(&event->xselectionclear);
		break;

	default:
		handleExtensions(event);
		break;
	}
}

#ifdef HAVE_INOTIFY
/*
 *----------------------------------------------------------------------
 * handle_inotify_events-
 * 	Check for inotify events
 *
 * Returns:
 * 	After reading events for the given file descriptor (fd) and
 *     watch descriptor (wd)
 *
 * Side effects:
 * 	Calls wDefaultsCheckDomains if config database is updated
 *----------------------------------------------------------------------
 */
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
			int i, matched = 0;

			for (i = 0; allowed[i]; i++) {
				if (strcmp(fname, allowed[i]) == 0) {
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

/*
 *----------------------------------------------------------------------
 * EventLoop-
 * 	Processes X and internal events indefinitely.
 *
 * Returns:
 * 	Never returns
 *
 * Side effects:
 * 	The LastTimestamp global variable is updated.
 *      Calls handle_inotify_events if the defaults database changes.
 *----------------------------------------------------------------------
 */
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

/*
 *----------------------------------------------------------------------
 * ProcessPendingEvents --
 * 	Processes the events that are currently pending (at the time
 *      this function is called) in the display's queue.
 *
 * Returns:
 *      After the pending events that were present at the function call
 *      are processed.
 *
 * Side effects:
 * 	Many -- whatever handling events may involve.
 *
 *----------------------------------------------------------------------
 */
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

Bool IsDoubleClick(WScreen * scr, XEvent * event)
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

static void saveTimestamp(XEvent * event)
{
	/*
	 * Never save CurrentTime as LastTimestamp because CurrentTime
	 * it's not a real timestamp (it's the 0L constant)
	 */

	switch (event->type) {
	case ButtonRelease:
	case ButtonPress:
		w_global.timestamp.last_event = event->xbutton.time;
		break;
	case KeyPress:
	case KeyRelease:
		w_global.timestamp.last_event = event->xkey.time;
		break;
	case MotionNotify:
		w_global.timestamp.last_event = event->xmotion.time;
		break;
	case PropertyNotify:
		w_global.timestamp.last_event = event->xproperty.time;
		break;
	case EnterNotify:
	case LeaveNotify:
		w_global.timestamp.last_event = event->xcrossing.time;
		break;
	case SelectionClear:
		w_global.timestamp.last_event = event->xselectionclear.time;
		break;
	case SelectionRequest:
		w_global.timestamp.last_event = event->xselectionrequest.time;
		break;
	case SelectionNotify:
		w_global.timestamp.last_event = event->xselection.time;
#ifdef USE_DOCK_XDND
		wXDNDProcessSelection(event);
#endif
		break;
	}
}

static int matchWindow(const void *item, const void *cdata)
{
	return (((WFakeGroupLeader *) item)->origLeader == (Window) cdata);
}

static void handleExtensions(XEvent * event)
{
#ifdef USE_XSHAPE
	if (w_global.xext.shape.supported
	    && event->type == w_global.xext.shape.event_base + XCB_SHAPE_NOTIFY) {
		handleShapeNotify(event);
	}
#endif
#ifdef USE_XSYNC
	if (wNETWMSyncRequestHandleAlarm(event))
		return;
#endif
	if (w_global.xext.xkb.supported && event->type == w_global.xext.xkb.event_base) {
		/* XKB has a single event opcode and every sub-type shares the
		 * same {response_type, xkbType, sequence, time, deviceID}
		 * prefix, so any of the typed structs reads xkbType */
		xcb_xkb_new_keyboard_notify_event_t *xe =
			(xcb_xkb_new_keyboard_notify_event_t *)((WXWireEvent *)event)->wire;
		int xkb_type = xe->xkbType;

		if (xkb_type == XCB_XKB_NEW_KEYBOARD_NOTIFY) {
			int j;
			WScreen *scr;

			for (j = 0; j < w_global.screen_count; j++) {
				scr = wScreenWithNumber(j);
				wReadKeybindings(scr, w_global.domain.wmaker->dictionary);
			}
		}
#ifdef KEEP_XKB_LOCK_STATUS
		else {
			/* Listen not only for IndicatorStateNotify but also for StateNotify
			 * which is commonly emitted on group (layout) changes. */
			if (w_global.config->modelock
			    && (xkb_type == XCB_XKB_INDICATOR_STATE_NOTIFY
				|| xkb_type == XCB_XKB_STATE_NOTIFY)) {
				handleXkbIndicatorStateNotify(event);
			}
		}
#endif /*KEEP_XKB_LOCK_STATUS */
	}
#ifdef USE_RANDR
	if (w_global.xext.randr.supported
	    && event->type == w_global.xext.randr.event_base + XCB_RANDR_SCREEN_CHANGE_NOTIFY) {
		xcb_randr_screen_change_notify_event_t *re =
			(xcb_randr_screen_change_notify_event_t *)((WXWireEvent *)event)->wire;
		WScreen *randr_scr = wScreenForRootWindow(re->root);
		if (randr_scr)
			wRandRHandleNotify(randr_scr);
	}
	if (w_global.xext.randr.supported
	    && event->type == w_global.xext.randr.event_base + XCB_RANDR_NOTIFY) {
		/* only CrtcChange and OutputChange carry the root that
		 * registered with RRSelectInput, every other subCode just
		 * rearms the per-screen debounce on screen 0 */
		xcb_randr_notify_event_t *re =
			(xcb_randr_notify_event_t *)((WXWireEvent *)event)->wire;
		WScreen *randr_scr;
		Window root;

		if (re->subCode == XCB_RANDR_NOTIFY_CRTC_CHANGE)
			root = re->u.cc.window;
		else if (re->subCode == XCB_RANDR_NOTIFY_OUTPUT_CHANGE)
			root = re->u.oc.window;
		else
			root = wScreenWithNumber(0)->root_win;
		randr_scr = wScreenForRootWindow(root);
		if (randr_scr)
			wRandRHandleNotify(randr_scr);
	}
#endif
}

static void handleMapRequest(XEvent * ev)
{
	WWindow *wwin;
	WScreen *scr = NULL;
	Window window = ev->xmaprequest.window;

	wwin = wWindowFor(window);
	if (wwin != NULL) {
		if (wwin->flags.shaded) {
			wUnshadeWindow(wwin);
		}
		/* deiconify window */
		if (wwin->flags.miniaturized) {
			wDeiconifyWindow(wwin);
		} else if (wwin->flags.hidden) {
			WApplication *wapp = wApplicationOf(wwin->main_window);
			/* go to the last workspace that the user worked on the app */
			if (wapp) {
				wWorkspaceChange(wwin->screen_ptr, wapp->last_workspace);
			}
			wUnhideApplication(wapp, False, False);
		}
		return;
	}

	scr = wScreenForRootWindow(ev->xmaprequest.parent);

	wwin = wManageWindow(scr, window, NULL);

	/*
	 * This is to let the Dock know that the application it launched
	 * has already been mapped (eg: it has finished launching).
	 * It is not necessary for normally docked apps, but is needed for
	 * apps that were forcedly docked (like with dockit).
	 */
	if (scr->last_dock) {
		pid_t client_pid = wGetWindowPid(wwin ? wwin->client_win : window);

		if (wwin && wwin->main_window != None && wwin->main_window != window)
			wDockTrackWindowLaunch(scr->last_dock, wwin->main_window, client_pid);
		else
			wDockTrackWindowLaunch(scr->last_dock, window, client_pid);
	}

	if (wwin) {
		wClientSetState(wwin, NormalState, None);
		if (wwin->flags.maximized) {
			wMaximizeWindow(wwin, wwin->flags.maximized,
					wGetHeadForWindow(wwin));
		}
		if (wwin->flags.shaded) {
			wwin->flags.shaded = 0;
			wwin->flags.skip_next_animation = 1;
			wShadeWindow(wwin);
		}
		if (wwin->flags.miniaturized) {
			wwin->flags.miniaturized = 0;
			wwin->flags.skip_next_animation = 1;
			wIconifyWindow(wwin);
		}
		if (wwin->flags.fullscreen) {
			wwin->flags.fullscreen = 0;
			wFullscreenWindow(wwin);
		}
		if (wwin->flags.hidden) {
			WApplication *wapp = wApplicationOf(wwin->main_window);

			wwin->flags.hidden = 0;
			wwin->flags.skip_next_animation = 1;
			if (wapp) {
				wHideApplication(wapp);
			}
		}
		/* tab-group membership saved in the session is applied after
		 * the deferred shade and iconify so an iconified group is
		 * rebuilt from members that already own a miniwindow */
		wGroupRestoreMember(wwin);
	}
}

static void handleDestroyNotify(XEvent * event)
{
	WWindow *wwin;
	WApplication *app;
	Window window = event->xdestroywindow.window;
	WScreen *scr = wScreenForRootWindow(event->xdestroywindow.event);
	int widx;

	wwin = wWindowFor(window);
	if (wwin) {
		wUnmanageWindow(wwin, False, True);
	}

	if (scr != NULL) {
		while ((widx = WMFindInArray(scr->fakeGroupLeaders, matchWindow, (void *)window)) != WANotFound) {
			WFakeGroupLeader *fPtr;

			fPtr = WMGetFromArray(scr->fakeGroupLeaders, widx);
			if (fPtr->retainCount > 0) {
				fPtr->retainCount--;
				if (fPtr->retainCount == 0 && fPtr->leader != None) {
					XDestroyWindow(dpy, fPtr->leader);
					fPtr->leader = None;
					XFlush(dpy);
				}
			}
			fPtr->origLeader = None;
		}
	}

	app = wApplicationOf(window);
	if (app) {
		if (window == app->main_window) {
			app->refcount = 0;
			wwin = app->main_window_desc->screen_ptr->focused_window;
			while (wwin) {
				if (wwin->main_window == window) {
					wwin->main_window = None;
				}
				wwin = wwin->prev;
			}
		}
		wApplicationDestroy(app);
	}
}

static void handleExpose(XEvent * event)
{
	WObjDescriptor *desc;
	XEvent ev;

	while (WXEvqTakeTyped(dpy, Expose, event->xexpose.window, &ev)) ;

	if (XFindContext(dpy, event->xexpose.window, w_global.context.client_win, (XPointer *) & desc) == XCNOENT) {
		return;
	}

	if (desc->handle_expose) {
		(*desc->handle_expose) (desc, event);
	}
}

static void executeWheelAction(WScreen *scr, XEvent *event, int action)
{
	WWindow *wwin;
	Bool next_direction;

	if (event->xbutton.button == Button5 || event->xbutton.button == Button6)
		next_direction = False;
	else
		next_direction = True;

	switch (action) {
	case WA_SWITCH_WORKSPACES:
		if (next_direction)
			wWorkspaceRelativeChange(scr, 1);
		else
			wWorkspaceRelativeChange(scr, -1);
		break;

	case WA_SWITCH_WINDOWS:
		wwin = scr->focused_window;
		if (next_direction)
			wWindowFocusNext(wwin, True);
		else
			wWindowFocusPrev(wwin, True);
		break;
	}
}

static void executeButtonAction(WScreen *scr, XEvent *event, int action)
{
	WWindow *wwin;

	switch (action) {
	case WA_SELECT_WINDOWS:
		wUnselectWindows(scr);
		wSelectWindows(scr, event);
		if (w_global.config->close_rootmenu_left_right_click){
			WMenu *menu = NULL;
			WMPropList *definition;
			menu = scr->root_menu;
			if (scr->root_menu){
				wMenuDestroy(menu,True);
				scr->root_menu = NULL;
				definition = w_global.domain.root_menu->dictionary;
				menu = configureMenu(scr, definition);
				scr->root_menu = menu;
			}
		}
		break;
	case WA_OPEN_APPMENU:
		OpenRootMenu(scr, event->xbutton.x_root, event->xbutton.y_root, False);
		/* ugly hack */
		if (scr->root_menu) {
			if (scr->root_menu->brother->flags.mapped)
				event->xbutton.window = scr->root_menu->brother->frame->core->window;
			else
				event->xbutton.window = scr->root_menu->frame->core->window;
		}
		break;
	case WA_OPEN_WINLISTMENU:
		OpenSwitchMenu(scr, event->xbutton.x_root, event->xbutton.y_root, False);
		if (scr->switch_menu) {
			if (scr->switch_menu->brother->flags.mapped)
				event->xbutton.window = scr->switch_menu->brother->frame->core->window;
			else
				event->xbutton.window = scr->switch_menu->frame->core->window;
		}
		break;
	case WA_MOVE_PREVWORKSPACE:
		wWorkspaceRelativeChange(scr, -1);
		break;
	case WA_MOVE_NEXTWORKSPACE:
		wWorkspaceRelativeChange(scr, 1);
		break;
	case WA_MOVE_PREVWINDOW:
		wwin = scr->focused_window;
		wWindowFocusPrev(wwin, True);
		break;
	case WA_MOVE_NEXTWINDOW:
		wwin = scr->focused_window;
		wWindowFocusNext(wwin, True);
		break;
	}
}

/* bindable */
static void handleButtonPress(XEvent * event)
{
	WObjDescriptor *desc;
	WScreen *scr;

	scr = wScreenForRootWindow(event->xbutton.root);

#ifdef BALLOON_TEXT
	wBalloonHide(scr);
#endif

	if (!w_global.config->disable_root_mouse && event->xbutton.window == scr->root_win) {
		if (event->xbutton.button == Button1 && w_global.config->mouse_button1 != WA_NONE) {
			executeButtonAction(scr, event, w_global.config->mouse_button1);
		} else if (event->xbutton.button == Button2 && w_global.config->mouse_button2 != WA_NONE) {
			executeButtonAction(scr, event, w_global.config->mouse_button2);
		} else if (event->xbutton.button == Button3 && w_global.config->mouse_button3 != WA_NONE) {
			executeButtonAction(scr, event, w_global.config->mouse_button3);
		} else if (event->xbutton.button == Button8 && w_global.config->mouse_button8 != WA_NONE) {
			executeButtonAction(scr, event, w_global.config->mouse_button8);
		}else if (event->xbutton.button == Button9 && w_global.config->mouse_button9 != WA_NONE) {
			executeButtonAction(scr, event, w_global.config->mouse_button9);
		} else if (event->xbutton.button == Button4 && w_global.config->mouse_wheel_scroll != WA_NONE) {
			executeWheelAction(scr, event, w_global.config->mouse_wheel_scroll);
		} else if (event->xbutton.button == Button5 && w_global.config->mouse_wheel_scroll != WA_NONE) {
			executeWheelAction(scr, event, w_global.config->mouse_wheel_scroll);
		} else if (event->xbutton.button == Button6 && w_global.config->mouse_wheel_tilt != WA_NONE) {
			executeWheelAction(scr, event, w_global.config->mouse_wheel_tilt);
		} else if (event->xbutton.button == Button7 && w_global.config->mouse_wheel_tilt != WA_NONE) {
			executeWheelAction(scr, event, w_global.config->mouse_wheel_tilt);
		}
	}

	desc = NULL;
	if (XFindContext(dpy, event->xbutton.subwindow, w_global.context.client_win, (XPointer *) & desc) == XCNOENT) {
		if (XFindContext(dpy, event->xbutton.window, w_global.context.client_win, (XPointer *) & desc) == XCNOENT) {
			return;
		}
	}

	if (desc->parent_type == WCLASS_WINDOW) {
		XSync(dpy, 0);

		if (event->xbutton.state & ( MOD_MASK | ControlMask )) {
			XAllowEvents(dpy, AsyncPointer, CurrentTime);
		} else {
			/*      if (w_global.config->focus_mode == WKF_CLICK) { */
			if (w_global.config->ignore_focus_click) {
				XAllowEvents(dpy, AsyncPointer, CurrentTime);
			}
			XAllowEvents(dpy, ReplayPointer, CurrentTime);
			/*      } */
		}
		XSync(dpy, 0);
	} else if (desc->parent_type == WCLASS_APPICON
		   || desc->parent_type == WCLASS_MINIWINDOW || desc->parent_type == WCLASS_DOCK_ICON) {
		if (event->xbutton.state & MOD_MASK) {
			XSync(dpy, 0);
			XAllowEvents(dpy, AsyncPointer, CurrentTime);
			XSync(dpy, 0);
		}
	}

	if (desc->handle_mousedown != NULL) {
		(*desc->handle_mousedown) (desc, event);
	}

	/* save double-click information */
	if (scr->flags.next_click_is_not_double) {
		scr->flags.next_click_is_not_double = 0;
	} else {
		scr->last_click_time = event->xbutton.time;
		scr->last_click_button = event->xbutton.button;
		scr->last_click_window = event->xbutton.window;
	}
}

static void handleMapNotify(XEvent * event)
{
	WWindow *wwin;

	wwin = wWindowFor(event->xmap.event);
	if (wwin && wwin->client_win == event->xmap.event) {
		if (wwin->flags.miniaturized) {
			wDeiconifyWindow(wwin);
		} else {
			XGrabServer(dpy);
			wWindowMap(wwin);
			wClientSetState(wwin, NormalState, None);
			XUngrabServer(dpy);
		}
	}
}

static void handleUnmapNotify(XEvent * event)
{
	WWindow *wwin;
	XEvent ev;
	Bool withdraw = False;

	/* only process windows with StructureNotify selected
	 * (ignore SubstructureNotify) */
	wwin = wWindowFor(event->xunmap.window);
	if (!wwin)
		return;

	/* whether the event is a Withdrawal request */
	if (event->xunmap.event == wwin->screen_ptr->root_win && event->xunmap.send_event)
		withdraw = True;

	if (wwin->client_win != event->xunmap.event && !withdraw)
		return;

	if (!wwin->flags.mapped && !withdraw
	    && wWindowIsVisibleOnAnyHead(wwin)
	    && !wwin->flags.miniaturized && !wwin->flags.hidden)
		return;

	XGrabServer(dpy);
	XUnmapWindow(dpy, wwin->frame->core->window);
	wwin->flags.mapped = 0;
	XSync(dpy, 0);
	/* check if the window was destroyed */
	if (WXEvqTakeTyped(dpy, DestroyNotify, wwin->client_win, &ev)) {
		DispatchEvent(&ev);
	} else {
		Bool reparented = False;

		if (WXEvqTakeTyped(dpy, ReparentNotify, wwin->client_win, &ev))
			reparented = True;

		/* withdraw window */
		wwin->flags.mapped = 0;
		if (!reparented)
			wClientSetState(wwin, WithdrawnState, None);

		/* if the window was reparented, do not reparent it back to the
		 * root window */
		wUnmanageWindow(wwin, !reparented, False);
	}
	XUngrabServer(dpy);
}

static void handleConfigureRequest(XEvent * event)
{
	WWindow *wwin;

	wwin = wWindowFor(event->xconfigurerequest.window);
	if (wwin == NULL) {
		/*
		 * Configure request for unmapped window
		 */
		wClientConfigure(NULL, &(event->xconfigurerequest));
	} else {
		wClientConfigure(wwin, &(event->xconfigurerequest));
	}
}

static void handlePropertyNotify(XEvent * event)
{
	WWindow *wwin;
	WApplication *wapp;
	Window jr;
	int ji;
	unsigned int ju;

	wwin = wWindowFor(event->xproperty.window);
	if (wwin) {
		if (!XGetGeometry(dpy, wwin->client_win, &jr, &ji, &ji, &ju, &ju, &ju, &ju)) {
			return;
		}
		wClientCheckProperty(wwin, &event->xproperty);
	} else {
		WScreen *scr = wScreenForRootWindow(event->xproperty.window);
		if (scr && scr->root_win == event->xproperty.window) {
			wNETWMCheckRootHintChange(scr, &event->xproperty);
			return;
		}
		/* a client may carry _NET_WM_USER_TIME on a separate
		 * helper window, route those notifies to the owner */
		wwin = wNETWMUserTimeWindowFor(event->xproperty.window);
		if (wwin) {
			wNETWMUserTimeNotify(wwin);
			return;
		}
	}
	wapp = wApplicationOf(event->xproperty.window);
	if (wapp) {
		wClientCheckProperty(wapp->main_window_desc, &event->xproperty);
	}
}

static void handleClientMessage(XEvent * event)
{
	WWindow *wwin;
	WObjDescriptor *desc;

	/* handle transition from Normal to Iconic state */
	if (event->xclient.message_type == w_global.atom.wm.change_state
	    && event->xclient.format == 32 && event->xclient.data.l[0] == IconicState) {

		wwin = wWindowFor(event->xclient.window);
		if (!wwin)
			return;
		if (!wwin->flags.miniaturized)
			wIconifyWindow(wwin);
	} else if (event->xclient.message_type == w_global.atom.wm.colormap_notify && event->xclient.format == 32) {
		WScreen *scr = wScreenForRootWindow(event->xclient.window);

		if (!scr)
			return;

		if (event->xclient.data.l[1] == 1) {	/* starting */
			wColormapAllowClientInstallation(scr, True);
		} else {	/* stopping */
			wColormapAllowClientInstallation(scr, False);
		}
	} else if (event->xclient.message_type == w_global.atom.wmaker.command) {

		char command[sizeof(event->xclient.data.b) + 1];

		/* WPrefs and setstyle send this as format 8, anything else
		 * means data.b is not the right view of the payload */
		if (event->xclient.format != 8)
			return;

		memcpy(command, event->xclient.data.b, sizeof(event->xclient.data.b));
		command[sizeof(event->xclient.data.b)] = '\0';

		/* the X-property control channel is deprecated, the in-tree
		 * senders use the Unix-socket IPC and only fall back here
		 * when connect() fails, anything else should switch to
		 * `wmakermsg reconfigure` */
		if (strncmp(command, "Reconfigure", sizeof("Reconfigure")) == 0) {
			wwarning(_("Got Reconfigure command over the deprecated X channel, use wmakermsg"));
			wDefaultsCheckDomains(NULL);
		} else {
			wwarning(_("Got unknown command %s over the deprecated X channel"), command);
		}

	} else if (event->xclient.message_type == w_global.atom.wmaker.wm_function) {
		WApplication *wapp;

		/* the sender may target either the group leader (then
		 * wApplicationOf finds it directly) or any managed window
		 * of the application, both arms used to be open-coded
		 * with the same switch, resolve once and hand each
		 * function the pointer it needs */
		wapp = wApplicationOf(event->xclient.window);
		wwin = wapp ? wapp->main_window_desc : wWindowFor(event->xclient.window);
		if (!wapp && wwin)
			wapp = wApplicationOf(wwin->main_window);

		switch (event->xclient.data.l[0]) {
		case WMFHideOtherApplications:
			if (wwin)
				wHideOtherApplications(wwin);
			break;
		case WMFHideApplication:
			if (wapp)
				wHideApplication(wapp);
			break;
		}
	} else if (event->xclient.message_type == w_global.atom.gnustep.wm_attr) {
		wwin = wWindowFor(event->xclient.window);
		if (!wwin)
			return;
		switch (event->xclient.data.l[0]) {
		case GSWindowLevelAttr:
			{
				int level = (int)event->xclient.data.l[1];

				if (WINDOW_LEVEL(wwin) != level) {
					ChangeStackingLevel(wwin->frame->core, level);
				}
			}
			break;
		}
	} else if (event->xclient.message_type == w_global.atom.gnustep.titlebar_state) {
		wwin = wWindowFor(event->xclient.window);
		if (!wwin)
			return;
		switch (event->xclient.data.l[0]) {
		case WMTitleBarNormal:
			wFrameWindowChangeState(wwin->frame, WS_UNFOCUSED);
			break;
		case WMTitleBarMain:
			wFrameWindowChangeState(wwin->frame, WS_PFOCUSED);
			break;
		case WMTitleBarKey:
			wFrameWindowChangeState(wwin->frame, WS_FOCUSED);
			break;
		}
	} else if (event->xclient.message_type == w_global.atom.wm.ignore_focus_events) {
		WScreen *scr = wScreenForRootWindow(event->xclient.window);
		if (!scr)
			return;
		scr->flags.ignore_focus_events = event->xclient.data.l[0] ? 1 : 0;
	} else if (wStartupNotifyProcessEvent(event)) {
		/* _NET_STARTUP_INFO{,_BEGIN}, consumed by libsn */
	} else if (wNETWMProcessClientMessage(&event->xclient)) {
		/* do nothing */
#ifdef USE_DOCK_XDND
	} else if (wXDNDProcessClientMessage(&event->xclient)) {
		/* do nothing */
#endif	/* USE_DOCK_XDND */
	} else {
		/*
		 * Non-standard thing, but needed by OffiX DND.
		 * For when the icon frame gets a ClientMessage
		 * that should have gone to the icon_window.
		 */
		if (XFindContext(dpy, event->xbutton.window, w_global.context.client_win, (XPointer *) & desc) != XCNOENT) {
			struct WIcon *icon = NULL;

			if (desc->parent_type == WCLASS_MINIWINDOW) {
				icon = (WIcon *) desc->parent;
			} else if (desc->parent_type == WCLASS_DOCK_ICON || desc->parent_type == WCLASS_APPICON) {
				icon = ((WAppIcon *) desc->parent)->icon;
			}
			if (icon && (wwin = icon->owner)) {
				if (wwin->client_win != event->xclient.window) {
					event->xclient.window = wwin->client_win;
					XSendEvent(dpy, wwin->client_win, False, NoEventMask, event);
				}
			}
		}
	}
}

static void raiseWindow(WScreen * scr)
{
	WWindow *wwin;

	scr->autoRaiseTimer = NULL;

	wwin = wWindowFor(scr->autoRaiseWindow);
	if (!wwin)
		return;

	if (!wwin->flags.destroyed && wwin->flags.focused) {
		wRaiseFrame(wwin->frame->core);
		/* this is needed or a race condition will occur */
		XSync(dpy, False);
	}
}

static void handleEnterNotify(XEvent * event)
{
	WWindow *wwin;
	WObjDescriptor *desc = NULL;
	XEvent ev;
	WScreen *scr = wScreenForRootWindow(event->xcrossing.root);

	if (WXEvqTakeTyped(dpy, LeaveNotify, event->xcrossing.window, &ev)) {
		/* already left the window... */
		saveTimestamp(&ev);
		if (ev.xcrossing.mode == event->xcrossing.mode && ev.xcrossing.detail == event->xcrossing.detail) {
			return;
		}
	}

	if (event->xcrossing.mode == NotifyNormal) {
		int c = wHotCornerForWindow(scr, event->xcrossing.window);
		if (c) {
			wHotCornersEnter(scr, c);
			return;
		}
	}

	if (XFindContext(dpy, event->xcrossing.window, w_global.context.client_win, (XPointer *) & desc) != XCNOENT) {
		if (desc->handle_enternotify)
			(*desc->handle_enternotify) (desc, event);
	}

	/* enter to window */
	wwin = wWindowFor(event->xcrossing.window);
	if (!wwin) {
		if (w_global.config->colormap_mode == WCM_POINTER) {
			wColormapInstallForWindow(scr, NULL);
		}
		if (scr->autoRaiseTimer && event->xcrossing.root == event->xcrossing.window) {
			WMDeleteTimerHandler(scr->autoRaiseTimer);
			scr->autoRaiseTimer = NULL;
		}
	} else {
		/* set auto raise timer even if in focus-follows-mouse mode
		 * and the event is for the frame window, even if the window
		 * has focus already.  useful if you move the pointer from a focused
		 * window to the root window and back pretty fast
		 *
		 * set focus if in focus-follows-mouse mode and the event
		 * is for the frame window and window doesn't have focus yet */
		if (w_global.config->focus_mode == WKF_SLOPPY
		    && wwin->frame->core->window == event->xcrossing.window && !scr->flags.doing_alt_tab) {

			if (!wwin->flags.focused && !WFLAGP(wwin, no_focusable))
				wSetFocusTo(scr, wwin);

			if (scr->autoRaiseTimer)
				WMDeleteTimerHandler(scr->autoRaiseTimer);
			scr->autoRaiseTimer = NULL;

			if (w_global.config->raise_delay && !WFLAGP(wwin, no_focusable)) {
				scr->autoRaiseWindow = wwin->frame->core->window;
				scr->autoRaiseTimer
				    = WMAddTimerHandler(w_global.config->raise_delay, (WMCallback *) raiseWindow, scr);
			}
		}
		/* Install colormap for window, if the colormap installation mode
		 * is colormap_follows_mouse */
		if (w_global.config->colormap_mode == WCM_POINTER) {
			if (wwin->client_win == event->xcrossing.window)
				wColormapInstallForWindow(scr, wwin);
			else
				wColormapInstallForWindow(scr, NULL);
		}
	}

	if (event->xcrossing.window == event->xcrossing.root
	    && event->xcrossing.detail == NotifyNormal
	    && event->xcrossing.detail != NotifyInferior && w_global.config->focus_mode != WKF_CLICK) {

		wSetFocusTo(scr, scr->focused_window);
	}
#ifdef BALLOON_TEXT
	wBalloonEnteredObject(scr, desc);
#endif
}

static void handleLeaveNotify(XEvent * event)
{
	WObjDescriptor *desc = NULL;
	WScreen *scr = wScreenForRootWindow(event->xcrossing.root);

	if (event->xcrossing.mode == NotifyNormal &&
	    wHotCornerForWindow(scr, event->xcrossing.window)) {
		wHotCornersLeave(scr);
		return;
	}

	if (XFindContext(dpy, event->xcrossing.window, w_global.context.client_win, (XPointer *) & desc) != XCNOENT) {
		if (desc->handle_leavenotify)
			(*desc->handle_leavenotify) (desc, event);
	}
}

#ifdef USE_XSHAPE
static void handleShapeNotify(XEvent * event)
{
	/* affected_window sits at wire offset 4 so the cooker already
	 * wrote it to xany.window and the WXEvqTakeTyped window match
	 * keeps working */
	xcb_shape_notify_event_t *se =
		(xcb_shape_notify_event_t *)((WXWireEvent *)event)->wire;
	int kind = se->shape_kind;
	int shaped = se->shaped;
	WWindow *wwin;
	XEvent ev;

	while (WXEvqTakeTyped(dpy, event->type, se->affected_window, &ev)) {
		xcb_shape_notify_event_t *ne =
			(xcb_shape_notify_event_t *)((WXWireEvent *)&ev)->wire;
		if (ne->shape_kind == XCB_SHAPE_SK_BOUNDING) {
			if (ne->shaped == shaped) {
				kind = ne->shape_kind;
				shaped = ne->shaped;
			} else {
				WXEvqPutBack(&ev);
				break;
			}
		}
	}

	wwin = wWindowFor(se->affected_window);
	if (!wwin || kind != XCB_SHAPE_SK_BOUNDING)
		return;

	if (!shaped && wwin->flags.shaped) {

		wwin->flags.shaped = 0;
		wWindowClearShape(wwin);

	} else if (shaped) {

		wwin->flags.shaped = 1;
		wWindowSetShape(wwin);
	}
}
#endif				/* USE_XSHAPE */

#ifdef KEEP_XKB_LOCK_STATUS
/* please help ]d if you know what to do */
static void handleXkbIndicatorStateNotify(XEvent *event)
{
	WWindow *wwin;
	WScreen *scr;
	int group;
	int i;
	(void) event;

	for (i = 0; i < w_global.screen_count; i++) {
		scr = wScreenWithNumber(i);
		wwin = scr->focused_window;
		if (wwin && wwin->flags.focused) {
			group = wXkbGetGroup();
			if (group >= 0 && wwin->frame->languagemode != group) {
				wwin->frame->last_languagemode = wwin->frame->languagemode;
				wwin->frame->languagemode = group;
				wWindowGetLanguageLabel(wwin->frame->languagemode, wwin->frame->language_label);
				wFrameWindowUpdateLanguageButton(wwin->frame);
			}
#ifdef XKB_BUTTON_HINT
			if (wwin->frame->titlebar) {
				wFrameWindowPaint(wwin->frame);
			}
#endif
		}
	}
}
#endif				/*KEEP_XKB_LOCK_STATUS */

static void handleColormapNotify(XEvent * event)
{
	WWindow *wwin;
	WScreen *scr;
	Bool reinstall = False;

	wwin = wWindowFor(event->xcolormap.window);
	if (!wwin)
		return;

	scr = wwin->screen_ptr;

	do {
		if (wwin) {
			if (event->xcolormap.new) {
				XWindowAttributes attr;

				XGetWindowAttributes(dpy, wwin->client_win, &attr);

				if (wwin == scr->cmap_window && wwin->cmap_window_no == 0)
					scr->current_colormap = attr.colormap;

				reinstall = True;
			} else if (event->xcolormap.state == ColormapUninstalled &&
				   scr->current_colormap == event->xcolormap.colormap) {

				/* some bastard app (like XV) removed our colormap */
				/*
				 * can't enforce or things like xscreensaver won't work
				 * reinstall = True;
				 */
			} else if (event->xcolormap.state == ColormapInstalled &&
				   scr->current_colormap == event->xcolormap.colormap) {

				/* someone has put our colormap back */
				reinstall = False;
			}
		}
	} while (WXEvqTakeTyped(dpy, ColormapNotify, None, event)
		 && ((wwin = wWindowFor(event->xcolormap.window)) || 1));

	if (reinstall && scr->current_colormap != None) {
		if (!scr->flags.colormap_stuff_blocked)
			XInstallColormap(dpy, scr->current_colormap);
	}
}

static void handleFocusIn(XEvent * event)
{
	WWindow *wwin;

	/*
	 * For applications that like stealing the focus.
	 */
	while (WXEvqTakeTyped(dpy, FocusIn, None, event)) ;
	saveTimestamp(event);
	if (event->xfocus.mode == NotifyUngrab
	    || event->xfocus.mode == NotifyGrab || event->xfocus.detail > NotifyNonlinearVirtual) {
		return;
	}

	wwin = wWindowFor(event->xfocus.window);
	if (wwin && !wwin->flags.focused) {
		if (wwin->flags.mapped)
			wSetFocusTo(wwin->screen_ptr, wwin);
		else
			wSetFocusTo(wwin->screen_ptr, NULL);
	} else if (!wwin) {
		WScreen *scr = wScreenForWindow(event->xfocus.window);
		if (scr)
			wSetFocusTo(scr, NULL);
	}
}

static WWindow *windowUnderPointer(WScreen * scr)
{
	unsigned int mask;
	int foo;
	Window bar, win;

	if (XQueryPointer(dpy, scr->root_win, &bar, &win, &foo, &foo, &foo, &foo, &mask))
		return wWindowFor(win);
	return NULL;
}

static int CheckFullScreenWindowFocused(WScreen * scr)
{
	if (scr->focused_window && scr->focused_window->flags.fullscreen)
		return 1;
	else
		return 0;
}

/* ------------------------------------------------------------------ *
 * Key-chain timeout support                                          *
 *                                                                    *
 * w_global.config->keychain_timeout_delay in milliseconds after a chain  *
 * leader is pressed, the chain is automatically cancelled so the     *
 * user is not stuck in a half-entered sequence. Set to 0 to disable. *
 * ------------------------------------------------------------------ */

static void hideChainOSD(WScreen *scr);

/* Cancels the chain on inactivity */
static void chainTimeoutCallback(void *data)
{
	WScreen *scr = (WScreen *)data;

	w_global.shortcut.chain_timeout_handler = NULL;
	hideChainOSD(scr);
	XUngrabKeyboard(dpy, CurrentTime);
	w_global.shortcut.curpos = NULL;
}

/* Start (or restart) the chain inactivity timer */
static void wStartChainTimer(WScreen *scr)
{
	if (w_global.config->keychain_timeout_delay > 0) {
		if (w_global.shortcut.chain_timeout_handler)
			WMDeleteTimerHandler(w_global.shortcut.chain_timeout_handler);
		w_global.shortcut.chain_timeout_handler =
			WMAddTimerHandler(w_global.config->keychain_timeout_delay, chainTimeoutCallback, scr);
	}
}

/* Cancel the chain inactivity timer, if armed */
static void wCancelChainTimer(void)
{
	if (w_global.shortcut.chain_timeout_handler) {
		WMDeleteTimerHandler(w_global.shortcut.chain_timeout_handler);
		w_global.shortcut.chain_timeout_handler = NULL;
	}
}

void wCancelKeyChain(WScreen *scr)
{
	wCancelChainTimer();
	hideChainOSD(scr);
	XUngrabKeyboard(dpy, CurrentTime);
	w_global.shortcut.curpos = NULL;
}

/* ------------------------------------------------------------------ *
 * Key-chain prefix OSD                                               *
 *                                                                    *
 * When KeychainDisplayPosition is not "none", entering or advancing *
 * a multi-stroke key chain pops up scr->keychain_osd showing the     *
 * keys typed so far, e.g. "Mod4+W" then "Mod4+W M". The popup is     *
 * hidden again on leaf execution, cancel, unrecognized key, or       *
 * inactivity timeout.                                                *
 * ------------------------------------------------------------------ */

#define CHAIN_OSD_PAD 8
/* The shortcut parser only caps the source string at MAX_SHORTCUT_LENGTH
 * with no explicit step limit, so MAX_SHORTCUT_LENGTH/2 single-character
 * steps is the hard upper bound on chain depth. */
#define CHAIN_OSD_MAX_STEPS (MAX_SHORTCUT_LENGTH / 2)

static void formatChainPosition(WKeyNode *pos, char *buf, size_t bufsz)
{
	WKeyNode *path[CHAIN_OSD_MAX_STEPS];
	WKeyNode *node;
	int depth, i;
	Bool truncated = False;

	depth = 0;
	for (node = pos; node != NULL; node = node->parent) {
		if (depth == CHAIN_OSD_MAX_STEPS) {
			truncated = True;
			break;
		}
		path[depth++] = node;
	}

	buf[0] = '\0';
	if (truncated)
		wstrlcat(buf, "... ", bufsz);

	for (i = depth - 1; i >= 0; i--) {
		char *seg;

		seg = GetShortcutLabelForKeycode(path[i]->modifier, path[i]->keycode);
		if (!seg)
			continue;

		if (buf[0] != '\0')
			wstrlcat(buf, " ", bufsz);
		wstrlcat(buf, seg, bufsz);
		wfree(seg);
	}
}

static void hideChainOSD(WScreen *scr)
{
	if (scr && scr->keychain_osd)
		WMUnmapWidget(scr->keychain_osd);
}

static void showChainOSD(WScreen *scr)
{
	char label[MAX_SHORTCUT_LENGTH * 2];
	WMRect rect;
	int head, w, h, px, py;

	if (!scr || !scr->keychain_osd)
		return;

	if (w_global.config->keychain_display_position == WD_NONE ||
	    w_global.shortcut.curpos == NULL) {
		hideChainOSD(scr);
		return;
	}

	formatChainPosition(w_global.shortcut.curpos, label, sizeof(label));
	WSetOSDViewText(scr->keychain_osd, label);

	head = wGetHeadForPointerLocation(scr);
	rect = wGetRectForHead(scr, head);
	w = WMWidgetWidth(scr->keychain_osd);
	h = WMWidgetHeight(scr->keychain_osd);

	switch (w_global.config->keychain_display_position) {
	case WD_TOP:
		px = rect.pos.x + (rect.size.width - w) / 2;
		py = rect.pos.y + CHAIN_OSD_PAD;
		break;
	case WD_BOTTOM:
		px = rect.pos.x + (rect.size.width - w) / 2;
		py = rect.pos.y + rect.size.height - h - CHAIN_OSD_PAD;
		break;
	case WD_TOPLEFT:
		px = rect.pos.x + CHAIN_OSD_PAD;
		py = rect.pos.y + CHAIN_OSD_PAD;
		break;
	case WD_TOPRIGHT:
		px = rect.pos.x + rect.size.width - w - CHAIN_OSD_PAD;
		py = rect.pos.y + CHAIN_OSD_PAD;
		break;
	case WD_BOTTOMLEFT:
		px = rect.pos.x + CHAIN_OSD_PAD;
		py = rect.pos.y + rect.size.height - h - CHAIN_OSD_PAD;
		break;
	case WD_BOTTOMRIGHT:
		px = rect.pos.x + rect.size.width - w - CHAIN_OSD_PAD;
		py = rect.pos.y + rect.size.height - h - CHAIN_OSD_PAD;
		break;
	case WD_CENTER:
	default:
		px = rect.pos.x + (rect.size.width - w) / 2;
		py = rect.pos.y + (rect.size.height - h) / 2;
		break;
	}

	WMMoveWidget(scr->keychain_osd, px, py);
	WMMapWidget(scr->keychain_osd);
	XRaiseWindow(dpy, WMWidgetXID(scr->keychain_osd));
}

void wRefreshKeyChainOSD(WScreen *scr)
{
	showChainOSD(scr);
}

#define ISMAPPED(w) ((w) && !(w)->flags.miniaturized && ((w)->flags.mapped || (w)->flags.shaded))
#define ISFOCUSED(w) ((w) && (w)->flags.focused)

static void startMarkCapture(WScreen *scr, Display *dpy, WMarkCaptureMode mode)
{
	scr->flags.mark_capture_mode = mode;
	XGrabKeyboard(dpy, scr->root_win, False, GrabModeAsync, GrabModeAsync, CurrentTime);
}

static void dispatchWKBDCommand(int command, WScreen *scr, WWindow *wwin, XEvent *event)
{
	short widx;
	int i;
#ifdef KEEP_XKB_LOCK_STATUS
	int xkbgroup;
#endif				/*KEEP_XKB_LOCK_STATUS */

	switch (command) {

	case WKBD_ROOTMENU:
		/*OpenRootMenu(scr, event->xkey.x_root, event->xkey.y_root, True); */
		if (!CheckFullScreenWindowFocused(scr)) {
			WMRect rect = wGetRectForHead(scr, wGetHeadForPointerLocation(scr));
			OpenRootMenu(scr, rect.pos.x + rect.size.width / 2, rect.pos.y + rect.size.height / 2,
				     True);
		}
		break;
	case WKBD_WINDOWLIST:
		if (!CheckFullScreenWindowFocused(scr)) {
			WMRect rect = wGetRectForHead(scr, wGetHeadForPointerLocation(scr));
			OpenSwitchMenu(scr, rect.pos.x + rect.size.width / 2, rect.pos.y + rect.size.height / 2,
				       True);
		}
		break;

	case WKBD_WINDOWMENU:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin))
			OpenWindowMenu(wwin, wwin->frame_x, wwin->frame_y + wwin->frame->top_width, True);
		break;
	case WKBD_MINIMIZEALL:
		CloseWindowMenu(scr);
		wHideAll(scr);
		break;
	case WKBD_MINIATURIZE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin)
		    && !WFLAGP(wwin, no_miniaturizable)) {
			CloseWindowMenu(scr);

			if (wwin->protocols.MINIATURIZE_WINDOW)
				wClientSendProtocol(wwin, w_global.atom.gnustep.wm_miniaturize_window, event->xbutton.time);
			else {
				wIconifyWindow(wwin);
			}
		}
		break;
	case WKBD_HIDE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin)) {
			CloseWindowMenu(scr);
			wRequestHideApplication(wApplicationOf(wwin->main_window), wwin);
		}
		break;
	case WKBD_HIDE_OTHERS:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin)) {
			CloseWindowMenu(scr);

			wHideOtherApplications(wwin);
		}
		break;
	case WKBD_MAXIMIZE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);

			handleMaximize(wwin, MAX_VERTICAL | MAX_HORIZONTAL | MAX_KEYBOARD);
		}
		break;
	case WKBD_FULLSCREEN:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin)
		    && (wwin->flags.fullscreen || IS_RESIZABLE(wwin))) {
			CloseWindowMenu(scr);
			if (wwin->flags.fullscreen)
				wUnfullscreenWindow(wwin);
			else
				wFullscreenWindow(wwin);
		}
		break;
	case WKBD_VMAXIMIZE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);

			handleMaximize(wwin, MAX_VERTICAL | MAX_KEYBOARD);
		}
		break;
	case WKBD_HMAXIMIZE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);

			handleMaximize(wwin, MAX_HORIZONTAL | MAX_KEYBOARD);
			movePointerToWindowCenter(wwin);
		}
		break;
	case WKBD_CENTRAL:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);

			handleMaximize(wwin, MAX_CENTRAL | MAX_KEYBOARD);
			movePointerToWindowCenter(wwin);
		}
		break;
	case WKBD_LHMAXIMIZE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);

			handleMaximize(wwin, MAX_VERTICAL | MAX_LEFTHALF | MAX_KEYBOARD);
			movePointerToWindowCenter(wwin);
		}
		break;
	case WKBD_RHMAXIMIZE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);

			handleMaximize(wwin, MAX_VERTICAL | MAX_RIGHTHALF | MAX_KEYBOARD);
			movePointerToWindowCenter(wwin);
		}
		break;
	case WKBD_THMAXIMIZE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);

			handleMaximize(wwin, MAX_HORIZONTAL | MAX_TOPHALF | MAX_KEYBOARD);
			movePointerToWindowCenter(wwin);
		}
		break;
	case WKBD_BHMAXIMIZE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);

			handleMaximize(wwin, MAX_HORIZONTAL | MAX_BOTTOMHALF | MAX_KEYBOARD);
			movePointerToWindowCenter(wwin);
		}
		break;
	case WKBD_LTCMAXIMIZE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);

			handleMaximize(wwin, MAX_LEFTHALF | MAX_TOPHALF | MAX_KEYBOARD);
			movePointerToWindowCenter(wwin);
		}
		break;
	case WKBD_RTCMAXIMIZE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);

			handleMaximize(wwin, MAX_RIGHTHALF | MAX_TOPHALF | MAX_KEYBOARD);
			movePointerToWindowCenter(wwin);
		}
		break;
	case WKBD_LBCMAXIMIZE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);

			handleMaximize(wwin, MAX_LEFTHALF | MAX_BOTTOMHALF | MAX_KEYBOARD);
			movePointerToWindowCenter(wwin);
		}
		 break;
	case WKBD_RBCMAXIMIZE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);

			handleMaximize(wwin, MAX_RIGHTHALF | MAX_BOTTOMHALF | MAX_KEYBOARD);
			movePointerToWindowCenter(wwin);
		}
		break;
	case WKBD_MAXIMUS:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && IS_RESIZABLE(wwin)) {
			CloseWindowMenu(scr);

			handleMaximize(wwin, MAX_MAXIMUS | MAX_KEYBOARD);
		}
		break;
	case WKBD_KEEP_ON_TOP:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin)) {
			CloseWindowMenu(scr);

			if (wwin->frame->core->stacking->window_level != WMFloatingLevel)
				ChangeStackingLevel(wwin->frame->core, WMFloatingLevel);
			else
				ChangeStackingLevel(wwin->frame->core, WMNormalLevel);
		}
		break;

	case WKBD_KEEP_AT_BOTTOM:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin)) {
			CloseWindowMenu(scr);

			if (wwin->frame->core->stacking->window_level != WMSunkenLevel)
				ChangeStackingLevel(wwin->frame->core, WMSunkenLevel);
			else
				ChangeStackingLevel(wwin->frame->core, WMNormalLevel);
		}
		break;
	case WKBD_OMNIPRESENT:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin)) {
			CloseWindowMenu(scr);

			wWindowSetOmnipresent(wwin, !wwin->flags.omnipresent);
		}
		break;
	case WKBD_RAISE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin)) {
			CloseWindowMenu(scr);

			wRaiseFrame(wwin->frame->core);
		}
		break;
	case WKBD_LOWER:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin)) {
			CloseWindowMenu(scr);

			wLowerFrame(wwin->frame->core);
		}
		break;
	case WKBD_RAISELOWER:
		/* raise or lower the window under the pointer, not the
		 * focused one
		 */
		wwin = windowUnderPointer(scr);
		if (wwin)
			wRaiseLowerFrame(wwin->frame->core);
		break;
	case WKBD_SHADE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && !WFLAGP(wwin, no_shadeable)) {
			if (wwin->flags.shaded)
				wUnshadeWindow(wwin);
			else
				wShadeWindow(wwin);
		}
		break;
	case WKBD_MOVERESIZE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && (IS_RESIZABLE(wwin) || IS_MOVABLE(wwin))) {
			CloseWindowMenu(scr);

			wKeyboardMoveResizeWindow(wwin);
		}
		break;
	case WKBD_CLOSE:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && !WFLAGP(wwin, no_closable)) {
			CloseWindowMenu(scr);
			if (wwin->protocols.DELETE_WINDOW) {
				wClientSendProtocol(wwin, w_global.atom.wm.delete_window, event->xkey.time);
				wNETWMPingWindow(wwin, event->xkey.time);
			}
		}
		break;
	case WKBD_SELECT:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin)) {
			wSelectWindow(wwin, !wwin->flags.selected);
		}
		break;

	case WKBD_WORKSPACEMAP:
		if (w_global.config->enable_workspace_pager)
			StartWorkspaceMap(scr);
		break;

	case WKBD_FOCUSNEXT:
		StartWindozeCycle(wwin, event, True, False);
		break;

	case WKBD_FOCUSPREV:
		StartWindozeCycle(wwin, event, False, False);
		break;

	case WKBD_FOCUSLEFT:
		wSetFocusToDirection(scr, DIRECTION_LEFT);
		break;

	case WKBD_FOCUSRIGHT:
		wSetFocusToDirection(scr, DIRECTION_RIGHT);
		break;

	case WKBD_FOCUSUP:
		wSetFocusToDirection(scr, DIRECTION_UP);
		break;

	case WKBD_FOCUSDOWN:
		wSetFocusToDirection(scr, DIRECTION_DOWN);
		break;

	case WKBD_GROUPNEXT:
		StartWindozeCycle(wwin, event, True, True);
		break;

	case WKBD_GROUPPREV:
		StartWindozeCycle(wwin, event, False, True);
		break;

	case WKBD_GROUPTAB_NEXT:
		wGroupTabNext(scr);
		break;

	case WKBD_GROUPTAB_PREV:
		wGroupTabPrev(scr);
		break;

	case WKBD_GROUPTAB_ATTACH:
		wGroupTabAttach(scr);
		break;

	case WKBD_GROUPTAB_DETACH:
		wGroupTabDetach(scr);
		break;

	case WKBD_WORKSPACE1 ... WKBD_WORKSPACE10:
		widx = command - WKBD_WORKSPACE1;
		i = (scr->current_workspace / 10) * 10 + widx;
		if (w_global.config->ws_advance || i < scr->workspace_count)
			wWorkspaceChange(scr, i);
		break;

	case WKBD_NEXTWORKSPACE:
		wWorkspaceRelativeChange(scr, 1);
		break;
	case WKBD_PREVWORKSPACE:
		wWorkspaceRelativeChange(scr, -1);
		break;
	case WKBD_LASTWORKSPACE:
		wWorkspaceChange(scr, scr->last_workspace);
		break;
	case WKBD_LEFTWORKSPACE:
		wWorkspaceDirectionalChange(scr, -1, 0);
		break;
	case WKBD_RIGHTWORKSPACE:
		wWorkspaceDirectionalChange(scr, 1, 0);
		break;
	case WKBD_UPWORKSPACE:
		wWorkspaceDirectionalChange(scr, 0, -1);
		break;
	case WKBD_DOWNWORKSPACE:
		wWorkspaceDirectionalChange(scr, 0, 1);
		break;

	case WKBD_MOVE_WORKSPACE1 ... WKBD_MOVE_WORKSPACE10:
		widx = command - WKBD_MOVE_WORKSPACE1;
		i = (scr->current_workspace / 10) * 10 + widx;
		if (wwin && (w_global.config->ws_advance || i < scr->workspace_count))
			wWindowChangeWorkspace(wwin, i);
		break;

	case WKBD_MOVE_NEXTWORKSPACE:
		if (wwin)
			wWindowChangeWorkspaceRelative(wwin, 1);
		break;
	case WKBD_MOVE_PREVWORKSPACE:
		if (wwin)
			wWindowChangeWorkspaceRelative(wwin, -1);
		break;
	case WKBD_MOVE_LASTWORKSPACE:
		if (wwin)
			wWindowChangeWorkspace(wwin, scr->last_workspace);
		break;
	case WKBD_MOVE_LEFTWORKSPACE:
	case WKBD_MOVE_RIGHTWORKSPACE:
	case WKBD_MOVE_UPWORKSPACE:
	case WKBD_MOVE_DOWNWORKSPACE:
		if (wwin) {
			int dc = 0, dr = 0;
			if (command == WKBD_MOVE_LEFTWORKSPACE)
				dc = -1;
			else if (command == WKBD_MOVE_RIGHTWORKSPACE)
				dc = 1;
			else if (command == WKBD_MOVE_UPWORKSPACE)
				dr = -1;
			else
				dr = 1;
			widx = wWorkspaceNeighbour(scr, scr->current_workspace, dc, dr);
			if (widx >= 0)
				wWindowChangeWorkspace(wwin, widx);
		}
		break;

	case WKBD_MOVE_NEXTWSLAYER:
	case WKBD_MOVE_PREVWSLAYER:
		{
			if (wwin) {
				int row, column;

				row = scr->current_workspace / 10;
				column = scr->current_workspace % 10;

				if (command == WKBD_MOVE_NEXTWSLAYER) {
					if ((row + 1) * 10 < scr->workspace_count)
						wWindowChangeWorkspace(wwin, column + (row + 1) * 10);
				} else {
					if (row > 0)
						wWindowChangeWorkspace(wwin, column + (row - 1) * 10);
				}
			}
		}
		break;

	case WKBD_WINDOW1:
	case WKBD_WINDOW2:
	case WKBD_WINDOW3:
	case WKBD_WINDOW4:
	case WKBD_WINDOW5:
	case WKBD_WINDOW6:
	case WKBD_WINDOW7:
	case WKBD_WINDOW8:
	case WKBD_WINDOW9:
	case WKBD_WINDOW10:

		widx = command - WKBD_WINDOW1;

		if (scr->shortcutWindows[widx]) {
			WMArray *list = scr->shortcutWindows[widx];
			int cw;
			int count = WMGetArrayItemCount(list);
			WWindow *twin;
			WMArrayIterator iter;
			WWindow *wwin;

			wUnselectWindows(scr);
			cw = scr->current_workspace;

			WM_ETARETI_ARRAY(list, wwin, iter) {
				if (count > 1)
					wWindowChangeWorkspace(wwin, cw);

				wMakeWindowVisible(wwin);

				if (count > 1)
					wSelectWindow(wwin, True);
			}

			/* rotate the order of windows, to create a cycling effect */
			twin = WMGetFromArray(list, 0);
			WMDeleteFromArray(list, 0);
			WMAddToArray(list, twin);

		} else if (wwin && ISMAPPED(wwin) && ISFOCUSED(wwin)) {
			if (scr->shortcutWindows[widx]) {
				WMFreeArray(scr->shortcutWindows[widx]);
				scr->shortcutWindows[widx] = NULL;
			}

			if (wwin->flags.selected && scr->selected_windows) {
				scr->shortcutWindows[widx] = WMDuplicateArray(scr->selected_windows);
				/*WMRemoveFromArray(scr->shortcutWindows[index], wwin);
				   WMInsertInArray(scr->shortcutWindows[index], 0, wwin); */
			} else {
				scr->shortcutWindows[widx] = WMCreateArray(4);
				WMAddToArray(scr->shortcutWindows[widx], wwin);
			}

			wSelectWindow(wwin, !wwin->flags.selected);
			XFlush(dpy);
			wusleep(3000);
			wSelectWindow(wwin, !wwin->flags.selected);
			XFlush(dpy);

		} else if (scr->selected_windows && WMGetArrayItemCount(scr->selected_windows)) {

			if (wwin->flags.selected && scr->selected_windows) {
				if (scr->shortcutWindows[widx]) {
					WMFreeArray(scr->shortcutWindows[widx]);
				}
				scr->shortcutWindows[widx] = WMDuplicateArray(scr->selected_windows);
			}
		}

		break;

	case WKBD_MOVE_12_TO_6_HEAD:
	case WKBD_MOVE_6_TO_12_HEAD:
		if (wwin)
			moveBetweenHeads(wwin, command - WKBD_MOVE_12_TO_6_HEAD);

		break;

	case WKBD_RELAUNCH:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin))
			(void) RelaunchWindow(wwin);

		break;

	case WKBD_SWITCH_SCREEN:
		if (w_global.screen_count > 1) {
			WScreen *scr2;
			int i;

			/* find index of this screen */
			for (i = 0; i < w_global.screen_count; i++) {
				if (wScreenWithNumber(i) == scr)
					break;
			}
			i++;
			if (i >= w_global.screen_count) {
				i = 0;
			}
			scr2 = wScreenWithNumber(i);

			if (scr2) {
				XWarpPointer(dpy, scr->root_win, scr2->root_win, 0, 0, 0, 0,
					     scr2->scr_width / 2, scr2->scr_height / 2);
			}
		}
		break;

	case WKBD_RUN:
	{
		ExecuteInputCommand(scr, _("exec %A(Run, Type command:)"));
		break;
	}

	case WKBD_EXIT:
	{
		/* quick mode is not allowed to prevent inadvertently call */
		ExecuteExitCommand(scr, 0);
		break;
	}

	case WKBD_PRINTS:
	{
		ScreenCapture(scr, PRINT_SCREEN);
		break;
	}

	case WKBD_PRINTW:
	{
		ScreenCapture(scr, PRINT_WINDOW);
		break;
	}

	case WKBD_PRINTP:
	{
		ScreenCapture(scr, PRINT_PARTIAL);
		break;
	}

	case WKBD_LOCKSCREEN:
		if (w_global.config->lock_screen_command &&
		    w_global.config->lock_screen_command[0])
			ExecuteShellCommand(scr, w_global.config->lock_screen_command);
		break;

	case WKBD_NEXTWSLAYER:
	case WKBD_PREVWSLAYER:
		{
			int row, column;

			row = scr->current_workspace / 10;
			column = scr->current_workspace % 10;

			if (command == WKBD_NEXTWSLAYER) {
				if ((row + 1) * 10 < scr->workspace_count)
					wWorkspaceChange(scr, column + (row + 1) * 10);
			} else {
				if (row > 0)
					wWorkspaceChange(scr, column + (row - 1) * 10);
			}
		}
		break;
	case WKBD_CLIPRAISELOWER:
		if (!wPreferences.flags.noclip)
			wDockRaiseLower(scr->workspaces[scr->current_workspace]->clip);
		break;
	case WKBD_DOCKRAISELOWER:
		if (!wPreferences.flags.nodock)
			wDockRaiseLower(scr->dock);
		break;
#ifdef KEEP_XKB_LOCK_STATUS
	case WKBD_TOGGLE:
		if (w_global.config->modelock) {
			/*toggle */
			wwin = scr->focused_window;

			if (wwin && wwin->flags.mapped
			    && wwin->frame->workspace == wwin->screen_ptr->current_workspace
			    && !wwin->flags.miniaturized && !wwin->flags.hidden) {
				xkbgroup = wXkbGetGroup();

				wwin->frame->languagemode = wwin->frame->last_languagemode;
				if (xkbgroup >= 0)
					wwin->frame->last_languagemode = xkbgroup;
				wXkbLockGroup(wwin->frame->languagemode);
				/* Update the language label text */
				wWindowGetLanguageLabel(wwin->frame->languagemode, wwin->frame->language_label);
			}
		}
		break;
#endif	/* KEEP_XKB_LOCK_STATUS */

	case WKBD_MARK_SET:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin))
			startMarkCapture(scr, dpy, MARK_CAPTURE_SET);
		break;

	case WKBD_MARK_UNSET:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin) && wwin->mark_key_label != NULL)
			wWindowUnsetMark(wwin);
		break;

	case WKBD_MARK_BRING:
		startMarkCapture(scr, dpy, MARK_CAPTURE_BRING);
		break;

	case WKBD_MARK_JUMP:
		startMarkCapture(scr, dpy, MARK_CAPTURE_JUMP);
		break;

	case WKBD_MARK_SWAP:
		if (ISMAPPED(wwin) && ISFOCUSED(wwin))
			startMarkCapture(scr, dpy, MARK_CAPTURE_SWAP);
		break;
	}
}

static void handleKeyPress(XEvent * event)
{
	WScreen *scr = wScreenForRootWindow(event->xkey.root);
	WWindow *wwin = scr->focused_window;
	WKeyNode  *siblings;
	WKeyNode  *match;
	WKeyAction *act;
	int modifiers;

	/* ignore CapsLock */
	modifiers = event->xkey.state & w_global.shortcut.modifiers_mask;

	/* ----------------------------------------------------------- *
	* Window mark capture mode                                     *
	*                                                              *
	* We grabbed the keyboard so all keypresses come here until    *
	* we release the grab.                                         *
	* ------------------------------------------------------------ */
	if (scr->flags.mark_capture_mode != MARK_CAPTURE_IDLE) {
		int capture_mode = scr->flags.mark_capture_mode;
		KeySym cap_ksym;
		char label[MAX_SHORTCUT_LENGTH];

		/* Skip modifier-only keypresses */
		cap_ksym = XLookupKeysym(&event->xkey, 0);
		if (cap_ksym == NoSymbol || IsModifierKey(cap_ksym))
			return;

		/* Real key received: exit capture mode and release grab */
		scr->flags.mark_capture_mode = MARK_CAPTURE_IDLE;
		XUngrabKeyboard(dpy, CurrentTime);

		if (!GetCanonicalShortcutLabel(modifiers, cap_ksym, label, sizeof(label)))
			wstrlcpy(label, "?", sizeof(label));

		if (capture_mode == MARK_CAPTURE_SET) {
			WWindow *target = scr->focused_window;
			Bool conflict = False;
			int i;

			/* Conflict check against static wmaker bindings */
			for (i = 0; i < WKBD_LAST; i++) {
				if (wKeyBindings[i].keycode == event->xkey.keycode && wKeyBindings[i].modifier == modifiers) {
					wwarning("window mark: '%s' is already a wmaker binding, mark not assigned", label);
					conflict = True;
					break;
				}
			}

			/* Conflict check against existing marks on other windows */
			if (!conflict) {
				WWindow *tw = scr->focused_window;
				while (tw) {
					if (tw != target && tw->mark_key_label != NULL && strcmp(tw->mark_key_label, label) == 0) {
						wwarning("window mark: label '%s' is already used by another window, mark not assigned", label);
						conflict = True;
						break;
					}
					tw = tw->prev;
				}
			}

			if (!conflict && target != NULL)
				wWindowSetMark(target, label);

		} else {
			/* Find marked window by label */
			WWindow *tw = scr->focused_window;

			while (tw) {
				if (tw->mark_key_label != NULL && strcmp(tw->mark_key_label, label) == 0)
					break;
				tw = tw->prev;
			}
			if (tw == NULL) {
				wwarning("window mark: no window labelled '%s'", label);
			} else if (capture_mode == MARK_CAPTURE_BRING) {
				if (tw->frame->workspace != scr->current_workspace)
					wWindowChangeWorkspace(tw, scr->current_workspace);
				wMakeWindowVisible(tw);
			} else if (capture_mode == MARK_CAPTURE_JUMP) {
				wMakeWindowVisible(tw);
			} else {
				/* MARK_CAPTURE_SWAP: swap position, size and workspace between focused and tw */
				WWindow *focused = scr->focused_window;
				int fx, fy, fw, fh, tx, ty, tw_w, tw_h;
				int f_ws, t_ws;

				if (focused == NULL || focused == tw)
					return;

				/* Snapshot both geometries */
				fx   = focused->frame_x;
				fy   = focused->frame_y;
				fw   = focused->client.width;
				fh   = focused->client.height;
				f_ws = focused->frame->workspace;

				tx   = tw->frame_x;
				ty   = tw->frame_y;
				tw_w = tw->client.width;
				tw_h = tw->client.height;
				t_ws = tw->frame->workspace;

				/* Swap workspaces first so configure lands in the right one */
				if (f_ws != t_ws) {
					wWindowChangeWorkspace(focused, t_ws);
					wWindowChangeWorkspace(tw, f_ws);
				}

				/* Swap positions and sizes */
				wWindowConfigure(focused, tx, ty, tw_w, tw_h);
				wWindowConfigure(tw, fx, fy, fw, fh);

				/* Follow origin window: switch view to the workspace it landed in,
				* then restore focus to it */
				if (f_ws != t_ws)
					wWorkspaceChange(scr, t_ws);
				wSetFocusTo(scr, focused);
			}
		}
		return;
	}

	/* ------------------------------------------------------------------ *
	 * Trie-based key-chain matching                                      *
	 *                                                                    *
	 * wKeyTreeRoot is a prefix trie covering ALL key bindings            *
	 * (wKeyBindings and root-menu shortcuts combined).                   *
	 * curpos tracks the last matched internal node.                      *
	 * NULL means we are at the root (idle).                              *
	 * ------------------------------------------------------------------ */

	if (w_global.shortcut.curpos != NULL) {
		/*  Inside a chain: look for the next key among children */
		if (event->xkey.keycode == wKeyBindings[WKBD_KEYCHAIN_CANCEL].keycode &&
			modifiers == wKeyBindings[WKBD_KEYCHAIN_CANCEL].modifier) {
			wCancelKeyChain(scr);
			return;
		}

		siblings = w_global.shortcut.curpos->first_child;
		match = wKeyTreeFind(siblings, modifiers, event->xkey.keycode);

		if (match != NULL && match->first_child != NULL) {
			/* Internal node: advance and keep waiting */
			w_global.shortcut.curpos = match;
			wStartChainTimer(scr);
			showChainOSD(scr);
			return;
		}

		if (match == NULL) {
			/* Unrecognized key inside chain: exit chain mode */
			wCancelKeyChain(scr);
			return;
		}

		/*
		 * Sticky-chain mode: when a KeychainCancelKey is configured,
		 * stay at the parent level after executing a leaf instead of always
		 * returning to root.
		 */
		if (wKeyBindings[WKBD_KEYCHAIN_CANCEL].keycode != 0) {
			WKeyNode *parent = match->parent;
			WKeyNode *child;
			int nchildren = 0;

			for (child = parent->first_child; child != NULL; child = child->next_sibling)
				nchildren++;

			if (nchildren > 1) {
				/* Multi-branch parent: stay in chain mode at this level */
				w_global.shortcut.curpos = parent;
				wStartChainTimer(scr);
				showChainOSD(scr);
			} else {
				/* Single-branch parent: nothing left to wait for, exit chain */
				wCancelKeyChain(scr);
			}
		} else {
			/* No cancel key configured: always exit chain after a leaf */
			wCancelKeyChain(scr);
		}
	} else {
		/* Idle: look for a root-level match */
		match = wKeyTreeFind(wKeyTreeRoot, modifiers, event->xkey.keycode);

		if (match == NULL) {
			/* Not a known shortcut: try to redispatch it */
			static int dontLoop = 0;

			if (dontLoop > 10) {
				wwarning("problem with key event processing code");
				return;
			}
			dontLoop++;
			if (wwin && wwin->flags.internal_window &&
				wwin->client_leader != None) {
				event->xany.window = wwin->client_leader;
				WMHandleEvent(event);
			}
			dontLoop--;

			return;
		}

		if (match->first_child != NULL) {
			/* Internal node: enter chain mode */
			w_global.shortcut.curpos = match;
			XGrabKeyboard(dpy, scr->root_win, False,
							GrabModeAsync, GrabModeAsync, CurrentTime);
			wStartChainTimer(scr);
			showChainOSD(scr);

			return;
		}
	}

	/* Execute all leaf actions for this key sequence */
	for (act = match->actions; act != NULL; act = act->next) {
		if (act->type == WKN_MENU) {
			WMenu *menu  = (WMenu *) act->u.menu.menu;
			WMenuEntry *entry = (WMenuEntry *) act->u.menu.entry;

			(*entry->callback)(menu, entry);
		} else {
			dispatchWKBDCommand(act->u.wkbd_idx, scr, wwin, event);
		}
	}
	return;
}

static void handleMotionNotify(XEvent *event)
{
	if (w_global.config->scrollable_menus) {
		WScreen *scr = wScreenForRootWindow(event->xmotion.root);
		WMPoint p = wmkpoint(event->xmotion.x_root, event->xmotion.y_root);
		WMRect rect = wGetRectForHead(scr, wGetHeadForPoint(scr, p));

		if (scr->flags.jump_back_pending ||
		    p.x <= (rect.pos.x + 1) ||
		    p.x >= (rect.pos.x + rect.size.width - 2) ||
		    p.y <= (rect.pos.y + 1) || p.y >= (rect.pos.y + rect.size.height - 2)) {
			WMenu *menu;

			menu = wMenuUnderPointer(scr);
			if (menu != NULL)
				wMenuScroll(menu);
		}
	}
}

static void handleVisibilityNotify(XEvent * event)
{
	WWindow *wwin;

	wwin = wWindowFor(event->xvisibility.window);
	if (!wwin)
		return;
	wwin->flags.obscured = (event->xvisibility.state == VisibilityFullyObscured);
}

static void handle_selection_request(XSelectionRequestEvent *event)
{
#ifdef USE_ICCCM_WMREPLACE
	static Atom atom_version = None;
	WScreen *scr;
	XSelectionEvent notify;

	/*
	 * This event must be sent to the slection requester to not block him
	 *
	 * We create it with the answer 'there is no selection' by default
	 */
	notify.type = SelectionNotify;
	notify.display = dpy;
	notify.requestor = event->requestor;
	notify.selection = event->selection;
	notify.target = event->target;
	notify.property = None; /* This says that there is no selection */
	notify.time = event->time;

	scr = wScreenForWindow(event->owner);
	if (!scr)
		goto not_our_selection;

	if (event->owner != scr->info_window)
		goto not_our_selection;

	if (event->selection != scr->sn_atom)
		goto not_our_selection;

	if (atom_version == None)
		atom_version = XInternAtom(dpy, "VERSION", False);

	if (event->target == atom_version) {
		static const long icccm_version[] = { 2, 0 };

		/*
		 * This protocol is defined in ICCCM 2.0:
		 * http://www.x.org/releases/X11R7.7/doc/xorg-docs/icccm/icccm.html
		 *  "Communication with the Window Manager by Means of Selections"
		 */

		/*
		 * Setting the property means the content of the selection is available
		 * According to the ICCCM spec, we need to support being asked for a property
		 * set to 'None' for compatibility with old clients
		 */
		notify.property = (event->property == None)?(event->target):(event->property);

		XChangeProperty(dpy, event->requestor, notify.property,
		                XA_INTEGER, 32, PropModeReplace,
		                (unsigned char *) icccm_version, wlengthof(icccm_version));
	}

 not_our_selection:
	if (notify.property == None)
		wwarning("received SelectionRequest(%s) for target=\"%s\" from requestor 0x%lX but we have no answer",
		         XGetAtomName(dpy, event->selection), XGetAtomName(dpy, event->target), (long) event->requestor);

	/* Send the answer to the requestor */
	XSendEvent(dpy, event->requestor, False, 0L, (XEvent *) &notify);

#else
	/*
	 * If the support for ICCCM window manager replacement was not enabled, we should not receive
	 * this kind of event, so we just ignore it (Conceptually, we should reply with 'SelectionNotify'
	 * event with property set to 'None' to tell that we don't have this selection, but that is a bit
	 * costly for an event that shall never happen).
	 */
	(void) event;
#endif
}

static void handle_selection_clear(XSelectionClearEvent *event)
{
#ifdef USE_ICCCM_WMREPLACE
	WScreen *scr = wScreenForWindow(event->window);

	if (!scr)
		return;

	if (event->selection != scr->sn_atom)
		return;

	wmessage(_("another window manager is replacing us!"));
	Shutdown(WSExitMode, True);
#else
	/*
	 * If the support for ICCCM window manager replacement was not enabled, we should not receive
	 * this kind of event, so we simply do nothing.
	 */
	(void) event;
#endif
}
