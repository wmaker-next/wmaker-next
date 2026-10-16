/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2013-2026 Window Maker Team
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
#ifndef WMMISC_H_
#define WMMISC_H_

#include <xkbcommon/xkbcommon.h>

#include "defaults.h"
#include "keybind.h"
#include "appicon.h"

#define M_QUICK 1

bool wFetchName(xcb_connection_t *dpy, xcb_window_t win, char **winname);
bool wGetIconName(xcb_connection_t *dpy, xcb_window_t win, char **iconname);
bool UpdateDomainFile(WDDomain * domain);

void move_window(xcb_window_t win, int from_x, int from_y, int to_x, int to_y);
void slide_windows(xcb_window_t wins[], int n, int from_x, int from_y, int to_x, int to_y,
		   char stack_axis);
void ParseWindowName(WMPropList *value, char **winstance, char **wclass, const char *where);

static inline void slide_window(xcb_window_t win, int from_x, int from_y, int to_x, int to_y)
{
	slide_windows(&win, 1, from_x, from_y, to_x, to_y, 'x');
}

/* Helper is a 'wmsetbg' subprocess with sets the background for the current workspace */
bool start_bg_helper(WScreen *scr);
void SendHelperMessage(WScreen *scr, char type, int workspace, const char *msg);

char *ShrinkString(WMFont *font, const char *string, int width);
char *FindImage(const char *paths, const char *file);
char *ExpandOptions(WScreen * scr, const char *cmdline);
char *getselection(WScreen *scr);
char *getuserinput(WScreen *scr, const char *line, int *ptr, bool advanced);
void ExecuteInputCommand(WScreen *scr, const char *cmdline);
void ExecuteExitCommand(WScreen *scr, long quickmode);
char *GetShortcutString(const char *text);
bool GetCanonicalShortcutLabel(unsigned int modifiers, xkb_keysym_t ksym, char *buf, size_t bufsz);
char *GetShortcutLabelForKeycode(unsigned int modifiers, xcb_keycode_t keycode);
char *GetShortcutKey(WShortKey key);
char *EscapeWM_CLASS(const char *name, const char *class);
char *StrConcatDot(const char *a, const char *b);
char *GetCommandForWindow(xcb_window_t win);
WMPoint wGetCenter(WScreen *scr, int width, int height);
int WMStrEqual(const char *x, const char *y);
int WMPLGetBool(WMPropList *value);
xcb_visualtype_t *wVisualForId(WScreen *scr, xcb_visualid_t vid);
#endif
