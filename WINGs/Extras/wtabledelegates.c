/*
 *  wtabledelegates.c
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 2001 Alfredo K. Kojima
 *  Copyright (c) 2001-2004 Dan Pascu
 *  Copyright (c) 2008-2026 Window Maker Team
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

#include <stdint.h>
#include <stdbool.h>

#include <cairo.h>
#include <WINGs/WINGsP.h>

#include "wtableview.h"

#include "wtabledelegates.h"

typedef struct {
	WMTableView *table;
	WMFont *font;
	WMColor *back;
	WMColor *selBack;
	WMColor *textColor;
} StringData;

typedef struct {
	WMTableView *table;
	WMColor *selBack;
} PixmapData;

typedef struct {
	WMTextField *widget;
	WMTableView *table;
	WMFont *font;
	WMColor *back;
	WMColor *selBack;
	WMColor *textColor;
} StringEditorData;

typedef struct {
	WMPopUpButton *widget;
	WMTableView *table;
	WMFont *font;
	char **options;
	int count;
	WMColor *back;
	WMColor *selBack;
	WMColor *textColor;
} EnumSelectorData;

typedef struct {
	WMButton *widget;
	WMTableView *table;
	bool state;
	WMColor *back;
	WMColor *selBack;
} BooleanSwitchData;

static const char *selectionColorName(void)
{
	return WINGsConfiguration.selectionColor ? WINGsConfiguration.selectionColor : "#bbbbcc";
}

/* one cell interior inset by one pixel from the grid line, filled in
 * the row-state background then clipped so text or the pixmap glyph
 * cannot overrun into the neighbouring cell */
static cairo_t *cellBegin(WMScreen *scr, xcb_drawable_t d, WMColor *back,
			  WMColor *selBack, WMRect rect, bool selected)
{
	cairo_t *cr;

	cr = WCairoBeginForDrawable(scr, d);
	if (!cr)
		return NULL;
	cairo_rectangle(cr, rect.pos.x + 1, rect.pos.y + 1,
			rect.size.width - 1, rect.size.height - 1);
	W_SetSourceColor(cr, selected ? selBack : back);
	cairo_fill_preserve(cr);
	cairo_clip(cr);
	return cr;
}

static void
stringDraw(WMScreen * scr, xcb_drawable_t d, WMColor *back, WMColor *selBack,
	   WMColor *textColor, WMFont *font, void *data, WMRect rect, bool selected)
{
	cairo_t *cr;
	int x, y;

	x = rect.pos.x + 5;
	y = rect.pos.y + (rect.size.height - WMFontHeight(font)) / 2;

	cr = cellBegin(scr, d, back, selBack, rect, selected);
	if (!cr)
		return;
	W_DrawString(cr, textColor, font, x, y, data, strlen(data));
	WCairoEnd(cr);
}

static void pixmapDraw(WMScreen *scr, xcb_drawable_t d, WMColor *back, WMColor *selBack,
		       WMPixmap *pixmap, WMRect rect, bool selected)
{
	cairo_t *cr;
	WMSize size;
	int x, y;

	cr = cellBegin(scr, d, back, selBack, rect, selected);
	if (!cr)
		return;
	if (pixmap) {
		size = WMGetPixmapSize(pixmap);
		x = rect.pos.x + (rect.size.width - size.width) / 2;
		y = rect.pos.y + (rect.size.height - size.height) / 2;
		W_PaintPixmap(cr, pixmap, x, y);
	}
	WCairoEnd(cr);
}

/* ---------------------------------------------------------------------- */

static void SECellPainter(WMTableColumnDelegate * self, WMTableColumn * column, int row, xcb_drawable_t d)
{
	StringEditorData *strdata = (StringEditorData *) self->data;
	WMTableView *table = WMGetTableColumnTableView(column);

	stringDraw(WMWidgetScreen(table), d,
		   strdata->back, strdata->selBack, strdata->textColor, strdata->font,
		   WMTableViewDataForCell(table, column, row), WMTableViewRectForCell(table, column, row), false);
}

