/*
 * WINGs input method support
 *
 * Two paths are compiled in:
 *
 *  non-xcb (WPrefs, wmagnify): classic Xlib XIM via XOpenIM/XCreateIC/
 *    XFilterEvent/Xutf8LookupString.  Unchanged from the original code.
 *
 *  xcb (wmaker after WXEvqFlipToXcb): XFilterEvent and Xutf8LookupString are
 *    retired.  Dead-key / Compose sequences are handled by xkb_compose_state
 *    (unconditional fallback).  When HAVE_XCB_IMDKIT is defined, a protocol
 *    IM client (xcb_xim_t) is also created and fcitx5 / ibus connections are
 *    routed through it, committed text arrives via the commit_string callback
 *    and is inserted at the cursor of the focused view.
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This library is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Library General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 */

#include <locale.h>
#include <stdint.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <xkbcommon/xkbcommon-compose.h>

#include "wconfig.h"
#include "WINGsP.h"

#ifdef HAVE_XCB_IMDKIT
#include <xcb/xcb.h>
#include <X11/Xlib-xcb.h>
#include <xcb-imdkit/imclient.h>
#endif

/* -----------------------------------------------------------------------
 * Internal structure for the non-xcb (Xlib XIM) path
 * -------------------------------------------------------------------- */

typedef struct W_IMContext {
	XIM xim;
	XIMStyle ximstyle;
} WMIMContext;

/* -----------------------------------------------------------------------
 * Non-xcb callbacks (Xlib XIM)
 * -------------------------------------------------------------------- */

static void instantiateIM_cb(Display * display, XPointer client_data, XPointer call_data)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) display;
	(void) call_data;

	W_InitIM((W_Screen *) client_data);
}

static void destroyIM_cb(XIM xim, XPointer client_data, XPointer call_data)
{
	W_Screen *scr = (W_Screen *) client_data;
	W_View *target;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) call_data;

	if (scr->imctx->xim != xim)
		return;

	target = scr->rootView->childrenList;
	while (target != NULL) {
		W_DestroyIC(target);
		target = target->nextSister;
	}

	wfree(scr->imctx);
	scr->imctx = NULL;

	XRegisterIMInstantiateCallback(scr->display, NULL, NULL, NULL, instantiateIM_cb, (XPointer) scr);
}

/* -----------------------------------------------------------------------
 * xcb-path compose helper
 * -------------------------------------------------------------------- */

/*
 * W_IMComposeKey -- feed one keysym through the xkb compose state machine.
 *
 * Returns the number of UTF-8 bytes written to buf (>= 0).
 *  > 0  : compose sequence completed, text in buf, sym_out filled
 *  0    : still composing, cancelled, or keysym not in any sequence
 *
 * compose_state is void * so the unit tests can use it without the
 * HAVE_XCB_IMDKIT guard.
 */
int W_IMComposeKey(void *compose_state, KeySym ksym, char *buf, int buflen, KeySym *sym_out)
{
	struct xkb_compose_state *cs = (struct xkb_compose_state *)compose_state;

	if (!cs || ksym == NoSymbol)
		return 0;

	if (xkb_compose_state_feed(cs, ksym) != XKB_COMPOSE_FEED_ACCEPTED)
		return 0;

	switch (xkb_compose_state_get_status(cs)) {
	case XKB_COMPOSE_COMPOSED: {
		int n = xkb_compose_state_get_utf8(cs, buf, buflen);

		if (sym_out)
			*sym_out = xkb_compose_state_get_one_sym(cs);
		xkb_compose_state_reset(cs);
		return n;
	}
	case XKB_COMPOSE_CANCELLED:
		xkb_compose_state_reset(cs);
		return 0;
	case XKB_COMPOSE_COMPOSING:
	case XKB_COMPOSE_NOTHING:
		break;
	}
	return 0;
}

#ifdef HAVE_XCB_IMDKIT

/* -----------------------------------------------------------------------
 * xcb-path IM bypass ring -- prevents re-forwarding keys the IM returned
 * -------------------------------------------------------------------- */

#define IM_BYPASS_MAX 8
static unsigned long im_bypass_serials[IM_BYPASS_MAX];
static int im_bypass_head;
static int im_bypass_count;

static void im_bypass_mark(unsigned long serial)
{
	im_bypass_serials[im_bypass_head] = serial;
	im_bypass_head = (im_bypass_head + 1) % IM_BYPASS_MAX;
	if (im_bypass_count < IM_BYPASS_MAX)
		im_bypass_count++;
}

