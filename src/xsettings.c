/*
 *  xsettings - XSETTINGS manager. See xsettings.h.
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include "wconfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fontconfig/fontconfig.h>

#include <WINGs/WUtil.h>
#include <WINGs/WINGs.h>
#include <WINGs/WXProp.h>

#include "WindowMaker.h"
#include "window.h"
#include "screen.h"
#include "xsettings.h"

/* How often to look for the selection becoming free after another
 * manager owned it at startup or took it from us. The timer only runs
 * while we do not own the selection, so the normal case has no added
 * wakeups. */
#define XSETTINGS_RETRY_MS 2000

typedef struct WXSettings {
	struct _WScreen *scr;
	xcb_window_t win;
	xcb_atom_t sel_atom;
	xcb_atom_t prop_atom;
	bool owned;
	WMHandlerID retry_timer;
	uint32_t serial;
	/* last published table, names are literals so only sval is owned */
	WXSetting set[WXSETTINGS_MAX];
	size_t n;
} WXSettings;

/* The hicolor theme name the desktopfile resolver looks under. */
static const char icon_theme[] = "hicolor";

static const char *const hintstyle_names[] = {
	"hintnone", "hintslight", "hintmedium", "hintfull"
};

/* indexed by FC_RGBA_*, the XSETTINGS spelling of the same values,
 * FC_RGBA_NONE(5) and UNKNOWN(0) both mean "none" */
static const char *const rgba_names[FC_RGBA_NONE + 1] = {
	"none", "rgb", "bgr", "vrgb", "vbgr", "none"
};

static void settings_free_values(WXSetting *set, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		wfree(set[i].sval);
		set[i].sval = NULL;
	}
}

static void setting_init(WXSetting *s, const char *name, uint8_t type)
{
	s->name = (char *)name;
	s->type = type;
	s->ival = 0;
	s->sval = NULL;
	s->last_change = 0;
}

/* The WINGs system font, the same value the WINGs widgets open with,
 * re-read by WMReloadConfiguration() on a WMGLOBAL change, "sans serif"
 * when unset. */
static const char *system_font(void)
{
	const char *s = W_getconf_systemFont();

	return (s && *s) ? s : "sans serif";
}

/* Read the font-rendering defaults fontconfig would hand a client:
 * substitute and match an empty pattern the way a toolkit's default
 * font does, then pull the four knobs GTK/Qt publish from XSETTINGS. */
static void fontconfig_defaults(bool *antialias, bool *hinting,
				const char **hintstyle, const char **rgba)
{
	FcPattern *pat, *match;
	FcResult res;
	FcBool b;
	int v;

	*antialias = true;
	*hinting = true;
	*hintstyle = hintstyle_names[FC_HINT_SLIGHT];
	*rgba = "none";

	pat = FcPatternCreate();
	if (!pat)
		return;

	FcConfigSubstitute(NULL, pat, FcMatchPattern);
	FcDefaultSubstitute(pat);
	match = FcFontMatch(NULL, pat, &res);
	FcPatternDestroy(pat);
	if (!match)
		return;

	if (FcPatternGetBool(match, FC_ANTIALIAS, 0, &b) == FcResultMatch)
		*antialias = b != FcFalse;
	if (FcPatternGetBool(match, FC_HINTING, 0, &b) == FcResultMatch)
		*hinting = b != FcFalse;
	if (FcPatternGetInteger(match, FC_HINT_STYLE, 0, &v) == FcResultMatch
	    && v >= FC_HINT_NONE && v <= FC_HINT_FULL)
		*hintstyle = hintstyle_names[v];
	if (FcPatternGetInteger(match, FC_RGBA, 0, &v) == FcResultMatch
	    && v >= 0 && v <= FC_RGBA_NONE)
		*rgba = rgba_names[v];

	FcPatternDestroy(match);
}

