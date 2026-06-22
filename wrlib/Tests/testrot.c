
#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>
#include "wraster.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "tile.xpm"
Display *dpy;
Window win;
RContext *ctx;
RImage *img;
Pixmap pix;

#define MAX(a,b) (a)>(b) ? (a) : (b)

int main(int argc, char **argv)
{
	RContextAttributes attr;
	float a;

	dpy = XOpenDisplay("");
	if (!dpy) {
		puts("can't open display");
		exit(1);
	}

	attr.flags = 0;
	ctx = RCreateContextXCB(XGetXCBConnection(dpy), DefaultScreen(dpy), &attr);

	if (argc < 2)
		img = RGetImageFromXPMData(ctx, image_name);
	else
		img = RLoadImage(ctx, argv[1], 0);

	if (!img) {
		puts(RMessageForError(RErrorCode));
		exit(1);
	}
	win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 10, 10,
				  MAX(img->width, img->height), MAX(img->height, img->width), 0, 0, 0);
	XMapRaised(dpy, win);
	XFlush(dpy);

	a = 0;
	while (1) {
		RImage *tmp;

		a = a + 1.0;

		tmp = RRotateImage(img, a);
		if (!RConvertImage(ctx, tmp, &pix)) {
			puts(RMessageForError(RErrorCode));
			exit(1);
		}
		RReleaseImage(tmp);

		XSetWindowBackgroundPixmap(dpy, win, pix);
		XFreePixmap(dpy, pix);
		XClearWindow(dpy, win);
		XSync(dpy, 0);
		usleep(50000);
	}
	exit(0);
}
