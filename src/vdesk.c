/*
 * vdesk.c- Viewport (pannable desktop) panning
 *
 *  Window Maker window manager
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

typedef struct _delay_configure {
	Window *wins;
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
		WArea area = wGetUsableAreaForHead(scr, head, NULL, False);
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

Bool wVdeskPerHeadBlocked(WScreen *scr, int workspace)
{
	if (w_global.config->per_head_workspaces)
		return True;

	if (workspace >= 0 && workspace < scr->workspace_count
	    && scr->workspaces[workspace]->head_name_count > 0)
		return True;

	return False;
}

Bool wVdeskSetViewport(WScreen *scr, int workspace, int view_x, int view_y)
{
	Bool adjust_flag = False;
	int diff_x, diff_y;
	int old_view_x, old_view_y;
	int64_t max_x, max_y;
	int j;
	Bool seen;
	static _delay_configure delay_configure = { NULL, 0, NULL };
	WWorkspace *wptr;
	WWindow *wwin;

	if (!w_global.config->vdesk_enable)
		return False;

	if (workspace < 0 || workspace >= scr->workspace_count)
		return False;

	if (scr->workspaces[workspace]->layout != WLAYOUT_FLOAT)
		return False;

	if (wVdeskPerHeadBlocked(scr, workspace))
		return False;

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
		return False;

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
			adjust_flag = True;

			/* the delayed notify reads the final geometry, so
			 * a window moved again before the fire needs no
			 * second entry and no second timer */
			seen = False;
			for (j = 0; j < delay_configure.count; j++) {
				if (delay_configure.wins[j] == wwin->client_win) {
					seen = True;
					break;
				}
			}
			if (!seen) {
				delay_configure.wins = wrealloc(delay_configure.wins,
					sizeof(Window) * (delay_configure.count + 1));
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

static Bool cmp_gez(int i) { return (i >= 0); }
static Bool cmp_gz(int i)  { return (i > 0); }

static WMPoint getClosestEdge(WScreen *scr, WMPoint direction, Bool (*cmp)(int))
{
	WMPoint closest = wmkpoint(0, 0);
	int closest_len = INT_MAX;
	WWindow *wwin;

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
				WArea area = wGetUsableAreaForHead(scr, head, NULL, False);
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

	return closest;
}

void wVdeskKeyboardMoveDesktop(WScreen *scr, WMPoint direction)
{
	WMPoint edge;
	int len, x, y;
	WMPoint step;

	if (scr->workspaces[scr->current_workspace]->layout != WLAYOUT_FLOAT)
		return;

	edge = getClosestEdge(scr, direction, cmp_gz);
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
	area = wGetUsableAreaForHead(scr, head, NULL, False);
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
	WSETUFLAG(wwin, virtual_stick, !WFLAGP(wwin, virtual_stick));
	CommitStacking(scr);
}

void wVdeskDragPan(WScreen *scr, XEvent *event)
{
	Bool done = False;
	int last_x, last_y;
	int x, y;
	XEvent ev;

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
	if (XGrabPointer(dpy, event->xbutton.root, False,
			 PointerMotionMask | ButtonReleaseMask,
			 GrabModeAsync, GrabModeAsync,
			 None, wPreferences.cursor[WCUR_MOVE], CurrentTime) != GrabSuccess)
		return;
	/* Discard the ButtonPress that brought us here and any stale motion
	 * so the first WMMaskEvent below is a fresh sample. Mirrors the
	 * edge-scroll grab at wVdeskMouseMoveDesktop. */
	XFlush(dpy);
	while (WXEvqTakeMask(dpy, PointerMotionMask | ButtonPressMask, None, &ev))
		;

	last_x = event->xbutton.x_root;
	last_y = event->xbutton.y_root;

	while (!done) {
		WMMaskEvent(dpy, PointerMotionMask | ButtonReleaseMask, &ev);

		if (ev.type == MotionNotify) {
			int dx = ev.xmotion.x_root - last_x;
			int dy = ev.xmotion.y_root - last_y;
			last_x = ev.xmotion.x_root;
			last_y = ev.xmotion.y_root;

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
		} else if (ev.type == ButtonRelease) {
			done = True;
		}
	}

	XUngrabPointer(dpy, CurrentTime);
}

/* Inactivity timeout for the edge-pan modal loop: a DPMS-wake EnterNotify
 * can strand the loop with no further motion, so abort after this. */
#define VDESK_PAN_IDLE_MS 500

static void panIdleTimeout(void *data)
{
	*(Bool *)data = True;
}

void wVdeskMouseMoveDesktop(XEvent *event, WMPoint direction)
{
	static int lock = False;
	Bool done = False;
	Bool moved = True;
	Bool timed_out = False;
	WMHandlerID pan_idle_timer = NULL;
	WScreen *scr = wScreenForRootWindow(event->xcrossing.root);
	WMPoint old_pos = wmkpoint(event->xcrossing.x_root, event->xcrossing.y_root);
	WMPoint step;
	int x, y;
	int resisted = 0;
	XEvent ev;
	int head;
	WMRect head_rect;
	WMPoint center;

	/* run the loop in the geometry of the head whose edge sentinel
	 * fired, so the warp target, the pan step origin and the pointer
	 * clamps never reach past that head */
	head = wGetHeadForPoint(scr, old_pos);
	head_rect = wGetRectForHead(scr, head);
	center = wmkpoint(head_rect.pos.x + head_rect.size.width / 2,
			  head_rect.pos.y + head_rect.size.height / 2);

	if (lock)
		return;
	lock = True;

	if (scr->workspaces[scr->current_workspace]->layout != WLAYOUT_FLOAT)
		goto exit;

	if (XGrabPointer(dpy, event->xcrossing.window, False,
			 PointerMotionMask, GrabModeAsync, GrabModeAsync,
			 scr->root_win, wPreferences.cursor[WCUR_EMPTY], CurrentTime) != GrabSuccess) {
		step = vec_scale2(direction, w_global.config->vedge_hscrollspeed,
				  w_global.config->vedge_vscrollspeed);
		x = scr->workspaces[scr->current_workspace]->view_x;
		y = scr->workspaces[scr->current_workspace]->view_y;
		if (wVdeskSetViewport(scr, scr->current_workspace, x + step.x, y + step.y)) {
			step = vec_scale(direction, 2);
			XWarpPointer(dpy, None, scr->root_win, 0, 0, 0, 0,
				     event->xcrossing.x_root - step.x,
				     event->xcrossing.y_root - step.y);
		}
		goto exit;
	}
	XFlush(dpy);
	while (WXEvqTakeMask(dpy, PointerMotionMask | ButtonPressMask, None, &ev))
		;

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
			XWarpPointer(dpy, None, scr->root_win, 0, 0, 0, 0,
				     center.x, center.y);
			moved = False;
		}
		/* Inlined WMMaskEvent that also aborts when the idle timer fires. */
		while (!timed_out &&
		       !WXEvqTakeMask(dpy, PointerMotionMask, None, &ev))
			W_HandleInputEvents(True, ConnectionNumber(dpy));
		if (timed_out) {
			/* the one-shot timer already fired and was swept */
			pan_idle_timer = NULL;
			break;
		}
		WMDeleteTimerHandler(pan_idle_timer);
		pan_idle_timer = WMAddTimerHandler(VDESK_PAN_IDLE_MS,
						  panIdleTimeout, &timed_out);

		if (ev.type == MotionNotify) {
			int step_len;
			step = wmkpoint(ev.xmotion.x_root - center.x,
					ev.xmotion.y_root - center.y);
			step_len = vec_dot(step, direction);
			if (step_len < 0) {
				done = True;
			} else if (step_len > 0) {
				Bool do_move = True;
				int resist = w_global.config->vedge_resistance;
				WMPoint closest;
				int closest_len = INT_MAX;
				if (resist) {
					closest = getClosestEdge(scr, direction, cmp_gez);
					closest_len = vec_dot(direction, closest);
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
					moved = True;
				}
			}
		}
	}
	WMDeleteTimerHandlerPtr(&pan_idle_timer);

	step = vec_add(old_pos, vec_scale(direction, -1));
	XWarpPointer(dpy, None, scr->root_win, 0, 0, 0, 0, step.x, step.y);
	XUngrabPointer(dpy, CurrentTime);

 exit:
	WMDeleteTimerHandlerPtr(&pan_idle_timer);
	lock = False;
}

