/*
 *  Window Maker window manager
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <https://www.gnu.org/licenses/>.
 */
#include "wconfig.h"

#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <pwd.h>
#include <math.h>
#include <time.h>
#include <errno.h>

#include <WINGs/WUtil.h>
#include <WINGs/WINGsP.h>
#include <WINGs/WXProp.h>
#include <wraster.h>

#include "window.h"
#include "misc.h"
#include "WindowMaker.h"
#include "GNUstep.h"
#include "screen.h"
#include "wcore.h"
#include "framewin.h"
#include "dialog.h"
#include "dialog_run.h"
#include "xutil.h"
#include "xinerama.h"
#include "wxkb.h"
#include "main.h"
#include "event.h"
#include "shutdown.h"
#include "xsmp.h"


#define ICON_SIZE w_global.config->icon_size

/**** Local prototypes *****/
static void UnescapeWM_CLASS(const char *str, char **name, char **class);

/* XFetchName Wrapper */
bool wFetchName(xcb_connection_t *dpy, xcb_window_t win, char **winname)
{
	*winname = WXGetWMName(dpy, win);
	return *winname != NULL;
}

/* XGetIconName Wrapper */
bool wGetIconName(xcb_connection_t *dpy, xcb_window_t win, char **iconname)
{
	*iconname = WXGetWMIconName(dpy, win);
	return *iconname != NULL;
}

int WMStrEqual(const char *x, const char *y)
{
	if ((x == NULL) && (y == NULL))
		return 1;

	if ((x == NULL) || (y == NULL))
		return 0;

	return (strcmp(x, y) == 0);
}

int WMPLGetBool(WMPropList *value)
{
	char *val;

	if (!WMIsPLString(value))
		return 0;

	val = WMGetFromPLString(value);
	if (val == NULL)
		return 0;

	if ((val[1] == '\0' &&
		 (val[0] == 'y' || val[0] == 'Y' || val[0] == 'T' ||
		  val[0] == 't' || val[0] == '1')) ||
		(strcasecmp(val, "YES") == 0 || strcasecmp(val, "TRUE") == 0)) {
		return 1;
	} else if ((val[1] == '\0' &&
				(val[0] == 'n' || val[0] == 'N' || val[0] == 'F' ||
				 val[0] == 'f' || val[0] == '0')) ||
			   (strcasecmp(val, "NO") == 0 || strcasecmp(val, "FALSE") == 0)) {
		return 0;
	} else {
		wwarning(_("can't convert \"%s\" to boolean"), val);
		return 0;
	}
}

static void eatExpose(void)
{
	xcb_generic_event_t *event = NULL, *foo = NULL;

	/* compress all expose events into a single one */

	if (WXEvqTakeMask(wdisp->conn, XCB_EVENT_MASK_EXPOSURE, XCB_NONE, &event)) {
		/* ignore other exposure events for this window */
		while (WXEvqTakeMask(wdisp->conn, XCB_EVENT_MASK_EXPOSURE, (((xcb_expose_event_t *)event)->window), &foo))
			free(foo);
		/* eat exposes for other windows */
		eatExpose();

		(((xcb_expose_event_t *)event)->count) = 0;
		WXEvqPutBack(event);
	}
}

void move_window(xcb_window_t win, int from_x, int from_y, int to_x, int to_y)
{
#ifdef USE_ANIMATIONS
	if (w_global.config->no_animations || w_global.display_off)
		xcb_configure_window(wdisp->conn, win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ to_x, to_y });
	else
		slide_window(win, from_x, from_y, to_x, to_y);
#else
	xcb_configure_window(wdisp->conn, win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ to_x, to_y });

	/* Tell the compiler it is normal that those parameters are not used in this case */
	(void) from_x;
	(void) from_y;
#endif
}

/* wins is an array of Window, sorted along the stack axis, the first
 * is going to be moved from (from_x,from_y) to (to_x,to_y) and the
 * following windows are offset by one icon on that axis.
 * Entries set to XCB_NONE are skipped. */
