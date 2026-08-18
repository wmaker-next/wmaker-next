/* screen.c - screen management
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#ifdef USE_RANDR
#include "randr.h"
#endif
#ifdef USE_XCB_SCREENSAVER
#include <xcb/screensaver.h>
#endif

#include <cairo-xcb.h>

#include <wraster.h>
#include "WindowMaker.h"
#include "def_pixmaps.h"
#include "screen.h"
#include "config-schema.h"
#include "texture.h"
#include "pixmap.h"
#include "menu.h"
#include "window.h"
#include "main.h"
#include "actions.h"
#include "properties.h"
#include "dock.h"
#include "resources.h"
#include "workspace.h"
#include "layout.h"
#include "session.h"
#include "balloon.h"
#include "geomview.h"
#include "wireframe.h"
#include "wmspec.h"
#include "rootmenu.h"
#include "wxkb.h"
#include "misc.h"
#include "ipc.h"
#include "startup_notify.h"
#include "wsound.h"

#include "xinerama.h"

#include <WINGs/WUtil.h>
#include <WINGs/WINGsP.h>

#include "defaults.h"

/* PointerMotionMask is added on demand by wScreenUpdateRootEventMask()
 * below, only while ScrollableMenus is on and a menu is actually
 * mapped, so an idle desktop receives no root MotionNotify at all
 * instead of one per pointer pixel.
 *
 * ButtonMotionMask is always selected, it generates nothing while no
 * button is held so the idle case is unchanged, but a button press on
 * the root window starts an implicit grab whose event mask is frozen
 * at this selection, and menuMouseDown() relies on that grab carrying
 * motion to highlight entries as the pointer moves over the root menu
 * opened from that press */
#define EVENT_MASK (LeaveWindowMask|EnterWindowMask|PropertyChangeMask\
    |SubstructureNotifyMask \
    |SubstructureRedirectMask|ButtonPressMask|ButtonReleaseMask\
    |ButtonMotionMask|KeyPressMask|KeyReleaseMask)

#ifdef USE_ICCCM_WMREPLACE
#define REPLACE_WM_TIMEOUT 15
#endif

static WMPropList *dApplications = NULL;
static WMPropList *dWorkspace;
static WMPropList *dDock;
static WMPropList *dClip;
static WMPropList *dDrawers = NULL;

static void make_keys(void)
{
	if (dApplications != NULL)
		return;

	dApplications = WMCreatePLString("Applications");
	dWorkspace = WMCreatePLString("Workspace");
	dDock = WMCreatePLString("Dock");
	dClip = WMCreatePLString("Clip");
	dDrawers = WMCreatePLString("Drawers");
}

/*
 * Support for ICCCM 2.0: Window Manager Replacement protocol
 * See: http://www.x.org/releases/X11R7.6/doc/xorg-docs/specs/ICCCM/icccm.html
 *
 * Basically, user should be able to dynamically change its window manager; this is done
 * cooperatively through a special Selection ("WM_Sn" where 'n' is the X screen number)
 *
 * This function does 2 things:
 *  - it checks if this selection is already owned, which means that another window
 * manager is running. If it is the case and user specified '--replace' on the command
 * line, then it asks the WM to shut down;
 *  - when ok, it sets the selection ownership to ourself, so another window manager
 * may ask us to shut down (this is handled in "event.c")
 */
static Bool replace_existing_wm(WScreen *scr)
{
#ifdef USE_ICCCM_WMREPLACE
	char atomName[16];
	xcb_generic_error_t *err;
	Window wm;
	uint32_t mask;
	unsigned long current_time;
	int ret;

	/* Try to acquire the atom named WM_S<screen> */
	ret = snprintf(atomName, sizeof(atomName), "WM_S%d", scr->screen);
	if (ret < 0 || ret == sizeof(atomName)) {
		werror("out of memory trying to allocate window manager selection atom for screen %d", scr->screen);
		return False;
	}

	scr->sn_atom = XInternAtom(dpy, atomName, False);
	if (! scr->sn_atom)
		return False;

	/* Check if an existing window manager owns the selection */
	wm = XGetSelectionOwner(dpy, scr->sn_atom);
	if (wm) {
		if (!wPreferences.flags.replace) {
			wmessage(_("another window manager is running"));
			wwarning(_("use the --replace flag to replace it"));
			return False;
		}

		/* watch for the outgoing WM's selection-owner window to
		 * be destroyed, it may already have exited between
		 * GetSelectionOwner and here so a BadWindow means there
		 * is nothing to wait for */
		mask = XCB_EVENT_MASK_STRUCTURE_NOTIFY;
		err = xcb_request_check(xconn,
			xcb_change_window_attributes_checked(xconn, wm,
				XCB_CW_EVENT_MASK, &mask));
		if (err) {
			free(err);
			wm = None;
		}
	}
#endif

	/* for our window manager info notice board and the selection owner */
	scr->info_window = XCreateSimpleWindow(dpy, scr->root_win, 0, 0, 10, 10, 0, 0, 0);

#ifdef USE_ICCCM_WMREPLACE
	/* Try to acquire the selection */
	current_time = CurrentTime;
	ret = XSetSelectionOwner(dpy, scr->sn_atom, scr->info_window, current_time);
	if (ret == BadAtom || ret == BadWindow)
		return False;

	/* Wait for other window manager to exit */
	if (wm) {
		unsigned long wait = 0;
		unsigned long timeout = REPLACE_WM_TIMEOUT * 1000000L;
		XEvent event;

		while (wait < timeout) {
			if (!(wait % 1000000))
				wmessage(_("waiting %lus for other window manager to exit"), (timeout - wait) / 1000000L);

			if (WXEvqTakeMask(dpy, StructureNotifyMask, wm, &event))
				if (event.type == DestroyNotify)
					break;

			wusleep(100000);
			wait += 100000;
		}

		if (wait >= timeout) {
			wwarning(_("other window manager hasn't exited!"));
			return False;
		}

		wmessage(_("replacing the other window manager"));
	}

	if (XGetSelectionOwner(dpy, scr->sn_atom) != scr->info_window)
		return False;

	WXSendClientMessage(dpy, scr->root_win, StructureNotifyMask,
			    scr->root_win, scr->sn_atom,
			    (long)current_time, (long)scr->sn_atom,
			    (long)scr->info_window, 0, 0);
#endif

	return True;
}

