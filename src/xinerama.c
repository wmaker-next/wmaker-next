/*
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wconfig.h"

#include "xinerama.h"

#include "screen.h"
#include "window.h"
#include "framewin.h"
#include "placement.h"
#include "dock.h"

#ifdef USE_XINERAMA
# ifdef SOLARIS_XINERAMA	/* sucks */
#  include <X11/extensions/xinerama.h>
# else
#  include <X11/extensions/Xinerama.h>
# endif
#endif

void wInitXinerama(WScreen * scr)
{
	scr->xine_info.primary_head = 0;
	scr->xine_info.screens = NULL;
	scr->xine_info.count = 0;
#ifdef USE_XINERAMA
# ifdef SOLARIS_XINERAMA
	if (XineramaGetState(dpy, scr->screen)) {
		WXineramaInfo *info = &scr->xine_info;
		XRectangle head[MAXFRAMEBUFFERS];
		unsigned char hints[MAXFRAMEBUFFERS];
		int i;

		if (XineramaGetInfo(dpy, scr->screen, head, hints, &info->count)) {

			info->screens = wmalloc(sizeof(WMRect) * (info->count + 1));

			for (i = 0; i < info->count; i++) {
				info->screens[i].pos.x = head[i].x;
				info->screens[i].pos.y = head[i].y;
				info->screens[i].size.width = head[i].width;
				info->screens[i].size.height = head[i].height;
			}
		}
	}
# else				/* !SOLARIS_XINERAMA */
	if (XineramaIsActive(dpy)) {
		XineramaScreenInfo *xine_screens;
		WXineramaInfo *info = &scr->xine_info;
		int i;

		xine_screens = XineramaQueryScreens(dpy, &info->count);

		info->screens = wmalloc(sizeof(WMRect) * (info->count + 1));

		for (i = 0; i < info->count; i++) {
			info->screens[i].pos.x = xine_screens[i].x_org;
			info->screens[i].pos.y = xine_screens[i].y_org;
			info->screens[i].size.width = xine_screens[i].width;
			info->screens[i].size.height = xine_screens[i].height;
		}
		XFree(xine_screens);
	}
# endif				/* !SOLARIS_XINERAMA */
#endif				/* USE_XINERAMA */
}

int wGetRectPlacementInfo(WScreen * scr, WMRect rect, int *flags)
{
	int best;
	unsigned long area, totalArea;
	int i;
	int rx = rect.pos.x;
	int ry = rect.pos.y;
	int rw = rect.size.width;
	int rh = rect.size.height;

	wassertrv(flags != NULL, 0);

	best = -1;
	area = 0;
	totalArea = 0;

	*flags = XFLAG_NONE;

	if (scr->xine_info.count <= 1) {
		unsigned long a;

		a = calcIntersectionArea(rx, ry, rw, rh, 0, 0, scr->scr_width, scr->scr_height);

		if (a == 0) {
			*flags |= XFLAG_DEAD;
		} else if (a != rw * rh) {
			*flags |= XFLAG_PARTIAL;
		}

		return scr->xine_info.primary_head;
	}

	for (i = 0; i < wXineramaHeads(scr); i++) {
		unsigned long a;

		a = calcIntersectionArea(rx, ry, rw, rh,
					 scr->xine_info.screens[i].pos.x,
					 scr->xine_info.screens[i].pos.y,
					 scr->xine_info.screens[i].size.width,
					 scr->xine_info.screens[i].size.height);

		totalArea += a;
		if (a > area) {
			if (best != -1)
				*flags |= XFLAG_MULTIPLE;
			area = a;
			best = i;
		}
	}

	if (best == -1) {
		*flags |= XFLAG_DEAD;
		best = wGetHeadForPointerLocation(scr);
	} else if (totalArea != rw * rh)
		*flags |= XFLAG_PARTIAL;

	return best;
}

