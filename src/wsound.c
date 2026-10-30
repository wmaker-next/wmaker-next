/*
 *  wsound.c
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

#include "wconfig.h"
#include "WindowMaker.h"
#include "wsound.h"
#include "ipc.h"

void wSoundPlay(WScreen *scr, const char *event)
{
	if (w_global.config->no_sound)
		return;
	if (!scr || !event || !*event)
		return;
	wIPCEmitSound(scr, event);
}