static Bool im_bypass_check(unsigned long serial)
{
	int i;

	for (i = 0; i < im_bypass_count; i++) {
		int idx = (im_bypass_head - 1 - i + IM_BYPASS_MAX) % IM_BYPASS_MAX;

		if (im_bypass_serials[idx] == serial) {
			/* compact this slot out of the ring */
			int j;

			for (j = i; j > 0; j--) {
				int cur  = (im_bypass_head - 1 - j + IM_BYPASS_MAX) % IM_BYPASS_MAX;
				int prev = (im_bypass_head     - j + IM_BYPASS_MAX) % IM_BYPASS_MAX;

				im_bypass_serials[cur] = im_bypass_serials[prev];
			}
			im_bypass_head = (im_bypass_head - 1 + IM_BYPASS_MAX) % IM_BYPASS_MAX;
			im_bypass_count--;
			return True;
		}
	}
	return False;
}

/* -----------------------------------------------------------------------
 * xcb-path IM callbacks (xcb-imdkit)
 * -------------------------------------------------------------------- */

/* find focused view with xcbxic == ic across all toplevels */
static W_View *find_xcbxic_view(W_Screen *scr, xcb_xic_t ic)
{
	W_FocusInfo *info;

	for (info = scr->focusInfo; info != NULL; info = info->next) {
		if (info->focused && info->focused->xcbxic == ic)
			return info->focused;
	}
	return NULL;
}

static void on_set_event_mask(xcb_xim_t *im, xcb_xic_t ic,
                              uint32_t forward_event_mask,
                              uint32_t synchronous_event_mask,
                              void *user_data)
{
	W_Screen *scr = (W_Screen *)user_data;
	W_View *view = find_xcbxic_view(scr, ic);

	(void)im;
	(void)synchronous_event_mask;

	if (view)
		view->xcbxic_fwd_mask = forward_event_mask;
}

static void on_commit_string(xcb_xim_t *im, xcb_xic_t ic,
                             uint32_t flag, char *str, uint32_t length,
                             uint32_t *keysym, size_t nkeysym,
                             void *user_data)
{
	W_Screen *scr = (W_Screen *)user_data;
	W_View *view;
	WMTextField *tf;
	char *text;

	(void)im;
	(void)flag;
	(void)keysym;
	(void)nkeysym;

	if (!str || length == 0)
		return;

	/* prefer the view that owns this IC, fall back to any focused textfield */
	view = find_xcbxic_view(scr, ic);
	if (!view) {
		W_FocusInfo *info;

		for (info = scr->focusInfo; info != NULL; info = info->next) {
			if (info->focused && info->focused->xcbxic) {
				view = info->focused;
				break;
			}
		}
	}
	if (!view || !view->self)
		return;
	if (W_CLASS(view->self) != WC_TextField)
		return;

	tf = (WMTextField *)view->self;
	text = wmalloc(length + 1);
	memcpy(text, str, length);
	text[length] = '\0';
	WMInsertTextFieldText(tf, text, WMGetTextFieldCursorPosition(tf));
	wfree(text);

	WMPostNotificationName(WMTextDidChangeNotification, tf,
	                       (void *)WMInsertTextEvent);
}

static void on_forward_event(xcb_xim_t *im, xcb_xic_t ic,
                             xcb_key_press_event_t *event,
                             void *user_data)
{
	W_Screen *scr = (W_Screen *)user_data;
	W_View *view = find_xcbxic_view(scr, ic);
	XEvent cooked;

	(void)im;

	if (!view)
		return;

	W_CookXcbEvent(scr->display, (xcb_generic_event_t *)event, &cooked);
	cooked.xkey.window = view->window;
	im_bypass_mark(cooked.xkey.serial);
	WXEvqPutBack(&cooked);
}

static void on_disconnected(xcb_xim_t *im, void *user_data)
{
	/* auto_connect=true in xcb_xim_open means xcb-imdkit will retry */
	(void)im;
	(void)user_data;
}

static void on_create_ic(xcb_xim_t *im, xcb_xic_t ic, void *user_data)
{
	W_View *view = (W_View *)user_data;

	(void)im;

	if (ic)
		view->xcbxic = ic;
}

static void on_im_open(xcb_xim_t *im, void *user_data)
{
	W_Screen *scr = (W_Screen *)user_data;
	W_View *child;

	(void)im;

	/* create ICs for views that realised while the IM was connecting */
	for (child = scr->rootView->childrenList; child != NULL; child = child->nextSister) {
		if (child->flags.realized && !child->xcbxic)
			W_CreateIC(child);
	}
}

