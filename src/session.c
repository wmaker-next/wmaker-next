/* session.c - session state handling and R6 style session management
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1998-2003 Dan Pascu
 *  Copyright (c) 1998-2003 Alfredo Kojima
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

/*
 *
 * If defined(XSMP_ENABLED) and session manager is running then
 * 	do normal stuff
 * else
 * 	do pre-R6 session management stuff (save window state and relaunch)
 *
 * When doing a checkpoint:
 *
 * = Without XSMP
 * Open "Stop"/status Dialog
 * Send SAVE_YOURSELF to clients and wait for reply
 * Save restart info
 * Save state of clients
 *
 * = With XSMP
 * Send checkpoint request to sm
 *
 * When exiting:
 * -------------
 *
 * = Without XSMP
 *
 * Open "Exit Now"/status Dialog
 * Send SAVE_YOURSELF to clients and wait for reply
 * Save restart info
 * Save state of clients
 * Send DELETE to all clients
 * When no more clients are left or user hit "Exit Now", exit
 *
 * = With XSMP
 *
 * Send Shutdown request to session manager
 * if SaveYourself message received, save state of clients
 * if the Die message is received, exit.
 */

#include "wconfig.h"


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>

#include "WindowMaker.h"
#include "screen.h"
#include "window.h"
#include "client.h"
#include "session.h"
#include "framewin.h"
#include "workspace.h"
#include "main.h"
#include "properties.h"
#include "application.h"
#include "appicon.h"
#include "dock.h"
#include "misc.h"
#include "tabgroup.h"

#include <WINGs/WUtil.h>


static WMPropList *sApplications = NULL;
static WMPropList *sCommand;
static WMPropList *sName;
static WMPropList *sWorkspace;
static WMPropList *sWorkspaces;
static WMPropList *sShaded;
static WMPropList *sMiniaturized;
static WMPropList *sMaximized;
static WMPropList *sHidden;
static WMPropList *sGeometry;
static WMPropList *sShortcutMask;
static WMPropList *sMarkKey;
static WMPropList *sTabGroup;

static WMPropList *sDock;
static WMPropList *sYes, *sNo;

static void make_keys(void)
{
	if (sApplications != NULL)
		return;

	sApplications = WMCreatePLString("Applications");
	sCommand = WMCreatePLString("Command");
	sName = WMCreatePLString("Name");
	sWorkspace = WMCreatePLString("Workspace");
	sWorkspaces = WMCreatePLString("Workspaces");
	sShaded = WMCreatePLString("Shaded");
	sMiniaturized = WMCreatePLString("Miniaturized");
	sMaximized = WMCreatePLString("Maximized");
	sHidden = WMCreatePLString("Hidden");
	sGeometry = WMCreatePLString("Geometry");
	sDock = WMCreatePLString("Dock");
	sShortcutMask = WMCreatePLString("ShortcutMask");
	sMarkKey = WMCreatePLString("MarkKey");
	sTabGroup = WMCreatePLString("WMTabGroup");

	sYes = WMCreatePLString("Yes");
	sNo = WMCreatePLString("No");
}

static unsigned getInt(WMPropList * value)
{
	char *val;
	unsigned n;

	if (!WMIsPLString(value))
		return 0;
	val = WMGetFromPLString(value);
	if (!val)
		return 0;
	if (sscanf(val, "%u", &n) != 1)
		return 0;

	return n;
}

static unsigned getHex(WMPropList * value)
{
	char *val;
	unsigned n;

	if (!WMIsPLString(value))
		return 0;
	val = WMGetFromPLString(value);
	if (!val)
		return 0;
	if (sscanf(val, "0x%04X", &n) != 1)
		return 0;

	return n;
}