static void collect(struct _WScreen *scr, WXSetting *set, size_t *np)
{
	size_t n = 0;
	char fontbuf[256];
	bool antialias, hinting;
	const char *hintstyle, *rgba;
	double dpi;

	dpi = WMScreenUIScale(scr->wmscreen) * 96.0;
	if (dpi < 1.0)
		dpi = 1.0;

	fontconfig_defaults(&antialias, &hinting, &hintstyle, &rgba);

	/* Xft/DPI is published in 1024ths of a point, the convention GTK
	 * (gtk-xft-dpi) and Qt both consume, so plain DPI below */
	setting_init(&set[n], "Xft/DPI", WXSETTINGS_TYPE_INTEGER);
	set[n++].ival = (uint32_t)(dpi * 1024.0 + 0.5);
	setting_init(&set[n], "Xft/Antialias", WXSETTINGS_TYPE_INTEGER);
	set[n++].ival = antialias ? 1 : 0;
	setting_init(&set[n], "Xft/Hinting", WXSETTINGS_TYPE_INTEGER);
	set[n++].ival = hinting ? 1 : 0;
	setting_init(&set[n], "Xft/HintStyle", WXSETTINGS_TYPE_STRING);
	set[n].sval = wstrdup(hintstyle);
	n++;
	setting_init(&set[n], "Xft/RGBA", WXSETTINGS_TYPE_STRING);
	set[n].sval = wstrdup(rgba);
	n++;

	if (wXSettingsPangoFontName(system_font(), dpi, fontbuf, sizeof(fontbuf))) {
		setting_init(&set[n], "Gtk/FontName", WXSETTINGS_TYPE_STRING);
		set[n].sval = wstrdup(fontbuf);
		n++;
	}

	setting_init(&set[n], "Gtk/IconThemeName", WXSETTINGS_TYPE_STRING);
	set[n].sval = wstrdup(icon_theme);
	n++;
	setting_init(&set[n], "Net/IconThemeName", WXSETTINGS_TYPE_STRING);
	set[n].sval = wstrdup(icon_theme);
	n++;

	*np = n;
}

static bool same_settings(const WXSetting *a, size_t na, const WXSetting *b, size_t nb)
{
	size_t i;

	if (na != nb)
		return false;

	for (i = 0; i < na; i++) {
		if (a[i].type != b[i].type || strcmp(a[i].name, b[i].name) != 0)
			return false;
		if (a[i].type == WXSETTINGS_TYPE_STRING) {
			if (!a[i].sval || !b[i].sval || strcmp(a[i].sval, b[i].sval) != 0)
				return false;
		} else if (a[i].ival != b[i].ival) {
			return false;
		}
	}

	return true;
}

void wXSettingsRefresh(struct _WScreen *scr)
{
	WXSettings *xs = (WXSettings *)scr->xsettings;
	WXSetting fresh[WXSETTINGS_MAX];
	size_t n = 0, i;
	unsigned char *payload;
	size_t plen;

	if (!xs || !xs->owned)
		return;

	collect(scr, fresh, &n);
	if (same_settings(xs->set, xs->n, fresh, n)) {
		settings_free_values(fresh, n);
		return;
	}

	xs->serial++;
	settings_free_values(xs->set, xs->n);
	for (i = 0; i < n; i++) {
		xs->set[i] = fresh[i];
		xs->set[i].last_change = xs->serial;
	}
	xs->n = n;

	if (wXSettingsEncode(xs->set, xs->n, xs->serial, &payload, &plen) != WXSETTINGS_OK) {
		wwarning("could not encode the XSETTINGS payload");
		return;
	}
	WXSetFormat8(wdisp->conn, xs->win, xs->prop_atom, xs->prop_atom, payload, plen);
	wfree(payload);
}

void wXSettingsRefreshAll(void)
{
	int i;

	for (i = 0; i < wdisp->screen_count; i++) {
		struct _WScreen *scr = wScreenWithNumber(i);

		if (scr)
			wXSettingsRefresh(scr);
	}
}

/* Claim the manager selection when it is free. Never take it away
 * from a live manager: a non-free selection is left to the retry
 * timer. Returns true when this process owns it after the call. */
