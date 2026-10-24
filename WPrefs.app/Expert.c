/* Expert.c- expert user options
 *
 *  WPrefs - Window Maker Preferences Program
 *
 *  Copyright (c) 2014-2026 Window Maker Team
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

#include <limits.h>

#include "WPrefs.h"

/* This structure containts the list of all the check-buttons to display in the
 * expert tab of the window with the corresponding information for effect.
 * Defaults for OPTION_WMAKER* keys come from the shared config schema.
 * OPTION_USERDEF goes to the WPrefs app domain.
 * OPTION_WMAKER_ALT maps a checkbox onto a two-value enum key, the schema
 * default is written when unchecked and the other enum value when checked.
 */
static struct expert_option {
	const char *label;  /* Text displayed to user */

	enum {
		OPTION_WMAKER,
		OPTION_WMAKER_ARRAY,
		OPTION_WMAKER_ALT,
		OPTION_USERDEF,
		OPTION_WMAKER_INT
	} class;

	const char *op_name;  /* The identifier for the option in the config file */

} expert_options[] = {

	{ N_("Disable miniwindows (icons for minimized windows). For use with KDE/GNOME."),
	  OPTION_WMAKER, "DisableMiniwindows" },

	{ N_("Ignore decoration hints for GTK applications."),
	  OPTION_WMAKER, "IgnoreGtkHints" },

	{ N_("Enable workspace pager."),
	  OPTION_WMAKER, "EnableWorkspacePager" },

	{ N_("Do not set non-WindowMaker specific parameters (do not use xset)."),
	  OPTION_USERDEF, "NoXSetStuff" },

	{ N_("Automatically save session when exiting Window Maker."),
	  OPTION_WMAKER, "SaveSessionOnExit" },

	{ N_("Use SaveUnder in window frames, icons, menus and other objects."),
	  OPTION_WMAKER, "UseSaveUnders" },

	{ N_("Disable confirmation panel for the Kill command."),
	  OPTION_WMAKER, "DontConfirmKill" },

	{ N_("Disable selection animation for selected icons."),
	  OPTION_WMAKER, "DisableBlinking" },

	{ N_("Blink the titlebar of windows that demand attention."),
	  OPTION_WMAKER, "BlinkTitlebarWhenUrgent" },

	{ N_("Cycle windows only on the active head."),
	  OPTION_WMAKER, "CycleActiveHeadOnly" },

	{ N_("Let a new window on another head take focus automatically."),
	  OPTION_WMAKER, "AutoFocusAcrossHeads" },

	{ N_("Each monitor keeps its own active workspace (requires restart)."),
	  OPTION_WMAKER, "PerHeadWorkspaces" },

	{ N_("Keep at least this many workspaces."),
	  OPTION_WMAKER_INT, "MinWorkspaceCount" },

#ifdef VIRTUAL_DESKTOP
	{ N_("Enable virtual desktop (pannable workspaces larger than the screen)."),
	  OPTION_WMAKER, "EnableVirtualDesktop" },
#endif

	{ N_("Cycle all windows from all workspaces."),
	  OPTION_WMAKER, "CycleAllWorkspaces" },

	{ N_("Ignore minimized windows when cycling."),
	  OPTION_WMAKER, "CycleIgnoreMinimized" },

	{ N_("Order the switch panel by creation time instead of focus history."),
	  OPTION_WMAKER_ALT, "SwitcherOrder" },

	{ N_("Show app icons in window list."),
	  OPTION_WMAKER, "WindowListAppIcons" },

	{ N_("Show switch panel when cycling windows."),
	  OPTION_WMAKER_ARRAY, "SwitchPanelImages" },

	{ N_("Show every tab-group member in the window switch panel."),
	  OPTION_WMAKER, "ShowAllTabsInSwitchPanel" },

	{ N_("Draw tab groups as titlebar segments instead of a [N/M] prefix."),
	  OPTION_WMAKER, "TabsInTitlebar" },

	{ N_("Accept tab drops anywhere on the target window, not just its titlebar."),
	  OPTION_WMAKER_ALT, "TabAttachArea" },

	{ N_("Show workspace title on Clip."),
	  OPTION_WMAKER, "ShowClipTitle" },

	{ N_("Skip empty workspaces when switching with Next/Prev/Left/Right/Up/Down keys."),
	  OPTION_WMAKER, "SkipEmptyWorkspaces" },

	{ N_("Highlight the icon of the application when it has the focus."),
	  OPTION_WMAKER, "HighlightActiveApp" },

#ifdef XKB_MODELOCK
	{ N_("Enable keyboard language switch button in window titlebars."),
	  OPTION_WMAKER, "KbdModeLock" },
#endif /* XKB_MODELOCK */

	{ N_("Snap a window to edge or corner by dragging."),
	  OPTION_WMAKER, "WindowSnapping" },

	{ N_("Distance from edge to begin window snap."),
	  OPTION_WMAKER_INT, "SnapEdgeDetect" },

	{ N_("Distance from corner to begin window snap."),
	  OPTION_WMAKER_INT, "SnapCornerDetect" },

	{ N_("Snap a window to the top to maximize it to the full screen."),
	  OPTION_WMAKER, "SnapToTopMaximizesFullscreen" },

	{ N_("Allow move half-maximized windows between multiple screens."),
	  OPTION_WMAKER, "MoveHalfMaximizedWindowsBetweenScreens" },

	{ N_("Alternative transitions between states for half maximized windows."),
	  OPTION_WMAKER, "AlternativeHalfMaximized" },

	{ N_("Move mouse pointer with half maximized windows."),
	  OPTION_WMAKER, "PointerWithHalfMaxWindows" },

	{ N_("Open dialogs in the same workspace as their owners."),
	  OPTION_WMAKER, "OpenTransientOnOwnerWorkspace" },

	{ N_("Wrap dock-attached icons around the screen edges."),
	  OPTION_WMAKER, "WrapAppiconsInDock" },

	{ N_("Double click on titlebar maximizes a window instead of shading it."),
	  OPTION_WMAKER, "DbClickMaximize" },

	{ N_("Close rootmenu when mouse (left or right) is clicked outside focus."),
	  OPTION_WMAKER, "CloseRootMenuByLeftOrRightMouseClick" },

	{ N_("Keep dock on primary head."),
	  OPTION_WMAKER, "KeepDockOnPrimaryHead"},

	{ N_("Allow windows to take focus using mouse wheel."),
	  OPTION_WMAKER, "MouseWheelFocus"},

	{ N_("Enable automatic mouse pointer hiding."),
	  OPTION_WMAKER, "AutoHideCursor"},

#ifdef USE_RANDR
	{ N_("Automatically (de)activate monitors on hotplug events."),
	  OPTION_WMAKER, "HotplugMonitor"},
#endif
};



