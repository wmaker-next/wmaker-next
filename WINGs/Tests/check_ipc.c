/*
 *  check_ipc - automated unit tests for the WIPC wire helpers in
 *  libWUtil (wIPCSocketPath, wIPCConnect, wIPCSend, wIPCRecv)
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
 *
 *  Run by `make check`. Exit status 0 on pass, non-zero with the file
 *  and line of every failing assertion printed to stderr.
 *
 *  Scope: this exercises the blocking client wire layer over a
 *  socketpair() with no X involvement. The server-side behaviours that
 *  need a live wmaker (sub-mask filtering, first-tick delivery, write
 *  queue drain timer) are covered by test/ipc-drive.sh instead, since
 *  src/ipc.c links against the X-dependent core and cannot be pulled
 *  into a libWUtil-only test binary.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <WINGs/WUtil.h>
#include <WINGs/WIPC.h>

#include "check_common.h"

static int has_suffix(const char *s, const char *suffix)
{
	size_t sl = strlen(s), xl = strlen(suffix);
	return sl >= xl && strcmp(s + sl - xl, suffix) == 0;
}

/* ---- socket-path canonicalisation ----------------------------------- */

static void test_socketpath(void)
{
	char *a, *b, *c, *d, *e;

	/* the override beats everything */
	setenv("WMAKER_IPC_SOCKET", "/tmp/check_ipc.override", 1);
	a = wIPCSocketPath(":0");
	CHECK(strcmp(a, "/tmp/check_ipc.override") == 0);
	wfree(a);
	unsetenv("WMAKER_IPC_SOCKET");

	/* directory selection follows XDG_RUNTIME_DIR */
	setenv("XDG_RUNTIME_DIR", "/tmp/check_ipc_runtime", 1);

	/* :0 and :0.0 normalise to the same key */
	a = wIPCSocketPath(":0");
	b = wIPCSocketPath(":0.0");
	CHECK(strcmp(a, b) == 0);
	CHECK(has_suffix(a, "/wmaker/ipc._0_0"));
	CHECK(strncmp(a, "/tmp/check_ipc_runtime/", 23) == 0);

	/* a different screen is a different socket */
	c = wIPCSocketPath(":0.1");
	CHECK(strcmp(a, c) != 0);
	CHECK(has_suffix(c, "/wmaker/ipc._0_1"));

	/* host-qualified display, every non-alnum becomes underscore */
	d = wIPCSocketPath("localhost:10.0");
	CHECK(has_suffix(d, "/wmaker/ipc.localhost_10_0"));

	/* NULL means $DISPLAY */
	setenv("DISPLAY", ":7", 1);
	e = wIPCSocketPath(NULL);
	CHECK(has_suffix(e, "/wmaker/ipc._7_0"));

	wfree(a);
	wfree(b);
	wfree(c);
	wfree(d);
	wfree(e);
	unsetenv("XDG_RUNTIME_DIR");
}

/* ---- send/recv round-trip ------------------------------------------- */

static void test_roundtrip(void)
{
	int sv[2];
	uint32_t type, len;
	char *body;

	CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

	CHECK(wIPCSend(sv[0], WIPC_GET_VERSION, NULL, 0) == 0);
	CHECK(wIPCRecv(sv[1], &type, &body, &len) == 0);
	CHECK(type == WIPC_GET_VERSION);
	CHECK(len == 0);
	CHECK(body != NULL && body[0] == '\0');
	wfree(body);

	CHECK(wIPCSend(sv[0], WIPC_COMMAND, "((reconfigure))", 15) == 0);
	CHECK(wIPCRecv(sv[1], &type, &body, &len) == 0);
	CHECK(type == WIPC_COMMAND);
	CHECK(len == 15);
	CHECK(strcmp(body, "((reconfigure))") == 0);
	/* trailing NUL appended past the wire length */
	CHECK(body[len] == '\0');
	wfree(body);

	close(sv[0]);
	close(sv[1]);
}

/* ---- partial-read reassembly ---------------------------------------- */

/* feed a frame one byte at a time from a forked writer so the parent's
 * blocking wIPCRecv() must loop on short reads */
