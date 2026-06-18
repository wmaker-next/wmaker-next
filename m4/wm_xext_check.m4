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
# Gate the X Shaped Window extension feature in src/ on the user's
# --disable-shape, no library probe: the only consumer is src/ via
# xcb-shape which is a hard PKG_CHECK_MODULES([XCB], ...) dependency,
# so there is no library to detect.
AC_DEFUN_ONCE([WM_XEXT_CHECK_XSHAPE],
[AS_IF([test "x$enable_shape" = "xno"],
    [unsupported="$unsupported XShape"],
    [AC_DEFINE([USE_XSHAPE], [1],
        [defined when shaped frame and wireframe support is built])
     supported_xext="$supported_xext XShape"]) dnl
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
# Check for the X RandR (Resize-and-Rotate) extension.  src/randr.c
# uses xcb-randr which is a hard PKG_CHECK_MODULES([XCB], ...) dep so
# the wmaker binary needs no probe, but util/wmlock and util/wmiv keep
# their own Xlib RandR (own process, own queue) and link @LIBXRANDR@,
# so the libXrandr 1.5 link/header probe stays for them and USE_RANDR
# stays gated on it so the util/ #ifdef and the link line agree.
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
# Gate _NET_WM_SYNC_REQUEST frame-synchronised resize on the user's
# --disable-xsync, no library probe: the only consumer is src/wmspec.c
# via xcb-sync which is a hard PKG_CHECK_MODULES([XCB], ...) dependency.
AC_DEFUN_ONCE([WM_XEXT_CHECK_XSYNC],
[AS_IF([test "x$enable_xsync" = "xno"],
    [unsupported="$unsupported XSync"],
    [AC_DEFINE([USE_XSYNC], [1],
        [defined when _NET_WM_SYNC_REQUEST resize pacing is built])
     supported_xext="$supported_xext XSync"]) dnl
]) dnl AC_DEFUN


