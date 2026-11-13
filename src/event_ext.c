/* event_ext.c - X extension events: Shape, RandR, XSync alarm, XKB,
 *               MIT-SCREEN-SAVER
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2014-2026 Window Maker Team
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

#include <stdlib.h>

#include <xcb/xcb.h>
#ifdef USE_XSHAPE
# include <xcb/shape.h>
#endif
#ifdef USE_RANDR
# include <xcb/randr.h>
#endif
#ifdef USE_XSYNC
# include <xcb/sync.h>
#endif
#ifdef USE_XCB_SCREENSAVER
# include <xcb/screensaver.h>
#endif
#include <xcb/xkb.h>

#include "WindowMaker.h"
#include "window.h"
#include "framewin.h"
#include "screen.h"
#include "wmspec.h"
#include "event.h"
#include "wxkb.h"
#include "accent_picker.h"
#ifdef USE_RANDR
# include "randr.h"
#endif


#ifdef USE_XSHAPE
static void handleShapeNotify(xcb_generic_event_t *ev)
{
	const xcb_shape_notify_event_t *se = (const xcb_shape_notify_event_t *)ev;
	int shaped = se->shaped;
	xcb_generic_event_t *peek, *owned;
	WWindow *wwin;

	/* extension events carry no wire window, so the run is
	 * compared on the typed affected_window field, and only the
	 * bounding neighbours with the same shaped flag compress */
	while (WXEvqPeek(wdisp->conn, &peek)) {
		const xcb_shape_notify_event_t *ne =
			(const xcb_shape_notify_event_t *)peek;

		if ((peek->response_type & 0x7f) != (ev->response_type & 0x7f)
		    || ne->affected_window != se->affected_window
		    || ne->shape_kind != XCB_SHAPE_SK_BOUNDING
		    || ne->shaped != shaped)
			break;
		WXEvqNext(wdisp->conn, &owned);
		free(owned);
	}

	wwin = wWindowFor(se->affected_window);
	if (!wwin || se->shape_kind != XCB_SHAPE_SK_BOUNDING)
		return;

	if (!shaped && wwin->flags.shaped) {

		wwin->flags.shaped = 0;
		wWindowClearShape(wwin);

	} else if (shaped) {

		wwin->flags.shaped = 1;
		wWindowSetShape(wwin);
	}
}
#endif	/* USE_XSHAPE */

#ifdef KEEP_XKB_LOCK_STATUS
static void handleXkbIndicatorStateNotify(void)
{
	WWindow *wwin;
	WScreen *scr;
	int group;
	int i;

	for (i = 0; i < wdisp->screen_count; i++) {
		scr = wScreenWithNumber(i);
		wwin = scr->focused_window;
		if (wwin && wwin->flags.focused) {
			group = wXkbGetGroup();
			if (group >= 0 && wwin->frame->languagemode != group) {
				wwin->frame->last_languagemode = wwin->frame->languagemode;
				wwin->frame->languagemode = group;
				wWindowGetLanguageLabel(wwin->frame->languagemode, wwin->frame->language_label);
				wFrameWindowUpdateLanguageButton(wwin->frame);
			}
#ifdef XKB_BUTTON_HINT
			if (wwin->frame->titlebar) {
				wFrameWindowPaint(wwin->frame);
			}
#endif
		}
	}
}

static void cancelAccentPickers(void)
{
	int i;

	for (i = 0; i < wdisp->screen_count; i++)
		wAccentPickerCancel(wScreenWithNumber(i));
}
#endif	/* KEEP_XKB_LOCK_STATUS */

static void handleXkbEvent(xcb_generic_event_t *ev)
{
	/* XKB has a single event opcode and every sub-type shares the
	 * same {response_type, xkbType, sequence, time, deviceID}
	 * prefix, so any of the typed structs reads xkbType */
	const xcb_xkb_new_keyboard_notify_event_t *ke =
		(const xcb_xkb_new_keyboard_notify_event_t *)ev;
	int xkb_type = ke->xkbType;

	if (xkb_type == XCB_XKB_STATE_NOTIFY
	    && wXkbUpdateState((const xcb_xkb_state_notify_event_t *)ev)) {
		wRereadKeybindings();
	}

	if (xkb_type == XCB_XKB_NEW_KEYBOARD_NOTIFY
	    || xkb_type == XCB_XKB_MAP_NOTIFY) {
		/* drop picker sessions before the keymap reload, a
		 * session must not outlive its map and the reload is
		 * slow enough for a release poll to commit in between */
#ifdef KEEP_XKB_LOCK_STATUS
		cancelAccentPickers();
#endif
		wRefreshKeybindings();
	}
#ifdef KEEP_XKB_LOCK_STATUS
	else if (w_global.config->modelock
		 && (xkb_type == XCB_XKB_INDICATOR_STATE_NOTIFY
		     || xkb_type == XCB_XKB_STATE_NOTIFY)) {
		/* StateNotify is the common signal on group (layout)
		 * changes, IndicatorStateNotify on lock-LED toggles */
		handleXkbIndicatorStateNotify();
	}
#endif
}

