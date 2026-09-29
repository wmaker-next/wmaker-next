/*
 * check_dockgeom - unit tests for the pure dock edge geometry in
 * dockgeom.c. No display needed, the anchor rect is passed in.
 */

#include "wconfig.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dockgeom.h"

#include "check_common.h"

#define ISZ 64

/* local stand-in for wmkrect() so the test links without libWINGs */
static WMRect mkrect(int x, int y, unsigned int width, unsigned int height)
{
	WMRect r;

	r.pos.x = x;
	r.pos.y = y;
	r.size.width = width;
	r.size.height = height;
	return r;
}

static WMRect screen_rect(void)
{
	return mkrect(0, 0, 1920, 1080);
}

static WMRect head_rect(void)
{
	return mkrect(1920, 0, 800, 600);
}

static void test_origin_screen(void)
{
	WMRect r = screen_rect();
	int x, y;

	wDockGeomOrigin(r, WDOCK_EDGE_LEFT, ISZ, 0, &x, &y);
	CHECK(x == 0 && y == 0);
	wDockGeomOrigin(r, WDOCK_EDGE_RIGHT, ISZ, 0, &x, &y);
	CHECK(x == 1920 - ISZ && y == 0);
	wDockGeomOrigin(r, WDOCK_EDGE_TOP, ISZ, 0, &x, &y);
	CHECK(x == 0 && y == 0);
	wDockGeomOrigin(r, WDOCK_EDGE_BOTTOM, ISZ, 0, &x, &y);
	CHECK(x == 0 && y == 1080 - ISZ);

	wDockGeomOrigin(r, WDOCK_EDGE_LEFT, ISZ, 4, &x, &y);
	CHECK(x == 4 && y == 4);
	wDockGeomOrigin(r, WDOCK_EDGE_RIGHT, ISZ, 4, &x, &y);
	CHECK(x == 1920 - ISZ - 4 && y == 4);
	wDockGeomOrigin(r, WDOCK_EDGE_TOP, ISZ, 4, &x, &y);
	CHECK(x == 4 && y == 4);
	wDockGeomOrigin(r, WDOCK_EDGE_BOTTOM, ISZ, 4, &x, &y);
	CHECK(x == 4 && y == 1080 - ISZ - 4);
}

static void test_origin_head(void)
{
	WMRect r = head_rect();
	int x, y;

	wDockGeomOrigin(r, WDOCK_EDGE_LEFT, ISZ, 0, &x, &y);
	CHECK(x == 1920 && y == 0);
	wDockGeomOrigin(r, WDOCK_EDGE_RIGHT, ISZ, 0, &x, &y);
	CHECK(x == 1920 + 800 - ISZ && y == 0);
	wDockGeomOrigin(r, WDOCK_EDGE_TOP, ISZ, 0, &x, &y);
	CHECK(x == 1920 && y == 0);
	wDockGeomOrigin(r, WDOCK_EDGE_BOTTOM, ISZ, 0, &x, &y);
	CHECK(x == 1920 && y == 600 - ISZ);

	wDockGeomOrigin(r, WDOCK_EDGE_TOP, ISZ, 4, &x, &y);
	CHECK(x == 1924 && y == 4);
	wDockGeomOrigin(r, WDOCK_EDGE_BOTTOM, ISZ, 4, &x, &y);
	CHECK(x == 1924 && y == 600 - ISZ - 4);
}

static void test_horizontal_and_signs(void)
{
	char axis;
	int sign;

	CHECK(wDockEdgeHorizontal(WDOCK_EDGE_LEFT) == false);
	CHECK(wDockEdgeHorizontal(WDOCK_EDGE_RIGHT) == false);
	CHECK(wDockEdgeHorizontal(WDOCK_EDGE_TOP) == true);
	CHECK(wDockEdgeHorizontal(WDOCK_EDGE_BOTTOM) == true);

	wDockGeomAcrossSign(WDOCK_EDGE_LEFT, &axis, &sign);
	CHECK(axis == 'x' && sign == 1);
	wDockGeomAcrossSign(WDOCK_EDGE_RIGHT, &axis, &sign);
	CHECK(axis == 'x' && sign == -1);
	wDockGeomAcrossSign(WDOCK_EDGE_TOP, &axis, &sign);
	CHECK(axis == 'y' && sign == 1);
	wDockGeomAcrossSign(WDOCK_EDGE_BOTTOM, &axis, &sign);
	CHECK(axis == 'y' && sign == -1);
}

