SUBDIRS = po Tests

AUTOMAKE_OPTIONS =

pkgdatadir  = $(datadir)/@PACKAGE@

bin_PROGRAMS = wxcopy wxpaste wdwrite wdread getstyle setstyle convertfonts \
	seticons geticonset wmsetbg wmagnify wmgenmenu wmmenugen wmiv wmakermsg

bin_SCRIPTS = wmaker.inst wm-oldmenu2new wkdemenu.pl

EXTRA_DIST = wmaker.inst.in bughint wm-oldmenu2new wkdemenu.pl

AM_CPPFLAGS = \
	 $(DFLAGS) -I$(top_srcdir)/WINGs -I$(top_builddir)/WINGs -I$(top_srcdir)/wrlib \
	@HEADER_SEARCH_PATH@ \
	-DETCDIR=\"sysconfdir\" -DDATADIR=\"pkgdatadir\"

liblist= @LIBRARY_SEARCH_PATH@ @INTLIBS@

wdwrite_LDADD = $(top_builddir)/WINGs/libWUtil.la $(liblist)

wdread_LDADD = $(top_builddir)/WINGs/libWUtil.la $(liblist)

wmakermsg_LDADD = $(top_builddir)/WINGs/libWUtil.la $(liblist)

bin_PROGRAMS += wmclipd
wmclipd_CFLAGS = @XCB_CFLAGS@
wmclipd_SOURCES = wmclipd.c clip-common.c clip-common.h
wmclipd_LDADD = \
	$(top_builddir)/WINGs/libWUtil.la \
	@XCB_LIBS@ $(liblist)

if BUILD_WMLOCK
bin_PROGRAMS += wmlock
wmlock_SOURCES = wmlock.c
wmlock_CFLAGS = @XCB_CFLAGS@ @XCB_DPMS_CFLAGS@
wmlock_LDADD = \
	$(top_builddir)/WINGs/libWINGs.la \
	$(top_builddir)/WINGs/libWUtil.la \
	$(top_builddir)/wrlib/libwraster.la \
	@XLFLAGS@ @XLIBS@ @XCB_DPMS_LIBS@ @PAM_LIBS@ $(liblist)
if HAVE_PAM
pamconfdir = $(sysconfdir)/pam.d
dist_pamconf_DATA = pam.d/wmlock
endif
endif
EXTRA_DIST += pam.d/wmlock

wxcopy_LDADD = @XLFLAGS@ @XLIBS@

wxpaste_LDADD = @XLFLAGS@ @XLIBS@

getstyle_LDADD = $(top_builddir)/WINGs/libWUtil.la $(liblist)

getstyle_SOURCES = getstyle.c fontconv.c common.h

setstyle_LDADD = \
	$(top_builddir)/WINGs/libWUtil.la \
	@XLFLAGS@ @XLIBS@ $(liblist)

setstyle_SOURCES = setstyle.c fontconv.c common.h

convertfonts_LDADD = $(top_builddir)/WINGs/libWUtil.la $(liblist)

convertfonts_SOURCES = convertfonts.c fontconv.c common.h

seticons_LDADD= $(top_builddir)/WINGs/libWUtil.la $(liblist)

geticonset_LDADD= $(top_builddir)/WINGs/libWUtil.la $(liblist)

wmagnify_CFLAGS = @CAIRO_CFLAGS@
wmagnify_LDADD = \
	$(top_builddir)/WINGs/libWINGs.la \
	$(top_builddir)/WINGs/libWUtil.la \
	$(top_builddir)/wrlib/libwraster.la \
	@XLFLAGS@ @XLIBS@ @CAIRO_LIBS@ @INTLIBS@

wmsetbg_CFLAGS = @CAIRO_CFLAGS@
wmsetbg_LDADD = \
	$(top_builddir)/WINGs/libWINGs.la \
	$(top_builddir)/WINGs/libWUtil.la \
	$(top_builddir)/wrlib/libwraster.la \
	@XLFLAGS@ @XLIBS@ @XCB_LIBS@ @CAIRO_LIBS@ @INTLIBS@

wmgenmenu_LDADD = \
	$(top_builddir)/WINGs/libWUtil.la \
	@INTLIBS@

wmgenmenu_SOURCES = wmgenmenu.c wmgenmenu.h

wmmenugen_LDADD = \
	$(top_builddir)/WINGs/libWUtil.la \
	@INTLIBS@

wmmenugen_SOURCES = wmmenugen.c wmmenugen.h wmmenugen_misc.c \
	wmmenugen_parse_wmconfig.c \
	wmmenugen_parse_xdg.c

wmiv_CFLAGS = @XCB_CFLAGS@ @CAIRO_CFLAGS@ @PTHREAD_CFLAGS@ @LIBARCHIVE_CFLAGS@

wmiv_LDADD = \
	$(top_builddir)/wrlib/libwraster.la \
	@XCB_LIBS@ -lxcb-icccm \
	@CAIRO_LIBS@ @PTHREAD_LIBS@ @LIBEXIF@ \
	@LIBARCHIVE_LIBS@ @LIBM@

wmiv_SOURCES = wmiv.c wmiv_icon.h clip-common.c clip-common.h

CLEANFILES = wmaker.inst

wmaker.inst: $(srcdir)/wmaker.inst.in ./Makefile
	$(AM_V_GEN)sed -e "s|#pkgdatadir#|$(pkgdatadir)|" \
		-e "s|#sysconfdir#|$(sysconfdir)/WindowMaker|" \
		-e "s|#version#|$(VERSION)|" \
		-e "s|#bindir#|$(bindir)|" \
			$(srcdir)/wmaker.inst.in >wmaker.inst ; \
	chmod 755 wmaker.inst
