/* wipcreply.h - internal: wmaker IPC command-reply envelope reader
 *
 * Copyright (c) 2026 Window Maker Team
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

#ifndef _WIPCREPLY_H_
#define _WIPCREPLY_H_

/* parse a command reply description, return 1 when success = YES,
 * 0 when the reply is well-formed but not accepted (missing key,
 * NO or any other value), -1 when the description is malformed */
int wipcreply_success(const char *desc);

#endif
