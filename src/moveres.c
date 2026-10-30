/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
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

#include "wconfig.h"
#include "wmspec.h"

#include <xkbcommon/xkbcommon-keysyms.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

#include "WindowMaker.h"
#include "framewin.h"
#include "window.h"
#include "client.h"
#include "icon.h"
#include "dock.h"
#include "actions.h"
#include "layout.h"
#include "wxkb.h"
#include "workspace.h"
#include "placement.h"

#include "geomview.h"
#include "screen.h"
#include "wireframe.h"
#include "xinerama.h"
#include "event.h"

#include <WINGs/WINGsP.h>

/* How many different types of geometry/position
 display thingies are there? */
#define NUM_DISPLAYS 4

#define LEFT            1
#define RIGHT           2
#define UP              4
#define DOWN            8

/* True if window currently has a border. This also includes borderless
 * windows which are currently selected
 */
#define HAS_BORDER_WITH_SELECT(w) ((w)->flags.selected || HAS_BORDER(w))


/*
 *----------------------------------------------------------------------
 * checkMouseSamplingRate-
 *      For lowering the mouse motion sampling rate for machines where
 * it's too high (SGIs). If it returns False then the event should be
 * ignored.
 *----------------------------------------------------------------------
 */
static bool checkMouseSamplingRate(xcb_generic_event_t ** ev)
{
	static xcb_timestamp_t previousMotion = 0;

	if (((*ev)->response_type & 0x7f) == XCB_MOTION_NOTIFY) {
		if (((xcb_motion_notify_event_t *)*ev)->time - previousMotion < DELAY_BETWEEN_MOUSE_SAMPLING) {
			return false;
		} else {
			previousMotion = ((xcb_motion_notify_event_t *)*ev)->time;
		}
	}
	return true;
}

/*
 *----------------------------------------------------------------------
 * moveGeometryDisplayCentered
 *
 * routine that moves the geometry/position window on scr so it is
 * centered over the given coordinates (x,y). Also the window position
 * is clamped so it stays on the screen at all times.
 *----------------------------------------------------------------------
 */
static void moveGeometryDisplayCentered(WScreen * scr, int x, int y)
{
	unsigned int w = WMWidgetWidth(scr->gview);
	unsigned int h = WMWidgetHeight(scr->gview);
	int x1 = 0, y1 = 0, x2 = scr->scr_width, y2 = scr->scr_height;

	x -= w / 2;
	y -= h / 2;

	/* dead area check */
	if (scr->xine_info.count) {
		WMRect rect;
		int head, flags;

		rect.pos.x = x;
		rect.pos.y = y;
		rect.size.width = w;
		rect.size.height = h;

		head = wGetRectPlacementInfo(scr, rect, &flags);

		if (flags & (XFLAG_DEAD | XFLAG_PARTIAL)) {
			rect = wGetRectForHead(scr, head);
			x1 = rect.pos.x;
			y1 = rect.pos.y;
			x2 = x1 + rect.size.width;
			y2 = y1 + rect.size.height;
		}
	}

	if (x < x1 + 1)
		x = x1 + 1;
	else if (x > (x2 - w))
		x = x2 - w;

	if (y < y1 + 1)
		y = y1 + 1;
	else if (y > (y2 - h))
		y = y2 - h;

	WMMoveWidget(scr->gview, x, y);
}

static void showPosition(WWindow * wwin, int x, int y)
{
	WScreen *scr = wwin->screen_ptr;
#ifdef VIRTUAL_DESKTOP
	/* show world coordinates (frame_x + view_x) so the move OSD reflects
	 * the pan offset, not just the on-screen position */
	WSetGeometryViewShownPosition(scr->gview,
		x + scr->workspaces[scr->current_workspace]->view_x,
		y + scr->workspaces[scr->current_workspace]->view_y);
#else
	WSetGeometryViewShownPosition(scr->gview, x, y);
#endif
}

static void placeGeometryDisplay(WWindow *wwin, int mode, int x, int y, int w, int h)
{
	WScreen *scr = wwin->screen_ptr;
	WMRect rect;

	if (mode == WDIS_CENTER) {
		rect = wGetRectForHead(scr, wGetHeadForWindow(wwin));
		moveGeometryDisplayCentered(scr, rect.pos.x + rect.size.width / 2,
					    rect.pos.y + rect.size.height / 2);
	} else if (mode == WDIS_TOPLEFT) {
		rect = wGetRectForHead(scr, wGetHeadForWindow(wwin));
		moveGeometryDisplayCentered(scr, rect.pos.x + 1, rect.pos.y + 1);
	} else if (mode == WDIS_FRAME_CENTER) {
		moveGeometryDisplayCentered(scr, x + w / 2, y + h / 2);
	}
	WMMapWidget(scr->gview);
}

static void cyclePositionDisplay(WWindow * wwin, int x, int y, int w, int h)
{
	WScreen *scr = wwin->screen_ptr;

	w_global.config->move_display++;
	w_global.config->move_display %= NUM_DISPLAYS;

	if (w_global.config->move_display == WDIS_NONE)
		WMUnmapWidget(scr->gview);
	else
		placeGeometryDisplay(wwin, w_global.config->move_display, x, y, w, h);
}

static void mapPositionDisplay(WWindow * wwin, int x, int y, int w, int h)
{
	WScreen *scr = wwin->screen_ptr;

	if (w_global.config->move_display == WDIS_NONE)
		return;

	placeGeometryDisplay(wwin, w_global.config->move_display, x, y, w, h);
	WSetGeometryViewShownPosition(scr->gview, x, y);
}

static void showGeometry(WWindow * wwin, int x1, int y1, int x2, int y2, int direction)
{
	WScreen *scr = wwin->screen_ptr;
	int ty, by;

	(void)direction;

	if (HAS_BORDER_WITH_SELECT(wwin)) {
		int fbw = wFrameBorderWidth(wwin);

		x1 += fbw;
		x2 += fbw;
		y1 += fbw;
		y2 += fbw;
	}

	ty = y1 + wwin->frame->top_width;
	by = y2 - wwin->frame->bottom_width;

	WSetGeometryViewShownSize(scr->gview, (x2 - x1 - wwin->normal_hints->base_width)
				  / wwin->normal_hints->width_inc,
				  (by - ty - wwin->normal_hints->base_height)
				  / wwin->normal_hints->height_inc);
}

static void cycleGeometryDisplay(WWindow * wwin, int x, int y, int w, int h, int dir)
{
	WScreen *scr = wwin->screen_ptr;

	w_global.config->size_display++;
	w_global.config->size_display %= NUM_DISPLAYS;

	if (w_global.config->size_display == WDIS_NONE) {
		WMUnmapWidget(scr->gview);
	} else {
		placeGeometryDisplay(wwin, w_global.config->size_display, x, y, w, h);
		showGeometry(wwin, x, y, x + w, y + h, dir);
	}
}

static void mapGeometryDisplay(WWindow * wwin, int x, int y, int w, int h)
{
	if (w_global.config->size_display == WDIS_NONE)
		return;

	placeGeometryDisplay(wwin, w_global.config->size_display, x, y, w, h);
	showGeometry(wwin, x, y, x + w, y + h, 0);
}

static void doWindowMove(WWindow * wwin, WMArray * array, int dx, int dy)
{
	WWindow *tmpw;
	WScreen *scr = wwin->screen_ptr;
	int x, y;

	if (!array || !WMGetArrayItemCount(array)) {
		wWindowMove(wwin, wwin->frame_x + dx, wwin->frame_y + dy);
	} else {
		WMArrayIterator iter;

		WM_ITERATE_ARRAY(array, tmpw, iter) {
			x = tmpw->frame_x + dx;
			y = tmpw->frame_y + dy;

#if 1				/* XXX: with xinerama patch was #if 0, check this */
			/* don't let windows become unreachable */

			if (x + (int)tmpw->frame->core->width < 20)
				x = 20 - (int)tmpw->frame->core->width;
			else if (x + 20 > scr->scr_width)
				x = scr->scr_width - 20;

			if (y + (int)tmpw->frame->core->height < 20)
				y = 20 - (int)tmpw->frame->core->height;
			else if (y + 20 > scr->scr_height)
				y = scr->scr_height - 20;
#else
			wScreenBringInside(scr, &x, &y,
					   (int)tmpw->frame->core->width, (int)tmpw->frame->core->height);
#endif

			wWindowMove(tmpw, x, y);
		}
	}
}

static void addWindowOutline(WWindow * wwin, int x, int y, int width, int height)
{
	int h = 0;
	int bottom = 0;
	int fb = 0;

	if (HAS_BORDER_WITH_SELECT(wwin))
		fb = wFrameBorderWidth(wwin);

	if (HAS_TITLEBAR(wwin) && !wwin->flags.shaded) {
		h = WSCALE(wwin->screen_ptr, WMFontHeight(wwin->screen_ptr->title_font) + (w_global.config->window_title_clearance +
								  TITLEBAR_EXTEND_SPACE) * 2);

		if (h > w_global.config->window_title_max_height)
			h = w_global.config->window_title_max_height;

		if (h < w_global.config->window_title_min_height)
			h = w_global.config->window_title_min_height;
	}
	if (HAS_RESIZEBAR(wwin) && !wwin->flags.shaded) {
		/* Can't use wwin-frame->bottom_width because, in some cases
		   (e.g. interactive placement), frame does not point to anything. */
		bottom = wwin->screen_ptr->resizebar_height;
	}

	wWireframeAddOutline(wwin->screen_ptr, x, y, width, height, h, bottom, fb);
}

static void addFrameOutlines(WWindow * wwin, WMArray * array, int dx, int dy)
{
	WWindow *tmpw;
	int scr_width = wwin->screen_ptr->scr_width;
	int scr_height = wwin->screen_ptr->scr_height;
	int x, y;

	if (!array) {

		x = wwin->frame_x + dx;
		y = wwin->frame_y + dy;

		addWindowOutline(wwin, x, y, wwin->frame->core->width, wwin->frame->core->height);

	} else {
		WMArrayIterator iter;

		WM_ITERATE_ARRAY(array, tmpw, iter) {
			x = tmpw->frame_x + dx;
			y = tmpw->frame_y + dy;

			/* don't let windows become unreachable */
#if 1				/* XXX: was 0 in XINERAMA patch, check */
			if (x + (int)tmpw->frame->core->width < 20)
				x = 20 - (int)tmpw->frame->core->width;
			else if (x + 20 > scr_width)
				x = scr_width - 20;

			if (y + (int)tmpw->frame->core->height < 20)
				y = 20 - (int)tmpw->frame->core->height;
			else if (y + 20 > scr_height)
				y = scr_height - 20;

#else
			wScreenBringInside(wwin->screen_ptr, &x, &y,
					   (int)tmpw->frame->core->width, (int)tmpw->frame->core->height);
#endif

			addWindowOutline(tmpw, x, y, tmpw->frame->core->width, tmpw->frame->core->height);
		}
	}
}

static void flushMotion(void)
{
	xcb_generic_event_t *ev = NULL;

	wXSync(wdisp->conn);
	while (WXEvqTakeMask(wdisp->conn, XCB_EVENT_MASK_BUTTON_MOTION, XCB_NONE, &ev))
		free(ev);
}

static void crossWorkspace(WScreen * scr, WWindow * wwin, int opaque_move, int new_workspace, int rewind)
{
	/* do not let window be unmapped */
	if (opaque_move) {
		wwin->flags.changing_workspace = 1;
		wWindowChangeWorkspace(wwin, new_workspace);
	}
	/* go to new workspace */
	wWorkspaceChangeFromPointer(scr, new_workspace);

	wwin->flags.changing_workspace = 0;

	if (rewind)
		wXWarpPointer(wdisp->conn, XCB_NONE, XCB_NONE, 0, 0, 0, 0, scr->scr_width - 20, 0);
	else
		wXWarpPointer(wdisp->conn, XCB_NONE, XCB_NONE, 0, 0, 0, 0, -(scr->scr_width - 20), 0);

	flushMotion();

	if (!opaque_move) {
		wXGrabPointer(wdisp->conn, scr->root_win, true, XCB_EVENT_MASK_POINTER_MOTION
			     | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS, XCB_GRAB_MODE_ASYNC,
			     XCB_GRAB_MODE_ASYNC, XCB_NONE, wdisp->cursor[WCUR_MOVE], XCB_CURRENT_TIME);
	}
}

typedef struct {
	/* arrays of WWindows sorted by the respective border position */
	WWindow **topList;	/* top border */
	WWindow **leftList;	/* left border */
	WWindow **rightList;	/* right border */
	WWindow **bottomList;	/* bottom border */
	int count;

	/* index of window in the above lists indicating the relative position
	 * of the window with the others */
	int topIndex;
	int leftIndex;
	int rightIndex;
	int bottomIndex;

	int rubCount;		/* for workspace switching */

	int winWidth, winHeight;	/* width/height of the window */
	int realX, realY;	/* actual position of the window */
	int calcX, calcY;	/* calculated position of window */
	int omouseX, omouseY;	/* old mouse position */
	int mouseX, mouseY;	/* last known position of the pointer */

	enum {
		SNAP_NONE,
		SNAP_LEFT,
		SNAP_RIGHT,
		SNAP_TOP,
		SNAP_BOTTOM,
		SNAP_TOPLEFT,
		SNAP_TOPRIGHT,
		SNAP_BOTTOMLEFT,
		SNAP_BOTTOMRIGHT
	} snap;
} MoveData;

#define WTOP(w) (w)->frame_y
#define WLEFT(w) (w)->frame_x
#define WRIGHT(w) ((w)->frame_x + (int)(w)->frame->core->width - 1 + \
		   (HAS_BORDER_WITH_SELECT(w) ? 2*wFrameBorderWidth(w) : 0))
#define WBOTTOM(w) ((w)->frame_y + (int)(w)->frame->core->height - 1 + \
		    (HAS_BORDER_WITH_SELECT(w) ? 2*wFrameBorderWidth(w) : 0))

