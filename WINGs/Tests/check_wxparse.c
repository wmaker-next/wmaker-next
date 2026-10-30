/*
 *  check_wxparse.c
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 2026 Window Maker Team
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

/* check_wxparse.c - unit tests for the client-side parsers:
 *
 *  - the text-property codec in wxhints.c (W_Latin1ToUtf8 / W_Utf8Next /
 *    W_Utf8ToLatin1), the typed replacement for the Xlib text-list
 *    converters
 *  - the numeric colour grammar in wxcore.c (WXParseNumericColor), the
 *    #rgb and rgb:RR/GG/BB forms of XParseColor
 *  - the property-level codec paths (WXRcvTextPropertyList decode,
 *    WXSetWMName/WXSetCommand encode) against a live server property,
 *    these skip (77) when no display is reachable, like wrlib's
 *    check_xside
 *
 * The codec and colour tests are connection-free by design, the
 * server LookupColor only understands names.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <WINGs/WUtil.h>
#include <WINGs/WXCore.h>
#include <WINGs/WXHints.h>

#include "WINGsP.h"

#include "check_common.h"

/* ---- codec: Latin-1 to UTF-8 ------------------------------------------ */

static void test_latin1_to_utf8(void)
{
	char *s;

	s = W_Latin1ToUtf8((const unsigned char *) "caf\xE9", 5);
	CHECK_STR(s, "caf\xC3\xA9");
	wfree(s);

	/* the F-era live-gate assertion: a Latin-1 WM_NAME "caf\351"
	 * surfaces as UTF-8 c3 a9 */
	s = W_Latin1ToUtf8((const unsigned char *) "\xFF\x80\x41", 3);
	CHECK(s != NULL && memcmp(s, "\xC3\xBF\xC2\x80\x41", 5) == 0
	      && s[5] == '\0');
	wfree(s);

	/* empty and pure-ASCII inputs */
	s = W_Latin1ToUtf8((const unsigned char *) "", 0);
	CHECK_STR(s, "");
	wfree(s);

	s = W_Latin1ToUtf8((const unsigned char *) "abc", 3);
	CHECK_STR(s, "abc");
	wfree(s);
}

/* ---- codec: UTF-8 walk ------------------------------------------------- */

static void test_utf8_next(void)
{
	unsigned int cp;
	const unsigned char c3a9[] = { 0xc3, 0xa9, 0x00 };
	const unsigned char f0[] = { 0xf0, 0x9f, 0x92, 0xa9, 0x00 };

	/* 1..4 byte sequences decode to the codepoint */
	CHECK(W_Utf8Next((const unsigned char *) "a", 1, &cp) == 1 && cp == 'a');
	CHECK(W_Utf8Next(c3a9, 2, &cp) == 2 && cp == 0xe9);
	CHECK(W_Utf8Next((const unsigned char *) "\xE2\x97\x8C", 3, &cp) == 3
	      && cp == 0x25cc);
	CHECK(W_Utf8Next(f0, 4, &cp) == 4 && cp == 0x1f4a9);

	/* malformed: bare continuation, bad lead, truncated tail */
	CHECK(W_Utf8Next((const unsigned char *) "\x80", 1, &cp) == 0);
	CHECK(W_Utf8Next((const unsigned char *) "\xf8\x80", 2, &cp) == 0);
	CHECK(W_Utf8Next((const unsigned char *) "\xc3", 1, &cp) == 0);
	/* bad continuation inside a sequence */
	CHECK(W_Utf8Next((const unsigned char *) "\xe2\x41\x8c", 3, &cp) == 0);
}

/* ---- codec: UTF-8 to Latin-1 (the STRING contract) --------------------- */

static void test_utf8_to_latin1(void)
{
	size_t n;
	char *s;

	s = W_Utf8ToLatin1("caf\xC3\xA9", 5, &n);
	CHECK(s != NULL && n == 4 && memcmp(s, "caf\xE9", 4) == 0);
	wfree(s);

	/* U+00FF is the highest encodable codepoint */
	s = W_Utf8ToLatin1("\xC3\xBF", 2, &n);
	CHECK(s != NULL && n == 1 && (unsigned char) s[0] == 0xff);
	wfree(s);

	/* above U+00FF refuses instead of serving mojibake */
	s = W_Utf8ToLatin1("\xE3\x81\x82", 3, &n);
	CHECK(s == NULL);

	/* malformed input refuses */
	s = W_Utf8ToLatin1("\xC3", 1, &n);
	CHECK(s == NULL);

	s = W_Utf8ToLatin1("", 0, &n);
	CHECK(s != NULL && n == 0 && s[0] == '\0');
	wfree(s);
}

/* ---- colour grammar: #RRR... ------------------------------------------- */

