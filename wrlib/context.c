/* context.c - X context management (xcb)
 *
 * Raster graphics library
 *
 * Copyright (c) 1997-2003 Alfredo K. Kojima
 * Copyright (c) 2009-2026 Window Maker Team
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xcb/xcb.h>
#include <xcb/shm.h>

#include "wraster.h"
#include "scale.h"
#include "xutil.h"
#include "wr_i18n.h"


static const RContextAttributes DEFAULT_CONTEXT_ATTRIBS = {
	RC_UseSharedMemory,	/* flags */
	0,
	0,
	0,
	0,
	1,			/* use_shared_memory */
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

/*
 * Pick the deepest TrueColor visual on the screen, preferring exactly 24
 * over 30/32 so the common 8/8/8 packer applies. Returns 0 when the
 * screen has no TrueColor visual at all (RCreateContextXCB then refuses).
 */
static int bestContext(RContext *context)
{
	xcb_depth_iterator_t di;
	xcb_visualtype_iterator_t vi;
	xcb_visualtype_t *best = NULL;
	uint8_t bestdepth = 0;

	for (di = xcb_screen_allowed_depths_iterator(context->xscreen); di.rem; xcb_depth_next(&di)) {
		for (vi = xcb_depth_visuals_iterator(di.data); vi.rem; xcb_visualtype_next(&vi)) {
			if (vi.data->_class != XCB_VISUAL_CLASS_TRUE_COLOR)
				continue;
			/* first depth-24 in setup order wins, matching the
			 * original XGetVisualInfo numvis-1..0 walk so the
			 * chosen visual does not change across the rewrite */
			if (di.data->depth == 24 && bestdepth != 24) {
				best = vi.data;
				bestdepth = 24;
			} else if (di.data->depth > 24 && !best) {
				best = vi.data;
				bestdepth = di.data->depth;
			} else if (!best) {
				best = vi.data;
				bestdepth = di.data->depth;
			}
		}
	}
	if (!best)
		return 0;

	context->xvisual = best;
	context->xdepth = bestdepth;
	return 1;
}

/*
 * Find a depth-32 TrueColor visual for per-pixel alpha and create a
 * colormap against it once so callers can hand both to XCreateWindow /
 * cairo_xcb_surface_create() without repeating the walk. Leaves
 * xvisual_argb NULL and cmap_argb 0 when the server has none (Xnest,
 * some VNC servers), the caller then keeps the opaque path.
 */
static void probeARGBVisual(RContext *context)
{
	xcb_depth_iterator_t di;
	xcb_visualtype_iterator_t vi;

	for (di = xcb_screen_allowed_depths_iterator(context->xscreen); di.rem; xcb_depth_next(&di)) {
		if (di.data->depth != 32)
			continue;
		for (vi = xcb_depth_visuals_iterator(di.data); vi.rem; xcb_visualtype_next(&vi)) {
			if (vi.data->_class != XCB_VISUAL_CLASS_TRUE_COLOR)
				continue;
			context->xvisual_argb = vi.data;
			context->cmap_argb = xcb_generate_id(context->xc);
			xcb_create_colormap(context->xc, XCB_COLORMAP_ALLOC_NONE,
					    context->cmap_argb, context->xscreen->root,
					    vi.data->visual_id);
			return;
		}
	}
}

static uint32_t pixel_for(RContext *ctx, uint32_t r, uint32_t g, uint32_t b)
{
	uint32_t rmask = ctx->xvisual->red_mask >> ctx->red_offset;
	uint32_t gmask = ctx->xvisual->green_mask >> ctx->green_offset;
	uint32_t bmask = ctx->xvisual->blue_mask >> ctx->blue_offset;

	return ((r * rmask / 0xff) << ctx->red_offset)
	     | ((g * gmask / 0xff) << ctx->green_offset)
	     | ((b * bmask / 0xff) << ctx->blue_offset);
}

/*
 * The chosen visual is not the screen default so pixmaps need a 1x1
 * override-redirect window of the right depth/visual to anchor against,
 * and a private colormap so a future RGetClosestXColor caller passing the
 * result to a server-side AllocColor on a non-default visual works. The
 * RGB_DEFAULT_MAP property lookup the Xlib path used to do is dropped,
 * nothing in tree consumed the resulting cmap and modern servers do not
 * publish one anyway.
 */
