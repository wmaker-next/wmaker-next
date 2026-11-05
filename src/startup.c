/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 1998-2003 Dan Pascu
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

#include <WINGs/WXHints.h>
#ifdef USE_XSHAPE
#include <xcb/shape.h>
#endif
#ifdef USE_RANDR
#include <xcb/randr.h>
#endif
#ifdef USE_XSYNC
#include <xcb/sync.h>
#endif
#ifdef USE_XCB_SCREENSAVER
#include <xcb/screensaver.h>
#endif
#include <xcb/xfixes.h>
#ifdef USE_XCB_XINPUT
#include <xcb/xinput.h>
#endif

#include <xcb/xproto.h>

#include "WindowMaker.h"
#include "GNUstep.h"
#include "texture.h"
#include "screen.h"
#include "window.h"
#include "misc.h"
#include "actions.h"
#include "client.h"
#include "main.h"
#include "startup.h"
#include "wdisplay.h"
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
#include "cursor_hide.h"
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
static void manageAllWindows(WScreen *scr, int crashed, int last_desktop);
static void restoreStartupFocus(WScreen *scr);

/* primary error path, in-stream errors from xcb-issued reply-less
 * requests reach the WINGs event queue as response_type 0 and land
 * here via WXEvqSetErrorHook(), FormatXError() takes the unpacked
 * fields so the handler needs no synthetic error event */
static void reportXError(unsigned int error_code, unsigned int major_opcode,
			 unsigned int minor_opcode, unsigned long resource_id,
			 unsigned long serial)
{
	char buffer[MAXLINE];

	/* ignore the benign races, a managed client can vanish between
	 * any two requests so BadWindow on a client-owned XID is normal */
	/* core error / request opcode codes (xproto): 3 BadWindow,
	 * 8 BadMatch, 9 BadDrawable, 14 GetGeometry, 42 SetInputFocus,
	 * 81 InstallColormap */
	if (resource_id != XCB_NONE
	    && ((error_code == 9 && major_opcode == 14)
		|| (error_code == 8 && major_opcode == 42)
		|| error_code == 3
		|| major_opcode == 81)) {
		return;
	}
	FormatXError(error_code, major_opcode, minor_opcode,
		     resource_id, serial, buffer, MAXLINE);
	wwarning(_("internal X error: %s"), buffer);
}

static void wHandleXcbError(xcb_connection_t *d, unsigned int error_code,
			    unsigned int major_opcode, unsigned int minor_opcode,
			    unsigned long resource_id, unsigned long serial)
{
	/* the in-stream xcb error path, the display argument carries no
	 * state the report needs */
	(void) d;

	reportXError(error_code, major_opcode, minor_opcode, resource_id,
		     serial);
}

/* the xcb-queue equivalent of the old Xlib IO-error handler: the
 * WINGs drain noticed xcb_connection_has_error() on the connection */
