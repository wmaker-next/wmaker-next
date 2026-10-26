/*
 *  wmlock - X11 screen locker for Window Maker
 *
 *  Copyright (c) 2026 Window Maker Team - David Maciejak
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
 *  Locks every X screen behind a fullscreen override-redirect cover that
 *  shows a grayscaled, stack-blurred snapshot of the root window. The
 *  keyboard and pointer are grabbed on the cover for the entire lifetime
 *  of the lock, the WINGs prompt is reparented inside the cover and key
 *  events are forwarded to it so user input is not delivered to other
 *  clients while the lock holds. Like every in-server X11 locker this
 *  protects against the physical keyboard and pointer only, a hostile
 *  client already on the display can XKillClient the cover and drop the
 *  grabs, see SECURITY in wmlock(1). Authentication goes through PAM
 *  where available, falling back to OpenBSD bsd_auth.
 */

#include "config.h"

#include <errno.h>
#include <getopt.h>
#include <langinfo.h>
#include <locale.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>

#include <xcb/xcb.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#ifdef HAVE_XCB_DPMS
#include <xcb/dpms.h>
#endif

#ifdef HAVE_PAM
#include <security/pam_appl.h>
#endif

#ifdef HAVE_BSD_AUTH
#include <login_cap.h>
#include <bsd_auth.h>
#endif

#ifdef __linux__
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/oom.h>
#include <linux/vt.h>
#endif

#include <wraster.h>
#include <WINGs/WINGs.h>
#include <WINGs/WXCore.h>
/* W_EventWindow and W_EventKeysym live in the private header, wmlock
 * drains the WINGs queue directly */
#include <WINGs/WINGsP.h>

#include "xcconn.h"

/* explicit_bzero is in glibc, musl, FreeBSD and OpenBSD, NetBSD only has
 * explicit_memset, and anything older has neither, so wrap whichever the
 * configure probe found and fall back to a volatile byte loop the
 * optimiser is not allowed to drop. */
static void scrub(void *p, size_t n)
{
#if defined(HAVE_EXPLICIT_BZERO)
	explicit_bzero(p, n);
#elif defined(HAVE_EXPLICIT_MEMSET)
	explicit_memset(p, 0, n);
#else
	volatile unsigned char *vp = p;

	while (n--)
		*vp++ = 0;
#endif
}

#define BLUR_RADIUS    40
#define GRAB_TRIES     6
#define GRAB_WAIT_US   100000

#define IDLE_TIMEOUT_DEFAULT  10
#define DPMS_TIMEOUT_DEFAULT  300
#define CLOCK_TIME_SIZE       96
#define CLOCK_DATE_SIZE       24
#define CLOCK_GAP             24

struct cover {
	xcb_window_t root;
	xcb_window_t win;
	xcb_pixmap_t bg;
};

static const char *prog;
static int verbose;

static xcb_connection_t *xc;
static int nscreens;
static struct cover *covers;

static xcb_window_t focus_save_win;
static int focus_save_revert;

#ifdef __linux__
static int vt_fd = -1;
#endif

/* the password buffer is filled by the WINGs text field, read by the PAM
 * conversation callback or auth_userokay, then scrubbed */
static char password[512];

/* idle clock view: while no key, button or motion has arrived for
 * idle_timeout seconds the prompt is unmapped and a large time + date is
 * painted straight onto covers[0].win, the blurred background pixmap is
 * the cover's XSetWindowBackgroundPixmap so XClearWindow is the erase
 * primitive in both directions of the toggle */
static int idle_clock = 1;
static int idle_timeout = IDLE_TIMEOUT_DEFAULT;
static int dpms_timeout = DPMS_TIMEOUT_DEFAULT;
static const char *time_fmt;
static const char *date_fmt = "%A %e %B";

static int idle_mode;
static time_t last_activity;
static char clock_shown[128];

static WMScreen *wscr;
static WMInputPanel *panel;
static WMHandlerID idle_timer;
static WMFont *font_time;
static WMFont *font_date;
static WMColor *col_clock;
static int bg_bright;

static int dpms_capable;
static int dpms_forced_off;
#ifdef HAVE_XCB_DPMS
static int dpms_was_enabled;
#endif

#ifdef HAVE_PAM
static pam_handle_t *pamh;
#endif