/*
 *----------------------------------------------------------------------
 * alreadyRunningError--
 * allocButtonPixmaps--
 * 	Allocate pixmaps used on window operation buttons (those in the
 * titlebar). The pixmaps are linked to the program. If XPM is supported
 * XPM pixmaps are used otherwise, equivalent bitmaps are used.
 *
 * Returns:
 * 	Nothing
 *
 * Side effects:
 * 	Allocates shared pixmaps for the screen. These pixmaps should
 * not be freed by anybody.
 *----------------------------------------------------------------------
 *
 *      A non-empty *ButtonPixmap default overrides the compiled-in
 *      glyph for that slot. The themed image is resolved against
 *      PixmapPath like every other style asset and marked themed so
 *      paintButton() paints it as-is and keeps its own colours, the
 *      compiled-in fallback is taken on any load failure so a broken
 *      theme still gets working buttons.
 */
static WPixmap *load_button_file(WScreen *scr, const char *file)
{
	RImage *image;
	WPixmap *pix;
	char *path;

	if (!file || !*file)
		return NULL;

	path = FindImage(w_global.config->pixmap_path, file);
	if (!path) {
		wwarning(_("could not find button pixmap \"%s\" in PixmapPath"), file);
		return NULL;
	}
	image = RLoadImage(scr->rcontext, path, 0);
	wfree(path);
	if (!image) {
		wwarning(_("could not load button pixmap \"%s\": %s"), file,
			 RMessageForError(RErrorCode));
		return NULL;
	}

	pix = wmalloc(sizeof(WPixmap));
	if (!RConvertImageMask(scr->rcontext, image, &pix->image, &pix->mask, 128)) {
		RReleaseImage(image);
		wfree(pix);
		return NULL;
	}
	pix->width = image->width;
	pix->height = image->height;
	pix->depth = scr->w_depth;
	pix->themed = 1;
	RReleaseImage(image);

	return pix;
}

static WPixmap *load_button_slot(WScreen *scr, const char *file,
				 char **pred_xpm, char **next_xpm)
{
	WPixmap *pix;

	pix = load_button_file(scr, file);
	if (!pix)
		pix = wPixmapCreateFromXPMData(scr,
			(w_global.config->new_style == TS_NEXT) ? next_xpm : pred_xpm);
	if (pix)
		pix->shared = 1;
	return pix;
}

static void allocButtonPixmaps(WScreen *scr)
{
	const WConfig *cfg = w_global.config;

	scr->b_pixmaps[WBUT_CLOSE] =
	    load_button_slot(scr, cfg->close_button_pixmap,
			     PRED_CLOSE_XPM, NEXT_CLOSE_XPM);
	scr->b_pixmaps[WBUT_BROKENCLOSE] =
	    load_button_slot(scr, cfg->broken_close_button_pixmap,
			     PRED_BROKEN_CLOSE_XPM, NEXT_BROKEN_CLOSE_XPM);
	scr->b_pixmaps[WBUT_ICONIFY] =
	    load_button_slot(scr, cfg->miniaturize_button_pixmap,
			     PRED_ICONIFY_XPM, NEXT_ICONIFY_XPM);
	scr->b_pixmaps[WBUT_MAXIMIZE] =
	    load_button_slot(scr, cfg->maximize_button_pixmap,
			     PRED_MAXIMIZE_XPM, NEXT_MAXIMIZE_XPM);
	scr->b_pixmaps[WBUT_RESTORE] =
	    load_button_slot(scr, cfg->restore_button_pixmap,
			     PRED_RESTORE_XPM, NEXT_RESTORE_XPM);
	scr->b_pixmaps[WBUT_KILL] =
	    load_button_slot(scr, cfg->kill_button_pixmap,
			     PRED_KILL_XPM, NEXT_KILL_XPM);
#ifdef XKB_BUTTON_HINT
	scr->b_pixmaps[WBUT_XKBGROUP1] =
	    load_button_slot(scr, NULL, PRED_XKBGROUP1_XPM, NEXT_XKBGROUP1_XPM);
	scr->b_pixmaps[WBUT_XKBGROUP2] =
	    load_button_slot(scr, NULL, PRED_XKBGROUP2_XPM, NEXT_XKBGROUP2_XPM);
	scr->b_pixmaps[WBUT_XKBGROUP3] =
	    load_button_slot(scr, NULL, PRED_XKBGROUP3_XPM, NEXT_XKBGROUP3_XPM);
	scr->b_pixmaps[WBUT_XKBGROUP4] =
	    load_button_slot(scr, NULL, PRED_XKBGROUP4_XPM, NEXT_XKBGROUP4_XPM);
#endif
}

static int get_dot_mult(void)
{
    int dot_mult;
    
    /*
     * Since it's an int, the multiplier will grow only when
     * a certain icon size is reached (1 for 64, 2 for 128,
     * 3 for 192, and so on).
     */
    dot_mult = w_global.config->icon_size / 64;

    if (dot_mult < 1)
        dot_mult = 1;
    
    return dot_mult;
}

/* one 3x2 (times m) raised-bevel dot on the image half of the pair,
 * the mask half is a plain rectangle so only these six cells show */
static void draw_dot(cairo_t *cr, WScreen *scr, int x, int y, int m)
{
	WCairoSetSourceColor(cr, scr->black);
	cairo_rectangle(cr, x, y, 2 * m, m);
	cairo_rectangle(cr, x, y + m, m, m);
	cairo_fill(cr);
	WCairoSetSourceColor(cr, scr->white);
	cairo_rectangle(cr, x + 2 * m, y, m, m);
	cairo_rectangle(cr, x + m, y + m, 2 * m, m);
	cairo_fill(cr);
}

static WPixmap *make3Dots(WScreen *scr)
{
	RContext *rc = scr->rcontext;
	WPixmap *wpix;
	Pixmap pix, mask;
	cairo_surface_t *surf;
	cairo_t *cr;
	int isize, m, dy, i;

	isize = w_global.config->icon_size;
	m = get_dot_mult();
	dy = isize - 6 * m;

	pix = XCreatePixmap(dpy, scr->w_win, isize, isize, scr->w_depth);
	surf = cairo_xcb_surface_create(rc->xc, (xcb_drawable_t)pix,
					rc->xvisual, isize, isize);
	cr = cairo_create(surf);
	WCairoSetSourceColor(cr, scr->black);
	cairo_paint(cr);
	for (i = 0; i < 3; i++)
		draw_dot(cr, scr, (4 + 5 * i) * m, dy, m);
	cairo_destroy(cr);
	cairo_surface_flush(surf);
	cairo_surface_destroy(surf);

	mask = XCreatePixmap(dpy, scr->w_win, isize, isize, 1);
	surf = cairo_xcb_surface_create_for_bitmap(rc->xc, rc->xscreen,
						   (xcb_pixmap_t)mask, isize, isize);
	cr = cairo_create(surf);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(cr, 0, 0, 0, 0);
	cairo_paint(cr);
	cairo_set_source_rgba(cr, 0, 0, 0, 1);
	for (i = 0; i < 3; i++)
		cairo_rectangle(cr, (4 + 5 * i) * m, dy, 3 * m, 2 * m);
	cairo_fill(cr);
	cairo_destroy(cr);
	cairo_surface_flush(surf);
	cairo_surface_destroy(surf);

	wpix = wPixmapCreate(pix, mask);
	wpix->shared = 1;

	return wpix;
}

