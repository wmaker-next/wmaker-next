/*
 *  swp_label.h
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

#ifndef WMSWP_LABEL_H
#define WMSWP_LABEL_H

typedef struct WSwitchLabelFields {
	const char *title;
	const char *wm_class;
	const char *wm_instance;
	const char *workspace;
	const char *state_long;
	const char *state_short;
	int workspace_count;
} WSwitchLabelFields;

char *wSwitchPanelFormatLabel(const char *format, const WSwitchLabelFields *fields);

#endif
