/* wmsetbg.c- sets root window background image and also works as
 * 		workspace background setting helper for wmaker
 *
 *  WindowMaker window manager
 *
 *  Copyright (c) 1998-2003 Alfredo K. Kojima
 *  Copyright (c) 1998-2003 Dan Pascu
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

/*
 * TODO: rewrite, too dirty
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xlib-xcb.h>
#include <xcb/randr.h>
#include <cairo-xcb.h>
#include <WINGs/WXProp.h>
#include <string.h>
#include <strings.h>
#include <pwd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>

/* Xlibint.h must precede any other Xlib consumer and leaks lowercase
 * min/max macros, so undef them immediately after the protocol header. */
#include <X11/Xlibint.h>
#include <X11/extensions/panoramiXproto.h>
#undef min
#undef max

#ifdef HAVE_STDNORETURN
#include <stdnoreturn.h>
#endif

#include "../src/wconfig.h"


#include <WINGs/WINGs.h>
#include <wraster.h>

typedef struct {
	WMRect *screens;
	int count;		/* screen count, 0 = inactive */
} WXineramaInfo;

#define WORKSPACE_COUNT (MAX_WORKSPACES+1)

Display *dpy;
char *display = "";
Window root;
int scr;
int scrWidth;
int scrHeight;
int scrX, scrY;

WXineramaInfo xineInfo;

Bool smooth = False;
Bool xineStretch = False;

Pixmap CurrentPixmap = None;
char *PixmapPath = NULL;

static const char *prog_name;

typedef struct BackgroundTexture {
	int refcount;

	int solid;

	char *spec;

	XColor color;
	Pixmap pixmap;		/* for all textures, including solid */
	int width;		/* size of the pixmap */
	int height;
} BackgroundTexture;

static noreturn void quit(int rcode)
{
	WMReleaseApplication();
	exit(rcode);
}

/* Minor opcodes for the post-1.0 XINERAMA requests, these live in
 * libXinerama's private headers rather than panoramiXproto.h. */
#define X_XineramaIsActive      4
#define X_XineramaQueryScreens  5

#define WMSETBG_MAX_HEADS       32

static char head_names[WMSETBG_MAX_HEADS][256];

typedef struct {
	char *name;
	char *image;
	char *style;
	char *color;
	int smooth;
	Bool used;
} OutputSpec;

static OutputSpec defSpec;
static OutputSpec *namedSpecs;
static int nnamed;
static Bool havePerOutput = False;

/* Speak the XINERAMA wire protocol directly so we do not link libXinerama.
 * On hardware the X server synthesises this list from RandR, and on nested
 * servers such as Xephyr +xinerama it is the only multi-head view, so a
 * single query covers both without a separate RandR path. */
static void initXinerama(void)
{
	xXineramaIsActiveReq *areq;
	xXineramaIsActiveReply arep;
	xXineramaQueryScreensReq *sreq;
	xXineramaQueryScreensReply srep;
	int major_opcode, first_event, first_error;

	xineInfo.screens = NULL;
	xineInfo.count = 0;

	if (!XQueryExtension(dpy, PANORAMIX_PROTOCOL_NAME,
			     &major_opcode, &first_event, &first_error))
		return;

	LockDisplay(dpy);
	GetReq(XineramaIsActive, areq);
	areq->reqType = major_opcode;
	areq->panoramiXReqType = X_XineramaIsActive;
	if (!_XReply(dpy, (xReply *)&arep, 0, xTrue))
		arep.state = 0;
	UnlockDisplay(dpy);
	SyncHandle();

	if (!arep.state)
		return;

	LockDisplay(dpy);
	GetReq(XineramaQueryScreens, sreq);
	sreq->reqType = major_opcode;
	sreq->panoramiXReqType = X_XineramaQueryScreens;
	if (!_XReply(dpy, (xReply *)&srep, 0, xFalse)) {
		UnlockDisplay(dpy);
		SyncHandle();
		return;
	}
	if (srep.number > 0 && srep.number <= WMSETBG_MAX_HEADS
	    && srep.length == srep.number * (sz_XineramaScreenInfo / 4)) {
		CARD32 i;

		xineInfo.screens = wmalloc(sizeof(WMRect) * (srep.number + 1));
		for (i = 0; i < srep.number; i++) {
			xXineramaScreenInfo si;

			_XRead(dpy, (char *)&si, sz_XineramaScreenInfo);
			xineInfo.screens[i].pos.x = si.x_org;
			xineInfo.screens[i].pos.y = si.y_org;
			xineInfo.screens[i].size.width = si.width;
			xineInfo.screens[i].size.height = si.height;
		}
		xineInfo.count = (int)srep.number;
	} else {
		_XEatDataWords(dpy, srep.length);
	}
	UnlockDisplay(dpy);
	SyncHandle();
}

static Bool initRandR(void)
{
	xcb_connection_t *xc = XGetXCBConnection(dpy);
	xcb_randr_get_monitors_reply_t *mr;
	xcb_randr_monitor_info_iterator_t mi;
	xcb_get_atom_name_cookie_t name_ck[WMSETBG_MAX_HEADS];
	xcb_randr_query_version_reply_t *vr;
	WMRect local_screens[WMSETBG_MAX_HEADS];
	xcb_atom_t local_atoms[WMSETBG_MAX_HEADS];
	Bool have15;
	int count = 0;
	int i;

	memset(head_names, 0, sizeof(head_names));

	vr = xcb_randr_query_version_reply(xc,
			xcb_randr_query_version(xc, 1, 5), NULL);
	if (!vr)
		return False;
	have15 = (vr->major_version > 1 ||
		  (vr->major_version == 1 && vr->minor_version >= 5));
	free(vr);
	if (!have15)
		return False;

	mr = xcb_randr_get_monitors_reply(xc,
			xcb_randr_get_monitors(xc, root, 1), NULL);
	if (!mr)
		return False;

	for (mi = xcb_randr_get_monitors_monitors_iterator(mr);
	     mi.rem && count < WMSETBG_MAX_HEADS;
	     xcb_randr_monitor_info_next(&mi)) {
		if (mi.data->width <= 0 || mi.data->height <= 0)
			continue;
		local_screens[count].pos.x = mi.data->x;
		local_screens[count].pos.y = mi.data->y;
		local_screens[count].size.width = mi.data->width;
		local_screens[count].size.height = mi.data->height;
		local_atoms[count] = mi.data->name;
		count++;
	}
	free(mr);

	if (count == 0)
		return False;

	for (i = 0; i < count; i++)
		name_ck[i] = (local_atoms[i] != XCB_ATOM_NONE)
			? xcb_get_atom_name(xc, local_atoms[i])
			: (xcb_get_atom_name_cookie_t){0};
	xcb_flush(xc);
	for (i = 0; i < count; i++) {
		xcb_get_atom_name_reply_t *rep;

		if (name_ck[i].sequence) {
			rep = xcb_get_atom_name_reply(xc, name_ck[i], NULL);
			if (rep) {
				int nlen = xcb_get_atom_name_name_length(rep);

				if (nlen >= (int)sizeof(head_names[i]))
					nlen = sizeof(head_names[i]) - 1;
				memcpy(head_names[i], xcb_get_atom_name_name(rep), nlen);
				head_names[i][nlen] = '\0';
				free(rep);
			}
		}
	}

	xineInfo.screens = wmalloc(sizeof(WMRect) * (count + 1));
	memcpy(xineInfo.screens, local_screens, sizeof(WMRect) * count);
	xineInfo.count = count;

	return True;
}