/* global xcb_xim list for W_IMFilterEventGlobal (one entry per screen) */
#define MAX_XCB_IMS 4
static xcb_xim_t *g_xcb_ims[MAX_XCB_IMS];
static int g_xcb_im_count;

static void xcb_im_register(xcb_xim_t *im)
{
	if (g_xcb_im_count < MAX_XCB_IMS)
		g_xcb_ims[g_xcb_im_count++] = im;
}

#endif /* HAVE_XCB_IMDKIT */

/* -----------------------------------------------------------------------
 * W_IMFilterEventGlobal -- filter XIM protocol messages before view lookup
 *
 * Called from WMHandleEvent in the xcb path, before W_GetViewForXWindow,
 * because XIM protocol messages arrive on IM-internal windows that have
 * no matching W_View.
 * -------------------------------------------------------------------- */

Bool W_IMFilterEventGlobal(Display *dpy, XEvent *event)
{
#ifdef HAVE_XCB_IMDKIT
	xcb_generic_event_t *xcb_ev;
	int i;

	(void)dpy;

	if (g_xcb_im_count == 0)
		return False;

	xcb_ev = (xcb_generic_event_t *)((WXWireEvent *)event)->wire;

	for (i = 0; i < g_xcb_im_count; i++) {
		if (xcb_xim_filter_event(g_xcb_ims[i], xcb_ev))
			return True;
	}
#else
	(void)dpy;
	(void)event;
#endif
	return False;
}

/* -----------------------------------------------------------------------
 * W_IMForwardKey -- forward a key press to the IM, returns True if consumed
 *
 * Called at the top of handleTextFieldKeyPress.  Returns True when the key
 * was sent to the IM and text will arrive later via commit_string, the
 * caller should return without further processing.  Returns False for
 * navigation/modifier keysyms and when no xcb-imdkit IC exists.
 * -------------------------------------------------------------------- */

#ifdef HAVE_XCB_IMDKIT

static Bool should_forward_to_im(KeySym ksym)
{
	if (ksym == NoSymbol)
		return False;
	/* modifier keys */
	if (ksym >= XK_Shift_L && ksym <= XK_Hyper_R)
		return False;
#ifdef XK_ISO_Lock
	if (ksym >= XK_ISO_Lock && ksym <= XK_ISO_Last_Group_Lock)
		return False;
#endif
	/* 0xFF00..0xFFFF: BackSpace, Tab, Return, Escape, arrows, F-keys, Delete */
	if (ksym >= 0xFF00)
		return False;
	/* 0xFE00..0xFEFF: dead keys -- forward to IM so it handles compose */
	if (ksym >= 0xFE00)
		return True;
	/* printable range */
	if (ksym >= 0x0020)
		return True;
	return False;
}

Bool W_IMForwardKey(W_View *view, XEvent *event)
{
	W_Screen *scr;
	xcb_xim_t *im;
	KeySym ksym;
	xcb_key_press_event_t *wire;

	if (!view->xcbxic)
		return False;

	scr = view->screen;
	if (!scr || !scr->xcbim)
		return False;

	/* event was re-injected by forward_event callback: process normally */
	if (im_bypass_check(event->xkey.serial))
		return False;

	ksym = XLookupKeysym(&event->xkey, 0);
	if (!should_forward_to_im(ksym))
		return False;

	im = (xcb_xim_t *)scr->xcbim;
	wire = (xcb_key_press_event_t *)((WXWireEvent *)event)->wire;
	xcb_xim_forward_event(im, view->xcbxic, wire);
	return True;
}

#else

Bool W_IMForwardKey(W_View *view, XEvent *event)
{
	(void)view;
	(void)event;
	return False;
}

#endif /* HAVE_XCB_IMDKIT */

/* -----------------------------------------------------------------------
 * W_InitIM -- called once per screen at screen creation
 * -------------------------------------------------------------------- */

static void init_compose_state(W_Screen *scr)
{
	struct xkb_context *ctx;
	struct xkb_compose_table *table;
	const char *locale;

	if (scr->compose_state)
		return;

	ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!ctx)
		return;

	locale = setlocale(LC_CTYPE, NULL);
	if (!locale || locale[0] == '\0')
		locale = "C";

	table = xkb_compose_table_new_from_locale(ctx, locale, XKB_COMPOSE_COMPILE_NO_FLAGS);
	if (table) {
		scr->compose_state = xkb_compose_state_new(table, XKB_COMPOSE_STATE_NO_FLAGS);
		xkb_compose_table_unref(table);
	}
	xkb_context_unref(ctx);
}

