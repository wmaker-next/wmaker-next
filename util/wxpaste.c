/* wxpaste.c- paste contents of cutbuffer to stdout
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2009-2026 Window Maker Team
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
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include "clip_common.h"
#include "xcconn.h"

#define MAXDATA		(4*1024*1024)
#define SELECTION_WAIT_MS 1000

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

/* Generate a server timestamp on our private window and bound the
 * wait so a broken display connection cannot wedge wxpaste forever. */
static xcb_timestamp_t getTimestamp(xcb_connection_t *c, xcb_window_t win)
{
	xcb_generic_event_t *ev;
	xcb_property_notify_event_t *pn;
	xcb_timestamp_t t = XCB_CURRENT_TIME;
	long long deadline = clip_monotonic_ms() + SELECTION_WAIT_MS;
	int wait_ms;
	bool got = false;

	xcb_change_window_attributes(c, win, XCB_CW_EVENT_MASK,
				     (const uint32_t[]){ XCB_EVENT_MASK_PROPERTY_CHANGE });
	xcb_change_property(c, XCB_PROP_MODE_REPLACE, win, XCB_ATOM_WM_NAME,
			    XCB_ATOM_STRING, 8, 7, "wxpaste");
	xcb_flush(c);

	while (!got) {
		wait_ms = (int)(deadline - clip_monotonic_ms());
		if (wait_ms <= 0)
			break;
		if (!clip_wait_event(c, &ev, wait_ms))
			break;
		pn = (xcb_property_notify_event_t *) ev;
		if ((ev->response_type & 0x7f) == XCB_PROPERTY_NOTIFY && pn->window == win) {
			t = pn->time;
			got = true;
		}
		free(ev);
	}
	return t;
}

typedef struct SelectionWaitData {
	xcb_window_t requestor;
	xcb_atom_t selection;
} SelectionWaitData;

static bool selectionNotifyMatches(xcb_generic_event_t *ev, SelectionWaitData *wait)
{
	xcb_selection_notify_event_t *sn;

	if ((ev->response_type & 0x7f) != XCB_SELECTION_NOTIFY)
		return false;
	sn = (xcb_selection_notify_event_t *) ev;
	return sn->requestor == wait->requestor && sn->selection == wait->selection;
}

