/* accent_picker.c - held-trigger and modifier accent picker
 *
 *  Window Maker window manager
 *
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

#ifdef KEEP_XKB_LOCK_STATUS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <cairo.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <WINGs/WINGsP.h>
#include <WINGs/WUtil.h>

#include "WindowMaker.h"
#include "screen.h"
#include "window.h"
#include "framewin.h"
#include "keybind.h"
#include "defaults.h"
#include "event.h"
#include "wxkb.h"
#include "accent_picker.h"
#include "accent_data.h"
#include "xinerama.h"

#define AP_SETS_MAX 64
#define AP_USER_MAX 256
#define AP_USER_BASE_MAX 32
#define AP_POLL_MS 25
#define AP_POPUP_PAD 4
#define AP_CELL_PAD 8
#define AP_CELL_GAP 4

typedef struct W_AccentPicker {
	WScreen *scr;

	bool active;
	bool modifier_mode;
	bool held_trigger;
	bool cancelled;
	bool commit_pending;
	bool mouse_commit;
	bool keyboard_grabbed;
	bool pointer_grabbed;

	unsigned int modifier_mask;
	unsigned int installed_mask;
	unsigned int installed_numlock;
	unsigned int installed_scrolllock;
	xcb_keycode_t installed_min;
	xcb_keycode_t installed_max;

	xcb_keycode_t trigger_keycode;
	xcb_keycode_t base_keycode;
	bool base_down;
	char base_utf8[AP_USER_BASE_MAX];
	bool uppercase;

	xcb_keycode_t last_release_keycode;
	xcb_timestamp_t last_release_time;

	const APSet *sets[AP_SETS_MAX];
	size_t set_count;
	APMapping *user_maps;
	size_t user_count;

	char **variants;
	int variant_count;
	int selected;

	xcb_window_t focus_target;
	WMHandlerID poll_timer;

	/* popup anchor, snapshotted when the session starts so the popup
	 * cannot jump while variants are scrolled */
	bool anchor_on_focus;
	int anchor_x, anchor_y;
	int anchor_head;
	int focus_x, focus_y, focus_w, focus_h;

	WMWindow *popup;
	WMView *strip;
	WMFont *font;
	int cell_width;
	int cell_height;
	int columns;
	bool popup_mapped;
} W_AccentPicker;

static void rebuildSets(W_AccentPicker *ap);
static void rebuildUserMaps(W_AccentPicker *ap);
static void finish(W_AccentPicker *ap, bool commit);
static void hidePopup(W_AccentPicker *ap);
static void redrawStrip(W_AccentPicker *ap);

/* ---------------------------------------------------------------- *
 * popup
 * ---------------------------------------------------------------- */

static void paintStrip(W_AccentPicker *ap)
{
	WMScreen *wscr;
	cairo_t *cr;
	WMColor *selectedColor;
	int i, w, h;

	if (!ap->strip || !ap->font || !W_VIEW_REALIZED(ap->strip) ||
	    ap->variant_count <= 0)
		return;

	wscr = ap->scr->wmscreen;
	w = W_VIEW_WIDTH(ap->strip);
	h = W_VIEW_HEIGHT(ap->strip);
	cr = WCairoBeginForDrawable(wscr, W_VIEW_DRAWABLE(ap->strip));
	if (!cr)
		return;

	W_SetSourceColor(cr, ap->scr->gray);
	cairo_rectangle(cr, 0, 0, w, h);
	cairo_fill(cr);

	selectedColor = (ap->selected >= 0 && ap->selected < ap->variant_count)
		? W_SelectedBackColor(wscr) : NULL;

	for (i = 0; i < ap->variant_count; i++) {
		int row = ap->columns > 0 ? i / ap->columns : 0;
		int column = ap->columns > 0 ? i % ap->columns : 0;
		int x = AP_POPUP_PAD + column * (ap->cell_width + AP_CELL_GAP);
		int y = AP_POPUP_PAD + row * (ap->cell_height + AP_CELL_GAP);
		int tw = WMWidthOfString(ap->font, ap->variants[i],
					 strlen(ap->variants[i]));
		int th = WMFontHeight(ap->font);

		if (i == ap->selected) {
			W_SetSourceColor(cr, selectedColor);
			cairo_rectangle(cr, x, y, ap->cell_width, ap->cell_height);
			cairo_fill(cr);
		}
		W_DrawString(cr, ap->scr->black, ap->font,
			     x + (ap->cell_width - tw) / 2,
			     y + (ap->cell_height - th) / 2,
			     ap->variants[i], strlen(ap->variants[i]));
	}

	W_DrawRelief(cr, wscr, 0, 0, w, h, WRSunken);
	WCairoEnd(cr);
	if (selectedColor)
		WMReleaseColor(selectedColor);
}

static void redrawStrip(W_AccentPicker *ap)
{
	if (ap->strip && W_VIEW_MAPPED(ap->strip))
		paintStrip(ap);
}

