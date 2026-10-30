/*
 *  wtabledelegates.h
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 2001 Alfredo K. Kojima
 *  Copyright (c) 2004 Dan Pascu
 *  Copyright (c) 2020 Window Maker Team
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

#ifndef _TABLEDELEGATES_H_
#define _TABLEDELEGATES_H_

#ifdef __cplusplus
extern "C" {
#endif

WMTableColumnDelegate *WTCreatePixmapDelegate(WMTableView *table);
WMTableColumnDelegate *WTCreateStringDelegate(WMTableView *table);
WMTableColumnDelegate *WTCreatePixmapStringDelegate(WMTableView *parent);

WMTableColumnDelegate *WTCreateStringEditorDelegate(WMTableView *table);

WMTableColumnDelegate *WTCreateEnumSelectorDelegate(WMTableView *table);
void WTSetEnumSelectorOptions(WMTableColumnDelegate *delegate,
                              char **options, int count);

WMTableColumnDelegate *WTCreateBooleanSwitchDelegate(WMTableView *parent);

#ifdef __cplusplus
}
#endif

#endif
