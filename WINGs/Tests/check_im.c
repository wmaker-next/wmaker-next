/*
 *  check_im.c
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, see <https://www.gnu.org/licenses/>.
 */

/* check_im.c - unit tests for xcb-path IM helpers
 *
 * Tests:
 *  - W_IMComposeKey via xkb_compose_state (dead-key sequences, cancel, null)
 *  - W_IMForwardKey returns false when xcbxic == 0 (no X server needed)
 *  - W_IMFilterEventGlobal returns false with no xcb-imdkit IM registered
 *
 * No Display is opened, no IM connection is made.
 */

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>

/*
 * Pull in the private WINGs header for the real W_View and W_Screen layouts.
 * check_im_CPPFLAGS adds -I$(top_srcdir)/WINGs/WINGs so this resolves to
 * WINGs/WINGs/WINGsP.h.
 */
#include "WINGsP.h"

#include "check_common.h"

/* -----------------------------------------------------------------------
 * Compose state helpers
 * -------------------------------------------------------------------- */

static struct xkb_compose_state *make_compose_state(const char *locale)
{
	struct xkb_context *ctx;
	struct xkb_compose_table *tbl;
	struct xkb_compose_state *st = NULL;

	ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!ctx)
		return NULL;

	tbl = xkb_compose_table_new_from_locale(ctx, locale,
	                                         XKB_COMPOSE_COMPILE_NO_FLAGS);
	if (tbl) {
		st = xkb_compose_state_new(tbl, XKB_COMPOSE_STATE_NO_FLAGS);
		xkb_compose_table_unref(tbl);
	}
	xkb_context_unref(ctx);
	return st;
}

/* -----------------------------------------------------------------------
 * W_IMComposeKey tests
 * -------------------------------------------------------------------- */

/* dead_grave + a -> "a-grave" (U+00E0, UTF-8: 0xc3 0xa0) */
static void test_compose_dead_grave_a(void)
{
	struct xkb_compose_state *cs = make_compose_state("en_US.UTF-8");
	char buf[16];
	xkb_keysym_t sym;
	int n;

	if (!cs) {
		fprintf(stderr, "SKIP test_compose_dead_grave_a: no compose table\n");
		return;
	}

	/* first key: dead_grave starts a compose sequence (still composing) */
	n = W_IMComposeKey(cs, XKB_KEY_dead_grave, buf, sizeof(buf), &sym);
	CHECK(n == 0);

	/* second key: 'a' completes it as a-grave */
	n = W_IMComposeKey(cs, XKB_KEY_a, buf, sizeof(buf), &sym);
	buf[n] = '\0';
	CHECK(n > 0);
	CHECK(strcmp(buf, "\xc3\xa0") == 0);   /* UTF-8 for U+00E0 */
	CHECK(sym == XKB_KEY_agrave);

	xkb_compose_state_unref(cs);
}

/* dead_grave + BackSpace: BackSpace is not in any compose continuation so
 * the sequence is cancelled.  The state must be reset afterwards.       */
static void test_compose_cancelled(void)
{
	struct xkb_compose_state *cs = make_compose_state("en_US.UTF-8");
	char buf[16];
	xkb_keysym_t sym;
	int n;

	if (!cs) {
		fprintf(stderr, "SKIP test_compose_cancelled: no compose table\n");
		return;
	}

	/* start a sequence */
	n = W_IMComposeKey(cs, XKB_KEY_dead_grave, buf, sizeof(buf), &sym);
	CHECK(n == 0);

	/* BackSpace (0xFF08) is not a valid continuation -> cancelled */
	n = W_IMComposeKey(cs, XKB_KEY_BackSpace, buf, sizeof(buf), &sym);
	CHECK(n == 0);

	/* state must be reset: a fresh dead_grave starts composing again */
	n = W_IMComposeKey(cs, XKB_KEY_dead_grave, buf, sizeof(buf), &sym);
	CHECK(n == 0);  /* composing */

	xkb_compose_state_unref(cs);
}

