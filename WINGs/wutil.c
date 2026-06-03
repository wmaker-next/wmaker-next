
/*
 * Handle events for non-GUI based applications
 */

#include "WINGsP.h"

void WHandleEvents(void)
{
	/* one blocking ev_run() iteration dispatches due timers, idle
	 * handlers, input handlers and the notification-queue flushes in
	 * one pass, with no referenced watchers it returns at once so a
	 * timer-only or empty caller is not a busy spin */
	W_HandleInputEvents(True, -1);
}
