/*
 *  configuration.c
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 1998 scottc
 *  Copyright (c) 1999-2000 Alfredo K. Kojima
 *  Copyright (c) 2000-2004 Dan Pascu
 *  Copyright (c) 2009-2026 Window Maker Team
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

#include "WINGsP.h"
#include "wconfig.h"


_WINGsConfiguration WINGsConfiguration;

#define SYSTEM_FONT "sans serif"
#define BOLD_SYSTEM_FONT "sans serif:bold"
#define DEFAULT_FONT_SIZE 12

#define FLOPPY_PATH "/floppy"

static unsigned getButtonWithName(const char *name, unsigned defaultButton)
{
	if (strncmp(name, "Button", 6) == 0 && strlen(name) == 7) {
		switch (name[6]) {
		case '1':
			return XCB_BUTTON_INDEX_1;
		case '2':
			return XCB_BUTTON_INDEX_2;
		case '3':
			return XCB_BUTTON_INDEX_3;
		case '4':
			return XCB_BUTTON_INDEX_4;
		case '5':
			return XCB_BUTTON_INDEX_5;
		default:
			break;
		}
	}

	return defaultButton;
}

void W_ReadConfigurations(void)
{
	WMUserDefaults *defaults;
	bool aaIsSet = false;

	memset(&WINGsConfiguration, 0, sizeof(_WINGsConfiguration));

	defaults = WMGetStandardUserDefaults();

	if (defaults) {
		char *buttonName, *str;
		WMPropList *val;
		unsigned button;

		/* WMGetStandardUserDefaults() caches the WMGLOBAL dictionary,
		 * so a reload would keep seeing the first-read values */
		WMReloadUserDefaultsGlobalDomain();

		str = WMGetUDStringForKey(defaults, "SystemFont");
		WINGsConfiguration.systemFont = str ? wstrdup(str) : NULL;

		str = WMGetUDStringForKey(defaults, "BoldSystemFont");
		WINGsConfiguration.boldSystemFont = str ? wstrdup(str) : NULL;

		val = WMGetUDObjectForKey(defaults, "AntialiasedText");
		if (val && WMIsPLString(val) && WMGetFromPLString(val)) {
			aaIsSet = true;
			WINGsConfiguration.antialiasedText =
				WMGetUDBoolForKey(defaults, "AntialiasedText");
		}

		WINGsConfiguration.doubleClickDelay = WMGetUDIntegerForKey(defaults, "DoubleClickTime");

		str = WMGetUDStringForKey(defaults, "FloppyPath");
		WINGsConfiguration.floppyPath = str ? wstrdup(str) : NULL;

		buttonName = WMGetUDStringForKey(defaults, "MouseWheelUp");
		if (buttonName) {
			button = getButtonWithName(buttonName, XCB_BUTTON_INDEX_4);
		} else {
			button = XCB_BUTTON_INDEX_4;
		}
		WINGsConfiguration.mouseWheelUp = button;

		buttonName = WMGetUDStringForKey(defaults, "MouseWheelDown");
		if (buttonName) {
			button = getButtonWithName(buttonName, XCB_BUTTON_INDEX_5);
		} else {
			button = XCB_BUTTON_INDEX_5;
		}
		WINGsConfiguration.mouseWheelDown = button;

		if (WINGsConfiguration.mouseWheelDown == WINGsConfiguration.mouseWheelUp) {
			WINGsConfiguration.mouseWheelUp = XCB_BUTTON_INDEX_4;
			WINGsConfiguration.mouseWheelDown = XCB_BUTTON_INDEX_5;
		}

		WINGsConfiguration.defaultFontSize = WMGetUDIntegerForKey(defaults, "DefaultFontSize");

		str = WMGetUDStringForKey(defaults, "DefaultBackColor");
		WINGsConfiguration.defaultBackColor = str ? wstrdup(str) : NULL;

		str = WMGetUDStringForKey(defaults, "DarkGrayColor");
		WINGsConfiguration.darkGrayColor = str ? wstrdup(str) : NULL;

		str = WMGetUDStringForKey(defaults, "TabColor");
		WINGsConfiguration.tabColor = str ? wstrdup(str) : NULL;

		str = WMGetUDStringForKey(defaults, "TabLightColor");
		WINGsConfiguration.tabLightColor = str ? wstrdup(str) : NULL;

		str = WMGetUDStringForKey(defaults, "SelectionColor");
		WINGsConfiguration.selectionColor = str ? wstrdup(str) : NULL;

		str = WMGetUDStringForKey(defaults, "GridColor");
		WINGsConfiguration.gridColor = str ? wstrdup(str) : NULL;

		str = WMGetUDStringForKey(defaults, "TextFieldBackColor");
		WINGsConfiguration.textFieldBackColor = str ? wstrdup(str) : NULL;

		str = WMGetUDStringForKey(defaults, "SelectedBackColor");
		WINGsConfiguration.selectedBackColor = str ? wstrdup(str) : NULL;
	}

	if (!WINGsConfiguration.systemFont) {
		WINGsConfiguration.systemFont = wstrdup(SYSTEM_FONT);
	}
	if (!WINGsConfiguration.boldSystemFont) {
		WINGsConfiguration.boldSystemFont = wstrdup(BOLD_SYSTEM_FONT);
	}
	if (WINGsConfiguration.defaultFontSize == 0) {
		WINGsConfiguration.defaultFontSize = DEFAULT_FONT_SIZE;
	}
	if (!aaIsSet) {
		WINGsConfiguration.antialiasedText = true;
	}
	if (!WINGsConfiguration.floppyPath) {
		WINGsConfiguration.floppyPath = wstrdup(FLOPPY_PATH);
	}
	if (WINGsConfiguration.doubleClickDelay == 0) {
		WINGsConfiguration.doubleClickDelay = 250;
	}
	if (WINGsConfiguration.mouseWheelUp == 0) {
		WINGsConfiguration.mouseWheelUp = XCB_BUTTON_INDEX_4;
	}
	if (WINGsConfiguration.mouseWheelDown == 0) {
		WINGsConfiguration.mouseWheelDown = XCB_BUTTON_INDEX_5;
	}

}

