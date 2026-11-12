/*
 * wmmenugen - Window Maker PropList menu generator
 *
 * Desktop Entry Specification parser functions
 *
 * Copyright (c) 2010. Tamas Tevesz <ice@extreme.hu>
 * Copyright (c) 2014-2026 Window Maker Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/*
 * http://standards.freedesktop.org/desktop-entry-spec/desktop-entry-spec-1.1.html
 * http://standards.freedesktop.org/menu-spec/menu-spec-1.1.html
 *
 * We will only deal with Type == "Application" entries in [Desktop Entry]
 * groups. `TryExec', when present, only checks that the application is
 * available. The menu command comes from `Exec', with field codes stripped
 * and each argument kept as a separate shell word.
 *
 * Only the (first, though there should not be more than one) `Main Category'
 * is used to place the entry in a submenu.
 *
 * Basic validation of the .desktop file is done.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <ftw.h>
#if DEBUG
#include <errno.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "wmmenugen.h"

/* LocaleString match levels */
enum {
	MATCH_DEFAULT,
	MATCH_LANG,
	MATCH_LANG_MODIFIER,
	MATCH_LANG_COUNTRY,
	MATCH_LANG_COUNTRY_MODIFIER
};

typedef struct {
	char	*Name;		/* Name */				/* localestring */
	int	 MatchLevel;	/* LocaleString match type */		/* int */
	char	*TryExec;	/* TryExec */				/* string */
	char	*Exec;		/* Exec */				/* string */
	char	*Path;		/* Path */				/* string */
	int	 Flags;		/* Flags */
	char	*Category;	/* Categories (first item only) */	/* string */
} XDGMenuEntry;

static void  getKey(char **target, const char *line);
static void  getStringValue(char **target, const char *line);
static void  getLocalizedStringValue(char **target, const char *line, int *match_level);
static bool getBooleanValue(const char *line);
static void  getMenuHierarchyFor(char **xdgmenuspec);
static bool compare_matchlevel(int *current_level, const char *found_locale);
static bool  xdg_to_wm(XDGMenuEntry *xdg, WMMenuEntry *wmentry);
static char *parse_xdg_exec(const char *exec);
static bool  try_exec_available(const char *file);
static void  init_xdg_storage(XDGMenuEntry *xdg);
static void  init_wm_storage(WMMenuEntry *wm);


