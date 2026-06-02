/*
 *  check_handlers - automated unit tests for the libev-backed WINGs
 *  timer, idle and input handlers
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Run by `make check`. Exit status 0 on pass, non-zero on the first
 *  failing assertion with the file and line printed to stderr.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <WINGs/WUtil.h>

/* WIReadMask and WMInitializeApplication/WMReleaseApplication live in
 * <WINGs/WINGs.h> which pulls in the full X11 toolkit headers, but the
 * symbols themselves are in libWUtil, mirror just what this X-less
 * test needs */
#ifndef WIReadMask
#define WIReadMask (1 << 0)
#endif
extern void WMInitializeApplication(const char *applicationName, int *argc, char **argv);
extern void WMReleaseApplication(void);
/* internal loop driver, declared in WINGsP.h, used here to test that the
 * X-fd-style watcher actually keeps ev_run(EVRUN_ONCE) blocking */
extern Bool W_HandleInputEvents(Bool waitForInput, int inputfd);

static int failures;

#define CHECK(cond) do {                                                   \
	if (!(cond)) {                                                     \
	    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);\
	    failures++;                                                    \
	}                                                                  \
    } while (0)

static double mono_now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* drive the X-less loop until *flag reaches want or the monotonic
 * deadline elapses, WHandleEvents() is non-blocking when no input
 * handlers are registered so yield briefly between polls instead of
 * spinning a core flat out */
static void pump_until(int *flag, int want, double timeout_s)
{
	double deadline = mono_now() + timeout_s;

	while (*flag < want && mono_now() < deadline) {
		WHandleEvents();
		usleep(500);
	}
}

/* drive the loop for a fixed wall-clock window regardless of *flag */
static void pump_for_ms(int ms)
{
	double deadline = mono_now() + ms / 1000.0;

	while (mono_now() < deadline) {
		WHandleEvents();
		usleep(500);
	}
}

/* ---- timers ---------------------------------------------------------- */

static void inc_int(void *data)
{
	(*(int *)data)++;
}

static void test_timer_oneshot(void)
{
	int hits = 0;

	WMAddTimerHandler(5, inc_int, &hits);
	pump_until(&hits, 1, 2.0);
	CHECK(hits == 1);

	/* one-shot must not fire again */
	WHandleEvents();
	WHandleEvents();
	CHECK(hits == 1);
}

static void test_timer_persistent(void)
{
	int hits = 0;
	WMHandlerID id;

	id = WMAddPersistentTimerHandler(3, inc_int, &hits);
	pump_until(&hits, 3, 2.0);
	CHECK(hits >= 3);

	WMDeleteTimerHandler(id);
	hits = 0;
	WHandleEvents();
	WHandleEvents();
	CHECK(hits == 0);
}

static void test_timer_persistent_spacing(void)
{
	int hits = 0;
	WMHandlerID id;

	/* a 20 ms persistent timer driven for ~110 ms must fire a bounded
	 * handful of times, if the compat runner failed to rearm it would
	 * fire on every WHandleEvents() iteration and hits would explode */
	id = WMAddPersistentTimerHandler(20, inc_int, &hits);
	pump_for_ms(110);
	WMDeleteTimerHandler(id);

	CHECK(hits >= 3);
	CHECK(hits <= 10);
}

struct self_del {
	WMHandlerID id;
	int hits;
};

static void timer_self_delete(void *data)
{
	struct self_del *s = data;

	s->hits++;
	WMDeleteTimerHandler(s->id);
}

static void test_timer_self_delete(void)
{
	struct self_del s = { NULL, 0 };

	/* one-shot deleting itself from inside the callback */
	s.id = WMAddTimerHandler(2, timer_self_delete, &s);
	pump_until(&s.hits, 1, 2.0);
	CHECK(s.hits == 1);

	/* persistent deleting itself from inside the callback, must fire
	 * exactly once and not be rescheduled */
	s.hits = 0;
	s.id = WMAddPersistentTimerHandler(2, timer_self_delete, &s);
	pump_until(&s.hits, 1, 2.0);
	CHECK(s.hits == 1);
	WHandleEvents();
	WHandleEvents();
	CHECK(s.hits == 1);
}

struct cross_del {
	WMHandlerID self;
	WMHandlerID other;
	int hits;
	int *other_hits;
};

static void timer_delete_other(void *data)
{
	struct cross_del *c = data;

	c->hits++;
	WMDeleteTimerHandler(c->other);
}

