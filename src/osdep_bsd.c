/*
 *  osdep_bsd.c
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 2010-2026 Window Maker Team
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

/*
 * Until FreeBSD gets their act together;
 * http://www.mail-archive.com/freebsd-hackers@freebsd.org/msg69469.html
 */
#if defined( FREEBSD )
#   undef _XOPEN_SOURCE
#endif

#if defined( FREEBSD ) || defined( DRAGONFLYBSD )
#   include <sys/types.h>
#else /* OPENBSD || NETBSD */
#   include <sys/param.h>
#endif
#include <sys/sysctl.h>

#include <assert.h>

#if defined( OPENBSD )
#   include <kvm.h>
#   include <limits.h>	/* _POSIX2_LINE_MAX */
#endif

#include <stdio.h>
#include <stdlib.h>

#if defined( OPENBSD )
#   include <string.h>
#endif

#include <unistd.h>

/* Additional headers for process introspection (IsProcessAncestor, etc.) */
#if defined( FREEBSD )
#   include <sys/user.h>		/* struct kinfo_proc (ki_* fields) */
#elif defined( DRAGONFLYBSD )
#   include <sys/kinfo.h>		/* struct kinfo_proc (kp_* fields) */
#endif
#include <strings.h>			/* strcasecmp */

#include <WINGs/WUtil.h>

#include "wconfig.h"
#include "osdep.h"

/*
 * IsProcessAncestor--
 * Returns True if 'ancestor' is a parent (or grandparent, ...) of 'pid'
 * by walking the process tree via the OS-native process info interface.
 */
bool IsProcessAncestor(pid_t ancestor, pid_t pid)
{
#if defined( OPENBSD )
	char kvmerr[_POSIX2_LINE_MAX];
	kvm_t *kd;
	struct kinfo_proc *kp;
	int procs;

	while (pid > 1) {
		pid_t ppid = -1;

		kd = kvm_openfiles(NULL, NULL, NULL, KVM_NO_FILES, kvmerr);
		if (!kd)
			return false;
		kp = kvm_getprocs(kd, KERN_PROC_PID, (int)pid, sizeof(*kp), &procs);
		if (kp && procs > 0)
			ppid = (pid_t)kp[0].p_ppid;
		kvm_close(kd);
		if (ppid < 0)
			return false;
		if (ppid == ancestor)
			return true;
		pid = ppid;
	}
	return false;
#elif defined( NETBSD )
	while (pid > 1) {
		struct kinfo_proc2 kp;
		int mib[6] = { CTL_KERN, KERN_PROC2, KERN_PROC_PID,
			       (int)pid, sizeof(kp), 1 };
		size_t count = sizeof(kp);
		pid_t ppid;

		if (sysctl(mib, 6, &kp, &count, NULL, 0) == -1 || count == 0)
			return false;
		ppid = (pid_t)kp.p_ppid;
		if (ppid == ancestor)
			return true;
		pid = ppid;
	}
	return false;
#elif defined( FREEBSD ) || defined( DRAGONFLYBSD )
	while (pid > 1) {
		struct kinfo_proc kp;
		int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)pid };
		size_t count = sizeof(kp);
		pid_t ppid;

		if (sysctl(mib, 4, &kp, &count, NULL, 0) == -1 || count == 0)
			return false;
#  if defined( FREEBSD )
		ppid = (pid_t)kp.ki_ppid;
#  else /* DRAGONFLYBSD */
		ppid = (pid_t)kp.kp_ppid;
#  endif
		if (ppid == ancestor)
			return true;
		pid = ppid;
	}
	return false;
#else
	(void)ancestor;
	(void)pid;
	return false;
#endif
}

/*
 * IsBackgroundProcess--
 * Returns 1 if the process was started in the background (shell '&'),
 * 0 if foreground, -1 on error. Determined by comparing the process
 * group against the terminal's foreground process group (tpgid).
 */
