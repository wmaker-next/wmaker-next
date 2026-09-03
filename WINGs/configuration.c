
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

void W_setconf_doubleClickDelay(int value)
{
	WINGsConfiguration.doubleClickDelay = value;
}

bool WMGetAntialiasedText(void)
{
	return WINGsConfiguration.antialiasedText;
}

void WMReloadConfiguration(void)
{
	/* W_ReadConfigurations() memsets WINGsConfiguration, so release the
	 * heap strings it owns first to avoid leaking them on every reload
	 * (AntialiasedText, DefaultFontSize and the numeric fields are not
	 * heap-allocated). */
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
}
