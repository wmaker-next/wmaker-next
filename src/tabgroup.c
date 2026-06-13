/*
 * Window tab-grouping for Window Maker.
 *
 * Multiple managed windows share one on-screen frame slot.  Only the
 * active member is mapped, the others stay managed but unmapped at the
 * same screen position.  Each member keeps its own WFrameWindow, the
 * active member's titlebar carries an [N/M] prefix until the segmented
 * renderer lands.
 */

#include "wconfig.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>
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
#include "stacking.h"
#include "event.h"
#include "icon.h"
#include "tabgroup.h"


static void wGroupUpdateTitlebar(WWindow *wwin);
static void wGroupNotifyState(WWindow *wwin);
static void wGroupRevealMember(WScreen *scr, WWindow *wwin, Bool focus);
static void wGroupPlaceDetached(WWindow *wwin, WWindow *anchor);
static void wGroupRemoveMember(WWindow *wwin, Bool unmanaging);
static void wGroupDestroy(WWindowGroup *grp, WScreen *scr);


/*
 * Shared reentry guard for every wGroupSync*() helper.  The propagators
 * call back into wWindowConfigure(), wWindowMove() and friends which in
 * turn call back into the same propagators, the guard breaks that cycle.
 */
static int syncing_peers = 0;


/* --------------------------------------------------------------------
 * Predicates
 * -------------------------------------------------------------------- */

Bool wGroupIsInactiveMember(const WWindow *wwin)
{
	if (!wwin || !wwin->tab_group)
		return False;
	return wwin->tab_group_index != wwin->tab_group->active_index;
}

/*
 * A window may join a tab group only when it has a normal titlebar,
 * is movable and resizable, sits at WMNormalLevel and is not currently
 * fullscreen, miniaturized or hidden.  Fixed-size and borderless
 * windows are refused so members never desync on size.
 */
Bool wGroupIsTabable(WWindow *wwin)
{
	if (!wwin || !wwin->frame)
		return False;
	if (!HAS_TITLEBAR(wwin) || !IS_MOVABLE(wwin) || !IS_RESIZABLE(wwin))
		return False;
	if (WINDOW_LEVEL(wwin) != WMNormalLevel)
		return False;
	if (wwin->flags.fullscreen || wwin->flags.miniaturized || wwin->flags.hidden)
		return False;
	if (wwin->flags.internal_window)
		return False;
	return True;
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
		if (cand->frame && cand->frame->workspace != scr->current_workspace
		    && !IS_OMNIPRESENT(cand))
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
	if (!wFetchName(dpy, wwin->client_win, &xname) || !xname)
		base = DEF_WINDOW_TITLE;
	else
		base = xname;

	if (wwin->tab_group && wwin->tab_group->member_count > 1) {
		snprintf(buf, sizeof(buf), "[%d/%d] %s",
		         wwin->tab_group_index + 1,
		         wwin->tab_group->member_count, base);
		wFrameWindowChangeTitle(wwin->frame, buf);
	} else {
		wFrameWindowChangeTitle(wwin->frame, base);
	}

	if (xname)
		XFree(xname);
}

void wGroupRefreshTitles(WWindowGroup *grp)
{
	int i;

	if (!grp)
		return;
	for (i = 0; i < grp->member_count; i++)
		wGroupUpdateTitlebar(grp->members[i]);
}

/* --------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------- */

static void wGroupNotifyState(WWindow *wwin)
{
	/* let wmspec.c rebuild _NET_WM_STATE so HIDDEN is toggled */
	WMPostNotificationName(WMNChangedState, wwin, "tabgroup");
}

static void wGroupRevealMember(WScreen *scr, WWindow *wwin, Bool focus)
{
	if (!wwin->frame)
		return;
	/* the whole group may be miniaturized or hidden, in that case the
	 * newly active member stays unmapped and only the membership state
	 * is published, the existing miniwindow or app icon brings it back */
	if (!wwin->flags.miniaturized && !wwin->flags.hidden) {
		XMapWindow(dpy, wwin->frame->core->window);
		if (!wwin->flags.shaded) {
			XMapWindow(dpy, wwin->client_win);
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

	for (p = &scr->window_groups; *p && *p != grp; p = &(*p)->next)
		;
	if (*p)
		*p = grp->next;

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
		XUnmapWindow(dpy, wwin->frame->core->window);
		wwin->flags.mapped = 0;
		wGroupNotifyState(wwin);
	}

	wGroupRefreshTitles(grp);
}

/*
 * Internal removal.  When unmanaging is True the leaving window is being
 * destroyed by wUnmanageWindow(), so it must not be touched beyond
 * clearing its membership pointer.
 */
static void wGroupRemoveMember(WWindow *wwin, Bool unmanaging)
{
	WWindowGroup *grp;
	WScreen *scr;
	WWindow *survivor;
	WWindow *anchor;
	int idx, i;
	Bool was_active;

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
		 * Only one member left, dissolve the group.  The survivor
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
				wGroupRevealMember(scr, wwin, False);
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
			wGroupRevealMember(scr, wwin, False);
	}
}

void wGroupDetach(WWindow *wwin)
{
	wGroupRemoveMember(wwin, False);
}

