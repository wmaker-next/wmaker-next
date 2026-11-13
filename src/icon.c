/* icon.c - window icon and dock and appicon parent
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2008-2026 Window Maker Team
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

#include <cairo.h>
#include <WINGs/WINGsP.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>
#include <wraster.h>
#include <sys/stat.h>

#include "WindowMaker.h"
#include "wcore.h"
#include "texture.h"
#include "window.h"
#include "icon.h"
#include "actions.h"
#include "stacking.h"
#include "application.h"
#include "defaults.h"
#include "appicon.h"
#include "dock.h"
#include "wmspec.h"
#include "misc.h"
#include "startup.h"
#include "wxkb.h"
#include "event.h"
#include "winmenu.h"

/**** Global variables ****/

#define MOD_MASK wdisp->modifier_mask
#define CACHE_ICON_PATH "/CachedPixmaps"

static void miniwindowExpose(WObjDescriptor *desc, xcb_generic_event_t *event);
static void miniwindowMouseDown(WObjDescriptor *desc, xcb_generic_event_t *event);
static void miniwindowDblClick(WObjDescriptor *desc, xcb_generic_event_t *event);

static WIcon *icon_create_core(WScreen *scr, int coord_x, int coord_y);

static void set_dockapp_in_icon(WIcon *icon);
static void get_rimage_icon_from_icon_win(WIcon *icon);
static void get_rimage_icon_from_user_icon(WIcon *icon);
static void get_rimage_icon_from_default_icon(WIcon *icon);
static void get_rimage_icon_from_x11(WIcon *icon);

static void icon_update_pixmap(WIcon *icon, RImage *image);
static void unset_icon_image(WIcon *icon);

/****** Notification Observers ******/

static void appearanceObserver(void *self, WMNotification *notif)
{
	WIcon *icon = (WIcon *) self;
	uintptr_t flags = (uintptr_t)WMGetNotificationClientData(notif);

	if ((flags & WTextureSettings) || (flags & WFontSettings)) {
		/* If the rimage exists, update the icon, else create it */
		if (icon->file_image)
			update_icon_pixmap(icon);
		else
			wIconPaint(icon);
	}

	/* so that the appicon expose handlers will paint the appicon specific
	 * stuff */
	xcb_clear_area(wdisp->conn, true, icon->core->window, 0, 0, icon->core->width, icon->core->height);
}

static void tileObserver(void *self, WMNotification *notif)
{
	WIcon *icon = (WIcon *) self;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) notif;

	update_icon_pixmap(icon);

	xcb_clear_area(wdisp->conn, true, icon->core->window, 0, 0, 1, 1);
}

/************************************/

static int getSize(xcb_drawable_t d, unsigned int *w, unsigned int *h, unsigned int *dep)
{
	if (d == XCB_NONE) {
		if (w)
			*w = 0;
		if (h)
			*h = 0;
		if (dep)
			*dep = 0;
		return 0;
	}

	WXGeometry geom;

	if (!WXRcvGeometry(wdisp->conn, WXReqGeometry(wdisp->conn, d), &geom))
		return 0;
	if (w)
		*w = geom.width;
	if (h)
		*h = geom.height;
	if (dep)
		*dep = geom.depth;
	return 1;
}

WIcon *icon_create_for_wwindow(WWindow *wwin)
{
	WScreen *scr = wwin->screen_ptr;
	WIcon *icon;

	icon = icon_create_core(scr, wwin->icon_x, wwin->icon_y);

	icon->owner = wwin;
	if (wwin->wm_hints && (wwin->wm_hints->flags & XCB_ICCCM_WM_HINT_ICON_WINDOW)) {
		if (wwin->client_win == wwin->main_window) {
			WApplication *wapp;
			/* do not let miniwindow steal app-icon's icon window */
			wapp = wApplicationOf(wwin->client_win);
			if (!wapp || wapp->app_icon == NULL)
				icon->icon_win = wwin->wm_hints->icon_window;
		} else {
			icon->icon_win = wwin->wm_hints->icon_window;
		}
	}
#ifdef NO_MINIWINDOW_TITLES
	icon->show_title = 0;
#else
	icon->show_title = 1;
#endif

	wIconChangeTitle(icon, wwin);
	icon->tile_type = TILE_NORMAL;

	set_icon_image_from_database(icon, wwin->wm_instance, wwin->wm_class, NULL);
	/* Update the icon, because icon could be NULL */
	wIconUpdate(icon);

	WMAddNotificationObserver(appearanceObserver, icon, WNIconAppearanceSettingsChanged, icon);
	WMAddNotificationObserver(tileObserver, icon, WNIconTileSettingsChanged, icon);

	return icon;
}

