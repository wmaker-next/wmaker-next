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

/*
 * Pure focus policy: the focus_mode dispatch deciding whether input
 * focus goes to the client, to no_focus_win or nowhere, the
 * TAKE_FOCUS protocol decision, and the directional focus scoring.
 */

#include "wconfig.h"

#include "focusmodel.h"

/* reduce a live window into the decision inputs, used for the modal
 * transient the applier found, whose fields the requested-target input
 * does not describe */
static void reduce_target(WWindow *wwin, WFocusWinIn *in)
{
	in->win = wwin;
	in->focusable = WFLAGP(wwin, no_focusable);
	in->focus_mode = wwin->focus_mode;
	in->takes_focus_proto = wwin->protocols.TAKE_FOCUS;
	in->mapped = wwin->flags.mapped;
}

void wFocusResolve(const WFocusWinIn *target, WWindow *focused,
		   bool has_modal_transient_for, WWindow *modal_transient,
		   WFocusPlan *out)
{
	WFocusWinIn red;

	if (target == NULL) {
		/* untargeted call: the no-focus window gets input focus
		 * and the focused list is not reordered */
		out->target = WFOCUS_TARGET_NONEWIN;
		out->send_take_focus = false;
		out->focus_path = false;
		out->reorder_list = false;
		out->redirect_to = false;
		out->redirect_win = NULL;
		return;
	}

	out->redirect_to = has_modal_transient_for;
	out->redirect_win = has_modal_transient_for ? modal_transient : NULL;

	/* the modal transient replaces the target for the dispatch */
	if (out->redirect_to) {
		reduce_target(modal_transient, &red);
		target = &red;
	}

	out->focus_path = target->mapped && !target->focusable;

	if (out->focus_path) {
		switch (target->focus_mode) {
		case WFM_NO_INPUT:
			out->target = WFOCUS_TARGET_NONEWIN;
			break;
		case WFM_PASSIVE:
		case WFM_LOCALLY_ACTIVE:
			out->target = WFOCUS_TARGET_CLIENT;
			break;
		case WFM_GLOBALLY_ACTIVE:
			out->target = WFOCUS_TARGET_NOTHING;
			break;
		}
		out->send_take_focus = target->takes_focus_proto;
	} else {
		out->target = WFOCUS_TARGET_NONEWIN;
		out->send_take_focus = false;
	}

	out->reorder_list = (target->win != focused);
}

WWindow *wFocusNearestInDirection(const WFocusDirCandidate *cands, int n,
				  int direction)
{
	const WFocusDirCandidate *best = NULL;
	long best_score = -1;
	int i;

	for (i = 0; i < n; i++) {
		int his_cx = cands[i].rel_cx;
		int his_cy = cands[i].rel_cy;
		int distance, offset;
		long score;

		switch (direction) {
		case DIRECTION_RIGHT:
			distance = his_cx;
			offset = his_cy < 0 ? -his_cy : his_cy;
			break;
		case DIRECTION_LEFT:
			distance = -his_cx;
			offset = his_cy < 0 ? -his_cy : his_cy;
			break;
		case DIRECTION_DOWN:
			distance = his_cy;
			offset = his_cx < 0 ? -his_cx : his_cx;
			break;
		case DIRECTION_UP:
			distance = -his_cy;
			offset = his_cx < 0 ? -his_cx : his_cx;
			break;
		default:
			continue;
		}

		/* candidate must be strictly in the requested direction */
		if (distance <= 0)
			continue;

		score = distance + offset;

		/* heavy penalty for windows more than 45 degrees off-axis */
		if (offset > distance)
			score += 1000000L;

		if (best_score < 0 || score < best_score) {
			best = &cands[i];
			best_score = score;
		}
	}

	return best ? best->win : NULL;
}
