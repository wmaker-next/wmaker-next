/*
 *  check_wtouch - unit tests for the wmtouch gesture core
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  Covers the deterministic state machine and the WMTouch domain parser
 *  with no libinput and no device. Links ../wmtouch-core.c + libWUtil.
 */

#include <stdio.h>
#include <string.h>

#include <WINGs/WUtil.h>

#include "wmtouch-core.h"
#include "check_common.h"

static void feed(WMTouchRecognizer *r, const WMTouchConfig *cfg,
		 enum WMGestureKind kind, int fingers, double dx, double dy)
{
	WMGestureEvent ev;

	ev.kind = kind;
	ev.type = WMT_GESTURE_SWIPE;
	ev.fingers = fingers;
	ev.dx = dx;
	ev.dy = dy;
	ev.scale = 1.0;
	(void)wmTouchRecognizerFeed(r, &ev, cfg);
}

static void feed_pinch(WMTouchRecognizer *r, const WMTouchConfig *cfg,
		       enum WMGestureKind kind, int fingers, double scale)
{
	WMGestureEvent ev;

	ev.kind = kind;
	ev.type = WMT_GESTURE_PINCH;
	ev.fingers = fingers;
	ev.dx = 0.0;
	ev.dy = 0.0;
	ev.scale = scale;
	(void)wmTouchRecognizerFeed(r, &ev, cfg);
}

