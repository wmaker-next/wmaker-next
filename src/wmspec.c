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
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*
 * All property reads/writes go through the typed WXProp helper layer
 * (WINGs/WXProp.h), which validates type/format/count and normalises the
 * Xlib long[] representation to uint32_t.
 */

#include "wconfig.h"

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <string.h>
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
#include "xinerama.h"
#include "properties.h"


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
static Atom net_desktop_layout;	/* XXX */
static Atom net_showing_desktop;

/* Other Root Window Messages */
static Atom net_close_window;
static Atom net_moveresize_window;	/* TODO */
static Atom net_wm_moveresize;

/* Application Window Properties */
static Atom net_wm_name;
static Atom net_wm_visible_name;	/* TODO (unnecessary?) */
static Atom net_wm_icon_name;
static Atom net_wm_visible_icon_name;	/* TODO (unnecessary?) */
static Atom net_wm_desktop;
#ifdef USE_XINERAMA
static Atom net_wm_fullscreen_monitors;
#endif
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
static Atom net_wm_strut;
static Atom net_wm_strut_partial;
static Atom net_wm_icon_geometry;
static Atom net_wm_icon;
static Atom net_wm_pid;
static Atom net_wm_handled_icons;
static Atom net_wm_window_opacity;

static Atom net_frame_extents;

/* Window Manager Protocols */
static Atom net_wm_ping;	/* TODO */

static Atom utf8_string;

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

	{"_NET_WM_NAME", &net_wm_name},
	{"_NET_WM_VISIBLE_NAME", &net_wm_visible_name},
	{"_NET_WM_ICON_NAME", &net_wm_icon_name},
	{"_NET_WM_VISIBLE_ICON_NAME", &net_wm_visible_icon_name},
	{"_NET_WM_DESKTOP", &net_wm_desktop},
#ifdef USE_XINERAMA
	{"_NET_WM_FULLSCREEN_MONITORS", &net_wm_fullscreen_monitors},
#endif
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
	{"_NET_WM_STRUT", &net_wm_strut},
	{"_NET_WM_STRUT_PARTIAL", &net_wm_strut_partial},
	{"_NET_WM_ICON_GEOMETRY", &net_wm_icon_geometry},
	{"_NET_WM_ICON", &net_wm_icon},
	{"_NET_WM_PID", &net_wm_pid},
	{"_NET_WM_HANDLED_ICONS", &net_wm_handled_icons},
	{"_NET_WM_WINDOW_OPACITY", &net_wm_window_opacity},

	{"_NET_FRAME_EXTENTS", &net_frame_extents},

	{"_NET_WM_PING", &net_wm_ping},

	{"UTF8_STRING", &utf8_string},
};

#define _NET_WM_STATE_ADD 1
#define _NET_WM_STATE_TOGGLE 2

static void observer(void *self, WMNotification *notif);
static void wsobserver(void *self, WMNotification *notif);

static void updateClientList(WScreen *scr);
static void updateClientListStacking(WScreen *scr, WWindow *);

static void updateWorkspaceNames(WScreen *scr);
static void updateCurrentWorkspace(WScreen *scr);
static void updateWorkspaceCount(WScreen *scr);
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
	/* XXX: extend this !!! */

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
	atom[i++] = net_wm_moveresize;
	atom[i++] = net_wm_desktop;
#ifdef USE_XINERAMA
	atom[i++] = net_wm_fullscreen_monitors;
#endif
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

	atom[i++] = net_wm_strut;
	atom[i++] = net_wm_icon_geometry;
	atom[i++] = net_wm_icon;
	atom[i++] = net_wm_handled_icons;
	atom[i++] = net_wm_window_opacity;

	atom[i++] = net_frame_extents;

	atom[i++] = net_wm_name;
	atom[i++] = net_wm_icon_name;

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

	if (wPreferences.enforce_icon_margin) {

		/* better use only 75% of icon_size. For 64x64 this means 48x48
		 * This leaves room around the icon for the miniwindow title and
		 * results in better overall aesthetics -Dan */
		wanted = (int)((double)wPreferences.icon_size * 0.75 + 0.5);

		/* the size should be a multiple of 4 */
		wanted = (wanted >> 2) << 2;

	} else {

		/* This is the "old" approach, which tries to find the largest
		 * icon that still fits into icon_size. */
		wanted = wPreferences.icon_size;

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
	image = wIconValidateIconSize(image, wPreferences.icon_size);

	return image;
}

static void updateIconImage(WWindow *wwin)
{
	/* Remove the icon image from X11 */
	if (wwin->net_icon_image)
		RReleaseImage(wwin->net_icon_image);

	/* Save the icon in the X11 icon */
	wwin->net_icon_image = get_window_image_from_x11(wwin->client_win);

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
		/* FIXME: get rid of workspace flashing ! */
		int ws = scr->current_workspace;
		WWindow **tmp;
		for (tmp = scr->netdata->show_desktop; *tmp; ++tmp) {
			wDeiconifyWindow(*tmp);
			(*tmp)->flags.net_show_desktop = 0;
		}
		if (ws != scr->current_workspace)
			wWorkspaceChange(scr, ws);
		wfree(scr->netdata->show_desktop);
		scr->netdata->show_desktop = NULL;
		updateShowDesktop(scr, False);
	}
}

