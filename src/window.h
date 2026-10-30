/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2009-2026 Window Maker Team
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

#ifndef WMWINDOW_H_
#define WMWINDOW_H_

#include <sys/time.h>

#include <wraster.h>
#include <WINGs/WXHints.h>

#include "wconfig.h"
#include "GNUstep.h"
#include "texture.h"
#include "menu.h"
#include "application.h"

/* not defined in my X11 headers */
#ifndef XCB_ICCCM_WM_HINT_X_URGENCY
#define XCB_ICCCM_WM_HINT_X_URGENCY (1L << 8)
#endif


#define BORDER_TOP	1
#define BORDER_BOTTOM	2
#define BORDER_LEFT	4
#define BORDER_RIGHT	8
#define BORDER_ALL	(1|2|4|8)


#define CLIENT_EVENTS (XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE\
    | XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_COLOR_MAP_CHANGE \
    | XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_VISIBILITY_CHANGE)

typedef enum {
    WFM_PASSIVE, WFM_NO_INPUT, WFM_LOCALLY_ACTIVE, WFM_GLOBALLY_ACTIVE
} FocusMode;


/*
 * window attribute flags.
 *
 * Note: attributes that should apply to the application as a
 * whole should only access the flags from app->main_window_desc->window_flags
 * This is to make sure that the application doesn't have many different
 * values for the same option. For example, imagine xfoo, which has
 * foo.bar as leader and it a child window named foo.baz. If you set
 * StartHidden YES for foo.bar and NO for foo.baz we must *always* guarantee
 * that the value that will be used will be that of the leader foo.bar.
 * The attributes inspector must always save application wide options
 * in the name of the leader window, not the child.
 *
 */
/*
 * All flags must have their default values = 0
 *
 * New flag scheme:
 *
 * user_flags, defined_flags
 * client_flags
 *
 * if defined window_flag then window_flag else client_flag
 *
 */

#define WFLAGP(wwin, FLAG)	((wwin)->defined_user_flags.FLAG \
    ? (wwin)->user_flags.FLAG \
    : (wwin)->client_flags.FLAG)

#define WSETUFLAG(wwin, FLAG, VAL)	(wwin)->user_flags.FLAG = (VAL),\
    (wwin)->defined_user_flags.FLAG = 1

/* Restore a user flag from the saved-state hint and tag it as
 * hint-carried, so the inspector does not persist it as a default.
 * The restored value is per-window, clear the domain tag so it
 * re-saves. */
#define WRESTOREHINTFLAG(wwin, FLAG, VAL)	WSETUFLAG(wwin, FLAG, VAL),\
	(wwin)->hint_flags.FLAG = 1, (wwin)->domain_flags.FLAG = 0

/* Set a per-window runtime override chosen from a menu or panel. The
 * value is user state, not domain-derived, so it rides the hint. */
#define WSETPERWINDOWFLAG(wwin, FLAG, VAL)	WSETUFLAG(wwin, FLAG, VAL),\
	(wwin)->domain_flags.FLAG = 0

