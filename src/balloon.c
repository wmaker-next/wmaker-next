/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1998-2003 Alfredo K. Kojima
 *  Copyright (c) 2014-2016 Window Maker Team
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

#ifdef BALLOON_TEXT

#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef SHAPED_BALLOON
#include <xcb/shape.h>
#endif

#include <stdlib.h>
#include <string.h>

#include <wraster.h>
#include <WINGs/WINGsP.h>

#include "WindowMaker.h"
#include "screen.h"
#include "texture.h"
#include "framewin.h"
#include "icon.h"
#include "appicon.h"
#include "workspace.h"
#include "balloon.h"
#include "misc.h"


typedef struct _WBalloon {
	Window window;

#ifdef SHAPED_BALLOON
#endif
	int prevType;

	Window objectWindow;
	char *text;
	int h;

	WMHandlerID timer;

	Pixmap contents;
	Pixmap mini_preview;

	char mapped;
	char ignoreTimer;
} WBalloon;

#define TOP	0
#define BOTTOM	1
#define LEFT	0
#define RIGHT	2

static int countLines(const char *text)
{
	const char *p = text;
	int h = 1;

	while (*p) {
		if (*p == '\n' && p[1] != 0)
			h++;
		p++;
	}
	return h;
}

static int getMaxStringWidth(WMFont *font, const char *text)
{
	const char *p = text;
	const char *pb = p;
	int pos = 0;
	int w = 0, wt;

	while (*p) {
		if (*p == '\n') {
			wt = WMWidthOfString(font, pb, pos);
			if (wt > w)
				w = wt;
			pos = 0;
			pb = p + 1;
		} else {
			pos++;
		}
		p++;
	}
	if (pos > 0) {
		wt = WMWidthOfString(font, pb, pos);
		if (wt > w)
			w = wt;
	}
	return w;
}

static void drawMultiLineString(WMScreen *scr, Pixmap pixmap, WMColor *color,
		    WMFont *font, int x, int y, const char *text, int len)
{
	const char *p = text;
	const char *pb = p;
	int l = 0, pos = 0;
	int height = WMFontHeight(font);

	while (*p && p - text < len) {
		if (*p == '\n') {
			WMDrawString(scr, pixmap, color, font, x, y + l * height, pb, pos);
			l++;
			pos = 0;
			pb = p + 1;
		} else {
			pos++;
		}
		p++;
	}
	if (pos > 0)
		WMDrawString(scr, pixmap, color, font, x, y + l * height, pb, pos);
}

#ifdef SHAPED_BALLOON

#define SPACE 12


static void showText(WScreen *scr, int x, int y, int h, int w, const char *text)
{
	int width;
	int height;
	Pixmap pixmap;
	Pixmap mask;
	WMFont *font = scr->info_text_font;
	int side = 0;
	int ty;
	int bx, by;

	if (scr->balloon->contents)
		XFreePixmap(dpy, scr->balloon->contents);

	width = getMaxStringWidth(font, text) + 16;
	height = countLines(text) * WMFontHeight(font) + 4;

	if (height < 16)
		height = 16;
	if (width < height)
		width = height;

	if (x + width > scr->scr_width) {
		side = RIGHT;
		bx = x - width + w / 2;
		if (bx < 0)
			bx = 0;
	} else {
		side = LEFT;
		bx = x + w / 2;
	}
	if (bx + width > scr->scr_width)
		bx = scr->scr_width - width;

	if (y - (height + SPACE) < 0) {
		side |= TOP;
		by = y + h - 1;
		ty = SPACE;
	} else {
		side |= BOTTOM;
		by = y - (height + SPACE);
		ty = 0;
	}
	WCairoRenderBalloon(scr->wmscreen, width, height + SPACE,
			    0.5, ((side & BOTTOM) ? 0 : SPACE) + 0.5,
			    width - 1, height - 1,
			    ((side & BOTTOM) ? WCairoBalloonBottom : 0)
			    | ((side & RIGHT) ? WCairoBalloonRight : 0),
			    SPACE, scr->white, scr->black, &pixmap, &mask);

	drawMultiLineString(scr->wmscreen, pixmap, scr->black, font, 8, ty + 2, text, strlen(text));

	XSetWindowBackgroundPixmap(dpy, scr->balloon->window, pixmap);
	scr->balloon->contents = pixmap;

	XResizeWindow(dpy, scr->balloon->window, width, height + SPACE);
	xcb_shape_mask(xconn, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING,
		       scr->balloon->window, 0, 0, mask);
	XFreePixmap(dpy, mask);
	XMoveWindow(dpy, scr->balloon->window, bx, by);
	XMapRaised(dpy, scr->balloon->window);

	scr->balloon->mapped = 1;
}
#else				/* !SHAPED_BALLOON */

