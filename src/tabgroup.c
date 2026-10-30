/*
 *  tabgroup.c
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

/*
 * Window tab-grouping for Window Maker.
 *
 * Multiple managed windows share one on-screen frame slot. Only the
 * active member is mapped, the others stay managed but unmapped at the
 * same screen position. Each member keeps its own WFrameWindow, the
 * active member's titlebar carries an [N/M] prefix until the segmented
 * renderer lands.
 */

#include "wconfig.h"

#include <xkbcommon/xkbcommon-keysyms.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "WindowMaker.h"
#include "GNUstep.h"
#include "screen.h"
#include "window.h"
#include "framewin.h"
#include "wcore.h"
#include "actions.h"
#include "client.h"
#include "main.h"
#include "misc.h"
#include "wxkb.h"
#include "stacking.h"
#include "event.h"
#include "icon.h"
#include "wireframe.h"
#include "tabgroup.h"


static void wGroupUpdateTitlebar(WWindow *wwin);
static void wGroupNotifyState(WWindow *wwin);
static void wGroupRevealMember(WScreen *scr, WWindow *wwin, bool focus);
static void wGroupPlaceDetached(WWindow *wwin, WWindow *anchor);
static void wGroupRemoveMember(WWindow *wwin, bool unmanaging);
static void wGroupDestroy(WWindowGroup *grp, WScreen *scr);


/*
 * Shared reentry guard for every wGroupSync*() helper. The propagators
 * call back into wWindowConfigure(), wWindowMove() and friends which in
 * turn call back into the same propagators, the guard breaks that cycle.
 */
static int syncing_peers = 0;

/*
 * Pending session-restore state. Members of a saved group arrive
 * asynchronously as their commands map a window, or all at once during a
 * warm restart, so each gid keeps an insertion-sorted list of arrived
 * members alongside the live group once two of them exist. Slots are
 * never freed, the list is short and only consulted from
 * wGroupRestoreMember() during startup.
 */
typedef struct WGroupRestore {
	WScreen *scr;
	unsigned gid;
	int active_idx;
	WWindowGroup *grp;
	int count;
	int cap;
	WWindow **win;
	int *saved_idx;
	struct WGroupRestore *next;
} WGroupRestore;

static WGroupRestore *restore_slots = NULL;


/* --------------------------------------------------------------------
 * Predicates
 * -------------------------------------------------------------------- */

bool wGroupIsInactiveMember(const WWindow *wwin)
{
	if (!wwin || !wwin->tab_group)
		return false;
	return wwin->tab_group_index != wwin->tab_group->active_index;
}

/*
 * A window may join a tab group only when it has a normal titlebar,
 * is movable and resizable, sits at WMNormalLevel and is not currently
 * fullscreen, miniaturized or hidden. Fixed-size and borderless
 * windows are refused so members never desync on size.
 */
bool wGroupIsTabable(WWindow *wwin)
{
	if (!wwin || !wwin->frame)
		return false;
	if (!HAS_TITLEBAR(wwin) || !IS_MOVABLE(wwin) || !IS_RESIZABLE(wwin))
		return false;
	if (WINDOW_LEVEL(wwin) != WMNormalLevel)
		return false;
	if (wwin->flags.fullscreen || wwin->flags.miniaturized || wwin->flags.hidden)
		return false;
	if (wwin->flags.internal_window)
		return false;
	if (WFLAGP(wwin, no_tabbing))
		return false;
	return true;
}

/*
 * Return the most recently focused window on the same workspace that
 * payload could be attached to, or NULL when no suitable target exists.
 */
WWindow *wGroupFindAttachTarget(WScreen *scr, WWindow *payload)
{
	WWindow *cand;

	if (!payload)
		return NULL;

	for (cand = payload->prev; cand; cand = cand->prev) {
		if (cand == payload)
			continue;
		if (cand->screen_ptr != scr)
			continue;
		if (cand->frame && !WWIN_ON_WORKSPACE(cand, scr->current_workspace))
			continue;
		if (!wGroupIsTabable(cand))
			continue;
		if (payload->tab_group && cand->tab_group == payload->tab_group)
			continue;
		return cand;
	}
	return NULL;
}

/* --------------------------------------------------------------------
 * Title prefix
 * -------------------------------------------------------------------- */

static void wGroupUpdateTitlebar(WWindow *wwin)
{
	char *xname;
	const char *base;
	char buf[512];

	if (!wwin->frame)
		return;

	xname = NULL;
	if (!wFetchName(wdisp->conn, wwin->client_win, &xname) || !xname)
		base = DEF_WINDOW_TITLE;
	else
		base = xname;

	/*
	 * The segmented renderer reads each member's plain frame->title and
	 * draws them side by side, so the [N/M] prefix is only applied when
	 * TabsInTitlebar is off.
	 */
	if (wwin->tab_group && wwin->tab_group->member_count > 1
	    && !w_global.config->tabs_in_titlebar) {
		snprintf(buf, sizeof(buf), "[%d/%d] %s",
		         wwin->tab_group_index + 1,
		         wwin->tab_group->member_count, base);
		wFrameWindowChangeTitle(wwin->frame, buf);
	} else if (!wFrameWindowChangeTitle(wwin->frame, base)
	           && wwin->frame->tab_seg_count > 0) {
		/* segmented mode keeps frame->title at the plain X title,
		 * so on detach or dissolve ChangeTitle short-circuits on
		 * the unchanged string and the inactive-segment overlay
		 * stays on the surface, force the clear+repaint here */
		wFrameWindowRepaintTitle(wwin->frame);
	}

	if (xname)
		wfree(xname);
}

void wGroupRefreshTitles(WWindowGroup *grp)
{
	WWindow *active;
	int i;

	if (!grp)
		return;
	for (i = 0; i < grp->member_count; i++)
		wGroupUpdateTitlebar(grp->members[i]);

	/*
	 * Each member's wFrameWindowChangeTitle() repainted its own frame,
	 * but only the active member is mapped and its segmented titlebar
	 * shows every member's title. An inactive member whose title did
	 * not change short-circuits without repainting, so do it once
	 * explicitly here.
	 */
	if (grp->active_index >= 0 && grp->active_index < grp->member_count) {
		active = grp->members[grp->active_index];
		if (active && active->frame)
			wFrameWindowRepaintTitle(active->frame);
	}
}

