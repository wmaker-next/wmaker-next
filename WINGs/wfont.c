
#include <stdlib.h>

#include "wconfig.h"

#include "WINGsP.h"

#include <wraster.h>
#include <assert.h>
#include <X11/Xlocale.h>

#include <X11/Xft/Xft.h>
#include <fontconfig/fontconfig.h>

#ifdef USE_PANGO
#include <pango/pango.h>
#include <pango/pangofc-fontmap.h>
#include <pango/pangoxft.h>
#endif

#define DEFAULT_FONT "sans serif:pixelsize=12"

#define DEFAULT_SIZE WINGsConfiguration.defaultFontSize

#ifndef USE_PANGO
/*
 * Per-character Xft font fallback.
 *
 * XftDrawStringUtf8 uses a single XftFont and shows a "notdef" box for any
 * code point not present in that font.  When the primary font is missing a
 * glyph (common for Unicode modifier symbols configured via
 * ModifierKeyShortLabels, or CJK in window titles), ask fontconfig for any
 * installed font carrying that code point at the primary font's pixel size
 * and weight, and use it for the missing run.
 *
 * The cache is keyed on the primary XftFont pointer so size/weight/slant
 * carry over from its resolved pattern, and the same fallback is reused
 * for every code point it covers before another lookup is made.
 */
#define FONT_FALLBACK_CACHE_SIZE 16

static struct {
	XftFont *primary;
	XftFont *fallback;
} fb_cache[FONT_FALLBACK_CACHE_SIZE];
static int fb_cache_n;

static XftFont *open_fallback_for_cp(Display *dpy, int screen, XftFont *primary, FcChar32 cp)
{
	FcPattern *pat, *match;
	FcCharSet *cs;
	FcResult result;
	XftFont *f = NULL;

	pat = FcPatternDuplicate(primary->pattern);
	if (!pat)
		return NULL;

	/* drop the resolved family/file/charset so fontconfig is free to pick
	 * a different face, keep pixelsize/weight/slant/dpi from the primary */
	FcPatternDel(pat, FC_FAMILY);
	FcPatternDel(pat, FC_FAMILYLANG);
	FcPatternDel(pat, FC_STYLE);
	FcPatternDel(pat, FC_FILE);
	FcPatternDel(pat, FC_INDEX);
	FcPatternDel(pat, FC_CHARSET);
	FcPatternDel(pat, FC_LANG);
	FcPatternDel(pat, FC_FULLNAME);
	FcPatternDel(pat, FC_POSTSCRIPT_NAME);

	cs = FcCharSetCreate();
	if (cs) {
		FcCharSetAddChar(cs, cp);
		FcPatternAddCharSet(pat, FC_CHARSET, cs);
		FcCharSetDestroy(cs);
	}
	FcPatternAddBool(pat, FC_SCALABLE, FcTrue);

	match = XftFontMatch(dpy, screen, pat, &result);
	FcPatternDestroy(pat);
	if (match) {
		f = XftFontOpenPattern(dpy, match);
		if (!f)
			FcPatternDestroy(match);
	}
	return f;
}

/* Return the XftFont that can render code point 'cp': primary if it has the
 * glyph, otherwise a cached or freshly matched fallback that does. Returns
 * primary if no installed font covers 'cp'. */
static XftFont *pick_font_for_cp(Display *dpy, int screen, XftFont *primary, FcChar32 cp)
{
	XftFont *f;
	int i;

	if (XftCharExists(dpy, primary, cp))
		return primary;

	/* try every fallback already opened for this primary */
	for (i = 0; i < fb_cache_n; i++) {
		if (fb_cache[i].primary == primary &&
		    XftCharExists(dpy, fb_cache[i].fallback, cp))
			return fb_cache[i].fallback;
	}

	f = open_fallback_for_cp(dpy, screen, primary, cp);
	if (!f || !XftCharExists(dpy, f, cp)) {
		if (f)
			XftFontClose(dpy, f);
		return primary;
	}

	if (fb_cache_n < FONT_FALLBACK_CACHE_SIZE) {
		fb_cache[fb_cache_n].primary  = primary;
		fb_cache[fb_cache_n].fallback = f;
		fb_cache_n++;
	}
	return f;
}

/* Drop every cached fallback font so a re-read of AntialiasedText does not
 * keep stale XftFont handles keyed on primary pointers that have since been
 * re-opened. The primaries themselves are owned by the WMFont cache. */
static void fb_cache_reset(Display *dpy)
{
	int i;

	for (i = 0; i < fb_cache_n; i++) {
		if (fb_cache[i].fallback)
			XftFontClose(dpy, fb_cache[i].fallback);
		fb_cache[i].primary = NULL;
		fb_cache[i].fallback = NULL;
	}
	fb_cache_n = 0;
}

