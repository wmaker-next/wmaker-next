/* accent_data.c - built-in accent picker character sets
 *
 *  Window Maker window manager
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
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include "wconfig.h"

#ifdef KEEP_XKB_LOCK_STATUS

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <WINGs/WUtil.h>

#include "accent_data.h"

static const APMapping ap_map_belarusian[] = {
	{ "a", (const char *const[]){ "á" }, 1 },
	{ "c", (const char *const[]){ "ć" }, 1 },
	{ "e", (const char *const[]){ "é" }, 1 },
	{ "l", (const char *const[]){ "ł" }, 1 },
	{ "n", (const char *const[]){ "ń" }, 1 },
	{ "o", (const char *const[]){ "ó" }, 1 },
	{ "s", (const char *const[]){ "ś" }, 1 },
	{ "z", (const char *const[]){ "ź", "ż" }, 2 },
	{ "u", (const char *const[]){ "ú" }, 1 },
};

static const APMapping ap_map_belarusian_cyrillic[] = {
	{ "г", (const char *const[]){ "ґ" }, 1 },
	{ "е", (const char *const[]){ "ё" }, 1 },
	{ "и", (const char *const[]){ "й" }, 1 },
	{ "у", (const char *const[]){ "ў" }, 1 },
	{ "ь", (const char *const[]){ "ъ" }, 1 },
};

static const APMapping ap_map_bulgarian[] = {
	{ "а", (const char *const[]){ "ъ" }, 1 },
	{ "е", (const char *const[]){ "ё" }, 1 },
	{ "и", (const char *const[]){ "ѝ" }, 1 },
};

static const APMapping ap_map_catalan[] = {
	{ "a", (const char *const[]){ "à", "á" }, 2 },
	{ "c", (const char *const[]){ "ç" }, 1 },
	{ "e", (const char *const[]){ "é", "è" }, 2 },
	{ "i", (const char *const[]){ "í", "ï" }, 2 },
	{ "l", (const char *const[]){ "l·" }, 1 },
	{ "o", (const char *const[]){ "ó", "ò" }, 2 },
	{ "u", (const char *const[]){ "ú", "ü" }, 2 },
};

static const APMapping ap_map_crimean[] = {
	{ "a", (const char *const[]){ "â" }, 1 },
	{ "c", (const char *const[]){ "ç" }, 1 },
	{ "e", (const char *const[]){ "é" }, 1 },
	{ "g", (const char *const[]){ "ğ" }, 1 },
	{ "i", (const char *const[]){ "î" }, 1 },
	{ "n", (const char *const[]){ "ñ" }, 1 },
	{ "o", (const char *const[]){ "ö" }, 1 },
	{ "s", (const char *const[]){ "ş" }, 1 },
	{ "u", (const char *const[]){ "ü" }, 1 },
};

static const APMapping ap_map_croatian[] = {
	{ "c", (const char *const[]){ "č", "ć" }, 2 },
	{ "d", (const char *const[]){ "đ" }, 1 },
	{ "s", (const char *const[]){ "š" }, 1 },
	{ "z", (const char *const[]){ "ž" }, 1 },
};

static const APMapping ap_map_czech[] = {
	{ "a", (const char *const[]){ "á" }, 1 },
	{ "c", (const char *const[]){ "č" }, 1 },
	{ "d", (const char *const[]){ "ď" }, 1 },
	{ "e", (const char *const[]){ "é", "ě" }, 2 },
	{ "i", (const char *const[]){ "í" }, 1 },
	{ "n", (const char *const[]){ "ň" }, 1 },
	{ "o", (const char *const[]){ "ó" }, 1 },
	{ "r", (const char *const[]){ "ř" }, 1 },
	{ "s", (const char *const[]){ "š" }, 1 },
	{ "t", (const char *const[]){ "ť" }, 1 },
	{ "u", (const char *const[]){ "ú", "ů" }, 2 },
	{ "y", (const char *const[]){ "ý" }, 1 },
	{ "z", (const char *const[]){ "ž" }, 1 },
};

static const APMapping ap_map_danish[] = {
	{ "a", (const char *const[]){ "å", "æ" }, 2 },
	{ "e", (const char *const[]){ "é" }, 1 },
	{ "o", (const char *const[]){ "ø" }, 1 },
	{ "u", (const char *const[]){ "ü" }, 1 },
};

static const APMapping ap_map_dutch[] = {
	{ "a", (const char *const[]){ "á", "ä" }, 2 },
	{ "c", (const char *const[]){ "ç" }, 1 },
	{ "e", (const char *const[]){ "é", "è", "ë" }, 3 },
	{ "i", (const char *const[]){ "í", "ï" }, 2 },
	{ "n", (const char *const[]){ "ñ" }, 1 },
	{ "o", (const char *const[]){ "ó", "ö" }, 2 },
	{ "u", (const char *const[]){ "ú", "ü" }, 2 },
};

static const APMapping ap_map_esperanto[] = {
	{ "c", (const char *const[]){ "ĉ" }, 1 },
	{ "g", (const char *const[]){ "ĝ" }, 1 },
	{ "h", (const char *const[]){ "ĥ" }, 1 },
	{ "j", (const char *const[]){ "ĵ" }, 1 },
	{ "s", (const char *const[]){ "ŝ" }, 1 },
	{ "u", (const char *const[]){ "ŭ" }, 1 },
};

static const APMapping ap_map_estonian[] = {
	{ "a", (const char *const[]){ "ä" }, 1 },
	{ "o", (const char *const[]){ "ö" }, 1 },
	{ "s", (const char *const[]){ "š" }, 1 },
	{ "u", (const char *const[]){ "ü" }, 1 },
	{ "z", (const char *const[]){ "ž" }, 1 },
};

static const APMapping ap_map_finnish[] = {
	{ "a", (const char *const[]){ "ä", "å" }, 2 },
	{ "o", (const char *const[]){ "ö" }, 1 },
	{ "s", (const char *const[]){ "š" }, 1 },
	{ "u", (const char *const[]){ "ü" }, 1 },
	{ "z", (const char *const[]){ "ž" }, 1 },
};

static const APMapping ap_map_french[] = {
	{ "a", (const char *const[]){ "à", "â", "æ" }, 3 },
	{ "c", (const char *const[]){ "ç" }, 1 },
	{ "e", (const char *const[]){ "é", "è", "ê", "ë", "€" }, 5 },
	{ "i", (const char *const[]){ "î", "ï" }, 2 },
	{ "o", (const char *const[]){ "ô", "œ" }, 2 },
	{ "u", (const char *const[]){ "ù", "û", "ü" }, 3 },
	{ "y", (const char *const[]){ "ÿ" }, 1 },
};

static const APMapping ap_map_gaeilge[] = {
	{ "a", (const char *const[]){ "á" }, 1 },
	{ "b", (const char *const[]){ "ḃ" }, 1 },
	{ "c", (const char *const[]){ "ċ" }, 1 },
	{ "d", (const char *const[]){ "ḋ" }, 1 },
	{ "e", (const char *const[]){ "é" }, 1 },
	{ "f", (const char *const[]){ "ḟ" }, 1 },
	{ "g", (const char *const[]){ "ġ" }, 1 },
	{ "i", (const char *const[]){ "í" }, 1 },
	{ "m", (const char *const[]){ "ṁ" }, 1 },
	{ "n", (const char *const[]){ "ṅ" }, 1 },
	{ "o", (const char *const[]){ "ó" }, 1 },
	{ "p", (const char *const[]){ "ṗ" }, 1 },
	{ "s", (const char *const[]){ "ṡ" }, 1 },
	{ "t", (const char *const[]){ "ṫ" }, 1 },
	{ "u", (const char *const[]){ "ú" }, 1 },
};

static const APMapping ap_map_gaidhlig[] = {
	{ "a", (const char *const[]){ "à" }, 1 },
	{ "b", (const char *const[]){ "ḃ" }, 1 },
	{ "c", (const char *const[]){ "ċ" }, 1 },
	{ "d", (const char *const[]){ "ḋ" }, 1 },
	{ "e", (const char *const[]){ "è" }, 1 },
	{ "f", (const char *const[]){ "ḟ" }, 1 },
	{ "g", (const char *const[]){ "ġ" }, 1 },
	{ "i", (const char *const[]){ "ì" }, 1 },
	{ "m", (const char *const[]){ "ṁ" }, 1 },
	{ "o", (const char *const[]){ "ò" }, 1 },
	{ "p", (const char *const[]){ "ṗ" }, 1 },
	{ "s", (const char *const[]){ "ṡ" }, 1 },
	{ "t", (const char *const[]){ "ṫ" }, 1 },
	{ "u", (const char *const[]){ "ù" }, 1 },
};

static const APMapping ap_map_german[] = {
	{ "a", (const char *const[]){ "ä" }, 1 },
	{ "o", (const char *const[]){ "ö" }, 1 },
	{ "s", (const char *const[]){ "ß" }, 1 },
	{ "u", (const char *const[]){ "ü" }, 1 },
};

static const APMapping ap_map_greek[] = {
	{ "α", (const char *const[]){ "ά" }, 1 },
	{ "ε", (const char *const[]){ "έ" }, 1 },
	{ "η", (const char *const[]){ "ή" }, 1 },
	{ "ι", (const char *const[]){ "ί", "ϊ", "ΐ" }, 3 },
	{ "ο", (const char *const[]){ "ό" }, 1 },
	{ "υ", (const char *const[]){ "ύ", "ϋ", "ΰ" }, 3 },
	{ "ω", (const char *const[]){ "ώ" }, 1 },
};

static const APMapping ap_map_hebrew[] = {
	{ "א", (const char *const[]){ "אַ", "אָ" }, 2 },
	{ "ב", (const char *const[]){ "בּ" }, 1 },
	{ "ג", (const char *const[]){ "גּ" }, 1 },
	{ "ד", (const char *const[]){ "דּ" }, 1 },
	{ "ה", (const char *const[]){ "הּ" }, 1 },
	{ "ו", (const char *const[]){ "וֹ", "וּ" }, 2 },
	{ "ז", (const char *const[]){ "זּ" }, 1 },
	{ "ח", (const char *const[]){ "חַ" }, 1 },
	{ "ט", (const char *const[]){ "טּ" }, 1 },
	{ "י", (const char *const[]){ "יִ" }, 1 },
	{ "כ", (const char *const[]){ "כּ" }, 1 },
	{ "ל", (const char *const[]){ "לּ" }, 1 },
	{ "מ", (const char *const[]){ "מּ" }, 1 },
	{ "נ", (const char *const[]){ "נּ" }, 1 },
	{ "ס", (const char *const[]){ "סּ" }, 1 },
	{ "ע", (const char *const[]){ "עַ" }, 1 },
	{ "פ", (const char *const[]){ "פּ" }, 1 },
	{ "צ", (const char *const[]){ "צּ" }, 1 },
	{ "ק", (const char *const[]){ "קּ" }, 1 },
	{ "ר", (const char *const[]){ "רּ" }, 1 },
	{ "ש", (const char *const[]){ "שׁ", "שׂ" }, 2 },
	{ "ת", (const char *const[]){ "תּ" }, 1 },
};

static const APMapping ap_map_hungarian[] = {
	{ "a", (const char *const[]){ "á" }, 1 },
	{ "c", (const char *const[]){ "cs" }, 1 },
	{ "e", (const char *const[]){ "é" }, 1 },
	{ "i", (const char *const[]){ "í" }, 1 },
	{ "o", (const char *const[]){ "ó", "ö", "ő" }, 3 },
	{ "u", (const char *const[]){ "ú", "ü", "ű" }, 3 },
};

static const APMapping ap_map_icelandic[] = {
	{ "a", (const char *const[]){ "á", "æ" }, 2 },
	{ "d", (const char *const[]){ "ð" }, 1 },
	{ "e", (const char *const[]){ "é" }, 1 },
	{ "i", (const char *const[]){ "í" }, 1 },
	{ "o", (const char *const[]){ "ó", "ö" }, 2 },
	{ "t", (const char *const[]){ "þ" }, 1 },
	{ "u", (const char *const[]){ "ú" }, 1 },
	{ "y", (const char *const[]){ "ý" }, 1 },
};

static const APMapping ap_map_italian[] = {
	{ "a", (const char *const[]){ "à" }, 1 },
	{ "e", (const char *const[]){ "è", "é" }, 2 },
	{ "i", (const char *const[]){ "ì" }, 1 },
	{ "o", (const char *const[]){ "ò" }, 1 },
	{ "u", (const char *const[]){ "ù" }, 1 },
};

static const APMapping ap_map_kurdish[] = {
	{ "c", (const char *const[]){ "ç" }, 1 },
	{ "e", (const char *const[]){ "ê" }, 1 },
	{ "i", (const char *const[]){ "î" }, 1 },
	{ "o", (const char *const[]){ "ô" }, 1 },
	{ "s", (const char *const[]){ "ş" }, 1 },
	{ "u", (const char *const[]){ "û" }, 1 },
};

static const APMapping ap_map_lithuanian[] = {
	{ "a", (const char *const[]){ "ą" }, 1 },
	{ "c", (const char *const[]){ "č" }, 1 },
	{ "e", (const char *const[]){ "ę", "ė" }, 2 },
	{ "i", (const char *const[]){ "į" }, 1 },
	{ "s", (const char *const[]){ "š" }, 1 },
	{ "u", (const char *const[]){ "ų", "ū" }, 2 },
	{ "z", (const char *const[]){ "ž" }, 1 },
};

static const APMapping ap_map_macedonian[] = {
	{ "г", (const char *const[]){ "ѓ" }, 1 },
	{ "к", (const char *const[]){ "ќ" }, 1 },
	{ "е", (const char *const[]){ "ё" }, 1 },
	{ "и", (const char *const[]){ "ѝ" }, 1 },
};

static const APMapping ap_map_maltese[] = {
	{ "c", (const char *const[]){ "ċ" }, 1 },
	{ "g", (const char *const[]){ "ġ" }, 1 },
	{ "h", (const char *const[]){ "ħ" }, 1 },
	{ "s", (const char *const[]){ "ż" }, 1 },
	{ "z", (const char *const[]){ "ż" }, 1 },
};

static const APMapping ap_map_maori[] = {
	{ "a", (const char *const[]){ "ā" }, 1 },
	{ "e", (const char *const[]){ "ē" }, 1 },
	{ "i", (const char *const[]){ "ī" }, 1 },
	{ "o", (const char *const[]){ "ō" }, 1 },
	{ "u", (const char *const[]){ "ū" }, 1 },
};

static const APMapping ap_map_norwegian[] = {
	{ "a", (const char *const[]){ "å", "æ" }, 2 },
	{ "e", (const char *const[]){ "é" }, 1 },
	{ "o", (const char *const[]){ "ø" }, 1 },
	{ "u", (const char *const[]){ "ü" }, 1 },
};

static const APMapping ap_map_pinyin[] = {
	{ "a", (const char *const[]){ "ā", "á", "ǎ", "à" }, 4 },
	{ "e", (const char *const[]){ "ē", "é", "ě", "è" }, 4 },
	{ "i", (const char *const[]){ "ī", "í", "ǐ", "ì" }, 4 },
	{ "o", (const char *const[]){ "ō", "ó", "ǒ", "ò" }, 4 },
	{ "u", (const char *const[]){ "ū", "ú", "ǔ", "ù" }, 4 },
	{ "ü", (const char *const[]){ "ǖ", "ǘ", "ǚ", "ǜ" }, 4 },
};

static const APMapping ap_map_polish[] = {
	{ "a", (const char *const[]){ "ą" }, 1 },
	{ "c", (const char *const[]){ "ć" }, 1 },
	{ "e", (const char *const[]){ "ę" }, 1 },
	{ "l", (const char *const[]){ "ł" }, 1 },
	{ "n", (const char *const[]){ "ń" }, 1 },
	{ "o", (const char *const[]){ "ó" }, 1 },
	{ "s", (const char *const[]){ "ś" }, 1 },
	{ "z", (const char *const[]){ "ź", "ż" }, 2 },
};

static const APMapping ap_map_portuguese[] = {
	{ "a", (const char *const[]){ "á", "â", "ã", "à" }, 4 },
	{ "c", (const char *const[]){ "ç" }, 1 },
	{ "e", (const char *const[]){ "é", "ê" }, 2 },
	{ "i", (const char *const[]){ "í" }, 1 },
	{ "o", (const char *const[]){ "ó", "ô", "õ" }, 3 },
	{ "u", (const char *const[]){ "ú", "ü" }, 2 },
};

static const APMapping ap_map_pitjantjatjara[] = {
	{ "a", (const char *const[]){ "á" }, 1 },
	{ "i", (const char *const[]){ "í" }, 1 },
	{ "l", (const char *const[]){ "ḻ" }, 1 },
	{ "n", (const char *const[]){ "ṉ" }, 1 },
	{ "r", (const char *const[]){ "ṟ" }, 1 },
	{ "t", (const char *const[]){ "ṯ" }, 1 },
	{ "u", (const char *const[]){ "ú" }, 1 },
};

static const APMapping ap_map_romanian[] = {
	{ "a", (const char *const[]){ "ă", "â", "á" }, 3 },
	{ "e", (const char *const[]){ "é" }, 1 },
	{ "i", (const char *const[]){ "î", "í" }, 2 },
	{ "o", (const char *const[]){ "ó" }, 1 },
	{ "s", (const char *const[]){ "ș" }, 1 },
	{ "t", (const char *const[]){ "ț" }, 1 },
	{ "u", (const char *const[]){ "ú" }, 1 },
};

static const APMapping ap_map_serbian[] = {
	{ "c", (const char *const[]){ "č", "ć" }, 2 },
	{ "d", (const char *const[]){ "đ" }, 1 },
	{ "s", (const char *const[]){ "š" }, 1 },
	{ "z", (const char *const[]){ "ž" }, 1 },
};

static const APMapping ap_map_serbian_cyrillic[] = {
	{ "г", (const char *const[]){ "ђ" }, 1 },
	{ "к", (const char *const[]){ "ћ" }, 1 },
	{ "ч", (const char *const[]){ "џ" }, 1 },
	{ "е", (const char *const[]){ "ё" }, 1 },
	{ "и", (const char *const[]){ "ј" }, 1 },
};

static const APMapping ap_map_slovak[] = {
	{ "a", (const char *const[]){ "á", "ä" }, 2 },
	{ "c", (const char *const[]){ "č" }, 1 },
	{ "d", (const char *const[]){ "ď" }, 1 },
	{ "e", (const char *const[]){ "é" }, 1 },
	{ "i", (const char *const[]){ "í" }, 1 },
	{ "l", (const char *const[]){ "ĺ", "ľ" }, 2 },
	{ "n", (const char *const[]){ "ň" }, 1 },
	{ "o", (const char *const[]){ "ó", "ô" }, 2 },
	{ "r", (const char *const[]){ "ŕ" }, 1 },
	{ "s", (const char *const[]){ "š" }, 1 },
	{ "t", (const char *const[]){ "ť" }, 1 },
	{ "u", (const char *const[]){ "ú" }, 1 },
	{ "y", (const char *const[]){ "ý" }, 1 },
	{ "z", (const char *const[]){ "ž" }, 1 },
};

static const APMapping ap_map_slovenian[] = {
	{ "c", (const char *const[]){ "č", "ć" }, 2 },
	{ "s", (const char *const[]){ "š" }, 1 },
	{ "z", (const char *const[]){ "ž" }, 1 },
};

static const APMapping ap_map_spanish[] = {
	{ "a", (const char *const[]){ "á" }, 1 },
	{ "e", (const char *const[]){ "é" }, 1 },
	{ "i", (const char *const[]){ "í" }, 1 },
	{ "n", (const char *const[]){ "ñ" }, 1 },
	{ "o", (const char *const[]){ "ó" }, 1 },
	{ "u", (const char *const[]){ "ú", "ü" }, 2 },
	{ "?", (const char *const[]){ "¿" }, 1 },
	{ "!", (const char *const[]){ "¡" }, 1 },
};

static const APMapping ap_map_swedish[] = {
	{ "a", (const char *const[]){ "å", "ä" }, 2 },
	{ "e", (const char *const[]){ "é" }, 1 },
	{ "o", (const char *const[]){ "ö" }, 1 },
	{ "u", (const char *const[]){ "ü" }, 1 },
};

static const APMapping ap_map_turkish[] = {
	{ "c", (const char *const[]){ "ç" }, 1 },
	{ "g", (const char *const[]){ "ğ" }, 1 },
	{ "i", (const char *const[]){ "ı", "İ" }, 2 },
	{ "o", (const char *const[]){ "ö" }, 1 },
	{ "s", (const char *const[]){ "ş" }, 1 },
	{ "u", (const char *const[]){ "ü" }, 1 },
};

static const APMapping ap_map_vietnamese[] = {
	{ "a", (const char *const[]){ "à", "á", "ả", "ã", "ạ", "ă", "ằ", "ắ", "ẳ", "ẵ", "ặ", "â", "ầ", "ấ", "ẩ", "ẫ", "ậ" }, 17 },
	{ "e", (const char *const[]){ "è", "é", "ẻ", "ẽ", "ẹ", "ê", "ề", "ế", "ể", "ễ", "ệ" }, 11 },
	{ "i", (const char *const[]){ "ì", "í", "ỉ", "ĩ", "ị" }, 5 },
	{ "o", (const char *const[]){ "ò", "ó", "ỏ", "õ", "ọ", "ô", "ồ", "ố", "ổ", "ỗ", "ộ", "ơ", "ờ", "ớ", "ở", "ỡ", "ợ" }, 17 },
	{ "u", (const char *const[]){ "ù", "ú", "ủ", "ũ", "ụ", "ư", "ừ", "ứ", "ử", "ữ", "ự" }, 11 },
	{ "y", (const char *const[]){ "ỳ", "ý", "ỷ", "ỹ", "ỵ" }, 5 },
	{ "d", (const char *const[]){ "đ" }, 1 },
};

static const APMapping ap_map_welsh[] = {
	{ "a", (const char *const[]){ "â", "á", "à", "ä" }, 4 },
	{ "e", (const char *const[]){ "ê", "é", "è", "ë" }, 4 },
	{ "i", (const char *const[]){ "î", "í", "ì", "ï" }, 4 },
	{ "o", (const char *const[]){ "ô", "ó", "ò", "ö" }, 4 },
	{ "u", (const char *const[]){ "û", "ú", "ù", "ü" }, 4 },
	{ "w", (const char *const[]){ "ŵ" }, 1 },
	{ "y", (const char *const[]){ "ŷ", "ý", "ỳ", "ÿ" }, 4 },
};

static const APMapping ap_map_special[] = {
	{ "0", (const char *const[]){ "⁰", "₀" }, 2 },
	{ "1", (const char *const[]){ "¹", "₁", "½", "⅓", "¼" }, 5 },
	{ "2", (const char *const[]){ "²", "₂", "⅔" }, 3 },
	{ "3", (const char *const[]){ "³", "₃", "¾" }, 3 },
	{ "4", (const char *const[]){ "⁴", "₄", "⅛" }, 3 },
	{ "5", (const char *const[]){ "⁵", "₅", "⅜" }, 3 },
	{ "6", (const char *const[]){ "⁶", "₆", "⅝" }, 3 },
	{ "7", (const char *const[]){ "⁷", "₇", "⅞" }, 3 },
	{ "8", (const char *const[]){ "⁸", "₈" }, 2 },
	{ "9", (const char *const[]){ "⁹", "₉" }, 2 },
	{ "+", (const char *const[]){ "±", "⊕", "∑" }, 3 },
	{ "-", (const char *const[]){ "–", "—", "−", "‐", "‑" }, 5 },
	{ "=", (const char *const[]){ "≠", "≈", "≡", "≤", "≥" }, 5 },
	{ "<", (const char *const[]){ "≤", "«", "‹", "⟨" }, 4 },
	{ ">", (const char *const[]){ "≥", "»", "›", "⟩" }, 4 },
	{ ",", (const char *const[]){ "„", "‚", "‹", "«" }, 4 },
	{ ".", (const char *const[]){ "…", "·", "•", "‥" }, 4 },
	{ "/", (const char *const[]){ "÷", "⁄", "∕" }, 3 },
	{ "?", (const char *const[]){ "¿" }, 1 },
	{ "!", (const char *const[]){ "¡" }, 1 },
	{ "*", (const char *const[]){ "×", "✕", "✖" }, 3 },
	{ "\"", (const char *const[]){ "“", "”", "„", "‟" }, 4 },
	{ "'", (const char *const[]){ "‘", "’", "‚", "‛" }, 4 },
	{ "(", (const char *const[]){ "[", "{", "⟨" }, 3 },
	{ ")", (const char *const[]){ "]", "}", "⟩" }, 3 },
	{ "[", (const char *const[]){ "(", "{" }, 2 },
	{ "]", (const char *const[]){ ")", "}" }, 2 },
	{ ":", (const char *const[]){ "∶", "÷" }, 2 },
	{ ";", (const char *const[]){ "⁏" }, 1 },
	{ "%", (const char *const[]){ "‰", "‱" }, 2 },
	{ "$", (const char *const[]){ "€", "£", "¥", "¢" }, 4 },
	{ "&", (const char *const[]){ "∧" }, 1 },
	{ "|", (const char *const[]){ "∣", "‖" }, 2 },
	{ "~", (const char *const[]){ "∼", "≈" }, 2 },
};

static const APMapping ap_map_currency[] = {
	{ "e", (const char *const[]){ "€" }, 1 },
	{ "s", (const char *const[]){ "$" }, 1 },
	{ "c", (const char *const[]){ "¢" }, 1 },
	{ "p", (const char *const[]){ "£", "₽", "₱" }, 3 },
	{ "y", (const char *const[]){ "¥" }, 1 },
	{ "d", (const char *const[]){ "₫" }, 1 },
	{ "r", (const char *const[]){ "₹" }, 1 },
	{ "w", (const char *const[]){ "₩" }, 1 },
	{ "t", (const char *const[]){ "₺" }, 1 },
	{ "b", (const char *const[]){ "฿" }, 1 },
	{ "n", (const char *const[]){ "₪" }, 1 },
	{ "k", (const char *const[]){ "₭" }, 1 },
	{ "f", (const char *const[]){ "₣" }, 1 },
	{ "g", (const char *const[]){ "₲" }, 1 },
	{ "h", (const char *const[]){ "₴" }, 1 },
	{ "l", (const char *const[]){ "₺" }, 1 },
	{ "u", (const char *const[]){ "₡" }, 1 },
};

static const APMapping ap_map_greek_polytonic[] = {
	{ "α", (const char *const[]){ "ἀ", "ἁ", "ἂ", "ἃ", "ἄ", "ἅ", "ἆ", "ἇ", "ᾳ", "ᾴ", "ᾶ", "ᾷ" }, 12 },
	{ "ε", (const char *const[]){ "ἐ", "ἑ", "ἒ", "ἓ", "ἔ", "ἕ" }, 6 },
	{ "η", (const char *const[]){ "ἠ", "ἡ", "ἢ", "ἣ", "ἤ", "ἥ", "ἦ", "ἧ", "ῃ", "ῄ", "ῆ", "ῇ" }, 12 },
	{ "ι", (const char *const[]){ "ἰ", "ἱ", "ἲ", "ἳ", "ἴ", "ἵ", "ἶ", "ἷ", "ὶ", "ί", "ῖ", "ϊ", "ΐ" }, 13 },
	{ "ο", (const char *const[]){ "ὀ", "ὁ", "ὂ", "ὃ", "ὄ", "ὅ" }, 6 },
	{ "υ", (const char *const[]){ "ὐ", "ὑ", "ὒ", "ὓ", "ὔ", "ὕ", "ὖ", "ὗ", "ὺ", "ύ", "ῦ", "ϋ", "ΰ" }, 13 },
	{ "ω", (const char *const[]){ "ὠ", "ὡ", "ὢ", "ὣ", "ὤ", "ὥ", "ὦ", "ὧ", "ῳ", "ῴ", "ῶ", "ῷ" }, 12 },
};

static const APMapping ap_map_ipa[] = {
	{ "a", (const char *const[]){ "ɑ", "ɐ", "æ", "ɶ" }, 4 },
	{ "b", (const char *const[]){ "ɓ", "ʙ" }, 2 },
	{ "c", (const char *const[]){ "ç", "ɕ" }, 2 },
	{ "d", (const char *const[]){ "ɖ", "ð" }, 2 },
	{ "e", (const char *const[]){ "ɛ", "ə", "ɘ", "ɜ", "ɞ" }, 5 },
	{ "f", (const char *const[]){ "ɸ" }, 1 },
	{ "g", (const char *const[]){ "ɡ", "ɣ", "ɢ", "ɠ" }, 4 },
	{ "h", (const char *const[]){ "ʰ", "ɦ", "ħ" }, 3 },
	{ "i", (const char *const[]){ "ɪ", "ɨ", "ɩ" }, 3 },
	{ "j", (const char *const[]){ "ʝ", "ɟ" }, 2 },
	{ "k", (const char *const[]){ "q", "ʞ" }, 2 },
	{ "l", (const char *const[]){ "ɫ", "ɭ", "ʎ", "ɬ", "ɮ" }, 5 },
	{ "m", (const char *const[]){ "ɱ", "ɯ" }, 2 },
	{ "n", (const char *const[]){ "ŋ", "ɲ", "ɳ", "ɴ" }, 4 },
	{ "o", (const char *const[]){ "ɔ", "ɒ", "ɵ" }, 3 },
	{ "p", (const char *const[]){ "ƥ" }, 1 },
	{ "r", (const char *const[]){ "ɾ", "ɹ", "ɻ", "ʁ", "ʀ", "ɽ" }, 6 },
	{ "s", (const char *const[]){ "ʃ", "ʂ", "ɕ" }, 3 },
	{ "t", (const char *const[]){ "θ", "ʈ", "ʇ" }, 3 },
	{ "u", (const char *const[]){ "ʊ", "ʉ", "ɯ" }, 3 },
	{ "v", (const char *const[]){ "ʋ", "ⱱ" }, 2 },
	{ "w", (const char *const[]){ "ʍ", "ɰ" }, 2 },
	{ "x", (const char *const[]){ "χ", "ɣ" }, 2 },
	{ "y", (const char *const[]){ "ʏ", "ɤ" }, 2 },
	{ "z", (const char *const[]){ "ʒ", "ʐ", "ʑ" }, 3 },
};

static const APMapping ap_map_proto_indo_european[] = {
	{ "a", (const char *const[]){ "ā", "á" }, 2 },
	{ "e", (const char *const[]){ "ē", "é" }, 2 },
	{ "i", (const char *const[]){ "ī", "í" }, 2 },
	{ "o", (const char *const[]){ "ō", "ó" }, 2 },
	{ "u", (const char *const[]){ "ū", "ú" }, 2 },
	{ "g", (const char *const[]){ "gʷ", "gʰ", "ǵ" }, 3 },
	{ "k", (const char *const[]){ "kʷ", "ḱ" }, 2 },
	{ "l", (const char *const[]){ "l̥" }, 1 },
	{ "m", (const char *const[]){ "m̥" }, 1 },
	{ "n", (const char *const[]){ "n̥" }, 1 },
	{ "r", (const char *const[]){ "r̥" }, 1 },
	{ "h", (const char *const[]){ "h₁", "h₂", "h₃" }, 3 },
	{ "s", (const char *const[]){ "sḱ" }, 1 },
	{ "b", (const char *const[]){ "bʰ" }, 1 },
	{ "d", (const char *const[]){ "dʰ" }, 1 },
	{ "p", (const char *const[]){ "pʰ" }, 1 },
	{ "t", (const char *const[]){ "tʰ" }, 1 },
};

static const APMapping ap_map_romanization[] = {
	{ "a", (const char *const[]){ "ā", "á", "ǎ", "à" }, 4 },
	{ "e", (const char *const[]){ "ē", "é", "ě", "è" }, 4 },
	{ "i", (const char *const[]){ "ī", "í", "ǐ", "ì" }, 4 },
	{ "o", (const char *const[]){ "ō", "ó", "ǒ", "ò" }, 4 },
	{ "u", (const char *const[]){ "ū", "ú", "ǔ", "ù" }, 4 },
	{ "d", (const char *const[]){ "ḍ" }, 1 },
	{ "g", (const char *const[]){ "ġ", "ğ" }, 2 },
	{ "h", (const char *const[]){ "ḥ", "ḫ" }, 2 },
	{ "n", (const char *const[]){ "ṅ", "ñ", "ṇ" }, 3 },
	{ "r", (const char *const[]){ "ṛ" }, 1 },
	{ "s", (const char *const[]){ "ṣ", "š" }, 2 },
	{ "t", (const char *const[]){ "ṭ" }, 1 },
	{ "z", (const char *const[]){ "ẓ", "ž" }, 2 },
};

static const char *const ap_layouts_belarusian[] = { "by", NULL };
static const char *const ap_layouts_belarusian_cyrillic[] = { "by", NULL };
static const char *const ap_layouts_bulgarian[] = { "bg", NULL };
static const char *const ap_layouts_catalan[] = { "cat", NULL };
static const char *const ap_layouts_croatian[] = { "hr", NULL };
static const char *const ap_layouts_czech[] = { "cz", NULL };
static const char *const ap_layouts_danish[] = { "dk", NULL };
static const char *const ap_layouts_dutch[] = { "nl", "be", NULL };
static const char *const ap_layouts_estonian[] = { "ee", NULL };
static const char *const ap_layouts_finnish[] = { "fi", NULL };
static const char *const ap_layouts_french[] = { "fr", "ca", "ch", "be", NULL };
static const char *const ap_layouts_gaeilge[] = { "ie", NULL };
static const char *const ap_layouts_german[] = { "de", "at", "ch", NULL };
static const char *const ap_layouts_greek[] = { "gr", NULL };
static const char *const ap_layouts_hebrew[] = { "il", NULL };
static const char *const ap_layouts_hungarian[] = { "hu", NULL };
static const char *const ap_layouts_icelandic[] = { "is", NULL };
static const char *const ap_layouts_italian[] = { "it", "ch", NULL };
static const char *const ap_layouts_kurdish[] = { "ku", NULL };
static const char *const ap_layouts_lithuanian[] = { "lt", NULL };
static const char *const ap_layouts_macedonian[] = { "mk", NULL };
static const char *const ap_layouts_maltese[] = { "mt", NULL };
static const char *const ap_layouts_norwegian[] = { "no", NULL };
static const char *const ap_layouts_polish[] = { "pl", NULL };
static const char *const ap_layouts_portuguese[] = { "pt", "br", NULL };
static const char *const ap_layouts_romanian[] = { "ro", NULL };
static const char *const ap_layouts_serbian[] = { "sr", NULL };
static const char *const ap_layouts_serbian_cyrillic[] = { "sr", NULL };
static const char *const ap_layouts_slovak[] = { "sk", NULL };
static const char *const ap_layouts_slovenian[] = { "sl", NULL };
static const char *const ap_layouts_spanish[] = { "es", NULL };
static const char *const ap_layouts_swedish[] = { "se", NULL };
static const char *const ap_layouts_turkish[] = { "tr", NULL };
static const char *const ap_layouts_vietnamese[] = { "vn", NULL };

static const APSet ap_sets[] = {
	{ "Belarusian", "Language", ap_layouts_belarusian, ap_map_belarusian, 9 },
	{ "Belarusian_Cyrillic", "Language", ap_layouts_belarusian_cyrillic, ap_map_belarusian_cyrillic, 5 },
	{ "Bulgarian", "Language", ap_layouts_bulgarian, ap_map_bulgarian, 3 },
	{ "Catalan", "Language", ap_layouts_catalan, ap_map_catalan, 7 },
	{ "Crimean", "Language", NULL, ap_map_crimean, 9 },
	{ "Croatian", "Language", ap_layouts_croatian, ap_map_croatian, 4 },
	{ "Czech", "Language", ap_layouts_czech, ap_map_czech, 13 },
	{ "Danish", "Language", ap_layouts_danish, ap_map_danish, 4 },
	{ "Dutch", "Language", ap_layouts_dutch, ap_map_dutch, 7 },
	{ "Esperanto", "Language", NULL, ap_map_esperanto, 6 },
	{ "Estonian", "Language", ap_layouts_estonian, ap_map_estonian, 5 },
	{ "Finnish", "Language", ap_layouts_finnish, ap_map_finnish, 5 },
	{ "French", "Language", ap_layouts_french, ap_map_french, 7 },
	{ "Gaeilge", "Language", ap_layouts_gaeilge, ap_map_gaeilge, 15 },
	{ "Gaidhlig", "Language", NULL, ap_map_gaidhlig, 14 },
	{ "German", "Language", ap_layouts_german, ap_map_german, 4 },
	{ "Greek", "Language", ap_layouts_greek, ap_map_greek, 7 },
	{ "Hebrew", "Language", ap_layouts_hebrew, ap_map_hebrew, 22 },
	{ "Hungarian", "Language", ap_layouts_hungarian, ap_map_hungarian, 6 },
	{ "Icelandic", "Language", ap_layouts_icelandic, ap_map_icelandic, 8 },
	{ "Italian", "Language", ap_layouts_italian, ap_map_italian, 5 },
	{ "Kurdish", "Language", ap_layouts_kurdish, ap_map_kurdish, 6 },
	{ "Lithuanian", "Language", ap_layouts_lithuanian, ap_map_lithuanian, 7 },
	{ "Macedonian", "Language", ap_layouts_macedonian, ap_map_macedonian, 4 },
	{ "Maltese", "Language", ap_layouts_maltese, ap_map_maltese, 5 },
	{ "Maori", "Language", NULL, ap_map_maori, 5 },
	{ "Norwegian", "Language", ap_layouts_norwegian, ap_map_norwegian, 4 },
	{ "Pinyin", "Language", NULL, ap_map_pinyin, 6 },
	{ "Polish", "Language", ap_layouts_polish, ap_map_polish, 8 },
	{ "Portuguese", "Language", ap_layouts_portuguese, ap_map_portuguese, 6 },
	{ "Pitjantjatjara", "Language", NULL, ap_map_pitjantjatjara, 7 },
	{ "Romanian", "Language", ap_layouts_romanian, ap_map_romanian, 7 },
	{ "Serbian", "Language", ap_layouts_serbian, ap_map_serbian, 4 },
	{ "Serbian_Cyrillic", "Language", ap_layouts_serbian_cyrillic, ap_map_serbian_cyrillic, 5 },
	{ "Slovak", "Language", ap_layouts_slovak, ap_map_slovak, 14 },
	{ "Slovenian", "Language", ap_layouts_slovenian, ap_map_slovenian, 3 },
	{ "Spanish", "Language", ap_layouts_spanish, ap_map_spanish, 8 },
	{ "Swedish", "Language", ap_layouts_swedish, ap_map_swedish, 4 },
	{ "Turkish", "Language", ap_layouts_turkish, ap_map_turkish, 6 },
	{ "Vietnamese", "Language", ap_layouts_vietnamese, ap_map_vietnamese, 7 },
	{ "Welsh", "Language", NULL, ap_map_welsh, 7 },
	{ "Special", "Special", NULL, ap_map_special, 34 },
	{ "Currency", "Special", NULL, ap_map_currency, 17 },
	{ "Greek_Polytonic", "Special", NULL, ap_map_greek_polytonic, 7 },
	{ "IPA", "Special", NULL, ap_map_ipa, 25 },
	{ "Proto_Indo_European", "Special", NULL, ap_map_proto_indo_european, 17 },
	{ "Romanization", "Special", NULL, ap_map_romanization, 13 },
};

typedef struct {
	const char *code;
	const char *const *sets;
	size_t count;
} APLayoutEntry;

static const char *const ap_lm_fr[] = { "French", NULL };
static const char *const ap_lm_ca[] = { "French", NULL };
static const char *const ap_lm_de[] = { "German", NULL };
static const char *const ap_lm_at[] = { "German", NULL };
static const char *const ap_lm_ch[] = { "German", "French", "Italian", NULL };
static const char *const ap_lm_be[] = { "Dutch", "French", NULL };
static const char *const ap_lm_es[] = { "Spanish", NULL };
static const char *const ap_lm_pt[] = { "Portuguese", NULL };
static const char *const ap_lm_br[] = { "Portuguese", NULL };
static const char *const ap_lm_it[] = { "Italian", NULL };
static const char *const ap_lm_nl[] = { "Dutch", NULL };
static const char *const ap_lm_se[] = { "Swedish", NULL };
static const char *const ap_lm_no[] = { "Norwegian", NULL };
static const char *const ap_lm_dk[] = { "Danish", NULL };
static const char *const ap_lm_fi[] = { "Finnish", NULL };
static const char *const ap_lm_is[] = { "Icelandic", NULL };
static const char *const ap_lm_pl[] = { "Polish", NULL };
static const char *const ap_lm_cz[] = { "Czech", NULL };
static const char *const ap_lm_sk[] = { "Slovak", NULL };
static const char *const ap_lm_hu[] = { "Hungarian", NULL };
static const char *const ap_lm_ro[] = { "Romanian", NULL };
static const char *const ap_lm_hr[] = { "Croatian", NULL };
static const char *const ap_lm_sr[] = { "Serbian", NULL };
static const char *const ap_lm_sl[] = { "Slovenian", NULL };
static const char *const ap_lm_lt[] = { "Lithuanian", NULL };
static const char *const ap_lm_ee[] = { "Estonian", NULL };
static const char *const ap_lm_gr[] = { "Greek", NULL };
static const char *const ap_lm_il[] = { "Hebrew", NULL };
static const char *const ap_lm_ie[] = { "Gaeilge", NULL };
static const char *const ap_lm_tr[] = { "Turkish", NULL };
static const char *const ap_lm_cat[] = { "Catalan", NULL };
static const char *const ap_lm_vn[] = { "Vietnamese", NULL };
static const char *const ap_lm_by[] = { "Belarusian", NULL };
static const char *const ap_lm_bg[] = { "Bulgarian", NULL };
static const char *const ap_lm_mk[] = { "Macedonian", NULL };
static const char *const ap_lm_mt[] = { "Maltese", NULL };
static const char *const ap_lm_ku[] = { "Kurdish", NULL };

static const APLayoutEntry ap_layout_map[] = {
	{ "fr", ap_lm_fr, 1 },
	{ "ca", ap_lm_ca, 1 },
	{ "de", ap_lm_de, 1 },
	{ "at", ap_lm_at, 1 },
	{ "ch", ap_lm_ch, 3 },
	{ "be", ap_lm_be, 2 },
	{ "es", ap_lm_es, 1 },
	{ "pt", ap_lm_pt, 1 },
	{ "br", ap_lm_br, 1 },
	{ "it", ap_lm_it, 1 },
	{ "nl", ap_lm_nl, 1 },
	{ "se", ap_lm_se, 1 },
	{ "no", ap_lm_no, 1 },
	{ "dk", ap_lm_dk, 1 },
	{ "fi", ap_lm_fi, 1 },
	{ "is", ap_lm_is, 1 },
	{ "pl", ap_lm_pl, 1 },
	{ "cz", ap_lm_cz, 1 },
	{ "sk", ap_lm_sk, 1 },
	{ "hu", ap_lm_hu, 1 },
	{ "ro", ap_lm_ro, 1 },
	{ "hr", ap_lm_hr, 1 },
	{ "sr", ap_lm_sr, 1 },
	{ "sl", ap_lm_sl, 1 },
	{ "lt", ap_lm_lt, 1 },
	{ "ee", ap_lm_ee, 1 },
	{ "gr", ap_lm_gr, 1 },
	{ "il", ap_lm_il, 1 },
	{ "ie", ap_lm_ie, 1 },
	{ "tr", ap_lm_tr, 1 },
	{ "cat", ap_lm_cat, 1 },
	{ "vn", ap_lm_vn, 1 },
	{ "by", ap_lm_by, 1 },
	{ "bg", ap_lm_bg, 1 },
	{ "mk", ap_lm_mk, 1 },
	{ "mt", ap_lm_mt, 1 },
	{ "ku", ap_lm_ku, 1 },
};
static const char *const ap_lmn_french[] = { "French", NULL };
static const char *const ap_lmn_french__canada_[] = { "French", NULL };
static const char *const ap_lmn_canadian_french[] = { "French", NULL };
static const char *const ap_lmn_german[] = { "German", NULL };
static const char *const ap_lmn_german__switzerland_[] = { "German", "French", "Italian", NULL };
static const char *const ap_lmn_swiss_german[] = { "German", "French", "Italian", NULL };
static const char *const ap_lmn_dutch[] = { "Dutch", NULL };
static const char *const ap_lmn_belgian[] = { "Dutch", "French", NULL };
static const char *const ap_lmn_spanish[] = { "Spanish", NULL };
static const char *const ap_lmn_portuguese[] = { "Portuguese", NULL };
static const char *const ap_lmn_portuguese__brazil_[] = { "Portuguese", NULL };
static const char *const ap_lmn_italian[] = { "Italian", NULL };
static const char *const ap_lmn_swedish[] = { "Swedish", NULL };
static const char *const ap_lmn_norwegian[] = { "Norwegian", NULL };
static const char *const ap_lmn_danish[] = { "Danish", NULL };
static const char *const ap_lmn_finnish[] = { "Finnish", NULL };
static const char *const ap_lmn_icelandic[] = { "Icelandic", NULL };
static const char *const ap_lmn_polish[] = { "Polish", NULL };
static const char *const ap_lmn_czech[] = { "Czech", NULL };
static const char *const ap_lmn_slovak[] = { "Slovak", NULL };
static const char *const ap_lmn_hungarian[] = { "Hungarian", NULL };
static const char *const ap_lmn_romanian[] = { "Romanian", NULL };
static const char *const ap_lmn_croatian[] = { "Croatian", NULL };
static const char *const ap_lmn_serbian[] = { "Serbian", NULL };
static const char *const ap_lmn_serbian__latin_[] = { "Serbian", NULL };
static const char *const ap_lmn_slovenian[] = { "Slovenian", NULL };
static const char *const ap_lmn_lithuanian[] = { "Lithuanian", NULL };
static const char *const ap_lmn_estonian[] = { "Estonian", NULL };
static const char *const ap_lmn_greek[] = { "Greek", NULL };
static const char *const ap_lmn_hebrew[] = { "Hebrew", NULL };
static const char *const ap_lmn_irish[] = { "Gaeilge", NULL };
static const char *const ap_lmn_turkish[] = { "Turkish", NULL };
static const char *const ap_lmn_catalan[] = { "Catalan", NULL };
static const char *const ap_lmn_vietnamese[] = { "Vietnamese", NULL };
static const char *const ap_lmn_belarusian[] = { "Belarusian", NULL };
static const char *const ap_lmn_bulgarian[] = { "Bulgarian", NULL };
static const char *const ap_lmn_macedonian[] = { "Macedonian", NULL };
static const char *const ap_lmn_maltese[] = { "Maltese", NULL };
static const char *const ap_lmn_kurdish[] = { "Kurdish", NULL };

static const APLayoutEntry ap_layout_name_map[] = {
	{ "french", ap_lmn_french, 1 },
	{ "french (canada)", ap_lmn_french__canada_, 1 },
	{ "canadian french", ap_lmn_canadian_french, 1 },
	{ "german", ap_lmn_german, 1 },
	{ "german (switzerland)", ap_lmn_german__switzerland_, 3 },
	{ "swiss german", ap_lmn_swiss_german, 3 },
	{ "dutch", ap_lmn_dutch, 1 },
	{ "belgian", ap_lmn_belgian, 2 },
	{ "spanish", ap_lmn_spanish, 1 },
	{ "portuguese", ap_lmn_portuguese, 1 },
	{ "portuguese (brazil)", ap_lmn_portuguese__brazil_, 1 },
	{ "italian", ap_lmn_italian, 1 },
	{ "swedish", ap_lmn_swedish, 1 },
	{ "norwegian", ap_lmn_norwegian, 1 },
	{ "danish", ap_lmn_danish, 1 },
	{ "finnish", ap_lmn_finnish, 1 },
	{ "icelandic", ap_lmn_icelandic, 1 },
	{ "polish", ap_lmn_polish, 1 },
	{ "czech", ap_lmn_czech, 1 },
	{ "slovak", ap_lmn_slovak, 1 },
	{ "hungarian", ap_lmn_hungarian, 1 },
	{ "romanian", ap_lmn_romanian, 1 },
	{ "croatian", ap_lmn_croatian, 1 },
	{ "serbian", ap_lmn_serbian, 1 },
	{ "serbian (latin)", ap_lmn_serbian__latin_, 1 },
	{ "slovenian", ap_lmn_slovenian, 1 },
	{ "lithuanian", ap_lmn_lithuanian, 1 },
	{ "estonian", ap_lmn_estonian, 1 },
	{ "greek", ap_lmn_greek, 1 },
	{ "hebrew", ap_lmn_hebrew, 1 },
	{ "irish", ap_lmn_irish, 1 },
	{ "turkish", ap_lmn_turkish, 1 },
	{ "catalan", ap_lmn_catalan, 1 },
	{ "vietnamese", ap_lmn_vietnamese, 1 },
	{ "belarusian", ap_lmn_belarusian, 1 },
	{ "bulgarian", ap_lmn_bulgarian, 1 },
	{ "macedonian", ap_lmn_macedonian, 1 },
	{ "maltese", ap_lmn_maltese, 1 },
	{ "kurdish", ap_lmn_kurdish, 1 },
};


typedef struct {
	uint32_t lower;
	uint32_t upper;
} APCasePair;

static const APCasePair ap_case_pairs[] = {
	{ 0x0061, 0x0041 },
	{ 0x0062, 0x0042 },
	{ 0x0063, 0x0043 },
	{ 0x0064, 0x0044 },
	{ 0x0065, 0x0045 },
	{ 0x0066, 0x0046 },
	{ 0x0067, 0x0047 },
	{ 0x0068, 0x0048 },
	{ 0x0069, 0x0049 },
	{ 0x006a, 0x004a },
	{ 0x006b, 0x004b },
	{ 0x006c, 0x004c },
	{ 0x006d, 0x004d },
	{ 0x006e, 0x004e },
	{ 0x006f, 0x004f },
	{ 0x0070, 0x0050 },
	{ 0x0071, 0x0051 },
	{ 0x0072, 0x0052 },
	{ 0x0073, 0x0053 },
	{ 0x0074, 0x0054 },
	{ 0x0075, 0x0055 },
	{ 0x0076, 0x0056 },
	{ 0x0077, 0x0057 },
	{ 0x0078, 0x0058 },
	{ 0x0079, 0x0059 },
	{ 0x007a, 0x005a },
	{ 0x00b5, 0x039c },
	{ 0x00df, 0x1e9e },
	{ 0x00e0, 0x00c0 },
	{ 0x00e1, 0x00c1 },
	{ 0x00e2, 0x00c2 },
	{ 0x00e3, 0x00c3 },
	{ 0x00e4, 0x00c4 },
	{ 0x00e5, 0x00c5 },
	{ 0x00e6, 0x00c6 },
	{ 0x00e7, 0x00c7 },
	{ 0x00e8, 0x00c8 },
	{ 0x00e9, 0x00c9 },
	{ 0x00ea, 0x00ca },
	{ 0x00eb, 0x00cb },
	{ 0x00ec, 0x00cc },
	{ 0x00ed, 0x00cd },
	{ 0x00ee, 0x00ce },
	{ 0x00ef, 0x00cf },
	{ 0x00f0, 0x00d0 },
	{ 0x00f1, 0x00d1 },
	{ 0x00f2, 0x00d2 },
	{ 0x00f3, 0x00d3 },
	{ 0x00f4, 0x00d4 },
	{ 0x00f5, 0x00d5 },
	{ 0x00f6, 0x00d6 },
	{ 0x00f8, 0x00d8 },
	{ 0x00f9, 0x00d9 },
	{ 0x00fa, 0x00da },
	{ 0x00fb, 0x00db },
	{ 0x00fc, 0x00dc },
	{ 0x00fd, 0x00dd },
	{ 0x00fe, 0x00de },
	{ 0x00ff, 0x0178 },
	{ 0x0101, 0x0100 },
	{ 0x0103, 0x0102 },
	{ 0x0105, 0x0104 },
	{ 0x0107, 0x0106 },
	{ 0x0109, 0x0108 },
	{ 0x010b, 0x010a },
	{ 0x010d, 0x010c },
	{ 0x010f, 0x010e },
	{ 0x0111, 0x0110 },
	{ 0x0113, 0x0112 },
	{ 0x0115, 0x0114 },
	{ 0x0117, 0x0116 },
	{ 0x0119, 0x0118 },
	{ 0x011b, 0x011a },
	{ 0x011d, 0x011c },
	{ 0x011f, 0x011e },
	{ 0x0121, 0x0120 },
	{ 0x0123, 0x0122 },
	{ 0x0125, 0x0124 },
	{ 0x0127, 0x0126 },
	{ 0x0129, 0x0128 },
	{ 0x012b, 0x012a },
	{ 0x012d, 0x012c },
	{ 0x012f, 0x012e },
	{ 0x0130, 0x0130 },
	{ 0x0131, 0x0049 },
	{ 0x0133, 0x0132 },
	{ 0x0135, 0x0134 },
	{ 0x0137, 0x0136 },
	{ 0x013a, 0x0139 },
	{ 0x013c, 0x013b },
	{ 0x013e, 0x013d },
	{ 0x0140, 0x013f },
	{ 0x0142, 0x0141 },
	{ 0x0144, 0x0143 },
	{ 0x0146, 0x0145 },
	{ 0x0148, 0x0147 },
	{ 0x014b, 0x014a },
	{ 0x014d, 0x014c },
	{ 0x014f, 0x014e },
	{ 0x0151, 0x0150 },
	{ 0x0153, 0x0152 },
	{ 0x0155, 0x0154 },
	{ 0x0157, 0x0156 },
	{ 0x0159, 0x0158 },
	{ 0x015b, 0x015a },
	{ 0x015d, 0x015c },
	{ 0x015f, 0x015e },
	{ 0x0161, 0x0160 },
	{ 0x0163, 0x0162 },
	{ 0x0165, 0x0164 },
	{ 0x0167, 0x0166 },
	{ 0x0169, 0x0168 },
	{ 0x016b, 0x016a },
	{ 0x016d, 0x016c },
	{ 0x016f, 0x016e },
	{ 0x0171, 0x0170 },
	{ 0x0173, 0x0172 },
	{ 0x0175, 0x0174 },
	{ 0x0177, 0x0176 },
	{ 0x017a, 0x0179 },
	{ 0x017c, 0x017b },
	{ 0x017e, 0x017d },
	{ 0x017f, 0x0053 },
	{ 0x0180, 0x0243 },
	{ 0x0183, 0x0182 },
	{ 0x0185, 0x0184 },
	{ 0x0188, 0x0187 },
	{ 0x018c, 0x018b },
	{ 0x0192, 0x0191 },
	{ 0x0195, 0x01f6 },
	{ 0x0199, 0x0198 },
	{ 0x019a, 0x023d },
	{ 0x019b, 0xa7dc },
	{ 0x019e, 0x0220 },
	{ 0x01a1, 0x01a0 },
	{ 0x01a3, 0x01a2 },
	{ 0x01a5, 0x01a4 },
	{ 0x01a8, 0x01a7 },
	{ 0x01ad, 0x01ac },
	{ 0x01b0, 0x01af },
	{ 0x01b4, 0x01b3 },
	{ 0x01b6, 0x01b5 },
	{ 0x01b9, 0x01b8 },
	{ 0x01bd, 0x01bc },
	{ 0x01bf, 0x01f7 },
	{ 0x01c5, 0x01c4 },
	{ 0x01c6, 0x01c4 },
	{ 0x01c8, 0x01c7 },
	{ 0x01c9, 0x01c7 },
	{ 0x01cb, 0x01ca },
	{ 0x01cc, 0x01ca },
	{ 0x01ce, 0x01cd },
	{ 0x01d0, 0x01cf },
	{ 0x01d2, 0x01d1 },
	{ 0x01d4, 0x01d3 },
	{ 0x01d6, 0x01d5 },
	{ 0x01d8, 0x01d7 },
	{ 0x01da, 0x01d9 },
	{ 0x01dc, 0x01db },
	{ 0x01dd, 0x018e },
	{ 0x01df, 0x01de },
	{ 0x01e1, 0x01e0 },
	{ 0x01e3, 0x01e2 },
	{ 0x01e5, 0x01e4 },
	{ 0x01e7, 0x01e6 },
	{ 0x01e9, 0x01e8 },
	{ 0x01eb, 0x01ea },
	{ 0x01ed, 0x01ec },
	{ 0x01ef, 0x01ee },
	{ 0x01f2, 0x01f1 },
	{ 0x01f3, 0x01f1 },
	{ 0x01f5, 0x01f4 },
	{ 0x01f9, 0x01f8 },
	{ 0x01fb, 0x01fa },
	{ 0x01fd, 0x01fc },
	{ 0x01ff, 0x01fe },
	{ 0x0201, 0x0200 },
	{ 0x0203, 0x0202 },
	{ 0x0205, 0x0204 },
	{ 0x0207, 0x0206 },
	{ 0x0209, 0x0208 },
	{ 0x020b, 0x020a },
	{ 0x020d, 0x020c },
	{ 0x020f, 0x020e },
	{ 0x0211, 0x0210 },
	{ 0x0213, 0x0212 },
	{ 0x0215, 0x0214 },
	{ 0x0217, 0x0216 },
	{ 0x0219, 0x0218 },
	{ 0x021b, 0x021a },
	{ 0x021d, 0x021c },
	{ 0x021f, 0x021e },
	{ 0x0223, 0x0222 },
	{ 0x0225, 0x0224 },
	{ 0x0227, 0x0226 },
	{ 0x0229, 0x0228 },
	{ 0x022b, 0x022a },
	{ 0x022d, 0x022c },
	{ 0x022f, 0x022e },
	{ 0x0231, 0x0230 },
	{ 0x0233, 0x0232 },
	{ 0x023c, 0x023b },
	{ 0x023f, 0x2c7e },
	{ 0x0240, 0x2c7f },
	{ 0x0242, 0x0241 },
	{ 0x0247, 0x0246 },
	{ 0x0249, 0x0248 },
	{ 0x024b, 0x024a },
	{ 0x024d, 0x024c },
	{ 0x024f, 0x024e },
	{ 0x0250, 0x2c6f },
	{ 0x0251, 0x2c6d },
	{ 0x0252, 0x2c70 },
	{ 0x0253, 0x0181 },
	{ 0x0254, 0x0186 },
	{ 0x0256, 0x0189 },
	{ 0x0257, 0x018a },
	{ 0x0259, 0x018f },
	{ 0x025b, 0x0190 },
	{ 0x025c, 0xa7ab },
	{ 0x0260, 0x0193 },
	{ 0x0261, 0xa7ac },
	{ 0x0263, 0x0194 },
	{ 0x0264, 0xa7cb },
	{ 0x0266, 0xa7aa },
	{ 0x0268, 0x0197 },
	{ 0x0269, 0x0196 },
	{ 0x026a, 0xa7ae },
	{ 0x026b, 0x2c62 },
	{ 0x026c, 0xa7ad },
	{ 0x026f, 0x019c },
	{ 0x0271, 0x2c6e },
	{ 0x0272, 0x019d },
	{ 0x0275, 0x019f },
	{ 0x027d, 0x2c64 },
	{ 0x0280, 0x01a6 },
	{ 0x0282, 0xa7c5 },
	{ 0x0283, 0x01a9 },
	{ 0x0287, 0xa7b1 },
	{ 0x0288, 0x01ae },
	{ 0x0289, 0x0244 },
	{ 0x028a, 0x01b1 },
	{ 0x028b, 0x01b2 },
	{ 0x028c, 0x0245 },
	{ 0x0292, 0x01b7 },
	{ 0x029d, 0xa7b2 },
	{ 0x029e, 0xa7b0 },
	{ 0x03ac, 0x0386 },
	{ 0x03ad, 0x0388 },
	{ 0x03ae, 0x0389 },
	{ 0x03af, 0x038a },
	{ 0x03b1, 0x0391 },
	{ 0x03b5, 0x0395 },
	{ 0x03b7, 0x0397 },
	{ 0x03b8, 0x0398 },
	{ 0x03b9, 0x0399 },
	{ 0x03bf, 0x039f },
	{ 0x03c5, 0x03a5 },
	{ 0x03c7, 0x03a7 },
	{ 0x03c9, 0x03a9 },
	{ 0x03ca, 0x03aa },
	{ 0x03cb, 0x03ab },
	{ 0x03cc, 0x038c },
	{ 0x03cd, 0x038e },
	{ 0x03ce, 0x038f },
	{ 0x0430, 0x0410 },
	{ 0x0433, 0x0413 },
	{ 0x0435, 0x0415 },
	{ 0x0438, 0x0418 },
	{ 0x0439, 0x0419 },
	{ 0x043a, 0x041a },
	{ 0x0443, 0x0423 },
	{ 0x0447, 0x0427 },
	{ 0x044a, 0x042a },
	{ 0x044c, 0x042c },
	{ 0x0451, 0x0401 },
	{ 0x0452, 0x0402 },
	{ 0x0453, 0x0403 },
	{ 0x0458, 0x0408 },
	{ 0x045b, 0x040b },
	{ 0x045c, 0x040c },
	{ 0x045d, 0x040d },
	{ 0x045e, 0x040e },
	{ 0x045f, 0x040f },
	{ 0x0491, 0x0490 },
	{ 0x1e03, 0x1e02 },
	{ 0x1e0b, 0x1e0a },
	{ 0x1e0d, 0x1e0c },
	{ 0x1e1f, 0x1e1e },
	{ 0x1e25, 0x1e24 },
	{ 0x1e2b, 0x1e2a },
	{ 0x1e31, 0x1e30 },
	{ 0x1e3b, 0x1e3a },
	{ 0x1e41, 0x1e40 },
	{ 0x1e45, 0x1e44 },
	{ 0x1e47, 0x1e46 },
	{ 0x1e49, 0x1e48 },
	{ 0x1e57, 0x1e56 },
	{ 0x1e5b, 0x1e5a },
	{ 0x1e5f, 0x1e5e },
	{ 0x1e61, 0x1e60 },
	{ 0x1e63, 0x1e62 },
	{ 0x1e6b, 0x1e6a },
	{ 0x1e6d, 0x1e6c },
	{ 0x1e6f, 0x1e6e },
	{ 0x1e93, 0x1e92 },
	{ 0x1ea1, 0x1ea0 },
	{ 0x1ea3, 0x1ea2 },
	{ 0x1ea5, 0x1ea4 },
	{ 0x1ea7, 0x1ea6 },
	{ 0x1ea9, 0x1ea8 },
	{ 0x1eab, 0x1eaa },
	{ 0x1ead, 0x1eac },
	{ 0x1eaf, 0x1eae },
	{ 0x1eb1, 0x1eb0 },
	{ 0x1eb3, 0x1eb2 },
	{ 0x1eb5, 0x1eb4 },
	{ 0x1eb7, 0x1eb6 },
	{ 0x1eb9, 0x1eb8 },
	{ 0x1ebb, 0x1eba },
	{ 0x1ebd, 0x1ebc },
	{ 0x1ebf, 0x1ebe },
	{ 0x1ec1, 0x1ec0 },
	{ 0x1ec3, 0x1ec2 },
	{ 0x1ec5, 0x1ec4 },
	{ 0x1ec7, 0x1ec6 },
	{ 0x1ec9, 0x1ec8 },
	{ 0x1ecb, 0x1eca },
	{ 0x1ecd, 0x1ecc },
	{ 0x1ecf, 0x1ece },
	{ 0x1ed1, 0x1ed0 },
	{ 0x1ed3, 0x1ed2 },
	{ 0x1ed5, 0x1ed4 },
	{ 0x1ed7, 0x1ed6 },
	{ 0x1ed9, 0x1ed8 },
	{ 0x1edb, 0x1eda },
	{ 0x1edd, 0x1edc },
	{ 0x1edf, 0x1ede },
	{ 0x1ee1, 0x1ee0 },
	{ 0x1ee3, 0x1ee2 },
	{ 0x1ee5, 0x1ee4 },
	{ 0x1ee7, 0x1ee6 },
	{ 0x1ee9, 0x1ee8 },
	{ 0x1eeb, 0x1eea },
	{ 0x1eed, 0x1eec },
	{ 0x1eef, 0x1eee },
	{ 0x1ef1, 0x1ef0 },
	{ 0x1ef3, 0x1ef2 },
	{ 0x1ef5, 0x1ef4 },
	{ 0x1ef7, 0x1ef6 },
	{ 0x1ef9, 0x1ef8 },
	{ 0x1f00, 0x1f08 },
	{ 0x1f01, 0x1f09 },
	{ 0x1f02, 0x1f0a },
	{ 0x1f03, 0x1f0b },
	{ 0x1f04, 0x1f0c },
	{ 0x1f05, 0x1f0d },
	{ 0x1f06, 0x1f0e },
	{ 0x1f07, 0x1f0f },
	{ 0x1f10, 0x1f18 },
	{ 0x1f11, 0x1f19 },
	{ 0x1f12, 0x1f1a },
	{ 0x1f13, 0x1f1b },
	{ 0x1f14, 0x1f1c },
	{ 0x1f15, 0x1f1d },
	{ 0x1f20, 0x1f28 },
	{ 0x1f21, 0x1f29 },
	{ 0x1f22, 0x1f2a },
	{ 0x1f23, 0x1f2b },
	{ 0x1f24, 0x1f2c },
	{ 0x1f25, 0x1f2d },
	{ 0x1f26, 0x1f2e },
	{ 0x1f27, 0x1f2f },
	{ 0x1f30, 0x1f38 },
	{ 0x1f31, 0x1f39 },
	{ 0x1f32, 0x1f3a },
	{ 0x1f33, 0x1f3b },
	{ 0x1f34, 0x1f3c },
	{ 0x1f35, 0x1f3d },
	{ 0x1f36, 0x1f3e },
	{ 0x1f37, 0x1f3f },
	{ 0x1f40, 0x1f48 },
	{ 0x1f41, 0x1f49 },
	{ 0x1f42, 0x1f4a },
	{ 0x1f43, 0x1f4b },
	{ 0x1f44, 0x1f4c },
	{ 0x1f45, 0x1f4d },
	{ 0x1f51, 0x1f59 },
	{ 0x1f53, 0x1f5b },
	{ 0x1f55, 0x1f5d },
	{ 0x1f57, 0x1f5f },
	{ 0x1f60, 0x1f68 },
	{ 0x1f61, 0x1f69 },
	{ 0x1f62, 0x1f6a },
	{ 0x1f63, 0x1f6b },
	{ 0x1f64, 0x1f6c },
	{ 0x1f65, 0x1f6d },
	{ 0x1f66, 0x1f6e },
	{ 0x1f67, 0x1f6f },
	{ 0x1f76, 0x1fda },
	{ 0x1f7a, 0x1fea },
	{ 0x2c65, 0x023a },
	{ 0x2c66, 0x023e },
};

#define AP_LAYOUT_CACHE 64

static const APSet *const *layout_sets_for(const char *code, size_t *count)
{
	static const APSet *cache[AP_LAYOUT_CACHE];
	char norm[64];
	size_t i, j, n, len;

	*count = 0;
	if (!code)
		return NULL;

	len = strcspn(code, "(:");
	if (len >= sizeof(norm))
		len = sizeof(norm) - 1;
	for (i = 0; i < len; i++)
		norm[i] = (char)tolower((unsigned char)code[i]);
	norm[len] = '\0';

	n = 0;
	for (i = 0; i < sizeof(ap_layout_map) / sizeof(ap_layout_map[0]); i++) {
		if (strcmp(ap_layout_map[i].code, norm) != 0)
			continue;
		for (j = 0; j < ap_layout_map[i].count && n < AP_LAYOUT_CACHE; j++) {
			const APSet *set = apSetFind(ap_layout_map[i].sets[j]);

			if (set)
				cache[n++] = set;
		}
	}

	/* xkbcommon reports the layout display name ("French"), not the
	 * XKB code ("fr"), so fall back to the name aliases and then to
	 * the base name with any parenthetical qualifier removed */
	if (n == 0) {
		len = strlen(code);
		if (len >= sizeof(norm))
			len = sizeof(norm) - 1;
		for (i = 0; i < len; i++)
			norm[i] = (char)tolower((unsigned char)code[i]);
		norm[len] = '\0';

		for (i = 0; i < sizeof(ap_layout_name_map) / sizeof(ap_layout_name_map[0]); i++) {
			if (strcmp(ap_layout_name_map[i].code, norm) != 0)
				continue;
			for (j = 0; j < ap_layout_name_map[i].count && n < AP_LAYOUT_CACHE; j++) {
				const APSet *set = apSetFind(ap_layout_name_map[i].sets[j]);

				if (set)
					cache[n++] = set;
			}
		}
	}

	if (n == 0) {
		char *paren;

		paren = strchr(norm, '(');
		if (paren) {
			while (paren > norm && paren[-1] == ' ')
				paren--;
			*paren = '\0';
		}
		for (i = 0; i < sizeof(ap_layout_name_map) / sizeof(ap_layout_name_map[0]); i++) {
			if (strcmp(ap_layout_name_map[i].code, norm) != 0)
				continue;
			for (j = 0; j < ap_layout_name_map[i].count && n < AP_LAYOUT_CACHE; j++) {
				const APSet *set = apSetFind(ap_layout_name_map[i].sets[j]);

				if (set)
					cache[n++] = set;
			}
		}
	}

	if (n == 0)
		return NULL;
	*count = n;
	return cache;
}

