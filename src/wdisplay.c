/*
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

#include "wconfig.h"

#include <stdlib.h>

#include "WindowMaker.h"
#include "wdisplay.h"

#include <WINGs/WUtil.h>

WDisplay *wdisp = NULL;

WDisplay *wDisplayCreate(void)
{
	WDisplay *disp = wmalloc(sizeof(WDisplay));

	disp->timestamp.last_event = XCB_CURRENT_TIME;
	disp->timestamp.focus_change = XCB_CURRENT_TIME;

	wdisp = disp;

	return disp;
}

void wDisplayAttach(xcb_connection_t *conn, int default_screen)
{
	if (!wdisp)
		return;

	wdisp->conn = conn;
	wdisp->default_screen = default_screen;

	wdisp->context.client_win = WMCreateHashTable(WMIntHashCallbacks);
	wdisp->context.app_win = WMCreateHashTable(WMIntHashCallbacks);
	wdisp->context.stack = WMCreateHashTable(WMIntHashCallbacks);
}

void wDisplayDestroy(void)
{
	int i;

	if (!wdisp)
		return;

	for (i = 0; i < 7; i++)
		wfree(wdisp->modifier_labels[i]);
	for (i = 0; i < 9; i++)
		wfree(wdisp->modifier_short_labels[i]);
	wfree(wdisp->screens);

	if (wdisp->context.client_win)
		WMFreeHashTable(wdisp->context.client_win);
	if (wdisp->context.app_win)
		WMFreeHashTable(wdisp->context.app_win);
	if (wdisp->context.stack)
		WMFreeHashTable(wdisp->context.stack);

	wfree(wdisp);
	wdisp = NULL;
}
