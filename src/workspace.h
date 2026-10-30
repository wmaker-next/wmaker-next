/* workspace.c- Workspace management
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2010-2026 Window Maker Team
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

#ifndef WMWORKSPACE_H_
#define WMWORKSPACE_H_



typedef struct WWorkspace {
    char *name;
    struct WDock *clip;
    RImage *map;
    /* WorkspaceOutputs pin: RandR output names this workspace is bound
     * to, owned strings. 0 entries means unpinned (allowed on every
     * head). */
    char **head_names;
    int head_name_count;
    /* per-workspace tiling layout, WLAYOUT_FLOAT is the no-op default */
    signed char layout;
    short master_count;
    float master_ratio;
#ifdef VIRTUAL_DESKTOP
    /* Viewport (pannable desktop) state. Under the window-move model
     * view_x/view_y record where the camera *is* for
     * EWMH/OSD. On-screen frame_x/frame_y are the world coords and a
     * pan mutates them directly. vdesk_w/h is the finite desktop extent
     * used to clamp the viewport. vdesk_x/y is the world-space origin of
     * that extent (0 when no window extends past the origin, negative
     * when a window has been moved off the left/top edge) so the viewport
     * clamp can reach windows dragged off-screen in any direction. The
     * extent itself is computed from invariant world coordinates
     * (frame + view) so it does not shrink as the viewport pans. */
    int vdesk_w, vdesk_h;
    int vdesk_x, vdesk_y;
    int view_x, view_y;
#endif
} WWorkspace;

void wWorkspaceMake(WScreen *scr, int count);
int wWorkspaceNew(WScreen *scr);
int wGetWorkspaceNumber(WScreen *scr, const char *value);
int wWorkspaceFindByName(WScreen *scr, const char *name);
bool wWorkspaceDelete(WScreen *scr, int workspace);
void wAutoRemoveEmptyWorkspaces(WScreen *scr);
void wWorkspaceChange(WScreen *scr, int workspace);
/* Pointer-initiated variants set w_global.pointer_driven_change so the warp
 * in wWorkspaceForceChange[OnHead] is skipped. */
void wWorkspaceChangeFromPointer(WScreen *scr, int workspace);
void wWorkspaceRelativeChangeFromPointer(WScreen *scr, int amount);
void wWorkspaceDirectionalChangeFromPointer(WScreen *scr, int dcol, int drow);
void wWorkspaceForceChange(WScreen *scr, int workspace);
WMenu *wWorkspaceMenuMake(WScreen *scr, bool titled);
void wWorkspaceMenuUpdate(WScreen *scr, WMenu *menu);
void wWorkspaceSaveState(WScreen *scr, WMPropList *old_state);
void wWorkspaceRestoreState(WScreen *scr);
void wWorkspaceRename(WScreen *scr, int workspace, const char *name);
void wWorkspaceRelativeChange(WScreen *scr, int amount);
bool wWorkspaceLayoutPosition(WScreen *scr, int index, int *row, int *col);
int wWorkspaceNeighbour(WScreen *scr, int from, int dcol, int drow);
void wWorkspaceDirectionalChange(WScreen *scr, int dcol, int drow);

/* Per-head independent workspace support */
int wWorkspaceForHead(WScreen *scr, int head);
int wWorkspaceRelativeHeadTarget(WScreen *scr, int head, int amount);
int wHeadShowingWorkspace(WScreen *scr, int workspace);
int wWorkspaceDefaultForHead(WScreen *scr, int head);
bool wWorkspaceIsAllowedOnHead(WScreen *scr, int workspace, int head);
bool wWorkspaceIsVisibleOnHead(WScreen *scr, int workspace, int head);
bool wWindowIsVisibleOnAnyHead(struct WWindow *wwin);
void wWorkspaceSetCurrentHead(WScreen *scr, int head);
void wWorkspaceForceChangeOnHead(WScreen *scr, int workspace, int head);
void wWorkspaceLoadOutputPins(WScreen *scr, WMPropList *wmaker_dict);
void wWorkspaceClearOutputPins(WScreen *scr);
void wWorkspaceSaveHeadState(WScreen *scr);
void wWorkspaceRestoreHeadState(WScreen *scr);
void wWorkspaceReconcileHeads(WScreen *scr);

#ifdef VIRTUAL_DESKTOP
/* Maps each edge InputOnly window to its edge-scroll direction handler.
 * Defined in src/vdesk.c, used by src/event_input.c. 0 until
 * wWorkspaceUpdateEdge() first runs. Declared extern here (not in
 * vdesk.h) so the standalone check_config unit test (which compiles
 * src/wconfigmodel.c without src/vdesk.c) can still see it as a plain
 * symbol. The runtime-reload branch in wconfigmodel.c calls
 * wWorkspaceUpdateEdge(), declared in vdesk.h. */
extern WMHashTable *wVEdgeTable;
#endif

#endif
