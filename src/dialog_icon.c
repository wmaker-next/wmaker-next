/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 1998-2003 Dan Pascu
 *  Copyright (c) 2014-2026 Window Maker Team
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

/* dialog_icon.c - icon selection dialog */

#include "wconfig.h"

#include <xkbcommon/xkbcommon-keysyms.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <errno.h>

#include <cairo.h>
#include <WINGs/WINGsP.h>

#ifndef PATH_MAX
#define PATH_MAX DEFAULT_PATH_MAX
#endif

#include "WindowMaker.h"
#include "screen.h"
#include "window.h"
#include "dialog_icon.h"
#include "dialog.h"
#include "misc.h"
#include "wxkb.h"
#include "actions.h"
#include "xinerama.h"

/*
 *****************************************************************
 * Icon Selection Panel
 *****************************************************************
 */

typedef struct IconPanel {

	WScreen *scr;

	WMWindow *win;

	WMLabel *dirLabel;
	WMLabel *iconLabel;

	WMList *dirList;
	WMList *iconList;
	WMFont *normalfont;

	WMFrame *previewFrame;
	WMLabel *iconView;

	WMFrame *modeFrame;
	WMButton *textButton;
	WMButton *imageButton;

	WMLabel *fileLabel;
	WMTextField *fileField;

	WMButton *okButton;
	WMButton *cancelButton;
#if 0
	WMButton *chooseButton;
#endif
	short done;
	short result;
	short preview;
	bool imageMode;
} IconPanel;

static void listPixmaps(WScreen *scr, WMList *lPtr, const char *path)
{
	struct dirent *dentry;
	DIR *dir;
	char pbuf[PATH_MAX + 16];
	char *apath;
	IconPanel *panel = WMGetHangedData(lPtr);

	panel->preview = false;

	apath = wexpandpath(path);
	dir = opendir(apath);

	if (!dir) {
		wfree(apath);
		snprintf(pbuf, sizeof(pbuf),
		         _("Could not open directory \"%s\":\n%s"),
		         path, strerror(errno));
		wMessageDialog(scr, _("Error"), pbuf, _("OK"), NULL, NULL);
		return;
	}

	/* list contents in the column */
	while ((dentry = readdir(dir))) {
		struct stat statb;

		if (strcmp(dentry->d_name, ".") == 0 || strcmp(dentry->d_name, "..") == 0)
			continue;

		if (wstrlcpy(pbuf, apath, sizeof(pbuf)) >= sizeof(pbuf) ||
		    wstrlcat(pbuf, "/", sizeof(pbuf)) >= sizeof(pbuf) ||
		    wstrlcat(pbuf, dentry->d_name, sizeof(pbuf)) >= sizeof(pbuf)) {
			wwarning(_("full path for file \"%s\" in \"%s\" is longer than %d bytes, skipped"),
			         dentry->d_name, path, (int) (sizeof(pbuf) - 1) );
			continue;
		}

		if (stat(pbuf, &statb) < 0)
			continue;

		if (statb.st_mode & (S_IRUSR | S_IRGRP | S_IROTH)
		    && statb.st_mode & (S_IFREG | S_IFLNK)) {
			WMAddListItem(lPtr, dentry->d_name);
		}
	}
	WMSortListItems(lPtr);

	closedir(dir);
	wfree(apath);
	panel->preview = true;
}

static void setViewedImage(IconPanel *panel, const char *file)
{
	WMPixmap *pixmap;
	RColor color;
	int iwidth, iheight;

	color.red = 0xae;
	color.green = 0xaa;
	color.blue = 0xae;
	color.alpha = 0;
	iwidth = WMWidgetWidth(panel->iconView);
	iheight = WMWidgetHeight(panel->iconView);
	pixmap = WMCreateScaledBlendedPixmapFromFile(WMWidgetScreen(panel->win), file, &color, iwidth, iheight);

	if (!pixmap) {
		WMSetButtonEnabled(panel->okButton, false);

		WMSetLabelText(panel->iconView, _("Could not load image file "));

		WMSetLabelImage(panel->iconView, NULL);
	} else {
		WMSetButtonEnabled(panel->okButton, true);

		WMSetLabelText(panel->iconView, NULL);
		WMSetLabelImage(panel->iconView, pixmap);
		WMReleasePixmap(pixmap);
	}
}

