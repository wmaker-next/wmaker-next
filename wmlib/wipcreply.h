/* wipcreply.h - internal: wmaker IPC command-reply envelope reader
 *
 * Copyright (c) 2026 Window Maker Team
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef _WIPCREPLY_H_
#define _WIPCREPLY_H_

/* parse a command reply description, return 1 when success = YES,
 * 0 when the reply is well-formed but not accepted (missing key,
 * NO or any other value), -1 when the description is malformed */
int wipcreply_success(const char *desc);

#endif
