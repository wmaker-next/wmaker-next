/*  wsmap.c - worskpace map
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 2014-2026 Window Maker Team - David Maciejak
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef USE_XSHAPE
#include <xcb/shape.h>
#endif

#include "screen.h"
#include "window.h"
#include "misc.h"
#include "wxkb.h"
#include "workspace.h"
#include "wsmap.h"

#include "WINGs/WINGsP.h"


static const int WORKSPACE_MAP_RATIO  = 10;
static const int WORKSPACE_SEPARATOR_WIDTH = 12;
static const int MINI_WORKSPACE_MIN_SIZE = 4;

static WMPixmap *frame_bg_focused;
static WMPixmap *frame_bg_unfocused;

typedef struct {
	WScreen *scr;
	WMWindow *win;
	int xcount, ycount;
	int wswidth, wsheight;
	int mini_workspace_width, mini_workspace_height;
	int edge;
	int border_width;
	int grid_cols, grid_rows;
} WWorkspaceMap;

typedef struct {
	WMButton *workspace_img_button;
	WMLabel *workspace_label;
} W_WorkspaceMap;

static void refresh_mini_workspace(WWorkspaceMap *wsmap, W_WorkspaceMap *wsmap_array);

void wWorkspaceMapUpdate(WScreen *scr)
{
	RImage *mini_preview;

	mini_preview = RCreateImageFromDrawable(scr->rcontext, scr->root_win, XCB_NONE);
	if (mini_preview) {
		RImage *tmp = scr->workspaces[scr->current_workspace]->map;

		if (tmp)
			RReleaseImage(tmp);

		scr->workspaces[scr->current_workspace]->map =
			RSmoothScaleImage(mini_preview,
			                  scr->scr_width / WORKSPACE_MAP_RATIO,
			                  scr->scr_height / WORKSPACE_MAP_RATIO);
		RReleaseImage(mini_preview);
	}
}

static void workspace_map_slide(WWorkspaceMap *wsmap)
{
	if (wsmap->edge == WD_TOP)
		slide_window(WMWidgetXID(wsmap->win), 0, -1 * wsmap->wsheight, wsmap->xcount, wsmap->ycount);
	else
		slide_window(WMWidgetXID(wsmap->win), 0, wsmap->scr->scr_height, wsmap->xcount, wsmap->ycount);
}

static void workspace_map_unslide(WWorkspaceMap *wsmap)
{
	if (wsmap->edge == WD_TOP)
		slide_window(WMWidgetXID(wsmap->win), wsmap->xcount, wsmap->ycount, 0, -1 * wsmap->wsheight);
	else
		slide_window(WMWidgetXID(wsmap->win), wsmap->xcount, wsmap->ycount, 0, wsmap->scr->scr_height);
}

static void workspace_map_destroy(WWorkspaceMap *wsmap)
{
	workspace_map_unslide(wsmap);
	WMUnmapWidget(wsmap->win);

	if (wsmap->win) {
		xcb_window_t info_win = wsmap->scr->info_window;

		WXSendClientMessage(wdisp->conn, info_win, XCB_EVENT_MASK_ENTER_WINDOW, info_win,
				    wdisp->atom.wm.ignore_focus_events,
				    true, 0, 0, 0, 0);
		WMUnmapWidget(wsmap->win);

		WXSendClientMessage(wdisp->conn, info_win, XCB_EVENT_MASK_ENTER_WINDOW, info_win,
				    wdisp->atom.wm.ignore_focus_events,
				    false, 0, 0, 0, 0);
		WMDestroyWidget(wsmap->win);

		if (frame_bg_focused)
			WMReleasePixmap(frame_bg_focused);
		if (frame_bg_unfocused)
			WMReleasePixmap(frame_bg_unfocused);
	}
	wfree(wsmap);
}

static void selected_workspace_callback(WMWidget *w, void *data)
{
	WWorkspaceMap *wsmap = (WWorkspaceMap *) data;
	WMButton *click_button = w;

	if (w && wsmap) {
		int workspace_id = atoi(WMGetButtonText(click_button));

		wWorkspaceChangeFromPointer(wsmap->scr, workspace_id);
		w_global.process_workspacemap_event = false;
	}
}

static void set_workspace_map_background_image(WWorkspaceMap *wsmap)
{
	xcb_pixmap_t pixmap = XCB_NONE, mask = XCB_NONE;

	if (wsmap->scr->wsmbackTexture->any.type == WTEX_PIXMAP) {
		RImage *tmp = wTextureRenderImage(wsmap->scr->wsmbackTexture, wsmap->wswidth, wsmap->wsheight, WREL_FLAT);

		if (!tmp)
			return;

		if (!RConvertImageMask(wsmap->scr->rcontext, tmp, &pixmap, &mask, 250)) {
			RReleaseImage(tmp);
			return;
		}
		RReleaseImage(tmp);

		xcb_change_window_attributes(wdisp->conn, WMWidgetXID(wsmap->win), XCB_CW_BACK_PIXMAP, (const uint32_t[]){ pixmap });

#ifdef USE_XSHAPE
		if (mask && wdisp->xext.shape.supported)
			xcb_shape_mask(wdisp->conn, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING,
				       WMWidgetXID(wsmap->win), 0, 0, mask);
#endif

		if (pixmap)
			xcb_free_pixmap(wdisp->conn, pixmap);
		if (mask)
			xcb_free_pixmap(wdisp->conn, mask);
	}
}

static void workspace_map_show(WWorkspaceMap *wsmap)
{
	WMMapSubwidgets(wsmap->win);
	WMMapWidget(wsmap->win);
	workspace_map_slide(wsmap);
}

static WMPixmap *get_frame_background_color(WWorkspaceMap *wsmap, unsigned int width, unsigned int height, int type)
{
	RImage *img;
	WMPixmap *pix;

	if (!wsmap->scr->window_title_texture[type])
		return NULL;

	img = wTextureRenderImage(wsmap->scr->window_title_texture[type], width, height, WREL_FLAT);
	if (!img)
		return NULL;

	pix = WMCreatePixmapFromRImage(wsmap->scr->wmscreen, img, 128);
	RReleaseImage(img);

	return pix;
}

static void configure_workspace_grid(WWorkspaceMap *wsmap)
{
	WScreen *scr = wsmap->scr;
	int font_height = WMFontHeight(scr->info_text_font);
	int max_w, max_h, base_w, base_h;

	/* wNETWMUpdateDesktopLayout() already validates the grid covers
	 * every workspace and clamps both dimensions, so just mirror it */
	wsmap->grid_cols = WMAX(1, scr->net_desktop_layout.cols);
	wsmap->grid_rows = WMAX(1, scr->net_desktop_layout.rows);

	base_w = WMAX(1, scr->scr_width / WORKSPACE_MAP_RATIO);
	base_h = WMAX(1, scr->scr_height / WORKSPACE_MAP_RATIO);
	max_w = (scr->scr_width - (wsmap->grid_cols + 1) * WORKSPACE_SEPARATOR_WIDTH) / wsmap->grid_cols;
	max_h = (scr->scr_height - wsmap->border_width - WORKSPACE_SEPARATOR_WIDTH
		 - wsmap->grid_rows * (font_height + WORKSPACE_SEPARATOR_WIDTH)) / wsmap->grid_rows;
	max_w = WMAX(MINI_WORKSPACE_MIN_SIZE, max_w);
	max_h = WMAX(MINI_WORKSPACE_MIN_SIZE, max_h);

	wsmap->mini_workspace_width = WMIN(base_w, max_w);
	wsmap->mini_workspace_height = WMIN(base_h, max_h);
	wsmap->wswidth = scr->scr_width;
	wsmap->wsheight = WORKSPACE_SEPARATOR_WIDTH
		+ wsmap->grid_rows * (font_height + wsmap->mini_workspace_height + WORKSPACE_SEPARATOR_WIDTH);
}

