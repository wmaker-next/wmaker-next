/* Workspace.c- workspace options
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
#include "config_values.h"

typedef struct _Panel {
	WMBox *box;

	char *sectionName;

	char *description;

	CallbackRec callbacks;

	WMWidget *parent;

	WMFrame *navF;

	WMButton *linkB;
	WMButton *cyclB;
	WMButton *newB;
	WMButton *autoB;
	WMLabel *linkL;
	WMLabel *cyclL;
	WMLabel *newL;
	WMLabel *autoL;

	WMLabel *posiL;
	WMLabel *posL;
	WMPopUpButton *posP;

	WMFrame *layF;
	WMPopUpButton *layP;
	WMTextField *mcT;
	WMSlider *mrS;
} _Panel;

static const struct {
	const char *db_value;
	const char *label;
} layout_choices[] = {
#define R(v, l) { v, l },
	WPREFS_TABLE_workspace_layouts(R)
#undef R
};

/* The popup index is written as the enum value, so the table must keep
 * the schema row order. Pinned by check_wprefs_schema. */
static const wm_enum_choice display_position_choices[] = {
#define R(v, l) { v, l },
	WPREFS_TABLE_display_position(R)
#undef R
};

#define ICON_FILE	"workspace"

#define DONT_LINK_FILE	"dontlinkworkspaces"
#define CYCLE_FILE	"cycleworkspaces"
#define ADVANCE_FILE	"advancetonewworkspace"
#define AUTO_REMOVE_FILE	"autoremoveempty"
#define WSNAME_FILE 	"workspacename"

/* Navigation icons are 48x48 / 50x40 XPMs. The navigation frame has four
 * rows in ~180 px, so each icon is scaled to NAV_ICON_H before being
 * placed on a uniformly-sized WIPImageOnly label, keeping the rows
 * aligned and the icons fully visible. Nearest-neighbor (RScaleImage)
 * is used because the source XPMs are pixel art — Catmull-Rom softens
 * them at this small ratio. */
#define NAV_ICON_H	26

static WMPixmap *loadScaledNavIcon(WMScreen *scr, RContext *rc, const char *file)
{
	WMPixmap *pix;
	RImage *src, *scaled;
	char *path;
	unsigned target_w;

	path = LocateImage(file);
	if (!path)
		return NULL;

	src = RLoadImage(rc, path, 0);
	wfree(path);
	if (!src) {
		wwarning(_("could not load image file %s"), file);
		return NULL;
	}

	target_w = src->width * NAV_ICON_H / src->height;
	if (target_w == 0)
		target_w = 1;

	scaled = RScaleImage(src, target_w, NAV_ICON_H);
	RReleaseImage(src);
	if (!scaled)
		return NULL;

	pix = WMCreatePixmapFromRImage(scr, scaled, 0);
	RReleaseImage(scaled);
	return pix;
}

static void showData(_Panel * panel)
{
	char buf[16];

	WMSetButtonSelected(panel->linkB, !GetBoolForKey("DontLinkWorkspaces"));

	WMSetButtonSelected(panel->cyclB, GetBoolForKey("CycleWorkspaces"));

	WMSetButtonSelected(panel->newB, GetBoolForKey("AdvanceToNewWorkspace"));

	WMSetButtonSelected(panel->autoB, GetBoolForKey("AutoRemoveEmptyWorkspaces"));

	/* popup items are added in WD_NONE..WD_BOTTOMRIGHT order in createPanel(),
	 * so the schema value is the popup index directly */
	WMSetPopUpButtonSelectedItem(panel->posP,
				     GetEnumForKey("WorkspaceNameDisplayPosition", WD_CENTER));

	/* layout_choices[] is added in WVL_FLOAT.. order in createPanel(),
	 * so the schema value is the popup index directly */
	WMSetPopUpButtonSelectedItem(panel->layP,
				     GetEnumForKey("DefaultWorkspaceLayout", WVL_FLOAT));
	snprintf(buf, sizeof(buf), "%d", GetIntegerForKey("LayoutMasterCount"));
	WMSetTextFieldText(panel->mcT, buf);
	WMSetSliderValue(panel->mrS, GetIntegerForKey("LayoutMasterRatio"));
}

