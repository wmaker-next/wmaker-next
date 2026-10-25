/*
 * check_config - tests for the typed xcb_window_t Maker config slice.
 */

#include "wconfig.h"
#include <stdbool.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "WindowMaker.h"
#include "screen.h"
#include "window.h"
#include "dockgeom.h"
#include "config-schema.h"
#include "wconfigmodel.h"

#include "check_common.h"

#ifdef VIRTUAL_DESKTOP
void wWorkspaceUpdateEdge(WScreen *scr) { (void)scr; }
#endif

struct wmaker_global_variables w_global;

static WDisplay test_display;
WDisplay *wdisp = &test_display;

void wCursorHideApplyConfig(WScreen *scr)
{
	(void)scr;
}
static int usable_area_updates;
static int root_mask_updates;
static int double_click_delay;
static int arrange_icons_all;
static int arrange_icons_visible;
static int workspace_force_changes;
static int workspace_force_last;
static int dock_swaps;
static int switch_panel_updates;
static char *switch_panel_tile;
static char *switch_panel_background;
static int switch_panel_center_width;
static int switch_panel_center_height;
static int modifier_label_updates;
static int modifier_short_label_updates;
static int hot_corner_updates;
static int modifier_key_updates;
static int urgent_blink_calls;
static int background_spec_updates;
static int workspace_specific_background_updates;
static int workspace_background_updates;
static int workspace_background_saw_smooth;
static int key_binding_updates;
static int key_binding_changed_updates;
static int key_binding_seen[WKBD_LAST];
static char *stub_binding_spec[WKBD_LAST];
static char *last_key_binding_spec;
static int last_key_binding_index;
static int key_tree_rebuilds;
static int key_regrabs;
static int regrab_mouse_grabs;
static int refresh_miniwindows;
static int screen_apply_icon_size;
static int icon_resize_all;
static int dock_apply_icon_size;
static int aa_flush_count;
static bool aa_current;
static int system_color_refreshes;
static int texture_spec_updates;
static int font_spec_updates;
static int color_spec_updates;
static int cursor_spec_updates;
static int last_texture_target;
static int last_font_target;
static int last_color_target;
static int color_targets[8];
static int last_cursor_target;
static int last_background_target;
static char *last_modifier_key_spec;
static char *last_texture_spec;
static char *last_font_spec;
static char *last_color_spec;
static char *last_cursor_spec;
static char *last_background_spec;
static int helper_messages;
static char last_helper_type;
static char *last_helper_msg;

#define EXPECTED_PIXMAP_PATH \
	"$WMAKER_USER_DATA/Pixmaps:" \
	"$WMAKER_USER_DATA/Backgrounds:" \
	"$WMAKER_USER_CACHE/CachedPixmaps:" \
	"~/pixmaps:" PKGDATADIR "/Pixmaps:" PKGDATADIR "/Backgrounds:" \
	PIXMAPDIR

#define EXPECTED_ICON_PATH \
	"$WMAKER_USER_DATA/Icons:" \
	"$WMAKER_USER_DATA/Pixmaps:" \
	"$WMAKER_USER_CACHE/CachedPixmaps:" \
	"~/pixmaps:" PKGDATADIR "/Icons:" PKGDATADIR "/Pixmaps:" \
	PIXMAPDIR

void wScreenUpdateUsableArea(WScreen *scr)
{
	(void)scr;
	usable_area_updates++;
}

void wScreenUpdateRootEventMask(WScreen *scr)
{
	(void)scr;
	root_mask_updates++;
}

void wHotCornersUpdate(WScreen *scr)
{
	(void)scr;
}

void wFindMouseApplyConfig(WScreen *scr)
{
	(void)scr;
}

void W_setconf_doubleClickDelay(int value)
{
	double_click_delay = value;
}

double WMScreenUIScale(WMScreen *scr)
{
	(void)scr;
	return 1.0;
}

void wArrangeIcons(WScreen *scr, bool arrangeAll)
{
	(void)scr;
	if (arrangeAll)
		arrange_icons_all++;
	else
		arrange_icons_visible++;
}

void wRefreshMiniwindows(WScreen *scr)
{
	(void)scr;
	refresh_miniwindows++;
}

void wWindowUrgentBlink(WWindow *wwin)
{
	(void)wwin;
	urgent_blink_calls++;
}

void wScreenApplyIconSize(WScreen *scr)
{
	(void)scr;
	screen_apply_icon_size++;
}

void wIconResizeAll(WScreen *scr)
{
	(void)scr;
	icon_resize_all++;
}

void wDockApplyIconSize(WScreen *scr)
{
	(void)scr;
	dock_apply_icon_size++;
}

void wWorkspaceForceChange(WScreen *scr, int workspace)
{
	(void)scr;
	workspace_force_changes++;
	workspace_force_last = workspace;
}

void wDockReanchor(struct WDock *dock)
{
	(void)dock;
	dock_swaps++;
}

void wDockSetEdge(struct WDock *dock, WDockEdge edge)
{
	(void)dock;
	(void)edge;
	dock_swaps++;
}

static void replace_test_string(char **target, const char *value)
{
	if (*target)
		wfree(*target);
	*target = value ? wstrdup(value) : NULL;
}


unsigned int wConfigApplyTextureSpec(WScreen *scr, const char *key,
				     int target,
				     const WConfigResourceSpec *spec,
				     const char *default_spec)
{
	(void)scr;
	(void)key;
	(void)default_spec;
	texture_spec_updates++;
	last_texture_target = target;
	replace_test_string(&last_texture_spec, spec ? spec->spec : NULL);
	return WCFG_REFRESH_WINDOW_TEXTURES;
}

unsigned int wConfigApplyFontSpec(WScreen *scr, const char *key,
				  int target,
				  const WConfigResourceSpec *spec,
				  const char *default_spec)
{
	(void)scr;
	(void)key;
	(void)default_spec;
	font_spec_updates++;
	last_font_target = target;
	replace_test_string(&last_font_spec, spec ? spec->spec : NULL);
	return WCFG_REFRESH_WINDOW_FONT;
}

unsigned int wConfigApplyColorSpec(WScreen *scr, const char *key,
				   int target,
				   const WConfigResourceSpec *spec,
				   const char *default_spec)
{
	(void)scr;
	(void)key;
	(void)default_spec;
	color_spec_updates++;
	last_color_target = target;
	if (color_spec_updates <= (int)(sizeof(color_targets) / sizeof(color_targets[0])))
		color_targets[color_spec_updates - 1] = target;
	replace_test_string(&last_color_spec, spec ? spec->spec : NULL);
	return WCFG_REFRESH_MENU_COLOR;
}

unsigned int wConfigApplyCursorSpec(WScreen *scr, const char *key,
				    int target,
				    const WConfigResourceSpec *spec,
				    const char *default_spec)
{
	(void)scr;
	(void)key;
	(void)default_spec;
	cursor_spec_updates++;
	last_cursor_target = target;
	replace_test_string(&last_cursor_spec, spec ? spec->spec : NULL);
	return 0;
}

void wConfigApplyModifierKey(const char *spec, const char *default_spec)
{
	const char *candidate = spec ? spec : default_spec;

	modifier_key_updates++;
	replace_test_string(&last_modifier_key_spec, candidate);
	if (candidate && strcmp(candidate, "Mod4") == 0)
		wdisp->modifier_mask = XCB_MOD_MASK_4;
	else if (candidate && strcmp(candidate, "Mod1") == 0)
		wdisp->modifier_mask = XCB_MOD_MASK_1;
}

void wConfigApplyTabDragModifier(const char *spec)
{
	if (!spec || spec[0] == '\0' || strcmp(spec, "None") == 0)
		wdisp->tab_drag_modifier_mask = 0;
	else if (strcmp(spec, "Shift") == 0)
		wdisp->tab_drag_modifier_mask = XCB_MOD_MASK_SHIFT;
}

unsigned int wConfigApplyWorkspaceBackgroundSpec(WScreen *scr, const char *key,
						 int target,
						 const WConfigResourceSpec *spec,
						 const char *default_spec)
{
	const char *candidate = spec && spec->spec ? spec->spec : default_spec;

	(void)key;
	background_spec_updates++;
	last_background_target = target;
	replace_test_string(&last_background_spec, candidate);
	if (target == WCFG_BACKGROUND_WORKSPACE_SPECIFIC) {
		workspace_specific_background_updates++;
		if (scr && candidate && strcmp(candidate, "()") != 0)
			scr->flags.backimage_helper_launched = 1;
	} else if (target == WCFG_BACKGROUND_WORKSPACE) {
		workspace_background_updates++;
		workspace_background_saw_smooth = w_global.config->smooth_workspace_back;
	}
	return 0;
}

void wConfigApplySwitchPanelImages(WScreen *scr,
				   const WConfigSwitchPanelImages *images)
{
	(void)scr;
	switch_panel_updates++;
	replace_test_string(&switch_panel_tile, images ? images->tile : NULL);
	replace_test_string(&switch_panel_background,
			    images ? images->background : NULL);
	switch_panel_center_width = images ? images->center_width : 0;
	switch_panel_center_height = images ? images->center_height : 0;
}

void wConfigApplyModifierKeyLabels(WScreen *scr,
				   const WConfigStringArray *labels)
{
	size_t i;

	(void)scr;
	modifier_label_updates++;
	for (i = 0; i < 7; i++) {
		replace_test_string(&wdisp->modifier_labels[i],
				    labels && i < labels->count ? labels->items[i] : NULL);
	}
}

void wConfigApplyModifierKeyShortLabels(WScreen *scr,
					const WConfigStringArray *labels)
{
	size_t i;

	(void)scr;
	modifier_short_label_updates++;
	for (i = 0; i < 9; i++) {
		const char *value = labels && i < labels->count ? labels->items[i] : NULL;
		replace_test_string(&wdisp->modifier_short_labels[i],
				    value && value[0] != '\0' ? value : NULL);
	}
}

void wConfigApplyHotCornerActions(WScreen *scr,
				  const WConfigStringArray *actions)
{
	size_t i;

	(void)scr;
	hot_corner_updates++;
	for (i = 0; i < 4; i++) {
		const char *value = actions && i < actions->count ? actions->items[i] : NULL;
		if (!scr)
			continue;
		replace_test_string(&scr->hot_corner_actions[i],
				    value && strcasecmp(value, "None") != 0 ? value : NULL);
	}
}

unsigned int wConfigApplyKeyBinding(WScreen *scr, const char *key,
				    int binding_index, const char *spec,
				    const char *default_spec,
				    bool *binding_changed)
{
	(void)scr;
	(void)key;
	(void)default_spec;
	key_binding_updates++;
	last_key_binding_index = binding_index;
	replace_test_string(&last_key_binding_spec, spec);
	if (binding_index >= 0 && binding_index < WKBD_LAST) {
		bool changed = (stub_binding_spec[binding_index] == NULL) !=
		    (spec == NULL) ||
		    (spec && strcmp(stub_binding_spec[binding_index], spec) != 0);

		key_binding_seen[binding_index]++;
		replace_test_string(&stub_binding_spec[binding_index], spec);
		if (binding_changed) {
			*binding_changed = changed;
			if (changed)
				key_binding_changed_updates++;
		}
	} else if (binding_changed) {
		*binding_changed = true;
		key_binding_changed_updates++;
	}
	if ((binding_index >= WKBD_WORKSPACE1 && binding_index <= WKBD_WORKSPACE10) ||
	    binding_index == WKBD_LASTWORKSPACE ||
	    (binding_index >= WKBD_MOVE_WORKSPACE1 &&
	     binding_index <= WKBD_MOVE_WORKSPACE10))
		return WCFG_REFRESH_WORKSPACE_MENU;
	return 0;
}

void wKeyTreeRebuild(WScreen *scr)
{
	(void)scr;
	key_tree_rebuilds++;
}

void wRegrabWindowMouseGrabs(WScreen *scr)
{
	(void)scr;
	regrab_mouse_grabs++;
}

void wRegrabFocusedWindowKeys(WScreen *scr)
{
	(void)scr;
	key_regrabs++;
}

void SendHelperMessage(WScreen *scr, char type, int workspace, const char *msg)
{
	(void)scr;
	(void)workspace;
	helper_messages++;
	last_helper_type = type;
	replace_test_string(&last_helper_msg, msg);
}

bool WMGetAntialiasedText(void)
{
	return aa_current;
}

void WMFlushFontCache(WMScreen *scr)
{
	(void)scr;
	aa_flush_count++;
}

void WMReleaseFont(WMFont *font)
{
	(void)font;
}

WMFont *WMBoldSystemFontOfSize(WMScreen *scr, int size)
{
	(void)scr;
	(void)size;
	return NULL;
}

bool WMRefreshSystemColors(WMScreen *scr)
{
	(void)scr;
	system_color_refreshes++;
	return true;
}

void wRefreshKeyChainOSD(WScreen *scr)
{
	(void)scr;
}

static void reset_phase4_counters(void)
{
	switch_panel_updates = 0;
	modifier_label_updates = 0;
	modifier_short_label_updates = 0;
	hot_corner_updates = 0;
	modifier_key_updates = 0;
	background_spec_updates = 0;
	workspace_specific_background_updates = 0;
	workspace_background_updates = 0;
	workspace_background_saw_smooth = -1;
	key_binding_updates = 0;
	key_binding_changed_updates = 0;
	key_tree_rebuilds = 0;
	key_regrabs = 0;
	regrab_mouse_grabs = 0;
	refresh_miniwindows = 0;
	screen_apply_icon_size = 0;
	icon_resize_all = 0;
	dock_apply_icon_size = 0;
	aa_flush_count = 0;
	aa_current = 0;
	system_color_refreshes = 0;
	urgent_blink_calls = 0;
	texture_spec_updates = 0;
	font_spec_updates = 0;
	color_spec_updates = 0;
	memset(color_targets, 0, sizeof(color_targets));
	cursor_spec_updates = 0;
	last_key_binding_index = -1;
	last_texture_target = -1;
	last_font_target = -1;
	last_color_target = -1;
	last_cursor_target = -1;
	last_background_target = -1;
	memset(key_binding_seen, 0, sizeof(key_binding_seen));
	replace_test_string(&switch_panel_tile, NULL);
	replace_test_string(&switch_panel_background, NULL);
	for (size_t i = 0; i < WKBD_LAST; i++)
		replace_test_string(&stub_binding_spec[i], NULL);
	replace_test_string(&last_key_binding_spec, NULL);
	replace_test_string(&last_modifier_key_spec, NULL);
	replace_test_string(&last_texture_spec, NULL);
	replace_test_string(&last_font_spec, NULL);
	replace_test_string(&last_color_spec, NULL);
	replace_test_string(&last_cursor_spec, NULL);
	replace_test_string(&last_background_spec, NULL);
	replace_test_string(&last_helper_msg, NULL);
	switch_panel_center_width = 0;
	switch_panel_center_height = 0;
	helper_messages = 0;
	last_helper_type = 0;
}

static unsigned int apply_runtime_defaults_for_test(WScreen *scr,
						    WMPropList *new_dict,
						    WMPropList *old_dict)
{
	if (old_dict)
		wConfigCacheWindowMaker(old_dict);
	else
		wConfigClearWindowMaker();
	return wConfigApplyRuntimeDefaults(scr, new_dict);
}

static void reset_preferences(void)
{
	wConfigClearWindowMaker();
	for (size_t i = 0; i < 7; i++) {
		if (wdisp->modifier_labels[i]) {
			wfree(wdisp->modifier_labels[i]);
			wdisp->modifier_labels[i] = NULL;
		}
	}
	for (size_t i = 0; i < 9; i++) {
		if (wdisp->modifier_short_labels[i]) {
			wfree(wdisp->modifier_short_labels[i]);
			wdisp->modifier_short_labels[i] = NULL;
		}
	}
	memset(&wdisp->flags, 0, sizeof(wdisp->flags));
	wdisp->modifier_mask = 0;
	wdisp->tab_drag_modifier_mask = 0;
	reset_phase4_counters();
}

static WMPropList *dict_from_desc(const char *desc)
{
	WMPropList *dict;

	dict = WMCreatePropListFromDescription(desc);
	if (!dict)
		fprintf(stderr, "bad proplist description: %s\n", desc);
	CHECK(dict != NULL);
	CHECK(dict == NULL || WMIsPLDictionary(dict));

	return dict;
}

static void dict_put_string(WMPropList *dict, const char *key, const char *value)
{
	WMPropList *plkey, *plvalue;

	plkey = WMCreatePLString(key);
	plvalue = WMCreatePLString(value);
	WMPutInPLDictionary(dict, plkey, plvalue);
	WMReleasePropList(plvalue);
	WMReleasePropList(plkey);
}

static WMPropList *dict_get_key(WMPropList *dict, const char *key)
{
	return WMGetFromPLDictionaryByName(dict, key);
}

