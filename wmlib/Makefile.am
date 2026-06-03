
AUTOMAKE_OPTIONS = no-dependencies

libWMaker_la_LDFLAGS = -version-info 1:1:0
lib_LTLIBRARIES = libWMaker.la

include_HEADERS = WMaker.h

AM_CPPFLAGS = $(DFLAGS) @XCFLAGS@

libWMaker_la_LIBADD = @XLFLAGS@ @XLIBS@

libWMaker_la_SOURCES = 	\
	menu.c 		\
	app.c 		\
	event.c 	\
	command.c	\
	app.h		\
	menu.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = wmlib.pc

DISTCLEANFILES = $(pkgconfig_DATA)

EXTRA_DIST = wmlib.pc.in
