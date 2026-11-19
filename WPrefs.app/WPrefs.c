/* WPrefs.c- main window and other basic stuff
 *
 *  WPrefs - Window Maker Preferences Program
 *
 *  Copyright (c) 1998-2003 Alfredo K. Kojima
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

#include "config.h"

#include "WPrefs.h"
#include <assert.h>
#include <unistd.h>
#include <WINGs/WXProp.h>

#ifdef HAVE_STDNORETURN
#include <stdnoreturn.h>
#endif


#define MAX_SECTIONS 32

typedef struct _WPrefs {
	WMScreen *scr;
	WMWindow *win;

	WMScrollView *scrollV;
	WMFrame *buttonF;
	WMButton *sectionB[MAX_SECTIONS];
	void *panels[MAX_SECTIONS];

	int sectionCount;

	WMButton *saveBtn;
	WMButton *closeBtn;
	WMButton *undoBtn;
	WMButton *undosBtn;
	WMButton *resetBtn;

	WMButton *balloonBtn;

	WMFrame *banner;
	WMLabel *nameL;
	WMLabel *versionL;
	WMLabel *statusL;

	Panel *currentPanel;
} _WPrefs;

static _WPrefs WPrefs;
static bool shuttingDown;

/* system wide defaults dictionary. Read-only */
static WMPropList *GlobalDB = NULL;
/* user defaults dictionary */
static WMPropList *WindowMakerDB = NULL;
static char *WindowMakerDBPath = NULL;
/* user WMSound domain dictionary, edited by the Sound panel. NULL until
 * either loaded from disk or created by the first SetSoundObjectForKey. */
static WMPropList *WMSoundDB;
static char *WMSoundDBPath;
/* installed global WMSound domain. Read-only */
static WMPropList *WMSoundGlobalDB;

static bool TIFFOK = false;

#define INITIALIZED_PANEL	(1<<0)

static void loadConfigurations(WMScreen * scr, WMWindow * mainw);
static void loadSoundDomain(WMScreen *scr, WMWindow *mainw);

static bool savePanelData(Panel *panel);

static void prepareForClose(void);

static void resetDefaults(WMWidget *w, void *data);

static noreturn void quit(WMWidget *w, void *data)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) w;
	(void) data;

	WPrefsShutdown();
	exit(0);
}

static WMPropList *effectiveValue(WMPropList *dict, WMPropList *key)
{
	WMPropList *v = dict ? WMGetFromPLDictionary(dict, key) : NULL;

	if (!v && GlobalDB)
		v = WMGetFromPLDictionary(GlobalDB, key);
	return v;
}

/* Panels blindly re-write every key they own on save, so diff the snapshot
 * taken before storeData() against the current dictionary and report only
 * keys the schema marks WCFG_STARTUP_ONLY whose effective value actually
 * changed. Fall back to GlobalDB on both sides so a key the compress step
 * dropped (because it matched the global default) does not read as a fresh
 * addition on the next save. */
static bool changedStartupOnlyKey(WMPropList *before, WMPropList *after)
{
	WMPropList *keys, *key, *v1, *v2;
	int i, n, side;
	bool hit = false;

	for (side = 0; side < 2 && !hit; side++) {
		WMPropList *dict = side ? before : after;

		if (!dict)
			continue;
		keys = WMGetPLDictionaryKeys(dict);
		n = WMGetPropListItemCount(keys);
		for (i = 0; i < n && !hit; i++) {
			key = WMGetFromPLArray(keys, i);
			if (!wConfigSchemaRequiresRestart(WMGetFromPLString(key)))
				continue;
			v1 = effectiveValue(after, key);
			v2 = effectiveValue(before, key);
			if (v1 == v2)
				continue;
			if (!v1 || !v2 || !WMIsPropListEqualTo(v1, v2))
				hit = true;
		}
		WMReleasePropList(keys);
	}
	return hit;
}

static void reportSaveError(WMScreen *scr, const char *path)
{
	char *msg;

	if (path)
		msg = wstrconcat(_("Could not save settings to "), path);
	else
		msg = wstrdup(_("Could not save settings."));

	WMRunAlertPanel(scr, WPrefs.win, _("Error"), msg, _("OK"), NULL, NULL);
	wfree(msg);
}

