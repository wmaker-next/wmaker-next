
#include <xcb/xcb.h>
#include <stdbool.h>

#include "wraster.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

xcb_connection_t *xc;
xcb_window_t win;
RContext *ctx;
RImage *imgh, *imgv, *imgd;
xcb_pixmap_t pix;
char *ProgName;

void print_help()
{
	printf("usage: %s [-options] color1 [color2 ...]\n", ProgName);
	puts("options:");
	puts(" -v <vis-id>	visual id to use");
}

int main(int argc, char **argv)
{
	RContextAttributes attr;
	int sn;
	xcb_window_t win;
	xcb_pixmap_t pix;
	RColor **colors = NULL;
	int i, ncolors = 0;
	char **color_name;
	xcb_connection_t *xc;
	uint16_t cr, cg, cb;
	uint32_t val[3];
	int visualID = -1;

	/* the #rgb form of XParseColor, scaled to 16 bits; named colours
	 * go through the server LookupColor below */
#define PARSE_HASH_COLOR(str, out) ({ \
	const char *h_ = (str) + 1; \
	int n_ = strlen(h_), v_, j_; \
	bool ok_ = n_ == 3 || n_ == 6 || n_ == 9 || n_ == 12; \
	if (ok_) { \
		n_ /= 3; \
		for (j_ = 0; j_ < n_ && ok_; j_++) { \
			char c_ = h_[j_] | 0x20; \
			ok_ = (c_ >= '0' && c_ <= '9') || (c_ >= 'a' && c_ <= 'f'); \
		} \
	} \
	if (ok_) { \
		int shift_ = n_ == 1 ? 4 : 0; \
		v_ = 0; \
		for (j_ = 0; j_ < n_; j_++) \
			v_ = v_ * 16 + (h_[j_] <= '9' ? h_[j_] - '0' : (h_[j_] | 0x20) - 'a' + 10); \
		(out) = v_ << shift_; \
		if (n_ == 1) (out) = (out) * 0x101 * 0x11; \
	} \
	ok_; })

	ProgName = strrchr(argv[0], '/');
	if (!ProgName)
		ProgName = argv[0];
	else
		ProgName++;

	color_name = (char **)malloc(sizeof(char *) * argc);
	if (color_name == NULL) {
		fprintf(stderr, "Cannot allocate memory!\n");
		exit(1);
	}

	if (argc > 1) {
		for (i = 1; i < argc; i++) {
			if (strcmp(argv[i], "-v") == 0) {
				i++;
				if (i >= argc) {
					fprintf(stderr, "too few arguments for %s\n", argv[i - 1]);
					exit(0);
				}
				if (sscanf(argv[i], "%i", &visualID) != 1) {
					fprintf(stderr, "bad value for visual ID: \"%s\"\n", argv[i]);
					exit(0);
				}
			} else if (argv[i][0] != '-') {
				color_name[ncolors++] = argv[i];
			} else {
				print_help();
				exit(1);
			}
		}
	}

	if (ncolors == 0) {
		print_help();
		exit(1);
	}

	xc = xcb_connect(NULL, &sn);
	if (xcb_connection_has_error(xc)) {
		puts("can't open display");
		exit(1);
	}
	attr.flags = 0;

	if (visualID >= 0) {
		attr.flags |= RC_VisualID;
		attr.visualid = visualID;
	}

	ctx = RCreateContextXCB(xc, sn, &attr);

	if (!ctx) {
		printf("could not initialize graphics library context: %s\n", RMessageForError(RErrorCode));
		exit(1);
	}

	colors = malloc(sizeof(RColor *) * (ncolors + 1));
	if (!colors) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	for (i = 0; i < ncolors; i++) {
		if (color_name[i][0] == '#') {
			if (!PARSE_HASH_COLOR(color_name[i], cr)
			    || !PARSE_HASH_COLOR(color_name[i] + 1, cg)
			    || !PARSE_HASH_COLOR(color_name[i] + 2, cb)) {
				printf("could not parse color \"%s\"\n", color_name[i]);
				exit(1);
			}
		} else {
			xcb_lookup_color_cookie_t ck;
			xcb_lookup_color_reply_t *rep;

			ck = xcb_lookup_color(xc, ctx->cmap,
					      (uint16_t) strlen(color_name[i]), color_name[i]);
			rep = xcb_lookup_color_reply(xc, ck, NULL);
			if (!rep) {
				printf("could not parse color \"%s\"\n", color_name[i]);
				exit(1);
			}
			cr = rep->exact_red;
			cg = rep->exact_green;
			cb = rep->exact_blue;
			free(rep);
		}
		{
			colors[i] = malloc(sizeof(RColor));
			if (!colors[i]) {
				fprintf(stderr, "out of memory\n");
				exit(1);
			}
			colors[i]->red = cr >> 8;
			colors[i]->green = cg >> 8;
			colors[i]->blue = cb >> 8;
			printf("0x%02x%02x%02x\n", colors[i]->red, colors[i]->green, colors[i]->blue);
		}
	}
	colors[i] = NULL;

	val[0] = ctx->black;
	val[1] = XCB_BACKING_STORE_ALWAYS;
	val[2] = ctx->cmap;
	win = xcb_generate_id(xc);
	xcb_create_window(xc, ctx->depth, win,
			  xcb_setup_roots_iterator(xcb_get_setup(xc)).data->root,
			  10, 10, 750, 250, 0,
			  XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT,
			  XCB_CW_BACK_PIXEL | XCB_CW_BACKING_STORE | XCB_CW_COLORMAP,
			  val);
	xcb_configure_window(xc, win, XCB_CONFIG_WINDOW_STACK_MODE,
			     (uint32_t[]) { XCB_STACK_MODE_ABOVE });
	xcb_map_window(xc, win);
	xcb_flush(xc);

	imgh = RRenderMultiGradient(250, 250, colors, RGRD_HORIZONTAL);
	imgv = RRenderMultiGradient(250, 250, colors, RGRD_VERTICAL);
	imgd = RRenderMultiGradient(250, 250, colors, RGRD_DIAGONAL);
	RConvertImage(ctx, imgh, &pix);
	xcb_copy_area(xc, pix, win, ctx->copy_xgc, 0, 0, 250, 250, 0, 0);
	RReleaseImage(imgh);

	RConvertImage(ctx, imgv, &pix);
	xcb_copy_area(xc, pix, win, ctx->copy_xgc, 0, 0, 250, 250, 250, 0);
	RReleaseImage(imgv);

	RConvertImage(ctx, imgd, &pix);
	xcb_copy_area(xc, pix, win, ctx->copy_xgc, 0, 0, 250, 250, 500, 0);
	RReleaseImage(imgd);

	xcb_flush(xc);

	getchar();

	free(color_name);
	for (i = 0; i < ncolors + 1; i++)
		free(colors[i]);
	free(colors);

	RDestroyContext(ctx);
	RShutdown();
	xcb_disconnect(xc);

	return 0;
}