void parse_xdg(const char *file, cb_add_menu_entry *addWMMenuEntryCallback)
{
	FILE *fp;
	char *buf, *p, *tmp = NULL, *key;
	WMMenuEntry *wm;
	XDGMenuEntry *xdg;
	int InGroup, rc;

	fp = fopen(file, "r");
	if (!fp) {
#if DEBUG
		fprintf(stderr, "Error opening file %s: %s\n", file, strerror(errno));
#endif
		return;
	}

	xdg = (XDGMenuEntry *)wmalloc(sizeof(XDGMenuEntry));
	wm = (WMMenuEntry *)wmalloc(sizeof(WMMenuEntry));
	InGroup = 0;

	while ((rc = wmmenu_read_line(fp, &buf)) != 0) {
		if (rc < 0)
			continue;

		p = buf;

		/* skip whitespaces */
		while (isspace((unsigned char)*p))
			p++;
		/* skip comments, empty lines */
		if (*p == '\0' || *p == '#') {
			wfree(buf);
			continue;
		}

		if (strcmp(p, "[Desktop Entry]") == 0) {
			/* if currently processing a group, we've just hit the
			 * end of its definition, try processing it
			 */
			if (InGroup && xdg_to_wm(xdg, wm)) {
				(*addWMMenuEntryCallback)(wm);
			}
			init_xdg_storage(xdg);
			init_wm_storage(wm);
			InGroup = 1;
			/* start processing group */
			wfree(buf);
			continue;
		} else if (p[0] == '[') {
			/* If we find a new group and the previous group was the main one,
			 * we stop all further processing
			 */
			if (InGroup) {
				wfree(buf);
				break;
			}
		}

		if (!InGroup) {
			wfree(buf);
			continue;
		}

		getKey(&key, p);
		if (key == NULL) { /* not `key' = `value' */
			wfree(buf);
			continue;
		}

		if (strcmp(key, "Type") == 0) {
			getStringValue(&tmp, p);
			if (strcmp(tmp, "Application") != 0)
				InGroup = 0;	/* if not application, skip current group */
			wfree(tmp);
			tmp = NULL;
		} else if (strcmp(key, "Name") == 0) {
			getLocalizedStringValue(&xdg->Name, p, &xdg->MatchLevel);
		} else if (strcmp(key, "NoDisplay") == 0) {
			if (getBooleanValue(p))	/* if nodisplay, skip current group */
				InGroup = 0;
		} else if (strcmp(key, "Hidden") == 0) {
			if (getBooleanValue(p))
				InGroup = 0;	/* if hidden, skip current group */
		} else if (strcmp(key, "TryExec") == 0) {
			getStringValue(&xdg->TryExec, p);
		} else if (strcmp(key, "Exec") == 0) {
			getStringValue(&xdg->Exec, p);
		} else if (strcmp(key, "Path") == 0) {
			getStringValue(&xdg->Path, p);
		} else if (strcmp(key, "Terminal") == 0) {
			if (getBooleanValue(p))
				xdg->Flags |= F_TERMINAL;
		} else if (strcmp(key, "Categories") == 0) {
			getStringValue(&xdg->Category, p);
			getMenuHierarchyFor(&xdg->Category);
		}

		if (xdg->Category == NULL)
			xdg->Category = wstrdup(_("Other"));

		wfree(key);
		key = NULL;
		wfree(buf);
	}

	fclose(fp);

	/* at the end of the file, might as well try to menuize what we have
	 * unless there was no group at all or it was marked as hidden
	 */
	if (InGroup && xdg_to_wm(xdg, wm))
		(*addWMMenuEntryCallback)(wm);

	init_xdg_storage(xdg);
	init_wm_storage(wm);
	wfree(xdg);
	wfree(wm);
}


/* coerce an xdg entry type into a wm entry type
 */
static bool try_exec_available(const char *file)
{
	if (!file || !*file)
		return false;
	/* the unescaped value names one executable, escaped spaces are
	 * part of the file name and must not split it */
	if (file[0] == '/')
		return access(file, X_OK) == 0;
	return fileInPathExact(file);
}

static bool xdg_to_wm(XDGMenuEntry *xdg, WMMenuEntry *wm)
{
	char *p;

	/* Exec is mandatory, TryExec only checks availability */
	if (!xdg->Exec)
		return false;
	if (xdg->TryExec && !try_exec_available(xdg->TryExec))
		return false;

	/* if there's no Name, use the first word of Exec */
	if (!xdg->Name) {
		xdg->Name = wstrdup(xdg->Exec);

		p = strchr(xdg->Name, ' ');
		if (p)
			*p = '\0';
	}
	wm->Name = xdg->Name;

	wm->CmdLine = parse_xdg_exec(xdg->Exec);
	if (!wm->CmdLine)
		return false;

	wm->Path = xdg->Path;
	wm->SubMenu = xdg->Category;
	wm->Flags = xdg->Flags | F_FREE_CMD_LINE;

	return true;
}

static bool appendArgChar(char **buf, size_t *len, size_t *cap, char c)
{
	char *tmp;

	if (*len + 1 >= *cap) {
		size_t newcap = *cap ? *cap * 2 : 32;

		tmp = wrealloc(*buf, newcap);
		if (!tmp)
			return false;
		*buf = tmp;
		*cap = newcap;
	}
	(*buf)[(*len)++] = c;
	(*buf)[*len] = '\0';
	return true;
}

static char *join_argv(char **argv, int argc)
{
	char *result, *p, *quoted;
	size_t total = 0;
	int i;

	for (i = 0; i < argc; i++) {
		quoted = wshellquote(argv[i]);
		if (!quoted)
			return NULL;
		total += strlen(quoted) + (i ? 1 : 0);
		wfree(quoted);
	}

	result = wmalloc(total + 1);
	p = result;
	for (i = 0; i < argc; i++) {
		size_t len;

		quoted = wshellquote(argv[i]);
		if (!quoted) {
			wfree(result);
			return NULL;
		}
		len = strlen(quoted);
		if (i)
			*p++ = ' ';
		memcpy(p, quoted, len);
		p += len;
		wfree(quoted);
	}
	*p = '\0';
	return result;
}

