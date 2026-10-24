/* cursor_hide.c - hide the pointer after keyboard or pointer inactivity
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

#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include <xcb/xfixes.h>
#ifdef USE_XCB_XINPUT
#include <xcb/xinput.h>
#endif

#include <WINGs/WINGsP.h>

#include "WindowMaker.h"
#include "screen.h"
#include "window.h"
#include "event.h"
#include "wxkb.h"
#include "cursor.h"
#include "cursor_hide.h"

#define CH_BUTTON_RECHECK_MS	1000

#ifdef USE_XCB_XINPUT
#define CH_MOTION_THRESHOLD_SQ	16
#define CH_MOTION_CHECK_MS	10
#endif

static void idleTimerCallback(void *data);
#ifdef USE_XCB_XINPUT
static void motionCheckCallback(void *data);
#endif

static struct {
	WMHandlerID idle_timer;
	WMHandlerID motion_timer;
	bool hidden;
	int hide_x;
	int hide_y;
	uint64_t last_activity_ms;
	bool applied_valid;
	bool applied_enabled;
	bool applied_keyboard;
	bool applied_idle;
	int applied_delay;
} cursor_hide;

static bool warned_unavailable;

static bool xinputAvailable(void)
{
#ifdef USE_XCB_XINPUT
	return wdisp->xext.xfixes.supported && wdisp->xext.xinput.supported;
#else
	return false;
#endif
}

#ifdef USE_XCB_XINPUT
static bool keyboardMode(void)
{
	const WConfig *config = w_global.config;

	return config && config->auto_hide_cursor &&
	    config->hide_cursor_after_keyboard;
}
#endif

static bool idleMode(void)
{
	const WConfig *config = w_global.config;

	return config && config->auto_hide_cursor &&
	    config->hide_cursor_after_idle;
}

static bool featureRequested(void)
{
	const WConfig *config = w_global.config;

	return config && config->auto_hide_cursor &&
	    (config->hide_cursor_after_keyboard ||
	     config->hide_cursor_after_idle);
}

static bool featureEnabled(void)
{
	return featureRequested() && xinputAvailable();
}

static int idleDelayMs(void)
{
	int delay = w_global.config->hide_cursor_idle_delay;

	if (delay < 1000)
		delay = 1000;
	else if (delay > 3600000)
		delay = 3600000;

	return delay;
}

static bool queryPointer(int *x, int *y, bool *button_held)
{
	int i;

	for (i = 0; i < wdisp->screen_count; i++) {
		WScreen *scr = wScreenWithNumber(i);

		if (scr && wCursorQueryPointer(scr, x, y, button_held))
			return true;
	}

	return false;
}

static uint64_t monotonicMs(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static void stopIdle(void)
{
	if (cursor_hide.idle_timer) {
		WMDeleteTimerHandler(cursor_hide.idle_timer);
		cursor_hide.idle_timer = NULL;
	}
}

static void armIdle(int delay_ms)
{
	stopIdle();
	cursor_hide.last_activity_ms = monotonicMs();
	if (delay_ms <= 0)
		return;

	cursor_hide.idle_timer = WMAddTimerHandler(delay_ms,
						   idleTimerCallback, NULL);
}

static void noteIdleActivity(void)
{
	cursor_hide.last_activity_ms = monotonicMs();
	if (!cursor_hide.idle_timer)
		cursor_hide.idle_timer =
		    WMAddTimerHandler(idleDelayMs(), idleTimerCallback, NULL);
}

static void stopMotionCheck(void)
{
	if (cursor_hide.motion_timer) {
		WMDeleteTimerHandler(cursor_hide.motion_timer);
		cursor_hide.motion_timer = NULL;
	}
}

static void setHidden(bool hidden)
{
	int i;

	if (!wdisp || !wdisp->xext.xfixes.supported)
		return;
	if (!hidden)
		stopMotionCheck();
	if (hidden == cursor_hide.hidden)
		return;

	for (i = 0; i < wdisp->screen_count; i++) {
		WScreen *scr = wScreenWithNumber(i);

		if (!scr)
			continue;
		if (hidden)
			xcb_xfixes_hide_cursor(wdisp->conn, scr->root_win);
		else
			xcb_xfixes_show_cursor(wdisp->conn, scr->root_win);
	}
	xcb_flush(wdisp->conn);
	cursor_hide.hidden = hidden;
}

static void captureHidePosition(void)
{
	int x, y;

	if (queryPointer(&x, &y, NULL)) {
		cursor_hide.hide_x = x;
		cursor_hide.hide_y = y;
	} else {
		cursor_hide.hide_x = 0;
		cursor_hide.hide_y = 0;
	}
}

#ifdef USE_XCB_XINPUT
static bool pointerMoved(void)
{
	int x, y;
	int dx, dy;

	if (!queryPointer(&x, &y, NULL))
		return true;

	dx = x - cursor_hide.hide_x;
	dy = y - cursor_hide.hide_y;

	return dx * dx + dy * dy >= CH_MOTION_THRESHOLD_SQ;
}
#endif

#ifdef USE_XCB_XINPUT
static void showCursor(void)
{
	if (cursor_hide.hidden)
		setHidden(false);
	if (idleMode())
		armIdle(idleDelayMs());
}
#endif

static void hideCursor(void)
{
	if (cursor_hide.hidden)
		return;

	captureHidePosition();
	setHidden(true);
}

#ifdef USE_XCB_XINPUT
static void handleKeyPress(const xcb_input_raw_key_press_event_t *event)
{
	xkb_keysym_t keysym;

	if (!keyboardMode())
		return;

	keysym = wXkbKeycodeToKeysym(event->detail, 0);
	if (keysym == XKB_KEY_NoSymbol || W_IsModifierKey(keysym))
		return;

	hideCursor();
}
#endif

#ifdef USE_XCB_XINPUT
static void motionCheckCallback(void *data)
{
	(void)data;

	cursor_hide.motion_timer = NULL;

	if (!featureEnabled() || !cursor_hide.hidden)
		return;
	if (!pointerMoved())
		return;

	setHidden(false);
	if (idleMode())
		armIdle(idleDelayMs());
}

static void handlePointerMotion(void)
{
	if (cursor_hide.hidden) {
		/* the raw event is queued before the master pointer state
		 * moves, so the position check runs just after this batch */
		if (!cursor_hide.motion_timer)
			cursor_hide.motion_timer =
			    WMAddTimerHandler(CH_MOTION_CHECK_MS,
					      motionCheckCallback, NULL);
		return;
	}

	if (idleMode())
		noteIdleActivity();
}
#endif