static char *read_text_file(const char *path)
{
	FILE *file;
	long size;
	char *buffer;
	size_t read_size;

	file = fopen(path, "rb");
	if (!file)
		return NULL;
	if (fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return NULL;
	}
	size = ftell(file);
	if (size < 0) {
		fclose(file);
		return NULL;
	}
	rewind(file);
	buffer = wmalloc((size_t)size + 1);
	read_size = fread(buffer, 1, (size_t)size, file);
	fclose(file);
	if (read_size != (size_t)size) {
		wfree(buffer);
		return NULL;
	}
	buffer[(size_t)size] = '\0';
	return buffer;
}

static char *replace_all_text(const char *text, const char *needle,
			      const char *replacement)
{
	const char *scan;
	char *result, *out;
	size_t text_len, needle_len, replacement_len, count = 0;

	needle_len = strlen(needle);
	if (needle_len == 0)
		return wstrdup(text);
	replacement_len = strlen(replacement);
	for (scan = text; (scan = strstr(scan, needle)) != NULL; scan += needle_len)
		count++;
	if (count == 0)
		return wstrdup(text);

	text_len = strlen(text);
	if (replacement_len >= needle_len) {
		size_t growth = replacement_len - needle_len;

		if (growth > 0 && count > (SIZE_MAX - text_len) / growth)
			return NULL;
		text_len += count * growth;
	} else {
		text_len -= count * (needle_len - replacement_len);
	}
	result = wmalloc(text_len + 1);
	out = result;
	while ((scan = strstr(text, needle)) != NULL) {
		size_t prefix_len = (size_t)(scan - text);

		memcpy(out, text, prefix_len);
		out += prefix_len;
		memcpy(out, replacement, replacement_len);
		out += replacement_len;
		text = scan + needle_len;
	}
	memcpy(out, text, strlen(text) + 1);
	return result;
}

static char *read_installed_defaults_template_text(void)
{
	static const char *paths[] = {
#ifdef WMAKER_DEFAULTS_TEMPLATE
		WMAKER_DEFAULTS_TEMPLATE,
#endif
		"../../WindowMaker/Defaults/WindowMaker.in",
		"WindowMaker/Defaults/WindowMaker.in"
	};
	char *text = NULL;
	size_t i;

	for (i = 0; i < sizeof(paths) / sizeof(paths[0]) && !text; i++)
		text = read_text_file(paths[i]);
	CHECK(text != NULL);
	return text;
}

static WMPropList *read_installed_defaults_template(void)
{
	char *text, *resolved_pkgdatadir, *resolved_pixmapdir;
	WMPropList *dict;

	text = read_installed_defaults_template_text();
	if (!text)
		return NULL;
	resolved_pkgdatadir = replace_all_text(text, "#pkgdatadir#", PKGDATADIR);
	wfree(text);
	if (!resolved_pkgdatadir)
		return NULL;
	resolved_pixmapdir = replace_all_text(resolved_pkgdatadir,
					    "#pixmapdir#", PIXMAPDIR);
	wfree(resolved_pkgdatadir);
	if (!resolved_pixmapdir)
		return NULL;

	dict = WMCreatePropListFromDescription(resolved_pixmapdir);
	wfree(resolved_pixmapdir);
	CHECK(dict != NULL);
	CHECK(dict == NULL || WMIsPLDictionary(dict));
	return dict;
}

static WMPropList *schema_default_plist(const WConfigEntry *entry)
{
	switch (entry->type) {
	case WCFG_STRING:
	case WCFG_KEY_BINDING:
	case WCFG_FONT_SPEC:
	case WCFG_COLOR_SPEC:
	case WCFG_BOOL:
	case WCFG_INT:
	case WCFG_UINT:
	case WCFG_ENUM:
		return WMCreatePLString(entry->default_value);
	case WCFG_PATH_LIST:
	case WCFG_COORD:
	case WCFG_STRING_ARRAY:
	case WCFG_SWITCH_PANEL_IMAGES:
	case WCFG_TEXTURE_SPEC:
	case WCFG_CURSOR_SPEC:
	case WCFG_BACKGROUND_SPEC:
		return WMCreatePropListFromDescription(entry->default_value);
	}

	return NULL;
}

/* WindowMaker.in is shared between builds, but a few schema defaults
 * are build-conditional. The .in file omits those keys so the
 * per-build schema default applies at runtime, and this test accepts
 * their absence rather than requiring one build's value. */
static bool schema_default_is_build_conditional(const WConfigEntry *entry)
{
	/* DEF_TITLEBAR_LEFT depends on XKB_BUTTON_HINT */
	if (strcmp(entry->key, "TitlebarLeft") == 0)
		return true;
	return false;
}

static bool is_non_schema_defaults_key(const char *key)
{
	(void)key;
#ifndef USE_RANDR
	if (strcmp(key, "HotplugMonitor") == 0)
		return true;
#endif
#ifndef KEEP_XKB_LOCK_STATUS
	if (strcmp(key, "KbdModeLock") == 0 ||
	    strcmp(key, "ToggleKbdModeKey") == 0 ||
	    strcmp(key, "AccentPickerModifier") == 0 ||
	    strcmp(key, "AccentPickerSets") == 0 ||
	    strcmp(key, "AccentPickerKey") == 0)
		return true;
#endif
#ifndef VIRTUAL_DESKTOP
	if (strcmp(key, "EnableVirtualDesktop") == 0 ||
	    strcmp(key, "VirtualEdgeBorderSize") == 0 ||
	    strcmp(key, "VirtualEdgeHorizontalScrollSpeed") == 0 ||
	    strcmp(key, "VirtualEdgeVerticalScrollSpeed") == 0 ||
	    strcmp(key, "VirtualEdgeResistance") == 0 ||
	    strcmp(key, "VirtualEdgeAttraction") == 0 ||
	    strcmp(key, "VirtualEdgeLeftKey") == 0 ||
	    strcmp(key, "VirtualEdgeRightKey") == 0 ||
	    strcmp(key, "VirtualEdgeUpKey") == 0 ||
	    strcmp(key, "VirtualEdgeDownKey") == 0 ||
	    strcmp(key, "VirtualDesktopHomeKey") == 0 ||
	    strcmp(key, "VirtualDesktopCenterKey") == 0 ||
	    strcmp(key, "VirtualDesktopPinKey") == 0)
		return true;
#endif

	return false;
}

static size_t expected_schema_field_size(const WConfigEntry *entry)
{
	switch (entry->type) {
	case WCFG_BOOL:
		return sizeof(char);
	case WCFG_INT:
		return sizeof(int);
	case WCFG_UINT:
		return sizeof(unsigned int);
	case WCFG_ENUM:
		return sizeof(signed char);
	case WCFG_STRING:
	case WCFG_PATH_LIST:
		return sizeof(char *);
	case WCFG_COORD:
		return sizeof(WConfigCoord);
	case WCFG_STRING_ARRAY:
		return sizeof(WConfigStringArray);
	case WCFG_SWITCH_PANEL_IMAGES:
		return sizeof(WConfigSwitchPanelImages);
	case WCFG_KEY_BINDING:
		return sizeof(WConfigKeyBinding);
	case WCFG_TEXTURE_SPEC:
	case WCFG_FONT_SPEC:
	case WCFG_COLOR_SPEC:
	case WCFG_CURSOR_SPEC:
	case WCFG_BACKGROUND_SPEC:
		return sizeof(WConfigResourceSpec);
	}

	return 0;
}

static void test_schema_integrity(void)
{
	const WConfigEntry *schema;
	unsigned int seen[WCFG_KEY_COUNT];
	size_t i, count;

	memset(seen, 0, sizeof(seen));
	schema = wConfigSchema();
	count = wConfigSchemaCount();
	CHECK(count == WCFG_KEY_COUNT);

	for (i = 0; i < count; i++) {
		CHECK(schema[i].key != NULL);
		CHECK(schema[i].default_value != NULL);
		CHECK(schema[i].size == expected_schema_field_size(&schema[i]));
		if (schema[i].type == WCFG_ENUM)
			CHECK(schema[i].enum_values != NULL);
		CHECK(schema[i].config_key < WCFG_KEY_COUNT);
		if (schema[i].config_key < WCFG_KEY_COUNT)
			seen[schema[i].config_key]++;
	}

	for (i = 0; i < WCFG_KEY_COUNT; i++)
		CHECK(seen[i] == 1);
}

static void test_unchanged_diff(void)
{
	WConfig *config;
	WConfigChanges changes;
	size_t i;

	config = wConfigReadWindowMaker(NULL);
	CHECK(config != NULL);
	CHECK(wConfigDiff(config, config, &changes));
	for (i = 0; i < WCFG_CHANGE_WORDS; i++)
		CHECK(changes.changed[i] == 0);
	wConfigDestroy(config);
}

static void test_defaults(void)
{
	WConfig *config;

	config = wConfigReadWindowMaker(NULL);
	CHECK(config != NULL);
	CHECK(config->icon_size == 64);
	CHECK(strcmp(config->modifier_key, "Mod1") == 0);
	CHECK(config->focus_mode == WKF_CLICK);
	CHECK(config->new_style == TS_NEW);
	CHECK(config->disable_dock == 0);
	CHECK(config->disable_clip == 0);
	CHECK(config->disable_drawers == 0);
	CHECK(config->clip_merged_in_dock == 0);
	CHECK(config->disable_miniwindows == 0);
	CHECK(config->enable_workspace_pager == 0);
	CHECK(config->per_head_workspaces == 0);
	CHECK(config->switch_panel_icon_size == 64);
	CHECK(config->icon_yard == (IY_BOTTOM | IY_LEFT | IY_HORIZ));
	CHECK(strcmp(config->pixmap_path, EXPECTED_PIXMAP_PATH) == 0);
	CHECK(strcmp(config->icon_path, EXPECTED_ICON_PATH) == 0);
	CHECK(config->sticky_icons == 0);
	CHECK(config->wrap_appicons_in_dock == 1);
	CHECK(config->window_place_origin.x == 64);
	CHECK(config->window_place_origin.y == 0);
	CHECK(config->keep_dock_on_primary_head == 0);
	CHECK(config->dock_edge == WDOCK_EDGE_RIGHT);
	CHECK(strcmp(config->screenshot_filename_template,
		     "screenshot_%Y-%m-%d_at_%H:%M:%S") == 0);
	CHECK(config->menu_style == MS_NORMAL);
	CHECK(config->title_justification == WTJ_CENTER);
	CHECK(config->window_title_clearance == 0);
	CHECK(config->window_title_min_height == 0);
	CHECK(config->window_title_max_height == INT_MAX);
	CHECK(config->menu_title_clearance == 0);
	CHECK(config->menu_title_min_height == 0);
	CHECK(config->menu_title_max_height == INT_MAX);
	CHECK(config->menu_text_clearance == 0);
	CHECK(config->iconification_style == WIS_ZOOM);
	CHECK(config->enforce_icon_margin == 1);
	CHECK(config->disable_root_mouse == 0);
	CHECK(config->mouse_button1 == WA_SELECT_WINDOWS);
	CHECK(config->mouse_button2 == WA_OPEN_WINLISTMENU);
	CHECK(config->mouse_button3 == WA_OPEN_APPMENU);
	CHECK(config->mouse_button8 == WA_NONE);
	CHECK(config->mouse_button9 == WA_NONE);
	CHECK(config->mouse_wheel_scroll == WA_NONE);
	CHECK(config->mouse_wheel_tilt == WA_NONE);
	CHECK(config->dont_confirm_kill == 0);
	CHECK(config->window_balloon == 1);
	CHECK(config->miniwin_title_balloon == 0);
	CHECK(config->miniwin_preview_balloon == 0);
	CHECK(config->appicon_balloon == 0);
	CHECK(config->help_balloon == 0);
	CHECK(config->edge_resistance == 30);
	CHECK(config->resize_increment == 0);
	CHECK(config->window_gap == 0);
	CHECK(config->no_sound == 1);
	CHECK(config->attract == 0);
	CHECK(config->dont_blink == 0);
	CHECK(config->single_click == 0);
	CHECK(config->strict_windoze_cycle == 1);
	CHECK(config->panel_only_open == 0);
	CHECK(config->minipreview_size == 128);
	CHECK(config->ignore_gtk_decoration_hints == 0);
	CHECK(config->no_window_over_dock == 0);
	CHECK(config->no_window_over_icons == 0);
	CHECK(config->size_display == WDIS_CENTER);
	CHECK(config->move_display == WDIS_FRAME_CENTER);
#ifdef USE_RANDR
	CHECK(config->hotplug_monitor == 0);
#endif
	CHECK(config->hot_corners == 0);
	CHECK(config->hot_corner_delay == 250);
	CHECK(config->hot_corner_edge == 2);
	CHECK(config->find_mouse_duration == 1000);
	CHECK(config->find_mouse_radius == 0);
	CHECK(config->auto_hide_cursor == 0);
	CHECK(config->hide_cursor_after_keyboard == 1);
	CHECK(config->hide_cursor_after_idle == 0);
	CHECK(config->hide_cursor_idle_delay == 5000);
	CHECK(config->window_list_app_icons == 0);
	CHECK(config->mouse_wheel_focus == 0);
	CHECK(config->keychain_timeout_delay == 500);
	CHECK(config->keychain_display_position == WD_NONE);
	CHECK(config->show_clip_title == 1);
#ifdef KEEP_XKB_LOCK_STATUS
	CHECK(config->modelock == 0);
	CHECK(strcmp(config->accent_picker_modifier, "None") == 0);
	CHECK(config->accent_picker_sets.count == 2);
	CHECK(config->accent_picker_sets.items != NULL);
	if (config->accent_picker_sets.items && config->accent_picker_sets.count == 2) {
		CHECK(strcmp(config->accent_picker_sets.items[0], "auto") == 0);
		CHECK(strcmp(config->accent_picker_sets.items[1], "Currency") == 0);
	}
#endif
	CHECK(config->history_lines == 500);
	CHECK(config->cycle_active_head_only == 0);
	CHECK(config->cycle_all_workspaces == 0);
	CHECK(config->cycle_ignore_minimized == 0);
	CHECK(config->double_click_maximize == 0);
	CHECK(config->close_rootmenu_left_right_click == 0);
	CHECK(config->colormap_mode == WCM_POINTER);
	CHECK(config->auto_focus == 1);
	CHECK(config->auto_focus_across_heads == 1);
	CHECK(config->pointer_follows_focus == 0);
	CHECK(config->pointer_follows_monitor == 1);
	CHECK(config->raise_delay == 0);
	CHECK(config->circ_raise == 0);
	CHECK(config->superfluous == 1);
	CHECK(config->ws_advance == 0);
	CHECK(config->auto_remove_empty_workspaces == 0);
	CHECK(config->min_workspace_count == 1);
	CHECK(config->ws_cycle == 0);
	CHECK(config->skip_empty_workspaces == 0);
	CHECK(config->workspace_name_display_position == WD_CENTER);
	CHECK(config->window_placement == WPM_AUTO);
	CHECK(config->workspace_border_position == WB_NONE);
	CHECK(config->workspace_border_size == 0);
	CHECK(config->wrap_menus == 0);
	CHECK(config->scrollable_menus == 1);
	CHECK(config->double_click_time == 250);
	CHECK(config->save_session_on_exit == 0);
	CHECK(config->menu_scroll_speed == SPEED_FAST);
	CHECK(config->icon_slide_speed == SPEED_FAST);
	CHECK(config->shade_speed == SPEED_FAST);
	CHECK(config->bounce_appicons_when_urgent == 1);
	CHECK(config->blink_titlebar_when_urgent == 1);
	CHECK(config->raise_appicons_when_bouncing == 0);
	CHECK(config->do_not_make_appicons_bounce == 0);
	CHECK(config->clip_auto_raise_delay == 600);
	CHECK(config->clip_auto_lower_delay == 1000);
	CHECK(config->clip_auto_expand_delay == 600);
	CHECK(config->clip_auto_collapse_delay == 1000);
	CHECK(config->align_menus == 0);
	CHECK(config->vi_key_menus == 0);
	CHECK(config->open_transients_with_parent == 0);
	CHECK(config->ignore_focus_click == 0);
	CHECK(config->use_saveunders == 0);
	CHECK(config->opaque_move == 1);
	CHECK(config->opaque_resize == 0);
	CHECK(config->opaque_move_resize_keyboard == 0);
	CHECK(config->no_animations == 0);
	CHECK(config->no_autowrap == 1);
	CHECK(config->window_snapping == 0);
	CHECK(config->shared_edge_resize == 0);
	CHECK(config->snap_edge_detect == 1);
	CHECK(config->snap_corner_detect == 10);
	CHECK(config->snap_to_top_maximizes_fullscreen == 0);
	CHECK(config->drag_maximized_window == DRAGMAX_MOVE);
	CHECK(config->move_half_max_between_heads == 0);
	CHECK(config->alt_half_maximize == 0);
	CHECK(config->pointer_with_half_max_windows == 0);
	CHECK(config->highlight_active_app == 1);
	CHECK(config->auto_arrange_icons == 0);
	CHECK(strcmp(config->workspace_specific_back.spec, "()") == 0);
	CHECK(strcmp(config->workspace_back.spec,
		     "(solid, \"rgb:50/50/75\")") == 0);
	CHECK(config->smooth_workspace_back == 0);
	CHECK(strcmp(config->widget_color.spec, "(solid, gray)") == 0);
	CHECK(strcmp(config->icon_back.spec,
		     "(dgradient, \"rgb:a6/a6/b6\", \"rgb:51/55/61\")") == 0);
	CHECK(strcmp(config->window_title_font.spec, "Sans:bold:pixelsize=12") == 0);
	CHECK(strcmp(config->highlight_color.spec, "white") == 0);
	CHECK(strcmp(config->cclip_title_color.spec, "rgb:61/61/61") == 0);
	CHECK(config->frame_border_width == 1);
	CHECK(config->frame_inner_border_width == 0);
	CHECK(config->frame_inner_border_sides.count == 4);
	CHECK(strcmp(config->frame_inner_border_sides.items[0], "left") == 0);
	CHECK(strcmp(config->frame_inner_border_sides.items[1], "right") == 0);
	CHECK(strcmp(config->frame_inner_border_sides.items[2], "top") == 0);
	CHECK(strcmp(config->frame_inner_border_sides.items[3], "bottom") == 0);
	CHECK(strcmp(config->frame_inner_border_color.spec, "#b5b5b5") == 0);
	CHECK(strcmp(config->frame_inner_focused_border_color.spec, "white") == 0);
	CHECK(strcmp(config->normal_cursor.spec, "(builtin, left_ptr)") == 0);
	CHECK(config->hot_corner_actions.count == 4);
	CHECK(strcmp(config->hot_corner_actions.items[0], "None") == 0);
	CHECK(strcmp(config->hot_corner_actions.items[3], "None") == 0);
	CHECK(strcmp(config->switch_panel_images.tile, "swtile.png") == 0);
	CHECK(strcmp(config->switch_panel_images.background, "swback.png") == 0);
	CHECK(config->switch_panel_images.center_width == 30);
	CHECK(config->switch_panel_images.center_height == 40);
	CHECK(config->modifier_key_labels.count == 7);
	CHECK(strcmp(config->modifier_key_labels.items[1], "Control+") == 0);
	CHECK(config->modifier_key_short_labels.count == 9);
	CHECK(strcmp(config->modifier_key_short_labels.items[7], "M+") == 0);
	CHECK(strcmp(config->key_bindings[WKBD_ROOTMENU].spec, "F12") == 0);
	CHECK(strcmp(config->key_bindings[WKBD_WINDOWMENU].spec,
		     "Control+Escape") == 0);
	CHECK(strcmp(config->key_bindings[WKBD_KEYCHAIN_CANCEL].spec, "None") == 0);
	CHECK(strcmp(config->key_bindings[WKBD_MINIATURIZE_OTHERS].spec, "None") == 0);
	CHECK(strcmp(config->key_bindings[WKBD_LOCKSCREEN].spec, "None") == 0);
	CHECK(strcmp(config->key_bindings[WKBD_CLIPBOARD].spec, "Mod4+v") == 0);
	CHECK(strcmp(config->key_bindings[WKBD_SHOW_DESKTOP].spec, "None") == 0);
	CHECK(strcmp(config->key_bindings[WKBD_GROUPWINDOWNEXT].spec, "Mod4+grave") == 0);
	CHECK(strcmp(config->key_bindings[WKBD_GROUPWINDOWPREV].spec, "Mod4+Shift+grave") == 0);
	CHECK(strcmp(config->key_bindings[WKBD_MOVE_TO_EDGE_LEFT].spec, "None") == 0);
#ifdef KEEP_XKB_LOCK_STATUS
	CHECK(strcmp(config->key_bindings[WKBD_ACCENTPICKER].spec, "None") == 0);
#endif
	CHECK(strcmp(config->lock_screen_command, "wmlock") == 0);
	CHECK(strcmp(config->clipboard_command, "wmakermsg send_tick wmclipd-popup") == 0);
	wConfigDestroy(config);
}