static char *parse_xdg_exec(const char *exec)
{
	char **argv = NULL;
	int argc = 0, cap = 0;
	char *arg = NULL, *result = NULL;
	size_t arglen = 0, argcap = 0;
	const char *src = exec;
	bool quoted, saw_quote;

	for (;;) {
		while (isspace((unsigned char)*src))
			src++;
		if (*src == '\0')
			break;

		arg = NULL;
		arglen = 0;
		argcap = 0;
		quoted = false;
		saw_quote = false;

		while (*src && (quoted || !isspace((unsigned char)*src))) {
			if (quoted) {
				if (*src == '"') {
					quoted = false;
					src++;
				} else if (*src == '\\') {
					src++;
					if (*src == '\0')
						goto fail;
					if (*src != '"' && *src != '`' &&
					    *src != '$' && *src != '\\')
						goto fail;
					if (!appendArgChar(&arg, &arglen, &argcap, *src++))
						goto fail;
				} else if (!appendArgChar(&arg, &arglen, &argcap, *src++)) {
					goto fail;
				}
			} else if (*src == '"') {
				quoted = true;
				saw_quote = true;
				src++;
			} else if (*src == '%') {
				src++;
				if (*src == '\0')
					break;
				if (*src == '%') {
					if (!appendArgChar(&arg, &arglen, &argcap, *src++))
						goto fail;
				} else if (strchr("fFuUdDnNickvm", *src)) {
					src++;
				} else {
					goto fail;
				}
			} else if (!appendArgChar(&arg, &arglen, &argcap, *src++)) {
				goto fail;
			}
		}

		if (quoted)
			goto fail;

		if (arglen > 0 || saw_quote) {
			if (argc == cap) {
				int newcap = cap ? cap * 2 : 8;
				char **newargv = wrealloc(argv, sizeof(char *) * newcap);

				if (!newargv)
					goto fail;
				argv = newargv;
				cap = newcap;
			}
			if (!arg)
				arg = wstrdup("");
			argv[argc++] = arg;
			arg = NULL;
		}
		wfree(arg);
	}

	if (argc == 0)
		goto fail;

	result = join_argv(argv, argc);

fail:
	wfree(arg);
	while (argc > 0)
		wfree(argv[--argc]);
	wfree(argv);
	return result;
}

/* (re-)initialize a XDGMenuEntry storage
 */
static void init_xdg_storage(XDGMenuEntry *xdg)
{

	if (xdg->Name)
		wfree(xdg->Name);
	if (xdg->TryExec)
		wfree(xdg->TryExec);
	if (xdg->Exec)
		wfree(xdg->Exec);
	if (xdg->Category)
		wfree(xdg->Category);
	if (xdg->Path)
		wfree(xdg->Path);

	xdg->Name = NULL;
	xdg->TryExec = NULL;
	xdg->Exec = NULL;
	xdg->Category = NULL;
	xdg->Path = NULL;
	xdg->Flags = 0;
	xdg->MatchLevel = -1;
}

/* (re-)initialize a WMMenuEntry storage
 */
static void init_wm_storage(WMMenuEntry *wm)
{
	if (wm->Flags & F_FREE_CMD_LINE)
		wfree(wm->CmdLine);

	wm->Name = NULL;
	wm->CmdLine = NULL;
	wm->Path = NULL;
	wm->Flags = 0;
}

/* get a key from line. allocates target, which must be wfreed later */
static void getKey(char **target, const char *line)
{
	const char *p;
	int kstart, kend;

	p = line;

	if (strchr(p, '=') == NULL) {		/* not `key' = `value' */
		*target = NULL;
		return;
	}

	kstart = 0;

	/* skip whitespace */
	while (isspace((unsigned char)*(p + kstart)))
		kstart++;

	/* skip up until first whitespace or '[' (localestring) or '=' */
	kend = kstart + 1;
	while (*(p + kend) && !isspace((unsigned char)*(p + kend)) && *(p + kend) != '=' && *(p + kend) != '[')
		kend++;

	*target = wstrndup(p + kstart, kend - kstart);
}