int IsBackgroundProcess(pid_t pid)
{
#if defined( OPENBSD )
	char kvmerr[_POSIX2_LINE_MAX];
	kvm_t *kd;
	struct kinfo_proc *kp;
	int procs, result;

	kd = kvm_openfiles(NULL, NULL, NULL, KVM_NO_FILES, kvmerr);
	if (!kd)
		return -1;
	kp = kvm_getprocs(kd, KERN_PROC_PID, (int)pid, sizeof(*kp), &procs);
	if (!kp || procs == 0) {
		kvm_close(kd);
		return -1;
	}
	/* p_tpgid is -1 when the process has no controlling terminal */
	if ((pid_t)kp[0].p_tpgid == (pid_t)-1 || kp[0].p_tpgid == 0)
		result = 0;
	else
		result = ((pid_t)kp[0].p__pgid != (pid_t)kp[0].p_tpgid) ? 1 : 0;
	kvm_close(kd);
	return result;
#elif defined( NETBSD )
	struct kinfo_proc2 kp;
	int mib[6] = { CTL_KERN, KERN_PROC2, KERN_PROC_PID,
		       (int)pid, sizeof(kp), 1 };
	size_t count = sizeof(kp);

	if (sysctl(mib, 6, &kp, &count, NULL, 0) == -1 || count == 0)
		return -1;
	if ((pid_t)kp.p_tpgid == (pid_t)-1 || kp.p_tpgid == 0)
		return 0;
	return ((pid_t)kp.p__pgid != (pid_t)kp.p_tpgid) ? 1 : 0;
#elif defined( FREEBSD )
	struct kinfo_proc kp;
	int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)pid };
	size_t count = sizeof(kp);

	if (sysctl(mib, 4, &kp, &count, NULL, 0) == -1 || count == 0)
		return -1;
	if (kp.ki_tpgid == (pid_t)-1 || kp.ki_tpgid == 0)
		return 0;
	return (kp.ki_pgid != kp.ki_tpgid) ? 1 : 0;
#elif defined( DRAGONFLYBSD )
	struct kinfo_proc kp;
	int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)pid };
	size_t count = sizeof(kp);

	if (sysctl(mib, 4, &kp, &count, NULL, 0) == -1 || count == 0)
		return -1;
	if (kp.kp_tpgid == (pid_t)-1 || kp.kp_tpgid == 0)
		return 0;
	return (kp.kp_pgid != kp.kp_tpgid) ? 1 : 0;
#else
	(void)pid;
	return 0;
#endif
}

/*
 * FindDescendantPidByCmdName--
 * Scan all processes for one whose command name (case-insensitive) matches
 * 'cmd' and that is a descendant of 'parent'. Returns the pid or 0 if not
 * found.
 *
 * Note: BSD stores a truncated command name (MAXCOMLEN characters) in the
 * process table. Names longer than MAXCOMLEN may not match.
 */
