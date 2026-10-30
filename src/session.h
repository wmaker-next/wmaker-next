/*  session.h
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1999-2003 Alfredo K. Kojima
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

#ifndef WMSESSION_H_
#define WMSESSION_H_

void wSessionSaveState(WScreen *scr);
void wSessionClearState(WScreen *scr);
void wSessionRestoreState(WScreen *scr);
void wSessionRestoreLastWorkspace(WScreen *scr);
void wSessionQueueSavedState(WScreen *scr);
void wSessionCleanupUnmatchedSavedState(void);
#endif