WIcon *icon_create_for_dock(WScreen *scr, const char *command, const char *wm_instance, const char *wm_class, int tile)
{
	WIcon *icon;

	icon = icon_create_core(scr, 0, 0);
	icon->tile_type = tile;

	set_icon_image_from_database(icon, wm_instance, wm_class, command);
	/* Update the icon, because icon could be NULL */
	wIconUpdate(icon);

	WMAddNotificationObserver(appearanceObserver, icon, WNIconAppearanceSettingsChanged, icon);
	WMAddNotificationObserver(tileObserver, icon, WNIconTileSettingsChanged, icon);

	return icon;
}

static WIcon *icon_create_core(WScreen *scr, int coord_x, int coord_y)
{
	WIcon *icon;

	icon = wmalloc(sizeof(WIcon));
	icon->core = wCoreCreateTopLevel(scr,
					 coord_x,
					 coord_y,
					 w_global.config->icon_size,
					 w_global.config->icon_size,
					 0, scr->w_depth, scr->w_visual, scr->w_colormap,
					 scr->white_pixel);

	/* will be overridden if this is a application icon */
	icon->core->descriptor.handle_mousedown = miniwindowMouseDown;
	icon->core->descriptor.handle_expose = miniwindowExpose;
	icon->core->descriptor.parent_type = WCLASS_MINIWINDOW;
	icon->core->descriptor.parent = icon;

	icon->core->stacking = wmalloc(sizeof(WStacking));
	icon->core->stacking->above = NULL;
	icon->core->stacking->under = NULL;
	icon->core->stacking->window_level = NORMAL_ICON_LEVEL;
	icon->core->stacking->child_of = NULL;

	/* Icon image */
	icon->file = NULL;
	icon->file_image = NULL;
	icon->icon_win = XCB_NONE;

	return icon;
}

void wIconDestroy(WIcon *icon)
{
	WCoreWindow *core = icon->core;
	WScreen *scr = core->screen_ptr;

	WMRemoveNotificationObserver(icon);

	if (icon->handlerID)
		WMDeleteTimerHandler(icon->handlerID);

	if (icon->icon_win) {
		int x = 0, y = 0;

		if (icon->owner) {
			x = icon->owner->icon_x;
			y = icon->owner->icon_y;
		}
		xcb_unmap_window(wdisp->conn, icon->icon_win);
		xcb_reparent_window(wdisp->conn, icon->icon_win, scr->root_win, x, y);
	}
	if (icon->icon_name)
		wfree(icon->icon_name);

	if (icon->pixmap)
		xcb_free_pixmap(wdisp->conn, icon->pixmap);

	if (icon->mini_preview)
		xcb_free_pixmap(wdisp->conn, icon->mini_preview);

	unset_icon_image(icon);

	wCoreDestroy(icon->core);
	wfree(icon);
}

static void drawIconTitleBackground(WScreen *scr, xcb_pixmap_t pixmap, int height)
{
	WTexSolid *tex = scr->icon_title_texture;
	int isz = w_global.config->icon_size;
	cairo_t *cr;

	cr = WCairoBeginForDrawable(scr->wmscreen, pixmap);
	if (!cr)
		return;
	cairo_set_line_width(cr, 1.0);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);

	W_SetSourceXColor(cr, &tex->normal);
	cairo_rectangle(cr, 0, 0, isz, height + 1);
	cairo_fill(cr);

	W_SetSourceXColor(cr, &tex->light);
	cairo_move_to(cr, 0.5, height + 1.5);
	cairo_line_to(cr, 0.5, 0.5);
	cairo_line_to(cr, isz + 0.5, 0.5);
	cairo_stroke(cr);

	W_SetSourceXColor(cr, &tex->dim);
	cairo_move_to(cr, isz - 0.5, 0.5);
	cairo_line_to(cr, isz - 0.5, height + 1.5);
	cairo_stroke(cr);

	WCairoEnd(cr);
}

/* the marching-ants selection outline: solid black under a
 * white 4-on/4-off dash with a per-tick offset, matching what
 * icon_select_gc's LineDoubleDash white/black produced */
static void drawIconSelection(WIcon *icon)
{
	WScreen *scr = icon->core->screen_ptr;
	double dash[2] = { 4.0, 4.0 };
	cairo_t *cr;

	cr = WCairoBeginForDrawable(scr->wmscreen, icon->core->window);
	if (!cr)
		return;
	cairo_set_line_width(cr, 1.0);
	cairo_rectangle(cr, 0.5, 0.5,
			icon->core->width - 1, icon->core->height - 1);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_stroke_preserve(cr);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_set_dash(cr, dash, 2, (double)icon->step);
	cairo_stroke(cr);
	WCairoEnd(cr);
}

