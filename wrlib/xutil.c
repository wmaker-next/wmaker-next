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
	context->shm_used = 0;
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

	alloc = context->shm_size ? context->shm_size : 256 * 1024;
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
	context->shm_used = 0;
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

	rximg = calloc(1, sizeof(RXImage));
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
		size_t need;

		rximg->image = XShmCreateImage(context->dpy, visual, depth,
					       ZPixmap, NULL, &rximg->info, width, height);

		need = (size_t)rximg->image->bytes_per_line * height;
		need = (need + 7u) & ~(size_t)7u;

		if (!ensureSharedSegment(context, need)) {
			XDestroyImage(rximg->image);
			goto retry_without_shm;
		}

		if (context->shm_used + need > context->shm_size) {
			/* Bump allocator wrapped, the server must have read
			 * every queued ShmPutImage out of the earlier slots
			 * before this caller overwrites offset 0. The attach
			 * and release brackets in ensureSharedSegment keep
			 * their own XSyncs, this is the only per-image one
			 * left and it fires once every shm_size bytes. */
			XSync(context->dpy, False);
			context->shm_used = 0;
		}

		rximg->is_shared = 1;
		rximg->info = context->shm_seg;
		rximg->image->data = context->shm_seg.shmaddr + context->shm_used;
		context->shm_used += need;
	}
#endif				/* USE_XSHM */

	return rximg;
}

void RDestroyXImage(RContext * context, RXImage * rximage)
{
	(void) context;
#ifndef USE_XSHM
	XDestroyImage(rximage->image);
#else				/* USE_XSHM */
	if (rximage->is_shared) {
		/* The data pointer is an offset into the persistent context
		 * segment, clear it so XDestroyImage frees only the XImage
		 * header. The reuse barrier is the wrap XSync in
		 * RCreateXImage, not here. */
		rximage->image->data = NULL;
	}
	XDestroyImage(rximage->image);
#endif
	free(rximage);
}

RXImage *RGetXImage(RContext * context, Drawable d, int x, int y, unsigned width, unsigned height)
{
	RXImage *ximg;

	ximg = calloc(1, sizeof(RXImage));
	if (!ximg) {
		RErrorCode = RERR_NOMEMORY;
		return NULL;
	}
#ifdef USE_XSHM
	ximg->is_shared = 0;
#endif
	ximg->image = XGetImage(context->dpy, d, x, y, width, height, AllPlanes, ZPixmap);
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

