/* xutil.c - utility functions for X
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <assert.h>

#ifdef USE_XSHM
#include <sys/ipc.h>
#include <sys/shm.h>
#endif				/* USE_XSHM */

#include "wraster.h"
#include "xutil.h"
#include "wr_i18n.h"


#ifdef USE_XSHM

static int shmError;

static int (*oldErrorHandler)(Display *dpy, XErrorEvent *err);

static int errorHandler(Display * dpy, XErrorEvent * err)
{
	shmError = 1;
	if (err->error_code != BadAccess)
		(*oldErrorHandler) (dpy, err);

	return 0;
}

static void releaseSharedSegment(RContext *context)
{
	if (context->shm_size == 0)
		return;

	XShmDetach(context->dpy, &context->shm_seg);
	/* The detach must reach the server before we drop our own
	 * attachment and IPC_RMID the segment, otherwise nattch can hit
	 * zero and the kernel frees it before the server reads from it. */
	XSync(context->dpy, False);
	if (shmdt(context->shm_seg.shmaddr) < 0)
		fprintf(stderr, _("wrlib: error occurred while releasing shared segment, %s: %s\n"),
			"shmdt", strerror(errno));
	if (shmctl(context->shm_seg.shmid, IPC_RMID, NULL) < 0)
		fprintf(stderr, _("wrlib: error occurred while releasing shared segment, %s: %s\n"),
			"shmctl", strerror(errno));
	context->shm_size = 0;
}

/*
 * Ensure the per-context shared segment is at least `needed` bytes,
 * creating and attaching one if none exists. Growth doubles the previous
 * size so a typical session converges in two or three steps. Returns the
 * usable size or zero on failure, in which case use_shared_memory is
 * cleared so the caller falls back to plain XPutImage.
 */
static size_t ensureSharedSegment(RContext *context, size_t needed)
{
	size_t alloc;

	if (context->shm_size >= needed)
		return context->shm_size;

	alloc = context->shm_size ? context->shm_size : 64 * 1024;
	while (alloc < needed)
		alloc *= 2;

	releaseSharedSegment(context);

	context->shm_seg.readOnly = False;
	context->shm_seg.shmid = shmget(IPC_PRIVATE, alloc, IPC_CREAT | 0600);
	if (context->shm_seg.shmid < 0) {
		fprintf(stderr, _("wrlib: could not allocate shared memory segment, %s: %s\n"),
			"shmget", strerror(errno));
		context->attribs->use_shared_memory = 0;
		return 0;
	}
	context->shm_seg.shmaddr = shmat(context->shm_seg.shmid, NULL, 0);
	if (context->shm_seg.shmaddr == (void *)-1) {
		fprintf(stderr, _("wrlib: could not allocate shared memory segment, %s: %s\n"),
			"shmat", strerror(errno));
		shmctl(context->shm_seg.shmid, IPC_RMID, NULL);
		context->attribs->use_shared_memory = 0;
		return 0;
	}

	shmError = 0;
	XSync(context->dpy, False);
	oldErrorHandler = XSetErrorHandler(errorHandler);
	XShmAttach(context->dpy, &context->shm_seg);
	XSync(context->dpy, False);
	XSetErrorHandler(oldErrorHandler);

	if (shmError) {
		fprintf(stderr, _("wrlib: could not attach shared memory segment to XImage\n"));
		shmdt(context->shm_seg.shmaddr);
		shmctl(context->shm_seg.shmid, IPC_RMID, NULL);
		context->attribs->use_shared_memory = 0;
		return 0;
	}

	context->shm_size = alloc;
	return alloc;
}

void RReleaseSharedSegment(RContext *context)
{
	releaseSharedSegment(context);
}

#endif

