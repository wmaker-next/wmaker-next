# wm_prog_cc_c11.m4 - Macros to see if compiler may support STD C11
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
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.


# WM_PROG_CC_C11
# --------------
#
# Require a compiler that supports the C11 standard with GNU extensions.
# The flag is set explicitly (not left to the compiler default) so the
# dialect is pinned across toolchain versions, and -std=gnu11 rather than
# -std=c11 is used because the tree relies on GNU extensions such as
# __attribute__ and statement expressions. configure aborts if no working
# flag is found, so source code can use C11 features (in particular
# _Static_assert) unconditionally.
AC_DEFUN_ONCE([WM_PROG_CC_C11],
[AC_CACHE_CHECK([for the flag to enable C11 with GNU extensions], [wm_cv_prog_cc_c11],
    [wm_cv_prog_cc_c11=no
     wm_save_CFLAGS="$CFLAGS"
     for wm_arg in "-std=gnu11" "-std=gnu17"
     do
         CFLAGS="$wm_save_CFLAGS $wm_arg"
         AC_COMPILE_IFELSE(
             [AC_LANG_PROGRAM([], [dnl
#if __STDC_VERSION__ < 201112L
fail_because_stdc_version_is_older_than_C11;
#endif
_Static_assert(sizeof(int) >= 2, "C11 _Static_assert must be available");
])],
             [wm_cv_prog_cc_c11="$wm_arg" ; break])
     done
     CFLAGS="$wm_save_CFLAGS"])
AS_IF([test "x$wm_cv_prog_cc_c11" = "xno"],
    [AC_MSG_ERROR([a compiler supporting C11 (-std=gnu11) is required])],
    [CFLAGS="$CFLAGS $wm_cv_prog_cc_c11"])
])
