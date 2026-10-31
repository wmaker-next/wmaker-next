/*
 *  WXCore - composed core-protocol helpers (xcb backend)
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
 */

#include "wconfig.h"

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xcb_cursor.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <WINGs/WUtil.h>
#include <WINGs/WXCore.h>
#include <WINGs/WXProp.h>

void wXSync(xcb_connection_t *dpy)
{
	xcb_get_input_focus_cookie_t ck;
	xcb_get_input_focus_reply_t *rep;
	xcb_generic_error_t *err;

	/* push the outgoing buffer before the barrier round trip */
	xcb_flush(dpy);
	ck = xcb_get_input_focus(dpy);
	err = NULL;
	rep = xcb_get_input_focus_reply(dpy, ck, &err);
	free(rep);
	free(err);
}

void wXFlush(xcb_connection_t *dpy)
{
	xcb_flush(dpy);
}

/* ---- map/stack family -------------------------------------------------- */

void wXMapWindow(xcb_connection_t *dpy, xcb_window_t w)
{
	xcb_map_window(dpy, w);
}

/* libX11's XMapRaised is configure(stack_mode Above) followed by map,
 * keep the two-request shape */
void wXMapRaised(xcb_connection_t *dpy, xcb_window_t w)
{
	xcb_configure_window(dpy, w, XCB_CONFIG_WINDOW_STACK_MODE,
			     (const uint32_t[]){ XCB_STACK_MODE_ABOVE });
	xcb_map_window(dpy, w);
}

void wXMapSubwindows(xcb_connection_t *dpy, xcb_window_t w)
{
	xcb_map_subwindows(dpy, w);
}

void wXUnmapSubwindows(xcb_connection_t *dpy, xcb_window_t w)
{
	xcb_unmap_subwindows(dpy, w);
}

void wXRaiseWindow(xcb_connection_t *dpy, xcb_window_t w)
{
	xcb_configure_window(dpy, w, XCB_CONFIG_WINDOW_STACK_MODE,
			     (const uint32_t[]){ XCB_STACK_MODE_ABOVE });
}

void wXLowerWindow(xcb_connection_t *dpy, xcb_window_t w)
{
	xcb_configure_window(dpy, w, XCB_CONFIG_WINDOW_STACK_MODE,
			     (const uint32_t[]){ XCB_STACK_MODE_BELOW });
}

void wXClearArea(xcb_connection_t *dpy, xcb_window_t w, int x, int y,
		 unsigned int width, unsigned int height, bool exposures)
{
	xcb_clear_area(dpy, exposures, w, x, y, width, height);
}

void wXFreePixmap(xcb_connection_t *dpy, xcb_pixmap_t pixmap)
{
	xcb_free_pixmap(dpy, pixmap);
}

/* ---- XConfigureWindow -------------------------------------------------- */

/* Xlib's CW_* bits and XWindowChanges argument shape, the xcb wire list
 * is emitted in ascending CW_ bit order */
int wXConfigureWindow(xcb_connection_t *dpy, xcb_window_t w, unsigned int mask, WXWindowChanges *wc)
{
	uint32_t vals[7];
	uint32_t xmask;
	int n = 0;

	if (mask & XCB_CONFIG_WINDOW_X)
		vals[n++] = (uint16_t) wc->x;
	if (mask & XCB_CONFIG_WINDOW_Y)
		vals[n++] = (uint16_t) wc->y;
	if (mask & XCB_CONFIG_WINDOW_WIDTH)
		vals[n++] = wc->width;
	if (mask & XCB_CONFIG_WINDOW_HEIGHT)
		vals[n++] = wc->height;
	if (mask & XCB_CONFIG_WINDOW_BORDER_WIDTH)
		vals[n++] = wc->border_width;
	if (mask & XCB_CONFIG_WINDOW_SIBLING)
		vals[n++] = wc->sibling;
	if (mask & XCB_CONFIG_WINDOW_STACK_MODE)
		vals[n++] = wc->stack_mode;

	xmask = mask & (XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_BORDER_WIDTH |
			XCB_CONFIG_WINDOW_SIBLING | XCB_CONFIG_WINDOW_STACK_MODE);
	xcb_configure_window(dpy, w, xmask, vals);
	return n;
}

bool wXGetGeometry(xcb_connection_t *dpy, xcb_drawable_t d, xcb_window_t *root, int *x, int *y,
		   unsigned int *width, unsigned int *height,
		   unsigned int *border_width, unsigned int *depth)
{
	xcb_get_geometry_cookie_t ck;
	xcb_get_geometry_reply_t *rep;

	ck = xcb_get_geometry(dpy, d);
	rep = xcb_get_geometry_reply(dpy, ck, NULL);
	if (!rep)
		return false;

	if (root)
		*root = rep->root;
	if (x)
		*x = (int16_t) rep->x;
	if (y)
		*y = (int16_t) rep->y;
	if (width)
		*width = rep->width;
	if (height)
		*height = rep->height;
	if (border_width)
		*border_width = rep->border_width;
	if (depth)
		*depth = rep->depth;
	free(rep);
	return true;
}

/* ---- properties / colours --------------------------------------------- */

void wXDeleteProperty(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop)
{
	xcb_delete_property(dpy, w, prop);
}

void wXFreeColors(xcb_connection_t *dpy, xcb_colormap_t cmap, uint32_t *pixels,
		  int npixels, uint32_t planes)
{
	uint32_t stack[16] = { 0 };
	uint32_t *vals = stack;
	int i;

	if (npixels > 16) {
		vals = wmalloc(npixels * sizeof(uint32_t));
	}
	for (i = 0; i < npixels; i++)
		vals[i] = pixels[i];
	xcb_free_colors(dpy, cmap, (uint32_t) planes, npixels, vals);
	if (vals != stack)
		wfree(vals);
}