static void lostXConnection(xcb_connection_t *dconn)
{
	(void) dconn;

	wdisp->conn = NULL;
	wDisplayDestroy();
	Exit(0);
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

/* Queue a transparent restart and wake the event loop so the state
 * flag is acted on even when the connection is otherwise idle.
 */
void wRequestRestart(void)
{
	WCHANGE_STATE(WSTATE_NEED_RESTART);
	if (sig_pipe[1] >= 0) {
		ssize_t r = write(sig_pipe[1], "x", 1);
		(void) r;
	}
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
	assert(i < wdisp->screen_count);

	return wdisp->screens[i];
}

WScreen *wScreenForRootWindow(xcb_window_t window)
{
	int i;

	if (wXScreenCount(wdisp->conn) == 1)
		return wdisp->screens[0];

	/* Since the number of heads will probably be small (normally 2),
	 * it should be faster to use this than a hash table, because
	 * of the overhead. */
	for (i = 0; i < wdisp->screen_count; i++)
		if (wdisp->screens[i]->root_win == window)
			return wdisp->screens[i];

	return wScreenForWindow(window);
}

WScreen *wScreenForWindow(xcb_window_t window)
{
	WXWindowAttributes attr;
	int i;

	if (wXScreenCount(wdisp->conn) == 1)
		return wdisp->screens[0];

	for (i = 0; i < wdisp->screen_count; i++)
		if (wdisp->screens[i]->root_win == window)
			return wdisp->screens[i];

	if (!wXGetWindowAttributes(wdisp->conn, window, &attr, NULL))
		return NULL;
	if (attr.root == window)
		return NULL;
	return wScreenForRootWindow(attr.root);
}

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
void StartUp(bool defaultScreenOnly)
{
	static const char empty_bits[(16 * 16) / 8];
	struct sigaction sig_action;
	int i, j, max, nscreens;
	int haveScreenZero;
	int *desktopHint;
	char **formats;
	xcb_pixmap_t cur;
	WXColor black;
	xcb_connection_t *xc = wdisp->conn;
	const xcb_query_extension_reply_t *xer;
	xcb_xfixes_query_version_reply_t *xfixrep;
#ifdef USE_XCB_XINPUT
	xcb_input_xi_query_version_reply_t *xirep;
#endif
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

	WXStaticAtomsInit(wdisp->conn);

	wdisp->atom.wm.state = wx_static_atoms[W_ATOM_WM_STATE];
	wdisp->atom.wm.change_state = wx_static_atoms[W_ATOM_WM_CHANGE_STATE];
	wdisp->atom.wm.protocols = wx_static_atoms[W_ATOM_WM_PROTOCOLS];
	wdisp->atom.wm.take_focus = wx_static_atoms[W_ATOM_WM_TAKE_FOCUS];
	wdisp->atom.wm.delete_window = wx_static_atoms[W_ATOM_WM_DELETE_WINDOW];
	wdisp->atom.wm.save_yourself = wx_static_atoms[W_ATOM_WM_SAVE_YOURSELF];
	wdisp->atom.wm.client_leader = wx_static_atoms[W_ATOM_WM_CLIENT_LEADER];
	wdisp->atom.wm.colormap_windows = wx_static_atoms[W_ATOM_WM_COLORMAP_WINDOWS];
	wdisp->atom.wm.colormap_notify = wx_static_atoms[W_ATOM_WM_COLORMAP_NOTIFY];
	wdisp->atom.wm.motif_wm_hints = wx_static_atoms[W_ATOM_MOTIF_WM_HINTS];
	wdisp->atom.wm.ignore_focus_events = wx_static_atoms[W_ATOM_WM_IGNORE_FOCUS_EVENTS];
	wdisp->atom.wm.window_role = wx_static_atoms[W_ATOM_WM_WINDOW_ROLE];
	wdisp->atom.wmaker.menu = wx_static_atoms[W_ATOM_WINDOWMAKER_MENU];
	wdisp->atom.wmaker.state = wx_static_atoms[W_ATOM_WINDOWMAKER_STATE];
	wdisp->atom.wmaker.wm_protocols = wx_static_atoms[W_ATOM_WINDOWMAKER_WM_PROTOCOLS];
	wdisp->atom.wmaker.wm_function = wx_static_atoms[W_ATOM_WINDOWMAKER_WM_FUNCTION];
	wdisp->atom.wmaker.noticeboard = wx_static_atoms[W_ATOM_WINDOWMAKER_NOTICEBOARD];
	wdisp->atom.wmaker.command = wx_static_atoms[W_ATOM_WINDOWMAKER_COMMAND];
	wdisp->atom.wmaker.icon_size = wx_static_atoms[W_ATOM_WINDOWMAKER_ICON_SIZE];
	wdisp->atom.wmaker.icon_tile = wx_static_atoms[W_ATOM_WINDOWMAKER_ICON_TILE];
	wdisp->atom.wmaker.mark_key = wx_static_atoms[W_ATOM_WINDOWMAKER_MARK_KEY];
	wdisp->atom.wmaker.tab_group = wx_static_atoms[W_ATOM_WINDOWMAKER_TABGROUP];
	wdisp->atom.gnustep.wm_attr = wx_static_atoms[W_ATOM_GNUSTEP_WM_ATTR];
	wdisp->atom.gnustep.wm_miniaturize_window = wx_static_atoms[W_ATOM_GNUSTEP_WM_MINIATURIZE_WINDOW];
	wdisp->atom.gnustep.wm_hide_app = wx_static_atoms[W_ATOM_GNUSTEP_WM_HIDE_APP];
	wdisp->atom.gnustep.titlebar_state = wx_static_atoms[W_ATOM_GNUSTEP_TITLEBAR_STATE];
	wdisp->atom.desktop.gtk_object_path = wx_static_atoms[W_ATOM_GTK_APPLICATION_OBJECT_PATH];

#ifdef USE_DOCK_XDND
	wXDNDInitializeAtoms();
#endif

	/* cursors */
	wdisp->cursor[WCUR_NORMAL] = XCB_NONE;	/* inherit from root */
	wdisp->cursor[WCUR_ROOT] = wXLoadCursor(wdisp->conn, 0, "left_ptr");
	wdisp->cursor[WCUR_ARROW] = wXLoadCursor(wdisp->conn, 0, "top_left_arrow");
	wdisp->cursor[WCUR_MOVE] = wXLoadCursor(wdisp->conn, 0, "fleur");
	wdisp->cursor[WCUR_RESIZE] = wXLoadCursor(wdisp->conn, 0, "sizing");
	wdisp->cursor[WCUR_TOPLEFTRESIZE] = wXLoadCursor(wdisp->conn, 0, "top_left_corner");
	wdisp->cursor[WCUR_TOPRIGHTRESIZE] = wXLoadCursor(wdisp->conn, 0, "top_right_corner");
	wdisp->cursor[WCUR_BOTTOMLEFTRESIZE] = wXLoadCursor(wdisp->conn, 0, "bottom_left_corner");
	wdisp->cursor[WCUR_BOTTOMRIGHTRESIZE] = wXLoadCursor(wdisp->conn, 0, "bottom_right_corner");
	wdisp->cursor[WCUR_VERTICALRESIZE] = wXLoadCursor(wdisp->conn, 0, "sb_v_double_arrow");
	wdisp->cursor[WCUR_HORIZONRESIZE] = wXLoadCursor(wdisp->conn, 0, "sb_h_double_arrow");
	wdisp->cursor[WCUR_WAIT] = wXLoadCursor(wdisp->conn, 0, "watch");
	wdisp->cursor[WCUR_QUESTION] = wXLoadCursor(wdisp->conn, 0, "question_arrow");
	wdisp->cursor[WCUR_TEXT] = wXLoadCursor(wdisp->conn, 0, "xterm");
	wdisp->cursor[WCUR_SELECT] = wXLoadCursor(wdisp->conn, 0, "cross");
	wdisp->cursor[WCUR_CAPTURE] = wXLoadCursor(wdisp->conn, 0, "crosshair");

	memset(&black, 0, sizeof(black));
	cur = wXCreateBitmapFromData(wdisp->conn, wXRootWindow(wdisp->conn, wdisp->default_screen), empty_bits, 16, 16);
	wdisp->cursor[WCUR_EMPTY] = wXCreatePixmapCursor(wdisp->conn, cur, cur, &black, &black, 0, 0);
	xcb_free_pixmap(wdisp->conn, cur);

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

	/* a lost X connection dies through the WINGs drain's dead hook */
	WXEvqSetDeadHook(lostXConnection);

	/* set hook for out event dispatcher in WINGs event dispatcher */
	WMHookEventHandler(DispatchEvent);

	/* initialize defaults stuff */
	w_global.domain.wmaker = wDefaultsInitDomain("WindowMaker", true);
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
	w_global.domain.root_menu = wDefaultsInitDomain("WMRootMenu", false);
	if (!w_global.domain.root_menu->dictionary)
		wwarning(_("could not read domain \"%s\" from defaults database"), "WMRootMenu");

	wDefaultsMergeGlobalMenus(w_global.domain.root_menu);

	/* WMWindowAttributes outer keys are X11 instance.class strings and
	 * are case-sensitive ("xterm" != "XTerm"), keep them distinct while
	 * the user and global domain files are read and merged */
	WMPLSetCaseSensitive(true);
	w_global.domain.window_attr = wDefaultsInitDomain("WMWindowAttributes", true);
	WMPLSetCaseSensitive(false);
	if (!w_global.domain.window_attr->dictionary)
		wwarning(_("could not read domain \"%s\" from defaults database"), "WMWindowAttributes");
	wWindowAttrsReload();

	/* WMGLOBAL holds WINGs-global options (AntialiasedText, SystemFont, ...);
	 * the WM only tracks it for live-reload change detection, WINGs reads
	 * the values itself via WMGetStandardUserDefaults/WMReloadConfiguration. */
	w_global.domain.wmglobal = wDefaultsInitDomain("WMGLOBAL", false);

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
	xcb_prefetch_extension_data(xc, &xcb_xfixes_id);
#ifdef USE_XCB_XINPUT
	xcb_prefetch_extension_data(xc, &xcb_input_id);
#endif

#ifdef USE_XSHAPE
	xer = xcb_get_extension_data(xc, &xcb_shape_id);
	wdisp->xext.shape.supported = xer && xer->present;
	if (wdisp->xext.shape.supported) {
		xcb_shape_query_version_reply_t *shr;

		wdisp->xext.shape.event_base = xer->first_event;
		shr = xcb_shape_query_version_reply(xc,
			xcb_shape_query_version(xc), NULL);
		if (shr) {
			wdisp->xext.shape.major_version = shr->major_version;
			wdisp->xext.shape.minor_version = shr->minor_version;
		}
		free(shr);
	}
#endif

#ifdef USE_XSYNC
	xer = xcb_get_extension_data(xc, &xcb_sync_id);
	wdisp->xext.xsync.supported = 0;
	if (xer && xer->present) {
		wdisp->xext.xsync.event_base = xer->first_event;
		syncr = xcb_sync_initialize_reply(xc,
				xcb_sync_initialize(xc, XCB_SYNC_MAJOR_VERSION,
						    XCB_SYNC_MINOR_VERSION), NULL);
		wdisp->xext.xsync.supported = syncr != NULL;
		free(syncr);
	}
#endif

#ifdef USE_RANDR
	xer = xcb_get_extension_data(xc, &xcb_randr_id);
	if (xer && xer->present) {
		wdisp->xext.randr.event_base = xer->first_event;
		rrr = xcb_randr_query_version_reply(xc,
				xcb_randr_query_version(xc, XCB_RANDR_MAJOR_VERSION,
							XCB_RANDR_MINOR_VERSION), NULL);
		if (rrr) {
			rr_major = rrr->major_version;
			rr_minor = rrr->minor_version;
		}
	}
	if (rrr && (rr_major > 1 || (rr_major == 1 && rr_minor >= 5))) {
		wdisp->xext.randr.supported = 1;
	} else {
		wdisp->xext.randr.supported = 0;
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
	wdisp->xext.screensaver.supported = xer && xer->present;
	if (wdisp->xext.screensaver.supported)
		wdisp->xext.screensaver.event_base = xer->first_event;
#endif

	xer = xcb_get_extension_data(xc, &xcb_xfixes_id);
	wdisp->xext.xfixes.supported = false;
	if (xer && xer->present) {
		xfixrep = xcb_xfixes_query_version_reply(xc,
			xcb_xfixes_query_version(xc, 4, 0), NULL);
		if (xfixrep) {
			wdisp->xext.xfixes.supported =
				xfixrep->major_version >= 4;
			free(xfixrep);
		}
	}

#ifdef USE_XCB_XINPUT
	xer = xcb_get_extension_data(xc, &xcb_input_id);
	wdisp->xext.xinput.supported = false;
	if (xer && xer->present) {
		wdisp->xext.xinput.major_opcode = xer->major_opcode;
		xirep = xcb_input_xi_query_version_reply(xc,
			xcb_input_xi_query_version(xc, 2, 0), NULL);
		if (xirep) {
			wdisp->xext.xinput.supported =
				xirep->major_version >= 2;
			free(xirep);
		}
	}
#endif

	/* wXkbInit() negotiates XkbUseExtension, loads the keymap and state,
	 * selects per-device events, derives the named-modifier and lock
	 * masks, and writes wdisp->xext.xkb and shortcut.modifiers_mask.
	 * It runs before wScreenInit so wConfigApplyModifierKey and the
	 * keybinding parser see the resolved Alt/Meta/Super masks, and
	 * before wWindowSetKeyGrabs so the lock-mask grab fan-out is
	 * correct on the first manage. */
	wXkbInit();
#ifdef KEEP_XKB_LOCK_STATUS
	if (w_global.config->modelock && !wdisp->xext.xkb.supported) {
		wwarning(_("XKB is not supported. KbdModeLock is automatically disabled."));
		w_global.config->modelock = 0;
	}
#endif

	/* extension probes have filled wdisp->xext.*.event_base, so the
	 * per-protocol handler tables can register their slots now,
	 * DispatchEvent() was hooked above as the WINGs extra event
	 * handler so events on non-view windows reach the table */
	wEventInitDispatch();
	wCursorHideInit();

	if (defaultScreenOnly)
		max = 1;
	else
		max = wXScreenCount(wdisp->conn);

	wdisp->screens = wmalloc(sizeof(WScreen *) * max);

	wdisp->screen_count = 0;

	/* Check if TIFF images are supported */
	formats = RSupportedFileFormats();
	if (formats) {
		for (i = 0; formats[i] != NULL; i++) {
			if (strcmp(formats[i], "TIFF") == 0) {
				wdisp->supports_tiff = 1;
				break;
			}
		}
	}

	/* old_config is NULL at startup so every key registers as changed
	 * and the per-screen apply realises every resource */
	wConfigPrepareRuntimeDefaults(wmaker_dict, NULL, new_config, &changes);

	nscreens = wXScreenCount(wdisp->conn);
	desktopHint = wmalloc(sizeof(int) * (size_t)(nscreens > 0 ? nscreens : 1));
	for (j = 0; j < nscreens; j++)
		desktopHint[j] = -1;
	if (defaultScreenOnly || max == 1) {
		j = wdisp->default_screen;
		desktopHint[j] = wNETWMGetDesktopHint(wXRootWindow(wdisp->conn, j));
	} else {
		for (j = 0; j < max; j++)
			desktopHint[j] = wNETWMGetDesktopHint(wXRootWindow(wdisp->conn, j));
	}

	/* manage the screens */
	for (j = 0; j < max; j++) {
		if (defaultScreenOnly || max == 1) {
			wdisp->screens[wdisp->screen_count] = wScreenInit(wdisp->default_screen, new_config, &changes);
			if (!wdisp->screens[wdisp->screen_count]) {
				wfatal(_("it seems that there is already a window manager running"));
				Exit(1);
			}
		} else {
			wdisp->screens[wdisp->screen_count] = wScreenInit(j, new_config, &changes);
			if (!wdisp->screens[wdisp->screen_count]) {
				wwarning(_("could not manage screen %i"), j);
				continue;
			}
		}
		wdisp->screen_count++;
	}

	/* A secondary-only instance must still notice RESOURCE_MANAGER
	 * changes on screen 0's root, where xrdb may publish Xft.dpi.
	 * This changes only this client's event mask on that root, so a
	 * window manager running there is not affected. */
	haveScreenZero = 0;
	for (j = 0; j < wdisp->screen_count; j++)
		if (wdisp->screens[j]->screen == 0)
			haveScreenZero = 1;
	if (!haveScreenZero) {
		uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;

		xcb_change_window_attributes(wdisp->conn,
					     wXRootWindow(wdisp->conn, 0),
					     XCB_CW_EVENT_MASK, &mask);
	}

	/* bind the per-screen IPC sockets before session restore and dock
	 * auto-launch so spawned children inherit a live WMAKER_IPC_SOCKET */
	for (j = 0; j < wdisp->screen_count; j++)
		wIPCServerStart(wdisp->screens[j]);

	InitializeSwitchMenu();

	/* initialize/restore state for the screens */
	for (j = 0; j < wdisp->screen_count; j++) {
		int lastDesktop;

		lastDesktop = desktopHint[wdisp->screens[j]->screen];

		/* mark the screen as being brought up before OpenRootMenu()
		 * runs from wScreenRestoreState(): with no readable WMRootMenu
		 * that path pops a modal wMessageDialog whose WMNextEvent loop
		 * would run the rest of the session with StartUp() never
		 * having returned, so wXSMPConnect(), execInitScript() and
		 * inotifyWatchConfig() would never fire, manageAllWindows()
		 * clears the flag once the initial adopt is done */
		wdisp->screens[j]->flags.startup = 1;

		wScreenRestoreState(wdisp->screens[j]);

		/* manage all windows that were already here before us */
		if (!wdisp->flags.nodock && wdisp->screens[j]->dock)
			wdisp->screens[j]->last_dock = wdisp->screens[j]->dock;

		/* Pre-load saved states from WMState so manageAllWindows can
		 * match adopted windows against their saved state.
		 * wSessionRestoreState runs after manageAllWindows. */
		if (wdisp->flags.restarting == 0 && !wdisp->flags.norestore)
			wSessionQueueSavedState(wdisp->screens[j]);

		manageAllWindows(wdisp->screens[j], wdisp->flags.restarting == 2, lastDesktop);

		if (wdisp->flags.restarting == 0 && !wdisp->flags.norestore)
			wSessionCleanupUnmatchedSavedState();

		/* restore saved menus */
		wMenuRestoreState(wdisp->screens[j]);

		/* If we're not restarting, restore session */
		if (wdisp->flags.restarting == 0 && !wdisp->flags.norestore)
			wSessionRestoreState(wdisp->screens[j]);

		if (!wdisp->flags.noautolaunch) {
			/* auto-launch apps */
			if (!wdisp->flags.nodock && wdisp->screens[j]->dock) {
				wdisp->screens[j]->last_dock = wdisp->screens[j]->dock;
				wDockDoAutoLaunch(wdisp->screens[j]->dock, 0);
			}
			/* auto-launch apps in clip */
			if (!wdisp->flags.noclip) {
				int i;
				for (i = 0; i < wdisp->screens[j]->workspace_count; i++) {
					if (wdisp->screens[j]->workspaces[i]->clip) {
						wdisp->screens[j]->last_dock = wdisp->screens[j]->workspaces[i]->clip;
						wDockDoAutoLaunch(wdisp->screens[j]->workspaces[i]->clip, i);
					}
				}
			}
			/* auto-launch apps in drawers */
			if (!wdisp->flags.nodrawer) {
				WDrawerChain *dc;
				for (dc = wdisp->screens[j]->drawers; dc; dc = dc->next) {
					wdisp->screens[j]->last_dock = dc->adrawer;
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
			wdisp->screens[j]->flags.suppress_switch_focus =
				!!wdisp->flags.restarting;
			for (h = 0; h < wHeadCount(wdisp->screens[j]); h++)
				wWorkspaceForceChangeOnHead(wdisp->screens[j],
					wdisp->screens[j]->head_workspace[h], h);
			wdisp->screens[j]->flags.suppress_switch_focus = 0;
			wWorkspaceSetCurrentHead(wdisp->screens[j],
				wdisp->screens[j]->xine_info.primary_head);
			restoreStartupFocus(wdisp->screens[j]);
		} else if (lastDesktop >= 0) {
			wWorkspaceForceChange(wdisp->screens[j], lastDesktop);
		} else {
			wSessionRestoreLastWorkspace(wdisp->screens[j]);
		}

		/* the corner InputOnly windows were created and raised by
		 * wConfigApplyRuntime() inside wScreenInit, but the dock,
		 * clip and adopted clients above are mapped with plain
		 * XMapWindow and end up on top of them, so a corner that
		 * happens to sit under the clip never receives EnterNotify,
		 * restack the corner windows once everything is up */
		wHotCornersRaise(wdisp->screens[j]);
	}

	for (j = 0; j < wdisp->screen_count; j++)
		wKeyTreeRebuild(wdisp->screens[j]);

	if (wdisp->screen_count == 0) {
		wfatal(_("could not manage any screen"));
		Exit(1);
	}

#ifndef HAVE_INOTIFY
	/* setup defaults file polling */
	if (!wdisp->flags.noupdates)
		WMAddTimerHandler(3000, wDefaultsCheckDomains, NULL);
#endif

	wfree(desktopHint);
}

static bool windowInList(xcb_window_t window, xcb_window_t * list, int count)
{
	for (; count > 0; count--) {
		if (window == list[count - 1])
			return true;
	}
	return false;
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
static void restoreStartupFocus(WScreen *scr)
{
	xcb_window_t xid = scr->startup_focus_win;
	WWindow *fwin;

	scr->startup_focus_win = XCB_NONE;
	if (!wdisp->flags.restarting || xid == XCB_NONE)
		return;

	fwin = wWindowFor(xid);
	if (fwin && fwin->screen_ptr == scr
	    && !WFLAGP(fwin, no_focusable) && !fwin->flags.hidden
	    && !fwin->flags.miniaturized) {
		wSetFocusTo(scr, fwin);
		return;
	}

	for (fwin = scr->focused_window; fwin; fwin = fwin->prev)
		if (!fwin->flags.hidden && !fwin->flags.miniaturized
		    && !WFLAGP(fwin, no_focusable))
			break;
	if (fwin)
		wSetFocusTo(scr, fwin);
}

static void manageAllWindows(WScreen *scr, int crashRecovery, int last_desktop)
{
	xcb_window_t root, parent;
	xcb_window_t *children;
	unsigned int nchildren;
	unsigned int i, j;
	WWindow *wwin;
	WXCookie *hints_ck, *attr_ck, *geom_ck;
	const xcb_setup_t *setup = xcb_get_setup(wdisp->conn);
	uint32_t own_base = setup->resource_id_base;
	uint32_t own_mask = setup->resource_id_mask;

	wXGrabServer(wdisp->conn);
	wXQueryTree(wdisp->conn, scr->root_win, &root, &parent, &children, &nchildren);

	scr->flags.startup = 1;
	/* one client-list rebuild per burst instead of two per window */
	scr->flags.manage_burst = 1;

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
	 * and the per-window attribute fetch that wManageWindow would
	 * otherwise do, all cost one round-trip instead of nchildren. */
	hints_ck = wmalloc(sizeof(WXCookie) * (nchildren ? nchildren : 1));
	attr_ck  = wmalloc(sizeof(WXCookie) * (nchildren ? nchildren : 1));
	geom_ck  = wmalloc(sizeof(WXCookie) * (nchildren ? nchildren : 1));
	for (i = 0; i < nchildren; i++) {
		hints_ck[i] = WXReqWMHints(wdisp->conn, children[i]);
		attr_ck[i]  = WXReqWindowAttributes(wdisp->conn, children[i]);
		geom_ck[i]  = WXReqGeometry(wdisp->conn, children[i]);
	}

	/* first remove all icon windows */
	for (i = 0; i < nchildren; i++) {
		WXWMHints *wmhints;

		wmhints = WXRcvWMHints(wdisp->conn, hints_ck[i]);
		if (wmhints && (wmhints->flags & XCB_ICCCM_WM_HINT_ICON_WINDOW)) {
			for (j = 0; j < nchildren; j++) {
				if (children[j] == wmhints->icon_window) {
					wfree(wmhints);
					wmhints = NULL;
					children[j] = XCB_NONE;
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
		WXWindowAttrReply xa;
		WXGeometry xg;
		WXWindowAttributes wattribs;

		if (children[i] == XCB_NONE) {
			WXDiscard(wdisp->conn, attr_ck[i]);
			WXDiscard(wdisp->conn, geom_ck[i]);
			continue;
		}

		if (!WXRcvWindowAttributes(wdisp->conn, attr_ck[i], &xa)) {
			WXDiscard(wdisp->conn, geom_ck[i]);
			continue;
		}

		/* drop override-redirect and unviewable children here so the
		 * many wmaker-owned helper windows do not each cycle
		 * XGrabServer/XUngrabServer inside wManageWindow */
		if (xa.override_redirect || xa.map_state == XCB_MAP_STATE_UNVIEWABLE) {
			WXDiscard(wdisp->conn, geom_ck[i]);
			continue;
		}

		if (!WXRcvGeometry(wdisp->conn, geom_ck[i], &xg))
			continue;

		/* assemble what wManageWindow and wClientGetNormalHints read
		 * from WXWindowAttributes */
		memset(&wattribs, 0, sizeof(wattribs));
		wattribs.override_redirect = xa.override_redirect;
		wattribs.map_state         = xa.map_state;
		wattribs.class             = xa.window_class;
		wattribs.colormap          = xa.colormap;
		wattribs.visual            = wVisualForId(scr, xa.visual);
		wattribs.root              = xg.root;
		wattribs.x                 = xg.x;
		wattribs.y                 = xg.y;
		wattribs.width             = xg.width;
		wattribs.height            = xg.height;
		wattribs.border_width      = xg.border_width;
		wattribs.depth             = xg.depth;

		/* if the visual could not be resolved do not hand a guessed
		 * one to frame creation, let wManageWindow fetch its own
		 * coherent WXWindowAttributes instead */
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
			    && (wwin->transient_for == XCB_NONE
				|| wwin->transient_for == scr->root_win
				|| !windowInList(wwin->transient_for, children, nchildren))) {

				wwin->flags.skip_next_animation = 1;
				wwin->flags.miniaturized = 0;
				wwin->flags.restore_iconified = 1;
				wIconifyWindow(wwin);
			} else {
				wClientSetState(wwin, XCB_ICCCM_WM_STATE_NORMAL, XCB_NONE);
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
	wXUngrabServer(wdisp->conn);

	scr->flags.manage_burst = 0;
	wNETWMFlushClientLists(scr);

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

	wfree(children);
	scr->flags.startup = 0;
	scr->flags.startup2 = 1;

	while (WXEvqPending(wdisp->conn)) {
		xcb_generic_event_t *ev = NULL;
		WMNextEvent(wdisp->conn, &ev);
		if (!ev)
			break;
		WMHandleEvent(ev);
		free(ev);
	}
	scr->last_workspace = 0;

	if (!w_global.config->per_head_workspaces)
		restoreStartupFocus(scr);

	if (w_global.config->per_head_workspaces) {
		scr->flags.suppress_switch_focus = !!wdisp->flags.restarting;
		wWorkspaceReconcileHeads(scr);
		wWorkspaceForceChangeOnHead(scr,
			scr->head_workspace[scr->xine_info.primary_head],
			scr->xine_info.primary_head);
		scr->flags.suppress_switch_focus = 0;
	} else if (wdisp->flags.restarting && last_desktop >= 0) {
		wWorkspaceForceChange(scr, last_desktop);
	} else {
		wWorkspaceForceChange(scr, 0);
	}
	if (!wdisp->flags.noclip)
		wDockShowIcons(scr->workspaces[scr->current_workspace]->clip);
	scr->flags.startup2 = 0;
}
