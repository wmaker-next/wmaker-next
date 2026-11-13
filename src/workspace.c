/* workspace.c- Workspace management
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


#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "WindowMaker.h"
#include "framewin.h"
#include "window.h"
#include "icon.h"
#include "misc.h"
#include "menu.h"
#include "application.h"
#include "dock.h"
#include "actions.h"
#include "workspace.h"
#include "appicon.h"
#include "wmspec.h"
#include "xinerama.h"
#include "event.h"
#include "wsmap.h"
#include "wsplan.h"
#include "dialog.h"
#include "tabgroup.h"
#include "osdview.h"
#include "layout.h"
#include "wsound.h"

_Static_assert(MAX_WORKSPACES <= 64,
               "WFrameWindow.ws_mask is 64 bits wide");

#define MC_DESTROY_LAST 1
#define MC_LAST_USED    2
#ifdef VIRTUAL_DESKTOP
/* keep restored viewport scalars well inside int so later world
 * coordinate sums cannot overflow */
#define RESTORE_COORD_LIMIT (INT_MAX / 16)
#endif
#define RESTORE_MASTER_COUNT_MAX 32
/* index of the first workspace menu entry, recorded per menu by
 * wWorkspaceMenuMake: the Layout cascade shifts it on the titled menu */
#define MC_WORKSPACE1(menu)	((menu)->ws_entry_base)

#define WORKSPACE_NAME_DISPLAY_PADDING 32

static WMPropList *dWorkspaces = NULL;
static WMPropList *dClip, *dName, *dHeadWorkspaces;
static WMPropList *dLayout, *dMasterCount, *dMasterRatio;
#ifdef VIRTUAL_DESKTOP
static WMPropList *dVdeskX, *dVdeskY, *dVdeskW, *dVdeskH, *dViewX, *dViewY;
#endif

static void make_keys(void)
{
	if (dWorkspaces != NULL)
		return;

	dWorkspaces = WMCreatePLString("Workspaces");
	dName = WMCreatePLString("Name");
	dClip = WMCreatePLString("Clip");
	dHeadWorkspaces = WMCreatePLString("HeadWorkspaces");
	dLayout = WMCreatePLString("Layout");
	dMasterCount = WMCreatePLString("MasterCount");
	dMasterRatio = WMCreatePLString("MasterRatio");
#ifdef VIRTUAL_DESKTOP
	/* Per-workspace viewport state: view_x/y is the camera origin,
	 * vdesk_w/h/x/y the finite-desktop extent and origin. Pair with
	 * wWindowSaveState's frame_x/y (saved screen positions) to
	 * reconstruct the world coord = frame + view. */
	dVdeskX = WMCreatePLString("VdeskX");
	dVdeskY = WMCreatePLString("VdeskY");
	dVdeskW = WMCreatePLString("VdeskW");
	dVdeskH = WMCreatePLString("VdeskH");
	dViewX = WMCreatePLString("ViewX");
	dViewY = WMCreatePLString("ViewY");
#endif
}

/* ---------- per-head workspace helpers ---------- */

int wWorkspaceForHead(WScreen *scr, int head)
{
	if (!w_global.config->per_head_workspaces || head < 0 || head >= WM_MAX_HEADS)
		return scr->current_workspace;
	return scr->head_workspace[head];
}

/* Head currently showing the given workspace, preferring the active
 * head, then any head where it is visible, then any head where it is
 * merely allowed by WorkspaceOutputs. Used to seed wwin->last_head
 * before automatic placement so a window targeting a workspace shown on
 * another monitor lands and maps there instead of under the pointer. */
int wHeadShowingWorkspace(WScreen *scr, int workspace)
{
	int active, h;

	if (!w_global.config->per_head_workspaces)
		return wGetActiveHead(scr);

	active = wGetActiveHead(scr);
	if (scr->head_workspace[active] == workspace)
		return active;
	for (h = 0; h < wHeadCount(scr); h++) {
		if (scr->head_workspace[h] == workspace)
			return h;
	}
	for (h = 0; h < wHeadCount(scr); h++) {
		if (wWorkspaceIsAllowedOnHead(scr, workspace, h))
			return h;
	}
	return active;
}

/* A workspace is allowed on a head when it is unpinned, or when one of
 * its WorkspaceOutputs names resolves to that head through
 * wHeadForOutputName() (RandR connector name, or "#N" head index when
 * the head has no RandR name). A workspace pinned only to outputs that
 * are currently absent is allowed on no head. */
bool wWorkspaceIsAllowedOnHead(WScreen *scr, int workspace, int head)
{
	WWorkspace *ws;
	int i;

	if (workspace < 0 || workspace >= scr->workspace_count)
		return false;
	ws = scr->workspaces[workspace];
	if (ws->head_name_count == 0)
		return true;

	for (i = 0; i < ws->head_name_count; i++) {
		if (wHeadForOutputName(scr, ws->head_names[i]) == head)
			return true;
	}
	return false;
}

bool wWorkspaceIsVisibleOnHead(WScreen *scr, int workspace, int head)
{
	if (!w_global.config->per_head_workspaces)
		return workspace == scr->current_workspace;
	if (head < 0 || head >= wHeadCount(scr))
		return false;
	return scr->head_workspace[head] == workspace;
}

/* The single visibility predicate in per-head mode: a window is visible
 * when omnipresent, or when its workspace is the active workspace on
 * the window's effective head. When the same workspace is active on
 * several heads the window still maps only on its own head. */
bool wWindowIsVisibleOnAnyHead(WWindow *wwin)
{
	WScreen *scr;
	int head;

	if (!wwin || !wwin->frame)
		return false;
	if (IS_OMNIPRESENT(wwin))
		return true;

	scr = wwin->screen_ptr;
	if (!w_global.config->per_head_workspaces)
		return WWIN_ON_WORKSPACE(wwin, scr->current_workspace);

	head = wWindowEffectiveHead(wwin);
	if (head >= 0 && head < wHeadCount(scr)
	    && WWIN_ON_WORKSPACE(wwin, scr->head_workspace[head]))
		return true;
	return false;
}

/* Update last_active_head, mirror current_workspace, and republish
 * _NET_CURRENT_DESKTOP. Called on focus changes between heads and after
 * a per-head workspace switch so legacy code that still reads
 * scr->current_workspace keeps seeing the focused head's workspace. */
void wWorkspaceSetCurrentHead(WScreen *scr, int head)
{
	int ws, old;

	if (!w_global.config->per_head_workspaces)
		return;
	if (head < 0 || head >= wHeadCount(scr))
		return;

	scr->last_active_head = head;
	ws = scr->head_workspace[head];
	if (ws < 0 || ws >= scr->workspace_count)
		return;

	old = scr->current_workspace;
	if (old == ws) {
		wNETWMUpdateCurrentDesktop(scr);
		return;
	}

	/* Mirror-only update: repoint the visible clip to the focused
	 * head's workspace without shuttling omnipresent icons. */
	wClipRepointForActiveHead(scr, old, ws);
	scr->current_workspace = ws;

	wWorkspaceMenuUpdate(scr, scr->workspace_menu);
	wWorkspaceMenuUpdate(scr, scr->clip_ws_menu);
	if (scr->clip_icon)
		wClipIconPaint(scr->clip_icon);
	wNETWMUpdateCurrentDesktop(scr);

	WMPostNotificationName(WMNWorkspaceChanged, scr, (void *)(uintptr_t) ws);
}

void wWorkspaceMake(WScreen * scr, int count)
{
	while (count > 0) {
		wWorkspaceNew(scr);
		count--;
	}
}

int wWorkspaceNew(WScreen *scr)
{
	WWorkspace *wspace, **list;
	int i;

	if (scr->workspace_count < MAX_WORKSPACES) {
		scr->workspace_count++;

		wspace = wmalloc(sizeof(WWorkspace));
		wspace->name = NULL;
		wspace->clip = NULL;
		wspace->head_names = NULL;
		wspace->head_name_count = 0;
		wspace->layout = w_global.config->default_workspace_layout;
		wspace->master_count = w_global.config->layout_master_count;
		wspace->master_ratio = w_global.config->layout_master_ratio / 100.0f;
#ifdef VIRTUAL_DESKTOP
		/* wmalloc zero-initialized this already. Seed the viewport
		 * fields here so a freshly created workspace has a sane camera
		 * origin and a screen-sized world before any window extends it. */
		wspace->vdesk_w = scr->scr_width;
		wspace->vdesk_h = scr->scr_height;
		wspace->vdesk_x = 0;
		wspace->vdesk_y = 0;
		wspace->view_x = 0;
		wspace->view_y = 0;
#endif

		if (!wspace->name) {
			static const char *new_name = NULL;
			static size_t name_length;

			if (new_name == NULL) {
				new_name = _("Workspace %i");
				name_length = strlen(new_name) + 8;
			}
			wspace->name = wmalloc(name_length);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
			/* the translated name is a printf-format string */
			snprintf(wspace->name, name_length, new_name, scr->workspace_count);
#pragma GCC diagnostic pop
		}

		if (!wdisp->flags.noclip)
			wspace->clip = wDockCreate(scr, WM_CLIP, NULL);

		list = wmalloc(sizeof(WWorkspace *) * scr->workspace_count);

		for (i = 0; i < scr->workspace_count - 1; i++)
			list[i] = scr->workspaces[i];

		list[i] = wspace;
		if (scr->workspaces)
			wfree(scr->workspaces);

		scr->workspaces = list;

		wWorkspaceMenuUpdate(scr, scr->workspace_menu);
		wWorkspaceMenuUpdate(scr, scr->clip_ws_menu);
		wNETWMUpdateDesktop(scr);
		WMPostNotificationName(WMNWorkspaceCreated, scr, (void *)(uintptr_t) (scr->workspace_count - 1));
		wXFlush(wdisp->conn);

		return scr->workspace_count - 1;
	}

	return -1;
}

