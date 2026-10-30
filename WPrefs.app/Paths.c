/* Paths.c- pixmap/icon paths
 *
 *  WPrefs - Window Maker Preferences Program
 *
 *  Copyright (c) 1998-2003 Alfredo K. Kojima
 *  Copyright (c) 2009-2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include "WPrefs.h"
#include <unistd.h>
#include <assert.h>

typedef struct _Panel {
	WMBox *box;
	char *sectionName;

	char *description;

	CallbackRec callbacks;

	WMWidget *parent;

	WMTabView *tabv;

	WMFrame *pixF;
	WMList *pixL;
	WMButton *pixaB;
	WMButton *pixrB;

	WMFrame *icoF;
	WMList *icoL;
	WMButton *icoaB;
	WMButton *icorB;

	WMFrame *sndF;
	WMList *sndL;
	WMButton *sndaB;
	WMButton *sndrB;

	WMColor *red;
	WMColor *black;
	WMColor *gray;
	WMFont *font;
} _Panel;

#define ICON_FILE	"paths"

static void addPathToList(WMList * list, int index, const char *path)
{
	char *fpath = wexpandpath(path);
	WMListItem *item;

	item = WMInsertListItem(list, index, path);

	if (access(fpath, X_OK) != 0) {
		item->uflags = 1;
	}
	wfree(fpath);
}

static void showData(_Panel * panel)
{
	WMPropList *array, *val;
	char *p;
	int i;

	/* showData doubles as the reset/revert refresh, so the lists
	 * must be rebuilt from scratch on every call */
	WMClearList(panel->icoL);
	WMClearList(panel->pixL);
	WMClearList(panel->sndL);

	array = GetObjectForKey("IconPath");
	if (!array || !WMIsPLArray(array)) {
		if (array)
			wwarning(_("bad value in option IconPath. Using default path list"));
		addPathToList(panel->icoL, -1, "~/pixmaps");
		p = wstrconcat(wuserdatapath(), "/Icons");
		addPathToList(panel->icoL, -1, p);
		wfree(p);
		addPathToList(panel->icoL, -1, WMAKER_RESOURCE_PATH "/Icons");
		addPathToList(panel->icoL, -1, WMAKER_RESOURCE_PATH "/Pixmaps");
	} else {
		for (i = 0; i < WMGetPropListItemCount(array); i++) {
			val = WMGetFromPLArray(array, i);
			addPathToList(panel->icoL, -1, WMGetFromPLString(val));
		}
	}

	array = GetObjectForKey("PixmapPath");
	if (!array || !WMIsPLArray(array)) {
		if (array)
			wwarning(_("bad value in option PixmapPath. Using default path list"));
		addPathToList(panel->pixL, -1, "~/pixmaps");
		p = wstrconcat(wuserdatapath(), "/Pixmaps");
		addPathToList(panel->pixL, -1, p);
		wfree(p);
		addPathToList(panel->pixL, -1, WMAKER_RESOURCE_PATH "/Pixmaps");
	} else {
		for (i = 0; i < WMGetPropListItemCount(array); i++) {
			val = WMGetFromPLArray(array, i);
			addPathToList(panel->pixL, -1, WMGetFromPLString(val));
		}
	}

	array = GetSoundObjectForKey("SoundPath");
	if (!array || !WMIsPLArray(array) || WMGetPropListItemCount(array) == 0) {
		if (array && !WMIsPLArray(array))
			wwarning(_("bad value in option SoundPath. Using default path list"));
		p = wstrconcat(wuserdatapath(), "/Sounds");
		addPathToList(panel->sndL, -1, p);
		wfree(p);
		addPathToList(panel->sndL, -1, WMAKER_RESOURCE_PATH "/Sounds");
	} else {
		for (i = 0; i < WMGetPropListItemCount(array); i++) {
			val = WMGetFromPLArray(array, i);
			addPathToList(panel->sndL, -1, WMGetFromPLString(val));
		}
	}
}

static void pushButton(WMWidget * w, void *data)
{
	_Panel *panel = (_Panel *) data;
	int i;

	/* icon paths */
	if (w == panel->icorB) {
		i = WMGetListSelectedItemRow(panel->icoL);

		if (i >= 0)
			WMRemoveListItem(panel->icoL, i);
	}

	/* pixmap paths */
	if (w == panel->pixrB) {
		i = WMGetListSelectedItemRow(panel->pixL);

		if (i >= 0)
			WMRemoveListItem(panel->pixL, i);
	}

	/* sound paths */
	if (w == panel->sndrB) {
		i = WMGetListSelectedItemRow(panel->sndL);

		if (i >= 0)
			WMRemoveListItem(panel->sndL, i);
	}
}

