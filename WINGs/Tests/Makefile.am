## automake input file for WINGs - Tests

AUTOMAKE_OPTIONS =

# interactive demo programs, built on demand only (e.g. `make wtest`)
EXTRA_PROGRAMS = wtest wmquery wmfile fonttest testmywidget

# libFuzzer harnesses for the property-list parser, built on demand
# only (e.g. `make fuzz_proplist`) and only under a clang sanitizer
# configure (-fsanitize=fuzzer comes from the per-program LDFLAGS, a
# plain gcc build never links them). Recipe:
#   ./configure CC=clang \
#     CFLAGS="-fsanitize=address,undefined,fuzzer-no-link -fno-sanitize-recover=undefined -g -O1" \
#     LDFLAGS="-fsanitize=address,undefined"
#   make -C WINGs/Tests fuzz_proplist
#   WINGs/Tests/fuzz_proplist corpus/ -fork=14 -ignore_crashes=1 -max_total_time=600
EXTRA_PROGRAMS += fuzz_proplist

fuzz_proplist_SOURCES = fuzz_proplist.c
fuzz_proplist_LDADD = $(top_builddir)/WINGs/libWUtil.la @INTLIBS@
fuzz_proplist_LDFLAGS = -fsanitize=fuzzer

# the demos' data files: logo.xpm/upbtn.xpm are #included at compile
# time (wmquery/wmfile) and loaded at runtime (wtest/fonttest), the
# dist tarball needs them
EXTRA_DIST = logo.xpm upbtn.xpm wm.html wm.png

# automated unit tests, run by `make check`
check_PROGRAMS = check_wutil check_proplist check_handlers check_ipc check_evq check_im check_menuparser check_wxparse check_xbm check_selection
TESTS = $(check_PROGRAMS)

check_wutil_SOURCES = check_wutil.c
check_wutil_LDADD = $(top_builddir)/WINGs/libWUtil.la @INTLIBS@

check_proplist_SOURCES = check_proplist.c
check_proplist_LDADD = $(top_builddir)/WINGs/libWUtil.la @INTLIBS@

check_handlers_SOURCES = check_handlers.c
check_handlers_LDADD = $(top_builddir)/WINGs/libWUtil.la @INTLIBS@

check_ipc_SOURCES = check_ipc.c
check_ipc_LDADD = $(top_builddir)/WINGs/libWUtil.la @INTLIBS@

# links libWINGs for the WXEvq* symbols but never opens a Display so
# make check stays X-free
check_evq_SOURCES = check_evq.c
check_evq_LDADD = $(top_builddir)/WINGs/libWINGs.la \
	$(top_builddir)/WINGs/libWUtil.la @INTLIBS@

# unit tests for xcb-path IM: compose fallback and W_IMForwardKey logic,
# never opens a Display or IM connection
check_im_SOURCES = check_im.c
check_im_CPPFLAGS = $(AM_CPPFLAGS) \
	-I$(top_srcdir)/WINGs/WINGs -I$(top_builddir)/WINGs/WINGs \
	-I$(top_srcdir)/src
check_im_LDADD = $(top_builddir)/WINGs/libWINGs.la \
	$(top_builddir)/WINGs/libWUtil.la \
	@XCB_LIBS@ @XCB_IMDKIT_LIBS@ @INTLIBS@

# unit tests for the client-side parsers: the text-property codec
# (wxhints.c) and the numeric colour grammar (wxcore.c) run without a
# connection, the property-level codec tests skip (77) without a display
check_wxparse_SOURCES = check_wxparse.c
check_wxparse_CPPFLAGS = $(AM_CPPFLAGS) \
	-I$(top_srcdir)/WINGs/WINGs -I$(top_builddir)/WINGs/WINGs
check_wxparse_LDADD = $(top_builddir)/WINGs/libWINGs.la \
	$(top_builddir)/WINGs/libWUtil.la @XCB_LIBS@ @INTLIBS@

check_menuparser_SOURCES = check_menuparser.c
check_menuparser_LDADD = $(top_builddir)/WINGs/libWINGs.la \
	$(top_builddir)/WINGs/libWUtil.la @INTLIBS@

# unit tests for the XBM parser core (wxcore.c), no Display
check_xbm_SOURCES = check_xbm.c
check_xbm_CPPFLAGS = $(AM_CPPFLAGS) \
	-I$(top_srcdir)/WINGs/WINGs -I$(top_builddir)/WINGs/WINGs
check_xbm_LDADD = $(top_builddir)/WINGs/libWINGs.la \
	$(top_builddir)/WINGs/libWUtil.la @INTLIBS@

# unit tests for the selection format-32 repack core (selection.c),
# no Display
check_selection_SOURCES = check_selection.c
check_selection_CPPFLAGS = $(AM_CPPFLAGS) \
	-I$(top_srcdir)/WINGs/WINGs -I$(top_builddir)/WINGs/WINGs
check_selection_LDADD = $(top_builddir)/WINGs/libWINGs.la \
	$(top_builddir)/WINGs/libWUtil.la @INTLIBS@


LDADD= $(top_builddir)/WINGs/libWINGs.la $(top_builddir)/wrlib/libwraster.la \
	$(top_builddir)/WINGs/libWUtil.la \
	@INTLIBS@ @XCB_LIBS@


testmywidget_SOURCES = testmywidget.c mywidget.c mywidget.h

fonttest_SOURCES = fonttest.c
fonttest_DEPENDENCIES = $(top_builddir)/WINGs/libWINGs.la

wtest_DEPENDENCIES = $(top_builddir)/WINGs/libWINGs.la

AM_CPPFLAGS = -I$(top_srcdir)/WINGs -I$(top_builddir)/WINGs \
	-I$(top_srcdir)/wrlib -I$(top_srcdir)/src -I$(top_srcdir)/test \
	-DRESOURCE_PATH=\"$(datadir)/WINGs\" @XCB_IMDKIT_CFLAGS@ @HEADER_SEARCH_PATH@ \
	@CAIRO_CFLAGS@
