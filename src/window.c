/* window.c - client window managing stuffs
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2008-2026 Window Maker Team
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

#ifdef USE_XSHAPE
#include <xcb/shape.h>
#endif
#ifdef USE_XRES
#include <xcb/res.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <ctype.h>
#include <unistd.h>

/* For getting mouse wheel mappings from WINGs */
#include <WINGs/WINGs.h>
#include <WINGs/WXProp.h>
#include <WINGs/WXHints.h>

#include "WindowMaker.h"
#include "geometry.h"
#include "GNUstep.h"
#include "wcore.h"
#include "framewin.h"
#include "texture.h"
#include "window.h"
#include "winspector.h"
#include "icon.h"
#include "properties.h"
#include "actions.h"
#include "layout.h"
#include "client.h"
#include "colormap.h"
#include "keybind.h"
#include "stacking.h"
#include "defaults.h"
#include "wdefaults.h"
#include "workspace.h"
#include "xinerama.h"
#include "appmenu.h"
#include "appicon.h"
#include "superfluous.h"
#include "rootmenu.h"
#include "placement.h"
#include "misc.h"
#include "startup.h"
#include "wxkb.h"
#include "tabgroup.h"
#include "winmenu.h"
#include "osdep.h"
#include "switchmenu.h"

#ifdef USE_MWM_HINTS
# include "motif.h"
#endif
#include "wmspec.h"

#define MOD_MASK wdisp->modifier_mask


/***** Local Stuff *****/
static WWindowState *windowState = NULL;
static FocusMode getFocusMode(WWindow *wwin);
static int getSavedStateCk(xcb_window_t window, WXCookie state_ck, WXCookie mark_ck,
			   WXCookie tab_ck, WSavedState **state);
static void setupGNUstepHints(WWindow *wwin, GNUstepWMAttributes *gs_hints);

/* frame window (during window grabs) */
static void frameMouseDown(WObjDescriptor *desc, xcb_generic_event_t *event);

/* close button */
static void windowCloseClick(WCoreWindow *sender, void *data, xcb_generic_event_t *event);
static void windowCloseDblClick(WCoreWindow *sender, void *data, xcb_generic_event_t *event);

/* iconify button */
static void windowIconifyClick(WCoreWindow *sender, void *data, xcb_generic_event_t *event);

/* maximize button */
static void windowMaximizeClick(WCoreWindow *sender, void *data, xcb_generic_event_t *event);

#ifdef XKB_BUTTON_HINT
static void windowLanguageClick(WCoreWindow *sender, void *data, xcb_generic_event_t *event);
#endif

static void titlebarMouseDown(WCoreWindow *sender, void *data, xcb_generic_event_t *event);
static void titlebarDblClick(WCoreWindow *sender, void *data, xcb_generic_event_t *event);
static void resizebarMouseDown(WCoreWindow *sender, void *data, xcb_generic_event_t *event);

static void release_wwindowstate(WWindowState *wstate);
static void translateWindowToHead(WWindow *wwin, int from_head, int to_head);

/****** Notification Observers ******/

static void appearanceObserver(void *self, WMNotification * notif)
{
	WWindow *wwin = (WWindow *) self;
	uintptr_t flags = (uintptr_t)WMGetNotificationClientData(notif);

	if (!wwin->frame || (!wwin->frame->titlebar && !wwin->frame->resizebar))
		return;

	if (flags & WFontSettings) {
		wWindowConfigureBorders(wwin);
		if (wwin->flags.shaded) {
			wFrameWindowResize(wwin->frame, wwin->frame->core->width, wwin->frame->top_width - 1);
			wwin->client.y = wwin->frame_y - wwin->client.height + wwin->frame->top_width;
			wWindowSynthConfigureNotify(wwin);
		}
	}
	if (flags & WTextureSettings)
		wwin->frame->flags.need_texture_remake = 1;

	if (flags & (WTextureSettings | WColorSettings)) {
		/* TabsInTitlebar raises this mask too, regenerate group
		 * member frame titles so the [N/M] prefix is added or
		 * removed before the repaint, only the active member
		 * triggers the walk so a group of N does not refresh N*N */
		if (wwin->tab_group && !wGroupIsInactiveMember(wwin))
			wGroupRefreshTitles(wwin->tab_group);

		if (wwin->frame->titlebar)
			xcb_clear_area(wdisp->conn, 0, wwin->frame->titlebar->window, 0, 0, 0, 0);

		wFrameWindowPaint(wwin->frame);
	}
}


/* Return the WWindow associated with a given (Xlib) Window. */
WWindow *wWindowFor(xcb_window_t window)
{
	WObjDescriptor *desc;

	if (window == XCB_NONE)
		return NULL;

	desc = WMHashGet(wdisp->context.client_win, (void *)(uintptr_t) window);
	if (desc == NULL)
		return NULL;

	if (desc->parent_type == WCLASS_WINDOW)
		return desc->parent;
	else if (desc->parent_type == WCLASS_FRAME) {
		WFrameWindow *frame = (WFrameWindow *) desc->parent;
		if (frame->flags.is_client_window_frame)
			return frame->child;
	}

	return NULL;
}

WWindow *wWindowCreate(void)
{
	WWindow *wwin;

	wwin = wmalloc(sizeof(WWindow));
	wretain(wwin);

	wwin->client_descriptor.handle_mousedown = frameMouseDown;
	wwin->client_descriptor.parent = wwin;
	wwin->client_descriptor.self = wwin;
	wwin->client_descriptor.parent_type = WCLASS_WINDOW;
	wwin->sn_workspace = -1;

	return wwin;
}

void wWindowDestroy(WWindow *wwin)
{
	int i;

	if (wwin->screen_ptr->cmap_window == wwin)
		wwin->screen_ptr->cmap_window = NULL;

	WMRemoveNotificationObserver(wwin);
	WMDeleteTimerHandlerPtr(&wwin->urgent_blink_timer);
	wNETWMCancelPing(wwin);
	wNETWMDetachUserTimeWindow(wwin);
	wNETWMSyncRequestDestroy(wwin);

	wwin->flags.destroyed = 1;

	for (i = 0; i < MAX_WINDOW_SHORTCUTS; i++) {
		if (!wwin->screen_ptr->shortcutWindows[i])
			continue;

		WMRemoveFromArray(wwin->screen_ptr->shortcutWindows[i], wwin);

		if (!WMGetArrayItemCount(wwin->screen_ptr->shortcutWindows[i])) {
			WMFreeArray(wwin->screen_ptr->shortcutWindows[i]);
			wwin->screen_ptr->shortcutWindows[i] = NULL;
		}
	}

	/* clean up any mark assigned to this window */
	if (wwin->mark_key_label != NULL)
		wWindowUnsetMark(wwin);

	if (wwin->fake_group && wwin->fake_group->retainCount > 0) {
		wwin->fake_group->retainCount--;
		if (wwin->fake_group->retainCount == 0 && wwin->fake_group->leader != XCB_NONE) {
			xcb_destroy_window(wdisp->conn, wwin->fake_group->leader);
			wwin->fake_group->leader = XCB_NONE;
			wwin->fake_group->origLeader = XCB_NONE;
			wXFlush(wdisp->conn);
		}
	}

	if (wwin->normal_hints)
		wfree(wwin->normal_hints);

	if (wwin->wm_hints)
		wfree(wwin->wm_hints);

	if (wwin->wm_instance)
		wunintern(wwin->wm_instance);

	if (wwin->wm_class)
		wunintern(wwin->wm_class);

	if (wwin->wm_command)
		wfree(wwin->wm_command);

	if (wwin->wm_role)
		wfree(wwin->wm_role);

	if (wwin->wm_machine)
		wfree(wwin->wm_machine);

	if (wwin->wm_title)
		wfree(wwin->wm_title);

	if (wwin->wm_types)
		wfree(wwin->wm_types);

	if (wwin->wm_type_names) {
		int k;

		for (k = 0; k < wwin->wm_type_count; k++)
			if (wwin->wm_type_names[k])
				free(wwin->wm_type_names[k]);
		wfree(wwin->wm_type_names);
	}

	if (wwin->frame_border.frame_border_color)
		WMReleaseColor(wwin->frame_border.frame_border_color);
	if (wwin->frame_border.frame_focused_border_color)
		WMReleaseColor(wwin->frame_border.frame_focused_border_color);
	if (wwin->frame_border.frame_selected_border_color)
		WMReleaseColor(wwin->frame_border.frame_selected_border_color);
	if (wwin->frame_border.frame_inner_border_color)
		WMReleaseColor(wwin->frame_border.frame_inner_border_color);
	if (wwin->frame_border.frame_inner_focused_border_color)
		WMReleaseColor(wwin->frame_border.frame_inner_focused_border_color);

	if (wwin->wm_gnustep_attr)
		wfree(wwin->wm_gnustep_attr);

	if (wwin->cmap_windows)
		wfree(wwin->cmap_windows);

	WMHashRemove(wdisp->context.client_win, (void *)(uintptr_t) wwin->client_win);

	if (wwin->frame)
		wFrameWindowDestroy(wwin->frame);

	if (wwin->icon) {
		RemoveFromStackList(wwin->icon->core);
		wIconDestroy(wwin->icon);
		if (w_global.config->auto_arrange_icons)
			wArrangeIcons(wwin->screen_ptr, true);
	}
	if (wwin->net_icon_image)
		RReleaseImage(wwin->net_icon_image);
	if (wwin->wm_hints_icon_image)
		RReleaseImage(wwin->wm_hints_icon_image);

	wrelease(wwin);
}

static void setupGNUstepHints(WWindow *wwin, GNUstepWMAttributes *gs_hints)
{
	if (gs_hints->flags & GSWindowStyleAttr) {
		if (gs_hints->window_style == WMBorderlessWindowMask) {
			wwin->client_flags.no_border = 1;
			wwin->client_flags.no_titlebar = 1;
			wwin->client_flags.no_closable = 1;
			wwin->client_flags.no_miniaturizable = 1;
			wwin->client_flags.no_resizable = 1;
			wwin->client_flags.no_close_button = 1;
			wwin->client_flags.no_miniaturize_button = 1;
			wwin->client_flags.no_resizebar = 1;
		} else {
			wwin->client_flags.no_close_button =
			    ((gs_hints->window_style & WMClosableWindowMask) ? 0 : 1);

			wwin->client_flags.no_closable = ((gs_hints->window_style & WMClosableWindowMask) ? 0 : 1);

			wwin->client_flags.no_miniaturize_button =
			    ((gs_hints->window_style & WMMiniaturizableWindowMask) ? 0 : 1);

			wwin->client_flags.no_miniaturizable = wwin->client_flags.no_miniaturize_button;

			wwin->client_flags.no_resizebar =
			    ((gs_hints->window_style & WMResizableWindowMask) ? 0 : 1);

			wwin->client_flags.no_resizable = wwin->client_flags.no_resizebar;

			/* these attributes supposedly imply in the existence
			 * of a titlebar */
			if (gs_hints->window_style & (WMResizableWindowMask |
						      WMClosableWindowMask | WMMiniaturizableWindowMask)) {
				wwin->client_flags.no_titlebar = 0;
			} else {
				wwin->client_flags.no_titlebar =
				    ((gs_hints->window_style & WMTitledWindowMask) ? 0 : 1);
			}

		}
	} else {
		/* setup the defaults */
		wwin->client_flags.no_border = 0;
		wwin->client_flags.no_titlebar = 0;
		wwin->client_flags.no_closable = 0;
		wwin->client_flags.no_miniaturizable = 0;
		wwin->client_flags.no_resizable = 0;
		wwin->client_flags.no_close_button = 0;
		wwin->client_flags.no_miniaturize_button = 0;
		wwin->client_flags.no_resizebar = 0;
	}
	if (gs_hints->extra_flags & GSNoApplicationIconFlag)
		wwin->client_flags.no_appicon = 1;
}

void wManagePrefetch(xcb_window_t window, WManageCookies *ck)
{
	ck->wm_state  = WXReqFormat32(wdisp->conn, window, wdisp->atom.wm.state,
				      wdisp->atom.wm.state, 1);
	ck->net_name  = wNETWMReqWindowName(window);
	ck->name      = WXReqWMName(wdisp->conn, window);
	ck->wclass    = WXReqWMClass(wdisp->conn, window);
	ck->gnustep   = WXReqFormat32(wdisp->conn, window, wdisp->atom.gnustep.wm_attr,
				      wdisp->atom.gnustep.wm_attr, 9);
	ck->leader    = WXReqFormat32(wdisp->conn, window, wdisp->atom.wm.client_leader,
				      XCB_ATOM_WINDOW, 1);
	ck->hints     = WXReqWMHints(wdisp->conn, window);
	ck->protocols = WXReqWMProtocols(wdisp->conn, window);
	ck->transient = WXReqTransientFor(wdisp->conn, window);
	ck->normal    = WXReqNormalHints(wdisp->conn, window);
#ifdef USE_MWM_HINTS
	ck->motif     = wMWMReqClientHints(window);
#else
	ck->motif.seq = 0;
#endif
	ck->cmap_windows = wClientReqColormapWindows(window);
	ck->command   = WXReqCommand(wdisp->conn, window);
	ck->role      = WXReqString8(wdisp->conn, window, wdisp->atom.wm.window_role, XCB_ATOM_STRING);
	ck->pid       = wNETWMReqPid(window);
	ck->appmenu   = WXReqExists(wdisp->conn, window, wdisp->atom.wmaker.menu);
	ck->gtk       = WXReqExists(wdisp->conn, window, wdisp->atom.desktop.gtk_object_path);
	ck->saved_state = WXReqFormat32(wdisp->conn, window, wdisp->atom.wmaker.state,
					wdisp->atom.wmaker.state, WSAVED_STATE_SLOTS);
	ck->mark_key  = WXReqString8(wdisp->conn, window, wdisp->atom.wmaker.mark_key, XCB_ATOM_STRING);
	ck->tab_group = WXReqFormat32(wdisp->conn, window, wdisp->atom.wmaker.tab_group,
				      wdisp->atom.wmaker.tab_group, 3);
	wNETWMReqClientHints(window, &ck->netwm);
	wNETWMReqInitialStateCookies(window, &ck->netwm_initial);
}

void wManageDiscard(WManageCookies *ck)
{
	WXCookie *p = (WXCookie *)ck;
	size_t i, n = sizeof(WManageCookies) / sizeof(WXCookie);

	/* the loop below walks ck as a flat WXCookie array, so the struct
	 * must contain nothing but WXCookie fields with no padding */
	_wutil_static_assert(sizeof(WManageCookies) % sizeof(WXCookie) == 0,
			     "WManageCookies must contain only WXCookie fields");

	for (i = 0; i < n; i++) {
		if (p[i].seq)
			WXDiscard(wdisp->conn, p[i]);
	}
}

static void discard_hints_from_gtk(WWindow *wwin, WXCookie ck)
{
	if (!WXRcvExists(wdisp->conn, ck))
		return;

	/* If we're here, that means the Property exists. We don't care what is inside, it means it is a GTK-based application */

	/* GTK is asking to remove these decorations: */
	wwin->client_flags.no_titlebar = 0;
	wwin->client_flags.no_close_button = 0;
	wwin->client_flags.no_miniaturize_button = 0;
	wwin->client_flags.no_resizebar = 0;
}

/* geometry spec parsing lives in geometry.c */

void wWindowSetupInitialAttributes(WWindow *wwin, WManageCookies *ck, int *level, int *workspace)
{
	WScreen *scr = wwin->screen_ptr;
	WManageCookies local_ck;
	bool own_ck = false;
	const char *geom_spec;

	if (!ck) {
		/* No prefetch supplied (e.g. inspector reload): issue only the
		 * cookies this function consumes and own their lifetime. */
		memset(&local_ck, 0, sizeof(local_ck));
#ifdef USE_MWM_HINTS
		local_ck.motif = wMWMReqClientHints(wwin->client_win);
#endif
		local_ck.gtk = WXReqExists(wdisp->conn, wwin->client_win,
					   wdisp->atom.desktop.gtk_object_path);
		wNETWMReqClientHints(wwin->client_win, &local_ck.netwm);
		ck = &local_ck;
		own_ck = true;
	}

	/* sets global default stuff */
	wDefaultFillAttributes(wwin->wm_instance, wwin->wm_class, &wwin->user_flags, NULL, true);
	wwin->defined_user_flags = wwin->user_flags;

	/*
	 * Decoration setting is done in this precedence (lower to higher)
	 * - use global default in the resource database
	 * - guess some settings
	 * - use GNUstep/external window attributes
	 * - set hints specified for the app in the resource DB
	 *
	 */
	wwin->client_flags.broken_close = 0;

	if (wwin->protocols.DELETE_WINDOW)
		wwin->client_flags.kill_close = 0;
	else
		wwin->client_flags.kill_close = 1;

	/* transients can't be iconified or maximized */
	if (wwin->transient_for != XCB_NONE && wwin->transient_for != scr->root_win) {
		wwin->client_flags.no_miniaturizable = 1;
		wwin->client_flags.no_miniaturize_button = 1;
	}

	/* if the window can't be resized, remove the resizebar */
	if (wwin->normal_hints->flags & (XCB_ICCCM_SIZE_HINT_P_MIN_SIZE | XCB_ICCCM_SIZE_HINT_P_MAX_SIZE)
	    && (wwin->normal_hints->min_width == wwin->normal_hints->max_width)
	    && (wwin->normal_hints->min_height == wwin->normal_hints->max_height)) {
		wwin->client_flags.no_resizable = 1;
		wwin->client_flags.no_resizebar = 1;
	}

	/* set GNUstep window attributes */
	if (wwin->wm_gnustep_attr) {
		setupGNUstepHints(wwin, wwin->wm_gnustep_attr);

		if (wwin->wm_gnustep_attr->flags & GSWindowLevelAttr) {

			*level = wwin->wm_gnustep_attr->window_level;
			/*
			 * INT_MIN is the only illegal window level.
			 */
			if (*level == INT_MIN)
				*level = INT_MIN + 1;
		} else {
			/* setup defaults */
			*level = WMNormalLevel;
		}
	} else {
		int tmp_workspace = -1;
		int tmp_level = INT_MIN;	/* INT_MIN is never used by the window levels */

#ifdef USE_MWM_HINTS
		wMWMRcvClientHints(wwin, ck->motif);
#endif	/* USE_MWM_HINTS */

		wNETWMRcvClientHints(wwin, &ck->netwm, &tmp_level, &tmp_workspace);

		if (w_global.config->ignore_gtk_decoration_hints)
			discard_hints_from_gtk(wwin, ck->gtk);

		/* window levels are between INT_MIN+1 and INT_MAX, so if we still
		 * have INT_MIN that means that no window level was requested. -Dan
		 */
		if (tmp_level == INT_MIN) {
			if (WFLAGP(wwin, floating))
				*level = WMFloatingLevel;
			else if (WFLAGP(wwin, sunken))
				*level = WMSunkenLevel;
			else
				*level = WMNormalLevel;
		} else {
			*level = tmp_level;
		}

		if (wwin->transient_for != XCB_NONE && wwin->transient_for != scr->root_win) {
			WWindow *transientOwner = wWindowFor(wwin->transient_for);
			if (transientOwner) {
				int ownerLevel = transientOwner->frame->core->stacking->window_level;
				if (ownerLevel > *level)
					*level = ownerLevel;
			}
		}

		if (tmp_workspace >= 0)
			*workspace = tmp_workspace % scr->workspace_count;
	}

	/*
	 * Set attributes specified only for that window/class.
	 * This might do duplicate work with the 1st wDefaultFillAttributes().
	 */
	wDefaultFillAttributes(wwin->wm_instance, wwin->wm_class, &wwin->user_flags,
			       &wwin->defined_user_flags, false);

	/* Restore decoration if the user has enabled the
	 * IgnoreDecorationChanges option */
	if (wwin->user_flags.ignore_decoration_changes) {
		WSETUFLAG(wwin, no_titlebar, 0);
		WSETUFLAG(wwin, no_resizable, 0);
		WSETUFLAG(wwin, no_miniaturizable, 0);
		WSETUFLAG(wwin, no_resizebar, 0);
		WSETUFLAG(wwin, no_close_button, 0);
		WSETUFLAG(wwin, no_miniaturize_button, 0);
		WSETUFLAG(wwin, no_border, 0);
		WSETUFLAG(wwin, no_movable, 0);
	}

	/*
	 * Sanity checks for attributes that depend on other attributes
	 */
	if (wwin->user_flags.no_appicon && wwin->defined_user_flags.no_appicon)
		wwin->user_flags.emulate_appicon = 0;

	if (wwin->main_window != XCB_NONE) {
		WApplication *wapp = wApplicationOf(wwin->main_window);
		if (wapp && !wapp->flags.emulated)
			wwin->user_flags.emulate_appicon = 0;
	}

	if (wwin->transient_for != XCB_NONE && wwin->transient_for != wwin->screen_ptr->root_win)
		wwin->user_flags.emulate_appicon = 0;

	if (wwin->user_flags.sunken && wwin->defined_user_flags.sunken
	    && wwin->user_flags.floating && wwin->defined_user_flags.floating)
		wwin->user_flags.sunken = 0;

	/* A window that does not have a title cannot be Shaded and we don't let user override this */
	wwin->client_flags.no_shadeable = WFLAGP(wwin, no_titlebar);
	wwin->defined_user_flags.no_shadeable = 0;

	/* windows that have takefocus=False shouldn't take focus at all */
	if (wwin->focus_mode == WFM_NO_INPUT)
		wwin->client_flags.no_focusable = 1;

	wDefaultFillFrameBorders(scr, wwin->wm_instance, wwin->wm_class, wwin);

	geom_spec = wDefaultGetStartGeometry(wwin->wm_instance, wwin->wm_class);
	if (geom_spec) {
		int gx = 0, gy = 0;
		unsigned int gw = 0, gh = 0;
		int gmask;

		gmask = wParseGeometry(geom_spec, &gx, &gy, &gw, &gh);
		if (gmask & (WidthValue | HeightValue | XValue | YValue)) {
			wwin->start_geometry.width = gw;
			wwin->start_geometry.height = gh;
			wwin->start_geometry.x = gx;
			wwin->start_geometry.y = gy;
			wwin->start_geometry_set = 1;
		}
	}

	/* Everything the fill defined is domain-supplied, those values
	 * re-derive at every manage so they never ride the hint. */
	wwin->domain_flags = wwin->defined_user_flags;

	if (own_ck)
		wManageDiscard(ck);
}

/*
 * Returns True if every pixel of 'wwin' is covered by at least one other
 * mapped window on the same workspace, making it invisible to the user
 */
bool wWindowIsFullyCovered(WWindow *wwin)
{
	WScreen *scr = wwin->screen_ptr;
	int cx = wwin->frame_x;
	int cy = wwin->frame_y;
	int cright  = cx + (int)wwin->frame->core->width;
	int cbottom = cy + (int)wwin->frame->core->height;
	WWindow *w;

	for (w = scr->focused_window; w != NULL; w = w->prev) {
		if (w == wwin)
			continue;
		if (!w->flags.mapped)
			continue;
		if (!w->frame || !wWindowIsVisibleOnAnyHead(w))
			continue;
		if (w->frame_x <= cx &&
		    w->frame_y <= cy &&
		    w->frame_x + (int)w->frame->core->width  >= cright &&
		    w->frame_y + (int)w->frame->core->height >= cbottom)
			return true;
	}
	return false;
}

bool wWindowObscuresWindow(WWindow *wwin, WWindow *obscured)
{
	int w1, h1, w2, h2;

	w1 = wwin->frame->core->width;
	h1 = wwin->frame->core->height;
	w2 = obscured->frame->core->width;
	h2 = obscured->frame->core->height;

	if (!IS_OMNIPRESENT(wwin) && !IS_OMNIPRESENT(obscured)
	    && WWIN_PRIMARY_WS(wwin) != WWIN_PRIMARY_WS(obscured))
		return false;

	if (wwin->frame_x + w1 < obscured->frame_x
	    || wwin->frame_y + h1 < obscured->frame_y
	    || wwin->frame_x > obscured->frame_x + w2 || wwin->frame_y > obscured->frame_y + h2)
		return false;

	return true;
}

/* Get the owning client PID for an X window, preferring the server-side
 * X-Resource extension over the client-supplied _NET_WM_PID property */
static pid_t getWindowPidXRes(xcb_window_t win)
{
#ifdef USE_XRES
	xcb_connection_t *conn = wdisp->conn;
	xcb_res_client_id_spec_t spec;
	xcb_res_query_client_ids_reply_t *reply;
	xcb_res_client_id_value_iterator_t it;
	pid_t pid = -1;

	if (conn) {
		spec.client = (uint32_t)win;
		spec.mask   = XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID;

		reply = xcb_res_query_client_ids_reply(conn,
		            xcb_res_query_client_ids(conn, 1, &spec), NULL);
		if (reply) {
			for (it = xcb_res_query_client_ids_ids_iterator(reply);
			     it.rem; xcb_res_client_id_value_next(&it)) {
				if (it.data->spec.mask == XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID &&
				    xcb_res_client_id_value_value_length(it.data) > 0) {
					pid = (pid_t)xcb_res_client_id_value_value(it.data)[0];
					break;
				}
			}
			free(reply);
		}
	}
	return pid;
#else
	(void)win;
	return -1;
#endif
}