size_t apSetCount(void)
{
	return sizeof(ap_sets) / sizeof(ap_sets[0]);
}

const APSet *apSetAt(size_t index)
{
	if (index >= apSetCount())
		return NULL;
	return &ap_sets[index];
}

const APSet *apSetFind(const char *name)
{
	size_t i;

	if (!name)
		return NULL;
	for (i = 0; i < apSetCount(); i++)
		if (strcasecmp(ap_sets[i].name, name) == 0)
			return &ap_sets[i];
	return NULL;
}

static bool already_have(const APSet *const *out, size_t count, const APSet *set)
{
	size_t i;

	for (i = 0; i < count; i++)
		if (out[i] == set)
			return true;
	return false;
}

static size_t append_set(const APSet *set, const APSet **out, size_t count,
			 size_t cap)
{
	if (!set || count >= cap || already_have(out, count, set))
		return count;
	out[count] = set;
	return count + 1;
}

static size_t append_layout(const char *code, const APSet **out, size_t count,
			    size_t cap)
{
	const APSet *sets[AP_LAYOUT_CACHE];
	size_t n, i;

	n = 0;
	{
		const APSet *const *live = layout_sets_for(code, &n);

		if (!live)
			return count;
		for (i = 0; i < n; i++)
			sets[i] = live[i];
	}
	for (i = 0; i < n; i++)
		count = append_set(sets[i], out, count, cap);
	return count;
}

