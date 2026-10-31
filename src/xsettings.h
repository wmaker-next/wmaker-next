/*
 *  xsettings - XSETTINGS manager. wmaker claims the _XSETTINGS_S%d
 *  selection, owns the _XSETTINGS_SETTINGS property on a private
 *  manager window and republishes it when the values it derives from
 *  its own configuration change, so GTK/Qt and other toolkits follow
 *  the same DPI, font, icon theme and font-rendering values the WM
 *  uses. See xsettingswire.c for the wire codec.
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

#ifndef WM_XSETTINGS_H_
#define WM_XSETTINGS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <xcb/xcb.h>

#include <WINGs/WUtil.h>

struct _WScreen;

/* One published setting. name is the XSETTINGS key ("Xft/DPI"). On the
 * encode side sval/ival are borrowed; wXSettingsDecode() returns the
 * same struct with wmalloc'd name/sval released by
 * wXSettingsFreeDecoded(). type picks which field is live. */
typedef struct {
	char *name;
	uint8_t type;
	uint32_t ival;		/* WXSETTINGS_TYPE_INTEGER */
	char *sval;		/* WXSETTINGS_TYPE_STRING */
	uint32_t last_change;	/* serial of the last change, filled by encode */
} WXSetting;

/* XSETTINGS setting types on the wire */
#define WXSETTINGS_TYPE_INTEGER 0
#define WXSETTINGS_TYPE_STRING  1
#define WXSETTINGS_TYPE_COLOR   2

/* Upper bound on the number of settings the manager publishes, also
 * the cap wXSettingsDecode() enforces so a hostile property cannot
 * force unbounded allocations. */
#define WXSETTINGS_MAX 32
/* Hard cap on an encoded payload, and the boundary decode rejects. */
#define WXSETTINGS_MAX_BYTES 65536

/* Result codes of the wire codec. */
#define WXSETTINGS_OK        0
#define WXSETTINGS_E_ARGS   -1
#define WXSETTINGS_E_SIZE   -2
#define WXSETTINGS_E_MALF   -3

/* --- wire codec (pure, no X connection, unit-test target) ------------- */

/* Number of bytes wXSettingsEncode() needs for these settings, 0 when
 * the list cannot be encoded (name too long, > WXSETTINGS_MAX_BYTES). */
size_t wXSettingsEncodedSize(const WXSetting *set, size_t n);

/* Encode the header (byte order, serial, count) and the settings into
 * a wmalloc'd buffer returned through *out with *outlen bytes. Always
 * little-endian with the matching byte-order marker. */
int wXSettingsEncode(const WXSetting *set, size_t n, uint32_t serial,
		     unsigned char **out, size_t *outlen);

/* Parse a _XSETTINGS_SETTINGS payload in either byte order. Returns
 * WXSETTINGS_OK and a wmalloc'd setting array in *set (freed with
 * wXSettingsFreeDecoded()) with the header serial in *serial, or a
 * negative code and nothing allocated. */
int wXSettingsDecode(const unsigned char *buf, size_t len,
		     uint32_t *serial, WXSetting **set, size_t *n);

/* Release a decode result. */
void wXSettingsFreeDecoded(WXSetting *set, size_t n);

/* Convert a WM font spec ("Sans:bold:pixelsize=12", "sans serif") into
 * a Pango font description ("Sans Bold 9"), scaling a pixelsize to
 * points at the given DPI so the toolkit font matches the WM font at
 * any scale. Returns false when the spec has no usable family or out
 * is too small. */
bool wXSettingsPangoFontName(const char *spec, double dpi,
			     char *out, size_t outlen);

/* --- manager ----------------------------------------------------------- */

/* Claim _XSETTINGS_S<screen> and publish the initial settings. When
 * another XSETTINGS manager already owns the selection, leave it alone
 * and keep checking in the background so the selection is claimed once
 * that manager exits. */
void wXSettingsInit(struct _WScreen *scr);

/* Re-collect the values derived from the current configuration and
 * republish on a content change (serial bump + property write). Safe
 * to call at any rate: an unchanged collection does not touch the
 * property. */
void wXSettingsRefresh(struct _WScreen *scr);

/* wXSettingsRefresh() for every managed screen. */
void wXSettingsRefreshAll(void);

/* Release the selection and drop the property (shutdown/restart). */
void wXSettingsShutdown(struct _WScreen *scr);

/* Another manager took the selection away, mark the screen as no
 * longer publishing and retry if that manager exits. Cheap to call for
 * every SelectionClear. */
void wXSettingsSelectionClear(const xcb_selection_clear_event_t *ev);

#endif /* WM_XSETTINGS_H_ */
