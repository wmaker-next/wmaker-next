/*
 * WINGs input method support
 *
 * One path, on the xcb-owned event queue:
 *
 *  Dead-key / Compose sequences are handled by xkb_compose_state
 *  (unconditional fallback). A protocol IM client (xcb_xim_t,
 *  auto-reconnecting) is created for every screen and fcitx5 / ibus
 *  connections are routed through it, committed text arrives via the
 *  commit_string callback and is inserted at the cursor of the focused
 *  view. Xlib's protocol-XIM transport is retired.
 *
 *  Copyright (c) 2009-2026 Window Maker Team
 *
 *  This library is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Library General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 */

#include <stdint.h>
#include <string.h>

#include <xcb/xcb.h>
#include <xcb-imdkit/imclient.h>
#include <xcb-imdkit/encoding.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>

#include "wconfig.h"
#include "WINGsP.h"

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
 * xcb-imdkit types.
 */
int W_IMComposeKey(void *compose_state, xkb_keysym_t ksym, char *buf, int buflen, xkb_keysym_t *sym_out)
{
	struct xkb_compose_state *cs = (struct xkb_compose_state *)compose_state;

	if (!cs || ksym == XKB_KEY_NoSymbol)
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

/* -----------------------------------------------------------------------
 * IM bypass ring -- prevents re-forwarding keys the IM returned
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

static bool im_bypass_check(unsigned long serial)
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
			return true;
		}
	}
	return false;
}

/* the true 32-bit sequence marked when the key was forwarded, matched
 * by the wire low half, found/not-found reported explicitly since a
 * pre-wrap session makes the true value equal the wire value */
static bool im_bypass_resolve(uint16_t wire, unsigned long *full)
{
	int i;

	for (i = 0; i < im_bypass_count; i++) {
		int idx = (im_bypass_head - 1 - i + IM_BYPASS_MAX) % IM_BYPASS_MAX;

		if ((im_bypass_serials[idx] & 0xFFFF) == wire) {
			*full = im_bypass_serials[idx];
			return true;
		}
	}
	return false;
}

/* -----------------------------------------------------------------------
 * IM callbacks (xcb-imdkit)
 * -------------------------------------------------------------------- */

/*
 * IC creation is serialized here. xcb-imdkit keeps exactly one
 * request in flight (im->current) and only drains its request queue
 * from the request entry points -- no reply handler re-triggers the
 * queue, and set_ic_focus/ext_move do not either -- so a create_ic
 * issued while another one awaits its reply would sit queued forever.
 * One request is therefore kept on the wire at a time and the next
 * one is picked by the reply callback or the reconnect walk.
 */
typedef struct {
	W_Screen *scr;
	W_View *view;
} ICPending;

/* destroy_ic requests deferred while another request owns the wire,
 * drained FIFO one per reply callback */
typedef struct ImDestroyPend {
	struct ImDestroyPend *next;
	xcb_xic_t ic;
} ImDestroyPend;

static void on_create_ic(xcb_xim_t *im, xcb_xic_t ic, void *user_data);
static void on_destroy_ic(xcb_xim_t *im, xcb_xic_t ic, void *user_data);

/* first realized view that asked for an IC and has none yet */
static W_View *first_wanted_view(W_View *view)
{
	W_View *child, *hit;

	if (view->flags.realized && view->flags.xcbxic_wanted && !view->xcbxic)
		return view;

	for (child = view->childrenList; child != NULL; child = child->nextSister) {
		hit = first_wanted_view(child);
		if (hit)
			return hit;
	}
	return NULL;
}

/* enqueue the destroy, the wire holds one request and xcb-imdkit only
 * drains its queue from request entry points */
static void im_issue_destroy(W_Screen *scr, xcb_xic_t ic)
{
	xcb_xim_t *im = (xcb_xim_t *)scr->xcbim;
	ImDestroyPend *pend, *last;

	if (!im)
		return;

	if (scr->ic_inflight || scr->ic_destroy_inflight) {
		pend = wmalloc(sizeof(*pend));
		pend->ic = ic;

		last = (ImDestroyPend *)scr->ic_destroy_pend;
		if (!last) {
			scr->ic_destroy_pend = pend;
		} else {
			while (last->next)
				last = last->next;
			last->next = pend;
		}
		return;
	}

	scr->ic_destroy_inflight = 1;
	xcb_xim_destroy_ic(im, ic, on_destroy_ic, scr);
}

static void im_flush_destroy(W_Screen *scr)
{
	ImDestroyPend *pend = (ImDestroyPend *)scr->ic_destroy_pend;
	xcb_xim_t *im = (xcb_xim_t *)scr->xcbim;

	if (!pend || !im || scr->ic_inflight || scr->ic_destroy_inflight)
		return;

	scr->ic_destroy_pend = pend->next;
	scr->ic_destroy_inflight = 1;
	xcb_xim_destroy_ic(im, pend->ic, on_destroy_ic, scr);
	wfree(pend);
}

static void im_kick(W_Screen *scr)
{
	xcb_xim_t *im = (xcb_xim_t *)scr->xcbim;
	ICPending *req;
	W_View *view;
	xcb_window_t win;
	uint32_t style = XCB_IM_PreeditNothing | XCB_IM_StatusNothing;

	if (!im || !scr->im_open)
		return;

	im_flush_destroy(scr);
	if (scr->ic_inflight || scr->ic_destroy_inflight)
		return;

	view = first_wanted_view(scr->rootView);
	if (!view)
		return;

	req = wmalloc(sizeof(ICPending));
	req->scr = scr;
	req->view = view;
	scr->ic_inflight = req;
	win = view->window;
	xcb_xim_create_ic(im, on_create_ic, req,
	                  XCB_XIM_XNInputStyle,   &style,
	                  XCB_XIM_XNClientWindow, &win,
	                  XCB_XIM_XNFocusWindow,  &win,
	                  NULL);
}

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
	char *text = NULL;
	bool lib_owned = false;

	(void)flag;
	(void)keysym;
	(void)nkeysym;

	if (!str || length == 0)
		return;

	/* the commit string is in the encoding picked at negotiation,
	 * but several servers (ibus among them) commit compound text
	 * even when UTF8_STRING was negotiated. CT marks its charset
	 * switches with ESC, so when UTF8_STRING is on the wire only a
	 * payload carrying an ESC byte is decoded as CT — well-behaved
	 * UTF-8 commits pass through untouched */
	if (xcb_xim_get_encoding(im) != XCB_XIM_UTF8_STRING ||
	    memchr(str, '\x1b', length)) {
		size_t out_len = 0;
		char *decoded = xcb_compound_text_to_utf8(str, length, &out_len);

		if (decoded) {
			text = decoded;
			lib_owned = true;
		}
	}
	if (!text) {
		text = wmalloc(length + 1);
		memcpy(text, str, length);
		text[length] = '\0';
	}

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
	if (!view || !view->self) {
		if (lib_owned)
			free(text);
		return;
	}
	if (W_CLASS(view->self) != WC_TextField) {
		if (lib_owned)
			free(text);
		return;
	}

	tf = (WMTextField *)view->self;
	WMInsertTextFieldText(tf, text, WMGetTextFieldCursorPosition(tf));
	if (lib_owned)
		free(text);
	else
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
	xcb_key_press_event_t *copy;
	unsigned long full;
	bool marked;

	(void)im;

	if (!view)
		return;

	/* re-inject the key event on the WINGs queue with the window
	 * rewritten to the IC's view so dispatch finds it. The echo
	 * carries only the 16-bit wire sequence, the true value marked
	 * at forward time is resolved from the bypass ring, unmatched
	 * echoes keep the 16-bit stamp and mark it themselves */
	copy = (xcb_key_press_event_t *) W_SynthEvent(event->response_type);
	memcpy(copy, event, sizeof(*event));
	copy->event = view->window;
	marked = im_bypass_resolve(event->sequence, &full);
	if (!marked)
		full = event->sequence;
	((xcb_generic_event_t *)copy)->full_sequence = full;
	if (!marked)
		im_bypass_mark(full);
	WXEvqPutBack((xcb_generic_event_t *)copy);
}

