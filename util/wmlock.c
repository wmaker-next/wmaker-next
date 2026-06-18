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
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

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

static Pixmap make_background(RContext *rc, Window root, int w, int h)
{
	XImage *xim;
	RImage *rim;
	Pixmap pix;

	pix = None;
	xim = XGetImage(dpy, root, 0, 0, (unsigned)w, (unsigned)h, AllPlanes, ZPixmap);
	if (!xim)
		return None;

	rim = RCreateImageFromXImage(rc, xim, NULL);
	XDestroyImage(xim);
	if (!rim)
		return None;

	RGrayscaleImage(rim);
	if (!RStackBlurImage(rim, BLUR_RADIUS))
		diag("RStackBlurImage: %s", RMessageForError(RErrorCode));

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
		wa.event_mask = KeyPressMask | KeyReleaseMask |
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
			covers[i].bg = make_background(rc, covers[i].root, w, h);
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

static void run_lock(WMScreen *scr, const char *user)
{
	WMInputPanel *panel;
	Window pwin;
	XEvent ev;
	KeySym ks;
	char *txt;
	char msg[128];
	int sw, sh, pw, ph, fails;

	sw = DisplayWidth(dpy, 0);
	sh = DisplayHeight(dpy, 0);

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
	WMMapWidget(panel->win);
	WMSetFocusToWidget(panel->text);
	panel->result = WAPROther;

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
				if (i == 0)
					XMoveWindow(dpy, pwin,
						    (ev.xconfigure.width - pw) / 2,
						    (ev.xconfigure.height - ph) / 2);
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
			if (ev.type == KeyPress) {
				ks = XLookupKeysym(&ev.xkey, 0);
				if (ks == XK_Escape) {
					WMSetTextFieldText(panel->text, "");
					continue;
				}
			}
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
			/* pointer is grabbed with owner_events False, every
			 * pointer event lands on the cover and is discarded,
			 * the panel is keyboard-only by design */
			continue;
		} else {
			/* expose, focus, property and the rest go to WINGs
			 * if it owns the window, otherwise drop them */
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

	WMDestroyInputPanel(panel);
}

static void cleanup(void)
{
	int i;

#ifdef __linux__
	vt_lock(0);
#endif
	if (!dpy)
		return;
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
	fprintf(out, "  -v, --verbose           print best-effort diagnostics to stderr\n");
	fprintf(out, "  -h, --help              display this help message\n");
	fprintf(out, "  --version               output version information and exit\n");
	exit(status);
}

int main(int argc, char **argv)
{
	enum { OPT_VERSION = 256 };
	WMScreen *scr;
	struct passwd *pw;
	const char *user;
	int ch, i;
	struct option longopts[] = {
		{ "verbose",  no_argument, NULL, 'v' },
		{ "help",     no_argument, NULL, 'h' },
		{ "version",  no_argument, NULL, OPT_VERSION },
		{ NULL,       0,           NULL, 0 }
	};
#ifdef HAVE_PAM
	struct pam_conv conv;
	int rc;
#endif

	prog = argv[0];

	while ((ch = getopt_long(argc, argv, "vh", longopts, NULL)) != -1) {
		switch (ch) {
		case 'v':
			verbose = 1;
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