bool wWorkspaceDelete(WScreen * scr, int workspace)
{
	WWindow *tmp;
	WWorkspace **list;
	uint64_t old_mask, new_mask;
	int old_primary, new_primary;
	int i, j;
	bool deleted_is_current, deleted_is_last;
	bool deleted_on_head[WM_MAX_HEADS], deleted_last_on_head[WM_MAX_HEADS];

	if (workspace <= 0)
		return false;

	/* verify if workspace is in use by some window, a multi-member
	 * window that also lives elsewhere is not a blocker */
	tmp = scr->focused_window;
	while (tmp) {
		if (!IS_OMNIPRESENT(tmp) && WWIN_WS_MASK(tmp) == WS_BIT(workspace)) {
			char buf[256];
			snprintf(buf, sizeof(buf), _("Workspace \"%s\" in use; cannot delete"),
				 scr->workspaces[workspace]->name);
			wMessageDialog(scr, _("Error"), buf, _("OK"), NULL, NULL);
			return false;
		}
		tmp = tmp->prev;
	}

	deleted_is_current = (scr->current_workspace == workspace);
	deleted_is_last = (scr->last_workspace == workspace);
	for (i = 0; i < wHeadCount(scr); i++) {
		deleted_on_head[i] = (scr->head_workspace[i] == workspace);
		deleted_last_on_head[i] = (scr->last_head_workspace[i] == workspace);
	}

	/* Compact every non-omnipresent membership set along with the
	 * workspace array, and keep its primary in the compacted index space. */
	tmp = scr->focused_window;
	while (tmp) {
		if (!IS_OMNIPRESENT(tmp)) {
			old_mask = WWIN_WS_MASK(tmp);
			old_primary = WWIN_PRIMARY_WS(tmp);
			new_mask = (old_mask & (WS_BIT(workspace) - 1))
				| ((old_mask & ~((WS_BIT(workspace) - 1) | WS_BIT(workspace))) >> 1);
			new_primary = old_primary;
			if (old_primary == workspace)
				new_primary = __builtin_ctzll(new_mask);
			else if (old_primary > workspace)
				new_primary--;

			tmp->frame->ws_mask = new_mask;
			tmp->frame->workspace = new_primary;
			if (new_primary != old_primary)
				WMPostNotificationName(WMNChangedWorkspace, tmp,
						       (void *)(uintptr_t)old_primary);
		}
		tmp = tmp->prev;
	}

	if (!wdisp->flags.noclip) {
		wDockDestroy(scr->workspaces[workspace]->clip);
		scr->workspaces[workspace]->clip = NULL;
	}

	list = wmalloc(sizeof(WWorkspace *) * (scr->workspace_count - 1));
	j = 0;
	for (i = 0; i < scr->workspace_count; i++) {
		if (i != workspace) {
			list[j++] = scr->workspaces[i];
		} else {
			int k;
			if (scr->workspaces[i]->name)
				wfree(scr->workspaces[i]->name);
			if (scr->workspaces[i]->map)
				RReleaseImage(scr->workspaces[i]->map);
			for (k = 0; k < scr->workspaces[i]->head_name_count; k++)
				wfree(scr->workspaces[i]->head_names[k]);
			if (scr->workspaces[i]->head_names)
				wfree(scr->workspaces[i]->head_names);
			wfree(scr->workspaces[i]);
		}
	}
	wfree(scr->workspaces);
	scr->workspaces = list;

	scr->workspace_count--;
	if (scr->current_workspace > workspace)
		scr->current_workspace--;
	if (scr->last_workspace > workspace)
		scr->last_workspace--;
	if (w_global.config->per_head_workspaces) {
		for (i = 0; i < wHeadCount(scr); i++) {
			if (scr->head_workspace[i] > workspace)
				scr->head_workspace[i]--;
			if (scr->last_head_workspace[i] > workspace)
				scr->last_head_workspace[i]--;
		}
	}

	/* the deleted workspace can still be the displayed one: its clip was
	   freed and equal indices now name the replacement workspace */
	if (scr->current_workspace >= scr->workspace_count)
		scr->current_workspace = scr->workspace_count - 1;

	if (deleted_is_current && !w_global.config->per_head_workspaces) {
		wWorkspaceForceChange(scr, scr->current_workspace);
		scr->last_workspace = 0;
		if (!wdisp->flags.noclip && scr->workspaces[scr->current_workspace]->clip)
			wDockShowIcons(scr->workspaces[scr->current_workspace]->clip);
	}
	if (deleted_is_last || scr->last_workspace >= scr->workspace_count)
		scr->last_workspace = 0;

	if (w_global.config->per_head_workspaces) {
		int h;

		for (h = 0; h < wHeadCount(scr); h++) {
			if (deleted_on_head[h] || scr->head_workspace[h] >= scr->workspace_count) {
				int to = scr->head_workspace[h];

				if (to >= scr->workspace_count)
					to = scr->workspace_count - 1;
				if (!wWorkspaceIsAllowedOnHead(scr, to, h))
					to = wWorkspaceDefaultForHead(scr, h);
				wWorkspaceForceChangeOnHead(scr, to, h);
			}
			if (deleted_last_on_head[h] || scr->last_head_workspace[h] >= scr->workspace_count)
				scr->last_head_workspace[h] = 0;
		}
		wWorkspaceSetCurrentHead(scr, wGetActiveHead(scr));
	}

	for (i = 0; i < scr->workspace_count; i++)
		wLayoutApply(scr, i);

	/* update menu */
	wWorkspaceMenuUpdate(scr, scr->workspace_menu);
	/* clip workspace menu */
	wWorkspaceMenuUpdate(scr, scr->clip_ws_menu);

	/* update also window menu */
	if (scr->workspace_submenu) {
		WMenu *menu = scr->workspace_submenu;

		i = menu->entry_no;
		while (i > scr->workspace_count)
			wMenuRemoveItem(menu, --i);
		wMenuRealize(menu);
	}
	/* and clip menu */
	if (scr->clip_submenu) {
		WMenu *menu = scr->clip_submenu;

		i = menu->entry_no;
		while (i > scr->workspace_count)
			wMenuRemoveItem(menu, --i);
		wMenuRealize(menu);
	}
	wNETWMUpdateDesktop(scr);
	WMPostNotificationName(WMNWorkspaceDestroyed, scr, (void *)(uintptr_t) workspace);

	return true;
}

/* Auto-destroy trailing empty workspaces. A workspace is empty when no
 * non-omnipresent window carries its bit and its clip holds no docked
 * launchers, so the sweep never reaps a workspace out from under a
 * launcher panel the user built. The currently displayed workspace
 * (and, under PerHeadWorkspaces, every head's active workspace) is
 * never destroyed even if empty, so the user's view is never yanked by
 * an automatic sweep. Workspaces below min_workspace_count are kept as
 * a permanent floor. Only contiguous empty workspaces at the end of
 * the array are reaped, so the numbering of workspaces still in use
 * stays stable. */

/* A clip with icons beyond its main tile carries user content. */
static bool workspace_clip_has_launchers(WScreen *scr, int workspace)
{
	WDock *clip = scr->workspaces[workspace]->clip;
	int i;

	if (!clip)
		return false;
	for (i = 1; i < clip->max_icons; i++)
		if (clip->icon_array[i])
			return true;
	return false;
}

void wAutoRemoveEmptyWorkspaces(WScreen *scr)
{
	uint64_t active_mask;
	int i, h;

	if (!w_global.config->auto_remove_empty_workspaces
	    || scr->flags.startup || scr->flags.startup2)
		return;

	active_mask = WS_BIT(scr->current_workspace);
	if (w_global.config->per_head_workspaces) {
		for (h = 0; h < wHeadCount(scr); h++)
			active_mask |= WS_BIT(scr->head_workspace[h]);
	}

	while (scr->workspace_count > 0) {
		WWindow *tmp;

		i = scr->workspace_count - 1;
		if (i < w_global.config->min_workspace_count)
			break;
		if (active_mask & WS_BIT(i))
			break;
		if (!wdisp->flags.noclip && workspace_clip_has_launchers(scr, i))
			break;

		tmp = scr->focused_window;
		while (tmp) {
			if (!IS_OMNIPRESENT(tmp) && (WWIN_WS_MASK(tmp) & WS_BIT(i)))
				break;
			tmp = tmp->prev;
		}
		if (tmp)
			break;

		if (!wWorkspaceDelete(scr, i))
			break;
	}
}

#define WORKSPACE_NAME_FADE_STEPS 10

static void fadeWorkspaceName(void *data)
{
	WScreen *scr = (WScreen *)data;

	scr->workspace_name_timer = NULL;
	if (!scr->workspace_name_osd)
		return;

	if (scr->workspace_name_fade_step >= WORKSPACE_NAME_FADE_STEPS) {
		WMUnmapWidget(scr->workspace_name_osd);
		return;
	}

	scr->workspace_name_fade_step++;
	WSetOSDViewAlpha(scr->workspace_name_osd,
			 1.0 - (double)scr->workspace_name_fade_step / WORKSPACE_NAME_FADE_STEPS);
	wXFlush(wdisp->conn);
	scr->workspace_name_timer =
		WMAddTimerHandler(WORKSPACE_NAME_FADE_DELAY, fadeWorkspaceName, scr);
}