void slide_windows(xcb_window_t wins[], int n, int from_x, int from_y, int to_x, int to_y,
		   char stack_axis)
{
	time_t time0 = time(NULL);
	float dx, dy, x = from_x, y = from_y, px, py;
	bool is_dx_nul, is_dy_nul;
	int dx_is_bigger = 0, dx_int, dy_int;
	int slide_delay, slide_steps, slide_slowdown;
	int off_x, off_y;
	int i;

	if (stack_axis == 'y') {
		off_x = 0;
		off_y = ICON_SIZE;
	} else {
		off_x = ICON_SIZE;
		off_y = 0;
	}

	/* animation parameters */
	static const struct {
		int delay;
		int steps;
		int slowdown;
	} apars[5] = {
		{ICON_SLIDE_DELAY_UF, ICON_SLIDE_STEPS_UF, ICON_SLIDE_SLOWDOWN_UF},
		{ICON_SLIDE_DELAY_F,  ICON_SLIDE_STEPS_F,  ICON_SLIDE_SLOWDOWN_F},
		{ICON_SLIDE_DELAY_M,  ICON_SLIDE_STEPS_M,  ICON_SLIDE_SLOWDOWN_M},
		{ICON_SLIDE_DELAY_S,  ICON_SLIDE_STEPS_S,  ICON_SLIDE_SLOWDOWN_S},
		{ICON_SLIDE_DELAY_US, ICON_SLIDE_STEPS_US, ICON_SLIDE_SLOWDOWN_US}
	};

	slide_slowdown = apars[(int)w_global.config->icon_slide_speed].slowdown;
	slide_steps = apars[(int)w_global.config->icon_slide_speed].steps;
	slide_delay = apars[(int)w_global.config->icon_slide_speed].delay;

	dx_int = to_x - from_x;
	dy_int = to_y - from_y;
	is_dx_nul = (dx_int == 0);
	is_dy_nul = (dy_int == 0);
	dx = (float) dx_int;
	dy = (float) dy_int;

	if (abs(dx_int) > abs(dy_int)) {
		dx_is_bigger = 1;
	}

	if (dx_is_bigger) {
		px = dx / slide_slowdown;
		if (px < slide_steps && px > 0)
			px = slide_steps;
		else if (px > -slide_steps && px < 0)
			px = -slide_steps;
		py = (is_dx_nul ? 0.0F : px * dy / dx);
	} else {
		py = dy / slide_slowdown;
		if (py < slide_steps && py > 0)
			py = slide_steps;
		else if (py > -slide_steps && py < 0)
			py = -slide_steps;
		px = (is_dy_nul ? 0.0F : py * dx / dy);
	}

	while (((int)x) != to_x ||
			 ((int)y) != to_y) {
		x += px;
		y += py;
		if ((px < 0 && (int)x < to_x) || (px > 0 && (int)x > to_x))
			x = (float)to_x;
		if ((py < 0 && (int)y < to_y) || (py > 0 && (int)y > to_y))
			y = (float)to_y;

		if (dx_is_bigger) {
			px = px * (1.0F - 1 / (float)slide_slowdown);
			if (px < slide_steps && px > 0)
				px = slide_steps;
			else if (px > -slide_steps && px < 0)
				px = -slide_steps;
			py = (is_dx_nul ? 0.0F : px * dy / dx);
		} else {
			py = py * (1.0F - 1 / (float)slide_slowdown);
			if (py < slide_steps && py > 0)
				py = slide_steps;
			else if (py > -slide_steps && py < 0)
				py = -slide_steps;
			px = (is_dy_nul ? 0.0F : py * dx / dy);
		}

		for (i = 0; i < n; i++) {
			if (wins[i] == XCB_NONE)
				continue;
			xcb_configure_window(wdisp->conn, wins[i], XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ (int)x + i * off_x, (int)y + i * off_y });
		}
		wXFlush(wdisp->conn);
		if (slide_delay > 0) {
			wusleep(slide_delay * 1000L);
		} else {
			wusleep(1000L);
		}
		if (time(NULL) - time0 > MAX_ANIMATION_TIME)
			break;
	}
	for (i = 0; i < n; i++) {
		if (wins[i] == XCB_NONE)
			continue;
		xcb_configure_window(wdisp->conn, wins[i], XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (const uint32_t[]){ to_x + i * off_x, to_y + i * off_y });
	}

	wXSync(wdisp->conn);
	/* compress expose events */
	eatExpose();
}

