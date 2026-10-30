/* dockedapp.c- docked application settings panel
 *
 *  Window Maker window manager
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

#include "wconfig.h"

#include <xkbcommon/xkbcommon-keysyms.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "WindowMaker.h"
#include "window.h"
#include "icon.h"
#include "appicon.h"
#include "dock.h"
#include "dockedapp.h"
#include "dialog.h"
#include "dialog_icon.h"
#include "misc.h"
#include "wxkb.h"
#include "defaults.h"
#include "framewin.h"
#include "xinerama.h"


static void updateCommand(WAppIcon * icon, char *command)
{
	if (icon->command)
		wfree(icon->command);
	if (command && (command[0] == 0 || (command[0] == '-' && command[1] == 0))) {
		wfree(command);
		command = NULL;
	}
	icon->command = command;

	if (!icon->wm_class && !icon->wm_instance && icon->command && strlen(icon->command) > 0) {
		icon->forced_dock = 1;
	}
}

static void updatePasteCommand(WAppIcon * icon, char *command)
{
	if (icon->paste_command)
		wfree(icon->paste_command);
	if (command && (command[0] == 0 || (command[0] == '-' && command[1] == 0))) {
		wfree(command);
		command = NULL;
	}
	icon->paste_command = command;
}

#ifdef USE_DOCK_XDND
static void updateDNDCommand(WAppIcon * icon, char *command)
{
	if (icon->dnd_command)
		wfree(icon->dnd_command);
	if (command && (command[0] == 0 || (command[0] == '-' && command[1] == 0))) {
		wfree(command);
		command = NULL;
	}
	icon->dnd_command = command;
}
#endif	/* USE_DOCK_XDND */

static void updateSettingsPanelIcon(AppSettingsPanel * panel)
{
	char *file;
	int size;

	file = WMGetTextFieldText(panel->iconField);
	if (!file || file[0] == 0) {
		/* nothing configured, preview the global default app icon
		 * so the borderless button is not an invisible click area */
		wfree(file);
		file = wDefaultGetIconFile(NULL, NULL, true);
		file = file ? wstrdup(file) : NULL;
	}
	if (!file)
		WMSetButtonImage(panel->iconBtn, NULL);
	else {
		char *path;

		path = FindImage(w_global.config->icon_path, file);
		if (!path) {
			wwarning(_("could not find icon %s, used in a docked application"), file);
			wfree(file);
			WMSetButtonImage(panel->iconBtn, NULL);
			return;
		} else {
			WMPixmap *pixmap;
			RColor color;

			color.red = 0xae;
			color.green = 0xaa;
			color.blue = 0xae;
			color.alpha = 0;
			size = w_global.config->icon_size;
			pixmap = WMCreateScaledBlendedPixmapFromFile(WMWidgetScreen(panel->win), path, &color, size, size);
			if (!pixmap) {
				WMSetButtonImage(panel->iconBtn, NULL);
			} else {
				WMSetButtonImage(panel->iconBtn, pixmap);
				WMReleasePixmap(pixmap);
			}
		}
		wfree(file);
		wfree(path);
	}
}

static void chooseIconCallback(WMWidget * self, void *clientData)
{
	char *file;
	AppSettingsPanel *panel = (AppSettingsPanel *) clientData;
	int result;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;

	panel->choosingIcon = 1;

	WMSetButtonEnabled(panel->browseBtn, false);
	WMSetButtonEnabled(panel->iconBtn, false);

	result = wIconChooserDialog(panel->wwin->screen_ptr, &file,
				    panel->editedIcon->wm_instance, panel->editedIcon->wm_class,
				    panel->wwin);

	panel->choosingIcon = 0;
	if (!panel->destroyed) {
		if (result) {
			WMSetTextFieldText(panel->iconField, file);
			updateSettingsPanelIcon(panel);
		}

		WMSetButtonEnabled(panel->browseBtn, true);
		WMSetButtonEnabled(panel->iconBtn, true);
	} else {
		/* kluge for the case, the user asked to close the panel before
		 * the icon chooser */
		DestroyDockAppSettingsPanel(panel);
	}
	if (result)
		wfree(file);
}