static void save(WMWidget * w, void *data)
{
	int i, res;
	bool saved = true;
	WMPropList *p1, *p2;
	WMPropList *keyList;
	WMPropList *key;
	WMPropList *reply;
	WMPropList *snapshot;
	const char *verb = "reconfigure";

	/* Parameter not used, but tell the compiler that it is ok */
	(void) data;

	snapshot = WindowMakerDB ? WMDeepCopyPropList(WindowMakerDB) : NULL;

	/*    puts("gathering data"); */
	for (i = 0; i < WPrefs.sectionCount; i++) {
		PanelRec *rec = WMGetHangedData(WPrefs.sectionB[i]);
		if ((rec->callbacks.flags & INITIALIZED_PANEL) && !savePanelData((Panel *) rec))
			saved = false;
	}

	if (!saved) {
		if (snapshot)
			WMReleasePropList(snapshot);
		reportSaveError(WMWidgetScreen(w), NULL);
		return;
	}

	/* Detect edits to keys the window manager freezes for the session,
	 * before the compress step below drops keys that match the global
	 * defaults. Offer a restart so the change is not a silent no-op. */
	if (changedStartupOnlyKey(snapshot, WindowMakerDB)) {
		res = WMRunAlertPanel(WMWidgetScreen(w), WPrefs.win,
				      _("Restart Required"),
				      _("One or more options you changed only take\n"
					"effect after Window Maker is restarted."),
				      _("Restart Now"), _("Later"), NULL);
		if (res == WAPRDefault)
			verb = "restart";
	}
	if (snapshot)
		WMReleasePropList(snapshot);

	/*    puts("compressing data"); */
	/* compare the user dictionary with the global and remove redundant data */
	keyList = WMGetPLDictionaryKeys(GlobalDB);
	/*    puts(WMGetPropListDescription(WindowMakerDB, False)); */
	for (i = 0; i < WMGetPropListItemCount(keyList); i++) {
		key = WMGetFromPLArray(keyList, i);

		/* We don't have this value anyway, so no problem.
		 * Probably  a new option */
		p1 = WMGetFromPLDictionary(WindowMakerDB, key);
		if (!p1)
			continue;
		/* The global doesn't have it, so no problem either. */
		p2 = WMGetFromPLDictionary(GlobalDB, key);
		if (!p2)
			continue;
		/* If both values are the same, don't save. */
		if (WMIsPropListEqualTo(p1, p2))
			WMRemoveFromPLDictionary(WindowMakerDB, key);
	}
	/*    puts(WMGetPropListDescription(WindowMakerDB, False)); */
	WMReleasePropList(keyList);
	/*    puts("storing data"); */

	if (!WMWritePropListToFile(WindowMakerDB, WindowMakerDBPath)) {
		reportSaveError(WMWidgetScreen(w), WindowMakerDBPath);
		return;
	}

	/* The Sound panel edits a separate domain; persist it alongside the
	 * main one so the wmsoundd daemon (inotify-watching the file) reloads. */
	if (WMSoundDB && WMSoundDBPath
	    && !WMWritePropListToFile(WMSoundDB, WMSoundDBPath)) {
		reportSaveError(WMWidgetScreen(w), WMSoundDBPath);
		return;
	}

	reply = wIPCCommand(NULL, verb);
	if (reply) {
		WMReleasePropList(reply);
		return;
	}

	/* fall back to the legacy X ClientMessage when no IPC socket is
	 * reachable, e.g. talking to an older Window Maker; that channel
	 * only accepts Reconfigure, so a restart request degrades to the
	 * live-apply subset and the frozen keys wait for a manual restart */
	WXSendWmakerCommandXC(WMScreenDisplay(WMWidgetScreen(w)), WMWidgetScreen(w)->screen, "Reconfigure");
}

static void undo(WMWidget * w, void *data)
{
	PanelRec *rec = (PanelRec *) WPrefs.currentPanel;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) w;
	(void) data;

	if (!rec)
		return;

	if (rec->callbacks.undoChanges && (rec->callbacks.flags & INITIALIZED_PANEL)) {
		(*rec->callbacks.undoChanges) (WPrefs.currentPanel);
	}
}

static void undoAll(WMWidget * w, void *data)
{
	int i;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) w;
	(void) data;

	for (i = 0; i < WPrefs.sectionCount; i++) {
		PanelRec *rec = WMGetHangedData(WPrefs.sectionB[i]);

		if (rec->callbacks.undoChanges && (rec->callbacks.flags & INITIALIZED_PANEL))
			(*rec->callbacks.undoChanges) ((Panel *) rec);
	}
}

/* Copy `path' to a `<path>.wprefs-backup' sibling so a reset is
 * recoverable by hand. Returns true when the backup is in place or
 * there was nothing to back up. */
static bool backupUserFile(const char *path)
{
	char *bpath;
	FILE *src, *dst;
	char buf[4096];
	size_t n;
	bool ok = true;

	if (!path || access(path, F_OK) != 0)
		return true;

	bpath = wstrconcat(path, ".wprefs-backup");

	src = fopen(path, "rb");
	dst = src ? fopen(bpath, "wb") : NULL;
	if (!src || !dst) {
		werror(_("could not back up %s as %s"), path, bpath);
		ok = false;
	}

	while (ok && src && (n = fread(buf, 1, sizeof(buf), src)) > 0)
		if (fwrite(buf, 1, n, dst) != n)
			ok = false;

	if (ok && src && ferror(src))
		ok = false;

	if (dst && fclose(dst) != 0)
		ok = false;
	if (src)
		fclose(src);

	wfree(bpath);
	return ok;
}

/* Empty the WMGLOBAL domain both on disk and in WPrefs' cached
 * WMUserDefaults, so later edits do not resurrect old keys. */
static void clearWMGlobalDomain(char *path)
{
	WMUserDefaults *db;
	WMPropList *keys;
	int i, n;

	if (!path)
		return;

	db = WMGetDefaultsFromPath(path);
	keys = WMGetUDKeys(db);
	if (keys) {
		n = WMGetPropListItemCount(keys);
		for (i = 0; i < n; i++)
			WMRemoveUDObjectForKey(db, WMGetFromPLString(WMGetFromPLArray(keys, i)));
		WMReleasePropList(keys);
	}
	WMSaveUserDefaults(db);
	wfree(path);
}

