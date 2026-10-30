/* command.c - WindowMaker commands
 *
 * WMlib - WindowMaker application programming interface
 *
 * Copyright (C) 1997-2003 Alfredo K. Kojima
 * Copyright (c) 2009-2026 Window Maker Team
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
 */

#include <sys/socket.h>
#include <sys/time.h>
#include <xcb/xcb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <WINGs/WIPC.h>

#include "wipcreply.h"
#include "WMaker.h"
#include "app.h"

#define IPC_REPLY_TIMEOUT_SECS 5

static xcb_window_t findRootWindow(WMAppContext *app)
{
	const xcb_setup_t *setup = xcb_get_setup(app->dpy);
	xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
	int i;

	for (i = 0; i < app->screen_number && iter.rem; i++)
		xcb_screen_next(&iter);

	return iter.rem ? iter.data->root : XCB_NONE;
}

/* Primary path: the AF_UNIX IPC. Returns true when the request was
 * accepted, false on any transport or protocol failure so the caller
 * can fall back to the deprecated root-window message. */
static int sendIPCBody(const char *body)
{
	char *path, *rbuf = NULL;
	uint32_t rtype = 0, rlen = 0;
	struct timeval tv;
	int fd, ok;

	path = wIPCSocketPath(NULL);
	fd = wIPCConnect(path);
	free(path);
	if (fd < 0)
		return 0;

	tv.tv_sec = IPC_REPLY_TIMEOUT_SECS;
	tv.tv_usec = 0;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	ok = (wIPCSend(fd, WIPC_COMMAND, body, strlen(body)) == 0);
	if (ok) {
		if (wIPCRecv(fd, &rtype, &rbuf, &rlen) == 0) {
			/* 1 accepted, 0 refused, -1 malformed reply */
			ok = (wipcreply_success(rbuf) == 1);
			free(rbuf);
		} else {
			ok = 0;
		}
	}
	close(fd);

	return ok;
}

/* Hide requests go through the generic `action' verb with the
 * group-leader XID as the target window. */
static int sendIPCAction(const char *action, xcb_window_t xid)
{
	char body[96];

	snprintf(body, sizeof(body), "((action %s %lu))", action,
		 (unsigned long)xid);
	return sendIPCBody(body);
}

static void sendLegacyFunc(WMAppContext *app, int code)
{
	xcb_client_message_event_t event;
	xcb_window_t root = findRootWindow(app);

	if (root == XCB_NONE)
		return;

	memset(&event, 0, sizeof(event));
	event.response_type = XCB_CLIENT_MESSAGE;
	event.format = 32;
	event.window = app->main_window;
	event.type = app->wmfunc_atom;
	event.data.data32[0] = (uint32_t) code;

	xcb_send_event(app->dpy, 0, root,
		       XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
		       (const char *)&event);
	xcb_flush(app->dpy);
}

void WMHideApplication(WMAppContext *app)
{
	if (sendIPCAction("Hide", app->main_window))
		return;
	sendLegacyFunc(app, WMFHideApplication);
}

void WMHideOthers(WMAppContext *app)
{
	if (sendIPCAction("HideOthers", app->main_window))
		return;
	sendLegacyFunc(app, WMFHideOtherApplications);
}

void WMSetWindowAttributes(xcb_connection_t *dpy, xcb_window_t window,
			   const GNUstepWMAttributes *attributes)
{
	uint32_t words[9];
	xcb_intern_atom_cookie_t cookie;
	xcb_intern_atom_reply_t *reply;
	xcb_atom_t atom;

	cookie = xcb_intern_atom(dpy, 0, strlen("_GNUSTEP_WM_ATTR"), "_GNUSTEP_WM_ATTR");
	reply = xcb_intern_atom_reply(dpy, cookie, NULL);
	if (!reply)
		return;
	atom = reply->atom;
	free(reply);

	/* pack the fields explicitly: the struct is fixed-width already
	 * but never hand the raw struct to a CARD32 property, the wire
	 * elements are 4 bytes on every platform */
	words[0] = attributes->flags;
	words[1] = attributes->window_style;
	words[2] = attributes->window_level;
	words[3] = attributes->reserved;
	words[4] = attributes->miniaturize_pixmap;
	words[5] = attributes->close_pixmap;
	words[6] = attributes->miniaturize_mask;
	words[7] = attributes->close_mask;
	words[8] = attributes->extra_flags;

	xcb_change_property(dpy, XCB_PROP_MODE_REPLACE, window, atom, atom, 32, 9, words);
}
