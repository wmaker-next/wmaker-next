/* action.c- misc. window commands (miniaturize, hide etc.)
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 1998-2003 Dan Pascu
 *  Copyright (c) 2014-2026 Window Maker Team
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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include "wconfig.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <math.h>
#include <time.h>

#include "WindowMaker.h"
#include "framewin.h"
#include "window.h"
#include "client.h"
#include "icon.h"
#include "colormap.h"
#include "application.h"
#include "actions.h"
#include "stacking.h"
#include "focusmodel.h"
#include "appicon.h"
#include "dock.h"
#include "appmenu.h"
#include "winspector.h"
#include "workspace.h"
#include "layout.h"
#include "xinerama.h"
#include "usermenu.h"
#include "placement.h"
#include "misc.h"
#include "event.h"
#include "tabgroup.h"
#include "wireframe.h"
#include "wsound.h"


#ifndef HAVE_FLOAT_MATHFUNC
#define sinf(x) ((float)sin((double)(x)))
#define cosf(x) ((float)cos((double)(x)))
#define sqrtf(x) ((float)sqrt((double)(x)))
#define atan2f(y, x) ((float)atan((double)(y) / (double)(x)))
#endif

static void find_maximus_geometry(WWindow *wwin, WArea usableArea, int *new_x, int *new_y,
				  unsigned int *new_width, unsigned int *new_height);

/******* Local Variables *******/
#ifdef USE_ANIMATIONS
static struct {
	int steps;
	int delay;
} shadePars[5] = {
	{ SHADE_STEPS_UF, SHADE_DELAY_UF },
	{ SHADE_STEPS_F, SHADE_DELAY_F },
	{ SHADE_STEPS_M, SHADE_DELAY_M },
	{ SHADE_STEPS_S, SHADE_DELAY_S },
	{ SHADE_STEPS_US, SHADE_DELAY_US }
};

#define SHADE_STEPS	shadePars[(int)w_global.config->shade_speed].steps
#define SHADE_DELAY	shadePars[(int)w_global.config->shade_speed].delay
#endif

#define UNSHADE         0
#define SHADE           1

static int compareTimes(xcb_timestamp_t t1, xcb_timestamp_t t2)
{
	xcb_timestamp_t diff;
	if (t1 == t2)
		return 0;
	diff = t1 - t2;
	return (diff < 60000) ? 1 : -1;
}

#ifdef USE_ANIMATIONS
static void shade_animate(WWindow *wwin, bool what);
#else
static inline void shade_animate(WWindow *wwin, bool what)
{
	/*
	 * This function is empty on purpose, so tell the compiler
	 * to not warn about parameters being not used
	 */
	(void) wwin;
	(void) what;
}
#endif

static void focusRestoredWindow(void *data)
{
	xcb_window_t client_win = (xcb_window_t)(uintptr_t)data;
	WWindow *wwin = wWindowFor(client_win);

	if (!wwin || !wwin->flags.mapped)
		return;

	wRaiseFrame(wwin->frame->core);
	xcb_set_input_focus(wdisp->conn, XCB_INPUT_FOCUS_PARENT, wwin->screen_ptr->no_focus_win, XCB_CURRENT_TIME);
	w_global.restore_focus_repair = true;
	wSetFocusTo(wwin->screen_ptr, wwin);
	w_global.restore_focus_repair = false;
}

/*
 *----------------------------------------------------------------------
 * wSetFocusTo--
 *	Changes the window focus to the one passed as argument.
 * If the window to focus is not already focused, it will be brought
 * to the head of the list of windows. Previously focused window is
 * unfocused.
 *
 * Side effects:
 *	Window list may be reordered and the window focus is changed.
 *
 *----------------------------------------------------------------------
 */
void wSetFocusTo(WScreen *scr, WWindow *wwin)
{
	static WScreen *old_scr = NULL;

	WWindow *old_focused;
	WWindow *focused = scr->focused_window;
	xcb_timestamp_t timestamp = wdisp->timestamp.last_event;
	WApplication *oapp = NULL, *napp = NULL;
	WFocusWinIn in;
	WFocusPlan plan;
	WWindow *modal_transient = NULL;
	int wasfocused;

	if (scr->flags.ignore_focus_events || compareTimes(wdisp->timestamp.focus_change, timestamp) > 0)
		return;

	/* if the target window has a mapped modal transient, redirect focus
	 * to that transient so clicking the parent of a modal dialog brings
	 * the dialog forward instead of burying it */
	if (wwin) {
		WWindow *tmp;
		for (tmp = scr->focused_window; tmp; tmp = tmp->prev) {
			if (tmp != wwin && tmp->flags.net_modal &&
			    tmp->transient_for == wwin->client_win &&
			    !tmp->flags.miniaturized && !tmp->flags.hidden &&
			    tmp->flags.mapped) {
				modal_transient = tmp;
				break;
			}
		}
	}

	memset(&in, 0, sizeof(in));
	if (wwin) {
		in.win = wwin;
		in.focusable = WFLAGP(wwin, no_focusable);
		in.focus_mode = wwin->focus_mode;
		in.takes_focus_proto = wwin->protocols.TAKE_FOCUS;
		in.mapped = wwin->flags.mapped;
	}
	wFocusResolve(wwin ? &in : NULL, focused,
		      modal_transient != NULL, modal_transient, &plan);

	if (plan.redirect_to)
		wwin = plan.redirect_win;

	if (!old_scr)
		old_scr = scr;

	old_focused = old_scr->focused_window;

	wdisp->timestamp.focus_change = timestamp;

	if (old_focused)
		oapp = wApplicationOf(old_focused->main_window);

	if (wwin == NULL) {
		xcb_window_t old_client = old_focused ? old_focused->client_win : XCB_NONE;

		xcb_set_input_focus(wdisp->conn, XCB_INPUT_FOCUS_PARENT, scr->no_focus_win, XCB_CURRENT_TIME);
		if (old_focused)
			wWindowUnfocus(old_focused);

		/* the hook may have destroyed the window and its app */
		if (oapp && wWindowFor(old_client)) {
			wAppMenuUnmap(oapp->menu);
			if (w_global.config->highlight_active_app)
				wApplicationDeactivate(oapp);
		}

		WMPostNotificationName(WMNChangedFocus, NULL, (void *)true);
		return;
	}

	if (old_scr != scr && old_focused)
		wWindowUnfocus(old_focused);

	wasfocused = wwin->flags.focused;
	napp = wApplicationOf(wwin->main_window);

	/* remember last workspace where the app has been */
	if (napp)
		napp->last_workspace = WWIN_PRIMARY_WS(wwin);

	if (plan.focus_path) {
		/* install colormap if colormap mode is lock mode */
		if (w_global.config->colormap_mode == WCM_CLICK)
			wColormapInstallForWindow(scr, wwin);

		/* set input focus */
		switch (plan.target) {
		case WFOCUS_TARGET_NONEWIN:
			xcb_set_input_focus(wdisp->conn, XCB_INPUT_FOCUS_PARENT, scr->no_focus_win, XCB_CURRENT_TIME);
			break;
		case WFOCUS_TARGET_CLIENT:
			xcb_set_input_focus(wdisp->conn, XCB_INPUT_FOCUS_PARENT, wwin->client_win, XCB_CURRENT_TIME);
			break;
		case WFOCUS_TARGET_NOTHING:
			break;
		}

		if (plan.send_take_focus)
			wClientSendProtocol(wwin, wdisp->atom.wm.take_focus, timestamp);

		wXFlush(wdisp->conn);
	} else {
		xcb_set_input_focus(wdisp->conn, XCB_INPUT_FOCUS_PARENT, scr->no_focus_win, XCB_CURRENT_TIME);
	}

	if (WFLAGP(wwin, no_focusable))
		return;

	/* if this is not the focused window focus it */
	if (plan.reorder_list) {
		/* change the focus window list order */
		if (wwin->prev)
			wwin->prev->next = wwin->next;

		if (wwin->next)
			wwin->next->prev = wwin->prev;

		wwin->prev = focused;
		if (focused)
			focused->next = wwin;
		wwin->next = NULL;
		scr->focused_window = wwin;

		if (oapp && oapp != napp) {
			wAppMenuUnmap(oapp->menu);
			if (w_global.config->highlight_active_app)
				wApplicationDeactivate(oapp);
		}

		/* reset fullscreen if temporarily removed due to lost focus*/
		if (wwin->flags.fullscreen)
			ChangeStackingLevel(wwin->frame->core, WMFullscreenLevel);
	}

	wWindowFocus(wwin, focused);

	if (napp && !wasfocused) {
#ifdef USER_MENU
		wUserMenuRefreshInstances(napp->menu, wwin);
#endif	/* USER_MENU */

		/* kix: Only menu map with mouse, not alt+tab! */
		if (wwin->flags.mapped)
			wAppMenuMap(napp->menu, wwin);
	}
	if (napp && w_global.config->highlight_active_app)
		wApplicationActivate(napp);

	/* In per-head mode the EWMH _NET_CURRENT_DESKTOP must follow the
	 * focused head, so Chromium/Electron see the right desktop when
	 * comparing it against _NET_WM_DESKTOP of windows on a secondary
	 * monitor. wWorkspaceSetCurrentHead also updates the
	 * compatibility scr->current_workspace mirror and clip label. */
	if (w_global.config->per_head_workspaces)
		wWorkspaceSetCurrentHead(scr, wWindowEffectiveHead(wwin));

	wXFlush(wdisp->conn);
	old_scr = scr;
}

/*
 *----------------------------------------------------------------------
 * wSetFocusToDirection--
 *	Moves focus to the nearest window in the given cardinal
 *	direction (DIRECTION_LEFT, DIRECTION_RIGHT, DIRECTION_UP,
 *	DIRECTION_DOWN from xinerama.h).
 *
 *	Selection algorithm: candidate windows are scored by
 *	(primary_distance + perpendicular_offset). A large penalty is
 *	added when the perpendicular offset exceeds the primary distance
 *	(i.e. the candidate is more than 45 degrees off-axis). The window
 *	with the lowest score wins. If no candidate exists in the requested
 *	direction the call is silently ignored.
 *----------------------------------------------------------------------
 */
void wSetFocusToDirection(WScreen *scr, int direction)
{
	WWindow *focused = scr->focused_window;
	WWindow *best = NULL;
	WWindow *candidate = NULL;
	WFocusDirCandidate *cands;
	int my_cx, my_cy;
	int n, i;

	if (!focused || !focused->flags.mapped)
		return;

	/* centre of the focused window */
	my_cx = focused->frame_x + (int)focused->frame->core->width  / 2;
	my_cy = focused->frame_y + (int)focused->frame->core->height / 2;

	/* reduce the eligible candidates, iterating from most-recently-
	 * focused to least-recently-focused */
	n = 0;
	for (candidate = focused->prev; candidate != NULL; candidate = candidate->prev) {
		if (!candidate->flags.mapped)
			continue;
		if (WFLAGP(candidate, no_focusable))
			continue;
		if (!candidate->frame || !wWindowIsVisibleOnAnyHead(candidate))
			continue;

		/* ignore fully covered windows if cannot raised them */
		if (!w_global.config->circ_raise && wWindowIsFullyCovered(candidate))
			continue;

		n++;
	}

	cands = wmalloc(sizeof(WFocusDirCandidate) * (n > 0 ? n : 1));

	i = 0;
	for (candidate = focused->prev; candidate != NULL; candidate = candidate->prev) {
		if (!candidate->flags.mapped)
			continue;
		if (WFLAGP(candidate, no_focusable))
			continue;
		if (!candidate->frame || !wWindowIsVisibleOnAnyHead(candidate))
			continue;

		/* ignore fully covered windows if cannot raised them */
		if (!w_global.config->circ_raise && wWindowIsFullyCovered(candidate))
			continue;

		/* relative centre of candidate */
		cands[i].win = candidate;
		cands[i].rel_cx = (candidate->frame_x - my_cx) + (int)candidate->frame->core->width / 2;
		cands[i].rel_cy = (candidate->frame_y - my_cy) + (int)candidate->frame->core->height / 2;
		i++;
	}

	best = wFocusNearestInDirection(cands, n, direction);
	wfree(cands);

	if (best) {
		if (w_global.config->circ_raise)
			wRaiseFrame(best->frame->core);
		wSetFocusTo(scr, best);
		warpPointerToFocusedWindow(best);
	}
}

void wShadeWindow(WWindow *wwin)
{

	if (wwin->flags.shaded)
		return;

	/* the client window is a frame child, not a stacked frame, direct
	 * configure (same class as the framewin titlebar/resizebar
	 * pushes) */
	xcb_configure_window(wdisp->conn, wwin->client_win, XCB_CONFIG_WINDOW_STACK_MODE, (const uint32_t[]){ XCB_STACK_MODE_BELOW });
	wSoundPlay(wwin->screen_ptr, WSOUND_SHADE);
	shade_animate(wwin, SHADE);

	wwin->flags.skip_next_animation = 0;
	wwin->flags.shaded = 1;
	wwin->flags.mapped = 0;
	/* prevent window withdrawal when getting UnmapNotify */
	xcb_change_window_attributes(wdisp->conn, wwin->client_win, XCB_CW_EVENT_MASK, (const uint32_t[]){ wwin->event_mask & ~XCB_EVENT_MASK_STRUCTURE_NOTIFY });
	xcb_unmap_window(wdisp->conn, wwin->client_win);
	xcb_change_window_attributes(wdisp->conn, wwin->client_win, XCB_CW_EVENT_MASK, (const uint32_t[]){ wwin->event_mask });

	/* for the client it's just like iconification */
	wFrameWindowResize(wwin->frame, wwin->frame->core->width, wwin->frame->top_width - 1);

	wwin->client.y = wwin->frame_y - wwin->client.height + wwin->frame->top_width;
	wWindowSynthConfigureNotify(wwin);

	/*
	   wClientSetState(wwin, IconicState, None);
	 */

	WMPostNotificationName(WMNChangedState, wwin, "shade");

	if (wwin->tab_group)
		wGroupSyncShade(wwin);

#ifdef USE_ANIMATIONS
	if (!wwin->screen_ptr->flags.startup) {
		/* Catch up with events not processed while animation was running */
		ProcessPendingEvents();
	}
#endif
}

void wUnshadeWindow(WWindow *wwin)
{
	int il, ir, it, ib;
	int iv;

	if (!wwin->flags.shaded)
		return;

	wwin->flags.shaded = 0;
	wwin->flags.mapped = 1;
	xcb_map_window(wdisp->conn, wwin->client_win);
	wSoundPlay(wwin->screen_ptr, WSOUND_UNSHADE);

	shade_animate(wwin, UNSHADE);

	wwin->flags.skip_next_animation = 0;
	wFrameInnerBorderWidths(wwin, &il, &ir, &it, &ib);
	iv = it + ib;
	wFrameWindowResize(wwin->frame, wwin->frame->core->width,
				wwin->frame->top_width + wwin->client.height
				+ wwin->frame->bottom_width + iv);
	xcb_configure_window(wdisp->conn, wwin->client_win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ il, wwin->frame->top_width + it });
	wFrameInnerBorderClientOrigin(wwin, wwin->frame_x, wwin->frame_y,
				      &wwin->client.x, &wwin->client.y);

	wWindowSynthConfigureNotify(wwin);

	/* if the window is focused, set the focus again as it was disabled during
	 * shading */
	if (wwin->flags.focused)
		wSetFocusTo(wwin->screen_ptr, wwin);

	WMPostNotificationName(WMNChangedState, wwin, "shade");

	if (wwin->tab_group)
		wGroupSyncShade(wwin);
}