static size_t resolve_auto(const char *const *layouts, size_t layout_count,
			   int active_layout, const APSet **out, size_t count,
			   size_t cap)
{
	size_t i;
	int active = active_layout;

	if (layout_count == 0)
		return count;
	if (active < 0 || (size_t)active >= layout_count)
		active = 0;

	count = append_layout(layouts[active], out, count, cap);
	for (i = 0; i < layout_count; i++) {
		if ((int)i == active)
			continue;
		count = append_layout(layouts[i], out, count, cap);
	}
	return count;
}

size_t apResolveSetsCount(const APSet *const *out, size_t out_cap)
{
	size_t i;

	if (!out)
		return 0;
	for (i = 0; i < out_cap; i++)
		if (!out[i])
			break;
	return i;
}

size_t apResolveSets(const char *const *tokens, size_t token_count,
		     const char *const *layouts, size_t layout_count,
		     int active_layout, const APSet **out, size_t out_cap,
		     bool *unknown_name)
{
	size_t i, count = 0;

	if (unknown_name)
		*unknown_name = false;
	if (!out || out_cap == 0)
		return 0;
	if (!tokens)
		token_count = 0;
	memset(out, 0, out_cap * sizeof(*out));

	for (i = 0; i < token_count; i++) {
		const char *token = tokens[i];

		if (!token)
			continue;
		if (strcasecmp(token, "auto") == 0) {
			count = resolve_auto(layouts, layout_count, active_layout,
					     out, count, out_cap);
		} else if (strcasecmp(token, "all") == 0) {
			size_t k;

			for (k = 0; k < apSetCount(); k++)
				count = append_set(apSetAt(k), out, count, out_cap);
		} else {
			const APSet *set = apSetFind(token);

			if (!set) {
				if (unknown_name)
					*unknown_name = true;
				continue;
			}
			count = append_set(set, out, count, out_cap);
		}
	}

	return count;
}

