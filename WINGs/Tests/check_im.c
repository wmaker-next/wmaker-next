/* check_im.c - unit tests for xcb-path IM helpers
 *
 * Tests:
 *  - W_IMComposeKey via xkb_compose_state (dead-key sequences, cancel, null)
 *  - W_IMForwardKey returns False when xcbxic == 0 (no X server needed)
 *  - W_IMFilterEventGlobal returns False with no xcb-imdkit IM registered
 *
 * No Display is opened, no IM connection is made.
 */

#include <stdio.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>
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
	KeySym sym;
	int n;

	if (!cs) {
		fprintf(stderr, "SKIP test_compose_dead_grave_a: no compose table\n");
		return;
	}

	/* first key: dead_grave starts a compose sequence (still composing) */
	n = W_IMComposeKey(cs, XK_dead_grave, buf, sizeof(buf), &sym);
	CHECK(n == 0);

	/* second key: 'a' completes it as a-grave */
	n = W_IMComposeKey(cs, XK_a, buf, sizeof(buf), &sym);
	buf[n] = '\0';
	CHECK(n > 0);
	CHECK(strcmp(buf, "\xc3\xa0") == 0);   /* UTF-8 for U+00E0 */
	CHECK(sym == XK_agrave);

	xkb_compose_state_unref(cs);
}

/* dead_grave + BackSpace: BackSpace is not in any compose continuation so
 * the sequence is cancelled.  The state must be reset afterwards.       */
static void test_compose_cancelled(void)
{
	struct xkb_compose_state *cs = make_compose_state("en_US.UTF-8");
	char buf[16];
	KeySym sym;
	int n;

	if (!cs) {
		fprintf(stderr, "SKIP test_compose_cancelled: no compose table\n");
		return;
	}

	/* start a sequence */
	n = W_IMComposeKey(cs, XK_dead_grave, buf, sizeof(buf), &sym);
	CHECK(n == 0);

	/* BackSpace (0xFF08) is not a valid continuation -> cancelled */
	n = W_IMComposeKey(cs, XK_BackSpace, buf, sizeof(buf), &sym);
	CHECK(n == 0);

	/* state must be reset: a fresh dead_grave starts composing again */
	n = W_IMComposeKey(cs, XK_dead_grave, buf, sizeof(buf), &sym);
	CHECK(n == 0);  /* composing */

	xkb_compose_state_unref(cs);
}

/* NULL compose_state -> returns 0, no crash */
static void test_compose_null_state(void)
{
	char buf[16];
	KeySym sym = NoSymbol;

	CHECK(W_IMComposeKey(NULL, XK_a, buf, sizeof(buf), &sym) == 0);
}

/* plain 'a' with no preceding dead key: NOTHING -> returns 0 */
static void test_compose_nothing(void)
{
	struct xkb_compose_state *cs = make_compose_state("en_US.UTF-8");
	char buf[16];
	KeySym sym;
	int n;

	if (!cs) {
		fprintf(stderr, "SKIP test_compose_nothing: no compose table\n");
		return;
	}

	n = W_IMComposeKey(cs, XK_a, buf, sizeof(buf), &sym);
	/* 'a' alone does not begin any sequence -> NOTHING -> 0 */
	CHECK(n == 0);

	xkb_compose_state_unref(cs);
}

/* -----------------------------------------------------------------------
 * W_IMForwardKey tests (no X server, no IM connection)
 * -------------------------------------------------------------------- */

/* xcbxic == 0 -> W_IMForwardKey returns False immediately */
static void test_forward_no_ic(void)
{
	static W_View view;
	XEvent ev;

	memset(&view, 0, sizeof(view));
	memset(&ev, 0, sizeof(ev));
	ev.type = KeyPress;
	ev.xkey.keycode = 38;   /* 'a' on a typical QWERTY layout */

	/* xcbxic stays 0 after memset; expect False regardless of keysym */
	CHECK(W_IMForwardKey(&view, &ev) == False);
}

/* xcbxic != 0 but xcbim == NULL -> W_IMForwardKey returns False */
static void test_forward_no_xcbim(void)
{
	static W_View view;
	static W_Screen scr;
	XEvent ev;

	memset(&view, 0, sizeof(view));
	memset(&scr,  0, sizeof(scr));
	memset(&ev,   0, sizeof(ev));

	view.screen  = &scr;
	view.xcbxic  = 1;
	scr.xcbim    = NULL;    /* no IM: must return False without crashing */

	ev.type = KeyPress;
	ev.xkey.keycode = 38;

	CHECK(W_IMForwardKey(&view, &ev) == False);
}

/* -----------------------------------------------------------------------
 * W_IMFilterEventGlobal: no xcb-imdkit IM registered -> False
 * -------------------------------------------------------------------- */

static void test_im_filter_global_no_xcbim(void)
{
	XEvent ev;

	memset(&ev, 0, sizeof(ev));
	ev.type = ClientMessage;

	/* g_xcb_ims[] is module-static in winputmethod.c and starts empty,
	 * so the first call must return False without crashing.             */
	CHECK(W_IMFilterEventGlobal(NULL, &ev) == False);
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

	return check_report("check_im");
}