static WMPropList *makeWindowState(WWindow * wwin, WApplication * wapp)
{
	WScreen *scr = wwin->screen_ptr;
	xcb_window_t win;
	int i;
	unsigned mask;
	char *class, *instance, *command = NULL, buffer[512];
	WMPropList *win_state, *cmd, *name, *workspace;
	WMPropList *shaded;
	WMPropList *miniaturized, *maximized, *hidden, *geometry;
	WMPropList *dock, *shortcut;
	WMPropList *mark_key_pl = NULL;

	if (wwin->orig_main_window != XCB_NONE && wwin->orig_main_window != wwin->client_win)
		win = wwin->orig_main_window;
	else
		win = wwin->client_win;

	command = GetCommandForWindow(win);
	if (!command) {
		if (wapp && wapp->app_icon && wapp->app_icon->command) {
			command = wstrdup(wapp->app_icon->command);
		} else
			return NULL;
	}

	if (PropGetWMClass(win, &class, &instance)) {
		if (class && instance) {
			if (class[0] == '\0' && wwin->wm_class && wwin->wm_class[0] != '\0') {
				wunintern(class);
				class = (char *)wintern(wwin->wm_class);
			}
			snprintf(buffer, sizeof(buffer), "%s%s%s", instance, class[0] ? "." : "", class);
		}
		else if (instance)
			snprintf(buffer, sizeof(buffer), "%s", instance);
		else if (class)
			snprintf(buffer, sizeof(buffer), ".%s", class);
		else
			snprintf(buffer, sizeof(buffer), ".");

		name = WMCreatePLString(buffer);
		cmd = WMCreatePLString(command);
		workspace = WMCreatePLString(scr->workspaces[WWIN_PRIMARY_WS(wwin)]->name);

		shaded = wwin->flags.shaded ? sYes : sNo;
		miniaturized = wwin->flags.miniaturized ? sYes : sNo;
		snprintf(buffer, sizeof(buffer), "0x%04X", (unsigned int)wwin->flags.maximized);
		maximized = WMCreatePLString(buffer);
		hidden = wwin->flags.hidden ? sYes : sNo;
		snprintf(buffer, sizeof(buffer), "%ux%u+%i+%i",
			 wwin->client.width, wwin->client.height, wwin->frame_x, wwin->frame_y);
		geometry = WMCreatePLString(buffer);

		for (mask = 0, i = 0; i < MAX_WINDOW_SHORTCUTS; i++) {
			if (scr->shortcutWindows[i] != NULL &&
			    WMGetFirstInArray(scr->shortcutWindows[i], wwin) != WANotFound)
				mask |= 1 << i;
		}

		snprintf(buffer, sizeof(buffer), "%u", mask);
		shortcut = WMCreatePLString(buffer);

		if (wwin->mark_key_label)
			mark_key_pl = WMCreatePLString(wwin->mark_key_label);

		win_state = WMCreatePLDictionary(sName, name,
						 sCommand, cmd,
						 sWorkspace, workspace,
						 sShaded, shaded,
						 sMiniaturized, miniaturized,
						 sMaximized, maximized,
						 sHidden, hidden,
						 sShortcutMask, shortcut, sGeometry, geometry, NULL);

		if (mark_key_pl) {
			WMPutInPLDictionary(win_state, sMarkKey, mark_key_pl);
			WMReleasePropList(mark_key_pl);
		}

		/* Extra workspace memberships beyond the primary. Written only
		 * when the window is on more than one workspace and not
		 * omnipresent so single-membership sessions stay unchanged. */
		if (!IS_OMNIPRESENT(wwin)
		    && WWIN_WS_MASK(wwin) != WS_BIT(WWIN_PRIMARY_WS(wwin))) {
			WMPropList *wl = WMCreatePLArray(NULL);
			WMPropList *e;

			for (i = 0; i < scr->workspace_count; i++) {
				if (!(WWIN_WS_MASK(wwin) & WS_BIT(i)))
					continue;
				e = WMCreatePLString(scr->workspaces[i]->name);
				WMAddToPLArray(wl, e);
				WMReleasePropList(e);
			}
			WMPutInPLDictionary(win_state, sWorkspaces, wl);
			WMReleasePropList(wl);
		}

		if (wwin->tab_group) {
			WMPropList *tab_pl, *e;

			tab_pl = WMCreatePLArray(NULL);
			snprintf(buffer, sizeof(buffer), "%u",
				 wGroupSessionId(scr, wwin->tab_group));
			e = WMCreatePLString(buffer);
			WMAddToPLArray(tab_pl, e);
			WMReleasePropList(e);
			snprintf(buffer, sizeof(buffer), "%d", wwin->tab_group_index);
			e = WMCreatePLString(buffer);
			WMAddToPLArray(tab_pl, e);
			WMReleasePropList(e);
			snprintf(buffer, sizeof(buffer), "%d",
				 wwin->tab_group->active_index);
			e = WMCreatePLString(buffer);
			WMAddToPLArray(tab_pl, e);
			WMReleasePropList(e);
			WMPutInPLDictionary(win_state, sTabGroup, tab_pl);
			WMReleasePropList(tab_pl);
		}

		WMReleasePropList(name);
		WMReleasePropList(cmd);
		WMReleasePropList(workspace);
		WMReleasePropList(maximized);
		WMReleasePropList(geometry);
		WMReleasePropList(shortcut);
		if (wapp && wapp->app_icon && wapp->app_icon->dock) {
			int i;
			char *name = NULL;
			if (wapp->app_icon->dock == scr->dock)
				name = "Dock";

			/* Try the clips */
			if (name == NULL) {
				for (i = 0; i < scr->workspace_count; i++)
					if (scr->workspaces[i]->clip == wapp->app_icon->dock)
						break;
				if (i < scr->workspace_count)
					name = scr->workspaces[i]->name;
			}
			/* Try the drawers */
			if (name == NULL) {
				WDrawerChain *dc;
				for (dc = scr->drawers; dc != NULL; dc = dc->next) {
					if (dc->adrawer == wapp->app_icon->dock)
						break;
				}
				assert(dc != NULL);
				name = dc->adrawer->icon_array[0]->wm_instance;
			}
			dock = WMCreatePLString(name);
			WMPutInPLDictionary(win_state, sDock, dock);
			WMReleasePropList(dock);
		}
	} else {
		win_state = NULL;
	}

	if (instance)
		wunintern(instance);
	if (class)
		wunintern(class);
	if (command)
		wfree(command);

	return win_state;
}

