/* winspector.c - window attribute inspector
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 1998-2003 Dan Pascu
 *  Copyright (c) 2026 Window Maker Team
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
#include <WINGs/WXHints.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "WindowMaker.h"
#include "screen.h"
#include "wcore.h"
#include "framewin.h"
#include "window.h"
#include "workspace.h"
#include "defaults.h"
#include "wdefaults.h"
#include "dialog.h"
#include "dialog_icon.h"
#include "icon.h"
#include "stacking.h"
#include "application.h"
#include "appicon.h"
#include "actions.h"
#include "winspector.h"
#include "dock.h"
#include "client.h"
#include "wmspec.h"
#include "misc.h"
#include "wxkb.h"
#include "switchmenu.h"

#include <WINGs/WUtil.h>

#define USE_TEXT_FIELD		1
#define UPDATE_TEXT_FIELD	2
#define REVERT_TO_DEFAULT	4
#define PWIDTH			290
#define ICON_PREVIEW_SIZE	56
#ifdef XKB_BUTTON_HINT
#define PHEIGHT			410
#else
#define PHEIGHT			390
#endif
#define UNDEFINED_POS		0xffffff
#define UPDATE_DEFAULTS		1
#define IS_BOOLEAN		2


/*
 * Generated from WATTR_BOOLS() in wdefaults.h: maps each WWindowAttrKey
 * to its proplist key string and the matching one-bit WWindowAttributes
 * mask, so the inspector cannot drift from the WMWindowAttributes schema.
 * The per-panel tables below carry only the page assignment and the
 * translatable label/balloon text, key_name and flag are looked up via
 * wattr_spec[tbl[i].attr].
 */
static const struct {
	const char *key_name;
	WWindowAttributes flag;
} wattr_spec[WATTR_BOOL_COUNT] = {
#define WATTR_SPEC_ROW(K, name, fld) { name, { .fld = 1 } },
	WATTR_BOOLS(WATTR_SPEC_ROW)
#undef WATTR_SPEC_ROW
};

_Static_assert(WATTR_BOOL_COUNT <= 64,
	       "make_keys() coverage bitmask needs widening past uint64_t");

static const struct InspectorAttr {
	WWindowAttrKey attr;
	const char *caption;
	const char *description;
} window_attribute[] = {
	{ WATTR_NO_TITLEBAR, N_("Disable titlebar"),
	  N_("Remove the titlebar of this window.\n"
	     "To access the window commands menu of a window\n"
	     "without its titlebar, press Control+Esc (or the\n"
	     "equivalent shortcut, if you changed the default\n"
	     "settings).") },

	{ WATTR_NO_RESIZEBAR, N_("Disable resizebar"),
	  N_("Remove the resizebar of this window.") },

	{ WATTR_NO_CLOSE_BUTTON, N_("Disable close button"),
	  N_("Remove the `close window' button of this window.") },

	{ WATTR_NO_MINIATURIZE_BUTTON, N_("Disable miniaturize button"),
	  N_("Remove the `miniaturize window' button of the window.") },

	{ WATTR_NO_BORDER, N_("Disable border"),
	  N_("Remove the 1 pixel black border around the window.") },

	{ WATTR_KEEP_ON_TOP, N_("Keep on top (floating)"),
	  N_("Keep the window over other windows, not allowing\n"
	     "them to cover it.") },

	{ WATTR_KEEP_ON_BOTTOM, N_("Keep at bottom (sunken)"),
	  N_("Keep the window under all other windows.") },

	{ WATTR_OMNIPRESENT, N_("Omnipresent"),
	  N_("Make window present in all workspaces.") },

	{ WATTR_AUTO_MINIATURIZE_ON_UNFOCUS, N_("Auto-miniaturize on unfocus"),
	  N_("Miniaturize the window automatically when it loses focus.") },

	{ WATTR_AUTO_HIDE_ON_UNFOCUS, N_("Auto-hide on unfocus"),
	  N_("Hide the window automatically when it loses focus.") },

	{ WATTR_START_MINIATURIZED, N_("Start miniaturized"),
	  N_("Make the window be automatically miniaturized when it's\n"
	     "first shown.") },

	{ WATTR_START_MAXIMIZED, N_("Start maximized"),
	  N_("Make the window be automatically maximized when it's\n"
	     "first shown.") },

	{ WATTR_FULL_MAXIMIZE, N_("Full screen maximization"),
	  N_("Make the window use the whole screen space when it's\n"
	     "maximized. The titlebar and resizebar will be moved\n"
	     "to outside the screen.") },

	{ WATTR_SWALLOW_CHILD, N_("Swallow child window"),
	  N_("When a child process of this window maps its own window,\n"
	     "hide this one and place the child where it was. When the\n"
	     "child closes, this window reappears.") }

}, advanced_option[] = {
	{ WATTR_NO_KEY_BINDINGS, N_("Do not bind keyboard shortcuts"),
	  N_("Do not bind keyboard shortcuts from Window Maker\n"
	     "when this window is focused. This will allow the\n"
	     "window to receive all key combinations regardless\n"
	     "of your shortcut configuration.") },

	{ WATTR_NO_MOUSE_BINDINGS, N_("Do not bind mouse clicks"),
	  N_("Do not bind mouse actions, such as `Alt'+drag\n"
	     "in the window (when Alt is the modifier you have\n"
	     "configured).") },

	{ WATTR_SKIP_WINDOW_LIST, N_("Do not show in the window list"),
	  N_("Do not list the window in the window list menu.") },

	{ WATTR_SKIP_SWITCH_PANEL, N_("Do not show in the switch panel"),
	  N_("Do not include in switch panel while cycling windows.") },

	{ WATTR_UNFOCUSABLE, N_("Do not let it take focus"),
	  N_("Do not let the window take keyboard focus when you\n"
	     "click on it.") },

	{ WATTR_KEEP_INSIDE_SCREEN, N_("Keep inside screen"),
	  N_("Do not allow the window to move itself completely\n"
	     "outside the screen. For bug compatibility.\n") },

	{ WATTR_NO_HIDE_OTHERS, N_("Ignore 'Hide Others'"),
	  N_("Do not hide the window when issuing the\n"
	     "`HideOthers' command.") },

	{ WATTR_DONT_SAVE_SESSION, N_("Ignore 'Save Session'"),
	  N_("Do not save the associated application in the\n"
	     "session's state, so that it won't be restarted\n"
	     "together with other applications when Window Maker\n"
	     "starts.") },

	{ WATTR_EMULATE_APP_ICON, N_("Emulate application icon"),
	  N_("Make this window act as an application that provides\n"
	     "enough information to Window Maker for a dockable\n"
	     "application icon to be created.") },

	{ WATTR_FOCUS_ACROSS_WORKSPACE, N_("Focus across workspaces"),
	  N_("Allow Window Maker to switch workspace to satisfy\n"
	     "a focus request (annoying).") },

	{ WATTR_NO_MINIATURIZABLE, N_("Do not let it be minimized"),
	  N_("Do not let the window of this application be\n"
	     "minimized.\n") },

	{ WATTR_IGNORE_DECORATION_CHANGES, N_("Ignore decoration changes"),
	  N_("Ignore any request from the application to change\n"
	     "window decorations (like hiding the titlebar).\n") },

	{ WATTR_NO_TABBING, N_("Do not allow tab grouping"),
	  N_("Refuse this window as either the dragged window or the\n"
	     "drop target when attaching windows into a tab group.") },

	{ WATTR_NO_TILING, N_("Do not tile"),
	  N_("Exempt this window from the workspace tiling layout.\n"
	     "It stays floating and the layout reflows around it.") },

#ifdef XKB_BUTTON_HINT
	{ WATTR_NO_LANGUAGE_BUTTON, N_("Disable language button"),
	  N_("Remove the `toggle language' button of the window.") },
#endif

}, application_attr[] = {
	{ WATTR_START_HIDDEN, N_("Start hidden"),
	  N_("Automatically hide application when it's started.") },

	{ WATTR_NO_APP_ICON, N_("No application icon"),
	  N_("Disable the application icon for the application.\n"
	     "Note that you won't be able to dock it anymore,\n"
	     "and any icons that are already docked will stop\n"
	     "working correctly.") },

	{ WATTR_SHARED_APP_ICON, N_("Shared application icon"),
	  N_("Use a single shared application icon for all of\n"
	     "the instances of this application.\n") }
};

typedef struct InspectorPanel {
	struct InspectorPanel *nextPtr;

	WWindow *frame;
	WWindow *inspected;	/* the window that's being inspected */
	WMWindow *win;
	xcb_window_t parent;

	/* common stuff */
	WMButton *revertBtn;
	WMButton *applyBtn;
	WMButton *saveBtn;
	WMPopUpButton *pagePopUp;

	/* first page. general stuff */
	WMFrame *specFrm;
	WMButton *instRb;
	WMButton *clsRb;
	WMButton *bothRb;
	WMButton *defaultRb;
	WMButton *selWinB;
	WMLabel *specLbl;

	/* second page. attributes */
	WMFrame *attrFrm;
	WMButton *attrChk[sizeof(window_attribute) / sizeof(window_attribute[0])];

	/* 3rd page. more attributes */
	WMFrame *moreFrm;
	WMButton *moreChk[sizeof(advanced_option) / sizeof(advanced_option[0])];

	/* 4th page. icon and workspace */
	WMFrame *iconFrm;
	WMLabel *iconLbl;
	WMLabel *fileLbl;
	WMTextField *fileText;
	WMButton *alwChk;

	/* page 0 standalone: auto-group all windows of this class */
	WMButton *autoTabGrpChk;
	WMButton *browseIconBtn;
	WMFrame *wsFrm;
	WMMultiSelectPopUpButton *wsP;

	/* 5th page. application wide attributes */
	WMFrame *appFrm;
	WMButton *appChk[sizeof(application_attr) / sizeof(application_attr[0])];

	unsigned int done:1;
	unsigned int destroyed:1;
	unsigned int choosingIcon:1;
	char *applied_icon_file;
} InspectorPanel;

