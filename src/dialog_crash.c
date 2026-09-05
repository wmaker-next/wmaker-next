/* dialog_crash.c - crash recovery dialog panel for the monitor process
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2014-2026 Window Maker Team
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

#include <xkbcommon/xkbcommon-keysyms.h>

#include <stdio.h>

#include <WINGs/WINGsP.h>

#include "WindowMaker.h"
#include "window.h"
#include "defaults.h"
#include "dialog.h"

/*
 ***********************************************************************
 * Crashing Dialog Panel
 ***********************************************************************
 */

typedef struct _CrashPanel {
	WMWindow *win;		/* main window */

	WMLabel *iconL;		/* application icon */
	WMLabel *nameL;		/* title of panel */

	WMFrame *sepF;		/* separator frame */

	WMLabel *noteL;		/* Title of note */
	WMLabel *note2L;	/* body of note with what happened */

	WMFrame *whatF;		/* "what to do next" frame */
	WMPopUpButton *whatP;	/* action selection popup button */

	WMButton *okB;		/* ok button */

	bool done;		/* if finished with this dialog */
	int action;		/* what to do after */

	xcb_keycode_t retKey;
	xcb_keycode_t upKey;
	xcb_keycode_t downKey;

} CrashPanel;

static void handleKeyPress(xcb_generic_event_t * event, void *clientData)
{
	CrashPanel *panel = (CrashPanel *) clientData;

	if (((xcb_key_press_event_t *)event)->detail == panel->retKey) {
		WMPerformButtonClick(panel->okB);
		return;
	}

	if (((xcb_key_press_event_t *)event)->detail == panel->upKey) {
		WMSelectPopUpButtonPreviousItem(panel->whatP);
		return;
	}

	if (((xcb_key_press_event_t *)event)->detail == panel->downKey) {
		WMSelectPopUpButtonNextItem(panel->whatP);
		return;
	}
}

static void okButtonCallback(void *self, void *clientData)
{
	CrashPanel *panel = (CrashPanel *) clientData;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;

	panel->done = true;
}

static void setCrashAction(void *self, void *clientData)
{
	WMPopUpButton *pop = (WMPopUpButton *) self;
	CrashPanel *panel = (CrashPanel *) clientData;

	panel->action = WMGetPopUpButtonSelectedItem(pop);
}

/* Make this read the logo from a compiled in pixmap -Dan */
static WMPixmap *getWindowMakerIconImage(WMScreen *scr)
{
	WMPixmap *pix = NULL;
	char *path = NULL;

	/* Get the Logo icon, without the default icon */
	path = get_icon_filename("Logo", "WMPanel", NULL, false);

	if (path) {
		RColor gray;

		gray.red = 0xae;
		gray.green = 0xaa;
		gray.blue = 0xae;
		gray.alpha = 0;

		pix = WMCreateBlendedPixmapFromFile(scr, path, &gray);
		wfree(path);
	}

	return pix;
}

#define PWIDTH	295
#define PHEIGHT	345