static void test_recognizer(void)
{
	WMTouchConfig cfg;
	WMTouchRecognizer r;

	wmTouchConfigInit(&cfg);

	wmTouchRecognizerInit(&r);
	feed(&r, &cfg, WMT_GESTURE_BEGIN, 3, 0, 0);
	feed(&r, &cfg, WMT_GESTURE_UPDATE, 3, 100, 5);
	CHECK(wmTouchRecognizerFeed(&r, &(WMGestureEvent){
		.kind = WMT_GESTURE_END, .fingers = 3}, &cfg) == WMT_DIR_RIGHT);

	wmTouchRecognizerInit(&r);
	feed(&r, &cfg, WMT_GESTURE_BEGIN, 3, 0, 0);
	feed(&r, &cfg, WMT_GESTURE_UPDATE, 3, -100, 3);
	CHECK(wmTouchRecognizerFeed(&r, &(WMGestureEvent){
		.kind = WMT_GESTURE_END, .fingers = 3}, &cfg) == WMT_DIR_LEFT);

	wmTouchRecognizerInit(&r);
	feed(&r, &cfg, WMT_GESTURE_BEGIN, 3, 0, 0);
	feed(&r, &cfg, WMT_GESTURE_UPDATE, 3, 5, -100);
	CHECK(wmTouchRecognizerFeed(&r, &(WMGestureEvent){
		.kind = WMT_GESTURE_END, .fingers = 3}, &cfg) == WMT_DIR_UP);

	wmTouchRecognizerInit(&r);
	feed(&r, &cfg, WMT_GESTURE_BEGIN, 4, 0, 0);
	feed(&r, &cfg, WMT_GESTURE_UPDATE, 4, 8, 100);
	CHECK(wmTouchRecognizerFeed(&r, &(WMGestureEvent){
		.kind = WMT_GESTURE_END, .fingers = 4}, &cfg) == WMT_DIR_DOWN);

	/* below threshold */
	wmTouchRecognizerInit(&r);
	feed(&r, &cfg, WMT_GESTURE_BEGIN, 3, 0, 0);
	feed(&r, &cfg, WMT_GESTURE_UPDATE, 3, 10, 0);
	CHECK(wmTouchRecognizerFeed(&r, &(WMGestureEvent){
		.kind = WMT_GESTURE_END, .fingers = 3}, &cfg) == WMT_DIR_NONE);

	/* diagonal, neither axis dominant enough */
	wmTouchRecognizerInit(&r);
	feed(&r, &cfg, WMT_GESTURE_BEGIN, 3, 0, 0);
	feed(&r, &cfg, WMT_GESTURE_UPDATE, 3, 70, 70);
	CHECK(wmTouchRecognizerFeed(&r, &(WMGestureEvent){
		.kind = WMT_GESTURE_END, .fingers = 3}, &cfg) == WMT_DIR_NONE);

	/* accumulated across several updates */
	wmTouchRecognizerInit(&r);
	feed(&r, &cfg, WMT_GESTURE_BEGIN, 3, 0, 0);
	feed(&r, &cfg, WMT_GESTURE_UPDATE, 3, 30, 1);
	feed(&r, &cfg, WMT_GESTURE_UPDATE, 3, 40, 1);
	CHECK(wmTouchRecognizerFeed(&r, &(WMGestureEvent){
		.kind = WMT_GESTURE_END, .fingers = 3}, &cfg) == WMT_DIR_RIGHT);

	/* BEGIN resets the accumulator */
	wmTouchRecognizerInit(&r);
	feed(&r, &cfg, WMT_GESTURE_BEGIN, 3, 0, 0);
	feed(&r, &cfg, WMT_GESTURE_UPDATE, 3, 55, 0);
	feed(&r, &cfg, WMT_GESTURE_BEGIN, 3, 0, 0);
	feed(&r, &cfg, WMT_GESTURE_UPDATE, 3, 20, 0);
	CHECK(wmTouchRecognizerFeed(&r, &(WMGestureEvent){
		.kind = WMT_GESTURE_END, .fingers = 3}, &cfg) == WMT_DIR_NONE);

	/* cancelled swipe is dropped */
	wmTouchRecognizerInit(&r);
	feed(&r, &cfg, WMT_GESTURE_BEGIN, 3, 0, 0);
	feed(&r, &cfg, WMT_GESTURE_UPDATE, 3, 200, 0);
	CHECK(wmTouchRecognizerFeed(&r, &(WMGestureEvent){
		.kind = WMT_GESTURE_CANCEL, .fingers = 3}, &cfg) == WMT_DIR_NONE);
	CHECK(wmTouchRecognizerFeed(&r, &(WMGestureEvent){
		.kind = WMT_GESTURE_END, .fingers = 3}, &cfg) == WMT_DIR_NONE);
	/* a stray UPDATE after CANCEL must not re-fire the stale deltas */
	CHECK(wmTouchRecognizerFeed(&r, &(WMGestureEvent){
		.kind = WMT_GESTURE_UPDATE, .fingers = 3, .dx = 5, .dy = 5}, &cfg) == WMT_DIR_NONE);

	/* END without BEGIN is inert */
	wmTouchRecognizerInit(&r);
	CHECK(wmTouchRecognizerFeed(&r, &(WMGestureEvent){
		.kind = WMT_GESTURE_END, .fingers = 3}, &cfg) == WMT_DIR_NONE);

	/* UPDATE without BEGIN is inert */
	wmTouchRecognizerInit(&r);
	CHECK(wmTouchRecognizerFeed(&r, &(WMGestureEvent){
		.kind = WMT_GESTURE_UPDATE, .fingers = 3, .dx = 100, .dy = 0}, &cfg) == WMT_DIR_NONE);

	/* a stray UPDATE after END must not re-fire the finished gesture */
	wmTouchRecognizerInit(&r);
	feed(&r, &cfg, WMT_GESTURE_BEGIN, 3, 0, 0);
	feed(&r, &cfg, WMT_GESTURE_UPDATE, 3, 100, 0);
	CHECK(wmTouchRecognizerFeed(&r, &(WMGestureEvent){
		.kind = WMT_GESTURE_END, .fingers = 3}, &cfg) == WMT_DIR_RIGHT);
	CHECK(wmTouchRecognizerFeed(&r, &(WMGestureEvent){
		.kind = WMT_GESTURE_UPDATE, .fingers = 3, .dx = 0, .dy = 0}, &cfg) == WMT_DIR_NONE);
	CHECK(wmTouchRecognizerFeed(&r, &(WMGestureEvent){
		.kind = WMT_GESTURE_END, .fingers = 3}, &cfg) == WMT_DIR_NONE);

	/* 3 -> 4 finger transition is a fresh gesture */
	wmTouchRecognizerInit(&r);
	feed(&r, &cfg, WMT_GESTURE_BEGIN, 3, 0, 0);
	feed(&r, &cfg, WMT_GESTURE_UPDATE, 3, 200, 0);
	feed(&r, &cfg, WMT_GESTURE_CANCEL, 3, 0, 0);
	feed(&r, &cfg, WMT_GESTURE_BEGIN, 4, 0, 0);
	feed(&r, &cfg, WMT_GESTURE_UPDATE, 4, 200, 0);
	CHECK(wmTouchRecognizerFeed(&r, &(WMGestureEvent){
		.kind = WMT_GESTURE_END, .fingers = 4}, &cfg) == WMT_DIR_RIGHT);

	/* pinch in and spread are decided by scale at END */
	wmTouchRecognizerInit(&r);
	feed_pinch(&r, &cfg, WMT_GESTURE_BEGIN, 3, 1.0);
	feed_pinch(&r, &cfg, WMT_GESTURE_UPDATE, 3, 0.9);
	feed_pinch(&r, &cfg, WMT_GESTURE_UPDATE, 3, 0.55);
	CHECK(wmTouchRecognizerFeed(&r, &(WMGestureEvent){
		.kind = WMT_GESTURE_END, .type = WMT_GESTURE_PINCH,
		.fingers = 3}, &cfg) == WMT_DIR_PINCH);

	wmTouchRecognizerInit(&r);
	feed_pinch(&r, &cfg, WMT_GESTURE_BEGIN, 3, 1.0);
	feed_pinch(&r, &cfg, WMT_GESTURE_UPDATE, 3, 1.4);
	feed_pinch(&r, &cfg, WMT_GESTURE_UPDATE, 3, 1.78);
	CHECK(wmTouchRecognizerFeed(&r, &(WMGestureEvent){
		.kind = WMT_GESTURE_END, .type = WMT_GESTURE_PINCH,
		.fingers = 3}, &cfg) == WMT_DIR_SPREAD);

	/* a scale inside the threshold band fires nothing */
	wmTouchRecognizerInit(&r);
	feed_pinch(&r, &cfg, WMT_GESTURE_BEGIN, 3, 1.0);
	feed_pinch(&r, &cfg, WMT_GESTURE_UPDATE, 3, 1.2);
	CHECK(wmTouchRecognizerFeed(&r, &(WMGestureEvent){
		.kind = WMT_GESTURE_END, .type = WMT_GESTURE_PINCH,
		.fingers = 3}, &cfg) == WMT_DIR_NONE);

	/* a cancelled pinch is dropped */
	wmTouchRecognizerInit(&r);
	feed_pinch(&r, &cfg, WMT_GESTURE_BEGIN, 3, 1.0);
	feed_pinch(&r, &cfg, WMT_GESTURE_UPDATE, 3, 0.4);
	CHECK(wmTouchRecognizerFeed(&r, &(WMGestureEvent){
		.kind = WMT_GESTURE_CANCEL, .type = WMT_GESTURE_PINCH,
		.fingers = 3}, &cfg) == WMT_DIR_NONE);

	/* four-finger pinch is accepted too */
	wmTouchRecognizerInit(&r);
	feed_pinch(&r, &cfg, WMT_GESTURE_BEGIN, 4, 1.0);
	feed_pinch(&r, &cfg, WMT_GESTURE_UPDATE, 4, 1.9);
	CHECK(wmTouchRecognizerFeed(&r, &(WMGestureEvent){
		.kind = WMT_GESTURE_END, .type = WMT_GESTURE_PINCH,
		.fingers = 4}, &cfg) == WMT_DIR_SPREAD);

	/* two-finger pinch is recognised like any other */
	wmTouchRecognizerInit(&r);
	feed_pinch(&r, &cfg, WMT_GESTURE_BEGIN, 2, 1.0);
	feed_pinch(&r, &cfg, WMT_GESTURE_UPDATE, 2, 2.4);
	CHECK(wmTouchRecognizerFeed(&r, &(WMGestureEvent){
		.kind = WMT_GESTURE_END, .type = WMT_GESTURE_PINCH,
		.fingers = 2}, &cfg) == WMT_DIR_SPREAD);

	/* two-finger swipes are not recognised: libinput reports swipe
	 * gestures for three or more fingers only */
	wmTouchRecognizerInit(&r);
	feed(&r, &cfg, WMT_GESTURE_BEGIN, 2, 0, 0);
	feed(&r, &cfg, WMT_GESTURE_UPDATE, 2, 200, 0);
	CHECK(wmTouchRecognizerFeed(&r, &(WMGestureEvent){
		.kind = WMT_GESTURE_END, .fingers = 2}, &cfg) == WMT_DIR_NONE);

	/* five-finger gestures are recognised too */
	wmTouchRecognizerInit(&r);
	feed(&r, &cfg, WMT_GESTURE_BEGIN, 5, 0, 0);
	feed(&r, &cfg, WMT_GESTURE_UPDATE, 5, 200, 0);
	CHECK(wmTouchRecognizerFeed(&r, &(WMGestureEvent){
		.kind = WMT_GESTURE_END, .fingers = 5}, &cfg) == WMT_DIR_RIGHT);

	wmTouchRecognizerInit(&r);
	feed_pinch(&r, &cfg, WMT_GESTURE_BEGIN, 5, 1.0);
	feed_pinch(&r, &cfg, WMT_GESTURE_UPDATE, 5, 1.9);
	CHECK(wmTouchRecognizerFeed(&r, &(WMGestureEvent){
		.kind = WMT_GESTURE_END, .type = WMT_GESTURE_PINCH,
		.fingers = 5}, &cfg) == WMT_DIR_SPREAD);

	/* six fingers are beyond the configured slots */
	wmTouchRecognizerInit(&r);
	feed(&r, &cfg, WMT_GESTURE_BEGIN, 6, 0, 0);
	feed(&r, &cfg, WMT_GESTURE_UPDATE, 6, 200, 0);
	CHECK(wmTouchRecognizerFeed(&r, &(WMGestureEvent){
		.kind = WMT_GESTURE_END, .fingers = 6}, &cfg) == WMT_DIR_NONE);
}