static void showWorkspaceName(WScreen *scr, int workspace)
{
	if (w_global.config->workspace_name_display_position == WD_NONE || scr->workspace_count < 2
	    || w_global.display_off || !scr->workspace_name_osd)
		return;

	if (scr->workspace_name_timer)
		WMDeleteTimerHandler(scr->workspace_name_timer);

	scr->workspace_name_fade_step = 0;
	WSetOSDViewAlpha(scr->workspace_name_osd, 1.0);
	WSetOSDViewText(scr->workspace_name_osd, scr->workspaces[workspace]->name);
	wPlaceOSDOnHead(scr, scr->workspace_name_osd,
			w_global.config->workspace_name_display_position,
			WORKSPACE_NAME_DISPLAY_PADDING);
	WMMapWidget(scr->workspace_name_osd);
	/* OSD widget, never in the stacking list, direct configure */
	xcb_configure_window(wdisp->conn, WMWidgetXID(scr->workspace_name_osd), XCB_CONFIG_WINDOW_STACK_MODE, (const uint32_t[]){ XCB_STACK_MODE_ABOVE });

	/* solid for WORKSPACE_NAME_DELAY, then a ten-step alpha ramp to
	 * zero at WORKSPACE_NAME_FADE_DELAY per step. Under a compositor
	 * the OSD window is depth-32 so the ramp is genuinely translucent,
	 * otherwise it darkens toward black before the unmap. */
	scr->workspace_name_timer =
		WMAddTimerHandler(WORKSPACE_NAME_DELAY, fadeWorkspaceName, scr);
}

void wWorkspaceChange(WScreen *scr, int workspace)
{
	if (scr->flags.startup || scr->flags.startup2 || scr->flags.ignore_focus_events)
		return;

	if (w_global.config->per_head_workspaces) {
		int head = wGetActiveHead(scr);

		if (workspace > scr->workspace_count - 1
		    && workspace < MAX_WORKSPACES && w_global.config->ws_advance)
			wWorkspaceMake(scr, workspace - scr->workspace_count + 1);
		if (!wWorkspaceIsAllowedOnHead(scr, workspace, head))
			return;
		if (workspace != scr->head_workspace[head])
			wWorkspaceForceChangeOnHead(scr, workspace, head);
		return;
	}

	if (workspace != scr->current_workspace)
		wWorkspaceForceChange(scr, workspace);
}

/* Pointer-initiated wrappers set w_global.pointer_driven_change so the warp
 * decision in wWorkspaceForceChange[OnHead] skips recentering the pointer. */
void wWorkspaceChangeFromPointer(WScreen *scr, int workspace)
{
	bool prev = w_global.pointer_driven_change;
	w_global.pointer_driven_change = true;
	wWorkspaceChange(scr, workspace);
	w_global.pointer_driven_change = prev;
}

void wWorkspaceRelativeChangeFromPointer(WScreen *scr, int amount)
{
	bool prev = w_global.pointer_driven_change;
	w_global.pointer_driven_change = true;
	wWorkspaceRelativeChange(scr, amount);
	w_global.pointer_driven_change = prev;
}

void wWorkspaceDirectionalChangeFromPointer(WScreen *scr, int dcol, int drow)
{
	bool prev = w_global.pointer_driven_change;
	w_global.pointer_driven_change = true;
	wWorkspaceDirectionalChange(scr, dcol, drow);
	w_global.pointer_driven_change = prev;
}

/* Advance one head's workspace by amount, skipping workspaces pinned to
 * other outputs so Next/Prev never lands on a workspace that cannot be
 * shown on the active head. */
int wWorkspaceRelativeHeadTarget(WScreen *scr, int head, int amount)
{
	int n = scr->workspace_count;
	int dir = (amount > 0) ? 1 : -1;
	int w = scr->head_workspace[head];
	int steps;

	if (amount == 0 || n <= 0)
		return -1;

	for (steps = 0; steps < n + 1; steps++) {
		w += dir;
		if (w < 0) {
			if (!w_global.config->ws_cycle)
				return -1;
			w = n - 1;
		} else if (w >= n) {
			if (w_global.config->ws_advance)
				return WMIN(scr->head_workspace[head] + amount,
					    MAX_WORKSPACES - 1);
			if (!w_global.config->ws_cycle)
				return -1;
			w = 0;
		}
		if (wWorkspaceIsAllowedOnHead(scr, w, head)) {
			amount -= dir;
			if (amount == 0)
				return w;
		}
	}
	return -1;
}

/* Occupied = a non-omnipresent window has the workspace bit, iconic counts.
 * Same test as wAutoRemoveEmptyWorkspaces. */
static bool workspace_is_occupied(WScreen *scr, int workspace)
{
	WWindow *tmp;

	if (workspace < 0 || workspace >= scr->workspace_count)
		return false;
	for (tmp = scr->focused_window; tmp; tmp = tmp->prev)
		if (!IS_OMNIPRESENT(tmp) && (WWIN_WS_MASK(tmp) & WS_BIT(workspace)))
			return true;
	return false;
}

/* Occupied-only walk of wWorkspaceRelativeHeadTarget (head < 0 = any head).
 * ws_advance is suppressed: a freshly grown workspace is empty by definition. */
static int wWorkspaceRelativeOccupiedTarget(WScreen *scr, int head, int amount)
{
	bool occupied[MAX_WORKSPACES];
	bool allowed[MAX_WORKSPACES];
	int from_ws = (head >= 0) ? scr->head_workspace[head] : scr->current_workspace;
	int i;

	for (i = 0; i < scr->workspace_count && i < MAX_WORKSPACES; i++) {
		occupied[i] = workspace_is_occupied(scr, i);
		allowed[i] = (head < 0) ? true : wWorkspaceIsAllowedOnHead(scr, i, head);
	}

	return wWsRelativeOccupiedTarget(scr->workspace_count, from_ws, amount,
					 w_global.config->ws_cycle, occupied, allowed);
}

void wWorkspaceRelativeChange(WScreen * scr, int amount)
{
	int w;

	/* While the deiconify animation is going on the window is
	 * still "flying" to its final position and we don't want to
	 * change workspace before the animation finishes, otherwise
	 * the window will land in the new workspace */
	if (w_global.ignore_workspace_change)
		return;

	if (w_global.config->per_head_workspaces) {
		int head = wGetActiveHead(scr);

		if (w_global.config->skip_empty_workspaces)
			w = wWorkspaceRelativeOccupiedTarget(scr, head, amount);
		else
			w = wWorkspaceRelativeHeadTarget(scr, head, amount);
		if (w >= 0)
			wWorkspaceChange(scr, w);
		return;
	}

	if (w_global.config->skip_empty_workspaces) {
		w = wWorkspaceRelativeOccupiedTarget(scr, -1, amount);
		if (w >= 0)
			wWorkspaceChange(scr, w);
		return;
	}

	w = scr->current_workspace + amount;

	if (amount < 0) {
		if (w >= 0) {
			wWorkspaceChange(scr, w);
		} else if (w_global.config->ws_cycle) {
			wWorkspaceChange(scr, scr->workspace_count + w);
		}
	} else if (amount > 0) {
		if (w < scr->workspace_count) {
			wWorkspaceChange(scr, w);
		} else if (w_global.config->ws_advance) {
			wWorkspaceChange(scr, WMIN(w, MAX_WORKSPACES - 1));
		} else if (w_global.config->ws_cycle) {
			wWorkspaceChange(scr, w % scr->workspace_count);
		}
	}
}

/*
 * Map a workspace index through the _NET_DESKTOP_LAYOUT grid by (dcol, drow)
 * cells and return the resulting index, or -1 when stepping off the grid (or
 * onto a cell past workspace_count) and CycleWorkspaces is off.
 *
 * The pager-supplied orientation decides whether consecutive indices run
 * along columns (HORZ) or rows (VERT), and the starting corner decides which
 * grid corner index 0 sits in. We translate index -> (row,col) in a top-left
 * frame, apply the screen-relative delta with the corner's axis flips, then
 * translate back.
 */
/* map a workspace index to its screen-relative (row, col) cell in the
 * _NET_DESKTOP_LAYOUT grid, with (0,0) always meaning top-left on screen
 * regardless of the pager's starting corner, return False when the index
 * is outside the grid */
bool wWorkspaceLayoutPosition(WScreen *scr, int index, int *row, int *col)
{
	return wWsLayoutPosition(scr->net_desktop_layout.rows,
				 scr->net_desktop_layout.cols,
				 scr->net_desktop_layout.orientation,
				 scr->net_desktop_layout.corner,
				 index, row, col);
}

int wWorkspaceNeighbour(WScreen *scr, int from, int dcol, int drow)
{
	return wWsNeighbour(scr->net_desktop_layout.rows,
			    scr->net_desktop_layout.cols,
			    scr->net_desktop_layout.orientation,
			    scr->net_desktop_layout.corner,
			    scr->workspace_count,
			    w_global.config->ws_cycle,
			    from, dcol, drow);
}

void wWorkspaceDirectionalChange(WScreen *scr, int dcol, int drow)
{
	int to;

	if (w_global.ignore_workspace_change)
		return;

	if (w_global.config->per_head_workspaces) {
		int head = wGetActiveHead(scr);
		bool occupied[MAX_WORKSPACES];
		bool allowed[MAX_WORKSPACES];
		int i;

		for (i = 0; i < scr->workspace_count && i < MAX_WORKSPACES; i++) {
			occupied[i] = workspace_is_occupied(scr, i);
			allowed[i] = wWorkspaceIsAllowedOnHead(scr, i, head);
		}

		/* Walk the layout in the requested direction until we land
		 * on a workspace allowed on the active head, so a workspace
		 * pinned to another output is skipped instead of swallowing
		 * the keystroke. */
		to = wWsDirectionalTarget(scr->net_desktop_layout.rows,
					  scr->net_desktop_layout.cols,
					  scr->net_desktop_layout.orientation,
					  scr->net_desktop_layout.corner,
					  scr->workspace_count,
					  scr->head_workspace[head],
					  dcol, drow,
					  w_global.config->ws_cycle,
					  w_global.config->skip_empty_workspaces,
					  occupied, allowed);
		if (to >= 0)
			wWorkspaceForceChangeOnHead(scr, to, head);
		return;
	}

	if (w_global.config->skip_empty_workspaces) {
		bool occupied[MAX_WORKSPACES];
		bool allowed[MAX_WORKSPACES];
		int i;

		for (i = 0; i < scr->workspace_count && i < MAX_WORKSPACES; i++) {
			occupied[i] = workspace_is_occupied(scr, i);
			allowed[i] = true;
		}

		to = wWsDirectionalTarget(scr->net_desktop_layout.rows,
					  scr->net_desktop_layout.cols,
					  scr->net_desktop_layout.orientation,
					  scr->net_desktop_layout.corner,
					  scr->workspace_count,
					  scr->current_workspace,
					  dcol, drow,
					  w_global.config->ws_cycle,
					  true, occupied, allowed);
		if (to >= 0)
			wWorkspaceChange(scr, to);
		return;
	}

	to = wWorkspaceNeighbour(scr, scr->current_workspace, dcol, drow);
	if (to >= 0)
		wWorkspaceChange(scr, to);
}

