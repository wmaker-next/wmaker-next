/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
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

#ifndef WINDOWMAKER_H_
#define WINDOWMAKER_H_

#include "wconfig.h"
#include <assert.h>
#include <limits.h>
#include <signal.h>
#include <xcb/xcb.h>
#include <WINGs/WINGs.h>
#include <WINGs/WXCore.h>
#include "keytree.h"
#include "wconfigmodel.h"


/* class codes */
typedef enum {
	WCLASS_UNKNOWN = 0,
	WCLASS_WINDOW = 1,          /* managed client windows */
	WCLASS_MENU = 2,            /* root menus */
	WCLASS_APPICON = 3,
	WCLASS_DUMMYWINDOW = 4,     /* window that holds window group leader */
	WCLASS_MINIWINDOW = 5,
	WCLASS_DOCK_ICON = 6,
	WCLASS_PAGER = 7,
	WCLASS_TEXT_INPUT = 8,
	WCLASS_FRAME = 9
} WClassType;


/*
 * generic window levels (a superset of the N*XTSTEP ones)
 * Applications should use levels between WMDesktopLevel and
 * WMScreensaverLevel anything boyond that range is allowed,
 * but discouraged.
 */
enum {
	WMBackLevel = INT_MIN+1,    /* Very lowest level */
	WMDesktopLevel = -1000,     /* Lowest level of normal use */
	WMSunkenLevel = -1,
	WMNormalLevel = 0,
	WMFloatingLevel = 3,
	WMDockLevel = 5,
	WMSubmenuLevel = 15,
	WMMainMenuLevel = 20,
	WMStatusLevel = 21,
	WMFullscreenLevel = 50,
	WMModalLevel = 100,
	WMPopUpLevel = 101,
	WMScreensaverLevel = 1000,
	WMOuterSpaceLevel = INT_MAX
};

/*
 * WObjDescriptor will be used by the event dispatcher to
 * send events to a particular object through the methods in the
 * method table. If all objects of the same class share the
 * same methods, the class method table should be used, otherwise
 * a new method table must be created for each object.
 * It is also assigned to find the parent structure of a given
 * window (like the WWindow or WMenu for a button)
 */

typedef struct WObjDescriptor {
	void *self;                        /* the object that will be called */
	/* event handlers */
	void (*handle_expose)(struct WObjDescriptor *sender, xcb_generic_event_t *event);
	void (*handle_mousedown)(struct WObjDescriptor *sender, xcb_generic_event_t *event);
	void (*handle_motionnotify)(struct WObjDescriptor *sender, xcb_generic_event_t *event);
	void (*handle_enternotify)(struct WObjDescriptor *sender, xcb_generic_event_t *event);
	void (*handle_leavenotify)(struct WObjDescriptor *sender, xcb_generic_event_t *event);

	WClassType parent_type;            /* type code of the parent */
	void *parent;                      /* parent object (WWindow or WMenu) */
} WObjDescriptor;

/* internal buttons */
#define WBUT_CLOSE              0
#define WBUT_BROKENCLOSE        1
#define WBUT_ICONIFY            2
#define WBUT_KILL		3
#define WBUT_MAXIMIZE		4
#define WBUT_RESTORE		5
#ifdef XKB_BUTTON_HINT
#define WBUT_XKBGROUP1		6
#define WBUT_XKBGROUP2		7
#define WBUT_XKBGROUP3		8
#define WBUT_XKBGROUP4		9
#define PRED_BPIXMAPS		10 /* reserved for 4 groups */
#else
#define PRED_BPIXMAPS		6 /* count of WBUT icons */
#endif /* XKB_BUTTON_HINT */

/*
 * Titlebar button roles, decoupled from the side they sit on. The
 * positional layout (left/right cluster, order within a cluster) is
 * read from the TitlebarLeft/TitlebarRight defaults at startup and
 * stored in WScreen.titlebar_layout. These types live here, not in
 * framewin.h, because screen.h embeds WTitlebarLayout and
 * config_schema_def.h is reached via wconfigmodel.h before any frame
 * header.
 */
