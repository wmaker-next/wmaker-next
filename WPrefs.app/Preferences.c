/* Preferences.c- misc ergonomic preferences
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
#include "enumchoices.h"


/* Possible choices to display window information during a resize */
static const wm_enum_choice resize_display[] = {
#define R(v, l) { v, l },
	WPREFS_TABLE_resize_display(R)
#undef R
};

/* Possible choices to display window information while a window is being moved */
static const wm_enum_choice move_display[] = {
#define R(v, l) { v, l },
	WPREFS_TABLE_move_display(R)
#undef R
};

/* All the places where a balloon can be used to display more stuff to user */
static const struct {
	const char *db_key;
	const char *label;
} balloon_choices[] = {
	{ "WindowTitleBalloons",       N_("incomplete window titles"), },
	{ "MiniwindowTitleBalloons",   N_("miniwindow titles"), },
	{ "AppIconBalloons",           N_("application/dock icons"), },
	{ "HelpBalloons",              N_("internal help"), }
};

static const struct {
	const char *db_key;
	int default_value;
	const char *label;
	const char *balloon_msg;
} appicon_bouncing[] = {
	{ "DoNotMakeAppIconsBounce",   false, N_("Disable AppIcon bounce"),
	  N_("By default, the AppIcon bounces when the application is launched") },

	{ "BounceAppIconsWhenUrgent",  true,  N_("Bounce when the application wants attention"),
	  NULL },

	{ "RaiseAppIconsWhenBouncing", false, N_("Raise AppIcon when bouncing"),
	  N_("Otherwise you will not see it bouncing if\nthere is a window in front of the AppIcon") }
};

typedef struct _Panel {
	WMBox *box;

	char *sectionName;

	char *description;

	CallbackRec callbacks;

	WMWidget *parent;

	WMFrame *sizeF;
	WMPopUpButton *sizeP;

	WMFrame *posiF;
	WMPopUpButton *posiP;

	WMFrame *ballF;
	WMButton *ballB[wlengthof_nocheck(balloon_choices)];

	WMFrame *optF;
	WMButton *bounceB[wlengthof_nocheck(appicon_bouncing)];

	WMFrame *borderF;
	WMSlider *borderS;
	WMLabel *borderL;
	WMButton *lrB;
	WMButton *tbB;

} _Panel;

#define ICON_FILE	"ergonomic"

static void borderCallback(WMWidget * w, void *data)
{
	_Panel *panel = (_Panel *) data;
	char buffer[64];
	int i;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) w;

	i = WMGetSliderValue(panel->borderS);

	if (i == 0)
		snprintf(buffer, sizeof(buffer), _("OFF"));
	else if (i == 1)
		snprintf(buffer, sizeof(buffer), _("1 pixel"));
	else if (i <= 4)
		/* 2-4 */
		snprintf(buffer, sizeof(buffer), _("%i pixels"), i);
	else
		/* >4 */
		snprintf(buffer, sizeof(buffer), _("%i pixels "), i);	/* note space! */
	WMSetLabelText(panel->borderL, buffer);
}

static void bounceMasterCallback(WMWidget *w, void *data)
{
	_Panel *panel = (_Panel *) data;
	bool disabled;
	int i;

	(void) w;

	disabled = WMGetButtonSelected(panel->bounceB[0]);
	for (i = 1; i < wlengthof(appicon_bouncing); i++)
		WMSetButtonEnabled(panel->bounceB[i], !disabled);
}

