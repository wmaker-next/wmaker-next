/* KeyboardShortcuts.c- keyboard shortcut bindings
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
#include "keybind_descriptions.h"
#include <ctype.h>

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

typedef struct _Panel {
	WMBox *box;

	char *sectionName;

	char *description;

	CallbackRec callbacks;

	WMWidget *parent;

	WMLabel *actL;
	WMList *actLs;

	WMFrame *filterF;
	WMButton *filterB[2];

	WMFrame *shoF;
	WMTextField *shoT;
	WMButton *cleB;
	WMButton *defB;

	WMLabel *instructionsL;

	WMColor *white;
	WMColor *black;
	WMColor *gray;
	WMFont *font;

	bool capturing;
	bool assignedOnly;
	char **shortcuts;
	int *shownActions;
	int shownCount;
	int actionCount;
} _Panel;

#define ICON_FILE	"keyshortcuts"

#define ACTION_HEADER_Y		9
#define ACTION_HEADER_HEIGHT	20
#define ACTION_LIST_Y		31
#define ACTION_LIST_HEIGHT	191
#define SHORTCUT_Y		78

/*
 * List of user definable shortcut keys, from the table shared with the
 * window manager so the two cannot drift. See
 * ../src/keybind_descriptions.h. The list is sorted by title below.
 */
static struct keyOption {
	const char *key;
	const char *title;
} keyOptions[] = {
#define KEYBIND_OPTION(key, category, description) { key, description },
	WCFG_KEYBIND_DESCRIPTIONS(KEYBIND_OPTION)
#undef KEYBIND_OPTION
};



static int NumLockMask(W_Screen *scr)
{
	WXModifierSlots slots;
	void *state = W_XkbStateOf(scr);

	W_XkbModifierSlots(WMScreenDisplay(scr),
			   state ? xkb_state_get_keymap(state) : NULL, &slots);
	return slots.numlock >= 0 ? 1 << slots.numlock : 0;
}

/* Append the modifier prefix and key name to the keybuf */
static void build_key_combo(unsigned int xkstate, const char *keyname,
                             unsigned int numlock_mask, char keybuf[64])
{
	if (xkstate & XCB_MOD_MASK_CONTROL)
		wstrlcat(keybuf, "Control+", 64);
	if (xkstate & XCB_MOD_MASK_SHIFT)
		wstrlcat(keybuf, "Shift+", 64);
	if ((numlock_mask != XCB_MOD_MASK_1) && (xkstate & XCB_MOD_MASK_1))
		wstrlcat(keybuf, "Mod1+", 64);
	if ((numlock_mask != XCB_MOD_MASK_2) && (xkstate & XCB_MOD_MASK_2))
		wstrlcat(keybuf, "Mod2+", 64);
	if ((numlock_mask != XCB_MOD_MASK_3) && (xkstate & XCB_MOD_MASK_3))
		wstrlcat(keybuf, "Mod3+", 64);
	if ((numlock_mask != XCB_MOD_MASK_4) && (xkstate & XCB_MOD_MASK_4))
		wstrlcat(keybuf, "Mod4+", 64);
	if ((numlock_mask != XCB_MOD_MASK_5) && (xkstate & XCB_MOD_MASK_5))
		wstrlcat(keybuf, "Mod5+", 64);
	wstrlcat(keybuf, keyname, 64);
}

static void keychainTimeout(void *data)
{
	*(bool *)data = true;
}

static bool waitForKeychainEvent(xcb_connection_t *dpy, int timeout_ms)
{
	bool fired = false;
	WMHandlerID timer;

	timer = WMAddTimerHandler(timeout_ms, keychainTimeout, &fired);
	while (!fired && !WXEvqPending(dpy)) {
		wXFlush(dpy);
		W_HandleInputEvents(true, xcb_get_file_descriptor(dpy));
	}

	if (!fired)
		WMDeleteTimerHandler(timer);

	return WXEvqPending(dpy);
}

/*
 * Interactively capture a key shortcut or keychain,
 * function waits KeychainTimeoutDelay or 300 ms after
 * each key press for another key in the chain,
 * and returns the full key specification string.
 */