static void resetDefaults(WMWidget *w, void *data)
{
	WMScreen *scr = WMWidgetScreen(w);
	WMPropList *reply;
	char *wmglobalPath, *rootmenuPath;
	int res;

	(void) data;

	res = WMRunAlertPanel(scr, WPrefs.win, _("Reset Defaults"),
			      _("This will discard the settings WPrefs manages\n"
				"(WindowMaker, WMSound, WMGLOBAL and the Applications\n"
				"Menu) and restore the shipped defaults. Session, dock\n"
				"and per-window attributes are not touched.\n\n"
				"Each replaced file is kept as a .wprefs-backup sibling."),
			      _("Reset"), _("Cancel"), NULL);
	if (res != WAPRDefault)
		return;

	wmglobalPath = wdefaultspathfordomain("WMGLOBAL");
	rootmenuPath = wdefaultspathfordomain("WMRootMenu");

	/* back up every user file before touching any of them, so a
	 * failed backup leaves the originals untouched */
	if (!backupUserFile(WindowMakerDBPath)
	    || !backupUserFile(WMSoundDBPath)
	    || !backupUserFile(wmglobalPath)
	    || !backupUserFile(rootmenuPath)) {
		WMRunAlertPanel(scr, WPrefs.win, _("Error"),
				_("Could not back up the current settings.\n"
				  "Nothing was changed."), _("OK"), NULL, NULL);
		wfree(wmglobalPath);
		wfree(rootmenuPath);
		return;
	}

	/* WindowMaker domain: replaced by an empty dictionary */
	WMReleasePropList(WindowMakerDB);
	WindowMakerDB = WMCreatePLDictionary(NULL, NULL);

	/* WMSound domain: dropped and removed, recreated on the next save */
	if (WMSoundDB) {
		WMReleasePropList(WMSoundDB);
		WMSoundDB = NULL;
	}
	if (WMSoundDBPath)
		unlink(WMSoundDBPath);

	/* WMGLOBAL: emptied */
	clearWMGlobalDomain(wmglobalPath);

	/* WMRootMenu: removed so wmaker falls back to the shipped plmenu */
	if (rootmenuPath)
		unlink(rootmenuPath);
	wfree(rootmenuPath);

	if (!WMWritePropListToFile(WindowMakerDB, WindowMakerDBPath)) {
		reportSaveError(scr, WindowMakerDBPath);
		return;
	}

	/* every initialized panel re-reads the cleared dictionaries */
	undoAll(NULL, NULL);

	reply = wIPCCommand(NULL, "restart");
	if (reply) {
		WMReleasePropList(reply);
		return;
	}

	/* fall back to the legacy X ClientMessage when no IPC socket is
	 * reachable, that channel only accepts Reconfigure */
	WXSendWmakerCommandXC(WMScreenDisplay(scr), scr->screen, "Reconfigure");
}

static void prepareForClose(void)
{
	int i;

	for (i = 0; i < WPrefs.sectionCount; i++) {
		PanelRec *rec = WMGetHangedData(WPrefs.sectionB[i]);

		if (rec->callbacks.prepareForClose && (rec->callbacks.flags & INITIALIZED_PANEL))
			(*rec->callbacks.prepareForClose) ((Panel *) rec);
	}
}

void WPrefsShutdown(void)
{
	WMScreen *scr = WPrefs.scr;
	xcb_connection_t *xc = scr ? WMScreenDisplay(scr) : NULL;
	int i;

	if (shuttingDown)
		return;
	shuttingDown = true;

	prepareForClose();

	WMShutdownScreen(scr);

	for (i = 0; i < WPrefs.sectionCount; i++) {
		wfree(WPrefs.panels[i]);
		WPrefs.panels[i] = NULL;
	}

	if (WindowMakerDB) {
		WMReleasePropList(WindowMakerDB);
		WindowMakerDB = NULL;
	}
	if (GlobalDB) {
		WMReleasePropList(GlobalDB);
		GlobalDB = NULL;
	}
	if (WMSoundDB) {
		WMReleasePropList(WMSoundDB);
		WMSoundDB = NULL;
	}
	if (WMSoundGlobalDB) {
		WMReleasePropList(WMSoundGlobalDB);
		WMSoundGlobalDB = NULL;
	}
	if (WindowMakerDBPath) {
		wfree(WindowMakerDBPath);
		WindowMakerDBPath = NULL;
	}
	if (WMSoundDBPath) {
		wfree(WMSoundDBPath);
		WMSoundDBPath = NULL;
	}

	WMShutdownFonts();
	WMReleaseApplication();
	RShutdown();
	wutil_shutdown();

	if (xc)
		xcb_disconnect(xc);
}

static void toggleBalloons(WMWidget *w, void *data)
{
	WMUserDefaults *udb = WMGetStandardUserDefaults();
	bool flag;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) w;
	(void) data;

	flag = WMGetButtonSelected(WPrefs.balloonBtn);

	WMSetBalloonEnabled(WMWidgetScreen(WPrefs.win), flag);

	WMSetUDBoolForKey(udb, flag, "BalloonHelp");
}

