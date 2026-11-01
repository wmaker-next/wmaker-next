/* wmsetbg.c- sets root window background image and also works as
 * 		workspace background setting helper for wmaker
 *
 *  WindowMaker window manager
 *
 *  Copyright (c) 1998-2003 Alfredo K. Kojima
 *  Copyright (c) 1998-2003 Dan Pascu
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

/*
 * TODO: rewrite, too dirty
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <xcb/xcb.h>
#include <xcb/xinerama.h>
#include <xcb/randr.h>
#include <cairo-xcb.h>
#include <WINGs/WXProp.h>
#include <WINGs/WXCore.h>
#include <string.h>
#include <strings.h>
#include <pwd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>

#ifdef HAVE_STDNORETURN
#include <stdnoreturn.h>
#endif

#include "../src/wconfig.h"

#include "xcconn.h"

#include <WINGs/WINGs.h>
#include <wraster.h>

typedef struct {
	WMRect *screens;
	int count;		/* screen count, 0 = inactive */
} WXineramaInfo;

#define WORKSPACE_COUNT (MAX_WORKSPACES+1)

/* the xcb connection for the libWINGs/libwraster calls (built in main
 * from xcb_connect) */
static xcb_connection_t *xc;
char *display = "";
xcb_window_t root;
int scr;
int scrWidth;
int scrHeight;
int scrX, scrY;

WXineramaInfo xineInfo;

bool smooth = false;
bool xineStretch = false;

char *PixmapPath = NULL;

static const char *prog_name;

typedef struct BackgroundTexture {
	int refcount;

	int solid;

	char *spec;

	WXColor color;
	xcb_pixmap_t pixmap;	/* for all textures, including solid */
	int width;		/* size of the pixmap */
	int height;
} BackgroundTexture;

static noreturn void quit(int rcode)
{
	WMReleaseApplication();
	exit(rcode);
}

#define WMSETBG_MAX_HEADS       32

static char head_names[WMSETBG_MAX_HEADS][256];

typedef struct {
	char *name;
	char *image;
	char *style;
	char *color;
	int smooth;
	bool used;
} OutputSpec;

static OutputSpec defSpec;
static OutputSpec *namedSpecs;
static int nnamed;
static bool havePerOutput = false;

/* On hardware the X server synthesises this list from RandR, and on
 * nested servers such as Xephyr +xinerama it is the only multi-head
 * view, so a single query covers both without a separate RandR path.
 * The generated xcb-xinerama module is part of libxcb itself, so no
 * libXinerama is linked. */
static void initXinerama(void)
{
	xcb_query_extension_cookie_t qck;
	xcb_query_extension_reply_t *qrep;
	xcb_xinerama_is_active_cookie_t ick;
	xcb_xinerama_is_active_reply_t *irep;
	xcb_xinerama_query_screens_cookie_t sck;
	xcb_xinerama_query_screens_reply_t *srep;
	xcb_xinerama_screen_info_t *info;
	int n, i;

	xineInfo.screens = NULL;
	xineInfo.count = 0;

	qck = xcb_query_extension(xc, 8, "XINERAMA");
	qrep = xcb_query_extension_reply(xc, qck, NULL);
	if (!qrep || !qrep->present) {
		free(qrep);
		return;
	}
	free(qrep);

	ick = xcb_xinerama_is_active(xc);
	irep = xcb_xinerama_is_active_reply(xc, ick, NULL);
	if (!irep || !irep->state) {
		free(irep);
		return;
	}
	free(irep);

	sck = xcb_xinerama_query_screens(xc);
	srep = xcb_xinerama_query_screens_reply(xc, sck, NULL);
	if (!srep)
		return;

	n = xcb_xinerama_query_screens_screen_info_length(srep);
	if (n > 0 && n <= WMSETBG_MAX_HEADS) {
		info = xcb_xinerama_query_screens_screen_info(srep);
		xineInfo.screens = wmalloc(sizeof(WMRect) * (n + 1));
		for (i = 0; i < n; i++) {
			xineInfo.screens[i].pos.x = info[i].x_org;
			xineInfo.screens[i].pos.y = info[i].y_org;
			xineInfo.screens[i].size.width = info[i].width;
			xineInfo.screens[i].size.height = info[i].height;
		}
		xineInfo.count = n;
	}
	free(srep);
}

