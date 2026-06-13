/*
 * Window tab-grouping for Window Maker.
 *
 * Multiple managed windows are attached into a tab group.  Only the
 * active member is mapped, all other members keep their WWindow and
 * WFrameWindow but stay unmapped at the same screen position.  Switching
 * the active tab maps the new member at the outgoing member's geometry
 * and stacking position before unmapping the old one.
 *
 * Tab groups are driven only by local WM gestures, keybindings and the
 * window menu.  No client-owned property or ClientMessage controls
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
 * Remove wwin from its group as part of wUnmanageWindow().  When destroyed
 * is True the leaving window is never reconfigured or mapped, only unlinked
 * so the group can reveal its next member.
 */
void wGroupUnmanageMember(WWindow *wwin, Bool destroyed);

/* propagate a real resize on source to all other members, reentry safe */
void wGroupSyncSize(WWindow *source);

/* refresh the [N/M] title prefix for every member of grp */
void wGroupRefreshTitles(WWindowGroup *grp);

/*
 * Send WM_DELETE_WINDOW (or kill when the protocol is unsupported) to
 * every member of grp.  The group dissolves naturally as each unmanage
 * event arrives.
 */
void wGroupCloseAll(WWindowGroup *grp);

/* predicates */
Bool wGroupIsTabable(WWindow *wwin);
Bool wGroupIsInactiveMember(const WWindow *wwin);
WWindow *wGroupFindAttachTarget(WScreen *scr, WWindow *payload);

/* keyboard handlers, dispatched from event.c */
void wGroupTabNext(WScreen *scr);
void wGroupTabPrev(WScreen *scr);
void wGroupTabAttach(WScreen *scr);
void wGroupTabDetach(WScreen *scr);

/*
 * Window-menu attach helper.  Grabs the pointer with the select cursor,
 * waits for a ButtonPress, resolves the target window, validates it with
 * wGroupIsTabable() and attaches payload to its group.  Escape or a click
 * on the root window cancels.
 */
void wGroupPickAndAttach(WScreen *scr, WWindow *payload);

#endif /* TABGROUP_H_ */