static void diag(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void diag(const char *fmt, ...)
{
	va_list ap;

	if (!verbose)
		return;
	va_start(ap, fmt);
	fprintf(stderr, "%s: ", prog);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

static void fatal(const char *fmt, ...)
	__attribute__((format(printf, 1, 2))) __attribute__((noreturn));

static void fatal(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "%s: ", prog);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
	exit(1);
}

#ifdef __linux__
static void protect_from_oom(void)
{
	FILE *f;

	f = fopen("/proc/self/oom_score_adj", "w");
	if (!f) {
		diag("oom_score_adj open: %s", strerror(errno));
		return;
	}
	fprintf(f, "%d\n", OOM_SCORE_ADJ_MIN);
	if (fclose(f))
		diag("oom_score_adj write: %s (locker may need cap_sys_resource)",
		     strerror(errno));
}

static void vt_lock(int lock)
{
	if (lock) {
		vt_fd = open("/dev/console", O_RDWR | O_CLOEXEC);
		if (vt_fd < 0) {
			diag("VT switch lock: open /dev/console: %s", strerror(errno));
			return;
		}
		if (ioctl(vt_fd, VT_LOCKSWITCH) < 0)
			diag("VT switch lock: ioctl: %s", strerror(errno));
	} else if (vt_fd >= 0) {
		if (ioctl(vt_fd, VT_UNLOCKSWITCH) < 0)
			diag("VT switch unlock: ioctl: %s", strerror(errno));
		close(vt_fd);
		vt_fd = -1;
	}
}
#endif

#ifdef HAVE_PAM
static int pam_conv_cb(int n, const struct pam_message **msg,
		       struct pam_response **resp, void *data)
{
	struct pam_response *r;
	int i;

	(void)data;

	if (n <= 0)
		return PAM_CONV_ERR;

	r = calloc((size_t)n, sizeof(*r));
	if (!r)
		return PAM_CONV_ERR;

	for (i = 0; i < n; i++) {
		r[i].resp_retcode = 0;
		r[i].resp = NULL;
		if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF ||
		    msg[i]->msg_style == PAM_PROMPT_ECHO_ON)
			r[i].resp = strdup(password);
	}
	*resp = r;
	return PAM_SUCCESS;
}
#endif

static int authenticate(const char *user)
{
#if defined(HAVE_PAM)
	int rc;

	(void)user;
	rc = pam_authenticate(pamh, 0);
	if (rc == PAM_SUCCESS)
		pam_setcred(pamh, PAM_REFRESH_CRED);
	else
		diag("pam_authenticate: %s", pam_strerror(pamh, rc));
	return rc == PAM_SUCCESS;
#elif defined(HAVE_BSD_AUTH)
	/* auth_userokay frees a non-NULL password and may write to user,
	 * so hand it private copies */
	char ucopy[64];
	char *pw;

	snprintf(ucopy, sizeof(ucopy), "%s", user);
	pw = strdup(password);
	if (!pw)
		return 0;
	return auth_userokay(ucopy, NULL, "auth-wmlock", pw) != 0;
#else
	(void)user;
	return 0;
#endif
}

static xcb_pixmap_t make_background(RContext *rc, xcb_window_t root, int *bright)
{
	RImage *rim;
	xcb_pixmap_t pix;
	long sum;
	int n, step, x, y, w, h, x0, y0, x1, y1, row, off;

	pix = XCB_NONE;
	*bright = 0;
	rim = RCreateImageFromDrawable(rc, root, XCB_NONE);
	if (!rim)
		return XCB_NONE;

	RGrayscaleImage(rim);
	if (!RStackBlurImage(rim, BLUR_RADIUS))
		diag("RStackBlurImage: %s", RMessageForError(RErrorCode));

	w = rim->width;
	h = rim->height;
	step = rim->format == RRGBAFormat ? 4 : 3;
	x0 = w / 4;
	x1 = w - x0;
	y0 = h / 3;
	y1 = h - y0;
	for (y = y0, sum = 0, n = 0; y < y1; y += 4) {
		row = y * w * step;
		for (x = x0; x < x1; x += 4) {
			off = row + x * step;
			sum += rim->data[off] + rim->data[off + 1] + rim->data[off + 2];
			n += 3;
		}
	}
	if (n)
		*bright = (sum / n) > 128;

	if (!RConvertImage(rc, rim, &pix))
		pix = XCB_NONE;
	RReleaseImage(rim);
	return pix;
}