/* Set the old coordinates using the current values */
void save_old_geometry(WWindow *wwin, int directions)
{
	/* never been saved? */
	if (!wwin->old_geometry.width)
		directions |= SAVE_GEOMETRY_X | SAVE_GEOMETRY_WIDTH;
	if (!wwin->old_geometry.height)
		directions |= SAVE_GEOMETRY_Y | SAVE_GEOMETRY_HEIGHT;

	if (directions & SAVE_GEOMETRY_X)
		wwin->old_geometry.x = wwin->frame_x;
	if (directions & SAVE_GEOMETRY_Y)
		wwin->old_geometry.y = wwin->frame_y;
	if (directions & SAVE_GEOMETRY_WIDTH)
		wwin->old_geometry.width = wwin->client.width;
	if (directions & SAVE_GEOMETRY_HEIGHT)
		wwin->old_geometry.height = wwin->client.height;
}

static void remember_geometry(WWindow *wwin, int *x, int *y, int *w, int *h)
{
	WMRect old_geom_rect;
	int old_head;
	bool same_head;

	old_geom_rect = wmkrect(wwin->old_geometry.x, wwin->old_geometry.y, wwin->old_geometry.width, wwin->old_geometry.height);
	old_head = wGetHeadForRect(wwin->screen_ptr, old_geom_rect);
	same_head = (wGetHeadForWindow(wwin) == old_head);
	*x = ((wwin->old_geometry.x || wwin->old_geometry.width) && same_head) ? wwin->old_geometry.x : wwin->frame_x;
	*y = ((wwin->old_geometry.y || wwin->old_geometry.height) && same_head) ? wwin->old_geometry.y : wwin->frame_y;
	*w = wwin->old_geometry.width ? wwin->old_geometry.width : wwin->client.width;
	*h = wwin->old_geometry.height ? wwin->old_geometry.height : wwin->client.height;
}

/* Remember geometry for unmaximizing */
void update_saved_geometry(WWindow *wwin)
{
	/* NOT if we aren't already maximized
	 * we'll save geometry when maximizing */
	if (!wwin->flags.maximized)
		return;

	/* NOT if we are fully maximized */
	if ((wwin->flags.maximized & MAX_MAXIMUS) ||
	    ((wwin->flags.maximized & MAX_HORIZONTAL) &&
	    (wwin->flags.maximized & MAX_VERTICAL)))
		return;

	/* save the co-ordinate in the axis in which we AREN'T maximized */
	if (wwin->flags.maximized & MAX_HORIZONTAL)
		save_old_geometry(wwin, SAVE_GEOMETRY_Y);
	if (wwin->flags.maximized & MAX_VERTICAL)
		save_old_geometry(wwin, SAVE_GEOMETRY_X);
}

void wMaximizeWindow(WWindow *wwin, int directions, int head)
{
	unsigned int new_width = 0, new_height = 0, half_scr_width, half_scr_height;
	int new_x = 0;
	int new_y = 0;
	int maximus_x = 0;
	int maximus_y = 0;
	unsigned int maximus_width = 0;
	unsigned int maximus_height = 0;
	WArea usableArea, totalArea;
	bool has_border = 1;
	int adj_size;
	WScreen *scr = wwin->screen_ptr;

	if (!IS_RESIZABLE(wwin))
		return;

	if (!HAS_BORDER(wwin))
		has_border = 0;

	if (w_global.config->drag_maximized_window == DRAGMAX_NOMOVE)
		wwin->client_flags.no_movable = 1;

	/* the size to adjust the geometry */
	adj_size = wFrameBorderWidth(wwin) * 2 * has_border;

	/* save old coordinates before we change the current values */
	if (!wwin->flags.maximized)
		save_old_geometry(wwin, SAVE_GEOMETRY_ALL);

	totalArea.x2 = scr->scr_width;
	totalArea.y2 = scr->scr_height;
	totalArea.x1 = 0;
	totalArea.y1 = 0;

	/* In case of mouse initiated maximize, use the head in which pointer is
	   located, rather than window position, which is passed to the function */
	if (!(directions & MAX_IGNORE_XINERAMA) && !(directions & MAX_KEYBOARD)) {
		WScreen *scr = wwin->screen_ptr;
		head = wGetHeadForPointerLocation(scr);
	}

	usableArea = wGetUsableAreaForHead(scr, head, &totalArea, true);

	/* Only save directions, not kbd or xinerama hints */
	directions &= (MAX_HORIZONTAL | MAX_VERTICAL | MAX_LEFTHALF | MAX_RIGHTHALF | MAX_TOPHALF | MAX_BOTTOMHALF | MAX_MAXIMUS | MAX_CENTRAL);

	if (WFLAGP(wwin, full_maximize))
		usableArea = totalArea;
	half_scr_width = (usableArea.x2 - usableArea.x1)/2;
	half_scr_height = (usableArea.y2 - usableArea.y1)/2;

	if (wwin->flags.shaded) {
		wwin->flags.skip_next_animation = 1;
		wUnshadeWindow(wwin);
	}

	if (directions & MAX_MAXIMUS) {
		find_maximus_geometry(wwin, usableArea, &maximus_x, &maximus_y, &maximus_width, &maximus_height);
		new_width = maximus_width - adj_size;
		new_height = maximus_height - adj_size;
		new_x = maximus_x;
		new_y = maximus_y;
		if (WFLAGP(wwin, full_maximize) && (new_y == 0)) {
			new_height += wwin->frame->bottom_width - 1;
			new_y -= wwin->frame->top_width;
		}

		wwin->maximus_x = new_x;
		wwin->maximus_y = new_y;
		wwin->flags.old_maximized |= MAX_MAXIMUS;
	} else {
		/* center the window if can fit, if not sticking it to the screen edges */
		if (directions & MAX_CENTRAL) {
			if (wwin->frame->core->height > (usableArea.y2 - usableArea.y1)) {
				new_y = usableArea.y1;
				new_height = usableArea.y2 - usableArea.y1 - adj_size;
			} else {
				new_height = (wwin->old_geometry.height) ? wwin->old_geometry.height : wwin->frame->core->height;
				new_height += wwin->frame->top_width + wwin->frame->bottom_width;
				new_y = half_scr_height - new_height / 2;
			}
			if (wwin->frame->core->width > (usableArea.x2 - usableArea.x1)) {
				new_x = usableArea.x1;
				new_width = usableArea.x2 - usableArea.x1 - adj_size;
			}
			else {
				new_width = (wwin->old_geometry.width) ? wwin->old_geometry.width : wwin->frame->core->width;
				new_x = half_scr_width - new_width / 2;
			}
		}
		/* set default values if no option set then */
		if (!(directions & (MAX_HORIZONTAL | MAX_LEFTHALF | MAX_RIGHTHALF | MAX_MAXIMUS | MAX_CENTRAL))) {
			new_width = (wwin->old_geometry.width) ? wwin->old_geometry.width : wwin->frame->core->width;
			new_x = (wwin->old_geometry.x) ? wwin->old_geometry.x : wwin->frame_x;
		}
		if (!(directions & (MAX_VERTICAL | MAX_TOPHALF | MAX_BOTTOMHALF | MAX_MAXIMUS| MAX_CENTRAL))) {
			new_height = (wwin->old_geometry.height) ? wwin->old_geometry.height : wwin->frame->core->height;
			new_y = (wwin->old_geometry.y) ? wwin->old_geometry.y : wwin->frame_y;
		}

		/* left|right position */
		if (directions & MAX_LEFTHALF) {
			new_width = half_scr_width - adj_size;
			new_x = usableArea.x1;
			if (directions & MAX_CENTRAL) {
				new_y = half_scr_height - wwin->old_geometry.height / 2;
			}
		} else if (directions & MAX_RIGHTHALF) {
			new_width = half_scr_width - adj_size;
			new_x = usableArea.x1 + half_scr_width;
			if (directions & MAX_CENTRAL)
				new_y = half_scr_height - wwin->old_geometry.height / 2;
		}
		/* top|bottom position */
		if (directions & MAX_TOPHALF) {
			new_height = half_scr_height - adj_size;
			new_y = usableArea.y1;
			if (directions & MAX_CENTRAL)
				new_x = half_scr_width - wwin->old_geometry.width / 2;
		} else if (directions & MAX_BOTTOMHALF) {
			new_height = half_scr_height - adj_size;
			new_y = usableArea.y1 + half_scr_height;
			if (directions & MAX_CENTRAL)
				new_x = half_scr_width - wwin->old_geometry.width / 2;
		}

		/* vertical|horizontal position */
		if (directions & MAX_HORIZONTAL) {
			new_width = usableArea.x2 - usableArea.x1 - adj_size;
			new_x = usableArea.x1;
		}
		if (directions & MAX_VERTICAL) {
			new_height = usableArea.y2 - usableArea.y1 - adj_size;
			new_y = usableArea.y1;
			if (WFLAGP(wwin, full_maximize) && (new_y == 0))
				new_y -= wwin->frame->top_width;
		}
	}

	if (!WFLAGP(wwin, full_maximize) && !(directions == MAX_MAXIMUS || directions == MAX_HORIZONTAL))
		new_height -= wwin->frame->top_width + wwin->frame->bottom_width;

	/* set maximization state */
	wwin->flags.maximized = directions;
	if ((wwin->flags.old_maximized & MAX_MAXIMUS) && !wwin->flags.maximized)
		wwin->flags.maximized = MAX_MAXIMUS;

	wWindowUpdateButtonImages(wwin);

	wWindowConstrainSize(wwin, &new_width, &new_height);

	wWindowCropSize(wwin, usableArea.x2 - usableArea.x1,
			usableArea.y2 - usableArea.y1, &new_width, &new_height);

	wWindowConfigure(wwin, new_x, new_y, new_width, new_height);
	wWindowSynthConfigureNotify(wwin);

	WMPostNotificationName(WMNChangedState, wwin, "maximize");
	wSoundPlay(wwin->screen_ptr, WSOUND_MAXIMIZE);

	if (wwin->tab_group)
		wGroupSyncMaximize(wwin);
}

/* generic (un)maximizer */
void handleMaximize(WWindow *wwin, int directions)
{
	int current = wwin->flags.maximized;
	int requested = directions & (MAX_HORIZONTAL | MAX_VERTICAL | MAX_LEFTHALF | MAX_RIGHTHALF | MAX_TOPHALF | MAX_BOTTOMHALF | MAX_MAXIMUS | MAX_CENTRAL);
	int effective = requested ^ current;
	int flags = directions & ~requested;
	int head = wGetHeadForWindow(wwin);
	int dest_head = -1;

	if (!effective) {
		/* allow wMaximizeWindow to restore the Maximusized size */
		if ((wwin->flags.old_maximized & MAX_MAXIMUS) &&
				!(requested & MAX_MAXIMUS))
			wMaximizeWindow(wwin, MAX_MAXIMUS | flags, head);

		else if (w_global.config->alt_half_maximize &&
				current & MAX_HORIZONTAL && current & MAX_VERTICAL &&
				requested & MAX_HORIZONTAL && requested & MAX_VERTICAL)
			wUnmaximizeWindow(wwin);

		/* Apply for window state, which is only horizontally or vertically
		 * maximized. Quarters cannot be handled here, since there is not clear
		 * on which direction user intend to move such window. */
		else if (w_global.config->move_half_max_between_heads &&
				current & (MAX_VERTICAL | MAX_HORIZONTAL)) {
			if (requested & MAX_LEFTHALF && current & MAX_LEFTHALF) {
				dest_head = wGetHeadRelativeToCurrentHead(wwin->screen_ptr,
						head, DIRECTION_LEFT);
				if (dest_head != -1) {
					effective |= MAX_RIGHTHALF;
					effective |= MAX_VERTICAL;
					effective &= ~(MAX_HORIZONTAL | MAX_LEFTHALF);
				}
			} else if (requested & MAX_RIGHTHALF &&
					current & MAX_RIGHTHALF) {
				dest_head = wGetHeadRelativeToCurrentHead(wwin->screen_ptr,
						head, DIRECTION_RIGHT);
				if (dest_head != -1) {
					effective |= MAX_LEFTHALF;
					effective |= MAX_VERTICAL;
					effective &= ~(MAX_HORIZONTAL | MAX_RIGHTHALF);
				}
			} else if (requested & MAX_TOPHALF && current & MAX_TOPHALF) {
				dest_head = wGetHeadRelativeToCurrentHead(wwin->screen_ptr,
						head, DIRECTION_UP);
				if (dest_head != -1) {
					effective |= MAX_BOTTOMHALF;
					effective |= MAX_HORIZONTAL;
					effective &= ~(MAX_VERTICAL | MAX_TOPHALF);
				}
			} else if (requested & MAX_BOTTOMHALF &&
					current & MAX_BOTTOMHALF) {
				dest_head = wGetHeadRelativeToCurrentHead(wwin->screen_ptr,
						head, DIRECTION_DOWN);
				if (dest_head != -1) {
					effective |= MAX_TOPHALF;
					effective |= MAX_HORIZONTAL;
					effective &= ~(MAX_VERTICAL | MAX_BOTTOMHALF);
				}
			}
		}

		if (dest_head != -1)
			/* tell wMaximizeWindow that we were using keyboard, not mouse,
			 * so that it will use calculated head as destination for
			 * move_half_max_between_heads feature, not from mouse pointer */
			wMaximizeWindow(wwin, (effective | flags | MAX_KEYBOARD),
					dest_head);
		else if (!w_global.config->alt_half_maximize)
			wUnmaximizeWindow(wwin);

		return;
	}

	/* these alone mean vertical|horizontal toggle */
	if ((effective == MAX_LEFTHALF) ||
			(effective == MAX_RIGHTHALF) ||
			(effective == MAX_TOPHALF) ||
			(effective == MAX_BOTTOMHALF))
		wUnmaximizeWindow(wwin);

	/* Following conditions might look complicated, but they are really simple:
	 * allow fullscreen transition only for half maximized state (and not
	 * corners) and only when requested state is also half maximized, but on
	 * opposite side of the screen. As for corners, it is similar, but
	 * expected is that only quarter maximized windows on corner can change
	 * its state to half maximized window, depending on direction. Note, that
	 * MAX_KEYBOARD is passed to the wMaximizeWindow function, to preserve the
	 * head, even if mouse was used for triggering the action. */

	/* Quarters alternative transition. */
	else if (w_global.config->alt_half_maximize &&
			((requested & MAX_LEFTHALF && requested & MAX_TOPHALF &&
			  current & MAX_RIGHTHALF && current & MAX_TOPHALF) ||
			(requested & MAX_RIGHTHALF && requested & MAX_TOPHALF &&
			  current & MAX_LEFTHALF && current & MAX_TOPHALF)))
		wMaximizeWindow(wwin, (MAX_TOPHALF | MAX_HORIZONTAL | MAX_KEYBOARD),
				head);
	else if (w_global.config->alt_half_maximize &&
			((requested & MAX_LEFTHALF && requested & MAX_BOTTOMHALF &&
			 current & MAX_RIGHTHALF && current & MAX_BOTTOMHALF) ||
			(requested & MAX_RIGHTHALF && requested & MAX_BOTTOMHALF &&
			 current & MAX_LEFTHALF && current & MAX_BOTTOMHALF)))
		wMaximizeWindow(wwin, (MAX_BOTTOMHALF | MAX_HORIZONTAL | MAX_KEYBOARD),
				head);
	else if (w_global.config->alt_half_maximize &&
			((requested & MAX_LEFTHALF && requested & MAX_BOTTOMHALF &&
			 current & MAX_LEFTHALF && current & MAX_TOPHALF) ||
			(requested & MAX_LEFTHALF && requested & MAX_TOPHALF &&
			 current & MAX_LEFTHALF && current & MAX_BOTTOMHALF)))
		wMaximizeWindow(wwin, (MAX_LEFTHALF | MAX_VERTICAL| MAX_KEYBOARD),
				head);
	else if (w_global.config->alt_half_maximize &&
			((requested & MAX_RIGHTHALF && requested & MAX_BOTTOMHALF &&
			 current & MAX_RIGHTHALF && current & MAX_TOPHALF) ||
			(requested & MAX_RIGHTHALF && requested & MAX_TOPHALF &&
			 current & MAX_RIGHTHALF && current & MAX_BOTTOMHALF)))
		wMaximizeWindow(wwin, (MAX_RIGHTHALF | MAX_VERTICAL| MAX_KEYBOARD),
				head);

	/* Half-maximized alternative transition */
	else if (w_global.config->alt_half_maximize && (
				(requested & MAX_LEFTHALF && requested & MAX_VERTICAL &&
				 current & MAX_RIGHTHALF && current & MAX_VERTICAL) ||
				(requested & MAX_RIGHTHALF && requested & MAX_VERTICAL &&
				 current & MAX_LEFTHALF && current & MAX_VERTICAL) ||
				(requested & MAX_TOPHALF && requested & MAX_HORIZONTAL &&
				 current & MAX_BOTTOMHALF && current & MAX_HORIZONTAL) ||
				(requested & MAX_BOTTOMHALF && requested & MAX_HORIZONTAL &&
				 current & MAX_TOPHALF && current & MAX_HORIZONTAL)))
		wMaximizeWindow(wwin, (MAX_HORIZONTAL|MAX_VERTICAL|MAX_KEYBOARD),
				head);

	else {

		if ((requested == (MAX_HORIZONTAL | MAX_VERTICAL)) ||
				(requested == MAX_MAXIMUS))
			effective = requested;
		else {
			if (requested & MAX_CENTRAL) {
				effective |= MAX_CENTRAL;
				if (current & (MAX_HORIZONTAL | MAX_VERTICAL))
					effective &= ~(MAX_HORIZONTAL | MAX_VERTICAL);
				else if (current & MAX_TOPHALF && current & MAX_LEFTHALF)
					effective &= ~(MAX_TOPHALF | MAX_LEFTHALF);
				else if (current & MAX_TOPHALF && current & MAX_RIGHTHALF)
					effective &= ~(MAX_TOPHALF | MAX_RIGHTHALF);
				else if (current & MAX_BOTTOMHALF && current & MAX_LEFTHALF)
					effective &= ~(MAX_BOTTOMHALF | MAX_LEFTHALF);
				else if (current & MAX_BOTTOMHALF && current & MAX_RIGHTHALF)
					effective &= ~(MAX_BOTTOMHALF | MAX_RIGHTHALF);
			} else if (requested & MAX_LEFTHALF) {
				if (!(requested & (MAX_TOPHALF | MAX_BOTTOMHALF)))
					effective |= MAX_VERTICAL;
				else
					effective |= requested & (MAX_TOPHALF | MAX_BOTTOMHALF);
				effective |= MAX_LEFTHALF;
				effective &= ~(MAX_HORIZONTAL | MAX_RIGHTHALF);
			} else if (requested & MAX_RIGHTHALF) {
				if (!(requested & (MAX_TOPHALF | MAX_BOTTOMHALF)))
					effective |= MAX_VERTICAL;
				else
					effective |= requested & (MAX_TOPHALF | MAX_BOTTOMHALF);
				effective |= MAX_RIGHTHALF;
				effective &= ~(MAX_HORIZONTAL | MAX_LEFTHALF);
			}
			if (requested & MAX_TOPHALF) {
				if (!(requested & (MAX_LEFTHALF | MAX_RIGHTHALF)))
					effective |= MAX_HORIZONTAL;
				else
					effective |= requested & (MAX_LEFTHALF | MAX_RIGHTHALF);
				effective |= MAX_TOPHALF;
				effective &= ~(MAX_VERTICAL | MAX_BOTTOMHALF);
			} else if (requested & MAX_BOTTOMHALF) {
				if (!(requested & (MAX_LEFTHALF | MAX_RIGHTHALF)))
					effective |= MAX_HORIZONTAL;
				else
					effective |= requested & (MAX_LEFTHALF | MAX_RIGHTHALF);
				effective |= MAX_BOTTOMHALF;
				effective &= ~(MAX_VERTICAL | MAX_TOPHALF);
			}
			if (requested & MAX_HORIZONTAL)
				effective &= ~(MAX_LEFTHALF | MAX_RIGHTHALF);
			if (requested & MAX_VERTICAL)
				effective &= ~(MAX_TOPHALF | MAX_BOTTOMHALF);
			effective &= ~MAX_MAXIMUS;
		}
		wMaximizeWindow(wwin, effective | flags, head);
	}
}

