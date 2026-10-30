/*
 *  check_xside.c
 *
 *  Raster graphics library
 *
 *  Copyright (c) 2026 Window Maker Team
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

/*
 * check_xside.c - round-trip RImage -> Pixmap -> RImage against a live
 * X server. RConvertImage packs into the visual's native ZPixmap and
 * uploads it (SHM when available), RCreateImageFromDrawable reads the
 * same pixmap back via xcb_image_get and reverse-packs it, so a pixel
 * mismatch in either direction is caught here. Exits 77 (automake SKIP)
 * when no display is reachable so make check stays X-free.
 *
 * Covered: RCreateContext, RConvertImage, RConvertImageMask,
 * RCreateImageFromDrawable, RGetXImage + RCreateImageFromRXImage.
 */

#include <stdio.h>
#include <stdlib.h>

#include "wraster.h"

#include "check_common.h"

static void check_pixel(RImage *img, int x, int y, int er, int eg, int eb, const char *what)
{
	RColor c;

	if (!RGetPixel(img, x, y, &c)) {
		fprintf(stderr, "FAIL %s: RGetPixel(%d,%d) out of range\n", what, x, y);
		check_failures++;
		return;
	}
	if (c.red != er || c.green != eg || c.blue != eb) {
		fprintf(stderr, "FAIL %s: (%d,%d) got %02x/%02x/%02x want %02x/%02x/%02x\n",
			what, x, y, c.red, c.green, c.blue,
			(unsigned int)er, (unsigned int)eg, (unsigned int)eb);
		check_failures++;
	}
}

int main(void)
{
	xcb_connection_t *xc;
	int sn;
	RContext *ctx;
	RImage *src, *back, *back2;
	RXImage *rx;
	xcb_pixmap_t pix, mask;
	RColor red = { 0xff, 0x00, 0x00, 0xff };
	RColor grn = { 0x00, 0xff, 0x00, 0xff };
	RColor blu = { 0x00, 0x00, 0xff, 0xff };
	RColor wht = { 0xff, 0xff, 0xff, 0xff };
	unsigned char *p;
	int x, y;
	RContext *xctx;

	xc = xcb_connect(NULL, &sn);
	if (xcb_connection_has_error(xc)) {
		fprintf(stderr, "SKIP: no display\n");
		return 77;
	}
	ctx = RCreateContextXCB(xc, sn, NULL);
	if (!ctx) {
		fprintf(stderr, "SKIP: RCreateContext failed (%s)\n", RMessageForError(RErrorCode));
		return 77;
	}

	printf("# context: depth=%d bpp visual=0x%x shm=%s\n",
	       ctx->depth, ctx->xvisual->visual_id,
	       ctx->attribs->use_shared_memory ? "yes" : "no");

	/* 17x13 so the stride padding is exercised, four corners get
	 * distinct primaries and the centre a mid-grey */
	src = RCreateImage(17, 13, 1);
	RFillImage(src, &wht);
	for (y = 0; y < 6; y++)
		for (x = 0; x < 8; x++)
			RPutPixel(src, x, y, &red);
	for (y = 0; y < 6; y++)
		for (x = 9; x < 17; x++)
			RPutPixel(src, x, y, &grn);
	for (y = 7; y < 13; y++)
		for (x = 0; x < 8; x++)
			RPutPixel(src, x, y, &blu);
	/* RPutPixel blends when alpha != 255, write the alpha-0 sample raw */
	p = src->data + (6 * 17 + 8) * 4;
	p[0] = 0x12; p[1] = 0x34; p[2] = 0x56; p[3] = 0x00;

	if (!RConvertImage(ctx, src, &pix)) {
		fprintf(stderr, "FAIL: RConvertImage (%s)\n", RMessageForError(RErrorCode));
		return 1;
	}
	xcb_flush(xc);

	back = RCreateImageFromDrawable(ctx, pix, XCB_NONE);
	if (!back) {
		fprintf(stderr, "FAIL: RCreateImageFromDrawable (%s)\n", RMessageForError(RErrorCode));
		return 1;
	}
	if (back->width != 17 || back->height != 13) {
		fprintf(stderr, "FAIL: round-trip size %dx%d\n", back->width, back->height);
		check_failures++;
	}
	check_pixel(back, 0, 0, 0xff, 0x00, 0x00, "convert/tl");
	check_pixel(back, 16, 0, 0x00, 0xff, 0x00, "convert/tr");
	check_pixel(back, 0, 12, 0x00, 0x00, 0xff, "convert/bl");
	check_pixel(back, 16, 12, 0xff, 0xff, 0xff, "convert/br");
	check_pixel(back, 8, 6, 0x12, 0x34, 0x56, "convert/mid");

	/* RGetXImage on a sub-rectangle then unpack via the RXImage path,
	 * mirrors the workspace.c name-fade reader */
	rx = RGetXImage(ctx, pix, 9, 0, 8, 6);
	if (!rx) {
		fprintf(stderr, "FAIL: RGetXImage\n");
		return 1;
	}
	back2 = RCreateImageFromRXImage(ctx, rx, NULL);
	RDestroyXImage(ctx, rx);
	if (!back2) {
		fprintf(stderr, "FAIL: RCreateImageFromRXImage\n");
		return 1;
	}
	check_pixel(back2, 0, 0, 0x00, 0xff, 0x00, "subrect/tl");
	check_pixel(back2, 7, 5, 0x00, 0xff, 0x00, "subrect/br");
	RReleaseImage(back2);

	/* mask: the alpha-0 centre pixel must read 0, every opaque
	 * corner must read 1 */
	if (!RConvertImageMask(ctx, src, &pix, &mask, 128)) {
		fprintf(stderr, "FAIL: RConvertImageMask (%s)\n", RMessageForError(RErrorCode));
		return 1;
	}
	xcb_flush(xc);
	back2 = RCreateImageFromDrawable(ctx, pix, mask);
	if (!back2 || back2->format != RRGBAFormat) {
		fprintf(stderr, "FAIL: RCreateImageFromDrawable+mask\n");
		return 1;
	}
	if (back2->data[(6 * 17 + 8) * 4 + 3] != 0) {
		fprintf(stderr, "FAIL mask: centre alpha %u != 0\n",
			back2->data[(6 * 17 + 8) * 4 + 3]);
		check_failures++;
	}
	if (back2->data[3] != 0xff) {
		fprintf(stderr, "FAIL mask: corner alpha %u != 0xff\n", back2->data[3]);
		check_failures++;
	}

	RReleaseImage(back);
	RReleaseImage(back2);
	RDestroyContext(ctx);

	/* second RCreateContextXCB on the same connection still round-trips */
	xctx = RCreateContextXCB(xc, sn, NULL);
	if (!xctx) {
		fprintf(stderr, "FAIL: RCreateContextXCB\n");
		return 1;
	}
	RFillImage(src, &grn);
	if (!RConvertImage(xctx, src, &pix)) {
		fprintf(stderr, "FAIL: xcb-ctx RConvertImage\n");
		check_failures++;
	} else {
		xcb_flush(xc);
		back = RCreateImageFromDrawable(xctx, pix, XCB_NONE);
		if (back) {
			check_pixel(back, 8, 6, 0x00, 0xff, 0x00, "xcb-ctx");
			RReleaseImage(back);
		} else {
			check_failures++;
		}
	}
	RReleaseImage(src);
	RDestroyContext(xctx);

	xcb_disconnect(xc);

	return check_report("check_xside");
}