bool wXQueryTree(xcb_connection_t *dpy, xcb_window_t w, xcb_window_t *root, xcb_window_t *parent,
		 xcb_window_t **children, unsigned int *nchildren)
{
	xcb_query_tree_cookie_t ck;
	xcb_query_tree_reply_t *rep;
	xcb_window_t *out = NULL;
	uint32_t n;

	ck = xcb_query_tree(dpy, w);
	rep = xcb_query_tree_reply(dpy, ck, NULL);
	if (rep == NULL)
		return false;

	n = xcb_query_tree_children_length(rep);
	if (n > 0) {
		/* copy out of the reply, which is released below */
		const xcb_window_t *kids = xcb_query_tree_children(rep);
		uint32_t i;

		out = wmalloc(n * sizeof(xcb_window_t));
		for (i = 0; i < n; i++)
			out[i] = kids[i];
	}
	if (root)
		*root = rep->root;
	if (parent)
		*parent = rep->parent;
	if (children)
		*children = out;
	else if (out)
		wfree(out);
	if (nchildren)
		*nchildren = n;
	free(rep);
	return true;
}

bool wXGetWindowAttributes(xcb_connection_t *dpy, xcb_window_t w, WXWindowAttributes *out,
			   xcb_visualid_t *vid_ret)
{
	WXCookie ack, gck;
	WXWindowAttrReply xa;
	WXGeometry xg;

	ack = WXReqWindowAttributes(dpy, w);
	gck = WXReqGeometry(dpy, w);
	if (!WXRcvWindowAttributes(dpy, ack, &xa))
		return false;
	if (!WXRcvGeometry(dpy, gck, &xg))
		return false;

	memset(out, 0, sizeof(*out));
	out->override_redirect = xa.override_redirect;
	out->map_state = xa.map_state;
	out->class = xa.window_class;
	out->colormap = xa.colormap;
	out->visual = wXVisualTypeForID(dpy, (xcb_visualid_t) xa.visual);
	out->all_event_masks = xa.all_event_masks;
	out->your_event_mask = xa.your_event_mask;
	out->root = xg.root;
	out->x = xg.x;
	out->y = xg.y;
	out->width = xg.width;
	out->height = xg.height;
	out->border_width = xg.border_width;
	out->depth = xg.depth;
	if (vid_ret)
		*vid_ret = (xcb_visualid_t) xa.visual;
	return true;
}

bool wXTranslateCoordinates(xcb_connection_t *dpy, xcb_window_t src, xcb_window_t dst,
			    int src_x, int src_y,
			    int *dest_x, int *dest_y, xcb_window_t *child)
{
	xcb_translate_coordinates_cookie_t ck;
	xcb_translate_coordinates_reply_t *rep;

	ck = xcb_translate_coordinates(dpy, src, dst, src_x, src_y);
	rep = xcb_translate_coordinates_reply(dpy, ck, NULL);
	if (rep == NULL)
		return false;
	if (dest_x)
		*dest_x = rep->dst_x;
	if (dest_y)
		*dest_y = rep->dst_y;
	if (child)
		*child = rep->child;
	free(rep);
	return true;
}

uint32_t wXWindowAttrValues(const WXSetWindowAttributes *attr, uint32_t mask,
			    uint32_t *vals)
{
	int nv = 0;

	/* one slot per mask bit, wire order = bit order */
	if (mask & XCB_CW_BACK_PIXMAP)
		vals[nv++] = attr->background_pixmap;
	if (mask & XCB_CW_BACK_PIXEL)
		vals[nv++] = attr->background_pixel;
	if (mask & XCB_CW_BORDER_PIXMAP)
		vals[nv++] = attr->border_pixmap;
	if (mask & XCB_CW_BORDER_PIXEL)
		vals[nv++] = attr->border_pixel;
	if (mask & XCB_CW_BIT_GRAVITY)
		vals[nv++] = attr->bit_gravity;
	if (mask & XCB_CW_WIN_GRAVITY)
		vals[nv++] = attr->win_gravity;
	if (mask & XCB_CW_BACKING_STORE)
		vals[nv++] = attr->backing_store;
	if (mask & XCB_CW_BACKING_PLANES)
		vals[nv++] = attr->backing_planes;
	if (mask & XCB_CW_BACKING_PIXEL)
		vals[nv++] = attr->backing_pixel;
	if (mask & XCB_CW_OVERRIDE_REDIRECT)
		vals[nv++] = attr->override_redirect;
	if (mask & XCB_CW_SAVE_UNDER)
		vals[nv++] = attr->save_under;
	if (mask & XCB_CW_EVENT_MASK)
		vals[nv++] = attr->event_mask;
	if (mask & XCB_CW_DONT_PROPAGATE)
		vals[nv++] = attr->do_not_propagate_mask;
	if (mask & XCB_CW_COLORMAP)
		vals[nv++] = attr->colormap;
	if (mask & XCB_CW_CURSOR)
		vals[nv++] = attr->cursor;
	return mask;
}

int wXGrabPointer(xcb_connection_t *dpy, xcb_window_t grab, bool owner_events,
		  unsigned int event_mask, int pointer_mode, int keyboard_mode,
		  xcb_window_t confine_to, xcb_cursor_t cursor, xcb_timestamp_t time)
{
	xcb_grab_pointer_cookie_t ck;
	xcb_grab_pointer_reply_t *rep;
	int status;

	ck = xcb_grab_pointer(dpy, owner_events, grab, event_mask,
			      pointer_mode, keyboard_mode, confine_to,
			      (xcb_cursor_t) cursor, time);
	rep = xcb_grab_pointer_reply(dpy, ck, NULL);
	if (rep == NULL)
		return -1;	/* connection failure, no wire status */
	status = rep->status;
	free(rep);
	return status;
}

void wXUngrabPointer(xcb_connection_t *dpy, xcb_timestamp_t time)
{
	xcb_ungrab_pointer(dpy, time);
}

int wXGrabKeyboard(xcb_connection_t *dpy, xcb_window_t grab, bool owner_events,
		   int pointer_mode, int keyboard_mode, xcb_timestamp_t time)
{
	xcb_grab_keyboard_cookie_t ck;
	xcb_grab_keyboard_reply_t *rep;
	int status;

	ck = xcb_grab_keyboard(dpy, owner_events, grab, time,
			       pointer_mode, keyboard_mode);
	rep = xcb_grab_keyboard_reply(dpy, ck, NULL);
	if (rep == NULL)
		return -1;	/* connection failure, no wire status */
	status = rep->status;
	free(rep);
	return status;
}

void wXUngrabKeyboard(xcb_connection_t *dpy, xcb_timestamp_t time)
{
	xcb_ungrab_keyboard(dpy, time);
}