static void icon_update_pixmap(WIcon *icon, RImage *image)
{
	RImage *tile;
	xcb_pixmap_t pixmap;
	int x, y, sx, sy;
	unsigned w, h;
	int theight = 0;
	WScreen *scr = icon->core->screen_ptr;

	switch (icon->tile_type) {
	case TILE_NORMAL:
		tile = RCloneImage(scr->icon_tile);
		break;
	case TILE_CLIP:
		tile = RCloneImage(scr->clip_tile);
		break;
	case TILE_DRAWER:
		tile = RCloneImage(scr->drawer_tile);
		break;
	default:
		/*
		 * The icon has always rigth value, this case is
		 * only to avoid a compiler warning with "tile"
		 * "may be used uninitialized"
		 */
		wwarning("Unknown tile type: %d.\n", icon->tile_type);
		tile = RCloneImage(scr->icon_tile);
	}

	if (image) {
		w = (image->width > w_global.config->icon_size)
		    ? w_global.config->icon_size : image->width;
		x = (w_global.config->icon_size - w) / 2;
		sx = (image->width - w) / 2;

		if (icon->show_title)
			theight = WSCALE(scr, WMFontHeight(scr->icon_title_font));

		h = (image->height + theight > w_global.config->icon_size
		     ? w_global.config->icon_size - theight : image->height);
		y = theight + (w_global.config->icon_size - theight - h) / 2;
		sy = (image->height - h) / 2;

		if (icon->tile_type == TILE_DRAWER) {
			int dx, dy;

			wDrawerIconTileShift(icon, &dx, &dy);
			x = WMIN(WMAX(x + dx, 0), w_global.config->icon_size - (int)w);
			y = WMIN(WMAX(y + dy, 0), w_global.config->icon_size - (int)h);
		}

		RCombineArea(tile, image, sx, sy, w, h, x, y);
	}

	if (icon->shadowed) {
		RColor color;

		color.red = scr->icon_back_texture->light.red >> 8;
		color.green = scr->icon_back_texture->light.green >> 8;
		color.blue = scr->icon_back_texture->light.blue >> 8;
		color.alpha = 150;	/* about 60% */
		RClearImage(tile, &color);
	}

	if (icon->highlighted) {
		RColor color;

		color.red = color.green = color.blue = 0;
		color.alpha = 160;
		RLightImage(tile, &color);
	}

	if (!RConvertImage(scr->rcontext, tile, &pixmap))
		wwarning(_("error rendering image:%s"), RMessageForError(RErrorCode));

	RReleaseImage(tile);

	WMScreenNoteDrawableSize(scr->wmscreen, pixmap,
				 w_global.config->icon_size,
				 w_global.config->icon_size);

	/* Draw the icon's title background (without text) */
	if (icon->show_title)
		drawIconTitleBackground(scr, pixmap, theight);

	icon->pixmap = pixmap;
}

void wIconChangeTitle(WIcon *icon, WWindow *wwin)
{
	bool from_client = true;

	if (!icon || !wwin)
		return;

	/* Remove the previous icon title */
	if (icon->icon_name != NULL)
		wfree(icon->icon_name);
	icon->icon_name = NULL;

	/* the application-icon stub for a shared_appicon fake group leader
	 * has client_win pointing at a window wmaker created with no name
	 * properties at all, so reading them back is three round-trips for
	 * a known-empty result, fall through to the visible-name update
	 * with no title which is what the reads would have produced */
	if (wwin->fake_group && wwin->fake_group->leader == wwin->client_win) {
		wNETWMUpdateVisibleIconName(wwin, NULL, NULL);
		return;
	}

	/* Set the new one, using two methods to identify
	the icon name or switch back to window name */
	icon->icon_name = wNETWMGetIconName(wwin->client_win);
	if (icon->icon_name) {
		/* treat empty string as unset */
		if (icon->icon_name[0] == '\0') {
			wfree(icon->icon_name);
			icon->icon_name = NULL;
		}
	}

	if (!icon->icon_name)
		if (!wGetIconName(wdisp->conn, wwin->client_win, &icon->icon_name)) {
			icon->icon_name = wNETWMGetWindowName(wwin->client_win);
			from_client = false;
		}

	/* publish _NET_WM_VISIBLE_ICON_NAME when we fall back to a name
	 * the client did not set as its icon name, remove it otherwise */
	wNETWMUpdateVisibleIconName(wwin, icon->icon_name,
				    from_client ? icon->icon_name : NULL);
}

int wIconEffectiveSize(void)
{
	int size;

	/* the forked crash monitor runs this dialog path before any
	 * domain was loaded, fall back to the schema defaults */
	if (!w_global.config)
		return 48;

	size = w_global.config->icon_size;

	if (w_global.config->enforce_icon_margin) {
		size = (int)((double)w_global.config->icon_size * 0.75 + 0.5);
		size = (size >> 2) << 2;
	}
	return size;
}

int wIconChangeImageFile(WIcon *icon, const char *file)
{
	WScreen *scr = icon->core->screen_ptr;
	char *path;
	RImage *image = NULL;

	/* If no new image, don't do nothing */
	if (!file)
		return 1;

	/* Find the new image */
	path = FindImage(w_global.config->icon_path, file);
	if (!path)
		return 0;

	image = get_rimage_from_file(scr, path, wIconEffectiveSize());
	if (!image) {
		wfree(path);
		return 0;
	}

	/* Set the new image */
	set_icon_image_from_image(icon, image);
	icon->file = wstrdup(path);
	update_icon_pixmap(icon);

	wfree(path);
	return 1;
}