static int create_covers(WMScreen *scr)
{
	WXSetWindowAttributes wa;
	WXColor black;
	xcb_cursor_t invisible;
	xcb_pixmap_t bit;
	uint32_t vals[15];
	uint32_t mask;
	RContext *rc;
	int i, w, h;
	char curs[8];

	rc = WMScreenRContext(scr);
	memset(curs, 0, sizeof(curs));

	covers = calloc((size_t)nscreens, sizeof(*covers));
	if (!covers)
		return 0;

	for (i = 0; i < nscreens; i++) {
		const xcb_screen_t *xscr = wXScreenOf(xc, i);

		covers[i].root = wXRootWindow(xc, i);
		w = wXDisplayWidth(xc, i);
		h = wXDisplayHeight(xc, i);

		wa.override_redirect = true;
		wa.background_pixel = xscr->black_pixel;
		wa.event_mask = XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
				XCB_EVENT_MASK_EXPOSURE |
				XCB_EVENT_MASK_VISIBILITY_CHANGE |
				XCB_EVENT_MASK_STRUCTURE_NOTIFY;

		covers[i].win = xcb_generate_id(xc);
		xcb_create_window(xc, XCB_COPY_FROM_PARENT,
				  covers[i].win, covers[i].root, 0, 0,
				  (unsigned int)w, (unsigned int)h, 0,
				  XCB_WINDOW_CLASS_COPY_FROM_PARENT,
				  XCB_COPY_FROM_PARENT,
				  wXWindowAttrValues(&wa,
				      XCB_CW_OVERRIDE_REDIRECT | XCB_CW_BACK_PIXEL |
				      XCB_CW_EVENT_MASK, vals), vals);

		/* the WINGs RContext is bound to screen 0, only that one can
		 * use the wrlib pipeline for the blurred snapshot, the rest
		 * stay solid black */
		if (i == 0) {
			covers[i].bg = make_background(rc, covers[i].root, &bg_bright);
			if (covers[i].bg != XCB_NONE)
				xcb_change_window_attributes(xc, covers[i].win,
							     XCB_CW_BACK_PIXMAP,
							     &covers[i].bg);
		}

		/* hide the cursor on this cover */
		bit = wXCreateBitmapFromData(xc, covers[i].win, curs, 8, 8);
		memset(&black, 0, sizeof(black));
		invisible = wXCreatePixmapCursor(xc, bit, bit, &black, &black, 0, 0);
		wXDefineCursor(xc, covers[i].win, invisible);
		wXFreePixmap(xc, bit);
		xcb_free_cursor(xc, invisible);

		/* StructureNotify on the root delivers the ConfigureNotify
		 * RandR emits on every screen-size change, so the cover can
		 * be resized without linking libXrandr or registering an
		 * extension wire-to-event converter, SubstructureNotify is
		 * for the raise_covers() restack watch */
		mask = XCB_EVENT_MASK_STRUCTURE_NOTIFY |
		       XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;
		xcb_change_window_attributes(xc, covers[i].root,
					     XCB_CW_EVENT_MASK, &mask);
	}
	return 1;
}

static int grab_inputs(void)
{
	int i, kb, pt;

	kb = pt = -1;
	for (i = 0; i < GRAB_TRIES; i++) {
		if (kb != WXGrabSuccess)
			kb = wXGrabKeyboard(xc, covers[0].win, true,
					    XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
					    XCB_CURRENT_TIME);
		if (pt != WXGrabSuccess)
			pt = wXGrabPointer(xc, covers[0].win, false,
					  XCB_EVENT_MASK_BUTTON_PRESS |
					  XCB_EVENT_MASK_BUTTON_RELEASE |
					  XCB_EVENT_MASK_POINTER_MOTION,
					  XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
					  XCB_NONE, XCB_NONE, XCB_CURRENT_TIME);
		if (kb == WXGrabSuccess && pt == WXGrabSuccess)
			return 1;
		if ((kb != WXGrabSuccess && kb != WXGrabAlreadyGrabbed) ||
		    (pt != WXGrabSuccess && pt != WXGrabAlreadyGrabbed))
			break;
		usleep(GRAB_WAIT_US);
	}
	if (kb != WXGrabSuccess)
		fprintf(stderr, "%s: cannot grab keyboard\n", prog);
	if (pt != WXGrabSuccess)
		fprintf(stderr, "%s: cannot grab pointer\n", prog);
	return 0;
}