/* --------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------- */

static void wGroupNotifyState(WWindow *wwin)
{
	/* let wmspec.c rebuild _NET_WM_STATE so HIDDEN is toggled */
	WMPostNotificationName(WMNChangedState, wwin, "tabgroup");
}

static void wGroupRevealMember(WScreen *scr, WWindow *wwin, bool focus)
{
	if (!wwin->frame)
		return;
	/* the whole group may be miniaturized or hidden, in that case the
	 * newly active member stays unmapped and only the membership state
	 * is published, the existing miniwindow or app icon brings it back */
	if (!wwin->flags.miniaturized && !wwin->flags.hidden) {
		xcb_map_window(wdisp->conn, wwin->frame->core->window);
		if (!wwin->flags.shaded) {
			xcb_map_window(wdisp->conn, wwin->client_win);
			wwin->flags.mapped = 1;
		}
		if (focus)
			wSetFocusTo(scr, wwin);
	}
	wGroupNotifyState(wwin);
}

/*
 * Place a freshly detached window next to the group's current spot.
 * The window keeps the group's size and is offset by one titlebar height
 * so it is visibly separate from the still-grouped frame.
 */
static void wGroupPlaceDetached(WWindow *wwin, WWindow *anchor)
{
	int off;

	if (!wwin->frame)
		return;

	off = wwin->frame->top_width > 0 ? (int)wwin->frame->top_width : 22;
	if (anchor && anchor->frame)
		wWindowMove(wwin, anchor->frame_x + off, anchor->frame_y + off);
	else
		wWindowMove(wwin, wwin->frame_x + off, wwin->frame_y + off);
}

static void wGroupDestroy(WWindowGroup *grp, WScreen *scr)
{
	WWindowGroup **p;
	WGroupRestore *rs;

	for (p = &scr->window_groups; *p && *p != grp; p = &(*p)->next)
		;
	if (*p)
		*p = grp->next;

	/* a pending restore slot may still point at this group when its
	 * members are closed before every saved peer has arrived */
	for (rs = restore_slots; rs; rs = rs->next)
		if (rs->grp == grp)
			rs->grp = NULL;

	wfree(grp->members);
	wfree(grp);
}

/* --------------------------------------------------------------------
 * Lifecycle
 * -------------------------------------------------------------------- */

WWindowGroup *wGroupCreate(WScreen *scr)
{
	WWindowGroup *grp;

	grp = wmalloc(sizeof(*grp));
	grp->members = NULL;
	grp->member_count = 0;
	grp->capacity = 0;
	grp->active_index = -1;
	grp->next = scr->window_groups;
	scr->window_groups = grp;
	return grp;
}

void wGroupAttach(WWindowGroup *grp, WWindow *wwin)
{
	WWindow *active;
	int idx;

	if (!grp || !wwin || !wwin->frame)
		return;
	if (wwin->tab_group == grp)
		return;
	if (wwin->tab_group)
		wGroupDetach(wwin);

	if (grp->member_count >= grp->capacity) {
		grp->capacity = grp->capacity ? grp->capacity * 2 : 4;
		grp->members = wrealloc(grp->members,
		                        sizeof(WWindow *) * (size_t)grp->capacity);
	}

	idx = grp->member_count++;
	grp->members[idx] = wwin;
	wwin->tab_group = grp;
	wwin->tab_group_index = idx;

	if (grp->active_index < 0) {
		grp->active_index = 0;
	} else {
		active = grp->members[grp->active_index];
		syncing_peers = 1;
		if (IS_RESIZABLE(wwin))
			wWindowConfigure(wwin, active->frame_x, active->frame_y,
			                 (int)active->client.width,
			                 (int)active->client.height);
		else
			wWindowMove(wwin, active->frame_x, active->frame_y);
		syncing_peers = 0;
		/* hand focus to the visible member before hiding the
		 * joining window so X does not revert focus to the root
		 * and pagers do not see FOCUSED on a HIDDEN client */
		if (wwin->flags.focused)
			wSetFocusTo(wwin->screen_ptr, active);
		/* only the active member may own a miniwindow, a session
		 * restore of an iconified group manages and iconifies each
		 * member independently before reattaching them */
		if (wwin->icon) {
			RemoveFromStackList(wwin->icon->core);
			wIconDestroy(wwin->icon);
			wwin->icon = NULL;
		}
		xcb_unmap_window(wdisp->conn, wwin->frame->core->window);
		wwin->flags.mapped = 0;
		wGroupNotifyState(wwin);
	}

	wGroupRefreshTitles(grp);
}

/*
 * Internal removal. When unmanaging is True the leaving window is being
 * destroyed by wUnmanageWindow(), so it must not be touched beyond
 * clearing its membership pointer.
 */
