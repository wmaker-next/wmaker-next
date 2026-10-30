/*
 *  sm-listener.c
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

/* Minimal XSMP session-manager listener used by test/xsmp-drive.sh to
 * exercise wmaker's SmcOpenConnection() path without a real desktop
 * session manager.
 *
 * Accepts one client. Prints one line per protocol event on stdout so
 * the driver can grep the log:
 *
 *   SESSION_MANAGER=<network-ids>   -- first line, driver exports this
 *   ready                           -- listen sockets are up
 *   register id=<client-id>
 *   set-prop <name> = "<v>" ...     -- one line per property
 *   save-yourself-request shutdown=<0|1> global=<0|1>
 *   phase2-request
 *   save-yourself-done success=<0|1>
 *   close-connection
 *
 * Signals from the driver:
 *   SIGUSR1  send SaveYourself (checkpoint, shutdown=False)
 *   SIGUSR2  send Die
 *   SIGTERM  exit
 */
#include <X11/SM/SMlib.h>
#include <X11/ICE/ICElib.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

static SmsConn client_sms;
static IceConn client_ice;
static int selfpipe[2] = { -1, -1 };
static int in_save;
static int shutting_down;

static void note(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
	fputc('\n', stdout);
	fflush(stdout);
}

static void sig_forward(int sig)
{
	unsigned char c = (unsigned char)sig;
	ssize_t r;

	do {
		r = write(selfpipe[1], &c, 1);
	} while (r < 0 && errno == EINTR);
}

static void ice_io_error(IceConn conn)
{
	(void)conn;
	/* IceProcessMessages returns IOError right after this, teardown
	 * happens there so libICE does not call exit() */
}

static Bool auth_any(char *hostname)
{
	(void)hostname;
	return True;
}

static Status cb_register(SmsConn sms, SmPointer data, char *previous_id)
{
	char *id;

	(void)data;

	if (previous_id) {
		id = previous_id;
	} else {
		id = SmsGenerateClientID(sms);
		if (!id)
			id = strdup("sm-listener-0");
	}
	SmsRegisterClientReply(sms, id);
	note("register id=%s", id);

	/* the protocol says the manager sends an initial SaveYourself with
	 * type=Local, shutdown=False right after registration so the client
	 * publishes its properties, wmaker publishes unconditionally on
	 * connect so this only exercises the callback wiring */
	SmsSaveYourself(sms, SmSaveLocal, False, SmInteractStyleNone, False);
	in_save = 1;
	if (!previous_id)
		free(id);
	return 1;
}

static void cb_interact_request(SmsConn sms, SmPointer data, int dialog_type)
{
	(void)data;
	(void)dialog_type;
	note("interact-request");
	SmsInteract(sms);
}

static void cb_interact_done(SmsConn sms, SmPointer data, Bool cancel)
{
	(void)sms;
	(void)data;
	note("interact-done cancel=%d", cancel ? 1 : 0);
}

static void cb_save_yourself_request(SmsConn sms, SmPointer data, int save_type,
				     Bool shutdown, int interact_style,
				     Bool fast, Bool global)
{
	(void)data;
	(void)interact_style;
	(void)fast;

	note("save-yourself-request shutdown=%d global=%d",
	     shutdown ? 1 : 0, global ? 1 : 0);
	shutting_down = shutdown;
	/* single-client manager, so honour the request immediately: turn
	 * it round as a SaveYourself to the (only) client */
	SmsSaveYourself(sms, save_type, shutdown, SmInteractStyleNone, False);
	in_save = 1;
}

static void cb_phase2_request(SmsConn sms, SmPointer data)
{
	(void)data;
	note("phase2-request");
	/* one client, nothing else to wait for, grant phase 2 straight away */
	SmsSaveYourselfPhase2(sms);
}

