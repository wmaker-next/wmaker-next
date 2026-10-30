/* MenuPreferences.c- menu related preferences
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

typedef struct _Panel {
	WMBox *box;

	char *sectionName;

	char *description;

	CallbackRec callbacks;

	WMWidget *parent;

	WMFrame *scrF;
	WMButton *scrB[5];

	WMFrame *aliF;
	WMButton *aliyB;
	WMButton *alinB;

	WMFrame *optF;
	WMButton *autoB;
	WMButton *autoC;
	WMButton *wrapB;

} _Panel;

#define ICON_FILE	"menuprefs"
#define SPEED_IMAGE "speed%i"
#define SPEED_IMAGE_S "speed%is"

#define MENU_ALIGN1 "menualign1"
#define MENU_ALIGN2 "menualign2"

static void showData(_Panel * panel)
{
	WMPerformButtonClick(panel->scrB[GetSpeedForKey("MenuScrollSpeed")]);

	if (GetBoolForKey("AlignSubmenus"))
		WMPerformButtonClick(panel->aliyB);
	else
		WMPerformButtonClick(panel->alinB);

	WMSetButtonSelected(panel->wrapB, GetBoolForKey("WrapMenus"));

	WMSetButtonSelected(panel->autoB, GetBoolForKey("ScrollableMenus"));

	WMSetButtonSelected(panel->autoC, GetBoolForKey("ViKeyMenus"));
}

static void storeData(_Panel * panel)
{
	int i;

	for (i = 0; i < wlengthof(panel->scrB); i++) {
		if (WMGetButtonSelected(panel->scrB[i]))
			break;
	}
	SetSpeedForKey(i, "MenuScrollSpeed");

	SetBoolForKey(WMGetButtonSelected(panel->aliyB), "AlignSubmenus");

	SetBoolForKey(WMGetButtonSelected(panel->wrapB), "WrapMenus");
	SetBoolForKey(WMGetButtonSelected(panel->autoB), "ScrollableMenus");
	SetBoolForKey(WMGetButtonSelected(panel->autoC), "ViKeyMenus");
}

static void createPanel(Panel * p)
{
	_Panel *panel = (_Panel *) p;
	WMScreen *scr = WMWidgetScreen(panel->parent);
	int i;
	char buf[64];

	panel->box = WMCreateBox(panel->parent);
	WMSetViewExpandsToParent(WMWidgetView(panel->box), 2, 2, 2, 2);

    /***************** Menu Scroll Speed ****************/
	panel->scrF = WMCreateFrame(panel->box);
	WMResizeWidget(panel->scrF, 235, 90);
	WMMoveWidget(panel->scrF, 25, 20);
	WMSetFrameTitle(panel->scrF, _("Menu Scrolling Speed"));

	for (i = 0; i < wlengthof(panel->scrB); i++) {
		panel->scrB[i] = WMCreateCustomButton(panel->scrF, WBBStateChangeMask);
		WMResizeWidget(panel->scrB[i], 40, 40);
		WMMoveWidget(panel->scrB[i], 15 + (40 * i), 30);
		WMSetButtonBordered(panel->scrB[i], false);
		WMSetButtonImagePosition(panel->scrB[i], WIPImageOnly);
		if (i > 0) {
			WMGroupButtons(panel->scrB[0], panel->scrB[i]);
		}
		snprintf(buf, sizeof(buf), SPEED_IMAGE, i);
		SetButtonImageFromFile(scr, panel->scrB[i], buf, false);
		snprintf(buf, sizeof(buf), SPEED_IMAGE_S, i);
		SetButtonImageFromFile(scr, panel->scrB[i], buf, true);
	}

	WMMapSubwidgets(panel->scrF);

    /***************** Submenu Alignment ****************/

	panel->aliF = WMCreateFrame(panel->box);
	WMResizeWidget(panel->aliF, 220, 90);
	WMMoveWidget(panel->aliF, 280, 20);
	WMSetFrameTitle(panel->aliF, _("Submenu Alignment"));

	panel->alinB = WMCreateButton(panel->aliF, WBTOnOff);
	WMResizeWidget(panel->alinB, 48, 48);
	WMMoveWidget(panel->alinB, 56, 25);
	WMSetButtonImagePosition(panel->alinB, WIPImageOnly);
	SetButtonImageFromFile(scr, panel->alinB, MENU_ALIGN1, false);
	panel->aliyB = WMCreateButton(panel->aliF, WBTOnOff);
	WMResizeWidget(panel->aliyB, 48, 48);
	WMMoveWidget(panel->aliyB, 120, 25);
	WMSetButtonImagePosition(panel->aliyB, WIPImageOnly);
	SetButtonImageFromFile(scr, panel->aliyB, MENU_ALIGN2, false);
	WMGroupButtons(panel->alinB, panel->aliyB);

	WMMapSubwidgets(panel->aliF);

    /***************** Options ****************/
	panel->optF = WMCreateFrame(panel->box);
	WMResizeWidget(panel->optF, 475, 96);
	WMMoveWidget(panel->optF, 25, 120);

	panel->wrapB = WMCreateSwitchButton(panel->optF);
	WMResizeWidget(panel->wrapB, 440, 32);
	WMMoveWidget(panel->wrapB, 25, 8);
	WMSetButtonText(panel->wrapB, _("Always open submenus inside the screen, instead of scrolling."));

	panel->autoB = WMCreateSwitchButton(panel->optF);
	WMResizeWidget(panel->autoB, 440, 32);
	WMMoveWidget(panel->autoB, 25, 34);
	WMSetButtonText(panel->autoB, _("Scroll off-screen menus when pointer is moved over them."));
	panel->autoC = WMCreateSwitchButton(panel->optF);
	WMResizeWidget(panel->autoC, 440, 32);
	WMMoveWidget(panel->autoC, 25, 58);
	WMSetButtonText(panel->autoC, _("Use h/j/k/l keys to select menu options."));

	WMMapSubwidgets(panel->optF);

	WMRealizeWidget(panel->box);
	WMMapSubwidgets(panel->box);

	showData(panel);
}

Panel *InitMenuPreferences(WMWidget *parent)
{
	_Panel *panel;

	panel = wmalloc(sizeof(_Panel));

	panel->sectionName = _("Menu Preferences");

	panel->description = _("Menu usability related options. Scrolling speed,\n" "alignment of submenus etc.");

	panel->parent = parent;

	panel->callbacks.createWidgets = createPanel;
	panel->callbacks.updateDomain = storeData;
	panel->callbacks.undoChanges = showData;

	AddSection(panel, ICON_FILE);

	return panel;
}
