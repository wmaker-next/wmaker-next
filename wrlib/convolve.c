/*
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

#ifdef __SSE2__
#include <emmintrin.h>
#endif

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
#define MASK(prev, cur, next, ch)\
    (*(prev-ch) + *prev + *(prev+ch)\
    +*(cur-ch) + 2 * *cur + *(cur+ch)\
    +*(next-ch) + *next + *(next+ch)) / 10

int RBlurImage(RImage * image)
{
	register int x, y;
	unsigned char *ptr, *nptr;
	unsigned char *pptr, *cptr;
	unsigned char *pbuf, *cbuf, *swp;
	int ch = image->format == RRGBAFormat ? 4 : 3;
	int stride = image->width * ch;

	/* only the interior is convolved, with less than three rows
	 * there is no interior */
	if (image->height < 3)
		return 1;

	pbuf = malloc(stride);
	cbuf = malloc(stride);
	if (!pbuf || !cbuf) {
		free(pbuf);
		free(cbuf);
		RErrorCode = RERR_NOMEMORY;
		return 0;
	}

	memcpy(pbuf, image->data, stride);
	memcpy(cbuf, image->data + stride, stride);

	for (y = 1; y < image->height - 1; y++) {
		/* pbuf holds the unblurred previous row, cbuf the
		 * unblurred current row, nptr is the next row which is
		 * still untouched in the image */
		ptr = image->data + (size_t)y * stride;
		nptr = ptr + stride;
		pptr = pbuf;
		cptr = cbuf;

		if (ch == 3) {
			ptr += 3;
			nptr += 3;
			pptr += 3;
			cptr += 3;

			for (x = 1; x < image->width - 1; x++) {
				*ptr = MASK(pptr, cptr, nptr, 3);
				ptr++;
				nptr++;
				pptr++;
				cptr++;

				*ptr = MASK(pptr, cptr, nptr, 3);
				ptr++;
				nptr++;
				pptr++;
				cptr++;

				*ptr = MASK(pptr, cptr, nptr, 3);
				ptr++;
				nptr++;
				pptr++;
				cptr++;
			}
		} else {
			ptr += 4;
			nptr += 4;
			pptr += 4;
			cptr += 4;

			for (x = 1; x < image->width - 1; x++) {
				*ptr = MASK(pptr, cptr, nptr, 4);
				ptr++;
				nptr++;
				pptr++;
				cptr++;

				*ptr = MASK(pptr, cptr, nptr, 4);
				ptr++;
				nptr++;
				pptr++;
				cptr++;

				*ptr = MASK(pptr, cptr, nptr, 4);
				ptr++;
				nptr++;
				pptr++;
				cptr++;

				*ptr = MASK(pptr, cptr, nptr, 4);
				ptr++;
				nptr++;
				pptr++;
				cptr++;
			}
		}

		/* the row just blurred is the previous row of the next
		 * pass, so refill its old buffer with the next row while
		 * it is still unmodified */
		swp = pbuf;
		pbuf = cbuf;
		cbuf = swp;
		memcpy(cbuf, image->data + (size_t)(y + 1) * stride, stride);
	}

	free(pbuf);
	free(cbuf);

	return 1;
}

#ifdef __SSE2__
/* one pass of the stack blur along an arbitrary stride, four channels
 * processed in lockstep as 32-bit lanes. RGB images run with the
 * fourth lane carrying whatever byte follows the pixel (RCreateImage
 * over-allocates by four bytes so the trailing read is safe) and the
 * store truncates to ch bytes. The integer divide is replaced by a
 * float divide and truncate, both operands fit a float mantissa
 * exactly so for a flat input where sum == k*divsum the quotient is
 * exactly k and the test_blur_flat fixed-point property holds. */