static void cb_save_yourself_done(SmsConn sms, SmPointer data, Bool success)
{
	(void)data;
	note("save-yourself-done success=%d", success ? 1 : 0);
	in_save = 0;
	if (shutting_down) {
		SmsDie(sms);
		shutting_down = 0;
	} else {
		SmsSaveComplete(sms);
	}
}

static void cb_close_connection(SmsConn sms, SmPointer data,
				int count, char **reasons)
{
	IceConn ice;

	(void)data;

	note("close-connection");
	ice = SmsGetIceConnection(sms);
	SmsCleanUp(sms);
	IceSetShutdownNegotiation(ice, False);
	IceCloseConnection(ice);
	if (client_sms == sms) {
		client_sms = NULL;
		client_ice = NULL;
	}
	if (count > 0)
		SmFreeReasons(count, reasons);
}

static void cb_set_properties(SmsConn sms, SmPointer data,
			      int num_props, SmProp **props)
{
	int i, j;

	(void)sms;
	(void)data;

	for (i = 0; i < num_props; i++) {
		printf("set-prop %s =", props[i]->name);
		for (j = 0; j < props[i]->num_vals; j++) {
			if (strcmp(props[i]->type, SmCARD8) == 0 &&
			    props[i]->vals[j].length == 1) {
				printf(" %d", *(unsigned char *)props[i]->vals[j].value);
			} else {
				printf(" \"%.*s\"", props[i]->vals[j].length,
				       (char *)props[i]->vals[j].value);
			}
		}
		fputc('\n', stdout);
		SmFreeProperty(props[i]);
	}
	fflush(stdout);
	free(props);
}

static void cb_delete_properties(SmsConn sms, SmPointer data,
				 int num_props, char **prop_names)
{
	int i;

	(void)sms;
	(void)data;
	for (i = 0; i < num_props; i++) {
		note("delete-prop %s", prop_names[i]);
		free(prop_names[i]);
	}
	free(prop_names);
}

static void cb_get_properties(SmsConn sms, SmPointer data)
{
	(void)data;
	SmsReturnProperties(sms, 0, NULL);
}

static Status new_client(SmsConn sms, SmPointer data, unsigned long *mask,
			 SmsCallbacks *cb, char **failure)
{
	(void)data;
	(void)failure;

	client_sms = sms;

	cb->register_client.callback = cb_register;
	cb->register_client.manager_data = NULL;
	cb->interact_request.callback = cb_interact_request;
	cb->interact_request.manager_data = NULL;
	cb->interact_done.callback = cb_interact_done;
	cb->interact_done.manager_data = NULL;
	cb->save_yourself_request.callback = cb_save_yourself_request;
	cb->save_yourself_request.manager_data = NULL;
	cb->save_yourself_phase2_request.callback = cb_phase2_request;
	cb->save_yourself_phase2_request.manager_data = NULL;
	cb->save_yourself_done.callback = cb_save_yourself_done;
	cb->save_yourself_done.manager_data = NULL;
	cb->close_connection.callback = cb_close_connection;
	cb->close_connection.manager_data = NULL;
	cb->set_properties.callback = cb_set_properties;
	cb->set_properties.manager_data = NULL;
	cb->delete_properties.callback = cb_delete_properties;
	cb->delete_properties.manager_data = NULL;
	cb->get_properties.callback = cb_get_properties;
	cb->get_properties.manager_data = NULL;

	*mask = SmsRegisterClientProcMask | SmsInteractRequestProcMask |
		SmsInteractDoneProcMask | SmsSaveYourselfRequestProcMask |
		SmsSaveYourselfP2RequestProcMask | SmsSaveYourselfDoneProcMask |
		SmsCloseConnectionProcMask | SmsSetPropertiesProcMask |
		SmsDeletePropertiesProcMask | SmsGetPropertiesProcMask;

	return 1;
}

