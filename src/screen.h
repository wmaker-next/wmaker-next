/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
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

#ifndef WMSCREEN_H_
#define WMSCREEN_H_

#include "wconfig.h"
#include "WindowMaker.h"
#include <sys/types.h>

#include <WINGs/WUtil.h>

#define PRINT_SCREEN 1
#define PRINT_WINDOW 2
#define PRINT_PARTIAL 3

/* Shared upper bound on Xinerama/RandR heads, see xinerama.h. Kept here
 * because WScreen embeds fixed per-head arrays and screen.h must not
 * include xinerama.h. */
#ifndef WM_MAX_HEADS
#define WM_MAX_HEADS 32
#endif

typedef struct {
    WMRect *screens;
    int count;                 /* screen count, 0 = inactive */
    int primary_head;	       /* main working screen */
} WXineramaInfo;



/* an area of the screen reserved by some window */
typedef struct WReservedArea {
    WArea area;                        /* left, right, top, bottom thickness from the root edges */
    int left_start, left_end;          /* y span of the left strut, root relative and inclusive */
    int right_start, right_end;        /* y span of the right strut, root relative and inclusive */
    int top_start, top_end;            /* x span of the top strut, root relative and inclusive */
    int bottom_start, bottom_end;      /* x span of the bottom strut, root relative and inclusive */
    xcb_window_t window;
    struct WReservedArea *next;
} WReservedArea;


typedef struct WAppIconChain {
    struct WAppIcon *aicon;
    struct WAppIconChain *next;
} WAppIconChain;


/* Drawers, which are docks, really */
typedef struct WDrawerChain {
    struct WDock *adrawer;
    struct WDrawerChain *next;
} WDrawerChain;

typedef enum {
    MARK_CAPTURE_IDLE  = 0,
    MARK_CAPTURE_SET   = 1,
    MARK_CAPTURE_BRING = 2,
    MARK_CAPTURE_JUMP  = 3,
    MARK_CAPTURE_SWAP  = 4
} WMarkCaptureMode;

/*
 * each WScreen is saved into a context associated with its root window
 */
