/* WPrefs.h- general definitions
 *
 *  WPrefs - Window Maker Preferences Program
 *
 *  Copyright (c) 1998-2003 Alfredo K. Kojima
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

#ifndef WPREFS_H_
#define WPREFS_H_

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <signal.h>


#include <wraster.h>

#include <WINGs/WINGs.h>
#include <WINGs/WINGsP.h>
#include <WINGs/WIPC.h>

/* Needed for HAVE_LIBINTL_H and EXTENDED_WINDOWSHORTCUT */
#include "../src/wconfig.h"

/* Shared WindowMaker defaults schema, enum-string tables and the
 * matching value constants (WKF_*, SPEED_*, WB_*, ...). */
#include "config_schema_public.h"
#include "config_values.h"

/*
 * The macro 'wlengthof' should be used as much as possible, this macro
 * is reserved for the cases where wlengthof cannot be used because the
 * static_assert. Typical symptom is this compiler error (gcc):
 *
 *   error: braced-group within expression allowed only inside a function
 *
 * which appears when you try to create an array whose size is determined using
 * wlengthof on some other array.
 */
#define wlengthof_nocheck(array)  \
	(sizeof(array) / sizeof(array[0]))


/****/

extern char *NOptionValueChanged;

typedef struct _Panel Panel;

typedef struct {
    unsigned flags;		       /* reserved for WPrefs.c Don't access it */

    void (*createWidgets)(Panel*);     /* called when showing for first time */
    void (*updateDomain)(Panel*);      /* save the changes to the dictionary */
    void (*undoChanges)(Panel*);       /* reset values to those in the dictionary */
    void (*prepareForClose)(Panel*);   /* called when exiting WPrefs */
    void (*showPanel)(Panel*);	       /* called when entering the panel */
    void (*hidePanel)(Panel*);	       /* called when exiting the panel */
} CallbackRec;


/* all Panels must start with the following layout */
typedef struct PanelRec {
    WMBox *box;

    char *sectionName;		       /* section name to display in titlebar */

    char *description;

    CallbackRec callbacks;
} PanelRec;


/* ---[ Wprefs.c ] ------------------------------------------------------- */

void AddSection(Panel *panel, const char *iconFile);

char *LocateImage(const char *name);

void SetButtonAlphaImage(WMScreen *scr, WMButton *bPtr, const char *file);

void SetButtonImageFromFile(WMScreen *scr, WMButton *btn, const char *file, bool alt);

/* Loads `file' into `icon_normal'. If `icon_greyed' is not NULL,
 * combine `icon_normal' with some grey and then optionally with image
 * `xis', and store it in `icon_greyed' (typically to produce a
 * greyed-out, red-crossed version of `icon_normal') */
void CreateImages(WMScreen *scr, RContext *rc, RImage *xis, const char *file,
		WMPixmap **icon_normal, WMPixmap **icon_greyed);

WMWindow *GetWindow(void);

/* Release the screen, widgets, fonts, domains and WINGs state before exit. */
void WPrefsShutdown(void);

/* manipulate the dictionary for the WindowMaker domain */

WMPropList *GetObjectForKey(const char *defaultName);

void SetObjectForKey(WMPropList *object, const char *defaultName);

void RemoveObjectForKey(const char *defaultName);

char *GetStringForKey(const char *defaultName);

int GetIntegerForKey(const char *defaultName);

bool GetBoolForKey(const char *defaultName);

int GetSpeedForKey(const char *defaultName);

int GetEnumForKey(const char *defaultName, int fallback);

bool GetDefaultBoolForKey(const char *defaultName);

int GetDefaultIntegerForKey(const char *defaultName);

void SetIntegerForKey(int value, const char *defaultName);

void SetStringForKey(const char *value, const char *defaultName);

void SetBoolForKey(bool value, const char *defaultName);

void SetSpeedForKey(int speed, const char *defaultName);

void SetEnumForKey(int value, const char *defaultName);

/* manipulate the dictionary for the WMSound domain (Sound.c panel) */

WMPropList *GetSoundObjectForKey(const char *defaultName);

void SetSoundObjectForKey(WMPropList *object, const char *defaultName);

/* enum-backed single-select popup helper for the {db_value, label} tables
 * expanded from WPREFS_TABLE_* macros */
typedef struct {
	const char *db_value;
	const char *label;
} wm_enum_choice;

WMPopUpButton *wm_enum_popup_new(WMWidget *parent,
				 const wm_enum_choice *table, int n);

void wm_enum_popup_select(WMPopUpButton *popup, const wm_enum_choice *table,
			  int n, const char *value, const char *key_name);

void wm_enum_popup_store(WMPopUpButton *popup, const wm_enum_choice *table,
			 int n, const char *key_name);

bool GetWMGlobalBoolForKey(const char *defaultName, bool fallback);

void SetWMGlobalBoolForKey(bool value, const char *defaultName);


/* ---[ KeyboardShortcuts.c ] -------------------------------------------- */

char *capture_shortcut(W_Screen *scr, bool *capturing, bool convert_case);


/* ---[ double.c ] ------------------------------------------------------- */
typedef struct W_DoubleTest DoubleTest;

DoubleTest *CreateDoubleTest(WMWidget *parent, const char *text);


/* ---[ main.c ] --------------------------------------------------------- */
void AddDeadChildHandler(pid_t pid, void (*handler)(void*), void *data);


/* ---[ xmodifier.c ] ---------------------------------------------------- */
int ModifierFromKey(W_Screen *scr, const char *key);


/* ---[ Panel Initializers ]---------------------------------------------- */

void Initialize(WMScreen *scr);

/* in alphabetical order - in case you'd want to add one */
Panel *InitAppearance(WMWidget *parent);
Panel *InitConfigurations(WMWidget *parent);
Panel *InitDocks(WMWidget *parent);
Panel *InitExpert(WMWidget *parent);
Panel *InitFocus(WMWidget *parent);
Panel *InitFontSimple(WMWidget *parent);
Panel *InitHotCornerShortcuts(WMWidget *parent);
Panel *InitIcons(WMWidget *parent);
Panel *InitKeyboardShortcuts(WMWidget *parent);
Panel *InitMenu(WMWidget *parent);
Panel *InitMenuPreferences(WMWidget *parent);
Panel *InitMouseSettings(WMWidget *parent);
Panel *InitPaths(WMWidget *parent);
Panel *InitPreferences(WMWidget *parent);
Panel *InitWindowHandling(WMWidget *parent);
Panel *InitWorkspace(WMWidget *parent);


#define FRAME_TOP	105
#define FRAME_LEFT	-2
#define FRAME_WIDTH	524
#define FRAME_HEIGHT	235
#endif /* WPREFS_H_ */
