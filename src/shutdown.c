/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
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

#include <stdlib.h>
#include <signal.h>
#include <unistd.h>


#include "WindowMaker.h"
#include "window.h"
#include "client.h"
#include "main.h"
#include "properties.h"
#include "session.h"
#include "winspector.h"
#include "wmspec.h"
#include "colormap.h"
#include "event.h"
#include "screen.h"
#include "shutdown.h"
#include "cursor_find.h"
#include "xsettings.h"
#include "ipc.h"
#include "xsmp.h"
#include "wsound.h"


static void wipeDesktop(WScreen * scr);
static void restoreWindows(WScreen * scr);

/*
 *----------------------------------------------------------------------
 * Shutdown-
 * 	Exits the window manager cleanly. If mode is WSLogoutMode,
 * the whole X session will be closed, by killing all clients if
 * no session manager is running or by asking a shutdown to
 * it if its present.
 *
 * external is True when the exit was triggered from outside the
 * user interface (SIGTERM/SIGINT/SIGHUP, or another WM taking
 * the WM_Sn selection). On a system reboot or DM logout the
 * clients receive SIGTERM at the same time we do, so by the
 * time we run here the managed-window list is partial or empty,
 * re-snapshotting it into WMState would silently overwrite a
 * previously saved session with Applications = (). Pass external
 * through so wScreenSaveState() carries the existing
 * Applications/Workspace forward instead of regenerating them.
 *----------------------------------------------------------------------
 */
void Shutdown(WShutdownMode mode, bool external)
{
	int i;

	/* a deliberate exit or restart must not look like a crash to the
	 * session manager, drop the restart hint and close the ICE
	 * connection before tearing down screens */
	wXSMPDisconnect();

	wFindMouseShutdown();

	switch (mode) {
	case WSLogoutMode:
	case WSKillMode:
	case WSExitMode:
		/* if there is no session manager, send SAVE_YOURSELF to
		 * the clients */
#ifdef HAVE_INOTIFY
		wInotifyCleanup();
#endif
		for (i = 0; i < wdisp->screen_count; i++) {
			WScreen *scr;

			scr = wScreenWithNumber(i);
			if (scr) {
				wSoundPlay(scr, WSOUND_SHUTDOWN);
				wIPCServerStop(scr, "exit");

				if (scr->helper_pid)
					kill(scr->helper_pid, SIGKILL);

				wScreenSaveState(scr, external);

				if (mode == WSKillMode)
					wipeDesktop(scr);
				else
					RestoreDesktop(scr);
				wScreenDestroy(scr);
			}
		}
		ExecExitScript();
		Exit(0);
		break;

	case WSRestartPreparationMode:
		for (i = 0; i < wdisp->screen_count; i++) {
			WScreen *scr;

#ifdef HAVE_INOTIFY
			wInotifyCleanup();
#endif
			scr = wScreenWithNumber(i);
			if (scr) {
				wSoundPlay(scr, WSOUND_SHUTDOWN);
				wIPCServerStop(scr, "restart");

				if (scr->helper_pid)
					kill(scr->helper_pid, SIGKILL);
				/* restart re-execs wmaker only, clients stay
				 * mapped, so the window list is trustworthy */
				wScreenSaveState(scr, false);
				RestoreDesktop(scr);
				wScreenDestroy(scr);
			}
		}
		break;
	}
}

/*
 * Unmanaging a window frees its frame core and its miniwindow, and both
 * are linked in the stacking chains being walked here, so no pointer
 * into the list may be held across an unmanage. Walk the list once to
 * collect the client window ids in visit order, then unmanage from the
 * collected ids, re-resolving each window in case an earlier unmanage
 * took a peer with it.
 */
