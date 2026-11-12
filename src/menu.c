/* menu.c- generic menu, used for root menu, application menus etc.
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 1998-2003 Dan Pascu
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

#include <xkbcommon/xkbcommon-keysyms.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <ctype.h>

#include <cairo.h>
#include <cairo-xcb.h>
#include <WINGs/WINGsP.h>

#include "WindowMaker.h"
#include "wcore.h"
#include "framewin.h"
#include "menu.h"
#include "actions.h"
#include "wxkb.h"
#include "winmenu.h"
#include "stacking.h"
#include "xinerama.h"
#include "workspace.h"
#ifdef VIRTUAL_DESKTOP
#include "vdesk.h"
#endif
#include "dialog.h"
#include "rootmenu.h"
#include "switchmenu.h"


#define MOD_MASK wdisp->modifier_mask

#define MENU_SCROLL_STEP  menuScrollParameters[(int)w_global.config->menu_scroll_speed].steps
#define MENU_SCROLL_DELAY menuScrollParameters[(int)w_global.config->menu_scroll_speed].delay

#define MENUW(m)	((m)->frame->core->width+2*(m)->frame->screen_ptr->frame_border_width)
#define MENUH(m)	((m)->frame->core->height+2*(m)->frame->screen_ptr->frame_border_width)

/***** Local Stuff ******/

static struct {
	int steps;
	int delay;
} menuScrollParameters[5] = {
	{
	MENU_SCROLL_STEPS_UF, MENU_SCROLL_DELAY_UF}, {
	MENU_SCROLL_STEPS_F, MENU_SCROLL_DELAY_F}, {
	MENU_SCROLL_STEPS_M, MENU_SCROLL_DELAY_M}, {
	MENU_SCROLL_STEPS_S, MENU_SCROLL_DELAY_S}, {
MENU_SCROLL_STEPS_US, MENU_SCROLL_DELAY_US}};

static void menuMouseDown(WObjDescriptor * desc, xcb_generic_event_t * event);
static void menuMotionNotify(WObjDescriptor * desc, xcb_generic_event_t * event);
static void menuLeaveNotify(WObjDescriptor * desc, xcb_generic_event_t * event);
static void menuExpose(WObjDescriptor * desc, xcb_generic_event_t * event);
static void menuTitleDoubleClick(WCoreWindow * sender, void *data, xcb_generic_event_t * event);
static void menuTitleMouseDown(WCoreWindow * sender, void *data, xcb_generic_event_t * event);
static void menuCloseClick(WCoreWindow * sender, void *data, xcb_generic_event_t * event);
static void updateTexture(WMenu * menu);
static bool saveMenuRecurs(WMPropList * menus, WScreen * scr, WMenu * menu);
static int restoreMenuRecurs(WScreen *scr, WMPropList *menus, WMenu *menu, const char *path);
static void selectEntry(WMenu * menu, int entry_no);
static void closeCascade(WMenu * menu);

/*
 * Hidden-entry layout helpers. A hidden entry stays in the entries[] array
 * (so its MC_* index keeps resolving) but contributes zero height and is
 * skipped by paint, hit-testing and keyboard navigation. visible_count is
 * recomputed on every layout pass in wMenuRealize() and cached on the menu
 * so the per-paint cost is O(1) for the common all-visible case.
 */
static int menuVisibleCount(const WMenu *menu)
{
	int i, n = 0;

	for (i = 0; i < menu->entry_no; i++)
		if (!menu->entries[i]->flags.hidden)
			n++;
	return n;
}

static int menuVisibleSlot(const WMenu *menu, int index)
{
	int i, slot = 0;

	if (index < 0)
		return -1;
	for (i = 0; i < index && i < menu->entry_no; i++)
		if (!menu->entries[i]->flags.hidden)
			slot++;
	return slot;
}

static int menuEntryAtPoint(const WMenu *menu, int y)
{
	int i, slot;

	if (y < 0 || menu->entry_height <= 0)
		return -1;
	slot = y / menu->entry_height;
	if (slot < 0)
		return -1;
	for (i = 0; i < menu->entry_no; i++) {
		if (menu->entries[i]->flags.hidden)
			continue;
		if (slot == 0)
			return i;
		slot--;
	}
	return -1;
}

static int menuFirstVisible(const WMenu *menu)
{
	int i;

	for (i = 0; i < menu->entry_no; i++)
		if (!menu->entries[i]->flags.hidden)
			return i;
	return -1;
}

static int menuLastVisible(const WMenu *menu)
{
	int i, last = -1;

	for (i = 0; i < menu->entry_no; i++)
		if (!menu->entries[i]->flags.hidden)
			last = i;
	return last;
}

static int menuStepVisible(const WMenu *menu, int index, int dir)
{
	int i, start, end, step;

	if (dir > 0) {
		start = index + 1;
		end = menu->entry_no;
		step = 1;
	} else {
		start = index - 1;
		end = -1;
		step = -1;
	}
	for (i = start; i != end; i += step) {
		if (i < 0 || i >= menu->entry_no)
			break;
		if (!menu->entries[i]->flags.hidden)
			return i;
	}
	/* wrap */
	if (dir > 0) {
		for (i = 0; i <= index && i < menu->entry_no; i++)
			if (!menu->entries[i]->flags.hidden)
				return i;
	} else {
		for (i = menu->entry_no - 1; i >= index && i >= 0; i--)
			if (!menu->entries[i]->flags.hidden)
				return i;
	}
	return -1;
}


/*
 * Track how many menus are mapped on the screen so the root window
 * PointerMotionMask for ScrollableMenus is selected only while a menu
 * exists for it to scroll, an idle desktop with no menu mapped then
 * receives no root MotionNotify even with ScrollableMenus on.
 */
static void menuTrackMapped(WMenu *menu, int delta)
{
	WScreen *scr = menu->frame->screen_ptr;
	int prev = scr->mapped_menu_count;

	scr->mapped_menu_count += delta;
	if (scr->mapped_menu_count < 0)
		scr->mapped_menu_count = 0;

	if ((prev == 0) != (scr->mapped_menu_count == 0))
		wScreenUpdateRootEventMask(scr);
}

/****** Notification Observers ******/

static void appearanceObserver(void *self, WMNotification * notif)
{
	WMenu *menu = (WMenu *) self;
	uintptr_t flags = (uintptr_t)WMGetNotificationClientData(notif);

	if (!menu->flags.realized)
		return;

	if (WMGetNotificationName(notif) == WNMenuAppearanceSettingsChanged) {
		if (flags & WFontSettings) {
			menu->flags.realized = 0;
			wMenuRealize(menu);
		}
		if (flags & WTextureSettings) {
			if (!menu->flags.brother)
				updateTexture(menu);
		}
		if (flags & (WTextureSettings | WColorSettings)) {
			wMenuPaint(menu);
		}
	} else if (menu->flags.titled) {

		if (flags & WFontSettings) {
			menu->flags.realized = 0;
			wMenuRealize(menu);
		}
		if (flags & WTextureSettings) {
			menu->frame->flags.need_texture_remake = 1;
		}
		if (flags & (WColorSettings | WTextureSettings)) {
			wFrameWindowPaint(menu->frame);
		}
	}
}

/************************************/

/*
 *----------------------------------------------------------------------
 * wMenuCreate--
 * 	Creates a new empty menu with the specified title. If main_menu
 * is True, the created menu will be a main menu, which has some special
 * properties such as being placed over other normal menus.
 * 	If title is NULL, the menu will have no titlebar.
 *
 * Returns:
 * 	The created menu.
 *----------------------------------------------------------------------
 */
WMenu *wMenuCreate(WScreen *screen, const char *title, int main_menu)
{
	WMenu *menu;
	static int brother = 0;
	int tmp, flags;

	menu = wmalloc(sizeof(WMenu));

#ifdef SINGLE_MENULEVEL
	tmp = WMSubmenuLevel;
#else
	tmp = (main_menu ? WMMainMenuLevel : WMSubmenuLevel);
#endif

	flags = WFF_SINGLE_STATE | WFF_BORDER;
	if (title) {
		flags |= WFF_TITLEBAR | WFF_CLOSE_BUTTON;
		menu->flags.titled = 1;
	}
	menu->frame =
	    wFrameWindowCreate(screen, tmp, 8, 2, 1, 1, flags | WFF_IS_MENU,
			       screen->menu_title_texture, NULL,
			       screen->menu_title_color, &screen->menu_title_font,
			       screen->w_depth, screen->w_visual, screen->w_colormap);

	menu->frame->core->descriptor.parent = menu;
	menu->frame->core->descriptor.parent_type = WCLASS_MENU;
	menu->frame->core->descriptor.handle_mousedown = menuMouseDown;

	wFrameWindowHideButton(menu->frame, WFF_CLOSE_BUTTON);

	if (title) {
		menu->frame->title = wstrdup(title);
	}

	menu->frame->flags.justification = WTJ_LEFT;

	menu->frame->button_image[WTB_CLOSE] = screen->b_pixmaps[WBUT_CLOSE];

	menu->entry_no = 0;
	menu->alloced_entries = 0;
	menu->selected_entry = -1;
	menu->entries = NULL;

	menu->frame_x = screen->app_menu_x;
	menu->frame_y = screen->app_menu_y;

	menu->frame->child = menu;

	menu->flags.lowered = 0;
	menu->flags.shaded = 0;

	/* create borders */
	if (title) {
		/* setup object descriptors */
		menu->frame->on_mousedown_titlebar = menuTitleMouseDown;
		menu->frame->on_dblclick_titlebar = menuTitleDoubleClick;
	}

	menu->frame->on_click_button[WTB_CLOSE] = menuCloseClick;

	menu->menu = wCoreCreate(menu->frame->core, 0, menu->frame->top_width, menu->frame->core->width, 10);

	menu->menu->descriptor.parent = menu;
	menu->menu->descriptor.parent_type = WCLASS_MENU;
	menu->menu->descriptor.handle_expose = menuExpose;
	menu->menu->descriptor.handle_mousedown = menuMouseDown;
	menu->menu->descriptor.handle_motionnotify = menuMotionNotify;
	menu->menu->descriptor.handle_leavenotify = menuLeaveNotify;

	/* Bare-hover highlight on a torn-off menu needs ungrabbed motion
	 * on the entry pane, which the shared WCORE_CHILD_EVENT_MASK omits
	 * so titlebars and resizebars stay wakeup-free while idle. */
	xcb_change_window_attributes(wdisp->conn, menu->menu->window, XCB_CW_EVENT_MASK, (const uint32_t[]){ WCORE_CHILD_EVENT_MASK | XCB_EVENT_MASK_POINTER_MOTION });

	menu->menu_texture_data = XCB_NONE;

	xcb_map_window(wdisp->conn, menu->menu->window);

	wXFlush(wdisp->conn);

	if (!brother) {
		brother = 1;
		menu->brother = wMenuCreate(screen, title, main_menu);
		brother = 0;
		menu->brother->flags.brother = 1;
		menu->brother->brother = menu;
	}
	WMAddNotificationObserver(appearanceObserver, menu, WNMenuAppearanceSettingsChanged, menu);

	WMAddNotificationObserver(appearanceObserver, menu, WNMenuTitleAppearanceSettingsChanged, menu);

	return menu;
}

WMenu *wMenuCreateForApp(WScreen *screen, const char *title, int main_menu)
{
	WMenu *menu;

	menu = wMenuCreate(screen, title, main_menu);
	if (!menu)
		return NULL;
	menu->flags.app_menu = 1;
	menu->brother->flags.app_menu = 1;

	return menu;
}

static void insertEntry(WMenu * menu, WMenuEntry * entry, int index)
{
	int i;

	for (i = menu->entry_no - 1; i >= index; i--) {
		menu->entries[i]->order++;
		menu->entries[i + 1] = menu->entries[i];
	}
	menu->entries[index] = entry;
}

