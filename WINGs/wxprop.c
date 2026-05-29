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
 *  All requests go through the xcb connection underlying the caller's
 *  Display* (XGetXCBConnection).  Xlib retains ownership of the event
 *  queue, so callers using XNextEvent/XCheckTypedEvent are unaffected.
 *  Because libX11 routes its own writes through the same xcb socket,
 *  request ordering between Xlib calls and these helpers is preserved
 *  without explicit cross-flushing.
 *
 *  Readers come in two layers.  WXReq* queues a GetProperty request and
 *  returns an opaque WXCookie without blocking, WXRcv* blocks for that
 *  cookie's reply, validates format/count, and decodes into the caller's
 *  output.  The synchronous WXGet* convenience wrappers compose the two.
 *  Replies are requested with the error pointer set to NULL: a BadWindow
 *  on a vanished client is consumed by xcb and the helper simply returns
 *  False/NULL.  Writers, delete and send_event are reply-less and
 *  therefore asynchronous, any error reaches the event stream and is
 *  dispatched through the existing Xlib error handler.
 */

#include "wconfig.h"

#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>
#include <X11/Xatom.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <stdlib.h>
#include <string.h>

#include <WINGs/WUtil.h>
#include <WINGs/WXProp.h>

/* The X protocol caps GetProperty.long_length at 2^32-1 quads, but the
 * server's maximum-request-size makes anything past a few MiB pointless.
 * Use a generous default that still bounds hostile properties. */
#define WX_MAX_PROP_QUADS 0x00400000u   /* 4 Mi 32-bit units = 16 MiB */

static inline xcb_connection_t *wx_conn(Display *dpy)
{
	return XGetXCBConnection(dpy);
}

static inline xcb_get_property_cookie_t wx_xck(WXCookie ck)
{
	xcb_get_property_cookie_t xck = { ck.seq };
	return xck;
}

static xcb_atom_t wx_utf8_string(Display *dpy)
{
	static Display *cached_dpy = NULL;
	static xcb_atom_t cached = XCB_ATOM_NONE;

	/* key the cache on the display so a second connection re-interns,
	 * libWINGs is used by tools that may open more than one */
	if (dpy != cached_dpy) {
		cached = (xcb_atom_t)XInternAtom(dpy, "UTF8_STRING", False);
		cached_dpy = dpy;
	}
	return cached;
}

/* ---- request half --------------------------------------------------- */

static WXCookie wx_req(Display *dpy, Window w, Atom prop, Atom type, uint32_t len)
{
	xcb_get_property_cookie_t xck;
	WXCookie ck;

	xck = xcb_get_property(wx_conn(dpy), 0, (xcb_window_t)w,
			       (xcb_atom_t)prop, (xcb_atom_t)type, 0, len);
	ck.seq = xck.sequence;
	return ck;
}

WXCookie WXReqFormat32(Display *dpy, Window w, Atom prop, Atom type, size_t max)
{
	uint32_t len;

	if (max == 0 || max > WX_MAX_PROP_QUADS)
		len = WX_MAX_PROP_QUADS;
	else
		len = (uint32_t)max;
	return wx_req(dpy, w, prop, type, len);
}

WXCookie WXReqString8(Display *dpy, Window w, Atom prop, Atom type)
{
	return wx_req(dpy, w, prop, type, WX_MAX_PROP_QUADS);
}

WXCookie WXReqExists(Display *dpy, Window w, Atom prop)
{
	return wx_req(dpy, w, prop, AnyPropertyType, 0);
}

void WXFlush(Display *dpy)
{
	xcb_flush(wx_conn(dpy));
}

void WXDiscard(Display *dpy, WXCookie ck)
{
	xcb_discard_reply(wx_conn(dpy), ck.seq);
}

/* ---- reply half: validating fetch ----------------------------------- */

/*
 * Collect the reply for a cookie and validate format/count.  On success
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
static xcb_get_property_reply_t *wx_rcv_raw(Display *dpy, WXCookie ck,
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

Bool WXRcvFormat32N(Display *dpy, WXCookie ck, uint32_t *out, size_t n)
{
	xcb_get_property_reply_t *r;
	uint32_t nitems;

	/* keep the min_items check and the copy length in step, a size_t n
	 * larger than the request cap cannot have been satisfied anyway */
	if (n > WX_MAX_PROP_QUADS) {
		WXDiscard(dpy, ck);
		return False;
	}

	r = wx_rcv_raw(dpy, ck, 32, (uint32_t)n, &nitems);
	if (!r)
		return False;

	memcpy(out, xcb_get_property_value(r), n * sizeof(uint32_t));
	free(r);
	return True;
}