static InspectorPanel *panelList = NULL;

/*
 * Property List cache for the WMWindowAttributes key strings, indexed by
 * WWindowAttrKey, generated once on first use so the per-panel save loops
 * can write pl_wattr[tbl[i].attr] without allocating a fresh PLString
 * every time.
 */
static WMPropList *pl_wattr[WATTR_BOOL_COUNT] = { [0] = NULL };

static WMPropList *AStartWorkspaces;
static WMPropList *AIcon;

/* application wide options */
static WMPropList *AnyWindow;
static WMPropList *EmptyString;
static WMPropList *Yes, *No;

static char *spec_text;
static void applySettings(WMWidget *button, void *panel);
static void applySettingsInternal(WMWidget *button, void *panel, bool per_window);

static InspectorPanel *createInspectorForWindow(WWindow *wwin, int xpos, int ypos, bool showSelectPanel);

static void create_tab_window_attributes(WWindow *wwin, InspectorPanel *panel, int frame_width);
static void create_tab_window_advanced(WWindow *wwin, InspectorPanel *panel, int frame_width);
static void create_tab_icon_workspace(WWindow *wwin, InspectorPanel *panel);
static void create_tab_app_specific(WWindow *wwin, InspectorPanel *panel, int frame_width);

/*
 * These 3 functions sets/clear/read the bit in a WWindowAttributes that
 * corresponds to a WWindowAttrKey. The one-bit mask comes from
 * wattr_spec[key].flag, the byte-loop OR/AND keeps working regardless of
 * which underlying word the compiler packs the bitfield into.
 */
static inline void set_attr_flag(WWindowAttributes *target, WWindowAttrKey key)
{
	size_t i;
	const unsigned char *src = (const unsigned char *) &wattr_spec[key].flag;
	unsigned char *dst = (unsigned char *) target;

	for (i = 0; i < sizeof(*target); i++)
		dst[i] |= src[i];
}

static inline void clear_attr_flag(WWindowAttributes *target, WWindowAttrKey key)
{
	size_t i;
	const unsigned char *src = (const unsigned char *) &wattr_spec[key].flag;
	unsigned char *dst = (unsigned char *) target;

	for (i = 0; i < sizeof(*target); i++)
		dst[i] &= ~src[i];
}

static inline int get_attr_flag(const WWindowAttributes *from, WWindowAttrKey key)
{
	size_t i;
	const unsigned char *field = (const unsigned char *) from;
	const unsigned char *xpect = (const unsigned char *) &wattr_spec[key].flag;

	for (i = 0; i < sizeof(*from); i++)
		if (field[i] & xpect[i])
			return 1;

	return 0;
}

/*
 * This function is creating the Property List for the cache mentionned above
 */
static void make_keys(void)
{
	int i;
	uint64_t covered;

	if (pl_wattr[0] != NULL)
		return;

	for (i = 0; i < WATTR_BOOL_COUNT; i++)
		pl_wattr[i] = WMCreatePLString(wattr_spec[i].key_name);

	/* every WATTR_BOOLS row must appear on exactly one inspector page,
	 * AlwaysUserIcon is the page-4 ignore-icon checkbox so it is the
	 * one expected gap in the three per-panel tables; AutoTabGroup is
	 * the page-0 Window Specification checkbox and is likewise handled
	 * as a standalone button outside the table-driven pages */
	covered = ((uint64_t)1 << WATTR_ALWAYS_USER_ICON) |
	    ((uint64_t)1 << WATTR_AUTO_TAB_GROUP);
	for (i = 0; i < wlengthof(window_attribute); i++)
		covered |= (uint64_t)1 << window_attribute[i].attr;
	for (i = 0; i < wlengthof(advanced_option); i++)
		covered |= (uint64_t)1 << advanced_option[i].attr;
	for (i = 0; i < wlengthof(application_attr); i++)
		covered |= (uint64_t)1 << application_attr[i].attr;
	if (covered != (~(uint64_t)0 >> (64 - WATTR_BOOL_COUNT)))
		wwarning("inspector attribute tables out of sync with WATTR_BOOLS");

	AIcon = WMCreatePLString("Icon");
	AStartWorkspaces = WMCreatePLString("StartWorkspaces");

	AnyWindow = WMCreatePLString("*");
	EmptyString = WMCreatePLString("");
	Yes = WMCreatePLString("Yes");
	No = WMCreatePLString("No");
}

static void freeInspector(InspectorPanel *panel)
{
	panel->destroyed = 1;

	if (panel->choosingIcon)
		return;

	WMDestroyWidget(panel->win);
	xcb_destroy_window(wdisp->conn, panel->parent);
	if (panel->applied_icon_file)
		wfree(panel->applied_icon_file);
	wfree(panel);
}

static void destroyInspector(WCoreWindow *foo, void *data, xcb_generic_event_t *event)
{
	InspectorPanel *panel, *tmp;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;
	(void) event;

	panel = panelList;
	while (panel->frame != data)
		panel = panel->nextPtr;

	if (panelList == panel) {
		panelList = panel->nextPtr;
	} else {
		tmp = panelList;
		while (tmp->nextPtr != panel)
			tmp = tmp->nextPtr;

		tmp->nextPtr = panel->nextPtr;
	}
	panel->inspected->flags.inspector_open = 0;
	panel->inspected->inspector = NULL;

	WMRemoveNotificationObserver(panel);

	wWindowUnmap(panel->frame);
	wUnmanageWindow(panel->frame, true, false);

	freeInspector(panel);
}

static void keyPressHandler(xcb_generic_event_t *event, void *data)
{
	InspectorPanel *panel = (InspectorPanel *) data;

	if (wXkbKeycodeToKeysym(((xcb_key_press_event_t *)event)->detail, 0) == XKB_KEY_Escape)
		destroyInspector(NULL, panel->frame, NULL);
}

void wDestroyInspectorPanels(void)
{
	InspectorPanel *panel;

	while (panelList != NULL) {
		panel = panelList;
		panelList = panelList->nextPtr;
		wUnmanageWindow(panel->frame, false, false);
		WMDestroyWidget(panel->win);

		panel->inspected->flags.inspector_open = 0;
		panel->inspected->inspector = NULL;

		wfree(panel);
	}
}

static void changePage(WMWidget *bPtr, void *client_data)
{
	InspectorPanel *panel = (InspectorPanel *) client_data;
	int page;

	page = WMGetPopUpButtonSelectedItem(bPtr);

	if (page == 0) {
		WMMapWidget(panel->specFrm);
		WMMapWidget(panel->autoTabGrpChk);
		WMMapWidget(panel->specLbl);
	} else if (page == 1) {
		WMMapWidget(panel->attrFrm);
	} else if (page == 2) {
		WMMapWidget(panel->moreFrm);
	} else if (page == 3) {
		WMMapWidget(panel->iconFrm);
		WMMapWidget(panel->wsFrm);
	} else {
		WMMapWidget(panel->appFrm);
	}

	if (page != 0) {
		WMUnmapWidget(panel->specFrm);
		WMUnmapWidget(panel->autoTabGrpChk);
		WMUnmapWidget(panel->specLbl);
	}
	if (page != 1)
		WMUnmapWidget(panel->attrFrm);
	if (page != 2)
		WMUnmapWidget(panel->moreFrm);
	if (page != 3) {
		WMUnmapWidget(panel->iconFrm);
		WMUnmapWidget(panel->wsFrm);
	}
	if (page != 4 && panel->appFrm)
		WMUnmapWidget(panel->appFrm);
}