static char *get_name_for_wwin(WWindow *wwin)
{
	return get_name_for_instance_class(wwin->wm_instance, wwin->wm_class);
}

char *get_name_for_instance_class(const char *wm_instance, const char *wm_class)
{
	char *suffix;
	int len;

	/* Reject path separators in WM_CLASS/WM_INSTANCE to prevent
	 * path traversal via the icon cache filename */
	if (wm_class && strchr(wm_class, '/'))
		wm_class = NULL;
	if (wm_instance && strchr(wm_instance, '/'))
		wm_instance = NULL;

	if (wm_class && wm_instance) {
		len = strlen(wm_class) + strlen(wm_instance) + 2;
		suffix = wmalloc(len);
		snprintf(suffix, len, "%s.%s", wm_instance, wm_class);
	} else if (wm_class) {
		len = strlen(wm_class) + 1;
		suffix = wmalloc(len);
		snprintf(suffix, len, "%s", wm_class);
	} else if (wm_instance) {
		len = strlen(wm_instance) + 1;
		suffix = wmalloc(len);
		snprintf(suffix, len, "%s", wm_instance);
	} else {
		return NULL;
	}

	return suffix;
}

/*
 * The directory wIconStore() writes cached app icons into
 * (<cache-root>/CachedPixmaps). Computed once and cached for the lifetime
 * of the process. The directory is not created here so read-side callers
 * can use it without side effects.
 */
const char *wGetIconCacheDir(void)
{
	static char *path;
	const char *prefix;

	if (path)
		return path;

	prefix = wusercachepath();
	if (prefix)
		path = wstrconcat(prefix, CACHE_ICON_PATH "/");

	return path;
}

/*
 * Returns a freshly allocated copy of the icon cache directory path,
 * creating the directory if it does not yet exist. Callers must wfree()
 * the result. Used by the write side (wIconStore, remove_cache_icon).
 */
static char *get_icon_cache_path(void)
{
	char *path;
	int ret;

	path = wstrdup(wGetIconCacheDir());
	if (!path)
		return NULL;

	/* If the folder exists, exit */
	if (access(path, F_OK) == 0)
		return path;

	/* Create the folder */
	ret = wmkdirhier(path);

	/* Exit 1 on success, 0 on failure */
	if (ret == 1)
		return path;

	/* Fail */
	wfree(path);
	return NULL;
}

static RImage *get_wwindow_image_from_wmhints(WWindow *wwin)
{
	WXWMHints *hints = wwin->wm_hints;

	if (!hints || !(hints->flags & XCB_ICCCM_WM_HINT_ICON_PIXMAP) || hints->icon_pixmap == XCB_NONE)
		return NULL;

	/* the pixmap and mask XIDs come from a manage-time property and
	 * the WM_HINTS PropertyNotify handler drops this cache when the
	 * client changes them, so the decoded image is stable between
	 * those points and the appicon, miniwindow, titlebar icon and
	 * wIconStore can all share one server read */
	if (!wwin->wm_hints_icon_image)
		wwin->wm_hints_icon_image =
			RCreateImageFromDrawable(wwin->screen_ptr->rcontext,
						 hints->icon_pixmap,
						 (hints->flags & XCB_ICCCM_WM_HINT_ICON_MASK)
						 ? hints->icon_mask : XCB_NONE);

	return RRetainImage(wwin->wm_hints_icon_image);
}

/*
 * Best icon for the titlebar slot, mirroring the wIconUpdate() source
 * order. Returns a new reference the caller must RReleaseImage(), or
 * NULL when nothing usable is found. IconWindowHint-only clients are
 * skipped (snapshotting an unmapped icon window is unreliable). When
 * the client supplies nothing, or always_user_icon is set, falls back
 * to the class/instance DB so the titlebar matches the miniwindow and
 * switch panel. The global default icon is not used here, a blank
 * titlebar slot is preferable to the generic placeholder at ~14 px.
 */
RImage *wWindowGetIconImage(WWindow *wwin)
{
	RImage *image = NULL;
	char *file;

	if (!WFLAGP(wwin, always_user_icon)) {
		if (wwin->net_icon_image)
			return RRetainImage(wwin->net_icon_image);
		if (wwin->wm_hints && (wwin->wm_hints->flags & XCB_ICCCM_WM_HINT_ICON_PIXMAP)) {
			image = get_wwindow_image_from_wmhints(wwin);
			if (image)
				return image;
		}
	}

	file = get_icon_filename(wwin->wm_instance, wwin->wm_class, NULL, false);
	if (file) {
		image = get_rimage_from_file(wwin->screen_ptr, file,
					     wIconEffectiveSize());
		wfree(file);
	}
	return image;
}

