/*
 * vdesk.h- Viewport (pannable desktop) panning
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

#ifndef WMVDESK_H_
#define WMVDESK_H_

#include "wconfig.h"

#ifdef VIRTUAL_DESKTOP

#include <WINGs/WUtil.h>

typedef struct _WScreen WScreen;

/* Move the viewport for the given workspace to (view_x, view_y) in world
 * coordinates, using the window-move model: every non-sticky window on
 * the workspace is shifted by -(view_x - old_view_x) /
 * -(view_y - old_view_y) via wWindowMove, so frame_x/frame_y ARE the world
 * coords and no camera offset is needed at paint time.
 *
 * Returns False immediately when the workspace layout is not
 * WLAYOUT_FLOAT — a tiled workspace cannot pan because wLayoutApply
 * would snap windows back.
 *
 * The viewport is clamped to the finite desktop extent
 * (view_x in [0, vdesk_w - scr_w]).
 *
 * Returns True if any window moved, False if the viewport was unchanged
 * or the workspace is not pannable. */
bool wVdeskSetViewport(WScreen *scr, int workspace, int view_x, int view_y);

/* Per-head guard. True when the viewport must stay inert:
 * per_head_workspaces is on, or the workspace is output-pinned via
 * WorkspaceOutputs (head_names non-empty). workspace = -1 checks only the
 * screen-level per_head_workspaces flag. */
bool wVdeskPerHeadBlocked(WScreen *scr, int workspace);

/* Recompute the finite desktop extent (vdesk_w/h) from the extents of all
 * non-sticky windows on the workspace, anchoring the origin at negative
 * world coordinates so the viewport clamp in wVdeskSetViewport can reach
 * windows dragged past the top/left edge. */
void wVdeskUpdateGeometry(WScreen *scr, int workspace);

/* Direction vectors for keyboard/edge pan. Stored as WMPoint so the
 * dot-product helpers in vdesk.c compose with wmkpoint() directly. */
#define VEC_LEFT  wmkpoint(-1, 0)
#define VEC_RIGHT wmkpoint(1, 0)
#define VEC_UP    wmkpoint(0, -1)
#define VEC_DOWN  wmkpoint(0, 1)

/* Keyboard pan: snap to the nearest window edge in the given direction
 * (one-shot, not continuous). Ported from the deleted 2010
 * wWorkspaceKeyboardMoveDesktop. */
void wVdeskKeyboardMoveDesktop(WScreen *scr, WMPoint direction);

/* Companion keybinds: home resets the camera to (0,0), center pans so
 * the focused window is screen-centered, pin toggles virtual_stick on
 * the focused window (in addition to the EWMH window-type defaults). */
void wVdeskHome(WScreen *scr);
void wVdeskCenter(WScreen *scr, WWindow *wwin);
void wVdeskPin(WScreen *scr, WWindow *wwin);

/* Pointer edge-scroll. Called from handleEnterNotify when the cursor
 * enters an edge InputOnly window (looked up via wVEdgeTable). Ported
 * from the deleted 2010 vdMouseMoveDesktop. The XEvent is the EnterNotify
 * that triggered the call. The loop runs in the geometry of the head
 * whose sentinel fired, so it pans on that head only. */
void wVdeskMouseMoveDesktop(xcb_generic_event_t *event, WMPoint direction);

/* Pointer drag-pan. Called from executeButtonAction when the user binds
 * a root-window mouse button to the VirtualDesktopPan action. The xcb_generic_event_t * is the ButtonPress that triggered the call. XGrabPointer flips the
 * cursor to WCUR_MOVE. MotionNotify deltas feed wVdeskSetViewport until
 * ButtonRelease. Drag-pan is inherently smooth (one SetViewport per
 * MotionNotify, no tween). */
void wVdeskDragPan(WScreen *scr, xcb_generic_event_t *event);

/* Edge InputOnly window lifecycle. wWorkspaceUpdateEdge() creates the
 * edge windows when vdesk_enable is on and destroys them when it is off.
 * It is called from the WCFG_KEY_ENABLE_VIRTUAL_DESKTOP runtime-reload
 * branch in wConfigApplyRuntime, at startup, and from the RandR
 * screen-change handler. wWorkspaceRaiseEdge / wWorkspaceLowerEdge keep
 * the edge InputOnly windows above the stack but below menus (called
 * from src/stacking.c and src/menu.c). */
void wWorkspaceUpdateEdge(WScreen *scr);
void wWorkspaceRaiseEdge(WScreen *scr);
void wWorkspaceLowerEdge(WScreen *scr);

#endif /* VIRTUAL_DESKTOP */

#endif /* WMVDESK_H_ */