static void test_timer_delete_other(void)
{
	struct cross_del a = { NULL, NULL, 0, NULL };
	int b_hits = 0;

	a.other_hits = &b_hits;
	/* both due on the same poll, A's callback deletes B, B must not
	 * fire and the registry walk must not touch freed memory */
	a.other = WMAddTimerHandler(1, inc_int, &b_hits);
	a.self  = WMAddTimerHandler(1, timer_delete_other, &a);

	pump_until(&a.hits, 1, 2.0);
	WHandleEvents();
	CHECK(a.hits == 1);
	CHECK(b_hits == 0);
}

static void test_timer_delete_by_clientdata(void)
{
	int a = 0, b = 0;
	WMHandlerID idb;

	WMAddTimerHandler(2, inc_int, &a);
	idb = WMAddTimerHandler(2, inc_int, &b);

	/* first match by clientData, leaves the other timer intact */
	WMDeleteTimerWithClientData(&a);

	pump_until(&b, 1, 2.0);
	CHECK(a == 0);
	CHECK(b == 1);

	(void)idb;
}

/* ---- idle ------------------------------------------------------------ */

static void test_idle_oneshot(void)
{
	int hits = 0;

	WMAddIdleHandler(inc_int, &hits);
	WHandleEvents();
	CHECK(hits == 1);

	WHandleEvents();
	CHECK(hits == 1);
}

static void idle_self_delete(void *data)
{
	struct self_del *s = data;

	s->hits++;
	WMDeleteIdleHandler(s->id);
}

static void test_idle_self_delete(void)
{
	struct self_del s = { NULL, 0 };

	s.id = WMAddIdleHandler(idle_self_delete, &s);
	WHandleEvents();
	CHECK(s.hits == 1);
}

static void idle_delete_other(void *data)
{
	struct cross_del *c = data;

	c->hits++;
	WMDeleteIdleHandler(c->other);
}

static void test_idle_delete_other(void)
{
	struct cross_del a = { NULL, NULL, 0, NULL };
	int b_hits = 0;

	a.other_hits = &b_hits;
	a.other = WMAddIdleHandler(inc_int, &b_hits);
	a.self  = WMAddIdleHandler(idle_delete_other, &a);

	WHandleEvents();
	CHECK(a.hits == 1);
	CHECK(b_hits == 0);
}

/* ---- input ----------------------------------------------------------- */

struct in_del {
	WMHandlerID id;
	int hits;
	int rfd;
};

static void input_self_delete(int fd, int mask, void *data)
{
	struct in_del *s = data;
	char buf[16];
	ssize_t r;

	(void)mask;
	r = read(fd, buf, sizeof(buf));
	(void)r;
	s->hits++;
	WMDeleteInputHandler(s->id);
}

static void test_input_pipe(void)
{
	int fds[2];
	struct in_del s = { NULL, 0, -1 };

	CHECK(pipe(fds) == 0);
	s.rfd = fds[0];
	s.id = WMAddInputHandler(fds[0], WIReadMask, input_self_delete, &s);

	CHECK(write(fds[1], "x", 1) == 1);
	pump_until(&s.hits, 1, 2.0);
	CHECK(s.hits == 1);

	/* handler deleted itself, a second write must not deliver */
	CHECK(write(fds[1], "y", 1) == 1);
	WHandleEvents();
	CHECK(s.hits == 1);

	close(fds[0]);
	close(fds[1]);
}

/* ---- re-entrancy ----------------------------------------------------- */

/* deliberately does not read, so once one byte is written the pipe stays
 * level-ready and every nested ev_run() at any depth has a wake source,
 * this stands in for the X connection fd a real modal WMNextEvent() loop
 * would block on */
static void hold_input(int fd, int mask, void *data)
{
	(void)fd;
	(void)mask;
	(*(int *)data)++;
}

struct reent {
	int hits;
	int wfd;
};

static void reentrant_cb(void *data)
{
	struct reent *r = data;

	r->hits++;
	/* arm the standing wake source before re-entering */
	if (write(r->wfd, "x", 1) != 1)
		return;
	/* re-enter the loop while the outer dispatch still owns this
	 * handler, the inner WHandleEvents() reaches ev_run() because an
	 * input handler is registered, which would re-dispatch this same
	 * watcher and free it under us if the dispatch path had not taken
	 * ownership (stopped the watcher) first */
	WHandleEvents();
}

static void test_timer_reentrant(void)
{
	int fds[2];
	int in_hits = 0;
	struct reent r = { 0, -1 };
	WMHandlerID in;

	CHECK(pipe(fds) == 0);
	r.wfd = fds[1];
	/* a live input handler is what makes W_HandleInputEvents() call
	 * ev_run() in the X-less path */
	in = WMAddInputHandler(fds[0], WIReadMask, hold_input, &in_hits);

	WMAddTimerHandler(1, reentrant_cb, &r);
	pump_until(&r.hits, 1, 2.0);

	/* exactly once, no double-dispatch, no use-after-free, and the
	 * inner ev_run() really ran (it dispatched the input handler) */
	CHECK(r.hits == 1);
	CHECK(in_hits > 0);

	WMDeleteInputHandler(in);
	close(fds[0]);
	close(fds[1]);
}