/* get the head that covers most of the rectangle */
int wGetHeadForRect(WScreen * scr, WMRect rect)
{
	int best;
	unsigned long area;
	int i;
	int rx = rect.pos.x;
	int ry = rect.pos.y;
	int rw = rect.size.width;
	int rh = rect.size.height;

	if (!scr->xine_info.count)
		return scr->xine_info.primary_head;

	best = -1;
	area = 0;

	for (i = 0; i < wXineramaHeads(scr); i++) {
		unsigned long a;

		a = calcIntersectionArea(rx, ry, rw, rh,
					 scr->xine_info.screens[i].pos.x,
					 scr->xine_info.screens[i].pos.y,
					 scr->xine_info.screens[i].size.width,
					 scr->xine_info.screens[i].size.height);

		if (a > area) {
			area = a;
			best = i;
		}
	}

	/*
	 * in case rect is in dead space, return valid head
	 */
	if (best == -1)
		best = wGetHeadForPointerLocation(scr);

	return best;
}

Bool wWindowTouchesHead(WWindow * wwin, int head)
{
	WScreen *scr;
	WMRect rect;
	int a;

	if (!wwin || !wwin->frame)
		return False;

	scr = wwin->screen_ptr;
	rect = wGetRectForHead(scr, head);
	a = calcIntersectionArea(wwin->frame_x, wwin->frame_y,
				 wwin->frame->core->width,
				 wwin->frame->core->height,
				 rect.pos.x, rect.pos.y, rect.size.width, rect.size.height);

	return (a != 0);
}

Bool wAppIconTouchesHead(WAppIcon * aicon, int head)
{
	WScreen *scr;
	WMRect rect;
	int a;

	if (!aicon || !aicon->icon)
		return False;

	scr = aicon->icon->core->screen_ptr;
	rect = wGetRectForHead(scr, head);
	a = calcIntersectionArea(aicon->x_pos, aicon->y_pos,
				 aicon->icon->core->width,
				 aicon->icon->core->height,
				 rect.pos.x, rect.pos.y, rect.size.width, rect.size.height);

	return (a != 0);
}

int wGetHeadForWindow(WWindow * wwin)
{
	WMRect rect;

	if (wwin == NULL || wwin->frame == NULL)
		return 0;

	rect.pos.x = wwin->frame_x;
	rect.pos.y = wwin->frame_y;
	rect.size.width = wwin->frame->core->width;
	rect.size.height = wwin->frame->core->height;

	return wGetHeadForRect(wwin->screen_ptr, rect);
}

/* Effective head for per-head workspace visibility. For mapped windows
 * this is wGetHeadForWindow(), but for unmapped/hidden/off-screen
 * windows wGetHeadForRect() falls back to the pointer head, which would
 * make a hidden window flip heads as the mouse moves. Use the cached
 * last_head instead, clamped to the current head count. */
int wWindowEffectiveHead(WWindow *wwin)
{
	int heads;

	if (wwin == NULL)
		return 0;

	heads = wHeadCount(wwin->screen_ptr);
	if (wwin->frame != NULL && (wwin->flags.mapped || wwin->flags.shaded)) {
		int h = wGetHeadForWindow(wwin);
		if (h >= 0 && h < heads) {
			wwin->last_head = h;
			return h;
		}
	}
	if (wwin->last_head >= 0 && wwin->last_head < heads)
		return wwin->last_head;
	return wwin->screen_ptr->xine_info.primary_head;
}

/* Head currently considered "active" for workspace keybindings, the
 * compatibility current_workspace mirror, and _NET_CURRENT_DESKTOP.
 * Prefer the focused window's head, then the last head we recorded from
 * focus or pointer activity, then the live pointer, then primary. */
int wGetActiveHead(WScreen *scr)
{
	int heads = wHeadCount(scr);
	int h;

	if (scr->focused_window && scr->focused_window->flags.focused
	    && (scr->focused_window->flags.mapped || scr->focused_window->flags.shaded)) {
		h = wWindowEffectiveHead(scr->focused_window);
		if (h >= 0 && h < heads)
			return h;
	}
	if (scr->last_active_head >= 0 && scr->last_active_head < heads)
		return scr->last_active_head;
	h = wGetHeadForPointerLocation(scr);
	if (h >= 0 && h < heads)
		return h;
	return scr->xine_info.primary_head;
}

