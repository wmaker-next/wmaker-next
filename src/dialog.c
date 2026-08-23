/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 1998-2003 Dan Pascu
 *  Copyright (c) 2014-2023 Window Maker Team
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

/* dialog.c - small modal dialog and info panels for internal use */

#include "wconfig.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/utsname.h>

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#include <wraster.h>

#include <WINGs/WINGsP.h>

#include "WindowMaker.h"
#include "screen.h"
#include "window.h"
#include "dialog.h"
#include "misc.h"
#include "stacking.h"
#include "framewin.h"
#include "actions.h"
#include "ipc.h"
#include "defaults.h"

int wMessageDialog(WScreen *scr, const char *title, const char *message, const char *defBtn, const char *altBtn, const char *othBtn)
{
	WMAlertPanel *panel;
	Window parent;
	WWindow *wwin;
	int result;
	WMPoint center;
	int pwidth, pheight;

	wIPCEmitDialog(scr, "message", title, message);

	panel = WMCreateScaledAlertPanel(scr->wmscreen, NULL, title, message, defBtn, altBtn, othBtn);
	pwidth = WMWidgetWidth(panel->win);
	pheight = WMWidgetHeight(panel->win);

	parent = XCreateSimpleWindow(dpy, scr->root_win, 0, 0, pwidth, pheight, 0, 0, 0);

	XReparentWindow(dpy, WMWidgetXID(panel->win), parent, 0, 0);

	center = wGetCenter(scr, pwidth, pheight);
	wwin = wManageInternalWindow(scr, parent, None, NULL, center.x, center.y, pwidth, pheight);
	wwin->client_leader = WMWidgetXID(panel->win);

	WMMapWidget(panel->win);

	wWindowMap(wwin);

	WMRunModalLoop(WMWidgetScreen(panel->win), WMWidgetView(panel->win));

	result = panel->result;

	WMUnmapWidget(panel->win);

	wUnmanageWindow(wwin, False, False);

	WMDestroyAlertPanel(panel);

	XDestroyWindow(dpy, parent);

	return result;
}

static void toggleSaveSession(WMWidget *w, void *data)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) data;

	w_global.config->save_session_on_exit = WMGetButtonSelected((WMButton *) w);
}

int wExitDialog(WScreen *scr, const char *title, const char *message, const char *defBtn, const char *altBtn, const char *othBtn)
{
	WMAlertPanel *panel;
	WMButton *saveSessionBtn;
	Window parent;
	WWindow *wwin;
	WMPoint center;
	int result;
	int pwidth, pheight;

	panel = WMCreateScaledAlertPanel(scr->wmscreen, NULL, title, message, defBtn, altBtn, othBtn);
	pwidth = WMWidgetWidth(panel->win);
	pheight = WMWidgetHeight(panel->win);

	/* add save session button */
	saveSessionBtn = WMCreateSwitchButton(panel->hbox);
	WMSetButtonAction(saveSessionBtn, toggleSaveSession, NULL);
	WMAddBoxSubview(panel->hbox, WMWidgetView(saveSessionBtn), False, True, pwidth / 2, 0, 0);
	WMSetButtonText(saveSessionBtn, _("Save workspace state"));
	WMSetButtonSelected(saveSessionBtn, w_global.config->save_session_on_exit);
	WMRealizeWidget(saveSessionBtn);
	WMMapWidget(saveSessionBtn);

	parent = XCreateSimpleWindow(dpy, scr->root_win, 0, 0, pwidth, pheight, 0, 0, 0);

	XReparentWindow(dpy, WMWidgetXID(panel->win), parent, 0, 0);

	center = wGetCenter(scr, pwidth, pheight);
	wwin = wManageInternalWindow(scr, parent, None, NULL, center.x, center.y, pwidth, pheight);

	wwin->client_leader = WMWidgetXID(panel->win);

	WMMapWidget(panel->win);

	wWindowMap(wwin);

	WMRunModalLoop(WMWidgetScreen(panel->win), WMWidgetView(panel->win));

	result = panel->result;

	WMUnmapWidget(panel->win);

	wUnmanageWindow(wwin, False, False);

	WMDestroyAlertPanel(panel);

	XDestroyWindow(dpy, parent);

	return result;
}