static int compareWTop(const void *a, const void *b)
{
	WWindow *wwin1 = *(WWindow **) a;
	WWindow *wwin2 = *(WWindow **) b;

	if (WTOP(wwin1) > WTOP(wwin2))
		return -1;
	else if (WTOP(wwin1) < WTOP(wwin2))
		return 1;
	else
		return 0;
}

static int compareWLeft(const void *a, const void *b)
{
	WWindow *wwin1 = *(WWindow **) a;
	WWindow *wwin2 = *(WWindow **) b;

	if (WLEFT(wwin1) > WLEFT(wwin2))
		return -1;
	else if (WLEFT(wwin1) < WLEFT(wwin2))
		return 1;
	else
		return 0;
}

static int compareWRight(const void *a, const void *b)
{
	WWindow *wwin1 = *(WWindow **) a;
	WWindow *wwin2 = *(WWindow **) b;

	if (WRIGHT(wwin1) < WRIGHT(wwin2))
		return -1;
	else if (WRIGHT(wwin1) > WRIGHT(wwin2))
		return 1;
	else
		return 0;
}

static int compareWBottom(const void *a, const void *b)
{
	WWindow *wwin1 = *(WWindow **) a;
	WWindow *wwin2 = *(WWindow **) b;

	if (WBOTTOM(wwin1) < WBOTTOM(wwin2))
		return -1;
	else if (WBOTTOM(wwin1) > WBOTTOM(wwin2))
		return 1;
	else
		return 0;
}

static void updateResistance(MoveData *data, int newX, int newY)
{
	int newX2 = newX + data->winWidth;
	int newY2 = newY + data->winHeight;
	bool ok = false;

	if (newX < data->realX) {
		if (data->rightIndex > 0 && newX < WRIGHT(data->rightList[data->rightIndex - 1])) {
			ok = true;
		} else if (data->leftIndex <= data->count - 1 && newX2 <= WLEFT(data->leftList[data->leftIndex])) {
			ok = true;
		}
	} else if (newX > data->realX) {
		if (data->leftIndex > 0 && newX2 > WLEFT(data->leftList[data->leftIndex - 1])) {
			ok = true;
		} else if (data->rightIndex <= data->count - 1
			   && newX >= WRIGHT(data->rightList[data->rightIndex])) {
			ok = true;
		}
	}

	if (!ok) {
		if (newY < data->realY) {
			if (data->bottomIndex > 0 && newY < WBOTTOM(data->bottomList[data->bottomIndex - 1])) {
				ok = true;
			} else if (data->topIndex <= data->count - 1
				   && newY2 <= WTOP(data->topList[data->topIndex])) {
				ok = true;
			}
		} else if (newY > data->realY) {
			if (data->topIndex > 0 && newY2 > WTOP(data->topList[data->topIndex - 1])) {
				ok = true;
			} else if (data->bottomIndex <= data->count - 1
				   && newY >= WBOTTOM(data->bottomList[data->bottomIndex])) {
				ok = true;
			}
		}
	}

	if (!ok)
		return;

	/* Step the cached indices to the boundaries for the current
	 * position instead of rescanning the whole lists. */
	while (data->bottomIndex > 0
	       && data->realY <= WBOTTOM(data->bottomList[data->bottomIndex - 1]))
		data->bottomIndex--;
	while (data->bottomIndex < data->count
	       && data->realY > WBOTTOM(data->bottomList[data->bottomIndex]))
		data->bottomIndex++;

	while (data->rightIndex > 0
	       && data->realX <= WRIGHT(data->rightList[data->rightIndex - 1]))
		data->rightIndex--;
	while (data->rightIndex < data->count
	       && data->realX > WRIGHT(data->rightList[data->rightIndex]))
		data->rightIndex++;

	while (data->leftIndex > 0
	       && (data->realX + data->winWidth) >= WLEFT(data->leftList[data->leftIndex - 1]))
		data->leftIndex--;
	while (data->leftIndex < data->count
	       && (data->realX + data->winWidth) < WLEFT(data->leftList[data->leftIndex]))
		data->leftIndex++;

	while (data->topIndex > 0
	       && (data->realY + data->winHeight) >= WTOP(data->topList[data->topIndex - 1]))
		data->topIndex--;
	while (data->topIndex < data->count
	       && (data->realY + data->winHeight) < WTOP(data->topList[data->topIndex]))
		data->topIndex++;
}

static void freeMoveData(MoveData * data)
{
	if (data->topList)
		wfree(data->topList);
	if (data->leftList)
		wfree(data->leftList);
	if (data->rightList)
		wfree(data->rightList);
	if (data->bottomList)
		wfree(data->bottomList);
}

static void updateMoveData(WWindow * wwin, MoveData * data)
{
	WScreen *scr = wwin->screen_ptr;
	WWindow *tmp;
	int i;

	data->count = 0;
	tmp = scr->focused_window;
	while (tmp) {
		if (tmp != wwin && wWindowIsVisibleOnAnyHead(tmp)
		    && !tmp->flags.miniaturized
		    && !tmp->flags.hidden && !tmp->flags.obscured && !WFLAGP(tmp, sunken)) {
			data->topList[data->count] = tmp;
			data->leftList[data->count] = tmp;
			data->rightList[data->count] = tmp;
			data->bottomList[data->count] = tmp;
			data->count++;
		}
		tmp = tmp->prev;
	}

	if (data->count == 0) {
		data->topIndex = 0;
		data->leftIndex = 0;
		data->rightIndex = 0;
		data->bottomIndex = 0;
		return;
	}

	/* order from closest to the border of the screen to farthest */

	qsort(data->topList, data->count, sizeof(data->topList[0]), compareWTop);
	qsort(data->leftList, data->count, sizeof(data->leftList[0]), compareWLeft);
	qsort(data->rightList, data->count, sizeof(data->rightList[0]), compareWRight);
	qsort(data->bottomList, data->count, sizeof(data->bottomList[0]), compareWBottom);

	/* figure the position of the window relative to the others */

	data->topIndex = -1;
	data->leftIndex = -1;
	data->rightIndex = -1;
	data->bottomIndex = -1;

	if (WTOP(wwin) < WBOTTOM(data->bottomList[0])) {
		data->bottomIndex = 0;
	}
	if (WLEFT(wwin) < WRIGHT(data->rightList[0])) {
		data->rightIndex = 0;
	}
	if (WRIGHT(wwin) > WLEFT(data->leftList[0])) {
		data->leftIndex = 0;
	}
	if (WBOTTOM(wwin) > WTOP(data->topList[0])) {
		data->topIndex = 0;
	}
	for (i = 0; i < data->count; i++) {
		if (WTOP(wwin) >= WBOTTOM(data->bottomList[i])) {
			data->bottomIndex = i + 1;
		}
		if (WLEFT(wwin) >= WRIGHT(data->rightList[i])) {
			data->rightIndex = i + 1;
		}
		if (WRIGHT(wwin) <= WLEFT(data->leftList[i])) {
			data->leftIndex = i + 1;
		}
		if (WBOTTOM(wwin) <= WTOP(data->topList[i])) {
			data->topIndex = i + 1;
		}
	}
}

static void initMoveData(WWindow * wwin, MoveData * data)
{
	int i;
	WWindow *tmp;

	memset(data, 0, sizeof(MoveData));

	for (i = 0, tmp = wwin->screen_ptr->focused_window; tmp != NULL; tmp = tmp->prev, i++) ;

	if (i > 1) {
		data->topList = wmalloc(sizeof(WWindow *) * i);
		data->leftList = wmalloc(sizeof(WWindow *) * i);
		data->rightList = wmalloc(sizeof(WWindow *) * i);
		data->bottomList = wmalloc(sizeof(WWindow *) * i);

		updateMoveData(wwin, data);
	}

	data->realX = wwin->frame_x;
	data->realY = wwin->frame_y;
	data->calcX = wwin->frame_x;
	data->calcY = wwin->frame_y;

	data->winWidth = wwin->frame->core->width + (HAS_BORDER_WITH_SELECT(wwin) ? 2 * wFrameBorderWidth(wwin) : 0);
	data->winHeight = wwin->frame->core->height + (HAS_BORDER_WITH_SELECT(wwin) ? 2 * wFrameBorderWidth(wwin) : 0);

	data->snap = SNAP_NONE;
}

static bool checkWorkspaceChange(WWindow * wwin, MoveData * data, bool opaqueMove)
{
	WScreen *scr = wwin->screen_ptr;
	bool changed = false;

	if (data->mouseX <= 1) {
		if (scr->current_workspace > 0) {
			crossWorkspace(scr, wwin, opaqueMove, scr->current_workspace - 1, true);
			changed = true;
			data->rubCount = 0;
		} else if (scr->current_workspace == 0 && w_global.config->ws_cycle) {
			crossWorkspace(scr, wwin, opaqueMove, scr->workspace_count - 1, true);
			changed = true;
			data->rubCount = 0;
		}
	} else if (data->mouseX >= scr->scr_width - 2) {
		if (scr->current_workspace == scr->workspace_count - 1) {
			if (w_global.config->ws_cycle || scr->workspace_count == MAX_WORKSPACES) {
				crossWorkspace(scr, wwin, opaqueMove, 0, false);
				changed = true;
				data->rubCount = 0;
			}
			/* if user insists on trying to go to next workspace even when
			 * it's already the last, create a new one */
			else if (data->omouseX == data->mouseX && w_global.config->ws_advance) {

				/* detect user "rubbing" the window against the edge */
				if (data->rubCount > 0 && data->omouseY - data->mouseY > MOVE_THRESHOLD) {

					data->rubCount = -(data->rubCount + 1);

				} else if (data->rubCount <= 0 && data->mouseY - data->omouseY > MOVE_THRESHOLD) {

					data->rubCount = -data->rubCount + 1;
				}
			}
			/* create a new workspace */
			if (abs(data->rubCount) > 2) {
				/* go to next workspace */
				wWorkspaceNew(scr);

				crossWorkspace(scr, wwin, opaqueMove, scr->current_workspace + 1, false);
				changed = true;
				data->rubCount = 0;
			}
		} else if (scr->current_workspace < scr->workspace_count) {
			/* go to next workspace */
			crossWorkspace(scr, wwin, opaqueMove, scr->current_workspace + 1, false);
			changed = true;
			data->rubCount = 0;
		}
	} else {
		data->rubCount = 0;
	}

	return changed;
}

/* Return the first window in list[start..0] (backward) or
 * list[start..count-1] whose perpendicular extent meets [lo, hi]:
 * the Y range for left/right lists, the X range for top/bottom lists.
 * Returns NULL when no window overlaps. */
static WWindow *
find_nearest_overlapping(WWindow **list, int start, int count,
			 bool backward, bool xaxis, int lo, int hi)
{
	int i;

	if (backward) {
		for (i = start; i >= 0; i--) {
			WWindow *tmp = list[i];
			int elo = xaxis ? WTOP(tmp) : WLEFT(tmp);
			int ehi = xaxis ? WBOTTOM(tmp) : WRIGHT(tmp);

			if (!(lo > ehi || hi < elo))
				return tmp;
		}
	} else {
		for (i = start; i < count; i++) {
			WWindow *tmp = list[i];
			int elo = xaxis ? WTOP(tmp) : WLEFT(tmp);
			int ehi = xaxis ? WBOTTOM(tmp) : WRIGHT(tmp);

			if (!(lo > ehi || hi < elo))
				return tmp;
		}
	}

	return NULL;
}

