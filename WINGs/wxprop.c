/*
 *  WXProp - typed X11 property and ClientMessage helpers (xcb backend)
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
 *
 *  All requests go through the caller's xcb connection and share its
 *  single output buffer and sequence numbering, request ordering
 *  with any other user of the connection is inherent.
 *
 *  Readers come in two layers. WXReq* queues a GetProperty request and
 *  returns an opaque WXCookie without blocking, WXRcv* blocks for that
 *  cookie's reply, validates format/count, and decodes into the caller's
 *  output. The synchronous WXGet* convenience wrappers compose the two.
 *  Replies are requested with the error pointer set to NULL: a BadWindow
 *  on a vanished client is consumed by xcb and the helper simply returns
 *  false/NULL. Writers, delete and send_event are reply-less and
 *  therefore asynchronous, an error arrives on the event stream as
 *  a response_type 0 event for the connection's error hook.
 */

#include "wconfig.h"

#include <WINGs/WXCore.h>
#include <xcb/xproto.h>
#include <stdlib.h>
#include <string.h>

#include <WINGs/WUtil.h>
#include <WINGs/WXProp.h>

/* The X protocol caps GetProperty.long_length at 2^32-1 quads, but the
 * server's maximum-request-size makes anything past a few MiB pointless.
 * Use a generous default that still bounds hostile properties. */
#define WX_MAX_PROP_QUADS 0x00400000u   /* 4 Mi 32-bit units = 16 MiB */

static inline xcb_connection_t *wx_conn(xcb_connection_t *dpy)
{
	return dpy;
}

static inline xcb_get_property_cookie_t wx_xck(WXCookie ck)
{
	xcb_get_property_cookie_t xck = { ck.seq };
	return xck;
}

static WXCookie wx_req(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop,
		       xcb_atom_t type, uint32_t len)
{
	xcb_get_property_cookie_t xck;
	WXCookie ck;

	xck = xcb_get_property(dpy, 0, w, prop, type, 0, len);
	ck.seq = xck.sequence;
	return ck;
}

WXCookie WXReqFormat32(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, xcb_atom_t type, size_t max)
{
	uint32_t len;

	if (max == 0 || max > WX_MAX_PROP_QUADS)
		len = WX_MAX_PROP_QUADS;
	else
		len = (uint32_t)max;
	return wx_req(dpy, w, prop, type, len);
}

WXCookie WXReqString8(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, xcb_atom_t type)
{
	return wx_req(dpy, w, prop, type, WX_MAX_PROP_QUADS);
}

WXCookie WXReqExists(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop)
{
	return wx_req(dpy, w, prop, XCB_ATOM_ANY, 0);
}

void WXFlush(xcb_connection_t *dpy)
{
	xcb_flush(wx_conn(dpy));
}

void WXDiscard(xcb_connection_t *dpy, WXCookie ck)
{
	xcb_discard_reply(wx_conn(dpy), ck.seq);
}

/* ---- reply half: validating fetch ----------------------------------- */

/*
 * Collect the reply for a cookie and validate format/count. On success
 * returns the xcb reply (caller in this file free()s it) and sets *nitems.
 * Returns NULL if the property is absent, the server reported an error,
 * or the actual format/count do not match what was requested.
 *
 * The required type was already supplied to xcb_get_property(), so when it
 * does not match, the server returns the actual type and format with an
 * empty value and bytes_after set to the full length. Reject that case
 * explicitly so string readers (which pass min_items 0) still see NULL on
 * a wrong-type property instead of an empty string.
 */
static xcb_get_property_reply_t *wx_rcv_raw(xcb_connection_t *dpy, WXCookie ck,
					    int req_fmt, uint32_t min_items,
					    uint32_t *nitems)
{
	xcb_get_property_reply_t *r;
	uint32_t n;

	r = xcb_get_property_reply(wx_conn(dpy), wx_xck(ck), NULL);
	if (!r)
		return NULL;

	if (r->type == XCB_ATOM_NONE ||
	    (req_fmt != 0 && r->format != req_fmt)) {
		free(r);
		return NULL;
	}

	if (r->format == 32)
		n = (uint32_t)(xcb_get_property_value_length(r) / 4);
	else if (r->format == 16)
		n = (uint32_t)(xcb_get_property_value_length(r) / 2);
	else
		n = (uint32_t)xcb_get_property_value_length(r);

	if (n < min_items || (n == 0 && r->bytes_after > 0)) {
		free(r);
		return NULL;
	}

	*nitems = n;
	return r;
}

/* ---- reply half: format-32 ------------------------------------------ */