int wInputDialog(WScreen *scr, const char *title, const char *message, char **text)
{
	WWindow *wwin;
	Window parent;
	WMInputPanel *panel;
	char *result;
	WMPoint center;

	wIPCEmitDialog(scr, "input", title, message);
	int pwidth, pheight;

	panel = WMCreateScaledInputPanel(scr->wmscreen, NULL, title, message, *text, _("OK"), _("Cancel"));
	pwidth = WMWidgetWidth(panel->win);
	pheight = WMWidgetHeight(panel->win);

	parent = XCreateSimpleWindow(dpy, scr->root_win, 0, 0, pwidth, pheight, 0, 0, 0);
	XReparentWindow(dpy, WMWidgetXID(panel->win), parent, 0, 0);

	center = wGetCenter(scr, pwidth, pheight);
	wwin = wManageInternalWindow(scr, parent, None, NULL, center.x, center.y, pwidth, pheight);

	wwin->client_leader = WMWidgetXID(panel->win);

	WMMapWidget(panel->win);

	wWindowMap(wwin);

	WMRunModalLoop(WMWidgetScreen(panel->win), WMWidgetView(panel->win));

	if (panel->result == WAPRDefault)
		result = WMGetTextFieldText(panel->text);
	else
		result = NULL;

	wUnmanageWindow(wwin, False, False);

	WMDestroyInputPanel(panel);

	XDestroyWindow(dpy, parent);

	if (result == NULL)
		return False;

	if (*text)
		wfree(*text);
	*text = result;

	return True;
}
/*
 ***********************************************************************
 * Info Panel
 ***********************************************************************
 */

typedef struct {
	WScreen *scr;
	WWindow *wwin;
	WMWindow *win;
	WMLabel *logoL;
	WMLabel *name1L;
	WMFrame *lineF;
	WMLabel *name2L;
	WMLabel *versionL;
	WMLabel *infoL;
	WMLabel *copyrL;
} InfoPanel;

#define COPYRIGHT_TEXT  \
    "Copyright \xc2\xa9 1997-2006 Alfredo K. Kojima\n"\
    "Copyright \xc2\xa9 1998-2006 Dan Pascu\n"\
    "Copyright \xc2\xa9 2013-%hu Window Maker Developers Team"

static InfoPanel *infoPanel = NULL;

static void destroyInfoPanel(WCoreWindow *foo, void *data, XEvent *event)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;
	(void) data;
	(void) event;

	WMUnmapWidget(infoPanel);
	wUnmanageWindow(infoPanel->wwin, False, False);
	WMDestroyWidget(infoPanel->win);
	wfree(infoPanel);
	infoPanel = NULL;
}

char *getPrettyOSName(void)
{
	char line[200];
	char *token;
	char *posn = NULL;
	const char s[2] = "=";
	FILE *fp;

	fp = fopen("/etc/os-release", "r");
	if (!fp) {
		fp = fopen("/usr/lib/os-release", "r");
		if (!fp) {
			wwarning(_("no os-release file on the system"));
			return NULL;
		}
	}

	while (fgets(line, sizeof(line), fp) != NULL) {
		if (strncmp(line, "PRETTY_NAME", strlen("PRETTY_NAME")) == 0) {
			line[strcspn(line, "\r\n")] = 0;
			token = strtok(line, s);
			if (token) {
				//get straight to the second part of the line
				token = strtok(NULL, s);
				if (token) {
					//removing potential quotes
					char *dst = token;
					char *src = token;
					char c;

					while ((c = *src++) != '\0') {
						if (c == '\\') {
							*dst++ = c;
							if ((c = *src++) == '\0')
								break;
							*dst++ = c;
						} else if (c != '"' && c != '\'')
							*dst++ = c;
					}
					*dst = '\0';
					posn = wstrdup(token);
				}
			}
			break;
		}
	}
	fclose(fp);
	return posn;
}

