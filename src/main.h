/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 1998-2003 Dan Pascu
 *  Copyright (c) 2013-2026 Window Maker Team
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

#ifndef WMMAIN_H_
#define WMMAIN_H_

#include "config.h"

#ifdef HAVE_STDNORETURN
#include <stdnoreturn.h>
#endif

struct WPendingLaunch;

/* wspawnvp_setup() pre_exec context for launches that may carry a
 * startup-notification sequence, the dock and the root menu share the
 * same setup callback so the SetupEnvironment() and setsid() body is
 * defined once in main.c */
struct LaunchSetup {
    WScreen *scr;
    struct WPendingLaunch *sn;
};

noreturn void Exit(int status);
void Restart(char *manager, bool abortOnFailure);
void SetupEnvironment(WScreen *scr);
void LaunchChildSetup(void *data);
void ExecuteShellCommand(WScreen *scr, const char *command);
void ExecuteShellCommandWithStartup(WScreen *scr, const char *command, bool startup_notify);
bool RelaunchWindow(WWindow *wwin);
noreturn void wAbort(int dumpCore);
void ExecExitScript(void);
int getWVisualID(int screen);

#endif
