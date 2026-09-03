/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2014-2026 Window Maker Team
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
 *  with this program; if not, write to the Free Software Foundation
 */

#include <stdbool.h>
#ifndef _XDND_H_
#define _XDND_H_

void wXDNDInitializeAtoms(void);
bool wXDNDProcessSelection(xcb_generic_event_t *event);
bool wXDNDProcessClientMessage(const xcb_client_message_event_t *event);
void wXDNDMakeAwareness(xcb_window_t window);

#ifndef XDND_VERSION
#define XDND_VERSION 3L
#endif

/* accessor macros over the xcb_client_message_event_t data.data32[]
 * words, the source window is data32[0] for every XdndEnter/Position/
 * Drop message and the type slots ride in data32[2..4] on Enter */
#define XDND_ENTER_SOURCE_WIN(e)	((e)->data.data32[0])
#define XDND_ENTER_THREE_TYPES(e)	(((e)->data.data32[1] & 0x1u) == 0)
#define XDND_ENTER_TYPE(e, i)		((e)->data.data32[2 + (i)])	/* i => (0, 1, 2) */

/* XdndPosition */
#define XDND_POSITION_SOURCE_WIN(e)     ((e)->data.data32[0])

/* XdndDrop */
#define XDND_DROP_SOURCE_WIN(e)         ((e)->data.data32[0])

#endif