void wSessionSaveState(WScreen * scr)
{
	WWindow *wwin = scr->focused_window;
	WMPropList *win_info, *wks;
	WMPropList *list = NULL;
	WMArray *wapp_list = NULL;

	make_keys();

	if (!scr->session_state) {
		scr->session_state = WMCreatePLDictionary(NULL, NULL);
		if (!scr->session_state)
			return;
	}

	list = WMCreatePLArray(NULL);

	wapp_list = WMCreateArray(16);

	while (wwin) {
		WApplication *wapp = wApplicationOf(wwin->main_window);
		xcb_window_t appId = wwin->orig_main_window;

		if ((wwin->transient_for == XCB_NONE || wwin->transient_for == wwin->screen_ptr->root_win)
		    && (WMGetFirstInArray(wapp_list, (void *)(uintptr_t) appId) == WANotFound
			|| WFLAGP(wwin, shared_appicon))
		    && !WFLAGP(wwin, dont_save_session)) {
			/* A entry for this application was not yet saved. Save one. */
			win_info = makeWindowState(wwin, wapp);
			if (win_info != NULL) {
				WMAddToPLArray(list, win_info);
				WMReleasePropList(win_info);
				/* If we were successful in saving the info for this window
				 * add the application the window belongs to, to the
				 * application list, so no multiple entries for the same
				 * application are saved.
				 */
				WMAddToArray(wapp_list, (void *)(uintptr_t) appId);
			}
		}
		wwin = wwin->prev;
	}
	WMRemoveFromPLDictionary(scr->session_state, sApplications);
	WMPutInPLDictionary(scr->session_state, sApplications, list);
	WMReleasePropList(list);

	wks = WMCreatePLString(scr->workspaces[scr->current_workspace]->name);
	WMPutInPLDictionary(scr->session_state, sWorkspace, wks);
	WMReleasePropList(wks);

	WMFreeArray(wapp_list);
}

