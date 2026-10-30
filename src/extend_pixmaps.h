/*
 *  extend_pixmaps.h
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 2001-2002 Alfredo K. Kojima
 *  Copyright (c) 2004 Dan Pascu
 *  Copyright (c) 1999-2026 Window Maker Team
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

static char *PRED_XKBGROUP1_XPM[] = {
    "    10    10        2            1",
    ". c #000000",
    "# c None",
    "........##",
    ".......###",
    "..########",
    "..########",
    "......####",
    ".....#####",
    "..########",
    "..########",
    "........##",
    "........##"
};

#ifdef LANGUAGE_TH
static char *PRED_XKBGROUP2_XPM[] = {
    "    10    10        2            1",
    ". c #000000",
    "# c None",
    "#..###..##",
    ".#..#.#..#",
    ".#...###..",
    "#...####..",
    "##..####..",
    "##..####..",
    "##..####..",
    "##..####..",
    "##..####..",
    "##.#####.#"
};
#elif defined LANGUAGE_SK
static char *PRED_XKBGROUP2_XPM[] = {
    "    10    10        2            1",
    ". c #000000",
    "# c None",
    "####..####",
    "####..####",
    "##......##",
    "##......##",
    "####..####",
    "#........#",
    "#........#",
    "####..####",
    "###....###",
    "##......##"};
#elif defined LANGUAGE_EL
static char *PRED_XKBGROUP2_XPM[] = {
    "    10    10        2            1",
    ". c #000000",
    "# c None",
    "##..######",
    "##..##....",
    "......####",
    "##..##....",
    "##..######",
    "..........",
    "##########",
    "..........",
    "##########",
    "##########"};
#else
static char *PRED_XKBGROUP2_XPM[] = {
    "    10    10        2            1",
    ". c #000000",
    "# c None",
    "##########",
    "#...##...#",
    "..........",
    "..........",
    "..........",
    "..........",
    "#........#",
    "##......##",
    "###....###",
    "####..####"
};
#endif

static char *PRED_XKBGROUP3_XPM[] = {
    "    10    10        2            1",
    ". c #000000",
    "# c None",
    "..........",
    "..........",
    "..##..##..",
    "..##..##..",
    "..........",
    "..........",
    "..######..",
    "...####...",
    "....##....",
    ".........."
};

static char *PRED_XKBGROUP4_XPM[] = {
    "    10    10        2            1",
    ". c #000000",
    "# c None",
    "..........",
    ".########.",
    ".#..##..#.",
    ".#..##..#.",
    ".########.",
    ".#......#.",
    ".##....##.",
    ".###..###.",
    ".########.",
    ".........."
};


static char *NEXT_XKBGROUP1_XPM[] = {
"10 10 6 1",
" 	c None",
".	c #E0E0E0",
"+	c #7D7D7D",
"@	c #363636",
"#	c #080808",
"$	c #BDBDBD",
".........+",
"..@@@@@@++",
"..@####+++",
"..#+$$$$++",
"..###+.$++",
"..#++..$++",
"..#$.$$$++",
"..#@@@@+++",
".$+#####@@",
"+++++++@@@"};

static char *NEXT_XKBGROUP2_XPM[] = {
"10 10 6 1",
" 	c None",
".	c #E0E0E0",
"+	c #7D7D7D",
"@	c #BDBDBD",
"#	c #080808",
"$	c #363636",
".........+",
".@##@@$#++",
".#.+#+###+",
".#+######+",
".########+",
"..######++",
"..@####+++",
".@@@#$++++",
".@++#$++$$",
"+++++++$$$"};

static char *NEXT_XKBGROUP3_XPM[] = {
"10 10 6 1",
" 	c None",
".	c #E0E0E0",
"+	c #7D7D7D",
"@	c #BDBDBD",
"#	c #080808",
"$	c #363636",
".........+",
"..@####.++",
".@######.+",
".#.+##.+#+",
".#++##++#+",
".########+",
".##@##@##+",
".+##@@##$+",
".@+####$$$",
"+++++++$$$"};

static char *NEXT_XKBGROUP4_XPM[] = {
"10 10 6 1",
" 	c None",
".	c #E0E0E0",
"+	c #7D7D7D",
"@	c #BDBDBD",
"#	c #080808",
"$	c #363636",
".........+",
"..@####.++",
".@#@@@@#.+",
".##$@@#$#+",
".#$$@@$$#+",
".#@@@@++#+",
".#+#++#.#+",
".+#+##$#$+",
".@+####$$$",
"+++++++$$$"};
