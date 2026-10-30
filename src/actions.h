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

#ifndef WMACTIONS_H_
#define WMACTIONS_H_

#include "window.h"
#include "icon.h"

#define MAX_HORIZONTAL         (1 << 0)
#define MAX_VERTICAL           (1 << 1)
#define MAX_LEFTHALF           (1 << 2)
#define MAX_RIGHTHALF          (1 << 3)
#define MAX_TOPHALF            (1 << 4)
#define MAX_BOTTOMHALF         (1 << 5)
#define MAX_MAXIMUS            (1 << 6)
#define MAX_CENTRAL	       (1 << 7)
#define MAX_IGNORE_XINERAMA    (1 << 8)
#define MAX_KEYBOARD           (1 << 9)

#define SAVE_GEOMETRY_X        (1 << 0)
#define SAVE_GEOMETRY_Y        (1 << 1)
#define SAVE_GEOMETRY_WIDTH    (1 << 2)
#define SAVE_GEOMETRY_HEIGHT   (1 << 3)
#define SAVE_GEOMETRY_ALL      SAVE_GEOMETRY_X | SAVE_GEOMETRY_Y | SAVE_GEOMETRY_WIDTH | SAVE_GEOMETRY_HEIGHT

void wSetFocusTo(WScreen *scr, WWindow *wwin);
void wSetFocusToDirection(WScreen *scr, int direction);

int wMouseMoveWindow(WWindow *wwin, xcb_generic_event_t *ev);
int wKeyboardMoveResizeWindow(WWindow *wwin);

void wMouseResizeWindow(WWindow *wwin, xcb_generic_event_t *ev);

void wShadeWindow(WWindow *wwin);
void wUnshadeWindow(WWindow *wwin);

void wIconifyWindow(WWindow *wwin);
void wDeiconifyWindow(WWindow *wwin);

void wSelectWindows(WScreen *scr, xcb_generic_event_t *ev);

void wSelectWindow(WWindow *wwin, bool flag);
void wUnselectWindows(WScreen *scr);

void wMaximizeWindow(WWindow *wwin, int directions, int head);
void wUnmaximizeWindow(WWindow *wwin);
void handleMaximize(WWindow *wwin, int directions);

void wHideAll(WScreen *src);
void wHideOtherApplications(WWindow *wwin);
void wMiniaturizeOtherApplications(WWindow *wwin);
void wShowAllWindows(WScreen *scr);

void wHideApplication(WApplication *wapp);
void wRequestHideApplication(WApplication *wapp, WWindow *via);
void wUnhideApplication(WApplication *wapp, bool miniwindows,
                        bool bringToCurrentWS);

void wHideWindow(WWindow *wwin, WIcon *icon, int icon_x, int icon_y, int animate);
void wUnhideWindow(WWindow *wwin, WIcon *icon, int icon_x, int icon_y,
		   int animate, int bringToCurrentWS);

void wRefreshDesktop(WScreen *scr);
void wRefreshMiniwindows(WScreen *scr);

void wArrangeIcons(WScreen *scr, bool arrangeAll);

void wMakeWindowVisible(WWindow *wwin);

void wFullscreenMonitorsWindow(WWindow *wwin, unsigned long top, unsigned long bottom,
					unsigned long left, unsigned long right);

void wFullscreenWindow(WWindow *wwin);
void wUnfullscreenWindow(WWindow *wwin);

void animateResize(WScreen *scr, int x, int y, int w, int h, int fx, int fy, int fw, int fh);
void update_saved_geometry(WWindow *wwin);
void save_old_geometry(WWindow *wwin, int directions);

void movePointerToWindowCenter(WWindow *wwin);
void moveBetweenHeads(WWindow *wwin, int direction);

bool find_nearest_edge(WWindow *self, int head, int workspace,
		       int direction, int from_offset, int to_offset,
		       int axis_lo, int axis_hi, int max_dist, int gap,
		       int seed_gap, int *best_offset);
void wMoveToEdge(WWindow *wwin, int direction, bool snap_to_windows);
void wGrowToEdge(WWindow *wwin, int direction);
void wShrinkToEdge(WWindow *wwin, int direction);

WWindow *wWindowUnderPointerForFocus(WScreen *scr, WWindow *exclude);

void warpPointerToFocusedWindow(WWindow *wwin);
void warpPointerToMonitor(WScreen *scr);
void warpPointerToNextMonitor(WScreen *scr);

#endif