void wXChangeActivePointerGrab(xcb_connection_t *dpy, unsigned int event_mask,
			       xcb_cursor_t cursor, xcb_timestamp_t time)
{
	xcb_change_active_pointer_grab(dpy, (xcb_cursor_t) cursor,
				       time, event_mask);
}

void wXAllowEvents(xcb_connection_t *dpy, int mode, xcb_timestamp_t time)
{
	xcb_allow_events(dpy, mode, time);
}

void wXWarpPointer(xcb_connection_t *dpy, xcb_window_t src, xcb_window_t dst,
		   int src_x, int src_y, unsigned int src_w, unsigned int src_h,
		   int dst_x, int dst_y)
{
	xcb_warp_pointer(dpy, src, dst, src_x, src_y, src_w, src_h,
			 dst_x, dst_y);
}

bool wXQueryPointer(xcb_connection_t *dpy, xcb_window_t w, xcb_window_t *root, xcb_window_t *child,
		    int *root_x, int *root_y, int *win_x, int *win_y,
		    unsigned int *mask)
{
	xcb_query_pointer_cookie_t ck;
	xcb_query_pointer_reply_t *rep;

	ck = xcb_query_pointer(dpy, w);
	rep = xcb_query_pointer_reply(dpy, ck, NULL);
	if (rep == NULL)
		return false;
	if (root)
		*root = rep->root;
	if (child)
		*child = rep->child;
	if (root_x)
		*root_x = rep->root_x;
	if (root_y)
		*root_y = rep->root_y;
	if (win_x)
		*win_x = rep->win_x;
	if (win_y)
		*win_y = rep->win_y;
	if (mask)
		*mask = rep->mask;
	free(rep);
	return true;
}

/* XQueryPointer's cross-screen semantics: false when the pointer is on
 * another screen than `w` (or on error), the button state in `mask`
 * otherwise */
bool wXQueryPointerSameScreen(xcb_connection_t *dpy, xcb_window_t w,
			      unsigned int *mask)
{
	xcb_query_pointer_cookie_t ck;
	xcb_query_pointer_reply_t *rep;
	bool same_screen;

	ck = xcb_query_pointer(dpy, w);
	rep = xcb_query_pointer_reply(dpy, ck, NULL);
	if (rep == NULL)
		return false;
	same_screen = rep->same_screen != 0;
	if (mask)
		*mask = rep->mask;
	free(rep);
	return same_screen;
}

bool wXGrabServer(xcb_connection_t *dpy)
{
	return WXCheckVoid(dpy, xcb_grab_server_checked(dpy));
}

void wXUngrabServer(xcb_connection_t *dpy)
{
	xcb_ungrab_server(dpy);
}

bool WXCheckVoid(xcb_connection_t *dpy, xcb_void_cookie_t ck)
{
	xcb_generic_error_t *err;

	err = xcb_request_check(dpy, ck);
	if (err) {
		free(err);
		return false;
	}
	return true;
}

void wXGrabKey(xcb_connection_t *dpy, int keycode, unsigned int modifiers,
	       xcb_window_t grab_window, bool owner_events, int pointer_mode,
	       int keyboard_mode)
{
	xcb_grab_key(dpy, owner_events, grab_window, modifiers,
		     keycode, pointer_mode, keyboard_mode);
}

void wXUngrabKey(xcb_connection_t *dpy, int keycode, unsigned int modifiers,
		 xcb_window_t grab_window)
{
	xcb_ungrab_key(dpy, keycode, grab_window, modifiers);
}

void wXGrabButton(xcb_connection_t *dpy, unsigned int button, unsigned int modifiers,
		  xcb_window_t grab_window, bool owner_events, unsigned int event_mask,
		  int pointer_mode, int keyboard_mode, xcb_window_t confine_to,
		  xcb_cursor_t cursor)
{
	xcb_grab_button(dpy, owner_events, grab_window, event_mask,
			pointer_mode, keyboard_mode, confine_to,
			(xcb_cursor_t) cursor, button, modifiers);
}

void wXUngrabButton(xcb_connection_t *dpy, unsigned int button, unsigned int modifiers,
		    xcb_window_t grab_window)
{
	xcb_ungrab_button(dpy, button, grab_window, modifiers);
}

bool wXAllocColor(xcb_connection_t *dpy, xcb_colormap_t cmap, WXColor *color)
{
	xcb_alloc_color_cookie_t ck;
	xcb_alloc_color_reply_t *rep;

	ck = xcb_alloc_color(dpy, cmap, color->red, color->green,
			     color->blue);
	rep = xcb_alloc_color_reply(dpy, ck, NULL);
	if (rep == NULL)
		return false;
	color->pixel = rep->pixel;
	/* the reply components are the exact values the entry was
	 * created with, Xlib stored those back into the caller's XColor */
	color->red = rep->red;
	color->green = rep->green;
	color->blue = rep->blue;
	free(rep);
	return true;
}

/* The numeric colour forms, parsed client-side exactly as libX11's
 * XParseColor did: #RRR... (3..12 digits, groups of 3) and
 * rgb:RR/GG/BB (1..4 digits per component). A group is scaled to
 * 16 bits with the Xlib multiplication table. Returns false when the
 * spec is not one of these two forms or when the form is malformed —
 * the caller keeps Xlib's contract of NOT consulting the server for
 * malformed numeric specs. */
