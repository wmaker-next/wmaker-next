/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2009-2026 Window Maker Team
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
#include <string.h>

#include "WindowMaker.h"
#include "wcore.h"


/*----------------------------------------------------------------------
 * wCoreCreateTopLevel--
 * 	Creates a toplevel window used for icons, menus and dialogs.
 *
 * Returns:
 * 	The created window.
 *--------------------------------------------------------------------- */
WCoreWindow *wCoreCreateTopLevel(WScreen *screen, int x, int y, int width, int height, int bwidth, int depth, xcb_visualtype_t *visual, xcb_colormap_t colormap, WMPixel border_pixel)
{
	WCoreWindow *core;
	uint32_t vals[6];
	uint32_t xvmask;
	int nv;

	core = wmalloc(sizeof(WCoreWindow));

	xvmask = XCB_CW_BORDER_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK
	       | XCB_CW_COLORMAP | XCB_CW_CURSOR;
	nv = 0;
	vals[nv++] = border_pixel;
	vals[nv++] = true;
	if (w_global.config->use_saveunders) {
		xvmask |= XCB_CW_SAVE_UNDER;
		vals[nv++] = true;
	}
	vals[nv++] = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_BUTTON_PRESS |
		     XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION |
		     XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW;
	vals[nv++] = colormap;
	vals[nv++] = wdisp->cursor[WCUR_NORMAL];

	core->window = xcb_generate_id(wdisp->conn);
	xcb_create_window(wdisp->conn, depth, core->window, screen->root_win, x, y, width, height,
			  bwidth, XCB_WINDOW_CLASS_COPY_FROM_PARENT,
			  visual ? visual->visual_id : XCB_COPY_FROM_PARENT,
			  xvmask, vals);
	core->width = width;
	core->height = height;
	core->screen_ptr = screen;
	core->descriptor.self = core;

	xcb_clear_area(wdisp->conn, 0, core->window, 0, 0, 0, 0);
	WMHashInsert(wdisp->context.client_win, (void *)(uintptr_t) core->window, &core->descriptor);
	WMScreenNoteDrawableSize(screen->wmscreen, core->window, width, height);

	return core;
}

/*----------------------------------------------------------------------
 * wCoreCreate--
 * 	Creates a brand new child window.
 * 	The window will have a border width of 0 and color is black.
 *
 * Returns:
 * 	A initialized core window structure.
 *
 * Side effects:
 * 	A window context for the created window is saved.
 *
 * Notes:
 * 	The event mask is initialized to a default value.
 *--------------------------------------------------------------------- */
WCoreWindow *wCoreCreate(WCoreWindow *parent, int x, int y, int width, int height)
{
	WCoreWindow *core;

	core = wmalloc(sizeof(WCoreWindow));

	core->window = xcb_generate_id(wdisp->conn);
	xcb_create_window(wdisp->conn, parent->screen_ptr->w_depth, core->window, parent->window,
			  x, y, width, height, 0,
			  XCB_WINDOW_CLASS_COPY_FROM_PARENT,
			  parent->screen_ptr->w_visual->visual_id,
			  XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK | XCB_CW_COLORMAP | XCB_CW_CURSOR,
			  (const uint32_t[]){ parent->screen_ptr->black_pixel,
					      WCORE_CHILD_EVENT_MASK,
					      parent->screen_ptr->w_colormap,
					      wdisp->cursor[WCUR_NORMAL] });

	core->width = width;
	core->height = height;
	core->screen_ptr = parent->screen_ptr;
	core->descriptor.self = core;

	WMHashInsert(wdisp->context.client_win, (void *)(uintptr_t) core->window, &core->descriptor);
	WMScreenNoteDrawableSize(core->screen_ptr->wmscreen, core->window, width, height);
	return core;
}

void wCoreDestroy(WCoreWindow * core)
{
	if (core->stacking)
		wfree(core->stacking);

	/* drop the cached size and any per-screen render state that is
	 * still pointed at this window before the server auto-frees the
	 * resources hung off it */
	if (core->screen_ptr && core->screen_ptr->wmscreen)
		WMScreenAbandonDrawable(core->screen_ptr->wmscreen, core->window);

	WMHashRemove(wdisp->context.client_win, (void *)(uintptr_t) core->window);
	xcb_destroy_window(wdisp->conn, core->window);
	wfree(core);
}

void wCoreConfigure(WCoreWindow * core, int req_x, int req_y, int req_w, int req_h)
{
	WXWindowChanges xwc;
	unsigned int mask;

	mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y;
	xwc.x = req_x;
	xwc.y = req_y;

	if (req_w <= 0)
		req_w = core->width;

	if (req_h <= 0)
		req_h = core->height;

	if (req_w != core->width || req_h != core->height) {
		mask |= XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
		xwc.width = req_w;
		xwc.height = req_h;
		core->width = req_w;
		core->height = req_h;
		WMScreenNoteDrawableSize(core->screen_ptr->wmscreen, core->window,
					 req_w, req_h);
	}
	wXConfigureWindow(wdisp->conn, core->window, mask, &xwc);
}
