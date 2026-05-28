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
 *  Readers issue a checked xcb_get_property and block on the reply with
 *  the error pointer set to NULL: a BadWindow on a vanished client is
 *  consumed by xcb and the helper simply returns False/NULL.  Writers,
 *  delete and send_event are reply-less and therefore asynchronous; any
 *  error reaches the event stream and is dispatched through the existing
 *  Xlib error handler, matching the previous Xlib-backend behaviour.
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

/*
 * Fetch a property and validate type/format/count.  On success returns the
 * xcb reply (caller in this file free()s it) and sets *nitems.  Returns
 * NULL if the property is absent, the server reports an error, or the
 * actual type/format do not match what was requested.
 *
 * min_items: reject results shorter than this.
 * max_items: longest read in 32-bit units; pass 0 for "as much as fits".
 */
static xcb_get_property_reply_t *wx_get_raw(Display *dpy, Window w, Atom prop,
					    Atom req_type, int req_fmt,
					    uint32_t min_items, uint32_t max_items,
					    uint32_t *nitems)
{
	xcb_connection_t *c = wx_conn(dpy);
	xcb_get_property_cookie_t ck;
	xcb_get_property_reply_t *r;
	uint32_t len, n;

	if (max_items == 0 || max_items > WX_MAX_PROP_QUADS)
		len = WX_MAX_PROP_QUADS;
	else
		len = max_items;

	ck = xcb_get_property(c, 0, (xcb_window_t)w, (xcb_atom_t)prop,
			      (xcb_atom_t)req_type, 0, len);
	r = xcb_get_property_reply(c, ck, NULL);
	if (!r)
		return NULL;

	if (r->type == XCB_ATOM_NONE ||
	    (req_type != AnyPropertyType && r->type != (xcb_atom_t)req_type) ||
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

	if (n < min_items) {
		free(r);
		return NULL;
	}

	*nitems = n;
	return r;
}

/* ---- format-32 readers ---------------------------------------------- */

Bool WXGetFormat32N(Display *dpy, Window w, Atom prop, Atom type,
		    uint32_t *out, size_t n)
{
	xcb_get_property_reply_t *r;
	uint32_t nitems;

	r = wx_get_raw(dpy, w, prop, type, 32, (uint32_t)n, (uint32_t)n, &nitems);
	if (!r)
		return False;

	memcpy(out, xcb_get_property_value(r), n * sizeof(uint32_t));
	free(r);
	return True;
}

Bool WXGetCardinal(Display *dpy, Window w, Atom prop, uint32_t *out)
{
	return WXGetFormat32N(dpy, w, prop, XA_CARDINAL, out, 1);
}

Bool WXGetCardinalSigned(Display *dpy, Window w, Atom prop, int32_t *out)
{
	return WXGetFormat32N(dpy, w, prop, XA_CARDINAL, (uint32_t *)out, 1);
}

Bool WXGetCardinalN(Display *dpy, Window w, Atom prop, uint32_t *out, size_t n)
{
	return WXGetFormat32N(dpy, w, prop, XA_CARDINAL, out, n);
}

uint32_t *WXGetCardinalList(Display *dpy, Window w, Atom prop,
			    size_t max, size_t *count)
{
	xcb_get_property_reply_t *r;
	uint32_t nitems;
	uint32_t *dst;

	r = wx_get_raw(dpy, w, prop, XA_CARDINAL, 32, 1, (uint32_t)max, &nitems);
	if (!r)
		return NULL;

	dst = wmalloc(sizeof(uint32_t) * (nitems ? nitems : 1));
	memcpy(dst, xcb_get_property_value(r), nitems * sizeof(uint32_t));
	free(r);

	*count = nitems;
	return dst;
}

Bool WXGetAtom(Display *dpy, Window w, Atom prop, Atom *out)
{
	uint32_t v;

	if (!WXGetFormat32N(dpy, w, prop, XA_ATOM, &v, 1))
		return False;
	*out = (Atom)v;
	return True;
}

Atom *WXGetAtomList(Display *dpy, Window w, Atom prop, size_t max, size_t *count)
{
	xcb_get_property_reply_t *r;
	uint32_t nitems, i;
	const uint32_t *src;
	Atom *dst;

	r = wx_get_raw(dpy, w, prop, XA_ATOM, 32, 1, (uint32_t)max, &nitems);
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

Bool WXGetWindow(Display *dpy, Window w, Atom prop, Window *out)
{
	uint32_t v;

	if (!WXGetFormat32N(dpy, w, prop, XA_WINDOW, &v, 1))
		return False;
	*out = (Window)v;
	return True;
}

Window *WXGetWindowList(Display *dpy, Window w, Atom prop, size_t max, size_t *count)
{
	xcb_get_property_reply_t *r;
	uint32_t nitems, i;
	const uint32_t *src;
	Window *dst;

	r = wx_get_raw(dpy, w, prop, XA_WINDOW, 32, 1, (uint32_t)max, &nitems);
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

/* ---- format-8 string readers ---------------------------------------- */

static char *wx_get_string8(Display *dpy, Window w, Atom prop, Atom type, size_t *len)
{
	xcb_get_property_reply_t *r;
	uint32_t nitems;
	char *dst;

	r = wx_get_raw(dpy, w, prop, type, 8, 0, 0, &nitems);
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

char *WXGetUTF8String(Display *dpy, Window w, Atom prop, size_t *len)
{
	return wx_get_string8(dpy, w, prop, (Atom)wx_utf8_string(dpy), len);
}

char *WXGetLatin1String(Display *dpy, Window w, Atom prop, size_t *len)
{
	return wx_get_string8(dpy, w, prop, XA_STRING, len);
}

Bool WXPropExists(Display *dpy, Window w, Atom prop)
{
	xcb_connection_t *c = wx_conn(dpy);
	xcb_get_property_cookie_t ck;
	xcb_get_property_reply_t *r;
	Bool exists;

	ck = xcb_get_property(c, 0, (xcb_window_t)w, (xcb_atom_t)prop,
			      XCB_GET_PROPERTY_TYPE_ANY, 0, 0);
	r = xcb_get_property_reply(c, ck, NULL);
	if (!r)
		return False;
	exists = (r->type != XCB_ATOM_NONE);
	free(r);
	return exists;
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
