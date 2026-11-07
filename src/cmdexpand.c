/*
 *  Window Maker window manager
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
 */

/*
 * ExpandOptions() was extracted from misc.c so it can be unit-tested
 * without dragging in X11, dialog.c or wxkb.c dependencies. The two
 * helpers it calls (getselection/getuserinput) are now non-static and
 * declared in misc.h; the test build provides its own stubs.
 */
#include "wconfig.h"

#include <stdlib.h>
#include <string.h>

#include <WINGs/WUtil.h>
#include <WINGs/WINGsP.h>

#include "window.h"	/* WWindow for scr->focused_window fields */
#include "misc.h"	/* ExpandOptions decl + getselection/getuserinput */
#include "screen.h"

#define S_NORMAL 0
#define S_ESCAPE 1
#define S_OPTION 2

/*
 * state    	input   new-state	output
 * NORMAL	%	OPTION		<nil>
 * NORMAL	\	ESCAPE		<nil>
 * NORMAL	etc.	NORMAL		<input>
 * ESCAPE	any	NORMAL		<input>
 * OPTION	s	NORMAL		<selection buffer>
 * OPTION	w	NORMAL		<selected window id>
 * OPTION	a	NORMAL		<input text>
 * OPTION	d	NORMAL		<OffiX DND selection object>
 * OPTION	W	NORMAL		<current workspace>
 * OPTION	etc.	NORMAL		%<input>
 */
#define TMPBUFSIZE 64
char *ExpandOptions(WScreen *scr, const char *cmdline)
{
	int ptr, optr, state, len, olen;
	char *out, *nout;
	char *selection = NULL;
	char *user_input = NULL;
	char tmpbuf[TMPBUFSIZE];
	int slen;

	len = strlen(cmdline);
	olen = len + 1;
	out = wmalloc(olen);
	if (!out) {
		wwarning(_("out of memory during expansion of \"%s\""), cmdline);
		return NULL;
	}
	*out = 0;
	ptr = 0;		/* input line pointer */
	optr = 0;		/* output line pointer */
	state = S_NORMAL;
	while (ptr < len) {
		switch (state) {
		case S_NORMAL:
			switch (cmdline[ptr]) {
			case '\\':
				state = S_ESCAPE;
				break;
			case '%':
				state = S_OPTION;
				break;
			default:
				state = S_NORMAL;
				out[optr++] = cmdline[ptr];
				break;
			}
			break;
		case S_ESCAPE:
			switch (cmdline[ptr]) {
			case 'n':
				out[optr++] = 10;
				break;

			case 'r':
				out[optr++] = 13;
				break;

			case 't':
				out[optr++] = 9;
				break;

			default:
				out[optr++] = cmdline[ptr];
			}
			state = S_NORMAL;
			break;
		case S_OPTION:
			state = S_NORMAL;
			switch (cmdline[ptr]) {
			case 'w':
				if (scr->focused_window && scr->focused_window->flags.focused) {
					snprintf(tmpbuf, sizeof(tmpbuf), "0x%x",
						 (unsigned int)scr->focused_window->client_win);
					slen = strlen(tmpbuf);
					olen += slen;
					nout = wrealloc(out, olen);
					if (!nout) {
						wwarning(_("out of memory during expansion of '%s' for command \"%s\""), "%w", cmdline);
						goto error;
					}
					out = nout;
					memcpy(out + optr, tmpbuf, slen);
					optr += slen;
				} else {
					out[optr++] = ' ';
				}
				break;

			case 'W':
				snprintf(tmpbuf, sizeof(tmpbuf), "0x%x", (unsigned int)scr->current_workspace + 1);
				slen = strlen(tmpbuf);
				olen += slen;
				nout = wrealloc(out, olen);
				if (!nout) {
					wwarning(_("out of memory during expansion of '%s' for command \"%s\""), "%W", cmdline);
					goto error;
				}
				out = nout;
				memcpy(out + optr, tmpbuf, slen);
				optr += slen;
				break;

			case 'a':
			case 'A':
				ptr++;
				user_input = getuserinput(scr, cmdline, &ptr, cmdline[ptr-1] == 'A');
				if (user_input) {
					slen = strlen(user_input);
					olen += slen;
					nout = wrealloc(out, olen);
					if (!nout) {
						wwarning(_("out of memory during expansion of '%s' for command \"%s\""), "%a", cmdline);
						wfree(user_input);
						goto error;
					}
					out = nout;
					memcpy(out + optr, user_input, slen);
					optr += slen;
					wfree(user_input);
				} else {
					/* Not an error, but user has Canceled the dialog box.
					 * This will make the command to not be performed. */
					goto error;
				}
				break;

#ifdef USE_DOCK_XDND
			case 'd':
				if (!scr->xdestring) {
					scr->flags.dnd_data_convertion_status = 1;
					goto error;
				}
				slen = strlen(scr->xdestring);
				if (scr->xdest_is_gnustep)
					slen += 12;	/* "-GSFilePath " */
				olen += slen;
				nout = wrealloc(out, olen);
				if (!nout) {
					wwarning(_("out of memory during expansion of '%s' for command \"%s\""), "%d", cmdline);
					goto error;
				}
				out = nout;
				if (scr->xdest_is_gnustep) {
					memcpy(out + optr, "-GSFilePath ", 12);
					optr += 12;
					slen -= 12;
				}
				memcpy(out + optr, scr->xdestring, slen);
				optr += slen;
				break;
#endif	/* USE_DOCK_XDND */

			case 's':
				if (!selection) {
					selection = getselection(scr);
				}
				if (!selection) {
					wwarning(_("selection not available"));
					goto error;
				}
				slen = strlen(selection);
#ifdef USE_DOCK_XDND
				if (scr->xdest_is_gnustep)
					slen += 12;	/* "-GSFilePath " */
#endif
				olen += slen;
				nout = wrealloc(out, olen);
				if (!nout) {
					wwarning(_("out of memory during expansion of '%s' for command \"%s\""), "%s", cmdline);
					goto error;
				}
				out = nout;
#ifdef USE_DOCK_XDND
				if (scr->xdest_is_gnustep) {
					memcpy(out + optr, "-GSFilePath ", 12);
					optr += 12;
					slen -= 12;
				}
#endif
				memcpy(out + optr, selection, slen);
				optr += slen;
				break;

			default:
				out[optr++] = '%';
				out[optr++] = cmdline[ptr];
			}
			break;
		}
		out[optr] = 0;
		ptr++;
	}
	if (selection)
		wfree(selection);
	return out;

 error:
	wfree(out);
	if (selection)
		wfree(selection);
	return NULL;
}