static bool claim(WXSettings *xs)
{
	xcb_window_t owner;

	owner = wXGetSelectionOwner(wdisp->conn, xs->sel_atom);
	if (owner && owner != xs->win)
		return false;

	wXSetSelectionOwner(wdisp->conn, xs->sel_atom, xs->win, XCB_CURRENT_TIME);
	if (wXGetSelectionOwner(wdisp->conn, xs->sel_atom) != xs->win)
		return false;

	xs->owned = true;

	/* ICCCM 2.8 manager selection: announce the takeover on the root
	 * window so clients waiting for a MANAGER message notice us */
	WXSendClientMessage(wdisp->conn, xs->scr->root_win, XCB_EVENT_MASK_STRUCTURE_NOTIFY,
			    xs->scr->root_win, wx_static_atoms[W_ATOM_MANAGER],
			    (long)XCB_CURRENT_TIME, (long)xs->sel_atom,
			    (long)xs->win, 0, 0);

	wXSettingsRefresh(xs->scr);
	return true;
}

static void retryClaim(void *data)
{
	WXSettings *xs = (WXSettings *)data;

	if (!xs->owned && claim(xs))
		WMDeleteTimerHandlerPtr(&xs->retry_timer);
}

static void armRetry(WXSettings *xs)
{
	if (!xs->retry_timer)
		xs->retry_timer = WMAddPersistentTimerHandler(XSETTINGS_RETRY_MS,
							      retryClaim, xs);
}

void wXSettingsInit(struct _WScreen *scr)
{
	char name[32];
	WXSettings *xs;
	xcb_window_t owner;

	snprintf(name, sizeof(name), "_XSETTINGS_S%d", scr->screen);

	xs = wmalloc(sizeof(WXSettings));
	xs->scr = scr;
	xs->sel_atom = WXInternAtom(wdisp->conn, name);
	xs->prop_atom = wx_static_atoms[W_ATOM_XSETTINGS_SETTINGS];
	if (!xs->sel_atom || !xs->prop_atom) {
		wwarning("could not intern the XSETTINGS atoms for screen %d", scr->screen);
		wfree(xs);
		return;
	}

	xs->win = xcb_generate_id(wdisp->conn);
	xcb_create_window(wdisp->conn, XCB_COPY_FROM_PARENT, xs->win, scr->root_win,
			  -1, -1, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY, XCB_COPY_FROM_PARENT,
			  0, NULL);
	scr->xsettings = xs;

	if (claim(xs))
		return;

	owner = wXGetSelectionOwner(wdisp->conn, xs->sel_atom);
	if (owner && owner != xs->win)
		wmessage(_("another XSETTINGS manager is running on screen %d"), scr->screen);
	else
		wwarning(_("could not claim the XSETTINGS selection for screen %d"),
			 scr->screen);

	/* the other manager may exit later, keep trying without taking
	 * the selection from anyone who still owns it */
	armRetry(xs);
}

void wXSettingsShutdown(struct _WScreen *scr)
{
	WXSettings *xs = (WXSettings *)scr->xsettings;

	if (!xs)
		return;

	WMDeleteTimerHandlerPtr(&xs->retry_timer);

	if (xs->owned) {
		WXDeleteProperty(wdisp->conn, xs->win, xs->prop_atom);
		wXSetSelectionOwner(wdisp->conn, xs->sel_atom, XCB_NONE, XCB_CURRENT_TIME);
	}
	settings_free_values(xs->set, xs->n);
	xcb_destroy_window(wdisp->conn, xs->win);
	wfree(xs);
	scr->xsettings = NULL;
}

void wXSettingsSelectionClear(const xcb_selection_clear_event_t *ev)
{
	int i;

	for (i = 0; i < wdisp->screen_count; i++) {
		struct _WScreen *scr = wScreenWithNumber(i);
		WXSettings *xs = scr ? (WXSettings *)scr->xsettings : NULL;

		if (xs && xs->owned && ev->owner == xs->win && ev->selection == xs->sel_atom) {
			xs->owned = false;
			wmessage(_("another XSETTINGS manager is taking over screen %d"), i);
			/* that manager may exit later, reclaim the selection
			 * once it is free again */
			armRetry(xs);
		}
	}
}