static void showText(WScreen *scr, int x, int y, int h, int w, const char *text)
{
	int width;
	int height;
	Pixmap pixmap;
	WMFont *font = scr->info_text_font;

	if (scr->balloon->contents)
		XFreePixmap(dpy, scr->balloon->contents);

	width = getMaxStringWidth(font, text) + 8;
	/*width = WMWidthOfString(font, text, strlen(text))+8; */
	height = countLines(text) * WMFontHeight(font) + 4;

	if (x < 0)
		x = 0;
	else if (x + width > scr->scr_width - 1)
		x = scr->scr_width - width;

	if (y - height - 2 < 0) {
		y += h;
		if (y < 0)
			y = 0;
	} else {
		y -= height + 2;
	}

	if (scr->window_title_texture[0])
		XSetForeground(dpy, scr->draw_gc, scr->window_title_texture[0]->any.color.pixel);
	else
		XSetForeground(dpy, scr->draw_gc, scr->light_pixel);

	pixmap = XCreatePixmap(dpy, scr->root_win, width, height, scr->w_depth);
	XFillRectangle(dpy, pixmap, scr->draw_gc, 0, 0, width, height);

	drawMultiLineString(scr->wmscreen, pixmap, scr->window_title_color[0], font, 4, 2, text, strlen(text));

	XResizeWindow(dpy, scr->balloon->window, width, height);
	XMoveWindow(dpy, scr->balloon->window, x, y);

	XSetWindowBackgroundPixmap(dpy, scr->balloon->window, pixmap);
	XClearWindow(dpy, scr->balloon->window);
	XMapRaised(dpy, scr->balloon->window);

	scr->balloon->contents = pixmap;

	scr->balloon->mapped = 1;
}
#endif				/* !SHAPED_BALLOON */

static void show_minipreview(WScreen *scr, int x, int y, const char *title, Pixmap mini_preview)
{
	Pixmap pixmap;
	WMFont *font = scr->info_text_font;
	int width, height;
	int titleHeight;
	char *shortenTitle;

	if (scr->balloon->contents)
		XFreePixmap(dpy, scr->balloon->contents);

	width  = w_global.config->minipreview_size;
	height = w_global.config->minipreview_size;

	if (w_global.config->miniwin_title_balloon) {
		shortenTitle = ShrinkString(font, title, width - MINIPREVIEW_BORDER * 2);
		titleHeight = countLines(shortenTitle) * WMFontHeight(font) + 4;
		height += titleHeight;
	} else {
		shortenTitle = NULL;
		titleHeight = 0;
	}

	if (x < 0)
		x = 0;
	else if (x + width > scr->scr_width - 1)
		x = scr->scr_width - width - 1;

	if (y - height - 2 < 0) {
		y += w_global.config->icon_size;
		if (y < 0)
			y = 0;
	} else {
		y -= height + 2;
	}

	if (scr->window_title_texture[0])
		XSetForeground(dpy, scr->draw_gc, scr->window_title_texture[0]->any.color.pixel);
	else
		XSetForeground(dpy, scr->draw_gc, scr->light_pixel);

	pixmap = XCreatePixmap(dpy, scr->root_win, width, height, scr->w_depth);
	XFillRectangle(dpy, pixmap, scr->draw_gc, 0, 0, width, height);

	if (shortenTitle != NULL) {
		drawMultiLineString(scr->wmscreen, pixmap, scr->window_title_color[0], font,
						MINIPREVIEW_BORDER, MINIPREVIEW_BORDER, shortenTitle, strlen(shortenTitle));
		wfree(shortenTitle);
	}

	XCopyArea(dpy, mini_preview, pixmap, scr->draw_gc,
	          0, 0, (w_global.config->minipreview_size - 1 - MINIPREVIEW_BORDER * 2),
	          (w_global.config->minipreview_size - 1 - MINIPREVIEW_BORDER * 2),
	          MINIPREVIEW_BORDER, MINIPREVIEW_BORDER + titleHeight);

#ifdef SHAPED_BALLOON
	xcb_shape_mask(xconn, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING,
		       scr->balloon->window, 0, 0, XCB_PIXMAP_NONE);
#endif
	XResizeWindow(dpy, scr->balloon->window, width, height);
	XMoveWindow(dpy, scr->balloon->window, x, y);

	XSetWindowBackgroundPixmap(dpy, scr->balloon->window, pixmap);

	XClearWindow(dpy, scr->balloon->window);
	XMapRaised(dpy, scr->balloon->window);


	scr->balloon->contents = pixmap;

	scr->balloon->mapped = 1;
}