#if defined( OPENBSD )
static pid_t find_descendant_pid(pid_t parent, const char *cmd)
{
	char kvmerr[_POSIX2_LINE_MAX];
	kvm_t *kd;
	struct kinfo_proc *kp;
	int procs, i;
	pid_t found = 0;

	kd = kvm_openfiles(NULL, NULL, NULL, KVM_NO_FILES, kvmerr);
	if (!kd)
		return 0;
	kp = kvm_getprocs(kd, KERN_PROC_ALL, 0, sizeof(*kp), &procs);
	if (kp) {
		for (i = 0; i < procs; i++) {
			if (strcasecmp(kp[i].p_comm, cmd) == 0 &&
			    IsProcessAncestor(parent, (pid_t)kp[i].p_pid)) {
				found = (pid_t)kp[i].p_pid;
				break;
			}
		}
	}
	kvm_close(kd);
	return found;
}
#elif defined( NETBSD )
static pid_t find_descendant_pid(pid_t parent, const char *cmd)
{
	int mib[6] = { CTL_KERN, KERN_PROC2, KERN_PROC_ALL, 0,
		       sizeof(struct kinfo_proc2), 0 };
	size_t len;
	struct kinfo_proc2 *kp;
	int nprocs, i;
	pid_t found = 0;

	if (sysctl(mib, 6, NULL, &len, NULL, 0) == -1 || len == 0)
		return 0;
	kp = (struct kinfo_proc2 *)malloc(len);
	if (!kp)
		return 0;
	mib[5] = len / sizeof(struct kinfo_proc2);
	if (sysctl(mib, 6, kp, &len, NULL, 0) == -1) {
		free(kp);
		return 0;
	}
	nprocs = (int)(len / sizeof(struct kinfo_proc2));
	for (i = 0; i < nprocs; i++) {
		if (strcasecmp(kp[i].p_comm, cmd) == 0 &&
		    IsProcessAncestor(parent, (pid_t)kp[i].p_pid)) {
			found = (pid_t)kp[i].p_pid;
			break;
		}
	}
	free(kp);
	return found;
}
#elif defined( FREEBSD ) || defined( DRAGONFLYBSD )
static pid_t find_descendant_pid(pid_t parent, const char *cmd)
{
#if defined( FREEBSD )
	int mib[3] = { CTL_KERN, KERN_PROC, KERN_PROC_PROC };
#else /* DRAGONFLYBSD has no KERN_PROC_PROC, KERN_PROC_ALL is the full table */
	int mib[3] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL };
#endif
	size_t len;
	struct kinfo_proc *kp;
	int nprocs, i;
	pid_t found = 0;

	if (sysctl(mib, 3, NULL, &len, NULL, 0) == -1 || len == 0)
		return 0;
	kp = (struct kinfo_proc *)malloc(len);
	if (!kp)
		return 0;
	if (sysctl(mib, 3, kp, &len, NULL, 0) == -1) {
		free(kp);
		return 0;
	}
	nprocs = (int)(len / sizeof(struct kinfo_proc));
	for (i = 0; i < nprocs; i++) {
#  if defined( FREEBSD )
		if (strcasecmp(kp[i].ki_comm, cmd) == 0 &&
		    IsProcessAncestor(parent, (pid_t)kp[i].ki_pid)) {
			found = (pid_t)kp[i].ki_pid;
#  else /* DRAGONFLYBSD */
		if (strcasecmp(kp[i].kp_comm, cmd) == 0 &&
		    IsProcessAncestor(parent, (pid_t)kp[i].kp_pid)) {
			found = (pid_t)kp[i].kp_pid;
#  endif
			break;
		}
	}
	free(kp);
	return found;
}
#else
static pid_t find_descendant_pid(pid_t parent, const char *cmd)
{
	(void)parent;
	(void)cmd;
	return 0;
}
#endif

