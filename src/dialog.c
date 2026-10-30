/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 1998-2003 Dan Pascu
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

/* dialog.c - small modal dialog and info panels for internal use */

#include "wconfig.h"


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/utsname.h>

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#include <wraster.h>

#include <cairo.h>

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
#include "keybind.h"

int wMessageDialog(WScreen *scr, const char *title, const char *message, const char *defBtn, const char *altBtn, const char *othBtn)
{
	WMAlertPanel *panel;
	xcb_window_t parent;
	WWindow *wwin;
	int result;
	WMPoint center;
	int pwidth, pheight;

	wIPCEmitDialog(scr, "message", title, message);

	panel = WMCreateScaledAlertPanel(scr->wmscreen, NULL, title, message, defBtn, altBtn, othBtn);
	pwidth = WSCALE(scr, WMWidgetWidth(panel->win));
	pheight = WSCALE(scr, WMWidgetHeight(panel->win));

	parent = xcb_generate_id(wdisp->conn);
	xcb_create_window(wdisp->conn, XCB_COPY_FROM_PARENT, parent, scr->root_win, 0, 0, pwidth, pheight, 0,
			  XCB_WINDOW_CLASS_COPY_FROM_PARENT, XCB_COPY_FROM_PARENT,
			  XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL, (const uint32_t[]){ 0, 0});

	xcb_reparent_window(wdisp->conn, WMWidgetXID(panel->win), parent, 0, 0);

	center = wGetCenter(scr, pwidth, pheight);
	wwin = wManageInternalWindow(scr, parent, XCB_NONE, NULL, center.x, center.y, pwidth, pheight);
	wwin->client_leader = WMWidgetXID(panel->win);

	WMMapWidget(panel->win);

	wWindowMap(wwin);

	WMRunModalLoop(WMWidgetScreen(panel->win), WMWidgetView(panel->win));

	result = panel->result;

	WMUnmapWidget(panel->win);

	wUnmanageWindow(wwin, false, false);

	WMDestroyAlertPanel(panel);

	xcb_destroy_window(wdisp->conn, parent);

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
	xcb_window_t parent;
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
	WMAddBoxSubview(panel->hbox, WMWidgetView(saveSessionBtn), false, true, pwidth / 2, 0, 0);
	WMSetButtonText(saveSessionBtn, _("Save workspace state"));
	WMSetButtonSelected(saveSessionBtn, w_global.config->save_session_on_exit);
	WMRealizeWidget(saveSessionBtn);
	WMMapWidget(saveSessionBtn);

	pwidth = WSCALE(scr, pwidth);
	pheight = WSCALE(scr, pheight);

	parent = xcb_generate_id(wdisp->conn);
	xcb_create_window(wdisp->conn, XCB_COPY_FROM_PARENT, parent, scr->root_win, 0, 0, pwidth, pheight, 0,
			  XCB_WINDOW_CLASS_COPY_FROM_PARENT, XCB_COPY_FROM_PARENT,
			  XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL, (const uint32_t[]){ 0, 0});

	xcb_reparent_window(wdisp->conn, WMWidgetXID(panel->win), parent, 0, 0);

	center = wGetCenter(scr, pwidth, pheight);
	wwin = wManageInternalWindow(scr, parent, XCB_NONE, NULL, center.x, center.y, pwidth, pheight);

	wwin->client_leader = WMWidgetXID(panel->win);

	WMMapWidget(panel->win);

	wWindowMap(wwin);

	WMRunModalLoop(WMWidgetScreen(panel->win), WMWidgetView(panel->win));

	result = panel->result;

	WMUnmapWidget(panel->win);

	wUnmanageWindow(wwin, false, false);

	WMDestroyAlertPanel(panel);

	xcb_destroy_window(wdisp->conn, parent);

	return result;
}


