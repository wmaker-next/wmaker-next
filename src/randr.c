/* randr.c - RandR multi-monitor support
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

#include "wconfig.h"

#ifdef USE_RANDR

#include <string.h>
#include <X11/Xlib.h>

#include "randr.h"
#include "window.h"
#include "framewin.h"
#include "xinerama.h"
#include "wmspec.h"
#include "dock.h"
#include "workspace.h"
#include "actions.h"
#include "hotcorner.h"

#define RANDR_DEBOUNCE_DELAY  100   /* milliseconds */

typedef struct {
    RROutput xid;
    RRCrtc crtc;
    int x, y, w, h;
    Rotation rotation;     /* current CRTC rotation */
    Bool connected;        /* RR_Connected */
    Bool stale;            /* mark-scan scratch flag */
    char name[64];
} WRandROutput;

typedef struct {
    WRandROutput outputs[WM_MAX_HEADS];
    int n_outputs;
} WRandRState;

/* XRROutputInfo.name is not NUL-terminated, copy nameLen bytes and terminate */
static void wRandR_CopyOutputName(WRandROutput *o, const XRROutputInfo *oi)
{
    size_t name_len = (size_t)oi->nameLen;

    if (name_len >= sizeof(o->name))
        name_len = sizeof(o->name) - 1;
    memcpy(o->name, oi->name, name_len);
    o->name[name_len] = '\0';
}

/* Push RandR geometry into the Xinerama head layer
 *
 * RandR 1.5 GetMonitors returns one entry per logical monitor with name,
 * geometry and the primary flag in a single round trip, mirrored outputs
 * sharing a CRTC are already coalesced server-side so no client-side
 * dedup or per-output/per-CRTC walk is needed for the head list.
 *
 * Returns True when the RandR view was adopted into xine_info, False when
 * the stub-RandR fallback kept the existing XINERAMA-derived head list. */
