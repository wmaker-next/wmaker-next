
/*
 * WINGs internal handlers: timer, idle and input handlers
 *
 * Backed by libev. The public WUtil.h API (WMAddTimerHandler,
 * WMAddPersistentTimerHandler, WMAddIdleHandler, WMAddInputHandler and
 * the matching WMDelete* calls) is unchanged, callers in src/, WPrefs
 * and util/ are not aware of the backend. libev picks epoll on Linux
 * and kqueue on the BSDs at runtime, with poll/select as last resort,
 * so there is one code path here for every supported platform.
 */

#include "wconfig.h"
#include "WINGsP.h"

#include <unistd.h>
#include <ev.h>

typedef struct InputHandler {
	ev_io watcher;		/* first member, the trampoline casts back */
	WMInputProc *callback;
	void *clientData;
	int mask;		/* WIReadMask | WIWriteMask | WIExceptMask */
	struct InputHandler *next;
	unsigned firing:1;
	unsigned deleted:1;
} InputHandler;

typedef struct TimerHandler {
	ev_timer watcher;	/* first member, the trampoline casts back */
	WMCallback *callback;
	void *clientData;
	struct TimerHandler *next;	/* unsorted registry, for WMDeleteTimerWithClientData */
	ev_tstamp due;		/* monotonic deadline, mirrors the libev schedule
				 * so the compat runner can test it without
				 * peeking at watcher internals */
	unsigned persistent:1;
	unsigned firing:1;
	unsigned deleted:1;
} TimerHandler;

typedef struct IdleHandler {
	ev_idle watcher;	/* first member, the trampoline casts back */
	WMCallback *callback;
	void *clientData;
	struct IdleHandler *next;
	unsigned firing:1;
	unsigned deleted:1;
} IdleHandler;

static struct ev_loop *w_loop;
static ev_prepare w_prepare;

/* unsorted registries, kept only so the WMDelete* helpers and the
 * type-specific compatibility runners can walk live handlers */
static TimerHandler *timerHandler;
static IdleHandler *idleHandler;
static InputHandler *inputHandler;

static int idle_handler_count;
static int input_handler_count;

/* set while the compat runners walk a registry, so WMDelete*Handler defers
 * the unlink+free of any non-firing entry instead of invalidating the
 * walker's saved next pointer */
static int timer_walking;
static int idle_walking;

/* lazily-started watcher on ConnectionNumber(dpy), see W_HandleInputEvents */
static ev_io w_xfd_watcher;
static int w_xfd = -1;

/* set only by input trampolines and the X-fd watcher, never by timer or
 * idle callbacks, so W_HandleInputEvents() keeps returning False when it
 * woke only because a timer expired (the old select() arm did the same) */
static Bool input_fired;

static void prepareTrampoline(struct ev_loop *loop, ev_prepare *w, int revents)
{
	(void)loop;
	(void)w;
	(void)revents;

	W_FlushASAPNotificationQueue();
}

static struct ev_loop *W_EventLoop(void)
{
	if (!w_loop) {
		/* a non-default loop, the default loop installs its own
		 * SIGCHLD handler to back ev_child watchers and would
		 * overwrite the buryChild handler wmaker sets in StartUp(),
		 * silently breaking every wAddDeathHandler callback */
		w_loop = ev_loop_new(EVFLAG_AUTO);
		ev_prepare_init(&w_prepare, prepareTrampoline);
		ev_prepare_start(w_loop, &w_prepare);
		/* the prepare watcher must not keep ev_run() from returning
		 * when nothing else is pending */
		ev_unref(w_loop);
	}
	return w_loop;
}

/* ---- timers ---------------------------------------------------------- */

static void unlinkTimer(TimerHandler *h)
{
	TimerHandler **pp = &timerHandler;

	while (*pp) {
		if (*pp == h) {
			*pp = h->next;
			return;
		}
		pp = &(*pp)->next;
	}
}

