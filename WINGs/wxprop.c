/*
 *  WXProp - typed X11 property and ClientMessage helpers (Xlib backend)
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "wconfig.h"

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <stdlib.h>
#include <string.h>

#include <WINGs/WUtil.h>
#include <WINGs/WXProp.h>

static Atom wx_utf8_string(Display *dpy)
{
	static Atom cached = None;

	if (cached == None)
		cached = XInternAtom(dpy, "UTF8_STRING", False);
	return cached;
}

/*
 * Fetch a property and validate type/format/count.  On success returns the
 * raw Xlib buffer (caller in this file XFree()s it) and sets *nitems.
 * Returns NULL if the property is absent, the server reports an error, or
 * the actual type/format do not match what was requested.
 *
 * min_items: reject results shorter than this.
 * max_items: longest read in 32-bit units; pass 0 for "as much as fits".
 */
static unsigned char *wx_get_raw(Display *dpy, Window w, Atom prop,
				 Atom req_type, int req_fmt,
				 unsigned long min_items, unsigned long max_items,
				 unsigned long *nitems)
{
	Atom type_ret;
	int fmt_ret;
	unsigned long nitems_ret, bytes_after;
	unsigned char *data = NULL;
	long len;

	if (max_items == 0 || max_items > 0x7fffffffUL)
		len = 0x7fffffffL;
	else
		len = (long)max_items;

	if (XGetWindowProperty(dpy, w, prop, 0L, len, False, req_type,
			       &type_ret, &fmt_ret, &nitems_ret, &bytes_after,
			       &data) != Success)
		return NULL;

	if (data == NULL)
		return NULL;

	if (type_ret == None ||
	    (req_type != AnyPropertyType && type_ret != req_type) ||
	    (req_fmt != 0 && fmt_ret != req_fmt) ||
	    nitems_ret < min_items) {
		XFree(data);
		return NULL;
	}

	*nitems = nitems_ret;
	return data;
}

/* ---- format-32 readers ------------------------------------------------ */

static Bool wx_get_long32_n(Display *dpy, Window w, Atom prop, Atom type,
			    long *out, size_t n)
{
	unsigned long nitems;
	unsigned char *raw;
	long *src;
	size_t i;

	raw = wx_get_raw(dpy, w, prop, type, 32, n, n, &nitems);
	if (!raw)
		return False;

	src = (long *)raw;
	for (i = 0; i < n; i++)
		out[i] = src[i];

	XFree(raw);
	return True;
}

Bool WXGetCardinal(Display *dpy, Window w, Atom prop, uint32_t *out)
{
	long v;

	if (!wx_get_long32_n(dpy, w, prop, XA_CARDINAL, &v, 1))
		return False;
	*out = (uint32_t)v;
	return True;
}

Bool WXGetCardinalSigned(Display *dpy, Window w, Atom prop, int32_t *out)
{
	long v;

	if (!wx_get_long32_n(dpy, w, prop, XA_CARDINAL, &v, 1))
		return False;
	*out = (int32_t)v;
	return True;
}

Bool WXGetFormat32N(Display *dpy, Window w, Atom prop, Atom type,
		    uint32_t *out, size_t n)
{
	unsigned long nitems;
	unsigned char *raw;
	long *src;
	size_t i;

	raw = wx_get_raw(dpy, w, prop, type, 32, n, n, &nitems);
	if (!raw)
		return False;

	src = (long *)raw;
	for (i = 0; i < n; i++)
		out[i] = (uint32_t)src[i];

	XFree(raw);
	return True;
}

Bool WXGetCardinalN(Display *dpy, Window w, Atom prop, uint32_t *out, size_t n)
{
	return WXGetFormat32N(dpy, w, prop, XA_CARDINAL, out, n);
}

