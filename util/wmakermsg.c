/*
 *  wmakermsg - send commands to and receive events from a running
 *  Window Maker over its Unix-socket IPC
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Exit status: 0 success, 1 transport failure (connect/read/write/
 *  protocol), 2 server replied success=NO to a COMMAND.
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <WINGs/WUtil.h>
#include <WINGs/WIPC.h>

static const char *prog;

static void usage(FILE *out)
{
	fprintf(out,
		"Usage: %s [-s SOCKET] [-q] VERB [ARG...]\n"
		"       %s [-s SOCKET] get_version|get_workspaces|get_windows\n"
		"       %s [-s SOCKET] send_tick PAYLOAD\n"
		"       %s [-s SOCKET] -m EVENT [EVENT...]\n"
		"       %s --get-socketpath\n",
		prog, prog, prog, prog, prog);
}

static int reply_is_success(const char *body)
{
	WMPropList *pl, *k, *v;
	int ok = 0;

	if (!body || !*body)
		return 0;
	pl = WMCreatePropListFromDescription(body);
	if (!pl)
		return 0;
	/* a single COMMAND returns a dict, multiple commands return an
	 * array of dicts and the CLI only ever sends one */
	if (WMIsPLDictionary(pl)) {
		k = WMCreatePLString("success");
		v = WMGetFromPLDictionary(pl, k);
		WMReleasePropList(k);
		if (v && WMIsPLString(v) && strcmp(WMGetFromPLString(v), "YES") == 0)
			ok = 1;
	}
	WMReleasePropList(pl);
	return ok;
}

static int do_monitor(int fd, int argc, char **argv, int quiet)
{
	WMPropList *arr, *s;
	uint32_t type, len;
	char *body, *sub;
	int i, rc;

	arr = WMCreatePLArray(NULL);
	for (i = 0; i < argc; i++) {
		s = WMCreatePLString(argv[i]);
		WMAddToPLArray(arr, s);
		WMReleasePropList(s);
	}
	sub = WMGetPropListDescription(arr, False);
	WMReleasePropList(arr);

	rc = wIPCSend(fd, WIPC_SUBSCRIBE, sub, (uint32_t)strlen(sub));
	wfree(sub);
	if (rc < 0) {
		fprintf(stderr, "%s: subscribe send: %s\n", prog, strerror(errno));
		return 1;
	}

	/* one event per line, flushed, so a `| while read -r` consumer
	 * never stalls on stdio buffering when stdout is a pipe */
	for (;;) {
		if (wIPCRecv(fd, &type, &body, &len) < 0)
			return errno == ECONNRESET ? 0 : 1;
		if (!quiet) {
			if (type & WIPC_EVENT_BIT)
				printf("%s\n", body);
			else
				printf("%s\n", body);
			fflush(stdout);
		}
		wfree(body);
	}
}

int main(int argc, char **argv)
{
	const char *socket_path = NULL;
	uint32_t req_type = WIPC_COMMAND;
	uint32_t rtype, rlen;
	WMPropList *outer, *inner, *s;
	char *path, *body, *rbody;
	int quiet = 0, monitor = 0;
	int opt, fd, i, rc;

	prog = argv[0];

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--get-socketpath") == 0) {
			path = wIPCSocketPath(NULL);
			printf("%s\n", path);
			wfree(path);
			return 0;
		}
		if (strcmp(argv[i], "--help") == 0) {
			usage(stdout);
			return 0;
		}
	}

	while ((opt = getopt(argc, argv, "s:qmh")) != -1) {
		switch (opt) {
		case 's':
			socket_path = optarg;
			break;
		case 'q':
			quiet = 1;
			break;
		case 'm':
			monitor = 1;
			break;
		case 'h':
			usage(stdout);
			return 0;
		default:
			usage(stderr);
			return 1;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1 && !monitor) {
		usage(stderr);
		return 1;
	}

	path = socket_path ? wstrdup(socket_path) : wIPCSocketPath(NULL);
	fd = wIPCConnect(path);
	if (fd < 0) {
		fprintf(stderr, "%s: cannot connect to %s: %s\n",
			prog, path, strerror(errno));
		wfree(path);
		return 1;
	}
	wfree(path);

	/* SIGPIPE on a dropped server should surface as a write error,
	 * not kill us before the exit-status convention can apply */
	signal(SIGPIPE, SIG_IGN);

	if (monitor)
		return do_monitor(fd, argc, argv, quiet);

	/* typed requests, everything else falls through to COMMAND */
	if (strcmp(argv[0], "get_version") == 0) {
		req_type = WIPC_GET_VERSION;
		body = wstrdup("");
	} else if (strcmp(argv[0], "get_workspaces") == 0) {
		req_type = WIPC_GET_WORKSPACES;
		body = wstrdup("");
	} else if (strcmp(argv[0], "get_windows") == 0) {
		req_type = WIPC_GET_WINDOWS;
		body = wstrdup("");
	} else if (strcmp(argv[0], "send_tick") == 0) {
		req_type = WIPC_SEND_TICK;
		s = WMCreatePLString(argc > 1 ? argv[1] : "");
		body = WMGetPropListDescription(s, False);
		WMReleasePropList(s);
	} else {
		inner = WMCreatePLArray(NULL);
		for (i = 0; i < argc; i++) {
			s = WMCreatePLString(argv[i]);
			WMAddToPLArray(inner, s);
			WMReleasePropList(s);
		}
		outer = WMCreatePLArray(inner, NULL);
		WMReleasePropList(inner);
		body = WMGetPropListDescription(outer, False);
		WMReleasePropList(outer);
	}

	rc = wIPCSend(fd, req_type, body, (uint32_t)strlen(body));
	wfree(body);
	if (rc < 0) {
		fprintf(stderr, "%s: send: %s\n", prog, strerror(errno));
		close(fd);
		return 1;
	}

	if (wIPCRecv(fd, &rtype, &rbody, &rlen) < 0) {
		fprintf(stderr, "%s: recv: %s\n", prog, strerror(errno));
		close(fd);
		return 1;
	}
	close(fd);

	if (!quiet && rlen)
		printf("%s\n", rbody);

	rc = 0;
	if (req_type == WIPC_COMMAND && !reply_is_success(rbody))
		rc = 2;
	wfree(rbody);
	return rc;
}
