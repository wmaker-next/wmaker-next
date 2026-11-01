/*
 *  colorpick.c
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 2004 Alfredo K. Kojima
 *  Copyright (c) 2004 Dan Pascu
 *  Copyright (c) 2009-2026 Window Maker Team
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

#include <stdlib.h>
#include <stdio.h>
#include <xcb/xcb.h>
#include <WINGs/WINGs.h>

void showSelectedColor(void *self, void *cdata)
{
	WMColorPanel *panel = (WMColorPanel *) self;

	(void) cdata;
	printf("Selected Color: %s\n", WMGetColorRGBDescription(WMGetColorPanelColor(panel)));
}

int main(int argc, char **argv)
{
	xcb_connection_t *xc;
	int sn;
	WMScreen *scr;
	WMColorPanel *panel;

	WMInitializeApplication("wmcolorpick", &argc, argv);

	/* xcb_connect(NULL, ...) reads $DISPLAY exactly like libX11's
	 * XOpenDisplay("") did */
	xc = xcb_connect(NULL, &sn);
	if (xcb_connection_has_error(xc)) {
		printf("could not open display\n");
		exit(1);
	}

	scr = WMCreateScreen(xc, sn);

	panel = WMGetColorPanel(scr);
	WMSetColorPanelAction(panel, showSelectedColor, NULL);
	WMShowColorPanel(panel);

	WMScreenMainLoop(scr);

	return 0;
}