/* NULL compose_state -> returns 0, no crash */
static void test_compose_null_state(void)
{
	char buf[16];
	xkb_keysym_t sym = XKB_KEY_NoSymbol;

	CHECK(W_IMComposeKey(NULL, XKB_KEY_a, buf, sizeof(buf), &sym) == 0);
}

/* plain 'a' with no preceding dead key: NOTHING -> returns 0 */
static void test_compose_nothing(void)
{
	struct xkb_compose_state *cs = make_compose_state("en_US.UTF-8");
	char buf[16];
	xkb_keysym_t sym;
	int n;

	if (!cs) {
		fprintf(stderr, "SKIP test_compose_nothing: no compose table\n");
		return;
	}

	n = W_IMComposeKey(cs, XKB_KEY_a, buf, sizeof(buf), &sym);
	/* 'a' alone does not begin any sequence -> NOTHING -> 0 */
	CHECK(n == 0);

	xkb_compose_state_unref(cs);
}

/* -----------------------------------------------------------------------
 * W_IMForwardKey tests (no X server, no IM connection)
 * -------------------------------------------------------------------- */

/* xcbxic == 0 -> W_IMForwardKey returns false immediately */
static void test_forward_no_ic(void)
{
	static W_View view;
	xcb_key_press_event_t ev;

	memset(&view, 0, sizeof(view));
	memset(&ev, 0, sizeof(ev));
	ev.response_type = XCB_KEY_PRESS;
	ev.detail = 38;   /* 'a' on a typical QWERTY layout */

	/* xcbxic stays 0 after memset; expect false regardless of keysym */
	CHECK(W_IMForwardKey(&view, (xcb_generic_event_t *)&ev) == false);
}

/* xcbxic != 0 but xcbim == NULL -> W_IMForwardKey returns false */
static void test_forward_no_xcbim(void)
{
	static W_View view;
	static W_Screen scr;
	xcb_key_press_event_t ev;

	memset(&view, 0, sizeof(view));
	memset(&scr,  0, sizeof(scr));
	memset(&ev,   0, sizeof(ev));

	view.screen  = &scr;
	view.xcbxic  = 1;
	scr.xcbim    = NULL;    /* no IM: must return false without crashing */

	ev.response_type = XCB_KEY_PRESS;
	ev.detail = 38;

	CHECK(W_IMForwardKey(&view, (xcb_generic_event_t *)&ev) == false);
}

/* -----------------------------------------------------------------------
 * W_IMFilterEventGlobal: no xcb-imdkit IM registered -> false
 * -------------------------------------------------------------------- */

static void test_im_filter_global_no_xcbim(void)
{
	xcb_client_message_event_t ev;

	memset(&ev, 0, sizeof(ev));
	ev.response_type = XCB_CLIENT_MESSAGE;

	/* g_xcb_ims[] is module-static in winputmethod.c and starts empty,
	 * so the first call must return false without crashing.             */
	CHECK(W_IMFilterEventGlobal((xcb_generic_event_t *)&ev) == false);
}

/* -----------------------------------------------------------------------
 * W_XkbLookupString core, driven with a headless xkbcommon state
 * (no server, no connection)
 * -------------------------------------------------------------------- */

/* keycode whose first keysym is `sym`, 0 when the keymap lacks it */
static xkb_keycode_t find_keycode(struct xkb_state *st, xkb_keysym_t sym)
{
	struct xkb_keymap *km = xkb_state_get_keymap(st);
	xkb_keycode_t kc;

	for (kc = xkb_keymap_min_keycode(km); kc <= xkb_keymap_max_keycode(km); kc++) {
		if (xkb_state_key_get_one_sym(st, kc) == sym)
			return kc;
	}
	return 0;
}

/* headless us keymap + state, 0 when the data files are unavailable */
static struct xkb_state *make_us_state(struct xkb_context **ctx_ret)
{
	struct xkb_context *ctx;
	struct xkb_keymap *km;
	struct xkb_state *st = NULL;

	ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (ctx) {
		km = xkb_keymap_new_from_names(ctx, &(struct xkb_rule_names) {
			.rules = NULL, .model = NULL, .layout = "us",
			.variant = NULL, .options = NULL },
			XKB_KEYMAP_COMPILE_NO_FLAGS);
		if (km) {
			st = xkb_state_new(km);
			xkb_keymap_unref(km);
		}
		if (!st)
			xkb_context_unref(ctx);
	}
	*ctx_ret = st ? ctx : NULL;
	return st;
}

