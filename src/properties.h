/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2009-2026 Window Maker Team
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


#ifndef WMPROPERTIES_H_
#define WMPROPERTIES_H_

#include "GNUstep.h"
#include <WINGs/WXHints.h>

void PropGetProtocols(xcb_window_t window, WProtocols *prots);
int PropGetWMClass(xcb_window_t window, char **wm_class, char **wm_instance);
int PropGetGNUstepWMAttr(xcb_window_t window, GNUstepWMAttributes **attr);

void PropSetWMakerProtocols(xcb_window_t root);
void PropCleanUp(xcb_window_t root);
void PropSetIconTileHint(WScreen *scr, RImage *image);

/* Split-phase variants for batching: caller issues the matching WXReq*
 * (see wManagePrefetch in window.c) and passes the cookie here. */
int    PropRcvWindowState  (WXCookie ck);
int    PropRcvNormalHints  (WXCookie ck, WXSizeHints *size_hints, int *pre_iccm);
void   PropRcvProtocols    (WXCookie ck, WProtocols *prots);
int    PropRcvWMClass      (WXCookie ck, char **wm_class, char **wm_instance);
int    PropRcvGNUstepWMAttr(WXCookie ck, GNUstepWMAttributes **attr);
xcb_window_t PropRcvClientLeader (WXCookie ck);

#endif