bool WXRcvFormat32N(xcb_connection_t *dpy, WXCookie ck, uint32_t *out, size_t n)
{
	xcb_get_property_reply_t *r;
	uint32_t nitems;

	/* keep the min_items check and the copy length in step, a size_t n
	 * larger than the request cap cannot have been satisfied anyway */
	if (n > WX_MAX_PROP_QUADS) {
		WXDiscard(dpy, ck);
		return false;
	}

	r = wx_rcv_raw(dpy, ck, 32, (uint32_t)n, &nitems);
	if (!r)
		return false;

	memcpy(out, xcb_get_property_value(r), n * sizeof(uint32_t));
	free(r);
	return true;
}

bool WXRcvCardinal(xcb_connection_t *dpy, WXCookie ck, uint32_t *out)
{
	return WXRcvFormat32N(dpy, ck, out, 1);
}

bool WXRcvCardinalSigned(xcb_connection_t *dpy, WXCookie ck, int32_t *out)
{
	return WXRcvFormat32N(dpy, ck, (uint32_t *)out, 1);
}

bool WXRcvCardinalN(xcb_connection_t *dpy, WXCookie ck, uint32_t *out, size_t n)
{
	return WXRcvFormat32N(dpy, ck, out, n);
}

uint32_t *WXRcvFormat32List(xcb_connection_t *dpy, WXCookie ck, size_t *count)
{
	xcb_get_property_reply_t *r;
	uint32_t nitems;
	uint32_t *dst;

	r = wx_rcv_raw(dpy, ck, 32, 1, &nitems);
	if (!r)
		return NULL;

	dst = wmalloc(sizeof(uint32_t) * (nitems ? nitems : 1));
	memcpy(dst, xcb_get_property_value(r), nitems * sizeof(uint32_t));
	free(r);

	*count = nitems;
	return dst;
}

uint32_t *WXRcvCardinalList(xcb_connection_t *dpy, WXCookie ck, size_t *count)
{
	return WXRcvFormat32List(dpy, ck, count);
}

bool WXRcvAtom(xcb_connection_t *dpy, WXCookie ck, xcb_atom_t *out)
{
	uint32_t v;

	if (!WXRcvFormat32N(dpy, ck, &v, 1))
		return false;
	*out = (xcb_atom_t)v;
	return true;
}

xcb_atom_t *WXRcvAtomList(xcb_connection_t *dpy, WXCookie ck, size_t *count)
{
	xcb_get_property_reply_t *r;
	uint32_t nitems, i;
	const uint32_t *src;
	xcb_atom_t *dst;

	r = wx_rcv_raw(dpy, ck, 32, 1, &nitems);
	if (!r)
		return NULL;

	dst = wmalloc(sizeof(xcb_atom_t) * (nitems ? nitems : 1));
	src = xcb_get_property_value(r);
	for (i = 0; i < nitems; i++)
		dst[i] = (xcb_atom_t)src[i];
	free(r);

	*count = nitems;
	return dst;
}

bool WXRcvWindow(xcb_connection_t *dpy, WXCookie ck, xcb_window_t *out)
{
	uint32_t v;

	if (!WXRcvFormat32N(dpy, ck, &v, 1))
		return false;
	*out = (xcb_window_t)v;
	return true;
}

xcb_window_t *WXRcvWindowList(xcb_connection_t *dpy, WXCookie ck, size_t *count)
{
	xcb_get_property_reply_t *r;
	uint32_t nitems, i;
	const uint32_t *src;
	xcb_window_t *dst;

	r = wx_rcv_raw(dpy, ck, 32, 1, &nitems);
	if (!r)
		return NULL;

	dst = wmalloc(sizeof(xcb_window_t) * (nitems ? nitems : 1));
	src = xcb_get_property_value(r);
	for (i = 0; i < nitems; i++)
		dst[i] = (xcb_window_t)src[i];
	free(r);

	*count = nitems;
	return dst;
}

/* ---- reply half: format-8 strings ----------------------------------- */

char *WXRcvString(xcb_connection_t *dpy, WXCookie ck, size_t *len)
{
	xcb_get_property_reply_t *r;
	uint32_t nitems;
	char *dst;

	r = wx_rcv_raw(dpy, ck, 8, 0, &nitems);
	if (!r)
		return NULL;

	dst = wmalloc((size_t)nitems + 1);
	if (nitems)
		memcpy(dst, xcb_get_property_value(r), nitems);
	dst[nitems] = '\0';
	free(r);

	if (len)
		*len = nitems;
	return dst;
}

