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

#ifndef WMDIALOG_RUN_H_
#define WMDIALOG_RUN_H_

#include "screen.h"

/* Advanced input dialog with history, Tab completion over $PATH,
 * XDG applications and documents, '=' calculator, '#' window and
 * '@' workspace targeting. `name' selects the history file suffix. */
int wAdvancedInputDialog(WScreen *scr, const char *title, const char *message,
			 const char *name, char **text);

#endif