static void workspace_map_realize(WWorkspaceMap *wsmap, WMFrame *frame_border, W_WorkspaceMap *wsmap_array)
{
	WMPixmap *frame_border_pixmap;
	WMSize label_size;

	WMRealizeWidget(wsmap->win);
	set_workspace_map_background_image(wsmap);

	frame_border_pixmap = get_frame_background_color(wsmap, wsmap->wswidth, wsmap->border_width, WS_FOCUSED);
	WMSetWidgetBackgroundPixmap(frame_border, frame_border_pixmap);
	WMReleasePixmap(frame_border_pixmap);

	label_size = WMGetViewSize(W_VIEW(wsmap_array[0].workspace_label));
	frame_bg_focused = get_frame_background_color(wsmap, label_size.width, label_size.height, WS_FOCUSED);
	frame_bg_unfocused = get_frame_background_color(wsmap, label_size.width, label_size.height, WS_UNFOCUSED);

	refresh_mini_workspace(wsmap, wsmap_array);
}

static WMPixmap *enlight_workspace(WScreen *scr, RImage *mini_wkspace_map)
{
	RImage *tmp = RCloneImage(mini_wkspace_map);
	RColor color;
	WMPixmap *icon;

	color.red = color.green = color.blue = 0;
	color.alpha = 160;
	RLightImage(tmp, &color);
	icon = WMCreatePixmapFromRImage(scr->wmscreen, tmp, 128);
	RReleaseImage(tmp);

	return icon;
}