WMenuEntry *wMenuInsertCallback(WMenu *menu, int index, const char *text,
				void (*callback) (WMenu * menu, WMenuEntry * entry), void *clientdata)
{
	WMenuEntry *entry;

	menu->flags.realized = 0;
	menu->brother->flags.realized = 0;

	/* reallocate array if it's too small */
	if (menu->entry_no >= menu->alloced_entries) {
		void *tmp;

		tmp = wrealloc(menu->entries, sizeof(WMenuEntry) * (menu->alloced_entries + 5));

		menu->entries = tmp;
		menu->alloced_entries += 5;

		menu->brother->entries = tmp;
		menu->brother->alloced_entries = menu->alloced_entries;
	}
	entry = wmalloc(sizeof(WMenuEntry));
	entry->flags.enabled = 1;
	entry->text = wstrdup(text);
	entry->icon = NULL;
	entry->cascade = -1;
	entry->clientdata = clientdata;
	entry->callback = callback;
	if (index < 0 || index >= menu->entry_no) {
		entry->order = menu->entry_no;
		menu->entries[menu->entry_no] = entry;
	} else {
		entry->order = index;
		insertEntry(menu, entry, index);
	}

	menu->entry_no++;
	menu->brother->entry_no = menu->entry_no;

	return entry;
}

void wMenuEntrySetCascade(WMenu * menu, WMenuEntry * entry, WMenu * cascade)
{
	WMenu *brother = menu->brother;
	int i, done;

	assert(menu->flags.brother == 0);

	if (entry->cascade >= 0) {
		menu->flags.realized = 0;
		brother->flags.realized = 0;
	}

	cascade->parent = menu;

	cascade->brother->parent = brother;

	done = 0;
	for (i = 0; i < menu->cascade_no; i++) {
		if (menu->cascades[i] == NULL) {
			menu->cascades[i] = cascade;
			brother->cascades[i] = cascade->brother;
			done = 1;
			entry->cascade = i;
			break;
		}
	}
	if (!done) {
		entry->cascade = menu->cascade_no;

		menu->cascades = wrealloc(menu->cascades, sizeof(menu->cascades[0]) * (menu->cascade_no + 1));
		menu->cascades[menu->cascade_no++] = cascade;

		brother->cascades = wrealloc(brother->cascades, sizeof(brother->cascades[0]) * (brother->cascade_no + 1));
		brother->cascades[brother->cascade_no++] = cascade->brother;
	}

	if (menu->flags.lowered) {

		cascade->flags.lowered = 1;
		ChangeStackingLevel(cascade->frame->core, WMNormalLevel);

		cascade->brother->flags.lowered = 1;
		ChangeStackingLevel(cascade->brother->frame->core, WMNormalLevel);
	}

	if (!menu->flags.realized)
		wMenuRealize(menu);
}

void wMenuEntryRemoveCascade(WMenu * menu, WMenuEntry * entry)
{
	assert(menu->flags.brother == 0);

	/* destroy cascade menu */
	if (entry->cascade >= 0 && menu->cascades && menu->cascades[entry->cascade] != NULL) {

		wMenuDestroy(menu->cascades[entry->cascade], true);

		menu->cascades[entry->cascade] = NULL;
		menu->brother->cascades[entry->cascade] = NULL;

		entry->cascade = -1;
	}
}

void wMenuRemoveItem(WMenu * menu, int index)
{
	int i;

	if (menu->flags.brother) {
		wMenuRemoveItem(menu->brother, index);
		return;
	}

	if (index >= menu->entry_no)
		return;

	/* destroy cascade menu */
	wMenuEntryRemoveCascade(menu, menu->entries[index]);

	/* destroy unshared data */

	if (menu->entries[index]->text)
		wfree(menu->entries[index]->text);

	if (menu->entries[index]->icon)
		wPixmapDestroy(menu->entries[index]->icon);

	wMenuEntrySetShortcutText(menu->entries[index], NULL);

	if (menu->entries[index]->free_cdata && menu->entries[index]->clientdata)
		(*menu->entries[index]->free_cdata) (menu->entries[index]->clientdata);

	wfree(menu->entries[index]);

	for (i = index; i < menu->entry_no - 1; i++) {
		menu->entries[i + 1]->order--;
		menu->entries[i] = menu->entries[i + 1];
	}
	menu->entry_no--;
	menu->brother->entry_no--;
}

/* ----- shared menu texture cache ------------------------------------- *
 *
 * In the default MS_NORMAL style every menu renders the same
 * (menu_item_texture, width, entry_height) strip and lets the server
 * tile it, so menus of the same width can share one Pixmap instead of
 * holding a copy each. MS_FLAT and MS_SINGLE_TEXTURE render full-height
 * images, so those keep per-menu ownership.
 */

struct WMenuTexCache {
	WTexture *texture;
	int width, height;
	xcb_pixmap_t pixmap;
	int refcount;
	struct WMenuTexCache *next;
};

static struct WMenuTexCache *menu_tex_cache;

static void release_menu_texture(WMenu *menu)
{
	if (menu->menu_texture_cache) {
		struct WMenuTexCache *e = menu->menu_texture_cache;
		struct WMenuTexCache **pp;

		menu->menu_texture_cache = NULL;
		menu->menu_texture_data = XCB_NONE;
		if (--e->refcount > 0)
			return;
		for (pp = &menu_tex_cache; *pp; pp = &(*pp)->next) {
			if (*pp == e) {
				*pp = e->next;
				break;
			}
		}
		FREE_PIXMAP(e->pixmap);
		wfree(e);
	} else {
		FREE_PIXMAP(menu->menu_texture_data);
	}
}

static xcb_pixmap_t renderTexture(WMenu * menu)
{
	RImage *img;
	xcb_pixmap_t pix;
	int i;
	RColor light;
	RColor dark;
	RColor mid;
	WScreen *scr = menu->menu->screen_ptr;
	RContext *rc = scr->rcontext;
	WTexture *texture = scr->menu_item_texture;
	cairo_pattern_t *pat;
	cairo_surface_t *surf;
	cairo_t *cr;
	int width, height;

	width = menu->menu->width;
	height = (w_global.config->menu_style == MS_NORMAL)
		 ? menu->entry_height : menu->menu->height + 1;

	pat = wTexturePattern(texture, width, height);
	if (pat) {
		pix = xcb_generate_id(rc->xc);
		xcb_create_pixmap(rc->xc, rc->xdepth, pix,
				  rc->xscreen->root, width, height);
		WMScreenNoteDrawableSize(scr->wmscreen, pix, width, height);

		surf = cairo_xcb_surface_create(rc->xc, (xcb_drawable_t)pix,
						rc->xvisual, width, height);
		cr = cairo_create(surf);
		cairo_set_source(cr, pat);
		cairo_paint(cr);
		cairo_pattern_destroy(pat);

		if (w_global.config->menu_style == MS_SINGLE_TEXTURE) {
			int vc = menuVisibleCount(menu);
			int s;

			for (s = 0; s + 1 < vc; s++)
				wTextureMenuBevel(cr, 0,
						       s * menu->entry_height,
						       width, menu->entry_height + 1);
			if (vc > 0)
				wTextureMenuBevel(cr, 0,
						       (vc - 1) * menu->entry_height,
						       width, menu->entry_height);
		} else if (w_global.config->menu_style == MS_NORMAL) {
			wTextureMenuBevel(cr, 0, 0, width, height);
		}

		cairo_destroy(cr);
		cairo_surface_flush(surf);
		cairo_surface_destroy(surf);
		return pix;
	}

	if (w_global.config->menu_style == MS_NORMAL) {
		img = wTextureRenderImage(texture, menu->menu->width, menu->entry_height, WREL_MENUENTRY);
	} else {
		img = wTextureRenderImage(texture, menu->menu->width, menu->menu->height + 1, WREL_MENUENTRY);
	}
	if (!img) {
		wwarning(_("could not render texture: %s"), RMessageForError(RErrorCode));

		return XCB_NONE;
	}

	if (w_global.config->menu_style == MS_SINGLE_TEXTURE) {
		light.alpha = 0;
		light.red = light.green = light.blue = 80;

		dark.alpha = 255;
		dark.red = dark.green = dark.blue = 0;

		mid.alpha = 0;
		mid.red = mid.green = mid.blue = 40;

		for (i = 1; i < menu->entry_no; i++) {
			ROperateLine(img, RSubtractOperation, 0, i * menu->entry_height - 2,
				     menu->menu->width - 1, i * menu->entry_height - 2, &mid);

			RDrawLine(img, 0, i * menu->entry_height - 1,
				  menu->menu->width - 1, i * menu->entry_height - 1, &dark);

			ROperateLine(img, RAddOperation, 0, i * menu->entry_height,
				     menu->menu->width - 1, i * menu->entry_height, &light);
		}
	}
	if (!RConvertImage(scr->rcontext, img, &pix)) {
		wwarning(_("error rendering image:%s"), RMessageForError(RErrorCode));
	}
	RReleaseImage(img);

	return pix;
}

static void updateTexture(WMenu * menu)
{
	WScreen *scr = menu->menu->screen_ptr;

	/* setup background texture */
	if (scr->menu_item_texture->any.type != WTEX_SOLID) {
		if (!menu->flags.brother) {
			release_menu_texture(menu);

			if (w_global.config->menu_style == MS_NORMAL) {
				WTexture *tex = scr->menu_item_texture;
				int w = menu->menu->width;
				int h = menu->entry_height;
				struct WMenuTexCache *e;

				for (e = menu_tex_cache; e; e = e->next)
					if (e->texture == tex && e->width == w && e->height == h)
						break;
				if (!e) {
					e = wmalloc(sizeof(*e));
					e->texture = tex;
					e->width = w;
					e->height = h;
					e->pixmap = renderTexture(menu);
					e->next = menu_tex_cache;
					menu_tex_cache = e;
				}
				e->refcount++;
				menu->menu_texture_cache = e;
				menu->menu_texture_data = e->pixmap;
			} else {
				menu->menu_texture_data = renderTexture(menu);
			}

			xcb_change_window_attributes(wdisp->conn, menu->menu->window, XCB_CW_BACK_PIXMAP, (const uint32_t[]){ menu->menu_texture_data });
			xcb_clear_area(wdisp->conn, 0, menu->menu->window, 0, 0, 0, 0);

			xcb_change_window_attributes(wdisp->conn, menu->brother->menu->window, XCB_CW_BACK_PIXMAP, (const uint32_t[]){ menu->menu_texture_data });
			xcb_clear_area(wdisp->conn, 0, menu->brother->menu->window, 0, 0, 0, 0);
		}
	} else {
		xcb_change_window_attributes(wdisp->conn, menu->menu->window, XCB_CW_BACK_PIXEL, (const uint32_t[]){ scr->menu_item_texture->any.color.pixel });
		xcb_clear_area(wdisp->conn, 0, menu->menu->window, 0, 0, 0, 0);
	}
}

void wMenuRealize(WMenu * menu)
{
	int i;
	int width, rwidth, mrwidth, mwidth;
	int theight, twidth, eheight;
	int visible_count;
	WScreen *scr = menu->frame->screen_ptr;
	static int brother_done = 0;
	int flags;

	if (!brother_done) {
		brother_done = 1;
		wMenuRealize(menu->brother);
		brother_done = 0;
	}

	flags = WFF_SINGLE_STATE | WFF_BORDER;
	if (menu->flags.titled)
		flags |= WFF_TITLEBAR | WFF_CLOSE_BUTTON;

	wFrameWindowUpdateBorders(menu->frame, flags);

	if (menu->flags.titled) {
		twidth = WSCALE(scr, WMWidthOfString(scr->menu_title_font, menu->frame->title, strlen(menu->frame->title)));
		theight = menu->frame->top_width;
		twidth += theight + (TS_NEW_GEOM(w_global.config->new_style) ? 16 : 8);
	} else {
		twidth = 0;
		theight = 0;
	}
	eheight = WSCALE(scr, WMFontHeight(scr->menu_entry_font) + 6 + w_global.config->menu_text_clearance * 2);
	menu->entry_height = eheight;
	mrwidth = 0;
	mwidth = 0;
	for (i = 0; i < menu->entry_no; i++) {
		char *text;

		if (menu->entries[i]->flags.hidden)
			continue;

		/* search widest text */
		text = menu->entries[i]->text;
		width = WSCALE(scr, WMWidthOfString(scr->menu_entry_font, text, strlen(text)) + 10);

		if (w_global.config->window_list_app_icons && menu->entries[i]->icon) {
			width += menu->entries[i]->icon->width + 4;
		}

		if (menu->entries[i]->flags.indicator) {
			width += MENU_INDICATOR_SPACE;
		}

		if (width > mwidth)
			mwidth = width;

		/* search widest text on right */
		text = menu->entries[i]->rtext;
		if (text)
			rwidth = WSCALE(scr, WMWidthOfString(scr->menu_entry_font, text, strlen(text))
			    + 10);
		else if (menu->entries[i]->cascade >= 0)
			rwidth = 16;
		else
			rwidth = 4;

		if (rwidth > mrwidth)
			mrwidth = rwidth;
	}
	mwidth += mrwidth;

	if (mwidth < twidth)
		mwidth = twidth;

	visible_count = menuVisibleCount(menu);

	wCoreConfigure(menu->menu, 0, theight, mwidth, visible_count * eheight - 1);
	wFrameWindowResize(menu->frame, mwidth, visible_count * eheight - 1
			   + menu->frame->top_width + menu->frame->bottom_width);

	updateTexture(menu);

	menu->flags.realized = 1;

	if (menu->flags.mapped)
		wMenuPaint(menu);
	if (menu->brother->flags.mapped)
		wMenuPaint(menu->brother);
}