static int showIconFor(WMScreen *scrPtr, InspectorPanel *panel, const char *wm_instance, const char *wm_class, int flags)
{
	WMPixmap *pixmap = (WMPixmap *) NULL;
	char *file = NULL, *path = NULL;
	RColor color;
	RImage *net_icon;

	if ((flags & USE_TEXT_FIELD) != 0) {
		file = WMGetTextFieldText(panel->fileText);
		if (file && file[0] == 0) {
			wfree(file);
			file = NULL;
		}
	} else if (flags & REVERT_TO_DEFAULT) {
		const char *db_icon;

		/* Get the application icon, default NOT included */
		db_icon = wDefaultGetIconFile(wm_instance, wm_class, false);
		if (db_icon != NULL) {
			file = wstrdup(db_icon);
			flags |= UPDATE_TEXT_FIELD;
		}
	}

	if ((flags & UPDATE_TEXT_FIELD) != 0)
		WMSetTextFieldText(panel->fileText, file);

	if (!file && (wm_instance || wm_class)) {
		/* no per-app Icon= entry, _NET_WM_ICON-only apps get the
		 * icon resolved by the same chain as their appicon */
		path = get_icon_filename(wm_instance, wm_class, NULL, false);
	} else if (file) {
		path = FindImage(w_global.config->icon_path, file);
		/* fall back to the icon cache directory, same as
		 * get_icon_filename() in wdefaults.c */
		if (!path)
			path = find_in_icon_cache(file, wm_instance, wm_class);

		if (!path) {
			char *buf;
			int len = strlen(file) + 80;

			buf = wmalloc(len);
			snprintf(buf, len, _("Could not find icon \"%s\" specified for this window"), file);
			wMessageDialog(panel->frame->screen_ptr, _("Error"), buf, _("OK"), NULL, NULL);
			wfree(buf);
			wfree(file);
			return -1;
		}

		wfree(file);
	}

	if (path) {
		color.red = 0xae;
		color.green = 0xaa;
		color.blue = 0xae;
		color.alpha = 0;
		pixmap = WMCreateScaledBlendedPixmapFromFile(scrPtr, path, &color,
							     ICON_PREVIEW_SIZE, ICON_PREVIEW_SIZE);

		if (!pixmap) {
			char *buf;
			int len = strlen(path) + 80;

			buf = wmalloc(len);
			snprintf(buf, len, _("Could not open specified icon \"%s\":%s"),
				 path, RMessageForError(RErrorCode));
			wMessageDialog(panel->frame->screen_ptr, _("Error"), buf, _("OK"), NULL, NULL);
			wfree(buf);
			wfree(path);
			return -1;
		}
		wfree(path);
	} else if (panel->inspected && panel->inspected->net_icon_image) {
		/* apps without any icon file get their _NET_WM_ICON */
		net_icon = RRetainImage(panel->inspected->net_icon_image);
		net_icon = wIconValidateIconSize(net_icon,
						 ICON_PREVIEW_SIZE + ICON_BORDER_WIDTH);
		pixmap = WMCreatePixmapFromRImage(scrPtr, net_icon, 127);
		RReleaseImage(net_icon);
	}

	if (!pixmap)
		return 0;

	WMSetLabelImage(panel->iconLbl, pixmap);
	if (pixmap)
		WMReleasePixmap(pixmap);

	return 0;
}

/* Will insert the attribute = value; pair in window's list,
 * if it's different from the defaults.
 * Defaults means either defaults database, or attributes saved
 * for the default window "*". This is to let one revert options that are
 * global because they were saved for all windows ("*"). */
static int
insertAttribute(WMPropList *dict, WMPropList *window, WMPropList *attr, WMPropList *value, int flags)
{
	WMPropList *def_win, *def_value = NULL;
	int update = 0, modified = 0;

	if (!(flags & UPDATE_DEFAULTS) && dict) {
		def_win = WMGetFromPLDictionary(dict, AnyWindow);
		if (def_win != NULL)
			def_value = WMGetFromPLDictionary(def_win, attr);
	}

	/* If we could not find defaults in database, fall to hardcoded values.
	 * Also this is True if we save defaults for all windows */
	if (!def_value)
		def_value = ((flags & IS_BOOLEAN) != 0) ? No : EmptyString;

	if (flags & IS_BOOLEAN)
		update = (WMPLGetBool(value) != WMPLGetBool(def_value));
	else
		update = !WMIsPropListEqualTo(value, def_value);

	if (update) {
		WMPutInPLDictionary(window, attr, value);
		modified = 1;
	}

	return modified;
}

static void saveSettings(WMWidget *button, void *client_data)
{
	InspectorPanel *panel = (InspectorPanel *) client_data;
	WWindow *wwin = panel->inspected;
	WDDomain *db = w_global.domain.window_attr;
	WMPropList *dict = NULL;
	WMPropList *winDic, *appDic, *value, *value1, *key = NULL, *key2, *arr = NULL;
	char *icon_file, *buf1, *buf2;
	int flags = 0, i = 0, different = 0, different2 = 0;
	uint64_t mask;
	int ws_count, j;

	/* Save will apply the changes and save them */
	applySettingsInternal(panel->applyBtn, panel, false);

	if (WMGetButtonSelected(panel->instRb) != 0) {
		key = WMCreatePLString(wwin->wm_instance);
	} else if (WMGetButtonSelected(panel->clsRb) != 0) {
		key = WMCreatePLString(wwin->wm_class);
	} else if (WMGetButtonSelected(panel->bothRb) != 0) {
		buf1 = StrConcatDot(wwin->wm_instance, wwin->wm_class);
		key = WMCreatePLString(buf1);
		wfree(buf1);
	} else if (WMGetButtonSelected(panel->defaultRb) != 0) {
		key = WMRetainPropList(AnyWindow);
		flags = UPDATE_DEFAULTS;
	}

	if (!key)
		return;

	dict = db->dictionary;
	if (!dict) {
		dict = WMCreatePLDictionary(NULL, NULL);
		if (dict) {
			db->dictionary = dict;
		} else {
			WMReleasePropList(key);
			return;
		}
	}

	if (showIconFor(WMWidgetScreen(button), panel, NULL, NULL, USE_TEXT_FIELD) < 0)
		return;

	WMPLSetCaseSensitive(true);

	winDic = WMCreatePLDictionary(NULL, NULL);
	appDic = WMCreatePLDictionary(NULL, NULL);

	/* Save the icon info */
	/* The flag "Ignore client suplied icon is not selected" */
	buf1 = wmalloc(4);
	snprintf(buf1, 4, "%s", (WMGetButtonSelected(panel->alwChk) != 0) ? "Yes" : "No");
	value1 = WMCreatePLString(buf1);
	different |= insertAttribute(dict, winDic, pl_wattr[WATTR_ALWAYS_USER_ICON], value1, flags | IS_BOOLEAN);
	WMReleasePropList(value1);
	wfree(buf1);

	/* Auto tab group by class (page 0 standalone checkbox) */
	value = WMCreatePLString((WMGetButtonSelected(panel->autoTabGrpChk) != 0) ? "Yes" : "No");
	different |= insertAttribute(dict, winDic, pl_wattr[WATTR_AUTO_TAB_GROUP], value, flags | IS_BOOLEAN);
	WMReleasePropList(value);

	/* The icon filename (if exists) */
	icon_file = WMGetTextFieldText(panel->fileText);
	if (icon_file != NULL) {
		if (icon_file[0] != '\0') {
			value = WMCreatePLString(icon_file);
			different |= insertAttribute(dict, winDic, AIcon, value, flags);
			WMReleasePropList(value);
		}
		wfree(icon_file);
	}

	mask = WMGetMultiSelectPopUpButtonSelectionMask(panel->wsP);
	ws_count = panel->frame->screen_ptr->workspace_count;

	for (j = 0; j < ws_count; j++) {
		if (mask & ((uint64_t)1 << j)) {
			if (!arr)
				arr = WMCreatePLArray(NULL, 0);
			value = WMCreatePLString(panel->frame->screen_ptr->workspaces[j]->name);
			WMAddToPLArray(arr, value);
			WMReleasePropList(value);
		}
	}
	if (arr) {
		different |= insertAttribute(dict, winDic, AStartWorkspaces, arr, flags);
		WMReleasePropList(arr);
	}

	flags |= IS_BOOLEAN;

	/* Attributes... --> Window Attributes. Hint-carried flags are
	 * per-window state restored from _WINDOWMAKER_STATE, not chosen
	 * here, so an unrelated Save must not publish them. */
	for (i = 0; i < wlengthof(window_attribute); i++) {
		if (get_attr_flag(&wwin->hint_flags, window_attribute[i].attr))
			continue;

		value = (WMGetButtonSelected(panel->attrChk[i]) != 0) ? Yes : No;
		different |= insertAttribute(dict, winDic, pl_wattr[window_attribute[i].attr], value, flags);
	}

	/* Attributes... --> Advanced Options, same hint-carried rule */
	for (i = 0; i < wlengthof(advanced_option); i++) {
		if (get_attr_flag(&wwin->hint_flags, advanced_option[i].attr))
			continue;

		value = (WMGetButtonSelected(panel->moreChk[i]) != 0) ? Yes : No;
		different |= insertAttribute(dict, winDic, pl_wattr[advanced_option[i].attr], value, flags);
	}

	/* Attributes... --> Application Specific */
	if (wwin->main_window != XCB_NONE && wApplicationOf(wwin->main_window) != NULL) {
		for (i = 0; i < wlengthof(application_attr); i++) {
			value = (WMGetButtonSelected(panel->appChk[i]) != 0) ? Yes : No;
			different2 |= insertAttribute(dict, appDic, pl_wattr[application_attr[i].attr], value, flags);
		}
	}

	key2 = NULL;
	if (wwin->fake_group) {
		key2 = WMCreatePLString(wwin->fake_group->identifier);
	} else if (wwin->main_window != wwin->client_win) {
		WApplication *wapp = wApplicationOf(wwin->main_window);

		if (wapp) {
			buf2 = StrConcatDot(wapp->main_window_desc->wm_instance,
					      wapp->main_window_desc->wm_class);
			key2 = WMCreatePLString(buf2);
			wfree(buf2);
		}
	}

	if (key2 && !WMIsPropListEqualTo(key, key2)) {
		/* The application leader has its own WMWindowAttributes entry,
		 * shared by every window in the group. Only refresh the
		 * Application Specific booleans there, leaving Icon and any
		 * other keys untouched, so saving from a secondary window
		 * cannot clobber settings written from a sibling. */
		WMPropList *prev = WMGetFromPLDictionary(dict, key2);

		if (prev && WMIsPLDictionary(prev)) {
			for (i = 0; i < wlengthof(application_attr); i++)
				WMRemoveFromPLDictionary(prev, pl_wattr[application_attr[i].attr]);
			if (different2)
				WMMergePLDictionaries(prev, appDic, true);
			if (WMGetPropListItemCount(prev) == 0)
				WMRemoveFromPLDictionary(dict, key2);
		} else if (different2) {
			WMPutInPLDictionary(dict, key2, appDic);
		}
	} else {
		WMMergePLDictionaries(winDic, appDic, true);
		different |= different2;
	}
	if (key2)
		WMReleasePropList(key2);
	WMReleasePropList(appDic);

	WMRemoveFromPLDictionary(dict, key);
	if (different)
		WMPutInPLDictionary(dict, key, winDic);

	WMReleasePropList(key);
	WMReleasePropList(winDic);

	UpdateDomainFile(db);
	wWindowAttrsReload();

	/* Saved choices are domain values, including choices previously
	 * applied as per-window overrides. Hint-carried values were skipped. */
	for (i = 0; i < wlengthof(window_attribute); i++) {
		if (!get_attr_flag(&wwin->hint_flags, window_attribute[i].attr))
			set_attr_flag(&wwin->domain_flags, window_attribute[i].attr);
	}
	for (i = 0; i < wlengthof(advanced_option); i++) {
		if (!get_attr_flag(&wwin->hint_flags, advanced_option[i].attr))
			set_attr_flag(&wwin->domain_flags, advanced_option[i].attr);
	}

	/* clean up */
	WMPLSetCaseSensitive(false);
}