static void raise_covers(void)
{
	int i;

	for (i = 0; i < nscreens; i++)
		wXRaiseWindow(xc, covers[i].win);
}

static int is_cover(xcb_window_t w)
{
	int i;

	for (i = 0; i < nscreens; i++)
		if (covers[i].win == w || covers[i].root == w)
			return 1;
	return 0;
}

static void dpms_init(void)
{
#ifdef HAVE_XCB_DPMS
	const xcb_query_extension_reply_t *ext;
	xcb_dpms_capable_reply_t *cr;
	xcb_dpms_info_reply_t *ir;
	xcb_generic_error_t *err;

	/* an extension request whose major opcode resolves to 0 because
	 * the extension is absent poisons the whole connection (libxcb
	 * refuses the request: XCB_CONN_ERROR on every later reply),
	 * so check presence before issuing any DPMS request */
	ext = xcb_get_extension_data(xc, &xcb_dpms_id);
	if (!ext || !ext->present) {
		diag("DPMS extension not present");
		return;
	}

	err = NULL;
	cr = xcb_dpms_capable_reply(xc, xcb_dpms_capable(xc), &err);
	free(err);
	if (!cr)
		return;
	dpms_capable = cr->capable;
	free(cr);
	if (!dpms_capable) {
		diag("DPMS not capable");
		return;
	}

	err = NULL;
	ir = xcb_dpms_info_reply(xc, xcb_dpms_info(xc), &err);
	free(err);
	if (ir) {
		dpms_was_enabled = ir->state;
		free(ir);
	}
	diag("DPMS capable, was %s", dpms_was_enabled ? "enabled" : "disabled");
#endif
}

static void dpms_force(int off)
{
	if (!dpms_capable)
		return;
#ifdef HAVE_XCB_DPMS
	if (off) {
		/* ForceLevel is a no-op while DPMS is disabled, enable it
		 * for the duration of the blank and restore the prior
		 * enable state when input wakes the prompt or the lock
		 * exits */
		xcb_dpms_enable(xc);
		xcb_dpms_force_level(xc, XCB_DPMS_DPMS_MODE_OFF);
		dpms_forced_off = 1;
	} else {
		xcb_dpms_force_level(xc, XCB_DPMS_DPMS_MODE_ON);
		if (!dpms_was_enabled)
			xcb_dpms_disable(xc);
		dpms_forced_off = 0;
	}
	xcb_flush(xc);
#else
	(void)off;
#endif
}

static void draw_clock(void)
{
	time_t now;
	struct tm tm;
	char tbuf[64];
	char dbuf[64];
	char key[128];
	const char *dp;
	int sw, sh, tw, th, dw, dh, total, y;

	now = time(NULL);
	localtime_r(&now, &tm);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
	/* both formats come from the command line */
	strftime(tbuf, sizeof(tbuf), time_fmt, &tm);
	strftime(dbuf, sizeof(dbuf), date_fmt, &tm);
#pragma GCC diagnostic pop

	/* %e space-pads single-digit days, drop a leading blank so the
	 * centred date does not look shifted */
	dp = dbuf;
	while (*dp == ' ')
		dp++;

	snprintf(key, sizeof(key), "%s\n%s", tbuf, dp);
	if (strcmp(key, clock_shown) == 0)
		return;

	sw = wXDisplayWidth(xc, 0);
	sh = wXDisplayHeight(xc, 0);
	th = (int)WMFontHeight(font_time);
	dh = (int)WMFontHeight(font_date);
	tw = WMWidthOfString(font_time, tbuf, (int)strlen(tbuf));
	dw = WMWidthOfString(font_date, dp, (int)strlen(dp));
	total = th + CLOCK_GAP + dh;
	y = (sh - total) / 2;

	wXClearArea(xc, covers[0].win, 0, 0, 0, 0, false);
	WMDrawString(wscr, covers[0].win, col_clock, font_time,
		     (sw - tw) / 2, y, tbuf, (int)strlen(tbuf));
	WMDrawString(wscr, covers[0].win, col_clock, font_date,
		     (sw - dw) / 2, y + th + CLOCK_GAP, dp, (int)strlen(dp));
	wXFlush(xc);

	snprintf(clock_shown, sizeof(clock_shown), "%s", key);
}

