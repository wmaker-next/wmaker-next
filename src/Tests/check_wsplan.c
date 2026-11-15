/*
 *  check_wsplan - unit tests for the pure workspace-switch decisions in
 *  src/wsplan.c. No display needed, the per-window inputs are fabricated.
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

#include "wsplan.h"

#include "check_common.h"

static WWindow fake_win;

static void fill_in(WWsWinIn *in, bool on_target)
{
	memset(in, 0, sizeof(*in));
	in->win = &fake_win;
	in->on_target = on_target;
	in->no_focusable = false;
	in->has_icon = true;
}

static void run(bool per_head, bool sticky, const WWsWinIn *in,
		WWsStep *steps, int *n)
{
	wWorkspaceWindowDecision(per_head, sticky, in, steps, WWS_MAX_STEPS, n);
}

static void test_single_ws_plain(void)
{
	WWsWinIn in;
	WWsStep steps[WWS_MAX_STEPS];
	int n;

	/* a window on the target that needs a remap: primary focus
	 * claim */
	fill_in(&in, true);
	run(false, true, &in, steps, &n);
	CHECK(n == 1);
	CHECK(steps[0].kind == WWS_REMAP);
	CHECK(steps[0].focus_priority == WWS_FOCUS_PRIMARY);

	/* an on-target window that is already mapped only carries the
	 * fallback (foc2) claim */
	fill_in(&in, true);
	in.mapped = true;
	run(false, true, &in, steps, &n);
	CHECK(n == 1);
	CHECK(steps[0].kind == WWS_KEEP);
	CHECK(steps[0].focus_priority == WWS_FOCUS_FALLBACK);

	/* a mapped window not on the target gets a deferred unmap */
	fill_in(&in, false);
	in.mapped = true;
	run(false, true, &in, steps, &n);
	CHECK(n == 1);
	CHECK(steps[0].kind == WWS_UNMAP);
	CHECK(steps[0].focus_priority == WWS_FOCUS_NONE);

	/* a window being changed by a drag is exempt from the unmap
	 * collection */
	fill_in(&in, false);
	in.mapped = true;
	in.changing_workspace = true;
	run(false, true, &in, steps, &n);
	CHECK(n == 0);
}

static void test_single_ws_focus_claims(void)
{
	WWsWinIn in;
	WWsStep steps[WWS_MAX_STEPS];
	int n;

	/* a shaded on-target window is remapped and claims both the
	 * fallback and the primary slot */
	fill_in(&in, true);
	in.shaded = true;
	run(false, true, &in, steps, &n);
	CHECK(n == 2);
	CHECK(steps[0].kind == WWS_KEEP);
	CHECK(steps[0].focus_priority == WWS_FOCUS_FALLBACK);
	CHECK(steps[1].kind == WWS_REMAP);
	CHECK(steps[1].focus_priority == WWS_FOCUS_PRIMARY);

	/* no_focusable windows still remap but claim nothing */
	fill_in(&in, true);
	in.no_focusable = true;
	run(false, true, &in, steps, &n);
	CHECK(n == 1);
	CHECK(steps[0].kind == WWS_REMAP);
	CHECK(steps[0].focus_priority == WWS_FOCUS_NONE);

	/* a selected window keeps its focus without a map step, the
	 * move itself stays in the applier */
	fill_in(&in, false);
	in.selected = true;
	run(false, true, &in, steps, &n);
	CHECK(n == 1);
	CHECK(steps[0].kind == WWS_KEEP);
	CHECK(steps[0].focus_priority == WWS_FOCUS_PRIMARY);

	/* a selected miniaturized window does not claim focus */
	fill_in(&in, false);
	in.selected = true;
	in.miniaturized = true;
	run(false, true, &in, steps, &n);
	CHECK(n == 0);

	/* an on-target selected window still records the fallback
	 * claim before the branch */
	fill_in(&in, true);
	in.selected = true;
	in.mapped = true;
	run(false, true, &in, steps, &n);
	CHECK(n == 2);
	CHECK(steps[0].focus_priority == WWS_FOCUS_FALLBACK);
	CHECK(steps[1].focus_priority == WWS_FOCUS_PRIMARY);
}