pid_t FindDescendantPidByCmdName(pid_t parent, const char *cmd)
{
	if (!cmd)
		return 0;

	return find_descendant_pid(parent, cmd);
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
/*
 * NetBSD, FreeBSD and DragonFlyBSD supply the necessary information via
 * sysctl(3). The result is a plain simple flat octet stream and its length.
 * The octet stream represents argv, with members separated by a null character.
 * The argv array returned by GetCommandForPid() consists of pointers into this
 * stream (which is stored in a static array, `args'). Net and Free/DFly only
 * differ slightly in the MIB vector given to sysctl(3). Free and DFly are
 * identical.
 *
 * OpenBSD supplies the necessary informationvia kvm(3) in the form of a real
 * argv array. This array is flattened to be in the same way as Net/Free/DFly
 * returns the arguments in the first place. This is done in order for the
 * storage (`args') to easily be made static, which means some memory bytes
 * are sacrificed to save hoops of memory management.
 */
bool GetCommandForPid(int pid, char ***argv, int *argc)
{
	/*
	 * it just returns failure if the sysctl calls fail; since there's
	 * apparently no harm done to the caller because of this, it seems
	 * more user-friendly than to bomb out.
	 */
	int j, mib[4];
	unsigned int i;
	size_t count;
	static char *args = NULL;
	static int argmax = 0;
#if defined( OPENBSD )
	char kvmerr[_POSIX2_LINE_MAX];	/* for kvm*() error reporting */
	int procs;			/* kvm_getprocs() */
	kvm_t *kd;
	struct kinfo_proc *kp;
	char **nargv;			/* kvm_getarg() */
#endif

	*argv = NULL;
	*argc = 0;

	/* the system-wide limit */
	if (argmax == 0) { /* it hopefully doesn't change at runtime *g* */
		mib[0] = CTL_KERN;
		mib[1] = KERN_ARGMAX;
		mib[2] = 0;
		mib[3] = 0;

		count = sizeof(argmax);
		if (sysctl(mib, 2, &argmax, &count, NULL, 0) == -1)
			return false;
	}

	/* if argmax is still 0, something went very seriously wrong */
	assert( argmax > 0);

	/* space for args; no need to free before returning even on errors */
	if (args == NULL)
		args = (char *)wmalloc(argmax);

#if defined( OPENBSD )
	/* kvm descriptor */
	kd = kvm_openfiles(NULL, NULL, NULL, KVM_NO_FILES, kvmerr);
	if (kd == NULL)
		return false;

	procs = 0;
	/* the process we are interested in */
	kp = kvm_getprocs(kd, KERN_PROC_PID, pid, sizeof(*kp), &procs);
	if (kp == NULL || procs == 0)
		/* if kvm_getprocs() bombs out or does not find the process */
		return false;

	/* get its argv */
	nargv = kvm_getargv(kd, kp, 0);
	if (nargv == NULL)
		return false;

	/* flatten nargv into args */
	count = 0;
	memset(args, 0, argmax);
	/*
	 * must have this much free space in `args' in order for the current
	 * iteration not to overflow it: we are at `count', and will append
	 * the next (*argc) arg and a nul (+1)
	 * technically, overflow (or truncation, which isn't handled) can not
	 * happen (should not, at least).
	 */
	#define ARGSPACE ( count + strlen(nargv[ (*argc) ] ) + 1 )
	while (nargv[*argc] && ARGSPACE < argmax ) {
		memcpy(args + count, nargv[*argc], strlen(nargv[*argc]));
		count += strlen(nargv[*argc]) + 1;
		(*argc)++;
	}
	#undef ARGSPACE
	/* by now *argc is correct as a byproduct */

	kvm_close(kd);
#else /* FREEBSD || NETBSD || DRAGONFLYBSD */

	mib[0] = CTL_KERN;
#if defined( NETBSD )
	mib[1] = KERN_PROC_ARGS;
	mib[2] = pid;
	mib[3] = KERN_PROC_ARGV;
#elif defined( FREEBSD ) || defined( DRAGONFLYBSD )
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_ARGS;
	mib[3] = pid;
#endif

	count = argmax;
	/* canary */
	*args = 0;
	if (sysctl(mib, 4, args, &count, NULL, 0) == -1 || *args == 0)
		return false;

	/* args is a flattened series of null-terminated strings */
	for (i = 0; i < count; i++)
		if (args[i] == '\0')
			(*argc)++;
#endif

	*argv = (char **)wmalloc(sizeof(char *) * (*argc + 1 /* term. null ptr */));
	(*argv)[0] = args;

	/* go through args, set argv[$next] to the beginning of each string */
	for (i = 0, j = 1; i < count; i++) {
		if (args[i] != '\0')
			continue;
		if (i < count - 1)
			(*argv)[j++] = &args[i + 1];
		if (j == *argc)
			break;
	}

	/* the list of arguments must be terminated by a null pointer */
	(*argv)[j] = NULL;
	return true;
}