static void wGroupRemoveMember(WWindow *wwin, bool unmanaging)
{
	WWindowGroup *grp;
	WScreen *scr;
	WWindow *survivor;
	WWindow *anchor;
	int idx, i;
	bool was_active;

	grp = wwin->tab_group;
	if (!grp)
		return;

	scr = wwin->screen_ptr;
	idx = wwin->tab_group_index;
	was_active = (idx == grp->active_index);

	for (i = idx; i < grp->member_count - 1; i++) {
		grp->members[i] = grp->members[i + 1];
		grp->members[i]->tab_group_index = i;
	}
	grp->member_count--;

	wwin->tab_group = NULL;
	wwin->tab_group_index = -1;

	if (!unmanaging) {
		wGroupUpdateTitlebar(wwin);
		wGroupNotifyState(wwin);
	}

	if (grp->member_count == 0) {
		wGroupDestroy(grp, scr);
		return;
	}

	if (grp->member_count == 1) {
		/*
		 * Only one member left, dissolve the group. The survivor
		 * keeps its current geometry, only the title prefix is
		 * removed.
		 */
		survivor = grp->members[0];
		survivor->tab_group = NULL;
		survivor->tab_group_index = -1;

		if (was_active)
			wGroupRevealMember(scr, survivor, unmanaging);
		wGroupUpdateTitlebar(survivor);
		wGroupNotifyState(survivor);

		if (!unmanaging) {
			wGroupPlaceDetached(wwin, survivor);
			if (!wwin->flags.mapped && !wwin->flags.miniaturized && !wwin->flags.hidden)
				wGroupRevealMember(scr, wwin, false);
		}

		wGroupDestroy(grp, scr);
		return;
	}

	if (was_active) {
		if (grp->active_index >= grp->member_count)
			grp->active_index = grp->member_count - 1;
		anchor = grp->members[grp->active_index];
		wGroupRevealMember(scr, anchor, unmanaging);
	} else {
		if (idx < grp->active_index)
			grp->active_index--;
		anchor = grp->members[grp->active_index];
	}

	wGroupRefreshTitles(grp);

	if (!unmanaging) {
		wGroupPlaceDetached(wwin, anchor);
		if (!wwin->flags.mapped && !wwin->flags.miniaturized && !wwin->flags.hidden)
			wGroupRevealMember(scr, wwin, false);
	}
}

void wGroupDetach(WWindow *wwin)
{
	wGroupRemoveMember(wwin, false);
}

void wGroupUnmanageMember(WWindow *wwin, bool destroyed)
{
	(void) destroyed;
	wGroupRemoveMember(wwin, true);
}

void wGroupSetActive(WWindowGroup *grp, int idx)
{
	WWindow *cur, *nxt;
	WScreen *scr;
	int x, y;

	if (!grp || idx < 0 || idx >= grp->member_count)
		return;
	if (grp->active_index < 0 || grp->active_index >= grp->member_count)
		return;
	if (idx == grp->active_index)
		return;

	cur = grp->members[grp->active_index];
	nxt = grp->members[idx];
	if (!cur || !nxt || !cur->frame || !nxt->frame)
		return;

	scr = cur->screen_ptr;
	x = cur->frame_x;
	y = cur->frame_y;

	/*
	 * The whole group may be miniaturized or hidden by an application
	 * hide. Both members are already unmapped in that case, so only
	 * the active index moves and the miniwindow, if any, is handed to
	 * the new active member so a later wDeiconifyWindow() finds it.
	 */
	if (cur->flags.miniaturized || cur->flags.hidden) {
		grp->active_index = idx;
		if (cur->icon && !nxt->icon) {
			nxt->icon = cur->icon;
			nxt->icon->owner = nxt;
			nxt->icon_x = cur->icon_x;
			nxt->icon_y = cur->icon_y;
			nxt->flags.icon_moved = cur->flags.icon_moved;
			cur->icon = NULL;
			wIconChangeTitle(nxt->icon, nxt);
			wIconPaint(nxt->icon);
		}
		wGroupNotifyState(cur);
		wGroupNotifyState(nxt);
		return;
	}

	wXGrabServer(wdisp->conn);

	/*
	 * Map and focus the incoming frame before unmapping the outgoing
	 * one so X does not revert focus to the root in between.
	 */
	wWindowMove(nxt, x, y);
	xcb_map_window(wdisp->conn, nxt->frame->core->window);
	if (!nxt->flags.shaded)
		xcb_map_window(wdisp->conn, nxt->client_win);
	nxt->flags.mapped = 1;

	/*
	 * Restack the incoming frame directly above the outgoing one so the
	 * group keeps its position in the stacking order rather than rising
	 * to the top of the level.
	 */
	MoveInStackListAbove(cur->frame->core, nxt->frame->core);

	grp->active_index = idx;
	wSetFocusTo(scr, nxt);

	xcb_unmap_window(wdisp->conn, cur->frame->core->window);
	cur->flags.mapped = 0;

	wXUngrabServer(wdisp->conn);

	wGroupNotifyState(cur);
	wGroupNotifyState(nxt);
}

/* --------------------------------------------------------------------
 * Size synchronisation
 * -------------------------------------------------------------------- */

void wGroupSyncSize(WWindow *source)
{
	WWindowGroup *grp;
	WWindow *peer;
	int width, height, x, y, i;

	grp = source->tab_group;
	if (!grp || syncing_peers)
		return;

	width = (int)source->client.width;
	height = (int)source->client.height;
	x = source->frame_x;
	y = source->frame_y;

	syncing_peers = 1;
	for (i = 0; i < grp->member_count; i++) {
		peer = grp->members[i];
		if (peer == source)
			continue;
		if (!IS_RESIZABLE(peer))
			continue;
		if ((int)peer->client.width != width || (int)peer->client.height != height
		    || peer->frame_x != x || peer->frame_y != y)
			wWindowConfigure(peer, x, y, width, height);
	}
	syncing_peers = 0;
}

void wGroupSyncPosition(WWindow *source)
{
	WWindowGroup *grp;
	WWindow *peer;
	int x, y, i;

	grp = source->tab_group;
	if (!grp || syncing_peers)
		return;

	x = source->frame_x;
	y = source->frame_y;

	syncing_peers = 1;
	for (i = 0; i < grp->member_count; i++) {
		peer = grp->members[i];
		if (peer == source)
			continue;
		if (peer->frame_x != x || peer->frame_y != y)
			wWindowMove(peer, x, y);
	}
	syncing_peers = 0;
}

void wGroupSyncWorkspace(WWindow *source)
{
	WWindowGroup *grp;
	WWindow *peer;
	uint64_t mask;
	int workspace, old, i;

	grp = source->tab_group;
	if (!grp || syncing_peers || !source->frame)
		return;

	workspace = WWIN_PRIMARY_WS(source);
	mask = WWIN_WS_MASK(source);

	syncing_peers = 1;
	for (i = 0; i < grp->member_count; i++) {
		peer = grp->members[i];
		if (peer == source || !peer->frame)
			continue;
		if (WWIN_PRIMARY_WS(peer) == workspace
		    && WWIN_WS_MASK(peer) == mask)
			continue;
		old = WWIN_PRIMARY_WS(peer);
		peer->frame->workspace = workspace;
		peer->frame->ws_mask = mask;
		peer->last_head = source->last_head;
		if (old != workspace)
			WMPostNotificationName(WMNChangedWorkspace, peer,
			                       (void *)(uintptr_t)old);
	}
	syncing_peers = 0;
}