/* Moving window between heads
 *
 * Window is moved between all avaiable heads for current screen in
 * requested direction.
 *
 * If heads have different sizes, window position will be adjusted to be inside
 * screen. Window will be unmaximized on move, so that it can be immediately be
 * maximixed by another shortcut.
 *
 *
 * direction = 0 means move clock-wise from 12h position to 6h
 *               (first try moving right, then bottom, then left, then up)
 * direction = 1 means move clock-wise from 6h position to 12h
 *               (first try moving left, then up, then right, then bottom)
 *
 *
 * As side effect this mean if you have 9 monitors - you will not be able to
 * move window into central cell without mouse.
 *
 * For common cases with 1 or 2 extra monitors this should work just fine.
 *
 * */

void moveBetweenHeads(WWindow *wwin, int direction)
{
	int head = wGetHeadForWindow(wwin);
	int destHead = -1;

	unsigned int new_width, new_height;
	int offsetX, newX;
	int offsetY, newY;
	WArea totalArea, oldHeadArea, destHeadArea;
	WScreen *scr = wwin->screen_ptr;

	int try_movements[2][4] = {
		{DIRECTION_RIGHT, DIRECTION_DOWN, DIRECTION_LEFT, DIRECTION_UP},
		{DIRECTION_LEFT, DIRECTION_UP, DIRECTION_RIGHT, DIRECTION_DOWN}
	};

	/* loop through directions array and try movements until one works */
	for (int try_movement_idx = 0;
			destHead == -1 && try_movement_idx < 4; try_movement_idx++) {
			destHead = wGetHeadRelativeToCurrentHead(wwin->screen_ptr,
					head, try_movements[direction][try_movement_idx]);
	}

	if (destHead != -1) {
		totalArea.x1 = 0;
		totalArea.y1 = 0;
		totalArea.x2 = scr->scr_width;
		totalArea.y2 = scr->scr_height;

		oldHeadArea = wGetUsableAreaForHead(scr, head, &totalArea, true);
		destHeadArea = wGetUsableAreaForHead(scr, destHead, &totalArea, true);

		offsetX = wwin->frame_x - oldHeadArea.x1;
		offsetY = wwin->frame_y - oldHeadArea.y1;

		newX = destHeadArea.x1 + offsetX;
		newY = destHeadArea.y1 + offsetY;

		new_width = wwin->client.width;
		new_height = wwin->client.height;

		/* try to brind window inside head coordinates */
		if (newX > destHeadArea.x2)
			newX = destHeadArea.x2 - wwin->client.width;

		if (newX < destHeadArea.x1)
			newX = destHeadArea.x1;

		if (newY > destHeadArea.y2)
			newY = destHeadArea.y2 - wwin->client.height;

		if (newY < destHeadArea.y1)
			newY = destHeadArea.y1;

		/* unset maximization state */
		wwin->flags.maximized = 0;
		wWindowConfigure(wwin, newX, newY, new_width, new_height);
	}

	return;
}

void wMoveToEdge(WWindow *wwin, int direction, bool snap_to_windows)
{
	WScreen *scr = wwin->screen_ptr;
	int head = wGetHeadForWindow(wwin);
	WArea total, usable;
	int gap = w_global.config->window_gap;
	int x = wwin->frame_x;
	int y = wwin->frame_y;
	int w = wwin->frame->core->width +
		(HAS_BORDER(wwin) ? 2 * wFrameBorderWidth(wwin) : 0);
	int h = wwin->frame->core->height +
		(HAS_BORDER(wwin) ? 2 * wFrameBorderWidth(wwin) : 0);
	int target, new_x = x, new_y = y;
	int dest_head;
	WArea dest_usable, dest_total;

	if (!IS_MOVABLE(wwin) || wwin->flags.shaded
	    || wwin->flags.fullscreen || wwin->flags.maximized)
		return;

	usable = wGetUsableAreaForHead(scr, head, &total, true);

	switch (direction) {
	case DIRECTION_LEFT:
		target = usable.x1;
		if (usable.x1 >= x)
			goto cross_head;
		new_x = target;
		break;
	case DIRECTION_RIGHT:
		target = usable.x2 - w;
		if (usable.x2 - w <= x)
			goto cross_head;
		new_x = target;
		break;
	case DIRECTION_UP:
		target = usable.y1;
		if (usable.y1 >= y)
			goto cross_head;
		new_y = target;
		break;
	case DIRECTION_DOWN:
		target = usable.y2 - h;
		if (usable.y2 - h <= y)
			goto cross_head;
		new_y = target;
		break;
	default:
		return;
	}

	if (snap_to_windows) {
		int best;
		int from_off, to_off, alo, ahi;

		if (direction == DIRECTION_LEFT) {
			from_off = x;
			to_off = usable.x1;
			alo = y;
			ahi = y + h - 1;
		} else if (direction == DIRECTION_RIGHT) {
			from_off = x + w;
			to_off = usable.x2;
			alo = y;
			ahi = y + h - 1;
		} else if (direction == DIRECTION_UP) {
			from_off = y;
			to_off = usable.y1;
			alo = x;
			ahi = x + w - 1;
		} else {
			from_off = y + h;
			to_off = usable.y2;
			alo = x;
			ahi = x + w - 1;
		}

		if (find_nearest_edge(wwin, head, scr->current_workspace, direction, from_off, to_off, alo, ahi, INT_MAX, gap, gap, &best)) {
			if (direction == DIRECTION_LEFT)
				new_x = best;
			else if (direction == DIRECTION_RIGHT)
				new_x = best - w;
			else if (direction == DIRECTION_UP)
				new_y = best;
			else
				new_y = best - h;
		}
	}

	wWindowMove(wwin, new_x, new_y);
	WMPostNotificationName(WMNChangedState, wwin, "move");
	return;

cross_head:
	dest_head = wGetHeadRelativeToCurrentHead(scr, head, direction);

	if (dest_head < 0 || dest_head == head)
		return;

	dest_usable = wGetUsableAreaForHead(scr, dest_head, &dest_total, true);

	switch (direction) {
	case DIRECTION_LEFT:
		new_x = dest_usable.x2 - w;
		break;
	case DIRECTION_RIGHT:
		new_x = dest_usable.x1;
		break;
	case DIRECTION_UP:
		new_y = dest_usable.y2 - h;
		break;
	case DIRECTION_DOWN:
		new_y = dest_usable.y1;
		break;
	}

	if (new_x > dest_usable.x2 - w)
		new_x = dest_usable.x2 - w;
	if (new_x < dest_usable.x1)
		new_x = WMAX(dest_usable.x1, dest_usable.x2 - w);
	if (new_y > dest_usable.y2 - h)
		new_y = dest_usable.y2 - h;
	if (new_y < dest_usable.y1)
		new_y = WMAX(dest_usable.y1, dest_usable.y2 - h);

	wWindowMove(wwin, new_x, new_y);
	WMPostNotificationName(WMNChangedState, wwin, "move");
}

void wGrowToEdge(WWindow *wwin, int direction)
{
	WScreen *scr = wwin->screen_ptr;
	int head = wGetHeadForWindow(wwin);
	WArea total, usable;
	int gap = w_global.config->window_gap;
	int x = wwin->frame_x;
	int y = wwin->frame_y;
	int fbw = HAS_BORDER(wwin) ? wFrameBorderWidth(wwin) : 0;
	int w = wwin->frame->core->width + 2 * fbw;
	int h = wwin->frame->core->height + 2 * fbw;
	int new_x = x, new_y = y, new_w = w, new_h = h;
	int il, ir, it, ib, ih, iv;
	unsigned int cw, ch;
	int best;

	if (!IS_RESIZABLE(wwin) || wwin->flags.fullscreen || wwin->flags.maximized)
		return;
	if (wwin->flags.shaded)
		wUnshadeWindow(wwin);

	usable = wGetUsableAreaForHead(scr, head, &total, true);
	wFrameInnerBorderWidths(wwin, &il, &ir, &it, &ib);
	ih = il + ir;
	iv = it + ib;

	switch (direction) {
	case DIRECTION_LEFT:
		new_x = usable.x1;
		new_w = x + w - new_x;
		break;
	case DIRECTION_RIGHT:
		new_w = usable.x2 - x;
		break;
	case DIRECTION_UP:
		new_y = usable.y1;
		new_h = y + h - new_y;
		break;
	case DIRECTION_DOWN:
		new_h = usable.y2 - y;
		break;
	default:
		return;
	}

	if ((direction == DIRECTION_LEFT || direction == DIRECTION_RIGHT) && new_w <= w)
		return;
	if ((direction == DIRECTION_UP || direction == DIRECTION_DOWN) && new_h <= h)
		return;

	if (direction == DIRECTION_LEFT) {
		if (!find_nearest_edge(wwin, head, scr->current_workspace, DIRECTION_LEFT, x, usable.x1, y, y + h - 1, INT_MAX, gap, gap, &best))
			return;
		new_x = best;
		new_w = x + w - new_x;
	} else if (direction == DIRECTION_RIGHT) {
		if (!find_nearest_edge(wwin, head, scr->current_workspace, DIRECTION_RIGHT, x + w, usable.x2, y, y + h - 1, INT_MAX, gap, gap, &best))
			return;
		new_w = best - x;
	} else if (direction == DIRECTION_UP) {
		if (!find_nearest_edge(wwin, head, scr->current_workspace, DIRECTION_UP, y, usable.y1, x, x + w - 1, INT_MAX, gap, gap, &best))
			return;
		new_y = best;
		new_h = y + h - new_y;
	} else {
		if (!find_nearest_edge(wwin, head, scr->current_workspace, DIRECTION_DOWN, y + h, usable.y2, x, x + w - 1, INT_MAX, gap, gap, &best))
			return;
		new_h = best - y;
	}

	if ((direction == DIRECTION_LEFT || direction == DIRECTION_RIGHT) && new_w <= w)
		return;
	if ((direction == DIRECTION_UP || direction == DIRECTION_DOWN) && new_h <= h)
		return;

	cw = new_w - ih - 2 * fbw;
	ch = new_h - wwin->frame->top_width - wwin->frame->bottom_width - iv - 2 * fbw;

	wWindowConstrainSize(wwin, &cw, &ch);
	wWindowCropSize(wwin, usable.x2 - usable.x1 - ih - 2 * fbw, usable.y2 - usable.y1 - wwin->frame->top_width - wwin->frame->bottom_width - iv - 2 * fbw, &cw, &ch);

	if (direction == DIRECTION_LEFT)
		new_x = x + w - (cw + ih + 2 * fbw);
	else if (direction == DIRECTION_UP)
		new_y = y + h - (ch + wwin->frame->top_width + wwin->frame->bottom_width + iv + 2 * fbw);

	if ((int)(cw + ih + 2 * fbw) == w && (int)(ch + wwin->frame->top_width + wwin->frame->bottom_width + iv + 2 * fbw) == h)
		return;

	wWindowConfigure(wwin, new_x, new_y, cw, ch);
	wWindowSynthConfigureNotify(wwin);
	WMPostNotificationName(WMNChangedState, wwin, "grow");
	if (wwin->tab_group)
		wGroupSyncMaximize(wwin);
}