static void handle_signal(int sig)
{
	switch (sig) {
	case SIGUSR1:
		if (client_sms && !in_save) {
			SmsSaveYourself(client_sms, SmSaveBoth, False,
					SmInteractStyleNone, False);
			in_save = 1;
			IceFlush(client_ice);
		}
		break;
	case SIGUSR2:
		if (client_sms) {
			SmsDie(client_sms);
			IceFlush(client_ice);
		}
		break;
	default:
		exit(0);
	}
}

int main(void)
{
	IceListenObj *listeners;
	struct sigaction sa;
	char errbuf[256];
	char *ids;
	fd_set fds;
	int n_listen, i, maxfd, fd;
	unsigned char sc;

	if (pipe(selfpipe) < 0) {
		perror("pipe");
		return 2;
	}
	fcntl(selfpipe[0], F_SETFL, O_NONBLOCK);
	fcntl(selfpipe[1], F_SETFL, O_NONBLOCK);

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_forward;
	sigaction(SIGUSR1, &sa, NULL);
	sigaction(SIGUSR2, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	IceSetIOErrorHandler(ice_io_error);

	errbuf[0] = '\0';
	if (!SmsInitialize("sm-listener", "1.0", new_client, NULL, auth_any,
			   sizeof(errbuf), errbuf)) {
		fprintf(stderr, "SmsInitialize failed: %s\n", errbuf);
		return 2;
	}

	errbuf[0] = '\0';
	if (!IceListenForConnections(&n_listen, &listeners,
				     sizeof(errbuf), errbuf)) {
		fprintf(stderr, "IceListenForConnections failed: %s\n", errbuf);
		return 2;
	}
	for (i = 0; i < n_listen; i++)
		IceSetHostBasedAuthProc(listeners[i], auth_any);

	ids = IceComposeNetworkIdList(n_listen, listeners);
	note("SESSION_MANAGER=%s", ids ? ids : "");
	free(ids);
	note("ready");

	for (;;) {
		FD_ZERO(&fds);
		maxfd = selfpipe[0];
		FD_SET(selfpipe[0], &fds);
		for (i = 0; i < n_listen; i++) {
			fd = IceGetListenConnectionNumber(listeners[i]);
			FD_SET(fd, &fds);
			if (fd > maxfd)
				maxfd = fd;
		}
		if (client_ice) {
			fd = IceConnectionNumber(client_ice);
			FD_SET(fd, &fds);
			if (fd > maxfd)
				maxfd = fd;
		}

		if (select(maxfd + 1, &fds, NULL, NULL, NULL) < 0) {
			if (errno == EINTR)
				continue;
			perror("select");
			return 2;
		}

		if (FD_ISSET(selfpipe[0], &fds)) {
			while (read(selfpipe[0], &sc, 1) == 1)
				handle_signal(sc);
		}

		for (i = 0; i < n_listen; i++) {
			IceAcceptStatus st;
			IceConn nc;

			fd = IceGetListenConnectionNumber(listeners[i]);
			if (!FD_ISSET(fd, &fds))
				continue;
			nc = IceAcceptConnection(listeners[i], &st);
			if (!nc || st != IceAcceptSuccess)
				continue;
			/* pump the ICE opening handshake to completion, the
			 * newClientProc runs from inside IceProcessMessages
			 * once the client sends the ProtocolSetup */
			while (IceConnectionStatus(nc) == IceConnectPending)
				IceProcessMessages(nc, NULL, NULL);
			if (IceConnectionStatus(nc) != IceConnectAccepted) {
				IceCloseConnection(nc);
				continue;
			}
			client_ice = nc;
		}

		if (client_ice &&
		    FD_ISSET(IceConnectionNumber(client_ice), &fds)) {
			if (IceProcessMessages(client_ice, NULL, NULL) ==
			    IceProcessMessagesIOError) {
				note("io-error");
				if (client_sms)
					SmsCleanUp(client_sms);
				IceSetShutdownNegotiation(client_ice, False);
				IceCloseConnection(client_ice);
				client_sms = NULL;
				client_ice = NULL;
			}
		}
	}
}