static void selectedSECellPainter(WMTableColumnDelegate * self, WMTableColumn * column, int row, xcb_drawable_t d)
{
	StringEditorData *strdata = (StringEditorData *) self->data;
	WMTableView *table = WMGetTableColumnTableView(column);

	stringDraw(WMWidgetScreen(table), d,
		   strdata->back, strdata->selBack, strdata->textColor, strdata->font,
		   WMTableViewDataForCell(table, column, row), WMTableViewRectForCell(table, column, row), true);
}

static void beginSECellEdit(WMTableColumnDelegate * self, WMTableColumn * column, int row)
{
	StringEditorData *strdata = (StringEditorData *) self->data;
	WMRect rect = WMTableViewRectForCell(strdata->table, column, row);
	void *data = WMTableViewDataForCell(strdata->table, column, row);

	WMSetTextFieldText(strdata->widget, (char *)data);
	WMMoveWidget(strdata->widget, rect.pos.x, rect.pos.y);
	WMResizeWidget(strdata->widget, rect.size.width + 1, rect.size.height + 1);

	WMMapWidget(strdata->widget);
}

static void endSECellEdit(WMTableColumnDelegate * self, WMTableColumn * column, int row)
{
	StringEditorData *strdata = (StringEditorData *) self->data;
	char *text;

	WMUnmapWidget(strdata->widget);

	text = WMGetTextFieldText(strdata->widget);
	WMSetTableViewDataForCell(strdata->table, column, row, (void *)text);
}

WMTableColumnDelegate *WTCreateStringEditorDelegate(WMTableView * parent)
{
	WMTableColumnDelegate *delegate = wmalloc(sizeof(WMTableColumnDelegate));
	WMScreen *scr = WMWidgetScreen(parent);
	StringEditorData *data = wmalloc(sizeof(StringEditorData));

	data->widget = WMCreateTextField(parent);
	W_ReparentView(WMWidgetView(data->widget), WMGetTableViewDocumentView(parent), 0, 0);
	data->table = parent;
	data->font = WMSystemFontOfSize(scr, 12);
	data->back = WMWhiteColor(scr);
	data->selBack = WMCreateNamedColor(scr, selectionColorName(), false);
	data->textColor = WMBlackColor(scr);

	delegate->data = data;
	delegate->drawCell = SECellPainter;
	delegate->drawSelectedCell = selectedSECellPainter;
	delegate->beginCellEdit = beginSECellEdit;
	delegate->endCellEdit = endSECellEdit;

	return delegate;
}

/* ---------------------------------------------------------------------- */

static void ESCellPainter(WMTableColumnDelegate * self, WMTableColumn * column, int row, xcb_drawable_t d)
{
	EnumSelectorData *strdata = (EnumSelectorData *) self->data;
	WMTableView *table = WMGetTableColumnTableView(column);
	uintptr_t i = (uintptr_t)WMTableViewDataForCell(table, column, row);

	stringDraw(WMWidgetScreen(table), d,
		   strdata->back, strdata->selBack, strdata->textColor, strdata->font,
		   strdata->options[i], WMTableViewRectForCell(table, column, row), false);
}

static void selectedESCellPainter(WMTableColumnDelegate * self, WMTableColumn * column, int row, xcb_drawable_t d)
{
	EnumSelectorData *strdata = (EnumSelectorData *) self->data;
	WMTableView *table = WMGetTableColumnTableView(column);
	uintptr_t i = (uintptr_t)WMTableViewDataForCell(table, column, row);

	stringDraw(WMWidgetScreen(table), d,
		   strdata->back, strdata->selBack, strdata->textColor, strdata->font,
		   strdata->options[i], WMTableViewRectForCell(table, column, row), true);
}

static void beginESCellEdit(WMTableColumnDelegate * self, WMTableColumn * column, int row)
{
	EnumSelectorData *strdata = (EnumSelectorData *) self->data;
	WMRect rect = WMTableViewRectForCell(strdata->table, column, row);
	uintptr_t data = (uintptr_t)WMTableViewDataForCell(strdata->table, column, row);

	wassertr(data < strdata->count);

	WMSetPopUpButtonSelectedItem(strdata->widget, data);

	WMMoveWidget(strdata->widget, rect.pos.x, rect.pos.y);
	WMResizeWidget(strdata->widget, rect.size.width, rect.size.height + 1);

	WMMapWidget(strdata->widget);
}