typedef struct {
    /* OpenStep */
    unsigned int no_titlebar:1;	       /* draw titlebar? */
    unsigned int no_resizable:1;
    unsigned int no_closable:1;
    unsigned int no_miniaturizable:1;
    unsigned int no_border:1;	       /* 1 pixel border around window */
#ifdef XKB_BUTTON_HINT
    unsigned int no_language_button:1;
#endif
    unsigned int no_movable:1;

    /* decorations */
    unsigned int no_resizebar:1;       /* draw the bottom handle? */
    unsigned int no_close_button:1;    /* draw a close button? */
    unsigned int no_miniaturize_button:1; /* draw an iconify button? */

    unsigned int broken_close:1;       /* is the close button broken? */

    /* ours */
    unsigned int kill_close:1;	       /* can't send WM_DELETE_WINDOW */

    unsigned int no_shadeable:1;
    unsigned int omnipresent:1;
    unsigned int skip_window_list:1;
    unsigned int skip_switchpanel:1;
    unsigned int floating:1;	       /* put in WMFloatingLevel */
    unsigned int sunken:1;	       /* put in WMSunkenLevel */
    unsigned int no_bind_keys:1;       /* intercept wm kbd binds
                                        * while window is focused */
    unsigned int no_bind_mouse:1;      /* intercept mouse events
                                        * on client area while window
                                        * is focused */
    unsigned int no_hide_others:1;     /* hide window when doing hideothers */
    unsigned int auto_hide_on_unfocus:1;       /* hide when losing focus */
    unsigned int auto_miniaturize_on_unfocus:1; /* miniaturize when losing focus */
    unsigned int no_appicon:1;	       /* make app icon */

    unsigned int shared_appicon:1;

    unsigned int dont_move_off:1;

    unsigned int no_focusable:1;
    unsigned int focus_across_wksp:1;   /* let wmaker switch workspace to follow
					 * a focus request */

    unsigned int always_user_icon:1;   /* ignore client IconPixmap or
                                        * IconWindow */

    unsigned int start_miniaturized:1;
    unsigned int start_hidden:1;
    unsigned int start_maximized:1;
    unsigned int dont_save_session:1;  /* do not save app's state in session */

    unsigned int full_maximize:1;
    unsigned int swallow_child:1;
#ifdef VIRTUAL_DESKTOP
    /* Pan-sticky: window stays put when the viewport pans. Set from EWMH
     * window-type hints and toggleable per-window via WKBD_VDESK_PIN. */
    unsigned int virtual_stick:1;
#endif
    /*
     * emulate_app_icon must be automatically disabled for apps that can
     * generate their own appicons and for apps that have no_appicon=1
     */
    unsigned int emulate_appicon:1;
    /* Ignore any changes to the decoration elements (titlebar, etc) */
    unsigned int ignore_decoration_changes:1;
    /* refuse this window as either payload or target of a tab group */
    unsigned int no_tabbing:1;
    /* auto-join an existing tab group whose members share the same WM_CLASS */
    unsigned int auto_tab_group:1;
    /* exempt this window from the workspace tiling layout */
    unsigned int no_tiling:1;

} WWindowAttributes;



/*
 * Window manager protocols that both the client as we understand.
 */
typedef struct {
    unsigned int TAKE_FOCUS:1;
    unsigned int DELETE_WINDOW:1;
    unsigned int SAVE_YOURSELF:1;
    /* EWMH */
    unsigned int NET_WM_PING:1;
    unsigned int NET_WM_SYNC_REQUEST:1;
    /* WindowMaker specific */
    unsigned int MINIATURIZE_WINDOW:1;
    unsigned int HIDE_APP:1;
} WProtocols;


/*
 * Structure used for storing fake window group information
 */
typedef struct WFakeGroupLeader {
    char *identifier;
    xcb_window_t leader;
    xcb_window_t origLeader;
    int retainCount;
} WFakeGroupLeader;


/*
 * Tab-grouping: multiple windows share one on-screen slot, only the active
 * member's frame is visible at a time. Members share the same position.
 */
typedef struct WWindowGroup {
    struct WWindow **members;   /* heap array of WWindow* */
    int member_count;
    int capacity;
    int active_index;           /* index of the currently visible member */
    struct WWindowGroup *next;  /* linked list anchored at WScreen.window_groups */
} WWindowGroup;


typedef struct WGeometry {
	int x, y;
	unsigned int width, height;
} WGeometry;

typedef struct WFrameBorderOverride {
	int frame_border_width;
	int frame_inner_border_width;
	unsigned int frame_inner_border_sides;
	WMColor *frame_border_color;
	WMColor *frame_focused_border_color;
	WMColor *frame_selected_border_color;
	WMColor *frame_inner_border_color;
	WMColor *frame_inner_focused_border_color;
	unsigned int frame_border_width_set:1;
	unsigned int frame_border_color_set:1;
	unsigned int frame_focused_border_color_set:1;
	unsigned int frame_selected_border_color_set:1;
	unsigned int frame_inner_border_width_set:1;
	unsigned int frame_inner_border_sides_set:1;
	unsigned int frame_inner_border_color_set:1;
	unsigned int frame_inner_focused_border_color_set:1;
} WFrameBorderOverride;