/*
 * wIconStore--
 * 	Stores the client supplied icon at CACHE_ICON_PATH
 * and returns the path for that icon. Returns NULL if there is no
 * client supplied icon or on failure.
 *
 * Side effects:
 * 	New directories might be created.
 */
char *wIconStore(WIcon *icon)
{
	char *path, *dir_path, *file, *filename;
	int len = 0;
	RImage *image = NULL;
	WWindow *wwin = icon->owner;

	if (!wwin)
		return NULL;

	dir_path = get_icon_cache_path();
	if (!dir_path)
		return NULL;

	file = get_name_for_wwin(wwin);
	if (!file) {
		wfree(dir_path);
		return NULL;
	}

	/* Create the file name */
	len = strlen(file) + 5;
	filename = wmalloc(len);
	snprintf(filename, len, "%s.xpm", file);
	wfree(file);

	/* Create the full path, including the filename */
	len = strlen(dir_path) + strlen(filename) + 1;
	path = wmalloc(len);
	snprintf(path, len, "%s%s", dir_path, filename);
	wfree(dir_path);

	/* If icon exists, exit */
	if (access(path, F_OK) == 0) {
		wfree(path);
		return filename;
	}

	if (wwin->net_icon_image)
		image = RRetainImage(wwin->net_icon_image);
	else
		image = get_wwindow_image_from_wmhints(wwin);

	(void) icon;

	if (!image) {
		wfree(path);
		wfree(filename);
		return NULL;
	}

	if (!RSaveImage(image, path, "XPM")) {
		wfree(filename);
		filename = NULL;
	}

	wfree(path);
	RReleaseImage(image);

	return filename;
}

void remove_cache_icon(char *filename)
{
	char *cachepath;
	char real_cache[PATH_MAX];
	char real_file[PATH_MAX];

	if (!filename)
		return;

	cachepath = get_icon_cache_path();
	if (!cachepath)
		return;

	/* Resolve both paths so a filename containing '..' cannot escape
	 * the cache directory via a prefix-only strncmp check */
	if (!realpath(cachepath, real_cache) || !realpath(filename, real_file)) {
		wfree(cachepath);
		return;
	}

	if (strncmp(real_file, real_cache, strlen(real_cache)) == 0) {
		/* ensure the match is at a path boundary */
		size_t clen = strlen(real_cache);
		if (real_cache[clen - 1] == '/' || real_file[clen] == '/' || real_file[clen] == '\0')
			unlink(filename);
	}

	wfree(cachepath);
}

static void cycleColor(void *data)
{
	WIcon *icon = (WIcon *) data;

	icon->step--;
	drawIconSelection(icon);

	/* the static rectangle is enough to mark the icon selected
	 * while no output is powered, stop re-arming so an idle
	 * session can reach zero wakeups, wIconResumeBlink() restarts
	 * the cycle when the display comes back */
	if (w_global.display_off) {
		icon->handlerID = NULL;
		return;
	}
	icon->handlerID = WMAddTimerHandler(COLOR_CYCLE_DELAY, cycleColor, icon);
}

static void resume_icon_blink(WIcon *icon)
{
	if (icon && icon->selected && !icon->handlerID
	    && !w_global.config->dont_blink)
		icon->handlerID = WMAddTimerHandler(10, cycleColor, icon);
}

void wIconResumeBlink(WScreen *scr)
{
	WAppIcon *aicon;
	WWindow *wwin;

	aicon = scr->app_icon_list;
	while (aicon) {
		resume_icon_blink(aicon->icon);
		aicon = aicon->next;
	}

	wwin = scr->focused_window;
	while (wwin) {
		if (wwin->flags.miniaturized)
			resume_icon_blink(wwin->icon);
		wwin = wwin->prev;
	}
}

void wIconSetHighlited(WIcon *icon, bool flag)
{
	if (icon->highlighted == flag)
		return;

	icon->highlighted = flag;
	update_icon_pixmap(icon);
}

void wIconSelect(WIcon *icon)
{
	icon->selected = !icon->selected;

	if (icon->selected) {
		icon->step = 0;
		if (!w_global.config->dont_blink)
			icon->handlerID = WMAddTimerHandler(10, cycleColor, icon);
		else
			drawIconSelection(icon);
	} else {
		WMDeleteTimerHandlerPtr(&icon->handlerID);
		xcb_clear_area(wdisp->conn, true, icon->core->window, 0, 0, icon->core->width, icon->core->height);
	}
}

static void unset_icon_image(WIcon *icon)
{
	if (icon->file) {
		wfree(icon->file);
		icon->file = NULL;
	}

	if (icon->file_image) {
		RReleaseImage(icon->file_image);
		icon->file_image = NULL;
	}
}

