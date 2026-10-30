/* usermenu.h- user defined menu
 *
 *  Window Maker window manager
 *
 *  Copyright (c) hmmm... Should I put everybody's name here?
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
 *
 *  Where's my lawyer?? -- ]d :D
 */

#ifdef USER_MENU

#ifndef _WUSERMENU_H_
#define _WUSERMENU_H_

WMenu *wUserMenuGet(WScreen *scr, WWindow *wwin);
void wUserMenuRefreshInstances(WMenu *menu, WWindow *wwin);

#endif
#endif /* USER_MENU */