static Bool wRandR_ApplyToXinerama(WScreen *scr)
{
    WMRect new_screens[WM_MAX_HEADS];
    char name_buf[WM_MAX_HEADS][64];
    const char *new_names[WM_MAX_HEADS];   /* parallel: monitor name of each head */
    XRRMonitorInfo *mi;
    Window dummy_w;
    unsigned int root_uw, root_uh, dummy_u;
    int nmon = 0;
    int count = 0;
    int primary = 0;
    int old_count;
    int root_w, root_h, dummy_i;
    int max_x, max_y;
    int i;

    mi = XRRGetMonitors(dpy, scr->root_win, True, &nmon);

    for (i = 0; mi && i < nmon && count < WM_MAX_HEADS; i++) {
        char *aname;

        if (mi[i].width <= 0 || mi[i].height <= 0)
            continue;

        new_screens[count].pos.x = mi[i].x;
        new_screens[count].pos.y = mi[i].y;
        new_screens[count].size.width = mi[i].width;
        new_screens[count].size.height = mi[i].height;

        name_buf[count][0] = '\0';
        aname = mi[i].name != None ? XGetAtomName(dpy, mi[i].name) : NULL;
        if (aname) {
            strncpy(name_buf[count], aname, sizeof(name_buf[count]) - 1);
            name_buf[count][sizeof(name_buf[count]) - 1] = '\0';
            XFree(aname);
        }
        new_names[count] = name_buf[count][0] ? name_buf[count] : NULL;

        if (mi[i].primary)
            primary = count;

        count++;
    }

    if (mi)
        XRRFreeMonitors(mi);

    /* Some servers (notably Xephyr +xinerama) expose real XINERAMA heads
     * but only a stub RandR view, GetMonitors there returns one synthetic
     * monitor that covers only part of the real root window, so query the
     * actual root window geometry here rather than trusting the cache.
     * If XINERAMA has already populated more heads than RandR can account
     * for and the RandR view does not span the real root, RandR is not the
     * authoritative geometry source, keep the XINERAMA-derived xine_info
     * instead of replacing it with an incomplete single-head fallback. */
    root_w = scr->scr_width;
    root_h = scr->scr_height;
    if (XGetGeometry(dpy, scr->root_win, &dummy_w, &dummy_i, &dummy_i,
                     &root_uw, &root_uh, &dummy_u, &dummy_u)) {
        root_w = (int)root_uw;
        root_h = (int)root_uh;
    }
    max_x = 0;
    max_y = 0;
    for (i = 0; i < count; i++) {
        int rx = new_screens[i].pos.x + new_screens[i].size.width;
        int ry = new_screens[i].pos.y + new_screens[i].size.height;
        if (rx > max_x)
            max_x = rx;
        if (ry > max_y)
            max_y = ry;
    }
    if (scr->xine_info.count > count
        && (count == 0 || max_x < root_w || max_y < root_h)) {
        wwarning("RandR reports %d active output(s) covering %dx%d but the "
                 "root window is %dx%d and XINERAMA reports %d head(s), "
                 "keeping XINERAMA geometry",
                 count, max_x, max_y, root_w, root_h,
                 scr->xine_info.count);
        /* Leave head_output_name[] unset so the "#N" index keys in
         * HeadWorkspaces and WorkspaceOutputs keep addressing these
         * heads, the stub view's synthetic monitor name carries no
         * connector identity worth pinning to. */
        return False;
    }

    /* Fallback: if every output disappeared, use the full virtual screen so
     * we never end up with zero heads */
    if (count == 0) {
        new_screens[0].pos.x = 0;
        new_screens[0].pos.y = 0;
        new_screens[0].size.width = scr->scr_width;
        new_screens[0].size.height = scr->scr_height;
        new_names[0] = NULL;
        count = 1;
    }

    old_count = wXineramaHeads(scr);

    /* Rebuild the active-head output-name table parallel to xine_info */
    wClearHeadOutputNames(scr);
    for (i = 0; i < count && i < WM_MAX_HEADS; i++) {
        if (new_names[i])
            scr->head_output_name[i] = wstrdup(new_names[i]);
    }

    /* Replace the screens array */
    if (scr->xine_info.screens)
        wfree(scr->xine_info.screens);

    scr->xine_info.screens = wmalloc(sizeof(WMRect) * (count + 1));
    memcpy(scr->xine_info.screens, new_screens, sizeof(WMRect) * count);
    scr->xine_info.count = count;

    scr->xine_info.primary_head = primary;

    /* XRRUpdateConfiguration() is gone since the cooked event no longer
     * exists, and Xlib's cached WidthOfScreen() never updates under
     * XCBOwnsEventQueue, so use the GetGeometry result above */
    scr->scr_width = root_w;
    scr->scr_height = root_h;

    /* Reallocate per-head usable area arrays if the head count changed */
    if (old_count != count) {
        wfree(scr->usableArea);
        wfree(scr->totalUsableArea);
        scr->usableArea = wmalloc(sizeof(WArea) * count);
        scr->totalUsableArea = wmalloc(sizeof(WArea) * count);
    }

    /* Seed usable areas from new geometry */
    for (i = 0; i < count; i++) {
        WMRect *r = &scr->xine_info.screens[i];

        scr->usableArea[i].x1 = scr->totalUsableArea[i].x1 = r->pos.x;
        scr->usableArea[i].y1 = scr->totalUsableArea[i].y1 = r->pos.y;
        scr->usableArea[i].x2 = scr->totalUsableArea[i].x2 = r->pos.x + r->size.width;
        scr->usableArea[i].y2 = scr->totalUsableArea[i].y2 = r->pos.y + r->size.height;
    }

    return True;
}

/* Bring any off-screen windows in */
static void wRandR_BringAllWindowsInside(WScreen *scr)
{
    WWindow *wwin;
    int i;

    for (wwin = scr->focused_window; wwin != NULL; wwin = wwin->prev) {
        int bw, wx, wy, ww, wh;
        int fully_inside = 0;

        if (!wwin->flags.mapped || wwin->flags.hidden)
            continue;

        bw = HAS_BORDER(wwin) ? scr->frame_border_width : 0;
        wx = wwin->frame_x - bw;
        wy = wwin->frame_y - bw;
        ww = (int)wwin->frame->core->width  + 2 * bw;
        wh = (int)wwin->frame->core->height + 2 * bw;

        /* Skip windows already fully contained within a surviving head */
        for (i = 0; i < wXineramaHeads(scr); i++) {
            WMRect r = wGetRectForHead(scr, i);
            if (wx >= r.pos.x && wy >= r.pos.y &&
                wx + ww <= r.pos.x + (int)r.size.width &&
                wy + wh <= r.pos.y + (int)r.size.height) {
                fully_inside = 1;
                break;
            }
        }

        if (!fully_inside)
            wWindowSnapToHead(wwin);
    }
}

