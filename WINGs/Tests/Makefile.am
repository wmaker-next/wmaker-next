## automake input file for WINGs - Tests

AUTOMAKE_OPTIONS =

# interactive demo programs, built on demand only (e.g. `make wtest`)
EXTRA_PROGRAMS = wtest wmquery wmfile fonttest testmywidget

# automated unit tests, run by `make check`
check_PROGRAMS = check_wutil check_proplist check_handlers check_ipc
TESTS = $(check_PROGRAMS)

check_wutil_SOURCES = check_wutil.c
check_wutil_LDADD = $(top_builddir)/WINGs/libWUtil.la @INTLIBS@

check_proplist_SOURCES = check_proplist.c
check_proplist_LDADD = $(top_builddir)/WINGs/libWUtil.la @INTLIBS@

check_handlers_SOURCES = check_handlers.c
check_handlers_LDADD = $(top_builddir)/WINGs/libWUtil.la @INTLIBS@

check_ipc_SOURCES = check_ipc.c
check_ipc_LDADD = $(top_builddir)/WINGs/libWUtil.la @INTLIBS@

LDADD= $(top_builddir)/WINGs/libWINGs.la $(top_builddir)/wrlib/libwraster.la \
	$(top_builddir)/WINGs/libWUtil.la \
	@XFT_LIBS@ @INTLIBS@ @XLIBS@


testmywidget_SOURCES = testmywidget.c mywidget.c mywidget.h

fonttest_SOURCES = fonttest.c
fonttest_DEPENDENCIES = $(top_builddir)/WINGs/libWINGs.la

wtest_DEPENDENCIES = $(top_builddir)/WINGs/libWINGs.la


EXTRA_DIST = logo.xpm upbtn.xpm wm.html wm.png

AM_CPPFLAGS = -I$(top_srcdir)/WINGs -I$(top_builddir)/WINGs \
	-I$(top_builddir)/wrlib -I$(top_srcdir)/src \
	-DRESOURCE_PATH=\"$(datadir)/WINGs\" @XFT_CFLAGS@ @HEADER_SEARCH_PATH@ \
	@PANGO_CFLAGS@
