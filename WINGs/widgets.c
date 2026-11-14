/*
 *  WINGs widget core: screens, widget lifecycle and defaults
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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <cairo-xcb.h>

#include "WINGsP.h"
#include "wconfig.h"

#include <fontconfig/fontconfig.h>
#include <xkbcommon/xkbcommon-keysyms.h>


/********** data ************/

#define CHECK_BUTTON_ON_WIDTH 	16
#define CHECK_BUTTON_ON_HEIGHT 	16

static char *CHECK_BUTTON_ON[] = {
	"               %",
	" .............%#",
	" ........... .%#",
	" .......... #.%#",
	" ......... #%.%#",
	" ........ #%..%#",
	" ... #.. #%...%#",
	" ... #% #%....%#",
	" ... % #%.....%#",
	" ...  #%......%#",
	" ... #%.......%#",
	" ...#%........%#",
	" .............%#",
	" .............%#",
	" %%%%%%%%%%%%%%#",
	"%###############"
};

#define CHECK_BUTTON_OFF_WIDTH 	16
#define CHECK_BUTTON_OFF_HEIGHT	16

static char *CHECK_BUTTON_OFF[] = {
	"               %",
	" .............%#",
	" .............%#",
	" .............%#",
	" .............%#",
	" .............%#",
	" .............%#",
	" .............%#",
	" .............%#",
	" .............%#",
	" .............%#",
	" .............%#",
	" .............%#",
	" .............%#",
	" %%%%%%%%%%%%%%#",
	"%###############"
};

#define RADIO_BUTTON_ON_WIDTH 	15
#define RADIO_BUTTON_ON_HEIGHT	15
static char *RADIO_BUTTON_ON[] = {
	".....%%%%%.....",
	"...%%#####%%...",
	"..%##.....%.%..",
	".%#%..    .....",
	".%#.        ...",
	"%#..        .. ",
	"%#.          . ",
	"%#.          . ",
	"%#.          . ",
	"%#.          . ",
	".%%.        . .",
	".%..        . .",
	"..%...    .. ..",
	".... .....  ...",
	".....     .....",
};

#define RADIO_BUTTON_OFF_WIDTH 	15
#define RADIO_BUTTON_OFF_HEIGHT	15
static char *RADIO_BUTTON_OFF[] = {
	".....%%%%%.....",
	"...%%#####%%...",
	"..%##.......%..",
	".%#%...........",
	".%#............",
	"%#............ ",
	"%#............ ",
	"%#............ ",
	"%#............ ",
	"%#............ ",
	".%%.......... .",
	".%........... .",
	"..%......... ..",
	".... .....  ...",
	".....     .....",
};

#define TRISTATE_BUTTON_ON_WIDTH 	15
#define TRISTATE_BUTTON_ON_HEIGHT	15
static char *TRISTATE_BUTTON_ON[] = {
	"%%%%%%%%%%%%%%.",
	"%%%%%%%%%%%%%. ",
	"%%           . ",
	"%% ##     ## . ",
	"%% ###   ### . ",
	"%%  ### ###  . ",
	"%%   #####   . ",
	"%%    ###    . ",
	"%%   #####   . ",
	"%%  ### ###  . ",
	"%% ###   ### . ",
	"%% ##     ## . ",
	"%%           . ",
	"%............. ",
	".              ",
};

#define TRISTATE_BUTTON_OFF_WIDTH 	15
#define TRISTATE_BUTTON_OFF_HEIGHT	15
static char *TRISTATE_BUTTON_OFF[] = {
	"%%%%%%%%%%%%%%.",
	"%%%%%%%%%%%%%. ",
	"%%           . ",
	"%%           . ",
	"%%           . ",
	"%%           . ",
	"%%           . ",
	"%%           . ",
	"%%           . ",
	"%%           . ",
	"%%           . ",
	"%%           . ",
	"%%           . ",
	"%............. ",
	".              ",
};

#define TRISTATE_BUTTON_TRI_WIDTH 	15
#define TRISTATE_BUTTON_TRI_HEIGHT	15
static char *TRISTATE_BUTTON_TRI[] = {
	"%%%%%%%%%%%%%%.",
	"%%%%%%%%%%%%%. ",
	"%%           . ",
	"%% # # # # # . ",
	"%%  # # # #  . ",
	"%% # # # # # . ",
	"%%  # # # #  . ",
	"%% # # # # # . ",
	"%%  # # # #  . ",
	"%% # # # # # . ",
	"%%  # # # #  . ",
	"%% # # # # # . ",
	"%%           . ",
	"%............. ",
	".              ",
};

static char *BUTTON_ARROW[] = {
	"..................",
	"....##....#### ...",
	"...#.%....#... ...",
	"..#..%#####... ...",
	".#............ ...",
	"#............. ...",
	".#............ ...",
	"..#..          ...",
	"...#. ............",
	"....# ............"
};

#define BUTTON_ARROW_WIDTH	18
#define BUTTON_ARROW_HEIGHT	10

static char *BUTTON_ARROW2[] = {
	"                  ",
	"    ##    ####.   ",
	"   # %    #   .   ",
	"  #  %#####   .   ",
	" #            .   ",
	"#             .   ",
	" #            .   ",
	"  #  ..........   ",
	"   # .            ",
	"    #.            "
};

#define BUTTON_ARROW2_WIDTH	18
#define BUTTON_ARROW2_HEIGHT	10

static char *SCROLLER_DIMPLE[] = {
	".%###.",
	"%#%%%%",
	"#%%...",
	"#%..  ",
	"#%.   ",
	".%.  ."
};

#define SCROLLER_DIMPLE_WIDTH   6
#define SCROLLER_DIMPLE_HEIGHT  6

static char *SCROLLER_ARROW_UP[] = {
	"....%....",
	"....#....",
	"...%#%...",
	"...###...",
	"..%###%..",
	"..#####..",
	".%#####%.",
	".#######.",
	"%#######%"
};

static char *HI_SCROLLER_ARROW_UP[] = {
	"    %    ",
	"    %    ",
	"   %%%   ",
	"   %%%   ",
	"  %%%%%  ",
	"  %%%%%  ",
	" %%%%%%% ",
	" %%%%%%% ",
	"%%%%%%%%%"
};

