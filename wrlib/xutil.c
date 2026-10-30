/* xutil.c - put/get RImages to/from the X server
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <xcb/xcb.h>
#include <xcb/shm.h>
#include <xcb/xcb_image.h>

#ifdef USE_XSHM
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#endif

#include "wraster.h"
#include "xutil.h"
#include "wr_i18n.h"


#ifdef USE_XSHM

static void wrxcb_sync(xcb_connection_t *xc)
{
	free(xcb_get_input_focus_reply(xc, xcb_get_input_focus(xc), NULL));
}

/*
 * Anonymous shared mapping for the AttachFd path. memfd_create on Linux
 * gives a file with no filesystem footprint that the server can mmap from
 * the passed descriptor, FreeBSD's SHM_ANON and OpenBSD's shm_mkstemp
 * cover the BSDs, and the SysV shmget path stays as the portable fallback
 * (NetBSD base Xorg, remote display, MIT-SHM < 1.2).
 */
static int alloc_shm_fd(size_t size)
{
	int fd = -1;

#if defined(HAVE_MEMFD_CREATE)
	fd = memfd_create("wraster-shm", MFD_CLOEXEC);
#elif defined(HAVE_DECL_SHM_ANON) && HAVE_DECL_SHM_ANON
	fd = shm_open(SHM_ANON, O_RDWR | O_CLOEXEC, 0600);
#elif defined(HAVE_SHM_MKSTEMP)
	char tmpl[] = "/wraster-shm-XXXXXXXX";
	fd = shm_mkstemp(tmpl);
	if (fd >= 0)
		shm_unlink(tmpl);
#endif
	if (fd < 0)
		return -1;
	if (ftruncate(fd, size) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static void releaseSharedSegment(RContext *context)
{
	if (context->shm_size == 0)
		return;

	xcb_shm_detach(context->xc, context->shm_xseg);
	if (context->shm_id >= 0) {
		/* SysV path: the detach must reach the server before we
		 * drop our own attachment and IPC_RMID the segment,
		 * otherwise nattch can hit zero and the kernel frees it
		 * before the server reads from it. */
		wrxcb_sync(context->xc);
		if (shmdt(context->shm_addr) < 0)
			fprintf(stderr, _("wrlib: error occurred while releasing shared segment, %s: %s\n"),
				"shmdt", strerror(errno));
		if (shmctl(context->shm_id, IPC_RMID, NULL) < 0)
			fprintf(stderr, _("wrlib: error occurred while releasing shared segment, %s: %s\n"),
				"shmctl", strerror(errno));
	} else {
		/* fd path: server holds its own mmap of the same inode so
		 * unmapping here cannot strand it, no ordering barrier */
		munmap(context->shm_addr, context->shm_size);
	}
	context->shm_xseg = 0;
	context->shm_addr = NULL;
	context->shm_size = 0;
	context->shm_used = 0;
}

/*
 * Ensure the per-context shared segment is at least `needed` bytes,
 * creating and attaching one if none exists. Growth doubles the previous
 * size so a typical session converges in two or three steps. Returns the
 * usable size or zero on failure, in which case use_shared_memory is
 * cleared so the caller falls back to plain xcb_image_put.
 */
static size_t ensureSharedSegment(RContext *context, size_t needed)
{
	xcb_generic_error_t *err;
	xcb_void_cookie_t ck;
	size_t alloc;
	int fd;

	if (context->shm_size >= needed)
		return context->shm_size;

	alloc = context->shm_size ? context->shm_size : 256 * 1024;
	while (alloc < needed)
		alloc *= 2;

	releaseSharedSegment(context);

	context->shm_id = -1;
	context->shm_xseg = xcb_generate_id(context->xc);

	fd = alloc_shm_fd(alloc);
	if (fd >= 0) {
		context->shm_addr = mmap(NULL, alloc, PROT_READ | PROT_WRITE,
					 MAP_SHARED, fd, 0);
		if (context->shm_addr == MAP_FAILED) {
			close(fd);
			context->shm_addr = NULL;
			goto sysv;
		}
		/* xcb_shm_attach_fd consumes fd (passes it to the server via
		 * SCM_RIGHTS and closes the local copy), so no close here */
		ck = xcb_shm_attach_fd_checked(context->xc, context->shm_xseg, fd, 0);
		err = xcb_request_check(context->xc, ck);
		if (err) {
			/* MIT-SHM < 1.2 (no AttachFd) or a server that
			 * refused the fd, fall through to the SysV path.
			 * The failed request consumed shm_xseg so take a
			 * fresh one for the retry. */
			free(err);
			munmap(context->shm_addr, alloc);
			context->shm_addr = NULL;
			context->shm_xseg = xcb_generate_id(context->xc);
			goto sysv;
		}
		context->shm_size = alloc;
		context->shm_used = 0;
		return alloc;
	}

sysv:
	context->shm_id = shmget(IPC_PRIVATE, alloc, IPC_CREAT | 0600);
	if (context->shm_id < 0) {
		fprintf(stderr, _("wrlib: could not allocate shared memory segment, %s: %s\n"),
			"shmget", strerror(errno));
		context->attribs->use_shared_memory = 0;
		return 0;
	}
	context->shm_addr = shmat(context->shm_id, NULL, 0);
	if (context->shm_addr == (void *)-1) {
		fprintf(stderr, _("wrlib: could not allocate shared memory segment, %s: %s\n"),
			"shmat", strerror(errno));
		shmctl(context->shm_id, IPC_RMID, NULL);
		context->shm_addr = NULL;
		context->attribs->use_shared_memory = 0;
		return 0;
	}

	ck = xcb_shm_attach_checked(context->xc, context->shm_xseg, context->shm_id, 0);
	err = xcb_request_check(context->xc, ck);
	if (err) {
		fprintf(stderr, _("wrlib: could not attach shared memory segment to server (error %d)\n"),
			err->error_code);
		free(err);
		shmdt(context->shm_addr);
		shmctl(context->shm_id, IPC_RMID, NULL);
		context->shm_addr = NULL;
		context->shm_xseg = 0;
		context->attribs->use_shared_memory = 0;
		return 0;
	}

	context->shm_size = alloc;
	context->shm_used = 0;
	return alloc;
}

#endif

void RReleaseSharedSegment(RContext *context)
{
#ifdef USE_XSHM
	releaseSharedSegment(context);
#else
	(void)context;
#endif
}

RXImage *RCreateXImage(RContext * context, int depth, unsigned width, unsigned height)
{
	RXImage *rximg;
#ifdef USE_XSHM
	size_t need;
#endif

	rximg = calloc(1, sizeof(RXImage));
	if (!rximg) {
		RErrorCode = RERR_NOMEMORY;
		return NULL;
	}
	rximg->ximg = xcb_image_create_native(context->xc, width, height,
					      XCB_IMAGE_FORMAT_Z_PIXMAP,
					      depth, NULL, (uint32_t)~0, NULL);
	if (!rximg->ximg) {
		free(rximg);
		RErrorCode = RERR_XERROR;
		return NULL;
	}

#ifdef USE_XSHM
	if (context->attribs->use_shared_memory) {
		need = rximg->ximg->size;
		need = (need + 7u) & ~(size_t)7u;

		if (!ensureSharedSegment(context, need))
			goto without_shm;

		if (context->shm_used + need > context->shm_size) {
			/* Bump allocator wrapped, the server must have read
			 * every queued ShmPutImage out of the earlier slots
			 * before this caller overwrites offset 0. The attach
			 * bracket in ensureSharedSegment is its own
			 * round-trip, this is the only per-image one left
			 * and it fires once every shm_size bytes. */
			wrxcb_sync(context->xc);
			context->shm_used = 0;
		}

		rximg->is_shared = 1;
		rximg->shm_offset = context->shm_used;
		rximg->ximg->data = context->shm_addr + context->shm_used;
		context->shm_used += need;
		return rximg;
	}
without_shm:
#endif
	rximg->ximg->data = malloc(rximg->ximg->size);
	if (!rximg->ximg->data) {
		xcb_image_destroy(rximg->ximg);
		free(rximg);
		RErrorCode = RERR_NOMEMORY;
		return NULL;
	}
	rximg->ximg->base = rximg->ximg->data;
	return rximg;
}

void RDestroyXImage(RContext * context, RXImage * rximage)
{
	(void) context;
	if (rximage->ximg) {
		if (rximage->is_shared) {
			/* The data pointer is an offset into the persistent
			 * context segment, clear base so xcb_image_destroy
			 * frees only the header. The reuse barrier is the
			 * wrap sync in RCreateXImage, not here. */
			rximage->ximg->base = NULL;
			rximage->ximg->data = NULL;
		}
		xcb_image_destroy(rximage->ximg);
	}
	free(rximage);
}

RXImage *RGetXImage(RContext * context, xcb_drawable_t d, int x, int y, unsigned int width, unsigned int height)
{
	RXImage *ximg;

	ximg = calloc(1, sizeof(RXImage));
	if (!ximg) {
		RErrorCode = RERR_NOMEMORY;
		return NULL;
	}
	ximg->ximg = xcb_image_get(context->xc, d, x, y, width, height,
				   ~(uint32_t)0, XCB_IMAGE_FORMAT_Z_PIXMAP);
	if (ximg->ximg == NULL) {
		free(ximg);
		return NULL;
	}
	return ximg;
}

void r_put_ximage(RContext *context, xcb_drawable_t d, xcb_gcontext_t gc, RXImage *rximg,
		  int src_x, int src_y, int dest_x, int dest_y,
		  unsigned int width, unsigned int height)
{
	xcb_image_t *xi = rximg->ximg;

#ifdef USE_XSHM
	if (rximg->is_shared) {
		xcb_shm_put_image(context->xc, d, gc, xi->width, xi->height,
				  src_x, src_y, width, height, dest_x, dest_y,
				  xi->depth, xi->format, 0,
				  context->shm_xseg, rximg->shm_offset);
		xcb_flush(context->xc);
		return;
	}
#endif
	if (src_x == 0 && src_y == 0 && width == xi->width && height == xi->height) {
		xcb_image_put(context->xc, d, gc, xi, dest_x, dest_y, 0);
	} else {
		xcb_image_t *sub;

		/* xcb_image_put has no source-rectangle parameters, the only
		 * non-full-image caller in the tree is the SHM branch so this
		 * copy is the cold path */
		sub = xcb_image_subimage(xi, src_x, src_y, width, height, NULL, 0, NULL);
		if (sub) {
			xcb_image_put(context->xc, d, gc, sub, dest_x, dest_y, 0);
			xcb_image_destroy(sub);
		}
	}
	xcb_flush(context->xc);
}

void
RPutXImage(RContext * context, xcb_drawable_t d, xcb_gcontext_t gc, RXImage * ximage, int src_x,
	   int src_y, int dest_x, int dest_y, unsigned int width, unsigned int height)
{
	r_put_ximage(context, d, gc, ximage,
		     src_x, src_y, dest_x, dest_y, width, height);
}
