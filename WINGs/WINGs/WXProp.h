/*
 *  WXProp - typed X11 property and ClientMessage helpers
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  All format-32 property data crosses this interface as fixed-width
 *  integers (uint32_t / int32_t), matching the X wire protocol and xcb's
 *  native types. Callers never see the Xlib long[] representation and
 *  never call XFree() on returned buffers, arrays are wmalloc()'d copies
 *  to be released with wfree().
 *
 *  The Xlib implementation lives in WINGs/wxprop.c. That file is the
 *  single point to rewrite for an xcb backend.
 */

#ifndef WINGS_WXPROP_H
#define WINGS_WXPROP_H

#include <stdbool.h>
#include <stdint.h>
#include <xcb/xcb.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Split-phase request/reply ---------------------------------------
 *
 * WXReq* queues a GetProperty request without blocking and returns an
 * opaque cookie. WXRcv* blocks for that cookie's reply, validates
 * format/count, and decodes into the caller's output. Issuing N WXReq*
 * calls followed by one WXFlush() and N WXRcv* calls in the same order
 * collapses N round-trips into one.
 *
 * The synchronous WXGet* readers below are implemented as
 * WXRcv*(dpy, WXReq*(...), ...) and remain the convenient choice for
 * single-property reads.
 *
 * Every issued cookie must eventually be passed to a WXRcv* or to
 * WXDiscard(), otherwise the reply stays cached for the lifetime of the
 * connection.
 */

typedef struct { unsigned int seq; } WXCookie;

WXCookie WXReqFormat32(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, xcb_atom_t type, size_t max);
WXCookie WXReqString8 (xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, xcb_atom_t type);
WXCookie WXReqExists  (xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop);

void WXFlush  (xcb_connection_t *dpy);
void WXDiscard(xcb_connection_t *dpy, WXCookie ck);

bool      WXRcvCardinal      (xcb_connection_t *dpy, WXCookie ck, uint32_t *out);
bool      WXRcvCardinalSigned(xcb_connection_t *dpy, WXCookie ck, int32_t  *out);
bool      WXRcvCardinalN     (xcb_connection_t *dpy, WXCookie ck, uint32_t *out, size_t n);
uint32_t *WXRcvCardinalList  (xcb_connection_t *dpy, WXCookie ck, size_t *count);
bool      WXRcvAtom          (xcb_connection_t *dpy, WXCookie ck, xcb_atom_t *out);
xcb_atom_t     *WXRcvAtomList      (xcb_connection_t *dpy, WXCookie ck, size_t *count);
bool      WXRcvWindow        (xcb_connection_t *dpy, WXCookie ck, xcb_window_t *out);
xcb_window_t   *WXRcvWindowList    (xcb_connection_t *dpy, WXCookie ck, size_t *count);
bool      WXRcvFormat32N     (xcb_connection_t *dpy, WXCookie ck, uint32_t *out, size_t n);
uint32_t *WXRcvFormat32List  (xcb_connection_t *dpy, WXCookie ck, size_t *count);
char     *WXRcvString        (xcb_connection_t *dpy, WXCookie ck, size_t *len);
bool      WXRcvExists        (xcb_connection_t *dpy, WXCookie ck);

/* --- Window attributes / geometry (split-phase) -----------------------
 *
 * XGetWindowAttributes() is one Xlib call but two protocol requests
 * (GetWindowAttributes + GetGeometry, since border_width/depth/x/y/width/
 * height come from the latter). Expose both so they can be batched.
 */

typedef struct {
    int      override_redirect;
    int      map_state;        /* IsUnmapped/IsUnviewable/IsViewable */
    int      window_class;     /* InputOutput/InputOnly */
    uint32_t visual;           /* VisualID, resolve with wXVisualTypeForID() */
    uint32_t colormap;
    uint32_t all_event_masks;
    uint32_t your_event_mask;
} WXWindowAttrReply;

typedef struct {
    xcb_window_t   root;
    int16_t  x, y;
    uint16_t width, height;
    uint16_t border_width;
    uint8_t  depth;
} WXGeometry;

WXCookie WXReqWindowAttributes(xcb_connection_t *dpy, xcb_window_t w);
WXCookie WXReqGeometry        (xcb_connection_t *dpy, xcb_window_t w);
bool     WXRcvWindowAttributes(xcb_connection_t *dpy, WXCookie ck, WXWindowAttrReply *out);
bool     WXRcvGeometry        (xcb_connection_t *dpy, WXCookie ck, WXGeometry *out);

/* --- Readers (synchronous) -------------------------------------------- */