static void panelBtnCallback(WMWidget * self, void *data)
{
	WMButton *btn = self;
	AppSettingsPanel *panel = (AppSettingsPanel *) data;
	char *text;

	if (panel->okBtn == btn) {
		text = WMGetTextFieldText(panel->iconField);
		if (text[0] == 0) {
			wfree(text);
			text = NULL;
		}

		if (!wIconChangeImageFile(panel->editedIcon->icon, text)) {
			char *buf;
			int len = (text ? strlen(text) : 0) + 64;

			buf = wmalloc(len);
			snprintf(buf, len, _("Could not open specified icon file: %s"), text ? text : "(none)");
			if (wMessageDialog(panel->wwin->screen_ptr, _("Error"), buf,
					   _("OK"), _("Ignore"), NULL) == WAPRDefault) {
				wfree(text);
				wfree(buf);
				return;
			}
			wfree(buf);
		} else {
			WAppIcon *aicon = panel->editedIcon;

			// Cf dock.c:dockIconPaint(WAppIcon *aicon)?
			if (aicon == aicon->icon->core->screen_ptr->clip_icon)
				wClipIconPaint(aicon);
			else if (wIsADrawer(aicon))
				wDrawerIconPaint(aicon);
			else
				wAppIconPaint(aicon);

			wDefaultChangeIcon(aicon->wm_instance, aicon->wm_class, text);
		}
		if (text)
			wfree(text);

		/* cannot free text from this, because it will be not be duplicated
		 * in updateCommand */
		text = WMGetTextFieldText(panel->commandField);
		if (text[0] == 0) {
			wfree(text);
			text = NULL;
		}
		updateCommand(panel->editedIcon, text);
#ifdef USE_DOCK_XDND
		/* cannot free text from this, because it will be not be duplicated
		 * in updateDNDCommand */
		text = WMGetTextFieldText(panel->dndCommandField);
		updateDNDCommand(panel->editedIcon, text);
#endif
		text = WMGetTextFieldText(panel->pasteCommandField);
		updatePasteCommand(panel->editedIcon, text);

		panel->editedIcon->auto_launch = WMGetButtonSelected(panel->autoLaunchBtn);

		panel->editedIcon->lock = WMGetButtonSelected(panel->lockBtn);
	}

	DestroyDockAppSettingsPanel(panel);
}

static void keyPressHandler(xcb_generic_event_t *event, void *data)
{
	AppSettingsPanel *panel = (AppSettingsPanel *) data;

	if (wXkbKeycodeToKeysym(((xcb_key_press_event_t *)event)->detail, 0) == XKB_KEY_Escape)
		WMPerformButtonClick(panel->cancelBtn);
}