static void test_icons(void)
{
	WWsWinIn in;
	WWsStep steps[WWS_MAX_STEPS];
	int n;

	/* sticky icons on: a miniaturized window on another workspace
	 * keeps its icon mapped, nothing is collected */
	fill_in(&in, false);
	in.miniaturized = true;
	run(false, true, &in, steps, &n);
	CHECK(n == 0);

	/* sticky icons off: the icon is collected for the deferred
	 * unmap */
	fill_in(&in, false);
	in.miniaturized = true;
	run(false, false, &in, steps, &n);
	CHECK(n == 1);
	CHECK(steps[0].kind == WWS_UNMAP_ICON);

	/* on return, the miniwindow is mapped again (unless
	 * omnipresent) */
	fill_in(&in, true);
	in.miniaturized = true;
	run(false, false, &in, steps, &n);
	CHECK(n == 1);
	CHECK(steps[0].kind == WWS_MAP_ICON);

	/* an omnipresent miniwindow is left alone: no icon map for
	 * omnipresent windows and no fallback claim while it is not
	 * mapped or shaded */
	fill_in(&in, true);
	in.miniaturized = true;
	in.omnipresent = true;
	run(false, false, &in, steps, &n);
	CHECK(n == 0);
}

static void test_per_head(void)
{
	WWsWinIn in;
	WWsStep steps[WWS_MAX_STEPS];
	int n;

	/* a window visible on the initiating head is remapped and
	 * claims focus through the effective-head preference */
	fill_in(&in, true);
	in.effective_head_matches = true;
	run(true, true, &in, steps, &n);
	CHECK(n == 1);
	CHECK(steps[0].kind == WWS_REMAP);
	CHECK(steps[0].focus_priority == WWS_FOCUS_PRIMARY);

	/* an already-mapped on-target window claims focus through the
	 * post check when it sits on the initiating head */
	fill_in(&in, true);
	in.mapped = true;
	in.effective_head_matches = true;
	run(true, true, &in, steps, &n);
	CHECK(n == 1);
	CHECK(steps[0].kind == WWS_KEEP);
	CHECK(steps[0].focus_priority == WWS_FOCUS_PRIMARY);

	/* no focus claim without the effective-head match */
	fill_in(&in, true);
	in.mapped = true;
	in.effective_head_matches = false;
	run(true, true, &in, steps, &n);
	CHECK(n == 0);

	/* an omnipresent window that is not visible anywhere is kept
	 * (no unmap collection, no claims) */
	fill_in(&in, false);
	in.omnipresent = true;
	in.mapped = true;
	run(true, true, &in, steps, &n);
	CHECK(n == 0);

	/* a mapped window that is not visible anywhere is collected,
	 * omnipresent windows are exempt */
	fill_in(&in, false);
	in.mapped = true;
	run(true, true, &in, steps, &n);
	CHECK(n == 1);
	CHECK(steps[0].kind == WWS_UNMAP);

	/* the selected-window focus claim survives miniaturization
	 * through the post check when it is shaded on the head */
	fill_in(&in, true);
	in.selected = true;
	in.miniaturized = true;
	in.shaded = true;
	in.effective_head_matches = true;
	run(true, true, &in, steps, &n);
	CHECK(n == 1);
	CHECK(steps[0].kind == WWS_KEEP);
	CHECK(steps[0].focus_priority == WWS_FOCUS_PRIMARY);
}

static void test_layout_grid(void)
{
	int row, col;

	/* row-major 2x2 */
	CHECK(wWsLayoutPosition(2, 2, 0, 0, 1, &row, &col));
	CHECK(row == 0 && col == 1);
	CHECK(wWsLayoutPosition(2, 2, 0, 0, 2, &row, &col));
	CHECK(row == 1 && col == 0);

	/* column-major 2x2 */
	CHECK(wWsLayoutPosition(2, 2, 1, 0, 1, &row, &col));
	CHECK(row == 1 && col == 0);
	CHECK(wWsLayoutPosition(2, 2, 1, 0, 2, &row, &col));
	CHECK(row == 0 && col == 1);

	/* top-right corner flips the column */
	CHECK(wWsLayoutPosition(1, 3, 0, 1, 0, &row, &col));
	CHECK(row == 0 && col == 2);
	CHECK(wWsLayoutIndex(1, 3, 0, 1, 0, 2) == 0);

	/* out of range rejects */
	CHECK(!wWsLayoutPosition(2, 2, 0, 0, 4, &row, &col));
	CHECK(!wWsLayoutPosition(0, 2, 0, 0, 0, &row, &col));
}

