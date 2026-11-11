/*
 *  wipccmd - command helpers for the Window Maker IPC socket
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
 *  The helpers here build or parse proplist payloads, so they stay in
 *  libWUtil; wipc.c itself is kept free of libWUtil symbols so the
 *  same transport source also builds into the LGPL-only libWMaker.
 */

#include "wconfig.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <WINGs/WUtil.h>
#include <WINGs/WIPC.h>

int wIPCSendStringArray(int fd, uint32_t type, const char *const *items, int count)
{
	WMPropList *arr, *s;
	char *body;
	int i, rc;

	arr = WMCreatePLArray(NULL);
	for (i = 0; i < count; i++) {
		s = WMCreatePLString(items[i]);
		WMAddToPLArray(arr, s);
		WMReleasePropList(s);
	}
	body = WMGetPropListDescription(arr, false);
	WMReleasePropList(arr);

	rc = wIPCSend(fd, type, body, strlen(body));
	wfree(body);
	return rc;
}

WMPropList *wIPCCommand(const char *display, const char *cmd)
{
	WMPropList *verb, *inner, *outer, *reply;
	char *path, *body, *rbuf;
	uint32_t rtype, rlen;
	int fd, ok;

	reply = NULL;
	path = wIPCSocketPath(display);
	fd = wIPCConnect(path);
	wfree(path);
	if (fd < 0)
		return NULL;

	verb = WMCreatePLString(cmd);
	inner = WMCreatePLArray(verb, NULL);
	WMReleasePropList(verb);
	outer = WMCreatePLArray(inner, NULL);
	WMReleasePropList(inner);
	body = WMGetPropListDescription(outer, false);
	WMReleasePropList(outer);

	ok = (wIPCSend(fd, WIPC_COMMAND, body, strlen(body)) == 0);
	wfree(body);

	if (ok && wIPCRecv(fd, &rtype, &rbuf, &rlen) == 0) {
		if (rtype == WIPC_COMMAND && rlen > 0)
			reply = WMCreatePropListFromDescription(rbuf);
		wfree(rbuf);
	}

	close(fd);
	return reply;
}
