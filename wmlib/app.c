/* app.c - application context stuff
 *
 * WMlib - WindowMaker application programming interface
 *
 * Copyright (C) 1997-2003 Alfredo K. Kojima
 * Copyright (c) 2009-2026 Window Maker Team
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

#include <xcb/xcb.h>
#include <stdlib.h>
#include <string.h>

#include "WMaker.h"
#include "app.h"

static xcb_atom_t wmInternAtom(WMAppContext *app, const char *name)
{
	xcb_intern_atom_cookie_t cookie;
	xcb_intern_atom_reply_t *reply;
	xcb_atom_t atom = XCB_ATOM_NONE;

	cookie = xcb_intern_atom(app->dpy, 0, strlen(name), name);
	reply = xcb_intern_atom_reply(app->dpy, cookie, NULL);
	if (reply) {
		atom = reply->atom;
		free(reply);
	}
	return atom;
}

WMAppContext *WMAppCreateWithMain(xcb_connection_t *dpy, int screen_number, xcb_window_t main_window)
{
	wmAppContext *ctx;

	ctx = calloc(1, sizeof(wmAppContext));
	if (!ctx)
		return NULL;

	ctx->dpy = dpy;
	ctx->screen_number = screen_number;
	ctx->our_leader_hint = 0;
	ctx->main_window = main_window;
	ctx->windows = malloc(sizeof(xcb_window_t));
	if (!ctx->windows) {
		free(ctx);
		return NULL;
	}
	ctx->win_count = 1;
	ctx->windows[0] = main_window;

	ctx->main_menu = NULL;

	ctx->last_menu_tag = 100;

	ctx->menu_atom = wmInternAtom(ctx, "_WINDOWMAKER_MENU");
	ctx->wmfunc_atom = wmInternAtom(ctx, "_WINDOWMAKER_WM_FUNCTION");

	return ctx;
}

int WMAppAddWindow(WMAppContext *app, xcb_window_t window)
{
	xcb_window_t *win;

	win = malloc(sizeof(xcb_window_t) * (app->win_count + 1));
	if (!win)
		return 0;

	memcpy(win, app->windows, sizeof(xcb_window_t) * app->win_count);

	free(app->windows);

	win[app->win_count] = window;
	app->windows = win;
	app->win_count++;

	return 1;
}

int WMAppSetMainMenu(WMAppContext *app, WMMenu *menu)
{
	app->main_menu = menu;
	return 1;
}