bool WXRcvExists(xcb_connection_t *dpy, WXCookie ck)
{
	xcb_get_property_reply_t *r;
	bool exists;

	r = xcb_get_property_reply(wx_conn(dpy), wx_xck(ck), NULL);
	if (!r)
		return false;
	exists = (r->type != XCB_ATOM_NONE);
	free(r);
	return exists;
}

/* ---- window attributes / geometry ----------------------------------- */

WXCookie WXReqWindowAttributes(xcb_connection_t *dpy, xcb_window_t w)
{
	xcb_get_window_attributes_cookie_t xck =
		xcb_get_window_attributes(wx_conn(dpy), (xcb_window_t)w);
	WXCookie ck;
	ck.seq = xck.sequence;
	return ck;
}

WXCookie WXReqGeometry(xcb_connection_t *dpy, xcb_window_t w)
{
	xcb_get_geometry_cookie_t xck =
		xcb_get_geometry(wx_conn(dpy), (xcb_drawable_t)w);
	WXCookie ck;
	ck.seq = xck.sequence;
	return ck;
}

bool WXRcvWindowAttributes(xcb_connection_t *dpy, WXCookie ck, WXWindowAttrReply *out)
{
	xcb_get_window_attributes_cookie_t xck;
	xcb_get_window_attributes_reply_t *r;

	xck.sequence = ck.seq;
	r = xcb_get_window_attributes_reply(wx_conn(dpy), xck, NULL);
	if (!r)
		return false;

	out->override_redirect = r->override_redirect;
	out->map_state         = r->map_state;
	out->window_class      = r->_class;
	out->visual            = r->visual;
	out->colormap          = r->colormap;
	out->all_event_masks   = r->all_event_masks;
	out->your_event_mask   = r->your_event_mask;
	free(r);
	return true;
}

bool WXRcvGeometry(xcb_connection_t *dpy, WXCookie ck, WXGeometry *out)
{
	xcb_get_geometry_cookie_t xck;
	xcb_get_geometry_reply_t *r;

	xck.sequence = ck.seq;
	r = xcb_get_geometry_reply(wx_conn(dpy), xck, NULL);
	if (!r)
		return false;

	out->root         = r->root;
	out->x            = r->x;
	out->y            = r->y;
	out->width        = r->width;
	out->height       = r->height;
	out->border_width = r->border_width;
	out->depth        = r->depth;
	free(r);
	return true;
}

/* ---- synchronous readers -------------------------------------------- */

bool WXGetFormat32N(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, xcb_atom_t type,
		    uint32_t *out, size_t n)
{
	return WXRcvFormat32N(dpy, WXReqFormat32(dpy, w, prop, type, n), out, n);
}

bool WXGetCardinal(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, uint32_t *out)
{
	return WXRcvCardinal(dpy, WXReqFormat32(dpy, w, prop, XCB_ATOM_CARDINAL, 1), out);
}

bool WXGetCardinalSigned(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, int32_t *out)
{
	return WXRcvCardinalSigned(dpy, WXReqFormat32(dpy, w, prop, XCB_ATOM_CARDINAL, 1), out);
}

bool WXGetCardinalN(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, uint32_t *out, size_t n)
{
	return WXRcvCardinalN(dpy, WXReqFormat32(dpy, w, prop, XCB_ATOM_CARDINAL, n), out, n);
}

uint32_t *WXGetCardinalList(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop,
			    size_t max, size_t *count)
{
	return WXRcvCardinalList(dpy, WXReqFormat32(dpy, w, prop, XCB_ATOM_CARDINAL, max), count);
}

bool WXGetAtom(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, xcb_atom_t *out)
{
	return WXRcvAtom(dpy, WXReqFormat32(dpy, w, prop, XCB_ATOM_ATOM, 1), out);
}

xcb_atom_t *WXGetAtomList(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, size_t max, size_t *count)
{
	return WXRcvAtomList(dpy, WXReqFormat32(dpy, w, prop, XCB_ATOM_ATOM, max), count);
}

bool WXGetWindow(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, xcb_window_t *out)
{
	return WXRcvWindow(dpy, WXReqFormat32(dpy, w, prop, XCB_ATOM_WINDOW, 1), out);
}

xcb_window_t *WXGetWindowList(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, size_t max, size_t *count)
{
	return WXRcvWindowList(dpy, WXReqFormat32(dpy, w, prop, XCB_ATOM_WINDOW, max), count);
}

char *WXGetUTF8String(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, size_t *len)
{
	return WXRcvString(dpy, WXReqString8(dpy, w, prop, wx_static_atoms[W_ATOM_UTF8_STRING]), len);
}

