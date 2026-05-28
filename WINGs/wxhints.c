/*
 *  WXHints - typed ICCCM convenience helpers (Xlib backend)
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "wconfig.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdlib.h>
#include <string.h>

#include <WINGs/WUtil.h>
#include <WINGs/WXProp.h>
#include <WINGs/WXHints.h>

/* --- allocators ------------------------------------------------------ */

WXSizeHints *WXAllocSizeHints(void)
{
	return wmalloc(sizeof(WXSizeHints));
}

WXWMHints *WXAllocWMHints(void)
{
	return wmalloc(sizeof(WXWMHints));
}

/* --- WM_NORMAL_HINTS ------------------------------------------------- */

Bool WXGetNormalHints(Display *dpy, Window w, WXSizeHints *out, long *supplied)
{
	long sup = 0;

	if (!XGetWMNormalHints(dpy, w, out, &sup))
		return False;
	if (supplied)
		*supplied = sup;
	return True;
}

void WXSetNormalHints(Display *dpy, Window w, const WXSizeHints *h)
{
	XSetWMNormalHints(dpy, w, (XSizeHints *)h);
}

/* --- WM_HINTS -------------------------------------------------------- */

WXWMHints *WXGetWMHints(Display *dpy, Window w)
{
	XWMHints *xh;
	WXWMHints *out;

	xh = XGetWMHints(dpy, w);
	if (!xh)
		return NULL;

	out = wmalloc(sizeof(WXWMHints));
	*out = *xh;
	XFree(xh);
	return out;
}

void WXSetWMHints(Display *dpy, Window w, const WXWMHints *h)
{
	XSetWMHints(dpy, w, (XWMHints *)h);
}

/* --- WM_CLASS -------------------------------------------------------- */

Bool WXGetWMClass(Display *dpy, Window w, char **instance, char **wclass)
{
	XClassHint ch;

	memset(&ch, 0, sizeof(ch));
	if (XGetClassHint(dpy, w, &ch) == 0)
		return False;

	if (instance)
		*instance = ch.res_name ? wstrdup(ch.res_name) : NULL;
	if (wclass)
		*wclass = ch.res_class ? wstrdup(ch.res_class) : NULL;

	if (ch.res_name)
		XFree(ch.res_name);
	if (ch.res_class)
		XFree(ch.res_class);
	return True;
}

void WXSetWMClass(Display *dpy, Window w, const char *instance, const char *wclass)
{
	XClassHint ch;

	ch.res_name  = (char *)(instance ? instance : "");
	ch.res_class = (char *)(wclass   ? wclass   : "");
	XSetClassHint(dpy, w, &ch);
}

/* --- WM_PROTOCOLS ---------------------------------------------------- */

Atom *WXGetWMProtocols(Display *dpy, Window w, size_t *count)
{
	Atom *xatoms = NULL;
	Atom *out;
	int n = 0, i;

	if (!XGetWMProtocols(dpy, w, &xatoms, &n) || n <= 0) {
		if (xatoms)
			XFree(xatoms);
		if (count)
			*count = 0;
		return NULL;
	}

	out = wmalloc(sizeof(Atom) * n);
	for (i = 0; i < n; i++)
		out[i] = xatoms[i];
	XFree(xatoms);

	if (count)
		*count = (size_t)n;
	return out;
}

void WXSetWMProtocols(Display *dpy, Window w, const Atom *atoms, size_t count)
{
	XSetWMProtocols(dpy, w, (Atom *)atoms, (int)count);
}

/* --- WM_TRANSIENT_FOR ----------------------------------------------- */

Bool WXGetTransientFor(Display *dpy, Window w, Window *out)
{
	return XGetTransientForHint(dpy, w, out) != 0;
}

void WXSetTransientFor(Display *dpy, Window w, Window owner)
{
	XSetTransientForHint(dpy, w, owner);
}

/* --- text-property helpers ------------------------------------------ */

