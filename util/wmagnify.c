/*
 * magnify - a X utility for magnifying screen image
 *
 * 2000/5/21  Alfredo K. Kojima
 *
 * This program is in the Public Domain.
 */
#include <config.h>

#include <X11/Xproto.h>
#include <X11/Xutil.h>

#include <WINGs/WINGs.h>
#include <wraster.h>
#include <cairo-xcb.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * TODO:
 * - lens that shows where it's magnifying
 *
 *
 */

int refreshrate = 200;

typedef struct {
	Drawable d;
	int width, height;
	int rwidth, rheight;	/* size of window in real pixels */
	int magfactor;
	int refreshrate;

	WMWindow *win;
	WMLabel *label;
	WMPixmap *pixmap;

	WMWindow *dlg;

	WMSlider *speed;
	WMSlider *magnify;
	WMButton *okB;
	WMButton *cancelB;
	WMButton *newB;

	int x, y;
	Bool frozen;
	Bool markPointerHotspot;

	WMHandlerID tid;
} BufferData;

static BufferData *newWindow(int magfactor);

int windowCount = 0;

Display *dpy, *vdpy;
WMScreen *scr;
static int rshift, gshift, bshift;
static unsigned long rmask, gmask, bmask;
WMColor *cursorColor1;
WMColor *cursorColor2;


static BufferData *makeBufferData(WMWindow * win, WMLabel * label, int width, int height, int magfactor)
{
	BufferData *data;

	data = wmalloc(sizeof(BufferData));

	data->rwidth = width;
	data->rheight = height;

	data->refreshrate = refreshrate;

	data->magfactor = magfactor;

	data->win = win;
	data->label = label;

	data->pixmap = WMCreatePixmap(scr, width, height, WMScreenDepth(scr), False);
	WMSetLabelImage(data->label, data->pixmap);

	data->d = WMGetPixmapXID(data->pixmap);

	data->frozen = False;

	data->width = width / magfactor;
	data->height = height / magfactor;

	return data;
}

static void resizeBufferData(BufferData * data, int width, int height, int magfactor)
{
	data->rwidth = width;
	data->rheight = height;
	data->magfactor = magfactor;
	data->width = width / magfactor;
	data->height = height / magfactor;

	WMResizeWidget(data->label, width, height);

	WMReleasePixmap(data->pixmap);
	data->pixmap = WMCreatePixmap(scr, width, height, WMScreenDepth(scr), False);
	WMSetLabelImage(data->label, data->pixmap);

	data->d = WMGetPixmapXID(data->pixmap);
}

static int mask_shift(unsigned long m)
{
	int s = 0;

	if (!m)
		return 0;
	while (!(m & 1)) {
		m >>= 1;
		s++;
	}
	/* scale a sub-8-bit channel up to 8 bits */
	while (!(m & 0x80)) {
		m = (m << 1) | 1;
		s--;
	}
	return s;
}

/* Unpack the grabbed source region into CAIRO_FORMAT_RGB24 (native
 * endian 0xRRGGBB per pixel) using the source visual's channel masks.
 * The result is a data->width x data->height buffer with the off-screen
 * border already black, so the caller can paint the whole thing. */
static void unpack_image(BufferData *data, XImage *im, int xoffs, int yoffs,
			 uint32_t *out, int stride)
{
	unsigned long p;
	uint32_t *row;
	int x, y;

	for (y = 0; y < data->height; y++) {
		row = (uint32_t *)((unsigned char *)out + y * stride);
		for (x = 0; x < data->width; x++) {
			if (x < xoffs || y < yoffs ||
			    x >= xoffs + im->width || y >= yoffs + im->height) {
				row[x] = 0;
				continue;
			}
			p = XGetPixel(im, x - xoffs, y - yoffs);
#define SH(v, s) ((s) >= 0 ? ((v) >> (s)) : ((v) << -(s)))
			row[x] = (SH(p & rmask, rshift) & 0xff) << 16 |
				 (SH(p & gmask, gshift) & 0xff) << 8 |
				 (SH(p & bmask, bshift) & 0xff);
#undef SH
		}
	}
}

