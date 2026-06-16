/*  wmspec.c-- support for the wm-spec Hints
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1998-2003 Alfredo K. Kojima
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

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xresource.h>
#ifdef USE_XSYNC
#include <X11/extensions/sync.h>
#endif
#include <signal.h>
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
#include "framewin.h"
#include "actions.h"
#include "client.h"
#include "appicon.h"
#include "wmspec.h"
#include "icon.h"
#include "stacking.h"
#include "tabgroup.h"
#include "xinerama.h"
#include "properties.h"
#include "dialog.h"
#include "startup_notify.h"


/* Root Window Properties */
static Atom net_supported;
static Atom net_client_list;
static Atom net_client_list_stacking;
static Atom net_number_of_desktops;
static Atom net_desktop_geometry;
static Atom net_desktop_viewport;
static Atom net_current_desktop;
static Atom net_desktop_names;
static Atom net_active_window;
static Atom net_workarea;
static Atom net_supporting_wm_check;
static Atom net_virtual_roots;	/* N/A */
static Atom net_desktop_layout;
static Atom net_showing_desktop;

/* Other Root Window Messages */
static Atom net_close_window;
static Atom net_moveresize_window;
static Atom net_wm_moveresize;
static Atom net_restack_window;
static Atom net_request_frame_extents;

/* Application Window Properties */
static Atom net_wm_name;
static Atom net_wm_visible_name;
static Atom net_wm_icon_name;
static Atom net_wm_visible_icon_name;
static Atom net_wm_desktop;
static Atom net_wm_fullscreen_monitors;
static Atom net_wm_window_type;
static Atom net_wm_window_type_desktop;
static Atom net_wm_window_type_dock;
static Atom net_wm_window_type_toolbar;
static Atom net_wm_window_type_menu;
static Atom net_wm_window_type_utility;
static Atom net_wm_window_type_splash;
static Atom net_wm_window_type_dialog;
static Atom net_wm_window_type_dropdown_menu;
static Atom net_wm_window_type_popup_menu;
static Atom net_wm_window_type_tooltip;
static Atom net_wm_window_type_notification;
static Atom net_wm_window_type_combo;
static Atom net_wm_window_type_dnd;
static Atom net_wm_window_type_normal;
static Atom net_wm_state;
static Atom net_wm_state_modal;
static Atom net_wm_state_sticky;
static Atom net_wm_state_maximized_vert;
static Atom net_wm_state_maximized_horz;
static Atom net_wm_state_shaded;
static Atom net_wm_state_skip_taskbar;
static Atom net_wm_state_skip_pager;
static Atom net_wm_state_hidden;
static Atom net_wm_state_fullscreen;
static Atom net_wm_state_above;
static Atom net_wm_state_below;
static Atom net_wm_state_focused;
static Atom net_wm_state_demands_attention;
static Atom net_wm_allowed_actions;
static Atom net_wm_action_move;
static Atom net_wm_action_resize;
static Atom net_wm_action_minimize;
static Atom net_wm_action_shade;
static Atom net_wm_action_stick;
static Atom net_wm_action_maximize_horz;
static Atom net_wm_action_maximize_vert;
static Atom net_wm_action_fullscreen;
static Atom net_wm_action_change_desktop;
static Atom net_wm_action_close;
static Atom net_wm_action_above;
static Atom net_wm_action_below;
static Atom net_wm_strut;
static Atom net_wm_strut_partial;
static Atom net_wm_icon_geometry;
static Atom net_wm_icon;
static Atom net_wm_pid;
static Atom net_wm_handled_icons;
static Atom net_wm_user_time;
static Atom net_wm_user_time_window;
static Atom net_wm_window_opacity;

static Atom net_frame_extents;
static Atom net_wm_full_placement;

/* Window Manager Protocols */
static Atom net_wm_ping;
#ifdef USE_XSYNC
static Atom net_wm_sync_request;
static Atom net_wm_sync_request_counter;
#endif

static Atom utf8_string;
static Atom net_startup_id;

typedef struct {
	char *name;
	Atom *atom;
} atomitem_t;

static atomitem_t atomNames[] = {
	{"_NET_SUPPORTED", &net_supported},
	{"_NET_CLIENT_LIST", &net_client_list},
	{"_NET_CLIENT_LIST_STACKING", &net_client_list_stacking},
	{"_NET_NUMBER_OF_DESKTOPS", &net_number_of_desktops},
	{"_NET_DESKTOP_GEOMETRY", &net_desktop_geometry},
	{"_NET_DESKTOP_VIEWPORT", &net_desktop_viewport},
	{"_NET_CURRENT_DESKTOP", &net_current_desktop},
	{"_NET_DESKTOP_NAMES", &net_desktop_names},
	{"_NET_ACTIVE_WINDOW", &net_active_window},
	{"_NET_WORKAREA", &net_workarea},
	{"_NET_SUPPORTING_WM_CHECK", &net_supporting_wm_check},
	{"_NET_VIRTUAL_ROOTS", &net_virtual_roots},
	{"_NET_DESKTOP_LAYOUT", &net_desktop_layout},
	{"_NET_SHOWING_DESKTOP", &net_showing_desktop},

	{"_NET_CLOSE_WINDOW", &net_close_window},
	{"_NET_MOVERESIZE_WINDOW", &net_moveresize_window},
	{"_NET_WM_MOVERESIZE", &net_wm_moveresize},
	{"_NET_RESTACK_WINDOW", &net_restack_window},
	{"_NET_REQUEST_FRAME_EXTENTS", &net_request_frame_extents},

	{"_NET_WM_NAME", &net_wm_name},
	{"_NET_WM_VISIBLE_NAME", &net_wm_visible_name},
	{"_NET_WM_ICON_NAME", &net_wm_icon_name},
	{"_NET_WM_VISIBLE_ICON_NAME", &net_wm_visible_icon_name},
	{"_NET_WM_DESKTOP", &net_wm_desktop},
	{"_NET_WM_FULLSCREEN_MONITORS", &net_wm_fullscreen_monitors},
	{"_NET_WM_WINDOW_TYPE", &net_wm_window_type},
	{"_NET_WM_WINDOW_TYPE_DESKTOP", &net_wm_window_type_desktop},
	{"_NET_WM_WINDOW_TYPE_DOCK", &net_wm_window_type_dock},
	{"_NET_WM_WINDOW_TYPE_TOOLBAR", &net_wm_window_type_toolbar},
	{"_NET_WM_WINDOW_TYPE_MENU", &net_wm_window_type_menu},
	{"_NET_WM_WINDOW_TYPE_UTILITY", &net_wm_window_type_utility},
	{"_NET_WM_WINDOW_TYPE_SPLASH", &net_wm_window_type_splash},
	{"_NET_WM_WINDOW_TYPE_DIALOG", &net_wm_window_type_dialog},
	{"_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", &net_wm_window_type_dropdown_menu},
	{"_NET_WM_WINDOW_TYPE_POPUP_MENU", &net_wm_window_type_popup_menu},
	{"_NET_WM_WINDOW_TYPE_TOOLTIP", &net_wm_window_type_tooltip},
	{"_NET_WM_WINDOW_TYPE_NOTIFICATION", &net_wm_window_type_notification},
	{"_NET_WM_WINDOW_TYPE_COMBO", &net_wm_window_type_combo},
	{"_NET_WM_WINDOW_TYPE_DND", &net_wm_window_type_dnd},
	{"_NET_WM_WINDOW_TYPE_NORMAL", &net_wm_window_type_normal},
	{"_NET_WM_STATE", &net_wm_state},
	{"_NET_WM_STATE_MODAL", &net_wm_state_modal},
	{"_NET_WM_STATE_STICKY", &net_wm_state_sticky},
	{"_NET_WM_STATE_MAXIMIZED_VERT", &net_wm_state_maximized_vert},
	{"_NET_WM_STATE_MAXIMIZED_HORZ", &net_wm_state_maximized_horz},
	{"_NET_WM_STATE_SHADED", &net_wm_state_shaded},
	{"_NET_WM_STATE_SKIP_TASKBAR", &net_wm_state_skip_taskbar},
	{"_NET_WM_STATE_SKIP_PAGER", &net_wm_state_skip_pager},
	{"_NET_WM_STATE_HIDDEN", &net_wm_state_hidden},
	{"_NET_WM_STATE_FULLSCREEN", &net_wm_state_fullscreen},
	{"_NET_WM_STATE_ABOVE", &net_wm_state_above},
	{"_NET_WM_STATE_BELOW", &net_wm_state_below},
	{"_NET_WM_STATE_FOCUSED", &net_wm_state_focused},
	{"_NET_WM_STATE_DEMANDS_ATTENTION", &net_wm_state_demands_attention},
	{"_NET_WM_ALLOWED_ACTIONS", &net_wm_allowed_actions},
	{"_NET_WM_ACTION_MOVE", &net_wm_action_move},
	{"_NET_WM_ACTION_RESIZE", &net_wm_action_resize},
	{"_NET_WM_ACTION_MINIMIZE", &net_wm_action_minimize},
	{"_NET_WM_ACTION_SHADE", &net_wm_action_shade},
	{"_NET_WM_ACTION_STICK", &net_wm_action_stick},
	{"_NET_WM_ACTION_MAXIMIZE_HORZ", &net_wm_action_maximize_horz},
	{"_NET_WM_ACTION_MAXIMIZE_VERT", &net_wm_action_maximize_vert},
	{"_NET_WM_ACTION_FULLSCREEN", &net_wm_action_fullscreen},
	{"_NET_WM_ACTION_CHANGE_DESKTOP", &net_wm_action_change_desktop},
	{"_NET_WM_ACTION_CLOSE", &net_wm_action_close},
	{"_NET_WM_ACTION_ABOVE", &net_wm_action_above},
	{"_NET_WM_ACTION_BELOW", &net_wm_action_below},
	{"_NET_WM_STRUT", &net_wm_strut},
	{"_NET_WM_STRUT_PARTIAL", &net_wm_strut_partial},
	{"_NET_WM_ICON_GEOMETRY", &net_wm_icon_geometry},
	{"_NET_WM_ICON", &net_wm_icon},
	{"_NET_WM_PID", &net_wm_pid},
	{"_NET_WM_HANDLED_ICONS", &net_wm_handled_icons},
	{"_NET_WM_WINDOW_OPACITY", &net_wm_window_opacity},

	{"_NET_FRAME_EXTENTS", &net_frame_extents},

	{"_NET_WM_USER_TIME", &net_wm_user_time},
	{"_NET_WM_USER_TIME_WINDOW", &net_wm_user_time_window},
	{"_NET_WM_FULL_PLACEMENT", &net_wm_full_placement},
	{"_NET_WM_PING", &net_wm_ping},
#ifdef USE_XSYNC
	{"_NET_WM_SYNC_REQUEST", &net_wm_sync_request},
	{"_NET_WM_SYNC_REQUEST_COUNTER", &net_wm_sync_request_counter},
#endif

	{"UTF8_STRING", &utf8_string},

	{"_NET_STARTUP_ID", &net_startup_id},
};

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
static void cacheLocalPid(WWindow *wwin);
static void wNETWMShowingDesktop(WScreen *scr, Bool show);