pid_t wGetWindowPid(xcb_window_t win)
{
	pid_t pid = getWindowPidXRes(win);

	if (pid > 0)
		return pid;
	return wNETWMGetPidForWindow(win);
}

/*
 * wWindowTrySwallow--
 * Called after a new window is fully managed. If the window's process
 * is a descendant of a managed window that has the swallow_child
 * attribute set, hide that parent and place the new window where it
 * was. Backgrounded children (started with &) are never swallowed.
 */
static void wWindowTrySwallow(WWindow *wwin)
{
	WScreen *scr = wwin->screen_ptr;
	WWindow *parent;
	pid_t child_pid;

	child_pid = wwin->client_pid;

	for (parent = scr->focused_window; parent; parent = parent->prev) {
		pid_t parent_pid;

		if (parent == wwin)
			continue;
		if (!WFLAGP(parent, swallow_child))
			continue;
		if (parent->swallowed)		/* already swallowing something */
			continue;

		parent_pid = parent->client_pid;
		if (parent_pid <= 0)
			continue;

		/* When the new window does not expose _NET_WM_PID, fall back
		 * to scanning the parent's process subtree for a process
		 * whose name matches WM_COMMAND or WM_CLASS */
		if (child_pid <= 0 && wwin->wm_command) {
			char *tok = wstrdup(wwin->wm_command);
			char *first = strtok(tok, " \t");
			if (first)
				child_pid = FindDescendantPidByCmdName(parent_pid, first);
			wfree(tok);
		}
		if (child_pid <= 0 && wwin->wm_instance)
			child_pid = FindDescendantPidByCmdName(parent_pid, wwin->wm_instance);
		if (child_pid <= 0 && wwin->wm_class)
			child_pid = FindDescendantPidByCmdName(parent_pid, wwin->wm_class);

		if (child_pid <= 0)
			continue;
		if (IsBackgroundProcess(child_pid) > 0)
			continue;
		if (!IsProcessAncestor(parent_pid, child_pid))
			continue;

		/* perform the swallow */
		parent->swallowed = wwin;
		wwin->swallower = parent;

		wWindowConfigure(wwin, parent->frame_x, parent->frame_y,
				 parent->client.width, parent->client.height);

		/* hide the parent through the standard hidden-flag path so
		 * the switch panel, workspace change, etc. all see it as
		 * hidden */
		parent->flags.hidden = 1;
		wWindowUnmap(parent);
		wClientSetState(parent, XCB_ICCCM_WM_STATE_ICONIC, XCB_NONE);
		WMPostNotificationName(WMNChangedState, parent, "hide");

		wSetFocusTo(scr, wwin);
		warpPointerToFocusedWindow(wwin);
		wRaiseFrame(wwin->frame->core);
		break;
	}
}

/*
 * wWindowUnswallow--
 * Called when a swallowed window is being unmanaged. Restores the
 * swallower that was hidden when the swallow happened.
 */
static void wWindowUnswallow(WWindow *wwin)
{
	WWindow *parent = wwin->swallower;

	if (!parent)
		return;

	parent->swallowed = NULL;
	wwin->swallower = NULL;

	/* unhide the parent directly rather than through wMakeWindowVisible(),
	 * which would unhide the whole application */
	parent->flags.hidden = 0;
	parent->flags.skip_next_animation = 1;
	if (wWindowIsVisibleOnAnyHead(parent)) {
		xcb_map_window(wdisp->conn, parent->client_win);
		xcb_map_window(wdisp->conn, parent->frame->core->window);
		wClientSetState(parent, XCB_ICCCM_WM_STATE_NORMAL, XCB_NONE);
		parent->flags.mapped = 1;
		wRaiseFrame(parent->frame->core);
		wSetFocusTo(parent->screen_ptr, parent);
		warpPointerToFocusedWindow(parent);
	}
}

static void fixLeaderProperties(WWindow *wwin)
{
	WXWMHints *hints;
	WXWMHints clientHints;
	WXWindowAttributes attr;
	xcb_window_t leaders[2], window;
	char **argv, *command;
	int argc, i, pid;
	bool haveCommand;

	pid = wwin->client_pid;
	if (pid > 0)
		haveCommand = GetCommandForPid(pid, &argv, &argc);
	else
		haveCommand = false;

	leaders[0] = wwin->client_leader;
	leaders[1] = wwin->group_id;

	if (haveCommand && !wwin->wm_command) {
		WXSetCommand(wdisp->conn, wwin->client_win, argv, argc);
		wwin->wm_command = wtokenjoin(argv, argc);
	}

	for (i = 0; i < 2; i++) {
		char *inst, *cls;

		window = leaders[i];
		/* the client window already carries everything this loop
		 * would back-fill (wm_class, wm_hints, WM_COMMAND from the
		 * block above) and CLIENT_EVENTS already covers the
		 * SelectInput mask, so skip it as a leader to avoid
		 * re-reading what wManagePrefetch just batched */
		if (!window || window == wwin->client_win)
			continue;

		if (WXGetWMClass(wdisp->conn, window, &inst, &cls)) {
			wfree(inst);
			wfree(cls);
		} else {
			WXSetWMClass(wdisp->conn, window, wwin->wm_instance, wwin->wm_class);
		}
		hints = WXGetWMHints(wdisp->conn, window);
		if (hints) {
			wfree(hints);
		} else if (wwin->wm_hints) {
			/* set window group leader to self, work on a stack
			 * copy so wwin->wm_hints stays unchanged */
			clientHints = *wwin->wm_hints;
			clientHints.window_group = window;
			clientHints.flags |= XCB_ICCCM_WM_HINT_WINDOW_GROUP;
			WXSetWMHints(wdisp->conn, window, &clientHints);
		}

		if (haveCommand) {
			command = GetCommandForWindow(window);
			if (command)
				wfree(command); /* command already set. nothing to do. */
			else
				WXSetCommand(wdisp->conn, window, argv, argc);
		}

		/* Make sure we get notification when this window is destroyed */
		if (wXGetWindowAttributes(wdisp->conn, window, &attr, NULL))
			xcb_change_window_attributes(wdisp->conn, window, XCB_CW_EVENT_MASK, (const uint32_t[]){ attr.your_event_mask | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE });
	}

	if (haveCommand)
		wfree(argv);
}

static xcb_window_t createFakeWindowGroupLeader(WScreen *scr, WWindow *wwin, xcb_window_t win,
					  char *instance, char *class)
{
	WXWMHints *hints, copy;
	xcb_window_t leader;
	int argc;
	char **argv;

	leader = xcb_generate_id(wdisp->conn);
	xcb_create_window(wdisp->conn, XCB_COPY_FROM_PARENT, leader, scr->root_win, 10, 10, 10, 10, 0,
			  XCB_WINDOW_CLASS_COPY_FROM_PARENT, XCB_COPY_FROM_PARENT,
			  XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL, (const uint32_t[]){ 0, 0});
	/* set class hint */
	WXSetWMClass(wdisp->conn, leader, instance, class);

	/* inherit these from the original leader if available, when the
	 * leader is the just-managed client window the values are already
	 * on wwin from the wManagePrefetch batch so use those instead of
	 * reading them back */
	if (win == wwin->client_win && wwin->wm_hints) {
		copy = *wwin->wm_hints;
		hints = &copy;
	} else {
		hints = WXGetWMHints(wdisp->conn, win);
	}
	if (!hints) {
		hints = WXAllocWMHints();
		hints->flags = 0;
	}
	/* set window group leader to self */
	hints->window_group = leader;
	hints->flags |= XCB_ICCCM_WM_HINT_WINDOW_GROUP;
	WXSetWMHints(wdisp->conn, leader, hints);
	if (hints != &copy)
		wfree(hints);

	if (win == wwin->client_win) {
		if (wwin->wm_command) {
			wtokensplit(wwin->wm_command, &argv, &argc);
			if (argc > 0)
				WXSetCommand(wdisp->conn, leader, argv, argc);
			wtokenfree(argv, argc);
		}
	} else if (WXGetCommand(wdisp->conn, win, &argv, &argc) && argc > 0) {
		WXSetCommand(wdisp->conn, leader, argv, argc);
		WXFreeStringList(argv, argc);
	}

	return leader;
}

static int matchIdentifier(const void *item, const void *cdata)
{
	return (strcmp(((WFakeGroupLeader *) item)->identifier, (char *)cdata) == 0);
}

/* Resolve the plural StartWorkspaces hint: lowest set bit is the primary,
 * rest goes into restored_ws_mask. Returns False for no/empty/omnipresent. */
static bool resolve_start_workspaces_hint(WScreen *scr, const char *instance,
					   const char *class,
					   int *out_workspace,
					   uint64_t *out_restored_mask)
{
	uint64_t hint_mask = 0;

	if (!wDefaultGetStartWorkspaces(scr, instance, class, &hint_mask))
		return false;
	if (hint_mask == 0 || hint_mask == WS_ALL)
		return false;
	if (scr->workspace_count < MAX_WORKSPACES)
		hint_mask &= WS_BIT(scr->workspace_count) - 1;
	if (hint_mask == 0)
		return false;
	*out_workspace = __builtin_ctzll(hint_mask);
	*out_restored_mask = hint_mask & ~WS_BIT(*out_workspace);
	return true;
}

/*
 *----------------------------------------------------------------
 * wManageWindow--
 * 	reparents the window and allocates a descriptor for it.
 * Window manager hints and other hints are fetched to configure
 * the window decoration attributes and others. User preferences
 * for the window are used if available, to configure window
 * decorations and some behaviour.
 * 	If in startup, windows that are override redirect,
 * unmapped and never were managed and are Withdrawn are not
 * managed.
 *
 * Returns:
 * 	the new window descriptor
 *
 * Side effects:
 * 	The window is reparented and appropriate notification
 * is done to the client. Input mask for the window is setup.
 * 	The window descriptor is also associated with various window
 * contexts and inserted in the head of the window list.
 * Event handler contexts are associated for some objects
 * (buttons, titlebar and resizebar)
 *
 *----------------------------------------------------------------
 */