/* Walk text[0..length) once, splitting at font changes, and either draw
 * each run (when 'draw' is non-NULL) or only measure.  Returns the total
 * advance width.  Pure-ASCII strings short-circuit to a single Xft call. */
static int walk_runs(Display *dpy, int screen, XftDraw *draw, XftColor *color,
                     XftFont *primary, int x, int y, const char *text, int length)
{
	const FcChar8 *p = (const FcChar8 *)text;
	const FcChar8 *end = p + length;
	const FcChar8 *run;
	XftFont *use;
	XGlyphInfo ext;
	FcChar32 cp;
	int n, total = 0;
	Bool ascii = True;

	for (n = 0; n < length; n++) {
		if ((unsigned char)text[n] > 0x7F) {
			ascii = False;
			break;
		}
	}
	if (ascii) {
		if (draw)
			XftDrawStringUtf8(draw, color, primary, x, y, p, length);
		XftTextExtentsUtf8(dpy, primary, p, length, &ext);
		return ext.xOff;
	}

	while (p < end) {
		n = FcUtf8ToUcs4(p, &cp, (int)(end - p));
		if (n <= 0)
			break;
		use = pick_font_for_cp(dpy, screen, primary, cp);

		/* extend the run while the chosen font stays the same */
		run = p;
		p += n;
		while (p < end) {
			n = FcUtf8ToUcs4(p, &cp, (int)(end - p));
			if (n <= 0 || pick_font_for_cp(dpy, screen, primary, cp) != use)
				break;
			p += n;
		}

		if (draw)
			XftDrawStringUtf8(draw, color, use, x + total, y, run, (int)(p - run));
		XftTextExtentsUtf8(dpy, use, run, (int)(p - run), &ext);
		total += ext.xOff;
	}
	return total;
}
#endif /* !USE_PANGO */

static FcPattern *xlfdToFcPattern(const char *xlfd)
{
	FcPattern *pattern;
	char *fname, *ptr;

	/* Just skip old font names that contain %d in them.
	 * We don't support that anymore. */
	if (strchr(xlfd, '%') != NULL)
		return FcNameParse((FcChar8 *) DEFAULT_FONT);

	fname = wstrdup(xlfd);
	if ((ptr = strchr(fname, ','))) {
		*ptr = 0;
	}
	pattern = XftXlfdParse(fname, False, False);
	wfree(fname);

	if (!pattern) {
		wwarning(_("invalid font: %s. Trying '%s'"), xlfd, DEFAULT_FONT);
		pattern = FcNameParse((FcChar8 *) DEFAULT_FONT);
	}

	return pattern;
}

static char *xlfdToFcName(const char *xlfd)
{
	FcPattern *pattern;
	char *fname;

	pattern = xlfdToFcPattern(xlfd);
	fname = (char *)FcNameUnparse(pattern);
	FcPatternDestroy(pattern);

	return fname;
}

static Bool hasProperty(FcPattern * pattern, const char *property)
{
	FcValue val;

	if (FcPatternGet(pattern, property, 0, &val) == FcResultMatch) {
		return True;
	}

	return False;
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

	result = (char *)FcNameUnparse(pattern);
	FcPatternDestroy(pattern);

	return result;
}

WMFont *WMCreateFont(WMScreen * scrPtr, const char *fontName)
{
	Display *display = scrPtr->display;
	WMFont *font;
	char *fname;
#ifdef USE_PANGO
	PangoFontMap *fontmap;
	PangoContext *context;
	PangoLayout *layout;
	FcPattern *pattern;
	PangoFontDescription *description;
	double size;
#endif

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

	font->font = XftFontOpenName(display, scrPtr->screen, fname);
	if (!font->font) {
		wfree(font);
		wfree(fname);
		return NULL;
	}

	font->height = font->font->ascent + font->font->descent;
	font->y = font->font->ascent;

	font->refCount = 1;

	font->name = fname;

#ifdef USE_PANGO
	fontmap = pango_xft_get_font_map(scrPtr->display, scrPtr->screen);
	context = pango_font_map_create_context(fontmap);
	layout = pango_layout_new(context);

	pattern = FcNameParse((FcChar8 *) font->name);
	description = pango_fc_font_description_from_pattern(pattern, FALSE);

	/* Pango examines FC_SIZE but not FC_PIXEL_SIZE of the patten, but
	 * font-name has only "pixelsize", so set the size manually here.
	 */
	if (FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &size) == FcResultMatch)
		pango_font_description_set_absolute_size(description, size * PANGO_SCALE);

	pango_layout_set_font_description(layout, description);

	font->layout = layout;

	pango_font_description_free(description);
	g_object_unref(context);
	FcPatternDestroy(pattern);