static WMPixmap *dummy_background_pixmap(WWorkspaceMap *wsmap)
{
	RImage *img;
	WMPixmap *icon;

	img = RCreateImage(wsmap->mini_workspace_width, wsmap->mini_workspace_height, 0);

	if (!img)
		return NULL;

	/* the workspace texture is not saved anywhere, so just using the default unfocus color */
	if (wsmap->scr->window_title_texture[WS_UNFOCUSED]) {
		RColor frame_bg_color;

		frame_bg_color.red = wsmap->scr->window_title_texture[WS_UNFOCUSED]->solid.normal.red;
		frame_bg_color.green = wsmap->scr->window_title_texture[WS_UNFOCUSED]->solid.normal.green;
		frame_bg_color.blue = wsmap->scr->window_title_texture[WS_UNFOCUSED]->solid.normal.blue;
		RFillImage(img, &frame_bg_color);
	}

	icon = WMCreatePixmapFromRImage(wsmap->scr->wmscreen, img, 128);
	RReleaseImage(img);

	return icon;
}

static void show_mini_workspace(WWorkspaceMap *wsmap, W_WorkspaceMap *wsmap_array, int max_mini_workspace)
{
	int index, space_width;
	int border_width_adjustement = (wsmap->edge == WD_TOP) ? 0 : wsmap->border_width;
	int font_height = WMFontHeight(wsmap->scr->info_text_font);

	space_width = (wsmap->wswidth - wsmap->grid_cols * wsmap->mini_workspace_width)
		      / (wsmap->grid_cols + 1);
	space_width = WMAX(1, space_width);

	for (index = 0; index < max_mini_workspace; index++) {
		int row = 0, col = 0, x, y;

		wWorkspaceLayoutPosition(wsmap->scr, index, &row, &col);
		x = col * wsmap->mini_workspace_width + (col + 1) * space_width;
		y = border_width_adjustement + WORKSPACE_SEPARATOR_WIDTH
		    + row * (font_height + wsmap->mini_workspace_height + WORKSPACE_SEPARATOR_WIDTH);

		if (wsmap_array[index].workspace_img_button) {
			WMResizeWidget(wsmap_array[index].workspace_img_button,
				       wsmap->mini_workspace_width, wsmap->mini_workspace_height);
			WMMoveWidget(wsmap_array[index].workspace_img_button, x, y + font_height);
			WMMapWidget(wsmap_array[index].workspace_img_button);
		}
		if (wsmap_array[index].workspace_label) {
			WMResizeWidget(wsmap_array[index].workspace_label, wsmap->mini_workspace_width, font_height);
			WMMoveWidget(wsmap_array[index].workspace_label, x, y);
			WMMapWidget(wsmap_array[index].workspace_label);
		}
	}
}

static WMPixmap *get_mini_workspace(WWorkspaceMap *wsmap, int index)
{
	if (!wsmap->scr->workspaces[index]->map)
		return NULL;

	if (index == wsmap->scr->current_workspace)
		return enlight_workspace(wsmap->scr, wsmap->scr->workspaces[index]->map);

	return WMCreatePixmapFromRImage(wsmap->scr->wmscreen, wsmap->scr->workspaces[index]->map, 128);
}

static void refresh_mini_workspace(WWorkspaceMap *wsmap, W_WorkspaceMap *wsmap_array)
{
	int index;
	char name[16];
	WMPixmap *icon;

	for (index = 0; index < wsmap->scr->workspace_count; index++) {
		WMSetLabelText(wsmap_array[index].workspace_label, wsmap->scr->workspaces[index]->name);
		snprintf(name, sizeof(name), "%d", index);
		WMSetButtonText(wsmap_array[index].workspace_img_button, name);

		if (index == wsmap->scr->current_workspace) {
			WMSetWidgetBackgroundPixmap(wsmap_array[index].workspace_label, frame_bg_focused);
			WMSetLabelTextColor(wsmap_array[index].workspace_label, wsmap->scr->window_title_color[WS_FOCUSED]);
		} else {
			WMSetWidgetBackgroundPixmap(wsmap_array[index].workspace_label, frame_bg_unfocused);
			WMSetLabelTextColor(wsmap_array[index].workspace_label, wsmap->scr->window_title_color[WS_UNFOCUSED]);
		}

		icon = get_mini_workspace(wsmap, index);
		if (icon) {
			WMSetButtonImage(wsmap_array[index].workspace_img_button, icon);
			WMReleasePixmap(icon);
		}
	}
}

