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
# When found, USE_XRES is defined, -lxcb-res lands in XRES_LIBS, and
# info is appended to 'supported_xext'.
# When not found, info is appended to 'unsupported'.
AC_DEFUN_ONCE([WM_XEXT_CHECK_XRES],
[AS_IF([test "x$enable_res" = "xno"],
    [unsupported="$unsupported XRes"],
    [PKG_CHECK_MODULES([XRES], [xcb-res],
        [AC_DEFINE([USE_XRES], [1],
                   [defined when xcb-res is available for server-side PID query])
         supported_xext="$supported_xext XRes"],
        [AS_IF([test "x$enable_res" = "xyes"],
            [AC_MSG_ERROR([xcb-res is required (use --disable-res to skip): $XRES_PKG_ERRORS])],
            [unsupported="$unsupported XRes"])])
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
# Gate the MIT-SHM put-image path in wrlib on the user's --disable-shm,
# no library probe: the only consumer is wrlib via xcb-shm which is a
# hard PKG_CHECK_MODULES([WRASTER_XCB], ...) dependency, so there is no
# library to detect, only the SysV shmget()/shmat() interface.
AC_DEFUN_ONCE([WM_XEXT_CHECK_XSHM],
[AS_IF([test "x$enable_shm" = "xno"],
    [unsupported="$unsupported XShm"],
    [AC_COMPILE_IFELSE([AC_LANG_PROGRAM([dnl
@%:@include <sys/ipc.h>
@%:@include <sys/shm.h>
], [dnl
  shmget(IPC_PRIVATE, 1024, IPC_CREAT);])],
        [AC_DEFINE([USE_XSHM], [1],
            [defined when the MIT-SHM put-image path in wrlib is built])
         supported_xext="$supported_xext XShm"],
        [AS_IF([test "x$enable_shm" = "xyes"],
            [AC_MSG_ERROR([cannot compile using SysV ipc/shm headers])],
            [unsupported="$unsupported XShm"]) ]) ]) dnl
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