void wShrinkToEdge(WWindow *wwin, int direction)
{
	WScreen *scr = wwin->screen_ptr;
	int head = wGetHeadForWindow(wwin);
	int gap = w_global.config->window_gap;
	int x = wwin->frame_x;
	int y = wwin->frame_y;
	int fbw = HAS_BORDER(wwin) ? wFrameBorderWidth(wwin) : 0;
	int w = wwin->frame->core->width + 2 * fbw;
	int h = wwin->frame->core->height + 2 * fbw;
	int new_x = x, new_y = y, new_w = w, new_h = h;
	int il, ir, it, ib, ih, iv;
	unsigned int cw, ch, min_cw, min_ch;
	int best;

	if (!IS_RESIZABLE(wwin) || wwin->flags.fullscreen || wwin->flags.maximized)
		return;
	if (wwin->flags.shaded)
		wUnshadeWindow(wwin);

	wFrameInnerBorderWidths(wwin, &il, &ir, &it, &ib);
	ih = il + ir;
	iv = it + ib;

	if (direction == DIRECTION_LEFT || direction == DIRECTION_RIGHT) {
		min_cw = 1;
		min_ch = h - wwin->frame->top_width - wwin->frame->bottom_width - iv - 2 * fbw;
		wWindowConstrainSize(wwin, &min_cw, &min_ch);
		new_w = w / 2;
		if (new_w < (int)(min_cw + ih + 2 * fbw))
			new_w = min_cw + ih + 2 * fbw;
	} else {
		min_cw = w - ih - 2 * fbw;
		min_ch = 1;
		wWindowConstrainSize(wwin, &min_cw, &min_ch);
		new_h = h / 2;
		if (new_h < (int)(min_ch + wwin->frame->top_width + wwin->frame->bottom_width + iv + 2 * fbw))
			new_h = min_ch + wwin->frame->top_width + wwin->frame->bottom_width + iv + 2 * fbw;
	}

	switch (direction) {
	case DIRECTION_LEFT:
		if (find_nearest_edge(wwin, head, scr->current_workspace, DIRECTION_LEFT, x + w, x + new_w, y, y + h - 1, INT_MAX, gap, 0, &best))
			new_w = best - x;
		break;
	case DIRECTION_RIGHT:
		new_x = x + w - new_w;
		if (find_nearest_edge(wwin, head, scr->current_workspace, DIRECTION_RIGHT, x, new_x, y, y + h - 1, INT_MAX, gap, 0, &best))
			new_w = x + w - best;
		break;
	case DIRECTION_UP:
		if (find_nearest_edge(wwin, head, scr->current_workspace, DIRECTION_UP, y + h, y + new_h, x, x + w - 1, INT_MAX, gap, 0, &best))
			new_h = best - y;
		break;
	case DIRECTION_DOWN:
		new_y = y + h - new_h;
		if (find_nearest_edge(wwin, head, scr->current_workspace, DIRECTION_DOWN, y, new_y, x, x + w - 1, INT_MAX, gap, 0, &best))
			new_h = y + h - best;
		break;
	default:
		return;
	}

	if ((direction == DIRECTION_LEFT || direction == DIRECTION_RIGHT) && new_w >= w)
		return;
	if ((direction == DIRECTION_UP || direction == DIRECTION_DOWN) && new_h >= h)
		return;

	cw = new_w - ih - 2 * fbw;
	ch = new_h - wwin->frame->top_width - wwin->frame->bottom_width - iv - 2 * fbw;

	wWindowConstrainSize(wwin, &cw, &ch);

	if (direction == DIRECTION_RIGHT)
		new_x = x + w - (cw + ih + 2 * fbw);
	else if (direction == DIRECTION_DOWN)
		new_y = y + h - (ch + wwin->frame->top_width + wwin->frame->bottom_width + iv + 2 * fbw);

	if ((int)(cw + ih + 2 * fbw) == w && (int)(ch + wwin->frame->top_width + wwin->frame->bottom_width + iv + 2 * fbw) == h)
		return;

	wWindowConfigure(wwin, new_x, new_y, cw, ch);
	wWindowSynthConfigureNotify(wwin);
	WMPostNotificationName(WMNChangedState, wwin, "shrink");
	if (wwin->tab_group)
		wGroupSyncMaximize(wwin);
}

/* the window boundary coordinates */
typedef struct {
	int left;
	int right;
	int bottom;
	int top;
	int width;
	int height;
} win_coords;

static void set_window_coords(WWindow *wwin, win_coords *obs)
{
	obs->left = wwin->frame_x;
	obs->top = wwin->frame_y;
	obs->width = wwin->frame->core->width;
	obs->height = wwin->frame->core->height;
	obs->bottom = obs->top + obs->height;
	obs->right = obs->left + obs->width;
}

/*
 * Maximus: tiled maximization (maximize without overlapping other windows)
 *
 * The original window 'orig' will be maximized to new coordinates 'new'.
 * The windows obstructing the maximization of 'orig' are denoted 'obs'.
 */
static void find_maximus_geometry(WWindow *wwin, WArea usableArea, int *new_x, int *new_y,
				  unsigned int *new_width, unsigned int *new_height)
{
	WWindow *tmp;
	short int tbar_height_0 = 0, rbar_height_0 = 0, bd_width_0 = 0;
	short int adjust_height;
	int x_intsect, y_intsect;
	/* the obstructing, original and new windows */
	win_coords obs, orig, new;

	/* set the original coordinate positions of the window to be Maximumized */
	if (wwin->flags.maximized) {
		/* window is already maximized; consider original geometry */
		remember_geometry(wwin, &orig.left, &orig.top, &orig.width, &orig.height);
		orig.bottom = orig.top + orig.height;
		orig.right = orig.left + orig.width;
	} else
		set_window_coords(wwin, &orig);

	/* Try to fully maximize first, then readjust later */
	new.left    = usableArea.x1;
	new.right   = usableArea.x2;
	new.top     = usableArea.y1;
	new.bottom  = usableArea.y2;

	if (HAS_TITLEBAR(wwin))
		tbar_height_0 = TITLEBAR_HEIGHT;
	if (HAS_RESIZEBAR(wwin))
		rbar_height_0 = wwin->screen_ptr->resizebar_height;
	if (HAS_BORDER(wwin))
		bd_width_0 = wFrameBorderWidth(wwin);

	/* the length to be subtracted if the window has titlebar, etc */
	adjust_height = tbar_height_0 + 2 * bd_width_0 + rbar_height_0;

	tmp = wwin;
	/* The focused window is always the last in the list */
	while (tmp->prev) {
		/* ignore windows in other workspaces etc */
		if (!wWindowIsVisibleOnAnyHead(tmp->prev)
		    || tmp->prev->flags.miniaturized || tmp->prev->flags.hidden) {
			tmp = tmp->prev;
			continue;
		}
		tmp = tmp->prev;

		/* Set the coordinates of obstructing window */
		set_window_coords(tmp, &obs);

		/* Try to maximize in the y direction first */
		x_intsect = calcIntersectionLength(orig.left, orig.width, obs.left, obs.width);
		if (x_intsect != 0) {
			/* TODO: Consider the case when coords are equal */
			if (obs.bottom < orig.top && obs.bottom > new.top) {
				/* w_0 is below the bottom of w_j */
				new.top = obs.bottom + 1;
			}
			if (orig.bottom < obs.top && obs.top < new.bottom) {
				/* The bottom of w_0 is above the top of w_j */
				new.bottom = obs.top - 1;
			}
		}
	}

	tmp = wwin;
	while (tmp->prev) {
		if (!wWindowIsVisibleOnAnyHead(tmp->prev)
		    || tmp->prev->flags.miniaturized || tmp->prev->flags.hidden) {
			tmp = tmp->prev;
			continue;
		}
		tmp = tmp->prev;

		set_window_coords(tmp, &obs);

		/*
		 * Use the new.top and new.height instead of original values
		 * as they may have different intersections with the obstructing windows
		 */
		new.height = new.bottom - new.top - adjust_height;
		y_intsect = calcIntersectionLength(new.top, new.height, obs.top, obs.height);
		if (y_intsect != 0) {
			if (obs.right < orig.left && obs.right > new.left) {
				/* w_0 is completely to the right of w_j */
				new.left = obs.right + 1;
			}
			if (orig.right < obs.left && obs.left < new.right) {
				/* w_0 is completely to the left of w_j */
				new.right = obs.left - 1;
			}
		}
	}

	*new_x = new.left;
	*new_y = new.top;
	/* xcalc needs -7 here, but other apps don't */
	*new_height = new.bottom - new.top - adjust_height - 1;
	*new_width = new.right - new.left;
}

void wUnmaximizeWindow(WWindow *wwin)
{
	int x, y, w, h, layout_workspace;

	if (!wwin->flags.maximized)
		return;

	layout_workspace = wwin->flags.tiled ? wLayoutWorkspaceForWindow(wwin) : -1;

	if (wwin->flags.shaded) {
		wwin->flags.skip_next_animation = 1;
		wUnshadeWindow(wwin);
	}

	if (w_global.config->drag_maximized_window == DRAGMAX_NOMOVE)
		wwin->client_flags.no_movable = 0;

	/* Use old coordinates if they are set, current values otherwise */
	remember_geometry(wwin, &x, &y, &w, &h);

	/* unMaximusize relative to original position */
	if (wwin->flags.maximized & MAX_MAXIMUS) {
		x += wwin->frame_x - wwin->maximus_x;
		y += wwin->frame_y - wwin->maximus_y;
	}

	wwin->flags.maximized = 0;
	wwin->flags.old_maximized = 0;
	wWindowUpdateButtonImages(wwin);
	wWindowConfigure(wwin, x, y, w, h);
	wWindowSynthConfigureNotify(wwin);

	if (wwin->flags.tiled) {
		wwin->flags.tiled = 0;
		wwin->flags.no_tiling = 1;
		if (layout_workspace >= 0)
			wLayoutApply(wwin->screen_ptr, layout_workspace);
	}

	WMPostNotificationName(WMNChangedState, wwin, "maximize");
	wSoundPlay(wwin->screen_ptr, WSOUND_UNMAXIMIZE);

	if (wwin->tab_group)
		wGroupSyncMaximize(wwin);
}

void wFullscreenMonitorsWindow(WWindow *wwin, unsigned long top, unsigned long bottom,
					unsigned long left, unsigned long right)
{
	int i;
	long monitor;
	WMRect rect1, rect2;

	if (top    < (unsigned long)wwin->screen_ptr->xine_info.count &&
	    bottom < (unsigned long)wwin->screen_ptr->xine_info.count &&
	    left   < (unsigned long)wwin->screen_ptr->xine_info.count &&
	    right  < (unsigned long)wwin->screen_ptr->xine_info.count) {
		wwin->flags.fullscreen_monitors[0] = top;
		wwin->flags.fullscreen_monitors[1] = bottom;
		wwin->flags.fullscreen_monitors[2] = left;
		wwin->flags.fullscreen_monitors[3] = right;
	} else {
		wwin->flags.fullscreen_monitors[0] = -1;
		return;
	}

	/* Per the EWMH spec, _NET_WM_FULLSCREEN_MONITORS must not change
	 * the window state, only update which monitors a fullscreen window
	 * spans. Only reconfigure if the window is already fullscreen. */
	if (!wwin->flags.fullscreen)
		return;

	i = 0;
	monitor = wwin->flags.fullscreen_monitors[i];
	rect1 = wwin->screen_ptr->xine_info.screens[monitor];

	for (i = 1; i <= 3; i++) {
		monitor = wwin->flags.fullscreen_monitors[i];
		rect2 = wwin->screen_ptr->xine_info.screens[monitor];
		wGetRectUnion(&rect1, &rect2, &rect1);
	}
	wWindowConfigure(wwin, rect1.pos.x, rect1.pos.y, rect1.size.width, rect1.size.height);

	/* echo the indices back as the _NET_WM_FULLSCREEN_MONITORS property
	 * via updateStateHint, which writes it when flags.fullscreen and
	 * fullscreen_monitors[0] != -1 */
	WMPostNotificationName(WMNChangedState, wwin, "fullscreen-monitors");
}

void wFullscreenWindow(WWindow *wwin)
{
	int head;
	WMRect rect;

	if (wwin->flags.fullscreen)
		return;

	wwin->flags.fullscreen = true;

	wWindowConfigureBorders(wwin);

	ChangeStackingLevel(wwin->frame->core, WMFullscreenLevel);

	wwin->bfs_geometry.x = wwin->frame_x;
	wwin->bfs_geometry.y = wwin->frame_y;
	wwin->bfs_geometry.width = wwin->frame->core->width;
	wwin->bfs_geometry.height = wwin->frame->core->height;
	wwin->bfs_maximized = wwin->flags.maximized;

	head = wGetHeadForWindow(wwin);
	rect = wGetRectForHead(wwin->screen_ptr, head);
	wWindowConfigure(wwin, rect.pos.x, rect.pos.y, rect.size.width, rect.size.height);

	wwin->screen_ptr->bfs_focused_window = wwin->screen_ptr->focused_window;
	wSetFocusTo(wwin->screen_ptr, wwin);

	WMPostNotificationName(WMNChangedState, wwin, "fullscreen");
}

void wUnfullscreenWindow(WWindow *wwin)
{
	int target_max;

	if (!wwin->flags.fullscreen)
		return;

	wwin->flags.fullscreen = false;

	/* restore exactly the pre-fullscreen state first, including the
	 * maximize flag, so old_geometry and the borders rebuild see the
	 * geometry that was current at wFullscreenWindow time */
	target_max = wwin->flags.maximized;
	wwin->flags.maximized = wwin->bfs_maximized;
	wWindowConfigure(wwin, wwin->bfs_geometry.x, wwin->bfs_geometry.y,
			 wwin->bfs_geometry.width, wwin->bfs_geometry.height);

	wWindowConfigureBorders(wwin);

	/* _NET_WM_STATE MAXIMIZED can change while fullscreen but
	 * IS_RESIZABLE() makes wMaximizeWindow() a no-op there, so the
	 * request is recorded in flags.maximized and applied now from
	 * the restored pre-fullscreen geometry */
	if (target_max != wwin->bfs_maximized) {
		if (target_max)
			wMaximizeWindow(wwin, target_max, wGetHeadForWindow(wwin));
		else
			wUnmaximizeWindow(wwin);
	}

	/* Restack out of the fullscreen layer after the geometry,
	 * decoration and maximize restore: the level change republishes
	 * _NET_WM_STATE through the stacking notification, and that write
	 * must not drop the fullscreen bit before the restore configures
	 * are on the wire, or a client requesting its own geometry on the
	 * state change gets the restore configures racing its granted
	 * reply. */
	if (wwin->frame->core->stacking->window_level == WMFullscreenLevel) {
		if (WFLAGP(wwin, sunken))
			ChangeStackingLevel(wwin->frame->core, WMSunkenLevel);
		else if (WFLAGP(wwin, floating))
			ChangeStackingLevel(wwin->frame->core, WMFloatingLevel);
		else
			ChangeStackingLevel(wwin->frame->core, WMNormalLevel);
	}

	/*
	   // seems unnecessary, but also harmless (doesn't generate flicker) -Dan
	   wFrameWindowPaint(wwin->frame);
	 */

	WMPostNotificationName(WMNChangedState, wwin, "fullscreen");

	if (wwin->screen_ptr->bfs_focused_window) {
		wSetFocusTo(wwin->screen_ptr, wwin->screen_ptr->bfs_focused_window);
		wwin->screen_ptr->bfs_focused_window = NULL;
	}
}

