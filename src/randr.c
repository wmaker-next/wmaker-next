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

#include <stdlib.h>
#include <string.h>
#include <xcb/randr.h>

#include "randr.h"
#include "window.h"
#include "defaults.h"
#include "framewin.h"
#include "xinerama.h"
#include "wmspec.h"
#include "dock.h"
#include "workspace.h"
#include "actions.h"
#include "event.h"
#include "xsettings.h"
#include "hotcorner.h"
#ifdef VIRTUAL_DESKTOP
#include "vdesk.h"
#endif

#define RANDR_DEBOUNCE_DELAY  100   /* milliseconds */

typedef struct {
	xcb_randr_output_t xid;
	xcb_randr_crtc_t crtc;
	int x, y, w, h;
	uint16_t rotation;     /* current CRTC rotation */
	bool connected;        /* RR_Connected */
	bool stale;            /* mark-scan scratch flag */
	char name[64];
} WRandROutput;

typedef struct {
	WRandROutput outputs[WM_MAX_HEADS];
	int n_outputs;
	/* raw GetMonitors active count from the last
	 * wRandR_ApplyToXinerama() call, before the zero-heads
	 * fallback synthesizes a screen-sized head, -1 when the
	 * GetMonitors request itself failed, used by wRandR_Update()
	 * to drive wDisplaySetOff() */
	int active_monitors;
} WRandRState;

typedef struct {
	xcb_randr_get_screen_resources_current_reply_t *reply;
	xcb_timestamp_t timestamp;
	xcb_randr_output_t *outputs;
	int noutput;
	xcb_randr_mode_info_t *modes;
	int nmode;
} WRandRResources;

/* GetOutputInfo's name field is not NUL-terminated, copy and terminate */
static void copyOutputName(WRandROutput *o, const char *name, int len)
{
	size_t n = (size_t)len;

	if (n >= sizeof(o->name))
		n = sizeof(o->name) - 1;
	memcpy(o->name, name, n);
	o->name[n] = '\0';
}

static bool getResources(WScreen *scr, WRandRResources *sr)
{
	sr->reply = xcb_randr_get_screen_resources_current_reply(wdisp->conn,
			xcb_randr_get_screen_resources_current(wdisp->conn, scr->root_win), NULL);
	if (!sr->reply)
		return false;

	sr->timestamp = sr->reply->config_timestamp;
	sr->outputs = xcb_randr_get_screen_resources_current_outputs(sr->reply);
	sr->noutput = xcb_randr_get_screen_resources_current_outputs_length(sr->reply);
	sr->modes = xcb_randr_get_screen_resources_current_modes(sr->reply);
	sr->nmode = xcb_randr_get_screen_resources_current_modes_length(sr->reply);
	return true;
}

