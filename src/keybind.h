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

#ifndef WMKEYBIND_H
#define WMKEYBIND_H

#include <stdbool.h>

#include <xcb/xcb.h>

#define MAX_SHORTCUT_LENGTH 64

struct _WScreen;
struct WWindow;

/* <X11/X.h> doesn't define these, even though XFree supports them */
#ifndef Button6
#define Button6 6
#endif

#ifndef Button7
#define Button7 7
#endif

#ifndef Button8
#define Button8 8
#endif

#ifndef Button9
#define Button9 9
#endif

enum {
	/* anywhere */
	WKBD_ROOTMENU,
	WKBD_WINDOWMENU,
	WKBD_WINDOWLIST,
	WKBD_KEYCHAIN_CANCEL,

	/* window */
	WKBD_MINIATURIZE,
	WKBD_MINIATURIZEALL,
	WKBD_MINIATURIZE_OTHERS,
	WKBD_HIDE,
	WKBD_HIDE_OTHERS,
	WKBD_SHOW_DESKTOP,
	WKBD_SHOWALL,
	WKBD_ARRANGEICONS,
	WKBD_MAXIMIZE,
	WKBD_VMAXIMIZE,
	WKBD_HMAXIMIZE,
	WKBD_LHMAXIMIZE,
	WKBD_RHMAXIMIZE,
	WKBD_THMAXIMIZE,
	WKBD_BHMAXIMIZE,
	WKBD_LTCMAXIMIZE,
	WKBD_RTCMAXIMIZE,
	WKBD_LBCMAXIMIZE,
	WKBD_RBCMAXIMIZE,
	WKBD_MAXIMUS,
	WKBD_FULLSCREEN,
	WKBD_SELECT,
	WKBD_KEEP_ON_TOP,
	WKBD_KEEP_AT_BOTTOM,
	WKBD_OMNIPRESENT,
	WKBD_RAISE,
	WKBD_LOWER,
	WKBD_RAISELOWER,
	WKBD_MOVERESIZE,
	WKBD_SHADE,
	WKBD_WORKSPACEMAP,
	WKBD_FOCUSNEXT,
	WKBD_FOCUSPREV,
	WKBD_FOCUSLEFT,
	WKBD_FOCUSRIGHT,
	WKBD_FOCUSUP,
	WKBD_FOCUSDOWN,
	WKBD_GROUPWINDOWNEXT,
	WKBD_GROUPWINDOWPREV,
	WKBD_GROUPNEXT,
	WKBD_GROUPPREV,
	WKBD_CENTRAL,
	WKBD_MARK_SET,
	WKBD_MARK_UNSET,
	WKBD_MARK_BRING,
	WKBD_MARK_JUMP,
	WKBD_MARK_SWAP,

	/* window, menu */
	WKBD_CLOSE,

	/* Dock */
	WKBD_DOCKRAISELOWER,

	/* Clip */
	WKBD_CLIPRAISELOWER,

	/* workspace */
	WKBD_WORKSPACE1,
	WKBD_WORKSPACE2,
	WKBD_WORKSPACE3,
	WKBD_WORKSPACE4,
	WKBD_WORKSPACE5,
	WKBD_WORKSPACE6,
	WKBD_WORKSPACE7,
	WKBD_WORKSPACE8,
	WKBD_WORKSPACE9,
	WKBD_WORKSPACE10,
	WKBD_NEXTWORKSPACE,
	WKBD_PREVWORKSPACE,
	WKBD_LASTWORKSPACE,
	WKBD_NEXTWSLAYER,
	WKBD_PREVWSLAYER,
	WKBD_LEFTWORKSPACE,
	WKBD_RIGHTWORKSPACE,
	WKBD_UPWORKSPACE,
	WKBD_DOWNWORKSPACE,