static void listCallback(void *self, void *data)
{
	WMList *lPtr = (WMList *) self;
	IconPanel *panel = (IconPanel *) data;
	char *path;

	if (lPtr == panel->dirList) {
		WMListItem *item = WMGetListSelectedItem(lPtr);

		if (item == NULL)
			return;
		path = item->text;

		WMSetTextFieldText(panel->fileField, path);

		WMSetLabelImage(panel->iconView, NULL);

		WMSetButtonEnabled(panel->okButton, false);

		WMClearList(panel->iconList);
		listPixmaps(panel->scr, panel->iconList, path);
	} else {
		char *tmp, *iconFile;
		WMListItem *item = WMGetListSelectedItem(panel->dirList);

		if (item == NULL)
			return;
		path = item->text;

		item = WMGetListSelectedItem(panel->iconList);
		if (item == NULL)
			return;
		iconFile = item->text;

		tmp = wexpandpath(path);
		path = wmalloc(strlen(tmp) + strlen(iconFile) + 2);
		snprintf(path, strlen(tmp) + strlen(iconFile) + 2, "%s/%s", tmp, iconFile);
		wfree(tmp);
		WMSetTextFieldText(panel->fileField, path);
		setViewedImage(panel, path);
		wfree(path);
	}
}

static void listIconPaths(WMList * lPtr)
{
	char *paths;
	char *path;

	paths = wstrdup(w_global.config->icon_path);

	path = strtok(paths, ":");

	do {
		char *tmp;

		tmp = wexpandpath(path);
		/* do not sort, because the order implies the order of
		 * directories searched */
		if (access(tmp, X_OK) == 0)
			WMAddListItem(lPtr, path);
		wfree(tmp);
	} while ((path = strtok(NULL, ":")) != NULL);

	wfree(paths);
}

static void drawIconProc(WMList * lPtr, int index, xcb_drawable_t d, char *text, int state, WMRect * rect)
{
	IconPanel *panel = WMGetHangedData(lPtr);
	WScreen *scr = panel->scr;
	char *file, *dirfile;
	WMPixmap *pixmap;
	WMColor *back;
	WMSize size;
	WMScreen *wmscr = WMWidgetScreen(panel->win);
	RColor color;
	cairo_t *cr;
	bool selected;
	int x, y, width, height, len;
	int i, j;
	int fheight, tlen, twidth;
	int ofx, ofy;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) index;

	if (!panel->preview)
		return;

	x = rect->pos.x;
	y = rect->pos.y;
	width = rect->size.width;
	height = rect->size.height;

	selected = (state & WLDSSelected) != 0;
	back = selected ? W_SelectedBackColor(wmscr) : scr->gray;

	dirfile = wexpandpath(WMGetListSelectedItem(panel->dirList)->text);
	len = strlen(dirfile) + strlen(text) + 4;
	file = wmalloc(len);
	snprintf(file, len, "%s/%s", dirfile, text);
	wfree(dirfile);

	color.red = WMRedComponentOfColor(back) >> 8;
	color.green = WMGreenComponentOfColor(back) >> 8;
	color.blue = WMBlueComponentOfColor(back) >> 8;
	color.alpha = WMGetColorAlpha(back) >> 8;

	pixmap = WMCreateScaledBlendedPixmapFromFile(wmscr, file, &color, width - 2, height - 2);
	wfree(file);

	if (!pixmap) {
		/*WMRemoveListItem(lPtr, index); */
		if (selected)
			WMReleaseColor(back);
		return;
	}

	cr = WCairoBeginForDrawable(wmscr, d);
	W_SetSourceColor(cr, back);
	cairo_rectangle(cr, x, y, width, height);
	cairo_fill(cr);

	W_SetSourceColor(cr, scr->white);
	cairo_rectangle(cr, x, y + height - 1, width, 1);
	cairo_fill(cr);

	size = WMGetPixmapSize(pixmap);

	/* WMCreateScaledBlendedPixmapFromFile already fits the image to
	 * the row cell so the extra 100x64 clip only ever bites when the
	 * loader could not scale, keep it as a guard */
	cairo_save(cr);
	cairo_rectangle(cr, x + (width - size.width) / 2, y + 2,
			size.width > 100 ? 100 : size.width,
			size.height > 64 ? 64 : size.height);
	cairo_clip(cr);
	W_PaintPixmap(cr, pixmap, x + (width - size.width) / 2, y + 2);
	cairo_restore(cr);

	fheight = WMFontHeight(panel->normalfont);
	tlen = strlen(text);
	twidth = WMWidthOfString(panel->normalfont, text, tlen);
	ofx = x + (width - twidth) / 2;
	ofy = y + 64 - fheight;

	for (i = -1; i < 2; i++)
		for (j = -1; j < 2; j++)
			W_DrawString(cr, scr->white, panel->normalfont,
					  ofx + i, ofy + j, text, tlen);

	W_DrawString(cr, scr->black, panel->normalfont, ofx, ofy, text, tlen);
	WCairoEnd(cr);

	WMReleasePixmap(pixmap);
	if (selected)
		WMReleaseColor(back);
	/* I hope it is better to do not use cache / on my box it is fast nuff */
	wXFlush(wdisp->conn);
}