int wShowCrashingDialogPanel(int whatSig)
{
	CrashPanel *panel;
	WMScreen *scr;
	WMFont *font;
	WMPixmap *logo;
	int screen_no, scr_width, scr_height;
	int action;
	char buf[256];

	screen_no = dflt_screen;
	scr_width = wXDisplayWidth(xconn, screen_no);
	scr_height = wXDisplayHeight(xconn, screen_no);

	scr = WMCreateScreen(xconn, screen_no);
	if (!scr) {
		werror(_("cannot open connection for crashing dialog panel. Aborting."));
		return WMAbort;
	}

	panel = wmalloc(sizeof(CrashPanel));

	panel->retKey = W_KeysymToKeycode(scr, XKB_KEY_Return);
	panel->upKey = W_KeysymToKeycode(scr, XKB_KEY_Up);
	panel->downKey = W_KeysymToKeycode(scr, XKB_KEY_Down);

	panel->win = WMCreateWindow(scr, "crashingDialog");
	WMResizeWidget(panel->win, PWIDTH, PHEIGHT);
	WMMoveWidget(panel->win, (scr_width - PWIDTH) / 2, (scr_height - PHEIGHT) / 2);

	logo = getWindowMakerIconImage(scr);
	if (logo) {
		panel->iconL = WMCreateLabel(panel->win);
		WMResizeWidget(panel->iconL, 64, 64);
		WMMoveWidget(panel->iconL, 10, 10);
		WMSetLabelImagePosition(panel->iconL, WIPImageOnly);
		WMSetLabelImage(panel->iconL, logo);
	}

	panel->nameL = WMCreateLabel(panel->win);
	WMResizeWidget(panel->nameL, 200, 30);
	WMMoveWidget(panel->nameL, 80, 25);
	WMSetLabelTextAlignment(panel->nameL, WALeft);
	font = WMBoldSystemFontOfSize(scr, 24);
	WMSetLabelFont(panel->nameL, font);
	WMReleaseFont(font);
	WMSetLabelText(panel->nameL, _("Fatal error"));

	panel->sepF = WMCreateFrame(panel->win);
	WMResizeWidget(panel->sepF, PWIDTH + 4, 2);
	WMMoveWidget(panel->sepF, -2, 80);

	panel->noteL = WMCreateLabel(panel->win);
	WMResizeWidget(panel->noteL, PWIDTH - 20, 40);
	WMMoveWidget(panel->noteL, 10, 90);
	WMSetLabelTextAlignment(panel->noteL, WAJustified);
	snprintf(buf, sizeof(buf), _("Window Maker received signal %i."), whatSig);
	WMSetLabelText(panel->noteL, buf);

	panel->note2L = WMCreateLabel(panel->win);
	WMResizeWidget(panel->note2L, PWIDTH - 20, 100);
	WMMoveWidget(panel->note2L, 10, 130);
	WMSetLabelTextAlignment(panel->note2L, WALeft);
	snprintf(buf, sizeof(buf), /* Comment for the PO file: the %s is an email address */
	         _(" This fatal error occurred probably due to a bug."
	           " Please fill the included BUGFORM and report it to %s."),
	         PACKAGE_BUGREPORT);
	WMSetLabelText(panel->note2L, buf);
	WMSetLabelWraps(panel->note2L, true);

	panel->whatF = WMCreateFrame(panel->win);
	WMResizeWidget(panel->whatF, PWIDTH - 20, 50);
	WMMoveWidget(panel->whatF, 10, 240);
	WMSetFrameTitle(panel->whatF, _("What do you want to do now?"));

	panel->whatP = WMCreatePopUpButton(panel->whatF);
	WMResizeWidget(panel->whatP, PWIDTH - 20 - 70, 20);
	WMMoveWidget(panel->whatP, 35, 20);
	WMSetPopUpButtonPullsDown(panel->whatP, false);
	WMSetPopUpButtonText(panel->whatP, _("Select action"));
	WMAddPopUpButtonItem(panel->whatP, _("Abort and leave a core file"));
	WMAddPopUpButtonItem(panel->whatP, _("Restart Window Maker"));
	WMAddPopUpButtonItem(panel->whatP, _("Start alternate window manager"));
	WMSetPopUpButtonAction(panel->whatP, setCrashAction, panel);
	WMSetPopUpButtonSelectedItem(panel->whatP, WMRestart);
	panel->action = WMRestart;

	WMMapSubwidgets(panel->whatF);

	panel->okB = WMCreateCommandButton(panel->win);
	WMResizeWidget(panel->okB, 80, 26);
	WMMoveWidget(panel->okB, 205, 309);
	WMSetButtonText(panel->okB, _("OK"));
	WMSetButtonImage(panel->okB, WMGetSystemPixmap(scr, WSIReturnArrow));
	WMSetButtonAltImage(panel->okB, WMGetSystemPixmap(scr, WSIHighlightedReturnArrow));
	WMSetButtonImagePosition(panel->okB, WIPRight);
	WMSetButtonAction(panel->okB, okButtonCallback, panel);

	panel->done = 0;

	WMCreateEventHandler(WMWidgetView(panel->win), XCB_EVENT_MASK_KEY_PRESS, handleKeyPress, panel);

	WMRealizeWidget(panel->win);
	WMMapSubwidgets(panel->win);

	WMMapWidget(panel->win);

	xcb_set_input_focus(xconn, XCB_INPUT_FOCUS_PARENT, WMWidgetXID(panel->win), XCB_CURRENT_TIME);

	while (!panel->done) {
		xcb_generic_event_t *event = NULL;

		WMNextEvent(xconn, &event);
		if (!event)
			break;
		WMHandleEvent(event);
		free(event);
	}

	action = panel->action;

	WMUnmapWidget(panel->win);
	WMDestroyWidget(panel->win);
	wfree(panel);

	return action;
}