WWindow *wManageWindow(WScreen *scr, xcb_window_t window,
		       const WXWindowAttributes *prefetched_attrs)
{
	WWindow *wwin;
	int x, y;
	unsigned width, height;
	WXWindowAttributes wattribs;
	WWindowState *win_state;
	WWindow *transientOwner = NULL;
	int window_level;
	int wm_state;
	int foo;
	int il, ir, it, ib;
	int workspace = -1;
	uint64_t restored_ws_mask = 0;
	char *title;
	bool withdraw = false;
	bool raise = false;
	bool dontBring = false;
	/* True when x/y were taken from a saved-state source (session file
	 * or _WINDOWMAKER_STATE), so the south-gravity conversion below
	 * can be skipped: saved coords are already frame-relative. */
	bool position_from_saved = false;
	WManageCookies ck;
	WSavedState *wstate;
	char **cmd_argv;
	int cmd_argc;
	uint32_t net_pid;
	int gx, gy;

	/* mutex. */
	wXGrabServer(wdisp->conn);

	/* make sure the window is still there. manageAllWindows() supplies
	 * attributes it already batched across every child, the MapRequest
	 * path passes NULL and we fetch here, the GetWindowAttributes
	 * round-trip itself orders the grab and its return value is the
	 * existence check, so no XSync between the two. */
	if (prefetched_attrs) {
		wattribs = *prefetched_attrs;
	} else if (!wXGetWindowAttributes(wdisp->conn, window, &wattribs, NULL)
		   || wattribs.visual == NULL) {
		wXUngrabServer(wdisp->conn);
		return NULL;
	}

	/* if it's an override-redirect, ignore it */
	if (wattribs.override_redirect) {
		wXUngrabServer(wdisp->conn);
		return NULL;
	}

	/* Issue every property request for this window in one burst, the
	 * first WXRcv* below blocks once for the whole batch. */
	wManagePrefetch(window, &ck);

	wm_state = PropRcvWindowState(ck.wm_state);

	/* if it's startup and the window is unmapped, don't manage it */
	if (scr->flags.startup && wm_state < 0 && wattribs.map_state == XCB_MAP_STATE_UNMAPPED) {
		wManageDiscard(&ck);
		wXUngrabServer(wdisp->conn);
		return NULL;
	}

	wwin = wWindowCreate();
	/* Mark the window as mid-manage so wWindowConfigureBorders() can
	 * skip the top_width-compensation on the initial static- and
	 * dynamic-rule decoration apply. Cleared just before return. */
	wwin->flags.initializing = 1;
	wwin->creation_id = ++scr->last_creation_id;

	title = WXRcvString(wdisp->conn, ck.net_name, NULL);
	if (title)
		wwin->flags.net_has_title = 1;
	else
		title = WXRcvWMName(wdisp->conn, ck.name);

	if (title)
		wwin->wm_title = wstrdup(title);

	WMHashInsert(wdisp->context.client_win, (void *)(uintptr_t) window, &wwin->client_descriptor);

#ifdef USE_XSHAPE
	if (wdisp->xext.shape.supported) {
		xcb_shape_query_extents_reply_t *sr;

		xcb_shape_select_input(wdisp->conn, window, 1);
		sr = xcb_shape_query_extents_reply(wdisp->conn,
				xcb_shape_query_extents(wdisp->conn, window), NULL);
		wwin->flags.shaped = sr ? sr->bounding_shaped : 0;
		free(sr);
	}
#endif

	/* Get hints and other information in properties */
	PropRcvWMClass(ck.wclass, &wwin->wm_class, &wwin->wm_instance);

	wwin->wm_role = WXRcvString(wdisp->conn, ck.role, NULL);
	if (wwin->wm_role && wwin->wm_role[0] == '\0') {
		wfree(wwin->wm_role);
		wwin->wm_role = NULL;
	}

	if (WXRcvCommand(wdisp->conn, ck.command, &cmd_argv, &cmd_argc)) {
		if (cmd_argc > 0)
			wwin->wm_command = wtokenjoin(cmd_argv, cmd_argc);
		WXFreeStringList(cmd_argv, cmd_argc);
	}
	if (wwin->wm_command && wwin->wm_command[0] == '\0') {
		wfree(wwin->wm_command);
		wwin->wm_command = NULL;
	}

	/* setup descriptor */
	wwin->client_win = window;
	wwin->screen_ptr = scr;
	wwin->old_border_width = wattribs.border_width;
	wwin->colormap = wattribs.colormap;
	/* one XRes round-trip per managed window, read everywhere else
	 * via wwin->client_pid, the wManagePrefetch batch is in flight
	 * so this stacks behind it rather than serialising. The
	 * _NET_WM_PID fallback is the batched cookie so a build without
	 * XRes (or a remote client XRes cannot resolve) costs no extra
	 * round-trip. */
	wwin->client_pid = getWindowPidXRes(window);
	if (wwin->client_pid <= 0) {
		if (WXRcvCardinal(wdisp->conn, ck.pid, &net_pid))
			wwin->client_pid = (pid_t)net_pid;
		ck.pid.seq = 0;
	}
	wwin->event_mask = CLIENT_EVENTS;

	xcb_change_window_attributes(wdisp->conn, window,
				     XCB_CW_EVENT_MASK | XCB_CW_SAVE_UNDER | XCB_CW_DONT_PROPAGATE,
				     /* wire order is ascending CW_ bit:
				      * save_under, event_mask, dont_propagate */
				     (const uint32_t[]){ false, CLIENT_EVENTS,
							 XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE });
	xcb_configure_window(wdisp->conn, window, XCB_CONFIG_WINDOW_BORDER_WIDTH, (const uint32_t[]){ 0 });

	if (wwin->wm_class != NULL && strcmp(wwin->wm_class, "DockApp") != 0)
		wwin->flags.fullscreen_monitors[0] = -1;

	/* get hints from GNUstep app */
	if (wwin->wm_class != NULL && strcmp(wwin->wm_class, "GNUstep") == 0)
		wwin->flags.is_gnustep = 1;

	if (!PropRcvGNUstepWMAttr(ck.gnustep, &wwin->wm_gnustep_attr))
		wwin->wm_gnustep_attr = NULL;

	if (wwin->wm_class != NULL && strcmp(wwin->wm_class, "DockApp") == 0) {
		wwin->flags.is_dockapp = 1;
		withdraw = true;
	}

	wwin->client_leader = PropRcvClientLeader(ck.leader);
	if (wwin->client_leader != XCB_NONE)
		wwin->main_window = wwin->client_leader;

	wwin->wm_hints = WXRcvWMHints(wdisp->conn, ck.hints);

	if (wwin->wm_hints) {
		if (wwin->wm_hints->flags & XCB_ICCCM_WM_HINT_STATE) {
			if (wwin->wm_hints->initial_state == XCB_ICCCM_WM_STATE_ICONIC) {
				wwin->flags.miniaturized = 1;
			} else if (wwin->wm_hints->initial_state == XCB_ICCCM_WM_STATE_WITHDRAWN) {
				wwin->flags.is_dockapp = 1;
				withdraw = true;
			}
		}

		if (wwin->wm_hints->flags & XCB_ICCCM_WM_HINT_WINDOW_GROUP) {
			wwin->group_id = wwin->wm_hints->window_group;
			/* window_group has priority over CLIENT_LEADER */
			wwin->main_window = wwin->group_id;
		} else {
			wwin->group_id = XCB_NONE;
		}

		if (wwin->wm_hints->flags & XCB_ICCCM_WM_HINT_X_URGENCY) {
			wwin->flags.urgent = 1;
			wAppBounceWhileUrgent(wApplicationOf(wwin->main_window));
		}
	} else {
		wwin->group_id = XCB_NONE;
	}

	PropRcvProtocols(ck.protocols, &wwin->protocols);

	if (!WXRcvTransientFor(wdisp->conn, ck.transient, &wwin->transient_for)) {
		wwin->transient_for = XCB_NONE;
	} else {
		if (wwin->transient_for == XCB_NONE || wwin->transient_for == window) {
			wwin->transient_for = scr->root_win;
		} else {
			transientOwner = wWindowFor(wwin->transient_for);
			if (transientOwner && transientOwner->main_window != XCB_NONE)
				wwin->main_window = transientOwner->main_window;
		}
	}

	/* guess the focus mode */
	wwin->focus_mode = getFocusMode(wwin);

	/* get geometry stuff */
	wClientGetNormalHints(wwin, &wattribs, true, ck.normal, &x, &y, &width, &height);

	/* get colormap windows */
	wClientRcvColormapWindows(wwin, ck.cmap_windows);

	/*
	 * Setup the decoration/window attributes and
	 * geometry
	 */
	wWindowSetupInitialAttributes(wwin, &ck, &window_level, &workspace);

	/* Make broken apps behave as a nice app. */
	if (WFLAGP(wwin, emulate_appicon))
		wwin->main_window = wwin->client_win;

	fixLeaderProperties(wwin);

	wwin->orig_main_window = wwin->main_window;

	if (wwin->flags.is_gnustep)
		WSETUFLAG(wwin, shared_appicon, 0);

	if (wwin->main_window) {
		bool has_appmenu;
		/* the prefetch batch already probed _WINDOWMAKER_MENU on the
		 * client window, only re-read when main_window resolved to a
		 * distinct group leader */
		if (wwin->main_window == wwin->client_win) {
			has_appmenu = WXRcvExists(wdisp->conn, ck.appmenu);
			ck.appmenu.seq = 0;
		} else {
			has_appmenu = WXPropExists(wdisp->conn, wwin->main_window,
						   wdisp->atom.wmaker.menu);
		}
		if (has_appmenu) {
			WSETUFLAG(wwin, shared_appicon, 0);
		}
	}

	if (wwin->flags.is_dockapp)
		WSETUFLAG(wwin, shared_appicon, 0);

	if (wwin->main_window) {
		WApplication *app = wApplicationOf(wwin->main_window);

		if (app && app->app_icon)
			WSETUFLAG(wwin, shared_appicon, 0);
	}

	if (!withdraw && wwin->main_window && WFLAGP(wwin, shared_appicon)) {
		char *buffer, *instance, *class;
		WFakeGroupLeader *fPtr;
		int index;

#define ADEQUATE(x) ((x)!=XCB_NONE && (x)!=wwin->client_win && (x)!=fPtr->leader)

		/* main_window is usually the just-managed client itself
		 * (clients without a group leader hint), in which case
		 * the class strings are already on wwin from the
		 * wManagePrefetch batch */
		if (wwin->main_window == wwin->client_win) {
			instance = wwin->wm_instance ? (char *)wintern(wwin->wm_instance) : NULL;
			class    = wwin->wm_class    ? (char *)wintern(wwin->wm_class)    : NULL;
		} else {
			PropGetWMClass(wwin->main_window, &class, &instance);
		}
		if (!class || class[0] == '\0') {
			wunintern(class);
			if (wwin->wm_class && wwin->wm_class[0] != '\0')
				class = (char *)wintern(wwin->wm_class);
			else
				class = (char *)wintern("default");
		}
		buffer = StrConcatDot(instance, class);

		index = WMFindInArray(scr->fakeGroupLeaders, matchIdentifier, (void *)buffer);
		if (index != WANotFound) {
			fPtr = WMGetFromArray(scr->fakeGroupLeaders, index);
			if (fPtr->retainCount == 0)
				fPtr->leader = createFakeWindowGroupLeader(scr, wwin, wwin->main_window,
									   instance, class);

			fPtr->retainCount++;
			if (fPtr->origLeader == XCB_NONE) {
				if (ADEQUATE(wwin->main_window)) {
					fPtr->retainCount++;
					fPtr->origLeader = wwin->main_window;
				}
			}
			wwin->fake_group = fPtr;
			wwin->main_window = fPtr->leader;
			wfree(buffer);
		} else {
			fPtr = (WFakeGroupLeader *) wmalloc(sizeof(WFakeGroupLeader));

			fPtr->identifier = buffer;
			fPtr->leader = createFakeWindowGroupLeader(scr, wwin, wwin->main_window, instance, class);
			fPtr->origLeader = XCB_NONE;
			fPtr->retainCount = 1;

			WMAddToArray(scr->fakeGroupLeaders, fPtr);

			if (ADEQUATE(wwin->main_window)) {
				fPtr->retainCount++;
				fPtr->origLeader = wwin->main_window;
			}
			wwin->fake_group = fPtr;
			wwin->main_window = fPtr->leader;
		}

		if (instance)
			wunintern(instance);

		if (class)
			wunintern(class);
#undef ADEQUATE
	}

	/*
	 * Setup the initial state of the window
	 */
	if (WFLAGP(wwin, start_miniaturized) && !WFLAGP(wwin, no_miniaturizable))
		wwin->flags.miniaturized = 1;

	if (WFLAGP(wwin, start_maximized) && IS_RESIZABLE(wwin))
		wwin->flags.maximized = MAX_VERTICAL | MAX_HORIZONTAL;

	wNETWMCheckInitialClientStateCk(wwin, &ck.netwm_initial);

	/* apply previous state if it exists and we're in startup */
	if (scr->flags.startup && wm_state >= 0) {
		if (wm_state == XCB_ICCCM_WM_STATE_ICONIC)
			wwin->flags.miniaturized = 1;
		else if (wm_state == XCB_ICCCM_WM_STATE_WITHDRAWN)
			withdraw = true;
	}

	/* if there is a saved state (from file), restore it */
	win_state = NULL;
	if (wwin->main_window != XCB_NONE)
		win_state = (WWindowState *) wWindowGetSavedState(wwin->main_window);
	else
		win_state = (WWindowState *) wWindowGetSavedState(window);

	if (win_state && !withdraw) {
		if (win_state->state->hidden > 0)
			wwin->flags.hidden = win_state->state->hidden;

		if (win_state->state->shaded > 0 && !WFLAGP(wwin, no_shadeable))
			wwin->flags.shaded = win_state->state->shaded;

		if (win_state->state->miniaturized > 0 && !WFLAGP(wwin, no_miniaturizable)) {
			wwin->flags.miniaturized = win_state->state->miniaturized;
			wwin->flags.restore_iconified = 1;
		}

		if (win_state->state->maximized > 0)
			wwin->flags.maximized = win_state->state->maximized;

		if (!IS_OMNIPRESENT(wwin)) {
			int hint_ws;
			uint64_t hint_rest;

			if (resolve_start_workspaces_hint(scr, wwin->wm_instance,
							  wwin->wm_class, &hint_ws, &hint_rest)) {
				workspace = hint_ws;
				restored_ws_mask = hint_rest;
			} else {
				workspace = win_state->state->workspace;
				restored_ws_mask = win_state->state->ws_mask;
				if (workspace >= scr->workspace_count)
					workspace = scr->current_workspace;
			}
		} else {
			workspace = scr->current_workspace;
		}
	}

	/* if we're restarting, restore saved state (from hints).
	 * This will overwrite previous */
	if (getSavedStateCk(window, ck.saved_state, ck.mark_key, ck.tab_group, &wstate)) {
		/* A per-window slot wins over the domain fill. A domain
		 * snapshot only fills a flag the current fill left
		 * undefined, so a domain edit wins while a deleted rule
		 * keeps the live window stable. */
#define WRESTORESLOT(FIELD, FLAG) \
		do { \
			if (wstate->FIELD##_set == 1 \
			    || (wstate->FIELD##_set == 2 && !wwin->domain_flags.FLAG)) \
				WRESTOREHINTFLAG(wwin, FLAG, wstate->FIELD); \
		} while (0)
		wwin->flags.shaded = wstate->shaded;
		wwin->flags.hidden = wstate->hidden;
		wwin->flags.miniaturized = wstate->miniaturized;
		wwin->flags.maximized = wstate->maximized;
		WRESTORESLOT(auto_hide_on_unfocus, auto_hide_on_unfocus);
		WRESTORESLOT(auto_miniaturize_on_unfocus, auto_miniaturize_on_unfocus);
		WRESTORESLOT(no_movable, no_movable);
		WRESTORESLOT(no_resizable, no_resizable);
		if (wstate->no_titlebar_set == 1
		    || (wstate->no_titlebar_set == 2 && !wwin->domain_flags.no_titlebar)) {
			WRESTOREHINTFLAG(wwin, no_titlebar, wstate->no_titlebar);
			WRESTOREHINTFLAG(wwin, no_shadeable, wstate->no_titlebar);
		}
		/* a titlebar-less window cannot be shaded */
		if (WFLAGP(wwin, no_titlebar))
			wwin->flags.shaded = 0;
		WRESTORESLOT(no_resizebar, no_resizebar);
		WRESTORESLOT(no_close_button, no_close_button);
		WRESTORESLOT(no_miniaturize_button, no_miniaturize_button);
		WRESTORESLOT(no_border, no_border);
		WRESTORESLOT(skip_window_list, skip_window_list);
		WRESTORESLOT(skip_switchpanel, skip_switchpanel);
#undef WRESTORESLOT
		if (wstate->stacking_level) {
			switch (wstate->stacking_level) {
			case 1:
				window_level = WMNormalLevel;
				break;
			case 2:
				window_level = WMFloatingLevel;
				break;
			default:
				window_level = WMSunkenLevel;
				break;
			}
			if (wstate->stacking_level == 2)
				WRESTOREHINTFLAG(wwin, floating, 1);
			else if (wstate->stacking_level == 3)
				WRESTOREHINTFLAG(wwin, sunken, 1);
		}
		if (wwin->flags.maximized) {
			wwin->old_geometry.x = wstate->x;
			wwin->old_geometry.y = wstate->y;
			wwin->old_geometry.width = wstate->w;
			wwin->old_geometry.height = wstate->h;
		}

		workspace = wstate->workspace;
		restored_ws_mask = wstate->ws_mask;
	} else {
		wstate = NULL;
	}

	/* restore window shortcut */
	if (wstate != NULL || win_state != NULL) {
		unsigned mask = 0;

		if (win_state != NULL)
			mask = win_state->state->window_shortcuts;

		if (wstate != NULL && mask == 0)
			mask = wstate->window_shortcuts;

		if (mask > 0) {
			int i;

			for (i = 0; i < MAX_WINDOW_SHORTCUTS; i++) {
				if (mask & (1 << i)) {
					if (!scr->shortcutWindows[i])
						scr->shortcutWindows[i] = WMCreateArray(4);

					WMAddToArray(scr->shortcutWindows[i], wwin);
				}
			}
		}
	}

	/* restore mark: prefer session state, fall back to warm-restart hint.
	 * The session-file and dynamic-rule paths steal from any existing
	 * holder so the global-uniqueness invariant holds across warm
	 * restarts. The client-supplied _WINDOWMAKER_MARK_KEY hint is
	 * untrusted, so set it without stealing to prevent a hostile
	 * client from stripping marks off other windows. */
	if (win_state != NULL && win_state->state->mark_key != NULL)
		wWindowStealMark(wwin, win_state->state->mark_key);
	else if (wstate != NULL && wstate->mark_key != NULL)
		wWindowSetMark(wwin, wstate->mark_key);
	else {
		const char *mk = wDefaultGetMarkKeyForWindow(wwin);

		if (mk != NULL) {
			if (mk[0] == '\0')
				wWindowUnsetMark(wwin);
			else
				wWindowStealMark(wwin, mk);
		}
	}

	/* restore tab group: warm-restart hint takes precedence because it
	 * is per-client-window, the session entry is matched by class and
	 * may be shared by several windows of the same launched command.
	 * The triple is stashed on wwin and applied by the caller after
	 * the deferred shade and iconify steps so an iconified group is
	 * rebuilt from members that already own their miniwindow. */
	if (wstate != NULL && wstate->tab_group_gid != 0) {
		wwin->restore_tab_gid = wstate->tab_group_gid;
		wwin->restore_tab_idx = wstate->tab_group_idx;
		wwin->restore_tab_active = wstate->tab_group_active;
	} else if (win_state != NULL && win_state->state->tab_group_gid != 0) {
		wwin->restore_tab_gid = win_state->state->tab_group_gid;
		wwin->restore_tab_idx = win_state->state->tab_group_idx;
		wwin->restore_tab_active = win_state->state->tab_group_active;
	}

	/* wstate is freed after the placement decision below, which still
	 * needs wstate->x/y as the saved frame origin on warm restart when
	 * the session-file channel (win_state) is empty. */

	/* don't let transients start miniaturized if their owners are not */
	if (transientOwner && !transientOwner->flags.miniaturized && wwin->flags.miniaturized && !withdraw) {
		wwin->flags.miniaturized = 0;
		if (wwin->wm_hints)
			wwin->wm_hints->initial_state = XCB_ICCCM_WM_STATE_NORMAL;
	}

	/* set workspace on which the window starts */
	if (workspace >= 0) {
		if (workspace > scr->workspace_count - 1)
			workspace = workspace % scr->workspace_count;
	} else {
		int hint_ws;
		uint64_t hint_rest;

		if (!IS_OMNIPRESENT(wwin) &&
		    resolve_start_workspaces_hint(scr, wwin->wm_instance,
						  wwin->wm_class, &hint_ws, &hint_rest)) {
			workspace = hint_ws;
			restored_ws_mask = hint_rest;
		} else {
			if (w_global.config->open_transients_with_parent && transientOwner)
				workspace = WWIN_PRIMARY_WS(transientOwner);
			else if (wwin->sn_workspace >= 0 && wwin->sn_workspace < scr->workspace_count)
				workspace = wwin->sn_workspace;
			else
				workspace = scr->current_workspace;
		}
	}

	/* Provisional per-head placement target: a head already showing
	 * this workspace (or allowed to), so PlaceWindow constrains the
	 * frame there and the visibility test after configure agrees. */
	wwin->last_head = wHeadShowingWorkspace(scr, workspace);

	/* setup window geometry */
	if (win_state && win_state->state->w > 0) {
		width = win_state->state->w;
		height = win_state->state->h;
	}

	wWindowConstrainSize(wwin, &width, &height);

	/* do not ask for window placement if the window is
	 * transient, during startup, or if the window wants
	 * to start iconic. If geometry was saved, restore it. */
	if (win_state && win_state->state->w > 0) {
		WMRect rect;
		int head, flags;

		x = win_state->state->x;
		y = win_state->state->y;
		position_from_saved = true;

		/* If the saved position falls in dead space (for example a monitor that no longer
		* exists), clamp the window to the nearest active head. */
		rect.pos.x = x;
		rect.pos.y = y;
		rect.size.width = width;
		rect.size.height = height;

		head = wGetRectPlacementInfo(scr, rect, &flags);
		if (flags & XFLAG_DEAD) {
			rect = wGetRectForHead(scr, head);

			x = rect.pos.x + (x * rect.size.width) / scr->scr_width;
			y = rect.pos.y + (y * rect.size.height) / scr->scr_height;

			wScreenBringInside(scr, &x, &y, width, height);
		}
	} else if (wstate != NULL && !wwin->flags.maximized && wstate->w > 0) {
		/* Warm restart: wSessionRestoreState() runs only on cold boot,
		 * so on a warm restart win_state is empty and without this
		 * branch x/y would default to wattribs->x/y (the
		 * client's current root position). wWindowConfigure() treats
		 * req_y as the new frame origin, so feeding it the client root
		 * y drifts frame_y by (top_width + border + inner) each
		 * restart. The _WINDOWMAKER_STATE property written by
		 * wWindowSaveState() stores the exact frame origin for
		 * non-maximized windows, so reuse it here. wstate->w/h are
		 * frame core dimensions and cannot override the client size
		 * (wattribs->width/height) that wWindowConfigure() expects. */
		WMRect rect;
		int head, flags;

		x = wstate->x;
		y = wstate->y;
		position_from_saved = true;

		rect.pos.x = x;
		rect.pos.y = y;
		rect.size.width = width;
		rect.size.height = height;

		head = wGetRectPlacementInfo(scr, rect, &flags);
		if (flags & XFLAG_DEAD) {
			rect = wGetRectForHead(scr, head);

			x = rect.pos.x + (x * rect.size.width) / scr->scr_width;
			y = rect.pos.y + (y * rect.size.height) / scr->scr_height;

			wScreenBringInside(scr, &x, &y, width, height);
		}
	} else if ((wwin->transient_for == XCB_NONE || w_global.config->window_placement != WPM_MANUAL)
		   && !scr->flags.startup
		   && !wwin->flags.miniaturized
		   && !wwin->flags.maximized && !(wwin->normal_hints->flags & (XCB_ICCCM_SIZE_HINT_US_POSITION | XCB_ICCCM_SIZE_HINT_P_POSITION))) {

		if (transientOwner && transientOwner->flags.mapped) {
			int offs = WMAX(20, 2 * transientOwner->frame->top_width);
			WMRect rect;
			int head;

			x = transientOwner->frame_x +
				abs(((int)transientOwner->frame->core->width - (int)width) / 2) + offs;
			y = transientOwner->frame_y +
				abs(((int)transientOwner->frame->core->height - (int)height) / 3) + offs;

			/* limit transient windows to be inside their parent's head */
			rect.pos.x = transientOwner->frame_x;
			rect.pos.y = transientOwner->frame_y;
			rect.size.width = transientOwner->frame->core->width;
			rect.size.height = transientOwner->frame->core->height;

			head = wGetHeadForRect(scr, rect);
			rect = wGetRectForHead(scr, head);

			if (x < rect.pos.x)
				x = rect.pos.x;
			else if (x + width > rect.pos.x + rect.size.width)
				x = rect.pos.x + rect.size.width - width;

			if (y < rect.pos.y)
				y = rect.pos.y;
			else if (y + height > rect.pos.y + rect.size.height)
				y = rect.pos.y + rect.size.height - height;

		} else {
			PlaceWindow(wwin, &x, &y, width, height);
		}

		if (w_global.config->window_placement == WPM_MANUAL)
			dontBring = true;

	} else if (scr->xine_info.count && (wwin->normal_hints->flags & XCB_ICCCM_SIZE_HINT_P_POSITION)) {
		int head, flags;
		WMRect rect;
		int reposition = 0;

		/* Make spash screens come out in the center of a head
		 * trouble is that most splashies never get here
		 * they are managed trough atoms but god knows where.
		 * Dan, do you know ? -peter
		 *
		 * Most of them are not managed, they have set
		 * OverrideRedirect, which means we can't do anything about
		 * them. -alfredo */

		/* xinerama checks for: across head and dead space */
		rect.pos.x = x;
		rect.pos.y = y;
		rect.size.width = width;
		rect.size.height = height;

		head = wGetRectPlacementInfo(scr, rect, &flags);

		if (flags & XFLAG_DEAD)
			reposition = 1;

		if (flags & XFLAG_MULTIPLE)
			reposition = 2;

		switch (reposition) {
		case 1:
			head = wGetHeadForPointerLocation(scr);
			rect = wGetRectForHead(scr, head);

			x = rect.pos.x + (x * rect.size.width) / scr->scr_width;
			y = rect.pos.y + (y * rect.size.height) / scr->scr_height;
			break;

		case 2:
			rect = wGetRectForHead(scr, head);

			if (x < rect.pos.x)
				x = rect.pos.x;
			else if (x + width > rect.pos.x + rect.size.width)
				x = rect.pos.x + rect.size.width - width;

			if (y < rect.pos.y)
				y = rect.pos.y;
			else if (y + height > rect.pos.y + rect.size.height)
				y = rect.pos.y + rect.size.height - height;

			break;

		default:
			break;
		}
	}

	if (WFLAGP(wwin, dont_move_off) && dontBring)
		wScreenBringInside(scr, &x, &y, width, height);

	wNETWMPositionSplash(wwin, &x, &y, width, height);

	if (wwin->start_geometry_set) {
		if (wwin->start_geometry.width > 0)
			width = wwin->start_geometry.width;
		if (wwin->start_geometry.height > 0)
			height = wwin->start_geometry.height;
		wWindowConstrainSize(wwin, &width, &height);
		if (wwin->start_geometry.x >= 0)
			x = wwin->start_geometry.x;
		else
			x = scr->scr_width - width + wwin->start_geometry.x;
		if (wwin->start_geometry.y >= 0)
			y = wwin->start_geometry.y;
		else
			y = scr->scr_height - height + wwin->start_geometry.y;
	}

	/* Create frame, borders and do reparenting */
	foo = WFF_MINIATURIZE_BUTTON | WFF_CLOSE_BUTTON | WFF_MAXIMIZE_BUTTON | WFF_ICON_SLOT;
#ifdef XKB_BUTTON_HINT
	if (w_global.config->modelock)
		foo |= WFF_LANGUAGE_BUTTON;
#endif
	if (HAS_TITLEBAR(wwin))
		foo |= WFF_TITLEBAR;

	if (HAS_RESIZEBAR(wwin))
		foo |= WFF_RESIZEBAR;

	if (HAS_BORDER(wwin))
		foo |= WFF_BORDER;
	foo |= WFF_IS_CLIENT_WINDOW_FRAME;

	wFrameInnerBorderWidths(wwin, &il, &ir, &it, &ib);
	wwin->frame = wFrameWindowCreate(scr, window_level,
					 x, y, width + il + ir, height + it + ib,
					 foo,
					 scr->window_title_texture,
					 scr->resizebar_texture, scr->window_title_color, &scr->title_font,
					 wattribs.depth, (xcb_visualtype_t *) wattribs.visual, wattribs.colormap);

	/* wFrameWindowCreate() ran wFrameWindowUpdateBorders() before
	 * fwin->child was set, so border pixels were allocated from the
	 * screen-global colors. Set child now and re-run so per-window
	 * frame border overrides take effect. */
	wwin->frame->child = wwin;
	wWindowConfigureBorders(wwin);

	wwin->frame->flags.justification = w_global.config->title_justification;

	wNETWMCheckInitialFrameStateCk(wwin, ck.netwm_initial.opacity);

	/* All prefetched cookies are now either consumed or skipped (the
	 * GNUstep branch bypasses motif/netwm/gtk, restore-on-startup may
	 * skip saved_state/mark_key). Discard whatever remains,
	 * already-consumed sequence numbers are a no-op. */
	wManageDiscard(&ck);

	/* setup button images */
	wWindowUpdateButtonImages(wwin);

	/* hide unused buttons */
	foo = 0;
	if (WFLAGP(wwin, no_close_button))
		foo |= WFF_CLOSE_BUTTON;

	if (WFLAGP(wwin, no_miniaturize_button))
		foo |= WFF_MINIATURIZE_BUTTON;

	if (!IS_RESIZABLE(wwin))
		foo |= WFF_MAXIMIZE_BUTTON;

#ifdef XKB_BUTTON_HINT
	if (WFLAGP(wwin, no_language_button) || WFLAGP(wwin, no_focusable))
		foo |= WFF_LANGUAGE_BUTTON;
#endif
	if (foo != 0)
		wFrameWindowHideButton(wwin->frame, foo);

	wwin->frame->workspace = workspace;
	/* Restored membership mask (from _WINDOWMAKER_STATE across a restart
	 * or the session Workspaces list) is applied on top of the primary
	 * bit and clamped to the current workspace count so a stale bit for
	 * a since-deleted workspace does not linger. */
	wwin->frame->ws_mask = WS_BIT(workspace) | restored_ws_mask;
	if (wwin->frame->ws_mask != WS_ALL && scr->workspace_count < MAX_WORKSPACES)
		wwin->frame->ws_mask &= WS_BIT(scr->workspace_count) - 1;
	if (wwin->flags.urgent && !WFLAGP(wwin, omnipresent))
		wwin->frame->ws_mask = WS_ALL;
	wwin->frame->on_click_button[WTB_MINIATURIZE] = windowIconifyClick;

#ifdef XKB_BUTTON_HINT
	if (w_global.config->modelock)
		wwin->frame->on_click_button[WTB_LANGUAGE] = windowLanguageClick;
#endif

	wwin->frame->on_click_button[WTB_CLOSE] = windowCloseClick;
	wwin->frame->on_dblclick_button[WTB_CLOSE] = windowCloseDblClick;
	wwin->frame->on_click_button[WTB_MAXIMIZE] = windowMaximizeClick;
	wwin->frame->on_mousedown_titlebar = titlebarMouseDown;
	wwin->frame->on_dblclick_titlebar = titlebarDblClick;
	wwin->frame->on_mousedown_resizebar = resizebarMouseDown;

	xcb_change_window_attributes(wdisp->conn, wwin->client_win, XCB_CW_EVENT_MASK, (const uint32_t[]){ wwin->event_mask & ~XCB_EVENT_MASK_STRUCTURE_NOTIFY });
	wFrameInnerBorderWidths(wwin, &il, NULL, &it, NULL);
	xcb_reparent_window(wdisp->conn, wwin->client_win, wwin->frame->core->window, il, wwin->frame->top_width + it);
	xcb_change_window_attributes(wdisp->conn, wwin->client_win, XCB_CW_EVENT_MASK, (const uint32_t[]){ wwin->event_mask });

	wClientGetGravityOffsets(wwin, &gx, &gy);

	/* The south-gravity adjustment converts a client-relative position
	 * (where the requested y is anchored to the client's bottom edge)
	 * into a frame-relative position by subtracting the frame's
	 * vertical decoration. It must not fire when the position came
	 * from a saved-state source: the session file and the
	 * _WINDOWMAKER_STATE property both store the frame origin, which
	 * is already frame-relative, so applying the conversion would
	 * shift the window up by (top_width + bottom_width) on every
	 * restore. */
	if (gy > 0 && !position_from_saved)
		y -= wwin->frame->top_width + wwin->frame->bottom_width;

	/*
	 * wWindowConfigure() will init the client window's size
	 * (wwin->client.{width,height}) and all other geometry
	 * related variables (frame_x,frame_y) */
	wWindowConfigure(wwin, x, y, width, height);

	/* wstate supplied the saved frame origin for the placement above.
	 * Now that wWindowConfigure() has set wwin->frame_x/frame_y it is
	 * safe to release. */
	if (wstate != NULL) {
		if (wstate->mark_key != NULL)
			wfree(wstate->mark_key);
		wfree(wstate);
	}

	wWindowRefreshTitlebarIcon(wwin);

	/* Finalize the per-head workspace head from the placed frame
	 * rectangle so visibility checks for an immediately-unmapped
	 * window do not fall back to the pointer head. */
	wwin->last_head = wGetHeadForWindow(wwin);

	/* A re-managed window can keep a pre-restart or client-supplied
	 * position on a head where its workspace is not allowed (startup
	 * skips PlaceWindow, USPosition/PPosition skip it too). Translate
	 * the frame onto the head that is showing or allowed to show the
	 * workspace, keeping its relative position within the head, so the
	 * geometry-derived last_head and the workspace pin agree and the
	 * window does not become permanently invisible. */
	if (w_global.config->per_head_workspaces && !IS_OMNIPRESENT(wwin)
	    && !wWorkspaceIsAllowedOnHead(scr, workspace, wwin->last_head)) {
		int target = wHeadShowingWorkspace(scr, workspace);

		if (target != wwin->last_head
		    && wWorkspaceIsAllowedOnHead(scr, workspace, target))
			translateWindowToHead(wwin, wwin->last_head, target);
	}

	/* to make sure the window receives it's new position after reparenting */
	wWindowSynthConfigureNotify(wwin);

	/* Setup descriptors and save window to internal lists */
	if (wwin->main_window != XCB_NONE) {
		WApplication *app;
		WWindow *leader;

		/* Leader windows do not necessary set themselves as leaders.
		 * If this is the case, point the leader of this window to
		 * itself */
		leader = wWindowFor(wwin->main_window);
		if (leader && leader->main_window == XCB_NONE)
			leader->main_window = leader->client_win;

		app = wApplicationCreate(wwin);
		if (app) {
			app->last_workspace = workspace;

			/* Do application specific stuff, like setting application
			 * wide attributes. */

			if (wwin->flags.hidden) {
				/* if the window was set to hidden because it was hidden
				 * in a previous incarnation and that state was restored */
				app->flags.hidden = 1;
			} else if (app->flags.hidden) {
				if (WFLAGP(app->main_window_desc, start_hidden)) {
					wwin->flags.hidden = 1;
				} else {
					wUnhideApplication(app, false, false);
					raise = true;
				}
			}
			wAppBounce(app);
		}
	}

	/* setup the frame descriptor */
	wwin->frame->core->descriptor.handle_mousedown = frameMouseDown;
	wwin->frame->core->descriptor.parent = wwin;
	wwin->frame->core->descriptor.parent_type = WCLASS_WINDOW;

	/* don't let windows go away if we die */
	xcb_change_save_set(wdisp->conn, XCB_SET_MODE_INSERT, window);

	/* server-side push-below of the fresh frame before it is mapped,
	 * exemption: the stacking list keeps the frame where
	 * AddToStackList() put it and the deferred flush enforces the
	 * list, so this direct configure reorders nothing */
	xcb_configure_window(wdisp->conn, window, XCB_CONFIG_WINDOW_STACK_MODE, (const uint32_t[]){ XCB_STACK_MODE_BELOW });

	/* if window is in this workspace and should be mapped, then  map it */
	if (!wwin->flags.miniaturized && wWindowIsVisibleOnAnyHead(wwin)
	    && !wwin->flags.hidden && !withdraw) {

		/* The following "if" is to avoid crashing of clients that expect
		 * WM_STATE set before they get mapped. Else WM_STATE is set later,
		 * after the return from this function. */
		if (wwin->wm_hints && (wwin->wm_hints->flags & XCB_ICCCM_WM_HINT_STATE))
			wClientSetState(wwin, wwin->wm_hints->initial_state, XCB_NONE);
		else
			wClientSetState(wwin, XCB_ICCCM_WM_STATE_NORMAL, XCB_NONE);

		if (w_global.config->superfluous && !w_global.config->no_animations && !scr->flags.startup &&
		    (wwin->transient_for == XCB_NONE || wwin->transient_for == scr->root_win) &&
		    /*
		     * The brain damaged idiotic non-click to focus modes will
		     * have trouble with this because:
		     *
		     * 1. window is created and mapped by the client
		     * 2. window is mapped by wmaker in small size
		     * 3. window is animated to grow to normal size
		     * 4. this function returns to normal event loop
		     * 5. eventually, the EnterNotify event that would trigger
		     * the window focusing (if the mouse is over that window)
		     * will be processed by wmaker.
		     * But since this event will be rather delayed
		     * (step 3 has a large delay) the time when the event occurred
		     * and when it is processed, the client that owns that window
		     * will reject the XSetInputFocus() for it.
		     */
		    (w_global.config->focus_mode == WKF_CLICK || w_global.config->auto_focus))
			DoWindowBirth(wwin);

		wWindowMap(wwin);
	}

	/* setup stacking descriptor */
	if (transientOwner)
		wwin->frame->core->stacking->child_of = transientOwner->frame->core;
	else
		wwin->frame->core->stacking->child_of = NULL;

	if (!scr->focused_window) {
		/* first window on the list */
		wwin->next = NULL;
		wwin->prev = NULL;
		scr->focused_window = wwin;
	} else {
		WWindow *tmp;

		/* add window at beginning of focus window list */
		tmp = scr->focused_window;
		while (tmp->prev)
			tmp = tmp->prev;

		tmp->prev = wwin;
		wwin->next = tmp;
		wwin->prev = NULL;
	}

	/* raise is set to true if we un-hid the app when this window was born.
	 * we raise, else old windows of this app will be above this new one. */
	if (raise)
		wRaiseFrame(wwin->frame->core);

	/* Update name must come after WApplication stuff is done */
	wWindowUpdateName(wwin, title);
	if (title)
		wfree(title);

	wXUngrabServer(wdisp->conn);

	/* Final preparations before window is ready to go */
	wFrameWindowChangeState(wwin->frame, WS_UNFOCUSED);

	if (!wwin->flags.miniaturized && wWindowIsVisibleOnAnyHead(wwin) && !wwin->flags.hidden) {
		if (((transientOwner && transientOwner->flags.focused)
		     || w_global.config->auto_focus) && !WFLAGP(wwin, no_focusable)) {

			/* only auto_focus if the pointer is on the same X screen,
			 * and optionally on the same Xinerama head, as the new
			 * window (AutoFocusAcrossHeads = NO for the latter) */
			short same_screen = 0, same_head = 1;

			int px, py, foo;
			unsigned int bar;
			xcb_window_t dummy;

			if (wXQueryPointer(wdisp->conn, scr->root_win, &dummy, &dummy,
					  &px, &py, &foo, &foo, &bar) != false) {
				same_screen = 1;
				if (!w_global.config->auto_focus_across_heads && scr->xine_info.count > 1)
					same_head = (wGetHeadForPoint(scr, wmkpoint(px, py))
						     == wGetHeadForWindow(wwin));
			}

			if (same_screen == 1 && same_head == 1) {
				if (wNETWMShouldStealFocus(wwin)) {
					wSetFocusTo(scr, wwin);
					warpPointerToFocusedWindow(wwin);
				} else {
					/* the new window's _NET_WM_USER_TIME
					 * is older than the screen's most
					 * recent user activity (or 0), do
					 * not steal focus and flag it so the
					 * user notices it has mapped */
					wwin->flags.net_demands_attention = 1;
					wAppBounceWhileUrgent(wApplicationOf(wwin->main_window));
					/* a stale launch must not cover the window
					 * the user is working in, keep it directly
					 * below the current focus */
					if (wwin->net_user_time_set && wwin->net_user_time != 0
					    && scr->focused_window && scr->focused_window->frame
					    && scr->focused_window != wwin
					    && WINDOW_LEVEL(scr->focused_window) == WINDOW_LEVEL(wwin))
						MoveInStackListUnder(scr->focused_window->frame->core,
								     wwin->frame->core);
				}
			}
		}
	}
	/* the frame only exists past creation above, so manage-time
	 * urgency (UrgencyHint or a denied map) arms here */
	wWindowUrgentBlink(wwin);
	wWindowResetMouseGrabs(wwin);

	if (!WFLAGP(wwin, no_bind_keys))
		wWindowSetKeyGrabs(wwin);

	WMPostNotificationName(WMNManaged, wwin, NULL);
	wColormapInstallForWindow(scr, scr->cmap_window);

	/* Setup Notification Observers */
	WMAddNotificationObserver(appearanceObserver, wwin, WNWindowAppearanceSettingsChanged, wwin);

	/*  Cleanup temporary stuff */
	if (win_state)
		wWindowDeleteSavedState(win_state);

	/* If the window must be withdrawed, then do it now. */
	if (withdraw) {
		wwin->flags.mapped = 0;
		wClientSetState(wwin, XCB_ICCCM_WM_STATE_WITHDRAWN, XCB_NONE);
		wUnmanageWindow(wwin, true, false);
		wwin = NULL;
	}

	/* try to swallow this window into a parent terminal */
	if (wwin)
		wWindowTrySwallow(wwin);

	if (wwin && wwin->frame)
		wLayoutApply(scr, WWIN_PRIMARY_WS(wwin));

	/* Initial static- and dynamic-rule decoration apply (including the
	 * first wWindowReapplyDecorativeAttrs() from wWindowUpdateName)
	 * is done. wWindowConfigureBorders() can now apply the
	 * top_width-compensation normally on subsequent decoration
	 * changes (interactive toggle, rule reload on title change). */
	if (wwin)
		wwin->flags.initializing = 0;
	return wwin;
}

WWindow *wManageInternalWindow(WScreen *scr, xcb_window_t window, xcb_window_t owner,
			       const char *title, int x, int y, int width, int height)
{
	WWindow *wwin;
	int foo;
	int il, ir, it, ib;

	wwin = wWindowCreate();
	wwin->creation_id = ++scr->last_creation_id;

	WMAddNotificationObserver(appearanceObserver, wwin, WNWindowAppearanceSettingsChanged, wwin);

	wwin->flags.internal_window = 1;

	wwin->client_flags.omnipresent = 1;
	wwin->client_flags.no_shadeable = 1;
	wwin->client_flags.no_resizable = 1;
	wwin->client_flags.no_miniaturizable = 1;

	wwin->focus_mode = WFM_PASSIVE;
	wwin->client_win = window;
	/* panels keep input focus on this window while open and need key
	 * events here for handleKeyPress() to redispatch into the WINGs
	 * tree. Must live in event_mask because wWindowMap()/
	 * wWindowUnmap() reselect client_win from it */
	wwin->event_mask = XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE;
	wwin->screen_ptr = scr;
	wwin->transient_for = owner;
	wwin->client.x = x;
	wwin->client.y = y;
	wwin->client.width = width;
	wwin->client.height = height;
	wwin->frame_x = wwin->client.x;
	wwin->frame_y = wwin->client.y;

	foo = WFF_CLOSE_BUTTON | WFF_BORDER;
	foo |= WFF_TITLEBAR;
	foo |= WFF_IS_CLIENT_WINDOW_FRAME;
#ifdef XKB_BUTTON_HINT
	if (w_global.config->modelock)
		foo |= WFF_LANGUAGE_BUTTON;
#endif

	wFrameInnerBorderWidths(wwin, &il, &ir, &it, &ib);
	wwin->frame = wFrameWindowCreate(scr, WMFloatingLevel,
					 wwin->frame_x, wwin->frame_y,
					 width + il + ir, height + it + ib,
					 foo,
					 scr->window_title_texture,
					 scr->resizebar_texture, scr->window_title_color, &scr->title_font,
					 scr->w_depth, scr->w_visual, scr->w_colormap);

	wwin->frame->child = wwin;
	wWindowConfigureBorders(wwin);

	WMHashInsert(wdisp->context.client_win, (void *)(uintptr_t) window, &wwin->client_descriptor);

	wwin->frame->flags.justification = w_global.config->title_justification;

	wFrameWindowChangeTitle(wwin->frame, title);

	/* setup button images */
	wWindowUpdateButtonImages(wwin);

	/* hide buttons */
	wFrameWindowHideButton(wwin->frame, WFF_CLOSE_BUTTON);

	wwin->frame->workspace = wwin->screen_ptr->current_workspace;
	wwin->frame->ws_mask = WS_ALL;

#ifdef XKB_BUTTON_HINT
	if (w_global.config->modelock)
		wwin->frame->on_click_button[WTB_LANGUAGE] = windowLanguageClick;
#endif

	wwin->frame->on_click_button[WTB_CLOSE] = windowCloseClick;
	wwin->frame->on_mousedown_titlebar = titlebarMouseDown;
	wwin->frame->on_dblclick_titlebar = titlebarDblClick;
	wwin->frame->on_mousedown_resizebar = resizebarMouseDown;
	wwin->client.y += wwin->frame->top_width;

	wFrameInnerBorderWidths(wwin, &il, NULL, &it, NULL);
	xcb_reparent_window(wdisp->conn, wwin->client_win, wwin->frame->core->window, il, wwin->frame->top_width + it);
	wWindowConfigure(wwin, wwin->frame_x, wwin->frame_y, wwin->client.width, wwin->client.height);

	/* setup the frame descriptor */
	wwin->frame->core->descriptor.handle_mousedown = frameMouseDown;
	wwin->frame->core->descriptor.parent = wwin;
	wwin->frame->core->descriptor.parent_type = WCLASS_WINDOW;

	/* server-side push-below of the fresh frame before the client is
	 * reparented and mapped, exemption: same as the wManageWindow
	 * path above, the stacking list is not reordered by this */
	xcb_configure_window(wdisp->conn, window, XCB_CONFIG_WINDOW_STACK_MODE, (const uint32_t[]){ XCB_STACK_MODE_BELOW });
	xcb_map_subwindows(wdisp->conn, wwin->frame->core->window);

	/* setup stacking descriptor */
	if (wwin->transient_for != XCB_NONE && wwin->transient_for != scr->root_win) {
		WWindow *tmp;
		tmp = wWindowFor(wwin->transient_for);
		if (tmp)
			wwin->frame->core->stacking->child_of = tmp->frame->core;
	} else {
		wwin->frame->core->stacking->child_of = NULL;
	}

	if (!scr->focused_window) {
		/* first window on the list */
		wwin->next = NULL;
		wwin->prev = NULL;
		scr->focused_window = wwin;
	} else {
		WWindow *tmp;

		/* add window at beginning of focus window list */
		tmp = scr->focused_window;
		while (tmp->prev)
			tmp = tmp->prev;
		tmp->prev = wwin;
		wwin->next = tmp;
		wwin->prev = NULL;
	}

	if (wwin->flags.is_gnustep == 0)
		wFrameWindowChangeState(wwin->frame, WS_UNFOCUSED);

	/*    if (w_global.config->auto_focus) */
	wSetFocusTo(scr, wwin);
	wWindowResetMouseGrabs(wwin);
	wWindowSetKeyGrabs(wwin);

	return wwin;
}

/*
 *----------------------------------------------------------------------
 * wUnmanageWindow--
 * 	Removes the frame window from a window and destroys all data
 * related to it. The window will be reparented back to the root window
 * if restore is True.
 *
 * Side effects:
 * 	Everything related to the window is destroyed and the window
 * is removed from the window lists. Focus is set to the previous on the
 * window list.
 *----------------------------------------------------------------------
 */
void wUnmanageWindow(WWindow *wwin, bool restore, bool destroyed)
{
	WCoreWindow *frame = wwin->frame->core;
	WWindow *owner = NULL;
	WWindow *newFocusedWindow = NULL;
	int wasFocused;
	WScreen *scr = wwin->screen_ptr;
	int wwin_workspace = WWIN_PRIMARY_WS(wwin);

	/* if this window was swallowed by a terminal, restore the terminal */
	wWindowUnswallow(wwin);

	/* if this window was itself swallowing a child, drop the link so the
	 * child does not keep a dangling swallower pointer */
	if (wwin->swallowed) {
		wwin->swallowed->swallower = NULL;
		wwin->swallowed = NULL;
	}

	/* First close attribute editor window if open */
	if (wwin->flags.inspector_open)
		wCloseInspectorForWindow(wwin);

	/* Close window menu if it's open for this window */
	if (wwin->flags.menu_open_for_me)
		CloseWindowMenu(scr);

	/* Don't restore focus to this window after a window exits
	 * fullscreen mode */
	if (scr->bfs_focused_window == wwin)
		scr->bfs_focused_window = NULL;

	/* remove from tab group so the group can reveal its next member,
	 * the leaving window itself is never reconfigured or remapped */
	if (wwin->tab_group)
		wGroupUnmanageMember(wwin, destroyed);
	/* drop from any pending session-restore slot so a late-arriving
	 * peer is not attached to a freed WWindow */
	wGroupRestoreForget(wwin);
	/* the pointer-pick attach may be waiting on the user with this
	 * window as its payload, clear the latch so the pick cancels
	 * cleanly instead of attaching a dead client */
	if (scr->tab_pick_payload == wwin)
		scr->tab_pick_payload = NULL;
	/* a tab drag may be in progress with this window as either the
	 * payload being dragged or the source frame the drag started from,
	 * clear the latch so the drag loop aborts cleanly */
	if (scr->tab_drag_payload == wwin)
		scr->tab_drag_payload = NULL;
	if (scr->tab_drag_source == wwin)
		scr->tab_drag_source = NULL;

	if (!destroyed) {
		if (!wwin->flags.internal_window)
			xcb_change_save_set(wdisp->conn, XCB_SET_MODE_DELETE, wwin->client_win);

		/* If this is a leader window, we still need to listen for
		 * DestroyNotify and PropertyNotify. */
		if (wApplicationOf(wwin->client_win))
			xcb_change_window_attributes(wdisp->conn, wwin->client_win, XCB_CW_EVENT_MASK, (const uint32_t[]){ XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE });
		else
			xcb_change_window_attributes(wdisp->conn, wwin->client_win, XCB_CW_EVENT_MASK, (const uint32_t[]){ XCB_EVENT_MASK_NO_EVENT });

		wXUngrabButton(wdisp->conn, XCB_BUTTON_INDEX_ANY, XCB_MOD_MASK_ANY, wwin->client_win);
		wXUngrabKey(wdisp->conn, XCB_GRAB_ANY, XCB_MOD_MASK_ANY, wwin->client_win);
	}

	xcb_unmap_window(wdisp->conn, frame->window);

	xcb_unmap_window(wdisp->conn, wwin->client_win);

	/* deselect window */
	wSelectWindow(wwin, false);

	/* remove all pending events on window */
	/* I think this only matters for autoraise */
	if (w_global.config->raise_delay)
		WMDeleteTimerWithClientData(wwin->frame->core);

	wXFlush(wdisp->conn);

	/* reparent the window back to the root */
	if (restore)
		wClientRestore(wwin);

	if (wwin->transient_for != scr->root_win) {
		owner = wWindowFor(wwin->transient_for);
		if (owner) {
			if (!owner->flags.semi_focused)
				owner = NULL;
			else
				owner->flags.semi_focused = 0;
		}
	}

	wasFocused = wwin->flags.focused;

	/* remove from window focus list */
	if (!wwin->prev && !wwin->next) {
		/* was the only window */
		scr->focused_window = NULL;
		newFocusedWindow = NULL;
	} else {
		WWindow *tmp;

		if (wwin->prev)
			wwin->prev->next = wwin->next;

		if (wwin->next)
			wwin->next->prev = wwin->prev;
		else {
			scr->focused_window = wwin->prev;
			scr->focused_window->next = NULL;
		}

		if (w_global.config->focus_mode == WKF_CLICK) {
			/* A focused transient hands focus back to its
			 * owner. Otherwise the next window in the focus
			 * list takes over, never the root. */
			tmp = wWindowFor(wwin->transient_for);
			if (tmp && (!tmp->flags.mapped || WFLAGP(tmp, no_focusable)))
				tmp = NULL;
			if (!tmp) {
				/* first pass skips windows hidden from the
				 * window list, the second accepts any
				 * focusable mapped window */
				tmp = scr->focused_window;
				while (tmp) {
					if (!WFLAGP(tmp, no_focusable) && !WFLAGP(tmp, skip_window_list)
					    && (tmp->flags.mapped || tmp->flags.shaded))
						break;
					tmp = tmp->prev;
				}
				if (!tmp) {
					tmp = scr->focused_window;
					while (tmp) {
						if (!WFLAGP(tmp, no_focusable)
						    && (tmp->flags.mapped || tmp->flags.shaded))
							break;
						tmp = tmp->prev;
					}
				}
			}
			newFocusedWindow = tmp;

		} else if (w_global.config->focus_mode == WKF_SLOPPY) {
			unsigned int mask;
			int foo;
			xcb_window_t bar, win;

			/*  This is to let the root window get the keyboard input
			 * if Sloppy focus mode and no other window get focus.
			 * This way keybindings will not freeze.
			 */
			tmp = NULL;
			if (wXQueryPointer(wdisp->conn, scr->root_win, &bar, &win, &foo, &foo, &foo, &foo, &mask))
				tmp = wWindowFor(win);
			if (tmp == wwin)
				tmp = NULL;
			newFocusedWindow = tmp;
		} else {
			newFocusedWindow = NULL;
		}
	}

	if (!wwin->flags.internal_window)
		WMPostNotificationName(WMNUnmanaged, wwin, NULL);
	if (wasFocused) {
		if (newFocusedWindow != owner && owner) {
			if (wwin->flags.is_gnustep == 0)
				wFrameWindowChangeState(owner->frame, WS_UNFOCUSED);
		}
		/* internal windows are wmaker-owned panels, their close
		 * must not pull focus to another client. Callers that
		 * want a specific successor set it themselves */
		wSetFocusTo(scr, wwin->flags.internal_window ? NULL : newFocusedWindow);
		if (!wwin->flags.internal_window && w_global.config->focus_mode == WKF_CLICK && newFocusedWindow)
			wRaiseFrame(newFocusedWindow->frame->core);
		if (!wwin->flags.internal_window)
			warpPointerToFocusedWindow(newFocusedWindow);
	}

	/* Close menu and unhighlight */
	WApplication *oapp = wApplicationOf(wwin->main_window);
	WApplication *napp = scr->focused_window ? wApplicationOf(scr->focused_window->main_window) : NULL;
	if (oapp && oapp != napp) {
		wAppMenuUnmap(oapp->menu);
		if (w_global.config->highlight_active_app)
			wApplicationDeactivate(oapp);
	}

	wNETCleanupFrameExtents(wwin);
	wNETWMShowingDesktopForgetWindow(scr, wwin);

	/* Re-queue saved state for session-restored iconified windows so a
	 * replacement window the client maps during startup also gets
	 * iconified. Startup-only: the queue exists for the replacement
	 * window of the startup adopt, a runtime close must not arm
	 * force-iconify plus the restore lock for the next launch of the
	 * command. Cleaned up by wWindowDeleteSavedStatesForPID on exit. */
	if (scr->flags.startup && wwin->flags.restore_iconified && wwin->wm_command) {
		WSavedState *state = wmalloc(sizeof(WSavedState));

		state->workspace = -1;
		state->miniaturized = 1;
		wWindowAddSavedState(wwin->wm_instance, wwin->wm_class,
				     wwin->wm_command, wwin->client_pid, state);
	}

	wWindowDestroy(wwin);
	wLayoutApply(scr, wwin_workspace);
	wAutoRemoveEmptyWorkspaces(scr);
	wXFlush(wdisp->conn);
}

void wWindowMap(WWindow *wwin)
{
	xcb_map_window(wdisp->conn, wwin->frame->core->window);
	if (!wwin->flags.shaded) {
		/* window will be remapped when getting MapNotify */
		xcb_change_window_attributes(wdisp->conn, wwin->client_win, XCB_CW_EVENT_MASK, (const uint32_t[]){ wwin->event_mask & ~XCB_EVENT_MASK_STRUCTURE_NOTIFY });
		xcb_map_window(wdisp->conn, wwin->client_win);
		xcb_change_window_attributes(wdisp->conn, wwin->client_win, XCB_CW_EVENT_MASK, (const uint32_t[]){ wwin->event_mask });

		wwin->flags.mapped = 1;
	}
}

void wWindowUnmap(WWindow *wwin)
{
	wwin->flags.mapped = 0;

	/* prevent window withdrawal when getting UnmapNotify */
	xcb_change_window_attributes(wdisp->conn, wwin->client_win, XCB_CW_EVENT_MASK, (const uint32_t[]){ wwin->event_mask & ~XCB_EVENT_MASK_STRUCTURE_NOTIFY });
	xcb_unmap_window(wdisp->conn, wwin->client_win);
	xcb_change_window_attributes(wdisp->conn, wwin->client_win, XCB_CW_EVENT_MASK, (const uint32_t[]){ wwin->event_mask });

	xcb_unmap_window(wdisp->conn, wwin->frame->core->window);
}

void wWindowSingleFocus(WWindow *wwin)
{
	WScreen *scr;
	int x, y, move = 0;

	if (!wwin)
		return;

	/*
	 * If the requested window is a non-active member of a tab group, switch
	 * to its tab first so that the frame is mapped before we try to focus it.
	 */
	if (wwin->tab_group && wwin->tab_group_index != wwin->tab_group->active_index)
		wGroupSetActive(wwin->tab_group, wwin->tab_group_index);

	scr = wwin->screen_ptr;
	wMakeWindowVisible(wwin);

	x = wwin->frame_x;
	y = wwin->frame_y;

	/* bring window back to visible area */
	move = wScreenBringInside(scr, &x, &y, wwin->frame->core->width, wwin->frame->core->height);

	if (move)
		wWindowConfigure(wwin, x, y, wwin->client.width, wwin->client.height);
}

void wWindowFocusPrev(WWindow *wwin, bool inSameWorkspace)
{
	WWindow *tmp;

	if (!wwin || !wwin->prev)
		return;

	tmp = wwin;
	while (tmp->prev)
		tmp = tmp->prev;

	if (inSameWorkspace)
		while (tmp && (WWIN_PRIMARY_WS(tmp) != WWIN_PRIMARY_WS(wwin)))
			tmp = tmp->next;

	wWindowSingleFocus(tmp);
}

void wWindowFocusNext(WWindow *wwin, bool inSameWorkspace)
{
	WWindow *tmp;

	if (!wwin || !wwin->prev)
		return;

	tmp = wwin->prev;
	if (inSameWorkspace)
		while (tmp && (WWIN_PRIMARY_WS(tmp) != WWIN_PRIMARY_WS(wwin)))
			tmp = tmp->prev;

	wWindowSingleFocus(tmp);
}

void wWindowFocus(WWindow *wwin, WWindow *owin)
{
	WWindow *nowner;
	WWindow *oowner;

#ifdef KEEP_XKB_LOCK_STATUS
	if (w_global.config->modelock)
		wXkbLockGroup(wwin->frame->languagemode);
#endif /* KEEP_XKB_LOCK_STATUS */

	wwin->flags.semi_focused = 0;

	if (wwin->flags.is_gnustep == 0)
		wFrameWindowChangeState(wwin->frame, WS_FOCUSED);

	wwin->flags.focused = 1;
	/* the window now has the user's attention, drop the EWMH demand
	 * so the bounce stops and pagers clear their highlight */
	if (wwin->flags.net_demands_attention) {
		wwin->flags.net_demands_attention = 0;
		wAppBounceWhileUrgent(wApplicationOf(wwin->main_window));
	}
	wWindowUrgentBlink(wwin);

	wWindowResetMouseGrabs(wwin);

	WMPostNotificationName(WMNChangedFocus, wwin, (void *)true);

	if (owin == wwin || !owin)
		return;

	nowner = wWindowFor(wwin->transient_for);

	/* new window is a transient for the old window */
	if (nowner == owin) {
		owin->flags.semi_focused = 1;
		wWindowUnfocus(nowner);
		return;
	}

	oowner = wWindowFor(owin->transient_for);

	/* new window is owner of old window */
	if (wwin == oowner) {
		wWindowUnfocus(owin);
		return;
	}

	if (!nowner) {
		wWindowUnfocus(owin);
		return;
	}

	/* new window has same owner of old window */
	if (oowner == nowner) {
		/* prevent unfocusing of owner */
		oowner->flags.semi_focused = 0;
		wWindowUnfocus(owin);
		oowner->flags.semi_focused = 1;

		return;
	}

	/* nowner != NULL && oowner != nowner */
	nowner->flags.semi_focused = 1;
	wWindowUnfocus(nowner);
	wWindowUnfocus(owin);
}

void wWindowUnfocus(WWindow *wwin)
{
	bool was_focused = wwin->flags.focused;

	CloseWindowMenu(wwin->screen_ptr);

	if (wwin->flags.is_gnustep == 0)
		wFrameWindowChangeState(wwin->frame, wwin->flags.semi_focused ? WS_PFOCUSED : WS_UNFOCUSED);

	if (wwin->transient_for != XCB_NONE && wwin->transient_for != wwin->screen_ptr->root_win) {
		WWindow *owner;
		owner = wWindowFor(wwin->transient_for);
		if (owner && owner->flags.semi_focused) {
			xcb_window_t oclient = owner->client_win;

			owner->flags.semi_focused = 0;
			if (owner->flags.mapped || owner->flags.shaded) {
				wWindowUnfocus(owner);
				if (!wWindowFor(oclient))
					return;
				wFrameWindowPaint(owner->frame);
			}
		}
	}
	wwin->flags.focused = 0;
	wWindowResetMouseGrabs(wwin);
	WMPostNotificationName(WMNChangedFocus, wwin, (void *)false);

	/* Only fire for a window that actually held focus, not transient
	 * owners unfocused as a side effect or a stale focused_window slot.
	 * Exclude windows with an open inspector so focusing the panel does
	 * not hide/miniaturize the inspected window mid-edit, the repair
	 * burst's own focus steal (w_global.restore_focus_repair), and
	 * skip when focus is moving to an internal window (dialog,
	 * inspector, docked panel) so opening one does not dismiss the
	 * window. By the time wWindowUnfocus() runs, scr->focused_window
	 * already holds the new target. */
	if (was_focused && !wwin->flags.miniaturized && !wwin->flags.hidden &&
	    !wwin->flags.semi_focused && wwin->flags.mapped &&
	    !wwin->flags.inspector_open &&
	    !w_global.restore_focus_repair &&
	    !(wwin->screen_ptr->focused_window &&
	      wwin->screen_ptr->focused_window->flags.internal_window)) {
		/* AutoHideOnUnfocus wins over AutoMiniaturizeOnUnfocus: hide is
		 * a distinct primitive and the user asking for it wants the
		 * window off-screen regardless of miniaturizability. NoMiniaturizable
		 * (per-class) still vetoes the miniaturize branch, mirroring the
		 * StartMiniaturized precedent at wManageWindow(). */
		if (WFLAGP(wwin, auto_hide_on_unfocus))
			wHideWindow(wwin, NULL, 0, 0, 0);
		else if (WFLAGP(wwin, auto_miniaturize_on_unfocus) &&
			 !WFLAGP(wwin, no_miniaturizable)) {
			xcb_window_t client = wwin->client_win;

			wIconifyWindow(wwin);
			if (!wWindowFor(client))
				return;
		}
	}
	wWindowUrgentBlink(wwin);
}

/*
 * Urgent titlebar blink.
 */

#define URGENT_BLINK_DELAY 500

static int wWindowUrgentBlinkable(WWindow *wwin)
{
	if (!wwin || !wwin->screen_ptr)
		return 0;
	if (!(wwin->flags.urgent || wwin->flags.net_demands_attention))
		return 0;
	if (wwin->flags.focused || wwin->flags.miniaturized || wwin->flags.hidden)
		return 0;
	if (wwin->flags.is_gnustep)
		return 0;
	if (!wwin->frame || !wwin->frame->flags.is_client_window_frame
	    || !wwin->frame->titlebar)
		return 0;
	if (!w_global.config->blink_titlebar_when_urgent)
		return 0;
	if (w_global.display_off)
		return 0;
	return 1;
}

static void wWindowUrgentBlinkTimer(void *arg)
{
	WWindow *wwin = arg;

	if (w_global.display_off) {
		/* wDisplaySetOff(False) re-arms when the output comes
		 * back, mirrors doAppUrgentBounce() */
		WMDeleteTimerHandlerPtr(&wwin->urgent_blink_timer);
		wFrameWindowUrgentBlink(wwin->frame, 0);
		return;
	}
	if (!wWindowUrgentBlinkable(wwin)) {
		WMDeleteTimerHandlerPtr(&wwin->urgent_blink_timer);
		wFrameWindowUrgentBlink(wwin->frame, 0);
		return;
	}
	wFrameWindowBlinkTick(wwin->frame);
}

void wWindowUrgentBlink(WWindow *wwin)
{
	if (wWindowUrgentBlinkable(wwin)) {
		if (!wwin->urgent_blink_timer) {
			wFrameWindowUrgentBlink(wwin->frame, 1);
			wwin->urgent_blink_timer =
			    WMAddPersistentTimerHandler(URGENT_BLINK_DELAY,
							wWindowUrgentBlinkTimer,
							wwin);
		}
	} else {
		WMDeleteTimerHandlerPtr(&wwin->urgent_blink_timer);
		if (wwin->frame)
			wFrameWindowUrgentBlink(wwin->frame, 0);
	}
}

/* Re-apply decoration-affecting attributes when a dynamic-rule predicate
 * source changes. Computes two merges so attributes previously applied
 * by a now-non-matching dynamic rule revert to their non-rule baseline:
 *  - base_attr/base_mask: four-level merge without the regex layer and
 *    without the global default (base_mask.flag is 1 only when a per-app
 *    entry actually set that field).
 *  - final_attr/final_mask: full merge with the regex layer but without
 *    the global default.
 * The regex contribution this pass is, per field,
 * final_mask.flag && !base_mask.flag. A field is touched only when the
 * regex layer is currently setting it (regex_mask.flag) or previously
 * set it and must revert (old_applied.flag). When reverting, if a per-app
 * entry also set the field, its value is restored via WSETUFLAG. Otherwise
 * the user flag is undefined so the client's own hints take over. Fields
 * in neither mask are left untouched. The runtime pattern mirrors
 * src/winspector.c applySettings(): WSETUFLAG the
 * user_flags/defined_user_flags pair, run per-field side effects, then a
 * single wWindowConfigureBorders()+wFrameWindowPaint()+wNETRMUpdateActions()
 * to rebuild decoration. */
static void wWindowReapplyDecorativeAttrs(WWindow *wwin)
{
	WWindowAttributes base_attr, base_mask;
	WWindowAttributes final_attr, final_mask;
	WWindowAttributes regex_mask;
	WWindowAttributes old_applied;
	bool old_omnipresent, old_skip_window_list, old_no_bind_keys, old_no_bind_mouse;
	int old_inner_width, old_inner_set;
	unsigned int old_inner_sides;
	int old_inner_sides_set;

	if (!wwin->frame)
		return;

	if (!wDefaultHasDynamicRules()) {
		static const WWindowAttributes zero;
		WWindowAttributes *old = &wwin->regex_applied_mask;

		if (memcmp(old, &zero, sizeof(WWindowAttributes)) == 0)
			return;
	}

	memset(&base_attr, 0, sizeof(base_attr));
	memset(&base_mask, 0, sizeof(base_mask));
	wDefaultFillAttributes(wwin->wm_instance, wwin->wm_class,
			       &base_attr, &base_mask, false);

	memset(&final_attr, 0, sizeof(final_attr));
	memset(&final_mask, 0, sizeof(final_mask));
	wDefaultFillAttributesForWindow(wwin, &final_attr, &final_mask, false);

	memset(&regex_mask, 0, sizeof(regex_mask));
#define REGEX_CONTRIB(flag) \
	regex_mask.flag = (final_mask.flag && !base_mask.flag) ? 1 : 0
	REGEX_CONTRIB(no_titlebar);
	REGEX_CONTRIB(no_resizebar);
	REGEX_CONTRIB(no_close_button);
	REGEX_CONTRIB(no_miniaturize_button);
	REGEX_CONTRIB(no_border);
	REGEX_CONTRIB(floating);
	REGEX_CONTRIB(sunken);
	REGEX_CONTRIB(omnipresent);
	REGEX_CONTRIB(skip_window_list);
	REGEX_CONTRIB(skip_switchpanel);
	REGEX_CONTRIB(dont_move_off);
	REGEX_CONTRIB(no_focusable);
	REGEX_CONTRIB(no_bind_mouse);
	REGEX_CONTRIB(no_bind_keys);
	REGEX_CONTRIB(no_appicon);
	REGEX_CONTRIB(shared_appicon);
	REGEX_CONTRIB(no_tabbing);
	REGEX_CONTRIB(no_tiling);
#undef REGEX_CONTRIB

	old_applied = wwin->regex_applied_mask;

	/* Dynamic rules matched at map time take precedence over the
	 * session-restored no_* flags. */
#define REAPPLY_FIELD(flag) \
	do { \
		if (regex_mask.flag) { \
			WSETUFLAG(wwin, flag, final_attr.flag); \
			wwin->domain_flags.flag = 1; \
		} else if (old_applied.flag) { \
			if (base_mask.flag) { \
				WSETUFLAG(wwin, flag, base_attr.flag); \
				wwin->domain_flags.flag = 1; \
			} else { \
				wwin->defined_user_flags.flag = 0; \
				wwin->domain_flags.flag = 0; \
			} \
		} \
	} while (0)
	REAPPLY_FIELD(no_close_button);
	REAPPLY_FIELD(no_miniaturize_button);
	REAPPLY_FIELD(no_border);
	REAPPLY_FIELD(no_resizebar);
	REAPPLY_FIELD(no_titlebar);
	REAPPLY_FIELD(skip_switchpanel);
	REAPPLY_FIELD(dont_move_off);
	REAPPLY_FIELD(no_focusable);
	REAPPLY_FIELD(no_appicon);
	REAPPLY_FIELD(shared_appicon);
	REAPPLY_FIELD(no_tabbing);
	REAPPLY_FIELD(no_tiling);
#undef REAPPLY_FIELD

	/* no_titlebar has a dependent side effect: a shaded window cannot
	 * lose its titlebar (the no_shadeable invariant enforced at
	 * manage time). */
	if (regex_mask.no_titlebar && final_attr.no_titlebar && wwin->flags.shaded)
		wUnshadeWindow(wwin);

	/* floating/sunken are a 3-way enum (Normal/Floating/Sunken), not
	 * independent bools. Evaluate them as a unit and call
	 * ChangeStackingLevel() once. */
	if (regex_mask.floating || regex_mask.sunken ||
	    old_applied.floating || old_applied.sunken) {
		bool use_final = regex_mask.floating || regex_mask.sunken;
		const WWindowAttributes *src = use_final ? &final_attr : &base_attr;
		int layer;

		if (src->floating)
			layer = WMFloatingLevel;
		else if (src->sunken)
			layer = WMSunkenLevel;
		else
			layer = WMNormalLevel;
		if (use_final || base_mask.floating || base_mask.sunken) {
			WSETUFLAG(wwin, floating, src->floating);
			WSETUFLAG(wwin, sunken, src->sunken);
		} else {
			wwin->defined_user_flags.floating = 0;
			wwin->defined_user_flags.sunken = 0;
		}
		ChangeStackingLevel(wwin->frame->core, layer);
	}

	/* omnipresent: toggle the WS_ALL runtime mask and notify. */
	old_omnipresent = WFLAGP(wwin, omnipresent);
	if (regex_mask.omnipresent || old_applied.omnipresent) {
		bool v;

		if (regex_mask.omnipresent)
			v = final_attr.omnipresent;
		else if (base_mask.omnipresent)
			v = base_attr.omnipresent;
		else {
			wwin->defined_user_flags.omnipresent = 0;
			v = WFLAGP(wwin, omnipresent);
		}
		if (regex_mask.omnipresent || base_mask.omnipresent)
			WSETUFLAG(wwin, omnipresent, v);
		if (v && wwin->frame->ws_mask != WS_ALL)
			wwin->frame->ws_mask = WS_ALL;
		else if (!v && wwin->frame->ws_mask == WS_ALL)
			wwin->frame->ws_mask = WS_BIT(wwin->frame->workspace);
		if (WFLAGP(wwin, omnipresent) != old_omnipresent)
			WMPostNotificationName(WMNChangedState, wwin, "omnipresent");
	}

	/* skip_window_list: rebuild the switch menu membership. */
	old_skip_window_list = WFLAGP(wwin, skip_window_list);
	if (regex_mask.skip_window_list || old_applied.skip_window_list) {
		bool v;

		if (regex_mask.skip_window_list)
			v = final_attr.skip_window_list;
		else if (base_mask.skip_window_list)
			v = base_attr.skip_window_list;
		else {
			wwin->defined_user_flags.skip_window_list = 0;
			v = WFLAGP(wwin, skip_window_list);
		}
		if (regex_mask.skip_window_list || base_mask.skip_window_list) {
			WSETUFLAG(wwin, skip_window_list, v);
			wwin->domain_flags.skip_window_list = 1;
		}
		if (WFLAGP(wwin, skip_window_list) != old_skip_window_list) {
			UpdateSwitchMenu(wwin->screen_ptr, wwin,
					 WFLAGP(wwin, skip_window_list) ? ACTION_REMOVE
									: ACTION_ADD);
		}
	}

	/* no_bind_keys: re-grab or ungrab keys. */
	old_no_bind_keys = WFLAGP(wwin, no_bind_keys);
	if (regex_mask.no_bind_keys || old_applied.no_bind_keys) {
		bool v;

		if (regex_mask.no_bind_keys)
			v = final_attr.no_bind_keys;
		else if (base_mask.no_bind_keys)
			v = base_attr.no_bind_keys;
		else {
			wwin->defined_user_flags.no_bind_keys = 0;
			v = WFLAGP(wwin, no_bind_keys);
		}
		if (regex_mask.no_bind_keys || base_mask.no_bind_keys)
			WSETUFLAG(wwin, no_bind_keys, v);
		if (WFLAGP(wwin, no_bind_keys) != old_no_bind_keys) {
			if (WFLAGP(wwin, no_bind_keys))
				wXUngrabKey(wdisp->conn, XCB_GRAB_ANY, XCB_MOD_MASK_ANY, wwin->frame->core->window);
			else
				wWindowSetKeyGrabs(wwin);
		}
	}

	/* no_bind_mouse: re-grab mouse bindings. */
	old_no_bind_mouse = WFLAGP(wwin, no_bind_mouse);
	if (regex_mask.no_bind_mouse || old_applied.no_bind_mouse) {
		bool v;

		if (regex_mask.no_bind_mouse)
			v = final_attr.no_bind_mouse;
		else if (base_mask.no_bind_mouse)
			v = base_attr.no_bind_mouse;
		else {
			wwin->defined_user_flags.no_bind_mouse = 0;
			v = WFLAGP(wwin, no_bind_mouse);
		}
		if (regex_mask.no_bind_mouse || base_mask.no_bind_mouse)
			WSETUFLAG(wwin, no_bind_mouse, v);
		if (WFLAGP(wwin, no_bind_mouse) != old_no_bind_mouse)
			wWindowResetMouseGrabs(wwin);
	}

	/* Re-evaluate frame border overrides: release the prior resolved
	 * colors so wDefaultFillFrameBorders() can re-resolve them from the
	 * now-current predicate sources (the title changed). */
	old_inner_width = wwin->frame_border.frame_inner_border_width;
	old_inner_set = wwin->frame_border.frame_inner_border_width_set;
	old_inner_sides = wwin->frame_border.frame_inner_border_sides;
	old_inner_sides_set = wwin->frame_border.frame_inner_border_sides_set;

	if (wwin->frame_border.frame_border_color) {
		WMReleaseColor(wwin->frame_border.frame_border_color);
		wwin->frame_border.frame_border_color = NULL;
	}
	if (wwin->frame_border.frame_focused_border_color) {
		WMReleaseColor(wwin->frame_border.frame_focused_border_color);
		wwin->frame_border.frame_focused_border_color = NULL;
	}
	if (wwin->frame_border.frame_selected_border_color) {
		WMReleaseColor(wwin->frame_border.frame_selected_border_color);
		wwin->frame_border.frame_selected_border_color = NULL;
	}
	if (wwin->frame_border.frame_inner_border_color) {
		WMReleaseColor(wwin->frame_border.frame_inner_border_color);
		wwin->frame_border.frame_inner_border_color = NULL;
	}
	if (wwin->frame_border.frame_inner_focused_border_color) {
		WMReleaseColor(wwin->frame_border.frame_inner_focused_border_color);
		wwin->frame_border.frame_inner_focused_border_color = NULL;
	}
	memset(&wwin->frame_border, 0, sizeof(wwin->frame_border));
	wDefaultFillFrameBorders(wwin->screen_ptr, wwin->wm_instance,
				 wwin->wm_class, wwin);

	/* when the inner border width or sides changed, the frame geometry
	 * must be re-applied so paintInnerBorderStrips() draws the
	 * selected sides at the correct dimensions */
	if (old_inner_set != wwin->frame_border.frame_inner_border_width_set ||
	    old_inner_width != wwin->frame_border.frame_inner_border_width ||
	    old_inner_sides_set != wwin->frame_border.frame_inner_border_sides_set ||
	    old_inner_sides != wwin->frame_border.frame_inner_border_sides)
		wWindowApplyInnerBorder(wwin);

	/* Decoration flags converge on a single wWindowConfigureBorders()
	 * call which rebuilds the titlebar, buttons, resizebar and border,
	 * then repaint and sync EWMH actions. */
	wwin->frame->flags.need_texture_change = 1;
	wWindowConfigureBorders(wwin);
	wFrameWindowPaint(wwin->frame);
	wNETWMUpdateActions(wwin, false);

	wwin->regex_applied_mask = regex_mask;
}

void wWindowUpdateName(WWindow *wwin, const char *newTitle)
{
	const char *title;
	char buf[512];
	WWindow *act;

	if (!wwin->frame)
		return;

	if (!newTitle)
		title = DEF_WINDOW_TITLE; /* the hint was removed */
	else
		title = newTitle;

	/* keep the dynamic-rule title predicate source in sync with the
	 * undecorated client title before any re-evaluation runs */
	if (wwin->wm_title) {
		wfree(wwin->wm_title);
		wwin->wm_title = NULL;
	}
	wwin->wm_title = wstrdup(title);

	/* Re-apply [N/M] prefix when this window is part of a tab group and
	 * the segmented titlebar renderer is off */
	if (wwin->tab_group && wwin->tab_group->member_count > 1
	    && !w_global.config->tabs_in_titlebar) {
		snprintf(buf, sizeof(buf), "[%d/%d] %s",
		         wwin->tab_group_index + 1, wwin->tab_group->member_count, title);
		title = buf;
	}

	/* publish _NET_WM_VISIBLE_NAME when we substitute a title that
	 * differs from what the client supplied, remove it when they match */
	wNETWMUpdateVisibleName(wwin, title, newTitle);

	if (wFrameWindowChangeTitle(wwin->frame, title))
		WMPostNotificationName(WMNChangedName, wwin, NULL);

	/* the visible segmented titlebar is the active member's frame, an
	 * inactive member's title change has to repaint that one too */
	if (wGroupIsInactiveMember(wwin)) {
		act = wwin->tab_group->members[wwin->tab_group->active_index];
		if (act && act->frame)
			wFrameWindowRepaintTitle(act->frame);
	}

	/* Re-evaluate dynamic-rule attributes now that the title (the only
	 * predicate source that changes on a WM_NAME/_NET_WM_NAME update)
	 * has been committed. The other six sources are read once at manage
	 * time. Rules keyed purely on them stay constant. The first call at
	 * map time seeds regex_applied_mask from zero. */
	wWindowReapplyDecorativeAttrs(wwin);
}

/*
 *----------------------------------------------------------------------
 *
 * wWindowConstrainSize--
 * 	Constrains size for the client window, taking the maximal size,
 * window resize increments and other size hints into account.
 *
 * Returns:
 * 	The closest size to what was given that the client window can
 * have.
 *
 *----------------------------------------------------------------------
 */
void wWindowConstrainSize(WWindow *wwin, unsigned int *nwidth, unsigned int *nheight)
{
	int width = (int)*nwidth;
	int height = (int)*nheight;
	int winc = 1;
	int hinc = 1;
	int minW = 1, minH = 1;
	int maxW = wwin->screen_ptr->scr_width * 2;
	int maxH = wwin->screen_ptr->scr_height * 2;
	int minAX = -1, minAY = -1;
	int maxAX = -1, maxAY = -1;
	int baseW = 0;
	int baseH = 0;

	if (wwin->normal_hints) {
		if (!wwin->flags.maximized) {
			winc = wwin->normal_hints->width_inc;
			hinc = wwin->normal_hints->height_inc;
		}
		if (wwin->normal_hints->min_width > minW)
			minW = wwin->normal_hints->min_width;
		if (wwin->normal_hints->min_height > minH)
			minH = wwin->normal_hints->min_height;
		if (wwin->normal_hints->max_width < maxW)
			maxW = wwin->normal_hints->max_width;
		if (wwin->normal_hints->max_height < maxH)
			maxH = wwin->normal_hints->max_height;

		if (wwin->normal_hints->flags & XCB_ICCCM_SIZE_HINT_P_ASPECT) {
			minAX = wwin->normal_hints->min_aspect_num;
			minAY = wwin->normal_hints->min_aspect_den;
			maxAX = wwin->normal_hints->max_aspect_num;
			maxAY = wwin->normal_hints->max_aspect_den;
		}

		baseW = wwin->normal_hints->base_width;
		baseH = wwin->normal_hints->base_height;
	}

	/* trust the mins provided by the client but not the maxs */
	if (width < minW)
		width = minW;
	if (height < minH)
		height = minH;

	/* if only one dimension is over the top, set a default 4/3 ratio */
	if (width > maxW && height < maxH)
		width = height * 4 / 3;
	else if(height > maxH && width < maxW)
		height = width * 3 / 4;
	else if(width > maxW && height > maxH) {
		/* if both are over the top, set size to almost fullscreen */
		height = wwin->screen_ptr->scr_height - 2 * w_global.config->icon_size;
		width = wwin->screen_ptr->scr_width - 2 * w_global.config->icon_size;
	}

	/* aspect ratio code borrowed from olwm */
	if (minAX > 0) {
		/* adjust max aspect ratio */
		if (!(maxAX == 1 && maxAY == 1) && width * maxAY > height * maxAX) {
			if (maxAX > maxAY) {
				height = (width * maxAY) / maxAX;
				if (height > maxH) {
					height = maxH;
					width = (height * maxAX) / maxAY;
				}
			} else {
				width = (height * maxAX) / maxAY;
				if (width > maxW) {
					width = maxW;
					height = (width * maxAY) / maxAX;
				}
			}
		}

		/* adjust min aspect ratio */
		if (!(minAX == 1 && minAY == 1) && width * minAY < height * minAX) {
			if (minAX > minAY) {
				height = (width * minAY) / minAX;
				if (height < minH) {
					height = minH;
					width = (height * minAX) / minAY;
				}
			} else {
				width = (height * minAX) / minAY;
				if (width < minW) {
					width = minW;
					height = (width * minAY) / minAX;
				}
			}
		}
	}

	if (!wwin->flags.maximized) {
		if (baseW != 0)
			width = (((width - baseW) / winc) * winc) + baseW;
		else
			width = (((width - minW) / winc) * winc) + minW;

		if (baseH != 0)
			height = (((height - baseH) / hinc) * hinc) + baseH;
		else
			height = (((height - minH) / hinc) * hinc) + minH;
	}

	/* misbehaving clients may cause preposterous values for these */
	if (width > 0)
		*nwidth = width;
	if (height > 0)
		*nheight = height;
}

void wWindowCropSize(WWindow *wwin, unsigned int maxW, unsigned int maxH,
		     unsigned int *width, unsigned int *height)
{
	int baseW = 0, baseH = 0;
	int winc = 1, hinc = 1;

	if (wwin->normal_hints) {
		baseW = wwin->normal_hints->base_width;
		baseH = wwin->normal_hints->base_height;

		winc = wwin->normal_hints->width_inc;
		hinc = wwin->normal_hints->height_inc;
	}

	if (*width > maxW)
		*width = maxW - (maxW - baseW) % winc;

	if (*height > maxH)
		*height = maxH - (maxH - baseH) % hinc;
}

/* Translate the frame from one head's rectangle into the same relative
 * position within another head's rectangle. wWindowConfigure() refreshes
 * wwin->last_head from the new geometry, so the caller does not need to. */
static void translateWindowToHead(WWindow *wwin, int from_head, int to_head)
{
	WScreen *scr = wwin->screen_ptr;
	WMRect from = wGetRectForHead(scr, from_head);
	WMRect to = wGetRectForHead(scr, to_head);
	int x, y;

	x = to.pos.x + (from.size.width > 0
			? (wwin->frame_x - from.pos.x) * to.size.width / from.size.width
			: 0);
	y = to.pos.y + (from.size.height > 0
			? (wwin->frame_y - from.pos.y) * to.size.height / from.size.height
			: 0);
	wWindowConfigure(wwin, x, y, wwin->client.width, wwin->client.height);
}

void wWindowChangeWorkspace(WWindow *wwin, int workspace)
{
	WScreen *scr = wwin->screen_ptr;
	WApplication *wapp;
	int unmap = 0;
	int src_workspace = WWIN_PRIMARY_WS(wwin);

	if (workspace >= scr->workspace_count || workspace < 0 || workspace == WWIN_PRIMARY_WS(wwin))
		return;

	if (w_global.config->per_head_workspaces) {
		int oldWorkspace = WWIN_PRIMARY_WS(wwin);
		bool was_visible = wWindowIsVisibleOnAnyHead(wwin);
		bool now_visible;

		if (!IS_OMNIPRESENT(wwin)) {
			wwin->frame->workspace = workspace;
			wwin->frame->ws_mask = WS_BIT(workspace);

			/* The absolute, directional, last-workspace and layer
			 * move-window keybinds, the window-menu Move To
			 * submenu and a client _NET_WM_DESKTOP request can all
			 * name a workspace pinned to another output, so the
			 * window's effective head no longer agrees with where
			 * that workspace can ever be shown. Carry the frame
			 * onto the head that is showing or allowed to show it,
			 * otherwise wWindowIsVisibleOnAnyHead() stays False
			 * for every per-head switch and the window is lost.
			 * Skip while changing_workspace is set so an opaque
			 * edge-scroll drag does not have the frame warped out
			 * from under the grabbed pointer. */
			if (!wwin->flags.changing_workspace
			    && !wWorkspaceIsAllowedOnHead(scr, workspace, wwin->last_head)) {
				int target = wHeadShowingWorkspace(scr, workspace);

				if (target != wwin->last_head
				    && wWorkspaceIsAllowedOnHead(scr, workspace, target))
					translateWindowToHead(wwin, wwin->last_head, target);
			}
		}
		now_visible = wWindowIsVisibleOnAnyHead(wwin);

		if (was_visible && !now_visible
		    && !IS_OMNIPRESENT(wwin) && !wwin->flags.changing_workspace) {
			wapp = wApplicationOf(wwin->main_window);
			if (wapp)
				wapp->last_workspace = workspace;
			if (wwin->flags.miniaturized && !w_global.config->sticky_icons) {
				if (wwin->icon) {
					xcb_unmap_window(wdisp->conn, wwin->icon->core->window);
					wwin->icon->mapped = 0;
				}
			} else if (wwin->flags.mapped || wwin->flags.shaded) {
				wSetFocusTo(scr, NULL);
				wWindowUnmap(wwin);
			}
		} else if (!was_visible && now_visible) {
			if (wwin->flags.miniaturized && !w_global.config->sticky_icons) {
				if (wwin->icon) {
					xcb_map_window(wdisp->conn, wwin->icon->core->window);
					wwin->icon->mapped = 1;
				}
			} else if (!wwin->flags.mapped
				   && !(wwin->flags.miniaturized || wwin->flags.hidden)
				   && !wGroupIsInactiveMember(wwin)) {
				wWindowMap(wwin);
			}
		}
		if (!IS_OMNIPRESENT(wwin))
			WMPostNotificationName(WMNChangedWorkspace, wwin,
					       (void *)(uintptr_t) oldWorkspace);
		if (wwin->tab_group)
			wGroupSyncWorkspace(wwin);
		wLayoutApply(scr, src_workspace);
		wLayoutApply(scr, workspace);
		wAutoRemoveEmptyWorkspaces(scr);
		return;
	}

	if (workspace != scr->current_workspace) {
		/* Sent to other workspace. Unmap window */
		if ((wwin->flags.mapped
		     || wwin->flags.shaded || (wwin->flags.miniaturized && !w_global.config->sticky_icons))
		    && !IS_OMNIPRESENT(wwin) && !wwin->flags.changing_workspace) {

			wapp = wApplicationOf(wwin->main_window);
			if (wapp)
				wapp->last_workspace = workspace;

			if (wwin->flags.miniaturized) {
				if (wwin->icon) {
					xcb_unmap_window(wdisp->conn, wwin->icon->core->window);
					wwin->icon->mapped = 0;
				}
			} else {
				unmap = 1;
				wSetFocusTo(scr, NULL);
			}
		}
	} else {
		/* brought to current workspace. Map window */
		if (wwin->flags.miniaturized && !w_global.config->sticky_icons) {
			if (wwin->icon) {
				xcb_map_window(wdisp->conn, wwin->icon->core->window);
				wwin->icon->mapped = 1;
			}
		} else if (!wwin->flags.mapped && !(wwin->flags.miniaturized || wwin->flags.hidden)
			   && !wGroupIsInactiveMember(wwin)) {
			wWindowMap(wwin);
		}
	}
	if (!IS_OMNIPRESENT(wwin)) {
		int oldWorkspace = WWIN_PRIMARY_WS(wwin);
		wwin->frame->workspace = workspace;
		wwin->frame->ws_mask = WS_BIT(workspace);
		WMPostNotificationName(WMNChangedWorkspace, wwin, (void *)(uintptr_t) oldWorkspace);
	}

	if (unmap)
		wWindowUnmap(wwin);

	if (wwin->tab_group)
		wGroupSyncWorkspace(wwin);

	wLayoutApply(scr, src_workspace);
	wLayoutApply(scr, workspace);
	wAutoRemoveEmptyWorkspaces(scr);
}

/* Set the full workspace membership mask and primary. wWindowChangeWorkspace()
 * remains the "move to N" verb (mask becomes exactly WS_BIT(N)) so its callers
 * and the opaque-drag changing_workspace path stay untouched, this entry point
 * is for the multi-membership toggles where the mask can gain or lose a bit
 * while the window stays visible. Omnipresent windows ignore the mask. */
void wWindowSetWorkspaceMask(WWindow *wwin, uint64_t mask, int primary)
{
	WScreen *scr = wwin->screen_ptr;
	WApplication *wapp;
	uint64_t old_mask, changed;
	int old_primary, i, fallback;
	bool was_visible, now_visible;

	if (!wwin->frame || IS_OMNIPRESENT(wwin))
		return;

	if (scr->workspace_count < MAX_WORKSPACES)
		mask &= WS_BIT(scr->workspace_count) - 1;
	if (mask == 0)
		return;

	if (primary < 0 || primary >= scr->workspace_count
	    || !(mask & WS_BIT(primary))) {
		fallback = scr->current_workspace;
		if (w_global.config->per_head_workspaces)
			fallback = scr->head_workspace[wWindowEffectiveHead(wwin)];
		if (mask & WS_BIT(fallback))
			primary = fallback;
		else
			primary = __builtin_ctzll(mask);
	}

	old_mask = wwin->frame->ws_mask;
	old_primary = wwin->frame->workspace;
	if (mask == old_mask && primary == old_primary)
		return;

	was_visible = wWindowIsVisibleOnAnyHead(wwin);
	wwin->frame->workspace = primary;
	wwin->frame->ws_mask = mask;
	now_visible = wWindowIsVisibleOnAnyHead(wwin);

	if (was_visible && !now_visible) {
		wapp = wApplicationOf(wwin->main_window);
		if (wapp)
			wapp->last_workspace = primary;
		if (wwin->flags.miniaturized && !w_global.config->sticky_icons) {
			if (wwin->icon) {
				xcb_unmap_window(wdisp->conn, wwin->icon->core->window);
				wwin->icon->mapped = 0;
			}
		} else if (wwin->flags.mapped || wwin->flags.shaded) {
			wSetFocusTo(scr, NULL);
			wWindowUnmap(wwin);
		}
	} else if (!was_visible && now_visible) {
		if (wwin->flags.miniaturized && !w_global.config->sticky_icons) {
			if (wwin->icon) {
				xcb_map_window(wdisp->conn, wwin->icon->core->window);
				wwin->icon->mapped = 1;
			}
		} else if (!wwin->flags.mapped
			   && !(wwin->flags.miniaturized || wwin->flags.hidden)
			   && !wGroupIsInactiveMember(wwin)) {
			wWindowMap(wwin);
		}
	}

	if (primary != old_primary)
		WMPostNotificationName(WMNChangedWorkspace, wwin,
				       (void *)(uintptr_t)old_primary);
	if ((old_mask == WS_ALL) != (mask == WS_ALL))
		WMPostNotificationName(WMNChangedState, wwin, "omnipresent");

	if (wwin->tab_group)
		wGroupSyncWorkspace(wwin);

	/* Reflow every workspace whose membership flipped so a tiling
	 * layout on either side picks up or drops this window. */
	changed = old_mask ^ mask;
	for (i = 0; changed; i++) {
		if (changed & WS_BIT(i))
			wLayoutApply(scr, i);
		changed &= ~WS_BIT(i);
	}
	wAutoRemoveEmptyWorkspaces(scr);
}

void wWindowAddToWorkspace(WWindow *wwin, int workspace)
{
	if (!wwin->frame || workspace < 0
	    || workspace >= wwin->screen_ptr->workspace_count)
		return;
	wWindowSetWorkspaceMask(wwin, wwin->frame->ws_mask | WS_BIT(workspace),
				wwin->frame->workspace);
}

void wWindowRemoveFromWorkspace(WWindow *wwin, int workspace)
{
	uint64_t mask;

	if (!wwin->frame || workspace < 0
	    || workspace >= wwin->screen_ptr->workspace_count)
		return;
	mask = wwin->frame->ws_mask & ~WS_BIT(workspace);
	if (mask == 0)
		return;
	/* primary is passed through unchanged, wWindowSetWorkspaceMask()
	 * picks current-if-member else lowest bit when it was the bit
	 * just cleared */
	wWindowSetWorkspaceMask(wwin, mask, wwin->frame->workspace);
}

void wWindowChangeWorkspaceRelative(WWindow *wwin, int amount)
{
	WScreen *scr = wwin->screen_ptr;
	int w;

	if (w_global.config->per_head_workspaces) {
		int head = wWindowEffectiveHead(wwin);

		/* Step through the same allowed-on-this-head sequence as
		 * wWorkspaceRelativeChange so the window never lands on a
		 * workspace pinned to another output and vanishes. */
		w = wWorkspaceRelativeHeadTarget(scr, head, amount);
		if (w < 0)
			return;
		if (w >= scr->workspace_count)
			wWorkspaceMake(scr, w - scr->workspace_count + 1);
		wWindowChangeWorkspace(wwin, w);
		return;
	}

	w = scr->current_workspace + amount;

	if (amount < 0) {
		if (w >= 0) {
			wWindowChangeWorkspace(wwin, w);
		} else if (w_global.config->ws_cycle) {
			wWindowChangeWorkspace(wwin, scr->workspace_count + w);
		}
	} else if (amount > 0) {
		if (w < scr->workspace_count) {
			wWindowChangeWorkspace(wwin, w);
		} else if (w_global.config->ws_advance) {
			int workspace = WMIN(w, MAX_WORKSPACES - 1);
			wWorkspaceMake(scr, workspace);
			wWindowChangeWorkspace(wwin, workspace);
		} else if (w_global.config->ws_cycle) {
			wWindowChangeWorkspace(wwin, w % scr->workspace_count);
		}
	}
}

void wWindowSynthConfigureNotify(WWindow *wwin)
{
	xcb_configure_notify_event_t sevent;

	memset(&sevent, 0, sizeof(sevent));
	sevent.response_type = XCB_CONFIGURE_NOTIFY;
	sevent.event = wwin->client_win;
	sevent.window = wwin->client_win;

	sevent.x = wwin->client.x;
	sevent.y = wwin->client.y;
	sevent.width = wwin->client.width;
	sevent.height = wwin->client.height;

	sevent.border_width = wwin->old_border_width;
	if (HAS_TITLEBAR(wwin) && wwin->frame->titlebar)
		sevent.above_sibling = wwin->frame->titlebar->window;
	else
		sevent.above_sibling = XCB_NONE;

	sevent.override_redirect = false;
	WXSendEvent32(wdisp->conn, &sevent, sizeof(sevent), 0, wwin->client_win,
		      XCB_EVENT_MASK_STRUCTURE_NOTIFY);
	xcb_flush(wdisp->conn);
}

/*
 *----------------------------------------------------------------------
 * wWindowConfigure()
 *
 * req_x, req_y: new requested positions for the frame
 * req_width, req_height: new requested sizes for the client
 *
 * Configures the frame, decorations and client window to the specified
 * geometry, whose validity is not checked --  wWindowConstrainSize()
 * must be used for that.
 * 	The size parameters are for the client window, but the position is
 * for the frame.
 * 	The client window receives a ConfigureNotify event, according
 * to what ICCCM says.
 *
 * Returns:
 * 	None
 *
 * Side effects:
 * 	Window size and position are changed and client window receives
 * a ConfigureNotify event.
 *----------------------------------------------------------------------
 */
void wWindowConfigure(WWindow *wwin, int req_x, int req_y, int req_width, int req_height)
{
	int synth_notify = false;
	int resize;
	int il, ir, it, ib;
	int ih, iv;

	wFrameInnerBorderWidths(wwin, &il, &ir, &it, &ib);
	ih = il + ir;
	iv = it + ib;

	resize = (req_width != wwin->client.width || req_height != wwin->client.height);

	if (WFLAGP(wwin, dont_move_off))
		wScreenBringInside(wwin->screen_ptr, &req_x, &req_y, req_width, req_height);

	if (resize) {
		if (req_width < MIN_WINDOW_SIZE)
			req_width = MIN_WINDOW_SIZE;
		if (req_height < MIN_WINDOW_SIZE)
			req_height = MIN_WINDOW_SIZE;

		/* If growing, resize inner part before frame,
		 * if shrinking, resize frame before.
		 * This will prevent the frame (that can have a different color)
		 * to be exposed, causing flicker */
		if (req_height > wwin->frame->core->height || req_width > wwin->frame->core->width)
			xcb_configure_window(wdisp->conn, wwin->client_win, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, (const uint32_t[]){ req_width, req_height });

		if (wwin->flags.shaded) {
			wFrameWindowConfigure(wwin->frame, req_x, req_y,
					   req_width + ih, wwin->frame->core->height);
			wwin->old_geometry.height = req_height;
		} else {
			int h;

			h = req_height + wwin->frame->top_width + wwin->frame->bottom_width
			    + iv;

			wFrameWindowConfigure(wwin->frame, req_x, req_y,
					   req_width + ih, h);
		}

		/*
		 * When the frame is resized/moved, the X server repositions a client
		 * with non-NorthWest gravity inside the frame to compensate (visible as
		 * a GravityNotify with xev), leaving it at the wrong offset.
		 */
		if (wwin->normal_hints && wwin->normal_hints->win_gravity != XCB_GRAVITY_NORTH_WEST)
			xcb_configure_window(wdisp->conn, wwin->client_win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ il, wwin->frame->top_width + it });

		if (!(req_height > wwin->frame->core->height || req_width > wwin->frame->core->width))
			xcb_configure_window(wdisp->conn, wwin->client_win, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, (const uint32_t[]){ req_width, req_height });

		wFrameInnerBorderClientOrigin(wwin, req_x, req_y,
					      &wwin->client.x, &wwin->client.y);
		wwin->client.width = req_width;
		wwin->client.height = req_height;
	} else {
		if (req_x != wwin->frame_x || req_y != wwin->frame_y) {
			wFrameInnerBorderClientOrigin(wwin, req_x, req_y,
						      &wwin->client.x, &wwin->client.y);
			xcb_configure_window(wdisp->conn, wwin->frame->core->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ req_x, req_y });
		}
		/*
		 * if the window is being moved but not resized
		 * or if we change nothing then
		 * send a synthetic ConfigureNotify
		 */
		synth_notify = true;
	}
	wwin->frame_x = req_x;
	wwin->frame_y = req_y;
#ifdef USE_XSHAPE
	if (wdisp->xext.shape.supported && wwin->flags.shaped && resize)
		wWindowSetShape(wwin);
#endif

	if (synth_notify)
		wWindowSynthConfigureNotify(wwin);

	wNETFrameExtents(wwin);

	/* Propagate resize to all other members of the same tab group */
	if (resize && wwin->tab_group)
		wGroupSyncSize(wwin);

	wwin->last_head = wGetHeadForWindow(wwin);

	if (wwin->on_geometry_change)
		wwin->on_geometry_change(wwin, wwin->geometry_data);

	wXFlush(wdisp->conn);
}

