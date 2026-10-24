/*
 * Typed Window Maker configuration model.
 */

#include "wconfig.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "WindowMaker.h"
#include "actions.h"
#include "dock.h"
#include "icon.h"
#include "screen.h"
#include "event.h"
#include "workspace.h"
#ifdef VIRTUAL_DESKTOP
#include "vdesk.h"
#endif
#include "config-schema.h"
#include "wconfigmodel.h"
#include "hotcorner.h"
#include "cursor_find.h"
#include "misc.h"
#include "window.h"

/* Parse a FrameInnerBorderSides WConfigStringArray into the
 * INNER_SIDE_* bitmask. An empty array means all four sides so a
 * user who sets only FrameInnerBorderWidth keeps the symmetric look. */
unsigned int wFrameInnerBorderSidesFromArray(const WConfigStringArray *array)
{
	unsigned int sides = 0;
	size_t i;

	if (!array || array->count == 0)
		return INNER_SIDE_ALL;

	for (i = 0; i < array->count; i++) {
		const char *s = array->items[i];

		if (!s)
			continue;
		if (strcasecmp(s, "left") == 0)
			sides |= INNER_SIDE_LEFT;
		else if (strcasecmp(s, "right") == 0)
			sides |= INNER_SIDE_RIGHT;
		else if (strcasecmp(s, "top") == 0)
			sides |= INNER_SIDE_TOP;
		else if (strcasecmp(s, "bottom") == 0)
			sides |= INNER_SIDE_BOTTOM;
		else if (strcasecmp(s, "all") == 0)
			sides |= INNER_SIDE_ALL;
		else
			wwarning(_("unknown FrameInnerBorderSides token \"%s\""), s);
	}
	return sides ? sides : INNER_SIDE_ALL;
}

static WMPropList *dict_get(WMPropList *dict, const char *key)
{
	return WMGetFromPLDictionaryByName(dict, key);
}

static WMPropList *schema_plkey(const WConfigEntry *entry)
{
	static WMPropList *plkeys[WCFG_KEY_COUNT];
	WConfigKey key;

	assert(entry != NULL);
	key = entry->config_key;
	assert(key >= 0 && key < WCFG_KEY_COUNT);
	if (!plkeys[key])
		plkeys[key] = WMCreatePLString(entry->key);

	return plkeys[key];
}

static WMPropList *dict_get_entry(WMPropList *dict, const WConfigEntry *entry)
{
	if (!dict)
		return NULL;
	return WMGetFromPLDictionary(dict, schema_plkey(entry));
}

static WMPropList *entry_default_plist(const WConfigEntry *entry)
{
	switch (entry->type) {
	case WCFG_STRING:
	case WCFG_KEY_BINDING:
	case WCFG_FONT_SPEC:
	case WCFG_COLOR_SPEC:
		return WMCreatePLString(entry->default_value);
	case WCFG_PATH_LIST:
	case WCFG_COORD:
	case WCFG_STRING_ARRAY:
	case WCFG_SWITCH_PANEL_IMAGES:
	case WCFG_TEXTURE_SPEC:
	case WCFG_CURSOR_SPEC:
	case WCFG_BACKGROUND_SPEC:
		return WMCreatePropListFromDescription(entry->default_value);
	case WCFG_BOOL:
	case WCFG_INT:
	case WCFG_UINT:
	case WCFG_ENUM:
		return WMCreatePLString(entry->default_value);
	default:
		return NULL;
	}
}

static void dict_put_default(WMPropList *dict, const WConfigEntry *entry)
{
	WMPropList *plkey, *plvalue;

	if (!dict)
		return;

	plkey = schema_plkey(entry);
	if (WMGetFromPLDictionary(dict, plkey))
		return;

	plvalue = entry_default_plist(entry);
	if (plvalue) {
		WMPutInPLDictionary(dict, plkey, plvalue);
		WMReleasePropList(plvalue);
	}
}

static void warn_using_default(const WConfigEntry *entry)
{
	wwarning(_("using default \"%s\" instead"), entry->default_value);
}

static const char *plist_string_or_default(const WConfigEntry *entry,
					   WMPropList *value)
{
	if (!value)
		return entry->default_value;
	if (WMIsPLString(value))
		return WMGetFromPLString(value);

	wwarning(_("Wrong option format for key \"%s\". Should be String."),
		 entry->key);
	warn_using_default(entry);
	return entry->default_value;
}

static bool parse_long_strict(const char *str, long *out)
{
	char *end;
	long value;

	errno = 0;
	value = strtol(str, &end, 0);
	if (errno != 0 || end == str)
		return false;
	while (isspace((unsigned char)*end))
		end++;
	if (*end != '\0')
		return false;

	*out = value;
	return true;
}

static bool parse_bool(const WConfigEntry *entry, const char *str, int *out)
{
	long value;

	if (((str[0] == 'y' || str[0] == 'Y') && str[1] == '\0') ||
	    strcasecmp(str, "YES") == 0) {
		*out = 1;
		return true;
	}
	if (((str[0] == 'n' || str[0] == 'N') && str[1] == '\0') ||
	    strcasecmp(str, "NO") == 0) {
		*out = 0;
		return true;
	}
	if (parse_long_strict(str, &value)) {
		*out = (value != 0);
		return true;
	}

	wwarning(_("can't convert \"%s\" to boolean for key \"%s\""),
		 str, entry->key);
	return false;
}

static bool parse_int(const WConfigEntry *entry, const char *str, int *out)
{
	long value;

	if (!parse_long_strict(str, &value)) {
		wwarning(_("can't convert \"%s\" to integer for key \"%s\""),
			 str, entry->key);
		return false;
	}
	if (value < entry->min_value || value > entry->max_value) {
		wwarning(_("integer value \"%s\" for key \"%s\" is outside [%d..%d]"),
			 str, entry->key, entry->min_value, entry->max_value);
		return false;
	}

	*out = (int)value;
	return true;
}

static void enum_values_description(const WConfigEnum *values, char *buffer,
				    size_t size)
{
	const WConfigEnum *v;

	buffer[0] = '\0';
	for (v = values; v->string != NULL; v++) {
		if (v->is_alias)
			continue;
		if (buffer[0] != '\0')
			wstrlcat(buffer, ", ", size);
		wstrlcat(buffer, "\"", size);
		wstrlcat(buffer, v->string, size);
		wstrlcat(buffer, "\"", size);
	}
}

static bool parse_enum(const WConfigEntry *entry, const char *str, int *out)
{
	const WConfigEnum *v;
	char buffer[256];

	for (v = entry->enum_values; v && v->string != NULL; v++) {
		if (strcasecmp(v->string, str) == 0) {
			*out = v->value;
			return true;
		}
	}

	enum_values_description(entry->enum_values, buffer, sizeof(buffer));
	wwarning(_("wrong option value for key \"%s\"; got \"%s\", should be one of %s."),
		 entry->key, str, buffer);
	return false;
}

