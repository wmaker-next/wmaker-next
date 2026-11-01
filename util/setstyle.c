/* setstyle.c - loads style related options to wmaker
 *
 *  WindowMaker window manager
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

#ifdef __GLIBC__
#define _GNU_SOURCE		/* getopt_long */
#endif

#include "config.h"

#include <sys/stat.h>

#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#ifdef HAVE_STDNORETURN
#include <stdnoreturn.h>
#endif

#include <WINGs/WUtil.h>
#include <WINGs/WIPC.h>
#include <WINGs/WXProp.h>
#include <WINGs/WXCore.h>

#include "../src/wconfig.h"

#include "common.h"
#include "xcconn.h"

#define MAX_OPTIONS 128

char *FontOptions[] = {
	"IconTitleFont",
	"ClipTitleFont",
	"LargeDisplayFont",
	"MenuTextFont",
	"MenuTitleFont",
	"WindowTitleFont",
	NULL
};

char *CursorOptions[] = {
	"NormalCursor",
	"ArrowCursor",
	"MoveCursor",
	"ResizeCursor",
	"TopLeftResizeCursor",
	"TopRightResizeCursor",
	"BottomLeftResizeCursor",
	"BottomRightResizeCursor",
	"VerticalResizeCursor",
	"HorizontalResizeCursor",
	"WaitCursor",
	"QuestionCursor",
	"TextCursor",
	"SelectCursor",
	NULL
};

static const char *prog_name;
int ignoreFonts = 0;
int ignoreCursors = 0;

static bool isCursorOption(const char *option)
{
	int i;

	for (i = 0; CursorOptions[i] != NULL; i++) {
		if (strcasecmp(option, CursorOptions[i]) == 0) {
			return true;
		}
	}

	return false;
}

static bool isFontOption(const char *option)
{
	int i;

	for (i = 0; FontOptions[i] != NULL; i++) {
		if (strcasecmp(option, FontOptions[i]) == 0) {
			return true;
		}
	}

	return false;
}

/* prepend prefix and a separator to the string at arr[idx], leaving the
 * entry untouched if it is missing or not a string */
/* style options whose value is a single pixmap filename rather than a
 * texture array, prefixed the same way hackPathInTexture() prefixes
 * the file element of a (tpixmap, file, ...) form */
static const char * const pixmap_string_options[] = {
	"CloseButtonPixmap",
	"BrokenCloseButtonPixmap",
	"KillButtonPixmap",
	"MiniaturizeButtonPixmap",
	"MaximizeButtonPixmap",
	"RestoreButtonPixmap",
	NULL
};

static bool isPixmapStringOption(const char *option)
{
	int i;

	for (i = 0; pixmap_string_options[i] != NULL; i++)
		if (strcasecmp(option, pixmap_string_options[i]) == 0)
			return true;
	return false;
}

static void prefixPathInArray(WMPropList *arr, int idx, const char *prefix)
{
	WMPropList *file = WMGetFromPLArray(arr, idx);
	const char *name;
	char *dir, *full;

	if (!file || !WMIsPLString(file))
		return;
	name = WMGetFromPLString(file);
	if (!name)
		return;

	dir = wstrconcat(prefix, "/");
	full = wstrconcat(dir, name);
	wfree(dir);

	WMDeleteFromPLArray(arr, idx);
	WMInsertInPLArray(arr, idx, WMCreatePLString(full));
	wfree(full);
}

/*
 * finds elements in `texture' that reference external files,
 * prepends `prefix' to these files. `prefix' is a path component
 * that qualifies the external references to be absolute, possibly
 * pending further expansion
 */
static void hackPathInTexture(WMPropList * texture, const char *prefix)
{
	WMPropList *type;
	char *t;

	/* get texture type */
	type = WMGetFromPLArray(texture, 0);
	t = WMGetFromPLString(type);
	if (t == NULL)
		return;

	if (strcasecmp(t, "tpixmap") == 0 ||
	    strcasecmp(t, "spixmap") == 0 ||
	    strcasecmp(t, "mpixmap") == 0 ||
	    strcasecmp(t, "cpixmap") == 0 ||
	    strcasecmp(t, "fpixmap") == 0 ||
	    strcasecmp(t, "tvgradient") == 0 ||
	    strcasecmp(t, "thgradient") == 0 ||
	    strcasecmp(t, "tdgradient") == 0) {
		prefixPathInArray(texture, 1, prefix);
	} else if (strcasecmp(t, "bitmap") == 0) {
		prefixPathInArray(texture, 1, prefix);
		prefixPathInArray(texture, 2, prefix);
	}
}