#ifdef USE_ANIMATIONS
static void animateResizeFlip(WScreen *scr, int x, int y, int w, int h, int fx, int fy, int fw, int fh, int steps)
{
#define FRAMES (MINIATURIZE_ANIMATION_FRAMES_F)
	float cx, cy, cw, ch;
	float xstep, ystep, wstep, hstep;
	xcb_point_t points[5];
	float dx, dch, midy;
	float angle, final_angle, delta;

	xstep = (float)(fx - x) / steps;
	ystep = (float)(fy - y) / steps;
	wstep = (float)(fw - w) / steps;
	hstep = (float)(fh - h) / steps;

	cx = (float)x;
	cy = (float)y;
	cw = (float)w;
	ch = (float)h;

	final_angle = 2 * WM_PI * MINIATURIZE_ANIMATION_TWIST_F;
	delta = (float)(final_angle / FRAMES);
	for (angle = 0;; angle += delta) {
		if (angle > final_angle)
			angle = final_angle;

		dx = (cw / 10) - ((cw / 5) * sinf(angle));
		dch = (ch / 2) * cosf(angle);
		midy = cy + (ch / 2);

		points[0].x = cx + dx;
		points[0].y = midy - dch;
		points[1].x = cx + cw - dx;
		points[1].y = points[0].y;
		points[2].x = cx + cw + dx;
		points[2].y = midy + dch;
		points[3].x = cx - dx;
		points[3].y = points[2].y;
		points[4].x = points[0].x;
		points[4].y = points[0].y;

		wWireframeBegin(scr);
		wWireframeAddPolyline(scr, points, 5);
		wWireframeCommit(scr);
		wXFlush(wdisp->conn);
		wusleep(MINIATURIZE_ANIMATION_DELAY_F);

		cx += xstep;
		cy += ystep;
		cw += wstep;
		ch += hstep;
		if (angle >= final_angle)
			break;

	}
	wWireframeHide(scr);
	wXFlush(wdisp->conn);
}

#undef FRAMES

static void
animateResizeTwist(WScreen *scr, int x, int y, int w, int h, int fx, int fy, int fw, int fh, int steps)
{
#define FRAMES (MINIATURIZE_ANIMATION_FRAMES_T)
	float cx, cy, cw, ch;
	float xstep, ystep, wstep, hstep;
	xcb_point_t points[5];
	float angle, final_angle, a, d, delta;

	x += w / 2;
	y += h / 2;
	fx += fw / 2;
	fy += fh / 2;

	xstep = (float)(fx - x) / steps;
	ystep = (float)(fy - y) / steps;
	wstep = (float)(fw - w) / steps;
	hstep = (float)(fh - h) / steps;

	cx = (float)x;
	cy = (float)y;
	cw = (float)w;
	ch = (float)h;

	final_angle = 2 * WM_PI * MINIATURIZE_ANIMATION_TWIST_T;
	delta = (float)(final_angle / FRAMES);
	for (angle = 0;; angle += delta) {
		if (angle > final_angle)
			angle = final_angle;

		a = atan2f(ch, cw);
		d = sqrtf((cw / 2) * (cw / 2) + (ch / 2) * (ch / 2));

		points[0].x = cx + cosf(angle - a) * d;
		points[0].y = cy + sinf(angle - a) * d;
		points[1].x = cx + cosf(angle + a) * d;
		points[1].y = cy + sinf(angle + a) * d;
		points[2].x = cx + cosf(angle - a + (float)WM_PI) * d;
		points[2].y = cy + sinf(angle - a + (float)WM_PI) * d;
		points[3].x = cx + cosf(angle + a + (float)WM_PI) * d;
		points[3].y = cy + sinf(angle + a + (float)WM_PI) * d;
		points[4].x = cx + cosf(angle - a) * d;
		points[4].y = cy + sinf(angle - a) * d;

		wWireframeBegin(scr);
		wWireframeAddPolyline(scr, points, 5);
		wWireframeCommit(scr);
		wXFlush(wdisp->conn);
		wusleep(MINIATURIZE_ANIMATION_DELAY_T);

		cx += xstep;
		cy += ystep;
		cw += wstep;
		ch += hstep;
		if (angle >= final_angle)
			break;

	}
	wWireframeHide(scr);
	wXFlush(wdisp->conn);
}

#undef FRAMES

static void animateResizeZoom(WScreen *scr, int x, int y, int w, int h, int fx, int fy, int fw, int fh, int steps)
{
#define FRAMES (MINIATURIZE_ANIMATION_FRAMES_Z)
	float cx[FRAMES], cy[FRAMES], cw[FRAMES], ch[FRAMES];
	float xstep, ystep, wstep, hstep;
	int i, j;

	xstep = (float)(fx - x) / steps;
	ystep = (float)(fy - y) / steps;
	wstep = (float)(fw - w) / steps;
	hstep = (float)(fh - h) / steps;

	for (j = 0; j < FRAMES; j++) {
		cx[j] = (float)x;
		cy[j] = (float)y;
		cw[j] = (float)w;
		ch[j] = (float)h;
	}
	for (i = 0; i <= steps; i++) {
		wWireframeBegin(scr);
		for (j = 0; j < FRAMES; j++)
			wWireframeAddOutline(scr, (int)cx[j], (int)cy[j],
					     (int)cw[j], (int)ch[j], 0, 0, 0);
		wWireframeCommit(scr);
		wXFlush(wdisp->conn);
		wusleep(MINIATURIZE_ANIMATION_DELAY_Z);

		for (j = 0; j < FRAMES; j++) {
			if (j < FRAMES - 1) {
				cx[j] = cx[j + 1];
				cy[j] = cy[j + 1];
				cw[j] = cw[j + 1];
				ch[j] = ch[j + 1];
			} else {
				cx[j] += xstep;
				cy[j] += ystep;
				cw[j] += wstep;
				ch[j] += hstep;
			}
		}
	}
	wWireframeHide(scr);
}

#undef FRAMES

void animateResize(WScreen *scr, int x, int y, int w, int h, int fx, int fy, int fw, int fh)
{
	int style = w_global.config->iconification_style;	/* Catch the value */
	int steps;

	if (style == WIS_NONE)
		return;

	if (style == WIS_RANDOM)
		style = rand() % 3;

	switch (style) {
	case WIS_TWIST:
		steps = MINIATURIZE_ANIMATION_STEPS_T;
		if (steps > 0)
			animateResizeTwist(scr, x, y, w, h, fx, fy, fw, fh, steps);
		break;
	case WIS_FLIP:
		steps = MINIATURIZE_ANIMATION_STEPS_F;
		if (steps > 0)
			animateResizeFlip(scr, x, y, w, h, fx, fy, fw, fh, steps);
		break;
	case WIS_ZOOM:
	default:
		steps = MINIATURIZE_ANIMATION_STEPS_Z;
		if (steps > 0)
			animateResizeZoom(scr, x, y, w, h, fx, fy, fw, fh, steps);
		break;
	}
}
#endif /* USE_ANIMATIONS */

static void flushExpose(void)
{
	xcb_generic_event_t *tmpev = NULL;

	while (WXEvqTakeTyped(wdisp->conn, XCB_EXPOSE, XCB_NONE, &tmpev)) {
		WMHandleEvent(tmpev);
		free(tmpev);
	}
	wXSync(wdisp->conn);
}

static void unmapTransientsFor(WWindow *wwin)
{
	WWindow *tmp;

	tmp = wwin->screen_ptr->focused_window;
	while (tmp) {
		/* unmap the transients for this transient */
		if (tmp != wwin && tmp->transient_for == wwin->client_win
		    && (tmp->flags.mapped || wwin->screen_ptr->flags.startup || tmp->flags.shaded)) {
			unmapTransientsFor(tmp);
			tmp->flags.miniaturized = 1;
			if (!tmp->flags.shaded)
				wWindowUnmap(tmp);
			else
				xcb_unmap_window(wdisp->conn, tmp->frame->core->window);
			/*
			   if (!tmp->flags.shaded)
			 */
			wClientSetState(tmp, XCB_ICCCM_WM_STATE_ICONIC, XCB_NONE);

			WMPostNotificationName(WMNChangedState, tmp, "iconify-transient");
		}
		tmp = tmp->prev;
	}
}

static void mapTransientsFor(WWindow *wwin)
{
	WWindow *tmp;

	tmp = wwin->screen_ptr->focused_window;
	while (tmp) {
		/* recursively map the transients for this transient */
		if (tmp != wwin && tmp->transient_for == wwin->client_win && /*!tmp->flags.mapped */ tmp->flags.miniaturized
		    && tmp->icon == NULL) {
			mapTransientsFor(tmp);
			tmp->flags.miniaturized = 0;
			if (!tmp->flags.shaded)
				wWindowMap(tmp);
			else
				xcb_map_window(wdisp->conn, tmp->frame->core->window);
			tmp->flags.semi_focused = 0;
			/*
			   if (!tmp->flags.shaded)
			 */
			wClientSetState(tmp, XCB_ICCCM_WM_STATE_NORMAL, XCB_NONE);

			WMPostNotificationName(WMNChangedState, tmp, "iconify-transient");
		}
		tmp = tmp->prev;
	}
}

static WWindow *recursiveTransientFor(WWindow *wwin)
{
	int i;

	if (!wwin)
		return XCB_NONE;

	/* hackish way to detect transient_for cycle */
	i = wwin->screen_ptr->window_count + 1;

	while (wwin && wwin->transient_for != XCB_NONE && i > 0) {
		wwin = wWindowFor(wwin->transient_for);
		i--;
	}
	if (i == 0 && wwin) {
		wwarning(_("window \"%s\" has a severely broken WM_TRANSIENT_FOR hint"),
		         wwin->frame->title);
		return NULL;
	}

	return wwin;
}

#ifdef USE_ANIMATIONS
static int getAnimationGeometry(WWindow *wwin, int *ix, int *iy, int *iw, int *ih)
{
	if (wwin->screen_ptr->flags.startup || w_global.config->no_animations
		|| w_global.display_off
		|| wwin->flags.skip_next_animation || wwin->icon == NULL)
		return 0;

	if (!w_global.config->disable_miniwindows && !wwin->flags.net_handle_icon) {
		*ix = wwin->icon_x;
		*iy = wwin->icon_y;
		*iw = wwin->icon->core->width;
		*ih = wwin->icon->core->height;
	} else {
		if (wwin->flags.net_handle_icon) {
			*ix = wwin->icon_x;
			*iy = wwin->icon_y;
			*iw = wwin->icon_w;
			*ih = wwin->icon_h;
		} else {
			*ix = 0;
			*iy = 0;
			*iw = wwin->screen_ptr->scr_width;
			*ih = wwin->screen_ptr->scr_height;
		}
	}
	return 1;
}
#endif	/* USE_ANIMATIONS */

