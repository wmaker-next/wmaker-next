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
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <https://www.gnu.org/licenses/>.
 *
 *  Property fetches go through xcb / xcb-icccm on the caller's xcb
 *  connection, which also owns the event queue. Each getter is split
 *  into a WXReq*
 *  request half (queues a GetProperty without blocking) and a WXRcv*
 *  reply half, and the synchronous WXGet* wrappers compose the two.
 *
 *  Text-property decoding/encoding (WM_NAME, WM_ICON_NAME,
 *  WM_COMMAND, generic text properties) is client-side: STRING is
 *  Latin-1, COMPOUND_TEXT goes through xcb-imdkit, UTF8_STRING passes
 *  through.
 */

#include "wconfig.h"

#include <WINGs/WXCore.h>
#include <WINGs/WUtil.h>
#include <WINGs/WXProp.h>
#include <WINGs/WXHints.h>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#include <xcb-imdkit/encoding.h>
#include <stdlib.h>
#include <string.h>

static inline xcb_connection_t *wx_conn(xcb_connection_t *dpy)
{
	return dpy;
}

static inline WXCookie wx_ck(xcb_get_property_cookie_t xck)
{
	WXCookie ck = { xck.sequence };
	return ck;
}

static inline xcb_get_property_cookie_t wx_xck(WXCookie ck)
{
	xcb_get_property_cookie_t xck = { ck.seq };
	return xck;
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
	(XCB_ICCCM_SIZE_HINT_US_POSITION | XCB_ICCCM_SIZE_HINT_US_SIZE | \
	 XCB_ICCCM_SIZE_HINT_P_POSITION | XCB_ICCCM_SIZE_HINT_P_SIZE | \
	 XCB_ICCCM_SIZE_HINT_P_MIN_SIZE | XCB_ICCCM_SIZE_HINT_P_MAX_SIZE | \
	 XCB_ICCCM_SIZE_HINT_P_RESIZE_INC | XCB_ICCCM_SIZE_HINT_P_ASPECT)

WXCookie WXReqNormalHints(xcb_connection_t *dpy, xcb_window_t w)
{
	return wx_ck(xcb_icccm_get_wm_normal_hints(dpy, (xcb_window_t)w));
}

bool WXRcvNormalHints(xcb_connection_t *dpy, WXCookie ck, WXSizeHints *out, uint32_t *supplied)
{
	xcb_get_property_reply_t *r;
	xcb_size_hints_t h;
	uint32_t n;

	r = xcb_get_property_reply(dpy, wx_xck(ck), NULL);
	if (!r)
		return false;

	/* short WM_NORMAL_HINTS properties succeed through
	 * get_wm_size_hints_from_reply but fill only some fields, zero
	 * the local so the partial copy never propagates stack bytes
	 * into window placement */
	memset(&h, 0, sizeof(h));
	n = (uint32_t)(xcb_get_property_value_length(r) / 4);
	if (r->type != XCB_ATOM_WM_SIZE_HINTS || r->format != 32 ||
	    !xcb_icccm_get_wm_size_hints_from_reply(&h, r)) {
		free(r);
		return false;
	}
	free(r);

	*out = h;
	if (n >= XCB_ICCCM_NUM_WM_SIZE_HINTS_ELEMENTS) {
		if (supplied)
			*supplied = WX_SUPPLIED_OLD | XCB_ICCCM_SIZE_HINT_BASE_SIZE | XCB_ICCCM_SIZE_HINT_P_WIN_GRAVITY;
	} else if (supplied) {
		*supplied = WX_SUPPLIED_OLD;
	}
	return true;
}

bool WXGetNormalHints(xcb_connection_t *dpy, xcb_window_t w, WXSizeHints *out, uint32_t *supplied)
{
	return WXRcvNormalHints(dpy, WXReqNormalHints(dpy, w), out, supplied);
}

void WXSetNormalHints(xcb_connection_t *dpy, xcb_window_t w, const WXSizeHints *in)
{
	xcb_size_hints_t h;

	memset(&h, 0, sizeof(h));
	h.flags          = in->flags;
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
	h.min_aspect_num = in->min_aspect_num;
	h.min_aspect_den = in->min_aspect_den;
	h.max_aspect_num = in->max_aspect_num;
	h.max_aspect_den = in->max_aspect_den;
	h.base_width     = in->base_width;
	h.base_height    = in->base_height;
	h.win_gravity    = in->win_gravity;

	xcb_icccm_set_wm_normal_hints(dpy, (xcb_window_t)w, &h);
}

/* --- WM_HINTS -------------------------------------------------------- */

WXCookie WXReqWMHints(xcb_connection_t *dpy, xcb_window_t w)
{
	return wx_ck(xcb_icccm_get_wm_hints(dpy, (xcb_window_t)w));
}

WXWMHints *WXRcvWMHints(xcb_connection_t *dpy, WXCookie ck)
{
	xcb_icccm_wm_hints_t h;
	WXWMHints *out;

	if (!xcb_icccm_get_wm_hints_reply(dpy, wx_xck(ck), &h, NULL))
		return NULL;

	out = wmalloc(sizeof(WXWMHints));
	*out = h;
	return out;
}

WXWMHints *WXGetWMHints(xcb_connection_t *dpy, xcb_window_t w)
{
	return WXRcvWMHints(dpy, WXReqWMHints(dpy, w));
}

void WXSetWMHints(xcb_connection_t *dpy, xcb_window_t w, const WXWMHints *in)
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

	xcb_icccm_set_wm_hints(dpy, (xcb_window_t)w, &h);
}