/* Resize one icon's top-level window and reload its image at the
 * current IconSize. The position is left untouched, the dock relayout
 * and wArrangeIcons() that follow a runtime IconSize change reposition
 * everything once every icon is at the new size. The cached
 * file_image is dropped first because get_rimage_icon_from_user_icon()
 * is otherwise a no-op when an image is already loaded, so it would
 * keep the old-size scale. */
static void resize_icon(WIcon *icon, const char *wm_instance,
			const char *wm_class, const char *command)
{
	int isize;

	if (!icon || !icon->core)
		return;

	isize = w_global.config->icon_size;
	xcb_configure_window(wdisp->conn, icon->core->window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, (const uint32_t[]){ isize, isize });
	icon->core->width = isize;
	icon->core->height = isize;
	WMScreenNoteDrawableSize(icon->core->screen_ptr->wmscreen,
				 icon->core->window, isize, isize);

	unset_icon_image(icon);
	set_icon_image_from_database(icon, wm_instance, wm_class, command);
	wIconUpdate(icon);
}

void wIconResizeAll(WScreen *scr)
{
	WAppIcon *aicon;
	WWindow *wwin;

	aicon = scr->app_icon_list;
	while (aicon) {
		resize_icon(aicon->icon, aicon->wm_instance,
			    aicon->wm_class, aicon->command);
		wAppIconPaint(aicon);
		aicon = aicon->next;
	}

	wwin = scr->focused_window;
	while (wwin) {
		if (wwin->icon && wwin->flags.miniaturized)
			resize_icon(wwin->icon, wwin->wm_instance,
				    wwin->wm_class, NULL);
		wwin = wwin->prev;
	}
}

void set_icon_image_from_image(WIcon *icon, RImage *image)
{
	if (!icon)
		return;

	unset_icon_image(icon);

	icon->file_image = NULL;
	icon->file_image = image;
}

void set_icon_minipreview(WIcon *icon, RImage *image)
{
	xcb_pixmap_t tmp;
	RImage *scaled_mini_preview;
	WScreen *scr = icon->core->screen_ptr;

	scaled_mini_preview = RSmoothScaleImage(image, w_global.config->minipreview_size - 2 * MINIPREVIEW_BORDER,
	                                  w_global.config->minipreview_size - 2 * MINIPREVIEW_BORDER);

	if (RConvertImage(scr->rcontext, scaled_mini_preview, &tmp)) {
		if (icon->mini_preview != XCB_NONE)
			xcb_free_pixmap(wdisp->conn, icon->mini_preview);
		icon->mini_preview = tmp;
	}
	RReleaseImage(scaled_mini_preview);
}

void wIconUpdate(WIcon *icon)
{
	WWindow *wwin = NULL;

	if (icon && icon->owner)
		wwin = icon->owner;

	if (wwin && WFLAGP(wwin, always_user_icon)) {
		/* Forced use user_icon */
		get_rimage_icon_from_user_icon(icon);
	} else if (icon->icon_win != XCB_NONE) {
		/* Get the Pixmap from the WIcon */
		get_rimage_icon_from_icon_win(icon);
	} else if (wwin && wwin->net_icon_image) {
		/* Use _NET_WM_ICON icon */
		get_rimage_icon_from_x11(icon);
	} else if (wwin && wwin->wm_hints && (wwin->wm_hints->flags & XCB_ICCCM_WM_HINT_ICON_PIXMAP)) {
		/* Get the Pixmap from the wm_hints, else, from the user */
		unset_icon_image(icon);
		icon->file_image = get_rimage_icon_from_wm_hints(icon);
		if (!icon->file_image)
			get_rimage_icon_from_user_icon(icon);
	} else {
		/* Get the Pixmap from the user */
		get_rimage_icon_from_user_icon(icon);
	}

	update_icon_pixmap(icon);
}

void update_icon_pixmap(WIcon *icon)
{
	if (icon->pixmap != XCB_NONE)
		xcb_free_pixmap(wdisp->conn, icon->pixmap);

	icon->pixmap = XCB_NONE;

	/* Create the pixmap */
	if (icon->file_image)
		icon_update_pixmap(icon, icon->file_image);

	/* If dockapp, put inside the icon */
	if (icon->icon_win != XCB_NONE) {
		/* file_image is NULL, because is docked app */
		icon_update_pixmap(icon, NULL);
		set_dockapp_in_icon(icon);
	}

	/* No pixmap, set default background */
	if (icon->pixmap != XCB_NONE)
		xcb_change_window_attributes(wdisp->conn, icon->core->window, XCB_CW_BACK_PIXMAP, (const uint32_t[]){ icon->pixmap });

	/* Paint it */
	wIconPaint(icon);
}

static void get_rimage_icon_from_x11(WIcon *icon)
{
	/* Remove the icon image */
	unset_icon_image(icon);

	/* Set the new icon image */
	icon->file_image = RRetainImage(icon->owner->net_icon_image);
}

