/* workspace.c- Workspace management
 *
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
} WWorkspace;

void wWorkspaceMake(WScreen *scr, int count);
int wWorkspaceNew(WScreen *scr);
int wGetWorkspaceNumber(WScreen *scr, const char *value);
Bool wWorkspaceDelete(WScreen *scr, int workspace);
void wAutoRemoveEmptyWorkspaces(WScreen *scr);
void wWorkspaceChange(WScreen *scr, int workspace);
void wWorkspaceForceChange(WScreen *scr, int workspace);
WMenu *wWorkspaceMenuMake(WScreen *scr, Bool titled);
void wWorkspaceMenuUpdate(WScreen *scr, WMenu *menu);
void wWorkspaceSaveState(WScreen *scr, WMPropList *old_state);
void wWorkspaceRestoreState(WScreen *scr);
void wWorkspaceRename(WScreen *scr, int workspace, const char *name);
void wWorkspaceRelativeChange(WScreen *scr, int amount);
Bool wWorkspaceLayoutPosition(WScreen *scr, int index, int *row, int *col);
int wWorkspaceNeighbour(WScreen *scr, int from, int dcol, int drow);
void wWorkspaceDirectionalChange(WScreen *scr, int dcol, int drow);

/* Per-head independent workspace support */
int wWorkspaceForHead(WScreen *scr, int head);
int wWorkspaceRelativeHeadTarget(WScreen *scr, int head, int amount);
int wHeadShowingWorkspace(WScreen *scr, int workspace);
int wWorkspaceDefaultForHead(WScreen *scr, int head);
Bool wWorkspaceIsAllowedOnHead(WScreen *scr, int workspace, int head);
Bool wWorkspaceIsVisibleOnHead(WScreen *scr, int workspace, int head);
Bool wWindowIsVisibleOnAnyHead(struct WWindow *wwin);
void wWorkspaceSetCurrentHead(WScreen *scr, int head);
void wWorkspaceForceChangeOnHead(WScreen *scr, int workspace, int head);
void wWorkspaceLoadOutputPins(WScreen *scr, WMPropList *wmaker_dict);
void wWorkspaceClearOutputPins(WScreen *scr);
void wWorkspaceSaveHeadState(WScreen *scr);
void wWorkspaceRestoreHeadState(WScreen *scr);
void wWorkspaceReconcileHeads(WScreen *scr);

#endif