static void test_names(void)
{
	WDockEdge e;

	CHECK(strcmp(wDockEdgeName(WDOCK_EDGE_LEFT), "Left") == 0);
	CHECK(strcmp(wDockEdgeName(WDOCK_EDGE_RIGHT), "Right") == 0);
	CHECK(strcmp(wDockEdgeName(WDOCK_EDGE_TOP), "Top") == 0);
	CHECK(strcmp(wDockEdgeName(WDOCK_EDGE_BOTTOM), "Bottom") == 0);

	CHECK(wDockEdgeFromName("top", &e) && e == WDOCK_EDGE_TOP);
	CHECK(wDockEdgeFromName("BOTTOM", &e) && e == WDOCK_EDGE_BOTTOM);
	CHECK(wDockEdgeFromName("Left", &e) && e == WDOCK_EDGE_LEFT);
	CHECK(wDockEdgeFromName("right", &e) && e == WDOCK_EDGE_RIGHT);

	/* unknown is a failure, not a silent Right */
	e = WDOCK_EDGE_RIGHT;
	CHECK(!wDockEdgeFromName("Centre", &e));
	CHECK(e == WDOCK_EDGE_RIGHT);
	CHECK(!wDockEdgeFromName("", &e));
	CHECK(!wDockEdgeFromName(NULL, &e));
}

static void test_hysteresis(void)
{
	WMRect r = screen_rect();

	/* pointer at the top of a right-side anchor stays Right: the
	 * upgrade trap, a tie never flips the edge */
	CHECK(wDockGeomNearestEdge(r, 1919, 0, WDOCK_EDGE_RIGHT, ISZ)
	      == WDOCK_EDGE_RIGHT);

	/* clearly closer to the top than the right */
	CHECK(wDockGeomNearestEdge(r, 1700, 5, WDOCK_EDGE_RIGHT, ISZ)
	      == WDOCK_EDGE_TOP);

	/* one pixel past the corner toward the top does not switch */
	CHECK(wDockGeomNearestEdge(r, 1918, 10, WDOCK_EDGE_RIGHT, ISZ)
	      == WDOCK_EDGE_RIGHT);

	/* more than ICON_SIZE closer to the top switches */
	CHECK(wDockGeomNearestEdge(r, 1919 - ISZ - 1, 0, WDOCK_EDGE_RIGHT, ISZ)
	      == WDOCK_EDGE_TOP);

	/* the same in the other diagonal */
	CHECK(wDockGeomNearestEdge(r, 1919, 1079, WDOCK_EDGE_RIGHT, ISZ)
	      == WDOCK_EDGE_RIGHT);
	CHECK(wDockGeomNearestEdge(r, 1700, 1074, WDOCK_EDGE_RIGHT, ISZ)
	      == WDOCK_EDGE_BOTTOM);

	/* a pointer outside the rect is clamped, so it cannot select an
	 * edge of a head it is not in */
	CHECK(wDockGeomNearestEdge(head_rect(), 100, 100, WDOCK_EDGE_RIGHT, ISZ)
	      == WDOCK_EDGE_LEFT);
	CHECK(wDockGeomNearestEdge(head_rect(), 4000, 300, WDOCK_EDGE_LEFT, ISZ)
	      == WDOCK_EDGE_RIGHT);
}

static void test_transpose(void)
{
	int x, y;

	/* Right->Top of dock slots (0,0) (0,1) (0,2) */
	x = 0; y = 0;
	wDockGeomTranspose(WDOCK_EDGE_RIGHT, WDOCK_EDGE_TOP, &x, &y);
	CHECK(x == 0 && y == 0);
	x = 0; y = 1;
	wDockGeomTranspose(WDOCK_EDGE_RIGHT, WDOCK_EDGE_TOP, &x, &y);
	CHECK(x == 1 && y == 0);
	x = 0; y = 2;
	wDockGeomTranspose(WDOCK_EDGE_RIGHT, WDOCK_EDGE_TOP, &x, &y);
	CHECK(x == 2 && y == 0);

	/* Right->Top of a drawer slot (-2, 0) */
	x = -2; y = 0;
	wDockGeomTranspose(WDOCK_EDGE_RIGHT, WDOCK_EDGE_TOP, &x, &y);
	CHECK(x == 0 && y == 2);

	/* Right->Left of (-2, 0) re-signs only */
	x = -2; y = 0;
	wDockGeomTranspose(WDOCK_EDGE_RIGHT, WDOCK_EDGE_LEFT, &x, &y);
	CHECK(x == 2 && y == 0);

	/* Right->Left of a dock slot keeps the column */
	x = 0; y = 3;
	wDockGeomTranspose(WDOCK_EDGE_RIGHT, WDOCK_EDGE_LEFT, &x, &y);
	CHECK(x == 0 && y == 3);

	/* Top->Bottom flips the Y sign only */
	x = 0; y = 2;
	wDockGeomTranspose(WDOCK_EDGE_TOP, WDOCK_EDGE_BOTTOM, &x, &y);
	CHECK(x == 0 && y == -2);
	x = 2; y = 0;
	wDockGeomTranspose(WDOCK_EDGE_TOP, WDOCK_EDGE_BOTTOM, &x, &y);
	CHECK(x == 2 && y == 0);

	/* Top->Right of a drawer slot (0, +2) goes back to (-2, 0) */
	x = 0; y = 2;
	wDockGeomTranspose(WDOCK_EDGE_TOP, WDOCK_EDGE_RIGHT, &x, &y);
	CHECK(x == -2 && y == 0);

	/* Bottom->Top of a drawer slot (0, -3) goes to (0, +3) */
	x = 0; y = -3;
	wDockGeomTranspose(WDOCK_EDGE_BOTTOM, WDOCK_EDGE_TOP, &x, &y);
	CHECK(x == 0 && y == 3);
}