/* Auto-deactivate a physically disconnected output that still holds a CRTC
 *
 * When a cable is pulled or monitor turned off, the X server sets
 * connection=RR_Disconnected but does NOT free the CRTC automatically.
 * We must call XRRSetCrtcConfig with mode=None to release it,
 * which causes the server to fire a fresh batch of RandR events.
 *
 * Returns True if at least one CRTC was freed */
static Bool wRandR_AutoDeactivate(WScreen *scr, WRandRState *state, XRRScreenResources *sr)
{
    Bool deactivated = False;
    int i;

    for (i = 0; i < state->n_outputs; i++) {
        WRandROutput *o = &state->outputs[i];
        Status ret;

        if (o->connected || o->crtc == None || o->stale)
            continue;

        /* Release the CRTC: no mode, no outputs */
        ret = XRRSetCrtcConfig(dpy, sr, o->crtc, sr->timestamp,
                               0, 0, None, RR_Rotate_0, NULL, 0);
        if (ret == RRSetConfigSuccess) {
            wwarning("RandR: released CRTC for disconnected output %s", o->name);
            o->crtc = None;
            o->w = o->h = 0;
            deactivated = True;
        } else {
            wwarning("RandR: failed to release CRTC for output %s", o->name);
        }
    }

    /* Compact and shrink the virtual framebuffer to the bounding box of remaining active outputs */
    if (deactivated) {
        int min_x = scr->scr_width;
        int max_x = 0, max_y = 0;
        int j;

        for (i = 0; i < state->n_outputs; i++) {
            WRandROutput *a = &state->outputs[i];

            if (a->crtc == None || a->w == 0 || a->h == 0)
                continue;

            if (a->x < min_x) min_x = a->x;
            if (a->x + a->w > max_x) max_x = a->x + a->w;
            if (a->y + a->h > max_y) max_y = a->y + a->h;
        }

        /* Slide surviving outputs left to eliminate dead space at the origin */
        if (min_x > 0 && max_x > 0) {
            for (i = 0; i < state->n_outputs; i++) {
                WRandROutput *a = &state->outputs[i];
                XRRCrtcInfo *ci;

                if (a->crtc == None || a->w == 0 || a->h == 0)
                    continue;

                ci = XRRGetCrtcInfo(dpy, sr, a->crtc);
                if (!ci)
                    continue;

                XRRSetCrtcConfig(dpy, sr, a->crtc, sr->timestamp,
                                 a->x - min_x, a->y,
                                 ci->mode, ci->rotation,
                                 ci->outputs, ci->noutput);

                for (j = 0; j < state->n_outputs; j++) {
                    if (state->outputs[j].crtc == a->crtc)
                        state->outputs[j].x -= min_x;
                }

                XRRFreeCrtcInfo(ci);
            }
            max_x -= min_x;
        }

        /* Shrink the virtual framebuffer down to the compacted bounding box */
        if (max_x > 0 && max_y > 0 &&
            (max_x < scr->scr_width || max_y < scr->scr_height)) {
            int mm_w = (int)((long)WidthMMOfScreen(ScreenOfDisplay(dpy, scr->screen))
                              * max_x / scr->scr_width);
            int mm_h = (int)((long)HeightMMOfScreen(ScreenOfDisplay(dpy, scr->screen))
                              * max_y / scr->scr_height);
            XRRSetScreenSize(dpy, scr->root_win, max_x, max_y, mm_w, mm_h);
        }
    }

    return deactivated;
}

/* Auto-activate a newly connected output that has no CRTC assigned yet
 *
 * Mirrors "xrandr --auto": find a free CRTC, pick the preferred mode,
 * place the new head to the right of all active outputs,
 * expand the virtual framebuffer if needed, then call XRRSetCrtcConfig.
 *
 * Returns True if at least one output was activated */
