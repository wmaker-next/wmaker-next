/*
 *  check_focusmodel - unit tests for the pure focus policy in
 *  src/focusmodel.c. No display needed, the inputs are fabricated.
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

#include <string.h>

#include "focusmodel.h"

#include "check_common.h"

static WWindow win_a, win_b, win_mod;

static void fill_in(WFocusWinIn *in, WWindow *win)
{
	memset(in, 0, sizeof(*in));
	in->win = win;
	in->focus_mode = WFM_PASSIVE;
	in->mapped = true;
}

static void test_redirect(void)
{
	WFocusWinIn in;
	WFocusPlan plan;

	/* the transient the applier found is mapped and passive */
	win_mod.flags.mapped = 1;
	win_mod.focus_mode = WFM_PASSIVE;

	/* a modal transient of the target redirects focus to the
	 * transient, and the transient's own focus mode drives the
	 * dispatch */
	fill_in(&in, &win_a);
	in.focus_mode = WFM_PASSIVE;
	wFocusResolve(&in, &win_b, true, &win_mod, &plan);
	CHECK(plan.redirect_to);
	CHECK(plan.redirect_win == &win_mod);
	/* the transient is mapped with the default (passive) mode */
	CHECK(plan.focus_path);
	CHECK(plan.target == WFOCUS_TARGET_CLIENT);
	CHECK(plan.reorder_list == (plan.redirect_win != &win_b));

	/* no modal transient: no redirect */
	fill_in(&in, &win_a);
	wFocusResolve(&in, &win_b, false, NULL, &plan);
	CHECK(!plan.redirect_to);
	CHECK(plan.redirect_win == NULL);
}

static void test_focus_modes(void)
{
	WFocusWinIn in;
	WFocusPlan plan;

	/* WFM_NO_INPUT: the no-focus window gets input focus, the
	 * TAKE_FOCUS protocol is still sent */
	fill_in(&in, &win_a);
	in.focus_mode = WFM_NO_INPUT;
	in.takes_focus_proto = true;
	wFocusResolve(&in, &win_b, false, NULL, &plan);
	CHECK(plan.focus_path);
	CHECK(plan.target == WFOCUS_TARGET_NONEWIN);
	CHECK(plan.send_take_focus);

	/* WFM_GLOBALLY_ACTIVE: protocol only, no xcb focus request */
	fill_in(&in, &win_a);
	in.focus_mode = WFM_GLOBALLY_ACTIVE;
	in.takes_focus_proto = true;
	wFocusResolve(&in, &win_b, false, NULL, &plan);
	CHECK(plan.focus_path);
	CHECK(plan.target == WFOCUS_TARGET_NOTHING);
	CHECK(plan.send_take_focus);

	in.takes_focus_proto = false;
	wFocusResolve(&in, &win_b, false, NULL, &plan);
	CHECK(!plan.send_take_focus);

	/* passive and locally-active go to the client */
	in.focus_mode = WFM_PASSIVE;
	wFocusResolve(&in, &win_b, false, NULL, &plan);
	CHECK(plan.target == WFOCUS_TARGET_CLIENT);
	in.focus_mode = WFM_LOCALLY_ACTIVE;
	wFocusResolve(&in, &win_b, false, NULL, &plan);
	CHECK(plan.target == WFOCUS_TARGET_CLIENT);
}

static void test_untargeted_and_edge_cases(void)
{
	WFocusWinIn in;
	WFocusPlan plan;

	/* untargeted call: no-focus window, no list reorder */
	wFocusResolve(NULL, &win_b, false, NULL, &plan);
	CHECK(plan.target == WFOCUS_TARGET_NONEWIN);
	CHECK(!plan.reorder_list);
	CHECK(!plan.send_take_focus);
	CHECK(!plan.redirect_to);

	/* an unmapped target skips the focus path entirely */
	fill_in(&in, &win_a);
	in.mapped = false;
	wFocusResolve(&in, &win_b, false, NULL, &plan);
	CHECK(!plan.focus_path);
	CHECK(plan.target == WFOCUS_TARGET_NONEWIN);
	CHECK(!plan.send_take_focus);

	/* a no-focusable target skips the focus path but is still the
	 * reorder candidate */
	fill_in(&in, &win_a);
	in.focusable = true;
	wFocusResolve(&in, &win_b, false, NULL, &plan);
	CHECK(!plan.focus_path);
	CHECK(plan.reorder_list);

	/* the reorder flag tracks the focused window identity */
	fill_in(&in, &win_b);
	wFocusResolve(&in, &win_b, false, NULL, &plan);
	CHECK(!plan.reorder_list);
}

static void test_direction(void)
{
	WFocusDirCandidate cands[4];
	WWindow *best;

	/* RIGHT: the nearest on-axis window wins */
	cands[0].win = &win_a;
	cands[0].rel_cx = 300;
	cands[0].rel_cy = 10;
	cands[1].win = &win_mod;
	cands[1].rel_cx = 120;
	cands[1].rel_cy = 0;
	cands[2].win = &win_b;
	cands[2].rel_cx = -50;		/* behind, not in direction */
	cands[2].rel_cy = 0;
	cands[3].win = &win_a;		/* placeholder, overwritten below */
	cands[3].rel_cx = 400;
	cands[3].rel_cy = 500;		/* more than 45 degrees off-axis */

	best = wFocusNearestInDirection(cands, 4, DIRECTION_RIGHT);
	CHECK(best == &win_mod);

	/* LEFT mirrors it */
	cands[0].rel_cx = -300;
	cands[0].rel_cy = 10;
	cands[1].rel_cx = -120;
	cands[1].rel_cy = 0;
	cands[2].rel_cx = 50;
	best = wFocusNearestInDirection(cands, 3, DIRECTION_LEFT);
	CHECK(best == &win_mod);

	/* DOWN: the off-axis candidate loses despite a smaller primary
	 * distance */
	cands[0].win = &win_a;
	cands[0].rel_cx = 0;
	cands[0].rel_cy = 200;
	cands[1].win = &win_mod;
	cands[1].rel_cx = 90;
	cands[1].rel_cy = 100;
	best = wFocusNearestInDirection(cands, 2, DIRECTION_DOWN);
	CHECK(best == &win_mod);

	/* nothing strictly in the direction: no winner */
	cands[0].rel_cy = 5;
	best = wFocusNearestInDirection(cands, 1, DIRECTION_UP);
	CHECK(best == NULL);
}

int main(void)
{
	test_redirect();
	test_focus_modes();
	test_untargeted_and_edge_cases();
	test_direction();

	return check_report("check_focusmodel");
}