static void
updateWindowPosition(WWindow * wwin, MoveData * data, bool doResistance,
		     bool opaqueMove, int newMouseX, int newMouseY)
{
	WScreen *scr = wwin->screen_ptr;
	int dx, dy;		/* how much mouse moved */
	int winL, winR, winT, winB;	/* requested new window position */
	int newX, newY;		/* actual new window position */
	bool hresist, vresist;
	bool attract;

	hresist = false;
	vresist = false;

	/* check the direction of the movement */
	dx = newMouseX - data->mouseX;
	dy = newMouseY - data->mouseY;

	data->omouseX = data->mouseX;
	data->omouseY = data->mouseY;
	data->mouseX = newMouseX;
	data->mouseY = newMouseY;

	winL = data->calcX + dx;
	winR = data->calcX + data->winWidth + dx;
	winT = data->calcY + dy;
	winB = data->calcY + data->winHeight + dy;

	newX = data->realX;
	newY = data->realY;

	if (doResistance) {
		int l_edge, r_edge;
		int edge_l, edge_r;
		int t_edge, b_edge;
		int edge_t, edge_b;
		int resist;

		resist = WIN_RESISTANCE(w_global.config->edge_resistance);
		attract = w_global.config->attract;
		/* horizontal movement: check horizontal edge resistances */
		if (dx || dy) {
			WMRect rect;
			int head;
			WWindow *looprw;
			/* window is the leftmost window: check against screen edge */

			/* Add inter head resistance 1/2 (if needed) */
			head = wGetHeadForPointerLocation(scr);
			rect = wGetRectForHead(scr, head);

			l_edge = WMAX(scr->totalUsableArea[head].x1, rect.pos.x);
			edge_l = l_edge - resist;
			edge_r = WMIN(scr->totalUsableArea[head].x2, rect.pos.x + rect.size.width);
			r_edge = edge_r + resist;

			/* 1 */
			if ((data->rightIndex >= 0) && (data->rightIndex <= data->count)) {
				looprw = find_nearest_overlapping(data->rightList, data->rightIndex - 1,
								  data->count, true, true,
								  data->realY, data->realY + data->winHeight);
				if (looprw && (attract || ((data->realX < (WRIGHT(looprw) + 2)) && dx < 0))) {
					l_edge = WRIGHT(looprw) + 1;
					resist = WIN_RESISTANCE(w_global.config->edge_resistance);
				}

				if (attract) {
					looprw = find_nearest_overlapping(data->rightList, data->rightIndex,
									  data->count, false, true,
									  data->realY, data->realY + data->winHeight);
					if (looprw) {
						r_edge = WRIGHT(looprw) + 1;
						resist = WIN_RESISTANCE(w_global.config->edge_resistance);
					}
				}
			}

			if ((data->leftIndex >= 0) && (data->leftIndex <= data->count)) {
				looprw = find_nearest_overlapping(data->leftList, data->leftIndex - 1,
								  data->count, true, true,
								  data->realY, data->realY + data->winHeight);
				if (looprw
				    && (attract
					|| (((data->realX + data->winWidth) > (WLEFT(looprw) - 1)) && dx > 0))) {
					edge_r = WLEFT(looprw);
					resist = WIN_RESISTANCE(w_global.config->edge_resistance);
				}

				if (attract) {
					looprw = find_nearest_overlapping(data->leftList, data->leftIndex,
									  data->count, false, true,
									  data->realY, data->realY + data->winHeight);
					if (looprw) {
						edge_l = WLEFT(looprw);
						resist = WIN_RESISTANCE(w_global.config->edge_resistance);
					}
				}
			}

			/*
			   printf("%d %d\n",winL,winR);
			   printf("l_ %d r_ %d _l %d _r %d\n",l_edge,r_edge,edge_l,edge_r);
			 */

			if ((winL - l_edge) < (r_edge - winL)) {
				if (resist > 0) {
					if ((attract && winL <= l_edge + resist && winL >= l_edge - resist)
					    || (dx < 0 && winL <= l_edge && winL >= l_edge - resist)) {
						newX = l_edge;
						hresist = true;
					}
				}
			} else {
				if (resist > 0 && attract && winL >= r_edge - resist && winL <= r_edge + resist) {
					newX = r_edge;
					hresist = true;
				}
			}

			if ((winR - edge_l) < (edge_r - winR)) {
				if (resist > 0 && attract && winR <= edge_l + resist && winR >= edge_l - resist) {
					newX = edge_l - data->winWidth;
					hresist = true;
				}
			} else {
				if (resist > 0) {
					if ((attract && winR >= edge_r - resist && winR <= edge_r + resist)
					    || (dx > 0 && winR >= edge_r && winR <= edge_r + resist)) {
						newX = edge_r - data->winWidth;
						hresist = true;
					}
				}
			}

			/* VeRT */
			/* Add inter head resistance 2/2 (if needed) */
			t_edge = WMAX(scr->totalUsableArea[head].y1, rect.pos.y);
			edge_t = t_edge - resist;
			edge_b = WMIN(scr->totalUsableArea[head].y2, rect.pos.y + rect.size.height);
			b_edge = edge_b + resist;

			if ((data->bottomIndex >= 0) && (data->bottomIndex <= data->count)) {
				looprw = find_nearest_overlapping(data->bottomList, data->bottomIndex - 1,
								  data->count, true, false,
								  data->realX, data->realX + data->winWidth);
				if (looprw && (attract || ((data->realY < (WBOTTOM(looprw) + 2)) && dy < 0))) {
					t_edge = WBOTTOM(looprw) + 1;
					resist = WIN_RESISTANCE(w_global.config->edge_resistance);
				}

				if (attract) {
					looprw = find_nearest_overlapping(data->bottomList, data->bottomIndex,
									  data->count, false, false,
									  data->realX, data->realX + data->winWidth);
					if (looprw) {
						b_edge = WBOTTOM(looprw) + 1;
						resist = WIN_RESISTANCE(w_global.config->edge_resistance);
					}
				}
			}

			if ((data->topIndex >= 0) && (data->topIndex <= data->count)) {
				looprw = find_nearest_overlapping(data->topList, data->topIndex - 1,
								  data->count, true, false,
								  data->realX, data->realX + data->winWidth);
				if (looprw
				    && (attract
					|| (((data->realY + data->winHeight) > (WTOP(looprw) - 1)) && dy > 0))) {
					edge_b = WTOP(looprw);
					resist = WIN_RESISTANCE(w_global.config->edge_resistance);
				}

				if (attract) {
					looprw = find_nearest_overlapping(data->topList, data->topIndex,
									  data->count, false, false,
									  data->realX, data->realX + data->winWidth);
					if (looprw) {
						edge_t = WTOP(looprw);
						resist = WIN_RESISTANCE(w_global.config->edge_resistance);
					}
				}
			}

			if ((winT - t_edge) < (b_edge - winT)) {
				if (resist > 0) {
					if ((attract && winT <= t_edge + resist && winT >= t_edge - resist)
					    || (dy < 0 && winT <= t_edge && winT >= t_edge - resist)) {
						newY = t_edge;
						vresist = true;
					}
				}
			} else {
				if (resist > 0 && attract && winT >= b_edge - resist && winT <= b_edge + resist) {
					newY = b_edge;
					vresist = true;
				}
			}

			if ((winB - edge_t) < (edge_b - winB)) {
				if (resist > 0 && attract && winB <= edge_t + resist && winB >= edge_t - resist) {
					newY = edge_t - data->winHeight;
					vresist = true;
				}
			} else {
				if (resist > 0) {
					if ((attract && winB >= edge_b - resist && winB <= edge_b + resist)
					    || (dy > 0 && winB >= edge_b && winB <= edge_b + resist)) {
						newY = edge_b - data->winHeight;
						vresist = true;
					}
				}
			}
		}
		/* END VeRT */

	}

	/* update window position */
	data->calcX += dx;
	data->calcY += dy;

	if (((dx > 0 && data->calcX - data->realX > 0)
	     || (dx < 0 && data->calcX - data->realX < 0)) && !hresist)
		newX = data->calcX;

	if (((dy > 0 && data->calcY - data->realY > 0)
	     || (dy < 0 && data->calcY - data->realY < 0)) && !vresist)
		newY = data->calcY;

	if (data->realX != newX || data->realY != newY) {
		if (opaqueMove)
			doWindowMove(wwin, scr->selected_windows, newX - wwin->frame_x, newY - wwin->frame_y);

		/* the wireframe outline is refreshed by the caller once the
		 * final position for this step is known */
		if (!scr->selected_windows && w_global.config->move_display == WDIS_FRAME_CENTER)
			moveGeometryDisplayCentered(scr, newX + data->winWidth / 2, newY + data->winHeight / 2);

		if (!scr->selected_windows)
			showPosition(wwin, newX, newY);
	}

	/* recalc relative window position */
	if (doResistance && (data->realX != newX || data->realY != newY)) {
		updateResistance(data, newX, newY);
	}

	data->realX = newX;
	data->realY = newY;
}

static void addSnapOutline(WWindow *wwin, int direction, int mouseX, int mouseY)
{
	WScreen *scr;
	int head, x, y, fb;
	unsigned int width, height;
	WMRect rect;

	scr = wwin->screen_ptr;
	head = wGetHeadForPoint(scr, wmkpoint(mouseX, mouseY));
	rect = wGetRectForHead(scr, head);

	x = rect.pos.x;
	y = rect.pos.y;
	width = rect.size.width;
	height = rect.size.height;
	fb = HAS_BORDER_WITH_SELECT(wwin) ? 2 * wFrameBorderWidth(wwin) : 0;

	switch (direction) {
	case SNAP_LEFT:
		width /= 2;
		break;

	case SNAP_RIGHT:
		width /= 2;
		x += width;
		break;

	case SNAP_TOP:
		if (!w_global.config->snap_to_top_maximizes_fullscreen)
			height /= 2;
		break;

	case SNAP_BOTTOM:
		height /= 2;
		y += height;
		break;

	case SNAP_TOPLEFT:
		width /= 2;
		height /= 2;
		break;

	case SNAP_TOPRIGHT:
		width /= 2;
		height /= 2;
		x += width;
		break;

	case SNAP_BOTTOMLEFT:
		width /= 2;
		height /= 2;
		y += height;
		break;

	case SNAP_BOTTOMRIGHT:
		width /= 2;
		height /= 2;
		x += width;
		y += height;
		break;
	}

	addWindowOutline(wwin, x, y, width - fb, height - fb);
}

/*
 * Refresh the wireframe to the current move state, the dragged frame(s)
 * while the move is non-opaque plus the snap-target preview when armed.
 * The wireframe is a real window, so there is no erase/redraw pairing,
 * each call simply replaces whatever was shown before.
 */
static void showMoveWireframe(WWindow *wwin, MoveData *data, bool opaqueMove)
{
	WScreen *scr = wwin->screen_ptr;

	wWireframeBegin(scr);
	if (!opaqueMove)
		addFrameOutlines(wwin, scr->selected_windows,
				 data->realX - wwin->frame_x, data->realY - wwin->frame_y);
	if (data->snap)
		addSnapOutline(wwin, data->snap, data->mouseX, data->mouseY);
	wWireframeCommit(scr);
}

static void showResizeWireframe(WWindow *wwin, int x, int y, int w, int h)
{
	wWireframeBegin(wwin->screen_ptr);
	addWindowOutline(wwin, x, y, w, h);
	wWireframeCommit(wwin->screen_ptr);
}

static int get_snap_direction(WScreen *scr, int x, int y)
{
	WMPoint point;
	WMRect head;
	int edge, corner;

	edge = w_global.config->snap_edge_detect;
	corner = w_global.config->snap_corner_detect;

	/* snap against the head the pointer is on, the seam between two
	 * heads is an edge of both of them, not the screen edge */
	point = wmkpoint(x, y);
	head = wGetRectForHead(scr, wGetHeadForPoint(scr, point));

	if (x < head.pos.x + corner && y < head.pos.y + corner)
		return SNAP_TOPLEFT;
	if (x < head.pos.x + corner && y >= head.pos.y + (int)head.size.height - corner)
		return SNAP_BOTTOMLEFT;
	if (x < head.pos.x + edge)
		return SNAP_LEFT;

	if (x >= head.pos.x + (int)head.size.width - corner && y < head.pos.y + corner)
		return SNAP_TOPRIGHT;
	if (x >= head.pos.x + (int)head.size.width - corner && y >= head.pos.y + (int)head.size.height - corner)
		return SNAP_BOTTOMRIGHT;
	if (x >= head.pos.x + (int)head.size.width - edge)
		return SNAP_RIGHT;

	if (y < head.pos.y + edge)
		return SNAP_TOP;
	if (y >= head.pos.y + (int)head.size.height - edge)
		return SNAP_BOTTOM;
	return SNAP_NONE;
}

static void do_snap(WWindow *wwin, MoveData *data)
{
	int directions;

	directions = 0;

	switch (data->snap) {
	case SNAP_NONE:
		return;
	case SNAP_LEFT:
		directions = MAX_VERTICAL | MAX_LEFTHALF;
		break;
	case SNAP_RIGHT:
		directions = MAX_VERTICAL | MAX_RIGHTHALF;
		break;
	case SNAP_TOP:
		if (w_global.config->snap_to_top_maximizes_fullscreen)
			directions = MAX_HORIZONTAL | MAX_VERTICAL;
		else
			directions = MAX_HORIZONTAL | MAX_TOPHALF;
		break;
	case SNAP_BOTTOM:
		directions = MAX_HORIZONTAL | MAX_BOTTOMHALF;
		break;
	case SNAP_TOPLEFT:
		directions = MAX_TOPHALF | MAX_LEFTHALF;
		break;
	case SNAP_TOPRIGHT:
		directions = MAX_TOPHALF | MAX_RIGHTHALF;
		break;
	case SNAP_BOTTOMLEFT:
		directions = MAX_BOTTOMHALF | MAX_LEFTHALF;
		break;
	case SNAP_BOTTOMRIGHT:
		directions = MAX_BOTTOMHALF | MAX_RIGHTHALF;
		break;
	}

	if (directions)
		handleMaximize(wwin, directions);
	data->snap = SNAP_NONE;
}

#define _KS KEY_CONTROL_WINDOW_WEIGHT

#define MOVABLE_BIT	0x01
#define RESIZABLE_BIT	0x02

