/*
 *  def_svgs.h
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

#ifndef WMDEFSVGS_H_
#define WMDEFSVGS_H_

#include "pixmap.h"

#ifdef USE_SVG
static const char *const button_svg_pred[] = {
	[WBTN_SVG_CLOSE] =
		"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'>"
		"<path d='M.5 .5L9.5 9.5M9.5 .5L.5 9.5' fill='none'"
		" stroke='#616561' stroke-width='2.75'/>"
		"<path d='M.5 .5L9.5 9.5M9.5 .5L.5 9.5' fill='none'"
		" stroke='#000' stroke-width='1.4'/></svg>",
	[WBTN_SVG_BROKEN_CLOSE] =
		"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'>"
		"<path d='M.5 .5L2.5 2.5M9.5 .5L7.5 2.5"
		"M.5 9.5L2.5 7.5M9.5 9.5L7.5 7.5' fill='none'"
		" stroke='#616561' stroke-width='2.75'/>"
		"<path d='M.5 .5L2.5 2.5M9.5 .5L7.5 2.5"
		"M.5 9.5L2.5 7.5M9.5 9.5L7.5 7.5' fill='none'"
		" stroke='#000' stroke-width='1.4'/></svg>",
	[WBTN_SVG_KILL] =
		"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'>"
		"<path d='M.5 .5L1.5 1.5M9.5 .5L8.5 1.5"
		"M.5 9.5L1.5 8.5M9.5 9.5L8.5 8.5"
		"M3.5 3.5L6.5 6.5M6.5 3.5L3.5 6.5' fill='none'"
		" stroke='#616561' stroke-width='2.75'/>"
		"<path d='M.5 .5L1.5 1.5M9.5 .5L8.5 1.5"
		"M.5 9.5L1.5 8.5M9.5 9.5L8.5 8.5"
		"M3.5 3.5L6.5 6.5M6.5 3.5L3.5 6.5' fill='none'"
		" stroke='#000' stroke-width='1.4'/></svg>",
	[WBTN_SVG_ICONIFY] =
		"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'>"
		"<path d='M0 0H10V10H0ZM1 3V9H9V3Z' fill='#000' fill-rule='evenodd'/></svg>",
	[WBTN_SVG_MAXIMIZE] =
		"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'>"
		"<path d='M5 0 L10 0 L10 5 Z M0 5 L0 10 L5 10 Z' fill='#000000'/></svg>",
	[WBTN_SVG_RESTORE] =
		"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'>"
		"<path d='M6 0L10 3V4H6ZM0 6H4V10L0 7Z' fill='#000'/></svg>",
};

#define NEXT_BUTTON_BACKPLATE \
	"<defs><linearGradient id='b' x2='1' y2='1'>" \
	"<stop stop-color='#fff'/><stop offset='1' stop-color='#e0e0e0'/></linearGradient>" \
	"<linearGradient id='x' x2='1' y2='0'>" \
	"<stop offset='.6' stop-color='#505050' stop-opacity='0'/>" \
	"<stop offset='1' stop-color='#505050' stop-opacity='.8'/></linearGradient>" \
	"<linearGradient id='y' x2='0' y2='1'>" \
	"<stop offset='.6' stop-color='#505050' stop-opacity='0'/>" \
	"<stop offset='1' stop-color='#505050' stop-opacity='.8'/></linearGradient></defs>" \
	"<path fill='url(#b)' d='M0 0H10V10H0Z'/>" \
	"<path fill='url(#x)' d='M0 0H10V10H0Z'/>" \
	"<path fill='url(#y)' d='M0 0H10V10H0Z'/>"

static const char *const button_svg_next[] = {
	[WBTN_SVG_CLOSE] =
		"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'>"
		NEXT_BUTTON_BACKPLATE
		"<path d='M1.5 1.5L8.5 8.5M8.5 1.5L1.5 8.5' fill='none'"
		" stroke='#b4b4b4' stroke-width='1.8'/>"
		"<path d='M1.5 1.5L8.5 8.5M8.5 1.5L1.5 8.5' fill='none'"
		" stroke='#202020' stroke-width='1'/></svg>",
	[WBTN_SVG_BROKEN_CLOSE] =
		"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'>"
		"<path fill='#e0e0e0' d='M0 0H10V10H0Z'/>"
		"<path fill='#7d7d7d' d='M9 0H10V10H0V9H9Z'/>"
		"<path d='M1.5 1.5L2.5 2.5M8.5 1.5L7.5 2.5"
		"M1.5 8.5L2.5 7.5M8.5 8.5L7.5 7.5' fill='none'"
		" stroke='#bdbdbd' stroke-width='2.5'/>"
		"<path d='M1.5 1.5L2.5 2.5M8.5 1.5L7.5 2.5"
		"M1.5 8.5L2.5 7.5M8.5 8.5L7.5 7.5' fill='none'"
		" stroke='#363636' stroke-width='1.3'/></svg>",
	[WBTN_SVG_KILL] =
		"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'>"
		NEXT_BUTTON_BACKPLATE
		"<path d='M1.5 1.5L2.5 2.5M8.5 1.5L7.5 2.5"
		"M1.5 8.5L2.5 7.5M8.5 8.5L7.5 7.5' fill='none'"
		" stroke='#b4b4b4' stroke-width='1.8'/>"
		"<path d='M1.5 1.5L2.5 2.5M8.5 1.5L7.5 2.5"
		"M1.5 8.5L2.5 7.5M8.5 8.5L7.5 7.5' fill='none'"
		" stroke='#303030' stroke-width='1'/>"
		"<path fill='#222' d='M4 4H6V6H4Z'/></svg>",
	[WBTN_SVG_ICONIFY] =
		"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'>"
		NEXT_BUTTON_BACKPLATE
		"<path d='M0 3.5H6' fill='none' stroke='#999' stroke-width='1'/>"
		"<path d='M0 4.5H5.5V10' fill='none' stroke='#111' stroke-width='1'/>"
		"<path d='M6.5 4V9' fill='none' stroke='#eee' stroke-width='1'/>"
		"<path d='M0 6.5H4.5' fill='none' stroke='#888' stroke-width='1'/></svg>",
	[WBTN_SVG_MAXIMIZE] =
		"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'>"
		"<path fill='#303030' d='M0 0H5L0 5ZM10 5V10H5Z'/>"
		"<path fill='#fbfbfb' d='M5 0H10V5H8V1H5ZM0 5H2V9H5V10H0Z'/>"
		"<path fill='#808080' d='M5 1H8V5H6L5 4L4 5H1Z"
		"M2 5H4L5 6L6 5H9L5 9H2Z'/>"
		"<path fill='none' stroke='#b4b4b4' stroke-width='.8'"
		" d='M4.5 1.5L1.5 4.5M8.5 5.5L5.5 8.5M5.5 3.5L3.5 5.5'/></svg>",
	[WBTN_SVG_RESTORE] =
		"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'>"
		"<path fill='#303030' d='M0 0H5L0 5ZM10 5V10H5Z'/>"
		"<path fill='#808080' d='M5 0H10V4H6V5H5L4 6H0V5ZM5 5H6V4H10V5"
		"L5 10H0V6H4V5Z'/>"
		"<path fill='#fbfbfb' d='M6 0H8V3H10V4H6ZM0 6H4V10H2V7H0Z'/>"
		"<path fill='none' stroke='#b4b4b4' stroke-width='.8'"
		" d='M5.5 .5L.5 5.5M9.5 4.5L4.5 9.5M5.5 3.5L3.5 5.5'/></svg>",
};
#undef NEXT_BUTTON_BACKPLATE
#endif

#endif