static void createMainWindow(WMScreen * scr)
{
	WMScroller *scroller;
	WMFont *font;
	char buffer[128];
	WMUserDefaults *udb;
	bool flag;

	WPrefs.win = WMCreateWindow(scr, "wprefs");
	WMResizeWidget(WPrefs.win, 520, 390);
	WMSetWindowTitle(WPrefs.win, _("Window Maker Preferences"));
	WMSetWindowCloseAction(WPrefs.win, quit, NULL);
	WMSetWindowMaxSize(WPrefs.win, 520, 390);
	WMSetWindowMinSize(WPrefs.win, 520, 390);
	WMSetWindowMiniwindowTitle(WPrefs.win, _("Preferences"));

	WPrefs.scrollV = WMCreateScrollView(WPrefs.win);
	WMResizeWidget(WPrefs.scrollV, 500, 87);
	WMMoveWidget(WPrefs.scrollV, 10, 10);
	WMSetScrollViewRelief(WPrefs.scrollV, WRSunken);
	WMSetScrollViewHasHorizontalScroller(WPrefs.scrollV, true);
	WMSetScrollViewHasVerticalScroller(WPrefs.scrollV, false);
	scroller = WMGetScrollViewHorizontalScroller(WPrefs.scrollV);
	WMSetScrollerArrowsPosition(scroller, WSANone);

	WPrefs.buttonF = WMCreateFrame(WPrefs.win);
	WMSetFrameRelief(WPrefs.buttonF, WRFlat);

	WMSetScrollViewContentView(WPrefs.scrollV, WMWidgetView(WPrefs.buttonF));

	WPrefs.undosBtn = WMCreateCommandButton(WPrefs.win);
	WMResizeWidget(WPrefs.undosBtn, 90, 28);
	WMMoveWidget(WPrefs.undosBtn, 135, 350);
	WMSetButtonText(WPrefs.undosBtn, _("Revert Page"));
	WMSetButtonAction(WPrefs.undosBtn, undo, NULL);

	WPrefs.undoBtn = WMCreateCommandButton(WPrefs.win);
	WMResizeWidget(WPrefs.undoBtn, 90, 28);
	WMMoveWidget(WPrefs.undoBtn, 235, 350);
	WMSetButtonText(WPrefs.undoBtn, _("Revert All"));
	WMSetButtonAction(WPrefs.undoBtn, undoAll, NULL);

	/* sits in the dead slot between the balloon-help switch and Save
	 * and stays mapped even on the welcome screen */
	WPrefs.resetBtn = WMCreateCommandButton(WPrefs.win);
	WMResizeWidget(WPrefs.resetBtn, 90, 28);
	WMMoveWidget(WPrefs.resetBtn, 219, 350);
	WMSetButtonText(WPrefs.resetBtn, _("Reset Defaults"));
	WMSetButtonAction(WPrefs.resetBtn, resetDefaults, NULL);

	WPrefs.saveBtn = WMCreateCommandButton(WPrefs.win);
	WMResizeWidget(WPrefs.saveBtn, 80, 28);
	WMMoveWidget(WPrefs.saveBtn, 335, 350);
	WMSetButtonText(WPrefs.saveBtn, _("Save"));
	WMSetButtonAction(WPrefs.saveBtn, save, NULL);

	WPrefs.closeBtn = WMCreateCommandButton(WPrefs.win);
	WMResizeWidget(WPrefs.closeBtn, 80, 28);
	WMMoveWidget(WPrefs.closeBtn, 425, 350);
	WMSetButtonText(WPrefs.closeBtn, _("Close"));
	WMSetButtonAction(WPrefs.closeBtn, quit, NULL);

	WPrefs.balloonBtn = WMCreateSwitchButton(WPrefs.win);
	WMResizeWidget(WPrefs.balloonBtn, 200, 28);
	WMMoveWidget(WPrefs.balloonBtn, 15, 350);
	WMSetButtonText(WPrefs.balloonBtn, _("Balloon Help"));
	WMSetButtonAction(WPrefs.balloonBtn, toggleBalloons, NULL);

	udb = WMGetStandardUserDefaults();
	flag = WMGetUDBoolForKey(udb, "BalloonHelp");
	WMSetButtonSelected(WPrefs.balloonBtn, flag);
	WMSetBalloonEnabled(scr, flag);

	/* banner */
	WPrefs.banner = WMCreateFrame(WPrefs.win);
	WMResizeWidget(WPrefs.banner, FRAME_WIDTH, FRAME_HEIGHT);
	WMMoveWidget(WPrefs.banner, FRAME_LEFT, FRAME_TOP);
	WMSetFrameRelief(WPrefs.banner, WRFlat);

	font = WMCreateFont(scr, "Lucida Sans,URW Gothic L,Times New Roman,serif"
			    ":bold:pixelsize=26:antialias=true");
	WPrefs.nameL = WMCreateLabel(WPrefs.banner);
	WMSetLabelTextAlignment(WPrefs.nameL, WACenter);
	WMResizeWidget(WPrefs.nameL, FRAME_WIDTH - 20, 60);
	WMMoveWidget(WPrefs.nameL, 10, 50);
	WMSetLabelFont(WPrefs.nameL, font);
	WMSetLabelText(WPrefs.nameL, _("Window Maker Preferences"));
	WMReleaseFont(font);

	WPrefs.versionL = WMCreateLabel(WPrefs.banner);
	WMResizeWidget(WPrefs.versionL, FRAME_WIDTH - 20, 20);
	WMMoveWidget(WPrefs.versionL, 10, 120);
	WMSetLabelTextAlignment(WPrefs.versionL, WACenter);
	snprintf(buffer, sizeof(buffer), _("Version %s"), VERSION);
	WMSetLabelText(WPrefs.versionL, buffer);

	WPrefs.statusL = WMCreateLabel(WPrefs.banner);
	WMResizeWidget(WPrefs.statusL, FRAME_WIDTH - 20, 60);
	WMMoveWidget(WPrefs.statusL, 10, 150);
	WMSetLabelTextAlignment(WPrefs.statusL, WACenter);
	WMSetLabelText(WPrefs.statusL, _("Starting..."));

	WMMapSubwidgets(WPrefs.win);

	WMUnmapWidget(WPrefs.undosBtn);
	WMUnmapWidget(WPrefs.undoBtn);
	WMUnmapWidget(WPrefs.saveBtn);
}

static void showPanel(Panel * panel)
{
	PanelRec *rec = (PanelRec *) panel;

	if (!(rec->callbacks.flags & INITIALIZED_PANEL)) {
		(*rec->callbacks.createWidgets) (panel);
		rec->callbacks.flags |= INITIALIZED_PANEL;
	}

	WMSetWindowTitle(WPrefs.win, rec->sectionName);

	if (rec->callbacks.showPanel)
		(*rec->callbacks.showPanel) (panel);

	WMMapWidget(rec->box);
}

static void hidePanel(Panel * panel)
{
	PanelRec *rec = (PanelRec *) panel;

	WMUnmapWidget(rec->box);

	if (rec->callbacks.hidePanel)
		(*rec->callbacks.hidePanel) (panel);
}

static bool savePanelData(Panel *panel)
{
	PanelRec *rec = (PanelRec *) panel;

	if (rec->callbacks.updateDomain)
		return (*rec->callbacks.updateDomain) (panel);

	return true;
}

static void changeSection(WMWidget * self, void *data)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) self;

	if (WPrefs.currentPanel == data)
		return;

	if (WPrefs.currentPanel == NULL) {
		WMDestroyWidget(WPrefs.nameL);
		WMDestroyWidget(WPrefs.versionL);
		WMDestroyWidget(WPrefs.statusL);

		WMSetFrameRelief(WPrefs.banner, WRGroove);

		/*      WMMapWidget(WPrefs.undosBtn);
		   WMMapWidget(WPrefs.undoBtn);
		 */
		WMMapWidget(WPrefs.saveBtn);
	}

	showPanel(data);

	if (WPrefs.currentPanel)
		hidePanel(WPrefs.currentPanel);
	WPrefs.currentPanel = data;
}