static void stripEvent(xcb_generic_event_t *event, void *data)
{
	W_AccentPicker *ap = data;

	switch (event->response_type & 0x7f) {
	case XCB_EXPOSE:
		paintStrip(ap);
		break;
	case XCB_BUTTON_PRESS: {
		xcb_button_press_event_t *e = (xcb_button_press_event_t *)event;
		int x, y, column, row, index;
		int rowStride = ap->cell_height + AP_CELL_GAP;
		int columnStride = ap->cell_width + AP_CELL_GAP;

		if (e->detail != 1 || ap->columns <= 0 ||
		    rowStride <= 0 || columnStride <= 0)
			break;
		x = (int)e->event_x - AP_POPUP_PAD;
		y = (int)e->event_y - AP_POPUP_PAD;
		if (x < 0 || y < 0)
			break;
		column = x / columnStride;
		row = y / rowStride;
		if (column >= ap->columns ||
		    x % columnStride > ap->cell_width ||
		    y % rowStride > ap->cell_height)
			break;
		index = row * ap->columns + column;
		if (index < 0 || index >= ap->variant_count)
			break;
		ap->selected = index;
		if (ap->modifier_mode && wXkbModifierDown(ap->modifier_mask)) {
			/* keep the synthetic text out of the modifier grab:
			 * close now, inject when the modifier is released */
			ap->mouse_commit = true;
			hidePopup(ap);
			break;
		}
		finish(ap, true);
		break;
	}
	default:
		break;
	}
}

static void ensurePopup(W_AccentPicker *ap)
{
	WMScreen *wscr = ap->scr->wmscreen;

	if (ap->popup)
		return;

	ap->font = WMCreateFont(wscr, "Sans:pixelsize=16");
	if (!ap->font)
		ap->font = WMSystemFontOfSize(wscr, 16);
	if (!ap->font)
		ap->font = WMDefaultSystemFont(wscr);
	if (!ap->font)
		return;
	ap->cell_height = WMFontHeight(ap->font) + 10;
	ap->cell_width = 60;

	ap->popup = WMCreateWindowWithStyle(wscr, "accentPicker",
					    WMBorderlessWindowMask);
	WMSetWindowLevel(ap->popup, WMPopUpMenuWindowLevel);
	WMResizeWidget(ap->popup, 100, ap->cell_height + 2 * AP_POPUP_PAD);

	ap->strip = W_CreateView(WMWidgetView(ap->popup));
	if (ap->strip) {
		WMCreateEventHandler(ap->strip,
				     XCB_EVENT_MASK_EXPOSURE |
				     XCB_EVENT_MASK_BUTTON_PRESS,
				     stripEvent, ap);
		W_ResizeView(ap->strip, 100,
			     ap->cell_height + 2 * AP_POPUP_PAD);
		W_MoveView(ap->strip, 0, 0);
	}

	WMMapSubwidgets(ap->popup);
	WMRealizeWidget(ap->popup);
	WMWidgetView(ap->popup)->flags.worksWhenModal = 1;

	xcb_change_window_attributes(wdisp->conn, WMWidgetXID(ap->popup),
				     XCB_CW_SAVE_UNDER | XCB_CW_OVERRIDE_REDIRECT,
				     (const uint32_t[]){ true, true });
}

static void hidePopup(W_AccentPicker *ap)
{
	if (ap->popup && ap->popup_mapped) {
		WMUnmapWidget(ap->popup);
		ap->popup_mapped = false;
	}
}

