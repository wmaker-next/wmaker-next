/* monitor.c - monitors the wmaker process
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2004 Alfredo K. Kojima
 *  Copyright (c) 2009-2026 Window Maker Team
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
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <ctype.h>
#include <sys/wait.h>
#ifdef __FreeBSD__
#include <sys/signal.h>
#endif

#include <WINGs/WUtil.h>

#include "WindowMaker.h"
#include "screen.h"
#include "window.h"
#include "dialog.h"
#include "main.h"


static WMArray *fallback_wms;

static bool isBlankString(const char *s)
{
	while (*s) {
		if (!isspace((unsigned char)*s))
			return false;
		s++;
	}
	return true;
}

static void createFallbackWMs(void)
{
	char *alt;

	fallback_wms = WMCreateArray(8);

	alt = getenv("WINDOWMAKER_ALT_WM");
	if (alt != NULL && !isBlankString(alt))
		WMAddToArray(fallback_wms, wstrdup(alt));

	WMAddToArray(fallback_wms, wstrdup("blackbox"));
	WMAddToArray(fallback_wms, wstrdup("metacity"));
	WMAddToArray(fallback_wms, wstrdup("fvwm"));
	WMAddToArray(fallback_wms, wstrdup("twm"));
	WMAddToArray(fallback_wms, wstrdup("rxvt"));
	WMAddToArray(fallback_wms, wstrdup("xterm"));
}

static int showCrashDialog(int sig)
{
	xcb_connection_t *conn;
	int crashAction;
	int scrnum;

	conn = xcb_connect(NULL, &scrnum);
	if (!xcb_connection_has_error(conn)) {
		wXGrabServer(conn);
		crashAction = wShowCrashingDialogPanel(sig, conn, scrnum);
		xcb_disconnect(conn);
	} else {
		xcb_disconnect(conn);
		werror(_("cannot open connection for crashing dialog panel. Aborting."));
		crashAction = WMAbort;
	}

	if (crashAction == WMStartAlternate) {
		int i;

		wmessage(_("trying to start alternate window manager..."));

		for (i = 0; i < WMGetArrayItemCount(fallback_wms); i++) {
			Restart(WMGetFromArray(fallback_wms, i), false);
		}

		wfatal(_("failed to start alternate window manager. Aborting."));

		return 0;
	} else if (crashAction == WMAbort)
		return 0;
	else
		return 1;
}

int MonitorLoop(int argc, char **argv)
{
	pid_t pid, exited;
	char **child_argv = wmalloc(sizeof(char *) * (argc + 2));
	int i, status;
	time_t last_start;
	bool error = false;

	createFallbackWMs();

	for (i = 0; i < argc; i++)
		child_argv[i] = argv[i];
	child_argv[i++] = "--for-real";
	child_argv[i] = NULL;

	for (;;) {
		last_start = time(NULL);

		/* Start Window Maker */
		pid = wspawnvp(child_argv);
		if (pid < 0) {
			werror(_("Error respawning Window Maker"));
			exit(1);
		}

		do {
			exited = waitpid(-1, &status, 0);
			if (exited < 0) {
				werror(_("Error during monitoring of Window Maker process."));
				error = true;
				break;
			}
		} while (exited != pid);

		if (error)
			break;

		child_argv[argc] = "--for-real-";

		/* Check if the wmaker process exited due to a crash */
		if (WIFSIGNALED(status) &&
		    (WTERMSIG(status) == SIGSEGV ||
		     WTERMSIG(status) == SIGBUS ||
		     WTERMSIG(status) == SIGILL || WTERMSIG(status) == SIGABRT || WTERMSIG(status) == SIGFPE)) {
			/* If so, we check when was the last restart.
			 * If it was less than 3s ago, it's a bad sign, so we show
			 * the crash panel and ask the user what to do */
			if (time(NULL) - last_start < 3) {
				if (showCrashDialog(WTERMSIG(status)) == 0) {
					wfree(child_argv);
					return 1;
				}
			}
			wwarning(_("Window Maker exited due to a crash (signal %i) and will be restarted."),
				 WTERMSIG(status));
		} else
			break;
	}
	wfree(child_argv);
	return 0;
}