static bool parse_value(const WConfigEntry *entry, const char *str, int *out)
{
	switch (entry->type) {
	case WCFG_BOOL:
		return parse_bool(entry, str, out);
	case WCFG_INT:
	case WCFG_UINT:
		return parse_int(entry, str, out);
	case WCFG_ENUM:
		return parse_enum(entry, str, out);
	case WCFG_STRING:
	case WCFG_PATH_LIST:
	case WCFG_COORD:
	case WCFG_STRING_ARRAY:
	case WCFG_SWITCH_PANEL_IMAGES:
	case WCFG_KEY_BINDING:
	case WCFG_TEXTURE_SPEC:
	case WCFG_FONT_SPEC:
	case WCFG_COLOR_SPEC:
	case WCFG_CURSOR_SPEC:
	case WCFG_BACKGROUND_SPEC:
		break;
	}

	return false;
}

static void store_numeric_value(WConfig *config, const WConfigEntry *entry,
				int value)
{
	char *base = (char *)config + entry->offset;

	switch (entry->type) {
	case WCFG_BOOL:
		assert(entry->size == sizeof(char));
		*(char *)base = value ? 1 : 0;
		break;
	case WCFG_INT:
		assert(entry->size == sizeof(int));
		*(int *)base = value;
		break;
	case WCFG_UINT:
		assert(entry->size == sizeof(unsigned int));
		*(unsigned int *)base = (unsigned int)value;
		break;
	case WCFG_ENUM:
		assert(entry->size == sizeof(signed char));
		*(signed char *)base = (signed char)value;
		break;
	case WCFG_STRING:
	case WCFG_PATH_LIST:
	case WCFG_COORD:
	case WCFG_STRING_ARRAY:
	case WCFG_SWITCH_PANEL_IMAGES:
	case WCFG_KEY_BINDING:
	case WCFG_TEXTURE_SPEC:
	case WCFG_FONT_SPEC:
	case WCFG_COLOR_SPEC:
	case WCFG_CURSOR_SPEC:
	case WCFG_BACKGROUND_SPEC:
		break;
	}
}

static bool parse_numeric_entry(WConfig *config, const WConfigEntry *entry,
				WMPropList *value)
{
	const char *str;
	int parsed;

	str = plist_string_or_default(entry, value);
	if (!parse_value(entry, str, &parsed)) {
		warn_using_default(entry);
		if (!parse_value(entry, entry->default_value, &parsed))
			return false;
	}

	store_numeric_value(config, entry, parsed);
	return true;
}

static bool parse_string_plist(const WConfigEntry *entry, WMPropList *value,
			       char **out)
{
	if (!value || !WMIsPLString(value)) {
		wwarning(_("Wrong option format for key \"%s\". Should be String."),
			 entry->key);
		return false;
	}

	*out = wstrdup(WMGetFromPLString(value));
	return true;
}

static bool parse_path_list_plist(const WConfigEntry *entry, WMPropList *value,
				  char **out)
{
	WMPropList *item;
	char *result, *ptr;
	size_t len = 0;
	int i, count;

	if (!value || !WMIsPLArray(value)) {
		wwarning(_("Wrong option format for key \"%s\". Should be an array of paths."),
			 entry->key);
		return false;
	}

	count = WMGetPropListItemCount(value);
	if (count < 1) {
		wwarning(_("Incorrect number of elements in array for key \"%s\"."),
			 entry->key);
		return false;
	}

	for (i = 0; i < count; i++) {
		size_t item_len;

		item = WMGetFromPLArray(value, i);
		if (!item || !WMIsPLString(item)) {
			wwarning(_("Wrong value for key \"%s\". Should be an array of paths."),
				 entry->key);
			return false;
		}
		item_len = strlen(WMGetFromPLString(item));
		if (SIZE_MAX - len < item_len + 1)
			return false;
		len += item_len + 1;
	}

	result = wmalloc(len);
	ptr = result;
	for (i = 0; i < count; i++) {
		const char *path = WMGetFromPLString(WMGetFromPLArray(value, i));
		size_t item_len = strlen(path);

		memcpy(ptr, path, item_len);
		ptr += item_len;
		if (i + 1 < count)
			*ptr++ = ':';
	}
	*ptr = '\0';

	*out = result;
	return true;
}

static bool parse_coord_plist(const WConfigEntry *entry, WMPropList *value,
			      WConfigCoord *out)
{
	WMPropList *elem_x, *elem_y;
	long x, y;

	if (!value || !WMIsPLArray(value)) {
		wwarning(_("Wrong option format for key \"%s\". Should be Coordinate."),
			 entry->key);
		return false;
	}
	if (WMGetPropListItemCount(value) != 2) {
		wwarning(_("Incorrect number of elements in array for key \"%s\"."),
			 entry->key);
		return false;
	}

	elem_x = WMGetFromPLArray(value, 0);
	elem_y = WMGetFromPLArray(value, 1);
	if (!elem_x || !elem_y || !WMIsPLString(elem_x) || !WMIsPLString(elem_y)) {
		wwarning(_("Wrong value for key \"%s\". Should be Coordinate."),
			 entry->key);
		return false;
	}
	if (!parse_long_strict(WMGetFromPLString(elem_x), &x) ||
	    !parse_long_strict(WMGetFromPLString(elem_y), &y) ||
	    x < INT_MIN || x > INT_MAX || y < INT_MIN || y > INT_MAX) {
		wwarning(_("can't convert array to integers for \"%s\"."), entry->key);
		return false;
	}

	out->x = (int)x;
	out->y = (int)y;
	return true;
}

static void string_array_clear(WConfigStringArray *array)
{
	size_t i;

	if (!array)
		return;

	for (i = 0; i < array->count; i++)
		if (array->items[i])
			wfree(array->items[i]);
	if (array->items)
		wfree(array->items);
	array->count = 0;
	array->items = NULL;
}

static bool parse_string_array_plist(const WConfigEntry *entry, WMPropList *value,
				     WConfigStringArray *out)
{
	WMPropList *item;
	size_t expected, count, i;

	if (!value || !WMIsPLArray(value)) {
		wwarning(_("Wrong option format for key \"%s\". Should be an array of strings."),
			 entry->key);
		return false;
	}

	if (entry->min_value < 0 || entry->min_value > entry->max_value)
		return false;

	expected = (size_t)entry->min_value;
	count = (size_t)WMGetPropListItemCount(value);
	if (count < expected || count > (size_t)entry->max_value) {
		wwarning(_("Incorrect number of elements in array for key \"%s\"."),
			 entry->key);
		return false;
	}

	out->count = count;
	if (count == 0) {
		out->items = NULL;
		return true;
	}
	out->items = wmalloc(count * sizeof(char *));
	memset(out->items, 0, count * sizeof(char *));
	for (i = 0; i < count; i++) {
		item = WMGetFromPLArray(value, (int)i);
		if (!item || !WMIsPLString(item)) {
			wwarning(_("Wrong value for key \"%s\". Should be an array of strings."),
				 entry->key);
			string_array_clear(out);
			return false;
		}
		out->items[i] = wstrdup(WMGetFromPLString(item));
	}

	return true;
}

