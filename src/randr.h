/* randr.h - RandR multi-monitor support
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

#ifndef _WMRANDR_H_
#define _WMRANDR_H_

#ifdef USE_RANDR

#include <xcb/randr.h>
#include "screen.h"

static inline bool wRandRScreenSizeForMode(int mode_width, int mode_height,
					  uint16_t rotation, int x,
					  int current_width, int current_height,
					  int min_width, int min_height,
					  int max_width, int max_height,
					  int *width, int *height)
{
	int tmp;

	if (rotation & (XCB_RANDR_ROTATION_ROTATE_90 | XCB_RANDR_ROTATION_ROTATE_270)) {
		tmp = mode_width;
		mode_width = mode_height;
		mode_height = tmp;
	}

	*width = x + mode_width;
	if (*width < current_width)
		*width = current_width;
	if (*width < min_width)
		*width = min_width;
	*height = mode_height;
	if (*height < current_height)
		*height = current_height;
	if (*height < min_height)
		*height = min_height;

	return *width <= max_width && *height <= max_height;
}

void wRandRInit(WScreen *scr);
void wRandRTeardown(WScreen *scr);
void wRandRHandleNotify(WScreen *scr);

#endif /* USE_RANDR */

#endif /* _WMRANDR_H_ */
