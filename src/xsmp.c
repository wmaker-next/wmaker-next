/*  xsmp.c - X Session Management Protocol client
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 2026 Window Maker Team
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
#include <string.h>

#include <WINGs/WUtil.h>

#include "xsmp.h"

char **wXSMPFilterArgv(int argc, char **argv, int *out_argc)
{
	char **out;
	int i, n;

	out = wmalloc(sizeof(char *) * (argc + 1));
	n = 0;
	for (i = 0; i < argc; i++) {
		if (strncmp(argv[i], "--for-real", strlen("--for-real")) == 0)
			continue;
		if (strcmp(argv[i], "-clientid") == 0 ||
		    strcmp(argv[i], "--clientid") == 0) {
			if (i + 1 < argc)
				i++;
			continue;
		}
		out[n++] = wstrdup(argv[i]);
	}
	out[n] = NULL;
	*out_argc = n;
	return out;
}

#ifdef USE_XSMP

#include <unistd.h>
#include <pwd.h>
#include <fcntl.h>
#include <sys/types.h>

#include <X11/SM/SMlib.h>
#include <X11/ICE/ICElib.h>

#include "WindowMaker.h"
#include "screen.h"
#include "window.h"
#include "session.h"
#include "shutdown.h"


static SmcConn sm_conn;
static IceConn sm_ice_conn;
static WMHandlerID ice_input;
static char *client_id;

/* Filtered copy of the launch argv used for both SmCloneCommand (as-is)
 * and SmRestartCommand (with -clientid ID appended). --for-real* is
 * dropped so the session manager relaunches the monitor process rather
 * than the managed child, and any incoming -clientid pair is dropped so
 * the freshly assigned id is the only one on the restart line. */
static int base_argc;
static char **base_argv;


static void ice_io_error(IceConn conn)
{
	(void)conn;
	/* IceProcessMessages() below returns IceProcessMessagesIOError right
	 * after this fires, teardown happens there so the handler stays a
	 * no-op and just stops libICE from calling exit(). */
}

static void ice_input_cb(int fd, int mask, void *data)
{
	IceProcessMessagesStatus st;

	(void)fd;
	(void)mask;
	(void)data;

	if (!sm_ice_conn)
		return;

	st = IceProcessMessages(sm_ice_conn, NULL, NULL);
	if (st == IceProcessMessagesIOError) {
		wwarning(_("lost connection to the session manager"));
		if (ice_input) {
			WMDeleteInputHandler(ice_input);
			ice_input = NULL;
		}
		SmcCloseConnection(sm_conn, 0, NULL);
		sm_conn = NULL;
		sm_ice_conn = NULL;
	}
}

/* ice_watch and cb_save_yourself sit on libICE/libSM ABI function
 * pointers, so their flag parameters must spell X11's Bool (int),
 * not C99 bool. */
static void ice_watch(IceConn conn, IcePointer data, Bool opening,
		      IcePointer *watch_data)
{
	int fd;

	(void)data;

	if (opening) {
		fd = IceConnectionNumber(conn);
		fcntl(fd, F_SETFD, FD_CLOEXEC);
		*watch_data = WMAddInputHandler(fd, WIReadMask, ice_input_cb, NULL);
		ice_input = *watch_data;
		sm_ice_conn = conn;
	} else {
		if (*watch_data) {
			WMDeleteInputHandler(*watch_data);
			if (ice_input == *watch_data)
				ice_input = NULL;
		}
		if (sm_ice_conn == conn)
			sm_ice_conn = NULL;
	}
}

static void set_restart_style(char hint)
{
	SmPropValue val;
	SmProp prop, *props[1];

	if (!sm_conn)
		return;

	val.length = 1;
	val.value = &hint;
	prop.name = SmRestartStyleHint;
	prop.type = SmCARD8;
	prop.num_vals = 1;
	prop.vals = &val;
	props[0] = &prop;
	SmcSetProperties(sm_conn, 1, props);
}

static void set_list_prop(const char *name, int argc, char **argv)
{
	SmPropValue *vals;
	SmProp prop, *props[1];
	int i;

	vals = wmalloc(sizeof(SmPropValue) * (argc > 0 ? argc : 1));
	for (i = 0; i < argc; i++) {
		vals[i].length = strlen(argv[i]);
		vals[i].value = argv[i];
	}
	prop.name = (char *)name;
	prop.type = SmLISTofARRAY8;
	prop.num_vals = argc;
	prop.vals = vals;
	props[0] = &prop;
	SmcSetProperties(sm_conn, 1, props);
	wfree(vals);
}

static void set_string_prop(const char *name, const char *value)
{
	SmPropValue val;
	SmProp prop, *props[1];

	val.length = strlen(value);
	val.value = (char *)value;
	prop.name = (char *)name;
	prop.type = SmARRAY8;
	prop.num_vals = 1;
	prop.vals = &val;
	props[0] = &prop;
	SmcSetProperties(sm_conn, 1, props);
}

