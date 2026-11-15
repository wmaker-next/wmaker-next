/*
 *  wfont.c
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 1998 scottc
 *  Copyright (c) 1999-2005 Dan Pascu
 *  Copyright (c) 1999-2004 Alfredo K. Kojima
 *  Copyright (c) 2000-2026 Window Maker Team
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

#include <stdlib.h>

#include "wconfig.h"

#include "WINGsP.h"

#include <wraster.h>
#include <assert.h>

#include <fontconfig/fontconfig.h>

#include <cairo.h>
#include <pango/pango.h>
#include <pango/pangofc-fontmap.h>
#include <pango/pangocairo.h>

#define DEFAULT_FONT "sans serif:pixelsize=12"

#define DEFAULT_SIZE WINGsConfiguration.defaultFontSize

/* Compat parse for legacy XLFD font names still present in old
 * WindowMaker/WMGLOBAL domains, e.g.
 *   -adobe-helvetica-bold-r-normal--12-120-75-75-p-70-iso8859-1
 * Only the family, weight, slant and pixelsize (or pointsize/10 when
 * pixelsize is a wildcard) carry into the fontconfig pattern, the
 * foundry, setwidth, resolution, spacing and encoding fields are
 * discarded because fontconfig substitutes those at match time.
 * A malformed name (missing leading dash, too few fields, %d
 * placeholder from an old fontset spec) falls back to DEFAULT_FONT. */
FcPattern *W_XlfdToFcPattern(const char *xlfd)
{
	static const struct { const char *name; int weight; } weights[] = {
		{ "thin",	FC_WEIGHT_THIN },
		{ "extralight",	FC_WEIGHT_EXTRALIGHT },
		{ "ultralight",	FC_WEIGHT_ULTRALIGHT },
		{ "light",	FC_WEIGHT_LIGHT },
		{ "book",	FC_WEIGHT_BOOK },
		{ "regular",	FC_WEIGHT_REGULAR },
		{ "normal",	FC_WEIGHT_NORMAL },
		{ "medium",	FC_WEIGHT_MEDIUM },
		{ "demibold",	FC_WEIGHT_DEMIBOLD },
		{ "semibold",	FC_WEIGHT_SEMIBOLD },
		{ "bold",	FC_WEIGHT_BOLD },
		{ "extrabold",	FC_WEIGHT_EXTRABOLD },
		{ "ultrabold",	FC_WEIGHT_ULTRABOLD },
		{ "black",	FC_WEIGHT_BLACK },
		{ "heavy",	FC_WEIGHT_HEAVY },
	};
	FcPattern *pattern;
	char *fname, *field[14], *p;
	double px, pt;
	int i, n;

	if (xlfd[0] != '-' || strchr(xlfd, '%') != NULL)
		return FcNameParse((const FcChar8 *)DEFAULT_FONT);

	/* work on a private copy, cut at the first comma so an old
	 * comma-separated fontset spec yields its first face, then
	 * split into up to 14 dash-separated fields (the leading dash
	 * means field[0] is one past the string start) */
	fname = wstrdup(xlfd);
	p = strchr(fname, ',');
	if (p)
		*p = '\0';
	n = 0;
	for (p = fname; *p && n < 14; p++) {
		if (*p == '-') {
			*p = '\0';
			field[n++] = p + 1;
		}
	}
	if (n < 7) {
		wwarning(_("invalid font: %s. Trying '%s'"), xlfd, DEFAULT_FONT);
		wfree(fname);
		return FcNameParse((const FcChar8 *)DEFAULT_FONT);
	}

	pattern = FcPatternCreate();

	/* family, wildcard leaves it to fontconfig's default sans */
	if (field[1][0] && strcmp(field[1], "*") != 0)
		FcPatternAddString(pattern, FC_FAMILY, (const FcChar8 *)field[1]);

	/* weight */
	for (i = 0; i < (int)wlengthof(weights); i++) {
		if (strcasecmp(field[2], weights[i].name) == 0) {
			FcPatternAddInteger(pattern, FC_WEIGHT, weights[i].weight);
			break;
		}
	}

	/* slant: r roman, i italic, o oblique */
	switch (field[3][0]) {
	case 'i': case 'I':
		FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ITALIC);
		break;
	case 'o': case 'O':
		FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_OBLIQUE);
		break;
	case 'r': case 'R':
		FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
		break;
	}

	/* pixelsize, else pointsize (decipoints in the XLFD field) */
	px = strtod(field[6], NULL);
	if (px > 0.0) {
		FcPatternAddDouble(pattern, FC_PIXEL_SIZE, px);
	} else if (n > 7) {
		pt = strtod(field[7], NULL);
		if (pt > 0.0)
			FcPatternAddDouble(pattern, FC_SIZE, pt / 10.0);
	}

	wfree(fname);
	return pattern;
}

