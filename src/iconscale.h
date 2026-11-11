/*
 *  iconscale.h
 *
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

#ifndef WMICONSCALE_H_
#define WMICONSCALE_H_

#include <wraster.h>

struct RImage;

/* Downscale to max_size minus the icon border margin on the long side,
 * preserving the aspect ratio and positive dimensions. Takes ownership
 * of icon and returns its replacement, or icon when unchanged. */
RImage *wIconValidateIconSize(RImage *icon, int max_size);

/* On scaling failure, returns NULL without releasing icon. */
RImage *wIconValidateIconSizeStrict(RImage *icon, int max_size);

#endif /* WMICONSCALE_H_ */
