/* wxpaste.c- paste contents of cutbuffer to stdout
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include "../config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_SYS_SELECT_H
# include <sys/select.h>
#endif

#define MAXDATA		(4*1024*1024)

static const char *prog_name;

static void print_help(void)
{
	printf("Usage: %s [OPTIONS]\n", prog_name);
	puts("Copies data from X selection or cutbuffer to stdout.");
	puts("");
	puts("  -display display         display to use");
	puts("  --cutbuffer number       cutbuffer number to get data from");
	puts("  --selection [selection]  reads data from named selection instead of cutbuffer");
	puts("  -h, --help               display this help and exit");
	puts("  -v, --version            output version information and exit");
}


typedef Bool XEventMatchProc(XEvent *event, void *data);

static void addMilliseconds(struct timespec *deadline, int timeout_ms)
{
	deadline->tv_sec += timeout_ms / 1000;
	deadline->tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
	if (deadline->tv_nsec >= 1000000000L) {
		deadline->tv_sec++;
		deadline->tv_nsec -= 1000000000L;
	}
}

static int remainingMilliseconds(const struct timespec *deadline)
{
	struct timespec now;
	time_t sec;
	long nsec;

	clock_gettime(CLOCK_MONOTONIC, &now);
	sec = deadline->tv_sec - now.tv_sec;
	nsec = deadline->tv_nsec - now.tv_nsec;
	if (nsec < 0) {
		sec--;
		nsec += 1000000000L;
	}
	if (sec < 0)
		return 0;
	if (sec > INT_MAX / 1000)
		return INT_MAX;
	return (int)(sec * 1000 + (nsec + 999999L) / 1000000L);
}

static int waitForXEvent(Display *dpy, int timeout_ms, XEventMatchProc *match, void *data, XEvent *out)
{
	struct timespec deadline;
	Bool bounded = timeout_ms >= 0;

	if (bounded) {
		clock_gettime(CLOCK_MONOTONIC, &deadline);
		addMilliseconds(&deadline, timeout_ms);
	}

	for (;;) {
		while (XPending(dpy) > 0) {
			XNextEvent(dpy, out);
			if ((*match)(out, data))
				return 1;
		}

		struct timeval timeout;
		struct timeval *timeout_ptr = NULL;
		fd_set fdset;
		int ready;

		if (bounded) {
			int remaining = remainingMilliseconds(&deadline);
			if (remaining <= 0)
				return 0;
			timeout.tv_sec = remaining / 1000;
			timeout.tv_usec = (remaining % 1000) * 1000;
			timeout_ptr = &timeout;
		}

		FD_ZERO(&fdset);
		FD_SET(ConnectionNumber(dpy), &fdset);
		XFlush(dpy);
		ready = select(ConnectionNumber(dpy) + 1, &fdset, NULL, NULL, timeout_ptr);
		if (ready < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (ready == 0)
			return 0;
	}
}

static Bool timestampEventMatches(XEvent *event, void *data)
{
	Window win = *(Window *)data;

	return event->type == PropertyNotify && event->xproperty.window == win;
}

static Time getTimestamp(Display * dpy, Window win)
{
	XEvent ev;

	/* Generate a server timestamp on our private window and bound the
	 * wait so a broken display connection cannot wedge wxpaste forever. */
	XSelectInput(dpy, win, PropertyChangeMask);
	XStoreName(dpy, win, "wxpaste");

	if (waitForXEvent(dpy, 1000, timestampEventMatches, &win, &ev) > 0)
		return ev.xproperty.time;

	return CurrentTime;
}

typedef struct SelectionWaitData {
	Window requestor;
	Atom selection;
} SelectionWaitData;

static Bool selectionNotifyMatches(XEvent *event, void *data)
{
	SelectionWaitData *wait = data;

	return event->type == SelectionNotify &&
		event->xselection.requestor == wait->requestor &&
		event->xselection.selection == wait->selection;
}