/*
 * Mirror the active member's shaded state onto every inactive peer.
 * The peer frame and client are physically resized and unmapped so a
 * later wGroupSetActive() shows a correctly rolled-up titlebar rather
 * than a full frame whose flag merely says shaded.
 */
void wGroupSyncShade(WWindow *source)
{
	WWindowGroup *grp;
	WWindow *peer;
	int i;
	bool shaded;

	grp = source->tab_group;
	if (!grp || syncing_peers)
		return;

	shaded = source->flags.shaded;

	syncing_peers = 1;
	for (i = 0; i < grp->member_count; i++) {
		peer = grp->members[i];
		if (peer == source || !peer->frame)
			continue;
		if (peer->flags.shaded == shaded)
			continue;

		peer->flags.shaded = shaded;
		if (shaded) {
			peer->flags.mapped = 0;
			xcb_change_window_attributes(wdisp->conn, peer->client_win, XCB_CW_EVENT_MASK, (const uint32_t[]){ peer->event_mask & ~XCB_EVENT_MASK_STRUCTURE_NOTIFY });
			xcb_unmap_window(wdisp->conn, peer->client_win);
			xcb_change_window_attributes(wdisp->conn, peer->client_win, XCB_CW_EVENT_MASK, (const uint32_t[]){ peer->event_mask });
			wFrameWindowResize(peer->frame, peer->frame->core->width,
			                   peer->frame->top_width - 1);
			peer->client.y = peer->frame_y - (int)peer->client.height
			                 + (int)peer->frame->top_width;
		} else {
			wFrameWindowResize(peer->frame, peer->frame->core->width,
			                   peer->frame->top_width
			                   + (int)peer->client.height
			                   + peer->frame->bottom_width);
			peer->client.y = peer->frame_y + (int)peer->frame->top_width;
			xcb_map_window(wdisp->conn, peer->client_win);
		}
		wWindowSynthConfigureNotify(peer);
		WMPostNotificationName(WMNChangedState, peer, "shade");
	}
	syncing_peers = 0;
}

/*
 * Mirror miniaturized state. Only the active member ever creates or
 * owns a WIcon, peers just follow flags.miniaturized and WM_STATE so
 * pagers and the switch menu show one minimized unit.
 */
void wGroupSyncMiniaturize(WWindow *source)
{
	WWindowGroup *grp;
	WWindow *peer;
	int i;
	bool mini;

	grp = source->tab_group;
	if (!grp || syncing_peers)
		return;

	mini = source->flags.miniaturized;

	syncing_peers = 1;
	for (i = 0; i < grp->member_count; i++) {
		peer = grp->members[i];
		if (peer == source)
			continue;
		if (peer->flags.miniaturized == mini)
			continue;

		peer->flags.miniaturized = mini;
		peer->flags.mapped = 0;
		wClientSetState(peer, mini ? XCB_ICCCM_WM_STATE_ICONIC : XCB_ICCCM_WM_STATE_NORMAL, XCB_NONE);
		WMPostNotificationName(WMNChangedState, peer, "iconify");
	}
	syncing_peers = 0;
}

/*
 * Mirror maximize bookkeeping. Geometry itself is already propagated
 * through wWindowConfigure() into wGroupSyncSize(), this only copies
 * the maximize direction bitmask and the saved pre-maximize geometry
 * so a later unmaximize on any member restores to the same place.
 */
void wGroupSyncMaximize(WWindow *source)
{
	WWindowGroup *grp;
	WWindow *peer;
	int i;

	grp = source->tab_group;
	if (!grp || syncing_peers)
		return;

	syncing_peers = 1;
	for (i = 0; i < grp->member_count; i++) {
		peer = grp->members[i];
		if (peer == source)
			continue;
		peer->flags.maximized = source->flags.maximized;
		peer->flags.old_maximized = source->flags.old_maximized;
		peer->old_geometry = source->old_geometry;
		peer->maximus_x = source->maximus_x;
		peer->maximus_y = source->maximus_y;
		if (peer->frame)
			wWindowUpdateButtonImages(peer);
		WMPostNotificationName(WMNChangedState, peer, "maximize");
	}
	syncing_peers = 0;
}

/* --------------------------------------------------------------------
 * Close all members
 * -------------------------------------------------------------------- */

void wGroupCloseAll(WWindowGroup *grp)
{
	WWindow **snap;
	WWindow *w;
	int n, i;

	if (!grp || grp->member_count <= 0)
		return;

	/*
	 * Snapshot the member list first. Each close triggers an
	 * asynchronous unmanage that mutates grp->members[] as the clients
	 * actually go away.
	 */
	n = grp->member_count;
	snap = wmalloc(sizeof(WWindow *) * (size_t)n);
	memcpy(snap, grp->members, sizeof(WWindow *) * (size_t)n);

	for (i = 0; i < n; i++) {
		w = snap[i];
		if (!w)
			continue;
		if (w->protocols.DELETE_WINDOW)
			wClientSendProtocol(w, wdisp->atom.wm.delete_window,
					wdisp->timestamp.last_event);
		else
			wClientKill(w);
	}

	wfree(snap);
}

void wGroupCloseMember(WWindowGroup *grp, int idx)
{
	WWindow *w;

	if (!grp || idx < 0 || idx >= grp->member_count)
		return;
	w = grp->members[idx];
	if (!w)
		return;
	if (w->protocols.DELETE_WINDOW)
		wClientSendProtocol(w, wdisp->atom.wm.delete_window,
				    wdisp->timestamp.last_event);
	else
		wClientKill(w);
}

/* --------------------------------------------------------------------
 * Keyboard handlers
 * -------------------------------------------------------------------- */