#ifdef HAVE_XCB_IMDKIT
static void init_xcb_im(W_Screen *scr)
{
	static const xcb_xim_im_callback im_cbs = {
		.set_event_mask = on_set_event_mask,
		.forward_event  = on_forward_event,
		.commit_string  = on_commit_string,
		.disconnected   = on_disconnected,
	};
	xcb_connection_t *conn;
	xcb_xim_t *im;

	if (scr->xcbim)
		return;

	conn = XGetXCBConnection(scr->display);
	if (!conn)
		return;

	im = xcb_xim_create(conn, scr->screen, NULL);
	if (!im)
		return;

	xcb_xim_set_use_utf8_string(im, true);
	xcb_xim_set_im_callback(im, &im_cbs, scr);
	if (xcb_xim_open(im, on_im_open, true, scr)) {
		scr->xcbim = im;
		xcb_im_register(im);
	} else {
		xcb_xim_destroy(im);
	}
}
#endif

void W_InitIM(W_Screen * scr)
{
	XIM xim;
	XIMStyles *im_styles;
	XIMCallback cb;
	int i;

	if (WXEvqXcbOwned()) {
		/* xcb path: xkb_compose_state for the dead-key / Compose
		 * fallback, plus an optional auto-reconnecting protocol IM */
		init_compose_state(scr);
#ifdef HAVE_XCB_IMDKIT
		init_xcb_im(scr);
#endif
		return;
	}

	/* non-xcb path: classic Xlib XIM --------------------------------- */
	if (scr->imctx)
		return;

	xim = XOpenIM(scr->display, NULL, NULL, NULL);
	if (!xim) {
		XRegisterIMInstantiateCallback(scr->display, NULL, NULL, NULL,
		                               instantiateIM_cb, (XPointer) scr);
		return;
	}

	scr->imctx = wmalloc(sizeof(WMIMContext));
	scr->imctx->xim = xim;

	cb.callback = destroyIM_cb;
	cb.client_data = (XPointer) scr;
	if (XSetIMValues(scr->imctx->xim, XNDestroyCallback, &cb, NULL))
		wwarning(_("could not add destroy callback for XIM input method"));
	XUnregisterIMInstantiateCallback(scr->display, NULL, NULL, NULL,
	                                 instantiateIM_cb, (XPointer) scr);
	/* Get available input style */
	XGetIMValues(scr->imctx->xim, XNQueryInputStyle, &im_styles, NULL);

	scr->imctx->ximstyle = 0;

	for (i = 0; i < im_styles->count_styles && scr->imctx->ximstyle == 0; i++) {
		if ((im_styles->supported_styles[i] & XIMPreeditPosition) &&
		    (im_styles->supported_styles[i] & XIMStatusNothing)) {
			scr->imctx->ximstyle = XIMPreeditPosition | XIMStatusNothing;
		} else if ((im_styles->supported_styles[i] & XIMPreeditNothing) &&
			   (im_styles->supported_styles[i] & XIMStatusNothing)) {
			scr->imctx->ximstyle = XIMPreeditNothing | XIMStatusNothing;
		}
	}
	XFree(im_styles);
}

/* -----------------------------------------------------------------------
 * W_CreateIC -- called when a view is realized
 * -------------------------------------------------------------------- */

