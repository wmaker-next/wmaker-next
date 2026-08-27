/*
 * Raster graphics library
 *
 * Copyright (c) 2026 Window Maker Team
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
 *  License along with this library; if not, see
 *  <https://www.gnu.org/licenses/>.
 */

/*
 * Wallpaper-derived UI palette extraction.
 *
 * RExtractPalette() runs a bounded median-cut quantiser over a sampled
 * subset of the image pixels and returns up to DYNAMIC_PALETTE_SWATCHES
 * representative swatches sorted by population x chroma^2 so vivid
 * accents outrank large dark areas. RDeriveDynamicTheme()
 * maps those swatches onto a small set of UI framing roles using the
 * WCAG 2.x relative-luminance / contrast-ratio test to pick text
 * colours and a light or dark "ladder" of stepped variants, so that a
 * degenerate (e.g. all-black) wallpaper still yields distinct focused,
 * unfocused and per-owner chrome rather than collapsing to one colour.
 *
 * The functions are pure transforms on RImage pixel buffers and have no
 * X dependency, so they are unit-testable without a display.
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "wraster.h"

/* Cap on sampled pixels */
#define PALETTE_SAMPLE_CAP 4096
/* Swatches within this per-channel distance merge */
#define PALETTE_DISTINCT_THRESH 8

typedef struct {
	unsigned char r, g, b;
} ppix;

typedef struct {
	ppix *p;	/* slice into the shared pixel array */
	size_t n;
} pbucket;

/* ------------------------------------------------------------------ *
 * Colour science
 * ------------------------------------------------------------------ */

static double srgb_channel_to_linear(unsigned char c)
{
	double cs = c / 255.0;

	return (cs <= 0.03928) ? (cs / 12.92)
			      : pow((cs + 0.055) / 1.055, 2.4);
}

double RRelativeLuminance(unsigned char r, unsigned char g, unsigned char b)
{
	return 0.2126 * srgb_channel_to_linear(r) +
	       0.7152 * srgb_channel_to_linear(g) +
	       0.0722 * srgb_channel_to_linear(b);
}

double RWCAGContrastRatio(double l1, double l2)
{
	double lighter = l1 > l2 ? l1 : l2;
	double darker = l1 > l2 ? l2 : l1;

	return (lighter + 0.05) / (darker + 0.05);
}

RColor RPickWCAGText(unsigned char r, unsigned char g, unsigned char b)
{
	double L = RRelativeLuminance(r, g, b);
	double white_ratio = (1.0 + 0.05) / (L + 0.05);
	double black_ratio = (L + 0.05) / 0.05;
	RColor c;

	c.alpha = 255;
	if (white_ratio >= black_ratio) {
		c.red = 255; c.green = 255; c.blue = 255;
	} else {
		c.red = 0; c.green = 0; c.blue = 0;
	}
	return c;
}

static RColor make_color(unsigned char r, unsigned char g, unsigned char b)
{
	RColor c;

	c.red = r; c.green = g; c.blue = b; c.alpha = 255;
	return c;
}

static unsigned char clamp_channel(int v)
{
	if (v < 0)
		return 0;
	if (v > 255)
		return 255;
	return (unsigned char)v;
}

static int swatch_distance(const RPaletteSwatch *a, const RPaletteSwatch *b)
{
	int dr = abs((int)a->r - (int)b->r);
	int dg = abs((int)a->g - (int)b->g);
	int db = abs((int)a->b - (int)b->b);
	int m = dr > dg ? dr : dg;

	return m > db ? m : db;
}

static int swatches_distinct(const RPaletteSwatch *a, const RPaletteSwatch *b)
{
	return swatch_distance(a, b) > PALETTE_DISTINCT_THRESH;
}

/* ------------------------------------------------------------------ *
 * Median-cut quantiser
 * ------------------------------------------------------------------ */

static int cmp_ppix(const ppix *pa, const ppix *pb, int channel)
{
	int va, vb;

	switch (channel) {
	case 0: va = pa->r; vb = pb->r; break;
	case 1: va = pa->g; vb = pb->g; break;
	default: va = pa->b; vb = pb->b; break;
	}
	return va - vb;
}

/* BSD qsort_r and macOS take the context before the comparator and
   pass it as the comparator's first parameter. glibc and NetBSD pass
   it as the comparator's last parameter. */
#if defined( FREEBSD ) || defined( DRAGONFLYBSD ) || \
    defined( OPENBSD ) || defined( __APPLE__ )