static void endESCellEdit(WMTableColumnDelegate * self, WMTableColumn * column, int row)
{
	EnumSelectorData *strdata = (EnumSelectorData *) self->data;
	int option;

	WMUnmapWidget(strdata->widget);

	option = WMGetPopUpButtonSelectedItem(strdata->widget);
	WMSetTableViewDataForCell(strdata->table, column, row, (void *)(uintptr_t) option);
}

WMTableColumnDelegate *WTCreateEnumSelectorDelegate(WMTableView * parent)
{
	WMTableColumnDelegate *delegate = wmalloc(sizeof(WMTableColumnDelegate));
	WMScreen *scr = WMWidgetScreen(parent);
	EnumSelectorData *data = wmalloc(sizeof(EnumSelectorData));

	data->widget = WMCreatePopUpButton(parent);
	W_ReparentView(WMWidgetView(data->widget), WMGetTableViewDocumentView(parent), 0, 0);
	data->table = parent;
	data->font = WMSystemFontOfSize(scr, 12);
	data->back = WMWhiteColor(scr);
	data->selBack = WMCreateNamedColor(scr, selectionColorName(), false);
	data->textColor = WMBlackColor(scr);
	data->count = 0;
	data->options = NULL;

	delegate->data = data;
	delegate->drawCell = ESCellPainter;
	delegate->drawSelectedCell = selectedESCellPainter;
	delegate->beginCellEdit = beginESCellEdit;
	delegate->endCellEdit = endESCellEdit;

	return delegate;
}

void WTSetEnumSelectorOptions(WMTableColumnDelegate * delegate, char **options, int count)
{
	EnumSelectorData *data = (EnumSelectorData *) delegate->data;
	int i;

	for (i = 0; i < WMGetPopUpButtonNumberOfItems(data->widget); i++) {
		WMRemovePopUpButtonItem(data->widget, 0);
	}

	data->options = options;
	data->count = count;

	for (i = 0; i < count; i++) {
		WMAddPopUpButtonItem(data->widget, options[i]);
	}
}

/* ---------------------------------------------------------------------- */

static void BSCellPainter(WMTableColumnDelegate * self, WMTableColumn * column, int row, xcb_drawable_t d)
{
	BooleanSwitchData *strdata = (BooleanSwitchData *) self->data;
	WMTableView *table = WMGetTableColumnTableView(column);
	uintptr_t i = (uintptr_t)WMTableViewDataForCell(table, column, row);
	WMScreen *scr = WMWidgetScreen(table);

	if (i) {
		pixmapDraw(scr, d,
			   strdata->back, strdata->selBack, WMGetSystemPixmap(scr, WSICheckMark),
			   WMTableViewRectForCell(table, column, row), false);
	} else {
		pixmapDraw(scr, d,
			   strdata->back, strdata->selBack, NULL, WMTableViewRectForCell(table, column, row), false);
	}
}

static void selectedBSCellPainter(WMTableColumnDelegate * self, WMTableColumn * column, int row, xcb_drawable_t d)
{
	BooleanSwitchData *strdata = (BooleanSwitchData *) self->data;
	WMTableView *table = WMGetTableColumnTableView(column);
	uintptr_t i = (uintptr_t)WMTableViewDataForCell(table, column, row);
	WMScreen *scr = WMWidgetScreen(table);

	if (i) {
		pixmapDraw(scr, d,
			   strdata->back, strdata->selBack, WMGetSystemPixmap(scr, WSICheckMark),
			   WMTableViewRectForCell(table, column, row), true);
	} else {
		pixmapDraw(scr, d,
			   strdata->back, strdata->selBack, NULL, WMTableViewRectForCell(table, column, row), true);
	}
}

