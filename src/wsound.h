#ifndef WSOUND_H_
#define WSOUND_H_

#define WSOUND_SHADE       "shade"
#define WSOUND_UNSHADE     "unshade"
#define WSOUND_MAXIMIZE    "maximize"
#define WSOUND_UNMAXIMIZE  "unmaximize"
#define WSOUND_ICONIFY     "iconify"
#define WSOUND_DEICONIFY   "deiconify"
#define WSOUND_HIDE        "hide"
#define WSOUND_UNHIDE      "unhide"
#define WSOUND_APPSTART    "appstart"
#define WSOUND_APPEXIT     "appexit"
#define WSOUND_DOCK        "dock"
#define WSOUND_UNDOCK      "undock"
#define WSOUND_KABOOM      "kaboom"
#define WSOUND_STARTUP     "startup"
#define WSOUND_SHUTDOWN    "shutdown"
#define WSOUND_WORKSPACE   "workspace"
#define WSOUND_SCREENSHOT  "screenshot"
#define WSOUND_LOCK        "lock"

void wSoundPlay(WScreen *scr, const char *event);

#endif /* WSOUND_H_ */
