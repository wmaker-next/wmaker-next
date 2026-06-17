
/*
 *  WINGs xcb-to-XEvent cooker
 *
 *  W_CookXcbEvent() field-copies one xcb_generic_event_t into a
 *  caller-supplied XEvent so the WMHandleEvent / WMEventProc surface
 *  and every WINGs widget keep working unchanged after the event
 *  queue flips to xcb.  Core events (types 2..34) map 1:1 since the
 *  X protocol wire structs and the Xlib XEvent variants carry the
 *  same fields, the work is renaming and widening.  Extension events
 *  are not decoded, the xany prefix is filled so dispatch and the
 *  WXEvqTake* matchers keep working and the 32 wire bytes are stashed
 *  in WXWireEvent.wire for src/event.c and src/wmspec.c to cast to the
 *  matching xcb_*_event_t struct.
 *
 *  Errors (response_type 0) are not cooked, wxevq.c routes them to
 *  the WXEvqErrorHook before this is called.
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 */

#include "wconfig.h"

#include <string.h>
#include <X11/Xlib.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include "WINGsP.h"

_Static_assert(sizeof(WXWireEvent) == sizeof(XEvent),
	       "WXWireEvent must overlay XEvent exactly");
_Static_assert(offsetof(WXWireEvent, type) == offsetof(XAnyEvent, type)
	       && offsetof(WXWireEvent, window) == offsetof(XAnyEvent, window),
	       "WXWireEvent prefix must alias XAnyEvent");
/* the wire bytes sit in the pad[24] slack past every cooked variant,
 * XCrossingEvent is the largest at 104 bytes on LP64, an explicit
 * assert here so a future Xlib that grows a variant fails the build
 * instead of silently overwriting the raw bytes */
_Static_assert(sizeof(XCrossingEvent) <= offsetof(WXWireEvent, wire),
	       "WXWireEvent.wire would alias a cooked variant");
_Static_assert(sizeof(XConfigureRequestEvent) <= offsetof(WXWireEvent, wire),
	       "WXWireEvent.wire would alias a cooked variant");
_Static_assert(sizeof(XClientMessageEvent) <= offsetof(WXWireEvent, wire),
	       "WXWireEvent.wire would alias a cooked variant");

/* every core event sets out->xany.{type,serial,send_event,display} from
 * the generic header, then the per-type arm fills the variant struct,
 * the first Window-typed field of each variant aliases xany.window so
 * that comes for free */
#define COOK_HDR(out, dpy, xe) do {                                     \
		(out)->xany.type = (xe)->response_type & 0x7f;          \
		(out)->xany.serial = (xe)->full_sequence;               \
		(out)->xany.send_event = ((xe)->response_type & 0x80) != 0; \
		(out)->xany.display = (dpy);                            \
	} while (0)

