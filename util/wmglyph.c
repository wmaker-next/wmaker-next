/*
 *  wmglyph - lightweight Unicode glyph selector
 *
 *  Emoji-picker-style UI: search field on top, category dropdown, a
 *  scrollable painted glyph grid in the middle, and a status bar at the
 *  bottom. Click a glyph to copy it to the X selection (middle-click or
 *  Ctrl+V pastes it elsewhere). The grid is a single custom-painted
 *  WMFrame (one X window for the whole table) so it scales to the full
 *  Unicode range without one window per cell.
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  The Unicode character/block data in wmglyph_data.{c,h} is derived from
 *  UnicodeData.txt and Blocks.txt (c) 1991-2026 Unicode, Inc., distributed
 *  under the Unicode License v3 — see util/wmglyph-data/UNICODE-LICENSE.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#ifdef __GLIBC__
#define _GNU_SOURCE		/* getopt_long */
#endif

#include <config.h>

#include <getopt.h>

#include <WINGs/WINGs.h>
#include <WINGs/WUtil.h>
#include <WINGs/WINGsP.h>

#include <pango/pango.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdnoreturn.h>

#include "wmglyph-data/wmglyph_data.h"
#include "wmglyph_icon.h"
#include "xcconn.h"

#define WIN_W (25 * CELL_W + 23)	/* 25 columns + scrollview relief(3) + vscroller(20) */
#define WIN_H 480
#define CELL_W 28
#define CELL_H 34
#define SEARCH_H 24
#define CATEGORY_H 28
#define STATUS_H 26
#define FONT_SPEC "Sans:pixelsize=20"

/* Cairo's XCB surface tops out around 32768px, so keep the grid
 * content at or below this and paginate wider result sets. */
#define MAX_GRID_H 32000
#define ROWS_PER_PAGE (MAX_GRID_H / CELL_H)
#define PAGE_BTN_W 64

typedef struct {
	xcb_connection_t *conn;
	WMScreen *scr;

	WMWindow *win;
	WMBox *vbox;
	WMTextField *search;
	WMPopUpButton *category;
	WMScrollView *scroll;
	WMFrame *grid;
	WMBox *statusBox;
	WMLabel *status;
	WMButton *prevBtn;
	WMButton *nextBtn;

	WMFont *font;
	WMFont *smallFont;
	WMColor *fg;
	WMColor *selFg;
	WMColor *cellBg;
	WMColor *panelBg;
	WMColor *selBg;
	WMColor *lineColor;

	xcb_atom_t utf8Atom;
	xcb_atom_t targetsAtom;
	xcb_atom_t clipboardAtom;

	char selBuf[8];
	int selLen;
	bool ownsSel;
	int selectedCp;

	int currentBlock;
	char searchQuery[64];

	int *visibleIdx;
	int visibleCount;
	int gridCols;
	int page;
	int pageSize;
} GlyphApp;

