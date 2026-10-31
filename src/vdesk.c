/*
 * vdesk.c- Viewport (pannable desktop) panning
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 2026 Window Maker Team
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

#ifdef VIRTUAL_DESKTOP

#include <limits.h>
#include <stdint.h>

#include <WINGs/WUtil.h>
#include <WINGs/WINGsP.h>

#include "WindowMaker.h"
#include "menu.h"
#include "workspace.h"
#include "screen.h"
#include "window.h"
#include "xinerama.h"
#include "framewin.h"
#include "stacking.h"
#include "wmspec.h"
#include "vdesk.h"
#include "layout.h"
#include "hotcorner.h"

typedef struct _delay_configure {
	xcb_window_t *wins;
	int count;
	WMHandlerID timer;
} _delay_configure;

static void sendConfigureNotify(_delay_configure *delay)
{
	int i;

	for (i = 0; i < delay->count; i++) {
		WWindow *wwin = wWindowFor(delay->wins[i]);

		if (wwin)
			wWindowSynthConfigureNotify(wwin);
	}
	wfree(delay->wins);
	delay->wins = NULL;
	delay->count = 0;
	delay->timer = NULL;
}

void wVdeskUpdateGeometry(WScreen *scr, int workspace)
{
	int most_left, most_right, most_top, most_bottom;
	WWindow *wwin;
	WWorkspace *wptr;

	int heads = wXineramaHeads(scr);
	typedef int strut_t[4];
	strut_t *strut;
	int head, i;

	if (workspace < 0 || workspace >= scr->workspace_count)
		return;

	wptr = scr->workspaces[workspace];
	strut = wmalloc(sizeof(strut_t) * heads);

	for (head = 0; head < heads; ++head) {
		WMRect rect = wGetRectForHead(scr, head);
		WArea area = wGetUsableAreaForHead(scr, head, NULL, false);
		strut[head][0] = area.x1 - rect.pos.x;
		strut[head][1] = rect.pos.x + rect.size.width - area.x2;
		strut[head][2] = area.y1 - rect.pos.y;
		strut[head][3] = rect.pos.y + rect.size.height - area.y2;
	}

	wwin = scr->focused_window;
	most_right = 0;
	most_bottom = 0;
	most_left = 0;
	most_top = 0;
	for (; wwin; wwin = wwin->prev) {
		if (WWIN_ON_WORKSPACE(wwin, workspace)) {
			if (!wwin->flags.miniaturized && !IS_VSTUCK(wwin)
			    && !WFLAGP(wwin, dont_move_off)
			    && !wwin->flags.hidden) {
				int border_w = HAS_BORDER(wwin) ? wFrameBorderWidth(wwin) : 0;

				head = wGetHeadForWindow(wwin);

				/* frame_x/y are the outer border-inclusive corner
				 * (XMoveWindow positions the border), so left/top need
				 * no border and right/bottom need 2*border_w */
				i = wwin->frame_x + wptr->view_x - strut[head][0];
				if (i < most_left)
					most_left = i;
				i = wwin->frame_x + wptr->view_x
				    + wwin->frame->core->width + 2 * border_w + strut[head][1];
				if (i > most_right)
					most_right = i;
				i = wwin->frame_y + wptr->view_y - strut[head][2];
				if (i < most_top)
					most_top = i;
				i = wwin->frame_y + wptr->view_y
				    + wwin->frame->core->height + 2 * border_w + strut[head][3];
				if (i > most_bottom)
					most_bottom = i;
			}
		}
	}

	/* The desktop origin tracks windows dragged past the top/left edge
	 * (negative world coords) so the clamp below can reach them. It stays
	 * at 0 when no window has crossed the origin. */
	wptr->vdesk_x = WMIN(most_left, 0);
	wptr->vdesk_y = WMIN(most_top, 0);
	wptr->vdesk_w =
		WMAX(most_right, scr->scr_width) - wptr->vdesk_x;
	wptr->vdesk_h =
		WMAX(most_bottom, scr->scr_height) - wptr->vdesk_y;

	wfree(strut);
}