typedef struct _WScreen {
	struct WDisplay *display;
    int	screen;			       /* screen number */
    xcb_window_t info_window;		       /* for our window manager info stuff */
#ifdef USE_ICCCM_WMREPLACE
    xcb_atom_t sn_atom;		       /* window manager selection */
#endif

    int scr_width;		       /* size of the screen */
    int scr_height;

    /* WMScreenUIScale(wmscreen) cached at wScreenInit() so wm-side
     * device-px layout can convert logical WMFontHeight() results
     * without a per-read accessor call. */
    double ui_scale;

    xcb_window_t root_win;		       /* root window of screen */
    int  depth;			       /* depth of the default visual */
    xcb_colormap_t colormap;		       /* root colormap */
    int root_colormap_install_count;
    struct WWindow *original_cmap_window; /* colormap before installing
                                           * root colormap temporarily */
    struct WWindow *cmap_window;
    xcb_colormap_t current_colormap;

    xcb_window_t w_win;		       /* window to use as drawable
                                        * for new GCs, pixmaps etc. */
    xcb_visualtype_t *w_visual;
    int  w_depth;
    xcb_colormap_t w_colormap;	       /* our colormap */

    WXineramaInfo xine_info;

    xcb_window_t no_focus_win;	       /* window to get focus when nobody
                                        * else can do it */

    struct WWindow *focused_window;    /* window that has the focus
                                        * Use this list if you want to
                                        * traverse the entire window list
                                        */
    struct WWindow *bfs_focused_window; /* window that had focus before
                                        * another window entered fullscreen
                                        */

    WMArray *selected_windows;

    WMArray *fakeGroupLeaders;         /* list of fake window group ids */

    struct WAppIcon *app_icon_list;    /* list of all app-icons on screen */

    struct WApplication *wapp_list;    /* list of all applications */

    struct WWindowGroup *window_groups; /* linked list of tab groups on this screen */
    /* wGroupPickAndAttach() latches the window being attached here so
     * wUnmanageWindow() can abort the pick cleanly if its client exits
     * while the pointer-pick loop is waiting on the user */
    struct WWindow *tab_pick_payload;
    /* wGroupDragTab() latches the payload being dragged and the visible
     * source frame here so wUnmanageWindow() can abort the drag cleanly
     * if either client exits while the modal loop is running */
    struct WWindow *tab_drag_payload;
    struct WWindow *tab_drag_source;

    WMBag *stacking_list;	       /* bag of lists of windows
                                        * in stacking order.
                                        * Indexed by window level
                                        * and each list on the array
                                        * is ordered from the topmost to
                                        * the lowest window
                                        */

    int window_count;		       /* number of windows in window_list */

    unsigned int last_creation_id;   /* last WWindow.creation_id handed out */

    int workspace_count;	       /* number of workspaces */

    struct WWorkspace **workspaces;    /* workspace array */

    int current_workspace;	       /* current workspace number */
    int last_workspace;		       /* last used workspace number */

#ifdef VIRTUAL_DESKTOP
    /* Edge InputOnly windows for viewport edge-scroll panning. Created
     * and destroyed by wWorkspaceUpdateEdge(). The wVEdgeTable hash
     * maps each edge window to its direction handler. */
    int virtual_nr_edges;
    xcb_window_t *virtual_edges;
#endif

    /* Per-head independent workspace state. With PerHeadWorkspaces off
     * these stay zeroed and current_workspace is the only source of
     * truth. With it on, current_workspace becomes a compatibility
     * mirror of head_workspace[last_active_head] for legacy UI/EWMH
     * paths, while visibility decisions use head_workspace[] directly. */
    int head_workspace[WM_MAX_HEADS];      /* active workspace per head */
    int last_head_workspace[WM_MAX_HEADS]; /* previous active workspace per head */
    int last_active_head;                  /* head of last focused/pointer event */
    char *head_output_name[WM_MAX_HEADS];  /* RandR output name per active head, or NULL */

    xcb_timestamp_t net_last_user_time;	       /* newest _NET_WM_USER_TIME observed,
					* used to deny focus to windows that
					* map with an older user-activity
					* timestamp */

    struct WStartupNotify *sn;	       /* startup-notification monitor and
					* pending-launch table, opaque,
					* defined in startup_notify.c */

    /* _NET_DESKTOP_LAYOUT as set by a pager on the root window, used by
     * the directional WorkspaceLeft/Right/Up/Down keybinds. With no pager
     * the default is one row of workspace_count columns, so Left/Right
     * behave like Prev/Next and Up/Down are no-ops. */
    struct {
	int orientation;	       /* 0 = horizontal, 1 = vertical */
	int cols;
	int rows;
	int corner;		       /* 0 TL, 1 TR, 2 BR, 3 BL */
    } net_desktop_layout;


    WReservedArea *reservedAreas;      /* used to build totalUsableArea */

    WArea *usableArea;		       /* area of the workspace where
                                        * we can put windows on, as defined
                                        * by other clients (not us) */
    WArea *totalUsableArea;	       /* same as above, but including
                                        * the dock and other stuff */

    WMColor *black;
    WMColor *white;
    WMColor *gray;
    WMColor *darkGray;

    /* shortcuts for the pixels of the above colors. just for convenience */
    WMPixel black_pixel;
    WMPixel white_pixel;
    WMPixel light_pixel;
    WMPixel dark_pixel;

    xcb_pixmap_t transp_stipple;	       /* for making holes in icon masks for
                                        * transparent icon simulation */
    WMFont *title_font;		       /* default font for the titlebars */
    WMFont *menu_title_font;	       /* font for menu titlebars */
    WMFont *menu_entry_font;	       /* font for menu items */
    WMFont *icon_title_font;	       /* for icon titles */
    WMFont *clip_title_font;	       /* for clip titles */
    WMFont *info_text_font;	       /* text on things like geometry
                                        * hint boxes */

    WMFont *workspace_name_font;

	/* AntialiasedText (WMGLOBAL) value the screen's fonts were last opened
	 * under; wConfigApplyAntialiasedText() compares it against the current
	 * WINGsConfiguration.antialiasedText to detect a live change. */
	bool antialiased_text_applied;

    WMColor *select_color;
    WMColor *select_text_color;
    /* foreground colors */
    WMColor *window_title_color[4];    /* window titlebar text (foc, unfoc, pfoc, tab-inactive)*/
    WMColor *menu_title_color[3];      /* menu titlebar text */
    WMColor *clip_title_color[2];      /* clip title text */
    WMColor *mtext_color;	       /* menu item text */
    WMColor *dtext_color;	       /* disabled menu item text */

    /* device-pixel decoration sizes, the config value or compile-time
     * constant multiplied by WMScreenUIScale() so a HiDPI display gets
     * a proportionally taller resizebar and thicker frame */
    int frame_border_width;
    int resizebar_height;
    int resizebar_corner_width;
    WMColor *frame_border_color;
    WMColor *frame_focused_border_color;
    WMColor *frame_selected_border_color;
	int frame_inner_border_width;
	unsigned int frame_inner_border_sides;
	WMColor *frame_inner_border_color;
	WMColor *frame_inner_focused_border_color;

    WMPixel frame_border_pixel;	       /* frame border */
    WMPixel frame_focused_border_pixel;	       /* frame border */
    WMPixel frame_selected_border_pixel;/* frame border */
	WMPixel frame_inner_border_pixel;
	WMPixel frame_inner_focused_border_pixel;


    union WTexture *menu_title_texture[3];/* menu titlebar texture (tex, -, -) */
    union WTexture *window_title_texture[4];  /* win textures (foc, unfoc, pfoc, tab-inactive) */
    union WTexture *resizebar_texture[3];/* window resizebar texture (tex, -, -) */

    union WTexture *menu_item_texture; /* menu item texture */

    struct WTexSolid *menu_item_auxtexture; /* additional texture to draw menu
    * cascade arrows */
    struct WTexSolid *icon_title_texture;/* icon titles */

    struct WTexSolid *widget_texture;

    struct WTexSolid *icon_back_texture; /* icon back color for shadowing */


    WMColor *icon_title_color;	       /* icon title color */

    struct WPixmap *b_pixmaps[PRED_BPIXMAPS]; /* internal pixmaps for buttons*/
    WTitlebarLayout titlebar_layout;          /* parsed TitlebarLeft/Right */
    struct WPixmap *menu_radio_indicator;/* left menu indicator */
    struct WPixmap *menu_check_indicator;/* left menu indicator for checkmark */
    struct WPixmap *menu_mini_indicator;   /* for miniwindow */
    struct WPixmap *menu_hide_indicator;   /* for hidden window */
    struct WPixmap *menu_shade_indicator;  /* for shaded window */
    struct WPixmap *menu_snap_vertical_indicator;  /* for vertical snap window */
    struct WPixmap *menu_snap_horizontal_indicator;  /* for horizontal snap window */
    struct WPixmap *menu_snap_rh_indicator;  /* for righ half snap window */
    struct WPixmap *menu_snap_lh_indicator;  /* for left half snap window */
    struct WPixmap *menu_snap_th_indicator;  /* for top half snap window */
    struct WPixmap *menu_snap_bh_indicator;  /* for bottom half snap window */
    struct WPixmap *menu_snap_tl_indicator;  /* for top left snap window */
    struct WPixmap *menu_snap_tr_indicator;  /* for top rigt snap window */
    struct WPixmap *menu_snap_bl_indicator;  /* for bottom left snap window */
    struct WPixmap *menu_snap_br_indicator;  /* for bottom right snap window */
    struct WPixmap *menu_snap_tiled_indicator;  /* for tiled window */
    struct WPixmap *menu_central_indicator;  /* for central window */

    int app_menu_x, app_menu_y;	       /* position for application menus */

    struct WMenu *root_menu;	       /* root window menu */
    struct WMenu *switch_menu;	       /* window list menu */
    struct WMenu *workspace_menu;      /* workspace operation */
	struct WMenu *layout_menu;         /* layout chooser for workspace_menu */
    struct WMenu *window_menu;	       /* window command menu */
    struct WMenu *icon_menu;	       /* icon/appicon menu */
    struct WMenu *workspace_submenu;   /* workspace list for window_menu */

    struct WDock *dock;		       /* the application dock */
    struct WMenu *dock_pos_menu;       /* Dock position menu */
    struct WPixmap *dock_dots;	       /* 3 dots for the Dock */
    xcb_window_t dock_shadow;		       /* shadow for dock buttons */
    struct WAppIcon *clip_icon;        /* The clip main icon, or the dock's, if they are merged */
    struct WMenu *clip_menu;           /* Menu for clips */
    struct WMenu *clip_submenu;        /* Workspace list for clips */
    struct WMenu *clip_options;	       /* Options for Clip */
    struct WMenu *clip_ws_menu;	       /* workspace menu for clip */
    struct WMenu *drawer_menu;         /* Menu for drawers */
    struct WDock *last_dock;
    WAppIconChain *global_icons;       /* for omnipresent icons chain in clip */
    int global_icon_count;	       /* How many global icons do we have */
    WDrawerChain *drawers;             /* Chain of drawers */
    /* Cache the following two informations, as they are used quite a lot */
    int drawer_count;                  /* Nb of drawers that */
    struct WDock *attracting_drawer;   /* The drawer that auto-attracts icons, or NULL */

    struct RContext *rcontext;	       /* wrlib context */

    WMScreen *wmscreen;		       /* for widget library */

    struct RImage *icon_tile;
    struct RImage *clip_tile;          /* tile with arrows to change workspace */
    struct RImage *drawer_tile;        /* tile for a drawer (tile + arrow) */
    xcb_pixmap_t icon_tile_pixmap;		/* For app supplied icons */

    struct RImage *def_icon_rimage;	/* Default RImage icon */

	/* per-screen realised switch-panel/placement state */
	WCoord window_place_origin;        /* WindowPlaceOrigin clamped to this screen */
	struct RImage *swtileImage;
	struct RImage *swbackImage[9];
	union WTexture *wsmbackTexture;
	char *hot_corner_actions[4];       /* Action of each corner */

    struct WDialogData *dialog_data;

    struct W_OSDView *gview;           /* size/position view */
    struct W_OSDView *keychain_osd;    /* key-chain prefix popup */
    struct W_OSDView *workspace_name_osd;  /* workspace-switch name popup */

    struct WWireframe *wireframe;      /* move/resize outline window */
	struct WFindMouse *findmouse;

    /* state and other informations */
    short cascade_index;	       /* for cascade window placement */

    WMPropList *session_state;

    /* for double-click detection */
    xcb_timestamp_t last_click_time;
    xcb_window_t last_click_window;
    int last_click_button;

    /* balloon help data */
    struct _WBalloon *balloon;

    /* workspace name data */
    WMHandlerID workspace_name_timer;
    int workspace_name_fade_step;

    /* one-shot hide timer for the layout-name flash in keychain_osd */
    WMHandlerID layout_osd_timer;

    /* mini screenshot data */
    xcb_window_t mini_screenshot;
    time_t mini_screenshot_timeout;
    WMHandlerID mini_screenshot_timer;

    /* for raise-delay */
    WMHandlerID autoRaiseTimer;
    xcb_window_t autoRaiseWindow;	       /* window that is scheduled to be
                                        * raised */
    /* for hot-corners delay */
    WMHandlerID hot_corner_timer;
    xcb_window_t *hot_corner_win;            /* 4 per head, InputOnly, topmost */
    int hot_corner_win_count;

    int mapped_menu_count;             /* WMenu with flags.mapped set, gates
                                        * PointerMotionMask on the root window
                                        * for ScrollableMenus */

#ifdef USE_RANDR
    WMHandlerID randr_debounce_timer;
    void *randr_state;
#endif

    /* for window shortcuts */
    WMArray *shortcutWindows[MAX_WINDOW_SHORTCUTS];

#ifdef USE_DOCK_XDND
    char *xdestring;
    char *xdndpath;                    /* single decoded file:// path for drop-to-dock */
    unsigned int xdest_is_gnustep : 1; /* target dock icon is a GNUstep app */
#endif

    struct NetData *netdata;

	struct WXSettings *xsettings;      /* XSETTINGS manager state, NULL
					    * until wXSettingsInit() */

    int helper_fd;
    pid_t helper_pid;

#ifdef KEEP_XKB_LOCK_STATUS
	struct W_AccentPicker *accentpicker; /* accent picker state */
#endif

    int ipc_fd;                        /* listening AF_UNIX socket, -1 if disabled */
    char *ipc_path;                    /* bound path, unlinked at shutdown */
    WMHandlerID ipc_listen;            /* accept-ready input handler */
    struct WIPCClient *ipc_clients;    /* head of per-screen client list */

    struct {
        unsigned int startup:1;	       /* during window manager startup */
        unsigned int regenerate_icon_textures:1;
        unsigned int dnd_data_convertion_status:1;
        unsigned int root_menu_changed_shortcuts:1;
        unsigned int added_workspace_menu:1;
        unsigned int added_windows_menu:1;
        unsigned int startup2:1;       /* startup phase 2 */
        unsigned int next_click_is_not_double:1;
        unsigned int backimage_helper_launched:1;
        /* some client has issued a WM_COLORMAP_NOTIFY */
        unsigned int colormap_stuff_blocked:1;
        unsigned int doing_alt_tab:1;
        unsigned int jump_back_pending:1;
        unsigned int ignore_focus_events:1;
        unsigned int need_restack:1;   /* stacking list changed, X side
                                        * stale until CommitStacking() */
	unsigned int manage_burst:1;   /* inside manageAllWindows(), EWMH
					* client lists flush once at end */
	unsigned int client_lists_stale:1; /* client lists changed while
					    * manage_burst was set */
        unsigned int in_hot_corner:3;
        unsigned int mark_capture_mode:3;  /* one of WMarkCaptureMode */

    } flags;
} WScreen;


