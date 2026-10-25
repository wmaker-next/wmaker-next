#!/usr/bin/env python3
"""
gen-data.py — generate wmglyph_data.{c,h} from UnicodeData.txt + Blocks.txt.

Parses the Unicode Character Database and emits a compact C data table
for wmglyph's glyph selector.

Usage:
    cd util/wmglyph-data
    ./gen-data.py

Inputs (in the same directory):
    UnicodeData.txt  — Unicode 18.0.0 UCD
    Blocks.txt       — Unicode 18.0.0 block ranges

Outputs:
    wmglyph_data.c   — generated data tables (committed to the repo)
    wmglyph_data.h   — generated header (committed to the repo)

The Unicode data files are © 1991-2026 Unicode, Inc. and are distributed
under the Unicode License v3 (see UNICODE-LICENSE in this directory).
The generated files are derived works and are likewise subject to the
Unicode License v3.

Hangul syllables and the BMP CJK ideographs exist in UnicodeData.txt
only as <First>/<Last> range pairs with algorithmic names. Those pairs
are expanded by formula (jamo composition, resp. "CJK UNIFIED
IDEOGRAPH-XXXX"). Surrogates, private use, and the astral ideograph
ranges (CJK extensions B onwards, Tangut, Jurchen, Seal) are not
expanded.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# Short jamo names used by the algorithmic Hangul syllable names.
HANGUL_LEADING = ("G", "GG", "N", "D", "DD", "R", "M", "B", "BB", "S", "SS",
                  "", "J", "JJ", "C", "K", "T", "P", "H")
HANGUL_MEDIAL = ("A", "AE", "YA", "YAE", "EO", "E", "YEO", "YE", "O", "WA",
                 "WAE", "OE", "YO", "U", "WEO", "WE", "WI", "YU", "EU", "YI",
                 "I")
HANGUL_TRAILING = ("", "G", "GG", "GS", "N", "NJ", "NH", "D", "L", "LG", "LM",
                   "LB", "LS", "LT", "LP", "LH", "M", "B", "BS", "S", "SS",
                   "NG", "J", "C", "K", "T", "P", "H")

# <First>/<Last> range pairs to expand, keyed by the name in the markers.
EXPANDED_RANGES = {
    "CJK Ideograph": "unified",
    "CJK Ideograph Extension A": "unified",
    "Hangul Syllable": "hangul",
}


def parse_blocks(path):
    blocks = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            # Format: "XXXX..XXXX; Block Name"
            rng, name = line.split(";", 1)
            start_s, end_s = rng.split("..")
            blocks.append((int(start_s, 16), int(end_s, 16), name.strip()))
    blocks.sort(key=lambda b: b[0])
    return blocks


def hangul_syllable_name(cp):
    sindex = cp - 0xAC00
    leading = HANGUL_LEADING[sindex // 588]
    medial = HANGUL_MEDIAL[(sindex % 588) // 28]
    trailing = HANGUL_TRAILING[sindex % 28]
    return "HANGUL SYLLABLE " + leading + medial + trailing


def range_char_name(family, cp):
    if family == "hangul":
        return hangul_syllable_name(cp)
    return "CJK UNIFIED IDEOGRAPH-%04X" % cp


def parse_chars(path):
    chars = []
    pending = None    # (first_cp, family, label) of a <..., First> marker
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line:
                continue
            fields = line.split(";")
            cp = int(fields[0], 16)
            name = fields[1]
            cat = fields[2]                       # general category
            if not name.startswith("<"):
                # Skip surrogates
                if 0xD800 <= cp <= 0xDFFF:
                    continue
                combining = cat in ("Mn", "Mc", "Me")
                chars.append((cp, name, combining))
            elif name.endswith(", First>"):
                label = name[1:-8]
                pending = (cp, EXPANDED_RANGES.get(label), label)
            elif name.endswith(", Last>"):
                label = name[1:-7]
                if not pending or pending[2] != label:
                    continue
                first_cp, family, _ = pending
                pending = None
                if family:
                    for r in range(first_cp, cp + 1):
                        chars.append((r, range_char_name(family, r), False))
                else:
                    print("Skipped range %s (U+%04X..U+%04X)"
                          % (label, first_cp, cp))
            # <control> and other markers are skipped
    chars.sort(key=lambda c: c[0])
    return chars


def find_block(cp, blocks):
    """Binary search for the block containing cp. Returns index or -1."""
    lo, hi = 0, len(blocks) - 1
    while lo <= hi:
        mid = (lo + hi) // 2
        start, end, _ = blocks[mid]
        if cp < start:
            hi = mid - 1
        elif cp > end:
            lo = mid + 1
        else:
            return mid
    return -1


def c_escape(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


def emit_header(f, block_count, char_count):
    f.write("/* Auto-generated from UnicodeData.txt + Blocks.txt (Unicode 18.0.0).\n")
    f.write(" * Do not edit — regenerate with gen-data.py.\n")
    f.write(" * UnicodeData.txt (c) 1991-2026 Unicode, Inc. — see UNICODE-LICENSE.\n")
    f.write(" */\n\n")
    f.write("#ifndef WMGLYPH_DATA_H\n")
    f.write("#define WMGLYPH_DATA_H\n\n")
    f.write("#include <stdint.h>\n\n")
    f.write("struct WmglyphBlock {\n")
    f.write("    uint32_t start;\n")
    f.write("    uint32_t end;\n")
    f.write("    const char *name;\n")
    f.write("};\n\n")
    f.write("struct WmglyphChar {\n")
    f.write("    uint32_t cp;\n")
    f.write("    uint16_t block;\n")
    f.write("    uint8_t combining;  /* 1 if general category Mn/Mc/Me */\n")
    f.write("    const char *name;\n")
    f.write("};\n\n")
    f.write("extern const struct WmglyphBlock wmglyph_blocks[];\n")
    f.write("extern const struct WmglyphChar wmglyph_chars[];\n")
    f.write("extern const int wmglyph_block_count;\n")
    f.write("extern const int wmglyph_char_count;\n\n")
    f.write("#define WMGLYPH_BLOCK_COUNT %d\n" % block_count)
    f.write("#define WMGLYPH_CHAR_COUNT %d\n" % char_count)
    f.write("\n#endif /* WMGLYPH_DATA_H */\n")


def emit_source(f, blocks, chars):
    f.write("/* Auto-generated from UnicodeData.txt + Blocks.txt (Unicode 18.0.0).\n")
    f.write(" * Do not edit — regenerate with gen-data.py.\n")
    f.write(" * UnicodeData.txt (c) 1991-2026 Unicode, Inc. — see UNICODE-LICENSE.\n")
    f.write(" */\n\n")
    f.write('#include "wmglyph_data.h"\n\n')

    f.write("const struct WmglyphBlock wmglyph_blocks[WMGLYPH_BLOCK_COUNT] = {\n")
    for start, end, name in blocks:
        f.write('\t{ 0x%04X, 0x%04X, "%s" },\n'
                % (start, end, c_escape(name)))
    f.write("};\n\n")

    f.write("const struct WmglyphChar wmglyph_chars[WMGLYPH_CHAR_COUNT] = {\n")
    for cp, block_idx, combining, name in chars:
        f.write('\t{ 0x%04X, %d, %d, "%s" },\n'
                % (cp, block_idx, combining, c_escape(name)))
    f.write("};\n\n")

    f.write("const int wmglyph_block_count = %d;\n" % len(blocks))
    f.write("const int wmglyph_char_count = %d;\n" % len(chars))


def main():
    blocks_path = os.path.join(HERE, "Blocks.txt")
    chars_path = os.path.join(HERE, "UnicodeData.txt")
    header_path = os.path.join(HERE, "wmglyph_data.h")
    source_path = os.path.join(HERE, "wmglyph_data.c")

    blocks = parse_blocks(blocks_path)
    chars = parse_chars(chars_path)

    # Assign block indices
    char_with_block = []
    for cp, name, combining in chars:
        idx = find_block(cp, blocks)
        if idx < 0:
            # Skip chars not in any block (shouldn't happen with @missing coverage)
            continue
        char_with_block.append((cp, idx, combining, name))
    chars = char_with_block

    # Filter blocks with no entries, then sort alphabetically by name
    block_has_chars = [False] * len(blocks)
    for _cp, idx, _combining, _name in chars:
        block_has_chars[idx] = True

    nonempty_blocks = []
    remap = {}
    for i, (start, end, name) in enumerate(blocks):
        if block_has_chars[i]:
            remap[i] = len(nonempty_blocks)
            nonempty_blocks.append((start, end, name))

    # Sort blocks alphabetically by name for the category dropdown
    sorted_blocks = sorted(enumerate(nonempty_blocks), key=lambda x: x[1][2].lower())
    old_to_new = {}
    sorted_nonempty = []
    for new_idx, (old_idx, (start, end, name)) in enumerate(sorted_blocks):
        old_to_new[old_idx] = new_idx
        sorted_nonempty.append((start, end, name))

    chars = [(cp, old_to_new[remap[idx]], combining, name)
             for cp, idx, combining, name in chars]

    print("Blocks: %d total, %d non-empty" % (len(blocks), len(sorted_nonempty)))
    print("Characters: %d (named or algorithmic, non-surrogate, in a non-empty block)"
          % len(chars))

    with open(header_path, "w", encoding="utf-8") as f:
        emit_header(f, len(sorted_nonempty), len(chars))
    with open(source_path, "w", encoding="utf-8") as f:
        emit_source(f, sorted_nonempty, chars)

    print("Wrote %s (%d blocks, %d chars)" % (source_path, len(sorted_nonempty), len(chars)))
    print("Wrote %s" % header_path)


if __name__ == "__main__":
    main()
