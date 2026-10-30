/*
 *  check_wprefs_schema.c
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
 * check_wprefs_schema - assert WPrefs enum-choice tables and the
 * WindowMaker config schema agree on every enum-typed key WPrefs
 * exposes. Adding a value to config_schema_def.h without adding it to
 * WPrefs.app/enumchoices.h (or vice versa) fails make check, every
 * schema enum key must be either offered by WPrefs or explicitly
 * listed as having no UI, and keys whose UI stores a popup or radio
 * index as the value are pinned to value == row position.
 */

#include "wconfig.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "config_schema.h"

/* enumchoices.h carries N_()-wrapped UI labels we discard here */
#ifndef N_
#define N_(text) (text)
#endif
#include "../../WPrefs.app/enumchoices.h"

#include "check_common.h"

struct choice {
	const char *key;
	const char *db_value;
};

#define R2(v, l)    { CUR_KEY, v },
#define R3(i, v, a) { CUR_KEY, v },

static const struct choice wprefs_choices[] = {
#define CUR_KEY "IconificationStyle"
	WPREFS_TABLE_icon_animation(R2)
#undef CUR_KEY
#define CUR_KEY "MenuStyle"
	WPREFS_TABLE_menu_style(R3)
#undef CUR_KEY
#define CUR_KEY "TitleJustify"
	WPREFS_TABLE_wintitle_align(R3)
#undef CUR_KEY
#define CUR_KEY "MouseLeftButtonAction"
	WPREFS_TABLE_button_actions(R2)
#undef CUR_KEY
#define CUR_KEY "MouseMiddleButtonAction"
	WPREFS_TABLE_button_actions(R2)
#undef CUR_KEY
#define CUR_KEY "MouseRightButtonAction"
	WPREFS_TABLE_button_actions(R2)
#undef CUR_KEY
#define CUR_KEY "MouseBackwardButtonAction"
	WPREFS_TABLE_button_actions(R2)
#undef CUR_KEY
#define CUR_KEY "MouseForwardButtonAction"
	WPREFS_TABLE_button_actions(R2)
#undef CUR_KEY
#define CUR_KEY "MouseWheelAction"
	WPREFS_TABLE_wheel_actions(R2)
#undef CUR_KEY
#define CUR_KEY "MouseWheelTiltAction"
	WPREFS_TABLE_wheel_actions(R2)
#undef CUR_KEY
#define CUR_KEY "WindowPlacement"
	WPREFS_TABLE_window_placements(R2)
#undef CUR_KEY
#define CUR_KEY "DragMaximizedWindow"
	WPREFS_TABLE_drag_maximized_window(R2)
#undef CUR_KEY
#define CUR_KEY "TabAttachArea"
	WPREFS_TABLE_tab_attach_area(R2)
#undef CUR_KEY
#define CUR_KEY "SwitcherOrder"
	WPREFS_TABLE_switcher_order(R2)
#undef CUR_KEY
#define CUR_KEY "ResizeDisplay"
	WPREFS_TABLE_resize_display(R2)
#undef CUR_KEY
#define CUR_KEY "MoveDisplay"
	WPREFS_TABLE_move_display(R2)
#undef CUR_KEY
#define CUR_KEY "DefaultWorkspaceLayout"
	WPREFS_TABLE_workspace_layouts(R2)
#undef CUR_KEY
#define CUR_KEY "FocusMode"
	WPREFS_TABLE_focus_mode(R2)
#undef CUR_KEY
#define CUR_KEY "NewStyle"
	WPREFS_TABLE_titlebar_mode(R2)
#undef CUR_KEY
#define CUR_KEY "ColormapMode"
	WPREFS_TABLE_colormap_mode(R2)
#undef CUR_KEY
#define CUR_KEY "IconPosition"
	WPREFS_TABLE_icon_position(R2)
#undef CUR_KEY
#define CUR_KEY "WorkspaceBorder"
	WPREFS_TABLE_workspace_border(R2)
#undef CUR_KEY
#define CUR_KEY "WorkspaceNameDisplayPosition"
	WPREFS_TABLE_display_position(R2)
#undef CUR_KEY
#define CUR_KEY "MenuScrollSpeed"
	WPREFS_TABLE_speed(R2)
#undef CUR_KEY
#define CUR_KEY "IconSlideSpeed"
	WPREFS_TABLE_speed(R2)
#undef CUR_KEY
#define CUR_KEY "ShadeSpeed"
	WPREFS_TABLE_speed(R2)
#undef CUR_KEY
#define CUR_KEY "DockEdge"
	WPREFS_TABLE_dock_edge(R3)
#undef CUR_KEY
};