struct WConfig;
struct WConfigChanges;

WScreen *wScreenInit(int screen_number, const struct WConfig *config,
                     const struct WConfigChanges *changes);
void wScreenSaveState(WScreen *scr, bool preserve_session);
void wScreenRestoreState(WScreen *scr);

int wScreenBringInside(WScreen *scr, int *x, int *y, int width, int height);
int wScreenKeepInside(WScreen *scr, int *x, int *y, int width, int height);
void ScreenCapture(WScreen *scr, int mode);

/* in startup.c */
WScreen *wScreenWithNumber(int i);
WScreen *wScreenForRootWindow(xcb_window_t window);   /* window must be valid */
WScreen *wScreenForWindow(xcb_window_t window);   /* slower than above functions */

void wScreenDestroy(WScreen *scr);
void wScreenApplyIconSize(WScreen *scr);
void wScreenUpdateUsableArea(WScreen *scr);
void wScreenUpdateRootEventMask(WScreen *scr);

/* logical <-> device px for wm-side layout that mixes WMFontHeight()
 * (logical) with WCoreWindow geometry (device). */
#define WSCALE(scr, n)   ((int)((n) * (scr)->ui_scale + 0.5))
#define WUNSCALE(scr, n) ((int)((n) / (scr)->ui_scale + 0.5))

void create_logo_image(WScreen *scr);
#endif