static void showBalloon(WScreen *scr)
{
	int x, y;
	Window foow;
	unsigned foo, w;

	scr->balloon->timer = NULL;
	scr->balloon->ignoreTimer = 1;

	if (!XGetGeometry(dpy, scr->balloon->objectWindow, &foow, &x, &y, &w, &foo, &foo, &foo)) {
		scr->balloon->prevType = 0;
		return;
	}

	if (w_global.config->miniwin_preview_balloon && scr->balloon->mini_preview != None)
		/* used to display either the mini-preview alone or the mini-preview with the title */
		show_minipreview(scr, x, y, scr->balloon->text, scr->balloon->mini_preview);
	else
		showText(scr, x, y, scr->balloon->h, w, scr->balloon->text);
}

static void frameBalloon(WObjDescriptor *object)
{
	WFrameWindow *fwin = (WFrameWindow *) object->parent;
	WScreen *scr = fwin->core->screen_ptr;

	if (fwin->titlebar != object->self || !fwin->flags.is_client_window_frame) {
		wBalloonHide(scr);
		return;
	}
	if (fwin->title && fwin->flags.incomplete_title) {
		scr->balloon->h = (fwin->titlebar ? fwin->titlebar->height : 0);
		scr->balloon->text = wstrdup(fwin->title);
		scr->balloon->objectWindow = fwin->core->window;
		scr->balloon->timer = WMAddTimerHandler(BALLOON_DELAY, (WMCallback *) showBalloon, scr);
	}
}

static void miniwindowBalloon(WObjDescriptor *object)
{
	WIcon *icon = (WIcon *) object->parent;
	WScreen *scr = icon->core->screen_ptr;

	if (!icon->icon_name) {
		wBalloonHide(scr);
		return;
	}
	scr->balloon->h = icon->core->height;
	scr->balloon->text = wstrdup(icon->icon_name);
	scr->balloon->mini_preview = icon->mini_preview;
	scr->balloon->objectWindow = icon->core->window;

	if ((scr->balloon->prevType == object->parent_type || scr->balloon->prevType == WCLASS_APPICON)
	    && scr->balloon->ignoreTimer) {
		XUnmapWindow(dpy, scr->balloon->window);
		showBalloon(scr);
	} else {
		scr->balloon->timer = WMAddTimerHandler(BALLOON_DELAY, (WMCallback *) showBalloon, scr);
	}
}

