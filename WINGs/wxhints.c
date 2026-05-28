/*
 *  WXHints - typed ICCCM convenience helpers (xcb backend)
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Property fetches go through xcb / xcb-icccm on the connection
 *  underlying the caller's Display* (XGetXCBConnection).  Xlib keeps
 *  ownership of the event queue.  Locale-sensitive decoding of
 *  COMPOUND_TEXT (WM_NAME, WM_ICON_NAME, generic text properties) stays
 *  on Xmb/Xutf8 conversion routines, which operate purely on client-side
 *  data and need no server round-trip, xcb has no equivalent.
 */

#include "wconfig.h"

#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#include <stdlib.h>
#include <string.h>

#include <WINGs/WUtil.h>
#include <WINGs/WXProp.h>
#include <WINGs/WXHints.h>

static inline xcb_connection_t *wx_conn(Display *dpy)
{
	return XGetXCBConnection(dpy);
}

static xcb_atom_t wx_wm_protocols(Display *dpy)
{
	static Display *cached_dpy = NULL;
	static xcb_atom_t cached = XCB_ATOM_NONE;

	/* key the cache on the display so a second connection re-interns,
	 * libWINGs is used by tools that may open more than one */
	if (dpy != cached_dpy) {
		cached = (xcb_atom_t)XInternAtom(dpy, "WM_PROTOCOLS", False);
		cached_dpy = dpy;
	}
	return cached;
}

/* --- allocators ------------------------------------------------------ */

WXSizeHints *WXAllocSizeHints(void)
{
	return wmalloc(sizeof(WXSizeHints));
}

WXWMHints *WXAllocWMHints(void)
{
	return wmalloc(sizeof(WXWMHints));
}

/* --- WM_NORMAL_HINTS ------------------------------------------------- */

/* Pre-ICCCM (15-field) supplied mask, matching XGetWMNormalHints. */
#define WX_SUPPLIED_OLD \
	(USPosition | USSize | PPosition | PSize | \
	 PMinSize | PMaxSize | PResizeInc | PAspect)

Bool WXGetNormalHints(Display *dpy, Window w, WXSizeHints *out, long *supplied)
{
	xcb_connection_t *c = wx_conn(dpy);
	xcb_get_property_cookie_t ck;
	xcb_get_property_reply_t *r;
	xcb_size_hints_t h;
	uint32_t n;

	ck = xcb_icccm_get_wm_normal_hints(c, (xcb_window_t)w);
	r = xcb_get_property_reply(c, ck, NULL);
	if (!r)
		return False;

	n = (uint32_t)(xcb_get_property_value_length(r) / 4);
	if (r->type != XCB_ATOM_WM_SIZE_HINTS || r->format != 32 ||
	    !xcb_icccm_get_wm_size_hints_from_reply(&h, r)) {
		free(r);
		return False;
	}
	free(r);

	memset(out, 0, sizeof(*out));
	out->flags        = (long)h.flags;
	out->x            = h.x;
	out->y            = h.y;
	out->width        = h.width;
	out->height       = h.height;
	out->min_width    = h.min_width;
	out->min_height   = h.min_height;
	out->max_width    = h.max_width;
	out->max_height   = h.max_height;
	out->width_inc    = h.width_inc;
	out->height_inc   = h.height_inc;
	out->min_aspect.x = h.min_aspect_num;
	out->min_aspect.y = h.min_aspect_den;
	out->max_aspect.x = h.max_aspect_num;
	out->max_aspect.y = h.max_aspect_den;
	if (n >= XCB_ICCCM_NUM_WM_SIZE_HINTS_ELEMENTS) {
		out->base_width  = h.base_width;
		out->base_height = h.base_height;
		out->win_gravity = (int)h.win_gravity;
		if (supplied)
			*supplied = WX_SUPPLIED_OLD | PBaseSize | PWinGravity;
	} else if (supplied) {
		*supplied = WX_SUPPLIED_OLD;
	}
	return True;
}

