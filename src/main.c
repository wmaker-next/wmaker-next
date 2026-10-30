/*
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

#ifdef HAVE_INOTIFY
#include <sys/inotify.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>


#include <locale.h>

/*
 * When built with --enable-asan, suppress LeakSanitizer reports for
 * process-lifetime allocations inside third-party libraries that we
 * cannot release (fontconfig keeps its FcInit configuration alive,
 * FcFini cannot run while a Pango font map is loaded, and pangocairo/
 * glib keep type-system bookkeeping).
 * This makes a plain `./src/wmaker` ASan run report only our own leaks.
 */
#if defined(__SANITIZE_ADDRESS__)
# define WMAKER_ASAN 1
#elif defined(__has_feature)
# if __has_feature(address_sanitizer)
#  define WMAKER_ASAN 1
# endif
#endif
#ifdef WMAKER_ASAN
const char *__lsan_default_suppressions(void);
const char *__lsan_default_suppressions(void)
{
	return "leak:libfontconfig.so\n"
	       "leak:libpangocairo-1.0.so\n"
	       "leak:libcairo.so\n"
	       "leak:libglib-2.0.so\n"
	       "leak:libgobject-2.0.so\n";
}
#endif

#define MAINFILE

#include "WindowMaker.h"
#include "window.h"
#include "defaults.h"
#include "event.h"
#include "ipc.h"
#include "startup.h"
#include "wdisplay.h"
#include "menu.h"
#include "keybind.h"
#include "wxkb.h"
#include "session.h"
#include "shutdown.h"
#include "desktopfile.h"
#include "dialog.h"
#include "main.h"
#include "monitor.h"
#include "misc.h"
#include "framewin.h"
#include "startup_notify.h"
#include "xsmp.h"
#include "wsound.h"

#include <WINGs/WUtil.h>

/****** Global Variables ******/
struct wmaker_global_variables w_global;

/* general info */

char *ProgName;

WShortKey wKeyBindings[WKBD_LAST];

/* notifications */
const char WMNManaged[] = "WMNManaged";
const char WMNUnmanaged[] = "WMNUnmanaged";
const char WMNChangedWorkspace[] = "WMNChangedWorkspace";
const char WMNChangedState[] = "WMNChangedState";
const char WMNChangedFocus[] = "WMNChangedFocus";
const char WMNChangedStacking[] = "WMNChangedStacking";
const char WMNChangedName[] = "WMNChangedName";

const char WMNWorkspaceCreated[] = "WMNWorkspaceCreated";
const char WMNWorkspaceDestroyed[] = "WMNWorkspaceDestroyed";
const char WMNWorkspaceChanged[] = "WMNWorkspaceChanged";
const char WMNWorkspaceNameChanged[] = "WMNWorkspaceNameChanged";
const char WMNWorkspaceLayoutChanged[] = "WMNWorkspaceLayoutChanged";

const char WMNResetStacking[] = "WMNResetStacking";

/******** End Global Variables *****/

static char *DisplayName = NULL;

static char **Arguments;

static int ArgCount;

static bool multiHead = true;

static int *wVisualID = NULL;
static int wVisualID_len = 0;

static int real_main(int argc, char **argv);

/* Defer the startup chime into the event loop. The WIPC socket is bound
 * in StartUp, but a subscriber must connect and SUBSCRIBE before it
 * receives anything, which only happens once EventLoop runs. */
#define STARTUP_SOUND_DELAY_MS 250

static void play_startup_sound(void *data)
{
	int i;
	WScreen *scr;

	(void)data;
	for (i = 0; i < wdisp->screen_count; i++) {
		scr = wScreenWithNumber(i);
		if (scr)
			wSoundPlay(scr, WSOUND_STARTUP);
	}
}

int getWVisualID(int screen)
{
        if (wVisualID == NULL)
		return -1;
        if (screen < 0 || screen >= wVisualID_len)
		return -1;

        return wVisualID[screen];
}

