/*  wmspec.c-- support for the wm-spec Hints
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1998-2003 Alfredo K. Kojima
 *  Copyright (c) 2009-2026 Window Maker Team
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

/*
 * All property reads/writes go through the typed WXProp helper layer
 * (WINGs/WXProp.h), which validates type/format/count and normalises the
 * Xlib long[] representation to uint32_t.
 */

#include "wconfig.h"

#ifdef USE_XSYNC
#include <xcb/sync.h>
#endif
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#include <WINGs/WUtil.h>
#include <WINGs/WXProp.h>
#include "WindowMaker.h"
#include "window.h"
#include "screen.h"
#include "workspace.h"
#include "vdesk.h"
#include "framewin.h"
#include "actions.h"
#include "client.h"
#include "appicon.h"
#include "dock.h"
#include "wmspec.h"
#include "icon.h"
#include "stacking.h"
#include "tabgroup.h"
#include "xinerama.h"
#include "misc.h"
#include "properties.h"
#include "dialog.h"
#include "startup_notify.h"


/* Cap on the number of _NET_WM_WINDOW_TYPE atoms resolved via
 * wXGetAtomName() per window. The property reader allows up to 16 MiB
 * of atoms, so without this cap a hostile client can stall the WM
 * with millions of synchronous round trips at manage time. */
#define NET_WM_WINDOW_TYPE_MAX 32


/* Root Window Properties */
static xcb_atom_t net_supported;
static xcb_atom_t net_client_list;
static xcb_atom_t net_client_list_stacking;
static xcb_atom_t net_number_of_desktops;
static xcb_atom_t net_desktop_geometry;
static xcb_atom_t net_desktop_viewport;
static xcb_atom_t net_current_desktop;
static xcb_atom_t net_desktop_names;
static xcb_atom_t net_active_window;
static xcb_atom_t net_workarea;
static xcb_atom_t net_supporting_wm_check;
static xcb_atom_t net_virtual_roots;	/* N/A */
static xcb_atom_t net_desktop_layout;
static xcb_atom_t net_showing_desktop;

/* Other Root Window Messages */
static xcb_atom_t net_close_window;
static xcb_atom_t net_moveresize_window;
static xcb_atom_t net_wm_moveresize;
static xcb_atom_t net_restack_window;
static xcb_atom_t net_request_frame_extents;

/* Application Window Properties */
static xcb_atom_t net_wm_name;
static xcb_atom_t net_wm_visible_name;
static xcb_atom_t net_wm_icon_name;
static xcb_atom_t net_wm_visible_icon_name;
static xcb_atom_t net_wm_desktop;
static xcb_atom_t net_wm_fullscreen_monitors;
static xcb_atom_t net_wm_window_type;
static xcb_atom_t net_wm_window_type_desktop;
static xcb_atom_t net_wm_window_type_dock;
static xcb_atom_t net_wm_window_type_toolbar;
static xcb_atom_t net_wm_window_type_menu;
static xcb_atom_t net_wm_window_type_utility;
static xcb_atom_t net_wm_window_type_splash;
static xcb_atom_t net_wm_window_type_dialog;
static xcb_atom_t net_wm_window_type_dropdown_menu;
static xcb_atom_t net_wm_window_type_popup_menu;
static xcb_atom_t net_wm_window_type_tooltip;
static xcb_atom_t net_wm_window_type_notification;
static xcb_atom_t net_wm_window_type_combo;
static xcb_atom_t net_wm_window_type_dnd;
static xcb_atom_t net_wm_window_type_normal;
static xcb_atom_t net_wm_state;
static xcb_atom_t net_wm_state_modal;
static xcb_atom_t net_wm_state_sticky;
static xcb_atom_t net_wm_state_maximized_vert;
static xcb_atom_t net_wm_state_maximized_horz;
static xcb_atom_t net_wm_state_shaded;
static xcb_atom_t net_wm_state_skip_taskbar;
static xcb_atom_t net_wm_state_skip_pager;
static xcb_atom_t net_wm_state_hidden;
static xcb_atom_t net_wm_state_fullscreen;
static xcb_atom_t net_wm_state_above;
static xcb_atom_t net_wm_state_below;
static xcb_atom_t net_wm_state_focused;
static xcb_atom_t net_wm_state_demands_attention;
static xcb_atom_t net_wm_allowed_actions;
static xcb_atom_t net_wm_action_move;
static xcb_atom_t net_wm_action_resize;
static xcb_atom_t net_wm_action_minimize;
static xcb_atom_t net_wm_action_shade;
static xcb_atom_t net_wm_action_stick;
static xcb_atom_t net_wm_action_maximize_horz;
static xcb_atom_t net_wm_action_maximize_vert;
static xcb_atom_t net_wm_action_fullscreen;
static xcb_atom_t net_wm_action_change_desktop;
static xcb_atom_t net_wm_action_close;
static xcb_atom_t net_wm_action_above;
static xcb_atom_t net_wm_action_below;
static xcb_atom_t net_wm_strut;
static xcb_atom_t net_wm_strut_partial;
static xcb_atom_t net_wm_icon_geometry;
static xcb_atom_t net_wm_icon;
static xcb_atom_t net_wm_pid;
static xcb_atom_t net_wm_handled_icons;
static xcb_atom_t net_wm_user_time;
static xcb_atom_t net_wm_user_time_window;
static xcb_atom_t net_wm_window_opacity;

static xcb_atom_t net_frame_extents;
static xcb_atom_t net_wm_full_placement;

/* Window Manager Protocols */
static xcb_atom_t net_wm_ping;
static xcb_atom_t net_wm_sync_request;
static xcb_atom_t net_wm_sync_request_counter;

static xcb_atom_t utf8_string;
static xcb_atom_t net_startup_id;

/*
 * The EWMH atom set: one row per file static and its shared-table id.
 * Expanded twice, into the fill block in wNETWMInitStuff and into the
 * root-window property wipe in wNETWMCleanup, so the two cannot drift.
 * The shared header owns the atom names.
 */
#define WNET_ATOMS(_) \
	_(net_supported,		W_ATOM_NET_SUPPORTED); \
	_(net_client_list,		W_ATOM_NET_CLIENT_LIST); \
	_(net_client_list_stacking,	W_ATOM_NET_CLIENT_LIST_STACKING); \
	_(net_number_of_desktops,	W_ATOM_NET_NUMBER_OF_DESKTOPS); \
	_(net_desktop_geometry,		W_ATOM_NET_DESKTOP_GEOMETRY); \
	_(net_desktop_viewport,		W_ATOM_NET_DESKTOP_VIEWPORT); \
	_(net_current_desktop,		W_ATOM_NET_CURRENT_DESKTOP); \
	_(net_desktop_names,		W_ATOM_NET_DESKTOP_NAMES); \
	_(net_active_window,		W_ATOM_NET_ACTIVE_WINDOW); \
	_(net_workarea,			W_ATOM_NET_WORKAREA); \
	_(net_supporting_wm_check,	W_ATOM_NET_SUPPORTING_WM_CHECK); \
	_(net_virtual_roots,		W_ATOM_NET_VIRTUAL_ROOTS); \
	_(net_desktop_layout,		W_ATOM_NET_DESKTOP_LAYOUT); \
	_(net_showing_desktop,		W_ATOM_NET_SHOWING_DESKTOP); \
	_(net_close_window,		W_ATOM_NET_CLOSE_WINDOW); \
	_(net_moveresize_window,	W_ATOM_NET_MOVERESIZE_WINDOW); \
	_(net_wm_moveresize,		W_ATOM_NET_WM_MOVERESIZE); \
	_(net_restack_window,		W_ATOM_NET_RESTACK_WINDOW); \
	_(net_request_frame_extents,	W_ATOM_NET_REQUEST_FRAME_EXTENTS); \
	_(net_wm_name,			W_ATOM_NET_WM_NAME); \
	_(net_wm_visible_name,		W_ATOM_NET_WM_VISIBLE_NAME); \
	_(net_wm_icon_name,		W_ATOM_NET_WM_ICON_NAME); \
	_(net_wm_visible_icon_name,	W_ATOM_NET_WM_VISIBLE_ICON_NAME); \
	_(net_wm_desktop,		W_ATOM_NET_WM_DESKTOP); \
	_(net_wm_fullscreen_monitors,	W_ATOM_NET_WM_FULLSCREEN_MONITORS); \
	_(net_wm_window_type,		W_ATOM_NET_WM_WINDOW_TYPE); \
	_(net_wm_window_type_desktop,	W_ATOM_NET_WM_WINDOW_TYPE_DESKTOP); \
	_(net_wm_window_type_dock,	W_ATOM_NET_WM_WINDOW_TYPE_DOCK); \
	_(net_wm_window_type_toolbar,	W_ATOM_NET_WM_WINDOW_TYPE_TOOLBAR); \
	_(net_wm_window_type_menu,	W_ATOM_NET_WM_WINDOW_TYPE_MENU); \
	_(net_wm_window_type_utility,	W_ATOM_NET_WM_WINDOW_TYPE_UTILITY); \
	_(net_wm_window_type_splash,	W_ATOM_NET_WM_WINDOW_TYPE_SPLASH); \
	_(net_wm_window_type_dialog,	W_ATOM_NET_WM_WINDOW_TYPE_DIALOG); \
	_(net_wm_window_type_dropdown_menu, W_ATOM_NET_WM_WINDOW_TYPE_DROPDOWN_MENU); \
	_(net_wm_window_type_popup_menu, W_ATOM_NET_WM_WINDOW_TYPE_POPUP_MENU); \
	_(net_wm_window_type_tooltip,	W_ATOM_NET_WM_WINDOW_TYPE_TOOLTIP); \
	_(net_wm_window_type_notification, W_ATOM_NET_WM_WINDOW_TYPE_NOTIFICATION); \
	_(net_wm_window_type_combo,	W_ATOM_NET_WM_WINDOW_TYPE_COMBO); \
	_(net_wm_window_type_dnd,	W_ATOM_NET_WM_WINDOW_TYPE_DND); \
	_(net_wm_window_type_normal,	W_ATOM_NET_WM_WINDOW_TYPE_NORMAL); \
	_(net_wm_state,			W_ATOM_NET_WM_STATE); \
	_(net_wm_state_modal,		W_ATOM_NET_WM_STATE_MODAL); \
	_(net_wm_state_sticky,		W_ATOM_NET_WM_STATE_STICKY); \
	_(net_wm_state_maximized_vert,	W_ATOM_NET_WM_STATE_MAXIMIZED_VERT); \
	_(net_wm_state_maximized_horz,	W_ATOM_NET_WM_STATE_MAXIMIZED_HORZ); \
	_(net_wm_state_shaded,		W_ATOM_NET_WM_STATE_SHADED); \
	_(net_wm_state_skip_taskbar,	W_ATOM_NET_WM_STATE_SKIP_TASKBAR); \
	_(net_wm_state_skip_pager,	W_ATOM_NET_WM_STATE_SKIP_PAGER); \
	_(net_wm_state_hidden,		W_ATOM_NET_WM_STATE_HIDDEN); \
	_(net_wm_state_fullscreen,	W_ATOM_NET_WM_STATE_FULLSCREEN); \
	_(net_wm_state_above,		W_ATOM_NET_WM_STATE_ABOVE); \
	_(net_wm_state_below,		W_ATOM_NET_WM_STATE_BELOW); \
	_(net_wm_state_focused,		W_ATOM_NET_WM_STATE_FOCUSED); \
	_(net_wm_state_demands_attention, W_ATOM_NET_WM_STATE_DEMANDS_ATTENTION); \
	_(net_wm_allowed_actions,	W_ATOM_NET_WM_ALLOWED_ACTIONS); \
	_(net_wm_action_move,		W_ATOM_NET_WM_ACTION_MOVE); \
	_(net_wm_action_resize,		W_ATOM_NET_WM_ACTION_RESIZE); \
	_(net_wm_action_minimize,	W_ATOM_NET_WM_ACTION_MINIMIZE); \
	_(net_wm_action_shade,		W_ATOM_NET_WM_ACTION_SHADE); \
	_(net_wm_action_stick,		W_ATOM_NET_WM_ACTION_STICK); \
	_(net_wm_action_maximize_horz,	W_ATOM_NET_WM_ACTION_MAXIMIZE_HORZ); \
	_(net_wm_action_maximize_vert,	W_ATOM_NET_WM_ACTION_MAXIMIZE_VERT); \
	_(net_wm_action_fullscreen,	W_ATOM_NET_WM_ACTION_FULLSCREEN); \
	_(net_wm_action_change_desktop,	W_ATOM_NET_WM_ACTION_CHANGE_DESKTOP); \
	_(net_wm_action_close,		W_ATOM_NET_WM_ACTION_CLOSE); \
	_(net_wm_action_above,		W_ATOM_NET_WM_ACTION_ABOVE); \
	_(net_wm_action_below,		W_ATOM_NET_WM_ACTION_BELOW); \
	_(net_wm_strut,			W_ATOM_NET_WM_STRUT); \
	_(net_wm_strut_partial,		W_ATOM_NET_WM_STRUT_PARTIAL); \
	_(net_wm_icon_geometry,		W_ATOM_NET_WM_ICON_GEOMETRY); \
	_(net_wm_icon,			W_ATOM_NET_WM_ICON); \
	_(net_wm_pid,			W_ATOM_NET_WM_PID); \
	_(net_wm_handled_icons,		W_ATOM_NET_WM_HANDLED_ICONS); \
	_(net_wm_window_opacity,	W_ATOM_NET_WM_WINDOW_OPACITY); \
	_(net_frame_extents,		W_ATOM_NET_FRAME_EXTENTS); \
	_(net_wm_user_time,		W_ATOM_NET_WM_USER_TIME); \
	_(net_wm_user_time_window,	W_ATOM_NET_WM_USER_TIME_WINDOW); \
	_(net_wm_full_placement,	W_ATOM_NET_WM_FULL_PLACEMENT); \
	_(net_wm_ping,			W_ATOM_NET_WM_PING); \
	_(net_wm_sync_request,		W_ATOM_NET_WM_SYNC_REQUEST); \
	_(net_wm_sync_request_counter,	W_ATOM_NET_WM_SYNC_REQUEST_COUNTER); \
	_(utf8_string,			W_ATOM_UTF8_STRING); \
	_(net_startup_id,		W_ATOM_NET_STARTUP_ID);

#define _NET_WM_STATE_ADD 1
#define _NET_WM_STATE_TOGGLE 2

/* wraparound-safe X timestamp comparison, both args read as 32-bit
 * server time so a-b in 32-bit signed gives the correct ordering across
 * the 49.7-day wrap */
#define USER_TIME_NEWER(a, b) ((int32_t)((uint32_t)(a) - (uint32_t)(b)) > 0)

static void observer(void *self, WMNotification *notif);
static void wsobserver(void *self, WMNotification *notif);

static void updateClientList(WScreen *scr);
static void updateClientListStacking(WScreen *scr, WWindow *);

static void updateWorkspaceNames(WScreen *scr);
static void updateCurrentWorkspace(WScreen *scr);
static void updateWorkspaceCount(WScreen *scr);
static void updateUserTimeWindow(WWindow *wwin, WXCookie *ck);
static void readUserTime(WWindow *wwin);
static void readUserTimeCk(WWindow *wwin, WXCookie ck);
static void cacheLocalPid(WWindow *wwin, WXCookie machine_ck);
static void wNETWMShowingDesktop(WScreen *scr, bool show);

typedef struct NetData {
	WScreen *scr;
	WReservedArea *strut;
	WWindow **show_desktop;
} NetData;