static void unescapeValue(char *value)
{
	char *p, *q;

	for (p = q = value; *p; p++) {
		if (*p != '\\') {
			*q++ = *p;
		} else {
			if (*++p == '\0')
				break;
			switch (*p) {
			case 's':  *q++ = ' ';  break;
			case 'n':  *q++ = '\n'; break;
			case 't':  *q++ = '\t'; break;
			case 'r':  *q++ = '\r'; break;
			case '\\': *q++ = '\\'; break;
			default:
				/*
				 * Skip invalid escape.
				 */
				break;
			}
		}
	}
	*q = '\0';
}

/* get a string value from line. allocates target, which must be wfreed later. */
static void getStringValue(char **target, const char *line)
{
	const char *p;
	char *value;
	int kstart;

	p = line;
	kstart = 0;

	/* skip until after '=' */
	while (*(p + kstart) && *(p + kstart) != '=')
		kstart++;
	kstart++;

	/* skip whitespace */
	while (*(p + kstart) && isspace((unsigned char)*(p + kstart)))
		kstart++;

	value = wstrdup(p + kstart);
	unescapeValue(value);

	wfree(*target);
	*target = value;
}

/* get a localized string value from line. allocates target, which must be wfreed later.
 * matching is dependent on the current value of target as well as on the
 * level the current value is matched on. guts matching algorithm is in
 * compare_matchlevel().
 */
static void getLocalizedStringValue(char **target, const char *line, int *match_level)
{
	const char *p;
	char *locale;
	int kstart;
	int sqbstart, sqbend;

	p = line;
	kstart = 0;
	sqbstart = 0;
	sqbend = 0;
	locale = NULL;

	/* skip until after '=', mark if '[' and ']' is found */
	while (*(p + kstart) && *(p + kstart) != '=') {
		switch (*(p + kstart)) {
			case '[': sqbstart = kstart + 1;break;
			case ']': sqbend = kstart;	break;
			default	:			break;
		}
		kstart++;
	}
	kstart++;

	/* skip whitespace */
	while (isspace(*(p + kstart)))
		kstart++;

	if (sqbstart > 0 && sqbend > sqbstart)
		locale = wstrndup(p + sqbstart, sqbend - sqbstart);

	/* if there is no value yet and this is the default key, return */
	if (!*target && !locale) {
		char *value = wstrdup(p + kstart);

		unescapeValue(value);
		*match_level = MATCH_DEFAULT;
		wfree(*target);
		*target = value;
		return;
	}

	if (compare_matchlevel(match_level, locale)) {
		char *value = wstrdup(p + kstart);

		unescapeValue(value);
		wfree(*target);
		*target = value;
	}

	wfree(locale);

	return;
}

/* get a boolean value from line */
static bool getBooleanValue(const char *line)
{
	char *p = NULL;
	int ret;

	getStringValue(&p, line);
	ret = strcmp(p, "true") == 0 ? true : false;
	wfree(p);

	return ret;
}

/* perform locale matching by implementing the algorithm specified in
 * xdg desktop entry specification, section "localized values for keys".
 */