void WXSetNormalHints(Display *dpy, Window w, const WXSizeHints *in)
{
	xcb_size_hints_t h;

	memset(&h, 0, sizeof(h));
	h.flags          = (uint32_t)in->flags;
	h.x              = in->x;
	h.y              = in->y;
	h.width          = in->width;
	h.height         = in->height;
	h.min_width      = in->min_width;
	h.min_height     = in->min_height;
	h.max_width      = in->max_width;
	h.max_height     = in->max_height;
	h.width_inc      = in->width_inc;
	h.height_inc     = in->height_inc;
	h.min_aspect_num = in->min_aspect.x;
	h.min_aspect_den = in->min_aspect.y;
	h.max_aspect_num = in->max_aspect.x;
	h.max_aspect_den = in->max_aspect.y;
	h.base_width     = in->base_width;
	h.base_height    = in->base_height;
	h.win_gravity    = (uint32_t)in->win_gravity;

	xcb_icccm_set_wm_normal_hints(wx_conn(dpy), (xcb_window_t)w, &h);
}

/* --- WM_HINTS -------------------------------------------------------- */

WXWMHints *WXGetWMHints(Display *dpy, Window w)
{
	xcb_connection_t *c = wx_conn(dpy);
	xcb_icccm_wm_hints_t h;
	WXWMHints *out;

	if (!xcb_icccm_get_wm_hints_reply(c,
			xcb_icccm_get_wm_hints(c, (xcb_window_t)w), &h, NULL))
		return NULL;

	out = wmalloc(sizeof(WXWMHints));
	out->flags         = (long)h.flags;
	out->input         = (Bool)h.input;
	out->initial_state = h.initial_state;
	out->icon_pixmap   = (Pixmap)h.icon_pixmap;
	out->icon_window   = (Window)h.icon_window;
	out->icon_x        = h.icon_x;
	out->icon_y        = h.icon_y;
	out->icon_mask     = (Pixmap)h.icon_mask;
	out->window_group  = (XID)h.window_group;
	return out;
}

void WXSetWMHints(Display *dpy, Window w, const WXWMHints *in)
{
	xcb_icccm_wm_hints_t h;

	memset(&h, 0, sizeof(h));
	h.flags         = (int32_t)in->flags;
	h.input         = (uint32_t)in->input;
	h.initial_state = in->initial_state;
	h.icon_pixmap   = (xcb_pixmap_t)in->icon_pixmap;
	h.icon_window   = (xcb_window_t)in->icon_window;
	h.icon_x        = in->icon_x;
	h.icon_y        = in->icon_y;
	h.icon_mask     = (xcb_pixmap_t)in->icon_mask;
	h.window_group  = (xcb_window_t)in->window_group;

	xcb_icccm_set_wm_hints(wx_conn(dpy), (xcb_window_t)w, &h);
}

/* --- WM_CLASS -------------------------------------------------------- */

Bool WXGetWMClass(Display *dpy, Window w, char **instance, char **wclass)
{
	xcb_connection_t *c = wx_conn(dpy);
	xcb_icccm_get_wm_class_reply_t r;

	if (!xcb_icccm_get_wm_class_reply(c,
			xcb_icccm_get_wm_class(c, (xcb_window_t)w), &r, NULL))
		return False;

	if (instance)
		*instance = r.instance_name ? wstrdup(r.instance_name) : NULL;
	if (wclass)
		*wclass = r.class_name ? wstrdup(r.class_name) : NULL;

	xcb_icccm_get_wm_class_reply_wipe(&r);
	return True;
}