static void switch_panel_images_clear(WConfigSwitchPanelImages *images)
{
	if (!images)
		return;
	if (images->tile)
		wfree(images->tile);
	if (images->background)
		wfree(images->background);
	images->tile = NULL;
	images->background = NULL;
	images->center_width = 0;
	images->center_height = 0;
}

static bool plist_string_at(WMPropList *array, int index, const char **out)
{
	WMPropList *item;

	item = WMGetFromPLArray(array, index);
	if (!item || !WMIsPLString(item))
		return false;
	*out = WMGetFromPLString(item);
	return true;
}

static bool plist_int_at(WMPropList *array, int index, int *out)
{
	const char *str;
	long value;

	if (!plist_string_at(array, index, &str))
		return false;
	if (!parse_long_strict(str, &value) || value < INT_MIN || value > INT_MAX)
		return false;
	*out = (int)value;
	return true;
}

static bool parse_switch_panel_images_plist(const WConfigEntry *entry,
					    WMPropList *value,
					    WConfigSwitchPanelImages *out)
{
	const char *tile, *background, *legacy_arrow;
	int count, center_width, center_height;

	if (!value || !WMIsPLArray(value)) {
		wwarning(_("Wrong option format for key \"%s\". Should be an array."),
			 entry->key);
		return false;
	}

	count = WMGetPropListItemCount(value);
	if (count == 0)
		return true;

	if (count > 5) {
		wwarning(_("Invalid number of arguments for option \"%s\""),
			 entry->key);
		return false;
	}

	if (!plist_string_at(value, 0, &tile)) {
		wwarning(_("Invalid arguments for option \"%s\""), entry->key);
		return false;
	}
	out->tile = wstrdup(tile);

	if (count == 1)
		return true;

	if (!plist_string_at(value, 1, &background)) {
		wwarning(_("Invalid arguments for option \"%s\""), entry->key);
		switch_panel_images_clear(out);
		return false;
	}
	out->background = wstrdup(background);

	if (count == 2)
		return true;

	/* The former third element, an arrow tile path, is accepted for
	 * compatibility but ignored: the drill marker is drawn onto the
	 * selected tile. */
	if (count == 3 || count == 5) {
		if (!plist_string_at(value, 2, &legacy_arrow)) {
			wwarning(_("Invalid arguments for option \"%s\""), entry->key);
			switch_panel_images_clear(out);
			return false;
		}
		if (count == 3)
			return true;
		if (!plist_int_at(value, 3, &center_width) ||
		    !plist_int_at(value, 4, &center_height)) {
			wwarning(_("Invalid arguments for option \"%s\""), entry->key);
			switch_panel_images_clear(out);
			return false;
		}
		out->center_width = center_width;
		out->center_height = center_height;
		return true;
	}

	/* (tile, background, center_width, center_height) */
	if (!plist_int_at(value, 2, &center_width) ||
	    !plist_int_at(value, 3, &center_height)) {
		wwarning(_("Invalid arguments for option \"%s\""), entry->key);
		switch_panel_images_clear(out);
		return false;
	}
	out->center_width = center_width;
	out->center_height = center_height;
	return true;
}

static bool parse_key_binding_plist(const WConfigEntry *entry, WMPropList *value,
				    WConfigKeyBinding *out)
{
	if (!value || !WMIsPLString(value)) {
		wwarning(_("Wrong option format for key \"%s\". Should be Key spec."),
			 entry->key);
		return false;
	}

	out->spec = wstrdup(WMGetFromPLString(value));
	return true;
}

static bool parse_string_resource_spec_plist(const WConfigEntry *entry,
					     WMPropList *value,
					     WConfigResourceSpec *out)
{
	if (!value || !WMIsPLString(value)) {
		wwarning(_("Wrong option format for key \"%s\". Should be String."),
			 entry->key);
		return false;
	}
	if (entry->type == WCFG_COLOR_SPEC && entry->default_value[0] != '\0' &&
	    WMGetFromPLString(value)[0] == '\0') {
		wwarning(_("Empty color for key \"%s\"."), entry->key);
		return false;
	}

	out->spec = wstrdup(WMGetFromPLString(value));
	return true;
}

static bool plist_first_string_equals(WMPropList *array, const char *expected)
{
	WMPropList *item;

	item = WMGetFromPLArray(array, 0);
	return item && WMIsPLString(item) && WMGetFromPLString(item) &&
	    strcasecmp(WMGetFromPLString(item), expected) == 0;
}

static bool parse_plist_resource_spec_plist(const WConfigEntry *entry,
					    WMPropList *value,
					    WConfigResourceSpec *out)
{
	char *desc;

	if (!value || !WMIsPLArray(value)) {
		wwarning(_("Wrong option format for key \"%s\". Should be an array."),
			 entry->key);
		return false;
	}

	if (entry->type == WCFG_TEXTURE_SPEC &&
	    entry->min_value == WCFG_TEXTURE_WIDGET_COLOR &&
	    !plist_first_string_equals(value, "solid")) {
		wwarning(_("Wrong option format for key \"%s\". Should be %s."),
			 entry->key, "Solid Texture");
		return false;
	}

	if (entry->type == WCFG_BACKGROUND_SPEC) {
		int i, count = WMGetPropListItemCount(value);

		if (entry->min_value == WCFG_BACKGROUND_WORKSPACE && count > 0) {
			WMPropList *item = WMGetFromPLArray(value, 0);

			if (!item || !WMIsPLString(item)) {
				wwarning(_("Wrong type for workspace background. Should be a texture type."));
				return false;
			}
		} else if (entry->min_value == WCFG_BACKGROUND_WORKSPACE_SPECIFIC) {
			for (i = count - 1; i >= 0; i--) {
				WMPropList *item = WMGetFromPLArray(value, i);

				if (!item || !WMIsPLArray(item))
					wwarning(_("Wrong type for background of workspace %i. Should be a texture."),
						 i);
			}
		}
	}

	desc = WMGetPropListDescription(value, false);
	if (!desc)
		return false;
	out->spec = desc;
	return true;
}

static bool parse_owned_plist_value(WConfig *config, const WConfigEntry *entry,
				    WMPropList *value)
{
	char *base = (char *)config + entry->offset;

	switch (entry->type) {
	case WCFG_STRING:
		return parse_string_plist(entry, value, (char **)base);
	case WCFG_PATH_LIST:
		return parse_path_list_plist(entry, value, (char **)base);
	case WCFG_COORD:
		return parse_coord_plist(entry, value, (WConfigCoord *)base);
	case WCFG_STRING_ARRAY:
		return parse_string_array_plist(entry, value,
						(WConfigStringArray *)base);
	case WCFG_SWITCH_PANEL_IMAGES:
		return parse_switch_panel_images_plist(entry, value,
						      (WConfigSwitchPanelImages *)base);
	case WCFG_KEY_BINDING:
		return parse_key_binding_plist(entry, value,
					       (WConfigKeyBinding *)base);
	case WCFG_FONT_SPEC:
	case WCFG_COLOR_SPEC:
		return parse_string_resource_spec_plist(entry, value,
						       (WConfigResourceSpec *)base);
	case WCFG_TEXTURE_SPEC:
	case WCFG_CURSOR_SPEC:
	case WCFG_BACKGROUND_SPEC:
		return parse_plist_resource_spec_plist(entry, value,
						      (WConfigResourceSpec *)base);
	case WCFG_BOOL:
	case WCFG_INT:
	case WCFG_UINT:
	case WCFG_ENUM:
		break;
	}

	return false;
}