static int stack_blur_pass_sse2(const unsigned char *in, unsigned char *out,
				int len, int lines, int pstride, int lstride,
				int ch, int radius)
{
	const __m128i zero = _mm_setzero_si128();
	const __m128 vdiv = _mm_set1_ps((float)((radius + 1) * (radius + 1)));
	__m128i *stack;
	__m128i sum, insum, outsum, px, q;
	const unsigned char *src, *sp;
	unsigned char *dst;
	int div, r1, line, i, k, head, tail;
	unsigned int packed;

	div = 2 * radius + 1;
	r1 = radius + 1;

	stack = malloc((size_t)div * sizeof(__m128i));
	if (!stack)
		return 0;

	for (line = 0; line < lines; line++) {
		src = in + (size_t)line * lstride;
		dst = out + (size_t)line * lstride;

		sum = insum = outsum = zero;

		for (i = -radius; i <= radius; i++) {
			k = (i < 0) ? 0 : ((i >= len) ? len - 1 : i);
			memcpy(&packed, src + (size_t)k * pstride, 4);
			px = _mm_cvtsi32_si128((int)packed);
			px = _mm_unpacklo_epi8(px, zero);
			px = _mm_unpacklo_epi16(px, zero);
			stack[i + radius] = px;
			sum = _mm_add_epi32(sum,
				_mm_mullo_epi16(px, _mm_set1_epi32(r1 - abs(i))));
			if (i > 0)
				insum = _mm_add_epi32(insum, px);
			else
				outsum = _mm_add_epi32(outsum, px);
		}

		head = radius;
		for (i = 0; i < len; i++) {
			unsigned char *dp = dst + (size_t)i * pstride;

			q = _mm_cvttps_epi32(
				_mm_div_ps(_mm_cvtepi32_ps(sum), vdiv));
			q = _mm_packs_epi32(q, zero);
			q = _mm_packus_epi16(q, zero);
			packed = (unsigned int)_mm_cvtsi128_si32(q);
			/* a four-byte store on the vertical RGB pass would
			 * clobber the already-written first byte of the next
			 * column on the last column, so honour ch exactly */
			if (ch == 4)
				memcpy(dp, &packed, 4);
			else {
				dp[0] = (unsigned char)packed;
				dp[1] = (unsigned char)(packed >> 8);
				dp[2] = (unsigned char)(packed >> 16);
			}

			sum = _mm_sub_epi32(sum, outsum);

			tail = head + r1;
			if (tail >= div)
				tail -= div;
			outsum = _mm_sub_epi32(outsum, stack[tail]);

			k = i + r1;
			if (k >= len)
				k = len - 1;
			sp = src + (size_t)k * pstride;
			memcpy(&packed, sp, 4);
			px = _mm_cvtsi32_si128((int)packed);
			px = _mm_unpacklo_epi8(px, zero);
			px = _mm_unpacklo_epi16(px, zero);
			stack[tail] = px;
			insum = _mm_add_epi32(insum, px);
			sum = _mm_add_epi32(sum, insum);

			head++;
			if (head >= div)
				head = 0;
			outsum = _mm_add_epi32(outsum, stack[head]);
			insum = _mm_sub_epi32(insum, stack[head]);
		}
	}

	free(stack);
	return 1;
}
#endif /* __SSE2__ */

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

	/* +4 so the SSE2 path can read four bytes at the last RGB pixel
	 * of tmp the same way RCreateImage's +4 covers image->data */
	tmp = malloc((size_t)stride * h + 4);
	stack = malloc((size_t)div * ch * sizeof(int));
	vmin = malloc((size_t)((w > h) ? w : h) * sizeof(int));

	if (!tmp || !stack || !vmin) {
		free(tmp);
		free(stack);
		free(vmin);
		RErrorCode = RERR_NOMEMORY;
		return 0;
	}

#ifdef __SSE2__
	if (stack_blur_pass_sse2(image->data, tmp, w, h, ch, stride,
				 ch, (int)radius)
	    && stack_blur_pass_sse2(tmp, image->data, h, w, stride, ch,
				    ch, (int)radius)) {
		free(tmp);
		free(stack);
		free(vmin);
		return 1;
	}
	/* fall through to the scalar path on allocation failure inside
	 * the SSE2 helper */
#endif

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