static Bool wRandR_AutoActivate(WScreen *scr, WRandRState *state, XRRScreenResources *sr)
{
    Bool activated = False;
    int i, j;

    for (i = 0; i < state->n_outputs; i++) {
        WRandROutput *o = &state->outputs[i];
        XRROutputInfo *oi;
        RRCrtc free_crtc = None;
        Rotation rotation = RR_Rotate_0;
        RRMode best_mode = None;
        int mode_w = 0, mode_h = 0;
        int new_x = 0;
        int new_vw, new_vh;
        Status ret;

        /* Only process newly connected outputs that have no CRTC yet */
        if (!o->connected || o->crtc != None)
            continue;

        oi = XRRGetOutputInfo(dpy, sr, o->xid);
        if (!oi)
            continue;

        if (oi->ncrtc == 0 || oi->nmode == 0) {
            XRRFreeOutputInfo(oi);
            continue;
        }

        /* Find a free CRTC that is idle and capable of driving this output */
        for (j = 0; j < oi->ncrtc; j++) {
            XRRCrtcInfo *ci = XRRGetCrtcInfo(dpy, sr, oi->crtcs[j]);
            if (ci) {
                if (ci->noutput == 0) {
                    int k;

                    for (k = 0; k < (int)ci->npossible; k++) {
                        if (ci->possible[k] == o->xid) {
                            free_crtc = oi->crtcs[j];
                            if (ci->rotations & RR_Rotate_0)
                                rotation = RR_Rotate_0;
                            else if (ci->rotations & RR_Rotate_90)
                                rotation = RR_Rotate_90;
                            else if (ci->rotations & RR_Rotate_180)
                                rotation = RR_Rotate_180;
                            else
                                rotation = RR_Rotate_270;
                            break;
                        }
                    }
                }
                XRRFreeCrtcInfo(ci);
            }
            if (free_crtc != None)
                break;
        }

        if (free_crtc == None) {
            wwarning("RandR: no free CRTC for output %s", o->name);
            XRRFreeOutputInfo(oi);
            continue;
        }

        /* Preferred mode is first in the list (per RandR spec) */
        best_mode = oi->modes[0];
        XRRFreeOutputInfo(oi);

        /* Look up its pixel dimensions in sr->modes[] */
        for (j = 0; j < sr->nmode; j++) {
            if (sr->modes[j].id == best_mode) {
                mode_w = (int)sr->modes[j].width;
                mode_h = (int)sr->modes[j].height;
                break;
            }
        }

        if (mode_w == 0 || mode_h == 0)
            continue;

        /* Position it to the right of all currently active outputs, same as GNOME */
        for (j = 0; j < state->n_outputs; j++) {
            WRandROutput *a = &state->outputs[j];

            if (a->crtc != None && a->w > 0) {
                int right = a->x + a->w;

                if (right > new_x)
                    new_x = right;
            }
        }

        /* Expand virtual framebuffer if the new head exceeds current size */
        new_vw = new_x + mode_w;
        new_vh = mode_h > scr->scr_height ? mode_h : scr->scr_height;

        if (new_vw > scr->scr_width || new_vh > scr->scr_height) {
            int mm_w = (int)((long)WidthMMOfScreen(ScreenOfDisplay(dpy, scr->screen))
                          * new_vw / scr->scr_width);
            int mm_h = (int)((long)HeightMMOfScreen(ScreenOfDisplay(dpy, scr->screen))
                          * new_vh / scr->scr_height);
            XRRSetScreenSize(dpy, scr->root_win, new_vw, new_vh, mm_w, mm_h);
        }

        ret = XRRSetCrtcConfig(dpy, sr, free_crtc, sr->timestamp,
                               new_x, 0, best_mode, rotation,
                               &o->xid, 1);
        if (ret == RRSetConfigSuccess) {
            wwarning("RandR: auto-activated output %s at +%d+0", o->name, new_x);
            activated = True;
        } else {
            wwarning("RandR: failed to auto-activate output %s", o->name);
        }
    }

    return activated;
}