unsigned W_getconf_mouseWheelUp(void)
{
	return WINGsConfiguration.mouseWheelUp;
}

unsigned W_getconf_mouseWheelDown(void)
{
	return WINGsConfiguration.mouseWheelDown;
}

const char *W_getconf_defaultBackColor(void)
{
	return WINGsConfiguration.defaultBackColor;
}

const char *W_getconf_darkGrayColor(void)
{
	return WINGsConfiguration.darkGrayColor;
}

const char *W_getconf_tabColor(void)
{
	return WINGsConfiguration.tabColor;
}

const char *W_getconf_tabLightColor(void)
{
	return WINGsConfiguration.tabLightColor;
}

const char *W_getconf_selectionColor(void)
{
	return WINGsConfiguration.selectionColor;
}

const char *W_getconf_gridColor(void)
{
	return WINGsConfiguration.gridColor;
}

const char *W_getconf_textFieldBackColor(void)
{
	return WINGsConfiguration.textFieldBackColor;
}

const char *W_getconf_selectedBackColor(void)
{
	return WINGsConfiguration.selectedBackColor;
}

void W_setconf_doubleClickDelay(int value)
{
	WINGsConfiguration.doubleClickDelay = value;
}

bool WMGetAntialiasedText(void)
{
	return WINGsConfiguration.antialiasedText;
}

const char *W_getconf_systemFont(void)
{
	return WINGsConfiguration.systemFont;
}

static bool confStringChanged(const char *old, const char *current)
{
	if (!old || !current)
		return old != current;

	return strcmp(old, current) != 0;
}

void WMReloadConfiguration(void)
{
	/* W_ReadConfigurations() memsets WINGsConfiguration, so release the
	 * heap strings it owns first to avoid leaking them on every reload
	 * (AntialiasedText, DefaultFontSize and the numeric fields are not
	 * heap-allocated). */
	char *colorKeys[8];
	bool colorsChanged;
	int i;

	colorKeys[0] = WINGsConfiguration.defaultBackColor;
	colorKeys[1] = WINGsConfiguration.darkGrayColor;
	colorKeys[2] = WINGsConfiguration.tabColor;
	colorKeys[3] = WINGsConfiguration.tabLightColor;
	colorKeys[4] = WINGsConfiguration.selectionColor;
	colorKeys[5] = WINGsConfiguration.gridColor;
	colorKeys[6] = WINGsConfiguration.textFieldBackColor;
	colorKeys[7] = WINGsConfiguration.selectedBackColor;

	if (WINGsConfiguration.systemFont) {
		wfree(WINGsConfiguration.systemFont);
		WINGsConfiguration.systemFont = NULL;
	}
	if (WINGsConfiguration.boldSystemFont) {
		wfree(WINGsConfiguration.boldSystemFont);
		WINGsConfiguration.boldSystemFont = NULL;
	}
	if (WINGsConfiguration.floppyPath) {
		wfree(WINGsConfiguration.floppyPath);
		WINGsConfiguration.floppyPath = NULL;
	}

	W_ReadConfigurations();

	/* the stash stays valid until here, so the change check runs
	 * before the release */
	colorsChanged =
	    confStringChanged(colorKeys[0], WINGsConfiguration.defaultBackColor) ||
	    confStringChanged(colorKeys[1], WINGsConfiguration.darkGrayColor) ||
	    confStringChanged(colorKeys[2], WINGsConfiguration.tabColor) ||
	    confStringChanged(colorKeys[3], WINGsConfiguration.tabLightColor) ||
	    confStringChanged(colorKeys[4], WINGsConfiguration.selectionColor) ||
	    confStringChanged(colorKeys[5], WINGsConfiguration.gridColor) ||
	    confStringChanged(colorKeys[6], WINGsConfiguration.textFieldBackColor) ||
	    confStringChanged(colorKeys[7], WINGsConfiguration.selectedBackColor);
	if (colorsChanged)
		W_ConfigColorsChanged();

	for (i = 0; i < 8; i++)
		wfree(colorKeys[i]);
}