char *capture_shortcut(W_Screen *scr, bool *capturing, bool convert_case)
{
	xcb_generic_event_t *ev = NULL;
	xcb_connection_t *dpy = WMScreenDisplay(scr);
	xkb_keysym_t ksym, uksym;
	/* Large enough for several chained chords */
	char buffer[512];
	char keybuf[64];
	char *key;
	unsigned int numlock_mask;
	bool have_key = false;
	bool chain_mode;
	int timeout_ms;

	timeout_ms = GetIntegerForKey("KeychainTimeoutDelay");
	if (timeout_ms <= 0)
		timeout_ms = 300;

	buffer[0] = '\0';

	/* ---- Phase 1: capture the first key (blocking) ---- */
	while (*capturing) {
		wXAllowEvents(dpy, XCB_ALLOW_ASYNC_KEYBOARD, XCB_CURRENT_TIME);
		WMNextEvent(dpy, &ev);
		if (!ev)
			break;
		if ((ev->response_type & 0x7f) == XCB_KEY_PRESS && (((xcb_key_press_event_t *)ev)->detail) != 0) {
			numlock_mask = NumLockMask(scr);

			/* conditional mask check to get numeric keypad keys */
			ksym = W_XkbKeysymForKeycode(scr, (((xcb_key_press_event_t *)ev)->detail), (((xcb_key_press_event_t *)ev)->state) & numlock_mask?1:0);

			if (!W_IsModifierKey(ksym)) {
				char ksname[64];

				if (convert_case) {
					uksym = xkb_keysym_to_upper(ksym);
					xkb_keysym_get_name(uksym, ksname, sizeof(ksname));
				} else {
					xkb_keysym_get_name(ksym, ksname, sizeof(ksname));
				}
				key = ksname;

				keybuf[0] = '\0';
				build_key_combo((((xcb_key_press_event_t *)ev)->state), key, numlock_mask, keybuf);
				wstrlcat(buffer, keybuf, sizeof(buffer));
				have_key = true;
				break;
			}
		}
		WMHandleEvent(ev);
		free(ev);
		ev = NULL;
	}

	/* ---- Phase 2: collect additional chain keys with timeout ---- */
	chain_mode = (timeout_ms > 0);
	while (*capturing && chain_mode) {
		if (!WXEvqPending(dpy) && !waitForKeychainEvent(dpy, timeout_ms))
			break;  /* timeout: the chain is complete */

		free(ev);
		ev = NULL;
		wXAllowEvents(dpy, XCB_ALLOW_ASYNC_KEYBOARD, XCB_CURRENT_TIME);
		WMNextEvent(dpy, &ev);
		if (!ev)
			break;
		if ((ev->response_type & 0x7f) == XCB_KEY_PRESS && (((xcb_key_press_event_t *)ev)->detail) != 0) {
			numlock_mask = NumLockMask(scr);
			ksym = W_XkbKeysymForKeycode(scr, (((xcb_key_press_event_t *)ev)->detail),
			                            (((xcb_key_press_event_t *)ev)->state) & numlock_mask ? 1 : 0);

			if (!W_IsModifierKey(ksym)) {
				char ksname[64];

				if (convert_case) {
					uksym = xkb_keysym_to_upper(ksym);
					xkb_keysym_get_name(uksym, ksname, sizeof(ksname));
				} else {
					xkb_keysym_get_name(ksym, ksname, sizeof(ksname));
				}
				key = ksname;

				keybuf[0] = '\0';
				build_key_combo((((xcb_key_press_event_t *)ev)->state), key, numlock_mask, keybuf);
				wstrlcat(buffer, " ", sizeof(buffer));
				wstrlcat(buffer, keybuf, sizeof(buffer));
			}
		} else {
			WMHandleEvent(ev);
		}
		free(ev);
		ev = NULL;
	}

	/* WMNextEvent hands us the popped event, so release the last one */
	free(ev);

	if (!have_key || !*capturing)
		return NULL;

	*capturing = 0;
	return wstrdup(buffer);
}

/*
 * check if the keystr entered is already set to another action
 * if found it returns the position in the keyOptions
 */
static int isKeySet(_Panel *panel, char *keystr)
{
	int i;
	char *str;

	for (i = 0; i < panel->actionCount; i++) {
		str = NULL;
		if (panel->shortcuts[i]) {
			str = wtrimspace(panel->shortcuts[i]);
			if (strlen(str) == 0) {
				wfree(str);
				str = NULL;
			}
		}
		if (str) {
			if (strcmp(keystr, str) == 0) {
				wfree(str);
				return i;
			}
			wfree(str);
		}
	}

	return -1;
}

static int actionForRow(_Panel *panel, int row)
{
	if (row < 0 || row >= panel->shownCount)
		return -1;

	return panel->shownActions[row];
}

