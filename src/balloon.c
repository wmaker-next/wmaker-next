/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1998-2003 Alfredo K. Kojima
 *  Copyright (c) 2014-2026 Window Maker Team
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
#ifdef SHAPED_BALLOON
#include <xcb/shape.h>
#endif

#include <stdlib.h>
#include <string.h>

#include <cairo-xcb.h>

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
	xcb_window_t window;

#ifdef SHAPED_BALLOON
#endif
	int prevType;

	xcb_window_t objectWindow;
	char *text;
	int h;

	WMHandlerID timer;

	xcb_pixmap_t contents;
	xcb_pixmap_t mini_preview;

	char mapped;
	char ignoreTimer;
} WBalloon;

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

static int getMaxStringWidth(WScreen *scr, WMFont *font, const char *text)
{
	const char *p = text;
	const char *pb = p;
	int pos = 0;
	int w = 0, wt;

	while (*p) {
		if (*p == '\n') {
			wt = WSCALE(scr, WMWidthOfString(font, pb, pos));
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
		wt = WSCALE(scr, WMWidthOfString(font, pb, pos));
		if (wt > w)
			w = wt;
	}
	return w;
}

static void drawMultiLineString(WScreen *scr, cairo_t *cr, WMColor *color, WMFont *font,
				int x, int y, const char *text, int len)
{
	const char *p = text;
	const char *pb = p;
	int l = 0, pos = 0;
	int height = WSCALE(scr, WMFontHeight(font));

	while (*p && p - text < len) {
		if (*p == '\n') {
			W_DrawString(cr, color, font, x, y + l * height, pb, pos);
			l++;
			pos = 0;
			pb = p + 1;
		} else {
			pos++;
		}
		p++;
	}
	if (pos > 0)
		W_DrawString(cr, color, font, x, y + l * height, pb, pos);
}

/* fresh background pixmap for the balloon window filled with the
 * unfocused-title texture colour (or gray if no title texture is set),
 * returned with a live cairo_t so the caller adds text and thumbnail
 * on top and calls WCairoEnd() itself */
static xcb_pixmap_t makeBackground(WScreen *scr, int width, int height, cairo_t **crp)
{
	xcb_pixmap_t pixmap;
	cairo_t *cr;

	pixmap = xcb_generate_id(wdisp->conn);
	xcb_create_pixmap(wdisp->conn, scr->w_depth, pixmap, scr->root_win, width, height);
	WMScreenNoteDrawableSize(scr->wmscreen, pixmap, width, height);
	cr = WCairoBeginForDrawable(scr->wmscreen, pixmap);
	if (scr->window_title_texture[0])
		W_SetSourceXColor(cr, &scr->window_title_texture[0]->any.color);
	else
		W_SetSourceColor(cr, scr->gray);
	cairo_paint(cr);
	*crp = cr;
	return pixmap;
}

static void freeContents(WScreen *scr)
{
	if (scr->balloon->contents) {
		WMScreenAbandonDrawable(scr->wmscreen, scr->balloon->contents);
		xcb_free_pixmap(wdisp->conn, scr->balloon->contents);
		scr->balloon->contents = XCB_NONE;
	}
}

#ifdef SHAPED_BALLOON

#define TOP	0
#define BOTTOM	1
#define LEFT	0
#define RIGHT	2

#define SPACE 12


static void showText(WScreen *scr, int x, int y, int h, int w, const char *text)
{
	int width;
	int height;
	xcb_pixmap_t pixmap;
	xcb_pixmap_t mask;
	WMFont *font = scr->info_text_font;
	cairo_t *cr;
	int side = 0;
	int ty;
	int bx, by;

	freeContents(scr);

	width = getMaxStringWidth(scr, font, text) + 16;
	height = WSCALE(scr, countLines(text) * WMFontHeight(font) + 4);

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
	W_RenderBalloon(scr->wmscreen, width, height + SPACE,
			    0.5, ((side & BOTTOM) ? 0 : SPACE) + 0.5,
			    width - 1, height - 1,
			    ((side & BOTTOM) ? WCairoBalloonBottom : 0)
			    | ((side & RIGHT) ? WCairoBalloonRight : 0),
			    SPACE, scr->white, scr->black, &pixmap, &mask);

	cr = WCairoBeginForDrawable(scr->wmscreen, pixmap);
	drawMultiLineString(scr, cr, scr->black, font, 8, ty + 2, text, strlen(text));
	WCairoEnd(cr);

	xcb_change_window_attributes(wdisp->conn, scr->balloon->window, XCB_CW_BACK_PIXMAP, (const uint32_t[]){ pixmap });
	scr->balloon->contents = pixmap;

	xcb_configure_window(wdisp->conn, scr->balloon->window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, (const uint32_t[]){ width, height + SPACE });
	xcb_shape_mask(wdisp->conn, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING,
		       scr->balloon->window, 0, 0, mask);
	xcb_free_pixmap(wdisp->conn, mask);
	xcb_configure_window(wdisp->conn, scr->balloon->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ bx, by });
	/* tooltip balloon, never in the stacking list, direct configure */
	xcb_configure_window(wdisp->conn, scr->balloon->window, XCB_CONFIG_WINDOW_STACK_MODE, (const uint32_t[]){ XCB_STACK_MODE_ABOVE });
	xcb_map_window(wdisp->conn, scr->balloon->window);

	scr->balloon->mapped = 1;
}
#else				/* !SHAPED_BALLOON */

