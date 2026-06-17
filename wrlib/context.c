/* context.c - X context management
 *
 * Raster graphics library
 *
 * Copyright (c) 1997-2003 Alfredo K. Kojima
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, see <https://www.gnu.org/licenses/>.
 */

#include <config.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wraster.h"
#include "scale.h"
#include "xutil.h"
#include "wr_i18n.h"


static Bool bestContext(Display * dpy, int screen_number, RContext * context);

static const RContextAttributes DEFAULT_CONTEXT_ATTRIBS = {
	RC_UseSharedMemory,	/* flags */
	0,
	0,
	0,
	0,
	True,			/* use_shared_memory */
	RMitchellFilter
};

static char *mygetenv(const char *var, int scr)
{
	char *p;
	char varname[64];

	snprintf(varname, sizeof(varname), "%s%i", var, scr);
	p = getenv(varname);
	if (!p) {
		p = getenv(var);
	}
	return p;
}

static void gatherconfig(RContext * context, int screen_n)
{
	char *ptr;

	ptr = mygetenv("WRASTER_GAMMA", screen_n);
	if (ptr) {
		float g1, g2, g3;
		if (sscanf(ptr, "%f/%f/%f", &g1, &g2, &g3) != 3 || g1 <= 0.0F || g2 <= 0.0F || g3 <= 0.0F) {
			fprintf(stderr, _("wrlib: invalid value \"%s\" for %s\n"), ptr, "WRASTER_GAMMA");
		} else {
			context->attribs->flags |= RC_GammaCorrection;
			context->attribs->rgamma = g1;
			context->attribs->ggamma = g2;
			context->attribs->bgamma = g3;
		}
	}
}

static void getColormap(RContext * context, int screen_number)
{
	Colormap cmap = None;
	XStandardColormap *cmaps;
	int ncmaps, i;

	if (XGetRGBColormaps(context->dpy,
			     RootWindow(context->dpy, screen_number), &cmaps, &ncmaps, XA_RGB_DEFAULT_MAP)) {
		for (i = 0; i < ncmaps; ++i) {
			if (cmaps[i].visualid == context->visual->visualid) {
				cmap = cmaps[i].colormap;
				break;
			}
		}
		XFree(cmaps);
	}
	if (cmap == None) {
		XColor color;

		cmap = XCreateColormap(context->dpy,
				       RootWindow(context->dpy, screen_number), context->visual, AllocNone);

		color.red = color.green = color.blue = 0;
		XAllocColor(context->dpy, cmap, &color);
		context->black = color.pixel;

		color.red = color.green = color.blue = 0xffff;
		XAllocColor(context->dpy, cmap, &color);
		context->white = color.pixel;

	}
	context->cmap = cmap;
}

static int count_offset(unsigned long mask)
{
	int i;

	i = 0;
	while ((mask & 1) == 0) {
		i++;
		mask = mask >> 1;
	}
	return i;
}