static void test_defaults_file_matches_schema(void)
{
	const WConfigEntry *schema;
	WMPropList *dict, *keys;
	WConfig *defaults, *from_file;
	WConfigChanges changes;
	size_t i, count;

	dict = read_installed_defaults_template();
	if (!dict)
		return;

	defaults = wConfigCreateDefault();
	from_file = wConfigReadWindowMaker(dict);
	CHECK(defaults != NULL);
	CHECK(from_file != NULL);
	CHECK(wConfigDiff(defaults, from_file, &changes));

	schema = wConfigSchema();
	count = wConfigSchemaCount();
	for (i = 0; i < count; i++) {
		WMPropList *value, *expected;

		value = dict_get_key(dict, schema[i].key);
		if (!value) {
			if (schema_default_is_build_conditional(&schema[i]))
				continue;
			fprintf(stderr, "missing WindowMaker.in default for schema key %s\n",
				schema[i].key);
			CHECK(value != NULL);
			continue;
		}

		expected = schema_default_plist(&schema[i]);
		if (!expected || !WMIsPropListEqualTo(value, expected)) {
			char *actual_desc = WMGetPropListDescription(value, false);
			char *expected_desc = expected ?
			    WMGetPropListDescription(expected, false) : NULL;

			fprintf(stderr,
				"WindowMaker.in default mismatch for %s: got %s, expected %s\n",
				schema[i].key,
				actual_desc ? actual_desc : "<invalid>",
				expected_desc ? expected_desc : "<invalid>");
			if (actual_desc)
				wfree(actual_desc);
			if (expected_desc)
				wfree(expected_desc);
			CHECK(expected != NULL && WMIsPropListEqualTo(value, expected));
		}
		if (expected)
			WMReleasePropList(expected);
		CHECK(!wConfigChangesHas(&changes, schema[i].config_key));
	}

	keys = WMGetPLDictionaryKeys(dict);
	CHECK(keys != NULL);
	if (keys) {
		int key_count = WMGetPropListItemCount(keys);
		int k;

		for (k = 0; k < key_count; k++) {
			WMPropList *key_pl = WMGetFromPLArray(keys, k);
			const char *key;

			CHECK(key_pl != NULL && WMIsPLString(key_pl));
			if (!key_pl || !WMIsPLString(key_pl))
				continue;
			key = WMGetFromPLString(key_pl);
			if (!wConfigEntryForKey(key) && !is_non_schema_defaults_key(key)) {
				fprintf(stderr,
					"WindowMaker.in has non-schema default key %s\n", key);
				CHECK(false);
			}
		}
		WMReleasePropList(keys);
	}

	wConfigDestroy(defaults);
	wConfigDestroy(from_file);
	WMReleasePropList(dict);
}

static void test_valid_parse(void)
{
	WMPropList *dict;
	WConfig *config;

	dict = dict_from_desc("{ IconSize = 48; "
			      "ModifierKey = Mod4; "
			      "FocusMode = Sloppy; NewStyle = next; "
			      "DisableDock = YES; DisableClip = YES; "
			      "DisableDrawers = YES; ClipMergedInDock = YES; "
			      "DisableMiniwindows = YES; EnableWorkspacePager = YES; "
			      "PerHeadWorkspaces = YES; "
			      "SwitchPanelIconSize = 40; IconPosition = trv; "
			      "PixmapPath = (\"/tmp/pix\", \"/tmp/bg\"); "
			      "IconPath = (\"/tmp/icons\"); StickyIcons = YES; "
			      "WrapAppiconsInDock = NO; "
			      "WindowPlaceOrigin = (600, \"-3\"); "
			      "KeepDockOnPrimaryHead = YES; "
			      "ScreenshotFilenameTemplate = \"shot_%H\"; "
			      "MenuStyle = flat; TitleJustify = Right; "
			      "WindowTitleExtendSpace = 1; "
			      "WindowTitleMinHeight = 2; "
			      "WindowTitleMaxHeight = 30; "
			      "MenuTitleExtendSpace = 3; "
			      "MenuTitleMinHeight = 4; "
			      "MenuTitleMaxHeight = 40; "
			      "MenuTextExtendSpace = 5; "
			      "IconificationStyle = Flip; EnforceIconMargin = YES; "
			      "DisableWSMouseActions = YES; "
			      "MouseLeftButtonAction = MoveToNextWindow; "
			      "MouseMiddleButtonAction = MoveToPrevWindow; "
			      "MouseRightButtonAction = MoveToNextWorkspace; "
			      "MouseBackwardButtonAction = MoveToPrevWorkspace; "
			      "MouseForwardButtonAction = OpenApplicationsMenu; "
			      "MouseWheelAction = SwitchWorkspaces; "
			      "MouseWheelTiltAction = SwitchWindows; "
			      "DontConfirmKill = YES; WindowTitleBalloons = NO; "
			      "MiniwindowTitleBalloons = YES; "
			      "MiniwindowPreviewBalloons = YES; "
			      "AppIconBalloons = YES; HelpBalloons = YES; "
			      "EdgeResistance = 45; ResizeIncrement = 12; "
			      "Attraction = YES; DisableBlinking = YES; "
			      "SingleClickLaunch = YES; StrictWindozeCycle = NO; "
			      "SwitchPanelOnlyOpen = YES; MiniPreviewSize = 96; "
			      "IgnoreGtkHints = YES; NoWindowOverDock = YES; "
			      "NoWindowOverIcons = YES; ResizeDisplay = None; "
			      "MoveDisplay = Corner; "
#ifdef USE_RANDR
			      "HotplugMonitor = YES; "
#endif
			      "HotCorners = YES; HotCornerDelay = 77; "
			      "HotCornerEdge = 4; WindowListAppIcons = YES; "
			      "FindMouseDuration = 1500; "
			      "FindMouseRadius = 333; "
			      "AutoHideCursor = YES; "
			      "HideCursorAfterKeyboard = NO; "
			      "HideCursorAfterIdle = YES; "
			      "HideCursorIdleDelay = 12000; "
			      "MouseWheelFocus = YES; KeychainTimeoutDelay = 88; "
			      "KeychainDisplayPosition = topleft; "
			      "ShowClipTitle = NO; "
#ifdef KEEP_XKB_LOCK_STATUS
			      "KbdModeLock = YES; "
#endif
			      "DialogHistoryLines = 12; CycleActiveHeadOnly = YES; "
			      "CycleAllWorkspaces = YES; CycleIgnoreMinimized = YES; "
			      "DbClickMaximize = YES; "
			      "CloseRootMenuByLeftOrRightMouseClick = YES; "
			      "ColormapMode = Manual; AutoFocus = NO; "
			      "AutoFocusAcrossHeads = NO; RaiseDelay = 12; "
			      "CirculateRaise = YES; Superfluous = NO; "
		      "AdvanceToNewWorkspace = YES; CycleWorkspaces = YES; "
		      "SkipEmptyWorkspaces = YES; "
		      "AutoRemoveEmptyWorkspaces = YES; MinWorkspaceCount = 2; "
			      "WorkspaceNameDisplayPosition = topright; "
			      "WindowPlacement = Center; WorkspaceBorder = AllDirections; "
			      "WorkspaceBorderSize = 7; WrapMenus = YES; "
			      "ScrollableMenus = NO; DoubleClickTime = 333; "
			      "SaveSessionOnExit = YES; MenuScrollSpeed = UltraSlow; "
			      "IconSlideSpeed = Medium; ShadeSpeed = Slow; "
			      "BounceAppIconsWhenUrgent = NO; "
			      "BlinkTitlebarWhenUrgent = NO; "
			      "RaiseAppIconsWhenBouncing = YES; "
			      "DoNotMakeAppIconsBounce = YES; "
			      "ClipAutoraiseDelay = 11; ClipAutolowerDelay = 22; "
			      "ClipAutoexpandDelay = 33; ClipAutocollapseDelay = 44; "
			      "AlignSubmenus = YES; ViKeyMenus = YES; "
			      "OpenTransientOnOwnerWorkspace = YES; "
			      "IgnoreFocusClick = YES; UseSaveUnders = YES; "
			      "OpaqueMove = NO; OpaqueResize = YES; "
			      "OpaqueMoveResizeKeyboard = YES; DisableAnimations = YES; "
			      "DontLinkWorkspaces = NO; WindowSnapping = YES; "
			      "SharedEdgeResize = YES; "
			      "SnapEdgeDetect = 5; SnapCornerDetect = 15; "
			      "SnapToTopMaximizesFullscreen = YES; "
			      "DragMaximizedWindow = RestoreGeometry; "
			      "MoveHalfMaximizedWindowsBetweenScreens = YES; "
			      "AlternativeHalfMaximized = YES; "
			      "PointerWithHalfMaxWindows = YES; "
			      "PointerFollowsFocus = YES; PointerFollowsMonitor = NO; "
			      "HighlightActiveApp = NO; AutoArrangeIcons = YES; "
			      "WorkspaceSpecificBack = ((solid, black), (solid, white)); "
			      "WorkspaceBack = (solid, \"rgb:11/22/33\"); "
			      "SmoothWorkspaceBack = YES; "
			      "WidgetColor = (solid, blue); "
			      "WindowTitleFont = \"Sans 10\"; "
			      "HighlightColor = red; FrameBorderWidth = 5; "
			      "NormalCursor = (builtin, watch); "
			      "HotCornerActions = (OpenRootMenu, None, Run, Workspace1); "
			      "SwitchPanelImages = (tile.png, back.png, 12, 13); "
			      "ModifierKeyLabels = (\"S+\", \"C+\", \"M1+\", \"M2+\", \"M3+\", \"M4+\", \"M5+\"); "
			      "ModifierKeyShortLabels = (S, C, M1, M2, M3, M4, M5, M, A); "
			      "RootMenuKey = \"Mod1+X Control+Y\"; "
			      "WindowGap = 7; DisableSound = NO; "
			      "ShowDesktopKey = \"Mod4+D\"; "
			      "GroupWindowNextKey = \"Mod1+grave\"; "
			      "GroupWindowPrevKey = \"Mod1+Shift+grave\"; "
			      "MoveToEdgeLeftKey = \"Mod1+Left\"; "
			      "Workspace1Key = \"Mod4+1\"; }");
	if (!dict)
		return;

	config = wConfigReadWindowMaker(dict);
	CHECK(config->icon_size == 48);
	CHECK(strcmp(config->modifier_key, "Mod4") == 0);
	CHECK(config->focus_mode == WKF_SLOPPY);
	CHECK(config->new_style == TS_NEXT);
	CHECK(config->disable_dock == 1);
	CHECK(config->disable_clip == 1);
	CHECK(config->disable_drawers == 1);
	CHECK(config->clip_merged_in_dock == 1);
	CHECK(config->disable_miniwindows == 1);
	CHECK(config->enable_workspace_pager == 1);
	CHECK(config->per_head_workspaces == 1);
	CHECK(config->switch_panel_icon_size == 40);
	CHECK(config->icon_yard == (IY_TOP | IY_RIGHT | IY_VERT));
	CHECK(strcmp(config->pixmap_path, "/tmp/pix:/tmp/bg") == 0);
	CHECK(strcmp(config->icon_path, "/tmp/icons") == 0);
	CHECK(config->sticky_icons == 1);
	CHECK(config->wrap_appicons_in_dock == 0);
	CHECK(config->window_place_origin.x == 600);
	CHECK(config->window_place_origin.y == -3);
	CHECK(config->keep_dock_on_primary_head == 1);
	CHECK(strcmp(config->screenshot_filename_template, "shot_%H") == 0);
	CHECK(config->menu_style == MS_FLAT);
	CHECK(config->title_justification == WTJ_RIGHT);
	CHECK(config->window_title_clearance == 1);
	CHECK(config->window_title_min_height == 2);
	CHECK(config->window_title_max_height == 30);
	CHECK(config->menu_title_clearance == 3);
	CHECK(config->menu_title_min_height == 4);
	CHECK(config->menu_title_max_height == 40);
	CHECK(config->menu_text_clearance == 5);
	CHECK(config->iconification_style == WIS_FLIP);
	CHECK(config->enforce_icon_margin == 1);
	CHECK(config->disable_root_mouse == 1);
	CHECK(config->mouse_button1 == WA_MOVE_NEXTWINDOW);
	CHECK(config->mouse_button2 == WA_MOVE_PREVWINDOW);
	CHECK(config->mouse_button3 == WA_MOVE_NEXTWORKSPACE);
	CHECK(config->mouse_button8 == WA_MOVE_PREVWORKSPACE);
	CHECK(config->mouse_button9 == WA_OPEN_APPMENU);
	CHECK(config->mouse_wheel_scroll == WA_SWITCH_WORKSPACES);
	CHECK(config->mouse_wheel_tilt == WA_SWITCH_WINDOWS);
	CHECK(config->dont_confirm_kill == 1);
	CHECK(config->window_balloon == 0);
	CHECK(config->miniwin_title_balloon == 1);
	CHECK(config->miniwin_preview_balloon == 1);
	CHECK(config->appicon_balloon == 1);
	CHECK(config->help_balloon == 1);
	CHECK(config->edge_resistance == 45);
	CHECK(config->resize_increment == 12);
	CHECK(config->attract == 1);
	CHECK(config->dont_blink == 1);
	CHECK(config->single_click == 1);
	CHECK(config->strict_windoze_cycle == 0);
	CHECK(config->panel_only_open == 1);
	CHECK(config->minipreview_size == 96);
	CHECK(config->ignore_gtk_decoration_hints == 1);
	CHECK(config->no_window_over_dock == 1);
	CHECK(config->no_window_over_icons == 1);
	CHECK(config->size_display == WDIS_NONE);
	CHECK(config->move_display == WDIS_TOPLEFT);
#ifdef USE_RANDR
	CHECK(config->hotplug_monitor == 1);
#endif
	CHECK(config->hot_corners == 1);
	CHECK(config->hot_corner_delay == 77);
	CHECK(config->hot_corner_edge == 4);
	CHECK(config->find_mouse_duration == 1500);
	CHECK(config->find_mouse_radius == 333);
	CHECK(config->auto_hide_cursor == 1);
	CHECK(config->hide_cursor_after_keyboard == 0);
	CHECK(config->hide_cursor_after_idle == 1);
	CHECK(config->hide_cursor_idle_delay == 12000);
	CHECK(config->window_list_app_icons == 1);
	CHECK(config->mouse_wheel_focus == 1);
	CHECK(config->keychain_timeout_delay == 88);
	CHECK(config->keychain_display_position == WD_TOPLEFT);
	CHECK(config->show_clip_title == 0);
#ifdef KEEP_XKB_LOCK_STATUS
	CHECK(config->modelock == 1);
#endif
	CHECK(config->history_lines == 12);
	CHECK(config->cycle_active_head_only == 1);
	CHECK(config->cycle_all_workspaces == 1);
	CHECK(config->cycle_ignore_minimized == 1);
	CHECK(config->double_click_maximize == 1);
	CHECK(config->close_rootmenu_left_right_click == 1);
	CHECK(config->colormap_mode == WCM_CLICK);
	CHECK(config->auto_focus == 0);
	CHECK(config->auto_focus_across_heads == 0);
	CHECK(config->raise_delay == 12);
	CHECK(config->circ_raise == 1);
	CHECK(config->superfluous == 0);
	CHECK(config->ws_advance == 1);
	CHECK(config->auto_remove_empty_workspaces == 1);
	CHECK(config->min_workspace_count == 2);
	CHECK(config->ws_cycle == 1);
	CHECK(config->skip_empty_workspaces == 1);
	CHECK(config->workspace_name_display_position == WD_TOPRIGHT);
	CHECK(config->window_placement == WPM_CENTER);
	CHECK(config->workspace_border_position == WB_ALLDIRS);
	CHECK(config->workspace_border_size == 7);
	CHECK(config->wrap_menus == 1);
	CHECK(config->scrollable_menus == 0);
	CHECK(config->double_click_time == 333);
	CHECK(config->save_session_on_exit == 1);
	CHECK(config->menu_scroll_speed == SPEED_ULTRASLOW);
	CHECK(config->icon_slide_speed == SPEED_MEDIUM);
	CHECK(config->shade_speed == SPEED_SLOW);
	CHECK(config->bounce_appicons_when_urgent == 0);
	CHECK(config->blink_titlebar_when_urgent == 0);
	CHECK(config->raise_appicons_when_bouncing == 1);
	CHECK(config->do_not_make_appicons_bounce == 1);
	CHECK(config->clip_auto_raise_delay == 11);
	CHECK(config->clip_auto_lower_delay == 22);
	CHECK(config->clip_auto_expand_delay == 33);
	CHECK(config->clip_auto_collapse_delay == 44);
	CHECK(config->align_menus == 1);
	CHECK(config->vi_key_menus == 1);
	CHECK(config->open_transients_with_parent == 1);
	CHECK(config->ignore_focus_click == 1);
	CHECK(config->use_saveunders == 1);
	CHECK(config->opaque_move == 0);
	CHECK(config->opaque_resize == 1);
	CHECK(config->opaque_move_resize_keyboard == 1);
	CHECK(config->no_animations == 1);
	CHECK(config->no_autowrap == 0);
	CHECK(config->window_snapping == 1);
	CHECK(config->shared_edge_resize == 1);
	CHECK(config->snap_edge_detect == 5);
	CHECK(config->snap_corner_detect == 15);
	CHECK(config->snap_to_top_maximizes_fullscreen == 1);
	CHECK(config->drag_maximized_window == DRAGMAX_RESTORE);
	CHECK(config->move_half_max_between_heads == 1);
	CHECK(config->alt_half_maximize == 1);
	CHECK(config->pointer_with_half_max_windows == 1);
	CHECK(config->pointer_follows_focus == 1);
	CHECK(config->pointer_follows_monitor == 0);
	CHECK(config->highlight_active_app == 0);
	CHECK(config->auto_arrange_icons == 1);
	CHECK(strcmp(config->workspace_specific_back.spec,
		     "((solid, black), (solid, white))") == 0);
	CHECK(strcmp(config->workspace_back.spec,
		     "(solid, \"rgb:11/22/33\")") == 0);
	CHECK(config->smooth_workspace_back == 1);
	CHECK(strcmp(config->widget_color.spec, "(solid, blue)") == 0);
	CHECK(strcmp(config->window_title_font.spec, "Sans 10") == 0);
	CHECK(strcmp(config->highlight_color.spec, "red") == 0);
	CHECK(config->frame_border_width == 5);
	CHECK(strcmp(config->normal_cursor.spec, "(builtin, watch)") == 0);
	CHECK(config->hot_corner_actions.count == 4);
	CHECK(strcmp(config->hot_corner_actions.items[0], "OpenRootMenu") == 0);
	CHECK(strcmp(config->hot_corner_actions.items[1], "None") == 0);
	CHECK(strcmp(config->switch_panel_images.tile, "tile.png") == 0);
	CHECK(strcmp(config->switch_panel_images.background, "back.png") == 0);
	CHECK(config->switch_panel_images.center_width == 12);
	CHECK(config->switch_panel_images.center_height == 13);
	CHECK(config->modifier_key_labels.count == 7);
	CHECK(strcmp(config->modifier_key_labels.items[0], "S+") == 0);
	CHECK(config->modifier_key_short_labels.count == 9);
	CHECK(strcmp(config->modifier_key_short_labels.items[8], "A") == 0);
	CHECK(strcmp(config->key_bindings[WKBD_ROOTMENU].spec,
		     "Mod1+X Control+Y") == 0);
	CHECK(strcmp(config->key_bindings[WKBD_WORKSPACE1].spec, "Mod4+1") == 0);
	CHECK(config->window_gap == 7);
	CHECK(config->no_sound == 0);
	CHECK(strcmp(config->key_bindings[WKBD_SHOW_DESKTOP].spec, "Mod4+D") == 0);
	CHECK(strcmp(config->key_bindings[WKBD_GROUPWINDOWNEXT].spec, "Mod1+grave") == 0);
	CHECK(strcmp(config->key_bindings[WKBD_GROUPWINDOWPREV].spec, "Mod1+Shift+grave") == 0);
	CHECK(strcmp(config->key_bindings[WKBD_MOVE_TO_EDGE_LEFT].spec, "Mod1+Left") == 0);

	wConfigDestroy(config);
	WMReleasePropList(dict);
}