#define QSORT_R_CTX_FIRST 1
#endif

#ifdef QSORT_R_CTX_FIRST
static int cmp_ppix_thunk(void *ctx, const void *a, const void *b)
{
	return cmp_ppix((const ppix *)a, (const ppix *)b, *(const int *)ctx);
}
#else
static int cmp_ppix_r(const void *a, const void *b, void *ctx)
{
	return cmp_ppix(a, b, *(const int *)ctx);
}
#endif

static void bucket_range(const pbucket *b, int *channel, int *range)
{
	size_t i;
	int rmin = 255, rmax = 0, gmin = 255, gmax = 0, bmin = 255, bmax = 0;
	int rrange, grange, brange;

	for (i = 0; i < b->n; i++) {
		ppix px = b->p[i];
		if (px.r < rmin) rmin = px.r;
		if (px.r > rmax) rmax = px.r;
		if (px.g < gmin) gmin = px.g;
		if (px.g > gmax) gmax = px.g;
		if (px.b < bmin) bmin = px.b;
		if (px.b > bmax) bmax = px.b;
	}
	rrange = rmax - rmin;
	grange = gmax - gmin;
	brange = bmax - bmin;
	*channel = 0;
	*range = rrange;
	if (grange > *range) { *channel = 1; *range = grange; }
	if (brange > *range) { *channel = 2; *range = brange; }
}

