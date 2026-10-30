/*
 *  handlers.c
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 2001-2004 Dan Pascu
 *  Copyright (c) 2009-2026 Window Maker Team
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, see <https://www.gnu.org/licenses/>.
 */

/*
 * WINGs internal handlers: timer, idle and input handlers
 *
 * Backed by libev. The public WUtil.h API (WMAddTimerHandler,
 * WMAddPersistentTimerHandler, WMAddIdleHandler, WMAddInputHandler and
 * the matching WMDelete* calls) is unchanged, callers in src/, WPrefs
 * and util/ are not aware of the backend. libev picks epoll on Linux
 * and kqueue on the BSDs at runtime, with poll/select as last resort,
 * so there is one code path here for every supported platform.
 *
 * Dispatch is purely ev_run()-driven: WMNextEvent/WMMaskEvent/WHandleEvents
 * call W_HandleInputEvents() which wraps one ev_run() iteration, libev
 * fires due timers, runs idle watchers when nothing else is pending, and
 * delivers fd readiness in the same pass. The ev_prepare watcher flushes
 * the ASAP notification queue just before each poll, the ev_check watcher
 * flushes the WhenIdle notification queue just after.
 *
 * Deferred destruction: handler structs are never freed inside ev_run().
 * Trampolines and WMDelete*Handler() mark a handler deleted while
 * `dispatching > 0`, and the sweep after ev_run() unlinks and frees every
 * marked entry. This makes self-delete, cross-delete and "delete a
 * one-shot whose handle the caller still holds after it already fired"
 * all safe regardless of libev's heap dispatch order.
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
static ev_check w_check;

/* unsorted registries, kept so WMDeleteTimerWithClientData can search by
 * clientData, the post-dispatch sweep can free deferred-deleted entries,
 * and W_EventLoopShutdown can stop and free everything */
static TimerHandler *timerHandler;
static IdleHandler *idleHandler;
static InputHandler *inputHandler;

/* nesting depth of ev_run() on the stack, while non-zero every delete
 * path defers the unlink+free to the sweep that runs when it drops to
 * zero, so a callback can delete any handler (its own, another due in
 * the same ev_run() round, or a one-shot that already fired and
 * self-marked) without invalidating a pointer libev or another callback
 * still holds */
static int dispatching;

/* lazily-started watcher on the X connection fd, see W_HandleInputEvents */
static ev_io w_xfd_watcher;
static int w_xfd = -1;

/* set only by input trampolines and the X-fd watcher, never by timer or
 * idle callbacks, so W_HandleInputEvents() keeps returning False when it
 * woke only because a timer expired (the old select() arm did the same) */
static bool input_fired;

/* single-slot per-iteration hook run from prepareTrampoline just before
 * each poll, lets an application apply deferred server-side state (e.g.
 * restacking) once per ev_run() iteration including nested modal loops */
static WMPrepareProc *w_prepare_hook;
static void *w_prepare_hook_data;

static void prepareTrampoline(struct ev_loop *loop, ev_prepare *w, int revents)
{
	(void)loop;
	(void)w;
	(void)revents;

	W_FlushASAPNotificationQueue();

	if (w_prepare_hook)
		(*w_prepare_hook)(w_prepare_hook_data);
}

static void checkTrampoline(struct ev_loop *loop, ev_check *w, int revents)
{
	(void)loop;
	(void)w;
	(void)revents;

	/* runs once per ev_run() iteration after dispatch, this is where
	 * the old W_CheckIdleHandlers() flushed the WMPostWhenIdle queue,
	 * including the case where no idle handler had been registered
	 * before the post */
	W_FlushIdleNotificationQueue();
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
		ev_unref(w_loop);

		ev_check_init(&w_check, checkTrampoline);
		ev_check_start(w_loop, &w_check);
		/* prepare and check are bookkeeping, they must not keep
		 * ev_run() from returning when nothing else is pending */
		ev_unref(w_loop);
	}
	return w_loop;
}

/* ---- deferred-free sweep -------------------------------------------- */

static void sweepDeleted(void)
{
	TimerHandler **tpp = &timerHandler;
	IdleHandler **ipp = &idleHandler;
	InputHandler **fpp = &inputHandler;

	while (*tpp) {
		TimerHandler *h = *tpp;
		if (h->deleted && !h->firing) {
			*tpp = h->next;
			wfree(h);
		} else {
			tpp = &h->next;
		}
	}
	while (*ipp) {
		IdleHandler *h = *ipp;
		if (h->deleted && !h->firing) {
			*ipp = h->next;
			wfree(h);
		} else {
			ipp = &h->next;
		}
	}
	while (*fpp) {
		InputHandler *h = *fpp;
		if (h->deleted && !h->firing) {
			*fpp = h->next;
			wfree(h);
		} else {
			fpp = &h->next;
		}
	}
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

	if (h->firing || h->deleted)
		/* an outer trampoline already owns this handler (a persistent
		 * timer whose callback ran past its own repeat interval and
		 * re-entered ev_run()), do not recurse */
		return;

	h->firing = 1;
	if (h->callback)
		(*h->callback)(h->clientData);
	h->firing = 0;

	if (h->deleted || !h->persistent) {
		/* one-shot timers self-destruct after firing, callers must not
		 * touch the WMHandlerID after the callback ran, but another
		 * callback in the same ev_run() round may still hold it, so
		 * defer the free to the post-dispatch sweep */
		ev_timer_stop(loop, &h->watcher);
		h->deleted = 1;
	}
	/* persistent and survived: libev already rearmed the repeating
	 * watcher before invoking us, nothing to do */
}

