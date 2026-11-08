/*
 * wmmenugen - Window Maker PropList menu generator
 *
 * miscellaneous functions
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

#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <WINGs/WUtil.h>


static const char *const terminals[] = {
	"x-terminal-emulator", /* Debian wrapper to launch user's preferred X terminal */
	"aterm",    /* AfterStep's X terminal, which provides "transparency" */
	"eterm",    /* Enlightenment's terminal, designed for eye-candyness (and efficiency) */
	"gnome-terminal",      /* GNOME project's terminal */
	"konsole",  /* KDE project's terminals */
	"kterm",    /* a Multi-Lingual Terminal based on xterm, originally by Katsuya Sano */
	"mlterm",   /* a Multi-Lingual Terminal emulator written from scratch */
	"rxvt",     /* a slimmed-down xterm */
	"mrxvt",    /* rxvt with support for tabs amongst other things */
	"pterm",    /* terminal based on PuTTY, a popular SSH client for Windows */
	"xterm",    /* the standard terminal provided by the X Window System */
	"dtterm"    /* provided by CDE, a frequent Desktop Environment in proprietary UNIXs */
};

/* pick a terminal emulator by finding the first existing entry of `terminals'
 * in $PATH. the returned pointer should be wfreed later.
 * if $WMMENU_TERMINAL exists in the environment, it's value overrides this
 * detection.
 */
char *find_terminal_emulator(void)
{
	char *path, *t;
	int i;

	t = getenv("WMMENU_TERMINAL");
	if (t)
		return wstrdup(t);

	path = getenv("PATH");
	if (!path)
		return NULL;

	for (i = 0; i < wlengthof(terminals); i++) {
		t = wfindfile(path, terminals[i]);
		if (t) {
			wfree(t);
			return wstrdup(terminals[i]);
		}
	}

	return NULL;
}

/* tokenize `what' (LC_MESSAGES or LANG if `what' is NULL) in the form of
 * `language[_territory][.codeset][@modifier]' into separate language, country,
 * encoding, modifier components, which are allocated on demand and should be
 * wfreed later. components that do not exist in `what' are set to NULL.
 */
void parse_locale(const char *what, char **language, char **country, char **encoding, char **modifier)
{
	char *e, *p;

	*language = *country = *encoding = *modifier = NULL;

	if (what == NULL) {
		e = getenv("LC_MESSAGES");
		if (e == NULL) {
			e = getenv("LANG");	/* this violates the spec */
			if (e == NULL)
				return;
		}
		e = wstrdup(e);
	} else {
		e = wstrdup(what);
	}

	if (strlen(e) == 0 ||
	    strcmp(e, "POSIX") == 0 ||
	    strcmp(e, "C") == 0)
		goto out;

	p = strchr(e, '@');
	if (p) {
		*modifier = wstrdup(p + 1);
		*p = '\0';
	}

	p = strchr(e, '.');
	if (p) {
		*encoding = wstrdup(p + 1);
		*p = '\0';
	}

	p = strchr(e, '_');
	if (p) {
		*country = wstrdup(p + 1);
		*p = '\0';
	}

	if (strlen(e) > 0)
		*language = wstrdup(e);

out:
	wfree(e);
	return;

}

/* determine whether (first token of) given file is in $PATH
 */
bool fileInPath(const char *file)
{
	char *p, *t;
	static char *path = NULL;

	if (!file || !*file)
		return false;

	/* if it's an absolute path spec, don't override the user.
	 * s/he might just know better.
	 */
	if (*file == '/')
		return true;

	/* if it has a directory separator at random places,
	 * we might know better.
	 */
	if (strchr(file, '/'))
		return false;

	if (!path) {
		path = getenv("PATH");
		if (!path)
			return false;
	}

	p = wstrdup(file);
	t = strpbrk(p, " \t");
	if (t)
		*t = '\0';

	t = wfindfile(path, p);
	wfree(p);

	if (t) {
		wfree(t);
		return true;
	}

	return false;
}

/* like fileInPath() but the whole name is one executable, escaped
 * spaces and all */
bool fileInPathExact(const char *file)
{
	char *path, *t;

	if (!file || !*file)
		return false;

	if (*file == '/')
		return true;

	if (strchr(file, '/'))
		return false;

	path = getenv("PATH");
	if (!path)
		return false;

	t = wfindfile(path, file);
	if (t) {
		wfree(t);
		return true;
	}
	return false;
}

#define MAX_LINE_BYTES (1024 * 1024)

/* one whole line per call, so a long Exec= value is not split into
 * fragments. Returns 1 on success, 0 at end of file, -1 when an
 * overlong line was discarded */
int wmmenu_read_line(FILE *fp, char **out)
{
	char chunk[1024];
	char *line = NULL;
	size_t len = 0, cap = 0;

	*out = NULL;
	if (!fgets(chunk, (int)sizeof(chunk), fp))
		return 0;

	for (;;) {
		size_t n = strlen(chunk);
		int complete = n > 0 && chunk[n - 1] == '\n';

		if (len + n + 1 > MAX_LINE_BYTES) {
			while (!complete) {
				if (!fgets(chunk, (int)sizeof(chunk), fp))
					break;
				n = strlen(chunk);
				complete = n > 0 && chunk[n - 1] == '\n';
			}
			wfree(line);
			fprintf(stderr, "wmmenugen: ignoring overlong input line\n");
			return -1;
		}
		if (len + n + 1 > cap) {
			cap = cap ? cap * 2 : 256;
			while (cap < len + n + 1)
				cap *= 2;
			line = wrealloc(line, cap);
		}
		memcpy(line + len, chunk, n);
		len += n;
		if (complete || !fgets(chunk, (int)sizeof(chunk), fp))
			break;
	}

	while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
		len--;
	line[len] = '\0';
	*out = line;
	return 1;
}