char *LocateImage(const char *name)
{
	char *path;
	char *tmp = wmalloc(strlen(name) + 8);

	if (TIFFOK) {
		snprintf(tmp, strlen(name) + 8, "%s.tiff", name);
		path = WMPathForResourceOfType(tmp, "tiff");
	} else {
		snprintf(tmp, strlen(name) + 8, "%s.xpm", name);
		path = WMPathForResourceOfType(tmp, "xpm");
	}
	wfree(tmp);
	if (!path) {
		wwarning(_("could not locate image file %s"), name);
	}

	return path;
}

void CreateImages(WMScreen *scr, RContext *rc, RImage *xis, const char *file,
		WMPixmap **icon_normal, WMPixmap **icon_greyed)
{
	RImage *icon;
	char *path;
	RColor gray = { 0xae, 0xaa, 0xae, 0 };

	path = LocateImage(file);
	if (!path)
	{
		*icon_normal = NULL;
		if (icon_greyed)
			*icon_greyed = NULL;
		return;
	}

	*icon_normal = WMCreatePixmapFromFile(scr, path);
	if (!*icon_normal)
	{
		wwarning(_("could not load icon %s"), path);
		if (icon_greyed)
			*icon_greyed = NULL;
		wfree(path);
		return;
	}

	if (!icon_greyed) // Greyed-out version not requested, we are done
	{
		wfree(path);
		return;
	}

	icon = RLoadImage(rc, path, 0);
	if (!icon)
	{
		wwarning(_("could not load icon %s"), path);
		*icon_greyed = NULL;
		wfree(path);
		return;
	}
	RCombineImageWithColor(icon, &gray);
	if (xis)
	{
		RCombineImagesWithOpaqueness(icon, xis, 180);
	}
	*icon_greyed = WMCreatePixmapFromRImage(scr, icon, 127);
	if (!*icon_greyed)
	{
		wwarning(_("could not process icon %s: %s"), path, RMessageForError(RErrorCode));
	}
	RReleaseImage(icon);
	wfree(path);
}


void SetButtonImageFromFile(WMScreen *scr, WMButton *btn, const char *file, bool alt)
{
	WMPixmap *icon;
	char *path;

	path = LocateImage(file);
	if (!path)
		return;

	icon = WMCreatePixmapFromFile(scr, path);
	if (icon) {
		if (alt)
			WMSetButtonAltImage(btn, icon);
		else
			WMSetButtonImage(btn, icon);
		WMReleasePixmap(icon);
	} else {
		wwarning(_("could not load icon file %s"), path);
	}
	wfree(path);
}


void SetButtonAlphaImage(WMScreen *scr, WMButton *bPtr, const char *file)
{
	WMPixmap *icon;
	RColor color;
	char *iconPath;

	iconPath = LocateImage(file);

	color.red = 0xae;
	color.green = 0xaa;
	color.blue = 0xae;
	color.alpha = 0;
	if (iconPath) {
		icon = WMCreateBlendedPixmapFromFile(scr, iconPath, &color);
		if (!icon)
			wwarning(_("could not load icon file %s"), iconPath);
	} else {
		icon = NULL;
	}

	WMSetButtonImage(bPtr, icon);
	if (icon)
		WMReleasePixmap(icon);

	color.red = 0xff;
	color.green = 0xff;
	color.blue = 0xff;
	color.alpha = 0;
	if (iconPath) {
		icon = WMCreateBlendedPixmapFromFile(scr, iconPath, &color);
		if (!icon)
			wwarning(_("could not load icon file %s"), iconPath);
	} else {
		icon = NULL;
	}

	WMSetButtonAltImage(bPtr, icon);

	if (icon)
		WMReleasePixmap(icon);

	if (iconPath)
		wfree(iconPath);
}

static WMPixmap *createSectionIcon(WMScreen *scr, const char *file)
{
	RImage *image, *card;
	WMPixmap *pix;
	RColor page = { 0xa0, 0x9c, 0xa0, 255 };
	unsigned char *p;
	char *path;
	int x, y;

	path = LocateImage(file);
	if (!path)
		return NULL;

	image = RLoadImage(WMScreenRContext(scr), path, 0);
	if (!image) {
		wwarning(_("could not load icon file %s"), path);
		wfree(path);
		return NULL;
	}

	card = RCreateImage(image->width, image->height, 0);
	if (!card) {
		RReleaseImage(image);
		wfree(path);
		return NULL;
	}
	RClearImage(card, &page);
	for (x = 0; x < image->width - 1; x++) {
		p = card->data + 3 * x;
		p[0] = p[1] = p[2] = 0x2c;
	}
	for (y = 0; y < image->height - 1; y++) {
		p = card->data + 3 * y * image->width;
		p[0] = p[1] = p[2] = 0x2c;
	}
	for (y = 0; y < image->height; y++) {
		p = card->data + 3 * (y * image->width + image->width - 1);
		p[0] = p[1] = p[2] = 0xfe;
	}
	for (x = 0; x < image->width; x++) {
		p = card->data + 3 * ((image->height - 1) * image->width + x);
		p[0] = p[1] = p[2] = 0xfe;
	}
	RCombineImages(card, image);
	pix = WMCreatePixmapFromRImage(scr, card, 0);
	RReleaseImage(card);
	RReleaseImage(image);
	wfree(path);
	return pix;
}