static void hackPaths(WMPropList * style, const char *prefix)
{
	WMPropList *keys;
	WMPropList *key;
	WMPropList *value;
	const char *keyname;
	int i;

	keys = WMGetPLDictionaryKeys(style);

	for (i = 0; i < WMGetPropListItemCount(keys); i++) {
		key = WMGetFromPLArray(keys, i);
		keyname = WMGetFromPLString(key);
		if (!keyname)
			continue;

		value = WMGetFromPLDictionary(style, key);
		if (!value)
			continue;

		if (WMIsPLString(value) &&
		    isPixmapStringOption(keyname)) {
			const char *name = WMGetFromPLString(value);
			char *dir, *full;

			if (!name || !*name)
				continue;
			dir = wstrconcat(prefix, "/");
			full = wstrconcat(dir, name);
			wfree(dir);
			WMPutInPLDictionary(style, key, WMCreatePLString(full));
			wfree(full);
			continue;
		}

		if (strcasecmp(keyname, "WorkspaceSpecificBack") == 0) {
			if (WMIsPLArray(value)) {
				int j;
				WMPropList *texture;

				for (j = 0; j < WMGetPropListItemCount(value); j++) {
					texture = WMGetFromPLArray(value, j);

					if (texture && WMIsPLArray(texture)
					    && WMGetPropListItemCount(texture) > 2) {

						hackPathInTexture(texture, prefix);
					}
				}
			}
		} else {

			if (WMIsPLArray(value) && WMGetPropListItemCount(value) > 2) {

				hackPathInTexture(value, prefix);
			}
		}
	}

	WMReleasePropList(keys);
}

/* Return the representative colour of a texture description as a
 * caller-owned proplist string (release with WMReleasePropList), or
 * NULL when none can be derived. */
static WMPropList *getColor(WMPropList * texture)
{
	WMPropList *value, *type;
	const char *s1, *s2;
	char *str;

	type = WMGetFromPLArray(texture, 0);
	if (!type)
		return NULL;

	value = NULL;

	str = WMGetFromPLString(type);
	if (!str)
		return NULL;

	if (strcasecmp(str, "solid") == 0) {
		value = WMGetFromPLArray(texture, 1);
	} else if (strcasecmp(str, "dgradient") == 0
		   || strcasecmp(str, "hgradient") == 0 || strcasecmp(str, "vgradient") == 0) {
		WMPropList *c1, *c2;
		unsigned int r1, g1, b1, r2, g2, b2;
		char buffer[32];

		c1 = WMGetFromPLArray(texture, 1);
		c2 = WMGetFromPLArray(texture, 2);
		s1 = c1 ? WMGetFromPLString(c1) : NULL;
		s2 = c2 ? WMGetFromPLString(c2) : NULL;
		if (s1 && s2 &&
		    sscanf(s1, "#%2x%2x%2x", &r1, &g1, &b1) == 3
		    && sscanf(s2, "#%2x%2x%2x", &r2, &g2, &b2) == 3) {
			snprintf(buffer, sizeof(buffer), "#%02x%02x%02x", (r1 + r2) / 2, (g1 + g2) / 2, (b1 + b2) / 2);
			return WMCreatePLString(buffer);
		} else {
			value = c1;
		}
	} else if (strcasecmp(str, "mdgradient") == 0
		   || strcasecmp(str, "mhgradient") == 0 || strcasecmp(str, "mvgradient") == 0) {

		value = WMGetFromPLArray(texture, 1);

	} else if (strcasecmp(str, "tpixmap") == 0
		   || strcasecmp(str, "cpixmap") == 0 || strcasecmp(str, "spixmap") == 0) {

		value = WMGetFromPLArray(texture, 2);
	}

	return value ? WMRetainPropList(value) : NULL;
}

/*
 * since some of the options introduce incompatibilities, we will need
 * to do a kluge here or the themes ppl will get real annoying.
 * So, treat for the absence of the following options:
 * IconTitleColor
 * IconTitleBack
 */
/* look up dict[key] without leaking the temporary key proplist */
static WMPropList *lookupStringKey(WMPropList *dict, const char *key)
{
	WMPropList *k = WMCreatePLString(key);
	WMPropList *v = WMGetFromPLDictionary(dict, k);
	WMReleasePropList(k);
	return v;
}

