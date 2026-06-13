/*
 * check_wprefs_schema - assert WPrefs enum-choice tables and the
 * WindowMaker config schema agree on every enum-typed key WPrefs
 * exposes.  Adding a value to config-schema-def.h without adding it to
 * WPrefs.app/enumchoices.h (or vice versa) fails make check.
 */

#include "wconfig.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "config-schema-public.h"

/* enumchoices.h carries N_()-wrapped UI labels we discard here */
#ifndef N_
#define N_(text) (text)
#endif
#include "../../WPrefs.app/enumchoices.h"

static int failures;

#define CHECK(cond) do {                                                   \
		if (!(cond)) {                                                     \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);\
			failures++;                                                    \
		}                                                                  \
	} while (0)

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
#define CUR_KEY "ResizeDisplay"
	WPREFS_TABLE_resize_display(R2)
#undef CUR_KEY
#define CUR_KEY "MoveDisplay"
	WPREFS_TABLE_move_display(R2)
#undef CUR_KEY
};

#undef R2
#undef R3

static const size_t wprefs_choice_count =
	sizeof(wprefs_choices) / sizeof(wprefs_choices[0]);

/* Schema values WPrefs deliberately does not offer.  Adding here needs
 * a reason, removing the underlying cause should remove the row. */
static const struct choice wprefs_hidden[] = {
	/* config-values.h marks WIS_RANDOM "secret" */
	{ "IconificationStyle", "random" },
	/* moveres.c showPosition() has the WDIS_NEW move-mode drawing
	 * compiled out under #if 0 and the cycle key skips it, the value
	 * is only meaningful for ResizeDisplay which shares the enum */
	{ "MoveDisplay", "Line" },
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

static int wprefs_offers(const char *key, const char *value)
{
	size_t i;

	for (i = 0; i < wprefs_choice_count; i++)
		if (strcasecmp(wprefs_choices[i].key, key) == 0 &&
		    strcasecmp(wprefs_choices[i].db_value, value) == 0)
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
			failures++;
			continue;
		}
		if (!schema_accepts(table, wprefs_choices[i].db_value)) {
			fprintf(stderr, "FAIL: WPrefs offers %s = \"%s\" but the schema rejects it\n",
				wprefs_choices[i].key, wprefs_choices[i].db_value);
			failures++;
		}
	}
}

/* Every non-alias schema value for a WPrefs-controlled key must be
 * offered by WPrefs, so a value added to config-schema-def.h without a
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
				failures++;
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

	if (failures) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}

	printf("ok - check_wprefs_schema (%zu choices)\n", wprefs_choice_count);
	return 0;
}
