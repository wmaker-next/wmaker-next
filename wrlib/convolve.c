/*
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "wraster.h"
#include "wr_i18n.h"


/*
 *----------------------------------------------------------------------
 * RBlurImage--
 * 	Apply 3x3 1 1 1 low pass, convolution mask to image.
 *                1 2 1
 *                1 1 1 /10
 *----------------------------------------------------------------------
 */
int RBlurImage(RImage * image)
{
	register int x, y;
	register int tmp;
	unsigned char *ptr, *nptr;
	unsigned char *pptr = NULL, *tmpp;
	int ch = image->format == RRGBAFormat ? 4 : 3;

	pptr = malloc(image->width * ch);
	if (!pptr) {
		RErrorCode = RERR_NOMEMORY;
		return 0;
	}
#define MASK(prev, cur, next, ch)\
    (*(prev-ch) + *prev + *(prev+ch)\
    +*(cur-ch) + 2 * *cur + *(cur+ch)\
    +*(next-ch) + *next + *(next+ch)) / 10

	memcpy(pptr, image->data, image->width * ch);

	ptr = image->data;
	nptr = ptr + image->width * ch;
	tmpp = pptr;

	if (ch == 3) {
		ptr += 3;
		nptr += 3;
		pptr += 3;

		for (y = 1; y < image->height - 1; y++) {

			for (x = 1; x < image->width - 1; x++) {
				tmp = *ptr;
				*ptr = MASK(pptr, ptr, nptr, 3);
				*pptr = tmp;
				ptr++;
				nptr++;
				pptr++;

				tmp = *ptr;
				*ptr = MASK(pptr, ptr, nptr, 3);
				*pptr = tmp;
				ptr++;
				nptr++;
				pptr++;

				tmp = *ptr;
				*ptr = MASK(pptr, ptr, nptr, 3);
				*pptr = tmp;
				ptr++;
				nptr++;
				pptr++;
			}
			pptr = tmpp;
			ptr += 6;
			nptr += 6;
			pptr += 6;
		}
	} else {
		ptr += 4;
		nptr += 4;
		pptr += 4;

		for (y = 1; y < image->height - 1; y++) {
			for (x = 1; x < image->width - 1; x++) {
				tmp = *ptr;
				*ptr = MASK(pptr, ptr, nptr, 4);
				*pptr = tmp;
				ptr++;
				nptr++;
				pptr++;

				tmp = *ptr;
				*ptr = MASK(pptr, ptr, nptr, 4);
				*pptr = tmp;
				ptr++;
				nptr++;
				pptr++;

				tmp = *ptr;
				*ptr = MASK(pptr, ptr, nptr, 4);
				*pptr = tmp;
				ptr++;
				nptr++;
				pptr++;

				tmp = *ptr;
				*ptr = MASK(pptr, ptr, nptr, 4);
				*pptr = tmp;
				ptr++;
				nptr++;
				pptr++;
			}
			pptr = tmpp;
			ptr += 8;
			nptr += 8;
			pptr += 8;
		}
	}

	free(tmpp);

	return 1;
}

/*
 *----------------------------------------------------------------------
 * RStackBlurImage--
 *	In-place approximate Gaussian blur with a parameterised radius.
 *
 *	This is Mario Klingemann's stack blur: a horizontal pass followed
 *	by a vertical pass, each maintaining a sliding triangular-weighted
 *	sum so the per-pixel cost is constant and independent of the
 *	radius. The result is visually close to a true Gaussian and far
 *	cheaper than iterating RBlurImage() for large radii.
 *
 *	radius is silently clamped to [1, 255]. radius 0 is a no-op.
 *	Both RRGBFormat and RRGBAFormat are handled, the alpha channel is
 *	blurred along with the colour channels.
 *
 *	Returns 1 on success, sets RErrorCode and returns 0 on
 *	allocation failure.
 *----------------------------------------------------------------------
 */
