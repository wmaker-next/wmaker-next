/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2004 Alfredo K. Kojima
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

#ifndef _SWITCHPANEL_H_
#define _SWITCHPANEL_H_

typedef struct SwitchPanel WSwitchPanel;

typedef enum {
	CYCLE_FLAT = 0,     /* all windows, no filtering         */
	CYCLE_CLASS_ONLY,   /* intra-app: same WM_CLASS          */
	CYCLE_GROUP         /* app-group: one tile per WM_CLASS  */
} CycleMode;

WSwitchPanel *wInitSwitchPanel(WScreen *scr, WWindow *curwin, CycleMode mode);

void wSwitchPanelDestroy(WSwitchPanel *panel);

WWindow *wSwitchPanelSelectNext(WSwitchPanel *panel, int back, int ignore_minimized, CycleMode mode);
WWindow *wSwitchPanelSelectFirst(WSwitchPanel *panel, int back);
WWindow *wSwitchPanelSelectByKey(WSwitchPanel *panel, int key_char, int back);

WWindow *wSwitchPanelHandleEvent(WSwitchPanel *panel, XEvent *event);

Window wSwitchPanelGetWindow(WSwitchPanel *swpanel);

#endif /* _SWITCHPANEL_H_ */