static void showData(_Panel * panel)
{
	char *str;
	int x;

	str = GetStringForKey("ResizeDisplay");
	wm_enum_popup_select(panel->sizeP, resize_display,
			     wlengthof(resize_display), str, "ResizeDisplay");

	str = GetStringForKey("MoveDisplay");
	wm_enum_popup_select(panel->posiP, move_display,
			     wlengthof(move_display), str, "MoveDisplay");

	x = GetIntegerForKey("WorkspaceBorderSize");
	/* clamp to the slider's range (UI geometry, not schema validation) */
	x = x < 0 ? 0 : x;
	x = x > 5 ? 5 : x;
	WMSetSliderValue(panel->borderS, x);
	borderCallback(NULL, panel);

	x = GetEnumForKey("WorkspaceBorder", WB_NONE);
	WMSetButtonSelected(panel->lrB, (x & WB_LEFTRIGHT) != 0);
	WMSetButtonSelected(panel->tbB, (x & WB_TOPBOTTOM) != 0);

	for (x = 0; x < wlengthof(appicon_bouncing); x++) {
		if (GetStringForKey(appicon_bouncing[x].db_key))
			WMSetButtonSelected(panel->bounceB[x], GetBoolForKey(appicon_bouncing[x].db_key));
	}
	bounceMasterCallback(NULL, panel);

	for (x = 0; x < wlengthof(balloon_choices); x++)
		WMSetButtonSelected(panel->ballB[x], GetBoolForKey(balloon_choices[x].db_key));
}

static void storeData(_Panel * panel)
{
	bool lr, tb;
	int i;

	wm_enum_popup_store(panel->sizeP, resize_display,
			    wlengthof(resize_display), "ResizeDisplay");

	wm_enum_popup_store(panel->posiP, move_display,
			    wlengthof(move_display), "MoveDisplay");

	lr = WMGetButtonSelected(panel->lrB);
	tb = WMGetButtonSelected(panel->tbB);
	SetEnumForKey((lr ? WB_LEFTRIGHT : 0) | (tb ? WB_TOPBOTTOM : 0), "WorkspaceBorder");
	SetIntegerForKey(WMGetSliderValue(panel->borderS), "WorkspaceBorderSize");

	for (i = 0; i < wlengthof(appicon_bouncing); i++)
		SetBoolForKey(WMGetButtonSelected(panel->bounceB[i]), appicon_bouncing[i].db_key);

	for (i = 0; i < wlengthof(balloon_choices); i++)
		SetBoolForKey(WMGetButtonSelected(panel->ballB[i]), balloon_choices[i].db_key);
}