static void setSupportedHints(WScreen *scr)
{
	static const char wm_class[] = "wmaker\0WindowMaker";
	xcb_atom_t atom[W_ATOM_COUNT];
	int i = 0;
	char wm_name[64] = "";

	/* set supported hints list */
	atom[i++] = net_client_list;
	atom[i++] = net_client_list_stacking;
	atom[i++] = net_number_of_desktops;
	atom[i++] = net_desktop_geometry;
	atom[i++] = net_desktop_viewport;
	atom[i++] = net_current_desktop;
	atom[i++] = net_desktop_names;
	atom[i++] = net_active_window;
	atom[i++] = net_workarea;
	atom[i++] = net_supporting_wm_check;
	atom[i++] = net_showing_desktop;
	atom[i++] = net_desktop_layout;
	atom[i++] = net_moveresize_window;
	atom[i++] = net_wm_moveresize;
	atom[i++] = net_restack_window;
	atom[i++] = net_request_frame_extents;
	atom[i++] = net_close_window;
	atom[i++] = net_wm_desktop;
	atom[i++] = net_wm_fullscreen_monitors;
	atom[i++] = net_wm_window_type;
	atom[i++] = net_wm_window_type_desktop;
	atom[i++] = net_wm_window_type_dock;
	atom[i++] = net_wm_window_type_toolbar;
	atom[i++] = net_wm_window_type_menu;
	atom[i++] = net_wm_window_type_utility;
	atom[i++] = net_wm_window_type_splash;
	atom[i++] = net_wm_window_type_dialog;
	atom[i++] = net_wm_window_type_dropdown_menu;
	atom[i++] = net_wm_window_type_popup_menu;
	atom[i++] = net_wm_window_type_tooltip;
	atom[i++] = net_wm_window_type_notification;
	atom[i++] = net_wm_window_type_combo;
	atom[i++] = net_wm_window_type_dnd;
	atom[i++] = net_wm_window_type_normal;

	atom[i++] = net_wm_state;
	atom[i++] = net_wm_state_modal;
	atom[i++] = net_wm_state_sticky;
	atom[i++] = net_wm_state_shaded;
	atom[i++] = net_wm_state_maximized_horz;
	atom[i++] = net_wm_state_maximized_vert;
	atom[i++] = net_wm_state_skip_taskbar;
	atom[i++] = net_wm_state_skip_pager;
	atom[i++] = net_wm_state_hidden;
	atom[i++] = net_wm_state_fullscreen;
	atom[i++] = net_wm_state_above;
	atom[i++] = net_wm_state_below;
	atom[i++] = net_wm_state_focused;
	atom[i++] = net_wm_state_demands_attention;

	atom[i++] = net_wm_allowed_actions;
	atom[i++] = net_wm_action_move;
	atom[i++] = net_wm_action_resize;
	atom[i++] = net_wm_action_minimize;
	atom[i++] = net_wm_action_shade;
	atom[i++] = net_wm_action_stick;
	atom[i++] = net_wm_action_maximize_horz;
	atom[i++] = net_wm_action_maximize_vert;
	atom[i++] = net_wm_action_fullscreen;
	atom[i++] = net_wm_action_change_desktop;
	atom[i++] = net_wm_action_close;
	atom[i++] = net_wm_action_above;
	atom[i++] = net_wm_action_below;

	atom[i++] = net_wm_pid;
	atom[i++] = net_wm_ping;
#ifdef USE_XSYNC
	if (wdisp->xext.xsync.supported) {
		atom[i++] = net_wm_sync_request;
		atom[i++] = net_wm_sync_request_counter;
	}
#endif
	atom[i++] = net_wm_user_time;
	atom[i++] = net_wm_user_time_window;
	atom[i++] = net_wm_full_placement;
#ifdef HAVE_STARTUP_NOTIFICATION
	atom[i++] = net_startup_id;
#endif

	atom[i++] = net_wm_strut;
	atom[i++] = net_wm_strut_partial;
	atom[i++] = net_wm_icon_geometry;
	atom[i++] = net_wm_icon;
	atom[i++] = net_wm_handled_icons;
	atom[i++] = net_wm_window_opacity;

	atom[i++] = net_frame_extents;

	atom[i++] = net_wm_name;
	atom[i++] = net_wm_visible_name;
	atom[i++] = net_wm_icon_name;
	atom[i++] = net_wm_visible_icon_name;

	WXSetAtomN(wdisp->conn, scr->root_win, net_supported, atom, i);

	/* set supporting wm hint */
	WXSetWindow(wdisp->conn, scr->root_win, net_supporting_wm_check, scr->info_window);
	WXSetWindow(wdisp->conn, scr->info_window, net_supporting_wm_check, scr->info_window);

	/* set _NET_WM_NAME on supporting window */
	snprintf(wm_name, sizeof(wm_name), "WindowMaker %s", VERSION);
	WXSetUTF8String(wdisp->conn, scr->info_window, net_wm_name, wm_name, strlen(wm_name));

	/* set _NET_WM_PID on supporting window */
	WXSetCardinal(wdisp->conn, scr->info_window, net_wm_pid, (uint32_t)getpid());

	/* set WM_CLASS on supporting window: instance\0class\0 */
	WXSetLatin1String(wdisp->conn, scr->info_window, XCB_ATOM_WM_CLASS, wm_class, sizeof(wm_class));
}

void wNETWMUpdateDesktop(WScreen *scr)
{
	uint32_t *views, sizes[2];
	int count, i;

	if (scr->workspace_count == 0)
		return;

	count = scr->workspace_count * 2;
	views = wmalloc(sizeof(uint32_t) * count);
#ifdef VIRTUAL_DESKTOP
	/* The virtual desktop extent only replaces the framebuffer size
	 * while viewport panning is enabled. With it off the EWMH desktop
	 * geometry must follow scr_width/scr_height, including a RandR
	 * mode change that leaves the per-workspace vdesk fields stale. */
	if (w_global.config->vdesk_enable) {
		sizes[0] = scr->workspaces[scr->current_workspace]->vdesk_w;
		sizes[1] = scr->workspaces[scr->current_workspace]->vdesk_h;
	} else {
		sizes[0] = scr->scr_width;
		sizes[1] = scr->scr_height;
	}
#else
	sizes[0] = scr->scr_width;
	sizes[1] = scr->scr_height;
#endif

	for (i = 0; i < scr->workspace_count; i++) {
#ifdef VIRTUAL_DESKTOP
		/* EWMH viewport has no signed values, so negatives would wrap */
		views[2 * i + 0] = scr->workspaces[i]->view_x > 0 ?
				   (uint32_t)scr->workspaces[i]->view_x : 0;
		views[2 * i + 1] = scr->workspaces[i]->view_y > 0 ?
				   (uint32_t)scr->workspaces[i]->view_y : 0;
#else
		views[2 * i + 0] = 0;
		views[2 * i + 1] = 0;
#endif
	}

	WXSetCardinalN(wdisp->conn, scr->root_win, net_desktop_geometry, sizes, 2);
	WXSetCardinalN(wdisp->conn, scr->root_win, net_desktop_viewport, views, count);

	wfree(views);
}

int wNETWMGetDesktopHint(xcb_window_t root)
{
	int32_t desktop;

	if (WXGetCardinalSigned(wdisp->conn, root,
				wx_static_atoms[W_ATOM_NET_CURRENT_DESKTOP], &desktop))
		return desktop;
	return -1;
}

int wNETWMGetCurrentDesktopFromHint(WScreen *scr)
{
	return wNETWMGetDesktopHint(scr->root_win);
}

/* ---- dock edge reservation ------------------------------------------- */

/*
 * Publish the dock's reservation as _NET_WM_STRUT(_PARTIAL) on the info
 * window so other clients honour it, and delete the properties when
 * there is nothing to reserve. Wmaker's own placement keeps using
 * wDockInsetArea(). The property is deliberately never fed through
 * scr->netdata->strut, otherwise the inset would be applied twice.
 * Only NoWindowOverDock=YES publishes, and the span is the head(s) the
 * dock's main tile actually overlaps, so a dock on the right of head 0
 * does not reserve head 1. A head that only grazes the tile at a corner
 * without reaching the dock's edge is ignored.
 */
void wDockPublishStrut(WScreen *scr)
{
	WMRect rect, head, u;
	WMRect touched, on_edge;
	uint32_t data[12];
	uint32_t legacy[4];
	bool any_touched = false;
	bool any_on_edge = false;
	int h;

	if (!scr || !scr->netdata)
		return;

	if (!w_global.config->no_window_over_dock || !scr->dock || !scr->dock->icon_array[0]) {
		wDockClearStrut(scr);
		return;
	}

	if (scr->dock->auto_hide && scr->dock->hidden) {
		wDockClearStrut(scr);
		return;
	}

	for (h = 0; h < wXineramaHeads(scr); h++) {
		if (!wAppIconTouchesHead(scr->dock->icon_array[0], h))
			continue;
		head = wGetRectForHead(scr, h);
		if (any_touched) {
			wGetRectUnion(&touched, &head, &u);
			touched = u;
		} else {
			touched = head;
			any_touched = true;
		}
		if (!wDockHeadOnEdge(head, scr->scr_width, scr->scr_height,
				   scr->dock->edge))
			continue;
		if (any_on_edge) {
			wGetRectUnion(&on_edge, &head, &u);
			on_edge = u;
		} else {
			on_edge = head;
			any_on_edge = true;
		}
	}
	if (any_on_edge)
		rect = on_edge;
	else if (any_touched)
		rect = touched;
	else
		rect = wmkrect(0, 0, scr->scr_width, scr->scr_height);

	wDockGeomStrut(rect, scr->scr_width, scr->scr_height, scr->dock->edge,
		       w_global.config->icon_size + DOCK_EXTRA_SPACE, data);

	if (!(data[0] || data[1] || data[2] || data[3])) {
		wDockClearStrut(scr);
		return;
	}

	legacy[0] = data[0];
	legacy[1] = data[1];
	legacy[2] = data[2];
	legacy[3] = data[3];

	/* the partial property takes precedence in our own reader */
	WXSetCardinalN(wdisp->conn, scr->info_window, net_wm_strut_partial, data, 12);
	WXSetCardinalN(wdisp->conn, scr->info_window, net_wm_strut, legacy, 4);
}

void wDockClearStrut(WScreen *scr)
{
	if (!scr || !scr->netdata)
		return;

	WXDeleteProperty(wdisp->conn, scr->info_window, net_wm_strut_partial);
	WXDeleteProperty(wdisp->conn, scr->info_window, net_wm_strut);
}

static RImage *makeRImageFromARGBData(const uint32_t *data)
{
	size_t size;
	int width, height, i;
	RImage *image;
	unsigned char *imgdata;
	uint32_t pixel;

	width = data[0];
	height = data[1];
	size = (size_t)width * (size_t)height;

	if (size == 0)
		return NULL;

	image = RCreateImage(width, height, true);
	if (!image)
		return NULL;

	for (imgdata = image->data, i = 2; (size_t)i < size + 2; i++, imgdata += 4) {
		pixel = data[i];
		imgdata[3] = (pixel >> 24) & 0xff;	/* A */
		imgdata[0] = (pixel >> 16) & 0xff;	/* R */
		imgdata[1] = (pixel >> 8) & 0xff;	/* G */
		imgdata[2] = (pixel >> 0) & 0xff;	/* B */
	}

	return image;
}

/* Find the best icon to be used by Window Maker for appicon/miniwindows. */
static RImage *findBestIcon(const uint32_t *data, size_t items)
{
	int wanted;
	int dx, dy, d;
	int sx, sy, size;
	int best_d, largest;
	size_t i;
	const uint32_t *icon;
	RImage *src_image, *ret_image;
	double f;

	wanted = wIconEffectiveSize();

	/* try to find an icon which is close to the wanted size, but not larger */
	icon = NULL;
	best_d = wanted * wanted * 2;
	for (i = 0L; i < items - 1;) {

		/* get the current icon's size */
		sx = (int)data[i];
		sy = (int)data[i + 1];
		if (sx < 1 || sy < 1 || sx > 4096 || sy > 4096)
			break;
		size = sx * sy + 2;
		if ((size_t)size > items - i)
			break;

		/* check the size difference if it's not too large */
		if ((sx <= wanted) && (sy <= wanted)) {
			dx = wanted - sx;
			dy = wanted - sy;
			d = (dx * dx) + (dy * dy);
			if (d < best_d) {
				icon = &data[i];
				best_d = d;
			}
		}

		i += size;
	}

	/* if an icon has been found, no transformation is needed */
	if (icon)
		return makeRImageFromARGBData(icon);

	/* We need to scale down an icon. Find the largest one, for it usually
	 * looks better to scale down a large image by a large scale than a
	 * small image by a small scale. */
	largest = 0;
	for (i = 0L; i < items - 1;) {
		sx = (int)data[i];
		sy = (int)data[i + 1];
		if (sx < 1 || sy < 1 || sx > 4096 || sy > 4096)
			break;
		size = sx * sy;
		if ((size_t)size + 2 > items - i)
			break;
		if (size > largest) {
			icon = &data[i];
			largest = size;
		}
		i += size + 2;
	}

	/* give up if there's no icon to work with */
	if (!icon)
		return NULL;

	/* create a scaled down version of the icon */
	src_image = makeRImageFromARGBData(icon);
	if (src_image->width > src_image->height) {
		f = (double)wanted / (double)src_image->width;
		ret_image = RScaleImage(src_image, wanted, (int)(f * (double)(src_image->height)));
	} else {
		f = (double)wanted / (double)src_image->height;
		ret_image = RScaleImage(src_image, (int)(f * (double)src_image->width), wanted);
	}
	RReleaseImage(src_image);
	return ret_image;
}

static RImage *get_window_image_from_x11_ck(WXCookie ck)
{
	RImage *image;
	size_t items;
	uint32_t *property;

	property = WXRcvCardinalList(wdisp->conn, ck, &items);
	if (!property)
		return NULL;

	if (items < 2) {
		wfree(property);
		return NULL;
	}

	/* Find the best icon */
	image = findBestIcon(property, items);
	wfree(property);
	if (!image)
		return NULL;

	/* Resize the image to the correct value */
	image = wIconValidateIconSize(image, w_global.config->icon_size);

	return image;
}

RImage *get_window_image_from_x11(xcb_window_t window)
{
	/* Cap the read at 4 Mi CARDINALs (16 MiB), Electron and Chromium
	 * based applications ship a single 1024x1024 icon (1,048,578
	 * cardinals) and nothing smaller, so the bound must cover that,
	 * while still preventing a hostile client from forcing an
	 * arbitrarily large transient allocation by stuffing the
	 * property. */
	return get_window_image_from_x11_ck(
		WXReqFormat32(wdisp->conn, window, net_wm_icon, XCB_ATOM_CARDINAL, 1u << 22));
}

static void updateIconImageCk(WWindow *wwin, WXCookie icon_ck)
{
	WApplication *app;
	bool had_image;

	/* Remove the icon image from X11 */
	had_image = (wwin->net_icon_image != NULL);
	if (wwin->net_icon_image)
		RReleaseImage(wwin->net_icon_image);

	/* Save the icon in the X11 icon */
	wwin->net_icon_image = get_window_image_from_x11_ck(icon_ck);

	if (wwin->frame)
		wWindowRefreshTitlebarIcon(wwin);

	/* Refresh the Window Icon */
	if (wwin->icon)
		wIconUpdate(wwin->icon);

	/* Refresh the application icon. The app_icon's owner is
	 * main_window_desc, whose net_icon_image was copied once at
	 * wApplicationCreate time, so propagate the new image there too
	 * or wIconUpdate below will not see it (clients that set
	 * _NET_WM_ICON only after mapping otherwise keep the default).
	 * When this window neither had nor has a _NET_WM_ICON there is
	 * nothing to propagate and the app icon's source (wm_hints
	 * icon_pixmap or the user database) has not changed, so skip the
	 * re-derive instead of round-tripping for the same RImage. */
	if (!had_image && !wwin->net_icon_image)
		return;

	app = wApplicationOf(wwin->main_window);
	if (app) {
		/* Only seed a leader that has no icon of its own, so a
		 * hidden group-leader stub still receives the first real
		 * window's image for the appicon, but a secondary window
		 * (a different WM_CLASS instance under the same leader)
		 * cannot overwrite the appicon with its own icon on every
		 * property update. */
		if (app->main_window_desc && app->main_window_desc != wwin
		    && !app->main_window_desc->net_icon_image) {
			app->main_window_desc->net_icon_image =
				RRetainImage(wwin->net_icon_image);
		}
		if (app->app_icon) {
			wIconUpdate(app->app_icon->icon);
			wAppIconPaint(app->app_icon);
		}
	}
}

static void updateIconImage(WWindow *wwin)
{
	updateIconImageCk(wwin,
		WXReqFormat32(wdisp->conn, wwin->client_win, net_wm_icon, XCB_ATOM_CARDINAL, 1u << 22));
}

static void updateWindowOpacityCk(WWindow *wwin, WXCookie ck)
{
	uint32_t opacity;

	if (!wwin->frame) {
		WXDiscard(wdisp->conn, ck);
		return;
	}

	/* We don't care about this ourselves, but other programs need us to copy
	 * this to the frame window. */
	if (WXRcvCardinal(wdisp->conn, ck, &opacity))
		WXSetCardinal(wdisp->conn, wwin->frame->core->window, net_wm_window_opacity, opacity);
	else
		WXDeleteProperty(wdisp->conn, wwin->frame->core->window, net_wm_window_opacity);
}

static void updateWindowOpacity(WWindow *wwin)
{
	updateWindowOpacityCk(wwin,
		WXReqFormat32(wdisp->conn, wwin->client_win, net_wm_window_opacity, XCB_ATOM_CARDINAL, 1));
}

/* read and validate _NET_DESKTOP_LAYOUT off the root window. The spec
 * carries orientation, columns, rows and starting corner, with one of
 * rows/cols allowed to be 0 meaning "derive from the desktop count". With
 * no property set we describe a single row so directional Left/Right keys
 * fall back to Prev/Next. */