static void check_strut(WDockEdge edge, int want_t, int want_s, int want_e,
			int rx, int ry, int rw, int rh)
{
	uint32_t out[12];
	WMRect r = mkrect(rx, ry, rw, rh);

	wDockGeomStrut(r, 3840, 1080, edge, ISZ, out);
	CHECK(out[0] == (edge == WDOCK_EDGE_LEFT ? want_t : 0));
	CHECK(out[1] == (edge == WDOCK_EDGE_RIGHT ? want_t : 0));
	CHECK(out[2] == (edge == WDOCK_EDGE_TOP ? want_t : 0));
	CHECK(out[3] == (edge == WDOCK_EDGE_BOTTOM ? want_t : 0));
	CHECK(out[4] == (edge == WDOCK_EDGE_LEFT ? want_s : 0));
	CHECK(out[5] == (edge == WDOCK_EDGE_LEFT ? want_e : 0));
	CHECK(out[6] == (edge == WDOCK_EDGE_RIGHT ? want_s : 0));
	CHECK(out[7] == (edge == WDOCK_EDGE_RIGHT ? want_e : 0));
	CHECK(out[8] == (edge == WDOCK_EDGE_TOP ? want_s : 0));
	CHECK(out[9] == (edge == WDOCK_EDGE_TOP ? want_e : 0));
	CHECK(out[10] == (edge == WDOCK_EDGE_BOTTOM ? want_s : 0));
	CHECK(out[11] == (edge == WDOCK_EDGE_BOTTOM ? want_e : 0));
}

static void test_strut(void)
{
	/* full root */
	check_strut(WDOCK_EDGE_LEFT, ISZ, 0, 1079, 0, 0, 1920, 1080);
	check_strut(WDOCK_EDGE_RIGHT, ISZ, 0, 1079, 0, 0, 1920, 1080);
	check_strut(WDOCK_EDGE_TOP, ISZ, 0, 1919, 0, 0, 1920, 1080);
	check_strut(WDOCK_EDGE_BOTTOM, ISZ, 0, 1919, 0, 0, 1920, 1080);

	/* a head not at the root origin still produces root coordinates */
	check_strut(WDOCK_EDGE_LEFT, ISZ, 0, 599, 1920, 0, 800, 600);
	check_strut(WDOCK_EDGE_RIGHT, ISZ, 0, 599, 1920, 0, 800, 600);
	check_strut(WDOCK_EDGE_TOP, ISZ, 1920, 2719, 1920, 0, 800, 600);
	check_strut(WDOCK_EDGE_BOTTOM, ISZ, 1920, 2719, 1920, 0, 800, 600);

	/* thickness clipped to the head size on the dock's axis */
	{
		uint32_t out[12];

		wDockGeomStrut(head_rect(), 3840, 1080, WDOCK_EDGE_LEFT,
			       10000, out);
		CHECK(out[0] == 800);
		wDockGeomStrut(head_rect(), 3840, 1080, WDOCK_EDGE_TOP,
			       10000, out);
		CHECK(out[2] == 600);
	}
}

int main(void)
{
	test_origin_screen();
	test_origin_head();
	test_horizontal_and_signs();
	test_names();
	test_hysteresis();
	test_transpose();
	test_strut();

	return check_report("check_dockgeom");
}