bool wVdeskPerHeadBlocked(WScreen *scr, int workspace)
{
	if (w_global.config->per_head_workspaces)
		return true;

	if (workspace >= 0 && workspace < scr->workspace_count
	    && scr->workspaces[workspace]->head_name_count > 0)
		return true;

	return false;
}

bool wVdeskSetViewport(WScreen *scr, int workspace, int view_x, int view_y)
{
	bool adjust_flag = false;
	int diff_x, diff_y;
	int old_view_x, old_view_y;
	int64_t max_x, max_y;
	int j;
	bool seen;
	static _delay_configure delay_configure = { NULL, 0, NULL };
	WWorkspace *wptr;
	WWindow *wwin;

	if (!w_global.config->vdesk_enable)
		return false;

	if (workspace < 0 || workspace >= scr->workspace_count)
		return false;

	if (scr->workspaces[workspace]->layout != WLAYOUT_FLOAT)
		return false;

	if (wVdeskPerHeadBlocked(scr, workspace))
		return false;

	wptr = scr->workspaces[workspace];

	old_view_x = wptr->view_x;
	old_view_y = wptr->view_y;

	wVdeskUpdateGeometry(scr, workspace);

	/* Clamp within the finite desktop, now anchored at the world origin
	 * (vdesk_x/y) rather than 0: a window dragged off the left/top edge
	 * gives the origin a negative value, and the lower clamp follows it
	 * so the viewport can pan past 0 to reveal that window. The upper
	 * bound is anchored at vdesk_x + vdesk_w (world right/bottom edge);
	 * compare in 64-bit so a caller-supplied INT_MAX view cannot wrap
	 * the sum and make both clamps inert. */
	max_x = (int64_t)wptr->vdesk_x + wptr->vdesk_w - scr->scr_width;
	max_y = (int64_t)wptr->vdesk_y + wptr->vdesk_h - scr->scr_height;
	if (view_x > max_x)
		view_x = (int)max_x;
	if (view_x < wptr->vdesk_x)
		view_x = wptr->vdesk_x;
	if (view_y > max_y)
		view_y = (int)max_y;
	if (view_y < wptr->vdesk_y)
		view_y = wptr->vdesk_y;

	diff_x = old_view_x - view_x;
	diff_y = old_view_y - view_y;
	if (!diff_x && !diff_y)
		return false;

	wptr->view_x = view_x;
	wptr->view_y = view_y;

	wNETWMUpdateDesktop(scr);

	for (wwin = scr->focused_window; wwin; wwin = wwin->prev) {
		if (WWIN_ON_WORKSPACE(wwin, workspace) && !IS_VSTUCK(wwin)
		    && !WFLAGP(wwin, dont_move_off)
		    && !wwin->flags.miniaturized
		    && !wwin->flags.hidden
		    && !wwin->flags.fullscreen) {
			wwin->frame_x += diff_x;
			wwin->frame_y += diff_y;
			wWindowMove(wwin, wwin->frame_x, wwin->frame_y);
			adjust_flag = true;

			/* the delayed notify reads the final geometry, so
			 * a window moved again before the fire needs no
			 * second entry and no second timer */
			seen = false;
			for (j = 0; j < delay_configure.count; j++) {
				if (delay_configure.wins[j] == wwin->client_win) {
					seen = true;
					break;
				}
			}
			if (!seen) {
				delay_configure.wins = wrealloc(delay_configure.wins,
					sizeof(xcb_window_t) * (delay_configure.count + 1));
				delay_configure.wins[delay_configure.count++] = wwin->client_win;
			}
		}
	}

	if (delay_configure.count > 0 && !delay_configure.timer)
		delay_configure.timer = WMAddTimerHandler(200,
			(WMCallback *) sendConfigureNotify, &delay_configure);

	return adjust_flag;
}