void wGroupTabNext(WScreen *scr)
{
	WWindow *wwin;
	WWindowGroup *grp;
	int next;

	wwin = scr->focused_window;
	if (!wwin || !wwin->tab_group)
		return;
	grp = wwin->tab_group;
	next = (grp->active_index + 1) % grp->member_count;
	wGroupSetActive(grp, next);
}

void wGroupTabPrev(WScreen *scr)
{
	WWindow *wwin;
	WWindowGroup *grp;
	int prev;

	wwin = scr->focused_window;
	if (!wwin || !wwin->tab_group)
		return;
	grp = wwin->tab_group;
	prev = (grp->active_index - 1 + grp->member_count) % grp->member_count;
	wGroupSetActive(grp, prev);
}

void wGroupTabAttach(WScreen *scr)
{
	WWindow *wwin, *target;
	WWindowGroup *grp;

	wwin = scr->focused_window;
	if (!wwin || !wGroupIsTabable(wwin))
		return;

	target = wGroupFindAttachTarget(scr, wwin);
	if (!target)
		return;

	grp = target->tab_group;
	if (!grp) {
		grp = wGroupCreate(scr);
		wGroupAttach(grp, target);
	}
	wGroupAttach(grp, wwin);
}

void wGroupTabDetach(WScreen *scr)
{
	WWindow *wwin;

	wwin = scr->focused_window;
	if (!wwin || !wwin->tab_group)
		return;
	wGroupDetach(wwin);
}

/* --------------------------------------------------------------------
 * Pointer-pick attach
 * -------------------------------------------------------------------- */

void wGroupPickAndAttach(WScreen *scr, WWindow *payload)
{
	WWindow *target;
	WWindowGroup *grp;
	xcb_generic_event_t *ev = NULL;
	xkb_keysym_t ks;
	xcb_window_t root, child;
	int rx, ry, wx, wy;
	unsigned int mask;
	bool done, cancelled;

	if (!payload || !wGroupIsTabable(payload))
		return;

	if (wXGrabPointer(wdisp->conn, scr->root_win, false,
	                 XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE,
	                 XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, scr->root_win,
			wdisp->cursor[WCUR_SELECT], XCB_CURRENT_TIME) != WXGrabSuccess)
		return;
	wXGrabKeyboard(wdisp->conn, scr->root_win, false, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_CURRENT_TIME);

	/* latch the payload so wUnmanageWindow() can knock it out if the
	 * client exits while the pick loop is waiting on the user, the
	 * SubstructureNotifyMask pull below lets DestroyNotify and
	 * UnmapNotify reach DispatchEvent() during the grab */
	scr->tab_pick_payload = payload;

	target = NULL;
	done = false;
	cancelled = false;

	while (!done) {
		free(ev);
		ev = NULL;
		WMMaskEvent(wdisp->conn, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_KEY_PRESS
		            | XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY, &ev);

		if (!ev)
			break;

		if (scr->tab_pick_payload != payload) {
			cancelled = true;
			break;
		}

		switch ((ev->response_type & 0x7f)) {
		case XCB_BUTTON_PRESS:
			if ((((xcb_button_press_event_t *)ev)->detail) > XCB_BUTTON_INDEX_3) {
				cancelled = true;
				done = true;
				break;
			}
			child = (((xcb_button_press_event_t *)ev)->child);
			if (child == XCB_NONE)
				wXQueryPointer(wdisp->conn, scr->root_win, &root, &child,
				              &rx, &ry, &wx, &wy, &mask);
			if (child == XCB_NONE || child == scr->root_win) {
				cancelled = true;
				done = true;
				break;
			}
			target = wWindowFor(child);
			done = true;
			break;
		case XCB_KEY_PRESS:
			ks = wXkbKeycodeToKeysym((((xcb_key_press_event_t *)ev)->detail), 0);
			if (ks == XKB_KEY_Escape) {
				cancelled = true;
				done = true;
			}
			break;
		default:
			/* Expose and the SubstructureNotify family, the latter
			 * so a payload or target unmanage is processed now
			 * rather than queued behind the grab */
			WMHandleEvent(ev);
			if (scr->tab_pick_payload != payload) {
				cancelled = true;
				done = true;
			}
			break;
		}
	}

	/* the final iteration's event is still owned here, including the
	 * payload-mismatch break path */
	free(ev);
	ev = NULL;

	scr->tab_pick_payload = NULL;

	wXUngrabKeyboard(wdisp->conn, XCB_CURRENT_TIME);
	wXUngrabPointer(wdisp->conn, XCB_CURRENT_TIME);
	wXSync(wdisp->conn);

	if (cancelled || !target || target == payload)
		return;
	if (target->screen_ptr != scr)
		return;
	if (!wGroupIsTabable(target))
		return;
	if (payload->tab_group && payload->tab_group == target->tab_group)
		return;

	grp = target->tab_group;
	if (!grp) {
		grp = wGroupCreate(scr);
		wGroupAttach(grp, target);
	}
	wGroupAttach(grp, payload);
}

/* --------------------------------------------------------------------
 * Reorder within a group
 * -------------------------------------------------------------------- */

void wGroupReorder(WWindowGroup *grp, WWindow *payload, int to_idx)
{
	WWindow *active;
	int from, i;

	if (!grp || !payload || payload->tab_group != grp)
		return;
	if (to_idx < 0)
		to_idx = 0;
	if (to_idx >= grp->member_count)
		to_idx = grp->member_count - 1;

	from = payload->tab_group_index;
	if (from < 0 || from >= grp->member_count || from == to_idx)
		return;

	active = NULL;
	if (grp->active_index >= 0 && grp->active_index < grp->member_count)
		active = grp->members[grp->active_index];

	if (from < to_idx) {
		for (i = from; i < to_idx; i++)
			grp->members[i] = grp->members[i + 1];
	} else {
		for (i = from; i > to_idx; i--)
			grp->members[i] = grp->members[i - 1];
	}
	grp->members[to_idx] = payload;

	for (i = 0; i < grp->member_count; i++) {
		grp->members[i]->tab_group_index = i;
		if (grp->members[i] == active)
			grp->active_index = i;
	}

	wGroupRefreshTitles(grp);
}