static void allocDefaultColors(WScreen *scr)
{
	scr->menu_title_color[0] = WMRetainColor(scr->white);

	/* don't retain scr->black here because we may alter its alpha */
	scr->mtext_color = WMCreateRGBColor(scr->wmscreen, 0, 0, 0, True);
	scr->dtext_color = WMCreateRGBColor(scr->wmscreen, 0, 0, 0, True);
}

static void createPixmaps(WScreen * scr)
{
	WPixmap *pix;

	/* load pixmaps */
#define LOADPIXMAPINDICATOR(XBM,W,I) {\
	pix = wPixmapCreateFromXBMData(scr, (char *)XBM, (char *)XBM,\
					W, MENU_SNAP_INDICATOR_H_XBM_SIZE,\
					scr->black_pixel, scr->white_pixel);\
	if (pix != NULL)\
		pix->shared = 1;\
	scr->I = pix;}

	LOADPIXMAPINDICATOR(MENU_RADIO_INDICATOR_XBM_DATA, MENU_RADIO_INDICATOR_XBM_SIZE, menu_radio_indicator)
	LOADPIXMAPINDICATOR(MENU_CHECK_INDICATOR_XBM_DATA, MENU_CHECK_INDICATOR_XBM_SIZE, menu_check_indicator)
	LOADPIXMAPINDICATOR(MENU_MINI_INDICATOR_XBM_DATA, MENU_MINI_INDICATOR_XBM_SIZE, menu_mini_indicator)
	LOADPIXMAPINDICATOR(MENU_HIDE_INDICATOR_XBM_DATA, MENU_HIDE_INDICATOR_XBM_SIZE, menu_hide_indicator)
	LOADPIXMAPINDICATOR(MENU_SHADE_INDICATOR_XBM_DATA, MENU_SHADE_INDICATOR_XBM_SIZE, menu_shade_indicator)

	LOADPIXMAPINDICATOR(MENU_SNAP_V_INDICATOR_XBM_DATA, MENU_SNAP_INDICATOR_W_XBM_SIZE, menu_snap_vertical_indicator)
	LOADPIXMAPINDICATOR(MENU_SNAP_H_INDICATOR_XBM_DATA, MENU_SNAP_INDICATOR_W_XBM_SIZE, menu_snap_horizontal_indicator)
	LOADPIXMAPINDICATOR(MENU_CENTRAL_INDICATOR_XBM_DATA, MENU_SNAP_INDICATOR_W_XBM_SIZE, menu_central_indicator)
	LOADPIXMAPINDICATOR(MENU_SNAP_RH_INDICATOR_XBM_DATA, MENU_SNAP_INDICATOR_W_XBM_SIZE, menu_snap_rh_indicator)
	LOADPIXMAPINDICATOR(MENU_SNAP_LH_INDICATOR_XBM_DATA, MENU_SNAP_INDICATOR_W_XBM_SIZE, menu_snap_lh_indicator)
	LOADPIXMAPINDICATOR(MENU_SNAP_TH_INDICATOR_XBM_DATA, MENU_SNAP_INDICATOR_W_XBM_SIZE, menu_snap_th_indicator)
	LOADPIXMAPINDICATOR(MENU_SNAP_BH_INDICATOR_XBM_DATA, MENU_SNAP_INDICATOR_W_XBM_SIZE, menu_snap_bh_indicator)
	LOADPIXMAPINDICATOR(MENU_SNAP_TL_INDICATOR_XBM_DATA, MENU_SNAP_INDICATOR_W_XBM_SIZE, menu_snap_tl_indicator)
	LOADPIXMAPINDICATOR(MENU_SNAP_TR_INDICATOR_XBM_DATA, MENU_SNAP_INDICATOR_W_XBM_SIZE, menu_snap_tr_indicator)
	LOADPIXMAPINDICATOR(MENU_SNAP_BL_INDICATOR_XBM_DATA, MENU_SNAP_INDICATOR_W_XBM_SIZE, menu_snap_bl_indicator)
	LOADPIXMAPINDICATOR(MENU_SNAP_BR_INDICATOR_XBM_DATA, MENU_SNAP_INDICATOR_W_XBM_SIZE, menu_snap_br_indicator)
	LOADPIXMAPINDICATOR(MENU_SNAP_TILED_INDICATOR_XBM_DATA, MENU_SNAP_INDICATOR_W_XBM_SIZE, menu_snap_tiled_indicator)

#undef LOADPIXMAPINDICATOR

	create_logo_image(scr);

	scr->dock_dots = make3Dots(scr);

	/* titlebar button pixmaps */
	allocButtonPixmaps(scr);
}

static void publishIconSizes(WScreen *scr)
{
	XIconSize icon_size[1];

	icon_size[0].min_width = 8;
	icon_size[0].min_height = 8;
	icon_size[0].max_width = w_global.config->icon_size - 4;
	icon_size[0].max_height = w_global.config->icon_size - 4;
	icon_size[0].width_inc = 1;
	icon_size[0].height_inc = 1;
	XSetIconSizes(dpy, scr->root_win, icon_size, 1);
}

/* Re-derive every per-screen object whose geometry was baked from
 * IconSize at wScreenInit() time. The icon tile and per-icon refresh
 * are handled separately by the IconBack force-reapply and
 * wIconResizeAll(), this covers what neither of those reach. */
void wScreenApplyIconSize(WScreen *scr)
{
	int isize;

	isize = w_global.config->icon_size;
	publishIconSizes(scr);

	/* dock_shadow is XMoveResizeWindow()'d to the current icon size at
	 * the start of every drag so it self-heals, but resize it now so
	 * the stale size never shows up before the first drag */
	XResizeWindow(dpy, scr->dock_shadow, isize, isize);

	/* the running-app dot indicator pixmap and its shape mask are
	 * sized icon_size x icon_size with the dots positioned by
	 * isize/64, so it must be rebuilt rather than scaled */
	if (scr->dock_dots)
		wPixmapDestroy(scr->dock_dots);
	scr->dock_dots = make3Dots(scr);
}