void wMenuDestroy(WMenu * menu, int recurse)
{
	int i;

	if (menu->flags.mapped)
		menuTrackMapped(menu, -1);

	WMRemoveNotificationObserver(menu);

	/* remove any pending timers */
	WMDeleteTimerHandlerPtr(&menu->timer);

	/* call destroy handler */
	if (menu->on_destroy)
		(*menu->on_destroy) (menu);

	/* Destroy items if this menu own them. If this is the "brother" menu,
	 * leave them alone as it is shared by them.
	 */
	if (!menu->flags.brother) {
		for (i = 0; i < menu->entry_no; i++) {

			wfree(menu->entries[i]->text);

			if (menu->entries[i]->icon)
				wPixmapDestroy(menu->entries[i]->icon);

			if (menu->entries[i]->rtext)
				wfree(menu->entries[i]->rtext);
#ifdef USER_MENU

			if (menu->entries[i]->instances) {
				WMReleasePropList(menu->entries[i]->instances);
			}
#endif				/* USER_MENU */

			if (menu->entries[i]->free_cdata && menu->entries[i]->clientdata) {
				(*menu->entries[i]->free_cdata) (menu->entries[i]->clientdata);
			}
			wfree(menu->entries[i]);
		}

		if (recurse) {
			for (i = 0; i < menu->cascade_no; i++) {
				if (menu->cascades[i]) {
					if (menu->cascades[i]->flags.brother)
						wMenuDestroy(menu->cascades[i]->brother, recurse);
					else
						wMenuDestroy(menu->cascades[i], recurse);
				}
			}
		}

		if (menu->entries)
			wfree(menu->entries);

	}

	release_menu_texture(menu);

	if (menu->cascades)
		wfree(menu->cascades);

	wCoreDestroy(menu->menu);
	wFrameWindowDestroy(menu->frame);

	/* destroy copy of this menu */
	if (!menu->flags.brother && menu->brother)
		wMenuDestroy(menu->brother, false);

	wfree(menu);
}

#define F_NORMAL	0
#define F_TOP		1
#define F_BOTTOM	2
#define F_NONE		3

/* one horizontal or vertical 1 px line, +0.5 so a width-1 stroke
 * lands on a single device pixel row */
static void hvLine(cairo_t *cr, const WXColor *c, int x1, int y1, int x2, int y2)
{
	W_SetSourceXColor(cr, c);
	cairo_move_to(cr, x1 + 0.5, y1 + 0.5);
	cairo_line_to(cr, x2 + 0.5, y2 + 0.5);
	cairo_stroke(cr);
}

static void drawFrame(cairo_t *cr, WTexSolid *aux, int y, int w, int h, int type)
{
	cairo_save(cr);
	cairo_set_line_width(cr, 1.0);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);

	hvLine(cr, &aux->dim, w - 1, y, w - 1, y + h - 1);
	if (type != F_TOP && type != F_NONE)
		hvLine(cr, &aux->dim, 1, y + h - 2, w - 1, y + h - 2);

	hvLine(cr, &aux->light, 0, y, 0, y + h - 1);
	if (type != F_BOTTOM && type != F_NONE)
		hvLine(cr, &aux->light, 0, y, w - 1, y);

	if (type != F_TOP && type != F_NONE)
		hvLine(cr, &aux->dark, 0, y + h - 1, w - 1, y + h - 1);

	cairo_restore(cr);
}

static int isSnapIndicator(int type)
{
	return type >= MI_SNAP_V && type <= MI_FULLSCREEN;
}

static void paintEntry(WMenu * menu, int index, int selected)
{
	WScreen *scr = menu->frame->screen_ptr;
	xcb_window_t win = menu->menu->window;
	WMenuEntry *entry = menu->entries[index];
	WTexSolid *aux = scr->menu_item_auxtexture;
	RContext *rc = scr->rcontext;
	cairo_surface_t *isurf, *msurf;
	cairo_t *cr;
	WMColor *color;
	int x, y, w, h, tw, ix, iy;
	int type;

	if (!menu->flags.realized)
		return;
	if (entry->flags.hidden)
		return;
	h = menu->entry_height;
	w = menu->menu->width;
	y = menuVisibleSlot(menu, index) * h;

	if (w_global.config->menu_style == MS_FLAT && menuVisibleCount(menu) > 1) {
		if (menuVisibleSlot(menu, index) == 0)
			type = F_TOP;
		else if (index == menuLastVisible(menu))
			type = F_BOTTOM;
		else
			type = F_NONE;
	} else {
		type = F_NORMAL;
	}

	/* the unselected background is the window's background pixmap
	 * (the texture rendered by updateTexture()), so restore it via
	 * XClearArea before opening the cairo context that layers the
	 * selection fill, bevel, glyphs and text on top */
	if (!selected) {
		if (scr->menu_item_texture->any.type == WTEX_SOLID)
			xcb_clear_area(wdisp->conn, false, win, 0, y + 1, w - 1, h - 3);
		else
			xcb_clear_area(wdisp->conn, false, win, 0, y, w, h);
	}

	cr = WCairoBeginForDrawable(scr->wmscreen, win);
	if (!cr)
		return;

	if (selected) {
		W_SetSourceColor(cr, scr->select_color);
		cairo_rectangle(cr, 1, y + 1, w - 2, h - 3);
		cairo_fill(cr);
	}
	if (scr->menu_item_texture->any.type == WTEX_SOLID)
		drawFrame(cr, aux, y, w, h, type);

	if (selected) {
		if (entry->flags.enabled)
			color = scr->select_text_color;
		else
			color = scr->dtext_color;
	} else if (!entry->flags.enabled) {
		color = scr->dtext_color;
	} else {
		color = scr->mtext_color;
	}
	/* draw text */
	x = 5;
	if (entry->flags.indicator)
		x += MENU_INDICATOR_SPACE + 2;

	if (w_global.config->window_list_app_icons && entry->icon && entry->icon->image != XCB_NONE) {
		ix = x;
		iy = y + (h - entry->icon->height) / 2;

		isurf = cairo_xcb_surface_create(rc->xc, (xcb_drawable_t)entry->icon->image,
						 rc->xvisual, entry->icon->width,
						 entry->icon->height);
		cairo_set_source_surface(cr, isurf, ix, iy);
		if (entry->icon->mask != XCB_NONE) {
			msurf = cairo_xcb_surface_create_for_bitmap(rc->xc, rc->xscreen,
								    (xcb_pixmap_t)entry->icon->mask,
								    entry->icon->width,
								    entry->icon->height);
			cairo_mask_surface(cr, msurf, ix, iy);
			cairo_surface_destroy(msurf);
		} else {
			cairo_rectangle(cr, ix, iy, entry->icon->width, entry->icon->height);
			cairo_fill(cr);
		}
		cairo_surface_destroy(isurf);

		x += entry->icon->width + 4;
	}

	W_DrawString(cr, color, scr->menu_entry_font,
			  x, 3 + y + w_global.config->menu_text_clearance,
			  entry->text, strlen(entry->text));

	if (entry->cascade >= 0) {
		/* the cascade arrow is a bevelled wedge, not axis-aligned,
		 * so integer endpoints keep the diagonals looking as before */
		cairo_set_line_width(cr, 1.0);
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);
		W_SetSourceXColor(cr, &aux->dim);
		cairo_move_to(cr, w - 11, y + 6);
		cairo_line_to(cr, w - 6, y + h / 2 - 1);
		cairo_stroke(cr);
		W_SetSourceXColor(cr, &aux->light);
		cairo_move_to(cr, w - 11, y + h - 8);
		cairo_line_to(cr, w - 6, y + h / 2 - 1);
		cairo_stroke(cr);
		hvLine(cr, &aux->dark, w - 12, y + 6, w - 12, y + h - 8);
	}

	/* draw indicator */
	if (entry->flags.indicator &&
	    (entry->flags.indicator_on || isSnapIndicator(entry->flags.indicator_type))) {
		int iw, ih;
		WPixmap *indicator = NULL;

		switch (entry->flags.indicator_type) {
		case MI_DIAMOND:
			indicator = scr->menu_radio_indicator;
			break;
		case MI_CHECK:
			indicator = scr->menu_check_indicator;
			break;
		case MI_MINIWINDOW:
			indicator = scr->menu_mini_indicator;
			break;
		case MI_HIDDEN:
			indicator = scr->menu_hide_indicator;
			break;
		case MI_SHADED:
			indicator = scr->menu_shade_indicator;
			break;
		case MI_SNAP_V:
			indicator = scr->menu_snap_vertical_indicator;
			break;
		case MI_SNAP_H:
			indicator = scr->menu_snap_horizontal_indicator;
			break;
		case MI_CENTRAL:
			indicator = scr->menu_central_indicator;
			break;
		case MI_SNAP_RH:
			indicator = scr->menu_snap_rh_indicator;
			break;
		case MI_SNAP_LH:
			indicator = scr->menu_snap_lh_indicator;
			break;
		case MI_SNAP_TH:
			indicator = scr->menu_snap_th_indicator;
			break;
		case MI_SNAP_BH:
			indicator = scr->menu_snap_bh_indicator;
			break;
		case MI_SNAP_TL:
			indicator = scr->menu_snap_tl_indicator;
			break;
		case MI_SNAP_TR:
			indicator = scr->menu_snap_tr_indicator;
			break;
		case MI_SNAP_BL:
			indicator = scr->menu_snap_bl_indicator;
			break;
		case MI_SNAP_BR:
			indicator = scr->menu_snap_br_indicator;
			break;
		case MI_SNAP_TILED:
			indicator = scr->menu_snap_tiled_indicator;
			break;
		case MI_MAXIMIZE:
			indicator = scr->menu_maximize_indicator;
			break;
		case MI_FULLSCREEN:
			indicator = scr->menu_fullscreen_indicator;
			break;
		case MI_NONE:
		default:
			break;
		}

		if (indicator) {
			iw = indicator->width;
			ih = indicator->height;
			/* the indicator is a shape-only glyph: fill the text
			 * colour through its mask so it tracks the selected
			 * and disabled states like the label does */
			if (isSnapIndicator(entry->flags.indicator_type) && !entry->flags.indicator_on)
				W_SetSourceColor(cr, scr->dtext_color);
			else
				W_SetSourceColor(cr, color);
			msurf = cairo_xcb_surface_create_for_bitmap(rc->xc, rc->xscreen,
								    (xcb_pixmap_t)indicator->mask,
								    iw, ih);
			cairo_mask_surface(cr, msurf, 5, y + (h - ih) / 2);
			cairo_surface_destroy(msurf);
		}
	}

	/* draw right text */

	if (entry->rtext && entry->cascade < 0) {
		tw = WSCALE(scr, WMWidthOfString(scr->menu_entry_font, entry->rtext, strlen(entry->rtext)));

		W_DrawString(cr, color, scr->menu_entry_font, w - 6 - tw,
				  y + 3 + w_global.config->menu_text_clearance,
				  entry->rtext, strlen(entry->rtext));
	}

	WCairoEnd(cr);
}

