/* accent_picker.h - held-trigger and modifier accent picker
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

#ifndef WMACCENT_PICKER_H
#define WMACCENT_PICKER_H

#include "wconfig.h"
#include <stdbool.h>

#include <xcb/xcb.h>

struct _WScreen;
struct WWindow;

#ifdef KEEP_XKB_LOCK_STATUS

void wAccentPickerInit(struct _WScreen *scr);
void wAccentPickerDestroy(struct _WScreen *scr);
void wAccentPickerApplyConfig(struct _WScreen *scr);
void wAccentPickerLayoutChanged(struct _WScreen *scr);
bool wAccentPickerIsActive(struct _WScreen *scr);

void wAccentPickerOpen(struct _WScreen *scr, struct WWindow *wwin,
		      xcb_generic_event_t *event);
bool wAccentPickerHandleKeyPress(struct _WScreen *scr, xcb_key_press_event_t *e);
bool wAccentPickerHandleKeyRelease(struct _WScreen *scr, xcb_key_release_event_t *e);
bool wAccentPickerHandleButtonPress(struct _WScreen *scr, xcb_button_press_event_t *e);

#endif /* KEEP_XKB_LOCK_STATUS */

#endif /* WMACCENT_PICKER_H */