/*
 * Stores client window information. Each client window has its
 * structure created when it's being first mapped.
 */
typedef struct WWindow {
	struct WWindow *prev;			/* window focus list */
	struct WWindow *next;

	WScreen *screen_ptr; 			/* pointer to the screen structure */
	WWindowAttributes user_flags;		/* window attribute flags set by user */
	WWindowAttributes defined_user_flags;	/* mask for user_flags */
	WWindowAttributes client_flags;		/* window attribute flags set by app
						 * initialized with global defaults */
	WWindowAttributes hint_flags;		/* user_flags bits restored from _WINDOWMAKER_STATE */
	WWindowAttributes domain_flags;		/* user_flags bits currently supplied by the
						 * domain fill, they never ride the hint */

	struct InspectorPanel *inspector;	/* pointer to attribute editor panel */

	struct WFrameWindow *frame;		/* the frame window */
	int frame_x, frame_y;			/* position of the frame in root*/

	WGeometry old_geometry;			/* (before things like maximize) */

	WGeometry bfs_geometry;			/* (before fullscreen) */
	int bfs_maximized;			/* flags.maximized at fullscreen entry */

	int maximus_x;				/* size after Maximusizing */
	int maximus_y;

	/* client window info */
	short old_border_width;			/* original border width of client_win*/
	xcb_window_t client_win;			/* the window we're managing */
	WObjDescriptor client_descriptor;	/* dummy descriptor for client */
	WMHandlerID urgent_blink_timer;	/* armed while the titlebar blinks
						 * its urgency phase */
	struct {
		int x, y;			/* position of *client* relative
						 * to root */
		unsigned int width, height;	/* size of the client window */
	} client;

	WXSizeHints *normal_hints;		/* WM_NORMAL_HINTS */
	WXWMHints *wm_hints;			/* WM_HINTS (optional) */
	char *wm_instance;			/* instance of WM_CLASS */
	char *wm_class;				/* class of WM_CLASS */
	char *wm_command;			/* WM_COMMAND argv joined with wtokenjoin */
	char *wm_role;				/* WM_WINDOW_ROLE (read once at manage time) */
	char *wm_machine;			/* WM_CLIENT_MACHINE (read once at manage time) */
	char *wm_title;			/* current window title, kept in sync by
						 * wWindowUpdateName() so the dynamic-rule
						 * lookup has a stable source without a frame */
	xcb_atom_t *wm_types;			/* full _NET_WM_WINDOW_TYPE atom list as
						 * advertised by the client (read once) */
	int wm_type_count;
	char **wm_type_names;		/* XGetAtomName() result per wm_types[],
						 * pre-resolved at manage time so the rule
						 * lookup never calls XGetAtomName() */
	GNUstepWMAttributes *wm_gnustep_attr;	/* GNUstep window attributes */

	int state;				/* state as in ICCCM */

	xcb_window_t transient_for;			/* WM_TRANSIENT_FOR */

	WFakeGroupLeader *fake_group;		/* Fake group leader for grouping into
						 * a single appicon */
	xcb_window_t group_id;			/* the leader window of the group */
	xcb_window_t client_leader;			/* WM_CLIENT_LEADER if not
						 * internal_window */

	xcb_window_t main_window;			/* main window for the application */
	xcb_window_t orig_main_window;		/* original main window of application.
						 * used for the shared appicon thing */

	xcb_colormap_t colormap;			/* client colormap, kept in sync
						 * by ColormapNotify */
	int cmap_window_no;
	xcb_window_t *cmap_windows;

	int last_head;				/* last reliable Xinerama head, used for
						 * per-head workspace visibility when the
						 * window is unmapped or off-screen */

	/* move/resize state for _NET_WM_MOVERESIZE support */
	struct {
		int active;			/* 1 if move/resize is in progress */
		int resize_edge;		/* which edge for resize (0-7) */
	} moveresize;

	/* protocols */
	WProtocols protocols;			/* accepted WM_PROTOCOLS */

	/* the owning client process for client_win, resolved once at
	 * manage time via wGetWindowPid() (XRes QueryClientIds with
	 * the property fallback) and read everywhere else, 0 when XRes
	 * and _NET_WM_PID are both absent or the client is remote */
	pid_t client_pid;

	/* _NET_WM_PING bookkeeping, armed when a close request is sent to a
	 * client that lists the protocol, disarmed by the echoed reply or by
	 * wWindowDestroy */
	struct {
		void *timer;			/* WMHandlerID, NULL when idle */
		xcb_timestamp_t timestamp;			/* cookie carried in data.l[1] */
		int pid;			/* _NET_WM_PID cached on manage,
						 * 0 when absent or remote */
		short term_sent;		/* SIGTERM already sent, next
						 * Force Quit escalates */
	} net_ping;

	/* _NET_WM_USER_TIME, used for focus-stealing prevention on map.
	 * net_user_time_win is the indirected property carrier when the
	 * client sets _NET_WM_USER_TIME_WINDOW, or None for the toplevel. */
	xcb_timestamp_t net_user_time;
	short net_user_time_set;
	xcb_window_t net_user_time_win;
	long net_user_time_win_mask;		/* our previous event mask on
						 * the helper, restored on
						 * detach so a hostile XID
						 * cannot make us drop a
						 * selection we held */
	int sn_workspace;			/* DESKTOP= from a matched
						 * startup-notification
						 * sequence, kept as int and
						 * range-checked at use because
						 * it comes from an untrusted
						 * root ClientMessage, -1 when
						 * unset */

#ifdef USE_XSYNC
	/* _NET_WM_SYNC_REQUEST: the client-created counter we watch via an
	 * alarm so opaque resizes are paced to the client's redraw */
	struct {
		unsigned long counter;		/* XSyncCounter, 0 = none */
		unsigned long alarm;		/* XSyncAlarm, 0 = none */
		uint32_t value_lo;
		uint32_t value_hi;
		struct timeval wait_start;	/* stamped by Send, deadline check */
		short waiting;			/* sent a request, no alarm yet */
		short disabled;			/* client missed a deadline */
	} net_sync;
#endif

	FocusMode focus_mode;			/* type of keyboard input focus */

	long event_mask;			/* the event mask thats selected */

	/* state flags */
	struct {
		unsigned int mapped:1;
		unsigned int focused:1;
		unsigned int miniaturized:1;
		unsigned int hidden:1;
		unsigned int shaded:1;
		unsigned int maximized:10;
		unsigned int old_maximized:10;
		unsigned int fullscreen:1;
		/* window is currently placed by the workspace tiling layout,
		 * old_geometry holds the pre-tiling frame */
		unsigned int tiled:1;
		/* runtime opt-out of the workspace tiling layout, set by the
		 * TilingToggleKey and by starting an interactive move/resize */
		unsigned int no_tiling:1;
		long fullscreen_monitors[4];
		unsigned int semi_focused:1;
		/* window type flags */
		unsigned int urgent:1;		/* if wm_hints says this is urgent */
#ifdef USE_XSHAPE
		unsigned int shaped:1;
#endif

		/* info flags */
		unsigned int is_gnustep:1;	/* 1 if the window belongs to a GNUstep
						 * app */
		unsigned int is_dockapp:1;	/* 1 if the window belongs to a DockApp */

		unsigned int icon_moved:1;	/* icon for this window was moved
						 * by the user */
		unsigned int selected:1;	/* multiple window selection */
		unsigned int skip_next_animation:1;
		unsigned int internal_window:1;
		unsigned int changing_workspace:1;

		unsigned int inspector_open:1;	/* attrib inspector is already open */

		unsigned int destroyed:1;	/* window was already destroyed */
		unsigned int menu_open_for_me:1;/* window commands menu */
		unsigned int obscured:1;	/* window is obscured */

		unsigned int net_skip_pager:1;
		unsigned int net_handle_icon:1;
		unsigned int net_show_desktop:1;
		unsigned int net_has_title:1;	/* use netwm version of WM_NAME */
		unsigned int net_modal:1;	/* _NET_WM_STATE_MODAL */
		unsigned int net_demands_attention:1;	/* _NET_WM_STATE_DEMANDS_ATTENTION */
		/* True from wManageWindow() entry until just before it
		 * returns. wWindowConfigureBorders() uses it to skip the
		 * top_width-compensation frame shift: on the initial setup
		 * path the frame position was already chosen (saved frame
		 * origin via _WINDOWMAKER_STATE, or fresh placement) for
		 * the FINAL decorations, so the "preserve client position"
		 * shift that is correct for an interactive decoration toggle
		 * would instead drift the frame by |Δtop_width| every
		 * restart. Covers both the static-rule apply at first
		 * wWindowConfigureBorders() and the first dynamic-rule
		 * apply via wWindowReapplyDecorativeAttrs() at map time. */
		unsigned int initializing:1;
		unsigned int restore_iconified:1;	/* iconified by session
						 * restore, not by the user,
						 * suppress deiconify on client
						 * re-map and re-queue state
						 * if unmanaged */
	} flags;				/* state of the window */

	struct WIcon *icon;			/* Window icon when miminized
						 * else is NULL! */
	int icon_x, icon_y;			/* position of the icon */
	int icon_w, icon_h;
	RImage *net_icon_image;			/* Window Image */
	RImage *wm_hints_icon_image;		/* lazy decode of WM_HINTS icon_pixmap */
	xcb_atom_t type;
	char *mark_key_label;		/* Vim-like Window Marking */

	/* window swallowing: terminals with swallow_child hide themselves
	 * when a child GUI process maps a window, and reappear when it
	 * exits */
	struct WWindow *swallowed;	/* child window this terminal is swallowing */
	struct WWindow *swallower;	/* terminal that swallowed this window */

	WWindowGroup *tab_group;	/* tab group this window belongs to, or NULL */
	int tab_group_index;		/* index within tab_group->members[] */
	unsigned restore_tab_gid;	/* saved tab-group identifier carried from
					 * wManageWindow() to wGroupRestoreMember(),
					 * zero when no membership was saved */
	int restore_tab_idx;
	int restore_tab_active;

	unsigned int creation_id;	/* per-screen monotonic manage-order id,
					 * used by SwitcherOrder = Age */

	WFrameBorderOverride frame_border;	/* per-window frame border overrides */
	WGeometry start_geometry;			/* StartGeometry attribute */
	unsigned int start_geometry_set:1;	/* StartGeometry was specified */
	WWindowAttributes regex_applied_mask;	/* which legal-delta fields the regex
						 * layer supplied at the last
						 * re-eval/map-time apply, so the next
						 * re-eval can revert them to baseline */

	/* invoked from wWindowMove()/wWindowConfigure() after geometry is
	 * final, so an attached overlay can track the window */
	void (*on_geometry_change)(struct WWindow *wwin, void *data);
	void *geometry_data;
} WWindow;