static void test_reassembly(void)
{
	const char payload[] = "(tick)";
	char frame[WIPC_HEADER_LEN + sizeof(payload) - 1];
	uint32_t type = WIPC_SEND_TICK, plen = sizeof(payload) - 1;
	uint32_t rtype, rlen;
	char *rbody;
	pid_t kid;
	int sv[2], status, i;

	CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

	memcpy(frame, WIPC_MAGIC, 8);
	memcpy(frame + 8, &plen, 4);
	memcpy(frame + 12, &type, 4);
	memcpy(frame + WIPC_HEADER_LEN, payload, plen);

	kid = fork();
	if (kid == 0) {
		close(sv[1]);
		for (i = 0; i < (int)sizeof(frame); i++) {
			ssize_t w = write(sv[0], frame + i, 1);
			(void)w;
			usleep(1000);
		}
		_exit(0);
	}
	CHECK(kid > 0);
	close(sv[0]);

	CHECK(wIPCRecv(sv[1], &rtype, &rbody, &rlen) == 0);
	CHECK(rtype == WIPC_SEND_TICK);
	CHECK(rlen == plen);
	CHECK(strcmp(rbody, payload) == 0);
	wfree(rbody);

	waitpid(kid, &status, 0);
	close(sv[1]);
}

/* ---- protocol violations -------------------------------------------- */

static void test_bad_magic(void)
{
	char hdr[WIPC_HEADER_LEN];
	uint32_t type, len, zero = 0;
	char *body;
	int sv[2];

	CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

	memcpy(hdr, "garbage!", 8);
	memcpy(hdr + 8, &zero, 4);
	memcpy(hdr + 12, &zero, 4);
	CHECK(write(sv[0], hdr, sizeof(hdr)) == (ssize_t)sizeof(hdr));

	CHECK(wIPCRecv(sv[1], &type, &body, &len) < 0);
	CHECK(errno == EPROTO);
	CHECK(body == NULL);

	close(sv[0]);
	close(sv[1]);
}

static void test_oversize(void)
{
	char hdr[WIPC_HEADER_LEN];
	uint32_t type, len, big = WIPC_MAX_PAYLOAD + 1, t = 0;
	char *body;
	int sv[2];

	CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

	memcpy(hdr, WIPC_MAGIC, 8);
	memcpy(hdr + 8, &big, 4);
	memcpy(hdr + 12, &t, 4);
	CHECK(write(sv[0], hdr, sizeof(hdr)) == (ssize_t)sizeof(hdr));

	CHECK(wIPCRecv(sv[1], &type, &body, &len) < 0);
	CHECK(errno == EMSGSIZE);
	CHECK(body == NULL);

	/* and the sender side refuses to encode it too */
	CHECK(wIPCSend(sv[0], 0, "x", WIPC_MAX_PAYLOAD + 1) < 0);
	CHECK(errno == EMSGSIZE);

	close(sv[0]);
	close(sv[1]);
}

static void test_embedded_nul(void)
{
	char hdr[WIPC_HEADER_LEN];
	char body[4] = { 'a', '\0', 'b', 'c' };
	uint32_t type, len, plen = 4, t = 0;
	char *out;
	int sv[2];

	CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

	memcpy(hdr, WIPC_MAGIC, 8);
	memcpy(hdr + 8, &plen, 4);
	memcpy(hdr + 12, &t, 4);
	CHECK(write(sv[0], hdr, sizeof(hdr)) == (ssize_t)sizeof(hdr));
	CHECK(write(sv[0], body, plen) == (ssize_t)plen);

	CHECK(wIPCRecv(sv[1], &type, &out, &len) < 0);
	CHECK(errno == EPROTO);
	CHECK(out == NULL);

	close(sv[0]);
	close(sv[1]);
}

static void test_eof_mid_frame(void)
{
	char hdr[WIPC_HEADER_LEN];
	uint32_t type, len, plen = 100, t = 0;
	char *body;
	int sv[2];

	CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

	memcpy(hdr, WIPC_MAGIC, 8);
	memcpy(hdr + 8, &plen, 4);
	memcpy(hdr + 12, &t, 4);
	/* full header, no payload, then EOF */
	CHECK(write(sv[0], hdr, sizeof(hdr)) == (ssize_t)sizeof(hdr));
	close(sv[0]);

	CHECK(wIPCRecv(sv[1], &type, &body, &len) < 0);
	CHECK(errno == ECONNRESET);
	CHECK(body == NULL);

	close(sv[1]);
}

/* ---- reply/event demux on a single fd ------------------------------- */

/* a real subscriber that also issues GET_* on the same connection sees
 * replies and events interleaved, the only way to tell them apart is
 * the high bit of the type field, this is what status-bar clients
 * avoid by opening two sockets, the wire layer must keep them straight */