#define SCROLLER_ARROW_UP_WIDTH   9
#define SCROLLER_ARROW_UP_HEIGHT  9

static char *SCROLLER_ARROW_DOWN[] = {
	"%#######%",
	".#######.",
	".%#####%.",
	"..#####..",
	"..%###%..",
	"...###...",
	"...%#%...",
	"....#....",
	"....%...."
};

static char *HI_SCROLLER_ARROW_DOWN[] = {
	"%%%%%%%%%",
	" %%%%%%% ",
	" %%%%%%% ",
	"  %%%%%  ",
	"  %%%%%  ",
	"   %%%   ",
	"   %%%   ",
	"    %    ",
	"    %    "
};

#define SCROLLER_ARROW_DOWN_WIDTH   9
#define SCROLLER_ARROW_DOWN_HEIGHT  9

static char *SCROLLER_ARROW_LEFT[] = {
	"........%",
	"......%##",
	"....%####",
	"..%######",
	"%########",
	"..%######",
	"....%####",
	"......%##",
	"........%"
};

static char *HI_SCROLLER_ARROW_LEFT[] = {
	"        %",
	"      %%%",
	"    %%%%%",
	"  %%%%%%%",
	"%%%%%%%%%",
	"  %%%%%%%",
	"    %%%%%",
	"      %%%",
	"        %"
};

#define SCROLLER_ARROW_LEFT_WIDTH   9
#define SCROLLER_ARROW_LEFT_HEIGHT  9

static char *SCROLLER_ARROW_RIGHT[] = {
	"%........",
	"##%......",
	"####%....",
	"######%..",
	"########%",
	"######%..",
	"####%....",
	"##%......",
	"%........"
};

static char *HI_SCROLLER_ARROW_RIGHT[] = {
	"%        ",
	"%%%      ",
	"%%%%%    ",
	"%%%%%%%  ",
	"%%%%%%%%%",
	"%%%%%%%  ",
	"%%%%%    ",
	"%%%      ",
	"%        "
};

#define SCROLLER_ARROW_RIGHT_WIDTH   9
#define SCROLLER_ARROW_RIGHT_HEIGHT  9

static char *POPUP_INDICATOR[] = {
	"        #==",
	" ......%#==",
	" ......%#%%",
	" ......%#%%",
	" %%%%%%%#%%",
	"#########%%",
	"==%%%%%%%%%",
	"==%%%%%%%%%"
};

#define POPUP_INDICATOR_WIDTH	11
#define POPUP_INDICATOR_HEIGHT 	8

static char *PULLDOWN_INDICATOR[] = {
	"=#######=",
	"=%===== =",
	"==%=== ==",
	"==%=== ==",
	"===%= ===",
	"===%= ===",
	"====%===="
};

#define PULLDOWN_INDICATOR_WIDTH	9
#define PULLDOWN_INDICATOR_HEIGHT 	7

#define CHECK_MARK_WIDTH 	8
#define CHECK_MARK_HEIGHT 	10

static char *CHECK_MARK[] = {
	"======== ",
	"======= #",
	"====== #%",
	"===== #%=",
	" #== #%==",
	" #% #%===",
	" % #%====",
	"  #%=====",
	" #%======",
	"#%======="
};

#define STIPPLE_WIDTH 8
#define STIPPLE_HEIGHT 8
static char STIPPLE_BITS[] = {
	0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55
};

static int userWidgetCount = 0;

/*****  end data  ******/

typedef struct GlyphPixmapRef {
	WMPixmap **image;
	char **data;
	int width, height;
} GlyphPixmapRef;

/* pick the four-tone bevel palette entry for one glyph cell and, when
 * the char is '=', clear the matching mask alpha byte so that cell is
 * outside the shape. Returns the WMColor to write into the image. */
static WMColor *pickCellColor(W_Screen *screen, char c, unsigned char *mask_cell)
{
	switch (c) {
	case ' ':
	case 'w':
		return screen->white;
	case '=':
		if (mask_cell)
			*mask_cell = 0;
		/* FALLTHRU */
	case '.':
	case 'l':
		return screen->gray;
	case '%':
	case 'd':
		return screen->darkGray;
	case '#':
	case 'b':
	default:
		return screen->black;
	}
}

static WMPixmap *makePixmap(W_Screen * sPtr, char **data, int width, int height, int masked)
{
	RContext *rc = sPtr->rcontext;
	xcb_pixmap_t pixmap, mask = XCB_NONE;
	cairo_surface_t *img, *m, *ps;
	cairo_t *cr;
	unsigned char *row, *mrow;
	int stride, mstride, x, y;
	WMColor *c;

	/* build the image and (optional) mask client-side so the whole
	 * glyph uploads in one PutImage each rather than one round trip
	 * per pixel */
	img = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
	stride = cairo_image_surface_get_stride(img);
	m = NULL;
	mstride = 0;
	if (masked) {
		m = cairo_image_surface_create(CAIRO_FORMAT_A8, width, height);
		mstride = cairo_image_surface_get_stride(m);
		memset(cairo_image_surface_get_data(m), 0xff, mstride * height);
	}

	for (y = 0; y < height; y++) {
		row = cairo_image_surface_get_data(img) + y * stride;
		mrow = m ? cairo_image_surface_get_data(m) + y * mstride : NULL;
		for (x = 0; x < width; x++) {
			c = pickCellColor(sPtr, data[y][x], mrow ? &mrow[x] : NULL);
			row[4 * x + 0] = c->color.blue >> 8;
			row[4 * x + 1] = c->color.green >> 8;
			row[4 * x + 2] = c->color.red >> 8;
		}
	}
	cairo_surface_mark_dirty(img);

	pixmap = xcb_generate_id(sPtr->display);
	xcb_create_pixmap(sPtr->display, sPtr->depth, pixmap,
			  W_DRAWABLE(sPtr), width, height);
	ps = cairo_xcb_surface_create(rc->xc, (xcb_drawable_t)pixmap,
				      rc->xvisual, width, height);
	cr = cairo_create(ps);
	cairo_set_source_surface(cr, img, 0, 0);
	cairo_paint(cr);
	cairo_destroy(cr);
	cairo_surface_flush(ps);
	cairo_surface_destroy(ps);
	cairo_surface_destroy(img);

	if (masked) {
		cairo_surface_mark_dirty(m);
		mask = xcb_generate_id(sPtr->display);
		xcb_create_pixmap(sPtr->display, 1, mask,
				  W_DRAWABLE(sPtr), width, height);
		ps = cairo_xcb_surface_create_for_bitmap(rc->xc, rc->xscreen,
							 (xcb_pixmap_t)mask, width, height);
		cr = cairo_create(ps);
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_surface(cr, m, 0, 0);
		cairo_paint(cr);
		cairo_destroy(cr);
		cairo_surface_flush(ps);
		cairo_surface_destroy(ps);
		cairo_surface_destroy(m);
	}

	return WMCreatePixmapFromXPixmaps(sPtr, pixmap, mask, width, height, sPtr->depth);
}