static bool parse_plist_with_default(WConfig *config, const WConfigEntry *entry,
				     WMPropList *value)
{
	WMPropList *default_value = NULL;
	bool parsed;

	if (!value) {
		default_value = entry_default_plist(entry);
		value = default_value;
	}

	parsed = parse_owned_plist_value(config, entry, value);
	if (!parsed) {
		if (default_value)
			WMReleasePropList(default_value);
		warn_using_default(entry);
		default_value = entry_default_plist(entry);
		if (!default_value)
			return false;

		parsed = parse_owned_plist_value(config, entry, default_value);
	}

	if (default_value)
		WMReleasePropList(default_value);
	return parsed;
}

static bool parse_entry(WConfig *config, const WConfigEntry *entry,
			WMPropList *value)
{
	switch (entry->type) {
	case WCFG_BOOL:
	case WCFG_INT:
	case WCFG_UINT:
	case WCFG_ENUM:
		return parse_numeric_entry(config, entry, value);
	case WCFG_STRING:
	case WCFG_PATH_LIST:
	case WCFG_COORD:
	case WCFG_STRING_ARRAY:
	case WCFG_SWITCH_PANEL_IMAGES:
	case WCFG_KEY_BINDING:
	case WCFG_TEXTURE_SPEC:
	case WCFG_FONT_SPEC:
	case WCFG_COLOR_SPEC:
	case WCFG_CURSOR_SPEC:
	case WCFG_BACKGROUND_SPEC:
		return parse_plist_with_default(config, entry, value);
	}

	return false;
}

WConfig *wConfigCreateDefault(void)
{
	return wConfigReadWindowMaker(NULL);
}

/* MinimizeAllKey was renamed to MiniaturizeAllKey with no alias */
static void warn_renamed_key(WMPropList *dict)
{
	WMPropList *v;
	const char *s;

	if (!dict)
		return;
	v = dict_get(dict, "MinimizeAllKey");
	if (!v || !WMIsPLString(v))
		return;
	s = WMGetFromPLString(v);
	if (s && *s && strcasecmp(s, "None") != 0)
		wwarning(_("\"MinimizeAllKey\" was renamed to \"MiniaturizeAllKey\"; update your domain"));
}

WConfig *wConfigReadWindowMaker(WMPropList *dict)
{
	WConfig *config;
	const WConfigEntry *schema;
	size_t i, count;

	config = wmalloc(sizeof(WConfig));
	schema = wConfigSchema();
	count = wConfigSchemaCount();
	for (i = 0; i < count; i++)
		parse_entry(config, &schema[i], dict_get_entry(dict, &schema[i]));

	warn_renamed_key(dict);

	return config;
}

static void destroy_entry_value(WConfig *config, const WConfigEntry *entry)
{
	char **value;
	char *base = (char *)config + entry->offset;

	switch (entry->type) {
	case WCFG_STRING:
	case WCFG_PATH_LIST:
		value = (char **)base;
		if (*value)
			wfree(*value);
		break;
	case WCFG_STRING_ARRAY:
		string_array_clear((WConfigStringArray *)base);
		break;
	case WCFG_SWITCH_PANEL_IMAGES:
		switch_panel_images_clear((WConfigSwitchPanelImages *)base);
		break;
	case WCFG_KEY_BINDING:
		if (((WConfigKeyBinding *)base)->spec)
			wfree(((WConfigKeyBinding *)base)->spec);
		break;
	case WCFG_TEXTURE_SPEC:
	case WCFG_FONT_SPEC:
	case WCFG_COLOR_SPEC:
	case WCFG_CURSOR_SPEC:
	case WCFG_BACKGROUND_SPEC:
		if (((WConfigResourceSpec *)base)->spec)
			wfree(((WConfigResourceSpec *)base)->spec);
		break;
	case WCFG_BOOL:
	case WCFG_INT:
	case WCFG_UINT:
	case WCFG_ENUM:
	case WCFG_COORD:
		break;
	}
}

static char *string_dup(const char *value)
{
	return value ? wstrdup(value) : NULL;
}

static void string_array_copy(WConfigStringArray *dst,
			      const WConfigStringArray *src)
{
	size_t i;

	dst->count = src->count;
	dst->items = src->count ? wmalloc(src->count * sizeof(char *)) : NULL;
	for (i = 0; i < src->count; i++)
		dst->items[i] = string_dup(src->items[i]);
}

/* Deep-copy one schema field from src into dst, taking ownership of any
 * heap allocations so dst can be destroyed independently of src. The
 * previous dst value must already have been released. */
static void copy_entry_value(WConfig *dst, const WConfig *src,
			     const WConfigEntry *entry)
{
	char *dst_base = (char *)dst + entry->offset;
	const char *src_base = (const char *)src + entry->offset;
	const WConfigSwitchPanelImages *src_sp;
	WConfigSwitchPanelImages *dst_sp;

	switch (entry->type) {
	case WCFG_STRING:
	case WCFG_PATH_LIST:
		*(char **)dst_base = string_dup(*(char * const *)src_base);
		break;
	case WCFG_STRING_ARRAY:
		string_array_copy((WConfigStringArray *)dst_base,
				  (const WConfigStringArray *)src_base);
		break;
	case WCFG_SWITCH_PANEL_IMAGES:
		src_sp = (const WConfigSwitchPanelImages *)src_base;
		dst_sp = (WConfigSwitchPanelImages *)dst_base;
		dst_sp->tile = string_dup(src_sp->tile);
		dst_sp->background = string_dup(src_sp->background);
		dst_sp->center_width = src_sp->center_width;
		dst_sp->center_height = src_sp->center_height;
		break;
	case WCFG_KEY_BINDING:
		((WConfigKeyBinding *)dst_base)->spec =
		    string_dup(((const WConfigKeyBinding *)src_base)->spec);
		break;
	case WCFG_TEXTURE_SPEC:
	case WCFG_FONT_SPEC:
	case WCFG_COLOR_SPEC:
	case WCFG_CURSOR_SPEC:
	case WCFG_BACKGROUND_SPEC:
		((WConfigResourceSpec *)dst_base)->spec =
		    string_dup(((const WConfigResourceSpec *)src_base)->spec);
		break;
	case WCFG_BOOL:
	case WCFG_INT:
	case WCFG_UINT:
	case WCFG_ENUM:
	case WCFG_COORD:
		memcpy(dst_base, src_base, entry->size);
		break;
	}
}