void wIconifyWindow(WWindow *wwin)
{
	WXWindowAttributes attribs;
	int present;

	/* a tab group is one user-visible unit, only the active member
	 * owns a miniwindow, redirect any iconify request on an inactive
	 * member to the active one and let wGroupSyncMiniaturize() mark
	 * the peers afterwards */
	if (wGroupIsInactiveMember(wwin))
		wwin = wwin->tab_group->members[wwin->tab_group->active_index];

	if (!wXGetWindowAttributes(wdisp->conn, wwin->client_win, &attribs, NULL))
		return; /* the window doesn't exist anymore */

	if (wwin->flags.miniaturized)
		return; /* already miniaturized */

	if (wwin->transient_for != XCB_NONE && wwin->transient_for != wwin->screen_ptr->root_win) {
		WWindow *owner = wWindowFor(wwin->transient_for);

		if (owner && owner->flags.miniaturized)
			return;
	}

	present = wWindowIsVisibleOnAnyHead(wwin);

	/* if the window is in another workspace, simplify process */
	if (present) {
		/* icon creation may take a while */
		wXGrabPointer(wdisp->conn, wwin->screen_ptr->root_win, false,
			     XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_BUTTON_RELEASE, XCB_GRAB_MODE_ASYNC,
			     XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE, XCB_CURRENT_TIME);
	}

	if (!w_global.config->disable_miniwindows && !wwin->flags.net_handle_icon) {
		if (!wwin->flags.icon_moved)
			PlaceIcon(wwin->screen_ptr, &wwin->icon_x, &wwin->icon_y, wGetHeadForWindow(wwin));

		wwin->icon = icon_create_for_wwindow(wwin);
		wwin->icon->mapped = 1;

		/* extract the window screenshot every time, as the option can be enable anytime */
		if (wwin->client_win && wwin->flags.mapped) {
			RImage *mini_preview;
			RXImage *pimg;
			unsigned int w, h;
			int x, y;
			xcb_window_t baz;

			/* the frame must be on top for the capture, the
			 * raise goes through the deferred stacking list */
			wRaiseFrame(wwin->frame->core);
			CommitStacking(wwin->screen_ptr);
			wXTranslateCoordinates(wdisp->conn, wwin->client_win, wwin->screen_ptr->root_win, 0, 0, &x, &y, &baz);

			w = attribs.width;
			h = attribs.height;

			if (x - attribs.x + attribs.width > wwin->screen_ptr->scr_width)
				w = wwin->screen_ptr->scr_width - x + attribs.x;

			if (y - attribs.y + attribs.height > wwin->screen_ptr->scr_height)
				h = wwin->screen_ptr->scr_height - y + attribs.y;

			pimg = RGetXImage(wwin->screen_ptr->rcontext, wwin->client_win, 0, 0, w, h);
			if (pimg) {
				mini_preview = RCreateImageFromRXImage(wwin->screen_ptr->rcontext, pimg, NULL);
				RDestroyXImage(wwin->screen_ptr->rcontext, pimg);

				if (mini_preview) {
					set_icon_minipreview(wwin->icon, mini_preview);
					RReleaseImage(mini_preview);
				} else {
					const char *title;
					char title_buf[32];

					if (wwin->frame->title) {
						title = wwin->frame->title;
					} else {
						snprintf(title_buf, sizeof(title_buf), "(id=0x%x)", wwin->client_win);
						title = title_buf;
					}
					wwarning(_("creation of mini-preview failed for window \"%s\""), title);
				}
			}
		}
	}

	wwin->flags.miniaturized = 1;
	wwin->flags.mapped = 0;
	wWindowUrgentBlink(wwin);

	/* unmap transients */
	unmapTransientsFor(wwin);

	if (present) {
#ifdef USE_ANIMATIONS
		int ix, iy, iw, ih;
#endif
		wSoundPlay(wwin->screen_ptr, WSOUND_ICONIFY);
		wXUngrabPointer(wdisp->conn, XCB_CURRENT_TIME);
		wWindowUnmap(wwin);
		/* let all Expose events arrive so that we can repaint
		 * something before the animation starts (and the server is grabbed) */
		wXSync(wdisp->conn);

		if (w_global.config->disable_miniwindows || wwin->flags.net_handle_icon)
			wClientSetState(wwin, XCB_ICCCM_WM_STATE_ICONIC, XCB_NONE);
		else
			wClientSetState(wwin, XCB_ICCCM_WM_STATE_ICONIC, wwin->icon->icon_win);

		flushExpose();
#ifdef USE_ANIMATIONS
		if (getAnimationGeometry(wwin, &ix, &iy, &iw, &ih))
			animateResize(wwin->screen_ptr, wwin->frame_x, wwin->frame_y,
				      wwin->frame->core->width, wwin->frame->core->height, ix, iy, iw, ih);
#endif
	} else {
		/* window was not visible yet (e.g. session-restore iconify
		 * before the frame was mapped), still set WM_STATE to
		 * IconicState so the client and downstream consumers see
		 * the correct state */
		if (w_global.config->disable_miniwindows || wwin->flags.net_handle_icon
		    || !wwin->icon)
			wClientSetState(wwin, XCB_ICCCM_WM_STATE_ICONIC, XCB_NONE);
		else
			wClientSetState(wwin, XCB_ICCCM_WM_STATE_ICONIC, wwin->icon->icon_win);
	}

	wwin->flags.skip_next_animation = 0;

	if (!w_global.config->disable_miniwindows && !wwin->flags.net_handle_icon) {
		if (wWindowIsVisibleOnAnyHead(wwin) || w_global.config->sticky_icons)
			xcb_map_window(wdisp->conn, wwin->icon->core->window);

		AddToStackList(wwin->icon->core);
		wLowerFrame(wwin->icon->core);
	}

	if (present) {
		WWindow *owner = recursiveTransientFor(wwin->screen_ptr->focused_window);

		/*
		 * It doesn't seem to be working and causes button event hangup
		 * when deiconifying a transient window.
		 setupIconGrabs(wwin->icon);
		 */
		if ((wwin->flags.focused || (owner && wwin->client_win == owner->client_win))
		    && w_global.config->focus_mode == WKF_CLICK) {
			WWindow *tmp;

			/* the next window in the focus list takes over.
			 * wwin itself and its unmapped transients are
			 * skipped by the mapped check */
			for (tmp = wwin->screen_ptr->focused_window; tmp; tmp = tmp->prev) {
				if (tmp != wwin && !WFLAGP(tmp, no_focusable)
				    && !WFLAGP(tmp, skip_window_list)
				    && (tmp->flags.mapped || tmp->flags.shaded))
					break;
			}
			if (!tmp) {
				for (tmp = wwin->screen_ptr->focused_window; tmp; tmp = tmp->prev) {
					if (tmp != wwin && !WFLAGP(tmp, no_focusable)
					    && (tmp->flags.mapped || tmp->flags.shaded))
						break;
				}
			}
			wSetFocusTo(wwin->screen_ptr, tmp);
			if (tmp)
				wRaiseFrame(tmp->frame->core);
			warpPointerToFocusedWindow(tmp);
		} else if (w_global.config->focus_mode != WKF_CLICK && wwin->flags.focused) {
			/* Skip the focus reset when iconify was triggered by the
			 * auto-hide/miniaturize-on-unfocus path: focus has already
			 * moved to the new window and wWindowUnfocus() cleared
			 * wwin->flags.focused, so resetting here would unfocus it. */
			wSetFocusTo(wwin->screen_ptr, NULL);
		}
#ifdef USE_ANIMATIONS
		if (!wwin->screen_ptr->flags.startup &&
		    !wwin->flags.restore_iconified) {
			/* Catch up with events not processed while animation was running */
			xcb_window_t clientwin = wwin->client_win;

			ProcessPendingEvents();

			/* the window can disappear while ProcessPendingEvents() runs */
			if (!wWindowFor(clientwin))
				return;
		}
#endif
	}

	/* maybe we want to do this regardless of net_handle_icon
	 * it seems to me we might break behaviour this way.
	 */
	if (wwin->flags.selected && !w_global.config->disable_miniwindows
	    && !wwin->flags.net_handle_icon)
		wIconSelect(wwin->icon);

	WMPostNotificationName(WMNChangedState, wwin, "iconify");

	if (wwin->tab_group)
		wGroupSyncMiniaturize(wwin);

	if (w_global.config->auto_arrange_icons)
		wArrangeIcons(wwin->screen_ptr, true);

	wLayoutApplyForWindow(wwin);
}

void wDeiconifyWindow(WWindow *wwin)
{
	bool was_shaded;
	/* deiconify always targets the active tab so it finds the
	 * miniwindow and maps the right frame, wWindowSingleFocus()
	 * has already promoted any explicitly requested member */
	if (wGroupIsInactiveMember(wwin))
		wwin = wwin->tab_group->members[wwin->tab_group->active_index];

	/* Let's avoid changing workspace while deiconifying */
	w_global.ignore_workspace_change = true;

	/* we're hiding for show_desktop */
	int netwm_hidden = wwin->flags.net_show_desktop &&
	    WWIN_PRIMARY_WS(wwin) != wwin->screen_ptr->current_workspace;

	was_shaded = wwin->flags.shaded;

	if (!netwm_hidden)
		wWindowChangeWorkspace(wwin, wwin->screen_ptr->current_workspace);

	if (!wwin->flags.miniaturized) {
		w_global.ignore_workspace_change = false;
		return;
	}

	if (wwin->transient_for != XCB_NONE && wwin->transient_for != wwin->screen_ptr->root_win) {
		WWindow *owner = recursiveTransientFor(wwin);

		if (owner && owner->flags.miniaturized) {
			wDeiconifyWindow(owner);
			wSetFocusTo(wwin->screen_ptr, wwin);
			warpPointerToFocusedWindow(wwin);
			wRaiseFrame(wwin->frame->core);
			w_global.ignore_workspace_change = false;
			return;
		}
	}

	wwin->flags.miniaturized = 0;
	wwin->flags.restore_iconified = 0;
	wWindowUrgentBlink(wwin);

	if (!netwm_hidden && !wwin->flags.shaded)
		wwin->flags.mapped = 1;

	if (!netwm_hidden || w_global.config->sticky_icons) {
		/* maybe we want to do this regardless of net_handle_icon
		 * it seems to me we might break behaviour this way.
		 */
		if (!w_global.config->disable_miniwindows && !wwin->flags.net_handle_icon
		    && wwin->icon != NULL) {
			if (wwin->icon->selected)
				wIconSelect(wwin->icon);

			xcb_unmap_window(wdisp->conn, wwin->icon->core->window);
		}
	}

	/* Relocate to an active head if the stored position is in dead space */
	wWindowSnapToHead(wwin);

	/* if the window is in another workspace, do it silently */
	if (!netwm_hidden) {
#ifdef USE_ANIMATIONS
		int ix, iy, iw, ih;
		if (getAnimationGeometry(wwin, &ix, &iy, &iw, &ih))
			animateResize(wwin->screen_ptr, ix, iy, iw, ih,
				      wwin->frame_x, wwin->frame_y,
				      wwin->frame->core->width, wwin->frame->core->height);
#endif
		wwin->flags.skip_next_animation = 0;
		wXGrabServer(wdisp->conn);
		if (!wwin->flags.shaded)
			xcb_map_window(wdisp->conn, wwin->client_win);

		xcb_map_window(wdisp->conn, wwin->frame->core->window);
		wRaiseFrame(wwin->frame->core);
		if (!wwin->flags.shaded)
			wClientSetState(wwin, XCB_ICCCM_WM_STATE_NORMAL, XCB_NONE);

		mapTransientsFor(wwin);
	}

	if (!w_global.config->disable_miniwindows && wwin->icon != NULL
	    && !wwin->flags.net_handle_icon) {
		RemoveFromStackList(wwin->icon->core);
		wSetFocusTo(wwin->screen_ptr, wwin);
		warpPointerToFocusedWindow(wwin);
		wIconDestroy(wwin->icon);
		wwin->icon = NULL;
	}

	if (!netwm_hidden) {
		wXUngrabServer(wdisp->conn);

		wSetFocusTo(wwin->screen_ptr, wwin);
		warpPointerToFocusedWindow(wwin);
		WMAddTimerHandler(1, focusRestoredWindow,
				  (void *)(uintptr_t)wwin->client_win);
	}

	if (w_global.config->auto_arrange_icons)
		wArrangeIcons(wwin->screen_ptr, true);

	WMPostNotificationName(WMNChangedState, wwin, "iconify");

	if (wwin->tab_group)
		wGroupSyncMiniaturize(wwin);

	/* wUnshadeWindow already played WSOUND_UNSHADE, so skip the
	 * deiconify sound on shaded+iconified restore. */
	if (!netwm_hidden)
		wUnshadeWindow(wwin);

	if (!netwm_hidden && !was_shaded)
		wSoundPlay(wwin->screen_ptr, WSOUND_DEICONIFY);

	w_global.ignore_workspace_change = false;

	wLayoutApplyForWindow(wwin);
}

/* Re-evaluate every miniaturized window against the current
 * DisableMiniwindows policy after a runtime flip. NO -> YES tears down
 * the existing miniwindow so it does not stay mapped and stacked,
 * YES -> NO fakes a re-iconify so the icon-creation branch of
 * wIconifyWindow() runs and the window gains its miniwindow. Windows
 * whose icon is owned by an EWMH pager are skipped, the same as the
 * iconify/deiconify paths above. */
void wRefreshMiniwindows(WScreen *scr)
{
	WWindow *wwin;
	bool disabled;

	disabled = w_global.config->disable_miniwindows;
	wwin = scr->focused_window;
	while (wwin) {
		if (wwin->flags.miniaturized && !wwin->flags.net_handle_icon
		    && !wGroupIsInactiveMember(wwin)) {
			if (disabled && wwin->icon) {
				if (wwin->icon->mapped)
					xcb_unmap_window(wdisp->conn, wwin->icon->core->window);
				RemoveFromStackList(wwin->icon->core);
				wIconDestroy(wwin->icon);
				wwin->icon = NULL;
			} else if (!disabled && !wwin->icon) {
				wwin->flags.miniaturized = false;
				wwin->flags.skip_next_animation = true;
				wIconifyWindow(wwin);
			}
		}
		wwin = wwin->prev;
	}

	if (!disabled && w_global.config->auto_arrange_icons)
		wArrangeIcons(scr, true);
}

void wHideWindow(WWindow *wwin, WIcon *icon, int icon_x, int icon_y, int animate)
{
	if (wwin->flags.miniaturized) {
		if (wwin->icon) {
			xcb_unmap_window(wdisp->conn, wwin->icon->core->window);
			wwin->icon->mapped = 0;
		}
		wwin->flags.hidden = 1;

		WMPostNotificationName(WMNChangedState, wwin, "hide");
		return;
	}

	if (wwin->flags.inspector_open)
		wHideInspectorForWindow(wwin);

	wwin->flags.hidden = 1;
	wWindowUnmap(wwin);

	wClientSetState(wwin, XCB_ICCCM_WM_STATE_ICONIC, icon ? icon->icon_win : XCB_NONE);
	flushExpose();
	wSoundPlay(wwin->screen_ptr, WSOUND_HIDE);

#ifdef USE_ANIMATIONS
	if (icon && !wwin->screen_ptr->flags.startup && !w_global.config->no_animations &&
	    !w_global.display_off && !wwin->flags.skip_next_animation && animate) {
		animateResize(wwin->screen_ptr, wwin->frame_x, wwin->frame_y,
			      wwin->frame->core->width, wwin->frame->core->height,
			      icon_x, icon_y, icon->core->width, icon->core->height);
	}
#else
	/* Tell the compiler it is normal that those parameters are not used in this case */
	(void) icon_x;
	(void) icon_y;
	(void) animate;
#endif
	wwin->flags.skip_next_animation = 0;

	WMPostNotificationName(WMNChangedState, wwin, "hide");
}

void wHideAll(WScreen *scr)
{
	WWindow *wwin;
	xcb_window_t *ids;
	WMenu *menu;
	unsigned int wcount = 0;
	int i;

	if (!scr)
		return;

	menu = scr->switch_menu;

	ids = wmalloc(sizeof(xcb_window_t));

	if (menu != NULL) {
		for (i = 0; i < menu->entry_no; i++) {
			wwin = (WWindow *) menu->entries[i]->clientdata;
			ids[wcount] = wwin->client_win;
			wcount++;
			ids = wrealloc(ids, sizeof(xcb_window_t) * (wcount + 1));
		}
	} else {
		wwin = scr->focused_window;

		while (wwin) {
			ids[wcount] = wwin->client_win;
			wcount++;
			ids = wrealloc(ids, sizeof(xcb_window_t) * (wcount + 1));
			wwin = wwin->prev;

		}
	}

	for (i = 0; i < wcount; i++) {
		wwin = wWindowFor(ids[i]);
		if (wwin
		    && WWIN_PRIMARY_WS(wwin) == scr->current_workspace
		    && !(wwin->flags.miniaturized || wwin->flags.hidden)
		    && !wwin->flags.internal_window
		    && !wGroupIsInactiveMember(wwin)
		    && !WFLAGP(wwin, no_miniaturizable)
		   ) {
			wwin->flags.skip_next_animation = 1;
			wIconifyWindow(wwin);
		}
	}

	wfree(ids);
}

void wHideOtherApplications(WWindow *awin)
{
	WWindow *wwin;
	WApplication *tapp;
	WScreen *scr;
	xcb_window_t *ids;
	xcb_window_t anchor;
	unsigned int wcount = 0;
	int i;

	if (!awin)
		return;

	scr = awin->screen_ptr;
	anchor = awin->client_win;

	/* Snapshot the focus chain first: wIconifyWindow()/wHideApplication()
	 * can rewire it via wSetFocusTo() focus migration, so walking it
	 * live can skip the window that migrated to the head. Client ids
	 * are stored because wIconifyWindow() can free a window mid-sweep,
	 * and the anchor is re-resolved the same way because its own client
	 * can be destroyed while a sibling's dispatch runs. */
	ids = wmalloc(sizeof(xcb_window_t));
	wwin = scr->focused_window;
	while (wwin) {
		ids[wcount++] = wwin->client_win;
		ids = wrealloc(ids, sizeof(xcb_window_t) * (wcount + 1));
		wwin = wwin->prev;
	}

	for (i = 0; i < wcount; i++) {
		awin = wWindowFor(anchor);
		if (!awin)
			break;
		wwin = wWindowFor(ids[i]);
		if (!wwin)
			continue;
		if (wwin != awin
		    && WWIN_PRIMARY_WS(wwin) == scr->current_workspace
		    && !(wwin->flags.miniaturized || wwin->flags.hidden)
		    && !wwin->flags.internal_window
		    && wGetWindowOfInspectorForWindow(wwin) != awin && !WFLAGP(wwin, no_hide_others)) {

			if (wwin->main_window == XCB_NONE || WFLAGP(wwin, no_appicon)) {
				if (!WFLAGP(wwin, no_miniaturizable)) {
					wwin->flags.skip_next_animation = 1;
					wIconifyWindow(wwin);
				}
			} else if (wwin->main_window != XCB_NONE && awin->main_window != wwin->main_window) {
				tapp = wApplicationOf(wwin->main_window);
				if (tapp) {
					tapp->flags.skip_next_animation = 1;
					wHideApplication(tapp);
				} else {
					if (!WFLAGP(wwin, no_miniaturizable)) {
						wwin->flags.skip_next_animation = 1;
						wIconifyWindow(wwin);
					}
				}
			}
		}
	}
	wfree(ids);
}

