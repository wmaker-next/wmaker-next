/*
 *  config_schema.c
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

/*
 * Typed Window Maker defaults schema.
 */

#include "wconfig.h"

#include <stdlib.h>
#include <strings.h>

#include "WindowMaker.h"
#include "dockgeom.h"
#include "config_schema.h"

#define FIELD(name) offsetof(WConfig, name), sizeof(((WConfig *)0)->name)
#define NUM2STRING_(x) #x
#define NUM2STRING(x) NUM2STRING_(x)

static const WConfigEnum focus_modes[] = {
	{"Manual", WKF_CLICK, 0},
	{"ClickToFocus", WKF_CLICK, 1},
	{"Sloppy", WKF_SLOPPY, 0},
	{"SemiAuto", WKF_SLOPPY, 1},
	{"Auto", WKF_SLOPPY, 1},
	{NULL, 0, 0}
};

static const WConfigEnum titlebar_modes[] = {
	{"new", TS_NEW, 0},
	{"old", TS_OLD, 0},
	{"next", TS_NEXT, 0},
	{"flat", TS_FLAT, 0},
	{NULL, 0, 0}
};

static const char *const wtb_role_name[WTB_ROLE_COUNT] = {
	[WTB_MINIATURIZE] = "Miniaturize",
	[WTB_CLOSE]       = "Close",
	[WTB_MAXIMIZE]    = "Maximize",
	[WTB_ICON]        = "Icon",
#ifdef XKB_BUTTON_HINT
	[WTB_LANGUAGE]    = "Language",
#endif
};

int wTitlebarLayoutBuild(struct WTitlebarLayout *out,
			 const WConfigStringArray *left,
			 const WConfigStringArray *right)
{
	const WConfigStringArray *side_array[2];
	int s, r, warned = 0;
	size_t i;
	short ord;

	for (r = 0; r < WTB_ROLE_COUNT; r++) {
		out->side[r] = WTB_SIDE_NONE;
		out->order[r] = 0;
	}

	side_array[0] = left;
	side_array[1] = right;

	for (s = 0; s < 2; s++) {
		ord = 0;
		if (!side_array[s])
			continue;
		for (i = 0; i < side_array[s]->count; i++) {
			const char *name = side_array[s]->items[i];

			for (r = 0; r < WTB_ROLE_COUNT; r++)
				if (strcasecmp(name, wtb_role_name[r]) == 0)
					break;
			if (r >= WTB_ROLE_COUNT) {
				wwarning(_("unknown titlebar button role \"%s\""), name);
				warned++;
				continue;
			}
			if (out->side[r] != WTB_SIDE_NONE) {
				wwarning(_("duplicate titlebar button role \"%s\""), name);
				warned++;
				continue;
			}
			out->side[r] = (s == 0) ? WTB_SIDE_LEFT : WTB_SIDE_RIGHT;
			out->order[r] = ord++;
		}
	}

	return warned;
}

static const WConfigEnum icon_positions[] = {
	{"blv", IY_BOTTOM | IY_LEFT | IY_VERT, 0},
	{"blh", IY_BOTTOM | IY_LEFT | IY_HORIZ, 0},
	{"brv", IY_BOTTOM | IY_RIGHT | IY_VERT, 0},
	{"brh", IY_BOTTOM | IY_RIGHT | IY_HORIZ, 0},
	{"tlv", IY_TOP | IY_LEFT | IY_VERT, 0},
	{"tlh", IY_TOP | IY_LEFT | IY_HORIZ, 0},
	{"trv", IY_TOP | IY_RIGHT | IY_VERT, 0},
	{"trh", IY_TOP | IY_RIGHT | IY_HORIZ, 0},
	{NULL, 0, 0}
};

static const WConfigEnum menu_styles[] = {
	{"normal", MS_NORMAL, 0},
	{"singletexture", MS_SINGLE_TEXTURE, 0},
	{"flat", MS_FLAT, 0},
	{NULL, 0, 0}
};

static const WConfigEnum dock_edges[] = {
	{"Left",   WDOCK_EDGE_LEFT,   0},
	{"Right",  WDOCK_EDGE_RIGHT,  0},
	{"Top",    WDOCK_EDGE_TOP,    0},
	{"Bottom", WDOCK_EDGE_BOTTOM, 0},
	{NULL, 0, 0}
};