static void timerTrampoline(struct ev_loop *loop, ev_timer *w, int revents)
{
	TimerHandler *h = (TimerHandler *)w;

	(void)revents;

	if (h->firing)
		/* a compat-runner frame or an outer trampoline already owns
		 * this handler, libev has rearmed a persistent watcher past
		 * its own callback duration, do not recurse */
		return;

	h->firing = 1;
	if (h->callback)
		(*h->callback)(h->clientData);
	h->firing = 0;

	if (h->deleted || !h->persistent) {
		/* one-shot timers self-destruct after firing, matching the old
		 * behaviour where W_CheckTimerHandlers() freed them, callers
		 * must not touch the WMHandlerID after the callback ran */
		ev_timer_stop(loop, &h->watcher);
		if (timer_walking) {
			/* a compat-runner walk is on the stack above this
			 * ev_run() frame and may still hold h or h->next,
			 * defer the free to its sweep */
			h->deleted = 1;
		} else {
			unlinkTimer(h);
			wfree(h);
		}
	} else {
		/* libev already rearmed the repeating watcher before invoking
		 * us, mirror the new deadline for the compat runner */
		h->due = ev_now(loop) + h->watcher.repeat;
	}
}

static WMHandlerID addTimer(int milliseconds, WMCallback *callback, void *cdata, Bool persistent)
{
	struct ev_loop *loop = W_EventLoop();
	TimerHandler *h;
	double after = milliseconds / 1000.0;

	h = wmalloc(sizeof(*h));
	h->callback = callback;
	h->clientData = cdata;
	h->persistent = persistent ? 1 : 0;

	ev_now_update(loop);
	ev_timer_init(&h->watcher, timerTrampoline, after, persistent ? after : 0.0);
	ev_timer_start(loop, &h->watcher);
	h->due = ev_now(loop) + after;

	h->next = timerHandler;
	timerHandler = h;

	return h;
}

WMHandlerID WMAddTimerHandler(int milliseconds, WMCallback *callback, void *cdata)
{
	return addTimer(milliseconds, callback, cdata, False);
}

WMHandlerID WMAddPersistentTimerHandler(int milliseconds, WMCallback *callback, void *cdata)
{
	return addTimer(milliseconds, callback, cdata, True);
}

void WMDeleteTimerHandler(WMHandlerID handlerID)
{
	TimerHandler *h = (TimerHandler *)handlerID;

	if (!h || h->deleted)
		return;

	ev_timer_stop(W_EventLoop(), &h->watcher);
	h->persistent = 0;

	if (h->firing || timer_walking) {
		/* called from inside its own callback, or from another
		 * callback while the compat runner is walking the registry,
		 * the dispatch path owns the final unlink and free */
		h->deleted = 1;
		return;
	}

	unlinkTimer(h);
	wfree(h);
}

void WMDeleteTimerWithClientData(void *cdata)
{
	TimerHandler *h;

	if (!cdata)
		return;

	for (h = timerHandler; h; h = h->next) {
		if (h->clientData == cdata) {
			/* first match only, same as the original implementation */
			WMDeleteTimerHandler(h);
			return;
		}
	}
}

/* ---- idle ------------------------------------------------------------ */

static void unlinkIdle(IdleHandler *h)
{
	IdleHandler **pp = &idleHandler;

	while (*pp) {
		if (*pp == h) {
			*pp = h->next;
			return;
		}
		pp = &(*pp)->next;
	}
}

static void idleTrampoline(struct ev_loop *loop, ev_idle *w, int revents)
{
	IdleHandler *h = (IdleHandler *)w;

	(void)revents;

	if (h->firing)
		return;

	/* idle handlers are one-shot, the old W_CheckIdleHandlers() removed
	 * them after firing, ev_idle is level-triggered so stop before the
	 * callback or a re-entrant ev_run() would re-dispatch this watcher
	 * unboundedly */
	ev_idle_stop(loop, &h->watcher);

	h->firing = 1;
	if (h->callback)
		(*h->callback)(h->clientData);
	h->firing = 0;

	if (idle_walking) {
		h->deleted = 1;
	} else {
		unlinkIdle(h);
		idle_handler_count--;
		wfree(h);
	}
}

WMHandlerID WMAddIdleHandler(WMCallback *callback, void *cdata)
{
	IdleHandler *h;

	h = wmalloc(sizeof(*h));
	h->callback = callback;
	h->clientData = cdata;

	ev_idle_init(&h->watcher, idleTrampoline);
	ev_idle_start(W_EventLoop(), &h->watcher);

	h->next = idleHandler;
	idleHandler = h;
	idle_handler_count++;

	return h;
}

void WMDeleteIdleHandler(WMHandlerID handlerID)
{
	IdleHandler *h = (IdleHandler *)handlerID;

	if (!h || h->deleted)
		return;

	ev_idle_stop(W_EventLoop(), &h->watcher);

	if (h->firing || idle_walking) {
		h->deleted = 1;
		return;
	}

	unlinkIdle(h);
	idle_handler_count--;
	wfree(h);
}

