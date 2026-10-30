/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2010-2026 Window Maker Team
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

#ifndef WMFRAMEWINDOW_H_
#define WMFRAMEWINDOW_H_

#include <stdint.h>

#include "wcore.h"
#include "pixmap.h"

#define BORDER_TOP	1
#define BORDER_BOTTOM	2
#define BORDER_LEFT	4
#define BORDER_RIGHT	8
#define BORDER_ALL	(1|2|4|8)


#define WFF_TITLEBAR		(1<<0)
#define WFF_MINIATURIZE_BUTTON	(1<<1)
#define WFF_CLOSE_BUTTON	(1<<2)
#define WFF_RESIZEBAR		(1<<3)
#define WFF_BORDER		(1<<4)
#define WFF_SINGLE_STATE	(1<<5)
#ifdef XKB_BUTTON_HINT
#define WFF_LANGUAGE_BUTTON	(1<<6)
#endif
#define WFF_SELECTED		(1<<7)
#define WFF_MAXIMIZE_BUTTON	(1<<8)
#define WFF_ICON_SLOT		(1<<9)


#define WFF_IS_SHADED	(1<<16)
#define WFF_IS_MENU	(1<<17)
#define WFF_IS_CLIENT_WINDOW_FRAME	(1<<18)

extern const int wtb_role_flag_shift[WTB_ROLE_COUNT];
#define WFF_ROLE_BIT(r)		(1 << wtb_role_flag_shift[(r)])

typedef struct WFrameWindow {
    WScreen *screen_ptr;	       /* pointer to the screen structure */

    WCoreWindow *core;

    WCoreWindow *titlebar;	       /* the titlebar */
    WCoreWindow *buttons[WTB_ROLE_COUNT];

    short workspace;		       /* primary workspace, always a member of ws_mask */
    uint64_t ws_mask;		       /* bit N set = window is on workspace N */

    short top_width;
    short bottom_width;

    short resizebar_corner_width;

    WCoreWindow *resizebar;	       /* bottom resizebar */
    xcb_window_t resizebar_cursor[3];	       /* InputOnly per-region hover cursors */

    xcb_pixmap_t title_back[4];	       /* focused, unfocused, pfocused, tab-inactive */
    xcb_pixmap_t resizebar_back[3];	       /* any, None, None */
    xcb_pixmap_t button_back[WTB_ROLE_COUNT][4];
    /* cache entries the Pixmaps above are borrowed from, the Pixmaps are
     * not owned by this frame and must be released through the cache,
     * never via XFreePixmap directly */
    struct WFrameTexCache *title_cache[4];
    struct WFrameTexCache *resizebar_cache;

    WPixmap *button_image[WTB_ROLE_COUNT];
#ifdef XKB_BUTTON_HINT
    WPixmap *languagebutton_image[2];    /* focused, unfocused */
#endif

    /* Icon role draws on the titlebar surface instead of owning a
     * button core, the scaled pixmap and its 1-bit clip mask are
     * cached here and refreshed by wFrameWindowSetIcon() */
    xcb_pixmap_t titlebar_icon;
    xcb_pixmap_t titlebar_icon_mask;
    short titlebar_icon_size;

    union WTexture **title_texture;
    union WTexture **resizebar_texture;
    WMColor **title_color;
    WMFont **font;

    char *title;		       /* window name (title) */

    /* Tab-group segmented titlebar. tab_seg_x holds tab_seg_count + 1
     * x-edges in titlebar-core coordinates, so segment i covers
     * [tab_seg_x[i], tab_seg_x[i+1]). Filled by wFrameWindowPaint()
     * when this frame's owner is the active member of a tab group and
     * TabsInTitlebar is on, cleared to tab_seg_count = 0 otherwise so
     * the hit-test helper returns -1. */
    int *tab_seg_x;
    int tab_seg_count;

#ifdef KEEP_XKB_LOCK_STATUS
    int languagemode;
    int last_languagemode;
    char language_label[3];        /* 2-letter language code */
#endif /* KEEP_XKB_LOCK_STATUS */

    /* thing that uses this frame. passed as data to callbacks */
    void *child;

    /* callbacks */
    void (*on_click_button[WTB_ROLE_COUNT])(WCoreWindow *sender, void *data, xcb_generic_event_t *event);
    void (*on_dblclick_button[WTB_ROLE_COUNT])(WCoreWindow *sender, void *data, xcb_generic_event_t *event);

    void (*on_mousedown_titlebar)(WCoreWindow *sender, void *data, xcb_generic_event_t *event);
    void (*on_dblclick_titlebar)(WCoreWindow *sender, void *data, xcb_generic_event_t *event);

    void (*on_mousedown_resizebar)(WCoreWindow *sender, void *data, xcb_generic_event_t *event);

    struct {
        unsigned int state:2;	       /* 3 possible states */
        unsigned int justification:2;
        unsigned int titlebar:1;
        unsigned int resizebar:1;

        /* per-role bits, indexed by WTitlebarRole */
        unsigned int has_button:WTB_ROLE_COUNT;
        unsigned int hide_button:WTB_ROLE_COUNT;
        unsigned int button_dont_fit:WTB_ROLE_COUNT;

        unsigned int need_texture_remake:1;

        unsigned int single_texture:1;

	/* urgent-blink phase: 1 shows the "focused" look, 0 the frame's
	 * own state, see frameEffectiveState() */
	unsigned int urgent_blink:1;

        unsigned int need_texture_change:1;

        unsigned int repaint_only_titlebar:1;
        unsigned int repaint_only_resizebar:1;

	unsigned int is_client_window_frame:1;
	unsigned int border:1;
        /* selects the menu_title_* triple from w_global.config instead of
         * window_title_*, replacing the three int * fields that pointed
         * into the swap-and-destroy WConfig and went stale on reload */
        unsigned int is_menu:1;

        unsigned int incomplete_title:1;
    } flags;
    int depth;
    xcb_visualtype_t *visual;
    xcb_colormap_t colormap;
	xcb_gcontext_t inner_border_gc;
    unsigned long *border_pixel;
    unsigned long *focused_border_pixel;
    unsigned long *selected_border_pixel;
	unsigned long *inner_border_pixel;
	unsigned long *inner_focused_border_pixel;
} WFrameWindow;