static void placePopup(W_AccentPicker *ap)
{
	WArea ua;
	double scale;
	int head, maxText = 0, maxColumns, columns, rows;
	int w, h, x, y, i, deviceW, deviceH;
	int headWidth, headHeight, logicalWidth, logicalHeight;
	int availableWidth;

	head = ap->anchor_head;
	ua = wGetUsableAreaForHead(ap->scr, head, NULL, true);

	scale = WMScreenUIScale(ap->scr->wmscreen);
	headWidth = ua.x2 - ua.x1;
	headHeight = ua.y2 - ua.y1;
	logicalWidth = (int)(headWidth / scale);
	logicalHeight = (int)(headHeight / scale);
	if (logicalWidth < 1)
		logicalWidth = 1;
	if (logicalHeight < 1)
		logicalHeight = 1;

	for (i = 0; i < ap->variant_count; i++) {
		int tw = WMWidthOfString(ap->font, ap->variants[i],
					 strlen(ap->variants[i]));

		if (tw > maxText)
			maxText = tw;
	}
	ap->cell_width = maxText + 2 * AP_CELL_PAD;
	ap->cell_height = WMFontHeight(ap->font) + 10;

	availableWidth = logicalWidth - 2 * AP_POPUP_PAD;
	if (availableWidth < 1)
		availableWidth = 1;
	maxColumns = (availableWidth + AP_CELL_GAP) /
		(ap->cell_width + AP_CELL_GAP);
	if (maxColumns < 1)
		maxColumns = 1;
	if (maxColumns > ap->variant_count)
		maxColumns = ap->variant_count;

	columns = maxColumns;
	rows = (ap->variant_count + columns - 1) / columns;
	ap->columns = columns;

	w = columns * ap->cell_width + (columns - 1) * AP_CELL_GAP +
		2 * AP_POPUP_PAD;
	if (w > logicalWidth)
		w = logicalWidth;
	h = rows * ap->cell_height + (rows - 1) * AP_CELL_GAP +
		2 * AP_POPUP_PAD;
	if (h > logicalHeight)
		h = logicalHeight;

	deviceW = (int)(w * scale + 0.5);
	deviceH = (int)(h * scale + 0.5);

	if (ap->anchor_on_focus) {
		wplacepopupnearrect(ap->focus_x, ap->focus_y,
				    ap->focus_w, ap->focus_h,
				    deviceW, deviceH,
				    ua.x1, ua.y1, ua.x2 - ua.x1, ua.y2 - ua.y1,
				    &x, &y);
	} else {
		x = ap->anchor_x - deviceW / 2;
		if (ap->anchor_y - deviceH - 4 >= ua.y1)
			y = ap->anchor_y - deviceH - 4;
		else
			y = ap->anchor_y + 16;
	}
	if (x + deviceW > ua.x2)
		x = ua.x2 - deviceW;
	if (x < ua.x1)
		x = ua.x1;
	if (y + deviceH > ua.y2)
		y = ua.y2 - deviceH;
	if (y < ua.y1)
		y = ua.y1;

	WMResizeWidget(ap->popup, (unsigned int)w, (unsigned int)h);
	if (ap->strip)
		W_ResizeView(ap->strip, (unsigned int)w, (unsigned int)h);
	WMMoveWidget(ap->popup, x, y);
}

static void updatePopup(W_AccentPicker *ap)
{
	if (ap->variant_count <= 0) {
		hidePopup(ap);
		return;
	}
	ensurePopup(ap);
	if (!ap->popup)
		return;

	placePopup(ap);
	if (!ap->popup_mapped) {
		WMMapWidget(ap->popup);
		ap->popup_mapped = true;
	}
	redrawStrip(ap);
	xcb_configure_window(wdisp->conn, WMWidgetXID(ap->popup),
			     XCB_CONFIG_WINDOW_STACK_MODE,
			     (const uint32_t[]){ XCB_STACK_MODE_ABOVE });
	xcb_flush(wdisp->conn);
}

/* ---------------------------------------------------------------- *
 * session state
 * ---------------------------------------------------------------- */

static xcb_window_t currentFocus(void)
{
	xcb_get_input_focus_reply_t *rep;
	xcb_window_t focus;

	rep = xcb_get_input_focus_reply(wdisp->conn,
					xcb_get_input_focus(wdisp->conn), NULL);
	if (!rep)
		return XCB_NONE;
	focus = rep->focus;
	free(rep);
	return focus;
}

static bool keycodeDown(xcb_keycode_t kc)
{
	xcb_query_keymap_reply_t *rep;
	char keys[32];
	bool down;

	rep = xcb_query_keymap_reply(wdisp->conn,
				     xcb_query_keymap(wdisp->conn), NULL);
	if (!rep)
		return false;
	memcpy(keys, rep->keys, sizeof(keys));
	free(rep);
	down = (keys[kc >> 3] & (1 << (kc & 7))) != 0;
	return down;
}

static void clearVariants(W_AccentPicker *ap)
{
	if (ap->variants)
		apFreeVariants(ap->variants, (size_t)ap->variant_count);
	ap->variants = NULL;
	ap->variant_count = 0;
	ap->selected = -1;
}

static void clearBase(W_AccentPicker *ap)
{
	ap->base_keycode = 0;
	ap->base_utf8[0] = '\0';
	ap->uppercase = false;
	clearVariants(ap);
	hidePopup(ap);
}

static void stopTimer(W_AccentPicker *ap)
{
	if (ap->poll_timer) {
		WMDeleteTimerHandler(ap->poll_timer);
		ap->poll_timer = NULL;
	}
}

