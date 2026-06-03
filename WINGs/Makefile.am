## automake input file for WINGs

AUTOMAKE_OPTIONS =

SUBDIRS = WINGs . po Documentation Resources Tests
DIST_SUBDIRS = $(SUBDIRS) Examples Extras

libWINGs_la_LDFLAGS = -version-info @WINGS_VERSION@
libWUtil_la_LDFLAGS = -version-info @WUTIL_VERSION@

lib_LTLIBRARIES = libWUtil.la libWINGs.la


LDADD= libWUtil.la libWINGs.la $(top_builddir)/wrlib/libwraster.la @INTLIBS@
libWINGs_la_LIBADD = libWUtil.la $(top_builddir)/wrlib/libwraster.la @XLIBS@ @XCB_LIBS@ @XFT_LIBS@ @FCLIBS@ @LIBM@ @PANGO_LIBS@
libWUtil_la_LIBADD = @LIBBSD@ @LIBEV_LIBS@

EXTRA_DIST = BUGS make-rgb Examples Extras Tests


# wbutton.c
libWINGs_la_SOURCES = 	\
	configuration.c \
	dragcommon.c \
	dragdestination.c \
	dragsource.c \
	rgb.h \
	selection.c \
	wxprop.c \
	wxhints.c \
	wappresource.c \
	wballoon.c \
	wbox.c \
	wbrowser.c \
	wbutton.c \
	wcolor.c \
	wcolorpanel.c \
	wcolorwell.c \
	wconfig.h \
	wevent.c \
	wfilepanel.c \
	wframe.c \
	wfont.c \
	wfontpanel.c \
	widgets.c \
	winputmethod.c \
	wlabel.c \
	wlist.c \
	wmenuitem.c \
	wmisc.c \
	wpanel.c \
	wpixmap.c \
	wpopupbutton.c \
	wprogressindicator.c \
	wruler.c \
	wscroller.c \
	wscrollview.c \
	wslider.c \
	wsplitview.c \
	wtabview.c \
	wtext.c \
	wtextfield.c \
	wview.c \
	wwindow.c

libWUtil_la_SOURCES = 	\
	array.c \
	bagtree.c \
	data.c \
	error.c \
	error.h \
	findfile.c \
	handlers.c \
	hashtable.c \
	intern.c \
	memory.c \
	menuparser.c \
	menuparser.h \
	menuparser_macros.c \
	misc.c \
	notification.c \
	proplist.c \
	spawn.c \
	string.c \
	tree.c \
	userdefaults.c \
	userdefaults.h \
	usleep.c \
	wapplication.c \
	wconfig.h \
	wutil.c


AM_CFLAGS = @PANGO_CFLAGS@ @FONTCONFIG_CFLAGS@

AM_CPPFLAGS = -DRESOURCE_PATH=\"$(datadir)/WINGs\" \
	 -I$(top_srcdir)/WINGs/WINGs -I$(top_builddir)/WINGs/WINGs \
	 -I$(top_builddir)/wrlib -I$(top_srcdir)/src \
	@XCB_CFLAGS@ @XFT_CFLAGS@ @HEADER_SEARCH_PATH@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = WINGs.pc WUtil.pc

DISTCLEANFILES = $(pkgconfig_DATA)

EXTRA_DIST += WINGs.pc.in WUtil.pc.in
