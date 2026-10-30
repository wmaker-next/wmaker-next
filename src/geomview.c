/*
 *  geomview.c
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 2000-2004 Alfredo K. Kojima
 *  Copyright (c) 2000-2004 Dan Pascu
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

#include <WINGs/WINGsP.h>

#include "geomview.h"

WGeometryView *WCreateGeometryView(WMScreen *scr)
{
	WGeometryView *gview;

	gview = WCreateOSDView(scr);
	if (!gview)
		return NULL;

	/* Lock the box size so it never resizes inside the server-grabbed
	 * interactive move/resize loop. */
	WSetOSDViewFixedTemplate(gview, "+0000 ,  +0000    ");

	return gview;
}

void WSetGeometryViewShownPosition(WGeometryView *gview, int x, int y)
{
	char buffer[64];

	snprintf(buffer, sizeof(buffer), "%+5i , %+5i    ", x, y);
	WSetOSDViewText(gview, buffer);
}

void WSetGeometryViewShownSize(WGeometryView *gview, unsigned width, unsigned height)
{
	char buffer[64];

	snprintf(buffer, sizeof(buffer), "%+5i x %+5i    ", (int)width, (int)height);
	WSetOSDViewText(gview, buffer);
}
