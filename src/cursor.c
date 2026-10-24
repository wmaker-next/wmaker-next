/* cursor.c - shared pointer query helpers for cursor features
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

#include "wconfig.h"

#include <stdint.h>
#include <stdlib.h>

#include <xcb/xcb.h>

#include "WindowMaker.h"
#include "screen.h"
#include "cursor.h"

bool wCursorQueryPointer(WScreen *scr, int *x, int *y, bool *button_held)
{
	xcb_query_pointer_cookie_t cookie;
	xcb_query_pointer_reply_t *reply;
	bool found = false;

	if (!scr || !wdisp || !wdisp->conn)
		return false;

	cookie = xcb_query_pointer(wdisp->conn, scr->root_win);
	reply = xcb_query_pointer_reply(wdisp->conn, cookie, NULL);
	if (reply) {
		if (reply->same_screen) {
			if (x)
				*x = reply->root_x;
			if (y)
				*y = reply->root_y;
			if (button_held) {
				uint16_t buttons = XCB_BUTTON_MASK_1 |
				    XCB_BUTTON_MASK_2 | XCB_BUTTON_MASK_3 |
				    XCB_BUTTON_MASK_4 | XCB_BUTTON_MASK_5;

				*button_held = (reply->mask & buttons) != 0;
			}
			found = true;
		}
		free(reply);
	}

	return found;
}
