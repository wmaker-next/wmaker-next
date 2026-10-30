/*
 *  wmisc.c
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 1998 scottc
 *  Copyright (c) 1999-2004 Dan Pascu
 *  Copyright (c) 1999-2001 Alfredo K. Kojima
 *  Copyright (c) 2009-2026 Window Maker Team
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

#include <cairo.h>

#include "WINGsP.h"

#include <wraster.h>
#include <ctype.h>

/* One horizontal or vertical device-pixel line. XDrawLine lights the
 * pixel row/column at integer coordinates and includes both endpoints,
 * cairo strokes are centred on the path with square caps that extend
 * half the line width past each end, so shifting the path by 0.5 in
 * both axes lands a width-1 stroke on exactly the same pixels. */
static void hvLine(cairo_t *cr, WMColor *c, int x1, int y1, int x2, int y2)
{
	W_SetSourceColor(cr, c);
	cairo_move_to(cr, x1 + 0.5, y1 + 0.5);
	cairo_line_to(cr, x2 + 0.5, y2 + 0.5);
	cairo_stroke(cr);
}

void
W_DrawRelief(cairo_t *cr, W_Screen *scr, int x, int y,
		  unsigned int width, unsigned int height, WMReliefType relief)
{
	WMColor *bgc, *wgc, *lgc, *dgc;

	cairo_save(cr);
	cairo_set_line_width(cr, 1.0);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);

	switch (relief) {
	case WRSimple:
		W_SetSourceColor(cr, scr->black);
		cairo_rectangle(cr, x + 0.5, y + 0.5, width - 1, height - 1);
		cairo_stroke(cr);
		cairo_restore(cr);
		return;
	case WRRaised:
		bgc = scr->black; dgc = scr->darkGray;
		wgc = scr->white; lgc = scr->gray;
		break;
	case WRSunken:
		wgc = scr->darkGray; lgc = scr->black;
		bgc = scr->white;    dgc = scr->gray;
		break;
	case WRPushed:
		lgc = wgc = scr->black;
		dgc = bgc = scr->white;
		break;
	case WRRidge:
		lgc = bgc = scr->darkGray;
		dgc = wgc = scr->white;
		break;
	case WRGroove:
		wgc = dgc = scr->darkGray;
		lgc = bgc = scr->white;
		break;
	default:
		cairo_restore(cr);
		return;
	}

	/* top left */
	hvLine(cr, wgc, x, y, x + width - 1, y);
	if (width > 2 && relief != WRRaised && relief != WRPushed)
		hvLine(cr, lgc, x + 1, y + 1, x + width - 3, y + 1);

	hvLine(cr, wgc, x, y, x, y + height - 1);
	if (height > 2 && relief != WRRaised && relief != WRPushed)
		hvLine(cr, lgc, x + 1, y + 1, x + 1, y + height - 3);

	/* bottom right */
	hvLine(cr, bgc, x, y + height - 1, x + width - 1, y + height - 1);
	if (width > 2 && relief != WRPushed)
		hvLine(cr, dgc, x + 1, y + height - 2, x + width - 2, y + height - 2);

	hvLine(cr, bgc, x + width - 1, y, x + width - 1, y + height - 1);
	if (height > 2 && relief != WRPushed)
		hvLine(cr, dgc, x + width - 2, y + 1, x + width - 2, y + height - 2);

	cairo_restore(cr);
}

/* Pango starts a new paragraph at several characters the old label
 * code treated as plain text, so replace them with spaces to keep the
 * historical line count. The replacement preserves byte offsets,
 * which W_PaintText uses to slice the layout lines. */
static bool lineSeparatorAt(const char *text, int length, int i, int *extra)
{
	unsigned char c = (unsigned char)text[i];

	*extra = 0;
	if (c == '\r' || c == '\v' || c == '\f')
		return true;
	if (c == 0xc2 && i + 1 < length
	    && (unsigned char)text[i + 1] == 0x85) {
		*extra = 1;
		return true;
	}
	if (c == 0xe2 && i + 2 < length
	    && (unsigned char)text[i + 1] == 0x80
	    && ((unsigned char)text[i + 2] == 0xa8
		|| (unsigned char)text[i + 2] == 0xa9)) {
		*extra = 2;
		return true;
	}
	return false;
}