#define vec_add(a, b) wmkpoint((a).x + (b).x, (a).y + (b).y)
#define vec_dot(a, b) ((a).x * (b).x + (a).y * (b).y)
#define vec_scale(a, s) wmkpoint((a).x * (s), (a).y * (s))
#define vec_scale2(a, s, t) wmkpoint((a).x * (s), (a).y * (t))

#ifndef HAS_BORDER
#define HAS_BORDER(w) (!(WFLAGP((w), no_border)))
#endif

#define updateMinimum(l,p,ml,mp) do { if (cmp(l) && (l)<(ml)) { (ml)=(l); (mp)=(p); } } while(0)

static bool cmp_gez(int i) { return (i >= 0); }
static bool cmp_gz(int i)  { return (i > 0); }

/* closest window-edge corner in the pan direction; no qualifying window
 * yields (0,0) with *found false, not an edge at distance 0 */
static WMPoint getClosestEdge(WScreen *scr, WMPoint direction, bool (*cmp)(int),
			     bool *found)
{
	WMPoint closest = wmkpoint(0, 0);
	int closest_len = INT_MAX;
	WWindow *wwin;

	if (found)
		*found = false;

	for (wwin = scr->focused_window; wwin; wwin = wwin->prev) {
		if (WWIN_ON_WORKSPACE(wwin, scr->current_workspace)) {
			if (!wwin->flags.miniaturized && !IS_VSTUCK(wwin)
			    && !WFLAGP(wwin, dont_move_off) && !wwin->flags.hidden) {
				/* frame_x is the OUTER corner (XMoveWindow
				 * positions the border-inclusive corner), so
				 * left/top need no border adjustment and
				 * right/bottom need +2*border_w for the border
				 * on both sides. See wVdeskUpdateGeometry. */
				int border_w = HAS_BORDER(wwin) ? wFrameBorderWidth(wwin) : 0;
				int len;
				int x1, x2, y1, y2;
				int head = wGetHeadForWindow(wwin);
				WArea area = wGetUsableAreaForHead(scr, head, NULL, false);
				WMPoint p;

				x1 = wwin->frame_x - area.x1;
				y1 = wwin->frame_y - area.y1;
				x2 = wwin->frame_x + wwin->frame->core->width + 2 * border_w - area.x2;
				y2 = wwin->frame_y + wwin->frame->core->height + 2 * border_w - area.y2;

				p = wmkpoint(x1, y1); len = vec_dot(direction, p);
				updateMinimum(len, p, closest_len, closest);
				p = wmkpoint(x1, y2); len = vec_dot(direction, p);
				updateMinimum(len, p, closest_len, closest);
				p = wmkpoint(x2, y1); len = vec_dot(direction, p);
				updateMinimum(len, p, closest_len, closest);
				p = wmkpoint(x2, y2); len = vec_dot(direction, p);
				updateMinimum(len, p, closest_len, closest);
			}
		}
	}

	if (found)
		*found = closest_len != INT_MAX;

	return closest;
}

void wVdeskKeyboardMoveDesktop(WScreen *scr, WMPoint direction)
{
	WMPoint edge;
	int len, x, y;
	WMPoint step;

	if (scr->workspaces[scr->current_workspace]->layout != WLAYOUT_FLOAT)
		return;

	edge = getClosestEdge(scr, direction, cmp_gz, NULL);
	len = vec_dot(edge, direction);
	step = vec_scale(direction, len);
	x = scr->workspaces[scr->current_workspace]->view_x;
	y = scr->workspaces[scr->current_workspace]->view_y;
	wVdeskSetViewport(scr, scr->current_workspace, x + step.x, y + step.y);
}

void wVdeskHome(WScreen *scr)
{
	if (scr->workspaces[scr->current_workspace]->layout != WLAYOUT_FLOAT)
		return;
	wVdeskSetViewport(scr, scr->current_workspace, 0, 0);
}

