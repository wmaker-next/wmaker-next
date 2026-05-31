## automake input file for wrlib

AUTOMAKE_OPTIONS =

# interactive demo programs, built on demand only (e.g. `make testdraw`)
EXTRA_PROGRAMS = testdraw testgrad testrot view

# automated decoder tests, run by `make check`
check_PROGRAMS = check_load
TESTS = $(check_PROGRAMS)
AM_TESTS_ENVIRONMENT = srcdir='$(srcdir)'; export srcdir;

check_load_SOURCES = check_load.c
check_load_LDADD = $(top_builddir)/wrlib/libwraster.la

EXTRA_DIST = test.png tile.xpm ballot_box.xpm \
	pnm-p1.pbm pnm-p2.pgm pnm-p3.ppm \
	pnm-p4.pbm pnm-p5.pgm pnm-p6.ppm

AM_CPPFLAGS = -I$(srcdir)/.. $(DFLAGS) @HEADER_SEARCH_PATH@

LIBLIST = $(top_builddir)/wrlib/libwraster.la @XLIBS@

testdraw_SOURCES = testdraw.c
testdraw_LDADD = $(LIBLIST)

testgrad_SOURCES = testgrad.c
testgrad_LDADD = $(LIBLIST)

testrot_SOURCES = testrot.c
testrot_LDADD = $(LIBLIST)

view_SOURCES= view.c
view_LDADD = $(LIBLIST)