static void browseForFile(WMWidget * w, void *data)
{
	_Panel *panel = (_Panel *) data;
	WMFilePanel *filePanel;

	assert(w == panel->icoaB || w == panel->pixaB || w == panel->sndaB);

	filePanel = WMGetOpenPanel(WMWidgetScreen(w));

	WMSetFilePanelCanChooseFiles(filePanel, false);

	if (WMRunModalFilePanelForDirectory(filePanel, panel->parent, "/", _("Select directory"), NULL) == true) {
		char *str = WMGetFilePanelFileName(filePanel);

		if (str) {
			int len = strlen(str);

			/* Remove the trailing '/' except if the path is exactly / */
			if (len > 1 && str[len - 1] == '/') {
				str[len - 1] = '\0';
				len--;
			}
			if (len > 0) {
				WMList *lPtr;
				int i;

				if (w == panel->icoaB)
					lPtr = panel->icoL;
				else if (w == panel->pixaB)
					lPtr = panel->pixL;
				else if (w == panel->sndaB)
					lPtr = panel->sndL;
				else
					goto error_unknown_widget;

				i = WMGetListSelectedItemRow(lPtr);
				if (i >= 0)
					i++;
				addPathToList(lPtr, i, str);
				WMSetListBottomPosition(lPtr, WMGetListNumberOfRows(lPtr));
			}
		error_unknown_widget:
			wfree(str);
		}
	}
}

static void paintItem(WMList * lPtr, int index, xcb_drawable_t d, char *text, int state, WMRect * rect)
{
	int width, height, x, y;
	_Panel *panel = (_Panel *) WMGetHangedData(lPtr);
	WMScreen *scr = WMWidgetScreen(lPtr);
	WMColor *backColor = (state & WLDSSelected) ? WMGetListSelectionColor(lPtr) : panel->gray;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) index;

	width = rect->size.width;
	height = rect->size.height;
	x = rect->pos.x;
	y = rect->pos.y;

	WMPaintColorSwatch(backColor, d, x, y, width, height);

	if (state & 1) {
		WMDrawString(scr, d, panel->red, panel->font, x + 4, y, text, strlen(text));
	} else {
		WMDrawString(scr, d, panel->black, panel->font, x + 4, y, text, strlen(text));
	}
}

static void storeData(_Panel * panel)
{
	WMPropList *list;
	WMPropList *tmp;
	int i;
	char *p;

	list = WMCreatePLArray(NULL, NULL);
	for (i = 0; i < WMGetListNumberOfRows(panel->icoL); i++) {
		p = WMGetListItem(panel->icoL, i)->text;
		tmp = WMCreatePLString(p);
		WMAddToPLArray(list, tmp);
		WMReleasePropList(tmp);
	}
	SetObjectForKey(list, "IconPath");
	WMReleasePropList(list);

	list = WMCreatePLArray(NULL, NULL);
	for (i = 0; i < WMGetListNumberOfRows(panel->pixL); i++) {
		p = WMGetListItem(panel->pixL, i)->text;
		tmp = WMCreatePLString(p);
		WMAddToPLArray(list, tmp);
		WMReleasePropList(tmp);
	}
	SetObjectForKey(list, "PixmapPath");
	WMReleasePropList(list);

	list = WMCreatePLArray(NULL, NULL);
	for (i = 0; i < WMGetListNumberOfRows(panel->sndL); i++) {
		p = WMGetListItem(panel->sndL, i)->text;
		tmp = WMCreatePLString(p);
		WMAddToPLArray(list, tmp);
		WMReleasePropList(tmp);
	}
	SetSoundObjectForKey(list, "SoundPath");
	WMReleasePropList(list);
}

