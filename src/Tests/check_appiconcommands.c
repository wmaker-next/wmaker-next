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
#include "check_common.h"

typedef void (*CommandSetter)(WAppIcon *, char *);

static void checkSetter(WAppIcon *icon, char **slot, CommandSetter setter)
{
	const char *values[] = { "-x", " - ", " ", "\"quoted\"", "app %s %d %%", NULL };
	char *same;
	int i;

	setter(icon, wstrdup("first"));
	CHECK_STR(*slot, "first");
	setter(icon, wstrdup("second"));
	CHECK_STR(*slot, "second");
	for (i = 0; values[i]; i++) {
		setter(icon, wstrdup(values[i]));
		CHECK_STR(*slot, values[i]);
	}

	same = *slot;
	setter(icon, same);
	CHECK(*slot == same);
	CHECK_STR(*slot, "app %s %d %%");

	setter(icon, NULL);
	CHECK(*slot == NULL);
	setter(icon, NULL);
	CHECK(*slot == NULL);
	setter(icon, wstrdup("before empty"));
	setter(icon, wstrdup(""));
	CHECK(*slot == NULL);
	setter(icon, wstrdup("before dash"));
	setter(icon, wstrdup("-"));
	CHECK(*slot == NULL);

	*slot = wstrdup("");
	setter(icon, *slot);
	CHECK(*slot == NULL);
	*slot = wstrdup("-");
	setter(icon, *slot);
	CHECK(*slot == NULL);
}

static void checkIndependence(void)
{
	WAppIcon icon = { 0 };

	wAppIconSetCommand(&icon, wstrdup("main"));
	wAppIconSetPasteCommand(&icon, wstrdup("paste %s"));
	CHECK(!icon.forced_dock);
	icon.forced_dock = 1;
#ifdef USE_DOCK_XDND
	wAppIconSetDnDCommand(&icon, wstrdup("drop %d"));
#endif
	wAppIconSetCommand(&icon, wstrdup("new main"));
	CHECK(icon.forced_dock);
	CHECK_STR(icon.paste_command, "paste %s");
#ifdef USE_DOCK_XDND
	CHECK_STR(icon.dnd_command, "drop %d");
	wAppIconSetDnDCommand(&icon, wstrdup("new drop %d"));
	CHECK_STR(icon.command, "new main");
	CHECK_STR(icon.paste_command, "paste %s");
#endif
	wAppIconSetPasteCommand(&icon, NULL);
	CHECK_STR(icon.command, "new main");
#ifdef USE_DOCK_XDND
	CHECK_STR(icon.dnd_command, "new drop %d");
	wAppIconSetDnDCommand(&icon, NULL);
#endif
	wAppIconSetCommand(&icon, NULL);
	wAppIconSetPasteCommand(&icon, NULL);
}

int main(void)
{
	WAppIcon icon = { 0 };

	checkSetter(&icon, &icon.command, wAppIconSetCommand);
	checkSetter(&icon, &icon.paste_command, wAppIconSetPasteCommand);
#ifdef USE_DOCK_XDND
	checkSetter(&icon, &icon.dnd_command, wAppIconSetDnDCommand);
#endif
	checkIndependence();
	return check_report("appiconcommands");
}