static void get_rimage_icon_from_user_icon(WIcon *icon)
{
	if (icon->file_image)
		return;

	get_rimage_icon_from_default_icon(icon);
}

static void get_rimage_icon_from_default_icon(WIcon *icon)
{
	WScreen *scr = icon->core->screen_ptr;

	/* If the icon don't have image, we should use the default image. */
	if (!scr->def_icon_rimage)
		scr->def_icon_rimage = get_default_image(scr);

	/* Remove the icon image */
	unset_icon_image(icon);

	/* Set the new icon image */
	icon->file_image = RRetainImage(scr->def_icon_rimage);
}

/* Get the RImage from the WIcon of the WWindow */
static void get_rimage_icon_from_icon_win(WIcon *icon)
{
	RImage *image;

	/* Create the new RImage */
	image = get_window_image_from_x11(icon->icon_win);

	/* Free the icon info */
	unset_icon_image(icon);

	/* Set the new info */
	icon->file_image = image;
}

/* Set the dockapp in the WIcon */
static void set_dockapp_in_icon(WIcon *icon)
{
	WXWindowAttributes attr;
	WScreen *scr = icon->core->screen_ptr;
	unsigned int w, h, d;

	/* Reparent the dock application to the icon */

	/* We need the application size to center it
	 * and show in the correct position */
	if (!getSize(icon->icon_win, &w, &h, &d)) {
		wwarning("Drawable invalid, skip reparenting dock app to icon.");
		return;
	}

	/* Set the background pixmap */
	xcb_change_window_attributes(wdisp->conn, icon->core->window, XCB_CW_BACK_PIXMAP, (const uint32_t[]){ scr->icon_tile_pixmap });

	/* Set the icon border */
	xcb_configure_window(wdisp->conn, icon->icon_win, XCB_CONFIG_WINDOW_BORDER_WIDTH, (const uint32_t[]){ 0 });

	/* Put the dock application in the icon */
	xcb_reparent_window(wdisp->conn, icon->icon_win, icon->core->window, (w_global.config->icon_size - w) / 2, (w_global.config->icon_size - h) / 2);

	/* Show it and save */
	xcb_map_window(wdisp->conn, icon->icon_win);
	xcb_change_save_set(wdisp->conn, XCB_SET_MODE_INSERT, icon->icon_win);

	/* Needed to move the icon clicking on the application part */
	if ((wXGetWindowAttributes(wdisp->conn, icon->icon_win, &attr, NULL)) &&
	    (attr.all_event_masks & XCB_EVENT_MASK_BUTTON_PRESS))
		wHackedGrabButton(XCB_BUTTON_INDEX_1, MOD_MASK, icon->core->window, true,
				  XCB_EVENT_MASK_BUTTON_PRESS, XCB_GRAB_MODE_SYNC, XCB_GRAB_MODE_ASYNC,
				  XCB_NONE, wdisp->cursor[WCUR_ARROW]);
}

/* Get the RImage from the XWindow wm_hints */
RImage *get_rimage_icon_from_wm_hints(WIcon *icon)
{
	RImage *image = NULL;
	WWindow *wwin;

	if ((!icon) || (!icon->owner))
		return NULL;

	wwin = icon->owner;

	if (!wwin->wm_hints)
		return NULL;

	image = get_wwindow_image_from_wmhints(wwin);
	if (!image) {
		/* RCreateImageFromDrawable already issued a GetGeometry on
		 * the pixmap, a NULL here means it was bad or empty */
		wwin->wm_hints->flags &= ~XCB_ICCCM_WM_HINT_ICON_PIXMAP;
		return NULL;
	}

	/* Resize the icon to the w_global.config->icon_size size */
	image = wIconValidateIconSize(image, w_global.config->icon_size);

	return image;
}

/* This function updates in the screen the icon title */
static void update_icon_title(WIcon *icon)
{
	WScreen *scr = icon->core->screen_ptr;
	int x, l, w;
	char *tmp;

	/* draw the icon title */
	if (icon->show_title && icon->icon_name != NULL) {
		tmp = ShrinkString(scr->icon_title_font, icon->icon_name, WUNSCALE(scr, w_global.config->icon_size - 4));
		w = WSCALE(scr, WMWidthOfString(scr->icon_title_font, tmp, l = strlen(tmp)));

		if (w > icon->core->width - 4)
			x = (icon->core->width - 4) - w;
		else
			x = (icon->core->width - w) / 2;

		WMDrawString(scr->wmscreen, icon->core->window, scr->icon_title_color,
			     scr->icon_title_font, x, 1, tmp, l);
		wfree(tmp);
	}
}


void wIconPaint(WIcon *icon)
{
	if (!icon || !icon->core || !icon->core->screen_ptr)
		return;

	xcb_clear_area(wdisp->conn, 0, icon->core->window, 0, 0, 0, 0);

	update_icon_title(icon);

	if (icon->selected)
		drawIconSelection(icon);
}

/******************************************************************/