/* blink the selected entry before its callback is invoked */
static void blinkMenuEntry(WMenu *menu, int index)
{
#if (MENU_BLINK_COUNT > 0 && MENU_BLINK_DELAY > 0)
	int i;

	if (w_global.config->no_animations)
		return;

	for (i = 0; i < MENU_BLINK_COUNT; i++) {
		paintEntry(menu, index, false);
		wXSync(wdisp->conn);
		wusleep(MENU_BLINK_DELAY);
		paintEntry(menu, index, true);
		wXSync(wdisp->conn);
		wusleep(MENU_BLINK_DELAY);
	}
#else
	(void) menu;
	(void) index;
#endif
}

static void move_menus(WMenu * menu, int x, int y)
{
	while (menu->parent) {
		menu = menu->parent;
		x -= MENUW(menu);
		if (!w_global.config->align_menus && menu->selected_entry >= 0) {
			y -= menuVisibleSlot(menu, menu->selected_entry) * menu->entry_height;
		}
	}
	wMenuMove(menu, x, y, true);
}

static void makeVisible(WMenu * menu)
{
	WScreen *scr = menu->frame->screen_ptr;
	int x1, y1, x2, y2, new_x, new_y;
	WMRect rect = wGetRectForHead(scr, wGetHeadForPointerLocation(scr));

	if (menu->entry_no < 0)
		return;

	x1 = menu->frame_x;
	y1 = menu->frame_y + menu->frame->top_width + menuVisibleSlot(menu, menu->selected_entry) * menu->entry_height;
	x2 = x1 + MENUW(menu);
	y2 = y1 + menu->entry_height;

	new_x = x1;
	new_y = y1;

	if (x1 < rect.pos.x) {
		new_x = rect.pos.x;
	} else if (x2 >= rect.pos.x + rect.size.width) {
		new_x = rect.pos.x + rect.size.width - MENUW(menu) - 1;
	}

	if (y1 < rect.pos.y) {
		new_y = rect.pos.y;
	} else if (y2 >= rect.pos.y + rect.size.height) {
		new_y = rect.pos.y + rect.size.height - menu->entry_height - 1;
	}

	new_y = new_y - menu->frame->top_width - menuVisibleSlot(menu, menu->selected_entry) * menu->entry_height;
	move_menus(menu, new_x, new_y);
}

static int check_key(WMenu * menu, const xcb_key_press_event_t * event)
{
	int i, ch, s;
	char buffer[32];

	if (wXkbStateUtf8(event->detail, buffer, sizeof(buffer)) < 1)
		return -1;

	ch = toupper((unsigned char) buffer[0]);

	s = (menu->selected_entry >= 0 ? menu->selected_entry + 1 : 0);

 again:
	for (i = s; i < menu->entry_no; i++) {
		if (ch == toupper(menu->entries[i]->text[0])) {
			return i;
		}
	}
	/* no match. Retry from start, if previous started from a selected entry */
	if (s != 0) {
		s = 0;
		goto again;
	}
	return -1;
}

static int keyboardMenu(WMenu * menu)
{
	xcb_generic_event_t *event = NULL;
	xkb_keysym_t ksym = XKB_KEY_NoSymbol;
	int done = 0;
	int index;
	WMenuEntry *entry;
	int old_pos_x = menu->frame_x;
	int old_pos_y = menu->frame_y;
	int new_x = old_pos_x, new_y = old_pos_y;
	WMRect rect = wGetRectForHead(menu->frame->screen_ptr,
				      wGetHeadForPointerLocation(menu->frame->screen_ptr));

	if (menu->flags.editing)
		return false;

	wXGrabKeyboard(wdisp->conn, menu->frame->core->window, true, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_CURRENT_TIME);

	if (menu->frame_y + menu->frame->top_width >= rect.pos.y + rect.size.height)
		new_y = rect.pos.y + rect.size.height - menu->frame->top_width;

	if (menu->frame_x + MENUW(menu) >= rect.pos.x + rect.size.width)
		new_x = rect.pos.x + rect.size.width - MENUW(menu) - 1;

	move_menus(menu, new_x, new_y);

	while (!done && menu->flags.mapped) {
		free(event);
		event = NULL;
		wXAllowEvents(wdisp->conn, XCB_ALLOW_ASYNC_KEYBOARD, XCB_CURRENT_TIME);
		WMMaskEvent(wdisp->conn, XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_BUTTON_PRESS
			    | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY, &event);
		if (!event)
			break;

		switch ((event->response_type & 0x7f)) {
		case XCB_KEY_PRESS:
			ksym = wXkbKeycodeToKeysym((((xcb_key_press_event_t *)event)->detail), 0);
			if (w_global.config->vi_key_menus) {
				switch (ksym) {
				case XKB_KEY_h:
					ksym = XKB_KEY_Left;
					break;

				case XKB_KEY_j:
					ksym = XKB_KEY_Down;
					break;

				case XKB_KEY_k:
					ksym = XKB_KEY_Up;
					break;

				case XKB_KEY_l:
					ksym = XKB_KEY_Right;
					break;

				}
			}
			switch (ksym) {
			case XKB_KEY_Escape:
				done = 1;
				break;

		case XKB_KEY_Home:
		case XKB_KEY_KP_Home:
			selectEntry(menu, menuFirstVisible(menu));
			makeVisible(menu);
			break;

		case XKB_KEY_End:
		case XKB_KEY_KP_End:
			selectEntry(menu, menuLastVisible(menu));
			makeVisible(menu);
			break;

		case XKB_KEY_Up:
		case XKB_KEY_KP_Up:
			if (menu->selected_entry < 0)
				selectEntry(menu, menuLastVisible(menu));
			else
				selectEntry(menu, menuStepVisible(menu, menu->selected_entry, -1));
			makeVisible(menu);
			break;

		case XKB_KEY_Down:
		case XKB_KEY_KP_Down:
			if (menu->selected_entry < 0)
				selectEntry(menu, menuFirstVisible(menu));
			else
				selectEntry(menu, menuStepVisible(menu, menu->selected_entry, 1));
			makeVisible(menu);
			break;

			case XKB_KEY_Right:
			case XKB_KEY_KP_Right:
				if (menu->selected_entry >= 0) {
					WMenuEntry *entry;
					entry = menu->entries[menu->selected_entry];

					if (entry->cascade >= 0 && menu->cascades
					    && menu->cascades[entry->cascade]->entry_no > 0) {

						wXUngrabKeyboard(wdisp->conn, XCB_CURRENT_TIME);

						selectEntry(menu->cascades[entry->cascade], 0);
						if (!keyboardMenu(menu->cascades[entry->cascade]))
							done = 1;

						wXGrabKeyboard(wdisp->conn, menu->frame->core->window, true,
							      XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_CURRENT_TIME);
					}
				}
				break;

			case XKB_KEY_Left:
			case XKB_KEY_KP_Left:
				if (menu->parent != NULL && menu->parent->selected_entry >= 0) {
					selectEntry(menu, -1);
					move_menus(menu, old_pos_x, old_pos_y);
					free(event);
					event = NULL;
					return true;
				}
				break;

			case XKB_KEY_Return:
			case XKB_KEY_KP_Enter:
				done = 2;
				break;

			default:
				index = check_key(menu, (const xcb_key_press_event_t *)event);
				if (index >= 0) {
					selectEntry(menu, index);
				}
			}
			break;

		default:
			if ((event->response_type & 0x7f) == XCB_BUTTON_PRESS)
				done = 1;

			WMHandleEvent(event);
		}

		free(event);
		event = NULL;
	}

	wXUngrabKeyboard(wdisp->conn, XCB_CURRENT_TIME);

	if (done == 2 && menu->selected_entry >= 0) {
		entry = menu->entries[menu->selected_entry];
	} else {
		entry = NULL;
	}

	if (entry && entry->callback != NULL && entry->flags.enabled && entry->cascade < 0) {
		blinkMenuEntry(menu, menu->selected_entry);
		selectEntry(menu, -1);

		if (!menu->flags.buttoned) {
			wMenuUnmap(menu);
			move_menus(menu, old_pos_x, old_pos_y);
		}
		closeCascade(menu);

		(*entry->callback) (menu, entry);
	} else {
		if (!menu->flags.buttoned) {
			wMenuUnmap(menu);
			move_menus(menu, old_pos_x, old_pos_y);
		}
		selectEntry(menu, -1);
	}

	/* returns True if returning from a submenu to a parent menu,
	 * False if exiting from menu */
	return false;
}

void wMenuMapAt(WMenu * menu, int x, int y, int keyboard)
{
	if (!menu->flags.realized) {
		menu->flags.realized = 1;
		wMenuRealize(menu);
	}

	if (!menu->flags.mapped) {
		if (w_global.config->wrap_menus) {
			WScreen *scr = menu->frame->screen_ptr;
			WMRect rect = wGetRectForHead(scr, wGetHeadForPointerLocation(scr));

			if (x < rect.pos.x)
				x = rect.pos.x;
			if (y < rect.pos.y)
				y = rect.pos.y;
			if (x + MENUW(menu) > rect.pos.x + rect.size.width)
				x = rect.pos.x + rect.size.width - MENUW(menu);
			if (y + MENUH(menu) > rect.pos.y + rect.size.height)
				y = rect.pos.y + rect.size.height - MENUH(menu);
		}

		xcb_configure_window(wdisp->conn, menu->frame->core->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ x, y });
		menu->frame_x = x;
		menu->frame_y = y;
		xcb_map_window(wdisp->conn, menu->frame->core->window);
		wRaiseFrame(menu->frame->core);
		menu->flags.mapped = 1;
		menuTrackMapped(menu, +1);
	} else {
		selectEntry(menu, 0);
	}

	if (keyboard)
		keyboardMenu(menu);
}

void wMenuMap(WMenu * menu)
{
	if (!menu->flags.realized) {
		menu->flags.realized = 1;
		wMenuRealize(menu);
	}
	if (menu->flags.app_menu && menu->parent == NULL) {
		menu->frame_x = menu->frame->screen_ptr->app_menu_x;
		menu->frame_y = menu->frame->screen_ptr->app_menu_y;
		xcb_configure_window(wdisp->conn, menu->frame->core->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ menu->frame_x, menu->frame_y });
	}
	xcb_map_window(wdisp->conn, menu->frame->core->window);
	wRaiseFrame(menu->frame->core);
	if (!menu->flags.mapped) {
		menu->flags.mapped = 1;
		menuTrackMapped(menu, +1);
	}
}

void wMenuUnmap(WMenu * menu)
{
	int i;

	xcb_unmap_window(wdisp->conn, menu->frame->core->window);
	if (menu->flags.titled && menu->flags.buttoned) {
		wFrameWindowHideButton(menu->frame, WFF_CLOSE_BUTTON);
	}
	if (menu->flags.mapped)
		menuTrackMapped(menu, -1);
	menu->flags.buttoned = 0;
	menu->flags.mapped = 0;
	menu->flags.open_to_left = 0;

	if (menu->flags.shaded) {
		wFrameWindowResize(menu->frame, menu->frame->core->width, menu->frame->top_width +
				   menu->entry_height * menuVisibleCount(menu) + menu->frame->bottom_width - 1);
		menu->flags.shaded = 0;
	}

	for (i = 0; i < menu->cascade_no; i++) {
		if (menu->cascades[i] != NULL
		    && menu->cascades[i]->flags.mapped && !menu->cascades[i]->flags.buttoned) {

			wMenuUnmap(menu->cascades[i]);
		}
	}
	menu->selected_entry = -1;
}

void wMenuPaint(WMenu * menu)
{
	int i;

	if (!menu->flags.mapped) {
		return;
	}

	/* paint entries */
	for (i = 0; i < menu->entry_no; i++) {
		paintEntry(menu, i, i == menu->selected_entry);
	}
}

void wMenuSetEnabled(WMenu * menu, int index, int enable)
{
	if (index >= menu->entry_no)
		return;
	menu->entries[index]->flags.enabled = enable;
	paintEntry(menu, index, index == menu->selected_entry);
	paintEntry(menu->brother, index, index == menu->selected_entry);
}

