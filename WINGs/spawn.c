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
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <https://www.gnu.org/licenses/>.
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
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "WINGs/WUtil.h"

static pid_t waitpid_nointr(pid_t pid, int *status, int options)
{
	pid_t r;

	do {
		r = waitpid(pid, status, options);
	} while (r < 0 && errno == EINTR);

	return r;
}

static void sleep_msec(long msec)
{
	struct timespec req, rem;

	req.tv_sec = msec / 1000;
	req.tv_nsec = (msec % 1000) * 1000000L;
	while (nanosleep(&req, &rem) < 0 && errno == EINTR)
		req = rem;
}

static void terminate_after_capture_cap(pid_t pid, int *status)
{
	int i;
	pid_t r;

	if (status)
		*status = -1;

	if (kill(pid, SIGTERM) < 0 && errno == ESRCH)
		return;

	for (i = 0; i < 20; i++) {
		r = waitpid_nointr(pid, status, WNOHANG);
		if (r == pid || r < 0)
			return;
		sleep_msec(10);
	}

	if (kill(pid, SIGKILL) < 0 && errno == ESRCH)
		return;

	(void)waitpid_nointr(pid, status, 0);
}

pid_t wspawnvp_setup(char *const argv[], void (*pre_exec)(void *udata), void *udata)
{
	pid_t pid;

	if (!argv || !argv[0]) {
		errno = EINVAL;
		return -1;
	}

	/* children created here exec immediately and never re-enter the
	 * libev-backed WINGs event loop, so ev_loop_fork() is not called,
	 * a future child that does want to keep running and use WMAdd*Handler
	 * after fork must call it before the first ev_run() */
	pid = fork();
	if (pid < 0) {
		werror(_("could not fork to run %s: %s"), argv[0], strerror(errno));
		return -1;
	}
	if (pid == 0) {
		if (pre_exec)
			pre_exec(udata);
		execvp(argv[0], argv);
		werror(_("could not execute %s: %s"), argv[0], strerror(errno));
		_exit(127);
	}
	return pid;
}

pid_t wspawnvp(char *const argv[])
{
	return wspawnvp_setup(argv, NULL, NULL);
}

int wspawnvp_wait(char *const argv[])
{
	pid_t pid;
	int status;

	pid = wspawnvp(argv);
	if (pid < 0)
		return -1;

	if (waitpid_nointr(pid, &status, 0) < 0)
		return -1;
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	return -1;
}

/*
 * Run argv with stdout connected to a pipe, read up to maxlen bytes into
 * a wmalloc'd NUL-terminated buffer, wait for the child, and return the
 * buffer (or NULL on fork/pipe failure). maxlen is a hard cap: when the
 * buffer fills, the child is terminated and *exit_status is set to -1.
 */
char *wspawnvp_capture(char *const argv[], size_t maxlen, int *exit_status)
{
	int fds[2];
	pid_t pid;
	char *buf;
	size_t got = 0;
	ssize_t r;
	int status = -1;
	int capped = 0;

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
	capped = got == maxlen;
	close(fds[0]);

	if (capped) {
		terminate_after_capture_cap(pid, &status);
		if (exit_status)
			*exit_status = -1;
		return buf;
	}

	if (waitpid_nointr(pid, &status, 0) < 0)
		status = -1;
	if (exit_status)
		*exit_status = (status >= 0 && WIFEXITED(status)) ? WEXITSTATUS(status) : -1;

	return buf;
}