static int setCrtcConfig(WRandRResources *sr, xcb_randr_crtc_t crtc,
						 int x, int y, xcb_randr_mode_t mode,
						 uint16_t rotation, xcb_randr_output_t *outputs, int noutput)
{
	xcb_randr_set_crtc_config_reply_t *r;
	int status;

	r = xcb_randr_set_crtc_config_reply(wdisp->conn,
			xcb_randr_set_crtc_config(wdisp->conn, crtc, XCB_CURRENT_TIME,
									  sr->timestamp, x, y, mode, rotation,
									  noutput, outputs), NULL);
	status = r ? r->status : XCB_RANDR_SET_CONFIG_FAILED;
	free(r);
	return status;
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
static bool wRandR_ApplyToXinerama(WScreen *scr)
{
	WMRect new_screens[WM_MAX_HEADS];
	char name_buf[WM_MAX_HEADS][64];
	const char *new_names[WM_MAX_HEADS];   /* parallel: monitor name of each head */
	xcb_atom_t name_atom[WM_MAX_HEADS];
	xcb_get_atom_name_cookie_t name_ck[WM_MAX_HEADS];
	xcb_get_atom_name_reply_t *name_rep;
	xcb_randr_get_monitors_reply_t *mr;
	xcb_randr_monitor_info_iterator_t mi;
	xcb_get_geometry_reply_t *gr;
	size_t nlen;
	int count = 0;
	int primary = 0;
	int old_count;
	int root_w, root_h;
	int max_x, max_y;
	int i;

	mr = xcb_randr_get_monitors_reply(wdisp->conn,
			xcb_randr_get_monitors(wdisp->conn, scr->root_win, 1), NULL);

	if (scr->randr_state)
		((WRandRState *)scr->randr_state)->active_monitors = mr ? 0 : -1;

	if (mr) {
		for (mi = xcb_randr_get_monitors_monitors_iterator(mr);
			 mi.rem && count < WM_MAX_HEADS;
			 xcb_randr_monitor_info_next(&mi)) {
			if (mi.data->width <= 0 || mi.data->height <= 0)
				continue;

			new_screens[count].pos.x = mi.data->x;
			new_screens[count].pos.y = mi.data->y;
			new_screens[count].size.width = mi.data->width;
			new_screens[count].size.height = mi.data->height;
			name_atom[count] = mi.data->name;

			if (mi.data->primary)
				primary = count;

			count++;
		}
		free(mr);
		if (scr->randr_state)
			((WRandRState *)scr->randr_state)->active_monitors = count;
	}

	/* Resolve every monitor-name atom in one round-trip rather than one
	 * GetAtomName per head. */
	for (i = 0; i < count; i++) {
		name_ck[i].sequence = 0;
		if (name_atom[i] != XCB_ATOM_NONE)
			name_ck[i] = xcb_get_atom_name(wdisp->conn, name_atom[i]);
	}
	xcb_flush(wdisp->conn);
	for (i = 0; i < count; i++) {
		name_buf[i][0] = '\0';
		name_rep = name_ck[i].sequence
				 ? xcb_get_atom_name_reply(wdisp->conn, name_ck[i], NULL) : NULL;
		if (name_rep) {
			nlen = xcb_get_atom_name_name_length(name_rep);
			if (nlen >= sizeof(name_buf[i]))
				nlen = sizeof(name_buf[i]) - 1;
			memcpy(name_buf[i], xcb_get_atom_name_name(name_rep), nlen);
			name_buf[i][nlen] = '\0';
			free(name_rep);
		}
		new_names[i] = name_buf[i][0] ? name_buf[i] : NULL;
	}

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
	gr = xcb_get_geometry_reply(wdisp->conn,
			xcb_get_geometry(wdisp->conn, scr->root_win), NULL);
	if (gr) {
		root_w = gr->width;
		root_h = gr->height;
		free(gr);
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
		return false;
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

	return true;
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

		bw = HAS_BORDER(wwin) ? wFrameBorderWidth(wwin) : 0;
		wx = wwin->frame_x;
		wy = wwin->frame_y;
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
 * We must call SetCrtcConfig with mode=None to release it, which causes
 * the server to fire a fresh batch of RandR events.
 *
 * Returns True if at least one CRTC was freed */
static bool wRandR_AutoDeactivate(WScreen *scr, WRandRState *state, WRandRResources *sr)
{
	bool deactivated = false;
	int i;

	for (i = 0; i < state->n_outputs; i++) {
		WRandROutput *o = &state->outputs[i];

		if (o->connected || o->crtc == XCB_NONE || o->stale)
			continue;

		/* Release the CRTC: no mode, no outputs */
		if (setCrtcConfig(sr, o->crtc, 0, 0, XCB_NONE,
						  XCB_RANDR_ROTATION_ROTATE_0, NULL, 0)
			== XCB_RANDR_SET_CONFIG_SUCCESS) {
			wwarning("RandR: released CRTC for disconnected output %s", o->name);
			o->crtc = XCB_NONE;
			o->w = o->h = 0;
			deactivated = true;
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

			if (a->crtc == XCB_NONE || a->w == 0 || a->h == 0)
				continue;

			if (a->x < min_x)
				min_x = a->x;
			if (a->x + a->w > max_x)
				max_x = a->x + a->w;
			if (a->y + a->h > max_y)
				max_y = a->y + a->h;
		}

		/* Slide surviving outputs left to eliminate dead space at the origin */
		if (min_x > 0 && max_x > 0) {
			for (i = 0; i < state->n_outputs; i++) {
				WRandROutput *a = &state->outputs[i];
				xcb_randr_get_crtc_info_reply_t *ci;

				if (a->crtc == XCB_NONE || a->w == 0 || a->h == 0)
					continue;

				ci = xcb_randr_get_crtc_info_reply(wdisp->conn,
						xcb_randr_get_crtc_info(wdisp->conn, a->crtc, sr->timestamp), NULL);
				if (!ci)
					continue;

				setCrtcConfig(sr, a->crtc, a->x - min_x, a->y,
							  ci->mode, ci->rotation,
							  xcb_randr_get_crtc_info_outputs(ci),
							  xcb_randr_get_crtc_info_outputs_length(ci));

				for (j = 0; j < state->n_outputs; j++) {
					if (state->outputs[j].crtc == a->crtc)
						state->outputs[j].x -= min_x;
				}

				free(ci);
			}
			max_x -= min_x;
		}

		/* Shrink the virtual framebuffer down to the compacted bounding box */
		if (max_x > 0 && max_y > 0 &&
			(max_x < scr->scr_width || max_y < scr->scr_height)) {
			int mm_w = (int)((long)wXScreenOf(wdisp->conn, scr->screen)->width_in_millimeters
							  * max_x / scr->scr_width);
			int mm_h = (int)((long)wXScreenOf(wdisp->conn, scr->screen)->height_in_millimeters
							  * max_y / scr->scr_height);
			xcb_generic_error_t *error;

			if (mm_w < 1)
				mm_w = 1;
			if (mm_h < 1)
				mm_h = 1;
			error = xcb_request_check(wdisp->conn,
					xcb_randr_set_screen_size_checked(wdisp->conn, scr->root_win,
								      max_x, max_y, mm_w, mm_h));
			if (error) {
				wwarning("RandR: failed to shrink framebuffer to %dx%d (X error %u)",
					 max_x, max_y, error->error_code);
				free(error);
			}
		}
	}

	return deactivated;
}

/* Auto-activate a newly connected output that has no CRTC assigned yet
 *
 * Mirrors "xrandr --auto": find a free CRTC, pick the preferred mode,
 * place the new head to the right of all active outputs,
 * expand the virtual framebuffer if needed, then SetCrtcConfig.
 *
 * Returns True if at least one output was activated */
static bool wRandR_AutoActivate(WScreen *scr, WRandRState *state, WRandRResources *sr)
{
	int i, j;

	for (i = 0; i < state->n_outputs; i++) {
		WRandROutput *o = &state->outputs[i];
		xcb_randr_get_output_info_reply_t *oi;
		xcb_randr_crtc_t *oi_crtcs;
		xcb_randr_mode_t *oi_modes;
		xcb_randr_crtc_t free_crtc = XCB_NONE;
		uint16_t rotation = XCB_RANDR_ROTATION_ROTATE_0;
		xcb_randr_mode_t best_mode = XCB_NONE;
		int oi_ncrtc, oi_nmode;
		int mode_w = 0, mode_h = 0;
		int new_x = 0;
		int new_vw, new_vh;
		xcb_get_geometry_reply_t *geometry;
		xcb_randr_get_screen_size_range_reply_t *range;
		xcb_generic_error_t *error;

		/* Only process newly connected outputs that have no CRTC yet */
		if (!o->connected || o->crtc != XCB_NONE || o->stale)
			continue;

		oi = xcb_randr_get_output_info_reply(wdisp->conn,
				xcb_randr_get_output_info(wdisp->conn, o->xid, sr->timestamp), NULL);
		if (!oi || oi->status != XCB_RANDR_SET_CONFIG_SUCCESS) {
			wwarning("RandR: could not query output %s for activation", o->name);
			free(oi);
			continue;
		}

		oi_crtcs = xcb_randr_get_output_info_crtcs(oi);
		oi_ncrtc = xcb_randr_get_output_info_crtcs_length(oi);
		oi_modes = xcb_randr_get_output_info_modes(oi);
		oi_nmode = xcb_randr_get_output_info_modes_length(oi);

		if (oi_ncrtc == 0 || oi_nmode == 0) {
			free(oi);
			continue;
		}

		/* Find a free CRTC that is idle and capable of driving this output */
		for (j = 0; j < oi_ncrtc; j++) {
			xcb_randr_get_crtc_info_reply_t *ci;
			xcb_randr_output_t *poss;
			int npos, k;

			ci = xcb_randr_get_crtc_info_reply(wdisp->conn,
					xcb_randr_get_crtc_info(wdisp->conn, oi_crtcs[j], sr->timestamp), NULL);
			if (!ci || ci->status != XCB_RANDR_SET_CONFIG_SUCCESS) {
				wwarning("RandR: could not query CRTC for output %s", o->name);
				free(ci);
				continue;
			}

			if (xcb_randr_get_crtc_info_outputs_length(ci) == 0) {
				poss = xcb_randr_get_crtc_info_possible(ci);
				npos = xcb_randr_get_crtc_info_possible_length(ci);
				for (k = 0; k < npos; k++) {
					if (poss[k] == o->xid) {
						if (ci->rotations & XCB_RANDR_ROTATION_ROTATE_0)
							rotation = XCB_RANDR_ROTATION_ROTATE_0;
						else if (ci->rotations & XCB_RANDR_ROTATION_ROTATE_90)
							rotation = XCB_RANDR_ROTATION_ROTATE_90;
						else if (ci->rotations & XCB_RANDR_ROTATION_ROTATE_180)
							rotation = XCB_RANDR_ROTATION_ROTATE_180;
						else if (ci->rotations & XCB_RANDR_ROTATION_ROTATE_270)
							rotation = XCB_RANDR_ROTATION_ROTATE_270;
						else
							continue;
						free_crtc = oi_crtcs[j];
						break;
					}
				}
			}
			free(ci);
			if (free_crtc != XCB_NONE)
				break;
		}

		if (free_crtc == XCB_NONE) {
			wwarning("RandR: no free CRTC for output %s", o->name);
			free(oi);
			continue;
		}

		/* Preferred mode is first in the list (per RandR spec) */
		best_mode = oi_modes[0];
		free(oi);

		/* Look up its pixel dimensions in sr->modes[] */
		for (j = 0; j < sr->nmode; j++) {
			if (sr->modes[j].id == best_mode) {
				mode_w = sr->modes[j].width;
				mode_h = sr->modes[j].height;
				break;
			}
		}

		if (mode_w == 0 || mode_h == 0)
			continue;

		/* Position it to the right of all currently active outputs */
		for (j = 0; j < state->n_outputs; j++) {
			WRandROutput *a = &state->outputs[j];

			if (!a->stale && a->crtc != XCB_NONE && a->w > 0) {
				int right = a->x + a->w;

				if (right > new_x)
					new_x = right;
			}
		}

		/* Expand virtual framebuffer if the new head exceeds current size */
		geometry = xcb_get_geometry_reply(wdisp->conn,
				xcb_get_geometry(wdisp->conn, scr->root_win), NULL);
		range = xcb_randr_get_screen_size_range_reply(wdisp->conn,
				xcb_randr_get_screen_size_range(wdisp->conn, scr->root_win), NULL);
		if (!geometry || !range || geometry->width == 0 || geometry->height == 0) {
			wwarning("RandR: could not query screen geometry and limits for output %s", o->name);
			free(geometry);
			free(range);
			continue;
		}
		if (!wRandRScreenSizeForMode(mode_w, mode_h, rotation, new_x,
					     geometry->width, geometry->height,
					     range->min_width, range->min_height,
					     range->max_width, range->max_height,
					     &new_vw, &new_vh)) {
			wwarning("RandR: output %s would exceed the maximum screen size %dx%d",
				 o->name, range->max_width, range->max_height);
			free(geometry);
			free(range);
			continue;
		}
		free(range);

		if (new_vw > geometry->width || new_vh > geometry->height) {
			int mm_w = (int)((long)wXScreenOf(wdisp->conn, scr->screen)->width_in_millimeters
						  * new_vw / geometry->width);
			int mm_h = (int)((long)wXScreenOf(wdisp->conn, scr->screen)->height_in_millimeters
						  * new_vh / geometry->height);
			if (mm_w < 1)
				mm_w = 1;
			if (mm_h < 1)
				mm_h = 1;
			error = xcb_request_check(wdisp->conn,
					xcb_randr_set_screen_size_checked(wdisp->conn, scr->root_win,
								      new_vw, new_vh, mm_w, mm_h));
			if (error) {
				wwarning("RandR: failed to resize framebuffer for output %s (X error %u)",
					 o->name, error->error_code);
				free(error);
				free(geometry);
				continue;
			}
		}
		free(geometry);

		if (setCrtcConfig(sr, free_crtc, new_x, 0, best_mode, rotation,
						  &o->xid, 1) == XCB_RANDR_SET_CONFIG_SUCCESS) {
			wwarning("RandR: auto-activated output %s at +%d+0", o->name, new_x);
			return true;
		} else {
			wwarning("RandR: failed to auto-activate output %s", o->name);
		}
	}

	return false;
}

static void wRandRDebounceTimerFired(void *data);

/* Synchronize RandR state with the X server */
static void wRandR_Update(WScreen *scr)
{
	WRandRState *state = (WRandRState *)scr->randr_state;
	char *old_name[WM_MAX_HEADS];
	int old_ws[WM_MAX_HEADS];
	WMRect old_screens[WM_MAX_HEADS];
	bool applied, geometry_changed;
	int old_heads;
	int old_width, old_height;
	int i, h, k;
#ifdef VIRTUAL_DESKTOP
	int ws;
#endif

	/* When HotplugMonitor is enabled, actively manage CRTC lifecycle.
	 * This needs the full per-output/per-CRTC view (connection state,
	 * CRTC binding, mode list) that GetMonitors does not expose, so
	 * the resources walk is confined to this branch. */
	if (w_global.config->hotplug_monitor) {
		WRandRResources sr;
		bool changed = false;
		bool snapshot_complete = true;
		int j;

		/* Assume all outputs removed until server confirms */
		for (i = 0; i < state->n_outputs; i++)
			state->outputs[i].stale = true;

		if (!getResources(scr, &sr)) {
			wwarning("wRandR_Update: GetScreenResourcesCurrent failed");
			return;
		}

		for (i = 0; i < sr.noutput; i++) {
			xcb_randr_get_output_info_reply_t *oi;
			WRandROutput *found = NULL;

			oi = xcb_randr_get_output_info_reply(wdisp->conn,
					xcb_randr_get_output_info(wdisp->conn, sr.outputs[i], sr.timestamp), NULL);
			if (!oi || oi->status != XCB_RANDR_SET_CONFIG_SUCCESS) {
				wwarning("RandR: could not query output 0x%x", sr.outputs[i]);
				free(oi);
				snapshot_complete = false;
				continue;
			}

			/* Match by XID (stable across reconfigures) */
			for (j = 0; j < state->n_outputs; j++) {
				if (state->outputs[j].xid == sr.outputs[i]) {
					found = &state->outputs[j];
					break;
				}
			}

			/* Append new output not seen before */
			if (!found && state->n_outputs < WM_MAX_HEADS) {
				found = &state->outputs[state->n_outputs++];
				memset(found, 0, sizeof(*found));
				found->xid = sr.outputs[i];
				copyOutputName(found,
							   (const char *)xcb_randr_get_output_info_name(oi),
							   xcb_randr_get_output_info_name_length(oi));
			}

			if (!found) {
				wwarning("RandR: too many outputs to manage safely");
				snapshot_complete = false;
			}
			if (found) {
				found->stale = false;
				found->connected = (oi->connection == XCB_RANDR_CONNECTION_CONNECTED);
				found->crtc = oi->crtc;
				found->w = 0;
				found->h = 0;
				found->rotation = XCB_RANDR_ROTATION_ROTATE_0;

				if (oi->crtc != XCB_NONE) {
					xcb_randr_get_crtc_info_reply_t *ci;

					ci = xcb_randr_get_crtc_info_reply(wdisp->conn,
							xcb_randr_get_crtc_info(wdisp->conn, oi->crtc, sr.timestamp), NULL);
					if (ci && ci->status == XCB_RANDR_SET_CONFIG_SUCCESS) {
						found->x = ci->x;
						found->y = ci->y;
						found->w = ci->width;
						found->h = ci->height;
						found->rotation = ci->rotation;
						free(ci);
					} else {
						wwarning("RandR: could not query CRTC for output %s",
							 found->name);
						free(ci);
						snapshot_complete = false;
					}
				}
			}

			free(oi);
		}

		if (snapshot_complete) {
			changed = wRandR_AutoDeactivate(scr, state, &sr);
			if (!changed)
				changed = wRandR_AutoActivate(scr, state, &sr);
		}

		free(sr.reply);

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
		if (changed) {
			WMDeleteTimerHandlerPtr(&scr->randr_debounce_timer);
			scr->randr_debounce_timer =
				WMAddTimerHandler(RANDR_DEBOUNCE_DELAY, wRandRDebounceTimerFired, scr);
			return;
		}
	}

	/* Snapshot per-head workspace by output name before head indices
	 * are reordered, so each monitor keeps its workspace after the
	 * geometry rebuild. */
	old_heads = wHeadCount(scr);
	old_width = scr->scr_width;
	old_height = scr->scr_height;
	for (h = 0; h < old_heads; h++) {
		old_screens[h] = wGetRectForHead(scr, h);
		old_name[h] = scr->head_output_name[h]
			? wstrdup(scr->head_output_name[h]) : NULL;
		old_ws[h] = scr->head_workspace[h];
	}

	/* Apply new geometry to the Xinerama head layer */
	i = scr->xine_info.primary_head;
	applied = wRandR_ApplyToXinerama(scr);

	/* GetMonitors with get_active=1 returning zero entries means no
	 * CRTC is driving an output, the framebuffer has no viewer.
	 * That is the lid-closed-with-no-external-display case, mark
	 * the display off so the decorative timers disarm. When the
	 * request itself failed (active_monitors == -1) leave the
	 * display state alone. This runs for both the applied and the
	 * stub-fallback branch because the all-off case is the one that
	 * usually takes the stub branch (xine_info still remembers the
	 * pre-disconnect head). */
	if (state->active_monitors >= 0)
		wDisplaySetOff(state->active_monitors == 0,
			       WDisplayPowerSourceRandR);

	if (!applied) {
		/* Stub-RandR fallback kept the XINERAMA head list, nothing
		 * actually moved, GetMonitors on such servers also makes them
		 * emit spurious change events so do not let those churn the
		 * per-head workspace map or rearrange icons. */
		for (h = 0; h < old_heads; h++)
			if (old_name[h])
				wfree(old_name[h]);
		return;
	}

	geometry_changed = old_width != scr->scr_width || old_height != scr->scr_height ||
			   old_heads != wHeadCount(scr);
	if (!geometry_changed) {
		for (h = 0; h < old_heads; h++) {
			WMRect current = wGetRectForHead(scr, h);

			if (old_screens[h].pos.x != current.pos.x ||
			    old_screens[h].pos.y != current.pos.y ||
			    old_screens[h].size.width != current.size.width ||
			    old_screens[h].size.height != current.size.height) {
				geometry_changed = true;
				break;
			}
		}
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

#ifdef VIRTUAL_DESKTOP
	wWorkspaceUpdateEdge(scr);
#endif

	/* Move the dock if needed */
	if (scr->dock) {
		/* grow the icon arrays so the freed along-axis space on
		 * a screen growth can be filled with new icons, this
		 * also re-pins the edge axis to the new geometry */
		wDockApplyIconSize(scr);
		if ((scr->xine_info.primary_head != i && w_global.config->keep_dock_on_primary_head) ||
		    scr->dock->x_pos < 0 ||
		    scr->dock->x_pos + w_global.config->icon_size > scr->scr_width ||
		    scr->dock->y_pos < 0 ||
		    scr->dock->y_pos + w_global.config->icon_size > scr->scr_height)
			wDockReanchor(scr->dock);
	}

	/* Snap each workspace clip back onto a visible head if it ended up
	 * outside the (now smaller or rearranged) virtual framebuffer */
	if (!wdisp->flags.noclip) {
		for (k = 0; k < scr->workspace_count; k++) {
			WDock *clip = scr->workspaces[k]->clip;

			if (clip)
				wClipSnapToHead(clip);
		}
	}

	/* Refresh usable areas and EWMH hints. The desktop geometry is
	 * the framebuffer size, so it changes with the RandR mode too;
	 * _NET_WORKAREA alone would leave clients tracking the old size.
	 * With the virtual desktop on the hint comes from the per-
	 * workspace extent instead, which the mode change does not
	 * recompute. */
	wScreenUpdateUsableArea(scr);
#ifdef VIRTUAL_DESKTOP
	if (w_global.config->vdesk_enable) {
		for (ws = 0; ws < scr->workspace_count; ws++)
			wVdeskUpdateGeometry(scr, ws);
	}
#endif
	wNETWMUpdateDesktop(scr);
	wNETWMUpdateWorkarea(scr);

	/* head geometry changed, republish XSETTINGS (a no-op when the
	 * values did not move with the geometry) */
	wXSettingsRefresh(scr);

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

	if (geometry_changed)
		wDefaultRefreshWorkspaceBackground(scr);

	/* Rearrange miniaturized and appicons into the icon yard */
	wArrangeIcons(scr, true);
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

	if (!wdisp->xext.randr.supported)
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

	WMDeleteTimerHandlerPtr(&scr->randr_debounce_timer);

	wClearHeadOutputNames(scr);
	wfree(scr->randr_state);
	scr->randr_state = NULL;
}

void wRandRHandleNotify(WScreen *scr)
{
	if (!scr || !scr->randr_state)
		return;

	/* the wire event has no payload wRandR_Update() needs (it queries
	 * monitors directly) and nested servers fire spurious notifies, so
	 * the notify only rearms the debounce */
	WMDeleteTimerHandlerPtr(&scr->randr_debounce_timer);
	scr->randr_debounce_timer =
		WMAddTimerHandler(RANDR_DEBOUNCE_DELAY, wRandRDebounceTimerFired, scr);
}

#endif /* USE_RANDR */