static void enter_idle(void)
{
	WMSetTextFieldText(panel->text, "");
	WMUnmapWidget(panel->win);
	idle_mode = 1;
	clock_shown[0] = '\0';
	draw_clock();
}

static void leave_idle(void)
{
	idle_mode = 0;
	wXClearArea(xc, covers[0].win, 0, 0, 0, 0, false);
	WMMapWidget(panel->win);
	WMSetFocusToWidget(panel->text);
	wXFlush(xc);
}

static void idle_tick(void *data)
{
	time_t now;

	(void)data;
	now = time(NULL);

	if (!idle_mode && idle_clock && now - last_activity >= idle_timeout)
		enter_idle();
	if (idle_mode)
		draw_clock();
	if (dpms_timeout > 0 && dpms_capable && !dpms_forced_off &&
	    now - last_activity >= dpms_timeout) {
		dpms_force(1);
		/* nothing left to count down to and the panel is dark,
		 * stop waking every second so the locker parks in
		 * epoll on the X fd alone, note_activity() re-arms on
		 * the first key, button or motion event */
		WMDeleteTimerHandler(idle_timer);
		idle_timer = NULL;
	}
}

static void note_activity(void)
{
	last_activity = time(NULL);

	/* the X server wakes DPMS on any input-device event, mirror
	 * that in our own state and restart the per-second countdown
	 * that idle_tick() dropped when it forced the panel off, this
	 * also covers the --dpms-timeout < --idle-timeout corner where
	 * leave_idle() would not have run */
	if (dpms_forced_off)
		dpms_force(0);
	if (!idle_timer)
		idle_timer = WMAddPersistentTimerHandler(1000, idle_tick, NULL);
}