uint32_t *WXGetCardinalList(Display *dpy, Window w, Atom prop,
			    size_t max, size_t *count)
{
	unsigned long nitems;
	unsigned char *raw;
	long *src;
	uint32_t *dst;
	size_t i;

	raw = wx_get_raw(dpy, w, prop, XA_CARDINAL, 32, 1, max, &nitems);
	if (!raw)
		return NULL;

	dst = wmalloc(sizeof(uint32_t) * (nitems ? nitems : 1));
	src = (long *)raw;
	for (i = 0; i < nitems; i++)
		dst[i] = (uint32_t)src[i];

	XFree(raw);
	*count = nitems;
	return dst;
}

Bool WXGetAtom(Display *dpy, Window w, Atom prop, Atom *out)
{
	long v;

	if (!wx_get_long32_n(dpy, w, prop, XA_ATOM, &v, 1))
		return False;
	*out = (Atom)(unsigned long)v;
	return True;
}

Atom *WXGetAtomList(Display *dpy, Window w, Atom prop, size_t max, size_t *count)
{
	unsigned long nitems;
	unsigned char *raw;
	long *src;
	Atom *dst;
	size_t i;

	raw = wx_get_raw(dpy, w, prop, XA_ATOM, 32, 1, max, &nitems);
	if (!raw)
		return NULL;

	dst = wmalloc(sizeof(Atom) * (nitems ? nitems : 1));
	src = (long *)raw;
	for (i = 0; i < nitems; i++)
		dst[i] = (Atom)(unsigned long)src[i];

	XFree(raw);
	*count = nitems;
	return dst;
}

Bool WXGetWindow(Display *dpy, Window w, Atom prop, Window *out)
{
	long v;

	if (!wx_get_long32_n(dpy, w, prop, XA_WINDOW, &v, 1))
		return False;
	*out = (Window)(unsigned long)v;
	return True;
}

Window *WXGetWindowList(Display *dpy, Window w, Atom prop, size_t max, size_t *count)
{
	unsigned long nitems;
	unsigned char *raw;
	long *src;
	Window *dst;
	size_t i;

	raw = wx_get_raw(dpy, w, prop, XA_WINDOW, 32, 1, max, &nitems);
	if (!raw)
		return NULL;

	dst = wmalloc(sizeof(Window) * (nitems ? nitems : 1));
	src = (long *)raw;
	for (i = 0; i < nitems; i++)
		dst[i] = (Window)(unsigned long)src[i];

	XFree(raw);
	*count = nitems;
	return dst;
}

/* ---- format-8 string readers ----------------------------------------- */

static char *wx_get_string8(Display *dpy, Window w, Atom prop, Atom type, size_t *len)
{
	unsigned long nitems;
	unsigned char *raw;
	char *dst;

	raw = wx_get_raw(dpy, w, prop, type, 8, 0, 0, &nitems);
	if (!raw)
		return NULL;

	dst = wmalloc(nitems + 1);
	if (nitems)
		memcpy(dst, raw, nitems);
	dst[nitems] = '\0';

	XFree(raw);
	if (len)
		*len = nitems;
	return dst;
}

char *WXGetUTF8String(Display *dpy, Window w, Atom prop, size_t *len)
{
	return wx_get_string8(dpy, w, prop, wx_utf8_string(dpy), len);
}

char *WXGetLatin1String(Display *dpy, Window w, Atom prop, size_t *len)
{
	return wx_get_string8(dpy, w, prop, XA_STRING, len);
}

Bool WXPropExists(Display *dpy, Window w, Atom prop)
{
	Atom type_ret;
	int fmt_ret;
	unsigned long nitems_ret, bytes_after;
	unsigned char *data = NULL;

	if (XGetWindowProperty(dpy, w, prop, 0L, 0L, False, AnyPropertyType,
			       &type_ret, &fmt_ret, &nitems_ret, &bytes_after,
			       &data) != Success)
		return False;
	if (data)
		XFree(data);
	return type_ret != None;
}

/* ---- format-32 writers ----------------------------------------------- */

