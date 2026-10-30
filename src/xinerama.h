/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2001 Alfredo K. Kojima
 *  Copyright (c) 2023-2026 Window Maker Team
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

#ifndef _WMXINERAMA_H_
#define _WMXINERAMA_H_

#include "appicon.h"
#include "screen.h"
#include "window.h"
#include <WINGs/WINGs.h>

void wInitXinerama(WScreen *scr);

/* Shared upper bound on Xinerama/RandR heads, used to size fixed
 * per-head arrays in WScreen and as the RandR output cap. Loops over
 * per-head state must still clamp to wXineramaHeads(scr). */
#define WM_MAX_HEADS 32

#define wXineramaHeads(scr) ((scr)->xine_info.count ? (scr)->xine_info.count : 1)
#define wHeadCount(scr)      WMIN(wXineramaHeads(scr), WM_MAX_HEADS)

#define XFLAG_NONE	0x00
#define XFLAG_DEAD	0x01
#define XFLAG_MULTIPLE	0x02
#define XFLAG_PARTIAL	0x04

enum {
	DIRECTION_LEFT,
	DIRECTION_RIGHT,
	DIRECTION_UP,
	DIRECTION_DOWN
};

int wGetRectPlacementInfo(WScreen *scr, WMRect rect, int *flags);

int wGetHeadForRect(WScreen *scr, WMRect rect);

int wGetHeadForWindow(WWindow *wwin);

int wGetHeadRelativeToCurrentHead(WScreen *scr, int current_head, int direction);

int wGetHeadForPoint(WScreen *scr, WMPoint point);

int wGetHeadForPointerLocation(WScreen *scr);

WMRect wGetRectForHead(WScreen *scr, int head);

WArea wGetUsableAreaForHead(WScreen *scr, int head, WArea *totalAreaPtr, bool noicons);

WMPoint wGetPointToCenterRectInHead(WScreen *scr, int head, int width, int height);

void wGetRectUnion(const WMRect *rect1, const WMRect *rect2, WMRect *dest);

bool wWindowTouchesHead(WWindow *wwin, int head);
bool wAppIconTouchesHead(WAppIcon *aicon, int head);

/* Per-head workspace support */
int wGetActiveHead(WScreen *scr);
int wWindowEffectiveHead(WWindow *wwin);
int wHeadForOutputName(WScreen *scr, const char *name);
const char *wOutputNameForHead(WScreen *scr, int head);
void wClearHeadOutputNames(WScreen *scr);

#endif