int wInputDialog(WScreen *scr, const char *title, const char *message, char **text)
{
	WWindow *wwin;
	xcb_window_t parent;
	WMInputPanel *panel;
	char *result;
	WMPoint center;

	wIPCEmitDialog(scr, "input", title, message);
	int pwidth, pheight;

	panel = WMCreateScaledInputPanel(scr->wmscreen, NULL, title, message, *text, _("OK"), _("Cancel"));
	pwidth = WSCALE(scr, WMWidgetWidth(panel->win));
	pheight = WSCALE(scr, WMWidgetHeight(panel->win));

	parent = xcb_generate_id(wdisp->conn);
	xcb_create_window(wdisp->conn, XCB_COPY_FROM_PARENT, parent, scr->root_win, 0, 0, pwidth, pheight, 0,
			  XCB_WINDOW_CLASS_COPY_FROM_PARENT, XCB_COPY_FROM_PARENT,
			  XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL, (const uint32_t[]){ 0, 0});
	xcb_reparent_window(wdisp->conn, WMWidgetXID(panel->win), parent, 0, 0);

	center = wGetCenter(scr, pwidth, pheight);
	wwin = wManageInternalWindow(scr, parent, XCB_NONE, NULL, center.x, center.y, pwidth, pheight);

	wwin->client_leader = WMWidgetXID(panel->win);

	WMMapWidget(panel->win);

	wWindowMap(wwin);

	WMRunModalLoop(WMWidgetScreen(panel->win), WMWidgetView(panel->win));

	if (panel->result == WAPRDefault)
		result = WMGetTextFieldText(panel->text);
	else
		result = NULL;

	wUnmanageWindow(wwin, false, false);

	WMDestroyInputPanel(panel);

	xcb_destroy_window(wdisp->conn, parent);

	if (result == NULL)
		return false;

	if (*text)
		wfree(*text);
	*text = result;

	return true;
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

static int compareFormatNames(const void *a, const void *b)
{
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static const char *const supportNames[] = {
#ifdef USE_MAGICK
	"ImageMagick",
#endif
#ifdef USE_MWM_HINTS
	"MWM",
#endif
	"WMSPEC",
#ifdef USE_DOCK_XDND
	"XDnD",
#endif
#ifdef USE_XRES
	"XRes",
#endif
	NULL
};

static void destroyInfoPanel(WCoreWindow *foo, void *data, xcb_generic_event_t *event)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;
	(void) data;
	(void) event;

	WMUnmapWidget(infoPanel);
	wUnmanageWindow(infoPanel->wwin, false, false);
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
	xcb_window_t parent;
	WWindow *wwin;
	WMPoint center;
	char **strl;
	int i, width = 50, sepHeight;
	int formatCount;
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
#if defined(HAVE_MALLOC_H) && defined(HAVE_MALLINFO2)
	struct mallinfo2 ma;
#endif

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
	WMSetLabelWraps(panel->versionL, false);

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
		 (unsigned int)scr->w_visual->visual_id, visuals[scr->w_visual->_class], scr->w_depth);
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
	ma = mallinfo2();
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
#endif

	strbuf = wstrappend(strbuf, _("Image formats: "));
	strl = RSupportedFileFormats();
	for (formatCount = 0; strl[formatCount] != NULL; formatCount++)
		;
	qsort(strl, formatCount, sizeof(*strl), compareFormatNames);
	separator = NULL;
	for (i = 0; strl[i] != NULL; i++) {
		if (separator != NULL)
			strbuf = wstrappend(strbuf, separator);
		strbuf = wstrappend(strbuf, strl[i]);
		separator = ", ";
	}

	strbuf = wstrappend(strbuf, _("\nAdditional support for: "));
	separator = NULL;
	for (i = 0; supportNames[i] != NULL; i++) {
		if (separator != NULL)
			strbuf = wstrappend(strbuf, separator);
		strbuf = wstrappend(strbuf, supportNames[i]);
		separator = ", ";
	}
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

	pwidth = WSCALE(scr, pwidth);
	pheight = WSCALE(scr, pheight);

	parent = xcb_generate_id(wdisp->conn);
	xcb_create_window(wdisp->conn, XCB_COPY_FROM_PARENT, parent, scr->root_win, 0, 0, pwidth, pheight, 0,
			  XCB_WINDOW_CLASS_COPY_FROM_PARENT, XCB_COPY_FROM_PARENT,
			  XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL, (const uint32_t[]){ 0, 0});

	xcb_reparent_window(wdisp->conn, WMWidgetXID(panel->win), parent, 0, 0);

	WMMapWidget(panel->win);

	center = wGetCenter(scr, pwidth, pheight);
	wwin = wManageInternalWindow(scr, parent, XCB_NONE, _("Info"), center.x, center.y, pwidth, pheight);

	WSETPERWINDOWFLAG(wwin, no_closable, 0);
	WSETPERWINDOWFLAG(wwin, no_close_button, 0);
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

static void destroyLegalPanel(WCoreWindow * foo, void *data, xcb_generic_event_t * event)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) foo;
	(void) data;
	(void) event;

	WMUnmapWidget(legalPanel->win);
	WMDestroyWidget(legalPanel->win);
	wUnmanageWindow(legalPanel->wwin, false, false);
	wfree(legalPanel);
	legalPanel = NULL;
}

