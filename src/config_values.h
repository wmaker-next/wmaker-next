/*
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

/*
 * Integer values that an enum-typed key in the WindowMaker defaults
 * domain can resolve to, lifted out of WindowMaker.h so config_schema.c
 * and WPrefs can name them without pulling in the rest of that header.
 * A few non-configurable internal indices (ACTION_*, WS_*, CLIP_*) are
 * kept here too because they sat in the same contiguous block and have
 * no dependencies of their own.
 */

#ifndef WMCONFIG_VALUES_H_
#define WMCONFIG_VALUES_H_

/* geometry displays */
#define WDIS_CENTER		0	/* center of screen */
#define WDIS_TOPLEFT		1	/* top left corner of screen */
#define WDIS_FRAME_CENTER	2	/* center of the frame */
#define WDIS_NONE		3

/* keyboard input focus mode */
#define WKF_CLICK	0
#define WKF_SLOPPY	2

/* colormap change mode */
#define WCM_CLICK	0
#define WCM_POINTER	1

/* window placement mode */
#define WPM_MANUAL	0
#define WPM_CASCADE	1
#define WPM_SMART	2
#define WPM_RANDOM	3
#define WPM_AUTO        4
#define WPM_CENTER      5

/* text justification */
#define WTJ_CENTER	0
#define WTJ_LEFT	1
#define WTJ_RIGHT	2

/* iconification styles */
#define WIS_ZOOM        0
#define WIS_TWIST       1
#define WIS_FLIP        2
#define WIS_NONE        3
#define WIS_RANDOM	4 /* secret */

/* switchmenu actions */
#define ACTION_ADD		0
#define ACTION_REMOVE		1
#define ACTION_CHANGE		2
#define ACTION_CHANGE_WORKSPACE	3
#define ACTION_CHANGE_STATE	4


/* mouse acceleration denominator, the numerator is the config value */
#define MOUSE_ACCELERATION_DENOM 10

/* speeds */
#define SPEED_ULTRAFAST 0
#define SPEED_FAST	1
#define SPEED_MEDIUM	2
#define SPEED_SLOW	3
#define SPEED_ULTRASLOW 4


/* window states */
#define WS_FOCUSED	0
#define WS_UNFOCUSED	1
#define WS_PFOCUSED	2
/* extra slot for inactive tab-group segments, never used as a
 * frame state, only as an index into the title texture and
 * colour arrays so themes can style inactive tabs distinctly,
 * left NULL when TabInactiveTitleBack/Color are unset and the
 * renderer falls back to WS_PFOCUSED */
#define WS_TABINACTIVE	3
#define WS_STATE_COUNT	4

/* clip title colors */
#define CLIP_NORMAL	0
#define CLIP_COLLAPSED	1


/* icon yard position */
#define	IY_VERT		1
#define	IY_HORIZ	0
#define	IY_TOP		2
#define	IY_BOTTOM	0
#define	IY_RIGHT	4
#define	IY_LEFT		0


/* menu styles */
#define MS_NORMAL		0
#define MS_SINGLE_TEXTURE	1
#define MS_FLAT			2

/* workspace actions */
#define WA_NONE                 0
#define WA_SELECT_WINDOWS       1
#define WA_OPEN_APPMENU         2
#define WA_OPEN_WINLISTMENU     3
#define WA_SWITCH_WORKSPACES    4
#define WA_MOVE_PREVWORKSPACE   5
#define WA_MOVE_NEXTWORKSPACE   6
#define WA_SWITCH_WINDOWS       7
#define WA_MOVE_PREVWINDOW      8
#define WA_MOVE_NEXTWINDOW      9
#define WA_VIRTUAL_DESKTOP_PAN  10

/* screen display position */
#define WD_NONE		0
#define WD_CENTER	1
#define WD_TOP		2
#define WD_BOTTOM	3
#define WD_LEFT		4
#define WD_RIGHT	5
#define WD_TOPLEFT	6
#define WD_TOPRIGHT	7
#define WD_BOTTOMLEFT	8
#define WD_BOTTOMRIGHT	9

/* titlebar style */
#define TS_NEW		0
#define TS_OLD		1
#define TS_NEXT		2
#define TS_FLAT		3

/* TS_FLAT reuses TS_NEW geometry (button cores are children of the
 * frame core and the titlebar is shrunk between them) but draws no
 * per-slice relief, so every site that decides parenting, button size
 * or x-math tests this instead of TS_NEW directly. */
#define TS_NEW_GEOM(s)	((s) == TS_NEW || (s) == TS_FLAT)

/* default titlebar button layout, left side */
#ifdef XKB_BUTTON_HINT
#define DEF_TITLEBAR_LEFT	"(Miniaturize, Language)"
#else
#define DEF_TITLEBAR_LEFT	"(Miniaturize)"
#endif

/* workspace border position */
#define	WB_NONE		0
#define	WB_LEFTRIGHT	1
#define	WB_TOPBOTTOM	2
#define WB_ALLDIRS      (WB_LEFTRIGHT|WB_TOPBOTTOM)

/* drag maximized window behaviors */
enum {
	DRAGMAX_MOVE,
	DRAGMAX_RESTORE,
	DRAGMAX_UNMAXIMIZE,
	DRAGMAX_NOMOVE
};

/* tab-group drag drop area */
enum {
	WTAB_ATTACH_TITLEBAR,
	WTAB_ATTACH_WINDOW
};

/* run-dialog completion matching mode */
enum {
	RUN_MATCH_PREFIX,
	RUN_MATCH_SUBSTRING,
	RUN_MATCH_FUZZY
};

/* switch panel order */
#define SWO_FOCUS	0	/* most recently focused first */
#define SWO_AGE		1	/* creation order, oldest first */

/* per-workspace tiling layout, kept in sync with WLayout in layout.h so
 * WPrefs can consume the schema without pulling in wmaker headers */
enum {
	WVL_FLOAT,
	WVL_TILE,
	WVL_TILE_H,
	WVL_GRID,
	WVL_MONOCLE
};

#endif /* WMCONFIG_VALUES_H_ */
