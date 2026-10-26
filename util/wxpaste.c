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
	bool got = false;

	xcb_change_window_attributes(c, win, XCB_CW_EVENT_MASK,
				     (const uint32_t[]){ XCB_EVENT_MASK_PROPERTY_CHANGE });
	xcb_change_property(c, XCB_PROP_MODE_REPLACE, win, XCB_ATOM_WM_NAME,
			    XCB_ATOM_STRING, 8, 7, "wxpaste");
	xcb_flush(c);

	while (!got) {
		if (!clip_wait_event(c, &ev, 1000)) {
			break;
		}
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

static char *fetchSelection(xcb_connection_t *c, int screen, const char *selection, int *length)
{
	xcb_intern_atom_cookie_t ck1, ck2;
	xcb_intern_atom_reply_t *r1, *r2;
	xcb_atom_t selatom, clipatom, prop;
	xcb_window_t win;
	xcb_timestamp_t now;
	xcb_get_property_cookie_t pck;
	xcb_get_property_reply_t *prep;
	xcb_generic_event_t *ev;
	SelectionWaitData wait;
	char *result = NULL;
	bool got = false;

	*length = 0;

	ck1 = xcb_intern_atom(c, 0, strlen(selection), selection);
	ck2 = xcb_intern_atom(c, 0, strlen("CLIPBOARD"), "CLIPBOARD");
	r1 = xcb_intern_atom_reply(c, ck1, NULL);
	r2 = xcb_intern_atom_reply(c, ck2, NULL);
	selatom = r1 ? r1->atom : XCB_ATOM_NONE;
	clipatom = r2 ? r2->atom : XCB_ATOM_NONE;
	free(r1);
	free(r2);
	if (selatom == XCB_ATOM_NONE || clipatom == XCB_ATOM_NONE)
		return NULL;

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
	xcb_convert_selection(c, win, selatom, XCB_ATOM_STRING, clipatom, now);
	xcb_flush(c);

	while (!got) {
		if (!clip_wait_event(c, &ev, 1000)) {
			break;
		}
		if (selectionNotifyMatches(ev, &wait)) {
			prop = ((xcb_selection_notify_event_t *) ev)->property;
			got = true;
		}
		free(ev);
	}

	if (got && prop != XCB_ATOM_NONE) {
		pck = xcb_get_property(c, false, win, prop, XCB_ATOM_STRING, 0, MAXDATA / 4);
		prep = xcb_get_property_reply(c, pck, NULL);
		if (prep && prep->type == XCB_ATOM_STRING && prep->format == 8 &&
		    prep->bytes_after == 0 && prep->value_len > 0 &&
		    prep->value_len <= INT_MAX) {
			size_t n = prep->value_len;

			result = malloc(n + 1);
			if (result) {
				memcpy(result, xcb_get_property_value(prep), n);
				result[n] = '\0';
				*length = (int) n;
			}
		}
		free(prep);
	}

	xcb_destroy_window(c, win);
	return result;
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

	if (selection_name)
		buf = fetchSelection(c, sn, selection_name, &l);
	else
		buf = NULL;

	if (buf == NULL)
		buf = fetchBuffer(c, buffer, &l);

	if (buf == NULL) {
		status = 1;
	} else {
		if (write(STDOUT_FILENO, buf, l) == -1)
			status = errno;
		else
			status = 0;
	}
	xcb_disconnect(c);
	exit(status);
}
