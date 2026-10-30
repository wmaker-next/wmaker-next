/*
 *  testmywidget.c
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

#include <WINGs/WINGs.h>
#include <stdio.h>
#include <stdlib.h>

#include "mywidget.h"

_Noreturn void wAbort(void)
{
	exit(1);
}

static void multiSelectActionCB(WMWidget *self, void *data)
{
	(void)self;
	(void)data;
	printf("multiselect: mask=0x%llx\n",
		(unsigned long long)WMGetMultiSelectPopUpButtonSelectionMask(
			(WMMultiSelectPopUpButton *)self));
}

static WMMultiSelectPopUpButton *testMultiSelectPopUp(WMWidget *parent)
{
	WMMultiSelectPopUpButton *btn;
	int i;

	btn = WMCreateMultiSelectPopUpButton(parent);
	WMMoveWidget(btn, 10, 40);
	WMResizeWidget(btn, 130, 20);
	WMSetMultiSelectPopUpButtonCaption(btn, "Nowhere in particular");
	WMSetMultiSelectPopUpButtonAction(btn, multiSelectActionCB, NULL);

	for (i = 0; i < 3; i++) {
		char buf[32];

		snprintf(buf, sizeof(buf), "Workspace %d", i + 1);
		WMAddMultiSelectPopUpButtonItem(btn, buf);
	}

	/* pre-select workspaces 1 and 3 (bits 0 and 2) */
	WMSetMultiSelectPopUpButtonSelectionMask(btn, (1ULL << 0) | (1ULL << 2));
	return btn;
}

int main(int argc, char **argv)
{
	xcb_connection_t *xc;
	int sn;
	xcb_connection_t *xc;
	WMScreen *scr;
	WMWindow *win;
	MyWidget *thing;
	WMMultiSelectPopUpButton *msp;
	uint64_t m;

	WMInitializeApplication("Test", &argc, argv);

	/* xcb_connect(NULL, ...) reads $DISPLAY exactly like libX11's
	 * XOpenDisplay("") did */
	xc = xcb_connect(NULL, &sn);
	if (xcb_connection_has_error(xc)) {
		wfatal("could not open display");
		exit(1);
	}

	scr = WMCreateSimpleApplicationScreen(xc, sn);

	/* init our widget */
	InitMyWidget(scr);

	win = WMCreateWindow(scr, "test");
	WMResizeWidget(win, 150, 80);

	thing = CreateMyWidget(win);
	SetMyWidgetText(thing, "The Test");
	WMResizeWidget(thing, 100, 20);
	WMMoveWidget(thing, 10, 10);

	/* exercise the multi-select popup button */
	msp = testMultiSelectPopUp(win);

	/* sanity-check the mask API before entering the event loop */
	m = WMGetMultiSelectPopUpButtonSelectionMask(msp);
	if (m != ((1ULL << 0) | (1ULL << 2))) {
		fprintf(stderr, "FAIL: mask=0x%llx want=0x%llx\n",
			(unsigned long long)m,
			(unsigned long long)((1ULL << 0) | (1ULL << 2)));
		exit(1);
	}
	if (WMGetMultiSelectPopUpButtonNumberOfItems(msp) != 3) {
		fprintf(stderr, "FAIL: item count\n");
		exit(1);
	}
	WMSetMultiSelectPopUpButtonSelectionMask(msp, 0);
	if (WMGetMultiSelectPopUpButtonSelectionMask(msp) != 0) {
		fprintf(stderr, "FAIL: clear mask\n");
		exit(1);
	}
	WMSetMultiSelectPopUpButtonSelectionMask(msp, (1ULL << 0) | (1ULL << 2));
	printf("ok - multiselectpopup\n");
	fflush(stdout);

	WMRealizeWidget(win);
	WMMapSubwidgets(win);
	WMMapWidget(win);

	WMScreenMainLoop(scr);

	return 0;
}