bool WXParseNumericColor(const char *spec, WXColor *color)
{
	size_t len;
	const char *p;
	int r, g, b;

	if (spec == NULL || spec[0] == '\0')
		return false;
	len = strlen(spec);

	if (spec[0] == '#') {
		const char *hex = "0123456789abcdef";
		static const uint16_t mult[5] = { 0, 0x1111, 0x101, 0x11, 0x1 };
		int val, i, ndigits;

		p = spec + 1;
		ndigits = 0;
		while (p[ndigits] != '\0') {
			for (i = 0; i < 16; i++)
				if (hex[i] == (p[ndigits] | 0x20))
					break;
			if (i == 16)
				return false;
			ndigits++;
		}
		if (ndigits % 3 != 0 || ndigits < 3 || ndigits > 12)
			return false;
		ndigits /= 3;
		r = g = b = 0;
		for (i = 0; i < ndigits; i++) {
			val = p[i] <= '9' ? p[i] - '0' : (p[i] | 0x20) - 'a' + 10;
			r = r * 16 + val;
		}
		for (i = 0; i < ndigits; i++) {
			val = p[ndigits + i] <= '9' ? p[ndigits + i] - '0' : (p[ndigits + i] | 0x20) - 'a' + 10;
			g = g * 16 + val;
		}
		for (i = 0; i < ndigits; i++) {
			val = p[2 * ndigits + i] <= '9' ? p[2 * ndigits + i] - '0' : (p[2 * ndigits + i] | 0x20) - 'a' + 10;
			b = b * 16 + val;
		}
		color->red = r * mult[ndigits];
		color->green = g * mult[ndigits];
		color->blue = b * mult[ndigits];
		return true;
	}

	if (len > 4 && spec[0] == 'r' && spec[1] == 'g'
	    && spec[2] == 'b' && spec[3] == ':') {
		/* rgb:RR/GG/BB with 1..4 digits per component */
		static const uint16_t mult[5] = { 0, 0x1111, 0x101, 0x11, 0x1 };
		int comp[3];
		int ci, digits, val;

		p = spec + 4;
		for (ci = 0; ci < 3; ci++) {
			comp[ci] = 0;
			digits = 0;
			while (*p != '\0' && *p != '/' && digits < 4) {
				if (*p >= '0' && *p <= '9')
					val = *p - '0';
				else if ((*p | 0x20) >= 'a' && (*p | 0x20) <= 'f')
					val = (*p | 0x20) - 'a' + 10;
				else
					return false;
				comp[ci] = comp[ci] * 16 + val;
				p++;
				digits++;
			}
			if (digits == 0)
				return false;
			comp[ci] *= mult[digits];
			if (ci < 2) {
				if (*p != '/')
					return false;
				p++;
			}
		}
		if (*p != '\0')
			return false;
		color->red = comp[0];
		color->green = comp[1];
		color->blue = comp[2];
		return true;
	}

	return false;
}

bool wXParseColor(xcb_connection_t *dpy, xcb_colormap_t cmap, const char *spec, WXColor *color)
{
	xcb_lookup_color_cookie_t ck;
	xcb_lookup_color_reply_t *rep;
	size_t len;

	if (spec == NULL)
		return false;
	len = strlen(spec);
	if (len == 0)
		return false;

	/* numeric forms are parsed client-side, as Xlib did */
	if (WXParseNumericColor(spec, color))
		return true;

	/* Xlib failed a malformed numeric form outright and only
	 * consulted LookupColor for named colours */
	if (spec[0] == '#')
		return false;
	if (len > 255)
		return false;
	if (len > 4 && (spec[0] | 0x20) == 'r' && (spec[1] | 0x20) == 'g'
	    && (spec[2] | 0x20) == 'b' && spec[3] == ':')
		return false;

	ck = xcb_lookup_color(dpy, cmap, len, spec);
	rep = xcb_lookup_color_reply(dpy, ck, NULL);
	if (rep == NULL)
		return false;
	color->red = rep->exact_red;
	color->green = rep->exact_green;
	color->blue = rep->exact_blue;
	free(rep);
	return true;
}

bool wXQueryColor(xcb_connection_t *dpy, xcb_colormap_t cmap, WXColor *color)
{
	xcb_query_colors_cookie_t ck;
	xcb_query_colors_reply_t *rep;
	xcb_rgb_t *rgb;
	uint32_t pixel;

	pixel = color->pixel;
	ck = xcb_query_colors(dpy, cmap, 1, &pixel);
	rep = xcb_query_colors_reply(dpy, ck, NULL);
	if (rep == NULL)
		return false;
	rgb = xcb_query_colors_colors(rep);
	color->red = rgb->red;
	color->green = rgb->green;
	color->blue = rgb->blue;
	free(rep);
	return true;
}

xcb_visualtype_t *wXVisualTypeForID(xcb_connection_t *dpy, xcb_visualid_t vid)
{
	const xcb_setup_t *setup = xcb_get_setup(dpy);
	xcb_screen_iterator_t sit;
	xcb_depth_iterator_t dit;
	xcb_visualtype_iterator_t vit;

	if (vid == 0)
		return NULL;
	for (sit = xcb_setup_roots_iterator(setup); sit.rem;
	     xcb_screen_next(&sit)) {
		for (dit = xcb_screen_allowed_depths_iterator(sit.data);
		     dit.rem; xcb_depth_next(&dit)) {
			for (vit = xcb_depth_visuals_iterator(dit.data);
			     vit.rem; xcb_visualtype_next(&vit)) {
				if (vit.data->visual_id == vid)
					return vit.data;
			}
		}
	}
	return NULL;
}

xcb_visualtype_t *wXDefaultVisualType(xcb_connection_t *dpy, int screen_number)
{
	xcb_screen_iterator_t sit;

	sit = xcb_setup_roots_iterator(xcb_get_setup(dpy));
	for (; sit.rem; xcb_screen_next(&sit)) {
		if (screen_number == 0)
			return wXVisualTypeForID(dpy, sit.data->root_visual);
		screen_number--;
	}
	return NULL;
}

static xcb_cursor_context_t *cursor_ctx;
static xcb_connection_t *cursor_ctx_conn;

xcb_cursor_t wXLoadCursor(xcb_connection_t *dpy, int screen_number, const char *name)
{
	xcb_screen_t *scr;

	/* the Xcursor theme is server-global, so the cached context is
	 * built from screen 0 and screen_number is ignored */
	(void)screen_number;

	if (cursor_ctx && cursor_ctx_conn != dpy) {
		xcb_cursor_context_free(cursor_ctx);
		cursor_ctx = NULL;
	}
	if (cursor_ctx == NULL) {
		scr = wXScreenOf(dpy, 0);
		if (!scr || xcb_cursor_context_new(dpy, scr, &cursor_ctx) < 0)
			return XCB_CURSOR_NONE;
		cursor_ctx_conn = dpy;
	}
	return xcb_cursor_load_cursor(cursor_ctx, name);
}