/* find the start of a UTF-8 character at or before the given position */
static int utf8_find_char_start(const char *string, int pos)
{
	while (pos > 0 && (string[pos] & 0xC0) == 0x80) {
		pos--;
	}
	return pos;
}

char *ShrinkString(WMFont *font, const char *string, int width)
{
	int w, w1;
	int p;
	char *pos;
	char *text;
	size_t text_size;
	int p1, p2, t, utf8_safe_pos;

	p = strlen(string);
	w = WMWidthOfString(font, string, p);
	text_size = strlen(string) + 8;
	text = wmalloc(text_size);
	wstrlcpy(text, string, text_size);
	if (w <= width)
		return text;

	pos = strchr(text, ' ');
	if (!pos)
		pos = strchr(text, ':');

	if (pos) {
		*pos = 0;
		p = strlen(text);
		w1 = WMWidthOfString(font, text, p);
		if (w1 > width) {
			p = 0;
			*pos = ' ';
			*text = 0;
		} else {
			*pos = 0;
			width -= w1;
			p++;
		}
		string += p;
		p = strlen(string);
	} else {
		*text = 0;
	}
	wstrlcat(text, "...", text_size);
	width -= WMWidthOfString(font, "...", 3);

	p1 = 0;
	p2 = p;
	t = (p2 - p1) / 2;
	while (p2 > p1) {
		int old_t = t;
		/* ensure we cut at UTF-8 character boundary */
		utf8_safe_pos = utf8_find_char_start(string, p - t);
		t = p - utf8_safe_pos;

		w = WMWidthOfString(font, &string[utf8_safe_pos], t);
		if (w > width) {
			p2 = t;
		} else if (w < width) {
			p1 = t;
		} else {
			p1 = t;
			break;
		}

		/* Calculate new t for next iteration */
		t = p1 + (p2 - p1) / 2;

		/* Prevent infinite loop if we're not making progress */
		if (t == old_t || (p2 - p1) <= 1) {
			break;
		}
	}

	/* ensure final cut is at UTF-8 character boundary */
	utf8_safe_pos = utf8_find_char_start(string, p - p1);
	wstrlcat(text, &string[utf8_safe_pos], text_size);

	return text;
}

char *FindImage(const char *paths, const char *file)
{
	const char *colon;
	char *base, *path = NULL;

	colon = strrchr(file, ':');
	if (colon) {
		base = wstrndup(file, colon - file);
		path = wfindfile(paths, base);
		wfree(base);
	}
	if (!colon || !path)
		path = wfindfile(paths, file);

	return path;
}

/* bound on the whole clipboard read, a hostile or hung selection
 * owner must not be able to stall the wait longer than this */
#define WMSEL_RETRIEVAL_TIMEOUT_MS 1000

/* the fields a SELECTION_NOTIFY must carry to be ours, the property
 * is the tag the request was made with */
struct SelNotifyMatch {
	xcb_atom_t selection;
	xcb_atom_t target;
	xcb_atom_t property;
};

/* drop replies from earlier timed-out requests, they name another tag
 * property, XCB_NONE (owner refused) still matches */
static bool matchSelNotify(const xcb_generic_event_t *event, void *cdata)
{
	const xcb_selection_notify_event_t *sn = (const xcb_selection_notify_event_t *)event;
	const struct SelNotifyMatch *m = cdata;

	return sn->selection == m->selection && sn->target == m->target
	       && (sn->property == m->property || sn->property == XCB_NONE);
}

