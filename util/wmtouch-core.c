/*
 *  wmtouch-core - touchpad gesture recognition and WMTouch domain parsing
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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "wmtouch-core.h"

#define THRESHOLD_MIN 1
#define THRESHOLD_MAX 10000
#define AXIS_MARGIN_MIN 0.0
#define AXIS_MARGIN_MAX 100.0
#define PINCH_THRESHOLD_MIN 1.0
#define PINCH_THRESHOLD_MAX 100.0

static const char *direction_suffix[WMT_DIR_COUNT] = {
	[WMT_DIR_LEFT]   = "Left",
	[WMT_DIR_RIGHT]  = "Right",
	[WMT_DIR_UP]     = "Up",
	[WMT_DIR_DOWN]   = "Down",
	[WMT_DIR_PINCH]  = "PinchIn",
	[WMT_DIR_SPREAD] = "PinchOut"
};

static const char *finger_prefix[4] = {
	"TwoFinger", "ThreeFinger", "FourFinger", "FiveFinger"
};

static const char *config_string(WMPropList *domain, const char *key)
{
	WMPropList *k, *v;
	const char *s;

	if (!domain)
		return NULL;
	k = WMCreatePLString(key);
	v = WMGetFromPLDictionary(domain, k);
	WMReleasePropList(k);
	if (!v || !WMIsPLString(v))
		return NULL;
	s = WMGetFromPLString(v);
	return (s && *s) ? s : NULL;
}

static bool is_none(const char *s)
{
	return !s || !*s || strcasecmp(s, "None") == 0;
}

void wmTouchConfigInit(WMTouchConfig *cfg)
{
	if (!cfg)
		return;
	memset(cfg, 0, sizeof(*cfg));
	cfg->swipe_threshold = WMTOUCH_DEFAULT_THRESHOLD;
	cfg->swipe_axis_margin = WMTOUCH_DEFAULT_AXIS_MARGIN;
	cfg->pinch_threshold = WMTOUCH_DEFAULT_PINCH_THRESHOLD;
}

void wmTouchConfigFree(WMTouchConfig *cfg)
{
	int f, d;

	if (!cfg)
		return;
	for (f = 0; f < 4; f++)
		for (d = 0; d < WMT_DIR_COUNT; d++) {
			if (cfg->actions[f][d])
				wfree(cfg->actions[f][d]);
			cfg->actions[f][d] = NULL;
		}
}

void wmTouchConfigFromPropList(WMPropList *domain, WMTouchConfig *cfg)
{
	const char *s;
	long threshold;
	double margin;
	int f, d;

	if (!cfg)
		return;

	wmTouchConfigFree(cfg);
	wmTouchConfigInit(cfg);

	s = config_string(domain, "SwipeThreshold");
	if (s) {
		char *end;

		threshold = strtol(s, &end, 10);
		if (end != s && *end == '\0') {
			if (threshold < THRESHOLD_MIN)
				threshold = THRESHOLD_MIN;
			if (threshold > THRESHOLD_MAX)
				threshold = THRESHOLD_MAX;
			cfg->swipe_threshold = (int)threshold;
		}
	}

	s = config_string(domain, "SwipeAxisMargin");
	if (s) {
		char *end;

		margin = strtod(s, &end);
		if (end != s && *end == '\0') {
			if (margin < AXIS_MARGIN_MIN)
				margin = AXIS_MARGIN_MIN;
			if (margin > AXIS_MARGIN_MAX)
				margin = AXIS_MARGIN_MAX;
			cfg->swipe_axis_margin = margin;
		}
	}

	s = config_string(domain, "PinchThreshold");
	if (s) {
		char *end;

		margin = strtod(s, &end);
		if (end != s && *end == '\0') {
			if (margin < PINCH_THRESHOLD_MIN)
				margin = PINCH_THRESHOLD_MIN;
			if (margin > PINCH_THRESHOLD_MAX)
				margin = PINCH_THRESHOLD_MAX;
			cfg->pinch_threshold = margin;
		}
	}

	for (f = 0; f < 4; f++) {
		for (d = 0; d < WMT_DIR_COUNT; d++) {
			char key[32];

			/* libinput has no two-finger swipes, so the
			 * TwoFinger prefix has only the pinch pair */
			if (f == 0 && d <= WMT_DIR_DOWN)
				continue;
			snprintf(key, sizeof(key), "%s%s", finger_prefix[f],
				 direction_suffix[d]);
			s = config_string(domain, key);
			if (!is_none(s))
				cfg->actions[f][d] = wstrdup(s);
		}
	}
}