static void updateImage(BufferData * data, int rx, int ry)
{
	int gx, gy, gw, gh;
	int xoffs, yoffs;
	int stride;
	XImage *image;
	uint32_t *buf;
	cairo_surface_t *ss, *ds;
	cairo_pattern_t *pat;
	cairo_t *cr;
	RContext *rc;

	gw = data->width;
	gh = data->height;

	gx = rx - gw / 2;
	gy = ry - gh / 2;

	xoffs = yoffs = 0;
	if (gx < 0) {
		xoffs = abs(gx);
		gw += gx;
		gx = 0;
	}
	if (gx + gw >= WidthOfScreen(DefaultScreenOfDisplay(vdpy))) {
		gw = WidthOfScreen(DefaultScreenOfDisplay(vdpy)) - gx;
	}
	if (gy < 0) {
		yoffs = abs(gy);
		gh += gy;
		gy = 0;
	}
	if (gy + gh >= HeightOfScreen(DefaultScreenOfDisplay(vdpy))) {
		gh = HeightOfScreen(DefaultScreenOfDisplay(vdpy)) - gy;
	}

	image = XGetImage(vdpy, DefaultRootWindow(vdpy), gx, gy, gw, gh, AllPlanes, ZPixmap);

	stride = cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, data->width);
	buf = wmalloc(stride * data->height);
	unpack_image(data, image, xoffs, yoffs, buf, stride);
	XDestroyImage(image);

	rc = WMScreenRContext(scr);
	ss = cairo_image_surface_create_for_data((unsigned char *)buf,
						 CAIRO_FORMAT_RGB24,
						 data->width, data->height, stride);
	ds = cairo_xcb_surface_create(rc->xc, (xcb_drawable_t)data->d, rc->xvisual,
				      data->rwidth, data->rheight);
	cr = cairo_create(ds);
	cairo_scale(cr, data->magfactor, data->magfactor);
	cairo_set_source_surface(cr, ss, 0, 0);
	pat = cairo_get_source(cr);
	cairo_pattern_set_filter(pat, CAIRO_FILTER_NEAREST);
	cairo_pattern_set_extend(pat, CAIRO_EXTEND_PAD);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_paint(cr);
	cairo_destroy(cr);
	cairo_surface_flush(ds);
	cairo_surface_destroy(ss);
	cairo_surface_destroy(ds);
	wfree(buf);

	if (data->markPointerHotspot && !data->frozen) {
		XRectangle rects[4];

		rects[0].x = (data->width / 2 - 3) * data->magfactor;
		rects[0].y = (data->height / 2) * data->magfactor;
		rects[0].width = 2 * data->magfactor;
		rects[0].height = data->magfactor;

		rects[1].x = (data->width / 2 + 2) * data->magfactor;
		rects[1].y = (data->height / 2) * data->magfactor;
		rects[1].width = 2 * data->magfactor;
		rects[1].height = data->magfactor;

		WMPaintColorSwatch(cursorColor1, data->d, rects[0].x, rects[0].y,
				   rects[0].width, rects[0].height);
		WMPaintColorSwatch(cursorColor1, data->d, rects[1].x, rects[1].y,
				   rects[1].width, rects[1].height);

		rects[2].y = (data->height / 2 - 3) * data->magfactor;
		rects[2].x = (data->width / 2) * data->magfactor;
		rects[2].height = 2 * data->magfactor;
		rects[2].width = data->magfactor;

		rects[3].y = (data->height / 2 + 2) * data->magfactor;
		rects[3].x = (data->width / 2) * data->magfactor;
		rects[3].height = 2 * data->magfactor;
		rects[3].width = data->magfactor;

		WMPaintColorSwatch(cursorColor2, data->d, rects[2].x, rects[2].y,
				   rects[2].width, rects[2].height);
		WMPaintColorSwatch(cursorColor2, data->d, rects[3].x, rects[3].y,
				   rects[3].width, rects[3].height);
	}

	WMRedisplayWidget(data->label);
}

static void update(void *d)
{
	BufferData *data = (BufferData *) d;
	Window win;
	int rx, ry;
	int bla;
	unsigned ubla;

	if (data->frozen) {
		rx = data->x;
		ry = data->y;
	} else {
		XQueryPointer(dpy, DefaultRootWindow(dpy), &win, &win, &rx, &ry, &bla, &bla, &ubla);
	}
	updateImage(data, rx, ry);

	data->tid = WMAddTimerHandler(data->refreshrate, update, data);
}

static void resizedWindow(void *d, WMNotification * notif)
{
	BufferData *data = (BufferData *) d;
	WMView *view = (WMView *) WMGetNotificationObject(notif);
	WMSize size;

	size = WMGetViewSize(view);

	resizeBufferData(data, size.width, size.height, data->magfactor);
}

static void closeWindow(WMWidget * w, void *d)
{
	BufferData *data = (BufferData *) d;

	windowCount--;
	if (windowCount == 0) {
		WMReleaseApplication();
		exit(0);
	} else {
		WMDeleteTimerHandler(data->tid);
		WMDestroyWidget(w);
		WMReleasePixmap(data->pixmap);
		wfree(data);
	}
}