static char *getClipboardSelection(WScreen * screen, xcb_atom_t selection)
{
	xcb_selection_notify_event_t *sn;
	xcb_generic_event_t *ev = NULL;
	xcb_atom_t property;
	xcb_window_t requestor;
	char *data;
	size_t len;
	struct SelNotifyMatch match;

	/* A fresh requestor also isolates late refusals with property=None.
	 * X clients can still forge a reply to the current requestor. */
	property = WXInternAtom(wdisp->conn, "_WINDOWMAKER_SEL_TAG");
	if (property == XCB_NONE) {
		wwarning("could not create selection property");
		return NULL;
	}
	requestor = xcb_generate_id(wdisp->conn);
	if (!WXCheckVoid(wdisp->conn,
			 xcb_create_window_checked(wdisp->conn, XCB_COPY_FROM_PARENT, requestor,
						   screen->root_win, 0, 0, 1, 1, 0,
						   XCB_WINDOW_CLASS_INPUT_OUTPUT,
						   XCB_COPY_FROM_PARENT, 0, NULL))) {
		wwarning("could not create selection requestor");
		return NULL;
	}

	if (!wXConvertSelection(wdisp->conn, selection, XCB_ATOM_STRING, property,
				requestor, XCB_CURRENT_TIME)) {
		xcb_destroy_window(wdisp->conn, requestor);
		wwarning("could not request text selection");
		return NULL;
	}

	match.selection = selection;
	match.target = XCB_ATOM_STRING;
	match.property = property;

	/* the bounded wait never re-enters the WINGs event loop so a
	 * WINGs timer would not fire here either (WM-2026-20) */
	if (!WXEvqTakeTypedTimed(wdisp->conn, XCB_SELECTION_NOTIFY, requestor,
				 matchSelNotify, &match, &ev, WMSEL_RETRIEVAL_TIMEOUT_MS)) {
		xcb_destroy_window(wdisp->conn, requestor);
		wwarning("selection retrieval timed out");
		return NULL;
	}

	sn = (xcb_selection_notify_event_t *)ev;

	/* nobody owns the selection or the current owner has
	 * nothing to do with what we need */
	if (sn->property == XCB_NONE) {
		free(ev);
		xcb_destroy_window(wdisp->conn, requestor);
		return NULL;
	}

	data = WXGetLatin1String(wdisp->conn, requestor, property, &len);
	free(ev);
	xcb_destroy_window(wdisp->conn, requestor);
	if (!data) {
		wwarning("invalid data in text selection");
		return NULL;
	}
	return data;
}

static char *getTextSelection(WScreen * screen, xcb_atom_t selection)
{
	int buffer = -1;

	switch (selection) {
	case XCB_ATOM_CUT_BUFFER0:
		buffer = 0;
		break;
	case XCB_ATOM_CUT_BUFFER1:
		buffer = 1;
		break;
	case XCB_ATOM_CUT_BUFFER2:
		buffer = 2;
		break;
	case XCB_ATOM_CUT_BUFFER3:
		buffer = 3;
		break;
	case XCB_ATOM_CUT_BUFFER4:
		buffer = 4;
		break;
	case XCB_ATOM_CUT_BUFFER5:
		buffer = 5;
		break;
	case XCB_ATOM_CUT_BUFFER6:
		buffer = 6;
		break;
	case XCB_ATOM_CUT_BUFFER7:
		buffer = 7;
		break;
	}
	if (buffer >= 0)
		return wXFetchBuffer(wdisp->conn, wdisp->default_screen, NULL, buffer);

	return getClipboardSelection(screen, selection);
}

char *getselection(WScreen * scr)
{
	char *tmp;

	tmp = getTextSelection(scr, XCB_ATOM_PRIMARY);
	if (!tmp)
		tmp = getTextSelection(scr, XCB_ATOM_CUT_BUFFER0);
	return tmp;
}

static char*
parseuserinputpart(const char *line, int *ptr, const char *endchars)
{
	int depth = 0, begin;
	char *value = NULL;
	begin = ++*ptr;

	while(line[*ptr] != '\0') {
		if(line[*ptr] == '(') {
			++depth;
		} else if(depth > 0 && line[*ptr] == ')') {
			--depth;
		} else if(depth == 0 && strchr(endchars, line[*ptr]) != NULL) {
			value = wmalloc(*ptr - begin + 1);
			memcpy(value, line + begin, *ptr - begin);
			value[*ptr - begin] = '\0';
			break;
		}
		++*ptr;
	}

	return value;
}

