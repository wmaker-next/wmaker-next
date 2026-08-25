# wm_dep_check.m4 - uniform dependency probing for pkg-config modules
#
# Copyright (c) 2026 Window Maker Team
#
# Redistribution and use is allowed according to the terms of the
# 2-clause BSD license. See COPYING.WTFPL for the project licence.
#
# Two shapes cover every pkg-config dependency in configure.ac so all
# probes share one argument grammar, one error message style and one
# feature-summary behaviour:
#
#   WM_REQUIRE_DEP(prefix, modules, why)
#     Mandatory dependency, configure fails naming 'why' when pkg-config
#     cannot find 'modules'.
#
#   WM_OPTIONAL_DEP(prefix, modules, state-var, mode, name,
#                   help-flag, help-text, when-found, missing-tag,
#                   conditional)
#     Tri-state probe behind --with-NAME / --enable-NAME (chosen by
#     'mode'):
#       auto - detect, disable when absent (the default)
#       yes  - detect, fail configure when absent
#       no   - skip detection entirely
#     state-var holds the tri-state and ends up yes or no on every path
#     ('auto' resolves during the probe), prefix_CFLAGS / prefix_LIBS are
#     substituted (empty when not built), when-found is expanded after a
#     successful probe (AC_DEFINE, feature summary), missing-tag is
#     appended to the $unsupported summary line whenever the feature ends
#     up not built, and conditional names an optional AM_CONDITIONAL keyed
#     on state-var = yes.

# WM_REQUIRE_DEP
# --------------
AC_DEFUN([WM_REQUIRE_DEP],
[PKG_CHECK_MODULES([$1], [$2], [],
    [AC_MSG_ERROR([$3 -- $$1_PKG_ERRORS])])
]) dnl WM_REQUIRE_DEP


# WM_OPTIONAL_DEP
# ---------------
AC_DEFUN([WM_OPTIONAL_DEP],
[m4_if([$4], [with],
    [AC_ARG_WITH([$5],
        [AS_HELP_STRING([$6], [$7])],
        [AS_CASE([$withval],
            [yes|no|auto], [[$3]=$withval],
            [AC_MSG_ERROR([bad value '$withval' for --with-$5, expected yes, no or auto])])],
        [[$3]=auto])],
    [AC_ARG_ENABLE([$5],
        [AS_HELP_STRING([$6], [$7])],
        [AS_CASE([$enableval],
            [yes|no|auto], [[$3]=$enableval],
            [AC_MSG_ERROR([bad value '$enableval' for --enable-$5, expected yes, no or auto])])],
        [[$3]=auto])])
[$1]_CFLAGS=
[$1]_LIBS=
AS_IF([test "x$$3" != "xno"],
    [PKG_CHECK_MODULES([$1], [$2],
        [[$3]=yes
         $8],
        [AS_IF([test "x$$3" = "xyes"],
            [AC_MSG_ERROR([pkg-config cannot find '$2' -- $$1_PKG_ERRORS])],
            [[$3]=no
             m4_ifval([$9], [unsupported="$unsupported $9"])])])],
    [m4_ifval([$9], [unsupported="$unsupported $9"])])
AC_SUBST([$1]_CFLAGS)
AC_SUBST([$1]_LIBS)
m4_ifval([$10],
    [AM_CONDITIONAL([$10], [test "x$$3" = "xyes"])])
]) dnl WM_OPTIONAL_DEP