/* Returns 1 on success, which may be an empty selection, 0 on failure. */
static int fetchSelection(xcb_connection_t *c, int screen, const char *selection,
			  char **data_out, int *length)
{
	xcb_intern_atom_cookie_t ck1, ck2, ck3;
	xcb_intern_atom_reply_t *r1, *r2, *r3;
	xcb_get_selection_owner_cookie_t ock;
	xcb_get_selection_owner_reply_t *orp;
	xcb_atom_t selatom, clipatom, incratom, prop = XCB_ATOM_NONE;
	xcb_window_t win;
	xcb_timestamp_t now;
	xcb_generic_event_t *ev;
	SelectionWaitData wait;
	unsigned char *data;
	xcb_atom_t type;
	size_t dlen;
	long long deadline;
	int wait_ms;
	int had_owner = 0;
	bool got = false;

	*data_out = NULL;
	*length = 0;

	ck1 = xcb_intern_atom(c, 0, strlen(selection), selection);
	ck2 = xcb_intern_atom(c, 0, strlen("CLIPBOARD"), "CLIPBOARD");
	ck3 = xcb_intern_atom(c, 0, strlen("INCR"), "INCR");
	r1 = xcb_intern_atom_reply(c, ck1, NULL);
	r2 = xcb_intern_atom_reply(c, ck2, NULL);
	r3 = xcb_intern_atom_reply(c, ck3, NULL);
	selatom = r1 ? r1->atom : XCB_ATOM_NONE;
	clipatom = r2 ? r2->atom : XCB_ATOM_NONE;
	incratom = r3 ? r3->atom : XCB_ATOM_NONE;
	free(r1);
	free(r2);
	free(r3);
	if (selatom == XCB_ATOM_NONE || clipatom == XCB_ATOM_NONE)
		return 0;

	win = xcb_generate_id(c);
	xcb_create_window(c, XCB_COPY_FROM_PARENT, win, xcconn_root(c, screen),
			  0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
			  XCB_COPY_FROM_PARENT, 0, NULL);

	/* The ICCCM says CurrentTime is not valid for
	 * xcb_convert_selection(), so generate a timestamp on our
	 * requestor window first. */
	now = getTimestamp(c, win);

	wait.requestor = win;
	wait.selection = selatom;
	/* a property-less SelectionNotify then means the owner refused,
	 * which must not masquerade as an empty selection */
	ock = xcb_get_selection_owner(c, selatom);
	orp = xcb_get_selection_owner_reply(c, ock, NULL);
	if (orp) {
		had_owner = orp->owner != XCB_ATOM_NONE;
		free(orp);
	}
	xcb_convert_selection(c, win, selatom, XCB_ATOM_STRING, clipatom, now);
	xcb_flush(c);

	deadline = clip_monotonic_ms() + SELECTION_WAIT_MS;
	while (!got) {
		wait_ms = (int)(deadline - clip_monotonic_ms());
		if (wait_ms <= 0)
			break;
		if (!clip_wait_event(c, &ev, wait_ms))
			break;
		if (selectionNotifyMatches(ev, &wait)) {
			prop = ((xcb_selection_notify_event_t *) ev)->property;
			got = true;
		}
		free(ev);
	}

	if (got && prop != XCB_ATOM_NONE) {
		data = clip_retrieve_property(c, win, prop, incratom, MAXDATA,
					      CLIP_DEFAULT_INCR_SEND_TIMEOUT_MS,
					      NULL, NULL, &type, &dlen);
		if (!data) {
			xcb_destroy_window(c, win);
			return 0;
		}
		*data_out = (char *)data;
		if (dlen <= INT_MAX)
			*length = (int)dlen;
	} else if (got && !had_owner) {
		*data_out = NULL;
		*length = 0;
	} else {
		/* refused by the owner, or no reply */
		xcb_destroy_window(c, win);
		return 0;
	}

	xcb_destroy_window(c, win);
	return 1;
}

static char *fetchBuffer(xcb_connection_t *c, int buffer, int *length)
{
	xcb_get_property_cookie_t ck;
	xcb_get_property_reply_t *rep;
	char *out = NULL;

	*length = 0;

	/* long_length is in 32-bit units, 10000000 is Xlib's own cap.
	 * Xlib's XFetchBuffer always reads screen 0's root */
	ck = xcb_get_property(c, false, xcconn_root(c, 0),
			      XCB_ATOM_CUT_BUFFER0 + buffer,
			      XCB_ATOM_STRING, 0, 10000000);
	rep = xcb_get_property_reply(c, ck, NULL);
	if (rep && rep->type == XCB_ATOM_STRING && rep->format == 8 &&
	    rep->bytes_after == 0 && rep->value_len > 0 &&
	    rep->value_len <= INT_MAX) {
		size_t n = rep->value_len;

		out = malloc(n + 1);
		if (out) {
			memcpy(out, xcb_get_property_value(rep), n);
			out[n] = '\0';
			*length = (int) n;
		}
	}
	free(rep);
	return out;
}

int main(int argc, char **argv)
{
	xcb_connection_t *c;
	int i, l = 0;
	int sn = 0;
	int buffer = 0;
	char *buf;
	int status;
	size_t done;
	ssize_t n;
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
	c = xcconn_open(display_name, prog_name, 1, &sn);

	status = 0;
	if (selection_name) {
		if (!fetchSelection(c, sn, selection_name, &buf, &l)) {
			xcb_disconnect(c);
			exit(1);
		}
	} else {
		buf = fetchBuffer(c, buffer, &l);
		if (buf == NULL)
			status = 1;
	}

	if (status == 0) {
		done = 0;
		while (done < (size_t)l) {
			n = write(STDOUT_FILENO, buf + done, (size_t)l - done);

			if (n < 0) {
				if (errno == EINTR)
					continue;
				status = errno;
				break;
			}
			if (n == 0) {
				status = EIO;
				break;
			}
			done += (size_t)n;
		}
	}
	xcb_disconnect(c);
	exit(status);
}