void wConfigDestroy(WConfig *config)
{
	const WConfigEntry *schema;
	size_t i, count;

	if (!config)
		return;

	schema = wConfigSchema();
	count = wConfigSchemaCount();
	for (i = 0; i < count; i++)
		destroy_entry_value(config, &schema[i]);
	wfree(config);
}

/* Deep-copy a WConfig via the schema's copy_entry_value() per-field helper */
WConfig *wConfigClone(const WConfig *src)
{
	WConfig *dst;
	const WConfigEntry *schema;
	size_t i, count;

	if (!src)
		return NULL;

	dst = wmalloc(sizeof(WConfig));
	schema = wConfigSchema();
	count = wConfigSchemaCount();
	for (i = 0; i < count; i++)
		copy_entry_value(dst, src, &schema[i]);

	return dst;
}

static bool string_values_equal(const char *old_value, const char *new_value)
{
	if (!old_value || !new_value)
		return old_value == new_value;
	return strcmp(old_value, new_value) == 0;
}

static bool string_array_values_equal(const WConfigStringArray *old_array,
				      const WConfigStringArray *new_array)
{
	size_t i;

	if (old_array->count != new_array->count)
		return false;
	for (i = 0; i < old_array->count; i++)
		if (!string_values_equal(old_array->items[i], new_array->items[i]))
			return false;

	return true;
}

static bool switch_panel_images_equal(const WConfigSwitchPanelImages *old_images,
				      const WConfigSwitchPanelImages *new_images)
{
	return string_values_equal(old_images->tile, new_images->tile) &&
	    string_values_equal(old_images->background, new_images->background) &&
	    old_images->center_width == new_images->center_width &&
	    old_images->center_height == new_images->center_height;
}

static bool entry_values_equal(const WConfig *old_config,
			       const WConfig *new_config,
			       const WConfigEntry *entry)
{
	const char *old_base = (const char *)old_config + entry->offset;
	const char *new_base = (const char *)new_config + entry->offset;
	const WConfigCoord *old_coord, *new_coord;
	const WConfigStringArray *old_array, *new_array;
	const WConfigSwitchPanelImages *old_images, *new_images;
	const WConfigResourceSpec *old_spec, *new_spec;

	switch (entry->type) {
	case WCFG_STRING:
	case WCFG_PATH_LIST:
		return string_values_equal(*(char * const *)old_base,
					   *(char * const *)new_base);
	case WCFG_COORD:
		old_coord = (const WConfigCoord *)old_base;
		new_coord = (const WConfigCoord *)new_base;
		return old_coord->x == new_coord->x && old_coord->y == new_coord->y;
	case WCFG_STRING_ARRAY:
		old_array = (const WConfigStringArray *)old_base;
		new_array = (const WConfigStringArray *)new_base;
		return string_array_values_equal(old_array, new_array);
	case WCFG_SWITCH_PANEL_IMAGES:
		old_images = (const WConfigSwitchPanelImages *)old_base;
		new_images = (const WConfigSwitchPanelImages *)new_base;
		return switch_panel_images_equal(old_images, new_images);
	case WCFG_KEY_BINDING:
		return string_values_equal(((const WConfigKeyBinding *)old_base)->spec,
					   ((const WConfigKeyBinding *)new_base)->spec);
	case WCFG_TEXTURE_SPEC:
	case WCFG_FONT_SPEC:
	case WCFG_COLOR_SPEC:
	case WCFG_CURSOR_SPEC:
	case WCFG_BACKGROUND_SPEC:
		old_spec = (const WConfigResourceSpec *)old_base;
		new_spec = (const WConfigResourceSpec *)new_base;
		return string_values_equal(old_spec->spec, new_spec->spec);
	case WCFG_BOOL:
	case WCFG_INT:
	case WCFG_UINT:
	case WCFG_ENUM:
		return memcmp(old_base, new_base, entry->size) == 0;
	}

	return false;
}

bool wConfigDiff(const WConfig *old_config, const WConfig *new_config,
		 WConfigChanges *changes)
{
	const WConfigEntry *schema;
	size_t i, count;

	if (!new_config || !changes)
		return false;

	memset(changes->changed, 0, sizeof(changes->changed));
	schema = wConfigSchema();
	count = wConfigSchemaCount();
	for (i = 0; i < count; i++) {
		if (!old_config || !entry_values_equal(old_config, new_config, &schema[i])) {
			changes->changed[WCFG_CHANGE_WORD(schema[i].config_key)] |=
			    WCFG_CHANGE_BIT(schema[i].config_key);
		}
	}

	return true;
}

bool wConfigChangesHas(const WConfigChanges *changes, WConfigKey key)
{
	if (!changes)
		return true;
	return (changes->changed[WCFG_CHANGE_WORD(key)] & WCFG_CHANGE_BIT(key)) != 0;
}

static unsigned int config_refresh_mask_for_changes(const WConfigChanges *changes)
{
	const WConfigEntry *schema;
	size_t i, count;
	unsigned int refresh = 0;

	schema = wConfigSchema();
	count = wConfigSchemaCount();
	for (i = 0; i < count; i++) {
		if ((schema[i].flags & WCFG_RUNTIME_RELOAD) &&
		    wConfigChangesHas(changes, schema[i].config_key))
			refresh |= schema[i].refresh_mask;
	}

	return refresh;
}

static WConfigCoord clamp_window_place_origin(WScreen *scr, WConfigCoord coord)
{
	if (!scr)
		return coord;

	if (coord.x < 0)
		coord.x = 0;
	else if (coord.x > scr->scr_width / 3)
		coord.x = scr->scr_width / 3;
	if (coord.y < 0)
		coord.y = 0;
	else if (coord.y > scr->scr_height / 3)
		coord.y = scr->scr_height / 3;

	return coord;
}

void wConfigApplyStatic(const WConfig *config)
{
	if (!config)
		return;

	/* Only the derived startup state remains here, plain config values
	 * are read directly from w_global.config now. ModifierKey is resolved
	 * on the runtime path (WCFG_KEY_MODIFIER_KEY) so it runs after
	 * wXkbInit() during the first per-screen apply and can re-grab on a
	 * live change. */
	wConfigApplyTabDragModifier(config->tab_drag_modifier);
	wdisp->flags.nodock = wdisp->flags.nodock || config->disable_dock;
	wdisp->flags.nodrawer = wdisp->flags.nodrawer ||
	    wdisp->flags.nodock || config->disable_drawers;
	wdisp->flags.noclip = wdisp->flags.noclip || config->disable_clip;
	wdisp->flags.clip_merged_in_dock = config->clip_merged_in_dock;
	wdisp->flags.noclip = wdisp->flags.noclip ||
	    config->clip_merged_in_dock;
}