/* Synchronize RandR state with the X server */
static void wRandR_Update(WScreen *scr)
{
    WRandRState *state = (WRandRState *)scr->randr_state;
    char *old_name[WM_MAX_HEADS];
    int old_ws[WM_MAX_HEADS];
    int old_heads;
    int i, h, k;

    /* When HotplugMonitor is enabled, actively manage CRTC lifecycle.
     * This needs the full per-output/per-CRTC view (connection state,
     * CRTC binding, mode list) that GetMonitors does not expose, so
     * the resources walk is confined to this branch. */
    if (w_global.config->hotplug_monitor) {
        XRRScreenResources *sr;
        Bool changed;
        int j;

        /* Assume all outputs removed until server confirms */
        for (i = 0; i < state->n_outputs; i++)
            state->outputs[i].stale = True;

        sr = XRRGetScreenResourcesCurrent(dpy, scr->root_win);
        if (!sr) {
            wwarning("wRandR_Update: XRRGetScreenResourcesCurrent failed");
            return;
        }

        for (i = 0; i < sr->noutput; i++) {
            XRROutputInfo *oi;
            WRandROutput *found = NULL;

            oi = XRRGetOutputInfo(dpy, sr, sr->outputs[i]);
            if (!oi)
                continue;

            /* Match by XID (stable across reconfigures) */
            for (j = 0; j < state->n_outputs; j++) {
                if (state->outputs[j].xid == sr->outputs[i]) {
                    found = &state->outputs[j];
                    break;
                }
            }

            /* Append new output not seen before */
            if (!found && state->n_outputs < WM_MAX_HEADS) {
                found = &state->outputs[state->n_outputs++];
                memset(found, 0, sizeof(*found));
                found->xid = sr->outputs[i];
                wRandR_CopyOutputName(found, oi);
            }

            if (found) {
                found->stale = False;
                found->connected = (oi->connection == RR_Connected);
                found->crtc = oi->crtc;
                found->w = 0;
                found->h = 0;
                found->rotation = RR_Rotate_0;

                if (oi->crtc != None) {
                    XRRCrtcInfo *ci = XRRGetCrtcInfo(dpy, sr, oi->crtc);
                    if (ci) {
                        found->x = ci->x;
                        found->y = ci->y;
                        found->w = ci->width;
                        found->h = ci->height;
                        found->rotation = ci->rotation;
                        XRRFreeCrtcInfo(ci);
                    }
                }
            }

            XRRFreeOutputInfo(oi);
        }

        changed  = wRandR_AutoDeactivate(scr, state, sr);
        changed |= wRandR_AutoActivate(scr, state, sr);

        XRRFreeScreenResources(sr);

        /* Remove outputs not reported by server at all */
        for (i = 0, j = 0; i < state->n_outputs; i++) {
            if (!state->outputs[i].stale) {
                if (j != i)
                    state->outputs[j] = state->outputs[i];
                j++;
            }
        }
        state->n_outputs = j;

        /* Our SetCrtcConfig calls generate a fresh RandR event burst,
         * the debounced re-entry will rebuild the head list then */
        if (changed)
            return;
    }

    /* Snapshot per-head workspace by output name before head indices
     * are reordered, so each monitor keeps its workspace after the
     * geometry rebuild. */
    old_heads = wHeadCount(scr);
    for (h = 0; h < old_heads; h++) {
        old_name[h] = scr->head_output_name[h]
            ? wstrdup(scr->head_output_name[h]) : NULL;
        old_ws[h] = scr->head_workspace[h];
    }

    /* Apply new geometry to the Xinerama head layer */
    i = scr->xine_info.primary_head;
    if (!wRandR_ApplyToXinerama(scr)) {
        /* Stub-RandR fallback kept the XINERAMA head list, nothing
         * actually moved, GetMonitors on such servers also makes them
         * emit spurious change events so do not let those churn the
         * per-head workspace map or rearrange icons. */
        for (h = 0; h < old_heads; h++)
            if (old_name[h])
                wfree(old_name[h]);
        return;
    }

    if (w_global.config->per_head_workspaces) {
        int new_ws[WM_MAX_HEADS];

        for (h = 0; h < wHeadCount(scr); h++) {
            new_ws[h] = -1;
            if (scr->head_output_name[h]) {
                for (k = 0; k < old_heads; k++) {
                    if (old_name[k]
                        && strcmp(old_name[k], scr->head_output_name[h]) == 0) {
                        new_ws[h] = old_ws[k];
                        break;
                    }
                }
            }
        }
        for (h = 0; h < wHeadCount(scr); h++)
            scr->head_workspace[h] = new_ws[h];
        for (h = 0; h < wHeadCount(scr); h++) {
            if (scr->head_workspace[h] < 0
                || scr->head_workspace[h] >= scr->workspace_count
                || !wWorkspaceIsAllowedOnHead(scr, scr->head_workspace[h], h))
                scr->head_workspace[h] = wWorkspaceDefaultForHead(scr, h);
        }
        if (scr->last_active_head >= wHeadCount(scr))
            scr->last_active_head = scr->xine_info.primary_head;
        scr->current_workspace = scr->head_workspace[scr->last_active_head];
    }

    for (h = 0; h < old_heads; h++)
        if (old_name[h])
            wfree(old_name[h]);

    wHotCornersUpdate(scr);

    /* Move the dock if needed */
    if (scr->dock &&
        ((scr->xine_info.primary_head != i && w_global.config->keep_dock_on_primary_head) ||
         scr->dock->x_pos < 0 ||
         scr->dock->x_pos >= scr->scr_width))
        wDockSwap(scr->dock);

    /* Snap each workspace clip back onto a visible head if it ended up
     * outside the (now smaller or rearranged) virtual framebuffer */
    if (!wPreferences.flags.noclip) {
        for (k = 0; k < scr->workspace_count; k++) {
            WDock *clip = scr->workspaces[k]->clip;

            if (clip)
                wClipSnapToHead(clip);
        }
    }

    /* Refresh usable areas and EWMH hints */
    wScreenUpdateUsableArea(scr);
    wNETWMUpdateWorkarea(scr);

    /* Bring any windows that ended up in dead space to an active head */
    wRandR_BringAllWindowsInside(scr);

    /* Reconcile per-head visibility now that head_workspace[] has been
     * remapped and windows have snapped to their new heads. */
    if (w_global.config->per_head_workspaces) {
        WWindow *wwin;
        for (wwin = scr->focused_window; wwin; wwin = wwin->prev)
            wwin->last_head = wGetHeadForWindow(wwin);
        wWorkspaceReconcileHeads(scr);
        wWorkspaceSetCurrentHead(scr, wGetActiveHead(scr));
    }

    /* Rearrange miniaturized and appicons into the icon yard */
    wArrangeIcons(scr, True);
}