char*
getuserinput(WScreen *scr, const char *line, int *ptr, bool advanced)
{
	char *ret = NULL, *title = NULL, *prompt = NULL, *name = NULL;
	char *dTitle, *dMsg;
	int rv;

	if (line[*ptr] == '(')
		title = parseuserinputpart(line, ptr, ",)");
	if (title != NULL && line[*ptr] == ',')
		prompt = parseuserinputpart(line, ptr, ",)");
	if (prompt != NULL && line[*ptr] == ',')
		name = parseuserinputpart(line, ptr, ")");

	dTitle = title ? _(title) : _("Program Arguments");
	if (prompt)
		dMsg = _(prompt);
	else if (title)
		dMsg = NULL;
	else
		dMsg = _("Enter command arguments:");

	if (advanced)
		rv = wAdvancedInputDialog(scr, dTitle, dMsg, name, &ret);
	else
		rv = wInputDialog(scr, dTitle, dMsg, &ret);

	if (title)
		wfree(title);
	if (prompt)
		wfree(prompt);
	if (name)
		wfree(name);

    return rv ? ret : NULL;
}

void ExecuteExitCommand(WScreen *scr, long quickmode)
{
	static int inside = 0;
	int result;

	/* prevent reentrant calls */
	if (inside)
		return;
	inside = 1;

#define R_CANCEL 0
#define R_EXIT   1

	result = R_CANCEL;

	if (quickmode == M_QUICK) {
		result = R_EXIT;
	} else {
		int r, oldSaveSessionFlag;

		oldSaveSessionFlag = w_global.config->save_session_on_exit;
		r = wExitDialog(scr, _("Exit"),
				_("Are you sure you want to quit Window Maker?"), _("Exit"), _("Cancel"), NULL);

		if (r == WAPRDefault) {
			result = R_EXIT;
		} else if (r == WAPRAlternate) {
			/* Don't modify the "save session on exit" flag if the
			 * user canceled the operation. */
			w_global.config->save_session_on_exit = oldSaveSessionFlag;
		}
	}
	if (result == R_EXIT) {
		if (!wXSMPRequestShutdown(w_global.config->save_session_on_exit))
			Shutdown(WSExitMode, false);
	}

#undef R_EXIT
#undef R_CANCEL
	inside = 0;
}

void ExecuteInputCommand(WScreen *scr, const char *cmdline)
{
	char *cmd;

	cmd = ExpandOptions(scr, cmdline);
	if (cmd) {
		wXGrabPointer(wdisp->conn, scr->root_win, true, 0,
		     XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, wdisp->cursor[WCUR_WAIT], XCB_CURRENT_TIME);
		wXSync(wdisp->conn);

		ExecuteShellCommandWithStartup(scr, cmd, false);
		wfree(cmd);

		wXUngrabPointer(wdisp->conn, XCB_CURRENT_TIME);
		wXSync(wdisp->conn);
	}
}

void ParseWindowName(WMPropList *value, char **winstance, char **wclass, const char *where)
{
	char *name;

	*winstance = *wclass = NULL;

	if (!WMIsPLString(value)) {
		wwarning(_("bad window name value in %s state info"), where);
		return;
	}

	name = WMGetFromPLString(value);
	if (!name || strlen(name) == 0) {
		wwarning(_("bad window name value in %s state info"), where);
		return;
	}

	UnescapeWM_CLASS(name, winstance, wclass);
}

bool GetCanonicalShortcutLabel(unsigned int modifiers, xkb_keysym_t ksym, char *buf, size_t bufsz)
{
	static const struct { unsigned int mask; const char *name; } mt[] = {
		{ XCB_MOD_MASK_SHIFT,   "Shift+"   },
		{ XCB_MOD_MASK_CONTROL, "Control+" },
		{ XCB_MOD_MASK_1,    "Mod1+"    },
		{ XCB_MOD_MASK_2,    "Mod2+"    },
		{ XCB_MOD_MASK_3,    "Mod3+"    },
		{ XCB_MOD_MASK_4,    "Mod4+"    },
		{ XCB_MOD_MASK_5,    "Mod5+"    },
		{ 0, NULL }
	};
	char kname[64];
	size_t i;

	if (wXkbKeysymName(ksym, kname, sizeof(kname)) < 0)
		return false;

	buf[0] = '\0';
	for (i = 0; mt[i].mask; i++) {
		if (modifiers & mt[i].mask)
			wstrlcat(buf, mt[i].name, bufsz);
	}
	wstrlcat(buf, kname, bufsz);

	return true;
}