/* reduce one window into the model's per-window decision inputs, run
 * after the omnipresent retag so the pre-reduced state matches what
 * the original loop saw */
static void ws_reduce_win(WWindow *wwin, int to_ws, bool per_head, int head,
			  WWsWinIn *in)
{
	memset(in, 0, sizeof(*in));
	in->win = wwin;
	in->omnipresent = IS_OMNIPRESENT(wwin);
	in->on_target = per_head ? wWindowIsVisibleOnAnyHead(wwin)
				 : WWIN_ON_WORKSPACE(wwin, to_ws);
	in->selected = wwin->flags.selected;
	in->changing_workspace = wwin->flags.changing_workspace;
	in->mapped = wwin->flags.mapped;
	in->shaded = wwin->flags.shaded;
	in->miniaturized = wwin->flags.miniaturized;
	in->hidden = wwin->flags.hidden;
	in->has_icon = wwin->icon != NULL;
	in->no_focusable = WFLAGP(wwin, no_focusable);
	in->group_inactive_member = wGroupIsInactiveMember(wwin);
	in->effective_head_matches = per_head && wWindowEffectiveHead(wwin) == head;
}

void wWorkspaceForceChange(WScreen * scr, int workspace)
{
	WWindow *tmp, *foc = NULL, *foc2 = NULL;

	if (workspace >= MAX_WORKSPACES || workspace < 0)
		return;

	if (w_global.config->enable_workspace_pager && !w_global.process_workspacemap_event)
		wWorkspaceMapUpdate(scr);

	SendHelperMessage(scr, 'C', workspace + 1, NULL);

	if (workspace > scr->workspace_count - 1)
		wWorkspaceMake(scr, workspace - scr->workspace_count + 1);

	wClipUpdateForWorkspaceChange(scr, workspace);

	scr->last_workspace = scr->current_workspace;
	scr->current_workspace = workspace;

	wWorkspaceMenuUpdate(scr, scr->workspace_menu);

	wWorkspaceMenuUpdate(scr, scr->clip_ws_menu);

	tmp = scr->focused_window;
	if (tmp != NULL) {
		WWindow **toUnmap;
		int toUnmapSize, toUnmapCount;

		if ((IS_OMNIPRESENT(tmp) && (tmp->flags.mapped || tmp->flags.shaded) &&
		     !WFLAGP(tmp, no_focusable)) || tmp->flags.changing_workspace) {
			foc = tmp;
		}

		toUnmapSize = 16;
		toUnmapCount = 0;
		toUnmap = wmalloc(toUnmapSize * sizeof(WWindow *));

		/* foc2 = tmp; will fix annoyance with gnome panel
		 * but will create annoyance for every other application
		 */
		while (tmp) {
			WWsWinIn in;
			WWsStep steps[WWS_MAX_STEPS];
			int n, s;

			/* Omnipresent windows track the new current workspace
			 * as their primary. Any window already on the target
			 * (omnipresent or a multi-member set that includes it)
			 * is a fallback focus candidate when nothing has to be
			 * remapped below. */
			if (IS_OMNIPRESENT(tmp) && WWIN_PRIMARY_WS(tmp) != workspace) {
				WApplication *wapp = wApplicationOf(tmp->main_window);

				tmp->frame->workspace = workspace;
				if (wapp)
					wapp->last_workspace = workspace;
			}

			ws_reduce_win(tmp, workspace, false, 0, &in);
			wWorkspaceWindowDecision(false, w_global.config->sticky_icons,
						 &in, steps, WWS_MAX_STEPS, &n);

			for (s = 0; s < n; s++) {
				if (steps[s].focus_priority == WWS_FOCUS_FALLBACK && !foc2)
					foc2 = tmp;

				switch (steps[s].kind) {
				case WWS_REMAP:
					/* remap windows that are on this workspace */
					wWindowMap(tmp);
					if (steps[s].focus_priority == WWS_FOCUS_PRIMARY && !foc)
						foc = tmp;
					break;

				case WWS_MAP_ICON:
					tmp->icon->mapped = 1;
					xcb_map_window(wdisp->conn, tmp->icon->core->window);
					break;

				case WWS_UNMAP:
				case WWS_UNMAP_ICON:
					/* defer unmap of windows (and per-workspace
					 * miniwindow icons) not on this workspace until
					 * after the destination workspace's windows have
					 * been mapped, so the root background is never
					 * exposed mid-switch */
					if (toUnmapCount == toUnmapSize)
					{
						toUnmapSize *= 2;
						toUnmap = wrealloc(toUnmap, toUnmapSize * sizeof(WWindow *));
					}
					toUnmap[toUnmapCount++] = tmp;
					break;

				case WWS_KEEP:
					if (steps[s].focus_priority == WWS_FOCUS_PRIMARY && !foc)
						foc = tmp;
					break;
				}
			}

			/* change selected windows' workspace. The move has
			 * immediate visibility, map/unmap and group-sync
			 * effects on later decisions, so it stays at its
			 * traversal point */
			if (tmp->flags.selected)
				wWindowChangeWorkspace(tmp, workspace);

			tmp = tmp->prev;
		}

		while (toUnmapCount > 0)
		{
			tmp = toUnmap[--toUnmapCount];
			if (tmp->flags.miniaturized && tmp->icon) {
				xcb_unmap_window(wdisp->conn, tmp->icon->core->window);
				tmp->icon->mapped = 0;
			} else {
				wWindowUnmap(tmp);
			}
		}
		wfree(toUnmap);

		/* Gobble up events unleashed by our mapping & unmapping.
		 * These may trigger various grab-initiated focus &
		 * crossing events. However, we don't care about them,
		 * and ignore their focus implications altogether to avoid
		 * flicker.
		 */
		scr->flags.ignore_focus_events = 1;
		ProcessPendingEvents();
		scr->flags.ignore_focus_events = 0;

		if (!foc)
			foc = foc2;

		/*
		 * Check that the window we want to focus still exists, because the application owning it
		 * could decide to unmap/destroy it in response to unmap any of its other window following
		 * the workspace change, this happening during our 'ProcessPendingEvents' loop.
		 */
		if (foc != NULL) {
			WWindow *parse;
			bool found;

			found = false;
			for (parse = scr->focused_window; parse != NULL; parse = parse->prev) {
				if (parse == foc) {
					found = true;
					break;
				}
			}
			if (!found)
				foc = NULL;
		}

		if (scr->focused_window && scr->focused_window->flags.mapped && !foc) {
			foc = scr->focused_window;
		}
		if (!scr->flags.suppress_switch_focus) {
			if (w_global.config->focus_mode == WKF_CLICK) {
				wSetFocusTo(scr, foc);
				if (!w_global.pointer_driven_change) {
					if (foc)
						warpPointerToFocusedWindow(foc);
					else
						warpPointerToMonitor(scr);
				}
			} else {
				unsigned int mask;
				int foo;
				xcb_window_t bar, win;
				WWindow *tmp;

				tmp = NULL;
				if (wXQueryPointer(wdisp->conn, scr->root_win, &bar, &win, &foo, &foo, &foo, &foo, &mask))
					tmp = wWindowFor(win);

				/* If there's a window under the pointer, focus it.
				 * (we ate all other focus events above, so it's
				 * certainly not focused). Otherwise focus last
				 * focused, or the root (depending on sloppiness)
				 */
				if (!tmp && w_global.config->focus_mode == WKF_SLOPPY) {
					wSetFocusTo(scr, foc);
					if (!w_global.pointer_driven_change) {
						if (foc)
							warpPointerToFocusedWindow(foc);
						else
							warpPointerToMonitor(scr);
					}
				} else {
					wSetFocusTo(scr, tmp);
				}
			}
		}
	}

	/* We need to always arrange icons when changing workspace, even if
	 * no autoarrange icons, because else the icons in different workspaces
	 * can be superposed.
	 * This can be avoided if appicons are also workspace specific.
	 */
	if (!w_global.config->sticky_icons)
		wArrangeIcons(scr, false);

	if (scr->dock)
		wAppIconPaint(scr->dock->icon_array[0]);

	if (!wdisp->flags.noclip && (scr->workspaces[workspace]->clip->auto_collapse ||
						scr->workspaces[workspace]->clip->auto_raise_lower)) {
		/* to handle enter notify. This will also */
		xcb_unmap_window(wdisp->conn, scr->clip_icon->icon->core->window);
		xcb_map_window(wdisp->conn, scr->clip_icon->icon->core->window);
	}
	else if (scr->clip_icon != NULL) {
		wClipIconPaint(scr->clip_icon);
	}
	wScreenUpdateUsableArea(scr);
	wNETWMUpdateDesktop(scr);
	showWorkspaceName(scr, workspace);
	if (scr->workspaces[workspace]->layout == WLAYOUT_FLOAT)
		wLayoutRestore(scr, workspace);
	else
		wLayoutApply(scr, workspace);

	WMPostNotificationName(WMNWorkspaceChanged, scr, (void *)(uintptr_t) workspace);
	wSoundPlay(scr, WSOUND_WORKSPACE);

	/*   wXSync(wdisp->conn); */
}

/* If the target workspace is pinned to several outputs, switching it on
 * one head should switch it on every listed active head together so the
 * workspace spans those monitors atomically. Fill switch_head[] with
 * the heads to update, always including the initiating head. */