static void test_domain(void)
{
	WMPropList *pl;
	WMTouchConfig cfg;

	pl = WMCreatePropListFromDescription(
		"{ SwipeThreshold = \"60\"; SwipeAxisMargin = \"2.5\";"
		" PinchThreshold = \"1.8\";"
		" ThreeFingerLeft = \"PrevWorkspace\";"
		" ThreeFingerDown = \"None\";"
		" TwoFingerPinchOut = \"Maximize\";"
		" TwoFingerLeft = \"PrevWorkspace\";"
		" ThreeFingerPinchOut = \"Maximize\";"
		" FourFingerPinchIn = \"Miniaturize\";"
		" FourFingerRight = \"NextWorkspace\";"
		" FiveFingerDown = \"ShowDesktop\"; }");
	CHECK(pl != NULL);
	wmTouchConfigInit(&cfg);
	wmTouchConfigFromPropList(pl, &cfg);
	CHECK(cfg.swipe_threshold == 60);
	CHECK(cfg.swipe_axis_margin > 2.4 && cfg.swipe_axis_margin < 2.6);
	CHECK(cfg.pinch_threshold > 1.7 && cfg.pinch_threshold < 1.9);
	CHECK_STR(wmTouchConfigAction(&cfg, 3, WMT_DIR_LEFT), "PrevWorkspace");
	CHECK(wmTouchConfigAction(&cfg, 3, WMT_DIR_DOWN) == NULL);
	CHECK(wmTouchConfigAction(&cfg, 3, WMT_DIR_RIGHT) == NULL);
	CHECK_STR(wmTouchConfigAction(&cfg, 3, WMT_DIR_SPREAD), "Maximize");
	CHECK_STR(wmTouchConfigAction(&cfg, 2, WMT_DIR_SPREAD), "Maximize");
	/* the TwoFingerLeft key above is not read: libinput has no
	 * two-finger swipes */
	CHECK(wmTouchConfigAction(&cfg, 2, WMT_DIR_LEFT) == NULL);
	CHECK(wmTouchConfigAction(&cfg, 2, WMT_DIR_PINCH) == NULL);
	CHECK(wmTouchConfigAction(&cfg, 3, WMT_DIR_PINCH) == NULL);
	CHECK_STR(wmTouchConfigAction(&cfg, 4, WMT_DIR_PINCH), "Miniaturize");
	CHECK_STR(wmTouchConfigAction(&cfg, 4, WMT_DIR_RIGHT), "NextWorkspace");
	CHECK_STR(wmTouchConfigAction(&cfg, 5, WMT_DIR_DOWN), "ShowDesktop");
	CHECK(wmTouchConfigAction(&cfg, 6, WMT_DIR_DOWN) == NULL);
	wmTouchConfigFree(&cfg);
	WMReleasePropList(pl);

	/* threshold clamp and defaults */
	pl = WMCreatePropListFromDescription(
		"{ SwipeThreshold = \"0\"; SwipeAxisMargin = \"-3\";"
		" PinchThreshold = \"0.5\"; }");
	CHECK(pl != NULL);
	wmTouchConfigInit(&cfg);
	wmTouchConfigFromPropList(pl, &cfg);
	CHECK(cfg.swipe_threshold == 1);
	CHECK(cfg.swipe_axis_margin < 0.0001);
	CHECK(cfg.pinch_threshold > 0.999 && cfg.pinch_threshold < 1.001);
	wmTouchConfigFree(&cfg);
	WMReleasePropList(pl);

	pl = WMCreatePropListFromDescription(
		"{ SwipeThreshold = \"999999\"; PinchThreshold = \"999\"; }");
	CHECK(pl != NULL);
	wmTouchConfigInit(&cfg);
	wmTouchConfigFromPropList(pl, &cfg);
	CHECK(cfg.swipe_threshold == 10000);
	CHECK(cfg.swipe_axis_margin > 1.1 && cfg.swipe_axis_margin < 1.3);
	CHECK(cfg.pinch_threshold > 99.9 && cfg.pinch_threshold < 100.1);
	wmTouchConfigFree(&cfg);
	WMReleasePropList(pl);

	/* absent domain leaves everything inert */
	wmTouchConfigInit(&cfg);
	wmTouchConfigFromPropList(NULL, &cfg);
	CHECK(cfg.swipe_threshold == WMTOUCH_DEFAULT_THRESHOLD);
	CHECK(cfg.pinch_threshold > 1.4 && cfg.pinch_threshold < 1.6);
	CHECK(wmTouchConfigAction(&cfg, 3, WMT_DIR_LEFT) == NULL);
	CHECK(wmTouchConfigAction(&cfg, 4, WMT_DIR_UP) == NULL);
	CHECK(wmTouchConfigAction(&cfg, 3, WMT_DIR_SPREAD) == NULL);
	wmTouchConfigFree(&cfg);
}