static void test_static_apply(void)
{
	WMPropList *dict;

	reset_preferences();
	dict = dict_from_desc("{ DisableDock = YES; DisableClip = NO; "
			      "DisableDrawers = NO; ClipMergedInDock = YES; "
			      "DisableMiniwindows = YES; EnableWorkspacePager = YES; "
			      "ModifierKey = Mod4; SwitchPanelIconSize = 48; }");
	if (!dict)
		return;

	wConfigApplyStaticDefaults(dict);
	CHECK(wdisp->flags.nodock == 1);
	CHECK(wdisp->flags.nodrawer == 1);
	CHECK(wdisp->flags.noclip == 1);
	CHECK(wdisp->flags.clip_merged_in_dock == 1);
	CHECK(w_global.config->disable_miniwindows == 1);
	CHECK(w_global.config->enable_workspace_pager == 1);
	CHECK(w_global.config->switch_panel_icon_size == 48);
	/* ModifierKey is now resolved on the runtime path (after wXkbInit),
	 * so wConfigApplyStatic no longer touches the modifier mask. */
	CHECK(modifier_key_updates == 0);
	CHECK(wdisp->modifier_mask == 0);

	wConfigClearWindowMaker();
	WMReleasePropList(dict);
}

static void test_invalid_fallback(void)
{
	WMPropList *dict;
	WConfig *config;

	dict = dict_from_desc("{ IconSize = 0; ColormapMode = nowhere; "
			      "RaiseDelay = \"-1\"; WorkspaceNameDisplayPosition = edge; "
			      "WindowPlacement = nowhere; DoubleClickTime = 0; "
			      "DisableDock = maybe; SwitchPanelIconSize = bad; "
			      "ModifierKey = (); "
			      "IconPosition = middle; PixmapPath = bad; "
			      "IconPath = (); StickyIcons = maybe; "
			      "WrapAppiconsInDock = maybe; "
			      "WindowPlaceOrigin = (bad, 2); "
			      "KeepDockOnPrimaryHead = maybe; "
			      "DockEdge = nowhere; "
			      "ScreenshotFilenameTemplate = (bad); "
			      "MenuStyle = diagonal; TitleJustify = middle; "
			      "WindowTitleExtendSpace = bad; "
			      "WindowTitleMaxHeight = bad; "
			      "MenuTitleExtendSpace = bad; "
			      "MenuTextExtendSpace = bad; "
			      "IconificationStyle = nowhere; EnforceIconMargin = maybe; "
			      "MouseLeftButtonAction = nope; MouseWheelAction = nope; "
			      "DontConfirmKill = maybe; WindowTitleBalloons = maybe; "
			      "EdgeResistance = bad; MiniPreviewSize = bad; "
			      "NoWindowOverDock = maybe; ResizeDisplay = nowhere; "
			      "HotCornerDelay = bad; DialogHistoryLines = bad; "
			      "FindMouseDuration = 1; FindMouseRadius = 5000; "
			      "AutoHideCursor = maybe; HideCursorAfterKeyboard = maybe; "
			      "HideCursorAfterIdle = maybe; HideCursorIdleDelay = 3600001; "
			      "ScrollableMenus = maybe; MenuScrollSpeed = nowhere; "
			      "ClipAutoraiseDelay = \"-1\"; AlignSubmenus = maybe; "
			      "OpaqueMove = maybe; SnapEdgeDetect = bad; "
			      "DragMaximizedWindow = nowhere; "
			      "WorkspaceSpecificBack = bad; WorkspaceBack = bad; "
			      "SmoothWorkspaceBack = maybe; FrameBorderWidth = bad; "
			      "HotCornerActions = (OnlyOne); "
			      "SwitchPanelImages = (tile.png, back.png, bad, 4); "
			      "ModifierKeyLabels = (Shift); "
			      "ModifierKeyShortLabels = (A, B); "
			      "WidgetColor = (dgradient, black, white); "
			      "WindowTitleFont = (bad); HighlightColor = (bad); "
			      "NormalCursor = bad; RootMenuKey = (bad); }");
	if (!dict)
		return;
	dict_put_string(dict, "WrapMenus", "");

	config = wConfigReadWindowMaker(dict);
	CHECK(config->icon_size == 64);
	CHECK(config->disable_dock == 0);
	CHECK(config->switch_panel_icon_size == 64);
	CHECK(strcmp(config->modifier_key, "Mod1") == 0);
	CHECK(config->icon_yard == (IY_BOTTOM | IY_LEFT | IY_HORIZ));
	CHECK(strcmp(config->pixmap_path, EXPECTED_PIXMAP_PATH) == 0);
	CHECK(strcmp(config->icon_path, EXPECTED_ICON_PATH) == 0);
	CHECK(config->sticky_icons == 0);
	CHECK(config->wrap_appicons_in_dock == 1);
	CHECK(config->window_place_origin.x == 64);
	CHECK(config->window_place_origin.y == 0);
	CHECK(config->keep_dock_on_primary_head == 0);
	CHECK(config->dock_edge == WDOCK_EDGE_RIGHT);
	CHECK(strcmp(config->screenshot_filename_template,
		     "screenshot_%Y-%m-%d_at_%H:%M:%S") == 0);
	CHECK(config->menu_style == MS_NORMAL);
	CHECK(config->title_justification == WTJ_CENTER);
	CHECK(config->window_title_clearance == 0);
	CHECK(config->window_title_max_height == INT_MAX);
	CHECK(config->menu_title_clearance == 0);
	CHECK(config->menu_text_clearance == 0);
	CHECK(config->colormap_mode == WCM_POINTER);
	CHECK(config->raise_delay == 0);
	CHECK(config->workspace_name_display_position == WD_CENTER);
	CHECK(config->window_placement == WPM_AUTO);
	CHECK(config->double_click_time == 250);
	CHECK(config->iconification_style == WIS_ZOOM);
	CHECK(config->enforce_icon_margin == 1);
	CHECK(config->mouse_button1 == WA_SELECT_WINDOWS);
	CHECK(config->mouse_wheel_scroll == WA_NONE);
	CHECK(config->dont_confirm_kill == 0);
	CHECK(config->window_balloon == 1);
	CHECK(config->edge_resistance == 30);
	CHECK(config->minipreview_size == 128);
	CHECK(config->no_window_over_dock == 0);
	CHECK(config->size_display == WDIS_CENTER);
	CHECK(config->hot_corner_delay == 250);
	CHECK(config->find_mouse_duration == 1000);
	CHECK(config->find_mouse_radius == 0);
	CHECK(config->auto_hide_cursor == 0);
	CHECK(config->hide_cursor_after_keyboard == 1);
	CHECK(config->hide_cursor_after_idle == 0);
	CHECK(config->hide_cursor_idle_delay == 5000);
	CHECK(config->history_lines == 500);
	CHECK(config->scrollable_menus == 1);
	CHECK(config->wrap_menus == 0);
	CHECK(config->menu_scroll_speed == SPEED_FAST);
	CHECK(config->clip_auto_raise_delay == 600);
	CHECK(config->align_menus == 0);
	CHECK(config->opaque_move == 1);
	CHECK(config->snap_edge_detect == 1);
	CHECK(config->drag_maximized_window == DRAGMAX_MOVE);
	CHECK(strcmp(config->workspace_specific_back.spec, "()") == 0);
	CHECK(strcmp(config->workspace_back.spec,
		     "(solid, \"rgb:50/50/75\")") == 0);
	CHECK(config->smooth_workspace_back == 0);
	CHECK(config->frame_border_width == 1);
	CHECK(config->hot_corner_actions.count == 4);
	CHECK(strcmp(config->hot_corner_actions.items[0], "None") == 0);
	CHECK(strcmp(config->switch_panel_images.tile, "swtile.png") == 0);
	CHECK(strcmp(config->switch_panel_images.background, "swback.png") == 0);
	CHECK(config->modifier_key_labels.count == 7);
	CHECK(strcmp(config->modifier_key_labels.items[0], "Shift+") == 0);
	CHECK(config->modifier_key_short_labels.count == 9);
	CHECK(strcmp(config->modifier_key_short_labels.items[0], "Sh+") == 0);
	CHECK(strcmp(config->key_bindings[WKBD_ROOTMENU].spec, "F12") == 0);
	CHECK(strcmp(config->widget_color.spec, "(solid, gray)") == 0);
	CHECK(strcmp(config->window_title_font.spec, "Sans:bold:pixelsize=12") == 0);
	CHECK(strcmp(config->highlight_color.spec, "white") == 0);
	CHECK(strcmp(config->normal_cursor.spec, "(builtin, left_ptr)") == 0);

	wConfigDestroy(config);
	WMReleasePropList(dict);
}