static WMHandlerID addTimer(int milliseconds, WMCallback *callback, void *cdata, bool persistent)
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
	/* timers below the default priority so a due timer does not gate
	 * ev_idle dispatch, the old loop ran idle handlers whenever no
	 * input was pending regardless of timers, input and idle stay at
	 * the default so pending input still suppresses idle */
	ev_set_priority(&h->watcher, EV_MINPRI);
	ev_timer_start(loop, &h->watcher);

	h->next = timerHandler;
	timerHandler = h;

	return h;
}

WMHandlerID WMAddTimerHandler(int milliseconds, WMCallback *callback, void *cdata)
{
	return addTimer(milliseconds, callback, cdata, false);
}

WMHandlerID WMAddPersistentTimerHandler(int milliseconds, WMCallback *callback, void *cdata)
{
	return addTimer(milliseconds, callback, cdata, true);
}

void WMDeleteTimerHandler(WMHandlerID handlerID)
{
	TimerHandler *h = (TimerHandler *)handlerID;

	if (!h || h->deleted)
		return;

	ev_timer_stop(W_EventLoop(), &h->watcher);
	h->persistent = 0;

	if (h->firing || dispatching) {
		/* called from inside its own callback, or from another
		 * callback while ev_run() is on the stack, the post-dispatch
		 * sweep owns the final unlink and free */
		h->deleted = 1;
		return;
	}

	unlinkTimer(h);
	wfree(h);
}

void WMDeleteTimerHandlerPtr(WMHandlerID *handlerID)
{
	if (!handlerID)
		return;
	WMDeleteTimerHandler(*handlerID);
	*handlerID = NULL;
}

void WMDeleteTimerWithClientData(void *cdata)
{
	TimerHandler *h;

	if (!cdata)
		return;

	for (h = timerHandler; h; h = h->next) {
		if (!h->deleted && h->clientData == cdata) {
			/* first live match only, same as the original
			 * implementation */
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

	if (h->firing || h->deleted)
		return;

	/* idle handlers are one-shot, ev_idle is level-triggered so stop
	 * before the callback or a re-entrant ev_run() would re-dispatch
	 * this watcher unboundedly */
	ev_idle_stop(loop, &h->watcher);

	h->firing = 1;
	if (h->callback)
		(*h->callback)(h->clientData);
	h->firing = 0;

	/* swept after ev_run() returns */
	h->deleted = 1;
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

	return h;
}

void WMDeleteIdleHandler(WMHandlerID handlerID)
{
	IdleHandler *h = (IdleHandler *)handlerID;

	if (!h || h->deleted)
		return;

	ev_idle_stop(W_EventLoop(), &h->watcher);

	if (h->firing || dispatching) {
		h->deleted = 1;
		return;
	}

	unlinkIdle(h);
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

	if (h->firing || h->deleted)
		return;

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

	input_fired = true;

	if (mask && h->callback) {
		h->firing = 1;
		(*h->callback)(w->fd, mask, h->clientData);
		h->firing = 0;
	}
	/* h->deleted set by a self- or cross-delete is swept after ev_run() */
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

	return h;
}

void WMDeleteInputHandler(WMHandlerID handlerID)
{
	InputHandler *h = (InputHandler *)handlerID;

	if (!h || h->deleted)
		return;

	ev_io_stop(W_EventLoop(), &h->watcher);

	if (h->firing || dispatching) {
		h->deleted = 1;
		return;
	}

	unlinkInput(h);
	wfree(h);
}

void WMSetPrepareHook(WMPrepareProc *proc, void *clientData)
{
	w_prepare_hook = proc;
	w_prepare_hook_data = clientData;
}

/* ---- loop driver ----------------------------------------------------- */

static void xfdTrampoline(struct ev_loop *loop, ev_io *w, int revents)
{
	(void)w;
	(void)revents;

	input_fired = true;
	/* hand control back to WMNextEvent() so it drains the xcb event queue */
	ev_break(loop, EVBREAK_ONE);
}

bool W_HandleInputEvents(bool waitForInput, int inputfd)
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

	/* ev_run() handles every case directly: with no referenced
	 * watchers it returns at once, with only timers it blocks until the
	 * next one is due, with idles it runs them and returns, with input
	 * or the X fd it polls, the prepare/check watchers flush the ASAP
	 * and WhenIdle notification queues either side of the poll */
	input_fired = false;

	dispatching++;
	ev_run(loop, waitForInput ? EVRUN_ONCE : EVRUN_NOWAIT);

	/* a timer callback or a WMPostWhenIdle observer flushed from the
	 * ev_check watcher may have queued new idle work after the idle
	 * phase of this iteration, the old WHandleEvents() ladder drained
	 * that by looping W_CheckIdleHandlers() while no input was pending,
	 * preserve that contract here, input_fired gates it so X data or a
	 * ready WINGs input handler still returns control immediately */
	while (!input_fired) {
		IdleHandler *ih;
		for (ih = idleHandler; ih && ih->deleted; ih = ih->next)
			;
		if (!ih)
			break;
		ev_run(loop, EVRUN_NOWAIT);
	}
	dispatching--;

	if (!dispatching)
		sweepDeleted();

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
	while (inputHandler) {
		InputHandler *h = inputHandler;
		ev_io_stop(w_loop, &h->watcher);
		inputHandler = h->next;
		wfree(h);
	}

	if (w_xfd >= 0) {
		ev_io_stop(w_loop, &w_xfd_watcher);
		w_xfd = -1;
	}

	ev_ref(w_loop);
	ev_prepare_stop(w_loop, &w_prepare);
	ev_ref(w_loop);
	ev_check_stop(w_loop, &w_check);

	ev_loop_destroy(w_loop);
	w_loop = NULL;
}