typedef struct _Panel {
	WMBox *box;
	char *sectionName;

	char *description;

	CallbackRec callbacks;

	WMWidget *parent;

	WMButton *swi[wlengthof_nocheck(expert_options)];

	WMTextField *textfield[wlengthof_nocheck(expert_options)];
	WMScrollView *sv;
	WMWidget *frame;

} _Panel;

#define ICON_FILE	"expert"

/* Resolve the {unchecked, checked} string pair for an OPTION_WMAKER_ALT
 * key from the schema enum table, with the schema default as the
 * unchecked value. Works only for exactly-two-value enums, which is
 * all this row class is meant for. */
static bool altPairForKey(const char *key, const char *pair[2])
{
	const WConfigEnum *e = wConfigSchemaEnumTable(key);
	const char *def = wConfigSchemaDefault(key);
	int n = 0;

	pair[0] = pair[1] = NULL;
	if (!e || !def)
		return false;

	for (; e->string; e++) {
		if (e->is_alias)
			continue;
		if (strcasecmp(e->string, def) == 0)
			pair[0] = e->string;
		else
			pair[1] = e->string;
		n++;
	}
	return n == 2 && pair[0] && pair[1];
}

static void changeIntTextfield(void *data, int delta)
{
	WMTextField *textfield;
	const char *key;
	char *text, buffer[12];
	long value;

	textfield = (WMTextField *)data;
	key = (const char *) WMGetHangedData(textfield);
	text = WMGetTextFieldText(textfield);
	value = wConfigSchemaParseInt(key, text);
	wfree(text);

	if (value > (long)INT_MAX - delta)
		value = INT_MAX;
	else if (value < (long)INT_MIN - delta)
		value = INT_MIN;
	else
		value += delta;

	value = wConfigSchemaClampInt(key, (int)value);

	snprintf(buffer, sizeof(buffer), "%d", (int)value);
	WMSetTextFieldText(textfield, buffer);
}

