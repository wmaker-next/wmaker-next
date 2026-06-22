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
#include <X11/Xlib-xcb.h>
#include <xcb/shm.h>

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

static xcb_screen_t *find_xcb_screen(xcb_connection_t *xc, int screen_number)
{
	xcb_screen_iterator_t it;

	it = xcb_setup_roots_iterator(xcb_get_setup(xc));
	for (; it.rem; xcb_screen_next(&it), screen_number--) {
		if (screen_number == 0)
			return it.data;
	}
	return NULL;
}

/*
 * Walk the per-screen depth list for visualid and hand back both the
 * visualtype and the depth it lives under, the equivalent xcb-util helper
 * (xcb_aux_get_visualtype) takes a depth as input rather than reporting it
 * so this avoids that dependency for one iterator walk.
 */
static xcb_visualtype_t *find_xcb_visual(xcb_screen_t *s, xcb_visualid_t visualid, uint8_t *depth_ret)
{
	xcb_depth_iterator_t di;
	xcb_visualtype_iterator_t vi;

	for (di = xcb_screen_allowed_depths_iterator(s); di.rem; xcb_depth_next(&di)) {
		for (vi = xcb_depth_visuals_iterator(di.data); vi.rem; xcb_visualtype_next(&vi)) {
			if (vi.data->visual_id == visualid) {
				if (depth_ret)
					*depth_ret = di.data->depth;
				return vi.data;
			}
		}
	}
	return NULL;
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

	/* xcb view of the same connection. The screen and visual lookups walk
	 * the connection setup so they cannot fail once the visualid passed
	 * the TrueColor check above, copy_xgc is the same server-side XID as
	 * copy_gc so internal puts and external XCopyArea share one GC. */
	context->xc = XGetXCBConnection(dpy);
	context->xscreen = find_xcb_screen(context->xc, screen_number);
	context->xvisual = find_xcb_visual(context->xscreen,
					   XVisualIDFromVisual(context->visual), &context->xdepth);
	context->copy_xgc = (xcb_gcontext_t)XGContextFromGC(context->copy_gc);

	/* calc offsets to create a TrueColor pixel */
	context->red_offset = count_offset(context->xvisual->red_mask);
	context->green_offset = count_offset(context->xvisual->green_mask);
	context->blue_offset = count_offset(context->xvisual->blue_mask);

	/* check avaiability of MIT-SHM */
	if (!(context->attribs->flags & RC_UseSharedMemory)) {
		context->attribs->flags |= RC_UseSharedMemory;
		context->attribs->use_shared_memory = True;
	}

	context->flags.use_shared_pixmap = 0;
	if (context->attribs->use_shared_memory) {
		xcb_shm_query_version_reply_t *r;

		r = xcb_shm_query_version_reply(context->xc,
						xcb_shm_query_version(context->xc), NULL);
		if (!r)
			context->attribs->use_shared_memory = False;
		free(r);
	}

	return context;
}

void RDestroyContext(RContext *context)
{
	if (context) {
		RReleaseSharedSegment(context);
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