int wKeyboardMoveResizeWindow(WWindow * wwin)
{
	WScreen *scr = wwin->screen_ptr;
	xcb_window_t root = scr->root_win;
	xcb_generic_event_t *event = NULL;
	int w = wwin->frame->core->width;
	int h = wwin->frame->core->height;
	int scr_width = wwin->screen_ptr->scr_width;
	int scr_height = wwin->screen_ptr->scr_height;
	int il = 0, ir = 0, it = 0, ib = 0;
	int vert_border, horz_border;
	int src_x = wwin->frame_x;
	int src_y = wwin->frame_y;
	int original_w = w;
	int original_h = h;
	int done, off_x, off_y, ww, wh, cw;
	int kspeed = _KS;
	int opaqueMoveResize = w_global.config->opaque_move_resize_keyboard;
	xcb_timestamp_t lastTime = 0;
	xcb_keycode_t shiftl, shiftr, ctrlmode;
	xkb_keysym_t keysym = XKB_KEY_NoSymbol;
	int moment = 0;
	int modes = ((IS_MOVABLE(wwin) ? MOVABLE_BIT : 0) | (IS_RESIZABLE(wwin) ? RESIZABLE_BIT : 0));
	int head = ((w_global.config->auto_arrange_icons && wXineramaHeads(scr) > 1)
		    ? wGetHeadForWindow(wwin)
		    : scr->xine_info.primary_head);

	shiftl = wXkbKeysymToKeycode(XKB_KEY_Shift_L);
	shiftr = wXkbKeysymToKeycode(XKB_KEY_Shift_R);
	ctrlmode = done = off_x = off_y = 0;

	if (HAS_BORDER(wwin))
		wFrameInnerBorderWidths(wwin, &il, &ir, &it, &ib);
	vert_border = wwin->frame->top_width + wwin->frame->bottom_width + it + ib;
	horz_border = il + ir;

	if (modes == RESIZABLE_BIT) {
		ctrlmode = 1;
	}

	/* the grab is a checked round-trip, the reply cannot precede the
	 * processing of anything queued before it */
	if (wXGrabKeyboard(wdisp->conn, root, false, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_CURRENT_TIME)
	    != XCB_GRAB_STATUS_SUCCESS) {
		wwarning(_("could not grab the keyboard for keyboard move/resize"));
		return 0;
	}

	if (!wwin->flags.selected) {
		wUnselectWindows(scr);
	}
	wXGrabPointer(wdisp->conn, scr->root_win, true, XCB_EVENT_MASK_POINTER_MOTION
		     | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS, XCB_GRAB_MODE_ASYNC,
		     XCB_GRAB_MODE_ASYNC, XCB_NONE, wdisp->cursor[WCUR_NORMAL], XCB_CURRENT_TIME);



	if (!opaqueMoveResize) {
		wWireframeBegin(scr);
		if (scr->selected_windows)
			addFrameOutlines(wwin, scr->selected_windows, off_x, off_y);
		else
			addWindowOutline(wwin, src_x + off_x, src_y + off_y, w, h);
		wWireframeCommit(scr);
	}
	if ((wwin->flags.shaded || scr->selected_windows) && (!scr->selected_windows)) {
		mapPositionDisplay(wwin, src_x, src_y, w, h);

	}
	ww = w;
	wh = h;
	while (1) {
		/*
		   looper.ox=off_x;
		   looper.oy=off_y;
		 */
		free(event);
		event = NULL;
		do {
			WMMaskEvent(wdisp->conn, XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE
				    | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_EXPOSURE, &event);
			if ((event->response_type & 0x7f) != XCB_EXPOSE)
				break;
			WMHandleEvent(event);
			free(event);
			event = NULL;
		} while (1);

		if (ctrlmode)
			showGeometry(wwin, src_x + off_x, src_y + off_y, src_x + off_x + ww, src_y + off_y + wh,
				     0);

		wXSync(wdisp->conn);

		switch ((event->response_type & 0x7f)) {
		case XCB_KEY_PRESS:
			/* accelerate */
			if ((((xcb_key_press_event_t *)event)->time) - lastTime > 50) {
				kspeed /= (1 + ((((xcb_key_press_event_t *)event)->time) - lastTime) / 100);
			} else {
				if (kspeed < 20) {
					kspeed++;
				}
			}
			if (kspeed < _KS)
				kspeed = _KS;
			lastTime = (((xcb_key_press_event_t *)event)->time);
			if (modes == (MOVABLE_BIT | RESIZABLE_BIT)) {
				if (((((xcb_key_press_event_t *)event)->state) & XCB_MOD_MASK_CONTROL) && !wwin->flags.shaded) {
					ctrlmode = 1;
					wUnselectWindows(scr);
				} else {
					ctrlmode = 0;
				}
			}
			if ((((xcb_key_press_event_t *)event)->detail) == shiftl || (((xcb_key_press_event_t *)event)->detail) == shiftr) {
				if (ctrlmode)
					cycleGeometryDisplay(wwin, src_x + off_x, src_y + off_y, ww, wh, 0);
				else
					cyclePositionDisplay(wwin, src_x + off_x, src_y + off_y, ww, wh);
			} else {

				keysym = wXkbKeycodeToKeysym((((xcb_key_press_event_t *)event)->detail), 0);
				switch (keysym) {
				case XKB_KEY_Return:
				case XKB_KEY_KP_Enter:
					done = 2;
					break;
				case XKB_KEY_Escape:
					done = 1;
					break;
				case XKB_KEY_Up:
				case XKB_KEY_KP_Up:
				case XKB_KEY_k:
					if (ctrlmode) {
						if (moment != UP)
							h = wh;
						h -= kspeed;
						moment = UP;
						if (h < 1)
							h = 1;
					} else
						off_y -= kspeed;
					break;
				case XKB_KEY_Down:
				case XKB_KEY_KP_Down:
				case XKB_KEY_j:
					if (ctrlmode) {
						if (moment != DOWN)
							h = wh;
						h += kspeed;
						moment = DOWN;
					} else
						off_y += kspeed;
					break;
				case XKB_KEY_Left:
				case XKB_KEY_KP_Left:
				case XKB_KEY_h:
					if (ctrlmode) {
						if (moment != LEFT)
							w = ww;
						w -= kspeed;
						if (w < 1)
							w = 1;
						moment = LEFT;
					} else
						off_x -= kspeed;
					break;
				case XKB_KEY_Right:
				case XKB_KEY_KP_Right:
				case XKB_KEY_l:
					if (ctrlmode) {
						if (moment != RIGHT)
							w = ww;
						w += kspeed;
						moment = RIGHT;
					} else
						off_x += kspeed;
					break;
				}

				ww = w;
				wh = h;
				wh -= vert_border;
				cw = ww - horz_border;
				wWindowConstrainSize(wwin, (unsigned int *)&cw, (unsigned int *)&wh);
				ww = cw + horz_border;
				wh += vert_border;

				if (w_global.config->ws_cycle) {
					if (src_x + off_x + ww < 20) {
						if (!scr->current_workspace)
							wWorkspaceChangeFromPointer(scr, scr->workspace_count - 1);
						else
							wWorkspaceChangeFromPointer(scr, scr->current_workspace - 1);

						off_x += scr_width;
					} else if (src_x + off_x + 20 > scr_width) {
						if (scr->current_workspace == scr->workspace_count - 1)
							wWorkspaceChangeFromPointer(scr, 0);
						else
							wWorkspaceChangeFromPointer(scr, scr->current_workspace + 1);

						off_x -= scr_width;
					}
				} else {
					if (src_x + off_x + ww < 20)
						off_x = 20 - ww - src_x;
					else if (src_x + off_x + 20 > scr_width)
						off_x = scr_width - 20 - src_x;
				}

				if (src_y + off_y + wh < 20) {
					off_y = 20 - wh - src_y;
				} else if (src_y + off_y + 20 > scr_height) {
					off_y = scr_height - 20 - src_y;
				}
			}
			break;
		case XCB_BUTTON_PRESS:
		case XCB_BUTTON_RELEASE:
			done = 1;
			break;
		case XCB_EXPOSE:
			WMHandleEvent(event);
			free(event);
			event = NULL;
			while (WXEvqTakeTyped(wdisp->conn, XCB_EXPOSE, XCB_NONE, &event)) {
				WMHandleEvent(event);
				free(event);
				event = NULL;
			}
			break;

		default:
			WMHandleEvent(event);
			break;
		}

		if (wwin->flags.shaded && !scr->selected_windows) {
			moveGeometryDisplayCentered(scr, src_x + off_x + w / 2, src_y + off_y + h / 2);
		} else {
			if (ctrlmode) {
				WMUnmapWidget(scr->gview);
				mapGeometryDisplay(wwin, src_x + off_x, src_y + off_y, ww, wh);
			} else if (!scr->selected_windows) {
				WMUnmapWidget(scr->gview);
				mapPositionDisplay(wwin, src_x + off_x, src_y + off_y, ww, wh);
			}
		}

		if (!opaqueMoveResize) {
			wWireframeBegin(scr);
			if (scr->selected_windows)
				addFrameOutlines(wwin, scr->selected_windows, off_x, off_y);
			else if (wwin->flags.shaded)
				addWindowOutline(wwin, src_x + off_x, src_y + off_y, w, h);
			else
				addWindowOutline(wwin, src_x + off_x, src_y + off_y, ww, wh);
			wWireframeCommit(scr);
		}

		if (ctrlmode) {
			showGeometry(wwin, src_x + off_x, src_y + off_y, src_x + off_x + ww, src_y + off_y + wh,
				     0);
		} else if (!scr->selected_windows)
			showPosition(wwin, src_x + off_x, src_y + off_y);

		if (opaqueMoveResize)
			wWindowConfigure(wwin, src_x + off_x, src_y + off_y, ww - horz_border,
					 wh - vert_border);

		if (done) {
			wWireframeHide(scr);

			if (ctrlmode) {
				showGeometry(wwin, src_x + off_x, src_y + off_y, src_x + off_x + ww,
					     src_y + off_y + wh, 0);
				WMUnmapWidget(scr->gview);
			} else
				WMUnmapWidget(scr->gview);

			wXUngrabKeyboard(wdisp->conn, XCB_CURRENT_TIME);
			wXUngrabPointer(wdisp->conn, XCB_CURRENT_TIME);

			/* XKeyEvent and XButtonEvent share layout up to .time
			 * so this reads correctly whichever ended the loop */
			wNETWMBumpUserTime(scr, (((xcb_button_press_event_t *)event)->time));

			if (done == 2) {
				if (wwin->flags.shaded || scr->selected_windows) {
					if (!scr->selected_windows) {
						wWindowMove(wwin, src_x + off_x, src_y + off_y);
						wWindowSynthConfigureNotify(wwin);
					} else {
						WMArrayIterator iter;
						WWindow *foo;

						doWindowMove(wwin, scr->selected_windows, off_x, off_y);

						WM_ITERATE_ARRAY(scr->selected_windows, foo, iter) {
							wWindowSynthConfigureNotify(foo);
						}
					}
				} else {
					if (ww != original_w)
						wwin->flags.maximized &= ~(MAX_HORIZONTAL | MAX_TOPHALF | MAX_BOTTOMHALF | MAX_MAXIMUS | MAX_CENTRAL);

					if (wh != original_h)
						wwin->flags.maximized &= ~(MAX_VERTICAL | MAX_LEFTHALF | MAX_RIGHTHALF | MAX_MAXIMUS | MAX_CENTRAL);

					wWindowConfigure(wwin, src_x + off_x, src_y + off_y,
							 ww - horz_border, wh - vert_border);
					wWindowSynthConfigureNotify(wwin);
				}
				wwin->last_head = wGetHeadForWindow(wwin);
				wWindowChangeWorkspace(wwin,
					wWorkspaceForHead(scr, wwin->last_head));
				wSetFocusTo(scr, wwin);
			}

			if (w_global.config->auto_arrange_icons && wXineramaHeads(scr) > 1 &&
			    head != wGetHeadForWindow(wwin)) {
				wArrangeIcons(scr, true);
			}

			update_saved_geometry(wwin);

			free(event);
			return 1;
		}
	}
}

/*
 *----------------------------------------------------------------------
 * wMouseMoveWindow--
 * 	Move the named window and the other selected ones (if any),
 * interactively. Also shows the position of the window, if only one
 * window is being moved.
 * 	If the window is not on the selected window list, the selected
 * windows are deselected.
 * 	If shift is pressed during the operation, the position display
 * is changed to another type.
 *
 * Returns:
 * 	True if the window was moved, False otherwise.
 *
 * Side effects:
 * 	The window(s) position is changed, and the client(s) are
 * notified about that.
 * 	The position display configuration may be changed.
 *----------------------------------------------------------------------
 */