/* re-render a glyph into its existing server pixmap so every widget
 * holding the WMPixmap sees the new system colors on its next repaint
 * (the shapes and masks are color-independent) */
static void refreshGlyphPixmap(W_Screen *sPtr, WMPixmap *pm, char **data,
			       int width, int height)
{
	cairo_surface_t *img, *ps;
	cairo_t *cr;
	unsigned char *row;
	int stride, x, y;
	WMColor *c;

	img = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
	stride = cairo_image_surface_get_stride(img);
	for (y = 0; y < height; y++) {
		row = cairo_image_surface_get_data(img) + y * stride;
		for (x = 0; x < width; x++) {
			c = pickCellColor(sPtr, data[y][x], NULL);
			row[4 * x + 0] = c->color.blue >> 8;
			row[4 * x + 1] = c->color.green >> 8;
			row[4 * x + 2] = c->color.red >> 8;
		}
	}
	cairo_surface_mark_dirty(img);

	ps = cairo_xcb_surface_create(sPtr->rcontext->xc, pm->pixmap,
				      sPtr->rcontext->xvisual, width, height);
	cr = cairo_create(ps);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_surface(cr, img, 0, 0);
	cairo_paint(cr);
	cairo_destroy(cr);
	cairo_surface_flush(ps);
	cairo_surface_destroy(ps);
	cairo_surface_destroy(img);
}

void W_RefreshGlyphPixmaps(W_Screen *sPtr)
{
	GlyphPixmapRef refs[] = {
		{ &sPtr->checkButtonImageOn, CHECK_BUTTON_ON,
		  CHECK_BUTTON_ON_WIDTH, CHECK_BUTTON_ON_HEIGHT },
		{ &sPtr->checkButtonImageOff, CHECK_BUTTON_OFF,
		  CHECK_BUTTON_OFF_WIDTH, CHECK_BUTTON_OFF_HEIGHT },
		{ &sPtr->radioButtonImageOn, RADIO_BUTTON_ON,
		  RADIO_BUTTON_ON_WIDTH, RADIO_BUTTON_ON_HEIGHT },
		{ &sPtr->radioButtonImageOff, RADIO_BUTTON_OFF,
		  RADIO_BUTTON_OFF_WIDTH, RADIO_BUTTON_OFF_HEIGHT },
		{ &sPtr->tristateButtonImageOn, TRISTATE_BUTTON_ON,
		  TRISTATE_BUTTON_ON_WIDTH, TRISTATE_BUTTON_ON_HEIGHT },
		{ &sPtr->tristateButtonImageOff, TRISTATE_BUTTON_OFF,
		  TRISTATE_BUTTON_OFF_WIDTH, TRISTATE_BUTTON_OFF_HEIGHT },
		{ &sPtr->tristateButtonImageTri, TRISTATE_BUTTON_TRI,
		  TRISTATE_BUTTON_TRI_WIDTH, TRISTATE_BUTTON_TRI_HEIGHT },
		{ &sPtr->buttonArrow, BUTTON_ARROW,
		  BUTTON_ARROW_WIDTH, BUTTON_ARROW_HEIGHT },
		{ &sPtr->pushedButtonArrow, BUTTON_ARROW2,
		  BUTTON_ARROW2_WIDTH, BUTTON_ARROW2_HEIGHT },
		{ &sPtr->scrollerDimple, SCROLLER_DIMPLE,
		  SCROLLER_DIMPLE_WIDTH, SCROLLER_DIMPLE_HEIGHT },
		{ &sPtr->upArrow, SCROLLER_ARROW_UP,
		  SCROLLER_ARROW_UP_WIDTH, SCROLLER_ARROW_UP_HEIGHT },
		{ &sPtr->downArrow, SCROLLER_ARROW_DOWN,
		  SCROLLER_ARROW_DOWN_WIDTH, SCROLLER_ARROW_DOWN_HEIGHT },
		{ &sPtr->leftArrow, SCROLLER_ARROW_LEFT,
		  SCROLLER_ARROW_LEFT_WIDTH, SCROLLER_ARROW_LEFT_HEIGHT },
		{ &sPtr->rightArrow, SCROLLER_ARROW_RIGHT,
		  SCROLLER_ARROW_RIGHT_WIDTH, SCROLLER_ARROW_RIGHT_HEIGHT },
		{ &sPtr->hiUpArrow, HI_SCROLLER_ARROW_UP,
		  SCROLLER_ARROW_UP_WIDTH, SCROLLER_ARROW_UP_HEIGHT },
		{ &sPtr->hiDownArrow, HI_SCROLLER_ARROW_DOWN,
		  SCROLLER_ARROW_DOWN_WIDTH, SCROLLER_ARROW_DOWN_HEIGHT },
		{ &sPtr->hiLeftArrow, HI_SCROLLER_ARROW_LEFT,
		  SCROLLER_ARROW_LEFT_WIDTH, SCROLLER_ARROW_LEFT_HEIGHT },
		{ &sPtr->hiRightArrow, HI_SCROLLER_ARROW_RIGHT,
		  SCROLLER_ARROW_RIGHT_WIDTH, SCROLLER_ARROW_RIGHT_HEIGHT },
		{ &sPtr->popUpIndicator, POPUP_INDICATOR,
		  POPUP_INDICATOR_WIDTH, POPUP_INDICATOR_HEIGHT },
		{ &sPtr->pullDownIndicator, PULLDOWN_INDICATOR,
		  PULLDOWN_INDICATOR_WIDTH, PULLDOWN_INDICATOR_HEIGHT },
		{ &sPtr->checkMark, CHECK_MARK, CHECK_MARK_WIDTH, CHECK_MARK_HEIGHT },
	};
	size_t i;

	for (i = 0; i < sizeof(refs) / sizeof(refs[0]); i++) {
		if (*refs[i].image)
			refreshGlyphPixmap(sPtr, *refs[i].image, refs[i].data,
					   refs[i].width, refs[i].height);
	}
}