static void appiconBalloon(WObjDescriptor *object)
{
	WAppIcon *aicon = (WAppIcon *) object->parent;
	WScreen *scr = aicon->icon->core->screen_ptr;
	char *tmp;

	/* Show balloon if it is the Clip and the workspace name is > 5 chars */
	if (object->parent == scr->clip_icon) {
		if (strlen(scr->workspaces[scr->current_workspace]->name) > 5) {
			scr->balloon->text = wstrdup(scr->workspaces[scr->current_workspace]->name);
		} else {
			wBalloonHide(scr);
			return;
		}
	} else if (aicon->display_name || (aicon->command && aicon->wm_class)) {
		int len;
		WApplication *app;
		unsigned int app_win_cnt = 0;
		const char *title, *detail;

		if (object->parent_type == WCLASS_DOCK_ICON) {
			if (aicon->main_window) {
				app = wApplicationOf(aicon->main_window);
				if (app && app->main_window_desc && app->main_window_desc->fake_group)
					app_win_cnt = app->main_window_desc->fake_group->retainCount - 1;
			}
		}

		/*
		 * Title prefers the user-set or .desktop-derived display name,
		 * else falls back to wm_class (or wm_instance for GNUstep apps
		 * where every class is "GNUstep"). Detail prefers the comment
		 * description, else the launch command.
		 */
		if (aicon->display_name)
			title = aicon->display_name;
		else if (aicon->wm_class && strcmp(aicon->wm_class, "GNUstep") == 0)
			title = aicon->wm_instance;
		else
			title = aicon->wm_class;
		detail = aicon->comment ? aicon->comment : aicon->command;

		len = (title ? strlen(title) : 0) + (detail ? strlen(detail) : 0) + 8;
		if (app_win_cnt > 0)
			len += 1 + snprintf(NULL, 0, "%u", app_win_cnt);

		tmp = wmalloc(len);
		if (app_win_cnt > 0 && detail)
			snprintf(tmp, len, "%u %s\n%s%s%s", app_win_cnt, title,
				 aicon->comment ? "" : "(", detail,
				 aicon->comment ? "" : ")");
		else if (app_win_cnt > 0)
			snprintf(tmp, len, "%u %s", app_win_cnt, title);
		else if (detail)
			snprintf(tmp, len, "%s\n%s%s%s", title,
				 aicon->comment ? "" : "(", detail,
				 aicon->comment ? "" : ")");
		else
			snprintf(tmp, len, "%s", title);
		scr->balloon->text = tmp;

	} else if (aicon->command) {
		scr->balloon->text = wstrdup(aicon->command);
	} else if (aicon->wm_class) {
		/* Check to see if it is a GNUstep App */
		if (strcmp(aicon->wm_class, "GNUstep") == 0)
			scr->balloon->text = wstrdup(aicon->wm_instance);
		else
			scr->balloon->text = wstrdup(aicon->wm_class);
	} else {
		wBalloonHide(scr);
		return;
	}
	scr->balloon->h = aicon->icon->core->height - 2;

	scr->balloon->objectWindow = aicon->icon->core->window;
	if ((scr->balloon->prevType == object->parent_type || scr->balloon->prevType == WCLASS_MINIWINDOW)
	    && scr->balloon->ignoreTimer) {
		XUnmapWindow(dpy, scr->balloon->window);
		showBalloon(scr);
	} else {
		scr->balloon->timer = WMAddTimerHandler(BALLOON_DELAY, (WMCallback *) showBalloon, scr);
	}
}

void wBalloonInitialize(WScreen *scr)
{
	WBalloon *bal;
	XSetWindowAttributes attribs;
	unsigned long vmask;

	bal = wmalloc(sizeof(WBalloon));

	scr->balloon = bal;

	vmask = CWSaveUnder | CWOverrideRedirect | CWColormap | CWBackPixel | CWBorderPixel;
	attribs.save_under = True;
	attribs.override_redirect = True;
	attribs.colormap = scr->w_colormap;
	attribs.background_pixel = scr->icon_back_texture->normal.pixel;
	attribs.border_pixel = 0;	/* do not care */

	bal->window = XCreateWindow(dpy, scr->root_win, 1, 1, 10, 10, 1,
				    scr->w_depth, CopyFromParent, scr->w_visual, vmask, &attribs);
#if 0
	/* select EnterNotify to so that the balloon will be unmapped
	 * when the pointer is moved over it */
	XSelectInput(dpy, bal->window, EnterWindowMask);
#endif
}

void wBalloonEnteredObject(WScreen *scr, WObjDescriptor *object)
{
	WBalloon *balloon = scr->balloon;

	if (balloon->timer) {
		WMDeleteTimerHandler(balloon->timer);
		balloon->timer = NULL;
		balloon->ignoreTimer = 0;
	}

	if (scr->balloon->text)
		wfree(scr->balloon->text);
	scr->balloon->text = NULL;

	scr->balloon->mini_preview = None;

	if (!object) {
		wBalloonHide(scr);
		balloon->ignoreTimer = 0;
		return;
	}

	switch (object->parent_type) {
	case WCLASS_FRAME:
		if (w_global.config->window_balloon)
			frameBalloon(object);
		break;
	case WCLASS_DOCK_ICON:
		if (w_global.config->appicon_balloon)
			appiconBalloon(object);
		break;
	case WCLASS_MINIWINDOW:
		if (w_global.config->miniwin_title_balloon || w_global.config->miniwin_preview_balloon)
			miniwindowBalloon(object);
		break;
	case WCLASS_APPICON:
		if (w_global.config->appicon_balloon)
			appiconBalloon(object);
		break;
	default:
		wBalloonHide(scr);
		break;
	}
	scr->balloon->prevType = object->parent_type;
}

void wBalloonHide(WScreen *scr)
{
	if (scr) {
		if (scr->balloon->mapped) {
			XUnmapWindow(dpy, scr->balloon->window);
			scr->balloon->mapped = 0;
		} else {
			WMDeleteTimerHandlerPtr(&scr->balloon->timer);
		}
		scr->balloon->prevType = 0;
	}
}

#endif