static void createPanel(Panel * p)
{
	_Panel *panel = (_Panel *) p;
	WMScreen *scr = WMWidgetScreen(panel->parent);
	WMPixmap *icon1;
	WMLabel *label;
	RContext *rc = WMScreenRContext(scr);
	int i;

	panel->box = WMCreateBox(panel->parent);
	WMSetViewExpandsToParent(WMWidgetView(panel->box), 2, 2, 2, 2);

    /***************** Workspace Navigation *****************/
	panel->navF = WMCreateFrame(panel->box);
	WMResizeWidget(panel->navF, 490, 184);
	WMMoveWidget(panel->navF, 15, 2);
	WMSetFrameTitle(panel->navF, _("Workspace Navigation"));

	panel->cyclB = WMCreateSwitchButton(panel->navF);
	WMResizeWidget(panel->cyclB, 410, 30);
	WMMoveWidget(panel->cyclB, 75, 18);
	WMSetButtonText(panel->cyclB, _("Wrap to the first workspace from the last workspace"));

	panel->cyclL = WMCreateLabel(panel->navF);
	WMResizeWidget(panel->cyclL, 60, 30);
	WMMoveWidget(panel->cyclL, 10, 16);
	WMSetLabelImagePosition(panel->cyclL, WIPImageOnly);
	icon1 = loadScaledNavIcon(scr, rc, CYCLE_FILE);
	if (icon1)
	{
		WMSetLabelImage(panel->cyclL, icon1);
		WMReleasePixmap(icon1);
	}

	/**/ panel->linkB = WMCreateSwitchButton(panel->navF);
	WMResizeWidget(panel->linkB, 410, 30);
	WMMoveWidget(panel->linkB, 75, 51);
	WMSetButtonText(panel->linkB, _("Switch workspaces while dragging windows"));

	panel->linkL = WMCreateLabel(panel->navF);
	WMResizeWidget(panel->linkL, 60, 30);
	WMMoveWidget(panel->linkL, 10, 49);
	WMSetLabelImagePosition(panel->linkL, WIPImageOnly);
	icon1 = loadScaledNavIcon(scr, rc, DONT_LINK_FILE);
	if (icon1)
	{
		WMSetLabelImage(panel->linkL, icon1);
		WMReleasePixmap(icon1);
	}

	/**/ panel->newB = WMCreateSwitchButton(panel->navF);
	WMResizeWidget(panel->newB, 410, 30);
	WMMoveWidget(panel->newB, 75, 84);
	WMSetButtonText(panel->newB, _("Automatically create new workspaces"));

	panel->newL = WMCreateLabel(panel->navF);
	WMResizeWidget(panel->newL, 60, 30);
	WMMoveWidget(panel->newL, 10, 82);
	WMSetLabelImagePosition(panel->newL, WIPImageOnly);
	icon1 = loadScaledNavIcon(scr, rc, ADVANCE_FILE);
	if (icon1)
	{
		WMSetLabelImage(panel->newL, icon1);
		WMReleasePixmap(icon1);
	}

	/**/ panel->autoB = WMCreateSwitchButton(panel->navF);
	WMResizeWidget(panel->autoB, 410, 30);
	WMMoveWidget(panel->autoB, 75, 117);
	WMSetButtonText(panel->autoB, _("Automatically remove empty workspaces"));

	panel->autoL = WMCreateLabel(panel->navF);
	WMResizeWidget(panel->autoL, 60, 30);
	WMMoveWidget(panel->autoL, 10, 115);
	WMSetLabelImagePosition(panel->autoL, WIPImageOnly);
	icon1 = loadScaledNavIcon(scr, rc, AUTO_REMOVE_FILE);
	if (icon1) {
		WMSetLabelImage(panel->autoL, icon1);
		WMReleasePixmap(icon1);
	}

	/**/ panel->posL = WMCreateLabel(panel->navF);
	WMResizeWidget(panel->posL, 275, 30);
	WMMoveWidget(panel->posL, 75, 150);
	// WMSetLabelTextAlignment(panel->posL, WARight);
	WMSetLabelText(panel->posL, _("Position of workspace name display"));

	panel->posiL = WMCreateLabel(panel->navF);
	WMResizeWidget(panel->posiL, 60, 30);
	WMMoveWidget(panel->posiL, 10, 148);
	WMSetLabelImagePosition(panel->posiL, WIPImageOnly);
	icon1 = loadScaledNavIcon(scr, rc, WSNAME_FILE);
	if (icon1)
	{
		WMSetLabelImage(panel->posiL, icon1);
		WMReleasePixmap(icon1);
	}

	panel->posP = WMCreatePopUpButton(panel->navF);
	WMResizeWidget(panel->posP, 125, 20);
	WMMoveWidget(panel->posP, 350, 153);
	for (i = 0; i < (int)wlengthof(display_position_choices); i++)
		WMAddPopUpButtonItem(panel->posP, _(display_position_choices[i].label));

	WMMapSubwidgets(panel->navF);

    /***************** Workspace Layout *****************/
	panel->layF = WMCreateFrame(panel->box);
	WMResizeWidget(panel->layF, 490, 42);
	WMMoveWidget(panel->layF, 15, 187);
	WMSetFrameTitle(panel->layF, _("Default Workspace Layout"));
	WMSetBalloonTextForView(
		_("Workspace layout applied to newly created workspaces.\n"
		  "Existing workspaces keep whatever layout they were\n"
		  "last set to. 'Float' is the ordinary unmanaged\n"
		  "placement."),
		WMWidgetView(panel->layF));

	panel->layP = WMCreatePopUpButton(panel->layF);
	WMResizeWidget(panel->layP, 100, 20);
	WMMoveWidget(panel->layP, 10, 16);
	for (i = 0; i < (int)wlengthof(layout_choices); i++)
		WMAddPopUpButtonItem(panel->layP, _(layout_choices[i].label));

	label = WMCreateLabel(panel->layF);
	WMResizeWidget(label, 90, 18);
	WMMoveWidget(label, 120, 17);
	WMSetLabelTextAlignment(label, WARight);
	WMSetLabelText(label, _("Master count:"));

	panel->mcT = WMCreateTextField(panel->layF);
	WMResizeWidget(panel->mcT, 30, 20);
	WMMoveWidget(panel->mcT, 214, 16);

	label = WMCreateLabel(panel->layF);
	WMResizeWidget(label, 80, 18);
	WMMoveWidget(label, 254, 17);
	WMSetLabelTextAlignment(label, WARight);
	WMSetLabelText(label, _("Master ratio:"));

	panel->mrS = WMCreateSlider(panel->layF);
	WMResizeWidget(panel->mrS, 140, 18);
	WMMoveWidget(panel->mrS, 338, 17);
	WMSetSliderMinValue(panel->mrS, 10);
	WMSetSliderMaxValue(panel->mrS, 90);

	WMMapSubwidgets(panel->layF);

	WMRealizeWidget(panel->box);
	WMMapSubwidgets(panel->box);

	showData(panel);
}

