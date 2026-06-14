# wm_xext_check.m4 - Macros to check for X extensions support libraries
#
# Copyright (c) 2013 Christophe CURIS
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, see <https://www.gnu.org/licenses/>.

# WM_XEXT_CHECK_XRES
# --------------------
#
# Check for the X Resource Window extension
# The check depends on variable 'enable_xshape' being either:
#   yes  - detect, fail if not found
#   no   - do not detect, disable support
#   auto - detect, disable if not found
#
# When found, append appropriate stuff in XLIBS, and append info to
# the variable 'supported_xext'
# When not found, append info to variable 'unsupported'
AC_DEFUN_ONCE([WM_XEXT_CHECK_XRES],
[WM_LIB_CHECK([XRes], [-lXRes], [XResQueryClientIds], [$XLIBS],
    [wm_save_CFLAGS="$CFLAGS"
     AC_COMPILE_IFELSE([AC_LANG_PROGRAM([dnl
@%:@include <X11/extensions/XRes.h>
], [dnl

  XResQueryClientIds(NULL, 0, NULL, NULL, NULL);])]
        [],
        [AC_MSG_ERROR([found $CACHEVAR but cannot compile using XRes header])])
     CFLAGS="$wm_save_CFLAGS"],
    [supported_xext], [XLIBS], [enable_res], [-])dnl
]) dnl AC_DEFUN


# WM_XEXT_CHECK_XSHAPE
# --------------------
#
# Check for the X Shaped Window extension
# The check depends on variable 'enable_xshape' being either:
#   yes  - detect, fail if not found
#   no   - do not detect, disable support
#   auto - detect, disable if not found
#
# When found, append appropriate stuff in XLIBS, and append info to
# the variable 'supported_xext'
# When not found, append info to variable 'unsupported'
AC_DEFUN_ONCE([WM_XEXT_CHECK_XSHAPE],
[WM_LIB_CHECK([XShape], [-lXext], [XShapeSelectInput], [$XLIBS],
    [wm_save_CFLAGS="$CFLAGS"
     AS_IF([wm_fn_lib_try_compile "X11/extensions/shape.h" "Window win;" "XShapeSelectInput(NULL, win, 0)" ""],
        [],
        [AC_MSG_ERROR([found $CACHEVAR but cannot compile using XShape header])])
     CFLAGS="$wm_save_CFLAGS"],
    [supported_xext], [XLIBS], [enable_shape], [-])dnl
]) dnl AC_DEFUN


# WM_XEXT_CHECK_XSHM
# ------------------
#
# Check for the MIT-SHM extension for Shared Memory support
# The check depends on variable 'enable_shm' being either:
#   yes  - detect, fail if not found
#   no   - do not detect, disable support
#   auto - detect, disable if not found
#
# When found, append appropriate stuff in XLIBS, and append info to
# the variable 'supported_xext'
# When not found, append info to variable 'unsupported'
AC_DEFUN_ONCE([WM_XEXT_CHECK_XSHM],
[WM_LIB_CHECK([XShm], [-lXext], [XShmAttach], [$XLIBS],
    [wm_save_CFLAGS="$CFLAGS"
     AC_COMPILE_IFELSE([AC_LANG_PROGRAM([dnl
@%:@include <X11/Xlib.h>
@%:@include <X11/extensions/XShm.h>
], [dnl
  XShmSegmentInfo si;

  XShmAttach(NULL, &si);])],
        [],
        [AC_MSG_ERROR([found $CACHEVAR but cannot compile using XShm header])])
     AC_COMPILE_IFELSE([AC_LANG_PROGRAM([dnl
@%:@include <sys/ipc.h>
@%:@include <sys/shm.h>
], [dnl
  shmget(IPC_PRIVATE, 1024, IPC_CREAT);])],
        [],
        [AC_MSG_ERROR([found $CACHEVAR but cannot compile using ipc/shm headers])])
     CFLAGS="$wm_save_CFLAGS"],
    [supported_xext], [XLIBS], [enable_shm], [-])dnl
]) dnl AC_DEFUN