static void finish(W_AccentPicker *ap, bool commit)
{
	char text[AP_MAX_VARIANT_BYTES];
	bool inject;

	text[0] = '\0';
	if (!ap->active)
		return;

	inject = commit && !ap->cancelled;
	if (inject && ap->focus_target != XCB_NONE &&
	    currentFocus() != ap->focus_target)
		inject = false;

	if (inject) {
		if (ap->variant_count > 0 && ap->selected >= 0 &&
		    ap->selected < ap->variant_count) {
			snprintf(text, sizeof(text), "%s", ap->variants[ap->selected]);
		} else if (ap->base_utf8[0]) {
			snprintf(text, sizeof(text), "%s", ap->base_utf8);
		}
	}

	stopTimer(ap);
	hidePopup(ap);
	ap->active = false;
	ap->modifier_mode = false;
	ap->held_trigger = false;
	ap->cancelled = false;
	ap->commit_pending = false;
	ap->mouse_commit = false;
	ap->base_down = false;

	if (ap->keyboard_grabbed) {
		wXUngrabKeyboard(wdisp->conn, XCB_CURRENT_TIME);
		ap->keyboard_grabbed = false;
	}
	if (ap->pointer_grabbed) {
		wXUngrabPointer(wdisp->conn, XCB_CURRENT_TIME);
		ap->pointer_grabbed = false;
	}

	clearBase(ap);
	xcb_flush(wdisp->conn);

	if (text[0])
		wXkbTypeText(text);
}

static void modifierPoll(void *data)
{
	W_AccentPicker *ap = data;

	ap->poll_timer = NULL;
	if (!ap->active || !ap->modifier_mode)
		return;
	if (!wXkbModifierDown(ap->modifier_mask)) {
		finish(ap, !ap->cancelled);
		return;
	}
	if (ap->focus_target != XCB_NONE && currentFocus() != ap->focus_target) {
		finish(ap, false);
		return;
	}
	ap->poll_timer = WMAddTimerHandler(AP_POLL_MS, modifierPoll, ap);
}

static void snapshotAnchor(W_AccentPicker *ap)
{
	WWindow *wwin = ap->scr->focused_window;
	WMRect rect;
	WMPoint point;
	xcb_window_t child;
	unsigned int mask;
	bool have_focus = false, have_pointer, pointer_on_focus_head;
	int dummy, head_focus = -1, head_pointer;

	rect.pos.x = rect.pos.y = 0;
	rect.size.width = rect.size.height = 0;

	if (wwin && wwin->frame) {
		rect.pos.x = wwin->frame_x;
		rect.pos.y = wwin->frame_y;
		rect.size.width = wwin->frame->core->width;
		rect.size.height = wwin->frame->core->height;
		if (rect.size.width > 0 && rect.size.height > 0) {
			have_focus = true;
			head_focus = wGetHeadForRect(ap->scr, rect);
		}
	}

	have_pointer = wXQueryPointer(wdisp->conn, ap->scr->root_win, NULL, &child,
				      &point.x, &point.y, &dummy, &dummy, &mask);
	if (!have_pointer) {
		point.x = ap->scr->scr_width / 2;
		point.y = ap->scr->scr_height / 2;
	}
	head_pointer = wGetHeadForPoint(ap->scr, point);
	pointer_on_focus_head = have_pointer && head_pointer == head_focus;

	wpickpopupanchor(have_focus, rect.pos.x, rect.pos.y,
			 rect.size.width, rect.size.height, pointer_on_focus_head,
			 point.x, point.y, &ap->anchor_x, &ap->anchor_y);
	ap->anchor_on_focus = have_focus && !pointer_on_focus_head;
	ap->anchor_head = ap->anchor_on_focus ? head_focus : head_pointer;
	ap->focus_x = rect.pos.x;
	ap->focus_y = rect.pos.y;
	ap->focus_w = rect.size.width;
	ap->focus_h = rect.size.height;
}

static bool startSession(W_AccentPicker *ap, bool modifier_mode,
			 xcb_keycode_t trigger)
{
	if (ap->active)
		return false;

	ap->active = true;
	ap->modifier_mode = modifier_mode;
	ap->held_trigger = !modifier_mode;
	ap->cancelled = false;
	ap->commit_pending = false;
	ap->mouse_commit = false;
	ap->base_keycode = 0;
	ap->base_utf8[0] = '\0';
	ap->uppercase = false;
	ap->base_down = false;
	ap->last_release_keycode = 0;
	ap->last_release_time = 0;
	ap->selected = -1;
	clearVariants(ap);
	ap->trigger_keycode = trigger;
	ap->focus_target = currentFocus();
	snapshotAnchor(ap);

	if (ap->held_trigger) {
		if (wXGrabKeyboard(wdisp->conn, ap->scr->root_win, false,
				   XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
				   XCB_CURRENT_TIME) != WXGrabSuccess) {
			wwarning(_("accent picker: could not grab the keyboard"));
			ap->active = false;
			ap->held_trigger = false;
			return false;
		}
		ap->keyboard_grabbed = true;
	} else {
		ap->poll_timer = WMAddTimerHandler(AP_POLL_MS, modifierPoll, ap);
	}

	if (wXGrabPointer(wdisp->conn, ap->scr->root_win, true,
			  XCB_EVENT_MASK_BUTTON_PRESS,
			  XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
			  XCB_NONE, XCB_NONE, XCB_CURRENT_TIME) == WXGrabSuccess)
		ap->pointer_grabbed = true;

	return true;
}