static void storeData(_Panel * panel)
{
	int tmp = WMGetPopUpButtonSelectedItem(panel->posP);
	char *s;

	if (tmp < 0)
		return;
	SetBoolForKey(!WMGetButtonSelected(panel->linkB), "DontLinkWorkspaces");
	SetBoolForKey(WMGetButtonSelected(panel->cyclB), "CycleWorkspaces");
	SetBoolForKey(WMGetButtonSelected(panel->newB), "AdvanceToNewWorkspace");
	SetBoolForKey(WMGetButtonSelected(panel->autoB), "AutoRemoveEmptyWorkspaces");

	SetEnumForKey(tmp, "WorkspaceNameDisplayPosition");

	tmp = WMGetPopUpButtonSelectedItem(panel->layP);
	if (tmp >= 0)
		SetEnumForKey(tmp, "DefaultWorkspaceLayout");
	s = WMGetTextFieldText(panel->mcT);
	SetIntegerForKey(wConfigSchemaParseInt("LayoutMasterCount", s), "LayoutMasterCount");
	wfree(s);
	SetIntegerForKey(WMGetSliderValue(panel->mrS), "LayoutMasterRatio");
}

Panel *InitWorkspace(WMWidget *parent)
{
	_Panel *panel;

	panel = wmalloc(sizeof(_Panel));

	panel->sectionName = _("Workspace Preferences");

	panel->description = _("Workspace navigation features\n"
			       "and workspace name display settings.");

	panel->parent = parent;

	panel->callbacks.createWidgets = createPanel;
	panel->callbacks.updateDomain = storeData;
	panel->callbacks.undoChanges = showData;

	AddSection(panel, ICON_FILE);

	return panel;
}