static RImage *loadImage(RContext * rc, const char *file)
{
	char *path;
	RImage *image;

	if (access(file, F_OK) != 0) {
		path = wfindfile(PixmapPath, file);
		if (!path) {
			wwarning("%s:could not find image file used in texture", file);
			return NULL;
		}
	} else {
		path = wstrdup(file);
	}

	image = RLoadImage(rc, path, 0);
	if (!image) {
		wwarning("%s:could not load image file used in texture:%s", path, RMessageForError(RErrorCode));
	}
	wfree(path);

	return image;
}

static cairo_t *cairo_begin_pixmap(RContext *rc, Pixmap p, int w, int h)
{
	cairo_surface_t *s;
	cairo_t *cr;

	s = cairo_xcb_surface_create(rc->xc, (xcb_drawable_t)p, rc->xvisual, w, h);
	cr = cairo_create(s);
	cairo_surface_destroy(s);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	return cr;
}

static void fill_pixmap(RContext *rc, Pixmap p, int w, int h, const XColor *c)
{
	cairo_t *cr;

	cr = cairo_begin_pixmap(rc, p, w, h);
	cairo_set_source_rgb(cr, c->red / 65535.0, c->green / 65535.0,
			     c->blue / 65535.0);
	cairo_paint(cr);
	cairo_surface_flush(cairo_get_target(cr));
	cairo_destroy(cr);
}

static void fill_rect(RContext *rc, Pixmap p, int x, int y, int w, int h,
		      const XColor *c)
{
	cairo_t *cr;

	cr = cairo_begin_pixmap(rc, p, scrWidth, scrHeight);
	cairo_set_source_rgb(cr, c->red / 65535.0, c->green / 65535.0,
			     c->blue / 65535.0);
	cairo_rectangle(cr, x, y, w, h);
	cairo_fill(cr);
	cairo_surface_flush(cairo_get_target(cr));
	cairo_destroy(cr);
}

static void
applyImage(RContext * rc, BackgroundTexture * texture, RImage * image, char type,
	   int x, int y, int width, int height)
{
	int w, h;
	Bool fimage = False;
	RImage *timage;

	switch (toupper(type)) {
	case 'S':
	case 'M':
	case 'F':
		if (toupper(type) == 'S') {
			w = width;
			h = height;
		} else if(toupper(type) == 'F') {
			if (image->width * height > image->height * width) {
				w = (height * image->width) / image->height;
				h = height;
			} else {
				w = width;
				h = (width * image->height) / image->width;
			}
		} else {
			if (image->width * height > image->height * width) {
				w = width;
				h = (width * image->height) / image->width;
			} else {
				w = (height * image->width) / image->height;
				h = height;
			}
		}

		if (w != image->width || h != image->height) {
			RImage *simage;

			if (smooth) {
				simage = RSmoothScaleImage(image, w, h);
			} else {
				simage = RScaleImage(image, w, h);
			}

			if (!simage) {
				wwarning("could not scale image:%s", RMessageForError(RErrorCode));
				return;
			}
			fimage = True;
			image = simage;
		}

		__attribute__((fallthrough));
	case 'T':
		if (image->width != width || image->height != height) {
			timage = RMakeTiledImage(image, width, height);
			if (!timage) {
				wwarning("could not tile texture:%s", RMessageForError(RErrorCode));
				return;
			}
			fimage = True;
			image = timage;
		}
		__attribute__((fallthrough));
	case 'C':
		{
			Pixmap pixmap;
			cairo_surface_t *ss;
			cairo_t *cr;
			int sx = 0, sy = 0;

			if (!RConvertImage(rc, image, &pixmap)) {
				wwarning("could not convert texture:%s", RMessageForError(RErrorCode));
				if (fimage)
					RReleaseImage(image);
				return;
			}

			if (image->height < height) {
				h = image->height;
				y += (height - h) / 2;
			} else {
				sy = (image->height - height) / 2;
				h = height;
			}
			if (image->width < width) {
				w = image->width;
				x += (width - w) / 2;
			} else {
				sx = (image->width - width) / 2;
				w = width;
			}

			cr = cairo_begin_pixmap(rc, texture->pixmap,
						texture->width, texture->height);
			ss = cairo_xcb_surface_create(rc->xc, (xcb_drawable_t)pixmap,
						      rc->xvisual,
						      image->width, image->height);
			cairo_set_source_surface(cr, ss, x - sx, y - sy);
			cairo_rectangle(cr, x, y, w, h);
			cairo_fill(cr);
			cairo_surface_destroy(ss);
			cairo_surface_flush(cairo_get_target(cr));
			cairo_destroy(cr);

			XFreePixmap(dpy, pixmap);
			if (fimage) {
				RReleaseImage(image);
			}
		}
		break;
	}
}

