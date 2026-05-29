/*
 *  WXHints - typed ICCCM convenience helpers
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Wraps the Xlib ICCCM convenience layer (XGetWMNormalHints, XGetWMHints,
 *  XGetClassHint, XGetWMProtocols, XGetTransientForHint, text properties)
 *  so that callers never allocate or free Xlib memory and never see
 *  XTextProperty/XClassHint.  Together with WXProp.h this is the second
 *  backend seam for an xcb migration: the Xlib implementation lives in
 *  WINGs/wxhints.c and rewrites onto xcb-util-icccm.
 *
 *  WXSizeHints / WXWMHints alias the Xlib structs today.  xcb-icccm's
 *  xcb_size_hints_t / xcb_icccm_wm_hints_t use the same field names, so
 *  the typedef is the only line that changes under xcb.
 *
 *  All pointers returned by getters are wmalloc()'d, release with wfree()
 *  (or WXFreeStringList for string arrays).
 */

#ifndef WINGS_WXHINTS_H
#define WINGS_WXHINTS_H

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stddef.h>

#include <WINGs/WXProp.h>   /* WXCookie */

#ifdef __cplusplus
extern "C" {
#endif

typedef XSizeHints WXSizeHints;
typedef XWMHints   WXWMHints;

/* --- Split-phase request/reply -------------------------------------- */

WXCookie WXReqNormalHints     (Display *dpy, Window w);
WXCookie WXReqWMHints         (Display *dpy, Window w);
WXCookie WXReqWMClass         (Display *dpy, Window w);
WXCookie WXReqWMProtocols     (Display *dpy, Window w);
WXCookie WXReqTransientFor    (Display *dpy, Window w);
WXCookie WXReqWMName          (Display *dpy, Window w);
WXCookie WXReqWMIconName      (Display *dpy, Window w);
WXCookie WXReqCommand         (Display *dpy, Window w);
WXCookie WXReqTextPropertyList(Display *dpy, Window w, Atom prop);

Bool       WXRcvNormalHints     (Display *dpy, WXCookie ck, WXSizeHints *out, long *supplied);
WXWMHints *WXRcvWMHints         (Display *dpy, WXCookie ck);
Bool       WXRcvWMClass         (Display *dpy, WXCookie ck, char **instance, char **wclass);
Atom      *WXRcvWMProtocols     (Display *dpy, WXCookie ck, size_t *count);
Bool       WXRcvTransientFor    (Display *dpy, WXCookie ck, Window *out);
char      *WXRcvWMName          (Display *dpy, WXCookie ck);
char      *WXRcvWMIconName      (Display *dpy, WXCookie ck);
Bool       WXRcvCommand         (Display *dpy, WXCookie ck, char ***argv, int *argc);
char     **WXRcvTextPropertyList(Display *dpy, WXCookie ck, int *count);

/* --- WM_NORMAL_HINTS / WM_HINTS ------------------------------------- */

WXSizeHints *WXAllocSizeHints(void);
WXWMHints   *WXAllocWMHints(void);

Bool WXGetNormalHints(Display *dpy, Window w, WXSizeHints *out, long *supplied);
void WXSetNormalHints(Display *dpy, Window w, const WXSizeHints *h);

WXWMHints *WXGetWMHints(Display *dpy, Window w);
void       WXSetWMHints(Display *dpy, Window w, const WXWMHints *h);

/* --- WM_CLASS -------------------------------------------------------- */

Bool WXGetWMClass(Display *dpy, Window w, char **instance, char **wclass);
void WXSetWMClass(Display *dpy, Window w, const char *instance, const char *wclass);

/* --- WM_PROTOCOLS ---------------------------------------------------- */

Atom *WXGetWMProtocols(Display *dpy, Window w, size_t *count);
void  WXSetWMProtocols(Display *dpy, Window w, const Atom *atoms, size_t count);

/* --- WM_TRANSIENT_FOR ------------------------------------------------ */

Bool WXGetTransientFor(Display *dpy, Window w, Window *out);
void WXSetTransientFor(Display *dpy, Window w, Window owner);

/* --- WM_NAME / WM_ICON_NAME (locale-decoded to UTF-8/locale string) -- */

char *WXGetWMName    (Display *dpy, Window w);
void  WXSetWMName    (Display *dpy, Window w, const char *name);
char *WXGetWMIconName(Display *dpy, Window w);
void  WXSetWMIconName(Display *dpy, Window w, const char *name);

/* --- WM_COMMAND ------------------------------------------------------ */

Bool WXGetCommand(Display *dpy, Window w, char ***argv, int *argc);
void WXSetCommand(Display *dpy, Window w, char **argv, int argc);

/* --- Text properties as string lists --------------------------------- */

char **WXGetTextPropertyList(Display *dpy, Window w, Atom prop, int *count);
void   WXSetTextPropertyList(Display *dpy, Window w, Atom prop,
                             char **list, int count);
void   WXFreeStringList(char **list, int count);

#ifdef __cplusplus
}
#endif

#endif /* WINGS_WXHINTS_H */
