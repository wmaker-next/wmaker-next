/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2013-2026 Window Maker Team
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


#ifndef WMTEXTURE_H_
#define WMTEXTURE_H_

#include "screen.h"
#include "wcore.h"

/* texture relief */
#define WREL_RAISED	0
#define WREL_SUNKEN	1
#define WREL_FLAT	2
#define WREL_ICON	4
#define WREL_MENUENTRY	6

/* texture types */
#define WREL_BORDER_MASK	1

#define WTEX_SOLID 	((1<<1)|WREL_BORDER_MASK)
#define WTEX_HGRADIENT	((1<<2)|WREL_BORDER_MASK)
#define WTEX_VGRADIENT	((1<<3)|WREL_BORDER_MASK)
#define WTEX_DGRADIENT	((1<<4)|WREL_BORDER_MASK)
#define WTEX_MHGRADIENT	((1<<5)|WREL_BORDER_MASK)
#define WTEX_MVGRADIENT	((1<<6)|WREL_BORDER_MASK)
#define WTEX_MDGRADIENT	((1<<7)|WREL_BORDER_MASK)
#define WTEX_IGRADIENT	((1<<8)|WREL_BORDER_MASK)
#define WTEX_PIXMAP	(1<<10)
#define WTEX_THGRADIENT	((1<<11)|WREL_BORDER_MASK)
#define WTEX_TVGRADIENT	((1<<12)|WREL_BORDER_MASK)
#define WTEX_TDGRADIENT	((1<<13)|WREL_BORDER_MASK)
#define WTEX_FUNCTION	((1<<14)|WREL_BORDER_MASK)

/* pixmap subtypes */
#define WTP_TILE	2
#define WTP_SCALE	4
#define WTP_CENTER	6


typedef struct {
    short type;			       /* type of texture */
    char subtype;		       /* subtype of the texture */
    WXColor color;		       /* default background color */
} WTexAny;


typedef struct WTexSolid {
    short type;
    char subtype;
    WXColor normal;

    WXColor light;
    WXColor dim;
    WXColor dark;
} WTexSolid;


typedef struct WTexGradient {
    short type;
    char subtype;
    WXColor normal;

    RColor color1;
    RColor color2;
} WTexGradient;


typedef struct WTexMGradient {
    short type;
    char subtype;
    WXColor normal;

    RColor **colors;
} WTexMGradient;


typedef struct WTexIGradient {
    short type;
    char dummy;
    WXColor normal;

    RColor colors1[2];
    RColor colors2[2];
    int thickness1;
    int thickness2;
} WTexIGradient;


typedef struct WTexPixmap {
    short type;
    char subtype;
    WXColor normal;

    struct RImage *pixmap;
} WTexPixmap;

typedef struct WTexTGradient {
    short type;
    char subtype;
    WXColor normal;

    RColor color1;
    RColor color2;
    struct RImage *pixmap;
    int opacity;
} WTexTGradient;

typedef struct WTexFunction {
    short type;
    char subtype;
    WXColor normal;

    void *handle;
    RImage *(*render) (int, char**, int, int, int);
    int argc;
    char **argv;
} WTexFunction;

typedef union WTexture {
    WTexAny any;
    WTexSolid solid;
    WTexGradient gradient;
    WTexIGradient igradient;
    WTexMGradient mgradient;
    WTexPixmap pixmap;
    WTexTGradient tgradient;
    WTexFunction function;
} WTexture;


WTexSolid *wTextureMakeSolid(WScreen*, WXColor*);
WTexGradient *wTextureMakeGradient(WScreen*, int, const RColor*, const RColor*);
WTexMGradient *wTextureMakeMGradient(WScreen*, int, RColor**);
WTexTGradient *wTextureMakeTGradient(WScreen*, int, const RColor*, const RColor*, const char *, int);
WTexIGradient *wTextureMakeIGradient(WScreen*, int, const RColor[2], int, const RColor[2]);
WTexPixmap *wTextureMakePixmap(WScreen *scr, int style, const char *pixmap_file,
                               WXColor *color);
void wTextureDestroy(WScreen*, WTexture*);
struct RImage *wTextureRenderImage(WTexture*, int, int, int);

typedef struct _cairo_pattern cairo_pattern_t;
typedef struct _cairo cairo_t;

/* WTexture as a cairo pattern with the same geometry as
 * wTextureRenderImage(texture, width, height, WREL_FLAT). NULL for the
 * variants with no direct pattern equivalent (interwoven, textured
 * gradient, plugin) so callers fall back to the RImage path. */
cairo_pattern_t *wTexturePattern(WTexture *texture, int width, int height);

/* the RBEV_RAISED2 highlight/shadow lines as translucent strokes on
 * top of an already-painted background */
void wTextureBevel(cairo_t *cr, int x, int y, int w, int h);

/* the WREL_MENUENTRY bevel: +highlight top/left, -dim right and inner
 * bottom, opaque black separator on the bottom edge */
void wTextureMenuBevel(cairo_t *cr, int x, int y, int w, int h);


void wTexturePaintTitlebar(struct WWindow *wwin, WTexture *texture, xcb_pixmap_t *tdata,
                           int repaint);


#define FREE_PIXMAP(p) do {				\
	if ((p) != XCB_NONE)				\
		wXFreePixmap(wdisp->conn, (p));		\
	(p) = XCB_NONE;					\
} while (0)

/* WREL_RAISED / WREL_ICON bevel in a WTexSolid's own light/dim/dark
 * colours (opaque strokes, unlike wTextureBevel's translucent
 * add/subtract), used on the solid-texture titlebar and button paths */
void wDrawBevel(cairo_t *cr, unsigned width, unsigned height,
                WTexSolid *texture, int relief);

#endif