static void test_runtime_apply(void)
{
	WMPropList *old_dict, *new_dict;
	WScreen screen;
	unsigned int refresh;

	reset_preferences();
	memset(&screen, 0, sizeof(screen));
	screen.scr_width = 900;
	screen.scr_height = 600;
	screen.workspaces = (struct WWorkspace **)&screen;
	screen.current_workspace = 3;
	screen.dock = (struct WDock *)&screen;
	usable_area_updates = 0;
	root_mask_updates = 0;
	double_click_delay = 0;
	arrange_icons_all = 0;
	arrange_icons_visible = 0;
	workspace_force_changes = 0;
	workspace_force_last = -1;
	dock_swaps = 0;

	old_dict = dict_from_desc("{ IconPosition = blh; "
				  "PixmapPath = (\"/old/pix\", \"/old/bg\"); "
				  "IconPath = (\"/old/icons\"); StickyIcons = NO; "
				  "WrapAppiconsInDock = YES; "
				  "WindowPlaceOrigin = (64, 0); "
				  "KeepDockOnPrimaryHead = NO; "
				  "ScreenshotFilenameTemplate = \"old_%H\"; "
				  "MenuStyle = normal; TitleJustify = Center; "
				  "WindowTitleExtendSpace = 0; "
				  "WindowTitleMinHeight = 0; "
				  "WindowTitleMaxHeight = 2147483647; "
				  "MenuTitleExtendSpace = 0; "
				  "MenuTitleMinHeight = 0; "
				  "MenuTitleMaxHeight = 2147483647; "
				  "MenuTextExtendSpace = 0; "
				  "IconificationStyle = Zoom; "
				  "EnforceIconMargin = NO; DisableWSMouseActions = NO; "
				  "MouseLeftButtonAction = SelectWindows; "
				  "MouseMiddleButtonAction = OpenWindowListMenu; "
				  "MouseRightButtonAction = OpenApplicationsMenu; "
				  "MouseBackwardButtonAction = None; "
				  "MouseForwardButtonAction = None; "
				  "MouseWheelAction = None; MouseWheelTiltAction = None; "
				  "DontConfirmKill = NO; WindowTitleBalloons = YES; "
				  "MiniwindowTitleBalloons = NO; "
				  "MiniwindowPreviewBalloons = NO; "
				  "AppIconBalloons = NO; HelpBalloons = NO; "
				  "EdgeResistance = 30; ResizeIncrement = 0; "
				  "Attraction = NO; DisableBlinking = NO; "
				  "SingleClickLaunch = NO; StrictWindozeCycle = YES; "
				  "SwitchPanelOnlyOpen = NO; MiniPreviewSize = 128; "
				  "IgnoreGtkHints = NO; NoWindowOverDock = NO; "
				  "NoWindowOverIcons = NO; ResizeDisplay = center; "
				  "MoveDisplay = floating; "
#ifdef USE_RANDR
				  "HotplugMonitor = NO; "
#endif
				  "HotCorners = NO; HotCornerDelay = 250; "
				  "HotCornerEdge = 2; WindowListAppIcons = NO; "
				  "MouseWheelFocus = NO; KeychainTimeoutDelay = 500; "
				  "KeychainDisplayPosition = none; "
				  "ShowClipTitle = YES; "
#ifdef KEEP_XKB_LOCK_STATUS
				  "KbdModeLock = NO; "
#endif
				  "DialogHistoryLines = 500; CycleActiveHeadOnly = NO; "
				  "CycleAllWorkspaces = NO; CycleIgnoreMinimized = NO; "
				  "DbClickMaximize = NO; "
				  "CloseRootMenuByLeftOrRightMouseClick = NO; "
				  "ColormapMode = Auto; AutoFocus = YES; "
				  "AutoFocusAcrossHeads = YES; RaiseDelay = 0; "
				  "CirculateRaise = NO; Superfluous = YES; "
				  "AdvanceToNewWorkspace = NO; CycleWorkspaces = NO; "
				  "SkipEmptyWorkspaces = NO; "
				  "WorkspaceNameDisplayPosition = center; "
				  "WorkspaceBorderSize = 0; ScrollableMenus = YES; "
				  "DoubleClickTime = 250; SaveSessionOnExit = NO; "
				  "MenuScrollSpeed = Fast; IconSlideSpeed = Fast; "
				  "ShadeSpeed = Fast; BounceAppIconsWhenUrgent = YES; "
				  "RaiseAppIconsWhenBouncing = NO; "
				  "DoNotMakeAppIconsBounce = NO; "
				  "ClipAutoraiseDelay = 600; ClipAutolowerDelay = 1000; "
				  "ClipAutoexpandDelay = 600; ClipAutocollapseDelay = 1000; "
				  "AlignSubmenus = NO; ViKeyMenus = NO; "
				  "OpenTransientOnOwnerWorkspace = NO; "
				  "IgnoreFocusClick = NO; UseSaveUnders = NO; "
				  "OpaqueMove = YES; OpaqueResize = NO; "
				  "OpaqueMoveResizeKeyboard = NO; DisableAnimations = NO; "
				  "DontLinkWorkspaces = YES; WindowSnapping = NO; "
				  "SharedEdgeResize = NO; "
				  "SnapEdgeDetect = 1; SnapCornerDetect = 10; "
				  "SnapToTopMaximizesFullscreen = NO; "
				  "DragMaximizedWindow = Move; "
				  "MoveHalfMaximizedWindowsBetweenScreens = NO; "
				  "AlternativeHalfMaximized = NO; "
				  "PointerWithHalfMaxWindows = NO; "
				  "HighlightActiveApp = YES; AutoArrangeIcons = NO; }");
	new_dict = dict_from_desc("{ IconPosition = trv; "
				  "PixmapPath = (\"/new/pix\", \"/new/bg\"); "
				  "IconPath = (\"/new/icons\", \"/new/more\"); "
				  "StickyIcons = YES; WrapAppiconsInDock = NO; "
				  "WindowPlaceOrigin = (900, \"-4\"); "
				  "KeepDockOnPrimaryHead = YES; "
				  "ScreenshotFilenameTemplate = \"shot_%S\"; "
				  "MenuStyle = flat; TitleJustify = Right; "
				  "WindowTitleExtendSpace = 1; "
				  "WindowTitleMinHeight = 2; "
				  "WindowTitleMaxHeight = 30; "
				  "MenuTitleExtendSpace = 3; "
				  "MenuTitleMinHeight = 4; "
				  "MenuTitleMaxHeight = 40; "
				  "MenuTextExtendSpace = 5; "
				  "IconificationStyle = Twist; "
				  "EnforceIconMargin = YES; DisableWSMouseActions = YES; "
				  "MouseLeftButtonAction = MoveToNextWindow; "
				  "MouseMiddleButtonAction = MoveToPrevWindow; "
				  "MouseRightButtonAction = MoveToNextWorkspace; "
				  "MouseBackwardButtonAction = MoveToPrevWorkspace; "
				  "MouseForwardButtonAction = OpenApplicationsMenu; "
				  "MouseWheelAction = SwitchWorkspaces; "
				  "MouseWheelTiltAction = SwitchWindows; "
				  "DontConfirmKill = YES; WindowTitleBalloons = NO; "
				  "MiniwindowTitleBalloons = YES; "
				  "MiniwindowPreviewBalloons = YES; "
				  "AppIconBalloons = YES; HelpBalloons = YES; "
				  "EdgeResistance = 45; ResizeIncrement = 12; "
				  "Attraction = YES; DisableBlinking = YES; "
				  "SingleClickLaunch = YES; StrictWindozeCycle = NO; "
				  "SwitchPanelOnlyOpen = YES; MiniPreviewSize = 96; "
				  "IgnoreGtkHints = YES; NoWindowOverDock = YES; "
				  "NoWindowOverIcons = YES; ResizeDisplay = None; "
				  "MoveDisplay = Corner; "
#ifdef USE_RANDR
				  "HotplugMonitor = YES; "
#endif
				  "HotCorners = YES; HotCornerDelay = 77; "
				  "HotCornerEdge = 4; WindowListAppIcons = YES; "
				  "FindMouseDuration = 1500; "
				  "FindMouseRadius = 333; "
				  "MouseWheelFocus = YES; KeychainTimeoutDelay = 88; "
				  "KeychainDisplayPosition = topleft; "
				  "ShowClipTitle = NO; "
#ifdef KEEP_XKB_LOCK_STATUS
				  "KbdModeLock = YES; "
#endif
				  "DialogHistoryLines = 12; CycleActiveHeadOnly = YES; "
				  "CycleAllWorkspaces = YES; CycleIgnoreMinimized = YES; "
				  "DbClickMaximize = YES; "
				  "CloseRootMenuByLeftOrRightMouseClick = YES; "
				  "ColormapMode = Manual; AutoFocus = NO; "
				  "AutoFocusAcrossHeads = NO; RaiseDelay = 15; "
				  "CirculateRaise = YES; Superfluous = NO; "
				  "AdvanceToNewWorkspace = YES; CycleWorkspaces = YES; "
				  "SkipEmptyWorkspaces = YES; "
				  "AutoRemoveEmptyWorkspaces = YES; MinWorkspaceCount = 2; "
				  "WorkspaceNameDisplayPosition = bottomleft; "
				  "WorkspaceBorderSize = 4; ScrollableMenus = NO; "
				  "DoubleClickTime = 400; SaveSessionOnExit = YES; "
				  "MenuScrollSpeed = UltraFast; IconSlideSpeed = Medium; "
				  "ShadeSpeed = UltraSlow; BounceAppIconsWhenUrgent = NO; "
				  "BlinkTitlebarWhenUrgent = NO; "
				  "RaiseAppIconsWhenBouncing = YES; "
				  "DoNotMakeAppIconsBounce = YES; "
				  "ClipAutoraiseDelay = 1; ClipAutolowerDelay = 2; "
				  "ClipAutoexpandDelay = 3; ClipAutocollapseDelay = 4; "
				  "AlignSubmenus = YES; ViKeyMenus = YES; "
				  "OpenTransientOnOwnerWorkspace = YES; "
				  "IgnoreFocusClick = YES; UseSaveUnders = YES; "
				  "OpaqueMove = NO; OpaqueResize = YES; "
				  "OpaqueMoveResizeKeyboard = YES; DisableAnimations = YES; "
				  "DontLinkWorkspaces = NO; WindowSnapping = YES; "
				  "SharedEdgeResize = YES; "
				  "SnapEdgeDetect = 5; SnapCornerDetect = 15; "
				  "SnapToTopMaximizesFullscreen = YES; "
				  "DragMaximizedWindow = NoMove; "
				  "MoveHalfMaximizedWindowsBetweenScreens = YES; "
				  "AlternativeHalfMaximized = YES; "
				  "PointerWithHalfMaxWindows = YES; "
				  "HighlightActiveApp = NO; AutoArrangeIcons = YES; }");
	if (!old_dict || !new_dict)
		goto out;

	refresh = apply_runtime_defaults_for_test(&screen, new_dict, old_dict);
	CHECK(w_global.config->icon_yard == (IY_TOP | IY_RIGHT | IY_VERT));
	CHECK(strcmp(w_global.config->pixmap_path, "/new/pix:/new/bg") == 0);
	CHECK(strcmp(w_global.config->icon_path, "/new/icons:/new/more") == 0);
	CHECK(w_global.config->sticky_icons == 1);
	CHECK(wdisp->flags.wrap_appicons_in_dock == 0);
	CHECK(screen.window_place_origin.x == 300);
	CHECK(screen.window_place_origin.y == 0);
	CHECK(w_global.config->keep_dock_on_primary_head == 1);
	CHECK(strcmp(w_global.config->screenshot_filename_template, "shot_%S") == 0);
	CHECK(w_global.config->menu_style == MS_FLAT);
	CHECK(w_global.config->title_justification == WTJ_RIGHT);
	CHECK(w_global.config->window_title_clearance == 1);
	CHECK(w_global.config->window_title_min_height == 2);
	CHECK(w_global.config->window_title_max_height == 30);
	CHECK(w_global.config->menu_title_clearance == 3);
	CHECK(w_global.config->menu_title_min_height == 4);
	CHECK(w_global.config->menu_title_max_height == 40);
	CHECK(w_global.config->menu_text_clearance == 5);
	CHECK(w_global.config->iconification_style == WIS_TWIST);
	CHECK(w_global.config->enforce_icon_margin == 1);
	CHECK(w_global.config->disable_root_mouse == 1);
	CHECK(w_global.config->mouse_button1 == WA_MOVE_NEXTWINDOW);
	CHECK(w_global.config->mouse_button2 == WA_MOVE_PREVWINDOW);
	CHECK(w_global.config->mouse_button3 == WA_MOVE_NEXTWORKSPACE);
	CHECK(w_global.config->mouse_button8 == WA_MOVE_PREVWORKSPACE);
	CHECK(w_global.config->mouse_button9 == WA_OPEN_APPMENU);
	CHECK(w_global.config->mouse_wheel_scroll == WA_SWITCH_WORKSPACES);
	CHECK(w_global.config->mouse_wheel_tilt == WA_SWITCH_WINDOWS);
	CHECK(w_global.config->dont_confirm_kill == 1);
	CHECK(w_global.config->window_balloon == 0);
	CHECK(w_global.config->miniwin_title_balloon == 1);
	CHECK(w_global.config->miniwin_preview_balloon == 1);
	CHECK(w_global.config->appicon_balloon == 1);
	CHECK(w_global.config->help_balloon == 1);
	CHECK(w_global.config->edge_resistance == 45);
	CHECK(w_global.config->resize_increment == 12);
	CHECK(w_global.config->attract == 1);
	CHECK(w_global.config->dont_blink == 1);
	CHECK(w_global.config->single_click == 1);
	CHECK(w_global.config->strict_windoze_cycle == 0);
	CHECK(w_global.config->panel_only_open == 1);
	CHECK(w_global.config->minipreview_size == 96);
	CHECK(w_global.config->ignore_gtk_decoration_hints == 1);
	CHECK(w_global.config->no_window_over_dock == 1);
	CHECK(w_global.config->no_window_over_icons == 1);
	CHECK(w_global.config->size_display == WDIS_NONE);
	CHECK(w_global.config->move_display == WDIS_TOPLEFT);
#ifdef USE_RANDR
	CHECK(w_global.config->hotplug_monitor == 1);
#endif
	CHECK(w_global.config->hot_corners == 1);
	CHECK(w_global.config->hot_corner_delay == 77);
	CHECK(w_global.config->hot_corner_edge == 4);
	CHECK(w_global.config->find_mouse_duration == 1500);
	CHECK(w_global.config->find_mouse_radius == 333);
	CHECK(w_global.config->window_list_app_icons == 1);
	CHECK(w_global.config->mouse_wheel_focus == 1);
	CHECK(w_global.config->keychain_timeout_delay == 88);
	CHECK(w_global.config->keychain_display_position == WD_TOPLEFT);
	CHECK(w_global.config->show_clip_title == 0);
#ifdef KEEP_XKB_LOCK_STATUS
	CHECK(w_global.config->modelock == 1);
#endif
	CHECK(w_global.config->history_lines == 12);
	CHECK(w_global.config->cycle_active_head_only == 1);
	CHECK(w_global.config->cycle_all_workspaces == 1);
	CHECK(w_global.config->cycle_ignore_minimized == 1);
	CHECK(w_global.config->double_click_maximize == 1);
	CHECK(w_global.config->close_rootmenu_left_right_click == 1);
	CHECK(w_global.config->colormap_mode == WCM_CLICK);
	CHECK(w_global.config->auto_focus == 0);
	CHECK(w_global.config->auto_focus_across_heads == 0);
	CHECK(w_global.config->raise_delay == 15);
	CHECK(w_global.config->circ_raise == 1);
	CHECK(w_global.config->superfluous == 0);
	CHECK(w_global.config->ws_advance == 1);
	CHECK(w_global.config->auto_remove_empty_workspaces == 1);
	CHECK(w_global.config->min_workspace_count == 2);
	CHECK(w_global.config->ws_cycle == 1);
	CHECK(w_global.config->skip_empty_workspaces == 1);
	CHECK(w_global.config->workspace_name_display_position == WD_BOTTOMLEFT);
	CHECK(w_global.config->workspace_border_size == 4);
	CHECK(w_global.config->scrollable_menus == 0);
	CHECK(w_global.config->double_click_time == 400);
	CHECK(w_global.config->save_session_on_exit == 1);
	CHECK(w_global.config->menu_scroll_speed == SPEED_ULTRAFAST);
	CHECK(w_global.config->icon_slide_speed == SPEED_MEDIUM);
	CHECK(w_global.config->shade_speed == SPEED_ULTRASLOW);
	CHECK(w_global.config->bounce_appicons_when_urgent == 0);
	CHECK(w_global.config->blink_titlebar_when_urgent == 0);
	CHECK(w_global.config->raise_appicons_when_bouncing == 1);
	CHECK(w_global.config->do_not_make_appicons_bounce == 1);
	CHECK(w_global.config->clip_auto_raise_delay == 1);
	CHECK(w_global.config->clip_auto_lower_delay == 2);
	CHECK(w_global.config->clip_auto_expand_delay == 3);
	CHECK(w_global.config->clip_auto_collapse_delay == 4);
	CHECK(w_global.config->align_menus == 1);
	CHECK(w_global.config->vi_key_menus == 1);
	CHECK(w_global.config->open_transients_with_parent == 1);
	CHECK(w_global.config->ignore_focus_click == 1);
	CHECK(w_global.config->use_saveunders == 1);
	CHECK(w_global.config->opaque_move == 0);
	CHECK(w_global.config->opaque_resize == 1);
	CHECK(w_global.config->opaque_move_resize_keyboard == 1);
	CHECK(w_global.config->no_animations == 1);
	CHECK(w_global.config->no_autowrap == 0);
	CHECK(w_global.config->window_snapping == 1);
	CHECK(w_global.config->shared_edge_resize == 1);
	CHECK(w_global.config->snap_edge_detect == 5);
	CHECK(w_global.config->snap_corner_detect == 15);
	CHECK(w_global.config->snap_to_top_maximizes_fullscreen == 1);
	CHECK(w_global.config->drag_maximized_window == DRAGMAX_NOMOVE);
	CHECK(w_global.config->move_half_max_between_heads == 1);
	CHECK(w_global.config->alt_half_maximize == 1);
	CHECK(w_global.config->pointer_with_half_max_windows == 1);
	CHECK(w_global.config->highlight_active_app == 0);
	CHECK(w_global.config->auto_arrange_icons == 1);
	CHECK(double_click_delay == 400);
	CHECK(usable_area_updates == 1);
	CHECK(arrange_icons_all == 1);
	CHECK(arrange_icons_visible == 1);
	CHECK(workspace_force_changes == 1);
	CHECK(workspace_force_last == 3);
	CHECK(dock_swaps == 1);
	CHECK(root_mask_updates == 1);
	/* WCFG_REFRESH_WINDOW_TEXTURES: PixmapPath changed above so every
	 * texture spec is force re-applied under the new search path. */
	CHECK(refresh == (WCFG_REFRESH_MENU_TEXTURE |
			  WCFG_REFRESH_WINDOW_TITLE_COLOR |
			  WCFG_REFRESH_WINDOW_FONT |
			  WCFG_REFRESH_BUTTON_IMAGES |
			  WCFG_REFRESH_MENU_TITLE_FONT |
			  WCFG_REFRESH_MENU_FONT |
			  WCFG_REFRESH_WINDOW_TEXTURES));

out:
	if (old_dict)
		WMReleasePropList(old_dict);
	if (new_dict)
		WMReleasePropList(new_dict);
}