void ShowDockAppSettingsPanel(WAppIcon * aicon)
{
	AppSettingsPanel *panel;
	WScreen *scr = aicon->icon->core->screen_ptr;
	xcb_window_t parent;
	WMFont *font;
	int x, y;
	WMBox *vbox;
	WMBox *hbox;
	WMRect rect;
	WMColor *grey;
	int wmScaleWidth, wmScaleHeight;
	int pwidth, pheight;
	int iconSize, headerH, headerX, textW, nameH, nameY, commentH;
	const char *fallback;

	/* get the width and height values of the system font for use with the WMScaleX/WMScaleY macros */
	WMGetScaleBaseFromSystemFont(scr->wmscreen, &wmScaleWidth, &wmScaleHeight);

	/* the header band beside the icon holds the bold name over an
	 * optional two-line description, both centered as a block so
	 * the name lines up with an icon image that is itself centered
	 * inside the icon_size button when smaller than icon_size */
	iconSize = w_global.config->icon_size;
	nameH = WMScaleY(18);
	commentH = WMScaleY(28);
	headerH = nameH + commentH;
	if (headerH < iconSize)
		headerH = iconSize;

	pwidth = WMScaleX(300);
	pheight = WMScaleY(10)                  /* upper margin */
		+ headerH                       /* icon, name and description */
		+ WMScaleY(10)                  /* padding */
		+ WMScaleY(20) + WMScaleY(2)    /* start option */
		+ WMScaleY(20) + WMScaleY(5)    /* lock option */
		+ WMScaleY(50) + WMScaleY(5)    /* app path and arguments */
		+ WMScaleY(70) + WMScaleY(5)    /* middle-click command */
		+ WMScaleY(70) + WMScaleY(5)    /* drag&drop command */
		+ WMScaleY(50) + WMScaleY(10)   /* icon file */
		+ WMScaleY(24)                  /* buttons */
		+ WMScaleY(10);                 /* lower margin */

	panel = wmalloc(sizeof(AppSettingsPanel));

	panel->editedIcon = aicon;

	aicon->panel = panel;
	aicon->editing = 1;

	panel->win = WMCreateWindow(scr->wmscreen, "applicationSettings");
	WMResizeWidget(panel->win, pwidth, pheight);

	/* Escape anywhere in the panel closes it */
	WMCreateEventHandler(WMWidgetView(panel->win), XCB_EVENT_MASK_KEY_PRESS, keyPressHandler, panel);

	/* clickable icon preview, also reachable from the Browse button so
	 * keyboard-only users keep a path to the chooser */
	panel->iconBtn = WMCreateCustomButton(panel->win, WBBSpringLoadedMask);
	WMResizeWidget(panel->iconBtn, iconSize, iconSize);
	WMMoveWidget(panel->iconBtn, WMScaleX(10), WMScaleY(10) + (headerH - iconSize) / 2);
	WMSetButtonImagePosition(panel->iconBtn, WIPImageOnly);
	WMSetButtonBordered(panel->iconBtn, false);
	WMSetButtonAction(panel->iconBtn, chooseIconCallback, panel);
	WMSetBalloonTextForView(_("Click to choose an icon"), WMWidgetView(panel->iconBtn));

	headerX = 2 * WMScaleX(10) + iconSize;
	textW = pwidth - headerX - WMScaleX(10);

	if (aicon->wm_class && strcmp(aicon->wm_class, "DockApp") == 0)
		fallback = aicon->wm_instance;
	else if (aicon->wm_class && strcmp(aicon->wm_class, "GNUstep") == 0)
		fallback = aicon->wm_instance;
	else
		fallback = aicon->wm_class;

	/* with a description the name+description block centers in the
	 * band, without one the name alone centers like the pre-0.97
	 * panel */
	if (aicon->comment)
		nameY = WMScaleY(10) + (headerH - nameH - commentH) / 2;
	else
		nameY = WMScaleY(10) + (headerH - nameH) / 2;

	panel->nameLabel = WMCreateLabel(panel->win);
	font = WMBoldSystemFontOfSize(scr->wmscreen, WMScaleY(14));
	WMResizeWidget(panel->nameLabel, textW, nameH);
	WMMoveWidget(panel->nameLabel, headerX, nameY);
	WMSetLabelTextAlignment(panel->nameLabel, WALeft);
	WMSetLabelFont(panel->nameLabel, font);
	WMReleaseFont(font);
	WMSetLabelText(panel->nameLabel,
		       aicon->display_name ? aicon->display_name : fallback);

	if (aicon->comment) {
		panel->commentLabel = WMCreateLabel(panel->win);
		font = WMSystemFontOfSize(scr->wmscreen, WMScaleY(11));
		grey = WMDarkGrayColor(scr->wmscreen);
		WMResizeWidget(panel->commentLabel, textW, commentH);
		WMMoveWidget(panel->commentLabel, headerX, nameY + nameH);
		WMSetLabelTextAlignment(panel->commentLabel, WALeft);
		WMSetLabelFont(panel->commentLabel, font);
		WMSetLabelTextColor(panel->commentLabel, grey);
		WMSetLabelWraps(panel->commentLabel, true);
		WMSetLabelText(panel->commentLabel, aicon->comment);
		WMReleaseFont(font);
		WMReleaseColor(grey);
	}

	vbox = WMCreateBox(panel->win);
	WMResizeWidget(vbox, pwidth - 2 * WMScaleX(10), pheight - headerH - 3 * WMScaleY(10));
	WMMoveWidget(vbox, WMScaleX(10), headerH + 2 * WMScaleY(10));

	panel->autoLaunchBtn = WMCreateSwitchButton(vbox);
	WMAddBoxSubview(vbox, WMWidgetView(panel->autoLaunchBtn), false, true, WMScaleY(20), WMScaleY(20), WMScaleY(2));
	WMSetButtonText(panel->autoLaunchBtn, _("Start when Window Maker is started"));
	WMSetButtonSelected(panel->autoLaunchBtn, aicon->auto_launch);

	panel->lockBtn = WMCreateSwitchButton(vbox);
	WMAddBoxSubview(vbox, WMWidgetView(panel->lockBtn), false, true, WMScaleY(20), WMScaleY(20), WMScaleY(5));
	WMSetButtonText(panel->lockBtn, _("Lock (prevent accidental removal)"));
	WMSetButtonSelected(panel->lockBtn, aicon->lock);

	panel->commandFrame = WMCreateFrame(vbox);
	WMSetFrameTitle(panel->commandFrame, _("Application path and arguments"));
	WMAddBoxSubview(vbox, WMWidgetView(panel->commandFrame), false, true, WMScaleY(50), WMScaleY(50), WMScaleY(5));

	panel->commandField = WMCreateTextField(panel->commandFrame);
	WMResizeWidget(panel->commandField, WMScaleX(260), WMScaleY(20));
	WMMoveWidget(panel->commandField, WMScaleX(10), WMScaleY(20));
	WMSetTextFieldText(panel->commandField, aicon->command);

	WMMapSubwidgets(panel->commandFrame);

	panel->pasteCommandFrame = WMCreateFrame(vbox);
	WMSetFrameTitle(panel->pasteCommandFrame, _("Command for middle-click launch"));
	WMAddBoxSubview(vbox, WMWidgetView(panel->pasteCommandFrame), false, true, WMScaleY(70), WMScaleY(70), WMScaleY(5));

	panel->pasteCommandField = WMCreateTextField(panel->pasteCommandFrame);
	WMResizeWidget(panel->pasteCommandField, WMScaleX(260), WMScaleY(20));
	WMMoveWidget(panel->pasteCommandField, WMScaleX(10), WMScaleY(20));

	panel->pasteCommandLabel = WMCreateLabel(panel->pasteCommandFrame);
	WMResizeWidget(panel->pasteCommandLabel, WMScaleX(260), WMScaleY(18));
	WMMoveWidget(panel->pasteCommandLabel, WMScaleX(10), WMScaleY(45));

	WMSetTextFieldText(panel->pasteCommandField, aicon->paste_command);
	WMSetLabelText(panel->pasteCommandLabel, _("%s will be replaced with current selection"));
	WMMapSubwidgets(panel->pasteCommandFrame);

	panel->dndCommandFrame = WMCreateFrame(vbox);
	WMSetFrameTitle(panel->dndCommandFrame, _("Command for dragged and dropped files"));
	WMAddBoxSubview(vbox, WMWidgetView(panel->dndCommandFrame), false, true, WMScaleY(70), WMScaleY(70), WMScaleY(5));

	panel->dndCommandField = WMCreateTextField(panel->dndCommandFrame);
	WMResizeWidget(panel->dndCommandField, WMScaleX(260), WMScaleY(20));
	WMMoveWidget(panel->dndCommandField, WMScaleX(10), WMScaleY(20));

	panel->dndCommandLabel = WMCreateLabel(panel->dndCommandFrame);
	WMResizeWidget(panel->dndCommandLabel, WMScaleX(260), WMScaleY(18));
	WMMoveWidget(panel->dndCommandLabel, WMScaleX(10), WMScaleY(45));
#ifdef USE_DOCK_XDND
	WMSetTextFieldText(panel->dndCommandField, aicon->dnd_command);
	WMSetLabelText(panel->dndCommandLabel, _("%d will be replaced with the file name"));
#else
	WMSetTextFieldEditable(panel->dndCommandField, false);
	WMSetLabelText(panel->dndCommandLabel, _("XDnD support was not compiled in"));

	WMSetFrameTitleColor(panel->dndCommandFrame, WMDarkGrayColor(scr->wmscreen));
	WMSetLabelTextColor(panel->dndCommandLabel, WMDarkGrayColor(scr->wmscreen));
#endif
	WMMapSubwidgets(panel->dndCommandFrame);

	panel->iconFrame = WMCreateFrame(vbox);
	WMSetFrameTitle(panel->iconFrame, _("Icon Image"));
	WMAddBoxSubview(vbox, WMWidgetView(panel->iconFrame), false, true, WMScaleY(50), WMScaleY(50), WMScaleY(10));

	panel->iconField = WMCreateTextField(panel->iconFrame);
	WMResizeWidget(panel->iconField, WMScaleX(180), WMScaleY(20));
	WMMoveWidget(panel->iconField, WMScaleX(10), WMScaleY(20));
	WMSetTextFieldText(panel->iconField, wDefaultGetIconFile(aicon->wm_instance, aicon->wm_class, false));

	panel->browseBtn = WMCreateCommandButton(panel->iconFrame);
	WMResizeWidget(panel->browseBtn, WMScaleX(70), WMScaleY(24));
	WMMoveWidget(panel->browseBtn, WMScaleX(200), WMScaleY(18));
	WMSetButtonText(panel->browseBtn, _("Browse..."));
	WMSetButtonAction(panel->browseBtn, chooseIconCallback, panel);

	hbox = WMCreateBox(vbox);
	WMSetBoxHorizontal(hbox, true);
	WMAddBoxSubview(vbox, WMWidgetView(hbox), false, true, WMScaleY(24), WMScaleY(24), 0);

	panel->okBtn = WMCreateCommandButton(hbox);
	WMSetButtonText(panel->okBtn, _("OK"));
	WMSetButtonAction(panel->okBtn, panelBtnCallback, panel);
	WMAddBoxSubviewAtEnd(hbox, WMWidgetView(panel->okBtn), false, true, WMScaleX(80), WMScaleX(80), 0);

	panel->cancelBtn = WMCreateCommandButton(hbox);
	WMSetButtonText(panel->cancelBtn, _("Cancel"));
	WMSetButtonAction(panel->cancelBtn, panelBtnCallback, panel);
	WMAddBoxSubviewAtEnd(hbox, WMWidgetView(panel->cancelBtn), false, true, WMScaleX(80), WMScaleX(80), 5);

	WMMapSubwidgets(hbox);

	WMSetViewNextResponder(WMWidgetView(panel->commandField), WMWidgetView(panel->win));
	WMSetViewNextResponder(WMWidgetView(panel->pasteCommandField), WMWidgetView(panel->win));
	WMSetViewNextResponder(WMWidgetView(panel->dndCommandField), WMWidgetView(panel->win));
	WMSetViewNextResponder(WMWidgetView(panel->iconField), WMWidgetView(panel->win));

	WMRealizeWidget(panel->win);
	WMMapSubwidgets(panel->win);
	WMMapSubwidgets(vbox);
	WMMapSubwidgets(panel->iconFrame);

	updateSettingsPanelIcon(panel);

	parent = xcb_generate_id(wdisp->conn);
	xcb_create_window(wdisp->conn, XCB_COPY_FROM_PARENT, parent, scr->root_win, 0, 0, pwidth, pheight, 0,
			  XCB_WINDOW_CLASS_COPY_FROM_PARENT, XCB_COPY_FROM_PARENT,
			  XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL, (const uint32_t[]){ 0, 0});

	xcb_reparent_window(wdisp->conn, WMWidgetXID(panel->win), parent, 0, 0);

	/*
	 * make things relative to head
	 */
	rect = wGetRectForHead(scr, wGetHeadForPointerLocation(scr));

	y = aicon->y_pos;
	if (y < 0)
		y = 0;
	else if (y + pheight > rect.pos.y + rect.size.height)
		y = rect.pos.y + rect.size.height - pheight - 3 * WMScaleY(10);

	if (aicon->dock && aicon->dock->type == WM_DOCK) {
		switch (aicon->dock->edge) {
		case WDOCK_EDGE_RIGHT:
			x = rect.pos.x + rect.size.width / 2;
			break;
		case WDOCK_EDGE_LEFT:
			x = rect.pos.x + rect.size.width / 2 - pwidth - WMScaleX(2);
			break;
		case WDOCK_EDGE_TOP:
			/* inner half of the head is the lower one */
			x = rect.pos.x + (rect.size.width - pwidth) / 2;
			y = rect.pos.y + rect.size.height / 2;
			break;
		case WDOCK_EDGE_BOTTOM:
		default:
			x = rect.pos.x + (rect.size.width - pwidth) / 2;
			y = rect.pos.y + rect.size.height / 2 - pheight - WMScaleY(2);
			break;
		}
	} else {
		x = rect.pos.x + (rect.size.width - pwidth) / 2;
	}

	panel->wwin = wManageInternalWindow(scr, parent, XCB_NONE,
					    _("Docked Application Settings"), x, y, pwidth, pheight);

	panel->wwin->client_leader = WMWidgetXID(panel->win);

	panel->parent = parent;

	WMMapWidget(panel->win);

	wWindowMap(panel->wwin);
}

void DestroyDockAppSettingsPanel(AppSettingsPanel * panel)
{
	if (!panel->destroyed) {
		xcb_unmap_window(wdisp->conn, panel->wwin->client_win);
		xcb_reparent_window(wdisp->conn, panel->wwin->client_win, panel->wwin->screen_ptr->root_win, 0, 0);
		wUnmanageWindow(panel->wwin, false, false);
	}

	panel->destroyed = 1;

	WMRemoveNotificationObserver(panel);

	/*
	 * kluge. If we destroy the panel before the icon chooser is closed,
	 * we will crash when it does close, trying to access something in the
	 * destroyed panel. Could use wretain()/wrelease() in the panel,
	 * but it is not working for some reason.
	 */
	if (panel->choosingIcon)
		return;

	WMDestroyWidget(panel->win);

	xcb_destroy_window(wdisp->conn, panel->parent);

	panel->editedIcon->panel = NULL;

	panel->editedIcon->editing = 0;

	wfree(panel);
}
