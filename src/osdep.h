/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
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

#ifndef WMOSDEP_H_
#define WMOSDEP_H_

#include <sys/types.h>

bool GetCommandForPid(int pid, char ***argv, int *argc);

/*
 * Walk /proc/<pid>/stat up the process tree to check if 'ancestor'
 * is a parent (or grandparent, ...) of 'pid'. Linux-only; stubs on
 * other platforms return False.
 */
bool IsProcessAncestor(pid_t ancestor, pid_t pid);

/*
 * Read /proc/<pid>/stat to determine whether the process was started
 * in the background (via shell '&'). Returns 1 if background, 0 if
 * foreground, -1 on error. Linux-only; stubs return 0.
 */
int IsBackgroundProcess(pid_t pid);

/*
 * Scan /proc for a process whose command (basename) matches 'cmd' and
 * that is a descendant of 'parent'. Returns the pid or 0 if not found.
 * Linux-only; stubs return 0.
 */
pid_t FindDescendantPidByCmdName(pid_t parent, const char *cmd);

#endif