static void test_demux(void)
{
	uint32_t type, len;
	char *body;
	int sv[2];

	CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

	/* server side queues an event then a reply back to back */
	CHECK(wIPCSend(sv[0], WIPC_EVENT_BIT | WIPC_EV_TICK,
		       "{first=YES;}", 12) == 0);
	CHECK(wIPCSend(sv[0], WIPC_GET_VERSION,
		       "{version=\"x\";}", 14) == 0);

	CHECK(wIPCRecv(sv[1], &type, &body, &len) == 0);
	CHECK((type & WIPC_EVENT_BIT) != 0);
	CHECK((type & ~WIPC_EVENT_BIT) == WIPC_EV_TICK);
	wfree(body);

	CHECK(wIPCRecv(sv[1], &type, &body, &len) == 0);
	CHECK((type & WIPC_EVENT_BIT) == 0);
	CHECK(type == WIPC_GET_VERSION);
	wfree(body);

	close(sv[0]);
	close(sv[1]);
}

/* ---- socket-directory validation ------------------------------------ */

/* wIPCConnect() refuses a socket whose directory is not a private
 * directory owned by us, so a pre-created predictable fallback directory
 * cannot collect command frames. A private directory with no socket must
 * reach connect() and fail with ENOENT rather than the earlier EACCES. */
static void test_socket_dir(void)
{
	char tmpl[] = "/tmp/check_ipc_dir_XXXXXX";
	char priv[256], pub[256], missing[256], file[256];
	char spath[320];
	struct sockaddr_un addr;
	int fd, server;

	if (!mkdtemp(tmpl))
		return;

	snprintf(priv, sizeof(priv), "%s/priv", tmpl);
	snprintf(pub, sizeof(pub), "%s/pub", tmpl);
	snprintf(missing, sizeof(missing), "%s/missing", tmpl);
	snprintf(file, sizeof(file), "%s/file", tmpl);
	CHECK(mkdir(priv, 0700) == 0);
	CHECK(chmod(priv, 0700) == 0);
	CHECK(mkdir(pub, 0755) == 0);
	CHECK(chmod(pub, 0755) == 0);
	fd = open(file, O_CREAT | O_WRONLY, 0600);
	CHECK(fd >= 0);
	if (fd >= 0)
		close(fd);

	/* private directory, no socket */
	snprintf(spath, sizeof(spath), "%s/sock", priv);
	errno = 0;
	CHECK(wIPCConnect(spath) < 0);
	CHECK(errno == ENOENT);

	/* A private directory with a real socket remains usable. */
	server = socket(AF_UNIX, SOCK_STREAM, 0);
	CHECK(server >= 0);
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/priv/sock", tmpl);
	CHECK(bind(server, (struct sockaddr *)&addr, sizeof(addr)) == 0);
	CHECK(listen(server, 1) == 0);
	fd = wIPCConnect(spath);
	CHECK(fd >= 0);
	if (fd >= 0)
		close(fd);
	close(server);
	unlink(spath);

	fd = open(spath, O_CREAT | O_WRONLY, 0600);
	CHECK(fd >= 0);
	if (fd >= 0)
		close(fd);
	errno = 0;
	CHECK(wIPCConnect(spath) < 0);
	CHECK(errno == EACCES);
	unlink(spath);

	/* group/other-accessible directory is rejected before connect() */
	snprintf(spath, sizeof(spath), "%s/sock", pub);
	errno = 0;
	CHECK(wIPCConnect(spath) < 0);
	CHECK(errno == EACCES);

	/* a regular file where the directory should be is rejected too */
	snprintf(spath, sizeof(spath), "%s/sock", file);
	errno = 0;
	CHECK(wIPCConnect(spath) < 0);
	CHECK(errno == EACCES);

	/* a missing directory is left to connect() to report */
	snprintf(spath, sizeof(spath), "%s/sock", missing);
	errno = 0;
	CHECK(wIPCConnect(spath) < 0);
	CHECK(errno == ENOENT);

	unlink(file);
	rmdir(pub);
	rmdir(priv);
	rmdir(tmpl);
}

/* ---- header layout invariants --------------------------------------- */

static void test_header_layout(void)
{
	/* the wire encoder lays the header out as 8+4+4 by hand, the
	 * documented constant must keep matching that */
	CHECK(WIPC_HEADER_LEN == 16);
	CHECK(strlen(WIPC_MAGIC) == 8);
	/* event ids fit in the broadcast bitmask */
	CHECK(WIPC_EV_COUNT <= 32);
}

int main(void)
{
	test_header_layout();
	test_socketpath();
	test_roundtrip();
	test_reassembly();
	test_bad_magic();
	test_oversize();
	test_embedded_nul();
	test_eof_mid_frame();
	test_demux();
	test_socket_dir();

	wutil_shutdown();

	return check_report("check_ipc");
}