static void wx_set_long32(Display *dpy, Window w, Atom prop, Atom type,
			  const long *v, size_t n)
{
	XChangeProperty(dpy, w, prop, type, 32, PropModeReplace,
			(unsigned char *)v, (int)n);
}

void WXSetCardinal(Display *dpy, Window w, Atom prop, uint32_t v)
{
	long lv = v;
	wx_set_long32(dpy, w, prop, XA_CARDINAL, &lv, 1);
}

void WXSetFormat32N(Display *dpy, Window w, Atom prop, Atom type,
		    const uint32_t *v, size_t n)
{
	long *buf;
	size_t i;

	buf = wmalloc(sizeof(long) * (n ? n : 1));
	for (i = 0; i < n; i++)
		buf[i] = v[i];
	wx_set_long32(dpy, w, prop, type, buf, n);
	wfree(buf);
}

void WXSetCardinalN(Display *dpy, Window w, Atom prop, const uint32_t *v, size_t n)
{
	WXSetFormat32N(dpy, w, prop, XA_CARDINAL, v, n);
}

void WXSetAtom(Display *dpy, Window w, Atom prop, Atom v)
{
	long lv = (long)v;
	wx_set_long32(dpy, w, prop, XA_ATOM, &lv, 1);
}

void WXSetAtomN(Display *dpy, Window w, Atom prop, const Atom *v, size_t n)
{
	long *buf;
	size_t i;

	buf = wmalloc(sizeof(long) * (n ? n : 1));
	for (i = 0; i < n; i++)
		buf[i] = (long)v[i];
	wx_set_long32(dpy, w, prop, XA_ATOM, buf, n);
	wfree(buf);
}

void WXSetWindow(Display *dpy, Window w, Atom prop, Window v)
{
	long lv = (long)v;
	wx_set_long32(dpy, w, prop, XA_WINDOW, &lv, 1);
}

void WXSetWindowN(Display *dpy, Window w, Atom prop, const Window *v, size_t n)
{
	long *buf;
	size_t i;

	buf = wmalloc(sizeof(long) * (n ? n : 1));
	for (i = 0; i < n; i++)
		buf[i] = (long)v[i];
	wx_set_long32(dpy, w, prop, XA_WINDOW, buf, n);
	wfree(buf);
}

void WXSetUTF8String(Display *dpy, Window w, Atom prop, const char *s, size_t len)
{
	XChangeProperty(dpy, w, prop, wx_utf8_string(dpy), 8, PropModeReplace,
			(const unsigned char *)s, (int)len);
}

void WXSetLatin1String(Display *dpy, Window w, Atom prop, const char *s, size_t len)
{
	XChangeProperty(dpy, w, prop, XA_STRING, 8, PropModeReplace,
			(const unsigned char *)s, (int)len);
}

void WXSetFormat8(Display *dpy, Window w, Atom prop, Atom type,
		  const unsigned char *data, size_t len)
{
	XChangeProperty(dpy, w, prop, type, 8, PropModeReplace, data, (int)len);
}

void WXDeleteProperty(Display *dpy, Window w, Atom prop)
{
	XDeleteProperty(dpy, w, prop);
}

/* ---- ClientMessage --------------------------------------------------- */

void WXSendClientMessage(Display *dpy, Window dest, long event_mask,
			 Window about, Atom message_type,
			 long l0, long l1, long l2, long l3, long l4)
{
	XEvent ev;

	memset(&ev, 0, sizeof(ev));
	ev.xclient.type = ClientMessage;
	ev.xclient.display = dpy;
	ev.xclient.window = about;
	ev.xclient.message_type = message_type;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = l0;
	ev.xclient.data.l[1] = l1;
	ev.xclient.data.l[2] = l2;
	ev.xclient.data.l[3] = l3;
	ev.xclient.data.l[4] = l4;

	XSendEvent(dpy, dest, False, event_mask, &ev);
}