void W_CreateIC(WMView * view)
{
	WMScreen *scr = W_VIEW_SCREEN(view);
	XVaNestedList preedit_attr = NULL;
	unsigned long fevent;

	if (!view->flags.realized)
		return;

	if (WXEvqXcbOwned()) {
#ifdef HAVE_XCB_IMDKIT
		xcb_xim_t *im = (xcb_xim_t *)scr->xcbim;
		xcb_window_t win = view->window;
		uint32_t style = XCB_IM_PreeditNothing | XCB_IM_StatusNothing;

		if (im && !view->xcbxic)
			xcb_xim_create_ic(im, on_create_ic, view,
			                  XCB_XIM_XNInputStyle,   &style,
			                  XCB_XIM_XNClientWindow, &win,
			                  XCB_XIM_XNFocusWindow,  &win,
			                  NULL);
#else
		(void)scr;
#endif
		return;
	}

	/* non-xcb path */
	if (view->xic || !scr->imctx)
		return;

	if (scr->imctx->ximstyle & XIMPreeditPosition) {
		XPoint spot;
		XRectangle rect;
		int ofs;

		ofs = (view->size.height - WMFontHeight(scr->normalFont)) / 2;

		rect.x = ofs;
		rect.y = ofs;
		rect.height = WMFontHeight(scr->normalFont);
		rect.width = view->size.width - ofs * 2;
		spot.x = rect.x;
		spot.y = rect.y + rect.height;

		/* this really needs to be changed, but I don't know how yet -Dan
		 * it used to be like this with fontsets, but no longer applies to xft */
		preedit_attr = XVaCreateNestedList(0, XNSpotLocation, &spot,
						   XNArea, &rect, XNFontInfo, scr->normalFont->font, NULL);
	}

	view->xic = XCreateIC(scr->imctx->xim, XNInputStyle, scr->imctx->ximstyle,
			      XNClientWindow, view->window,
			      preedit_attr ? XNPreeditAttributes : NULL, preedit_attr, NULL);

	if (preedit_attr)
		XFree(preedit_attr);

	if (view->xic) {
		fevent = 0;
		XGetICValues(view->xic, XNFilterEvents, &fevent, NULL);
		XSelectInput(scr->display, view->window,
			     ButtonPressMask | ButtonReleaseMask | ExposureMask |
			     KeyPressMask | FocusChangeMask | ButtonMotionMask | fevent);
	}
}

/* -----------------------------------------------------------------------
 * W_DestroyIC
 * -------------------------------------------------------------------- */

void W_DestroyIC(WMView * view)
{
	if (WXEvqXcbOwned()) {
#ifdef HAVE_XCB_IMDKIT
		WMScreen *scr = W_VIEW_SCREEN(view);
		xcb_xim_t *im = (xcb_xim_t *)scr->xcbim;

		if (im && view->xcbxic) {
			xcb_xim_destroy_ic(im, view->xcbxic, NULL, NULL);
			view->xcbxic = 0;
			view->xcbxic_fwd_mask = 0;
		}
#endif
		return;
	}

	if (view->xic) {
		XDestroyIC(view->xic);
		view->xic = 0;
	}
}

/* -----------------------------------------------------------------------
 * W_FocusIC / W_UnFocusIC
 * -------------------------------------------------------------------- */

static void setPreeditArea(W_View * view)
{
	WMScreen *scr = W_VIEW_SCREEN(view);
	XVaNestedList preedit_attr = NULL;

	if (view->xic && (scr->imctx->ximstyle & XIMPreeditPosition)) {
		XRectangle rect;
		int ofs;

		ofs = (view->size.height - WMFontHeight(scr->normalFont)) / 2;
		rect.x = ofs;
		rect.y = ofs;
		rect.height = WMFontHeight(scr->normalFont);
		rect.width = view->size.width - ofs * 2;

		preedit_attr = XVaCreateNestedList(0, XNArea, &rect, NULL);
		XSetICValues(view->xic, XNPreeditAttributes, preedit_attr, NULL);

		if (preedit_attr)
			XFree(preedit_attr);
	}
}

void W_FocusIC(WMView * view)
{
	WMScreen *scr = W_VIEW_SCREEN(view);

	if (WXEvqXcbOwned()) {
#ifdef HAVE_XCB_IMDKIT
		xcb_xim_t *im = (xcb_xim_t *)scr->xcbim;

		if (im && view->xcbxic)
			xcb_xim_set_ic_focus(im, view->xcbxic);
#else
		(void)scr;
#endif
		return;
	}

	if (view->xic) {
		XSetICFocus(view->xic);
		XSetICValues(view->xic, XNFocusWindow, view->window, NULL);

		if (scr->imctx->ximstyle & XIMPreeditPosition)
			setPreeditArea(view);
	}
}

void W_UnFocusIC(WMView * view)
{
	if (WXEvqXcbOwned()) {
#ifdef HAVE_XCB_IMDKIT
		WMScreen *scr = W_VIEW_SCREEN(view);
		xcb_xim_t *im = (xcb_xim_t *)scr->xcbim;

		if (im && view->xcbxic)
			xcb_xim_unset_ic_focus(im, view->xcbxic);
#endif
		return;
	}

	if (view->xic)
		XUnsetICFocus(view->xic);
}

/* -----------------------------------------------------------------------
 * W_SetPreeditPositon -- update preedit spot location
 * -------------------------------------------------------------------- */