static FcPattern *xlfdToFcPattern(const char *xlfd)
{
	return W_XlfdToFcPattern(xlfd);
}

/* FcNameUnparse() memory is fontconfig-owned, callers release with wfree() */
static char *dupFcName(FcPattern *pattern)
{
	char *name, *copy;

	name = (char *)FcNameUnparse(pattern);
	if (!name)
		return NULL;
	copy = wstrdup(name);
	FcStrFree((FcChar8 *) name);

	return copy;
}

static char *xlfdToFcName(const char *xlfd)
{
	FcPattern *pattern;
	char *fname;

	pattern = xlfdToFcPattern(xlfd);
	fname = dupFcName(pattern);
	FcPatternDestroy(pattern);

	return fname;
}

static bool hasProperty(FcPattern * pattern, const char *property)
{
	FcValue val;

	if (FcPatternGet(pattern, property, 0, &val) == FcResultMatch) {
		return true;
	}

	return false;
}

static char *makeFontOfSize(const char *font, int size, const char *fallback)
{
	FcPattern *pattern = NULL;
	char *result;

	if (font && font[0] == '-') {
		pattern = xlfdToFcPattern(font);
	} else {
		pattern = FcNameParse((const FcChar8 *) font);
	}

	if (!pattern) {
		wwarning(_("could not load font spec: %s."), font);
		if (!fallback)
			return NULL;
		pattern = FcPatternCreate();
		if (!pattern)
			return NULL;
		if (!FcPatternAddString(pattern, FC_FAMILY, (const FcChar8 *) fallback)) {
			wfatal(_("could not load default font spec: %s."), fallback);
			FcPatternDestroy(pattern);
			return NULL;
		}
	}

	if (size > 0) {
		FcPatternDel(pattern, FC_PIXEL_SIZE);
		FcPatternAddDouble(pattern, FC_PIXEL_SIZE, (double)size);
	} else if (size == 0 && !hasProperty(pattern, "size") && !hasProperty(pattern, FC_PIXEL_SIZE)) {
		FcPatternAddDouble(pattern, FC_PIXEL_SIZE, (double)DEFAULT_SIZE);
	}

	result = dupFcName(pattern);
	FcPatternDestroy(pattern);

	return result;
}

