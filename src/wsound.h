/*
 *  wsound.h
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 2001-2004 Dan Pascu
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

#ifndef WSOUND_H_
#define WSOUND_H_

#define WSOUND_SHADE       "shade"
#define WSOUND_UNSHADE     "unshade"
#define WSOUND_MAXIMIZE    "maximize"
#define WSOUND_UNMAXIMIZE  "unmaximize"
#define WSOUND_ICONIFY     "iconify"
#define WSOUND_DEICONIFY   "deiconify"
#define WSOUND_HIDE        "hide"
#define WSOUND_UNHIDE      "unhide"
#define WSOUND_APPSTART    "appstart"
#define WSOUND_APPEXIT     "appexit"
#define WSOUND_DOCK        "dock"
#define WSOUND_UNDOCK      "undock"
#define WSOUND_KABOOM      "kaboom"
#define WSOUND_STARTUP     "startup"
#define WSOUND_SHUTDOWN    "shutdown"
#define WSOUND_WORKSPACE   "workspace"
#define WSOUND_SCREENSHOT  "screenshot"
#define WSOUND_LOCK        "lock"

void wSoundPlay(WScreen *scr, const char *event);

#endif /* WSOUND_H_ */