#define HAS_TITLEBAR(w)		(!(WFLAGP((w), no_titlebar) || (w)->flags.fullscreen))
#define HAS_RESIZEBAR(w)	(!(WFLAGP((w), no_resizebar) || (w)->flags.fullscreen))
#define HAS_BORDER(w)		(!(WFLAGP((w), no_border) || (w)->flags.fullscreen))
#define IS_MOVABLE(w)		(!(WFLAGP((w), no_movable) || (w)->flags.fullscreen))
#define IS_RESIZABLE(w)	(!(WFLAGP((w), no_resizable) || (w)->flags.fullscreen))
#define ISMAPPED(w)		((w) && !(w)->flags.miniaturized && ((w)->flags.mapped || (w)->flags.shaded))
#define ISFOCUSED(w)		((w) && (w)->flags.focused)
#define WINDOW_LEVEL(w)		((w)->frame->core->stacking->window_level)

/* Which sides the inner frame border is painted on, matching the
 * _NET_FRAME_EXTENTS order: left, right, top, bottom. An empty sides
 * selection (or an unset override) means all four. */
#define INNER_SIDE_LEFT   (1u << 0)
#define INNER_SIDE_RIGHT  (1u << 1)
#define INNER_SIDE_TOP    (1u << 2)
#define INNER_SIDE_BOTTOM (1u << 3)
#define INNER_SIDE_ALL    (INNER_SIDE_LEFT | INNER_SIDE_RIGHT | INNER_SIDE_TOP | INNER_SIDE_BOTTOM)