int wMouseMoveWindow(WWindow * wwin, xcb_generic_event_t * ev)
{
	WScreen *scr = wwin->screen_ptr;
	xcb_generic_event_t *event = NULL;
	xcb_window_t root = scr->root_win;
	xcb_keycode_t shiftl, shiftr;
	bool done = false;
	int started = 0;
	int warped = 0;
	/* This needs not to change while moving, else bad things can happen */
	int opaqueMove = w_global.config->opaque_move;
	unsigned long crossing_begin = 0;
	MoveData moveData;
	int head = ((w_global.config->auto_arrange_icons && wXineramaHeads(scr) > 1)
		    ? wGetHeadForWindow(wwin)
		    : scr->xine_info.primary_head);

	if (!IS_MOVABLE(wwin))
		return false;

	if (w_global.config->opaque_move && !w_global.config->use_saveunders) {
		xcb_change_window_attributes(wdisp->conn, wwin->frame->core->window,
					     XCB_CW_SAVE_UNDER, (const uint32_t[]){ true });
	}

	initMoveData(wwin, &moveData);

	moveData.mouseX = ((xcb_motion_notify_event_t *)ev)->root_x;
	moveData.mouseY = ((xcb_motion_notify_event_t *)ev)->root_y;

	if (!wwin->flags.selected) {
		/* this window is not selected, unselect others and move only wwin */
		wUnselectWindows(scr);
	}
	shiftl = wXkbKeysymToKeycode(XKB_KEY_Shift_L);
	shiftr = wXkbKeysymToKeycode(XKB_KEY_Shift_R);
	while (!done) {
		if (warped) {
			int junk, rx, ry;
			unsigned int junkm;
			xcb_window_t junkw;

			/* wXWarpPointer() doesn't seem to generate Motion events, so
			 * we've got to simulate them */
			wXQueryPointer(wdisp->conn, root, &junkw, &junkw, &rx, &ry, &junk, &junk, &junkm);
			((xcb_motion_notify_event_t *)event)->root_x = rx;
			((xcb_motion_notify_event_t *)event)->root_y = ry;
		} else {
			/* the previous iteration's event is superseded,
			 * the warped branch reuses it so the free lives on
			 * this side */
			free(event);
			event = NULL;

			WMMaskEvent(wdisp->conn, XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_BUTTON_MOTION
				    | XCB_EVENT_MASK_POINTER_MOTION_HINT
				    | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_EXPOSURE, &event);

			if ((event->response_type & 0x7f) == XCB_MOTION_NOTIFY) {
				/* compress MotionNotify events, keep only
				 * the newest */
				xcb_generic_event_t *nev;

				while (WXEvqTakeMask(wdisp->conn, XCB_EVENT_MASK_BUTTON_MOTION, XCB_NONE, &nev)) {
					free(event);
					event = nev;
				}
				if (!checkMouseSamplingRate(&event))
					continue;
			}
		}
		switch ((event->response_type & 0x7f)) {
		case XCB_KEY_PRESS:
			if (((((xcb_key_press_event_t *)event)->detail) == shiftl || (((xcb_key_press_event_t *)event)->detail) == shiftr)
			    && started && !scr->selected_windows) {
				cyclePositionDisplay(wwin, moveData.realX, moveData.realY,
						     moveData.winWidth, moveData.winHeight);
			}
			break;

		case XCB_MOTION_NOTIFY:
			if (started) {
				updateWindowPosition(wwin, &moveData,
						     scr->selected_windows == NULL
						     && w_global.config->edge_resistance > 0,
						     opaqueMove, (((xcb_motion_notify_event_t *)event)->root_x), (((xcb_motion_notify_event_t *)event)->root_y));

				if (IS_RESIZABLE(wwin) && w_global.config->window_snapping) {
					int snap_direction;

					snap_direction = get_snap_direction(scr, moveData.mouseX, moveData.mouseY);

					if (!w_global.config->no_autowrap &&
					    snap_direction != SNAP_TOP &&
					    snap_direction != SNAP_BOTTOM)
						snap_direction = SNAP_NONE;

					moveData.snap = snap_direction;
				}

				if (!warped && !w_global.config->no_autowrap) {
					int oldWorkspace = scr->current_workspace;

					if (checkWorkspaceChange(wwin, &moveData, opaqueMove)) {
						if (scr->current_workspace != oldWorkspace
						    && w_global.config->edge_resistance > 0
						    && scr->selected_windows == NULL)
							updateMoveData(wwin, &moveData);
						warped = 1;
					}
				} else {
					warped = 0;
				}

				showMoveWireframe(wwin, &moveData, opaqueMove);
			} else if (abs(((xcb_motion_notify_event_t *)ev)->root_x - (((xcb_motion_notify_event_t *)event)->root_x)) >= MOVE_THRESHOLD
				   || abs(((xcb_motion_notify_event_t *)ev)->root_y - (((xcb_motion_notify_event_t *)event)->root_y)) >= MOVE_THRESHOLD) {

				/* open the Enter/Leave suppression bracket
				 * before the first frame-moving request
				 * (DRAGMAX_RESTORE wUnmaximizeWindow runs
				 * just below), every crossing generated
				 * until the ButtonRelease settle is then
				 * dropped by sequence at dispatch time */
				crossing_begin = wEventIgnoreCrossingBegin();

				if (wwin->flags.tiled) {
					/* pop the window out of the layout at
					 * the first real motion, so the drag
					 * starts from where the user grabbed
					 * it and a plain click never ejects
					 * the window from the layout */
					wwin->flags.tiled = 0;
					wwin->flags.no_tiling = 1;
					wLayoutApplyForWindow(wwin);
				}

				if (wwin->flags.maximized) {
					if (w_global.config->drag_maximized_window == DRAGMAX_RESTORE) {
						float titlebar_ratio;
						int new_x, new_y;

						titlebar_ratio = (moveData.mouseX - wwin->frame_x) /
							(float)wwin->frame->core->width;
						new_y = wwin->frame_y;
						wUnmaximizeWindow(wwin);
						new_x = moveData.mouseX - titlebar_ratio * wwin->frame->core->width;
						wWindowMove(wwin, new_x, new_y);
						moveData.realX = moveData.calcX = wwin->frame_x;
						moveData.realY = moveData.calcY = wwin->frame_y;
					}
					if (w_global.config->drag_maximized_window == DRAGMAX_UNMAXIMIZE) {
						wwin->flags.maximized = 0;
						wwin->flags.old_maximized = 0;
					}
				}

				wXChangeActivePointerGrab(wdisp->conn, XCB_EVENT_MASK_BUTTON_MOTION
							 | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS,
							 wdisp->cursor[WCUR_MOVE], XCB_CURRENT_TIME);
				started = 1;
				wXGrabKeyboard(wdisp->conn, root, false, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_CURRENT_TIME);

				if (!scr->selected_windows)
					mapPositionDisplay(wwin, moveData.realX, moveData.realY,
							   moveData.winWidth, moveData.winHeight);

				showMoveWireframe(wwin, &moveData, opaqueMove);
			}

			break;

		case XCB_BUTTON_PRESS:
			break;

		case XCB_BUTTON_RELEASE:
			if (!wwin->moveresize.active && ((((xcb_button_press_event_t *)event)->detail) != ((xcb_button_press_event_t *)ev)->detail))
				break;

			wWireframeHide(scr);

			if (started) {
				if (moveData.snap)
					do_snap(wwin, &moveData);
				else if (!opaqueMove)
					doWindowMove(wwin, scr->selected_windows,
						     moveData.realX - wwin->frame_x,
						     moveData.realY - wwin->frame_y);
#ifndef CONFIGURE_WINDOW_WHILE_MOVING
				wWindowSynthConfigureNotify(wwin);
#endif
				wXUngrabKeyboard(wdisp->conn, XCB_CURRENT_TIME);
				wwin->last_head = wGetHeadForWindow(wwin);
				if (!opaqueMove) {
					wWindowChangeWorkspace(wwin,
						wWorkspaceForHead(scr, wwin->last_head));
					wSetFocusTo(scr, wwin);
				} else if (w_global.config->per_head_workspaces) {
					/* opaque drag across a head boundary
					 * adopts that head's workspace */
					wWindowChangeWorkspace(wwin,
						wWorkspaceForHead(scr, wwin->last_head));
					wSetFocusTo(scr, wwin);
				}
				/* close the suppression bracket after the
				 * last frame-moving request, the
				 * NotifyUngrab crossing from the later
				 * XUngrabPointer falls outside the range
				 * so focus-follows-mouse still sees where
				 * the pointer landed */
				wEventIgnoreCrossingEnd(crossing_begin);

				if (!scr->selected_windows) {
					/* get rid of the geometry window */
					WMUnmapWidget(scr->gview);
				}
			}
			if (wwin->moveresize.active) {
				wXUngrabPointer(wdisp->conn, XCB_CURRENT_TIME);
				wwin->moveresize.active = 0;
			}
			/* the drag loop is modal so any MapRequest that arrived
			 * during the move was queued, advance the screen
			 * baseline to the release timestamp so a stale-stamped
			 * window processed next does not steal focus */
			wNETWMBumpUserTime(scr, (((xcb_button_press_event_t *)event)->time));
			done = true;
			break;

		default:
			WMHandleEvent(event);
			break;
		}
	}

	free(event);

	if (w_global.config->opaque_move && !w_global.config->use_saveunders) {
		xcb_change_window_attributes(wdisp->conn, wwin->frame->core->window,
					     XCB_CW_SAVE_UNDER, (const uint32_t[]){ false });
	}

	freeMoveData(&moveData);

	if (started && w_global.config->auto_arrange_icons && wXineramaHeads(scr) > 1 &&
	    head != wGetHeadForWindow(wwin)) {
		wArrangeIcons(scr, true);
	}

	if (started)
		update_saved_geometry(wwin);

	return started;
}

/*
 * Adjust the proposed frame width and height so the moving edge of an
 * interactive resize lands on the closest snap target within
 * EdgeResistance pixels. The usable-area boundary on the head under
 * the pointer is always a resistance target, so a window growing
 * outward sticks at it the same way the move path resists dragging
 * past the screen edge. When Attraction is enabled the boundary
 * becomes magnetic both ways and both edges of every visible
 * neighbour that overlaps the window on the perpendicular axis are
 * added as targets, the facing edge for abutment and the same-side
 * edge for alignment. The raw rw and rh accumulators in the caller
 * keep tracking the pointer so the snap releases once the drag passes
 * the resistance zone, and wWindowConstrainSize runs after this so
 * size-hint increments still round the result.
 */
static void snapResizeGeometry(WWindow *wwin, int res, int rx1, int ry1,
			       int rx2, int ry2, int dw, int dh,
			       int *fw, int *fh)
{
	WScreen *scr = wwin->screen_ptr;
	int resist = WIN_RESISTANCE(w_global.config->edge_resistance);
	bool attract = w_global.config->attract;
	int fb2 = HAS_BORDER_WITH_SELECT(wwin) ? 2 * wFrameBorderWidth(wwin) : 0;
	int span_l = WLEFT(wwin), span_r = WRIGHT(wwin);
	int span_t = WTOP(wwin), span_b = WBOTTOM(wwin);
	int cand_x = 0, cand_y = 0;
	int best_x = 0, best_y = 0;
	int dist_x, dist_y;
	int head, scr_lo, scr_hi, e, d, nfw, nfh;
	WMRect rect;
	WWindow *tmp;

	if (resist <= 0)
		return;

	dist_x = dist_y = resist + 1;

	/* moving-edge candidate, frame-border inclusive: the left and top
	 * edges are the frame origin, the right and bottom edges are one
	 * past the last pixel so they compare directly against WLEFT and
	 * WRIGHT+1 of a neighbour */
	if (res & LEFT)
		cand_x = rx2 + 1 - *fw;
	else if (res & RIGHT)
		cand_x = rx1 + *fw + fb2;
	if (res & UP)
		cand_y = ry2 + 1 - *fh;
	else if (res & DOWN)
		cand_y = ry1 + *fh + fb2;

	head = wGetHeadForPointerLocation(scr);
	rect = wGetRectForHead(scr, head);

	if (res & (LEFT | RIGHT)) {
		scr_lo = WMAX(scr->totalUsableArea[head].x1, rect.pos.x);
		scr_hi = WMIN(scr->totalUsableArea[head].x2, rect.pos.x + rect.size.width);
		e = (res & LEFT) ? scr_lo : scr_hi;
		d = abs(cand_x - e);
		if (d < dist_x
		    && (attract || (dw > 0 && ((res & LEFT) ? cand_x <= e : cand_x >= e)))) {
			best_x = e;
			dist_x = d;
		}
	}
	if (res & (UP | DOWN)) {
		scr_lo = WMAX(scr->totalUsableArea[head].y1, rect.pos.y);
		scr_hi = WMIN(scr->totalUsableArea[head].y2, rect.pos.y + rect.size.height);
		e = (res & UP) ? scr_lo : scr_hi;
		d = abs(cand_y - e);
		if (d < dist_y
		    && (attract || (dh > 0 && ((res & UP) ? cand_y <= e : cand_y >= e)))) {
			best_y = e;
			dist_y = d;
		}
	}

	if (attract) {
		for (tmp = scr->focused_window; tmp; tmp = tmp->prev) {
			if (tmp == wwin || !wWindowIsVisibleOnAnyHead(tmp)
			    || tmp->flags.miniaturized || tmp->flags.hidden
			    || tmp->flags.obscured || WFLAGP(tmp, sunken))
				continue;

			if ((res & (LEFT | RIGHT))
			    && span_t <= WBOTTOM(tmp) && span_b >= WTOP(tmp)) {
				e = (res & LEFT) ? WRIGHT(tmp) + 1 : WLEFT(tmp);
				d = abs(cand_x - e);
				if (d < dist_x) {
					best_x = e;
					dist_x = d;
				}
				e = (res & LEFT) ? WLEFT(tmp) : WRIGHT(tmp) + 1;
				d = abs(cand_x - e);
				if (d < dist_x) {
					best_x = e;
					dist_x = d;
				}
			}
			if ((res & (UP | DOWN))
			    && span_l <= WRIGHT(tmp) && span_r >= WLEFT(tmp)) {
				e = (res & UP) ? WBOTTOM(tmp) + 1 : WTOP(tmp);
				d = abs(cand_y - e);
				if (d < dist_y) {
					best_y = e;
					dist_y = d;
				}
				e = (res & UP) ? WTOP(tmp) : WBOTTOM(tmp) + 1;
				d = abs(cand_y - e);
				if (d < dist_y) {
					best_y = e;
					dist_y = d;
				}
			}
		}
	}

	if (dist_x <= resist) {
		nfw = (res & LEFT) ? rx2 + 1 - best_x : best_x - rx1 - fb2;
		if (nfw > 0)
			*fw = nfw;
	}
	if (dist_y <= resist) {
		nfh = (res & UP) ? ry2 + 1 - best_y : best_y - ry1 - fb2;
		if (nfh > 0)
			*fh = nfh;
	}
}

bool find_nearest_edge(WWindow *self, int head, int workspace,
		       int direction, int from_offset, int to_offset,
		       int axis_lo, int axis_hi, int max_dist, int gap,
		       int seed_gap, int *best_offset)
{
	WScreen *scr = self->screen_ptr;
	WWindow *tmp;
	bool found = false;
	bool decreasing;
	int best, cand, dist;

	switch (direction) {
	case DIRECTION_LEFT:
	case DIRECTION_UP:
		decreasing = true;
		best = to_offset + seed_gap;
		break;
	case DIRECTION_RIGHT:
	case DIRECTION_DOWN:
		decreasing = false;
		best = to_offset - seed_gap;
		break;
	default:
		return false;
	}

	dist = decreasing ? (from_offset - best) : (best - from_offset);
	if (dist < 0)
		dist = -dist;
	if (dist <= max_dist)
		found = true;

	for (tmp = scr->focused_window; tmp; tmp = tmp->prev) {
		if (tmp == self)
			continue;
		if (tmp->flags.miniaturized || tmp->flags.hidden)
			continue;
		if (!wWindowIsVisibleOnAnyHead(tmp))
			continue;
		if (!WWIN_ON_WORKSPACE(tmp, workspace))
			continue;
		if (wGetHeadForWindow(tmp) != head)
			continue;

		if (direction == DIRECTION_LEFT || direction == DIRECTION_RIGHT) {
			if (WTOP(tmp) > axis_hi || WBOTTOM(tmp) < axis_lo)
				continue;
			if (direction == DIRECTION_LEFT)
				cand = WRIGHT(tmp) + 1 + gap;
			else
				cand = WLEFT(tmp) - gap;
		} else {
			if (WLEFT(tmp) > axis_hi || WRIGHT(tmp) < axis_lo)
				continue;
			if (direction == DIRECTION_UP)
				cand = WBOTTOM(tmp) + 1 + gap;
			else
				cand = WTOP(tmp) - gap;
		}

		if (decreasing) {
			if (cand > from_offset || cand < to_offset)
				continue;
		} else {
			if (cand < from_offset || cand > to_offset)
				continue;
		}

		dist = decreasing ? (from_offset - cand) : (cand - from_offset);
		if (dist < 0)
			dist = -dist;
		if (dist > max_dist)
			continue;

		if (!found || (decreasing ? (cand > best) : (cand < best))) {
			best = cand;
			found = true;
		}
	}

	if (found)
		*best_offset = best;
	return found;
}

