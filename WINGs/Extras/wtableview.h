/*
 *  wtableview.h
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 2000-2003 Alfredo K. Kojima
 *  Copyright (c) 2000-2004 Dan Pascu
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



#ifndef _WTABLEVIEW_H_
#define _WTABLEVIEW_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct W_TableColumn WMTableColumn;
typedef struct W_TableView WMTableView;


extern const char *WMTableViewSelectionDidChangeNotification;


typedef struct WMTableColumnDelegate {
    void *data;
    void (*drawCell)(struct WMTableColumnDelegate *self,
                     WMTableColumn *column, int row, xcb_drawable_t d);
    void (*drawSelectedCell)(struct WMTableColumnDelegate *self,
                             WMTableColumn *column, int row, xcb_drawable_t d);

    void (*beginCellEdit)(struct WMTableColumnDelegate *self, WMTableColumn *column,
                          int row);
    void (*endCellEdit)(struct WMTableColumnDelegate *self, WMTableColumn *column,
                        int row);
} WMTableColumnDelegate;


typedef struct W_TableViewDelegate {
    void *data;
    int (*numberOfRows)(struct W_TableViewDelegate *self,
                        WMTableView *table);
    void *(*valueForCell)(struct W_TableViewDelegate *self,
                          WMTableColumn *column, int row);
    void (*setValueForCell)(struct W_TableViewDelegate *self,
                            WMTableColumn *column, int row, void *value);
} WMTableViewDelegate;





WMTableColumn *WMCreateTableColumn(char *title);

void WMSetTableColumnWidth(WMTableColumn *column, unsigned width);

void WMSetTableColumnConstraints(WMTableColumn *column,
                                 unsigned minWidth, unsigned maxWidth);

void WMSetTableColumnDelegate(WMTableColumn *column,
                              WMTableColumnDelegate *delegate);


WMTableView *WMGetTableColumnTableView(WMTableColumn *column);

void WMSetTableColumnId(WMTableColumn *column, void *id);

void *WMGetTableColumnId(WMTableColumn *column);


WMTableView *WMCreateTableView(WMWidget *parent);


void WMSetTableViewDataSource(WMTableView *table, void *source);

void *WMGetTableViewDataSource(WMTableView *table);

void WMSetTableViewHeaderHeight(WMTableView *table, unsigned height);

void WMAddTableViewColumn(WMTableView *table, WMTableColumn *column);

void WMSetTableViewDelegate(WMTableView *table, WMTableViewDelegate *delegate);

void WMSetTableViewAction(WMTableView *table, WMAction *action,
                          void *clientData);

void *WMGetTableViewClickedColumn(WMTableView *table);

int WMGetTableViewClickedRow(WMTableView *table);

WMArray *WMGetTableViewSelectedRows(WMTableView *table);

WMView *WMGetTableViewDocumentView(WMTableView *table);

void WMEditTableViewRow(WMTableView *table, int row);

void WMSelectTableViewRow(WMTableView *table, int row);

void *WMTableViewDataForCell(WMTableView *table, WMTableColumn *column,
                             int row);

void WMSetTableViewDataForCell(WMTableView *table, WMTableColumn *column,
                               int row, void *data);

WMRect WMTableViewRectForCell(WMTableView *table, WMTableColumn *column,
                              int row);

void WMSetTableViewBackgroundColor(WMTableView *table, WMColor *color);

void WMSetTableViewGridColor(WMTableView *table, WMColor *color);

void WMSetTableViewRowHeight(WMTableView *table, int height);

void WMReloadTableView(WMTableView *table);

void WMNoteTableViewNumberOfRowsChanged(WMTableView *table);

void WMScrollTableViewRowToVisible(WMTableView *table, int row);

void WMSetTableViewHasHorizontalScroller(WMTableView *tPtr, bool flag);


#ifdef __cplusplus
}
#endif

#endif