void W_SetPreeditPositon(W_View * view, int x, int y)
{
	WMScreen *scr = W_VIEW_SCREEN(view);
	XVaNestedList preedit_attr;
	XPoint spot;
	int ofs;

	if (WXEvqXcbOwned()) {
#ifdef HAVE_XCB_IMDKIT
		xcb_xim_t *im = (xcb_xim_t *)scr->xcbim;

		if (im && view->xcbxic)
			xcb_xim_ext_move(im, view->xcbxic, (int16_t)x, (int16_t)y);
#else
		(void)scr; (void)x; (void)y;
#endif
		return;
	}

	if (view->xic && (scr->imctx->ximstyle & XIMPreeditPosition)) {
		ofs = (view->size.height - WMFontHeight(scr->normalFont)) / 2;
		spot.x = x;
		spot.y = y + view->size.height - ofs - 3;
		preedit_attr = XVaCreateNestedList(0, XNSpotLocation, &spot, NULL);
		XSetICValues(view->xic, XNPreeditAttributes, preedit_attr, NULL);
		if (preedit_attr)
			XFree(preedit_attr);
	}
}

/* -----------------------------------------------------------------------
 * W_LookupString
 * -------------------------------------------------------------------- */

int W_LookupString(W_View *view, XKeyPressedEvent *event, char *buffer, int buflen, KeySym *keysym, Status *status)
{
	WMScreen *scr = W_VIEW_SCREEN(view);

	if (WXEvqXcbOwned()) {
		struct xkb_compose_state *cs;
		KeySym ksym = NoSymbol;
		KeySym composed_sym;
		int n, cn;

		/* xcb path: XFilterEvent and Xutf8LookupString are retired.
		 *
		 * When xcbxic != 0 the key was already forwarded to the IM by
		 * W_IMForwardKey, only navigation keysyms reach here, so plain
		 * XLookupString is sufficient (no compose for navigation).
		 *
		 * When xcbxic == 0 run xkb_compose_state to handle dead-key /
		 * Compose sequences, fall back to XLookupString otherwise. */
		if (view->xcbxic) {
			n = XLookupString(event, buffer, buflen, keysym, NULL);
			if (status)
				*status = (n > 0) ? XLookupBoth : XLookupKeySym;
			return n;
		}

		/* compose fallback */
		n = XLookupString(event, buffer, buflen, &ksym, NULL);
		if (keysym)
			*keysym = ksym;
		if (status)
			*status = (n > 0) ? XLookupBoth : XLookupKeySym;

		cs = (struct xkb_compose_state *)scr->compose_state;
		if (cs && ksym != NoSymbol) {
			composed_sym = ksym;
			cn = W_IMComposeKey(cs, ksym, buffer, buflen, &composed_sym);
			if (cn > 0) {
				if (keysym)
					*keysym = composed_sym;
				if (status)
					*status = XLookupBoth;
				return cn;
			}
			/* still composing: swallow the dead key silently */
			if (xkb_compose_state_get_status(cs) == XKB_COMPOSE_COMPOSING) {
				if (status)
					*status = XLookupNone;
				return 0;
			}
		}
		return n;
	}

	/* non-xcb path: original Xlib implementation */
	XSetInputFocus(scr->display, view->window, RevertToParent, CurrentTime);

#ifdef X_HAVE_UTF8_STRING
	if (view->xic)
		return Xutf8LookupString(view->xic, event, buffer, buflen, keysym, status);
#endif
	return XLookupString(event, buffer, buflen, keysym, (XComposeStatus *) status);
}

/* -----------------------------------------------------------------------
 * W_KeycodeToKeysym (unchanged)
 * -------------------------------------------------------------------- */

/*
 * Map a keycode to the corresponding keysym
 * To replace the deprecated X11 function XKeycodeToKeysym
 */
KeySym W_KeycodeToKeysym(Display *display, KeyCode keycode, int index)
{
	static int min_kc = -1;
	static int max_kc;
	int num_syms;
	KeySym *key_syms;
	KeySym ks;

	XDisplayKeycodes(display, &min_kc, &max_kc);
	if (keycode < min_kc || keycode > max_kc || index < 0) {
		return NoSymbol;
	}

	key_syms = XGetKeyboardMapping(display, keycode, 1, &num_syms);
	if (index >= num_syms) {
		XFree(key_syms);
		return NoSymbol;
	}

	ks = key_syms[index];
	XFree(key_syms);

	return ks;
}