WMFont *WMCreateFont(WMScreen * scrPtr, const char *fontName)
{
	WMFont *font;
	char *fname;
	PangoFontMap *fontmap;
	PangoContext *context;
	PangoFontMetrics *metrics;
	FcPattern *pattern;
	PangoFontDescription *description;
	double size;
	int ascent, descent;
	void *old;

	if (fontName && fontName[0] == '-') {
		fname = xlfdToFcName(fontName);
	} else {
		fname = wstrdup(fontName);
	}

	if (!WINGsConfiguration.antialiasedText && !strstr(fname, ":antialias=")) {
		fname = wstrappend(fname, ":antialias=false");
	}

	font = WMHashGet(scrPtr->fontCache, fname);
	if (font) {
		WMRetainFont(font);
		wfree(fname);
		return font;
	}

	font = wmalloc(sizeof(WMFont));

	font->screen = scrPtr;
	font->refCount = 1;
	font->name = fname;

	/* the default cairo font map on any fontconfig platform is a
	 * PangoFcFontMap, so pango_fc_font_description_from_pattern()
	 * below resolves against the same fontconfig configuration the
	 * previous Xft path used. The description stays at the requested
	 * logical size, W_DrawString() applies uiScale at paint time
	 * so WMFontHeight()/WMWidthOfString() return logical px and mix
	 * correctly with logical view->size in widget layout. */
	fontmap = pango_cairo_font_map_get_default();
	context = pango_font_map_create_context(fontmap);

	pattern = FcNameParse((FcChar8 *) font->name);
	description = pango_fc_font_description_from_pattern(pattern, FALSE);

	/* Pango examines FC_SIZE but not FC_PIXEL_SIZE of the pattern, but
	 * font-name has only "pixelsize", so set the size manually here.
	 */
	if (FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &size) == FcResultMatch)
		pango_font_description_set_absolute_size(description, size * PANGO_SCALE);

	font->layout = pango_layout_new(context);
	pango_layout_set_font_description(font->layout, description);

	font->wrapLayout = pango_layout_new(context);
	pango_layout_set_font_description(font->wrapLayout, description);

	/* WMFontHeight() and the callers that offset by font->y expect the
	 * primary face's ascent+descent, matching what the previous
	 * XftFontOpenName() path exposed via font->font->ascent/descent. */
	metrics = pango_context_get_metrics(context, description, NULL);
	ascent = PANGO_PIXELS(pango_font_metrics_get_ascent(metrics));
	descent = PANGO_PIXELS(pango_font_metrics_get_descent(metrics));
	font->height = ascent + descent;
	font->y = ascent;
	pango_font_metrics_unref(metrics);

	/* Hint the layout at the resolution the glyphs are painted at. */
	if (scrPtr->uiScale > 1.0001) {
		PangoMatrix matrix = PANGO_MATRIX_INIT;

		pango_matrix_scale(&matrix, scrPtr->uiScale, scrPtr->uiScale);
		pango_context_set_matrix(context, &matrix);
		pango_layout_context_changed(font->layout);
		pango_layout_context_changed(font->wrapLayout);
	}

	pango_font_description_free(description);
	g_object_unref(context);
	FcPatternDestroy(pattern);

	old = WMHashInsert(scrPtr->fontCache, font->name, font);
	assert(old == NULL);
	(void)old;

	return font;
}

WMFont *WMRetainFont(WMFont * font)
{
	wassertrv(font != NULL, NULL);

	font->refCount++;

	return font;
}

void WMReleaseFont(WMFont * font)
{
	wassertr(font != NULL);

	font->refCount--;
	if (font->refCount < 1) {
		if (font->layout)
			g_object_unref(font->layout);
		if (font->wrapLayout)
			g_object_unref(font->wrapLayout);
		if (font->name) {
			WMHashRemove(font->screen->fontCache, font->name);
			wfree(font->name);
		}
		wfree(font);
	}
}

bool WMIsAntialiasingEnabled(WMScreen * scrPtr)
{
	return scrPtr->antialiasedText;
}

unsigned int WMFontHeight(WMFont * font)
{
	wassertrv(font != NULL, 0);

	return font->height;
}

char *WMGetFontName(WMFont * font)
{
	wassertrv(font != NULL, NULL);

	return font->name;
}

void WMGetScaleBaseFromSystemFont(WMScreen *scrPtr, int *alphabetWidth, int *fontHeight)
{
	WMFont *font;

	font = WMDefaultSystemFont(scrPtr);
	*alphabetWidth = WMWidthOfString(font, "abcdefghijklmnopqrstuvwxyz", 26);
	*fontHeight = WMFontHeight(font);
	WMReleaseFont(font);
}

WMFont *WMDefaultSystemFont(WMScreen * scrPtr)
{
	return WMRetainFont(scrPtr->normalFont);
}

WMFont *WMDefaultBoldSystemFont(WMScreen * scrPtr)
{
	return WMRetainFont(scrPtr->boldFont);
}