int RStackBlurImage(RImage *image, unsigned radius)
{
	unsigned char *src, *tmp, *out;
	int *stack, *vmin;
	int insum[4], outsum[4], sum[4];
	int w, h, ch, div, divsum, r1;
	int x, y, c, i, p, sp, ss, rbs, stride;

	if (radius < 1)
		return 1;
	if (radius > 255)
		radius = 255;

	w = image->width;
	h = image->height;
	ch = (image->format == RRGBAFormat) ? 4 : 3;
	stride = w * ch;

	if (w < 2 || h < 2)
		return 1;

	div = 2 * (int)radius + 1;
	r1 = (int)radius + 1;
	divsum = r1 * r1;

	tmp = malloc((size_t)stride * h);
	stack = malloc((size_t)div * ch * sizeof(int));
	vmin = malloc((size_t)((w > h) ? w : h) * sizeof(int));

	if (!tmp || !stack || !vmin) {
		free(tmp);
		free(stack);
		free(vmin);
		RErrorCode = RERR_NOMEMORY;
		return 0;
	}

	/* horizontal pass: image->data -> tmp */
	for (x = 0; x < w; x++)
		vmin[x] = ((x + r1 < w) ? x + r1 : w - 1) * ch;

	src = image->data;
	out = tmp;
	for (y = 0; y < h; y++) {
		for (c = 0; c < ch; c++)
			insum[c] = outsum[c] = sum[c] = 0;

		for (i = -(int)radius; i <= (int)radius; i++) {
			p = ((i < 0) ? 0 : ((i > w - 1) ? w - 1 : i)) * ch;
			sp = (i + (int)radius) * ch;
			rbs = r1 - abs(i);
			for (c = 0; c < ch; c++) {
				stack[sp + c] = src[p + c];
				sum[c] += src[p + c] * rbs;
				if (i > 0)
					insum[c] += src[p + c];
				else
					outsum[c] += src[p + c];
			}
		}

		sp = (int)radius;
		for (x = 0; x < w; x++) {
			for (c = 0; c < ch; c++) {
				out[x * ch + c] = (unsigned char)(sum[c] / divsum);
				sum[c] -= outsum[c];
			}

			ss = ((sp + r1) % div) * ch;
			for (c = 0; c < ch; c++)
				outsum[c] -= stack[ss + c];

			p = vmin[x];
			for (c = 0; c < ch; c++) {
				stack[ss + c] = src[p + c];
				insum[c] += src[p + c];
				sum[c] += insum[c];
			}

			sp = (sp + 1) % div;
			ss = sp * ch;
			for (c = 0; c < ch; c++) {
				outsum[c] += stack[ss + c];
				insum[c] -= stack[ss + c];
			}
		}

		src += stride;
		out += stride;
	}

	/* vertical pass: tmp -> image->data */
	for (y = 0; y < h; y++)
		vmin[y] = ((y + r1 < h) ? y + r1 : h - 1) * stride;

	for (x = 0; x < w; x++) {
		src = tmp + x * ch;
		out = image->data + x * ch;

		for (c = 0; c < ch; c++)
			insum[c] = outsum[c] = sum[c] = 0;

		for (i = -(int)radius; i <= (int)radius; i++) {
			p = ((i < 0) ? 0 : ((i > h - 1) ? h - 1 : i)) * stride;
			sp = (i + (int)radius) * ch;
			rbs = r1 - abs(i);
			for (c = 0; c < ch; c++) {
				stack[sp + c] = src[p + c];
				sum[c] += src[p + c] * rbs;
				if (i > 0)
					insum[c] += src[p + c];
				else
					outsum[c] += src[p + c];
			}
		}

		sp = (int)radius;
		for (y = 0; y < h; y++) {
			for (c = 0; c < ch; c++) {
				out[y * stride + c] = (unsigned char)(sum[c] / divsum);
				sum[c] -= outsum[c];
			}

			ss = ((sp + r1) % div) * ch;
			for (c = 0; c < ch; c++)
				outsum[c] -= stack[ss + c];

			p = vmin[y];
			for (c = 0; c < ch; c++) {
				stack[ss + c] = src[p + c];
				insum[c] += src[p + c];
				sum[c] += insum[c];
			}

			sp = (sp + 1) % div;
			ss = sp * ch;
			for (c = 0; c < ch; c++) {
				outsum[c] += stack[ss + c];
				insum[c] -= stack[ss + c];
			}
		}
	}

	free(tmp);
	free(stack);
	free(vmin);

	return 1;
}

