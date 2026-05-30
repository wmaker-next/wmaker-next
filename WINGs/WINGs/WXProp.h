/*
 *  WXProp - typed X11 property and ClientMessage helpers
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  All format-32 property data crosses this interface as fixed-width
 *  integers (uint32_t / int32_t), matching the X wire protocol and xcb's
 *  native types.  Callers never see the Xlib long[] representation and
 *  never call XFree() on returned buffers, arrays are wmalloc()'d copies
 *  to be released with wfree().
 *
 *  The Xlib implementation lives in WINGs/wxprop.c.  That file is the
 *  single point to rewrite for an xcb backend.
 */

#ifndef WINGS_WXPROP_H
#define WINGS_WXPROP_H

#include <X11/Xlib.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Split-phase request/reply ---------------------------------------
 *
 * WXReq* queues a GetProperty request without blocking and returns an
 * opaque cookie.  WXRcv* blocks for that cookie's reply, validates
 * format/count, and decodes into the caller's output.  Issuing N WXReq*
 * calls followed by one WXFlush() and N WXRcv* calls in the same order
 * collapses N round-trips into one.
 *
 * The synchronous WXGet* readers below are implemented as
 * WXRcv*(dpy, WXReq*(...), ...) and remain the convenient choice for
 * single-property reads.
 *
 * Every issued cookie must eventually be passed to a WXRcv* or to
 * WXDiscard(), otherwise the reply stays cached for the lifetime of the
 * connection.
 */

typedef struct { unsigned int seq; } WXCookie;

WXCookie WXReqFormat32(Display *dpy, Window w, Atom prop, Atom type, size_t max);
WXCookie WXReqString8 (Display *dpy, Window w, Atom prop, Atom type);
WXCookie WXReqExists  (Display *dpy, Window w, Atom prop);

void WXFlush  (Display *dpy);
void WXDiscard(Display *dpy, WXCookie ck);

Bool      WXRcvCardinal      (Display *dpy, WXCookie ck, uint32_t *out);
Bool      WXRcvCardinalSigned(Display *dpy, WXCookie ck, int32_t  *out);
Bool      WXRcvCardinalN     (Display *dpy, WXCookie ck, uint32_t *out, size_t n);
uint32_t *WXRcvCardinalList  (Display *dpy, WXCookie ck, size_t *count);
Bool      WXRcvAtom          (Display *dpy, WXCookie ck, Atom *out);
Atom     *WXRcvAtomList      (Display *dpy, WXCookie ck, size_t *count);
Bool      WXRcvWindow        (Display *dpy, WXCookie ck, Window *out);
Window   *WXRcvWindowList    (Display *dpy, WXCookie ck, size_t *count);
Bool      WXRcvFormat32N     (Display *dpy, WXCookie ck, uint32_t *out, size_t n);
uint32_t *WXRcvFormat32List  (Display *dpy, WXCookie ck, size_t *count);
char     *WXRcvString        (Display *dpy, WXCookie ck, size_t *len);
Bool      WXRcvExists        (Display *dpy, WXCookie ck);

/* --- Window attributes / geometry (split-phase) -----------------------
 *
 * XGetWindowAttributes() is one Xlib call but two protocol requests
 * (GetWindowAttributes + GetGeometry, since border_width/depth/x/y/width/
 * height come from the latter). Expose both so they can be batched.
 */

typedef struct {
    int      override_redirect;
    int      map_state;        /* IsUnmapped/IsUnviewable/IsViewable */
    int      window_class;     /* InputOutput/InputOnly */
    uint32_t visual;           /* VisualID, resolve with XGetVisualInfo() */
    uint32_t colormap;
    uint32_t all_event_masks;
    uint32_t your_event_mask;
} WXWindowAttributes;

typedef struct {
    Window   root;
    int16_t  x, y;
    uint16_t width, height;
    uint16_t border_width;
    uint8_t  depth;
} WXGeometry;

WXCookie WXReqWindowAttributes(Display *dpy, Window w);
WXCookie WXReqGeometry        (Display *dpy, Window w);
Bool     WXRcvWindowAttributes(Display *dpy, WXCookie ck, WXWindowAttributes *out);
Bool     WXRcvGeometry        (Display *dpy, WXCookie ck, WXGeometry *out);

/* --- Readers (synchronous) -------------------------------------------- */

Bool      WXGetCardinal      (Display *dpy, Window w, Atom prop, uint32_t *out);
Bool      WXGetCardinalSigned(Display *dpy, Window w, Atom prop, int32_t  *out);
Bool      WXGetCardinalN     (Display *dpy, Window w, Atom prop,
                              uint32_t *out, size_t n);
uint32_t *WXGetCardinalList  (Display *dpy, Window w, Atom prop,
                              size_t max, size_t *count);

Bool      WXGetAtom          (Display *dpy, Window w, Atom prop, Atom *out);
Atom     *WXGetAtomList      (Display *dpy, Window w, Atom prop,
                              size_t max, size_t *count);

Bool      WXGetWindow        (Display *dpy, Window w, Atom prop, Window *out);
Window   *WXGetWindowList    (Display *dpy, Window w, Atom prop,
                              size_t max, size_t *count);

char     *WXGetUTF8String    (Display *dpy, Window w, Atom prop, size_t *len);
char     *WXGetLatin1String  (Display *dpy, Window w, Atom prop, size_t *len);

Bool      WXPropExists       (Display *dpy, Window w, Atom prop);

/* Generic format-32 reader for properties whose type is not one of the
 * predefined XA_* atoms (e.g. WM_STATE, _GNUSTEP_WM_ATTR). */
Bool      WXGetFormat32N     (Display *dpy, Window w, Atom prop, Atom type,
                              uint32_t *out, size_t n);

/* --- Writers ---------------------------------------------------------- */

void WXSetCardinal    (Display *dpy, Window w, Atom prop, uint32_t  v);
void WXSetCardinalN   (Display *dpy, Window w, Atom prop,
                       const uint32_t *v, size_t n);
void WXSetAtom        (Display *dpy, Window w, Atom prop, Atom v);
void WXSetAtomN       (Display *dpy, Window w, Atom prop,
                       const Atom *v, size_t n);
void WXSetWindow      (Display *dpy, Window w, Atom prop, Window v);
void WXSetWindowN     (Display *dpy, Window w, Atom prop,
                       const Window *v, size_t n);
void WXSetUTF8String  (Display *dpy, Window w, Atom prop,
                       const char *s, size_t len);
void WXSetLatin1String(Display *dpy, Window w, Atom prop,
                       const char *s, size_t len);

/* Generic writers for properties whose type is not one of the predefined
 * XA_* atoms. */
void WXSetFormat32N   (Display *dpy, Window w, Atom prop, Atom type,
                       const uint32_t *v, size_t n);
void WXSetFormat8     (Display *dpy, Window w, Atom prop, Atom type,
                       const unsigned char *data, size_t len);

void WXDeleteProperty (Display *dpy, Window w, Atom prop);

/* --- ClientMessage ---------------------------------------------------- */

void WXSendClientMessage(Display *dpy, Window dest, long event_mask,
                         Window about, Atom message_type,
                         long l0, long l1, long l2, long l3, long l4);

#ifdef __cplusplus
}
#endif

#endif /* WINGS_WXPROP_H */