static void idleTimerCallback(void *data)
{
	bool button_held = false;
	uint64_t elapsed;
	int delay;

	(void)data;

	cursor_hide.idle_timer = NULL;

	if (!featureEnabled() || !idleMode())
		return;

	if (queryPointer(NULL, NULL, &button_held) && button_held) {
		armIdle(CH_BUTTON_RECHECK_MS);
		return;
	}

	delay = idleDelayMs();
	elapsed = monotonicMs() - cursor_hide.last_activity_ms;
	if (elapsed < (uint64_t)delay) {
		cursor_hide.idle_timer =
		    WMAddTimerHandler((int)((uint64_t)delay - elapsed),
				      idleTimerCallback, NULL);
		return;
	}

	hideCursor();
}

#ifdef USE_XCB_XINPUT
static void selectRawEvents(WScreen *scr, bool enable)
{
	struct {
		xcb_input_event_mask_t head;
		uint32_t bits;
	} mask;

	if (!scr || !wdisp->xext.xinput.supported)
		return;

	memset(&mask, 0, sizeof(mask));
	mask.head.deviceid = XCB_INPUT_DEVICE_ALL_MASTER;
	mask.head.mask_len = 1;
	if (enable) {
		mask.bits = XCB_INPUT_XI_EVENT_MASK_RAW_KEY_PRESS |
		    XCB_INPUT_XI_EVENT_MASK_RAW_KEY_RELEASE |
		    XCB_INPUT_XI_EVENT_MASK_RAW_BUTTON_PRESS |
		    XCB_INPUT_XI_EVENT_MASK_RAW_BUTTON_RELEASE |
		    XCB_INPUT_XI_EVENT_MASK_RAW_MOTION;
	}
	xcb_input_xi_select_events(wdisp->conn, scr->root_win, 1, &mask.head);
}