/* ---- input ----------------------------------------------------------- */

static void unlinkInput(InputHandler *h)
{
	InputHandler **pp = &inputHandler;

	while (*pp) {
		if (*pp == h) {
			*pp = h->next;
			return;
		}
		pp = &(*pp)->next;
	}
}

static void inputTrampoline(struct ev_loop *loop, ev_io *w, int revents)
{
	InputHandler *h = (InputHandler *)w;
	int mask = 0;

	(void)loop;

	if (revents & EV_READ) {
		if (h->mask & WIReadMask)
			mask |= WIReadMask;
		/* libev surfaces POLLERR/POLLHUP through EV_READ, there is no
		 * separate exception set, no caller in the tree passes
		 * WIExceptMask without WIReadMask but honour it anyway */
		if (h->mask & WIExceptMask)
			mask |= WIExceptMask;
	}
	if ((revents & EV_WRITE) && (h->mask & WIWriteMask))
		mask |= WIWriteMask;

	input_fired = True;

	if (mask && h->callback) {
		h->firing = 1;
		(*h->callback)(w->fd, mask, h->clientData);
		h->firing = 0;

		if (h->deleted) {
			unlinkInput(h);
			input_handler_count--;
			wfree(h);
		}
	}
}

WMHandlerID WMAddInputHandler(int fd, int condition, WMInputProc *proc, void *clientData)
{
	InputHandler *h;
	int events = 0;

	h = wmalloc(sizeof(*h));
	h->callback = proc;
	h->clientData = clientData;
	h->mask = condition;

	if (condition & (WIReadMask | WIExceptMask))
		events |= EV_READ;
	if (condition & WIWriteMask)
		events |= EV_WRITE;

	ev_io_init(&h->watcher, inputTrampoline, fd, events);
	ev_io_start(W_EventLoop(), &h->watcher);

	h->next = inputHandler;
	inputHandler = h;
	input_handler_count++;

	return h;
}

void WMDeleteInputHandler(WMHandlerID handlerID)
{
	InputHandler *h = (InputHandler *)handlerID;

	if (!h)
		return;

	ev_io_stop(W_EventLoop(), &h->watcher);

	if (h->firing) {
		h->deleted = 1;
		return;
	}

	unlinkInput(h);
	input_handler_count--;
	wfree(h);
}

/* ---- compatibility runners ------------------------------------------ */

/*
 * W_CheckTimerHandlers and W_CheckIdleHandlers are the legacy entry points
 * still called from WINGs/wevent.c, WINGs/wutil.c and src/misc.c. They are
 * type-specific on purpose: a bare ev_run(EVRUN_NOWAIT) would let a timer
 * check dispatch input callbacks and an idle check fire timers, which would
 * change the ordering documented around WMNextEvent(). Walk only the
 * matching registry instead. These shims retire once the wevent.c ladder
 * is collapsed onto ev_run() directly (PLAN-wings-libev.md step 4).
 */

static void sweepDeletedTimers(void)
{
	TimerHandler **pp = &timerHandler;

	while (*pp) {
		TimerHandler *h = *pp;
		if (h->deleted && !h->firing) {
			*pp = h->next;
			wfree(h);
		} else {
			pp = &h->next;
		}
	}
}

void W_CheckTimerHandlers(void)
{
	struct ev_loop *loop = W_EventLoop();
	TimerHandler *h;
	ev_tstamp now;

	if (!timerHandler) {
		W_FlushASAPNotificationQueue();
		return;
	}

	ev_now_update(loop);
	now = ev_now(loop);

	/* deletions during dispatch (self or other) only mark deleted=1,
	 * the sweep below frees them, so h->next stays valid for the walk */
	timer_walking++;
	for (h = timerHandler; h; h = h->next) {
		if (h->firing || h->deleted || h->due > now)
			continue;

		/* take the watcher out of libev's heap before running the
		 * callback so a re-entrant ev_run() from inside it (modal
		 * WMNextEvent loop, nested WHandleEvents) cannot re-dispatch
		 * the same due timer through timerTrampoline() */
		ev_timer_stop(loop, &h->watcher);

		h->firing = 1;
		if (h->callback)
			(*h->callback)(h->clientData);
		h->firing = 0;

		if (h->deleted || !h->persistent) {
			h->deleted = 1;
		} else {
			/* persistent and survived, rearm at now + repeat in
			 * both libev's heap and our mirror so the next compat
			 * poll honours the interval instead of re-firing
			 * immediately, ev_timer_again() on a stopped watcher
			 * starts it with after = repeat */
			ev_timer_again(loop, &h->watcher);
			h->due = ev_now(loop) + h->watcher.repeat;
		}
	}
	timer_walking--;

	if (!timer_walking)
		sweepDeletedTimers();

	W_FlushASAPNotificationQueue();
}