typedef struct NetData {
	WScreen *scr;
	WReservedArea *strut;
	WWindow **show_desktop;
} NetData;

static void setSupportedHints(WScreen *scr)
{
	static const char wm_class[] = "wmaker\0WindowMaker";
	Atom atom[wlengthof(atomNames)];
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
	if (w_global.xext.xsync.supported) {
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

	WXSetAtomN(dpy, scr->root_win, net_supported, atom, i);

	/* set supporting wm hint */
	WXSetWindow(dpy, scr->root_win, net_supporting_wm_check, scr->info_window);
	WXSetWindow(dpy, scr->info_window, net_supporting_wm_check, scr->info_window);

	/* set _NET_WM_NAME on supporting window */
	snprintf(wm_name, sizeof(wm_name), "WindowMaker %s", VERSION);
	WXSetUTF8String(dpy, scr->info_window, net_wm_name, wm_name, strlen(wm_name));

	/* set _NET_WM_PID on supporting window */
	WXSetCardinal(dpy, scr->info_window, net_wm_pid, (uint32_t)getpid());

	/* set WM_CLASS on supporting window: instance\0class\0 */
	WXSetLatin1String(dpy, scr->info_window, XA_WM_CLASS, wm_class, sizeof(wm_class));
}

void wNETWMUpdateDesktop(WScreen *scr)
{
	uint32_t *views, sizes[2];
	int count, i;

	if (scr->workspace_count == 0)
		return;

	count = scr->workspace_count * 2;
	views = wmalloc(sizeof(uint32_t) * count);
	sizes[0] = scr->scr_width;
	sizes[1] = scr->scr_height;

	for (i = 0; i < scr->workspace_count; i++) {
		views[2 * i + 0] = 0;
		views[2 * i + 1] = 0;
	}

	WXSetCardinalN(dpy, scr->root_win, net_desktop_geometry, sizes, 2);
	WXSetCardinalN(dpy, scr->root_win, net_desktop_viewport, views, count);

	wfree(views);
}

int wNETWMGetCurrentDesktopFromHint(WScreen *scr)
{
	int32_t desktop;

	if (WXGetCardinalSigned(dpy, scr->root_win, net_current_desktop, &desktop))
		return desktop;
	return -1;
}

static RImage *makeRImageFromARGBData(const uint32_t *data)
{
	int size, width, height, i;
	RImage *image;
	unsigned char *imgdata;
	uint32_t pixel;

	width = data[0];
	height = data[1];
	size = width * height;

	if (size == 0)
		return NULL;

	image = RCreateImage(width, height, True);
	if (!image)
		return NULL;

	for (imgdata = image->data, i = 2; i < size + 2; i++, imgdata += 4) {
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

	if (w_global.config->enforce_icon_margin) {

		/* better use only 75% of icon_size. For 64x64 this means 48x48
		 * This leaves room around the icon for the miniwindow title and
		 * results in better overall aesthetics -Dan */
		wanted = (int)((double)w_global.config->icon_size * 0.75 + 0.5);

		/* the size should be a multiple of 4 */
		wanted = (wanted >> 2) << 2;

	} else {

		/* This is the "old" approach, which tries to find the largest
		 * icon that still fits into icon_size. */
		wanted = w_global.config->icon_size;

	}

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

RImage *get_window_image_from_x11(Window window)
{
	RImage *image;
	size_t items;
	uint32_t *property;

	/* Get the icon from X11 Window. Cap the read at 4 Mi CARDINALs
	 * (16 MiB), Electron and Chromium based applications ship a
	 * single 1024x1024 icon (1,048,578 cardinals) and nothing
	 * smaller, so the bound must cover that, while still preventing
	 * a hostile client from forcing an arbitrarily large transient
	 * allocation by stuffing the property. */
	property = WXGetCardinalList(dpy, window, net_wm_icon, 1u << 22, &items);
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

static void updateIconImage(WWindow *wwin)
{
	/* Remove the icon image from X11 */
	if (wwin->net_icon_image)
		RReleaseImage(wwin->net_icon_image);

	/* Save the icon in the X11 icon */
	wwin->net_icon_image = get_window_image_from_x11(wwin->client_win);

	if (wwin->frame)
		wWindowRefreshTitlebarIcon(wwin);

	/* Refresh the Window Icon */
	if (wwin->icon)
		wIconUpdate(wwin->icon);

	/* Refresh the application icon. The app_icon's owner is
	 * main_window_desc, whose net_icon_image was copied once at
	 * wApplicationCreate time, so propagate the new image there too
	 * or wIconUpdate below will not see it (clients that set
	 * _NET_WM_ICON only after mapping otherwise keep the default). */
	WApplication *app = wApplicationOf(wwin->main_window);
	if (app) {
		if (app->main_window_desc && app->main_window_desc != wwin) {
			if (app->main_window_desc->net_icon_image)
				RReleaseImage(app->main_window_desc->net_icon_image);
			app->main_window_desc->net_icon_image =
				RRetainImage(wwin->net_icon_image);
		}
		if (app->app_icon) {
			wIconUpdate(app->app_icon->icon);
			wAppIconPaint(app->app_icon);
		}
	}
}

static void updateWindowOpacityCk(WWindow *wwin, WXCookie ck)
{
	uint32_t opacity;

	if (!wwin->frame) {
		WXDiscard(dpy, ck);
		return;
	}

	/* We don't care about this ourselves, but other programs need us to copy
	 * this to the frame window. */
	if (WXRcvCardinal(dpy, ck, &opacity))
		WXSetCardinal(dpy, wwin->frame->core->window, net_wm_window_opacity, opacity);
	else
		WXDeleteProperty(dpy, wwin->frame->core->window, net_wm_window_opacity);
}

static void updateWindowOpacity(WWindow *wwin)
{
	updateWindowOpacityCk(wwin,
		WXReqFormat32(dpy, wwin->client_win, net_wm_window_opacity, XA_CARDINAL, 1));
}

/* read and validate _NET_DESKTOP_LAYOUT off the root window. The spec
 * carries orientation, columns, rows and starting corner, with one of
 * rows/cols allowed to be 0 meaning "derive from the desktop count". With
 * no property set we describe a single row so directional Left/Right keys
 * fall back to Prev/Next. */
void wNETWMUpdateDesktopLayout(WScreen *scr)
{
	uint32_t v[4];
	int n, cols, rows;

	scr->net_desktop_layout.orientation = 0;
	scr->net_desktop_layout.corner = 0;
	scr->net_desktop_layout.cols = WMAX(1, scr->workspace_count);
	scr->net_desktop_layout.rows = 1;

	if (!WXGetCardinalN(dpy, scr->root_win, net_desktop_layout, v, 4))
		return;

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

void wNETWMCheckRootHintChange(WScreen *scr, XPropertyEvent *event)
{
	if (event->atom == net_desktop_layout)
		wNETWMUpdateDesktopLayout(scr);
}

static void updateShowDesktop(WScreen *scr, Bool show)
{
	WXSetCardinal(dpy, scr->root_win, net_showing_desktop, show == True);
}

static void wNETWMShowingDesktop(WScreen *scr, Bool show)
{
	if (show && scr->netdata->show_desktop == NULL) {
		WWindow *tmp, **wins;
		int i = 0;

		wins = (WWindow **) wmalloc(sizeof(WWindow *) * (scr->window_count + 1));

		tmp = scr->focused_window;
		while (tmp) {
			if (!tmp->flags.hidden && !tmp->flags.miniaturized && !WFLAGP(tmp, skip_window_list)) {

				wins[i++] = tmp;
				tmp->flags.skip_next_animation = 1;
				tmp->flags.net_show_desktop = 1;
				wIconifyWindow(tmp);
			}

			tmp = tmp->prev;
		}
		wins[i++] = NULL;

		scr->netdata->show_desktop = wins;
		updateShowDesktop(scr, True);
	} else if (scr->netdata->show_desktop != NULL) {
		WWindow **tmp;
		for (tmp = scr->netdata->show_desktop; *tmp; ++tmp) {
			wDeiconifyWindow(*tmp);
			(*tmp)->flags.net_show_desktop = 0;
		}
		wfree(scr->netdata->show_desktop);
		scr->netdata->show_desktop = NULL;
		updateShowDesktop(scr, False);
	}
}

void wNETWMInitStuff(WScreen *scr)
{
	NetData *data;
	int i;
#ifdef HAVE_XINTERNATOMS
	Atom atoms[wlengthof(atomNames)];
	char *names[wlengthof(atomNames)];
#endif

#ifdef DEBUG_WMSPEC
	wmessage("wNETWMInitStuff");
#endif

#ifdef HAVE_XINTERNATOMS
	for (i = 0; i < wlengthof(atomNames); ++i)
		names[i] = atomNames[i].name;

	XInternAtoms(dpy, &names[0], wlengthof(atomNames), False, atoms);
	for (i = 0; i < wlengthof(atomNames); ++i)
		*atomNames[i].atom = atoms[i];
#else
	for (i = 0; i < wlengthof(atomNames); i++)
		*atomNames[i].atom = XInternAtom(dpy, atomNames[i].name, False);
#endif

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
	updateShowDesktop(scr, False);
	wNETWMUpdateDesktopLayout(scr);

	wScreenUpdateUsableArea(scr);
}

void wNETWMCleanup(WScreen *scr)
{
	int i;

	for (i = 0; i < wlengthof(atomNames); i++)
		WXDeleteProperty(dpy, scr->root_win, *atomNames[i].atom);
}

void wNETWMUpdateActions(WWindow *wwin, Bool del)
{
	Atom action[12];	/* nr of actions atoms defined */
	int i = 0;

	if (del) {
		WXDeleteProperty(dpy, wwin->client_win, net_wm_allowed_actions);
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

	WXSetAtomN(dpy, wwin->client_win, net_wm_allowed_actions, action, i);
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

	WXSetCardinalN(dpy, scr->root_win, net_workarea, property_value, nb_workspace * 4);
	wfree(property_value);
}

static inline int clampi(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

Bool wNETWMGetUsableArea(WScreen *scr, int head, WArea *area)
{
	WReservedArea *cur;
	WMRect rect;

	if (!scr->netdata || !scr->netdata->strut)
		return False;

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
		return False;

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

	return True;
}

static void updateClientList(WScreen *scr)
{
	WWindow *wwin;
	Window *windows;
	int count;

	windows = (Window *) wmalloc(sizeof(Window) * (scr->window_count + 1));

	count = 0;
	wwin = scr->focused_window;
	while (wwin) {
		windows[count++] = wwin->client_win;
		wwin = wwin->prev;
	}
	WXSetWindowN(dpy, scr->root_win, net_client_list, windows, count);

	wfree(windows);
	XFlush(dpy);
}

static void updateClientListStacking(WScreen *scr, WWindow *wwin_excl)
{
	WWindow *wwin;
	Window *client_list, *client_list_reverse;
	int client_count, i;
	WCoreWindow *tmp;
	WMBagIterator iter;

	/* update client list */
	i = scr->window_count + 1;
	client_list = (Window *) wmalloc(sizeof(Window) * i);
	client_list_reverse = (Window *) wmalloc(sizeof(Window) * i);

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
		Window w = client_list[client_count - i - 1];
		client_list_reverse[i] = w;
	}

	WXSetWindowN(dpy, scr->root_win, net_client_list_stacking, client_list_reverse, client_count);

	wfree(client_list);
	wfree(client_list_reverse);

	XFlush(dpy);
}

static void updateWorkspaceCount(WScreen *scr)
{				/* changeable */
	WXSetCardinal(dpy, scr->root_win, net_number_of_desktops, scr->workspace_count);
}

static void updateCurrentWorkspace(WScreen *scr)
{				/* changeable */
	WXSetCardinal(dpy, scr->root_win, net_current_desktop, scr->current_workspace);
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

	WXSetUTF8String(dpy, scr->root_win, net_desktop_names, buf, len);
}

static void updateFocusHint(WScreen *scr)
{				/* changeable */
	Window window;

	if (!scr->focused_window || !scr->focused_window->flags.focused)
		window = None;
	else
		window = scr->focused_window->client_win;

	WXSetWindow(dpy, scr->root_win, net_active_window, window);
}

static void updateWorkspaceHint(WWindow *wwin, Bool fake, Bool del)
{
	if (del) {
		WXDeleteProperty(dpy, wwin->client_win, net_wm_desktop);
	} else {
		uint32_t l = ((fake || IS_OMNIPRESENT(wwin)) ? 0xffffffffu : (uint32_t)wwin->frame->workspace);
		WXSetCardinal(dpy, wwin->client_win, net_wm_desktop, l);
	}
}

static void updateStateHint(WWindow *wwin, Bool changedWorkspace, Bool del)
{				/* changeable */
	if (del) {
		WXDeleteProperty(dpy, wwin->client_win, net_wm_state);
		WXDeleteProperty(dpy, wwin->client_win, net_wm_fullscreen_monitors);
	} else {
		Atom state[15];	/* nr of defined state atoms */
		int i = 0;

		if (changedWorkspace || (w_global.config->sticky_icons && !IS_OMNIPRESENT(wwin)))
			updateWorkspaceHint(wwin, False, False);

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
					updateWorkspaceHint(wwin, True, False);
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

		WXSetAtomN(dpy, wwin->client_win, net_wm_state, state, i);

		if (wwin->flags.fullscreen && (wwin->flags.fullscreen_monitors[0] != -1)) {
			uint32_t data[4];

			data[0] = wwin->flags.fullscreen_monitors[0];
			data[1] = wwin->flags.fullscreen_monitors[1];
			data[2] = wwin->flags.fullscreen_monitors[2];
			data[3] = wwin->flags.fullscreen_monitors[3];

			WXSetCardinalN(dpy, wwin->client_win, net_wm_fullscreen_monitors, data, 4);
		}
	}
}

/*
 * Build a WReservedArea from raw _NET_WM_STRUT(_PARTIAL) data. The values
 * come from an untrusted client property, so clamp every field to the root
 * extent and drop edges whose start/end span is inverted, otherwise a
 * hostile or buggy panel can produce a nonsensical workarea.
 */
static WReservedArea *makeReservedArea(WScreen *scr, Window w,
				       const uint32_t *v, Bool partial)
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

static Bool updateStrut(WScreen *scr, Window w, Bool adding)
{
	WReservedArea *area;
	Bool hasState = False;

	if (adding) {
		uint32_t strut[12];
		Bool partial;

		/* _NET_WM_STRUT_PARTIAL takes precedence over the legacy
		 * _NET_WM_STRUT, and carries per-edge start/end spans so a
		 * panel reserving only part of an edge does not steal that
		 * edge on every head. */
		partial = WXGetCardinalN(dpy, w, net_wm_strut_partial, strut, 12);
		if (partial || WXGetCardinalN(dpy, w, net_wm_strut, strut, 4)) {

			area = makeReservedArea(scr, w, strut, partial);

			area->next = scr->netdata->strut;
			scr->netdata->strut = area;

			hasState = True;
		}
	} else {
		/* deleting */
		area = scr->netdata->strut;

		if (area) {
			if (area->window == w) {
				scr->netdata->strut = area->next;
				wfree(area);
				hasState = True;
			} else {
				while (area->next && area->next->window != w)
					area = area->next;

				if (area->next) {
					WReservedArea *next;

					next = area->next->next;
					wfree(area->next);
					area->next = next;

					hasState = True;
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
		/* //layer = WMPopUpLevel; // this seems a bad idea -Dan */
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

static void doStateAtom(WWindow *wwin, Atom state, int set, Bool init)
{
	if (state == net_wm_state_sticky) {
		if (set == _NET_WM_STATE_TOGGLE)
			set = !IS_OMNIPRESENT(wwin);

		if (set != wwin->flags.omnipresent)
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

		if (init) {
			wwin->client_flags.floating = set;
		} else {
			wwin->client_flags.floating = set;
			ChangeStackingLevel(wwin->frame->core, getWindowLayer(wwin));
		}

	} else if (state == net_wm_state_below) {
		if (set == _NET_WM_STATE_TOGGLE)
			set = !(wwin->client_flags.sunken);

		if (init) {
			wwin->client_flags.sunken = set;
		} else {
			wwin->client_flags.sunken = set;
			ChangeStackingLevel(wwin->frame->core, getWindowLayer(wwin));
		}

	} else if (state == net_wm_state_demands_attention) {
		if (set == _NET_WM_STATE_TOGGLE)
			set = !wwin->flags.net_demands_attention;

		/* an already-focused window does not get to demand attention,
		 * the spec asks the WM to clear this state once the window
		 * has the user's attention */
		if (set && wwin->flags.focused)
			set = 0;

		wwin->flags.net_demands_attention = set;
		if (!init)
			wAppBounceWhileUrgent(wApplicationOf(wwin->main_window));

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
		wmessage("doStateAtom unknown atom %s set %d", XGetAtomName(dpy, state), set);
#endif
	}
}

static void removeIcon(WWindow *wwin)
{
	if (wwin->icon == NULL)
		return;
	if (wwin->flags.miniaturized && wwin->icon->mapped) {
		XUnmapWindow(dpy, wwin->icon->core->window);
		RemoveFromStackList(wwin->icon->core);
		wIconDestroy(wwin->icon);
		wwin->icon = NULL;
	}
}

static Bool handleWindowType(WWindow *wwin, Atom type, int *layer)
{
	Bool ret = True;

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
		ret = False;
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
	Atom *type;

	type = WXGetAtomList(dpy, wwin->client_win, net_wm_window_type, 32, &nitems);
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
		wNETWMUpdateActions(wwin, False);
	}
}

static void addStrutCk(WScreen *scr, Window w, WXCookie strut, WXCookie strut_partial)
{
	WReservedArea *area;
	uint32_t v[12];
	Bool partial;

	/* consume both cookies on every path so callers do not have to,
	 * _NET_WM_STRUT_PARTIAL takes precedence over the legacy strut */
	if (WXRcvCardinalN(dpy, strut_partial, v, 12)) {
		WXDiscard(dpy, strut);
		partial = True;
	} else if (WXRcvCardinalN(dpy, strut, v, 4)) {
		partial = False;
	} else {
		return;
	}

	area = makeReservedArea(scr, w, v, partial);
	area->next = scr->netdata->strut;
	scr->netdata->strut = area;
}

void wNETWMReqClientHints(Window window, WNETWMHintCookies *ck)
{
	ck->desktop       = WXReqFormat32(dpy, window, net_wm_desktop, XA_CARDINAL, 1);
	ck->state         = WXReqFormat32(dpy, window, net_wm_state, XA_ATOM, 1024);
	ck->type          = WXReqFormat32(dpy, window, net_wm_window_type, XA_ATOM, 1024);
	ck->strut         = WXReqFormat32(dpy, window, net_wm_strut, XA_CARDINAL, 4);
	ck->strut_partial = WXReqFormat32(dpy, window, net_wm_strut_partial, XA_CARDINAL, 12);
}

void wNETWMReqInitialStateCookies(Window window, WNETWMInitialCookies *ck)
{
	ck->handled_icons = WXReqFormat32(dpy, window, net_wm_handled_icons, XA_CARDINAL, 1);
	ck->icon_geom     = WXReqFormat32(dpy, window, net_wm_icon_geometry, XA_CARDINAL, 4);
	ck->opacity       = WXReqFormat32(dpy, window, net_wm_window_opacity, XA_CARDINAL, 1);
	ck->user_time_win = WXReqFormat32(dpy, window, net_wm_user_time_window, XA_WINDOW, 1);
	ck->startup_id    = WXReqString8(dpy, window, net_startup_id, utf8_string);
}

void wNETWMRcvClientHints(WWindow *wwin, WNETWMHintCookies *ck, int *layer, int *workspace)
{
	int32_t desktop;
	size_t i, nitems;
	Atom *atoms;

	if (WXRcvCardinalSigned(dpy, ck->desktop, &desktop)) {
		if (desktop == -1)
			wwin->client_flags.omnipresent = 1;
		else
			*workspace = desktop;
	}

	atoms = WXRcvAtomList(dpy, ck->state, &nitems);
	if (atoms) {
		for (i = 0; i < nitems; ++i)
			doStateAtom(wwin, atoms[i], _NET_WM_STATE_ADD, True);
		wfree(atoms);
	}

	atoms = WXRcvAtomList(dpy, ck->type, &nitems);
	if (atoms) {
		for (i = 0; i < nitems; ++i) {
			if (handleWindowType(wwin, atoms[i], layer))
				break;
		}
		wfree(atoms);
	}

	wNETWMUpdateActions(wwin, False);
	updateStrut(wwin->screen_ptr, wwin->client_win, False);
	addStrutCk(wwin->screen_ptr, wwin->client_win, ck->strut, ck->strut_partial);

	wScreenUpdateUsableArea(wwin->screen_ptr);
}

WXCookie wNETWMReqWindowName(Window window)
{
	return WXReqString8(dpy, window, net_wm_name, utf8_string);
}

static Bool updateNetIconInfoCk(WWindow *wwin, WXCookie handled_ck, WXCookie geom_ck)
{
	uint32_t handled;
	uint32_t geom[4];
	Bool hasState = False;
	Bool old_state = wwin->flags.net_handle_icon;

	if (WXRcvCardinal(dpy, handled_ck, &handled)) {
		wwin->flags.net_handle_icon = (handled != 0);
		hasState = True;
	} else {
		wwin->flags.net_handle_icon = False;
	}

	if (WXRcvCardinalN(dpy, geom_ck, geom, 4)) {
#ifdef NETWM_PROPER
		if (wwin->flags.net_handle_icon)
#else
		wwin->flags.net_handle_icon = True;
#endif
		{
			wwin->icon_x = geom[0];
			wwin->icon_y = geom[1];
			wwin->icon_w = geom[2];
			wwin->icon_h = geom[3];
		}

		hasState = True;
	} else {
		wwin->flags.net_handle_icon = False;
	}

	if (wwin->flags.miniaturized && old_state != wwin->flags.net_handle_icon) {
		if (wwin->flags.net_handle_icon) {
			removeIcon(wwin);
		} else {
			wwin->flags.miniaturized = False;
			wwin->flags.skip_next_animation = True;
			wIconifyWindow(wwin);
		}
	}

	return hasState;
}

static Bool updateNetIconInfo(WWindow *wwin)
{
	return updateNetIconInfoCk(wwin,
		WXReqFormat32(dpy, wwin->client_win, net_wm_handled_icons, XA_CARDINAL, 1),
		WXReqFormat32(dpy, wwin->client_win, net_wm_icon_geometry, XA_CARDINAL, 4));
}

static void matchStartupNotification(WWindow *wwin, WNETWMInitialCookies *ck)
{
	char *sid;
	Time launch_ts = 0;
	int sn_ws = -1;
	pid_t pid;

	sid = WXRcvString(dpy, ck->startup_id, NULL);
	if (!sid && wwin->main_window != None && wwin->main_window != wwin->client_win)
		sid = WXGetUTF8String(dpy, wwin->main_window, net_startup_id, NULL);

	pid = wGetWindowPid(wwin->client_win);

	if (wStartupNotifyMatchWindow(wwin->screen_ptr, sid,
				      wwin->wm_class, wwin->wm_instance,
				      pid, &launch_ts, &sn_ws)) {
		/* a leaderless toplevel that we know we launched should
		 * still get a WApplication and an appicon so the dock tile
		 * can attach to it and clear when the window goes away,
		 * treat it as if EmulateAppIcon was set for this map only */
		if (wwin->main_window == None && wwin->transient_for == None
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

	wNETWMShowingDesktop(wwin->screen_ptr, False);

	/* wNETWMRcvClientHints() already consumed _NET_WM_WINDOW_TYPE and
	 * stored the matched atom in wwin->type, so reapply the derived
	 * client flags from that instead of fetching the property again.
	 * The GNUstep branch in wWindowSetupInitialAttributes() skips
	 * wNETWMRcvClientHints(), so for those windows wwin->type is still
	 * None and we fall back to the synchronous fetch the old code path
	 * used. The frame does not exist yet here so the frame-update tail
	 * of updateWindowType() is a no-op either way. */
	if (wwin->type != None) {
		int layer;
		handleWindowType(wwin, wwin->type, &layer);
	} else {
		updateWindowType(wwin);
	}

	updateNetIconInfoCk(wwin, ck->handled_icons, ck->icon_geom);
	updateIconImage(wwin);

	updateUserTimeWindow(wwin, &ck->user_time_win);
	readUserTime(wwin);
	matchStartupNotification(wwin, ck);

	cacheLocalPid(wwin);
	wNETWMSyncRequestSetup(wwin);
}

void wNETWMCheckInitialClientState(WWindow *wwin)
{
	WNETWMInitialCookies ck;

	wNETWMReqInitialStateCookies(wwin->client_win, &ck);
	wNETWMCheckInitialClientStateCk(wwin, &ck);
	WXDiscard(dpy, ck.opacity);
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
		WXReqFormat32(dpy, wwin->client_win, net_wm_window_opacity, XA_CARDINAL, 1));
}

static void handleDesktopNames(WScreen *scr)
{
	char *data, *name;
	size_t i, len;
	int n;

	data = WXGetUTF8String(dpy, scr->root_win, net_desktop_names, &len);
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

Bool wNETWMProcessClientMessage(XClientMessageEvent *event)
{
	WScreen *scr;
	WWindow *wwin;

#ifdef DEBUG_WMSPEC
	wmessage("processClientMessage type %s", XGetAtomName(dpy, event->message_type));
#endif

	/* every NETWM client message carries 32-bit data fields, refuse
	 * format-8/16 spoofs before reading data.l[] */
	if (event->format != 32)
		return False;

	scr = wScreenForWindow(event->window);
	if (scr) {
		/* generic client messages */
		if (event->message_type == net_current_desktop) {
			wWorkspaceChange(scr, event->data.l[0]);
			return True;

		} else if (event->message_type == net_number_of_desktops) {
			long value;

			value = event->data.l[0];
			if (value < 1)
				value = 1;
			else if (value > MAX_WORKSPACES)
				value = MAX_WORKSPACES;

			if (value > scr->workspace_count) {
				wWorkspaceMake(scr, value - scr->workspace_count);
			} else if (value < scr->workspace_count) {
				int i;
				Bool rebuild = False;

				for (i = scr->workspace_count - 1; i >= value; i--) {
					if (!wWorkspaceDelete(scr, i)) {
						rebuild = True;
						break;
					}
				}

				if (rebuild)
					updateWorkspaceCount(scr);
			}
			return True;

		} else if (event->message_type == net_showing_desktop) {
			wNETWMShowingDesktop(scr, event->data.l[0]);
			return True;

		} else if (event->message_type == net_desktop_names) {
			handleDesktopNames(scr);
			return True;

		} else if (event->message_type == w_global.atom.wm.protocols
			   && (Atom)event->data.l[0] == net_wm_ping) {
			/* client echoed our ping back to the root window,
			 * data.l[2] is the client window per spec */
			wwin = wWindowFor((Window)event->data.l[2]);
			if (wwin && wwin->net_ping.timer
			    && (Time)event->data.l[1] == wwin->net_ping.timestamp)
				wNETWMCancelPing(wwin);
			return True;
		}
	}

	/* _NET_REQUEST_FRAME_EXTENTS is sent for a window that is not yet
	 * mapped or managed, so handle it before requiring a WWindow. The
	 * spec only asks for an estimate, reply with the extents a normally
	 * decorated window would get under the current theme, which is what
	 * clients use this for (placing themselves so the framed window
	 * lands where they expect). If the window turns out to be a splash
	 * or borderless type the real extents replace this on map. */
	if (event->message_type == net_request_frame_extents) {
		const WConfig *cfg = w_global.config;
		uint32_t extents[4];
		int top;

		if (!scr)
			scr = wScreenForWindow(event->window);
		if (!scr)
			return True;
		top = WMFontHeight(scr->title_font)
		      + (cfg->window_title_clearance + TITLEBAR_EXTEND_SPACE) * 2;
		if (top > cfg->window_title_max_height)
			top = cfg->window_title_max_height;
		if (top < cfg->window_title_min_height)
			top = cfg->window_title_min_height;
		extents[0] = scr->frame_border_width;
		extents[1] = scr->frame_border_width;
		extents[2] = top + scr->frame_border_width;
		extents[3] = RESIZEBAR_HEIGHT + scr->frame_border_width;
		WXSetCardinalN(dpy, event->window, net_frame_extents, extents, 4);
		XFlush(dpy);
		return True;
	}

	/* window specific client messages */

	wwin = wWindowFor(event->window);
	if (!wwin)
		return False;

	if (event->message_type == net_active_window) {
		WScreen *scr = wwin->screen_ptr;
		long source = event->data.l[0];
		Time ev_time = (Time)event->data.l[1];
		Bool same_workspace = wwin->frame->workspace == scr->current_workspace;
		Bool same_head = True;
		Bool allow;

		if (w_global.config->per_head_workspaces) {
			/* In per-head mode the target may be visible on a
			 * head whose workspace differs from the mirror,
			 * collapse both gates into one visibility check. */
			same_workspace = wWindowIsVisibleOnAnyHead(wwin);
			same_head = True;
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
			allow = False;
		} else if (source == 2 || WFLAGP(wwin, focus_across_wksp)) {
			allow = True;
		} else {
			allow = same_workspace
				&& (same_head || w_global.config->auto_focus_across_heads);
		}

		if (allow) {
			/* an honoured non-pager request carries a user-activity
			 * timestamp, advance the focus-stealing baseline so a
			 * later request with an older stamp is recognised as
			 * stale even if no _NET_WM_USER_TIME write intervenes */
			if (source != 2 && ev_time != 0
			    && (scr->net_last_user_time == 0
				|| USER_TIME_NEWER(ev_time, scr->net_last_user_time)))
				scr->net_last_user_time = ev_time;
			wNETWMShowingDesktop(scr, False);
			wMakeWindowVisible(wwin);
		} else if (!wwin->flags.focused && !wwin->flags.net_demands_attention) {
			wwin->flags.net_demands_attention = 1;
			updateStateHint(wwin, False, False);
			wAppBounceWhileUrgent(wApplicationOf(wwin->main_window));
		}
		return True;

	} else if (event->message_type == net_close_window) {
		if (!WFLAGP(wwin, no_closable)) {
			if (wwin->protocols.DELETE_WINDOW) {
				wClientSendProtocol(wwin, w_global.atom.wm.delete_window,
										  w_global.timestamp.last_event);
				wNETWMPingWindow(wwin, w_global.timestamp.last_event);
			}
		}
		return True;

	} else if (event->message_type == net_moveresize_window) {
		XConfigureRequestEvent xcre;
		unsigned long flags = (unsigned long)event->data.l[0];
		int gravity = flags & 0xff;
		int saved_gravity = wwin->normal_hints->win_gravity;

		/*
		 * Reuse the ConfigureRequest path so frame extents, size
		 * constraints and maximize state are handled in one place.
		 * Presence bits 8-11 map directly onto CWX..CWHeight.
		 */
		memset(&xcre, 0, sizeof(xcre));
		xcre.window = event->window;
		xcre.value_mask = (flags >> 8) & (CWX | CWY | CWWidth | CWHeight);
		xcre.x = event->data.l[1];
		xcre.y = event->data.l[2];
		xcre.width = event->data.l[3];
		xcre.height = event->data.l[4];
		xcre.border_width = wwin->old_border_width;

		/* gravity 0 means honour the window's own gravity, anything
		 * outside the X11 gravity range is hostile input */
		if (gravity >= NorthWestGravity && gravity <= StaticGravity)
			wwin->normal_hints->win_gravity = gravity;
		wClientConfigure(wwin, &xcre);
		wwin->normal_hints->win_gravity = saved_gravity;
		return True;

	} else if (event->message_type == net_restack_window) {
		XConfigureRequestEvent xcre;
		long detail = event->data.l[2];
		WWindow *sib;

		/* data.l[0] = source indication, l[1] = sibling, l[2] = stack
		 * mode (Above..Opposite). Route through the ConfigureRequest
		 * path so the sibling is translated to its frame and the
		 * managed stacking list is rebuilt in one place. Refuse a
		 * detail outside the X11 range, and only honour the sibling
		 * when it resolves to a managed client whose frame is a real
		 * sibling of ours under root, otherwise wClientConfigure()
		 * forwards the raw id to XConfigureWindow() and a hostile
		 * non-sibling triggers BadMatch. */
		if (detail < Above || detail > Opposite)
			return True;
		memset(&xcre, 0, sizeof(xcre));
		xcre.window = event->window;
		xcre.value_mask = CWStackMode;
		xcre.detail = detail;
		sib = (event->data.l[1] != None) ? wWindowFor((Window)event->data.l[1]) : NULL;
		if (sib && sib != wwin && sib->screen_ptr == wwin->screen_ptr) {
			xcre.value_mask |= CWSibling;
			xcre.above = sib->client_win;
		}
		xcre.border_width = wwin->old_border_width;
		wClientConfigure(wwin, &xcre);
		return True;

	} else if (event->message_type == net_wm_state) {
		int maximized = wwin->flags.maximized;
		long set = event->data.l[0];

#ifdef DEBUG_WMSPEC
		wmessage("net_wm_state set %ld a1 %s a2 %s", set,
		       XGetAtomName(dpy, event->data.l[1]), XGetAtomName(dpy, event->data.l[2]));
#endif

		doStateAtom(wwin, (Atom) event->data.l[1], set, False);
		if (event->data.l[2])
			doStateAtom(wwin, (Atom) event->data.l[2], set, False);

		if (wwin->flags.maximized != maximized && !wwin->flags.fullscreen) {
			if (!wwin->flags.maximized) {
				wwin->flags.maximized = maximized;
				wUnmaximizeWindow(wwin);
			} else {
				wMaximizeWindow(wwin, wwin->flags.maximized,
						wGetHeadForWindow(wwin));
			}
		}
		updateStateHint(wwin, False, False);
		return True;

	} else if (event->message_type == net_wm_handled_icons || event->message_type == net_wm_icon_geometry) {
		updateNetIconInfo(wwin);
		return True;

	} else if (event->message_type == net_wm_desktop) {
		long desktop = event->data.l[0];

		if (desktop == -1) {
			wWindowSetOmnipresent(wwin, True);
		} else {
			if (IS_OMNIPRESENT(wwin))
				wWindowSetOmnipresent(wwin, False);
			wWindowChangeWorkspace(wwin, desktop);
		}
		return True;

	} else if (event->message_type == net_wm_moveresize) {
		XEvent fake_event;
		int direction = event->data.l[2];
		int x_root = event->data.l[0];
		int y_root = event->data.l[1];
		int button = event->data.l[3];
		int junk;
		Window junkw;
		unsigned int mask;

		if (direction == _NET_WM_MOVERESIZE_CANCEL) {
			if (wwin->moveresize.active) {
				memset(&fake_event, 0, sizeof(XEvent));
				fake_event.type = ButtonRelease;
				fake_event.xbutton.window = wwin->frame->core->window;
				fake_event.xbutton.x_root = x_root;
				fake_event.xbutton.y_root = y_root;
				fake_event.xbutton.button = event->data.l[3];
				XSendEvent(dpy, wwin->frame->core->window, False, ButtonReleaseMask, &fake_event);
			}
			return True;
		}

		/* Check if already in progress */
		if (wwin->moveresize.active)
			return True;

		/* Check if the initiating button is actually pressed */
		if (!XQueryPointer(dpy, wwin->screen_ptr->root_win, &junkw, &junkw,
					&junk, &junk, &junk, &junk, &mask))
			return True;

		if (button > 0) {
			unsigned int expected =
				button == 1 ? Button1Mask :
				button == 2 ? Button2Mask :
				button == 3 ? Button3Mask :
				0;

			if (expected && !(mask & expected))
				/* Race: button already released */
				return True;
		}

		/* Check if operation is allowed */
		if (direction == _NET_WM_MOVERESIZE_MOVE) {
			if (WFLAGP(wwin, no_movable))
				return True;
		} else if (direction <= _NET_WM_MOVERESIZE_SIZE_LEFT) {
			if (WFLAGP(wwin, no_resizable))
				return True;
		} else {
			return True;
		}

		/* Grab the pointer for the operation */
		if (XGrabPointer(dpy, wwin->frame->core->window, False,
								ButtonReleaseMask | PointerMotionMask | ButtonPressMask,
								GrabModeAsync, GrabModeAsync,
								None, None, CurrentTime) != GrabSuccess)
			return True;

		/* Set up the move/resize state */
		wwin->moveresize.active = 1;
		wwin->moveresize.resize_edge = direction;

		memset(&fake_event, 0, sizeof(XEvent));
		fake_event.type = MotionNotify;
		fake_event.xmotion.x_root = x_root;
		fake_event.xmotion.y_root = y_root;
		fake_event.xmotion.window = wwin->frame->core->window;
		if (direction == _NET_WM_MOVERESIZE_MOVE)
			wMouseMoveWindow(wwin, &fake_event);
		else
			wMouseResizeWindow(wwin, &fake_event);

		return True;

	} else if (event->message_type == net_wm_fullscreen_monitors) {
		unsigned long top, bottom, left, right, src_indication;

		top = event->data.l[0];
		bottom = event->data.l[1];
		left = event->data.l[2];
		right = event->data.l[3];
		src_indication = event->data.l[4];

		if (src_indication > 1)
			wwarning("_NET_WM_FULLSCREEN_MONITORS source indication %ld not supported", src_indication);

		wFullscreenMonitorsWindow(wwin, top, bottom, left, right);
		return True;
	}

#ifdef DEBUG_WMSPEC
	wmessage("processClientMessage unsupported type %s", XGetAtomName(dpy, event->message_type));
#endif
	return False;
}

void wNETWMCheckClientHintChange(WWindow *wwin, XPropertyEvent *event)
{
#ifdef DEBUG_WMSPEC
	wmessage("clientHintChange type %s", XGetAtomName(dpy, event->atom));
#endif

	if (event->atom == net_wm_strut || event->atom == net_wm_strut_partial) {
		updateStrut(wwin->screen_ptr, wwin->client_win, False);
		updateStrut(wwin->screen_ptr, wwin->client_win, True);
		wScreenUpdateUsableArea(wwin->screen_ptr);
	} else if (event->atom == net_wm_handled_icons || event->atom == net_wm_icon_geometry) {
		updateNetIconInfo(wwin);
	} else if (event->atom == net_wm_window_type) {
		updateWindowType(wwin);
	} else if (event->atom == net_wm_name) {
		char *name = wNETWMGetWindowName(wwin->client_win);
		wWindowUpdateName(wwin, name);
		if (name)
			wfree(name);
	} else if (event->atom == net_wm_icon_name) {
		if (wwin->icon) {
			wIconChangeTitle(wwin->icon, wwin);
			wIconPaint(wwin->icon);
		}
	} else if (event->atom == net_wm_icon) {
		updateIconImage(wwin);
	} else if (event->atom == net_wm_window_opacity) {
		updateWindowOpacity(wwin);
	} else if (event->atom == net_wm_user_time) {
		readUserTime(wwin);
	} else if (event->atom == net_wm_user_time_window) {
		updateUserTimeWindow(wwin, NULL);
		readUserTime(wwin);
	}
}

int wNETWMGetPidForWindow(Window window)
{
	uint32_t pid;

	if (WXGetCardinal(dpy, window, net_wm_pid, &pid))
		return (int)pid;
	return 0;
}

/* ---- _NET_WM_PING --------------------------------------------------- */

/* how long to wait for the echoed reply before treating the client as
 * hung and prompting the user, in milliseconds */
#define NET_WM_PING_TIMEOUT 5000

Bool wNETWMIsPingProtocol(Atom protocol)
{
	return protocol == net_wm_ping;
}

void wNETWMCancelPing(WWindow *wwin)
{
	if (wwin->net_ping.timer) {
		WMDeleteTimerHandler(wwin->net_ping.timer);
		wwin->net_ping.timer = NULL;
	}
	wwin->net_ping.timestamp = 0;
	wwin->net_ping.term_sent = 0;
}

/* read _NET_WM_PID and WM_CLIENT_MACHINE once at manage time and cache
 * the pid only when the client explicitly names this host, so a later
 * property change cannot redirect Force Quit at an arbitrary process,
 * and a missing WM_CLIENT_MACHINE is treated as remote */
static void cacheLocalPid(WWindow *wwin)
{
	char host[256], *machine;
	int pid;

	wwin->net_ping.pid = 0;

	pid = wNETWMGetPidForWindow(wwin->client_win);
	if (pid <= 0)
		return;

	machine = WXGetLatin1String(dpy, wwin->client_win, XA_WM_CLIENT_MACHINE, NULL);
	if (!machine)
		return;

	if (gethostname(host, sizeof(host)) == 0) {
		host[sizeof(host) - 1] = '\0';
		if (strcmp(host, machine) == 0)
			wwin->net_ping.pid = pid;
	}
	wfree(machine);
}

static void handlePingTimeout(void *cdata)
{
	WWindow *wwin = cdata;
	const char *title;
	char *msg;
	int pid, result;

	wwin->net_ping.timer = NULL;

	title = (wwin->frame && wwin->frame->title) ? wwin->frame->title : "";
	msg = wstrconcat(title,
			 _(" is not responding.\n\n"
			   "You can wait for it to continue or force it to quit, "
			   "losing any unsaved changes."));

	wretain(wwin);
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
				kill((pid_t)pid, SIGTERM);
				wwin->net_ping.term_sent = 1;
				wNETWMPingWindow(wwin, w_global.timestamp.last_event);
			} else {
				kill((pid_t)pid, SIGKILL);
			}
		} else {
			wClientKill(wwin);
		}
	} else {
		/* Wait, ping again so a still-hung client re-prompts */
		wNETWMPingWindow(wwin, w_global.timestamp.last_event);
	}
	wrelease(wwin);
}

void wNETWMPingWindow(WWindow *wwin, Time timestamp)
{
	if (!wwin->protocols.NET_WM_PING)
		return;
	/* one outstanding ping per window, the reply for the earlier close
	 * attempt is just as good an alive signal */
	if (wwin->net_ping.timer)
		return;

	if (timestamp == CurrentTime)
		timestamp = w_global.timestamp.last_event;
	wwin->net_ping.timestamp = timestamp;

	WXSendClientMessage(dpy, wwin->client_win, NoEventMask,
			    wwin->client_win, w_global.atom.wm.protocols,
			    (long)net_wm_ping, (long)timestamp,
			    (long)wwin->client_win, 0, 0);
	XFlush(dpy);

	wwin->net_ping.timer =
		WMAddTimerHandler(NET_WM_PING_TIMEOUT, handlePingTimeout, wwin);
}

/* ---- _NET_WM_USER_TIME / _NET_WM_USER_TIME_WINDOW ------------------- */

/* maps the indirected user-time carrier window back to its WWindow so
 * PropertyNotify on the helper can be routed without a linear scan */
static XContext user_time_ctx = 0;

static void readUserTime(WWindow *wwin)
{
	Window src = wwin->net_user_time_win ? wwin->net_user_time_win
					     : wwin->client_win;
	uint32_t ts;

	if (!WXGetCardinal(dpy, src, net_wm_user_time, &ts))
		return;

	wwin->net_user_time = (Time)ts;
	wwin->net_user_time_set = 1;

	/* track the newest activity timestamp seen on this screen so a
	 * later map can be compared against it */
	if (ts != 0 && (wwin->screen_ptr->net_last_user_time == 0
			|| USER_TIME_NEWER(ts, wwin->screen_ptr->net_last_user_time)))
		wwin->screen_ptr->net_last_user_time = (Time)ts;
}

static void detachUserTimeWindow(WWindow *wwin)
{
	if (!user_time_ctx || wwin->net_user_time_win == None)
		return;
	XDeleteContext(dpy, wwin->net_user_time_win, user_time_ctx);
	/* restore exactly the mask we held before attaching, never blanket
	 * NoEventMask, so a malicious XID cannot trick us into dropping
	 * SubstructureRedirect on root or button grabs on a frame */
	XSelectInput(dpy, wwin->net_user_time_win, wwin->net_user_time_win_mask);
	wwin->net_user_time_win = None;
	wwin->net_user_time_win_mask = 0;
}

/* the helper XID comes from a client property and is hostile input,
 * refuse to select on anything we already manage or already hold a mask
 * on, that covers root, frames, client windows, icons and the supporting
 * window without having to enumerate them */
static Bool isSafeUserTimeWindow(WWindow *wwin, Window win, long *prev_mask)
{
	XWindowAttributes attr;
	XPointer p;
	WScreen *scr;
	int i;

	if (win == None || win == wwin->client_win)
		return False;
	if (XFindContext(dpy, win, w_global.context.client_win, &p) != XCNOENT)
		return False;
	if (user_time_ctx
	    && XFindContext(dpy, win, user_time_ctx, &p) != XCNOENT)
		return False;
	for (i = 0; i < w_global.screen_count; i++) {
		scr = wScreenWithNumber(i);
		if (win == scr->root_win || win == scr->info_window || win == scr->no_focus_win
		    || win == scr->dock_shadow || win == scr->workspace_name
		    || win == scr->mini_screenshot)
			return False;
	}
	if (!XGetWindowAttributes(dpy, win, &attr))
		return False;
	if (attr.your_event_mask != 0)
		return False;

	*prev_mask = attr.your_event_mask;
	return True;
}

static void updateUserTimeWindow(WWindow *wwin, WXCookie *ck)
{
	Window win = None;
	long prev_mask = 0;

	if (ck)
		WXRcvWindow(dpy, *ck, &win);
	else
		WXGetWindow(dpy, wwin->client_win, net_wm_user_time_window, &win);

	if (win == wwin->net_user_time_win)
		return;

	detachUserTimeWindow(wwin);

	if (!isSafeUserTimeWindow(wwin, win, &prev_mask))
		return;

	if (!user_time_ctx)
		user_time_ctx = XUniqueContext();
	wwin->net_user_time_win = win;
	wwin->net_user_time_win_mask = prev_mask;
	XSaveContext(dpy, win, user_time_ctx, (XPointer)wwin);
	XSelectInput(dpy, win, prev_mask | PropertyChangeMask);
}

WWindow *wNETWMUserTimeWindowFor(Window window)
{
	XPointer p;

	if (!user_time_ctx)
		return NULL;
	if (XFindContext(dpy, window, user_time_ctx, &p) != 0)
		return NULL;
	return (WWindow *)p;
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

Bool wNETWMIsSyncRequestProtocol(Atom protocol)
{
#ifdef USE_XSYNC
	return protocol == net_wm_sync_request;
#else
	(void)protocol;
	return False;
#endif
}

#ifdef USE_XSYNC

/* maps an XSyncAlarm XID back to its WWindow so the AlarmNotify handler
 * can clear the waiting flag without scanning the managed list */
static XContext sync_alarm_ctx = 0;

/* give up waiting for a client redraw after this many milliseconds and
 * fall back to unsynchronised resize for the rest of the drag */
#define NET_WM_SYNC_TIMEOUT 500

/* the counter XID comes from a client property, treat any X error during
 * an XSync call on it as "this client lied", swallow it and disable sync
 * for that window rather than letting it surface as an internal error */
static int xsync_trap_tripped;
static XErrorHandler xsync_trap_prev;

static int xsyncErrorTrap(Display *d, XErrorEvent *e)
{
	(void)d; (void)e;
	xsync_trap_tripped = 1;
	return 0;
}

static void xsyncTrapPush(void)
{
	xsync_trap_tripped = 0;
	XSync(dpy, False);
	xsync_trap_prev = XSetErrorHandler(xsyncErrorTrap);
}

static int xsyncTrapPop(void)
{
	XSync(dpy, False);
	XSetErrorHandler(xsync_trap_prev);
	return xsync_trap_tripped;
}

void wNETWMSyncRequestSetup(WWindow *wwin)
{
	XSyncAlarmAttributes attr;
	XSyncValue val;
	uint32_t counter;

	if (!w_global.xext.xsync.supported || !wwin->protocols.NET_WM_SYNC_REQUEST)
		return;
	if (!WXGetCardinal(dpy, wwin->client_win, net_wm_sync_request_counter, &counter)
	    || counter == 0)
		return;

	wwin->net_sync.value_lo = 0;
	wwin->net_sync.value_hi = 0;
	wwin->net_sync.waiting = 0;
	wwin->net_sync.disabled = 0;

	xsyncTrapPush();

	XSyncIntToValue(&val, 0);
	XSyncSetCounter(dpy, (XSyncCounter)counter, val);

	attr.trigger.counter = (XSyncCounter)counter;
	attr.trigger.value_type = XSyncAbsolute;
	attr.trigger.test_type = XSyncPositiveComparison;
	XSyncIntToValue(&attr.trigger.wait_value, 1);
	XSyncIntToValue(&attr.delta, 1);
	attr.events = True;

	wwin->net_sync.alarm = XSyncCreateAlarm(dpy,
		XSyncCACounter | XSyncCAValueType | XSyncCATestType
		| XSyncCAValue | XSyncCADelta | XSyncCAEvents, &attr);

	if (xsyncTrapPop()) {
		if (wwin->net_sync.alarm) {
			xsyncTrapPush();
			XSyncDestroyAlarm(dpy, (XSyncAlarm)wwin->net_sync.alarm);
			xsyncTrapPop();
		}
		wwin->net_sync.alarm = 0;
		wwin->net_sync.counter = 0;
		wwin->net_sync.disabled = 1;
		return;
	}

	wwin->net_sync.counter = counter;

	if (wwin->net_sync.alarm) {
		if (!sync_alarm_ctx)
			sync_alarm_ctx = XUniqueContext();
		XSaveContext(dpy, wwin->net_sync.alarm, sync_alarm_ctx,
			     (XPointer)wwin);
	}
}

void wNETWMSyncRequestDestroy(WWindow *wwin)
{
	if (wwin->net_sync.alarm) {
		if (sync_alarm_ctx)
			XDeleteContext(dpy, wwin->net_sync.alarm, sync_alarm_ctx);
		xsyncTrapPush();
		XSyncDestroyAlarm(dpy, (XSyncAlarm)wwin->net_sync.alarm);
		xsyncTrapPop();
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
	XSyncAlarmAttributes attr;
	XSyncValue target;

	if (!wwin->net_sync.counter || !wwin->net_sync.alarm
	    || wwin->net_sync.disabled || wwin->net_sync.waiting)
		return;

	if (++wwin->net_sync.value_lo == 0)
		wwin->net_sync.value_hi++;

	WXSendClientMessage(dpy, wwin->client_win, NoEventMask,
			    wwin->client_win, w_global.atom.wm.protocols,
			    (long)net_wm_sync_request,
			    (long)w_global.timestamp.last_event,
			    (long)wwin->net_sync.value_lo,
			    (long)wwin->net_sync.value_hi, 0);

	XSyncIntsToValue(&target, wwin->net_sync.value_lo,
			 (int)wwin->net_sync.value_hi);
	attr.trigger.wait_value = target;

	xsyncTrapPush();
	XSyncChangeAlarm(dpy, (XSyncAlarm)wwin->net_sync.alarm,
			 XSyncCAValue, &attr);
	if (xsyncTrapPop()) {
		wwin->net_sync.disabled = 1;
		wwin->net_sync.waiting = 0;
		return;
	}

	gettimeofday(&wwin->net_sync.wait_start, NULL);
	wwin->net_sync.waiting = 1;
}

Bool wNETWMSyncRequestHandleAlarm(XEvent *event)
{
	uint32_t alarm;
	XPointer p;

	if (!w_global.xext.xsync.supported
	    || event->type != w_global.xext.xsync.event_base + XSyncAlarmNotify)
		return False;

	/* xcb_sync_alarm_notify_event_t carries the alarm id at wire
	 * offset 4, the cooker stashed the 32 wire bytes for us */
	memcpy(&alarm, ((WXWireEvent *)event)->wire + 4, sizeof(alarm));
	if (!sync_alarm_ctx
	    || XFindContext(dpy, alarm, sync_alarm_ctx, &p) != 0)
		return True;

	((WWindow *)p)->net_sync.waiting = 0;
	return True;
}

/* called from the opaque-resize loop before issuing the next configure,
 * drain any AlarmNotify already in the Xlib queue so a client that has
 * caught up clears its waiting flag, and if the request has been
 * outstanding for longer than NET_WM_SYNC_TIMEOUT fall back to
 * unsynchronised resize for the rest of the drag, never blocks so
 * ButtonRelease and Expose for other clients are serviced immediately */
Bool wNETWMSyncRequestPending(WWindow *wwin)
{
	XEvent ev;
	struct timeval now;
	long elapsed;

	if (!wwin->net_sync.waiting)
		return False;

	while (WXEvqTakeTyped(dpy,
			      w_global.xext.xsync.event_base + XSyncAlarmNotify,
			      None, &ev))
		wNETWMSyncRequestHandleAlarm(&ev);

	if (!wwin->net_sync.waiting)
		return False;

	gettimeofday(&now, NULL);
	elapsed = (now.tv_sec - wwin->net_sync.wait_start.tv_sec) * 1000
		  + (now.tv_usec - wwin->net_sync.wait_start.tv_usec) / 1000;
	if (elapsed >= NET_WM_SYNC_TIMEOUT) {
		wwin->net_sync.waiting = 0;
		wwin->net_sync.disabled = 1;
		return False;
	}

	return True;
}

#else /* !USE_XSYNC */

void wNETWMSyncRequestSetup(WWindow *wwin)       { (void)wwin; }
void wNETWMSyncRequestDestroy(WWindow *wwin)     { (void)wwin; }
void wNETWMSyncRequestSend(WWindow *wwin)        { (void)wwin; }
Bool wNETWMSyncRequestPending(WWindow *wwin)     { (void)wwin; return False; }
Bool wNETWMSyncRequestHandleAlarm(XEvent *event) { (void)event; return False; }

#endif /* USE_XSYNC */

/*
 * Advance the screen's user-activity baseline from a WM-side interaction
 * (titlebar drag, resize, keyboard move) so a window that maps right after
 * with a timestamp older than the interaction does not steal focus.
 */
void wNETWMBumpUserTime(WScreen *scr, Time ts)
{
	if (ts != 0 && (scr->net_last_user_time == 0
			|| USER_TIME_NEWER(ts, scr->net_last_user_time)))
		scr->net_last_user_time = ts;
}

Bool wNETWMShouldStealFocus(WWindow *wwin)
{
	WScreen *scr = wwin->screen_ptr;
	WWindow *focused;

	/* a client that explicitly stamps user_time = 0 is asking the WM
	 * not to focus it on initial map */
	if (wwin->net_user_time_set && wwin->net_user_time == 0)
		return False;

	focused = scr->focused_window;
	if (focused && focused != wwin) {
		/* a transient of the current focus holder, or of its
		 * application leader, is part of what the user is already
		 * working in, do not let a stale or missing timestamp keep
		 * a Save dialog from taking input */
		if (wwin->transient_for != None
		    && wwin->transient_for != scr->root_win
		    && (wwin->transient_for == focused->client_win
			|| wwin->transient_for == focused->main_window))
			return True;

		/* same for a _NET_WM_STATE_MODAL window of the focused
		 * application, which a toolkit may map as group-transient
		 * rather than transient for a specific window */
		if (wwin->flags.net_modal
		    && wwin->main_window != None
		    && wwin->main_window == focused->main_window)
			return True;

		/* a window mapping into a higher stacking layer than the
		 * current focus is going to obscure it regardless, denying
		 * focus would leave keyboard input going to a window the
		 * user can no longer see */
		if (wwin->frame && focused->frame
		    && WINDOW_LEVEL(wwin) > WINDOW_LEVEL(focused))
			return True;
	}

	/* if no other window has reported activity yet, or this window
	 * never set a user time, fall through to the existing policy */
	if (!wwin->net_user_time_set || scr->net_last_user_time == 0)
		return True;

	/* deny focus when the new window's last user activity is older
	 * than what we have already seen elsewhere */
	if (USER_TIME_NEWER(scr->net_last_user_time, wwin->net_user_time))
		return False;

	return True;
}

char *wNETWMGetWindowName(Window window)
{
	return WXGetUTF8String(dpy, window, net_wm_name, NULL);
}

char *wNETWMGetIconName(Window window)
{
	return WXGetUTF8String(dpy, window, net_wm_icon_name, NULL);
}

/*
 * Publish the title actually shown by the window manager when it differs
 * from what the client set, or remove the property when they match again.
 */
static void updateVisibleNameProp(Window window, Atom prop,
				  const char *visible, const char *client_name)
{
	if (window == None)
		return;

	if (visible && (!client_name || strcmp(visible, client_name) != 0))
		WXSetUTF8String(dpy, window, prop, visible, strlen(visible));
	else
		WXDeleteProperty(dpy, window, prop);
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
		updateStateHint(wwin, True, False);

		updateStrut(wwin->screen_ptr, wwin->client_win, False);
		updateStrut(wwin->screen_ptr, wwin->client_win, True);
		wScreenUpdateUsableArea(wwin->screen_ptr);
	} else if (strcmp(name, WMNUnmanaged) == 0 && wwin) {
		updateClientList(wwin->screen_ptr);
		updateClientListStacking(wwin->screen_ptr, wwin);
		updateWorkspaceHint(wwin, False, True);
		updateStateHint(wwin, False, True);
		wNETWMUpdateActions(wwin, True);
		wNETWMUpdateVisibleName(wwin, NULL, NULL);
		wNETWMUpdateVisibleIconName(wwin, NULL, NULL);

		updateStrut(wwin->screen_ptr, wwin->client_win, False);
		wScreenUpdateUsableArea(wwin->screen_ptr);
	} else if (strcmp(name, WMNChangedStacking) == 0 && wwin) {
		updateClientListStacking(wwin->screen_ptr, NULL);
		updateStateHint(wwin, False, False);
	} else if (strcmp(name, WMNChangedFocus) == 0 && wwin) {
		updateFocusHint(ndata->scr);
		updateStateHint(wwin, False, False);
	} else if (strcmp(name, WMNChangedWorkspace) == 0 && wwin) {
		updateWorkspaceHint(wwin, False, False);
		updateStateHint(wwin, True, False);
	} else if (strcmp(name, WMNChangedState) == 0 && wwin) {
		updateStateHint(wwin, !strcmp(data, "omnipresent"), False);
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
	 * part of the client window.  In our case that means
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
		extents[0] += wwin->screen_ptr->frame_border_width;
		extents[1] += wwin->screen_ptr->frame_border_width;
		extents[2] += wwin->screen_ptr->frame_border_width;
		extents[3] += wwin->screen_ptr->frame_border_width;
	}

	WXSetCardinalN(dpy, wwin->client_win, net_frame_extents, extents, 4);
}

void wNETCleanupFrameExtents(WWindow *wwin)
{
	WXDeleteProperty(dpy, wwin->client_win, net_frame_extents);
}