static char *cleanLineSeparators(const char *text, int length)
{
	char *copy = NULL;
	int i, extra, j;

	for (i = 0; i < length; i++) {
		if (!lineSeparatorAt(text, length, i, &extra))
			continue;
		copy = wmalloc(length + 1);
		memcpy(copy, text, length);
		copy[length] = '\0';
		break;
	}

	if (!copy)
		return NULL;

	for (i = 0; i < length; i++) {
		if (!lineSeparatorAt(copy, length, i, &extra))
			continue;
		for (j = 0; j <= extra; j++)
			copy[i + j] = ' ';
		i += extra;
	}

	return copy;
}

static PangoLayout *setWrappedLayout(WMFont *font, const char *text,
				     int length, int width, int wrap)
{
	PangoLayout *layout = font->wrapLayout;

	if (!layout)
		return NULL;

	if (wrap && width > 0) {
		pango_layout_set_width(layout, width * PANGO_SCALE);
		pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
	} else {
		/* pango wraps per character at width 0 */
		pango_layout_set_width(layout, -1);
	}
	pango_layout_set_text(layout, text, length);

	return layout;
}

/* Pango counts a trailing '\n' as a final empty line, drop it. */
static int wrappedLineCount(PangoLayout *layout, const char *text, int length)
{
	int lines = pango_layout_get_line_count(layout);

	if (length == 0)
		return 0;
	if (lines > 0 && text[length - 1] == '\n')
		lines--;

	return lines;
}

int W_GetTextHeight(WMFont * font, const char *text, int width, int wrap)
{
	PangoLayout *layout;
	char *clean;
	int length;

	wassertrv(font != NULL && text != NULL, 0);

	length = strlen(text);
	clean = cleanLineSeparators(text, length);
	layout = setWrappedLayout(font, clean ? clean : text, length, width, wrap);
	wfree(clean);
	if (!layout)
		return 0;

	return wrappedLineCount(layout, text, length) * WMFontHeight(font);
}

void
W_PaintText(cairo_t *cr, WMFont *font, int x, int y,
		 int width, WMAlignment alignment, WMColor *color, int wrap,
		 const char *text, int length)
{
	PangoLayout *layout;
	PangoLayoutIter *iter;
	char *clean;
	int fheight = WMFontHeight(font);
	int lines, i;

	if (length <= 0)
		return;

	wassertr(font != NULL && text != NULL);

	clean = cleanLineSeparators(text, length);
	if (clean)
		text = clean;

	layout = setWrappedLayout(font, text, length, width, wrap);
	if (!layout) {
		wfree(clean);
		return;
	}

	lines = wrappedLineCount(layout, text, length);

	iter = pango_layout_get_iter(layout);
	for (i = 0; i < lines; i++) {
		PangoLayoutLine *line = pango_layout_iter_get_line_readonly(iter);
		int start = line->start_index;
		int count = line->length;
		int line_width = WMWidthOfString(font, text + start, count);
		int line_x;

		if (alignment == WALeft)
			line_x = x;
		else if (alignment == WARight)
			line_x = x + width - line_width;
		else
			line_x = x + (width - line_width) / 2;

		W_DrawString(cr, color, font, line_x, y, text + start, count);

		y += fheight;
		pango_layout_iter_next_line(iter);
	}
	pango_layout_iter_free(iter);
	wfree(clean);
}