WMFont *WMSystemFontOfSize(WMScreen * scrPtr, int size)
{
	WMFont *font;
	char *fontSpec;

	fontSpec = makeFontOfSize(WINGsConfiguration.systemFont, size, DEFAULT_FONT);

	if (!fontSpec) {
		return NULL;
	}

	font = WMCreateFont(scrPtr, fontSpec);

	if (!font) {
		wwarning(_("could not load font: %s."), fontSpec);
	}

	wfree(fontSpec);

	return font;
}

WMFont *WMBoldSystemFontOfSize(WMScreen * scrPtr, int size)
{
	WMFont *font;
	char *fontSpec;

	fontSpec = makeFontOfSize(WINGsConfiguration.boldSystemFont, size, DEFAULT_FONT);

	if (!fontSpec) {
		return NULL;
	}

	font = WMCreateFont(scrPtr, fontSpec);

	if (!font) {
		wwarning(_("could not load font: %s."), fontSpec);
	}

	wfree(fontSpec);

	return font;
}

/* Set text on the layout only when it differs, so a WMWidthOfString()
 * followed by a WMDrawString() with the same text (the common pattern
 * in every centred-label caller) does not re-run itemisation and
 * shaping. */
static void setLayoutText(PangoLayout *layout, const char *text, int length)
{
	const char *prev;

	prev = pango_layout_get_text(layout);
	if (prev == NULL || strncmp(text, prev, length) != 0 || prev[length] != '\0')
		pango_layout_set_text(layout, text, length);
}

int WMWidthOfString(WMFont * font, const char *text, int length)
{
	int width;

	wassertrv(font != NULL && text != NULL, 0);

	setLayoutText(font->layout, text, length);
	pango_layout_get_pixel_size(font->layout, &width, NULL);
	return width;
}

int WMHeightOfString(WMFont * font, const char *text, int length)
{
	int height;

	wassertrv(font != NULL && text != NULL, 0);

	setLayoutText(font->layout, text, length);
	pango_layout_get_pixel_size(font->layout, NULL, &height);
	return height;
}

/* Render the layout at (x, y). The font description is at logical
 * pixelsize so on a scaled cairo context (WINGs W_View drawables) the
 * CTM carries the layout to device size. On an unscaled context
 * (wm-side WCoreWindow drawables, device-px paint) apply the scale
 * locally around the pango call so the same font renders at the same
 * device size in both worlds, treating (x, y) as device px there. */
void W_ShowLayout(cairo_t *cr, WMFont *font, PangoLayout *layout, int x, int y)
{
	double s = font->screen->uiScale;
	cairo_matrix_t m;

	cairo_get_matrix(cr, &m);
	if (s > 1.0001 && m.xx > 0.999 && m.xx < 1.001) {
		cairo_save(cr);
		cairo_translate(cr, x, y);
		cairo_scale(cr, s, s);
		cairo_move_to(cr, 0, 0);
		pango_cairo_update_layout(cr, layout);
		pango_cairo_show_layout(cr, layout);
		cairo_restore(cr);
	} else {
		cairo_move_to(cr, x, y);
		pango_cairo_update_layout(cr, layout);
		pango_cairo_show_layout(cr, layout);
	}
}

void W_DrawString(cairo_t *cr, WMColor *color, WMFont *font,
		       int x, int y, const char *text, int length)
{
	setLayoutText(font->layout, text, length);
	W_SetSourceColor(cr, color);
	W_ShowLayout(cr, font, font->layout, x, y);
}

void WMDrawString(WMScreen * scr, xcb_drawable_t d, WMColor * color, WMFont * font, int x, int y, const char *text, int length)
{
	cairo_t *cr;

	wassertr(font != NULL);

	cr = WCairoBeginForDrawable(scr, d);
	if (!cr)
		return;
	W_DrawString(cr, color, font, x, y, text, length);
	WCairoEnd(cr);
}