static char *fetchSelection(Display *dpy, const char *selection, int *length)
{
	Atom selatom = XInternAtom(dpy, selection, False);
	Atom clipatom = XInternAtom(dpy, "CLIPBOARD", False);
	Time now;
	XEvent ev;
	Window win;
	SelectionWaitData wait;
	char *result = NULL;

	win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 1, 1, 0, 0, 0);
	/* The ICCCM says CurrentTime is not valid for XConvertSelection(),
	 * so generate a timestamp on our requestor window first. */
	now = getTimestamp(dpy, win);

	wait.requestor = win;
	wait.selection = selatom;
	XConvertSelection(dpy, selatom, XA_STRING, clipatom, win, now);

	if (waitForXEvent(dpy, 1000, selectionNotifyMatches, &wait, &ev) > 0 &&
	    ev.xselection.property != None) {
		Atom rtype;
		int bits;
		unsigned long len, bytes;
		unsigned char *data = NULL;

		if (XGetWindowProperty(dpy, win, ev.xselection.property, 0, MAXDATA / 4, False,
				       XA_STRING, &rtype, &bits, &len, &bytes, &data) == Success &&
		    rtype == XA_STRING && bits == 8 && bytes == 0 && len <= INT_MAX) {
			*length = (int)len;
			result = (char *)data;
		} else if (data) {
			XFree(data);
		}
	}

	XDestroyWindow(dpy, win);
	return result;
}

int main(int argc, char **argv)
{
	Display *dpy;
	int i, l = 0;
	int buffer = 0;
	char *buf;
	int status;
	char *display_name = "";
	char *selection_name = NULL;

	prog_name = argv[0];
	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			if (argv[i][1] == 'h' || strcmp(argv[i], "--help") == 0) {
				print_help();
				exit(0);
			} else if (argv[i][1] == 'v' || strcmp(argv[i], "--version") == 0) {
				printf("%s (Window Maker %s)\n", prog_name, VERSION);
				exit(0);
			} else if (strcmp(argv[i], "-selection") == 0 || strcmp(argv[i], "--selection") == 0) {
				if (i < argc - 1) {
					selection_name = argv[++i];
				} else {
					selection_name = "PRIMARY";
				}
			} else if (strcmp(argv[i], "-display") == 0) {
				if (i < argc - 1) {
					display_name = argv[++i];
				} else {
					print_help();
					exit(0);
				}
			} else if (strcmp(argv[i], "-cutbuffer") == 0 || strcmp(argv[i], "--cutbuffer") == 0) {
				if (i < argc - 1) {
					i++;
					if (sscanf(argv[i], "%i", &buffer) != 1) {
						fprintf(stderr, "%s: could not convert \"%s\" to int\n",
						        prog_name, argv[i]);
						exit(1);
					}
					if (buffer < 0 || buffer > 7) {
						fprintf(stderr, "%s: invalid buffer number %i\n", prog_name, buffer);
						exit(1);
					}
				} else {
					fprintf(stderr, "%s: invalid argument '%s'\n", prog_name, argv[i]);
					fprintf(stderr, "Try '%s --help' for more information.\n", prog_name);
					exit(1);
				}
			}
		} else {
			fprintf(stderr, "%s: invalid argument '%s'\n", prog_name, argv[i]);
			fprintf(stderr, "Try '%s --help' for more information.\n", prog_name);
			exit(1);
		}
	}
	dpy = XOpenDisplay(display_name);
	if (!dpy) {
		fprintf(stderr, "%s: could not open display \"%s\"\n", prog_name, XDisplayName(display_name));
		exit(1);
	}

	if (selection_name) {
		buf = fetchSelection(dpy, selection_name, &l);
	} else {
		buf = NULL;
	}

	if (buf == NULL) {
		buf = XFetchBuffer(dpy, &l, buffer);
	}

	if (buf == NULL) {
		status = 1;
	} else {
		if (write(STDOUT_FILENO, buf, l) == -1)
			status = errno;
		else
			status = 0;
	}
	XCloseDisplay(dpy);
	exit(status);
}