static BackgroundTexture *parseTexture(RContext * rc, char *text)
{
	BackgroundTexture *texture = NULL;
	WMPropList *texarray;
	WMPropList *val;
	int count;
	char *tmp;
	char *type;

#define GETSTRORGOTO(val, str, i, label) \
    val = WMGetFromPLArray(texarray, i);\
    if (!WMIsPLString(val)) {\
    wwarning("could not parse texture %s", text);\
    goto label;\
    }\
    str = WMGetFromPLString(val)

	texarray = WMCreatePropListFromDescription(text);
	if (!texarray || !WMIsPLArray(texarray)
	    || (count = WMGetPropListItemCount(texarray)) < 2) {

		wwarning("could not parse texture %s", text);
		if (texarray)
			WMReleasePropList(texarray);
		return NULL;
	}

	texture = wmalloc(sizeof(BackgroundTexture));

	GETSTRORGOTO(val, type, 0, error);

	if (strcasecmp(type, "solid") == 0) {
		XColor color;
		Pixmap pixmap;

		texture->solid = 1;

		GETSTRORGOTO(val, tmp, 1, error);

		if (!XParseColor(dpy, DefaultColormap(dpy, scr), tmp, &color)) {
			wwarning("could not parse color %s in texture %s", tmp, text);
			goto error;
		}
		XAllocColor(dpy, DefaultColormap(dpy, scr), &color);

		pixmap = XCreatePixmap(dpy, root, 8, 8, DefaultDepth(dpy, scr));
		fill_pixmap(rc, pixmap, 8, 8, &color);

		texture->pixmap = pixmap;
		texture->color = color;
		texture->width = 8;
		texture->height = 8;
	} else if (strcasecmp(type, "vgradient") == 0
		   || strcasecmp(type, "dgradient") == 0 || strcasecmp(type, "hgradient") == 0) {
		XColor color;
		RColor color1, color2;
		RImage *image;
		Pixmap pixmap;
		RGradientStyle gtype;
		int iwidth, iheight;

		GETSTRORGOTO(val, tmp, 1, error);

		if (!XParseColor(dpy, DefaultColormap(dpy, scr), tmp, &color)) {
			wwarning("could not parse color %s in texture %s", tmp, text);
			goto error;
		}

		color1.red = color.red >> 8;
		color1.green = color.green >> 8;
		color1.blue = color.blue >> 8;

		GETSTRORGOTO(val, tmp, 2, error);

		if (!XParseColor(dpy, DefaultColormap(dpy, scr), tmp, &color)) {
			wwarning("could not parse color %s in texture %s", tmp, text);
			goto error;
		}

		color2.red = color.red >> 8;
		color2.green = color.green >> 8;
		color2.blue = color.blue >> 8;

		switch (type[0]) {
		case 'h':
		case 'H':
			gtype = RHorizontalGradient;
			iwidth = scrWidth;
			iheight = 32;
			break;
		case 'V':
		case 'v':
			gtype = RVerticalGradient;
			iwidth = 32;
			iheight = scrHeight;
			break;
		default:
			gtype = RDiagonalGradient;
			iwidth = scrWidth;
			iheight = scrHeight;
			break;
		}

		image = RRenderGradient(iwidth, iheight, &color1, &color2, gtype);

		if (!image) {
			wwarning("could not render gradient texture:%s", RMessageForError(RErrorCode));
			goto error;
		}

		if (!RConvertImage(rc, image, &pixmap)) {
			wwarning("could not convert texture:%s", RMessageForError(RErrorCode));
			RReleaseImage(image);
			goto error;
		}

		texture->width = image->width;
		texture->height = image->height;
		RReleaseImage(image);

		texture->pixmap = pixmap;
	} else if (strcasecmp(type, "mvgradient") == 0
		   || strcasecmp(type, "mdgradient") == 0 || strcasecmp(type, "mhgradient") == 0) {
		XColor color;
		RColor **colors;
		RImage *image;
		Pixmap pixmap;
		int i, j;
		RGradientStyle gtype;
		int iwidth, iheight;

		colors = wmalloc(sizeof(RColor *) * (count - 1));

		for (i = 2; i < count; i++) {
			val = WMGetFromPLArray(texarray, i);
			if (!WMIsPLString(val)) {
				wwarning("could not parse texture %s", text);

				for (j = 0; colors[j] != NULL; j++)
					wfree(colors[j]);
				wfree(colors);
				goto error;
			}
			tmp = WMGetFromPLString(val);

			if (!XParseColor(dpy, DefaultColormap(dpy, scr), tmp, &color)) {
				wwarning("could not parse color %s in texture %s", tmp, text);

				for (j = 0; colors[j] != NULL; j++)
					wfree(colors[j]);
				wfree(colors);
				goto error;
			}
			colors[i - 2] = wmalloc(sizeof(RColor));

			colors[i - 2]->red = color.red >> 8;
			colors[i - 2]->green = color.green >> 8;
			colors[i - 2]->blue = color.blue >> 8;
		}

		switch (type[1]) {
		case 'h':
		case 'H':
			gtype = RHorizontalGradient;
			iwidth = scrWidth;
			iheight = 32;
			break;
		case 'V':
		case 'v':
			gtype = RVerticalGradient;
			iwidth = 32;
			iheight = scrHeight;
			break;
		default:
			gtype = RDiagonalGradient;
			iwidth = scrWidth;
			iheight = scrHeight;
			break;
		}

		image = RRenderMultiGradient(iwidth, iheight, colors, gtype);

		for (j = 0; colors[j] != NULL; j++)
			wfree(colors[j]);
		wfree(colors);

		if (!image) {
			wwarning("could not render gradient texture:%s", RMessageForError(RErrorCode));
			goto error;
		}

		if (!RConvertImage(rc, image, &pixmap)) {
			wwarning("could not convert texture:%s", RMessageForError(RErrorCode));
			RReleaseImage(image);
			goto error;
		}

		texture->width = image->width;
		texture->height = image->height;
		RReleaseImage(image);

		texture->pixmap = pixmap;
	} else if (strcasecmp(type, "cpixmap") == 0
		   || strcasecmp(type, "spixmap") == 0 || strcasecmp(type, "fpixmap") == 0
		   || strcasecmp(type, "mpixmap") == 0 || strcasecmp(type, "tpixmap") == 0) {
		XColor color;
		Pixmap pixmap = None;
		RImage *image = NULL;
		int iwidth = 0, iheight = 0;
		RColor rcolor;

		GETSTRORGOTO(val, tmp, 1, error);
		/*
		   if (toupper(type[0]) == 'T' || toupper(type[0]) == 'C')
		   pixmap = LoadJPEG(rc, tmp, &iwidth, &iheight);
		 */

		if (!pixmap) {
			image = loadImage(rc, tmp);
			if (!image) {
				goto error;
			}
			iwidth = image->width;
			iheight = image->height;
		}

		/* cannot use GETSTRORGOTO() here
		 * as we have to free image in case of error */
		val = WMGetFromPLArray(texarray, 2);
		if (!WMIsPLString(val)) {
			wwarning("could not parse texture %s", text);
			RReleaseImage(image);
			goto error;
		}
		tmp = WMGetFromPLString(val);

		if (!XParseColor(dpy, DefaultColormap(dpy, scr), tmp, &color)) {
			wwarning("could not parse color %s in texture %s", tmp, text);
			RReleaseImage(image);
			goto error;
		}
		if (!XAllocColor(dpy, DefaultColormap(dpy, scr), &color)) {
			rcolor.red = color.red >> 8;
			rcolor.green = color.green >> 8;
			rcolor.blue = color.blue >> 8;
			RGetClosestXColor(rc, &rcolor, &color.pixel);
		} else {
			unsigned long pixel = color.pixel;

			rcolor.red = 0;
			rcolor.green = 0;
			rcolor.blue = 0;
			/* the allocated cell's pixel is never read back (the
			 * renderers use the server-adjusted RGB), so return the
			 * reservation immediately */
			XFreeColors(dpy, DefaultColormap(dpy, scr), &pixel, 1, 0);
		}
		/* for images with a transparent color */
		if (image && image->data[3])
			RCombineImageWithColor(image, &rcolor);

		switch (toupper(type[0])) {
		case 'T':
			texture->width = iwidth;
			texture->height = iheight;
			if (!pixmap && !RConvertImage(rc, image, &pixmap)) {
				wwarning("could not convert texture:%s", RMessageForError(RErrorCode));
				RReleaseImage(image);
				goto error;
			}

			texture->pixmap = pixmap;
			texture->color = color;
			break;
		case 'S':
		case 'M':
		case 'C':
		case 'F':
			{
				Pixmap tpixmap =
				    XCreatePixmap(dpy, root, scrWidth, scrHeight, DefaultDepth(dpy, scr));
				fill_pixmap(rc, tpixmap, scrWidth, scrHeight, &color);

				texture->pixmap = tpixmap;
				texture->color = color;
				texture->width = scrWidth;
				texture->height = scrHeight;

				if (!image)
					break;

				if (xineInfo.count && ! xineStretch) {
					int i;
					for (i = 0; i < xineInfo.count; ++i) {
						applyImage(rc, texture, image, type[0],
							   xineInfo.screens[i].pos.x, xineInfo.screens[i].pos.y,
							   xineInfo.screens[i].size.width,
							   xineInfo.screens[i].size.height);
					}
				} else {
					applyImage(rc, texture, image, type[0], 0, 0, scrWidth, scrHeight);
				}
			}
			break;
		}
		if (image)
			RReleaseImage(image);

	} else if (strcasecmp(type, "thgradient") == 0
		   || strcasecmp(type, "tvgradient") == 0 || strcasecmp(type, "tdgradient") == 0) {
		XColor color;
		RColor color1, color2;
		RImage *image;
		RImage *gradient;
		RImage *tiled;
		Pixmap pixmap;
		int opaq;
		char *file;
		RGradientStyle gtype;
		int twidth, theight;

		GETSTRORGOTO(val, file, 1, error);

		GETSTRORGOTO(val, tmp, 2, error);

		opaq = atoi(tmp);

		GETSTRORGOTO(val, tmp, 3, error);

		if (!XParseColor(dpy, DefaultColormap(dpy, scr), tmp, &color)) {
			wwarning("could not parse color %s in texture %s", tmp, text);
			goto error;
		}

		color1.red = color.red >> 8;
		color1.green = color.green >> 8;
		color1.blue = color.blue >> 8;

		GETSTRORGOTO(val, tmp, 4, error);

		if (!XParseColor(dpy, DefaultColormap(dpy, scr), tmp, &color)) {
			wwarning("could not parse color %s in texture %s", tmp, text);
			goto error;
		}

		color2.red = color.red >> 8;
		color2.green = color.green >> 8;
		color2.blue = color.blue >> 8;

		image = loadImage(rc, file);
		if (!image) {
			goto error;
		}

		switch (type[1]) {
		case 'h':
		case 'H':
			gtype = RHorizontalGradient;
			twidth = scrWidth;
			theight = image->height > scrHeight ? scrHeight : image->height;
			break;
		case 'V':
		case 'v':
			gtype = RVerticalGradient;
			twidth = image->width > scrWidth ? scrWidth : image->width;
			theight = scrHeight;
			break;
		default:
			gtype = RDiagonalGradient;
			twidth = scrWidth;
			theight = scrHeight;
			break;
		}
		gradient = RRenderGradient(twidth, theight, &color1, &color2, gtype);

		if (!gradient) {
			wwarning("could not render texture:%s", RMessageForError(RErrorCode));
			RReleaseImage(image);
			goto error;
		}

		tiled = RMakeTiledImage(image, twidth, theight);
		if (!tiled) {
			wwarning("could not render texture:%s", RMessageForError(RErrorCode));
			RReleaseImage(gradient);
			RReleaseImage(image);
			goto error;
		}
		RReleaseImage(image);

		RCombineImagesWithOpaqueness(tiled, gradient, opaq);
		RReleaseImage(gradient);

		if (!RConvertImage(rc, tiled, &pixmap)) {
			wwarning("could not convert texture:%s", RMessageForError(RErrorCode));
			RReleaseImage(tiled);
			goto error;
		}
		texture->width = tiled->width;
		texture->height = tiled->height;

		RReleaseImage(tiled);

		texture->pixmap = pixmap;
	} else if (strcasecmp(type, "function") == 0) {
		/* Leave this in to handle the unlikely case of
		 * someone actually having function textures configured */
		wwarning("function texture support has been removed");
		goto error;
	} else {
		wwarning("invalid texture type %s", text);
		goto error;
	}

	texture->spec = wstrdup(text);

	WMReleasePropList(texarray);

	return texture;

 error:
	if (texture)
		wfree(texture);
	if (texarray)
		WMReleasePropList(texarray);

	return NULL;
}