static void setBase(W_AccentPicker *ap, xcb_keycode_t kc, const char *utf8,
		    bool shift)
{
	bool is_letter = false;
	char *norm;
	size_t count = 0;
	bool upper;
	int start;

	snprintf(ap->base_utf8, sizeof(ap->base_utf8), "%s", utf8);
	ap->base_keycode = kc;
	norm = apNormalizeBaseCopy(utf8, &is_letter);
	if (!norm)
		return;
	upper = is_letter && shift;
	ap->uppercase = upper;

	clearVariants(ap);
	ap->variants = apMergeVariants(ap->sets, ap->set_count,
				       ap->user_maps, ap->user_count,
				       norm, upper, &count);
	wfree(norm);
	ap->variant_count = (int)count;

	if (ap->variant_count <= 0) {
		ap->selected = -1;
		hidePopup(ap);
		return;
	}

	if (ap->held_trigger && shift)
		start = ap->variant_count - 1;
	else if (ap->held_trigger || upper)
		start = 0;
	else
		start = -1;
	ap->selected = start;
	updatePopup(ap);
}

static void cycleVariant(W_AccentPicker *ap, int delta)
{
	if (ap->variant_count <= 0)
		return;
	if (ap->selected < 0)
		ap->selected = delta > 0 ? 0 : ap->variant_count - 1;
	else
		ap->selected = (ap->selected + delta + ap->variant_count) %
			       ap->variant_count;
	redrawStrip(ap);
}

static bool keyUtf8(xcb_keycode_t kc, char *buf, size_t size)
{
	int n;
	uint32_t cp;

	if (wXkbKeycodeToKeysym(kc, 0) == XKB_KEY_NoSymbol)
		return false;
	n = wXkbStateUtf8(kc, buf, size);
	if (n <= 0 || buf[0] == '\0')
		return false;
	if (apUtf8Codepoints(buf) != 1)
		return false;
	/* printable only */
	cp = (unsigned char)buf[0];
	if (cp < 0x20 && cp != 0)
		return false;
	if (cp == 0x7f)
		return false;
	return apValidUtf8(buf);
}

static int navDelta(xkb_keysym_t ks)
{
	switch (ks) {
	case XKB_KEY_space:
	case XKB_KEY_Down:
	case XKB_KEY_Right:
	case XKB_KEY_j:
	case XKB_KEY_J:
		return 1;
	case XKB_KEY_Up:
	case XKB_KEY_Left:
	case XKB_KEY_k:
	case XKB_KEY_K:
		return -1;
	default:
		return 0;
	}
}

/* ---------------------------------------------------------------- *
 * config and grabs
 * ---------------------------------------------------------------- */

