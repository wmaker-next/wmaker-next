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
 *  Typed ICCCM convenience helpers over xcb-util-icccm. Callers work
 *  with wmalloc()'d strings and structs (release with wfree()) and
 *  never see the raw request/reply plumbing, text properties are
 *  decoded behind the interface.
 *
 *  WXSizeHints / WXWMHints alias the xcb-icccm structs. The Xlib spellings
 *  used the same field names, so callers did not change under the swap.
 *
 *  All pointers returned by getters are wmalloc()'d, release with wfree()
 *  (or WXFreeStringList for string arrays).
 */

#ifndef WINGS_WXHINTS_H
#define WINGS_WXHINTS_H

#include <stdbool.h>
#include <xcb/xcb_icccm.h>
#include <stddef.h>

#include <WINGs/WXProp.h>   /* WXCookie */

#ifdef __cplusplus
extern "C" {
#endif

typedef xcb_size_hints_t     WXSizeHints;
typedef xcb_icccm_wm_hints_t WXWMHints;

/* --- Split-phase request/reply -------------------------------------- */

WXCookie WXReqNormalHints     (xcb_connection_t *dpy, xcb_window_t w);
WXCookie WXReqWMHints         (xcb_connection_t *dpy, xcb_window_t w);
WXCookie WXReqWMClass         (xcb_connection_t *dpy, xcb_window_t w);
WXCookie WXReqWMProtocols     (xcb_connection_t *dpy, xcb_window_t w);
WXCookie WXReqTransientFor    (xcb_connection_t *dpy, xcb_window_t w);
WXCookie WXReqWMName          (xcb_connection_t *dpy, xcb_window_t w);
WXCookie WXReqWMIconName      (xcb_connection_t *dpy, xcb_window_t w);
WXCookie WXReqCommand         (xcb_connection_t *dpy, xcb_window_t w);
WXCookie WXReqTextPropertyList(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop);

bool       WXRcvNormalHints     (xcb_connection_t *dpy, WXCookie ck, WXSizeHints *out, uint32_t *supplied);
WXWMHints *WXRcvWMHints         (xcb_connection_t *dpy, WXCookie ck);
bool       WXRcvWMClass         (xcb_connection_t *dpy, WXCookie ck, char **instance, char **wclass);
xcb_atom_t      *WXRcvWMProtocols     (xcb_connection_t *dpy, WXCookie ck, size_t *count);
bool       WXRcvTransientFor    (xcb_connection_t *dpy, WXCookie ck, xcb_window_t *out);
char      *WXRcvWMName          (xcb_connection_t *dpy, WXCookie ck);
char      *WXRcvWMIconName      (xcb_connection_t *dpy, WXCookie ck);
bool       WXRcvCommand         (xcb_connection_t *dpy, WXCookie ck, char ***argv, int *argc);
char     **WXRcvTextPropertyList(xcb_connection_t *dpy, WXCookie ck, int *count);

/* --- WM_NORMAL_HINTS / WM_HINTS ------------------------------------- */

WXSizeHints *WXAllocSizeHints(void);
WXWMHints   *WXAllocWMHints(void);

bool WXGetNormalHints(xcb_connection_t *dpy, xcb_window_t w, WXSizeHints *out, uint32_t *supplied);
void WXSetNormalHints(xcb_connection_t *dpy, xcb_window_t w, const WXSizeHints *h);

WXWMHints *WXGetWMHints(xcb_connection_t *dpy, xcb_window_t w);
void       WXSetWMHints(xcb_connection_t *dpy, xcb_window_t w, const WXWMHints *h);

/* --- WM_CLASS -------------------------------------------------------- */

bool WXGetWMClass(xcb_connection_t *dpy, xcb_window_t w, char **instance, char **wclass);
void WXSetWMClass(xcb_connection_t *dpy, xcb_window_t w, const char *instance, const char *wclass);

/* --- WM_PROTOCOLS ---------------------------------------------------- */

xcb_atom_t *WXGetWMProtocols(xcb_connection_t *dpy, xcb_window_t w, size_t *count);
void  WXSetWMProtocols(xcb_connection_t *dpy, xcb_window_t w, const xcb_atom_t *atoms, size_t count);

/* --- WM_TRANSIENT_FOR ------------------------------------------------ */

bool WXGetTransientFor(xcb_connection_t *dpy, xcb_window_t w, xcb_window_t *out);
void WXSetTransientFor(xcb_connection_t *dpy, xcb_window_t w, xcb_window_t owner);

/* --- WM_NAME / WM_ICON_NAME (decoded to UTF-8) ----------------------- */

char *WXGetWMName    (xcb_connection_t *dpy, xcb_window_t w);
void  WXSetWMName    (xcb_connection_t *dpy, xcb_window_t w, const char *name);
char *WXGetWMIconName(xcb_connection_t *dpy, xcb_window_t w);
void  WXSetWMIconName(xcb_connection_t *dpy, xcb_window_t w, const char *name);

/* --- WM_COMMAND ------------------------------------------------------ */

bool WXGetCommand(xcb_connection_t *dpy, xcb_window_t w, char ***argv, int *argc);
void WXSetCommand(xcb_connection_t *dpy, xcb_window_t w, char **argv, int argc);

/* --- Text properties as string lists --------------------------------- */

char **WXGetTextPropertyList(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop, int *count);
void   WXSetTextPropertyList(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop,
                             char **list, int count);
void   WXFreeStringList(char **list, int count);

#ifdef __cplusplus
}
#endif

#endif /* WINGS_WXHINTS_H */