typedef enum {
	WTB_MINIATURIZE,
	WTB_CLOSE,
	WTB_MAXIMIZE,
	WTB_ICON,
#ifdef XKB_BUTTON_HINT
	WTB_LANGUAGE,
#endif
	WTB_ROLE_COUNT
} WTitlebarRole;

typedef enum {
	WTB_SIDE_NONE,
	WTB_SIDE_LEFT,
	WTB_SIDE_RIGHT
} WTitlebarSide;

typedef struct WTitlebarLayout {
	WTitlebarSide side[WTB_ROLE_COUNT];
	short order[WTB_ROLE_COUNT];
} WTitlebarLayout;

/* Mouse cursors */
typedef enum {
	WCUR_NORMAL,
	WCUR_MOVE,
	WCUR_RESIZE,
	WCUR_TOPLEFTRESIZE,
	WCUR_TOPRIGHTRESIZE,
	WCUR_BOTTOMLEFTRESIZE,
	WCUR_BOTTOMRIGHTRESIZE,
	WCUR_VERTICALRESIZE,
	WCUR_HORIZONRESIZE,
	WCUR_WAIT,
	WCUR_ARROW,
	WCUR_QUESTION,
	WCUR_TEXT,
	WCUR_SELECT,
	WCUR_CAPTURE,
	WCUR_ROOT,
	WCUR_EMPTY,

	/* Count of the number of cursors defined */
	WCUR_LAST
} w_cursor;

#include "config_values.h"

/* program states */
typedef enum {
	WSTATE_NORMAL		= 0,
	WSTATE_NEED_EXIT	= 1,
	WSTATE_NEED_RESTART	= 2,
	WSTATE_EXITING		= 3,
	WSTATE_RESTARTING	= 4,
	WSTATE_MODAL		= 5,
	WSTATE_NEED_REREAD	= 6
} wprog_state;


#define WCHECK_STATE(chk_state)	(w_global.program.state == (chk_state))


#define WCHANGE_STATE(nstate) {\
		if (w_global.program.state == WSTATE_NORMAL		\
		    || (nstate) != WSTATE_MODAL)			\
			w_global.program.state = (nstate);		\
		if (w_global.program.signal_state != 0)			\
			w_global.program.state = w_global.program.signal_state;	\
}


/* only call inside signal handlers, with signals blocked */
#define SIG_WCHANGE_STATE(nstate) {\
		w_global.program.signal_state = (nstate);	\
		w_global.program.state = (nstate);		\
}


/* Flags for the Window Maker state when restarting/crash situations */
#define WFLAGS_NONE       (0)
#define WFLAGS_CRASHED    (1<<0)


/* notifications */

#ifdef MAINFILE
#define NOTIFICATION(n) const char WN##n [] = #n
#else
#define NOTIFICATION(n) extern const char WN##n []
#endif

NOTIFICATION(WindowAppearanceSettingsChanged);

NOTIFICATION(IconAppearanceSettingsChanged);

NOTIFICATION(IconTileSettingsChanged);

NOTIFICATION(MenuAppearanceSettingsChanged);

NOTIFICATION(MenuTitleAppearanceSettingsChanged);


/* appearance settings clientdata flags */
enum {
	WFontSettings = 1 << 0,
	WTextureSettings = 1 << 1,
	WColorSettings = 1 << 2
};



typedef struct {
	int x1, y1;
	int x2, y2;
} WArea;

typedef struct WCoord {
	int x, y;
} WCoord;

/*
 * Connection-scoped runtime state. One instance, created in real_main().
 * NULL in the monitor process.
 */