static void applySettingsInternal(WMWidget *button, void *client_data, bool per_window)
{
	InspectorPanel *panel = (InspectorPanel *) client_data;
	WWindow *wwin = panel->inspected;
	WApplication *wapp = wApplicationOf(wwin->main_window);
	int old_skip_window_list, old_omnipresent, old_no_bind_keys, old_no_bind_mouse;
	int old_always_user_icon;
	int hide_i = -1, mini_i = -1;
	const char *old_icon_file;
	int i, val;

	/* the gate below compares the field against the icon the app icon
	 * last SHOWED, not the domain: after Y over a saved X and Apply,
	 * typing X back must re-apply even though the domain still says X */
	old_icon_file = panel->applied_icon_file ? panel->applied_icon_file
			: wDefaultGetIconFile(wwin->wm_instance, wwin->wm_class, false);

	old_skip_window_list = WFLAGP(wwin, skip_window_list);
	old_omnipresent = WFLAGP(wwin, omnipresent);
	old_no_bind_keys = WFLAGP(wwin, no_bind_keys);
	old_no_bind_mouse = WFLAGP(wwin, no_bind_mouse);
	old_always_user_icon = WFLAGP(wwin, always_user_icon);

	showIconFor(WMWidgetScreen(button), panel, NULL, NULL, USE_TEXT_FIELD);

	/* Attributes... --> Window Attributes */
	for (i = 0; i < wlengthof(window_attribute); i++) {
		val = WMGetButtonSelected(panel->attrChk[i]) != 0;

		/* A checkbox that actually flips the flag makes the value
		 * user-chosen, dropping the hint-carried mark. */
		if (get_attr_flag(&wwin->user_flags, window_attribute[i].attr) != val) {
			clear_attr_flag(&wwin->hint_flags, window_attribute[i].attr);
			if (per_window)
				clear_attr_flag(&wwin->domain_flags, window_attribute[i].attr);
			if (window_attribute[i].attr == WATTR_NO_TITLEBAR)
				wwin->hint_flags.no_shadeable = 0;
		}

		if (val)
			set_attr_flag(&wwin->user_flags, window_attribute[i].attr);
		else
			clear_attr_flag(&wwin->user_flags, window_attribute[i].attr);

		set_attr_flag(&wwin->defined_user_flags, window_attribute[i].attr);

		if (window_attribute[i].attr == WATTR_AUTO_HIDE_ON_UNFOCUS)
			hide_i = i;
		else if (window_attribute[i].attr == WATTR_AUTO_MINIATURIZE_ON_UNFOCUS)
			mini_i = i;
	}

	/* hide wins over miniaturize (wWindowUnfocus), so keep the pair
	 * exclusive rather than letting the user set both */
	if (hide_i >= 0 && mini_i >= 0) {
		if (WFLAGP(wwin, auto_hide_on_unfocus)) {
			WSETPERWINDOWFLAG(wwin, auto_miniaturize_on_unfocus, 0);
			WMSetButtonSelected(panel->attrChk[mini_i], 0);
		} else if (WFLAGP(wwin, auto_miniaturize_on_unfocus)) {
			WSETPERWINDOWFLAG(wwin, auto_hide_on_unfocus, 0);
			WMSetButtonSelected(panel->attrChk[hide_i], 0);
		}
		WMSetButtonEnabled(panel->attrChk[mini_i], !WFLAGP(wwin, auto_hide_on_unfocus));
		WMSetButtonEnabled(panel->attrChk[hide_i], !WFLAGP(wwin, auto_miniaturize_on_unfocus));
	}

	/* Attributes... --> Advanced Options */
	for (i = 0; i < wlengthof(advanced_option); i++) {
		val = WMGetButtonSelected(panel->moreChk[i]) != 0;

		if (get_attr_flag(&wwin->user_flags, advanced_option[i].attr) != val) {
			clear_attr_flag(&wwin->hint_flags, advanced_option[i].attr);
			if (per_window)
				clear_attr_flag(&wwin->domain_flags, advanced_option[i].attr);
		}

		if (val)
			set_attr_flag(&wwin->user_flags, advanced_option[i].attr);
		else
			clear_attr_flag(&wwin->user_flags, advanced_option[i].attr);

		set_attr_flag(&wwin->defined_user_flags, advanced_option[i].attr);
	}

	WSETPERWINDOWFLAG(wwin, always_user_icon, WMGetButtonSelected(panel->alwChk));
	WSETPERWINDOWFLAG(wwin, auto_tab_group, WMGetButtonSelected(panel->autoTabGrpChk));

	if (WFLAGP(wwin, no_titlebar) && wwin->flags.shaded)
		wUnshadeWindow(wwin);

	WSETPERWINDOWFLAG(wwin, no_shadeable, WFLAGP(wwin, no_titlebar));

	/*
	 * Update the window level according to AlwaysOnTop/AlwaysOnBotton
	 * if the level did not change, ChangeStackingLevel will do nothing anyway
	 */
	if (WFLAGP(wwin, floating))
		ChangeStackingLevel(wwin->frame->core, WMFloatingLevel);
	else if (WFLAGP(wwin, sunken))
		ChangeStackingLevel(wwin->frame->core, WMSunkenLevel);
	else
		ChangeStackingLevel(wwin->frame->core, WMNormalLevel);

	/* The Omnipresent attribute checkbox now drives IS_OMNIPRESENT via
	 * WFLAGP, so drop any runtime WS_ALL mask so unchecking it here
	 * actually returns the window to a single workspace. Multi-bit
	 * memberships that are not WS_ALL are left intact. */
	if (wwin->frame && wwin->frame->ws_mask == WS_ALL)
		wwin->frame->ws_mask = WS_BIT(wwin->frame->workspace);

	if (WFLAGP(wwin, skip_window_list) != old_skip_window_list) {
		UpdateSwitchMenu(wwin->screen_ptr, wwin, WFLAGP(wwin, skip_window_list)?ACTION_REMOVE:ACTION_ADD);
	} else {
		if (WFLAGP(wwin, omnipresent) != old_omnipresent)
			WMPostNotificationName(WMNChangedState, wwin, "omnipresent");
	}

	if (WFLAGP(wwin, no_bind_keys) != old_no_bind_keys) {
		if (WFLAGP(wwin, no_bind_keys))
			wXUngrabKey(wdisp->conn, XCB_GRAB_ANY, XCB_MOD_MASK_ANY, wwin->frame->core->window);
		else
			wWindowSetKeyGrabs(wwin);
	}

	if (WFLAGP(wwin, no_bind_mouse) != old_no_bind_mouse)
		wWindowResetMouseGrabs(wwin);

	wwin->frame->flags.need_texture_change = 1;
	wWindowConfigureBorders(wwin);
	wFrameWindowPaint(wwin->frame);
	wNETWMUpdateActions(wwin, false);

	/* Can't apply emulate_appicon because it will probably cause problems. */
	if (wapp) {
		/* do application wide stuff */
		for (i = 0; i < wlengthof(application_attr); i++) {
			if (WMGetButtonSelected(panel->appChk[i]))
				set_attr_flag(&wapp->main_window_desc->user_flags, application_attr[i].attr);
			else
				clear_attr_flag(&wapp->main_window_desc->user_flags, application_attr[i].attr);

			set_attr_flag(&wapp->main_window_desc->defined_user_flags, application_attr[i].attr);
		}

		if (WFLAGP(wapp->main_window_desc, no_appicon))
			unpaint_app_icon(wapp);
		else
			paint_app_icon(wapp);

		char *file = WMGetTextFieldText(panel->fileText);
		if (file[0] == 0) {
			wfree(file);
			file = NULL;
		}

		/* If always_user_icon flag is set, but the user icon is not set
		 * we use client supplied icon and we unset the flag */
		if ((WFLAGP(wwin, always_user_icon) && (!file))) {
			/* Show the warning */
			char *buf;
			int len = 100;

			buf = wmalloc(len);
			snprintf(buf, len, _("Ignore client supplied icon is set, but icon filename textbox is empty. Using client supplied icon"));
			wMessageDialog(panel->frame->screen_ptr, _("Warning"), buf, _("OK"), NULL, NULL);
			wfree(buf);
			wfree(file);

			/* Change the flags */
			WSETPERWINDOWFLAG(wwin, always_user_icon, 0);
			WMSetButtonSelected(panel->alwChk, 0);
		}

		/* Only replace the icon when always_user_icon or the icon
		 * filename changed, so a bare Apply does not clobber a docked
		 * app icon with the client WM_HINTS pixmap. */
		if (WFLAGP(wwin, always_user_icon) != old_always_user_icon ||
		    (file && !old_icon_file) ||
		    (!file && old_icon_file) ||
		    (file && old_icon_file && strcmp(file, old_icon_file) != 0)) {
			if (!WFLAGP(wwin, always_user_icon)) {
				/* Change App Icon image, using the icon provided by the client */
				if (wapp->app_icon) {
					RImage *image = get_rimage_icon_from_wm_hints(wapp->app_icon->icon);

					if (image) {
						set_icon_image_from_image(wapp->app_icon->icon, image);
						update_icon_pixmap(wapp->app_icon->icon);
					} else {
						wIconUpdate(wapp->app_icon->icon);
					}
				}

				/* Change icon image if the app is minimized,
				 * using the icon provided by the client */
				if (wwin->icon) {
					RImage *image = get_rimage_icon_from_wm_hints(wwin->icon);

					if (image) {
						set_icon_image_from_image(wwin->icon, image);
						update_icon_pixmap(wwin->icon);
					} else {
						wIconUpdate(wwin->icon);
					}
				}
			} else {
				/* Change App Icon image */
				if (wapp->app_icon)
					wIconChangeImageFile(wapp->app_icon->icon, file);

				/* Change icon image if the app is minimized */
				if (wwin->icon)
					wIconChangeImageFile(wwin->icon, file);
			}

			if (panel->applied_icon_file)
				wfree(panel->applied_icon_file);
			panel->applied_icon_file = file ? wstrdup(file) : NULL;
		}

		if (file)
			wfree(file);
	}

	/* always_user_icon may have flipped above (and may have been
	 * reverted again by the empty-filename correction), refresh
	 * the titlebar icon from the settled flag value */
	wWindowRefreshTitlebarIcon(wwin);

	wNETFrameExtents(wwin);
}