void wMenuSetHidden(WMenu *menu, int index, int hidden)
{
	if (index >= menu->entry_no)
		return;
	menu->entries[index]->flags.hidden = hidden;
	/* Hidden entries contribute zero height and are skipped by paint,
	 * hit-testing and keyboard nav, so the menu must be re-realised
	 * (resize window + rebuild the SINGLE_TEXTURE background pixmap)
	 * before the next paint. Callers that batch updates set
	 * flags.realized = 0 themselves; do it here too so a lone
	 * wMenuSetHidden on an already-realised menu takes effect. */
	menu->flags.realized = 0;
	menu->brother->flags.realized = 0;
}

/*
 * Replace the right-hand shortcut label of a menu entry, releasing the
 * previous one. The update*Menu functions refresh these labels every
 * time a menu opens, so plain assignment leaks the old string.
 */
void wMenuEntrySetShortcutText(WMenuEntry *entry, char *rtext)
{
	wfree(entry->rtext);
	entry->rtext = rtext;
}


static void selectEntry(WMenu * menu, int entry_no)
{
	WMenuEntry *entry;
	WMenu *submenu;
	int old_entry;

	if (menu->entries == NULL)
		return;

	if (entry_no >= menu->entry_no)
		return;

	old_entry = menu->selected_entry;
	menu->selected_entry = entry_no;

	if (old_entry != entry_no) {

		/* unselect previous entry */
		if (old_entry >= 0) {
			paintEntry(menu, old_entry, false);
			entry = menu->entries[old_entry];

			/* unmap cascade */
			if (entry->cascade >= 0 && menu->cascades) {
				if (!menu->cascades[entry->cascade]->flags.buttoned) {
					wMenuUnmap(menu->cascades[entry->cascade]);
				}
			}
		}

		if (entry_no < 0) {
			menu->selected_entry = -1;
			return;
		}
		entry = menu->entries[entry_no];

		if (entry->cascade >= 0 && menu->cascades && entry->flags.enabled) {
			/* Callback for when the submenu is opened.
			 */
			submenu = menu->cascades[entry->cascade];
			if (submenu && submenu->flags.brother)
				submenu = submenu->brother;

			if (entry->callback) {
				/* Only call the callback if the submenu is not yet mapped.
				 */
				if (menu->flags.brother) {
					if (!submenu || !submenu->flags.mapped)
						(*entry->callback) (menu->brother, entry);
				} else {
					if (!submenu || !submenu->flags.buttoned)
						(*entry->callback) (menu, entry);
				}
			}

			/* the submenu menu might have changed */
			submenu = menu->cascades[entry->cascade];

			/* map cascade */
			if (!submenu->flags.mapped) {
				int x, y;

				if (!submenu->flags.realized)
					wMenuRealize(submenu);
				if (w_global.config->wrap_menus) {
					if (menu->flags.open_to_left)
						submenu->flags.open_to_left = 1;

					if (submenu->flags.open_to_left) {
						x = menu->frame_x - MENUW(submenu);
						if (x < 0) {
							x = 0;
							submenu->flags.open_to_left = 0;
						}
					} else {
						x = menu->frame_x + MENUW(menu);

						if (x + MENUW(submenu)
						    >= menu->frame->screen_ptr->scr_width) {

							x = menu->frame_x - MENUW(submenu);
							submenu->flags.open_to_left = 1;
						}
					}
				} else {
					x = menu->frame_x + MENUW(menu);
				}

			if (w_global.config->align_menus) {
				y = menu->frame_y;
			} else {
				y = menu->frame_y + menu->entry_height * menuVisibleSlot(menu, entry_no);
				if (menu->flags.titled)
					y += menu->frame->top_width;
				if (menu->cascades[entry->cascade]->flags.titled)
					y -= menu->cascades[entry->cascade]->frame->top_width;
				}

				wMenuMapAt(menu->cascades[entry->cascade], x, y, false);
				menu->cascades[entry->cascade]->parent = menu;
			} else {
				return;
			}
		}
		paintEntry(menu, entry_no, true);
	}
}

static WMenu *findMenu(WScreen * scr, int *x_ret, int *y_ret)
{
	WMenu *menu;
	WObjDescriptor *desc;
	xcb_window_t root_ret, win, junk_win;
	int x, y, wx, wy;
	unsigned int mask;

	wXQueryPointer(wdisp->conn, scr->root_win, &root_ret, &win, &x, &y, &wx, &wy, &mask);

	if (win == XCB_NONE)
		return NULL;

	desc = WMHashGet(wdisp->context.client_win, (void *)(uintptr_t) win);
	if (desc == NULL)
		return NULL;

	if (desc->parent_type == WCLASS_MENU) {
		menu = (WMenu *) desc->parent;
		wXTranslateCoordinates(wdisp->conn, root_ret, menu->menu->window, wx, wy, x_ret, y_ret, &junk_win);
		return menu;
	}
	return NULL;
}

static void closeCascade(WMenu * menu)
{
	WMenu *parent = menu->parent;

	if (menu->flags.brother || (!menu->flags.buttoned && (!menu->flags.app_menu || menu->parent != NULL))) {

		selectEntry(menu, -1);
		wXSync(wdisp->conn);
#if (MENU_BLINK_DELAY > 2)
		if (!w_global.config->no_animations)
			wusleep(MENU_BLINK_DELAY / 2);
#endif
		wMenuUnmap(menu);
		while (parent != NULL
		       && (parent->parent != NULL || !parent->flags.app_menu || parent->flags.brother)
		       && !parent->flags.buttoned) {
			selectEntry(parent, -1);
			wMenuUnmap(parent);
			parent = parent->parent;
		}
		if (parent)
			selectEntry(parent, -1);
	}
}

static void closeBrotherCascadesOf(WMenu * menu)
{
	WMenu *tmp;
	int i;

	for (i = 0; i < menu->cascade_no; i++) {
		if (menu->cascades[i]->flags.brother) {
			tmp = menu->cascades[i];
		} else {
			tmp = menu->cascades[i]->brother;
		}
		if (tmp->flags.mapped) {
			selectEntry(tmp->parent, -1);
			closeBrotherCascadesOf(tmp);
			break;
		}
	}
}

static inline int getEntryAt(const WMenu *menu, int x, int y)
{
	(void) x;
	return menuEntryAtPoint(menu, y);
}

static WMenu *parentMenu(WMenu * menu)
{
	WMenu *parent;
	WMenuEntry *entry;

	if (menu->flags.buttoned)
		return menu;

	while (menu->parent && menu->parent->flags.mapped) {
		parent = menu->parent;
		if (parent->selected_entry < 0)
			break;
		entry = parent->entries[parent->selected_entry];
		if (!entry->flags.enabled || entry->cascade < 0 || !parent->cascades ||
		    parent->cascades[entry->cascade] != menu)
			break;
		menu = parent;
		if (menu->flags.buttoned)
			break;
	}

	return menu;
}

/*
 * Will raise the passed menu, if submenu = 0
 * If submenu > 0 will also raise all mapped submenus
 * until the first buttoned one
 * If submenu < 0 will also raise all mapped parent menus
 * until the first buttoned one
 */

static void raiseMenus(WMenu * menu, int submenus)
{
	WMenu *submenu;
	int i;

	if (!menu)
		return;

	wRaiseFrame(menu->frame->core);

	if (submenus > 0 && menu->selected_entry >= 0) {
		i = menu->entries[menu->selected_entry]->cascade;
		if (i >= 0 && menu->cascades) {
			submenu = menu->cascades[i];
			if (submenu->flags.mapped && !submenu->flags.buttoned)
				raiseMenus(submenu, submenus);
		}
	}
	if (submenus < 0 && !menu->flags.buttoned && menu->parent && menu->parent->flags.mapped)
		raiseMenus(menu->parent, submenus);
}

WMenu *wMenuUnderPointer(WScreen * screen)
{
	WObjDescriptor *desc;
	xcb_window_t root_ret, win;
	int dummy;
	unsigned int mask;

	wXQueryPointer(wdisp->conn, screen->root_win, &root_ret, &win, &dummy, &dummy, &dummy, &dummy, &mask);

	if (win == XCB_NONE)
		return NULL;

	desc = WMHashGet(wdisp->context.client_win, (void *)(uintptr_t) win);
	if (desc == NULL)
		return NULL;

	if (desc->parent_type == WCLASS_MENU)
		return (WMenu *) desc->parent;
	return NULL;
}

static void getPointerPosition(WScreen * scr, int *x, int *y)
{
	xcb_window_t root_ret, win;
	int wx, wy;
	unsigned int mask;

	wXQueryPointer(wdisp->conn, scr->root_win, &root_ret, &win, x, y, &wx, &wy, &mask);
}

static void getScrollAmount(WMenu * menu, int *hamount, int *vamount)
{
	WScreen *scr = menu->menu->screen_ptr;
	int menuX1 = menu->frame_x;
	int menuY1 = menu->frame_y;
	int menuX2 = menu->frame_x + MENUW(menu);
	int menuY2 = menu->frame_y + MENUH(menu);
	int xroot, yroot;
	WMRect rect = wGetRectForHead(scr, wGetHeadForPointerLocation(scr));

	*hamount = 0;
	*vamount = 0;

	getPointerPosition(scr, &xroot, &yroot);

	if (xroot <= (rect.pos.x + 1) && menuX1 < rect.pos.x) {
		/* scroll to the right */
		*hamount = WMIN(MENU_SCROLL_STEP, abs(menuX1));

	} else if (xroot >= (rect.pos.x + rect.size.width - 2) && menuX2 > (rect.pos.x + rect.size.width - 1)) {
		/* scroll to the left */
		*hamount = WMIN(MENU_SCROLL_STEP, abs(menuX2 - rect.pos.x - (int)rect.size.width - 1));

		if (*hamount == 0)
			*hamount = 1;

		*hamount = -*hamount;
	}

	if (yroot <= (rect.pos.y + 1) && menuY1 < rect.pos.y) {
		/* scroll down */
		*vamount = WMIN(MENU_SCROLL_STEP, abs(menuY1));

	} else if (yroot >= (rect.pos.y + rect.size.height - 2) && menuY2 > (rect.pos.y + rect.size.height - 1)) {
		/* scroll up */
		*vamount = WMIN(MENU_SCROLL_STEP, abs(menuY2 - rect.pos.y - (int)rect.size.height - 2));

		*vamount = -*vamount;
	}
}

static void dragScrollMenuCallback(void *data)
{
	WMenu *menu = (WMenu *) data;
	WScreen *scr = menu->menu->screen_ptr;
	WMenu *parent = parentMenu(menu);
	int hamount, vamount;
	int x, y;
	int newSelectedEntry;

	getScrollAmount(menu, &hamount, &vamount);

	if (hamount != 0 || vamount != 0) {
		wMenuMove(parent, parent->frame_x + hamount, parent->frame_y + vamount, true);
		if (findMenu(scr, &x, &y)) {
			newSelectedEntry = getEntryAt(menu, x, y);
			selectEntry(menu, newSelectedEntry);
		} else {
			/* Pointer fell outside of menu. If the selected entry is
			 * not a submenu, unselect it */
			if (menu->selected_entry >= 0 && menu->entries[menu->selected_entry]->cascade < 0)
				selectEntry(menu, -1);
			newSelectedEntry = 0;
		}

		/* paranoid check */
		if (newSelectedEntry >= 0) {
			/* keep scrolling */
			menu->timer = WMAddTimerHandler(MENU_SCROLL_DELAY, dragScrollMenuCallback, menu);
		} else {
			menu->timer = NULL;
		}
	} else {
		/* don't need to scroll anymore */
		menu->timer = NULL;
		if (findMenu(scr, &x, &y)) {
			newSelectedEntry = getEntryAt(menu, x, y);
			selectEntry(menu, newSelectedEntry);
		}
	}
}