static void test_color_hash(void)
{
	WXColor c;

	/* 1 digit per component scales with 0x1111 */
	CHECK(WXParseNumericColor("#f00", &c));
	CHECK(c.red == 0xffff && c.green == 0x0000 && c.blue == 0x0000);

	/* 2 digits per component scale with 0x101 */
	CHECK(WXParseNumericColor("#123456", &c));
	CHECK(c.red == 0x1212 && c.green == 0x3434 && c.blue == 0x5656);

	/* lowercase hex digits */
	CHECK(WXParseNumericColor("#abcdef", &c));
	CHECK(c.red == 0xabab && c.green == 0xcdcd && c.blue == 0xefef);

	/* 4 digits per component scale with 1 (12 digits total) */
	CHECK(WXParseNumericColor("#123456789012", &c));
	CHECK(c.red == 0x1234 && c.green == 0x5678 && c.blue == 0x9012);

	/* malformed: digit count not divisible by 3, out of range, or a
	 * non-hex character */
	CHECK(!WXParseNumericColor("#", &c));
	CHECK(!WXParseNumericColor("#12", &c));
	CHECK(!WXParseNumericColor("#1234", &c));
	CHECK(!WXParseNumericColor("#1234567890abc", &c));
	CHECK(!WXParseNumericColor("#12g456", &c));
	CHECK(!WXParseNumericColor("", &c));
	CHECK(!WXParseNumericColor(NULL, &c));
}

/* ---- colour grammar: rgb:RR/GG/BB -------------------------------------- */

static void test_color_rgb(void)
{
	WXColor c;

	/* mixed widths: 2, 1, 2 digits */
	CHECK(WXParseNumericColor("rgb:ff/0/80", &c));
	CHECK(c.red == 0xffff && c.green == 0x0000 && c.blue == 0x8080);

	/* uniform widths */
	CHECK(WXParseNumericColor("rgb:12/34/56", &c));
	CHECK(c.red == 0x1212 && c.green == 0x3434 && c.blue == 0x5656);

	CHECK(WXParseNumericColor("rgb:1234/5678/9abc", &c));
	CHECK(c.red == 0x1234 && c.green == 0x5678 && c.blue == 0x9abc);

	/* the prefix is lowercase, Xlib rejects an uppercase rgb: */
	CHECK(!WXParseNumericColor("RGB:1/2/3", &c));

	/* malformed: missing component, empty component, a fifth digit,
	 * an invalid digit, or a trailing character */
	CHECK(!WXParseNumericColor("rgb:", &c));
	CHECK(!WXParseNumericColor("rgb:1/2", &c));
	CHECK(!WXParseNumericColor("rgb:1//2", &c));
	CHECK(!WXParseNumericColor("rgb:12345/0/0", &c));
	CHECK(!WXParseNumericColor("rgb:gg/0/0", &c));
	CHECK(!WXParseNumericColor("rgb:1/2/3/4", &c));
}

/* ---- not a numeric form (the caller's LookupColor fall-through) -------- */

static void test_color_names_rejected(void)
{
	WXColor c;

	CHECK(!WXParseNumericColor("red", &c));
	CHECK(!WXParseNumericColor("gray20", &c));
}

/* ---- property round trips (display-gated) ------------------------------ */

static xcb_connection_t *xc;
static xcb_window_t pwin;

static xcb_get_property_reply_t *wm_prop(xcb_atom_t prop)
{
	xcb_get_property_cookie_t ck;

	ck = xcb_get_property(xc, 0, pwin, prop, XCB_ATOM_ANY, 0, 1000000);
	return xcb_get_property_reply(xc, ck, NULL);
}