/* req_x, req_y: new position of the frame */
void wWindowMove(WWindow *wwin, int req_x, int req_y)
{
#ifdef CONFIGURE_WINDOW_WHILE_MOVING
	int synth_notify = false;

	/* Send a synthetic ConfigureNotify event for every window movement. */
	if ((req_x != wwin->frame_x || req_y != wwin->frame_y))
		synth_notify = true;
#else
	/* A single synthetic ConfigureNotify event is sent at the end of
	 * a completed (opaque) movement in moveres.c */
#endif

	if (WFLAGP(wwin, dont_move_off))
		wScreenBringInside(wwin->screen_ptr, &req_x, &req_y,
				   wwin->frame->core->width, wwin->frame->core->height);

	wFrameInnerBorderClientOrigin(wwin, req_x, req_y, &wwin->client.x, &wwin->client.y);

	xcb_configure_window(wdisp->conn, wwin->frame->core->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ req_x, req_y });

	wwin->frame_x = req_x;
	wwin->frame_y = req_y;

	if (wwin->tab_group)
		wGroupSyncPosition(wwin);

#ifdef CONFIGURE_WINDOW_WHILE_MOVING
	if (synth_notify)
		wWindowSynthConfigureNotify(wwin);
#endif

	if (wwin->on_geometry_change)
		wwin->on_geometry_change(wwin, wwin->geometry_data);
}