static void freeTexture(BackgroundTexture * texture)
{
	if (texture->solid) {
		unsigned long pixel[1];

		pixel[0] = texture->color.pixel;
		/* dont free black/white pixels */
		if (pixel[0] != BlackPixelOfScreen(DefaultScreenOfDisplay(dpy))
		    && pixel[0] != WhitePixelOfScreen(DefaultScreenOfDisplay(dpy)))
			XFreeColors(dpy, DefaultColormap(dpy, scr), pixel, 1, 0);
	}
	if (texture->pixmap) {
		XFreePixmap(dpy, texture->pixmap);
	}
	wfree(texture->spec);
	wfree(texture);
}

static Bool parseBackColor(RContext *rc, const char *str, XColor *xc, RColor *rcolor)
{
	if (!XParseColor(dpy, DefaultColormap(dpy, scr), str, xc)) {
		wwarning("could not parse color %s", str);
		xc->red = xc->green = xc->blue = 0;
		xc->pixel = 0;
		rcolor->red = rcolor->green = rcolor->blue = 0;
		return False;
	}
	if (!XAllocColor(dpy, DefaultColormap(dpy, scr), xc)) {
		rcolor->red = xc->red >> 8;
		rcolor->green = xc->green >> 8;
		rcolor->blue = xc->blue >> 8;
		RGetClosestXColor(rc, rcolor, &xc->pixel);
	} else {
		unsigned long pixel = xc->pixel;

		rcolor->red = 0;
		rcolor->green = 0;
		rcolor->blue = 0;
		XFreeColors(dpy, DefaultColormap(dpy, scr), &pixel, 1, 0);
	}
	return True;
}

static OutputSpec *findSpec(const char *name)
{
	int i;

	if (name && name[0]) {
		for (i = 0; i < nnamed; i++)
			if (namedSpecs[i].name && strcmp(namedSpecs[i].name, name) == 0) {
				namedSpecs[i].used = True;
				return &namedSpecs[i];
			}
	}
	return &defSpec;
}