/* --------------------------------------------------------------------
 * Titlebar drag-to-group
 * -------------------------------------------------------------------- */

/*
 * Validate target as a drop destination for payload. This is the
 * tabability predicate plus the extra restrictions a drag drop needs:
 * same screen, currently visible on the active workspace, and not the
 * payload itself or already its peer.
 */
static bool wGroupIsDropTarget(WScreen *scr, WWindow *target, WWindow *payload)
{
	if (!target || target == payload)
		return false;
	if (target->screen_ptr != scr)
		return false;
	if (!wGroupIsTabable(target))
		return false;
	if (!WWIN_ON_WORKSPACE(target, scr->current_workspace))
		return false;
	if (wGroupIsInactiveMember(target))
		return false;
	return true;
}

/*
 * Resolve the topmost managed window under root coordinates (rx, ry).
 * Avoids XTranslateCoordinates() against the source frame so the source
 * itself is not always returned, instead it walks the toplevel children
 * of the root from top to bottom and returns the first one whose frame
 * rectangle contains the point.
 */
static WWindow *wGroupWindowUnder(WScreen *scr, WWindow *source, int rx, int ry)
{
	xcb_window_t junk_root, junk_parent, *children;
	WWindow *cand;
	unsigned int n, i;
	int fx, fy, fw, fh;

	children = NULL;
	if (!wXQueryTree(wdisp->conn, scr->root_win, &junk_root, &junk_parent,
	                 &children, &n))
		return NULL;

	cand = NULL;
	/* children[] is bottom-to-top, walk from the top */
	for (i = n; i-- > 0;) {
		cand = wWindowFor(children[i]);
		if (!cand || !cand->frame || cand == source)
			continue;
		if (!cand->flags.mapped && !cand->flags.shaded)
			continue;
		fx = cand->frame_x;
		fy = cand->frame_y;
		fw = (int)cand->frame->core->width;
		fh = (int)cand->frame->core->height;
		if (rx >= fx && rx < fx + fw && ry >= fy && ry < fy + fh)
			break;
		cand = NULL;
	}

	if (children)
		wfree(children);
	return cand;
}

/*
 * Resolve and validate the attach target under root (rx, ry), honouring
 * TabAttachArea so the hover feedback in the drag loop and the release
 * outcome agree on where a drop is accepted.
 */
static WWindow *wGroupDropTargetAt(WScreen *scr, WWindow *source,
				   WWindow *payload, int rx, int ry)
{
	WWindow *target;

	target = wGroupWindowUnder(scr, source, rx, ry);
	if (!target || !wGroupIsDropTarget(scr, target, payload))
		return NULL;
	if (w_global.config->tab_attach_area == WTAB_ATTACH_TITLEBAR
	    && ry >= target->frame_y + (int)target->frame->top_width)
		return NULL;
	return target;
}

/*
 * Detach payload at frame position (rx, ry) instead of the default
 * titlebar offset, used when a tab is torn off and dropped on the root.
 */
static void wGroupTearOffAt(WScreen *scr, WWindow *payload, int rx, int ry)
{
	WWindowGroup *grp;
	int idx, i;
	bool was_active;

	grp = payload->tab_group;
	if (!grp) {
		wWindowMove(payload, rx, ry);
		return;
	}

	idx = payload->tab_group_index;
	was_active = (idx == grp->active_index);

	for (i = idx; i < grp->member_count - 1; i++) {
		grp->members[i] = grp->members[i + 1];
		grp->members[i]->tab_group_index = i;
	}
	grp->member_count--;

	payload->tab_group = NULL;
	payload->tab_group_index = -1;

	if (grp->member_count <= 1) {
		WWindow *survivor = grp->member_count == 1 ? grp->members[0] : NULL;

		if (survivor) {
			survivor->tab_group = NULL;
			survivor->tab_group_index = -1;
			if (was_active)
				wGroupRevealMember(scr, survivor, false);
			wGroupUpdateTitlebar(survivor);
			wGroupNotifyState(survivor);
		}
		wGroupDestroy(grp, scr);
	} else {
		if (was_active) {
			if (grp->active_index >= grp->member_count)
				grp->active_index = grp->member_count - 1;
			wGroupRevealMember(scr, grp->members[grp->active_index], false);
		} else if (idx < grp->active_index) {
			grp->active_index--;
		}
		wGroupRefreshTitles(grp);
	}

	wGroupUpdateTitlebar(payload);
	wWindowMove(payload, rx, ry);
	wGroupRevealMember(scr, payload, true);
	wRaiseFrame(payload->frame->core);
}