static void beginBSCellEdit(WMTableColumnDelegate * self, WMTableColumn * column, int row)
{
	BooleanSwitchData *strdata = (BooleanSwitchData *) self->data;
	WMRect rect = WMTableViewRectForCell(strdata->table, column, row);
	uintptr_t data = (uintptr_t)WMTableViewDataForCell(strdata->table, column, row);

	WMSetButtonSelected(strdata->widget, data);
	WMMoveWidget(strdata->widget, rect.pos.x + 1, rect.pos.y + 1);
	WMResizeWidget(strdata->widget, rect.size.width - 1, rect.size.height - 1);

	WMMapWidget(strdata->widget);
}

static void endBSCellEdit(WMTableColumnDelegate * self, WMTableColumn * column, int row)
{
	BooleanSwitchData *strdata = (BooleanSwitchData *) self->data;
	int value;

	value = WMGetButtonSelected(strdata->widget);
	WMSetTableViewDataForCell(strdata->table, column, row, (void *)(uintptr_t) value);
	WMUnmapWidget(strdata->widget);
}

WMTableColumnDelegate *WTCreateBooleanSwitchDelegate(WMTableView * parent)
{
	WMTableColumnDelegate *delegate = wmalloc(sizeof(WMTableColumnDelegate));
	WMScreen *scr = WMWidgetScreen(parent);
	BooleanSwitchData *data = wmalloc(sizeof(BooleanSwitchData));
	WMColor *color;

	data->widget = WMCreateSwitchButton(parent);
	W_ReparentView(WMWidgetView(data->widget), WMGetTableViewDocumentView(parent), 0, 0);
	WMSetButtonText(data->widget, NULL);
	WMSetButtonImagePosition(data->widget, WIPImageOnly);
	WMSetButtonImage(data->widget, NULL);
	WMSetButtonAltImage(data->widget, WMGetSystemPixmap(scr, WSICheckMark));

	data->table = parent;
	color = WMCreateNamedColor(scr, selectionColorName(), false);
	WMSetWidgetBackgroundColor(data->widget, color);
	data->back = WMWhiteColor(scr);
	data->selBack = color;

	delegate->data = data;
	delegate->drawCell = BSCellPainter;
	delegate->drawSelectedCell = selectedBSCellPainter;
	delegate->beginCellEdit = beginBSCellEdit;
	delegate->endCellEdit = endBSCellEdit;

	return delegate;
}

/* ---------------------------------------------------------------------- */

static void SCellPainter(WMTableColumnDelegate * self, WMTableColumn * column, int row, xcb_drawable_t d)
{
	StringData *strdata = (StringData *) self->data;
	WMTableView *table = WMGetTableColumnTableView(column);

	stringDraw(WMWidgetScreen(table), d,
		   strdata->back, strdata->selBack, strdata->textColor, strdata->font,
		   WMTableViewDataForCell(table, column, row), WMTableViewRectForCell(table, column, row), false);
}

static void selectedSCellPainter(WMTableColumnDelegate * self, WMTableColumn * column, int row, xcb_drawable_t d)
{
	StringData *strdata = (StringData *) self->data;
	WMTableView *table = WMGetTableColumnTableView(column);

	stringDraw(WMWidgetScreen(table), d,
		   strdata->back, strdata->selBack, strdata->textColor, strdata->font,
		   WMTableViewDataForCell(table, column, row), WMTableViewRectForCell(table, column, row), true);
}

WMTableColumnDelegate *WTCreateStringDelegate(WMTableView * parent)
{
	WMTableColumnDelegate *delegate = wmalloc(sizeof(WMTableColumnDelegate));
	WMScreen *scr = WMWidgetScreen(parent);
	StringData *data = wmalloc(sizeof(StringData));

	data->table = parent;
	data->font = WMSystemFontOfSize(scr, 12);
	data->back = WMWhiteColor(scr);
	data->selBack = WMCreateNamedColor(scr, selectionColorName(), false);
	data->textColor = WMBlackColor(scr);

	delegate->data = data;
	delegate->drawCell = SCellPainter;
	delegate->drawSelectedCell = selectedSCellPainter;
	delegate->beginCellEdit = NULL;
	delegate->endCellEdit = NULL;

	return delegate;
}