static void renderOutputHead(RContext *rc, BackgroundTexture *texture,
			     WMRect *rect, const char *name)
{
	OutputSpec *spec = findSpec(name);
	const char *style = spec->style ? spec->style :
		(defSpec.style ? defSpec.style : "spixmap");
	const char *color_str = spec->color ? spec->color :
		(defSpec.color ? defSpec.color : "gray20");
	const char *image_name = spec->image ? spec->image : defSpec.image;
	XColor xc;
	RColor rcolor;
	Bool old_smooth;
	RImage *image = NULL;

	parseBackColor(rc, color_str, &xc, &rcolor);

	fill_rect(rc, texture->pixmap, rect->pos.x, rect->pos.y,
		  rect->size.width, rect->size.height, &xc);

	if (!image_name)
		return;

	image = loadImage(rc, image_name);
	if (!image)
		return;

	if (image->data[3])
		RCombineImageWithColor(image, &rcolor);

	old_smooth = smooth;
	if (spec->smooth >= 0)
		smooth = (Bool)spec->smooth;

	if (strcmp(style, "solid") != 0)
		applyImage(rc, texture, image, style[0],
			   rect->pos.x, rect->pos.y,
			   rect->size.width, rect->size.height);

	smooth = old_smooth;
	RReleaseImage(image);
}

static BackgroundTexture *renderPerOutput(RContext *rc)
{
	BackgroundTexture *texture;
	XColor xc;
	RColor rcolor;
	int i;

	texture = wmalloc(sizeof(BackgroundTexture));
	texture->pixmap = XCreatePixmap(dpy, root, scrWidth, scrHeight,
					DefaultDepth(dpy, scr));
	texture->width = scrWidth;
	texture->height = scrHeight;
	texture->solid = 0;
	texture->spec = wstrdup("per-output");

	/* cover the pixels that no monitor owns */
	parseBackColor(rc, defSpec.color ? defSpec.color : "gray20", &xc, &rcolor);
	fill_pixmap(rc, texture->pixmap, scrWidth, scrHeight, &xc);

	if (xineInfo.count && !xineStretch) {
		for (i = 0; i < xineInfo.count; i++)
			renderOutputHead(rc, texture, &xineInfo.screens[i],
					 head_names[i][0] ? head_names[i] : NULL);
	} else {
		WMRect whole;

		whole.pos.x = 0;
		whole.pos.y = 0;
		whole.size.width = scrWidth;
		whole.size.height = scrHeight;
		renderOutputHead(rc, texture, &whole, NULL);
	}

	for (i = 0; i < nnamed; i++)
		if (!namedSpecs[i].used)
			wwarning("--output %s: no matching monitor", namedSpecs[i].name);

	return texture;
}

static void setupTexture(RContext * rc, BackgroundTexture ** textures, int *maxTextures, int workspace, char *texture)
{
	BackgroundTexture *newTexture = NULL;
	int i;

	/* unset the texture */
	if (!texture) {
		if (textures[workspace] != NULL) {
			textures[workspace]->refcount--;

			if (textures[workspace]->refcount == 0)
				freeTexture(textures[workspace]);
		}
		textures[workspace] = NULL;
		return;
	}

	if (textures[workspace]
	    && strcasecmp(textures[workspace]->spec, texture) == 0) {
		/* texture did not change */
		return;
	}

	/* check if the same texture is already created */
	for (i = 0; i < *maxTextures; i++) {
		if (textures[i] && strcasecmp(textures[i]->spec, texture) == 0) {
			newTexture = textures[i];
			break;
		}
	}

	if (!newTexture) {
		/* create the texture */
		newTexture = parseTexture(rc, texture);
	}
	if (!newTexture)
		return;

	if (textures[workspace] != NULL) {

		textures[workspace]->refcount--;

		if (textures[workspace]->refcount == 0)
			freeTexture(textures[workspace]);
	}

	newTexture->refcount++;
	textures[workspace] = newTexture;

	if (*maxTextures < workspace)
		*maxTextures = workspace;
}

static Pixmap duplicatePixmap(Pixmap pixmap, int width, int height)
{
	Display *tmpDpy;
	Pixmap copyP;

	/* must open a new display or the RetainPermanent will
	 * leave stuff allocated in RContext unallocated after exit */
	tmpDpy = XOpenDisplay(display);
	if (!tmpDpy) {
		wwarning("could not open display to update background image information");

		return None;
	} else {
		xcb_connection_t *txc = XGetXCBConnection(tmpDpy);
		xcb_gcontext_t tgc = xcb_generate_id(txc);

		XSync(dpy, False);

		copyP = XCreatePixmap(tmpDpy, root, width, height, DefaultDepth(tmpDpy, scr));
		xcb_create_gc(txc, tgc, copyP, 0, NULL);
		xcb_copy_area(txc, pixmap, copyP, tgc, 0, 0, 0, 0, width, height);
		xcb_free_gc(txc, tgc);
		XSync(tmpDpy, False);

		XSetCloseDownMode(tmpDpy, RetainPermanent);
		XCloseDisplay(tmpDpy);
	}

	return copyP;
}

static int dummyErrorHandler(Display * dpy, XErrorEvent * err)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) dpy;
	(void) err;

	return 0;
}

static void setPixmapProperty(Pixmap pixmap)
{
	static Atom prop = 0;
	static Atom eprop = 0;
	uint32_t old_root, old_eset;
	Bool have_root, have_eset;

	if (!prop) {
		prop = XInternAtom(dpy, "_XROOTPMAP_ID", False);
		eprop = XInternAtom(dpy, "ESETROOT_PMAP_ID", False);
	}

	XGrabServer(dpy);

	/* Free the previous background pixmap only when both _XROOTPMAP_ID
	 * and ESETROOT_PMAP_ID hold the same XID, that pairing marks a
	 * RetainPermanent zombie left by a compliant root setter so
	 * XKillClient on it cannot hit a live connection.  A bare
	 * _XROOTPMAP_ID may come from a tool that closed in Destroy mode,
	 * in which case the client bits of the stored XID can already be
	 * recycled to a live client, including our own dpy, and killing
	 * it produces "X connection broken". */
	have_root = WXGetFormat32N(dpy, root, prop, XA_PIXMAP, &old_root, 1);
	have_eset = WXGetFormat32N(dpy, root, eprop, XA_PIXMAP, &old_eset, 1);
	if (have_root && have_eset && old_root == old_eset && old_eset != None) {
		XSetErrorHandler(dummyErrorHandler);
		XKillClient(dpy, (XID)old_eset);
		XSync(dpy, False);
		XSetErrorHandler(NULL);
	}
	if (pixmap) {
		uint32_t v = (uint32_t)pixmap;
		WXSetFormat32N(dpy, root, prop, XA_PIXMAP, &v, 1);
		WXSetFormat32N(dpy, root, eprop, XA_PIXMAP, &v, 1);
	} else {
		WXDeleteProperty(dpy, root, prop);
		WXDeleteProperty(dpy, root, eprop);
	}

	XUngrabServer(dpy);
	XFlush(dpy);
}