xcb_cursor_t wXCreatePixmapCursor(xcb_connection_t *dpy, xcb_pixmap_t source, xcb_pixmap_t mask,
				  const WXColor *foreground,
				  const WXColor *background,
				  unsigned int x, unsigned int y)
{
	xcb_cursor_t cid;

	cid = xcb_generate_id(dpy);
	xcb_create_cursor(dpy, cid, source, mask,
			  foreground->red, foreground->green, foreground->blue,
			  background->red, background->green, background->blue,
			  x, y);
	return cid;
}

void wXDefineCursor(xcb_connection_t *dpy, xcb_window_t w, xcb_cursor_t cursor)
{
	xcb_change_window_attributes(dpy, w, XCB_CW_CURSOR,
				     (const uint32_t[]){ cursor });
}

void wXRecolorCursor(xcb_connection_t *dpy, xcb_cursor_t cursor, const WXColor *fg,
		     const WXColor *bg)
{
	xcb_recolor_cursor(dpy, cursor,
			   fg->red, fg->green, fg->blue,
			   bg->red, bg->green, bg->blue);
}

xcb_screen_t *wXScreenOf(xcb_connection_t *dpy, int screen_number)
{
	xcb_screen_iterator_t sit;

	sit = xcb_setup_roots_iterator(xcb_get_setup(dpy));
	for (; sit.rem; xcb_screen_next(&sit)) {
		if (screen_number == 0)
			return sit.data;
		screen_number--;
	}
	return NULL;
}

int wXScreenCount(xcb_connection_t *dpy)
{
	return xcb_setup_roots_iterator(xcb_get_setup(dpy)).rem;
}

xcb_window_t wXRootWindow(xcb_connection_t *dpy, int screen_number)
{
	xcb_screen_t *xscr = wXScreenOf(dpy, screen_number);

	return xscr ? xscr->root : 0;
}

uint8_t *WXBMPackWire(const char *bits, unsigned int width,
		      unsigned int rows, size_t *wire_len)
{
	unsigned int row_bytes, wire_row;
	uint8_t *wire;
	unsigned int y;

	if (width == 0 || rows == 0) {
		if (wire_len)
			*wire_len = 0;
		return NULL;
	}

	row_bytes = (width + 7) / 8;
	wire_row = (width + 31) / 32 * 4;
	wire = wmalloc((size_t) rows * wire_row);
	for (y = 0; y < rows; y++)
		memcpy(wire + (size_t) y * wire_row,
		       bits + (size_t) y * row_bytes, row_bytes);
	if (wire_len)
		*wire_len = (size_t) rows * wire_row;
	return wire;
}

xcb_pixmap_t wXCreateBitmapFromData(xcb_connection_t *dpy, xcb_drawable_t d, const char *bits,
			      unsigned int width, unsigned int height)
{
	xcb_gcontext_t gc;
	uint32_t vals[3] = { XCB_GX_COPY, 1, 0 };
	xcb_pixmap_t pix;
	unsigned int row_bytes, wire_row, max_rows, max_data;
	unsigned int done, n;
	size_t wire_len;
	uint8_t *wire;
	bool msb_bits;

	pix = xcb_generate_id(dpy);
	xcb_create_pixmap(dpy, 1, pix, d, width, height);
	if (bits == NULL || width == 0 || height == 0 || width > 32768 || height > 32768)
		return pix;

	gc = xcb_generate_id(dpy);
	xcb_create_gc(dpy, gc, pix, XCB_GC_FUNCTION | XCB_GC_FOREGROUND
		      | XCB_GC_BACKGROUND, vals);

	/* XBM packs the leftmost pixel in the least significant bit.
	 * Mirror every byte for an MSB-first server. Repack and chunk
	 * below the maximum request length. */
	row_bytes = (width + 7) / 8;
	wire_row = (width + 31) / 32 * 4;
	max_data = xcb_get_maximum_request_length(dpy) * 4 - 1024;
	max_rows = max_data / wire_row;
	if (max_rows == 0)
		max_rows = 1;
	msb_bits = (xcb_get_setup(dpy)->bitmap_format_bit_order
		    == XCB_IMAGE_ORDER_MSB_FIRST);

	for (done = 0; done < height; done += n) {
		n = (height - done < max_rows) ? height - done : max_rows;
		wire = WXBMPackWire(bits + (size_t) done * row_bytes, width, n,
				    &wire_len);
		if (msb_bits) {
			size_t i;

			for (i = 0; i < wire_len; i++) {
				uint8_t b = wire[i];

				wire[i] = (uint8_t)(((b & 0x01) << 7) | ((b & 0x02) << 5)
					      | ((b & 0x04) << 3) | ((b & 0x08) << 1)
					      | ((b & 0x10) >> 1) | ((b & 0x20) >> 3)
					      | ((b & 0x40) >> 5) | ((b & 0x80) >> 7));
			}
		}
		xcb_put_image(dpy, XCB_IMAGE_FORMAT_XY_PIXMAP, pix, gc,
			      width, n, 0, (int16_t) done, 0, 1, wire_len, wire);
		wfree(wire);
	}
	xcb_free_gc(dpy, gc);
	return pix;
}

xcb_window_t wXGetSelectionOwner(xcb_connection_t *dpy, xcb_atom_t selection)
{
	xcb_get_selection_owner_cookie_t ck;
	xcb_get_selection_owner_reply_t *rep;
	xcb_window_t owner;

	ck = xcb_get_selection_owner(dpy, selection);
	rep = xcb_get_selection_owner_reply(dpy, ck, NULL);
	if (rep == NULL)
		return 0;
	owner = rep->owner;
	free(rep);
	return owner;
}

void wXSetSelectionOwner(xcb_connection_t *dpy, xcb_atom_t selection, xcb_window_t owner, xcb_timestamp_t time)
{
	xcb_set_selection_owner(dpy, owner, selection, time);
}

bool wXConvertSelection(xcb_connection_t *dpy, xcb_atom_t selection, xcb_atom_t target, xcb_atom_t property,
			xcb_window_t requestor, xcb_timestamp_t time)
{
	return WXCheckVoid(dpy,
			   xcb_convert_selection_checked(dpy, requestor, selection,
							 target, property, time));
}