int wHeadForOutputName(WScreen *scr, const char *name)
{
	int i;

	if (!name)
		return -1;
	for (i = 0; i < wHeadCount(scr); i++) {
		if (scr->head_output_name[i]
		    && strcmp(scr->head_output_name[i], name) == 0)
			return i;
	}
	/* "#N" addresses an active head by index only when that head has no
	 * RandR output name, mirroring the HeadWorkspaces save/restore key.
	 * This keeps WorkspaceOutputs usable under bare Xinerama (and Xephyr
	 * +xinerama, which exposes 0 active RandR outputs) without letting an
	 * index pin shadow a real connector name on hardware. */
	if (name[0] == '#' && sscanf(name + 1, "%d", &i) == 1
	    && i >= 0 && i < wHeadCount(scr) && !scr->head_output_name[i])
		return i;
	return -1;
}

const char *wOutputNameForHead(WScreen *scr, int head)
{
	if (head < 0 || head >= WM_MAX_HEADS)
		return NULL;
	return scr->head_output_name[head];
}

void wClearHeadOutputNames(WScreen *scr)
{
	int i;

	for (i = 0; i < WM_MAX_HEADS; i++) {
		if (scr->head_output_name[i]) {
			wfree(scr->head_output_name[i]);
			scr->head_output_name[i] = NULL;
		}
	}
}

/* Find head on left, right, up or down direction relative to current
head. If there is no screen available on pointed direction, -1 will be
returned.*/
int wGetHeadRelativeToCurrentHead(WScreen *scr, int current_head, int direction)
{
	short int found = 0;
	int i;
	int distance = 0;
	int smallest_distance = 0;
	int nearest_head = scr->xine_info.primary_head;
	WMRect crect = wGetRectForHead(scr, current_head);

	for (i = 0; i < scr->xine_info.count; i++) {
		if (i == current_head)
			continue;

		WMRect *rect = &scr->xine_info.screens[i];

		/* calculate distance from the next screen to current one */
		switch (direction) {
			case DIRECTION_LEFT:
				if (rect->pos.x < crect.pos.x) {
					found = 1;
					distance = abs((rect->pos.x + (int)rect->size.width)
							- crect.pos.x) + abs(rect->pos.y + crect.pos.y);
				}
				break;
			case DIRECTION_RIGHT:
				if (rect->pos.x > crect.pos.x) {
					found = 1;
					distance = abs((crect.pos.x + (int)crect.size.width)
							- rect->pos.x) + abs(rect->pos.y + crect.pos.y);
				}
				break;
			case DIRECTION_UP:
				if (rect->pos.y < crect.pos.y) {
					found = 1;
					distance = abs((rect->pos.y + (int)rect->size.height)
							- crect.pos.y) + abs(rect->pos.x + crect.pos.x);
				}
				break;
			case DIRECTION_DOWN:
				if (rect->pos.y > crect.pos.y) {
					found = 1;
					distance = abs((crect.pos.y + (int)crect.size.height)
							- rect->pos.y) + abs(rect->pos.x + crect.pos.x);
				}
				break;
		}

		if (found && distance == 0)
			return i;

		if (smallest_distance == 0)
			smallest_distance = distance;

		if (abs(distance) <= smallest_distance) {
			smallest_distance = distance;
			nearest_head = i;
		}
	}

	if (found && smallest_distance != 0 && nearest_head != current_head)
		return nearest_head;

	return -1;
}

int wGetHeadForPoint(WScreen * scr, WMPoint point)
{
	int i;

	for (i = 0; i < scr->xine_info.count; i++) {
		WMRect *rect = &scr->xine_info.screens[i];

		if ((unsigned)(point.x - rect->pos.x) < rect->size.width &&
		    (unsigned)(point.y - rect->pos.y) < rect->size.height)
			return i;
	}
	return scr->xine_info.primary_head;
}

int wGetHeadForPointerLocation(WScreen * scr)
{
	WMPoint point;
	Window bla;
	int ble;
	unsigned int blo;

	if (!scr->xine_info.count)
		return scr->xine_info.primary_head;

	if (!XQueryPointer(dpy, scr->root_win, &bla, &bla, &point.x, &point.y, &ble, &ble, &blo))
		return scr->xine_info.primary_head;

	return wGetHeadForPoint(scr, point);
}