static void scrollMenuCallback(void *data)
{
	WMenu *menu = (WMenu *) data;
	WMenu *parent = parentMenu(menu);
	int hamount = 0;	/* amount to scroll */
	int vamount = 0;

#ifdef VIRTUAL_DESKTOP
	/* don't scroll menus when in vdesk mode (edges own the screen edge) */
	if (!w_global.config->vdesk_enable)
#endif
		getScrollAmount(menu, &hamount, &vamount);

	if (hamount != 0 || vamount != 0) {
		wMenuMove(parent, parent->frame_x + hamount, parent->frame_y + vamount, true);

		/* keep scrolling */
		menu->timer = WMAddTimerHandler(MENU_SCROLL_DELAY, scrollMenuCallback, menu);
	} else {
		/* don't need to scroll anymore */
		menu->timer = NULL;
	}
}

#define MENU_SCROLL_BORDER   5

static int isPointNearBorder(WMenu * menu, int x, int y)
{
	int menuX1 = menu->frame_x;
	int menuY1 = menu->frame_y;
	int menuX2 = menu->frame_x + MENUW(menu);
	int menuY2 = menu->frame_y + MENUH(menu);
	int flag = 0;
	int head = wGetHeadForPoint(menu->frame->screen_ptr, wmkpoint(x, y));
	WMRect rect = wGetRectForHead(menu->frame->screen_ptr, head);

	/* XXX: handle screen joins properly !! */

	if (x >= menuX1 && x <= menuX2 &&
	    (y < rect.pos.y + MENU_SCROLL_BORDER || y >= rect.pos.y + rect.size.height - MENU_SCROLL_BORDER))
		flag = 1;
	else if (y >= menuY1 && y <= menuY2 &&
		 (x < rect.pos.x + MENU_SCROLL_BORDER || x >= rect.pos.x + rect.size.width - MENU_SCROLL_BORDER))
		flag = 1;

	return flag;
}

typedef struct _delay {
	WMenu *menu;
	int ox, oy;
} _delay;

static void callback_leaving(void *user_param)
{
	_delay *dl = (_delay *) user_param;

	wMenuMove(dl->menu, dl->ox, dl->oy, true);
	dl->menu->jump_back = NULL;
	dl->menu->menu->screen_ptr->flags.jump_back_pending = 0;
	wfree(dl);
}

void wMenuScroll(WMenu *menu)
{
	WMenu *smenu;
	WMenu *omenu = parentMenu(menu);
	WScreen *scr = menu->frame->screen_ptr;
	int done = 0;
	int jump_back = 0;
	int old_frame_x = omenu->frame_x;
	int old_frame_y = omenu->frame_y;
	xcb_generic_event_t *ev = NULL;

	if (omenu->jump_back)
		WMDeleteTimerWithClientData(omenu->jump_back);

	if (( /*omenu->flags.buttoned && */ !w_global.config->wrap_menus)
	    || omenu->flags.app_menu) {
		jump_back = 1;
	}

	if (!w_global.config->wrap_menus)
		raiseMenus(omenu, true);
	else
		raiseMenus(menu, false);

	if (!menu->timer)
		scrollMenuCallback(menu);

	while (!done) {
		int x, y, on_border, on_x_edge, on_y_edge, on_title;
		WMRect rect;

		free(ev);
		ev = NULL;
		WMNextEvent(wdisp->conn, &ev);
		if (!ev)
			break;
		switch ((ev->response_type & 0x7f)) {
		case XCB_ENTER_NOTIFY:
			WMHandleEvent(ev);
			/* Fall through. */
		case XCB_MOTION_NOTIFY:
			x = ((ev->response_type & 0x7f) == XCB_MOTION_NOTIFY) ? (((xcb_motion_notify_event_t *)ev)->root_x) : (((xcb_enter_notify_event_t *)ev)->root_x);
			y = ((ev->response_type & 0x7f) == XCB_MOTION_NOTIFY) ? (((xcb_motion_notify_event_t *)ev)->root_y) : (((xcb_enter_notify_event_t *)ev)->root_y);

			/* on_border is != 0 if the pointer is between the menu
			 * and the screen border and is close enough to the border */
			on_border = isPointNearBorder(menu, x, y);

			smenu = wMenuUnderPointer(scr);

			if ((smenu == NULL && !on_border) || (smenu && parentMenu(smenu) != omenu)) {
				done = 1;
				break;
			}

			rect = wGetRectForHead(scr, wGetHeadForPoint(scr, wmkpoint(x, y)));
			on_x_edge = x <= rect.pos.x + 1 || x >= rect.pos.x + rect.size.width - 2;
			on_y_edge = y <= rect.pos.y + 1 || y >= rect.pos.y + rect.size.height - 2;
			on_border = on_x_edge || on_y_edge;

			if (!on_border && !jump_back) {
				done = 1;
				break;
			}

			if (menu->timer && (smenu != menu || (!on_y_edge && !on_x_edge))) {
				WMDeleteTimerHandler(menu->timer);
				menu->timer = NULL;
			}

			if (smenu != NULL)
				menu = smenu;

			if (!menu->timer)
				scrollMenuCallback(menu);
			break;
		case XCB_BUTTON_PRESS:
			/* True if we push on title, or drag the omenu to other position */
			on_title = (((xcb_button_press_event_t *)ev)->root_x) >= omenu->frame_x &&
			    (((xcb_button_press_event_t *)ev)->root_x) <= omenu->frame_x + MENUW(omenu) &&
			    (((xcb_button_press_event_t *)ev)->root_y) >= omenu->frame_y &&
			    (((xcb_button_press_event_t *)ev)->root_y) <= omenu->frame_y + omenu->frame->top_width;
			WMHandleEvent(ev);
			smenu = wMenuUnderPointer(scr);
			if (smenu == NULL || (smenu && smenu->flags.buttoned && smenu != omenu))
				done = 1;
			else if (smenu == omenu && on_title) {
				jump_back = 0;
				done = 1;
			}
			break;
		case XCB_KEY_PRESS:
			done = 1;
			/* Fall through. */
		default:
			WMHandleEvent(ev);
			break;
		}

		free(ev);
		ev = NULL;
	}

	WMDeleteTimerHandlerPtr(&menu->timer);

	if (jump_back) {
		_delay *delayer;

		if (!omenu->jump_back) {
			delayer = wmalloc(sizeof(_delay));
			delayer->menu = omenu;
			delayer->ox = old_frame_x;
			delayer->oy = old_frame_y;
			omenu->jump_back = delayer;
			scr->flags.jump_back_pending = 1;
		} else
			delayer = omenu->jump_back;
		WMAddTimerHandler(MENU_JUMP_BACK_DELAY, callback_leaving, delayer);
	}
}

static void menuExpose(WObjDescriptor * desc, xcb_generic_event_t * event)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) event;

	wMenuPaint(desc->parent);
}

/*
 * Ungrabbed pointer motion over the entry pane of a mapped menu.
 * A menu opened from a right-click or menuMouseDown() runs its own
 * pointer-grab loop, so this only fires when a torn-off (buttoned)
 * menu is sitting on the desktop with no button held, which
 * previously left entries unhighlighted until the next click.
 */
static void menuMotionNotify(WObjDescriptor * desc, xcb_generic_event_t * event)
{
	WMenu *menu = desc->parent;
	int entry_no;

	if (!menu->flags.mapped || menu->flags.editing || menu->flags.inside_handler)
		return;

	entry_no = getEntryAt(menu, ((xcb_motion_notify_event_t *)event)->event_x, ((xcb_motion_notify_event_t *)event)->event_y);
	if (entry_no < 0 || entry_no >= menu->entry_no)
		entry_no = -1;

	if (entry_no != menu->selected_entry)
		selectEntry(menu, entry_no);
}

static void menuLeaveNotify(WObjDescriptor * desc, xcb_generic_event_t * event)
{
	WMenu *menu = desc->parent;
	int sel = menu->selected_entry;

	if (!menu->flags.mapped || menu->flags.inside_handler)
		return;

	/* Ignore crossings generated by a grab or ungrab, and inferior
	 * crossings into the frame border, so the highlight does not
	 * flicker when menuMouseDown() takes its pointer grab. */
	if (((xcb_enter_notify_event_t *)event)->mode != XCB_NOTIFY_MODE_NORMAL
	    || ((xcb_enter_notify_event_t *)event)->detail == XCB_NOTIFY_DETAIL_INFERIOR)
		return;

	/* Keep the highlight (and the mapped submenu it holds open) when
	 * the selected entry is a cascade so the pointer can travel into
	 * the child, matching the held-button drag behaviour. */
	if (sel >= 0 && sel < menu->entry_no
	    && menu->entries[sel]->cascade >= 0 && menu->cascades
	    && menu->cascades[menu->entries[sel]->cascade]->flags.mapped)
		return;

	selectEntry(menu, -1);
}

typedef struct {
	int *delayed_select;
	WMenu *menu;
	WMHandlerID magic;
} delay_data;

static void delaySelection(void *data)
{
	delay_data *d = (delay_data *) data;
	int x, y, entry_no;
	WMenu *menu;

	d->magic = NULL;

	menu = findMenu(d->menu->menu->screen_ptr, &x, &y);
	if (menu && (d->menu == menu || d->delayed_select)) {
		entry_no = getEntryAt(menu, x, y);
		selectEntry(menu, entry_no);
	}
	if (d->delayed_select)
		*(d->delayed_select) = 0;
}