char *WXGetLatin1String(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, size_t *len)
{
	return WXRcvString(dpy, WXReqString8(dpy, w, prop, XCB_ATOM_STRING), len);
}

bool WXPropExists(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop)
{
	return WXRcvExists(dpy, WXReqExists(dpy, w, prop));
}

/* ---- format-32 writers ---------------------------------------------- */

static inline void wx_set32(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, xcb_atom_t type,
			    const uint32_t *v, size_t n)
{
	xcb_change_property(wx_conn(dpy), XCB_PROP_MODE_REPLACE,
			    (xcb_window_t)w, (xcb_atom_t)prop,
			    (xcb_atom_t)type, 32, (uint32_t)n, v);
}

void WXSetCardinal(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, uint32_t v)
{
	wx_set32(dpy, w, prop, XCB_ATOM_CARDINAL, &v, 1);
}

void WXSetCardinalN(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, const uint32_t *v, size_t n)
{
	wx_set32(dpy, w, prop, XCB_ATOM_CARDINAL, v, n);
}

void WXSetFormat32N(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, xcb_atom_t type,
		    const uint32_t *v, size_t n)
{
	wx_set32(dpy, w, prop, type, v, n);
}

void WXSetAtom(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, xcb_atom_t v)
{
	uint32_t a = (uint32_t)v;
	wx_set32(dpy, w, prop, XCB_ATOM_ATOM, &a, 1);
}

void WXSetAtomN(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, const xcb_atom_t *v, size_t n)
{
	uint32_t *buf;
	size_t i;

	buf = wmalloc(sizeof(uint32_t) * (n ? n : 1));
	for (i = 0; i < n; i++)
		buf[i] = (uint32_t)v[i];
	wx_set32(dpy, w, prop, XCB_ATOM_ATOM, buf, n);
	wfree(buf);
}

void WXSetWindow(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, xcb_window_t v)
{
	uint32_t win = (uint32_t)v;
	wx_set32(dpy, w, prop, XCB_ATOM_WINDOW, &win, 1);
}

void WXSetWindowN(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, const xcb_window_t *v, size_t n)
{
	uint32_t *buf;
	size_t i;

	buf = wmalloc(sizeof(uint32_t) * (n ? n : 1));
	for (i = 0; i < n; i++)
		buf[i] = (uint32_t)v[i];
	wx_set32(dpy, w, prop, XCB_ATOM_WINDOW, buf, n);
	wfree(buf);
}

void WXSetUTF8String(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, const char *s, size_t len)
{
	xcb_change_property(wx_conn(dpy), XCB_PROP_MODE_REPLACE,
			    (xcb_window_t)w, (xcb_atom_t)prop,
			    wx_static_atoms[W_ATOM_UTF8_STRING], 8, (uint32_t)len, s);
}

void WXSetLatin1String(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, const char *s, size_t len)
{
	xcb_change_property(wx_conn(dpy), XCB_PROP_MODE_REPLACE,
			    (xcb_window_t)w, (xcb_atom_t)prop,
			    XCB_ATOM_STRING, 8, (uint32_t)len, s);
}

void WXSetFormat8(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, xcb_atom_t type,
		  const unsigned char *data, size_t len)
{
	xcb_change_property(wx_conn(dpy), XCB_PROP_MODE_REPLACE,
			    (xcb_window_t)w, (xcb_atom_t)prop,
			    (xcb_atom_t)type, 8, (uint32_t)len, data);
}

void WXDeleteProperty(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop)
{
	xcb_delete_property(wx_conn(dpy), (xcb_window_t)w, (xcb_atom_t)prop);
}

/* ---- ClientMessage --------------------------------------------------- */

void WXSendClientMessage(xcb_connection_t *dpy, xcb_window_t dest, long event_mask,
			 xcb_window_t about, xcb_atom_t message_type,
			 long l0, long l1, long l2, long l3, long l4)
{
	xcb_client_message_event_t ev;

	memset(&ev, 0, sizeof(ev));
	ev.response_type = XCB_CLIENT_MESSAGE;
	ev.format = 32;
	ev.window = (xcb_window_t)about;
	ev.type = (xcb_atom_t)message_type;
	ev.data.data32[0] = (uint32_t)l0;
	ev.data.data32[1] = (uint32_t)l1;
	ev.data.data32[2] = (uint32_t)l2;
	ev.data.data32[3] = (uint32_t)l3;
	ev.data.data32[4] = (uint32_t)l4;

	WXSendEvent32(wx_conn(dpy), &ev, sizeof(ev), 0, (xcb_window_t)dest,
		      (uint32_t)event_mask);
}
