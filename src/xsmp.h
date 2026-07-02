/*  xsmp.h - X Session Management Protocol client
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

#ifndef WMXSMP_H_
#define WMXSMP_H_

#include <X11/Xlib.h>

/* Return a wmalloc()'d NULL-terminated copy of argv with any --for-real*
 * flag and any -clientid/--clientid pair removed, and write the surviving
 * count to *out_argc. Always compiled so check_xsmp can cover it on a
 * build without libSM. */
char **wXSMPFilterArgv(int argc, char **argv, int *out_argc);

#ifdef USE_XSMP

/* Open the SmcConnection to $SESSION_MANAGER, register the ICE fd on the
 * WINGs event loop, and publish SmRestartCommand/SmCloneCommand built from
 * argv (with any --for-real* / -clientid pair filtered out) plus the id
 * assigned by the session manager. previous_id is the value received via
 * -clientid on the command line, or NULL on a cold start. No-op when
 * $SESSION_MANAGER is unset. */
void wXSMPConnect(int argc, char **argv, const char *previous_id);

/* True when a session manager connection is open, callers use this to
 * decide whether to hand shutdown to the session manager or run the
 * local wipeDesktop()/Shutdown() path. */
Bool wXSMPConnected(void);

/* Ask the session manager for a global SaveYourself with shutdown=True.
 * save_state maps to SmSaveBoth vs SmSaveGlobal so the exit-dialog
 * "Save workspace state" checkbox controls whether clients are asked
 * to checkpoint. Returns False when no connection is open so the caller
 * falls back to the direct Shutdown() path. */
Bool wXSMPRequestShutdown(Bool save_state);

/* Called from Shutdown() before exit so the session manager sees a
 * deliberate close as SmRestartIfRunning rather than a crash to
 * relaunch, then closes the ICE connection. */
void wXSMPDisconnect(void);

#else  /* !USE_XSMP */

static inline void wXSMPConnect(int argc, char **argv, const char *previous_id)
{
	(void)argc; (void)argv; (void)previous_id;
}
static inline Bool wXSMPConnected(void) { return False; }
static inline Bool wXSMPRequestShutdown(Bool save_state) { (void)save_state; return False; }
static inline void wXSMPDisconnect(void) { }

#endif  /* USE_XSMP */

#endif  /* WMXSMP_H_ */