void wShowLegalPanel(WScreen *scr)
{
	LegalPanel *panel;
	xcb_window_t parent;
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
	WMSetLabelWraps(panel->licenseL, true);
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

	pwidth = WSCALE(scr, pwidth);
	pheight = WSCALE(scr, pheight);

	parent = xcb_generate_id(wdisp->conn);
	xcb_create_window(wdisp->conn, XCB_COPY_FROM_PARENT, parent, scr->root_win, 0, 0, pwidth, pheight, 0,
			  XCB_WINDOW_CLASS_COPY_FROM_PARENT, XCB_COPY_FROM_PARENT,
			  XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL, (const uint32_t[]){ 0, 0});
	xcb_reparent_window(wdisp->conn, WMWidgetXID(panel->win), parent, 0, 0);
	center = wGetCenter(scr, pwidth, pheight);
	wwin = wManageInternalWindow(scr, parent, XCB_NONE, _("Legal"), center.x, center.y, pwidth, pheight);

	WSETPERWINDOWFLAG(wwin, no_closable, 0);
	WSETPERWINDOWFLAG(wwin, no_close_button, 0);
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
 * Keyboard Shortcuts Panel
 ***********************************************************************
 */

typedef enum {
	SHORTCUT_HEADER,
	SHORTCUT_ROW
} ShortcutRowType;

typedef struct {
	ShortcutRowType type;
	const char *category;     /* header text */
	const WKeybindInfo *info; /* bound row */
	int stripe;               /* alternating background for rows */
} ShortcutRow;

typedef struct {
	WScreen *scr;
	WWindow *wwin;
	WMWindow *win;
	WMList *list;
	WMFont *font;
	WMFont *boldFont;
	WKeybindInfo *items;
	int count;
	ShortcutRow *rows;
	int rowCount;
	WMColor *headerBack;
	WMColor *headerFg;
	WMColor *altBack;
	WMColor *capBack;
	WMColor *capBorder;
	WMColor *textFg;
} ShortcutsPanel;

static ShortcutsPanel *shortcutsPanel;

static int compareKeybindInfo(const void *a, const void *b)
{
	const WKeybindInfo *x = a;
	const WKeybindInfo *y = b;
	int c = strcasecmp(x->category, y->category);

	if (c != 0)
		return c;

	return strcasecmp(x->description, y->description);
}

static void buildShortcutRows(ShortcutsPanel *panel)
{
	const char *lastCategory = NULL;
	int i, stripe = 0;

	if (panel->count > 1)
		qsort(panel->items, panel->count, sizeof(*panel->items),
		      compareKeybindInfo);

	panel->rows = wmalloc(sizeof(*panel->rows) * (panel->count * 2 + 1));
	panel->rowCount = 0;

	for (i = 0; i < panel->count; i++) {
		WKeybindInfo *info = &panel->items[i];

		if (!lastCategory
		    || strcmp(lastCategory, info->category) != 0) {
			panel->rows[panel->rowCount].type = SHORTCUT_HEADER;
			panel->rows[panel->rowCount].category = info->category;
			panel->rows[panel->rowCount].info = NULL;
			panel->rows[panel->rowCount].stripe = 0;
			panel->rowCount++;
			lastCategory = info->category;
			stripe = 0;
		}

		panel->rows[panel->rowCount].type = SHORTCUT_ROW;
		panel->rows[panel->rowCount].category = info->category;
		panel->rows[panel->rowCount].info = info;
		panel->rows[panel->rowCount].stripe = stripe;
		panel->rowCount++;
		stripe ^= 1;
	}
}

static void destroyShortcutsPanel(WCoreWindow *foo, void *data,
				  xcb_generic_event_t *event)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void)foo;
	(void)data;
	(void)event;

	wKeybindFree(shortcutsPanel->items, shortcutsPanel->count);
	wfree(shortcutsPanel->rows);
	if (shortcutsPanel->font)
		WMReleaseFont(shortcutsPanel->font);
	if (shortcutsPanel->boldFont)
		WMReleaseFont(shortcutsPanel->boldFont);
	if (shortcutsPanel->headerBack)
		WMReleaseColor(shortcutsPanel->headerBack);
	if (shortcutsPanel->headerFg)
		WMReleaseColor(shortcutsPanel->headerFg);
	if (shortcutsPanel->altBack)
		WMReleaseColor(shortcutsPanel->altBack);
	if (shortcutsPanel->capBack)
		WMReleaseColor(shortcutsPanel->capBack);
	if (shortcutsPanel->capBorder)
		WMReleaseColor(shortcutsPanel->capBorder);
	if (shortcutsPanel->textFg)
		WMReleaseColor(shortcutsPanel->textFg);
	WMUnmapWidget(shortcutsPanel->win);
	wUnmanageWindow(shortcutsPanel->wwin, false, false);
	WMDestroyWidget(shortcutsPanel->win);
	wfree(shortcutsPanel);
	shortcutsPanel = NULL;
}