static void create_mini_workspace(WScreen *scr, WWorkspaceMap *wsmap, W_WorkspaceMap *wsmap_array)
{
	int workspace_index;
	char name[16];

	for (workspace_index = 0; workspace_index < scr->workspace_count; workspace_index++) {
		WMButton *mini_workspace_btn = WMCreateButton(wsmap->win, WBTOnOff);
		WMLabel *workspace_name_label = WMCreateLabel(wsmap->win);

		WMSetButtonBordered(mini_workspace_btn, 0);
		WMSetLabelFont(workspace_name_label, scr->info_text_font);
		WMSetLabelText(workspace_name_label, scr->workspaces[workspace_index]->name);

		wsmap_array[workspace_index].workspace_img_button = mini_workspace_btn;
		wsmap_array[workspace_index].workspace_label = workspace_name_label;

		WMSetButtonImagePosition(mini_workspace_btn, WIPImageOnly);
		snprintf(name, sizeof(name), "%d", workspace_index);
		WMSetButtonText(mini_workspace_btn, name);
		WMSetButtonAction(mini_workspace_btn, selected_workspace_callback, wsmap);
	}
	show_mini_workspace(wsmap, wsmap_array, scr->workspace_count);
}

static WWorkspaceMap *create_workspace_map(WScreen *scr, W_WorkspaceMap *wsmap_array, int edge)
{
	WWorkspaceMap *wsmap;

	if (scr->workspace_count == 0)
		return NULL;

	wsmap = wmalloc(sizeof(*wsmap));

	wsmap->border_width = 5;
	wsmap->edge = edge;
	wsmap->scr = scr;
	configure_workspace_grid(wsmap);
	wsmap->win = WMCreateWindow(scr->wmscreen, "wsmap");

	if (wsmap->scr->wsmbackTexture->any.type == WTEX_SOLID) {
		WMColor *tmp = WMCreateRGBColor(scr->wmscreen,
							wsmap->scr->wsmbackTexture->any.color.red,
							wsmap->scr->wsmbackTexture->any.color.green,
							wsmap->scr->wsmbackTexture->any.color.blue,
							false);
		WMSetWidgetBackgroundColor(wsmap->win, tmp);
		WMReleaseColor(tmp);
	}
	WMResizeWidget(wsmap->win, wsmap->wswidth, wsmap->wsheight + wsmap->border_width);

	WMFrame *framel = WMCreateFrame(wsmap->win);
	WMResizeWidget(framel, wsmap->wswidth, wsmap->border_width);
	WMSetFrameRelief(framel, WRSimple);
	wWorkspaceMapUpdate(scr);

	wsmap->xcount = 0;
	if (edge == WD_TOP) {
		wsmap->ycount = 0;
		WMMoveWidget(framel, 0, wsmap->wsheight);
	} else {
		wsmap->ycount = scr->scr_height - wsmap->wsheight - wsmap->border_width;
		WMMoveWidget(framel, 0, 0);
	}

	create_mini_workspace(scr, wsmap, wsmap_array);
	workspace_map_realize(wsmap, framel, wsmap_array);

	return wsmap;
}