/* IC ids are invalid after a disconnect; the reconnect walk re-creates */
static void clear_xcbxic(W_View *view)
{
	W_View *child;

	view->xcbxic = 0;
	view->xcbxic_fwd_mask = 0;
	for (child = view->childrenList; child != NULL; child = child->nextSister)
		clear_xcbxic(child);
}

static void on_disconnected(xcb_xim_t *im, void *user_data)
{
	W_Screen *scr = (W_Screen *)user_data;
	ImDestroyPend *pend;

	(void)im;
	/* every IC died with the connection; xcb-imdkit freed the
	 * in-flight request without running its callback, so the
	 * ICPending cell has to be released here */
	scr->im_open = 0;
	if (scr->ic_inflight) {
		wfree(scr->ic_inflight);
		scr->ic_inflight = NULL;
	}
	scr->ic_inflight_gone = 0;
	while ((pend = (ImDestroyPend *)scr->ic_destroy_pend) != NULL) {
		scr->ic_destroy_pend = pend->next;
		wfree(pend);
	}
	scr->ic_destroy_inflight = 0;
	clear_xcbxic(scr->rootView);
}

static void on_create_ic(xcb_xim_t *im, xcb_xic_t ic, void *user_data)
{
	ICPending *req = (ICPending *)user_data;
	W_Screen *scr = req->scr;
	W_View *view = req->view;

	(void)im;
	scr->ic_inflight = NULL;
	if (scr->ic_inflight_gone) {
		/* the view was destroyed while the request was on the
		 * wire; do not touch the dangling pointer, but the IC
		 * the server just created would leak with a focus
		 * window pointing at the dead view */
		if (ic)
			im_issue_destroy(scr, ic);
		scr->ic_inflight_gone = 0;
	} else if (ic) {
		view->xcbxic = ic;
	} else {
		/* creation failed; a persistent failure (a hostile or
		 * wedged IM server) would re-pick this view on every
		 * kick, so drop the want and let the compose fallback
		 * carry the view */
		view->flags.xcbxic_wanted = 0;
	}
	wfree(req);
	im_kick(scr);
}