static int collectHeadsToSwitch(WScreen *scr, int workspace, int head, int *switch_head)
{
	WWorkspace *ws = scr->workspaces[workspace];
	int n = 0, i, j, h;

	switch_head[n++] = head;
	if (ws->head_name_count <= 1)
		return n;
	for (i = 0; i < ws->head_name_count && n < WM_MAX_HEADS; i++) {
		int dup = 0;

		h = wHeadForOutputName(scr, ws->head_names[i]);
		if (h < 0 || h >= WM_MAX_HEADS)
			continue;
		for (j = 0; j < n; j++) {
			if (switch_head[j] == h) {
				dup = 1;
				break;
			}
		}
		if (!dup)
			switch_head[n++] = h;
	}
	return n;
}

/* Per-head equivalent of wWorkspaceForceChange. Updates head_workspace[]
 * for the requested head (and any sibling heads listed in a multi-output
 * WorkspaceOutputs pin), then runs one map/unmap pass over every managed
 * window using wWindowIsVisibleOnAnyHead() instead of comparing against
 * a single workspace index. */
void wWorkspaceForceChangeOnHead(WScreen *scr, int workspace, int head)
{
	WWindow *tmp, *foc = NULL;
	int switch_head[WM_MAX_HEADS];
	int n_switch, i;
	int old_mirror;

	if (workspace >= MAX_WORKSPACES || workspace < 0)
		return;
	if (head < 0 || head >= wHeadCount(scr))
		return;

	if (w_global.config->enable_workspace_pager && !w_global.process_workspacemap_event)
		wWorkspaceMapUpdate(scr);

	SendHelperMessage(scr, 'C', workspace + 1, NULL);

	if (workspace > scr->workspace_count - 1)
		wWorkspaceMake(scr, workspace - scr->workspace_count + 1);

	n_switch = collectHeadsToSwitch(scr, workspace, head, switch_head);
	for (i = 0; i < n_switch; i++) {
		int h = switch_head[i];
		scr->last_head_workspace[h] = scr->head_workspace[h];
		scr->head_workspace[h] = workspace;
	}

	/* Update the compatibility mirror through the real clip path so
	 * omnipresent clip icons migrate exactly once for this switch. */
	old_mirror = scr->current_workspace;
	scr->last_active_head = head;
	wClipUpdateForWorkspaceChange(scr, workspace);
	scr->last_workspace = old_mirror;
	scr->current_workspace = workspace;

	wWorkspaceMenuUpdate(scr, scr->workspace_menu);
	wWorkspaceMenuUpdate(scr, scr->clip_ws_menu);

	tmp = scr->focused_window;
	if (tmp != NULL) {
		WWindow **toUnmap;
		int toUnmapSize, toUnmapCount;

		if ((IS_OMNIPRESENT(tmp) && (tmp->flags.mapped || tmp->flags.shaded) &&
		     !WFLAGP(tmp, no_focusable)) || tmp->flags.changing_workspace)
			foc = tmp;

		toUnmapSize = 16;
		toUnmapCount = 0;
		toUnmap = wmalloc(toUnmapSize * sizeof(WWindow *));

		while (tmp) {
			WWsWinIn in;
			WWsStep steps[WWS_MAX_STEPS];
			int n, s;

			ws_reduce_win(tmp, workspace, true, head, &in);
			wWorkspaceWindowDecision(true, w_global.config->sticky_icons,
						 &in, steps, WWS_MAX_STEPS, &n);

			for (s = 0; s < n; s++) {
				switch (steps[s].kind) {
				case WWS_REMAP:
					wWindowMap(tmp);
					if (steps[s].focus_priority == WWS_FOCUS_PRIMARY && !foc)
						foc = tmp;
					break;

				case WWS_MAP_ICON:
					tmp->icon->mapped = 1;
					xcb_map_window(wdisp->conn, tmp->icon->core->window);
					break;

				case WWS_UNMAP:
				case WWS_UNMAP_ICON:
					if (toUnmapCount == toUnmapSize) {
						toUnmapSize *= 2;
						toUnmap = wrealloc(toUnmap, toUnmapSize * sizeof(WWindow *));
					}
					toUnmap[toUnmapCount++] = tmp;
					break;

				case WWS_KEEP:
					if (steps[s].focus_priority == WWS_FOCUS_PRIMARY && !foc)
						foc = tmp;
					break;
				}
			}

			/* change selected windows' workspace. The move has
			 * immediate visibility, map/unmap and group-sync
			 * effects on later decisions, so it stays at its
			 * traversal point */
			if (tmp->flags.selected)
				wWindowChangeWorkspace(tmp, workspace);

			tmp = tmp->prev;
		}

		while (toUnmapCount > 0) {
			tmp = toUnmap[--toUnmapCount];
			if (tmp->flags.miniaturized && tmp->icon) {
				xcb_unmap_window(wdisp->conn, tmp->icon->core->window);
				tmp->icon->mapped = 0;
			} else {
				wWindowUnmap(tmp);
			}
		}
		wfree(toUnmap);

		scr->flags.ignore_focus_events = 1;
		ProcessPendingEvents();
		scr->flags.ignore_focus_events = 0;

		if (foc != NULL) {
			WWindow *parse;
			bool found = false;
			for (parse = scr->focused_window; parse != NULL; parse = parse->prev) {
				if (parse == foc) {
					found = true;
					break;
				}
			}
			if (!found)
				foc = NULL;
		}

		if (scr->focused_window && scr->focused_window->flags.mapped && !foc)
			foc = scr->focused_window;
		if (!scr->flags.suppress_switch_focus) {
			if (w_global.config->focus_mode == WKF_CLICK) {
				wSetFocusTo(scr, foc);
				if (!w_global.pointer_driven_change) {
					if (foc)
						warpPointerToFocusedWindow(foc);
					else
						warpPointerToMonitor(scr);
				}
			} else {
				unsigned int mask;
				int foo;
				xcb_window_t bar, win;
				WWindow *ptmp = NULL;

				if (wXQueryPointer(wdisp->conn, scr->root_win, &bar, &win, &foo, &foo, &foo, &foo, &mask))
					ptmp = wWindowFor(win);
				if (!ptmp && w_global.config->focus_mode == WKF_SLOPPY) {
					wSetFocusTo(scr, foc);
					if (!w_global.pointer_driven_change) {
						if (foc)
							warpPointerToFocusedWindow(foc);
						else
							warpPointerToMonitor(scr);
					}
				} else
					wSetFocusTo(scr, ptmp);
			}
		}
	}

	if (!w_global.config->sticky_icons)
		wArrangeIcons(scr, false);

	if (scr->dock)
		wAppIconPaint(scr->dock->icon_array[0]);

	if (!wdisp->flags.noclip && (scr->workspaces[workspace]->clip->auto_collapse ||
						scr->workspaces[workspace]->clip->auto_raise_lower)) {
		xcb_unmap_window(wdisp->conn, scr->clip_icon->icon->core->window);
		xcb_map_window(wdisp->conn, scr->clip_icon->icon->core->window);
	} else if (scr->clip_icon != NULL) {
		wClipIconPaint(scr->clip_icon);
	}
	wScreenUpdateUsableArea(scr);
	wNETWMUpdateDesktop(scr);
	wNETWMUpdateCurrentDesktop(scr);
	showWorkspaceName(scr, workspace);
	if (scr->workspaces[workspace]->layout == WLAYOUT_FLOAT)
		wLayoutRestore(scr, workspace);
	else
		wLayoutApply(scr, workspace);

	WMPostNotificationName(WMNWorkspaceChanged, scr, (void *)(uintptr_t) workspace);
	wSoundPlay(scr, WSOUND_WORKSPACE);
}

/* One full map/unmap reconciliation over every window using the per-head
 * visibility predicate, with no focus change. Used at startup after
 * head_workspace[] has been restored and after RandR hotplug remap. */
void wWorkspaceReconcileHeads(WScreen *scr)
{
	WWindow *tmp;

	if (!w_global.config->per_head_workspaces)
		return;

	for (tmp = scr->focused_window; tmp; tmp = tmp->prev) {
		bool visible = wWindowIsVisibleOnAnyHead(tmp);

		if (tmp->flags.hidden)
			continue;
		if (visible) {
			if (!(tmp->flags.mapped || tmp->flags.miniaturized || tmp->flags.shaded)
			    && !wGroupIsInactiveMember(tmp))
				wWindowMap(tmp);
			if (!w_global.config->sticky_icons && tmp->flags.miniaturized
			    && !IS_OMNIPRESENT(tmp) && tmp->icon) {
				tmp->icon->mapped = 1;
				xcb_map_window(wdisp->conn, tmp->icon->core->window);
			}
		} else {
			if ((tmp->flags.mapped || tmp->flags.shaded)
			    && !IS_OMNIPRESENT(tmp) && !tmp->flags.changing_workspace)
				wWindowUnmap(tmp);
			if (!w_global.config->sticky_icons && tmp->flags.miniaturized
			    && !IS_OMNIPRESENT(tmp) && tmp->icon) {
				xcb_unmap_window(wdisp->conn, tmp->icon->core->window);
				tmp->icon->mapped = 0;
			}
		}
	}
}

void wWorkspaceClearOutputPins(WScreen *scr)
{
	int i, k;

	for (i = 0; i < scr->workspace_count; i++) {
		WWorkspace *ws = scr->workspaces[i];
		for (k = 0; k < ws->head_name_count; k++)
			wfree(ws->head_names[k]);
		if (ws->head_names)
			wfree(ws->head_names);
		ws->head_names = NULL;
		ws->head_name_count = 0;
	}
}

static void addPinName(WWorkspace *ws, const char *name)
{
	int i;

	if (!name || ws->head_name_count >= WM_MAX_HEADS)
		return;
	for (i = 0; i < ws->head_name_count; i++) {
		if (strcmp(ws->head_names[i], name) == 0)
			return;
	}
	ws->head_names = wrealloc(ws->head_names,
				  sizeof(char *) * (ws->head_name_count + 1));
	ws->head_names[ws->head_name_count++] = wstrdup(name);
}

