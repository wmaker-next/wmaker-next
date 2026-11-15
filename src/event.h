/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
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

#ifndef WMEVENT_H
#define WMEVENT_H

#include "config.h"

#ifdef HAVE_STDNORETURN
#include <stdnoreturn.h>
#endif

#include <xcb/xcb.h>

typedef void (WDeathHandler)(pid_t pid, unsigned int status, void *cdata);

noreturn void EventLoop(void);
void DispatchEvent(xcb_generic_event_t *event);
void ProcessPendingEvents(void);
WMagicNumber wAddDeathHandler(pid_t pid, WDeathHandler *callback, void *cdata,
			      const char *source, int slot);
void wDeleteDeathHandlersForData(void *cdata);
#ifdef HAVE_INOTIFY
void wInotifyCleanup(void);
#endif

/* register an additional watch on the shared inotify fd, the
 * callback fires once per read batch that contained at least one
 * event for this wd, returns the watch descriptor or -1 when
 * inotify is unavailable, no-op stubs when !HAVE_INOTIFY */
int wInotifyAddWatch(const char *path, unsigned mask, void (*cb)(void));
void wInotifyRemoveWatch(int wd);
bool IsDoubleClick(WScreen *scr, xcb_generic_event_t *event);

/* per-protocol handlers in event_client.c, event_input.c, event_root.c
 * and event_ext.c are dispatched from a table indexed by
 * response_type & 0x7f. The event is the raw wire buffer, handlers
 * cast it to the matching xcb_*_event_t and the same pointer reaches
 * the WObjDescriptor.handle_*(desc, xcb_generic_event_t *) callbacks
 * and WMHandleEvent() from a modal loop's default arm. Handlers may
 * rewrite fields in place (the internal-window client_leader redirect
 * in handleKeyPress does), the buffer stays owned by the dispatch
 * loop. */
typedef void (*WEventHandler)(xcb_generic_event_t *event);

void wEventRegisterHandler(unsigned int response_type, WEventHandler h);
void wEventInitDispatch(void);

/* per-module registration, called once from wEventInitDispatch() after
 * the extension probes have filled wdisp->xext.*.event_base */
void wEventClientRegister(void);
void wEventInputRegister(void);
void wEventRootRegister(void);
void wEventExtRegister(void);

/* update wdisp->timestamp.last_event from any timestamp-bearing
 * event, the dock XDND selection-completion side-effect that lived in
 * the old saveTimestamp() switch is now a proper SelectionNotify
 * handler in event_root.c */
void wSaveTimestamp(const xcb_generic_event_t *xe);

/* Bracket a request burst whose generated Enter/Leave events must not
 * reach the crossing handlers. Begin() returns the sequence of an
 * xcb_no_operation issued now; End() issues a closing no-op, records
 * the [begin,end) pair in the wdisp->ignore_crossing ring for
 * dispatch-time filtering, and sweeps anything already drained into
 * the WINGs queue via WXEvqDropRange(). Ignored() is the
 * dispatch-time predicate. */
unsigned long wEventIgnoreCrossingBegin(void);
void wEventIgnoreCrossingEnd(unsigned long begin);
bool wEventCrossingIgnored(unsigned long sequence);

/* the keysym-to-keycode resolution and named-modifier masks may have
 * shifted, re-derive every WShortKey from its config string and
 * regrab on every managed window, both the core MappingNotify
 * (event_input.c) and XKB NewKeyboardNotify/MapNotify (event_ext.c)
 * land here */
void wRefreshKeybindings(void);
void wRereadKeybindings(void);

/* Tear down any active key-chain state on scr: cancel the inactivity
 * timer, hide the prefix OSD, ungrab the keyboard, and clear curpos.
 * Safe to call when no chain is active. */
void wCancelKeyChain(WScreen *scr);

/* End the key chain when the armed position lost its children, pruning
 * the nodes it leaves empty back toward the root so a shorter binding
 * on an ancestor matches again. Safe to call when the chain is idle or
 * still has keys to grow into. */
void wCancelStalledKeyChain(void);

/* Re-evaluate the key-chain prefix OSD on scr against the current
 * KeychainDisplayPosition setting and curpos: hide it for "none" or
 * idle, otherwise rebuild the label and move/map at the configured
 * position. Used by wConfigApplyRuntime() so a live position change
 * takes effect without waiting for the next key press. */
void wRefreshKeyChainOSD(WScreen *scr);

/* who is driving wDisplaySetOff(), carried into the display_power
 * IPC event payload and used to skip the DisplayOff/OnCommand
 * spawn for IPC-driven changes so `wmakermsg display-power off'
 * from a script does not recursively launch the locker the script
 * is coordinating with */
typedef enum {
	WDisplayPowerSourceIPC,
	WDisplayPowerSourceRandR,
	WDisplayPowerSourceScreenSaver
} WDisplayPowerSource;

/* set or clear the display-off state and resume any decorative
 * timers (selected-icon ants, urgent-icon bounce) that disarmed
 * themselves while the display was off, emit the display_power
 * IPC event, and for non-IPC sources spawn the configured
 * DisplayOffCommand or DisplayOnCommand, called from the IPC
 * `display-power' verb and from RandR / MIT-SCREEN-SAVER notify */
void wDisplaySetOff(bool off, WDisplayPowerSource source);

/* called from the signal handler, status is the raw waitpid() value */
void NotifyDeadProcess(pid_t pid, int wait_status);

#endif /* WMEVENT_H */