void AddSection(Panel * panel, const char *iconFile)
{
	WMButton *bPtr;
	WMPixmap *icon;

	assert(WPrefs.sectionCount < MAX_SECTIONS);

	bPtr = WMCreateCustomButton(WPrefs.buttonF, WBBStateLightMask | WBBStateChangeMask);
	WMResizeWidget(bPtr, 64, 64);
	WMMoveWidget(bPtr, WPrefs.sectionCount * 64, 0);
	WMSetButtonImagePosition(bPtr, WIPImageOnly);
	WMSetButtonAction(bPtr, changeSection, panel);
	WMHangData(bPtr, panel);

	WMSetBalloonTextForView(((PanelRec *) panel)->description, WMWidgetView(bPtr));
	icon = createSectionIcon(WMWidgetScreen(bPtr), iconFile);
	WMSetButtonImage(bPtr, icon);
	if (icon)
		WMReleasePixmap(icon);
	WMMapWidget(bPtr);

	WPrefs.sectionB[WPrefs.sectionCount] = bPtr;
	WPrefs.panels[WPrefs.sectionCount] = panel;

	if (WPrefs.sectionCount > 0)
		WMGroupButtons(WPrefs.sectionB[0], bPtr);

	WPrefs.sectionCount++;

	WMResizeWidget(WPrefs.buttonF, WPrefs.sectionCount * 64, 64);
}

void Initialize(WMScreen * scr)
{
	char **list;
	int i;
	char *path;
	xcb_atom_t net_wm_pid;
	xcb_connection_t *dpy = WMScreenDisplay(scr);

	list = RSupportedFileFormats();
	for (i = 0; list[i] != NULL; i++) {
		if (strcmp(list[i], "TIFF") == 0) {
			TIFFOK = true;
			break;
		}
	}

	if (TIFFOK)
		path = WMPathForResourceOfType("WPrefs.tiff", NULL);
	else
		path = WMPathForResourceOfType("WPrefs.xpm", NULL);
	if (path) {
		RImage *tmp;

		tmp = RLoadImage(WMScreenRContext(scr), path, 0);
		if (!tmp) {
			wwarning(_("could not load image file %s:%s"), path, RMessageForError(RErrorCode));
		} else {
			WMSetApplicationIconImage(scr, tmp);
			RReleaseImage(tmp);
		}
		wfree(path);
	}

	memset(&WPrefs, 0, sizeof(_WPrefs));
	WPrefs.scr = scr;
	createMainWindow(scr);

	WMRealizeWidget(WPrefs.win);

	net_wm_pid = wx_static_atoms[W_ATOM_NET_WM_PID];
	WXSetCardinal(dpy, WMWidgetXID(WPrefs.win), net_wm_pid, (uint32_t)getpid());

	WMSetWindowMiniwindowImage(WPrefs.win, WMGetApplicationIconImage(scr));

	WMMapWidget(WPrefs.win);
	wXFlush(WMScreenDisplay(scr));
	WMSetLabelText(WPrefs.statusL, _("Loading Window Maker configuration files..."));
	wXFlush(WMScreenDisplay(scr));
	loadConfigurations(scr, WPrefs.win);
	loadSoundDomain(scr, WPrefs.win);

	WMSetLabelText(WPrefs.statusL, _("Initializing configuration panels..."));

	InitFocus(WPrefs.banner);
	InitWindowHandling(WPrefs.banner);

	InitMenuPreferences(WPrefs.banner);
	InitIcons(WPrefs.banner);
	InitPreferences(WPrefs.banner);

	InitPaths(WPrefs.banner);
	InitDocks(WPrefs.banner);
	InitWorkspace(WPrefs.banner);
	InitConfigurations(WPrefs.banner);

	InitMenu(WPrefs.banner);

#ifdef not_yet_fully_implemented
	InitKeyboardSettings(WPrefs.banner);
#endif
	InitKeyboardShortcuts(WPrefs.banner);
	InitHotCornerShortcuts(WPrefs.banner);
	InitMouseSettings(WPrefs.banner);

	InitAppearance(WPrefs.banner);

	InitFontSimple(WPrefs.banner);

#ifdef not_yet_fully_implemented
	InitThemes(WPrefs.banner);
#endif
	InitExpert(WPrefs.banner);

	WMRealizeWidget(WPrefs.scrollV);

	WMSetLabelText(WPrefs.statusL, "");
}

WMWindow *GetWindow(void)
{
	return WPrefs.win;
}