bool wGroupDragTab(WWindow *source, WWindow *payload, xcb_generic_event_t *ev)
{
	WScreen *scr;
	WWindow *target, *hover;
	WWindowGroup *src_grp, *dst_grp;
	xcb_generic_event_t *event = NULL;
	xkb_keysym_t ks;
	int orig_rx, orig_ry, rx, ry;
	int outline_w, outline_h, title_h;
	int drop_seg;
	bool started, done, cancelled, over_target;

	if (!source || !payload || !source->frame || !payload->frame)
		return false;

	scr = source->screen_ptr;
	src_grp = payload->tab_group;

	orig_rx = ((xcb_button_press_event_t *)ev)->root_x;
	orig_ry = ((xcb_button_press_event_t *)ev)->root_y;
	rx = orig_rx;
	ry = orig_ry;

	/* The dragged outline tracks the source frame size so attaching to
	 * a target gives the user a sense of the resulting frame footprint
	 * and tearing off lands the payload exactly where the outline was. */
	outline_w = (int)source->frame->core->width;
	outline_h = (int)source->frame->core->height;
	title_h = (int)source->frame->top_width;

	if (wXGrabPointer(wdisp->conn, scr->root_win, false,
	                 XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_RELEASE
	                 | XCB_EVENT_MASK_BUTTON_PRESS,
	                 XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, scr->root_win,
			wdisp->cursor[WCUR_MOVE], XCB_CURRENT_TIME) != WXGrabSuccess)
		return false;
	wXGrabKeyboard(wdisp->conn, scr->root_win, false, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
	              XCB_CURRENT_TIME);

	scr->tab_drag_payload = payload;
	scr->tab_drag_source = source;
	started = false;
	done = false;
	cancelled = false;
	over_target = false;

	while (!done) {
		free(event);
		event = NULL;
		WMMaskEvent(wdisp->conn, XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_POINTER_MOTION
		            | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_KEY_PRESS
		            | XCB_EVENT_MASK_EXPOSURE, &event);

		/* the payload or the source frame may have been unmanaged
		 * while the modal loop was waiting on the next event,
		 * wUnmanageWindow() clears the matching latch in that case */
		if (scr->tab_drag_payload != payload
		    || scr->tab_drag_source != source) {
			cancelled = true;
			free(event);
			event = NULL;
			break;
		}

		if (!event)
			break;

		switch ((event->response_type & 0x7f)) {
		case XCB_MOTION_NOTIFY: {
			/* drain to the newest queued motion, keep that one */
			xcb_generic_event_t *nev;

			while (WXEvqTakeMask(wdisp->conn, XCB_EVENT_MASK_BUTTON_MOTION
			                       | XCB_EVENT_MASK_POINTER_MOTION, XCB_NONE, &nev)) {
				free(event);
				event = nev;
			}
			rx = (((xcb_motion_notify_event_t *)event)->root_x);
			ry = (((xcb_motion_notify_event_t *)event)->root_y);
			if (!started) {
				if (abs(rx - orig_rx) < MOVE_THRESHOLD
				    && abs(ry - orig_ry) < MOVE_THRESHOLD)
					break;
				started = true;
			}
			/* Hover feedback: when the pointer enters a valid
			 * attach area the wireframe snaps onto that frame and
			 * the grab cursor switches to the same WCUR_SELECT
			 * glyph the pointer-pick attach uses, leaving the
			 * area restores the free outline and move cursor. */
			hover = wGroupDropTargetAt(scr, source, payload, rx, ry);
			if ((hover != NULL) != over_target) {
				over_target = (hover != NULL);
				wXChangeActivePointerGrab(wdisp->conn, XCB_EVENT_MASK_BUTTON_MOTION
				        | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_RELEASE
				        | XCB_EVENT_MASK_BUTTON_PRESS,
					wdisp->cursor[over_target
				                ? WCUR_SELECT : WCUR_MOVE],
				        XCB_CURRENT_TIME);
			}
			wWireframeBegin(scr);
			if (hover)
				wWireframeAddOutline(scr, hover->frame_x,
				        hover->frame_y,
				        (int)hover->frame->core->width,
				        (int)hover->frame->core->height,
				        (int)hover->frame->top_width, 0, 1);
			else
				wWireframeAddOutline(scr, rx - outline_w / 2,
				        ry - title_h / 2,
				        outline_w, outline_h, title_h, 0, 1);
			wWireframeCommit(scr);
			break;
		}

		case XCB_BUTTON_PRESS:
			break;

		case XCB_BUTTON_RELEASE:
			if ((((xcb_button_press_event_t *)event)->detail) != ((xcb_button_press_event_t *)ev)->detail)
				break;
			rx = (((xcb_button_press_event_t *)event)->root_x);
			ry = (((xcb_button_press_event_t *)event)->root_y);
			done = true;
			break;

		case XCB_KEY_PRESS:
			ks = wXkbKeycodeToKeysym((((xcb_key_press_event_t *)event)->detail), 0);
			if (ks == XKB_KEY_Escape) {
				cancelled = true;
				done = true;
			}
			break;

		case XCB_EXPOSE:
			WMHandleEvent(event);
			break;

		default:
			break;
		}

		free(event);
		event = NULL;
	}

	wWireframeHide(scr);
	wXUngrabKeyboard(wdisp->conn, XCB_CURRENT_TIME);
	wXUngrabPointer(wdisp->conn, XCB_CURRENT_TIME);
	wXSync(wdisp->conn);

	/* Clear the latch before any attach/detach/reorder so a callee that
	 * happens to dissolve the source group does not see a stale payload
	 * pointer on the screen. */
	scr->tab_drag_payload = NULL;
	scr->tab_drag_source = NULL;

	if (cancelled || !started) {
		return started;
	}


	/* The payload may have lost its group while the loop ran. */
	if (src_grp && payload->tab_group != src_grp)
		src_grp = payload->tab_group;

	target = wGroupDropTargetAt(scr, source, payload, rx, ry);

	/* Drop on the source frame: reorder within the same group when the
	 * release point lies on a different segment, otherwise no-op. */
	if (!target && src_grp && source->tab_group == src_grp
	    && source->frame->tab_seg_count > 0
	    && rx >= source->frame_x && rx < source->frame_x + outline_w
	    && ry >= source->frame_y && ry < source->frame_y + title_h) {
		drop_seg = wFrameWindowTabSegmentAt(source->frame,
		                                    rx - source->frame_x);
		if (drop_seg >= 0 && drop_seg != payload->tab_group_index)
			wGroupReorder(src_grp, payload, drop_seg);
		return true;
	}

	if (target) {
		if (src_grp && target->tab_group == src_grp) {
			drop_seg = -1;
			if (target->frame->tab_seg_count > 0)
				drop_seg = wFrameWindowTabSegmentAt(target->frame,
				                rx - target->frame_x);
			if (drop_seg >= 0)
				wGroupReorder(src_grp, payload, drop_seg);
			return true;
		}
		dst_grp = target->tab_group;
		if (!dst_grp) {
			dst_grp = wGroupCreate(scr);
			wGroupAttach(dst_grp, target);
		}
		/* wGroupAttach() detaches payload from src_grp first when
		 * needed, src_grp may dissolve as a side effect. */
		wGroupAttach(dst_grp, payload);
		wGroupSetActive(dst_grp, payload->tab_group_index);
		return true;
	}

	/* No valid target: tear the payload off at the outline position. */
	wGroupTearOffAt(scr, payload, rx - outline_w / 2, ry - title_h / 2);
	return true;
}