int wFrameBorderWidth(const WWindow *wwin);
int wFrameInnerBorderWidth(const WWindow *wwin);
unsigned int wFrameInnerBorderSides(const WWindow *wwin);
void wFrameInnerBorderWidths(const WWindow *wwin, int *il, int *ir, int *it, int *ib);
void wFrameInnerBorderClientOrigin(const WWindow *wwin, int frame_x, int frame_y,
				   int *client_x, int *client_y);
WMColor *wFrameBorderColor(const WWindow *wwin);
WMColor *wFrameFocusedBorderColor(const WWindow *wwin);
WMColor *wFrameSelectedBorderColor(const WWindow *wwin);
WMColor *wFrameInnerBorderColor(const WWindow *wwin);
WMColor *wFrameInnerFocusedBorderColor(const WWindow *wwin);

/* Workspace membership predicates. WWIN_PRIMARY_WS names the single
 * workspace used for labels, EWMH _NET_WM_DESKTOP and tiling ownership.
 * WWIN_ON_WORKSPACE tests the ws_mask bitset. Omnipresent has two
 * sources: the WWindowAttributes hint (window type or a saved
 * per-class attribute) via WFLAGP, which forces it regardless of the
 * mask, and the runtime toggle which is ws_mask == WS_ALL. */
#define WS_BIT(n)		((uint64_t)1 << (n))
#define WS_ALL			(~(uint64_t)0)
#define WWIN_WS_MASK(w)		((w)->frame->ws_mask)
#define WWIN_PRIMARY_WS(w)	((w)->frame->workspace)
#define IS_OMNIPRESENT(w)	(WFLAGP(w, omnipresent) || ((w)->frame && WWIN_WS_MASK(w) == WS_ALL))
#define WWIN_ON_WORKSPACE(w, n)	(WFLAGP(w, omnipresent) || (WWIN_WS_MASK(w) & WS_BIT(n)))