void wSessionClearState(WScreen * scr)
{
	make_keys();

	if (!scr->session_state)
		return;

	WMRemoveFromPLDictionary(scr->session_state, sApplications);
	WMRemoveFromPLDictionary(scr->session_state, sWorkspace);
}

static void child_env(void *data)
{
	SetupEnvironment(data);
}

/* Match a saved app against an already-managed top-level window.
 * On a cold start after an exit that preserved clients (RestoreDesktop
 * reparents them to root), the previous instance is still alive and
 * has been adopted by manageAllWindows(). Spawning a second copy would
 * duplicate the app. The per-window _WINDOWMAKER_STATE hint already
 * restored workspace/shade/iconify/maximize state.
 */
static WWindow *findManagedWindow(WScreen *scr, const char *instance,
				  const char *class, const char *command)
{
	WWindow *wwin = scr->focused_window;

	while (wwin) {
		if (!wwin->flags.internal_window &&
		    (wwin->transient_for == XCB_NONE ||
		     wwin->transient_for == wwin->screen_ptr->root_win) &&
		    WMStrEqual(instance, wwin->wm_instance) &&
		    WMStrEqual(class, wwin->wm_class) &&
		    wWindowCommandsMatch(command, wwin->wm_command))
			return wwin;
		wwin = wwin->prev;
	}
	return NULL;
}

static pid_t execCommand(WScreen *scr, char *command)
{
	pid_t pid;
	char **argv = NULL;
	int argc;
	char *single_command;

	wtokensplit(command, &argv, &argc);

	if (!argc)
		return 0;

	/* Some clients report the complete command as one quoted WM_COMMAND
	 * element. Keep the original command for state matching, but split
	 * that element before spawning the replacement process. */
	if (argc == 1 && strchr(argv[0], ' ') != NULL &&
	    access(argv[0], X_OK) != 0) {
		single_command = wstrdup(argv[0]);
		wtokenfree(argv, argc);
		wtokensplit(single_command, &argv, &argc);
		wfree(single_command);
	}

	if (!argc)
		return 0;

	argv = wrealloc(argv, (argc + 1) * sizeof(*argv));
	argv[argc] = NULL;

	pid = wspawnvp_setup(argv, child_env, scr);
	wtokenfree(argv, argc);
	return pid;
}

