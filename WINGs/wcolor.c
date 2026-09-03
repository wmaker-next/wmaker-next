
#include <cairo.h>

#include "WINGsP.h"

#include "wconfig.h"

#include <wraster.h>

static WMColor *createRGBAColor(WMScreen * scr, unsigned short red,
				unsigned short green, unsigned short blue, unsigned short alpha);

/*
 * TODO: make the color creation code return the same WMColor for the
 * same colors.
 * make findCloseColor() find the closest color in the RContext pallette
 * or in the other colors allocated by WINGs.
 */

static int allocPixel(WMScreen *scr, WXColor *xcolor)
{
	RColor rcolor;

	if (scr->rcontext->vclass == XCB_VISUAL_CLASS_TRUE_COLOR) {
		/* AllocColor on a TrueColor colormap is a server round-trip
		 * that returns the value RGetClosestXColor computes locally
		 * from the visual masks, so derive the pixel here. */
		rcolor.red = xcolor->red >> 8;
		rcolor.green = xcolor->green >> 8;
		rcolor.blue = xcolor->blue >> 8;
		rcolor.alpha = 0;
		return RGetClosestXColor(scr->rcontext, &rcolor, &xcolor->pixel);
	}

	return wXAllocColor(scr->display, scr->colormap, xcolor);
}

static WMColor *findCloseColor(WMScreen * scr, unsigned short red, unsigned short green,
			       unsigned short blue, unsigned short alpha)
{
	WMColor *color;
	WXColor xcolor;
	RColor rcolor;

	rcolor.red = red >> 8;
	rcolor.green = green >> 8;
	rcolor.blue = blue >> 8;
	rcolor.alpha = alpha >> 8;

	xcolor.red = red;
	xcolor.green = green;
	xcolor.blue = blue;
	if (!RGetClosestXColor(scr->rcontext, &rcolor, &xcolor.pixel))
		return NULL;

	if (scr->rcontext->vclass != XCB_VISUAL_CLASS_TRUE_COLOR &&
	    !wXAllocColor(scr->display, scr->colormap, &xcolor))
		return NULL;

	color = wmalloc(sizeof(WMColor));

	color->screen = scr;
	color->refCount = 1;
	color->color = xcolor;
	color->alpha = alpha;
	color->flags.exact = 0;

	return color;
}

static WMColor *createRGBAColor(WMScreen * scr, unsigned short red, unsigned short green,
				unsigned short blue, unsigned short alpha)
{
	WMColor *color;
	WXColor xcolor;

	xcolor.red = red;
	xcolor.green = green;
	xcolor.blue = blue;
	if (!allocPixel(scr, &xcolor))
		return NULL;

	color = wmalloc(sizeof(WMColor));

	color->screen = scr;
	color->refCount = 1;
	color->color = xcolor;
	color->alpha = alpha;
	color->flags.exact = 1;

	return color;
}

WMColor *WMCreateRGBColor(WMScreen * scr, unsigned short red, unsigned short green,
			  unsigned short blue, bool exact)
{
	WMColor *color = NULL;

	if (!exact || !(color = createRGBAColor(scr, red, green, blue, 0xffff))) {
		color = findCloseColor(scr, red, green, blue, 0xffff);
	}
	if (!color)
		color = scr->black;

	return color;
}

RColor WMGetRColorFromColor(WMColor * color)
{
	RColor rcolor;

	rcolor.red = color->color.red >> 8;
	rcolor.green = color->color.green >> 8;
	rcolor.blue = color->color.blue >> 8;
	rcolor.alpha = color->alpha >> 8;

	return rcolor;
}

WMColor *WMCreateRGBAColor(WMScreen * scr, unsigned short red, unsigned short green,
			   unsigned short blue, unsigned short alpha, bool exact)
{
	WMColor *color = NULL;

	if (!exact || !(color = createRGBAColor(scr, red, green, blue, alpha))) {
		color = findCloseColor(scr, red, green, blue, alpha);
	}
	if (!color)
		color = scr->black;

	return color;
}