#ifdef USE_RANDR
static void handleRandRScreenChange(xcb_generic_event_t *ev)
{
	const xcb_randr_screen_change_notify_event_t *re =
		(const xcb_randr_screen_change_notify_event_t *)ev;
	WScreen *scr = wScreenForRootWindow(re->root);

	if (scr)
		wRandRHandleNotify(scr);
}

static void handleRandRNotify(xcb_generic_event_t *ev)
{
	/* only CrtcChange and OutputChange carry the root that
	 * registered with RRSelectInput, every other subCode just
	 * rearms the per-screen debounce on screen 0 */
	const xcb_randr_notify_event_t *re = (const xcb_randr_notify_event_t *)ev;
	WScreen *scr;
	xcb_window_t root;

	if (re->subCode == XCB_RANDR_NOTIFY_CRTC_CHANGE)
		root = re->u.cc.window;
	else if (re->subCode == XCB_RANDR_NOTIFY_OUTPUT_CHANGE)
		root = re->u.oc.window;
	else
		root = wScreenWithNumber(0)->root_win;
	scr = wScreenForRootWindow(root);
	if (scr)
		wRandRHandleNotify(scr);
}
#endif	/* USE_RANDR */

#ifdef USE_XCB_SCREENSAVER
static void handleScreenSaverNotify(xcb_generic_event_t *ev)
{
	const xcb_screensaver_notify_event_t *se =
		(const xcb_screensaver_notify_event_t *)ev;

	/* STATE_ON means the server has activated its internal blank
	 * window or DPMS has powered the panel down, STATE_OFF means
	 * the user is back, STATE_CYCLE only arrives with the cycle
	 * mask which is not selected here, treat anything else as no
	 * change so a stray STATE_DISABLED never latches display_off */
	if (se->state == XCB_SCREENSAVER_STATE_ON)
		wDisplaySetOff(true, WDisplayPowerSourceScreenSaver);
	else if (se->state == XCB_SCREENSAVER_STATE_OFF)
		wDisplaySetOff(false, WDisplayPowerSourceScreenSaver);
}
#endif	/* USE_XCB_SCREENSAVER */

#ifdef USE_XSYNC
static void handleSyncAlarm(xcb_generic_event_t *ev)
{
	/* wNETWMSyncRequestHandleAlarm() reads .wire itself and is also
	 * called from the opaque-resize loop with a WXEvqTakeTyped result,
	 * so keep it on the cooked-event signature for now */
	wNETWMSyncRequestHandleAlarm(ev);
}
#endif

void wEventExtRegister(void)
{
#ifdef USE_XSHAPE
	if (wdisp->xext.shape.supported)
		wEventRegisterHandler(wdisp->xext.shape.event_base + XCB_SHAPE_NOTIFY,
				      handleShapeNotify);
#endif
#ifdef USE_XSYNC
	if (wdisp->xext.xsync.supported)
		wEventRegisterHandler(wdisp->xext.xsync.event_base + XCB_SYNC_ALARM_NOTIFY,
				      handleSyncAlarm);
#endif
#ifdef USE_RANDR
	if (wdisp->xext.randr.supported) {
		wEventRegisterHandler(wdisp->xext.randr.event_base + XCB_RANDR_SCREEN_CHANGE_NOTIFY,
				      handleRandRScreenChange);
		wEventRegisterHandler(wdisp->xext.randr.event_base + XCB_RANDR_NOTIFY,
				      handleRandRNotify);
	}
#endif
#ifdef USE_XCB_SCREENSAVER
	if (wdisp->xext.screensaver.supported)
		wEventRegisterHandler(wdisp->xext.screensaver.event_base
				      + XCB_SCREENSAVER_NOTIFY,
				      handleScreenSaverNotify);
#endif
	if (wdisp->xext.xkb.supported)
		wEventRegisterHandler(wdisp->xext.xkb.event_base, handleXkbEvent);
}