RXImage *RCreateXImage(RContext * context, int depth, unsigned width, unsigned height)
{
	RXImage *rximg;
	Visual *visual = context->visual;

	rximg = malloc(sizeof(RXImage));
	if (!rximg) {
		RErrorCode = RERR_NOMEMORY;
		return NULL;
	}
#ifndef USE_XSHM
	rximg->image = XCreateImage(context->dpy, visual, depth, ZPixmap, 0, NULL, width, height, 8, 0);
	if (!rximg->image) {
		free(rximg);
		RErrorCode = RERR_XERROR;
		return NULL;
	}
	rximg->image->data = malloc(rximg->image->bytes_per_line * height);
	if (!rximg->image->data) {
		XDestroyImage(rximg->image);
		free(rximg);
		RErrorCode = RERR_NOMEMORY;
		return NULL;
	}
#else				/* USE_XSHM */
	if (!context->attribs->use_shared_memory) {
 retry_without_shm:

		context->attribs->use_shared_memory = 0;
		rximg->is_shared = 0;
		rximg->image = XCreateImage(context->dpy, visual, depth, ZPixmap, 0, NULL, width, height, 8, 0);
		if (!rximg->image) {
			free(rximg);
			RErrorCode = RERR_XERROR;
			return NULL;
		}
		rximg->image->data = malloc(rximg->image->bytes_per_line * height);
		if (!rximg->image->data) {
			XDestroyImage(rximg->image);
			free(rximg);
			RErrorCode = RERR_NOMEMORY;
			return NULL;
		}
	} else {
		rximg->image = XShmCreateImage(context->dpy, visual, depth,
					       ZPixmap, NULL, &rximg->info, width, height);

		if (!ensureSharedSegment(context,
					 (size_t)rximg->image->bytes_per_line * height)) {
			XDestroyImage(rximg->image);
			goto retry_without_shm;
		}

		rximg->is_shared = 1;
		rximg->info = context->shm_seg;
		rximg->image->data = rximg->info.shmaddr;
	}
#endif				/* USE_XSHM */

	return rximg;
}

void RDestroyXImage(RContext * context, RXImage * rximage)
{
#ifndef USE_XSHM
	/* Argument is not used in this case, tell the compiler it is ok */
	(void) context;

	XDestroyImage(rximage->image);
#else				/* USE_XSHM */
	if (rximage->is_shared) {
		/* The data pointer belongs to the persistent context segment,
		 * clear it so XDestroyImage frees only the XImage header.
		 * The XSync is the reuse barrier, the server must have read
		 * the queued ShmPutImage or shm-pixmap CopyArea out of this
		 * segment before the next RCreateXImage caller overwrites it. */
		rximage->image->data = NULL;
		XSync(context->dpy, False);
	}
	XDestroyImage(rximage->image);
#endif
	free(rximage);
}

static unsigned getDepth(Display * dpy, Drawable d)
{
	Window w;
	int foo;
	unsigned bar;
	unsigned depth;

	XGetGeometry(dpy, d, &w, &foo, &foo, &bar, &bar, &bar, &depth);

	return depth;
}

RXImage *RGetXImage(RContext * context, Drawable d, int x, int y, unsigned width, unsigned height)
{
	RXImage *ximg = NULL;

#ifdef USE_XSHM
	if (context->attribs->use_shared_memory && 0) {
		ximg = RCreateXImage(context, getDepth(context->dpy, d), width, height);

		if (ximg && !ximg->is_shared) {
			RDestroyXImage(context, ximg);
			ximg = NULL;
		}
		if (ximg) {
			XShmGetImage(context->dpy, d, ximg->image, x, y, AllPlanes);
		}
	}
	if (!ximg) {
		ximg = malloc(sizeof(RXImage));
		if (!ximg) {
			RErrorCode = RERR_NOMEMORY;
			return NULL;
		}
		ximg->is_shared = 0;
		ximg->image = XGetImage(context->dpy, d, x, y, width, height, AllPlanes, ZPixmap);
	}
#else				/* !USE_XSHM */
	ximg = malloc(sizeof(RXImage));
	if (!ximg) {
		RErrorCode = RERR_NOMEMORY;
		return NULL;
	}

	ximg->image = XGetImage(context->dpy, d, x, y, width, height, AllPlanes, ZPixmap);
#endif				/* !USE_XSHM */

	if (ximg->image == NULL) {
		free(ximg);
		return NULL;
	}

	return ximg;
}

void
RPutXImage(RContext * context, Drawable d, GC gc, RXImage * ximage, int src_x,
	   int src_y, int dest_x, int dest_y, unsigned int width, unsigned int height)
{
#ifndef USE_XSHM
	XPutImage(context->dpy, d, gc, ximage->image, src_x, src_y, dest_x, dest_y, width, height);
#else
	if (ximage->is_shared) {
		XShmPutImage(context->dpy, d, gc, ximage->image, src_x, src_y,
			     dest_x, dest_y, width, height, False);
	} else {
		XPutImage(context->dpy, d, gc, ximage->image, src_x, src_y, dest_x, dest_y, width, height);
	}
	XFlush(context->dpy);
#endif				/* USE_XSHM */
}

#ifdef USE_XSHM
Pixmap R_CreateXImageMappedPixmap(RContext * context, RXImage * rximage)
{
	Pixmap pix;

	pix = XShmCreatePixmap(context->dpy, context->drawable,
			       rximage->image->data, &rximage->info,
			       rximage->image->width, rximage->image->height, rximage->image->depth);

	return pix;
}

#endif				/* USE_XSHM */