static void handleGenericEvent(xcb_generic_event_t *event)
{
	xcb_ge_generic_event_t *ge = (xcb_ge_generic_event_t *)event;

	if (!featureEnabled())
		return;
	if (ge->extension != wdisp->xext.xinput.major_opcode)
		return;

	switch (ge->event_type) {
	case XCB_INPUT_RAW_KEY_PRESS:
		handleKeyPress((const xcb_input_raw_key_press_event_t *)event);
		break;
	case XCB_INPUT_RAW_BUTTON_PRESS:
	case XCB_INPUT_RAW_BUTTON_RELEASE:
		showCursor();
		break;
	case XCB_INPUT_RAW_MOTION:
		handlePointerMotion();
		break;
	default:
		break;
	}
}
#else
static void selectRawEvents(WScreen *scr, bool enable)
{
	(void)scr;
	(void)enable;
}
#endif

void wCursorHideInit(void)
{
#ifdef USE_XCB_XINPUT
	wEventRegisterHandler(XCB_GE_GENERIC, handleGenericEvent);
#endif
}

void wCursorHideApplyConfig(WScreen *scr)
{
	const WConfig *config = w_global.config;
	bool requested;
	bool available;
	bool enabled;
	bool keyboard;
	bool idle;
	int delay;

	if (!config) {
		selectRawEvents(scr, false);
		return;
	}

	requested = featureRequested();
	available = xinputAvailable();
	enabled = requested && available;
	keyboard = enabled && config->hide_cursor_after_keyboard;
	idle = enabled && config->hide_cursor_after_idle;
	delay = idleDelayMs();

	selectRawEvents(scr, enabled);

	if (!requested) {
		warned_unavailable = false;
	} else if (!available && !warned_unavailable) {
		wwarning(_("auto hide cursor: XFixes 4.0 and XInput2 2.0 are required"));
		warned_unavailable = true;
	}

	if (!enabled) {
		stopIdle();
		stopMotionCheck();
		setHidden(false);
		cursor_hide.applied_valid = true;
		cursor_hide.applied_enabled = false;
		cursor_hide.applied_keyboard = false;
		cursor_hide.applied_idle = false;
		cursor_hide.applied_delay = delay;
		return;
	}

	if (cursor_hide.applied_valid && cursor_hide.applied_enabled &&
	    cursor_hide.applied_keyboard == keyboard &&
	    cursor_hide.applied_idle == idle &&
	    cursor_hide.applied_delay == delay)
		return;

	if (!cursor_hide.applied_valid || !cursor_hide.applied_enabled) {
		setHidden(false);
		if (idle)
			armIdle(delay);
		else
			stopIdle();
	} else if (idle) {
		if (!cursor_hide.applied_idle ||
		    cursor_hide.applied_delay != delay)
			armIdle(delay);
	} else if (cursor_hide.applied_idle) {
		stopIdle();
	}

	cursor_hide.applied_valid = true;
	cursor_hide.applied_enabled = true;
	cursor_hide.applied_keyboard = keyboard;
	cursor_hide.applied_idle = idle;
	cursor_hide.applied_delay = delay;
}

void wCursorHideReveal(void)
{
#ifdef USE_XCB_XINPUT
	showCursor();
#endif
}

void wCursorHideShutdown(void)
{
	stopIdle();
	stopMotionCheck();
	setHidden(false);
}