static const WConfigEnum title_justifications[] = {
	{"Left", WTJ_LEFT, 0},
	{"Center", WTJ_CENTER, 0},
	{"Right", WTJ_RIGHT, 0},
	{NULL, 0, 0}
};

static const WConfigEnum iconification_styles[] = {
	{"Zoom", WIS_ZOOM, 0},
	{"Twist", WIS_TWIST, 0},
	{"Flip", WIS_FLIP, 0},
	{"None", WIS_NONE, 0},
	{"random", WIS_RANDOM, 0},
	{NULL, 0, 0}
};

static const WConfigEnum mouse_button_actions[] = {
	{"None", WA_NONE, 0},
	{"SelectWindows", WA_SELECT_WINDOWS, 0},
	{"OpenApplicationsMenu", WA_OPEN_APPMENU, 0},
	{"OpenWindowListMenu", WA_OPEN_WINLISTMENU, 0},
	{"MoveToPrevWorkspace", WA_MOVE_PREVWORKSPACE, 0},
	{"MoveToNextWorkspace", WA_MOVE_NEXTWORKSPACE, 0},
	{"MoveToPrevWindow", WA_MOVE_PREVWINDOW, 0},
	{"MoveToNextWindow", WA_MOVE_NEXTWINDOW, 0},
#ifdef VIRTUAL_DESKTOP
	{"VirtualDesktopPan", WA_VIRTUAL_DESKTOP_PAN, 0},
#endif
	{NULL, 0, 0}
};

static const WConfigEnum mouse_wheel_actions[] = {
	{"None", WA_NONE, 0},
	{"SwitchWorkspaces", WA_SWITCH_WORKSPACES, 0},
	{"SwitchWindows", WA_SWITCH_WINDOWS, 0},
	{NULL, 0, 0}
};

static const WConfigEnum geometry_displays[] = {
	{"None", WDIS_NONE, 0},
	{"Center", WDIS_CENTER, 0},
	{"Corner", WDIS_TOPLEFT, 0},
	{"Floating", WDIS_FRAME_CENTER, 0},
	{NULL, 0, 0}
};

static const WConfigEnum colormap_modes[] = {
	{"Manual", WCM_CLICK, 0},
	{"ClickToFocus", WCM_CLICK, 1},
	{"Auto", WCM_POINTER, 0},
	{"FocusFollowMouse", WCM_POINTER, 1},
	{NULL, 0, 0}
};

static const WConfigEnum placements[] = {
	{"Auto", WPM_AUTO, 0},
	{"Smart", WPM_SMART, 0},
	{"Cascade", WPM_CASCADE, 0},
	{"Random", WPM_RANDOM, 0},
	{"Manual", WPM_MANUAL, 0},
	{"Center", WPM_CENTER, 0},
	{NULL, 0, 0}
};

static const WConfigEnum workspace_borders[] = {
	{"None", WB_NONE, 0},
	{"LeftRight", WB_LEFTRIGHT, 0},
	{"TopBottom", WB_TOPBOTTOM, 0},
	{"AllDirections", WB_ALLDIRS, 0},
	{NULL, 0, 0}
};

static const WConfigEnum display_positions[] = {
	{"none", WD_NONE, 0},
	{"center", WD_CENTER, 0},
	{"top", WD_TOP, 0},
	{"bottom", WD_BOTTOM, 0},
	{"left", WD_LEFT, 0},
	{"right", WD_RIGHT, 0},
	{"topleft", WD_TOPLEFT, 0},
	{"topright", WD_TOPRIGHT, 0},
	{"bottomleft", WD_BOTTOMLEFT, 0},
	{"bottomright", WD_BOTTOMRIGHT, 0},
	{NULL, 0, 0}
};

static const WConfigEnum speeds[] = {
	{"UltraFast", SPEED_ULTRAFAST, 0},
	{"Fast", SPEED_FAST, 0},
	{"Medium", SPEED_MEDIUM, 0},
	{"Slow", SPEED_SLOW, 0},
	{"UltraSlow", SPEED_ULTRASLOW, 0},
	{NULL, 0, 0}
};

