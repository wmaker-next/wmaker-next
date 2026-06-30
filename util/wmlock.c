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

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#ifdef HAVE_XCB_DPMS
#include <X11/Xlib-xcb.h>
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
	Window root;
	Window win;
	Pixmap bg;
};

static const char *prog;
static int verbose;

static Display *dpy;
static int nscreens;
static struct cover *covers;

static Window focus_save_win;
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
static xcb_connection_t *xc;
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

static int focus_save_errhandler(Display *d, XErrorEvent *e)
{
	(void)d;
	(void)e;
	return 0;
}

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

static Pixmap make_background(RContext *rc, Window root, int *bright)
{
	RImage *rim;
	Pixmap pix;
	long sum;
	int n, step, x, y, w, h, x0, y0, x1, y1, row, off;

	pix = None;
	*bright = 0;
	rim = RCreateImageFromDrawable(rc, root, None);
	if (!rim)
		return None;

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
		pix = None;
	RReleaseImage(rim);
	return pix;
}

static int create_covers(WMScreen *scr)
{
	XSetWindowAttributes wa;
	XColor black;
	Cursor invisible;
	Pixmap bit;
	RContext *rc;
	int i, w, h;
	char curs[8];

	rc = WMScreenRContext(scr);
	memset(curs, 0, sizeof(curs));

	covers = calloc((size_t)nscreens, sizeof(*covers));
	if (!covers)
		return 0;

	for (i = 0; i < nscreens; i++) {
		covers[i].root = RootWindow(dpy, i);
		w = DisplayWidth(dpy, i);
		h = DisplayHeight(dpy, i);

		wa.override_redirect = True;
		wa.background_pixel = BlackPixel(dpy, i);
		wa.event_mask = KeyPressMask | KeyReleaseMask | ExposureMask |
				VisibilityChangeMask | StructureNotifyMask;

		covers[i].win = XCreateWindow(dpy, covers[i].root, 0, 0,
					      (unsigned)w, (unsigned)h, 0,
					      CopyFromParent, CopyFromParent,
					      CopyFromParent,
					      CWOverrideRedirect | CWBackPixel | CWEventMask,
					      &wa);

		/* the WINGs RContext is bound to screen 0, only that one can
		 * use the wrlib pipeline for the blurred snapshot, the rest
		 * stay solid black */
		if (i == 0) {
			covers[i].bg = make_background(rc, covers[i].root, &bg_bright);
			if (covers[i].bg != None)
				XSetWindowBackgroundPixmap(dpy, covers[i].win, covers[i].bg);
		}

		/* hide the cursor on this cover */
		bit = XCreateBitmapFromData(dpy, covers[i].win, curs, 8, 8);
		memset(&black, 0, sizeof(black));
		invisible = XCreatePixmapCursor(dpy, bit, bit, &black, &black, 0, 0);
		XDefineCursor(dpy, covers[i].win, invisible);
		XFreePixmap(dpy, bit);
		XFreeCursor(dpy, invisible);

		/* StructureNotify on the root delivers the ConfigureNotify
		 * RandR emits on every screen-size change, so the cover can
		 * be resized without linking libXrandr or registering an
		 * extension wire-to-event converter, SubstructureNotify is
		 * for the raise_covers() restack watch */
		XSelectInput(dpy, covers[i].root,
			     StructureNotifyMask | SubstructureNotifyMask);
	}
	return 1;
}

static int grab_inputs(void)
{
	int i, kb, pt;

	kb = pt = -1;
	for (i = 0; i < GRAB_TRIES; i++) {
		if (kb != GrabSuccess)
			kb = XGrabKeyboard(dpy, covers[0].win, True,
					   GrabModeAsync, GrabModeAsync, CurrentTime);
		if (pt != GrabSuccess)
			pt = XGrabPointer(dpy, covers[0].win, False,
					  ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
					  GrabModeAsync, GrabModeAsync,
					  None, None, CurrentTime);
		if (kb == GrabSuccess && pt == GrabSuccess)
			return 1;
		if ((kb != GrabSuccess && kb != AlreadyGrabbed) ||
		    (pt != GrabSuccess && pt != AlreadyGrabbed))
			break;
		usleep(GRAB_WAIT_US);
	}
	if (kb != GrabSuccess)
		fprintf(stderr, "%s: cannot grab keyboard\n", prog);
	if (pt != GrabSuccess)
		fprintf(stderr, "%s: cannot grab pointer\n", prog);
	return 0;
}

