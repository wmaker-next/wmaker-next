/*
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

#include "wconfig.h"
#include <WINGs/WUtil.h>
#include "appicon.h"

static void setCommand(char **slot, char *command)
{
	if (command == *slot) {
		if (command && (command[0] == '\0' || (command[0] == '-' && command[1] == '\0'))) {
			wfree(command);
			*slot = NULL;
		}
		return;
	}

	if (command && (command[0] == '\0' || (command[0] == '-' && command[1] == '\0'))) {
		wfree(command);
		command = NULL;
	}
	if (*slot)
		wfree(*slot);
	*slot = command;
}

void wAppIconSetCommand(WAppIcon *aicon, char *command)
{
	setCommand(&aicon->command, command);
}

void wAppIconSetPasteCommand(WAppIcon *aicon, char *command)
{
	setCommand(&aicon->paste_command, command);
}

#ifdef USE_DOCK_XDND
void wAppIconSetDnDCommand(WAppIcon *aicon, char *command)
{
	setCommand(&aicon->dnd_command, command);
}
#endif