static void restoreWindows(WScreen * scr)
{
	WCoreWindow *core;
	WMBagIterator iter;
	WWindow *wwin;
	xcb_window_t *windows;
	int count, i, size;

	size = scr->window_count > 0 ? scr->window_count : 1;
	windows = wmalloc(sizeof(xcb_window_t) * (size_t) size);
	count = 0;

	for (core = WMBagLast(scr->stacking_list, &iter); core != NULL;
	     core = WMBagPrevious(scr->stacking_list, &iter)) {
		/* go to the end of the list */
		while (core->stacking->under)
			core = core->stacking->under;

		for (; core != NULL; core = core->stacking->above) {
			if (core->descriptor.parent_type != WCLASS_WINDOW)
				continue;

			wwin = core->descriptor.parent;
			if (count == size) {
				size *= 2;
				windows = wrealloc(windows, sizeof(xcb_window_t) * (size_t) size);
			}
			windows[count++] = wwin->client_win;
		}
	}

	for (i = 0; i < count; i++) {
		wwin = wWindowFor(windows[i]);
		if (!wwin)
			continue;

		wUnmanageWindow(wwin, !wwin->flags.internal_window, false);
		xcb_map_window(wdisp->conn, windows[i]);
	}

	wfree(windows);
}

/*
 *----------------------------------------------------------------------
 * RestoreDesktop--
 * 	Puts the desktop in a usable state when exiting.
 *
 * Side effects:
 * 	All frame windows are removed and windows are reparented
 * back to root. Windows that are outside the screen are
 * brought to a viable place.
 *
 *----------------------------------------------------------------------
 */
void RestoreDesktop(WScreen * scr)
{
	if (scr->helper_pid > 0) {
		kill(scr->helper_pid, SIGTERM);
		scr->helper_pid = 0;
	}

	wXGrabServer(wdisp->conn);
	wDestroyInspectorPanels();

	/* reparent windows back to the root window, keeping the stacking order */
	restoreWindows(scr);

	wXUngrabServer(wdisp->conn);
	xcb_set_input_focus(wdisp->conn, XCB_INPUT_FOCUS_PARENT, XCB_INPUT_FOCUS_POINTER_ROOT, XCB_CURRENT_TIME);
	wColormapInstallForWindow(scr, NULL);
	PropCleanUp(scr->root_win);
	wNETWMCleanup(scr);
	wXSettingsShutdown(scr);
	wXSync(wdisp->conn);
}

/*
 *----------------------------------------------------------------------
 * wipeDesktop--
 * 	Kills every client on a screen. Managed windows get
 * WM_DELETE_WINDOW where they advertised it, then a root wXQueryTree
 * sweep xcb_kill_client()s every top-level that is not one of our own
 * so override-redirect and withdrawn clients do not survive the
 * session kill.
 *
 * Side effects:
 * 	All clients are closed.
 *
 *----------------------------------------------------------------------
 */
static void wipeDesktop(WScreen * scr)
{
	WWindow *wwin;
	xcb_window_t root, parent;
	xcb_window_t *children;
	unsigned int nchildren, i;
	const xcb_setup_t *setup = xcb_get_setup(wdisp->conn);
	uint32_t own_base = setup->resource_id_base;
	uint32_t own_mask = setup->resource_id_mask;

	/* Managed clients first: they are reparented under our frame
	 * windows so the root-children sweep below will not see them,
	 * and walking the WWindow list lets us honour WM_DELETE_WINDOW
	 * where the client asked for it. */
	wwin = scr->focused_window;
	while (wwin) {
		if (wwin->protocols.DELETE_WINDOW)
			wClientSendProtocol(wwin, wdisp->atom.wm.delete_window,
									  wdisp->timestamp.last_event);
		else
			wClientKill(wwin);
		wwin = wwin->prev;
	}

	/* Sweep every remaining top-level that is not in this
	 * connection's XID range (frames, dock tiles, menus, OSD).
	 * Override-redirect panels, withdrawn clients and anything
	 * else that never had a WWindow would otherwise outlive the
	 * session kill. */
	if (wXQueryTree(wdisp->conn, scr->root_win, &root, &parent, &children, &nchildren)) {
		for (i = 0; i < nchildren; i++) {
			if (((uint32_t)children[i] & ~own_mask) != own_base)
				xcb_kill_client(wdisp->conn, children[i]);
		}
		if (children)
			wfree(children);
	}

	wXSync(wdisp->conn);
}