#ifdef VIRTUAL_DESKTOP
/* Pan-sticky predicate. A fullscreen window is also treated as
 * pan-stuck so panning cannot strand a fullscreen client pointing at
 * empty canvas. */
# ifndef IS_VSTUCK
#  define IS_VSTUCK(w)	(WFLAGP((w), virtual_stick) || (w)->flags.fullscreen)
# endif
#endif

/* _WINDOWMAKER_STATE quad count, shared by save and prefetch.
 * Slots 16-23 carry the per-window menu state (stacking level and the
 * decoration and skip tri-states) so it survives a warm restart. */
#define WSAVED_STATE_SLOTS 24

/*
 * Changes to this must update wWindowSaveState/getSavedState
 */
typedef struct WSavedState {
    int workspace;
    uint64_t ws_mask;		       /* 0 = derive WS_BIT(workspace) */
    int miniaturized;
    int maximized;
    int shaded;
    int no_titlebar;
    int no_titlebar_set;
    int no_resizebar;
    int no_resizebar_set;
    int no_close_button;
    int no_close_button_set;
    int no_miniaturize_button;
    int no_miniaturize_button_set;
    int no_border;
    int no_border_set;
    int hidden;
    int auto_hide_on_unfocus;
    int auto_hide_on_unfocus_set;
    int auto_miniaturize_on_unfocus;
    int auto_miniaturize_on_unfocus_set;
    int no_movable;
    int no_movable_set;
    int no_resizable;
    int no_resizable_set;
    int stacking_level;	       /* 0 unset, 1 normal, 2 floating, 3 sunken */
    int skip_window_list;      /* user value, valid when _set is 1 */
    int skip_window_list_set;
    int skip_switchpanel;
    int skip_switchpanel_set;
    int x;			       /* original geometry of the */
    int y;			       /* window if it's maximized */
    unsigned int w;
    unsigned int h;
    unsigned window_shortcuts; /* mask like 1<<shortcut_number */
    char *mark_key;	/* serialised mark key label */
    unsigned tab_group_gid;    /* non-zero when this window was a tab member */
    int tab_group_idx;         /* position within the saved group */
    int tab_group_active;      /* saved active_index of the group */
} WSavedState;

