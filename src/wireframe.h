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

#ifndef WMWIREFRAME_H
#define WMWIREFRAME_H

#include "screen.h"

/*
 * Override-redirect outline window for non-opaque move/resize and the
 * snap-target preview. The window is root-sized and never moves or
 * resizes, only its ShapeBounding is rewritten on every Commit to the
 * union of the outline rectangles accumulated since the previous Begin.
 * The background pixel paints the visible region, so there is no Expose
 * handling, no XOR erase/redraw pairing, and no XGrabServer.
 *
 * Usage per step:
 *   wWireframeBegin(scr);
 *   wWireframeAddOutline(scr, ...);   one call per frame to show
 *   wWireframeCommit(scr);
 * and wWireframeHide(scr) once at the end of the interaction.
 */

void wWireframeBegin(WScreen *scr);
void wWireframeAddOutline(WScreen *scr, int x, int y, int width, int height,
			  int titlebar_h, int resizebar_h, int border);

/* add an open or closed polyline (any orientation) rendered as a
 * WIREFRAME_THICKNESS-wide band, used for the non-axis-aligned
 * iconify animations, points are in root coordinates and the array
 * is copied so the caller may reuse it, the no-XShape fallback path
 * skips polylines since callers are cosmetic-only animations */
void wWireframeAddPolyline(WScreen *scr, const XPoint *pts, int npts);

void wWireframeCommit(WScreen *scr);
void wWireframeHide(WScreen *scr);

#endif