static void rebuildActionList(_Panel *panel)
{
	int selected = -1;
	int selectRow = -1;
	int row;
	int i;

	row = WMGetListSelectedItemRow(panel->actLs);
	if (row >= 0)
		selected = actionForRow(panel, row);

	WMClearList(panel->actLs);
	panel->shownCount = 0;

	for (i = 0; i < panel->actionCount; i++) {
		if (panel->assignedOnly && !panel->shortcuts[i])
			continue;

		panel->shownActions[panel->shownCount] = i;
		WMAddListItem(panel->actLs, _(keyOptions[i].title));
		if (i == selected)
			selectRow = panel->shownCount;
		panel->shownCount++;
	}

	if (selectRow >= 0)
		WMSelectListItem(panel->actLs, selectRow);

	WMRedisplayWidget(panel->actLs);
}

static void filterClick(WMWidget *w, void *data)
{
	_Panel *panel = (_Panel *) data;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) w;

	panel->assignedOnly = WMGetButtonSelected(panel->filterB[1]);
	rebuildActionList(panel);

	if (WMGetListSelectedItemRow(panel->actLs) < 0)
		WMSetTextFieldText(panel->shoT, NULL);
}

static void captureClick(WMWidget * w, void *data)
{
	_Panel *panel = (_Panel *) data;
	xcb_connection_t *dpy = WMScreenDisplay(WMWidgetScreen(panel->parent));
	char *shortcut;

	if (!panel->capturing) {
		panel->capturing = 1;
		WMSetButtonText(w, _("Cancel"));
		WMSetLabelText(panel->instructionsL,
			       _("Press the desired shortcut key(s) or click Cancel to stop capturing."));
		wXGrabKeyboard(dpy, WMWidgetXID(panel->parent), true, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_CURRENT_TIME);
		shortcut = capture_shortcut(WMWidgetScreen(panel->parent), &panel->capturing, 1);
		if (shortcut) {
			int key_idx = -1;
			int row = WMGetListSelectedItemRow(panel->actLs);
			int action = actionForRow(panel, row);

			key_idx = isKeySet(panel, shortcut);
			if (key_idx >= 0 && (key_idx != action)) {
				char *msg;

				msg = wstrconcat(_("Key shortcut already in use by the "), _(keyOptions[key_idx].title));
				WMRunAlertPanel(WMWidgetScreen(w), GetWindow(),
						_("Error"),
						msg,
						_("OK"), NULL, NULL);
				wfree(msg);
				wfree(shortcut);
			} else {
				WMSetTextFieldText(panel->shoT, shortcut);
				if (action >= 0) {
					if (panel->shortcuts[action])
						wfree(panel->shortcuts[action]);
					panel->shortcuts[action] = shortcut;

					WMRedisplayWidget(panel->actLs);
				} else {
					wfree(shortcut);
				}
			}
		}
	}
	panel->capturing = 0;
	WMSetButtonText(w, _("Capture"));
	WMSetLabelText(panel->instructionsL, _("Click on Capture to interactively define the shortcut key(s)."));
	wXUngrabKeyboard(dpy, XCB_CURRENT_TIME);
}

static void clearShortcut(WMWidget * w, void *data)
{
	_Panel *panel = (_Panel *) data;
	int row = WMGetListSelectedItemRow(panel->actLs);
	int action = actionForRow(panel, row);

	/* Parameter not used, but tell the compiler that it is ok */
	(void) w;

	/* Cancel any ongoing capture so the keychain loop is unblocked */
	panel->capturing = 0;

	WMSetTextFieldText(panel->shoT, NULL);

	if (action >= 0) {
		if (panel->shortcuts[action])
			wfree(panel->shortcuts[action]);
		panel->shortcuts[action] = NULL;
		rebuildActionList(panel);
	}
}

static void typedKeys(void *observerData, WMNotification * notification)
{
	_Panel *panel = (_Panel *) observerData;
	int row = WMGetListSelectedItemRow(panel->actLs);
	int action = actionForRow(panel, row);

	/* Parameter not used, but tell the compiler that it is ok */
	(void) notification;

	if (action < 0)
		return;

	if (panel->shortcuts[action])
		wfree(panel->shortcuts[action]);
	panel->shortcuts[action] = WMGetTextFieldText(panel->shoT);
	if (strlen(panel->shortcuts[action]) == 0) {
		wfree(panel->shortcuts[action]);
		panel->shortcuts[action] = NULL;
	}

	if (panel->assignedOnly && !panel->shortcuts[action])
		rebuildActionList(panel);
	else
		WMRedisplayWidget(panel->actLs);
}

static void listClick(WMWidget * w, void *data)
{
	_Panel *panel = (_Panel *) data;
	int row = WMGetListSelectedItemRow(w);
	int action = actionForRow(panel, row);

	if (action >= 0)
		WMSetTextFieldText(panel->shoT, panel->shortcuts[action]);
}