/*
 * Shared-edge resize: when the grabbed edge of an interactive resize is
 * the seam of a tiled half/quarter arrangement, the window across the
 * seam is resized in lockstep, one growing by d while the other shrinks.
 */

/* abutment tolerance: WindowGap, with slack for hand placement */
#define SHARED_EDGE_ABUT_TOL()	WMAX(w_global.config->window_gap, 8)

typedef struct {
	WWindow *partner;
	int side;		/* +1: partner right/below the seam, -1 left/above */
	int gap;		/* seam gap measured at drag start */
	int clamp_lo;		/* inclusive seam range from both min sizes */
	int clamp_hi;
	int fixed_far;		/* partner outer boundary away from the seam */
	int fixed_near;		/* grabbed window boundary opposite the seam */
	int fb2;		/* partner frame border term */
	int inner;		/* partner il+ir (X) or it+ib (Y) */
	int titlebar;		/* partner top_width+bottom_width (Y only) */
	int min;		/* partner minimum client extent on the axis */
	int px, py, pw, ph;	/* last computed partner client geometry */
} SharedEdgePair;

static int shared_edge_min_client_extent(WWindow *wwin, int horizontal)
{
	int minv = 1;

	if (wwin->normal_hints) {
		if (horizontal) {
			if (wwin->normal_hints->min_width > minv)
				minv = wwin->normal_hints->min_width;
		} else {
			if (wwin->normal_hints->min_height > minv)
				minv = wwin->normal_hints->min_height;
		}
	}
	if (minv < MIN_WINDOW_SIZE)
		minv = MIN_WINDOW_SIZE;

	return minv;
}

/* Allowed range for one client dimension of wwin while the other
 * dimension is pinned at other: minimum and maximum size hints plus
 * the PAspect span projected on the dragged axis. Upper bounds round
 * down and lower bounds round up, so both stay exact in integers. */
static void shared_edge_dim_range(WWindow *wwin, int horizontal, int other,
				  int *lo, int *hi)
{
	int minv = 1;
	int maxv = horizontal ? wwin->screen_ptr->scr_width * 2
			      : wwin->screen_ptr->scr_height * 2;
	int minax = -1, minay = -1, maxax = -1, maxay = -1;

	if (wwin->normal_hints) {
		if (horizontal) {
			if (wwin->normal_hints->min_width > minv)
				minv = wwin->normal_hints->min_width;
			if (wwin->normal_hints->max_width < maxv)
				maxv = wwin->normal_hints->max_width;
		} else {
			if (wwin->normal_hints->min_height > minv)
				minv = wwin->normal_hints->min_height;
			if (wwin->normal_hints->max_height < maxv)
				maxv = wwin->normal_hints->max_height;
		}
		if (wwin->normal_hints->flags & XCB_ICCCM_SIZE_HINT_P_ASPECT) {
			minax = wwin->normal_hints->min_aspect_num;
			minay = wwin->normal_hints->min_aspect_den;
			maxax = wwin->normal_hints->max_aspect_num;
			maxay = wwin->normal_hints->max_aspect_den;
		}
	}

	if (horizontal) {
		if (maxax > 0 && !(maxax == 1 && maxay == 1)) {
			int64_t m = (int64_t)other * maxax / maxay;

			if (m < maxv)
				maxv = m < INT_MIN ? INT_MIN : (int)m;
		}
		if (minax > 0 && !(minax == 1 && minay == 1)) {
			int64_t m = ((int64_t)other * minax + minay - 1) / minay;

			if (m > minv)
				minv = m > INT_MAX ? INT_MAX : (int)m;
		}
	} else {
		if (maxax > 0 && !(maxax == 1 && maxay == 1)) {
			int64_t m = ((int64_t)other * maxay + maxax - 1) / maxax;

			if (m > minv)
				minv = m > INT_MAX ? INT_MAX : (int)m;
		}
		if (minax > 0 && !(minax == 1 && minay == 1)) {
			int64_t m = (int64_t)other * minay / minax;

			if (m < maxv)
				maxv = m < INT_MIN ? INT_MIN : (int)m;
		}
	}

	*lo = minv;
	*hi = maxv;
}

/* Nearest value for one client dimension of wwin on its resize
 * increment grid, inside [lo, hi]. Returns dim unchanged when the
 * span holds no grid point, so the hard bounds win over the grid. */
static int shared_edge_snap_dim(WWindow *wwin, int horizontal, int dim,
				int lo, int hi)
{
	int base = 1, inc = 1;
	int64_t k, rem, floorv, ceilv;

	if (wwin->normal_hints) {
		base = horizontal ? wwin->normal_hints->base_width
				  : wwin->normal_hints->base_height;
		inc = horizontal ? wwin->normal_hints->width_inc
				 : wwin->normal_hints->height_inc;
		if (base == 0)
			base = horizontal ? wwin->normal_hints->min_width
					  : wwin->normal_hints->min_height;
	}
	if (inc < 1)
		inc = 1;
	if (base < 1)
		base = 1;

	k = ((int64_t)dim - base) / inc;
	rem = ((int64_t)dim - base) % inc;
	if (rem < 0) {
		k--;
		rem += inc;
	}
	floorv = base + k * inc;
	ceilv = (rem > 0) ? floorv + inc : floorv;

	if (floorv >= lo && floorv <= hi) {
		if (ceilv >= lo && ceilv <= hi && ceilv - dim < dim - floorv)
			return (int)ceilv;
		return (int)floorv;
	}
	if (ceilv >= lo && ceilv <= hi)
		return (int)ceilv;

	return dim;
}

/* Partner client geometry for a seam at the given coordinate. The far
 * edge stays fixed, the near edge follows the seam at the recorded
 * distance. Client dimensions come out through the partner's own
 * border widths, never the grabbed window's. */
static void shared_edge_partner_geometry(SharedEdgePair *p, int axis, int seam)
{
	if (axis == 0) {
		if (p->side > 0)
			p->px = seam + p->gap;
		else
			p->px = p->fixed_far;
		p->pw = ((p->side > 0) ? p->fixed_far : seam - p->gap)
			- p->px - p->fb2 - p->inner;
	} else {
		if (p->side > 0)
			p->py = seam + p->gap;
		else
			p->py = p->fixed_far;
		p->ph = ((p->side > 0) ? p->fixed_far : seam - p->gap)
			- p->py - p->fb2 - p->titlebar - p->inner;
	}
}

/*
 * Find the window tiled against wwin on one axis. axis is 0 for an X
 * split (left/right seam) or 1 for a Y split (top/bottom seam), side is
 * the direction to look in relative to wwin (+1 right/below, -1
 * left/above). Detection is purely geometric and seam-local: any
 * resizable window abutting wwin across the split axis and matching
 * wwin's extent on the perpendicular axis qualifies. Returns at most
 * one partner, refusing ambiguous arrangements.
 */
static WWindow *wFindTilePartnerAxis(WWindow *wwin, int axis, int side)
{
	WScreen *scr = wwin->screen_ptr;
	int head = wGetHeadForWindow(wwin);
	int tol = SHARED_EDGE_ABUT_TOL();
	int self_left = WLEFT(wwin), self_right = WRIGHT(wwin) + 1;
	int self_top = WTOP(wwin), self_bottom = WBOTTOM(wwin) + 1;
	int self_lo, self_hi, facing;
	WWindow *tmp, *best;
	int best_diff;
	bool ambiguous;

	if (axis == 0) {
		self_lo = self_top;
		self_hi = self_bottom;
		facing = (side > 0) ? self_right : self_left;
	} else {
		self_lo = self_left;
		self_hi = self_right;
		facing = (side > 0) ? self_bottom : self_top;
	}

	best = NULL;
	best_diff = 0;
	ambiguous = false;

	for (tmp = scr->focused_window; tmp; tmp = tmp->prev) {
		int cand_lo, cand_hi, cand_facing, cand_span;
		int overlap, span, diff;

		/* a shaded window passes every geometric test but cannot
		 * be meaningfully resized, just skip it. Transients are
		 * never tile partners either. */
		if (tmp == wwin || tmp->flags.miniaturized || tmp->flags.hidden
		    || tmp->flags.shaded || tmp->flags.obscured
		    || WFLAGP(tmp, sunken) || tmp->transient_for != XCB_NONE)
			continue;
		if (!IS_RESIZABLE(tmp))
			continue;
		if (!WWIN_ON_WORKSPACE(tmp, scr->current_workspace))
			continue;
		if (wGetHeadForWindow(tmp) != head)
			continue;

		if (axis == 0) {
			cand_lo = WTOP(tmp);
			cand_hi = WBOTTOM(tmp) + 1;
			cand_facing = (side > 0) ? WLEFT(tmp) : WRIGHT(tmp) + 1;
		} else {
			cand_lo = WLEFT(tmp);
			cand_hi = WRIGHT(tmp) + 1;
			cand_facing = (side > 0) ? WTOP(tmp) : WBOTTOM(tmp) + 1;
		}

		/* abutting across the split axis */
		if (abs(cand_facing - facing) > tol)
			continue;

		/* matched extent on the perpendicular axis: >90% overlap
		 * and no grossly different span, so a half next to a
		 * quarter refuses while hand-placement drift pairs. The
		 * floor absorbs row quantization of terminal clients. */
		cand_span = cand_hi - cand_lo;
		diff = abs(cand_span - (self_hi - self_lo));
		overlap = WMIN(self_hi, cand_hi) - WMAX(self_lo, cand_lo);
		span = WMIN(self_hi - self_lo, cand_span);
		if (overlap <= 0 || overlap * 10 <= span * 9)
			continue;
		if (diff > WMAX(span / 8, 28))
			continue;

		if (!best || diff < best_diff) {
			best = tmp;
			best_diff = diff;
			ambiguous = false;
		} else if (diff == best_diff) {
			ambiguous = true;
		}
	}

	if (ambiguous)
		return NULL;

	return best;
}

/* Record the seam constants for one axis of a drag. Everything the
 * per-motion math needs is fixed at start: the partner's far edge, the
 * gap, and the clamp range derived from both windows' minimum sizes.
 * If that range is empty the pairing is refused and the axis falls
 * back to a plain single-window resize. */
static void shared_edge_pair_setup(SharedEdgePair *p, WWindow *wwin,
				   int axis, int side,
				   int origin, int end, int vert_border)
{
	WWindow *partner;
	int gap, self_inner, self_min, far_total, seam0;
	int il, ir, it, ib;

	memset(p, 0, sizeof(*p));

	partner = wFindTilePartnerAxis(wwin, axis, side);
	if (!partner)
		return;

	wFrameInnerBorderWidths(wwin, &il, &ir, &it, &ib);
	self_inner = il + ir;
	self_min = shared_edge_min_client_extent(wwin, axis == 0);

	p->fb2 = HAS_BORDER_WITH_SELECT(partner) ? 2 * wFrameBorderWidth(partner) : 0;
	wFrameInnerBorderWidths(partner, &il, &ir, &it, &ib);
	if (axis == 0) {
		p->inner = il + ir;
		p->titlebar = 0;
	} else {
		p->inner = it + ib;
		p->titlebar = partner->frame->top_width + partner->frame->bottom_width;
	}
	p->min = shared_edge_min_client_extent(partner, axis == 0);
	far_total = p->min + p->fb2 + p->inner + p->titlebar;

	/* the seam tracks the grabbed window's moving edge in the same
	 * frame-core coordinates the resize loop uses (fx+fw for a RIGHT
	 * grab and so on), so the recorded gap absorbs any outer border
	 * and the pair never shifts at drag start */
	if (side > 0) {
		if (axis == 0) {
			gap = WLEFT(partner) - (WLEFT(wwin) + (int)wwin->frame->core->width);
			p->fixed_far = WRIGHT(partner) + 1;
			p->clamp_lo = origin + self_min + self_inner;
		} else {
			gap = WTOP(partner) - (WTOP(wwin) + (int)wwin->frame->core->height);
			p->fixed_far = WBOTTOM(partner) + 1;
			p->clamp_lo = origin + self_min + vert_border;
		}
		p->clamp_hi = p->fixed_far - gap - far_total;
	} else {
		if (axis == 0) {
			gap = WLEFT(wwin) - (WRIGHT(partner) + 1);
			p->fixed_far = WLEFT(partner);
			p->fixed_near = end + 1;
			p->clamp_hi = end + 1 - self_min - self_inner;
		} else {
			gap = WTOP(wwin) - (WBOTTOM(partner) + 1);
			p->fixed_far = WTOP(partner);
			p->fixed_near = end + 1;
			p->clamp_hi = end + 1 - self_min - vert_border;
		}
		p->clamp_lo = p->fixed_far + gap + far_total;
	}

	if (p->clamp_lo > p->clamp_hi) {
		/* both min sizes exceed the span, plain resize instead */
		memset(p, 0, sizeof(*p));
		return;
	}

	p->partner = partner;
	p->side = side;
	p->gap = gap;

	p->px = WLEFT(partner);
	p->py = WTOP(partner);
	p->pw = partner->client.width;
	p->ph = partner->client.height;

	seam0 = origin;
	if (side > 0)
		seam0 += (axis == 0) ? (int)wwin->frame->core->width
				     : (int)wwin->frame->core->height;
	shared_edge_partner_geometry(p, axis, seam0);
}

/* Slide each active seam to the grabbed window's moving edge, clamp
 * so neither window violates its minimum size, then reconcile the
 * dragged dimension of both windows with their size hints (minimum,
 * maximum, aspect, resize increments) and recompute the grabbed
 * dimension accordingly. The seam ends on the grabbed window's
 * constrained edge. The partner keeps its far edge fixed and lets
 * its near edge absorb the rounding, so it can sit up to one resize
 * increment away from the seam. The axes are independent. */