static void handle_event(WWorkspaceMap *wsmap, W_WorkspaceMap *wsmap_array)
{
	xcb_generic_event_t *ev = NULL;
	int modifiers;
	xcb_keycode_t escKey = wXkbKeysymToKeycode(XKB_KEY_Escape);

	wXGrabKeyboard(wdisp->conn, WMWidgetXID(wsmap->win), false, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_CURRENT_TIME);
	wXGrabPointer(wdisp->conn, WMWidgetXID(wsmap->win), true,
	             XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS,
	             XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, WMWidgetXID(wsmap->win), XCB_NONE, XCB_CURRENT_TIME);

	w_global.process_workspacemap_event = true;
	while (w_global.process_workspacemap_event) {
		free(ev);
		ev = NULL;
		WMMaskEvent(wdisp->conn, XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_POINTER_MOTION |
		             XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_FOCUS_CHANGE, &ev);
		if (!ev)
			break;

		modifiers = (((xcb_key_press_event_t *)ev)->state) & w_global.shortcut.modifiers_mask;

		switch ((ev->response_type & 0x7f)) {
			WMScreen *wmscr;
			WMColor *black;

		case XCB_KEY_PRESS:
			if ((((xcb_key_press_event_t *)ev)->detail) == escKey || (wKeyBindings[WKBD_WORKSPACEMAP].keycode != 0 &&
			                                  wKeyBindings[WKBD_WORKSPACEMAP].keycode == (((xcb_key_press_event_t *)ev)->detail) &&
			                                  wKeyBindings[WKBD_WORKSPACEMAP].modifier == modifiers)) {
				w_global.process_workspacemap_event = false;
			} else {
				xkb_keysym_t ks = wXkbKeycodeToKeysym((((xcb_key_press_event_t *)ev)->detail), 0);

				if (ks == XKB_KEY_Left)
					wWorkspaceDirectionalChange(wsmap->scr, -1, 0);
				else if (ks == XKB_KEY_Right)
					wWorkspaceDirectionalChange(wsmap->scr, 1, 0);
				else if (ks == XKB_KEY_Up)
					wWorkspaceDirectionalChange(wsmap->scr, 0, -1);
				else if (ks == XKB_KEY_Down)
					wWorkspaceDirectionalChange(wsmap->scr, 0, 1);
				refresh_mini_workspace(wsmap, wsmap_array);
			}
			break;

		case XCB_BUTTON_PRESS:
			switch ((((xcb_button_press_event_t *)ev)->detail)) {
			case Button6:
				wWorkspaceDirectionalChangeFromPointer(wsmap->scr, -1, 0);
				refresh_mini_workspace(wsmap, wsmap_array);
				break;
			case Button7:
				wWorkspaceDirectionalChangeFromPointer(wsmap->scr, 1, 0);
				refresh_mini_workspace(wsmap, wsmap_array);
				break;
			default:
				WMHandleEvent(ev);
			}
			break;

		case  XCB_FOCUS_IN:
			wmscr = wsmap->scr->wmscreen;
			black = WMBlackColor(wmscr);
			const char *text = "?";
			WMFont *bold = WMBoldSystemFontOfSize(wmscr, wsmap->mini_workspace_width / 3);
			int x = (wsmap->mini_workspace_width / 2) - (WMWidthOfString(bold, text, strlen(text)) / 2);
			int y = (wsmap->mini_workspace_height / 2) - (WMFontHeight(bold) / 2);
			WMPixmap *icon = dummy_background_pixmap(wsmap);

			if (icon) {
				int i;

				WMDrawString(wmscr, WMGetPixmapXID(icon),
						black, bold, x, y, text, strlen(text));
				for (i = 0; i < wsmap->scr->workspace_count; i++) {
					if (!wsmap->scr->workspaces[i]->map)
						WMSetButtonImage(wsmap_array[i].workspace_img_button, icon);
				}
				WMReleasePixmap(icon);
			}
			WMHandleEvent(ev);
			break;

		default:
			WMHandleEvent(ev);
			break;
		}

		free(ev);
		ev = NULL;
	}

	free(ev);
	wXUngrabPointer(wdisp->conn, XCB_CURRENT_TIME);
	wXUngrabKeyboard(wdisp->conn, XCB_CURRENT_TIME);

	workspace_map_destroy(wsmap);
}

static WWorkspaceMap *init_workspace_map(WScreen *scr, W_WorkspaceMap *wsmap_array)
{
	WWorkspaceMap *wsmap;

	wsmap = create_workspace_map(scr, wsmap_array, WD_BOTTOM);
	return wsmap;
}

void StartWorkspaceMap(WScreen *scr)
{
	WWorkspaceMap *wsmap;
	W_WorkspaceMap wsmap_array[MAX_WORKSPACES];

	memset(wsmap_array, 0, sizeof(wsmap_array));

	/* save the current screen before displaying the workspace map */
	wWorkspaceMapUpdate(scr);

	wsmap = init_workspace_map(scr, wsmap_array);
	if (wsmap) {
		workspace_map_show(wsmap);
		handle_event(wsmap, wsmap_array);
	}
}