WFrameWindow*
wFrameWindowCreate(WScreen *scr, int wlevel, int x, int y,
                   int width, int height, int flags,
                   union WTexture **title_texture,
                   union WTexture **resize_texture,
                   WMColor **color, WMFont **font,
                   int depth, xcb_visualtype_t *visual, xcb_colormap_t colormap);

void wFrameWindowUpdateBorders(WFrameWindow *fwin, int flags);

void wFrameWindowDestroy(WFrameWindow *fwin);

void wFrameWindowChangeState(WFrameWindow *fwin, int state);

/* arm or disarm the urgent-blink appearance; repaints immediately so the
 * window lands on the "focused" look the moment urgency arrives */
void wFrameWindowUrgentBlink(WFrameWindow *fwin, int on);
/* one blink tick: flip the phase and repaint titlebar and buttons */
void wFrameWindowBlinkTick(WFrameWindow *fwin);

void wFrameWindowPaint(WFrameWindow *fwin);

void wFrameWindowConfigure(WFrameWindow *fwin, int x, int y, int width, int height);

void wFrameWindowResize(WFrameWindow *fwin, int width, int height);

void wFrameWindowShowButton(WFrameWindow *fwin, int flags);

void wFrameWindowHideButton(WFrameWindow *fwin, int flags);

int wFrameWindowChangeTitle(WFrameWindow *fwin, const char *new_title);

void wFrameWindowRepaintTitle(WFrameWindow *fwin);

int wFrameWindowTabSegmentAt(WFrameWindow *fwin, int x);

void wFrameWindowSetIcon(WFrameWindow *fwin, struct RImage *image);

#ifdef XKB_BUTTON_HINT
void wFrameWindowUpdateLanguageButton(WFrameWindow *fwin);
#endif

#endif
