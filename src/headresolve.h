/*
 *  headresolve.h
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

#ifndef WMHEADRESOLVE_H_
#define WMHEADRESOLVE_H_

#include <stdbool.h>

#include <WINGs/WINGs.h>

/*
 * Pure head lookup over one RandR monitor name per head, NULL for an
 * unnamed head, unit testable without a display.
 */

typedef enum {
	WHEAD_SEL_POINTER = 0, /* empty or Cursor: the pointer head */
	WHEAD_SEL_WINDOW,      /* Window: the focused-window head */
	WHEAD_SEL_HEAD,        /* resolved to a head index */
	WHEAD_SEL_UNRESOLVED   /* a name or index that matches no head */
} WHeadSelKind;

typedef struct {
	WHeadSelKind kind;
	int head; /* valid for WHEAD_SEL_HEAD */
} WHeadSelection;

/* Strict "#N" head index: non-negative decimal digits that fit in int.
 * Rejects "#", "#5abc", "#+5", "#-1", whitespace and junk. */
bool wHeadIndexParse(const char *token, int *index);

/* Plain value lookup, exact case-sensitive names first (including
 * names starting with '#'), then a strict "#N" naming a head only when
 * that head has no name. Returns -1 for no head. */
int wHeadResolveFromNames(const char *const *names, int count, const char *value);

/* Full classification of a configured value. The Cursor and Window
 * keywords are recognized case-insensitively before any name lookup.
 * The case-sensitive "name:" prefix selects the exact name that
 * follows it, never a keyword or a "#N" token. */
WHeadSelection wHeadSelectFromNames(const char *const *names, int count,
				    const char *value);

/* Head with the greatest intersection with the frame, or -1 when the
 * frame is degenerate or intersects no head. */
int wHeadForFrameRect(const WMRect *heads, int count, WMRect frame);

#endif /* WMHEADRESOLVE_H_ */