void wVdeskCenter(WScreen *scr, WWindow *wwin)
{
	int cx, cy, head;
	WArea area;

	if (!wwin || scr->workspaces[scr->current_workspace]->layout != WLAYOUT_FLOAT)
		return;
	head = wGetHeadForWindow(wwin);
	area = wGetUsableAreaForHead(scr, head, NULL, false);
	cx = wwin->frame_x + wwin->frame->core->width / 2
		- (area.x1 + (area.x2 - area.x1) / 2);
	cy = wwin->frame_y + wwin->frame->core->height / 2
		- (area.y1 + (area.y2 - area.y1) / 2);
	wVdeskSetViewport(scr, scr->current_workspace, cx, cy);
}

void wVdeskPin(WScreen *scr, WWindow *wwin)
{
	if (!wwin)
		return;
	WSETPERWINDOWFLAG(wwin, virtual_stick, !WFLAGP(wwin, virtual_stick));
	CommitStacking(scr);
}

void wVdeskDragPan(WScreen *scr, xcb_generic_event_t *event)
{
	bool done = false;
	int last_x, last_y;
	int x, y;
	xcb_generic_event_t *ev = NULL;

	/* A tiled workspace cannot pan because wLayoutApply would snap
	 * windows back to tiled positions, destroying the pan. Also
	 * short-circuit when the feature is disabled at runtime. */
	if (!w_global.config->vdesk_enable)
		return;
	if (scr->workspaces[scr->current_workspace]->layout != WLAYOUT_FLOAT)
		return;
	if (wVdeskPerHeadBlocked(scr, scr->current_workspace))
		return;

	/* XGrabPointer flips the cursor to WCUR_MOVE for the duration of
	 * the drag and routes all pointer events to us until ButtonRelease.
	 * PointerMotionMask (not ButtonMotionMask) matches the edge-scroll
	 * grab in wVdeskMouseMoveDesktop: it selects all motion events
	 * during the grab, which is what a free-form drag needs. */
	if (wXGrabPointer(wdisp->conn, ((xcb_button_press_event_t *)event)->root, false,
			 XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_RELEASE,
			 XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
			 XCB_NONE, wdisp->cursor[WCUR_MOVE], XCB_CURRENT_TIME) != WXGrabSuccess)
		return;
	/* Discard the ButtonPress that brought us here and any stale motion
	 * so the first WMMaskEvent below is a fresh sample. Mirrors the
	 * edge-scroll grab at wVdeskMouseMoveDesktop. */
	wXFlush(wdisp->conn);
	while (WXEvqTakeMask(wdisp->conn, XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_PRESS, XCB_NONE, &ev)) {
		free(ev);
		ev = NULL;
	}

	last_x = ((xcb_button_press_event_t *)event)->root_x;
	last_y = ((xcb_button_press_event_t *)event)->root_y;

	while (!done) {
		free(ev);
		ev = NULL;
		WMMaskEvent(wdisp->conn, XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_RELEASE, &ev);
		if (!ev)
			break;

		if ((ev->response_type & 0x7f) == XCB_MOTION_NOTIFY) {
			int dx = (((xcb_motion_notify_event_t *)ev)->root_x) - last_x;
			int dy = (((xcb_motion_notify_event_t *)ev)->root_y) - last_y;
			last_x = (((xcb_motion_notify_event_t *)ev)->root_x);
			last_y = (((xcb_motion_notify_event_t *)ev)->root_y);

			if (dx != 0 || dy != 0) {
				/* Window-move model: pan = move windows by -diff,
				 * so the camera moves opposite to the drag. Dragging
				 * right reveals content to the left, exactly like the
				 * edge-scroll path (drag direction = pan direction of
				 * the pointer, content scrolls the other way). */
				x = scr->workspaces[scr->current_workspace]->view_x;
				y = scr->workspaces[scr->current_workspace]->view_y;
				wVdeskSetViewport(scr, scr->current_workspace,
						  x - dx, y - dy);
			}
		} else if ((ev->response_type & 0x7f) == XCB_BUTTON_RELEASE) {
			done = true;
		}
	}
	free(ev);
	ev = NULL;

	wXUngrabPointer(wdisp->conn, XCB_CURRENT_TIME);
}

