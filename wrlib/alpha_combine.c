/* alpha_combine.c - Alpha channel combination, based on Gimp 1.1.24
 * The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 * Copyright (c) 2012-2026 Window Maker Team
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

#include "config.h"

#include "wraster.h"
#include "wr_i18n.h"

#ifdef __SSE2__
#include <emmintrin.h>
#endif

#ifdef __SSE2__
/*
 * Four-pixel-wide RGBA-over-RGBA pass that produces the same byte
 * stream as the scalar loop. The three scalar fast-path branches
 * (alpha == 0, sa == alpha, sa == 0) all reduce to the general blend
 * once ratio is defined as 0 when alpha is 0: sa == alpha gives
 * ratio 256 so the source is copied, sa == 0 gives ratio 0 so the
 * destination is kept, and alpha == 0 implies sa == 0 and da == 0 so
 * a zero ratio leaves both colour and the (zero) result alpha intact.
 *
 * The (sa << 8) / alpha integer divide is done as a float divide and
 * truncate. Both operands are exact in single precision and the true
 * quotient is at least 1/255 away from any integer it does not equal,
 * far above the half-ulp rounding error in [0,256], so the truncated
 * float quotient matches the C integer quotient for every input.
 */
static void combine_alpha_row_sse2(unsigned char *dst, const unsigned char *src,
				   int width, int opacity)
{
	const __m128i zero = _mm_setzero_si128();
	const __m128i one32 = _mm_set1_epi32(1);
	const __m128i v80 = _mm_set1_epi32(0x80);
	const __m128i v255 = _mm_set1_epi32(255);
	const __m128i v256_16 = _mm_set1_epi16(256);
	const __m128i vop = _mm_set1_epi32(opacity);
	const __m128i mask_rgb = _mm_set1_epi32(0x00ffffff);
	__m128i sv, dv, sa, da, t, alpha, divisor, ratio32, r16, rr;
	__m128i rlo, rhi, ilo, ihi, slo, shi, dlo, dhi, blo, bhi, out;
	__m128 num, den;
	int x;

	for (x = 0; x + 4 <= width; x += 4) {
		sv = _mm_loadu_si128((const __m128i *)src);
		dv = _mm_loadu_si128((const __m128i *)dst);

		sa = _mm_srli_epi32(sv, 24);
		da = _mm_srli_epi32(dv, 24);

		if (opacity != 255) {
			t = _mm_add_epi32(_mm_mullo_epi16(sa, vop), v80);
			sa = _mm_srli_epi32(_mm_add_epi32(_mm_srli_epi32(t, 8), t), 8);
		}

		t = _mm_mullo_epi16(da, _mm_sub_epi32(v255, sa));
		t = _mm_add_epi32(t, v80);
		alpha = _mm_add_epi32(sa,
			_mm_srli_epi32(_mm_add_epi32(_mm_srli_epi32(t, 8), t), 8));

		/* alpha == 0 implies sa == 0, so dividing by max(alpha,1)
		 * yields ratio 0 with no separate mask */
		divisor = alpha;
		t = _mm_cmpeq_epi32(divisor, zero);
		divisor = _mm_or_si128(divisor, _mm_and_si128(t, one32));
		num = _mm_cvtepi32_ps(_mm_slli_epi32(sa, 8));
		den = _mm_cvtepi32_ps(divisor);
		ratio32 = _mm_cvttps_epi32(_mm_div_ps(num, den));

		r16 = _mm_packs_epi32(ratio32, ratio32);
		rr = _mm_unpacklo_epi16(r16, r16);
		rlo = _mm_unpacklo_epi32(rr, rr);
		rhi = _mm_unpackhi_epi32(rr, rr);
		ilo = _mm_sub_epi16(v256_16, rlo);
		ihi = _mm_sub_epi16(v256_16, rhi);

		slo = _mm_unpacklo_epi8(sv, zero);
		shi = _mm_unpackhi_epi8(sv, zero);
		dlo = _mm_unpacklo_epi8(dv, zero);
		dhi = _mm_unpackhi_epi8(dv, zero);

		blo = _mm_srli_epi16(
			_mm_add_epi16(_mm_mullo_epi16(dlo, ilo),
				      _mm_mullo_epi16(slo, rlo)), 8);
		bhi = _mm_srli_epi16(
			_mm_add_epi16(_mm_mullo_epi16(dhi, ihi),
				      _mm_mullo_epi16(shi, rhi)), 8);

		out = _mm_packus_epi16(blo, bhi);
		out = _mm_and_si128(out, mask_rgb);
		out = _mm_or_si128(out, _mm_slli_epi32(alpha, 24));

		_mm_storeu_si128((__m128i *)dst, out);

		src += 16;
		dst += 16;
	}

	for (; x < width; x++) {
		int s_a = src[3];
		int d_a, tt, al, ratio, inv;

		if (opacity != 255) {
			tt = s_a * opacity + 0x80;
			s_a = ((tt >> 8) + tt) >> 8;
		}
		d_a = dst[3];
		tt = d_a * (255 - s_a) + 0x80;
		al = s_a + (((tt >> 8) + tt) >> 8);
		if (al == 0) {
			dst[3] = 0;
		} else {
			ratio = (s_a << 8) / al;
			inv = 256 - ratio;
			dst[0] = (dst[0] * inv + src[0] * ratio) >> 8;
			dst[1] = (dst[1] * inv + src[1] * ratio) >> 8;
			dst[2] = (dst[2] * inv + src[2] * ratio) >> 8;
			dst[3] = al;
		}
		src += 4;
		dst += 4;
	}
}
#endif /* __SSE2__ */

void RCombineAlpha(unsigned char *d, unsigned char *s, int s_has_alpha,
                    int width, int height, int dwi, int swi, int opacity) {
    int x, y;
    unsigned char *dst = d;
    unsigned char *src = s;

#ifdef __SSE2__
	if (s_has_alpha) {
		for (y = 0; y < height; y++) {
			combine_alpha_row_sse2(dst, src, width, opacity);
			dst += width * 4 + dwi;
			src += width * 4 + swi;
		}
		return;
	}
#endif

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            int sa = s_has_alpha ? src[3] : 255;
            int t, alpha;

            if (opacity != 255) {
                t = sa * opacity + 0x80;
                sa = ((t >> 8) + t) >> 8;
            }

            t = dst[3] * (255 - sa) + 0x80;
            alpha = sa + (((t >> 8) + t) >> 8);

            if (alpha == 0) {
                dst[3] = 0;
            } else if (sa == alpha) {
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = alpha;
            } else if (sa == 0) {
                dst[3] = alpha;
            } else {
                int ratio = (sa << 8) / alpha;
                int inv_ratio = 256 - ratio;

                dst[0] = (dst[0] * inv_ratio + src[0] * ratio) >> 8;
                dst[1] = (dst[1] * inv_ratio + src[1] * ratio) >> 8;
                dst[2] = (dst[2] * inv_ratio + src[2] * ratio) >> 8;
                dst[3] = alpha;
            }

            dst += 4;
            src += s_has_alpha ? 4 : 3;
        }
        dst += dwi;
        src += swi;
    }
}
