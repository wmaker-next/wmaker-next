/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
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

#ifndef WMCLIENT_H_
#define WMCLIENT_H_

void wClientSetState(WWindow *wwin, int state, xcb_window_t icon_win);

void wClientRestore(WWindow *wwin);
void wClientConfigure(WWindow *wwin, const xcb_configure_request_event_t *xcre);
void wClientGetGravityOffsets(WWindow *wwin, int *ofs_x, int *ofs_y);
void wClientSendProtocol(WWindow *wwin, xcb_atom_t protocol, xcb_timestamp_t time);
void wClientKill(WWindow *wwin);
void wClientCheckProperty(WWindow *wwin, xcb_atom_t atom);


void wClientGetNormalHints(WWindow *wwin, WXWindowAttributes *wattribs,
                           bool geometry, WXCookie normal_ck,
                           int *x, int *y, unsigned *width,
                           unsigned *height);
WXCookie wClientReqColormapWindows(xcb_window_t window);
void wClientRcvColormapWindows(WWindow *wwin, WXCookie ck);
void GetColormapWindows(WWindow *wwin);

#endif
