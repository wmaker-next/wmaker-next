/*
 *  tabgroup.h
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
 * Multiple managed windows are attached into a tab group. Only the
 * active member is mapped, all other members keep their WWindow and
 * WFrameWindow but stay unmapped at the same screen position. Switching
 * the active tab maps the new member at the outgoing member's geometry
 * and stacking position before unmapping the old one.
 *
 * Tab groups are driven only by local WM gestures, keybindings and the
 * window menu. No client-owned property or ClientMessage controls
 * group membership.
 */

#ifndef TABGROUP_H_
#define TABGROUP_H_

#include "window.h"
#include "screen.h"

/* lifecycle */
WWindowGroup *wGroupCreate(WScreen *scr);
void wGroupAttach(WWindowGroup *grp, WWindow *wwin);
void wGroupDetach(WWindow *wwin);
void wGroupSetActive(WWindowGroup *grp, int idx);

/*
 * Remove wwin from its group as part of wUnmanageWindow(). When destroyed
 * is True the leaving window is never reconfigured or mapped, only unlinked
 * so the group can reveal its next member.
 */
void wGroupUnmanageMember(WWindow *wwin, bool destroyed);

/*
 * Peer state propagation. Each helper is called from the active member's
 * normal action path and mirrors the relevant state onto every inactive
 * member of the same group. All of them share one reentry guard so a
 * peer update never recurses back through the originating action.
 */
void wGroupSyncSize(WWindow *source);
void wGroupSyncPosition(WWindow *source);
void wGroupSyncWorkspace(WWindow *source);
void wGroupSyncShade(WWindow *source);
void wGroupSyncMiniaturize(WWindow *source);
void wGroupSyncMaximize(WWindow *source);

/*
 * Refresh every member's frame title and repaint the active member's
 * titlebar. When TabsInTitlebar is on the segmented renderer reads
 * each member's plain frame->title, otherwise the [N/M] prefix is
 * applied.
 */
void wGroupRefreshTitles(WWindowGroup *grp);

/*
 * Send WM_DELETE_WINDOW (or kill) to one member by index without first
 * switching the active tab. Used by Ctrl+Button1 on a titlebar segment.
 */
void wGroupCloseMember(WWindowGroup *grp, int idx);

/*
 * Send WM_DELETE_WINDOW (or kill when the protocol is unsupported) to
 * every member of grp. The group dissolves naturally as each unmanage
 * event arrives.
 */
void wGroupCloseAll(WWindowGroup *grp);

/* predicates */
bool wGroupIsTabable(WWindow *wwin);
bool wGroupIsInactiveMember(const WWindow *wwin);
WWindow *wGroupFindAttachTarget(WScreen *scr, WWindow *payload);

/* keyboard handlers, dispatched from event.c */
void wGroupTabNext(WScreen *scr);
void wGroupTabPrev(WScreen *scr);
void wGroupTabAttach(WScreen *scr);
void wGroupTabDetach(WScreen *scr);

/*
 * Window-menu attach helper. Grabs the pointer with the select cursor,
 * waits for a ButtonPress, resolves the target window, validates it with
 * wGroupIsTabable() and attaches payload to its group. Escape or a click
 * on the root window cancels.
 */
void wGroupPickAndAttach(WScreen *scr, WWindow *payload);

/*
 * Move payload to position to_idx within its own group. Used by the
 * titlebar segment drag when the drop lands on the same group.
 */
void wGroupReorder(WWindowGroup *grp, WWindow *payload, int to_idx);

/*
 * Titlebar drag-to-group gesture. ev is the ButtonPress that started
 * the drag, source is the visible frame owner whose titlebar was pressed
 * and payload is the group member being dragged out. Runs a local event
 * loop with the move cursor and a wireframe outline, on release the drop
 * point is resolved through wWindowFor() and the payload is attached,
 * detached at the drop position, or reordered within the same group.
 * Returns True when the drag actually started past MOVE_THRESHOLD so the
 * caller can suppress its click action.
 */
bool wGroupDragTab(WWindow *source, WWindow *payload, xcb_generic_event_t *ev);

/*
 * Session and warm-restart persistence. wGroupSessionId() returns a
 * non-zero per-screen group identifier stable for the lifetime of grp,
 * or 0 when grp is NULL or no longer linked. wGroupRestoreMember() is
 * called once for every newly managed window from the MapRequest and
 * manageAllWindows() paths after the deferred shade, iconify and hide
 * steps so each member is in its final state before it is attached.
 * The saved (gid, idx, active) triple is read from wwin->restore_tab_*
 * which wManageWindow() sets from either WSavedState source. Members
 * arrive asynchronously, so the first member of a gid is held as a
 * normal window and the group is created lazily when its second member
 * appears. Later arrivals are attached and reordered to their saved
 * position. wGroupRestoreForget() drops wwin from any pending restore
 * slot and is safe to call for windows that were never registered.
 */
unsigned wGroupSessionId(WScreen *scr, WWindowGroup *grp);
void wGroupRestoreMember(WWindow *wwin);
void wGroupRestoreForget(WWindow *wwin);
void wGroupAutoAttach(WWindow *wwin);

#endif /* TABGROUP_H_ */
