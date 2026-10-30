/* resources.c - manage X resources (colors etc)
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2009-2026 Window Maker Team
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include <wraster.h>

#include "WindowMaker.h"
#include "texture.h"
#include "resources.h"
#include "screen.h"

static unsigned long scale_color_component(unsigned short value, unsigned long mask)
{
	unsigned long m = mask;
	int shift = 0, bits = 0;

	if (!m)
		return 0;

	while (!(m & 1)) {
		shift++;
		m >>= 1;
	}
	while (m) {
		bits++;
		m >>= 1;
	}

	return ((unsigned long)(value >> (16 - bits)) & ((1UL << bits) - 1)) << shift;
}

/*
 * XParseColor parses #rrggbb and rgb:rr/gg/bb client-side, but any other
 * spelling is sent to the server as a LookupColor request and blocks for the
 * reply. The shipped defaults, schema fallbacks and most themes only use the
 * handful of X11 names below, so resolve those locally and let everything
 * else fall through.
 */
static const struct {
	const char *name;
	unsigned short r, g, b;
} builtin_color_names[] = {
	{ "black",    0x0000, 0x0000, 0x0000 },
	{ "white",    0xffff, 0xffff, 0xffff },
	{ "gray",     0xbebe, 0xbebe, 0xbebe },
	{ "grey",     0xbebe, 0xbebe, 0xbebe },
	{ "gray40",   0x6666, 0x6666, 0x6666 },
	{ "gray50",   0x7f7f, 0x7f7f, 0x7f7f },
	{ "red",      0xffff, 0x0000, 0x0000 },
	{ "green",    0x0000, 0xffff, 0x0000 },
	{ "blue",     0x0000, 0x0000, 0xffff },
};

int wParseColor(xcb_colormap_t colormap, const char *spec, WXColor *color)
{
	size_t i;

	for (i = 0; i < wlengthof(builtin_color_names); i++) {
		if (strcasecmp(spec, builtin_color_names[i].name) == 0) {
			color->red = builtin_color_names[i].r;
			color->green = builtin_color_names[i].g;
			color->blue = builtin_color_names[i].b;
			return true;
		}
	}

	return wXParseColor(wdisp->conn, colormap, spec, color);
}

int wAllocColor(WScreen *scr, xcb_colormap_t colormap, WXColor *color)
{
	if (scr->w_visual->_class == XCB_VISUAL_CLASS_TRUE_COLOR) {
		/* Compute pixel directly from RGB components using the visual's
		 * channel masks, an AllocColor request on a TrueColor colormap
		 * is a server round-trip that returns exactly this value. */
		color->pixel = scale_color_component(color->red,   scr->w_visual->red_mask)
		             | scale_color_component(color->green, scr->w_visual->green_mask)
		             | scale_color_component(color->blue,  scr->w_visual->blue_mask);
		return true;
	}

	return wXAllocColor(wdisp->conn, colormap, color);
}

int wGetColorForColormap(WScreen *scr, xcb_colormap_t colormap, const char *color_name, WXColor *color)
{
	if (!wParseColor(colormap, color_name, color)) {
		wwarning(_("could not parse color \"%s\""), color_name);
		return false;
	}

	if (!wAllocColor(scr, colormap, color)) {
		wwarning(_("could not allocate color \"%s\""), color_name);
		return false;
	}
	return true;
}

int wGetColor(WScreen *scr, const char *color_name, WXColor *color)
{
	return wGetColorForColormap(scr, scr->w_colormap, color_name, color);
}

void wFreeColor(WScreen * scr, unsigned long pixel)
{
	if (scr->w_visual->_class == XCB_VISUAL_CLASS_TRUE_COLOR)
		return;

	if (pixel != scr->white_pixel && pixel != scr->black_pixel) {
		uint32_t colors[1];

		colors[0] = pixel;
		wXFreeColors(wdisp->conn, scr->w_colormap, colors, 1, 0);
	}
}