RPalette *RExtractPalette(const RImage *image)
{
	RPalette *pal;
	ppix *pix;
	pbucket buck[DYNAMIC_PALETTE_SWATCHES];
	size_t npix = 0, i, nb;
	int ch, max_buckets;
	long total, stride, idx;
	pbucket *b;
	size_t mid;
	unsigned long wa, wb, wsum;
	RPaletteSwatch m;
	int mn;

	pal = malloc(sizeof(RPalette));
	if (!pal)
		return NULL;
	pal->count = 0;

	ch = (image->format == RRGBAFormat) ? 4 : 3;
	total = (long)image->width * (long)image->height;
	pix = malloc((total > PALETTE_SAMPLE_CAP ? PALETTE_SAMPLE_CAP : (total > 0 ? (size_t)total : 1)) * sizeof(ppix));
	if (!pix) {
		free(pal);
		return NULL;
	}

	/* Sample pixels, at most PALETTE_SAMPLE_CAP. Skip transparent. */
	stride = (total > PALETTE_SAMPLE_CAP) ? (total / PALETTE_SAMPLE_CAP) : 1;
	if (stride < 1)
		stride = 1;
	for (idx = 0; idx < total && npix < PALETTE_SAMPLE_CAP; idx += stride) {
		const unsigned char *p = image->data + idx * ch;
		if (ch == 4 && p[3] < 128)
			continue;
		pix[npix].r = p[0];
		pix[npix].g = p[1];
		pix[npix].b = p[2];
		npix++;
	}

	if (npix == 0) {
		/* Degenerate: no opaque pixels. Report a single black swatch. */
		pal->swatches[0].r = 0;
		pal->swatches[0].g = 0;
		pal->swatches[0].b = 0;
		pal->swatches[0].weight = 0;
		pal->count = 1;
		free(pix);
		return pal;
	}

	/* Median cut into up to DYNAMIC_PALETTE_SWATCHES buckets. */
	buck[0].p = pix;
	buck[0].n = npix;
	nb = 1;
	max_buckets = DYNAMIC_PALETTE_SWATCHES;
	while (nb < (size_t)max_buckets) {
		size_t best = (size_t)-1;
		int best_range = 0, best_channel = 0;
		size_t k;
		for (k = 0; k < nb; k++) {
			int chn, rng;
			if (buck[k].n < 2)
				continue;
			bucket_range(&buck[k], &chn, &rng);
			if (rng > 0 && rng >= best_range) {
				best = k;
				best_range = rng;
				best_channel = chn;
			}
		}
		if (best == (size_t)-1)
			break;
		b = &buck[best];
		mid = b->n / 2;
		qsort_r(b->p, b->n, sizeof(ppix),
#ifdef QSORT_R_CTX_FIRST
			&best_channel, cmp_ppix_thunk);
#else
			cmp_ppix_r, &best_channel);
#endif
		buck[nb].p = b->p + mid;
		buck[nb].n = b->n - mid;
		b->n = mid;
		nb++;
	}

	/* Representatives: weighted average per bucket. */
	for (i = 0; i < nb; i++) {
		size_t j;
		unsigned long rr = 0, gg = 0, bb = 0;
		for (j = 0; j < buck[i].n; j++) {
			rr += buck[i].p[j].r;
			gg += buck[i].p[j].g;
			bb += buck[i].p[j].b;
		}
		if (buck[i].n) {
			pal->swatches[pal->count].r = (unsigned char)(rr / buck[i].n);
			pal->swatches[pal->count].g = (unsigned char)(gg / buck[i].n);
			pal->swatches[pal->count].b = (unsigned char)(bb / buck[i].n);
			pal->swatches[pal->count].weight = buck[i].n;
			pal->count++;
		}
	}

	free(pix);

	/* Merge near-duplicate swatches. */
	if (pal->count > 1) {
		int merged = 1;
		while (merged) {
			int a;
			merged = 0;
			for (a = 0; a < pal->count && !merged; a++) {
				int b;
				for (b = a + 1; b < pal->count; b++) {
					if (swatches_distinct(&pal->swatches[a],
							      &pal->swatches[b]))
						continue;
					wa = pal->swatches[a].weight;
					wb = pal->swatches[b].weight;
					wsum = wa + wb;
					if (wsum) {
						m.r = (unsigned char)((pal->swatches[a].r * wa +
								       pal->swatches[b].r * wb) / wsum);
						m.g = (unsigned char)((pal->swatches[a].g * wa +
								       pal->swatches[b].g * wb) / wsum);
						m.b = (unsigned char)((pal->swatches[a].b * wa +
								       pal->swatches[b].b * wb) / wsum);
					} else {
						m = pal->swatches[a];
					}
					m.weight = wsum;
					pal->swatches[a] = m;
					/* shift down and drop last */
					pal->swatches[b] = pal->swatches[pal->count - 1];
					pal->count--;
					merged = 1;
					break;
				}
			}
		}
	}

	/* Sort by population x chroma^2 so vivid accents outrank large dark areas */
	if (pal->count > 1) {
		int a;
		for (a = 1; a < pal->count; a++) {
			RPaletteSwatch s = pal->swatches[a];
			int b = a;
			unsigned long s_chroma, s_score;
			s_chroma = (s.r > s.g ? s.r : s.g);
			if (s.b > s_chroma) s_chroma = s.b;
			mn = (s.r < s.g ? s.r : s.g);
			if (s.b < mn) mn = s.b;
			s_chroma -= mn;
			s_chroma += 1;
			s_score = s.weight * s_chroma * s_chroma;
			while (b > 0) {
				unsigned long p_chroma, p_score;
				RPaletteSwatch *p = &pal->swatches[b - 1];
				p_chroma = (p->r > p->g ? p->r : p->g);
				if (p->b > p_chroma) p_chroma = p->b;
				mn = (p->r < p->g ? p->r : p->g);
				if (p->b < mn) mn = p->b;
				p_chroma -= mn;
				p_chroma += 1;
				p_score = p->weight * p_chroma * p_chroma;
				if (p_score >= s_score)
					break;
				pal->swatches[b] = pal->swatches[b - 1];
				b--;
			}
			pal->swatches[b] = s;
		}
	}

	return pal;
}

/* ------------------------------------------------------------------ *
 * HSL conversion (for tonal palette generation)
 * ------------------------------------------------------------------ */

static RColor swatch_to_color(const RPaletteSwatch *s)
{
	return make_color(s->r, s->g, s->b);
}

static void rgb_to_hsl(unsigned char r, unsigned char g, unsigned char b,
		       double *h, double *s, double *l)
{
	double rf = r / 255.0, gf = g / 255.0, bf = b / 255.0;
	double mx, mn, d;

	mx = rf > gf ? rf : gf;
	if (bf > mx) mx = bf;
	mn = rf < gf ? rf : gf;
	if (bf < mn) mn = bf;

	*l = (mx + mn) / 2.0;
	d = mx - mn;
	*s = (d < 1e-10) ? 0.0 :
	     (*l > 0.5) ? (d / (2.0 - mx - mn)) : (d / (mx + mn));
	if (d < 1e-10) {
		*h = 0.0;
	} else if (mx <= rf + 1e-10 && mx >= rf - 1e-10) {
		*h = ((gf - bf) / d);
		if (*h < 0) *h += 6.0;
	} else if (mx <= gf + 1e-10 && mx >= gf - 1e-10) {
		*h = ((bf - rf) / d) + 2.0;
	} else {
		*h = ((rf - gf) / d) + 4.0;
	}
	*h /= 6.0;
}