static char *wx_decode_text_property(Display *dpy, XTextProperty *tp)
{
	char **list = NULL;
	char *ret;
	int n = 0;

	if (!tp->value || tp->nitems == 0)
		return NULL;

	if (tp->encoding == XA_STRING) {
		return wstrndup((char *)tp->value, tp->nitems);
	}

	if (XmbTextPropertyToTextList(dpy, tp, &list, &n) >= Success &&
	    n > 0 && list && list[0]) {
		ret = wstrdup(list[0]);
		XFreeStringList(list);
		return ret;
	}
	if (list)
		XFreeStringList(list);

	return wstrndup((char *)tp->value, tp->nitems);
}

char *WXGetWMName(Display *dpy, Window w)
{
	XTextProperty tp;
	char *ret;

	if (!XGetWMName(dpy, w, &tp) || !tp.value)
		return NULL;
	ret = wx_decode_text_property(dpy, &tp);
	XFree(tp.value);
	return ret;
}

void WXSetWMName(Display *dpy, Window w, const char *name)
{
	XTextProperty tp;

	if (!name)
		name = "";
	if (Xutf8TextListToTextProperty(dpy, (char **)&name, 1,
					XStdICCTextStyle, &tp) < Success)
		return;
	XSetWMName(dpy, w, &tp);
	if (tp.value)
		XFree(tp.value);
}

char *WXGetWMIconName(Display *dpy, Window w)
{
	XTextProperty tp;
	char *ret;

	if (!XGetWMIconName(dpy, w, &tp) || !tp.value)
		return NULL;
	ret = wx_decode_text_property(dpy, &tp);
	XFree(tp.value);
	return ret;
}

void WXSetWMIconName(Display *dpy, Window w, const char *name)
{
	XTextProperty tp;

	if (!name)
		name = "";
	if (Xutf8TextListToTextProperty(dpy, (char **)&name, 1,
					XStdICCTextStyle, &tp) < Success)
		return;
	XSetWMIconName(dpy, w, &tp);
	if (tp.value)
		XFree(tp.value);
}

/* --- WM_COMMAND ------------------------------------------------------ */

Bool WXGetCommand(Display *dpy, Window w, char ***argv, int *argc)
{
	char **xargv = NULL;
	int xargc = 0, i;

	if (!XGetCommand(dpy, w, &xargv, &xargc) || xargc <= 0) {
		if (xargv)
			XFreeStringList(xargv);
		*argv = NULL;
		*argc = 0;
		return False;
	}

	*argv = wmalloc(sizeof(char *) * xargc);
	for (i = 0; i < xargc; i++)
		(*argv)[i] = wstrdup(xargv[i]);
	*argc = xargc;

	XFreeStringList(xargv);
	return True;
}

void WXSetCommand(Display *dpy, Window w, char **argv, int argc)
{
	XSetCommand(dpy, w, argv, argc);
}

/* --- generic text-property string lists ------------------------------ */

char **WXGetTextPropertyList(Display *dpy, Window w, Atom prop, int *count)
{
	XTextProperty tp;
	char **xlist = NULL;
	char **out;
	int n = 0, i;

	*count = 0;
	if (!XGetTextProperty(dpy, w, &tp, prop) || !tp.value)
		return NULL;

	if (XTextPropertyToStringList(&tp, &xlist, &n) == 0 || n <= 0) {
		if (xlist)
			XFreeStringList(xlist);
		XFree(tp.value);
		return NULL;
	}
	XFree(tp.value);

	out = wmalloc(sizeof(char *) * n);
	for (i = 0; i < n; i++)
		out[i] = wstrdup(xlist[i]);
	XFreeStringList(xlist);

	*count = n;
	return out;
}

void WXSetTextPropertyList(Display *dpy, Window w, Atom prop,
			   char **list, int count)
{
	XTextProperty tp;

	if (XStringListToTextProperty(list, count, &tp) == 0)
		return;
	XSetTextProperty(dpy, w, &tp, prop);
	if (tp.value)
		XFree(tp.value);
}

void WXFreeStringList(char **list, int count)
{
	int i;

	if (!list)
		return;
	for (i = 0; i < count; i++)
		wfree(list[i]);
	wfree(list);
}