Bool WXRcvCardinal(Display *dpy, WXCookie ck, uint32_t *out)
{
	return WXRcvFormat32N(dpy, ck, out, 1);
}

Bool WXRcvCardinalSigned(Display *dpy, WXCookie ck, int32_t *out)
{
	return WXRcvFormat32N(dpy, ck, (uint32_t *)out, 1);
}

Bool WXRcvCardinalN(Display *dpy, WXCookie ck, uint32_t *out, size_t n)
{
	return WXRcvFormat32N(dpy, ck, out, n);
}

uint32_t *WXRcvCardinalList(Display *dpy, WXCookie ck, size_t *count)
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

Bool WXRcvAtom(Display *dpy, WXCookie ck, Atom *out)
{
	uint32_t v;

	if (!WXRcvFormat32N(dpy, ck, &v, 1))
		return False;
	*out = (Atom)v;
	return True;
}

Atom *WXRcvAtomList(Display *dpy, WXCookie ck, size_t *count)
{
	xcb_get_property_reply_t *r;
	uint32_t nitems, i;
	const uint32_t *src;
	Atom *dst;

	r = wx_rcv_raw(dpy, ck, 32, 1, &nitems);
	if (!r)
		return NULL;

	dst = wmalloc(sizeof(Atom) * (nitems ? nitems : 1));
	src = xcb_get_property_value(r);
	for (i = 0; i < nitems; i++)
		dst[i] = (Atom)src[i];
	free(r);

	*count = nitems;
	return dst;
}

Bool WXRcvWindow(Display *dpy, WXCookie ck, Window *out)
{
	uint32_t v;

	if (!WXRcvFormat32N(dpy, ck, &v, 1))
		return False;
	*out = (Window)v;
	return True;
}

Window *WXRcvWindowList(Display *dpy, WXCookie ck, size_t *count)
{
	xcb_get_property_reply_t *r;
	uint32_t nitems, i;
	const uint32_t *src;
	Window *dst;

	r = wx_rcv_raw(dpy, ck, 32, 1, &nitems);
	if (!r)
		return NULL;

	dst = wmalloc(sizeof(Window) * (nitems ? nitems : 1));
	src = xcb_get_property_value(r);
	for (i = 0; i < nitems; i++)
		dst[i] = (Window)src[i];
	free(r);

	*count = nitems;
	return dst;
}

/* ---- reply half: format-8 strings ----------------------------------- */

char *WXRcvString(Display *dpy, WXCookie ck, size_t *len)
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

Bool WXRcvExists(Display *dpy, WXCookie ck)
{
	xcb_get_property_reply_t *r;
	Bool exists;

	r = xcb_get_property_reply(wx_conn(dpy), wx_xck(ck), NULL);
	if (!r)
		return False;
	exists = (r->type != XCB_ATOM_NONE);
	free(r);
	return exists;
}

/* ---- synchronous readers -------------------------------------------- */

Bool WXGetFormat32N(Display *dpy, Window w, Atom prop, Atom type,
		    uint32_t *out, size_t n)
{
	return WXRcvFormat32N(dpy, WXReqFormat32(dpy, w, prop, type, n), out, n);
}

Bool WXGetCardinal(Display *dpy, Window w, Atom prop, uint32_t *out)
{
	return WXRcvCardinal(dpy, WXReqFormat32(dpy, w, prop, XA_CARDINAL, 1), out);
}

Bool WXGetCardinalSigned(Display *dpy, Window w, Atom prop, int32_t *out)
{
	return WXRcvCardinalSigned(dpy, WXReqFormat32(dpy, w, prop, XA_CARDINAL, 1), out);
}

Bool WXGetCardinalN(Display *dpy, Window w, Atom prop, uint32_t *out, size_t n)
{
	return WXRcvCardinalN(dpy, WXReqFormat32(dpy, w, prop, XA_CARDINAL, n), out, n);
}

uint32_t *WXGetCardinalList(Display *dpy, Window w, Atom prop,
			    size_t max, size_t *count)
{
	return WXRcvCardinalList(dpy, WXReqFormat32(dpy, w, prop, XA_CARDINAL, max), count);
}

Bool WXGetAtom(Display *dpy, Window w, Atom prop, Atom *out)
{
	return WXRcvAtom(dpy, WXReqFormat32(dpy, w, prop, XA_ATOM, 1), out);
}

Atom *WXGetAtomList(Display *dpy, Window w, Atom prop, size_t max, size_t *count)
{
	return WXRcvAtomList(dpy, WXReqFormat32(dpy, w, prop, XA_ATOM, max), count);
}

Bool WXGetWindow(Display *dpy, Window w, Atom prop, Window *out)
{
	return WXRcvWindow(dpy, WXReqFormat32(dpy, w, prop, XA_WINDOW, 1), out);
}