void wNETWMUpdateDesktopLayout(WScreen *scr)
{
	uint32_t v[4];
	uint32_t *list;
	size_t count;
	int n, cols, rows;

	scr->net_desktop_layout.orientation = 0;
	scr->net_desktop_layout.corner = 0;
	scr->net_desktop_layout.cols = WMAX(1, scr->workspace_count);
	scr->net_desktop_layout.rows = 1;

	list = WXGetCardinalList(wdisp->conn, scr->root_win,
				 net_desktop_layout, 4, &count);
	if (!list)
		return;
	if (count < 3) {
		wfree(list);
		return;
	}
	v[0] = list[0];
	v[1] = list[1];
	v[2] = list[2];
	v[3] = (count > 3) ? list[3] : 0;
	wfree(list);

	/* the property is client-set on root, clamp before any signed
	 * arithmetic so a hostile pager cannot overflow rows*cols */
	n = WMAX(1, scr->workspace_count);
	cols = (v[1] > MAX_WORKSPACES) ? MAX_WORKSPACES : (int)v[1];
	rows = (v[2] > MAX_WORKSPACES) ? MAX_WORKSPACES : (int)v[2];

	if (cols <= 0 && rows <= 0)
		return;
	if (cols <= 0)
		cols = (n + rows - 1) / rows;
	else if (rows <= 0)
		rows = (n + cols - 1) / cols;

	/* a pager that advertises a grid smaller than the desktop count is
	 * not honoured, fall back to the linear default rather than leave
	 * later workspaces unreachable through directional keys */
	if (cols * rows < n)
		return;

	scr->net_desktop_layout.orientation = (v[0] == 1) ? 1 : 0;
	scr->net_desktop_layout.cols = cols;
	scr->net_desktop_layout.rows = rows;
	scr->net_desktop_layout.corner = (v[3] <= 3) ? (int)v[3] : 0;
}

void wNETWMCheckRootHintChange(WScreen *scr, xcb_atom_t atom)
{
	if (atom == net_desktop_layout)
		wNETWMUpdateDesktopLayout(scr);
}

static void updateShowDesktop(WScreen *scr, bool show)
{
	WXSetCardinal(wdisp->conn, scr->root_win, net_showing_desktop, show == true);
}

static void wNETWMShowingDesktop(WScreen *scr, bool show)
{
	if (show && scr->netdata->show_desktop == NULL) {
		WWindow *tmp, **wins;
		xcb_window_t *ids;
		int i, n = 0;

		wins = (WWindow **) wmalloc(sizeof(WWindow *) * (scr->window_count + 1));
		ids = (xcb_window_t *) wmalloc(sizeof(xcb_window_t) * (scr->window_count + 1));

		/* collect the pass first and iconify afterwards: iconifying
		 * the focused window hands focus to the window under the
		 * pointer under click focus, and wSetFocusTo moves that
		 * window to the chain head, unlinking it from the middle
		 * and rewiring the neighbours' prev links, so walking and
		 * iconifying in one pass would resume from a stale link
		 * and skip the migrated window. Re-resolve each id because
		 * wIconifyWindow() can free a window via ProcessPendingEvents().
		 * Publish the restore list first so a destroyed window is
		 * pruned from it by wNETWMShowingDesktopForgetWindow() */
		for (tmp = scr->focused_window; tmp; tmp = tmp->prev) {
			if (!tmp->flags.hidden && !tmp->flags.miniaturized
			    && !tmp->flags.internal_window
			    && !WFLAGP(tmp, skip_window_list)) {
				wins[n] = tmp;
				ids[n] = tmp->client_win;
				n++;
			}
		}
		wins[n] = NULL;

		scr->netdata->show_desktop = wins;
		updateShowDesktop(scr, true);

		for (i = 0; i < n; i++) {
			WWindow *cand = wWindowFor(ids[i]);

			if (!cand)
				continue;
			cand->flags.skip_next_animation = 1;
			cand->flags.net_show_desktop = 1;
			wIconifyWindow(cand);
		}
		wfree(ids);
	} else if (scr->netdata->show_desktop != NULL) {
		WWindow **tmp;
		for (tmp = scr->netdata->show_desktop; *tmp; ++tmp) {
			(*tmp)->flags.restore_iconified = 0;
			wDeiconifyWindow(*tmp);
			(*tmp)->flags.net_show_desktop = 0;
		}
		wfree(scr->netdata->show_desktop);
		scr->netdata->show_desktop = NULL;
		updateShowDesktop(scr, false);
	}
}

void wNETWMShowingDesktopToggle(WScreen *scr)
{
	wNETWMShowingDesktop(scr, scr->netdata->show_desktop == NULL);
}

/* Remove wwin from the show-desktop restore list so a window
 * destroyed while show-desktop is active does not leave a stale
 * pointer for the restore loop. */
void wNETWMShowingDesktopForgetWindow(WScreen *scr, WWindow *wwin)
{
	WWindow **arr, **dst;

	arr = scr->netdata->show_desktop;
	if (!arr)
		return;
	for (dst = arr; *dst; dst++) {
		if (*dst == wwin) {
			for (; *dst; dst++)
				*dst = *(dst + 1);
			break;
		}
	}
}

#ifdef DEBUG_WMSPEC
/* wXGetAtomName returns a malloc'd string that must be free()d, and
 * returns NULL for an invalid atom. Both are UB for %s. Return a
 * static buffer instead, freeing the Xlib allocation. */
static const char *atom_name(xcb_atom_t atom)
{
	static char buf[2][64];
	static int slot;
	char *name = wXGetAtomName(wdisp->conn, atom);
	char *out = buf[slot ^= 1];

	if (name) {
		snprintf(out, sizeof(buf[0]), "%s", name);
		free(name);
		return out;
	}
	return "<bad-atom>";
}
#endif

void wNETWMInitStuff(WScreen *scr)
{
	NetData *data;

#ifdef DEBUG_WMSPEC
	wmessage("wNETWMInitStuff");
#endif

#define WNET_FILL_(var, id)	var = wx_static_atoms[id]
	WNET_ATOMS(WNET_FILL_)
#undef WNET_FILL_

	data = wmalloc(sizeof(NetData));
	data->scr = scr;
	data->strut = NULL;
	data->show_desktop = NULL;

	scr->netdata = data;

	setSupportedHints(scr);

	WMAddNotificationObserver(observer, data, WMNManaged, NULL);
	WMAddNotificationObserver(observer, data, WMNUnmanaged, NULL);
	WMAddNotificationObserver(observer, data, WMNChangedWorkspace, NULL);
	WMAddNotificationObserver(observer, data, WMNChangedState, NULL);
	WMAddNotificationObserver(observer, data, WMNChangedFocus, NULL);
	WMAddNotificationObserver(observer, data, WMNChangedStacking, NULL);
	WMAddNotificationObserver(observer, data, WMNChangedName, NULL);

	WMAddNotificationObserver(wsobserver, data, WMNResetStacking, NULL);
	WMAddNotificationObserver(wsobserver, data, WMNWorkspaceCreated, NULL);
	WMAddNotificationObserver(wsobserver, data, WMNWorkspaceDestroyed, NULL);
	WMAddNotificationObserver(wsobserver, data, WMNWorkspaceChanged, NULL);
	WMAddNotificationObserver(wsobserver, data, WMNWorkspaceNameChanged, NULL);

	updateClientList(scr);
	updateClientListStacking(scr, NULL);
	updateWorkspaceCount(scr);
	updateWorkspaceNames(scr);
	updateShowDesktop(scr, false);
	wNETWMUpdateDesktopLayout(scr);

	wScreenUpdateUsableArea(scr);
}

void wNETWMCleanup(WScreen *scr, bool keep_current_desktop)
{
#define WNET_CLEAN_(var, id)						\
	do {								\
		if (!keep_current_desktop || var != net_current_desktop)	\
			WXDeleteProperty(wdisp->conn, scr->root_win, var);	\
	} while (0)
	WNET_ATOMS(WNET_CLEAN_)
#undef WNET_CLEAN_
}

void wNETWMUpdateActions(WWindow *wwin, bool del)
{
	xcb_atom_t action[12];	/* nr of actions atoms defined */
	int i = 0;

	if (del) {
		WXDeleteProperty(wdisp->conn, wwin->client_win, net_wm_allowed_actions);
		return;
	}

	if (IS_MOVABLE(wwin))
		action[i++] = net_wm_action_move;

	if (IS_RESIZABLE(wwin))
		action[i++] = net_wm_action_resize;

	if (!WFLAGP(wwin, no_miniaturizable))
		action[i++] = net_wm_action_minimize;

	if (!WFLAGP(wwin, no_shadeable))
		action[i++] = net_wm_action_shade;

	/*    if (!WFLAGP(wwin, no_stickable)) */
	action[i++] = net_wm_action_stick;

	/*    if (!(WFLAGP(wwin, no_maximizeable) & MAX_HORIZONTAL)) */
	if (IS_RESIZABLE(wwin))
		action[i++] = net_wm_action_maximize_horz;

	/*    if (!(WFLAGP(wwin, no_maximizeable) & MAX_VERTICAL)) */
	if (IS_RESIZABLE(wwin))
		action[i++] = net_wm_action_maximize_vert;

	/*    if (!WFLAGP(wwin, no_fullscreen)) */
	action[i++] = net_wm_action_fullscreen;

	/*    if (!WFLAGP(wwin, no_change_desktop)) */
	action[i++] = net_wm_action_change_desktop;

	if (!WFLAGP(wwin, no_closable))
		action[i++] = net_wm_action_close;

	action[i++] = net_wm_action_above;
	action[i++] = net_wm_action_below;

	WXSetAtomN(wdisp->conn, wwin->client_win, net_wm_allowed_actions, action, i);
}

void wNETWMUpdateWorkarea(WScreen *scr)
{
	WArea total_usable;
	int nb_workspace;
	uint32_t *property_value;
	int i;

	if (!scr->netdata) {
		/* If the _NET_xxx were not initialised, it not necessary to do anything */
		return;
	}

	if (!scr->usableArea) {
		/* If we don't have any info, we fall back on using the complete screen area */
		total_usable.x1 = 0;
		total_usable.y1 = 0;
		total_usable.x2 = scr->scr_width;
		total_usable.y2 = scr->scr_height;

	} else {
		/*
		 * the _NET_WORKAREA is supposed to contain the total area of the screen that
		 * is usable, so we merge the areas from all xrandr sub-screens
		 */
		total_usable = scr->usableArea[0];

		for (i = 1; i < wXineramaHeads(scr); i++) {
			/* The merge is not subtle because _NET_WORKAREA does not need more */
			if (scr->usableArea[i].x1 < total_usable.x1)
				total_usable.x1 = scr->usableArea[i].x1;

			if (scr->usableArea[i].y1 < total_usable.y1)
				total_usable.y1 = scr->usableArea[i].y1;

			if (scr->usableArea[i].x2 > total_usable.x2)
				total_usable.x2 = scr->usableArea[i].x2;

			if (scr->usableArea[i].y2 > total_usable.y2)
				total_usable.y2 = scr->usableArea[i].y2;
		}

	}

	/* We are expected to repeat the information for each workspace */
	if (scr->workspace_count == 0)
		nb_workspace = 1;
	else
		nb_workspace = scr->workspace_count;

	property_value = wmalloc(sizeof(uint32_t) * nb_workspace * 4);
	for (i = 0; i < nb_workspace; i++) {
		property_value[4 * i + 0] = total_usable.x1;
		property_value[4 * i + 1] = total_usable.y1;
		property_value[4 * i + 2] = total_usable.x2 - total_usable.x1;
		property_value[4 * i + 3] = total_usable.y2 - total_usable.y1;
	}

	WXSetCardinalN(wdisp->conn, scr->root_win, net_workarea, property_value, nb_workspace * 4);
	wfree(property_value);
}