void wWindowApplyInnerBorder(WWindow *wwin)
{
	int il, ir, it, ib;
	int width;
	int height;

	if (!wwin->frame || !wwin->frame->flags.is_client_window_frame)
		return;

	wFrameInnerBorderWidths(wwin, &il, &ir, &it, &ib);
	width = wwin->client.width + il + ir;
	height = wwin->client.height + wwin->frame->top_width + wwin->frame->bottom_width + it + ib;

	wFrameWindowConfigure(wwin->frame, wwin->frame_x, wwin->frame_y, width, height);
	xcb_configure_window(wdisp->conn, wwin->client_win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ il, wwin->frame->top_width + it });
	wFrameInnerBorderClientOrigin(wwin, wwin->frame_x, wwin->frame_y,
				     &wwin->client.x, &wwin->client.y);
	wWindowSynthConfigureNotify(wwin);
	wNETFrameExtents(wwin);
}

/* Move the window to the nearest on-screen position if its stored
 * frame origin falls in dead space (for example when a RandR monitor
 * was removed while the window was miniaturized or hidden) */
void wWindowSnapToHead(WWindow *wwin)
{
#ifdef USE_RANDR
	int bw = HAS_BORDER(wwin) ? wFrameBorderWidth(wwin) : 0;
	int rx = wwin->frame_x;
	int ry = wwin->frame_y;

	if (wScreenBringInside(wwin->screen_ptr, &rx, &ry,
	                       wwin->frame->core->width  + 2 * bw,
	                       wwin->frame->core->height + 2 * bw))
		wWindowMove(wwin, rx, ry);
#else
	/* Parameter not used, but tell the compiler that it is ok */
	(void) wwin;
#endif
}