void
WMDrawImageString(WMScreen * scr, xcb_drawable_t d, WMColor * color, WMColor * background,
		  WMFont * font, int x, int y, const char *text, int length)
{
	cairo_t *cr;
	int width;

	wassertr(font != NULL);

	cr = WCairoBeginForDrawable(scr, d);
	if (!cr)
		return;

	setLayoutText(font->layout, text, length);
	pango_layout_get_pixel_size(font->layout, &width, NULL);

	W_SetSourceColor(cr, background);
	cairo_rectangle(cr, x, y, width, font->height);
	cairo_fill(cr);

	W_SetSourceColor(cr, color);
	W_ShowLayout(cr, font, font->layout, x, y);

	WCairoEnd(cr);
}

WMFont *WMCopyFontWithStyle(WMScreen * scrPtr, WMFont * font, WMFontStyle style)
{
	FcPattern *pattern;
	WMFont *copy;
	char *name;

	if (!font)
		return NULL;

	/* It's enough to add italic to slant, even if the font has no italic
	 * variant, but only oblique. This is because fontconfig will actually
	 * return the closest match font to what we requested which is the
	 * oblique font. Same goes for using bold for weight.
	 */
	pattern = FcNameParse((FcChar8 *) WMGetFontName(font));
	switch (style) {
	case WFSNormal:
		FcPatternDel(pattern, FC_WEIGHT);
		FcPatternDel(pattern, FC_SLANT);
		break;
	case WFSBold:
		FcPatternDel(pattern, FC_WEIGHT);
		FcPatternAddString(pattern, FC_WEIGHT, (FcChar8 *) "bold");
		break;
	case WFSItalic:
		FcPatternDel(pattern, FC_SLANT);
		FcPatternAddString(pattern, FC_SLANT, (FcChar8 *) "italic");
		break;
	case WFSBoldItalic:
		FcPatternDel(pattern, FC_WEIGHT);
		FcPatternDel(pattern, FC_SLANT);
		FcPatternAddString(pattern, FC_WEIGHT, (FcChar8 *) "bold");
		FcPatternAddString(pattern, FC_SLANT, (FcChar8 *) "italic");
		break;
	}

	name = dupFcName(pattern);
	copy = WMCreateFont(scrPtr, name);
	FcPatternDestroy(pattern);
	wfree(name);

	return copy;
}

void WMFlushFontCache(WMScreen *scr)
{
	WMFont *old_normal, *old_bold;

	if (!scr || !scr->fontCache)
		return;

	/* Detach every cached font without freeing it. The hash callbacks
	 * (WMStringPointerHashCallbacks) own neither key nor data, so
	 * WMResetHashTable only releases its internal nodes. Each holder
	 * (WScreen font fields, retained widget fonts) keeps a valid
	 * reference and releases it through WMReleaseFont's refcounting,
	 * while the now-empty cache forces the next WMCreateFont to re-open
	 * under the current WINGsConfiguration.antialiasedText flag. */
	WMResetHashTable(scr->fontCache);

	/* The per-screen system fonts are detached above; release the old
	 * (orphaned) handles and re-open them so WMDefaultSystemFont and
	 * WMDefaultBoldSystemFont return AA-current fonts. boldFont may alias
	 * normalFont (the widgets.c init falls back to normalFont when the
	 * bold face fails to load) without an extra retain, so release the
	 * distinct pointer only. */
	old_normal = scr->normalFont;
	old_bold = scr->boldFont;
	scr->normalFont = NULL;
	scr->boldFont = NULL;
	if (old_normal)
		WMReleaseFont(old_normal);
	if (old_bold && old_bold != old_normal)
		WMReleaseFont(old_bold);
	scr->normalFont = WMSystemFontOfSize(scr, 0);
	scr->boldFont = WMBoldSystemFontOfSize(scr, 0);
	if (!scr->boldFont)
		scr->boldFont = scr->normalFont;
}

/* Exit-only. Pango's font map holds an FcConfig reference past
 * pango_fc_font_map_shutdown(), so release it before FcFini(). */
void WMShutdownFonts(void)
{
	PangoFontMap *fontmap = pango_cairo_font_map_get_default();

	if (fontmap)
		pango_fc_font_map_shutdown(PANGO_FC_FONT_MAP(fontmap));
	FcConfigDestroy(FcConfigGetCurrent());
	FcFini();
}