#endif

	assert(WMHashInsert(scrPtr->fontCache, font->name, font) == NULL);

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
		XftFontClose(font->screen->display, font->font);
#ifdef USE_PANGO
		if (font->layout) {
			g_object_unref(font->layout);
		}
#endif
		if (font->name) {
			WMHashRemove(font->screen->fontCache, font->name);
			wfree(font->name);
		}
		wfree(font);
	}
}

Bool WMIsAntialiasingEnabled(WMScreen * scrPtr)
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

int WMWidthOfString(WMFont * font, const char *text, int length)
{
#ifdef USE_PANGO
	const char *previous_text;
	int width;
#endif

	wassertrv(font != NULL && text != NULL, 0);
#ifdef USE_PANGO
	previous_text = pango_layout_get_text(font->layout);
	if ((previous_text == NULL) || (strncmp(text, previous_text, length) != 0) || previous_text[length] != '\0')
		pango_layout_set_text(font->layout, text, length);
	pango_layout_get_pixel_size(font->layout, &width, NULL);

	return width;
#else
	return walk_runs(font->screen->display, font->screen->screen, NULL, NULL,
	                 font->font, 0, 0, text, length);
#endif
}

void WMScreenAbandonXftDrawable(WMScreen *scr, Drawable d)
{
	if (!scr || !scr->xftdraw)
		return;
	if (XftDrawDrawable(scr->xftdraw) == d)
		XftDrawChange(scr->xftdraw, W_DRAWABLE(scr));
}

void WMDrawString(WMScreen * scr, Drawable d, WMColor * color, WMFont * font, int x, int y, const char *text, int length)
{
	XftColor xftcolor;
#ifdef USE_PANGO
	const char *previous_text;
#endif

	wassertr(font != NULL);

	xftcolor.color.red = color->color.red;
	xftcolor.color.green = color->color.green;
	xftcolor.color.blue = color->color.blue;
	xftcolor.color.alpha = color->alpha;;
	xftcolor.pixel = W_PIXEL(color);

	XftDrawChange(scr->xftdraw, d);

#ifdef USE_PANGO
	previous_text = pango_layout_get_text(font->layout);
	if ((previous_text == NULL) || (strncmp(text, previous_text, length) != 0) || previous_text[length] != '\0')
		pango_layout_set_text(font->layout, text, length);
	pango_xft_render_layout(scr->xftdraw, &xftcolor, font->layout, x * PANGO_SCALE, y * PANGO_SCALE);
#else
	walk_runs(scr->display, scr->screen, scr->xftdraw, &xftcolor,
	          font->font, x, y + font->y, text, length);
#endif
}

void
WMDrawImageString(WMScreen * scr, Drawable d, WMColor * color, WMColor * background,
		  WMFont * font, int x, int y, const char *text, int length)
{
	XftColor textColor;
	XftColor bgColor;
#ifdef USE_PANGO
	const char *previous_text;
#endif

	wassertr(font != NULL);

	textColor.color.red = color->color.red;
	textColor.color.green = color->color.green;
	textColor.color.blue = color->color.blue;
	textColor.color.alpha = color->alpha;;
	textColor.pixel = W_PIXEL(color);

	bgColor.color.red = background->color.red;
	bgColor.color.green = background->color.green;
	bgColor.color.blue = background->color.blue;
	bgColor.color.alpha = background->alpha;;
	bgColor.pixel = W_PIXEL(background);

	XftDrawChange(scr->xftdraw, d);

	XftDrawRect(scr->xftdraw, &bgColor, x, y, WMWidthOfString(font, text, length), font->height);

#ifdef USE_PANGO
	previous_text = pango_layout_get_text(font->layout);
	if ((previous_text == NULL) || (strncmp(text, previous_text, length) != 0) || previous_text[length] != '\0')
		pango_layout_set_text(font->layout, text, length);
	pango_xft_render_layout(scr->xftdraw, &textColor, font->layout, x * PANGO_SCALE, y * PANGO_SCALE);
#else
	walk_runs(scr->display, scr->screen, scr->xftdraw, &textColor,
	          font->font, x, y + font->y, text, length);
#endif
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

	name = (char *)FcNameUnparse(pattern);
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

#ifndef USE_PANGO
	/* Stale per-character fallbacks were keyed on primary XftFont handles
	 * that the detach above orphaned; drop them so they are re-matched. */
	fb_cache_reset(scr->display);
#endif
}