static void test_neighbour(void)
{
	/* row-major 2x2, four workspaces */
	CHECK(wWsNeighbour(2, 2, 0, 0, 4, false, 0, 1, 0) == 1);
	CHECK(wWsNeighbour(2, 2, 0, 0, 4, false, 0, 0, 1) == 2);
	CHECK(wWsNeighbour(2, 2, 0, 0, 4, false, 0, -1, 0) == -1);

	/* cycling wraps on both axes */
	CHECK(wWsNeighbour(2, 2, 0, 0, 4, true, 1, 1, 0) == 0);
	CHECK(wWsNeighbour(2, 2, 0, 0, 4, true, 2, 0, 1) == 0);
	CHECK(wWsNeighbour(2, 2, 0, 0, 4, true, 2, 0, -1) == 0);

	/* incomplete row-major 2x3 grid (cell 5 unused): wrap along the axis */
	CHECK(wWsNeighbour(2, 3, 0, 0, 5, true, 4, 1, 0) == 3);
	CHECK(wWsNeighbour(2, 3, 0, 0, 5, true, 3, -1, 0) == 4);
	CHECK(wWsNeighbour(2, 3, 0, 0, 5, true, 2, 1, 0) == 0);
	CHECK(wWsNeighbour(2, 3, 0, 0, 5, true, 0, -1, 0) == 2);
	CHECK(wWsNeighbour(2, 3, 0, 0, 5, true, 4, 0, 1) == 1);
	CHECK(wWsNeighbour(2, 3, 0, 0, 5, true, 1, 0, -1) == 4);
	/* a column whose only workspace is the start wraps to itself */
	CHECK(wWsNeighbour(2, 3, 0, 0, 5, true, 2, 0, 1) == 2);
	/* without cycling an unused cell stops */
	CHECK(wWsNeighbour(2, 3, 0, 0, 5, false, 4, 1, 0) == -1);

	/* incomplete column-major 2x3 grid */
	CHECK(wWsNeighbour(2, 3, 1, 0, 5, true, 1, 1, 0) == 3);
	CHECK(wWsNeighbour(2, 3, 1, 0, 5, true, 3, 1, 0) == 1);
	CHECK(wWsNeighbour(2, 3, 1, 0, 5, true, 4, 0, 1) == 4);

	/* corner mirrors keep the wrap on the same screen row */
	CHECK(wWsNeighbour(2, 3, 0, 1, 5, true, 3, 1, 0) == 4);
	CHECK(wWsNeighbour(2, 3, 0, 1, 5, true, 4, 1, 0) == 3);
	CHECK(wWsNeighbour(2, 3, 0, 3, 5, true, 4, 1, 0) == 3);
	CHECK(wWsNeighbour(2, 3, 1, 2, 5, true, 0, 1, 0) == 4);
}