static void drawShortcutRow(WMList *lPtr, int index, xcb_drawable_t d,
			    char *text, int state, WMRect *rect)
{
	ShortcutsPanel *panel = WMGetHangedData(lPtr);
	W_Screen *screen = W_VIEW(lPtr)->screen;
	ShortcutRow *row;
	WMColor *back, *fg;
	cairo_t *cr;
	int textRight = 0;

	(void)text;

	if (!panel || index < 0 || index >= panel->rowCount)
		return;

	row = &panel->rows[index];

	if (row->type == SHORTCUT_HEADER) {
		back = panel->headerBack;
		fg = panel->headerFg;
	} else if (state & WLDSSelected) {
		back = WMGetListSelectionColor(lPtr);
		fg = panel->textFg;
	} else {
		back = row->stripe ? panel->altBack
			: WMGetWidgetBackgroundColor(lPtr);
		fg = panel->textFg;
	}

	cr = WCairoBeginForDrawable(screen, d);
	if (!cr)
		return;

	W_SetSourceColor(cr, back);
	cairo_rectangle(cr, 0, 0, rect->size.width, rect->size.height);
	cairo_fill(cr);

	if (row->type == SHORTCUT_HEADER) {
		if (panel->boldFont) {
			W_PaintText(cr, panel->boldFont, 8,
				    (rect->size.height - WMFontHeight(panel->boldFont)) / 2,
				    rect->size.width - 16, WALeft, fg, false,
				    row->category, strlen(row->category));
		}
	} else if (panel->font) {
		const WKeybindInfo *info = row->info;
		int y = (rect->size.height - WMFontHeight(panel->font)) / 2;

		textRight = rect->size.width - 8;

		if (info->label) {
			int len = strlen(info->label);
			int capW = WMWidthOfString(panel->font, info->label, len) + 12;
			int capH = rect->size.height - 6;
			int capY = 3;
			int capX = rect->size.width - capW - 8;

			W_SetSourceColor(cr, panel->capBorder);
			cairo_rectangle(cr, capX, capY, capW, capH);
			cairo_fill(cr);
			W_SetSourceColor(cr, panel->capBack);
			cairo_rectangle(cr, capX + 1, capY + 1, capW - 2, capH - 2);
			cairo_fill(cr);

			W_PaintText(cr, panel->font, capX + 6,
				    capY + (capH - WMFontHeight(panel->font)) / 2,
				    capW - 12, WACenter, fg, false,
				    info->label, len);

			textRight = capX - 8;
		}

		if (textRight > 8)
			W_PaintText(cr, panel->font, 8, y, textRight - 8,
				    WALeft, fg, false, info->description,
				    strlen(info->description));
	}

	WCairoEnd(cr);
}

