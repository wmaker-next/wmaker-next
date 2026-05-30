/*
 *  spawn.c - argv-based child process helpers
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  These wrap fork()+execvp() so callers pass an argv array instead of
 *  building a shell command string. Nothing here goes through /bin/sh,
 *  so arguments are delivered to the child verbatim with no quoting,
 *  word-splitting or glob expansion. Callers that genuinely need shell
 *  semantics can pass {"/bin/sh", "-c", cmd, NULL} explicitly.
 */

#include "wconfig.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "WINGs/WUtil.h"

pid_t wspawnvp(char *const argv[])
{
	pid_t pid;

	if (!argv || !argv[0]) {
		errno = EINVAL;
		return -1;
	}

	pid = fork();
	if (pid < 0) {
		werror(_("could not fork to run %s: %s"), argv[0], strerror(errno));
		return -1;
	}
	if (pid == 0) {
		execvp(argv[0], argv);
		werror(_("could not execute %s: %s"), argv[0], strerror(errno));
		_exit(127);
	}
	return pid;
}

int wspawnvp_wait(char *const argv[])
{
	pid_t pid;
	int status;

	pid = wspawnvp(argv);
	if (pid < 0)
		return -1;

	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR)
			return -1;
	}
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	return -1;
}

/*
 * Run argv with stdout connected to a pipe, read up to maxlen bytes into
 * a wmalloc'd NUL-terminated buffer, wait for the child, and return the
 * buffer (or NULL on fork/pipe failure). The buffer is returned even when
 * the child exits non-zero, with the status placed in *exit_status when
 * that pointer is not NULL.
 */
char *wspawnvp_capture(char *const argv[], size_t maxlen, int *exit_status)
{
	int fds[2];
	pid_t pid;
	char *buf;
	size_t got = 0;
	ssize_t r;
	int status;

	if (exit_status)
		*exit_status = -1;

	if (!argv || !argv[0] || maxlen == 0) {
		errno = EINVAL;
		return NULL;
	}

	if (pipe(fds) < 0) {
		werror(_("could not create pipe to run %s: %s"), argv[0], strerror(errno));
		return NULL;
	}

	pid = fork();
	if (pid < 0) {
		werror(_("could not fork to run %s: %s"), argv[0], strerror(errno));
		close(fds[0]);
		close(fds[1]);
		return NULL;
	}
	if (pid == 0) {
		close(fds[0]);
		if (dup2(fds[1], STDOUT_FILENO) < 0)
			_exit(126);
		close(fds[1]);
		execvp(argv[0], argv);
		werror(_("could not execute %s: %s"), argv[0], strerror(errno));
		_exit(127);
	}

	close(fds[1]);
	buf = wmalloc(maxlen + 1);
	while (got < maxlen) {
		r = read(fds[0], buf + got, maxlen - got);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (r == 0)
			break;
		got += (size_t)r;
	}
	buf[got] = '\0';
	/* drain anything left so the child does not block on a full pipe */
	{
		char drain[256];
		while (read(fds[0], drain, sizeof(drain)) > 0)
			;
	}
	close(fds[0]);

	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR) {
			status = -1;
			break;
		}
	}
	if (exit_status)
		*exit_status = (status >= 0 && WIFEXITED(status)) ? WEXITSTATUS(status) : -1;

	return buf;
}
