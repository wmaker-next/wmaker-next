/* wdefaults.c - window specific defaults
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2009-2026 Window Maker Team
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

#include "wconfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <regex.h>
#include <sys/types.h>

#include <xkbcommon/xkbcommon-keysyms.h>

#include <wraster.h>

#include <WINGs/WUtil.h>
#include "WindowMaker.h"
#include "window.h"
#include "appicon.h"
#include "screen.h"
#include "workspace.h"
#include "defaults.h"
#include "desktopfile.h"
#include "config_schema_public.h"
#include "wdefaults.h"
#include "icon.h"
#include "misc.h"

_Static_assert(WATTR_BOOL_COUNT <= 64,
	       "WWindowAttrs bitset is uint64_t, raise the field width");

/*
 * Cache of parsed WMWindowAttributes entries, keyed by the proplist
 * dictionary key string ("instance.class", "instance", "class" or "*").
 * The proplist dictionary remains the source of truth for writes,
 * wWindowAttrsReload() rebuilds this cache from it.
 */
static WMHashTable *attrs_cache;
/* every boolean defaults to No, set covers all bits so merge_attrs()
 * writes every flag when useGlobalDefault is in effect */
static const WWindowAttrs attrs_global_default = {
	.value = 0,
	.set = ~(uint64_t)0,
	.icon = NULL,
	.start_workspace = NULL,
	.start_workspaces = NULL,
	.start_workspace_count = 0,
	.frame_set = 0,
	.frame_border_width = 0,
	.frame_inner_border_width = 0,
	.frame_inner_border_sides = INNER_SIDE_ALL,
	.frame_border_color_spec = NULL,
	.frame_focused_border_color_spec = NULL,
	.frame_selected_border_color_spec = NULL,
	.frame_inner_border_color_spec = NULL,
	.frame_inner_focused_border_color_spec = NULL,
	.start_geometry_spec = NULL,
	.mark_key_spec = NULL
};

/* proplist key atoms used by the write/maintenance helpers */
static WMPropList *AIcon;
static WMPropList *AnyWindow;

/* proplist keys for the DynamicRules array, created once in
 * init_wdefaults() and held for program lifetime */
static WMPropList *ADynamicRules;
static WMPropList *AMatchClass, *AMatchInstance, *AMatchTitle;
static WMPropList *AMatchRole, *AMatchCommand, *AMatchMachine, *AMatchWindowType;
static WMPropList *AAttributes;

typedef enum {
	REGEX_MATCH_CLASS       = 1 << 0,
	REGEX_MATCH_INSTANCE    = 1 << 1,
	REGEX_MATCH_TITLE       = 1 << 2,
	REGEX_MATCH_ROLE        = 1 << 3,
	REGEX_MATCH_COMMAND     = 1 << 4,
	REGEX_MATCH_MACHINE     = 1 << 5,
	REGEX_MATCH_WINDOW_TYPE = 1 << 6,
} regex_match_kind;

typedef struct WDynamicRule {
	unsigned int has_match;        /* bitmask of regex_match_kind */
	regex_t re_class;
	regex_t re_instance;
	regex_t re_title;
	regex_t re_role;
	regex_t re_command;
	regex_t re_machine;
	regex_t re_window_type;
	unsigned int compiled;        /* bitmask of regex_match_kind that compiled */
	WWindowAttrs *attrs;       /* parsed Attributes block, owned */
} WDynamicRule;

static WDynamicRule *dynamic_rules;
static int dynamic_rule_count;
static unsigned int attrs_generation;

#define WATTR_NAME_ROW(K, name, flag) name,
static const char * const attr_bool_names[WATTR_BOOL_COUNT] = {
	WATTR_BOOLS(WATTR_NAME_ROW)
};
#undef WATTR_NAME_ROW

static void init_wdefaults(void)
{
	if (AIcon)
		return;
	AIcon = WMCreatePLString("Icon");
	AnyWindow = WMCreatePLString("*");
	ADynamicRules       = WMCreatePLString("DynamicRules");
	AMatchClass         = WMCreatePLString("MatchClass");
	AMatchInstance      = WMCreatePLString("MatchInstance");
	AMatchTitle         = WMCreatePLString("MatchTitle");
	AMatchRole          = WMCreatePLString("MatchRole");
	AMatchCommand       = WMCreatePLString("MatchCommand");
	AMatchMachine       = WMCreatePLString("MatchMachine");
	AMatchWindowType    = WMCreatePLString("MatchWindowType");
	AAttributes         = WMCreatePLString("Attributes");
}

static char *get_entry_string(WMPropList *entry, const char *key)
{
	WMPropList *value;

	value = WMGetFromPLDictionaryByName(entry, key);
	if (!value)
		return NULL;
	if (!WMIsPLString(value)) {
		wwarning(_("Wrong option format for key \"%s\". Should be %s."),
			 key, "String");
		return NULL;
	}
	return WMGetFromPLString(value);
}

static unsigned int inner_border_sides_parse(const char *spec)
{
	unsigned int sides = 0;
	const char *p = spec ? spec : "";

	while (*p) {
		char buf[16];
		size_t len = 0;

		while (*p && (isspace((unsigned char)*p) || *p == ',' || *p == '(' || *p == ')'))
			p++;
		while (*p && !isspace((unsigned char)*p) && *p != ',' && *p != '(' && *p != ')') {
			if (len < sizeof(buf) - 1)
				buf[len++] = *p;
			p++;
		}
		buf[len] = '\0';
		if (len == 0)
			continue;
		if (strcasecmp(buf, "left") == 0)
			sides |= INNER_SIDE_LEFT;
		else if (strcasecmp(buf, "right") == 0)
			sides |= INNER_SIDE_RIGHT;
		else if (strcasecmp(buf, "top") == 0)
			sides |= INNER_SIDE_TOP;
		else if (strcasecmp(buf, "bottom") == 0)
			sides |= INNER_SIDE_BOTTOM;
		else if (strcasecmp(buf, "all") == 0)
			sides |= INNER_SIDE_ALL;
		else
			wwarning(_("unknown FrameInnerBorderSides token \"%s\""), buf);
	}
	return sides ? sides : INNER_SIDE_ALL;
}