static void menuMouseDown(WObjDescriptor * desc, xcb_generic_event_t * event)
{
	WWindow *wwin;
	const xcb_button_press_event_t *bev = (const xcb_button_press_event_t *)event;
	WMenu *menu = desc->parent;
	WMenu *smenu;
	WScreen *scr = menu->frame->screen_ptr;
	WMenuEntry *entry = NULL;
	xcb_generic_event_t *ev = NULL;
	int close_on_exit = 0;
	int done = 0;
	int delayed_select = 0;
	int entry_no;
	int x, y;
	int prevx, prevy;
	int old_frame_x = 0;
	int old_frame_y = 0;
	delay_data d_data = { NULL, NULL, NULL };

	/* Doesn't seem to be needed anymore (if delayed selection handler is
	 * added only if not present). there seem to be no other side effects
	 * from removing this and it is also possible that it was only added
	 * to avoid problems with adding the delayed selection timer handler
	 * multiple times
	 */
	/*if (menu->flags.inside_handler) {
	   return;
	   } */
	menu->flags.inside_handler = 1;

	if (!w_global.config->wrap_menus) {
		smenu = parentMenu(menu);
		old_frame_x = smenu->frame_x;
		old_frame_y = smenu->frame_y;
	} else if (((xcb_button_press_event_t *)event)->event == menu->frame->core->window) {
		/* This is true if the menu was launched with right click on root window */
		if (!d_data.magic) {
			delayed_select = 1;
			d_data.delayed_select = &delayed_select;
			d_data.menu = menu;
			d_data.magic = WMAddTimerHandler(w_global.config->double_click_time, delaySelection, &d_data);
		}
	}

	wRaiseFrame(menu->frame->core);

	close_on_exit = ((event->response_type & 0x80) || menu->flags.brother);

	smenu = findMenu(scr, &x, &y);
	if (!smenu) {
		x = -1;
		y = -1;
	} else {
		menu = smenu;
	}

	if (menu->flags.editing) {
		goto byebye;
	}
	entry_no = getEntryAt(menu, x, y);
	if ((entry_no >= 0) && (entry_no < menu->entry_no)) {
		entry = menu->entries[entry_no];

		if (!close_on_exit && (bev->state & XCB_MOD_MASK_CONTROL) && smenu && entry->flags.editable) {
			char buffer[128];
			char *name;
			int number = entry_no - smenu->ws_entry_base;

			if (number < 0 || number >= scr->workspace_count)
				goto byebye;
			name = wstrdup(scr->workspaces[number]->name);
			snprintf(buffer, sizeof(buffer), _("Type the name for workspace %i:"), number + 1);

			wMenuUnmap(scr->root_menu);

			if (wInputDialog(scr, _("Rename Workspace"), buffer, &name))
				wWorkspaceRename(scr, number, name);

			if (name)
				wfree(name);

			goto byebye;
		} else if (bev->state & XCB_MOD_MASK_CONTROL) {
			goto byebye;
		}

		if (entry->flags.enabled && entry->cascade >= 0 && menu->cascades) {
			WMenu *submenu = menu->cascades[entry->cascade];
			/* map cascade */
			if (submenu->flags.mapped && !submenu->flags.buttoned && menu->selected_entry != entry_no) {
				wMenuUnmap(submenu);
			}
			if (!submenu->flags.mapped && !delayed_select) {
				selectEntry(menu, entry_no);
			} else if (!submenu->flags.buttoned) {
				selectEntry(menu, -1);
			}

		} else if (!delayed_select) {
			if (menu == scr->switch_menu && ((xcb_button_press_event_t *)event)->detail == XCB_BUTTON_INDEX_3) {
				selectEntry(menu, entry_no);
				OpenWindowMenu2((WWindow *)entry->clientdata,
								((xcb_button_press_event_t *)event)->root_x,
								((xcb_button_press_event_t *)event)->root_y, false);
				wwin = (WWindow *)entry->clientdata;
				desc = &wwin->screen_ptr->window_menu->menu->descriptor;
				event->response_type |= 0x80;
				(*desc->handle_mousedown)(desc, event);

				wXUngrabPointer(wdisp->conn, XCB_CURRENT_TIME);
				selectEntry(menu, -1);
				return;
			} else {
				selectEntry(menu, entry_no);
			}
		}

		if (!w_global.config->wrap_menus && !w_global.config->scrollable_menus) {
			if (!menu->timer)
				dragScrollMenuCallback(menu);
		}
	}

	prevx = bev->root_x;
	prevy = bev->root_y;
#ifdef VIRTUAL_DESKTOP
	if (w_global.config->vdesk_enable)
		wWorkspaceLowerEdge(scr);
#endif
	while (!done) {
		int x, y;

		free(ev);
		ev = NULL;
		wXAllowEvents(wdisp->conn, XCB_ALLOW_ASYNC_POINTER | XCB_ALLOW_SYNC_POINTER, XCB_CURRENT_TIME);

		WMMaskEvent(wdisp->conn, XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS, &ev);
		if (!ev)
			break;
		switch ((ev->response_type & 0x7f)) {
		case XCB_MOTION_NOTIFY:
			smenu = findMenu(scr, &x, &y);

			if (smenu == NULL) {
				/* moved mouse out of menu */

				if (!delayed_select && d_data.magic) {
					WMDeleteTimerHandler(d_data.magic);
					d_data.magic = NULL;
				}
				if (menu == NULL
				    || (menu->selected_entry >= 0
					&& menu->entries[menu->selected_entry]->cascade >= 0)) {
					prevx = (((xcb_motion_notify_event_t *)ev)->root_x);
					prevy = (((xcb_motion_notify_event_t *)ev)->root_y);

					break;
				}
				selectEntry(menu, -1);
				menu = smenu;
				prevx = (((xcb_motion_notify_event_t *)ev)->root_x);
				prevy = (((xcb_motion_notify_event_t *)ev)->root_y);
				break;
			} else if (menu && menu != smenu
				   && (menu->selected_entry < 0
				       || menu->entries[menu->selected_entry]->cascade < 0)) {
				selectEntry(menu, -1);

				if (!delayed_select && d_data.magic) {
					WMDeleteTimerHandler(d_data.magic);
					d_data.magic = NULL;
				}
			} else {

				/* hysteresis for item selection */

				/* check if the motion was to the side, indicating that
				 * the user may want to cross to a submenu */
				if (!delayed_select && menu) {
					int dx;
					bool moved_to_submenu;	/* moved to direction of submenu */

					dx = abs(prevx - (((xcb_motion_notify_event_t *)ev)->root_x));

					moved_to_submenu = false;
					if (dx > 0	/* if moved enough to the side */
					    /* maybe a open submenu */
					    && menu->selected_entry >= 0
					    /* moving to the right direction */
					    && (w_global.config->align_menus || (((xcb_motion_notify_event_t *)ev)->root_y) >= prevy)) {
						int index;

						index = menu->entries[menu->selected_entry]->cascade;
						if (index >= 0) {
							if (menu->cascades[index]->frame_x > menu->frame_x) {
								if (prevx < (((xcb_motion_notify_event_t *)ev)->root_x))
									moved_to_submenu = true;
							} else {
								if (prevx > (((xcb_motion_notify_event_t *)ev)->root_x))
									moved_to_submenu = true;
							}
						}
					}

					if (menu != smenu) {
						WMDeleteTimerHandlerPtr(&d_data.magic);
					} else if (moved_to_submenu) {
						/* while we are moving, postpone the selection */
						if (d_data.magic) {
							WMDeleteTimerHandler(d_data.magic);
						}
						d_data.delayed_select = NULL;
						d_data.menu = menu;
						d_data.magic = WMAddTimerHandler(MENU_SELECT_DELAY,
										 delaySelection, &d_data);
						prevx = (((xcb_motion_notify_event_t *)ev)->root_x);
						prevy = (((xcb_motion_notify_event_t *)ev)->root_y);
						break;
					} else {
						WMDeleteTimerHandlerPtr(&d_data.magic);
					}
				}
			}
			prevx = (((xcb_motion_notify_event_t *)ev)->root_x);
			prevy = (((xcb_motion_notify_event_t *)ev)->root_y);
			if (menu != smenu) {
				/* pointer crossed menus */
				if (menu)
					WMDeleteTimerHandlerPtr(&menu->timer);
				if (smenu)
					dragScrollMenuCallback(smenu);
			}
			menu = smenu;
			if (!menu->timer)
				dragScrollMenuCallback(menu);

			if (!delayed_select) {
				entry_no = getEntryAt(menu, x, y);
				if ((entry_no >= 0) && (entry_no < menu->entry_no)) {
					entry = menu->entries[entry_no];
					if (entry->flags.enabled && entry->cascade >= 0 && menu->cascades) {
						WMenu *submenu = menu->cascades[entry->cascade];
						if (submenu->flags.mapped && !submenu->flags.buttoned
						    && menu->selected_entry != entry_no) {
							wMenuUnmap(submenu);
						}
					}
				}
				selectEntry(menu, entry_no);
			}
			break;

		case XCB_BUTTON_PRESS:
			break;

		case XCB_BUTTON_RELEASE:
			if ((((xcb_button_press_event_t *)ev)->detail) == ((xcb_button_press_event_t *)event)->detail)
				done = 1;
			break;

		case XCB_EXPOSE:
			WMHandleEvent(ev);
#ifdef VIRTUAL_DESKTOP
			/* expose raises the edge windows back up; lower them
			 * again so the menu stays above the edges */
			if (w_global.config->vdesk_enable)
				wWorkspaceLowerEdge(scr);
#endif
			break;
		}

		free(ev);
		ev = NULL;
	}

	if (menu)
		WMDeleteTimerHandlerPtr(&menu->timer);
	WMDeleteTimerHandlerPtr(&d_data.magic);

	if (menu && menu->selected_entry >= 0) {
		entry = menu->entries[menu->selected_entry];
		if (entry->callback != NULL && entry->flags.enabled && entry->cascade < 0) {
			/* blink and erase menu selection */
			blinkMenuEntry(menu, menu->selected_entry);

			/* unmap the menu, its parents and call the callback */
			if (!menu->flags.buttoned && (!menu->flags.app_menu || menu->parent != NULL)) {
				closeCascade(menu);
			} else {
				selectEntry(menu, -1);
			}
			(*entry->callback) (menu, entry);

			/* If the user double clicks an entry, the entry will
			 * be executed twice, which is not good for things like
			 * the root menu. So, ignore any clicks that were generated
			 * while the entry was being executed */
			while (WXEvqTakeTyped(wdisp->conn, XCB_BUTTON_PRESS, menu->menu->window, &ev))
				free(ev);
		} else if (entry->callback != NULL && entry->cascade < 0) {
			selectEntry(menu, -1);
		} else {
			if (entry->cascade >= 0 && menu->cascades && menu->cascades[entry->cascade]->flags.brother) {
				selectEntry(menu, -1);
			}
		}
	}

	if (((WMenu *) desc->parent)->flags.brother || close_on_exit || !smenu)
		closeCascade(desc->parent);

	/* close the cascade windows that should not remain opened */
	closeBrotherCascadesOf(desc->parent);

	if (!w_global.config->wrap_menus)
		wMenuMove(parentMenu(desc->parent), old_frame_x, old_frame_y, true);

 byebye:
	/* Just to be sure in case we skip the 2 above because of a goto byebye */
	if (menu)
		WMDeleteTimerHandlerPtr(&menu->timer);
	WMDeleteTimerHandlerPtr(&d_data.magic);

	((WMenu *) desc->parent)->flags.inside_handler = 0;
#ifdef VIRTUAL_DESKTOP
	if (w_global.config->vdesk_enable)
		wWorkspaceRaiseEdge(scr);
#endif
}

void wMenuMove(WMenu * menu, int x, int y, int submenus)
{
	WMenu *submenu;
	int i;

	if (!menu)
		return;

	menu->frame_x = x;
	menu->frame_y = y;
	xcb_configure_window(wdisp->conn, menu->frame->core->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ x, y });

	if (submenus > 0 && menu->selected_entry >= 0) {
		i = menu->entries[menu->selected_entry]->cascade;

		if (i >= 0 && menu->cascades) {
			submenu = menu->cascades[i];
			if (submenu->flags.mapped && !submenu->flags.buttoned) {
				if (w_global.config->align_menus) {
					wMenuMove(submenu, x + MENUW(menu), y, submenus);
				} else {
					wMenuMove(submenu, x + MENUW(menu),
						  y + submenu->entry_height * menuVisibleSlot(menu, menu->selected_entry), submenus);
				}
			}
		}
	}
	if (submenus < 0 && menu->parent != NULL && menu->parent->flags.mapped && !menu->parent->flags.buttoned) {
		if (w_global.config->align_menus) {
			wMenuMove(menu->parent, x - MENUW(menu->parent), y, submenus);
		} else {
			wMenuMove(menu->parent, x - MENUW(menu->parent), menu->frame_y
				  - menu->parent->entry_height * menuVisibleSlot(menu->parent, menu->parent->selected_entry), submenus);
		}
	}
}

static void changeMenuLevels(WMenu * menu, int lower)
{
	int i;

	if (!lower) {
		ChangeStackingLevel(menu->frame->core, (!menu->parent ? WMMainMenuLevel : WMSubmenuLevel));
		wRaiseFrame(menu->frame->core);
		menu->flags.lowered = 0;
	} else {
		ChangeStackingLevel(menu->frame->core, WMNormalLevel);
		wLowerFrame(menu->frame->core);
		menu->flags.lowered = 1;
	}
	for (i = 0; i < menu->cascade_no; i++) {
		if (menu->cascades[i]
		    && !menu->cascades[i]->flags.buttoned && menu->cascades[i]->flags.lowered != lower) {
			changeMenuLevels(menu->cascades[i], lower);
		}
	}
}

static void menuTitleDoubleClick(WCoreWindow * sender, void *data, xcb_generic_event_t * event)
{
	WMenu *menu = data;
	int lower;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) sender;

	if (((xcb_button_press_event_t *)event)->state & MOD_MASK) {
		if (menu->flags.lowered) {
			lower = 0;
		} else {
			lower = 1;
		}
		changeMenuLevels(menu, lower);
	} else {
		if (menu->flags.shaded) {
			wFrameWindowResize(menu->frame, menu->frame->core->width, menu->frame->top_width +
					   menu->entry_height * menuVisibleCount(menu) + menu->frame->bottom_width - 1);
			menu->flags.shaded = 0;
		} else {
			wFrameWindowResize(menu->frame, menu->frame->core->width, menu->frame->top_width - 1);
			menu->flags.shaded = 1;
		}
	}
}