static void showText(WScreen *scr, int x, int y, int h, int w, const char *text)
{
	int width;
	int height;
	xcb_pixmap_t pixmap;
	WMFont *font = scr->info_text_font;
	cairo_t *cr;

	(void)w;

	freeContents(scr);

	width = getMaxStringWidth(scr, font, text) + 8;
	height = WSCALE(scr, countLines(text) * WMFontHeight(font) + 4);

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

	pixmap = makeBackground(scr, width, height, &cr);
	drawMultiLineString(scr, cr, scr->window_title_color[0], font, 4, 2, text, strlen(text));
	WCairoEnd(cr);

	xcb_configure_window(wdisp->conn, scr->balloon->window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, (const uint32_t[]){ width, height });
	xcb_configure_window(wdisp->conn, scr->balloon->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ x, y });

	xcb_change_window_attributes(wdisp->conn, scr->balloon->window, XCB_CW_BACK_PIXMAP, (const uint32_t[]){ pixmap });
	xcb_clear_area(wdisp->conn, 0, scr->balloon->window, 0, 0, 0, 0);
	/* tooltip balloon, never in the stacking list, direct configure */
	xcb_configure_window(wdisp->conn, scr->balloon->window, XCB_CONFIG_WINDOW_STACK_MODE, (const uint32_t[]){ XCB_STACK_MODE_ABOVE });
	xcb_map_window(wdisp->conn, scr->balloon->window);

	scr->balloon->contents = pixmap;

	scr->balloon->mapped = 1;
}
#endif				/* !SHAPED_BALLOON */