void wShowInfoPanel(WScreen *scr)
{
	InfoPanel *panel;
	WMPixmap *logo;
	WMFont *font;
	char *strbuf = NULL;
	const char *separator;
	/* sized for two utsname fields (SYS_NMLN is 256 on FreeBSD) plus
	 * the surrounding format text, so gcc -Wformat-truncation does not
	 * flag the "Running on: %s (%s)" line below */
	char buffer[640];
	Window parent;
	WWindow *wwin;
	WMPoint center;
	char **strl;
	int i, width = 50, sepHeight;
	char *visuals[] = {
		"StaticGray",
		"GrayScale",
		"StaticColor",
		"PseudoColor",
		"TrueColor",
		"DirectColor"
	};
	int wmScaleWidth, wmScaleHeight;
	int pwidth, pheight;
	int current_year = 2020;
	time_t s;
	struct tm *current_time;
	struct utsname uts;

	s = time(NULL);
	if (s) {
		current_time = localtime(&s);
		if (current_time->tm_year > (current_year - 1900))
			current_year = current_time->tm_year + 1900;
	}

	if (infoPanel) {
		if (infoPanel->scr == scr) {
			wRaiseFrame(infoPanel->wwin->frame->core);
			wSetFocusTo(scr, infoPanel->wwin);
		}
		return;
	}

	panel = wmalloc(sizeof(InfoPanel));

	panel->scr = scr;

	panel->win = WMCreateWindow(scr->wmscreen, "info");
	WMGetScaleBaseFromSystemFont(scr->wmscreen, &wmScaleWidth, &wmScaleHeight);
#if defined(HAVE_MALLOC_H) && defined(HAVE_MALLINFO2) && defined(DEBUG)
	pwidth = WMScaleX(432);
#else
	pwidth = WMScaleX(392);
#endif
	pheight = WMScaleY(270);
	WMResizeWidget(panel->win, pwidth, pheight);

	logo = WMCreateApplicationIconBlendedPixmap(scr->wmscreen, (RColor *) NULL);
	if (!logo) {
		logo = WMRetainPixmap(WMGetApplicationIconPixmap(scr->wmscreen));
	}
	if (logo) {
		panel->logoL = WMCreateLabel(panel->win);
		WMResizeWidget(panel->logoL, WMScaleX(64), WMScaleY(64));
		WMMoveWidget(panel->logoL, WMScaleX(30), WMScaleY(20));
		WMSetLabelImagePosition(panel->logoL, WIPImageOnly);
		WMSetLabelImage(panel->logoL, logo);
		WMReleasePixmap(logo);
	}

	sepHeight = WMScaleY(3);
	panel->name1L = WMCreateLabel(panel->win);
	WMResizeWidget(panel->name1L, WMScaleX(240), WMScaleY(30) + WMScaleY(2));
	WMMoveWidget(panel->name1L, WMScaleX(100), WMScaleY(30) - WMScaleY(2) - sepHeight);

	snprintf(buffer,
		sizeof(buffer),
		"Lucida Sans,Comic Sans MS,URW Gothic L,Trebuchet MS:italic:pixelsize=%d:antialias=true",
		WMScaleY(24));
	font = WMCreateFont(scr->wmscreen, buffer);
	strbuf = "Window Maker";
	if (font) {
		width = WMWidthOfString(font, strbuf, strlen(strbuf));
		WMSetLabelFont(panel->name1L, font);
		WMReleaseFont(font);
	}
	WMSetLabelTextAlignment(panel->name1L, WACenter);
	WMSetLabelText(panel->name1L, strbuf);

	panel->lineF = WMCreateFrame(panel->win);
	WMResizeWidget(panel->lineF, width, sepHeight);
	WMMoveWidget(panel->lineF, WMScaleX(100) + (WMScaleX(240) - width) / 2, WMScaleY(60) - sepHeight);
	WMSetFrameRelief(panel->lineF, WRSimple);
	WMSetWidgetBackgroundColor(panel->lineF, scr->black);

	panel->name2L = WMCreateLabel(panel->win);
	WMResizeWidget(panel->name2L, WMScaleX(240), WMScaleY(24));
	WMMoveWidget(panel->name2L, WMScaleX(100), WMScaleY(60));
	snprintf(buffer, sizeof(buffer), "URW Gothic L,Nimbus Sans L:pixelsize=%d:antialias=true", WMScaleY(16));
	font = WMCreateFont(scr->wmscreen, buffer);
	if (font) {
		WMSetLabelFont(panel->name2L, font);
		WMReleaseFont(font);
		font = NULL;
	}
	WMSetLabelTextAlignment(panel->name2L, WACenter);
	WMSetLabelText(panel->name2L, _("Window Manager for X"));

	snprintf(buffer, sizeof(buffer), _("Version %s"), VERSION);
	panel->versionL = WMCreateLabel(panel->win);
	WMResizeWidget(panel->versionL, WMScaleX(310), WMScaleY(16));
	WMMoveWidget(panel->versionL, WMScaleX(30), WMScaleY(95));
	WMSetLabelTextAlignment(panel->versionL, WARight);
	WMSetLabelText(panel->versionL, buffer);
	WMSetLabelWraps(panel->versionL, False);

	panel->copyrL = WMCreateLabel(panel->win);
	WMResizeWidget(panel->copyrL, WMScaleX(360), WMScaleY(60));
	WMMoveWidget(panel->copyrL, WMScaleX(15), WMScaleY(215));
	WMSetLabelTextAlignment(panel->copyrL, WALeft);

	snprintf(buffer, sizeof(buffer), COPYRIGHT_TEXT, current_year);
	WMSetLabelText(panel->copyrL, buffer);
	font = WMSystemFontOfSize(scr->wmscreen, WMScaleY(11));
	if (font) {
		WMSetLabelFont(panel->copyrL, font);
		WMReleaseFont(font);
		font = NULL;
	}

	strbuf = NULL;
	if (uname(&uts) != -1) {
		char *posn = getPrettyOSName();
		if (posn) {
			snprintf(buffer, sizeof(buffer), _("Running on: %s (%s)\n"), posn, uts.machine);
			free(posn);
		}
		else
			snprintf(buffer, sizeof(buffer), _("Running on: %s (%s)\n"), uts.sysname, uts.machine);
		strbuf = wstrappend(strbuf, buffer);
	}

	snprintf(buffer, sizeof(buffer), _("Using visual 0x%x: %s %ibpp "),
		 (unsigned)scr->w_visual->visualid, visuals[scr->w_visual->class], scr->w_depth);
	strbuf = wstrappend(strbuf, buffer);

	switch (scr->w_depth) {
	case 15:
		strbuf = wstrappend(strbuf, _("(32 thousand colors)\n"));
		break;
	case 16:
		strbuf = wstrappend(strbuf, _("(64 thousand colors)\n"));
		break;
	case 24:
	case 32:
		strbuf = wstrappend(strbuf, _("(16 million colors)\n"));
		break;
	case 30:
	case 40:
		strbuf = wstrappend(strbuf, _("(1 billion colors)\n"));
		break;
	default:
		snprintf(buffer, sizeof(buffer), _("(%d colors)\n"), 1 << scr->w_depth);
		strbuf = wstrappend(strbuf, buffer);
		break;
	}

#if defined(HAVE_MALLOC_H) && defined(HAVE_MALLINFO2)
	{
		struct mallinfo2 ma = mallinfo2();
		snprintf(buffer, sizeof(buffer),
#ifdef DEBUG
					_("Total memory allocated: %lu kB (in use: %lu kB, %lu free chunks)\n"),
#else
					_("Total memory allocated: %lu kB (in use: %lu kB)\n"),
#endif
					(ma.arena + ma.hblkhd) / 1024,
					(ma.uordblks + ma.hblkhd) / 1024
#ifdef DEBUG
					/*
					 * This information is representative of the memory
					 * fragmentation. In ideal case it should be 1, but
					 * that is never possible
					 */
					, ma.ordblks
#endif
					);

		strbuf = wstrappend(strbuf, buffer);
	}
#endif

	strbuf = wstrappend(strbuf, _("Image formats: "));
	strl = RSupportedFileFormats();
	separator = NULL;
	for (i = 0; strl[i] != NULL; i++) {
		if (separator != NULL)
			strbuf = wstrappend(strbuf, separator);
		strbuf = wstrappend(strbuf, strl[i]);
		separator = ", ";
	}

	strbuf = wstrappend(strbuf, _("\nAdditional support for: "));
	strbuf = wstrappend(strbuf, "WMSPEC");

#ifdef USE_MWM_HINTS
	strbuf = wstrappend(strbuf, ", MWM");
#endif

#ifdef USE_DOCK_XDND
	strbuf = wstrappend(strbuf, ", XDnD");
#endif

#ifdef USE_XRES
	strbuf = wstrappend(strbuf, ", XRes");
#endif

#ifdef USE_MAGICK
	strbuf = wstrappend(strbuf, ", ImageMagick");
#endif

	strbuf = wstrappend(strbuf, _("\n"));
	strbuf = wstrappend(strbuf, "RandR: ");
	snprintf(buffer, sizeof(buffer), scr->xine_info.count > 1 ? _("%d head(s) found") : _("%d head found"), scr->xine_info.count);
	strbuf = wstrappend(strbuf, buffer);

	panel->infoL = WMCreateLabel(panel->win);
	WMMoveWidget(panel->infoL, WMScaleX(15), WMScaleY(115));
	WMSetLabelText(panel->infoL, strbuf);
	font = WMSystemFontOfSize(scr->wmscreen, WMScaleY(11));
	if (font) {
		const char *line = strbuf;
		int infoWidth = 0;

		while (line && *line) {
			const char *lineEnd = strchr(line, '\n');
			size_t lineLength = lineEnd ? (size_t)(lineEnd - line) : strlen(line);
			int lineWidth = WMWidthOfString(font, line, lineLength);

			if (lineWidth > infoWidth)
				infoWidth = lineWidth;
			line = lineEnd ? lineEnd + 1 : NULL;
		}

		if (infoWidth + WMScaleX(36) > pwidth)
			pwidth = infoWidth + WMScaleX(36);
		WMResizeWidget(panel->win, pwidth, pheight);
		WMResizeWidget(panel->infoL, pwidth - WMScaleX(30), WMScaleY(105));
		WMResizeWidget(panel->copyrL, pwidth - WMScaleX(30), WMScaleY(60));
		WMSetLabelFont(panel->infoL, font);
		WMReleaseFont(font);
		font = NULL;
	}
	wfree(strbuf);

	WMRealizeWidget(panel->win);
	WMMapSubwidgets(panel->win);

	parent = XCreateSimpleWindow(dpy, scr->root_win, 0, 0, pwidth, pheight, 0, 0, 0);

	XReparentWindow(dpy, WMWidgetXID(panel->win), parent, 0, 0);

	WMMapWidget(panel->win);

	center = wGetCenter(scr, pwidth, pheight);
	wwin = wManageInternalWindow(scr, parent, None, _("Info"), center.x, center.y, pwidth, pheight);

	WSETUFLAG(wwin, no_closable, 0);
	WSETUFLAG(wwin, no_close_button, 0);
#ifdef XKB_BUTTON_HINT
	wFrameWindowHideButton(wwin->frame, WFF_LANGUAGE_BUTTON);
#endif
	wWindowUpdateButtonImages(wwin);
	wFrameWindowShowButton(wwin->frame, WFF_CLOSE_BUTTON);
	wwin->frame->on_click_button[WTB_CLOSE] = destroyInfoPanel;

	wWindowMap(wwin);

	panel->wwin = wwin;
	infoPanel = panel;
}