void wWindowUpdateButtonImages(WWindow *wwin)
{
	WScreen *scr = wwin->screen_ptr;
	xcb_pixmap_t pixmap, mask;
	WFrameWindow *fwin = wwin->frame;

	if (!HAS_TITLEBAR(wwin))
		return;

	/* miniaturize button */
	if (!WFLAGP(wwin, no_miniaturize_button)) {
		if (wwin->wm_gnustep_attr && wwin->wm_gnustep_attr->flags & GSMiniaturizePixmapAttr) {
			pixmap = wwin->wm_gnustep_attr->miniaturize_pixmap;

			if (wwin->wm_gnustep_attr->flags & GSMiniaturizeMaskAttr)
				mask = wwin->wm_gnustep_attr->miniaturize_mask;
			else
				mask = XCB_NONE;

			if (fwin->button_image[WTB_MINIATURIZE]
			    && (fwin->button_image[WTB_MINIATURIZE]->image != pixmap
				|| fwin->button_image[WTB_MINIATURIZE]->mask != mask)) {
				wPixmapDestroy(fwin->button_image[WTB_MINIATURIZE]);
				fwin->button_image[WTB_MINIATURIZE] = NULL;
			}

			if (!fwin->button_image[WTB_MINIATURIZE]) {
				fwin->button_image[WTB_MINIATURIZE] = wPixmapCreate(pixmap, mask);
				fwin->button_image[WTB_MINIATURIZE]->client_owned = 1;
				fwin->button_image[WTB_MINIATURIZE]->client_owned_mask = 1;
			}
		} else {
			if (fwin->button_image[WTB_MINIATURIZE] && !fwin->button_image[WTB_MINIATURIZE]->shared)
				wPixmapDestroy(fwin->button_image[WTB_MINIATURIZE]);

			fwin->button_image[WTB_MINIATURIZE] = scr->b_pixmaps[WBUT_ICONIFY];
		}
	}

	/* maximize button */
	if (fwin->button_image[WTB_MAXIMIZE] && !fwin->button_image[WTB_MAXIMIZE]->shared)
		wPixmapDestroy(fwin->button_image[WTB_MAXIMIZE]);
	fwin->button_image[WTB_MAXIMIZE] =
		scr->b_pixmaps[(wwin->flags.maximized && w_global.config->new_style != TS_FLAT)
			       ? WBUT_RESTORE : WBUT_MAXIMIZE];

	/* close button */

	/* redefine WFLAGP to MGFLAGP to allow broken close operation */
#define MGFLAGP(wwin, FLAG)	(wwin)->client_flags.FLAG

	if (!WFLAGP(wwin, no_close_button)) {
		if (wwin->wm_gnustep_attr && wwin->wm_gnustep_attr->flags & GSClosePixmapAttr) {
			pixmap = wwin->wm_gnustep_attr->close_pixmap;

			if (wwin->wm_gnustep_attr->flags & GSCloseMaskAttr)
				mask = wwin->wm_gnustep_attr->close_mask;
			else
				mask = XCB_NONE;

			if (fwin->button_image[WTB_CLOSE]
			    && (fwin->button_image[WTB_CLOSE]->image != pixmap
				|| fwin->button_image[WTB_CLOSE]->mask != mask)) {
				wPixmapDestroy(fwin->button_image[WTB_CLOSE]);
				fwin->button_image[WTB_CLOSE] = NULL;
			}

			if (!fwin->button_image[WTB_CLOSE]) {
				fwin->button_image[WTB_CLOSE] = wPixmapCreate(pixmap, mask);
				fwin->button_image[WTB_CLOSE]->client_owned = 1;
				fwin->button_image[WTB_CLOSE]->client_owned_mask = 1;
			}

		} else if (WFLAGP(wwin, kill_close)) {
			if (fwin->button_image[WTB_CLOSE] && !fwin->button_image[WTB_CLOSE]->shared)
				wPixmapDestroy(fwin->button_image[WTB_CLOSE]);

			fwin->button_image[WTB_CLOSE] = scr->b_pixmaps[WBUT_KILL];

		} else if (MGFLAGP(wwin, broken_close)) {
			if (fwin->button_image[WTB_CLOSE] && !fwin->button_image[WTB_CLOSE]->shared)
				wPixmapDestroy(fwin->button_image[WTB_CLOSE]);

			fwin->button_image[WTB_CLOSE] = scr->b_pixmaps[WBUT_BROKENCLOSE];

		} else {
			if (fwin->button_image[WTB_CLOSE] && !fwin->button_image[WTB_CLOSE]->shared)
				wPixmapDestroy(fwin->button_image[WTB_CLOSE]);

			fwin->button_image[WTB_CLOSE] = scr->b_pixmaps[WBUT_CLOSE];
		}
	}

	/* force buttons to be redrawn */
	fwin->flags.need_texture_change = 1;
	wFrameWindowPaint(fwin);
}

int wFrameBorderWidth(const WWindow *wwin)
{
	if (wwin->frame_border.frame_border_width_set)
		return wwin->frame_border.frame_border_width;
	return wwin->screen_ptr->frame_border_width;
}

int wFrameInnerBorderWidth(const WWindow *wwin)
{
	if (wwin->frame_border.frame_inner_border_width_set)
		return wwin->frame_border.frame_inner_border_width;
	return wwin->screen_ptr->frame_inner_border_width;
}

unsigned int wFrameInnerBorderSides(const WWindow *wwin)
{
	if (wwin->frame_border.frame_inner_border_sides_set)
		return wwin->frame_border.frame_inner_border_sides;
	return wwin->screen_ptr->frame_inner_border_sides;
}

void wFrameInnerBorderWidths(const WWindow *wwin, int *il, int *ir, int *it, int *ib)
{
	int w = HAS_BORDER(wwin) ? wFrameInnerBorderWidth(wwin) : 0;
	unsigned int sides = wFrameInnerBorderSides(wwin);

	if (il)
		*il = (sides & INNER_SIDE_LEFT) ? w : 0;
	if (ir)
		*ir = (sides & INNER_SIDE_RIGHT) ? w : 0;
	if (it)
		*it = (sides & INNER_SIDE_TOP) ? w : 0;
	if (ib)
		*ib = (sides & INNER_SIDE_BOTTOM) ? w : 0;
}

void wFrameInnerBorderClientOrigin(const WWindow *wwin, int frame_x, int frame_y,
				   int *client_x, int *client_y)
{
	int il, it;
	int bw = HAS_BORDER(wwin) ? wFrameBorderWidth(wwin) : 0;

	wFrameInnerBorderWidths(wwin, &il, NULL, &it, NULL);
	if (client_x)
		*client_x = frame_x + bw + il;
	if (client_y)
		*client_y = frame_y + bw + wwin->frame->top_width + it;
}

WMColor *wFrameBorderColor(const WWindow *wwin)
{
	if (wwin->frame_border.frame_border_color_set)
		return wwin->frame_border.frame_border_color;
	return wwin->screen_ptr->frame_border_color;
}

WMColor *wFrameFocusedBorderColor(const WWindow *wwin)
{
	if (wwin->frame_border.frame_focused_border_color_set)
		return wwin->frame_border.frame_focused_border_color;
	return wwin->screen_ptr->frame_focused_border_color;
}

WMColor *wFrameSelectedBorderColor(const WWindow *wwin)
{
	if (wwin->frame_border.frame_selected_border_color_set)
		return wwin->frame_border.frame_selected_border_color;
	return wwin->screen_ptr->frame_selected_border_color;
}

WMColor *wFrameInnerBorderColor(const WWindow *wwin)
{
	if (wwin->frame_border.frame_inner_border_color_set)
		return wwin->frame_border.frame_inner_border_color;
	return wwin->screen_ptr->frame_inner_border_color;
}

WMColor *wFrameInnerFocusedBorderColor(const WWindow *wwin)
{
	if (wwin->frame_border.frame_inner_focused_border_color_set)
		return wwin->frame_border.frame_inner_focused_border_color;
	return wwin->screen_ptr->frame_inner_focused_border_color;
}

/*
 *---------------------------------------------------------------------------
 * wWindowConfigureBorders--
 * 	Update window border configuration according to attribute flags.
 *
 *---------------------------------------------------------------------------
 */