#define T_WINGS_IMAGES_FILE  RESOURCE_PATH"/Images.tiff"
#define X_WINGS_IMAGES_FILE  RESOURCE_PATH"/Images.xpm"

static bool loadPixmaps(WMScreen * scr)
{
	RImage *image, *tmp;
	RColor gray;
	RColor white;
	WMColor *sysgray;

	sysgray = WMGrayColor(scr);
	gray = WMGetRColorFromColor(sysgray);
	WMReleaseColor(sysgray);

	white.red = 0xff;
	white.green = 0xff;
	white.blue = 0xff;

	image = RLoadImage(scr->rcontext, T_WINGS_IMAGES_FILE, 0);
	if (!image)
		image = RLoadImage(scr->rcontext, X_WINGS_IMAGES_FILE, 0);
	if (!image) {
		wwarning(_("WINGs: could not load widget images file: %s"), RMessageForError(RErrorCode));
		return false;
	}
	/* home icon */
	/* make it have a gray background */
	tmp = RGetSubImage(image, 0, 0, 24, 24);
	RCombineImageWithColor(tmp, &gray);
	scr->homeIcon = WMCreatePixmapFromRImage(scr, tmp, 128);
	RReleaseImage(tmp);
	/* make it have a white background */
	tmp = RGetSubImage(image, 0, 0, 24, 24);
	RCombineImageWithColor(tmp, &white);
	scr->altHomeIcon = WMCreatePixmapFromRImage(scr, tmp, 128);
	RReleaseImage(tmp);

	/* trash can */
	tmp = RGetSubImage(image, 104, 0, 24, 24);
	RCombineImageWithColor(tmp, &white);
	scr->trashcanIcon = WMCreatePixmapFromRImage(scr, tmp, 128);
	RReleaseImage(tmp);
	tmp = RGetSubImage(image, 104, 0, 24, 24);
	RCombineImageWithColor(tmp, &white);
	scr->altTrashcanIcon = WMCreatePixmapFromRImage(scr, tmp, 128);
	RReleaseImage(tmp);
	/* create dir */
	tmp = RGetSubImage(image, 104, 24, 24, 24);
	RCombineImageWithColor(tmp, &white);
	scr->createDirIcon = WMCreatePixmapFromRImage(scr, tmp, 128);
	RReleaseImage(tmp);
	tmp = RGetSubImage(image, 104, 24, 24, 24);
	RCombineImageWithColor(tmp, &white);
	scr->altCreateDirIcon = WMCreatePixmapFromRImage(scr, tmp, 128);
	RReleaseImage(tmp);
	/* diskettes */
	tmp = RGetSubImage(image, 24, 80, 24, 24);
	RCombineImageWithColor(tmp, &white);
	scr->disketteIcon = WMCreatePixmapFromRImage(scr, tmp, 128);
	RReleaseImage(tmp);
	tmp = RGetSubImage(image, 24, 80, 24, 24);
	RCombineImageWithColor(tmp, &white);
	scr->altDisketteIcon = WMCreatePixmapFromRImage(scr, tmp, 128);
	RReleaseImage(tmp);
	/* unmount */
	tmp = RGetSubImage(image, 0, 80, 24, 24);
	RCombineImageWithColor(tmp, &white);
	scr->unmountIcon = WMCreatePixmapFromRImage(scr, tmp, 128);
	RReleaseImage(tmp);
	tmp = RGetSubImage(image, 0, 80, 24, 24);
	RCombineImageWithColor(tmp, &white);
	scr->altUnmountIcon = WMCreatePixmapFromRImage(scr, tmp, 128);
	RReleaseImage(tmp);

	/* Magnifying Glass Icon for ColorPanel */
	tmp = RGetSubImage(image, 24, 0, 40, 32);
	RCombineImageWithColor(tmp, &gray);
	scr->magnifyIcon = WMCreatePixmapFromRImage(scr, tmp, 128);
	RReleaseImage(tmp);
	/* ColorWheel Icon for ColorPanel */
	tmp = RGetSubImage(image, 0, 25, 24, 24);
	scr->wheelIcon = WMCreatePixmapFromRImage(scr, tmp, 128);
	RReleaseImage(tmp);
	/* GrayScale Icon for ColorPanel */
	tmp = RGetSubImage(image, 65, 0, 40, 24);
	scr->grayIcon = WMCreatePixmapFromRImage(scr, tmp, 128);
	RReleaseImage(tmp);
	/* RGB Icon for ColorPanel */
	tmp = RGetSubImage(image, 25, 33, 40, 24);
	scr->rgbIcon = WMCreatePixmapFromRImage(scr, tmp, 128);
	RReleaseImage(tmp);
	/* CMYK Icon for ColorPanel */
	tmp = RGetSubImage(image, 65, 25, 40, 24);
	scr->cmykIcon = WMCreatePixmapFromRImage(scr, tmp, 128);
	RReleaseImage(tmp);
	/* HSB Icon for ColorPanel */
	tmp = RGetSubImage(image, 0, 57, 40, 24);
	scr->hsbIcon = WMCreatePixmapFromRImage(scr, tmp, 128);
	RReleaseImage(tmp);
	/* CustomColorPalette Icon for ColorPanel */
	tmp = RGetSubImage(image, 81, 57, 40, 24);
	scr->customPaletteIcon = WMCreatePixmapFromRImage(scr, tmp, 128);
	RReleaseImage(tmp);
	/* ColorList Icon for ColorPanel */
	tmp = RGetSubImage(image, 41, 57, 40, 24);
	scr->colorListIcon = WMCreatePixmapFromRImage(scr, tmp, 128);
	RReleaseImage(tmp);

	RReleaseImage(image);

	return true;
}