static void applySettings(WMWidget *button, void *client_data)
{
	applySettingsInternal(button, client_data, true);
}

static void revertSettings(WMWidget *button, void *client_data)
{
	InspectorPanel *panel = (InspectorPanel *) client_data;
	WWindow *wwin = panel->inspected;
	WApplication *wapp = wApplicationOf(wwin->main_window);
	int i, workspace, level;
	char *wm_instance = NULL, *wm_class = NULL;
	uint64_t mask = 0;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) button;

	if (panel->instRb && WMGetButtonSelected(panel->instRb) != 0)
		wm_instance = wwin->wm_instance;
	else if (panel->clsRb && WMGetButtonSelected(panel->clsRb) != 0)
		wm_class = wwin->wm_class;
	else if (panel->bothRb && WMGetButtonSelected(panel->bothRb) != 0) {
		wm_instance = wwin->wm_instance;
		wm_class = wwin->wm_class;
	}

	memset(&wwin->defined_user_flags, 0, sizeof(WWindowAttributes));
	memset(&wwin->user_flags, 0, sizeof(WWindowAttributes));
	memset(&wwin->client_flags, 0, sizeof(WWindowAttributes));
	memset(&wwin->hint_flags, 0, sizeof(WWindowAttributes));

	wWindowSetupInitialAttributes(wwin, NULL, &level, &workspace);

	/* Attributes... --> Window Attributes */
	for (i = 0; i < wlengthof(window_attribute); i++) {
		int is_userdef, flag;

		is_userdef = get_attr_flag(&wwin->defined_user_flags, window_attribute[i].attr);
		if (is_userdef)
			flag = get_attr_flag(&wwin->user_flags, window_attribute[i].attr);
		else
			flag = get_attr_flag(&wwin->client_flags, window_attribute[i].attr);

		WMSetButtonSelected(panel->attrChk[i], flag);
	}

	/* Attributes... --> Advanced Options */
	for (i = 0; i < wlengthof(advanced_option); i++) {
		int is_userdef, flag;

		is_userdef = get_attr_flag(&wwin->defined_user_flags, advanced_option[i].attr);
		if (is_userdef)
			flag = get_attr_flag(&wwin->user_flags, advanced_option[i].attr);
		else
			flag = get_attr_flag(&wwin->client_flags, advanced_option[i].attr);

		WMSetButtonSelected(panel->moreChk[i], flag);
	}

	/* Attributes... --> Application Specific */
	if (panel->appFrm && wapp) {
		for (i = 0; i < wlengthof(application_attr); i++) {
			int is_userdef, flag = 0;

			is_userdef = get_attr_flag(&wapp->main_window_desc->defined_user_flags, application_attr[i].attr);
			if (is_userdef)
				flag = get_attr_flag(&wapp->main_window_desc->user_flags, application_attr[i].attr);
			else
				flag = get_attr_flag(&wapp->main_window_desc->client_flags, application_attr[i].attr);

			WMSetButtonSelected(panel->appChk[i], flag);
		}
	}
	WMSetButtonSelected(panel->alwChk, WFLAGP(wwin, always_user_icon));
	WMSetButtonSelected(panel->autoTabGrpChk, WFLAGP(wwin, auto_tab_group));

	showIconFor(WMWidgetScreen(panel->alwChk), panel, wm_instance, wm_class, REVERT_TO_DEFAULT);

	if (wDefaultGetStartWorkspaces(wwin->screen_ptr, wm_instance, wm_class, &mask))
		WMSetMultiSelectPopUpButtonSelectionMask(panel->wsP, mask);
	else
		WMSetMultiSelectPopUpButtonSelectionMask(panel->wsP, 0);

	/* must auto apply, so that there wno't be internal
	 * inconsistencies between the state in the flags and
	 * the actual state of the window */
	applySettingsInternal(panel->applyBtn, panel, false);
}

static void chooseIconCallback(WMWidget *self, void *clientData)
{
	char *file;
	InspectorPanel *panel = (InspectorPanel *) clientData;
	int result;

	panel->choosingIcon = 1;

	WMSetButtonEnabled(panel->browseIconBtn, false);

	result = wIconChooserDialog(panel->frame->screen_ptr, &file,
				    panel->inspected->wm_instance,
				    panel->inspected->wm_class, panel->frame);

	panel->choosingIcon = 0;

	if (!panel->destroyed) {	/* kluge */
		if (result) {
			WMSetTextFieldText(panel->fileText, file);
			showIconFor(WMWidgetScreen(self), panel, NULL, NULL, USE_TEXT_FIELD);
		}
		WMSetButtonEnabled(panel->browseIconBtn, true);
	} else {
		freeInspector(panel);
	}
	if (result)
		wfree(file);
}

static void textEditedObserver(void *observerData, WMNotification *notification)
{
	InspectorPanel *panel = (InspectorPanel *) observerData;
	long movement = (long)WMGetNotificationClientData(notification);

	/* Escape is handled by the panel-wide keyPressHandler, which the
	 * key dispatch also reaches when a text field holds focus */

	if (movement != WMReturnTextMovement)
		return;

	showIconFor(WMWidgetScreen(panel->win), panel, NULL, NULL, USE_TEXT_FIELD);
}

static void selectSpecification(WMWidget *bPtr, void *data)
{
	InspectorPanel *panel = (InspectorPanel *) data;
	char str[256];
	WWindow *wwin = panel->inspected;

	if (bPtr == panel->defaultRb && (wwin->wm_instance || wwin->wm_class))
		WMSetButtonEnabled(panel->applyBtn, false);
	else
		WMSetButtonEnabled(panel->applyBtn, true);

	snprintf(str, sizeof(str),
	         _("Inspecting  %s.%s"),
	         wwin->wm_instance ? wwin->wm_instance : "?",
	         wwin->wm_class ? wwin->wm_class : "?");

	wFrameWindowChangeTitle(panel->frame->frame, str);
}

static void selectWindow(WMWidget *bPtr, void *data)
{
	InspectorPanel *panel = (InspectorPanel *) data;
	WWindow *wwin = panel->inspected;
	WScreen *scr = wwin->screen_ptr;
	xcb_generic_event_t *event = NULL;
	WWindow *iwin;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) bPtr;

	if (wXGrabPointer(wdisp->conn, scr->root_win, true,
			 XCB_EVENT_MASK_BUTTON_PRESS, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE,
			 wdisp->cursor[WCUR_SELECT], XCB_CURRENT_TIME) != WXGrabSuccess) {
		wwarning("could not grab mouse pointer");
		return;
	}

	WMSetLabelText(panel->specLbl, _("Click in the window you wish to inspect."));
	WMMaskEvent(wdisp->conn, XCB_EVENT_MASK_BUTTON_PRESS, &event);
	wXUngrabPointer(wdisp->conn, XCB_CURRENT_TIME);

	iwin = wWindowFor(((xcb_button_press_event_t *)event)->child);
	free(event);
	if (iwin && !iwin->flags.internal_window && iwin != wwin && !iwin->flags.inspector_open) {
		iwin->flags.inspector_open = 1;
		iwin->inspector = createInspectorForWindow(iwin,
							   panel->frame->frame_x, panel->frame->frame_y, true);
		wCloseInspectorForWindow(wwin);
	} else {
		WMSetLabelText(panel->specLbl, spec_text);
	}
}