static double hsl_channel(double p, double q, double t)
{
	if (t < 0) t += 1.0;
	if (t > 1.0) t -= 1.0;
	if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
	if (t < 0.5) return q;
	if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
	return p;
}

static RColor hsl_to_rgb(double h, double s, double l)
{
	double r, g, b;

	if (s < 1e-10) {
		r = g = b = l;
	} else {
		double q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
		double p = 2.0 * l - q;

		r = hsl_channel(p, q, h + 1.0 / 3.0);
		g = hsl_channel(p, q, h);
		b = hsl_channel(p, q, h - 1.0 / 3.0);
	}
	return make_color(clamp_channel((int)(r * 255.0 + 0.5)),
			 clamp_channel((int)(g * 255.0 + 0.5)),
			 clamp_channel((int)(b * 255.0 + 0.5)));
}

/* ------------------------------------------------------------------ *
 * Tonal palette role mapping
 * ------------------------------------------------------------------ */

/* Dark-mode tones */
#define TONE_SURFACE_DARK    0.06
#define TONE_CONTAINER_DARK  0.16
#define TONE_CONTAINER_HI_DARK 0.26
#define TONE_ICON_FAR_DARK   0.35
#define TONE_PRIMARY_DARK    0.80
#define TONE_ON_SURFACE_DARK 0.90
#define TONE_ON_SURFACE_VAR_DARK 0.85
#define TONE_ON_PRIMARY_DARK 0.15
#define TONE_DISABLED_DARK   0.50
#define TONE_BORDER_DARK     0.70

/* Light-mode tones */
#define TONE_SURFACE_LIGHT    0.96
#define TONE_CONTAINER_LIGHT  0.90
#define TONE_CONTAINER_HI_LIGHT 0.86
#define TONE_ICON_FAR_LIGHT   0.80
#define TONE_PRIMARY_LIGHT    0.40
#define TONE_ON_SURFACE_LIGHT 0.10
#define TONE_ON_SURFACE_VAR_LIGHT 0.30
#define TONE_ON_PRIMARY_LIGHT 0.95
#define TONE_DISABLED_LIGHT   0.55
#define TONE_BORDER_LIGHT     0.50

/* Surface saturation: a fraction of the seed's, capped. Monochrome seeds use neutral. */
#define SURFACE_SAT_CAP    0.15
#define SURFACE_SAT_FACTOR 0.20
#define MONO_THRESH        0.05