typedef struct WDisplay {
	xcb_connection_t *conn;        /* attached by wDisplayAttach() */
	int default_screen;

	/* window -> object tables (the XContext replacements) */
	struct {
		WMHashTable *client_win;
		WMHashTable *app_win;
		WMHashTable *stack;
	} context;

	/* interned atoms */
	struct {
		/* Window-Manager related */
		struct {
			xcb_atom_t state;
			xcb_atom_t change_state;
			xcb_atom_t protocols;
			xcb_atom_t take_focus;
			xcb_atom_t delete_window;
			xcb_atom_t save_yourself;
			xcb_atom_t client_leader;
			xcb_atom_t colormap_windows;
			xcb_atom_t colormap_notify;
			xcb_atom_t motif_wm_hints;
			xcb_atom_t ignore_focus_events;
			xcb_atom_t window_role;
		} wm;

		/* GNUStep related */
		struct {
			xcb_atom_t wm_attr;
			xcb_atom_t wm_miniaturize_window;
			xcb_atom_t wm_hide_app;
			xcb_atom_t wm_resizebar;
			xcb_atom_t titlebar_state;
		} gnustep;

		/* Destkop-environment related */
		struct {
			xcb_atom_t gtk_object_path;
		} desktop;

		/* WindowMaker specific */
		struct {
			xcb_atom_t menu;
			xcb_atom_t wm_protocols;
			xcb_atom_t state;

			xcb_atom_t wm_function;
			xcb_atom_t noticeboard;
			xcb_atom_t command;

			xcb_atom_t icon_size;
			xcb_atom_t icon_tile;
			xcb_atom_t mark_key;
			xcb_atom_t tab_group;
		} wmaker;

	} atom;

	/* extension presence/event-base (shape, xkb, randr, xsync,
	 * screensaver) */
	struct {
#ifdef USE_XSHAPE
		struct {
			bool supported;
			int event_base;
			int major_version, minor_version;
		} shape;
#endif

		struct {
			bool supported;
			int event_base;
		} xkb;

#ifdef USE_RANDR
		struct {
			bool supported;
			int event_base;
		} randr;
#endif

#ifdef USE_XSYNC
		struct {
			bool supported;
			int event_base;
		} xsync;
#endif

#ifdef USE_XCB_SCREENSAVER
		struct {
			bool supported;
			int event_base;
		} screensaver;
#endif

		struct {
			bool supported;
		} xfixes;

#ifdef USE_XCB_XINPUT
		struct {
			bool supported;
			uint8_t major_opcode;
		} xinput;
#endif

		int dummy;
	} xext;

	/* last-event timestamps */
	struct {
		xcb_timestamp_t last_event;
		xcb_timestamp_t focus_change;
	} timestamp;

	/* [begin,end) request-sequence brackets recorded around bursts
	 * that move/map/unmap windows. Crossings inside a bracket are
	 * dropped so they cannot drive focus/raise/dock handlers.
	 * Zeroed slots match nothing. */
	struct {
		struct {
			unsigned long begin;
			unsigned long end;
		} range[4];
		unsigned int head;
	} ignore_crossing;

	struct _WScreen **screens;
	int screen_count;

	/* borrowed from w_global.config, synced at every assignment */
	WConfig *config;

	/* runtime state shared by the command-line switches */
	struct {
#ifdef USE_ICCCM_WMREPLACE
		unsigned int replace:1;               /* replace existing window manager */
#endif
		unsigned int nodock:1;                /* don't display the dock */
		unsigned int noclip:1;                /* don't display the clip */
		unsigned int clip_merged_in_dock:1;   /* disable clip, switch workspaces with dock */
		unsigned int nodrawer:1;              /* don't use drawers */
		unsigned int wrap_appicons_in_dock:1; /* Whether to wrap appicons when Dock is moved up and down */
		unsigned int noupdates:1;             /* don't require ~/GNUstep (-static) */
		unsigned int noautolaunch:1;          /* don't autolaunch apps */
		unsigned int norestore:1;             /* don't restore session */
		unsigned int restarting:2;
	} flags;

	xcb_cursor_t cursor[WCUR_LAST];

	unsigned int modifier_mask;
	unsigned int tab_drag_modifier_mask;
	char *modifier_labels[7];
	char *modifier_short_labels[9];
	unsigned int supports_tiff;
} WDisplay;