static InspectorPanel *createInspectorForWindow(WWindow *wwin, int xpos, int ypos, bool showSelectPanel)
{
	WScreen *scr = wwin->screen_ptr;
	InspectorPanel *panel;
	xcb_window_t parent;
	char *str = NULL, *tmp = NULL;
	int x, y, btn_width, frame_width;
	WMButton *selectedBtn = NULL;

	spec_text = _("The configuration will apply to all\n"
		      "windows that have their WM_CLASS\n"
		      "property set to the above selected\n" "name, when saved.");

	panel = wmalloc(sizeof(InspectorPanel));
	memset(panel, 0, sizeof(InspectorPanel));

	panel->destroyed = 0;
	panel->inspected = wwin;
	panel->nextPtr = panelList;
	panelList = panel;
	panel->win = WMCreateWindow(scr->wmscreen, "windowInspector");
	WMResizeWidget(panel->win, PWIDTH, PHEIGHT);

	/* Escape anywhere in the panel closes it */
	WMCreateEventHandler(WMWidgetView(panel->win), XCB_EVENT_MASK_KEY_PRESS, keyPressHandler, panel);

	/**** create common stuff ****/
	/* command buttons */
	btn_width = (PWIDTH - (2 * 15) - (2 * 10)) / 3;
	panel->saveBtn = WMCreateCommandButton(panel->win);
	WMSetButtonAction(panel->saveBtn, saveSettings, panel);
	WMMoveWidget(panel->saveBtn, (2 * (btn_width + 10)) + 15, PHEIGHT - 40);
	WMSetButtonText(panel->saveBtn, _("Save"));
	WMResizeWidget(panel->saveBtn, btn_width, 28);
	if (wdisp->flags.noupdates || !(wwin->wm_class || wwin->wm_instance))
		WMSetButtonEnabled(panel->saveBtn, false);

	panel->applyBtn = WMCreateCommandButton(panel->win);
	WMSetButtonAction(panel->applyBtn, applySettings, panel);
	WMMoveWidget(panel->applyBtn, btn_width + 10 + 15, PHEIGHT - 40);
	WMSetButtonText(panel->applyBtn, _("Apply"));
	WMResizeWidget(panel->applyBtn, btn_width, 28);

	panel->revertBtn = WMCreateCommandButton(panel->win);
	WMSetButtonAction(panel->revertBtn, revertSettings, panel);
	WMMoveWidget(panel->revertBtn, 15, PHEIGHT - 40);
	WMSetButtonText(panel->revertBtn, _("Reload"));
	WMResizeWidget(panel->revertBtn, btn_width, 28);

	/* page selection popup button */
	panel->pagePopUp = WMCreatePopUpButton(panel->win);
	WMSetPopUpButtonAction(panel->pagePopUp, changePage, panel);
	WMMoveWidget(panel->pagePopUp, 25, 15);
	WMResizeWidget(panel->pagePopUp, PWIDTH - 50, 20);

	WMAddPopUpButtonItem(panel->pagePopUp, _("Window Specification"));
	WMAddPopUpButtonItem(panel->pagePopUp, _("Window Attributes"));
	WMAddPopUpButtonItem(panel->pagePopUp, _("Advanced Options"));
	WMAddPopUpButtonItem(panel->pagePopUp, _("Icon and Initial Workspace"));
	WMAddPopUpButtonItem(panel->pagePopUp, _("Application Specific"));

	/**** window spec ****/
	frame_width = PWIDTH - (2 * 15);

	panel->specFrm = WMCreateFrame(panel->win);
	WMSetFrameTitle(panel->specFrm, _("Window Specification"));
	WMMoveWidget(panel->specFrm, 15, 50);
	WMResizeWidget(panel->specFrm, frame_width, 145);

	panel->defaultRb = WMCreateRadioButton(panel->specFrm);
	WMMoveWidget(panel->defaultRb, 10, 78);
	WMResizeWidget(panel->defaultRb, frame_width - (2 * 10), 20);
	WMSetButtonText(panel->defaultRb, _("Defaults for all windows"));
	WMSetButtonSelected(panel->defaultRb, false);
	WMSetButtonAction(panel->defaultRb, selectSpecification, panel);

	if (wwin->wm_class && wwin->wm_instance) {
		tmp = wstrconcat(wwin->wm_instance, ".");
		str = wstrconcat(tmp, wwin->wm_class);

		panel->bothRb = WMCreateRadioButton(panel->specFrm);
		WMMoveWidget(panel->bothRb, 10, 18);
		WMResizeWidget(panel->bothRb, frame_width - (2 * 10), 20);
		WMSetButtonText(panel->bothRb, str);
		wfree(tmp);
		wfree(str);
		WMGroupButtons(panel->defaultRb, panel->bothRb);

		if (!selectedBtn)
			selectedBtn = panel->bothRb;

		WMSetButtonAction(panel->bothRb, selectSpecification, panel);
	}

	if (wwin->wm_instance) {
		panel->instRb = WMCreateRadioButton(panel->specFrm);
		WMMoveWidget(panel->instRb, 10, 38);
		WMResizeWidget(panel->instRb, frame_width - (2 * 10), 20);
		WMSetButtonText(panel->instRb, wwin->wm_instance);
		WMGroupButtons(panel->defaultRb, panel->instRb);

		if (!selectedBtn)
			selectedBtn = panel->instRb;

		WMSetButtonAction(panel->instRb, selectSpecification, panel);
	}

	if (wwin->wm_class) {
		panel->clsRb = WMCreateRadioButton(panel->specFrm);
		WMMoveWidget(panel->clsRb, 10, 58);
		WMResizeWidget(panel->clsRb, frame_width - (2 * 10), 20);
		WMSetButtonText(panel->clsRb, wwin->wm_class);
		WMGroupButtons(panel->defaultRb, panel->clsRb);

		if (!selectedBtn)
			selectedBtn = panel->clsRb;

		WMSetButtonAction(panel->clsRb, selectSpecification, panel);
	}

	panel->selWinB = WMCreateCommandButton(panel->specFrm);
	WMMoveWidget(panel->selWinB, 20, 145 - 24 - 10);
	WMResizeWidget(panel->selWinB, frame_width - 2 * 10 - 20, 24);
	WMSetButtonText(panel->selWinB, _("Select window"));
	WMSetButtonAction(panel->selWinB, selectWindow, panel);

	/* Checkbox sits between specFrm (bottom y=210) and specLbl, as a
	 * direct child of the inspector window so it is not framed. */
	panel->autoTabGrpChk = WMCreateSwitchButton(panel->win);
	WMMoveWidget(panel->autoTabGrpChk, 15, 207);
	WMResizeWidget(panel->autoTabGrpChk, frame_width, 20);
	WMSetButtonText(panel->autoTabGrpChk, _("Auto tab group by class"));
	WMSetButtonSelected(panel->autoTabGrpChk, WFLAGP(wwin, auto_tab_group));
	WMSetBalloonTextForView(_("When a new window of this class is opened and a tab group\n"
				  "on the current workspace already contains a member of the\n"
				  "same class, attach the new window to that group automatically."),
				WMWidgetView(panel->autoTabGrpChk));

	panel->specLbl = WMCreateLabel(panel->win);
	WMMoveWidget(panel->specLbl, 15, 240);
	WMResizeWidget(panel->specLbl, frame_width, 85);
	WMSetLabelText(panel->specLbl, spec_text);
	WMSetLabelWraps(panel->specLbl, true);

	WMSetLabelTextAlignment(panel->specLbl, WALeft);

	/**** attributes ****/
	create_tab_window_attributes(wwin, panel, frame_width);
	create_tab_window_advanced(wwin, panel, frame_width);
	create_tab_icon_workspace(wwin, panel);
	create_tab_app_specific(wwin, panel, frame_width);

	/* if the window is a transient, don't let it have a miniaturize button */
	if (wwin->transient_for != XCB_NONE && wwin->transient_for != scr->root_win)
		WMSetButtonEnabled(panel->attrChk[3], false);
	else
		WMSetButtonEnabled(panel->attrChk[3], true);

	if (!wwin->wm_class && !wwin->wm_instance)
		WMSetPopUpButtonItemEnabled(panel->pagePopUp, 0, false);

	WMRealizeWidget(panel->win);

	WMMapSubwidgets(panel->win);
	WMMapSubwidgets(panel->specFrm);
	WMMapSubwidgets(panel->attrFrm);
	WMMapSubwidgets(panel->moreFrm);
	WMMapSubwidgets(panel->iconFrm);
	WMMapSubwidgets(panel->wsFrm);
	if (panel->appFrm)
		WMMapSubwidgets(panel->appFrm);

	if (showSelectPanel) {
		WMSetPopUpButtonSelectedItem(panel->pagePopUp, 0);
		changePage(panel->pagePopUp, panel);
	} else {
		WMSetPopUpButtonSelectedItem(panel->pagePopUp, 1);
		changePage(panel->pagePopUp, panel);
	}

	parent = xcb_generate_id(wdisp->conn);
	xcb_create_window(wdisp->conn, XCB_COPY_FROM_PARENT, parent, scr->root_win, 0, 0, PWIDTH, PHEIGHT, 0,
			  XCB_WINDOW_CLASS_COPY_FROM_PARENT, XCB_COPY_FROM_PARENT,
			  XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL, (const uint32_t[]){ 0, 0});
	panel->parent = parent;
	xcb_reparent_window(wdisp->conn, WMWidgetXID(panel->win), parent, 0, 0);

	WMMapWidget(panel->win);

	WXSetTransientFor(wdisp->conn, parent, wwin->client_win);

	if (xpos == UNDEFINED_POS) {
		x = wwin->frame_x + wwin->frame->core->width / 2;
		y = wwin->frame_y + wwin->frame->top_width * 2;
		if (y + PHEIGHT > scr->scr_height)
			y = scr->scr_height - PHEIGHT - 30;
		if (x + PWIDTH > scr->scr_width)
			x = scr->scr_width - PWIDTH;
	} else {
		x = xpos;
		y = ypos;
	}

	panel->frame = wManageInternalWindow(scr, parent, wwin->client_win, "Inspector", x, y, PWIDTH, PHEIGHT);

	if (!selectedBtn)
		selectedBtn = panel->defaultRb;

	WMSetButtonSelected(selectedBtn, true);
	selectSpecification(selectedBtn, panel);

	/* kluge to know who should get the key events */
	panel->frame->client_leader = WMWidgetXID(panel->win);

	panel->frame->client_flags.no_closable = 0;
	panel->frame->client_flags.no_close_button = 0;
	wWindowUpdateButtonImages(panel->frame);
	wFrameWindowShowButton(panel->frame->frame, WFF_CLOSE_BUTTON);
	panel->frame->frame->on_click_button[WTB_CLOSE] = destroyInspector;

	wWindowMap(panel->frame);

	showIconFor(WMWidgetScreen(panel->alwChk), panel, wwin->wm_instance, wwin->wm_class, REVERT_TO_DEFAULT);

	return panel;
}

