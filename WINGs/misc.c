/* WUtil / misc.c
 *
 *  Copyright (c) 2001 Dan Pascu
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

/* Miscellaneous helper functions */

#include <WUtil.h>

#include "WINGsP.h"
#include "error.h"

WMRange wmkrange(int start, int count)
{
	WMRange range;

	range.position = start;
	range.count = count;

	return range;
}

void wpickpopupanchor(bool have_focus, int focus_x, int focus_y,
		      int focus_w, int focus_h, bool pointer_on_focus_head,
		      int pointer_x, int pointer_y, int *anchor_x, int *anchor_y)
{
	if (have_focus && !pointer_on_focus_head) {
		*anchor_x = focus_x + focus_w / 2;
		*anchor_y = focus_y + focus_h / 2;
	} else {
		*anchor_x = pointer_x;
		*anchor_y = pointer_y;
	}
}

void wplacepopupnearrect(int fx, int fy, int fw, int fh,
			 int pw, int ph,
			 int ux, int uy, int uw, int uh,
			 int *px, int *py)
{
	int x, y;

	x = fx + (fw - pw) / 2;
	if (pw <= fw && ph <= fh) {
		y = fy + (fh - ph) / 2;
	} else {
		y = fy + fh + 4;
		if (y + ph > uy + uh)
			y = fy - ph - 4;
	}
	if (x + pw > ux + uw)
		x = ux + uw - pw;
	if (x < ux)
		x = ux;
	if (y + ph > uy + uh)
		y = uy + uh - ph;
	if (y < uy)
		y = uy;
	*px = x;
	*py = y;
}

/*
 * wutil_shutdown - cleanup in WUtil when user program wants to exit
 */
void wutil_shutdown(void)
{
	W_EventLoopShutdown();
#ifdef HAVE_SYSLOG
	w_syslog_close();
#endif
}
