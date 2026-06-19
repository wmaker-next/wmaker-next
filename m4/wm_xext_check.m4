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
# Check for the X Resource Window extension (via xcb-res)
# The check depends on variable 'enable_res' being either:
#   yes  - detect, fail if not found
#   no   - do not detect, disable support
#   auto - detect, disable if not found
#
# When found, USE_XRES is defined, xcb-res is appended to XLIBS, and
# info is appended to 'supported_xext'.
# When not found, info is appended to 'unsupported'.
AC_DEFUN_ONCE([WM_XEXT_CHECK_XRES],
[AS_IF([test "x$enable_res" = "xno"],
    [unsupported="$unsupported XRes"],
    [PKG_CHECK_MODULES([XRES], [xcb-res],
        [AC_DEFINE([USE_XRES], [1],
                   [defined when xcb-res is available for server-side PID query])
         supported_xext="$supported_xext XRes"
         XLIBS="$XLIBS $XRES_LIBS"
         AC_SUBST([XRES_CFLAGS])],
        [AS_IF([test "x$enable_res" = "xyes"],
            [AC_MSG_ERROR([xcb-res is required (use --disable-res to skip): $XRES_PKG_ERRORS])],
            [unsupported="$unsupported XRes"
             XRES_CFLAGS=
             AC_SUBST([XRES_CFLAGS])])])
    ])
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
# Gate the X RandR live-reconfigure feature in src/ and the WPrefs
# HotplugMonitor checkbox on the user's --disable-randr, no library
# probe: every in-tree RandR consumer goes through xcb-randr which is a
# hard PKG_CHECK_MODULES([XCB], ...) dependency, so there is no library
# to detect.
AC_DEFUN_ONCE([WM_XEXT_CHECK_XRANDR],
[AS_IF([test "x$enable_randr" = "xno"],
    [unsupported="$unsupported RandR"],
    [AC_DEFINE([USE_RANDR], [1],
        [defined when RandR multi-monitor live reconfiguration is built])
     supported_xext="$supported_xext RandR"]) dnl
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


