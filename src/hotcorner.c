/* hotcorner.c - hot-corner InputOnly windows
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

#include <stdlib.h>

#include "WindowMaker.h"
#include "screen.h"
#include "xinerama.h"
#include "main.h"
#include "keybind.h"
#include "hotcorner.h"


static void hotCornerDelay(void *data)
{
	WScreen *scr = (WScreen *) data;
	const char *cmd = NULL;
	int command;

	if (scr->flags.in_hot_corner)
		cmd = scr->hot_corner_actions[scr->flags.in_hot_corner - 1];
	if (cmd) {
		command = wKeybindForAction(cmd);
		if (command >= 0)
			wKeybindDispatch(command, scr, scr->focused_window, NULL);
		else
			ExecuteShellCommand(scr, cmd);
	}
	WMDeleteTimerHandlerPtr(&scr->hot_corner_timer);
}

static void destroy_windows(WScreen *scr)
{
	int i;

	for (i = 0; i < scr->hot_corner_win_count; i++)
		xcb_destroy_window(wdisp->conn, scr->hot_corner_win[i]);
	if (scr->hot_corner_win)
		wfree(scr->hot_corner_win);
	scr->hot_corner_win = NULL;
	scr->hot_corner_win_count = 0;
}

int wHotCornerEdgeForRect(WMRect r)
{
	int edge = w_global.config->hot_corner_edge;
	int limit = WMIN((int)r.size.width, (int)r.size.height) / 2;

	if (edge < 1)
		edge = 1;
	if (limit < 1)
		limit = 1;

	return WMIN(edge, limit);
}

/*
 * (Re)create, place, map or tear down the per-head InputOnly corner
 * windows according to the current HotCorners / HotCornerEdge settings
 * and the current head geometry. Called from wConfigApplyRuntime() when
 * the relevant keys change and from the RandR path when heads change.
 */
void wHotCornersUpdate(WScreen *scr)
{
	int heads, want, h, i;

	if (!scr)
		return;

	if (!w_global.config->hot_corners) {
		wHotCornersLeave(scr);
		destroy_windows(scr);
		return;
	}

	heads = wXineramaHeads(scr);
	want = heads * 4;

	if (want != scr->hot_corner_win_count) {
		destroy_windows(scr);
		scr->hot_corner_win = wmalloc(sizeof(xcb_window_t) * want);
		scr->hot_corner_win_count = want;

		for (i = 0; i < want; i++) {
			scr->hot_corner_win[i] = xcb_generate_id(wdisp->conn);
			xcb_create_window(wdisp->conn, XCB_COPY_FROM_PARENT, scr->hot_corner_win[i],
					  scr->root_win, -10, -10, 1, 1, 0,
					  XCB_WINDOW_CLASS_INPUT_ONLY, XCB_COPY_FROM_PARENT,
					  XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK,
					  (const uint32_t[]){ true, XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW });
		}
	}

	/* four windows per head laid out as TL, TR, BL, BR so that
	 * (index % 4) + 1 gives the CORNER_* constant directly */
	for (h = 0; h < heads; h++) {
		WMRect r = wGetRectForHead(scr, h);
		int x[4], y[4];
		int e = wHotCornerEdgeForRect(r);

		x[0] = r.pos.x;                     y[0] = r.pos.y;
		x[1] = r.pos.x + r.size.width  - e; y[1] = r.pos.y;
		x[2] = r.pos.x;                     y[2] = r.pos.y + r.size.height - e;
		x[3] = r.pos.x + r.size.width  - e; y[3] = r.pos.y + r.size.height - e;

		for (i = 0; i < 4; i++) {
			xcb_configure_window(wdisp->conn, scr->hot_corner_win[h * 4 + i], XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, (const uint32_t[]){ x[i], y[i], e, e });
			xcb_map_window(wdisp->conn, scr->hot_corner_win[h * 4 + i]);
		}
	}

	wHotCornersRaise(scr);
}

/*
 * Restack the corner windows above everything else. Called from the
 * stacking paths whenever a managed frame can end up at the very top of
 * the X stack, and after wHotCornersUpdate(). Cheap no-op when the
 * feature is disabled.
 */
void wHotCornersRaise(WScreen *scr)
{
	int i;

	/* InputOnly corner windows, never in the stacking list, direct
	 * configure */
	for (i = 0; i < scr->hot_corner_win_count; i++)
		xcb_configure_window(wdisp->conn, scr->hot_corner_win[i], XCB_CONFIG_WINDOW_STACK_MODE, (const uint32_t[]){ XCB_STACK_MODE_ABOVE });
}

int wHotCornerForWindow(WScreen *scr, xcb_window_t w)
{
	int i;

	for (i = 0; i < scr->hot_corner_win_count; i++) {
		if (scr->hot_corner_win[i] == w)
			return (i % 4) + 1;
	}
	return CORNER_NONE;
}

bool wHotCornerContainsPoint(WScreen *scr, int corner, int x, int y)
{
	int heads = wXineramaHeads(scr);
	int h;

	for (h = 0; h < heads; h++) {
		WMRect r = wGetRectForHead(scr, h);
		int e = wHotCornerEdgeForRect(r);
		int cx, cy;

		switch (corner) {
		case CORNER_TOPLEFT:
			cx = r.pos.x;
			cy = r.pos.y;
			break;
		case CORNER_TOPRIGHT:
			cx = r.pos.x + r.size.width - e;
			cy = r.pos.y;
			break;
		case CORNER_BOTTOMLEFT:
			cx = r.pos.x;
			cy = r.pos.y + r.size.height - e;
			break;
		case CORNER_BOTTOMRIGHT:
			cx = r.pos.x + r.size.width - e;
			cy = r.pos.y + r.size.height - e;
			break;
		default:
			return false;
		}

		if (x >= cx && x < cx + e && y >= cy && y < cy + e)
			return true;
	}
	return false;
}

void wHotCornersEnter(WScreen *scr, int corner)
{
	if (scr->flags.in_hot_corner == corner && !scr->hot_corner_timer)
		return;
	scr->flags.in_hot_corner = corner;
	if (!scr->hot_corner_timer)
		scr->hot_corner_timer =
		    WMAddTimerHandler(w_global.config->hot_corner_delay, hotCornerDelay, scr);
}

void wHotCornersLeave(WScreen *scr)
{
	scr->flags.in_hot_corner = CORNER_NONE;
	WMDeleteTimerHandlerPtr(&scr->hot_corner_timer);
}