static void raise_covers(void)
{
	int i;

	for (i = 0; i < nscreens; i++)
		XRaiseWindow(dpy, covers[i].win);
}

static int is_cover(Window w)
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

	xc = XGetXCBConnection(dpy);

	/* Xlib still owns the event queue here, sending an extension
	 * request whose major opcode resolves to 0 because the extension
	 * is absent puts an error on the wire that desyncs Xlib's reply
	 * tracking, so check presence before issuing any DPMS request */
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
	strftime(tbuf, sizeof(tbuf), time_fmt, &tm);
	strftime(dbuf, sizeof(dbuf), date_fmt, &tm);

	/* %e space-pads single-digit days, drop a leading blank so the
	 * centred date does not look shifted */
	dp = dbuf;
	while (*dp == ' ')
		dp++;

	snprintf(key, sizeof(key), "%s\n%s", tbuf, dp);
	if (strcmp(key, clock_shown) == 0)
		return;

	sw = DisplayWidth(dpy, 0);
	sh = DisplayHeight(dpy, 0);
	th = (int)WMFontHeight(font_time);
	dh = (int)WMFontHeight(font_date);
	tw = WMWidthOfString(font_time, tbuf, (int)strlen(tbuf));
	dw = WMWidthOfString(font_date, dp, (int)strlen(dp));
	total = th + CLOCK_GAP + dh;
	y = (sh - total) / 2;

	XClearWindow(dpy, covers[0].win);
	WMDrawString(wscr, covers[0].win, col_clock, font_time,
		     (sw - tw) / 2, y, tbuf, (int)strlen(tbuf));
	WMDrawString(wscr, covers[0].win, col_clock, font_date,
		     (sw - dw) / 2, y + th + CLOCK_GAP, dp, (int)strlen(dp));
	XFlush(dpy);

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
	XClearWindow(dpy, covers[0].win);
	WMMapWidget(panel->win);
	WMSetFocusToWidget(panel->text);
	XFlush(dpy);
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
	Window pwin;
	XEvent ev;
	KeySym ks;
	char *txt;
	char msg[128];
	int sw, sh, pw, ph, fails, woke;

	wscr = scr;
	sw = DisplayWidth(dpy, 0);
	sh = DisplayHeight(dpy, 0);

	font_time = WMSystemFontOfSize(scr, CLOCK_TIME_SIZE);
	font_date = WMSystemFontOfSize(scr, CLOCK_DATE_SIZE);
	col_clock = bg_bright ? WMBlackColor(scr) : WMWhiteColor(scr);

	panel = WMCreateInputPanel(scr, NULL, "Screen Locked",
				   "Type your password and press Return:",
				   "", "Unlock", NULL);
	WMSetTextFieldSecure(panel->text, True);
	WMRealizeWidget(panel->win);

	pwin = WMWidgetXID(panel->win);
	pw = WMWidgetWidth(panel->win);
	ph = WMWidgetHeight(panel->win);

	/* place the panel inside the cover so stacking is never a question,
	 * the cover already has override_redirect so the WM never sees it */
	XReparentWindow(dpy, pwin, covers[0].win, (sw - pw) / 2, (sh - ph) / 2);
	panel->result = WAPROther;

	last_activity = time(NULL);
	if (idle_clock) {
		enter_idle();
	} else {
		WMMapWidget(panel->win);
		WMSetFocusToWidget(panel->text);
	}
	idle_timer = WMAddPersistentTimerHandler(1000, idle_tick, NULL);

	XSync(dpy, False);
	fails = 0;

	for (;;) {
		/* WMHandleEvent drains the Xlib queue into the WINGs
		 * internal pending list during Expose compression, so a
		 * bare XNextEvent here would lose any event queued behind
		 * a panel Expose, go through WMNextEvent which reads that
		 * list first */
		WMNextEvent(dpy, &ev);

		if (ev.type == ConfigureNotify) {
			int i;

			for (i = 0; i < nscreens; i++) {
				if (covers[i].root != ev.xconfigure.window)
					continue;
				XResizeWindow(dpy, covers[i].win,
					      (unsigned)ev.xconfigure.width,
					      (unsigned)ev.xconfigure.height);
				XClearWindow(dpy, covers[i].win);
				if (i == 0) {
					XMoveWindow(dpy, pwin,
						    (ev.xconfigure.width - pw) / 2,
						    (ev.xconfigure.height - ph) / 2);
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

		if (ev.type == KeyPress || ev.type == KeyRelease) {
			/* the keyboard grab delivers everything to the
			 * cover, intercept Escape ourselves and forward the
			 * rest to the panel so the WINGs text field handles
			 * editing and Return triggers the default button */
			note_activity();
			woke = 0;
			if (idle_mode && ev.type == KeyPress) {
				leave_idle();
				woke = 1;
			}
			if (ev.type == KeyPress) {
				ks = XLookupKeysym(&ev.xkey, 0);
				if (ks == XK_Escape) {
					WMSetTextFieldText(panel->text, "");
					continue;
				}
				/* Return on the keystroke that revealed the
				 * prompt would submit an empty password and
				 * count a failure, swallow it so the first
				 * Return only wakes */
				if (woke && (ks == XK_Return || ks == XK_KP_Enter))
					continue;
			}
			if (idle_mode)
				continue;
			ev.xany.window = pwin;
			WMHandleEvent(&ev);
		} else if (ev.type == VisibilityNotify || ev.type == MapNotify ||
			   ev.type == ConfigureNotify) {
			if (is_cover(ev.xany.window))
				raise_covers();
			else
				WMHandleEvent(&ev);
		} else if (ev.type == ButtonPress || ev.type == ButtonRelease ||
			   ev.type == MotionNotify) {
			/* pointer is grabbed with owner_events False so every
			 * pointer event lands on the cover, the panel itself
			 * stays keyboard-only but motion or a click counts as
			 * activity for the idle clock and DPMS wake */
			note_activity();
			if (idle_mode)
				leave_idle();
			continue;
		} else if (ev.type == Expose) {
			if (ev.xexpose.window == covers[0].win) {
				if (idle_mode && ev.xexpose.count == 0) {
					clock_shown[0] = '\0';
					draw_clock();
				}
			} else {
				WMHandleEvent(&ev);
			}
		} else {
			/* focus, property and the rest go to WINGs if it
			 * owns the window, otherwise drop them */
			WMHandleEvent(&ev);
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
		XSync(dpy, False);

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
		XBell(dpy, 100);
		XSync(dpy, False);
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
	if (!dpy)
		return;
	if (dpms_forced_off)
		dpms_force(0);
	XUngrabKeyboard(dpy, CurrentTime);
	XUngrabPointer(dpy, CurrentTime);
	if (covers) {
		for (i = 0; i < nscreens; i++) {
			if (covers[i].bg != None)
				XFreePixmap(dpy, covers[i].bg);
			if (covers[i].win != None)
				XDestroyWindow(dpy, covers[i].win);
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
	if (focus_save_win != None) {
		int (*old_handler)(Display *dpy, XErrorEvent *ev);

		XSync(dpy, False);
		old_handler = XSetErrorHandler(focus_save_errhandler);
		XSetInputFocus(dpy, focus_save_win, focus_save_revert, CurrentTime);
		XSync(dpy, False);
		XSetErrorHandler(old_handler);
	}
	XSync(dpy, False);
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
	scr = WMOpenScreen(NULL);
	if (!scr)
		fatal("cannot open display");
	dpy = WMScreenDisplay(scr);
	nscreens = ScreenCount(dpy);

	XGetInputFocus(dpy, &focus_save_win, &focus_save_revert);

	dpms_init();
	atexit(cleanup);

	if (!create_covers(scr))
		fatal("out of memory");

	/* the grab target must be viewable, map screen 0 first and sync
	 * so the server has it before XGrabKeyboard runs */
	XMapRaised(dpy, covers[0].win);
	XSync(dpy, False);
	if (!grab_inputs())
		return 1;
	for (i = 1; i < nscreens; i++)
		XMapRaised(dpy, covers[i].win);
	XSync(dpy, False);

#ifdef __linux__
	vt_lock(1);
#endif

	run_lock(scr, user);

	return 0;
}
