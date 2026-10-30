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

#include "wconfig.h"

#include <string.h>
#include <stdlib.h>

#include <WINGs/WXProp.h>

#include "WindowMaker.h"
#include "window.h"
#include "GNUstep.h"
#include "properties.h"
#include "wmspec.h"


int PropRcvNormalHints(WXCookie ck, WXSizeHints *size_hints, int *pre_iccm)
{
	uint32_t supplied_hints;

	if (!WXRcvNormalHints(wdisp->conn, ck, size_hints, &supplied_hints)) {
		return false;
	}
	if (supplied_hints == (XCB_ICCCM_SIZE_HINT_US_POSITION | XCB_ICCCM_SIZE_HINT_US_SIZE | XCB_ICCCM_SIZE_HINT_P_POSITION | XCB_ICCCM_SIZE_HINT_P_SIZE | XCB_ICCCM_SIZE_HINT_P_MIN_SIZE | XCB_ICCCM_SIZE_HINT_P_MAX_SIZE
			       | XCB_ICCCM_SIZE_HINT_P_RESIZE_INC | XCB_ICCCM_SIZE_HINT_P_ASPECT)) {
		*pre_iccm = 1;
	} else {
		*pre_iccm = 0;
	}
	return true;
}

int PropRcvWMClass(WXCookie ck, char **wm_class, char **wm_instance)
{
	char *inst, *cls;

	if (!WXRcvWMClass(wdisp->conn, ck, &inst, &cls)) {
		*wm_class = (char *)wintern("default");
		*wm_instance = (char *)wintern("default");
		return false;
	}
	*wm_instance = (char *)wintern(inst);
	*wm_class = (char *)wintern(cls);
	wfree(inst);
	wfree(cls);
	return true;
}

int PropGetWMClass(xcb_window_t window, char **wm_class, char **wm_instance)
{
	return PropRcvWMClass(WXReqWMClass(wdisp->conn, window), wm_class, wm_instance);
}

void PropRcvProtocols(WXCookie ck, WProtocols *prots)
{
	xcb_atom_t *protocols;
	size_t count, i;

	memset(prots, 0, sizeof(WProtocols));
	protocols = WXRcvWMProtocols(wdisp->conn, ck, &count);
	if (!protocols)
		return;

	for (i = 0; i < count; i++) {
		if (protocols[i] == wdisp->atom.wm.take_focus)
			prots->TAKE_FOCUS = 1;
		else if (protocols[i] == wdisp->atom.wm.delete_window)
			prots->DELETE_WINDOW = 1;
		else if (protocols[i] == wdisp->atom.wm.save_yourself)
			prots->SAVE_YOURSELF = 1;
		else if (wNETWMIsPingProtocol(protocols[i]))
			prots->NET_WM_PING = 1;
		else if (wNETWMIsSyncRequestProtocol(protocols[i]))
			prots->NET_WM_SYNC_REQUEST = 1;
		else if (protocols[i] == wdisp->atom.gnustep.wm_miniaturize_window)
			prots->MINIATURIZE_WINDOW = 1;
		else if (protocols[i] == wdisp->atom.gnustep.wm_hide_app)
			prots->HIDE_APP = 1;
	}
	wfree(protocols);
}

void PropGetProtocols(xcb_window_t window, WProtocols *prots)
{
	PropRcvProtocols(WXReqWMProtocols(wdisp->conn, window), prots);
}

int PropRcvGNUstepWMAttr(WXCookie ck, GNUstepWMAttributes **attr)
{
	uint32_t data[9];

	if (!WXRcvFormat32N(wdisp->conn, ck, data, 9))
		return false;

	*attr = wmalloc(sizeof(GNUstepWMAttributes));
	(*attr)->flags = data[0];
	(*attr)->window_style = data[1];
	(*attr)->window_level = data[2];
	(*attr)->reserved = data[3];
	(*attr)->miniaturize_pixmap = data[4];
	(*attr)->close_pixmap = data[5];
	(*attr)->miniaturize_mask = data[6];
	(*attr)->close_mask = data[7];
	(*attr)->extra_flags = data[8];

	return true;
}

int PropGetGNUstepWMAttr(xcb_window_t window, GNUstepWMAttributes **attr)
{
	return PropRcvGNUstepWMAttr(WXReqFormat32(wdisp->conn, window,
				    wdisp->atom.gnustep.wm_attr,
				    wdisp->atom.gnustep.wm_attr, 9), attr);
}

void PropSetWMakerProtocols(xcb_window_t root)
{
	xcb_atom_t protocols[3];
	int count = 0;

	protocols[count++] = wdisp->atom.wmaker.menu;
	protocols[count++] = wdisp->atom.wmaker.wm_function;
	protocols[count++] = wdisp->atom.wmaker.noticeboard;

	WXSetAtomN(wdisp->conn, root, wdisp->atom.wmaker.wm_protocols, protocols, count);
}

void PropSetIconTileHint(WScreen * scr, RImage * image)
{
	static xcb_atom_t imageAtom = 0;
	unsigned char *tmp;
	int x, y;

	if (scr->info_window == XCB_NONE)
		return;

	if (!imageAtom) {
		/*
		 * WIDTH, HEIGHT (16 bits, MSB First)
		 * array of R,G,B,A bytes
		 */
		imageAtom = wx_static_atoms[W_ATOM_RGBA_IMAGE];
	}

	tmp = malloc(image->width * image->height * 4 + 4);
	if (!tmp) {
		wwarning("could not allocate memory to set _WINDOWMAKER_ICON_TILE hint");
		return;
	}

	tmp[0] = image->width >> 8;
	tmp[1] = image->width & 0xff;
	tmp[2] = image->height >> 8;
	tmp[3] = image->height & 0xff;

	if (image->format == RRGBAFormat) {
		memcpy(&tmp[4], image->data, image->width * image->height * 4);
	} else {
		unsigned char *ptr = tmp + 4;
		unsigned char *src = (unsigned char *)image->data;

		for (y = 0; y < image->height; y++) {
			for (x = 0; x < image->width; x++) {
				*ptr++ = *src++;
				*ptr++ = *src++;
				*ptr++ = *src++;
				*ptr++ = 255;
			}
		}
	}

	WXSetFormat8(wdisp->conn, scr->info_window, wdisp->atom.wmaker.icon_tile,
		     imageAtom, tmp, image->width * image->height * 4 + 4);
	wfree(tmp);

}

xcb_window_t PropRcvClientLeader(WXCookie ck)
{
	xcb_window_t leader;

	if (!WXRcvWindow(wdisp->conn, ck, &leader))
		return XCB_NONE;

	return leader;
}

int PropRcvWindowState(WXCookie ck)
{
	uint32_t state;

	if (!WXRcvFormat32N(wdisp->conn, ck, &state, 1))
		return -1;

	return (int)state;
}

void PropCleanUp(xcb_window_t root)
{
	WXDeleteProperty(wdisp->conn, root, wdisp->atom.wmaker.wm_protocols);
	WXDeleteProperty(wdisp->conn, root, wdisp->atom.wmaker.noticeboard);
	WXDeleteProperty(wdisp->conn, root, XCB_ATOM_WM_ICON_SIZE);
}