/*
 ***********************************************************************
 * Legal Panel
 ***********************************************************************
 */

typedef struct {
	WScreen *scr;
	WWindow *wwin;
	WMWindow *win;
	WMFrame *frame;
	WMLabel *licenseL;
} LegalPanel;

static LegalPanel *legalPanel = NULL;

static void destroyLegalPanel(WCoreWindow * foo, void *data, XEvent * event)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;
	(void) data;
	(void) event;

	WMUnmapWidget(legalPanel->win);
	WMDestroyWidget(legalPanel->win);
	wUnmanageWindow(legalPanel->wwin, False, False);
	wfree(legalPanel);
	legalPanel = NULL;
}

void wShowLegalPanel(WScreen *scr)
{
	LegalPanel *panel;
	Window parent;
	WWindow *wwin;
	WMPoint center;
	int wmScaleWidth, wmScaleHeight;
	int pwidth, pheight;

	if (legalPanel) {
		if (legalPanel->scr == scr) {
			wRaiseFrame(legalPanel->wwin->frame->core);
			wSetFocusTo(scr, legalPanel->wwin);
		}
		return;
	}

	panel = wmalloc(sizeof(LegalPanel));
	panel->scr = scr;
	panel->win = WMCreateWindow(scr->wmscreen, "legal");
	WMGetScaleBaseFromSystemFont(scr->wmscreen, &wmScaleWidth, &wmScaleHeight);
	pwidth = WMScaleX(440);
	pheight = WMScaleY(270);
	WMResizeWidget(panel->win, pwidth, pheight);

	panel->frame = WMCreateFrame(panel->win);
	WMResizeWidget(panel->frame, pwidth - (2 * WMScaleX(10)), pheight - (2 * WMScaleY(10)));
	WMMoveWidget(panel->frame, WMScaleX(10), WMScaleY(10));
	WMSetFrameTitle(panel->frame, NULL);

	panel->licenseL = WMCreateLabel(panel->frame);
	WMSetLabelWraps(panel->licenseL, True);
	WMResizeWidget(panel->licenseL, pwidth - (4 * WMScaleX(10)), pheight - (4 * WMScaleY(10)));
	WMMoveWidget(panel->licenseL, WMScaleX(8), WMScaleY(8));
	WMSetLabelTextAlignment(panel->licenseL, WALeft);
	WMSetLabelText(panel->licenseL,
		       _("    Window Maker is free software; you can redistribute it and/or "
			 "modify it under the terms of the GNU General Public License as "
			 "published by the Free Software Foundation; either version 2 of the "
			 "License, or (at your option) any later version.\n\n"
			 "    Window Maker is distributed in the hope that it will be useful, "
			 "but WITHOUT ANY WARRANTY; without even the implied warranty "
			 "of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. "
			 "See the GNU General Public License for more details.\n\n"
			 "    You should have received a copy of the GNU General Public "
			 "License along with this program; if not, see <https://www.gnu.org/licenses/>."));

	WMRealizeWidget(panel->win);
	WMMapSubwidgets(panel->win);
	WMMapSubwidgets(panel->frame);

	parent = XCreateSimpleWindow(dpy, scr->root_win, 0, 0, pwidth, pheight, 0, 0, 0);
	XReparentWindow(dpy, WMWidgetXID(panel->win), parent, 0, 0);
	center = wGetCenter(scr, pwidth, pheight);
	wwin = wManageInternalWindow(scr, parent, None, _("Legal"), center.x, center.y, pwidth, pheight);

	WSETUFLAG(wwin, no_closable, 0);
	WSETUFLAG(wwin, no_close_button, 0);
	wWindowUpdateButtonImages(wwin);
	wFrameWindowShowButton(wwin->frame, WFF_CLOSE_BUTTON);
#ifdef XKB_BUTTON_HINT
	wFrameWindowHideButton(wwin->frame, WFF_LANGUAGE_BUTTON);
#endif
	wwin->frame->on_click_button[WTB_CLOSE] = destroyLegalPanel;
	panel->wwin = wwin;

	WMMapWidget(panel->win);
	wWindowMap(wwin);
	legalPanel = panel;
}

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

	Bool done;		/* if finished with this dialog */
	int action;		/* what to do after */

	KeyCode retKey;
	KeyCode upKey;
	KeyCode downKey;

} CrashPanel;

