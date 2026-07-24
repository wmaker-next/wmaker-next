/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 1998-2003 Dan Pascu
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#ifdef __FreeBSD__
#include <sys/signal.h>
#endif

#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <WINGs/WXHints.h>
#include <X11/cursorfont.h>
#include <X11/Xproto.h>
#include <X11/Xlib-xcb.h>
#ifdef USE_XSHAPE
#include <xcb/shape.h>
#include <xcb/shape.h>
#endif
#ifdef USE_RANDR
#include <xcb/randr.h>
#include <xcb/randr.h>
#endif
#ifdef USE_XSYNC
#include <xcb/sync.h>
#include <xcb/sync.h>
#endif
#ifdef USE_XCB_SCREENSAVER
#include <xcb/screensaver.h>
#endif

#include "WindowMaker.h"
#include "GNUstep.h"
#include "texture.h"
#include "screen.h"
#include "window.h"
#include "actions.h"
#include "client.h"
#include "main.h"
#include "startup.h"
#include "dock.h"
#include "workspace.h"
#include "xinerama.h"
#include "keybind.h"
#include "framewin.h"
#include "session.h"
#include "defaults.h"
#include "properties.h"
#include "dialog.h"
#include "wmspec.h"
#include "event.h"
#include "hotcorner.h"
#include "ipc.h"
#include "wxkb.h"
#include "switchmenu.h"
#include "tabgroup.h"
#include "wconfigmodel.h"
#ifdef USE_DOCK_XDND
#include "xdnd.h"
#endif

#include "xutil.h"

/* for SunOS */
#ifndef SA_RESTART
# define SA_RESTART 0
#endif

/* Just in case, for weirdo systems */
#ifndef SA_NODEFER
# define SA_NODEFER 0
#endif

/***** Local *****/
static WScreen **wScreen = NULL;
static void manageAllWindows(WScreen * scr, int crashed);

/* primary error path, in-stream errors from xcb-issued reply-less
 * requests reach the WINGs event queue as response_type 0 and land
 * here via WXEvqSetErrorHook(), Xlib-issued errors arrive via the
 * catchXError shim below so the warning text and the
 * BadWindow-on-a-vanished-client suppression are identical for both
 * sources, FormatXError() takes the unpacked fields so neither path
 * needs a synthetic XErrorEvent */
static void wHandleXcbError(Display *d, unsigned int error_code,
			    unsigned int major_opcode, unsigned int minor_opcode,
			    unsigned long resource_id, unsigned long serial)
{
	char buffer[MAXLINE];

	/* ignore the benign races, a managed client can vanish between
	 * any two requests so BadWindow on a client-owned XID is normal */
	if (resource_id != None
	    && ((error_code == BadDrawable && major_opcode == X_GetGeometry)
		|| (error_code == BadMatch && major_opcode == X_SetInputFocus)
		|| error_code == BadWindow
		|| major_opcode == X_InstallColormap)) {
		return;
	}
	FormatXError(d, error_code, major_opcode, minor_opcode, resource_id,
		     serial, buffer, MAXLINE);
	wwarning(_("internal X error: %s"), buffer);
}

/* the residual Xlib request surface (grabs, reparent, core drawing
 * until Cairo) still routes through the global Xlib error handler,
 * unpack the XErrorEvent and forward to the same body */
static int catchXError(Display *d, XErrorEvent *error)
{
	wHandleXcbError(d, error->error_code, error->request_code,
			error->minor_code, error->resourceid, error->serial);
	return 0;
}

/*
 *----------------------------------------------------------------------
 * handleXIO-
 * 	Handle X shutdowns and other stuff.
 *----------------------------------------------------------------------
 */
static int handleXIO(Display * xio_dpy)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) xio_dpy;

	dpy = NULL;
	Exit(0);
	return 0;
}

/* self-pipe written by signal handlers to wake the main event loop */
static int sig_pipe[2] = { -1, -1 };

static void handleSigPipeInput(int fd, int mask, void *cdata)
{
	char buf[32];

	(void) mask;
	(void) cdata;

	/* drain the wake-up bytes */
	while (read(fd, buf, sizeof(buf)) > 0)
		;

	if (WCHECK_STATE(WSTATE_NEED_RESTART))
		wwarning("got signal - restarting");
	else if (WCHECK_STATE(WSTATE_NEED_REREAD))
		wwarning("got signal - rereading defaults");
	else if (WCHECK_STATE(WSTATE_NEED_EXIT))
		wwarning("got signal - exiting...");

	/* act on the state flags from main-thread context */
	DispatchEvent(NULL);
}

/*
 *----------------------------------------------------------------------
 * handleExitSig--
 * 	User generated exit signal handler.
 *
 * 	Only async-signal-safe calls here: set the state flag and write
 * 	one byte to the self-pipe so the main event loop wakes up and
 * 	runs DispatchEvent(NULL) from handleSigPipeInput.
 *----------------------------------------------------------------------
 */