typedef struct WWindowState {
    char *instance;
    char *class;
    char *command;
    pid_t pid;
    WSavedState *state;
    struct WWindowState *next;
} WWindowState;

typedef void* WMagicNumber;

/*
 * Batched property prefetch for managing a window. wManagePrefetch()
 * issues every WXReq* in one burst so the replies arrive in a single
 * server round-trip, and consumers in wManageWindow() and the helpers
 * it calls receive individual cookies via WXRcv*. wManageDiscard()
 * releases any cookie that was not consumed (already-consumed or
 * never-issued sequence numbers are harmless to discard).
 */
typedef struct {
    WXCookie desktop;       /* _NET_WM_DESKTOP */
    WXCookie state;         /* _NET_WM_STATE */
    WXCookie type;          /* _NET_WM_WINDOW_TYPE */
    WXCookie strut;         /* _NET_WM_STRUT */
    WXCookie strut_partial; /* _NET_WM_STRUT_PARTIAL */
} WNETWMHintCookies;

/* one-shot reads done only on the manage path, kept separate from
 * WNETWMHintCookies so the shared bundle's other consumers do not have
 * to discard cookies they never asked for */
typedef struct {
    WXCookie handled_icons; /* _NET_WM_HANDLED_ICONS */
    WXCookie icon_geom;     /* _NET_WM_ICON_GEOMETRY */
    WXCookie opacity;       /* _NET_WM_WINDOW_OPACITY */
    WXCookie user_time_win; /* _NET_WM_USER_TIME_WINDOW */
    WXCookie user_time;     /* _NET_WM_USER_TIME (on the client window) */
    WXCookie startup_id;    /* _NET_STARTUP_ID */
    WXCookie icon;          /* _NET_WM_ICON */
    WXCookie machine;       /* WM_CLIENT_MACHINE */
} WNETWMInitialCookies;

typedef struct {
    WXCookie wm_state;      /* WM_STATE */
    WXCookie net_name;      /* _NET_WM_NAME */
    WXCookie name;          /* WM_NAME */
    WXCookie wclass;        /* WM_CLASS */
    WXCookie gnustep;       /* _GNUSTEP_WM_ATTR */
    WXCookie leader;        /* WM_CLIENT_LEADER */
    WXCookie hints;         /* WM_HINTS */
    WXCookie protocols;     /* WM_PROTOCOLS */
    WXCookie transient;     /* WM_TRANSIENT_FOR */
    WXCookie normal;        /* WM_NORMAL_HINTS */
    WXCookie motif;         /* _MOTIF_WM_HINTS */
    WXCookie cmap_windows;  /* WM_COLORMAP_WINDOWS */
    WXCookie command;       /* WM_COMMAND */
	WXCookie role;          /* WM_WINDOW_ROLE */
    WXCookie pid;           /* _NET_WM_PID, fallback for wGetWindowPid */
    WXCookie appmenu;       /* _WINDOWMAKER_MENU (existence) */
    WXCookie gtk;           /* _GTK_APPLICATION_OBJECT_PATH (existence) */
    WXCookie saved_state;   /* _WINDOWMAKER_STATE */
    WXCookie mark_key;      /* _WINDOWMAKER_MARK_KEY */
    WXCookie tab_group;     /* _WINDOWMAKER_TABGROUP */
    WNETWMHintCookies netwm;
    WNETWMInitialCookies netwm_initial;
} WManageCookies;

void wManagePrefetch(xcb_window_t window, WManageCookies *ck);
void wManageDiscard(WManageCookies *ck);

void wWindowDestroy(WWindow *wwin);
WWindow *wWindowCreate(void);