void wShowInspectorForWindow(WWindow *wwin)
{
	if (wwin->flags.inspector_open)
		return;

	WMSetBalloonEnabled(wwin->screen_ptr->wmscreen, w_global.config->help_balloon);

	make_keys();
	wwin->flags.inspector_open = 1;
	wwin->inspector = createInspectorForWindow(wwin, UNDEFINED_POS, UNDEFINED_POS, false);
}

void wHideInspectorForWindow(WWindow *wwin)
{
	WWindow *pwin = wwin->inspector->frame;

	wWindowUnmap(pwin);
	pwin->flags.hidden = 1;

	wClientSetState(pwin, XCB_ICCCM_WM_STATE_ICONIC, XCB_NONE);
}

void wUnhideInspectorForWindow(WWindow *wwin)
{
	WWindow *pwin = wwin->inspector->frame;

	pwin->flags.hidden = 0;
	pwin->flags.mapped = 1;
	xcb_map_window(wdisp->conn, pwin->client_win);
	xcb_map_window(wdisp->conn, pwin->frame->core->window);
	wClientSetState(pwin, XCB_ICCCM_WM_STATE_NORMAL, XCB_NONE);
}

WWindow *wGetWindowOfInspectorForWindow(WWindow *wwin)
{
	if (!wwin->inspector)
		return NULL;

	assert(wwin->flags.inspector_open != 0);
	return wwin->inspector->frame;
}

void wCloseInspectorForWindow(WWindow *wwin)
{
	WWindow *pwin = wwin->inspector->frame;	/* the inspector window */

	(*pwin->frame->on_click_button[WTB_CLOSE]) (NULL, pwin, NULL);
}

/* AutoHideOnUnfocus and AutoMiniaturizeOnUnfocus are mutually exclusive
 * at the dispatch level (hide wins, see wWindowUnfocus), so grey out the
 * other checkbox when one is checked */
static void toggleAutoHideOnUnfocus(WMWidget *self, void *client_data)
{
	InspectorPanel *panel = (InspectorPanel *) client_data;
	int i, other = -1;

	for (i = 0; i < wlengthof(window_attribute); i++)
		if (window_attribute[i].attr == WATTR_AUTO_MINIATURIZE_ON_UNFOCUS)
			other = i;
	if (other < 0)
		return;
	if (WMGetButtonSelected(self))
		WMSetButtonSelected(panel->attrChk[other], 0);
	WMSetButtonEnabled(panel->attrChk[other], !WMGetButtonSelected(self));
}

static void toggleAutoMiniaturizeOnUnfocus(WMWidget *self, void *client_data)
{
	InspectorPanel *panel = (InspectorPanel *) client_data;
	int i, other = -1;

	for (i = 0; i < wlengthof(window_attribute); i++)
		if (window_attribute[i].attr == WATTR_AUTO_HIDE_ON_UNFOCUS)
			other = i;
	if (other < 0)
		return;
	if (WMGetButtonSelected(self))
		WMSetButtonSelected(panel->attrChk[other], 0);
	WMSetButtonEnabled(panel->attrChk[other], !WMGetButtonSelected(self));
}

static void create_tab_window_attributes(WWindow *wwin, InspectorPanel *panel, int frame_width)
{
	int i, j, tmp;
	int hide_i = -1, mini_i = -1;
	/* Indices into window_attribute[] laid out top-to-bottom, sorted by the
	 * translated caption so the page stays alphabetical. attrChk[] remains
	 * indexed by the original table position so the save/apply/revert loops
	 * keep matching attrChk[i] with window_attribute[i].attr. */
	int order[sizeof(window_attribute) / sizeof(window_attribute[0])];

	panel->attrFrm = WMCreateFrame(panel->win);
	WMSetFrameTitle(panel->attrFrm, _("Attributes"));
	WMMoveWidget(panel->attrFrm, 15, 45);
	/* Match moreFrm's height so both tab pages present the same frame
	 * size. window_attribute[] has 14 entries (the XKB NO_LANGUAGE_BUTTON
	 * is in advanced_option[], not here), so the XKB-tall frame just has
	 * more bottom margin. */
#ifdef XKB_BUTTON_HINT
	WMResizeWidget(panel->attrFrm, frame_width, 320);
#else
	WMResizeWidget(panel->attrFrm, frame_width, 300);
#endif

	for (i = 0; i < wlengthof(window_attribute); i++)
		order[i] = i;
	for (i = 1; i < wlengthof(window_attribute); i++) {
		for (j = i; j > 0; j--) {
			const char *a = _(window_attribute[order[j - 1]].caption);
			const char *b = _(window_attribute[order[j]].caption);

			if (strcoll(a, b) <= 0)
				break;
			tmp = order[j];
			order[j] = order[j - 1];
			order[j - 1] = tmp;
		}
	}

	for (i = 0; i < wlengthof(window_attribute); i++) {
		int idx = order[i];
		int y = 20 * (i + 1) - 4;
		int is_userdef, flag;

		is_userdef = get_attr_flag(&wwin->defined_user_flags, window_attribute[idx].attr);
		if (is_userdef)
			flag = get_attr_flag(&wwin->user_flags, window_attribute[idx].attr);
		else
			flag = get_attr_flag(&wwin->client_flags, window_attribute[idx].attr);

		panel->attrChk[idx] = WMCreateSwitchButton(panel->attrFrm);
		WMMoveWidget(panel->attrChk[idx], 10, y);
		WMResizeWidget(panel->attrChk[idx], frame_width - 15, 20);
		WMSetButtonSelected(panel->attrChk[idx], flag);
		WMSetButtonText(panel->attrChk[idx], _(window_attribute[idx].caption));
		WMSetBalloonTextForView(_(window_attribute[idx].description),
					WMWidgetView(panel->attrChk[idx]));
	}

	/* grey out the opposite box so both cannot be set together */
	for (i = 0; i < wlengthof(window_attribute); i++) {
		if (window_attribute[i].attr == WATTR_AUTO_HIDE_ON_UNFOCUS)
			hide_i = i;
		else if (window_attribute[i].attr == WATTR_AUTO_MINIATURIZE_ON_UNFOCUS)
			mini_i = i;
	}
	if (hide_i >= 0 && mini_i >= 0) {
		bool hide = WFLAGP(wwin, auto_hide_on_unfocus);
		bool mini = WFLAGP(wwin, auto_miniaturize_on_unfocus);

		if (hide && mini)
			WMSetButtonSelected(panel->attrChk[mini_i], 0);
		WMSetButtonEnabled(panel->attrChk[mini_i], !hide);
		WMSetButtonEnabled(panel->attrChk[hide_i], !mini);

		WMSetButtonAction(panel->attrChk[hide_i], toggleAutoHideOnUnfocus, panel);
		WMSetButtonAction(panel->attrChk[mini_i], toggleAutoMiniaturizeOnUnfocus, panel);
	}
}