#define LEFT_EDGE   0x01
#define RIGHT_EDGE  0x02
#define TOP_EDGE    0x04
#define BOTTOM_EDGE 0x08
#define ALL_EDGES   0x0F

XContext wVEdgeContext = 0;

static void vdHandleEnter_u(XEvent *event) { wVdeskMouseMoveDesktop(event, VEC_UP); }
static void vdHandleEnter_d(XEvent *event) { wVdeskMouseMoveDesktop(event, VEC_DOWN); }
static void vdHandleEnter_l(XEvent *event) { wVdeskMouseMoveDesktop(event, VEC_LEFT); }
static void vdHandleEnter_r(XEvent *event) { wVdeskMouseMoveDesktop(event, VEC_RIGHT); }

static void createEdges(WScreen *scr)
{
	int i, j;
	int vmask;
	XSetWindowAttributes attribs;
	int heads = wXineramaHeads(scr);
	int *hasEdges;
	int thickness = WMAX(1, (int)w_global.config->vedge_bordersize);
	int nr_edges = 0;
	int max_edges = 4 * heads;
	int head;
	Window *edges;

	if (scr->virtual_edges)
		return;

	hasEdges = wmalloc(sizeof(int) * heads);
	edges = wmalloc(sizeof(Window) * max_edges);

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

	if (wVEdgeContext == 0)
		wVEdgeContext = XUniqueContext();

	vmask = CWEventMask | CWOverrideRedirect;
	attribs.event_mask = (EnterWindowMask | LeaveWindowMask | VisibilityChangeMask);
	attribs.override_redirect = True;

	for (head = 0; head < wXineramaHeads(scr); ++head) {
		WMRect rect = wGetRectForHead(scr, head);

		if (hasEdges[head] & TOP_EDGE) {
			edges[nr_edges] =
				XCreateWindow(dpy, scr->root_win, rect.pos.x, rect.pos.y,
					      rect.size.width, thickness, 0,
					      CopyFromParent, InputOnly, CopyFromParent, vmask, &attribs);
			XSaveContext(dpy, edges[nr_edges], wVEdgeContext, (XPointer) vdHandleEnter_u);
			++nr_edges;
		}
		if (hasEdges[head] & BOTTOM_EDGE) {
			edges[nr_edges] =
				XCreateWindow(dpy, scr->root_win, rect.pos.x,
					      rect.pos.y + rect.size.height - thickness,
					      rect.size.width, thickness, 0,
					      CopyFromParent, InputOnly, CopyFromParent, vmask, &attribs);
			XSaveContext(dpy, edges[nr_edges], wVEdgeContext, (XPointer) vdHandleEnter_d);
			++nr_edges;
		}
		if (hasEdges[head] & LEFT_EDGE) {
			edges[nr_edges] =
				XCreateWindow(dpy, scr->root_win, rect.pos.x, rect.pos.y,
					      thickness, rect.size.height, 0,
					      CopyFromParent, InputOnly, CopyFromParent, vmask, &attribs);
			XSaveContext(dpy, edges[nr_edges], wVEdgeContext, (XPointer) vdHandleEnter_l);
			++nr_edges;
		}
		if (hasEdges[head] & RIGHT_EDGE) {
			edges[nr_edges] =
				XCreateWindow(dpy, scr->root_win,
					      rect.pos.x + rect.size.width - thickness, rect.pos.y,
					      thickness, rect.size.height, 0,
					      CopyFromParent, InputOnly, CopyFromParent, vmask, &attribs);
			XSaveContext(dpy, edges[nr_edges], wVEdgeContext, (XPointer) vdHandleEnter_r);
			++nr_edges;
		}
	}

	scr->virtual_nr_edges = nr_edges;
	scr->virtual_edges = edges;

	for (i = 0; i < scr->virtual_nr_edges; ++i)
		XMapWindow(dpy, scr->virtual_edges[i]);
	wWorkspaceRaiseEdge(scr);

	wfree(hasEdges);
}

static void destroyEdges(WScreen *scr)
{
	int i;

	if (!scr->virtual_edges)
		return;

	for (i = 0; i < scr->virtual_nr_edges; ++i) {
		XDeleteContext(dpy, scr->virtual_edges[i], wVEdgeContext);
		XUnmapWindow(dpy, scr->virtual_edges[i]);
		XDestroyWindow(dpy, scr->virtual_edges[i]);
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

	if (toggle) {
		for (i = 0; i < scr->virtual_nr_edges; ++i)
			XRaiseWindow(dpy, scr->virtual_edges[i]);
	} else {
		for (i = scr->virtual_nr_edges - 1; i >= 0; --i)
			XRaiseWindow(dpy, scr->virtual_edges[i]);
	}
	toggle ^= 1;
}

void wWorkspaceLowerEdge(WScreen *scr)
{
	int i;
	for (i = 0; i < scr->virtual_nr_edges; ++i)
		XLowerWindow(dpy, scr->virtual_edges[i]);
}

#endif /* VIRTUAL_DESKTOP */