void wWindowConfigureBorders(WWindow *wwin)
{
	if (wwin->frame) {
		int flags;
		int newy, oldh;
		int il, it;

		flags = WFF_MINIATURIZE_BUTTON | WFF_CLOSE_BUTTON
			| WFF_MAXIMIZE_BUTTON | WFF_ICON_SLOT;

#ifdef XKB_BUTTON_HINT
	if (w_global.config->modelock)
		flags |= WFF_LANGUAGE_BUTTON;
#endif

		if (HAS_TITLEBAR(wwin))
			flags |= WFF_TITLEBAR;
		if (HAS_RESIZEBAR(wwin) && IS_RESIZABLE(wwin))
			flags |= WFF_RESIZEBAR;
		if (HAS_BORDER(wwin))
			flags |= WFF_BORDER;
		if (wwin->flags.shaded)
			flags |= WFF_IS_SHADED;
		if (wwin->flags.selected)
			flags |= WFF_SELECTED;

		oldh = wwin->frame->top_width;
		{
			int old_border = wwin->frame->flags.border;

			wFrameWindowUpdateBorders(wwin->frame, flags);
			if (old_border != wwin->frame->flags.border)
				wWindowApplyInnerBorder(wwin);
		}
		if (oldh != wwin->frame->top_width) {
			/* When the titlebar height changes on an already-mapped
			 * window (interactive decoration toggle, runtime rule
			 * reload), keep the client's root position fixed by
			 * shifting the frame: newy = frame_y + oldh - top_width.
			 * On the initial setup path (wManageWindow still in
			 * progress, flags.initializing is set) the frame
			 * position was already chosen for the FINAL decorations
			 * by wManageWindow() (saved frame origin via
			 * _WINDOWMAKER_STATE or fresh placement), so the shift
			 * would move the window by |Δtop_width| every restart.
			 * Keep the frame put in that case. The XMoveWindow and
			 * wWindowConfigure below still reposition the client at
			 * the new (inner-left, top_width + inner-top) offset
			 * inside the resized frame. */
			if (wwin->flags.initializing)
				newy = wwin->frame_y;
			else
				newy = wwin->frame_y + oldh - wwin->frame->top_width;

			wFrameInnerBorderWidths(wwin, &il, NULL, &it, NULL);
			xcb_configure_window(wdisp->conn, wwin->client_win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ il, wwin->frame->top_width + it });
			wWindowConfigure(wwin, wwin->frame_x, newy, wwin->client.width, wwin->client.height);
			/* the existing call below only fires when a hide
			 * flag toggles, wWindowUpdateButtonImages itself
			 * early-returns when !HAS_TITLEBAR, so a
			 * _NET_WM_STATE_MAXIMIZED_* change that arrives
			 * while fullscreen leaves the Maximize glyph
			 * stale until this branch runs on titlebar
			 * recreation. Same for the icon scaled to the
			 * old top_width */
			if (HAS_TITLEBAR(wwin))
				wWindowUpdateButtonImages(wwin);
			wWindowRefreshTitlebarIcon(wwin);
		}

		flags = 0;
		if (!WFLAGP(wwin, no_miniaturize_button)
		    && (wwin->frame->flags.hide_button & (1 << WTB_MINIATURIZE)))
			flags |= WFF_MINIATURIZE_BUTTON;

#ifdef XKB_BUTTON_HINT
		if (!WFLAGP(wwin, no_language_button)
		    && (wwin->frame->flags.hide_button & (1 << WTB_LANGUAGE)))
			flags |= WFF_LANGUAGE_BUTTON;
#endif

		if (!WFLAGP(wwin, no_close_button)
		    && (wwin->frame->flags.hide_button & (1 << WTB_CLOSE)))
			flags |= WFF_CLOSE_BUTTON;

		if (IS_RESIZABLE(wwin)
		    && (wwin->frame->flags.hide_button & (1 << WTB_MAXIMIZE)))
			flags |= WFF_MAXIMIZE_BUTTON;

		if (flags != 0) {
			wWindowUpdateButtonImages(wwin);
			wFrameWindowShowButton(wwin->frame, flags);
		}

		flags = 0;
		if (WFLAGP(wwin, no_miniaturize_button)
		    && !(wwin->frame->flags.hide_button & (1 << WTB_MINIATURIZE)))
			flags |= WFF_MINIATURIZE_BUTTON;

#ifdef XKB_BUTTON_HINT
		if (WFLAGP(wwin, no_language_button)
		    && !(wwin->frame->flags.hide_button & (1 << WTB_LANGUAGE)))
			flags |= WFF_LANGUAGE_BUTTON;
#endif

		if (WFLAGP(wwin, no_close_button)
		    && !(wwin->frame->flags.hide_button & (1 << WTB_CLOSE)))
			flags |= WFF_CLOSE_BUTTON;

		if (!IS_RESIZABLE(wwin)
		    && !(wwin->frame->flags.hide_button & (1 << WTB_MAXIMIZE)))
			flags |= WFF_MAXIMIZE_BUTTON;

		if (flags != 0)
			wFrameWindowHideButton(wwin->frame, flags);

#ifdef USE_XSHAPE
		if (wdisp->xext.shape.supported && wwin->flags.shaped)
			wWindowSetShape(wwin);
#endif
	}
}

void wWindowSaveState(WWindow *wwin)
{
	uint32_t data[WSAVED_STATE_SLOTS];
	int i;

	memset(data, 0, sizeof(data));
	data[0] = WWIN_PRIMARY_WS(wwin);
	data[1] = wwin->flags.miniaturized;
	data[2] = wwin->flags.shaded;
	data[3] = wwin->flags.hidden;
	data[4] = wwin->flags.maximized;
	if (wwin->flags.maximized == 0) {
		data[5] = wwin->frame_x;
		data[6] = wwin->frame_y;
		data[7] = wwin->frame->core->width;
		data[8] = wwin->frame->core->height;
	} else {
		data[5] = wwin->old_geometry.x;
		data[6] = wwin->old_geometry.y;
		data[7] = wwin->old_geometry.width;
		data[8] = wwin->old_geometry.height;
	}

	for (i = 0; i < MAX_WINDOW_SHORTCUTS; i++) {
		if (wwin->screen_ptr->shortcutWindows[i] &&
		    WMCountInArray(wwin->screen_ptr->shortcutWindows[i], wwin))
			data[9] |= 1 << i;
	}
	data[10] = (uint32_t)(wwin->frame->ws_mask & 0xffffffffu);
	data[11] = (uint32_t)(wwin->frame->ws_mask >> 32);
	/* The tri-state slots are 0 unset, 1 off, 2 on for a per-window
	 * value and 3 off, 4 on for a domain-supplied snapshot, which the
	 * restore only applies where the current fill left the flag
	 * undefined, so a domain edit wins but a deleted rule keeps the
	 * live window stable across a warm restart. */
#define WSAVED_TRISTATE(FLAG) \
	(wwin->defined_user_flags.FLAG \
	 ? wwin->user_flags.FLAG + 1 + (wwin->domain_flags.FLAG ? 2 : 0) : 0)
	/* per-window auto-on-unfocus bits, modelled on shaded/hidden so they
	 * survive warm restart without propagating to same-class siblings */
	data[12] = WSAVED_TRISTATE(auto_hide_on_unfocus);
	data[13] = WSAVED_TRISTATE(auto_miniaturize_on_unfocus);
	/* user geometry locks, tri-state so a user-unlock of a
	 * client-locked window survives too */
	data[14] = WSAVED_TRISTATE(no_movable);
	data[15] = WSAVED_TRISTATE(no_resizable);
	/* per-window menu state so it survives a warm restart: stacking level
	 * (plain levels only, so a GNUstep or modal level is not squashed)
	 * and tri-state decoration and skip flags */
	switch (wwin->frame->core->stacking->window_level) {
	case WMNormalLevel:
		data[16] = 1;
		break;
	case WMFloatingLevel:
		data[16] = 2;
		break;
	case WMSunkenLevel:
		data[16] = 3;
		break;
	default:
		data[16] = 0;
		break;
	}
	data[17] = WSAVED_TRISTATE(no_titlebar);
	data[18] = WSAVED_TRISTATE(no_resizebar);
	data[19] = WSAVED_TRISTATE(no_close_button);
	data[20] = WSAVED_TRISTATE(no_miniaturize_button);
	data[21] = WSAVED_TRISTATE(no_border);
	data[22] = WSAVED_TRISTATE(skip_window_list);
	data[23] = WSAVED_TRISTATE(skip_switchpanel);
#undef WSAVED_TRISTATE

	WXSetFormat32N(wdisp->conn, wwin->client_win, wdisp->atom.wmaker.state,
		       wdisp->atom.wmaker.state, data, WSAVED_STATE_SLOTS);

	if (wwin->tab_group != NULL) {
		uint32_t tab[3];

		tab[0] = wGroupSessionId(wwin->screen_ptr, wwin->tab_group);
		tab[1] = (uint32_t)wwin->tab_group_index;
		tab[2] = (uint32_t)wwin->tab_group->active_index;
		WXSetFormat32N(wdisp->conn, wwin->client_win, wdisp->atom.wmaker.tab_group,
			       wdisp->atom.wmaker.tab_group, tab, 3);
	} else {
		WXDeleteProperty(wdisp->conn, wwin->client_win, wdisp->atom.wmaker.tab_group);
	}

	if (wwin->mark_key_label != NULL)
		WXSetLatin1String(wdisp->conn, wwin->client_win, wdisp->atom.wmaker.mark_key,
				  wwin->mark_key_label, strlen(wwin->mark_key_label));
	else
		WXDeleteProperty(wdisp->conn, wwin->client_win, wdisp->atom.wmaker.mark_key);
}

/* Decode a tri-state _WINDOWMAKER_STATE slot: 0 unset, 1 off, 2 on.
 * Values 3 and 4 are a snapshot of a domain-supplied off/on, they only
 * fill a flag the current domain fill left undefined. Anything past 4
 * is corrupt and decodes to unset. *is_set is 0 unset, 1 per-window,
 * 2 domain snapshot. */
static void decodeSavedTriState(uint32_t raw, int *value, int *is_set)
{
	if (raw == 0 || raw > 4) {
		*value = 0;
		*is_set = 0;
		return;
	}
	*value = (raw & 1) ? 0 : 1;
	*is_set = (raw >= 3) ? 2 : 1;
}

static int getSavedStateCk(xcb_window_t window, WXCookie state_ck, WXCookie mark_ck,
			   WXCookie tab_ck, WSavedState **state)
{
	uint32_t *data;
	uint32_t tab[3];
	uint32_t raw;
	size_t len, count;

	/* Accept the pre-mask 10-slot layout, the 12-slot layout, the
	 * 16-slot layout, and the current WSAVED_STATE_SLOTS-slot one so a
	 * restart from an older binary still recovers primary workspace, the
	 * mask defaults to 0 (derived from primary), the auto-on-unfocus bits
	 * default to 0 (per-window, off), the geometry locks default to 0 (not
	 * user-defined), and the menu state defaults to unset (re-derive).
	 * Quads past the current layout, written by a newer or older binary,
	 * are ignored. */
	data = WXRcvFormat32List(wdisp->conn, state_ck, &count);
	if (!data || count < 10) {
		wfree(data);
		WXDiscard(wdisp->conn, mark_ck);
		WXDiscard(wdisp->conn, tab_ck);
		return 0;
	}

	WXDeleteProperty(wdisp->conn, window, wdisp->atom.wmaker.state);

	*state = wmalloc(sizeof(WSavedState));

	(*state)->workspace = data[0];
	(*state)->miniaturized = data[1];
	(*state)->shaded = data[2];
	(*state)->hidden = data[3];
	(*state)->maximized = data[4];
	(*state)->x = (int32_t)data[5];
	(*state)->y = (int32_t)data[6];
	(*state)->w = data[7];
	(*state)->h = data[8];
	(*state)->window_shortcuts = data[9];
	if (count >= 12)
		(*state)->ws_mask = (uint64_t)data[10] | ((uint64_t)data[11] << 32);
	if (count >= 13)
		decodeSavedTriState(data[12], &(*state)->auto_hide_on_unfocus,
				    &(*state)->auto_hide_on_unfocus_set);
	if (count >= 14)
		decodeSavedTriState(data[13], &(*state)->auto_miniaturize_on_unfocus,
				    &(*state)->auto_miniaturize_on_unfocus_set);
	if (count >= 15)
		decodeSavedTriState(data[14], &(*state)->no_movable,
				    &(*state)->no_movable_set);
	if (count >= 16)
		decodeSavedTriState(data[15], &(*state)->no_resizable,
				    &(*state)->no_resizable_set);
	raw = count >= 17 ? data[16] : 0;
	(*state)->stacking_level = (raw >= 1 && raw <= 3) ? (int)raw : 0;
	if (count >= 18)
		decodeSavedTriState(data[17], &(*state)->no_titlebar, &(*state)->no_titlebar_set);
	if (count >= 19)
		decodeSavedTriState(data[18], &(*state)->no_resizebar, &(*state)->no_resizebar_set);
	if (count >= 20)
		decodeSavedTriState(data[19], &(*state)->no_close_button, &(*state)->no_close_button_set);
	if (count >= 21)
		decodeSavedTriState(data[20], &(*state)->no_miniaturize_button,
				    &(*state)->no_miniaturize_button_set);
	if (count >= 22)
		decodeSavedTriState(data[21], &(*state)->no_border, &(*state)->no_border_set);
	if (count >= 23)
		decodeSavedTriState(data[22], &(*state)->skip_window_list,
				    &(*state)->skip_window_list_set);
	if (count >= 24)
		decodeSavedTriState(data[23], &(*state)->skip_switchpanel,
				    &(*state)->skip_switchpanel_set);
	wfree(data);

	(*state)->mark_key = WXRcvString(wdisp->conn, mark_ck, &len);
	if ((*state)->mark_key)
		WXDeleteProperty(wdisp->conn, window, wdisp->atom.wmaker.mark_key);

	if (WXRcvFormat32N(wdisp->conn, tab_ck, tab, 3)) {
		(*state)->tab_group_gid = tab[0];
		(*state)->tab_group_idx = (int)tab[1];
		(*state)->tab_group_active = (int)tab[2];
		WXDeleteProperty(wdisp->conn, window, wdisp->atom.wmaker.tab_group);
	}

	return 1;
}

#ifdef USE_XSHAPE
void wWindowClearShape(WWindow * wwin)
{
	xcb_shape_mask(wdisp->conn, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING,
		       wwin->frame->core->window, 0, wwin->frame->top_width,
		       XCB_PIXMAP_NONE);
	wXFlush(wdisp->conn);
}

void wWindowSetShape(WWindow * wwin)
{
	xcb_rectangle_t rect[2];
	int count;
	int il, it;

	/* the client is reparented at (il, top_width + it), not (0, top_width) */
	wFrameInnerBorderWidths(wwin, &il, NULL, &it, NULL);

#ifdef OPTIMIZE_SHAPE
	int i;
	xcb_shape_get_rectangles_reply_t *gr;
	xcb_rectangle_t *rects;
	xcb_rectangle_t *urec;

	/* only shape is the client's */
	if (!HAS_TITLEBAR(wwin) && !HAS_RESIZEBAR(wwin))
		goto alt_code;

	/* Get array of rectangles describing the shape mask */
	gr = xcb_shape_get_rectangles_reply(wdisp->conn,
			xcb_shape_get_rectangles(wdisp->conn, wwin->client_win,
						 XCB_SHAPE_SK_BOUNDING), NULL);
	if (!gr)
		goto alt_code;

	count = xcb_shape_get_rectangles_rectangles_length(gr);
	rects = xcb_shape_get_rectangles_rectangles(gr);

	urec = malloc(sizeof(xcb_rectangle_t) * (count + 2));
	if (!urec) {
		free(gr);
		goto alt_code;
	}

	/* insert our decoration rectangles in the rect list */
	memcpy(urec, rects, sizeof(xcb_rectangle_t) * count);
	free(gr);

	for (i = 0; i < count; i++) {
		urec[i].x += il;
		urec[i].y += it;
	}

	if (HAS_TITLEBAR(wwin)) {
		urec[count].x = -1;
		urec[count].y = -1 - wwin->frame->top_width;
		urec[count].width = wwin->frame->core->width + 2;
		urec[count].height = wwin->frame->top_width + 1;
		count++;
	}
	if (HAS_RESIZEBAR(wwin)) {
		urec[count].x = -1;
		urec[count].y = wwin->frame->core->height - wwin->frame->bottom_width - wwin->frame->top_width;
		urec[count].width = wwin->frame->core->width + 2;
		urec[count].height = wwin->frame->bottom_width + 1;
		count++;
	}

	/* shape our frame window */
	xcb_shape_rectangles(wdisp->conn, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING,
			     XCB_CLIP_ORDERING_UNSORTED, wwin->frame->core->window,
			     0, wwin->frame->top_width, count, urec);
	wXFlush(wdisp->conn);
	free(urec);
	return;

 alt_code:
#endif				/* OPTIMIZE_SHAPE */
	count = 0;
	if (HAS_TITLEBAR(wwin)) {
		rect[count].x = -1;
		rect[count].y = -1;
		rect[count].width = wwin->frame->core->width + 2;
		rect[count].height = wwin->frame->top_width + 1;
		count++;
	}
	if (HAS_RESIZEBAR(wwin)) {
		rect[count].x = -1;
		rect[count].y = wwin->frame->core->height - wwin->frame->bottom_width;
		rect[count].width = wwin->frame->core->width + 2;
		rect[count].height = wwin->frame->bottom_width + 1;
		count++;
	}
	if (count > 0) {
		xcb_shape_rectangles(wdisp->conn, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING,
				     XCB_CLIP_ORDERING_UNSORTED,
				     wwin->frame->core->window, 0, 0, count, rect);
	}
	xcb_shape_combine(wdisp->conn, count > 0 ? XCB_SHAPE_SO_UNION : XCB_SHAPE_SO_SET,
			  XCB_SHAPE_SK_BOUNDING, XCB_SHAPE_SK_BOUNDING,
			  wwin->frame->core->window, il,
			  wwin->frame->top_width + it,
			  wwin->client_win);
	wXFlush(wdisp->conn);
}
#endif				/* USE_XSHAPE */

/* ====================================================================== */

static FocusMode getFocusMode(WWindow * wwin)
{
	FocusMode mode;

	if ((wwin->wm_hints) && (wwin->wm_hints->flags & XCB_ICCCM_WM_HINT_INPUT)) {
		if (wwin->wm_hints->input == true) {
			if (wwin->protocols.TAKE_FOCUS)
				mode = WFM_LOCALLY_ACTIVE;
			else
				mode = WFM_PASSIVE;
		} else {
			if (wwin->protocols.TAKE_FOCUS)
				mode = WFM_GLOBALLY_ACTIVE;
			else
				mode = WFM_NO_INPUT;
		}
	} else {
		mode = WFM_PASSIVE;
	}
	return mode;
}

