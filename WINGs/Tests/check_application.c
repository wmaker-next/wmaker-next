/*
 *  check_application - unit tests for WINGs application lifecycle state
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, see <https://www.gnu.org/licenses/>.
 */

#include <string.h>

#include <WINGs/WINGs.h>
#include <WINGs/WINGsP.h>

#include "check_common.h"

int main(void)
{
	int argc = 1;
	char *argv[] = { "check-application", NULL };

	CHECK(!W_ApplicationInitialized());

	WMInitializeApplication("CheckApplication", &argc, argv);
	CHECK(W_ApplicationInitialized());
	CHECK(WMGetApplicationName() != NULL);
	if (WMGetApplicationName())
		CHECK(strcmp(WMGetApplicationName(), "CheckApplication") == 0);

	WMSetResourcePath("/tmp/check-application-resource");
	WMReleaseApplication();
	CHECK(!W_ApplicationInitialized());
	CHECK(WMGetApplicationName() == NULL);

	/* release must be safe to call again */
	WMReleaseApplication();
	CHECK(!W_ApplicationInitialized());

	return check_report("application lifecycle");
}