/* --- WM_CLASS -------------------------------------------------------- */

WXCookie WXReqWMClass(xcb_connection_t *dpy, xcb_window_t w)
{
	return wx_ck(xcb_icccm_get_wm_class(dpy, (xcb_window_t)w));
}

bool WXRcvWMClass(xcb_connection_t *dpy, WXCookie ck, char **instance, char **wclass)
{
	xcb_icccm_get_wm_class_reply_t r;

	if (!xcb_icccm_get_wm_class_reply(dpy, wx_xck(ck), &r, NULL))
		return false;

	if (instance)
		*instance = r.instance_name ? wstrdup(r.instance_name) : NULL;
	if (wclass)
		*wclass = r.class_name ? wstrdup(r.class_name) : NULL;

	xcb_icccm_get_wm_class_reply_wipe(&r);
	return true;
}

bool WXGetWMClass(xcb_connection_t *dpy, xcb_window_t w, char **instance, char **wclass)
{
	return WXRcvWMClass(dpy, WXReqWMClass(dpy, w), instance, wclass);
}

void WXSetWMClass(xcb_connection_t *dpy, xcb_window_t w, const char *instance, const char *wclass)
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

	xcb_icccm_set_wm_class(dpy, (xcb_window_t)w,
			       (uint32_t)(ilen + clen + 2), buf);
	wfree(buf);
}

/* --- WM_PROTOCOLS ---------------------------------------------------- */

WXCookie WXReqWMProtocols(xcb_connection_t *dpy, xcb_window_t w)
{
	return wx_ck(xcb_icccm_get_wm_protocols(dpy, (xcb_window_t)w,
						wx_static_atoms[W_ATOM_WM_PROTOCOLS]));
}

xcb_atom_t *WXRcvWMProtocols(xcb_connection_t *dpy, WXCookie ck, size_t *count)
{
	xcb_icccm_get_wm_protocols_reply_t r;
	xcb_atom_t *out;
	uint32_t i;

	if (count)
		*count = 0;

	if (!xcb_icccm_get_wm_protocols_reply(dpy, wx_xck(ck), &r, NULL))
		return NULL;

	if (r.atoms_len == 0) {
		xcb_icccm_get_wm_protocols_reply_wipe(&r);
		return NULL;
	}

	out = wmalloc(sizeof(xcb_atom_t) * r.atoms_len);
	for (i = 0; i < r.atoms_len; i++)
		out[i] = (xcb_atom_t)r.atoms[i];

	if (count)
		*count = r.atoms_len;
	xcb_icccm_get_wm_protocols_reply_wipe(&r);
	return out;
}

xcb_atom_t *WXGetWMProtocols(xcb_connection_t *dpy, xcb_window_t w, size_t *count)
{
	return WXRcvWMProtocols(dpy, WXReqWMProtocols(dpy, w), count);
}

void WXSetWMProtocols(xcb_connection_t *dpy, xcb_window_t w, const xcb_atom_t *atoms, size_t count)
{
	uint32_t *buf;
	size_t i;

	buf = wmalloc(sizeof(uint32_t) * (count ? count : 1));
	for (i = 0; i < count; i++)
		buf[i] = (uint32_t)atoms[i];

	xcb_icccm_set_wm_protocols(dpy, (xcb_window_t)w,
				   wx_static_atoms[W_ATOM_WM_PROTOCOLS], (uint32_t)count, buf);
	wfree(buf);
}

/* --- WM_TRANSIENT_FOR ----------------------------------------------- */