/* Convert a canonical keysym name (as produced by wXkbKeysymName, e.g.
 * "odiaeresis", "plus", "0x010000e0") to its printable UTF-8 glyph for
 * display purposes. Returns 'name' unchanged when there is no printable
 * form (function keys, dead keys, control characters, space, ...).
 * 'buf' must be at least 8 bytes. */
static const char *keysym_name_to_display(const char *name, char *buf, size_t bufsz)
{
	xkb_keysym_t ks;
	unsigned char c0;
	int n;

	ks = wXkbKeysymFromName(name);
	if (ks == XKB_KEY_NoSymbol)
		return name;

	n = xkb_keysym_to_utf8(ks, buf, bufsz);
	/* n includes the terminating NUL, n > 1 means at least one real byte */
	if (n <= 1)
		return name;

	/* Skip C0 controls (U+0001-U+001F), space (U+0020), DEL (U+007F),
	 * and C1 controls (U+0080-U+009F encoded as 0xC2 0x80-0x9F). */
	c0 = (unsigned char)buf[0];
	if (c0 >= 0x21 && c0 != 0x7F &&
	    !(c0 == 0xC2 && (unsigned char)buf[1] <= 0x9F))
		return buf;
	return name;
}

char *GetShortcutString(const char *shortcut)
{
	char *buffer = NULL;
	char *k, *tmp, *text;
	char disp_buf[8];

	tmp = text = wstrdup(shortcut);

	/* get modifiers */
	while ((k = strchr(text, '+')) != NULL) {
		int mod;
		const char *lbl;

		*k = 0;
		mod = wXModifierFromKey(text);
		if (mod < 0) {
			wfree(buffer);
			return wstrdup("bug");
		}
		lbl = wXModifierToShortcutLabel(mod);
		if (lbl)
			buffer = wstrappend(buffer, lbl);
		else
			buffer = wstrappend(buffer, text);
		text = k + 1;
	}

	/* Convert the canonical key name to its printable glyph where possible,
	 * "odiaeresis" -> U+00F6, "plus" -> "+", "0x010000e0" -> U+00E0, etc. */
	buffer = wstrappend(buffer, keysym_name_to_display(text, disp_buf, sizeof(disp_buf)));
	wfree(tmp);

	return buffer;
}

char *GetShortcutLabelForKeycode(unsigned int modifiers, xcb_keycode_t keycode)
{
	char buf[MAX_SHORTCUT_LENGTH];

	if (!GetCanonicalShortcutLabel(modifiers, wXkbKeycodeToKeysym(keycode, 0),
				       buf, sizeof(buf)))
		return NULL;

	return GetShortcutString(buf);
}

char *GetShortcutKey(WShortKey key)
{
	char buf[MAX_SHORTCUT_LENGTH];
	char *result, *seg;
	int step;

	if (!GetCanonicalShortcutLabel(key.modifier,
				       wXkbKeycodeToKeysym(key.keycode, 0),
				       buf, sizeof(buf)))
		return NULL;

	/* Convert the leader token to its display string */
	result = GetShortcutString(buf);

	/* Append each chain follower separated by a space */
	for (step = 0; step < key.chain_length - 1; step++) {
		char *combined;

		if (key.chain_keycodes[step] == 0)
			break;

		if (!GetCanonicalShortcutLabel(key.chain_modifiers[step],
					       wXkbKeycodeToKeysym(key.chain_keycodes[step], 0),
					       buf, sizeof(buf)))
			break;

		seg = GetShortcutString(buf);
		combined = wstrconcat(result, " ");
		wfree(result);
		result = wstrconcat(combined, seg);
		wfree(combined);
		wfree(seg);
	}

	return result;
}

void wShortKeyFree(WShortKey *key)
{
	if (!key)
		return;

	wfree(key->chain_modifiers);
	wfree(key->chain_keycodes);
	memset(key, 0, sizeof(*key));
}