static void setWVisualID(int screen, int val)
{
        int i;

        if (screen < 0)
		return;

        if (wVisualID == NULL) {
		/* no array at all, alloc space for screen + 1 entries
		 * and init with default value */
		wVisualID_len = screen + 1;
		wVisualID = (int *)wmalloc(wVisualID_len * sizeof(int));
		for (i = 0; i < wVisualID_len; i++) {
			wVisualID[i] = -1;
		}
	} else if (screen >= wVisualID_len) {
		/* larger screen number than previously allocated
		 so enlarge array */
		int oldlen = wVisualID_len;

		wVisualID_len = screen + 1;
		wVisualID = (int *)wrealloc(wVisualID, wVisualID_len * sizeof(int));
		for (i = oldlen; i < wVisualID_len; i++) {
			wVisualID[i] = -1;
		}
	}

	wVisualID[screen] = val;
}

/*
 * this function splits a given string at the comma into tokens
 * and set the wVisualID variable to each parsed number
 */
static int initWVisualID(const char *user_str)
{
	char *mystr = strdup(user_str);
	int cur_in_pos = 0;
	int cur_out_pos = 0;
	int cur_screen = 0;
	int error_found = 0;

	if (!mystr)
		return 1;

	for (;;) {
		/* check for delimiter */
		if (user_str[cur_in_pos] == '\0' || user_str[cur_in_pos] == ',') {
			int v;

			mystr[cur_out_pos] = '\0';

			if (sscanf(mystr, "%i", &v) != 1) {
				error_found = 1;
				break;
			}

			setWVisualID(cur_screen, v);

			cur_screen++;
			cur_out_pos = 0;
		}

		/* break in case last char has been consumed */
		if (user_str[cur_in_pos] == '\0')
			break;

                /* if the current char is no delimiter put it into mystr */
		if (user_str[cur_in_pos] != ',') {
			mystr[cur_out_pos++] = user_str[cur_in_pos];
		}
		cur_in_pos++;
	}

	free(mystr);

	if (cur_screen == 0||error_found != 0)
		return 1;

	return 0;
}

noreturn void Exit(int status)
{
	if (wdisp && wdisp->conn) {
		xcb_disconnect(wdisp->conn);
		wdisp->conn = NULL;
	}

	wXDGAppListInvalidate();
	wXDGMimeInvalidate();
	wXDGDocListInvalidate();
	RShutdown(); /* wrlib clean exit */
	wutil_shutdown();  /* WUtil clean-up */

	/* last, so wAbort can still reach wdisp during teardown */
	wDisplayDestroy();

	exit(status);
}

void Restart(char *manager, bool abortOnFailure)
{
	char *prog = NULL;
	char *argv[MAX_RESTART_ARGS];
	xcb_connection_t *conn;
	int i;

	if (manager && manager[0] != 0) {
		prog = argv[0] = strtok(manager, " ");
		for (i = 1; i < MAX_RESTART_ARGS; i++) {
			argv[i] = strtok(NULL, " ");
			if (argv[i] == NULL) {
				break;
			}
		}
	}
	if (wdisp && wdisp->conn) {
		conn = wdisp->conn;
		wDisplayDestroy();
		xcb_disconnect(conn);
	}
	if (!prog) {
		execvp(Arguments[0], Arguments);
		wfatal(_("failed to restart Window Maker."));
	} else {
		execvp(prog, argv);
		werror(_("could not exec %s"), prog);
	}
	if (abortOnFailure)
		exit(7);
}

void SetupEnvironment(WScreen * scr)
{
	char *tmp, *ptr;
	char buf[16];

	if (multiHead) {
		int len = strlen(DisplayName) + 64;
		tmp = wmalloc(len);
		snprintf(tmp, len, "DISPLAY=%s", DisplayName);

		/* Search from the end to be compatible with ipv6 address */
		ptr = strrchr(tmp, ':');
		if (ptr == NULL) {
			static bool message_already_displayed = false;

			if (!message_already_displayed)
				wwarning(_("the display name has an unexpected syntax: \"%s\""),
				         DisplayName);
			message_already_displayed = true;
		} else {
			/* If found, remove the screen specification from the display variable */
			ptr = strchr(ptr, '.');
			if (ptr)
				*ptr = 0;
		}
		snprintf(buf, sizeof(buf), ".%i", scr->screen);
		if (wstrlcat(tmp, buf, len) >= (size_t)len)
			wwarning(_("the display name is too long: \"%s\""), DisplayName);
		putenv(tmp);
	}

	/* the IPC socket exists on single-head too, so this export is
	 * unconditional, children launched from the dock or root menu
	 * inherit the socket for the screen that launched them */
	if (scr->ipc_path)
		setenv("WMAKER_IPC_SOCKET", scr->ipc_path, 1);
}