void create_logo_image(WScreen *scr)
{
	RImage *image = get_icon_image(scr, "Logo", "WMPanel", 128);

	if (!image) {
		wwarning(_("could not load logo image for panels: %s"), RMessageForError(RErrorCode));
	} else {
		WMSetApplicationIconImage(scr->wmscreen, image);
		RReleaseImage(image);
	}
}

/*
 *----------------------------------------------------------------------
 * createInternalWindows--
 * 	Creates some windows used internally by the program. One to
 * receive input focus when no other window can get it and another
 * to display window geometry information during window resize/move.
 *
 * Returns:
 * 	Nothing
 *
 * Side effects:
 * 	Windows are created and some colors are allocated for the
 * window background.
 *----------------------------------------------------------------------
 */
static void createInternalWindows(WScreen * scr)
{
	int vmask;
	XSetWindowAttributes attribs;

	/* InputOnly window to get the focus when no other window can get it */
	vmask = CWEventMask | CWOverrideRedirect;
	attribs.event_mask = KeyPressMask | FocusChangeMask;
	attribs.override_redirect = True;
	scr->no_focus_win = XCreateWindow(dpy, scr->root_win, -10, -10, 4, 4, 0, 0,
					  InputOnly, CopyFromParent, vmask, &attribs);
	XSelectInput(dpy, scr->no_focus_win, KeyPressMask | KeyReleaseMask);
	XMapWindow(dpy, scr->no_focus_win);

	XSetInputFocus(dpy, scr->no_focus_win, RevertToParent, CurrentTime);

	/* shadow window for dock buttons */
	vmask = CWBorderPixel | CWBackPixmap | CWBackPixel | CWCursor | CWSaveUnder | CWOverrideRedirect;
	attribs.border_pixel = scr->black_pixel;
	attribs.save_under = True;
	attribs.override_redirect = True;
	attribs.background_pixmap = None;
	attribs.background_pixel = scr->white_pixel;
	attribs.cursor = wPreferences.cursor[WCUR_NORMAL];
	vmask |= CWColormap;
	attribs.colormap = scr->w_colormap;
	scr->dock_shadow =
	    XCreateWindow(dpy, scr->root_win, 0, 0, w_global.config->icon_size,
			  w_global.config->icon_size, 0, scr->w_depth, CopyFromParent, scr->w_visual, vmask, &attribs);

	scr->mini_screenshot_timeout = 0;
}

/*
 *----------------------------------------------------------------------
 * wScreenInit--
 * 	Initializes the window manager for the given screen and
 * allocates a WScreen descriptor for it. Many resources are allocated
 * for the screen and the root window is setup appropriately.
 *
 * Returns:
 * 	The WScreen descriptor for the screen.
 *
 * Side effects:
 * 	Many resources are allocated and the IconSize property is
 * set on the root window.
 *	The program can be aborted if some fatal error occurs.
 *----------------------------------------------------------------------
 */
