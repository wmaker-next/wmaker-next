/* Configurations.c- misc. configurations
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

	WMFrame *icoF;
	WMButton *icoB[5];

	WMFrame *shaF;
	WMButton *shaB[5];

	WMFrame *titlF;
	WMButton *oldsB;
	WMButton *newsB;
	WMButton *nextB;
	WMButton *flatB;

	WMFrame *animF;
	WMButton *animB;
	WMButton *supB;
	WMButton *sfxB;
	WMLabel *noteL;

	WMFrame *smoF;
	WMButton *smoB;
	WMButton *aaB;
} _Panel;

#define ICON_FILE	"configs"
#define OLDS_IMAGE	"oldstyle"
#define NEWS_IMAGE	"newstyle"
#define NEXT_IMAGE	"nextstyle"
#define FLAT_IMAGE	"flatstyle"
#define ANIM_IMAGE	"animations"
#define SUPERF_IMAGE	"moreanim"
#define SOUND_IMAGE	"sound"
#define SMOOTH_IMAGE	"smooth"
#define SPEED_IMAGE 	"speed%i"
#define SPEED_IMAGE_S 	"speed%is"
#define ARQUIVO_XIS	"xis"

static void showData(_Panel *panel)
{
	char *str;

	WMPerformButtonClick(panel->icoB[GetSpeedForKey("IconSlideSpeed")]);
	WMPerformButtonClick(panel->shaB[GetSpeedForKey("ShadeSpeed")]);

	str = GetStringForKey("NewStyle");
	if (str && strcasecmp(str, "next") == 0) {
		WMPerformButtonClick(panel->nextB);
	} else if (str && strcasecmp(str, "old") == 0) {
		WMPerformButtonClick(panel->oldsB);
	} else if (str && strcasecmp(str, "flat") == 0) {
		WMPerformButtonClick(panel->flatB);
	} else {
		WMPerformButtonClick(panel->newsB);
	}

	WMSetButtonSelected(panel->animB, !GetBoolForKey("DisableAnimations"));
	WMSetButtonSelected(panel->supB, GetBoolForKey("Superfluous"));
	WMSetButtonSelected(panel->sfxB, !GetBoolForKey("DisableSound"));
	WMSetButtonSelected(panel->smoB, GetBoolForKey("SmoothWorkspaceBack"));
	WMSetButtonSelected(panel->aaB, GetWMGlobalBoolForKey("AntialiasedText", true));
}

static void createPanel(Panel *p)
{
	_Panel *panel = (_Panel *) p;
	WMScreen *scr = WMWidgetScreen(panel->parent);
	char *buf1, *buf2;
	WMPixmap *icon, *altIcon;
	RImage *xis = NULL;
	int i;
	RContext *rc = WMScreenRContext(scr);
	WMFont *font = WMSystemFontOfSize(scr, 10);
	char *path;

	path = LocateImage(ARQUIVO_XIS);
	if (path) {
		xis = RLoadImage(rc, path, 0);
		if (!xis)
			wwarning(_("could not load image file %s"), path);
		wfree(path);
	}

	panel->box = WMCreateBox(panel->parent);
	WMSetViewExpandsToParent(WMWidgetView(panel->box), 2, 2, 2, 2);

    /*********** Icon Slide Speed **********/
	panel->icoF = WMCreateFrame(panel->box);
	WMResizeWidget(panel->icoF, 212, 45);
	WMMoveWidget(panel->icoF, 15, 10);
	WMSetFrameTitle(panel->icoF, _("Icon Slide Speed"));

    /*********** Shade Animation Speed **********/
	panel->shaF = WMCreateFrame(panel->box);
	WMResizeWidget(panel->shaF, 212, 45);
	WMMoveWidget(panel->shaF, 15, 65);
	WMSetFrameTitle(panel->shaF, _("Shade Animation Speed"));

	buf1 = wmalloc(strlen(SPEED_IMAGE) + 1);
	buf2 = wmalloc(strlen(SPEED_IMAGE_S) + 1);

	for (i = 0; i < 5; i++) {
		panel->icoB[i] = WMCreateCustomButton(panel->icoF, WBBStateChangeMask);
		panel->shaB[i] = WMCreateCustomButton(panel->shaF, WBBStateChangeMask);
		WMResizeWidget(panel->icoB[i], 40, 26);
		WMMoveWidget(panel->icoB[i], 2 + (40 * i), 14);
		WMResizeWidget(panel->shaB[i], 40, 26);
		WMMoveWidget(panel->shaB[i], 2 + (40 * i), 14);
		WMSetButtonBordered(panel->icoB[i], false);
		WMSetButtonImagePosition(panel->icoB[i], WIPImageOnly);
		if (i > 0) {
			WMGroupButtons(panel->icoB[0], panel->icoB[i]);
		}
		WMSetButtonBordered(panel->shaB[i], false);
		WMSetButtonImagePosition(panel->shaB[i], WIPImageOnly);
		if (i > 0) {
			WMGroupButtons(panel->shaB[0], panel->shaB[i]);
		}
		snprintf(buf1, strlen(SPEED_IMAGE) + 1, SPEED_IMAGE, i);
		snprintf(buf2, strlen(SPEED_IMAGE_S) + 1, SPEED_IMAGE_S, i);
		path = LocateImage(buf1);
		if (path) {
			icon = WMCreatePixmapFromFile(scr, path);
			if (icon) {
				WMSetButtonImage(panel->icoB[i], icon);
				WMSetButtonImage(panel->shaB[i], icon);
				WMReleasePixmap(icon);
			} else {
				wwarning(_("could not load icon file %s"), path);
			}
			wfree(path);
		}
		path = LocateImage(buf2);
		if (path) {
			icon = WMCreatePixmapFromFile(scr, path);
			if (icon) {
				WMSetButtonAltImage(panel->icoB[i], icon);
				WMSetButtonAltImage(panel->shaB[i], icon);
				WMReleasePixmap(icon);
			} else {
				wwarning(_("could not load icon file %s"), path);
			}
			wfree(path);
		}
	}
	wfree(buf1);
	wfree(buf2);

	WMMapSubwidgets(panel->icoF);
	WMMapSubwidgets(panel->shaF);

    /***************** Smoothed Scaling *****************/
	panel->smoF = WMCreateFrame(panel->box);
	WMResizeWidget(panel->smoF, 268, 100);
	WMMoveWidget(panel->smoF, 237, 120);
	WMSetFrameTitle(panel->smoF, _("Smoothing"));

	panel->smoB = WMCreateButton(panel->smoF, WBTToggle);
	WMResizeWidget(panel->smoB, 76, 70);
	WMMoveWidget(panel->smoB, 53, 20);
	WMSetButtonFont(panel->smoB, font);
	WMSetButtonText(panel->smoB, _("Images"));
	WMSetButtonImagePosition(panel->smoB, WIPAbove);
	path = LocateImage(SMOOTH_IMAGE);
	if (path) {
		RImage *image, *scaled;

		image = RLoadImage(WMScreenRContext(scr), path, 0);
		wfree(path);

		if (!image)
			goto skip_smooth_preview;

		scaled = RScaleImage(image, 48, 48);
		icon = WMCreatePixmapFromRImage(scr, scaled, 128);
		RReleaseImage(scaled);
		if (icon) {
			WMSetButtonImage(panel->smoB, icon);
			WMReleasePixmap(icon);
		}

		scaled = RSmoothScaleImage(image, 48, 48);
		icon = WMCreatePixmapFromRImage(scr, scaled, 128);
		RReleaseImage(scaled);
		if (icon) {
			WMSetButtonAltImage(panel->smoB, icon);
			WMReleasePixmap(icon);
		}

		RReleaseImage(image);
	}
 skip_smooth_preview:
	WMSetBalloonTextForView(_("Smooth scaled background images, neutralizing\n"
				  "the `pixelization' effect. This will slow\n"
				  "down loading of background images considerably."), WMWidgetView(panel->smoB));

	panel->aaB = WMCreateButton(panel->smoF, WBTToggle);
	WMResizeWidget(panel->aaB, 76, 70);
	WMMoveWidget(panel->aaB, 139, 20);
	WMSetButtonFont(panel->aaB, font);
	WMSetButtonText(panel->aaB, _("Font edges"));
	WMSetButtonImagePosition(panel->aaB, WIPAbove);
	CreateImages(scr, rc, xis, "fonts", &altIcon, &icon);
	if (icon) {
		WMSetButtonImage(panel->aaB, icon);
		WMReleasePixmap(icon);
	}
	if (altIcon) {
		WMSetButtonAltImage(panel->aaB, altIcon);
		WMReleasePixmap(altIcon);
	}
	WMSetBalloonTextForView(_("Render fonts with antialiased (smooth) edges.\n"
			  "Applies to all WINGs applications immediately."), WMWidgetView(panel->aaB));

	WMMapSubwidgets(panel->smoF);

    /***************** Titlebar Style Size ****************/
	panel->titlF = WMCreateFrame(panel->box);
	WMResizeWidget(panel->titlF, 212, 100);
	WMMoveWidget(panel->titlF, 15, 120);
	WMSetFrameTitle(panel->titlF, _("Titlebar Style"));

	panel->oldsB = WMCreateButton(panel->titlF, WBTOnOff);
	WMResizeWidget(panel->oldsB, 50, 40);
	WMMoveWidget(panel->oldsB, 6, 32);
	WMSetButtonImagePosition(panel->oldsB, WIPImageOnly);
	path = LocateImage(OLDS_IMAGE);
	if (path) {
		icon = WMCreatePixmapFromFile(scr, path);
		if (icon) {
			WMSetButtonImage(panel->oldsB, icon);
			WMReleasePixmap(icon);
		}
		wfree(path);
	}

	panel->newsB = WMCreateButton(panel->titlF, WBTOnOff);
	WMResizeWidget(panel->newsB, 50, 40);
	WMMoveWidget(panel->newsB, 56, 32);
	WMSetButtonImagePosition(panel->newsB, WIPImageOnly);
	path = LocateImage(NEWS_IMAGE);
	if (path) {
		icon = WMCreatePixmapFromFile(scr, path);
		if (icon) {
			WMSetButtonImage(panel->newsB, icon);
			WMReleasePixmap(icon);
		}
		wfree(path);
	}

	panel->nextB = WMCreateButton(panel->titlF, WBTOnOff);
	WMResizeWidget(panel->nextB, 50, 40);
	WMMoveWidget(panel->nextB, 106, 32);
	WMSetButtonImagePosition(panel->nextB, WIPImageOnly);
	path = LocateImage(NEXT_IMAGE);
	if (path) {
		icon = WMCreatePixmapFromFile(scr, path);
		if (icon) {
			WMSetButtonImage(panel->nextB, icon);
			WMReleasePixmap(icon);
		}
		wfree(path);
	}

	panel->flatB = WMCreateButton(panel->titlF, WBTOnOff);
	WMResizeWidget(panel->flatB, 50, 40);
	WMMoveWidget(panel->flatB, 156, 32);
	WMSetButtonImagePosition(panel->flatB, WIPImageOnly);
	path = LocateImage(FLAT_IMAGE);
	if (path) {
		icon = WMCreatePixmapFromFile(scr, path);
		if (icon) {
			WMSetButtonImage(panel->flatB, icon);
			WMReleasePixmap(icon);
		}
		wfree(path);
	}

	WMGroupButtons(panel->newsB, panel->oldsB);
	WMGroupButtons(panel->newsB, panel->nextB);
	WMGroupButtons(panel->newsB, panel->flatB);

	WMMapSubwidgets(panel->titlF);

    /**************** Features ******************/
	panel->animF = WMCreateFrame(panel->box);
	WMResizeWidget(panel->animF, 268, 100);
	WMMoveWidget(panel->animF, 237, 10);
	WMSetFrameTitle(panel->animF, _("Animations and Sound"));

	panel->animB = WMCreateButton(panel->animF, WBTToggle);
	WMResizeWidget(panel->animB, 76, 70);
	WMMoveWidget(panel->animB, 10, 20);
	WMSetButtonFont(panel->animB, font);
	WMSetButtonText(panel->animB, _("Animations"));
	WMSetButtonImagePosition(panel->animB, WIPAbove);
	CreateImages(scr, rc, xis, ANIM_IMAGE, &altIcon, &icon);
	if (icon) {
		WMSetButtonImage(panel->animB, icon);
		WMReleasePixmap(icon);
	}
	if (altIcon) {
		WMSetButtonAltImage(panel->animB, altIcon);
		WMReleasePixmap(altIcon);
	}
	WMSetBalloonTextForView(_("Disable/enable animations such as those shown\n"
				  "for window miniaturization, shading etc."), WMWidgetView(panel->animB));

	panel->supB = WMCreateButton(panel->animF, WBTToggle);
	WMResizeWidget(panel->supB, 76, 70);
	WMMoveWidget(panel->supB, 96, 20);
	WMSetButtonFont(panel->supB, font);
	WMSetButtonText(panel->supB, _("Superfluous"));
	WMSetButtonImagePosition(panel->supB, WIPAbove);
	CreateImages(scr, rc, xis, SUPERF_IMAGE, &altIcon, &icon);
	if (icon) {
		WMSetButtonImage(panel->supB, icon);
		WMReleasePixmap(icon);
	}
	if (altIcon) {
		WMSetButtonAltImage(panel->supB, altIcon);
		WMReleasePixmap(altIcon);
	}
	WMSetBalloonTextForView(_("Disable/enable `superfluous' features and\n"
			  "animations. These include the `ghosting' of the\n"
			  "dock when it's being moved to another edge and\n"
			  "the explosion animation when undocking icons."), WMWidgetView(panel->supB));

	panel->sfxB = WMCreateButton(panel->animF, WBTToggle);
	WMResizeWidget(panel->sfxB, 76, 70);
	WMMoveWidget(panel->sfxB, 182, 20);
	WMSetButtonFont(panel->sfxB, font);
	WMSetButtonText(panel->sfxB, _("Sounds"));
	WMSetButtonImagePosition(panel->sfxB, WIPAbove);
	CreateImages(scr, rc, xis, SOUND_IMAGE, &altIcon, &icon);
	if (icon) {
		WMSetButtonImage(panel->sfxB, icon);
		WMReleasePixmap(icon);
	}
	if (altIcon) {
		WMSetButtonAltImage(panel->sfxB, altIcon);
		WMReleasePixmap(altIcon);
	}
	WMSetBalloonTextForView(_("Disable/enable sound effects for "
			  "window actions like shading, iconifying\n"
			  "and docking. Requires the wmsoundd daemon "
			  "to be running."), WMWidgetView(panel->sfxB));

	WMMapSubwidgets(panel->animF);

	WMRealizeWidget(panel->box);
	WMMapSubwidgets(panel->box);

	if (xis)
		RReleaseImage(xis);
	WMReleaseFont(font);
	showData(panel);
}

