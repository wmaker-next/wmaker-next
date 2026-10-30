/*
 *  usleep.c
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 1998 scottc
 *  Copyright (c) 1998-2004 Dan Pascu
 *  Copyright (c) 1999 Alfredo K. Kojima
 *  Copyright (c) 2009-2026 Window Maker Team
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

#include <errno.h>
#include <time.h>

#include "WUtil.h"
#include "wconfig.h"

void wusleep(unsigned int usec)
{
	struct timespec tm;

	/* An arbitrary limit of 10 minutes -- in WM, if
	 * somethings wants to sleep anything even close to
	 * this, it's most likely an error.
	 */
	if (usec > 600000000)
		return;

	tm.tv_sec = usec / 1000000;
	tm.tv_nsec = (usec % 1000000) * 1000;

	while (nanosleep(&tm, &tm) == -1 && errno == EINTR)
		;

}