static void changeTexture(BackgroundTexture * texture)
{
	Pixmap pixmap;

	if (!texture) {
		return;
	}

	if (texture->solid) {
		XSetWindowBackground(dpy, root, texture->color.pixel);
	} else {
		XSetWindowBackgroundPixmap(dpy, root, texture->pixmap);
	}
	XClearWindow(dpy, root);

	XSync(dpy, False);

	pixmap = duplicatePixmap(texture->pixmap, texture->width, texture->height);
	setPixmapProperty(pixmap);
}

static int readmsg(int fd, char *buffer, int size)
{
	int count;

	while (size > 0) {
		count = read(fd, buffer, size);
		if (count < 0)
			return -1;
		if (count == 0)
			return -1;
		size -= count;
		buffer += count;
		if (size > 0)
			*buffer = 0;
	}

	return size;
}

/*
 * Message Format:
 * sizeSntexture_spec - sets the texture for workspace n
 * sizeCn - change background texture to the one for workspace n
 * sizePpath - set the pixmap search path
 *
 * n is 4 bytes
 * size = 4 bytes for length of the message data
 */
static noreturn void helperLoop(RContext * rc)
{
	BackgroundTexture *textures[WORKSPACE_COUNT];
	int maxTextures = 0;
	char buffer[2048], buf[8];
	int size;
	int errcount = 4;

	memset(textures, 0, WORKSPACE_COUNT * sizeof(BackgroundTexture *));
	memset(buffer, 0, sizeof(buffer));

	while (1) {
		int workspace = -1;

		/* get length of message */
		if (readmsg(0, buffer, 4) < 0) {
			werror("error reading message from Window Maker");
			errcount--;
			if (errcount == 0) {
				wfatal("quitting");
				quit(1);
			}
			continue;
		}
		memcpy(buf, buffer, 4);
		buf[4] = 0;
		size = atoi(buf);
		if (size < 0 || size >= sizeof(buffer)) {
			wfatal("received invalid size %d for message from WindowMaker", size);
			quit(1);
		}
		if (size == 0) {
			werror("received 0-sized message from WindowMaker, trying to continue");
			continue;
		}

		/* get message */
		if (readmsg(0, buffer, size) < 0) {
			werror("error reading message from Window Maker");
			errcount--;
			if (errcount == 0) {
				wfatal("quitting");
				quit(1);
			}
			continue;
		}
		buffer[size] = 0;
#ifdef DEBUG
		printf("RECEIVED %s\n", buffer);
#endif
		if (buffer[0] != 'P' && buffer[0] != 'K') {
			memcpy(buf, &buffer[1], 4);
			buf[4] = 0;
			workspace = atoi(buf);
			if (workspace < 0 || workspace >= WORKSPACE_COUNT) {
				wwarning("received message with invalid workspace number %i", workspace);
				continue;
			}
		}

		switch (buffer[0]) {
		case 'S':
#ifdef DEBUG
			printf("set texture %s\n", &buffer[5]);
#endif
			setupTexture(rc, textures, &maxTextures, workspace, &buffer[5]);
			break;

		case 'C':
#ifdef DEBUG
			printf("change texture %i\n", workspace);
#endif
			if (!textures[workspace]) {
				changeTexture(textures[0]);
			} else {
				changeTexture(textures[workspace]);
			}
			break;

		case 'P':
#ifdef DEBUG
			printf("change pixmappath %s\n", &buffer[1]);
#endif
			if (PixmapPath)
				wfree(PixmapPath);
			PixmapPath = wstrdup(&buffer[1]);
			break;

		case 'U':
#ifdef DEBUG
			printf("unset workspace %i\n", workspace);
#endif
			setupTexture(rc, textures, &maxTextures, workspace, NULL);
			break;

		case 'K':
#ifdef DEBUG
			printf("exit command\n");
#endif
			quit(0);

		default:
			wwarning("unknown message received");
			break;
		}
	}
}

static int updateDomain(const char *domain, const char *key, const char *texture)
{
	char *program = "wdwrite";
	int status;

	/* domain name validation now lives in wdefaultspathfordomain(),
	 * wdwrite will refuse a bad name with a clear message */
	(void)wspawnvp_wait((char *[]){ program, (char *)domain, "SmoothWorkspaceBack",
					smooth ? "YES" : "NO", NULL });

	status = wspawnvp_wait((char *[]){ program, (char *)domain, (char *)key,
					  (char *)texture, NULL });
	if (status < 0)
		wwarning("warning could not run \"%s\"", program);

	return status;
}

static WMPropList *getValueForKey(const char *domain, const char *keyName)
{
	char *path;
	WMPropList *key, *val, *d;

	key = WMCreatePLString(keyName);

	/* try to find PixmapPath in user defaults */
	path = wdefaultspathfordomain(domain);
	if (!path) {
		WMReleasePropList(key);
		return NULL;
	}
	d = WMReadPropListFromFile(path);
	if (!d) {
		wwarning("could not open domain file %s", path);
	}
	wfree(path);

	if (d && !WMIsPLDictionary(d)) {
		WMReleasePropList(d);
		d = NULL;
	}
	if (d) {
		val = WMGetFromPLDictionary(d, key);
	} else {
		val = NULL;
	}
	/* try to find PixmapPath in global defaults */
	if (!val) {
		if (d)
			WMReleasePropList(d);
		path = wglobaldefaultspathfordomain(domain);
		if (!path) {
			wwarning("could not locate file for domain %s", domain);
			d = NULL;
		} else {
			d = WMReadPropListFromFile(path);
			wfree(path);
		}

		if (d && !WMIsPLDictionary(d)) {
			WMReleasePropList(d);
			d = NULL;
		}
		if (d) {
			val = WMGetFromPLDictionary(d, key);

		} else {
			val = NULL;
		}
	}

	if (val)
		WMRetainPropList(val);

	WMReleasePropList(key);
	if (d)
		WMReleasePropList(d);

	return val;
}

static char *getPixmapPath(const char *domain)
{
	WMPropList *val;
	char *ptr, *data;
	size_t len;
	int i, count;

	val = getValueForKey(domain, "PixmapPath");

	if (!val || !WMIsPLArray(val)) {
		if (val)
			WMReleasePropList(val);
		return wstrdup("");
	}

	count = WMGetPropListItemCount(val);
	len = 0;
	for (i = 0; i < count; i++) {
		WMPropList *v;

		v = WMGetFromPLArray(val, i);
		if (!v || !WMIsPLString(v)) {
			continue;
		}
		len += strlen(WMGetFromPLString(v)) + 1;
	}

	ptr = data = wmalloc(len + 1);
	*ptr = 0;

	for (i = 0; i < count; i++) {
		WMPropList *v;
		const char *item;
		size_t item_len;

		v = WMGetFromPLArray(val, i);
		if (!v || !WMIsPLString(v)) {
			continue;
		}
		item = WMGetFromPLString(v);
		item_len = strlen(item);
		memcpy(ptr, item, item_len);
		ptr += item_len;
		*ptr++ = ':';
	}
	if (ptr > data)
		ptr--;
	*ptr = '\0';

	WMReleasePropList(val);

	return data;
}