char *wXGetAtomName(xcb_connection_t *dpy, xcb_atom_t atom)
{
	xcb_get_atom_name_cookie_t ck;
	xcb_get_atom_name_reply_t *rep;
	char *name = NULL;
	int len;

	ck = xcb_get_atom_name(dpy, atom);
	rep = xcb_get_atom_name_reply(dpy, ck, NULL);
	if (rep == NULL)
		return NULL;
	len = xcb_get_atom_name_name_length(rep);
	name = malloc(len + 1);
	if (name) {
		memcpy(name, xcb_get_atom_name_name(rep), len);
		name[len] = '\0';
	}
	free(rep);
	return name;
}

/* --- atom intern cache ------------------------------------------------ */

/* Xlib cached XInternAtom internally, xcb pays one round trip per call,
 * so libWINGs keeps its own cache. Outer table: connection pointer ->
 * per-connection table. Inner table: name -> atom. Names are borrowed,
 * not copied, callers pass literals or long-lived strings. A failed
 * intern is not cached, so a connection that recovers (or a new
 * connection reusing a freed pointer slot) re-interns. */
static WMHashTable *wxc_atom_cache;

static WMHashTable *wxcAtomTable(xcb_connection_t *xc)
{
	WMHashTable *inner;

	if (wxc_atom_cache == NULL)
		wxc_atom_cache = WMCreateHashTable(WMIntHashCallbacks);

	inner = WMHashGet(wxc_atom_cache, xc);
	if (inner == NULL) {
		inner = WMCreateHashTable(WMStringPointerHashCallbacks);
		WMHashInsert(wxc_atom_cache, xc, inner);
	}
	return inner;
}

xcb_atom_t WXInternAtom(xcb_connection_t *dpy, const char *name)
{
	WMHashTable *table;
	xcb_connection_t *xc;
	uintptr_t cached;
	xcb_intern_atom_cookie_t ck;
	xcb_intern_atom_reply_t *rep;

	table = wxcAtomTable(dpy);
	cached = (uintptr_t)WMHashGet(table, name);
	if (cached != 0)
		return (xcb_atom_t)cached;

	xc = dpy;
	ck = xcb_intern_atom(xc, 0, strlen(name), name);
	rep = xcb_intern_atom_reply(xc, ck, NULL);
	if (rep == NULL)
		return XCB_ATOM_NONE;

	cached = rep->atom;
	/* the cache keeps its own copy of the key: callers may pass
	 * stack buffers (W_ScreenCheckCompositor, screen.c's WM_S%d), and
	 * a borrowed pointer into a dead frame poisons every later
	 * lookup. One copy per distinct name for the process lifetime,
	 * the same shape Xlib's intern cache has. */
	WMHashInsert(table, wstrdup(name), (void *)cached);
	free(rep);
	return (xcb_atom_t)cached;
}

void WXInternAtomBatch(xcb_connection_t *dpy, char **names, int count, xcb_atom_t *atoms)
{
	WMHashTable *table;
	xcb_connection_t *xc;
	xcb_intern_atom_cookie_t *cks;
	char *have_ck;
	int i;

	if (count <= 0)
		return;

	xc = dpy;
	table = wxcAtomTable(xc);
	cks = wmalloc(sizeof(xcb_intern_atom_cookie_t) * count);
	have_ck = wmalloc(count);

	for (i = 0; i < count; i++) {
		if ((uintptr_t)WMHashGet(table, names[i]) == 0) {
			cks[i] = xcb_intern_atom(xc, 0, strlen(names[i]), names[i]);
			have_ck[i] = 1;
		}
	}
	for (i = 0; i < count; i++) {
		uintptr_t cached = (uintptr_t)WMHashGet(table, names[i]);
		xcb_intern_atom_reply_t *rep;

		if (cached != 0) {
			if (have_ck[i]) {
				/* a later duplicate of a name interned
				 * earlier in the batch: its reply must
				 * still be collected or it stays pinned
				 * in xcb's queue until disconnect */
				rep = xcb_intern_atom_reply(xc, cks[i], NULL);
				free(rep);
			}
			atoms[i] = (xcb_atom_t)cached;
			continue;
		}
		rep = xcb_intern_atom_reply(xc, cks[i], NULL);
		if (rep == NULL) {
			atoms[i] = XCB_ATOM_NONE;
			continue;
		}
		WMHashInsert(table, wstrdup(names[i]), (void *)(uintptr_t)rep->atom);
		atoms[i] = (xcb_atom_t)rep->atom;
		free(rep);
	}
	wfree(cks);
	wfree(have_ck);
}

xcb_atom_t wx_static_atoms[W_ATOM_COUNT];

void WXStaticAtomsInit(xcb_connection_t *dpy)
{
#define WX_ATOM_NAME_(id, name)	name,
	static char *names[] = {
		WX_STATIC_ATOMS(WX_ATOM_NAME_)
	};
#undef WX_ATOM_NAME_
	static bool inited;

	if (inited)
		return;
	inited = true;

	WXInternAtomBatch(dpy, names, wlengthof(names), wx_static_atoms);
}

/* XBM parser for the X11 format: #define <name>_width/_height (and
 * optional _x_hot/_y_hot) followed by "static ... char <name>_bits[] =
 * { ... };". The ancient X10 "static short" layout is rejected with
 * BitmapFileInvalid, every XBM file emitted in decades uses the char
 * format. Values are comma-separated C number literals. */