/* ---------------------------------------------------------------------- */

static void PCellPainter(WMTableColumnDelegate * self, WMTableColumn * column, int row, xcb_drawable_t d)
{
	StringData *strdata = (StringData *) self->data;
	WMTableView *table = WMGetTableColumnTableView(column);

	pixmapDraw(WMWidgetScreen(table), d,
		   strdata->back, strdata->selBack,
		   (WMPixmap *) WMTableViewDataForCell(table, column, row),
		   WMTableViewRectForCell(table, column, row), false);
}

static void selectedPCellPainter(WMTableColumnDelegate * self, WMTableColumn * column, int row, xcb_drawable_t d)
{
	StringData *strdata = (StringData *) self->data;
	WMTableView *table = WMGetTableColumnTableView(column);

	pixmapDraw(WMWidgetScreen(table), d,
		   strdata->back, strdata->selBack,
		   (WMPixmap *) WMTableViewDataForCell(table, column, row),
		   WMTableViewRectForCell(table, column, row), true);
}

WMTableColumnDelegate *WTCreatePixmapDelegate(WMTableView * table)
{
	WMTableColumnDelegate *delegate = wmalloc(sizeof(WMTableColumnDelegate));
	WMScreen *scr = WMWidgetScreen(table);
	StringData *data = wmalloc(sizeof(StringData));

	data->table = table;
	data->back = WMWhiteColor(scr);
	data->selBack = WMCreateNamedColor(scr, selectionColorName(), false);

	delegate->data = data;
	delegate->drawCell = PCellPainter;
	delegate->drawSelectedCell = selectedPCellPainter;
	delegate->beginCellEdit = NULL;
	delegate->endCellEdit = NULL;

	return delegate;
}

/* ---------------------------------------------------------------------- */

static void drawPSCell(WMTableColumnDelegate * self, xcb_drawable_t d, WMTableColumn * column, int row, bool selected)
{
	StringData *strdata = (StringData *) self->data;
	WMTableView *table = WMGetTableColumnTableView(column);
	void **data;
	WMPixmap *pix;
	char *str;
	WMRect rect;
	WMSize size;

	data = WMTableViewDataForCell(table, column, row);

	str = (char *)data[0];
	pix = (WMPixmap *) data[1];

	rect = WMTableViewRectForCell(table, column, row);

	if (pix) {
		int owidth = rect.size.width;

		size = WMGetPixmapSize(pix);
		rect.size.width = size.width;

		pixmapDraw(WMWidgetScreen(table),
			   WMViewXID(WMGetTableViewDocumentView(table)),
			   strdata->back, strdata->selBack, pix, rect, selected);

		rect.pos.x += size.width - 1;
		rect.size.width = owidth - size.width + 1;
	}

	stringDraw(WMWidgetScreen(table), d, strdata->back, strdata->selBack,
		   strdata->textColor, strdata->font, str, rect, selected);
}

static void PSCellPainter(WMTableColumnDelegate * self, WMTableColumn * column, int row, xcb_drawable_t d)
{
	drawPSCell(self, d, column, row, false);
}

static void selectedPSCellPainter(WMTableColumnDelegate * self, WMTableColumn * column, int row, xcb_drawable_t d)
{
	drawPSCell(self, d, column, row, true);
}

WMTableColumnDelegate *WTCreatePixmapStringDelegate(WMTableView * parent)
{
	WMTableColumnDelegate *delegate = wmalloc(sizeof(WMTableColumnDelegate));
	WMScreen *scr = WMWidgetScreen(parent);
	StringData *data = wmalloc(sizeof(StringData));

	data->table = parent;
	data->font = WMSystemFontOfSize(scr, 12);
	data->back = WMWhiteColor(scr);
	data->selBack = WMCreateNamedColor(scr, selectionColorName(), false);
	data->textColor = WMBlackColor(scr);

	delegate->data = data;
	delegate->drawCell = PSCellPainter;
	delegate->drawSelectedCell = selectedPSCellPainter;
	delegate->beginCellEdit = NULL;
	delegate->endCellEdit = NULL;

	return delegate;
}