unsigned int wConfigApplyRuntime(WScreen *scr, const WConfig *old_config,
				 const WConfig *new_config,
				 const WConfigChanges *changes)
{
	bool update_usable_area = false;
	bool update_root_mask = false;
	bool update_hot_corners = false;
	bool update_findmouse = false;
	bool arrange_all_icons = false;
	bool sticky_icons_changed = false;
	bool keybindings_changed = false;
	bool workspace_back_force = false;
	bool workspace_back_requested;
	bool workspace_back_actual;
	bool pixmap_path_changed;
	unsigned int refresh;
	const WConfigEntry *schema;
	size_t i, count;

	if (!new_config)
		return 0;

	if (scr && wConfigChangesHas(changes, WCFG_KEY_FRAME_INNER_BORDER_WIDTH))
		scr->frame_inner_border_width = new_config->frame_inner_border_width;

	if (scr && wConfigChangesHas(changes, WCFG_KEY_FRAME_INNER_BORDER_SIDES))
		scr->frame_inner_border_sides =
			wFrameInnerBorderSidesFromArray(&new_config->frame_inner_border_sides);

	refresh = config_refresh_mask_for_changes(changes);
	pixmap_path_changed = wConfigChangesHas(changes, WCFG_KEY_PIXMAP_PATH);

	if (wConfigChangesHas(changes, WCFG_KEY_ICON_POSITION)) {
		update_usable_area = true;
		arrange_all_icons = true;
	}
	if (wConfigChangesHas(changes, WCFG_KEY_STICKY_ICONS))
		sticky_icons_changed = true;
	if (wConfigChangesHas(changes, WCFG_KEY_WRAP_APPICONS_IN_DOCK))
		wdisp->flags.wrap_appicons_in_dock =
		    new_config->wrap_appicons_in_dock;
	if (wConfigChangesHas(changes, WCFG_KEY_WINDOW_PLACE_ORIGIN) && scr) {
		WConfigCoord coord = clamp_window_place_origin(scr,
								 new_config->window_place_origin);

		scr->window_place_origin.x = coord.x;
		scr->window_place_origin.y = coord.y;
	}
	if (wConfigChangesHas(changes, WCFG_KEY_KEEP_DOCK_ON_PRIMARY_HEAD)) {
		if (scr && scr->dock)
			wDockReanchor(scr->dock);
	}
	if (wConfigChangesHas(changes, WCFG_KEY_DOCK_EDGE)) {
		if (scr && scr->dock)
			wDockSetEdge(scr->dock, new_config->dock_edge);
	}
	if (wConfigChangesHas(changes, WCFG_KEY_WINDOW_GAP))
		update_usable_area = true;
	if (wConfigChangesHas(changes, WCFG_KEY_NO_WINDOW_OVER_DOCK))
		update_usable_area = true;
	if (wConfigChangesHas(changes, WCFG_KEY_NO_WINDOW_OVER_ICONS))
		update_usable_area = true;
	if (wConfigChangesHas(changes, WCFG_KEY_HOT_CORNERS)) {
		update_root_mask = true;
		update_hot_corners = true;
	}
	if (wConfigChangesHas(changes, WCFG_KEY_HOT_CORNER_EDGE))
		update_hot_corners = true;
	if (wConfigChangesHas(changes, WCFG_KEY_FIND_MOUSE_DURATION) ||
	    wConfigChangesHas(changes, WCFG_KEY_FIND_MOUSE_RADIUS))
		update_findmouse = true;
	if (wConfigChangesHas(changes, WCFG_KEY_WORKSPACE_BORDER))
		update_usable_area = true;
	if (wConfigChangesHas(changes, WCFG_KEY_WORKSPACE_BORDER_SIZE))
		update_usable_area = true;
#ifdef VIRTUAL_DESKTOP
	if (wConfigChangesHas(changes, WCFG_KEY_ENABLE_VIRTUAL_DESKTOP) ||
	    wConfigChangesHas(changes, WCFG_KEY_VIRTUALEDGE_BORDERSIZE) ||
	    wConfigChangesHas(changes, WCFG_KEY_VIRTUALEDGE_RESISTANCE) ||
	    wConfigChangesHas(changes, WCFG_KEY_VIRTUALEDGE_ATTRACTION)) {
		if (scr)
			wWorkspaceUpdateEdge(scr);
	}
#endif
	if (wConfigChangesHas(changes, WCFG_KEY_SCROLLABLE_MENUS))
		update_root_mask = true;
	if (wConfigChangesHas(changes, WCFG_KEY_KEYCHAIN_DISPLAY_POSITION) && scr)
		wRefreshKeyChainOSD(scr);
	if (wConfigChangesHas(changes, WCFG_KEY_DOUBLE_CLICK_TIME))
		W_setconf_doubleClickDelay(new_config->double_click_time);
	if (scr) {
		double uiscale = WMScreenUIScale(scr->wmscreen);

		if (wConfigChangesHas(changes, WCFG_KEY_FRAME_BORDER_WIDTH)) {
			double scaled = new_config->frame_border_width * uiscale + 0.5;

			if (scaled > 0xffff)
				scaled = 0xffff;
			scr->frame_border_width = (int)scaled;
		}
		if (!old_config) {
			scr->resizebar_height =
			    (int)(RESIZEBAR_HEIGHT * uiscale + 0.5);
			scr->resizebar_corner_width =
			    (int)(RESIZEBAR_CORNER_WIDTH * uiscale + 0.5);
		}
	}

	workspace_back_requested =
	    wConfigChangesHas(changes, WCFG_KEY_WORKSPACE_BACK);
	workspace_back_actual = !old_config ||
	    !string_values_equal(old_config->workspace_back.spec,
				 new_config->workspace_back.spec);
	if (wConfigChangesHas(changes, WCFG_KEY_WORKSPACE_SPECIFIC_BACK)) {
		bool helper_needed = scr && !scr->flags.backimage_helper_launched;

		refresh |= wConfigApplyWorkspaceBackgroundSpec(scr,
		    "WorkspaceSpecificBack", WCFG_BACKGROUND_WORKSPACE_SPECIFIC,
		    &new_config->workspace_specific_back, "()");
		workspace_back_force = helper_needed && scr &&
		    scr->flags.backimage_helper_launched;
	}
	if (workspace_back_requested && (workspace_back_actual ||
					workspace_back_force))
		refresh |= wConfigApplyWorkspaceBackgroundSpec(scr, "WorkspaceBack",
		    WCFG_BACKGROUND_WORKSPACE, &new_config->workspace_back,
		    "(solid, \"rgb:50/50/75\")");

	if (wConfigChangesHas(changes, WCFG_KEY_HOT_CORNER_ACTIONS))
		wConfigApplyHotCornerActions(scr, &new_config->hot_corner_actions);
	if (wConfigChangesHas(changes, WCFG_KEY_SWITCH_PANEL_IMAGES) ||
	    pixmap_path_changed)
		wConfigApplySwitchPanelImages(scr, &new_config->switch_panel_images);
	if (scr && pixmap_path_changed && scr->flags.backimage_helper_launched) {
		/* the wmsetbg helper is only told PixmapPath at launch, so a
		 * running helper would keep resolving background pixmap files
		 * against the old search path until restarted */
		SendHelperMessage(scr, 'P', -1, new_config->pixmap_path);
	}
	if (wConfigChangesHas(changes, WCFG_KEY_TAB_DRAG_MODIFIER))
		wConfigApplyTabDragModifier(new_config->tab_drag_modifier);
	if (wConfigChangesHas(changes, WCFG_KEY_MODIFIER_KEY)) {
		/* Re-resolve the named modifier to a mask and re-issue the
		 * passive button grabs that depend on it. Key grabs use the
		 * per-shortcut modifier resolved from the binding spec, not
		 * wdisp->modifier_mask, so they need no re-grab here. */
		wConfigApplyModifierKey(new_config->modifier_key, "Mod1");
		if (scr)
			wRegrabWindowMouseGrabs(scr);
	}
	if (scr && scr->icon_tile
	    && wConfigChangesHas(changes, WCFG_KEY_ICON_SIZE)) {
		/* re-derive every cached IconSize derivative: per-screen
		 * windows and pixmaps first, then the icon/clip/drawer tile
		 * (so wIconResizeAll() composites onto the new tile), then
		 * every icon core and image, then the dock and clip slot
		 * positions. wScreenUpdateUsableArea() and wArrangeIcons()
		 * run from the tail of this function. scr->icon_tile gates
		 * out the cold-boot pass where the texture loop below sets
		 * it for the first time. */
		wScreenApplyIconSize(scr);
		refresh |= wConfigForceReapplyIconBack(scr);
		wIconResizeAll(scr);
		wDockApplyIconSize(scr);
		update_usable_area = true;
		arrange_all_icons = true;
	}
	if (scr && wConfigChangesHas(changes, WCFG_KEY_DISABLE_MINIWINDOWS))
		wRefreshMiniwindows(scr);
	if (scr && wConfigChangesHas(changes, WCFG_KEY_BLINK_TITLEBAR_WHEN_URGENT)) {
		WWindow *wwin = scr->focused_window;

		while (wwin) {
			wWindowUrgentBlink(wwin);
			wwin = wwin->prev;
		}
	}
	if (wConfigChangesHas(changes, WCFG_KEY_MODIFIER_KEY_LABELS))
		wConfigApplyModifierKeyLabels(scr, &new_config->modifier_key_labels);
	if (wConfigChangesHas(changes, WCFG_KEY_MODIFIER_KEY_SHORT_LABELS))
		wConfigApplyModifierKeyShortLabels(scr,
						&new_config->modifier_key_short_labels);


	schema = wConfigSchema();
	count = wConfigSchemaCount();

	for (i = 0; i < count; i++) {
		const WConfigResourceSpec *spec;
		bool changed;

		changed = wConfigChangesHas(changes, schema[i].config_key);
		/* Texture and cursor specs may reference pixmap files by name,
		 * resolved against PixmapPath at apply time and cached for the
		 * session. Force a re-apply when PixmapPath changes so a
		 * previously unresolved file is retried under the new search
		 * path and a theme edit that only touches PixmapPath still
		 * repaints. */
		if (pixmap_path_changed && (schema[i].type == WCFG_TEXTURE_SPEC ||
					    schema[i].type == WCFG_CURSOR_SPEC))
			changed = true;
		if (!changed)
			continue;

		spec = (const WConfigResourceSpec *)
		    ((const char *)new_config + schema[i].offset);
		switch (schema[i].type) {
		case WCFG_TEXTURE_SPEC:
			refresh |= wConfigApplyTextureSpec(scr, schema[i].key,
							  schema[i].min_value, spec,
							  schema[i].default_value);
			break;
		case WCFG_FONT_SPEC:
			refresh |= wConfigApplyFontSpec(scr, schema[i].key,
						       schema[i].min_value, spec,
						       schema[i].default_value);
			break;
		case WCFG_COLOR_SPEC:
			refresh |= wConfigApplyColorSpec(scr, schema[i].key,
							schema[i].min_value, spec,
							schema[i].default_value);
			break;
		case WCFG_CURSOR_SPEC:
			refresh |= wConfigApplyCursorSpec(scr, schema[i].key,
							 schema[i].min_value, spec,
							 schema[i].default_value);
			break;
		case WCFG_BACKGROUND_SPEC:
			break;
		case WCFG_BOOL:
		case WCFG_INT:
		case WCFG_UINT:
		case WCFG_ENUM:
		case WCFG_STRING:
		case WCFG_PATH_LIST:
		case WCFG_COORD:
		case WCFG_STRING_ARRAY:
		case WCFG_SWITCH_PANEL_IMAGES:
		case WCFG_KEY_BINDING:
			break;
		}
	}

	for (i = 0; i < count; i++) {
		const WConfigKeyBinding *binding;
		bool binding_changed = false;

		if (schema[i].type != WCFG_KEY_BINDING ||
		    !wConfigChangesHas(changes, schema[i].config_key))
			continue;

		binding = (const WConfigKeyBinding *)
		    ((const char *)new_config + schema[i].offset);
		refresh |= wConfigApplyKeyBinding(scr, schema[i].key,
						  schema[i].min_value, binding->spec,
						  schema[i].default_value,
						  &binding_changed);
		/* Key off the change set, not binding_changed: the globals
		 * are already updated for the second screen's pass. */
		keybindings_changed = true;
	}

	if (keybindings_changed) {
		wRegrabFocusedWindowKeys(scr);
		if (scr)
			wKeyTreeRebuild(scr);
	}

	/* AntialiasedText lives in the WMGLOBAL domain (not a WCFG_SCHEMA
	 * key), so it has no change bit; re-read it on every reload and, if
	 * it actually changed, detach the WINGs font cache so fonts re-open
	 * under the new flag and re-apply the WM-side font pointers. */
	refresh |= wConfigApplyAntialiasedText(scr);

	refresh |= wConfigApplySystemColors(scr);

	if (scr && update_usable_area)
		wScreenUpdateUsableArea(scr);
	if (scr && arrange_all_icons)
		wArrangeIcons(scr, true);
	if (scr && sticky_icons_changed && scr->workspaces) {
		wWorkspaceForceChange(scr, scr->current_workspace);
		wArrangeIcons(scr, false);
	}
	if (scr && update_root_mask)
		wScreenUpdateRootEventMask(scr);
	if (scr && update_hot_corners)
		wHotCornersUpdate(scr);
	if (scr && update_findmouse)
		wFindMouseApplyConfig(scr);

	return refresh;
}