void wWindowSetKeyGrabs(WWindow * wwin)
{
	int i;
	WShortKey *key;

	for (i = 0; i < WKBD_LAST; i++) {
		key = &wKeyBindings[i];

		if (key->keycode == 0)
			continue;

		/* WKBD_KEYCHAIN_CANCEL is only meaningful while inside an active key chain */
		if (i == WKBD_KEYCHAIN_CANCEL)
			continue;

		if (key->modifier != XCB_MOD_MASK_ANY) {
			wXGrabKey(wdisp->conn, key->keycode, key->modifier | XCB_MOD_MASK_LOCK,
				 wwin->frame->core->window, true, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
#ifdef NUMLOCK_HACK
			/* Also grab all modifier combinations possible that include,
			 * LockMask, ScrollLockMask and NumLockMask, so that keygrabs
			 * work even if the NumLock/ScrollLock key is on.
			 */
			wHackedGrabKey(key->keycode, key->modifier,
				       wwin->frame->core->window, true, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
#endif
		}
		wXGrabKey(wdisp->conn, key->keycode, key->modifier,
			 wwin->frame->core->window, true, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
	}

	wRootMenuBindShortcuts(wwin->frame->core->window);
}

void wWindowResetMouseGrabs(WWindow * wwin)
{
	/* Mouse grabs can't be done on the client window because of
	 * ICCCM and because clients that try to do the same will crash.
	 *
	 * But there is a problem which makes tbar buttons of unfocused
	 * windows not usable as the click goes to the frame window instead
	 * of the button itself. Must figure a way to fix that.
	 */

	wXUngrabButton(wdisp->conn, XCB_BUTTON_INDEX_ANY, XCB_MOD_MASK_ANY, wwin->client_win);

	if (!WFLAGP(wwin, no_bind_mouse)) {
		/* grabs for Meta+drag */
		wHackedGrabButton(XCB_BUTTON_INDEX_ANY, MOD_MASK, wwin->client_win,
				  true, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE,
				  XCB_GRAB_MODE_SYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE);

		/* for CTRL+Wheel to Scroll Horiz, we have to grab CTRL as well
		 * but we only grab it for Button4 and Button 5 since a lot of apps
		 * use CTRL+Button1-3 for app related functionality
		 */
		if (w_global.config->resize_increment > 0) {
			wHackedGrabButton(XCB_BUTTON_INDEX_4, XCB_MOD_MASK_CONTROL, wwin->client_win,
							  true, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE,
							  XCB_GRAB_MODE_SYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE);
			wHackedGrabButton(XCB_BUTTON_INDEX_5, XCB_MOD_MASK_CONTROL, wwin->client_win,
							  true, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE,
							  XCB_GRAB_MODE_SYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE);

			wHackedGrabButton(XCB_BUTTON_INDEX_4, MOD_MASK | XCB_MOD_MASK_CONTROL, wwin->client_win,
							  true, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE,
							  XCB_GRAB_MODE_SYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE);
			wHackedGrabButton(XCB_BUTTON_INDEX_5, MOD_MASK | XCB_MOD_MASK_CONTROL, wwin->client_win,
							  true, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE,
							  XCB_GRAB_MODE_SYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE);
		}
	}

	if (!wwin->flags.focused && !WFLAGP(wwin, no_focusable)
	    && !wwin->flags.is_gnustep) {
		/* the passive grabs to focus the window */
		/* if (w_global.config->focus_mode == WKF_CLICK) */
		wXGrabButton(wdisp->conn, XCB_BUTTON_INDEX_ANY, XCB_MOD_MASK_ANY, wwin->client_win,
			    true, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE, XCB_GRAB_MODE_SYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE);
	}
	wXFlush(wdisp->conn);
}

void wWindowUpdateGNUstepAttr(WWindow * wwin, GNUstepWMAttributes * attr)
{
	if (attr->flags & GSExtraFlagsAttr) {
		if (MGFLAGP(wwin, broken_close) != (attr->extra_flags & GSDocumentEditedFlag)) {
			wwin->client_flags.broken_close = !MGFLAGP(wwin, broken_close);
			wWindowUpdateButtonImages(wwin);
		}
	}
}

WMagicNumber wWindowAddSavedState(const char *instance, const char *class,
				const char *command, pid_t pid, WSavedState *state)
{
	WWindowState *wstate;

	wstate = malloc(sizeof(WWindowState));
	if (!wstate)
		return NULL;

	memset(wstate, 0, sizeof(WWindowState));
	wstate->pid = pid;
	if (instance)
		wstate->instance = wstrdup(instance);
	if (class)
		wstate->class = wstrdup(class);
	if (command)
		wstate->command = wstrdup(command);
	wstate->state = state;

	wstate->next = windowState;
	windowState = wstate;

	return wstate;
}

static void splitCommandForMatch(const char *command, char ***argv, int *argc)
{
	char *copy, *single_command;

	copy = wstrdup(command);
	wtokensplit(copy, argv, argc);
	wfree(copy);

	/* A sole whitespace-bearing token is normally an executable path.
	 * Launchers that instead put the whole command line in one WM_COMMAND
	 * element leave a non-executable string, so unpack only that form. */
	if (*argc == 1 && strchr((*argv)[0], ' ') != NULL &&
	    access((*argv)[0], X_OK) != 0) {
		single_command = wstrdup((*argv)[0]);
		wtokenfree(*argv, *argc);
		*argv = NULL;
		wtokensplit(single_command, argv, argc);
		wfree(single_command);
	}
}

bool wWindowCommandsMatch(const char *saved, const char *current)
{
	char **saved_argv = NULL, **current_argv = NULL;
	int saved_argc, current_argc, i;
	bool match = false;

	if (!saved || !current)
		return WMStrEqual(saved, current);

	if (WMStrEqual(saved, current))
		return true;

	splitCommandForMatch(saved, &saved_argv, &saved_argc);
	splitCommandForMatch(current, &current_argv, &current_argc);

	if (saved_argc != current_argc)
		goto cleanup;

	match = true;
	for (i = 0; i < saved_argc; i++) {
		if (!WMStrEqual(saved_argv[i], current_argv[i])) {
			match = false;
			break;
		}
	}

cleanup:
	wtokenfree(saved_argv, saved_argc);
	wtokenfree(current_argv, current_argc);
	return match;
}

WMagicNumber wWindowGetSavedState(xcb_window_t win)
{
	char *instance, *class, *command = NULL;
	WWindowState *wstate = windowState;

	if (!wstate)
		return NULL;

	command = GetCommandForWindow(win);
	if (!command)
		return NULL;

	if (PropGetWMClass(win, &class, &instance)) {
		while (wstate) {
			if (WMStrEqual(instance, wstate->instance) &&
			    WMStrEqual(class, wstate->class) &&
			    wWindowCommandsMatch(wstate->command, command)) {
				break;
			}
			wstate = wstate->next;
		}
	} else {
		wstate = NULL;
	}

	if (command)
		wfree(command);
	if (instance)
		wunintern(instance);
	if (class)
		wunintern(class);

	return wstate;
}

void wWindowDeleteSavedState(WMagicNumber id)
{
	WWindowState *tmp, *wstate = (WWindowState *) id;

	if (!wstate || !windowState)
		return;

	tmp = windowState;
	if (tmp == wstate) {
		windowState = wstate->next;
		release_wwindowstate(wstate);
	} else {
		while (tmp->next) {
			if (tmp->next == wstate) {
				tmp->next = wstate->next;
				release_wwindowstate(wstate);
				break;
			}
			tmp = tmp->next;
		}
	}
}

void wWindowDeleteSavedStatesForPID(pid_t pid)
{
	WWindowState *tmp, *wstate;

	if (!windowState)
		return;

	tmp = windowState;
	if (tmp->pid == pid) {
		wstate = windowState;
		windowState = tmp->next;

		release_wwindowstate(wstate);
	} else {
		while (tmp->next) {
			if (tmp->next->pid == pid) {
				wstate = tmp->next;
				tmp->next = wstate->next;
				release_wwindowstate(wstate);
				break;
			}
			tmp = tmp->next;
		}
	}
}

void wWindowDeleteUnmatchedSavedStates(void)
{
	WWindowState *wstate, *next;

	wstate = windowState;
	while (wstate) {
		next = wstate->next;
		if (wstate->pid == 0)
			wWindowDeleteSavedState(wstate);
		wstate = next;
	}
}

static void release_wwindowstate(WWindowState *wstate)
{
	if (wstate->instance)
		wfree(wstate->instance);

	if (wstate->class)
		wfree(wstate->class);

	if (wstate->command)
		wfree(wstate->command);

	if (wstate->state && wstate->state->mark_key)
		wfree(wstate->state->mark_key);

	wfree(wstate->state);
	wfree(wstate);
}

void wWindowSetOmnipresent(WWindow *wwin, bool flag)
{
	uint64_t mask;

	if (!wwin->frame)
		return;
	if (!flag && WFLAGP(wwin, omnipresent))
		return;
	mask = flag ? WS_ALL : WS_BIT(wwin->frame->workspace);
	if (wwin->frame->ws_mask == mask)
		return;

	wwin->frame->ws_mask = mask;
	if (wwin->tab_group)
		wGroupSyncWorkspace(wwin);
	WMPostNotificationName(WMNChangedState, wwin, "omnipresent");
	wAutoRemoveEmptyWorkspaces(wwin->screen_ptr);
}

void wWindowSetMark(WWindow *wwin, const char *label)
{
	/* Remove any previous mark first */
	if (wwin->mark_key_label != NULL)
		wWindowUnsetMark(wwin);

	wwin->mark_key_label = wstrdup(label);

	UpdateSwitchMenu(wwin->screen_ptr, wwin, ACTION_CHANGE);
}

/* Sanitize a mark label for safe log output by replacing
 * non-printable characters with '?'. */
static void log_mark_label(char *dst, const char *src, size_t n)
{
	size_t i;

	for (i = 0; i + 1 < n && src[i]; i++)
		dst[i] = iscntrl((unsigned char)src[i]) ? '?' : src[i];
	dst[i] = '\0';
}

void wWindowUnsetMark(WWindow *wwin)
{
	if (wwin->mark_key_label == NULL)
		return;

	wfree(wwin->mark_key_label);
	wwin->mark_key_label = NULL;

	UpdateSwitchMenu(wwin->screen_ptr, wwin, ACTION_CHANGE);
}

void wWindowStealMark(WWindow *wwin, const char *label)
{
	WScreen *scr = wwin->screen_ptr;
	WWindow *tw;

	/* At manage time wwin is not yet linked into scr->focused_window
	 * (the link-up happens later in wManageWindow), so this scan never
	 * finds wwin itself. Walk the entire list: the global-uniqueness
	 * invariant should leave at most one holder, but the scan must
	 * not assume it. */
	tw = scr->focused_window;
	while (tw) {
		if (tw != wwin && tw->mark_key_label != NULL &&
		    strcmp(tw->mark_key_label, label) == 0) {
			char safe_lbl[128];

			log_mark_label(safe_lbl, label, sizeof(safe_lbl));
			wwarning("window mark: label '%s' moved from %s.%s (0x%lx) "
				 "to %s.%s (0x%lx)", safe_lbl,
				 tw->wm_instance ? tw->wm_instance : "",
				 tw->wm_class ? tw->wm_class : "",
				 (unsigned long)tw->client_win,
				 wwin->wm_instance ? wwin->wm_instance : "",
				 wwin->wm_class ? wwin->wm_class : "",
				 (unsigned long)wwin->client_win);
			wWindowUnsetMark(tw);
		}
		tw = tw->prev;
	}

	wWindowSetMark(wwin, label);
}


static void resizebarMouseDown(WCoreWindow *sender, void *data, xcb_generic_event_t *event)
{
	WWindow *wwin = data;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) sender;

#ifndef NUMLOCK_HACK
	if ((((xcb_button_press_event_t *)event)->state & ValidModMask)
	    != (((xcb_button_press_event_t *)event)->state & ~XCB_MOD_MASK_LOCK)) {
		wwarning(_("The NumLock, ScrollLock or similar key seems to be turned on. "
			   "Turn it off or some mouse actions and keyboard shortcuts will not work."));
	}
#endif

	((xcb_button_press_event_t *)event)->state &= w_global.shortcut.modifiers_mask;

	CloseWindowMenu(wwin->screen_ptr);

	if (w_global.config->focus_mode == WKF_CLICK && !(((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_CONTROL)
	    && !WFLAGP(wwin, no_focusable)) {
		wSetFocusTo(wwin->screen_ptr, wwin);
	}

	if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_1)
		wRaiseFrame(wwin->frame->core);

	if (((xcb_button_press_event_t *)event)->event != wwin->frame->resizebar->window) {
		if (wXGrabPointer(wdisp->conn, wwin->frame->resizebar->window, true,
				 XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS,
				 XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE, XCB_CURRENT_TIME) != WXGrabSuccess) {
			return;
		}
	}

	if (((xcb_button_press_event_t *)event)->state & MOD_MASK) {
		/* move the window */
		wMouseMoveWindow(wwin, event);
		wXUngrabPointer(wdisp->conn, XCB_CURRENT_TIME);
	} else {
		wMouseResizeWindow(wwin, event);
		wXUngrabPointer(wdisp->conn, XCB_CURRENT_TIME);
	}
}

static void titlebarDblClick(WCoreWindow *sender, void *data, xcb_generic_event_t *event)
{
	WWindow *wwin = data;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) sender;

	((xcb_button_press_event_t *)event)->state &= w_global.shortcut.modifiers_mask;

	if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_1 ) {
		if (((xcb_button_press_event_t *)event)->state == 0 ) {
			if (!WFLAGP(wwin, no_shadeable) && !w_global.config->double_click_maximize) {
				/* shade window */
				if (wwin->flags.shaded)
					wUnshadeWindow(wwin);
				else
					wShadeWindow(wwin);
			}
		}

		if (w_global.config->double_click_maximize) {
			int dir = 0;

			if (((xcb_button_press_event_t *)event)->state == 0) {
				/* maximize window in both directions */
				dir |= (MAX_VERTICAL|MAX_HORIZONTAL);
				int ndir = dir ^ wwin->flags.maximized;
				if (ndir != 0)
					wMaximizeWindow(wwin, ndir, wGetHeadForWindow(wwin));
				else
					wUnmaximizeWindow(wwin);
			}
		} else {
			int dir = 0;

			if (((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_CONTROL)
				dir |= MAX_VERTICAL;

			if (((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_SHIFT) {
				dir |= MAX_HORIZONTAL;
				if (!(((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_CONTROL))
					wSelectWindow(wwin, !wwin->flags.selected);
			}

			/* maximize window */
			if (dir != 0 && IS_RESIZABLE(wwin)) {
				int ndir = dir ^ wwin->flags.maximized;

				if (ndir != 0)
					wMaximizeWindow(wwin, ndir, wGetHeadForWindow(wwin));
				else
					wUnmaximizeWindow(wwin);
			}
		}
	} else if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_3) {
		if (((xcb_button_press_event_t *)event)->state & MOD_MASK)
			wHideOtherApplications(wwin);
	} else if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_2) {
		wSelectWindow(wwin, !wwin->flags.selected);
	} else if (((xcb_button_press_event_t *)event)->detail == W_getconf_mouseWheelUp()) {
		if (wwin->tab_group)
			wGroupTabPrev(wwin->screen_ptr);
		else
			wShadeWindow(wwin);
	} else if (((xcb_button_press_event_t *)event)->detail == W_getconf_mouseWheelDown()) {
		if (wwin->tab_group)
			wGroupTabNext(wwin->screen_ptr);
		else
			wUnshadeWindow(wwin);
	}
}

static void frameMouseDown(WObjDescriptor *desc, xcb_generic_event_t *event)
{
	WWindow *wwin = desc->parent;
	unsigned int new_width, w_scale;
	unsigned int new_height, h_scale;
	unsigned int resize_width_increment = 0;
	unsigned int resize_height_increment = 0;

	if (wwin->normal_hints) {
		w_scale = (w_global.config->resize_increment + wwin->normal_hints->width_inc - 1) / wwin->normal_hints->width_inc;
		h_scale = (w_global.config->resize_increment + wwin->normal_hints->height_inc - 1) / wwin->normal_hints->height_inc;
		resize_width_increment = wwin->normal_hints->width_inc * w_scale;
		resize_height_increment = wwin->normal_hints->height_inc * h_scale;
	}
	if (resize_width_increment <= 1 && resize_height_increment <= 1) {
		resize_width_increment = w_global.config->resize_increment;
		resize_height_increment = w_global.config->resize_increment;
	}

	((xcb_button_press_event_t *)event)->state &= w_global.shortcut.modifiers_mask;

	CloseWindowMenu(wwin->screen_ptr);

	if ((w_global.config->mouse_wheel_focus || (((xcb_button_press_event_t *)event)->detail != XCB_BUTTON_INDEX_4 && ((xcb_button_press_event_t *)event)->detail != XCB_BUTTON_INDEX_5)) && !(((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_CONTROL) && !WFLAGP(wwin, no_focusable))
		wSetFocusTo(wwin->screen_ptr, wwin);

	if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_1)
		wRaiseFrame(wwin->frame->core);

	if (((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_CONTROL) {
		if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_4) {
			new_width = wwin->client.width - resize_width_increment;
			wWindowConstrainSize(wwin, &new_width, &wwin->client.height);
			wWindowConfigure(wwin, wwin->frame_x, wwin->frame_y, new_width, wwin->client.height);
		}
		if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_5) {
			new_width = wwin->client.width + resize_width_increment;
			wWindowConstrainSize(wwin, &new_width, &wwin->client.height);
			wWindowConfigure(wwin, wwin->frame_x, wwin->frame_y, new_width, wwin->client.height);
		}
	}

	if (((xcb_button_press_event_t *)event)->state & MOD_MASK) {
		/* move the window */
		if (wXGrabPointer(wdisp->conn, wwin->client_win, false,
				 XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS,
				 XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE, XCB_CURRENT_TIME) != WXGrabSuccess) {
			return;
		}
		if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_3) {
			wMouseResizeWindow(wwin, event);
		} else if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_4) {
			new_height = wwin->client.height - resize_height_increment;
			wWindowConstrainSize(wwin, &wwin->client.width, &new_height);
			wWindowConfigure(wwin, wwin->frame_x, wwin->frame_y, wwin->client.width, new_height);
		} else if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_5) {
			new_height = wwin->client.height + resize_height_increment;
			wWindowConstrainSize(wwin, &wwin->client.width, &new_height);
			wWindowConfigure(wwin, wwin->frame_x, wwin->frame_y, wwin->client.width, new_height);
		} else if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_1 || ((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_2) {
			wMouseMoveWindow(wwin, event);
		}
		wXUngrabPointer(wdisp->conn, XCB_CURRENT_TIME);
	}
}

static void titlebarMouseDown(WCoreWindow *sender, void *data, xcb_generic_event_t *event)
{
	WWindow *wwin = (WWindow *) data;
	WWindow *menu_for = wwin;
	int seg = -1;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) sender;

#ifndef NUMLOCK_HACK
	if ((((xcb_button_press_event_t *)event)->state & ValidModMask) != (((xcb_button_press_event_t *)event)->state & ~XCB_MOD_MASK_LOCK))
		wwarning(_("The NumLock, ScrollLock or similar key seems to be turned on. "
			   "Turn it off or some mouse actions and keyboard shortcuts will not work."));
#endif
	((xcb_button_press_event_t *)event)->state &= w_global.shortcut.modifiers_mask;

	CloseWindowMenu(wwin->screen_ptr);

	/*
	 * Tab-group segmented titlebar hit-test. tab_seg_count is non-zero
	 * only when wFrameWindowPaint() drew segments for this frame, so a
	 * stale array on an unmapped peer or a TabsInTitlebar = NO frame
	 * is never consulted.
	 */
	if (wwin->tab_group && wwin->frame->tab_seg_count > 0
	    && ((xcb_button_press_event_t *)event)->event == wwin->frame->titlebar->window)
		seg = wFrameWindowTabSegmentAt(wwin->frame, ((xcb_button_press_event_t *)event)->event_x);

	/* wheel over a grouped titlebar cycles tabs, ungrouped windows
	 * keep the existing shade/unshade behaviour in the dblclick path */
	if (wwin->tab_group
	    && (((xcb_button_press_event_t *)event)->detail == W_getconf_mouseWheelUp()
		|| ((xcb_button_press_event_t *)event)->detail == W_getconf_mouseWheelDown())) {
		if (((xcb_button_press_event_t *)event)->detail == W_getconf_mouseWheelUp())
			wGroupTabPrev(wwin->screen_ptr);
		else
			wGroupTabNext(wwin->screen_ptr);
		return;
	}

	/* Button2 on a segment starts a tab drag with that member as the
	 * payload, on a non-segmented titlebar Button2 still drags the
	 * whole window so a singleton can be dropped onto another frame */
	if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_2
	    && !(((xcb_button_press_event_t *)event)->state & (MOD_MASK | XCB_MOD_MASK_SHIFT | XCB_MOD_MASK_CONTROL))
	    && wGroupIsTabable(wwin)) {
		WWindow *payload = wwin;

		if (seg >= 0 && seg < wwin->tab_group->member_count)
			payload = wwin->tab_group->members[seg];
		wGroupDragTab(wwin, payload, event);
		return;
	}

	if (seg >= 0 && ((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_1) {
		unsigned int tdm = wdisp->tab_drag_modifier_mask;

		if (((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_CONTROL) {
			/* close this member only, do not activate it first */
			wGroupCloseMember(wwin->tab_group, seg);
			return;
		}
		/* TabDragModifier + Button1 on any segment, including the
		 * active one, starts a tab drag. Tested before the active
		 * segment fall-through so it wins over whole-window move. */
		if (tdm != 0 && (((xcb_button_press_event_t *)event)->state & tdm) == tdm
		    && seg < wwin->tab_group->member_count) {
			wGroupDragTab(wwin, wwin->tab_group->members[seg], event);
			return;
		}
		if (seg != wwin->tab_group->active_index
		    && !(((xcb_button_press_event_t *)event)->state & (MOD_MASK | XCB_MOD_MASK_SHIFT))) {
			/* Button1 on an inactive segment: run the drag loop
			 * with that member as payload. When the press is
			 * released without crossing MOVE_THRESHOLD the loop
			 * returns False and we treat it as a plain click,
			 * activating the segment and raising the frame. */
			if (wGroupDragTab(wwin, wwin->tab_group->members[seg], event))
				return;
			if (!wwin->tab_group || seg >= wwin->tab_group->member_count)
				return;
			wGroupSetActive(wwin->tab_group, seg);
			if (wwin->tab_group
			    && wwin->tab_group->members[wwin->tab_group->active_index]->frame)
				wRaiseFrame(wwin->tab_group->members[wwin->tab_group->active_index]->frame->core);
			return;
		}
		/* active segment, or modifier held, continue to the
		 * existing move/raise/select path below */
	}

	if (w_global.config->focus_mode == WKF_CLICK && !(((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_CONTROL)
	    && !WFLAGP(wwin, no_focusable))
		wSetFocusTo(wwin->screen_ptr, wwin);

	if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_1 || ((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_2) {

		if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_1) {
			if (((xcb_button_press_event_t *)event)->state & MOD_MASK)
				wLowerFrame(wwin->frame->core);
			else
				wRaiseFrame(wwin->frame->core);
		}
		if ((((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_SHIFT)
		    && !(((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_CONTROL)) {
			wSelectWindow(wwin, !wwin->flags.selected);
			return;
		}
		if (((xcb_button_press_event_t *)event)->event != wwin->frame->titlebar->window
		    && wXGrabPointer(wdisp->conn, wwin->frame->titlebar->window, false,
				    XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS,
				    XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE, XCB_CURRENT_TIME) != WXGrabSuccess) {
			return;
		}

		/* move the window */
		wMouseMoveWindow(wwin, event);

		wXUngrabPointer(wdisp->conn, XCB_CURRENT_TIME);
	} else if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_3 && ((xcb_button_press_event_t *)event)->state == 0
		   && !wwin->flags.internal_window && !WCHECK_STATE(WSTATE_MODAL)) {
		WObjDescriptor *desc;

		if (((xcb_button_press_event_t *)event)->event != wwin->frame->titlebar->window
		    && wXGrabPointer(wdisp->conn, wwin->frame->titlebar->window, false,
				    XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS,
				    XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE, XCB_CURRENT_TIME) != WXGrabSuccess) {
			return;
		}

		/* Button3 on a segment opens the window menu for that
		 * member rather than the active one so per-tab Close,
		 * Kill and Detach are reachable without switching first */
		if (seg >= 0 && seg < wwin->tab_group->member_count)
			menu_for = wwin->tab_group->members[seg];

		OpenWindowMenu(menu_for, ((xcb_button_press_event_t *)event)->root_x, wwin->frame_y + wwin->frame->top_width, false);

		/* allow drag select */
		if (!wwin->screen_ptr->window_menu || !wwin->screen_ptr->window_menu->menu) {
			wXUngrabPointer(wdisp->conn, XCB_CURRENT_TIME);
			return;
		}
		desc = &wwin->screen_ptr->window_menu->menu->descriptor;
		event->response_type |= 0x80;
		(*desc->handle_mousedown) (desc, event);

		wXUngrabPointer(wdisp->conn, XCB_CURRENT_TIME);
	}
}

static void windowCloseClick(WCoreWindow *sender, void *data, xcb_generic_event_t *event)
{
	WWindow *wwin = data;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) sender;

	((xcb_button_press_event_t *)event)->state &= w_global.shortcut.modifiers_mask;

	CloseWindowMenu(wwin->screen_ptr);

	if (((xcb_button_press_event_t *)event)->detail < XCB_BUTTON_INDEX_1 || ((xcb_button_press_event_t *)event)->detail > XCB_BUTTON_INDEX_3)
		return;

	/* shift-click on a tab-grouped window closes every member */
	if ((((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_SHIFT) && wwin->tab_group) {
		wGroupCloseAll(wwin->tab_group);
		return;
	}

	/* if control-click, kill the client */
	if (((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_CONTROL) {
		wClientKill(wwin);
	} else {
		if (wwin->protocols.DELETE_WINDOW && ((xcb_button_press_event_t *)event)->state == 0) {
			/* send delete message */
			wClientSendProtocol(wwin, wdisp->atom.wm.delete_window,
									  wdisp->timestamp.last_event);
			wNETWMPingWindow(wwin, wdisp->timestamp.last_event);
		}
	}
}

static void windowCloseDblClick(WCoreWindow *sender, void *data, xcb_generic_event_t *event)
{
	WWindow *wwin = data;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) sender;

	CloseWindowMenu(wwin->screen_ptr);

	if (((xcb_button_press_event_t *)event)->detail < XCB_BUTTON_INDEX_1 || ((xcb_button_press_event_t *)event)->detail > XCB_BUTTON_INDEX_3)
		return;

	/* send delete message */
	if (wwin->protocols.DELETE_WINDOW) {
		wClientSendProtocol(wwin, wdisp->atom.wm.delete_window,
								  wdisp->timestamp.last_event);
		wNETWMPingWindow(wwin, wdisp->timestamp.last_event);
	} else {
		wClientKill(wwin);
	}
}

#ifdef XKB_BUTTON_HINT
/* Helper function to extract the 2-letter language code for a given XKB group index */
void wWindowGetLanguageLabel(int group_index, char *label)
{
	char *prop, *end, *layout, *tok;
	size_t len, f;
	int g;

	/* Default to empty - will fallback to pixmap if we can't get the label */
	label[0] = '\0';

	/* _XKB_RULES_NAMES on the default root is a STRING property holding
	 * five NUL-separated fields, rules\0model\0layout\0variant\0options,
	 * walk to the third field then take the group_index-th comma token */
	prop = WXGetLatin1String(wdisp->conn, wXRootWindow(wdisp->conn, wdisp->default_screen),
				 wx_static_atoms[W_ATOM_XKB_RULES_NAMES], &len);
	if (!prop)
		return;
	end = prop + len;

	layout = prop;
	for (f = 0; f < 2 && layout < end; f++)
		layout += strlen(layout) + 1;

	if (layout < end && *layout) {
		tok = layout;
		for (g = 0; g < group_index && tok; g++) {
			tok = strchr(tok, ',');
			if (tok)
				tok++;
		}
		if (tok && *tok && *tok != ',') {
			label[0] = (char) toupper((unsigned char) tok[0]);
			label[1] = (tok[1] && tok[1] != ',')
				   ? (char) tolower((unsigned char) tok[1]) : '\0';
			label[2] = '\0';
		}
	}
	wfree(prop);
}

static void windowLanguageClick(WCoreWindow *sender, void *data, xcb_generic_event_t *event)
{
	WWindow *wwin = data;
	WFrameWindow *fwin = wwin->frame;
	WScreen *scr = fwin->screen_ptr;
	int tl;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) sender;

	if (((xcb_button_press_event_t *)event)->detail != XCB_BUTTON_INDEX_1 && ((xcb_button_press_event_t *)event)->detail != XCB_BUTTON_INDEX_3)
		return;
	tl = wwin->frame->languagemode;

	/* advance to the next configured XKB layout, the keymap loaded by
	 * wXkbInit() already knows how many groups exist and their names
	 * so this is a local lookup with no round-trip */
	int n = wXkbNumLayouts();
	int newgroup = -1;
	if (n > 1) {
		int i;
		for (i = 1; i <= n; i++) {
			int cand = (tl + i) % n;
			if (wXkbLayoutName(cand)) {
				newgroup = cand;
				break;
			}
		}
	}

	if (newgroup >= 0) {
		wwin->frame->last_languagemode = tl;
		wwin->frame->languagemode = newgroup;
		wXkbLockGroup(wwin->frame->languagemode);
	} else {
		/* fallback to previous toggle behaviour for setups with only
		 * one group or when group info is not available */
		wwin->frame->languagemode = wwin->frame->last_languagemode;
		wwin->frame->last_languagemode = tl;
		wXkbLockGroup(wwin->frame->languagemode);
	}
	/* Update label */
	wWindowGetLanguageLabel(wwin->frame->languagemode, wwin->frame->language_label);

	wSetFocusTo(scr, wwin);
	wFrameWindowUpdateLanguageButton(wwin->frame);
	if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_3)
		return;
	wRaiseFrame(fwin->core);
}
#endif

static void windowMaximizeClick(WCoreWindow *sender, void *data, xcb_generic_event_t *event)
{
	WWindow *wwin = data;

	(void) sender;

	((xcb_button_press_event_t *)event)->state &= w_global.shortcut.modifiers_mask;

	CloseWindowMenu(wwin->screen_ptr);

	if (((xcb_button_press_event_t *)event)->detail != XCB_BUTTON_INDEX_1)
		return;

	if (((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_CONTROL) {
		/* control-click is the stronger action, same convention as
		 * close (kill) and miniaturize (hide app), fullscreen strips
		 * the titlebar so the way back is the keybinding or the
		 * window menu */
		if (wwin->flags.fullscreen)
			wUnfullscreenWindow(wwin);
		else
			wFullscreenWindow(wwin);
	} else if (((xcb_button_press_event_t *)event)->state == 0) {
		handleMaximize(wwin, MAX_HORIZONTAL | MAX_VERTICAL);
	}
}

void wWindowRefreshTitlebarIcon(WWindow *wwin)
{
	RImage *img;

	if (!wwin->frame
	    || wwin->screen_ptr->titlebar_layout.side[WTB_ICON] == WTB_SIDE_NONE)
		return;

	if (!wwin->frame->titlebar || wwin->frame->top_width <= 4) {
		/* nothing to decode either way, when !titlebar the
		 * setter frees+hides and returns without redraw, when
		 * top_width <= 4 with a live titlebar it redraws */
		wFrameWindowSetIcon(wwin->frame, NULL);
		return;
	}

	img = wWindowGetIconImage(wwin);
	wFrameWindowSetIcon(wwin->frame, img);
	if (img)
		RReleaseImage(img);
}

static void windowIconifyClick(WCoreWindow *sender, void *data, xcb_generic_event_t *event)
{
	WWindow *wwin = data;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) sender;

	((xcb_button_press_event_t *)event)->state &= w_global.shortcut.modifiers_mask;

	CloseWindowMenu(wwin->screen_ptr);

	if (((xcb_button_press_event_t *)event)->detail < XCB_BUTTON_INDEX_1 || ((xcb_button_press_event_t *)event)->detail > XCB_BUTTON_INDEX_3)
		return;

	if ((((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_CONTROL) || (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_3)) {
		wRequestHideApplication(wApplicationOf(wwin->main_window), wwin);
	} else if (((xcb_button_press_event_t *)event)->state == 0) {
		if (wwin->protocols.MINIATURIZE_WINDOW)
			wClientSendProtocol(wwin, wdisp->atom.gnustep.wm_miniaturize_window,
					    wdisp->timestamp.last_event);
		else
			wIconifyWindow(wwin);
	}
}