static bool compare_matchlevel(int *current_level, const char *found_locale)
{
	/* current key locale */
	char *key_lang, *key_ctry, *key_enc, *key_mod;

	parse_locale(found_locale, &key_lang, &key_ctry, &key_enc, &key_mod);

	if (env_lang && key_lang && strcmp(env_lang, key_lang) != 0) {
		/*
		 * Shortcut: if key and env languages don't match,
		 * don't even bother. This takes care of the great
		 * majority of the cases without having to go through
		 * the more theoretical parts of the spec'd algo.
		 */
		wfree(key_lang);
		wfree(key_ctry);
		wfree(key_enc);
		wfree(key_mod);
		return false;
	}

	if (!env_mod && key_mod) {
		/*
		 * If LC_MESSAGES does not have a MODIFIER field,
		 * then no key with a modifier will be matched.
		 */
		wfree(key_lang);
		wfree(key_ctry);
		wfree(key_enc);
		wfree(key_mod);
		return false;
	}

	if (!env_ctry && key_ctry) {
		/*
		 * Similarly, if LC_MESSAGES does not have a COUNTRY field,
		 * then no key with a country specified will be matched.
		 */
		wfree(key_lang);
		wfree(key_ctry);
		wfree(key_enc);
		wfree(key_mod);
		return false;
	}

	/* LC_MESSAGES value: lang_COUNTRY@MODIFIER */
	if (env_lang && env_ctry && env_mod) {		/* lang_COUNTRY@MODIFIER */
		if (key_lang && key_ctry && key_mod &&
		    strcmp(env_lang, key_lang) == 0 &&
		    strcmp(env_ctry, key_ctry) == 0 &&
		    strcmp(env_mod,  key_mod) == 0) {
			*current_level = MATCH_LANG_COUNTRY_MODIFIER;
			wfree(key_lang);
			wfree(key_ctry);
			wfree(key_enc);
			wfree(key_mod);
			return true;
		} else if (key_lang && key_ctry &&	/* lang_COUNTRY */
		    strcmp(env_lang, key_lang) == 0 &&
		    strcmp(env_ctry, key_ctry) == 0 &&
		    *current_level < MATCH_LANG_COUNTRY) {
			*current_level = MATCH_LANG_COUNTRY;
			wfree(key_lang);
			wfree(key_ctry);
			wfree(key_enc);
			wfree(key_mod);
			return true;
		} else if (key_lang && key_mod &&	/* lang@MODIFIER */
		    strcmp(env_lang, key_lang) == 0 &&
		    strcmp(env_mod,  key_mod) == 0 &&
		    *current_level < MATCH_LANG_MODIFIER) {
			*current_level = MATCH_LANG_MODIFIER;
			wfree(key_lang);
			wfree(key_ctry);
			wfree(key_enc);
			wfree(key_mod);
			return true;
		} else if (key_lang &&			/* lang */
		    strcmp(env_lang, key_lang) == 0 &&
		    *current_level < MATCH_LANG) {
			*current_level = MATCH_LANG;
			wfree(key_lang);
			wfree(key_ctry);
			wfree(key_enc);
			wfree(key_mod);
			return true;
		} else {
			wfree(key_lang);
			wfree(key_ctry);
			wfree(key_enc);
			wfree(key_mod);
			return false;
		}
	}

	/* LC_MESSAGES value: lang_COUNTRY */
	if (env_lang && env_ctry) {			/* lang_COUNTRY */
		if (key_lang && key_ctry &&
		    strcmp(env_lang, key_lang) == 0 &&
		    strcmp(env_ctry, key_ctry) == 0 &&
		    *current_level < MATCH_LANG_COUNTRY) {
			*current_level = MATCH_LANG_COUNTRY;
			wfree(key_lang);
			wfree(key_ctry);
			wfree(key_enc);
			wfree(key_mod);
			return true;
		} else if (key_lang &&			/* lang */
		    strcmp(env_lang, key_lang) == 0 &&
		    *current_level < MATCH_LANG) {
			*current_level = MATCH_LANG;
			wfree(key_lang);
			wfree(key_ctry);
			wfree(key_enc);
			wfree(key_mod);
			return true;
		} else {
			wfree(key_lang);
			wfree(key_ctry);
			wfree(key_enc);
			wfree(key_mod);
			return false;
		}
	}

	/* LC_MESSAGES value: lang@MODIFIER */
	if (env_lang && env_mod) {			/* lang@MODIFIER */
		if (key_lang && key_mod &&
		    strcmp(env_lang, key_lang) == 0 &&
		    strcmp(env_mod,  key_mod) == 0 &&
		    *current_level < MATCH_LANG_MODIFIER) {
			*current_level = MATCH_LANG_MODIFIER;
			wfree(key_lang);
			wfree(key_ctry);
			wfree(key_enc);
			wfree(key_mod);
			return true;
		} else if (key_lang &&			/* lang */
		    strcmp(env_lang, key_lang) == 0 &&
		    *current_level < MATCH_LANG) {
			*current_level = MATCH_LANG;
			wfree(key_lang);
			wfree(key_ctry);
			wfree(key_enc);
			wfree(key_mod);
			return true;
		} else {
			wfree(key_lang);
			wfree(key_ctry);
			wfree(key_enc);
			wfree(key_mod);
			return false;
		}
	}

	/* LC_MESSAGES value: lang */
	if (env_lang) {					/* lang */
		if (key_lang &&
		    strcmp(env_lang, key_lang) == 0 &&
		    *current_level < MATCH_LANG) {
			*current_level = MATCH_LANG;
			wfree(key_lang);
			wfree(key_ctry);
			wfree(key_enc);
			wfree(key_mod);
			return true;
		} else {
			wfree(key_lang);
			wfree(key_ctry);
			wfree(key_enc);
			wfree(key_mod);
			return false;
		}
	}

	/* MATCH_DEFAULT is handled in getLocalizedStringValue */

	wfree(key_lang);
	wfree(key_ctry);
	wfree(key_enc);
	wfree(key_mod);
	return false;
}