void wConfigAdoptWindowMaker(WConfig *config)
{
	if (w_global.config == config)
		return;
	wConfigDestroy(w_global.config);
	w_global.config = config;
	wdisp->config = config;
}

void wConfigCacheWindowMaker(WMPropList *dict)
{
	wConfigAdoptWindowMaker(wConfigReadWindowMaker(dict));
}

void wConfigClearWindowMaker(void)
{
	/* Test teardown helper; production reloads adopt a replacement config. */
	wConfigDestroy(w_global.config);
	w_global.config = NULL;
	wdisp->config = NULL;
}

void wConfigApplyStaticDefaults(WMPropList *dict)
{
	/* Test helper, mirrors the StartUp() ordering so check_config can
	 * inspect the derived flags and the adopted WConfig together. */
	wConfigAdoptWindowMaker(wConfigReadWindowMaker(dict));
	wConfigApplyStatic(w_global.config);
}

void wConfigPrepareRuntimeDefaults(WMPropList *new_dict,
				   const WConfig *old_config,
				   WConfig *new_config,
				   WConfigChanges *changes)
{
	const WConfigEntry *schema;
	size_t i, count;

	if (!changes)
		return;
	memset(changes->changed, 0, sizeof(changes->changed));
	if (!new_config)
		return;

	wConfigDiff(old_config, new_config, changes);

	schema = wConfigSchema();
	count = wConfigSchemaCount();
	for (i = 0; i < count; i++) {
		WMPropList *new_value;

		if (!(schema[i].flags & WCFG_RUNTIME_RELOAD)) {
			/* Startup-only key, freeze it for the session by
			 * carrying the previously adopted (and possibly
			 * sanity-clamped) value forward, regardless of what
			 * the reloaded file now says, so readers that go
			 * straight to w_global.config never see a mid-session
			 * change to IconSize, FocusMode, NewStyle, etc. */
			if (old_config) {
				destroy_entry_value(new_config, &schema[i]);
				copy_entry_value(new_config, old_config, &schema[i]);
				changes->changed[WCFG_CHANGE_WORD(schema[i].config_key)] &=
				    ~WCFG_CHANGE_BIT(schema[i].config_key);
			}
			continue;
		}

		new_value = dict_get_entry(new_dict, &schema[i]);
		if (!new_value && old_config) {
			/* Key was removed at runtime, keep the value the
			 * running session already has rather than reverting
			 * to the schema default, and suppress the change so
			 * no refresh side effect fires. */
			destroy_entry_value(new_config, &schema[i]);
			copy_entry_value(new_config, old_config, &schema[i]);
			changes->changed[WCFG_CHANGE_WORD(schema[i].config_key)] &=
			    ~WCFG_CHANGE_BIT(schema[i].config_key);
		} else if (!new_value && !old_config) {
			dict_put_default(new_dict, &schema[i]);
		}
	}

	if (wConfigChangesHas(changes, WCFG_KEY_WORKSPACE_SPECIFIC_BACK)) {
		WMPropList *new_value;

		new_value = dict_get(new_dict, "WorkspaceBack");
		if (new_value || !old_config)
			changes->changed[WCFG_CHANGE_WORD(WCFG_KEY_WORKSPACE_BACK)] |=
			    WCFG_CHANGE_BIT(WCFG_KEY_WORKSPACE_BACK);
	}
}