#undef R2
#undef R3

static const size_t wprefs_choice_count =
	sizeof(wprefs_choices) / sizeof(wprefs_choices[0]);

/* Schema values WPrefs deliberately does not offer. Adding here needs
 * a reason, removing the underlying cause should remove the row. */
static const struct choice wprefs_hidden[] = {
	/* config_values.h marks WIS_RANDOM "secret" */
	{ "IconificationStyle", "random" },
};

static int wprefs_hides(const char *key, const char *value)
{
	size_t i;

	for (i = 0; i < sizeof(wprefs_hidden) / sizeof(wprefs_hidden[0]); i++)
		if (strcasecmp(wprefs_hidden[i].key, key) == 0 &&
		    strcasecmp(wprefs_hidden[i].db_value, value) == 0)
			return 1;
	return 0;
}

/* Enum-typed schema keys WPrefs deliberately offers no widget for.
 * The coverage test below requires every schema enum key to be offered
 * in the manifest or listed here, so a new key cannot appear without a
 * decision about its WPrefs story. */
static const char *const wprefs_no_ui[] = {
	/* position of the keychain OSD, wdwrite only */
	"KeychainDisplayPosition",
	/* run-dialog text matching, dialog-side knob */
	"RunDialogMatching",
};

/* Keys whose WPrefs UI stores the selected popup or radio index as the
 * value, so the schema rows must keep value == row position. The
 * owning widget is named per row. */
static const char *const wprefs_positional[] = {
	"DefaultWorkspaceLayout",	/* Workspace.c layout popup */
	"WorkspaceNameDisplayPosition",	/* Workspace.c position popup */
	"MenuScrollSpeed",		/* MenuPreferences.c speed radio */
	"IconSlideSpeed",		/* Configurations.c speed radio */
	"ShadeSpeed",			/* Configurations.c speed radio */
};

static int wprefs_offers(const char *key, const char *value)
{
	size_t i;

	for (i = 0; i < wprefs_choice_count; i++)
		if (strcasecmp(wprefs_choices[i].key, key) == 0 &&
		    strcasecmp(wprefs_choices[i].db_value, value) == 0)
			return 1;
	return 0;
}

static int wprefs_offers_any(const char *key)
{
	size_t i;

	for (i = 0; i < wprefs_choice_count; i++)
		if (strcmp(wprefs_choices[i].key, key) == 0)
			return 1;
	return 0;
}

static int wprefs_no_ui_has(const char *key)
{
	size_t i;

	for (i = 0; i < sizeof(wprefs_no_ui) / sizeof(wprefs_no_ui[0]); i++)
		if (strcasecmp(wprefs_no_ui[i], key) == 0)
			return 1;
	return 0;
}

static int schema_accepts(const WConfigEnum *table, const char *value)
{
	const WConfigEnum *e;

	for (e = table; e->string; e++)
		if (strcasecmp(e->string, value) == 0)
			return 1;
	return 0;
}

