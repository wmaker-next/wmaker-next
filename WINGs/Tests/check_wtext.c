/*
 *  check_wtext.c
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
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <WINGs/WINGs.h>
#include "check_common.h"

static void check_alignment(WMScreen *screen, WMAlignment alignment)
{
	static char *image[] = {
		"17 9 2 1",
		"  c white",
		". c black",
		".................",
		".               .",
		".               .",
		".               .",
		".               .",
		".               .",
		".               .",
		".               .",
		"................."
	};
	WMWindow *window;
	WMText *text;
	WMFrame *object;
	WMFont *font, *other_font;
	WMPixmap *pixmap;
	WMColor *color;
	WMPoint position;
	void *block;
	int widths[6], line_start, object_line_start, object_line_end;
	int visible_width, line_width, offset, expected_x, i, pass;

	window = WMCreateWindow(screen, "check-wtext");
	WMResizeWidget(window, 520, 200);
	text = WMCreateText(window);
	WMResizeWidget(text, 510, 180);
	font = WMCreateFont(screen, "sans:pixelsize=14");
	other_font = WMCreateFont(screen, "serif:pixelsize=17");
	pixmap = WMCreatePixmapFromXPMData(screen, image);
	CHECK(font != NULL && other_font != NULL && pixmap != NULL);
	if (!font || !other_font || !pixmap)
		exit(1);
	color = WMBlackColor(screen);
	object = WMCreateFrame(text);
	WMResizeWidget(object, 13, 12);
	WMSetTextAlignment(text, alignment);
	WMRealizeWidget(window);
	WMResizeWidget(text, 500, 180);

	WMFreezeText(text);
	block = WMCreateTextBlockWithText(text, "AV office ", font, color, 1, 10);
	WMSetTextBlockProperties(text, block, 1, 0, 1, 0, NULL);
	WMAppendTextBlock(text, block);
	block = WMCreateTextBlockWithText(text, "lambda ", other_font, color, 0, 7);
	WMAppendTextBlock(text, block);
	block = WMCreateTextBlockWithPixmap(text, pixmap, "image", color, 0, 0);
	WMAppendTextBlock(text, block);
	block = WMCreateTextBlockWithObject(text, object, "object", color, 0, 0);
	WMAppendTextBlock(text, block);
	block = WMCreateTextBlockWithText(text, "tail", font, color, 0, 4);
	WMAppendTextBlock(text, block);
	WMThawText(text);

	widths[0] = WMWidthOfString(font, "AV ", 3);
	widths[1] = WMWidthOfString(font, "office ", 7);
	widths[2] = WMWidthOfString(other_font, "lambda ", 7);
	widths[3] = 17;
	widths[4] = 13;
	widths[5] = WMWidthOfString(font, "tail", 4);
	for (pass = 0; pass < 2; pass++) {
		if (pass)
			WMResizeWidget(text, 95, 180);
		visible_width = (pass ? 95 : 500) - 12;
		line_start = 0;
		line_width = 0;
		object_line_start = object_line_end = 0;
		for (i = 0; i < 6; i++) {
			if (line_width + widths[i] >= visible_width) {
				if (line_start <= 4 && i > 4)
					object_line_end = i;
				line_start = i;
				line_width = 0;
			}
			line_width += widths[i];
			if (i == 4)
				object_line_start = line_start;
		}
		if (!object_line_end)
			object_line_end = 6;
		line_width = 0;
		for (i = object_line_start; i < object_line_end; i++)
			line_width += widths[i];
		offset = alignment == WARight ? visible_width - line_width :
		    alignment == WACenter ? (visible_width - line_width) / 2 : 0;
		expected_x = 4 + offset;
		for (i = object_line_start; i < 4; i++)
			expected_x += widths[i];
		position = WMGetViewPosition(WMWidgetView(object));
		if (position.x != expected_x) {
			fprintf(stderr, "FAIL alignment %u, width %d: object x=%d, expected %d\n",
				alignment, visible_width, position.x, expected_x);
			check_failures++;
		}
	}

	/* Clear embedded objects before their child views are destroyed. */
	WMClearText(text);
	WMDestroyWidget(window);
	WMReleasePixmap(pixmap);
	WMReleaseFont(other_font);
	WMReleaseFont(font);
}

static void check_monofont(WMScreen *screen)
{
	const char *first = "AV office ";
	const char *second = "caf\xc3\xa9 text for wrapping";
	WMWindow *window;
	WMText *text;
	WMFont *font, *other_font;
	WMColor *color;
	void *block;
	char *stream;

	window = WMCreateWindow(screen, "check-wtext-mono");
	text = WMCreateText(window);
	WMResizeWidget(text, 95, 180);
	font = WMCreateFont(screen, "sans:pixelsize=14");
	other_font = WMCreateFont(screen, "serif:pixelsize=17");
	CHECK(font != NULL && other_font != NULL);
	if (!font || !other_font)
		exit(1);
	color = WMBlackColor(screen);
	WMSetTextDefaultFont(text, font);
	WMSetTextUsesMonoFont(text, true);
	WMSetTextAlignment(text, WARight);
	WMRealizeWidget(window);
	WMResizeWidget(text, 96, 180);

	WMFreezeText(text);
	block = WMCreateTextBlockWithText(text, first, other_font, color, 1, strlen(first));
	WMAppendTextBlock(text, block);
	block = WMCreateTextBlockWithText(text, second, other_font, color, 0, strlen(second));
	WMAppendTextBlock(text, block);
	WMThawText(text);
	stream = WMGetTextStream(text);
	CHECK_STR(stream, "AV office caf\xc3\xa9 text for wrapping");
	wfree(stream);

	WMClearText(text);
	WMDestroyWidget(window);
	WMReleaseFont(other_font);
	WMReleaseFont(font);
}

int main(int argc, char **argv)
{
	xcb_connection_t *connection;
	WMScreen *screen;
	int screen_number;

	WMInitializeApplication("CheckWText", &argc, argv);
	connection = xcb_connect(NULL, &screen_number);
	if (xcb_connection_has_error(connection)) {
		fprintf(stderr, "SKIP: no display\n");
		xcb_disconnect(connection);
		return 77;
	}
	screen = WMCreateScreen(connection, screen_number);
	if (!screen) {
		fprintf(stderr, "FAIL: cannot create WINGs screen\n");
		xcb_disconnect(connection);
		return 1;
	}
	check_alignment(screen, WALeft);
	check_alignment(screen, WACenter);
	check_alignment(screen, WARight);
	check_monofont(screen);
	WMShutdownScreen(screen);
	WMShutdownFonts();
	wutil_shutdown();
	xcb_disconnect(connection);
	return check_report("wtext layout");
}
