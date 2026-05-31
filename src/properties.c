/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
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
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "wconfig.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <string.h>
#include <stdlib.h>

#include <WINGs/WXProp.h>

#include "WindowMaker.h"
#include "window.h"
#include "GNUstep.h"
#include "properties.h"


int PropRcvNormalHints(WXCookie ck, WXSizeHints *size_hints, int *pre_iccm)
{
	long supplied_hints;

	if (!WXRcvNormalHints(dpy, ck, size_hints, &supplied_hints)) {
		return False;
	}
	if (supplied_hints == (USPosition | USSize | PPosition | PSize | PMinSize | PMaxSize
			       | PResizeInc | PAspect)) {
		*pre_iccm = 1;
	} else {
		*pre_iccm = 0;
	}
	return True;
}

int PropGetNormalHints(Window window, WXSizeHints *size_hints, int *pre_iccm)
{
	return PropRcvNormalHints(WXReqNormalHints(dpy, window), size_hints, pre_iccm);
}

int PropRcvWMClass(WXCookie ck, char **wm_class, char **wm_instance)
{
	char *inst, *cls;

	if (!WXRcvWMClass(dpy, ck, &inst, &cls)) {
		*wm_class = (char *)wintern("default");
		*wm_instance = (char *)wintern("default");
		return False;
	}
	*wm_instance = (char *)wintern(inst);
	*wm_class = (char *)wintern(cls);
	wfree(inst);
	wfree(cls);
	return True;
}

int PropGetWMClass(Window window, char **wm_class, char **wm_instance)
{
	return PropRcvWMClass(WXReqWMClass(dpy, window), wm_class, wm_instance);
}

void PropRcvProtocols(WXCookie ck, WProtocols *prots)
{
	Atom *protocols;
	size_t count, i;

	memset(prots, 0, sizeof(WProtocols));
	protocols = WXRcvWMProtocols(dpy, ck, &count);
	if (!protocols)
		return;

	for (i = 0; i < count; i++) {
		if (protocols[i] == w_global.atom.wm.take_focus)
			prots->TAKE_FOCUS = 1;
		else if (protocols[i] == w_global.atom.wm.delete_window)
			prots->DELETE_WINDOW = 1;
		else if (protocols[i] == w_global.atom.wm.save_yourself)
			prots->SAVE_YOURSELF = 1;
		else if (protocols[i] == w_global.atom.gnustep.wm_miniaturize_window)
			prots->MINIATURIZE_WINDOW = 1;
	}
	wfree(protocols);
}

void PropGetProtocols(Window window, WProtocols *prots)
{
	PropRcvProtocols(WXReqWMProtocols(dpy, window), prots);
}

int PropRcvGNUstepWMAttr(WXCookie ck, GNUstepWMAttributes **attr)
{
	uint32_t data[9];

	if (!WXRcvFormat32N(dpy, ck, data, 9))
		return False;

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

	return True;
}

int PropGetGNUstepWMAttr(Window window, GNUstepWMAttributes **attr)
{
	return PropRcvGNUstepWMAttr(WXReqFormat32(dpy, window,
				    w_global.atom.gnustep.wm_attr,
				    w_global.atom.gnustep.wm_attr, 9), attr);
}

void PropSetWMakerProtocols(Window root)
{
	Atom protocols[3];
	int count = 0;

	protocols[count++] = w_global.atom.wmaker.menu;
	protocols[count++] = w_global.atom.wmaker.wm_function;
	protocols[count++] = w_global.atom.wmaker.noticeboard;

	WXSetAtomN(dpy, root, w_global.atom.wmaker.wm_protocols, protocols, count);
}

void PropSetIconTileHint(WScreen * scr, RImage * image)
{
	static Atom imageAtom = 0;
	unsigned char *tmp;
	int x, y;

	if (scr->info_window == None)
		return;

	if (!imageAtom) {
		/*
		 * WIDTH, HEIGHT (16 bits, MSB First)
		 * array of R,G,B,A bytes
		 */
		imageAtom = XInternAtom(dpy, "_RGBA_IMAGE", False);
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

	WXSetFormat8(dpy, scr->info_window, w_global.atom.wmaker.icon_tile,
		     imageAtom, tmp, image->width * image->height * 4 + 4);
	wfree(tmp);

}

Window PropRcvClientLeader(WXCookie ck)
{
	Window leader;

	if (!WXRcvWindow(dpy, ck, &leader))
		return None;

	return leader;
}

Window PropGetClientLeader(Window window)
{
	return PropRcvClientLeader(WXReqFormat32(dpy, window,
				   w_global.atom.wm.client_leader, XA_WINDOW, 1));
}

int PropRcvWindowState(WXCookie ck)
{
	uint32_t state;

	if (!WXRcvFormat32N(dpy, ck, &state, 1))
		return -1;

	return (int)state;
}

int PropGetWindowState(Window window)
{
	return PropRcvWindowState(WXReqFormat32(dpy, window,
				  w_global.atom.wm.state,
				  w_global.atom.wm.state, 1));
}

void PropCleanUp(Window root)
{
	WXDeleteProperty(dpy, root, w_global.atom.wmaker.wm_protocols);
	WXDeleteProperty(dpy, root, w_global.atom.wmaker.noticeboard);
	WXDeleteProperty(dpy, root, XA_WM_ICON_SIZE);
}