static void run_lock(WMScreen *scr, const char *user)
{
	xcb_window_t pwin;
	xcb_generic_event_t *ev = NULL;
	char *txt;
	char msg[128];
	int sw, sh, pw, ph, fails, woke;

	wscr = scr;
	sw = wXDisplayWidth(xc, 0);
	sh = wXDisplayHeight(xc, 0);

	font_time = WMSystemFontOfSize(scr, CLOCK_TIME_SIZE);
	font_date = WMSystemFontOfSize(scr, CLOCK_DATE_SIZE);
	col_clock = bg_bright ? WMBlackColor(scr) : WMWhiteColor(scr);

	panel = WMCreateInputPanel(scr, NULL, "Screen Locked",
				   "", "", "Unlock", NULL);
	WMSetTextFieldSecure(panel->text, true);
	WMSetTextFieldPlaceholder(panel->text, "Enter your password");
	WMRealizeWidget(panel->win);

	pwin = WMWidgetXID(panel->win);
	pw = WMWidgetWidth(panel->win);
	ph = WMWidgetHeight(panel->win);

	/* place the panel inside the cover so stacking is never a question,
	 * the cover already has override_redirect so the WM never sees it */
	xcb_reparent_window(xc, pwin, covers[0].win,
			    (int16_t)((sw - pw) / 2), (int16_t)((sh - ph) / 2));
	panel->result = WAPROther;

	last_activity = time(NULL);
	if (idle_clock) {
		enter_idle();
	} else {
		WMMapWidget(panel->win);
		WMSetFocusToWidget(panel->text);
	}
	idle_timer = WMAddPersistentTimerHandler(1000, idle_tick, NULL);

	wXSync(xc);
	fails = 0;

	for (;;) {
		/* each iteration owns one event, continue paths and the
		 * bottom of the body both hand it back here */
		free(ev);
		ev = NULL;

		/* WMHandleEvent drains the WINGs internal pending list
		 * during Expose compression, so a bare xcb_wait_for_event
		 * here would lose any event queued behind a panel Expose,
		 * go through WMNextEvent which reads that list first */
		WMNextEvent(xc, &ev);

		if ((ev->response_type & 0x7f) == XCB_CONFIGURE_NOTIFY) {
			int i;
			WXWindowChanges wc;

			for (i = 0; i < nscreens; i++) {
				if (covers[i].root != (((xcb_configure_notify_event_t *)ev)->window))
					continue;
				wc.width = ((xcb_configure_notify_event_t *)ev)->width;
				wc.height = ((xcb_configure_notify_event_t *)ev)->height;
				wXConfigureWindow(xc, covers[i].win,
						  XCB_CONFIG_WINDOW_WIDTH |
						  XCB_CONFIG_WINDOW_HEIGHT, &wc);
				wXClearArea(xc, covers[i].win, 0, 0, 0, 0, false);
				if (i == 0) {
					wc.x = (((xcb_configure_notify_event_t *)ev)->width - pw) / 2;
					wc.y = (((xcb_configure_notify_event_t *)ev)->height - ph) / 2;
					wXConfigureWindow(xc, pwin,
							  XCB_CONFIG_WINDOW_X |
							  XCB_CONFIG_WINDOW_Y, &wc);
					clock_shown[0] = '\0';
					if (idle_mode)
						draw_clock();
				}
				raise_covers();
			}
			/* fall through, child-of-root and cover-window
			 * ConfigureNotify is the raise_covers() trigger and
			 * the WINGs panel needs its own */
		}

		if ((ev->response_type & 0x7f) == XCB_KEY_PRESS || (ev->response_type & 0x7f) == XCB_KEY_RELEASE) {
			/* the keyboard grab delivers everything to the
			 * cover, intercept Escape ourselves and forward the
			 * rest to the panel so the WINGs text field handles
			 * editing and Return triggers the default button */
			note_activity();
			woke = 0;
			if (idle_mode && (ev->response_type & 0x7f) == XCB_KEY_PRESS) {
				leave_idle();
				woke = 1;
			}
			if ((ev->response_type & 0x7f) == XCB_KEY_PRESS) {
				xkb_keysym_t ks = W_EventKeysym(wscr,
						(const xcb_key_press_event_t *)ev);

				if (ks == XKB_KEY_Escape) {
					WMSetTextFieldText(panel->text, "");
					continue;
				}
				/* Return on the keystroke that revealed the
				 * prompt would submit an empty password and
				 * count a failure, swallow it so the first
				 * Return only wakes */
				if (woke && (ks == XKB_KEY_Return || ks == XKB_KEY_KP_Enter))
					continue;
			}
			if (idle_mode)
				continue;
			((xcb_key_press_event_t *)ev)->event = pwin;
			WMHandleEvent(ev);
		} else if ((ev->response_type & 0x7f) == XCB_VISIBILITY_NOTIFY || (ev->response_type & 0x7f) == XCB_MAP_NOTIFY ||
			   (ev->response_type & 0x7f) == XCB_CONFIGURE_NOTIFY) {
			if (is_cover(W_EventWindow(ev)))
				raise_covers();
			else
				WMHandleEvent(ev);
		} else if ((ev->response_type & 0x7f) == XCB_BUTTON_PRESS || (ev->response_type & 0x7f) == XCB_BUTTON_RELEASE ||
			   (ev->response_type & 0x7f) == XCB_MOTION_NOTIFY) {
			/* pointer is grabbed with owner_events false so every
			 * pointer event lands on the cover, the panel itself
			 * stays keyboard-only but motion or a click counts as
			 * activity for the idle clock and DPMS wake */
			note_activity();
			if (idle_mode)
				leave_idle();
			continue;
		} else if ((ev->response_type & 0x7f) == XCB_EXPOSE) {
			if ((((xcb_expose_event_t *)ev)->window) == covers[0].win) {
				if (idle_mode && (((xcb_expose_event_t *)ev)->count) == 0) {
					clock_shown[0] = '\0';
					draw_clock();
				}
			} else {
				WMHandleEvent(ev);
			}
		} else {
			/* focus, property and the rest go to WINGs if it
			 * owns the window, otherwise drop them */
			WMHandleEvent(ev);
		}

		if (panel->result != WAPRDefault)
			continue;

		/* Return was pressed, collect the field, scrub it, try */
		txt = WMGetTextFieldText(panel->text);
		WMSetTextFieldText(panel->text, "");
		panel->result = WAPROther;

		if (txt) {
			snprintf(password, sizeof(password), "%s", txt);
			scrub(txt, strlen(txt));
			wfree(txt);
		} else {
			password[0] = '\0';
		}

		/* PAM may block for several seconds on a wrong password,
		 * flush the cleared field and the un-pressed button repaint
		 * to the server first and tell the user what is happening */
		if (panel->mLbl)
			WMSetLabelText(panel->mLbl, "Authenticating...");
		wXSync(xc);

		if (authenticate(user)) {
			scrub(password, sizeof(password));
			break;
		}
		scrub(password, sizeof(password));

		fails++;
		snprintf(msg, sizeof(msg),
			 "Authentication failed (%d). Try again:", fails);
		if (panel->mLbl)
			WMSetLabelText(panel->mLbl, msg);
		wXBell(xc, 100);
		wXSync(xc);
	}

	WMDeleteTimerHandler(idle_timer);
	WMDestroyInputPanel(panel);
	WMReleaseFont(font_time);
	WMReleaseFont(font_date);
	WMReleaseColor(col_clock);
}

