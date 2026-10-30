/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2004 Alfredo K. Kojima
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

#ifndef _SWITCHPANEL_H_
#define _SWITCHPANEL_H_

typedef struct SwitchPanel WSwitchPanel;

typedef enum {
	CYCLE_FLAT = 0,     /* all windows, no filtering         */
	CYCLE_CLASS_ONLY,   /* intra-app: same WM_CLASS          */
	CYCLE_GROUP         /* app-group: one tile per WM_CLASS  */
} CycleMode;

typedef enum { STRIP_MAIN, STRIP_SUB } Strip;

WSwitchPanel *wInitSwitchPanel(WScreen *scr, WWindow *curwin, CycleMode mode);

void wSwitchPanelDestroy(WSwitchPanel *panel);

WWindow *wSwitchPanelSelectNext(WSwitchPanel *panel, int back, int ignore_minimized, CycleMode mode, Strip strip);
WWindow *wSwitchPanelSelectFirst(WSwitchPanel *panel, int back, Strip strip);
WWindow *wSwitchPanelSelectByKey(WSwitchPanel *panel, int key_char, int back, Strip strip);

WWindow *wSwitchPanelHandleEvent(WSwitchPanel *panel, xcb_generic_event_t *event);

xcb_window_t wSwitchPanelGetWindow(WSwitchPanel *swpanel);

/* Drill into the highlighted group's windows (sub-panel).
 * Returns the first (MRU) window of the group. */
WWindow *wSwitchPanelDrill(WSwitchPanel *panel);

/* Show the sub-panel without entering drilled mode, so arrow keys
 * stay on the main strip. Returns the group rep on the main strip. */
WWindow *wSwitchPanelPreviewDrill(WSwitchPanel *panel);

/* Rebuild the sub-strip for the group now highlighted on the main strip.
 * Returns the new group's first (MRU) window. */
WWindow *wSwitchPanelRefreshSub(WSwitchPanel *panel);

/* Collapse the sub-strip and return focus to the main strip. Returns the
 * group rep now highlighted on the main strip. */
WWindow *wSwitchPanelUndrill(WSwitchPanel *panel);

/* True once the sub-strip has been opened with wSwitchPanelDrill. */
bool wSwitchPanelIsDrilled(WSwitchPanel *panel);

/* True if the sub-strip is shown as a preview (not focused). */
bool wSwitchPanelIsDrillPreview(WSwitchPanel *panel);

#endif /* _SWITCHPANEL_H_ */
