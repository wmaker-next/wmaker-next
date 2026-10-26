/* osdview.h - generic single-line on-screen-display popup
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

#ifndef OSDVIEW_H
#define OSDVIEW_H

struct _WScreen;

typedef struct W_OSDView WOSDView;

/* Default gap in pixels between an OSD popup and its anchored edge,
 * shared by the key-chain chord display and the layout flash display */
#define WOSD_EDGE_PAD 8

WOSDView *WCreateOSDView(WMScreen *scr);

/* Override the 12pt system font used for the text and re-measure. The
 * font is retained. */
void WSetOSDViewFont(WOSDView *view, WMFont *font);

/* Lock the widget size to whatever 'tmpl' measures so that subsequent
 * WSetOSDViewText() calls only repaint and never resize. Used by the
 * move/resize geometry display so the box stays fixed inside the
 * server-grabbed interactive loop. */
void WSetOSDViewFixedTemplate(WOSDView *view, const char *tmpl);

/* Replace the displayed text. The string is copied. When no fixed
 * template is set the widget resizes to fit. */
void WSetOSDViewText(WOSDView *view, const char *text);

/* Clear the background; use ARGB compositing when available and a shaped
 * text window as the fallback on non-composited displays. */
void WSetOSDViewTransparent(WOSDView *view, bool transparent);

/* Whole-frame opacity in [0,1], repaints when mapped. Only visually
 * translucent when a compositor is running and the server offers a
 * depth-32 visual, otherwise the frame darkens toward black. */
void WSetOSDViewAlpha(WOSDView *view, double alpha);

/* Move 'view' onto the head under the pointer at one of the WD_*
 * anchor positions from config_values.h, 'pad' pixels from the
 * anchored edge(s). WD_NONE is treated as WD_CENTER, callers gate
 * visibility on WD_NONE themselves. */
void wPlaceOSDOnHead(struct _WScreen *scr, WOSDView *view, int wd_pos, int pad);

#endif  /* OSDVIEW_H */