WScreen *wScreenInit(int screen_number, const struct WConfig *config,
		     const struct WConfigChanges *changes)
{
	WScreen *scr;
	RContextAttributes rattr = {};
	xcb_generic_error_t *xerr;
	uint32_t event_mask;
	int i, nret;
	XColor xcol;
	XVisualInfo vtempl, *vinfo;
	char *desc;

	scr = wmalloc(sizeof(WScreen));

	scr->stacking_list = WMCreateTreeBag();

	/* initialize globals */
	scr->screen = screen_number;
	scr->root_win = RootWindow(dpy, screen_number);
	scr->depth = DefaultDepth(dpy, screen_number);
	scr->colormap = DefaultColormap(dpy, screen_number);

	scr->scr_width = WidthOfScreen(ScreenOfDisplay(dpy, screen_number));
	scr->scr_height = HeightOfScreen(ScreenOfDisplay(dpy, screen_number));

	wInitXinerama(scr);

	scr->usableArea = (WArea *) wmalloc(sizeof(WArea) * wXineramaHeads(scr));
	scr->totalUsableArea = (WArea *) wmalloc(sizeof(WArea) * wXineramaHeads(scr));

	for (i = 0; i < wXineramaHeads(scr); ++i) {
		WMRect rect = wGetRectForHead(scr, i);
		scr->usableArea[i].x1 = scr->totalUsableArea[i].x1 = rect.pos.x;
		scr->usableArea[i].y1 = scr->totalUsableArea[i].y1 = rect.pos.y;
		scr->usableArea[i].x2 = scr->totalUsableArea[i].x2 = rect.pos.x + rect.size.width;
		scr->usableArea[i].y2 = scr->totalUsableArea[i].y2 = rect.pos.y + rect.size.height;
	}

#ifdef USE_RANDR
	wRandRInit(scr);
#endif

	scr->fakeGroupLeaders = WMCreateArray(16);

	/* Do we need to replace an existing window manager? */
	if (!replace_existing_wm(scr)) {
		XDestroyWindow(dpy, scr->info_window);
		wfree(scr);
		return NULL;
	}

	/* selecting SubstructureRedirectMask on the root window is the
	 * canonical "is another WM running" probe, only one client may
	 * hold it at a time so a BadAccess here means we lost the race,
	 * a checked xcb call gives that answer locally and synchronously
	 * without the global XSetErrorHandler trap bracket and the
	 * flanking XSync, the conditional PointerMotionMask is re-applied
	 * later from wScreenUpdateRootEventMask() whenever
	 * ScrollableMenus changes at runtime or a menu is mapped or
	 * unmapped, and XKB event selection is per-device so wXkbInit()
	 * has already done it */
	event_mask = EVENT_MASK;
	xerr = xcb_request_check(xconn,
		xcb_change_window_attributes_checked(xconn, scr->root_win,
						      XCB_CW_EVENT_MASK,
						      &event_mask));
	if (xerr) {
		free(xerr);
		wfree(scr);
		return NULL;
	}

#ifdef USE_RANDR
	if (w_global.xext.randr.supported)
		xcb_randr_select_input(xconn, scr->root_win,
				       XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE
				       | XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE
				       | XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE);
#endif

#ifdef USE_XCB_SCREENSAVER
	if (w_global.xext.screensaver.supported)
		xcb_screensaver_select_input(xconn, scr->root_win,
					     XCB_SCREENSAVER_EVENT_NOTIFY_MASK);
#endif

	XDefineCursor(dpy, scr->root_win, wPreferences.cursor[WCUR_ROOT]);

	/* screen descriptor for raster graphic library */
	rattr.flags = 0;
	if (getWVisualID(screen_number) >= 0) {
		rattr.flags |= RC_VisualID;
		rattr.visualid = getWVisualID(screen_number);
	}

	scr->rcontext = RCreateContextXCB(xconn, screen_number, &rattr);
	if (scr->rcontext == NULL) {
		wfatal(_("can't create Context on screen %d, %s"),
		       screen_number, RMessageForError(RErrorCode));
		goto abort_no_context;
	}

	scr->w_win = scr->rcontext->drawable;
	scr->w_depth = scr->rcontext->depth;
	scr->w_colormap = scr->rcontext->cmap;
	/* wraster picked the visual on the xcb side, so resolve the
	 * matching libX11 Visual* here for the Xlib drawing that still
	 * runs in src/ until the cairo-xcb rewrite lands */
	vtempl.visualid = scr->rcontext->xvisual->visual_id;
	vinfo = XGetVisualInfo(dpy, VisualIDMask, &vtempl, &nret);
	scr->w_visual = vinfo ? vinfo[0].visual : DefaultVisual(dpy, screen_number);
	if (vinfo)
		XFree(vinfo);

	/* create screen descriptor for WINGs */
	scr->wmscreen = WMCreateScreenWithRContext(dpy, screen_number, scr->rcontext);
	scr->ui_scale = WMScreenUIScale(scr->wmscreen);

	if (!scr->wmscreen) {
		wfatal(_("could not initialize WINGs widget set"));
		RDestroyContext(scr->rcontext);
	abort_no_context:
		WMFreeArray(scr->fakeGroupLeaders);
		wfree(scr->totalUsableArea);
		wfree(scr->usableArea);
		WMFreeBag(scr->stacking_list);
		wfree(scr);
		return NULL;
	}

	scr->black = WMBlackColor(scr->wmscreen);
	scr->white = WMWhiteColor(scr->wmscreen);
	scr->gray = WMGrayColor(scr->wmscreen);
	scr->darkGray = WMDarkGrayColor(scr->wmscreen);

	scr->black_pixel = WMColorPixel(scr->black);	/*scr->rcontext->black; */
	scr->white_pixel = WMColorPixel(scr->white);	/*scr->rcontext->white; */
	scr->light_pixel = WMColorPixel(scr->gray);
	scr->dark_pixel = WMColorPixel(scr->darkGray);

	/* create GCs with default values */
	allocDefaultColors(scr);

	/* read defaults for this screen, old_config is NULL because
	 * wScreenInit() only runs at first startup */
	wReadDefaults(scr, NULL, config, changes);
	scr->frame_inner_border_width = config->frame_inner_border_width;
	scr->frame_inner_border_sides =
		wFrameInnerBorderSidesFromArray(&config->frame_inner_border_sides);

	/* frame boder color */
	desc = WMGetColorRGBDescription(scr->frame_border_color);
	wGetColor(scr, desc, &xcol);
	scr->frame_border_pixel = xcol.pixel;
	wfree(desc);
	desc = WMGetColorRGBDescription(scr->frame_focused_border_color);
	wGetColor(scr, desc, &xcol);
	scr->frame_focused_border_pixel = xcol.pixel;
	wfree(desc);
	desc = WMGetColorRGBDescription(scr->frame_selected_border_color);
	wGetColor(scr, desc, &xcol);
	scr->frame_selected_border_pixel = xcol.pixel;
	wfree(desc);
	scr->frame_inner_border_pixel = WMColorPixel(scr->frame_inner_border_color);
	scr->frame_inner_focused_border_pixel = WMColorPixel(scr->frame_inner_focused_border_color);

	createInternalWindows(scr);

	wNETWMInitStuff(scr);
	wStartupNotifyInit(scr);

	/* create initial workspace */
	wWorkspaceNew(scr);

	/* create shared pixmaps */
	createPixmaps(scr);

	wTitlebarLayoutBuild(&scr->titlebar_layout,
			     &w_global.config->titlebar_left,
			     &w_global.config->titlebar_right);

	/* set icon sizes we can accept from clients */
	publishIconSizes(scr);

	/* setup WindowMaker protocols property in the root window */
	PropSetWMakerProtocols(scr->root_win);

	/* setup our noticeboard */
	WXSetWindow(dpy, scr->info_window, w_global.atom.wmaker.noticeboard, scr->info_window);
	WXSetWindow(dpy, scr->root_win, w_global.atom.wmaker.noticeboard, scr->info_window);

#ifdef BALLOON_TEXT
	/* initialize balloon text stuff */
	wBalloonInitialize(scr);
#endif

	scr->info_text_font = WMBoldSystemFontOfSize(scr->wmscreen, 12);
	/* record the AA flag the screen's fonts are opened under so a later
	 * wConfigApplyAntialiasedText() can detect a live WMGLOBAL change */
	scr->antialiased_text_applied = WMGetAntialiasedText();

	scr->gview = WCreateGeometryView(scr->wmscreen);
	WMRealizeWidget(scr->gview);

	scr->keychain_osd = WCreateOSDView(scr->wmscreen);
	WMRealizeWidget(scr->keychain_osd);

	scr->workspace_name_osd = WCreateOSDView(scr->wmscreen);
	WSetOSDViewTransparent(scr->workspace_name_osd, True);
	if (scr->workspace_name_font)
		WSetOSDViewFont(scr->workspace_name_osd, scr->workspace_name_font);
	WMRealizeWidget(scr->workspace_name_osd);

	wScreenUpdateUsableArea(scr);

	return scr;
}

/*
 * Re-select the root window event mask with PointerMotionMask added
 * only when a feature that consumes root MotionNotify is enabled.
 * Hot corners are driven by EnterNotify on dedicated InputOnly windows,
 * so only ScrollableMenus still needs root motion events, and only
 * while a menu is actually mapped, transient menus run a pointer grab
 * so motion reaches them regardless of the root mask, this term covers
 * torn-off menus that sit open between grabs.
 */
void wScreenUpdateRootEventMask(WScreen *scr)
{
	long mask = EVENT_MASK;

	if (w_global.config->scrollable_menus && scr->mapped_menu_count > 0)
		mask |= PointerMotionMask;

	XSelectInput(dpy, scr->root_win, mask);
}