const char *wmTouchConfigAction(const WMTouchConfig *cfg, int fingers,
				enum WMGestureDirection dir)
{
	int f;

	if (!cfg || dir < 0 || dir >= WMT_DIR_COUNT)
		return NULL;
	if (fingers == 2)
		f = 0;
	else if (fingers == 3)
		f = 1;
	else if (fingers == 4)
		f = 2;
	else if (fingers == 5)
		f = 3;
	else
		return NULL;
	return cfg->actions[f][dir];
}

void wmTouchRecognizerInit(WMTouchRecognizer *r)
{
	if (!r)
		return;
	memset(r, 0, sizeof(*r));
}

enum WMGestureDirection wmTouchRecognizerFeed(WMTouchRecognizer *r,
					      const WMGestureEvent *ev,
					      const WMTouchConfig *cfg)
{
	double adx, ady;
	int fingers;

	if (!r || !ev)
		return WMT_DIR_NONE;

	switch (ev->kind) {
	case WMT_GESTURE_BEGIN:
		r->active = true;
		r->type = ev->type;
		r->fingers = ev->fingers;
		r->dx = 0.0;
		r->dy = 0.0;
		r->scale = 1.0;
		return WMT_DIR_NONE;

	case WMT_GESTURE_UPDATE:
		/* must not fall into the END decision below on a stale
		 * gesture: an UPDATE with no active gesture is dropped */
		if (!r->active)
			return WMT_DIR_NONE;
		r->dx += ev->dx;
		r->dy += ev->dy;
		r->scale = ev->scale;
		r->fingers = ev->fingers;
		return WMT_DIR_NONE;

	case WMT_GESTURE_CANCEL:
		r->active = false;
		return WMT_DIR_NONE;

	case WMT_GESTURE_END:
		if (!r->active)
			return WMT_DIR_NONE;
		r->active = false;
		break;
	}

	if (!cfg)
		return WMT_DIR_NONE;
	fingers = r->fingers;
	if (fingers < 2 || fingers > 5)
		return WMT_DIR_NONE;
	/* libinput only reports swipe gestures for three or more
	 * fingers, so a two-finger swipe never fires */
	if (r->type == WMT_GESTURE_SWIPE && fingers == 2)
		return WMT_DIR_NONE;

	if (r->type == WMT_GESTURE_PINCH) {
		if (r->scale > 1.0 && r->scale >= cfg->pinch_threshold)
			return WMT_DIR_SPREAD;
		if (r->scale < 1.0 && r->scale <= 1.0 / cfg->pinch_threshold)
			return WMT_DIR_PINCH;
		return WMT_DIR_NONE;
	}

	adx = fabs(r->dx);
	ady = fabs(r->dy);

	if (adx >= cfg->swipe_threshold
	    && adx >= cfg->swipe_axis_margin * ady)
		return r->dx > 0 ? WMT_DIR_RIGHT : WMT_DIR_LEFT;
	if (ady >= cfg->swipe_threshold
	    && ady >= cfg->swipe_axis_margin * adx)
		return r->dy > 0 ? WMT_DIR_DOWN : WMT_DIR_UP;

	return WMT_DIR_NONE;
}