# WM_XEXT_CHECK_XRANDR
# --------------------
#
# Check for the X RandR (Resize-and-Rotate) extension
# The check depends on variable 'enable_randr' being either:
#   yes  - detect, fail if not found
#   no   - do not detect, disable support
#   auto - detect, disable if not found
#
# When found, append appropriate stuff in LIBXRANDR, and append info to
# the variable 'supported_xext'
# When not found, append info to variable 'unsupported'
AC_DEFUN_ONCE([WM_XEXT_CHECK_XRANDR],
[WM_LIB_CHECK([RandR], [-lXrandr], [XRRGetMonitors], [$XLIBS],
    [wm_save_CFLAGS="$CFLAGS"
     AS_IF([wm_fn_lib_try_compile "X11/extensions/Xrandr.h" "Display *dpy; int n;" "XRRGetMonitors(dpy, 0, 1, &n)" ""],
        [],
        [AC_MSG_ERROR([found $CACHEVAR but cannot compile using XRandR 1.5 header])])
     CFLAGS="$wm_save_CFLAGS"],
    [supported_xext], [LIBXRANDR], [], [-])dnl
AC_SUBST([LIBXRANDR])dnl
]) dnl AC_DEFUN


# WM_XEXT_CHECK_XSYNC
# -------------------
#
# Check for the X Synchronization extension, used by _NET_WM_SYNC_REQUEST
# to pace interactive resizes to the client's redraw.
# The check depends on variable 'enable_xsync' being either:
#   yes  - detect, fail if not found
#   no   - do not detect, disable support
#   auto - detect, disable if not found
#
# When found, append -lXext to XLIBS and define USE_XSYNC.
AC_DEFUN_ONCE([WM_XEXT_CHECK_XSYNC],
[WM_LIB_CHECK([XSync], [-lXext], [XSyncInitialize], [$XLIBS],
    [wm_save_CFLAGS="$CFLAGS"
     AC_COMPILE_IFELSE([AC_LANG_PROGRAM([dnl
@%:@include <X11/Xlib.h>
@%:@include <X11/extensions/sync.h>
], [dnl
  Display *dpy = (Display *)0;
  int a, b;
  XSyncInitialize(dpy, &a, &b);])],
        [],
        [AC_MSG_ERROR([found $CACHEVAR but cannot compile using XSync header])])
     CFLAGS="$wm_save_CFLAGS"],
    [supported_xext], [XLIBS], [enable_xsync], [-])dnl
]) dnl AC_DEFUN


# WM_XEXT_CHECK_XKBFILE
# ---------------------
#
# Check for the XKB File extension library (libxkbfile)
# The check depends on variable 'enable_modelock' being either:
#   yes  - detect, fail if not found
#   no   - do not detect, disable support
#
# When found, append appropriate stuff in LIBXKBFILE, and append info to
# the variable 'supported_xext'
# When not found, generate an error because it's required for modelock
AC_DEFUN_ONCE([WM_XEXT_CHECK_XKBFILE],
[WM_LIB_CHECK([XKBFile], [-lxkbfile], [XkbRF_GetNamesProp], [$XLIBS],
    [wm_save_CFLAGS="$CFLAGS"
     AC_COMPILE_IFELSE([AC_LANG_PROGRAM([dnl
@%:@include <stdio.h>
@%:@include <X11/Xlib.h>
@%:@include <X11/XKBlib.h>
@%:@include <X11/extensions/XKBfile.h>
@%:@include <X11/extensions/XKBrules.h>
], [dnl
  Display *dpy = NULL;
  XkbRF_VarDefsRec vd;
  XkbRF_GetNamesProp(dpy, NULL, &vd);])],
        [],
        [AC_MSG_ERROR([found $CACHEVAR but cannot compile using XKBfile header])])
     CFLAGS="$wm_save_CFLAGS"],
    [supported_xext], [LIBXKBFILE], [enable_modelock], [-])dnl
AC_SUBST([LIBXKBFILE])dnl
]) dnl AC_DEFUN