void wNETWMInitStuff(WScreen *scr)
{
	NetData *data;
	int i;

#ifdef DEBUG_WMSPEC
	wmessage("wNETWMInitStuff");
#endif

#ifdef HAVE_XINTERNATOMS
	{
		Atom atoms[wlengthof(atomNames)];
		char *names[wlengthof(atomNames)];

		for (i = 0; i < wlengthof(atomNames); ++i)
			names[i] = atomNames[i].name;

		XInternAtoms(dpy, &names[0], wlengthof(atomNames), False, atoms);
		for (i = 0; i < wlengthof(atomNames); ++i)
			*atomNames[i].atom = atoms[i];

	}
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

	WMAddNotificationObserver(wsobserver, data, WMNWorkspaceCreated, NULL);
	WMAddNotificationObserver(wsobserver, data, WMNWorkspaceDestroyed, NULL);
	WMAddNotificationObserver(wsobserver, data, WMNWorkspaceChanged, NULL);
	WMAddNotificationObserver(wsobserver, data, WMNWorkspaceNameChanged, NULL);

	updateClientList(scr);
	updateClientListStacking(scr, NULL);
	updateWorkspaceCount(scr);
	updateWorkspaceNames(scr);
	updateShowDesktop(scr, False);

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
	Atom action[10];	/* nr of actions atoms defined */
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

	WXSetAtomN(dpy, wwin->client_win, net_wm_allowed_actions, action, i);
}

void wNETWMUpdateWorkarea(WScreen *scr)
{
	WArea total_usable;
	int nb_workspace;

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
		int i;

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

	{
		uint32_t property_value[nb_workspace * 4];
		int i;

		for (i = 0; i < nb_workspace; i++) {
			property_value[4 * i + 0] = total_usable.x1;
			property_value[4 * i + 1] = total_usable.y1;
			property_value[4 * i + 2] = total_usable.x2 - total_usable.x1;
			property_value[4 * i + 3] = total_usable.y2 - total_usable.y1;
		}

		WXSetCardinalN(dpy, scr->root_win, net_workarea, property_value, nb_workspace * 4);
	}
}