static void miniwindowExpose(WObjDescriptor *desc, xcb_generic_event_t *event)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) event;

	wIconPaint(desc->parent);
}

static void miniwindowDblClick(WObjDescriptor *desc, xcb_generic_event_t *event)
{
	WIcon *icon = desc->parent;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) event;

	assert(icon->owner != NULL);

	wDeiconifyWindow(icon->owner);
}

static void miniwindowMouseDown(WObjDescriptor *desc, xcb_generic_event_t *event)
{
	WIcon *icon = desc->parent;
	WWindow *wwin = icon->owner;
	xcb_generic_event_t *ev = NULL;
	int x = wwin->icon_x, y = wwin->icon_y;
	int dx = ((xcb_button_press_event_t *)event)->event_x, dy = ((xcb_button_press_event_t *)event)->event_y;
	int grabbed = 0;
	int clickButton = ((xcb_button_press_event_t *)event)->detail;
	bool hasMoved = false;

	if (WCHECK_STATE(WSTATE_MODAL))
		return;

	if (IsDoubleClick(icon->core->screen_ptr, event)) {
		miniwindowDblClick(desc, event);
		return;
	}

	if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_1) {
		if (((xcb_button_press_event_t *)event)->state & MOD_MASK)
			wLowerFrame(icon->core);
		else
			wRaiseFrame(icon->core);
		if (((xcb_button_press_event_t *)event)->state & XCB_MOD_MASK_SHIFT) {
			wIconSelect(icon);
			wSelectWindow(icon->owner, !wwin->flags.selected);
		}
	} else if (((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_3) {
		WObjDescriptor *desc;

		OpenMiniwindowMenu(wwin, ((xcb_button_press_event_t *)event)->root_x, ((xcb_button_press_event_t *)event)->root_y);

		/* allow drag select of menu */
		desc = &wwin->screen_ptr->window_menu->menu->descriptor;
		event->response_type |= 0x80;
		(*desc->handle_mousedown) (desc, event);

		return;
	}

	if (wXGrabPointer(wdisp->conn, icon->core->window, false, XCB_EVENT_MASK_BUTTON_MOTION
			 | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS, XCB_GRAB_MODE_ASYNC,
			 XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE, XCB_CURRENT_TIME) != WXGrabSuccess) {
	}
	while (1) {
		free(ev);
		ev = NULL;
		WMMaskEvent(wdisp->conn, XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS
			    | XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_EXPOSURE, &ev);
		if (!ev)
			break;
		switch ((ev->response_type & 0x7f)) {
		case XCB_EXPOSE:
			WMHandleEvent(ev);
			break;

		case XCB_MOTION_NOTIFY:
			hasMoved = true;
			if (!grabbed) {
				if (abs(dx - (((xcb_motion_notify_event_t *)ev)->event_x)) >= MOVE_THRESHOLD
				    || abs(dy - (((xcb_motion_notify_event_t *)ev)->event_y)) >= MOVE_THRESHOLD) {
					wXChangeActivePointerGrab(wdisp->conn, XCB_EVENT_MASK_BUTTON_MOTION
								 | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS,
								 wdisp->cursor[WCUR_MOVE], XCB_CURRENT_TIME);
					grabbed = 1;
				} else {
					break;
				}
			}
			x = (((xcb_motion_notify_event_t *)ev)->root_x) - dx;
			y = (((xcb_motion_notify_event_t *)ev)->root_y) - dy;
			xcb_configure_window(wdisp->conn, icon->core->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ x, y });
			break;

		case XCB_BUTTON_PRESS:
			break;

		case XCB_BUTTON_RELEASE:
			if ((((xcb_button_press_event_t *)ev)->detail) != clickButton)
				break;

			if (wwin->icon_x != x || wwin->icon_y != y)
				wwin->flags.icon_moved = 1;

			xcb_configure_window(wdisp->conn, icon->core->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ x, y });

			wwin->icon_x = x;
			wwin->icon_y = y;
			wXUngrabPointer(wdisp->conn, XCB_CURRENT_TIME);

			if (w_global.config->auto_arrange_icons)
				wArrangeIcons(wwin->screen_ptr, true);
			if (w_global.config->single_click && !hasMoved)
				miniwindowDblClick(desc, event);
			free(ev);
			return;

		}
	}
}

void set_icon_image_from_database(WIcon *icon, const char *wm_instance, const char *wm_class, const char *command)
{
	char *file = NULL;

	file = get_icon_filename(wm_instance, wm_class, command, false);
	if (file) {
		/* removeAppIconFor() calls this on a docked icon that may
		 * still hold the live _NET_WM_ICON image and its cache path,
		 * release those before replacing them */
		unset_icon_image(icon);
		icon->file = wstrdup(file);
		icon->file_image = get_rimage_from_file(icon->core->screen_ptr, icon->file, wIconEffectiveSize());
		wfree(file);
	}
}
