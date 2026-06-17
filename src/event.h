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

#include <xcb/xcb.h>

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

/* per-protocol handlers in event_client.c, event_input.c, event_root.c
 * and event_ext.c are dispatched from a table indexed by
 * response_type & 0x7f.  Each handler reads the typed xcb struct via
 * xe for its own logic and passes the cooked ev through to the legacy
 * XEvent* callback surface (WObjDescriptor, wClientConfigure,
 * wNETWMProcessClientMessage, WMHandleEvent) until those signatures
 * move onto xcb in a later commit.  The wire bytes travel inside ev
 * via WXWireEvent.wire so a handler reached from a modal loop's
 * WMHandleEvent default arm has them too. */
typedef void (*WEventHandler)(const xcb_generic_event_t *xe, XEvent *ev);

void wEventRegisterHandler(unsigned int response_type, WEventHandler h);
void wEventInitDispatch(void);

/* per-module registration, called once from wEventInitDispatch() after
 * the extension probes have filled w_global.xext.*.event_base */
void wEventClientRegister(void);
void wEventInputRegister(void);
void wEventRootRegister(void);
void wEventExtRegister(void);

/* update w_global.timestamp.last_event from any timestamp-bearing
 * event, the dock XDND selection-completion side-effect that lived in
 * the old saveTimestamp() switch is now a proper SelectionNotify
 * handler in event_root.c */
void wSaveTimestamp(const xcb_generic_event_t *xe);

/* the keysym-to-keycode resolution and named-modifier masks may have
 * shifted, re-derive every WShortKey from its config string and
 * regrab on every managed window, both the core MappingNotify
 * (event_input.c) and XKB NewKeyboardNotify/MapNotify (event_ext.c)
 * land here */
void wRefreshKeybindings(void);

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