void wScreenUpdateUsableArea(WScreen *scr)
{
	/*
	 * scr->totalUsableArea[] will become the usableArea used for Windowplacement,
	 * scr->usableArea[] will be used for iconplacement, hence no iconyard nor
	 * border.
	 */

	WArea area;
	int i;
	unsigned long tmp_area, best_area = 0;
	unsigned int size = w_global.config->workspace_border_size;
	unsigned int position = w_global.config->workspace_border_position;

	for (i = 0; i < wXineramaHeads(scr); ++i) {
		WMRect rect = wGetRectForHead(scr, i);
		scr->totalUsableArea[i].x1 = rect.pos.x;
		scr->totalUsableArea[i].y1 = rect.pos.y;
		scr->totalUsableArea[i].x2 = rect.pos.x + rect.size.width;
		scr->totalUsableArea[i].y2 = rect.pos.y + rect.size.height;

		if (wNETWMGetUsableArea(scr, i, &area)) {
			scr->totalUsableArea[i].x1 = WMAX(scr->totalUsableArea[i].x1, area.x1);
			scr->totalUsableArea[i].y1 = WMAX(scr->totalUsableArea[i].y1, area.y1);
			scr->totalUsableArea[i].x2 = WMIN(scr->totalUsableArea[i].x2, area.x2);
			scr->totalUsableArea[i].y2 = WMIN(scr->totalUsableArea[i].y2, area.y2);
		}

		scr->usableArea[i] = scr->totalUsableArea[i];

		if (w_global.config->no_window_over_icons) {
			if (w_global.config->icon_yard & IY_VERT) {
				if (w_global.config->icon_yard & IY_RIGHT)
					scr->totalUsableArea[i].x2 -= w_global.config->icon_size;
				else
					scr->totalUsableArea[i].x1 += w_global.config->icon_size;
			} else {
				if (w_global.config->icon_yard & IY_TOP)
					scr->totalUsableArea[i].y1 += w_global.config->icon_size;
				else
					scr->totalUsableArea[i].y2 -= w_global.config->icon_size;
			}
		}

		if (scr->totalUsableArea[i].x2 - scr->totalUsableArea[i].x1 < rect.size.width / 2) {
			scr->totalUsableArea[i].x1 = rect.pos.x;
			scr->totalUsableArea[i].x2 = rect.pos.x + rect.size.width;
		}

		if (scr->totalUsableArea[i].y2 - scr->totalUsableArea[i].y1 < rect.size.height / 2) {
			scr->totalUsableArea[i].y1 = rect.pos.y;
			scr->totalUsableArea[i].y2 = rect.pos.y + rect.size.height;
		}

		tmp_area = (scr->totalUsableArea[i].x2 - scr->totalUsableArea[i].x1) *
		    (scr->totalUsableArea[i].y2 - scr->totalUsableArea[i].y1);

		if (tmp_area > best_area) {
			best_area = tmp_area;
			area = scr->totalUsableArea[i];
		}

		if (size > 0 && position != WB_NONE) {
			if (position & WB_LEFTRIGHT) {
				scr->totalUsableArea[i].x1 += size;
				scr->totalUsableArea[i].x2 -= size;
			}
			if (position & WB_TOPBOTTOM) {
				scr->totalUsableArea[i].y1 += size;
				scr->totalUsableArea[i].y2 -= size;
			}
		}
	}

	if (w_global.config->auto_arrange_icons)
		wArrangeIcons(scr, True);

	/* publish the recomputed usable area as _NET_WORKAREA so pagers
	 * and toolkits see strut changes, not only workspace-count ones */
	wNETWMUpdateWorkarea(scr);

	wLayoutApply(scr, scr->current_workspace);
}

void wScreenRestoreState(WScreen * scr)
{
	WMPropList *state;
	char *path;

	OpenRootMenu(scr, -10000, -10000, False);
	wMenuUnmap(scr->root_menu);

	make_keys();

	if (w_global.screen_count == 1) {
		path = wdefaultspathfordomain("WMState");
	} else {
		char buf[16];
		snprintf(buf, sizeof(buf), "WMState.%i", scr->screen);
		path = wdefaultspathfordomain(buf);
	}
	scr->session_state = WMReadPropListFromFile(path);
	wfree(path);
	if (!scr->session_state && w_global.screen_count > 1) {
		path = wdefaultspathfordomain("WMState");
		scr->session_state = WMReadPropListFromFile(path);
		wfree(path);
	}

	if (!scr->session_state)
		scr->session_state = WMCreatePLDictionary(NULL, NULL);

	if (!wPreferences.flags.nodock) {
		state = WMGetFromPLDictionary(scr->session_state, dDock);
		scr->dock = wDockRestoreState(scr, state, WM_DOCK);
	}

	if (!wPreferences.flags.noclip) {
		state = WMGetFromPLDictionary(scr->session_state, dClip);
		scr->clip_icon = wClipRestoreState(scr, state);
	}

	if (!wPreferences.flags.nodrawer) {
		if (!scr->dock->on_right_side) {
			/* Drawer tile was created early in wScreenInit() -> wReadDefaults(). At
			 * that time, scr->dock was NULL and the tile was created as if we were on
			 * the right side. If we aren't, redo it now. */
			assert(scr->drawer_tile);
			RReleaseImage(scr->drawer_tile);
			scr->drawer_tile = wDrawerMakeTile(scr, scr->icon_tile);
		}
		wDrawersRestoreState(scr);
	}

	wWorkspaceRestoreState(scr);

	if (w_global.config->per_head_workspaces) {
		wWorkspaceLoadOutputPins(scr,
			w_global.domain.wmaker ? w_global.domain.wmaker->dictionary : NULL);
		wWorkspaceRestoreHeadState(scr);
	}

	wScreenUpdateUsableArea(scr);
}

void wScreenSaveState(WScreen *scr, Bool preserve_session)
{
	WWindow *wwin;
	char *str;
	WMPropList *old_state, *foo;

	make_keys();

	/* save state of windows */
	wwin = scr->focused_window;
	while (wwin) {
		wWindowSaveState(wwin);
		wwin = wwin->prev;
	}

	if (wPreferences.flags.noupdates)
		return;

	old_state = scr->session_state;
	if (!old_state) {
		/* wScreenRestoreState() has not run, or was interrupted before
		 * it loaded WMState and created scr->dock and scr->clip_icon.
		 * SIGTERM during the OpenRootMenu() error dialog at startup
		 * lands here, every save below would dereference NULL */
		return;
	}
	scr->session_state = WMCreatePLDictionary(NULL, NULL);

	WMPLSetCaseSensitive(True);

	/* save dock state to file */
	if (!wPreferences.flags.nodock) {
		wDockSaveState(scr, old_state);
	} else {
		foo = WMGetFromPLDictionary(old_state, dDock);
		if (foo != NULL)
			WMPutInPLDictionary(scr->session_state, dDock, foo);
	}
	if (!wPreferences.flags.noclip) {
		wClipSaveState(scr);
	} else {
		foo = WMGetFromPLDictionary(old_state, dClip);
		if (foo != NULL)
			WMPutInPLDictionary(scr->session_state, dClip, foo);
	}

	wWorkspaceSaveState(scr, old_state);
	wWorkspaceSaveHeadState(scr);

	if (!wPreferences.flags.nodrawer) {
		wDrawersSaveState(scr);
	} else {
		foo = WMGetFromPLDictionary(old_state, dDrawers);
		if (foo != NULL)
			WMPutInPLDictionary(scr->session_state, dDrawers, foo);
	}


	if (w_global.config->save_session_on_exit && !preserve_session) {
		wSessionSaveState(scr);
	} else {
		foo = WMGetFromPLDictionary(old_state, dApplications);
		if (foo != NULL)
			WMPutInPLDictionary(scr->session_state, dApplications, foo);

		foo = WMGetFromPLDictionary(old_state, dWorkspace);
		if (foo != NULL)
			WMPutInPLDictionary(scr->session_state, dWorkspace, foo);
	}

	/* clean up */
	WMPLSetCaseSensitive(False);

	wMenuSaveState(scr);

	if (w_global.screen_count == 1) {
		str = wdefaultspathfordomain("WMState");
	} else {
		char buf[16];
		snprintf(buf, sizeof(buf), "WMState.%i", scr->screen);
		str = wdefaultspathfordomain(buf);
	}
	if (!WMWritePropListToFile(scr->session_state, str)) {
		werror(_("could not save session state in %s"), str);
	}
	wfree(str);
	WMReleasePropList(old_state);
}

