
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

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = wmlib.pc

DISTCLEANFILES = $(pkgconfig_DATA)

EXTRA_DIST = wmlib.pc.in
