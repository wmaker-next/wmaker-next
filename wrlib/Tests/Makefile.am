## automake input file for wrlib

AUTOMAKE_OPTIONS =

# interactive demo programs, built on demand only (e.g. `make testdraw`)
EXTRA_PROGRAMS = testdraw testgrad testrot view

# automated decoder tests, run by `make check`
check_PROGRAMS = check_load check_convolve check_image check_xside
TESTS = $(check_PROGRAMS)
AM_TESTS_ENVIRONMENT = srcdir='$(srcdir)'; export srcdir;

check_load_SOURCES = check_load.c
check_load_LDADD = $(top_builddir)/wrlib/libwraster.la \
	$(top_builddir)/WINGs/libWUtil.la

check_convolve_SOURCES = check_convolve.c
check_convolve_LDADD = $(top_builddir)/wrlib/libwraster.la

check_image_SOURCES = check_image.c
check_image_LDADD = $(top_builddir)/wrlib/libwraster.la

# round-trips RImage -> Pixmap -> RImage so it needs a server, exits 77
# (SKIP) without one so make check stays X-free
check_xside_SOURCES = check_xside.c
check_xside_LDADD = $(top_builddir)/wrlib/libwraster.la @WRASTER_XCB_LIBS@ @XLIBS@ -lX11-xcb

EXTRA_DIST = test.png tile.xpm ballot_box.xpm \
	pnm-p1.pbm pnm-p2.pgm pnm-p3.ppm \
	pnm-p4.pbm pnm-p5.pgm pnm-p6.ppm

AM_CPPFLAGS = -I$(top_srcdir)/wrlib -I$(srcdir)/.. -I$(top_srcdir)/test $(DFLAGS) @HEADER_SEARCH_PATH@

LIBLIST = $(top_builddir)/wrlib/libwraster.la @XLIBS@ -lX11-xcb

testdraw_SOURCES = testdraw.c
testdraw_LDADD = $(LIBLIST)

testgrad_SOURCES = testgrad.c
testgrad_LDADD = $(LIBLIST)

testrot_SOURCES = testrot.c
testrot_LDADD = $(LIBLIST)

view_SOURCES= view.c
view_LDADD = $(LIBLIST)