int wScreenBringInside(WScreen * scr, int *x, int *y, int width, int height)
{
	int moved = 0;
	int tol_w, tol_h;
	/*
	 * With respect to the head that contains most of the window.
	 */
	int sx1, sy1, sx2, sy2;

	WMRect rect;
	int head, flags;

	rect.pos.x = *x;
	rect.pos.y = *y;
	rect.size.width = width;
	rect.size.height = height;

	head = wGetRectPlacementInfo(scr, rect, &flags);
	rect = wGetRectForHead(scr, head);

	sx1 = rect.pos.x;
	sy1 = rect.pos.y;
	sx2 = sx1 + rect.size.width;
	sy2 = sy1 + rect.size.height;

#if 0				/* NOTE: gives funky group movement */
	if (flags & XFLAG_MULTIPLE) {
		/*
		 * since we span multiple heads, pull window totaly inside
		 */
		if (*x < sx1)
			*x = sx1, moved = 1;
		else if (*x + width > sx2)
			*x = sx2 - width, moved = 1;

		if (*y < sy1)
			*y = sy1, moved = 1;
		else if (*y + height > sy2)
			*y = sy2 - height, moved = 1;

		return moved;
	}
#endif

	if (width > 20)
		tol_w = width / 2;
	else
		tol_w = 20;

	if (height > 20)
		tol_h = height / 2;
	else
		tol_h = 20;

	if (*x + width < sx1 + 10)
		*x = sx1 - tol_w, moved = 1;
	else if (*x >= sx2 - 10)
		*x = sx2 - tol_w - 1, moved = 1;

	if (*y < sy1 - height + 10)
		*y = sy1 - tol_h, moved = 1;
	else if (*y >= sy2 - 10)
		*y = sy2 - tol_h - 1, moved = 1;

	return moved;
}

int wScreenKeepInside(WScreen * scr, int *x, int *y, int width, int height)
{
	int moved = 0;
	int sx1, sy1, sx2, sy2;
	WMRect rect;
	int head;

	rect.pos.x = *x;
	rect.pos.y = *y;
	rect.size.width = width;
	rect.size.height = height;

	head = wGetHeadForRect(scr, rect);
	rect = wGetRectForHead(scr, head);

	sx1 = rect.pos.x;
	sy1 = rect.pos.y;
	sx2 = sx1 + rect.size.width;
	sy2 = sy1 + rect.size.height;

	if (*x < sx1)
		*x = sx1, moved = 1;
	else if (*x + width > sx2)
		*x = sx2 - width, moved = 1;

	if (*y < sy1)
		*y = sy1, moved = 1;
	else if (*y + height > sy2)
		*y = sy2 - height, moved = 1;

	return moved;
}

static RImage *imageCaptureArea(WScreen *scr)
{
	XEvent event;
	RXImage *ximg;
	RImage *img;
	int quit = 0;
	int xp = -1;
	int yp = -1;
	int w = 0, h = 0;
	int x = xp, y = yp;

	if (XGrabPointer(dpy, scr->root_win, False, ButtonMotionMask
			| ButtonReleaseMask | ButtonPressMask, GrabModeAsync,
			GrabModeAsync, None, wPreferences.cursor[WCUR_CAPTURE], CurrentTime) != Success) {
		return NULL;
	}
	XGrabKeyboard(dpy, scr->root_win, False, GrabModeAsync, GrabModeAsync, CurrentTime);

	while (!quit) {
		WMMaskEvent(dpy, ButtonReleaseMask | PointerMotionMask | ButtonPressMask
			    | KeyPressMask | ExposureMask, &event);

		switch (event.type) {
		case ButtonPress:
			if (event.xbutton.button == Button1) {
				xp = event.xbutton.x_root;
				yp = event.xbutton.y_root;
			}
			break;
		case ButtonRelease:
			if (event.xbutton.button == Button1) {
				quit = 1;
				if (w > 0 && h > 0) {
					/* save_under is only a hint, when the server
					 * ignores it the unmap clears the band areas
					 * to background and queues Expose, drain and
					 * dispatch those before GetImage so the
					 * capture border is repainted content not a
					 * cleared 3 px strip */
					wWireframeHide(scr);
					XSync(dpy, False);
					while (WXEvqTakeMask(dpy, ExposureMask, None, &event))
						WMHandleEvent(&event);
					XUngrabKeyboard(dpy, CurrentTime);
					XUngrabPointer(dpy, CurrentTime);
					ximg = RGetXImage(scr->rcontext, scr->root_win, x, y, w, h);
					if (!ximg)
						return NULL;
					img = RCreateImageFromRXImage(scr->rcontext, ximg, NULL);
					RDestroyXImage(scr->rcontext, ximg);
					return img;
				}
			}
			break;
		case MotionNotify:
			/* drop stale root MotionNotify still queued from before
			 * the grab, otherwise the rectangle anchors at (-1,-1) */
			if (xp < 0)
				break;
			x = event.xmotion.x_root;
			if (x < xp) {
				w = xp - x;
			} else {
				w = x - xp;
				x = xp;
			}
			y = event.xmotion.y_root;
			if (y < yp) {
				h = yp - y;
			} else {
				h = y - yp;
				y = yp;
			}
			wWireframeBegin(scr);
			wWireframeAddOutline(scr, x, y, w, h, 0, 0, 0);
			wWireframeCommit(scr);
			break;
		case KeyPress:
			if (wXkbKeycodeToKeysym(event.xkey.keycode, 0) == XK_Escape)
				quit = 1;
			break;
		default:
			WMHandleEvent(&event);
			break;
		}
	}

	wWireframeHide(scr);
	XUngrabKeyboard(dpy, CurrentTime);
	XUngrabPointer(dpy, CurrentTime);
	return NULL;
}