static void loadConfigurations(WMScreen * scr, WMWindow * mainw)
{
	WMPropList *db, *gdb;
	char *path;
	char buffer[1024];
	char mbuf[1069]; /* Size of buffer and extra characters for the sprintfs */
	int v1, v2, v3;
	char *argv[3];
	char *out;
	char *ptr;

	path = wdefaultspathfordomain("WindowMaker");
	WindowMakerDBPath = path;

	db = WMReadPropListFromFile(path);
	if (db) {
		if (!WMIsPLDictionary(db)) {
			WMReleasePropList(db);
			db = NULL;
			snprintf(mbuf, sizeof(mbuf), _("Window Maker domain (%s) is corrupted!"), path);
			WMRunAlertPanel(scr, mainw, _("Error"), mbuf, _("OK"), NULL, NULL);
		}
	} else {
		snprintf(mbuf, sizeof(mbuf), _("Could not load Window Maker domain (%s) from defaults database."), path);
		WMRunAlertPanel(scr, mainw, _("Error"), mbuf, _("OK"), NULL, NULL);
	}

	path = getenv("WMAKER_BIN_NAME");
	if (!path || !*path)
		path = "wmaker";

	argv[0] = path;
	argv[1] = "--version";
	argv[2] = NULL;
	out = wspawnvp_capture(argv, sizeof(buffer) - 1, NULL);
	if (out)
		wstrlcpy(buffer, out, sizeof(buffer));
	wfree(out);
	if (!out || buffer[0] == '\0') {
		werror(_("could not extract version information from Window Maker"));
		wfatal(_("Make sure wmaker is in your search path."));

		WMRunAlertPanel(scr, mainw, _("Error"),
				_
				("Could not extract version from Window Maker. Make sure it is correctly installed and is in your PATH environment variable."),
				_("OK"), NULL, NULL);
		exit(1);
	}

	if (sscanf(buffer, "Window Maker %i.%i.%i", &v1, &v2, &v3) != 3
	    && sscanf(buffer, "WindowMaker %i.%i.%i", &v1, &v2, &v3) != 3) {
		WMRunAlertPanel(scr, mainw, _("Error"),
				_("Could not extract version from Window Maker. "
				  "Make sure it is correctly installed and the path "
				  "where it installed is in the PATH environment "
				  "variable."), _("OK"), NULL, NULL);
		exit(1);
	}
	if (v1 == 0 && (v2 < 18 || v3 < 0)) {
		snprintf(mbuf, sizeof(mbuf), _("WPrefs only supports Window Maker 0.18.0 or newer.\n"
				"The version installed is %i.%i.%i\n"), v1, v2, v3);
		WMRunAlertPanel(scr, mainw, _("Error"), mbuf, _("OK"), NULL, NULL);
		exit(1);

	}
	if (v1 > 1 || (v1 == 1 && (v2 > 0))) {
		snprintf(mbuf, sizeof(mbuf),
			_
			("Window Maker %i.%i.%i, which is installed in your system, is not fully supported by this version of WPrefs."),
			v1, v2, v3);
		WMRunAlertPanel(scr, mainw, _("Warning"), mbuf, _("OK"), NULL, NULL);
	}

	argv[1] = "--global_defaults_path";
	out = wspawnvp_capture(argv, sizeof(buffer) - 1, NULL);
	if (!out || out[0] == '\0') {
		wfree(out);
		werror(_("could not run \"%s --global_defaults_path\"."), path);
		exit(1);
	}
	wstrlcpy(buffer, out, sizeof(buffer));
	wfree(out);
	ptr = strchr(buffer, '\n');
	if (ptr)
		*ptr = 0;
	if (wstrlcat(buffer, "/WindowMaker", sizeof(buffer)) >= sizeof(buffer)) {
		werror(_("global defaults path is too long"));
		exit(1);
	}

	gdb = WMReadPropListFromFile(buffer);

	if (gdb) {
		if (!WMIsPLDictionary(gdb)) {
			WMReleasePropList(gdb);
			gdb = NULL;
			snprintf(mbuf, sizeof(mbuf), _("Window Maker domain (%s) is corrupted!"), buffer);
			WMRunAlertPanel(scr, mainw, _("Error"), mbuf, _("OK"), NULL, NULL);
		}
	} else {
		snprintf(mbuf, sizeof(mbuf), _("Could not load global Window Maker domain (%s)."), buffer);
		WMRunAlertPanel(scr, mainw, _("Error"), mbuf, _("OK"), NULL, NULL);
	}

	if (!db) {
		db = WMCreatePLDictionary(NULL, NULL);
	}
	if (!gdb) {
		gdb = WMCreatePLDictionary(NULL, NULL);
	}

	GlobalDB = gdb;

	WindowMakerDB = db;
}

/* Load the user WMSound domain (~/.../WMSound). A missing file is not an
 * error: WMSoundDB stays NULL and is created on the first write. The
 * installed global WMSound domain is a read-only fallback. */
static void loadSoundDomain(WMScreen *scr, WMWindow *mainw)
{
	char *path;
	char mbuf[256];

	path = wdefaultspathfordomain("WMSound");
	WMSoundDBPath = path;

	WMSoundDB = WMReadPropListFromFile(path);
	if (WMSoundDB && !WMIsPLDictionary(WMSoundDB)) {
		WMReleasePropList(WMSoundDB);
		WMSoundDB = NULL;
		snprintf(mbuf, sizeof(mbuf),
			 _("Window Maker sound domain (%s) is corrupted!"), path);
		WMRunAlertPanel(scr, mainw, _("Error"), mbuf, _("OK"), NULL, NULL);
	}

	path = wglobaldefaultspathfordomain("WMSound");
	WMSoundGlobalDB = path ? WMReadPropListFromFile(path) : NULL;
	if (WMSoundGlobalDB && !WMIsPLDictionary(WMSoundGlobalDB)) {
		WMReleasePropList(WMSoundGlobalDB);
		WMSoundGlobalDB = NULL;
	}
	wfree(path);
}

WMPropList *GetObjectForKey(const char *defaultName)
{
	WMPropList *object = NULL;
	WMPropList *key = WMCreatePLString(defaultName);

	object = WMGetFromPLDictionary(WindowMakerDB, key);
	if (!object)
		object = WMGetFromPLDictionary(GlobalDB, key);

	WMReleasePropList(key);

	return object;
}

void SetObjectForKey(WMPropList * object, const char *defaultName)
{
	WMPropList *key = WMCreatePLString(defaultName);

	WMPutInPLDictionary(WindowMakerDB, key, object);
	WMReleasePropList(key);
}

void RemoveObjectForKey(const char *defaultName)
{
	WMPropList *key = WMCreatePLString(defaultName);

	WMRemoveFromPLDictionary(WindowMakerDB, key);

	WMReleasePropList(key);
}

/* manipulate the dictionary for the WMSound domain (edited by Sound.c) */

WMPropList *GetSoundObjectForKey(const char *defaultName)
{
	WMPropList *key, *object = NULL;

	key = WMCreatePLString(defaultName);
	if (WMSoundDB)
		object = WMGetFromPLDictionary(WMSoundDB, key);
	if (!object && WMSoundGlobalDB)
		object = WMGetFromPLDictionary(WMSoundGlobalDB, key);
	WMReleasePropList(key);
	return object;
}