WXBitmapStatus WXBitmapParseFile(FILE *f, WXBitmapData *out)
{
	char line[512];
	unsigned int w = 0, h = 0;
	int hx = -1, hy = -1;
	unsigned char *data = NULL;
	size_t datalen = 0, cap;
	bool in_data = false, done = false;
	long v;
	int d;

	memset(out, 0, sizeof(*out));
	out->x_hot = -1;
	out->y_hot = -1;

	rewind(f);
	while (!done && fgets(line, sizeof(line), f)) {
		char *p = line;

		while (*p == ' ' || *p == '\t')
			p++;
		if (strncmp(p, "#define", 7) == 0) {
			char nm[128];

			if (sscanf(p, "#define %127[^ ] %ld", nm, &v) != 2)
				continue;
			size_t nl = strlen(nm);
			if (nl > 6 && strcmp(nm + nl - 6, "_width") == 0)
				w = (unsigned int) v;
			else if (nl > 7 && strcmp(nm + nl - 7, "_height") == 0)
				h = (unsigned int) v;
			else if (nl > 6 && strcmp(nm + nl - 6, "_x_hot") == 0)
				hx = (int) v;
			else if (nl > 6 && strcmp(nm + nl - 6, "_y_hot") == 0)
				hy = (int) v;
			continue;
		}
		if (strncmp(p, "static", 6) != 0)
			continue;
		if (strstr(p, "short") != NULL)
			/* X10 XBM files carry a `short' data array, only
			 * the X11 `unsigned char' format is supported */
			return WXBitmapFileInvalid;
		if (strstr(p, "{") == NULL)
			continue;

		if (w == 0 || h == 0 || w > 32768 || h > 32768)
			break;
		cap = (size_t)((w + 7) / 8) * h;
		data = wmalloc(cap);
		in_data = true;
		for (;;) {
			int c = fgetc(f);

			if (c == '}' || c == EOF)
				break;
			if (c >= '0' && c <= '9')
				;
			else if ((c | 0x20) >= 'a' && (c | 0x20) <= 'f')
				;
			else
				continue;
			v = 0;
			for (;;) {
				if (c >= '0' && c <= '9')
					d = c - '0';
				else if ((c | 0x20) >= 'a' && (c | 0x20) <= 'f')
					d = (c | 0x20) - 'a' + 10;
				else
					break;
				v = v * 16 + d;
				c = fgetc(f);
				if (c == EOF)
					break;
				/* skip the 0x/0X prefix: the digits
				 * that follow still belong to this
				 * value (X11 XBM files spell every
				 * byte 0xNN) */
				if (v == 0 && d == 0 && (c == 'x' || c == 'X')) {
					c = fgetc(f);
					if (c == EOF)
						break;
				}
			}
			if (datalen < cap)
				data[datalen++] = (unsigned char) (v & 0xff);
		}
		done = in_data;
	}

	if (w == 0 || h == 0 || w > 32768 || h > 32768 || !done) {
		wfree(data);
		return WXBitmapFileInvalid;
	}

	out->width = w;
	out->height = h;
	out->x_hot = hx;
	out->y_hot = hy;
	out->data = data;
	out->data_len = datalen;
	return WXBitmapSuccess;
}

WXBitmapStatus wXReadBitmapFile(xcb_connection_t *dpy, xcb_drawable_t d, const char *filename,
				unsigned int *width, unsigned int *height,
				xcb_pixmap_t *bitmap, int *x_hot, int *y_hot)
{
	FILE *f;
	WXBitmapData bd;
	WXBitmapStatus status;

	f = fopen(filename, "r");
	if (f == NULL)
		return WXBitmapOpenFailed;

	status = WXBitmapParseFile(f, &bd);
	fclose(f);
	if (status != WXBitmapSuccess)
		return status;

	*bitmap = wXCreateBitmapFromData(dpy, d, (const char *) bd.data, bd.width, bd.height);
	if (width)
		*width = bd.width;
	if (height)
		*height = bd.height;
	if (x_hot)
		*x_hot = bd.x_hot;
	if (y_hot)
		*y_hot = bd.y_hot;
	wfree(bd.data);
	return WXBitmapSuccess;
}

static xcb_window_t default_root(xcb_connection_t *dpy, int screen)
{
	return wXRootWindow(dpy, screen);
}

void wXStoreBuffer(xcb_connection_t *dpy, int screen, const char *bytes, int nbytes, int buffer)
{
	xcb_window_t root;

	if (nbytes < 0 || buffer < 0 || buffer > 7)
		return;
	root = default_root(dpy, screen);
	xcb_change_property(dpy, XCB_PROP_MODE_REPLACE, root,
			    XCB_ATOM_CUT_BUFFER0 + buffer, XCB_ATOM_STRING, 8,
			    (uint32_t) nbytes, bytes);
}

char *wXFetchBuffer(xcb_connection_t *dpy, int screen, int *nbytes, int buffer)
{
	xcb_get_property_cookie_t ck;
	xcb_get_property_reply_t *rep;
	char *out = NULL;
	int n;

	if (nbytes)
		*nbytes = 0;
	if (buffer < 0 || buffer > 7)
		return NULL;

	/* long_length is in 32-bit units, 10000000 is Xlib's own cap */
	ck = xcb_get_property(dpy, false, default_root(dpy, screen),
			      XCB_ATOM_CUT_BUFFER0 + buffer,
			      XCB_ATOM_STRING, 0, 10000000);
	rep = xcb_get_property_reply(dpy, ck, NULL);
	if (rep != NULL && rep->type == XCB_ATOM_STRING && rep->format == 8) {
		n = xcb_get_property_value_length(rep);
		out = wmalloc(n + 1);
		memcpy(out, xcb_get_property_value(rep), n);
		out[n] = '\0';
		if (nbytes)
			*nbytes = n;
	}
	free(rep);
	return out;
}

void wXRotateBuffers(xcb_connection_t *dpy, int screen, int rotate)
{
	xcb_atom_t atoms[8];
	int i;

	for (i = 0; i < 8; i++)
		atoms[i] = XCB_ATOM_CUT_BUFFER0 + i;
	xcb_rotate_properties(dpy, default_root(dpy, screen), 8, rotate, atoms);
}

/* ICCCM 4.1.4 withdraw: unmap plus a synthetic UnmapNotify to the root. */
bool wXWithdrawWindow(xcb_connection_t *dpy, xcb_window_t w, int screen_number)
{
	xcb_unmap_notify_event_t ev;
	xcb_window_t root = wXRootWindow(dpy, screen_number);

	xcb_unmap_window(dpy, w);

	memset(&ev, 0, sizeof(ev));
	ev.response_type = XCB_UNMAP_NOTIFY;
	ev.event = root;
	ev.window = w;
	WXSendEvent32(dpy, &ev, sizeof(ev), false, root,
		      XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY);
	return true;
}

/* Xlib's XRestackWindows: windows[i] is configured directly below
 * windows[i-1], windows[0] keeps its position. */
void wXRestackWindows(xcb_connection_t *dpy, xcb_window_t *windows, int n)
{
	int i;

	for (i = 1; i < n; i++) {
		xcb_configure_window(dpy, windows[i],
				     XCB_CONFIG_WINDOW_SIBLING | XCB_CONFIG_WINDOW_STACK_MODE,
				     (const uint32_t[]){ windows[i - 1], XCB_STACK_MODE_BELOW });
	}
}