RContext *RCreateContext(Display * dpy, int screen_number, const RContextAttributes * attribs)
{
	RContext *context;
	XGCValues gcv;

	context = malloc(sizeof(RContext));
	if (!context) {
		RErrorCode = RERR_NOMEMORY;
		return NULL;
	}
	memset(context, 0, sizeof(RContext));

	context->dpy = dpy;

	context->screen_number = screen_number;

	context->attribs = malloc(sizeof(RContextAttributes));
	if (!context->attribs) {
		free(context);
		RErrorCode = RERR_NOMEMORY;
		return NULL;
	}
	if (!attribs)
		*context->attribs = DEFAULT_CONTEXT_ATTRIBS;
	else
		*context->attribs = *attribs;

	if (!(context->attribs->flags & RC_ScalingFilter)) {
		context->attribs->flags |= RC_ScalingFilter;
		context->attribs->scaling_filter = RCatmullRomFilter;
	}

	/* get configuration from environment variables */
	gatherconfig(context, screen_number);
	wraster_change_filter(context->attribs->scaling_filter);
	if ((context->attribs->flags & RC_VisualID)) {
		XVisualInfo *vinfo, templ;
		int nret;

		templ.screen = screen_number;
		templ.visualid = context->attribs->visualid;
		vinfo = XGetVisualInfo(context->dpy, VisualIDMask | VisualScreenMask, &templ, &nret);
		if (!vinfo || nret == 0) {
			free(context);
			RErrorCode = RERR_BADVISUALID;
			return NULL;
		}

		if (vinfo[0].visual == DefaultVisual(dpy, screen_number)) {
			context->attribs->flags |= RC_DefaultVisual;
		} else {
			XSetWindowAttributes attr;
			unsigned long mask;

			context->visual = vinfo[0].visual;
			context->depth = vinfo[0].depth;
			context->vclass = vinfo[0].class;
			getColormap(context, screen_number);
			attr.colormap = context->cmap;
			attr.override_redirect = True;
			attr.border_pixel = 0;
			attr.background_pixel = 0;
			mask = CWBorderPixel | CWColormap | CWOverrideRedirect | CWBackPixel;
			context->drawable =
			    XCreateWindow(dpy, RootWindow(dpy, screen_number), 1, 1,
					  1, 1, 0, context->depth, CopyFromParent, context->visual, mask, &attr);
		}
		XFree(vinfo);
	}

	/* use default */
	if (!context->visual) {
		if ((context->attribs->flags & RC_DefaultVisual)
		    || !bestContext(dpy, screen_number, context)) {
			context->visual = DefaultVisual(dpy, screen_number);
			context->depth = DefaultDepth(dpy, screen_number);
			context->cmap = DefaultColormap(dpy, screen_number);
			context->drawable = RootWindow(dpy, screen_number);
			context->black = BlackPixel(dpy, screen_number);
			context->white = WhitePixel(dpy, screen_number);
			context->vclass = context->visual->class;
		}
	}

	if (context->vclass != TrueColor) {
		fprintf(stderr,
			_("wrlib: visual 0x%lX is not TrueColor, indexed-colour visuals are no longer supported\n"),
			context->visual->visualid);
		RErrorCode = RERR_BADVISUALCLASS;
		free(context->attribs);
		free(context);
		return NULL;
	}

	gcv.function = GXcopy;
	gcv.graphics_exposures = False;
	context->copy_gc = XCreateGC(dpy, context->drawable, GCFunction | GCGraphicsExposures, &gcv);

	/* calc offsets to create a TrueColor pixel */
	context->red_offset = count_offset(context->visual->red_mask);
	context->green_offset = count_offset(context->visual->green_mask);
	context->blue_offset = count_offset(context->visual->blue_mask);

	/* check avaiability of MIT-SHM */
#ifdef USE_XSHM
	if (!(context->attribs->flags & RC_UseSharedMemory)) {
		context->attribs->flags |= RC_UseSharedMemory;
		context->attribs->use_shared_memory = True;
	}

	if (context->attribs->use_shared_memory) {
		int major, minor;
		Bool sharedPixmaps;

		context->flags.use_shared_pixmap = 0;

		if (!XShmQueryVersion(context->dpy, &major, &minor, &sharedPixmaps)) {
			context->attribs->use_shared_memory = False;
		} else {
			if (XShmPixmapFormat(context->dpy) == ZPixmap)
				context->flags.use_shared_pixmap = sharedPixmaps;
		}
	}
#endif

	return context;
}

void RDestroyContext(RContext *context)
{
	if (context) {
#ifdef USE_XSHM
		RReleaseSharedSegment(context);
#endif
		if (context->copy_gc)
			XFreeGC(context->dpy, context->copy_gc);
		if (context->attribs) {
			if ((context->attribs->flags & RC_VisualID) &&
			    !(context->attribs->flags & RC_DefaultVisual))
				XDestroyWindow(context->dpy, context->drawable);
			free(context->attribs);
		}
		free(context);
	}
}

static Bool bestContext(Display * dpy, int screen_number, RContext * context)
{
	XVisualInfo *vinfo = NULL, rvinfo;
	int best = -1, numvis, i;
	long flags;
	XSetWindowAttributes attr;

	rvinfo.class = TrueColor;
	rvinfo.screen = screen_number;
	flags = VisualClassMask | VisualScreenMask;

	vinfo = XGetVisualInfo(dpy, flags, &rvinfo, &numvis);
	if (vinfo) {		/* look for a TrueColor, 24-bit or more (pref 24) */
		for (i = numvis - 1, best = -1; i >= 0; i--) {
			if (vinfo[i].depth == 24)
				best = i;
			else if (vinfo[i].depth > 24 && best < 0)
				best = i;
		}
	}
	if (best > -1) {
		context->visual = vinfo[best].visual;
		context->depth = vinfo[best].depth;
		context->vclass = vinfo[best].class;
		getColormap(context, screen_number);
		attr.colormap = context->cmap;
		attr.override_redirect = True;
		attr.border_pixel = 0;
		context->drawable =
		    XCreateWindow(dpy, RootWindow(dpy, screen_number),
				  1, 1, 1, 1, 0, context->depth,
				  CopyFromParent, context->visual,
				  CWBorderPixel | CWColormap | CWOverrideRedirect, &attr);
	}
	if (vinfo)
		XFree((char *)vinfo);

	if (best < 0)
		return False;
	else
		return True;
}
