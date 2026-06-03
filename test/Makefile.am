## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS = no-dependencies

# wm_fsm_test.c needs GTK3 which is outside the project's dependency
# policy and is not probed at configure time, ship the source for
# reference but do not wire it into the build
EXTRA_DIST = notest.c wm_fsm_test.c asan-drive.sh lsan-suppressions.txt

# wmlib smoke test, built on demand only (this directory is not in
# SUBDIRS, only DIST_SUBDIRS)
EXTRA_PROGRAMS = wtest

wtest_SOURCES = wtest.c

wtest_LDADD = $(top_builddir)/wmlib/libWMaker.la @XLFLAGS@ @XLIBS@

AM_CPPFLAGS = -g -D_BSD_SOURCE @XCFLAGS@ -I$(top_srcdir)/wmlib