char *EscapeWM_CLASS(const char *name, const char *class)
{
	char *ret;
	char *ename = NULL, *eclass = NULL;
	int i, j, l;

	if (!name && !class)
		return NULL;

	if (name) {
		l = strlen(name);
		ename = wmalloc(l * 2 + 1);
		j = 0;
		for (i = 0; i < l; i++) {
			if (name[i] == '\\') {
				ename[j++] = '\\';
			} else if (name[i] == '.') {
				ename[j++] = '\\';
			}
			ename[j++] = name[i];
		}
		ename[j] = 0;
	}
	if (class) {
		l = strlen(class);
		eclass = wmalloc(l * 2 + 1);
		j = 0;
		for (i = 0; i < l; i++) {
			if (class[i] == '\\') {
				eclass[j++] = '\\';
			} else if (class[i] == '.') {
				eclass[j++] = '\\';
			}
			eclass[j++] = class[i];
		}
		eclass[j] = 0;
	}

	if (ename && eclass) {
		int len = strlen(ename) + strlen(eclass) + 4;
		ret = wmalloc(len);
		snprintf(ret, len, "%s.%s", ename, eclass);
		wfree(ename);
		wfree(eclass);
	} else if (ename) {
		ret = wstrdup(ename);
		wfree(ename);
	} else {
		ret = wstrdup(eclass);
		wfree(eclass);
	}

	return ret;
}

static void UnescapeWM_CLASS(const char *str, char **name, char **class)
{
	int i, j, k, dot;
	int length_of_name;

	j = strlen(str);

	/* separate string in 2 parts */
	length_of_name = 0;
	dot = -1;
	for (i = 0; i < j; i++, length_of_name++) {
		if (str[i] == '\\') {
			i++;
			continue;
		} else if (str[i] == '.') {
			dot = i;
			break;
		}
	}

	/* unescape the name */
	if (length_of_name > 0) {
		*name = wmalloc(length_of_name + 1);
		for (i = 0, k = 0; i < dot; i++) {
			if (str[i] != '\\')
				(*name)[k++] = str[i];
		}
		(*name)[k] = '\0';
	} else {
		*name = NULL;
	}

	/* unescape the class */
	if (dot < j-1) {
		*class = wmalloc(j - (dot + 1) + 1);
		for (i = dot + 1, k = 0; i < j; i++) {
			if (str[i] != '\\')
				(*class)[k++] = str[i];
		}
		(*class)[k] = 0;
	} else {
		*class = NULL;
	}
}

static void track_bg_helper_death(pid_t pid, unsigned int status, void *client_data)
{
	WScreen *scr = (WScreen *) client_data;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) pid;
	(void) status;

	close(scr->helper_fd);
	scr->helper_fd = 0;
	scr->helper_pid = 0;
	scr->flags.backimage_helper_launched = 0;
}

typedef struct {
	WScreen *scr;
	int read_fd;
	int write_fd;
} BgHelperSetup;

static void bg_helper_pre_exec(void *data)
{
	BgHelperSetup *setup = data;

	close(setup->write_fd);
	SetupEnvironment(setup->scr);
	if (dup2(setup->read_fd, STDIN_FILENO) < 0) {
		werror(_("%s failed, can't set workspace specific background image (%s)"),
		       "dup2()", strerror(errno));
		_exit(1);
	}
	close(setup->read_fd);
}

bool start_bg_helper(WScreen *scr)
{
	pid_t pid;
	int filedes[2];
	char *helper_argv[4];
	BgHelperSetup setup;

	if (pipe(filedes) < 0) {
		werror(_("%s failed, can't set workspace specific background image (%s)"),
		       "pipe()", strerror(errno));
		return false;
	}

	helper_argv[0] = "wmsetbg";
	helper_argv[1] = "-helper";
	if (w_global.config->smooth_workspace_back) {
		helper_argv[2] = "-S";
		helper_argv[3] = NULL;
	} else {
		helper_argv[2] = NULL;
	}
	setup.scr = scr;
	setup.read_fd = filedes[0];
	setup.write_fd = filedes[1];

	pid = wspawnvp_setup(helper_argv, bg_helper_pre_exec, &setup);
	if (pid < 0) {
		close(filedes[0]);
		close(filedes[1]);
		return false;
	} else {
		/* We don't need this side of the pipe in the parent process */
		close(filedes[0]);

		if (fcntl(filedes[1], F_SETFD, FD_CLOEXEC) < 0)
			wwarning(_("could not set close-on-exec flag for bg_helper's communication file handle (%s)"),
			         strerror(errno));

		scr->helper_fd = filedes[1];
		scr->helper_pid = pid;
		scr->flags.backimage_helper_launched = 1;

		wAddDeathHandler(pid, track_bg_helper_death, scr, "helper", -1);

		return true;
	}
}