static const APMapping *find_mapping(const APMapping *maps, size_t count,
				     const char *base)
{
	size_t i;

	for (i = 0; i < count; i++)
		if (strcmp(maps[i].base, base) == 0)
			return &maps[i];
	return NULL;
}

static bool list_contains(char *const *list, size_t count, const char *value)
{
	size_t i;

	for (i = 0; i < count; i++)
		if (strcmp(list[i], value) == 0)
			return true;
	return false;
}

uint32_t apToUpper(uint32_t cp)
{
	size_t i;

	for (i = 0; i < sizeof(ap_case_pairs) / sizeof(ap_case_pairs[0]); i++)
		if (ap_case_pairs[i].lower == cp)
			return ap_case_pairs[i].upper;
	return cp;
}

uint32_t apToLower(uint32_t cp)
{
	size_t i;

	if (cp == 0x0130)
		return 'i';
	for (i = 0; i < sizeof(ap_case_pairs) / sizeof(ap_case_pairs[0]); i++)
		if (ap_case_pairs[i].upper == cp)
			return ap_case_pairs[i].lower;
	return cp;
}

static int utf8_decode(const char *s, size_t len, uint32_t *cp)
{
	const unsigned char *p = (const unsigned char *)s;
	uint32_t c;

	if (len == 0)
		return 0;
	if (p[0] < 0x80) {
		*cp = p[0];
		return 1;
	}
	if ((p[0] & 0xe0) == 0xc0) {
		if (len < 2 || (p[1] & 0xc0) != 0x80)
			return -1;
		c = ((uint32_t)(p[0] & 0x1f) << 6) | (p[1] & 0x3f);
		if (c < 0x80)
			return -1;
		*cp = c;
		return 2;
	}
	if ((p[0] & 0xf0) == 0xe0) {
		if (len < 3 || (p[1] & 0xc0) != 0x80 || (p[2] & 0xc0) != 0x80)
			return -1;
		c = ((uint32_t)(p[0] & 0x0f) << 12) |
		    ((uint32_t)(p[1] & 0x3f) << 6) | (p[2] & 0x3f);
		if (c < 0x800 || (c >= 0xd800 && c <= 0xdfff))
			return -1;
		*cp = c;
		return 3;
	}
	if ((p[0] & 0xf8) == 0xf0) {
		if (len < 4 || (p[1] & 0xc0) != 0x80 ||
		    (p[2] & 0xc0) != 0x80 || (p[3] & 0xc0) != 0x80)
			return -1;
		c = ((uint32_t)(p[0] & 0x07) << 18) |
		    ((uint32_t)(p[1] & 0x3f) << 12) |
		    ((uint32_t)(p[2] & 0x3f) << 6) | (p[3] & 0x3f);
		if (c < 0x10000 || c > 0x10ffff)
			return -1;
		*cp = c;
		return 4;
	}
	return -1;
}