static WWindowAttrs *parse_attrs_entry(WMPropList *entry)
{
	WWindowAttrs *attrs;
	const char *str;
	WMPropList *sws, *sides;
	int i, j, cnt, n;
	unsigned int sides_mask;

	attrs = wmalloc(sizeof(*attrs));

	for (i = 0; i < WATTR_BOOL_COUNT; i++) {
		WMPropList *value;

		value = WMGetFromPLDictionaryByName(entry, attr_bool_names[i]);
		if (!value)
			continue;
		attrs->set |= (uint64_t)1 << i;
		if (WMPLGetBool(value))
			attrs->value |= (uint64_t)1 << i;
	}

	str = get_entry_string(entry, "Icon");
	if (str)
		attrs->icon = wstrdup(str);
	str = get_entry_string(entry, "StartWorkspace");
	if (str)
		attrs->start_workspace = wstrdup(str);
	sws = WMGetFromPLDictionaryByName(entry, "StartWorkspaces");
	if (sws && WMIsPLArray(sws)) {
		int sws_n = WMGetPropListItemCount(sws);

		attrs->start_workspaces = wmalloc(sizeof(char *) * (sws_n + 1));
		cnt = 0;
		for (j = 0; j < sws_n; j++) {
			WMPropList *item = WMGetFromPLArray(sws, j);
			const char *tok;

			if (!item || !WMIsPLString(item))
				continue;
			tok = WMGetFromPLString(item);
			if (!tok || tok[0] == '\0')
				continue;
			attrs->start_workspaces[cnt++] = wstrdup(tok);
		}
		attrs->start_workspaces[cnt] = NULL;
		attrs->start_workspace_count = cnt;
	}
	str = get_entry_string(entry, "MarkKey");
	if (str)
		attrs->mark_key_spec = wstrdup(str);

	str = get_entry_string(entry, "FrameBorderWidth");
	if (str) {
		attrs->frame_border_width = wConfigSchemaParseInt("FrameBorderWidth", str);
		attrs->frame_set |= WATTR_FRAME_BORDER_WIDTH;
	}
	str = get_entry_string(entry, "FrameBorderColor");
	if (str) {
		attrs->frame_border_color_spec = wstrdup(str);
		attrs->frame_set |= WATTR_FRAME_BORDER_COLOR;
	}
	str = get_entry_string(entry, "FrameFocusedBorderColor");
	if (str) {
		attrs->frame_focused_border_color_spec = wstrdup(str);
		attrs->frame_set |= WATTR_FRAME_FOCUSED_BORDER_COLOR;
	}
	str = get_entry_string(entry, "FrameSelectedBorderColor");
	if (str) {
		attrs->frame_selected_border_color_spec = wstrdup(str);
		attrs->frame_set |= WATTR_FRAME_SELECTED_BORDER_COLOR;
	}
	str = get_entry_string(entry, "FrameInnerBorderWidth");
	if (str) {
		attrs->frame_inner_border_width = wConfigSchemaParseInt("FrameInnerBorderWidth", str);
		attrs->frame_set |= WATTR_FRAME_INNER_BORDER_WIDTH;
	}
	sides = WMGetFromPLDictionaryByName(entry, "FrameInnerBorderSides");
	if (sides) {
		if (WMIsPLArray(sides)) {
			n = WMGetPropListItemCount(sides);
			sides_mask = 0;

			for (j = 0; j < n; j++) {
				WMPropList *item = WMGetFromPLArray(sides, j);
				const char *tok;

				if (!item || !WMIsPLString(item))
					continue;
				tok = WMGetFromPLString(item);
				if (strcasecmp(tok, "left") == 0)
					sides_mask |= INNER_SIDE_LEFT;
				else if (strcasecmp(tok, "right") == 0)
					sides_mask |= INNER_SIDE_RIGHT;
				else if (strcasecmp(tok, "top") == 0)
					sides_mask |= INNER_SIDE_TOP;
				else if (strcasecmp(tok, "bottom") == 0)
					sides_mask |= INNER_SIDE_BOTTOM;
				else if (strcasecmp(tok, "all") == 0)
					sides_mask |= INNER_SIDE_ALL;
				else
					wwarning(_("unknown FrameInnerBorderSides token \"%s\""), tok);
			}
			attrs->frame_inner_border_sides = sides_mask ? sides_mask : INNER_SIDE_ALL;
			attrs->frame_set |= WATTR_FRAME_INNER_BORDER_SIDES;
		} else if (WMIsPLString(sides)) {
			attrs->frame_inner_border_sides =
				inner_border_sides_parse(WMGetFromPLString(sides));
			attrs->frame_set |= WATTR_FRAME_INNER_BORDER_SIDES;
		}
	}
	str = get_entry_string(entry, "FrameInnerBorderColor");
	if (str) {
		attrs->frame_inner_border_color_spec = wstrdup(str);
		attrs->frame_set |= WATTR_FRAME_INNER_BORDER_COLOR;
	}
	str = get_entry_string(entry, "FrameInnerFocusedBorderColor");
	if (str) {
		attrs->frame_inner_focused_border_color_spec = wstrdup(str);
		attrs->frame_set |= WATTR_FRAME_INNER_FOCUSED_BORDER_COLOR;
	}
	str = get_entry_string(entry, "StartGeometry");
	if (str)
		attrs->start_geometry_spec = wstrdup(str);

	return attrs;
}

static void free_attrs_entry(WWindowAttrs *attrs)
{
	if (!attrs)
		return;
	if (attrs->icon)
		wfree(attrs->icon);
	if (attrs->start_workspace)
		wfree(attrs->start_workspace);
	if (attrs->start_workspaces) {
		int j;

		for (j = 0; j < attrs->start_workspace_count; j++)
			wfree(attrs->start_workspaces[j]);
		wfree(attrs->start_workspaces);
	}
	if (attrs->mark_key_spec)
		wfree(attrs->mark_key_spec);
	if (attrs->frame_border_color_spec)
		wfree(attrs->frame_border_color_spec);
	if (attrs->frame_focused_border_color_spec)
		wfree(attrs->frame_focused_border_color_spec);
	if (attrs->frame_selected_border_color_spec)
		wfree(attrs->frame_selected_border_color_spec);
	if (attrs->frame_inner_border_color_spec)
		wfree(attrs->frame_inner_border_color_spec);
	if (attrs->frame_inner_focused_border_color_spec)
		wfree(attrs->frame_inner_focused_border_color_spec);
	if (attrs->start_geometry_spec)
		wfree(attrs->start_geometry_spec);
	wfree(attrs);
}

static void free_attrs_cache(void)
{
	WMHashEnumerator e;
	WWindowAttrs *attrs;

	if (!attrs_cache)
		return;
	e = WMEnumerateHashTable(attrs_cache);
	while ((attrs = WMNextHashEnumeratorItem(&e)) != NULL)
		free_attrs_entry(attrs);
	WMFreeHashTable(attrs_cache);
	attrs_cache = NULL;
}

/* Backslash-escape every POSIX ERE metacharacter in s so the result
 * can be spliced into a regex pattern as a literal substring. */
static char *re_escape(const char *s)
{
	static const char specials[] = ".[]\\()*+?^$|{}";
	size_t n, i, j;
	char *out;

	n = strlen(s);
	/* upper bound: every char escaped plus terminator */
	out = wmalloc(n * 2 + 1);
	for (i = 0, j = 0; i < n; i++) {
		if (strchr(specials, s[i]))
			out[j++] = '\\';
		out[j++] = s[i];
	}
	out[j] = '\0';
	return out;
}

/* Splice environment-variable values into $_NAME sequences in pattern.
 * NAME is [A-Za-z_][A-Za-z0-9_]*, auto-escaped for POSIX ERE. Unset vars
 * substitute to empty. $_ with no name byte and $ not followed by _ are
 * passed through verbatim. Substitution does not happen inside bracket
 * expressions [...] where backslash is not honoured. */
