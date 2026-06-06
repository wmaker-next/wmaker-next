/*
 *  wipc - blocking client side of the Window Maker IPC socket
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  Pure POSIX, no Xlib, no event loop, so wmakermsg and the util/
 *  text-mode tools link only -lWUtil.
 */

#include "wconfig.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include <WINGs/WUtil.h>
#include <WINGs/WIPC.h>

/* canonical display key, every byte outside [A-Za-z0-9] becomes '_' and a
 * missing screen suffix is treated as ".0" so ":0" and ":0.0" both yield
 * "_0_0", "localhost:10.0" yields "localhost_10_0" */
static char *display_key(const char *display)
{
	const char *colon;
	char *raw, *out, *p;
	size_t len;

	if (!display || !*display)
		display = getenv("DISPLAY");
	if (!display || !*display)
		display = ":0";

	colon = strrchr(display, ':');
	if (colon && !strchr(colon, '.')) {
		len = strlen(display);
		raw = wmalloc(len + 3);
		memcpy(raw, display, len);
		raw[len] = '.';
		raw[len + 1] = '0';
		raw[len + 2] = '\0';
	} else {
		raw = wstrdup(display);
	}

	out = raw;
	for (p = out; *p; p++) {
		if (!isalnum((unsigned char)*p))
			*p = '_';
	}
	return out;
}

char *wIPCSocketPath(const char *display)
{
	const char *override, *runtime;
	char *key, *dir, *path;
	char tmp[64];
	size_t len;

	override = getenv("WMAKER_IPC_SOCKET");
	if (override && *override)
		return wstrdup(override);

	key = display_key(display);

	runtime = getenv("XDG_RUNTIME_DIR");
	if (runtime && *runtime) {
		dir = wstrconcat(runtime, "/wmaker");
	} else {
		snprintf(tmp, sizeof(tmp), "/tmp/wmaker-%lu",
			 (unsigned long)getuid());
		dir = wstrdup(tmp);
	}

	len = strlen(dir) + strlen(key) + 6;
	path = wmalloc(len);
	snprintf(path, len, "%s/ipc.%s", dir, key);

	wfree(dir);
	wfree(key);
	return path;
}

int wIPCConnect(const char *path)
{
	struct sockaddr_un addr;
	int fd, flags;

	if (!path || strlen(path) >= sizeof(addr.sun_path)) {
		errno = ENAMETOOLONG;
		return -1;
	}

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;

	flags = fcntl(fd, F_GETFD);
	if (flags >= 0)
		fcntl(fd, F_SETFD, flags | FD_CLOEXEC);

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		int saved = errno;
		close(fd);
		errno = saved;
		return -1;
	}
	return fd;
}

static int writeall(int fd, const void *buf, size_t len)
{
	const char *p = buf;
	ssize_t n;

	while (len > 0) {
		n = write(fd, p, len);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		p += n;
		len -= (size_t)n;
	}
	return 0;
}

static int readall(int fd, void *buf, size_t len)
{
	char *p = buf;
	ssize_t n;

	while (len > 0) {
		n = read(fd, p, len);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (n == 0) {
			errno = ECONNRESET;
			return -1;
		}
		p += n;
		len -= (size_t)n;
	}
	return 0;
}

int wIPCSend(int fd, uint32_t type, const char *payload, uint32_t len)
{
	char hdr[WIPC_HEADER_LEN];

	if (len > WIPC_MAX_PAYLOAD) {
		errno = EMSGSIZE;
		return -1;
	}

	memcpy(hdr, WIPC_MAGIC, 8);
	memcpy(hdr + 8, &len, 4);
	memcpy(hdr + 12, &type, 4);

	if (writeall(fd, hdr, sizeof(hdr)) < 0)
		return -1;
	if (len > 0 && writeall(fd, payload, len) < 0)
		return -1;
	return 0;
}

int wIPCRecv(int fd, uint32_t *type, char **payload, uint32_t *len)
{
	char hdr[WIPC_HEADER_LEN];
	uint32_t l, t;
	char *buf;

	*payload = NULL;
	*len = 0;
	*type = 0;

	if (readall(fd, hdr, sizeof(hdr)) < 0)
		return -1;

	if (memcmp(hdr, WIPC_MAGIC, 8) != 0) {
		errno = EPROTO;
		return -1;
	}
	memcpy(&l, hdr + 8, 4);
	memcpy(&t, hdr + 12, 4);

	if (l > WIPC_MAX_PAYLOAD) {
		errno = EMSGSIZE;
		return -1;
	}

	buf = wmalloc((size_t)l + 1);
	if (l > 0 && readall(fd, buf, l) < 0) {
		wfree(buf);
		return -1;
	}
	buf[l] = '\0';

	/* the payload feeds a NUL-terminated parser, an interior NUL would
	 * let strlen-based callers diverge from the wire length */
	if (memchr(buf, '\0', l) != NULL) {
		wfree(buf);
		errno = EPROTO;
		return -1;
	}

	*type = t;
	*len = l;
	*payload = buf;
	return 0;
}

WMPropList *wIPCCommand(const char *display, const char *cmd)
{
	WMPropList *verb, *inner, *outer, *reply;
	char *path, *body, *rbuf;
	uint32_t rtype, rlen;
	int fd, ok;

	reply = NULL;
	path = wIPCSocketPath(display);
	fd = wIPCConnect(path);
	wfree(path);
	if (fd < 0)
		return NULL;

	verb = WMCreatePLString(cmd);
	inner = WMCreatePLArray(verb, NULL);
	outer = WMCreatePLArray(inner, NULL);
	body = WMGetPropListDescription(outer, False);
	WMReleasePropList(outer);

	ok = (wIPCSend(fd, WIPC_COMMAND, body, (uint32_t)strlen(body)) == 0);
	wfree(body);

	if (ok && wIPCRecv(fd, &rtype, &rbuf, &rlen) == 0) {
		if (rtype == WIPC_COMMAND && rlen > 0)
			reply = WMCreatePropListFromDescription(rbuf);
		wfree(rbuf);
	}

	close(fd);
	return reply;
}
