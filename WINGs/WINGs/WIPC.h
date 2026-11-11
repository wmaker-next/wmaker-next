/*
 *  WIPC - Window Maker IPC wire protocol and blocking client helpers
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, see <https://www.gnu.org/licenses/>.
 *
 *  AF_UNIX SOCK_STREAM, fixed 16-byte header, native byte order
 *  (the socket is same-host by definition), payload is a textual
 *  property-list description. See doc/wmaker-ipc.txt for the full
 *  message and event tables.
 *
 *  This header deliberately does not include <WINGs/WUtil.h>, which
 *  pulls in Xlib, so text-mode clients can include only this and link
 *  -lWUtil with no X dependency.
 */

#ifndef _WIPC_H_
#define _WIPC_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIPC_MAGIC        "wmaker0\n"
#define WIPC_HEADER_LEN   16
#define WIPC_MAX_PAYLOAD  (1u << 20)
#define WIPC_EVENT_BIT    (1u << 31)

enum WIPCType {
	WIPC_COMMAND = 0,
	WIPC_SUBSCRIBE,
	WIPC_GET_WORKSPACES,
	WIPC_GET_WINDOWS,
	WIPC_GET_VERSION,
	WIPC_SEND_TICK,
	WIPC_GET_WINDOW_TREE
};

enum WIPCEvent {
	WIPC_EV_WORKSPACE = 0,
	WIPC_EV_WINDOW,
	WIPC_EV_SHUTDOWN,
	WIPC_EV_CHILD_EXIT,
	WIPC_EV_DIALOG,
	WIPC_EV_TICK,
	WIPC_EV_SCREENSHOT,
	WIPC_EV_DISPLAY_POWER,
	WIPC_EV_SOUND,
	WIPC_EV_COUNT
};

/* Sound event table: identifier suffix, IPC wire name, WMSound domain
 * key. The domain keys must stay in sync with WMSound.in */
#define WSOUND_EVENTS(X) \
	X(SHADE,       "shade",      "Shade") \
	X(UNSHADE,     "unshade",    "UnShade") \
	X(MAXIMIZE,    "maximize",   "Maximize") \
	X(UNMAXIMIZE,  "unmaximize", "UnMaximize") \
	X(ICONIFY,     "iconify",    "Iconify") \
	X(DEICONIFY,   "deiconify",  "DeIconify") \
	X(HIDE,        "hide",       "Hide") \
	X(UNHIDE,      "unhide",     "UnHide") \
	X(APPSTART,    "appstart",   "AppStart") \
	X(APPEXIT,     "appexit",    "AppExit") \
	X(DOCK,        "dock",       "Dock") \
	X(UNDOCK,      "undock",     "UnDock") \
	X(KABOOM,      "kaboom",     "Kaboom") \
	X(STARTUP,     "startup",    "Startup") \
	X(SHUTDOWN,    "shutdown",   "Shutdown") \
	X(WORKSPACE,   "workspace",  "Workspace") \
	X(SCREENSHOT,  "screenshot", "Screenshot") \
	X(LOCK,        "lock",       "Lock")

/* forward declaration only, callers that touch the result include
 * <WINGs/WUtil.h> themselves, the struct tag is used directly here so
 * including both headers does not redeclare the typedef */
struct W_PropList;

/* Resolve the client-side socket path for the given X display name.
 * A NULL or empty display means $DISPLAY. The display is canonicalised
 * (":0" and ":0.0" both map to "_0_0") so a client started without an
 * explicit screen suffix reaches the same socket the server bound for
 * screen 0. Honours $WMAKER_IPC_SOCKET as an absolute override, else
 * uses $XDG_RUNTIME_DIR/wmaker/, else /tmp/wmaker-<uid>/. Returned
 * string is wmalloc'd, never NULL. */
char *wIPCSocketPath(const char *display);

/* Blocking connect() to a resolved path. Returns the fd on success,
 * -1 with errno set on failure. The socket is close-on-exec. */
int wIPCConnect(const char *path);

/* Frame and write one message. payload may be NULL when len is 0.
 * Returns 0 on success, -1 with errno set on short write or error. */
int wIPCSend(int fd, uint32_t type, const char *payload, size_t len);

/* Build a flat proplist array of count strings, serialise it and send
 * it as a single frame of the given type. Convenience for SUBSCRIBE
 * and similar string-list messages so callers do not open-code the
 * create/append/describe/release/send/free sequence. */
int wIPCSendStringArray(int fd, uint32_t type, const char *const *items, int count);

/* Blocking receive of one complete frame. *payload is wmalloc'd with a
 * trailing NUL appended (so *len bytes of wire data, (*payload)[*len]
 * == '\0'). Returns 0 on success, -1 on read error or protocol
 * violation (bad magic, oversize length, EOF mid-frame), in which case
 * *payload is NULL. */
int wIPCRecv(int fd, uint32_t *type, char **payload, uint32_t *len);

/* Convenience for one-shot callers (WPrefs, setstyle): resolve the
 * path for display, connect, send a single COMMAND with verb cmd and
 * no arguments, read one reply, close, and return the parsed reply
 * proplist (release with WMReleasePropList). Returns NULL on any
 * transport failure. */
struct W_PropList *wIPCCommand(const char *display, const char *cmd);

#ifdef __cplusplus
}
#endif

#endif /* _WIPC_H_ */