void wShowShortcutsPanel(WScreen *scr)
{
	ShortcutsPanel *panel;
	xcb_window_t parent;
	WWindow *wwin;
	WMPoint center;
	int wmScaleWidth, wmScaleHeight;
	int pwidth, pheight;
	int i;

	if (shortcutsPanel) {
		if (shortcutsPanel->scr == scr) {
			wRaiseFrame(shortcutsPanel->wwin->frame->core);
			wSetFocusTo(scr, shortcutsPanel->wwin);
		}
		return;
	}

	panel = wmalloc(sizeof(ShortcutsPanel));
	panel->scr = scr;
	panel->win = WMCreateWindow(scr->wmscreen, "shortcuts");
	WMGetScaleBaseFromSystemFont(scr->wmscreen, &wmScaleWidth, &wmScaleHeight);
	pwidth = WMScaleX(560);
	pheight = WMScaleY(460);
	WMResizeWidget(panel->win, pwidth, pheight);

	panel->font = WMSystemFontOfSize(scr->wmscreen, WMScaleY(11));
	panel->boldFont = WMBoldSystemFontOfSize(scr->wmscreen, WMScaleY(11));

	panel->headerBack = WMCreateRGBColor(scr->wmscreen, 0x3333, 0x3333, 0x3333, false);
	panel->headerFg = WMWhiteColor(scr->wmscreen);
	panel->altBack = WMCreateRGBColor(scr->wmscreen, 0xf2f2, 0xf2f2, 0xf2f2, false);
	panel->capBack = WMCreateRGBColor(scr->wmscreen, 0xe6e6, 0xe6e6, 0xe6e6, false);
	panel->capBorder = WMCreateRGBColor(scr->wmscreen, 0x9999, 0x9999, 0x9999, false);
	panel->textFg = WMBlackColor(scr->wmscreen);

	panel->list = WMCreateList(panel->win);
	WMMoveWidget(panel->list, WMScaleX(10), WMScaleY(8));
	WMResizeWidget(panel->list, pwidth - (2 * WMScaleX(10)),
		       pheight - WMScaleY(16));
	WMSetListAllowMultipleSelection(panel->list, false);
	WMSetListAllowEmptySelection(panel->list, true);
	WMSetListUserDrawProc(panel->list, drawShortcutRow);
	if (panel->font)
		WMSetListUserDrawItemHeight(panel->list,
					    WMFontHeight(panel->font) + WMScaleY(6));
	WMHangData(panel->list, panel);

	panel->items = wKeybindCollect(0, &panel->count);
	buildShortcutRows(panel);
	for (i = 0; i < panel->rowCount; i++)
		WMAddListItem(panel->list,
			      panel->rows[i].type == SHORTCUT_HEADER
			      ? panel->rows[i].category
			      : panel->rows[i].info->action);

	WMRealizeWidget(panel->win);
	WMMapSubwidgets(panel->win);

	pwidth = WSCALE(scr, pwidth);
	pheight = WSCALE(scr, pheight);

	parent = xcb_generate_id(wdisp->conn);
	xcb_create_window(wdisp->conn, XCB_COPY_FROM_PARENT, parent, scr->root_win, 0, 0, pwidth, pheight, 0,
			  XCB_WINDOW_CLASS_COPY_FROM_PARENT, XCB_COPY_FROM_PARENT,
			  XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL, (const uint32_t[]){ 0, 0});
	xcb_reparent_window(wdisp->conn, WMWidgetXID(panel->win), parent, 0, 0);
	WMMapWidget(panel->win);

	center = wGetCenter(scr, pwidth, pheight);
	wwin = wManageInternalWindow(scr, parent, XCB_NONE, _("Keyboard Shortcuts"),
				     center.x, center.y, pwidth, pheight);

	WSETPERWINDOWFLAG(wwin, no_closable, 0);
	WSETPERWINDOWFLAG(wwin, no_close_button, 0);
#ifdef XKB_BUTTON_HINT
	wFrameWindowHideButton(wwin->frame, WFF_LANGUAGE_BUTTON);
#endif
	wWindowUpdateButtonImages(wwin);
	wFrameWindowShowButton(wwin->frame, WFF_CLOSE_BUTTON);
	wwin->frame->on_click_button[WTB_CLOSE] = destroyShortcutsPanel;

	wWindowMap(wwin);

	panel->wwin = wwin;
	shortcutsPanel = panel;
}