static void createPanel(Panel * p)
{
	_Panel *panel = (_Panel *) p;
	int i;

	panel->box = WMCreateBox(panel->parent);
	WMSetViewExpandsToParent(WMWidgetView(panel->box), 2, 2, 2, 2);

    /***************** Size Display ****************/
	panel->sizeF = WMCreateFrame(panel->box);
	WMResizeWidget(panel->sizeF, 255, 52);
	WMMoveWidget(panel->sizeF, 15, 7);
	WMSetFrameTitle(panel->sizeF, _("Size Display"));

	WMSetBalloonTextForView(_("The position or style of the window size\n"
				  "display that's shown when a window is resized."), WMWidgetView(panel->sizeF));

	panel->sizeP = wm_enum_popup_new(panel->sizeF, resize_display,
					  wlengthof(resize_display));
	WMResizeWidget(panel->sizeP, 227, 20);
	WMMoveWidget(panel->sizeP, 14, 20);

	WMMapSubwidgets(panel->sizeF);

    /***************** Position Display ****************/
	panel->posiF = WMCreateFrame(panel->box);
	WMResizeWidget(panel->posiF, 255, 52);
	WMMoveWidget(panel->posiF, 15, 66);
	WMSetFrameTitle(panel->posiF, _("Position Display"));

	WMSetBalloonTextForView(_("The position or style of the window position\n"
				  "display that's shown when a window is moved."), WMWidgetView(panel->posiF));

	panel->posiP = wm_enum_popup_new(panel->posiF, move_display,
					  wlengthof(move_display));
	WMResizeWidget(panel->posiP, 227, 20);
	WMMoveWidget(panel->posiP, 14, 20);

	WMMapSubwidgets(panel->posiF);

    /***************** Balloon Text ****************/
	panel->ballF = WMCreateFrame(panel->box);
	WMResizeWidget(panel->ballF, 220, 130);
	WMMoveWidget(panel->ballF, 285, 7);
	WMSetFrameTitle(panel->ballF, _("Show balloon for..."));

	for (i = 0; i < wlengthof(balloon_choices); i++) {
		panel->ballB[i] = WMCreateSwitchButton(panel->ballF);
		WMResizeWidget(panel->ballB[i], 198, 20);
		WMMoveWidget(panel->ballB[i], 11, 20 + i * 26);
		WMSetButtonText(panel->ballB[i], _(balloon_choices[i].label));
	}

	WMMapSubwidgets(panel->ballF);

    /***************** Options ****************/
	panel->optF = WMCreateFrame(panel->box);
	WMResizeWidget(panel->optF, 255, 96);
	WMMoveWidget(panel->optF, 15, 125);
	WMSetFrameTitle(panel->optF, _("AppIcon bouncing"));

	for (i = 0; i < wlengthof(appicon_bouncing); i++) {
		panel->bounceB[i] = WMCreateSwitchButton(panel->optF);
		WMResizeWidget(panel->bounceB[i], 237, 26);
		WMMoveWidget(panel->bounceB[i], 9, 16 + i * 26);
		WMSetButtonText(panel->bounceB[i], _(appicon_bouncing[i].label));

		if (appicon_bouncing[i].default_value)
			WMSetButtonSelected(panel->bounceB[i], true);

		if (appicon_bouncing[i].balloon_msg)
			WMSetBalloonTextForView(_(appicon_bouncing[i].balloon_msg),
			                        WMWidgetView(panel->bounceB[i]));
	}
	WMSetButtonAction(panel->bounceB[0], bounceMasterCallback, panel);

	WMMapSubwidgets(panel->optF);

    /***************** Workspace border ****************/
	panel->borderF = WMCreateFrame(panel->box);
	WMResizeWidget(panel->borderF, 220, 77);
	WMMoveWidget(panel->borderF, 285, 144);
	WMSetFrameTitle(panel->borderF, _("Workspace border"));

	panel->borderS = WMCreateSlider(panel->borderF);
	WMResizeWidget(panel->borderS, 80, 15);
	WMMoveWidget(panel->borderS, 11, 22);
	WMSetSliderMinValue(panel->borderS, 0);
	WMSetSliderMaxValue(panel->borderS, 5);
	WMSetSliderAction(panel->borderS, borderCallback, panel);

	panel->borderL = WMCreateLabel(panel->borderF);
	WMResizeWidget(panel->borderL, 100, 15);
	WMMoveWidget(panel->borderL, 105, 22);

	panel->lrB = WMCreateSwitchButton(panel->borderF);
	WMMoveWidget(panel->lrB, 11, 40);
	WMResizeWidget(panel->lrB, 95, 30);
	WMSetButtonText(panel->lrB, _("Left/Right"));

	panel->tbB = WMCreateSwitchButton(panel->borderF);
	WMMoveWidget(panel->tbB, 110, 40);
	WMResizeWidget(panel->tbB, 105, 30);
	WMSetButtonText(panel->tbB, _("Top/Bottom"));

	WMMapSubwidgets(panel->borderF);

	WMRealizeWidget(panel->box);
	WMMapSubwidgets(panel->box);

	showData(panel);
}

Panel *InitPreferences(WMWidget *parent)
{
	_Panel *panel;

	panel = wmalloc(sizeof(_Panel));

	panel->sectionName = _("Miscellaneous Ergonomic Preferences");
	panel->description = _("Various settings like balloon text, geometry\n" "displays etc.");

	panel->parent = parent;

	panel->callbacks.createWidgets = createPanel;
	panel->callbacks.updateDomain = storeData;
	panel->callbacks.undoChanges = showData;

	AddSection(panel, ICON_FILE);

	return panel;
}