/* dict[key] = value without leaking the temporary key proplist,
 * the dictionary retains both key and value */
static void putStringKey(WMPropList *dict, const char *key, WMPropList *value)
{
	WMPropList *k = WMCreatePLString(key);
	WMPutInPLDictionary(dict, k, value);
	WMReleasePropList(k);
}

static void hackStyle(WMPropList * style)
{
	WMPropList *keys, *tmp;
	int foundIconTitle = 0, foundResizebarBack = 0;
	int i;

	keys = WMGetPLDictionaryKeys(style);

	for (i = 0; i < WMGetPropListItemCount(keys); i++) {
		char *str;

		tmp = WMGetFromPLArray(keys, i);
		str = WMGetFromPLString(tmp);
		if (str) {
			if (ignoreFonts && isFontOption(str)) {
				WMRemoveFromPLDictionary(style, tmp);
				continue;
			}
			if (ignoreCursors && isCursorOption(str)) {
				WMRemoveFromPLDictionary(style, tmp);
				continue;
			}
			if (isFontOption(str)) {
				WMPropList *value;
				char *newfont, *oldfont;

				value = WMGetFromPLDictionary(style, tmp);
				if (value) {
					oldfont = WMGetFromPLString(value);
					newfont = convertFont(oldfont, false);
					if (newfont != oldfont) {
						value = WMCreatePLString(newfont);
						WMPutInPLDictionary(style, tmp, value);
						WMReleasePropList(value);
						wfree(newfont);
					}
				}
			}
			if (strcasecmp(str, "IconTitleColor") == 0 || strcasecmp(str, "IconTitleBack") == 0) {
				foundIconTitle = 1;
			} else if (strcasecmp(str, "ResizebarBack") == 0) {
				foundResizebarBack = 1;
			}
		}
	}
	WMReleasePropList(keys);

	if (!foundIconTitle) {
		/* set the default values */
		tmp = lookupStringKey(style, "FTitleColor");
		if (tmp)
			putStringKey(style, "IconTitleColor", tmp);

		tmp = lookupStringKey(style, "FTitleBack");
		if (tmp) {
			WMPropList *value = getColor(tmp);
			if (value) {
				putStringKey(style, "IconTitleBack", value);
				WMReleasePropList(value);
			}
		}
	}

	if (!foundResizebarBack) {
		/* set the default values */
		tmp = lookupStringKey(style, "UTitleBack");
		if (tmp) {
			WMPropList *value = getColor(tmp);
			if (value) {
				WMPropList *solid = WMCreatePLString("solid");
				WMPropList *t = WMCreatePLArray(solid, value, NULL);
				putStringKey(style, "ResizebarBack", t);
				WMReleasePropList(t);
				WMReleasePropList(solid);
				WMReleasePropList(value);
			}
		}
	}

	if (!lookupStringKey(style, "MenuStyle")) {
		WMPropList *normal = WMCreatePLString("normal");
		putStringKey(style, "MenuStyle", normal);
		WMReleasePropList(normal);
	}
}

static noreturn void print_help(int print_usage, int exitval)
{
	printf("Usage: %s [OPTIONS] FILE\n", prog_name);
	if (print_usage) {
		puts("Reads style/theme configuration from FILE and updates Window Maker.");
		puts("");
		puts("  --no-fonts          ignore font related options");
		puts("  --no-cursors        ignore cursor related options");
		puts("  --ignore <option>   ignore changes in the specified option");
		puts("  -h, --help          display this help and exit");
		puts("  -v, --version       output version information and exit");
	}
	exit(exitval);
}

