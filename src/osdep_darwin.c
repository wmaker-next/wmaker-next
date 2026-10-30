/*
 *  osdep_darwin.c
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

#include <sys/types.h>
#include <sys/sysctl.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <WINGs/WUtil.h>

#include "wconfig.h"
#include "osdep.h"

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

/*
 * copy argc and argv for an existing process identified by `pid'
 * into suitable storage given in ***argv and *argc.
 *
 * subsequent calls use the same static area for argv and argc.
 *
 * returns 0 for failure, in which case argc := 0 and argv := NULL
 * returns 1 for success
 */
bool GetCommandForPid(int pid, char ***argv, int *argc)
#ifdef KERN_PROCARGS2
{
	int mib[4];
	unsigned int idx;
	size_t count;
	static char *args = NULL;
	static int argmax = 0;

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
	assert(argmax > 0);

	/* space for args; no need to free before returning even on errors */
	if (args == NULL)
		args = (char *)wmalloc(argmax);

	/* get process args */
	mib[0] = CTL_KERN;
	mib[1] = KERN_PROCARGS2;
	mib[2] = pid;

	count = argmax;
	if (sysctl(mib, 3, args, &count, NULL, 0) == -1 || count == 0)
		return false;

	/* get argc, skip */
	memcpy(argc, args, sizeof(*argc));
	idx = sizeof(*argc);

	while (args[idx++] != '\0')		/* skip execname */
		;
	while (args[idx] == '\0')		/* padding too */
		idx++;
	/* args[idx] is at at begininng of args now */

    int found = 0;
    char *p = &args[idx];
    while(found < *argc)
    {
        while(*p != '\0') p++; // look for the next \0
        while(*p == '\0') p++; // skip over padding \0s
        found++;

        // Don’t overrun!
        if (p-args >= argmax)
        {
            return false;
        }
    }
    // At this point, p points to the last \0 in the source array. 

    // Buffer needed for the strings
    unsigned stringbuf_size = p - &args[idx];

    // Buffer needed for the pointers (plus one terminating NULL)
    unsigned pointerbuf_size = sizeof(char *) * (*argc + 1);
    
    *argv = wmalloc(pointerbuf_size + stringbuf_size);
    char* stringstart = (char *)(*argv) + pointerbuf_size;

    memcpy(stringstart, &args[idx], stringbuf_size);

    found = 0;
    p = stringstart;
    while(found < *argc)
    {
        (*argv)[found] = p;
                
        while(*p != '\0') p++; // look for the next \0
        while(*p == '\0') p++; // skip over padding \0s
        
        found++;
    }
    (*argv)[found] = NULL; // Terminating NULL
    
	return true;
}
#else /* !KERN_PROCARGS2 */
{
	*argv = NULL;
	*argc = 0;

	return false;
}
#endif