/* Inactivity timeout for the edge-pan modal loop: a DPMS-wake EnterNotify
 * can strand the loop with no further motion, so abort after this. */
#define VDESK_PAN_IDLE_MS 500

static void panIdleTimeout(void *data)
{
	*(bool *)data = true;
}

void wVdeskMouseMoveDesktop(xcb_generic_event_t *event, WMPoint direction)
{
	static int lock = false;
	bool done = false;
	bool moved = true;
	bool timed_out = false;
	WMHandlerID pan_idle_timer = NULL;
	WScreen *scr = wScreenForRootWindow(((xcb_enter_notify_event_t *)event)->root);
	WMPoint old_pos = wmkpoint(((xcb_enter_notify_event_t *)event)->root_x, ((xcb_enter_notify_event_t *)event)->root_y);
	WMPoint step;
	int x, y;
	int resisted = 0;
	xcb_generic_event_t *ev = NULL;
	int head;
	WMRect head_rect;
	WMPoint center;

	if (!scr)
		return;

	/* run the loop in the geometry of the head whose edge sentinel
	 * fired, so the warp target, the pan step origin and the pointer
	 * clamps never reach past that head */
	head = wGetHeadForPoint(scr, old_pos);
	head_rect = wGetRectForHead(scr, head);
	center = wmkpoint(head_rect.pos.x + head_rect.size.width / 2,
			  head_rect.pos.y + head_rect.size.height / 2);

	if (lock)
		return;
	lock = true;

	if (scr->workspaces[scr->current_workspace]->layout != WLAYOUT_FLOAT)
		goto exit;

	if (wXGrabPointer(wdisp->conn, ((xcb_enter_notify_event_t *)event)->event, false,
			 XCB_EVENT_MASK_POINTER_MOTION, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
			 scr->root_win, wdisp->cursor[WCUR_EMPTY], XCB_CURRENT_TIME) != WXGrabSuccess) {
		step = vec_scale2(direction, w_global.config->vedge_hscrollspeed,
				  w_global.config->vedge_vscrollspeed);
		x = scr->workspaces[scr->current_workspace]->view_x;
		y = scr->workspaces[scr->current_workspace]->view_y;
		if (wVdeskSetViewport(scr, scr->current_workspace, x + step.x, y + step.y)) {
			step = vec_scale(direction, 2);
			wXWarpPointer(wdisp->conn, XCB_NONE, scr->root_win, 0, 0, 0, 0,
				     ((xcb_enter_notify_event_t *)event)->root_x - step.x,
				     ((xcb_enter_notify_event_t *)event)->root_y - step.y);
		}
		goto exit;
	}
	wXFlush(wdisp->conn);
	while (WXEvqTakeMask(wdisp->conn, XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_PRESS, XCB_NONE, &ev)) {
		free(ev);
		ev = NULL;
	}

	if (old_pos.x < head_rect.pos.x)
		old_pos.x = head_rect.pos.x;
	if (old_pos.y < head_rect.pos.y)
		old_pos.y = head_rect.pos.y;
	if (old_pos.x > head_rect.pos.x + head_rect.size.width)
		old_pos.x = head_rect.pos.x + head_rect.size.width;
	if (old_pos.y > head_rect.pos.y + head_rect.size.height)
		old_pos.y = head_rect.pos.y + head_rect.size.height;

	pan_idle_timer = WMAddTimerHandler(VDESK_PAN_IDLE_MS,
					    panIdleTimeout, &timed_out);
	while (!done && !timed_out) {
		if (moved) {
			wXWarpPointer(wdisp->conn, XCB_NONE, scr->root_win, 0, 0, 0, 0,
				     center.x, center.y);
			moved = false;
		}
		/* Inlined WMMaskEvent that also aborts when the idle timer
		 * fires, each iteration owns one event */
		free(ev);
		ev = NULL;
		while (!timed_out &&
		       !WXEvqTakeMask(wdisp->conn, XCB_EVENT_MASK_POINTER_MOTION, XCB_NONE, &ev))
			W_HandleInputEvents(true, xcb_get_file_descriptor(wdisp->conn));
		if (timed_out) {
			free(ev);
			ev = NULL;
			/* the one-shot timer already fired and was swept */
			pan_idle_timer = NULL;
			break;
		}
		WMDeleteTimerHandler(pan_idle_timer);
		pan_idle_timer = WMAddTimerHandler(VDESK_PAN_IDLE_MS,
						  panIdleTimeout, &timed_out);

		if ((ev->response_type & 0x7f) == XCB_MOTION_NOTIFY) {
			int step_len;
			step = wmkpoint((((xcb_motion_notify_event_t *)ev)->root_x) - center.x,
					(((xcb_motion_notify_event_t *)ev)->root_y) - center.y);
			step_len = vec_dot(step, direction);
			if (step_len < 0) {
				done = true;
			} else if (step_len > 0) {
				bool do_move = true;
				bool found_edge = false;
				int resist = w_global.config->vedge_resistance;
				WMPoint closest;
				int closest_len = INT_MAX;
				if (resist) {
					closest = getClosestEdge(scr, direction, cmp_gez, &found_edge);
					closest_len = vec_dot(direction, closest);
					/* no qualifying window: no resistance,
					 * nothing to attract to */
					if (!found_edge)
						closest_len = INT_MAX;
				}
				if (!closest_len) {
					resisted += step_len;
					do_move = resisted >= resist;
					if (do_move) {
						closest_len = INT_MAX;
						step_len = resisted - resist;
						resisted = 0;
					}
				}
				if (do_move) {
					if (closest_len <= (int)w_global.config->vedge_attraction)
						step = vec_scale(direction, closest_len);
					else
						step = vec_scale(direction, step_len);
					x = scr->workspaces[scr->current_workspace]->view_x;
					y = scr->workspaces[scr->current_workspace]->view_y;
					wVdeskSetViewport(scr, scr->current_workspace,
							  x + step.x, y + step.y);
					moved = true;
				}
			}
		}
	}
	WMDeleteTimerHandlerPtr(&pan_idle_timer);

	step = vec_add(old_pos, vec_scale(direction, -1));
	wXWarpPointer(wdisp->conn, XCB_NONE, scr->root_win, 0, 0, 0, 0, step.x, step.y);
	wXUngrabPointer(wdisp->conn, XCB_CURRENT_TIME);

 exit:
	free(ev);
	ev = NULL;
	WMDeleteTimerHandlerPtr(&pan_idle_timer);
	lock = false;
}