static char *subst_env_in_pattern(const char *pattern)
{
	size_t cap, len, i;
	char *out;
	bool in_bracket = false;
	bool bracket_first = false;
	char class_term = '\0';

	if (!pattern)
		return NULL;

	cap = strlen(pattern) + 1;
	out = wmalloc(cap);
	len = 0;

	for (i = 0; pattern[i] != '\0'; i++) {
		char c = pattern[i];

		if (in_bracket) {
			if (class_term) {
				if (c == class_term && pattern[i + 1] == ']') {
					out[len++] = c;
					out[len++] = ']';
					i += 1;
					if (len + 2 >= cap) {
						cap = (len + 2) * 2;
						out = wrealloc(out, cap);
					}
					class_term = '\0';
					bracket_first = false;
					continue;
				}
				out[len++] = c;
				if (len + 1 >= cap) {
					cap *= 2;
					out = wrealloc(out, cap);
				}
				continue;
			}
			if (c == '[' && (pattern[i + 1] == ':' ||
					 pattern[i + 1] == '.' ||
					 pattern[i + 1] == '=')) {
				class_term = pattern[i + 1];
				bracket_first = false;
				out[len++] = c;
				if (len + 1 >= cap) {
					cap *= 2;
					out = wrealloc(out, cap);
				}
				continue;
			}
			if (c == ']' && !bracket_first) {
				in_bracket = false;
				out[len++] = c;
				if (len + 1 >= cap) {
					cap *= 2;
					out = wrealloc(out, cap);
				}
				continue;
			}
			if (bracket_first && c == '^') {
				out[len++] = c;
				if (len + 1 >= cap) {
					cap *= 2;
					out = wrealloc(out, cap);
				}
				continue;
			}
			bracket_first = false;
			out[len++] = c;
			if (len + 1 >= cap) {
				cap *= 2;
				out = wrealloc(out, cap);
			}
			continue;
		}

		if (c == '[') {
			in_bracket = true;
			bracket_first = true;
			class_term = '\0';
			out[len++] = c;
			if (len + 1 >= cap) {
				cap *= 2;
				out = wrealloc(out, cap);
			}
			continue;
		}

		if (c == '$' && pattern[i + 1] == '_') {
			char name[256];
			size_t ni = 0;
			size_t j = i + 2;
			const char *val;

			while (pattern[j] != '\0' && ni < sizeof(name) - 1 &&
			       (isalnum((unsigned char)pattern[j]) || pattern[j] == '_'))
				name[ni++] = pattern[j++];
			name[ni] = '\0';

			if (ni == 0) {
				/* $_ with no name byte: literal $_, no getenv() */
				out[len++] = '$';
				out[len++] = '_';
				if (len + 1 >= cap) {
					cap = (len + 1) * 2;
					out = wrealloc(out, cap);
				}
				i += 1; /* consumed the '_' */
				continue;
			}

			val = getenv(name);
			if (val && val[0]) {
				char *esc = re_escape(val);
				size_t el = strlen(esc);

				while (len + el + 1 >= cap) {
					cap *= 2;
					out = wrealloc(out, cap);
				}
				memcpy(out + len, esc, el);
				len += el;
				wfree(esc);
			}
			i = j - 1; /* skip the consumed name bytes */
			continue;
		}

		out[len++] = c;
		if (len + 1 >= cap) {
			cap *= 2;
			out = wrealloc(out, cap);
		}
	}
	out[len] = '\0';
	return out;
}

static int compile_one(regex_t *re, const char *pattern)
{
	char *subst;
	int ok;

	subst = subst_env_in_pattern(pattern);
	ok = regcomp(re, subst ? subst : "", REG_EXTENDED | REG_NOSUB) == 0;
	wfree(subst);
	return ok;
}

/* Compile all present Match* predicates of one rule. Returns True on
 * success, False (with a wwarning per bad pattern) if any compile failed. */
static bool compile_rule_predicates(WDynamicRule *r, WMPropList *rule)
{
	struct {
		WMPropList *key;
		regex_t *re;
		unsigned int flag;
		const char *name;
	} p[7];
	int k;
	bool bad = false;

	p[0].key = AMatchClass;       p[0].re = &r->re_class;       p[0].flag = REGEX_MATCH_CLASS;       p[0].name = "Class";
	p[1].key = AMatchInstance;    p[1].re = &r->re_instance;    p[1].flag = REGEX_MATCH_INSTANCE;   p[1].name = "Instance";
	p[2].key = AMatchTitle;       p[2].re = &r->re_title;       p[2].flag = REGEX_MATCH_TITLE;      p[2].name = "Title";
	p[3].key = AMatchRole;        p[3].re = &r->re_role;         p[3].flag = REGEX_MATCH_ROLE;       p[3].name = "Role";
	p[4].key = AMatchCommand;     p[4].re = &r->re_command;     p[4].flag = REGEX_MATCH_COMMAND;    p[4].name = "Command";
	p[5].key = AMatchMachine;     p[5].re = &r->re_machine;     p[5].flag = REGEX_MATCH_MACHINE;   p[5].name = "Machine";
	p[6].key = AMatchWindowType;  p[6].re = &r->re_window_type;  p[6].flag = REGEX_MATCH_WINDOW_TYPE; p[6].name = "WindowType";

	for (k = 0; k < 7 && !bad; k++) {
		WMPropList *m = WMGetFromPLDictionary(rule, p[k].key);

		if (m && WMIsPLString(m)) {
			if (compile_one(p[k].re, WMGetFromPLString(m))) {
				r->has_match |= p[k].flag;
				r->compiled  |= p[k].flag;
			} else {
				wwarning("DynamicRules: bad Match%s pattern", p[k].name);
				bad = true;
			}
		} else if (m) {
			wwarning("DynamicRules: Match%s is not a string", p[k].name);
			bad = true;
		}
	}
	return !bad;
}

/* regfree() every compiled regex of a rule. The compiled bitmask gates
 * each call since regfree() on an uncompiled regex_t is undefined. */
static void regfree_rule(WDynamicRule *r)
{
	if (r->compiled & REGEX_MATCH_CLASS)
		regfree(&r->re_class);
	if (r->compiled & REGEX_MATCH_INSTANCE)
		regfree(&r->re_instance);
	if (r->compiled & REGEX_MATCH_TITLE)
		regfree(&r->re_title);
	if (r->compiled & REGEX_MATCH_ROLE)
		regfree(&r->re_role);
	if (r->compiled & REGEX_MATCH_COMMAND)
		regfree(&r->re_command);
	if (r->compiled & REGEX_MATCH_MACHINE)
		regfree(&r->re_machine);
	if (r->compiled & REGEX_MATCH_WINDOW_TYPE)
		regfree(&r->re_window_type);
	r->compiled = 0;
	r->has_match = 0;
}

static void free_dynamic_rules(void)
{
	int i;

	if (!dynamic_rules)
		return;
	for (i = 0; i < dynamic_rule_count; i++) {
		WDynamicRule *r = &dynamic_rules[i];

		regfree_rule(r);
		free_attrs_entry(r->attrs);
	}
	wfree(dynamic_rules);
	dynamic_rules = NULL;
	dynamic_rule_count = 0;
}