static void test_runtime_apply_phase4(void)
{
	WMPropList *old_dict, *new_dict;
	WScreen screen;
	unsigned int refresh;

	reset_preferences();
	memset(&screen, 0, sizeof(screen));
	old_dict = dict_from_desc("{ HotCornerActions = (None, None, None, None); "
				  "SwitchPanelImages = (swtile.png, swback.png, 30, 40); "
				  "ModifierKeyLabels = (\"Shift+\", \"Control+\", \"Mod1+\", \"Mod2+\", \"Mod3+\", \"Mod4+\", \"Mod5+\"); "
				  "ModifierKeyShortLabels = (\"Sh+\", \"^\", \"M1+\", \"M2+\", \"M3+\", \"M4+\", \"M5+\", \"M+\", \"A+\"); "
				  "RootMenuKey = F12; Workspace1Key = \"Mod1+1\"; }");
	new_dict = dict_from_desc("{ HotCornerActions = (OpenRootMenu, None, Run, Workspace1); "
				  "SwitchPanelImages = (tile.png, back.png, 12, 13); "
				  "ModifierKeyLabels = (\"S+\", \"C+\", \"M1+\", \"M2+\", \"M3+\", \"M4+\", \"M5+\"); "
				  "ModifierKeyShortLabels = (S, C, M1, M2, M3, M4, M5, M, A); "
				  "RootMenuKey = \"Mod1+X Control+Y\"; "
				  "Workspace1Key = \"Mod4+1\"; }");
	if (!old_dict || !new_dict)
		goto out;

	refresh = apply_runtime_defaults_for_test(&screen, new_dict, old_dict);
	CHECK(hot_corner_updates == 1);
	CHECK(strcmp(screen.hot_corner_actions[0], "OpenRootMenu") == 0);
	CHECK(screen.hot_corner_actions[1] == NULL);
	CHECK(strcmp(screen.hot_corner_actions[2], "Run") == 0);
	CHECK(switch_panel_updates == 1);
	CHECK(strcmp(switch_panel_tile, "tile.png") == 0);
	CHECK(strcmp(switch_panel_background, "back.png") == 0);
	CHECK(switch_panel_center_width == 12);
	CHECK(switch_panel_center_height == 13);
	CHECK(modifier_label_updates == 1);
	CHECK(strcmp(wdisp->modifier_labels[0], "S+") == 0);
	CHECK(modifier_short_label_updates == 1);
	CHECK(strcmp(wdisp->modifier_short_labels[8], "A") == 0);
	CHECK(key_binding_updates == 2);
	CHECK(key_binding_seen[WKBD_ROOTMENU] == 1);
	CHECK(key_binding_seen[WKBD_WORKSPACE1] == 1);
	CHECK(key_binding_changed_updates == 2);
	CHECK(key_tree_rebuilds == 1);
	CHECK(key_regrabs == 1);
	CHECK(refresh == WCFG_REFRESH_WORKSPACE_MENU);

out:
	for (size_t i = 0; i < 4; i++)
		if (screen.hot_corner_actions[i])
			wfree(screen.hot_corner_actions[i]);
	if (old_dict)
		WMReleasePropList(old_dict);
	if (new_dict)
		WMReleasePropList(new_dict);
}


static void test_runtime_apply_phase5(void)
{
	WMPropList *old_dict, *new_dict;
	WScreen screen;
	unsigned int refresh;

	reset_preferences();
	memset(&screen, 0, sizeof(screen));
	old_dict = dict_from_desc("{ WidgetColor = (solid, gray); "
				  "WindowTitleFont = \"Sans:bold:pixelsize=12\"; "
				  "HighlightColor = white; "
				  "NormalCursor = (builtin, left_ptr); }");
	new_dict = dict_from_desc("{ WidgetColor = (solid, blue); "
				  "WindowTitleFont = \"Sans 10\"; "
				  "HighlightColor = red; "
				  "NormalCursor = (builtin, watch); }");
	if (!old_dict || !new_dict)
		goto out;

	refresh = apply_runtime_defaults_for_test(&screen, new_dict, old_dict);
	CHECK(texture_spec_updates == 1);
	CHECK(font_spec_updates == 1);
	CHECK(color_spec_updates == 1);
	CHECK(cursor_spec_updates == 1);
	CHECK(last_texture_target == WCFG_TEXTURE_WIDGET_COLOR);
	CHECK(last_font_target == WCFG_FONT_WINDOW_TITLE);
	CHECK(last_color_target == WCFG_COLOR_HIGHLIGHT);
	CHECK(last_cursor_target == WCFG_CURSOR_NORMAL);
	CHECK(strcmp(last_texture_spec, "(solid, blue)") == 0);
	CHECK(strcmp(last_font_spec, "Sans 10") == 0);
	CHECK(strcmp(last_color_spec, "red") == 0);
	CHECK(strcmp(last_cursor_spec, "(builtin, watch)") == 0);
	CHECK(refresh == (WCFG_REFRESH_WINDOW_TEXTURES |
			  WCFG_REFRESH_WINDOW_FONT |
			  WCFG_REFRESH_MENU_COLOR));

out:
	if (old_dict)
		WMReleasePropList(old_dict);
	if (new_dict)
		WMReleasePropList(new_dict);
}

/* The per-screen "changed" result is false from the second screen on,
 * but every screen must still regrab. */
static void test_keybinding_regrab_all_screens(void)
{
	WMPropList *old_dict, *new_dict;
	WConfig *old_config, *new_config;
	WConfigChanges changes;
	WScreen screen0, screen1;

	reset_preferences();
	memset(&screen0, 0, sizeof(screen0));
	memset(&screen1, 0, sizeof(screen1));
	old_dict = dict_from_desc("{ RootMenuKey = F12; }");
	new_dict = dict_from_desc("{ RootMenuKey = \"Mod1+X\"; }");
	if (!old_dict || !new_dict)
		goto out;

	wConfigCacheWindowMaker(old_dict);
	old_config = w_global.config;
	new_config = wConfigReadWindowMaker(new_dict);
	wConfigPrepareRuntimeDefaults(new_dict, old_config, new_config, &changes);
	w_global.config = new_config;
	wdisp->config = new_config;

	wConfigApplyRuntime(&screen0, old_config, new_config, &changes);
	CHECK(key_regrabs == 1);
	CHECK(key_tree_rebuilds == 1);
	wConfigApplyRuntime(&screen1, old_config, new_config, &changes);
	CHECK(key_binding_changed_updates == 1);
	CHECK(key_regrabs == 2);
	CHECK(key_tree_rebuilds == 2);
	wConfigDestroy(old_config);

out:
	if (old_dict)
		WMReleasePropList(old_dict);
	if (new_dict)
		WMReleasePropList(new_dict);
	wConfigClearWindowMaker();
}

static void test_runtime_apply_uses_cached_old_config(void)
{
	WMPropList *cache_dict, *old_dict, *new_dict;
	unsigned int refresh;

	reset_preferences();
	cache_dict = dict_from_desc("{ MenuStyle = flat; }");
	old_dict = dict_from_desc("{ MenuStyle = normal; }");
	new_dict = dict_from_desc("{ MenuStyle = flat; }");
	if (!cache_dict || !old_dict || !new_dict)
		goto out;

	wConfigCacheWindowMaker(cache_dict);
	refresh = wConfigApplyRuntimeDefaults(NULL, new_dict);
	CHECK(refresh == 0);
	CHECK(w_global.config->menu_style == MS_FLAT);

out:
	if (cache_dict)
		WMReleasePropList(cache_dict);
	if (old_dict)
		WMReleasePropList(old_dict);
	if (new_dict)
		WMReleasePropList(new_dict);
	wConfigClearWindowMaker();
}

static void test_startup_only_frozen_across_reload(void)
{
	WMPropList *old_dict, *new_dict;

	reset_preferences();
	/* simulate StartUp(): adopt the boot-time parse and clamp it.
	 * FocusMode, EnableWorkspacePager, SwitchPanelIconSize,
	 * DisableMiniwindows and IconSize used to be in this set but are
	 * now WCFG_RUNTIME_RELOAD (see the matching test_*_live_apply
	 * cases); NewStyle and the dock-presence flags are still frozen. */
	old_dict = dict_from_desc("{ DisableDock = YES; "
				  "NewStyle = next; }");
	if (!old_dict)
		return;
	wConfigCacheWindowMaker(old_dict);
	CHECK(w_global.config->new_style == TS_NEXT);

	/* runtime reload with the startup-only keys edited or removed,
	 * none of this may leak through */
	new_dict = dict_from_desc("{ NewStyle = old; }");
	if (!new_dict)
		goto out;
	wConfigApplyRuntimeDefaults(NULL, new_dict);
	CHECK(w_global.config->new_style == TS_NEXT);
	CHECK(w_global.config->disable_dock == 1);

	WMReleasePropList(new_dict);
out:
	WMReleasePropList(old_dict);
	wConfigClearWindowMaker();
}

/* FocusMode is read per-event with no cached derivative, so unfreezing it
 * is enough: a runtime change must propagate to w_global.config. */
/* DockEdge: each of the four names parses to its WDockEdge value and
 * stores back as the same db string. */
static void test_dock_edge_roundtrip(void)
{
	static const struct {
		const char *name;
		WDockEdge edge;
	} cases[] = {
		{ "Left",   WDOCK_EDGE_LEFT },
		{ "Right",  WDOCK_EDGE_RIGHT },
		{ "Top",    WDOCK_EDGE_TOP },
		{ "Bottom", WDOCK_EDGE_BOTTOM },
	};
	size_t i;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		WMPropList *old_dict, *new_dict;

		reset_preferences();
		old_dict = dict_from_desc("{ DockEdge = Right; }");
		new_dict = dict_from_desc("{ }");
		if (old_dict && new_dict)
			dict_put_string(new_dict, "DockEdge", cases[i].name);
		if (!old_dict || !new_dict)
			goto out;
		apply_runtime_defaults_for_test(NULL, new_dict, old_dict);
		CHECK(w_global.config->dock_edge == cases[i].edge);
		CHECK(wConfigSchemaEnumTable("DockEdge") != NULL);

out:
		if (old_dict)
			WMReleasePropList(old_dict);
		if (new_dict)
			WMReleasePropList(new_dict);
	}
	wConfigClearWindowMaker();
}

static void test_focus_mode_live_apply(void)
{
	WMPropList *old_dict, *new_dict;

	reset_preferences();
	old_dict = dict_from_desc("{ FocusMode = manual; }");
	new_dict = dict_from_desc("{ FocusMode = sloppy; }");
	if (!old_dict || !new_dict)
		goto out;
	apply_runtime_defaults_for_test(NULL, new_dict, old_dict);
	CHECK(w_global.config->focus_mode == WKF_SLOPPY);

out:
	if (old_dict)
		WMReleasePropList(old_dict);
	if (new_dict)
		WMReleasePropList(new_dict);
	wConfigClearWindowMaker();
}

/* IconSize: a runtime change re-derives every cached derivative via
 * wScreenApplyIconSize, a forced IconBack reapply, wIconResizeAll and
 * wDockApplyIconSize, then refreshes the usable area and rearranges
 * icons. The cold-boot pass is gated out by scr->icon_tile so the
 * first IconBack apply in the texture loop is not duplicated. */
static void test_icon_size_live_apply(void)
{
	WMPropList *old_dict, *new_dict;
	WScreen screen;
	int dummy_tile;

	reset_preferences();
	memset(&screen, 0, sizeof(screen));
	old_dict = dict_from_desc("{ IconSize = 64; }");
	new_dict = dict_from_desc("{ IconSize = 48; }");
	if (!old_dict || !new_dict)
		goto out;

	/* cold-boot pass: icon_tile is NULL so the apply block is skipped
	 * and the value is just adopted */
	apply_runtime_defaults_for_test(&screen, old_dict, NULL);
	CHECK(w_global.config->icon_size == 64);
	CHECK(screen_apply_icon_size == 0);
	CHECK(icon_resize_all == 0);

	/* runtime change with icon_tile set: every helper fires */
	screen.icon_tile = (void *)&dummy_tile;
	usable_area_updates = 0;
	arrange_icons_all = 0;
	texture_spec_updates = 0;
	apply_runtime_defaults_for_test(&screen, new_dict, old_dict);
	CHECK(w_global.config->icon_size == 48);
	CHECK(screen_apply_icon_size == 1);
	CHECK(icon_resize_all == 1);
	CHECK(dock_apply_icon_size == 1);
	CHECK(usable_area_updates == 1);
	CHECK(arrange_icons_all == 1);
	CHECK(texture_spec_updates == 1);
	CHECK(last_texture_target == WCFG_TEXTURE_ICON_BACK);

out:
	if (old_dict)
		WMReleasePropList(old_dict);
	if (new_dict)
		WMReleasePropList(new_dict);
	wConfigClearWindowMaker();
}

/* DisableMiniwindows is read at every minimize/restore decision; flipping
 * it live walks every miniaturized window via wRefreshMiniwindows() so a
 * NO -> YES tears down stale miniwindows and YES -> NO creates them. */
static void test_disable_miniwindows_live_apply(void)
{
	WMPropList *old_dict, *new_dict;
	WScreen screen;

	reset_preferences();
	memset(&screen, 0, sizeof(screen));
	old_dict = dict_from_desc("{ DisableMiniwindows = NO; }");
	new_dict = dict_from_desc("{ DisableMiniwindows = YES; }");
	if (!old_dict || !new_dict)
		goto out;
	apply_runtime_defaults_for_test(&screen, new_dict, old_dict);
	CHECK(w_global.config->disable_miniwindows == 1);
	CHECK(refresh_miniwindows == 1);

out:
	if (old_dict)
		WMReleasePropList(old_dict);
	if (new_dict)
		WMReleasePropList(new_dict);
	wConfigClearWindowMaker();
}

/* BlinkTitlebarWhenUrgent re-arms or disarms the per-window blink
 * timers for every window on the screen when it changes. */
static void test_blink_titlebar_when_urgent_live_apply(void)
{
	WMPropList *old_dict, *new_dict;
	WScreen screen;
	WWindow wwin;

	reset_preferences();
	memset(&screen, 0, sizeof(screen));
	memset(&wwin, 0, sizeof(wwin));
	screen.focused_window = &wwin;
	old_dict = dict_from_desc("{ BlinkTitlebarWhenUrgent = YES; }");
	new_dict = dict_from_desc("{ BlinkTitlebarWhenUrgent = NO; }");
	if (!old_dict || !new_dict)
		goto out;
	urgent_blink_calls = 0;
	apply_runtime_defaults_for_test(&screen, new_dict, old_dict);
	CHECK(w_global.config->blink_titlebar_when_urgent == 0);
	CHECK(urgent_blink_calls == 1);

	urgent_blink_calls = 0;
	apply_runtime_defaults_for_test(&screen, new_dict, new_dict);
	CHECK(urgent_blink_calls == 0);

out:
	if (old_dict)
		WMReleasePropList(old_dict);
	if (new_dict)
		WMReleasePropList(new_dict);
	wConfigClearWindowMaker();
}

/* SwitchPanelIconSize is re-read by wInitSwitchPanel() on every Alt-Tab
 * and the cached SwitchPanelImages tile is stored at source resolution
 * and scaled per construction, so unfreezing alone makes the next panel
 * pick up the new size. */
