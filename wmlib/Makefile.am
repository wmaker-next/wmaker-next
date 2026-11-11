
AUTOMAKE_OPTIONS = no-dependencies

libWMaker_la_LDFLAGS = -version-info 1:1:0
lib_LTLIBRARIES = libWMaker.la

# the LGPL library links only the self-contained IPC transport, never
# the GPL libWUtil core
include_HEADERS = WMaker.h

AM_CPPFLAGS = @XCB_CORE_CFLAGS@ -I$(top_srcdir)/WINGs -I$(top_builddir)/WINGs

libWMaker_la_LIBADD = $(top_builddir)/WINGs/libwipc.la @XCB_CORE_LIBS@

libWMaker_la_SOURCES = 	\
	menu.c 		\
	app.c 		\
	event.c 	\
	command.c	\
	wipcreply.c	\
	app.h		\
	menu.h		\
	wipcreply.h

check_PROGRAMS = check_wmlib
TESTS = $(check_PROGRAMS)

check_wmlib_SOURCES = check_wmlib.c
check_wmlib_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/test
check_wmlib_LDADD = libWMaker.la $(top_builddir)/WINGs/libWUtil.la \
	@XCB_CORE_LIBS@ @INTLIBS@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = wmlib.pc

DISTCLEANFILES = $(pkgconfig_DATA)

EXTRA_DIST = COPYING.LIB wmlib.pc.in