static void test_idle_reentrant(void)
{
	int fds[2];
	int in_hits = 0;
	struct reent r = { 0, -1 };
	WMHandlerID in;

	CHECK(pipe(fds) == 0);
	r.wfd = fds[1];
	in = WMAddInputHandler(fds[0], WIReadMask, hold_input, &in_hits);

	WMAddIdleHandler(reentrant_cb, &r);
	WHandleEvents();
	CHECK(r.hits == 1);
	CHECK(in_hits > 0);

	WMDeleteInputHandler(in);
	close(fds[0]);
	close(fds[1]);
}

/* ---- inputfd blocking ----------------------------------------------- */

static void test_inputfd_blocks(void)
{
	int fds[2];
	pid_t kid;
	double t0, dt;
	Bool ret;
	int status;

	/* W_HandleInputEvents(True, fd) with no other referenced watchers
	 * must block on fd until it becomes readable, this is the
	 * WMNextEvent() path with ConnectionNumber(dpy) substituted by a
	 * pipe, an unref'd X-fd watcher would make ev_run(EVRUN_ONCE)
	 * return immediately and turn WMNextEvent into a busy spin */
	CHECK(pipe(fds) == 0);

	kid = fork();
	if (kid == 0) {
		close(fds[0]);
		usleep(80 * 1000);
		ssize_t w = write(fds[1], "x", 1);
		(void)w;
		_exit(0);
	}
	CHECK(kid > 0);
	close(fds[1]);

	t0 = mono_now();
	ret = W_HandleInputEvents(True, fds[0]);
	dt = mono_now() - t0;

	CHECK(ret == True);
	/* blocked for roughly the child's delay, generous lower bound for
	 * scheduler jitter, upper bound catches a hang */
	CHECK(dt >= 0.04);
	CHECK(dt < 2.0);

	waitpid(kid, &status, 0);
	close(fds[0]);
}

/* ---- notification queue --------------------------------------------- */

static void notif_observer(void *observerData, WMNotification *notif)
{
	(void)notif;
	(*(int *)observerData)++;
}

static void test_notification_asap_idle(void)
{
	int asap_hits = 0, idle_hits = 0;
	WMNotificationQueue *q;

	WMAddNotificationObserver(notif_observer, &asap_hits, "check.asap", NULL);
	WMAddNotificationObserver(notif_observer, &idle_hits, "check.idle", NULL);

	q = WMGetDefaultNotificationQueue();
	WMEnqueueNotification(q, WMCreateNotification("check.asap", NULL, NULL), WMPostASAP);
	WMEnqueueNotification(q, WMCreateNotification("check.idle", NULL, NULL), WMPostWhenIdle);

	/* nothing delivered yet */
	CHECK(asap_hits == 0);
	CHECK(idle_hits == 0);

	WHandleEvents();

	/* ASAP flushes from the timer/input path, WhenIdle flushes from
	 * W_CheckIdleHandlers() even though no idle handler was registered
	 * before the post */
	CHECK(asap_hits == 1);
	CHECK(idle_hits == 1);

	WMRemoveNotificationObserver(&asap_hits);
	WMRemoveNotificationObserver(&idle_hits);
}

/* ---- ordering -------------------------------------------------------- */

static void test_zero_timer_and_idle(void)
{
	int t = 0, i = 0;

	WMAddTimerHandler(0, inc_int, &t);
	WMAddIdleHandler(inc_int, &i);

	WHandleEvents();
	/* both must have run in one WHandleEvents() pass */
	CHECK(t == 1);
	CHECK(i == 1);
}

int main(int argc, char **argv)
{
	/* needed for the notification-center hash tables */
	WMInitializeApplication("check_handlers", &argc, argv);

	test_timer_oneshot();
	test_timer_persistent();
	test_timer_persistent_spacing();
	test_timer_self_delete();
	test_timer_delete_other();
	test_timer_delete_by_clientdata();
	test_idle_oneshot();
	test_idle_self_delete();
	test_idle_delete_other();
	test_input_pipe();
	test_timer_reentrant();
	test_idle_reentrant();
	test_notification_asap_idle();
	test_zero_timer_and_idle();
	/* last, it leaves the static X-fd watcher pointing at a closed fd
	 * until W_EventLoopShutdown() stops it */
	test_inputfd_blocks();

	WMReleaseApplication();
	wutil_shutdown();

	if (failures) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	printf("ok - check_handlers\n");
	return 0;
}