static void show_minipreview(WScreen *scr, int x, int y, const char *title, xcb_pixmap_t mini_preview)
{
	RContext *rc = scr->rcontext;
	xcb_pixmap_t pixmap;
	WMFont *font = scr->info_text_font;
	cairo_surface_t *psurf;
	cairo_t *cr;
	int width, height;
	int titleHeight;
	int thumb;
	char *shortenTitle;

	freeContents(scr);

	width  = w_global.config->minipreview_size;
	height = w_global.config->minipreview_size;

	if (w_global.config->miniwin_title_balloon) {
		shortenTitle = ShrinkString(font, title, WUNSCALE(scr, width - MINIPREVIEW_BORDER * 2));
		titleHeight = WSCALE(scr, countLines(shortenTitle) * WMFontHeight(font) + 4);
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

	pixmap = makeBackground(scr, width, height, &cr);

	if (shortenTitle != NULL) {
		drawMultiLineString(scr, cr, scr->window_title_color[0], font,
				    MINIPREVIEW_BORDER, MINIPREVIEW_BORDER,
				    shortenTitle, strlen(shortenTitle));
		wfree(shortenTitle);
	}

	/* the scaled snapshot pixmap set_icon_minipreview() built is one
	 * pixel wider than the border-inset area, keep the same one-pixel
	 * right and bottom margin the previous copy left */
	thumb = w_global.config->minipreview_size - MINIPREVIEW_BORDER * 2;
	psurf = cairo_xcb_surface_create(rc->xc, (xcb_drawable_t)mini_preview,
					 rc->xvisual, thumb, thumb);
	cairo_set_source_surface(cr, psurf,
				 MINIPREVIEW_BORDER, MINIPREVIEW_BORDER + titleHeight);
	cairo_rectangle(cr, MINIPREVIEW_BORDER, MINIPREVIEW_BORDER + titleHeight,
			thumb - 1, thumb - 1);
	cairo_fill(cr);
	cairo_surface_destroy(psurf);
	WCairoEnd(cr);

#ifdef SHAPED_BALLOON
	xcb_shape_mask(wdisp->conn, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING,
		       scr->balloon->window, 0, 0, XCB_PIXMAP_NONE);
#endif
	xcb_configure_window(wdisp->conn, scr->balloon->window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, (const uint32_t[]){ width, height });
	xcb_configure_window(wdisp->conn, scr->balloon->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ x, y });

	xcb_change_window_attributes(wdisp->conn, scr->balloon->window, XCB_CW_BACK_PIXMAP, (const uint32_t[]){ pixmap });

	xcb_clear_area(wdisp->conn, 0, scr->balloon->window, 0, 0, 0, 0);
	/* tooltip balloon, never in the stacking list, direct configure */
	xcb_configure_window(wdisp->conn, scr->balloon->window, XCB_CONFIG_WINDOW_STACK_MODE, (const uint32_t[]){ XCB_STACK_MODE_ABOVE });
	xcb_map_window(wdisp->conn, scr->balloon->window);


	scr->balloon->contents = pixmap;

	scr->balloon->mapped = 1;
}

static void showBalloon(WScreen *scr)
{
	int x, y;
	unsigned int w;
	WXGeometry geom;

	scr->balloon->timer = NULL;
	scr->balloon->ignoreTimer = 1;

	if (!WXRcvGeometry(wdisp->conn, WXReqGeometry(wdisp->conn, scr->balloon->objectWindow), &geom)) {
		scr->balloon->prevType = 0;
		return;
	}

	x = geom.x;
	y = geom.y;
	w = geom.width;

	if (w_global.config->miniwin_preview_balloon && scr->balloon->mini_preview != XCB_NONE)
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
		xcb_unmap_window(wdisp->conn, scr->balloon->window);
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
		xcb_unmap_window(wdisp->conn, scr->balloon->window);
		showBalloon(scr);
	} else {
		scr->balloon->timer = WMAddTimerHandler(BALLOON_DELAY, (WMCallback *) showBalloon, scr);
	}
}

void wBalloonInitialize(WScreen *scr)
{
	WBalloon *bal;

	bal = wmalloc(sizeof(WBalloon));

	scr->balloon = bal;

	bal->window = xcb_generate_id(wdisp->conn);
	xcb_create_window(wdisp->conn, scr->w_depth, bal->window, scr->root_win, 1, 1, 10, 10, 1,
			  XCB_WINDOW_CLASS_COPY_FROM_PARENT, scr->w_visual->visual_id,
			  XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_OVERRIDE_REDIRECT
			  | XCB_CW_SAVE_UNDER | XCB_CW_COLORMAP,
			  (const uint32_t[]){ scr->icon_back_texture->normal.pixel, 0, true, true,
					      scr->w_colormap });
#if 0
	/* select EnterNotify to so that the balloon will be unmapped
	 * when the pointer is moved over it */
	xcb_change_window_attributes(wdisp->conn, bal->window, XCB_CW_EVENT_MASK, (const uint32_t[]){ XCB_EVENT_MASK_ENTER_WINDOW });
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

	scr->balloon->mini_preview = XCB_NONE;

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
			xcb_unmap_window(wdisp->conn, scr->balloon->window);
			scr->balloon->mapped = 0;
		} else {
			WMDeleteTimerHandlerPtr(&scr->balloon->timer);
		}
		scr->balloon->prevType = 0;
	}
}

#endif