#ifdef USE_XSHAPE
void wWindowSetShape(WWindow *wwin);
void wWindowClearShape(WWindow *wwin);
#endif

WWindow *wManageWindow(WScreen *scr, xcb_window_t window,
		       const WXWindowAttributes *prefetched_attrs);

void wUnmanageWindow(WWindow *wwin, bool restore, bool destroyed);

void wWindowSingleFocus(WWindow *wwin);
void wWindowFocusPrev(WWindow *wwin, bool inSameWorkspace);
void wWindowFocusNext(WWindow *wwin, bool inSameWorkspace);
void wWindowFocus(WWindow *wwin, WWindow *owin);
void wWindowUnfocus(WWindow *wwin);

/* Idempotent: arms the per-window titlebar blink when the window is urgent
 * and blinkable, disarms it otherwise. Mirrors wAppBounceWhileUrgent(). */
void wWindowUrgentBlink(WWindow *wwin);

void wWindowUpdateName(WWindow *wwin, const char *newTitle);
void wWindowConstrainSize(WWindow *wwin, unsigned int *nwidth, unsigned int *nheight);
void wWindowCropSize(WWindow *wwin, unsigned int maxw, unsigned int maxh,
                     unsigned int *nwidth, unsigned int *nheight);
void wWindowConfigure(WWindow *wwin, int req_x, int req_y,
                      int req_width, int req_height);

void wWindowMove(WWindow *wwin, int req_x, int req_y);
void wWindowSnapToHead(WWindow *wwin);

void wWindowSynthConfigureNotify(WWindow *wwin);

WWindow *wWindowFor(xcb_window_t window);


void wWindowConfigureBorders(WWindow *wwin);
void wWindowApplyInnerBorder(WWindow *wwin);

void wWindowUpdateButtonImages(WWindow *wwin);

void wWindowRefreshTitlebarIcon(WWindow *wwin);

void wWindowSaveState(WWindow *wwin);

void wWindowChangeWorkspace(WWindow *wwin, int workspace);
void wWindowChangeWorkspaceRelative(WWindow *wwin, int amount);
void wWindowSetWorkspaceMask(WWindow *wwin, uint64_t mask, int primary);
void wWindowAddToWorkspace(WWindow *wwin, int workspace);
void wWindowRemoveFromWorkspace(WWindow *wwin, int workspace);

void wWindowSetKeyGrabs(WWindow *wwin);

void wWindowResetMouseGrabs(WWindow *wwin);

WWindow *wManageInternalWindow(WScreen *scr, xcb_window_t window, xcb_window_t owner,
                               const char *title, int x, int y,
                               int width, int height);

void wWindowSetupInitialAttributes(WWindow *wwin, WManageCookies *ck, int *level, int *workspace);

void wWindowUpdateGNUstepAttr(WWindow *wwin, GNUstepWMAttributes *attr);

void wWindowMap(WWindow *wwin);

void wWindowUnmap(WWindow *wwin);

pid_t wGetWindowPid(xcb_window_t win);

void wWindowDeleteSavedStatesForPID(pid_t pid);
void wWindowDeleteUnmatchedSavedStates(void);

WMagicNumber wWindowAddSavedState(const char *instance, const char *class, const char *command,
                                  pid_t pid, WSavedState *state);

WMagicNumber wWindowGetSavedState(xcb_window_t win);

void wWindowDeleteSavedState(WMagicNumber id);

bool wWindowCommandsMatch(const char *saved, const char *current);

bool wWindowIsFullyCovered(WWindow *wwin);
bool wWindowObscuresWindow(WWindow *wwin, WWindow *obscured);

void wWindowSetOmnipresent(WWindow *wwin, bool flag);

/* Vim-like window marking management */
void wWindowSetMark(WWindow *wwin, const char *label);
void wWindowUnsetMark(WWindow *wwin);
void wWindowStealMark(WWindow *wwin, const char *label);

#ifdef XKB_BUTTON_HINT
void wWindowGetLanguageLabel(int group_index, char *label);
#endif

#endif