static void test_replay_parser(void)
{
	WMGestureEvent ev;

	CHECK(wmTouchParseEventLine("begin 3\n", &ev) == 1);
	CHECK(ev.kind == WMT_GESTURE_BEGIN && ev.fingers == 3);
	CHECK(ev.type == WMT_GESTURE_SWIPE);
	CHECK(wmTouchParseEventLine("update 3 12.5 -4\n", &ev) == 1);
	CHECK(ev.kind == WMT_GESTURE_UPDATE && ev.fingers == 3);
	CHECK(ev.dx > 12.4 && ev.dx < 12.6);
	CHECK(ev.dy > -4.1 && ev.dy < -3.9);
	CHECK(wmTouchParseEventLine("end 3\n", &ev) == 1);
	CHECK(ev.kind == WMT_GESTURE_END && ev.fingers == 3);
	CHECK(wmTouchParseEventLine("cancel 4\n", &ev) == 1);
	CHECK(ev.kind == WMT_GESTURE_CANCEL && ev.fingers == 4);

	CHECK(wmTouchParseEventLine("pinch-begin 3\n", &ev) == 1);
	CHECK(ev.kind == WMT_GESTURE_BEGIN && ev.fingers == 3);
	CHECK(ev.type == WMT_GESTURE_PINCH);
	CHECK(wmTouchParseEventLine("pinch-update 3 0.55\n", &ev) == 1);
	CHECK(ev.kind == WMT_GESTURE_UPDATE && ev.fingers == 3);
	CHECK(ev.type == WMT_GESTURE_PINCH);
	CHECK(ev.scale > 0.54 && ev.scale < 0.56);
	CHECK(wmTouchParseEventLine("pinch-end 3\n", &ev) == 1);
	CHECK(ev.kind == WMT_GESTURE_END && ev.fingers == 3);
	CHECK(ev.type == WMT_GESTURE_PINCH);
	CHECK(wmTouchParseEventLine("pinch-cancel 4\n", &ev) == 1);
	CHECK(ev.kind == WMT_GESTURE_CANCEL && ev.fingers == 4);

	CHECK(wmTouchParseEventLine("\n", &ev) == 0);
	CHECK(wmTouchParseEventLine("   \n", &ev) == 0);
	CHECK(wmTouchParseEventLine("# a comment\n", &ev) == 0);

	CHECK(wmTouchParseEventLine("bogus 3\n", &ev) == -1);
	CHECK(wmTouchParseEventLine("update 3 1\n", &ev) == -1);
	CHECK(wmTouchParseEventLine("end 3 x\n", &ev) == -1);
	CHECK(wmTouchParseEventLine("begin 0\n", &ev) == -1);
}

int main(void)
{
	test_recognizer();
	test_domain();
	test_replay_parser();
	return check_report("wmtouch-core");
}