void wGroupUnmanageMember(WWindow *wwin, Bool destroyed)
{
	(void) destroyed;
	wGroupRemoveMember(wwin, True);
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
	 * hide.  Both members are already unmapped in that case, so only
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

	XGrabServer(dpy);

	/*
	 * Map and focus the incoming frame before unmapping the outgoing
	 * one so X does not revert focus to the root in between.
	 */
	wWindowMove(nxt, x, y);
	XMapWindow(dpy, nxt->frame->core->window);
	if (!nxt->flags.shaded)
		XMapWindow(dpy, nxt->client_win);
	nxt->flags.mapped = 1;

	/*
	 * Restack the incoming frame directly above the outgoing one so the
	 * group keeps its position in the stacking order rather than rising
	 * to the top of the level.
	 */
	MoveInStackListAbove(cur->frame->core, nxt->frame->core);

	grp->active_index = idx;
	wSetFocusTo(scr, nxt);

	XUnmapWindow(dpy, cur->frame->core->window);
	cur->flags.mapped = 0;

	XUngrabServer(dpy);

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
	int workspace, old, i;

	grp = source->tab_group;
	if (!grp || syncing_peers || !source->frame)
		return;

	workspace = source->frame->workspace;

	syncing_peers = 1;
	for (i = 0; i < grp->member_count; i++) {
		peer = grp->members[i];
		if (peer == source || !peer->frame)
			continue;
		if (peer->frame->workspace == workspace)
			continue;
		old = peer->frame->workspace;
		peer->frame->workspace = workspace;
		peer->last_head = source->last_head;
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
	Bool shaded;

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
			XSelectInput(dpy, peer->client_win,
			             peer->event_mask & ~StructureNotifyMask);
			XUnmapWindow(dpy, peer->client_win);
			XSelectInput(dpy, peer->client_win, peer->event_mask);
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
			XMapWindow(dpy, peer->client_win);
		}
		wWindowSynthConfigureNotify(peer);
		WMPostNotificationName(WMNChangedState, peer, "shade");
	}
	syncing_peers = 0;
}

/*
 * Mirror miniaturized state.  Only the active member ever creates or
 * owns a WIcon, peers just follow flags.miniaturized and WM_STATE so
 * pagers and the switch menu show one minimized unit.
 */
void wGroupSyncMiniaturize(WWindow *source)
{
	WWindowGroup *grp;
	WWindow *peer;
	int i;
	Bool mini;

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
		wClientSetState(peer, mini ? IconicState : NormalState, None);
		WMPostNotificationName(WMNChangedState, peer, "iconify");
	}
	syncing_peers = 0;
}

/*
 * Mirror maximize bookkeeping.  Geometry itself is already propagated
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
	 * Snapshot the member list first.  Each close triggers an
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
			wClientSendProtocol(w, w_global.atom.wm.delete_window,
			                    w_global.timestamp.last_event);
		else
			wClientKill(w);
	}

	wfree(snap);
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
	XEvent ev;
	KeySym ks;
	Window root, child;
	int rx, ry, wx, wy;
	unsigned int mask;
	Bool done, cancelled;

	if (!payload || !wGroupIsTabable(payload))
		return;

	if (XGrabPointer(dpy, scr->root_win, False,
	                 ButtonPressMask | ButtonReleaseMask,
	                 GrabModeAsync, GrabModeAsync, scr->root_win,
	                 wPreferences.cursor[WCUR_SELECT], CurrentTime) != GrabSuccess)
		return;
	XGrabKeyboard(dpy, scr->root_win, False, GrabModeAsync, GrabModeAsync, CurrentTime);

	/* latch the payload so wUnmanageWindow() can knock it out if the
	 * client exits while the pick loop is waiting on the user, the
	 * SubstructureNotifyMask pull below lets DestroyNotify and
	 * UnmapNotify reach DispatchEvent() during the grab */
	scr->tab_pick_payload = payload;

	target = NULL;
	done = False;
	cancelled = False;

	while (!done) {
		WMMaskEvent(dpy, ButtonPressMask | ButtonReleaseMask | KeyPressMask
		            | ExposureMask | SubstructureNotifyMask, &ev);

		if (scr->tab_pick_payload != payload) {
			cancelled = True;
			break;
		}

		switch (ev.type) {
		case ButtonPress:
			if (ev.xbutton.button > Button3) {
				cancelled = True;
				done = True;
				break;
			}
			child = ev.xbutton.subwindow;
			if (child == None)
				XQueryPointer(dpy, scr->root_win, &root, &child,
				              &rx, &ry, &wx, &wy, &mask);
			if (child == None || child == scr->root_win) {
				cancelled = True;
				done = True;
				break;
			}
			target = wWindowFor(child);
			done = True;
			break;
		case KeyPress:
			ks = XLookupKeysym(&ev.xkey, 0);
			if (ks == XK_Escape) {
				cancelled = True;
				done = True;
			}
			break;
		default:
			/* Expose and the SubstructureNotify family, the latter
			 * so a payload or target unmanage is processed now
			 * rather than queued behind the grab */
			WMHandleEvent(&ev);
			if (scr->tab_pick_payload != payload) {
				cancelled = True;
				done = True;
			}
			break;
		}
	}

	scr->tab_pick_payload = NULL;

	XUngrabKeyboard(dpy, CurrentTime);
	XUngrabPointer(dpy, CurrentTime);
	XSync(dpy, False);

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