const char *wmTouchDirectionName(enum WMGestureDirection dir)
{
	switch (dir) {
	case WMT_DIR_LEFT:
		return "Left";
	case WMT_DIR_RIGHT:
		return "Right";
	case WMT_DIR_UP:
		return "Up";
	case WMT_DIR_DOWN:
		return "Down";
	case WMT_DIR_PINCH:
		return "PinchIn";
	case WMT_DIR_SPREAD:
		return "PinchOut";
	default:
		return "None";
	}
}

static int parse_long_token(const char *s, long *out)
{
	char *end;

	if (!s || !*s)
		return 0;
	*out = strtol(s, &end, 10);
	return *end == '\0';
}

static int parse_double_token(const char *s, double *out)
{
	char *end;

	if (!s || !*s)
		return 0;
	*out = strtod(s, &end);
	return *end == '\0';
}

int wmTouchParseEventLine(const char *line, WMGestureEvent *out)
{
	char buf[256];
	char *save = NULL, *tok, *hash;
	char *toks[5];
	int ntok = 0;
	long lval;
	double dval;

	if (!line || !out)
		return -1;
	if (strlen(line) >= sizeof(buf))
		return -1;

	snprintf(buf, sizeof(buf), "%s", line);
	hash = strchr(buf, '#');
	if (hash)
		*hash = '\0';

	for (tok = strtok_r(buf, " \t\r\n", &save);
	     tok && ntok < 5;
	     tok = strtok_r(NULL, " \t\r\n", &save))
		toks[ntok++] = tok;

	if (ntok == 0)
		return 0;

	if (strcmp(toks[0], "begin") == 0
	    || strcmp(toks[0], "end") == 0
	    || strcmp(toks[0], "cancel") == 0) {
		if (ntok != 2 || !parse_long_token(toks[1], &lval))
			return -1;
		if (strcmp(toks[0], "begin") == 0)
			out->kind = WMT_GESTURE_BEGIN;
		else if (strcmp(toks[0], "end") == 0)
			out->kind = WMT_GESTURE_END;
		else
			out->kind = WMT_GESTURE_CANCEL;
		out->type = WMT_GESTURE_SWIPE;
		out->scale = 1.0;
		out->dx = 0.0;
		out->dy = 0.0;
	} else if (strcmp(toks[0], "update") == 0) {
		if (ntok != 4 || !parse_long_token(toks[1], &lval)
		    || !parse_double_token(toks[2], &dval))
			return -1;
		out->kind = WMT_GESTURE_UPDATE;
		out->type = WMT_GESTURE_SWIPE;
		out->scale = 1.0;
		out->dx = dval;
		if (!parse_double_token(toks[3], &dval))
			return -1;
		out->dy = dval;
	} else if (strcmp(toks[0], "pinch-begin") == 0
		   || strcmp(toks[0], "pinch-end") == 0
		   || strcmp(toks[0], "pinch-cancel") == 0) {
		if (ntok != 2 || !parse_long_token(toks[1], &lval))
			return -1;
		if (strcmp(toks[0], "pinch-begin") == 0)
			out->kind = WMT_GESTURE_BEGIN;
		else if (strcmp(toks[0], "pinch-end") == 0)
			out->kind = WMT_GESTURE_END;
		else
			out->kind = WMT_GESTURE_CANCEL;
		out->type = WMT_GESTURE_PINCH;
		out->scale = 1.0;
		out->dx = 0.0;
		out->dy = 0.0;
	} else if (strcmp(toks[0], "pinch-update") == 0) {
		if (ntok != 3 || !parse_long_token(toks[1], &lval)
		    || !parse_double_token(toks[2], &dval))
			return -1;
		out->kind = WMT_GESTURE_UPDATE;
		out->type = WMT_GESTURE_PINCH;
		out->scale = dval;
		out->dx = 0.0;
		out->dy = 0.0;
	} else {
		return -1;
	}

	if (lval < 1 || lval > 5)
		return -1;
	out->fingers = (int)lval;
	return 1;
}