static void handleKeyPress(XEvent * event, void *clientData)
{
	CrashPanel *panel = (CrashPanel *) clientData;

	if (event->xkey.keycode == panel->retKey) {
		WMPerformButtonClick(panel->okB);
		return;
	}

	if (event->xkey.keycode == panel->upKey) {
		WMSelectPopUpButtonPreviousItem(panel->whatP);
		return;
	}

	if (event->xkey.keycode == panel->downKey) {
		WMSelectPopUpButtonNextItem(panel->whatP);
		return;
	}
}

static void okButtonCallback(void *self, void *clientData)
{
	CrashPanel *panel = (CrashPanel *) clientData;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;

	panel->done = True;
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
	path = get_icon_filename("Logo", "WMPanel", NULL, False);

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

	screen_no = DefaultScreen(dpy);
	scr_width = WidthOfScreen(ScreenOfDisplay(dpy, screen_no));
	scr_height = HeightOfScreen(ScreenOfDisplay(dpy, screen_no));

	scr = WMCreateScreen(dpy, screen_no);
	if (!scr) {
		werror(_("cannot open connection for crashing dialog panel. Aborting."));
		return WMAbort;
	}

	panel = wmalloc(sizeof(CrashPanel));

	panel->retKey = XKeysymToKeycode(dpy, XK_Return);
	panel->upKey = XKeysymToKeycode(dpy, XK_Up);
	panel->downKey = XKeysymToKeycode(dpy, XK_Down);

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
	WMSetLabelWraps(panel->note2L, True);

	panel->whatF = WMCreateFrame(panel->win);
	WMResizeWidget(panel->whatF, PWIDTH - 20, 50);
	WMMoveWidget(panel->whatF, 10, 240);
	WMSetFrameTitle(panel->whatF, _("What do you want to do now?"));

	panel->whatP = WMCreatePopUpButton(panel->whatF);
	WMResizeWidget(panel->whatP, PWIDTH - 20 - 70, 20);
	WMMoveWidget(panel->whatP, 35, 20);
	WMSetPopUpButtonPullsDown(panel->whatP, False);
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

	WMCreateEventHandler(WMWidgetView(panel->win), KeyPressMask, handleKeyPress, panel);

	WMRealizeWidget(panel->win);
	WMMapSubwidgets(panel->win);

	WMMapWidget(panel->win);

	XSetInputFocus(dpy, WMWidgetXID(panel->win), RevertToParent, CurrentTime);

	while (!panel->done) {
		XEvent event;

		WMNextEvent(dpy, &event);
		WMHandleEvent(&event);
	}

	action = panel->action;

	WMUnmapWidget(panel->win);
	WMDestroyWidget(panel->win);
	wfree(panel);

	return action;
}