static WMList *createIconList(IconPanel *panel, bool thumbnails)
{
	WMList *list;
	int wmScaleWidth, wmScaleHeight;

	WMGetScaleBaseFromSystemFont(WMWidgetScreen(panel->win), &wmScaleWidth, &wmScaleHeight);

	list = WMCreateList(panel->win);
	WMResizeWidget(list, WMScaleX(220), WMScaleY(170));
	WMMoveWidget(list, WMScaleX(280), WMScaleY(30));
	WMSetListAction(list, listCallback, panel);
	WMHangData(list, panel);

	if (thumbnails) {
		WMSetListUserDrawItemHeight(list, 68);
		WMSetListUserDrawProc(list, drawIconProc);
	}

	return list;
}

static void replaceIconList(IconPanel *panel, bool thumbnails)
{
	WMList *oldList = panel->iconList;
	WMList *newList;
	int selectedRow, topItem, count, i;

	selectedRow = WMGetListSelectedItemRow(oldList);
	topItem = WMGetListPosition(oldList);
	count = WMGetListNumberOfRows(oldList);

	panel->imageMode = thumbnails;

	newList = createIconList(panel, thumbnails);
	panel->iconList = newList;

	for (i = 0; i < count; i++)
		WMAddListItem(newList, WMGetListItem(oldList, i)->text);

	WMRealizeWidget(newList);
	WMMapWidget(newList);

	if (selectedRow >= 0)
		WMSelectListItem(newList, selectedRow);

	WMSetListPosition(newList, topItem);

	WMDestroyWidget(oldList);
}

static void buttonCallback(void *self, void *clientData)
{
	WMButton *bPtr = (WMButton *) self;
	IconPanel *panel = (IconPanel *) clientData;

	if (bPtr == panel->okButton) {
		panel->done = true;
		panel->result = true;
		WMBreakModalLoop(WMWidgetScreen(panel->win));
	} else if (bPtr == panel->cancelButton) {
		panel->done = true;
		panel->result = false;
		WMBreakModalLoop(WMWidgetScreen(panel->win));
	} else if (bPtr == panel->textButton || bPtr == panel->imageButton) {
		bool thumbnails = WMGetButtonSelected(panel->imageButton) != 0;

		if (thumbnails != panel->imageMode)
			replaceIconList(panel, thumbnails);
	}
#if 0
	else if (bPtr == panel->chooseButton) {
		WMOpenPanel *op;

		op = WMCreateOpenPanel(WMWidgetScreen(bPtr));

		if (WMRunModalFilePanelForDirectory(op, NULL, "/usr/local", NULL, NULL)) {
			char *path;
			path = WMGetFilePanelFile(op);
			WMSetTextFieldText(panel->fileField, path);
			setViewedImage(panel, path);
			wfree(path);
		}
		WMDestroyFilePanel(op);
	}
#endif
}