static void shared_edge_adjust(SharedEdgePair *pair_x, SharedEdgePair *pair_y,
			       WWindow *wwin, int vert_border,
			       int *fx, int *fy, int *fw, int *fh)
{
	SharedEdgePair *pairs[2];
	int i, seam;

	pairs[0] = pair_x;
	pairs[1] = pair_y;

	for (i = 0; i < 2; i++) {
		SharedEdgePair *p = pairs[i];
		int origin, chrome, other;
		int g, glo, ghi, dlo, dhi;
		int praw, plo, phi, pfin;
		int il, ir;

		if (!p->partner)
			continue;

		wFrameInnerBorderWidths(wwin, &il, &ir, NULL, NULL);
		if (i == 0) {
			origin = *fx;
			/* the loop's dimensions are frame-outer, the hint
			 * reconciliation below works on the client extent */
			chrome = il + ir;
			other = *fh - vert_border;
			seam = (p->side > 0) ? *fx + *fw : *fx;
		} else {
			origin = *fy;
			chrome = vert_border;
			other = *fw - (il + ir);
			seam = (p->side > 0) ? *fy + *fh : *fy;
		}

		if (seam < p->clamp_lo)
			seam = p->clamp_lo;
		else if (seam > p->clamp_hi)
			seam = p->clamp_hi;

		/* grabbed dimension in the units wWindowConstrainSize
		 * works in, bounded by the clamped seam range and the
		 * window's own hints, then snapped to its resize grid */
		if (p->side > 0) {
			g = seam - origin - chrome;
			glo = p->clamp_lo - origin - chrome;
			ghi = p->clamp_hi - origin - chrome;
		} else {
			g = p->fixed_near - seam - chrome;
			glo = p->fixed_near - p->clamp_hi - chrome;
			ghi = p->fixed_near - p->clamp_lo - chrome;
		}

		shared_edge_dim_range(wwin, i == 0, other, &dlo, &dhi);
		glo = WMAX(glo, dlo);
		ghi = WMIN(ghi, dhi);
		if (glo > ghi) {
			/* the paired hint sets cannot both hold, drop
			 * the pairing and let the axis resize alone */
			p->partner = NULL;
			continue;
		}

		/* wWindowConstrainSize skips the resize increments for
		 * maximized windows, so the snap does too */
		if (!wwin->flags.maximized)
			g = shared_edge_snap_dim(wwin, i == 0, g, glo, ghi);

		if (p->side > 0)
			seam = origin + g + chrome;
		else
			seam = p->fixed_near - g - chrome;

		if (i == 0) {
			*fw = g + chrome;
			if (p->side < 0)
				*fx = seam;
		} else {
			*fh = g + chrome;
			if (p->side < 0)
				*fy = seam;
		}

		/* partner geometry from the final seam, then the same
		 * hint reconciliation on the partner's dimension. The
		 * far edge stays put and the near edge takes the
		 * rounding */
		shared_edge_partner_geometry(p, i, seam);

		praw = (i == 0) ? p->pw : p->ph;
		shared_edge_dim_range(p->partner, i == 0,
				      (i == 0) ? p->ph : p->pw, &plo, &phi);
		if (!p->partner->flags.maximized)
			pfin = shared_edge_snap_dim(p->partner, i == 0, praw, plo, phi);
		else
			pfin = praw;

		if (pfin != praw) {
			if (i == 0) {
				if (p->side > 0)
					p->px -= pfin - praw;
				p->pw = pfin;
			} else {
				if (p->side > 0)
					p->py -= pfin - praw;
				p->ph = pfin;
			}
		}
	}
}

/* Live partner update on the opaque path, paced by the partner's own
 * _NET_WM_SYNC state so the pair cannot desync. */
static void shared_edge_apply_motion(SharedEdgePair *p)
{
	if (!p->partner || p->pw < 1 || p->ph < 1)
		return;

	if (!wNETWMSyncRequestPending(p->partner)) {
		wNETWMSyncRequestSend(p->partner);
		wWindowConfigure(p->partner, p->px, p->py, p->pw, p->ph);
	}
}

/*
 * A completed paired drag leaves every moved window matching no
 * maximize layout, so drop all maximize state instead of leaving bits
 * that make the next titlebar drag restore stale pre-maximize
 * geometry.
 */
static void shared_edge_forget_maximized(WWindow *wwin)
{
	if (!wwin || !(wwin->flags.maximized || wwin->flags.old_maximized))
		return;

	wwin->flags.maximized = 0;
	wwin->flags.old_maximized = 0;
	wWindowUpdateButtonImages(wwin);
	WMPostNotificationName(WMNChangedState, wwin, "maximize");
}

#define RESIZEBAR	1
#define HCONSTRAIN	2

static int getResizeDirection(WWindow * wwin, int x, int y, int dy, int flags)
{
	int w = wwin->frame->core->width - 1;
	int cw = wwin->frame->resizebar_corner_width;
	int dir;

	/* if not resizing through the resizebar */
	if (!(flags & RESIZEBAR)) {

		int xdir = (abs(x) < (wwin->client.width / 2)) ? LEFT : RIGHT;
		int ydir = (abs(y) < (wwin->client.height / 2)) ? UP : DOWN;

		/* How much resize space is allowed */
		int spacew = wwin->client.width / 3;
		int spaceh = wwin->client.height / 3;

		/* Determine where x fits */
		if ((abs(x) > wwin->client.width/2 - spacew/2) &&
		    (abs(x) < wwin->client.width/2 + spacew/2)) {
			/* Resize vertically */
			xdir = 0;
		} else if ((abs(y) > wwin->client.height/2 - spaceh/2) &&
		           (abs(y) < wwin->client.height/2 + spaceh/2)) {
			/* Resize horizontally */
			ydir = 0;
		}
		return (xdir | ydir);
	}

	/* window is too narrow. allow diagonal resize */
	if (cw * 2 >= w) {
		int ydir;

		if (flags & HCONSTRAIN)
			ydir = 0;
		else
			ydir = DOWN;
		if (x < cw)
			return (LEFT | ydir);
		else
			return (RIGHT | ydir);
	}
	/* vertical resize */
	if ((x > cw) && (x < w - cw))
		return DOWN;

	if (x < cw)
		dir = LEFT;
	else
		dir = RIGHT;

	if ((abs(dy) > 0) && !(flags & HCONSTRAIN))
		dir |= DOWN;

	return dir;
}

void wMouseResizeWindow(WWindow * wwin, xcb_generic_event_t * ev)
{
	xcb_generic_event_t *event = NULL;
	WScreen *scr = wwin->screen_ptr;
	xcb_window_t root = scr->root_win;
	int il = 0, ir = 0, it = 0, ib = 0;
	int vert_border = wwin->frame->top_width + wwin->frame->bottom_width;
	int horz_border = 0;
	int fw = wwin->frame->core->width;
	int fh = wwin->frame->core->height;
	int fx = wwin->frame_x;
	int fy = wwin->frame_y;
	int is_resizebar = (wwin->frame->resizebar && W_EventWindow(ev) == wwin->frame->resizebar->window);
	int orig_x, orig_y;
	int started;
	int dw, dh;
	int cfw;
	int rw = fw, rh = fh;
	int rx1, ry1, rx2, ry2;
	int res = 0;
	xcb_keycode_t shiftl, shiftr;
	int orig_fw = fw;
	int orig_fh = fh;
	int original_fw = fw;
	int original_fh = fh;
	int head = ((w_global.config->auto_arrange_icons && wXineramaHeads(scr) > 1)
		    ? wGetHeadForWindow(wwin)
		    : scr->xine_info.primary_head);
	int opaqueResize = w_global.config->opaque_resize;
	SharedEdgePair pair_x, pair_y;

	pair_x.partner = NULL;
	pair_y.partner = NULL;

	if (HAS_BORDER(wwin))
		wFrameInnerBorderWidths(wwin, &il, &ir, &it, &ib);
	vert_border += it + ib;
	horz_border = il + ir;

	if (!IS_RESIZABLE(wwin))
		return;

#ifdef USE_XSYNC
	/* a previous drag may have given up on this client, every new
	 * interactive resize gets a fresh chance to keep up, and the last
	 * request from that drag may have been acked through the main
	 * loop after the resize loop returned so do not carry the flag */
	wwin->net_sync.disabled = 0;
	wwin->net_sync.waiting = 0;
#endif

	if (wwin->flags.shaded) {
		wwarning("internal error: tryein");
		return;
	}
	if ((ev->response_type & 0x7f) == XCB_MOTION_NOTIFY) {
		orig_x = ((xcb_motion_notify_event_t *)ev)->root_x;
		orig_y = ((xcb_motion_notify_event_t *)ev)->root_y;
	} else {
		orig_x = ((xcb_button_press_event_t *)ev)->root_x;
		orig_y = ((xcb_button_press_event_t *)ev)->root_y;
	}

	started = 0;
	wUnselectWindows(scr);
	rx1 = fx;
	rx2 = fx + fw - 1;
	ry1 = fy;
	ry2 = fy + fh - 1;
	shiftl = wXkbKeysymToKeycode(XKB_KEY_Shift_L);
	shiftr = wXkbKeysymToKeycode(XKB_KEY_Shift_R);

	if (wwin->moveresize.active) {
		int direction = wwin->moveresize.resize_edge;

		res = 0;
		is_resizebar = 0;
		if (direction == _NET_WM_MOVERESIZE_SIZE_TOP)
			res |= UP;
		else if (direction == _NET_WM_MOVERESIZE_SIZE_BOTTOM)
			res |= DOWN;
		else if (direction == _NET_WM_MOVERESIZE_SIZE_LEFT)
			res |= LEFT;
		else if (direction == _NET_WM_MOVERESIZE_SIZE_RIGHT)
			res |= RIGHT;
		else if (direction == _NET_WM_MOVERESIZE_SIZE_TOPLEFT)
			res |= (UP | LEFT);
		else if (direction == _NET_WM_MOVERESIZE_SIZE_TOPRIGHT)
			res |= (UP | RIGHT);
		else if (direction == _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT)
			res |= (DOWN | LEFT);
		else if (direction == _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT)
			res |= (DOWN | RIGHT);
	}

	while (1) {
		free(event);
		event = NULL;

		WMMaskEvent(wdisp->conn, XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_BUTTON_MOTION
			    | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION_HINT | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_EXPOSURE, &event);

		if ((event->response_type & 0x7f) == XCB_MOTION_NOTIFY) {
			/* the opaque path skips the configure while a
			 * _NET_WM_SYNC_REQUEST is outstanding, during which
			 * motion events queue, drain to the latest position
			 * the same way the move loop does so the next
			 * configure applies the current pointer position */
			xcb_generic_event_t *nev;

			while (WXEvqTakeMask(wdisp->conn, XCB_EVENT_MASK_BUTTON_MOTION, XCB_NONE, &nev)) {
				free(event);
				event = nev;
			}
			if (!checkMouseSamplingRate(&event))
				continue;
		}

		switch ((event->response_type & 0x7f)) {
		case XCB_KEY_PRESS:
			showGeometry(wwin, fx, fy, fx + fw, fy + fh, res);
			if (!opaqueResize && started
			    && ((((xcb_key_press_event_t *)event)->detail) == shiftl || (((xcb_key_press_event_t *)event)->detail) == shiftr))
				cycleGeometryDisplay(wwin, fx, fy, fw, fh, res);
			showGeometry(wwin, fx, fy, fx + fw, fy + fh, res);
			break;

		case XCB_BUTTON_RELEASE:
			if (!wwin->moveresize.active
			    && ((((xcb_button_press_event_t *)event)->detail) != ((xcb_button_press_event_t *)ev)->detail))
				break;

			/* a release carries root_x/root_y like a motion
			 * event, use it as the last pointer sample so a
			 * fling with no motion after the handoff still
			 * resizes */
			__attribute__((fallthrough));
		case XCB_MOTION_NOTIFY:
			if (started
			    || abs(orig_x - (((xcb_motion_notify_event_t *)event)->root_x)) >= MOVE_THRESHOLD
			    || abs(orig_y - (((xcb_motion_notify_event_t *)event)->root_y)) >= MOVE_THRESHOLD) {
				if (!started) {
					int tx, ty;
					xcb_window_t junkw;
					int flags;

					if (wwin->flags.tiled) {
						/* pop the window out of the
						 * layout at the first real
						 * motion so a plain click on
						 * the resizebar never ejects
						 * it */
						wwin->flags.tiled = 0;
						wwin->flags.no_tiling = 1;
						wLayoutApplyForWindow(wwin);
					}

					wXTranslateCoordinates(wdisp->conn, root, wwin->frame->core->window,
							      orig_x, orig_y, &tx, &ty, &junkw);

					/* check if resizing through resizebar */
					if (is_resizebar)
						flags = RESIZEBAR;
					else
						flags = 0;

					if (is_resizebar && ((((xcb_button_press_event_t *)ev)->state & XCB_MOD_MASK_SHIFT)
							     || abs(orig_y - (((xcb_motion_notify_event_t *)event)->root_y)) < HRESIZE_THRESHOLD))
						flags |= HCONSTRAIN;

					/* a _NET_WM_MOVERESIZE resize names its
					 * edge in the message, only derive one
					 * from the position for WM-initiated
					 * resizes */
					if (!wwin->moveresize.active)
						res = getResizeDirection(wwin, tx, ty, orig_y - (((xcb_motion_notify_event_t *)event)->root_y), flags);

					if (res == (UP | LEFT))
						wXChangeActivePointerGrab(wdisp->conn, XCB_EVENT_MASK_BUTTON_MOTION
									 | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS,
									 wdisp->cursor[WCUR_TOPLEFTRESIZE], XCB_CURRENT_TIME);
					else if (res == (UP | RIGHT))
						wXChangeActivePointerGrab(wdisp->conn, XCB_EVENT_MASK_BUTTON_MOTION
								 | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS,
								 wdisp->cursor[WCUR_TOPRIGHTRESIZE], XCB_CURRENT_TIME);
					else if (res == (DOWN | LEFT))
						wXChangeActivePointerGrab(wdisp->conn, XCB_EVENT_MASK_BUTTON_MOTION
								 | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS,
								 wdisp->cursor[WCUR_BOTTOMLEFTRESIZE], XCB_CURRENT_TIME);
					else if (res == (DOWN | RIGHT))
						wXChangeActivePointerGrab(wdisp->conn, XCB_EVENT_MASK_BUTTON_MOTION
								 | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS,
								 wdisp->cursor[WCUR_BOTTOMRIGHTRESIZE], XCB_CURRENT_TIME);
					else if (res == DOWN || res == UP)
						wXChangeActivePointerGrab(wdisp->conn, XCB_EVENT_MASK_BUTTON_MOTION
								 | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS,
								 wdisp->cursor[WCUR_VERTICALRESIZE], XCB_CURRENT_TIME);
					else if (res & (DOWN | UP))
						wXChangeActivePointerGrab(wdisp->conn, XCB_EVENT_MASK_BUTTON_MOTION
								 | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS,
								 wdisp->cursor[WCUR_VERTICALRESIZE], XCB_CURRENT_TIME);
					else if (res & (LEFT | RIGHT))
						wXChangeActivePointerGrab(wdisp->conn, XCB_EVENT_MASK_BUTTON_MOTION
								 | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS,
								 wdisp->cursor[WCUR_HORIZONRESIZE], XCB_CURRENT_TIME);

					wXGrabKeyboard(wdisp->conn, root, false, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_CURRENT_TIME);

					/* Draw the resize frame for the first time. */
					mapGeometryDisplay(wwin, fx, fy, fw, fh);

					started = 1;

					if (w_global.config->shared_edge_resize) {
						if (res & (LEFT | RIGHT))
							shared_edge_pair_setup(&pair_x, wwin, 0,
									       (res & RIGHT) ? 1 : -1,
									       rx1, rx2, vert_border);
						if (res & (UP | DOWN))
							shared_edge_pair_setup(&pair_y, wwin, 1,
									       (res & DOWN) ? 1 : -1,
									       ry1, ry2, vert_border);
					}
				}

				dw = 0;
				dh = 0;

				orig_fw = fw;
				orig_fh = fh;

				if (res & LEFT)
					dw = orig_x - (((xcb_motion_notify_event_t *)event)->root_x);
				else if (res & RIGHT)
					dw = (((xcb_motion_notify_event_t *)event)->root_x) - orig_x;
				if (res & UP)
					dh = orig_y - (((xcb_motion_notify_event_t *)event)->root_y);
				else if (res & DOWN)
					dh = (((xcb_motion_notify_event_t *)event)->root_y) - orig_y;

				orig_x = (((xcb_motion_notify_event_t *)event)->root_x);
				orig_y = (((xcb_motion_notify_event_t *)event)->root_y);

				rw += dw;
				rh += dh;
				fw = rw;
				fh = rh;
				snapResizeGeometry(wwin, res, rx1, ry1, rx2, ry2, dw, dh, &fw, &fh);
				fh -= vert_border;
				/* the loop works in frame-outer units, the
				 * constrain works on the client extent */
				cfw = fw - horz_border;
				wWindowConstrainSize(wwin, (unsigned int *)&cfw, (unsigned int *)&fh);
				fw = cfw + horz_border;
				fh += vert_border;
				if (res & LEFT)
					fx = rx2 - fw + 1;
				else if (res & RIGHT)
					fx = rx1;
				if (res & UP)
					fy = ry2 - fh + 1;
				else if (res & DOWN)
					fy = ry1;

				if (pair_x.partner || pair_y.partner)
					shared_edge_adjust(&pair_x, &pair_y, wwin,
							   vert_border, &fx, &fy, &fw, &fh);

				if (w_global.config->size_display == WDIS_FRAME_CENTER)
					moveGeometryDisplayCentered(scr, fx + fw / 2, fy + fh / 2);

				if (!opaqueResize)
					showResizeWireframe(wwin, fx, fy, fw, fh);

				if (fh != orig_fh || fw != orig_fw)
					showGeometry(wwin, fx, fy, fx + fw, fy + fh, res);

				if (opaqueResize) {
					/* Fist clean the geometry line */
					showGeometry(wwin, fx, fy, fx + fw, fy + fh, res);
					/* Now, continue drawing */
					moveGeometryDisplayCentered(scr, fx + fw / 2, fy + fh / 2);
					/* pace to the client's redraw without
					 * blocking, if the previous
					 * _NET_WM_SYNC_REQUEST is still
					 * outstanding skip this configure and
					 * apply the latest geometry once the
					 * alarm arrives, the geometry view and
					 * fw/fh have already advanced so the
					 * next pass or ButtonRelease catches
					 * the client up */
					if (!wNETWMSyncRequestPending(wwin)) {
						wNETWMSyncRequestSend(wwin);
						wWindowConfigure(wwin, fx, fy, fw - horz_border, fh - vert_border);
					}

					shared_edge_apply_motion(&pair_x);
					shared_edge_apply_motion(&pair_y);
					showGeometry(wwin, fx, fy, fx + fw, fy + fh, res);
				}
			}

			if ((event->response_type & 0x7f) != XCB_BUTTON_RELEASE)
				break;

			if (started) {
				showGeometry(wwin, fx, fy, fx + fw, fy + fh, res);

				wWireframeHide(scr);

				wXUngrabKeyboard(wdisp->conn, XCB_CURRENT_TIME);
				WMUnmapWidget(scr->gview);

				if (fw != original_fw)
					wwin->flags.maximized &= ~(MAX_HORIZONTAL | MAX_TOPHALF | MAX_BOTTOMHALF | MAX_MAXIMUS | MAX_CENTRAL);

				if (fh != original_fh)
					wwin->flags.maximized &= ~(MAX_VERTICAL | MAX_LEFTHALF | MAX_RIGHTHALF | MAX_MAXIMUS | MAX_CENTRAL);

				wWindowConfigure(wwin, fx, fy, fw - horz_border, fh - vert_border);
				wWindowSynthConfigureNotify(wwin);

				if (pair_x.partner) {
					wWindowConfigure(pair_x.partner, pair_x.px, pair_x.py,
							 pair_x.pw, pair_x.ph);
					wWindowSynthConfigureNotify(pair_x.partner);
				}
				if (pair_y.partner) {
					wWindowConfigure(pair_y.partner, pair_y.px, pair_y.py,
							 pair_y.pw, pair_y.ph);
					wWindowSynthConfigureNotify(pair_y.partner);
				}

				/* an unchanged drag did not break the maximized layout */
				if ((pair_x.partner || pair_y.partner)
				    && (fw != original_fw || fh != original_fh)) {
					shared_edge_forget_maximized(wwin);
					shared_edge_forget_maximized(pair_x.partner);
					shared_edge_forget_maximized(pair_y.partner);
				}
			}
			if (wwin->moveresize.active) {
				wXUngrabPointer(wdisp->conn, XCB_CURRENT_TIME);
				wwin->moveresize.active = 0;
			}
			wNETWMBumpUserTime(scr, (((xcb_button_press_event_t *)event)->time));

			free(event);
			return;

		case XCB_BUTTON_PRESS:
			break;

		default:
			WMHandleEvent(event);
		}
	}

	free(event);

	if (w_global.config->auto_arrange_icons && wXineramaHeads(scr) > 1 && head != wGetHeadForWindow(wwin))
		wArrangeIcons(scr, true);
}

