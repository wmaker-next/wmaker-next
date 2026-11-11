/*
 *  ipc - Unix-socket control channel and event stream, server side
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

#ifndef _WM_IPC_H_
#define _WM_IPC_H_

#include <stdbool.h>
#include <stdint.h>

#include <WINGs/WIPC.h>

struct _WScreen;
struct W_PropList;

/* opaque per-connection state, full definition in ipc.c, exposed only
 * so WScreen can hold the head of the per-screen client list */
struct WIPCClient;

/* bind the per-screen listening socket and register the accept handler,
 * called once per managed WScreen during StartUp() before session restore
 * and dock auto-launch so spawned children inherit a live socket */
void wIPCServerStart(struct _WScreen *scr);

int wIPCWaitForCycle(struct _WScreen *scr, int timeout_ms);

/* queue a shutdown event to subscribers, drain pending writes for a short
 * bounded interval, then close every client and unlink the socket, mode
 * is one of "exit" or "restart" and goes into the event payload, called
 * from Shutdown() */
void wIPCServerStop(struct _WScreen *scr, const char *mode);

/* broadcast an event to subscribed clients, scr == NULL means
 * process-scoped (every screen's client list), non-NULL limits delivery
 * to that screen's clients, ev is a WIPC_EV_* index (the EVENT_BIT is
 * added here), pl is serialised once and the buffer reused for every
 * recipient, pl may be NULL for an empty payload */
void wIPCBroadcast(struct _WScreen *scr, uint32_t ev, struct W_PropList *pl);

/* true when at least one client would receive ev, scr == NULL checks
 * every screen like wIPCBroadcast() */
bool wIPCIsSubscribed(struct _WScreen *scr, uint32_t ev);

/* convenience emitters used outside ipc.c so callers do not build
 * proplists by hand */
void wIPCEmitChildExit(int pid, int wait_status, const char *source, int slot);
void wIPCEmitDialog(struct _WScreen *scr, const char *kind,
		    const char *title, const char *text);
void wIPCEmitScreenshot(struct _WScreen *scr, const char *path, int mode);
void wIPCEmitDisplayPower(int off, const char *source);
void wIPCEmitSound(struct _WScreen *scr, const char *event);

#endif /* _WM_IPC_H_ */
