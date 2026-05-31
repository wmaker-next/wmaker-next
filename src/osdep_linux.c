
#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <WINGs/WUtil.h>

#include "wconfig.h"
#include "osdep.h"

static Bool getProcStatFields(pid_t pid, int *ppid, int *pgrp, int *tpgid)
{
	char path[64], line[4096];
	char *rparen, *rest;
	FILE *f;
	int scan_ppid, scan_pgrp, scan_tpgid;

	snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);
	f = fopen(path, "r");
	if (!f)
		return False;

	if (!fgets(line, sizeof(line), f)) {
		fclose(f);
		return False;
	}
	fclose(f);

	rparen = strrchr(line, ')');
	if (!rparen)
		return False;

	rest = rparen + 1;
	while (*rest == ' ')
		rest++;
	if (*rest == '\0')
		return False;

	/* Skip state. Remaining fields start at ppid, pgrp, session, tty_nr, tpgid. */
	rest++;
	while (*rest == ' ')
		rest++;

	if (sscanf(rest, "%d %d %*d %*d %d", &scan_ppid, &scan_pgrp, &scan_tpgid) != 3)
		return False;

	if (ppid)
		*ppid = scan_ppid;
	if (pgrp)
		*pgrp = scan_pgrp;
	if (tpgid)
		*tpgid = scan_tpgid;

	return True;
}

static const char *baseName(const char *path)
{
	const char *base = strrchr(path, '/');

	return base ? base + 1 : path;
}


/*
 * copy argc and argv for an existing process identified by `pid'
 * into suitable storage given in ***argv and *argc.
 *
 * subsequent calls use the same static area for argv and argc.
 *
 * returns 0 for failure, in which case argc := 0 and argv := NULL
 * returns 1 for success
 */
Bool GetCommandForPid(int pid, char ***argv, int *argc)
{
	static char buf[_POSIX_ARG_MAX];
	int fd, i, j;
	ssize_t count;

	*argv = NULL;
	*argc = 0;

	/* cmdline is a flattened series of null-terminated strings */
	snprintf(buf, sizeof(buf), "/proc/%d/cmdline", pid);
	while (1) {
		/* not switching this to stdio yet, as this does not need
		 * to be portable, and i'm lazy */
		fd = open(buf, O_RDONLY);
		if (fd != -1)
			break;
		if (errno == EINTR)
			continue;
		return False;
	}

	while (1) {
		count = read(fd, buf, sizeof(buf));
		if (count != -1)
			break;
		if (errno == EINTR)
			continue;
		close(fd);
		return False;
	}
	close(fd);

	/* count args */
	for (i = 0; i < count; i++)
		if (buf[i] == '\0')
			(*argc)++;

	if (*argc == 0)
		return False;

	*argv = (char **)wmalloc(sizeof(char *) * (*argc + 1 /* term. null ptr */));
	(*argv)[0] = buf;

	/* go through buf, set argv[$next] to the beginning of each string */
	for (i = 0, j = 1; i < count; i++) {
		if (buf[i] != '\0')
			continue;
		if (i < count - 1)
			(*argv)[j++] = &buf[i + 1];
		if (j == *argc)
			break;
	}

	/* the list of arguments must be terminated by a null pointer */
	(*argv)[j] = NULL;
	return True;
}

/*
 * isAncestorPid--
 * Returns True if 'ancestor' is a parent (or grandparent, ...) of 'pid'
 * by walking /proc/<pid>/stat up the process tree.
 */
Bool IsProcessAncestor(pid_t ancestor, pid_t pid)
{
	int ppid;

	while (pid > 1) {
		ppid = 0;
		if (!getProcStatFields(pid, &ppid, NULL, NULL))
			return False;
		if ((pid_t)ppid == ancestor)
			return True;
		pid = (pid_t)ppid;
	}
	return False;
}

/*
 * isProcessBackground--
 * Check if a process was started in the background (detached with &).
 * Returns 1 if background, 0 if foreground, -1 on error.
 */
int IsBackgroundProcess(pid_t pid)
{
	int pgrp, tpgid;

	/* Fields in /proc/<pid>/stat:
	 * 1 pid, 2 comm, 3 state, 4 ppid, 5 pgrp, 6 session, 7 tty_nr,
	 * 8 tpgid  <-- foreground process group of controlling tty.
	 */
	if (!getProcStatFields(pid, NULL, &pgrp, &tpgid))
		return -1;

	/* If there is no controlling tty (tpgid 0 or -1), assume foreground. */
	if (tpgid <= 0)
		return 0;

	return (pgrp != tpgid) ? 1 : 0;
}

/*
 * findDescendantByCmd--
 * Scan /proc for a process whose command (basename) matches 'cmd' and
 * that is a descendant of 'parent'. Returns the pid or 0 if not found.
 */
pid_t FindDescendantPidByCmdName(pid_t parent, const char *cmd)
{
	DIR *d;
	struct dirent *entry;
	char path[256];
	FILE *f;
	pid_t pid = 0;
	const char *cmd_base;

	if (!cmd)
		return 0;
	cmd_base = baseName(cmd);

	d = opendir("/proc");
	if (!d)
		return 0;

	while ((entry = readdir(d)) != NULL) {
		char *end;
		long cand_long;
		pid_t cand;

		if (!isdigit((unsigned char)entry->d_name[0]))
			continue;

		errno = 0;
		cand_long = strtol(entry->d_name, &end, 10);
		if (errno != 0 || *end != '\0' || cand_long <= 0)
			continue;
		cand = (pid_t)cand_long;
		if ((long)cand != cand_long)
			continue;

		{
			char cmdline[1024];
			size_t namelen = strlen(entry->d_name);
			/* ensure /proc/<pid>/cmdline + NUL fits into path */
			if (namelen + 15 > sizeof(path))
				continue;
			snprintf(path, sizeof(path), "/proc/%s/cmdline", entry->d_name);
			f = fopen(path, "r");
			if (!f)
				continue;
			size_t r = fread(cmdline, 1, sizeof(cmdline) - 1, f);
			fclose(f);
			if (r > 0) {
				cmdline[r] = '\0';
				/* cmdline is NUL-separated; extract basename of first token */
				char *base = strrchr(cmdline, '/');

				if (base)
					base++;
				else
					base = cmdline;

				if (strcasecmp(base, cmd_base) == 0) {
					if (IsProcessAncestor(parent, cand)) {
						pid = cand;
						break;
					}
				}
			}
		}
	}

	closedir(d);
	return pid;
}
