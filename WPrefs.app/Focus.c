/* Focus.c- input and colormap focus stuff
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

	WMFrame *kfocF;
	WMButton *kfocB[2];

	WMFrame *cfocF;
	WMButton *autB;
	WMButton *manB;

	WMFrame *raisF;
	WMButton *raisB[5];
	WMTextField *raisT;
	WMLabel *raisL;

	WMFrame *optF;
	WMButton *ignB;
	WMButton *newB;
	WMButton *warpFocB;
	WMButton *craisB;

	char raiseDelaySelected;
} _Panel;

#define ICON_FILE	"windowfocus"

#define DELAY_ICON "timer%i"
#define DELAY_ICON_S "timer%is"

static void showData(_Panel * panel)
{
	int i;
	char buffer[32];

	if (GetEnumForKey("FocusMode", WKF_CLICK) == WKF_SLOPPY)
		WMSetButtonSelected(panel->kfocB[1], 1);
	else
		WMSetButtonSelected(panel->kfocB[0], 1);

	if (GetEnumForKey("ColormapMode", WCM_POINTER) == WCM_CLICK)
		WMPerformButtonClick(panel->manB);
	else
		WMPerformButtonClick(panel->autB);

	i = GetIntegerForKey("RaiseDelay");
	snprintf(buffer, sizeof(buffer), "%i", i);
	WMSetTextFieldText(panel->raisT, buffer);

	switch (i) {
	case 0:
		WMPerformButtonClick(panel->raisB[0]);
		break;
	case 10:
		WMPerformButtonClick(panel->raisB[1]);
		break;
	case 100:
		WMPerformButtonClick(panel->raisB[2]);
		break;
	case 350:
		WMPerformButtonClick(panel->raisB[3]);
		break;
	case 800:
		WMPerformButtonClick(panel->raisB[4]);
		break;
	}

	 /**/ WMSetButtonSelected(panel->ignB, GetBoolForKey("IgnoreFocusClick"));

	WMSetButtonSelected(panel->newB, GetBoolForKey("AutoFocus"));
	WMSetButtonSelected(panel->warpFocB, GetBoolForKey("PointerFollowsFocus"));
	WMSetButtonSelected(panel->craisB, GetBoolForKey("CirculateRaise"));
}

static bool storeData(_Panel *panel)
{
	char *str;
	int i;

	SetEnumForKey(WMGetButtonSelected(panel->kfocB[1]) ? WKF_SLOPPY : WKF_CLICK, "FocusMode");
	SetEnumForKey(WMGetButtonSelected(panel->manB) ? WCM_CLICK : WCM_POINTER, "ColormapMode");

	str = WMGetTextFieldText(panel->raisT);
	if (sscanf(str, "%i", &i) != 1)
		i = 0;
	SetIntegerForKey(i, "RaiseDelay");
	wfree(str);

	SetBoolForKey(WMGetButtonSelected(panel->ignB), "IgnoreFocusClick");
	SetBoolForKey(WMGetButtonSelected(panel->newB), "AutoFocus");
	SetBoolForKey(WMGetButtonSelected(panel->warpFocB), "PointerFollowsFocus");
	SetBoolForKey(WMGetButtonSelected(panel->craisB), "CirculateRaise");

	return true;
}

static void pushDelayButton(WMWidget * w, void *data)
{
	_Panel *panel = (_Panel *) data;

	panel->raiseDelaySelected = 1;
	if (w == panel->raisB[0]) {
		WMSetTextFieldText(panel->raisT, _("OFF"));
	} else if (w == panel->raisB[1]) {
		WMSetTextFieldText(panel->raisT, "10");
	} else if (w == panel->raisB[2]) {
		WMSetTextFieldText(panel->raisT, "100");
	} else if (w == panel->raisB[3]) {
		WMSetTextFieldText(panel->raisT, "350");
	} else if (w == panel->raisB[4]) {
		WMSetTextFieldText(panel->raisT, "800");
	}
}