WMScreen *WMOpenScreen(const char *display)
{
	xcb_connection_t *xc;
	int sn;

	xc = xcb_connect(display, &sn);
	if (xcb_connection_has_error(xc)) {
		wwarning(_("WINGs: could not open display %s"),
			 display ? display : getenv("DISPLAY"));
		xcb_disconnect(xc);
		return NULL;
	}

	return WMCreateSimpleApplicationScreen(xc, sn);
}

WMScreen *WMCreateSimpleApplicationScreen(xcb_connection_t * display, int screen)
{
	WMScreen *scr;

	scr = WMCreateScreen(display, screen);

	scr->aflags.hasAppIcon = 0;
	scr->aflags.simpleApplication = 1;

	return scr;
}

WMScreen *WMCreateScreen(xcb_connection_t * display, int screen)
{
	return WMCreateScreenWithRContext(display, screen,
			RCreateContextXCB(display, screen, NULL));
}

/* the two masks every keyboard path shares: what the grab fan-out
 * drops and which bit the widgets read as Alt */
void W_UpdateScreenModifierMasks(W_Screen *scrPtr)
{
	WXModifierSlots slots;
	struct xkb_keymap *keymap;
	unsigned int altMask, metaMask;

	keymap = scrPtr->xkb ? xkb_state_get_keymap(W_XkbStateOf(scrPtr))
			     : NULL;
	W_XkbModifierSlots(scrPtr->display, keymap, &slots);

	altMask = slots.alt >= 0 ? 1u << slots.alt : 0;
	metaMask = slots.meta >= 0 ? 1u << slots.meta : 0;

	scrPtr->ignoredModifierMask =
		(slots.numlock >= 0 ? 1u << slots.numlock : 0)
		| (slots.scrolllock >= 0 ? 1u << slots.scrolllock : 0)
		| XCB_MOD_MASK_LOCK;
	scrPtr->altModifierMask = altMask ? altMask
		: (metaMask ? metaMask : XCB_MOD_MASK_1);
}