WXCookie WXReqTransientFor(xcb_connection_t *dpy, xcb_window_t w)
{
	return wx_ck(xcb_icccm_get_wm_transient_for(dpy, (xcb_window_t)w));
}

bool WXRcvTransientFor(xcb_connection_t *dpy, WXCookie ck, xcb_window_t *out)
{
	xcb_window_t owner;

	if (!xcb_icccm_get_wm_transient_for_reply(dpy, wx_xck(ck),
						  &owner, NULL))
		return false;
	*out = (xcb_window_t)owner;
	return true;
}

bool WXGetTransientFor(xcb_connection_t *dpy, xcb_window_t w, xcb_window_t *out)
{
	return WXRcvTransientFor(dpy, WXReqTransientFor(dpy, w), out);
}

void WXSetTransientFor(xcb_connection_t *dpy, xcb_window_t w, xcb_window_t owner)
{
	xcb_icccm_set_wm_transient_for(dpy, (xcb_window_t)w,
				       (xcb_window_t)owner);
}

/* ---- client-side text property codec --------------------------------- */
/* The Xlib locale converters that used to sit here needed a Display*;
 * the codec below needs nothing but the intern cache. STRING is
 * Latin-1, COMPOUND_TEXT goes through xcb-imdkit, UTF8_STRING passes
 * through untouched, everything else fails like XConverterNotFound. */

static void wx_ctext_init(void)
{
	static bool done = false;

	if (!done) {
		xcb_compound_text_init();
		done = true;
	}
}

/* Latin-1 bytes to UTF-8, wmalloc()ed. */
char *W_Latin1ToUtf8(const unsigned char *in, size_t len)
{
	char *out = wmalloc(len * 2 + 1);
	size_t i, n = 0;

	for (i = 0; i < len; i++) {
		unsigned char c = in[i];

		if (c < 0x80) {
			out[n++] = (char) c;
		} else {
			out[n++] = (char) (0xc0 | (c >> 6));
			out[n++] = (char) (0x80 | (c & 0x3f));
		}
	}
	out[n] = '\0';
	return out;
}

/* One decoded UTF-8 character; returns its byte length (0 on a
 * malformed sequence), stores the codepoint. */
int W_Utf8Next(const unsigned char *s, size_t len, unsigned int *cp)
{
	unsigned int v;
	int n, i;

	if (s[0] < 0x80) {
		*cp = s[0];
		return 1;
	}
	if ((s[0] & 0xe0) == 0xc0) {
		v = s[0] & 0x1f;
		n = 2;
	} else if ((s[0] & 0xf0) == 0xe0) {
		v = s[0] & 0x0f;
		n = 3;
	} else if ((s[0] & 0xf8) == 0xf0) {
		v = s[0] & 0x07;
		n = 4;
	} else {
		return 0;
	}
	if ((size_t) n > len)
		return 0;
	for (i = 1; i < n; i++) {
		if ((s[i] & 0xc0) != 0x80)
			return 0;
		v = (v << 6) | (s[i] & 0x3f);
	}
	*cp = v;
	return n;
}

/* UTF-8 to Latin-1 bytes when every codepoint fits, NULL otherwise.
 * wmalloc()ed. */
char *W_Utf8ToLatin1(const char *s, size_t len, size_t *outlen)
{
	unsigned char *out = wmalloc(len + 1);
	size_t i = 0, n = 0;

	while (i < len) {
		unsigned int cp;
		int cl = W_Utf8Next((const unsigned char *) s + i, len - i, &cp);

		if (cl == 0 || cp > 0xff) {
			wfree(out);
			return NULL;
		}
		out[n++] = (unsigned char) cp;
		i += (size_t) cl;
	}
	out[n] = '\0';
	*outlen = n;
	return (char *) out;
}

/* Decode one NUL-free element of an 8-bit text property into a
 * wmalloc()ed UTF-8 string, NULL on an unsupported encoding or a
 * conversion failure. */
static char *wx_decode_element(xcb_connection_t *dpy, xcb_atom_t encoding,
			       const unsigned char *bytes, size_t len)
{
	(void) dpy;

	if (len == 0) {
		char *empty = wmalloc(1);

		empty[0] = '\0';
		return empty;
	}

	if (encoding == XCB_ATOM_STRING)
		return W_Latin1ToUtf8(bytes, len);

	if (encoding == wx_static_atoms[W_ATOM_COMPOUND_TEXT] ||
	    encoding == wx_static_atoms[W_ATOM_TEXT]) {
		char *utf, *out;
		size_t outlen = 0;

		wx_ctext_init();
		utf = xcb_compound_text_to_utf8((const char *) bytes, len, &outlen);
		if (!utf)
			return NULL;
		/* lib-owned malloc, copy into a wmalloc()ed string */
		out = wstrdup(utf);
		free(utf);
		return out;
	}

	if (encoding == wx_static_atoms[W_ATOM_UTF8_STRING]) {
		char *out = wmalloc(len + 1);

		memcpy(out, bytes, len);
		out[len] = '\0';
		return out;
	}

	return NULL;
}