RDynamicTheme *RDeriveDynamicTheme(const RImage *image, int mode)
{
	RPalette *pal;
	RDynamicTheme *th;
	RColor seed;
	double h, s, l;
	int is_dark;
	double surf_sat, accent_sat;
	double total_w = 0, weighted_l = 0;
	int i;

	pal = RExtractPalette(image);
	if (!pal)
		return NULL;

	th = malloc(sizeof(RDynamicTheme));
	if (!th) {
		free(pal);
		return NULL;
	}
	memset(th, 0, sizeof(*th));

	seed = (pal->count >= 1) ? swatch_to_color(&pal->swatches[0])
				: make_color(0, 0, 0);
	rgb_to_hsl(seed.red, seed.green, seed.blue, &h, &s, &l);

	/* Decide dark/light: use the wallpaper's average luminance, not the seed's */
	for (i = 0; i < pal->count; i++) {
		double sw_l, dummy_h, dummy_s;
		rgb_to_hsl(pal->swatches[i].r, pal->swatches[i].g,
			   pal->swatches[i].b, &dummy_h, &dummy_s, &sw_l);
		weighted_l += sw_l * (double)pal->swatches[i].weight;
		total_w += (double)pal->swatches[i].weight;
	}
	if (total_w > 0)
		l = weighted_l / total_w;

	if (mode == RDYNAMIC_MODE_LIGHT)
		is_dark = 0;
	else if (mode == RDYNAMIC_MODE_DARK)
		is_dark = 1;
	else
		is_dark = (l < 0.40);

	/* Saturation for surfaces vs accent */
	if (s < MONO_THRESH) {
		surf_sat = 0.0;
		accent_sat = 0.0;
	} else {
		surf_sat = s * SURFACE_SAT_FACTOR;
		if (surf_sat > SURFACE_SAT_CAP)
			surf_sat = SURFACE_SAT_CAP;
		accent_sat = s;
	}

	/* Generate roles from the seed hue + fixed tones */
	if (is_dark) {
		th->roles[RDYNAMIC_ROLE_DOMINANT_BACK] = hsl_to_rgb(h, surf_sat, TONE_SURFACE_DARK);
		th->roles[RDYNAMIC_ROLE_SECONDARY_BACK] = hsl_to_rgb(h, surf_sat, TONE_CONTAINER_DARK);
		th->roles[RDYNAMIC_ROLE_TERTIARY_BACK] = hsl_to_rgb(h, surf_sat, TONE_CONTAINER_HI_DARK);
		th->roles[RDYNAMIC_ROLE_ICON_BACK_NEAR] = hsl_to_rgb(h, surf_sat, TONE_CONTAINER_DARK);
		th->roles[RDYNAMIC_ROLE_ICON_BACK_FAR] = hsl_to_rgb(h, surf_sat, TONE_ICON_FAR_DARK);
		th->roles[RDYNAMIC_ROLE_HIGHLIGHT_BACK] = hsl_to_rgb(h, accent_sat, TONE_PRIMARY_DARK);
		th->roles[RDYNAMIC_ROLE_DOMINANT_TEXT] = hsl_to_rgb(h, surf_sat, TONE_ON_SURFACE_DARK);
		th->roles[RDYNAMIC_ROLE_SECONDARY_TEXT] = hsl_to_rgb(h, surf_sat, TONE_ON_SURFACE_VAR_DARK);
		th->roles[RDYNAMIC_ROLE_TERTIARY_TEXT] = hsl_to_rgb(h, surf_sat, TONE_ON_SURFACE_VAR_DARK);
		th->roles[RDYNAMIC_ROLE_HIGHLIGHT_TEXT] = hsl_to_rgb(h, accent_sat, TONE_ON_PRIMARY_DARK);
		th->roles[RDYNAMIC_ROLE_MENU_DISABLED_TEXT] = hsl_to_rgb(h, surf_sat, TONE_DISABLED_DARK);
		th->roles[RDYNAMIC_ROLE_FOCUSED_BORDER] = hsl_to_rgb(h, accent_sat, TONE_BORDER_DARK);
	} else {
		th->roles[RDYNAMIC_ROLE_DOMINANT_BACK] = hsl_to_rgb(h, surf_sat, TONE_SURFACE_LIGHT);
		th->roles[RDYNAMIC_ROLE_SECONDARY_BACK] = hsl_to_rgb(h, surf_sat, TONE_CONTAINER_LIGHT);
		th->roles[RDYNAMIC_ROLE_TERTIARY_BACK] = hsl_to_rgb(h, surf_sat, TONE_CONTAINER_HI_LIGHT);
		th->roles[RDYNAMIC_ROLE_ICON_BACK_NEAR] = hsl_to_rgb(h, surf_sat, TONE_CONTAINER_LIGHT);
		th->roles[RDYNAMIC_ROLE_ICON_BACK_FAR] = hsl_to_rgb(h, surf_sat, TONE_ICON_FAR_LIGHT);
		th->roles[RDYNAMIC_ROLE_HIGHLIGHT_BACK] = hsl_to_rgb(h, accent_sat, TONE_PRIMARY_LIGHT);
		th->roles[RDYNAMIC_ROLE_DOMINANT_TEXT] = hsl_to_rgb(h, surf_sat, TONE_ON_SURFACE_LIGHT);
		th->roles[RDYNAMIC_ROLE_SECONDARY_TEXT] = hsl_to_rgb(h, surf_sat, TONE_ON_SURFACE_VAR_LIGHT);
		th->roles[RDYNAMIC_ROLE_TERTIARY_TEXT] = hsl_to_rgb(h, surf_sat, TONE_ON_SURFACE_VAR_LIGHT);
		th->roles[RDYNAMIC_ROLE_HIGHLIGHT_TEXT] = hsl_to_rgb(h, accent_sat, TONE_ON_PRIMARY_LIGHT);
		th->roles[RDYNAMIC_ROLE_MENU_DISABLED_TEXT] = hsl_to_rgb(h, surf_sat, TONE_DISABLED_LIGHT);
		th->roles[RDYNAMIC_ROLE_FOCUSED_BORDER] = hsl_to_rgb(h, accent_sat, TONE_BORDER_LIGHT);
	}
	th->ladder = is_dark ? RDYNAMIC_LADDER_DARK : RDYNAMIC_LADDER_LIGHT;

	free(pal);
	return th;
}
