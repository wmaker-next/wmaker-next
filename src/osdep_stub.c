
#include <sys/utsname.h>

#include <WINGs/WUtil.h>

#include "wconfig.h"
#include "osdep.h"

bool GetCommandForPid(int pid, char ***argv, int *argc)
{
	static int notified = 0;

	if (!notified) {
		struct utsname un;

		/* The comment below is placed in the PO file by xgettext to help translator */
		if (uname(&un) != -1) {
			/*
			 *  1st %s is a function name
			 *  2nd %s is an email address
			 *  3rd %s is the name of the operating system
			 */
			wwarning(_("%s is not implemented on this platform; "
			           "tell %s you are running %s release %s version %s"),
			         __FUNCTION__, PACKAGE_BUGREPORT,
				un.sysname, un.release, un.version);
			notified = 1;
		}

	}

	*argv = NULL;
	*argc = 0;

	return false;
}


bool IsProcessAncestor(pid_t ancestor, pid_t pid)
{
	(void)ancestor;
	(void)pid;

	return false;
}


int IsBackgroundProcess(pid_t pid)
{
	(void)pid;

	return 0;
}


pid_t FindDescendantPidByCmdName(pid_t parent, const char *cmd)
{
	(void)parent;
	(void)cmd;

	return 0;
}