static void hideMiniScreenshot(void *data)
{
	WScreen *scr = (WScreen *) data;

	if (time(NULL) < scr->mini_screenshot_timeout) {
		scr->mini_screenshot_timer = WMAddTimerHandler(WORKSPACE_NAME_FADE_DELAY, hideMiniScreenshot, scr);
	} else {
		XWindowAttributes attr;

		WMDeleteTimerHandler(scr->mini_screenshot_timer);
		if (XGetWindowAttributes(dpy, scr->mini_screenshot, &attr))
			slide_window(scr->mini_screenshot, attr.x, attr.y, attr.x + attr.width, attr.y);
		XUnmapWindow(dpy, scr->mini_screenshot);
		XDestroyWindow(dpy, scr->mini_screenshot);
		scr->mini_screenshot_timeout = 0;
	}
}

static void showMiniScreenshot(WScreen *scr, RImage *img)
{
	Pixmap pix;
	int x = scr->scr_width - img->width - 20;
	int y = scr->scr_height - img->height - 20;

	if (!scr->mini_screenshot_timeout) {
		Window win;

		win = XCreateSimpleWindow(dpy, scr->root_win, x, y,
			img->width, img->height, scr->frame_border_width, 0, 0);
		scr->mini_screenshot = win;
	}
	RConvertImage(scr->rcontext, img, &pix);
	XSetWindowBackgroundPixmap(dpy, scr->mini_screenshot, pix);
	XFreePixmap(dpy, pix);
	XClearWindow(dpy, scr->mini_screenshot);
	XMapRaised(dpy, scr->mini_screenshot);
	XFlush(dpy);

	scr->mini_screenshot_timeout = time(NULL) + 2;
	scr->mini_screenshot_timer = WMAddTimerHandler(WORKSPACE_NAME_FADE_DELAY, hideMiniScreenshot, scr);
}

void ScreenCapture(WScreen *scr, int mode)
{
	time_t s;
	short i = 0;
	struct tm *tm_info;
	char filename_date_part[60];
	char filename[80];
	char *filepath;
	char *screenshot_dir;
	RImage *img = NULL;
	RImage *scale_img = NULL;

#ifdef USE_PNG
	char *filetype = ".png";
#else
#ifdef USE_JPEG
	char *filetype = ".jpg";
#else
	char *filetype = NULL;
#endif
#endif

	if (!filetype) {
		werror(_("Unable to find a proper screenshot image format"));
		return;
	}

	screenshot_dir = wstrconcat(wuserdatapath(), "/Screenshots/");
	if (!wmkdirhier(screenshot_dir)) {
		wfree(screenshot_dir);
		werror(_("Unable to create screenshot directory"));
		return;
	}

	s = time(NULL);
	tm_info = localtime(&s);
	strftime(filename_date_part, sizeof(filename_date_part), w_global.config->screenshot_filename_template, tm_info);

	if (strchr(filename_date_part, '/') != NULL) {
		wfree(screenshot_dir);
		werror(_("Unsafe screenshot filename template, it should not contain a path separator"));
		return;
	}

	snprintf(filename, sizeof(filename), "%s%s", filename_date_part, filetype);

	filepath = wstrconcat(screenshot_dir, filename);
	while (access(filepath, F_OK) == 0 && i < 600) {
		i++;
		snprintf(filename, sizeof(filename), "%s_%d%s", filename_date_part, i, filetype);
		wfree(filepath);
		filepath = wstrconcat(screenshot_dir, filename);
	}

	/* cannot generate an available filename ?! */
	if (i == 600) {
		wfree(filepath);
		wfree(screenshot_dir);
		werror(_("Could not generate a free screenshot filename"));
		return;
	}

	switch (mode) {
		WWindow *wwin;
		RXImage *pimg;

		case PRINT_WINDOW:
			wwin = scr->focused_window;
			if (wwin && !wwin->flags.shaded) {
				/*
				 * check if hint WM_TAKE_FOCUS is set, if it's the case
				 * we can take screenshot of the out of screen window
				 */
				if (wwin->focus_mode >= WFM_LOCALLY_ACTIVE) {
					img = RCreateImageFromDrawable(scr->rcontext, wwin->client_win, None);
				}
				else {
					/* we will only capture the visible window part */
					int x_crop = 0;
					int y_crop = 0;
					int w_crop = wwin->client.width;
					int h_crop = wwin->client.height;

					if (wwin->client.x > 0)
						x_crop = wwin->client.x;
					if (wwin->client.y > 0)
						y_crop = wwin->client.y;

					if (wwin->client.x + wwin->client.width > scr->scr_width)
						w_crop = scr->scr_width - wwin->client.x;
					if (wwin->client.y + wwin->client.height > scr->scr_height)
						h_crop = scr->scr_height - wwin->client.y;

					pimg = RGetXImage(scr->rcontext, scr->root_win, x_crop, y_crop,
							(wwin->client.x > 0)?w_crop:w_crop + wwin->client.x,
							(wwin->client.y > 0)?h_crop:h_crop + wwin->client.y);

					if (pimg) {
						img = RCreateImageFromRXImage(scr->rcontext, pimg, NULL);
						RDestroyXImage(scr->rcontext, pimg);
					}
				}
			}
			break;
		case PRINT_PARTIAL:
			img = imageCaptureArea(scr);
			break;
		default:
			/* PRINT_SCREEN*/
			img = RCreateImageFromDrawable(scr->rcontext, scr->root_win, None);
	}

	if (img) {
		if (RSaveTitledImage(img, filepath, (char *)(filetype + 1), "Screenshot from Window Maker")) {
			wIPCEmitScreenshot(scr, filepath, mode);
			wSoundPlay(scr, WSOUND_SCREENSHOT);
			scale_img = RSmoothScaleImage(img, scr->scr_width / 10, scr->scr_height / 10);
			showMiniScreenshot(scr, scale_img);
			RReleaseImage(scale_img);
#ifdef DEBUG
			wmessage("screenshot filepath: %s", filepath);
#endif
		}
		RReleaseImage(img);
	}
	wfree(filepath);
	wfree(screenshot_dir);
}

void wScreenDestroy(WScreen *scr)
{
	wStartupNotifyShutdown(scr);
#ifdef USE_RANDR
	wRandRTeardown(scr);
#else
	/* Parameter not used, but tell the compiler that it is ok */
	(void) scr;
#endif
}