static void on_destroy_ic(xcb_xim_t *im, xcb_xic_t ic, void *user_data)
{
	W_Screen *scr = (W_Screen *)user_data;

	(void)im;
	(void)ic;

	scr->ic_destroy_inflight = 0;
	im_kick(scr);
}

static void on_im_open(xcb_xim_t *im, void *user_data)
{
	W_Screen *scr = (W_Screen *)user_data;

	(void)im;
	scr->im_open = 1;
	/* create ICs for views that realised while the IM was
	 * connecting or that lost theirs on a reconnect */
	im_kick(scr);
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

/* -----------------------------------------------------------------------
 * W_IMFilterEventGlobal -- filter XIM protocol messages before view lookup
 *
 * Called from WMHandleEvent, before W_GetViewForXWindow, because XIM
 * protocol messages arrive on IM-internal windows that have no matching
 * W_View.
 * -------------------------------------------------------------------- */

bool W_IMFilterEventGlobal(xcb_generic_event_t *event)
{
	int i;

	if (g_xcb_im_count == 0)
		return false;

	for (i = 0; i < g_xcb_im_count; i++) {
		if (xcb_xim_filter_event(g_xcb_ims[i], event))
			return true;
	}
	return false;
}

/* -----------------------------------------------------------------------
 * W_IMForwardKey -- forward a key press to the IM, returns True if consumed
 *
 * Called at the top of handleTextFieldKeyPress. Returns True when the key
 * was sent to the IM and text will arrive later via commit_string, the
 * caller should return without further processing. Returns False for
 * navigation/modifier keysyms and when no xcb-imdkit IC exists.
 * -------------------------------------------------------------------- */

static bool should_forward_to_im(xkb_keysym_t ksym)
{
	if (ksym == XKB_KEY_NoSymbol)
		return false;
	/* modifier keys */
	if (ksym >= XKB_KEY_Shift_L && ksym <= XKB_KEY_Hyper_R)
		return false;
	if (ksym >= XKB_KEY_ISO_Lock && ksym <= XKB_KEY_ISO_Last_Group_Lock)
		return false;
	/* 0xFF00..0xFFFF: BackSpace, Tab, Return, Escape, arrows, F-keys, Delete */
	if (ksym >= 0xFF00)
		return false;
	/* 0xFE00..0xFEFF: dead keys -- forward to IM so it handles compose */
	if (ksym >= 0xFE00)
		return true;
	/* printable range */
	if (ksym >= 0x0020)
		return true;
	return false;
}

bool W_IMForwardKey(W_View *view, xcb_generic_event_t *event)
{
	W_Screen *scr;
	xcb_xim_t *im;
	xkb_keysym_t ksym;

	if (!view->xcbxic)
		return false;

	scr = view->screen;
	if (!scr || !scr->xcbim)
		return false;

	/* event was re-injected by forward_event callback: process normally */
	if (im_bypass_check(event->full_sequence))
		return false;

	/* keysym classification only needs the keycode, resolved against
	 * the screen's xkbcommon keymap */
	ksym = W_EventKeysym(scr, (const xcb_key_press_event_t *)event);
	if (!should_forward_to_im(ksym))
		return false;

	im = (xcb_xim_t *)scr->xcbim;
	/* the echo comes back with only the 16-bit wire sequence, the
	 * bypass ring resolves it via the true value marked here */
	im_bypass_mark(event->full_sequence);
	xcb_xim_forward_event(im, view->xcbxic, (xcb_key_press_event_t *)event);
	return true;
}

/* -----------------------------------------------------------------------
 * W_InitIM -- called once per screen at screen creation
 *
 * The xkbcommon stack (keymap, state, compose) is W_InitXkb()'s job in
 * wxkb.c, called from the screen-creation path before this.
 * -------------------------------------------------------------------- */

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

	conn = scr->display;
	if (!conn)
		return;

	im = xcb_xim_create(conn, scr->screen, NULL);
	if (!im)
		return;

	/* UTF-8 preferred; compound text stays offered for servers that
	 * only negotiate it, on_commit_string decodes whichever the
	 * server picked (ibus commits compound text even when UTF8_STRING
	 * was negotiated) */
	xcb_compound_text_init();
	xcb_xim_set_use_utf8_string(im, true);
	xcb_xim_set_im_callback(im, &im_cbs, scr);
	/*
	 * xcb_xim_open() returns False when no IM server has registered
	 * yet, but with auto_connect it has already armed the
	 * XIM_SERVERS root-property watch at that point: once a server
	 * appears, the property notify drives the reconnect and
	 * on_im_open fires. The handle must therefore be kept and
	 * registered even on a failed first open, and the im kept in
	 * the event filter so the notify is seen.
	 */
	scr->xcbim = im;
	xcb_im_register(im);
	(void)xcb_xim_open(im, on_im_open, true, scr);
}