static RETSIGTYPE handleExitSig(int sig)
{
	int save_errno = errno;

	if (sig == SIGUSR1) {
		SIG_WCHANGE_STATE(WSTATE_NEED_RESTART);
	} else if (sig == SIGUSR2) {
		SIG_WCHANGE_STATE(WSTATE_NEED_REREAD);
	} else if (sig == SIGTERM || sig == SIGINT || sig == SIGHUP) {
		SIG_WCHANGE_STATE(WSTATE_NEED_EXIT);
	}

	if (sig_pipe[1] >= 0) {
		ssize_t r = write(sig_pipe[1], "x", 1);
		(void) r;
	}

	errno = save_errno;
}

/* Dummy signal handler */
static void dummyHandler(int sig)
{
	/* Parameter is not used, but tell the compiler that it is ok */
	(void) sig;
}

/*
 *----------------------------------------------------------------------
 * handleSig--
 * 	general signal handler. Exits the program gently.
 *----------------------------------------------------------------------
 */
static RETSIGTYPE handleSig(int sig)
{
	static const char msg[] = "wmaker: got fatal signal\n";
	ssize_t r = write(STDERR_FILENO, msg, sizeof(msg) - 1);

	(void) r;

	/* Setting the signal behaviour back to default and then reraising the
	 * signal is a cleaner way to make program exit and core dump than calling
	 * abort(), since it correctly returns from the signal handler and sets
	 * the flags accordingly. -Dan
	 */
	if (sig == SIGSEGV || sig == SIGFPE || sig == SIGBUS || sig == SIGILL || sig == SIGABRT) {
		signal(sig, SIG_DFL);
		kill(getpid(), sig);
		return;
	}

	wAbort(0);
}

static RETSIGTYPE buryChild(int foo)
{
	pid_t pid;
	int status;
	int save_errno = errno;
	sigset_t sigs;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;

	sigfillset(&sigs);
	/* Block signals so that NotifyDeadProcess() doesn't get fux0red */
	sigprocmask(SIG_BLOCK, &sigs, NULL);

	/* R.I.P. */
	/* If 2 or more kids exit in a small time window, before this handler gets
	 * the chance to get invoked, the SIGCHLD signals will be merged and only
	 * one SIGCHLD signal will be sent to us. We use a while loop to get all
	 * exited child status because we can't count on the number of SIGCHLD
	 * signals to know exactly how many kids have exited. -Dan
	 */
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0 || (pid < 0 && errno == EINTR)) {
		if (pid > 0)
			NotifyDeadProcess(pid, status);
	}

	sigprocmask(SIG_UNBLOCK, &sigs, NULL);

	if (sig_pipe[1] >= 0) {
		ssize_t r = write(sig_pipe[1], "x", 1);
		(void) r;
	}

	errno = save_errno;
}

WScreen *wScreenWithNumber(int i)
{
	assert(i < w_global.screen_count);

	return wScreen[i];
}

WScreen *wScreenForRootWindow(Window window)
{
	int i;

	if (w_global.screen_count == 1)
		return wScreen[0];

	/* Since the number of heads will probably be small (normally 2),
	 * it should be faster to use this than a hash table, because
	 * of the overhead. */
	for (i = 0; i < w_global.screen_count; i++)
		if (wScreen[i]->root_win == window)
			return wScreen[i];

	return wScreenForWindow(window);
}

WScreen *wScreenForWindow(Window window)
{
	XWindowAttributes attr;

	if (w_global.screen_count == 1)
		return wScreen[0];

	if (XGetWindowAttributes(dpy, window, &attr))
		return wScreenForRootWindow(attr.root);

	return NULL;
}

/*
 * Core atoms interned at startup. Each row is the field path under
 * w_global.atom and the X atom name. The list is expanded twice, once
 * into atomNames[] for the batched XInternAtoms() call and once into the
 * assignment sequence, so the name array and the field stores cannot
 * drift out of step. To add an atom, add one row here and the matching
 * Atom field in WindowMaker.h, no index renumbering.
 */