	/* move to workspace */
	WKBD_MOVE_WORKSPACE1,
	WKBD_MOVE_WORKSPACE2,
	WKBD_MOVE_WORKSPACE3,
	WKBD_MOVE_WORKSPACE4,
	WKBD_MOVE_WORKSPACE5,
	WKBD_MOVE_WORKSPACE6,
	WKBD_MOVE_WORKSPACE7,
	WKBD_MOVE_WORKSPACE8,
	WKBD_MOVE_WORKSPACE9,
	WKBD_MOVE_WORKSPACE10,

	/* toggle workspace membership */
	WKBD_TOGGLE_WORKSPACE1,
	WKBD_TOGGLE_WORKSPACE2,
	WKBD_TOGGLE_WORKSPACE3,
	WKBD_TOGGLE_WORKSPACE4,
	WKBD_TOGGLE_WORKSPACE5,
	WKBD_TOGGLE_WORKSPACE6,
	WKBD_TOGGLE_WORKSPACE7,
	WKBD_TOGGLE_WORKSPACE8,
	WKBD_TOGGLE_WORKSPACE9,
	WKBD_TOGGLE_WORKSPACE10,

	WKBD_MOVE_NEXTWORKSPACE,
	WKBD_MOVE_PREVWORKSPACE,
	WKBD_MOVE_LASTWORKSPACE,
	WKBD_MOVE_NEXTWSLAYER,
	WKBD_MOVE_PREVWSLAYER,
	WKBD_MOVE_LEFTWORKSPACE,
	WKBD_MOVE_RIGHTWORKSPACE,
	WKBD_MOVE_UPWORKSPACE,
	WKBD_MOVE_DOWNWORKSPACE,

	/* window shortcuts */
	WKBD_WINDOW1,
	WKBD_WINDOW2,
	WKBD_WINDOW3,
	WKBD_WINDOW4,
	WKBD_WINDOW5,
	WKBD_WINDOW6,
	WKBD_WINDOW7,
	WKBD_WINDOW8,
	WKBD_WINDOW9,
	WKBD_WINDOW10,

	/* shortcuts to move window between heads */
	WKBD_MOVE_12_TO_6_HEAD,
	WKBD_MOVE_6_TO_12_HEAD,

	/* launch a new instance of the active window */
	WKBD_RELAUNCH,

	/* screen */
	WKBD_SWITCH_SCREEN,

	/* open the "find" dialog */
	WKBD_RUN,

	/* open the keyboard shortcuts panel */
	WKBD_SHOWKEYBINDS,

	/* open "exit" dialog */
	WKBD_EXIT,

	/* screen print */
	WKBD_PRINTS,

	/* window print */
	WKBD_PRINTW,

	/* partial print */
	WKBD_PRINTP,

	/* run LockScreenCommand */
	WKBD_LOCKSCREEN,

	/* run ClipboardCommand */
	WKBD_CLIPBOARD,