/* --------------------------------------------------------------------
 * Session and warm-restart persistence
 * -------------------------------------------------------------------- */

unsigned wGroupSessionId(WScreen *scr, WWindowGroup *grp)
{
	WWindowGroup *p;
	unsigned id;

	if (!scr || !grp)
		return 0;

	id = 1;
	for (p = scr->window_groups; p; p = p->next, id++)
		if (p == grp)
			return id;
	return 0;
}

static WGroupRestore *wGroupRestoreSlot(WScreen *scr, unsigned gid)
{
	WGroupRestore *rs;

	for (rs = restore_slots; rs; rs = rs->next)
		if (rs->scr == scr && rs->gid == gid)
			return rs;

	rs = wmalloc(sizeof(*rs));
	rs->scr = scr;
	rs->gid = gid;
	rs->active_idx = -1;
	rs->grp = NULL;
	rs->count = 0;
	rs->cap = 4;
	rs->win = wmalloc(sizeof(WWindow *) * (size_t)rs->cap);
	rs->saved_idx = wmalloc(sizeof(int) * (size_t)rs->cap);
	rs->next = restore_slots;
	restore_slots = rs;
	return rs;
}

void wGroupRestoreMember(WWindow *wwin)
{
	WGroupRestore *rs;
	WScreen *scr;
	unsigned gid;
	int idx, pos, i, k;

	if (!wwin || wwin->restore_tab_gid == 0 || wwin->tab_group)
		return;

	gid = wwin->restore_tab_gid;
	idx = wwin->restore_tab_idx;
	wwin->restore_tab_gid = 0;

	/* the saved member must still satisfy the structural part of the
	 * tabable predicate, a class change or a no_titlebar attribute
	 * applied since the session was saved disqualifies it. The
	 * miniaturized flag is allowed because every member of a group
	 * iconified at save time carries it and the post-manage iconify
	 * step has already given each one its own miniwindow, which
	 * wGroupAttach() discards for members that join as inactive. */
	if (!wwin->frame
	    || !HAS_TITLEBAR(wwin) || !IS_MOVABLE(wwin) || !IS_RESIZABLE(wwin)
	    || WINDOW_LEVEL(wwin) != WMNormalLevel
	    || wwin->flags.hidden || wwin->flags.fullscreen
	    || wwin->flags.internal_window)
		return;

	scr = wwin->screen_ptr;
	rs = wGroupRestoreSlot(scr, gid);
	if (rs->active_idx < 0)
		rs->active_idx = wwin->restore_tab_active;

	if (rs->count >= rs->cap) {
		rs->cap *= 2;
		rs->win = wrealloc(rs->win, sizeof(WWindow *) * (size_t)rs->cap);
		rs->saved_idx = wrealloc(rs->saved_idx, sizeof(int) * (size_t)rs->cap);
	}

	/* keep arrived members sorted by their saved index so the rebuilt
	 * members[] array matches the original tab order, ties go to
	 * arrival order */
	pos = rs->count;
	while (pos > 0 && rs->saved_idx[pos - 1] > idx)
		pos--;
	for (i = rs->count; i > pos; i--) {
		rs->win[i] = rs->win[i - 1];
		rs->saved_idx[i] = rs->saved_idx[i - 1];
	}
	rs->win[pos] = wwin;
	rs->saved_idx[pos] = idx;
	rs->count++;

	if (rs->count < 2)
		return;

	if (!rs->grp) {
		rs->grp = wGroupCreate(scr);
		wGroupAttach(rs->grp, rs->win[0]);
		wGroupAttach(rs->grp, rs->win[1]);
	} else {
		wGroupAttach(rs->grp, wwin);
		wGroupReorder(rs->grp, wwin, pos);
	}

	/* the saved active member may already have arrived, switch to it
	 * now so later arrivals do not steal visibility */
	for (k = 0; k < rs->count; k++) {
		if (rs->saved_idx[k] == rs->active_idx) {
			if (rs->grp->active_index != k)
				wGroupSetActive(rs->grp, k);
			break;
		}
	}
}

void wGroupRestoreForget(WWindow *wwin)
{
	WGroupRestore *rs;
	int i, k;

	if (!wwin || !restore_slots)
		return;

	for (rs = restore_slots; rs; rs = rs->next) {
		for (i = 0; i < rs->count; i++) {
			if (rs->win[i] != wwin)
				continue;
			for (k = i; k < rs->count - 1; k++) {
				rs->win[k] = rs->win[k + 1];
				rs->saved_idx[k] = rs->saved_idx[k + 1];
			}
			rs->count--;
			return;
		}
	}
}

/*
 * When the AutoTabGroup attribute is set for a window's class, attach the
 * new window to the first visible tab group on its workspace that already
 * contains a member with the same wm_class string. Session restore takes
 * priority: if wwin arrived with a restore_tab_gid we leave it for
 * wGroupRestoreMember() and do nothing here.
 */
void wGroupAutoAttach(WWindow *wwin)
{
	WWindowGroup *grp;
	int i;

	if (!wwin)
		return;
	if (!WFLAGP(wwin, auto_tab_group))
		return;
	if (!wGroupIsTabable(wwin))
		return;
	/* session restore has already placed this window in a group */
	if (wwin->tab_group)
		return;

	if (!wwin->wm_class)
		return;

	for (grp = wwin->screen_ptr->window_groups; grp; grp = grp->next) {
		for (i = 0; i < grp->member_count; i++) {
			WWindow *m = grp->members[i];

			if (!m || m == wwin)
				continue;
			if (m->screen_ptr != wwin->screen_ptr)
				continue;
			if (!m->wm_class || strcmp(m->wm_class, wwin->wm_class) != 0)
				continue;
			/* group must be on the same workspace or omnipresent */
			if (m->frame &&
			    !WWIN_ON_WORKSPACE(m, wwin->screen_ptr->current_workspace))
				continue;
			if (!wGroupIsTabable(m))
				continue;
			wGroupAttach(grp, wwin);
			return;
		}
	}
}