static void
paintTextAndImage(W_View *view, int wrap, WMColor *textColor, W_Font *font,
		  WMReliefType relief, const char *text,
		  WMAlignment alignment, W_Pixmap *image,
		  WMImagePosition position, WMColor *backColor, int ofs,
		  PangoLayout *markupLayout)
{
	W_Screen *screen = view->screen;
	int ix, iy;
	int x, y, w, h;
	int textHeight = 0;
	int blockOfs = 0;
	cairo_t *cr;

	cr = WCairoBeginBuffered(screen, view->window);
	if (!cr)
		return;

	W_PaintViewBackground(cr, view, backColor);

	if (relief == WRFlat) {
		x = 0;
		y = 0;
		w = view->size.width;
		h = view->size.height;
	} else {
		x = 1;
		y = 1;
		w = view->size.width - 3;
		h = view->size.height - 3;
	}

	/* WIPAbove: centre the icon+caption block vertically */
	if (position == WIPAbove && image != NULL) {
		if (markupLayout) {
			if (wrap)
				pango_layout_set_width(markupLayout, (w - 8) * PANGO_SCALE);
			else
				pango_layout_set_width(markupLayout, -1);
			pango_layout_get_pixel_size(markupLayout, NULL, &textHeight);
		} else if (text != NULL) {
			textHeight = W_GetTextHeight(font, text, w - 8, wrap);
		} else {
			textHeight = 0;
		}
		blockOfs = (h - image->height - textHeight) / 2;
		if (blockOfs < 0)
			blockOfs = 0;
	}

	/* calc. image alignment */
	if (position != WIPNoImage && image != NULL) {
		switch (position) {
		case WIPOverlaps:
		case WIPImageOnly:
			ix = (view->size.width - image->width) / 2;
			iy = (view->size.height - image->height) / 2;
			break;

		case WIPLeft:
			ix = x;
			iy = y + (h - image->height) / 2;
			x = x + image->width + 5;
			y = 0;
			w -= image->width + 5;
			break;

		case WIPRight:
			ix = view->size.width - image->width - x;
			iy = y + (h - image->height) / 2;
			w -= image->width + 5;
			break;

		case WIPBelow:
			ix = (view->size.width - image->width) / 2;
			iy = h - image->height;
			y = 0;
			h -= image->height;
			break;

		default:
		case WIPAbove:
			ix = (view->size.width - image->width) / 2;
			iy = y + blockOfs;
			y = iy + image->height;
			h = textHeight;
			break;
		}

		ix += ofs;
		iy += ofs;

		W_PaintPixmap(cr, image, ix, iy);
	}

	/* draw text */
	if (position != WIPImageOnly) {
		if (markupLayout) {
			W_SetSourceColor(cr, textColor);
			if (wrap) {
				PangoAlignment pa;

				if (alignment == WARight)
					pa = PANGO_ALIGN_RIGHT;
				else if (alignment == WACenter)
					pa = PANGO_ALIGN_CENTER;
				else
					pa = PANGO_ALIGN_LEFT;
				pango_layout_set_width(markupLayout, (w - 8) * PANGO_SCALE);
				pango_layout_set_wrap(markupLayout, PANGO_WRAP_WORD);
				pango_layout_set_alignment(markupLayout, pa);
				pango_layout_get_pixel_size(markupLayout, NULL, &textHeight);
				W_ShowLayout(cr, font, markupLayout,
					     x + ofs + 4, y + ofs + (h - textHeight) / 2);
			} else {
				int tw, tx;

				pango_layout_set_width(markupLayout, -1);
				pango_layout_get_pixel_size(markupLayout, &tw, &textHeight);
				if (alignment == WARight)
					tx = x + (w - 8) - tw;
				else if (alignment == WACenter)
					tx = x + ((w - 8) - tw) / 2;
				else
					tx = x;
				W_ShowLayout(cr, font, markupLayout,
					     tx + ofs + 4, y + ofs + (h - textHeight) / 2);
			}
		} else if (text != NULL) {
			textHeight = W_GetTextHeight(font, text, w - 8, wrap);
			W_PaintText(cr, font, x + ofs + 4, y + ofs + (h - textHeight) / 2,
					 w - 8, alignment, textColor, wrap, text, strlen(text));
		}
	}

	/* draw relief */
	W_DrawRelief(cr, screen, 0, 0, view->size.width, view->size.height, relief);

	WCairoEnd(cr);
}

void
W_PaintTextAndImage(W_View *view, int wrap, WMColor *textColor, W_Font *font,
		    WMReliefType relief, const char *text,
		    WMAlignment alignment, W_Pixmap *image,
		    WMImagePosition position, WMColor *backColor, int ofs)
{
	paintTextAndImage(view, wrap, textColor, font, relief, text,
			  alignment, image, position, backColor, ofs, NULL);
}

void
W_PaintTextAndImageMarkup(W_View *view, int wrap, WMColor *textColor,
			  W_Font *font, WMReliefType relief,
			  WMAlignment alignment, W_Pixmap *image,
			  WMImagePosition position, WMColor *backColor, int ofs,
			  PangoLayout *markupLayout)
{
	paintTextAndImage(view, wrap, textColor, font, relief, NULL,
			  alignment, image, position, backColor, ofs, markupLayout);
}

WMPoint wmkpoint(int x, int y)
{
	WMPoint point;

	point.x = x;
	point.y = y;

	return point;
}

WMSize wmksize(unsigned int width, unsigned int height)
{
	WMSize size;

	size.width = width;
	size.height = height;

	return size;
}

WMRect wmkrect(int x, int y, unsigned int width, unsigned int height)
{
	WMRect rect;

	rect.pos.x = x;
	rect.pos.y = y;
	rect.size.width = width;
	rect.size.height = height;

	return rect;
}