#define WGLOBAL_ATOMS(_) \
	_(wm.state,                     "WM_STATE") \
	_(wm.change_state,              "WM_CHANGE_STATE") \
	_(wm.protocols,                 "WM_PROTOCOLS") \
	_(wm.take_focus,                "WM_TAKE_FOCUS") \
	_(wm.delete_window,             "WM_DELETE_WINDOW") \
	_(wm.save_yourself,             "WM_SAVE_YOURSELF") \
	_(wm.client_leader,             "WM_CLIENT_LEADER") \
	_(wm.colormap_windows,          "WM_COLORMAP_WINDOWS") \
	_(wm.colormap_notify,           "WM_COLORMAP_NOTIFY") \
	_(wm.motif_wm_hints,            "_MOTIF_WM_HINTS") \
	_(wm.ignore_focus_events,       "WM_IGNORE_FOCUS_EVENTS") \
	_(wmaker.menu,                  "_WINDOWMAKER_MENU") \
	_(wmaker.state,                 "_WINDOWMAKER_STATE") \
	_(wmaker.wm_protocols,          "_WINDOWMAKER_WM_PROTOCOLS") \
	_(wmaker.wm_function,           "_WINDOWMAKER_WM_FUNCTION") \
	_(wmaker.noticeboard,           "_WINDOWMAKER_NOTICEBOARD") \
	_(wmaker.command,               "_WINDOWMAKER_COMMAND") \
	_(wmaker.icon_size,             "_WINDOWMAKER_ICON_SIZE") \
	_(wmaker.icon_tile,             "_WINDOWMAKER_ICON_TILE") \
	_(wmaker.mark_key,              "_WINDOWMAKER_MARK_KEY") \
	_(wmaker.tab_group,             "_WINDOWMAKER_TABGROUP") \
	_(gnustep.wm_attr,              GNUSTEP_WM_ATTR_NAME) \
	_(gnustep.wm_miniaturize_window, GNUSTEP_WM_MINIATURIZE_WINDOW) \
	_(gnustep.wm_hide_app,          GNUSTEP_WM_HIDE_APP) \
	_(gnustep.titlebar_state,       GNUSTEP_TITLEBAR_STATE) \
	_(desktop.gtk_object_path,      "_GTK_APPLICATION_OBJECT_PATH")

#define WATOM_NAME(field, name)		name,
static char *atomNames[] = {
	WGLOBAL_ATOMS(WATOM_NAME)
};
#undef WATOM_NAME

/*
 *----------------------------------------------------------
 * StartUp--
 * 	starts the window manager and setup global data.
 * Called from main() at startup.
 *
 * Side effects:
 * global data declared in main.c is initialized
 *----------------------------------------------------------
 */