static void menuTitleMouseDown(WCoreWindow * sender, void *data, xcb_generic_event_t * event)
{
	WMenu *menu = data;
	WMenu *tmp;
	xcb_generic_event_t *ev = NULL;
	int x = menu->frame_x, y = menu->frame_y;
	int dx = ((xcb_button_press_event_t *)event)->root_x, dy = ((xcb_button_press_event_t *)event)->root_y;
	int i, lower;
	bool started;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) sender;

	/* can't touch the menu copy */
	if (menu->flags.brother)
		return;

	if (((xcb_button_press_event_t *)event)->detail != XCB_BUTTON_INDEX_1 && ((xcb_button_press_event_t *)event)->detail != XCB_BUTTON_INDEX_2)
		return;

	if (((xcb_button_press_event_t *)event)->state & MOD_MASK) {
		wLowerFrame(menu->frame->core);
		lower = 1;
	} else {
		wRaiseFrame(menu->frame->core);
		lower = 0;
	}
	tmp = menu;

	/* lower/raise all submenus */
	while (1) {
		if (tmp->selected_entry >= 0 && tmp->cascades && tmp->entries[tmp->selected_entry]->cascade >= 0) {
			tmp = tmp->cascades[tmp->entries[tmp->selected_entry]->cascade];
			if (!tmp || !tmp->flags.mapped)
				break;
			if (lower)
				wLowerFrame(tmp->frame->core);
			else
				wRaiseFrame(tmp->frame->core);
		} else {
			break;
		}
	}

	/* tear off the menu if it's a root menu or a cascade
	   application menu */
	if (!menu->flags.buttoned && !menu->flags.brother && (!menu->flags.app_menu || menu->parent != NULL)) {
		menu->flags.buttoned = 1;
		wFrameWindowShowButton(menu->frame, WFF_CLOSE_BUTTON);
		if (menu->parent) {
			/* turn off selected menu entry in parent menu */
			selectEntry(menu->parent, -1);

			/* make parent map the copy in place of the original */
			for (i = 0; i < menu->parent->cascade_no; i++) {
				if (menu->parent->cascades[i] == menu) {
					menu->parent->cascades[i] = menu->brother;
					break;
				}
			}
		}
	}

	started = false;
	while (1) {
		free(ev);
		ev = NULL;
		WMMaskEvent(wdisp->conn, XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_EXPOSURE, &ev);
		if (!ev)
			break;
		switch ((ev->response_type & 0x7f)) {
		case XCB_MOTION_NOTIFY:
			if (started) {
				x += (((xcb_motion_notify_event_t *)ev)->root_x) - dx;
				y += (((xcb_motion_notify_event_t *)ev)->root_y) - dy;
				dx = (((xcb_motion_notify_event_t *)ev)->root_x);
				dy = (((xcb_motion_notify_event_t *)ev)->root_y);
				wMenuMove(menu, x, y, true);
			} else {
				if (abs((((xcb_motion_notify_event_t *)ev)->root_x) - dx) > MOVE_THRESHOLD
				    || abs((((xcb_motion_notify_event_t *)ev)->root_y) - dy) > MOVE_THRESHOLD) {
					started = true;
					wXGrabPointer(wdisp->conn, menu->frame->titlebar->window, false,
						     XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_BUTTON_RELEASE
						     | XCB_EVENT_MASK_BUTTON_PRESS,
						     XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE,
						     wdisp->cursor[WCUR_MOVE], XCB_CURRENT_TIME);
				}
			}
			break;

		case XCB_BUTTON_PRESS:
			break;

		case XCB_BUTTON_RELEASE:
			if ((((xcb_button_press_event_t *)ev)->detail) != ((xcb_button_press_event_t *)event)->detail)
				break;
			wXUngrabPointer(wdisp->conn, XCB_CURRENT_TIME);
			free(ev);
			return;

		default:
			WMHandleEvent(ev);
			break;
		}
	}
}

/*
 *----------------------------------------------------------------------
 * menuCloseClick--
 * 	Handles mouse click on the close button of menus. The menu is
 * closed when the button is clicked.
 *
 * Side effects:
 * 	The closed menu is reinserted at its parent menus
 * cascade list.
 *----------------------------------------------------------------------
 */
static void menuCloseClick(WCoreWindow * sender, void *data, xcb_generic_event_t * event)
{
	WMenu *menu = (WMenu *) data;
	WMenu *parent = menu->parent;
	int i;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) sender;
	(void) event;

	if (parent) {
		for (i = 0; i < parent->cascade_no; i++) {
			/* find the entry that points to the copy */
			if (parent->cascades[i] == menu->brother) {
				/* make it point to the original */
				parent->cascades[i] = menu;
				menu->parent = parent;
				break;
			}
		}
	}
	wMenuUnmap(menu);
}

static void saveMenuInfo(WMPropList * dict, WMenu * menu, WMPropList * key)
{
	WMPropList *value, *list;
	char buffer[256];

	snprintf(buffer, sizeof(buffer), "%i,%i", menu->frame_x, menu->frame_y);
	value = WMCreatePLString(buffer);
	list = WMCreatePLArray(value, NULL);
	if (menu->flags.lowered)
		WMAddToPLArray(list, WMCreatePLString("lowered"));
	WMPutInPLDictionary(dict, key, list);
	WMReleasePropList(value);
	WMReleasePropList(list);
}

void wMenuSaveState(WScreen * scr)
{
	WMPropList *menus, *key;
	int save_menus = 0;

	menus = WMCreatePLDictionary(NULL, NULL);

	if (scr->switch_menu && scr->switch_menu->flags.buttoned) {
		key = WMCreatePLString("SwitchMenu");
		saveMenuInfo(menus, scr->switch_menu, key);
		WMReleasePropList(key);
		save_menus = 1;
	}

	if (saveMenuRecurs(menus, scr, scr->root_menu))
		save_menus = 1;

	if (scr->workspace_menu && scr->workspace_menu->flags.buttoned) {
		key = WMCreatePLString("WorkspaceMenu");
		saveMenuInfo(menus, scr->workspace_menu, key);
		WMReleasePropList(key);
		save_menus = 1;
	}

	if (save_menus) {
		key = WMCreatePLString("Menus");
		WMPutInPLDictionary(scr->session_state, key, menus);
		WMReleasePropList(key);
	}
	WMReleasePropList(menus);
}

static bool getMenuPath(WMenu * menu, char *buffer, int bufSize)
{
	bool ok;
	int len = 0;

	if (!menu->flags.titled || !menu->frame->title[0])
		return false;

	len = strlen(menu->frame->title);
	if (len >= bufSize)
		return false;

	if (menu->parent) {
		ok = getMenuPath(menu->parent, buffer, bufSize - len - 1);
		if (!ok)
			return false;
	}

	if (wstrlcat(buffer, "\\", bufSize) >= (size_t)bufSize ||
	    wstrlcat(buffer, menu->frame->title, bufSize) >= (size_t)bufSize)
		return false;

	return true;
}

static bool saveMenuRecurs(WMPropList * menus, WScreen * scr, WMenu * menu)
{
	WMPropList *key;
	int save_menus = 0, i;
	char buffer[512];
	bool ok = true;

	if (menu->flags.brother)
		menu = menu->brother;

	if (menu->flags.buttoned && menu != scr->switch_menu) {

		buffer[0] = '\0';
		ok = getMenuPath(menu, buffer, 510);

		if (ok) {
			key = WMCreatePLString(buffer);
			saveMenuInfo(menus, menu, key);
			WMReleasePropList(key);
			save_menus = 1;
		}
	}

	if (ok) {
		for (i = 0; i < menu->cascade_no; i++) {
			if (saveMenuRecurs(menus, scr, menu->cascades[i]))
				save_menus = 1;
		}
	}
	return save_menus;
}

#define COMPLAIN(key) wwarning(_("bad value in menus state info: %s"), key)

static bool getMenuInfo(WMPropList * info, int *x, int *y, bool * lowered)
{
	WMPropList *pos;

	*lowered = false;

	if (WMIsPLArray(info)) {
		WMPropList *flags;
		pos = WMGetFromPLArray(info, 0);
		flags = WMGetFromPLArray(info, 1);
		if (flags != NULL && WMIsPLString(flags) && WMGetFromPLString(flags) != NULL
		    && strcmp(WMGetFromPLString(flags), "lowered") == 0) {
			*lowered = true;
		}
	} else {
		pos = info;
	}

	if (pos != NULL && WMIsPLString(pos)) {
		if (sscanf(WMGetFromPLString(pos), "%i,%i", x, y) != 2)
			COMPLAIN("Position");
	} else {
		COMPLAIN("(position, flags...)");
		return false;
	}

	return true;
}

static int restoreMenu(WScreen *scr, WMPropList *menu)
{
	int x, y;
	bool lowered = false;
	WMenu *pmenu = NULL;

	if (!menu)
		return false;

	if (!getMenuInfo(menu, &x, &y, &lowered))
		return false;

	OpenSwitchMenu(scr, x, y, false);
	pmenu = scr->switch_menu;

	if (pmenu) {
		int width = MENUW(pmenu);
		int height = MENUH(pmenu);
		WMRect rect = wGetRectForHead(scr, wGetHeadForPointerLocation(scr));

		if (lowered) {
			changeMenuLevels(pmenu, true);
		}

		if (x < rect.pos.x - width)
			x = rect.pos.x;
		if (x > rect.pos.x + rect.size.width)
			x = rect.pos.x + rect.size.width - width;
		if (y < rect.pos.y)
			y = rect.pos.y;
		if (y > rect.pos.y + rect.size.height)
			y = rect.pos.y + rect.size.height - height;

		wMenuMove(pmenu, x, y, true);
		pmenu->flags.buttoned = 1;
		wFrameWindowShowButton(pmenu->frame, WFF_CLOSE_BUTTON);
		return true;
	}
	return false;
}

static int restoreMenuRecurs(WScreen *scr, WMPropList *menus, WMenu *menu, const char *path)
{
	WMPropList *key, *entry;
	char buffer[1024];
	int i, x, y, res;
	bool lowered;

	if (strlen(path) + strlen(menu->frame->title) > 510)
		return false;

	snprintf(buffer, sizeof(buffer), "%s\\%s", path, menu->frame->title);
	key = WMCreatePLString(buffer);
	entry = WMGetFromPLDictionary(menus, key);
	res = false;

	if (entry && getMenuInfo(entry, &x, &y, &lowered)) {

		if (!menu->flags.mapped) {
			int width = MENUW(menu);
			int height = MENUH(menu);
			WMRect rect = wGetRectForHead(scr, wGetHeadForPointerLocation(scr));

			wMenuMapAt(menu, x, y, false);

			if (menu->parent) {
				/* make parent map the copy in place of the original */
				for (i = 0; i < menu->parent->cascade_no; i++) {
					if (menu->parent->cascades[i] == menu) {
						menu->parent->cascades[i] = menu->brother;
						break;
					}
				}
			}
			if (lowered) {
				changeMenuLevels(menu, true);
			}

			if (x < rect.pos.x - width)
				x = rect.pos.x;
			if (x > rect.pos.x + rect.size.width)
				x = rect.pos.x + rect.size.width - width;
			if (y < rect.pos.y)
				y = rect.pos.y;
			if (y > rect.pos.y + rect.size.height)
				y = rect.pos.y + rect.size.height - height;

			wMenuMove(menu, x, y, true);
			menu->flags.buttoned = 1;
			wFrameWindowShowButton(menu->frame, WFF_CLOSE_BUTTON);
			res = true;
		}
	}

	WMReleasePropList(key);

	for (i = 0; i < menu->cascade_no; i++) {
		if (restoreMenuRecurs(scr, menus, menu->cascades[i], buffer) != false)
			res = true;
	}

	return res;
}

void wMenuRestoreState(WScreen * scr)
{
	WMPropList *menus, *menu, *key, *skey;

	if (!scr->session_state) {
		return;
	}

	key = WMCreatePLString("Menus");
	menus = WMGetFromPLDictionary(scr->session_state, key);
	WMReleasePropList(key);

	if (!menus)
		return;

	/* restore menus */

	skey = WMCreatePLString("SwitchMenu");
	menu = WMGetFromPLDictionary(menus, skey);
	WMReleasePropList(skey);
	restoreMenu(scr, menu);

	if (!scr->root_menu) {
		OpenRootMenu(scr, scr->scr_width * 2, 0, false);
		wMenuUnmap(scr->root_menu);
	}
	restoreMenuRecurs(scr, menus, scr->root_menu, "");
}
