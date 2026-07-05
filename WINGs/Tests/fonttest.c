/*
 * WINGs Font Test - Cairo+Pango Rendering Showcase
 *
 * This test demonstrates the improvements in font rendering
 * using Cairo for antialiasing and Pango for advanced text layout
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <WINGs/WINGs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Display *dpy;

/* Test samples showing different capabilities */
static const char *sampleTexts[] = {
	"The quick brown fox jumps over the lazy dog",
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ",
	"abcdefghijklmnopqrstuvwxyz",
	"0123456789 !@#$%^&*()_+-=[]{}|;':\",./<>?",
	"Cairo+Pango: Smooth antialiased rendering",
	"Unicode: Héllo Wörld • 你好世界 • مرحبا بك",
	"Mixed: English 日本語 العربية Русский Ελληνικά",
	"Symbols: ← → ↑ ↓ ✓ ✗ ★ ☆ ♠ ♥ ♦ ♣",
	NULL
};

void paintSamples(XEvent *event, void *data)
{
	WMFrame *frame = (WMFrame *)data;
	Window win = WMWidgetXID(frame);
	int width = WMWidgetWidth(frame);
	int height = WMWidgetHeight(frame);
	WMScreen *scr = WMWidgetScreen(frame);

	/* Only paint on Expose events */
	if (event->type != Expose)
		return;

	WMFont *fonts[6];
	const char *fontSpecs[] = {
		"Sans:pixelsize=12",
		"Sans:pixelsize=14:weight=bold",
		"Serif:pixelsize=16:slant=italic",
		"Monospace:pixelsize=13",
		"Sans:pixelsize=18:weight=bold",
		"Serif:pixelsize=20"
	};

	WMColor *black = WMBlackColor(scr);
	WMColor *blue = WMCreateRGBColor(scr, 0x2020, 0x4080, 0xC0C0, False);
	WMColor *darkGreen = WMCreateRGBColor(scr, 0x2020, 0x8080, 0x2020, False);
	WMColor *bg = WMCreateRGBColor(scr, 0xF0F0, 0xF0F0, 0xF0F0, False);
	int y = 10;
	int i;

	/* Clear with background */
	XSetForeground(WMScreenDisplay(scr), WMColorGC(bg), WMColorPixel(bg));
	XFillRectangle(WMScreenDisplay(scr), win, WMColorGC(bg), 0, 0, width, height);

	/* Create test fonts */
	for (i = 0; i < 6; i++) {
		fonts[i] = WMCreateFont(scr, fontSpecs[i]);
		if (!fonts[i]) {
			fonts[i] = WMSystemFontOfSize(scr, 12);
		}
	}

	/* Title */
	WMFont *titleFont = WMBoldSystemFontOfSize(scr, 18);
	const char *title = "Cairo+Pango Font Rendering Test";
	WMDrawString(scr, win, blue, titleFont, 10, y, title, strlen(title));
	y += WMFontHeight(titleFont) + 10;
	WMReleaseFont(titleFont);

	/* Separator */
	XDrawLine(WMScreenDisplay(scr), win, WMColorGC(black),
	          10, y, width - 10, y);
	y += 10;

	/* Render sample texts */
	for (i = 0; sampleTexts[i] != NULL && y < height - 60; i++) {
		WMFont *font = fonts[i % 6];
		WMColor *color = (i % 3 == 0) ? black : (i % 3 == 1) ? blue : darkGreen;

		/* Font spec info */
		char info[256];
		snprintf(info, sizeof(info), "[%s]:", fontSpecs[i % 6]);
		WMFont *infoFont = WMSystemFontOfSize(scr, 9);
		WMDrawString(scr, win, darkGreen, infoFont, 10, y, info, strlen(info));
		WMReleaseFont(infoFont);
		y += 12;

		/* Sample text */
		const char *text = sampleTexts[i];
		WMDrawString(scr, win, color, font, 20, y, text, strlen(text));
		y += WMFontHeight(font) + 8;
	}

	/* Footer */
	y = height - 30;
	XDrawLine(WMScreenDisplay(scr), win, WMColorGC(black),
	          10, y - 5, width - 10, y - 5);

	WMFont *footerFont = WMSystemFontOfSize(scr, 9);
	const char *footer = "✓ Rendering: Cairo + Pango (advanced layout, full Unicode support)";
	WMDrawString(scr, win, black, footerFont, 10, y, footer, strlen(footer));
	WMReleaseFont(footerFont);

	/* Cleanup */
	for (i = 0; i < 6; i++) {
		WMReleaseFont(fonts[i]);
	}
	WMReleaseColor(black);
	WMReleaseColor(blue);
	WMReleaseColor(darkGreen);
	WMReleaseColor(bg);
}

void testMainWindow(WMScreen *scr)
{
	WMWindow *win;
	WMFrame *frame;

	win = WMCreateWindow(scr, "fontTest");
	WMSetWindowTitle(win, "Font Rendering Test - Cairo+Pango");
	WMResizeWidget(win, 750, 550);
	WMSetWindowCloseAction(win, NULL, NULL);

	frame = WMCreateFrame(win);
	WMResizeWidget(frame, 750, 550);
	WMMoveWidget(frame, 0, 0);
	WMSetFrameTitle(frame, "Font Samples - Antialiased with Cairo");

	WMSetWidgetBackgroundColor(frame, WMWhiteColor(scr));
	WMSetFrameRelief(frame, WRFlat);

	/* Set up expose event handler */
	WMCreateEventHandler(WMWidgetView(frame), ExposureMask,
	                     paintSamples, frame);

	WMMapSubwidgets(win);
	WMRealizeWidget(win);
	WMMapWidget(win);
}