static void wRandRDebounceTimerFired(void *data)
{
    WScreen *scr = (WScreen *)data;

    scr->randr_debounce_timer = NULL;
    wRandR_Update(scr);
}

/* end of local stuff */

void wRandRInit(WScreen *scr)
{
    WRandRState *state;

    if (!w_global.xext.randr.supported)
        return;

    state = wmalloc(sizeof(WRandRState));
    memset(state, 0, sizeof(*state));
    scr->randr_state = state;

    wRandR_ApplyToXinerama(scr);
}

void wRandRTeardown(WScreen *scr)
{
    if (!scr->randr_state)
        return;

    if (scr->randr_debounce_timer) {
        WMDeleteTimerHandler(scr->randr_debounce_timer);
        scr->randr_debounce_timer = NULL;
    }

    wClearHeadOutputNames(scr);
    wfree(scr->randr_state);
    scr->randr_state = NULL;
}

void wRandRHandleNotify(WScreen *scr)
{
    if (!scr || !scr->randr_state)
        return;

    /* XRRUpdateConfiguration() only mutated libX11's cached Screen
     * width/height which wRandR_Update() never reads (it queries
     * monitors directly) and which is unreliable under nested servers
     * anyway, so the wire event has no useful payload here and the
     * notify only rearms the debounce */

    /* Debounce: cancel any pending timer, then restart it */
    if (scr->randr_debounce_timer) {
        WMDeleteTimerHandler(scr->randr_debounce_timer);
        scr->randr_debounce_timer = NULL;
    }
    scr->randr_debounce_timer =
        WMAddTimerHandler(RANDR_DEBOUNCE_DELAY, wRandRDebounceTimerFired, scr);
}

#endif /* USE_RANDR */