static void scrollViewWheelHandler(xcb_generic_event_t *event, void *data)
{
	_Panel *panel = (_Panel *) data;
	int amount, viewH, contentH, newY, maxY;
	WMRect rect;
	WMPoint pt;

	if (!panel || !panel->sv || !panel->frame)
		return;

	if ((event->response_type & 0x7f) != XCB_BUTTON_PRESS)
		return;

	if (((xcb_button_press_event_t *)event)->detail != WINGsConfiguration.mouseWheelUp &&
		((xcb_button_press_event_t *)event)->detail != WINGsConfiguration.mouseWheelDown)
		return;

	rect = WMGetScrollViewVisibleRect(panel->sv);
	viewH = rect.size.height;
	contentH = WMWidgetHeight(panel->frame);

	if (((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_CONTROL) {
		amount = viewH; /* page */
	} else if (((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_SHIFT) {
		amount = 1; /* line */
	} else {
		amount = viewH / 3; /* default */
		if (amount == 0)
			amount = 1;
	}

	if (((xcb_button_press_event_t *)event)->detail == WINGsConfiguration.mouseWheelUp)
		amount = -amount;

	newY = rect.pos.y + amount;
	maxY = contentH - viewH;
	if (maxY < 0)
		maxY = 0;
	if (newY < 0)
		newY = 0;
	if (newY > maxY)
		newY = maxY;

	pt.x = rect.pos.x;
	pt.y = newY;

	WMScrollViewScrollPoint(panel->sv, pt);
}

static void scrollViewRealizeObserver(void *self, WMNotification *not)
{
	(void) not;
	_Panel *panel = (_Panel *) self;
	xcb_connection_t *dpy = NULL;
	xcb_window_t viewport_win = 0;
	WMView *frameView;

	if (!panel || !panel->frame)
		return;

	frameView = WMWidgetView(panel->frame);

	if (frameView && frameView->parent) {
		dpy = frameView->screen->display;
		viewport_win = frameView->parent->window;
	}

	/* fallback: use the scrollview's view window if parent viewport not available */
	if (!viewport_win && panel->sv) {
		WMView *svView = WMWidgetView(panel->sv);
		if (svView && svView->screen) {
			dpy = svView->screen->display;
			viewport_win = svView->window;
		}
	}

	if (!dpy || viewport_win == 0)
		return;

	wXGrabButton(dpy, WINGsConfiguration.mouseWheelUp, XCB_MOD_MASK_ANY, viewport_win,
				true, XCB_EVENT_MASK_BUTTON_PRESS, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE);
	wXGrabButton(dpy, WINGsConfiguration.mouseWheelDown, XCB_MOD_MASK_ANY, viewport_win,
				true, XCB_EVENT_MASK_BUTTON_PRESS, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE);
}

static void scrollViewPrepareForClose(Panel *p)
{
	_Panel *panel = (_Panel *) p;
	xcb_connection_t *dpy = NULL;
	xcb_window_t viewport_win = 0;

	if (!panel)
		return;

	if (panel->frame) {
		WMView *frameView = WMWidgetView(panel->frame);
		if (frameView && frameView->parent) {
			dpy = frameView->screen->display;
			viewport_win = frameView->parent->window;
		}
	}

	if (!viewport_win && panel->sv) {
		WMView *svView = WMWidgetView(panel->sv);
		if (svView && svView->screen) {
			dpy = svView->screen->display;
			viewport_win = svView->window;
		}
	}

	if (dpy && viewport_win != 0) {
		wXUngrabButton(dpy, WINGsConfiguration.mouseWheelUp, XCB_MOD_MASK_ANY, viewport_win);
		wXUngrabButton(dpy, WINGsConfiguration.mouseWheelDown, XCB_MOD_MASK_ANY, viewport_win);
	}

	WMRemoveNotificationObserver(panel);
}

static void downButtonCallback(WMWidget *self, void *data)
{
	(void) self;
	changeIntTextfield(data, -1);
}

static void upButtonCallback(WMWidget *self, void *data)
{
	(void) self;
	changeIntTextfield(data, 1);
}

/* Clamp the text field to the schema's [min, max] when the user leaves
 * the field, so the UI never displays (or stores) an out-of-range value
 * such as a negative snap distance. The option key is hung on the field
 * via WMHangData in createPanel(). */
static void intTextFieldEndEditing(WMTextField *tPtr)
{
	const char *key;
	char *text, buffer[12];
	int value;

	key = (const char *) WMGetHangedData(tPtr);
	if (!key)
		return;

	text = WMGetTextFieldText(tPtr);
	value = wConfigSchemaParseInt(key, text);
	wfree(text);
	snprintf(buffer, sizeof(buffer), "%d", value);
	WMSetTextFieldText(tPtr, buffer);
}

static void intTextFieldObserver(void *self, WMNotification *not)
{
	(void) self;
	intTextFieldEndEditing(WMGetNotificationObject(not));
}

static int cmpExpertOptions(const void *v1, const void *v2)
{
	int rc;
	const struct expert_option *opt1 = (struct expert_option *)v1;
	const struct expert_option *opt2 = (struct expert_option *)v2;

	if ((rc = strcmp(opt1->label, opt2->label)) < 0)
		return -1;
	else if (rc > 0)
		return 1;
	return 0;
}
static void showData(_Panel *panel)
{
	WMUserDefaults *udb = WMGetStandardUserDefaults();
	int i, state;

	for (i = 0; i < wlengthof(expert_options); i++) {
		switch (expert_options[i].class) {
		case OPTION_WMAKER:
			if (GetStringForKey(expert_options[i].op_name))
				state = GetBoolForKey(expert_options[i].op_name);
			else
				state = GetDefaultBoolForKey(expert_options[i].op_name);
			WMSetButtonSelected(panel->swi[i], state);
			break;

		case OPTION_WMAKER_ARRAY: {
				char *str = GetStringForKey(expert_options[i].op_name);
				/* enabled unless explicitly set to "None" */
				WMSetButtonSelected(panel->swi[i], !(str && strcasecmp(str, "None") == 0));
			}
			break;

		case OPTION_WMAKER_ALT: {
				const char *pair[2];
				const char *str = GetStringForKey(expert_options[i].op_name);
				state = altPairForKey(expert_options[i].op_name, pair) &&
					str && strcasecmp(str, pair[1]) == 0;
				WMSetButtonSelected(panel->swi[i], state);
			}
			break;

		case OPTION_USERDEF:
			WMSetButtonSelected(panel->swi[i], WMGetUDBoolForKey(udb, expert_options[i].op_name));
			break;

		case OPTION_WMAKER_INT:
		{
			char tmp[12];
			int value;

			if (GetStringForKey(expert_options[i].op_name))
				value = GetIntegerForKey(expert_options[i].op_name);
			else
				value = GetDefaultIntegerForKey(expert_options[i].op_name);

			snprintf(tmp, sizeof(tmp), "%d", value);
			WMSetTextFieldText(panel->textfield[i], tmp);
			break;
		}
		}
	}
}

static void createPanel(Panel *p)
{
	_Panel *panel = (_Panel *) p;
	WMScrollView *sv;
	WMFrame *f;
	int i;

	panel->box = WMCreateBox(panel->parent);
	WMSetViewExpandsToParent(WMWidgetView(panel->box), 2, 2, 2, 2);

	sv = WMCreateScrollView(panel->box);
	WMResizeWidget(sv, 500, 215);
	WMMoveWidget(sv, 12, 10);
	WMSetScrollViewRelief(sv, WRSunken);
	WMSetScrollViewHasVerticalScroller(sv, true);
	WMSetScrollViewHasHorizontalScroller(sv, false);

	f = WMCreateFrame(panel->box);
	WMResizeWidget(f, 495, wlengthof(expert_options) * 25 + 8);
	WMSetFrameRelief(f, WRFlat);

	qsort(expert_options, wlengthof(expert_options), sizeof(expert_options[0]), cmpExpertOptions);
	for (i = 0; i < wlengthof(expert_options); i++) {
		if (expert_options[i].class != OPTION_WMAKER_INT) {
			panel->swi[i] = WMCreateSwitchButton(f);
			WMResizeWidget(panel->swi[i], FRAME_WIDTH - 40, 25);
			WMMoveWidget(panel->swi[i], 5, 5 + i * 25);

			WMSetButtonText(panel->swi[i], _(expert_options[i].label));
		}

		if (expert_options[i].class == OPTION_WMAKER_INT) {
			WMButton *up, *down;
			WMLabel *label;

			panel->textfield[i] = WMCreateTextField(f);
			WMResizeWidget(panel->textfield[i], 41, 20);
			WMMoveWidget(panel->textfield[i], 22, 7 + i * 25);
			WMHangData(panel->textfield[i], (void *) expert_options[i].op_name);
			WMAddNotificationObserver(intTextFieldObserver, panel,
						  WMTextDidEndEditingNotification,
						  panel->textfield[i]);

			down = WMCreateCommandButton(f);
			WMSetButtonImage(down, WMGetSystemPixmap(WMWidgetScreen(down), WSIArrowDown));
			WMSetButtonAltImage(down,
					    WMGetSystemPixmap(WMWidgetScreen(down), WSIHighlightedArrowDown));
			WMSetButtonImagePosition(down, WIPImageOnly);
			WMSetButtonAction(down, downButtonCallback, panel->textfield[i]);
			WMResizeWidget(down, 16, 16);
			WMMoveWidget(down, 5, 9 + i * 25);

			up = WMCreateCommandButton(f);
			WMSetButtonImage(up, WMGetSystemPixmap(WMWidgetScreen(up), WSIArrowUp));
			WMSetButtonAltImage(up,
					    WMGetSystemPixmap(WMWidgetScreen(up), WSIHighlightedArrowUp));
			WMSetButtonImagePosition(up, WIPImageOnly);
			WMSetButtonAction(up, upButtonCallback, panel->textfield[i]);
			WMResizeWidget(up, 16, 16);
			WMMoveWidget(up, 64, 9 + i * 25);

			label = WMCreateLabel(f);
			WMSetLabelText(label, _(expert_options[i].label));
			WMResizeWidget(label, FRAME_WIDTH - 99, 25);
			WMMoveWidget(label, 85, 5 + i * 25);
		}
	}

	WMMapSubwidgets(panel->box);
	WMSetScrollViewContentView(sv, WMWidgetView(f));
	/* keep references for the wheel handler and register it */
	panel->sv = sv;
	panel->frame = f;
	WMCreateEventHandler(WMWidgetView(sv), XCB_EVENT_MASK_BUTTON_PRESS, scrollViewWheelHandler, panel);
	WMAddNotificationObserver(scrollViewRealizeObserver, panel, WMViewRealizedNotification, WMWidgetView(sv));
	WMRealizeWidget(panel->box);

	showData(panel);
}

static void storeDefaults(_Panel *panel)
{
	WMUserDefaults *udb = WMGetStandardUserDefaults();
	int i;

	for (i = 0; i < wlengthof(expert_options); i++) {
		switch (expert_options[i].class) {
		case OPTION_WMAKER:
			SetBoolForKey(WMGetButtonSelected(panel->swi[i]), expert_options[i].op_name);
			break;

		case OPTION_WMAKER_ARRAY:
			if (WMGetButtonSelected(panel->swi[i])) {
				/* check if the array was not manually modified */
				char *str = GetStringForKey(expert_options[i].op_name);
				if (str && strcasecmp(str, "None") == 0)
					RemoveObjectForKey(expert_options[i].op_name);
			}
			else
				SetStringForKey("None", expert_options[i].op_name);
			break;

		case OPTION_WMAKER_ALT: {
				const char *pair[2];
				if (altPairForKey(expert_options[i].op_name, pair))
					SetStringForKey(pair[WMGetButtonSelected(panel->swi[i]) ? 1 : 0],
							expert_options[i].op_name);
			}
			break;

		case OPTION_USERDEF:
			WMSetUDBoolForKey(udb, WMGetButtonSelected(panel->swi[i]), expert_options[i].op_name);
			break;

		case OPTION_WMAKER_INT:
		{
			char *text = WMGetTextFieldText(panel->textfield[i]);
			int value = wConfigSchemaParseInt(expert_options[i].op_name, text);

			wfree(text);
			SetIntegerForKey(value, expert_options[i].op_name);
			break;
		}
		}
	}
}

Panel *InitExpert(WMWidget *parent)
{
	_Panel *panel;

	panel = wmalloc(sizeof(_Panel));

	panel->sectionName = _("Expert User Preferences");

	panel->description = _("Options for people who know what they're doing...\n"
			       "Also has some other misc. options.");

	panel->parent = parent;

	panel->callbacks.createWidgets = createPanel;
	panel->callbacks.updateDomain = storeDefaults;
	panel->callbacks.undoChanges = showData;
	panel->callbacks.prepareForClose = scrollViewPrepareForClose;

	AddSection(panel, ICON_FILE);

	return panel;
}
