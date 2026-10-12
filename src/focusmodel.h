/*
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

#ifndef WMFOCUSMODEL_H_
#define WMFOCUSMODEL_H_

#include "window.h"
#include "xinerama.h"

/*
 * Pure focus policy core, extracted from actions.c. The applier scans
 * for the modal transient, builds the inputs from the final target and
 * applies the plan (input focus, protocol, list reorder, colormap).
 */
typedef enum {
	WFOCUS_TARGET_CLIENT,	/* xcb_set_input_focus on wwin->client_win */
	WFOCUS_TARGET_NONEWIN,	/* xcb_set_input_focus on scr->no_focus_win */
	WFOCUS_TARGET_NOTHING	/* WFM_GLOBALLY_ACTIVE: protocol only */
} WFocusTarget;

/* the final focus target, pre-reduced by the applier after the modal
 * transient redirect. A NULL target means an untargeted call */
typedef struct {
	WWindow *win;		/* opaque handle, used for reorder_list */
	bool focusable;		/* WFLAGP(no_focusable) */
	int focus_mode;		/* WFM_* */
	bool takes_focus_proto;	/* protocols.TAKE_FOCUS */
	bool mapped;
} WFocusWinIn;

typedef struct {
	WFocusTarget target;
	bool send_take_focus;
	bool focus_path;	/* the mapped && !no_focusable branch */
	bool reorder_list;	/* focused != target */
	bool redirect_to;	/* modal transient to focus instead */
	WWindow *redirect_win;
} WFocusPlan;

void wFocusResolve(const WFocusWinIn *target, WWindow *focused,
		   bool has_modal_transient_for, WWindow *modal_transient,
		   WFocusPlan *out);

/* directional focus candidate scoring over a pre-reduced candidate
 * list (centre coordinates relative to the focused window's centre),
 * returning the winning window or NULL */
typedef struct {
	WWindow *win;		/* opaque handle, returned on match */
	int rel_cx, rel_cy;
} WFocusDirCandidate;

WWindow *wFocusNearestInDirection(const WFocusDirCandidate *cands, int n,
				  int direction);
#endif