static void test_xkb_lookup_text_and_keysym(void)
{
	struct xkb_context *ctx;
	struct xkb_state *st = make_us_state(&ctx);
	xcb_key_press_event_t ev;
	xkb_keycode_t kc;
	char buf[16];
	xkb_keysym_t ksym = XKB_KEY_NoSymbol;
	WXLookupStatus status = WX_LOOKUP_NONE - 1;
	int n;

	if (!st) {
		fprintf(stderr, "SKIP test_xkb_lookup_text_and_keysym: no keymap\n");
		return;
	}

	kc = find_keycode(st, XKB_KEY_a);
	if (!kc) {
		xkb_state_unref(st);
		xkb_context_unref(ctx);
		fprintf(stderr, "SKIP test_xkb_lookup_text_and_keysym: no 'a'\n");
		return;
	}

	memset(&ev, 0, sizeof(ev));
	ev.response_type = XCB_KEY_PRESS;
	ev.detail = kc;

	n = W_XkbLookupString(st, NULL, &ev, buf, (int)sizeof(buf) - 1,
			      &ksym, &status);
	CHECK(n == 1);
	CHECK(buf[0] == 'a');
	CHECK(ksym == XKB_KEY_a);
	CHECK(status == WX_LOOKUP_BOTH);

	/* no buffer: keysym only, no text */
	n = W_XkbLookupString(st, NULL, &ev, NULL, 0, &ksym, &status);
	CHECK(n == 0);
	CHECK(ksym == XKB_KEY_a);
	CHECK(status == WX_LOOKUP_KEYSYM);

	xkb_state_unref(st);
	xkb_context_unref(ctx);
}

static void test_xkb_lookup_keysym_only(void)
{
	struct xkb_context *ctx;
	struct xkb_state *st = make_us_state(&ctx);
	xcb_key_press_event_t ev;
	xkb_keycode_t kc;
	xkb_keysym_t ksym = XKB_KEY_NoSymbol;
	WXLookupStatus status = WX_LOOKUP_NONE - 1;
	int n;

	if (!st) {
		fprintf(stderr, "SKIP test_xkb_lookup_keysym_only: no keymap\n");
		return;
	}

	/* a navigation key: no text, keysym only */
	kc = find_keycode(st, XKB_KEY_Escape);
	if (!kc)
		kc = find_keycode(st, XKB_KEY_F1);
	if (!kc) {
		xkb_state_unref(st);
		xkb_context_unref(ctx);
		fprintf(stderr, "SKIP test_xkb_lookup_keysym_only: no navigation key\n");
		return;
	}

	memset(&ev, 0, sizeof(ev));
	ev.response_type = XCB_KEY_PRESS;
	ev.detail = kc;

	n = W_XkbLookupString(st, NULL, &ev, NULL, 0, &ksym, &status);
	CHECK(n == 0);
	CHECK(ksym != XKB_KEY_NoSymbol);
	CHECK(status == WX_LOOKUP_KEYSYM);

	xkb_state_unref(st);
	xkb_context_unref(ctx);
}

static void test_xkb_lookup_null_state(void)
{
	xcb_key_press_event_t ev;
	char buf[8];
	xkb_keysym_t ksym = XKB_KEY_NoSymbol;
	WXLookupStatus status = WX_LOOKUP_NONE - 1;

	memset(&ev, 0, sizeof(ev));
	ev.response_type = XCB_KEY_PRESS;
	ev.detail = 38;

	CHECK(W_XkbLookupString(NULL, NULL, &ev, buf, (int)sizeof(buf) - 1,
				&ksym, &status) == 0);
	CHECK(status == WX_LOOKUP_NONE);
}

/* a plain key must not disturb the compose machine and a compose-aware
 * call must still return the plain text for keys not in any sequence */