/* the process singleton, NULL in the monitor process */
extern WDisplay *wdisp;

/****** Global Variables ******/
extern struct wmaker_global_variables {
	/* Tracking of the state of the program */
	struct {
		volatile sig_atomic_t state;
		volatile sig_atomic_t signal_state;
	} program;

	/* locale to use. NULL==POSIX or C */
	const char *locale;

	/* the display name the connection was opened on, the
	 * DisplayString(dpy) stand-in for the IPC socket path and the
	 * root-menu DISPLAY macro */
	const char *display_name;

	/* Global Domains, for storing dictionaries */
	struct {
		/* Note: you must #include <defaults.h> if you want to use them */
		struct WDDomain *wmaker;
		struct WDDomain *window_attr;
		struct WDDomain *root_menu;
		struct WDDomain *wmglobal;
	} domain;

	/* Parsed WindowMaker domain currently applied to the runtime state. */
	struct WConfig *config;

	/*
	 * Ignore Workspace Change:
	 * this variable is used to prevent workspace switch while certain
	 * operations are ongoing.
	 */
	bool ignore_workspace_change;

	/* Set while the focus-repair timer re-asserts focus so the
	 * AutoMiniaturize/AutoHide-on-unfocus hook stays out of the
	 * focus it steals from during the repair burst. */
	bool restore_focus_repair;

	/* Set around a pointer-initiated switch so the PointerFollowsMonitor and
	 * PointerFollowsFocus warps are skipped. Toggled by the
	 * wWorkspaceChangeFromPointer wrappers, read by the force-change warp. */
	bool pointer_driven_change;

	/*
	 * Process WorkspaceMap Event:
	 * this variable is set when the Workspace Map window is being displayed,
	 * it is mainly used to avoid re-opening another one at the same time
	 */
	bool process_workspacemap_event;

	/*
	 * Display power state:
	 * set when no output is showing the framebuffer (lid closed,
	 * DPMS Off, screensaver active). Animation entry points and
	 * recurring decorative timers short-circuit while this is set so
	 * an idle session reaches zero wakeups. Toggled by the
	 * `display-power' IPC verb, RandR all-outputs-off detection and
	 * the MIT-SCREEN-SAVER notify event, see wDisplaySetOff().
	 */
	bool display_off;

#ifdef HAVE_INOTIFY
	struct {
		int fd_event_queue;   /* Inotify's queue file descriptor */
		int wd_defaults;   /* Watch Descriptor for the 'Defaults' configuration file */
		WMHandlerID input_handler;
	} inotify;
#endif

	/* Keyboard and shortcuts */
	struct {
		/*
		 * Bit-mask to hide special key modifiers which we don't want to
		 * impact the shortcuts (typically: CapsLock, NumLock, ScrollLock)
		 */
		unsigned int modifiers_mask;

		/*
		 * Key-chain trie cursor.
		 *
		 * curpos == NULL  : idle, no active chain.
		 * curpos != NULL  : inside a chain; curpos points to the last matched
		 *                   internal node in wKeyTreeRoot. The next expected
		 *                   key is one of curpos->first_child's siblings.
		 */
		WKeyNode    *curpos;
		WMHandlerID  chain_timeout_handler; /* non-NULL while chain timer is armed */
	} shortcut;
} w_global;

/****** Notifications ******/
extern const char WMNManaged[];
extern const char WMNUnmanaged[];
extern const char WMNChangedWorkspace[];
extern const char WMNChangedState[];
extern const char WMNChangedFocus[];
extern const char WMNChangedStacking[];
extern const char WMNChangedName[];

extern const char WMNWorkspaceCreated[];
extern const char WMNWorkspaceDestroyed[];
extern const char WMNWorkspaceChanged[];
extern const char WMNWorkspaceNameChanged[];
extern const char WMNWorkspaceLayoutChanged[];

extern const char WMNResetStacking[];

#endif
