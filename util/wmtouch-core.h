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
 *  This file has no libinput dependency so the gesture state machine and
 *  the domain parser are exercised by `make check` on hosts without
 *  libinput. It links libWUtil only.
 */

#ifndef WMTOUCH_CORE_H
#define WMTOUCH_CORE_H

#include <stdbool.h>

#include <WINGs/WUtil.h>

#define WMTOUCH_DOMAIN "WMTouch"

#define WMTOUCH_DEFAULT_THRESHOLD       60
#define WMTOUCH_DEFAULT_AXIS_MARGIN     1.2
#define WMTOUCH_DEFAULT_PINCH_THRESHOLD 1.5

enum WMGestureKind {
	WMT_GESTURE_BEGIN,
	WMT_GESTURE_UPDATE,
	WMT_GESTURE_END,
	WMT_GESTURE_CANCEL
};

enum WMGestureType {
	WMT_GESTURE_SWIPE,
	WMT_GESTURE_PINCH
};

typedef struct {
	enum WMGestureKind kind;
	enum WMGestureType type;
	int fingers;
	double dx;
	double dy;
	double scale;
} WMGestureEvent;

enum WMGestureDirection {
	WMT_DIR_LEFT = 0,
	WMT_DIR_RIGHT,
	WMT_DIR_UP,
	WMT_DIR_DOWN,
	WMT_DIR_PINCH,
	WMT_DIR_SPREAD,
	WMT_DIR_NONE
};

#define WMT_DIR_COUNT 6

typedef struct {
	int swipe_threshold;
	double swipe_axis_margin;
	double pinch_threshold;
	/* actions[finger-2][direction] or NULL when None/absent */
	char *actions[4][WMT_DIR_COUNT];
} WMTouchConfig;

typedef struct {
	int fingers;
	double dx;
	double dy;
	double scale;
	enum WMGestureType type;
	bool active;
} WMTouchRecognizer;

/* Domain handling */
void wmTouchConfigInit(WMTouchConfig *cfg);
void wmTouchConfigFromPropList(WMPropList *domain, WMTouchConfig *cfg);
void wmTouchConfigFree(WMTouchConfig *cfg);
const char *wmTouchConfigAction(const WMTouchConfig *cfg, int fingers,
				enum WMGestureDirection dir);

/* Gesture state machine. Feed each event in order, the return value is
 * non-NONE only for the END event that decided a swipe. */
void wmTouchRecognizerInit(WMTouchRecognizer *r);
enum WMGestureDirection wmTouchRecognizerFeed(WMTouchRecognizer *r,
					      const WMGestureEvent *ev,
					      const WMTouchConfig *cfg);

/* Replay-line parser: "begin N", "update N dx dy", "end N", "cancel N",
 * "pinch-begin N", "pinch-update N scale", "pinch-end N" and
 * "pinch-cancel N". Returns 1 on a parsed event, 0 on a blank/comment
 * line, -1 on error. */
int wmTouchParseEventLine(const char *line, WMGestureEvent *out);

const char *wmTouchDirectionName(enum WMGestureDirection dir);

#endif /* WMTOUCH_CORE_H */