static void test_occupied_target(void)
{
	bool occupied[5] = { false, true, true, false, true };
	bool allowed[5] = { true, true, true, true, true };

	/* the first occupied workspace in the walk direction wins */
	CHECK(wWsRelativeOccupiedTarget(5, 1, 1, true, occupied, allowed) == 2);
	CHECK(wWsRelativeOccupiedTarget(5, 1, 2, true, occupied, allowed) == 4);

	/* empty workspaces are skipped and the walk wraps */
	CHECK(wWsRelativeOccupiedTarget(5, 4, 1, true, occupied, allowed) == 1);
	CHECK(wWsRelativeOccupiedTarget(5, 4, 2, true, occupied, allowed) == 2);

	/* backwards */
	CHECK(wWsRelativeOccupiedTarget(5, 2, -1, true, occupied, allowed) == 1);

	/* without cycling the walk stops at the edge */
	CHECK(wWsRelativeOccupiedTarget(5, 4, 1, false, occupied, allowed) == -1);
	CHECK(wWsRelativeOccupiedTarget(5, 0, -1, false, occupied, allowed) == -1);

	/* amount zero and empty screens refuse */
	CHECK(wWsRelativeOccupiedTarget(5, 1, 0, true, occupied, allowed) == -1);
	CHECK(wWsRelativeOccupiedTarget(0, 0, 1, true, occupied, allowed) == -1);

	/* per-head pin filtering: an occupied but disallowed workspace
	 * is skipped */
	allowed[2] = false;
	CHECK(wWsRelativeOccupiedTarget(5, 1, 1, true, occupied, allowed) == 4);
}

static void test_directional(void)
{
	bool occupied[4] = { true, true, false, true };
	bool allowed[4] = { true, true, true, true };

	/* plain move right on a 2x2 row-major grid */
	CHECK(wWsDirectionalTarget(2, 2, 0, 0, 4, 0, 1, 0, false, false,
				   occupied, allowed) == 1);

	/* going down from 0 lands on 2 */
	CHECK(wWsDirectionalTarget(2, 2, 0, 0, 4, 0, 0, 1, false, false,
				   occupied, allowed) == 2);

	/* the right neighbour is empty and nothing lies beyond: the
	 * keystroke is swallowed */
	occupied[1] = false;
	CHECK(wWsDirectionalTarget(2, 2, 0, 0, 4, 0, 1, 0, false, true,
				   occupied, allowed) == -1);

	/* a three-cell row skips the empty middle workspace */
	occupied[0] = true;
	occupied[1] = false;
	occupied[2] = true;
	occupied[3] = true;
	CHECK(wWsDirectionalTarget(1, 3, 0, 0, 3, 0, 1, 0, false, true,
				   occupied, allowed) == 2);

	/* a workspace pinned to another head is skipped */
	occupied[0] = true;
	occupied[1] = true;
	occupied[2] = true;
	allowed[1] = false;
	CHECK(wWsDirectionalTarget(1, 3, 0, 0, 3, 0, 1, 0, false, false,
				   occupied, allowed) == 2);
}

static void test_no_icon_unmap(void)
{
	WWsWinIn in;
	WWsStep steps[WWS_MAX_STEPS];
	int n;

	/* miniaturized without an icon: the unmap must not claim one */
	fill_in(&in, false);
	in.miniaturized = true;
	in.has_icon = false;
	in.shaded = true;
	run(false, false, &in, steps, &n);
	CHECK(n == 1);
	CHECK(steps[0].kind == WWS_UNMAP);

	fill_in(&in, false);
	in.miniaturized = true;
	in.has_icon = false;
	in.mapped = true;
	run(false, false, &in, steps, &n);
	CHECK(n == 1);
	CHECK(steps[0].kind == WWS_UNMAP);

	fill_in(&in, false);
	in.miniaturized = true;
	in.has_icon = false;
	in.shaded = true;
	run(true, false, &in, steps, &n);
	CHECK(n == 1);
	CHECK(steps[0].kind == WWS_UNMAP);

	fill_in(&in, false);
	in.miniaturized = true;
	in.shaded = true;
	run(false, false, &in, steps, &n);
	CHECK(n == 1);
	CHECK(steps[0].kind == WWS_UNMAP_ICON);

	fill_in(&in, false);
	in.miniaturized = true;
	in.shaded = true;
	run(true, false, &in, steps, &n);
	CHECK(n == 1);
	CHECK(steps[0].kind == WWS_UNMAP_ICON);
}

int main(void)
{
	test_single_ws_plain();
	test_single_ws_focus_claims();
	test_icons();
	test_no_icon_unmap();
	test_per_head();
	test_layout_grid();
	test_neighbour();
	test_occupied_target();
	test_directional();

	return check_report("check_wsplan");
}