void W_CookXcbEvent(Display *dpy, void *xcb_ev, XEvent *out)
{
	xcb_generic_event_t *xe = xcb_ev;
	int type = xe->response_type & 0x7f;

	memset(out, 0, sizeof(*out));
	COOK_HDR(out, dpy, xe);

	switch (type) {
	case XCB_KEY_PRESS:
	case XCB_KEY_RELEASE: {
		xcb_key_press_event_t *e = (xcb_key_press_event_t *)xe;
		out->xkey.window      = e->event;
		out->xkey.root        = e->root;
		out->xkey.subwindow   = e->child;
		out->xkey.time        = e->time;
		out->xkey.x           = e->event_x;
		out->xkey.y           = e->event_y;
		out->xkey.x_root      = e->root_x;
		out->xkey.y_root      = e->root_y;
		out->xkey.state       = e->state;
		out->xkey.keycode     = e->detail;
		out->xkey.same_screen = e->same_screen;
		break;
	}
	case XCB_BUTTON_PRESS:
	case XCB_BUTTON_RELEASE: {
		xcb_button_press_event_t *e = (xcb_button_press_event_t *)xe;
		out->xbutton.window      = e->event;
		out->xbutton.root        = e->root;
		out->xbutton.subwindow   = e->child;
		out->xbutton.time        = e->time;
		out->xbutton.x           = e->event_x;
		out->xbutton.y           = e->event_y;
		out->xbutton.x_root      = e->root_x;
		out->xbutton.y_root      = e->root_y;
		out->xbutton.state       = e->state;
		out->xbutton.button      = e->detail;
		out->xbutton.same_screen = e->same_screen;
		break;
	}
	case XCB_MOTION_NOTIFY: {
		xcb_motion_notify_event_t *e = (xcb_motion_notify_event_t *)xe;
		out->xmotion.window      = e->event;
		out->xmotion.root        = e->root;
		out->xmotion.subwindow   = e->child;
		out->xmotion.time        = e->time;
		out->xmotion.x           = e->event_x;
		out->xmotion.y           = e->event_y;
		out->xmotion.x_root      = e->root_x;
		out->xmotion.y_root      = e->root_y;
		out->xmotion.state       = e->state;
		out->xmotion.is_hint     = e->detail;
		out->xmotion.same_screen = e->same_screen;
		break;
	}
	case XCB_ENTER_NOTIFY:
	case XCB_LEAVE_NOTIFY: {
		xcb_enter_notify_event_t *e = (xcb_enter_notify_event_t *)xe;
		out->xcrossing.window    = e->event;
		out->xcrossing.root      = e->root;
		out->xcrossing.subwindow = e->child;
		out->xcrossing.time      = e->time;
		out->xcrossing.x         = e->event_x;
		out->xcrossing.y         = e->event_y;
		out->xcrossing.x_root    = e->root_x;
		out->xcrossing.y_root    = e->root_y;
		out->xcrossing.mode      = e->mode;
		out->xcrossing.detail    = e->detail;
		/* one wire byte carries both flags, 0x01 focus 0x02 same_screen */
		out->xcrossing.same_screen = (e->same_screen_focus & 0x02) != 0;
		out->xcrossing.focus       = (e->same_screen_focus & 0x01) != 0;
		out->xcrossing.state     = e->state;
		break;
	}
	case XCB_FOCUS_IN:
	case XCB_FOCUS_OUT: {
		xcb_focus_in_event_t *e = (xcb_focus_in_event_t *)xe;
		out->xfocus.window = e->event;
		out->xfocus.mode   = e->mode;
		out->xfocus.detail = e->detail;
		break;
	}
	case XCB_KEYMAP_NOTIFY: {
		/* only event with no wire sequence number, xcb still fills
		 * full_sequence from the connection counter so xany.serial
		 * is set above, the 31 key bytes cover keycodes 8..255 so
		 * land at key_vector[1..31] and Xlib leaves window unset */
		xcb_keymap_notify_event_t *e = (xcb_keymap_notify_event_t *)xe;
		out->xkeymap.window = None;
		memcpy(&out->xkeymap.key_vector[1], e->keys, 31);
		break;
	}
	case XCB_EXPOSE: {
		xcb_expose_event_t *e = (xcb_expose_event_t *)xe;
		out->xexpose.window = e->window;
		out->xexpose.x      = e->x;
		out->xexpose.y      = e->y;
		out->xexpose.width  = e->width;
		out->xexpose.height = e->height;
		out->xexpose.count  = e->count;
		break;
	}
	case XCB_GRAPHICS_EXPOSURE: {
		xcb_graphics_exposure_event_t *e = (xcb_graphics_exposure_event_t *)xe;
		out->xgraphicsexpose.drawable   = e->drawable;
		out->xgraphicsexpose.x          = e->x;
		out->xgraphicsexpose.y          = e->y;
		out->xgraphicsexpose.width      = e->width;
		out->xgraphicsexpose.height     = e->height;
		out->xgraphicsexpose.count      = e->count;
		out->xgraphicsexpose.major_code = e->major_opcode;
		out->xgraphicsexpose.minor_code = e->minor_opcode;
		break;
	}
	case XCB_NO_EXPOSURE: {
		xcb_no_exposure_event_t *e = (xcb_no_exposure_event_t *)xe;
		out->xnoexpose.drawable   = e->drawable;
		out->xnoexpose.major_code = e->major_opcode;
		out->xnoexpose.minor_code = e->minor_opcode;
		break;
	}
	case XCB_VISIBILITY_NOTIFY: {
		xcb_visibility_notify_event_t *e = (xcb_visibility_notify_event_t *)xe;
		out->xvisibility.window = e->window;
		out->xvisibility.state  = e->state;
		break;
	}
	case XCB_CREATE_NOTIFY: {
		xcb_create_notify_event_t *e = (xcb_create_notify_event_t *)xe;
		out->xcreatewindow.parent       = e->parent;
		out->xcreatewindow.window       = e->window;
		out->xcreatewindow.x            = e->x;
		out->xcreatewindow.y            = e->y;
		out->xcreatewindow.width        = e->width;
		out->xcreatewindow.height       = e->height;
		out->xcreatewindow.border_width = e->border_width;
		out->xcreatewindow.override_redirect = e->override_redirect;
		break;
	}
	case XCB_DESTROY_NOTIFY: {
		xcb_destroy_notify_event_t *e = (xcb_destroy_notify_event_t *)xe;
		out->xdestroywindow.event  = e->event;
		out->xdestroywindow.window = e->window;
		break;
	}
	case XCB_UNMAP_NOTIFY: {
		xcb_unmap_notify_event_t *e = (xcb_unmap_notify_event_t *)xe;
		out->xunmap.event          = e->event;
		out->xunmap.window         = e->window;
		out->xunmap.from_configure = e->from_configure;
		break;
	}
	case XCB_MAP_NOTIFY: {
		xcb_map_notify_event_t *e = (xcb_map_notify_event_t *)xe;
		out->xmap.event             = e->event;
		out->xmap.window            = e->window;
		out->xmap.override_redirect = e->override_redirect;
		break;
	}
	case XCB_MAP_REQUEST: {
		xcb_map_request_event_t *e = (xcb_map_request_event_t *)xe;
		out->xmaprequest.parent = e->parent;
		out->xmaprequest.window = e->window;
		break;
	}
	case XCB_REPARENT_NOTIFY: {
		xcb_reparent_notify_event_t *e = (xcb_reparent_notify_event_t *)xe;
		out->xreparent.event   = e->event;
		out->xreparent.window  = e->window;
		out->xreparent.parent  = e->parent;
		out->xreparent.x       = e->x;
		out->xreparent.y       = e->y;
		out->xreparent.override_redirect = e->override_redirect;
		break;
	}
	case XCB_CONFIGURE_NOTIFY: {
		xcb_configure_notify_event_t *e = (xcb_configure_notify_event_t *)xe;
		out->xconfigure.event        = e->event;
		out->xconfigure.window       = e->window;
		out->xconfigure.x            = e->x;
		out->xconfigure.y            = e->y;
		out->xconfigure.width        = e->width;
		out->xconfigure.height       = e->height;
		out->xconfigure.border_width = e->border_width;
		out->xconfigure.above        = e->above_sibling;
		out->xconfigure.override_redirect = e->override_redirect;
		break;
	}
	case XCB_CONFIGURE_REQUEST: {
		xcb_configure_request_event_t *e = (xcb_configure_request_event_t *)xe;
		out->xconfigurerequest.parent       = e->parent;
		out->xconfigurerequest.window       = e->window;
		out->xconfigurerequest.x            = e->x;
		out->xconfigurerequest.y            = e->y;
		out->xconfigurerequest.width        = e->width;
		out->xconfigurerequest.height       = e->height;
		out->xconfigurerequest.border_width = e->border_width;
		out->xconfigurerequest.above        = e->sibling;
		out->xconfigurerequest.detail       = e->stack_mode;
		out->xconfigurerequest.value_mask   = e->value_mask;
		break;
	}
	case XCB_GRAVITY_NOTIFY: {
		xcb_gravity_notify_event_t *e = (xcb_gravity_notify_event_t *)xe;
		out->xgravity.event  = e->event;
		out->xgravity.window = e->window;
		out->xgravity.x      = e->x;
		out->xgravity.y      = e->y;
		break;
	}
	case XCB_RESIZE_REQUEST: {
		xcb_resize_request_event_t *e = (xcb_resize_request_event_t *)xe;
		out->xresizerequest.window = e->window;
		out->xresizerequest.width  = e->width;
		out->xresizerequest.height = e->height;
		break;
	}
	case XCB_CIRCULATE_NOTIFY: {
		xcb_circulate_notify_event_t *e = (xcb_circulate_notify_event_t *)xe;
		out->xcirculate.event  = e->event;
		out->xcirculate.window = e->window;
		out->xcirculate.place  = e->place;
		break;
	}
	case XCB_CIRCULATE_REQUEST: {
		xcb_circulate_request_event_t *e = (xcb_circulate_request_event_t *)xe;
		out->xcirculaterequest.parent = e->event;
		out->xcirculaterequest.window = e->window;
		out->xcirculaterequest.place  = e->place;
		break;
	}
	case XCB_PROPERTY_NOTIFY: {
		xcb_property_notify_event_t *e = (xcb_property_notify_event_t *)xe;
		out->xproperty.window = e->window;
		out->xproperty.atom   = e->atom;
		out->xproperty.time   = e->time;
		out->xproperty.state  = e->state;
		break;
	}
	case XCB_SELECTION_CLEAR: {
		xcb_selection_clear_event_t *e = (xcb_selection_clear_event_t *)xe;
		out->xselectionclear.window    = e->owner;
		out->xselectionclear.selection = e->selection;
		out->xselectionclear.time      = e->time;
		break;
	}
	case XCB_SELECTION_REQUEST: {
		xcb_selection_request_event_t *e = (xcb_selection_request_event_t *)xe;
		out->xselectionrequest.owner     = e->owner;
		out->xselectionrequest.requestor = e->requestor;
		out->xselectionrequest.selection = e->selection;
		out->xselectionrequest.target    = e->target;
		out->xselectionrequest.property  = e->property;
		out->xselectionrequest.time      = e->time;
		break;
	}
	case XCB_SELECTION_NOTIFY: {
		xcb_selection_notify_event_t *e = (xcb_selection_notify_event_t *)xe;
		out->xselection.requestor = e->requestor;
		out->xselection.selection = e->selection;
		out->xselection.target    = e->target;
		out->xselection.property  = e->property;
		out->xselection.time      = e->time;
		break;
	}
	case XCB_COLORMAP_NOTIFY: {
		xcb_colormap_notify_event_t *e = (xcb_colormap_notify_event_t *)xe;
		out->xcolormap.window   = e->window;
		out->xcolormap.colormap = e->colormap;
#if defined(__cplusplus) || defined(c_plusplus)
		out->xcolormap.c_new    = e->_new;
#else
		out->xcolormap.new      = e->_new;
#endif
		out->xcolormap.state    = e->state;
		break;
	}
	case XCB_CLIENT_MESSAGE: {
		xcb_client_message_event_t *e = (xcb_client_message_event_t *)xe;
		int i;
		out->xclient.window       = e->window;
		out->xclient.message_type = e->type;
		out->xclient.format       = e->format;
		/* data.l is long[5] on Xlib, data32 is uint32_t[5] on xcb,
		 * copy element-wise so LP64 widens, format-8 and 16 are
		 * byte-compatible so a straight memcpy is fine for those */
		if (e->format == 32)
			for (i = 0; i < 5; i++)
				out->xclient.data.l[i] = e->data.data32[i];
		else
			memcpy(out->xclient.data.b, e->data.data8, 20);
		break;
	}
	case XCB_MAPPING_NOTIFY: {
		xcb_mapping_notify_event_t *e = (xcb_mapping_notify_event_t *)xe;
		out->xmapping.window        = None;
		out->xmapping.request       = e->request;
		out->xmapping.first_keycode = e->first_keycode;
		out->xmapping.count         = e->count;
		break;
	}
	default: {
		/* extension event or XGE GenericEvent, fill the xany prefix
		 * so dispatch on type and serial works, xany.window is set
		 * to whatever sits at wire offset 4 which is the affected
		 * window for Shape and the alarm id for XSync, callers that
		 * need a different field read it from .wire themselves */
		WXWireEvent *we = (WXWireEvent *)out;
		uint32_t at4;
		memcpy(&at4, (unsigned char *)xe + 4, sizeof(at4));
		we->window = at4;
		break;
	}
	}

	/* always stash the 32 wire bytes at the tail of the XEvent so the
	 * src/ dispatch table can hand the raw xcb struct to per-protocol
	 * handlers regardless of how the cooked event reached it (main
	 * loop, modal-loop default arm, WXEvqTakeTyped lookahead) */
	memcpy(((WXWireEvent *)out)->wire, xe, sizeof(((WXWireEvent *)out)->wire));
}