/* get the dimensions of the head */
WMRect wGetRectForHead(WScreen * scr, int head)
{
	WMRect rect;

	if (head < scr->xine_info.count) {
		rect.pos.x = scr->xine_info.screens[head].pos.x;
		rect.pos.y = scr->xine_info.screens[head].pos.y;
		rect.size.width = scr->xine_info.screens[head].size.width;
		rect.size.height = scr->xine_info.screens[head].size.height;
	} else {
		rect.pos.x = 0;
		rect.pos.y = 0;
		rect.size.width = scr->scr_width;
		rect.size.height = scr->scr_height;
	}

	return rect;
}

WArea wGetUsableAreaForHead(WScreen * scr, int head, WArea * totalAreaPtr, Bool noicons)
{
	WArea totalArea, usableArea;
	WMRect rect = wGetRectForHead(scr, head);

	totalArea.x1 = rect.pos.x;
	totalArea.y1 = rect.pos.y;
	totalArea.x2 = totalArea.x1 + rect.size.width;
	totalArea.y2 = totalArea.y1 + rect.size.height;

	if (totalAreaPtr != NULL)
		*totalAreaPtr = totalArea;

	if (head < wXineramaHeads(scr)) {
		usableArea = noicons ? scr->totalUsableArea[head] : scr->usableArea[head];
	} else
		usableArea = totalArea;

	if (noicons) {
		/* check if user wants dock covered */
		if (scr->dock && w_global.config->no_window_over_dock && wAppIconTouchesHead(scr->dock->icon_array[0], head)) {
			int offset = w_global.config->icon_size + DOCK_EXTRA_SPACE;

			if (scr->dock->on_right_side)
				usableArea.x2 -= offset;
			else
				usableArea.x1 += offset;
		}

		/* check if icons are on the same side as dock, and adjust if not done already */
		if (scr->dock && w_global.config->no_window_over_icons && !w_global.config->no_window_over_dock && (w_global.config->icon_yard & IY_VERT)) {
			int offset = w_global.config->icon_size + DOCK_EXTRA_SPACE;

			if (scr->dock->on_right_side && (w_global.config->icon_yard & IY_RIGHT))
				usableArea.x2 -= offset;
			/* can't use IY_LEFT in if, it's 0 ... */
			if (!scr->dock->on_right_side && !(w_global.config->icon_yard & IY_RIGHT))
				usableArea.x1 += offset;
		}
	}

	return usableArea;
}

WMPoint wGetPointToCenterRectInHead(WScreen * scr, int head, int width, int height)
{
	WMPoint p;
	WMRect rect = wGetRectForHead(scr, head);

	p.x = rect.pos.x + (rect.size.width - width) / 2;
	p.y = rect.pos.y + (rect.size.height - height) / 2;

	return p;
}

/* Find the bounding rect of the union of two rectangles */
void wGetRectUnion(const WMRect *rect1, const WMRect *rect2, WMRect *dest)
{
	int dest_x, dest_y;
	int dest_w, dest_h;

	dest_x = rect1->pos.x;
	dest_y = rect1->pos.y;
	dest_w = rect1->size.width;
	dest_h = rect1->size.height;

	if (rect2->pos.x < dest_x) {
		dest_w += dest_x - rect2->pos.x;
		dest_x = rect2->pos.x;
	}
	if (rect2->pos.y < dest_y) {
		dest_h += dest_y - rect2->pos.y;
		dest_y = rect2->pos.y;
	}
	if (rect2->pos.x + rect2->size.width > dest_x + dest_w)
		dest_w = rect2->pos.x + rect2->size.width - dest_x;
	if (rect2->pos.y + rect2->size.height > dest_y + dest_h)
		dest_h = rect2->pos.y + rect2->size.height - dest_y;

	dest->pos.x = dest_x;
	dest->pos.y = dest_y;
	dest->size.width = dest_w;
	dest->size.height = dest_h;
}