typedef struct {
	WScreen *scr;
	char *command;
} _tuple;

static void shellCommandHandler(pid_t pid, unsigned int status, void *client_data)
{
	_tuple *data = (_tuple *) client_data;

	wStartupLaunchChildExited(data->scr, pid);

	if (status == 127) {
		char *buffer;

		buffer = wstrconcat(_("Could not execute command: "), data->command);

		wMessageDialog(data->scr, _("Error"), buffer, _("OK"), NULL, NULL);
		wfree(buffer);
	} else if (status != 127) {
		/*
		   printf("%s: %i\n", data->command, status);
		 */
	}

	wfree(data->command);
	wfree(data);
}

void LaunchChildSetup(void *data)
{
	struct LaunchSetup *s = data;
	int fd;

	SetupEnvironment(s->scr);
#ifdef HAVE_SETSID
	setsid();
#endif

	/* a chatty child must not block on the WM's undrained stderr */
	fd = open("/dev/null", O_RDWR);
	if (fd >= 0) {
		dup2(fd, STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		if (fd > STDERR_FILENO)
			close(fd);
	}

	wStartupLaunchSetupChild(s->sn);
}

void ExecuteShellCommandWithStartup(WScreen *scr, const char *command, bool startup_notify)
{
	static char *shell = NULL;
	struct LaunchSetup ls;
	pid_t pid;

	/*
	 * This have a problem: if the shell is tcsh (not sure about others)
	 * and ~/.tcshrc have /bin/stty erase ^H somewhere on it, the shell
	 * will block and the command will not be executed.
	 if (!shell) {
	 shell = getenv("SHELL");
	 if (!shell)
	 shell = "/bin/sh";
	 }
	 */
	shell = "/bin/sh";

	ls.scr = scr;
	ls.sn = startup_notify ? wStartupLaunchBegin(scr, command, command, NULL,
						     scr->current_workspace, NULL) : NULL;

	pid = wspawnvp_setup((char *[]){ shell, "-c", (char *)command, NULL },
			     LaunchChildSetup, &ls);

	if (pid < 0) {
		werror("cannot fork a new process");
		wStartupLaunchAbort(ls.sn);
	} else {
		_tuple *data = wmalloc(sizeof(_tuple));

		data->scr = scr;
		data->command = wstrdup(command);

		wAddDeathHandler(pid, shellCommandHandler, data, "menu", -1);
		wStartupLaunchSpawned(ls.sn, pid);
	}
}

void ExecuteShellCommand(WScreen *scr, const char *command)
{
	ExecuteShellCommandWithStartup(scr, command, true);
}

/*
 *---------------------------------------------------------------------
 * RelaunchWindow--
 * 	Launch a new instance of the active window
 *
 *----------------------------------------------------------------------
 */
bool RelaunchWindow(WWindow *wwin)
{
	if (! wwin || ! wwin->client_win) {
		werror("no window to relaunch");
		return false;
	}

	char **argv = NULL;
	int argc;
	char *command = NULL;

	command = GetCommandForWindow(wwin->client_win);
	if (!command) {
#ifdef USE_XRES
		werror("cannot relaunch the application because no associated process found");
#else
		werror("cannot relaunch the application because no WM_COMMAND property is set");
#endif
		return false;
	} else
		wtokensplit(command, &argv, &argc);

	if (!argc) {
		wfree(command);
		return false;
	}

	argv = wrealloc(argv, (argc + 1) * sizeof(*argv));
	argv[argc] = NULL;

	struct LaunchSetup ls;
	ls.scr = wwin->screen_ptr;
	ls.sn = wStartupLaunchBegin(wwin->screen_ptr, argv[0], argv[0], wwin->wm_class,
				    wwin->frame ? WWIN_PRIMARY_WS(wwin)
						: wwin->screen_ptr->current_workspace,
				    NULL);

	pid_t pid = wspawnvp_setup(argv, LaunchChildSetup, &ls);

	if (pid < 0) {
		werror("cannot fork a new process");
		wStartupLaunchAbort(ls.sn);

		wtokenfree(argv, argc);
		wfree(command);
		return false;
	} else {
		_tuple *data = wmalloc(sizeof(_tuple));

		data->scr = wwin->screen_ptr;
		data->command = command;
		wStartupLaunchSpawned(ls.sn, pid);

		/* not actually a shell command */
		wAddDeathHandler(pid, shellCommandHandler, data, "menu", -1);

		wtokenfree(argv, argc);
	}

	return true;
}

/*
 *---------------------------------------------------------------------
 * wAbort--
 * 	Do a major cleanup and exit the program
 *
 *----------------------------------------------------------------------
 */
noreturn void wAbort(int dumpCore)
{
	int i;
	WScreen *scr;

	for (i = 0; wdisp && i < wdisp->screen_count; i++) {
		scr = wScreenWithNumber(i);
		if (scr)
			RestoreDesktop(scr);
	}
	printf(_("%s aborted.\n"), ProgName);
	if (dumpCore)
		abort();
	else
		exit(1);
}

static void print_help(void)
{
	printf(_("Usage: %s [options]\n"), ProgName);
	puts(_("The Window Maker window manager for the X window system"));
	puts("");
	puts(_(" -display host:dpy	display to use"));
#ifdef USE_ICCCM_WMREPLACE
	puts(_(" --replace		replace running window manager"));
#endif
	puts(_(" --no-dock		do not open the application Dock"));
	puts(_(" --no-clip		do not open the workspace Clip"));
	puts(_(" --no-autolaunch	do not autolaunch applications"));
	puts(_(" --no-drawer		disable drawers in the dock"));
	puts(_(" --dont-restore		do not restore saved session"));
	puts(_(" --clientid id		session management client ID"));

	puts(_(" --locale locale	locale to use"));

	puts(_(" --visual-id visualid	visual id of visual to use"));
	puts(_(" --static		do not update or save configurations"));
#ifndef HAVE_INOTIFY
	puts(_(" --no-polling		do not periodically check for configuration updates"));
#endif
	puts(_(" --global_defaults_path	print the path for default config and exit"));
	puts(_(" --get-socketpath	print the IPC socket path for $DISPLAY and exit"));
	puts(_(" --version		print version and exit"));
	puts(_(" --help			show this message"));
}

static void check_defaults(void)
{
	char *path;

	path = wdefaultspathfordomain("WindowMaker");

	if (access(path, R_OK) != 0) {
		wwarning(_("could not find user configuration directory (%s)."), path);

		if (wspawnvp_wait((char *[]){ "wmaker.inst", "--batch", NULL }) != 0) {
			wwarning(_("There was an error while creating the configuration directory, please "
				   "make sure you have installed Window Maker correctly and run wmaker.inst"));
		} else {
			wwarning(_("%s directory created with default configuration."), path);
		}
	}

	wfree(path);
}

#ifdef HAVE_INOTIFY
/*
 * Add watch here, used to notify if configuration
 * files have changed, using linux kernel inotify mechanism
 */

static void inotifyWatchConfig(void)
{
	char *watchPath = NULL;

	w_global.inotify.fd_event_queue = inotify_init();	/* Initialise an inotify instance */
	if (w_global.inotify.fd_event_queue < 0) {
		wwarning(_("could not initialise an inotify instance."
			   " Changes to the defaults database will require"
			   " a restart to take effect. Check your kernel!"));
	} else {
		watchPath = wdefaultspathfordomain("");
		/* Add the watch; really we are only looking for modify events
		 * but we might want more in the future so check all events for now.
		 * The individual events are checked for in event.c.
		 */
		w_global.inotify.wd_defaults = inotify_add_watch(w_global.inotify.fd_event_queue, watchPath, IN_ALL_EVENTS);
		if (w_global.inotify.wd_defaults < 0) {
			wwarning(_("could not add an inotify watch on path %s."
				   " Changes to the defaults database will require"
				   " a restart to take effect."), watchPath);
			wInotifyCleanup();
		}
	}
	wfree(watchPath);
	/* the run-dialog document index registers a watch per root
	 * through these hooks so a deep create or delete invalidates
	 * the cache without the root-mtime heuristic, the hooks stay
	 * NULL on platforms without inotify and the index falls back
	 * to the mtime check */
	wXDGSetDocWatchHooks(wInotifyAddWatch, wInotifyRemoveWatch);
}
#endif /* HAVE_INOTIFY */

static void execInitScript(void)
{
	char *file;

	file = wfindfile(DEF_CONFIG_PATHS, DEF_INIT_SCRIPT);

	if (file) {
		/* run the script directly (its shebang picks the interpreter)
		 * rather than handing the path to /bin/sh -c */
		if (wspawnvp_wait((char *[]){ file, NULL }) != 0)
			werror(_("%s:could not execute initialization script"), file);

		wfree(file);
	}
}

void ExecExitScript(void)
{
	char *file;

	file = wfindfile(DEF_CONFIG_PATHS, DEF_EXIT_SCRIPT);

	if (file) {
		if (wspawnvp_wait((char *[]){ file, NULL }) != 0)
			werror(_("%s:could not execute exit script"), file);

		wfree(file);
	}
}

int main(int argc, char **argv)
{
	int i_am_the_monitor, i;

	memset(&w_global, 0, sizeof(w_global));
	w_global.program.state = WSTATE_NORMAL;
	w_global.program.signal_state = WSTATE_NORMAL;
	w_global.ignore_workspace_change = false;
	w_global.restore_focus_repair = false;
	w_global.shortcut.modifiers_mask = 0xff;
#ifdef HAVE_INOTIFY
	w_global.inotify.fd_event_queue = -1;
	w_global.inotify.wd_defaults = -1;
#endif

	/* setup common stuff for the monitor and wmaker itself */
	WMInitializeApplication("WindowMaker", &argc, argv);

	i_am_the_monitor = 1;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--get-socketpath") == 0) {
			char *p = wIPCSocketPath(DisplayName);
			printf("%s\n", p);
			wfree(p);
			exit(0);
		} else if (strncmp(argv[i], "--for-real", strlen("--for-real")) == 0) {
			i_am_the_monitor = 0;
			break;
		} else if (strcmp(argv[i], "-display") == 0 || strcmp(argv[i], "--display") == 0) {
			i++;
			if (i >= argc) {
				wwarning(_("too few arguments for %s"), argv[i - 1]);
				exit(0);
			}
			DisplayName = argv[i];
		}
	}

	/* resolve a missing -display argument against the environment,
	 * the client-side half of the XDisplayName() contract */
	if (!DisplayName) {
		const char *env = getenv("DISPLAY");

		DisplayName = wstrdup(env ? env : "");
	}
	w_global.display_name = DisplayName;
	setenv("DISPLAY", DisplayName, 1);

	/* Let xdg-open, portal backends and toolkit theme pickers identify the
	 * desktop, but do not overwrite a value supplied by the session manager. */
	setenv("XDG_CURRENT_DESKTOP", "WindowMaker", 0);

	if (i_am_the_monitor)
		return MonitorLoop(argc, argv);
	else
		return real_main(argc, argv);
}

