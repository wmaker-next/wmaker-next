/*
 *  fonttest.c
 *
 *  Window Maker WINGs library
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

xcb_connection_t *xc;
int sn;
xcb_connection_t *xc;

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

void paintSamples(xcb_generic_event_t *event, void *data)
{
	WMFrame *frame = (WMFrame *)data;
	xcb_window_t win = WMWidgetXID(frame);
	int width = WMWidgetWidth(frame);
	int height = WMWidgetHeight(frame);
	WMScreen *scr = WMWidgetScreen(frame);

	/* Only paint on Expose events */
	if ((event->response_type & 0x7f) != XCB_EXPOSE)
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
	WMColor *blue = WMCreateRGBColor(scr, 0x2020, 0x4080, 0xC0C0, false);
	WMColor *darkGreen = WMCreateRGBColor(scr, 0x2020, 0x8080, 0x2020, false);
	WMColor *bg = WMCreateRGBColor(scr, 0xF0F0, 0xF0F0, 0xF0F0, false);
	int y = 10;
	int i;

	/* Clear with background */
	WMPaintColorSwatch(bg, win, 0, 0, width, height);

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
	WMPaintColorSwatch(black, win, 10, y, width - 20, 1);
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
	WMPaintColorSwatch(black, win, 10, y - 5, width - 20, 1);

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
	WMCreateEventHandler(WMWidgetView(frame), XCB_EVENT_MASK_EXPOSURE,
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
	WMSetBoxHorizontal(vbox, false);
	WMResizeWidget(vbox, 600, 600);

	/* Title */
	label = WMCreateLabel(vbox);
	WMSetLabelText(label, "Cairo Font Sizes - Smooth Antialiasing");
	WMSetLabelFont(label, WMBoldSystemFontOfSize(scr, 18));
	WMSetLabelTextAlignment(label, WACenter);
	WMResizeWidget(label, 600, 40);
	WMAddBoxSubview(vbox, WMWidgetView(label), false, true, 40, 0, 5);

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
		WMAddBoxSubview(vbox, WMWidgetView(label), false, true, i + 10, 0, 2);

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
	WMSetBoxHorizontal(vbox, false);
	WMResizeWidget(vbox, 500, 500);

	/* Title */
	label = WMCreateLabel(vbox);
	WMSetLabelText(label, "Unicode with Pango - Full Support");
	WMSetLabelFont(label, WMBoldSystemFontOfSize(scr, 16));
	WMSetLabelTextAlignment(label, WACenter);
	WMResizeWidget(label, 500, 35);
	WMAddBoxSubview(vbox, WMWidgetView(label), false, true, 35, 0, 5);

	font = WMSystemFontOfSize(scr, 14);

	/* Unicode samples */
	for (i = 0; unicodeTests[i] != NULL; i++) {
		label = WMCreateLabel(vbox);
		WMSetLabelText(label, unicodeTests[i]);
		WMSetLabelFont(label, font);
		WMResizeWidget(label, 500, 28);
		WMAddBoxSubview(vbox, WMWidgetView(label), false, true, 28, 0, 2);
	}

	WMMapSubwidgets(vbox);
	WMMapSubwidgets(win);
	WMRealizeWidget(win);
	WMMapWidget(win);
}

static WMLabel *makeMarkupLabel(WMWidget *parent, const char *markup,
				int width, int height, bool wrap,
				WMAlignment align, WMFont *font)
{
	WMLabel *label = WMCreateLabel(parent);

	WMSetLabelTextAlignment(label, align);
	WMSetLabelWraps(label, wrap);
	if (font)
		WMSetLabelFont(label, font);
	WMResizeWidget(label, width, height);
	if (markup)
		WMSetLabelMarkup(label, markup);
	else
		WMSetLabelText(label, markup);
	return label;
}