bool wDefaultHasDynamicRules(void)
{
	return dynamic_rule_count > 0;
}

unsigned int wDefaultAttrsGeneration(void)
{
	return attrs_generation;
}

void wWindowAttrsReload(void)
{
	WMPropList *dict, *keys, *rules_array;
	int i, count, rcount, j;

	attrs_generation++;

	/* icon lookups must re-decode after the reload */
	RReleaseCache();

	init_wdefaults();
	free_attrs_cache();
	free_dynamic_rules();
	attrs_cache = WMCreateHashTable(WMStringHashCallbacks);

	if (!w_global.domain.window_attr)
		return;
	dict = w_global.domain.window_attr->dictionary;
	if (!dict || !WMIsPLDictionary(dict))
		return;

	/* both the outer instance.class key and the inner attribute keys
	 * are matched case-sensitively, same as the original per-call
	 * proplist walk */
	WMPLSetCaseSensitive(true);

	keys = WMGetPLDictionaryKeys(dict);
	count = keys ? WMGetPropListItemCount(keys) : 0;
	for (i = 0; i < count; i++) {
		WMPropList *plkey = WMGetFromPLArray(keys, i);
		WMPropList *entry;
		const char *key;

		if (!plkey || !WMIsPLString(plkey))
			continue;
		key = WMGetFromPLString(plkey);
		entry = WMGetFromPLDictionary(dict, plkey);
		if (!entry || !WMIsPLDictionary(entry))
			continue;
		WMHashInsert(attrs_cache, key, parse_attrs_entry(entry));
	}
	if (keys)
		WMReleasePropList(keys);

	/* DynamicRules array: compile each Match* regex once at reload time
	 * and parse its Attributes block. The first matching rule (in array
	 * order) is layered on top of the four-level precedence stack. */
	rules_array = WMGetFromPLDictionary(dict, ADynamicRules);
	if (rules_array && WMIsPLArray(rules_array)) {
		rcount = WMGetPropListItemCount(rules_array);
		/* wmalloc() zero-initialises, so the WDynamicRule
		 * fields start at zero/NULL */
		if (rcount > 0)
			dynamic_rules = wmalloc(sizeof(WDynamicRule) * rcount);
		for (j = 0; j < rcount; j++) {
			WMPropList *rule = WMGetFromPLArray(rules_array, j);
			WMPropList *attrs_pl;
			WDynamicRule *r = &dynamic_rules[dynamic_rule_count];

			if (!rule || !WMIsPLDictionary(rule))
				continue;

			if (!compile_rule_predicates(r, rule)) {
				/* regfree the partial compiles of this slot
				 * so free_dynamic_rules() does not see a
				 * stale compiled bit on an abandoned slot;
				 * the slot is left unused */
				regfree_rule(r);
				continue;
			}

			/* a rule with no Match* key would match every
			 * window */
			if (!r->has_match) {
				regfree_rule(r);
				continue;
			}

			attrs_pl = WMGetFromPLDictionary(rule, AAttributes);
			if (!attrs_pl || !WMIsPLDictionary(attrs_pl)) {
				regfree_rule(r);
				continue;
			}
			r->attrs = parse_attrs_entry(attrs_pl);

			dynamic_rule_count++;
		}
	}

	WMPLSetCaseSensitive(false);
}

static const WWindowAttrs *attrs_lookup(const char *key)
{
	if (!attrs_cache)
		wWindowAttrsReload();
	if (!key)
		return NULL;
	return WMHashGet(attrs_cache, key);
}

/* Look up the parsed entries for instance.class/instance/class/"*" in
 * precedence order. Returns the number of levels filled (up to 4),
 * level[0] is the highest-precedence match. */
static int attrs_lookup_levels(const char *instance, const char *class,
			       bool useGlobalDefault, const WWindowAttrs *level[4])
{
	int n = 0;

	/* A client may set WM_CLASS with one or both components empty.
	 * An empty string identifies nothing, so skip that lookup level
	 * and fall through to the less specific ones, the same way an
	 * absent component would. */
	if (instance && !*instance)
		instance = NULL;
	if (class && !*class)
		class = NULL;

	if (instance && class) {
		char *both = StrConcatDot(instance, class);
		level[n] = attrs_lookup(both);
		wfree(both);
		if (level[n])
			n++;
	}
	if (instance) {
		level[n] = attrs_lookup(instance);
		if (level[n])
			n++;
	}
	if (class) {
		level[n] = attrs_lookup(class);
		if (level[n])
			n++;
	}
	if (useGlobalDefault) {
		level[n] = attrs_lookup("*");
		if (level[n])
			n++;
	}
	return n;
}