void StartUp(Bool defaultScreenOnly)
{
	static const char empty_bits[(16 * 16) / 8];
	struct sigaction sig_action;
	int i, j, max;
	int idx;
	char **formats;
	Pixmap cur;
	XColor black;
	Atom atom[wlengthof(atomNames)];
	xcb_connection_t *xc = XGetXCBConnection(dpy);
	const xcb_query_extension_reply_t *xer;
#ifdef USE_XSYNC
	xcb_sync_initialize_reply_t *syncr;
#endif
#ifdef USE_RANDR
	xcb_randr_query_version_reply_t *rrr = NULL;
	uint32_t rr_major = 0, rr_minor = 0;
#endif

	/* No active key chain at startup */
	w_global.shortcut.curpos = NULL;
	w_global.shortcut.chain_timeout_handler = NULL;

	memset(&wKeyBindings, 0, sizeof(wKeyBindings));

	w_global.context.client_win = XUniqueContext();
	w_global.context.app_win = XUniqueContext();
	w_global.context.stack = XUniqueContext();

	/*    _XA_VERSION = XInternAtom(dpy, "VERSION", False); */

	XInternAtoms(dpy, atomNames, wlengthof(atomNames), False, atom);

	idx = 0;
#define WATOM_ASSIGN(field, name)	w_global.atom.field = atom[idx++];
	WGLOBAL_ATOMS(WATOM_ASSIGN)
#undef WATOM_ASSIGN

#ifdef USE_DOCK_XDND
	wXDNDInitializeAtoms();
#endif

	/* cursors */
	wPreferences.cursor[WCUR_NORMAL] = None;	/* inherit from root */
	wPreferences.cursor[WCUR_ROOT] = XCreateFontCursor(dpy, XC_left_ptr);
	wPreferences.cursor[WCUR_ARROW] = XCreateFontCursor(dpy, XC_top_left_arrow);
	wPreferences.cursor[WCUR_MOVE] = XCreateFontCursor(dpy, XC_fleur);
	wPreferences.cursor[WCUR_RESIZE] = XCreateFontCursor(dpy, XC_sizing);
	wPreferences.cursor[WCUR_TOPLEFTRESIZE] = XCreateFontCursor(dpy, XC_top_left_corner);
	wPreferences.cursor[WCUR_TOPRIGHTRESIZE] = XCreateFontCursor(dpy, XC_top_right_corner);
	wPreferences.cursor[WCUR_BOTTOMLEFTRESIZE] = XCreateFontCursor(dpy, XC_bottom_left_corner);
	wPreferences.cursor[WCUR_BOTTOMRIGHTRESIZE] = XCreateFontCursor(dpy, XC_bottom_right_corner);
	wPreferences.cursor[WCUR_VERTICALRESIZE] = XCreateFontCursor(dpy, XC_sb_v_double_arrow);
	wPreferences.cursor[WCUR_HORIZONRESIZE] = XCreateFontCursor(dpy, XC_sb_h_double_arrow);
	wPreferences.cursor[WCUR_WAIT] = XCreateFontCursor(dpy, XC_watch);
	wPreferences.cursor[WCUR_QUESTION] = XCreateFontCursor(dpy, XC_question_arrow);
	wPreferences.cursor[WCUR_TEXT] = XCreateFontCursor(dpy, XC_xterm);	/* odd name??? */
	wPreferences.cursor[WCUR_SELECT] = XCreateFontCursor(dpy, XC_cross);
	wPreferences.cursor[WCUR_CAPTURE] = XCreateFontCursor(dpy, XC_crosshair);

	memset(&black, 0, sizeof(black));
	cur = XCreateBitmapFromData(dpy, DefaultRootWindow(dpy), empty_bits, 16, 16);
	wPreferences.cursor[WCUR_EMPTY] = XCreatePixmapCursor(dpy, cur, cur, &black, &black, 0, 0);
	XFreePixmap(dpy, cur);

	/* self-pipe so signal handlers can wake the main loop without
	 * calling async-signal-unsafe functions from handler context */
	if (pipe(sig_pipe) == 0) {
		fcntl(sig_pipe[0], F_SETFL, O_NONBLOCK);
		fcntl(sig_pipe[1], F_SETFL, O_NONBLOCK);
		fcntl(sig_pipe[0], F_SETFD, FD_CLOEXEC);
		fcntl(sig_pipe[1], F_SETFD, FD_CLOEXEC);
		WMAddInputHandler(sig_pipe[0], WIReadMask, handleSigPipeInput, NULL);
	} else {
		werror("could not create signal wake-up pipe");
		sig_pipe[0] = sig_pipe[1] = -1;
	}

	/* emergency exit... */
	sig_action.sa_handler = handleSig;
	sigemptyset(&sig_action.sa_mask);

	sig_action.sa_flags = SA_RESTART;
	sigaction(SIGQUIT, &sig_action, NULL);
	/* instead of catching these, we let the default handler abort the
	 * program. The new monitor process will take appropriate action
	 * when it detects the crash.
	 sigaction(SIGSEGV, &sig_action, NULL);
	 sigaction(SIGBUS, &sig_action, NULL);
	 sigaction(SIGFPE, &sig_action, NULL);
	 sigaction(SIGABRT, &sig_action, NULL);
	 */

	sig_action.sa_handler = handleExitSig;

	/* Here we set SA_RESTART for safety, because SIGUSR1 may not be handled
	 * immediately. -Dan */
	sig_action.sa_flags = SA_RESTART;
	sigaction(SIGTERM, &sig_action, NULL);
	sigaction(SIGINT, &sig_action, NULL);
	sigaction(SIGHUP, &sig_action, NULL);
	sigaction(SIGUSR1, &sig_action, NULL);
	sigaction(SIGUSR2, &sig_action, NULL);

	/* ignore dead pipe */
	/* Because POSIX mandates that only signal with handlers are reset
	 * across an exec*(), we do not want to propagate ignoring SIGPIPEs
	 * to children. Hence the dummy handler.
	 * Philippe Troin <phil@fifi.org>
	 */
	sig_action.sa_handler = &dummyHandler;
	sig_action.sa_flags = SA_RESTART;
	sigaction(SIGPIPE, &sig_action, NULL);

	/* handle dead children */
	sig_action.sa_handler = buryChild;
	sig_action.sa_flags = SA_NOCLDSTOP | SA_RESTART;
	sigaction(SIGCHLD, &sig_action, NULL);

	/* Now we unblock all signals, that may have been blocked by the parent
	 * who exec()-ed us. This can happen for example if Window Maker crashes
	 * and restarts itself or another window manager from the signal handler.
	 * In this case, the new process inherits the blocked signal mask and
	 * will no longer react to that signal, until unblocked.
	 * This is because the signal handler of the process who crashed (parent)
	 * didn't return, and the signal remained blocked. -Dan
	 */
	sigfillset(&sig_action.sa_mask);
	sigprocmask(SIG_UNBLOCK, &sig_action.sa_mask, NULL);

	/* handle X shutdowns a such */
	XSetIOErrorHandler(handleXIO);

	/* set hook for out event dispatcher in WINGs event dispatcher */
	WMHookEventHandler(DispatchEvent);

	/* initialize defaults stuff */
	w_global.domain.wmaker = wDefaultsInitDomain("WindowMaker", True);
	if (!w_global.domain.wmaker->dictionary)
		wwarning(_("could not read domain \"%s\" from defaults database"), "WindowMaker");

	/* parse the WindowMaker domain once and adopt it as w_global.config
	 * immediately so every reader sees the typed values during screen
	 * init, the same WConfig is then reused for the per-screen runtime
	 * apply below */
	WMPropList *wmaker_dict = w_global.domain.wmaker ?
	    w_global.domain.wmaker->dictionary : NULL;
	WConfig *new_config = wConfigReadWindowMaker(wmaker_dict);
	WConfigChanges changes;

	wConfigAdoptWindowMaker(new_config);

	/* derive the few startup-only flags that are not direct config reads */
	wConfigApplyStatic(new_config);

	/* check sanity of some values */
	if (new_config->icon_size < 16) {
		wwarning(_("icon size is configured to %i, but it's too small. Using 16 instead"),
			 new_config->icon_size);
		new_config->icon_size = 16;
	}

	/* init other domains */
	w_global.domain.root_menu = wDefaultsInitDomain("WMRootMenu", False);
	if (!w_global.domain.root_menu->dictionary)
		wwarning(_("could not read domain \"%s\" from defaults database"), "WMRootMenu");

	wDefaultsMergeGlobalMenus(w_global.domain.root_menu);

	/* WMWindowAttributes outer keys are X11 instance.class strings and
	 * are case-sensitive ("xterm" != "XTerm"), keep them distinct while
	 * the user and global domain files are read and merged */
	WMPLSetCaseSensitive(True);
	w_global.domain.window_attr = wDefaultsInitDomain("WMWindowAttributes", True);
	WMPLSetCaseSensitive(False);
	if (!w_global.domain.window_attr->dictionary)
		wwarning(_("could not read domain \"%s\" from defaults database"), "WMWindowAttributes");
	wWindowAttrsReload();

	/* WMGLOBAL holds WINGs-global options (AntialiasedText, SystemFont, ...);
	 * the WM only tracks it for live-reload change detection, WINGs reads
	 * the values itself via WMGetStandardUserDefaults/WMReloadConfiguration. */
	w_global.domain.wmglobal = wDefaultsInitDomain("WMGLOBAL", False);

	XSetErrorHandler((XErrorHandler) catchXError);
	WXEvqSetErrorHook(wHandleXcbError);

	/* prefetch the QueryExtension probes so they fly in one round-trip,
	 * then collect first_event from the cached replies and negotiate
	 * versions, the cached reply is owned by xcb so do not free xer */
	(void) xer;
#ifdef USE_XSHAPE
	xcb_prefetch_extension_data(xc, &xcb_shape_id);
#endif
#ifdef USE_XSYNC
	xcb_prefetch_extension_data(xc, &xcb_sync_id);
#endif
#ifdef USE_RANDR
	xcb_prefetch_extension_data(xc, &xcb_randr_id);
#endif
#ifdef USE_XCB_SCREENSAVER
	xcb_prefetch_extension_data(xc, &xcb_screensaver_id);
#endif

#ifdef USE_XSHAPE
	xer = xcb_get_extension_data(xc, &xcb_shape_id);
	w_global.xext.shape.supported = xer && xer->present;
	if (w_global.xext.shape.supported) {
		w_global.xext.shape.event_base = xer->first_event;
		free(xcb_shape_query_version_reply(xc, xcb_shape_query_version(xc), NULL));
	}
#endif

#ifdef USE_XSYNC
	xer = xcb_get_extension_data(xc, &xcb_sync_id);
	w_global.xext.xsync.supported = 0;
	if (xer && xer->present) {
		w_global.xext.xsync.event_base = xer->first_event;
		syncr = xcb_sync_initialize_reply(xc,
				xcb_sync_initialize(xc, XCB_SYNC_MAJOR_VERSION,
						    XCB_SYNC_MINOR_VERSION), NULL);
		w_global.xext.xsync.supported = syncr != NULL;
		free(syncr);
	}
#endif

#ifdef USE_RANDR
	xer = xcb_get_extension_data(xc, &xcb_randr_id);
	if (xer && xer->present) {
		w_global.xext.randr.event_base = xer->first_event;
		rrr = xcb_randr_query_version_reply(xc,
				xcb_randr_query_version(xc, XCB_RANDR_MAJOR_VERSION,
							XCB_RANDR_MINOR_VERSION), NULL);
		if (rrr) {
			rr_major = rrr->major_version;
			rr_minor = rrr->minor_version;
		}
	}
	if (rrr && (rr_major > 1 || (rr_major == 1 && rr_minor >= 5))) {
		w_global.xext.randr.supported = 1;
	} else {
		w_global.xext.randr.supported = 0;
		if (!xer || !xer->present)
			wwarning(_("RandR extension is not available"));
		else if (!rrr)
			wwarning(_("RandR version check failed, RandR disabled"));
		else
			wwarning(_("RandR version %u.%u found but RandR version >=1.5 required"),
				 rr_major, rr_minor);
	}
	free(rrr);
#endif

#ifdef USE_XCB_SCREENSAVER
	xer = xcb_get_extension_data(xc, &xcb_screensaver_id);
	w_global.xext.screensaver.supported = xer && xer->present;
	if (w_global.xext.screensaver.supported) {
		w_global.xext.screensaver.event_base = xer->first_event;
		free(xcb_screensaver_query_version_reply(xc,
			xcb_screensaver_query_version(xc,
				XCB_SCREENSAVER_MAJOR_VERSION,
				XCB_SCREENSAVER_MINOR_VERSION), NULL));
	}
#endif

	/* wXkbInit() negotiates XkbUseExtension, loads the keymap and state,
	 * selects per-device events, derives the named-modifier and lock
	 * masks, and writes w_global.xext.xkb and shortcut.modifiers_mask.
	 * It runs before wScreenInit so wConfigApplyModifierKey and the
	 * keybinding parser see the resolved Alt/Meta/Super masks, and
	 * before wWindowSetKeyGrabs so the lock-mask grab fan-out is
	 * correct on the first manage. */
	wXkbInit();
#ifdef KEEP_XKB_LOCK_STATUS
	if (w_global.config->modelock && !w_global.xext.xkb.supported) {
		wwarning(_("XKB is not supported. KbdModeLock is automatically disabled."));
		w_global.config->modelock = 0;
	}
#endif

	/* extension probes have filled w_global.xext.*.event_base, so the
	 * per-protocol handler tables can register their slots now,
	 * DispatchEvent() was hooked above as the WINGs extra event
	 * handler so events on non-view windows reach the table */
	wEventInitDispatch();

	if (defaultScreenOnly)
		max = 1;
	else
		max = ScreenCount(dpy);

	wScreen = wmalloc(sizeof(WScreen *) * max);

	w_global.screen_count = 0;

	/* Check if TIFF images are supported */
	formats = RSupportedFileFormats();
	if (formats) {
		for (i = 0; formats[i] != NULL; i++) {
			if (strcmp(formats[i], "TIFF") == 0) {
				wPreferences.supports_tiff = 1;
				break;
			}
		}
	}

	/* old_config is NULL at startup so every key registers as changed
	 * and the per-screen apply realises every resource */
	wConfigPrepareRuntimeDefaults(wmaker_dict, NULL, new_config, &changes);

	/* manage the screens */
	for (j = 0; j < max; j++) {
		if (defaultScreenOnly || max == 1) {
			wScreen[w_global.screen_count] = wScreenInit(DefaultScreen(dpy), new_config, &changes);
			if (!wScreen[w_global.screen_count]) {
				wfatal(_("it seems that there is already a window manager running"));
				Exit(1);
			}
		} else {
			wScreen[w_global.screen_count] = wScreenInit(j, new_config, &changes);
			if (!wScreen[w_global.screen_count]) {
				wwarning(_("could not manage screen %i"), j);
				continue;
			}
		}
		w_global.screen_count++;
	}

	/* bind the per-screen IPC sockets before session restore and dock
	 * auto-launch so spawned children inherit a live WMAKER_IPC_SOCKET */
	for (j = 0; j < w_global.screen_count; j++)
		wIPCServerStart(wScreen[j]);

	InitializeSwitchMenu();

	/* initialize/restore state for the screens */
	for (j = 0; j < w_global.screen_count; j++) {
		int lastDesktop;

		lastDesktop = wNETWMGetCurrentDesktopFromHint(wScreen[j]);

		/* mark the screen as being brought up before OpenRootMenu()
		 * runs from wScreenRestoreState(): with no readable WMRootMenu
		 * that path pops a modal wMessageDialog whose WMNextEvent loop
		 * would run the rest of the session with StartUp() never
		 * having returned, so wXSMPConnect(), execInitScript() and
		 * inotifyWatchConfig() would never fire, manageAllWindows()
		 * clears the flag once the initial adopt is done */
		wScreen[j]->flags.startup = 1;

		wScreenRestoreState(wScreen[j]);

		/* manage all windows that were already here before us */
		if (!wPreferences.flags.nodock && wScreen[j]->dock)
			wScreen[j]->last_dock = wScreen[j]->dock;

		manageAllWindows(wScreen[j], wPreferences.flags.restarting == 2);

		/* restore saved menus */
		wMenuRestoreState(wScreen[j]);

		/* If we're not restarting, restore session */
		if (wPreferences.flags.restarting == 0 && !wPreferences.flags.norestore)
			wSessionRestoreState(wScreen[j]);

		if (!wPreferences.flags.noautolaunch) {
			/* auto-launch apps */
			if (!wPreferences.flags.nodock && wScreen[j]->dock) {
				wScreen[j]->last_dock = wScreen[j]->dock;
				wDockDoAutoLaunch(wScreen[j]->dock, 0);
			}
			/* auto-launch apps in clip */
			if (!wPreferences.flags.noclip) {
				int i;
				for (i = 0; i < wScreen[j]->workspace_count; i++) {
					if (wScreen[j]->workspaces[i]->clip) {
						wScreen[j]->last_dock = wScreen[j]->workspaces[i]->clip;
						wDockDoAutoLaunch(wScreen[j]->workspaces[i]->clip, i);
					}
				}
			}
			/* auto-launch apps in drawers */
			if (!wPreferences.flags.nodrawer) {
				WDrawerChain *dc;
				for (dc = wScreen[j]->drawers; dc; dc = dc->next) {
					wScreen[j]->last_dock = dc->adrawer;
					wDockDoAutoLaunch(dc->adrawer, 0);
				}
			}
		}

		/* go to workspace where we were before restart */
		if (w_global.config->per_head_workspaces) {
			/* head_workspace[] was already restored from
			 * HeadWorkspaces by wScreenRestoreState, just bring
			 * each head to its workspace and set the mirror. */
			int h;
			for (h = 0; h < wHeadCount(wScreen[j]); h++)
				wWorkspaceForceChangeOnHead(wScreen[j],
					wScreen[j]->head_workspace[h], h);
			wWorkspaceSetCurrentHead(wScreen[j],
				wScreen[j]->xine_info.primary_head);
		} else if (lastDesktop >= 0) {
			wWorkspaceForceChange(wScreen[j], lastDesktop);
		} else {
			wSessionRestoreLastWorkspace(wScreen[j]);
		}

		/* the corner InputOnly windows were created and raised by
		 * wConfigApplyRuntime() inside wScreenInit, but the dock,
		 * clip and adopted clients above are mapped with plain
		 * XMapWindow and end up on top of them, so a corner that
		 * happens to sit under the clip never receives EnterNotify,
		 * restack the corner windows once everything is up */
		wHotCornersRaise(wScreen[j]);
	}

	for (j = 0; j < w_global.screen_count; j++) {
		wKeyTreeRebuild(wScreen[j]);
	}

	if (w_global.screen_count == 0) {
		wfatal(_("could not manage any screen"));
		Exit(1);
	}

#ifndef HAVE_INOTIFY
	/* setup defaults file polling */
	if (!wPreferences.flags.noupdates)
		WMAddTimerHandler(3000, wDefaultsCheckDomains, NULL);
#endif

}