static const WConfigEnum drag_maximized_windows[] = {
	{"Move", DRAGMAX_MOVE, 0},
	{"RestoreGeometry", DRAGMAX_RESTORE, 0},
	{"Unmaximize", DRAGMAX_UNMAXIMIZE, 0},
	{"NoMove", DRAGMAX_NOMOVE, 0},
	{NULL, 0, 0}
};

static const WConfigEnum tab_attach_areas[] = {
	{"Titlebar", WTAB_ATTACH_TITLEBAR, 0},
	{"Window", WTAB_ATTACH_WINDOW, 0},
	{NULL, 0, 0}
};

static const WConfigEnum switcher_orders[] = {
	{"Focus", SWO_FOCUS, 0},
	{"Age",   SWO_AGE,   0},
	{NULL, 0, 0}
};

static const WConfigEnum run_matching_modes[] = {
	{"prefix", RUN_MATCH_PREFIX, 0},
	{"substring", RUN_MATCH_SUBSTRING, 0},
	{"fuzzy", RUN_MATCH_FUZZY, 0},
	{NULL, 0, 0}
};

static const WConfigEnum workspace_layouts[] = {
	{"float", WVL_FLOAT, 0},
	{"tile", WVL_TILE, 0},
	{"tileh", WVL_TILE_H, 0},
	{"grid", WVL_GRID, 0},
	{"monocle", WVL_MONOCLE, 0},
	{NULL, 0, 0}
};

#define WCFG_SCHEMA_ROW(K, name, T, field, def, etable, lo, hi, fl, refresh) \
	{ name, WCFG_KEY_##K, WCFG_##T, def, FIELD(field), etable, lo, hi, fl, refresh },
#define WCFG_SCHEMA_KB_ROW(K, name, def, wkbd) \
	{ name, WCFG_KEY_##K, WCFG_KEY_BINDING, def, FIELD(key_bindings[wkbd]), \
	  NULL, wkbd, wkbd, WCFG_RUNTIME_RELOAD, 0 },

static const WConfigEntry config_schema[] = {
	WCFG_SCHEMA(WCFG_SCHEMA_ROW)
	WCFG_SCHEMA_KEYBINDINGS(WCFG_SCHEMA_KB_ROW)
};

#undef WCFG_SCHEMA_ROW
#undef WCFG_SCHEMA_KB_ROW

_Static_assert(sizeof(config_schema) / sizeof(config_schema[0]) == WCFG_KEY_COUNT,
	       "config schema/key enum mismatch");

const WConfigEntry *wConfigSchema(void)
{
	return config_schema;
}

size_t wConfigSchemaCount(void)
{
	return sizeof(config_schema) / sizeof(config_schema[0]);
}

const WConfigEntry *wConfigEntryForKey(const char *key)
{
	size_t i;

	if (!key)
		return NULL;

	for (i = 0; i < wConfigSchemaCount(); i++)
		if (strcasecmp(config_schema[i].key, key) == 0)
			return &config_schema[i];

	return NULL;
}

const WConfigEnum *wConfigSchemaEnumTable(const char *key)
{
	const WConfigEntry *e = wConfigEntryForKey(key);

	return e ? e->enum_values : NULL;
}

const char *wConfigSchemaDefault(const char *key)
{
	const WConfigEntry *e = wConfigEntryForKey(key);

	return e ? e->default_value : NULL;
}

int wConfigSchemaClampInt(const char *key, int v)
{
	const WConfigEntry *e = wConfigEntryForKey(key);

	if (!e)
		return v;
	if (v < e->min_value)
		return e->min_value;
	if (v > e->max_value)
		return e->max_value;
	return v;
}

int wConfigSchemaParseInt(const char *key, const char *str)
{
	long value = strtol(str ? str : "", NULL, 10);

	if (value > INT_MAX)
		value = INT_MAX;
	else if (value < INT_MIN)
		value = INT_MIN;
	return wConfigSchemaClampInt(key, (int)value);
}

int wConfigSchemaRequiresRestart(const char *key)
{
	const WConfigEntry *e = wConfigEntryForKey(key);

	return e && (e->flags & WCFG_STARTUP_ONLY);
}