Bool wNETWMGetUsableArea(WScreen *scr, int head, WArea *area)
{
	WReservedArea *cur;
	WMRect rect;

	if (!scr->netdata || !scr->netdata->strut)
		return False;

	area->x1 = area->y1 = area->x2 = area->y2 = 0;

	for (cur = scr->netdata->strut; cur; cur = cur->next) {
		WWindow *wwin = wWindowFor(cur->window);
		if (wWindowTouchesHead(wwin, head)) {
			if (cur->area.x1 > area->x1)
				area->x1 = cur->area.x1;
			if (cur->area.y1 > area->y1)
				area->y1 = cur->area.y1;
			if (cur->area.x2 > area->x2)
				area->x2 = cur->area.x2;
			if (cur->area.y2 > area->y2)
				area->y2 = cur->area.y2;
		}
	}

	if (area->x1 == 0 && area->x2 == 0 && area->y1 == 0 && area->y2 == 0)
		return False;

	rect = wGetRectForHead(scr, head);

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
#ifdef USE_XINERAMA
		WXDeleteProperty(dpy, wwin->client_win, net_wm_fullscreen_monitors);
#endif
	} else {
		Atom state[15];	/* nr of defined state atoms */
		int i = 0;

		if (changedWorkspace || (wPreferences.sticky_icons && !IS_OMNIPRESENT(wwin)))
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
		if ((wwin->flags.hidden || wwin->flags.miniaturized) && !wwin->flags.net_show_desktop) {
			state[i++] = net_wm_state_hidden;
			state[i++] = net_wm_state_skip_pager;

			if (wwin->flags.miniaturized && wPreferences.sticky_icons) {
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

		WXSetAtomN(dpy, wwin->client_win, net_wm_state, state, i);

#ifdef USE_XINERAMA
		if (wwin->flags.fullscreen && (wwin->flags.fullscreen_monitors[0] != -1)) {
			uint32_t data[4];

			data[0] = wwin->flags.fullscreen_monitors[0];
			data[1] = wwin->flags.fullscreen_monitors[1];
			data[2] = wwin->flags.fullscreen_monitors[2];
			data[3] = wwin->flags.fullscreen_monitors[3];

			WXSetCardinalN(dpy, wwin->client_win, net_wm_fullscreen_monitors, data, 4);
		}
#endif
	}
}

static Bool updateStrut(WScreen *scr, Window w, Bool adding)
{
	WReservedArea *area;
	Bool hasState = False;

	if (adding) {
		uint32_t strut[12];

		/* XXX: For net_wm_strut_partial we discard the start/end fields
		 * and treat it as a (deprecated) net_wm_strut. This means we
		 * are marking the whole width or height of the screen as
		 * reserved, which is not necessarily what the strut defines,
		 * but for placement or maximization it's good enough.
		 */
		if (WXGetCardinalN(dpy, w, net_wm_strut, strut, 4) ||
		    WXGetCardinalN(dpy, w, net_wm_strut_partial, strut, 12)) {

			area = (WReservedArea *) wmalloc(sizeof(WReservedArea));
			area->area.x1 = strut[0];
			area->area.x2 = strut[1];
			area->area.y1 = strut[2];
			area->area.y2 = strut[3];

			area->window = w;

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

		if (init) {
			wwin->flags.maximized |= (set ? MAX_VERTICAL : 0);
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

		if (init) {
			wwin->flags.maximized |= (set ? MAX_HORIZONTAL : 0);
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
	} else if (wwin->type == net_wm_window_type_tooltip) {
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
	} else if (wwin->type == net_wm_window_type_notification) {
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
	} else if (wwin->type == net_wm_window_type_dnd) {
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

	/* consume both cookies on every path so callers do not have to */
	if (WXRcvCardinalN(dpy, strut, v, 4)) {
		WXDiscard(dpy, strut_partial);
	} else if (!WXRcvCardinalN(dpy, strut_partial, v, 12)) {
		return;
	}

	area = (WReservedArea *) wmalloc(sizeof(WReservedArea));
	area->area.x1 = v[0];
	area->area.x2 = v[1];
	area->area.y1 = v[2];
	area->area.y2 = v[3];
	area->window = w;

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

void wNETWMCheckClientHints(WWindow *wwin, int *layer, int *workspace)
{
	WNETWMHintCookies ck;

	wNETWMReqClientHints(wwin->client_win, &ck);
	wNETWMRcvClientHints(wwin, &ck, layer, workspace);
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
	char *data, *names[32];
	size_t i, len;
	int n;

	data = WXGetUTF8String(dpy, scr->root_win, net_desktop_names, &len);
	if (data == NULL)
		return;

	n = 0;
	names[n] = data;
	for (i = 0; i < len; i++) {
		if (data[i] == 0) {
			n++;
			if (n >= (int)wlengthof(names))
				break;
			names[n] = &data[i];
		} else if (*names[n] == 0) {
			names[n] = &data[i];
			wWorkspaceRename(scr, n, names[n]);
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
		}
	}

	/* window specific client messages */

	wwin = wWindowFor(event->window);
	if (!wwin)
		return False;

	if (event->message_type == net_active_window) {
		/*
		 * Satisfy a client's focus request only if
		 * - request comes from a pager, or
		 * - it's explicitly allowed in Advanced Options, or
		 * - giving the client the focus does not cause a change in
		 *   the active workspace (XXX: or the active head if Xinerama)
		 */
		if (wwin->frame->workspace == wwin->screen_ptr->current_workspace /* No workspace change */
		    || event->data.l[0] == 2 /* Requested by pager */
		    || WFLAGP(wwin, focus_across_wksp) /* Explicitly allowed */) {
				wNETWMShowingDesktop(wwin->screen_ptr, False);
				wMakeWindowVisible(wwin);
		}
		return True;

	} else if (event->message_type == net_close_window) {
		if (!WFLAGP(wwin, no_closable)) {
			if (wwin->protocols.DELETE_WINDOW)
				wClientSendProtocol(wwin, w_global.atom.wm.delete_window,
										  w_global.timestamp.last_event);
		}
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

		if (wwin->flags.maximized != maximized) {
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

#ifdef USE_XINERAMA
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
#else
	}
#endif

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
	}
}

int wNETWMGetPidForWindow(Window window)
{
	uint32_t pid;

	if (WXGetCardinal(dpy, window, net_wm_pid, &pid))
		return (int)pid;
	return 0;
}

char *wNETWMGetWindowName(Window window)
{
	return WXGetUTF8String(dpy, window, net_wm_name, NULL);
}

char *wNETWMGetIconName(Window window)
{
	return WXGetUTF8String(dpy, window, net_wm_icon_name, NULL);
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

		updateStrut(wwin->screen_ptr, wwin->client_win, False);
		wScreenUpdateUsableArea(wwin->screen_ptr);
	} else if (strcmp(name, WMNResetStacking) == 0 && wwin) {
		updateClientListStacking(wwin->screen_ptr, NULL);
		updateStateHint(wwin, False, False);
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
		wNETWMUpdateWorkarea(scr);
	} else if (strcmp(name, WMNWorkspaceDestroyed) == 0) {
		updateWorkspaceCount(scr);
		updateWorkspaceNames(scr);
		wNETWMUpdateWorkarea(scr);
	} else if (strcmp(name, WMNWorkspaceChanged) == 0) {
		updateCurrentWorkspace(scr);
	} else if (strcmp(name, WMNWorkspaceNameChanged) == 0) {
		updateWorkspaceNames(scr);
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