bool      WXGetCardinal      (xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, uint32_t *out);
bool      WXGetCardinalSigned(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, int32_t  *out);
bool      WXGetCardinalN     (xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop,
                              uint32_t *out, size_t n);
uint32_t *WXGetCardinalList  (xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop,
                              size_t max, size_t *count);

bool      WXGetAtom          (xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, xcb_atom_t *out);
xcb_atom_t     *WXGetAtomList      (xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop,
                              size_t max, size_t *count);

bool      WXGetWindow        (xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, xcb_window_t *out);
xcb_window_t   *WXGetWindowList    (xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop,
                              size_t max, size_t *count);

char     *WXGetUTF8String    (xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, size_t *len);
char     *WXGetLatin1String  (xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, size_t *len);

bool      WXPropExists       (xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop);

/* Generic format-32 reader for properties whose type is not one of the
 * predefined XA_* atoms (e.g. WM_STATE, _GNUSTEP_WM_ATTR). */
bool      WXGetFormat32N     (xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, xcb_atom_t type,
                              uint32_t *out, size_t n);

/* --- Writers ---------------------------------------------------------- */

void WXSetCardinal    (xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, uint32_t  v);
void WXSetCardinalN   (xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop,
                       const uint32_t *v, size_t n);
void WXSetAtom        (xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, xcb_atom_t v);
void WXSetAtomN       (xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop,
                       const xcb_atom_t *v, size_t n);
void WXSetWindow      (xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, xcb_window_t v);
void WXSetWindowN     (xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop,
                       const xcb_window_t *v, size_t n);
void WXSetUTF8String  (xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop,
                       const char *s, size_t len);
void WXSetLatin1String(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop,
                       const char *s, size_t len);

/* Generic writers for properties whose type is not one of the predefined
 * XA_* atoms. */
void WXSetFormat32N   (xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, xcb_atom_t type,
                       const uint32_t *v, size_t n);
void WXSetFormat8     (xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, xcb_atom_t type,
                       const unsigned char *data, size_t len);

void WXDeleteProperty (xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop);

/* --- ClientMessage ---------------------------------------------------- */

void WXSendClientMessage(xcb_connection_t *dpy, xcb_window_t dest, long event_mask,
                         xcb_window_t about, xcb_atom_t message_type,
                         long l0, long l1, long l2, long l3, long l4);

/* --- Selection -------------------------------------------------------- */

/* Reply to a SelectionRequest with a SelectionNotify carrying the given
 * property (None to refuse). Inline so selection owners can use it
 * without pulling in libWINGs. The union zeroes the full 32 wire
 * bytes: xcb_send_event copies 32 bytes and the typed struct is only
 * 24. */
static inline void WXSendSelectionNotify(xcb_connection_t *dpy,
                                         const xcb_selection_request_event_t *req,
                                         xcb_atom_t property)
{
	union {
		xcb_selection_notify_event_t ev;
		char buf[32];
	} u;

	memset(&u, 0, sizeof(u));
	u.ev.response_type = XCB_SELECTION_NOTIFY;
	u.ev.requestor = req->requestor;
	u.ev.selection = req->selection;
	u.ev.target = req->target;
	u.ev.property = property;
	u.ev.time = req->time;
	xcb_send_event(dpy, 0, req->requestor, 0, u.buf);
	xcb_flush(dpy);
}

/* --- XDND --------------------------------------------------------------
 *
 * In-place percent-decode of a file:// URI path as delivered over an
 * XDND text/uri-list transfer. Inline so the dock target in src/, the
 * standalone wmiv source/target, and any future caller share one copy
 * without a link dependency. */
static inline int WXDndHexNibble(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return -1;
}

static inline void WXDndDecodeURI(char *uri)
{
	char *last;
	int hi, lo;

	if (uri[0] == '\0' || uri[1] == '\0')
		return;

	last = uri + strlen(uri);
	while (uri < last - 2) {
		if (*uri == '%') {
			hi = WXDndHexNibble((unsigned char)uri[1]);
			lo = WXDndHexNibble((unsigned char)uri[2]);
			if (hi < 0 || lo < 0)
				break;
			*uri = (char)((hi << 4) | lo);
			memmove(uri + 1, uri + 3, last - (uri + 2));
			last -= 2;
		}
		uri++;
	}
}

/* --- Legacy WM command ------------------------------------------------ */

/* The xcb-native _WINDOWMAKER_COMMAND fallback sender for the standalone
 * tools is WXSendWmakerCommandXC() in WXCore.h. The retired Xlib-shaped
 * twin (once setstyle's fallback) packed the same format-8 ClientMessage
 * over an XEvent. */

#ifdef __cplusplus
}
#endif

#endif /* WINGS_WXPROP_H */