static void showData(_Panel * panel)
{
	char *str;
	int i;

	for (i = 0; i < panel->actionCount; i++) {

		str = GetStringForKey(keyOptions[i].key);
		if (panel->shortcuts[i])
			wfree(panel->shortcuts[i]);
		if (str)
			panel->shortcuts[i] = wtrimspace(str);
		else
			panel->shortcuts[i] = NULL;

		if (panel->shortcuts[i] &&
		    (strcasecmp(panel->shortcuts[i], "none") == 0 || strlen(panel->shortcuts[i]) == 0)) {
			wfree(panel->shortcuts[i]);
			panel->shortcuts[i] = NULL;
		}
	}
	rebuildActionList(panel);
}

static void paintItem(WMList * lPtr, int index, xcb_drawable_t d, char *text, int state, WMRect * rect)
{
	int width, height, x, y;
	_Panel *panel = (_Panel *) WMGetHangedData(lPtr);
	WMScreen *scr = WMWidgetScreen(lPtr);
	WMColor *backColor = (state & WLDSSelected) ? WMGetListSelectionColor(lPtr) : panel->gray;

	width = rect->size.width;
	height = rect->size.height;
	x = rect->pos.x;
	y = rect->pos.y;

	WMPaintColorSwatch(backColor, d, x, y, width, height);

	if (panel->shortcuts[panel->shownActions[index]]) {
		WMPixmap *pix = WMGetSystemPixmap(scr, WSICheckMark);
		WMSize size = WMGetPixmapSize(pix);

		WMDrawPixmap(pix, d, x + (20 - size.width) / 2, (height - size.height) / 2 + y);
		WMReleasePixmap(pix);
	}

	WMDrawString(scr, d, panel->black, panel->font, x + 20, y, text, strlen(text));
}

static int cmpKeyOptions(const void *v1, const void *v2)
{
	int rc;
	const struct keyOption *opt1 = (struct keyOption *)v1;
	const struct keyOption *opt2 = (struct keyOption *)v2;

	if ((rc = strncmp(opt1->title, opt2->title, 20)) < 0)
		return -1;
	else if (rc > 0)
		return 1;
	return 0;
}

static int centeredFrameY(WMScreen *scr, int headerY, int headerHeight)
{
	WMFont *font;
	int offset;

	font = WMDefaultSystemFont(scr);
	offset = (headerHeight - WMFontHeight(font) + 1) / 2;
	WMReleaseFont(font);

	if (offset < 0)
		offset = 0;

	return headerY + offset;
}