static int real_main(int argc, char **argv)
{
	int i;
	char *pos;
	int d, s;
	int dflt;
	xcb_connection_t *conn;
	xcb_screen_t *xscr;
	const char *sm_client_id = NULL;

	setlocale(LC_ALL, "");
	wsetabort(wAbort);

	/* for telling WPrefs what's the name of the wmaker binary being ran */
	setenv("WMAKER_BIN_NAME", argv[0], 1);

	ArgCount = argc;
	Arguments = wmalloc(sizeof(char *) * (ArgCount + 1));
	for (i = 0; i < argc; i++) {
		Arguments[i] = argv[i];
	}
	/* add the extra option to signal that we're just restarting wmaker */
	Arguments[argc - 1] = "--for-real=";
	Arguments[argc] = NULL;

	ProgName = strrchr(argv[0], '/');
	if (!ProgName)
		ProgName = argv[0];
	else
		ProgName++;

	wDisplayCreate();

	if (argc > 1) {
		for (i = 1; i < argc; i++) {
			if (strcmp(argv[i], "-nocpp") == 0 || strcmp(argv[i], "--no-cpp") == 0) {
				wwarning(_("option \"%s\" is deprecated, please remove it from your script"), argv[i]);
			} else if (strcmp(argv[i], "--for-real") == 0) {
				wdisp->flags.restarting = 0;
			} else if (strcmp(argv[i], "--for-real=") == 0) {
				wdisp->flags.restarting = 1;
			} else if (strcmp(argv[i], "--for-real-") == 0) {
				wdisp->flags.restarting = 2;
			} else if (strcmp(argv[i], "-no-autolaunch") == 0
				   || strcmp(argv[i], "--no-autolaunch") == 0) {
				wdisp->flags.noautolaunch = 1;
			} else if (strcmp(argv[i], "-dont-restore") == 0 || strcmp(argv[i], "--dont-restore") == 0) {
				wdisp->flags.norestore = 1;
			} else if (strcmp(argv[i], "-nodock") == 0 || strcmp(argv[i], "--no-dock") == 0) {
				wdisp->flags.nodock = 1;
				wdisp->flags.nodrawer = 1;
			} else if (strcmp(argv[i], "-noclip") == 0 || strcmp(argv[i], "--no-clip") == 0) {
				wdisp->flags.noclip = 1;
			} else if (strcmp(argv[i], "-nodrawer") == 0 || strcmp(argv[i], "--no-drawer") == 0) {
				wdisp->flags.nodrawer = 1;
#ifdef USE_ICCCM_WMREPLACE
			} else if (strcmp(argv[i], "-replace") == 0 || strcmp(argv[i], "--replace") == 0) {
				wdisp->flags.replace = 1;
#endif
			} else if (strcmp(argv[i], "-version") == 0 || strcmp(argv[i], "--version") == 0) {
				printf("Window Maker %s\n", VERSION);
				exit(0);
			} else if (strcmp(argv[i], "--global_defaults_path") == 0) {
				printf("%s\n", PKGCONFDIR);
				exit(0);
			} else if (strcmp(argv[i], "-locale") == 0 || strcmp(argv[i], "--locale") == 0) {
				i++;
				if (i >= argc) {
					wwarning(_("too few arguments for %s"), argv[i - 1]);
					exit(0);
				}
				w_global.locale = argv[i];
			} else if (strcmp(argv[i], "-display") == 0 || strcmp(argv[i], "--display") == 0) {
				i++;
				if (i >= argc) {
					wwarning(_("too few arguments for %s"), argv[i - 1]);
					exit(0);
				}
				DisplayName = argv[i];
			} else if (strcmp(argv[i], "-visualid") == 0 || strcmp(argv[i], "--visual-id") == 0) {
				i++;
				if (i >= argc) {
					wwarning(_("too few arguments for %s"), argv[i - 1]);
					exit(0);
				}
				if (initWVisualID(argv[i]) != 0) {
					wwarning(_("bad value for visualid: \"%s\""), argv[i]);
					exit(0);
				}
			} else if (strcmp(argv[i], "-clientid") == 0 || strcmp(argv[i], "--clientid") == 0) {
				i++;
				if (i >= argc) {
					wwarning(_("too few arguments for %s"), argv[i - 1]);
					exit(0);
				}
				sm_client_id = argv[i];
			} else if (strcmp(argv[i], "-static") == 0 || strcmp(argv[i], "--static") == 0) {
				wdisp->flags.noupdates = 1;
			} else if (strcmp(argv[i], "--no-polling") == 0) {
#ifndef HAVE_INOTIFY
				wdisp->flags.noupdates = 1;
#else
				wmessage(_("your version of Window Maker was compiled with INotify support, so \"--no-polling\" has no effect"));
#endif
			} else if (strcmp(argv[i], "--help") == 0) {
				print_help();
				exit(0);
			} else {
				printf(_("%s: invalid argument '%s'\n"), argv[0], argv[i]);
				printf(_("Try '%s --help' for more information\n"), argv[0]);
				exit(1);
			}
		}
	}

	if (!wdisp->flags.noupdates) {
		/* check existence of Defaults DB directory */
		check_defaults();
	}

	if (w_global.locale) {
		setenv("LANG", w_global.locale, 1);
	} else {
		w_global.locale = getenv("LC_ALL");
		if (!w_global.locale) {
			w_global.locale = getenv("LANG");
		}
	}

	setlocale(LC_ALL, "");

	if (!w_global.locale || strcmp(w_global.locale, "C") == 0 || strcmp(w_global.locale, "POSIX") == 0)
		w_global.locale = NULL;
#ifdef I18N
	if (getenv("NLSPATH")) {
		bindtextdomain("WindowMaker", getenv("NLSPATH"));
#if defined(MENU_TEXTDOMAIN)
		bindtextdomain(MENU_TEXTDOMAIN, getenv("NLSPATH"));
#endif
	} else {
		bindtextdomain("WindowMaker", LOCALEDIR);
#if defined(MENU_TEXTDOMAIN)
		bindtextdomain(MENU_TEXTDOMAIN, LOCALEDIR);
#endif
	}
	bind_textdomain_codeset("WindowMaker", "UTF-8");
#if defined(MENU_TEXTDOMAIN)
	bind_textdomain_codeset(MENU_TEXTDOMAIN, "UTF-8");
#endif
	textdomain("WindowMaker");
#endif

	if (w_global.locale) {
		char *locale, *ptr;

		/* w_global.locale is const-typed, truncate at the
		 * codeset suffix on a writable copy and adopt that */
		locale = wstrdup(w_global.locale);
		ptr = strchr(locale, '.');
		if (ptr)
			*ptr = 0;
		w_global.locale = locale;
	}

	/* open display; an empty resolved name means DISPLAY was unset,
	 * xcb_connect("") would silently target :0 so keep the open
	 * failure contract */
	if (DisplayName[0] == '\0') {
		wfatal(_("could not open display \"%s\""), DisplayName);
		exit(1);
	}

	conn = xcb_connect(DisplayName, &dflt);
	if (xcb_connection_has_error(conn)) {
		wfatal(_("could not open display \"%s\""), DisplayName);
		exit(1);
	}

	wDisplayAttach(conn, dflt);

	if (fcntl(xcb_get_file_descriptor(conn), F_SETFD, FD_CLOEXEC) < 0) {
		werror("error setting close-on-exec flag for X connection");
		exit(1);
	}

	if (getWVisualID(0) < 0) {
		/*
		 *   If unspecified, use default visual instead of waiting
		 * for wrlib/context.c:bestContext() that may end up choosing
		 * the "fake" 24 bits added by the Composite extension.
		 *   This is required to avoid all sort of corruptions when
		 * composite is enabled, and at a depth other than 24.
		 */
		xscr = wXScreenOf(conn, dflt);
		setWVisualID(0, (int)xscr->root_visual);
        }

	/* check if the user specified a complete display name (with screen).
	 * If so, only manage the specified screen */
	if (DisplayName)
		pos = strchr(DisplayName, ':');
	else
		pos = NULL;

	if (pos && sscanf(pos, ":%i.%i", &d, &s) == 2)
		multiHead = false;

	StartUp(!multiHead);

	if (wdisp->screen_count == 1)
		multiHead = false;

	wXSMPConnect(argc, argv, sm_client_id);

	execInitScript();
#ifdef HAVE_INOTIFY
	inotifyWatchConfig();
#endif
	WMAddTimerHandler(STARTUP_SOUND_DELAY_MS, play_startup_sound, NULL);
	EventLoop();
	return -1;
}