static void create_tab_window_advanced(WWindow *wwin, InspectorPanel *panel, int frame_width)
{
	int i, j, tmp;
	/* Indices into advanced_option[] laid out top-to-bottom in the frame.
	 * Sorted by the translated caption so the inspector presents the
	 * Advanced entries in the user's locale collation order, independent
	 * of the source-code table layout. panel->moreChk[] remains indexed
	 * by the original advanced_option[] position so the save/apply/revert
	 * loops keep matching moreChk[i] with advanced_option[i].attr. */
	int order[sizeof(advanced_option) / sizeof(advanced_option[0])];

	panel->moreFrm = WMCreateFrame(panel->win);
	WMSetFrameTitle(panel->moreFrm, _("Advanced"));
	WMMoveWidget(panel->moreFrm, 15, 45);
#ifdef XKB_BUTTON_HINT
	WMResizeWidget(panel->moreFrm, frame_width, 320);
#else
	WMResizeWidget(panel->moreFrm, frame_width, 300);
#endif

	for (i = 0; i < wlengthof(advanced_option); i++)
		order[i] = i;
	/* Stable insertion sort by strcoll() of the translated caption. */
	for (i = 1; i < wlengthof(advanced_option); i++) {
		for (j = i; j > 0; j--) {
			const char *a = _(advanced_option[order[j - 1]].caption);
			const char *b = _(advanced_option[order[j]].caption);

			if (strcoll(a, b) <= 0)
				break;
			tmp = order[j];
			order[j] = order[j - 1];
			order[j - 1] = tmp;
		}
	}

	for (i = 0; i < wlengthof(advanced_option); i++) {
		int idx = order[i];
		int is_userdef, flag;

		is_userdef = get_attr_flag(&wwin->defined_user_flags, advanced_option[idx].attr);
		if (is_userdef)
			flag = get_attr_flag(&wwin->user_flags, advanced_option[idx].attr);
		else
			flag = get_attr_flag(&wwin->client_flags, advanced_option[idx].attr);

		panel->moreChk[idx] = WMCreateSwitchButton(panel->moreFrm);
		WMMoveWidget(panel->moreChk[idx], 10, 20 * (i + 1) - 4);
		WMResizeWidget(panel->moreChk[idx], frame_width - 15, 20);
		WMSetButtonSelected(panel->moreChk[idx], flag);
		WMSetButtonText(panel->moreChk[idx], _(advanced_option[idx].caption));

		WMSetBalloonTextForView(_(advanced_option[idx].description), WMWidgetView(panel->moreChk[idx]));
	}
}

static void create_tab_icon_workspace(WWindow *wwin, InspectorPanel *panel)
{
	const char *db_icon;
	WScreen *scr = wwin->screen_ptr;
	int i = 0;
	uint64_t mask = 0;

	/* miniwindow/workspace */
	panel->iconFrm = WMCreateFrame(panel->win);
	WMMoveWidget(panel->iconFrm, 15, 50);
	WMResizeWidget(panel->iconFrm, PWIDTH - (2 * 15), 170);
	WMSetFrameTitle(panel->iconFrm, _("Application Icon"));

	panel->iconLbl = WMCreateLabel(panel->iconFrm);
	WMMoveWidget(panel->iconLbl, PWIDTH - (2 * 15) - 22 - 64, 20);
	WMResizeWidget(panel->iconLbl, 64, 64);
	WMSetLabelRelief(panel->iconLbl, WRGroove);
	WMSetLabelImagePosition(panel->iconLbl, WIPImageOnly);

	panel->browseIconBtn = WMCreateCommandButton(panel->iconFrm);
	WMSetButtonAction(panel->browseIconBtn, chooseIconCallback, panel);
	WMMoveWidget(panel->browseIconBtn, 22, 32);
	WMResizeWidget(panel->browseIconBtn, 120, 26);
	WMSetButtonText(panel->browseIconBtn, _("Browse..."));

	panel->fileLbl = WMCreateLabel(panel->iconFrm);
	WMMoveWidget(panel->fileLbl, 20, 85);
	WMResizeWidget(panel->fileLbl, PWIDTH - (2 * 15) - (2 * 20), 14);
	WMSetLabelText(panel->fileLbl, _("Icon filename:"));

	panel->fileText = WMCreateTextField(panel->iconFrm);
	WMMoveWidget(panel->fileText, 20, 105);
	WMResizeWidget(panel->fileText, PWIDTH - (2 * 20) - (2 * 15), 20);
	db_icon = wDefaultGetIconFile(wwin->wm_instance, wwin->wm_class, false);
	WMSetTextFieldText(panel->fileText, db_icon);
	WMAddNotificationObserver(textEditedObserver, panel, WMTextDidEndEditingNotification, panel->fileText);
	WMSetViewNextResponder(WMWidgetView(panel->fileText), WMWidgetView(panel->win));

	panel->alwChk = WMCreateSwitchButton(panel->iconFrm);
	WMMoveWidget(panel->alwChk, 20, 130);
	WMResizeWidget(panel->alwChk, PWIDTH - (2 * 15) - (2 * 15), 30);
	WMSetButtonText(panel->alwChk, _("Ignore client supplied icon"));
	WMSetButtonSelected(panel->alwChk, WFLAGP(wwin, always_user_icon));

	panel->wsFrm = WMCreateFrame(panel->win);
	WMMoveWidget(panel->wsFrm, 15, 225);
	WMResizeWidget(panel->wsFrm, PWIDTH - (2 * 15), 70);
	WMSetFrameTitle(panel->wsFrm, _("Initial Workspaces"));

	WMSetBalloonTextForView(_("The workspace(s) to place the window when it's"
				  " first shown. An empty selection means no"
				  " opinion, use the normal rules."),
				WMWidgetView(panel->wsFrm));

	panel->wsP = WMCreateMultiSelectPopUpButton(panel->wsFrm);
	WMMoveWidget(panel->wsP, 20, 30);
	WMResizeWidget(panel->wsP, PWIDTH - (2 * 15) - (2 * 20), 20);
	WMSetMultiSelectPopUpButtonCaption(panel->wsP, _("Nowhere in particular"));

	for (i = 0; i < wwin->screen_ptr->workspace_count; i++)
		WMAddMultiSelectPopUpButtonItem(panel->wsP, scr->workspaces[i]->name);

	if (wDefaultGetStartWorkspaces(wwin->screen_ptr, wwin->wm_instance,
				       wwin->wm_class, &mask))
		WMSetMultiSelectPopUpButtonSelectionMask(panel->wsP, mask);
}

static void create_tab_app_specific(WWindow *wwin, InspectorPanel *panel, int frame_width)
{
	WScreen *scr = wwin->screen_ptr;
	int i, j, tmp;
	/* See create_tab_window_advanced for the rationale on sorting by
	 * the translated caption while keeping panel->appChk[] indexed by
	 * the original application_attr[] position. */
	int order[sizeof(application_attr) / sizeof(application_attr[0])];

	if (wwin->main_window != XCB_NONE) {
		WApplication *wapp = wApplicationOf(wwin->main_window);

		panel->appFrm = WMCreateFrame(panel->win);
		WMSetFrameTitle(panel->appFrm, _("Application Attributes"));
		WMMoveWidget(panel->appFrm, 15, 50);
		WMResizeWidget(panel->appFrm, frame_width, 240);

		for (i = 0; i < wlengthof(application_attr); i++)
			order[i] = i;
		for (i = 1; i < wlengthof(application_attr); i++) {
			for (j = i; j > 0; j--) {
				const char *a = _(application_attr[order[j - 1]].caption);
				const char *b = _(application_attr[order[j]].caption);

				if (strcoll(a, b) <= 0)
					break;
				tmp = order[j];
				order[j] = order[j - 1];
				order[j - 1] = tmp;
			}
		}

		for (i = 0; i < wlengthof(application_attr); i++) {
			int idx = order[i];
			int is_userdef, flag;

			is_userdef = get_attr_flag(&wapp->main_window_desc->defined_user_flags, application_attr[idx].attr);
			if (is_userdef)
				flag = get_attr_flag(&wapp->main_window_desc->user_flags, application_attr[idx].attr);
			else
				flag = get_attr_flag(&wapp->main_window_desc->client_flags, application_attr[idx].attr);

			panel->appChk[idx] = WMCreateSwitchButton(panel->appFrm);
			WMMoveWidget(panel->appChk[idx], 10, 20 * (i + 1));
			WMResizeWidget(panel->appChk[idx], 205, 20);
			WMSetButtonSelected(panel->appChk[idx], flag);
			WMSetButtonText(panel->appChk[idx], _(application_attr[idx].caption));

			WMSetBalloonTextForView(_(application_attr[idx].description),
			                        WMWidgetView(panel->appChk[idx]));
		}

		if (WFLAGP(wwin, emulate_appicon)) {
			WMSetButtonEnabled(panel->appChk[1], false);
			WMSetButtonEnabled(panel->moreChk[8], true);
		} else {
			WMSetButtonEnabled(panel->appChk[1], true);
			WMSetButtonEnabled(panel->moreChk[8], false);
		}
	} else {
		if ((wwin->transient_for != XCB_NONE && wwin->transient_for != scr->root_win)
		    || !wwin->wm_class || !wwin->wm_instance)
			tmp = false;
		else
			tmp = true;

		WMSetButtonEnabled(panel->moreChk[8], tmp);

		WMSetPopUpButtonItemEnabled(panel->pagePopUp, 4, false);
		panel->appFrm = NULL;
	}
}