static int utf8_encode(uint32_t cp, char *buf)
{
	if (cp < 0x80) {
		buf[0] = (char)cp;
		return 1;
	}
	if (cp < 0x800) {
		buf[0] = (char)(0xc0 | (cp >> 6));
		buf[1] = (char)(0x80 | (cp & 0x3f));
		return 2;
	}
	if (cp < 0x10000) {
		if (cp >= 0xd800 && cp <= 0xdfff)
			return -1;
		buf[0] = (char)(0xe0 | (cp >> 12));
		buf[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
		buf[2] = (char)(0x80 | (cp & 0x3f));
		return 3;
	}
	if (cp <= 0x10ffff) {
		buf[0] = (char)(0xf0 | (cp >> 18));
		buf[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
		buf[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
		buf[3] = (char)(0x80 | (cp & 0x3f));
		return 4;
	}
	return -1;
}

bool apValidUtf8(const char *s)
{
	size_t len, pos = 0;
	uint32_t cp;

	if (!s)
		return false;
	len = strlen(s);
	while (pos < len) {
		int n = utf8_decode(s + pos, len - pos, &cp);

		if (n <= 0)
			return false;
		pos += (size_t)n;
	}
	return true;
}

size_t apUtf8Codepoints(const char *s)
{
	size_t len, pos = 0, count = 0;
	uint32_t cp;

	if (!s)
		return 0;
	len = strlen(s);
	while (pos < len) {
		int n = utf8_decode(s + pos, len - pos, &cp);

		if (n <= 0)
			return count;
		pos += (size_t)n;
		count++;
	}
	return count;
}

char *apUppercaseCopy(const char *s)
{
	size_t len, pos = 0, out = 0;
	char *res;
	uint32_t cp;

	if (!s)
		return NULL;
	len = strlen(s);
	res = wmalloc(len * 4 + 1);
	while (pos < len) {
		int n = utf8_decode(s + pos, len - pos, &cp);
		int m;

		if (n <= 0)
			break;
		cp = apToUpper(cp);
		m = utf8_encode(cp, res + out);
		if (m <= 0)
			break;
		out += (size_t)m;
		pos += (size_t)n;
	}
	res[out] = '\0';
	return res;
}

char *apNormalizeBaseCopy(const char *s, bool *is_letter)
{
	size_t len, pos = 0, out = 0;
	char *res;
	uint32_t cp;

	if (is_letter)
		*is_letter = false;
	if (!s)
		return NULL;
	len = strlen(s);
	res = wmalloc(len * 4 + 1);
	while (pos < len) {
		int n = utf8_decode(s + pos, len - pos, &cp);
		int m;
		uint32_t lo;

		if (n <= 0)
			break;
		lo = apToLower(cp);
		if (is_letter &&
		    (lo != cp || apToUpper(cp) != cp))
			*is_letter = true;
		m = utf8_encode(lo, res + out);
		if (m <= 0)
			break;
		out += (size_t)m;
		pos += (size_t)n;
	}
	res[out] = '\0';
	return res;
}

char **apMergeVariants(const APSet *const *sets, size_t set_count,
		       const APMapping *user, size_t user_count,
		       const char *base, bool uppercase, size_t *out_count)
{
	char **list;
	size_t count = 0, cap = AP_MAX_VARIANTS;
	size_t i, j;

	if (out_count)
		*out_count = 0;
	if (!base)
		return NULL;

	list = wmalloc(cap * sizeof(char *));
	if (user) {
		const APMapping *m = find_mapping(user, user_count, base);

		if (m) {
			for (j = 0; j < m->variant_count && count < cap; j++) {
				const char *v = m->variants[j];

				if (!v || !apValidUtf8(v) ||
				    strlen(v) >= AP_MAX_VARIANT_BYTES)
					continue;
				if (list_contains(list, count, v))
					continue;
				list[count++] = wstrdup(v);
			}
		}
	}
	for (i = 0; i < set_count; i++) {
		const APMapping *m;

		if (!sets[i])
			continue;
		m = find_mapping(sets[i]->mappings, sets[i]->mapping_count, base);
		if (!m)
			continue;
		for (j = 0; j < m->variant_count && count < cap; j++) {
			const char *v = m->variants[j];

			if (!v || !apValidUtf8(v) ||
			    strlen(v) >= AP_MAX_VARIANT_BYTES)
				continue;
			if (list_contains(list, count, v))
				continue;
			list[count++] = wstrdup(v);
		}
	}

	if (uppercase) {
		for (i = 0; i < count; i++) {
			char *up = apUppercaseCopy(list[i]);

			if (!up)
				continue;
			if (strlen(up) >= AP_MAX_VARIANT_BYTES) {
				wfree(up);
				continue;
			}
			wfree(list[i]);
			list[i] = up;
		}
		for (i = 0; i < count; i++) {
			for (j = i + 1; j < count; j++) {
				if (strcmp(list[i], list[j]) == 0) {
					wfree(list[j]);
					memmove(list + j, list + j + 1,
						(count - j - 1) * sizeof(char *));
					count--;
					j--;
				}
			}
		}
	}

	if (count == 0) {
		wfree(list);
		return NULL;
	}
	if (out_count)
		*out_count = count;
	return list;
}

void apFreeVariants(char **variants, size_t count)
{
	size_t i;

	if (!variants)
		return;
	for (i = 0; i < count; i++)
		if (variants[i])
			wfree(variants[i]);
	wfree(variants);
}

#endif /* KEEP_XKB_LOCK_STATUS */