WMScreen *WMCreateScreenWithRContext(xcb_connection_t * display, int screen, RContext * context)
{
	W_Screen *scrPtr;
	static int initialized = 0;
	static int fcInitialized;
	int i;
	int exists[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	xcb_list_properties_cookie_t pck;
	xcb_list_properties_reply_t *prep;
	int plen;
	WXColor bla;
	xcb_pixmap_t blank;

	if (!initialized) {

		initialized = 1;

		W_ReadConfigurations();

		assert(W_ApplicationInitialized());
	}

	scrPtr = malloc(sizeof(W_Screen));
	if (!scrPtr)
		return NULL;
	memset(scrPtr, 0, sizeof(W_Screen));

	scrPtr->aflags.hasAppIcon = 1;

	scrPtr->display = display;
	scrPtr->screen = screen;
	scrPtr->rcontext = context;

	scrPtr->depth = context->depth;
	scrPtr->lastEventTime = 0;
	scrPtr->colormap = context->cmap;
	/* wraster picked the visualtype on the xcb side, keep it */
	scrPtr->visual = wXVisualTypeForID(display, context->xvisual->visual_id);
	if (scrPtr->visual == NULL)
		scrPtr->visual = wXDefaultVisualType(display, screen);
	if (context->xvisual_argb)
		scrPtr->visual_argb = wXVisualTypeForID(display,
						context->xvisual_argb->visual_id);

	scrPtr->rootWin = wXRootWindow(display, screen);

	/* FcPatternGetString() warns unless FcInit() was called. Do it once,
	 * WMShutdownFonts() balances a single reference. */
	if (!fcInitialized) {
		FcInit();
		fcInitialized = 1;
	}

	scrPtr->fontCache = WMCreateHashTable(WMStringPointerHashCallbacks);
	scrPtr->drawableSize = WMCreateHashTable(WMIntHashCallbacks);
	scrPtr->compositor_running = W_ScreenCheckCompositor(scrPtr);
	WMScreenReadUIScale(scrPtr);

	/* Create missing CUT_BUFFERs */
	pck = xcb_list_properties(display, scrPtr->rootWin);
	prep = xcb_list_properties_reply(display, pck, NULL);
	if (prep) {
		const xcb_atom_t *props = xcb_list_properties_atoms(prep);

		plen = xcb_list_properties_atoms_length(prep);

		for (i = 0; i < plen; i++) {
			switch (props[i]) {
			case XCB_ATOM_CUT_BUFFER0:
				exists[0] = 1;
				break;
			case XCB_ATOM_CUT_BUFFER1:
				exists[1] = 1;
				break;
			case XCB_ATOM_CUT_BUFFER2:
				exists[2] = 1;
				break;
			case XCB_ATOM_CUT_BUFFER3:
				exists[3] = 1;
				break;
			case XCB_ATOM_CUT_BUFFER4:
				exists[4] = 1;
				break;
			case XCB_ATOM_CUT_BUFFER5:
				exists[5] = 1;
				break;
			case XCB_ATOM_CUT_BUFFER6:
				exists[6] = 1;
				break;
			case XCB_ATOM_CUT_BUFFER7:
				exists[7] = 1;
				break;
			default:
				break;
			}
		}
		free(prep);
	}
	for (i = 0; i < 8; i++) {
		if (!exists[i]) {
			wXStoreBuffer(display, screen, "", 0, i);
		}
	}

	scrPtr->ignoredModifierMask = 0;
	scrPtr->altModifierMask = 0;

	/* load the xkbcommon keymap first so the modifier classification
	 * can resolve the lock-key keycodes against it */
	W_InitXkb(scrPtr);

	W_UpdateScreenModifierMasks(scrPtr);

	/* initially allocate some colors */
	WMWhiteColor(scrPtr);
	WMBlackColor(scrPtr);
	WMGrayColor(scrPtr);
	WMDarkGrayColor(scrPtr);

	scrPtr->stipple = wXCreateBitmapFromData(display, W_DRAWABLE(scrPtr),
						 STIPPLE_BITS, STIPPLE_WIDTH, STIPPLE_HEIGHT);

	scrPtr->antialiasedText = WINGsConfiguration.antialiasedText;

	scrPtr->normalFont = WMSystemFontOfSize(scrPtr, 0);

	scrPtr->boldFont = WMBoldSystemFontOfSize(scrPtr, 0);

	if (!scrPtr->boldFont)
		scrPtr->boldFont = scrPtr->normalFont;

	if (!scrPtr->normalFont) {
		wwarning(_("could not load any fonts. Make sure your font installation"
			   " and locale settings are correct."));

		return NULL;
	}

	/* input method stuff, after the xkbcommon stack loaded above */
	W_InitIM(scrPtr);

	scrPtr->checkButtonImageOn = makePixmap(scrPtr, CHECK_BUTTON_ON,
						CHECK_BUTTON_ON_WIDTH, CHECK_BUTTON_ON_HEIGHT, false);

	scrPtr->checkButtonImageOff = makePixmap(scrPtr, CHECK_BUTTON_OFF,
						 CHECK_BUTTON_OFF_WIDTH, CHECK_BUTTON_OFF_HEIGHT, false);

	scrPtr->radioButtonImageOn = makePixmap(scrPtr, RADIO_BUTTON_ON,
						RADIO_BUTTON_ON_WIDTH, RADIO_BUTTON_ON_HEIGHT, false);

	scrPtr->radioButtonImageOff = makePixmap(scrPtr, RADIO_BUTTON_OFF,
						 RADIO_BUTTON_OFF_WIDTH, RADIO_BUTTON_OFF_HEIGHT, false);

	scrPtr->tristateButtonImageOn = makePixmap(scrPtr, TRISTATE_BUTTON_ON,
	                                           TRISTATE_BUTTON_ON_WIDTH, TRISTATE_BUTTON_ON_HEIGHT, false);

	scrPtr->tristateButtonImageOff = makePixmap(scrPtr, TRISTATE_BUTTON_OFF,
	                                            TRISTATE_BUTTON_OFF_WIDTH, TRISTATE_BUTTON_OFF_HEIGHT, false);

	scrPtr->tristateButtonImageTri = makePixmap(scrPtr, TRISTATE_BUTTON_TRI,
	                                            TRISTATE_BUTTON_TRI_WIDTH, TRISTATE_BUTTON_TRI_HEIGHT, false);

	/* allocate the two widget colors now so paint paths (popup menu
	 * highlights) can read the screen fields without a NULL check */
	WMReleaseColor(W_TextFieldBackColor(scrPtr));
	WMReleaseColor(W_SelectedBackColor(scrPtr));

	scrPtr->buttonArrow = makePixmap(scrPtr, BUTTON_ARROW, BUTTON_ARROW_WIDTH, BUTTON_ARROW_HEIGHT, false);

	scrPtr->pushedButtonArrow = makePixmap(scrPtr, BUTTON_ARROW2,
					       BUTTON_ARROW2_WIDTH, BUTTON_ARROW2_HEIGHT, false);

	scrPtr->scrollerDimple = makePixmap(scrPtr, SCROLLER_DIMPLE,
					    SCROLLER_DIMPLE_WIDTH, SCROLLER_DIMPLE_HEIGHT, false);

	scrPtr->upArrow = makePixmap(scrPtr, SCROLLER_ARROW_UP,
				     SCROLLER_ARROW_UP_WIDTH, SCROLLER_ARROW_UP_HEIGHT, true);

	scrPtr->downArrow = makePixmap(scrPtr, SCROLLER_ARROW_DOWN,
				       SCROLLER_ARROW_DOWN_WIDTH, SCROLLER_ARROW_DOWN_HEIGHT, true);

	scrPtr->leftArrow = makePixmap(scrPtr, SCROLLER_ARROW_LEFT,
				       SCROLLER_ARROW_LEFT_WIDTH, SCROLLER_ARROW_LEFT_HEIGHT, true);

	scrPtr->rightArrow = makePixmap(scrPtr, SCROLLER_ARROW_RIGHT,
					SCROLLER_ARROW_RIGHT_WIDTH, SCROLLER_ARROW_RIGHT_HEIGHT, true);

	scrPtr->hiUpArrow = makePixmap(scrPtr, HI_SCROLLER_ARROW_UP,
				       SCROLLER_ARROW_UP_WIDTH, SCROLLER_ARROW_UP_HEIGHT, true);

	scrPtr->hiDownArrow = makePixmap(scrPtr, HI_SCROLLER_ARROW_DOWN,
					 SCROLLER_ARROW_DOWN_WIDTH, SCROLLER_ARROW_DOWN_HEIGHT, true);

	scrPtr->hiLeftArrow = makePixmap(scrPtr, HI_SCROLLER_ARROW_LEFT,
					 SCROLLER_ARROW_LEFT_WIDTH, SCROLLER_ARROW_LEFT_HEIGHT, true);

	scrPtr->hiRightArrow = makePixmap(scrPtr, HI_SCROLLER_ARROW_RIGHT,
					  SCROLLER_ARROW_RIGHT_WIDTH, SCROLLER_ARROW_RIGHT_HEIGHT, true);

	scrPtr->popUpIndicator = makePixmap(scrPtr, POPUP_INDICATOR,
					    POPUP_INDICATOR_WIDTH, POPUP_INDICATOR_HEIGHT, true);

	scrPtr->pullDownIndicator = makePixmap(scrPtr, PULLDOWN_INDICATOR,
					       PULLDOWN_INDICATOR_WIDTH, PULLDOWN_INDICATOR_HEIGHT, true);

	scrPtr->checkMark = makePixmap(scrPtr, CHECK_MARK, CHECK_MARK_WIDTH, CHECK_MARK_HEIGHT, true);

	loadPixmaps(scrPtr);

	scrPtr->defaultCursor = wXLoadCursor(display, screen, "left_ptr");

	scrPtr->textCursor = wXLoadCursor(display, screen, "xterm");

	blank = wXCreateBitmapFromData(display, W_DRAWABLE(scrPtr), "\0", 1, 1);
	scrPtr->invisibleCursor = wXCreatePixmapCursor(display, blank, blank, &bla, &bla, 0, 0);
	wXFreePixmap(display, blank);

	WXStaticAtomsInit(display);

	scrPtr->attribsAtom = wx_static_atoms[W_ATOM_GNUSTEP_WM_ATTR];

	scrPtr->deleteWindowAtom = wx_static_atoms[W_ATOM_WM_DELETE_WINDOW];

	scrPtr->protocolsAtom = wx_static_atoms[W_ATOM_WM_PROTOCOLS];

	scrPtr->clipboardAtom = wx_static_atoms[W_ATOM_CLIPBOARD];

	scrPtr->xdndAwareAtom = wx_static_atoms[W_ATOM_XDND_AWARE];
	scrPtr->xdndSelectionAtom = wx_static_atoms[W_ATOM_XDND_SELECTION];
	scrPtr->xdndEnterAtom = wx_static_atoms[W_ATOM_XDND_ENTER];
	scrPtr->xdndLeaveAtom = wx_static_atoms[W_ATOM_XDND_LEAVE];
	scrPtr->xdndPositionAtom = wx_static_atoms[W_ATOM_XDND_POSITION];
	scrPtr->xdndDropAtom = wx_static_atoms[W_ATOM_XDND_DROP];
	scrPtr->xdndFinishedAtom = wx_static_atoms[W_ATOM_XDND_FINISHED];
	scrPtr->xdndTypeListAtom = wx_static_atoms[W_ATOM_XDND_TYPE_LIST];
	scrPtr->xdndActionListAtom = wx_static_atoms[W_ATOM_XDND_ACTION_LIST];
	scrPtr->xdndActionDescriptionAtom = wx_static_atoms[W_ATOM_XDND_ACTION_DESCRIPTION];
	scrPtr->xdndStatusAtom = wx_static_atoms[W_ATOM_XDND_STATUS];

	scrPtr->xdndActionCopy = wx_static_atoms[W_ATOM_XDND_ACTION_COPY];
	scrPtr->xdndActionMove = wx_static_atoms[W_ATOM_XDND_ACTION_MOVE];
	scrPtr->xdndActionLink = wx_static_atoms[W_ATOM_XDND_ACTION_LINK];
	scrPtr->xdndActionAsk = wx_static_atoms[W_ATOM_XDND_ACTION_ASK];
	scrPtr->xdndActionPrivate = wx_static_atoms[W_ATOM_XDND_ACTION_PRIVATE];

	scrPtr->wmIconDragOffsetAtom = wx_static_atoms[W_ATOM_WINGS_DND_MOUSE_OFFSET];

	scrPtr->wmStateAtom = wx_static_atoms[W_ATOM_WM_STATE];

	scrPtr->utf8String = wx_static_atoms[W_ATOM_UTF8_STRING];
	scrPtr->netwmName = wx_static_atoms[W_ATOM_NET_WM_NAME];
	scrPtr->netwmIconName = wx_static_atoms[W_ATOM_NET_WM_ICON_NAME];
	scrPtr->netwmIcon = wx_static_atoms[W_ATOM_NET_WM_ICON];

	scrPtr->rootView = W_CreateRootView(scrPtr);

	scrPtr->balloon = W_CreateBalloon(scrPtr);

	W_InitApplication(scrPtr);

	return scrPtr;
}

void WMSetWidgetDefaultFont(WMScreen * scr, WMFont * font)
{
	WMReleaseFont(scr->normalFont);
	scr->normalFont = WMRetainFont(font);
}

void WMSetWidgetDefaultBoldFont(WMScreen * scr, WMFont * font)
{
	WMReleaseFont(scr->boldFont);
	scr->boldFont = WMRetainFont(font);
}

/* Widgets borrow normalFont/boldFont without retaining, so destroy the
 * windows before releasing them. boldFont may alias normalFont. */
void WMShutdownScreen(WMScreen *scr)
{
	WMFont *normal, *bold;

	if (!scr)
		return;

	while (scr->windowList)
		WMDestroyWidget((WMWidget *) scr->windowList);

	normal = scr->normalFont;
	bold = scr->boldFont;
	scr->normalFont = NULL;
	scr->boldFont = NULL;
	if (normal)
		WMReleaseFont(normal);
	if (bold && bold != normal)
		WMReleaseFont(bold);

	W_DeinitXkb(scr);
}

void WMHangData(WMWidget * widget, void *data)
{
	W_VIEW(widget)->hangedData = data;
}

void *WMGetHangedData(WMWidget * widget)
{
	return W_VIEW(widget)->hangedData;
}

void WMDestroyWidget(WMWidget * widget)
{
	W_UnmapView(W_VIEW(widget));
	W_DestroyView(W_VIEW(widget));
}

void WMSetFocusToWidget(WMWidget * widget)
{
	W_SetFocusOfTopLevel(W_TopLevelOfView(W_VIEW(widget)), W_VIEW(widget));
}

/*
 * WMRealizeWidget-
 * 	Realizes the widget and all its children.
 *
 */
void WMRealizeWidget(WMWidget * w)
{
	W_RealizeView(W_VIEW(w));
}

bool WMRealizeWidgetChecked(WMWidget *w)
{
	return W_RealizeViewChecked(W_VIEW(w));
}

void WMMapWidget(WMWidget * w)
{
	W_MapView(W_VIEW(w));
}

void WMReparentWidget(WMWidget * w, WMWidget * newParent, int x, int y)
{
	W_ReparentView(W_VIEW(w), W_VIEW(newParent), x, y);
}

static void makeChildrenAutomap(W_View * view, int flag)
{
	view = view->childrenList;

	while (view) {
		view->flags.mapWhenRealized = flag;
		makeChildrenAutomap(view, flag);

		view = view->nextSister;
	}
}

void WMMapSubwidgets(WMWidget * w)
{
	/* make sure that subwidgets created after the parent was realized
	 * are mapped too */
	if (!W_VIEW(w)->flags.realized) {
		makeChildrenAutomap(W_VIEW(w), true);
	} else {
		W_MapSubviews(W_VIEW(w));
	}
}

void WMUnmapSubwidgets(WMWidget * w)
{
	if (!W_VIEW(w)->flags.realized) {
		makeChildrenAutomap(W_VIEW(w), false);
	} else {
		W_UnmapSubviews(W_VIEW(w));
	}
}

void WMUnmapWidget(WMWidget * w)
{
	W_UnmapView(W_VIEW(w));
}

bool WMWidgetIsMapped(WMWidget * w)
{
	return W_VIEW(w)->flags.mapped;
}

void WMSetWidgetBackgroundColor(WMWidget * w, WMColor * color)
{
	W_SetViewBackgroundColor(W_VIEW(w), color);
	if (W_VIEW(w)->flags.mapped)
		WMRedisplayWidget(w);
}

WMColor *WMGetWidgetBackgroundColor(WMWidget * w)
{
	return W_VIEW(w)->backColor;
}

void WMSetWidgetBackgroundPixmap(WMWidget *w, WMPixmap *pix)
{
	if (!pix)
		return;

	W_SetViewBackgroundPixmap(W_VIEW(w), pix);
	if (W_VIEW(w)->flags.mapped)
		WMRedisplayWidget(w);
}

WMPixmap *WMGetWidgetBackgroundPixmap(WMWidget *w)
{
	return W_VIEW(w)->backImage;
}

void WMRaiseWidget(WMWidget * w)
{
	W_RaiseView(W_VIEW(w));
}

void WMLowerWidget(WMWidget * w)
{
	W_LowerView(W_VIEW(w));
}

void WMMoveWidget(WMWidget * w, int x, int y)
{
	W_MoveView(W_VIEW(w), x, y);
}

void WMResizeWidget(WMWidget * w, unsigned int width, unsigned int height)
{
	W_ResizeView(W_VIEW(w), width, height);
}

W_Class W_RegisterUserWidget(void)
{
	userWidgetCount++;

	return userWidgetCount + WC_UserWidget - 1;
}

RContext *WMScreenRContext(WMScreen * scr)
{
	return scr->rcontext;
}

unsigned int WMWidgetWidth(WMWidget * w)
{
	return W_VIEW(w)->size.width;
}

unsigned int WMWidgetHeight(WMWidget * w)
{
	return W_VIEW(w)->size.height;
}

xcb_window_t WMWidgetXID(WMWidget * w)
{
	return W_VIEW(w)->window;
}

WMScreen *WMWidgetScreen(WMWidget * w)
{
	return W_VIEW(w)->screen;
}

void WMScreenMainLoop(WMScreen * scr)
{
	xcb_generic_event_t *event = NULL;

	while (1) {
		WMNextEvent(scr->display, &event);
		if (!event)
			break;
		WMHandleEvent(event);
		free(event);
		event = NULL;
	}
}

void WMBreakModalLoop(WMScreen * scr)
{
	scr->modalLoop = 0;
}

void WMRunModalLoop(WMScreen * scr, WMView * view)
{
	/* why is scr passed if is determined from the view? */
	/*WMScreen *scr = view->screen; */
	int oldModalLoop = scr->modalLoop;
	WMView *oldModalView = scr->modalView;

	scr->modalView = view;

	scr->modalLoop = 1;
	while (scr->modalLoop) {
		xcb_generic_event_t *event = NULL;

		WMNextEvent(scr->display, &event);
		if (!event)
			break;
		WMHandleEvent(event);
		free(event);
	}

	scr->modalView = oldModalView;
	scr->modalLoop = oldModalLoop;
}

xcb_connection_t *WMScreenDisplay(WMScreen * scr)
{
	return scr->display;
}

int WMScreenDepth(WMScreen * scr)
{
	return scr->depth;
}

double WMScreenUIScale(WMScreen *scr)
{
	return scr && scr->uiScale > 0.0 ? scr->uiScale : 1.0;
}

/* scan a RESOURCE_MANAGER buffer for a single "key: value" line, matching
 * key at the start of a line so a substring like Xft.dpi does not fire on
 * SomeOtherThing.Xft.dpi, and return a pointer to the first non-blank
 * byte of the value, the buffer is NUL-terminated by WXGetLatin1String
 * so callers can hand the return straight to strtod */
static const char *findResourceValue(const char *buf, const char *key)
{
	const char *p;
	size_t klen;

	klen = strlen(key);
	p = buf;
	while (p) {
		if (strncmp(p, key, klen) == 0 && p[klen] == ':') {
			p += klen + 1;
			while (*p == ' ' || *p == '\t')
				p++;
			return p;
		}
		p = strchr(p, '\n');
		if (p)
			p++;
	}
	return NULL;
}

/* read Xft.dpi from one screen's RESOURCE_MANAGER property, returning
 * false when the property or the key is absent. xcb-util-xrm is not
 * needed because Xft.dpi is by convention a plain literal key with no
 * wildcard resolution. */
static bool readDPIOnScreen(WMScreen *scr, int screen, double *dpi)
{
	const char *val;
	char *rm;

	*dpi = 0.0;
	rm = WXGetLatin1String(scr->display, wXRootWindow(scr->display, screen),
			       XCB_ATOM_RESOURCE_MANAGER, NULL);
	if (!rm)
		return false;
	val = findResourceValue(rm, "Xft.dpi");
	if (!val) {
		wfree(rm);
		return false;
	}
	*dpi = strtod(val, NULL);
	wfree(rm);
	return true;
}

/* Read Xft.dpi from the screen's own root first, then screen 0. Missing
 * keys keep 1.0 rather than using physical size, and values below 1.0
 * are clamped because only the up-scale path is implemented. */
static double readUIScale(WMScreen *scr, double *dpi)
{
	double d, s;

	if (!readDPIOnScreen(scr, scr->screen, &d) && scr->screen != 0)
		readDPIOnScreen(scr, 0, &d);
	s = d > 0.0 ? d / 96.0 : 1.0;
	if (s > 4.0)
		s = 4.0;
	if (s < 1.0)
		s = 1.0;
	if (dpi)
		*dpi = d;
	return s;
}

void WMScreenReadUIScale(WMScreen *scr)
{
	double d, s;

	s = readUIScale(scr, &d);
	if (scr->uiScale <= 0.0 || (s - scr->uiScale) * (s - scr->uiScale) > 0.0001) {
		scr->uiScale = s;
		if (d > 0.0)
			wmessage("screen %d: Xft.dpi %.0f (scale %.2f)",
				 scr->screen, d, s);
	}
}

double WMScreenPeekUIScale(WMScreen *scr)
{
	return readUIScale(scr, NULL);
}

unsigned int WMScreenWidth(WMScreen * scr)
{
	return scr->rootView->size.width;
}

unsigned int WMScreenHeight(WMScreen * scr)
{
	return scr->rootView->size.height;
}

void WMRedisplayWidget(WMWidget * w)
{
	W_RedisplayView(W_VIEW(w));
}