static void createPanel(Panel * p)
{
	_Panel *panel = (_Panel *) p;
	WMScreen *scr = WMWidgetScreen(panel->parent);
	WMTabViewItem *tab;

	panel->black = WMBlackColor(scr);
	panel->gray = WMGrayColor(scr);
	panel->red = WMCreateRGBColor(scr, 0xffff, 0, 0, true);
	panel->font = WMSystemFontOfSize(scr, 12);

	panel->box = WMCreateBox(panel->parent);
	WMSetViewExpandsToParent(WMWidgetView(panel->box), 2, 2, 2, 2);

	panel->tabv = WMCreateTabView(panel->box);
	WMMoveWidget(panel->tabv, 12, 10);
	WMResizeWidget(panel->tabv, 500, 215);

	/* icon path */
	panel->icoF = WMCreateFrame(panel->box);
	WMSetFrameRelief(panel->icoF, WRFlat);
	WMResizeWidget(panel->icoF, 230, 210);

	tab = WMCreateTabViewItemWithIdentifier(0);
	WMSetTabViewItemView(tab, WMWidgetView(panel->icoF));
	WMAddItemInTabView(panel->tabv, tab);
	WMSetTabViewItemLabel(tab, _("Icon Search Paths"));

	panel->icoL = WMCreateList(panel->icoF);
	WMResizeWidget(panel->icoL, 480, 147);
	WMMoveWidget(panel->icoL, 10, 10);
	WMSetListUserDrawProc(panel->icoL, paintItem);
	WMHangData(panel->icoL, panel);

	panel->icoaB = WMCreateCommandButton(panel->icoF);
	WMResizeWidget(panel->icoaB, 95, 24);
	WMMoveWidget(panel->icoaB, 293, 165);
	WMSetButtonText(panel->icoaB, _("Add"));
	WMSetButtonAction(panel->icoaB, browseForFile, panel);
	WMSetButtonImagePosition(panel->icoaB, WIPRight);

	panel->icorB = WMCreateCommandButton(panel->icoF);
	WMResizeWidget(panel->icorB, 95, 24);
	WMMoveWidget(panel->icorB, 395, 165);
	WMSetButtonText(panel->icorB, _("Remove"));
	WMSetButtonAction(panel->icorB, pushButton, panel);

	WMMapSubwidgets(panel->icoF);

	/* pixmap path */
	panel->pixF = WMCreateFrame(panel->box);
	WMSetFrameRelief(panel->pixF, WRFlat);
	WMResizeWidget(panel->pixF, 230, 210);

	tab = WMCreateTabViewItemWithIdentifier(0);
	WMSetTabViewItemView(tab, WMWidgetView(panel->pixF));
	WMAddItemInTabView(panel->tabv, tab);
	WMSetTabViewItemLabel(tab, _("Pixmap Search Paths"));

	panel->pixL = WMCreateList(panel->pixF);
	WMResizeWidget(panel->pixL, 480, 147);
	WMMoveWidget(panel->pixL, 10, 10);
	WMSetListUserDrawProc(panel->pixL, paintItem);
	WMHangData(panel->pixL, panel);

	panel->pixaB = WMCreateCommandButton(panel->pixF);
	WMResizeWidget(panel->pixaB, 95, 24);
	WMMoveWidget(panel->pixaB, 293, 165);
	WMSetButtonText(panel->pixaB, _("Add"));
	WMSetButtonAction(panel->pixaB, browseForFile, panel);
	WMSetButtonImagePosition(panel->pixaB, WIPRight);

	panel->pixrB = WMCreateCommandButton(panel->pixF);
	WMResizeWidget(panel->pixrB, 95, 24);
	WMMoveWidget(panel->pixrB, 395, 165);
	WMSetButtonText(panel->pixrB, _("Remove"));
	WMSetButtonAction(panel->pixrB, pushButton, panel);

	WMMapSubwidgets(panel->pixF);

	/* sound path */
	panel->sndF = WMCreateFrame(panel->box);
	WMSetFrameRelief(panel->sndF, WRFlat);
	WMResizeWidget(panel->sndF, 230, 210);

	tab = WMCreateTabViewItemWithIdentifier(0);
	WMSetTabViewItemView(tab, WMWidgetView(panel->sndF));
	WMAddItemInTabView(panel->tabv, tab);
	WMSetTabViewItemLabel(tab, _("Sound Search Paths"));

	panel->sndL = WMCreateList(panel->sndF);
	WMResizeWidget(panel->sndL, 480, 147);
	WMMoveWidget(panel->sndL, 10, 10);
	WMSetListUserDrawProc(panel->sndL, paintItem);
	WMHangData(panel->sndL, panel);

	panel->sndaB = WMCreateCommandButton(panel->sndF);
	WMResizeWidget(panel->sndaB, 95, 24);
	WMMoveWidget(panel->sndaB, 293, 165);
	WMSetButtonText(panel->sndaB, _("Add"));
	WMSetButtonAction(panel->sndaB, browseForFile, panel);
	WMSetButtonImagePosition(panel->sndaB, WIPRight);

	panel->sndrB = WMCreateCommandButton(panel->sndF);
	WMResizeWidget(panel->sndrB, 95, 24);
	WMMoveWidget(panel->sndrB, 395, 165);
	WMSetButtonText(panel->sndrB, _("Remove"));
	WMSetButtonAction(panel->sndrB, pushButton, panel);

	WMMapSubwidgets(panel->sndF);

	WMRealizeWidget(panel->box);
	WMMapSubwidgets(panel->box);

	showData(panel);
}

Panel *InitPaths(WMWidget *parent)
{
	_Panel *panel;

	panel = wmalloc(sizeof(_Panel));

	panel->sectionName = _("Search Path Configuration");

	panel->description = _("Search paths to use when looking for pixmaps\n" "and icons.");

	panel->parent = parent;

	panel->callbacks.createWidgets = createPanel;
	panel->callbacks.updateDomain = storeData;
	panel->callbacks.undoChanges = showData;

	AddSection(panel, ICON_FILE);

	return panel;
}
