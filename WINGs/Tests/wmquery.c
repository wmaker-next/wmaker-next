/*
 *  wmquery.c
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 2000-2004 Dan Pascu
 *  Copyright (c) 2009-2026 Window Maker Team
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, see <https://www.gnu.org/licenses/>.
 */

/*
 * Author: Len Trigg <trigg@cs.waikato.ac.nz>
 */

#include <WINGs/WINGs.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include "logo.xpm"

_Noreturn void wAbort(void)
{
	 exit(1);
}

char *ProgName;

_Noreturn void usage(void)
{
	fprintf(stderr,
		"usage:\n"
		"\t%s [-options]\n"
		"\n"
		"options:\n"
		"  -i <str>\tInitial entry contents (default none)\n"
		"  -p <str>\tPrompt message (default none)\n"
		"  -t <str>\tQuery window title (default none)\n"
		"\n"
		"information:\n"
		"\t%s pops up a WindowMaker style input panel.\n"
		"\n" "version:\n" "\t%s\n", ProgName, ProgName, __DATE__);
	exit(0);
}

int main(int argc, char **argv)
{
	xcb_connection_t *xc;
	int sn;
	xcb_connection_t *xc;
	WMScreen *scr;
	WMPixmap *pixmap;
	char *title = NULL;
	char *prompt = NULL;
	char *initial = NULL;
	char *result = NULL;
	int ch;

	WMInitializeApplication("WMQuery", &argc, argv);

	ProgName = argv[0];

	/* xcb_connect(NULL, ...) reads $DISPLAY exactly like libX11's
	 * XOpenDisplay("") did */
	xc = xcb_connect(NULL, &sn);
	if (xcb_connection_has_error(xc)) {
		puts("could not open display");
		exit(1);
	}

	while ((ch = getopt(argc, argv, "i:hp:t:")) != -1)
		switch (ch) {
		case 'i':
			initial = optarg;
			break;
		case 'p':
			prompt = optarg;
			break;
		case 't':
			title = optarg;
			break;
		default:
			usage();
		}

	for (; optind < argc; optind++)
		usage();

	scr = WMCreateSimpleApplicationScreen(xc, sn);

	pixmap = WMCreatePixmapFromXPMData(scr, GNUSTEP_XPM);

	WMSetApplicationIconPixmap(scr, pixmap);
	WMReleasePixmap(pixmap);

	if ((result = WMRunInputPanel(scr, NULL, title, prompt, initial, "OK", "Cancel")) != NULL)
		printf("%s\n", result);
	else
		printf("\n");
	return 0;
}
