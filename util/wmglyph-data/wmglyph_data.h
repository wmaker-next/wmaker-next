/* Auto-generated from UnicodeData.txt + Blocks.txt (Unicode 18.0.0).
 * Do not edit — regenerate with gen-data.py.
 * UnicodeData.txt (c) 1991-2026 Unicode, Inc. — see UNICODE-LICENSE.
 */

#ifndef WMGLYPH_DATA_H
#define WMGLYPH_DATA_H

#include <stdint.h>

struct WmglyphBlock {
    uint32_t start;
    uint32_t end;
    const char *name;
};

struct WmglyphChar {
    uint32_t cp;
    uint16_t block;
    uint8_t combining;  /* 1 if general category Mn/Mc/Me */
    const char *name;
};

extern const struct WmglyphBlock wmglyph_blocks[];
extern const struct WmglyphChar wmglyph_chars[];
extern const int wmglyph_block_count;
extern const int wmglyph_char_count;

#define WMGLYPH_BLOCK_COUNT 334
#define WMGLYPH_CHAR_COUNT 79988

#endif /* WMGLYPH_DATA_H */