static void test_switch_panel_icon_size_live_apply(void)
{
	WMPropList *old_dict, *new_dict;

	reset_preferences();
	old_dict = dict_from_desc("{ SwitchPanelIconSize = 64; }");
	new_dict = dict_from_desc("{ SwitchPanelIconSize = 40; }");
	if (!old_dict || !new_dict)
		goto out;
	apply_runtime_defaults_for_test(NULL, new_dict, old_dict);
	CHECK(w_global.config->switch_panel_icon_size == 40);

out:
	if (old_dict)
		WMReleasePropList(old_dict);
	if (new_dict)
		WMReleasePropList(new_dict);
	wConfigClearWindowMaker();
}

/* PixmapPath is resolved at texture/cursor/switch-panel apply time and the
 * result cached for the session, so a runtime PixmapPath change must force
 * a re-apply of every such consumer (and tell a running background helper
 * the new search path) even when their own values did not change. */
static void test_pixmap_path_live_apply(void)
{
	WMPropList *old_dict, *new_dict;
	const WConfigEntry *schema;
	WScreen screen;
	size_t j, count;
	int textures = 0, cursors = 0;

	reset_preferences();
	memset(&screen, 0, sizeof(screen));
	screen.flags.backimage_helper_launched = 1;

	schema = wConfigSchema();
	count = wConfigSchemaCount();
	for (j = 0; j < count; j++) {
		if (schema[j].type == WCFG_TEXTURE_SPEC)
			textures++;
		else if (schema[j].type == WCFG_CURSOR_SPEC)
			cursors++;
	}

	old_dict = dict_from_desc("{ PixmapPath = (\"/old\"); }");
	new_dict = dict_from_desc("{ PixmapPath = (\"/new/pix\"); }");
	if (!old_dict || !new_dict)
		goto out;

	apply_runtime_defaults_for_test(&screen, new_dict, old_dict);
	CHECK(strcmp(w_global.config->pixmap_path, "/new/pix") == 0);
	CHECK(switch_panel_updates == 1);
	CHECK(texture_spec_updates == textures);
	CHECK(cursor_spec_updates == cursors);
	CHECK(helper_messages == 1);
	CHECK(last_helper_type == 'P');
	CHECK(last_helper_msg && strcmp(last_helper_msg, "/new/pix") == 0);

	/* no helper running, no 'P' message */
	reset_phase4_counters();
	screen.flags.backimage_helper_launched = 0;
	apply_runtime_defaults_for_test(&screen, old_dict, new_dict);
	CHECK(switch_panel_updates == 1);
	CHECK(texture_spec_updates == textures);
	CHECK(helper_messages == 0);

	/* PixmapPath unchanged, none of this fires */
	reset_phase4_counters();
	apply_runtime_defaults_for_test(&screen, old_dict, old_dict);
	CHECK(switch_panel_updates == 0);
	CHECK(texture_spec_updates == 0);
	CHECK(cursor_spec_updates == 0);
	CHECK(helper_messages == 0);

out:
	if (old_dict)
		WMReleasePropList(old_dict);
	if (new_dict)
		WMReleasePropList(new_dict);
	wConfigClearWindowMaker();
}

/* wConfigSchemaRequiresRestart() is the WPrefs-visible view of the
 * WCFG_STARTUP_ONLY schema flag: nonzero for a still-frozen key, zero for
 * a runtime-reload key or an unknown key. */
static void test_schema_requires_restart(void)
{
	const WConfigEntry *schema;
	size_t j, count;
	int frozen = 0;

	CHECK(wConfigSchemaRequiresRestart("PerHeadWorkspaces"));
	CHECK(wConfigSchemaRequiresRestart("NewStyle"));
	CHECK(wConfigSchemaRequiresRestart("DisableDock"));
	CHECK(!wConfigSchemaRequiresRestart("IconSize"));
	CHECK(!wConfigSchemaRequiresRestart("FocusMode"));
	CHECK(!wConfigSchemaRequiresRestart("PointerFollowsFocus"));
	CHECK(!wConfigSchemaRequiresRestart("PointerFollowsMonitor"));
	CHECK(!wConfigSchemaRequiresRestart("PixmapPath"));
	CHECK(!wConfigSchemaRequiresRestart("NoSuchKey"));
	CHECK(!wConfigSchemaRequiresRestart(NULL));

	/* the accessor and the schema flag must agree for every key */
	schema = wConfigSchema();
	count = wConfigSchemaCount();
	for (j = 0; j < count; j++) {
		int want = (schema[j].flags & WCFG_STARTUP_ONLY) != 0;
		CHECK(!wConfigSchemaRequiresRestart(schema[j].key) == !want);
		if (want)
			frozen++;
	}
	CHECK(frozen > 0);
}

/* EnableWorkspacePager is read per-event; no NETWM hint depends on it, so
 * unfreezing suffices and the new value propagates on reload. */
static void test_enable_workspace_pager_live_apply(void)
{
	WMPropList *old_dict, *new_dict;

	reset_preferences();
	old_dict = dict_from_desc("{ EnableWorkspacePager = NO; }");
	new_dict = dict_from_desc("{ EnableWorkspacePager = YES; }");
	if (!old_dict || !new_dict)
		goto out;
	apply_runtime_defaults_for_test(NULL, new_dict, old_dict);
	CHECK(w_global.config->enable_workspace_pager == 1);

out:
	if (old_dict)
		WMReleasePropList(old_dict);
	if (new_dict)
		WMReleasePropList(new_dict);
	wConfigClearWindowMaker();
}

/* ModifierKey now resolves on the runtime path: the mask updates and the
 * modifier-bound button grabs are re-issued for every managed window. Key
 * grabs use the per-shortcut modifier and are not re-grabbed here. */
static void test_modifier_key_live_apply(void)
{
	WMPropList *old_dict, *new_dict;
	WScreen screen;

	reset_preferences();
	memset(&screen, 0, sizeof(screen));
	old_dict = dict_from_desc("{ ModifierKey = Mod1; }");
	new_dict = dict_from_desc("{ ModifierKey = Mod4; }");
	if (!old_dict || !new_dict)
		goto out;
	apply_runtime_defaults_for_test(&screen, new_dict, old_dict);
	CHECK(modifier_key_updates == 1);
	CHECK(strcmp(last_modifier_key_spec, "Mod4") == 0);
	CHECK(wdisp->modifier_mask == XCB_MOD_MASK_4);
	CHECK(regrab_mouse_grabs == 1);

out:
	if (old_dict)
		WMReleasePropList(old_dict);
	if (new_dict)
		WMReleasePropList(new_dict);
	wConfigClearWindowMaker();
}

/* wConfigForceReapplyFonts re-applies every FONT_SPEC entry (used after a
 * WINGs font-cache detach so WM-side fonts re-open under the new AA flag). */
static void test_force_reapply_fonts(void)
{
	WScreen screen;
	unsigned int refresh;

	reset_preferences();
	memset(&screen, 0, sizeof(screen));
	wConfigCacheWindowMaker(NULL);
	font_spec_updates = 0;
	refresh = wConfigForceReapplyFonts(&screen);
	CHECK(font_spec_updates == 6);
	CHECK(refresh != 0);

	wConfigClearWindowMaker();
}

/* AntialiasedText (WMGLOBAL) live-apply: wConfigApplyAntialiasedText detects
 * a change against the screen's last-applied value and, on a change, flushes
 * the WINGs font cache and re-applies the WM-side fonts; a no-op otherwise. */
static void test_antialiased_text_live_apply(void)
{
	WScreen screen;
	unsigned int refresh;
	static char wmscreen_dummy;

	reset_preferences();
	memset(&screen, 0, sizeof(screen));
	screen.wmscreen = (WMScreen *)&wmscreen_dummy;
	wConfigCacheWindowMaker(NULL);

	/* applied == current (both false after reset) -> no flush */
	screen.antialiased_text_applied = aa_current;
	font_spec_updates = 0;
	aa_flush_count = 0;
	refresh = wConfigApplyAntialiasedText(&screen);
	CHECK(refresh == 0);
	CHECK(aa_flush_count == 0);
	CHECK(font_spec_updates == 0);
	CHECK(screen.antialiased_text_applied == aa_current);

	/* WMGLOBAL AntialiasedText flipped on -> flush + re-apply fonts */
	aa_current = true;
	font_spec_updates = 0;
	aa_flush_count = 0;
	refresh = wConfigApplyAntialiasedText(&screen);
	CHECK(aa_flush_count == 1);
	CHECK(font_spec_updates == 6);
	CHECK(screen.antialiased_text_applied == true);
	CHECK(refresh != 0);

	/* second call with no further change -> no-op */
	font_spec_updates = 0;
	aa_flush_count = 0;
	refresh = wConfigApplyAntialiasedText(&screen);
	CHECK(refresh == 0);
	CHECK(aa_flush_count == 0);
	CHECK(font_spec_updates == 0);

	wConfigClearWindowMaker();
}

/* A WMGLOBAL system-color change contributes the system-color refresh bit
 * so wReadDefaults posts the appearance notifications and repainted
 * surfaces stop referencing the freed pseudocolor cells. */
static void test_system_color_refresh_mask(void)
{
	WScreen screen;
	unsigned int refresh;
	static char wmscreen_dummy;

	reset_preferences();
	memset(&screen, 0, sizeof(screen));

	/* no WMScreen yet -> no bit, no refresh */
	refresh = wConfigApplySystemColors(&screen);
	CHECK(refresh == 0);
	CHECK(system_color_refreshes == 0);

	screen.wmscreen = (WMScreen *)&wmscreen_dummy;
	refresh = wConfigApplySystemColors(&screen);
	CHECK(refresh == WCFG_REFRESH_SYSTEM_COLOR);
	CHECK(system_color_refreshes == 1);

	refresh = wConfigApplySystemColors(NULL);
	CHECK(refresh == 0);
	CHECK(system_color_refreshes == 1);
}

static void test_runtime_apply_phase7(void)
{
	WMPropList *old_dict, *new_dict;
	WScreen screen;
	unsigned int refresh;

	reset_preferences();
	memset(&screen, 0, sizeof(screen));
	old_dict = dict_from_desc("{ WorkspaceSpecificBack = (); "
				  "WorkspaceBack = (solid, black); "
				  "SmoothWorkspaceBack = NO; FrameBorderWidth = 1; }");
	new_dict = dict_from_desc("{ WorkspaceSpecificBack = ((solid, red)); "
				  "WorkspaceBack = (solid, blue); "
				  "SmoothWorkspaceBack = YES; FrameBorderWidth = 5; }");
	if (!old_dict || !new_dict)
		goto first_out;

	refresh = apply_runtime_defaults_for_test(&screen, new_dict, old_dict);
	CHECK(background_spec_updates == 2);
	CHECK(workspace_specific_background_updates == 1);
	CHECK(workspace_background_updates == 1);
	CHECK(last_background_target == WCFG_BACKGROUND_WORKSPACE);
	CHECK(strcmp(last_background_spec, "(solid, blue)") == 0);
	/* the new config is adopted before side effects run, so the
	 * background apply already sees the new SmoothWorkspaceBack */
	CHECK(workspace_background_saw_smooth == 1);
	CHECK(w_global.config->smooth_workspace_back == 1);
	CHECK(screen.frame_border_width == 5);
	CHECK(refresh == WCFG_REFRESH_FRAME_BORDER);

first_out:
	if (old_dict)
		WMReleasePropList(old_dict);
	if (new_dict)
		WMReleasePropList(new_dict);

	reset_preferences();
	memset(&screen, 0, sizeof(screen));
	old_dict = dict_from_desc("{ WorkspaceSpecificBack = (); "
				  "WorkspaceBack = (solid, black); }");
	new_dict = dict_from_desc("{ WorkspaceSpecificBack = ((solid, red)); "
				  "WorkspaceBack = (solid, black); }");
	if (!old_dict || !new_dict)
		goto second_out;

	refresh = apply_runtime_defaults_for_test(&screen, new_dict, old_dict);
	CHECK(background_spec_updates == 2);
	CHECK(workspace_specific_background_updates == 1);
	CHECK(workspace_background_updates == 1);
	CHECK(strcmp(last_background_spec, "(solid, black)") == 0);
	CHECK(refresh == 0);

second_out:
	if (old_dict)
		WMReleasePropList(old_dict);
	if (new_dict)
		WMReleasePropList(new_dict);
}

static void test_cursor_hide_live_reload(void)
{
	WMPropList *old_dict, *new_dict;
	unsigned int refresh;

	reset_preferences();
	old_dict = dict_from_desc("{ AutoHideCursor = NO; "
				  "HideCursorAfterKeyboard = YES; "
				  "HideCursorAfterIdle = NO; "
				  "HideCursorIdleDelay = 5000; }");
	new_dict = dict_from_desc("{ AutoHideCursor = YES; "
				  "HideCursorAfterKeyboard = NO; "
				  "HideCursorAfterIdle = YES; "
				  "HideCursorIdleDelay = 42000; }");
	if (!old_dict || !new_dict)
		goto out;

	refresh = apply_runtime_defaults_for_test(NULL, new_dict, old_dict);
	CHECK(w_global.config->auto_hide_cursor == 1);
	CHECK(w_global.config->hide_cursor_after_keyboard == 0);
	CHECK(w_global.config->hide_cursor_after_idle == 1);
	CHECK(w_global.config->hide_cursor_idle_delay == 42000);
	CHECK(refresh == 0);

out:
	if (old_dict)
		WMReleasePropList(old_dict);
	if (new_dict)
		WMReleasePropList(new_dict);
	reset_preferences();
}

/* wConfigClone deep-copies a WConfig: the clone is value-equal to the
 * source (no diff), mutating one field on the clone flags exactly that key,
 * and both can be destroyed independently without aliasing/double-free. */
static void test_wConfigClone(void)
{
	WMPropList *dict;
	WConfig *config, *clone;
	WConfigChanges changes;
	size_t i;

	dict = dict_from_desc("{ IconSize = 48; ModifierKey = Mod4; "
			      "FTitleColor = black; "
			      "UTitleBack = (solid, \"#123456\"); }");
	if (!dict)
		return;
	config = wConfigReadWindowMaker(dict);
	CHECK(config != NULL);
	clone = wConfigClone(config);
	CHECK(clone != NULL);

	/* clone is value-equal to the source: an empty change set */
	CHECK(wConfigDiff(config, clone, &changes));
	for (i = 0; i < WCFG_CHANGE_WORDS; i++)
		CHECK(changes.changed[i] == 0);

	/* mutate one palette field on the clone only */
	wfree(clone->ftitle_color.spec);
	clone->ftitle_color.spec = wstrdup("#abcdef");
	CHECK(wConfigDiff(config, clone, &changes));
	CHECK(wConfigChangesHas(&changes, WCFG_KEY_F_TITLE_COLOR));
	CHECK(!wConfigChangesHas(&changes, WCFG_KEY_U_TITLE_BACK));

	/* independent lifetimes: destroying both must not double-free */
	wConfigDestroy(clone);
	wConfigDestroy(config);
	WMReleasePropList(dict);
}

static void test_inner_frame_live_apply(void)
{
	WMPropList *old_dict, *new_dict;
	WScreen screen;
	unsigned int refresh;

	reset_preferences();
	memset(&screen, 0, sizeof(screen));
	old_dict = dict_from_desc("{ FrameInnerBorderWidth = 2; "
				  "FrameInnerBorderSides = (left, right, top, bottom); "
				  "FrameInnerBorderColor = green; "
				  "FrameInnerFocusedBorderColor = yellow; }");
	new_dict = dict_from_desc("{ FrameInnerBorderWidth = 4; "
				  "FrameInnerBorderSides = (top, left); "
				  "FrameInnerBorderColor = red; "
				  "FrameInnerFocusedBorderColor = blue; }");
	if (!old_dict || !new_dict)
		goto out;

	refresh = apply_runtime_defaults_for_test(&screen, new_dict, old_dict);
	CHECK(screen.frame_inner_border_width == 4);
	CHECK(screen.frame_inner_border_sides == (INNER_SIDE_TOP | INNER_SIDE_LEFT));
	CHECK(color_spec_updates == 2);
	CHECK(color_targets[0] == WCFG_COLOR_FRAME_INNER_BORDER);
	CHECK(color_targets[1] == WCFG_COLOR_FRAME_INNER_FOCUSED_BORDER);
	CHECK(last_color_target == WCFG_COLOR_FRAME_INNER_FOCUSED_BORDER);
	CHECK(strcmp(last_color_spec, "blue") == 0);
	CHECK(refresh == (WCFG_REFRESH_INNER_BORDER |
				  WCFG_REFRESH_MENU_COLOR));

out:
	if (old_dict)
		WMReleasePropList(old_dict);
	if (new_dict)
		WMReleasePropList(new_dict);
}