static void keyHandler(XEvent * event, void *d)
{
	BufferData *data = (BufferData *) d;
	char buf[32];
	KeySym ks;
	WMView *view = WMWidgetView(data->win);
	WMSize size;

	size = WMGetViewSize(view);

	if (XLookupString(&event->xkey, buf, 31, &ks, NULL) > 0) {
		switch (buf[0]) {
		case 'n':
			newWindow(data->magfactor);
			break;
		case 'm':
			data->markPointerHotspot = !data->markPointerHotspot;
			break;
		case 'f':
		case ' ':
			data->frozen = !data->frozen;
			if (data->frozen) {
				data->x = event->xkey.x_root;
				data->y = event->xkey.y_root;
				snprintf(buf, sizeof(buf), "[Magnify %ix]", data->magfactor);
			} else {
				snprintf(buf, sizeof(buf), "Magnify %ix", data->magfactor);
			}
			WMSetWindowTitle(data->win, buf);
			break;
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			resizeBufferData(data, size.width, size.height, buf[0] - '0');
			if (data->frozen) {
				snprintf(buf, sizeof(buf), "[Magnify %ix]", data->magfactor);
			} else {
				snprintf(buf, sizeof(buf), "Magnify %ix", data->magfactor);
			}
			WMSetWindowTitle(data->win, buf);
			break;
		}
	}
}

static BufferData *newWindow(int magfactor)
{
	WMWindow *win;
	WMLabel *label;
	BufferData *data;
	char buf[32];

	windowCount++;

	win = WMCreateWindow(scr, "magnify");
	WMResizeWidget(win, 300, 200);
	snprintf(buf, sizeof(buf), "Magnify %ix", magfactor);
	WMSetWindowTitle(win, buf);
	WMSetViewNotifySizeChanges(WMWidgetView(win), True);

	label = WMCreateLabel(win);
	WMResizeWidget(label, 300, 200);
	WMMoveWidget(label, 0, 0);
	WMSetLabelRelief(label, WRSunken);
	WMSetLabelImagePosition(label, WIPImageOnly);

	data = makeBufferData(win, label, 300, 200, magfactor);

	WMCreateEventHandler(WMWidgetView(win), KeyReleaseMask, keyHandler, data);

	WMAddNotificationObserver(resizedWindow, data, WMViewSizeDidChangeNotification, WMWidgetView(win));

	WMRealizeWidget(win);

	WMMapSubwidgets(win);
	WMMapWidget(win);

	WMSetWindowCloseAction(win, closeWindow, data);
	data->tid = WMAddTimerHandler(refreshrate, update, data);

	return data;
}

int main(int argc, char **argv)
{
	int i;
	char *display = "";
	char *vdisplay = NULL;
	int magfactor = 2;
	Visual *vis;

	WMInitializeApplication("Magnify", &argc, argv);

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-display") == 0) {
			i++;
			if (i >= argc)
				goto help;
			display = argv[i];
		} else if (strcmp(argv[i], "-vdisplay") == 0) {
			i++;
			if (i >= argc)
				goto help;
			vdisplay = argv[i];
		} else if (strcmp(argv[i], "-m") == 0) {
			i++;
			if (i >= argc)
				goto help;
			magfactor = atoi(argv[i]);
			if (magfactor < 1 || magfactor > 32) {
				printf("%s:invalid magnification factor ``%s''\n", argv[0], argv[i]);
				exit(1);
			}
		} else if (strcmp(argv[i], "-r") == 0) {
			i++;
			if (i >= argc)
				goto help;
			refreshrate = atoi(argv[i]);
			if (refreshrate < 1) {
				printf("%s:invalid refresh rate ``%s''\n", argv[0], argv[i]);
				exit(1);
			}
		} else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
 help:

			printf("Usage: %s [options]\n", argv[0]);
			puts("Options:");
			puts("  -display <display>\tdisplay where to magnification is shown");
			puts("  -m <number>\t\tchange magnification factor (default 2)");
			puts("  -r <number>\t\tchange refresh delay, in milliseconds (default 200)");
			puts("  -vdisplay <display>\tdisplay from which the magnification is taken");
			puts("  -h, --help\t\tdisplay this help page");
			puts("Keys:");
			puts("  1,2,3,4,5,6,7,8,9	change the magnification factor");
			puts("  <space>, f		freeze the 'camera', making it magnify only the current\n"
			     "			position");
			puts("  n			create a new window");
			puts("  m			show/hide the pointer hotspot mark");
			exit(0);
		}
	}

	dpy = XOpenDisplay(display);
	if (!dpy) {
		puts("could not open display");
		exit(1);
	}

	if (vdisplay) {
		vdpy = XOpenDisplay(vdisplay);
		if (!vdpy) {
			puts("could not open display to be viewed");
			exit(1);
		}
	} else {
		vdpy = dpy;
	}

	vis = DefaultVisual(vdpy, DefaultScreen(vdpy));
	rmask = vis->red_mask;
	gmask = vis->green_mask;
	bmask = vis->blue_mask;
	rshift = mask_shift(rmask);
	gshift = mask_shift(gmask);
	bshift = mask_shift(bmask);

	scr = WMCreateScreen(dpy, 0);

	cursorColor1 = WMCreateNamedColor(scr, "#ff0000", False);
	cursorColor2 = WMCreateNamedColor(scr, "#00ff00", False);

	newWindow(magfactor);

	WMScreenMainLoop(scr);

	return 0;
}
