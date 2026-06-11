/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2013 Window Maker Team
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

#ifndef WMEVENT_H
#define WMEVENT_H

#include "config.h"

#ifdef HAVE_STDNORETURN
#include <stdnoreturn.h>
#endif

typedef void (WDeathHandler)(pid_t pid, unsigned int status, void *cdata);

noreturn void EventLoop(void);
void DispatchEvent(XEvent *event);
void ProcessPendingEvents(void);
WMagicNumber wAddDeathHandler(pid_t pid, WDeathHandler *callback, void *cdata,
			      const char *source, int slot);
#ifdef HAVE_INOTIFY
void wInotifyCleanup(void);
#endif
Bool IsDoubleClick(WScreen *scr, XEvent *event);

/* Tear down any active key-chain state on scr: cancel the inactivity
 * timer, hide the prefix OSD, ungrab the keyboard, and clear curpos.
 * Safe to call when no chain is active. */
void wCancelKeyChain(WScreen *scr);

/* Re-evaluate the key-chain prefix OSD on scr against the current
 * KeychainDisplayPosition setting and curpos: hide it for "none" or
 * idle, otherwise rebuild the label and move/map at the configured
 * position. Used by wConfigApplyRuntime() so a live position change
 * takes effect without waiting for the next key press. */
void wRefreshKeyChainOSD(WScreen *scr);

/* called from the signal handler, status is the raw waitpid() value */
void NotifyDeadProcess(pid_t pid, int wait_status);

#endif /* WMEVENT_H */
