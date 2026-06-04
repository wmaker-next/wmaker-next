/*
 * Minimal view onto the typed WindowMaker defaults schema for consumers
 * outside src/ (today, WPrefs.app). Exposes only the per-key enum string
 * table, default value and integer bounds, looked up by proplist key
 * name, so callers do not need WConfigEntry, WConfigKey or wconfigmodel.h.
 *
 * The full internal API lives in config-schema.h.
 */

#ifndef WMCONFIG_SCHEMA_PUBLIC_H_
#define WMCONFIG_SCHEMA_PUBLIC_H_

typedef struct WConfigEnum {
	const char *string;
	int value;
	char is_alias;
} WConfigEnum;

/* NULL-terminated table of accepted strings for an enum-typed key,
 * or NULL if the key is unknown or not enum-typed. */
const WConfigEnum *wConfigSchemaEnumTable(const char *key);

/* Default value string from the schema, or NULL if the key is unknown. */
const char *wConfigSchemaDefault(const char *key);

/* Clamp v to the schema's [lo, hi] for an integer-typed key,
 * returns v unchanged if the key is unknown. */
int wConfigSchemaClampInt(const char *key, int v);

#endif /* WMCONFIG_SCHEMA_PUBLIC_H_ */