static char *getFullPixmapPath(const char *file)
{
	char *tmp;

	if (!PixmapPath || !(tmp = wfindfile(PixmapPath, file))) {
		int bsize = 512;
		char *path = wmalloc(bsize);

		while (!getcwd(path, bsize)) {
			if (errno != ERANGE) {
				wfree(path);
				return NULL;
			}
			bsize += bsize / 2;
			path = wrealloc(path, bsize);
		}

		tmp = wstrconcat(path, "/");
		wfree(path);
		path = wstrconcat(tmp, file);
		wfree(tmp);

		return path;
	}

	/* the file is in the PixmapPath */
	wfree(tmp);

	return wstrdup(file);
}

static void print_help(void)
{
	printf("Usage: %s [options] [image]\n", prog_name);
	puts("Sets the workspace background to the specified image or a texture and");
	puts("optionally update Window Maker configuration");
	puts("");
	puts(" -display <display>            display to use");
	puts(" -S, --smooth                  smooth scaled image");
	puts(" -X, --stretch-all             stretch image across all heads");
	puts(" -o, --output <name>           assign following image/mode/color to a specific output");
	puts(" -c, -b, --color <color>       background color");
	puts(" -t, --tile                    tile image");
	puts(" -e, --center                  center image");
	puts(" -s, --scale                   scale image (default)");
	puts(" -a, --maxscale                scale image and keep aspect ratio");
	puts(" -f, --fillscale               scale image to fill screen and keep aspect ratio");
	puts(" -m, --mode <mode>             tile|center|scale|fit|fill|solid_color");
	puts(" -u, --update-wmaker           update WindowMaker domain database");
	puts(" -D, --update-domain <domain>  update <domain> database");
	puts(" -p, --parse <texture>         proplist style texture specification");
	puts(" -w, --workspace <workspace>   update background for the specified workspace");
	puts(" -v, --version                 show version of wmsetbg and exit");
	puts(" -h, --help                    show this help and exit");
}

static int changeTextureForWorkspace(const char *domain, char *texture, int workspace)
{
	WMPropList *array, *val;
	char *value;
	int j, status;

	val = WMCreatePropListFromDescription(texture);
	if (!val) {
		wwarning("could not parse texture %s", texture);
		return -1;
	}

	array = getValueForKey("WindowMaker", "WorkspaceSpecificBack");

	if (!array) {
		array = WMCreatePLArray(NULL, NULL);
	}

	j = WMGetPropListItemCount(array);
	if (workspace >= j) {
		WMPropList *empty;

		empty = WMCreatePLArray(NULL, NULL);

		while (j++ < workspace - 1) {
			WMAddToPLArray(array, empty);
		}
		WMAddToPLArray(array, val);

		WMReleasePropList(empty);
	} else {
		int idx = workspace > 0 ? workspace - 1 : 0;

		WMDeleteFromPLArray(array, idx);
		WMInsertInPLArray(array, idx, val);
	}

	value = WMGetPropListDescription(array, False);
	status = updateDomain(domain, "WorkspaceSpecificBack", value);

	wfree(value);
	WMReleasePropList(val);
	WMReleasePropList(array);
	return status;
}

static const char *mode_to_style(const char *mode)
{
	if (strcmp(mode, "tile") == 0)
		return "tpixmap";
	if (strcmp(mode, "center") == 0)
		return "cpixmap";
	if (strcmp(mode, "scale") == 0)
		return "spixmap";
	if (strcmp(mode, "fit") == 0)
		return "mpixmap";
	if (strcmp(mode, "fill") == 0)
		return "fpixmap";
	if (strcmp(mode, "solid_color") == 0)
		return "solid";
	return NULL;
}