void testSizeComparison(WMScreen *scr)
{
	WMWindow *win;
	WMBox *vbox;
	WMLabel *label;
	int i;

	win = WMCreateWindow(scr, "sizeComparison");
	WMSetWindowTitle(win, "Font Size Comparison");
	WMResizeWidget(win, 600, 600);
	WMSetWindowCloseAction(win, NULL, NULL);

	vbox = WMCreateBox(win);
	WMSetBoxHorizontal(vbox, False);
	WMResizeWidget(vbox, 600, 600);

	/* Title */
	label = WMCreateLabel(vbox);
	WMSetLabelText(label, "Cairo Font Sizes - Smooth Antialiasing");
	WMSetLabelFont(label, WMBoldSystemFontOfSize(scr, 18));
	WMSetLabelTextAlignment(label, WACenter);
	WMResizeWidget(label, 600, 40);
	WMAddBoxSubview(vbox, WMWidgetView(label), False, True, 40, 0, 5);

	/* Size samples */
	for (i = 8; i <= 36; i += 2) {
		WMFont *font = WMSystemFontOfSize(scr, i);
		char text[128];
		snprintf(text, sizeof(text),
		         "Size %d: Smooth Cairo rendering (Aa Bb Cc Dd 123 !@#)", i);

		label = WMCreateLabel(vbox);
		WMSetLabelText(label, text);
		WMSetLabelFont(label, font);
		WMResizeWidget(label, 600, i + 10);
		WMAddBoxSubview(vbox, WMWidgetView(label), False, True, i + 10, 0, 2);

		/* Don't release font - WMLabel takes ownership */
	}

	WMMapSubwidgets(vbox);
	WMMapSubwidgets(win);
	WMRealizeWidget(win);
	WMMapWidget(win);
}

void testUnicode(WMScreen *scr)
{
	WMWindow *win;
	WMBox *vbox;
	WMLabel *label;
	WMFont *font;
	int i;

	const char *unicodeTests[] = {
		"Latin: The Quick Brown Fox",
		"Greek: Τὴν δὲ σοφίαν",
		"Cyrillic: Россия Украина",
		"Arabic: مرحبا بكم (RTL)",
		"Hebrew: שלום עולם (RTL)",
		"Chinese: 你好世界",
		"Japanese: こんにちは世界",
		"Korean: 안녕하세요",
		"Thai: สวัสดีครับ",
		"Emoticons: 😀 😃 😄 😁 🎉 ❤️ 👍 ⭐",
		"Emoji: 🌍 🌈 🔥 💻 🎨 🎵 🚀 ✨",
		"Symbols: ← → ↑ ↓ ♠ ♥ ♦ ♣",
		"Math: ∑ ∫ ∂ √ ∞ ≈ ≠ ≤ ≥",
		"Currency: $ € £ ¥ ₹ ₽",
		NULL
	};

	win = WMCreateWindow(scr, "unicodeTest");
	WMSetWindowTitle(win, "Unicode Support Test");
	WMResizeWidget(win, 500, 500);
	WMSetWindowCloseAction(win, NULL, NULL);

	vbox = WMCreateBox(win);
	WMSetBoxHorizontal(vbox, False);
	WMResizeWidget(vbox, 500, 500);

	/* Title */
	label = WMCreateLabel(vbox);
	WMSetLabelText(label, "Unicode with Pango - Full Support");
	WMSetLabelFont(label, WMBoldSystemFontOfSize(scr, 16));
	WMSetLabelTextAlignment(label, WACenter);
	WMResizeWidget(label, 500, 35);
	WMAddBoxSubview(vbox, WMWidgetView(label), False, True, 35, 0, 5);

	font = WMSystemFontOfSize(scr, 14);

	/* Unicode samples */
	for (i = 0; unicodeTests[i] != NULL; i++) {
		label = WMCreateLabel(vbox);
		WMSetLabelText(label, unicodeTests[i]);
		WMSetLabelFont(label, font);
		WMResizeWidget(label, 500, 28);
		WMAddBoxSubview(vbox, WMWidgetView(label), False, True, 28, 0, 2);
	}

	WMMapSubwidgets(vbox);
	WMMapSubwidgets(win);
	WMRealizeWidget(win);
	WMMapWidget(win);
}

int main(int argc, char **argv)
{
	WMScreen *scr;

	WMInitializeApplication("FontTest", &argc, argv);

	dpy = XOpenDisplay("");
	if (!dpy) {
		fprintf(stderr, "Could not open display\n");
		exit(1);
	}

	scr = WMCreateScreen(dpy, DefaultScreen(dpy));

	printf("\n=================================================\n");
	printf("WINGs Font Rendering Test\n");
	printf("=================================================\n");
	printf("Backend: Cairo + Pango\n");
	printf("=================================================\n\n");

	printf("Creating test windows...\n");
	printf("  1. Main font samples window\n");
	printf("  2. Font size comparison\n");
	printf("  3. Unicode support test\n\n");
	printf("Close all windows to exit.\n");
	printf("Observe the smooth antialiased text!\n\n");

	/* Create test windows */
	testMainWindow(scr);
	testSizeComparison(scr);
	testUnicode(scr);

	WMScreenMainLoop(scr);

	return 0;
}
