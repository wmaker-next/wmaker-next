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
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, see <https://www.gnu.org/licenses/>.
 *
 *  Pure POSIX, no Xlib, no event loop, no libWUtil symbols, so the
 *  transport builds into libWUtil, wmakermsg and the util/ text-mode
 *  tools link only -lWUtil, and wmlib links it through the same
 *  convenience archive without pulling the GPL proplist core into
 *  the LGPL-only libWMaker.
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
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>

#include <WINGs/WIPC.h>

/* standalone allocation helpers mirroring wmalloc/wfree semantics
 * (zero-initialised, abort on exhaustion) so this file does not
 * reference libWUtil symbols */
static void *xmalloc(size_t size)
{
	void *tmp = malloc(size);

	if (tmp == NULL)
		abort();
	return memset(tmp, 0, size);
}

static void xfree(void *ptr)
{
	free(ptr);
}

static char *xstrdup(const char *str)
{
	size_t len = strlen(str) + 1;
	char *copy = xmalloc(len);

	memcpy(copy, str, len);
	return copy;
}

static char *xstrconcat(const char *str1, const char *str2)
{
	size_t l1 = strlen(str1);
	size_t len = l1 + strlen(str2) + 1;
	char *str = xmalloc(len);

	memcpy(str, str1, l1);
	memcpy(str + l1, str2, len - l1);
	return str;
}

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
		raw = xmalloc(len + 3);
		memcpy(raw, display, len);
		raw[len] = '.';
		raw[len + 1] = '0';
		raw[len + 2] = '\0';
	} else {
		raw = xstrdup(display);
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
		return xstrdup(override);

	key = display_key(display);

	runtime = getenv("XDG_RUNTIME_DIR");
	if (runtime && *runtime) {
		dir = xstrconcat(runtime, "/wmaker");
	} else {
		snprintf(tmp, sizeof(tmp), "/tmp/wmaker-%lu",
			 (unsigned long)getuid());
		dir = xstrdup(tmp);
	}

	len = strlen(dir) + strlen(key) + 6;
	path = xmalloc(len);
	snprintf(path, len, "%s/ipc.%s", dir, key);

	xfree(dir);
	xfree(key);
	return path;
}

/* the socket directory must be a real directory owned by us with no
 * group or other access, matching the server-side check, so a local
 * attacker cannot pre-create the predictable /tmp fallback and collect
 * our frames. Capture its identity so replacement during connect is
 * detected before any frames are sent. */
static int socket_dir_stat(const char *path, struct stat *st)
{
	const char *slash;
	char *dir;
	size_t len;
	int ok;

	slash = strrchr(path, '/');
	if (!slash) {
		dir = xstrdup(".");
	} else {
		len = (size_t)(slash - path);
		if (len == 0)
			len = 1;
		dir = xmalloc(len + 1);
		memcpy(dir, path, len);
	}

	if (lstat(dir, st) < 0) {
		ok = 0;
	} else if (!S_ISDIR(st->st_mode) || st->st_uid != getuid() ||
		   (st->st_mode & 077) != 0) {
		errno = EACCES;
		ok = 0;
	} else {
		ok = 1;
	}
	xfree(dir);
	return ok;
}

int wIPCConnect(const char *path)
{
	struct sockaddr_un addr;
	struct stat dir_before, dir_after, socket_before, socket_after;
	int fd, flags;

	if (!path || strlen(path) >= sizeof(addr.sun_path)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	if (!socket_dir_stat(path, &dir_before))
		return -1;
	if (lstat(path, &socket_before) < 0)
		return -1;
	if (!S_ISSOCK(socket_before.st_mode) || socket_before.st_uid != getuid()) {
		errno = EACCES;
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
	if (!socket_dir_stat(path, &dir_after) || lstat(path, &socket_after) < 0 ||
	    dir_before.st_dev != dir_after.st_dev || dir_before.st_ino != dir_after.st_ino ||
	    socket_before.st_dev != socket_after.st_dev || socket_before.st_ino != socket_after.st_ino ||
	    !S_ISSOCK(socket_after.st_mode) || socket_after.st_uid != getuid()) {
		close(fd);
		errno = EACCES;
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

int wIPCSend(int fd, uint32_t type, const char *payload, size_t len)
{
	char hdr[WIPC_HEADER_LEN];
	uint32_t len32;

	/* check the size_t value before narrowing so a >4 GiB body on
	 * LP64 is rejected rather than silently truncated by the cast */
	if (len > WIPC_MAX_PAYLOAD) {
		errno = EMSGSIZE;
		return -1;
	}
	len32 = (uint32_t)len;

	memcpy(hdr, WIPC_MAGIC, 8);
	memcpy(hdr + 8, &len32, 4);
	memcpy(hdr + 12, &type, 4);

	if (writeall(fd, hdr, sizeof(hdr)) < 0)
		return -1;
	if (len32 > 0 && writeall(fd, payload, len32) < 0)
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

	buf = xmalloc((size_t)l + 1);
	if (l > 0 && readall(fd, buf, l) < 0) {
		xfree(buf);
		return -1;
	}
	buf[l] = '\0';

	/* the payload feeds a NUL-terminated parser, an interior NUL would
	 * let strlen-based callers diverge from the wire length */
	if (memchr(buf, '\0', l) != NULL) {
		xfree(buf);
		errno = EPROTO;
		return -1;
	}

	*type = t;
	*len = l;
	*payload = buf;
	return 0;
}