void wMiniaturizeOtherApplications(WWindow *awin)
{
	WWindow *wwin;
	WScreen *scr;
	xcb_window_t *ids;
	xcb_window_t anchor;
	unsigned int wcount = 0;
	int i;

	if (!awin)
		return;

	scr = awin->screen_ptr;
	anchor = awin->client_win;

	/* Snapshot the focus chain first: wIconifyWindow() can rewire it
	 * via wSetFocusTo() focus migration, so walking it live can skip
	 * the window that migrated to the head. Client ids are stored
	 * because wIconifyWindow() can free a window mid-sweep, and the
	 * anchor is re-resolved the same way because its own client can be
	 * destroyed while a sibling's dispatch runs. */
	ids = wmalloc(sizeof(xcb_window_t));
	wwin = scr->focused_window;
	while (wwin) {
		ids[wcount++] = wwin->client_win;
		ids = wrealloc(ids, sizeof(xcb_window_t) * (wcount + 1));
		wwin = wwin->prev;
	}

	for (i = 0; i < wcount; i++) {
		awin = wWindowFor(anchor);
		if (!awin)
			break;
		wwin = wWindowFor(ids[i]);
		if (!wwin)
			continue;
		if (wwin != awin
		    && WWIN_PRIMARY_WS(wwin) == scr->current_workspace
		    && !(wwin->flags.miniaturized || wwin->flags.hidden)
		    && !wwin->flags.internal_window
		    && !wGroupIsInactiveMember(wwin)
		    && !WFLAGP(wwin, no_miniaturizable)
		    && !WFLAGP(wwin, no_hide_others)
		    && wGetWindowOfInspectorForWindow(wwin) != awin
		    && !(awin->main_window != XCB_NONE && wwin->main_window == awin->main_window)) {
			wwin->flags.skip_next_animation = 1;
			wIconifyWindow(wwin);
		}
	}
	wfree(ids);
}

/*
 * User-gesture entry point for "hide this application". When any managed
 * window of the app advertises _GNUSTEP_WM_HIDE_APP in WM_PROTOCOLS, send
 * the protocol message there so the app drives its own hide and its
 * NSApplication delegate hooks fire, otherwise fall back to hiding from
 * the WM side via wHideApplication(). Callers that already hold the
 * window the user interacted with pass it as via, callers that only hold
 * the WApplication (app-icon / dock menu) pass NULL and a suitable window
 * is looked up. Do not call this from the _WINDOWMAKER_WM_FUNCTION
 * receiver or state-restore paths, those must use wHideApplication()
 * directly to avoid a request loop with the GNUstep backend.
 */
void wRequestHideApplication(WApplication *wapp, WWindow *via)
{
	if (!via || !via->protocols.HIDE_APP) {
		WWindow *wlist;

		via = NULL;
		if (wapp && wapp->main_window_desc) {
			wlist = wapp->main_window_desc->screen_ptr->focused_window;
			for (; wlist; wlist = wlist->prev) {
				if (wlist->main_window == wapp->main_window
				    && wlist->protocols.HIDE_APP) {
					via = wlist;
					break;
				}
			}
		}
	}
	if (via) {
		wClientSendProtocol(via, wdisp->atom.gnustep.wm_hide_app,
				    wdisp->timestamp.last_event);
		return;
	}
	if (wapp && wapp->main_window_desc
	    && !WFLAGP(wapp->main_window_desc, no_appicon))
		wHideApplication(wapp);
}

void wHideApplication(WApplication *wapp)
{
	WScreen *scr;
	WWindow *wlist;
	int hadfocus;
	int animate;

	if (!wapp) {
		wwarning("trying to hide a non grouped window");
		return;
	}
	if (!wapp->main_window_desc) {
		wwarning("group leader not found for window group");
		return;
	}
	scr = wapp->main_window_desc->screen_ptr;
	hadfocus = 0;
	wlist = scr->focused_window;
	if (!wlist)
		return;

	if (wlist->main_window == wapp->main_window)
		wapp->last_focused = wlist;
	else
		wapp->last_focused = NULL;

	animate = !wapp->flags.skip_next_animation;

	while (wlist) {
		if (wlist->main_window == wapp->main_window) {
			if (wlist->flags.focused)
				hadfocus = 1;
			if (wapp->app_icon) {
				wHideWindow(wlist, wapp->app_icon->icon,
					   wapp->app_icon->x_pos, wapp->app_icon->y_pos, animate);
				animate = false;
			}
		}
		wlist = wlist->prev;
	}

	wapp->flags.skip_next_animation = 0;

	if (hadfocus) {
		if (w_global.config->focus_mode == WKF_CLICK) {
			wlist = wWindowUnderPointerForFocus(scr, NULL);
			wSetFocusTo(scr, wlist);
			if (wlist)
				wRaiseFrame(wlist->frame->core);
			warpPointerToFocusedWindow(wlist);
		} else {
			wSetFocusTo(scr, NULL);
		}
	}

	wapp->flags.hidden = 1;

	if (w_global.config->auto_arrange_icons)
		wArrangeIcons(scr, true);

#ifdef HIDDENDOT
	if (wapp->app_icon)
		wAppIconPaint(wapp->app_icon);
#endif
}

void wUnhideWindow(WWindow *wwin, WIcon *icon, int icon_x, int icon_y,
		   int animate, int bringToCurrentWS)
{
	if (bringToCurrentWS)
		wWindowChangeWorkspace(wwin, wwin->screen_ptr->current_workspace);

	wwin->flags.hidden = 0;
	wSoundPlay(wwin->screen_ptr, WSOUND_UNHIDE);

	/* Relocate to an active head if the stored position is in dead space */
	wWindowSnapToHead(wwin);

#ifdef USE_ANIMATIONS
	if (icon && !wwin->screen_ptr->flags.startup && !w_global.config->no_animations &&
	    !w_global.display_off && animate) {
		animateResize(wwin->screen_ptr, icon_x, icon_y,
			      icon->core->width, icon->core->height,
			      wwin->frame_x, wwin->frame_y,
			      wwin->frame->core->width, wwin->frame->core->height);
	}
#else
	/* Tell the compiler it is normal that those parameters are not used in this case */
	(void) icon;
	(void) icon_x;
	(void) icon_y;
	(void) animate;
#endif
	wwin->flags.skip_next_animation = 0;
	if (wwin->screen_ptr->current_workspace == WWIN_PRIMARY_WS(wwin)
	    && !wGroupIsInactiveMember(wwin)) {
		xcb_map_window(wdisp->conn, wwin->client_win);
		xcb_map_window(wdisp->conn, wwin->frame->core->window);
		wClientSetState(wwin, XCB_ICCCM_WM_STATE_NORMAL, XCB_NONE);
		wwin->flags.mapped = 1;
		wRaiseFrame(wwin->frame->core);
	}
	if (wwin->flags.inspector_open)
		wUnhideInspectorForWindow(wwin);

	wWindowUrgentBlink(wwin);

	WMPostNotificationName(WMNChangedState, wwin, "hide");
}

void wUnhideApplication(WApplication *wapp, bool miniwindows, bool bringToCurrentWS)
{
	WScreen *scr;
	WWindow *wlist, *next;
	WWindow *focused = NULL;
	WWindow *target = NULL;
	xcb_window_t clientwin;
	int animate;

	if (!wapp)
		return;

	scr = wapp->main_window_desc->screen_ptr;
	wlist = scr->focused_window;
	if (!wlist)
		return;

	/* goto beginning of list */
	while (wlist->prev)
		wlist = wlist->prev;

	animate = !wapp->flags.skip_next_animation;

	while (wlist) {
		next = wlist->next;

		if (wlist->main_window == wapp->main_window) {
			if (wlist->flags.focused)
				focused = wlist;
			else if (!focused || !focused->flags.focused)
				focused = wlist;

			if (wlist->flags.miniaturized) {
				if ((bringToCurrentWS || w_global.config->sticky_icons ||
				     WWIN_PRIMARY_WS(wlist) == scr->current_workspace) && wlist->icon) {
					if (!wlist->icon->mapped) {
						int x, y;

						PlaceIcon(scr, &x, &y, wGetHeadForWindow(wlist));
						if (wlist->icon_x != x || wlist->icon_y != y)
							xcb_configure_window(wdisp->conn, wlist->icon->core->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ x, y });
						wlist->icon_x = x;
						wlist->icon_y = y;
						xcb_map_window(wdisp->conn, wlist->icon->core->window);
						wlist->icon->mapped = 1;
					}
					wRaiseFrame(wlist->icon->core);
				}
				if (bringToCurrentWS)
					wWindowChangeWorkspace(wlist, scr->current_workspace);
				wlist->flags.hidden = 0;
				if (miniwindows && WWIN_PRIMARY_WS(wlist) == scr->current_workspace)
					wDeiconifyWindow(wlist);
				WMPostNotificationName(WMNChangedState, wlist, "hide");
			} else if (wlist->flags.shaded) {
				if (bringToCurrentWS)
					wWindowChangeWorkspace(wlist, scr->current_workspace);
				wlist->flags.hidden = 0;
				if (WWIN_PRIMARY_WS(wlist) == scr->current_workspace
				    && !wGroupIsInactiveMember(wlist)) {
					wRaiseFrame(wlist->frame->core);
					xcb_map_window(wdisp->conn, wlist->frame->core->window);
					if (miniwindows)
						wUnshadeWindow(wlist);
				}
				WMPostNotificationName(WMNChangedState, wlist, "hide");
			} else if (wlist->flags.hidden) {
				WAppIcon *ai = wapp->app_icon;

				wUnhideWindow(wlist, ai ? ai->icon : NULL,
					     ai ? ai->x_pos : 0, ai ? ai->y_pos : 0,
					     animate, bringToCurrentWS);
				animate = false;
			} else {
				if (bringToCurrentWS && WWIN_PRIMARY_WS(wlist) != scr->current_workspace)
					wWindowChangeWorkspace(wlist, scr->current_workspace);

				wRaiseFrame(wlist->frame->core);
			}
		}
		wlist = next;
	}
	wapp->flags.skip_next_animation = 0;
	wapp->flags.hidden = 0;

	/* The target may have disappeared while restoring the application. */
	if (wapp->last_focused && wapp->last_focused->flags.mapped)
		target = wapp->last_focused;
	else if (focused)
		target = focused;

	if (target) {
		clientwin = target->client_win;
		target = wWindowFor(clientwin);
		if (target) {
			wRaiseFrame(target->frame->core);
			wSetFocusTo(scr, target);
			warpPointerToFocusedWindow(target);
			/* Fire strictly later than the 1 ms deiconify timers
			 * above so libev's heap orders it after them and the
			 * final focus lands on the intended window, not a
			 * deiconified one. */
			WMAddTimerHandler(2, focusRestoredWindow,
					  (void *)(uintptr_t)clientwin);
		}
	}

	wapp->last_focused = NULL;
	if (w_global.config->auto_arrange_icons)
		wArrangeIcons(scr, true);

#ifdef HIDDENDOT
	if (wapp->app_icon)
		wAppIconPaint(wapp->app_icon);
#endif
}

void wShowAllWindows(WScreen *scr)
{
	WWindow *wwin, *old_foc;
	WApplication *wapp;

	old_foc = wwin = scr->focused_window;
	while (wwin) {
		if (!wwin->flags.internal_window &&
		    WWIN_ON_WORKSPACE(wwin, scr->current_workspace)) {
			if (wwin->flags.miniaturized) {
				wwin->flags.skip_next_animation = 1;
				wDeiconifyWindow(wwin);
			} else if (wwin->flags.hidden) {
				wapp = wApplicationOf(wwin->main_window);
				if (wapp) {
					wUnhideApplication(wapp, false, false);
				} else {
					wwin->flags.skip_next_animation = 1;
					wDeiconifyWindow(wwin);
				}
			}
		}
		wwin = wwin->prev;
	}
	wSetFocusTo(scr, old_foc);
	warpPointerToFocusedWindow(old_foc);
	/*wRaiseFrame(old_foc->frame->core); */
}

void wRefreshDesktop(WScreen *scr)
{
	xcb_window_t win;

	win = xcb_generate_id(wdisp->conn);
	xcb_create_window(wdisp->conn, XCB_COPY_FROM_PARENT, win, scr->root_win, 0, 0, scr->scr_width,
			  scr->scr_height, 0, XCB_WINDOW_CLASS_COPY_FROM_PARENT, XCB_COPY_FROM_PARENT,
			  XCB_CW_BACKING_STORE | XCB_CW_SAVE_UNDER, (const uint32_t[]){ XCB_BACKING_STORE_NOT_USEFUL, false });
	xcb_configure_window(wdisp->conn, win, XCB_CONFIG_WINDOW_STACK_MODE, (const uint32_t[]){ XCB_STACK_MODE_ABOVE });
	/* refresh-desktop overlay, unmapped again right away, never in
	 * the stacking list, direct configure */
	xcb_map_window(wdisp->conn, win);
	xcb_destroy_window(wdisp->conn, win);
	wXFlush(wdisp->conn);
}

void wArrangeIcons(WScreen *scr, bool arrangeAll)
{
	WWindow *wwin;
	WAppIcon *aicon;

	int head;
	const int heads = wXineramaHeads(scr);

	struct HeadVars {
		int pf;		/* primary axis */
		int sf;		/* secondary axis */
		int fullW;
		int fullH;
		int pi, si;
		int sx1, sx2, sy1, sy2;	/* screen boundary */
		int sw, sh;
		int xo, yo;
		int xs, ys;
	} *vars;

	int isize = w_global.config->icon_size;

	vars = (struct HeadVars *)wmalloc(sizeof(struct HeadVars) * heads);

	for (head = 0; head < heads; ++head) {
		WArea area = wGetUsableAreaForHead(scr, head, NULL, false);
		WMRect rect;

		if (scr->dock && wAppIconTouchesHead(scr->dock->icon_array[0], head))
			wDockInsetArea(scr, &area);

		rect = wmkrect(area.x1, area.y1, area.x2 - area.x1, area.y2 - area.y1);

		vars[head].pi = vars[head].si = 0;
		vars[head].sx1 = rect.pos.x;
		vars[head].sy1 = rect.pos.y;
		vars[head].sw = rect.size.width;
		vars[head].sh = rect.size.height;
		vars[head].sx2 = vars[head].sx1 + vars[head].sw;
		vars[head].sy2 = vars[head].sy1 + vars[head].sh;
		vars[head].sw = isize * (vars[head].sw / isize);
		vars[head].sh = isize * (vars[head].sh / isize);
		vars[head].fullW = (vars[head].sx2 - vars[head].sx1) / isize;
		vars[head].fullH = (vars[head].sy2 - vars[head].sy1) / isize;

		/* icon yard boundaries */
		if (w_global.config->icon_yard & IY_VERT) {
			vars[head].pf = vars[head].fullH;
			vars[head].sf = vars[head].fullW;
		} else {
			vars[head].pf = vars[head].fullW;
			vars[head].sf = vars[head].fullH;
		}
		if (w_global.config->icon_yard & IY_RIGHT) {
			vars[head].xo = vars[head].sx2 - isize;
			vars[head].xs = -1;
		} else {
			vars[head].xo = vars[head].sx1;
			vars[head].xs = 1;
		}
		if (w_global.config->icon_yard & IY_TOP) {
			vars[head].yo = vars[head].sy1;
			vars[head].ys = 1;
		} else {
			vars[head].yo = vars[head].sy2 - isize;
			vars[head].ys = -1;
		}
	}

#define X ((w_global.config->icon_yard & IY_VERT) \
	? vars[head].xo + vars[head].xs*(vars[head].si*isize) \
	: vars[head].xo + vars[head].xs*(vars[head].pi*isize))