static void sweepDeletedIdles(void)
{
	IdleHandler **pp = &idleHandler;

	while (*pp) {
		IdleHandler *h = *pp;
		if (h->deleted && !h->firing) {
			*pp = h->next;
			idle_handler_count--;
			wfree(h);
		} else {
			pp = &h->next;
		}
	}
}

Bool W_CheckIdleHandlers(void)
{
	struct ev_loop *loop = W_EventLoop();
	IdleHandler *h;

	if (idle_handler_count == 0) {
		W_FlushIdleNotificationQueue();
		/* an observer in the idle queue may have added an idle handler */
		return idle_handler_count > 0;
	}

	idle_walking++;
	for (h = idleHandler; h; h = h->next) {
		if (h->firing || h->deleted)
			continue;

		/* same ownership rule as timers, stop before manual dispatch
		 * so a re-entrant ev_run() cannot fire idleTrampoline() on
		 * this watcher while we still own h */
		ev_idle_stop(loop, &h->watcher);

		h->firing = 1;
		if (h->callback)
			(*h->callback)(h->clientData);
		h->firing = 0;

		/* idle handlers are one-shot */
		h->deleted = 1;
	}
	idle_walking--;

	if (!idle_walking)
		sweepDeletedIdles();

	W_FlushIdleNotificationQueue();

	/* not necessarily False, a callback can re-add itself */
	return idle_handler_count > 0;
}

/* ---- loop driver ----------------------------------------------------- */

static void xfdTrampoline(struct ev_loop *loop, ev_io *w, int revents)
{
	(void)w;
	(void)revents;

	input_fired = True;
	/* hand control back to WMNextEvent() so it drains Xlib's queue */
	ev_break(loop, EVBREAK_ONE);
}

Bool W_HandleInputEvents(Bool waitForInput, int inputfd)
{
	struct ev_loop *loop = W_EventLoop();

	if (inputfd >= 0 && inputfd != w_xfd) {
		if (w_xfd >= 0)
			ev_io_stop(loop, &w_xfd_watcher);
		ev_io_init(&w_xfd_watcher, xfdTrampoline, inputfd, EV_READ);
		/* keep this watcher referenced, the X connection is exactly
		 * what WMNextEvent()/WMMaskEvent() must block on when no
		 * timers, idles or WINGs input handlers are registered, an
		 * unref'd-only loop makes ev_run(EVRUN_ONCE) return at once
		 * and turns those into a busy spin */
		ev_io_start(loop, &w_xfd_watcher);
		w_xfd = inputfd;
	}

	if (inputfd < 0 && input_handler_count == 0) {
		/* preserve the old X-less shim behaviour, do not block on
		 * timers only when no input handlers are registered, the
		 * direct ev_run path may revisit this intentionally */
		W_FlushASAPNotificationQueue();
		return False;
	}

	input_fired = False;
	ev_run(loop, waitForInput ? EVRUN_ONCE : EVRUN_NOWAIT);

	W_FlushASAPNotificationQueue();

	return input_fired;
}

/* ---- shutdown -------------------------------------------------------- */

void W_EventLoopShutdown(void)
{
	if (!w_loop)
		return;

	while (timerHandler) {
		TimerHandler *h = timerHandler;
		ev_timer_stop(w_loop, &h->watcher);
		timerHandler = h->next;
		wfree(h);
	}
	while (idleHandler) {
		IdleHandler *h = idleHandler;
		ev_idle_stop(w_loop, &h->watcher);
		idleHandler = h->next;
		wfree(h);
	}
	idle_handler_count = 0;
	while (inputHandler) {
		InputHandler *h = inputHandler;
		ev_io_stop(w_loop, &h->watcher);
		inputHandler = h->next;
		wfree(h);
	}
	input_handler_count = 0;

	if (w_xfd >= 0) {
		ev_io_stop(w_loop, &w_xfd_watcher);
		w_xfd = -1;
	}

	ev_ref(w_loop);
	ev_prepare_stop(w_loop, &w_prepare);

	ev_loop_destroy(w_loop);
	w_loop = NULL;
}