	/* highlight the mouse pointer */
	WKBD_FINDMOUSE,

#ifdef KEEP_XKB_LOCK_STATUS
	WKBD_TOGGLE,
	WKBD_ACCENTPICKER,
#endif
	/* tab-group cycling and management */
	WKBD_GROUPTAB_NEXT,
	WKBD_GROUPTAB_PREV,
	WKBD_GROUPTAB_ATTACH,
	WKBD_GROUPTAB_DETACH,
	/* per-workspace tiling layout */
	WKBD_LAYOUTCYCLE,
	WKBD_LAYOUTCYCLEBACK,
	WKBD_LAYOUTAXIS,
	WKBD_LAYOUTFLOAT,
	WKBD_MASTERCOUNTINC,
	WKBD_MASTERCOUNTDEC,
	WKBD_MASTERRATIOINC,
	WKBD_MASTERRATIODEC,
	WKBD_TILINGTOGGLE,
	WKBD_TILINGSWAP,
#ifdef VIRTUAL_DESKTOP
	/* viewport pan + home/center/pin */
	WKBD_VDESK_LEFT,
	WKBD_VDESK_RIGHT,
	WKBD_VDESK_UP,
	WKBD_VDESK_DOWN,
	WKBD_VDESK_HOME,
	WKBD_VDESK_CENTER,
	WKBD_VDESK_PIN,
#endif
	/* edge-snap actions */
	WKBD_MOVE_TO_EDGE_LEFT,
	WKBD_MOVE_TO_EDGE_RIGHT,
	WKBD_MOVE_TO_EDGE_UP,
	WKBD_MOVE_TO_EDGE_DOWN,
	WKBD_MOVE_TO_EDGE_LEFT_NOSNAP,
	WKBD_MOVE_TO_EDGE_RIGHT_NOSNAP,
	WKBD_MOVE_TO_EDGE_UP_NOSNAP,
	WKBD_MOVE_TO_EDGE_DOWN_NOSNAP,
	WKBD_GROW_TO_EDGE_LEFT,
	WKBD_GROW_TO_EDGE_RIGHT,
	WKBD_GROW_TO_EDGE_UP,
	WKBD_GROW_TO_EDGE_DOWN,
	WKBD_SHRINK_TO_EDGE_LEFT,
	WKBD_SHRINK_TO_EDGE_RIGHT,
	WKBD_SHRINK_TO_EDGE_UP,
	WKBD_SHRINK_TO_EDGE_DOWN,
	/* keep this last */
	WKBD_LAST
};

typedef struct WShortKey {
    unsigned int modifier;   /* leader (or only) key modifier - always valid */
    xcb_keycode_t keycode;    /* leader (or only) key code - always valid */

	/* Key-chain support */
    int chain_length;
    unsigned int *chain_modifiers;   /* heap-allocated, NULL for single keys */
    xcb_keycode_t *chain_keycodes;    /* heap-allocated, NULL for single keys */
} WShortKey;

/* ---[ Global Variables ]------------------------------------------------ */

extern WShortKey wKeyBindings[WKBD_LAST];

/* ---[ Functions ]------------------------------------------------------- */

void wShortKeyFree(WShortKey *key);

/* Execute a single key-binding action. `event` may be NULL when the
 * action is not driven by a key press (IPC, hot corners, gestures), and
 * the dispatch then falls back to the last known event timestamp. */
void wKeybindDispatch(int command, struct _WScreen *scr, struct WWindow *wwin,
		      xcb_generic_event_t *event);

/* Resolve an action name from the key-binding schema to its WKBD_*
 * command index. Accepts the config key spelling ("NextWorkspaceKey")
 * or the short action name ("NextWorkspace"). Returns -1 if unknown. */
int wKeybindForAction(const char *name);

/* True when the clipboard keybinding dispatched its command from a
 * real key press within the last few seconds. Consumes the recorded
 * dispatch, so one key invocation marks exactly one tick. */
bool wClipboardTakeKeyHint(void);

/* Read-only, duplicate-free list of advertised action names, used by the
 * IPC `info` verb so clients can validate names without executing them. */
int wKeybindActionCount(void);
const char *wKeybindActionName(int index);

/* One row of the active key-binding table, shared by the IPC
 * `get-keybinds` verb and the shortcuts panel. */
typedef struct WKeybindInfo {
	const char *action;      /* short action name, e.g. "Hide" */
	const char *config_key;  /* schema key, e.g. "HideKey" */
	const char *spec;        /* raw config string, e.g. "Mod1+H" */
	const char *category;    /* group heading, e.g. "Windows" */
	const char *description; /* human-readable summary */
	char *label;             /* rendered display label, NULL if unbound */
	int bound;               /* nonzero when a shortcut is active */
} WKeybindInfo;

/* Collect the schema key-binding rows in schema order. When
 * include_unbound is zero only rows with an active shortcut are
 * returned. The returned array is owned by the caller and must be
 * released with wKeybindFree(); *count receives the number of rows. */
WKeybindInfo *wKeybindCollect(int include_unbound, int *count);
void wKeybindFree(WKeybindInfo *items, int count);

#endif /* WMKEYBIND_H */