static void createPanel(Panel * p)
{
	_Panel *panel = (_Panel *) p;
	WMScreen *scr = WMWidgetScreen(panel->parent);
	WMColor *color;
	WMFont *boldFont;

	panel->capturing = 0;

	panel->white = WMWhiteColor(scr);
	panel->black = WMBlackColor(scr);
	panel->gray = WMGrayColor(scr);
	panel->font = WMSystemFontOfSize(scr, 12);

	panel->box = WMCreateBox(panel->parent);
	WMSetViewExpandsToParent(WMWidgetView(panel->box), 2, 2, 2, 2);

	boldFont = WMBoldSystemFontOfSize(scr, 12);

	/* **************** Actions **************** */
	panel->actL = WMCreateLabel(panel->box);
	WMResizeWidget(panel->actL, 314, ACTION_HEADER_HEIGHT);
	WMMoveWidget(panel->actL, 9, ACTION_HEADER_Y);
	WMSetLabelFont(panel->actL, boldFont);
	WMSetLabelText(panel->actL, _("Actions"));
	WMSetLabelRelief(panel->actL, WRSunken);
	WMSetLabelTextAlignment(panel->actL, WACenter);
	color = WMDarkGrayColor(scr);
	WMSetWidgetBackgroundColor(panel->actL, color);
	WMReleaseColor(color);
	WMSetLabelTextColor(panel->actL, panel->white);

	panel->actLs = WMCreateList(panel->box);
	WMResizeWidget(panel->actLs, 314, ACTION_LIST_HEIGHT);
	WMMoveWidget(panel->actLs, 9, ACTION_LIST_Y);
	WMSetListUserDrawProc(panel->actLs, paintItem);
	WMHangData(panel->actLs, panel);

	qsort(keyOptions, wlengthof(keyOptions), sizeof(keyOptions[0]), cmpKeyOptions);
	WMSetListAction(panel->actLs, listClick, panel);

	panel->actionCount = wlengthof(keyOptions);
	panel->shortcuts = wmalloc(sizeof(char *) * panel->actionCount);
	panel->shownActions = wmalloc(sizeof(int) * panel->actionCount);

    /***************** Actions List Filter *****************/

	panel->filterF = WMCreateFrame(panel->box);
	WMResizeWidget(panel->filterF, 178, 62);
	WMMoveWidget(panel->filterF, 333,
		     centeredFrameY(scr, ACTION_HEADER_Y, ACTION_HEADER_HEIGHT));
	WMSetFrameTitle(panel->filterF, _("List Mode"));

	panel->filterB[0] = WMCreateRadioButton(panel->filterF);
	WMResizeWidget(panel->filterB[0], 160, 20);
	WMMoveWidget(panel->filterB[0], 9, 17);
	WMSetButtonText(panel->filterB[0], _("All Actions"));
	WMSetButtonAction(panel->filterB[0], filterClick, panel);

	panel->filterB[1] = WMCreateRadioButton(panel->filterF);
	WMResizeWidget(panel->filterB[1], 160, 20);
	WMMoveWidget(panel->filterB[1], 9, 37);
	WMSetButtonText(panel->filterB[1], _("Assigned Actions"));
	WMSetButtonAction(panel->filterB[1], filterClick, panel);

	WMGroupButtons(panel->filterB[0], panel->filterB[1]);
	WMSetButtonSelected(panel->filterB[0], true);

	WMMapSubwidgets(panel->filterF);

    /***************** Shortcut ****************/

	panel->shoF = WMCreateFrame(panel->box);
	WMResizeWidget(panel->shoF, 178, ACTION_LIST_Y + ACTION_LIST_HEIGHT - SHORTCUT_Y);
	WMMoveWidget(panel->shoF, 333, SHORTCUT_Y);
	WMSetFrameTitle(panel->shoF, _("Shortcut"));

	panel->shoT = WMCreateTextField(panel->shoF);
	WMResizeWidget(panel->shoT, 160, 20);
	WMMoveWidget(panel->shoT, 9, 22);
	WMAddNotificationObserver(typedKeys, panel, WMTextDidChangeNotification, panel->shoT);

	panel->cleB = WMCreateCommandButton(panel->shoF);
	WMResizeWidget(panel->cleB, 75, 24);
	WMMoveWidget(panel->cleB, 9, 50);
	WMSetButtonText(panel->cleB, _("Clear"));
	WMSetButtonAction(panel->cleB, clearShortcut, panel);

	panel->defB = WMCreateCommandButton(panel->shoF);
	WMResizeWidget(panel->defB, 75, 24);
	WMMoveWidget(panel->defB, 94, 50);
	WMSetButtonText(panel->defB, _("Capture"));
	WMSetButtonAction(panel->defB, captureClick, panel);

	panel->instructionsL = WMCreateLabel(panel->shoF);
	WMResizeWidget(panel->instructionsL, 160, 56);
	WMMoveWidget(panel->instructionsL, 9, 80);
	WMSetLabelTextAlignment(panel->instructionsL, WACenter);
	WMSetLabelWraps(panel->instructionsL, true);
	WMSetLabelText(panel->instructionsL, _("Click on Capture to interactively define the shortcut key."));

	WMMapSubwidgets(panel->shoF);

	WMReleaseFont(boldFont);

	WMRealizeWidget(panel->box);
	WMMapSubwidgets(panel->box);

	showData(panel);
}

static bool storeData(_Panel *panel)
{
	int i;
	char *str;

	for (i = 0; i < panel->actionCount; i++) {
		str = NULL;
		if (panel->shortcuts[i]) {
			str = wtrimspace(panel->shortcuts[i]);
			if (strlen(str) == 0) {
				wfree(str);
				str = NULL;
			}
		}
		if (str) {
			SetStringForKey(str, keyOptions[i].key);
			wfree(str);
		} else {
			SetStringForKey("None", keyOptions[i].key);
		}
	}

	return true;
}

Panel *InitKeyboardShortcuts(WMWidget *parent)
{
	_Panel *panel;

	panel = wmalloc(sizeof(_Panel));

	panel->sectionName = _("Keyboard Shortcut Preferences");

	panel->description = _("Change the keyboard shortcuts for actions such\n"
			       "as changing workspaces and opening menus.");

	panel->parent = parent;

	panel->callbacks.createWidgets = createPanel;
	panel->callbacks.updateDomain = storeData;
	panel->callbacks.undoChanges = showData;

	AddSection(panel, ICON_FILE);

	return panel;
}