#define WX_MAX_TEXT_ELEMENTS 4096

/* Split an 8-bit text property into its NUL-separated elements and
 * decode each one. A trailing empty piece (the terminating NUL some
 * clients store) is dropped, matching the text-list semantics
 * XmbTextPropertyToTextList implemented. */
static char **wx_decode_text_property(xcb_connection_t *dpy, const unsigned char *bytes,
				      size_t len, xcb_atom_t encoding, int *count)
{
	char **list;
	size_t start, i;
	int n, max;

	max = 2;
	list = wmalloc(sizeof(char *) * max);
	n = 0;

	start = 0;
	for (i = 0; i <= len; i++) {
		if (i < len && bytes[i] != '\0')
			continue;

		if (i > start || (i == start && i < len)) {
			char *s;

			if (n >= WX_MAX_TEXT_ELEMENTS) {
				while (n > 0)
					wfree(list[--n]);
				wfree(list);
				return NULL;
			}
			s = wx_decode_element(dpy, encoding, bytes + start,
					      i - start);

			if (!s) {
				while (n > 0)
					wfree(list[--n]);
				wfree(list);
				return NULL;
			}
			if (n == max - 1) {
				max *= 2;
				list = wrealloc(list, sizeof(char *) * max);
			}
			list[n++] = s;
		}
		start = i + 1;
	}

	if (n == 0) {
		wfree(list);
		return NULL;
	}

	*count = n;
	return list;
}

/* --- text-property helpers ------------------------------------------ */

WXCookie WXReqTextPropertyList(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop)
{
	return wx_ck(xcb_get_property(dpy, 0, (xcb_window_t)w,
				      (xcb_atom_t)prop,
				      XCB_GET_PROPERTY_TYPE_ANY, 0, 0x00100000u));
}

/* Collect a text-property reply and decode it client-side. */
char **WXRcvTextPropertyList(xcb_connection_t *dpy, WXCookie ck, int *count)
{
	xcb_get_property_reply_t *r;
	char **out;

	*count = 0;

	r = xcb_get_property_reply(dpy, wx_xck(ck), NULL);
	if (!r)
		return NULL;
	if (r->type == XCB_ATOM_NONE || r->format != 8 || r->bytes_after != 0) {
		free(r);
		return NULL;
	}
	/* a present but empty WM_NAME or WM_ICON_NAME is a deliberate
	 * blank title, return one empty string so callers do not fall
	 * back to a generated name */
	if (xcb_get_property_value_length(r) == 0) {
		free(r);
		out = wmalloc(sizeof(char *));
		out[0] = wstrdup("");
		*count = 1;
		return out;
	}

	out = wx_decode_text_property(dpy, xcb_get_property_value(r),
				      xcb_get_property_value_length(r),
				      (xcb_atom_t) r->type, count);
	free(r);
	return out;
}

static char *wx_rcv_text_single(xcb_connection_t *dpy, WXCookie ck)
{
	char **list, *ret;
	int n;

	list = WXRcvTextPropertyList(dpy, ck, &n);
	if (!list)
		return NULL;
	ret = list[0];
	/* free spillover entries, keep [0] */
	while (--n > 0)
		wfree(list[n]);
	wfree(list);
	return ret;
}

WXCookie WXReqWMName(xcb_connection_t *dpy, xcb_window_t w)
{
	return WXReqTextPropertyList(dpy, w, XCB_ATOM_WM_NAME);
}

char *WXRcvWMName(xcb_connection_t *dpy, WXCookie ck)
{
	return wx_rcv_text_single(dpy, ck);
}

char *WXGetWMName(xcb_connection_t *dpy, xcb_window_t w)
{
	return WXRcvWMName(dpy, WXReqWMName(dpy, w));
}

WXCookie WXReqWMIconName(xcb_connection_t *dpy, xcb_window_t w)
{
	return WXReqTextPropertyList(dpy, w, XCB_ATOM_WM_ICON_NAME);
}

char *WXRcvWMIconName(xcb_connection_t *dpy, WXCookie ck)
{
	return wx_rcv_text_single(dpy, ck);
}