unsigned int wConfigApplyRuntimeDefaults(WScreen *scr, WMPropList *new_dict)
{
	WConfig *old_config;
	WConfig *new_config;
	WConfigChanges changes;
	unsigned int refresh;

	/* Test helper, mirrors the runtime-reload ordering in
	 * wDefaultsCheckDomains() so check_config sees the new values via
	 * w_global.config while side-effect handlers run. */
	new_config = wConfigReadWindowMaker(new_dict);
	old_config = w_global.config;
	wConfigPrepareRuntimeDefaults(new_dict, old_config, new_config, &changes);
	w_global.config = new_config;
	wdisp->config = new_config;
	refresh = wConfigApplyRuntime(scr, old_config, new_config, &changes);
	wConfigDestroy(old_config);

	return refresh;
}

/* Re-apply every FONT_SPEC entry from the adopted config, returning the
 * combined refresh mask. Used after the WINGs font cache is detached so
 * each WM-side font pointer is re-opened under the current antialias flag;
 * the per-font set*Font callbacks release the previously detached handle
 * through normal refcounting and store the new one. */
unsigned int wConfigForceReapplyIconBack(WScreen *scr)
{
	const WConfigEntry *schema;
	const WConfig *config;
	size_t i, count;

	if (!scr)
		return 0;
	config = w_global.config;
	if (!config)
		return 0;

	schema = wConfigSchema();
	count = wConfigSchemaCount();
	for (i = 0; i < count; i++) {
		if (schema[i].config_key != WCFG_KEY_ICON_BACK)
			continue;
		return wConfigApplyTextureSpec(scr, schema[i].key,
					       schema[i].min_value,
					       &config->icon_back,
					       schema[i].default_value);
	}
	return 0;
}

unsigned int wConfigForceReapplyFonts(WScreen *scr)
{
	const WConfigEntry *schema;
	const WConfig *config;
	const char *base;
	size_t i, count;
	unsigned int refresh = 0;

	if (!scr)
		return 0;
	config = w_global.config;
	if (!config)
		return 0;

	schema = wConfigSchema();
	count = wConfigSchemaCount();
	base = (const char *)config;
	for (i = 0; i < count; i++) {
		const WConfigResourceSpec *spec;

		if (schema[i].type != WCFG_FONT_SPEC)
			continue;
		spec = (const WConfigResourceSpec *)(base + schema[i].offset);
		refresh |= wConfigApplyFontSpec(scr, schema[i].key,
						schema[i].min_value, spec,
						schema[i].default_value);
	}
	return refresh;
}

/* AntialiasedText is a WMGLOBAL-domain bool (not a WCFG_SCHEMA key), read
 * once by WINGs at font open time and baked into the cache key. WMReload-
 * Configuration() re-reads it when the WMGLOBAL file changes; here we
 * detect a change against the value this screen's fonts were last opened
 * under, and if it differs, detach the WINGs font cache and re-open the
 * WM-side fonts so the new flag takes effect without a restart. */
unsigned int wConfigApplyAntialiasedText(WScreen *scr)
{
	bool current;
	unsigned int refresh;

	if (!scr || !scr->wmscreen)
		return 0;

	current = WMGetAntialiasedText();
	if (scr->antialiased_text_applied == current)
		return 0;
	scr->antialiased_text_applied = current;

	/* Detach the cache (holders keep valid references and release them via
	 * refcounting; the empty cache forces the next WMCreateFont to re-open
	 * under the new flag) and re-acquire the per-screen system fonts. */
	WMFlushFontCache(scr->wmscreen);

	/* Re-open the WM-side config fonts under the new flag. */
	refresh = wConfigForceReapplyFonts(scr);

	/* info_text_font is a WINGs system font (no FONT_SPEC), so re-open it
	 * explicitly; the old handle was detached above. */
	if (scr->info_text_font) {
		WMReleaseFont(scr->info_text_font);
		scr->info_text_font = NULL;
	}
	scr->info_text_font = WMBoldSystemFontOfSize(scr->wmscreen, 12);

	return refresh;
}

unsigned int wConfigApplySystemColors(WScreen *scr)
{
	if (!scr || !scr->wmscreen)
		return 0;

	return WMRefreshSystemColors(scr->wmscreen) ? WCFG_REFRESH_SYSTEM_COLOR : 0;
}