#undef LEFT
#undef RIGHT
#undef UP
#undef DOWN
#undef HCONSTRAIN
#undef RESIZEBAR

void wUnselectWindows(WScreen * scr)
{
	WWindow *wwin;

	if (!scr->selected_windows)
		return;

	while (WMGetArrayItemCount(scr->selected_windows)) {
		wwin = WMGetFromArray(scr->selected_windows, 0);
		if (wwin->flags.miniaturized && wwin->icon && wwin->icon->selected)
			wIconSelect(wwin->icon);

		wSelectWindow(wwin, false);
	}
	WMFreeArray(scr->selected_windows);
	scr->selected_windows = NULL;
}

static void selectWindowsInside(WScreen * scr, int x1, int y1, int x2, int y2)
{
	WWindow *tmpw;

	/* select the windows and put them in the selected window list */
	tmpw = scr->focused_window;
	while (tmpw != NULL) {
		if (!(tmpw->flags.miniaturized || tmpw->flags.hidden)) {
			if (wWindowIsVisibleOnAnyHead(tmpw)
			    && (tmpw->frame_x >= x1) && (tmpw->frame_y >= y1)
			    && (tmpw->frame->core->width + tmpw->frame_x <= x2)
			    && (tmpw->frame->core->height + tmpw->frame_y <= y2)) {
				wSelectWindow(tmpw, true);
			}
		}
		tmpw = tmpw->prev;
	}
}

void wSelectWindows(WScreen * scr, xcb_generic_event_t * ev)
{
	xcb_generic_event_t *event = NULL;
	int xp = ((xcb_button_press_event_t *)ev)->root_x;
	int yp = ((xcb_button_press_event_t *)ev)->root_y;
	int w = 0, h = 0;
	int x = xp, y = yp;

	if (wXGrabPointer(wdisp->conn, scr->root_win, false, XCB_EVENT_MASK_BUTTON_MOTION
			 | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS, XCB_GRAB_MODE_ASYNC,
			 XCB_GRAB_MODE_ASYNC, XCB_NONE, wdisp->cursor[WCUR_NORMAL], XCB_CURRENT_TIME) != WXGrabSuccess) {
		return;
	}

	wUnselectWindows(scr);

	while (1) {
		free(event);
		event = NULL;

		WMMaskEvent(wdisp->conn, XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_PRESS
			    | XCB_EVENT_MASK_EXPOSURE, &event);

		switch ((event->response_type & 0x7f)) {
		case XCB_MOTION_NOTIFY:
			x = (((xcb_motion_notify_event_t *)event)->root_x);
			if (x < xp) {
				w = xp - x;
			} else {
				w = x - xp;
				x = xp;
			}
			y = (((xcb_motion_notify_event_t *)event)->root_y);
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

		case XCB_BUTTON_PRESS:
			break;

		case XCB_BUTTON_RELEASE:
			if ((((xcb_button_press_event_t *)event)->detail) != ((xcb_button_press_event_t *)ev)->detail)
				break;

			wWireframeHide(scr);
			wXUngrabPointer(wdisp->conn, XCB_CURRENT_TIME);
			selectWindowsInside(scr, x, y, x + w, y + h);
			free(event);
			return;

		default:
			/* the wireframe band is a real window now, so the
			 * server sends Expose to whatever it just uncovered,
			 * dispatch them so titlebars and dock tiles repaint
			 * during the drag instead of after release */
			WMHandleEvent(event);
			break;
		}
	}
}

void InteractivePlaceWindow(WWindow * wwin, int *x_ret, int *y_ret, unsigned width, unsigned height)
{
	WScreen *scr = wwin->screen_ptr;
	xcb_window_t root = scr->root_win;
	int x, y, h = 0;
	xcb_generic_event_t *event = NULL;
	xcb_keycode_t shiftl, shiftr;
	xcb_window_t junkw;
	int junk;

	if (wXGrabPointer(wdisp->conn, root, true, XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_PRESS,
			 XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, wdisp->cursor[WCUR_NORMAL], XCB_CURRENT_TIME) != WXGrabSuccess) {
		*x_ret = 0;
		*y_ret = 0;
		return;
	}
	if (HAS_TITLEBAR(wwin)) {
		h = WSCALE(scr, WMFontHeight(scr->title_font) + (w_global.config->window_title_clearance +
						     TITLEBAR_EXTEND_SPACE) * 2);

		if (h > w_global.config->window_title_max_height)
			h = w_global.config->window_title_max_height;

		if (h < w_global.config->window_title_min_height)
			h = w_global.config->window_title_min_height;

		height += h;
	}
	if (HAS_RESIZEBAR(wwin)) {
		height += wwin->screen_ptr->resizebar_height;
	}
	wXGrabKeyboard(wdisp->conn, root, false, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_CURRENT_TIME);
	wXQueryPointer(wdisp->conn, root, &junkw, &junkw, &x, &y, &junk, &junk, (unsigned int *)&junk);
	mapPositionDisplay(wwin, x - width / 2, y - h / 2, width, height);

	showResizeWireframe(wwin, x - width / 2, y - h / 2, width, height);

	shiftl = wXkbKeysymToKeycode(XKB_KEY_Shift_L);
	shiftr = wXkbKeysymToKeycode(XKB_KEY_Shift_R);
	while (1) {
		free(event);
		event = NULL;

		WMMaskEvent(wdisp->conn, XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS, &event);

		if (!checkMouseSamplingRate(&event))
			continue;

		switch ((event->response_type & 0x7f)) {
		case XCB_KEY_PRESS:
			if (((((xcb_key_press_event_t *)event)->detail) == shiftl)
			    || ((((xcb_key_press_event_t *)event)->detail) == shiftr)) {
				cyclePositionDisplay(wwin, x - width / 2, y - h / 2, width, height);
			}
			break;

		case XCB_MOTION_NOTIFY:
			x = (((xcb_motion_notify_event_t *)event)->root_x);
			y = (((xcb_motion_notify_event_t *)event)->root_y);

			if (w_global.config->move_display == WDIS_FRAME_CENTER)
				moveGeometryDisplayCentered(scr, x, y + (height - h) / 2);

			showPosition(wwin, x - width / 2, y - h / 2);

			showResizeWireframe(wwin, x - width / 2, y - h / 2, width, height);

			break;

		case XCB_BUTTON_PRESS:
			wWireframeHide(scr);
			*x_ret = x - width / 2;
			*y_ret = y - h / 2;
			wXUngrabPointer(wdisp->conn, XCB_CURRENT_TIME);
			wXUngrabKeyboard(wdisp->conn, XCB_CURRENT_TIME);
			/* get rid of the geometry window */
			WMUnmapWidget(scr->gview);
			free(event);
			return;

		default:
			WMHandleEvent(event);
			break;
		}
	}
}