void W_InitIM(W_Screen * scr)
{
	/* the auto-reconnecting protocol IM, compose/dead-key fallback
	 * stays for when no IM server answers */
	init_xcb_im(scr);
}

/* -----------------------------------------------------------------------
 * W_CreateIC -- called when a view is realized
 * -------------------------------------------------------------------- */

void W_CreateIC(WMView * view)
{
	WMScreen *scr = W_VIEW_SCREEN(view);

	if (!view->flags.realized)
		return;

	view->flags.xcbxic_wanted = 1;
	im_kick(scr);
}

/* -----------------------------------------------------------------------
 * W_DestroyIC
 * -------------------------------------------------------------------- */

void W_DestroyIC(WMView * view)
{
	WMScreen *scr = W_VIEW_SCREEN(view);
	ICPending *req = (ICPending *)scr->ic_inflight;

	view->flags.xcbxic_wanted = 0;
	if (req && req->view == view) {
		/* the reply callback will find the request but must not
		 * touch the view */
		scr->ic_inflight_gone = 1;
	}
	if (view->xcbxic) {
		im_issue_destroy(scr, view->xcbxic);
		view->xcbxic = 0;
		view->xcbxic_fwd_mask = 0;
	}
	im_kick(scr);
}

/* -----------------------------------------------------------------------
 * W_FocusIC / W_UnFocusIC
 * -------------------------------------------------------------------- */

