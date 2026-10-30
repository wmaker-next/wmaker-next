/* colormap.c - colormap handling code
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1998-2003 Alfredo K. Kojima
 *  Copyright (c) Rob Nation and others
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
 *
 *  This code slightly based on fvwm code, but completely rewritten.
 */

#include "wconfig.h"

#include "WindowMaker.h"

#include "window.h"
#include "framewin.h"
#include "colormap.h"
#include "misc.h"


void wColormapInstallForWindow(WScreen * scr, WWindow * wwin)
{
	int i;
	WXWindowAttributes attributes;
	xcb_colormap_t cmap;
	int done = 0;

	scr->cmap_window = wwin;

	if (scr->root_colormap_install_count > 0) {
		scr->original_cmap_window = wwin;
		return;
	}

	/* install colormap for all windows of the client */
	if (wwin && wwin->cmap_window_no > 0 && wwin->cmap_windows) {
		for (i = wwin->cmap_window_no - 1; i >= 0; i--) {
			xcb_window_t w;

			w = wwin->cmap_windows[i];
			if (w == wwin->client_win)
				done = 1;

			wXGetWindowAttributes(wdisp->conn, w, &attributes, NULL);
			if (attributes.colormap == XCB_NONE)
				attributes.colormap = scr->colormap;

			if (scr->current_colormap != attributes.colormap) {
				scr->current_colormap = attributes.colormap;
				/*
				 * ICCCM 2.0: some client requested permission
				 * to install colormaps by itself and we granted.
				 * So, we can't install any colormaps.
				 */
				if (!scr->flags.colormap_stuff_blocked)
					xcb_install_colormap(wdisp->conn, attributes.colormap);
			}
		}
	}

	if (!done) {
		/* the client's own colormap was captured from the
		 * GetWindowAttributes reply at manage time and is kept
		 * in sync by ColormapNotify, and the root colormap is
		 * the screen default by definition, so neither needs a
		 * round-trip here */
		cmap = wwin ? wwin->colormap : scr->colormap;
		if (cmap == XCB_NONE)
			cmap = scr->colormap;

		if (scr->current_colormap != cmap) {
			scr->current_colormap = cmap;
			if (!scr->flags.colormap_stuff_blocked)
				xcb_install_colormap(wdisp->conn, cmap);
		}
	}
	wXFlush(wdisp->conn);
}

void wColormapAllowClientInstallation(WScreen * scr, bool starting)
{
	scr->flags.colormap_stuff_blocked = starting;
	/*
	 * Client stopped managing the colormap stuff. Restore the colormap
	 * that would be installed if the client did not request colormap
	 * stuff.
	 */
	if (!starting) {
		xcb_install_colormap(wdisp->conn, scr->current_colormap);
		wXFlush(wdisp->conn);
	}
}