static Bool windowInList(Window window, Window * list, int count)
{
	for (; count >= 0; count--) {
		if (window == list[count])
			return True;
	}
	return False;
}

/*
 *-----------------------------------------------------------------------
 * manageAllWindows--
 * 	Manages all windows in the screen.
 *
 * Notes:
 * 	Called when the wm is being started.
 *	No events can be processed while the windows are being
 * reparented/managed.
 *-----------------------------------------------------------------------
 */
static Visual *visualForId(WScreen *scr, VisualID vid)
{
	XVisualInfo tmpl, *vi;
	int n;
	Visual *v;

	/* almost every client uses the screen's default visual, so try
	 * that first before walking the full visual list */
	if (scr->w_visual && XVisualIDFromVisual(scr->w_visual) == vid)
		return scr->w_visual;

	tmpl.visualid = vid;
	vi = XGetVisualInfo(dpy, VisualIDMask, &tmpl, &n);
	if (!vi || n < 1) {
		if (vi)
			XFree(vi);
		/* do not guess, a non-default (e.g. ARGB) visual paired
		 * with the prefetched depth/colormap would BadMatch on
		 * frame creation. Caller falls back to a fresh
		 * XGetWindowAttributes() instead. */
		return NULL;
	}
	v = vi[0].visual;
	XFree(vi);
	return v;
}