/* get the (first) xdg main category from a list of categories
 */
static void  getMenuHierarchyFor(char **xdgmenuspec)
{
	char *category, *p;
	char buf[1024];

	if (!*xdgmenuspec || !**xdgmenuspec)
		return;

	category = wstrdup(*xdgmenuspec);
	wfree(*xdgmenuspec);
	memset(buf, 0, sizeof(buf));

	p = strtok(category, ";");
	while (p) {		/* get a known category */
		if (strcmp(p, "AudioVideo") == 0) {
			snprintf(buf, sizeof(buf), "%s", _("Audio & Video"));
			break;
		} else if (strcmp(p, "Audio") == 0) {
			snprintf(buf, sizeof(buf), "%s", _("Audio"));
			break;
		} else if (strcmp(p, "Video") == 0) {
			snprintf(buf, sizeof(buf), "%s", _("Video"));
			break;
		} else if (strcmp(p, "Development") == 0) {
			snprintf(buf, sizeof(buf), "%s", _("Development"));
			break;
		} else if (strcmp(p, "Education") == 0) {
			snprintf(buf, sizeof(buf), "%s", _("Education"));
			break;
		} else if (strcmp(p, "Game") == 0) {
			snprintf(buf, sizeof(buf), "%s", _("Game"));
			break;
		} else if (strcmp(p, "Graphics") == 0) {
			snprintf(buf, sizeof(buf), "%s", _("Graphics"));
			break;
		} else if (strcmp(p, "Network") == 0) {
			snprintf(buf, sizeof(buf), "%s", _("Network"));
			break;
		} else if (strcmp(p, "Office") == 0) {
			snprintf(buf, sizeof(buf), "%s", _("Office"));
			break;
		} else if (strcmp(p, "Science") == 0) {
			snprintf(buf, sizeof(buf), "%s", _("Science"));
			break;
		} else if (strcmp(p, "Settings") == 0) {
			snprintf(buf, sizeof(buf), "%s", _("Settings"));
			break;
		} else if (strcmp(p, "System") == 0) {
			snprintf(buf, sizeof(buf), "%s", _("System"));
			break;
		} else if (strcmp(p, "Utility") == 0) {
			snprintf(buf, sizeof(buf), "%s", _("Utility"));
			break;
		/* reserved categories */
		} else if (strcmp(p, "Screensaver") == 0) {
			snprintf(buf, sizeof(buf), "%s", _("Screensaver"));
			break;
		} else if (strcmp(p, "TrayIcon") == 0) {
			snprintf(buf, sizeof(buf), "%s", _("Tray Icon"));
			break;
		} else if (strcmp(p, "Applet") == 0) {
			snprintf(buf, sizeof(buf), "%s", _("Applet"));
			break;
		} else if (strcmp(p, "Shell") == 0) {
			snprintf(buf, sizeof(buf), "%s", _("Shell"));
			break;
		}
		p = strtok(NULL, ";");
	}

	wfree(category);

	if (!*buf)		/* come up with something if nothing found */
		snprintf(buf, sizeof(buf), "%s", _("Other"));

	*xdgmenuspec = wstrdup(buf);
}