int main(int argc, char **argv)
{
	int i;
	int helperMode = 0;
	RContext *rc;
	RContextAttributes rattr;
	char *style;
	char *back_color;
	char *image_name;
	char *domain = "WindowMaker";
	int update = 0;
	char *texture = NULL;
	int workspace = -1;
	OutputSpec *curSpec;
	int j;
	Bool texture_from_p;
	Bool any_spec;

	defSpec.name = NULL;
	defSpec.image = NULL;
	defSpec.style = NULL;
	defSpec.color = NULL;
	defSpec.smooth = -1;
	curSpec = &defSpec;

	signal(SIGINT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
	signal(SIGSEGV, SIG_DFL);
	signal(SIGBUS, SIG_DFL);
	signal(SIGFPE, SIG_DFL);
	signal(SIGABRT, SIG_DFL);
	signal(SIGHUP, SIG_DFL);
	signal(SIGPIPE, SIG_DFL);
	signal(SIGCHLD, SIG_DFL);

	WMInitializeApplication("wmsetbg", &argc, argv);

	prog_name = argv[0];
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-helper") == 0) {
			helperMode = 1;
		} else if (strcmp(argv[i], "-display") == 0) {
			i++;
			if (i >= argc) {
				wfatal("too few arguments for %s", argv[i - 1]);
				quit(1);
			}
			display = argv[i];
		} else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--mode") == 0) {
			const char *s;

			i++;
			if (i >= argc) {
				wfatal("too few arguments for %s", argv[i - 1]);
				quit(1);
			}
			s = mode_to_style(argv[i]);
			if (!s) {
				wfatal("unknown mode '%s'", argv[i]);
				quit(1);
			}
			curSpec->style = (char *)s;
		} else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--scale") == 0) {
			curSpec->style = "spixmap";
		} else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--tile") == 0) {
			curSpec->style = "tpixmap";
		} else if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--center") == 0) {
			curSpec->style = "cpixmap";
		} else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--maxscale") == 0) {
			curSpec->style = "mpixmap";
		} else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--fillscale") == 0) {
			curSpec->style = "fpixmap";
		} else if (strcmp(argv[i], "-S") == 0 || strcmp(argv[i], "--smooth") == 0) {
			curSpec->smooth = 1;
		} else if (strcmp(argv[i], "--stretch-all") == 0 || strcmp(argv[i], "--span") == 0
			   || strcmp(argv[i], "-X") == 0 || strcmp(argv[i], "--xinerama") == 0) {
			xineStretch = True;
		} else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
			i++;
			if (i >= argc) {
				wfatal("too few arguments for %s", argv[i - 1]);
				quit(1);
			}
			/* reuse an existing named spec or push a new one */
			for (j = 0; j < nnamed; j++) {
				if (namedSpecs[j].name && strcmp(namedSpecs[j].name, argv[i]) == 0)
					break;
			}
			if (j == nnamed) {
				namedSpecs = wrealloc(namedSpecs, sizeof(OutputSpec) * (nnamed + 1));
				memset(&namedSpecs[nnamed], 0, sizeof(OutputSpec));
				namedSpecs[nnamed].name = argv[i];
				namedSpecs[nnamed].smooth = -1;
				nnamed++;
			}
			curSpec = &namedSpecs[j];
			havePerOutput = True;
		} else if (strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--update-wmaker") == 0) {
			update++;
		} else if (strcmp(argv[i], "-D") == 0 || strcmp(argv[i], "--update-domain") == 0) {
			update++;
			i++;
			if (i >= argc) {
				wfatal("too few arguments for %s", argv[i - 1]);
				quit(1);
			}
			domain = wstrdup(argv[i]);
		} else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "-b") == 0
			   || strcmp(argv[i], "--color") == 0
			   || strcmp(argv[i], "--back-color") == 0) {
			i++;
			if (i >= argc) {
				wfatal("too few arguments for %s", argv[i - 1]);
				quit(1);
			}
			curSpec->color = argv[i];
		} else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--parse") == 0) {
			i++;
			if (i >= argc) {
				wfatal("too few arguments for %s", argv[i - 1]);
				quit(1);
			}
			texture = argv[i];
		} else if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--workspace") == 0) {
			i++;
			if (i >= argc) {
				wfatal("too few arguments for %s", argv[i - 1]);
				quit(1);
			}
			if (sscanf(argv[i], "%i", &workspace) != 1) {
				wfatal("bad value for workspace number: \"%s\"", argv[i]);
				quit(1);
			}
		} else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
			printf("%s (Window Maker %s)\n", prog_name, VERSION);
			quit(0);
		} else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			print_help();
			quit(0);
		} else if (argv[i][0] != '-') {
			curSpec->image = argv[i];
		} else {
			printf("%s: invalid argument '%s'\n", prog_name, argv[i]);
			printf("Try '%s --help' for more information\n", prog_name);
			quit(1);
		}
	}

	style = defSpec.style ? defSpec.style : "spixmap";
	back_color = defSpec.color ? defSpec.color : "gray20";
	image_name = defSpec.image;
	smooth = (defSpec.smooth >= 0) ? (Bool)defSpec.smooth : False;
	texture_from_p = (texture != NULL);

	if (havePerOutput && texture_from_p) {
		wfatal("--output and --parse are mutually exclusive");
		quit(1);
	}
	if (havePerOutput && update) {
		wfatal("--output cannot be combined with --update-wmaker or --update-domain");
		quit(1);
	}
	if (havePerOutput && workspace >= 0) {
		wfatal("--output and --workspace are mutually exclusive");
		quit(1);
	}

	any_spec = (image_name != NULL);

	if (!any_spec && style && strcmp(style, "solid") == 0)
		any_spec = True;
	for (j = 0; j < nnamed && !any_spec; j++) {
		any_spec = (namedSpecs[j].image != NULL);
		if (!any_spec && namedSpecs[j].style &&
		    strcmp(namedSpecs[j].style, "solid") == 0)
			any_spec = True;
	}
	if (!any_spec && !texture && !helperMode) {
		printf("%s: you must specify a image file name or a texture\n", prog_name);
		printf("Try '%s --help' for more information\n", prog_name);
		quit(1);
	}

	PixmapPath = getPixmapPath(domain);
	if (!smooth) {
		WMPropList *val;

		val = getValueForKey(domain, "SmoothWorkspaceBack");

		if (val && WMIsPLString(val) && strcasecmp(WMGetFromPLString(val), "YES") == 0)
			smooth = True;
		if (val)
			WMReleasePropList(val);
	}

	dpy = XOpenDisplay(display);
	if (!dpy) {
		wfatal("could not open display");
		quit(1);
	}
#if 0
	XSynchronize(dpy, 1);
#endif

	root = DefaultRootWindow(dpy);

	scr = DefaultScreen(dpy);

	scrWidth = WidthOfScreen(DefaultScreenOfDisplay(dpy));
	scrHeight = HeightOfScreen(DefaultScreenOfDisplay(dpy));
	scrX = scrY = 0;

	if (!initRandR())
		initXinerama();

	rattr.flags = RC_DefaultVisual;
	rc = RCreateContextXCB(XGetXCBConnection(dpy), scr, &rattr);
	if (!rc) {
		wfatal("could not initialize wrlib: %s", RMessageForError(RErrorCode));
		quit(1);
	}

	if (helperMode) {
		int result;

		/* lower priority, so that it won't use all the CPU */
		result = nice(15);
		if (result == -1)
			wwarning("error could not nice process");

		helperLoop(rc);
	} else {
		BackgroundTexture *tex;
		char *gen_texture = NULL;
		int update_status = -1;

		if (!texture) {
			if (strcmp(style, "solid") == 0) {
				WMPropList *pl = WMCreatePLArray(WMCreatePLString("solid"),
								 WMCreatePLString(back_color),
								 NULL);
				gen_texture = WMGetPropListDescription(pl, False);
				WMReleasePropList(pl);
				texture = gen_texture;
			} else if (image_name) {
				char *image_path = getFullPixmapPath(image_name);
				/* build the (style, path, color) array via the proplist
				 * API so quotes and backslashes in the path are escaped
				 * in the serialized description, instead of breaking
				 * out of a hand-built "..." */
				WMPropList *pl = WMCreatePLArray(WMCreatePLString(style),
								 WMCreatePLString(image_path),
								 WMCreatePLString(back_color),
								 NULL);
				gen_texture = WMGetPropListDescription(pl, False);
				WMReleasePropList(pl);
				wfree(image_path);
				texture = gen_texture;
			}
		}

		if (update && workspace < 0 && texture) {
			update_status = updateDomain(domain, "WorkspaceBack", texture);
			if (update_status >= 0) {
				if (gen_texture)
					wfree(gen_texture);
				if (namedSpecs)
					wfree(namedSpecs);
				RDestroyContext(rc);
				WMReleaseApplication();
				return update_status;
			}
		}

		if (havePerOutput && !texture_from_p)
			tex = renderPerOutput(rc);
		else {
			if (!texture)
				quit(1);
			tex = parseTexture(rc, texture);
		}
		if (!tex)
			quit(1);

		if (workspace < 0)
			changeTexture(tex);
		else if (texture) {
			/* always update domain */
			update_status = changeTextureForWorkspace(domain, texture, workspace);
		}

		freeTexture(tex);
		if (gen_texture)
			wfree(gen_texture);
		if (namedSpecs) {
			wfree(namedSpecs);
			namedSpecs = NULL;
		}
		if (update_status >= 0) {
			RDestroyContext(rc);
			WMReleaseApplication();
			return update_status;
		}
	}

	if (namedSpecs)
		wfree(namedSpecs);
	RDestroyContext(rc);
	WMReleaseApplication();
	return 0;
}