static WSavedState *getWindowState(WScreen * scr, WMPropList * win_state)
{
	WSavedState *state = wmalloc(sizeof(WSavedState));
	WMPropList *value;
	char *tmp;
	unsigned mask;

	state->workspace = -1;
	value = WMGetFromPLDictionary(win_state, sWorkspace);
	if (value && WMIsPLString(value)) {
		tmp = WMGetFromPLString(value);
		state->workspace = wGetWorkspaceNumber(scr, tmp);
	}

	value = WMGetFromPLDictionary(win_state, sWorkspaces);
	if (value && WMIsPLArray(value)) {
		int n = WMGetPropListItemCount(value);
		int j;

		for (j = 0; j < n; j++) {
			WMPropList *e = WMGetFromPLArray(value, j);
			int w;

			if (!e || !WMIsPLString(e))
				continue;
			w = wWorkspaceFindByName(scr, WMGetFromPLString(e));
			if (w >= 0)
				state->ws_mask |= WS_BIT(w);
		}
	}

	value = WMGetFromPLDictionary(win_state, sShaded);
	if (value != NULL)
		state->shaded = WMPLGetBool(value);

	value = WMGetFromPLDictionary(win_state, sMiniaturized);
	if (value != NULL)
		state->miniaturized = WMPLGetBool(value);

	value = WMGetFromPLDictionary(win_state, sMaximized);
	if (value != NULL) {
		state->maximized = getHex(value);
	}

	value = WMGetFromPLDictionary(win_state, sHidden);
	if (value != NULL)
		state->hidden = WMPLGetBool(value);

	value = WMGetFromPLDictionary(win_state, sShortcutMask);
	if (value != NULL) {
		mask = getInt(value);
		state->window_shortcuts = mask;
	}

	value = WMGetFromPLDictionary(win_state, sMarkKey);
	if (value != NULL && WMIsPLString(value)) {
		char *s = WMGetFromPLString(value);
		if (s && *s)
			state->mark_key = wstrdup(s);
	}

	value = WMGetFromPLDictionary(win_state, sTabGroup);
	if (value && WMIsPLArray(value) && WMGetPropListItemCount(value) >= 3) {
		state->tab_group_gid = getInt(WMGetFromPLArray(value, 0));
		state->tab_group_idx = (int)getInt(WMGetFromPLArray(value, 1));
		state->tab_group_active = (int)getInt(WMGetFromPLArray(value, 2));
	}

	value = WMGetFromPLDictionary(win_state, sGeometry);
	if (value && WMIsPLString(value)) {
		if (!(sscanf(WMGetFromPLString(value), "%ux%u+%i+%i",
			     &state->w, &state->h, &state->x, &state->y) == 4 && (state->w > 0 && state->h > 0))) {
			state->w = 0;
			state->h = 0;
		}
	}

	return state;
}

void wSessionRestoreState(WScreen *scr)
{
	WSavedState *state;
	char *instance, *class, *command;
	WMPropList *win_info, *apps, *cmd, *value;
	pid_t pid;
	int i, count;
	WDock *dock;
	WAppIcon *btn = NULL;
	int j, n, found;
	char *tmp;

	make_keys();

	if (!scr->session_state)
		return;

	WMPLSetCaseSensitive(true);

	apps = WMGetFromPLDictionary(scr->session_state, sApplications);
	if (!apps)
		return;

	count = WMGetPropListItemCount(apps);
	if (count == 0)
		return;

	for (i = 0; i < count; i++) {
		win_info = WMGetFromPLArray(apps, i);

		cmd = WMGetFromPLDictionary(win_info, sCommand);
		if (!cmd || !WMIsPLString(cmd) || !(command = WMGetFromPLString(cmd))) {
			continue;
		}

		value = WMGetFromPLDictionary(win_info, sName);
		if (!value)
			continue;

		ParseWindowName(value, &instance, &class, "session");
		if (!instance && !class)
			continue;

		state = getWindowState(scr, win_info);

		dock = NULL;
		value = WMGetFromPLDictionary(win_info, sDock);
		if (value && WMIsPLString(value) && (tmp = WMGetFromPLString(value)) != NULL) {
			j = wWorkspaceFindByName(scr, tmp);
			if (!strcasecmp(tmp, "DOCK")) {
				dock = scr->dock;
			} else if (j >= 0) {
				dock = scr->workspaces[j]->clip;
			} else if (sscanf(tmp, "%i", &n) != 1) {
				/* Try the drawers */
				WDrawerChain *dc;

				for (dc = scr->drawers; dc != NULL; dc = dc->next)
				{
					if (strcmp(dc->adrawer->icon_array[0]->wm_instance, tmp) == 0)
					{
						dock = dc->adrawer;
						break;
					}
				}
			} else if (n == 0) {
				dock = scr->dock;
			} else if (n > 0 && n <= scr->workspace_count) {
				dock = scr->workspaces[n - 1]->clip;
			}
		}

		found = 0;
		if (dock != NULL) {
			for (j = 0; j < dock->max_icons; j++) {
				btn = dock->icon_array[j];
				if (btn && WMStrEqual(instance, btn->wm_instance) &&
				    WMStrEqual(class, btn->wm_class) &&
				    WMStrEqual(command, btn->command) &&
				    !btn->launching) {
					found = 1;
					break;
				}
			}
		}

		if (found) {
			wDockLaunchWithState(btn, state);
		} else if (findManagedWindow(scr, instance, class, command)) {
			/* Already running, state was restored from
			 * _WINDOWMAKER_STATE by manageAllWindows */
			if (state->mark_key)
				wfree(state->mark_key);
			wfree(state);
		} else if ((pid = execCommand(scr, command)) > 0) {
			wWindowAddSavedState(instance, class, command, pid, state);
		} else {
			if (state->mark_key)
				wfree(state->mark_key);
			wfree(state);
		}

		if (instance)
			wfree(instance);
		if (class)
			wfree(class);
	}
	/* clean up */
	WMPLSetCaseSensitive(false);
}