#define LEFT_EDGE   0x01
#define RIGHT_EDGE  0x02
#define TOP_EDGE    0x04
#define BOTTOM_EDGE 0x08
#define ALL_EDGES   0x0F
#define VEDGE_CORNER_APPROACH 24

WMHashTable *wVEdgeTable = NULL;

static void vdHandleEnter_u(xcb_generic_event_t *event) { wVdeskMouseMoveDesktop(event, VEC_UP); }
static void vdHandleEnter_d(xcb_generic_event_t *event) { wVdeskMouseMoveDesktop(event, VEC_DOWN); }
static void vdHandleEnter_l(xcb_generic_event_t *event) { wVdeskMouseMoveDesktop(event, VEC_LEFT); }
static void vdHandleEnter_r(xcb_generic_event_t *event) { wVdeskMouseMoveDesktop(event, VEC_RIGHT); }

static void createEdges(WScreen *scr)
{
	int i, j;
	uint32_t vmask;
	int heads = wXineramaHeads(scr);
	int *hasEdges;
	int thickness = WMAX(1, (int)w_global.config->vedge_bordersize);
	int nr_edges = 0;
	int max_edges = 4 * heads;
	int head;
	xcb_window_t *edges;

	if (scr->virtual_edges)
		return;

	hasEdges = wmalloc(sizeof(int) * heads);
	edges = wmalloc(sizeof(xcb_window_t) * max_edges);

	for (i = 0; i < heads; ++i)
		hasEdges[i] = ALL_EDGES;
	for (i = 0; i < heads; ++i) {
		WMRect i_rect = wGetRectForHead(scr, i);
		for (j = i + 1; j < heads; ++j) {
			WMRect j_rect = wGetRectForHead(scr, j);
			int vlen = (WMIN(i_rect.pos.y + i_rect.size.height,
					 j_rect.pos.y + j_rect.size.height) -
				    WMAX(i_rect.pos.y, j_rect.pos.y));
			int hlen = (WMIN(i_rect.pos.x + i_rect.size.width,
					 j_rect.pos.x + j_rect.size.width) -
				    WMAX(i_rect.pos.x, j_rect.pos.x));
			if (vlen > 0 && hlen == 0) {
				if (i_rect.pos.x < j_rect.pos.x) {
					hasEdges[i] &= ~RIGHT_EDGE;
					hasEdges[j] &= ~LEFT_EDGE;
				} else {
					hasEdges[j] &= ~RIGHT_EDGE;
					hasEdges[i] &= ~LEFT_EDGE;
				}
			} else if (vlen == 0 && hlen > 0) {
				if (i_rect.pos.y < j_rect.pos.y) {
					hasEdges[i] &= ~BOTTOM_EDGE;
					hasEdges[j] &= ~TOP_EDGE;
				} else {
					hasEdges[j] &= ~BOTTOM_EDGE;
					hasEdges[i] &= ~TOP_EDGE;
				}
			}
		}
	}

	wNETWMUpdateDesktop(scr);

	if (wVEdgeTable == NULL)
		wVEdgeTable = WMCreateHashTable(WMIntHashCallbacks);

	vmask = XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;

	for (head = 0; head < wXineramaHeads(scr); ++head) {
		WMRect rect = wGetRectForHead(scr, head);
		int width = (int)rect.size.width;
		int height = (int)rect.size.height;
		int guard = 0;

		if (w_global.config->hot_corners) {
			guard = wHotCornerEdgeForRect(rect) + VEDGE_CORNER_APPROACH;
			guard = WMIN(guard, WMIN(width, height) / 2);
		}

		if ((hasEdges[head] & TOP_EDGE) && width > 2 * guard) {
			edges[nr_edges] = xcb_generate_id(wdisp->conn);
			xcb_create_window(wdisp->conn, XCB_COPY_FROM_PARENT, edges[nr_edges],
					  scr->root_win, rect.pos.x + guard, rect.pos.y,
					  width - 2 * guard, thickness, 0,
					  XCB_WINDOW_CLASS_INPUT_ONLY, XCB_COPY_FROM_PARENT,
					  vmask, (const uint32_t[]){ true, XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_VISIBILITY_CHANGE });
			WMHashInsert(wVEdgeTable, (void *)(uintptr_t) edges[nr_edges], vdHandleEnter_u);
			++nr_edges;
		}
		if ((hasEdges[head] & BOTTOM_EDGE) && width > 2 * guard) {
			edges[nr_edges] = xcb_generate_id(wdisp->conn);
			xcb_create_window(wdisp->conn, XCB_COPY_FROM_PARENT, edges[nr_edges],
					  scr->root_win, rect.pos.x + guard,
					  rect.pos.y + rect.size.height - thickness,
					  width - 2 * guard, thickness, 0,
					  XCB_WINDOW_CLASS_INPUT_ONLY, XCB_COPY_FROM_PARENT,
					  vmask, (const uint32_t[]){ true, XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_VISIBILITY_CHANGE });
			WMHashInsert(wVEdgeTable, (void *)(uintptr_t) edges[nr_edges], vdHandleEnter_d);
			++nr_edges;
		}
		if ((hasEdges[head] & LEFT_EDGE) && height > 2 * guard) {
			edges[nr_edges] = xcb_generate_id(wdisp->conn);
			xcb_create_window(wdisp->conn, XCB_COPY_FROM_PARENT, edges[nr_edges],
					  scr->root_win, rect.pos.x, rect.pos.y + guard,
					  thickness, height - 2 * guard, 0,
					  XCB_WINDOW_CLASS_INPUT_ONLY, XCB_COPY_FROM_PARENT,
					  vmask, (const uint32_t[]){ true, XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_VISIBILITY_CHANGE });
			WMHashInsert(wVEdgeTable, (void *)(uintptr_t) edges[nr_edges], vdHandleEnter_l);
			++nr_edges;
		}
		if ((hasEdges[head] & RIGHT_EDGE) && height > 2 * guard) {
			edges[nr_edges] = xcb_generate_id(wdisp->conn);
			xcb_create_window(wdisp->conn, XCB_COPY_FROM_PARENT, edges[nr_edges],
					  scr->root_win,
					  rect.pos.x + rect.size.width - thickness, rect.pos.y + guard,
					  thickness, height - 2 * guard, 0,
					  XCB_WINDOW_CLASS_INPUT_ONLY, XCB_COPY_FROM_PARENT,
					  vmask, (const uint32_t[]){ true, XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_VISIBILITY_CHANGE });
			WMHashInsert(wVEdgeTable, (void *)(uintptr_t) edges[nr_edges], vdHandleEnter_r);
			++nr_edges;
		}
	}

	scr->virtual_nr_edges = nr_edges;
	scr->virtual_edges = edges;

	for (i = 0; i < scr->virtual_nr_edges; ++i)
		xcb_map_window(wdisp->conn, scr->virtual_edges[i]);
	wWorkspaceRaiseEdge(scr);

	wfree(hasEdges);
}