static void makePrivateDrawable(RContext *context)
{
	uint32_t cwvals[4];

	context->cmap = xcb_generate_id(context->xc);
	xcb_create_colormap(context->xc, XCB_COLORMAP_ALLOC_NONE, context->cmap,
			    context->xscreen->root, context->xvisual->visual_id);

	cwvals[0] = 0;				/* back_pixel */
	cwvals[1] = 0;				/* border_pixel */
	cwvals[2] = 1;				/* override_redirect */
	cwvals[3] = context->cmap;
	context->drawable = xcb_generate_id(context->xc);
	xcb_create_window(context->xc, context->xdepth, context->drawable,
			  context->xscreen->root, 1, 1, 1, 1, 0,
			  XCB_WINDOW_CLASS_COPY_FROM_PARENT, context->xvisual->visual_id,
			  XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL |
			  XCB_CW_OVERRIDE_REDIRECT | XCB_CW_COLORMAP, cwvals);
}

RContext *RCreateContextXCB(xcb_connection_t *xc, int screen_number, const RContextAttributes *attribs)
{
	RContext *context;
	uint32_t gcvals[2];

	context = calloc(1, sizeof(RContext));
	if (!context) {
		RErrorCode = RERR_NOMEMORY;
		return NULL;
	}

	context->xc = xc;
	context->screen_number = screen_number;
	context->xscreen = find_xcb_screen(xc, screen_number);
	if (!context->xscreen) {
		free(context);
		RErrorCode = RERR_BADVISUALID;
		return NULL;
	}

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

	gatherconfig(context, screen_number);
	wraster_change_filter(context->attribs->scaling_filter);

	if ((context->attribs->flags & RC_VisualID)) {
		context->xvisual = find_xcb_visual(context->xscreen,
						   context->attribs->visualid, &context->xdepth);
		if (!context->xvisual) {
			free(context->attribs);
			free(context);
			RErrorCode = RERR_BADVISUALID;
			return NULL;
		}
		if (context->xvisual->visual_id == context->xscreen->root_visual)
			context->attribs->flags |= RC_DefaultVisual;
	}

	if (!context->xvisual) {
		if ((context->attribs->flags & RC_DefaultVisual) || !bestContext(context)) {
			context->xvisual = find_xcb_visual(context->xscreen,
							   context->xscreen->root_visual,
							   &context->xdepth);
			context->attribs->flags |= RC_DefaultVisual;
		}
	}

	context->vclass = context->xvisual->_class;
	if (context->vclass != XCB_VISUAL_CLASS_TRUE_COLOR) {
		fprintf(stderr,
			_("wrlib: visual 0x%X is not TrueColor, indexed-colour visuals are no longer supported\n"),
			context->xvisual->visual_id);
		RErrorCode = RERR_BADVISUALCLASS;
		free(context->attribs);
		free(context);
		return NULL;
	}

	context->depth = context->xdepth;
	context->red_offset = count_offset(context->xvisual->red_mask);
	context->green_offset = count_offset(context->xvisual->green_mask);
	context->blue_offset = count_offset(context->xvisual->blue_mask);

	if (context->xvisual->visual_id == context->xscreen->root_visual) {
		context->cmap = context->xscreen->default_colormap;
		context->drawable = context->xscreen->root;
		context->black = context->xscreen->black_pixel;
		context->white = context->xscreen->white_pixel;
	} else {
		makePrivateDrawable(context);
		context->black = pixel_for(context, 0, 0, 0);
		context->white = pixel_for(context, 0xff, 0xff, 0xff);
	}

	probeARGBVisual(context);

	gcvals[0] = XCB_GX_COPY;
	gcvals[1] = 0;
	context->copy_xgc = xcb_generate_id(xc);
	xcb_create_gc(xc, context->copy_xgc, context->drawable,
		      XCB_GC_FUNCTION | XCB_GC_GRAPHICS_EXPOSURES, gcvals);

	if (!(context->attribs->flags & RC_UseSharedMemory)) {
		context->attribs->flags |= RC_UseSharedMemory;
		context->attribs->use_shared_memory = 1;
	}

	context->flags.use_shared_pixmap = 0;
	if (context->attribs->use_shared_memory) {
		xcb_shm_query_version_reply_t *r;

		r = xcb_shm_query_version_reply(xc, xcb_shm_query_version(xc), NULL);
		if (!r)
			context->attribs->use_shared_memory = 0;
		free(r);
	}

	return context;
}

void RDestroyContext(RContext *context)
{
	if (context) {
		RReleaseSharedSegment(context);
		if (context->copy_xgc)
			xcb_free_gc(context->xc, context->copy_xgc);
		if (context->cmap_argb)
			xcb_free_colormap(context->xc, context->cmap_argb);
		if (context->drawable && context->xscreen
		    && context->drawable != context->xscreen->root) {
			xcb_destroy_window(context->xc, context->drawable);
			xcb_free_colormap(context->xc, context->cmap);
		}
		free(context->attribs);
		free(context);
	}
}