void wSessionRestoreLastWorkspace(WScreen * scr)
{
	WMPropList *wks;
	int w;
	char *value;

	make_keys();

	if (!scr->session_state)
		return;

	WMPLSetCaseSensitive(true);

	wks = WMGetFromPLDictionary(scr->session_state, sWorkspace);
	if (!wks || !WMIsPLString(wks))
		return;

	value = WMGetFromPLString(wks);

	if (!value)
		return;

	/* clean up */
	WMPLSetCaseSensitive(false);

	/* Get the workspace number for the workspace name */
	w = wGetWorkspaceNumber(scr, value);

	if (w >= 0 && w != scr->current_workspace && w < scr->workspace_count)
		wWorkspaceChange(scr, w);
}

/* Pre-load non-dock saved states into windowState so manageAllWindows can
 * match adopted windows. wSessionRestoreState runs after manageAllWindows,
 * so without this adopted windows miss their saved miniaturized state.
 * Unconsumed pid=0 entries are cleaned up before relaunching unmatched apps. */
void wSessionQueueSavedState(WScreen *scr)
{
	WMPropList *apps, *win_info, *cmd, *value;
	char *instance, *class, *command;
	WSavedState *state;
	int i, count;

	make_keys();

	if (!scr->session_state)
		return;

	WMPLSetCaseSensitive(true);

	apps = WMGetFromPLDictionary(scr->session_state, sApplications);
	if (!apps)
		goto cleanup;

	count = WMGetPropListItemCount(apps);
	for (i = 0; i < count; i++) {
		win_info = WMGetFromPLArray(apps, i);

		cmd = WMGetFromPLDictionary(win_info, sCommand);
		if (!cmd || !WMIsPLString(cmd))
			continue;
		command = WMGetFromPLString(cmd);
		if (!command)
			continue;

		value = WMGetFromPLDictionary(win_info, sName);
		if (!value)
			continue;

		ParseWindowName(value, &instance, &class, "session-queue");
		if (!instance && !class)
			continue;

		/* skip dock/clip/drawer entries (they have Dock = ...) */
		value = WMGetFromPLDictionary(win_info, sDock);
		if (value)
			goto next;

		state = getWindowState(scr, win_info);

		wWindowAddSavedState(instance, class, command, 0, state);
next:
		if (instance)
			wfree(instance);
		if (class)
			wfree(class);
	}

cleanup:
	WMPLSetCaseSensitive(false);
}

/* Remove unmatched (pid=0) saved states queued by wSessionQueueSavedState
 * but not consumed by manageAllWindows, so wSessionRestoreState does not
 * double-launch adopted apps. The saved-state list is process-wide, so
 * there is no per-screen argument. */
void wSessionCleanupUnmatchedSavedState(void)
{
	wWindowDeleteUnmatchedSavedStates();
}