static void storeData(_Panel *panel)
{
	int i;

	for (i = 0; i < 5; i++) {
		if (WMGetButtonSelected(panel->icoB[i]))
			break;
	}
	SetSpeedForKey(i, "IconSlideSpeed");

	for (i = 0; i < 5; i++) {
		if (WMGetButtonSelected(panel->shaB[i]))
			break;
	}
	SetSpeedForKey(i, "ShadeSpeed");

	if (WMGetButtonSelected(panel->newsB)) {
		SetStringForKey("new", "NewStyle");
	} else if (WMGetButtonSelected(panel->oldsB)) {
		SetStringForKey("old", "NewStyle");
	} else if (WMGetButtonSelected(panel->flatB)) {
		SetStringForKey("flat", "NewStyle");
	} else {
		SetStringForKey("next", "NewStyle");
	}
	SetBoolForKey(!WMGetButtonSelected(panel->animB), "DisableAnimations");
	SetBoolForKey(WMGetButtonSelected(panel->supB), "Superfluous");
	SetBoolForKey(!WMGetButtonSelected(panel->sfxB), "DisableSound");
	SetBoolForKey(WMGetButtonSelected(panel->smoB), "SmoothWorkspaceBack");
	SetWMGlobalBoolForKey(WMGetButtonSelected(panel->aaB), "AntialiasedText");
}

Panel *InitConfigurations(WMWidget *parent)
{
	_Panel *panel;

	panel = wmalloc(sizeof(_Panel));

	panel->sectionName = _("Other Configurations");
	panel->description = _("Animation speeds, titlebar styles and\n"
			       "various option toggling.");

	panel->parent = parent;
	panel->callbacks.createWidgets = createPanel;
	panel->callbacks.updateDomain = storeData;
	panel->callbacks.undoChanges = showData;

	AddSection(panel, ICON_FILE);

	return panel;
}