void WXSetWMClass(Display *dpy, Window w, const char *instance, const char *wclass)
{
	size_t ilen, clen;
	char *buf;

	if (!instance)
		instance = "";
	if (!wclass)
		wclass = "";
	ilen = strlen(instance);
	clen = strlen(wclass);

	buf = wmalloc(ilen + clen + 2);
	memcpy(buf, instance, ilen);
	buf[ilen] = '\0';
	memcpy(buf + ilen + 1, wclass, clen);
	buf[ilen + 1 + clen] = '\0';

	xcb_icccm_set_wm_class(wx_conn(dpy), (xcb_window_t)w,
			       (uint32_t)(ilen + clen + 2), buf);
	wfree(buf);
}

/* --- WM_PROTOCOLS ---------------------------------------------------- */

Atom *WXGetWMProtocols(Display *dpy, Window w, size_t *count)
{
	xcb_connection_t *c = wx_conn(dpy);
	xcb_icccm_get_wm_protocols_reply_t r;
	Atom *out;
	uint32_t i;

	if (count)
		*count = 0;

	if (!xcb_icccm_get_wm_protocols_reply(c,
			xcb_icccm_get_wm_protocols(c, (xcb_window_t)w,
						   wx_wm_protocols(dpy)),
			&r, NULL))
		return NULL;

	if (r.atoms_len == 0) {
		xcb_icccm_get_wm_protocols_reply_wipe(&r);
		return NULL;
	}

	out = wmalloc(sizeof(Atom) * r.atoms_len);
	for (i = 0; i < r.atoms_len; i++)
		out[i] = (Atom)r.atoms[i];

	if (count)
		*count = r.atoms_len;
	xcb_icccm_get_wm_protocols_reply_wipe(&r);
	return out;
}

void WXSetWMProtocols(Display *dpy, Window w, const Atom *atoms, size_t count)
{
	uint32_t *buf;
	size_t i;

	buf = wmalloc(sizeof(uint32_t) * (count ? count : 1));
	for (i = 0; i < count; i++)
		buf[i] = (uint32_t)atoms[i];

	xcb_icccm_set_wm_protocols(wx_conn(dpy), (xcb_window_t)w,
				   wx_wm_protocols(dpy), (uint32_t)count, buf);
	wfree(buf);
}

/* --- WM_TRANSIENT_FOR ----------------------------------------------- */

Bool WXGetTransientFor(Display *dpy, Window w, Window *out)
{
	xcb_connection_t *c = wx_conn(dpy);
	xcb_window_t owner;

	if (!xcb_icccm_get_wm_transient_for_reply(c,
			xcb_icccm_get_wm_transient_for(c, (xcb_window_t)w),
			&owner, NULL))
		return False;
	*out = (Window)owner;
	return True;
}

void WXSetTransientFor(Display *dpy, Window w, Window owner)
{
	xcb_icccm_set_wm_transient_for(wx_conn(dpy), (xcb_window_t)w,
				       (xcb_window_t)owner);
}

/* --- text-property helpers ------------------------------------------ */

/* Fetch a text property over xcb, then hand the bytes to Xlib's locale
 * converter.  XmbTextPropertyToTextList is purely client-side. */
static char **wx_get_text_list(Display *dpy, Window w, Atom prop, int *count)
{
	xcb_connection_t *c = wx_conn(dpy);
	xcb_get_property_cookie_t ck;
	xcb_get_property_reply_t *r;
	XTextProperty tp;
	char **xlist = NULL, **out;
	int n = 0, i;

	*count = 0;

	ck = xcb_get_property(c, 0, (xcb_window_t)w, (xcb_atom_t)prop,
			      XCB_GET_PROPERTY_TYPE_ANY, 0, 0x00100000u);
	r = xcb_get_property_reply(c, ck, NULL);
	if (!r)
		return NULL;
	if (r->type == XCB_ATOM_NONE || r->format != 8 ||
	    xcb_get_property_value_length(r) == 0) {
		free(r);
		return NULL;
	}

	tp.value    = xcb_get_property_value(r);
	tp.encoding = (Atom)r->type;
	tp.format   = r->format;
	tp.nitems   = (unsigned long)xcb_get_property_value_length(r);

	if (XmbTextPropertyToTextList(dpy, &tp, &xlist, &n) < Success ||
	    n <= 0 || !xlist) {
		if (xlist)
			XFreeStringList(xlist);
		free(r);
		return NULL;
	}
	free(r);

	out = wmalloc(sizeof(char *) * n);
	for (i = 0; i < n; i++)
		out[i] = wstrdup(xlist[i]);
	XFreeStringList(xlist);

	*count = n;
	return out;
}