/* Two-string WM_CLASS, res_name NUL res_class NUL, type STRING. */
/* Format-8 _WINDOWMAKER_COMMAND ClientMessage to the root window of
 * the given screen, the xcb-native twin of the retired Xlib-shaped
 * WXSendWmakerCommand(). Ends with a get_input_focus round trip so
 * one-shot callers disconnecting right after (setstyle's fallback)
 * cannot lose the one-way event: xcb_flush() followed immediately by
 * xcb_disconnect() can drop buffered requests, XCloseDisplay synced. */
void WXSendWmakerCommandXC(xcb_connection_t *dpy, int screen, const char *cmd)
{
	xcb_client_message_event_t ev;
	xcb_window_t root = default_root(dpy, screen);
	xcb_get_input_focus_cookie_t ck;
	xcb_get_input_focus_reply_t *rep;

	memset(&ev, 0, sizeof(ev));
	ev.response_type = XCB_CLIENT_MESSAGE;
	ev.format = 8;
	ev.window = root;
	WXStaticAtomsInit(dpy);
	ev.type = wx_static_atoms[W_ATOM_WINDOWMAKER_COMMAND];
	snprintf((char *)ev.data.data8, sizeof(ev.data.data8), "%s", cmd);
	WXSendEvent32(dpy, &ev, sizeof(ev), 0, root,
		      XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT);
	xcb_flush(dpy);
	/* a reply-bearing cookie, never xcb_request_check, the reply
	 * read is the barrier */
	ck = xcb_get_input_focus(dpy);
	rep = xcb_get_input_focus_reply(dpy, ck, NULL);
	if (rep)
		free(rep);
}

void wXSetClassHint(xcb_connection_t *dpy, xcb_window_t w, const char *res_name,
		    const char *res_class)
{
	size_t len1 = res_name ? strlen(res_name) : 0;
	size_t len2 = res_class ? strlen(res_class) : 0;
	char buf[512];

	if (len1 + len2 + 2 > sizeof(buf))
		return;
	memcpy(buf, res_name ? res_name : "", len1 + 1);
	memcpy(buf + len1 + 1, res_class ? res_class : "", len2 + 1);
	WXSetFormat8(dpy, w, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING,
		     (const unsigned char *) buf, len1 + len2 + 2);
}

void wXBell(xcb_connection_t *dpy, int percent)
{
	xcb_bell(dpy, percent);
}

void wXChangePointerControl(xcb_connection_t *dpy, bool do_accel, bool do_threshold,
			    int acc_num, int acc_denom, int threshold)
{
	xcb_change_pointer_control(dpy, acc_num, acc_denom,
				   threshold, do_accel, do_threshold);
}

/* Screen geometry from the xcb_setup (the Xlib WidthOfScreen/
 * HeightOfDisplay macros were Display*-bound). */
unsigned int wXDisplayWidth(xcb_connection_t *dpy, int screen_number)
{
	xcb_screen_iterator_t sit;

	sit = xcb_setup_roots_iterator(xcb_get_setup(dpy));
	for (; sit.rem; xcb_screen_next(&sit)) {
		if (screen_number == 0)
			return sit.data->width_in_pixels;
		screen_number--;
	}
	return 0;
}

unsigned int wXDisplayHeight(xcb_connection_t *dpy, int screen_number)
{
	xcb_screen_iterator_t sit;

	sit = xcb_setup_roots_iterator(xcb_get_setup(dpy));
	for (; sit.rem; xcb_screen_next(&sit)) {
		if (screen_number == 0)
			return sit.data->height_in_pixels;
		screen_number--;
	}
	return 0;
}

xcb_pixmap_t wXCreatePixmap(xcb_connection_t *dpy, xcb_drawable_t d, unsigned int width,
		      unsigned int height, unsigned int depth)
{
	xcb_pixmap_t pix;

	pix = xcb_generate_id(dpy);
	xcb_create_pixmap(dpy, depth, pix, d, width, height);
	return pix;
}

/* XCreatePixmapFromBitmapData equivalent: depth-1 bitmap from the XBM
 * bits, then a depth- pixmap painted through a fg/bg GC. */
xcb_pixmap_t wXCreatePixmapFromBitmapData(xcb_connection_t *dpy, xcb_drawable_t d, const char *bits,
				    unsigned int width, unsigned int height,
				    unsigned long fg, unsigned long bg,
				    unsigned int depth)
{
	xcb_pixmap_t pix, bitmap;
	xcb_gcontext_t gc;

	bitmap = wXCreateBitmapFromData(dpy, d, bits, width, height);

	pix = xcb_generate_id(dpy);
	xcb_create_pixmap(dpy, depth, pix, d, width, height);

	gc = xcb_generate_id(dpy);
	xcb_create_gc(dpy, gc, pix, XCB_GC_FUNCTION | XCB_GC_FOREGROUND
		      | XCB_GC_BACKGROUND | XCB_GC_PLANE_MASK,
		      (const uint32_t[]){ XCB_GX_COPY, fg, bg, ~0u });
	/* the bits come across as a plane, an XCopyArea between the
	 * depth-1 bitmap and the depth- pixmap would BadMatch */
	xcb_copy_plane(dpy, bitmap, pix, gc, 0, 0, 0, 0, width, height, 1);
	xcb_free_gc(dpy, gc);
	xcb_free_pixmap(dpy, bitmap);
	return pix;
}

/* XGetPointerControl equivalent, Xlib argument shape. */
void wXGetPointerControl(xcb_connection_t *dpy, int *acc_num, int *acc_denom,
			 int *threshold)
{
	xcb_get_pointer_control_cookie_t ck;
	xcb_get_pointer_control_reply_t *rep;

	ck = xcb_get_pointer_control(dpy);
	rep = xcb_get_pointer_control_reply(dpy, ck, NULL);
	if (!rep)
		return;
	if (acc_num)
		*acc_num = rep->acceleration_numerator;
	if (acc_denom)
		*acc_denom = rep->acceleration_denominator;
	if (threshold)
		*threshold = rep->threshold;
	free(rep);
}