/* Every db_value WPrefs offers must resolve through the schema. */
static void test_wprefs_values_in_schema(void)
{
	size_t i;
	const WConfigEnum *table;

	for (i = 0; i < wprefs_choice_count; i++) {
		table = wConfigSchemaEnumTable(wprefs_choices[i].key);
		if (!table) {
			fprintf(stderr, "FAIL: WPrefs key \"%s\" is not an enum-typed schema key\n",
				wprefs_choices[i].key);
			check_failures++;
			continue;
		}
		if (!schema_accepts(table, wprefs_choices[i].db_value)) {
			fprintf(stderr, "FAIL: WPrefs offers %s = \"%s\" but the schema rejects it\n",
				wprefs_choices[i].key, wprefs_choices[i].db_value);
			check_failures++;
		}
	}
}

/* Every non-alias schema value for a WPrefs-controlled key must be
 * offered by WPrefs, so a value added to config_schema_def.h without a
 * matching enumchoices.h row fails here. */
static void test_schema_values_in_wprefs(void)
{
	size_t i;
	const char *prev_key;
	const WConfigEnum *table, *e;

	prev_key = NULL;
	for (i = 0; i < wprefs_choice_count; i++) {
		if (prev_key && strcmp(prev_key, wprefs_choices[i].key) == 0)
			continue;
		prev_key = wprefs_choices[i].key;

		table = wConfigSchemaEnumTable(prev_key);
		if (!table)
			continue;  /* already reported above */

		for (e = table; e->string; e++) {
			if (e->is_alias)
				continue;
			if (wprefs_hides(prev_key, e->string))
				continue;
			if (!wprefs_offers(prev_key, e->string)) {
				fprintf(stderr,
					"FAIL: schema accepts %s = \"%s\" but WPrefs does not offer it\n",
					prev_key, e->string);
				check_failures++;
			}
		}
	}
}

/* Every schema enum key must be offered by WPrefs or listed as no-UI,
 * so a new key cannot appear without a decision about its WPrefs
 * story. */
static void test_schema_enum_key_coverage(void)
{
	size_t i;

	for (i = 0; i < wConfigSchemaCount(); i++) {
		const WConfigEntry *e = &wConfigSchema()[i];

		if (e->type != WCFG_ENUM)
			continue;
		if (wprefs_offers_any(e->key))
			continue;
		if (wprefs_no_ui_has(e->key))
			continue;
		fprintf(stderr,
			"FAIL: schema enum key \"%s\" is neither offered by WPrefs nor marked no-UI\n",
			e->key);
		check_failures++;
	}
}

/* The stored index must match the schema table row order. */
static void test_popup_index_pins(void)
{
	size_t k;

	for (k = 0; k < sizeof(wprefs_positional) / sizeof(wprefs_positional[0]); k++) {
		const char *key = wprefs_positional[k];
		const WConfigEnum *table = wConfigSchemaEnumTable(key);
		const WConfigEnum *row;
		int index = 0;

		if (!table) {
			fprintf(stderr, "FAIL: positional key \"%s\" has no enum table\n", key);
			check_failures++;
			continue;
		}
		for (row = table; row->string; row++, index++) {
			if (row->is_alias) {
				fprintf(stderr,
					"FAIL: %s row \"%s\" is an alias, a popup index cannot select it\n",
					key, row->string);
				check_failures++;
			} else if (row->value != index) {
				fprintf(stderr,
					"FAIL: %s row \"%s\" has value %d but the popup index is %d\n",
					key, row->string, row->value, index);
				check_failures++;
			}
		}
	}
}

int main(void)
{
	CHECK(wprefs_choice_count > 0);

	/* keyOptions[] is a static table inside WPrefs.app, not
	 * reachable from here, this only proves the key exists in the
	 * public schema header */
	CHECK(wConfigSchemaDefault("ToggleFullscreenKey") != NULL);

	test_wprefs_values_in_schema();
	test_schema_values_in_wprefs();
	test_schema_enum_key_coverage();
	test_popup_index_pins();

	return check_report("check_wprefs_schema");
}