void W_FocusIC(WMView * view)
{
	WMScreen *scr = W_VIEW_SCREEN(view);
	xcb_xim_t *im = (xcb_xim_t *)scr->xcbim;

	if (im && view->xcbxic)
		xcb_xim_set_ic_focus(im, view->xcbxic);
	im_kick(scr);
}

void W_UnFocusIC(WMView * view)
{
	WMScreen *scr = W_VIEW_SCREEN(view);
	xcb_xim_t *im = (xcb_xim_t *)scr->xcbim;

	if (im && view->xcbxic)
		xcb_xim_unset_ic_focus(im, view->xcbxic);
	im_kick(scr);
}

/* -----------------------------------------------------------------------
 * W_SetPreeditPositon -- update preedit spot location
 * -------------------------------------------------------------------- */

void W_SetPreeditPositon(W_View * view, int x, int y)
{
	WMScreen *scr = W_VIEW_SCREEN(view);
	xcb_xim_t *im = (xcb_xim_t *)scr->xcbim;

	if (im && view->xcbxic)
		xcb_xim_ext_move(im, view->xcbxic, (int16_t)x, (int16_t)y);
	im_kick(scr);
}

/* -----------------------------------------------------------------------
 * W_LookupString
 * -------------------------------------------------------------------- */

int W_LookupString(W_View *view, const xcb_key_press_event_t *event, char *buffer, int buflen, xkb_keysym_t *keysym, WXLookupStatus *status)
{
	WMScreen *scr = W_VIEW_SCREEN(view);
	void *state = W_XkbStateOf(scr);
	void *compose = scr->compose_state;

	/* When xcbxic != 0 the key was already forwarded to the IM
	 * by W_IMForwardKey, only navigation keysyms reach here and
	 * compose must not run, the IM owns the dead keys.
	 *
	 * When xcbxic == 0 the xkb compose state handles dead-key /
	 * Compose sequences, but only on press: compose rules
	 * progress on key presses and a release-fed state machine
	 * would leak into the next press elsewhere (the screen's
	 * compose state is shared). */
	if (view->xcbxic || (event->response_type & 0x7f) != XCB_KEY_PRESS)
		compose = NULL;

	/* state is NULL only when the xkb stack never came up at all
	 * (no server XKB and no fallback), W_XkbLookupString answers
	 * XLookupNone for that */
	return W_XkbLookupString(state, compose, event,
				 buffer, buflen,
				 keysym, status);
}

/* -----------------------------------------------------------------------
 * W_EventKeysym
 * -------------------------------------------------------------------- */

/*
 * Keysym of a key event without touching the compose state machine,
 * for the navigation-only classifications. Resolved against the
 * per-screen xkbcommon state.
 */
xkb_keysym_t W_EventKeysym(W_Screen *scr, const xcb_key_press_event_t *event)
{
	void *state = scr ? W_XkbStateOf(scr) : NULL;

	if (!state)
		return XKB_KEY_NoSymbol;

	return xkb_state_key_get_one_sym(state, event->detail);
}