/* Parse the WorkspaceOutputs dictionary from the WindowMaker defaults
 * domain. Keys are 1-based workspace numbers (or names, resolved at
 * parse time), values are an output-name string or an array of strings.
 * Unknown workspaces are warned and skipped, unknown output names are
 * accepted so a pin survives the output being unplugged. */
void wWorkspaceLoadOutputPins(WScreen *scr, WMPropList *wmaker_dict)
{
	WMPropList *key, *pins, *keys, *k, *v;
	int i, n;

	wWorkspaceClearOutputPins(scr);

	if (!wmaker_dict || !WMIsPLDictionary(wmaker_dict))
		return;

	key = WMCreatePLString("WorkspaceOutputs");
	pins = WMGetFromPLDictionary(wmaker_dict, key);
	WMReleasePropList(key);
	if (!pins || !WMIsPLDictionary(pins))
		return;

	keys = WMGetPLDictionaryKeys(pins);
	if (!keys)
		return;
	n = WMGetPropListItemCount(keys);
	for (i = 0; i < n; i++) {
		int w;

		k = WMGetFromPLArray(keys, i);
		if (!k || !WMIsPLString(k))
			continue;
		w = wGetWorkspaceNumber(scr, WMGetFromPLString(k));
		if (w < 0 || w >= scr->workspace_count) {
			wwarning(_("WorkspaceOutputs: unknown workspace \"%s\""),
				 WMGetFromPLString(k));
			continue;
		}
		v = WMGetFromPLDictionary(pins, k);
		if (!v)
			continue;
		if (WMIsPLString(v)) {
			addPinName(scr->workspaces[w], WMGetFromPLString(v));
		} else if (WMIsPLArray(v)) {
			int j, m = WMGetPropListItemCount(v);
			for (j = 0; j < m; j++) {
				WMPropList *e = WMGetFromPLArray(v, j);
				if (e && WMIsPLString(e))
					addPinName(scr->workspaces[w], WMGetFromPLString(e));
			}
		}
	}
	WMReleasePropList(keys);
}

/* Pick a workspace for a head that has no restored value: prefer one
 * pinned to that head's output, then the first allowed workspace not
 * currently shown on any other head, then any allowed workspace even if
 * already shown elsewhere. If every existing workspace is pinned to
 * outputs other than this head's, create a fresh unpinned workspace so
 * the head never ends up showing one its WorkspaceOutputs forbid. */
int wWorkspaceDefaultForHead(WScreen *scr, int head)
{
	WWorkspace *ws;
	int i, h, busy;

	for (i = 0; i < scr->workspace_count; i++) {
		ws = scr->workspaces[i];
		if (ws->head_name_count > 0 && wWorkspaceIsAllowedOnHead(scr, i, head))
			return i;
	}
	for (i = 0; i < scr->workspace_count; i++) {
		if (!wWorkspaceIsAllowedOnHead(scr, i, head))
			continue;
		busy = 0;
		for (h = 0; h < wHeadCount(scr); h++) {
			if (h != head && scr->head_workspace[h] == i) {
				busy = 1;
				break;
			}
		}
		if (!busy)
			return i;
	}
	for (i = 0; i < scr->workspace_count; i++) {
		if (wWorkspaceIsAllowedOnHead(scr, i, head))
			return i;
	}

	i = wWorkspaceNew(scr);
	if (i >= 0) {
		wwarning(_("no workspace allowed on output \"%s\", created \"%s\""),
			 wOutputNameForHead(scr, head) ? wOutputNameForHead(scr, head) : "?",
			 scr->workspaces[i]->name);
		return i;
	}

	/* MAX_WORKSPACES reached and every one of them is pinned away from
	 * this head, so the config is unsatisfiable as written. Drop the pin
	 * on workspace 0 so the returned value is genuinely allowed and the
	 * caller never forces a disallowed workspace onto the head. */
	ws = scr->workspaces[0];
	for (i = 0; i < ws->head_name_count; i++)
		wfree(ws->head_names[i]);
	if (ws->head_names)
		wfree(ws->head_names);
	ws->head_names = NULL;
	ws->head_name_count = 0;
	wwarning(_("no workspace allowed on output \"%s\" and at MAX_WORKSPACES, "
		   "dropping WorkspaceOutputs pin on \"%s\""),
		 wOutputNameForHead(scr, head) ? wOutputNameForHead(scr, head) : "?",
		 ws->name);
	return 0;
}

/* Persist per-head workspaces by RandR output name so a monitor keeps
 * its workspace across reconnects and head index reorders, with a "#N"
 * index fallback for unnamed (non-RandR) heads. Stored in WMState next
 * to the existing workspace session keys. Values are 1-based strings. */
void wWorkspaceSaveHeadState(WScreen *scr)
{
	WMPropList *dict, *k, *v;
	char buf[64];
	int h;

	if (!w_global.config->per_head_workspaces || !scr->session_state)
		return;

	make_keys();
	dict = WMCreatePLDictionary(NULL, NULL);
	for (h = 0; h < wHeadCount(scr); h++) {
		const char *out = wOutputNameForHead(scr, h);
		if (out)
			k = WMCreatePLString(out);
		else {
			snprintf(buf, sizeof(buf), "#%d", h);
			k = WMCreatePLString(buf);
		}
		snprintf(buf, sizeof(buf), "%d", scr->head_workspace[h] + 1);
		v = WMCreatePLString(buf);
		WMPutInPLDictionary(dict, k, v);
		WMReleasePropList(k);
		WMReleasePropList(v);
	}
	WMPutInPLDictionary(scr->session_state, dHeadWorkspaces, dict);
	WMReleasePropList(dict);
}

void wWorkspaceRestoreHeadState(WScreen *scr)
{
	WMPropList *dict, *k, *v;
	char buf[64];
	int h, w;

	if (!w_global.config->per_head_workspaces)
		return;

	make_keys();
	for (h = 0; h < WM_MAX_HEADS; h++) {
		scr->head_workspace[h] = -1;
		scr->last_head_workspace[h] = 0;
	}

	dict = scr->session_state
		? WMGetFromPLDictionary(scr->session_state, dHeadWorkspaces) : NULL;
	if (dict && WMIsPLDictionary(dict)) {
		for (h = 0; h < wHeadCount(scr); h++) {
			const char *out = wOutputNameForHead(scr, h);
			v = NULL;
			if (out) {
				k = WMCreatePLString(out);
				v = WMGetFromPLDictionary(dict, k);
				WMReleasePropList(k);
			}
			if (!v) {
				snprintf(buf, sizeof(buf), "#%d", h);
				k = WMCreatePLString(buf);
				v = WMGetFromPLDictionary(dict, k);
				WMReleasePropList(k);
			}
			if (v && WMIsPLString(v)
			    && sscanf(WMGetFromPLString(v), "%d", &w) == 1
			    && w - 1 >= 0 && w - 1 < scr->workspace_count
			    && wWorkspaceIsAllowedOnHead(scr, w - 1, h))
				scr->head_workspace[h] = w - 1;
		}
	}

	for (h = 0; h < wHeadCount(scr); h++) {
		if (scr->head_workspace[h] < 0)
			scr->head_workspace[h] = wWorkspaceDefaultForHead(scr, h);
	}
	scr->last_active_head = scr->xine_info.primary_head;
	scr->current_workspace = scr->head_workspace[scr->last_active_head];
}

static void switchWSCommand(WMenu * menu, WMenuEntry * entry)
{
	wWorkspaceChangeFromPointer(menu->frame->screen_ptr, (long)entry->clientdata);
}

static void lastWSCommand(WMenu *menu, WMenuEntry *entry)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) entry;

	wWorkspaceChangeFromPointer(menu->frame->screen_ptr, menu->frame->screen_ptr->last_workspace);
}

static void deleteWSCommand(WMenu *menu, WMenuEntry *entry)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) entry;

	wWorkspaceDelete(menu->frame->screen_ptr, menu->frame->screen_ptr->workspace_count - 1);
}

static bool workspace_name_taken(WScreen *scr, int workspace, const char *name)
{
	int i;

	for (i = 0; i < scr->workspace_count; i++) {
		if (i != workspace && strcmp(scr->workspaces[i]->name, name) == 0)
			return true;
	}
	return false;
}

static void newWSCommand(WMenu *menu, WMenuEntry *foo)
{
	int ws;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;

	ws = wWorkspaceNew(menu->frame->screen_ptr);

	/* autochange workspace */
	if (ws >= 0)
		wWorkspaceChangeFromPointer(menu->frame->screen_ptr, ws);
}

void wWorkspaceRename(WScreen *scr, int workspace, const char *name)
{
	char buf[MAX_WORKSPACENAME_WIDTH + 1];
	char *tmp;
	WMenu *menu;
	int idx;

	if (workspace >= scr->workspace_count)
		return;

	/* trim white spaces */
	tmp = wtrimspace(name);

	if (strlen(tmp) == 0) {
		snprintf(buf, sizeof(buf), _("Workspace %i"), workspace + 1);
	} else {
		wstrlcpy(buf, tmp, sizeof(buf));
	}
	wfree(tmp);

	if (workspace_name_taken(scr, workspace, buf)) {
		char base[MAX_WORKSPACENAME_WIDTH + 1];
		int n;

		wstrlcpy(base, buf, sizeof(base));
		for (n = 2; n <= MAX_WORKSPACES; n++) {
			snprintf(buf, sizeof(buf), "%.*s (%d)",
				 MAX_WORKSPACENAME_WIDTH - 8, base, n);
			if (!workspace_name_taken(scr, workspace, buf))
				break;
		}
	}

	/* update workspace */
	wfree(scr->workspaces[workspace]->name);
	scr->workspaces[workspace]->name = wstrdup(buf);

	menu = scr->clip_ws_menu;
	if (menu) {
		idx = workspace + MC_WORKSPACE1(menu);
		/* a clip menu can predate any workspace entry, in which
		 * case the next wWorkspaceMenuUpdate copies fresh names */
		if (idx < menu->entry_no) {
			if (strcmp(menu->entries[idx]->text, buf) != 0) {
				wfree(menu->entries[idx]->text);
				menu->entries[idx]->text = wstrdup(buf);
				wMenuRealize(menu);
			}
		}
	}
	menu = scr->workspace_menu;
	if (menu) {
		idx = workspace + MC_WORKSPACE1(menu);
		if (idx < menu->entry_no) {
			if (strcmp(menu->entries[idx]->text, buf) != 0) {
				wfree(menu->entries[idx]->text);
				menu->entries[idx]->text = wstrdup(buf);
				wMenuRealize(menu);
			}
		}
	}

	if (scr->clip_icon)
		wClipIconPaint(scr->clip_icon);

	WMPostNotificationName(WMNWorkspaceNameChanged, scr, (void *)(uintptr_t) workspace);
}