static inline int clampi(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

bool wNETWMGetUsableArea(WScreen *scr, int head, WArea *area)
{
	WReservedArea *cur;
	WMRect rect;

	if (!scr->netdata || !scr->netdata->strut)
		return false;

	area->x1 = area->y1 = area->x2 = area->y2 = 0;

	rect = wGetRectForHead(scr, head);

	for (cur = scr->netdata->strut; cur; cur = cur->next) {
		/* Only honour an edge reservation when the reserved rectangle
		 * actually intersects this head, so a panel reserving 64 px on
		 * the root left edge does not also shrink a side-by-side head
		 * at x=1920. Each edge is tested on both axes, the start/end
		 * span from _NET_WM_STRUT_PARTIAL and the thickness reaching
		 * into the head along the perpendicular axis. */
		if (cur->area.x1 > area->x1
		    && cur->area.x1 > rect.pos.x
		    && cur->left_start < rect.pos.y + rect.size.height
		    && cur->left_end >= rect.pos.y)
			area->x1 = cur->area.x1;

		if (cur->area.x2 > area->x2
		    && scr->scr_width - cur->area.x2 < rect.pos.x + rect.size.width
		    && cur->right_start < rect.pos.y + rect.size.height
		    && cur->right_end >= rect.pos.y)
			area->x2 = cur->area.x2;

		if (cur->area.y1 > area->y1
		    && cur->area.y1 > rect.pos.y
		    && cur->top_start < rect.pos.x + rect.size.width
		    && cur->top_end >= rect.pos.x)
			area->y1 = cur->area.y1;

		if (cur->area.y2 > area->y2
		    && scr->scr_height - cur->area.y2 < rect.pos.y + rect.size.height
		    && cur->bottom_start < rect.pos.x + rect.size.width
		    && cur->bottom_end >= rect.pos.x)
			area->y2 = cur->area.y2;
	}

	if (area->x1 == 0 && area->x2 == 0 && area->y1 == 0 && area->y2 == 0)
		return false;

	/* NOTE(gryf): calculation for the reserved area should be preformed for
	 * current head, but area, which comes form _NET_WM_STRUT, has to be
	 * calculated relative to entire screen size, i.e. suppose we have three
	 * heads arranged like this:
	 *
	 * ╔════════════════════════╗
	 * ║ 0                      ║
	 * ║                        ╠═══════════╗
	 * ║                        ║ 2         ║
	 * ║                        ║           ║
	 * ╟────────────────────────╫───────────╢
	 * ╠════════════════════════╬═══════════╝
	 * ║ 1                      ║
	 * ║                        ║
	 * ║                        ║
	 * ║                        ║
	 * ║                        ║
	 * ╟────────────────────────╢
	 * ╚════════════════════════╝
	 *
	 * where head 0 have resolution 1920x1080, head 1: 1920x1200 and head 2
	 * 800x600, so the screen size in this arrangement will be 2720x2280 (1920
	 * + 800) x (1080 + 1200).
	 *
	 * Bottom line represents some 3rd party panel, which sets properties in
	 * _NET_WM_STRUT_PARTIAL and optionally _NET_WM_STRUT.
	 *
	 * By coincidence, coordinates x1 and y1 from left and top are the same as
	 * the original data which came from _NET_WM_STRUT, since they meaning
	 * distance from the edge, so we leave it as-is, otherwise if they have 0
	 * value, we need to set right head position.
	 */

	/* optional reserved space from left */
	if (area->x1 == 0) area->x1 = rect.pos.x;

	/* optional reserved space from top */
	if (area->y1 == 0) area->y1 = rect.pos.y;

	/* optional reserved space from right */
	if (area->x2 == 0)
		area->x2 = rect.pos.x + rect.size.width;
	else
		area->x2 = scr->scr_width - area->x2;

	/* optional reserved space from bottom */
	if (area->y2 == 0)
		area->y2 = rect.pos.y + rect.size.height;
	else
		area->y2 = scr->scr_height - area->y2;

	/* Opposing or oversized struts can leave the usable rectangle
	 * inverted or outside this head, which placement and maximize then
	 * trust as width = x2 - x1. Clamp to the head and collapse any
	 * inversion onto the head edges so callers always see x1 <= x2. */
	area->x1 = clampi(area->x1, rect.pos.x, rect.pos.x + rect.size.width);
	area->x2 = clampi(area->x2, rect.pos.x, rect.pos.x + rect.size.width);
	area->y1 = clampi(area->y1, rect.pos.y, rect.pos.y + rect.size.height);
	area->y2 = clampi(area->y2, rect.pos.y, rect.pos.y + rect.size.height);
	if (area->x1 > area->x2) {
		area->x1 = rect.pos.x;
		area->x2 = rect.pos.x + rect.size.width;
	}
	if (area->y1 > area->y2) {
		area->y1 = rect.pos.y;
		area->y2 = rect.pos.y + rect.size.height;
	}

	return true;
}

static void updateClientList(WScreen *scr)
{
	WWindow *wwin;
	xcb_window_t *windows;
	int count;

	if (scr->flags.manage_burst) {
		scr->flags.client_lists_stale = 1;
		return;
	}

	windows = (xcb_window_t *) wmalloc(sizeof(xcb_window_t) * (scr->window_count + 1));

	count = 0;
	wwin = scr->focused_window;
	while (wwin) {
		windows[count++] = wwin->client_win;
		wwin = wwin->prev;
	}
	WXSetWindowN(wdisp->conn, scr->root_win, net_client_list, windows, count);

	wfree(windows);
	wXFlush(wdisp->conn);
}

static void updateClientListStacking(WScreen *scr, WWindow *wwin_excl)
{
	WWindow *wwin;
	xcb_window_t *client_list, *client_list_reverse;
	int client_count, i;
	WCoreWindow *tmp;
	WMBagIterator iter;

	if (scr->flags.manage_burst) {
		scr->flags.client_lists_stale = 1;
		return;
	}

	/* update client list */
	i = scr->window_count + 1;
	client_list = (xcb_window_t *) wmalloc(sizeof(xcb_window_t) * i);
	client_list_reverse = (xcb_window_t *) wmalloc(sizeof(xcb_window_t) * i);

	client_count = 0;
	WM_ETARETI_BAG(scr->stacking_list, tmp, iter) {
		while (tmp) {
			wwin = wWindowFor(tmp->window);
			/* wwin_excl is a window to exclude from the list
			   (e.g. it's now unmanaged) */
			if (wwin && (wwin != wwin_excl))
				client_list[client_count++] = wwin->client_win;
			tmp = tmp->stacking->under;
		}
	}

	for (i = 0; i < client_count; i++) {
		xcb_window_t w = client_list[client_count - i - 1];
		client_list_reverse[i] = w;
	}

	WXSetWindowN(wdisp->conn, scr->root_win, net_client_list_stacking, client_list_reverse, client_count);

	wfree(client_list);
	wfree(client_list_reverse);

	wXFlush(wdisp->conn);
}

/*
 * Flush point for the manage burst: updates suppressed while
 * scr->flags.manage_burst was set are applied here as one rebuild of
 * each list. A no-op when nothing was suppressed.
 */
void wNETWMFlushClientLists(WScreen *scr)
{
	if (!scr->flags.client_lists_stale)
		return;

	scr->flags.client_lists_stale = 0;
	updateClientList(scr);
	updateClientListStacking(scr, NULL);
}

static void updateWorkspaceCount(WScreen *scr)
{				/* changeable */
	WXSetCardinal(wdisp->conn, scr->root_win, net_number_of_desktops, scr->workspace_count);
}

static void updateCurrentWorkspace(WScreen *scr)
{				/* changeable */
	WXSetCardinal(wdisp->conn, scr->root_win, net_current_desktop, scr->current_workspace);
}

/* Public wrapper so per-head focus changes can republish
 * _NET_CURRENT_DESKTOP without going through the WMNWorkspaceChanged
 * observer, since in per-head mode the EWMH current desktop must follow
 * the focused head even when no workspace switch happened. */
void wNETWMUpdateCurrentDesktop(WScreen *scr)
{
	updateCurrentWorkspace(scr);
}

static void updateWorkspaceNames(WScreen *scr)
{
	char buf[MAX_WORKSPACES * (MAX_WORKSPACENAME_WIDTH + 1)], *pos;
	unsigned int i, len, curr_size;

	pos = buf;
	len = 0;
	for (i = 0; i < scr->workspace_count; i++) {
		size_t room = sizeof(buf) - len;

		curr_size = wstrlcpy(pos, scr->workspaces[i]->name, room);
		/* wstrlcpy returns strlen(src), so on truncation curr_size
		 * is larger than what was actually written. Stop rather than
		 * advance past the buffer and underflow room next time. */
		if (curr_size + 1 >= room)
			break;
		pos += (curr_size + 1);
		len += (curr_size + 1);
	}

	WXSetUTF8String(wdisp->conn, scr->root_win, net_desktop_names, buf, len);
}

static void updateFocusHint(WScreen *scr)
{				/* changeable */
	xcb_window_t window;

	if (!scr->focused_window || !scr->focused_window->flags.focused)
		window = XCB_NONE;
	else
		window = scr->focused_window->client_win;

	WXSetWindow(wdisp->conn, scr->root_win, net_active_window, window);
}

static void updateWorkspaceHint(WWindow *wwin, bool fake, bool del)
{
	if (del) {
		WXDeleteProperty(wdisp->conn, wwin->client_win, net_wm_desktop);
	} else {
		uint32_t l = ((fake || IS_OMNIPRESENT(wwin)) ? 0xffffffffu : (uint32_t)WWIN_PRIMARY_WS(wwin));
		WXSetCardinal(wdisp->conn, wwin->client_win, net_wm_desktop, l);
	}
}

static void updateStateHint(WWindow *wwin, bool changedWorkspace, bool del)
{				/* changeable */
	if (del) {
		WXDeleteProperty(wdisp->conn, wwin->client_win, net_wm_state);
		WXDeleteProperty(wdisp->conn, wwin->client_win, net_wm_fullscreen_monitors);
	} else {
		xcb_atom_t state[15];	/* nr of defined state atoms */
		int i = 0;

		if (changedWorkspace || (w_global.config->sticky_icons && !IS_OMNIPRESENT(wwin)))
			updateWorkspaceHint(wwin, false, false);

		if (IS_OMNIPRESENT(wwin))
			state[i++] = net_wm_state_sticky;
		if (wwin->flags.shaded)
			state[i++] = net_wm_state_shaded;
		if (wwin->flags.maximized & MAX_HORIZONTAL)
			state[i++] = net_wm_state_maximized_horz;
		if (wwin->flags.maximized & MAX_VERTICAL)
			state[i++] = net_wm_state_maximized_vert;
		if (WFLAGP(wwin, skip_window_list))
			state[i++] = net_wm_state_skip_taskbar;
		if (wwin->flags.net_skip_pager)
			state[i++] = net_wm_state_skip_pager;
		if (wGroupIsInactiveMember(wwin))
			state[i++] = net_wm_state_hidden;
		else if ((wwin->flags.hidden || wwin->flags.miniaturized) && !wwin->flags.net_show_desktop) {
			state[i++] = net_wm_state_hidden;
			state[i++] = net_wm_state_skip_pager;

			if (wwin->flags.miniaturized && w_global.config->sticky_icons) {
				if (!IS_OMNIPRESENT(wwin))
					updateWorkspaceHint(wwin, true, false);
				state[i++] = net_wm_state_sticky;
			}
		}
		if (WFLAGP(wwin, sunken))
			state[i++] = net_wm_state_below;
		if (WFLAGP(wwin, floating))
			state[i++] = net_wm_state_above;
		if (wwin->flags.fullscreen)
			state[i++] = net_wm_state_fullscreen;
		if (wwin->flags.focused)
			state[i++] = net_wm_state_focused;
		if (wwin->flags.net_modal)
			state[i++] = net_wm_state_modal;
		if (wwin->flags.net_demands_attention || wwin->flags.urgent)
			state[i++] = net_wm_state_demands_attention;

		WXSetAtomN(wdisp->conn, wwin->client_win, net_wm_state, state, i);

		if (wwin->flags.fullscreen && (wwin->flags.fullscreen_monitors[0] != -1)) {
			uint32_t data[4];

			data[0] = wwin->flags.fullscreen_monitors[0];
			data[1] = wwin->flags.fullscreen_monitors[1];
			data[2] = wwin->flags.fullscreen_monitors[2];
			data[3] = wwin->flags.fullscreen_monitors[3];

			WXSetCardinalN(wdisp->conn, wwin->client_win, net_wm_fullscreen_monitors, data, 4);
		}
	}
}

/*
 * Build a WReservedArea from raw _NET_WM_STRUT(_PARTIAL) data. The values
 * come from an untrusted client property, so clamp every field to the root
 * extent and drop edges whose start/end span is inverted, otherwise a
 * hostile or buggy panel can produce a nonsensical workarea.
 */
static WReservedArea *makeReservedArea(WScreen *scr, xcb_window_t w,
				       const uint32_t *v, bool partial)
{
	WReservedArea *area = wmalloc(sizeof(WReservedArea));
	int sw = scr->scr_width, sh = scr->scr_height;

	area->area.x1 = clampi(v[0], 0, sw);
	area->area.x2 = clampi(v[1], 0, sw);
	area->area.y1 = clampi(v[2], 0, sh);
	area->area.y2 = clampi(v[3], 0, sh);

	if (partial) {
		area->left_start   = clampi(v[4],  0, sh - 1);
		area->left_end     = clampi(v[5],  0, sh - 1);
		area->right_start  = clampi(v[6],  0, sh - 1);
		area->right_end    = clampi(v[7],  0, sh - 1);
		area->top_start    = clampi(v[8],  0, sw - 1);
		area->top_end      = clampi(v[9],  0, sw - 1);
		area->bottom_start = clampi(v[10], 0, sw - 1);
		area->bottom_end   = clampi(v[11], 0, sw - 1);

		/* an inverted span is degenerate, treat that edge as absent */
		if (area->left_start > area->left_end)
			area->area.x1 = 0;
		if (area->right_start > area->right_end)
			area->area.x2 = 0;
		if (area->top_start > area->top_end)
			area->area.y1 = 0;
		if (area->bottom_start > area->bottom_end)
			area->area.y2 = 0;
	} else {
		/* plain _NET_WM_STRUT reserves the full edge */
		area->left_start  = area->right_start  = 0;
		area->left_end    = area->right_end    = sh - 1;
		area->top_start   = area->bottom_start = 0;
		area->top_end     = area->bottom_end   = sw - 1;
	}

	area->window = w;
	return area;
}

static bool updateStrut(WScreen *scr, xcb_window_t w, bool adding)
{
	WReservedArea *area;
	bool hasState = false;

	if (adding) {
		uint32_t strut[12];
		bool partial;

		/* _NET_WM_STRUT_PARTIAL takes precedence over the legacy
		 * _NET_WM_STRUT, and carries per-edge start/end spans so a
		 * panel reserving only part of an edge does not steal that
		 * edge on every head. */
		partial = WXGetCardinalN(wdisp->conn, w, net_wm_strut_partial, strut, 12);
		if (partial || WXGetCardinalN(wdisp->conn, w, net_wm_strut, strut, 4)) {

			area = makeReservedArea(scr, w, strut, partial);

			area->next = scr->netdata->strut;
			scr->netdata->strut = area;

			hasState = true;
		}
	} else {
		/* deleting */
		area = scr->netdata->strut;

		if (area) {
			if (area->window == w) {
				scr->netdata->strut = area->next;
				wfree(area);
				hasState = true;
			} else {
				while (area->next && area->next->window != w)
					area = area->next;

				if (area->next) {
					WReservedArea *next;

					next = area->next->next;
					wfree(area->next);
					area->next = next;

					hasState = true;
				}
			}
		}
	}

	return hasState;
}

static int getWindowLayer(WWindow *wwin)
{
	int layer = WMNormalLevel;

	if (wwin->type == net_wm_window_type_desktop) {
		layer = WMDesktopLevel;
	} else if (wwin->type == net_wm_window_type_dock) {
		layer = WMDockLevel;
	} else if (wwin->type == net_wm_window_type_toolbar) {
		layer = WMMainMenuLevel;
	} else if (wwin->type == net_wm_window_type_menu) {
		layer = WMSubmenuLevel;
	} else if (wwin->type == net_wm_window_type_utility) {
	} else if (wwin->type == net_wm_window_type_splash) {
	} else if (wwin->type == net_wm_window_type_dialog) {
		if (wwin->transient_for) {
			WWindow *parent = wWindowFor(wwin->transient_for);
			if (parent && parent->flags.fullscreen)
				layer = WMFullscreenLevel;
		}
	} else if (wwin->type == net_wm_window_type_dropdown_menu) {
		layer = WMSubmenuLevel;
	} else if (wwin->type == net_wm_window_type_popup_menu) {
		layer = WMSubmenuLevel;
	} else if (wwin->type == net_wm_window_type_tooltip) {
	} else if (wwin->type == net_wm_window_type_notification) {
		layer = WMPopUpLevel;
	} else if (wwin->type == net_wm_window_type_combo) {
		layer = WMSubmenuLevel;
	} else if (wwin->type == net_wm_window_type_dnd) {
	} else if (wwin->type == net_wm_window_type_normal) {
	}

	if (wwin->client_flags.sunken && WMSunkenLevel < layer)
		layer = WMSunkenLevel;
	if (wwin->client_flags.floating && WMFloatingLevel > layer)
		layer = WMFloatingLevel;

	return layer;
}

static void doStateAtom(WWindow *wwin, xcb_atom_t state, int set, bool init)
{
	if (state == net_wm_state_sticky) {
		if (set == _NET_WM_STATE_TOGGLE)
			set = !IS_OMNIPRESENT(wwin);

		wWindowSetOmnipresent(wwin, set);

	} else if (state == net_wm_state_shaded) {
		if (set == _NET_WM_STATE_TOGGLE)
			set = !wwin->flags.shaded;

		if (init) {
			wwin->flags.shaded = set;
		} else {
			if (set)
				wShadeWindow(wwin);
			else
				wUnshadeWindow(wwin);
		}
	} else if (state == net_wm_state_skip_taskbar) {
		if (set == _NET_WM_STATE_TOGGLE)
			set = !wwin->client_flags.skip_window_list;

		wwin->client_flags.skip_window_list = set;
		wwin->client_flags.skip_switchpanel = set;
	} else if (state == net_wm_state_skip_pager) {
		if (set == _NET_WM_STATE_TOGGLE)
			set = !wwin->flags.net_skip_pager;

		wwin->flags.net_skip_pager = set;
	} else if (state == net_wm_state_maximized_vert) {
		if (set == _NET_WM_STATE_TOGGLE)
			set = !(wwin->flags.maximized & MAX_VERTICAL);

		if (init || wwin->flags.fullscreen) {
			/* IS_RESIZABLE() is false while fullscreen so
			 * wMaximizeWindow() would drop the request, record
			 * the bit and let wUnfullscreenWindow() apply it */
			if (set)
				wwin->flags.maximized |= MAX_VERTICAL;
			else
				wwin->flags.maximized &= ~MAX_VERTICAL;
		} else {
			if (set)
				wMaximizeWindow(wwin, wwin->flags.maximized | MAX_VERTICAL,
						wGetHeadForWindow(wwin));
			else
				wMaximizeWindow(wwin, wwin->flags.maximized & ~MAX_VERTICAL,
						wGetHeadForWindow(wwin));
		}
	} else if (state == net_wm_state_maximized_horz) {
		if (set == _NET_WM_STATE_TOGGLE)
			set = !(wwin->flags.maximized & MAX_HORIZONTAL);

		if (init || wwin->flags.fullscreen) {
			if (set)
				wwin->flags.maximized |= MAX_HORIZONTAL;
			else
				wwin->flags.maximized &= ~MAX_HORIZONTAL;
		} else {
			if (set)
				wMaximizeWindow(wwin, wwin->flags.maximized | MAX_HORIZONTAL,
						wGetHeadForWindow(wwin));
			else
				wMaximizeWindow(wwin, wwin->flags.maximized & ~MAX_HORIZONTAL,
						wGetHeadForWindow(wwin));
		}
	} else if (state == net_wm_state_hidden) {
		if (set == _NET_WM_STATE_TOGGLE)
			set = !(wwin->flags.miniaturized);

		if (init) {
			wwin->flags.miniaturized = set;
		} else {
			if (set)
				wIconifyWindow(wwin);
			else
				wDeiconifyWindow(wwin);
		}
	} else if (state == net_wm_state_fullscreen) {
		if (set == _NET_WM_STATE_TOGGLE)
			set = !(wwin->flags.fullscreen);

		if (init) {
			wwin->flags.fullscreen = set;
		} else {
			if (set)
				wFullscreenWindow(wwin);
			else {
				wUnfullscreenWindow(wwin);
				wwin->flags.fullscreen_monitors[0] = -1;
			}
		}
	} else if (state == net_wm_state_above) {
		if (set == _NET_WM_STATE_TOGGLE)
			set = !(wwin->client_flags.floating);

		wwin->client_flags.floating = set;
		if (!init && wwin->frame)
			ChangeStackingLevel(wwin->frame->core, getWindowLayer(wwin));

	} else if (state == net_wm_state_below) {
		if (set == _NET_WM_STATE_TOGGLE)
			set = !(wwin->client_flags.sunken);

		wwin->client_flags.sunken = set;
		if (!init && wwin->frame)
			ChangeStackingLevel(wwin->frame->core, getWindowLayer(wwin));

	} else if (state == net_wm_state_demands_attention) {
		if (set == _NET_WM_STATE_TOGGLE)
			set = !wwin->flags.net_demands_attention;

		/* an already-focused window does not get to demand attention,
		 * the spec asks the WM to clear this state once the window
		 * has the user's attention */
		if (set && wwin->flags.focused)
			set = 0;

		wwin->flags.net_demands_attention = set;
		if (!init) {
			wAppBounceWhileUrgent(wApplicationOf(wwin->main_window));
			wWindowUrgentBlink(wwin);
		}

	} else if (state == net_wm_state_modal) {
		if (set == _NET_WM_STATE_TOGGLE)
			set = !wwin->flags.net_modal;

		wwin->flags.net_modal = set;

		if (!init && set && wwin->frame) {
			wRaiseFrame(wwin->frame->core);
			wSetFocusTo(wwin->screen_ptr, wwin);
		}

	} else {
#ifdef DEBUG_WMSPEC
		wmessage("doStateAtom unknown atom %s set %d", atom_name(state), set);
#endif
	}
}

static void removeIcon(WWindow *wwin)
{
	if (wwin->icon == NULL)
		return;
	if (wwin->flags.miniaturized && wwin->icon->mapped) {
		xcb_unmap_window(wdisp->conn, wwin->icon->core->window);
		RemoveFromStackList(wwin->icon->core);
		wIconDestroy(wwin->icon);
		wwin->icon = NULL;
	}
}

static bool handleWindowType(WWindow *wwin, xcb_atom_t type, int *layer)
{
	bool ret = true;

	if (type == net_wm_window_type_desktop) {
		wwin->client_flags.no_titlebar = 1;
		wwin->client_flags.no_resizable = 1;
		wwin->client_flags.no_miniaturizable = 1;
		wwin->client_flags.no_border = 1;
		wwin->client_flags.no_resizebar = 1;
		wwin->client_flags.no_shadeable = 1;
		wwin->client_flags.no_movable = 1;
		wwin->client_flags.omnipresent = 1;
		wwin->client_flags.skip_window_list = 1;
		wwin->client_flags.skip_switchpanel = 1;
		wwin->client_flags.dont_move_off = 1;
		wwin->client_flags.no_appicon = 1;
		wwin->flags.net_skip_pager = 1;
		wwin->frame_x = 0;
		wwin->frame_y = 0;
#ifdef VIRTUAL_DESKTOP
		wwin->client_flags.virtual_stick = 1;
#endif
	} else if (type == net_wm_window_type_dock) {
		wwin->client_flags.no_titlebar = 1;
		wwin->client_flags.no_resizable = 1;
		wwin->client_flags.no_miniaturizable = 1;
		wwin->client_flags.no_border = 1;	/* XXX: really not a single decoration. */
		wwin->client_flags.no_resizebar = 1;
		wwin->client_flags.no_shadeable = 1;
		wwin->client_flags.no_movable = 1;
		wwin->client_flags.omnipresent = 1;
		wwin->client_flags.skip_window_list = 1;
		wwin->client_flags.skip_switchpanel = 1;
		wwin->client_flags.dont_move_off = 1;
		wwin->flags.net_skip_pager = 1;
#ifdef VIRTUAL_DESKTOP
		wwin->client_flags.virtual_stick = 1;
#endif
	} else if (type == net_wm_window_type_toolbar ||
	           type == net_wm_window_type_menu) {
		wwin->client_flags.no_resizable = 1;
		wwin->client_flags.no_miniaturizable = 1;
		wwin->client_flags.no_resizebar = 1;
		wwin->client_flags.no_shadeable = 1;
		wwin->client_flags.skip_window_list = 1;
		wwin->client_flags.skip_switchpanel = 1;
		wwin->client_flags.dont_move_off = 1;
		wwin->client_flags.no_appicon = 1;
#ifdef VIRTUAL_DESKTOP
		wwin->client_flags.virtual_stick = 1;
#endif
	} else if (type == net_wm_window_type_dropdown_menu ||
			type == net_wm_window_type_popup_menu ||
			type == net_wm_window_type_combo) {
		wwin->client_flags.no_titlebar = 1;
		wwin->client_flags.no_resizable = 1;
		wwin->client_flags.no_miniaturizable = 1;
		wwin->client_flags.no_resizebar = 1;
		wwin->client_flags.no_shadeable = 1;
		wwin->client_flags.skip_window_list = 1;
		wwin->client_flags.skip_switchpanel = 1;
		wwin->client_flags.dont_move_off = 1;
		wwin->client_flags.no_appicon = 1;
	} else if (type == net_wm_window_type_utility) {
		wwin->client_flags.no_appicon = 1;
	} else if (type == net_wm_window_type_splash) {
		wwin->client_flags.no_titlebar = 1;
		wwin->client_flags.no_resizable = 1;
		wwin->client_flags.no_miniaturizable = 1;
		wwin->client_flags.no_resizebar = 1;
		wwin->client_flags.no_shadeable = 1;
		wwin->client_flags.no_movable = 1;
		wwin->client_flags.skip_window_list = 1;
		wwin->client_flags.skip_switchpanel = 1;
		wwin->client_flags.dont_move_off = 1;
		wwin->client_flags.no_appicon = 1;
		wwin->flags.net_skip_pager = 1;
	} else if (type == net_wm_window_type_dialog) {
		/* These also seem a bad idea in our context -Dan
		   // wwin->client_flags.skip_window_list = 1;
		   // wwin->client_flags.no_appicon = 1;
		 */
	} else if (type == net_wm_window_type_tooltip) {
		wwin->client_flags.no_titlebar = 1;
		wwin->client_flags.no_resizable = 1;
		wwin->client_flags.no_miniaturizable = 1;
		wwin->client_flags.no_resizebar = 1;
		wwin->client_flags.no_shadeable = 1;
		wwin->client_flags.no_movable = 1;
		wwin->client_flags.skip_window_list = 1;
		wwin->client_flags.skip_switchpanel = 1;
		wwin->client_flags.dont_move_off = 1;
		wwin->client_flags.no_appicon = 1;
		wwin->client_flags.no_focusable = 1;
		wwin->flags.net_skip_pager = 1;
	} else if (type == net_wm_window_type_notification) {
		wwin->client_flags.no_titlebar = 1;
		wwin->client_flags.no_resizable = 1;
		wwin->client_flags.no_miniaturizable = 1;
		wwin->client_flags.no_border = 1;
		wwin->client_flags.no_resizebar = 1;
		wwin->client_flags.no_shadeable = 1;
		wwin->client_flags.no_movable = 1;
		wwin->client_flags.omnipresent = 1;
		wwin->client_flags.skip_window_list = 1;
		wwin->client_flags.skip_switchpanel = 1;
		wwin->client_flags.dont_move_off = 1;
		wwin->client_flags.no_hide_others= 1;
		wwin->client_flags.no_appicon = 1;
		wwin->client_flags.no_focusable = 1;
		wwin->flags.net_skip_pager = 1;
	} else if (type == net_wm_window_type_dnd) {
		wwin->client_flags.no_titlebar = 1;
		wwin->client_flags.no_resizable = 1;
		wwin->client_flags.no_miniaturizable = 1;
		wwin->client_flags.no_border = 1;
		wwin->client_flags.no_resizebar = 1;
		wwin->client_flags.no_shadeable = 1;
		wwin->client_flags.no_movable = 1;
		wwin->client_flags.skip_window_list = 1;
		wwin->client_flags.skip_switchpanel = 1;
		wwin->client_flags.dont_move_off = 1;
		wwin->client_flags.no_appicon = 1;
		wwin->flags.net_skip_pager = 1;
	} else if (type == net_wm_window_type_normal) {
	} else {
		ret = false;
	}

	/* Restore decoration if the user has enabled the
	 * IgnoreDecorationChanges option */
	if (WFLAGP(wwin, ignore_decoration_changes)) {
		wwin->client_flags.no_titlebar = 0;
		wwin->client_flags.no_resizable = 0;
		wwin->client_flags.no_miniaturizable = 0;
		wwin->client_flags.no_resizebar = 0;
		wwin->client_flags.no_border = 0;
		wwin->client_flags.no_movable = 0;
	}

	wwin->type = type;
	*layer = getWindowLayer(wwin);

	return ret;
}

void wNETWMPositionSplash(WWindow *wwin, int *x, int *y, int width, int height)
{
	if (wwin->type == net_wm_window_type_splash) {
		WScreen *scr = wwin->screen_ptr;
		WMRect rect = wGetRectForHead(scr, wGetHeadForPointerLocation(scr));
		*x = rect.pos.x + (rect.size.width - width) / 2;
		*y = rect.pos.y + (rect.size.height - height) / 2;
	}
}

static void updateWindowType(WWindow *wwin)
{
	int layer = INT_MIN; //illegal level
	size_t i, nitems;
	xcb_atom_t *type;

	type = WXGetAtomList(wdisp->conn, wwin->client_win, net_wm_window_type, 32, &nitems);
	if (type) {
		for (i = 0; i < nitems; ++i) {
			if (handleWindowType(wwin, type[i], &layer))
				break;
		}
		wfree(type);
	}

	if (wwin->frame != NULL) {
		if (layer != INT_MIN)
			ChangeStackingLevel(wwin->frame->core, layer);
		wwin->frame->flags.need_texture_change = 1;
		wWindowConfigureBorders(wwin);
		wFrameWindowPaint(wwin->frame);
		wNETWMUpdateActions(wwin, false);
	}
}

static void addStrutCk(WScreen *scr, xcb_window_t w, WXCookie strut, WXCookie strut_partial)
{
	WReservedArea *area;
	uint32_t v[12];
	bool partial;

	/* consume both cookies on every path so callers do not have to,
	 * _NET_WM_STRUT_PARTIAL takes precedence over the legacy strut */
	if (WXRcvCardinalN(wdisp->conn, strut_partial, v, 12)) {
		WXDiscard(wdisp->conn, strut);
		partial = true;
	} else if (WXRcvCardinalN(wdisp->conn, strut, v, 4)) {
		partial = false;
	} else {
		return;
	}

	area = makeReservedArea(scr, w, v, partial);
	area->next = scr->netdata->strut;
	scr->netdata->strut = area;
}

void wNETWMReqClientHints(xcb_window_t window, WNETWMHintCookies *ck)
{
	ck->desktop       = WXReqFormat32(wdisp->conn, window, net_wm_desktop, XCB_ATOM_CARDINAL, 1);
	ck->state         = WXReqFormat32(wdisp->conn, window, net_wm_state, XCB_ATOM_ATOM, 1024);
	ck->type          = WXReqFormat32(wdisp->conn, window, net_wm_window_type, XCB_ATOM_ATOM, 1024);
	ck->strut         = WXReqFormat32(wdisp->conn, window, net_wm_strut, XCB_ATOM_CARDINAL, 4);
	ck->strut_partial = WXReqFormat32(wdisp->conn, window, net_wm_strut_partial, XCB_ATOM_CARDINAL, 12);
}

void wNETWMReqInitialStateCookies(xcb_window_t window, WNETWMInitialCookies *ck)
{
	ck->handled_icons = WXReqFormat32(wdisp->conn, window, net_wm_handled_icons, XCB_ATOM_CARDINAL, 1);
	ck->icon_geom     = WXReqFormat32(wdisp->conn, window, net_wm_icon_geometry, XCB_ATOM_CARDINAL, 4);
	ck->opacity       = WXReqFormat32(wdisp->conn, window, net_wm_window_opacity, XCB_ATOM_CARDINAL, 1);
	ck->user_time_win = WXReqFormat32(wdisp->conn, window, net_wm_user_time_window, XCB_ATOM_WINDOW, 1);
	ck->user_time     = WXReqFormat32(wdisp->conn, window, net_wm_user_time, XCB_ATOM_CARDINAL, 1);
	ck->startup_id    = WXReqString8(wdisp->conn, window, net_startup_id, utf8_string);
	/* icon last so its potentially large reply does not sit ahead of
	 * the small ones in the server's output queue */
	ck->icon          = WXReqFormat32(wdisp->conn, window, net_wm_icon, XCB_ATOM_CARDINAL, 1u << 22);
	ck->machine       = WXReqString8(wdisp->conn, window, XCB_ATOM_WM_CLIENT_MACHINE, XCB_ATOM_STRING);
}

WXCookie wNETWMReqPid(xcb_window_t window)
{
	return WXReqFormat32(wdisp->conn, window, net_wm_pid, XCB_ATOM_CARDINAL, 1);
}

void wNETWMRcvClientHints(WWindow *wwin, WNETWMHintCookies *ck, int *layer, int *workspace)
{
	int32_t desktop;
	size_t i, nitems;
	xcb_atom_t *atoms;

	if (WXRcvCardinalSigned(wdisp->conn, ck->desktop, &desktop)) {
		if (desktop == -1)
			wwin->client_flags.omnipresent = 1;
		else
			*workspace = desktop;
	}

	atoms = WXRcvAtomList(wdisp->conn, ck->state, &nitems);
	if (atoms) {
		for (i = 0; i < nitems; ++i)
			doStateAtom(wwin, atoms[i], _NET_WM_STATE_ADD, true);
		wfree(atoms);
	}

	atoms = WXRcvAtomList(wdisp->conn, ck->type, &nitems);
	if (atoms) {
		/* Retain the advertised _NET_WM_WINDOW_TYPE list and its
		 * resolved atom names on WWindow so the dynamic-rule
		 * MatchWindowType predicate can test against every type
		 * (not just the first one Window Maker understood) without
		 * calling wXGetAtomName() on the per-title-change re-eval
		 * path. Free any prior copy first: this path also runs on
		 * the inspector re-apply path (wWindowSetupInitialAttributes
		 * with ck=NULL), which re-reads the property.
		 * Cap the retained list so a hostile client cannot stall
		 * the WM with millions of XGetAtomName round trips. */
		size_t capped = nitems > NET_WM_WINDOW_TYPE_MAX ? NET_WM_WINDOW_TYPE_MAX : nitems;

		if (wwin->wm_types)
			wfree(wwin->wm_types);
		if (wwin->wm_type_names) {
			size_t k;

			for (k = 0; k < (size_t)wwin->wm_type_count; k++)
				if (wwin->wm_type_names[k])
					free(wwin->wm_type_names[k]);
			wfree(wwin->wm_type_names);
		}
		wwin->wm_types = NULL;
		wwin->wm_type_names = NULL;
		wwin->wm_type_count = 0;
		if (capped > 0) {
			size_t k;

			wwin->wm_types = wmalloc(sizeof(xcb_atom_t) * capped);
			memcpy(wwin->wm_types, atoms, sizeof(xcb_atom_t) * capped);
			wwin->wm_type_names = wmalloc(sizeof(char *) * capped);
			for (k = 0; k < capped; k++) {
				char *nm = wXGetAtomName(wdisp->conn, atoms[k]);

				wwin->wm_type_names[k] = nm; /* may be NULL */
			}
			wwin->wm_type_count = (int)capped;
		}

		for (i = 0; i < capped; ++i) {
			if (handleWindowType(wwin, atoms[i], layer))
				break;
		}
		wfree(atoms);
	}

	wNETWMUpdateActions(wwin, false);
	updateStrut(wwin->screen_ptr, wwin->client_win, false);
	addStrutCk(wwin->screen_ptr, wwin->client_win, ck->strut, ck->strut_partial);

	wScreenUpdateUsableArea(wwin->screen_ptr);
}

WXCookie wNETWMReqWindowName(xcb_window_t window)
{
	return WXReqString8(wdisp->conn, window, net_wm_name, utf8_string);
}

static bool updateNetIconInfoCk(WWindow *wwin, WXCookie handled_ck, WXCookie geom_ck)
{
	uint32_t handled;
	uint32_t geom[4];
	bool hasState = false;
	bool old_state = wwin->flags.net_handle_icon;

	if (WXRcvCardinal(wdisp->conn, handled_ck, &handled)) {
		wwin->flags.net_handle_icon = (handled != 0);
		hasState = true;
	} else {
		wwin->flags.net_handle_icon = false;
	}

	if (WXRcvCardinalN(wdisp->conn, geom_ck, geom, 4)) {
#ifdef NETWM_PROPER
		if (wwin->flags.net_handle_icon)
#else
		wwin->flags.net_handle_icon = true;
#endif
		{
			wwin->icon_x = geom[0];
			wwin->icon_y = geom[1];
			wwin->icon_w = geom[2];
			wwin->icon_h = geom[3];
		}

		hasState = true;
	} else {
		wwin->flags.net_handle_icon = false;
	}

	if (wwin->flags.miniaturized && old_state != wwin->flags.net_handle_icon) {
		if (wwin->flags.net_handle_icon) {
			removeIcon(wwin);
		} else {
			wwin->flags.miniaturized = false;
			wwin->flags.skip_next_animation = true;
			wIconifyWindow(wwin);
		}
	}

	return hasState;
}

static bool updateNetIconInfo(WWindow *wwin)
{
	return updateNetIconInfoCk(wwin,
		WXReqFormat32(wdisp->conn, wwin->client_win, net_wm_handled_icons, XCB_ATOM_CARDINAL, 1),
		WXReqFormat32(wdisp->conn, wwin->client_win, net_wm_icon_geometry, XCB_ATOM_CARDINAL, 4));
}

static void matchStartupNotification(WWindow *wwin, WNETWMInitialCookies *ck)
{
	char *sid;
	xcb_timestamp_t launch_ts = 0;
	int sn_ws = -1;
	pid_t pid;

	sid = WXRcvString(wdisp->conn, ck->startup_id, NULL);
	if (!sid && wwin->main_window != XCB_NONE && wwin->main_window != wwin->client_win
	    && !wwin->fake_group)
		/* a real client-set group leader may carry the id when
		 * the toplevel does not, but a wmaker-created fake leader
		 * never does so skip that round-trip */
		sid = WXGetUTF8String(wdisp->conn, wwin->main_window, net_startup_id, NULL);

	pid = wwin->client_pid;

	if (wStartupNotifyMatchWindow(wwin->screen_ptr, sid,
				      wwin->wm_class, wwin->wm_instance,
				      pid, &launch_ts, &sn_ws)) {
		/* a leaderless toplevel that we know we launched should
		 * still get a WApplication and an appicon so the dock tile
		 * can attach to it and clear when the window goes away,
		 * treat it as if EmulateAppIcon was set for this map only */
		if (wwin->main_window == XCB_NONE && wwin->transient_for == XCB_NONE
		    && !wwin->flags.is_dockapp)
			wwin->main_window = wwin->client_win;

		/* keep an explicit _NET_WM_USER_TIME = 0 authoritative,
		 * the existing focus policy treats it as "do not focus me"
		 * regardless of how the window was launched */
		if (launch_ts && (!wwin->net_user_time_set
				  || (wwin->net_user_time != 0
				      && USER_TIME_NEWER(launch_ts, wwin->net_user_time)))) {
			wwin->net_user_time = launch_ts;
			wwin->net_user_time_set = 1;
			/* advance the screen baseline the same way
			 * readUserTime() does so a later window with an
			 * older timestamp cannot steal focus from this one */
			if (wwin->screen_ptr->net_last_user_time == 0
			    || USER_TIME_NEWER(launch_ts, wwin->screen_ptr->net_last_user_time))
				wwin->screen_ptr->net_last_user_time = launch_ts;
		}
		if (sn_ws >= 0)
			wwin->sn_workspace = sn_ws;
	}
	wfree(sid);
}

void wNETWMCheckInitialClientStateCk(WWindow *wwin, WNETWMInitialCookies *ck)
{
#ifdef DEBUG_WMSPEC
	wmessage("wNETWMCheckInitialClientState");
#endif

	wNETWMShowingDesktop(wwin->screen_ptr, false);

	/* wNETWMRcvClientHints() already consumed _NET_WM_WINDOW_TYPE and
	 * stored the matched atom (or left wwin->type as None when the
	 * property was absent) so reapply the derived client flags from
	 * that instead of fetching the property again. Only the GNUstep
	 * branch in wWindowSetupInitialAttributes() skips
	 * wNETWMRcvClientHints(), so re-read for those windows only. The
	 * frame does not exist yet here so the frame-update tail of
	 * updateWindowType() is a no-op either way. */
	if (wwin->wm_gnustep_attr) {
		updateWindowType(wwin);
	} else if (wwin->type != XCB_NONE) {
		int layer;
		handleWindowType(wwin, wwin->type, &layer);
	}

	updateNetIconInfoCk(wwin, ck->handled_icons, ck->icon_geom);
	updateIconImageCk(wwin, ck->icon);

	updateUserTimeWindow(wwin, &ck->user_time_win);
	readUserTimeCk(wwin, ck->user_time);
	matchStartupNotification(wwin, ck);

	cacheLocalPid(wwin, ck->machine);
	wNETWMSyncRequestSetup(wwin);
}

void wNETWMCheckInitialClientState(WWindow *wwin)
{
	WNETWMInitialCookies ck;

	wNETWMReqInitialStateCookies(wwin->client_win, &ck);
	wNETWMCheckInitialClientStateCk(wwin, &ck);
	WXDiscard(wdisp->conn, ck.opacity);
}

void wNETWMCheckInitialFrameStateCk(WWindow *wwin, WXCookie opacity)
{
#ifdef DEBUG_WMSPEC
	wmessage("wNETWMCheckInitialFrameState");
#endif

	updateWindowOpacityCk(wwin, opacity);
}

void wNETWMCheckInitialFrameState(WWindow *wwin)
{
	wNETWMCheckInitialFrameStateCk(wwin,
		WXReqFormat32(wdisp->conn, wwin->client_win, net_wm_window_opacity, XCB_ATOM_CARDINAL, 1));
}

static void handleDesktopNames(WScreen *scr)
{
	char *data, *name;
	size_t i, len;
	int n;

	data = WXGetUTF8String(wdisp->conn, scr->root_win, net_desktop_names, &len);
	if (data == NULL)
		return;

	/* the property is a sequence of NUL-terminated UTF-8 strings, one per
	 * workspace from index 0, walk it and rename each workspace as its
	 * terminator is reached, WXGetUTF8String guarantees data[len] == '\0'
	 * so an unterminated trailing name is still applied */
	n = 0;
	name = data;
	for (i = 0; i <= len; i++) {
		if (data[i] == 0) {
			if (*name != 0)
				wWorkspaceRename(scr, n, name);
			n++;
			if (n >= MAX_WORKSPACES)
				break;
			name = &data[i + 1];
		}
	}
	wfree(data);
}

bool wNETWMProcessClientMessage(const xcb_client_message_event_t *event)
{
	WScreen *scr;
	WWindow *wwin;

#ifdef DEBUG_WMSPEC
	wmessage("processClientMessage type %s", atom_name(event->type));
#endif

	/* every NETWM client message carries 32-bit data fields, refuse
	 * format-8/16 spoofs before reading data.data32[] */
	if (event->format != 32)
		return false;

	scr = wScreenForWindow(event->window);
	if (scr) {
		/* generic client messages */
		if (event->type == net_current_desktop) {
			wWorkspaceChange(scr, event->data.data32[0]);
			return true;

		} else if (event->type == net_number_of_desktops) {
			long value;

			/* the wire word is CARD32, cast through int32_t so a
			 * client that sent a negative-as-32-bit count still
			 * lands in the value < 1 clamp below on LP64 */
			value = (int32_t)event->data.data32[0];
			if (value < 1)
				value = 1;
			else if (value > MAX_WORKSPACES)
				value = MAX_WORKSPACES;

			if (value > scr->workspace_count) {
				wWorkspaceMake(scr, value - scr->workspace_count);
			} else if (value < scr->workspace_count) {
				int i;
				bool rebuild = false;

				for (i = scr->workspace_count - 1; i >= value; i--) {
					if (!wWorkspaceDelete(scr, i)) {
						rebuild = true;
						break;
					}
				}

				if (rebuild)
					updateWorkspaceCount(scr);
			}
			return true;

		} else if (event->type == net_showing_desktop) {
			wNETWMShowingDesktop(scr, event->data.data32[0]);
			return true;
#ifdef VIRTUAL_DESKTOP
		} else if (event->type == net_desktop_viewport) {
			int ws = scr->current_workspace;
			int vx = (int32_t)event->data.data32[0];
			int vy = (int32_t)event->data.data32[1];
			WWorkspace *wptr = scr->workspaces[ws];

			/* vx/vy are untrusted client input: compare in
			 * 64-bit so vx + scr_width cannot overflow int and
			 * slip past the clamp */
			if ((int64_t)vx >= wptr->vdesk_x && (int64_t)vy >= wptr->vdesk_y
			    && (int64_t)vx + scr->scr_width <= (int64_t)wptr->vdesk_x + wptr->vdesk_w
			    && (int64_t)vy + scr->scr_height <= (int64_t)wptr->vdesk_y + wptr->vdesk_h) {
				wVdeskSetViewport(scr, ws, vx, vy);
				return true;
			}
#endif
		} else if (event->type == net_desktop_names) {
			handleDesktopNames(scr);
			return true;

		} else if (event->type == wdisp->atom.wm.protocols
			   && (xcb_atom_t)event->data.data32[0] == net_wm_ping) {
			/* client echoed our ping back to the root window,
			 * data.l[2] is the client window per spec */
			wwin = wWindowFor((xcb_window_t)event->data.data32[2]);
			if (wwin && wwin->net_ping.timer
			    && (xcb_timestamp_t)event->data.data32[1] == wwin->net_ping.timestamp)
				wNETWMCancelPing(wwin);
			return true;
		}
	}

	/* _NET_REQUEST_FRAME_EXTENTS is sent for a window that is not yet
	 * mapped or managed, so handle it before requiring a WWindow. The
	 * spec only asks for an estimate, reply with the extents a normally
	 * decorated window would get under the current theme, which is what
	 * clients use this for (placing themselves so the framed window
	 * lands where they expect). If the window turns out to be a splash
	 * or borderless type the real extents replace this on map. */
	if (event->type == net_request_frame_extents) {
		const WConfig *cfg = w_global.config;
		uint32_t extents[4];
		int top;

		if (!scr)
			scr = wScreenForWindow(event->window);
		if (!scr)
			return true;
		top = WSCALE(scr, WMFontHeight(scr->title_font)
		      + (cfg->window_title_clearance + TITLEBAR_EXTEND_SPACE) * 2);
		if (top > cfg->window_title_max_height)
			top = cfg->window_title_max_height;
		if (top < cfg->window_title_min_height)
			top = cfg->window_title_min_height;
		extents[0] = scr->frame_border_width;
		extents[1] = scr->frame_border_width;
		extents[2] = top + scr->frame_border_width;
		extents[3] = scr->resizebar_height + scr->frame_border_width;
		WXSetCardinalN(wdisp->conn, event->window, net_frame_extents, extents, 4);
		wXFlush(wdisp->conn);
		return true;
	}

	/* window specific client messages */

	wwin = wWindowFor(event->window);
	if (!wwin)
		return false;

	if (event->type == net_active_window) {
		WScreen *scr = wwin->screen_ptr;
		long source = event->data.data32[0];
		xcb_timestamp_t ev_time = (xcb_timestamp_t)event->data.data32[1];
		bool same_workspace;
		bool same_head = true;
		bool allow;

		/* a managed window without a frame is mid-teardown,
		 * activating it would dereference frame in the
		 * workspace/wMakeWindowVisible paths below */
		if (!wwin->frame)
			return true;

		same_workspace = WWIN_PRIMARY_WS(wwin) == scr->current_workspace;

		if (w_global.config->per_head_workspaces) {
			/* In per-head mode the target may be visible on a
			 * head whose workspace differs from the mirror,
			 * collapse both gates into one visibility check. */
			same_workspace = wWindowIsVisibleOnAnyHead(wwin);
			same_head = true;
		} else if (scr->xine_info.count > 1 && scr->focused_window
		    && scr->focused_window->flags.mapped)
			same_head = wGetHeadForWindow(wwin)
				    == wGetHeadForWindow(scr->focused_window);

		/*
		 * source 2 is a pager or taskbar, the user clicked on it so
		 * always honour. Everything else, including source 0 (legacy)
		 * and out-of-range values, is treated as an application
		 * asking for itself, since any X client can choose data.l[0]
		 * and a forged 0 must not bypass the policy. Application
		 * requests may not warp workspace or head unless the
		 * inspector explicitly allows it for this client, and are
		 * refused outright when the supplied timestamp is older than
		 * the last user activity seen on the screen.
		 */
		if (source != 2 && ev_time != 0 && scr->net_last_user_time != 0
		    && USER_TIME_NEWER(scr->net_last_user_time, ev_time)) {
			allow = false;
		} else if (source == 2 || WFLAGP(wwin, focus_across_wksp)) {
			allow = true;
		} else {
			allow = same_workspace
				&& (same_head || w_global.config->auto_focus_across_heads);
		}

		if (allow) {
			/* A client must not undo the saved minimized state while it is
			 * being restored. Pager requests still represent explicit user
			 * activation and remain actionable. */
			if (wwin->flags.restore_iconified && source != 2)
				return true;

			/* an honoured non-pager request carries a user-activity
			 * timestamp, advance the focus-stealing baseline so a
			 * later request with an older stamp is recognised as
			 * stale even if no _NET_WM_USER_TIME write intervenes */
			if (source != 2 && ev_time != 0
			    && (scr->net_last_user_time == 0
				|| USER_TIME_NEWER(ev_time, scr->net_last_user_time)))
				scr->net_last_user_time = ev_time;
			wNETWMShowingDesktop(scr, false);
			wMakeWindowVisible(wwin);
			warpPointerToFocusedWindow(wwin);
		} else if (!wwin->flags.focused && !wwin->flags.net_demands_attention) {
			wwin->flags.net_demands_attention = 1;
			updateStateHint(wwin, false, false);
			wAppBounceWhileUrgent(wApplicationOf(wwin->main_window));
			wWindowUrgentBlink(wwin);
		}
		return true;

	} else if (event->type == net_close_window) {
		if (!WFLAGP(wwin, no_closable)) {
			if (wwin->protocols.DELETE_WINDOW) {
				wClientSendProtocol(wwin, wdisp->atom.wm.delete_window,
										  wdisp->timestamp.last_event);
				wNETWMPingWindow(wwin, wdisp->timestamp.last_event);
			}
		}
		return true;

	} else if (event->type == net_moveresize_window) {
		xcb_configure_request_event_t xcre;
		unsigned long flags;
		int gravity;
		int saved_gravity;

		if (wwin->flags.internal_window || !wwin->normal_hints)
			return true;

		flags = (unsigned long)event->data.data32[0];
		gravity = flags & 0xff;
		saved_gravity = wwin->normal_hints->win_gravity;

		/*
		 * Reuse the ConfigureRequest path so frame extents, size
		 * constraints and maximize state are handled in one place.
		 * Presence bits 8-11 map directly onto CWX..CWHeight.
		 */
		memset(&xcre, 0, sizeof(xcre));
		xcre.window = event->window;
		xcre.value_mask = (flags >> 8) & (XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT);
		xcre.x = (int16_t)event->data.data32[1];
		xcre.y = (int16_t)event->data.data32[2];
		xcre.width = (uint16_t)event->data.data32[3];
		xcre.height = (uint16_t)event->data.data32[4];
		xcre.border_width = wwin->old_border_width;

		/* gravity 0 means honour the window's own gravity, anything
		 * outside the X11 gravity range is hostile input */
		if (gravity >= XCB_GRAVITY_NORTH_WEST && gravity <= XCB_GRAVITY_STATIC)
			wwin->normal_hints->win_gravity = gravity;
		wClientConfigure(wwin, &xcre);
		wwin->normal_hints->win_gravity = saved_gravity;
		return true;

	} else if (event->type == net_restack_window) {
		xcb_configure_request_event_t xcre;
		long source = event->data.data32[0];
		long detail = event->data.data32[2];
		WWindow *sib;

		/* data32[0] = source indication (1=app, 2=pager), l[1] =
		 * sibling, l[2] = stack mode (Above..Opposite). Route through
		 * the ConfigureRequest path so the sibling is translated to
		 * its frame and the managed stacking list is rebuilt in one
		 * place. Refuse a detail outside the X11 range, and only
		 * honour the sibling when it resolves to a managed client
		 * whose frame is a real sibling of ours under root, otherwise
		 * wClientConfigure() forwards the raw id to wXConfigureWindow()
		 * and a hostile non-sibling triggers BadMatch.
		 *
		 * Source indication: only source 2 (pager) may restack a
		 * window relative to an arbitrary sibling. Source 1 (app)
		 * and source 0 (legacy) may only restack the window itself
		 * (no sibling), matching the EWMH policy that an application
		 * must not reorder other clients' windows. */
		if (detail < XCB_STACK_MODE_ABOVE || detail > XCB_STACK_MODE_OPPOSITE)
			return true;
		if (source != 2 && event->data.data32[1] != XCB_NONE)
			return true;
		memset(&xcre, 0, sizeof(xcre));
		xcre.window = event->window;
		xcre.value_mask = XCB_CONFIG_WINDOW_STACK_MODE;
		xcre.stack_mode = (uint8_t)detail;
		sib = (event->data.data32[1] != XCB_NONE) ? wWindowFor((xcb_window_t)event->data.data32[1]) : NULL;
		if (sib && sib != wwin && sib->screen_ptr == wwin->screen_ptr) {
			xcre.value_mask |= XCB_CONFIG_WINDOW_SIBLING;
			xcre.sibling = sib->client_win;
		}
		xcre.border_width = wwin->old_border_width;
		wClientConfigure(wwin, &xcre);
		return true;

	} else if (event->type == net_wm_state) {
		int maximized = wwin->flags.maximized;
		long set = event->data.data32[0];

#ifdef DEBUG_WMSPEC
		wmessage("net_wm_state set %ld a1 %s a2 %s", set,
		       atom_name(event->data.data32[1]), atom_name(event->data.data32[2]));
#endif

		doStateAtom(wwin, (xcb_atom_t) event->data.data32[1], set, false);
		if (event->data.data32[2] && event->data.data32[2] != event->data.data32[1])
			doStateAtom(wwin, (xcb_atom_t) event->data.data32[2], set, false);

		if (wwin->flags.maximized != maximized && !wwin->flags.fullscreen) {
			if (!wwin->flags.maximized) {
				wwin->flags.maximized = maximized;
				wUnmaximizeWindow(wwin);
			} else {
				wMaximizeWindow(wwin, wwin->flags.maximized,
						wGetHeadForWindow(wwin));
			}
		}
		updateStateHint(wwin, false, false);
		return true;

	} else if (event->type == net_wm_handled_icons || event->type == net_wm_icon_geometry) {
		updateNetIconInfo(wwin);
		return true;

	} else if (event->type == net_wm_desktop) {
		/* 0xFFFFFFFF means all desktops, cast through int32_t so
		 * the -1 comparison holds on LP64 where widening a
		 * uint32_t to long would give 4294967295 instead */
		long desktop = (int32_t)event->data.data32[0];

		if (desktop == -1) {
			wWindowSetOmnipresent(wwin, true);
		} else {
			if (IS_OMNIPRESENT(wwin))
				wWindowSetOmnipresent(wwin, false);
			wWindowChangeWorkspace(wwin, desktop);
		}
		return true;

	} else if (event->type == net_wm_moveresize) {
		xcb_motion_notify_event_t *fake_event = NULL;
		int direction = event->data.data32[2];
		int x_root = event->data.data32[0];
		int y_root = event->data.data32[1];
		int button = event->data.data32[3];
		unsigned int mask;
		int grab;
		int tries;

		if (!wwin->frame)
			return true;

		if (direction == _NET_WM_MOVERESIZE_CANCEL) {
			if (wwin->moveresize.active) {
				xcb_button_release_event_t fake;

				memset(&fake, 0, sizeof(fake));
				fake.response_type = XCB_BUTTON_RELEASE;
				fake.event = wwin->frame->core->window;
				fake.root_x = x_root;
				fake.root_y = y_root;
				fake.detail = event->data.data32[3];
				WXSendEvent32(wdisp->conn, &fake, sizeof(fake), 0,
					      wwin->frame->core->window,
					      XCB_EVENT_MASK_BUTTON_RELEASE);
			}
			return true;
		}

		/* Check if already in progress */
		if (wwin->moveresize.active)
			return true;

		/* Check if the initiating button is actually pressed, and
		 * that the pointer is on this screen (XQueryPointer's
		 * cross-screen refusal, lost when the helper discarded
		 * same_screen) */
		if (!wXQueryPointerSameScreen(wdisp->conn, wwin->screen_ptr->root_win, &mask))
			return true;

		if (button > 0) {
			unsigned int expected =
				(button >= 1 && button <= 3) ?
				(unsigned int)(1 << (button + 7)) : 0;

			if (expected && !(mask & expected))
				/* Race: button already released */
				return true;
		}

		/* Check if operation is allowed */
		if (direction == _NET_WM_MOVERESIZE_MOVE) {
			if (!IS_MOVABLE(wwin))
				return true;
		} else if (direction >= 0 && direction <= _NET_WM_MOVERESIZE_SIZE_LEFT) {
			if (!IS_RESIZABLE(wwin) || wwin->flags.shaded)
				return true;
		} else {
			return true;
		}

		/* a CSD client can still hold its implicit grab here,
		 * retry while the initiating button is down */
		grab = wXGrabPointer(wdisp->conn, wwin->frame->core->window, false,
				     XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_PRESS,
				     XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
				     XCB_NONE, XCB_NONE, XCB_CURRENT_TIME);
		for (tries = 0; grab == WXGrabAlreadyGrabbed && tries < 15; tries++) {
			unsigned int held;

			if (!wXQueryPointerSameScreen(wdisp->conn, wwin->screen_ptr->root_win, &mask))
				return true;
			if (button >= 1 && button <= 3)
				held = (unsigned int)(1 << (button + 7));
			else
				held = (1 << 8) | (1 << 9) | (1 << 10);
			if (!(mask & held))
				return true;

			wusleep(10000);
			grab = wXGrabPointer(wdisp->conn, wwin->frame->core->window, false,
					     XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_PRESS,
					     XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
					     XCB_NONE, XCB_NONE, XCB_CURRENT_TIME);
		}
		if (grab != WXGrabSuccess)
			return true;

		/* Set up the move/resize state */
		wwin->moveresize.active = 1;
		wwin->moveresize.resize_edge = direction;

		/* synthesized motion event in wire form, moveres only
		 * reads the pointer position and the target window */
		fake_event = calloc(1, sizeof(*fake_event));
		if (!fake_event) {
			wwin->moveresize.active = 0;
			wXUngrabPointer(wdisp->conn, XCB_CURRENT_TIME);
			return true;
		}
		fake_event->response_type = XCB_MOTION_NOTIFY;
		fake_event->root_x = x_root;
		fake_event->root_y = y_root;
		fake_event->event = wwin->frame->core->window;
		if (direction == _NET_WM_MOVERESIZE_MOVE)
			wMouseMoveWindow(wwin, (xcb_generic_event_t *)fake_event);
		else
			wMouseResizeWindow(wwin, (xcb_generic_event_t *)fake_event);
		free(fake_event);

		if (wwin->moveresize.active) {
			wXUngrabPointer(wdisp->conn, XCB_CURRENT_TIME);
			wwin->moveresize.active = 0;
		}

		return true;

	} else if (event->type == net_wm_fullscreen_monitors) {
		unsigned long top, bottom, left, right, src_indication;

		top = event->data.data32[0];
		bottom = event->data.data32[1];
		left = event->data.data32[2];
		right = event->data.data32[3];
		src_indication = event->data.data32[4];

		if (src_indication > 1)
			wwarning("_NET_WM_FULLSCREEN_MONITORS source indication %lu not supported", src_indication);

		wFullscreenMonitorsWindow(wwin, top, bottom, left, right);
		return true;
	}

#ifdef DEBUG_WMSPEC
	wmessage("processClientMessage unsupported type %s", atom_name(event->type));
#endif
	return false;
}

void wNETWMCheckClientHintChange(WWindow *wwin, xcb_atom_t atom)
{
	char *name = NULL, *tmp = NULL;
#ifdef DEBUG_WMSPEC
	wmessage("clientHintChange type %s", atom_name(atom));
#endif

	if (atom == net_wm_strut || atom == net_wm_strut_partial) {
		updateStrut(wwin->screen_ptr, wwin->client_win, false);
		updateStrut(wwin->screen_ptr, wwin->client_win, true);
		wScreenUpdateUsableArea(wwin->screen_ptr);
	} else if (atom == net_wm_handled_icons || atom == net_wm_icon_geometry) {
		updateNetIconInfo(wwin);
	} else if (atom == net_wm_window_type) {
		updateWindowType(wwin);
	} else if (atom == net_wm_name) {
		name = wNETWMGetWindowName(wwin->client_win);
		/* an empty property counts as absent, like the icon name */
		if (name && name[0] == '\0') {
			wfree(name);
			name = NULL;
		}
		if (name) {
			wwin->flags.net_has_title = 1;
			wWindowUpdateName(wwin, name);
			wfree(name);
		} else {
			/* the UTF-8 title is gone, fall back to the legacy
			 * title now, a WM_NAME notify may never arrive */
			wwin->flags.net_has_title = 0;
			if (!wFetchName(wdisp->conn, wwin->client_win, &tmp))
				wWindowUpdateName(wwin, NULL);
			else
				wWindowUpdateName(wwin, tmp);
			if (tmp)
				wfree(tmp);
		}
	} else if (atom == net_wm_icon_name) {
		if (wwin->icon) {
			wIconChangeTitle(wwin->icon, wwin);
			wIconPaint(wwin->icon);
		}
	} else if (atom == net_wm_icon) {
		updateIconImage(wwin);
	} else if (atom == net_wm_window_opacity) {
		updateWindowOpacity(wwin);
	} else if (atom == net_wm_user_time) {
		readUserTime(wwin);
	} else if (atom == net_wm_user_time_window) {
		updateUserTimeWindow(wwin, NULL);
		readUserTime(wwin);
	}
}

int wNETWMGetPidForWindow(xcb_window_t window)
{
	uint32_t pid;

	if (WXGetCardinal(wdisp->conn, window, net_wm_pid, &pid))
		return (int)pid;
	return 0;
}

/* ---- _NET_WM_PING --------------------------------------------------- */

/* how long to wait for the echoed reply before treating the client as
 * hung and prompting the user, in milliseconds */
#define NET_WM_PING_TIMEOUT 5000

bool wNETWMIsPingProtocol(xcb_atom_t protocol)
{
	return protocol == net_wm_ping;
}

void wNETWMCancelPing(WWindow *wwin)
{
	WMDeleteTimerHandlerPtr(&wwin->net_ping.timer);
	wwin->net_ping.timestamp = 0;
	wwin->net_ping.term_sent = 0;
}

/* cache the manage-time pid for Force Quit only when the client
 * explicitly names this host so a later property change cannot redirect
 * the kill at an arbitrary process, and a missing WM_CLIENT_MACHINE is
 * treated as remote. wwin->client_pid was already populated by
 * wGetWindowPid() (XRes preferred, falling back to _NET_WM_PID), so
 * use that instead of reading the property again, an XRes-supplied
 * value is local by construction so the hostname check still passes. */
static void cacheLocalPid(WWindow *wwin, WXCookie machine_ck)
{
	char host[256], *machine;
	int pid;

	wwin->net_ping.pid = 0;

	pid = (int)wwin->client_pid;
	machine = WXRcvString(wdisp->conn, machine_ck, NULL);

	/* Retain the already-fetched WM_CLIENT_MACHINE string on WWindow so
	 * the dynamic-rule MatchMachine predicate can read it without a
	 * second round-trip. The string is freed in wWindowDestroy(). An
	 * empty string is dropped to NULL, mirroring wm_role handling.
	 * The inspector re-apply path re-runs this after manage time, so
	 * release any prior value first. */
	wfree(wwin->wm_machine);
	wwin->wm_machine = NULL;
	if (machine && machine[0] != '\0')
		wwin->wm_machine = machine;
	else
		wfree(machine);

	if (pid <= 0)
		return;

	if (gethostname(host, sizeof(host)) == 0) {
		host[sizeof(host) - 1] = '\0';
		if (wwin->wm_machine && strcmp(host, wwin->wm_machine) == 0)
			wwin->net_ping.pid = pid;
	}
}

static void handlePingTimeout(void *cdata)
{
	WWindow *wwin = cdata;
	const char *title;
	char *msg;
	int pid, result;

	wwin->net_ping.timer = NULL;

	wretain(wwin);

	title = (wwin->frame && wwin->frame->title) ? wwin->frame->title : "";
	msg = wstrconcat(title,
			 _(" is not responding.\n\n"
			   "You can wait for it to continue or force it to quit, "
			   "losing any unsaved changes."));

	result = wMessageDialog(wwin->screen_ptr, _("Application Not Responding"),
				msg, _("Force Quit"), _("Wait"), NULL);
	wfree(msg);

	if (wwin->flags.destroyed) {
		/* client went away while the dialog was up */
		wrelease(wwin);
		return;
	}

	if (result == WAPRDefault) {
		pid = wwin->net_ping.pid;
		if (pid > 0) {
			/* first Force Quit asks nicely, a second one (after
			 * the next ping also times out) escalates */
			if (!wwin->net_ping.term_sent) {
				if (kill((pid_t)pid, SIGTERM) == 0) {
					wwin->net_ping.term_sent = 1;
					wNETWMPingWindow(wwin, wdisp->timestamp.last_event);
				} else {
					wwarning(_("could not signal process %d (%s), "
						   "dropping its X connection instead"),
						 pid, strerror(errno));
					wClientKill(wwin);
				}
			} else if (kill((pid_t)pid, SIGKILL) != 0) {
				wwarning(_("could not kill process %d (%s), "
					   "dropping its X connection instead"),
					 pid, strerror(errno));
				wClientKill(wwin);
			}
		} else {
			wClientKill(wwin);
		}
	} else {
		/* Wait, ping again so a still-hung client re-prompts */
		wNETWMPingWindow(wwin, wdisp->timestamp.last_event);
	}
	wrelease(wwin);
}

void wNETWMPingWindow(WWindow *wwin, xcb_timestamp_t timestamp)
{
	if (!wwin->protocols.NET_WM_PING)
		return;
	/* one outstanding ping per window, the reply for the earlier close
	 * attempt is just as good an alive signal */
	if (wwin->net_ping.timer)
		return;

	if (timestamp == XCB_CURRENT_TIME)
		timestamp = wdisp->timestamp.last_event;
	wwin->net_ping.timestamp = timestamp;

	WXSendClientMessage(wdisp->conn, wwin->client_win, XCB_EVENT_MASK_NO_EVENT,
			    wwin->client_win, wdisp->atom.wm.protocols,
			    (long)net_wm_ping, (long)timestamp,
			    (long)wwin->client_win, 0, 0);
	wXFlush(wdisp->conn);

	wwin->net_ping.timer =
		WMAddTimerHandler(NET_WM_PING_TIMEOUT, handlePingTimeout, wwin);
}

/* ---- _NET_WM_USER_TIME / _NET_WM_USER_TIME_WINDOW ------------------- */

/* maps the indirected user-time carrier window back to its WWindow so
 * PropertyNotify on the helper can be routed without a linear scan */
static WMHashTable *user_time_table = NULL;

static void applyUserTime(WWindow *wwin, uint32_t ts)
{
	wwin->net_user_time = (xcb_timestamp_t)ts;
	wwin->net_user_time_set = 1;

	/* track the newest activity timestamp seen on this screen so a
	 * later map can be compared against it */
	if (ts != 0 && (wwin->screen_ptr->net_last_user_time == 0
			|| USER_TIME_NEWER(ts, wwin->screen_ptr->net_last_user_time)))
		wwin->screen_ptr->net_last_user_time = (xcb_timestamp_t)ts;
}

static void readUserTime(WWindow *wwin)
{
	xcb_window_t src = wwin->net_user_time_win ? wwin->net_user_time_win
					     : wwin->client_win;
	uint32_t ts;

	if (WXGetCardinal(wdisp->conn, src, net_wm_user_time, &ts))
		applyUserTime(wwin, ts);
}

/* the prefetched cookie was issued against client_win, when the client
 * indirects through _NET_WM_USER_TIME_WINDOW the value lives on a
 * different window so discard the cookie and read that one instead */
static void readUserTimeCk(WWindow *wwin, WXCookie ck)
{
	uint32_t ts;

	if (wwin->net_user_time_win != XCB_NONE) {
		WXDiscard(wdisp->conn, ck);
		readUserTime(wwin);
	} else if (WXRcvCardinal(wdisp->conn, ck, &ts)) {
		applyUserTime(wwin, ts);
	}
}

static void detachUserTimeWindow(WWindow *wwin)
{
	if (!user_time_table || wwin->net_user_time_win == XCB_NONE)
		return;
	WMHashRemove(user_time_table, (void *)(uintptr_t) wwin->net_user_time_win);
	/* restore exactly the mask we held before attaching, never blanket
	 * NoEventMask, so a malicious XID cannot trick us into dropping
	 * SubstructureRedirect on root or button grabs on a frame */
	xcb_change_window_attributes(wdisp->conn, wwin->net_user_time_win, XCB_CW_EVENT_MASK, (const uint32_t[]){ wwin->net_user_time_win_mask });
	wwin->net_user_time_win = XCB_NONE;
	wwin->net_user_time_win_mask = 0;
}

/* the helper XID comes from a client property and is hostile input,
 * refuse to select on anything we already manage or already hold a mask
 * on, that covers root, frames, client windows, icons and the supporting
 * window without having to enumerate them */
static bool isSafeUserTimeWindow(WWindow *wwin, xcb_window_t win, long *prev_mask)
{
	WXWindowAttributes attr;
	WScreen *scr;
	int i;

	if (win == XCB_NONE || win == wwin->client_win)
		return false;
	if (WMHashGet(wdisp->context.client_win, (void *)(uintptr_t) win) != NULL)
		return false;
	if (user_time_table
	    && WMHashGet(user_time_table, (void *)(uintptr_t) win) != NULL)
		return false;
	for (i = 0; i < wdisp->screen_count; i++) {
		scr = wScreenWithNumber(i);
		if (win == scr->root_win || win == scr->info_window || win == scr->no_focus_win
		    || win == scr->dock_shadow || win == scr->mini_screenshot)
			return false;
	}
	if (!wXGetWindowAttributes(wdisp->conn, win, &attr, NULL))
		return false;
	if (attr.your_event_mask != 0)
		return false;

	*prev_mask = attr.your_event_mask;
	return true;
}

static void updateUserTimeWindow(WWindow *wwin, WXCookie *ck)
{
	xcb_window_t win = XCB_NONE;
	long prev_mask = 0;

	if (ck)
		WXRcvWindow(wdisp->conn, *ck, &win);
	else
		WXGetWindow(wdisp->conn, wwin->client_win, net_wm_user_time_window, &win);

	if (win == wwin->net_user_time_win)
		return;

	detachUserTimeWindow(wwin);

	if (!isSafeUserTimeWindow(wwin, win, &prev_mask))
		return;

	if (!user_time_table)
		user_time_table = WMCreateHashTable(WMIntHashCallbacks);
	wwin->net_user_time_win = win;
	wwin->net_user_time_win_mask = prev_mask;
	WMHashInsert(user_time_table, (void *)(uintptr_t) win, wwin);
	xcb_change_window_attributes(wdisp->conn, win, XCB_CW_EVENT_MASK, (const uint32_t[]){ prev_mask | XCB_EVENT_MASK_PROPERTY_CHANGE });
}

WWindow *wNETWMUserTimeWindowFor(xcb_window_t window)
{
	if (!user_time_table)
		return NULL;
	return WMHashGet(user_time_table, (void *)(uintptr_t) window);
}

void wNETWMUserTimeNotify(WWindow *wwin)
{
	readUserTime(wwin);
}

void wNETWMDetachUserTimeWindow(WWindow *wwin)
{
	detachUserTimeWindow(wwin);
}

/* ---- _NET_WM_SYNC_REQUEST ------------------------------------------- */

bool wNETWMIsSyncRequestProtocol(xcb_atom_t protocol)
{
#ifdef USE_XSYNC
	return protocol == net_wm_sync_request;
#else
	(void)protocol;
	return false;
#endif
}

#ifdef USE_XSYNC

/* maps an XSyncAlarm XID back to its WWindow so the AlarmNotify handler
 * can clear the waiting flag without scanning the managed list */
static WMHashTable *sync_alarm_table = NULL;

/* give up waiting for a client redraw after this many milliseconds and
 * fall back to unsynchronised resize for the rest of the drag */
#define NET_WM_SYNC_TIMEOUT 500

/* the counter XID comes from a client property so every request on it
 * is checked, an error means "this client lied" so disable sync for
 * that window rather than letting it surface as an internal error,
 * one xcb_request_check() round-trip per call replaces the global
 * XSetErrorHandler trap bracket and the XSync that flanked it */
static bool xsyncRequestFailed(xcb_void_cookie_t ck)
{
	return !WXCheckVoid(wdisp->conn, ck);
}

void wNETWMSyncRequestSetup(WWindow *wwin)
{
	xcb_sync_create_alarm_value_list_t v;
	xcb_sync_int64_t zero = { 0, 0 };
	xcb_sync_alarm_t alarm;
	uint32_t counter;

	if (!wdisp->xext.xsync.supported || !wwin->protocols.NET_WM_SYNC_REQUEST)
		return;
	if (!WXGetCardinal(wdisp->conn, wwin->client_win, net_wm_sync_request_counter, &counter)
	    || counter == 0)
		return;

	wwin->net_sync.value_lo = 0;
	wwin->net_sync.value_hi = 0;
	wwin->net_sync.waiting = 0;
	wwin->net_sync.disabled = 0;

	v.counter   = counter;
	v.valueType = XCB_SYNC_VALUETYPE_ABSOLUTE;
	v.value.hi  = 0;
	v.value.lo  = 1;
	v.testType  = XCB_SYNC_TESTTYPE_POSITIVE_COMPARISON;
	v.delta.hi  = 0;
	v.delta.lo  = 1;
	v.events    = 1;

	alarm = xcb_generate_id(wdisp->conn);

	if (xsyncRequestFailed(xcb_sync_set_counter_checked(wdisp->conn, counter, zero))
	    || xsyncRequestFailed(xcb_sync_create_alarm_aux_checked(wdisp->conn, alarm,
			XCB_SYNC_CA_COUNTER | XCB_SYNC_CA_VALUE_TYPE
			| XCB_SYNC_CA_VALUE | XCB_SYNC_CA_TEST_TYPE
			| XCB_SYNC_CA_DELTA | XCB_SYNC_CA_EVENTS, &v))) {
		/* the alarm id was never accepted so nothing to destroy */
		wwin->net_sync.alarm = 0;
		wwin->net_sync.counter = 0;
		wwin->net_sync.disabled = 1;
		return;
	}

	wwin->net_sync.alarm = alarm;
	wwin->net_sync.counter = counter;

	if (!sync_alarm_table)
		sync_alarm_table = WMCreateHashTable(WMIntHashCallbacks);
	WMHashInsert(sync_alarm_table, (void *)(uintptr_t) wwin->net_sync.alarm, wwin);
}

void wNETWMSyncRequestDestroy(WWindow *wwin)
{
	if (wwin->net_sync.alarm) {
		if (sync_alarm_table)
			WMHashRemove(sync_alarm_table, (void *)(uintptr_t) wwin->net_sync.alarm);
		/* the alarm is wmaker-owned so the only failure is a race
		 * with the server discarding it, ignore */
		(void) WXCheckVoid(wdisp->conn,
				   xcb_sync_destroy_alarm_checked(wdisp->conn, wwin->net_sync.alarm));
		wwin->net_sync.alarm = 0;
	}
	wwin->net_sync.counter = 0;
	wwin->net_sync.waiting = 0;
}

/* send the protocol message asking the client to bump its counter to the
 * next value once it has redrawn for the upcoming ConfigureNotify, and
 * arm the alarm to fire when the counter reaches that value */
void wNETWMSyncRequestSend(WWindow *wwin)
{
	xcb_sync_change_alarm_value_list_t v;

	if (!wwin->net_sync.counter || !wwin->net_sync.alarm
	    || wwin->net_sync.disabled || wwin->net_sync.waiting)
		return;

	if (++wwin->net_sync.value_lo == 0)
		wwin->net_sync.value_hi++;

	WXSendClientMessage(wdisp->conn, wwin->client_win, XCB_EVENT_MASK_NO_EVENT,
			    wwin->client_win, wdisp->atom.wm.protocols,
			    (long)net_wm_sync_request,
			    (long)wdisp->timestamp.last_event,
			    (long)wwin->net_sync.value_lo,
			    (long)wwin->net_sync.value_hi, 0);

	v.value.hi = (int32_t)wwin->net_sync.value_hi;
	v.value.lo = wwin->net_sync.value_lo;

	if (xsyncRequestFailed(xcb_sync_change_alarm_aux_checked(wdisp->conn,
			wwin->net_sync.alarm, XCB_SYNC_CA_VALUE, &v))) {
		wwin->net_sync.disabled = 1;
		wwin->net_sync.waiting = 0;
		return;
	}

	gettimeofday(&wwin->net_sync.wait_start, NULL);
	wwin->net_sync.waiting = 1;
}

bool wNETWMSyncRequestHandleAlarm(xcb_generic_event_t *event)
{
	xcb_sync_alarm_notify_event_t *ae;
	WWindow *wwin;

	if (!wdisp->xext.xsync.supported
	    || (event->response_type & 0x7f) != wdisp->xext.xsync.event_base + XCB_SYNC_ALARM_NOTIFY)
		return false;

	ae = (xcb_sync_alarm_notify_event_t *)event;
	if (sync_alarm_table == NULL)
		return true;
	wwin = WMHashGet(sync_alarm_table, (void *)(uintptr_t) ae->alarm);
	if (wwin == NULL)
		return true;

	wwin->net_sync.waiting = 0;
	return true;
}

/* called from the opaque-resize loop before issuing the next configure,
 * drain any AlarmNotify already in the Xlib queue so a client that has
 * caught up clears its waiting flag, and if the request has been
 * outstanding for longer than NET_WM_SYNC_TIMEOUT fall back to
 * unsynchronised resize for the rest of the drag, never blocks so
 * ButtonRelease and Expose for other clients are serviced immediately */
bool wNETWMSyncRequestPending(WWindow *wwin)
{
	xcb_generic_event_t *ev = NULL;
	struct timeval now;
	long elapsed;

	if (!wwin->net_sync.waiting)
		return false;

	while (WXEvqTakeTyped(wdisp->conn,
			      wdisp->xext.xsync.event_base + XCB_SYNC_ALARM_NOTIFY,
			      XCB_NONE, &ev)) {
		wNETWMSyncRequestHandleAlarm(ev);
		free(ev);
	}

	if (!wwin->net_sync.waiting)
		return false;

	gettimeofday(&now, NULL);
	elapsed = (now.tv_sec - wwin->net_sync.wait_start.tv_sec) * 1000
		  + (now.tv_usec - wwin->net_sync.wait_start.tv_usec) / 1000;
	if (elapsed >= NET_WM_SYNC_TIMEOUT) {
		wwin->net_sync.waiting = 0;
		wwin->net_sync.disabled = 1;
		return false;
	}

	return true;
}

#else /* !USE_XSYNC */

void wNETWMSyncRequestSetup(WWindow *wwin)       { (void)wwin; }
void wNETWMSyncRequestDestroy(WWindow *wwin)     { (void)wwin; }
void wNETWMSyncRequestSend(WWindow *wwin)        { (void)wwin; }
bool wNETWMSyncRequestPending(WWindow *wwin)     { (void)wwin; return false; }
bool wNETWMSyncRequestHandleAlarm(xcb_generic_event_t *event) { (void)event; return false; }

#endif /* USE_XSYNC */

/*
 * Advance the screen's user-activity baseline from a WM-side interaction
 * (titlebar drag, resize, keyboard move) so a window that maps right after
 * with a timestamp older than the interaction does not steal focus.
 */
void wNETWMBumpUserTime(WScreen *scr, xcb_timestamp_t ts)
{
	if (ts != 0 && (scr->net_last_user_time == 0
			|| USER_TIME_NEWER(ts, scr->net_last_user_time)))
		scr->net_last_user_time = ts;
}

bool wNETWMShouldStealFocus(WWindow *wwin)
{
	WScreen *scr = wwin->screen_ptr;
	WWindow *focused;

	/* a client that explicitly stamps user_time = 0 is asking the WM
	 * not to focus it on initial map */
	if (wwin->net_user_time_set && wwin->net_user_time == 0)
		return false;

	focused = scr->focused_window;
	if (focused && focused != wwin) {
		/* a transient of the current focus holder, or of its
		 * application leader, is part of what the user is already
		 * working in, do not let a stale or missing timestamp keep
		 * a Save dialog from taking input */
		if (wwin->transient_for != XCB_NONE
		    && wwin->transient_for != scr->root_win
		    && (wwin->transient_for == focused->client_win
			|| wwin->transient_for == focused->main_window))
			return true;

		/* same for a _NET_WM_STATE_MODAL window of the focused
		 * application, which a toolkit may map as group-transient
		 * rather than transient for a specific window */
		if (wwin->flags.net_modal
		    && wwin->main_window != XCB_NONE
		    && wwin->main_window == focused->main_window)
			return true;

		/* a window mapping into a higher stacking layer than the
		 * current focus is going to obscure it regardless, denying
		 * focus would leave keyboard input going to a window the
		 * user can no longer see */
		if (wwin->frame && focused->frame
		    && WINDOW_LEVEL(wwin) > WINDOW_LEVEL(focused))
			return true;
	}

	/* if no other window has reported activity yet, or this window
	 * never set a user time, fall through to the existing policy */
	if (!wwin->net_user_time_set || scr->net_last_user_time == 0)
		return true;

	/* deny focus when the new window's last user activity is older
	 * than what we have already seen elsewhere */
	if (USER_TIME_NEWER(scr->net_last_user_time, wwin->net_user_time))
		return false;

	return true;
}

char *wNETWMGetWindowName(xcb_window_t window)
{
	return WXGetUTF8String(wdisp->conn, window, net_wm_name, NULL);
}

char *wNETWMGetIconName(xcb_window_t window)
{
	return WXGetUTF8String(wdisp->conn, window, net_wm_icon_name, NULL);
}

/*
 * Publish the title actually shown by the window manager when it differs
 * from what the client set, or remove the property when they match again.
 */
static void updateVisibleNameProp(xcb_window_t window, xcb_atom_t prop,
				  const char *visible, const char *client_name)
{
	if (window == XCB_NONE)
		return;

	if (visible && (!client_name || strcmp(visible, client_name) != 0))
		WXSetUTF8String(wdisp->conn, window, prop, visible, strlen(visible));
	else
		WXDeleteProperty(wdisp->conn, window, prop);
}

void wNETWMUpdateVisibleName(WWindow *wwin, const char *visible, const char *client_name)
{
	updateVisibleNameProp(wwin->client_win, net_wm_visible_name, visible, client_name);
}

void wNETWMUpdateVisibleIconName(WWindow *wwin, const char *visible, const char *client_name)
{
	updateVisibleNameProp(wwin->client_win, net_wm_visible_icon_name, visible, client_name);
}

static void observer(void *self, WMNotification *notif)
{
	WWindow *wwin = (WWindow *) WMGetNotificationObject(notif);
	const char *name = WMGetNotificationName(notif);
	void *data = WMGetNotificationClientData(notif);
	NetData *ndata = (NetData *) self;

	if (strcmp(name, WMNManaged) == 0 && wwin) {
		updateClientList(wwin->screen_ptr);
		updateClientListStacking(wwin->screen_ptr, NULL);
		updateStateHint(wwin, true, false);

		/* wNETWMRcvClientHints() already decoded _NET_WM_STRUT(_PARTIAL)
		 * from the prefetch batch via addStrutCk() and called
		 * wScreenUpdateUsableArea(), and the PropertyNotify handler
		 * covers later changes, only the GNUstep-attr branch in
		 * wWindowSetupInitialAttributes() skipped that call so read
		 * for those windows here. */
		if (wwin->wm_gnustep_attr) {
			updateStrut(wwin->screen_ptr, wwin->client_win, false);
			updateStrut(wwin->screen_ptr, wwin->client_win, true);
			wScreenUpdateUsableArea(wwin->screen_ptr);
		}
	} else if (strcmp(name, WMNUnmanaged) == 0 && wwin) {
		updateClientList(wwin->screen_ptr);
		updateClientListStacking(wwin->screen_ptr, wwin);
		updateWorkspaceHint(wwin, false, true);
		updateStateHint(wwin, false, true);
		wNETWMUpdateActions(wwin, true);
		wNETWMUpdateVisibleName(wwin, NULL, NULL);
		wNETWMUpdateVisibleIconName(wwin, NULL, NULL);

		updateStrut(wwin->screen_ptr, wwin->client_win, false);
		wScreenUpdateUsableArea(wwin->screen_ptr);
	} else if (strcmp(name, WMNChangedStacking) == 0 && wwin) {
		updateClientListStacking(wwin->screen_ptr, NULL);
		updateStateHint(wwin, false, false);
	} else if (strcmp(name, WMNChangedFocus) == 0 && wwin) {
		updateFocusHint(ndata->scr);
		updateStateHint(wwin, false, false);
	} else if (strcmp(name, WMNChangedWorkspace) == 0 && wwin) {
		updateWorkspaceHint(wwin, false, false);
		updateStateHint(wwin, true, false);
	} else if (strcmp(name, WMNChangedState) == 0 && wwin) {
		updateStateHint(wwin, !strcmp(data, "omnipresent"), false);
	}
}

static void wsobserver(void *self, WMNotification *notif)
{
	WScreen *scr = (WScreen *) WMGetNotificationObject(notif);
	const char *name = WMGetNotificationName(notif);

	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;

	if (strcmp(name, WMNWorkspaceCreated) == 0) {
		updateWorkspaceCount(scr);
		updateWorkspaceNames(scr);
		updateCurrentWorkspace(scr);
		wNETWMUpdateDesktop(scr);
		wNETWMUpdateWorkarea(scr);
		wNETWMUpdateDesktopLayout(scr);
	} else if (strcmp(name, WMNWorkspaceDestroyed) == 0) {
		updateWorkspaceCount(scr);
		updateWorkspaceNames(scr);
		wNETWMUpdateDesktop(scr);
		wNETWMUpdateWorkarea(scr);
		wNETWMUpdateDesktopLayout(scr);
	} else if (strcmp(name, WMNWorkspaceChanged) == 0) {
		updateCurrentWorkspace(scr);
	} else if (strcmp(name, WMNWorkspaceNameChanged) == 0) {
		updateWorkspaceNames(scr);
	} else if (strcmp(name, WMNResetStacking) == 0) {
		/* CommitStacking() posts this with the WScreen as object,
		 * the old arm in observer() mis-cast it to WWindow * and
		 * was never subscribed, so _NET_CLIENT_LIST_STACKING went
		 * stale after every ConfigureRequest restack */
		updateClientListStacking(scr, NULL);
	}
}

void wNETFrameExtents(WWindow *wwin)
{
	uint32_t extents[4] = { 0, 0, 0, 0 };

	/* The extents array describes dimensions which are not
	 * part of the client window. In our case that means
	 * widths of the border and heights of the titlebar and resizebar.
	 *
	 * Index 0 = left
	 *       1 = right
	 *       2 = top
	 *       3 = bottom
	 */
	if (wwin->frame->titlebar)
		extents[2] = wwin->frame->titlebar->height;
	if (wwin->frame->resizebar)
		extents[3] = wwin->frame->resizebar->height;
	if (HAS_BORDER(wwin)) {
		int il, ir, it, ib;
		int outer = wFrameBorderWidth(wwin);

		wFrameInnerBorderWidths(wwin, &il, &ir, &it, &ib);
		extents[0] += outer + il;
		extents[1] += outer + ir;
		extents[2] += outer + it;
		extents[3] += outer + ib;
	}

	WXSetCardinalN(wdisp->conn, wwin->client_win, net_frame_extents, extents, 4);
}

void wNETCleanupFrameExtents(WWindow *wwin)
{
	WXDeleteProperty(wdisp->conn, wwin->client_win, net_frame_extents);
}