void testMarkup(WMScreen *scr)
{
	WMWindow *win;
	WMBox *vbox;
	WMLabel *label;
	WMFont *font;
	WMPixmap *logo;

	win = WMCreateWindow(scr, "markupTest");
	WMSetWindowTitle(win, "WMLabel Pango Markup Test");
	WMResizeWidget(win, 640, 520);
	WMSetWindowCloseAction(win, NULL, NULL);

	vbox = WMCreateBox(win);
	WMSetBoxHorizontal(vbox, false);
	WMResizeWidget(vbox, 640, 520);

	font = WMSystemFontOfSize(scr, 14);

	/* 1. Plain text regression (markup mode off) */
	label = WMCreateLabel(vbox);
	WMSetLabelText(label, "1. Plain: Hello World (no markup)");
	WMSetLabelFont(label, font);
	WMResizeWidget(label, 640, 25);
	WMAddBoxSubview(vbox, WMWidgetView(label), false, true, 25, 0, 4);

	/* 2. Bold + italic, wrap at small width so span crosses a line */
	label = makeMarkupLabel(vbox,
		"2. <b>Bold</b> and <i>italic</i> wrapped around the line boundary",
		200, 50, true, WALeft, font);
	WMAddBoxSubview(vbox, WMWidgetView(label), false, true, 50, 0, 4);

	/* 3. Per-span foreground colors */
	label = makeMarkupLabel(vbox,
		"3. mix <span foreground=\"red\">red</span> and <span foreground=\"blue\">blue</span>",
		640, 25, false, WALeft, font);
	WMAddBoxSubview(vbox, WMWidgetView(label), false, true, 25, 0, 4);

	/* 4. Mixed font sizes (vertical centering via pixel_size) */
	label = makeMarkupLabel(vbox,
		"4. <span font=\"Sans 24\">big</span> small",
		640, 40, false, WALeft, font);
	WMAddBoxSubview(vbox, WMWidgetView(label), false, true, 40, 0, 4);

	/* 5. Malformed markup - must render verbatim, not blank, emit wwarning */
	label = makeMarkupLabel(vbox,
		"5. a < b > c (malformed, renders verbatim)",
		640, 25, false, WALeft, font);
	WMAddBoxSubview(vbox, WMWidgetView(label), false, true, 25, 0, 4);

	/* 6. noWrap center-aligned mixed weight - alignment parity */
	label = makeMarkupLabel(vbox,
		"6. <b>center</b> <i>noWrap</i> aligned",
		640, 25, false, WACenter, font);
	WMAddBoxSubview(vbox, WMWidgetView(label), false, true, 25, 0, 4);

	/* 7. Image at WIPLeft under markup - text block offsets past image */
	logo = WMCreatePixmapFromFile(scr, "logo.xpm");
	if (logo) {
		label = makeMarkupLabel(vbox,
			"7. <b>image</b> + <i>markup</i> text",
			640, 60, false, WALeft, font);
		WMSetLabelImage(label, logo);
		WMSetLabelImagePosition(label, WIPLeft);
		WMReleasePixmap(logo);
	} else {
		label = makeMarkupLabel(vbox,
			"7. <b>image</b> + <i>markup</i> text (logo.xpm not found)",
			640, 60, false, WALeft, font);
	}
	WMAddBoxSubview(vbox, WMWidgetView(label), false, true, 60, 0, 4);

	WMReleaseFont(font);

	WMMapSubwidgets(vbox);
	WMMapSubwidgets(win);
	WMRealizeWidget(win);
	WMMapWidget(win);
}

int main(int argc, char **argv)
{
	WMScreen *scr;

	WMInitializeApplication("FontTest", &argc, argv);

	/* xcb_connect(NULL, ...) reads $DISPLAY exactly like libX11's
	 * XOpenDisplay("") did */
	xc = xcb_connect(NULL, &sn);
	if (xcb_connection_has_error(xc)) {
		fprintf(stderr, "Could not open display\n");
		exit(1);
	}

	scr = WMCreateScreen(xc, sn);

	printf("\n=================================================\n");
	printf("WINGs Font Rendering Test\n");
	printf("=================================================\n");
	printf("Backend: Cairo + Pango\n");
	printf("=================================================\n\n");

	printf("Creating test windows...\n");
	printf("  1. Main font samples window\n");
	printf("  2. Font size comparison\n");
	printf("  3. Unicode support test\n");
	printf("  4. Pango markup test\n\n");
	printf("Close all windows to exit.\n");
	printf("Observe the smooth antialiased text!\n\n");

	/* Create test windows */
	testMainWindow(scr);
	testSizeComparison(scr);
	testUnicode(scr);
	testMarkup(scr);

	WMScreenMainLoop(scr);

	return 0;
}