static void cleanup(void)
{
	int i;

#ifdef __linux__
	vt_lock(0);
#endif
	if (xc == NULL)
		return;
	if (dpms_forced_off)
		dpms_force(0);
	wXUngrabKeyboard(xc, XCB_CURRENT_TIME);
	wXUngrabPointer(xc, XCB_CURRENT_TIME);
	if (covers) {
		for (i = 0; i < nscreens; i++) {
			if (covers[i].bg != XCB_NONE)
				wXFreePixmap(xc, covers[i].bg);
			if (covers[i].win != XCB_NONE)
				xcb_destroy_window(xc, covers[i].win);
		}
		free(covers);
		covers = NULL;
	}
#ifdef HAVE_PAM
	if (pamh) {
		pam_end(pamh, PAM_SUCCESS);
		pamh = NULL;
	}
#endif
	if (focus_save_win != XCB_NONE) {
		wXSync(xc);
		/* the XSetErrorHandler trap that used to swallow the
		 * focus_save restore errors (the cover may already be
		 * gone) becomes a checked request with the error
		 * discarded, the §2.4 rule */
		(void) WXCheckVoid(xc,
				   xcb_set_input_focus_checked(xc, focus_save_revert,
							       focus_save_win,
							       XCB_CURRENT_TIME));
		wXSync(xc);
	}
	wXSync(xc);

	if (wscr) {
		WMShutdownScreen(wscr);
		wscr = NULL;
		WMShutdownFonts();
	}
}

static void usage(int status) __attribute__((noreturn));

static void usage(int status)
{
	FILE *out = status ? stderr : stdout;

	fprintf(out, "Usage: %s [OPTIONS]\n", prog);
	fprintf(out, "Lock the X display until the invoking user's password is entered.\n\n");
	fprintf(out, "  --no-idle-clock         keep the prompt visible, never show the clock\n");
	fprintf(out, "  --idle-timeout SECS     hide the prompt after SECS without input (default %d)\n",
		IDLE_TIMEOUT_DEFAULT);
	fprintf(out, "  --dpms-timeout SECS     force the monitor off after SECS without input,\n");
	fprintf(out, "                          0 disables (default %d)\n", DPMS_TIMEOUT_DEFAULT);
	fprintf(out, "  --time-format FMT       strftime(3) format for the clock time line\n");
	fprintf(out, "                          (default \"%%I:%%M %%p\" or \"%%H:%%M\" by locale)\n");
	fprintf(out, "  --date-format FMT       strftime(3) format for the clock date (default \"%%A %%e %%B\")\n");
	fprintf(out, "  -v, --verbose           print best-effort diagnostics to stderr\n");
	fprintf(out, "  -h, --help              display this help message\n");
	fprintf(out, "  --version               output version information and exit\n");
	exit(status);
}