static void destroyEdges(WScreen *scr)
{
	int i;

	if (!scr->virtual_edges)
		return;

	for (i = 0; i < scr->virtual_nr_edges; ++i) {
		WMHashRemove(wVEdgeTable, (void *)(uintptr_t) scr->virtual_edges[i]);
		xcb_unmap_window(wdisp->conn, scr->virtual_edges[i]);
		xcb_destroy_window(wdisp->conn, scr->virtual_edges[i]);
	}

	wfree(scr->virtual_edges);
	scr->virtual_edges = NULL;
	scr->virtual_nr_edges = 0;
}

void wWorkspaceUpdateEdge(WScreen *scr)
{
	destroyEdges(scr);
	if (w_global.config->vdesk_enable && !wVdeskPerHeadBlocked(scr, -1))
		createEdges(scr);
}

void wWorkspaceRaiseEdge(WScreen *scr)
{
	static int toggle;
	int i;

	if (!scr->virtual_edges)
		return;

	/* InputOnly edge windows, never in the stacking list, direct
	 * configure */
	if (toggle) {
		for (i = 0; i < scr->virtual_nr_edges; ++i)
			xcb_configure_window(wdisp->conn, scr->virtual_edges[i], XCB_CONFIG_WINDOW_STACK_MODE, (const uint32_t[]){ XCB_STACK_MODE_ABOVE });
	} else {
		for (i = scr->virtual_nr_edges - 1; i >= 0; --i)
			xcb_configure_window(wdisp->conn, scr->virtual_edges[i], XCB_CONFIG_WINDOW_STACK_MODE, (const uint32_t[]){ XCB_STACK_MODE_ABOVE });
	}
	toggle ^= 1;
	wHotCornersRaise(scr);
}

void wWorkspaceLowerEdge(WScreen *scr)
{
	int i;
	for (i = 0; i < scr->virtual_nr_edges; ++i)
		xcb_configure_window(wdisp->conn, scr->virtual_edges[i], XCB_CONFIG_WINDOW_STACK_MODE, (const uint32_t[]){ XCB_STACK_MODE_BELOW });
}

#endif /* VIRTUAL_DESKTOP */