void SetSoundObjectForKey(WMPropList *object, const char *defaultName)
{
	WMPropList *key;

	if (!WMSoundDB)
		WMSoundDB = WMCreatePLDictionary(NULL, NULL);
	key = WMCreatePLString(defaultName);
	WMPutInPLDictionary(WMSoundDB, key, object);
	WMReleasePropList(key);
}

char *GetStringForKey(const char *defaultName)
{
	WMPropList *val;

	val = GetObjectForKey(defaultName);

	if (!val)
		return NULL;

	if (!WMIsPLString(val))
		return NULL;

	return WMGetFromPLString(val);
}

int GetIntegerForKey(const char *defaultName)
{
	WMPropList *val;
	char *str;
	int value;

	val = GetObjectForKey(defaultName);

	if (!val)
		return 0;

	if (!WMIsPLString(val))
		return 0;

	str = WMGetFromPLString(val);
	if (!str)
		return 0;

	if (sscanf(str, "%i", &value) != 1)
		return 0;

	return value;
}

bool GetBoolForKey(const char *defaultName)
{
	int value;
	char *str;

	str = GetStringForKey(defaultName);

	if (!str)
		return false;

	if (sscanf(str, "%i", &value) == 1 && value != 0)
		return true;

	if (strcasecmp(str, "YES") == 0)
		return true;

	if (strcasecmp(str, "Y") == 0)
		return true;

	return false;
}

void SetIntegerForKey(int value, const char *defaultName)
{
	WMPropList *object;
	char buffer[128];

	snprintf(buffer, sizeof(buffer), "%i", value);
	object = WMCreatePLString(buffer);

	SetObjectForKey(object, defaultName);
	WMReleasePropList(object);
}

void SetStringForKey(const char *value, const char *defaultName)
{
	WMPropList *object;

	object = WMCreatePLString(value);

	SetObjectForKey(object, defaultName);
	WMReleasePropList(object);
}

static WMUserDefaults *wmGlobalDefaults(void)
{
	char *path = wdefaultspathfordomain("WMGLOBAL");
	WMUserDefaults *defaults = WMGetDefaultsFromPath(path);

	wfree(path);
	return defaults;
}

bool GetWMGlobalBoolForKey(const char *defaultName, bool fallback)
{
	WMUserDefaults *defaults = wmGlobalDefaults();

	if (!WMGetUDObjectForKey(defaults, defaultName))
		return fallback;
	return WMGetUDBoolForKey(defaults, defaultName);
}

void SetWMGlobalBoolForKey(bool value, const char *defaultName)
{
	WMUserDefaults *defaults = wmGlobalDefaults();

	WMSetUDBoolForKey(defaults, value, defaultName);
	WMSaveUserDefaults(defaults);
}

void SetBoolForKey(bool value, const char *defaultName)
{
	static WMPropList *yes = NULL, *no = NULL;

	if (!yes) {
		yes = WMCreatePLString("YES");
		no = WMCreatePLString("NO");
	}

	SetObjectForKey(value ? yes : no, defaultName);
}

int GetEnumForKey(const char *defaultName, int fallback)
{
	const WConfigEnum *e = wConfigSchemaEnumTable(defaultName);
	const char *str;

	if (!e)
		return fallback;

	str = GetStringForKey(defaultName);
	if (!str)
		str = wConfigSchemaDefault(defaultName);
	if (!str)
		return fallback;

	for (; e->string; e++)
		if (strcasecmp(e->string, str) == 0)
			return e->value;

	wwarning(_("bad value \"%s\" for option %s"), str, defaultName);
	return fallback;
}

void SetEnumForKey(int value, const char *defaultName)
{
	const WConfigEnum *e = wConfigSchemaEnumTable(defaultName);

	if (!e)
		return;

	for (; e->string; e++)
		if (e->value == value && !e->is_alias) {
			SetStringForKey(e->string, defaultName);
			return;
		}
}

WMPopUpButton *wm_enum_popup_new(WMWidget *parent, const wm_enum_choice *table, int n)
{
	WMPopUpButton *popup = WMCreatePopUpButton(parent);
	int i;

	for (i = 0; i < n; i++)
		WMAddPopUpButtonItem(popup, _(table[i].label));

	return popup;
}

void wm_enum_popup_select(WMPopUpButton *popup, const wm_enum_choice *table,
			  int n, const char *value, const char *key_name)
{
	int i;

	if (value) {
		for (i = 0; i < n; i++) {
			if (strcasecmp(value, table[i].db_value) == 0) {
				WMSetPopUpButtonSelectedItem(popup, i);
				return;
			}
		}
		wwarning(_("bad value \"%s\" for option %s, using default \"%s\""),
			 value, key_name, table[0].db_value);
	}
	WMSetPopUpButtonSelectedItem(popup, 0);
}

void wm_enum_popup_store(WMPopUpButton *popup, const wm_enum_choice *table,
			 int n, const char *key_name)
{
	int i = WMGetPopUpButtonSelectedItem(popup);

	if (i >= 0 && i < n)
		SetStringForKey(table[i].db_value, key_name);
}

bool GetDefaultBoolForKey(const char *defaultName)
{
	const char *def = wConfigSchemaDefault(defaultName);

	return def && (strcasecmp(def, "YES") == 0 || strcasecmp(def, "Y") == 0);
}

int GetDefaultIntegerForKey(const char *defaultName)
{
	const char *def = wConfigSchemaDefault(defaultName);
	int value;

	if (!def || sscanf(def, "%i", &value) != 1)
		return 0;
	return value;
}

/* Speed sliders run slow→fast (0..4) but SPEED_* runs fast→slow,
 * keep that inversion local to these two helpers. */
void SetSpeedForKey(int speed, const char *defaultName)
{
	SetEnumForKey(SPEED_ULTRASLOW - speed, defaultName);
}

int GetSpeedForKey(const char *defaultName)
{
	return SPEED_ULTRASLOW - GetEnumForKey(defaultName, SPEED_MEDIUM);
}
