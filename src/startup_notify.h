/*  startup_notify.h - freedesktop startup-notification launcher and monitor
 *
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

#ifndef WMSTARTUP_NOTIFY_H_
#define WMSTARTUP_NOTIFY_H_

#include <X11/Xlib.h>
#include <sys/types.h>

struct _WScreen;
struct WAppIcon;

/* Opaque per-screen monitor state hung off WScreen, body in startup_notify.c */
struct WStartupNotify;

/* Public handle for an in-flight launch, alias of the internal pending entry
 * so callers never need a second incompatible struct tag. */
typedef struct WPendingLaunch WPendingLaunch;
typedef WPendingLaunch WStartupLaunch;

#ifdef HAVE_STARTUP_NOTIFICATION

void wStartupNotifyInit(struct _WScreen *scr);
void wStartupNotifyShutdown(struct _WScreen *scr);
Bool wStartupNotifyProcessEvent(XEvent *ev);
Bool wStartupNotifyAppStarting(struct _WScreen *scr);
void wStartupNotifyUpdateCursor(struct _WScreen *scr);

Bool wStartupNotifyMatchWindow(struct _WScreen *scr, const char *startup_id,
			       const char *wm_class, const char *wm_instance,
			       Time *launch_time_out, int *workspace_out);

WStartupLaunch *wStartupLaunchBegin(struct _WScreen *scr, const char *bin,
				    const char *name, const char *wmclass,
				    int workspace, struct WAppIcon *btn);
void wStartupLaunchSetupChild(WStartupLaunch *l);
void wStartupLaunchSpawned(WStartupLaunch *l, pid_t pid);
void wStartupLaunchAbort(WStartupLaunch *l);
void wStartupLaunchChildExited(struct _WScreen *scr, pid_t pid);

void wStartupNotifyForgetIcon(struct WAppIcon *btn);

#else  /* !HAVE_STARTUP_NOTIFICATION */

static inline void wStartupNotifyInit(struct _WScreen *scr) { (void)scr; }
static inline void wStartupNotifyShutdown(struct _WScreen *scr) { (void)scr; }
static inline Bool wStartupNotifyProcessEvent(XEvent *ev) { (void)ev; return False; }
static inline Bool wStartupNotifyAppStarting(struct _WScreen *scr) { (void)scr; return False; }
static inline void wStartupNotifyUpdateCursor(struct _WScreen *scr) { (void)scr; }

static inline Bool wStartupNotifyMatchWindow(struct _WScreen *scr, const char *startup_id,
					     const char *wm_class, const char *wm_instance,
					     Time *launch_time_out, int *workspace_out)
{
	(void)scr; (void)startup_id; (void)wm_class; (void)wm_instance;
	(void)launch_time_out; (void)workspace_out;
	return False;
}

static inline WStartupLaunch *wStartupLaunchBegin(struct _WScreen *scr, const char *bin,
						  const char *name, const char *wmclass,
						  int workspace, struct WAppIcon *btn)
{
	(void)scr; (void)bin; (void)name; (void)wmclass; (void)workspace; (void)btn;
	return NULL;
}
static inline void wStartupLaunchSetupChild(WStartupLaunch *l) { (void)l; }
static inline void wStartupLaunchSpawned(WStartupLaunch *l, pid_t pid) { (void)l; (void)pid; }
static inline void wStartupLaunchAbort(WStartupLaunch *l) { (void)l; }
static inline void wStartupLaunchChildExited(struct _WScreen *scr, pid_t pid) { (void)scr; (void)pid; }

static inline void wStartupNotifyForgetIcon(struct WAppIcon *btn) { (void)btn; }

#endif  /* HAVE_STARTUP_NOTIFICATION */

#endif  /* WMSTARTUP_NOTIFY_H_ */