int main(int argc, char **argv)
{
	WMPropList *prop, *style, *reply;
	char *path;
	char *file = NULL;
	struct stat st;
	size_t homelen;
	int i, ch, ignflag = 0;
	int ignoreCount = 0;
	char *ignoreList[MAX_OPTIONS];

	struct option longopts[] = {
		{ "version",	no_argument,		NULL,			'v' },
		{ "help",	no_argument,		NULL,			'h' },
		{ "no-fonts",	no_argument,		&ignoreFonts,		1 },
		{ "no-cursors",	no_argument,		&ignoreCursors,		1 },
		{ "ignore",	required_argument,	&ignflag,		1 },
		{ NULL,		0,			NULL,			0 }
	};

	prog_name = argv[0];
	while ((ch = getopt_long(argc, argv, "hv", longopts, NULL)) != -1)
		switch(ch) {
			case 'v':
				printf("%s (Window Maker %s)\n", prog_name, VERSION);
				return 0;
				/* NOTREACHED */
			case 'h':
				print_help(1, 0);
				/* NOTREACHED */
			case 0:
				if (ignflag) {
					if (ignoreCount >= MAX_OPTIONS) {
						printf("Maximum %d `ignore' arguments\n", MAX_OPTIONS);
						return 1;
					}
					ignoreList[ignoreCount++] = optarg;
					ignflag = 0;
				};
				break;
			default:
				print_help(0, 1);
				/* NOTREACHED */
		}

	argc -= optind;
	argv += optind;

	if (argc != 1)
		print_help(0, 1);

	file = argv[0];

	WMPLSetCaseSensitive(false);

	style = NULL;
	path = wdefaultspathfordomain("WindowMaker");

	prop = WMReadPropListFromFile(path);
	if (!prop) {
		perror(path);
		printf("%s: could not load WindowMaker configuration file.\n", prog_name);
		goto fail;
	}

	if (stat(file, &st) < 0) {
		perror(file);
		goto fail;
	}
	if (S_ISDIR(st.st_mode)) {		/* theme pack */
		char buf[PATH_MAX];
		char *homedir;

		if (realpath(file, buf) == NULL) {
			perror(file);
			goto fail;
		}
		if (wstrlcat(buf, "/style", sizeof(buf)) >= sizeof(buf)) {
			printf("%s: %s: path too long\n", prog_name, file);
			goto fail;
		}

		if (stat(buf, &st) != 0 || !S_ISREG(st.st_mode)) {	/* maybe symlink too? */
			printf("%s: %s: style file not found or not a file\n", prog_name, buf);
			goto fail;
		}

		style = WMReadPropListFromFile(buf);
		if (!style) {
			perror(buf);
			printf("%s: could not load style file.\n", prog_name);
			goto fail;
		}

		buf[strlen(buf) - 6 /* strlen("/style") */] = '\0';
		homedir = wstrdup(wgethomedir());
		homelen = strlen(homedir);
		if (homelen > 1	&&	/* this is insane, wgethomedir() returns `/' on error */
		    strncmp(homedir, buf, homelen) == 0 &&
		    (buf[homelen] == '/' || buf[homelen] == '\0')) {
			/* theme pack is under ${HOME}; exchange ${HOME} part
			 * for `~' so it gets portable references to the user home dir */
			*buf = '~';
			memmove(buf + 1, buf + homelen, strlen(buf) - homelen + 1);
		}
		wfree(homedir);

		hackPaths(style, buf);		/* this will prefix pixmaps in the style
						 * with absolute(ish) references */

	} else {				/* normal style file */

		style = WMReadPropListFromFile(file);
		if (!style) {
			perror(file);
			printf("%s:could not load style file.\n", prog_name);
			goto fail;
		}
	}

	if (!WMIsPLDictionary(style)) {
		printf("%s: '%s' is not a style file/theme\n", prog_name, file);
		goto fail;
	}

	hackStyle(style);

	if (ignoreCount > 0) {
		for (i = 0; i < ignoreCount; i++) {
			WMPropList *ikey = WMCreatePLString(ignoreList[i]);
			WMRemoveFromPLDictionary(style, ikey);
			WMReleasePropList(ikey);
		}
	}

	WMMergePLDictionaries(prop, style, true);
	WMReleasePropList(style);
	style = NULL;

	if (!WMWritePropListToFile(prop, path)) {
		werror("could not write %s", path);
		goto fail;
	}
	WMReleasePropList(prop);
	wfree(path);

	reply = wIPCCommand(NULL, "reconfigure");
	if (reply) {
		WMReleasePropList(reply);
		return 0;
	}

	/* fall back to the legacy X ClientMessage when no IPC socket is
	 * reachable, e.g. talking to an older Window Maker */
	{
		xcb_connection_t *xc;
		int sn;

		xc = xcconn_open(NULL, NULL, 0, &sn);
		if (xc) {
			WXSendWmakerCommandXC(xc, sn, "Reconfigure");
			xcb_disconnect(xc);
		}
	}

	return 0;

fail:
	if (style)
		WMReleasePropList(style);
	if (prop)
		WMReleasePropList(prop);
	wfree(path);
	return 1;
}