/* callback for when menu entry is edited */
static void onMenuEntryEdited(WMenu * menu, WMenuEntry * entry)
{
	char *tmp;

	tmp = entry->text;
	wWorkspaceRename(menu->frame->screen_ptr, (long)entry->clientdata, tmp);
}

static void cleanupLayoutMenu(WMenu *menu)
{
	if (menu->frame->screen_ptr->layout_menu == menu)
		menu->frame->screen_ptr->layout_menu = NULL;
}

static void setLayoutCommand(WMenu *menu, WMenuEntry *entry)
{
	WScreen *scr = menu->frame->screen_ptr;

	wLayoutSetWorkspace(scr, scr->current_workspace,
			    (WLayout)(uintptr_t) entry->clientdata);
}

/* Mark the radio entry matching the current workspace's layout. */
static void updateLayoutMenu(WScreen *scr)
{
	WMenu *menu = scr->layout_menu;
	int i, current;

	if (!menu)
		return;

	current = scr->workspaces[scr->current_workspace]->layout;
	for (i = 0; i < WLAYOUT_COUNT; i++)
		menu->entries[i]->flags.indicator_on = (i == current);
	wMenuPaint(menu);
}

/* Layout changes also arrive from key bindings and IPC, so react to the
 * notification instead of every call site. */
static void obs_workspace_layout(void *self, WMNotification *notif)
{
	(void)self;
	updateLayoutMenu(WMGetNotificationObject(notif));
}

static void addLayoutMenu(WScreen *scr, WMenu *wsmenu)
{
	static int sentinel;
	static bool observer_added;
	WMenu *menu;
	WMenuEntry *entry;
	int i;

	menu = wMenuCreate(scr, _("Layout"), false);
	if (!menu) {
		wwarning(_("could not create Layout menu"));
		return;
	}
	menu->on_destroy = cleanupLayoutMenu;

	for (i = 0; i < WLAYOUT_COUNT; i++) {
		entry = wMenuAddCallback(menu, _(wLayoutDisplayName(i)),
					 setLayoutCommand,
					 (void *)(uintptr_t) i);
		entry->flags.indicator = 1;
		entry->flags.indicator_type = MI_DIAMOND;
	}
	menu->flags.realized = 0;
	wMenuRealize(menu);

	entry = wMenuAddCallback(wsmenu, _("Layout"), NULL, NULL);
	wMenuEntrySetCascade(wsmenu, entry, menu);
	scr->layout_menu = menu;

	if (!observer_added) {
		observer_added = true;
		WMAddNotificationObserver(obs_workspace_layout, &sentinel,
					  WMNWorkspaceLayoutChanged, NULL);
	}
}

WMenu *wWorkspaceMenuMake(WScreen * scr, bool titled)
{
	WMenu *wsmenu;
	WMenuEntry *entry;

	wsmenu = wMenuCreate(scr, titled ? _("Workspaces") : NULL, false);
	if (!wsmenu) {
		wwarning(_("could not create Workspace menu"));
		return NULL;
	}

	/* callback to be called when an entry is edited */
	wsmenu->on_edit = onMenuEntryEdited;

	wMenuAddCallback(wsmenu, _("New"), newWSCommand, NULL);
	wMenuAddCallback(wsmenu, _("Destroy Last"), deleteWSCommand, NULL);

	entry = wMenuAddCallback(wsmenu, _("Last Used"), lastWSCommand, NULL);
	entry->rtext = GetShortcutKey(wKeyBindings[WKBD_LASTWORKSPACE]);

	if (titled)
		addLayoutMenu(scr, wsmenu);

	/* first workspace entry follows the fixed items and, on the
	 * titled menu, the Layout cascade */
	wsmenu->ws_entry_base = wsmenu->entry_no;
	wsmenu->brother->ws_entry_base = wsmenu->entry_no;

	return wsmenu;
}

void wWorkspaceMenuUpdate(WScreen * scr, WMenu * menu)
{
	int i;
	long ws;
	char title[MAX_WORKSPACENAME_WIDTH + 1];
	WMenuEntry *entry;
	int tmp;

	if (!menu)
		return;

	if (menu->entry_no < scr->workspace_count + MC_WORKSPACE1(menu)) {
		/* new workspace(s) added */
		i = scr->workspace_count - (menu->entry_no - MC_WORKSPACE1(menu));
		ws = menu->entry_no - MC_WORKSPACE1(menu);
		while (i > 0) {
			wstrlcpy(title, scr->workspaces[ws]->name, MAX_WORKSPACENAME_WIDTH);

			entry = wMenuAddCallback(menu, title, switchWSCommand, (void *)ws);
			entry->flags.indicator = 1;
			entry->flags.indicator_type = MI_DIAMOND;
			entry->flags.editable = 1;

			i--;
			ws++;
		}
	} else if (menu->entry_no > scr->workspace_count + MC_WORKSPACE1(menu)) {
		/* removed workspace(s) */
		for (i = menu->entry_no - 1; i >= scr->workspace_count + MC_WORKSPACE1(menu); i--)
			wMenuRemoveItem(menu, i);
	}

	for (i = 0; i < scr->workspace_count; i++) {
		/* workspace shortcut labels */
		if (i / 10 == scr->current_workspace / 10)
			wMenuEntrySetShortcutText(menu->entries[i + MC_WORKSPACE1(menu)],
						  GetShortcutKey(wKeyBindings[WKBD_WORKSPACE1 + (i % 10)]));
		else
			wMenuEntrySetShortcutText(menu->entries[i + MC_WORKSPACE1(menu)], NULL);

		menu->entries[i + MC_WORKSPACE1(menu)]->flags.indicator_on = 0;
	}
	menu->entries[scr->current_workspace + MC_WORKSPACE1(menu)]->flags.indicator_on = 1;
	wMenuRealize(menu);

	/* don't let user destroy current workspace */
	if (scr->current_workspace == scr->workspace_count - 1)
		wMenuSetEnabled(menu, MC_DESTROY_LAST, false);
	else
		wMenuSetEnabled(menu, MC_DESTROY_LAST, true);

	/* back to last workspace */
	if (scr->workspace_count && scr->last_workspace != scr->current_workspace)
		wMenuSetEnabled(menu, MC_LAST_USED, true);
	else
		wMenuSetEnabled(menu, MC_LAST_USED, false);

	tmp = menu->frame->top_width + 5;
	/* if menu got unreachable, bring it to a visible place */
	if (menu->frame_x < tmp - (int)menu->frame->core->width)
		wMenuMove(menu, tmp - (int)menu->frame->core->width, menu->frame_y, false);

	updateLayoutMenu(scr);
	wMenuPaint(menu);
}

void wWorkspaceSaveState(WScreen * scr, WMPropList * old_state)
{
	WMPropList *parr, *pstr, *wks_state, *old_wks_state, *foo, *bar;
	char buf[32];
	int i;

	make_keys();

	old_wks_state = WMGetFromPLDictionary(old_state, dWorkspaces);
	parr = WMCreatePLArray(NULL);
	for (i = 0; i < scr->workspace_count; i++) {
		pstr = WMCreatePLString(scr->workspaces[i]->name);
		wks_state = WMCreatePLDictionary(dName, pstr, NULL);
		WMReleasePropList(pstr);
		pstr = WMCreatePLString(wLayoutName(scr->workspaces[i]->layout));
		WMPutInPLDictionary(wks_state, dLayout, pstr);
		WMReleasePropList(pstr);
		if (scr->workspaces[i]->layout != WLAYOUT_FLOAT) {
			snprintf(buf, sizeof(buf), "%d", scr->workspaces[i]->master_count);
			pstr = WMCreatePLString(buf);
			WMPutInPLDictionary(wks_state, dMasterCount, pstr);
			WMReleasePropList(pstr);
			snprintf(buf, sizeof(buf), "%.2f",
				 (double)scr->workspaces[i]->master_ratio);
			pstr = WMCreatePLString(buf);
			WMPutInPLDictionary(wks_state, dMasterRatio, pstr);
			WMReleasePropList(pstr);
		}
		if (!wdisp->flags.noclip) {
			pstr = wClipSaveWorkspaceState(scr, i);
			WMPutInPLDictionary(wks_state, dClip, pstr);
			WMReleasePropList(pstr);
		} else if (old_wks_state != NULL) {
			foo = WMGetFromPLArray(old_wks_state, i);
			if (foo != NULL) {
				bar = WMGetFromPLDictionary(foo, dClip);
				if (bar != NULL)
					WMPutInPLDictionary(wks_state, dClip, bar);
			}
		}
#ifdef VIRTUAL_DESKTOP
		/* Persist per-workspace viewport state. A tiled workspace
		 * never panned, so its view is (0,0) and extent is
		 * screen-sized; still saved so a later float toggle restores
		 * a sane camera origin. The per-head guard is checked at pan
		 * time, not here. */
		snprintf(buf, sizeof(buf), "%d", scr->workspaces[i]->vdesk_x);
		pstr = WMCreatePLString(buf);
		WMPutInPLDictionary(wks_state, dVdeskX, pstr);
		WMReleasePropList(pstr);
		snprintf(buf, sizeof(buf), "%d", scr->workspaces[i]->vdesk_y);
		pstr = WMCreatePLString(buf);
		WMPutInPLDictionary(wks_state, dVdeskY, pstr);
		WMReleasePropList(pstr);
		snprintf(buf, sizeof(buf), "%d", scr->workspaces[i]->vdesk_w);
		pstr = WMCreatePLString(buf);
		WMPutInPLDictionary(wks_state, dVdeskW, pstr);
		WMReleasePropList(pstr);
		snprintf(buf, sizeof(buf), "%d", scr->workspaces[i]->vdesk_h);
		pstr = WMCreatePLString(buf);
		WMPutInPLDictionary(wks_state, dVdeskH, pstr);
		WMReleasePropList(pstr);
		snprintf(buf, sizeof(buf), "%d", scr->workspaces[i]->view_x);
		pstr = WMCreatePLString(buf);
		WMPutInPLDictionary(wks_state, dViewX, pstr);
		WMReleasePropList(pstr);
		snprintf(buf, sizeof(buf), "%d", scr->workspaces[i]->view_y);
		pstr = WMCreatePLString(buf);
		WMPutInPLDictionary(wks_state, dViewY, pstr);
		WMReleasePropList(pstr);
#endif
		WMAddToPLArray(parr, wks_state);
		WMReleasePropList(wks_state);
	}
	WMPutInPLDictionary(scr->session_state, dWorkspaces, parr);
	WMReleasePropList(parr);
}

