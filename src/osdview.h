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

typedef struct W_OSDView WOSDView;

WOSDView *WCreateOSDView(WMScreen *scr);

/* Lock the widget size to whatever 'tmpl' measures so that subsequent
 * WSetOSDViewText() calls only repaint and never resize. Used by the
 * move/resize geometry display so the box stays fixed inside the
 * server-grabbed interactive loop. */
void WSetOSDViewFixedTemplate(WOSDView *view, const char *tmpl);

/* Replace the displayed text. The string is copied. When no fixed
 * template is set the widget resizes to fit. */
void WSetOSDViewText(WOSDView *view, const char *text);

#endif  /* OSDVIEW_H */