int main(int argc, char **argv)
{
	int sn = 0;

	enum {
		OPT_NO_IDLE_CLOCK = 256,
		OPT_IDLE_TIMEOUT,
		OPT_DPMS_TIMEOUT,
		OPT_TIME_FORMAT,
		OPT_DATE_FORMAT,
		OPT_VERSION,
	};
	WMScreen *scr;
	struct passwd *pw;
	const char *user;
	int ch, i;
	xcb_get_input_focus_cookie_t ck;
	xcb_get_input_focus_reply_t *rep;
	struct option longopts[] = {
		{ "verbose",       no_argument,       NULL, 'v' },
		{ "no-idle-clock",  no_argument,       NULL, OPT_NO_IDLE_CLOCK },
		{ "idle-timeout",   required_argument, NULL, OPT_IDLE_TIMEOUT },
		{ "dpms-timeout",   required_argument, NULL, OPT_DPMS_TIMEOUT },
		{ "time-format",    required_argument, NULL, OPT_TIME_FORMAT },
		{ "date-format",    required_argument, NULL, OPT_DATE_FORMAT },
		{ "help",           no_argument,       NULL, 'h' },
		{ "version",        no_argument,       NULL, OPT_VERSION },
		{ NULL,             0,                 NULL, 0 }
	};
#ifdef HAVE_PAM
	struct pam_conv conv;
	int rc;
#endif

	prog = argv[0];
	setlocale(LC_ALL, "");

	while ((ch = getopt_long(argc, argv, "vh", longopts, NULL)) != -1) {
		switch (ch) {
		case 'v':
			verbose = 1;
			break;
		case OPT_NO_IDLE_CLOCK:
			idle_clock = 0;
			break;
		case OPT_IDLE_TIMEOUT:
			idle_timeout = atoi(optarg);
			if (idle_timeout <= 0)
				fatal("--idle-timeout must be a positive number of seconds");
			break;
		case OPT_DPMS_TIMEOUT:
			dpms_timeout = atoi(optarg);
			if (dpms_timeout < 0)
				fatal("--dpms-timeout must be zero or a positive number of seconds");
			break;
		case OPT_TIME_FORMAT:
			time_fmt = optarg;
			break;
		case OPT_DATE_FORMAT:
			date_fmt = optarg;
			break;
		case 'h':
			usage(0);
			/* NOTREACHED */
		case OPT_VERSION:
			printf("wmlock (Window Maker %s)\n", VERSION);
			return 0;
		default:
			usage(2);
			/* NOTREACHED */
		}
	}

	/* T_FMT_AMPM is the locale's 12-hour clock format and is empty in
	 * locales that do not use one, so use it as the 12h/24h selector
	 * and pick a seconds-free format ourselves rather than the
	 * locale's own which usually carries seconds */
	if (!time_fmt) {
		const char *ampm = nl_langinfo(T_FMT_AMPM);

		time_fmt = (ampm && *ampm) ? "%I:%M %p" : "%H:%M";
	}

	pw = getpwuid(getuid());
	if (!pw || !pw->pw_name)
		fatal("cannot determine current user");
	user = pw->pw_name;

#ifdef HAVE_PAM
	conv.conv = pam_conv_cb;
	conv.appdata_ptr = NULL;
	rc = pam_start(WMLOCK_PAM_SERVICE, user, &conv, &pamh);
	if (rc != PAM_SUCCESS)
		fatal("pam_start(%s): %s", WMLOCK_PAM_SERVICE, pam_strerror(pamh, rc));
	if (getenv("DISPLAY"))
		pam_set_item(pamh, PAM_TTY, getenv("DISPLAY"));
#endif

#ifdef __linux__
	protect_from_oom();
	if (mlock(password, sizeof(password)) != 0)
		diag("mlock password buffer: %s", strerror(errno));
#endif

	WMInitializeApplication("wmlock", &argc, argv);

	xc = xcconn_open(NULL, NULL, 0, &sn);
	if (!xc)
		fatal("cannot open display");

	scr = WMCreateSimpleApplicationScreen(xc, sn);
	if (!scr)
		fatal("cannot initialize application");
	nscreens = wXScreenCount(xc);

	ck = xcb_get_input_focus(xc);
	rep = xcb_get_input_focus_reply(xc, ck, NULL);
	if (rep) {
		focus_save_win = rep->focus;
		focus_save_revert = rep->revert_to;
		free(rep);
	}

	dpms_init();
	atexit(cleanup);

	if (!create_covers(scr))
		fatal("out of memory");

	/* the grab target must be viewable, map screen 0 first and sync
	 * so the server has it before the keyboard grab runs */
	wXMapRaised(xc, covers[0].win);
	wXSync(xc);
	if (!grab_inputs())
		return 1;
	for (i = 1; i < nscreens; i++)
		wXMapRaised(xc, covers[i].win);
	wXSync(xc);

#ifdef __linux__
	vt_lock(1);
#endif

	run_lock(scr, user);

	return 0;
}