/* parse an integer slot strictly, then clamp it to [min, max] */
static bool restore_int(WMPropList *pl, long min, long max, int *out)
{
	char *s = WMGetFromPLString(pl);
	char *end;
	long v;

	if (!s || *s == '\0')
		return false;

	errno = 0;
	v = strtol(s, &end, 10);
	if (end == s || *end != '\0' || errno == ERANGE)
		return false;

	if (v < min)
		v = min;
	if (v > max)
		v = max;
	*out = (int)v;
	return true;
}

void wWorkspaceRestoreState(WScreen *scr)
{
	WMPropList *parr, *pstr, *wks_state, *clip_state;
	double ratio;
	char *end;
	int i, j;
	int ws_menu_base;

	make_keys();

	if (scr->session_state == NULL)
		return;

	parr = WMGetFromPLDictionary(scr->session_state, dWorkspaces);

	if (!parr)
		return;

	for (i = 0; i < WMIN(WMGetPropListItemCount(parr), MAX_WORKSPACES); i++) {
		wks_state = WMGetFromPLArray(parr, i);
		if (WMIsPLDictionary(wks_state))
			pstr = WMGetFromPLDictionary(wks_state, dName);
		else
			pstr = wks_state;

		if (i >= scr->workspace_count)
			wWorkspaceNew(scr);

		if (scr->workspace_menu) {
			ws_menu_base = MC_WORKSPACE1(scr->workspace_menu);

			wfree(scr->workspace_menu->entries[i + ws_menu_base]->text);
			scr->workspace_menu->entries[i + ws_menu_base]->text =
				wstrndup(WMGetFromPLString(pstr), MAX_WORKSPACENAME_WIDTH);
			scr->workspace_menu->flags.realized = 0;
		}

		wfree(scr->workspaces[i]->name);
		scr->workspaces[i]->name = wstrndup(WMGetFromPLString(pstr), MAX_WORKSPACENAME_WIDTH);

		if (WMIsPLDictionary(wks_state)) {
			pstr = WMGetFromPLDictionary(wks_state, dLayout);
			if (pstr && WMIsPLString(pstr))
				scr->workspaces[i]->layout =
					wLayoutFromName(WMGetFromPLString(pstr));
			pstr = WMGetFromPLDictionary(wks_state, dMasterCount);
			if (pstr && WMIsPLString(pstr)) {
				int master_count;

				if (restore_int(pstr, 0, RESTORE_MASTER_COUNT_MAX,
						&master_count))
					scr->workspaces[i]->master_count =
						(short)master_count;
			}
			pstr = WMGetFromPLDictionary(wks_state, dMasterRatio);
			if (pstr && WMIsPLString(pstr)) {
				ratio = wstrtod(WMGetFromPLString(pstr), &end);
				if (end != WMGetFromPLString(pstr)
				    && *end == '\0' && isfinite(ratio)
				    && ratio >= -(double)FLT_MAX
				    && ratio <= (double)FLT_MAX)
					scr->workspaces[i]->master_ratio =
						(float)ratio;
			}
#ifdef VIRTUAL_DESKTOP
			/* Restore per-workspace viewport state by direct
			 * field assignment, NOT wVdeskSetViewport: the saved
			 * frame_x/frame_y restored by wSessionRestoreState()
			 * are saved screen positions, so calling
			 * wVdeskSetViewport would shift them by the saved
			 * view delta and break the restore. */
			pstr = WMGetFromPLDictionary(wks_state, dVdeskX);
			if (pstr && WMIsPLString(pstr)) {
				int v;

				if (restore_int(pstr, -RESTORE_COORD_LIMIT,
						RESTORE_COORD_LIMIT, &v))
					scr->workspaces[i]->vdesk_x = v;
			}
			pstr = WMGetFromPLDictionary(wks_state, dVdeskY);
			if (pstr && WMIsPLString(pstr)) {
				int v;

				if (restore_int(pstr, -RESTORE_COORD_LIMIT,
						RESTORE_COORD_LIMIT, &v))
					scr->workspaces[i]->vdesk_y = v;
			}
			pstr = WMGetFromPLDictionary(wks_state, dVdeskW);
			if (pstr && WMIsPLString(pstr)) {
				int v;

				if (restore_int(pstr, 0, RESTORE_COORD_LIMIT, &v))
					scr->workspaces[i]->vdesk_w = v;
			}
			pstr = WMGetFromPLDictionary(wks_state, dVdeskH);
			if (pstr && WMIsPLString(pstr)) {
				int v;

				if (restore_int(pstr, 0, RESTORE_COORD_LIMIT, &v))
					scr->workspaces[i]->vdesk_h = v;
			}
			pstr = WMGetFromPLDictionary(wks_state, dViewX);
			if (pstr && WMIsPLString(pstr)) {
				int v;

				if (restore_int(pstr, -RESTORE_COORD_LIMIT,
						RESTORE_COORD_LIMIT, &v))
					scr->workspaces[i]->view_x = v;
			}
			pstr = WMGetFromPLDictionary(wks_state, dViewY);
			if (pstr && WMIsPLString(pstr)) {
				int v;

				if (restore_int(pstr, -RESTORE_COORD_LIMIT,
						RESTORE_COORD_LIMIT, &v))
					scr->workspaces[i]->view_y = v;
			}
#endif
		}

		if (!wdisp->flags.noclip) {
			int added_omnipresent_icons = 0;

			clip_state = WMGetFromPLDictionary(wks_state, dClip);
			if (scr->workspaces[i]->clip)
				wDockDestroy(scr->workspaces[i]->clip);

			scr->workspaces[i]->clip = wDockRestoreState(scr, clip_state, WM_CLIP);
			if (i > 0)
				wDockHideIcons(scr->workspaces[i]->clip);

			/* We set the global icons here, because scr->workspaces[i]->clip
			 * was not valid in wDockRestoreState().
			 * There we only set icon->omnipresent to know which icons we
			 * need to set here.
			 */
			for (j = 0; j < scr->workspaces[i]->clip->max_icons; j++) {
				WAppIcon *aicon = scr->workspaces[i]->clip->icon_array[j];
				int k;

				if (!aicon || !aicon->omnipresent)
					continue;
				aicon->omnipresent = 0;
				if (wClipMakeIconOmnipresent(aicon, true) != WO_SUCCESS)
					continue;
				if (i == 0)
					continue;

				/* Move this appicon from workspace i to workspace 0 */
				scr->workspaces[i]->clip->icon_array[j] = NULL;
				scr->workspaces[i]->clip->icon_count--;

				added_omnipresent_icons++;
				/* If there are too many omnipresent appicons, we are in trouble */
				assert(scr->workspaces[0]->clip->icon_count + added_omnipresent_icons
				       <= scr->workspaces[0]->clip->max_icons);
				/* Find first free spot on workspace 0 */
				for (k = 0; k < scr->workspaces[0]->clip->max_icons; k++)
					if (scr->workspaces[0]->clip->icon_array[k] == NULL)
						break;
				scr->workspaces[0]->clip->icon_array[k] = aicon;
				aicon->dock = scr->workspaces[0]->clip;
			}
			scr->workspaces[0]->clip->icon_count += added_omnipresent_icons;
		}

		WMPostNotificationName(WMNWorkspaceNameChanged, scr, (void *)(uintptr_t) i);
	}
}

int wWorkspaceFindByName(WScreen *scr, const char *name)
{
	int i, found = -1;

	if (!name)
		return -1;
	for (i = 0; i < scr->workspace_count; i++) {
		if (strcmp(scr->workspaces[i]->name, name) == 0) {
			if (found != -1)
				return -1;
			found = i;
		}
	}
	return found;
}

/* Returns the workspace number for a given workspace name */
int wGetWorkspaceNumber(WScreen *scr, const char *value)
{
	int w, i, found = -1;

	if (!value)
		return -1;
	for (i = 0; i < scr->workspace_count; i++) {
		if (strcmp(scr->workspaces[i]->name, value) == 0) {
			if (found != -1)
				return -1;
			found = i;
		}
	}
	if (found != -1)
		return found;
	if (sscanf(value, "%i", &w) == 1 && w >= 1 && w <= scr->workspace_count)
		return w - 1;

	return -1;
}
