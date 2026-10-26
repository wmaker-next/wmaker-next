/*
 * Typed Window Maker defaults schema.
 */

#ifndef WMCONFIG_SCHEMA_H_
#define WMCONFIG_SCHEMA_H_

#include <stddef.h>
#include <limits.h>

#include "wconfigmodel.h"
#include "config_schema_public.h"

typedef enum {
	WCFG_BOOL,
	WCFG_INT,
	WCFG_UINT,
	WCFG_ENUM,
	WCFG_STRING,
	WCFG_PATH_LIST,
	WCFG_COORD,
	WCFG_STRING_ARRAY,
	WCFG_SWITCH_PANEL_IMAGES,
	WCFG_KEY_BINDING,
	WCFG_TEXTURE_SPEC,
	WCFG_FONT_SPEC,
	WCFG_COLOR_SPEC,
	WCFG_CURSOR_SPEC,
	WCFG_BACKGROUND_SPEC
} WConfigType;

typedef struct WConfigEntry {
	const char *key;
	WConfigKey config_key;
	WConfigType type;
	const char *default_value;
	size_t offset;
	size_t size;
	const WConfigEnum *enum_values;
	/* Numeric bounds for scalar types; target/count metadata for nonnumeric parsers. */
	int min_value;
	int max_value;
	unsigned int flags;
	unsigned int refresh_mask;
} WConfigEntry;

enum {
	WCFG_STARTUP_ONLY = 1 << 0,
	WCFG_RUNTIME_RELOAD = 1 << 1
};

const WConfigEntry *wConfigSchema(void);
size_t wConfigSchemaCount(void);
const WConfigEntry *wConfigEntryForKey(const char *key);

struct WTitlebarLayout;
int wTitlebarLayoutBuild(struct WTitlebarLayout *out,
			 const WConfigStringArray *left,
			 const WConfigStringArray *right);

#endif