void SendHelperMessage(WScreen *scr, char type, int workspace, const char *msg)
{
	char *buffer;
	size_t len;
	size_t i;
	size_t msg_len;
	char buf[16];

	if (!scr->flags.backimage_helper_launched) {
		return;
	}

	msg_len = msg ? strlen(msg) : 0;
	len = msg_len + (workspace >= 0 ? 4 : 0) + 1;
	if (len > 9999) {
		wwarning(_("background image helper message is too long"));
		return;
	}
	buffer = wmalloc(len + 5);
	snprintf(buf, sizeof(buf), "%4zu", len);
	memcpy(buffer, buf, 4);
	buffer[4] = type;
	i = 5;
	if (workspace >= 0) {
		snprintf(buf, sizeof(buf), "%4i", workspace);
		memcpy(&buffer[i], buf, 4);
		i += 4;
		buffer[i] = 0;
	}
	if (msg)
		memcpy(&buffer[i], msg, msg_len + 1);

	if (write(scr->helper_fd, buffer, len + 4) < 0) {
		werror(_("could not send message to background image helper"));
	}
	wfree(buffer);
}

bool UpdateDomainFile(WDDomain * domain)
{
	struct stat stbuf;
	char path[PATH_MAX];
	WMPropList *shared_dict, *dict;
	bool result, freeDict = false;

	dict = domain->dictionary;
	if (WMIsPLDictionary(domain->dictionary)) {
		/* retrieve global system dictionary */
		snprintf(path, sizeof(path), "%s/%s", PKGCONFDIR, domain->domain_name);
		if (stat(path, &stbuf) >= 0) {
			shared_dict = WMReadPropListFromFile(path);
			if (shared_dict) {
				if (WMIsPLDictionary(shared_dict)) {
					freeDict = true;
					dict = WMDeepCopyPropList(domain->dictionary);
					WMSubtractPLDictionaries(dict, shared_dict, true);
				}
				WMReleasePropList(shared_dict);
			}
		}
	}

	result = WMWritePropListToFile(dict, domain->path);

	if (freeDict) {
		WMReleasePropList(dict);
	}

	return result;
}

char *StrConcatDot(const char *a, const char *b)
{
	int len;
	char *str;

	if (!a)
		a = "";
	if (!b)
		b = "";

	len = strlen(a) + strlen(b) + 4;
	str = wmalloc(len);

	snprintf(str, len, "%s.%s", a, b);

	return str;
}

static char *getCommandForWindow(xcb_window_t win, int elements)
{
	char **argv, *command = NULL;
	int argc;

	if (WXGetCommand(wdisp->conn, win, &argv, &argc)) {
		if (argc > 0 && argv != NULL) {
			if (elements == 0)
				elements = argc;
			command = wtokenjoin(argv, WMIN(argc, elements));
			if (command[0] == 0) {
				wfree(command);
				command = NULL;
			}
		}
		if (argv) {
			WXFreeStringList(argv, argc);
		}
	}

	return command;
}

/* Free result when done */
char *GetCommandForWindow(xcb_window_t win)
{
	return getCommandForWindow(win, 0);
}

/* Center a width x height rectangle on the head under the pointer */
WMPoint wGetCenter(WScreen *scr, int width, int height)
{
	return wGetPointToCenterRectInHead(scr, wGetHeadForPointerLocation(scr), width, height);
}

xcb_visualtype_t *wVisualForId(WScreen *scr, xcb_visualid_t vid)
{
	/* almost every client uses the screen's default visual, so try
	 * that first before walking the full visual list */
	if (scr && scr->w_visual && scr->w_visual->visual_id == vid)
		return scr->w_visual;

	return wXVisualTypeForID(wdisp->conn, (xcb_visualid_t) vid);
}