static void test_xkb_lookup_with_compose_state(void)
{
	struct xkb_compose_state *cs = make_compose_state("en_US.UTF-8");
	struct xkb_context *ctx;
	struct xkb_state *st;
	xcb_key_press_event_t ev;
	xkb_keycode_t kc;
	char buf[16];
	xkb_keysym_t ksym = XKB_KEY_NoSymbol;
	int n;

	if (!cs) {
		fprintf(stderr, "SKIP test_xkb_lookup_with_compose_state: no compose table\n");
		return;
	}
	st = make_us_state(&ctx);
	kc = st ? find_keycode(st, XKB_KEY_a) : 0;
	if (!kc) {
		xkb_compose_state_unref(cs);
		xkb_state_unref(st);
		xkb_context_unref(ctx);
		fprintf(stderr, "SKIP test_xkb_lookup_with_compose_state: no 'a'\n");
		return;
	}

	memset(&ev, 0, sizeof(ev));
	ev.response_type = XCB_KEY_PRESS;
	ev.detail = kc;

	n = W_XkbLookupString(st, cs, &ev, buf, (int)sizeof(buf) - 1, &ksym, NULL);
	CHECK(n == 1);
	CHECK(buf[0] == 'a');
	CHECK(ksym == XKB_KEY_a);
	/* the state machine saw only a key that is in no sequence */
	CHECK(xkb_compose_state_get_status(cs) == XKB_COMPOSE_NOTHING);

	xkb_compose_state_unref(cs);
	xkb_state_unref(st);
	xkb_context_unref(ctx);
}

/* -----------------------------------------------------------------------
 * W_XkbKeysymToKeycode core + W_KeysymToKeycode guard, driven with the
 * headless us keymap (no server, no connection)
 * -------------------------------------------------------------------- */

static void test_xkb_keysym_to_keycode(void)
{
	struct xkb_context *ctx;
	struct xkb_state *st = make_us_state(&ctx);
	struct xkb_keymap *km;
	xkb_keycode_t kc_state;
	xcb_keycode_t kc;

	if (!st) {
		fprintf(stderr, "SKIP test_xkb_keysym_to_keycode: no keymap\n");
		return;
	}
	km = xkb_state_get_keymap(st);

	/* the crash dialog's three keys must all resolve */
	kc = W_XkbKeysymToKeycode(km, XKB_KEY_Return);
	CHECK(kc != 0);
	kc_state = find_keycode(st, XKB_KEY_Return);
	CHECK(kc_state != 0);
	CHECK((xkb_keycode_t) kc == kc_state);

	kc = W_XkbKeysymToKeycode(km, XKB_KEY_Up);
	CHECK(kc != 0);
	kc = W_XkbKeysymToKeycode(km, XKB_KEY_Down);
	CHECK(kc != 0);

	/* keysyms the keymap does not carry resolve to 0 */
	CHECK(W_XkbKeysymToKeycode(km, XKB_KEY_NoSymbol) == 0);
	CHECK(W_XkbKeysymToKeycode(km, XKB_KEY_Cyrillic_zhe) == 0);
	CHECK(W_XkbKeysymToKeycode(NULL, XKB_KEY_Return) == 0);

	xkb_state_unref(st);
	xkb_context_unref(ctx);
}

/* the W_Screen wrapper must return 0, not crash, without an xkb stack */
static void test_keysym_to_keycode_no_xkb(void)
{
	static W_Screen scr;

	memset(&scr, 0, sizeof(scr));

	CHECK(W_KeysymToKeycode(&scr, XKB_KEY_Return) == 0);
	CHECK(W_KeysymToKeycode(NULL, XKB_KEY_Return) == 0);
}

/* -----------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------- */

int main(void)
{
	test_compose_dead_grave_a();
	test_compose_cancelled();
	test_compose_null_state();
	test_compose_nothing();
	test_forward_no_ic();
	test_forward_no_xcbim();
	test_im_filter_global_no_xcbim();
	test_xkb_lookup_text_and_keysym();
	test_xkb_lookup_keysym_only();
	test_xkb_lookup_null_state();
	test_xkb_lookup_with_compose_state();
	test_xkb_keysym_to_keycode();
	test_keysym_to_keycode_no_xkb();

	return check_report("check_im");
}