static int utf8_encode(uint32_t cp, char *buf)
{
	if (cp < 0x80u) {
		buf[0] = (char)cp;
		return 1;
	} else if (cp < 0x800u) {
		buf[0] = (char)(0xC0u | (cp >> 6));
		buf[1] = (char)(0x80u | (cp & 0x3Fu));
		return 2;
	} else if (cp < 0x10000u) {
		buf[0] = (char)(0xE0u | (cp >> 12));
		buf[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
		buf[2] = (char)(0x80u | (cp & 0x3Fu));
		return 3;
	} else {
		buf[0] = (char)(0xF0u | (cp >> 18));
		buf[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
		buf[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
		buf[3] = (char)(0x80u | (cp & 0x3Fu));
		return 4;
	}
}

static uint32_t utf8_decode(const char *buf, int len)
{
	switch (len) {
	case 1:
		return (unsigned char)buf[0];
	case 2:
		return ((uint32_t)(unsigned char)buf[0] & 0x1Fu) << 6
		       | ((unsigned char)buf[1] & 0x3Fu);
	case 3:
		return ((uint32_t)(unsigned char)buf[0] & 0x0Fu) << 12
		       | ((unsigned char)buf[1] & 0x3Fu) << 6
		       | ((unsigned char)buf[2] & 0x3Fu);
	default:
		return ((uint32_t)(unsigned char)buf[0] & 0x07u) << 18
		       | ((unsigned char)buf[1] & 0x3Fu) << 12
		       | ((unsigned char)buf[2] & 0x3Fu) << 6
		       | ((unsigned char)buf[3] & 0x3Fu);
	}
}

static WMData *convertSelection(WMView *view, xcb_atom_t selection, xcb_atom_t target,
				void *cdata, xcb_atom_t *type)
{
	GlyphApp *app = (GlyphApp *)cdata;
	WMData *data;
	/* format-32 property payload: selection.c sizes format-32
	 * items as sizeof(long) (the LP64 contract) */
	unsigned long atoms[3];
	uint32_t cp;
	char latin1;

	(void)view;
	(void)selection;

	if (target == app->utf8Atom) {
		data = WMCreateDataWithBytes(app->selBuf, (unsigned int)app->selLen);
		WMSetDataFormat(data, 8);
		*type = target;
		return data;
	}
	if (target == XCB_ATOM_STRING) {
		/* The ICCCM defines XCB_ATOM_STRING as Latin-1. Refuse
		 * codepoints above U+00FF instead of serving mojibake.
		 * The requestor gets a conversion failure and can ask
		 * for UTF8_STRING instead. */
		cp = utf8_decode(app->selBuf, app->selLen);
		if (cp > 0xFFu)
			return NULL;
		latin1 = (char)cp;
		data = WMCreateDataWithBytes(&latin1, 1);
		WMSetDataFormat(data, 8);
		*type = XCB_ATOM_STRING;
		return data;
	}
	if (target == app->targetsAtom) {
		atoms[0] = app->targetsAtom;
		atoms[1] = app->utf8Atom;
		atoms[2] = XCB_ATOM_STRING;
		data = WMCreateDataWithBytes(atoms, sizeof(atoms));
		WMSetDataFormat(data, 32);
		*type = XCB_ATOM_ATOM;
		return data;
	}
	return NULL;
}

static void selectionLost(WMView *view, xcb_atom_t selection, void *cdata)
{
	(void)view;
	(void)selection;
	((GlyphApp *)cdata)->ownsSel = false;
}

static WMSelectionProcs selProcs = {
	convertSelection, selectionLost, NULL
};

static void refreshGrid(GlyphApp *app);

static void rebuildVisible(GlyphApp *app)
{
	app->visibleCount = 0;
	int qLen = (int)strlen(app->searchQuery);

	for (int i = 0; i < wmglyph_char_count; i++) {
		const struct WmglyphChar *c = &wmglyph_chars[i];
		bool match = true;
		if (qLen > 0) {
			match = (strcasestr(c->name, app->searchQuery) != NULL);
		} else if (app->currentBlock >= 0) {
			match = (c->block == (uint16_t)app->currentBlock);
		}
		if (match) {
			app->visibleIdx[app->visibleCount++] = i;
		}
	}
}

static int pageEntryCount(GlyphApp *app)
{
	int start = app->page * app->pageSize;
	int n = app->visibleCount - start;

	if (app->pageSize < 1 || n <= 0)
		return 0;
	return (n < app->pageSize) ? n : app->pageSize;
}

static void updatePageUI(GlyphApp *app)
{
	int pageCount = 1;
	int start;
	char msg[192];

	if (app->pageSize > 0 && app->visibleCount > 0)
		pageCount = (app->visibleCount + app->pageSize - 1) / app->pageSize;

	WMSetButtonEnabled(app->prevBtn, app->page > 0);
	WMSetButtonEnabled(app->nextBtn, app->page + 1 < pageCount);

	if (app->visibleCount == 0) {
		WMSetLabelText(app->status, "No matching characters.");
		return;
	}
	if (pageCount == 1) {
		WMSetLabelText(app->status, "Click a glyph to copy it to the selection.");
		return;
	}
	start = app->page * app->pageSize;
	snprintf(msg, sizeof(msg),
		"Page %d of %d: showing %d to %d of %d characters. Click a glyph to copy.",
		app->page + 1, pageCount, start + 1, start + pageEntryCount(app),
		app->visibleCount);
	WMSetLabelText(app->status, msg);
}

static int gridHeightFor(GlyphApp *app, int gridW)
{
	int cols = gridW / CELL_W;
	int rows;

	if (cols < 1) cols = 1;
	app->gridCols = cols;
	app->pageSize = ROWS_PER_PAGE * cols;

	/* A resize can shrink the page capacity, so keep the page in
	 * range and the tail of the result set reachable. */
	if (app->visibleCount > 0 && app->page * app->pageSize >= app->visibleCount)
		app->page = (app->visibleCount - 1) / app->pageSize;

	rows = (pageEntryCount(app) + cols - 1) / cols;
	int h = rows * CELL_H;
	if (h > MAX_GRID_H)
		h = MAX_GRID_H;
	return h;
}

static void paintGrid(xcb_generic_event_t *event, void *data)
{
	GlyphApp *app = (GlyphApp *)data;
	if ((event->response_type & 0x7f) != XCB_EXPOSE)
		return;

	/* Resize the content to fit the scroll on first paint, once the
	 * scrollview has been realized and has a real size. */
	if (app->gridCols == 0) {
		refreshGrid(app);
	}

	xcb_drawable_t d = WMWidgetXID(app->grid);
	int cols = app->gridCols;
	if (cols < 1) cols = 1;
	int pageStart = app->page * app->pageSize;
	int pageEntries = pageEntryCount(app);

	/* Only paint the visible portion — the grid content can be
	 * thousands of pixels tall for large blocks (CJK has 20k+). */
	WMRect vis = WMGetScrollViewVisibleRect(app->scroll);
	int visY = vis.pos.y;
	int visH = vis.size.height;
	WMPaintColorSwatch(app->panelBg, d, vis.pos.x, visY,
			   (unsigned)vis.size.width, (unsigned)visH);
	int firstRow = visY / CELL_H;
	int lastRow = (visY + visH) / CELL_H + 1;
	int totalRows = (pageEntries + cols - 1) / cols;
	if (lastRow > totalRows) lastRow = totalRows;

	/* Paint all cell backgrounds first, then all glyphs. */
	for (int row = firstRow; row <= lastRow; row++) {
		for (int col = 0; col < cols; col++) {
			int idx = row * cols + col;
			if (idx >= pageEntries)
				break;
			int charIdx = app->visibleIdx[pageStart + idx];
			uint32_t cp = wmglyph_chars[charIdx].cp;

			int cx = col * CELL_W;
			int cy = row * CELL_H;

			WMColor *bg = (cp == (uint32_t)app->selectedCp) ? app->selBg : app->cellBg;
			WMPaintColorSwatch(bg, d, cx + 1, cy + 1, CELL_W - 2, CELL_H - 2);
		}
	}

	for (int row = firstRow; row <= lastRow; row++) {
		for (int col = 0; col < cols; col++) {
			int idx = row * cols + col;
			if (idx >= pageEntries)
				break;
			int charIdx = app->visibleIdx[pageStart + idx];
			uint32_t cp = wmglyph_chars[charIdx].cp;

			int cx = col * CELL_W;
			int cy = row * CELL_H;

			char buf[8];	/* up to 3 (U+25CC) + 4 (glyph) + NUL */
			int len;
			if (wmglyph_chars[charIdx].combining) {
				/* Combining marks have zero advance width and
				 * need a base character to attach to. Prepend
				 * U+25CC DOTTED CIRCLE for display only — the
				 * selection buffer keeps just the mark, so the
				 * user pastes it after a real base character. */
				static const char dottedCircle[] = "\xE2\x97\x8C";
				memcpy(buf, dottedCircle, 3);
				len = utf8_encode(cp, buf + 3) + 3;
			} else {
				len = utf8_encode(cp, buf);
			}
			int tw = WMWidthOfString(app->font, buf, len);
			int tx = cx + (CELL_W - tw) / 2;

			/* Query the Pango layout's ink rectangle to center the
			 * actual rendered pixels, not the logical extent, so
			 * glyphs with deep descenders (Arabic, Devanagari, …)
			 * stay within the tile. */
			PangoRectangle ink;
			pango_layout_set_text(app->font->layout, buf, len);
			pango_layout_get_pixel_extents(app->font->layout, &ink, NULL);
			int inkH = ink.height;
			int inkY = cy + (CELL_H - inkH) / 2 - ink.y;

		WMColor *fg = (cp == (uint32_t)app->selectedCp) ? app->selFg : app->fg;
		/* The cell background was already painted by the swatch loop
		 * above; use WMDrawString (not WMDrawImageString) so the
		 * library does not fill a tw*font->height bg rect that would
		 * overflow the 1px tile border into neighbouring cells. */
		WMDrawString(app->scr, d, fg, app->font, tx, inkY, buf, len);
		}
	}
}

static void onButtonPress(xcb_generic_event_t *event, void *data)
{
	GlyphApp *app = (GlyphApp *)data;
	if ((event->response_type & 0x7f) != XCB_BUTTON_PRESS)
		return;

	/* Mouse wheel scrolls the grid (buttons 4/5). */
	if (((xcb_button_press_event_t *)event)->detail == 4
	    || ((xcb_button_press_event_t *)event)->detail == 5) {
		int contentH = (int)WMWidgetHeight(app->grid);
		WMRect vis = WMGetScrollViewVisibleRect(app->scroll);
		int maxScroll = contentH - vis.size.height;
		if (maxScroll <= 0)
			return;
		int dy = (((xcb_button_press_event_t *)event)->detail == 5) ? CELL_H * 3 : -CELL_H * 3;
		int newY = vis.pos.y + dy;
		if (newY < 0)
			newY = 0;
		if (newY > maxScroll)
			newY = maxScroll;
		WMScrollViewScrollPoint(app->scroll, wmkpoint(0, newY));
		return;
	}

	int x = ((xcb_button_press_event_t *)event)->event_x;
	int y = ((xcb_button_press_event_t *)event)->event_y;
	int cols = app->gridCols;
	if (cols < 1) cols = 1;
	int pageStart = app->page * app->pageSize;
	int pageEntries = pageEntryCount(app);
	int idx = (y / CELL_H) * cols + (x / CELL_W);

	if (idx < 0 || idx >= pageEntries) {
		app->selectedCp = -1;
	} else {
		int charIdx = app->visibleIdx[pageStart + idx];
		uint32_t cp = wmglyph_chars[charIdx].cp;
		app->selectedCp = (int)cp;
		app->selLen = utf8_encode(cp, app->selBuf);
		app->selBuf[app->selLen] = '\0';

		/* Drop the handlers from an earlier claim before
		 * re-asserting ownership, or they pile up and every
		 * paste gets served once per stale handler. */
		WMView *view = WMWidgetView(app->grid);
		if (app->ownsSel) {
			WMDeleteSelectionHandler(view, XCB_ATOM_PRIMARY, XCB_CURRENT_TIME);
			WMDeleteSelectionHandler(view, app->clipboardAtom, XCB_CURRENT_TIME);
			app->ownsSel = false;
		}
		if (WMCreateSelectionHandler(view, XCB_ATOM_PRIMARY, ((xcb_button_press_event_t *)event)->time, &selProcs, app)
		    && WMCreateSelectionHandler(view, app->clipboardAtom, ((xcb_button_press_event_t *)event)->time, &selProcs, app)) {
			app->ownsSel = true;
		}

		const char *blockName = wmglyph_blocks[wmglyph_chars[charIdx].block].name;
		char *escBlock = g_markup_escape_text(blockName, -1);
		char *escBuf = g_markup_escape_text(app->selBuf, app->selLen);
		char *escName = g_markup_escape_text(wmglyph_chars[charIdx].name, -1);
		char msg[512];
		snprintf(msg, sizeof(msg),
		         "<b>%s</b>  U+%04X  %s  %s   (copied)",
		         escBlock, cp, escBuf, escName);
		WMSetLabelText(app->status, msg);
		g_free(escBlock);
		g_free(escBuf);
		g_free(escName);
	}

	wXClearArea(app->conn, WMWidgetXID(app->grid), 0, 0, 0, 0, true);
}

static void refreshGrid(GlyphApp *app)
{
	/* Size the grid (content view) to the viewport interior and the
	 * full content height. WMResizeScrollViewContent() is not used here
	 * because it resizes the scrollview *itself* (view + viewport +
	 * scrollers) to wrap the content — our scrollview is box-laid-out
	 * at a fixed size, so only the content view should change. The
	 * scrollview catches the content view's ConfigureNotify via its
	 * SubstructureNotifyMask handler and updates the scroller
	 * proportion automatically. */
	WMRect vis = WMGetScrollViewVisibleRect(app->scroll);
	int gridW = vis.size.width;
	int visH = vis.size.height;
	if (gridW <= 0 || visH <= 0)
		return;
	int contentH = gridHeightFor(app, gridW);
	if (contentH < visH)
		contentH = visH;
	WMResizeWidget(app->grid, (unsigned)gridW, (unsigned)contentH);
	wXClearArea(app->conn, WMWidgetXID(app->grid), 0, 0, 0, 0, true);
}

static void changePage(GlyphApp *app, int delta)
{
	int maxPage = 0;

	if (app->pageSize > 0 && app->visibleCount > 0)
		maxPage = (app->visibleCount - 1) / app->pageSize;

	app->page += delta;
	if (app->page > maxPage)
		app->page = maxPage;
	if (app->page < 0)
		app->page = 0;

	WMScrollViewScrollPoint(app->scroll, wmkpoint(0, 0));
	refreshGrid(app);
	updatePageUI(app);
}

static void onPagePrev(WMWidget *self, void *data)
{
	(void)self;
	changePage((GlyphApp *)data, -1);
}

static void onPageNext(WMWidget *self, void *data)
{
	(void)self;
	changePage((GlyphApp *)data, 1);
}

static void onCategoryChanged(WMWidget *self, void *data)
{
	GlyphApp *app = (GlyphApp *)data;
	(void)self;
	app->currentBlock = WMGetPopUpButtonSelectedItem(app->category);
	app->searchQuery[0] = '\0';
	WMSetTextFieldText(app->search, "");
	app->selectedCp = -1;
	app->page = 0;
	rebuildVisible(app);
	refreshGrid(app);
	updatePageUI(app);
}

static void onSearchChanged(void *observer, WMNotification *notification)
{
	GlyphApp *app = (GlyphApp *)observer;
	(void)notification;
	char *text = WMGetTextFieldText(app->search);
	strncpy(app->searchQuery, text ? text : "", sizeof(app->searchQuery) - 1);
	app->searchQuery[sizeof(app->searchQuery) - 1] = '\0';
	if (text) wfree(text);
	app->page = 0;
	rebuildVisible(app);
	refreshGrid(app);
	updatePageUI(app);
}

static void onScrollViewResized(void *observer, WMNotification *notification)
{
	GlyphApp *app = (GlyphApp *)observer;
	(void)notification;
	refreshGrid(app);
	updatePageUI(app);
}

static noreturn void quit(WMWidget *self, void *data)
{
	GlyphApp *app = data;

	(void)self;
	if (app && app->visibleIdx)
		free(app->visibleIdx);
	exit(0);
}

static const char *prog_name;

static noreturn void print_help(int print_usage, int exitval)
{
	printf("Usage: %s [-h] [-v]\n", prog_name);
	if (print_usage) {
		puts("Lightweight Unicode glyph selector. Click a character to copy it");
		puts("to the X selection.");
		puts("");
		puts("  -h, --help     display this help and exit");
		puts("  -v, --version  output version information and exit");
	}
	exit(exitval);
}

int main(int argc, char **argv)
{
	GlyphApp app;
	RImage *icon;
	memset(&app, 0, sizeof app);
	app.selectedCp = -1;
	app.currentBlock = 0;

	prog_name = argv[0];

	static struct option longopts[] = {
		{ "help",    no_argument, NULL, 'h' },
		{ "version", no_argument, NULL, 'v' },
		{ NULL,      0,           NULL,  0  }
	};
	int ch;
	while ((ch = getopt_long(argc, argv, "hv", longopts, NULL)) != -1) {
		switch (ch) {
			case 'v':
				printf("%s (Window Maker %s)\n", prog_name, VERSION);
				return 0;
			case 'h':
				print_help(1, 0);
			default:
				print_help(0, 1);
		}
	}

	WMInitializeApplication("wmglyph", &argc, argv);

	xcb_connection_t *xc;
	int sn;

	xc = xcconn_open(NULL, "wmglyph", 1, &sn);
	app.conn = xc;
	app.scr = WMCreateScreen(xc, sn);

	/* Application icon: load the embedded XPM through wraster and set
	 * it both as the WINGs appicon (WMHints on the group leader, used by
	 * the dock/clip and WINGs panels) and as the miniwindow image
	 * (_NET_WM_ICON on the main window, used by EWMH taskbars). */
	icon = RGetImageFromXPMData(WMScreenRContext(app.scr), wmglyph_xpm);
	if (icon) {
		WMSetApplicationIconImage(app.scr, icon);
		RReleaseImage(icon);
	}

	app.utf8Atom = wx_static_atoms[W_ATOM_UTF8_STRING];
	app.targetsAtom = wx_static_atoms[W_ATOM_TARGETS];
	app.clipboardAtom = wx_static_atoms[W_ATOM_CLIPBOARD];

	app.fg = WMBlackColor(app.scr);
	app.cellBg = WMWhiteColor(app.scr);
	app.panelBg = WMGrayColor(app.scr);
	if (W_getconf_selectedBackColor()) {
		app.selBg = W_SelectedBackColor(app.scr);
		app.selFg = WMBlackColor(app.scr);
	} else {
		app.selBg = WMDarkGrayColor(app.scr);
		app.selFg = WMWhiteColor(app.scr);
	}
	app.lineColor = WMDarkGrayColor(app.scr);

	app.font = WMCreateFont(app.scr, FONT_SPEC);
	if (!app.font)
		app.font = WMSystemFontOfSize(app.scr, 16);
	app.smallFont = WMSystemFontOfSize(app.scr, 11);

	app.visibleIdx = malloc(sizeof(int) * wmglyph_char_count);
	if (!app.visibleIdx) {
		fprintf(stderr, "wmglyph: out of memory\n");
		exit(1);
	}

	app.win = WMCreateWindow(app.scr, "wmglyph");
	WMResizeWidget(app.win, WIN_W, WIN_H);
	WMSetWindowTitle(app.win, "wmglyph");
	WMSetWindowCloseAction(app.win, quit, &app);

	app.vbox = WMCreateBox(app.win);
	WMSetBoxHorizontal(app.vbox, false);
	WMResizeWidget(app.vbox, WIN_W, WIN_H);
	WMSetViewExpandsToParent(WMWidgetView(app.vbox), 0, 0, 0, 0);

	/* Search field */
	app.search = WMCreateTextField(app.vbox);
	WMSetTextFieldText(app.search, "");
	WMSetTextFieldPlaceholder(app.search, "Search glyphs");
	WMAddBoxSubview(app.vbox, WMWidgetView(app.search), false, true, SEARCH_H, SEARCH_H, 2);

	/* Category dropdown */
	app.category = WMCreatePopUpButton(app.vbox);
	for (int i = 0; i < wmglyph_block_count; i++) {
		WMAddPopUpButtonItem(app.category, wmglyph_blocks[i].name);
	}
	WMSetPopUpButtonAction(app.category, onCategoryChanged, &app);
	WMAddBoxSubview(app.vbox, WMWidgetView(app.category), false, true, CATEGORY_H, CATEGORY_H, 2);

	/* Scrollable grid */
	app.scroll = WMCreateScrollView(app.vbox);
	WMSetScrollViewRelief(app.scroll, WRSunken);
	WMSetScrollViewHasVerticalScroller(app.scroll, true);
	WMSetScrollViewHasHorizontalScroller(app.scroll, false);

	app.grid = WMCreateFrame(app.vbox);
	WMSetFrameRelief(app.grid, WRFlat);
	WMSetWidgetBackgroundColor(app.grid, app.panelBg);
	WMResizeWidget(app.grid, 100, 100);
	WMMapWidget(app.grid);
	WMSetScrollViewContentView(app.scroll, WMWidgetView(app.grid));
	WMAddBoxSubview(app.vbox, WMWidgetView(app.scroll), true, true, 0, 0, 0);

	/* Status bar with pager buttons */
	app.statusBox = WMCreateBox(app.vbox);
	WMSetBoxHorizontal(app.statusBox, true);

	app.status = WMCreateLabel(app.statusBox);
	WMSetLabelRelief(app.status, WRSunken);
	WMSetLabelFont(app.status, app.smallFont);
	WMSetLabelText(app.status, "Click a glyph to copy it to the selection.");
	WMSetLabelUseMarkup(app.status, true);
	WMAddBoxSubview(app.statusBox, WMWidgetView(app.status), true, true, 0, 0, 0);

	app.prevBtn = WMCreateCommandButton(app.statusBox);
	WMSetButtonText(app.prevBtn, "< Prev");
	WMSetButtonAction(app.prevBtn, onPagePrev, &app);
	WMSetButtonEnabled(app.prevBtn, false);
	WMAddBoxSubview(app.statusBox, WMWidgetView(app.prevBtn), false, true, PAGE_BTN_W, PAGE_BTN_W, 1);

	app.nextBtn = WMCreateCommandButton(app.statusBox);
	WMSetButtonText(app.nextBtn, "Next >");
	WMSetButtonAction(app.nextBtn, onPageNext, &app);
	WMSetButtonEnabled(app.nextBtn, false);
	WMAddBoxSubview(app.statusBox, WMWidgetView(app.nextBtn), false, true, PAGE_BTN_W, PAGE_BTN_W, 1);

	WMAddBoxSubview(app.vbox, WMWidgetView(app.statusBox), false, true, STATUS_H, STATUS_H, 2);

	WMCreateEventHandler(WMWidgetView(app.grid), XCB_EVENT_MASK_EXPOSURE, paintGrid, &app);
	WMCreateEventHandler(WMWidgetView(app.grid), XCB_EVENT_MASK_BUTTON_PRESS, onButtonPress, &app);

	WMAddNotificationObserver(onSearchChanged, &app, WMTextDidChangeNotification, app.search);
	WMAddNotificationObserver(onScrollViewResized, &app, WMViewSizeDidChangeNotification, WMWidgetView(app.scroll));

	/* Populate initial visible set (first non-empty block) */
	WMSetPopUpButtonSelectedItem(app.category, 0);
	app.currentBlock = 0;
	app.gridCols = 0;
	rebuildVisible(&app);

	WMRealizeWidget(app.win);
	WMSetWindowMiniwindowImage(app.win, WMGetApplicationIconImage(app.scr));
	WMMapSubwidgets(app.vbox);
	WMMapSubwidgets(app.statusBox);
	WMMapSubwidgets(app.win);
	WMMapWidget(app.win);

	/* Focus the search field so the user can start typing immediately. */
	WMSetFocusToWidget(app.search);

	/* Force an initial layout now that the scrollview has a real size. */
	refreshGrid(&app);
	updatePageUI(&app);

	WMScreenMainLoop(app.scr);
}