static bool initRandR(void)
{
	xcb_randr_get_monitors_reply_t *mr;
	xcb_randr_monitor_info_iterator_t mi;
	const xcb_query_extension_reply_t *rer;
	xcb_get_atom_name_cookie_t name_ck[WMSETBG_MAX_HEADS];
	xcb_randr_query_version_reply_t *vr;
	WMRect local_screens[WMSETBG_MAX_HEADS];
	xcb_atom_t local_atoms[WMSETBG_MAX_HEADS];
	bool have15;
	int count = 0;
	int i;

	memset(head_names, 0, sizeof(head_names));

	/* a request for an absent extension poisons the whole connection
	 * (libxcb refuses the send), so check presence first */
	rer = xcb_get_extension_data(xc, &xcb_randr_id);
	if (!rer || !rer->present)
		return false;

	vr = xcb_randr_query_version_reply(xc,
			xcb_randr_query_version(xc, 1, 5), NULL);
	if (!vr)
		return false;
	have15 = (vr->major_version > 1 ||
		  (vr->major_version == 1 && vr->minor_version >= 5));
	free(vr);
	if (!have15)
		return false;

	mr = xcb_randr_get_monitors_reply(xc,
			xcb_randr_get_monitors(xc, root, 1), NULL);
	if (!mr)
		return false;

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
		return false;

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

	return true;
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

static cairo_t *cairo_begin_pixmap(RContext *rc, xcb_pixmap_t p, int w, int h)
{
	cairo_surface_t *s;
	cairo_t *cr;

	s = cairo_xcb_surface_create(rc->xc, (xcb_drawable_t)p, rc->xvisual, w, h);
	cr = cairo_create(s);
	cairo_surface_destroy(s);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	return cr;
}

static void fill_pixmap(RContext *rc, xcb_pixmap_t p, int w, int h, const WXColor *c)
{
	cairo_t *cr;

	cr = cairo_begin_pixmap(rc, p, w, h);
	cairo_set_source_rgb(cr, c->red / 65535.0, c->green / 65535.0,
			     c->blue / 65535.0);
	cairo_paint(cr);
	cairo_surface_flush(cairo_get_target(cr));
	cairo_destroy(cr);
}

static void fill_rect(RContext *rc, xcb_pixmap_t p, int x, int y, int w, int h,
		      const WXColor *c)
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
	bool fimage = false;
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
			fimage = true;
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
			fimage = true;
			image = timage;
		}
		__attribute__((fallthrough));
	case 'C':
		{
			xcb_pixmap_t pixmap;
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

			wXFreePixmap(xc, pixmap);
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
	uint32_t px32;

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
		WXColor color;
		xcb_pixmap_t pixmap;

		texture->solid = 1;

		GETSTRORGOTO(val, tmp, 1, error);

		if (!wXParseColor(xc, rc->cmap, tmp, &color)) {
			wwarning("could not parse color %s in texture %s", tmp, text);
			goto error;
		}
		if (!wXAllocColor(xc, rc->cmap, &color)) {
			wwarning("could not allocate color %s in texture %s", tmp, text);
			goto error;
		}

		pixmap = wXCreatePixmap(xc, root, 8, 8, rc->depth);
		fill_pixmap(rc, pixmap, 8, 8, &color);

		texture->pixmap = pixmap;
		texture->color = color;
		texture->width = 8;
		texture->height = 8;
	} else if (strcasecmp(type, "vgradient") == 0
		   || strcasecmp(type, "dgradient") == 0 || strcasecmp(type, "hgradient") == 0) {
		WXColor color;
		RColor color1, color2;
		RImage *image;
		xcb_pixmap_t pixmap;
		RGradientStyle gtype;
		int iwidth, iheight;

		GETSTRORGOTO(val, tmp, 1, error);

		if (!wXParseColor(xc, rc->cmap, tmp, &color)) {
			wwarning("could not parse color %s in texture %s", tmp, text);
			goto error;
		}

		color1.red = color.red >> 8;
		color1.green = color.green >> 8;
		color1.blue = color.blue >> 8;

		GETSTRORGOTO(val, tmp, 2, error);

		if (!wXParseColor(xc, rc->cmap, tmp, &color)) {
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
		WXColor color;
		RColor **colors;
		RImage *image;
		xcb_pixmap_t pixmap;
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

			if (!wXParseColor(xc, rc->cmap, tmp, &color)) {
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
		WXColor color;
		xcb_pixmap_t pixmap = XCB_NONE;
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

		if (!wXParseColor(xc, rc->cmap, tmp, &color)) {
			wwarning("could not parse color %s in texture %s", tmp, text);
			RReleaseImage(image);
			goto error;
		}
		rcolor.red = color.red >> 8;
		rcolor.green = color.green >> 8;
		rcolor.blue = color.blue >> 8;
		if (!wXAllocColor(xc, rc->cmap, &color)) {
			RGetClosestXColor(rc, &rcolor, &px32);
			color.pixel = px32;
		} else {
			/* the allocated cell's pixel is never read back (the
			 * renderers use the server-adjusted RGB), so return the
			 * reservation immediately */
			wXFreeColors(xc, rc->cmap, &color.pixel, 1, 0);
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
				xcb_pixmap_t tpixmap =
				    wXCreatePixmap(xc, root, scrWidth, scrHeight, rc->depth);
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
		WXColor color;
		RColor color1, color2;
		RImage *image;
		RImage *gradient;
		RImage *tiled;
		xcb_pixmap_t pixmap;
		int opaq;
		char *file;
		RGradientStyle gtype;
		int twidth, theight;

		GETSTRORGOTO(val, file, 1, error);

		GETSTRORGOTO(val, tmp, 2, error);

		opaq = atoi(tmp);

		GETSTRORGOTO(val, tmp, 3, error);

		if (!wXParseColor(xc, rc->cmap, tmp, &color)) {
			wwarning("could not parse color %s in texture %s", tmp, text);
			goto error;
		}

		color1.red = color.red >> 8;
		color1.green = color.green >> 8;
		color1.blue = color.blue >> 8;

		GETSTRORGOTO(val, tmp, 4, error);

		if (!wXParseColor(xc, rc->cmap, tmp, &color)) {
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

static void freeTexture(RContext *rc, BackgroundTexture *texture)
{
	if (texture->solid) {
		uint32_t pixel;

		pixel = texture->color.pixel;
		/* dont free black/white pixels */
		if (pixel != rc->black && pixel != rc->white)
			wXFreeColors(xc, rc->cmap, &pixel, 1, 0);
	}
	if (texture->pixmap) {
		wXFreePixmap(xc, texture->pixmap);
	}
	wfree(texture->spec);
	wfree(texture);
}

static bool parseBackColor(RContext *rc, const char *str, WXColor *color, RColor *rcolor)
{
	uint32_t px32;

	if (!wXParseColor(xc, rc->cmap, str, color)) {
		wwarning("could not parse color %s", str);
		color->red = color->green = color->blue = 0;
		color->pixel = 0;
		rcolor->red = rcolor->green = rcolor->blue = 0;
		return false;
	}
	if (!wXAllocColor(xc, rc->cmap, color)) {
		rcolor->red = color->red >> 8;
		rcolor->green = color->green >> 8;
		rcolor->blue = color->blue >> 8;
		RGetClosestXColor(rc, rcolor, &px32);
		color->pixel = px32;
	} else {
		rcolor->red = 0;
		rcolor->green = 0;
		rcolor->blue = 0;
		wXFreeColors(xc, rc->cmap, &color->pixel, 1, 0);
	}
	return true;
}

static OutputSpec *findSpec(const char *name)
{
	int i;

	if (name && name[0]) {
		for (i = 0; i < nnamed; i++)
			if (namedSpecs[i].name && strcmp(namedSpecs[i].name, name) == 0) {
				namedSpecs[i].used = true;
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
	WXColor xc;
	RColor rcolor;
	bool old_smooth;
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
		smooth = (spec->smooth != 0);

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
	WXColor xcol;
	RColor rcolor;
	int i;

	texture = wmalloc(sizeof(BackgroundTexture));
	texture->pixmap = wXCreatePixmap(xc, root, scrWidth, scrHeight,
					 rc->depth);
	texture->width = scrWidth;
	texture->height = scrHeight;
	texture->solid = 0;
	texture->spec = wstrdup("per-output");

	/* cover the pixels that no monitor owns */
	parseBackColor(rc, defSpec.color ? defSpec.color : "gray20", &xcol, &rcolor);
	fill_pixmap(rc, texture->pixmap, scrWidth, scrHeight, &xcol);

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
				freeTexture(rc, textures[workspace]);
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
			freeTexture(rc, textures[workspace]);
	}

	newTexture->refcount++;
	textures[workspace] = newTexture;

	if (*maxTextures < workspace)
		*maxTextures = workspace;
}

static xcb_pixmap_t duplicatePixmap(RContext *rc, xcb_pixmap_t pixmap, int width, int height)
{
	xcb_connection_t *txc;
	int tsn = 0;
	xcb_pixmap_t copyP;
	xcb_gcontext_t tgc;
	xcb_get_input_focus_cookie_t ck;
	xcb_get_input_focus_reply_t *rep;
	xcb_generic_error_t *err;

	/* must open a new connection or the RetainPermanent will
	 * leave stuff allocated in RContext unallocated after exit */
	txc = xcconn_open(display[0] ? display : NULL, NULL, 0, &tsn);
	if (!txc) {
		wwarning("could not open display to update background image information");

		return XCB_NONE;
	}

	wXSync(xc);

	copyP = xcb_generate_id(txc);
	xcb_create_pixmap(txc, rc->depth, copyP, root, width, height);
	tgc = xcb_generate_id(txc);
	xcb_create_gc(txc, tgc, copyP, 0, NULL);
	xcb_copy_area(txc, pixmap, copyP, tgc, 0, 0, 0, 0, width, height);
	xcb_free_gc(txc, tgc);

	xcb_set_close_down_mode(txc, XCB_CLOSE_DOWN_RETAIN_PERMANENT);

	/* the barrier that matters: xcb_flush() does not guarantee the
	 * queued SetCloseDownMode is written and xcb_disconnect() discards
	 * the output buffer, so a reply read after the mode change is what
	 * forces the bytes out before the connection closes. A successful
	 * reply also proves the server processed every queued request,
	 * including the mode change */
	ck = xcb_get_input_focus(txc);
	rep = xcb_get_input_focus_reply(txc, ck, &err);
	if (err) {
		free(err);
		err = NULL;
	}
	if (rep) {
		free(rep);
		xcb_disconnect(txc);
		return copyP;
	}
	/* the retain could not be confirmed, so the copy must not be
	 * published as a zombie XID */
	xcb_disconnect(txc);

	return XCB_NONE;
}

static void setPixmapProperty(xcb_pixmap_t pixmap)
{
	static xcb_atom_t prop = XCB_ATOM_NONE;
	static xcb_atom_t eprop = XCB_ATOM_NONE;
	static xcb_atom_t zprop = XCB_ATOM_NONE;
	uint32_t old_root, old_eset, old_zomb;
	bool have_root, have_eset, have_zomb;

	if (prop == XCB_ATOM_NONE) {
		prop = WXInternAtom(xc, "_XROOTPMAP_ID");
		eprop = WXInternAtom(xc, "ESETROOT_PMAP_ID");
		zprop = WXInternAtom(xc, "_WMSETBG_RETAINED_PIXMAP");
	}

	wXGrabServer(xc);

	/* Free the previous background pixmap only when _XROOTPMAP_ID,
	 * ESETROOT_PMAP_ID and the private retained-zombie marker all hold
	 * the same XID. The marker is only published by our own compliant
	 * RetainPermanent setter, so a matching triple guarantees the XID
	 * belongs to a zombie client slot. Killing an XID whose slot was
	 * recycled to a live connection tears that client down with
	 * "X connection broken", so a bare pair left by a foreign setter
	 * or by an older wmsetbg is left untouched instead of killed. */
	have_root = WXGetFormat32N(xc, root, prop, XCB_ATOM_PIXMAP, &old_root, 1);
	have_eset = WXGetFormat32N(xc, root, eprop, XCB_ATOM_PIXMAP, &old_eset, 1);
	have_zomb = WXGetFormat32N(xc, root, zprop, XCB_ATOM_PIXMAP, &old_zomb, 1);
	if (have_root && have_eset && have_zomb
	    && old_root == old_eset && old_eset == old_zomb
	    && old_zomb != XCB_NONE) {
		/* checked so an invalid XID is consumed here instead of
		 * ending the process — the old dummyErrorHandler trap */
		(void) WXCheckVoid(xc, xcb_kill_client_checked(xc, old_eset));
	}
	if (pixmap) {
		uint32_t v = (uint32_t)pixmap;
		WXSetFormat32N(xc, root, prop, XCB_ATOM_PIXMAP, &v, 1);
		WXSetFormat32N(xc, root, eprop, XCB_ATOM_PIXMAP, &v, 1);
		WXSetFormat32N(xc, root, zprop, XCB_ATOM_PIXMAP, &v, 1);
	} else {
		WXDeleteProperty(xc, root, prop);
		WXDeleteProperty(xc, root, eprop);
		WXDeleteProperty(xc, root, zprop);
	}

	wXUngrabServer(xc);
	wXFlush(xc);
}

static void changeTexture(RContext *rc, BackgroundTexture * texture)
{
	uint32_t back_pixel;

	if (!texture) {
		return;
	}

	if (texture->solid) {
		back_pixel = texture->color.pixel;
		xcb_change_window_attributes(xc, root, XCB_CW_BACK_PIXEL,
					     &back_pixel);
	} else {
		xcb_change_window_attributes(xc, root, XCB_CW_BACK_PIXMAP,
					     &texture->pixmap);
	}
	xcb_clear_area(xc, 0, root, 0, 0, 0, 0);

	wXSync(xc);

	setPixmapProperty(duplicatePixmap(rc, texture->pixmap, texture->width,
					  texture->height));
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
				changeTexture(rc, textures[0]);
			} else {
				changeTexture(rc, textures[workspace]);
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

/* Write a color key to the domain via wdwrite (value must be proplist-quoted) */
static void write_color_key(const char *domain, const char *key,
			    unsigned char r, unsigned char g, unsigned char b)
{
	char buf[12];

	snprintf(buf, sizeof(buf), "\"#%02x%02x%02x\"", r, g, b);
	(void)wspawnvp_wait((char *[]){"wdwrite", (char *)domain,
				   (char *)key, buf, NULL});
}

/* Write a (solid, "#rrggbb") texture key to the domain via wdwrite. */
static void write_solid_texture_key(const char *domain, const char *key,
				    unsigned char r, unsigned char g,
				    unsigned char b)
{
	char buf[24];

	snprintf(buf, sizeof(buf), "(solid, \"#%02x%02x%02x\")", r, g, b);
	(void)wspawnvp_wait((char *[]){"wdwrite", (char *)domain,
				   (char *)key, buf, NULL});
}

/* Write a (dgradient, "#rrggbb", "#rrggbb") texture key to the domain. */
static void write_gradient_texture_key(const char *domain, const char *key,
				       unsigned char r1, unsigned char g1,
				       unsigned char b1,
				       unsigned char r2, unsigned char g2,
				       unsigned char b2)
{
	char buf[48];

	snprintf(buf, sizeof(buf),
		 "(dgradient, \"#%02x%02x%02x\", \"#%02x%02x%02x\")",
		 r1, g1, b1, r2, g2, b2);
	(void)wspawnvp_wait((char *[]){"wdwrite", (char *)domain,
				   (char *)key, buf, NULL});
}

/* Derive palette from <image> and write color/texture keys to the domain */
static void derive_dynamic_colors(RContext *rc, const char *domain,
				  const char *image_name, const char *back_color,
				  int mode)
{
	RImage *image = NULL;
	RDynamicTheme *th;

	if (image_name)
		image = loadImage(rc, image_name);
	if (!image && back_color) {
		RColor c;
		WXColor xcol;

		if (wXParseColor(xc, rc->cmap, back_color, &xcol)) {
			c.red = xcol.red >> 8;
			c.green = xcol.green >> 8;
			c.blue = xcol.blue >> 8;
			image = RCreateImage(1, 1, 0);
			if (image) {
				image->data[0] = c.red;
				image->data[1] = c.green;
				image->data[2] = c.blue;
			}
		}
	}
	if (!image) {
		wwarning("--dynamic-color: no image to derive from");
		return;
	}

	th = RDeriveDynamicTheme(image, mode);
	if (th) {
		RColor dom = th->roles[RDYNAMIC_ROLE_DOMINANT_BACK];
		RColor sec = th->roles[RDYNAMIC_ROLE_SECONDARY_BACK];
		RColor ter = th->roles[RDYNAMIC_ROLE_TERTIARY_BACK];
		RColor in = th->roles[RDYNAMIC_ROLE_ICON_BACK_NEAR];
		RColor ifar = th->roles[RDYNAMIC_ROLE_ICON_BACK_FAR];
		RColor hl = th->roles[RDYNAMIC_ROLE_HIGHLIGHT_BACK];
		RColor dt = th->roles[RDYNAMIC_ROLE_DOMINANT_TEXT];
		RColor st = th->roles[RDYNAMIC_ROLE_SECONDARY_TEXT];
		RColor tt = th->roles[RDYNAMIC_ROLE_TERTIARY_TEXT];
		RColor ht = th->roles[RDYNAMIC_ROLE_HIGHLIGHT_TEXT];
		RColor md = th->roles[RDYNAMIC_ROLE_MENU_DISABLED_TEXT];
		RColor fb = th->roles[RDYNAMIC_ROLE_FOCUSED_BORDER];

		write_solid_texture_key(domain, "FTitleBack", dom.red, dom.green, dom.blue);
		write_solid_texture_key(domain, "MenuTitleBack", dom.red, dom.green, dom.blue);
		write_solid_texture_key(domain, "UTitleBack", sec.red, sec.green, sec.blue);
		write_solid_texture_key(domain, "MenuTextBack", sec.red, sec.green, sec.blue);
		write_solid_texture_key(domain, "ResizebarBack", sec.red, sec.green, sec.blue);
		write_solid_texture_key(domain, "WidgetColor", sec.red, sec.green, sec.blue);
		write_solid_texture_key(domain, "PTitleBack", ter.red, ter.green, ter.blue);
		write_gradient_texture_key(domain, "IconBack",
					   in.red, in.green, in.blue,
					   ifar.red, ifar.green, ifar.blue);

		write_color_key(domain, "IconTitleBack", sec.red, sec.green, sec.blue);
		write_color_key(domain, "FTitleColor", dt.red, dt.green, dt.blue);
		write_color_key(domain, "UTitleColor", st.red, st.green, st.blue);
		write_color_key(domain, "PTitleColor", tt.red, tt.green, tt.blue);
		write_color_key(domain, "MenuTitleColor", dt.red, dt.green, dt.blue);
		write_color_key(domain, "MenuTextColor", st.red, st.green, st.blue);
		write_color_key(domain, "MenuDisabledColor", md.red, md.green, md.blue);
		write_color_key(domain, "IconTitleColor", st.red, st.green, st.blue);
		write_color_key(domain, "HighlightColor", hl.red, hl.green, hl.blue);
		write_color_key(domain, "HighlightTextColor", ht.red, ht.green, ht.blue);
		write_color_key(domain, "ClipTitleColor", dt.red, dt.green, dt.blue);
		write_color_key(domain, "CClipTitleColor", dt.red, dt.green, dt.blue);
		write_color_key(domain, "TabInactiveTitleColor", st.red, st.green, st.blue);
		write_color_key(domain, "FrameBorderColor", sec.red, sec.green, sec.blue);
		write_color_key(domain, "FrameFocusedBorderColor", fb.red, fb.green, fb.blue);
		write_color_key(domain, "FrameSelectedBorderColor", dt.red, dt.green, dt.blue);

		free(th);
	} else {
		wwarning("--dynamic-color: could not derive the palette");
	}
	RReleaseImage(image);
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
	puts("     --dynamic-color[=MODE]    derive UI palette from the wallpaper and");
	puts("                               write it to the domain (MODE: Auto|Light|Dark)");
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
	if (workspace > j || j == 0) {
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

	value = WMGetPropListDescription(array, false);
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
	char *dyn_image = NULL;
	char *dyn_color = NULL;
	char *domain = "WindowMaker";
	char *domain_dup = NULL;	/* owned copy once -D overrides the default */
	int update = 0;
	char *texture = NULL;
	int workspace = -1;
	int dynamic_color_mode = -1;	/* -1 off, RDYNAMIC_MODE_* otherwise */
	OutputSpec *curSpec;
	int j;
	bool texture_from_p;
	bool any_spec;
	const char *connect_name;
	WMPropList *pl;
	WMPropList *plStyle;
	WMPropList *plPath;
	WMPropList *plColor;

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
			xineStretch = true;
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
			havePerOutput = true;
		} else if (strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--update-wmaker") == 0) {
			update++;
		} else if (strcmp(argv[i], "-D") == 0 || strcmp(argv[i], "--update-domain") == 0) {
			update++;
			i++;
			if (i >= argc) {
				wfatal("too few arguments for %s", argv[i - 1]);
				quit(1);
			}
			wfree(domain_dup);
			domain = domain_dup = wstrdup(argv[i]);
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
			if (sscanf(argv[i], "%i", &workspace) != 1 || workspace < 0
			    || workspace >= WORKSPACE_COUNT) {
				wfatal("bad value for workspace number: \"%s\"", argv[i]);
				quit(1);
			}
		} else if (strcmp(argv[i], "--dynamic-color") == 0) {
			dynamic_color_mode = RDYNAMIC_MODE_AUTO;
		} else if (strncmp(argv[i], "--dynamic-color=", 16) == 0) {
			const char *m = argv[i] + 16;

			if (strcasecmp(m, "Auto") == 0)
				dynamic_color_mode = RDYNAMIC_MODE_AUTO;
			else if (strcasecmp(m, "Light") == 0)
				dynamic_color_mode = RDYNAMIC_MODE_LIGHT;
			else if (strcasecmp(m, "Dark") == 0)
				dynamic_color_mode = RDYNAMIC_MODE_DARK;
			else {
				wfatal("unknown --dynamic-color mode '%s' (Auto|Light|Dark)", m);
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
	smooth = (defSpec.smooth >= 0) ? (defSpec.smooth != 0) : false;
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

	/* --dynamic-color source: the default image, else the first
	 * per-output image, else the first user color. The "gray20"
	 * fill default is not a palette source. */
	if (dynamic_color_mode >= 0) {
		dyn_image = image_name;
		dyn_color = defSpec.color;
		if (!dyn_image) {
			for (j = 0; j < nnamed && !dyn_image; j++)
				dyn_image = namedSpecs[j].image;
		}
		if (!dyn_image && !dyn_color) {
			for (j = 0; j < nnamed && !dyn_color; j++)
				dyn_color = namedSpecs[j].color;
		}
	}

	if (!any_spec && style && strcmp(style, "solid") == 0)
		any_spec = true;
	for (j = 0; j < nnamed && !any_spec; j++) {
		any_spec = (namedSpecs[j].image != NULL);
		if (!any_spec && namedSpecs[j].style &&
		    strcmp(namedSpecs[j].style, "solid") == 0)
			any_spec = true;
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
			smooth = true;
		if (val)
			WMReleasePropList(val);
	}

	/* the global `display` string stays as-is — duplicatePixmap()
	 * reuses it for its own connection */
	connect_name = display[0] ? display : NULL;
	xc = xcconn_open(connect_name, NULL, 0, &scr);
	if (!xc) {
		wfatal("could not open display");
		quit(1);
	}
	root = wXRootWindow(xc, scr);

	scrWidth = wXDisplayWidth(xc, scr);
	scrHeight = wXDisplayHeight(xc, scr);
	scrX = scrY = 0;

	if (!initRandR())
		initXinerama();

	rattr.flags = RC_DefaultVisual;
	rc = RCreateContextXCB(xc, scr, &rattr);
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
				plStyle = WMCreatePLString("solid");
				plColor = WMCreatePLString(back_color);
				pl = WMCreatePLArray(plStyle, plColor, NULL);
				gen_texture = WMGetPropListDescription(pl, false);
				WMReleasePropList(pl);
				WMReleasePropList(plStyle);
				WMReleasePropList(plColor);
				texture = gen_texture;
			} else if (image_name) {
				char *image_path = getFullPixmapPath(image_name);

				if (!image_path) {
					wwarning("could not resolve the path for image %s",
						 image_name);
				} else {
					/* build the (style, path, color) array via the proplist
					 * API so quotes and backslashes in the path are escaped
					 * in the serialized description, instead of breaking
					 * out of a hand-built "..." */
					plStyle = WMCreatePLString(style);
					plPath = WMCreatePLString(image_path);
					plColor = WMCreatePLString(back_color);
					pl = WMCreatePLArray(plStyle, plPath, plColor, NULL);
					gen_texture = WMGetPropListDescription(pl, false);
					WMReleasePropList(pl);
					WMReleasePropList(plStyle);
					WMReleasePropList(plPath);
					WMReleasePropList(plColor);
					wfree(image_path);
					texture = gen_texture;
				}
			}
		}

		if (update && workspace < 0 && texture) {
			update_status = updateDomain(domain, "WorkspaceBack", texture);
			if (update_status >= 0) {
				if (dynamic_color_mode >= 0)
					derive_dynamic_colors(rc, domain, dyn_image,
							      dyn_color, dynamic_color_mode);
				if (gen_texture)
					wfree(gen_texture);
				if (namedSpecs)
					wfree(namedSpecs);
				wfree(domain_dup);
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
			changeTexture(rc, tex);
		else if (texture) {
			/* always update domain */
			update_status = changeTextureForWorkspace(domain, texture, workspace);
		}

		freeTexture(rc, tex);
		if (gen_texture)
			wfree(gen_texture);
		if (namedSpecs) {
			wfree(namedSpecs);
			namedSpecs = NULL;
		}

		if (dynamic_color_mode >= 0)
			derive_dynamic_colors(rc, domain, dyn_image, dyn_color,
					      dynamic_color_mode);

		if (update_status >= 0) {
			wfree(domain_dup);
			RDestroyContext(rc);
			WMReleaseApplication();
			return update_status;
		}
	}

	if (namedSpecs)
		wfree(namedSpecs);
	wfree(domain_dup);
	RDestroyContext(rc);
	WMReleaseApplication();
	return 0;
}
