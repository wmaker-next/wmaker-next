SUBDIRS = po Tests

AUTOMAKE_OPTIONS =

pkgdatadir  = $(datadir)/@PACKAGE@

bin_PROGRAMS = wxcopy wxpaste wdwrite wdread getstyle setstyle convertfonts \
	seticons geticonset wmsetbg wmagnify wmgenmenu wmmenugen wmiv wmakermsg wmglyph

bin_SCRIPTS = wmaker.inst wm-oldmenu2new wkdemenu.pl

EXTRA_DIST = wmaker.inst.in bughint wm-oldmenu2new wkdemenu.pl \
	wmglyph-data/UNICODE-LICENSE

AM_CPPFLAGS = \
	 $(DFLAGS) -I$(top_srcdir)/WINGs -I$(top_builddir)/WINGs -I$(top_srcdir)/wrlib \
	@HEADER_SEARCH_PATH@ \
	-DETCDIR=\"sysconfdir\" -DDATADIR=\"pkgdatadir\"

liblist= @LIBRARY_SEARCH_PATH@ @INTLIBS@

wdwrite_LDADD = $(top_builddir)/WINGs/libWUtil.la $(liblist)

wdread_LDADD = $(top_builddir)/WINGs/libWUtil.la $(liblist)

wmakermsg_LDADD = $(top_builddir)/WINGs/libWUtil.la $(liblist)

bin_PROGRAMS += wmclipd
wmclipd_CFLAGS = @XCB_CFLAGS@ @XCB_IMDKIT_CFLAGS@ @CAIRO_CFLAGS@
wmclipd_SOURCES = wmclipd.c clip_common.c clip_common.h clip_geom.c clip_geom.h xcconn.c xcconn.h
wmclipd_LDADD = \
	$(top_builddir)/WINGs/libWINGs.la \
	$(top_builddir)/WINGs/libWUtil.la \
	$(top_builddir)/wrlib/libwraster.la \
	@XCB_LIBS@ @XCB_IMDKIT_LIBS@ $(liblist)

if BUILD_WMLOCK
bin_PROGRAMS += wmlock
wmlock_SOURCES = wmlock.c xcconn.c xcconn.h
wmlock_CFLAGS = @XCB_CFLAGS@ @XCB_DPMS_CFLAGS@ @CAIRO_CFLAGS@
wmlock_LDADD = \
	$(top_builddir)/WINGs/libWINGs.la \
	$(top_builddir)/WINGs/libWUtil.la \
	$(top_builddir)/wrlib/libwraster.la \
	@XCB_LIBS@ @XCB_DPMS_LIBS@ @PAM_LIBS@ $(liblist)
if HAVE_PAM
pamconfdir = $(sysconfdir)/pam.d
dist_pamconf_DATA = pam.d/wmlock
endif
endif
EXTRA_DIST += pam.d/wmlock

if BUILD_WSOUND
bin_PROGRAMS += wmsoundd
wmsoundd_SOURCES = wmsoundd.c
wmsoundd_CFLAGS = @WSOUND_CFLAGS@
wmsoundd_LDADD = \
	$(top_builddir)/WINGs/libWUtil.la \
	@WSOUND_LIBS@ $(liblist)
endif

if BUILD_WTOUCH
bin_PROGRAMS += wmtouchd
wmtouchd_SOURCES = wmtouchd.c wmtouch_core.c wmtouch_core.h
wmtouchd_CFLAGS = @WTOUCH_CFLAGS@
wmtouchd_LDADD = \
	$(top_builddir)/WINGs/libWUtil.la \
	@WTOUCH_LIBS@ $(liblist)
endif
EXTRA_DIST += wmtouch_core.c wmtouch_core.h

wxcopy_CFLAGS = @XCB_CORE_CFLAGS@
wxcopy_SOURCES = wxcopy.c xcconn.c xcconn.h
wxcopy_LDADD = @XCB_CORE_LIBS@

wxpaste_CFLAGS = @XCB_CORE_CFLAGS@
wxpaste_SOURCES = wxpaste.c clip_common.c clip_common.h xcconn.c xcconn.h
wxpaste_LDADD = @XCB_CORE_LIBS@

getstyle_LDADD = $(top_builddir)/WINGs/libWUtil.la $(liblist)

getstyle_SOURCES = getstyle.c fontconv.c common.h

setstyle_LDADD = \
	$(top_builddir)/WINGs/libWINGs.la \
	$(top_builddir)/WINGs/libWUtil.la \
	@XCB_LIBS@ $(liblist)

setstyle_SOURCES = setstyle.c fontconv.c common.h xcconn.c xcconn.h

convertfonts_LDADD = $(top_builddir)/WINGs/libWUtil.la $(liblist)

convertfonts_SOURCES = convertfonts.c fontconv.c common.h

seticons_LDADD= $(top_builddir)/WINGs/libWUtil.la $(liblist)

geticonset_LDADD= $(top_builddir)/WINGs/libWUtil.la $(liblist)

wmagnify_CFLAGS = @CAIRO_CFLAGS@ @WRASTER_XCB_CFLAGS@
wmagnify_SOURCES = wmagnify.c xcconn.c xcconn.h
wmagnify_LDADD = \
	$(top_builddir)/WINGs/libWINGs.la \
	$(top_builddir)/WINGs/libWUtil.la \
	$(top_builddir)/wrlib/libwraster.la \
	@XCB_LIBS@ @WRASTER_XCB_LIBS@ @CAIRO_LIBS@ @INTLIBS@

wmglyph_LDADD = \
	$(top_builddir)/WINGs/libWINGs.la \
	$(top_builddir)/WINGs/libWUtil.la \
	$(top_builddir)/wrlib/libwraster.la \
	@XCB_LIBS@ @CAIRO_LIBS@ @INTLIBS@

wmglyph_SOURCES = wmglyph.c wmglyph-data/wmglyph_data.c wmglyph-data/wmglyph_data.h wmglyph_icon.h \
	xcconn.c xcconn.h
wmglyph_CFLAGS = -I$(srcdir)/wmglyph-data @CAIRO_CFLAGS@ @FONTCONFIG_CFLAGS@

wmsetbg_CFLAGS = @XCB_CFLAGS@ @CAIRO_CFLAGS@
wmsetbg_SOURCES = wmsetbg.c xcconn.c xcconn.h
wmsetbg_LDADD = \
	$(top_builddir)/WINGs/libWINGs.la \
	$(top_builddir)/WINGs/libWUtil.la \
	$(top_builddir)/wrlib/libwraster.la \
	@XCB_LIBS@ @CAIRO_LIBS@ @INTLIBS@

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

wmiv_SOURCES = wmiv.c wmiv_icon.h clip_common.c clip_common.h xcconn.c xcconn.h

CLEANFILES = wmaker.inst

wmaker.inst: $(srcdir)/wmaker.inst.in ./Makefile
	$(AM_V_GEN)sed -e "s|#pkgdatadir#|$(pkgdatadir)|" \
		-e "s|#sysconfdir#|$(sysconfdir)/WindowMaker|" \
		-e "s|#version#|$(VERSION)|" \
		-e "s|#bindir#|$(bindir)|" \
			$(srcdir)/wmaker.inst.in >wmaker.inst ; \
	chmod 755 wmaker.inst