WMColor *WMCreateNamedColor(WMScreen * scr, const char *name, bool exact)
{
	WMColor *color;
	WXColor xcolor;

	if (!wXParseColor(scr->display, scr->colormap, name, &xcolor))
		return NULL;

	if (scr->visual->_class == XCB_VISUAL_CLASS_TRUE_COLOR)
		exact = true;

	if (!exact || !(color = createRGBAColor(scr, xcolor.red, xcolor.green, xcolor.blue, 0xffff))) {
		color = findCloseColor(scr, xcolor.red, xcolor.green, xcolor.blue, 0xffff);
	}
	return color;
}

WMColor *WMRetainColor(WMColor * color)
{
	assert(color != NULL);

	color->refCount++;

	return color;
}

void WMReleaseColor(WMColor * color)
{
	color->refCount--;

	if (color->refCount < 1) {
		if (color->screen->rcontext->vclass != XCB_VISUAL_CLASS_TRUE_COLOR)
			wXFreeColors(color->screen->display, color->screen->colormap, &(color->color.pixel), 1, 0);
		wfree(color);
	}
}

void WMSetColorAlpha(WMColor * color, unsigned short alpha)
{
	color->alpha = alpha;
}

void WMPaintColorSwatch(WMColor * color, xcb_drawable_t d, int x, int y, unsigned int width, unsigned int height)
{
	cairo_t *cr;

	cr = WCairoBeginForDrawable(color->screen, d);
	if (!cr)
		return;
	WCairoSetSourceColor(cr, color);
	cairo_rectangle(cr, x, y, width, height);
	cairo_fill(cr);
	WCairoEnd(cr);
}

WMPixel WMColorPixel(WMColor * color)
{
	return color->color.pixel;
}

/* "system" colors */
WMColor *WMWhiteColor(WMScreen * scr)
{
	if (!scr->white) {
		scr->white = WMCreateRGBColor(scr, 0xffff, 0xffff, 0xffff, true);
		if (!scr->white->flags.exact)
			wwarning(_("could not allocate %s color"), _("white"));
	}
	return WMRetainColor(scr->white);
}

WMColor *WMBlackColor(WMScreen * scr)
{
	if (!scr->black) {
		scr->black = WMCreateRGBColor(scr, 0, 0, 0, true);
		if (!scr->black->flags.exact)
			wwarning(_("could not allocate %s color"), _("black"));
	}
	return WMRetainColor(scr->black);
}

WMColor *WMGrayColor(WMScreen * scr)
{
	if (!scr->gray) {
		scr->gray = WMCreateRGBColor(scr, 0xaeba, 0xaaaa, 0xaeba, true);
		if (!scr->gray->flags.exact)
			wwarning(_("could not allocate %s color"), _("gray"));
	}
	return WMRetainColor(scr->gray);
}

WMColor *WMDarkGrayColor(WMScreen * scr)
{
	if (!scr->darkGray) {
		scr->darkGray = WMCreateRGBColor(scr, 0x5144, 0x5555, 0x5144, true);
		if (!scr->darkGray->flags.exact)
			wwarning(_("could not allocate %s color"), _("dark gray"));
	}
	return WMRetainColor(scr->darkGray);
}

unsigned short WMRedComponentOfColor(WMColor * color)
{
	return color->color.red;
}

unsigned short WMGreenComponentOfColor(WMColor * color)
{
	return color->color.green;
}

unsigned short WMBlueComponentOfColor(WMColor * color)
{
	return color->color.blue;
}

unsigned short WMGetColorAlpha(WMColor * color)
{
	return color->alpha;
}

char *WMGetColorRGBDescription(WMColor * color)
{
	char *str = wmalloc(8);

	if (snprintf(str, 8, "#%02x%02x%02x",
		     color->color.red >> 8, color->color.green >> 8, color->color.blue >> 8) >= 8) {
		wfree(str);
		return NULL;
	}

	return str;
}