static void test_empty_color_uses_default(void)
{
	WMPropList *dict;
	WConfig *config;

	dict = dict_from_desc("{ FTitleColor = \"\"; "
			      "FrameInnerBorderColor = \"\"; "
			      "FrameInnerFocusedBorderColor = \"\"; "
			      "TabInactiveTitleColor = \"\"; }");
	if (!dict)
		return;

	config = wConfigReadWindowMaker(dict);
	CHECK(strcmp(config->ftitle_color.spec, "white") == 0);
	CHECK(strcmp(config->frame_inner_border_color.spec, "#b5b5b5") == 0);
	CHECK(strcmp(config->frame_inner_focused_border_color.spec, "white") == 0);
	CHECK(config->tab_inactive_title_color.spec[0] == '\0');
	wConfigDestroy(config);
	WMReleasePropList(dict);
}

static void test_deleted_key_keeps_current(void)
{
	WMPropList *old_dict, *new_dict;
	WScreen screen;
	unsigned int refresh;

	reset_preferences();
	memset(&screen, 0, sizeof(screen));
	screen.scr_width = 900;
	screen.scr_height = 600;
	screen.window_place_origin.x = 123;
	screen.window_place_origin.y = 45;
	wdisp->flags.wrap_appicons_in_dock = 0;

	old_dict = dict_from_desc("{ HotCornerActions = (OpenRootMenu, None, Run, Workspace1); "
				  "WorkspaceSpecificBack = ((solid, black)); "
				  "WorkspaceBack = (solid, blue); "
				  "SmoothWorkspaceBack = YES; "
				  "WidgetColor = (solid, blue); "
				  "WindowTitleFont = \"Sans 10\"; "
				  "HighlightColor = red; "
				  "NormalCursor = (builtin, watch); "
				  "RootMenuKey = \"Mod1+X Control+Y\"; "
				  "IconPosition = trv; "
				  "PixmapPath = (\"/keep/pix\", \"/keep/bg\"); "
				  "IconPath = (\"/keep/icons\"); "
				  "StickyIcons = YES; WrapAppiconsInDock = NO; "
				  "WindowPlaceOrigin = (123, 45); "
				  "KeepDockOnPrimaryHead = YES; "
				  "ScreenshotFilenameTemplate = \"keep_%S\"; "
				  "MenuStyle = flat; TitleJustify = Right; "
				  "WindowTitleExtendSpace = 1; "
				  "WindowTitleMinHeight = 2; "
				  "WindowTitleMaxHeight = 30; "
				  "MenuTitleExtendSpace = 3; "
				  "MenuTitleMinHeight = 4; "
				  "MenuTitleMaxHeight = 40; "
				  "MenuTextExtendSpace = 5; "
				  "DisableWSMouseActions = YES; "
				  "MouseLeftButtonAction = MoveToNextWindow; "
				  "DontConfirmKill = YES; EdgeResistance = 66; "
				  "WindowTitleBalloons = NO; NoWindowOverDock = YES; "
				  "HotCorners = YES; KeychainTimeoutDelay = 88; "
				  "FindMouseDuration = 900; "
				  "FindMouseRadius = 77; "
				  "KeychainDisplayPosition = topleft; "
				  "DialogHistoryLines = 12; "
				  "AutoFocus = YES; WrapMenus = YES; "
				  "AlignSubmenus = YES; ClipAutoraiseDelay = 123; "
				  "UseSaveUnders = YES; SnapEdgeDetect = 77; }");
	new_dict = dict_from_desc("{ ScrollableMenus = YES; }");
	if (!old_dict || !new_dict)
		goto out;

	refresh = apply_runtime_defaults_for_test(&screen, new_dict, old_dict);
	CHECK(w_global.config->auto_focus == 1);
	CHECK(w_global.config->wrap_menus == 1);
	CHECK(w_global.config->align_menus == 1);
	CHECK(w_global.config->clip_auto_raise_delay == 123);
	CHECK(w_global.config->use_saveunders == 1);
	CHECK(w_global.config->snap_edge_detect == 77);
	CHECK(w_global.config->disable_root_mouse == 1);
	CHECK(w_global.config->mouse_button1 == WA_MOVE_NEXTWINDOW);
	CHECK(w_global.config->dont_confirm_kill == 1);
	CHECK(w_global.config->edge_resistance == 66);
	CHECK(w_global.config->window_balloon == 0);
	CHECK(w_global.config->icon_yard == (IY_TOP | IY_RIGHT | IY_VERT));
	CHECK(strcmp(w_global.config->pixmap_path, "/keep/pix:/keep/bg") == 0);
	CHECK(strcmp(w_global.config->icon_path, "/keep/icons") == 0);
	CHECK(w_global.config->sticky_icons == 1);
	CHECK(wdisp->flags.wrap_appicons_in_dock == 0);
	CHECK(screen.window_place_origin.x == 123);
	CHECK(screen.window_place_origin.y == 45);
	CHECK(w_global.config->keep_dock_on_primary_head == 1);
	CHECK(strcmp(w_global.config->screenshot_filename_template, "keep_%S") == 0);
	CHECK(w_global.config->menu_style == MS_FLAT);
	CHECK(w_global.config->title_justification == WTJ_RIGHT);
	CHECK(w_global.config->window_title_clearance == 1);
	CHECK(w_global.config->window_title_min_height == 2);
	CHECK(w_global.config->window_title_max_height == 30);
	CHECK(w_global.config->menu_title_clearance == 3);
	CHECK(w_global.config->menu_title_min_height == 4);
	CHECK(w_global.config->menu_title_max_height == 40);
	CHECK(w_global.config->menu_text_clearance == 5);
	CHECK(w_global.config->no_window_over_dock == 1);
	CHECK(w_global.config->hot_corners == 1);
	CHECK(w_global.config->find_mouse_duration == 900);
	CHECK(w_global.config->find_mouse_radius == 77);
	CHECK(w_global.config->keychain_timeout_delay == 88);
	CHECK(w_global.config->keychain_display_position == WD_TOPLEFT);
	CHECK(w_global.config->history_lines == 12);
	CHECK(w_global.config->smooth_workspace_back == 1);
	CHECK(hot_corner_updates == 0);
	CHECK(background_spec_updates == 0);
	CHECK(texture_spec_updates == 0);
	CHECK(font_spec_updates == 0);
	CHECK(color_spec_updates == 0);
	CHECK(cursor_spec_updates == 0);
	CHECK(key_binding_updates == 0);
	CHECK(refresh == 0);

out:
	if (old_dict)
		WMReleasePropList(old_dict);
	if (new_dict)
		WMReleasePropList(new_dict);
}

/* parse_string_array_plist() now treats min_value != max_value as a
 * range, the existing fixed-length callers (HotCornerActions etc) keep
 * min == max and still reject under/over */
static void test_string_array_range(void)
{
	const WConfigEntry *left = wConfigEntryForKey("TitlebarLeft");
	const WConfigEntry *hot = wConfigEntryForKey("HotCornerActions");
	WMPropList *dict;
	WConfig *config;

	CHECK(left != NULL && left->min_value == 0 && left->max_value == 8);
	CHECK(hot != NULL && hot->min_value == 4 && hot->max_value == 4);

	/* () accepted */
	dict = dict_from_desc("{ TitlebarLeft = (); }");
	config = wConfigReadWindowMaker(dict);
	CHECK(config->titlebar_left.count == 0);
	wConfigDestroy(config);
	WMReleasePropList(dict);

	/* one entry accepted */
	dict = dict_from_desc("{ TitlebarLeft = (A); }");
	config = wConfigReadWindowMaker(dict);
	CHECK(config->titlebar_left.count == 1);
	CHECK(strcmp(config->titlebar_left.items[0], "A") == 0);
	wConfigDestroy(config);
	WMReleasePropList(dict);

	/* eight entries accepted */
	dict = dict_from_desc("{ TitlebarLeft = (A,B,C,D,E,F,G,H); }");
	config = wConfigReadWindowMaker(dict);
	CHECK(config->titlebar_left.count == 8);
	CHECK(strcmp(config->titlebar_left.items[7], "H") == 0);
	wConfigDestroy(config);
	WMReleasePropList(dict);

	/* nine entries rejected, falls back to default */
	dict = dict_from_desc("{ TitlebarLeft = (A,B,C,D,E,F,G,H,I); }");
	config = wConfigReadWindowMaker(dict);
#ifdef XKB_BUTTON_HINT
	CHECK(config->titlebar_left.count == 2);
#else
	CHECK(config->titlebar_left.count == 1);
#endif
	wConfigDestroy(config);
	WMReleasePropList(dict);

	/* fixed-length caller still rejects under/over */
	dict = dict_from_desc("{ HotCornerActions = (A, B, C); }");
	config = wConfigReadWindowMaker(dict);
	CHECK(config->hot_corner_actions.count == 4);
	CHECK(strcmp(config->hot_corner_actions.items[0], "None") == 0);
	wConfigDestroy(config);
	WMReleasePropList(dict);

	dict = dict_from_desc("{ HotCornerActions = (A, B, C, D, E); }");
	config = wConfigReadWindowMaker(dict);
	CHECK(config->hot_corner_actions.count == 4);
	CHECK(strcmp(config->hot_corner_actions.items[0], "None") == 0);
	wConfigDestroy(config);
	WMReleasePropList(dict);
}

static WConfigStringArray make_array(const char *const *items, size_t count)
{
	WConfigStringArray a;

	a.count = count;
	a.items = (char **)(void *)items;
	return a;
}

static void test_titlebar_layout_build(void)
{
	struct case_t {
		const char *name;
		const char *left[8];
		size_t nleft;
		const char *right[8];
		size_t nright;
		WTitlebarSide side[WTB_ROLE_COUNT];
		short order[WTB_ROLE_COUNT];
		int expect_warning;
	};
	static const struct case_t cases[] = {
		{ "default",
		  { "Miniaturize" }, 1, { "Close" }, 1,
		  { [WTB_MINIATURIZE] = WTB_SIDE_LEFT, [WTB_CLOSE] = WTB_SIDE_RIGHT },
		  { [WTB_MINIATURIZE] = 0, [WTB_CLOSE] = 0 },
		  0 },
		{ "swapped",
		  { "Close" }, 1, { "Miniaturize" }, 1,
		  { [WTB_MINIATURIZE] = WTB_SIDE_RIGHT, [WTB_CLOSE] = WTB_SIDE_LEFT },
		  { [WTB_MINIATURIZE] = 0, [WTB_CLOSE] = 0 },
		  0 },
		{ "empty",
		  { NULL }, 0, { NULL }, 0,
		  { 0 },
		  { 0 },
		  0 },
		{ "case-insensitive",
		  { "miniaturize" }, 1, { NULL }, 0,
		  { [WTB_MINIATURIZE] = WTB_SIDE_LEFT },
		  { [WTB_MINIATURIZE] = 0 },
		  0 },
		{ "unknown",
		  { "Foo" }, 1, { NULL }, 0,
		  { 0 },
		  { 0 },
		  1 },
		{ "duplicate-same-array",
		  { "Close", "Close" }, 2, { NULL }, 0,
		  { [WTB_CLOSE] = WTB_SIDE_LEFT },
		  { [WTB_CLOSE] = 0 },
		  1 },
		{ "duplicate-across-arrays",
		  { "Close" }, 1, { "Close" }, 1,
		  { [WTB_CLOSE] = WTB_SIDE_LEFT },
		  { [WTB_CLOSE] = 0 },
		  1 },
		{ "macos-cluster",
		  { "Close", "Miniaturize", "Maximize" }, 3, { NULL }, 0,
		  { [WTB_CLOSE] = WTB_SIDE_LEFT, [WTB_MINIATURIZE] = WTB_SIDE_LEFT,
		    [WTB_MAXIMIZE] = WTB_SIDE_LEFT },
		  { [WTB_CLOSE] = 0, [WTB_MINIATURIZE] = 1, [WTB_MAXIMIZE] = 2 },
		  0 },
		{ "icon-left",
		  { "Icon" }, 1, { "Miniaturize", "Close" }, 2,
		  { [WTB_ICON] = WTB_SIDE_LEFT, [WTB_MINIATURIZE] = WTB_SIDE_RIGHT,
		    [WTB_CLOSE] = WTB_SIDE_RIGHT },
		  { [WTB_ICON] = 0, [WTB_MINIATURIZE] = 0, [WTB_CLOSE] = 1 },
		  0 },
		{ "icon-right",
		  { NULL }, 0, { "Icon", "Close" }, 2,
		  { [WTB_ICON] = WTB_SIDE_RIGHT, [WTB_CLOSE] = WTB_SIDE_RIGHT },
		  { [WTB_ICON] = 0, [WTB_CLOSE] = 1 },
		  0 },
#ifdef XKB_BUTTON_HINT
		{ "modelock-left",
		  { "Miniaturize", "Language" }, 2, { NULL }, 0,
		  { [WTB_MINIATURIZE] = WTB_SIDE_LEFT, [WTB_LANGUAGE] = WTB_SIDE_LEFT },
		  { [WTB_MINIATURIZE] = 0, [WTB_LANGUAGE] = 1 },
		  0 },
		{ "modelock-right",
		  { "Miniaturize" }, 1, { "Language", "Close" }, 2,
		  { [WTB_MINIATURIZE] = WTB_SIDE_LEFT, [WTB_LANGUAGE] = WTB_SIDE_RIGHT,
		    [WTB_CLOSE] = WTB_SIDE_RIGHT },
		  { [WTB_MINIATURIZE] = 0, [WTB_LANGUAGE] = 0, [WTB_CLOSE] = 1 },
		  0 },
#endif
	};
	WTitlebarLayout layout;
	WConfigStringArray left, right;
	size_t i;
	int r, warned;
	WMPropList *pl;
	const char *items[8];
	WConfigStringArray def;
	int n;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		left = make_array(cases[i].left, cases[i].nleft);
		right = make_array(cases[i].right, cases[i].nright);
		warned = wTitlebarLayoutBuild(&layout, &left, &right);
		if (warned != cases[i].expect_warning) {
			fprintf(stderr, "FAIL titlebar_layout case '%s': warned=%d expected=%d\n",
				cases[i].name, warned, cases[i].expect_warning);
			check_failures++;
		}
		for (r = 0; r < WTB_ROLE_COUNT; r++) {
			if (layout.side[r] != cases[i].side[r]) {
				fprintf(stderr,
					"FAIL titlebar_layout case '%s': role %d side=%u expected=%u\n",
					cases[i].name, r, layout.side[r], cases[i].side[r]);
				check_failures++;
			}
			if (layout.side[r] != WTB_SIDE_NONE
			    && layout.order[r] != cases[i].order[r]) {
				fprintf(stderr,
					"FAIL titlebar_layout case '%s': role %d order=%d expected=%d\n",
					cases[i].name, r, layout.order[r], cases[i].order[r]);
				check_failures++;
			}
		}
	}

	/* DEF_TITLEBAR_LEFT parses to the historical default */
	pl = WMCreatePropListFromDescription(DEF_TITLEBAR_LEFT);
	CHECK(pl != NULL && WMIsPLArray(pl));
	n = WMGetPropListItemCount(pl);
	for (r = 0; r < n; r++)
		items[r] = WMGetFromPLString(WMGetFromPLArray(pl, r));
	def = make_array(items, (size_t)n);
	right = make_array(NULL, 0);
	warned = wTitlebarLayoutBuild(&layout, &def, &right);
	CHECK(warned == 0);
	CHECK(layout.side[WTB_MINIATURIZE] == WTB_SIDE_LEFT);
	CHECK(layout.order[WTB_MINIATURIZE] == 0);
#ifdef XKB_BUTTON_HINT
	CHECK(layout.side[WTB_LANGUAGE] == WTB_SIDE_LEFT);
	CHECK(layout.order[WTB_LANGUAGE] == 1);
#endif
	WMReleasePropList(pl);
}

int main(void)
{
	WMPLSetCaseSensitive(false);

	test_schema_integrity();
	test_unchanged_diff();
	test_defaults();
	test_defaults_file_matches_schema();
	test_valid_parse();
	test_static_apply();
	test_invalid_fallback();
	test_runtime_apply();
	test_runtime_apply_phase4();
	test_runtime_apply_phase5();
	test_runtime_apply_phase7();
	test_cursor_hide_live_reload();
	test_wConfigClone();
	test_inner_frame_live_apply();
	test_empty_color_uses_default();
	test_keybinding_regrab_all_screens();
	test_runtime_apply_uses_cached_old_config();
	test_startup_only_frozen_across_reload();
	test_focus_mode_live_apply();
	test_dock_edge_roundtrip();
	test_enable_workspace_pager_live_apply();
	test_switch_panel_icon_size_live_apply();
	test_pixmap_path_live_apply();
	test_schema_requires_restart();
	test_disable_miniwindows_live_apply();
	test_blink_titlebar_when_urgent_live_apply();
	test_icon_size_live_apply();
	test_modifier_key_live_apply();
	test_force_reapply_fonts();
	test_antialiased_text_live_apply();
	test_system_color_refresh_mask();
	test_deleted_key_keeps_current();
	test_string_array_range();
	test_titlebar_layout_build();

	reset_preferences();
	return check_report("check_config");
}
