/* dockgeom.c - pure screen-edge geometry for the dock */

#include "wconfig.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "dockgeom.h"

bool wDockEdgeHorizontal(WDockEdge edge)
{
	return edge == WDOCK_EDGE_TOP || edge == WDOCK_EDGE_BOTTOM;
}

const char *wDockEdgeName(WDockEdge edge)
{
	switch (edge) {
	case WDOCK_EDGE_LEFT:
		return "Left";
	case WDOCK_EDGE_RIGHT:
		return "Right";
	case WDOCK_EDGE_TOP:
		return "Top";
	case WDOCK_EDGE_BOTTOM:
		return "Bottom";
	}
	return NULL;
}

bool wDockEdgeFromName(const char *str, WDockEdge *edge)
{
	WDockEdge e;

	if (!str)
		return false;

	for (e = WDOCK_EDGE_LEFT; e <= WDOCK_EDGE_BOTTOM; e++) {
		if (strcasecmp(str, wDockEdgeName(e)) == 0) {
			if (edge)
				*edge = e;
			return true;
		}
	}
	return false;
}

void wDockGeomAcrossSign(WDockEdge edge, char *axis, int *sign)
{
	char a;
	int s;

	switch (edge) {
	case WDOCK_EDGE_LEFT:
		a = 'x';
		s = 1;
		break;
	case WDOCK_EDGE_RIGHT:
		a = 'x';
		s = -1;
		break;
	case WDOCK_EDGE_TOP:
		a = 'y';
		s = 1;
		break;
	case WDOCK_EDGE_BOTTOM:
	default:
		a = 'y';
		s = -1;
		break;
	}

	if (axis)
		*axis = a;
	if (sign)
		*sign = s;
}

void wDockGeomOrigin(WMRect rect, WDockEdge edge, int icon_size, int extra,
		     int *x, int *y)
{
	int px, py;

	switch (edge) {
	case WDOCK_EDGE_RIGHT:
		px = rect.pos.x + rect.size.width - icon_size - extra;
		py = rect.pos.y + extra;
		break;
	case WDOCK_EDGE_TOP:
		px = rect.pos.x + extra;
		py = rect.pos.y + extra;
		break;
	case WDOCK_EDGE_BOTTOM:
		px = rect.pos.x + extra;
		py = rect.pos.y + rect.size.height - icon_size - extra;
		break;
	case WDOCK_EDGE_LEFT:
	default:
		px = rect.pos.x + extra;
		py = rect.pos.y + extra;
		break;
	}

	if (x)
		*x = px;
	if (y)
		*y = py;
}

WDockEdge wDockGeomNearestEdge(WMRect rect, int px, int py, WDockEdge current,
			       int icon_size)
{
	int dist[WDOCK_EDGE_BOTTOM + 1];
	WDockEdge e, best;
	int cur;

	if (px < rect.pos.x)
		px = rect.pos.x;
	else if (px >= rect.pos.x + rect.size.width)
		px = rect.pos.x + rect.size.width - 1;

	if (py < rect.pos.y)
		py = rect.pos.y;
	else if (py >= rect.pos.y + rect.size.height)
		py = rect.pos.y + rect.size.height - 1;

	dist[WDOCK_EDGE_LEFT] = px - rect.pos.x;
	dist[WDOCK_EDGE_RIGHT] = rect.pos.x + rect.size.width - 1 - px;
	dist[WDOCK_EDGE_TOP] = py - rect.pos.y;
	dist[WDOCK_EDGE_BOTTOM] = rect.pos.y + rect.size.height - 1 - py;

	best = WDOCK_EDGE_LEFT;
	for (e = WDOCK_EDGE_LEFT + 1; e <= WDOCK_EDGE_BOTTOM; e++)
		if (dist[e] < dist[best])
			best = e;

	if (current < WDOCK_EDGE_LEFT || current > WDOCK_EDGE_BOTTOM)
		return best;

	cur = dist[current];
	if (best != current && dist[best] <= cur - icon_size)
		return best;

	return current;
}

void wDockGeomTranspose(WDockEdge edge_from, WDockEdge edge_to,
			int *xindex, int *yindex)
{
	char axis;
	int sign;
	int xi, yi;

	xi = *xindex;
	yi = *yindex;

	if (wDockEdgeHorizontal(edge_from) != wDockEdgeHorizontal(edge_to)) {
		int t = xi;

		xi = yi;
		yi = t;
	}

	wDockGeomAcrossSign(edge_to, &axis, &sign);
	if (axis == 'x') {
		if (xi != 0) {
			xi = sign * abs(xi);
			yi = 0;
		}
	} else {
		if (yi != 0) {
			yi = sign * abs(yi);
			xi = 0;
		}
	}

	*xindex = xi;
	*yindex = yi;
}

void wDockGeomStrut(WMRect rect, int root_w, int root_h, WDockEdge edge,
		    int thickness, uint32_t out[12])
{
	int start, end;

	memset(out, 0, sizeof(uint32_t) * 12);

	if (thickness <= 0)
		return;

	if (wDockEdgeHorizontal(edge)) {
		if (thickness > rect.size.height)
			thickness = rect.size.height;
		start = rect.pos.x;
		end = rect.pos.x + rect.size.width - 1;
		if (end > root_w - 1)
			end = root_w - 1;
	} else {
		if (thickness > rect.size.width)
			thickness = rect.size.width;
		start = rect.pos.y;
		end = rect.pos.y + rect.size.height - 1;
		if (end > root_h - 1)
			end = root_h - 1;
	}
	if (start < 0)
		start = 0;

	switch (edge) {
	case WDOCK_EDGE_LEFT:
		out[0] = thickness;	/* left */
		out[4] = start;		/* left_start */
		out[5] = end;		/* left_end */
		break;
	case WDOCK_EDGE_RIGHT:
		out[1] = thickness;	/* right */
		out[6] = start;		/* right_start */
		out[7] = end;		/* right_end */
		break;
	case WDOCK_EDGE_TOP:
		out[2] = thickness;	/* top */
		out[8] = start;		/* top_start */
		out[9] = end;		/* top_end */
		break;
	case WDOCK_EDGE_BOTTOM:
		out[3] = thickness;	/* bottom */
		out[10] = start;	/* bottom_start */
		out[11] = end;		/* bottom_end */
		break;
	}
}