#define Y ((w_global.config->icon_yard & IY_VERT) \
	? vars[head].yo + vars[head].ys*(vars[head].pi*isize) \
	: vars[head].yo + vars[head].ys*(vars[head].si*isize))

	/* arrange application icons */
	aicon = scr->app_icon_list;
	/* reverse them to avoid unnecessarily sliding of icons */
	while (aicon && aicon->next)
		aicon = aicon->next;

	while (aicon) {
		if (!aicon->docked) {
			/* CHECK: can icon be NULL here ? */
			/* The intention here is to place the AppIcon on the head that
			 * contains most of the applications _main_ window. */
			head = wGetHeadForWindow(aicon->icon->owner);

			if (aicon->x_pos != X || aicon->y_pos != Y) {
#ifdef USE_ANIMATIONS
				if (!w_global.config->no_animations && !w_global.display_off)
					slide_window(aicon->icon->core->window, aicon->x_pos, aicon->y_pos, X, Y);
#endif /* USE_ANIMATIONS */
			}
			wAppIconMove(aicon, X, Y);
			vars[head].pi++;
			if (vars[head].pi >= vars[head].pf) {
				vars[head].pi = 0;
				vars[head].si++;
			}
		}
		aicon = aicon->prev;
	}

	/* arrange miniwindows */
	wwin = scr->focused_window;
	/* reverse them to avoid unnecessarily shuffling */
	while (wwin && wwin->prev)
		wwin = wwin->prev;

	while (wwin) {
		if (wwin->icon && wwin->flags.miniaturized && !wwin->flags.hidden &&
		    (WWIN_ON_WORKSPACE(wwin, scr->current_workspace) ||
		     w_global.config->sticky_icons)) {

			head = wGetHeadForWindow(wwin);

			if (arrangeAll || !wwin->flags.icon_moved) {
				if (wwin->icon_x != X || wwin->icon_y != Y)
					move_window(wwin->icon->core->window, wwin->icon_x, wwin->icon_y, X, Y);

				wwin->icon_x = X;
				wwin->icon_y = Y;

				vars[head].pi++;
				if (vars[head].pi >= vars[head].pf) {
					vars[head].pi = 0;
					vars[head].si++;
				}
			}
		}
		if (arrangeAll)
			wwin->flags.icon_moved = 0;
		/* we reversed the order, so we use next */
		wwin = wwin->next;
	}

	wfree(vars);
}

void wSelectWindow(WWindow *wwin, bool flag)
{
	WScreen *scr = wwin->screen_ptr;

	if (flag) {
		wwin->flags.selected = 1;
		if (wwin->frame->selected_border_pixel)
			xcb_change_window_attributes(wdisp->conn, wwin->frame->core->window, XCB_CW_BORDER_PIXEL, (const uint32_t[]){ *wwin->frame->selected_border_pixel });
		else
			xcb_change_window_attributes(wdisp->conn, wwin->frame->core->window, XCB_CW_BORDER_PIXEL, (const uint32_t[]){ scr->white_pixel });

		if (!HAS_BORDER(wwin))
			xcb_configure_window(wdisp->conn, wwin->frame->core->window, XCB_CONFIG_WINDOW_BORDER_WIDTH, (const uint32_t[]){ wFrameBorderWidth(wwin) });

		if (!scr->selected_windows)
			scr->selected_windows = WMCreateArray(4);
		WMAddToArray(scr->selected_windows, wwin);
	} else {
		wwin->flags.selected = 0;
		if (wwin->flags.focused) {
			if (wwin->frame->focused_border_pixel)
				xcb_change_window_attributes(wdisp->conn, wwin->frame->core->window, XCB_CW_BORDER_PIXEL, (const uint32_t[]){ *wwin->frame->focused_border_pixel });
			else
				xcb_change_window_attributes(wdisp->conn, wwin->frame->core->window, XCB_CW_BORDER_PIXEL, (const uint32_t[]){ scr->frame_focused_border_pixel });
		} else {
			if (wwin->frame->border_pixel)
				xcb_change_window_attributes(wdisp->conn, wwin->frame->core->window, XCB_CW_BORDER_PIXEL, (const uint32_t[]){ *wwin->frame->border_pixel });
			else
				xcb_change_window_attributes(wdisp->conn, wwin->frame->core->window, XCB_CW_BORDER_PIXEL, (const uint32_t[]){ scr->frame_border_pixel });
		}

		if (!HAS_BORDER(wwin))
			xcb_configure_window(wdisp->conn, wwin->frame->core->window, XCB_CONFIG_WINDOW_BORDER_WIDTH, (const uint32_t[]){ 0 });

		if (scr->selected_windows)
			WMRemoveFromArray(scr->selected_windows, wwin);
	}
}

void wMakeWindowVisible(WWindow *wwin)
{
	if (!wWindowIsVisibleOnAnyHead(wwin)) {
		if (w_global.config->per_head_workspaces) {
			WScreen *scr = wwin->screen_ptr;
			int ws = WWIN_PRIMARY_WS(wwin);
			int head = wWindowEffectiveHead(wwin);

			/* Prefer the window's own head, but never push a
			 * pinned workspace onto a head that WorkspaceOutputs
			 * forbids, fall back to a head that may show it. */
			if (!wWorkspaceIsAllowedOnHead(scr, ws, head))
				head = wHeadShowingWorkspace(scr, ws);
			if (wWorkspaceIsAllowedOnHead(scr, ws, head))
				wWorkspaceForceChangeOnHead(scr, ws, head);
		} else {
			wWorkspaceChange(wwin->screen_ptr, WWIN_PRIMARY_WS(wwin));
		}
	}

	if (wwin->flags.shaded)
		wUnshadeWindow(wwin);

	if (wwin->flags.hidden) {
		WApplication *app;

		app = wApplicationOf(wwin->main_window);
		if (app) {
			/* trick to get focus to this window */
			app->last_focused = wwin;
			wUnhideApplication(app, false, false);
		} else if (!wwin->flags.miniaturized) {
			/* no application to ride, and the miniaturized
			 * case recovers through the deiconify below */
			wUnhideWindow(wwin, NULL, 0, 0, false, false);
		}
	}
	if (wwin->flags.miniaturized) {
		wDeiconifyWindow(wwin);
	} else {
		if (!WFLAGP(wwin, no_focusable))
			wSetFocusTo(wwin->screen_ptr, wwin);
		wRaiseFrame(wwin->frame->core);
	}
}

void movePointerToWindowCenter(WWindow *wwin)
{
	if (!w_global.config->pointer_with_half_max_windows)
		return;

	xcb_change_window_attributes(wdisp->conn, wwin->client_win, XCB_CW_EVENT_MASK, (const uint32_t[]){ wwin->event_mask });
	wXWarpPointer(wdisp->conn, XCB_NONE, wwin->screen_ptr->root_win, 0, 0, 0, 0,
			wwin->frame_x + wwin->client.width / 2,
			wwin->frame_y + wwin->client.height / 2);
	wXFlush(wdisp->conn);
}

/* Focusable client window under the pointer, or NULL if on the root.
 * exclude is the window being minimized/hidden/closed (may be NULL). */
WWindow *wWindowUnderPointerForFocus(WScreen *scr, WWindow *exclude)
{
	unsigned int mask;
	int foo;
	xcb_window_t bar, win;
	WWindow *tmp;

	if (!wXQueryPointer(wdisp->conn, scr->root_win, &bar, &win,
			   &foo, &foo, &foo, &foo, &mask))
		return NULL;

	tmp = wWindowFor(win);
	if (!tmp || tmp == exclude)
		return NULL;
	if (WFLAGP(tmp, no_focusable))
		return NULL;
	if (tmp->flags.hidden || tmp->flags.miniaturized)
		return NULL;
	if (!(tmp->flags.mapped || tmp->flags.shaded))
		return NULL;
	return tmp;
}

/*
 * Warp the pointer to the center of wwin's frame. Gated by
 * PointerFollowsFocus.
 *
 * Call this only from EXPLICIT focus paths where a window received
 * focus (new-window manage, cycling/switch panel, directional focus
 * keys, keyboard tab switching, workspace switch landing on a window,
 * honored _NET_ACTIVE_WINDOW, focus migration after close/hide/
 * iconify/deiconify). Never call it from pointer-driven paths
 * (EnterNotify sloppy focus, FocusIn handler, click-to-focus, tab-
 * strip clicks): warping on pointer motion would fight sloppy focus.
 */
void warpPointerToFocusedWindow(WWindow *wwin)
{
	xcb_window_t root_ret, win_ret;
	int rx, ry, wx, wy;
	unsigned int mask;
	int fx, fy, fw, fh;

	if (!w_global.config->pointer_follows_focus)
		return;
	if (!wwin || !wwin->frame || !wwin->frame->core)
		return;
	/* workspace-switch candidates are mapped by construction. These
	 * checks guard the close/destroy-time callers */
	if (!wwin->flags.mapped || wwin->flags.miniaturized || wwin->flags.hidden)
		return;

	/* Skip the warp when the pointer is already inside the frame, so
	 * cycling between two overlapping windows does not jitter the
	 * cursor onto the frame center each time. */
	fx = wwin->frame_x;
	fy = wwin->frame_y;
	fw = (int)wwin->frame->core->width;
	fh = (int)wwin->frame->core->height;
	if (wXQueryPointer(wdisp->conn, wwin->screen_ptr->root_win, &root_ret, &win_ret,
			 &rx, &ry, &wx, &wy, &mask)
	    && rx >= fx && rx < fx + fw && ry >= fy && ry < fy + fh)
		return;

	wXWarpPointer(wdisp->conn, XCB_NONE, wwin->screen_ptr->root_win, 0, 0, 0, 0,
			fx + fw / 2, fy + fh / 2);
	wXFlush(wdisp->conn);
}

/*
 * Warp the pointer to the center of the head the pointer is on. Gated
 * by PointerFollowsMonitor. Call on workspace switch to an empty
 * workspace (no window to follow), so the pointer does not leap to
 * another head on a multi-head screen when merely switching
 * workspaces. Never call it from pointer-driven paths.
 *
 * Falls back to the whole-screen center when Xinerama is off or the
 * head lookup fails.
 */
void warpPointerToMonitor(WScreen *scr)
{
	WMRect r;
	int cx, cy;
	int head;

	if (!w_global.config->pointer_follows_monitor)
		return;
	if (!scr)
		return;

	if (scr->xine_info.count > 1) {
		head = wGetHeadForPointerLocation(scr);
		if (head < 0 || head >= scr->xine_info.count)
			head = scr->xine_info.primary_head;
		if (head < 0 || head >= scr->xine_info.count)
			head = 0;
		r = scr->xine_info.screens[head];
		cx = r.pos.x + (int)r.size.width  / 2;
		cy = r.pos.y + (int)r.size.height / 2;
	} else {
		cx = scr->scr_width  / 2;
		cy = scr->scr_height / 2;
	}

	wXWarpPointer(wdisp->conn, XCB_NONE, scr->root_win, 0, 0, 0, 0, cx, cy);
	wXFlush(wdisp->conn);
}

/*
 * Warp pointer to the center of the next monitor, cycling.
 *
 * Monitors are the Xinerama/RandR heads of every managed screen,
 * visited in (screen, head) order starting from the head the pointer
 * is on. With one head per screen this reduces to hopping from X11
 * screen to X11 screen as before.
 *
 * Gated by PointerFollowsMonitor so the previously-unconditional
 * WKBD_SWITCH_SCREEN warp can be turned off.
 */
void warpPointerToNextMonitor(WScreen *scr)
{
	WMRect rect;
	WScreen *target;
	int total, current, next;
	int i, h;

	if (!w_global.config->pointer_follows_monitor)
		return;

	total = 0;
	for (i = 0; i < wdisp->screen_count; i++)
		total += wHeadCount(wScreenWithNumber(i));

	if (total <= 1)
		return;

	/* flat index of the monitor the pointer is currently on */
	current = 0;
	for (i = 0; i < wdisp->screen_count; i++) {
		if (wScreenWithNumber(i) == scr) {
			h = wGetHeadForPointerLocation(scr);
			if (h < 0 || h >= wHeadCount(scr))
				h = 0;
			current += h;
			break;
		}
		current += wHeadCount(wScreenWithNumber(i));
	}

	next = (current + 1) % total;

	/* map the flat index back to a (screen, head) pair */
	target = NULL;
	for (i = 0; i < wdisp->screen_count; i++) {
		target = wScreenWithNumber(i);
		h = wHeadCount(target);
		if (next < h)
			break;
		next -= h;
		target = NULL;
	}
	if (!target)
		return;

	rect = wGetRectForHead(target, next);
	wXWarpPointer(wdisp->conn, XCB_NONE, target->root_win, 0, 0, 0, 0,
			rect.pos.x + (int)rect.size.width / 2,
			rect.pos.y + (int)rect.size.height / 2);
	wXFlush(wdisp->conn);
}

/*
 * Do the animation while shading (called with what = SHADE)
 * or unshading (what = UNSHADE).
 */
#ifdef USE_ANIMATIONS
static void shade_animate(WWindow *wwin, bool what)
{
	int y, s, w, h;
	int il, it;
	time_t time0 = time(NULL);

	wFrameInnerBorderWidths(wwin, &il, NULL, &it, NULL);

	if (wwin->flags.skip_next_animation || w_global.config->no_animations
	    || w_global.display_off)
		return;

	switch (what) {
	case SHADE:
		if (!wwin->screen_ptr->flags.startup) {
			/* do the shading animation */
			h = wwin->frame->core->height;
			s = h / SHADE_STEPS;
			if (s < 1)
				s = 1;
			w = wwin->frame->core->width;
			y = wwin->frame->top_width;
			while (h > wwin->frame->top_width + 1) {
				xcb_configure_window(wdisp->conn, wwin->client_win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ il, y });
				xcb_configure_window(wdisp->conn, wwin->frame->core->window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, (const uint32_t[]){ w, h });
				wXFlush(wdisp->conn);

				if (time(NULL) - time0 > MAX_ANIMATION_TIME)
					break;

				if (SHADE_DELAY > 0)
					wusleep(SHADE_DELAY * 1000L);
				else
					wusleep(10);
				h -= s;
				y -= s;
			}
			xcb_configure_window(wdisp->conn, wwin->client_win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ il, wwin->frame->top_width + it });
		}
		break;

	case UNSHADE:
		h = wwin->frame->top_width + wwin->frame->bottom_width;
		y = wwin->frame->top_width - wwin->client.height;
		s = abs(y) / SHADE_STEPS;
		if (s < 1)
			s = 1;
		w = wwin->frame->core->width;
		xcb_configure_window(wdisp->conn, wwin->client_win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ il, y });
		if (s > 0) {
			while (h < wwin->client.height + wwin->frame->top_width + wwin->frame->bottom_width) {
				xcb_configure_window(wdisp->conn, wwin->frame->core->window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, (const uint32_t[]){ w, h });
				xcb_configure_window(wdisp->conn, wwin->client_win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ il, y });
				wXFlush(wdisp->conn);
				if (SHADE_DELAY > 0)
					wusleep(SHADE_DELAY * 2000L / 3);
				else
					wusleep(10);
				h += s;
				y += s;

				if (time(NULL) - time0 > MAX_ANIMATION_TIME)
					break;
			}
		}
		xcb_configure_window(wdisp->conn, wwin->client_win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ il, wwin->frame->top_width + it });
		break;
	}
}
#endif