static void raiseTextChanged(void *observerData, WMNotification * notification)
{
	_Panel *panel = (_Panel *) observerData;
	int i;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) notification;

	if (panel->raiseDelaySelected) {
		for (i = 0; i < 5; i++) {
			WMSetButtonSelected(panel->raisB[i], false);
		}
		panel->raiseDelaySelected = 0;
	}
}

static void createPanel(Panel * p)
{
	_Panel *panel = (_Panel *) p;
	WMScreen *scr = WMWidgetScreen(panel->parent);
	int i;
	char buf[64];
	WMColor *color;
	WMFont *font;
	WMBox *box;

	panel->box = WMCreateBox(panel->parent);
	WMSetViewExpandsToParent(WMWidgetView(panel->box), 2, 2, 2, 2);

    /***************** Input Focus Mode *****************/
	panel->kfocF = WMCreateFrame(panel->box);
	WMResizeWidget(panel->kfocF, 240, 125);
	WMMoveWidget(panel->kfocF, 15, 10);
	WMSetFrameTitle(panel->kfocF, _("Input Focus Mode"));

	box = WMCreateBox(panel->kfocF);
	WMSetViewExpandsToParent(WMWidgetView(box), 10, 15, 10, 10);
	WMSetBoxHorizontal(box, false);

	panel->kfocB[0] = WMCreateRadioButton(box);
	WMSetButtonText(panel->kfocB[0], _("Manual:  Click on the window to set "
	                                   "keyboard input focus"));
	WMAddBoxSubview(box, WMWidgetView(panel->kfocB[0]), true, true, 20, 0, 0);

	panel->kfocB[1] = WMCreateRadioButton(box);
	WMGroupButtons(panel->kfocB[0], panel->kfocB[1]);
	WMSetButtonText(panel->kfocB[1], _("Auto:  Set keyboard input focus to "
	                                   "the window under the mouse pointer"));
	WMAddBoxSubview(box, WMWidgetView(panel->kfocB[1]), true, true, 20, 0, 0);

	WMMapSubwidgets(box);
	WMMapWidget(box);

    /***************** Colormap Installation Mode ****************/

	panel->cfocF = WMCreateFrame(panel->box);
	WMResizeWidget(panel->cfocF, 240, 77);
	WMMoveWidget(panel->cfocF, 15, 143);
	WMSetFrameTitle(panel->cfocF, _("Install colormap from the window..."));

	WMSetBalloonTextForView(_("This option is for screens that can display only a limited number\n"
	                          "of colors at a time, so they use an indexed table of colors (called\n"
	                          "a ColorMap) that each application customizes for its needs, and\n"
	                          "WindowMaker will set the global ColorMap dynamically from the\n"
	                          "active application.\n"
	                          "You can know the capability of your screen in WindowMaker's info\n"
	                          "panel as the 'visual'."),
	                        WMWidgetView(panel->cfocF));

	panel->manB = WMCreateRadioButton(panel->cfocF);
	WMResizeWidget(panel->manB, 226, 24);
	WMMoveWidget(panel->manB, 9, 18);
	WMSetButtonText(panel->manB, _("...that has the input focus"));

	panel->autB = WMCreateRadioButton(panel->cfocF);
	WMResizeWidget(panel->autB, 226, 24);
	WMMoveWidget(panel->autB, 9, 43);
	WMSetButtonText(panel->autB, _("...that's under the mouse pointer"));
	WMGroupButtons(panel->manB, panel->autB);

	WMMapSubwidgets(panel->cfocF);

    /***************** Automatic window raise delay *****************/
	panel->raisF = WMCreateFrame(panel->box);
	WMResizeWidget(panel->raisF, 245, 68);
	WMMoveWidget(panel->raisF, 265, 10);
	WMSetFrameTitle(panel->raisF, _("Automatic Window Raise Delay"));

	for (i = 0; i < 5; i++) {
		panel->raisB[i] = WMCreateCustomButton(panel->raisF, WBBStateChangeMask);
		WMResizeWidget(panel->raisB[i], 25, 25);
		WMMoveWidget(panel->raisB[i], 12 + (30 * i), 25);
		WMSetButtonBordered(panel->raisB[i], false);
		WMSetButtonImagePosition(panel->raisB[i], WIPImageOnly);
		WMSetButtonAction(panel->raisB[i], pushDelayButton, panel);
		if (i > 0)
			WMGroupButtons(panel->raisB[0], panel->raisB[i]);
		snprintf(buf, sizeof(buf), DELAY_ICON, i);
		SetButtonImageFromFile(scr, panel->raisB[i], buf, false);
		snprintf(buf, sizeof(buf), DELAY_ICON_S, i);
		SetButtonImageFromFile(scr, panel->raisB[i], buf, true);
	}

	panel->raisT = WMCreateTextField(panel->raisF);
	WMResizeWidget(panel->raisT, 36, 20);
	WMMoveWidget(panel->raisT, 165, 28);
	WMAddNotificationObserver(raiseTextChanged, panel, WMTextDidChangeNotification, panel->raisT);

	color = WMDarkGrayColor(scr);
	font = WMSystemFontOfSize(scr, 10);

	panel->raisL = WMCreateLabel(panel->raisF);
	WMResizeWidget(panel->raisL, 36, 16);
	WMMoveWidget(panel->raisL, 205, 32);
	WMSetLabelText(panel->raisL, _("ms"));
	WMSetLabelTextColor(panel->raisL, color);
	WMSetLabelFont(panel->raisL, font);

	WMReleaseColor(color);
	WMReleaseFont(font);

	WMMapSubwidgets(panel->raisF);

    /***************** Options ****************/
	panel->optF = WMCreateFrame(panel->box);
	WMResizeWidget(panel->optF, 245, 132);
	WMMoveWidget(panel->optF, 265, 88);

	panel->ignB = WMCreateSwitchButton(panel->optF);
	WMResizeWidget(panel->ignB, 228, 38);
	WMMoveWidget(panel->ignB, 10, 4);
	WMSetButtonText(panel->ignB, _("Do not let applications receive the click used to focus windows"));

	panel->newB = WMCreateSwitchButton(panel->optF);
	WMResizeWidget(panel->newB, 228, 22);
	WMMoveWidget(panel->newB, 10, 42);
	WMSetButtonText(panel->newB, _("Automatically focus new windows"));

	panel->warpFocB = WMCreateSwitchButton(panel->optF);
	WMResizeWidget(panel->warpFocB, 228, 22);
	WMMoveWidget(panel->warpFocB, 10, 64);
	WMSetButtonText(panel->warpFocB, _("Move pointer to focused window"));
	WMSetBalloonTextForView(_("Moves the pointer to the center of a window when focus is set by the\n"
		"keyboard, the window switcher, activation requests, or when a new\n"
		"window is created. Does not move the pointer when you focus a\n"
		"window by moving the mouse into it. Restoring windows from\n"
		"miniwindows or dock icons also moves the pointer."),
		WMWidgetView(panel->warpFocB));

	panel->craisB = WMCreateSwitchButton(panel->optF);
	WMResizeWidget(panel->craisB, 228, 38);
	WMMoveWidget(panel->craisB, 10, 86);
	WMSetButtonText(panel->craisB, _("Raise window when switching focus with keyboard"));

	WMMapSubwidgets(panel->optF);

	WMRealizeWidget(panel->box);
	WMMapSubwidgets(panel->box);

	showData(panel);
}

Panel *InitFocus(WMWidget *parent)
{
	_Panel *panel;

	panel = wmalloc(sizeof(_Panel));

	panel->sectionName = _("Window Focus Preferences");
	panel->description = _("Keyboard focus switching policy and related options.");
	panel->parent = parent;

	panel->callbacks.createWidgets = createPanel;
	panel->callbacks.updateDomain = storeData;
	panel->callbacks.undoChanges = showData;

	AddSection(panel, ICON_FILE);

	return panel;
}