static char *wx_get_text_single(Display *dpy, Window w, Atom prop)
{
	char **list, *ret;
	int n;

	list = wx_get_text_list(dpy, w, prop, &n);
	if (!list)
		return NULL;
	ret = list[0];
	/* free spillover entries, keep [0] */
	while (--n > 0)
		wfree(list[n]);
	wfree(list);
	return ret;
}

char *WXGetWMName(Display *dpy, Window w)
{
	return wx_get_text_single(dpy, w, XA_WM_NAME);
}

char *WXGetWMIconName(Display *dpy, Window w)
{
	return wx_get_text_single(dpy, w, XA_WM_ICON_NAME);
}

static void wx_set_text(Display *dpy, Window w, Atom prop, const char *s)
{
	XTextProperty tp;

	if (!s)
		s = "";
	if (Xutf8TextListToTextProperty(dpy, (char **)&s, 1,
					XStdICCTextStyle, &tp) < Success)
		return;
	xcb_change_property(wx_conn(dpy), XCB_PROP_MODE_REPLACE,
			    (xcb_window_t)w, (xcb_atom_t)prop,
			    (xcb_atom_t)tp.encoding, tp.format,
			    (uint32_t)tp.nitems, tp.value);
	if (tp.value)
		XFree(tp.value);
}

void WXSetWMName(Display *dpy, Window w, const char *name)
{
	wx_set_text(dpy, w, XA_WM_NAME, name);
}

void WXSetWMIconName(Display *dpy, Window w, const char *name)
{
	wx_set_text(dpy, w, XA_WM_ICON_NAME, name);
}

/* --- WM_COMMAND ------------------------------------------------------ */

Bool WXGetCommand(Display *dpy, Window w, char ***argv, int *argc)
{
	*argv = wx_get_text_list(dpy, w, XA_WM_COMMAND, argc);
	return *argv != NULL;
}

void WXSetCommand(Display *dpy, Window w, char **argv, int argc)
{
	XTextProperty tp;

	if (XStringListToTextProperty(argv, argc, &tp) == 0)
		return;
	xcb_change_property(wx_conn(dpy), XCB_PROP_MODE_REPLACE,
			    (xcb_window_t)w, XCB_ATOM_WM_COMMAND,
			    (xcb_atom_t)tp.encoding, tp.format,
			    (uint32_t)tp.nitems, tp.value);
	if (tp.value)
		XFree(tp.value);
}

/* --- generic text-property string lists ------------------------------ */

char **WXGetTextPropertyList(Display *dpy, Window w, Atom prop, int *count)
{
	return wx_get_text_list(dpy, w, prop, count);
}

void WXSetTextPropertyList(Display *dpy, Window w, Atom prop,
			   char **list, int count)
{
	XTextProperty tp;

	if (XStringListToTextProperty(list, count, &tp) == 0)
		return;
	xcb_change_property(wx_conn(dpy), XCB_PROP_MODE_REPLACE,
			    (xcb_window_t)w, (xcb_atom_t)prop,
			    (xcb_atom_t)tp.encoding, tp.format,
			    (uint32_t)tp.nitems, tp.value);
	if (tp.value)
		XFree(tp.value);
}

void WXFreeStringList(char **list, int count)
{
	int i;

	if (!list)
		return;
	for (i = 0; i < count; i++)
		wfree(list[i]);
	wfree(list);
}
