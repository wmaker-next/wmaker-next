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
#include "screen.h"
#include "workspace.h"
#include "config-schema.h"
#include "wconfigmodel.h"

void wKeyTreeRebuild(WScreen *scr);

static WMPropList *dict_get(WMPropList *dict, const char *key)
{
	WMPropList *plkey, *value;

	if (!dict)
		return NULL;

	plkey = WMCreatePLString(key);
	value = WMGetFromPLDictionary(dict, plkey);
	WMReleasePropList(plkey);

	return value;
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

static Bool parse_long_strict(const char *str, long *out)
{
	char *end;
	long value;

	errno = 0;
	value = strtol(str, &end, 0);
	if (errno != 0 || end == str)
		return False;
	while (isspace((unsigned char)*end))
		end++;
	if (*end != '\0')
		return False;

	*out = value;
	return True;
}

static Bool parse_bool(const WConfigEntry *entry, const char *str, int *out)
{
	long value;

	if (((str[0] == 'y' || str[0] == 'Y') && str[1] == '\0') ||
	    strcasecmp(str, "YES") == 0) {
		*out = 1;
		return True;
	}
	if (((str[0] == 'n' || str[0] == 'N') && str[1] == '\0') ||
	    strcasecmp(str, "NO") == 0) {
		*out = 0;
		return True;
	}
	if (parse_long_strict(str, &value)) {
		*out = (value != 0);
		return True;
	}

	wwarning(_("can't convert \"%s\" to boolean for key \"%s\""),
		 str, entry->key);
	return False;
}

static Bool parse_int(const WConfigEntry *entry, const char *str, int *out)
{
	long value;

	if (!parse_long_strict(str, &value)) {
		wwarning(_("can't convert \"%s\" to integer for key \"%s\""),
			 str, entry->key);
		return False;
	}
	if (value < entry->min_value || value > entry->max_value) {
		wwarning(_("integer value \"%s\" for key \"%s\" is outside [%d..%d]"),
			 str, entry->key, entry->min_value, entry->max_value);
		return False;
	}

	*out = (int)value;
	return True;
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

static Bool parse_enum(const WConfigEntry *entry, const char *str, int *out)
{
	const WConfigEnum *v;
	char buffer[256];

	for (v = entry->enum_values; v && v->string != NULL; v++) {
		if (strcasecmp(v->string, str) == 0) {
			*out = v->value;
			return True;
		}
	}

	enum_values_description(entry->enum_values, buffer, sizeof(buffer));
	wwarning(_("wrong option value for key \"%s\"; got \"%s\", should be one of %s."),
		 entry->key, str, buffer);
	return False;
}

static Bool parse_value(const WConfigEntry *entry, const char *str, int *out)
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

	return False;
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

static Bool parse_numeric_entry(WConfig *config, const WConfigEntry *entry,
				WMPropList *value)
{
	const char *str;
	int parsed;

	str = plist_string_or_default(entry, value);
	if (!parse_value(entry, str, &parsed)) {
		warn_using_default(entry);
		if (!parse_value(entry, entry->default_value, &parsed))
			return False;
	}

	store_numeric_value(config, entry, parsed);
	return True;
}

static Bool parse_string_plist(const WConfigEntry *entry, WMPropList *value,
			       char **out)
{
	if (!value || !WMIsPLString(value)) {
		wwarning(_("Wrong option format for key \"%s\". Should be String."),
			 entry->key);
		return False;
	}

	*out = wstrdup(WMGetFromPLString(value));
	return True;
}

static Bool parse_path_list_plist(const WConfigEntry *entry, WMPropList *value,
				  char **out)
{
	WMPropList *item;
	char *result, *ptr;
	size_t len = 0;
	int i, count;

	if (!value || !WMIsPLArray(value)) {
		wwarning(_("Wrong option format for key \"%s\". Should be an array of paths."),
			 entry->key);
		return False;
	}

	count = WMGetPropListItemCount(value);
	if (count < 1) {
		wwarning(_("Incorrect number of elements in array for key \"%s\"."),
			 entry->key);
		return False;
	}

	for (i = 0; i < count; i++) {
		size_t item_len;

		item = WMGetFromPLArray(value, i);
		if (!item || !WMIsPLString(item)) {
			wwarning(_("Wrong value for key \"%s\". Should be an array of paths."),
				 entry->key);
			return False;
		}
		item_len = strlen(WMGetFromPLString(item));
		if (SIZE_MAX - len < item_len + 1)
			return False;
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
	return True;
}

static Bool parse_coord_plist(const WConfigEntry *entry, WMPropList *value,
			      WConfigCoord *out)
{
	WMPropList *elem_x, *elem_y;
	long x, y;

	if (!value || !WMIsPLArray(value)) {
		wwarning(_("Wrong option format for key \"%s\". Should be Coordinate."),
			 entry->key);
		return False;
	}
	if (WMGetPropListItemCount(value) != 2) {
		wwarning(_("Incorrect number of elements in array for key \"%s\"."),
			 entry->key);
		return False;
	}

	elem_x = WMGetFromPLArray(value, 0);
	elem_y = WMGetFromPLArray(value, 1);
	if (!elem_x || !elem_y || !WMIsPLString(elem_x) || !WMIsPLString(elem_y)) {
		wwarning(_("Wrong value for key \"%s\". Should be Coordinate."),
			 entry->key);
		return False;
	}
	if (!parse_long_strict(WMGetFromPLString(elem_x), &x) ||
	    !parse_long_strict(WMGetFromPLString(elem_y), &y) ||
	    x < INT_MIN || x > INT_MAX || y < INT_MIN || y > INT_MAX) {
		wwarning(_("can't convert array to integers for \"%s\"."), entry->key);
		return False;
	}

	out->x = (int)x;
	out->y = (int)y;
	return True;
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

static Bool parse_string_array_plist(const WConfigEntry *entry, WMPropList *value,
				     WConfigStringArray *out)
{
	WMPropList *item;
	size_t expected, count, i;

	if (!value || !WMIsPLArray(value)) {
		wwarning(_("Wrong option format for key \"%s\". Should be an array of strings."),
			 entry->key);
		return False;
	}

	if (entry->min_value < 0 || entry->min_value != entry->max_value)
		return False;

	expected = (size_t)entry->min_value;
	count = (size_t)WMGetPropListItemCount(value);
	if (count != expected) {
		wwarning(_("Incorrect number of elements in array for key \"%s\"."),
			 entry->key);
		return False;
	}

	out->count = count;
	if (count == 0) {
		out->items = NULL;
		return True;
	}
	out->items = wmalloc(count * sizeof(char *));
	memset(out->items, 0, count * sizeof(char *));
	for (i = 0; i < count; i++) {
		item = WMGetFromPLArray(value, (int)i);
		if (!item || !WMIsPLString(item)) {
			wwarning(_("Wrong value for key \"%s\". Should be an array of strings."),
				 entry->key);
			string_array_clear(out);
			return False;
		}
		out->items[i] = wstrdup(WMGetFromPLString(item));
	}

	return True;
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

static Bool plist_string_at(WMPropList *array, int index, const char **out)
{
	WMPropList *item;

	item = WMGetFromPLArray(array, index);
	if (!item || !WMIsPLString(item))
		return False;
	*out = WMGetFromPLString(item);
	return True;
}

static Bool plist_int_at(WMPropList *array, int index, int *out)
{
	const char *str;
	long value;

	if (!plist_string_at(array, index, &str))
		return False;
	if (!parse_long_strict(str, &value) || value < INT_MIN || value > INT_MAX)
		return False;
	*out = (int)value;
	return True;
}

static Bool parse_switch_panel_images_plist(const WConfigEntry *entry,
					    WMPropList *value,
					    WConfigSwitchPanelImages *out)
{
	const char *tile, *background;
	int count, center_width, center_height;

	if (!value || !WMIsPLArray(value)) {
		wwarning(_("Wrong option format for key \"%s\". Should be an array."),
			 entry->key);
		return False;
	}

	count = WMGetPropListItemCount(value);
	if (count == 0)
		return True;

	if (count != 1 && count != 4) {
		wwarning(_("Invalid number of arguments for option \"%s\""),
			 entry->key);
		return False;
	}

	if (!plist_string_at(value, 0, &tile)) {
		wwarning(_("Invalid arguments for option \"%s\""), entry->key);
		return False;
	}
	out->tile = wstrdup(tile);

	if (count == 1)
		return True;

	if (!plist_string_at(value, 1, &background) ||
	    !plist_int_at(value, 2, &center_width) ||
	    !plist_int_at(value, 3, &center_height)) {
		wwarning(_("Invalid arguments for option \"%s\""), entry->key);
		switch_panel_images_clear(out);
		return False;
	}

	out->background = wstrdup(background);
	out->center_width = center_width;
	out->center_height = center_height;
	return True;
}

static Bool parse_key_binding_plist(const WConfigEntry *entry, WMPropList *value,
				    WConfigKeyBinding *out)
{
	if (!value || !WMIsPLString(value)) {
		wwarning(_("Wrong option format for key \"%s\". Should be Key spec."),
			 entry->key);
		return False;
	}

	out->spec = wstrdup(WMGetFromPLString(value));
	return True;
}

static Bool parse_string_resource_spec_plist(const WConfigEntry *entry,
					     WMPropList *value,
					     WConfigResourceSpec *out)
{
	if (!value || !WMIsPLString(value)) {
		wwarning(_("Wrong option format for key \"%s\". Should be String."),
			 entry->key);
		return False;
	}

	out->spec = wstrdup(WMGetFromPLString(value));
	return True;
}

static Bool plist_first_string_equals(WMPropList *array, const char *expected)
{
	WMPropList *item;

	item = WMGetFromPLArray(array, 0);
	return item && WMIsPLString(item) && WMGetFromPLString(item) &&
	    strcasecmp(WMGetFromPLString(item), expected) == 0;
}

static Bool parse_plist_resource_spec_plist(const WConfigEntry *entry,
					    WMPropList *value,
					    WConfigResourceSpec *out)
{
	char *desc;

	if (!value || !WMIsPLArray(value)) {
		wwarning(_("Wrong option format for key \"%s\". Should be an array."),
			 entry->key);
		return False;
	}

	if (entry->type == WCFG_TEXTURE_SPEC &&
	    entry->min_value == WCFG_TEXTURE_WIDGET_COLOR &&
	    !plist_first_string_equals(value, "solid")) {
		wwarning(_("Wrong option format for key \"%s\". Should be %s."),
			 entry->key, "Solid Texture");
		return False;
	}

	if (entry->type == WCFG_BACKGROUND_SPEC) {
		int i, count = WMGetPropListItemCount(value);

		if (entry->min_value == WCFG_BACKGROUND_WORKSPACE && count > 0) {
			WMPropList *item = WMGetFromPLArray(value, 0);

			if (!item || !WMIsPLString(item)) {
				wwarning(_("Wrong type for workspace background. Should be a texture type."));
				return False;
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

	desc = WMGetPropListDescription(value, False);
	if (!desc)
		return False;
	out->spec = desc;
	return True;
}

static Bool parse_owned_plist_value(WConfig *config, const WConfigEntry *entry,
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

	return False;
}

static Bool parse_plist_with_default(WConfig *config, const WConfigEntry *entry,
				     WMPropList *value)
{
	WMPropList *default_value = NULL;
	Bool parsed;

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
			return False;

		parsed = parse_owned_plist_value(config, entry, default_value);
	}

	if (default_value)
		WMReleasePropList(default_value);
	return parsed;
}

static Bool parse_entry(WConfig *config, const WConfigEntry *entry,
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

	return False;
}

WConfig *wConfigCreateDefault(void)
{
	return wConfigReadWindowMaker(NULL);
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

static Bool string_values_equal(const char *old_value, const char *new_value)
{
	if (!old_value || !new_value)
		return old_value == new_value;
	return strcmp(old_value, new_value) == 0;
}

static Bool string_array_values_equal(const WConfigStringArray *old_array,
				      const WConfigStringArray *new_array)
{
	size_t i;

	if (old_array->count != new_array->count)
		return False;
	for (i = 0; i < old_array->count; i++)
		if (!string_values_equal(old_array->items[i], new_array->items[i]))
			return False;

	return True;
}

static Bool switch_panel_images_equal(const WConfigSwitchPanelImages *old_images,
				      const WConfigSwitchPanelImages *new_images)
{
	return string_values_equal(old_images->tile, new_images->tile) &&
	    string_values_equal(old_images->background, new_images->background) &&
	    old_images->center_width == new_images->center_width &&
	    old_images->center_height == new_images->center_height;
}

static Bool entry_values_equal(const WConfig *old_config,
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

	return False;
}

Bool wConfigDiff(const WConfig *old_config, const WConfig *new_config,
		 WConfigChanges *changes)
{
	const WConfigEntry *schema;
	size_t i, count;

	if (!new_config || !changes)
		return False;

	memset(changes->changed, 0, sizeof(changes->changed));
	schema = wConfigSchema();
	count = wConfigSchemaCount();
	for (i = 0; i < count; i++) {
		if (!old_config || !entry_values_equal(old_config, new_config, &schema[i])) {
			changes->changed[WCFG_CHANGE_WORD(schema[i].config_key)] |=
			    WCFG_CHANGE_BIT(schema[i].config_key);
		}
	}

	return True;
}

Bool wConfigChangesHas(const WConfigChanges *changes, WConfigKey key)
{
	if (!changes)
		return True;
	return (changes->changed[WCFG_CHANGE_WORD(key)] & WCFG_CHANGE_BIT(key)) != 0;
}

static void replace_preference_string(char **target, const char *value)
{
	if (*target)
		wfree(*target);
	*target = value ? wstrdup(value) : NULL;
}

typedef enum {
	WCFG_PREF_COPY_BYTES,
	WCFG_PREF_COPY_STRING
} WConfigPreferenceCopyType;

typedef struct WConfigPreferenceCopy {
	WConfigKey key;
	size_t config_offset;
	size_t pref_offset;
	size_t config_size;
	size_t pref_size;
	WConfigPreferenceCopyType type;
} WConfigPreferenceCopy;

#define WCFG_COPY_BYTES(key, config_field, pref_field) \
	{ key, offsetof(WConfig, config_field), \
	  offsetof(struct WPreferences, pref_field), \
	  sizeof(((WConfig *)0)->config_field), \
	  sizeof(((struct WPreferences *)0)->pref_field), WCFG_PREF_COPY_BYTES }
#define WCFG_COPY_STRING(key, config_field, pref_field) \
	{ key, offsetof(WConfig, config_field), \
	  offsetof(struct WPreferences, pref_field), sizeof(char *), \
	  sizeof(char *), WCFG_PREF_COPY_STRING }

static const WConfigPreferenceCopy runtime_preference_copies[] = {
	WCFG_COPY_BYTES(WCFG_KEY_ICON_POSITION, icon_yard, icon_yard),
	WCFG_COPY_STRING(WCFG_KEY_PIXMAP_PATH, pixmap_path, pixmap_path),
	WCFG_COPY_STRING(WCFG_KEY_ICON_PATH, icon_path, icon_path),
	WCFG_COPY_BYTES(WCFG_KEY_STICKY_ICONS, sticky_icons, sticky_icons),
	WCFG_COPY_BYTES(WCFG_KEY_KEEP_DOCK_ON_PRIMARY_HEAD,
			keep_dock_on_primary_head, keep_dock_on_primary_head),
	WCFG_COPY_STRING(WCFG_KEY_SCREENSHOT_FILENAME_TEMPLATE,
			 screenshot_filename_template,
			 screenshot_filename_template),
	WCFG_COPY_BYTES(WCFG_KEY_MENU_STYLE, menu_style, menu_style),
	WCFG_COPY_BYTES(WCFG_KEY_TITLE_JUSTIFY, title_justification,
			title_justification),
	WCFG_COPY_BYTES(WCFG_KEY_WINDOW_TITLE_EXTEND_SPACE,
			window_title_clearance, window_title_clearance),
	WCFG_COPY_BYTES(WCFG_KEY_WINDOW_TITLE_MIN_HEIGHT,
			window_title_min_height, window_title_min_height),
	WCFG_COPY_BYTES(WCFG_KEY_WINDOW_TITLE_MAX_HEIGHT,
			window_title_max_height, window_title_max_height),
	WCFG_COPY_BYTES(WCFG_KEY_MENU_TITLE_EXTEND_SPACE,
			menu_title_clearance, menu_title_clearance),
	WCFG_COPY_BYTES(WCFG_KEY_MENU_TITLE_MIN_HEIGHT,
			menu_title_min_height, menu_title_min_height),
	WCFG_COPY_BYTES(WCFG_KEY_MENU_TITLE_MAX_HEIGHT,
			menu_title_max_height, menu_title_max_height),
	WCFG_COPY_BYTES(WCFG_KEY_MENU_TEXT_EXTEND_SPACE,
			menu_text_clearance, menu_text_clearance),
	WCFG_COPY_BYTES(WCFG_KEY_ICONIFICATION_STYLE, iconification_style,
			iconification_style),
	WCFG_COPY_BYTES(WCFG_KEY_ENFORCE_ICON_MARGIN, enforce_icon_margin,
			enforce_icon_margin),
	WCFG_COPY_BYTES(WCFG_KEY_DISABLE_WS_MOUSE_ACTIONS, disable_root_mouse,
			disable_root_mouse),
	WCFG_COPY_BYTES(WCFG_KEY_MOUSE_LEFT_BUTTON_ACTION, mouse_button1,
			mouse_button1),
	WCFG_COPY_BYTES(WCFG_KEY_MOUSE_MIDDLE_BUTTON_ACTION, mouse_button2,
			mouse_button2),
	WCFG_COPY_BYTES(WCFG_KEY_MOUSE_RIGHT_BUTTON_ACTION, mouse_button3,
			mouse_button3),
	WCFG_COPY_BYTES(WCFG_KEY_MOUSE_BACKWARD_BUTTON_ACTION, mouse_button8,
			mouse_button8),
	WCFG_COPY_BYTES(WCFG_KEY_MOUSE_FORWARD_BUTTON_ACTION, mouse_button9,
			mouse_button9),
	WCFG_COPY_BYTES(WCFG_KEY_MOUSE_WHEEL_ACTION, mouse_wheel_scroll,
			mouse_wheel_scroll),
	WCFG_COPY_BYTES(WCFG_KEY_MOUSE_WHEEL_TILT_ACTION, mouse_wheel_tilt,
			mouse_wheel_tilt),
	WCFG_COPY_BYTES(WCFG_KEY_DONT_CONFIRM_KILL, dont_confirm_kill,
			dont_confirm_kill),
	WCFG_COPY_BYTES(WCFG_KEY_WINDOW_TITLE_BALLOONS, window_balloon,
			window_balloon),
	WCFG_COPY_BYTES(WCFG_KEY_MINIWINDOW_TITLE_BALLOONS,
			miniwin_title_balloon, miniwin_title_balloon),
	WCFG_COPY_BYTES(WCFG_KEY_MINIWINDOW_PREVIEW_BALLOONS,
			miniwin_preview_balloon, miniwin_preview_balloon),
	WCFG_COPY_BYTES(WCFG_KEY_APPICON_BALLOONS, appicon_balloon,
			appicon_balloon),
	WCFG_COPY_BYTES(WCFG_KEY_HELP_BALLOONS, help_balloon, help_balloon),
	WCFG_COPY_BYTES(WCFG_KEY_EDGE_RESISTANCE, edge_resistance,
			edge_resistance),
	WCFG_COPY_BYTES(WCFG_KEY_RESIZE_INCREMENT, resize_increment,
			resize_increment),
	WCFG_COPY_BYTES(WCFG_KEY_ATTRACTION, attract, attract),
	WCFG_COPY_BYTES(WCFG_KEY_DISABLE_BLINKING, dont_blink, dont_blink),
	WCFG_COPY_BYTES(WCFG_KEY_SINGLE_CLICK_LAUNCH, single_click,
			single_click),
	WCFG_COPY_BYTES(WCFG_KEY_STRICT_WINDOZE_CYCLE, strict_windoze_cycle,
			strict_windoze_cycle),
	WCFG_COPY_BYTES(WCFG_KEY_SWITCH_PANEL_ONLY_OPEN, panel_only_open,
			panel_only_open),
	WCFG_COPY_BYTES(WCFG_KEY_MINI_PREVIEW_SIZE, minipreview_size,
			minipreview_size),
	WCFG_COPY_BYTES(WCFG_KEY_IGNORE_GTK_HINTS,
			ignore_gtk_decoration_hints,
			ignore_gtk_decoration_hints),
	WCFG_COPY_BYTES(WCFG_KEY_NO_WINDOW_OVER_DOCK, no_window_over_dock,
			no_window_over_dock),
	WCFG_COPY_BYTES(WCFG_KEY_NO_WINDOW_OVER_ICONS, no_window_over_icons,
			no_window_over_icons),
	WCFG_COPY_BYTES(WCFG_KEY_RESIZE_DISPLAY, size_display, size_display),
	WCFG_COPY_BYTES(WCFG_KEY_MOVE_DISPLAY, move_display, move_display),
#ifdef USE_RANDR
	WCFG_COPY_BYTES(WCFG_KEY_HOTPLUG_MONITOR, hotplug_monitor,
			hotplug_monitor),
#endif
	WCFG_COPY_BYTES(WCFG_KEY_HOT_CORNERS, hot_corners, hot_corners),
	WCFG_COPY_BYTES(WCFG_KEY_HOT_CORNER_DELAY, hot_corner_delay,
			hot_corner_delay),
	WCFG_COPY_BYTES(WCFG_KEY_HOT_CORNER_EDGE, hot_corner_edge,
			hot_corner_edge),
	WCFG_COPY_BYTES(WCFG_KEY_WINDOW_LIST_APP_ICONS, window_list_app_icons,
			window_list_app_icons),
	WCFG_COPY_BYTES(WCFG_KEY_MOUSE_WHEEL_FOCUS, mouse_wheel_focus,
			mouse_wheel_focus),
	WCFG_COPY_BYTES(WCFG_KEY_KEYCHAIN_TIMEOUT_DELAY, keychain_timeout_delay,
			keychain_timeout_delay),
	WCFG_COPY_BYTES(WCFG_KEY_SHOW_CLIP_TITLE, show_clip_title,
			show_clip_title),
#ifdef KEEP_XKB_LOCK_STATUS
	WCFG_COPY_BYTES(WCFG_KEY_KBD_MODE_LOCK, modelock, modelock),
#endif
	WCFG_COPY_BYTES(WCFG_KEY_DIALOG_HISTORY_LINES, history_lines,
			history_lines),
	WCFG_COPY_BYTES(WCFG_KEY_CYCLE_ACTIVE_HEAD_ONLY,
			cycle_active_head_only, cycle_active_head_only),
	WCFG_COPY_BYTES(WCFG_KEY_CYCLE_ALL_WORKSPACES, cycle_all_workspaces,
			cycle_all_workspaces),
	WCFG_COPY_BYTES(WCFG_KEY_CYCLE_IGNORE_MINIMIZED,
			cycle_ignore_minimized, cycle_ignore_minimized),
	WCFG_COPY_BYTES(WCFG_KEY_DB_CLICK_FULL_SCREEN, double_click_fullscreen,
			double_click_fullscreen),
	WCFG_COPY_BYTES(WCFG_KEY_CLOSE_ROOT_MENU_BY_LEFT_OR_RIGHT_MOUSE_CLICK,
			close_rootmenu_left_right_click,
			close_rootmenu_left_right_click),
	WCFG_COPY_BYTES(WCFG_KEY_COLORMAP_MODE, colormap_mode, colormap_mode),
	WCFG_COPY_BYTES(WCFG_KEY_AUTO_FOCUS, auto_focus, auto_focus),
	WCFG_COPY_BYTES(WCFG_KEY_AUTO_FOCUS_ACROSS_HEADS, auto_focus_across_heads,
			auto_focus_across_heads),
	WCFG_COPY_BYTES(WCFG_KEY_RAISE_DELAY, raise_delay, raise_delay),
	WCFG_COPY_BYTES(WCFG_KEY_CIRCULATE_RAISE, circ_raise, circ_raise),
	WCFG_COPY_BYTES(WCFG_KEY_SUPERFLUOUS, superfluous, superfluous),
	WCFG_COPY_BYTES(WCFG_KEY_ADVANCE_TO_NEW_WORKSPACE, ws_advance,
			ws_advance),
	WCFG_COPY_BYTES(WCFG_KEY_CYCLE_WORKSPACES, ws_cycle, ws_cycle),
	WCFG_COPY_BYTES(WCFG_KEY_WORKSPACE_NAME_DISPLAY_POSITION,
			workspace_name_display_position,
			workspace_name_display_position),
	WCFG_COPY_BYTES(WCFG_KEY_WINDOW_PLACEMENT, window_placement,
			window_placement),
	WCFG_COPY_BYTES(WCFG_KEY_WORKSPACE_BORDER, workspace_border_position,
			workspace_border_position),
	WCFG_COPY_BYTES(WCFG_KEY_WORKSPACE_BORDER_SIZE, workspace_border_size,
			workspace_border_size),
	WCFG_COPY_BYTES(WCFG_KEY_WRAP_MENUS, wrap_menus, wrap_menus),
	WCFG_COPY_BYTES(WCFG_KEY_SCROLLABLE_MENUS, scrollable_menus,
			scrollable_menus),
	WCFG_COPY_BYTES(WCFG_KEY_DOUBLE_CLICK_TIME, double_click_time,
			dblclick_time),
	WCFG_COPY_BYTES(WCFG_KEY_SAVE_SESSION_ON_EXIT, save_session_on_exit,
			save_session_on_exit),
	WCFG_COPY_BYTES(WCFG_KEY_MENU_SCROLL_SPEED, menu_scroll_speed,
			menu_scroll_speed),
	WCFG_COPY_BYTES(WCFG_KEY_ICON_SLIDE_SPEED, icon_slide_speed,
			icon_slide_speed),
	WCFG_COPY_BYTES(WCFG_KEY_SHADE_SPEED, shade_speed, shade_speed),
	WCFG_COPY_BYTES(WCFG_KEY_BOUNCE_APPICONS_WHEN_URGENT,
			bounce_appicons_when_urgent,
			bounce_appicons_when_urgent),
	WCFG_COPY_BYTES(WCFG_KEY_RAISE_APPICONS_WHEN_BOUNCING,
			raise_appicons_when_bouncing,
			raise_appicons_when_bouncing),
	WCFG_COPY_BYTES(WCFG_KEY_DO_NOT_MAKE_APPICONS_BOUNCE,
			do_not_make_appicons_bounce,
			do_not_make_appicons_bounce),
	WCFG_COPY_BYTES(WCFG_KEY_CLIP_AUTORAISE_DELAY, clip_auto_raise_delay,
			clip_auto_raise_delay),
	WCFG_COPY_BYTES(WCFG_KEY_CLIP_AUTOLOWER_DELAY, clip_auto_lower_delay,
			clip_auto_lower_delay),
	WCFG_COPY_BYTES(WCFG_KEY_CLIP_AUTOEXPAND_DELAY, clip_auto_expand_delay,
			clip_auto_expand_delay),
	WCFG_COPY_BYTES(WCFG_KEY_CLIP_AUTOCOLLAPSE_DELAY,
			clip_auto_collapse_delay, clip_auto_collapse_delay),
	WCFG_COPY_BYTES(WCFG_KEY_ALIGN_SUBMENUS, align_menus, align_menus),
	WCFG_COPY_BYTES(WCFG_KEY_VI_KEY_MENUS, vi_key_menus, vi_key_menus),
	WCFG_COPY_BYTES(WCFG_KEY_OPEN_TRANSIENT_ON_OWNER_WORKSPACE,
			open_transients_with_parent,
			open_transients_with_parent),
	WCFG_COPY_BYTES(WCFG_KEY_IGNORE_FOCUS_CLICK, ignore_focus_click,
			ignore_focus_click),
	WCFG_COPY_BYTES(WCFG_KEY_USE_SAVE_UNDERS, use_saveunders,
			use_saveunders),
	WCFG_COPY_BYTES(WCFG_KEY_OPAQUE_MOVE, opaque_move, opaque_move),
	WCFG_COPY_BYTES(WCFG_KEY_OPAQUE_RESIZE, opaque_resize, opaque_resize),
	WCFG_COPY_BYTES(WCFG_KEY_OPAQUE_MOVE_RESIZE_KEYBOARD,
			opaque_move_resize_keyboard,
			opaque_move_resize_keyboard),
	WCFG_COPY_BYTES(WCFG_KEY_DISABLE_ANIMATIONS, no_animations,
			no_animations),
	WCFG_COPY_BYTES(WCFG_KEY_DONT_LINK_WORKSPACES, no_autowrap,
			no_autowrap),
	WCFG_COPY_BYTES(WCFG_KEY_WINDOW_SNAPPING, window_snapping,
			window_snapping),
	WCFG_COPY_BYTES(WCFG_KEY_SNAP_EDGE_DETECT, snap_edge_detect,
			snap_edge_detect),
	WCFG_COPY_BYTES(WCFG_KEY_SNAP_CORNER_DETECT, snap_corner_detect,
			snap_corner_detect),
	WCFG_COPY_BYTES(WCFG_KEY_SNAP_TO_TOP_MAXIMIZES_FULLSCREEN,
			snap_to_top_maximizes_fullscreen,
			snap_to_top_maximizes_fullscreen),
	WCFG_COPY_BYTES(WCFG_KEY_DRAG_MAXIMIZED_WINDOW, drag_maximized_window,
			drag_maximized_window),
	WCFG_COPY_BYTES(WCFG_KEY_MOVE_HALF_MAXIMIZED_WINDOWS_BETWEEN_SCREENS,
			move_half_max_between_heads,
			move_half_max_between_heads),
	WCFG_COPY_BYTES(WCFG_KEY_ALTERNATIVE_HALF_MAXIMIZED, alt_half_maximize,
			alt_half_maximize),
	WCFG_COPY_BYTES(WCFG_KEY_POINTER_WITH_HALF_MAX_WINDOWS,
			pointer_with_half_max_windows,
			pointer_with_half_max_windows),
	WCFG_COPY_BYTES(WCFG_KEY_HIGHLIGHT_ACTIVE_APP, highlight_active_app,
			highlight_active_app),
	WCFG_COPY_BYTES(WCFG_KEY_AUTO_ARRANGE_ICONS, auto_arrange_icons,
			auto_arrange_icons)
};

static Bool size_range_fits(size_t offset, size_t size, size_t total)
{
	return offset <= total && size <= total - offset;
}

static Bool runtime_preference_copy_is_valid(const WConfigPreferenceCopy *copy)
{
	if (copy->key >= WCFG_KEY_COUNT)
		return False;
	if (!size_range_fits(copy->config_offset, copy->config_size, sizeof(WConfig)) ||
	    !size_range_fits(copy->pref_offset, copy->pref_size, sizeof(struct WPreferences)))
		return False;
	if (copy->type == WCFG_PREF_COPY_STRING)
		return copy->config_size == sizeof(char *) &&
		    copy->pref_size == sizeof(char *);

	return copy->type == WCFG_PREF_COPY_BYTES &&
	    copy->config_size == copy->pref_size;
}

Bool wConfigRuntimePreferenceCopiesAreValid(void)
{
	size_t i;

	for (i = 0; i < wlengthof(runtime_preference_copies); i++) {
		if (!runtime_preference_copy_is_valid(&runtime_preference_copies[i]))
			return False;
	}

	return True;
}

static void copy_runtime_preference(const WConfigPreferenceCopy *copy,
				    const WConfig *config)
{
	const char *source = (const char *)config + copy->config_offset;
	char *target = (char *)&wPreferences + copy->pref_offset;
	Bool valid;

	valid = runtime_preference_copy_is_valid(copy);
	assert(valid);
	if (!valid)
		return;
	if (copy->type == WCFG_PREF_COPY_STRING) {
		replace_preference_string((char **)target, *(char * const *)source);
		return;
	}

	memcpy(target, source, copy->config_size);
}

static void apply_runtime_preference_copies(const WConfig *config,
					    const WConfigChanges *changes)
{
	size_t i;

	for (i = 0; i < wlengthof(runtime_preference_copies); i++) {
		const WConfigPreferenceCopy *copy = &runtime_preference_copies[i];

		if (wConfigChangesHas(changes, copy->key))
			copy_runtime_preference(copy, config);
	}
}

#undef WCFG_COPY_STRING
#undef WCFG_COPY_BYTES

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

	wPreferences.cmap_size = config->colormap_size;
	wPreferences.no_dithering = config->disable_dithering;
	wPreferences.icon_size = config->icon_size;
	wConfigApplyModifierKey(config->modifier_key, "Mod1");
	wPreferences.focus_mode = config->focus_mode;
	wPreferences.new_style = config->new_style;
	wPreferences.flags.nodock = wPreferences.flags.nodock || config->disable_dock;
	wPreferences.flags.nodrawer = wPreferences.flags.nodrawer ||
	    wPreferences.flags.nodock || config->disable_drawers;
	wPreferences.flags.noclip = wPreferences.flags.noclip || config->disable_clip;
	wPreferences.flags.clip_merged_in_dock = config->clip_merged_in_dock;
	wPreferences.flags.noclip = wPreferences.flags.noclip ||
	    config->clip_merged_in_dock;
	wPreferences.disable_miniwindows = config->disable_miniwindows;
	wPreferences.enable_workspace_pager = config->enable_workspace_pager;
	wPreferences.switch_panel_icon_size = config->switch_panel_icon_size;
}

unsigned int wConfigApplyRuntime(WScreen *scr, const WConfig *old_config,
				 const WConfig *new_config,
				 const WConfigChanges *changes)
{
	Bool update_usable_area = False;
	Bool update_root_mask = False;
	Bool arrange_all_icons = False;
	Bool sticky_icons_changed = False;
	Bool keybindings_changed = False;
	Bool workspace_back_force = False;
	Bool workspace_back_requested;
	Bool workspace_back_actual;
	unsigned int refresh;

	if (!new_config)
		return 0;

	refresh = config_refresh_mask_for_changes(changes);
	apply_runtime_preference_copies(new_config, changes);

	if (wConfigChangesHas(changes, WCFG_KEY_ICON_POSITION)) {
		update_usable_area = True;
		arrange_all_icons = True;
	}
	if (wConfigChangesHas(changes, WCFG_KEY_STICKY_ICONS))
		sticky_icons_changed = True;
	if (wConfigChangesHas(changes, WCFG_KEY_WRAP_APPICONS_IN_DOCK))
		wPreferences.flags.wrap_appicons_in_dock =
		    new_config->wrap_appicons_in_dock;
	if (wConfigChangesHas(changes, WCFG_KEY_WINDOW_PLACE_ORIGIN)) {
		WConfigCoord coord = clamp_window_place_origin(scr,
								 new_config->window_place_origin);

		wPreferences.window_place_origin.x = coord.x;
		wPreferences.window_place_origin.y = coord.y;
	}
	if (wConfigChangesHas(changes, WCFG_KEY_KEEP_DOCK_ON_PRIMARY_HEAD)) {
		if (scr && scr->dock)
			wDockSwap(scr->dock);
	}
	if (wConfigChangesHas(changes, WCFG_KEY_NO_WINDOW_OVER_DOCK))
		update_usable_area = True;
	if (wConfigChangesHas(changes, WCFG_KEY_NO_WINDOW_OVER_ICONS))
		update_usable_area = True;
	if (wConfigChangesHas(changes, WCFG_KEY_HOT_CORNERS))
		update_root_mask = True;
	if (wConfigChangesHas(changes, WCFG_KEY_WORKSPACE_BORDER))
		update_usable_area = True;
	if (wConfigChangesHas(changes, WCFG_KEY_WORKSPACE_BORDER_SIZE))
		update_usable_area = True;
	if (wConfigChangesHas(changes, WCFG_KEY_SCROLLABLE_MENUS))
		update_root_mask = True;
	if (wConfigChangesHas(changes, WCFG_KEY_DOUBLE_CLICK_TIME))
		W_setconf_doubleClickDelay(wPreferences.dblclick_time);
	if (scr && wConfigChangesHas(changes, WCFG_KEY_FRAME_BORDER_WIDTH))
		scr->frame_border_width = new_config->frame_border_width;

	workspace_back_requested =
	    wConfigChangesHas(changes, WCFG_KEY_WORKSPACE_BACK);
	workspace_back_actual = !old_config ||
	    !string_values_equal(old_config->workspace_back.spec,
				 new_config->workspace_back.spec);
	if (wConfigChangesHas(changes, WCFG_KEY_WORKSPACE_SPECIFIC_BACK)) {
		Bool helper_needed = scr && !scr->flags.backimage_helper_launched;

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
	if (wConfigChangesHas(changes, WCFG_KEY_SMOOTH_WORKSPACE_BACK))
		wPreferences.smooth_workspace_back = new_config->smooth_workspace_back;

	if (wConfigChangesHas(changes, WCFG_KEY_HOT_CORNER_ACTIONS))
		wConfigApplyHotCornerActions(scr, &new_config->hot_corner_actions);
	if (wConfigChangesHas(changes, WCFG_KEY_SWITCH_PANEL_IMAGES))
		wConfigApplySwitchPanelImages(scr, &new_config->switch_panel_images);
	if (wConfigChangesHas(changes, WCFG_KEY_MODIFIER_KEY_LABELS))
		wConfigApplyModifierKeyLabels(scr, &new_config->modifier_key_labels);
	if (wConfigChangesHas(changes, WCFG_KEY_MODIFIER_KEY_SHORT_LABELS))
		wConfigApplyModifierKeyShortLabels(scr,
						&new_config->modifier_key_short_labels);


	{
		const WConfigEntry *schema = wConfigSchema();
		size_t i, count = wConfigSchemaCount();

		for (i = 0; i < count; i++) {
			const WConfigResourceSpec *spec;

			if (!wConfigChangesHas(changes, schema[i].config_key))
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
	}

	{
		const WConfigEntry *schema = wConfigSchema();
		size_t i, count = wConfigSchemaCount();

		for (i = 0; i < count; i++) {
			const WConfigKeyBinding *binding;
			Bool binding_changed = False;

			if (schema[i].type != WCFG_KEY_BINDING ||
			    !wConfigChangesHas(changes, schema[i].config_key))
				continue;

			binding = (const WConfigKeyBinding *)
			    ((const char *)new_config + schema[i].offset);
			refresh |= wConfigApplyKeyBinding(scr, schema[i].key,
							  schema[i].min_value, binding->spec,
							  schema[i].default_value,
							  &binding_changed);
			keybindings_changed = keybindings_changed || binding_changed;
		}
	}

	if (scr && keybindings_changed)
		wKeyTreeRebuild(scr);

	if (scr && update_usable_area)
		wScreenUpdateUsableArea(scr);
	if (scr && arrange_all_icons)
		wArrangeIcons(scr, True);
	if (scr && sticky_icons_changed && scr->workspaces) {
		wWorkspaceForceChange(scr, scr->current_workspace);
		wArrangeIcons(scr, False);
	}
	if (scr && update_root_mask)
		wScreenUpdateRootEventMask(scr);

	return refresh;
}

void wConfigAdoptWindowMaker(WConfig *config)
{
	if (w_global.config == config)
		return;
	wConfigDestroy(w_global.config);
	w_global.config = config;
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
}

void wConfigApplyStaticDefaults(WMPropList *dict)
{
	WConfig *config;

	config = wConfigReadWindowMaker(dict);
	wConfigApplyStatic(config);
	wConfigDestroy(config);
}

void wConfigPrepareRuntimeDefaults(WMPropList *new_dict,
				   const WConfig *old_config,
				   const WConfig *new_config,
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

		if (!(schema[i].flags & WCFG_RUNTIME_RELOAD))
			continue;

		new_value = dict_get_entry(new_dict, &schema[i]);
		if (!new_value && old_config) {
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
	const WConfig *old_config;
	WConfig *new_config;
	WConfigChanges changes;
	unsigned int refresh;

	new_config = wConfigReadWindowMaker(new_dict);
	old_config = w_global.config;
	wConfigPrepareRuntimeDefaults(new_dict, old_config, new_config, &changes);
	refresh = wConfigApplyRuntime(scr, old_config, new_config, &changes);
	wConfigDestroy(new_config);

	return refresh;
}