static void manageAllWindows(WScreen * scr, int crashRecovery)
{
	Window root, parent;
	Window *children;
	unsigned int nchildren;
	unsigned int i, j;
	WWindow *wwin;
	WXCookie *hints_ck, *attr_ck, *geom_ck;
	const xcb_setup_t *setup = xcb_get_setup(XGetXCBConnection(dpy));
	uint32_t own_base = setup->resource_id_base;
	uint32_t own_mask = setup->resource_id_mask;

	XGrabServer(dpy);
	XQueryTree(dpy, scr->root_win, &root, &parent, &children, &nchildren);

	scr->flags.startup = 1;

	/* Windows in this connection's XID range are wmaker's own helper
	 * windows (dock tiles, balloon, geometry view, hot-corner sentinels),
	 * all override-redirect and never candidates for management, so drop
	 * them in place before spending three requests apiece asking the
	 * server about them. */
	for (i = 0, j = 0; i < nchildren; i++) {
		if (((uint32_t)children[i] & ~own_mask) != own_base)
			children[j++] = children[i];
	}
	nchildren = j;

	/* Issue WM_HINTS, GetWindowAttributes and GetGeometry for every
	 * child up front so the icon-window and override-redirect filters,
	 * and the per-window XGetWindowAttributes that wManageWindow would
	 * otherwise do, all cost one round-trip instead of nchildren. */
	hints_ck = wmalloc(sizeof(WXCookie) * (nchildren ? nchildren : 1));
	attr_ck  = wmalloc(sizeof(WXCookie) * (nchildren ? nchildren : 1));
	geom_ck  = wmalloc(sizeof(WXCookie) * (nchildren ? nchildren : 1));
	for (i = 0; i < nchildren; i++) {
		hints_ck[i] = WXReqWMHints(dpy, children[i]);
		attr_ck[i]  = WXReqWindowAttributes(dpy, children[i]);
		geom_ck[i]  = WXReqGeometry(dpy, children[i]);
	}

	/* first remove all icon windows */
	for (i = 0; i < nchildren; i++) {
		WXWMHints *wmhints;

		wmhints = WXRcvWMHints(dpy, hints_ck[i]);
		if (wmhints && (wmhints->flags & IconWindowHint)) {
			for (j = 0; j < nchildren; j++) {
				if (children[j] == wmhints->icon_window) {
					wfree(wmhints);
					wmhints = NULL;
					children[j] = None;
					break;
				}
			}
		}
		if (wmhints) {
			wfree(wmhints);
		}
	}
	wfree(hints_ck);

	for (i = 0; i < nchildren; i++) {
		WXWindowAttributes xa;
		WXGeometry xg;
		XWindowAttributes wattribs;

		if (children[i] == None) {
			WXDiscard(dpy, attr_ck[i]);
			WXDiscard(dpy, geom_ck[i]);
			continue;
		}

		if (!WXRcvWindowAttributes(dpy, attr_ck[i], &xa)) {
			WXDiscard(dpy, geom_ck[i]);
			continue;
		}

		/* drop override-redirect and unviewable children here so the
		 * many wmaker-owned helper windows do not each cycle
		 * XGrabServer/XUngrabServer inside wManageWindow */
		if (xa.override_redirect || xa.map_state == IsUnviewable) {
			WXDiscard(dpy, geom_ck[i]);
			continue;
		}

		if (!WXRcvGeometry(dpy, geom_ck[i], &xg))
			continue;

		/* assemble what wManageWindow and wClientGetNormalHints read
		 * from XWindowAttributes */
		memset(&wattribs, 0, sizeof(wattribs));
		wattribs.override_redirect = xa.override_redirect;
		wattribs.map_state         = xa.map_state;
		wattribs.class             = xa.window_class;
		wattribs.colormap          = xa.colormap;
		wattribs.visual            = visualForId(scr, xa.visual);
		wattribs.root              = xg.root;
		wattribs.x                 = xg.x;
		wattribs.y                 = xg.y;
		wattribs.width             = xg.width;
		wattribs.height            = xg.height;
		wattribs.border_width      = xg.border_width;
		wattribs.depth             = xg.depth;

		/* if the visual could not be resolved do not hand a guessed
		 * one to frame creation, let wManageWindow fetch its own
		 * coherent XWindowAttributes instead */
		wwin = wManageWindow(scr, children[i],
				     wattribs.visual ? &wattribs : NULL);
		if (wwin) {
			/* apply states got from WSavedState */
			/* shaded + minimized is not restored correctly */
			if (wwin->flags.shaded) {
				wwin->flags.shaded = 0;
				wShadeWindow(wwin);
			}
			if (wwin->flags.miniaturized
			    && (wwin->transient_for == None
				|| wwin->transient_for == scr->root_win
				|| !windowInList(wwin->transient_for, children, nchildren))) {

				wwin->flags.skip_next_animation = 1;
				wwin->flags.miniaturized = 0;
				wIconifyWindow(wwin);
			} else {
				wClientSetState(wwin, NormalState, None);
			}
			if (crashRecovery) {
				int border;

				border = (!HAS_BORDER(wwin) ? 0 : wFrameBorderWidth(wwin));

				wWindowMove(wwin, wwin->frame_x - border,
					    wwin->frame_y - border -
					    (wwin->frame->titlebar ? wwin->frame->titlebar->height : 0));
			}
			/* tab-group membership from _WINDOWMAKER_TABGROUP is
			 * applied after the deferred shade and iconify so an
			 * iconified group is rebuilt from members that each
			 * already own a miniwindow */
			wGroupRestoreMember(wwin);
			/* auto-group by WM_CLASS when no session group was restored */
			if (!wwin->tab_group)
				wGroupAutoAttach(wwin);
		}
	}
	wfree(attr_ck);
	wfree(geom_ck);
	XUngrabServer(dpy);

	/* hide apps */
	wwin = scr->focused_window;
	while (wwin) {
		if (wwin->flags.hidden) {
			WApplication *wapp = wApplicationOf(wwin->main_window);

			if (wapp) {
				wwin->flags.hidden = 0;
				wHideApplication(wapp);
			} else {
				wwin->flags.hidden = 0;
			}
		}
		wwin = wwin->prev;
	}

	XFree(children);
	scr->flags.startup = 0;
	scr->flags.startup2 = 1;

	while (WXEvqPending(dpy)) {
		XEvent ev;
		WMNextEvent(dpy, &ev);
		WMHandleEvent(&ev);
	}
	scr->last_workspace = 0;
	if (w_global.config->per_head_workspaces) {
		wWorkspaceReconcileHeads(scr);
		wWorkspaceForceChangeOnHead(scr,
			scr->head_workspace[scr->xine_info.primary_head],
			scr->xine_info.primary_head);
	} else {
		wWorkspaceForceChange(scr, 0);
	}
	if (!wPreferences.flags.noclip)
		wDockShowIcons(scr->workspaces[scr->current_workspace]->clip);
	scr->flags.startup2 = 0;
}
