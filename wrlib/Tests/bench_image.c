/*
 *  bench_image - libwraster pixel-loop micro-benchmark
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Times the RImage geometry, filter and decode helpers that icon and
 *  theme loading depend on, at three sizes, and prints throughput in
 *  megapixels per second so a rendering rewrite has the same reference
 *  numbers across machines. Images are generated in memory and a stub
 *  RContext is built on the stack so no X server is needed.
 *
 *  Method: each operation is calibrated by doubling the iteration
 *  count until one trial takes at least BENCH_MIN_NS, then five trials
 *  of that count are run and the minimum is reported. Set
 *  BENCH_SIZES=small to skip the 1080p row for a quick smoke run.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <config.h>
#include <wraster.h>

#define BENCH_MIN_NS   (100 * 1000 * 1000)
#define BENCH_TRIALS   5

static RContextAttributes stub_attribs;
static RContext stub_ctx;
static char tmpdir[] = "/tmp/wrlib-bench-XXXXXX";

static double now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

/* gradient with a per-pixel value so cache effects are realistic and
 * the optimiser cannot constant-fold the inner loops */
static RImage *make_image(unsigned w, unsigned h, int alpha)
{
	RImage *img = RCreateImage(w, h, alpha);
	unsigned char *p;
	unsigned x, y;
	int ch;

	if (!img) {
		fprintf(stderr, "RCreateImage(%u,%u): %s\n", w, h,
			RMessageForError(RErrorCode));
		exit(1);
	}
	ch = (img->format == RRGBAFormat) ? 4 : 3;
	p = img->data;
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			p[0] = (unsigned char)(x * 255 / (w ? w : 1));
			p[1] = (unsigned char)(y * 255 / (h ? h : 1));
			p[2] = (unsigned char)((x ^ y) & 0xff);
			if (ch == 4)
				p[3] = (unsigned char)(128 + ((x + y) & 0x7f));
			p += ch;
		}
	}
	return img;
}

static const char *write_ppm(unsigned w, unsigned h)
{
	static char path[256];
	RImage *img = make_image(w, h, 0);
	FILE *f;

	snprintf(path, sizeof(path), "%s/bench-%ux%u.ppm", tmpdir, w, h);
	f = fopen(path, "wb");
	if (!f) {
		perror(path);
		exit(1);
	}
	fprintf(f, "P6\n%u %u\n255\n", w, h);
	fwrite(img->data, 3, (size_t)w * h, f);
	fclose(f);
	RReleaseImage(img);
	return path;
}

typedef struct {
	RImage *src;
	RImage *src2;
	const char *path;
	unsigned w, h;
} bench_ctx;

typedef void (*bench_fn)(bench_ctx *c);

static void op_scale_up(bench_ctx *c)
{
	RImage *r = RScaleImage(c->src, c->w * 2, c->h * 2);
	RReleaseImage(r);
}

static void op_scale_down(bench_ctx *c)
{
	RImage *r = RScaleImage(c->src, c->w / 2, c->h / 2);
	RReleaseImage(r);
}

static void op_smooth_up(bench_ctx *c)
{
	RImage *r = RSmoothScaleImage(c->src, c->w * 2, c->h * 2);
	RReleaseImage(r);
}

static void op_smooth_down(bench_ctx *c)
{
	RImage *r = RSmoothScaleImage(c->src, c->w / 2, c->h / 2);
	RReleaseImage(r);
}

static void op_rotate90(bench_ctx *c)
{
	RImage *r = RRotateImage(c->src, 90.0f);
	RReleaseImage(r);
}

static void op_flip_h(bench_ctx *c)
{
	RImage *r = RFlipImage(c->src, RHorizontalFlip);
	RReleaseImage(r);
}

static void op_blur(bench_ctx *c)
{
	/* RBlurImage works in place, clone per iteration so each trial
	 * sees the same input rather than an increasingly soft one */
	RImage *r = RCloneImage(c->src);
	RBlurImage(r);
	RReleaseImage(r);
}