static void keyPressHandler(xcb_generic_event_t * event, void *data)
{
	IconPanel *panel = (IconPanel *) data;
	xkb_keysym_t ksym;

	if ((event->response_type & 0x7f) == XCB_KEY_RELEASE)
		return;

	ksym = wXkbKeycodeToKeysym(((xcb_key_press_event_t *)event)->detail, 0);

	switch (ksym) {
	case XKB_KEY_Return:
		WMPerformButtonClick(panel->okButton);
		break;
	case XKB_KEY_Escape:
		WMPerformButtonClick(panel->cancelButton);
		break;
	}
}

bool wIconChooserDialog(WScreen *scr, char **file, const char *instance, const char *class,
			WWindow *for_window)
{
	WWindow *wwin;
	WWindow *prevFocused;
	xcb_window_t parent;
	xcb_window_t owner;
	IconPanel *panel;
	WMColor *color;
	WMFont *boldFont;
	bool result;
	bool setModal = false;
	int wmScaleWidth, wmScaleHeight;
	int pwidth, pheight;
	static const char *prefix = NULL;
	char *title;
	int len;
	WMPoint center;

	panel = wmalloc(sizeof(IconPanel));

	panel->scr = scr;

	panel->win = WMCreateWindow(scr->wmscreen, "iconChooser");
	WMGetScaleBaseFromSystemFont(scr->wmscreen, &wmScaleWidth, &wmScaleHeight);
	pwidth = WMScaleX(620);
	pheight = WMScaleY(280);
	WMResizeWidget(panel->win, pwidth, pheight);

	WMCreateEventHandler(WMWidgetView(panel->win), XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE, keyPressHandler, panel);

	boldFont = WMBoldSystemFontOfSize(scr->wmscreen, WMScaleY(12));
	panel->normalfont = WMSystemFontOfSize(WMWidgetScreen(panel->win), WMScaleY(12));

	panel->dirLabel = WMCreateLabel(panel->win);
	WMResizeWidget(panel->dirLabel, WMScaleX(260), WMScaleY(20));
	WMMoveWidget(panel->dirLabel, WMScaleX(10), WMScaleY(7));
	WMSetLabelText(panel->dirLabel, _("Directories"));
	WMSetLabelFont(panel->dirLabel, boldFont);
	WMSetLabelTextAlignment(panel->dirLabel, WACenter);

	WMSetLabelRelief(panel->dirLabel, WRSunken);

	panel->iconLabel = WMCreateLabel(panel->win);
	WMResizeWidget(panel->iconLabel, WMScaleX(220), WMScaleY(20));
	WMMoveWidget(panel->iconLabel, WMScaleX(280), WMScaleY(7));
	WMSetLabelText(panel->iconLabel, _("Icons"));
	WMSetLabelFont(panel->iconLabel, boldFont);
	WMSetLabelTextAlignment(panel->iconLabel, WACenter);

	WMReleaseFont(boldFont);

	color = WMWhiteColor(scr->wmscreen);
	WMSetLabelTextColor(panel->dirLabel, color);
	WMSetLabelTextColor(panel->iconLabel, color);
	WMReleaseColor(color);

	color = WMDarkGrayColor(scr->wmscreen);
	WMSetWidgetBackgroundColor(panel->iconLabel, color);
	WMSetWidgetBackgroundColor(panel->dirLabel, color);
	WMReleaseColor(color);

	WMSetLabelRelief(panel->iconLabel, WRSunken);

	panel->dirList = WMCreateList(panel->win);
	WMResizeWidget(panel->dirList, WMScaleX(260), WMScaleY(170));
	WMMoveWidget(panel->dirList, WMScaleX(10), WMScaleY(30));
	WMSetListAction(panel->dirList, listCallback, panel);

	panel->iconList = createIconList(panel, false);

	panel->modeFrame = WMCreateFrame(panel->win);
	WMResizeWidget(panel->modeFrame, WMScaleX(100), WMScaleY(70));
	WMMoveWidget(panel->modeFrame, WMScaleX(510), WMScaleY(130));
	WMSetFrameTitle(panel->modeFrame, _("List Mode"));

	panel->textButton = WMCreateButton(panel->modeFrame, WBTRadio);
	WMResizeWidget(panel->textButton, WMScaleX(84), WMScaleY(18));
	WMMoveWidget(panel->textButton, WMScaleX(8), WMScaleY(20));
	WMSetButtonText(panel->textButton, _("Names"));
	WMSetButtonSelected(panel->textButton, true);
	WMSetButtonAction(panel->textButton, buttonCallback, panel);

	panel->imageButton = WMCreateButton(panel->modeFrame, WBTRadio);
	WMResizeWidget(panel->imageButton, WMScaleX(84), WMScaleY(18));
	WMMoveWidget(panel->imageButton, WMScaleX(8), WMScaleY(38));
	WMSetButtonText(panel->imageButton, _("Thumbs"));
	WMSetButtonAction(panel->imageButton, buttonCallback, panel);

	WMGroupButtons(panel->textButton, panel->imageButton);

	panel->previewFrame = WMCreateFrame(panel->win);
	WMResizeWidget(panel->previewFrame, WMScaleX(100), WMScaleY(95));
	WMMoveWidget(panel->previewFrame, WMScaleX(510), WMScaleY(30));
	WMSetFrameTitle(panel->previewFrame, _("Preview"));

	panel->iconView = WMCreateLabel(panel->previewFrame);
	WMResizeWidget(panel->iconView, WMScaleX(82), WMScaleY(70));
	WMMoveWidget(panel->iconView, WMScaleX(9), WMScaleY(20));
	WMSetLabelImagePosition(panel->iconView, WIPOverlaps);
	WMSetLabelRelief(panel->iconView, WRSunken);
	WMSetLabelTextAlignment(panel->iconView, WACenter);

	panel->fileLabel = WMCreateLabel(panel->win);
	WMResizeWidget(panel->fileLabel, WMScaleX(80), WMScaleY(20));
	WMMoveWidget(panel->fileLabel, WMScaleX(10), WMScaleY(210));
	WMSetLabelText(panel->fileLabel, _("File Name:"));

	panel->fileField = WMCreateTextField(panel->win);
	WMSetViewNextResponder(WMWidgetView(panel->fileField), WMWidgetView(panel->win));
	WMResizeWidget(panel->fileField, WMScaleX(515), WMScaleY(20));
	WMMoveWidget(panel->fileField, WMScaleX(95), WMScaleY(210));
	WMSetTextFieldEditable(panel->fileField, false);

	panel->okButton = WMCreateCommandButton(panel->win);
	WMResizeWidget(panel->okButton, WMScaleX(80), WMScaleY(26));
	WMMoveWidget(panel->okButton, WMScaleX(530), WMScaleY(242));
	WMSetButtonText(panel->okButton, _("OK"));
	WMSetButtonEnabled(panel->okButton, false);
	WMSetButtonAction(panel->okButton, buttonCallback, panel);

	panel->cancelButton = WMCreateCommandButton(panel->win);
	WMResizeWidget(panel->cancelButton, WMScaleX(80), WMScaleY(26));
	WMMoveWidget(panel->cancelButton, WMScaleX(440), WMScaleY(242));
	WMSetButtonText(panel->cancelButton, _("Cancel"));
	WMSetButtonAction(panel->cancelButton, buttonCallback, panel);
#if 0
	panel->chooseButton = WMCreateCommandButton(panel->win);
	WMResizeWidget(panel->chooseButton, WMScaleX(110), WMScaleY(26));
	WMMoveWidget(panel->chooseButton, WMScaleX(150), WMScaleY(242));
	WMSetButtonText(panel->chooseButton, _("Choose File"));
	WMSetButtonAction(panel->chooseButton, buttonCallback, panel);
#endif
	WMRealizeWidget(panel->win);
	WMMapSubwidgets(panel->win);
	WMMapSubwidgets(panel->previewFrame);
	WMMapSubwidgets(panel->modeFrame);

	pwidth = WSCALE(scr, pwidth);
	pheight = WSCALE(scr, pheight);

	parent = xcb_generate_id(wdisp->conn);
	xcb_create_window(wdisp->conn, XCB_COPY_FROM_PARENT, parent, scr->root_win, 0, 0, pwidth, pheight, 0,
			  XCB_WINDOW_CLASS_COPY_FROM_PARENT, XCB_COPY_FROM_PARENT,
			  XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL, (const uint32_t[]){ 0, 0});

	xcb_reparent_window(wdisp->conn, WMWidgetXID(panel->win), parent, 0, 0);

	if (prefix == NULL)
		prefix = _("Icon Chooser");

	len = strlen(prefix)
		+ 2	// " ["
		+ (instance ? strlen(instance) : 0)
		+ 1	// "."
		+ (class ? strlen(class) : 0)
		+ 1	// "]"
		+ 1;	// final NUL

	title = wmalloc(len);
	if (instance || class)
		snprintf(title, len, "%s [%s%s%s]", prefix,
		         instance ? instance : "", instance && class ? "." : "", class ? class : "");
	else
		snprintf(title, len, "%s", prefix);

	center = wGetCenter(scr, pwidth, pheight);

	/* remember who owns focus so it can be restored on close,
	 * wUnmanageWindow() would otherwise hand focus to the next
	 * window in the focus list */
	prevFocused = scr->focused_window;
	owner = for_window ? for_window->client_win : XCB_NONE;

	wwin = wManageInternalWindow(scr, parent, owner,
				     title, center.x, center.y, pwidth, pheight);
	wfree(title);

	/* let handleKeyPress() redispatch keys aimed at the raw wrapper
	 * into the WINGs widget tree, key interest is selected centrally
	 * via wwin->event_mask */
	wwin->client_leader = WMWidgetXID(panel->win);

	if (owner != XCB_NONE)
		WXSetTransientFor(wdisp->conn, parent, owner);

	/* put icon paths in the list */
	listIconPaths(panel->dirList);

	WMMapWidget(panel->win);

	wWindowMap(wwin);
	wSetFocusTo(scr, wwin);
	WMSetFocusToWidget(panel->dirList);

	if (w_global.program.state == WSTATE_NORMAL) {
		WCHANGE_STATE(WSTATE_MODAL);
		setModal = true;
	}

	WMRunModalLoop(WMWidgetScreen(panel->win), WMWidgetView(panel->win));

	if (setModal && w_global.program.state == WSTATE_MODAL)
		WCHANGE_STATE(WSTATE_NORMAL);

	if (panel->result) {
		char *defaultPath, *wantedPath;
		WMListItem *sel;

		sel = WMGetListSelectedItem(panel->iconList);
		/* copy out of the list, the item owns its text buffer */
		*file = (sel && sel->text) ? wstrdup(sel->text) : NULL;
		if (!*file || **file == 0) {
			wfree(*file);
			*file = NULL;
		} else {
			defaultPath = FindImage(w_global.config->icon_path, *file);
			wantedPath = WMGetTextFieldText(panel->fileField);
			if (defaultPath) {
				/* if the file is not the default, use full path */
				if (strcmp(wantedPath, defaultPath) != 0) {
					wfree(*file);
					*file = wstrdup(wantedPath);
				}
				wfree(defaultPath);
			}
			wfree(wantedPath);
		}
	} else {
		*file = NULL;
	}

	result = panel->result;

	WMReleaseFont(panel->normalfont);

	WMUnmapWidget(panel->win);

	WMDestroyWidget(panel->win);

	wUnmanageWindow(wwin, false, false);

	/* destroy the chooser's X window before restoring focus, otherwise
	 * the server delivers a trailing FocusIn that undoes the restore */
	xcb_destroy_window(wdisp->conn, parent);

	/* hand focus back to the opener. Clicking its WINGs widgets never
	 * moves WM focus, so fall back to whoever held focus before if
	 * the opener is gone */
	if (for_window && wWindowFor(owner) == for_window)
		wSetFocusTo(scr, for_window);
	else if (prevFocused && prevFocused->screen_ptr == scr &&
		 wWindowFor(prevFocused->client_win) == prevFocused)
		wSetFocusTo(scr, prevFocused);

	wfree(panel);

	return result;
}