static void grabCombo(W_AccentPicker *ap, xcb_keycode_t kc, unsigned int mods)
{
	xcb_window_t root = ap->scr->root_win;

	wXGrabKey(wdisp->conn, kc, mods, root, false,
		  XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
	wXGrabKey(wdisp->conn, kc, mods | XCB_MOD_MASK_LOCK, root, false,
		  XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
#ifdef NUMLOCK_HACK
	wHackedGrabKey(kc, mods, root, false,
		       XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
#endif
}

static void ungrabCombo(W_AccentPicker *ap, xcb_keycode_t kc, unsigned int mods,
			unsigned int num, unsigned int scroll)
{
	unsigned int locks[4];
	int n = 0, i;
	xcb_window_t root = ap->scr->root_win;

	locks[n++] = 0;
	if (num)
		locks[n++] = num;
	if (scroll)
		locks[n++] = scroll;
	if (num && scroll)
		locks[n++] = num | scroll;

	for (i = 0; i < n; i++) {
		wXUngrabKey(wdisp->conn, kc, mods | locks[i], root);
		wXUngrabKey(wdisp->conn, kc, mods | XCB_MOD_MASK_LOCK | locks[i], root);
	}
}

static void removeGrabs(W_AccentPicker *ap)
{
	unsigned int kc;

	if (!ap->installed_mask)
		return;
	for (kc = ap->installed_min; kc <= ap->installed_max; kc++) {
		ungrabCombo(ap, (xcb_keycode_t)kc, ap->installed_mask,
			    ap->installed_numlock, ap->installed_scrolllock);
		ungrabCombo(ap, (xcb_keycode_t)kc,
			    ap->installed_mask | XCB_MOD_MASK_SHIFT,
			    ap->installed_numlock, ap->installed_scrolllock);
	}
	xcb_flush(wdisp->conn);
	ap->installed_mask = 0;
}

static void installGrabs(W_AccentPicker *ap, unsigned int mask)
{
	unsigned int kc;
	xcb_keycode_t min_kc, max_kc;

	removeGrabs(ap);
	if (!mask)
		return;

	min_kc = wXkbMinKeycode();
	max_kc = wXkbMaxKeycode();
	if (!min_kc || !max_kc || max_kc < min_kc)
		return;

	for (kc = min_kc; kc <= max_kc; kc++) {
		if (wXkbKeycodeIsModifier((xcb_keycode_t)kc))
			continue;
		grabCombo(ap, (xcb_keycode_t)kc, mask);
		grabCombo(ap, (xcb_keycode_t)kc, mask | XCB_MOD_MASK_SHIFT);
	}
	xcb_flush(wdisp->conn);

	ap->installed_mask = mask;
	ap->installed_min = min_kc;
	ap->installed_max = max_kc;
	ap->installed_numlock = wXkbNumLockMask();
	ap->installed_scrolllock = wXkbScrollLockMask();
}

static bool user_map_valid(const char *base, WMPropList *value)
{
	if (!base || !*base || !apValidUtf8(base) ||
	    strlen(base) >= AP_USER_BASE_MAX)
		return false;
	if (!value || !WMIsPLArray(value))
		return false;
	if (WMGetPropListItemCount(value) < 1 ||
	    WMGetPropListItemCount(value) > AP_MAX_VARIANTS)
		return false;
	return true;
}

static void freeUserMaps(W_AccentPicker *ap)
{
	size_t j;

	if (!ap->user_maps)
		return;
	for (j = 0; j < ap->user_count; j++) {
		size_t k;

		wfree((char *)ap->user_maps[j].base);
		for (k = 0; k < ap->user_maps[j].variant_count; k++)
			wfree((char *)ap->user_maps[j].variants[k]);
		wfree((void *)ap->user_maps[j].variants);
	}
	wfree(ap->user_maps);
	ap->user_maps = NULL;
	ap->user_count = 0;
}

static void rebuildUserMaps(W_AccentPicker *ap)
{
	WMPropList *dict, *variants, *keys;
	char *key;
	int n, i;
	size_t count = 0;

	freeUserMaps(ap);

	dict = w_global.domain.wmaker ? w_global.domain.wmaker->dictionary : NULL;
	variants = WMGetFromPLDictionaryByName(dict, "AccentPickerVariants");
	if (!variants || !WMIsPLDictionary(variants))
		return;

	keys = WMGetPLDictionaryKeys(variants);
	if (!keys)
		return;
	n = WMGetPropListItemCount(keys);
	if (n <= 0 || n > AP_USER_MAX) {
		WMReleasePropList(keys);
		return;
	}

	ap->user_maps = wmalloc((size_t)n * sizeof(APMapping));
	for (i = 0; i < n && count < (size_t)n; i++) {
		WMPropList *item = WMGetFromPLArray(keys, i);
		WMPropList *value;
		APMapping *map;
		char **list;
		int vn, v;
		bool is_letter = false;
		char *norm;
		size_t vc = 0;

		if (!item || !WMIsPLString(item))
			continue;
		key = WMGetFromPLString(item);
		value = WMGetFromPLDictionary(variants, item);
		if (!user_map_valid(key, value)) {
			wwarning(_("accent picker: ignoring invalid AccentPickerVariants entry"));
			continue;
		}

		vn = WMGetPropListItemCount(value);
		list = wmalloc((size_t)vn * sizeof(char *));
		for (v = 0; v < vn; v++) {
			WMPropList *elem = WMGetFromPLArray(value, v);

			if (!elem || !WMIsPLString(elem))
				continue;
			{
				const char *text = WMGetFromPLString(elem);

				if (!apValidUtf8(text) ||
				    strlen(text) >= AP_MAX_VARIANT_BYTES)
					continue;
				list[vc++] = wstrdup(text);
			}
		}
		if (vc == 0) {
			wfree(list);
			continue;
		}

		norm = apNormalizeBaseCopy(key, &is_letter);
		if (!norm) {
			size_t k;

			for (k = 0; k < vc; k++)
				wfree(list[k]);
			wfree(list);
			continue;
		}

		map = &ap->user_maps[count++];
		map->base = norm;
		map->variants = (const char *const *)list;
		map->variant_count = vc;
	}
	WMReleasePropList(keys);
	ap->user_count = count;
}

static void rebuildSets(W_AccentPicker *ap)
{
	const char *layouts[64];
	const char *const *tokens;
	size_t count, layout_count = 0;
	int active;
	bool unknown = false;
	int n, i;

	n = wXkbNumLayouts();
	if (n > (int)(sizeof(layouts) / sizeof(layouts[0])))
		n = (int)(sizeof(layouts) / sizeof(layouts[0]));
	for (i = 0; i < n; i++) {
		const char *name = wXkbLayoutName(i);

		if (name)
			layouts[layout_count++] = name;
	}
	active = wXkbGetGroup();

	tokens = NULL;
	count = 0;
	if (w_global.config) {
		tokens = (const char *const *)w_global.config->accent_picker_sets.items;
		count = w_global.config->accent_picker_sets.count;
	}

	ap->set_count = apResolveSets(tokens, count, layouts, layout_count,
				      active, ap->sets, AP_SETS_MAX, &unknown);
	if (unknown) {
		static bool warned;

		if (!warned) {
			wwarning(_("accent picker: ignoring an unknown character set name"));
			warned = true;
		}
	}
}

static unsigned int wantedMask(bool *invalid)
{
	const char *name = w_global.config ? w_global.config->accent_picker_modifier : NULL;
	unsigned int mask;
	int m;

	*invalid = false;
	if (!name || !*name || strcasecmp(name, "None") == 0)
		return 0;
	m = wXModifierFromKey(name);
	if (m <= 0) {
		*invalid = true;
		return 0;
	}
	mask = (unsigned int)m;
	if (mask == XCB_MOD_MASK_SHIFT || mask == XCB_MOD_MASK_CONTROL) {
		*invalid = true;
		return 0;
	}
	return mask;
}

void wAccentPickerApplyConfig(WScreen *scr)
{
	W_AccentPicker *ap;
	unsigned int mask, old;
	bool invalid;

	if (!scr)
		return;
	ap = scr->accentpicker;
	if (!ap)
		return;

	finish(ap, false);
	rebuildSets(ap);
	rebuildUserMaps(ap);

	mask = wantedMask(&invalid);
	if (invalid) {
		static bool warned;

		if (!warned) {
			wwarning(_("accent picker: invalid AccentPickerModifier, modifier mode disabled"));
			warned = true;
		}
	}

	if (mask && w_global.config) {
		const char *name = w_global.config->accent_picker_modifier;

		if (name && (strcasecmp(name, "Super") == 0 ||
			     strcasecmp(name, "Alt") == 0 ||
			     strcasecmp(name, "Meta") == 0)) {
			static bool warned;

			if (!warned) {
				wwarning(_("accent picker: %s may collide with application shortcuts"),
					 name);
				warned = true;
			}
		}
		{
			static bool overlap_warned;
			int i;

			if (!overlap_warned) {
				for (i = 0; i < WKBD_LAST; i++) {
					if (i == WKBD_ACCENTPICKER)
						continue;
					if (wKeyBindings[i].keycode &&
					    (wKeyBindings[i].modifier & mask)) {
						wwarning(_("accent picker: modifier overlaps an existing key binding"));
						overlap_warned = true;
						break;
					}
				}
			}
		}
	}

	old = ap->installed_mask;
	ap->modifier_mask = mask;
	if (old != mask)
		installGrabs(ap, mask);
}

void wAccentPickerLayoutChanged(WScreen *scr)
{
	W_AccentPicker *ap;
	unsigned int mask;

	if (!scr)
		return;
	ap = scr->accentpicker;
	if (!ap)
		return;

	finish(ap, false);
	rebuildSets(ap);

	mask = ap->installed_mask;
	if (mask)
		installGrabs(ap, mask);
}

/* ---------------------------------------------------------------- *
 * entry points
 * ---------------------------------------------------------------- */

void wAccentPickerInit(WScreen *scr)
{
	W_AccentPicker *ap;

	if (!scr || scr->accentpicker)
		return;
	ap = wmalloc(sizeof(*ap));
	ap->scr = scr;
	scr->accentpicker = ap;
}

void wAccentPickerDestroy(WScreen *scr)
{
	W_AccentPicker *ap;

	if (!scr)
		return;
	ap = scr->accentpicker;
	if (!ap)
		return;

	finish(ap, false);
	removeGrabs(ap);
	stopTimer(ap);
	if (ap->popup)
		WMDestroyWidget(ap->popup);
	if (ap->font)
		WMReleaseFont(ap->font);
	if (ap->variants)
		clearVariants(ap);
	freeUserMaps(ap);
	scr->accentpicker = NULL;
	wfree(ap);
}

bool wAccentPickerIsActive(WScreen *scr)
{
	return scr && scr->accentpicker && scr->accentpicker->active;
}

void wAccentPickerOpen(WScreen *scr, WWindow *wwin, xcb_generic_event_t *event)
{
	W_AccentPicker *ap;
	xcb_keycode_t trigger;

	(void)wwin;
	if (!scr)
		return;
	ap = scr->accentpicker;
	if (!ap || ap->active)
		return;
	trigger = wKeyBindings[WKBD_ACCENTPICKER].keycode;
	if (event)
		trigger = ((xcb_key_press_event_t *)event)->detail;
	if (trigger)
		startSession(ap, false, trigger);
}

bool wAccentPickerHandleKeyPress(WScreen *scr, xcb_key_press_event_t *e)
{
	W_AccentPicker *ap;
	WShortKey *binding;
	xkb_keysym_t ks;
	bool trigger_match = false;
	bool modifier_match = false;
	bool shift;
	char utf8[AP_USER_BASE_MAX];

	if (!scr)
		return false;
	ap = scr->accentpicker;
	if (!ap)
		return false;

	if (ap->active) {
		if (ap->focus_target != XCB_NONE &&
		    currentFocus() != ap->focus_target) {
			finish(ap, false);
			return true;
		}
		if (ap->mouse_commit)
			return true;
		if (ap->held_trigger && e->detail == ap->trigger_keycode)
			return true;

		ks = wXkbKeycodeToKeysym(e->detail, 0);
		if (ks == XKB_KEY_Escape) {
			ap->cancelled = true;
			finish(ap, false);
			return true;
		}
		if (ks == XKB_KEY_Return || ks == XKB_KEY_KP_Enter) {
			ap->commit_pending = true;
			return true;
		}
		if (ks == XKB_KEY_BackSpace) {
			if (!ap->base_utf8[0]) {
				if (!ap->held_trigger)
					finish(ap, false);
			} else {
				clearBase(ap);
			}
			return true;
		}

		if (navDelta(ks) != 0) {
			cycleVariant(ap, navDelta(ks));
			return true;
		}
		if (ks == XKB_KEY_Home) {
			if (ap->variant_count > 0) {
				ap->selected = 0;
				redrawStrip(ap);
			}
			return true;
		}
		if (ks == XKB_KEY_End) {
			if (ap->variant_count > 0) {
				ap->selected = ap->variant_count - 1;
				redrawStrip(ap);
			}
			return true;
		}

		if (keyUtf8(e->detail, utf8, sizeof(utf8))) {
			/* auto-repeat of the base key arrives as a fake
			 * release/press pair with the same timestamp */
			if (e->detail == ap->last_release_keycode &&
			    e->time == ap->last_release_time)
				return true;

			if (ap->held_trigger && e->detail == ap->base_keycode) {
				cycleVariant(ap, 1);
				return true;
			}
			if (!ap->held_trigger && e->detail == ap->base_keycode)
				return true;

			shift = (e->state & XCB_MOD_MASK_SHIFT) != 0 ||
				(e->state & XCB_MOD_MASK_LOCK) != 0;
			setBase(ap, e->detail, utf8, shift);
			return true;
		}
		return true;
	}

	if (scr->flags.mark_capture_mode != MARK_CAPTURE_IDLE)
		return false;

	binding = &wKeyBindings[WKBD_ACCENTPICKER];
	if (binding->keycode && binding->chain_length == 0 &&
	    e->detail == binding->keycode &&
	    (e->state & w_global.shortcut.modifiers_mask) == binding->modifier)
		trigger_match = true;

	if (ap->modifier_mask &&
	    (e->state & ap->modifier_mask) == ap->modifier_mask &&
	    !wXkbKeycodeIsModifier(e->detail))
		modifier_match = true;

	if (!trigger_match && !modifier_match)
		return false;

	if (w_global.shortcut.curpos != NULL)
		wCancelKeyChain(scr);

	if (trigger_match) {
		if (!startSession(ap, false, e->detail))
			return false;
		return true;
	}

	if (!keyUtf8(e->detail, utf8, sizeof(utf8)))
		return true;

	if (!startSession(ap, true, e->detail))
		return false;

	shift = (e->state & XCB_MOD_MASK_SHIFT) != 0 ||
		(e->state & XCB_MOD_MASK_LOCK) != 0;
	setBase(ap, e->detail, utf8, shift);
	return true;
}

bool wAccentPickerHandleKeyRelease(WScreen *scr, xcb_key_release_event_t *e)
{
	W_AccentPicker *ap;

	if (!scr)
		return false;
	ap = scr->accentpicker;
	if (!ap || !ap->active)
		return false;

	if (ap->focus_target != XCB_NONE && currentFocus() != ap->focus_target) {
		finish(ap, false);
		return true;
	}

	ap->last_release_keycode = e->detail;
	ap->last_release_time = e->time;

	if (ap->held_trigger && e->detail == ap->trigger_keycode) {
		/* server auto-repeat sends a fake release before the fake
		 * press. The key is still physically down in that case. */
		if (!keycodeDown(ap->trigger_keycode))
			finish(ap, !ap->cancelled);
		return true;
	}
	if (e->detail == ap->base_keycode)
		ap->base_down = false;

	return true;
}

bool wAccentPickerHandleButtonPress(WScreen *scr, xcb_button_press_event_t *e)
{
	W_AccentPicker *ap;

	(void)e;
	if (!scr)
		return false;
	ap = scr->accentpicker;
	if (!ap || !ap->active)
		return false;
	if (ap->mouse_commit)
		return true;

	/* Clicks on the popup are handled by the strip's WINGs view and
	 * never reach this dispatcher. Any other button press while the
	 * picker owns the pointer is outside the popup. */
	ap->cancelled = true;
	finish(ap, false);
	return true;
}

#endif /* KEEP_XKB_LOCK_STATUS */