static int test_properties(void)
{
	xcb_get_property_reply_t *r;
	xcb_screen_t *scr;
	xcb_atom_t ctext, u8;
	char *s, **list, *argv[3], *sl[1];
	const char *v;
	char trail[4];
	int argc, rawlen;
	uint32_t vlen;

	xc = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(xc)) {
		xcb_disconnect(xc);
		xc = NULL;
		return 77;
	}
	scr = xcb_setup_roots_iterator(xcb_get_setup(xc)).data;
	pwin = xcb_generate_id(xc);
	xcb_create_window(xc, scr->root_depth, pwin, scr->root, 0, 0, 8, 8, 0,
			  XCB_WINDOW_CLASS_INPUT_OUTPUT, scr->root_visual,
			  0, NULL);
	xcb_flush(xc);
	WXStaticAtomsInit(xc);
	ctext = WXInternAtom(xc, "COMPOUND_TEXT");
	u8 = WXInternAtom(xc, "UTF8_STRING");

	/* Latin-1 fits: the wire property is STRING and decodes back;
	 * the encoder appends the trailing NUL (latin_len + 1) */
	WXSetWMName(xc, pwin, "caf\xC3\xA9");
	r = wm_prop(XCB_ATOM_WM_NAME);
	CHECK(r && r->type == XCB_ATOM_STRING && r->format == 8);
	rawlen = r ? xcb_get_property_value_length(r) : -1;
	CHECK(rawlen == 5);
	v = r ? xcb_get_property_value(r) : "";
	CHECK(r && memcmp(v, "caf\xE9\0", 5) == 0);
	free(r);
	s = WXGetWMName(xc, pwin);
	CHECK_STR(s, "caf\xC3\xA9");
	wfree(s);

	/* non-Latin-1 falls back to COMPOUND_TEXT and round-trips */
	WXSetWMName(xc, pwin, "\xE6\x97\xA5\xE6\x9C\xAC");
	r = wm_prop(XCB_ATOM_WM_NAME);
	CHECK(r && r->type == ctext && r->format == 8);
	free(r);
	s = WXGetWMName(xc, pwin);
	CHECK_STR(s, "\xE6\x97\xA5\xE6\x9C\xAC");
	wfree(s);

	/* UTF8_STRING passes through untouched */
	xcb_change_property(xc, XCB_PROP_MODE_REPLACE, pwin,
			    XCB_ATOM_WM_NAME, u8, 8, 4, "h\xC3\xA9y");
	s = WXGetWMName(xc, pwin);
	CHECK_STR(s, "h\xC3\xA9y");
	wfree(s);

	/* a present but empty WM_NAME is a deliberate blank title */
	xcb_change_property(xc, XCB_PROP_MODE_REPLACE, pwin,
			    XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, 0, "");
	s = WXGetWMName(xc, pwin);
	CHECK(s != NULL && strcmp(s, "") == 0);
	wfree(s);

	/* the terminating NUL some clients store is dropped */
	trail[0] = 'a';
	trail[1] = 'b';
	trail[2] = 'c';
	trail[3] = '\0';
	xcb_change_property(xc, XCB_PROP_MODE_REPLACE, pwin,
			    XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, 4, trail);
	s = WXGetWMName(xc, pwin);
	CHECK_STR(s, "abc");
	wfree(s);

	/* an unsupported encoding fails like XConverterNotFound */
	xcb_change_property(xc, XCB_PROP_MODE_REPLACE, pwin,
			    XCB_ATOM_WM_NAME, XCB_ATOM_INTEGER, 8, 1, "1");
	s = WXGetWMName(xc, pwin);
	CHECK(s == NULL);

	/* an absent property is NULL */
	xcb_delete_property(xc, pwin, XCB_ATOM_WM_NAME);
	s = WXGetWMName(xc, pwin);
	CHECK(s == NULL);

	/* WM_COMMAND: the NUL-joined format-8 contract, no encoder
	 * rounds — the argv bytes are the caller's Latin-1 */
	argv[0] = "wmaker";
	argv[1] = "caf\xE9";
	argv[2] = "x";
	WXSetCommand(xc, pwin, argv, 3);
	r = wm_prop(XCB_ATOM_WM_COMMAND);
	CHECK(r && r->type == XCB_ATOM_STRING && r->format == 8);
	rawlen = r ? xcb_get_property_value_length(r) : -1;
	CHECK(rawlen == 14);
	v = r ? xcb_get_property_value(r) : "";
	vlen = r ? (uint32_t) rawlen : 0;
	CHECK(r && v[vlen - 1] == '\0');
	CHECK(r && memchr(v, '\0', vlen - 1) != NULL);
	free(r);
	CHECK(WXGetCommand(xc, pwin, &list, &argc));
	CHECK(argc == 3);
	if (list && argc == 3) {
		CHECK_STR(list[0], "wmaker");
		CHECK_STR(list[1], "caf\xC3\xA9");
		CHECK_STR(list[2], "x");
		WXFreeStringList(list, argc);
	} else if (list) {
		WXFreeStringList(list, argc);
	}

	/* WXSetTextPropertyList writes the caller's Latin-1 bytes with
	 * the trailing NUL (the ICCCM contract), the read side decodes */
	sl[0] = "caf\xE9";
	WXSetTextPropertyList(xc, pwin, XCB_ATOM_WM_COMMAND, sl, 1);
	r = wm_prop(XCB_ATOM_WM_COMMAND);
	CHECK(r && r->type == XCB_ATOM_STRING);
	rawlen = r ? xcb_get_property_value_length(r) : -1;
	CHECK(rawlen == 5);
	v = r ? xcb_get_property_value(r) : "";
	CHECK(r && memcmp(v, "caf\xE9\0", 5) == 0);
	free(r);
	CHECK(WXGetCommand(xc, pwin, &list, &argc));
	CHECK(argc == 1);
	if (list && argc == 1) {
		CHECK_STR(list[0], "caf\xC3\xA9");
		WXFreeStringList(list, argc);
	} else if (list) {
		WXFreeStringList(list, argc);
	}

	xcb_destroy_window(xc, pwin);
	xcb_flush(xc);
	xcb_disconnect(xc);
	xc = NULL;
	return 0;
}

int main(void)
{
	int rc;

	test_latin1_to_utf8();
	test_utf8_next();
	test_utf8_to_latin1();
	test_color_hash();
	test_color_rgb();
	test_color_names_rejected();

	rc = test_properties();
	if (rc == 77) {
		printf("SKIP: no display\n");
		if (check_failures)
			return check_report("check_wxparse");
		return 77;
	}

	return check_report("check_wxparse");
}