static void publish_properties(void)
{
	struct passwd *pw;
	char **restart;
	char *cwd;
	int i;

	set_string_prop(SmProgram, base_argv[0]);

	pw = getpwuid(getuid());
	if (pw && pw->pw_name)
		set_string_prop(SmUserID, pw->pw_name);

	cwd = getcwd(NULL, 0);
	if (cwd) {
		set_string_prop(SmCurrentDirectory, cwd);
		free(cwd);
	}

	set_list_prop(SmCloneCommand, base_argc, base_argv);

	restart = wmalloc(sizeof(char *) * (base_argc + 2));
	for (i = 0; i < base_argc; i++)
		restart[i] = base_argv[i];
	restart[base_argc] = "-clientid";
	restart[base_argc + 1] = client_id;
	set_list_prop(SmRestartCommand, base_argc + 2, restart);
	wfree(restart);

	/* MonitorLoop() already relaunches the managed child on crash, so
	 * SmRestartImmediately would race the monitor and start a second
	 * top-level instance. SmRestartIfRunning is the protocol default
	 * but set it explicitly so wXSMPDisconnect() dropping to the same
	 * value is a visible no-op rather than relying on SM defaults. */
	set_restart_style(SmRestartIfRunning);
}

static void save_phase2(SmcConn conn, SmPointer data)
{
	WScreen *scr;
	int i;

	(void)data;

	/* Phase 2 runs after every other client has answered its own
	 * SaveYourself, so the managed-window list is authoritative and
	 * wSessionSaveState() records the full set. Mirror the root-menu
	 * SAVE_SESSION path: snapshot Applications explicitly when the
	 * user has SaveSessionOnExit off, then let wScreenSaveState()
	 * write WMState (it re-snapshots itself when the option is on). */
	for (i = 0; i < w_global.screen_count; i++) {
		scr = wScreenWithNumber(i);
		if (!scr)
			continue;
		if (!w_global.config->save_session_on_exit)
			wSessionSaveState(scr);
		wScreenSaveState(scr, false);
	}

	/* SmRestartCommand carries the client id, and WMState is a fixed
	 * per-user path, so nothing about the restart line changes across
	 * checkpoints and the initial publish_properties() stays valid. */
	SmcSaveYourselfDone(conn, true);
}

static void cb_save_yourself(SmcConn conn, SmPointer data, int save_type,
			     Bool shutdown, int interact_style, Bool fast)
{
	(void)data;
	(void)save_type;
	(void)shutdown;
	(void)interact_style;
	(void)fast;

	/* The window manager wants the phase-2 slot so it snapshots after
	 * clients have quiesced. If the request is refused fall back to an
	 * immediate save so the session manager is not left waiting. */
	if (!SmcRequestSaveYourselfPhase2(conn, save_phase2, NULL))
		save_phase2(conn, NULL);
}

static void cb_die(SmcConn conn, SmPointer data)
{
	(void)conn;
	(void)data;

	/* external=True: the client set was checkpointed in phase 2 and
	 * peers may already be exiting, carry the on-disk Applications
	 * forward rather than re-snapshotting a draining window list. */
	Shutdown(WSExitMode, true);
}

static void cb_save_complete(SmcConn conn, SmPointer data)
{
	(void)conn;
	(void)data;
}

static void cb_shutdown_cancelled(SmcConn conn, SmPointer data)
{
	(void)conn;
	(void)data;
}

void wXSMPConnect(int argc, char **argv, const char *previous_id)
{
	SmcCallbacks cb;
	char errbuf[256];
	char *id_ret = NULL;

	if (getenv("SESSION_MANAGER") == NULL)
		return;

	base_argv = wXSMPFilterArgv(argc, argv, &base_argc);

	IceSetIOErrorHandler(ice_io_error);
	if (IceAddConnectionWatch(ice_watch, NULL) == 0) {
		wwarning(_("could not register ICE connection watch"));
		return;
	}

	memset(&cb, 0, sizeof(cb));
	cb.save_yourself.callback = cb_save_yourself;
	cb.die.callback = cb_die;
	cb.save_complete.callback = cb_save_complete;
	cb.shutdown_cancelled.callback = cb_shutdown_cancelled;

	errbuf[0] = '\0';
	sm_conn = SmcOpenConnection(NULL, NULL, SmProtoMajor, SmProtoMinor,
				    SmcSaveYourselfProcMask | SmcDieProcMask |
				    SmcSaveCompleteProcMask |
				    SmcShutdownCancelledProcMask,
				    &cb, (char *)previous_id, &id_ret,
				    sizeof(errbuf), errbuf);
	if (!sm_conn) {
		wwarning(_("could not connect to the session manager: %s"),
			 errbuf[0] ? errbuf : "unknown error");
		IceRemoveConnectionWatch(ice_watch, NULL);
		return;
	}

	client_id = id_ret;
	publish_properties();
}

bool wXSMPConnected(void)
{
	return sm_conn != NULL;
}

bool wXSMPRequestShutdown(bool save_state)
{
	if (!sm_conn)
		return false;

	SmcRequestSaveYourself(sm_conn, save_state ? SmSaveBoth : SmSaveGlobal,
			       true, SmInteractStyleAny, false, true);
	if (sm_ice_conn)
		IceFlush(sm_ice_conn);
	return true;
}

void wXSMPDisconnect(void)
{
	if (!sm_conn)
		return;

	set_restart_style(SmRestartIfRunning);
	if (sm_ice_conn)
		IceFlush(sm_ice_conn);

	if (ice_input) {
		WMDeleteInputHandler(ice_input);
		ice_input = NULL;
	}
	SmcCloseConnection(sm_conn, 0, NULL);
	sm_conn = NULL;
	sm_ice_conn = NULL;
}

#endif  /* USE_XSMP */