static void op_stack_blur(bench_ctx *c)
{
	RImage *r = RCloneImage(c->src);
	RStackBlurImage(r, 16);
	RReleaseImage(r);
}

static void op_combine(bench_ctx *c)
{
	RImage *r = RCloneImage(c->src);
	RCombineImagesWithOpaqueness(r, c->src2, 128);
	RReleaseImage(r);
}

static void op_load_ppm(bench_ctx *c)
{
	RImage *r = RLoadImage(&stub_ctx, c->path, 0);
	RReleaseImage(r);
}

static double time_n(bench_fn fn, bench_ctx *c, unsigned long n)
{
	double t0;
	unsigned long i;

	t0 = now_ns();
	for (i = 0; i < n; i++)
		fn(c);
	return now_ns() - t0;
}

static void run(const char *name, bench_fn fn, bench_ctx *c, unsigned long pixels)
{
	unsigned long iters = 1;
	double t, best;
	int trial;

	/* calibrate */
	for (;;) {
		t = time_n(fn, c, iters);
		if (t >= BENCH_MIN_NS || iters > (1UL << 24))
			break;
		iters *= 2;
	}

	best = t;
	for (trial = 1; trial < BENCH_TRIALS; trial++) {
		t = time_n(fn, c, iters);
		if (t < best)
			best = t;
	}

	printf("  %-22s %5ux%-5u %8lu it  %9.3f ms  %9.1f MPix/s\n",
	       name, c->w, c->h, iters, best / iters / 1e6,
	       (double)pixels * iters / best * 1e3);
}

static const struct { unsigned w, h; } sizes[] = {
	{   64,   64 },
	{  256,  256 },
	{ 1920, 1080 },
};

int main(void)
{
	const char *env_sizes;
	size_t nsizes, i;

	memset(&stub_attribs, 0, sizeof(stub_attribs));
	stub_attribs.scaling_filter = RMitchellFilter;
	memset(&stub_ctx, 0, sizeof(stub_ctx));
	stub_ctx.attribs = &stub_attribs;
	stub_ctx.depth = 24;

	setenv("RIMAGE_CACHE", "0", 1);

	if (!mkdtemp(tmpdir)) {
		perror("mkdtemp");
		return 1;
	}

	nsizes = sizeof(sizes) / sizeof(sizes[0]);
	env_sizes = getenv("BENCH_SIZES");
	if (env_sizes && strcmp(env_sizes, "small") == 0)
		nsizes = 2;

	printf("# wrlib bench_image, min of %d trials >= %d ms each, scaling_filter=Mitchell\n",
	       BENCH_TRIALS, BENCH_MIN_NS / 1000000);

	for (i = 0; i < nsizes; i++) {
		bench_ctx c;
		unsigned long pix;

		c.w = sizes[i].w;
		c.h = sizes[i].h;
		pix = (unsigned long)c.w * c.h;
		c.src  = make_image(c.w, c.h, 1);
		c.src2 = make_image(c.w, c.h, 1);
		c.path = write_ppm(c.w, c.h);

		printf("\n");
		/* output-pixel count for the scale ops, source-pixel for the rest */
		run("RScaleImage 2x",       op_scale_up,   &c, pix * 4);
		run("RScaleImage 0.5x",     op_scale_down, &c, pix / 4);
		run("RSmoothScaleImage 2x", op_smooth_up,   &c, pix * 4);
		run("RSmoothScaleImage .5x",op_smooth_down, &c, pix / 4);
		run("RRotateImage 90",      op_rotate90,   &c, pix);
		run("RFlipImage H",         op_flip_h,     &c, pix);
		run("RBlurImage",           op_blur,       &c, pix);
		run("RStackBlurImage r=16", op_stack_blur, &c, pix);
		run("RCombine opaq=128",    op_combine,    &c, pix);
		run("RLoadImage P6 PPM",    op_load_ppm,   &c, pix);

		unlink(c.path);
		RReleaseImage(c.src);
		RReleaseImage(c.src2);
	}

	rmdir(tmpdir);
	return 0;
}