char *WXGetWMIconName(xcb_connection_t *dpy, xcb_window_t w)
{
	return WXRcvWMIconName(dpy, WXReqWMIconName(dpy, w));
}

/* Encode one UTF-8 string the XStdICCTextStyle way: STRING when every
 * codepoint fits Latin-1, COMPOUND_TEXT otherwise. The value buffer is
 * wmalloc()ed and carries the trailing NUL a text property has. */
static bool wx_encode_stdicctext(xcb_connection_t *dpy, const char *s, xcb_atom_t *encoding,
				 unsigned char **value, uint32_t *nitems)
{
	size_t len = strlen(s);
	size_t latin_len = 0;
	char *latin;
	char *ct;

	(void) dpy;
	size_t ctlen = 0;

	latin = W_Utf8ToLatin1(s, len, &latin_len);
	if (latin) {
		*encoding = XCB_ATOM_STRING;
		*value = (unsigned char *) latin;
		*nitems = (uint32_t) latin_len + 1;
		return true;
	}

	wx_ctext_init();
	ct = xcb_utf8_to_compound_text(s, len, &ctlen);
	if (!ct)
		return false;
	*encoding = wx_static_atoms[W_ATOM_COMPOUND_TEXT];
	*value = (unsigned char *) wstrdup(ct);
	*nitems = (uint32_t) ctlen + 1;
	free(ct);
	return true;
}

static void wx_set_text(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, const char *s)
{
	xcb_atom_t encoding;
	unsigned char *value;
	uint32_t nitems;

	if (!s)
		s = "";
	if (!wx_encode_stdicctext(dpy, s, &encoding, &value, &nitems))
		return;
	xcb_change_property(dpy, XCB_PROP_MODE_REPLACE,
			    (xcb_window_t)w, (xcb_atom_t)prop,
			    (xcb_atom_t)encoding, 8, nitems, value);
	wfree(value);
}

void WXSetWMName(xcb_connection_t *dpy, xcb_window_t w, const char *name)
{
	wx_set_text(dpy, w, XCB_ATOM_WM_NAME, name);
}

void WXSetWMIconName(xcb_connection_t *dpy, xcb_window_t w, const char *name)
{
	wx_set_text(dpy, w, XCB_ATOM_WM_ICON_NAME, name);
}

/* --- WM_COMMAND ------------------------------------------------------ */

WXCookie WXReqCommand(xcb_connection_t *dpy, xcb_window_t w)
{
	return WXReqTextPropertyList(dpy, w, XCB_ATOM_WM_COMMAND);
}

bool WXRcvCommand(xcb_connection_t *dpy, WXCookie ck, char ***argv, int *argc)
{
	*argv = WXRcvTextPropertyList(dpy, ck, argc);
	return *argv != NULL;
}

bool WXGetCommand(xcb_connection_t *dpy, xcb_window_t w, char ***argv, int *argc)
{
	return WXRcvCommand(dpy, WXReqCommand(dpy, w), argv, argc);
}

/* the Xlib XStringListToTextProperty shape: format-8 STRING property,
 * the elements NUL-separated with a trailing NUL (the ICCCM WM_COMMAND
 * contract; the bytes are the caller's, no codec involved) */
static void wx_set_text_list(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop,
			     char **list, int count)
{
	size_t total = 0, n;
	int i;
	unsigned char *data, *p;

	if (count < 1 || !list)
		return;
	for (i = 0; i < count; i++)
		total += strlen(list[i]) + 1;
	data = wmalloc(total);
	p = data;
	for (i = 0; i < count; i++) {
		n = strlen(list[i]);
		memcpy(p, list[i], n);
		p += n;
		*p++ = '\0';
	}
	xcb_change_property(dpy, XCB_PROP_MODE_REPLACE, (xcb_window_t)w,
			    (xcb_atom_t)prop, XCB_ATOM_STRING, 8,
			    (uint32_t)total, data);
	wfree(data);
}

void WXSetCommand(xcb_connection_t *dpy, xcb_window_t w, char **argv, int argc)
{
	wx_set_text_list(dpy, w, XCB_ATOM_WM_COMMAND, argv, argc);
}

/* --- generic text-property string lists ------------------------------ */

char **WXGetTextPropertyList(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, int *count)
{
	return WXRcvTextPropertyList(dpy, WXReqTextPropertyList(dpy, w, prop), count);
}

void WXSetTextPropertyList(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop,
			   char **list, int count)
{
	wx_set_text_list(dpy, w, prop, list, count);
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