static void merge_attrs(WWindowAttributes *attr, WWindowAttributes *mask,
			const WWindowAttrs *src)
{
#define WATTR_MERGE_ROW(K, name, flag) \
	if (src->set & ((uint64_t)1 << WATTR_##K)) { \
		attr->flag = (src->value >> WATTR_##K) & 1u; \
		if (mask) mask->flag = 1; \
	}
	WATTR_BOOLS(WATTR_MERGE_ROW)
#undef WATTR_MERGE_ROW
}

static const char *cap_predicate(const char *s, char *buf, size_t bufsize)
{
	size_t len;

	if (!s)
		return NULL;
	len = strnlen(s, bufsize);
	if (len >= bufsize) {
		memcpy(buf, s, bufsize - 1);
		buf[bufsize - 1] = '\0';
		return buf;
	}
	return s;
}

/*
 *----------------------------------------------------------------------
 * attrs_lookup_dynamic / fill_attrs_with_title--
 *  Return the first DynamicRule whose every present Match* predicate
 * matches. Empty (NULL) predicate sources are treated as no match,
 * mirroring attrs_lookup_levels(). window_types is an array of
 * already-resolved _NET_WM_WINDOW_TYPE atom-name strings. The lookup
 * never calls XGetAtomName() itself. A rule with MatchWindowType
 * matches if its regex matches any advertised type name. An empty list
 * never satisfies a MatchWindowType predicate.
 *
 * fill_attrs_with_title runs the four-level merge first, then layers
 * the first matching dynamic rule on top. The dynamic lookup is gated
 * by the useDynamic parameter, so the legacy wDefaultFillAttributes()
 * path (useDynamic=False) skips the regex layer entirely while
 * wDefaultFillAttributesForWindow() (useDynamic=True) runs it.
 * attrs_lookup_dynamic() treats NULL predicate sources as no-match,
 * so a rule with only MatchClass/MatchInstance still applies.
 *----------------------------------------------------------------------
 */

static const WWindowAttrs *attrs_lookup_dynamic(const char *title,
						const char *instance,
						const char *class,
						const char *role,
						const char *command,
						const char *machine,
						const char * const *window_types,
						int window_type_count)
{
	char title_buf[DYNRULE_MAX_TITLE_LEN + 1];
	char instance_buf[DYNRULE_MAX_PREDICATE_LEN + 1];
	char class_buf[DYNRULE_MAX_PREDICATE_LEN + 1];
	char role_buf[DYNRULE_MAX_PREDICATE_LEN + 1];
	char command_buf[DYNRULE_MAX_PREDICATE_LEN + 1];
	char machine_buf[DYNRULE_MAX_PREDICATE_LEN + 1];
	int i, j;

	if (!dynamic_rule_count)
		return NULL;

	/* Empty strings identify nothing, matching attrs_lookup_levels.
	 * Treat them as absent so a rule with MatchClass can never match
	 * a client that set WM_CLASS to the empty string. */
	if (instance && !*instance)
		instance = NULL;
	if (class && !*class)
		class = NULL;

	title = cap_predicate(title, title_buf, sizeof(title_buf));
	instance = cap_predicate(instance, instance_buf, sizeof(instance_buf));
	class = cap_predicate(class, class_buf, sizeof(class_buf));
	role = cap_predicate(role, role_buf, sizeof(role_buf));
	command = cap_predicate(command, command_buf, sizeof(command_buf));
	machine = cap_predicate(machine, machine_buf, sizeof(machine_buf));

	for (i = 0; i < dynamic_rule_count; i++) {
		WDynamicRule *r = &dynamic_rules[i];

		if ((r->has_match & REGEX_MATCH_CLASS) &&
		    (!class || regexec(&r->re_class, class, 0, NULL, 0) != 0))
			continue;
		if ((r->has_match & REGEX_MATCH_INSTANCE) &&
		    (!instance || regexec(&r->re_instance, instance, 0, NULL, 0) != 0))
			continue;
		if ((r->has_match & REGEX_MATCH_TITLE) &&
		    (!title || regexec(&r->re_title, title, 0, NULL, 0) != 0))
			continue;
		if ((r->has_match & REGEX_MATCH_ROLE) &&
		    (!role || regexec(&r->re_role, role, 0, NULL, 0) != 0))
			continue;
		if ((r->has_match & REGEX_MATCH_COMMAND) &&
		    (!command || regexec(&r->re_command, command, 0, NULL, 0) != 0))
			continue;
		if ((r->has_match & REGEX_MATCH_MACHINE) &&
		    (!machine || regexec(&r->re_machine, machine, 0, NULL, 0) != 0))
			continue;
		if (r->has_match & REGEX_MATCH_WINDOW_TYPE) {
			bool matched = false;

			for (j = 0; j < window_type_count; j++) {
				if (window_types[j] &&
				    regexec(&r->re_window_type, window_types[j],
					    0, NULL, 0) == 0) {
					matched = true;
					break;
				}
			}
			if (!matched)
				continue;
		}
		return r->attrs;
	}
	return NULL;
}

static void fill_attrs_with_title(const char *instance, const char *class,
				 const char *title,
				 const char *role, const char *command,
				 const char *machine,
				 const char * const *window_types,
				 int window_type_count,
				 WWindowAttributes *attr, WWindowAttributes *mask,
				 bool useGlobalDefault, bool useDynamic)
{
	const WWindowAttrs *level[4];
	const WWindowAttrs *re;
	int n, i;

	n = attrs_lookup_levels(instance, class, useGlobalDefault, level);

	/* with useGlobalDefault every key falls back to No, so apply the
	 * all-zero/all-set global default first, then layer the parsed
	 * entries on top from lowest precedence to highest */
	if (useGlobalDefault)
		merge_attrs(attr, mask, &attrs_global_default);
	for (i = n - 1; i >= 0; i--)
		merge_attrs(attr, mask, level[i]);

	/* Dynamic rules layer on top, highest precedence. The lookup is
	 * only attempted when useDynamic=True, so the legacy
	 * wDefaultFillAttributes() path skips the regex layer. */
	if (useDynamic) {
		re = attrs_lookup_dynamic(title, instance, class, role, command,
					  machine, window_types,
					  window_type_count);
		if (re)
			merge_attrs(attr, mask, re);
	}
}

void wDefaultFillAttributes(const char *instance, const char *class,
			    WWindowAttributes *attr, WWindowAttributes *mask,
			    bool useGlobalDefault)
{
	fill_attrs_with_title(instance, class, NULL, NULL, NULL, NULL, NULL, 0,
			      attr, mask, useGlobalDefault, false);
}

/*
 *----------------------------------------------------------------------
 * wDefaultFillAttributesForWindow--
 *  Same as wDefaultFillAttributes but pulls the dynamic-rule predicate
 * sources (title, role, command, machine, and _NET_WM_WINDOW_TYPE
 * atom names) directly from WWindow. The atom-name list is read from
 * wwin->wm_type_names (populated once at manage time) so this path
 * stays round-trip-free on the per-title-change re-evaluation path.
 *----------------------------------------------------------------------
 */
void wDefaultFillAttributesForWindow(const WWindow *wwin,
				     WWindowAttributes *attr,
				     WWindowAttributes *mask,
				     bool useGlobalDefault)
{
	const char *title = wwin->wm_title ? wwin->wm_title : NULL;

	fill_attrs_with_title(wwin->wm_instance, wwin->wm_class, title,
			      wwin->wm_role, wwin->wm_command, wwin->wm_machine,
			      (const char * const *)wwin->wm_type_names,
			      wwin->wm_type_count,
			      attr, mask, useGlobalDefault, true);
}

/*
 * Remove a stale Icon entry from the WMWindowAttributes domain so the
 * missing-icon warning is emitted only once and the application falls
 * back to the default icon. The entry is always dropped from the
 * in-memory dictionary, the on-disk user defaults are rewritten only
 * when the stale entry actually came from there, entries inherited from
 * the global system defaults are left on disk and merely suppressed in
 * memory until the domain is next reloaded.
 */
static void drop_stale_icon(const char *instance, const char *class, const char *file_name)
{
	WDDomain *db = w_global.domain.window_attr;
	WMPropList *dict, *attr, *value, *user_dict, *user_attr;
	WMPropList *keys[3];
	WMPropList *match_key = NULL, *match_attr = NULL;
	bool from_user = false;
	bool persist;
	int i, nkeys = 0;

	if (!db || !db->dictionary || !file_name || file_name[0] == '\0')
		return;

	/*
	 * For absolute paths only prune when the file is really gone, do not
	 * drop the entry on transient errors such as EACCES on a network mount
	 * that is briefly unavailable. Relative names are searched through
	 * IconPath by FindImage(), which loses errno, so for those we only
	 * suppress in memory and never rewrite the user file, a transient
	 * search-path failure then self-heals on the next domain reload.
	 */
	if (file_name[0] == '/') {
		if (access(file_name, F_OK) < 0 && errno != ENOENT)
			return;
		persist = true;
	} else {
		persist = false;
	}

	dict = db->dictionary;

	WMPLSetCaseSensitive(true);

	/* same empty-is-absent rule as attrs_lookup_levels() */
	if (instance && !*instance)
		instance = NULL;
	if (class && !*class)
		class = NULL;

	if (instance && class) {
		char *both = StrConcatDot(instance, class);
		keys[nkeys++] = WMCreatePLString(both);
		wfree(both);
	}
	if (instance)
		keys[nkeys++] = WMCreatePLString(instance);
	if (class)
		keys[nkeys++] = WMCreatePLString(class);

	/* Find which per-app entry actually holds the stale Icon value */
	for (i = 0; i < nkeys; i++) {
		attr = WMGetFromPLDictionary(dict, keys[i]);
		if (!attr || !WMIsPLDictionary(attr))
			continue;
		value = WMGetFromPLDictionary(attr, AIcon);
		if (value && WMIsPLString(value) &&
		    strcmp(WMGetFromPLString(value), file_name) == 0) {
			match_key = keys[i];
			match_attr = attr;
			break;
		}
	}

	if (!match_attr)
		goto out;

	/*
	 * Decide whether the stale entry came from the user's own defaults
	 * file or was merged in from the global system defaults, we only
	 * rewrite the user file in the former case.
	 */
	user_dict = db->path ? WMReadPropListFromFile(db->path) : NULL;
	if (user_dict) {
		if (WMIsPLDictionary(user_dict)) {
			user_attr = WMGetFromPLDictionary(user_dict, match_key);
			if (user_attr && WMIsPLDictionary(user_attr) &&
			    WMGetFromPLDictionary(user_attr, AIcon))
				from_user = true;
		}
		WMReleasePropList(user_dict);
	}

	WMRemoveFromPLDictionary(match_attr, AIcon);
	if (WMGetPropListItemCount(match_attr) == 0)
		WMRemoveFromPLDictionary(dict, match_key);

	if (persist && from_user && !wdisp->flags.noupdates)
		UpdateDomainFile(db);

	wWindowAttrsReload();

out:
	for (i = 0; i < nkeys; i++)
		WMReleasePropList(keys[i]);

	WMPLSetCaseSensitive(false);
}

/* Search the icon cache directory for an icon. When file_name is given
 * (a per-app Icon entry from WMWindowAttributes) look it up there. When
 * it is NULL, fall back to the standard <instance>.<class>.xpm naming
 * that wIconStore() uses, so an entry dropped after a stale-IconPath
 * lookup self-heals from the cached file. */
char *find_in_icon_cache(const char *file_name,
			       const char *winstance, const char *wclass)
{
	const char *cache_dir = wGetIconCacheDir();
	char *constructed = NULL;
	char *path;

	if (!cache_dir)
		return NULL;

	if (!file_name) {
		char *base = get_name_for_instance_class(winstance, wclass);

		if (!base)
			return NULL;
		constructed = wstrconcat(base, ".xpm");
		wfree(base);
		file_name = constructed;
	}

	path = FindImage(cache_dir, file_name);
	wfree(constructed);
	return path;
}

/* Get the file name of the image, using instance and class */
char *get_icon_filename(const char *winstance, const char *wclass, const char *command,
			bool default_icon)
{
	char *file_name;
	char *file_path;

	/* Look up the per-app icon file name, using instance and class */
	file_name = wDefaultGetIconFile(winstance, wclass, false);

	/* Check if the file really exists in the disk */
	if (file_name)
		file_path = FindImage(w_global.config->icon_path, file_name);
	else
		file_path = NULL;

	/* wIconStore() writes cached icons to <cache-root>/CachedPixmaps,
	 * which the default IconPath includes via $WMAKER_USER_CACHE. A user
	 * IconPath inherited from a pre-XDG-cache layout may still point at
	 * the old (now unused) location and miss the cache dir entirely, so
	 * search it directly to keep the cache write and the cache lookup
	 * consistent regardless of the configured IconPath. When no per-app
	 * Icon entry exists at all, the cache is also searched by its
	 * standard naming so a dropped entry self-heals. */
	if (!file_path)
		file_path = find_in_icon_cache(file_name, winstance, wclass);

	/* If the specific icon filename is not found, and command is specified,
	 * then include the .app icons and re-do the search. */
	if (!file_path && command) {
		wApplicationExtractDirPackIcon(command, winstance, wclass);
		file_name = wDefaultGetIconFile(winstance, wclass, false);
		if (file_name) {
			file_path = FindImage(w_global.config->icon_path, file_name);
			if (!file_path)
				file_path = find_in_icon_cache(file_name, winstance, wclass);
		}
	}

	/*
	 * Freedesktop hicolor icon-theme fallback for windows that set
	 * neither an _NET_WM_ICON property nor a WMWindowAttributes Icon=
	 * entry. Electron/Chromium apps rely on the desktop environment
	 * resolving their icon from hicolor/apps/<name>.{png,xpm,svg}.
	 * res_class is the canonical icon-name identifier, fall back to
	 * res_instance, then to a lower-cased retry since WM_CLASS casing
	 * is unconstrained while hicolor file names are conventionally
	 * lower-case. This runs only after the user-configured Icon= and
	 * the icon cache misses, so a real asset wins over the generic
	 * default icon below.
	 */
	if (!file_path && wclass) {
		file_path = wXDGFindThemeIcon(wclass, "apps",
					      wIconEffectiveSize());
	}
	if (!file_path && winstance) {
		file_path = wXDGFindThemeIcon(winstance, "apps",
					      wIconEffectiveSize());
	}
	if (!file_path && wclass) {
		char *fold = wstrdup(wclass);
		char *p;

		for (p = fold; *p; p++)
			*p = tolower((unsigned char)*p);
		if (strcmp(fold, wclass) != 0)
			file_path = wXDGFindThemeIcon(fold, "apps",
						      wIconEffectiveSize());
		wfree(fold);
	}

	/*
	 * If a per-app Icon entry exists but the image file is gone, warn once
	 * and drop the stale entry from WMWindowAttributes so we fall back to
	 * the default icon instead of repeating the warning on every lookup.
	 */
	if (file_name && !file_path) {
		wwarning(_("icon \"%s\" doesn't exist, check your config files"), file_name);
		drop_stale_icon(winstance, wclass, file_name);
		/* file_name pointed into the cache entry we just removed */
		file_name = NULL;
	}

	/*
	 * Don't wfree(file_name), it points into the parsed attrs cache and
	 * is owned by it, not allocated for the caller.
	 */

	if (!file_path && default_icon)
		file_path = get_default_image_path();

	return file_path;
}

/* This function returns the image picture for the file_name file */
RImage *get_rimage_from_file(WScreen *scr, const char *file_name, int max_size)
{
	RImage *image = NULL;

	if (!file_name)
		return NULL;

	/* RLoadImage() passes width=height=0 to the loaders, which leaves
	 * SVGs rendering at their intrinsic size (capped at 256) instead of
	 * the wanted icon size. wIconValidateIconSize() below only downscales,
	 * never upscales, so an SVG with a small intrinsic size would stay
	 * tiny. Pass max_size as the render dimensions: only the SVG loader
	 * consults width/height, every raster loader ignores them. */
	image = RLoadImageAtSize(scr->rcontext, file_name, 0,
				 (unsigned int)max_size, (unsigned int)max_size);
	if (!image)
		wwarning(_("error loading image file \"%s\": %s"), file_name,
			 RMessageForError(RErrorCode));

	image = wIconValidateIconSize(image, max_size);

	return image;
}

/* This function returns the default icon's full path
 * If the path for an icon is not found, returns NULL */
char *get_default_image_path(void)
{
	char *path = NULL, *file = NULL;

	/* Get the default icon */
	file = wDefaultGetIconFile(NULL, NULL, true);
	if (file)
		path = FindImage(w_global.config->icon_path, file);

	return path;
}

/* This function creates the RImage using the default icon */
RImage *get_default_image(WScreen *scr)
{
	RImage *image = NULL;
	char *path = NULL;

	/* Get the filename full path */
	path = get_default_image_path();
	if (!path)
		return NULL;

	/* Get the default icon */
	image = get_rimage_from_file(scr, path, w_global.config->icon_size);
	if (!image)
		wwarning(_("could not find default icon \"%s\""), path);

	wfree(path);

	/* Resize the icon to the w_global.config->icon_size size
	 * usually this function will return early, because size is right */
	image = wIconValidateIconSize(image, w_global.config->icon_size);

	return image;
}

RImage *get_icon_image(WScreen *scr, const char *winstance, const char *wclass, int max_size)
{
	char *file_name;
	RImage *image;

	/* Get the file name of the image, using instance and class */
	file_name = get_icon_filename(winstance, wclass, NULL, true);
	image = get_rimage_from_file(scr, file_name, max_size);
	wfree(file_name);

	return image;
}

bool wDefaultGetStartWorkspaces(WScreen *scr, const char *instance,
			       const char *class, uint64_t *out_mask)
{
	const WWindowAttrs *level[4];
	int n, i, j;

	n = attrs_lookup_levels(instance, class, true, level);
	for (i = 0; i < n; i++) {
		const WWindowAttrs *a = level[i];

		if (a->start_workspace_count > 0 && a->start_workspaces) {
			uint64_t mask = 0;

			for (j = 0; j < a->start_workspace_count; j++) {
				int w = wGetWorkspaceNumber(scr, a->start_workspaces[j]);

				if (w >= 0 && w < MAX_WORKSPACES)
					mask |= WS_BIT(w);
			}
			if (mask) {
				*out_mask = mask;
				return true;
			}
		}
	}
	/* fall back to singular StartWorkspace as a 1-bit mask */
	for (i = 0; i < n; i++) {
		const char *ws = level[i]->start_workspace;

		if (ws && ws[0] != '\0') {
			int w = wGetWorkspaceNumber(scr, ws);

			if (w >= 0 && w < MAX_WORKSPACES) {
				*out_mask = WS_BIT(w);
				return true;
			}
		}
	}
	return false;
}

void wDefaultFillFrameBorders(WScreen *scr, const char *instance,
			     const char *class, WWindow *wwin)
{
	const WWindowAttrs *level[4];
	const WWindowAttrs *re = NULL;
	int n, i;
	bool resolved_any = false;

	n = attrs_lookup_levels(instance, class, true, level);
	for (i = 0; i < n; i++) {
		const WWindowAttrs *a = level[i];

		if ((a->frame_set & WATTR_FRAME_BORDER_WIDTH) && !wwin->frame_border.frame_border_width_set) {
			wwin->frame_border.frame_border_width = a->frame_border_width;
			wwin->frame_border.frame_border_width_set = 1;
			resolved_any = true;
		}
		if ((a->frame_set & WATTR_FRAME_INNER_BORDER_WIDTH) && !wwin->frame_border.frame_inner_border_width_set) {
			wwin->frame_border.frame_inner_border_width = a->frame_inner_border_width;
			wwin->frame_border.frame_inner_border_width_set = 1;
			resolved_any = true;
		}
		if ((a->frame_set & WATTR_FRAME_INNER_BORDER_SIDES) && !wwin->frame_border.frame_inner_border_sides_set) {
			wwin->frame_border.frame_inner_border_sides = a->frame_inner_border_sides;
			wwin->frame_border.frame_inner_border_sides_set = 1;
			resolved_any = true;
		}
		if ((a->frame_set & WATTR_FRAME_BORDER_COLOR) && !wwin->frame_border.frame_border_color_set) {
			wwin->frame_border.frame_border_color = (a->frame_border_color_spec && a->frame_border_color_spec[0])
				? WMCreateNamedColor(scr->wmscreen, a->frame_border_color_spec, true) : NULL;
			wwin->frame_border.frame_border_color_set = 1;
			resolved_any = true;
		}
		if ((a->frame_set & WATTR_FRAME_FOCUSED_BORDER_COLOR) && !wwin->frame_border.frame_focused_border_color_set) {
			wwin->frame_border.frame_focused_border_color = (a->frame_focused_border_color_spec && a->frame_focused_border_color_spec[0])
				? WMCreateNamedColor(scr->wmscreen, a->frame_focused_border_color_spec, true) : NULL;
			wwin->frame_border.frame_focused_border_color_set = 1;
			resolved_any = true;
		}
		if ((a->frame_set & WATTR_FRAME_SELECTED_BORDER_COLOR) && !wwin->frame_border.frame_selected_border_color_set) {
			wwin->frame_border.frame_selected_border_color = (a->frame_selected_border_color_spec && a->frame_selected_border_color_spec[0])
				? WMCreateNamedColor(scr->wmscreen, a->frame_selected_border_color_spec, true) : NULL;
			wwin->frame_border.frame_selected_border_color_set = 1;
			resolved_any = true;
		}
		if ((a->frame_set & WATTR_FRAME_INNER_BORDER_COLOR) && !wwin->frame_border.frame_inner_border_color_set) {
			wwin->frame_border.frame_inner_border_color = (a->frame_inner_border_color_spec && a->frame_inner_border_color_spec[0])
				? WMCreateNamedColor(scr->wmscreen, a->frame_inner_border_color_spec, true) : NULL;
			wwin->frame_border.frame_inner_border_color_set = 1;
			resolved_any = true;
		}
		if ((a->frame_set & WATTR_FRAME_INNER_FOCUSED_BORDER_COLOR) && !wwin->frame_border.frame_inner_focused_border_color_set) {
			wwin->frame_border.frame_inner_focused_border_color = (a->frame_inner_focused_border_color_spec && a->frame_inner_focused_border_color_spec[0])
				? WMCreateNamedColor(scr->wmscreen, a->frame_inner_focused_border_color_spec, true) : NULL;
			wwin->frame_border.frame_inner_focused_border_color_set = 1;
			resolved_any = true;
		}
	}

	/* Dynamic rules layer on top, highest precedence. attrs_lookup_dynamic()
	 * already treats NULL predicate sources as no-match, so a rule with only
	 * MatchClass/MatchInstance still applies here. Override any field the
	 * matched rule supplies, releasing the prior value first. */
	re = attrs_lookup_dynamic(wwin->wm_title, instance, class, wwin->wm_role,
				 wwin->wm_command, wwin->wm_machine,
				 (const char * const *)wwin->wm_type_names,
				 wwin->wm_type_count);
	if (re) {
		if (re->frame_set & WATTR_FRAME_BORDER_WIDTH) {
			wwin->frame_border.frame_border_width = re->frame_border_width;
			wwin->frame_border.frame_border_width_set = 1;
			resolved_any = true;
		}
		if (re->frame_set & WATTR_FRAME_INNER_BORDER_WIDTH) {
			wwin->frame_border.frame_inner_border_width = re->frame_inner_border_width;
			wwin->frame_border.frame_inner_border_width_set = 1;
			resolved_any = true;
		}
		if (re->frame_set & WATTR_FRAME_INNER_BORDER_SIDES) {
			wwin->frame_border.frame_inner_border_sides = re->frame_inner_border_sides;
			wwin->frame_border.frame_inner_border_sides_set = 1;
			resolved_any = true;
		}
#define OVERLAY_COLOR(spec_field, mask_bit, target_field, set_field) \
	do { \
		if (re->frame_set & (mask_bit)) { \
			if (wwin->frame_border.target_field) \
				WMReleaseColor(wwin->frame_border.target_field); \
			wwin->frame_border.target_field = (re->spec_field && re->spec_field[0]) \
				? WMCreateNamedColor(scr->wmscreen, re->spec_field, true) : NULL; \
			wwin->frame_border.set_field = 1; \
			resolved_any = true; \
		} \
	} while (0)
		OVERLAY_COLOR(frame_border_color_spec, WATTR_FRAME_BORDER_COLOR,
			      frame_border_color, frame_border_color_set);
		OVERLAY_COLOR(frame_focused_border_color_spec, WATTR_FRAME_FOCUSED_BORDER_COLOR,
			      frame_focused_border_color, frame_focused_border_color_set);
		OVERLAY_COLOR(frame_selected_border_color_spec, WATTR_FRAME_SELECTED_BORDER_COLOR,
			      frame_selected_border_color, frame_selected_border_color_set);
		OVERLAY_COLOR(frame_inner_border_color_spec, WATTR_FRAME_INNER_BORDER_COLOR,
			      frame_inner_border_color, frame_inner_border_color_set);
		OVERLAY_COLOR(frame_inner_focused_border_color_spec, WATTR_FRAME_INNER_FOCUSED_BORDER_COLOR,
			      frame_inner_focused_border_color, frame_inner_focused_border_color_set);
#undef OVERLAY_COLOR
	}

	(void) resolved_any;
}

const char *wDefaultGetStartGeometry(const char *instance, const char *class)
{
	const WWindowAttrs *level[4];
	int n, i;

	n = attrs_lookup_levels(instance, class, true, level);
	for (i = 0; i < n; i++) {
		const char *spec = level[i]->start_geometry_spec;

		if (spec && spec[0] != '\0')
			return spec;
	}
	return NULL;
}

const char *wDefaultGetMarkKeyForWindow(const WWindow *wwin)
{
	const char *result = NULL;
	const char *title;
	const WWindowAttrs *re;
	const WWindowAttrs *level[4];
	int n, i;

	n = attrs_lookup_levels(wwin->wm_instance, wwin->wm_class, true, level);
	for (i = 0; i < n; i++) {
		if (level[i]->mark_key_spec != NULL) {
			result = level[i]->mark_key_spec;
			break;
		}
	}

	title = wwin->wm_title ? wwin->wm_title : NULL;
	re = attrs_lookup_dynamic(title, wwin->wm_instance, wwin->wm_class,
				  wwin->wm_role, wwin->wm_command, wwin->wm_machine,
				  (const char * const *)wwin->wm_type_names,
				  wwin->wm_type_count);
	if (re && re->mark_key_spec != NULL)
		result = re->mark_key_spec;

	return result;
}

/* Get the name of the Icon File. If default_icon is True, then, default value included.
 * The returned pointer is owned by the attrs cache, do not free it and do
 * not keep it across a domain reload or a wDefaultChangeIcon() call. */
char *wDefaultGetIconFile(const char *instance, const char *class, bool default_icon)
{
	const WWindowAttrs *level[4];
	int n, i;

	n = attrs_lookup_levels(instance, class, default_icon, level);
	for (i = 0; i < n; i++)
		if (level[i]->icon)
			return level[i]->icon;
	return NULL;
}

void wDefaultChangeIcon(const char *instance, const char *class, const char *file)
{
	WDDomain *db = w_global.domain.window_attr;
	WMPropList *icon_value = NULL, *value, *attr, *key, *def_win, *def_icon = NULL;
	WMPropList *dict = db->dictionary;
	int same = 0;

	init_wdefaults();

	if (!dict) {
		dict = WMCreatePLDictionary(NULL, NULL);
		if (dict)
			db->dictionary = dict;
		else
			return;
	}

	WMPLSetCaseSensitive(true);

	/* same empty-is-absent rule as attrs_lookup_levels() */
	if (instance && !*instance)
		instance = NULL;
	if (class && !*class)
		class = NULL;

	if (instance && class) {
		char *buffer;

		buffer = StrConcatDot(instance, class);
		key = WMCreatePLString(buffer);
		wfree(buffer);
	} else if (instance) {
		key = WMCreatePLString(instance);
	} else if (class) {
		key = WMCreatePLString(class);
	} else {
		key = WMRetainPropList(AnyWindow);
	}

	if (file) {
		value = WMCreatePLString(file);
		icon_value = WMCreatePLDictionary(AIcon, value, NULL);
		WMReleasePropList(value);

		def_win = WMGetFromPLDictionary(dict, AnyWindow);
		if (def_win != NULL)
			def_icon = WMGetFromPLDictionary(def_win, AIcon);

		if (def_icon && !strcmp(WMGetFromPLString(def_icon), file))
			same = 1;
	}

	attr = WMGetFromPLDictionary(dict, key);
	if (attr != NULL) {
		if (WMIsPLDictionary(attr)) {
			if (icon_value != NULL && !same)
				WMMergePLDictionaries(attr, icon_value, false);
			else
				WMRemoveFromPLDictionary(attr, AIcon);
		}
	} else if (icon_value != NULL && !same) {
		WMPutInPLDictionary(dict, key, icon_value);
	}

	if (!wdisp->flags.noupdates)
		UpdateDomainFile(db);

	WMReleasePropList(key);
	if (icon_value)
		WMReleasePropList(icon_value);

	WMPLSetCaseSensitive(false);

	wWindowAttrsReload();
}

void wDefaultPurgeInfo(const char *instance, const char *class)
{
	WMPropList *value, *key, *dict;
	char *buffer;

	init_wdefaults();

	WMPLSetCaseSensitive(true);

	buffer = StrConcatDot(instance, class);
	key = WMCreatePLString(buffer);

	dict = WMGetFromPLDictionary(w_global.domain.window_attr->dictionary, key);

	if (dict) {
		value = WMGetFromPLDictionary(dict, AIcon);
		if (value) {
			WMRemoveFromPLDictionary(dict, AIcon);
		}
		WMRemoveFromPLDictionary(w_global.domain.window_attr->dictionary, key);
		UpdateDomainFile(w_global.domain.window_attr);
		wWindowAttrsReload();
	}

	wfree(buffer);
	WMReleasePropList(key);
	WMPLSetCaseSensitive(false);
}