Window *WXGetWindowList(Display *dpy, Window w, Atom prop, size_t max, size_t *count)
{
	return WXRcvWindowList(dpy, WXReqFormat32(dpy, w, prop, XA_WINDOW, max), count);
}

char *WXGetUTF8String(Display *dpy, Window w, Atom prop, size_t *len)
{
	return WXRcvString(dpy, WXReqString8(dpy, w, prop, (Atom)wx_utf8_string(dpy)), len);
}

char *WXGetLatin1String(Display *dpy, Window w, Atom prop, size_t *len)
{
	return WXRcvString(dpy, WXReqString8(dpy, w, prop, XA_STRING), len);
}

Bool WXPropExists(Display *dpy, Window w, Atom prop)
{
	return WXRcvExists(dpy, WXReqExists(dpy, w, prop));
}

/* ---- format-32 writers ---------------------------------------------- */

static inline void wx_set32(Display *dpy, Window w, Atom prop, Atom type,
			    const uint32_t *v, size_t n)
{
	xcb_change_property(wx_conn(dpy), XCB_PROP_MODE_REPLACE,
			    (xcb_window_t)w, (xcb_atom_t)prop,
			    (xcb_atom_t)type, 32, (uint32_t)n, v);
}

void WXSetCardinal(Display *dpy, Window w, Atom prop, uint32_t v)
{
	wx_set32(dpy, w, prop, XA_CARDINAL, &v, 1);
}

void WXSetCardinalN(Display *dpy, Window w, Atom prop, const uint32_t *v, size_t n)
{
	wx_set32(dpy, w, prop, XA_CARDINAL, v, n);
}

void WXSetFormat32N(Display *dpy, Window w, Atom prop, Atom type,
		    const uint32_t *v, size_t n)
{
	wx_set32(dpy, w, prop, type, v, n);
}

void WXSetAtom(Display *dpy, Window w, Atom prop, Atom v)
{
	uint32_t a = (uint32_t)v;
	wx_set32(dpy, w, prop, XA_ATOM, &a, 1);
}

void WXSetAtomN(Display *dpy, Window w, Atom prop, const Atom *v, size_t n)
{
	uint32_t *buf;
	size_t i;

	buf = wmalloc(sizeof(uint32_t) * (n ? n : 1));
	for (i = 0; i < n; i++)
		buf[i] = (uint32_t)v[i];
	wx_set32(dpy, w, prop, XA_ATOM, buf, n);
	wfree(buf);
}

void WXSetWindow(Display *dpy, Window w, Atom prop, Window v)
{
	uint32_t win = (uint32_t)v;
	wx_set32(dpy, w, prop, XA_WINDOW, &win, 1);
}

void WXSetWindowN(Display *dpy, Window w, Atom prop, const Window *v, size_t n)
{
	uint32_t *buf;
	size_t i;

	buf = wmalloc(sizeof(uint32_t) * (n ? n : 1));
	for (i = 0; i < n; i++)
		buf[i] = (uint32_t)v[i];
	wx_set32(dpy, w, prop, XA_WINDOW, buf, n);
	wfree(buf);
}

void WXSetUTF8String(Display *dpy, Window w, Atom prop, const char *s, size_t len)
{
	xcb_change_property(wx_conn(dpy), XCB_PROP_MODE_REPLACE,
			    (xcb_window_t)w, (xcb_atom_t)prop,
			    wx_utf8_string(dpy), 8, (uint32_t)len, s);
}

void WXSetLatin1String(Display *dpy, Window w, Atom prop, const char *s, size_t len)
{
	xcb_change_property(wx_conn(dpy), XCB_PROP_MODE_REPLACE,
			    (xcb_window_t)w, (xcb_atom_t)prop,
			    XCB_ATOM_STRING, 8, (uint32_t)len, s);
}

void WXSetFormat8(Display *dpy, Window w, Atom prop, Atom type,
		  const unsigned char *data, size_t len)
{
	xcb_change_property(wx_conn(dpy), XCB_PROP_MODE_REPLACE,
			    (xcb_window_t)w, (xcb_atom_t)prop,
			    (xcb_atom_t)type, 8, (uint32_t)len, data);
}

void WXDeleteProperty(Display *dpy, Window w, Atom prop)
{
	xcb_delete_property(wx_conn(dpy), (xcb_window_t)w, (xcb_atom_t)prop);
}

/* ---- ClientMessage --------------------------------------------------- */

void WXSendClientMessage(Display *dpy, Window dest, long event_mask,
			 Window about, Atom message_type,
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

	xcb_send_event(wx_conn(dpy), 0, (xcb_window_t)dest,
		       (uint32_t)event_mask, (const char *)&ev);
}
