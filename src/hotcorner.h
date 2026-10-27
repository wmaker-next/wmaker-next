/* hotcorner.h - hot-corner InputOnly windows
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

#ifndef WMHOTCORNER_H
#define WMHOTCORNER_H

#include <stdbool.h>

#include "screen.h"

#define CORNER_NONE        0
#define CORNER_TOPLEFT     1
#define CORNER_TOPRIGHT    2
#define CORNER_BOTTOMLEFT  3
#define CORNER_BOTTOMRIGHT 4

void wHotCornersUpdate(WScreen *scr);
void wHotCornersRaise(WScreen *scr);
int  wHotCornerForWindow(WScreen *scr, xcb_window_t w);
int  wHotCornerEdgeForRect(WMRect r);
bool wHotCornerContainsPoint(WScreen *scr, int corner, int x, int y);
void wHotCornersEnter(WScreen *scr, int corner);
void wHotCornersLeave(WScreen *scr);

#endif
