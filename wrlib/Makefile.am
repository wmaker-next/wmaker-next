## automake input file for wrlib

SUBDIRS = . po Tests
DIST_SUBDIRS = $(SUBDIRS)

AUTOMAKE_OPTIONS =

EXTRA_DIST = COPYING stb_image.h stb_image_resize2.h

lib_LTLIBRARIES = libwraster.la

libwraster_la_LDFLAGS = -version-info @WRASTER_VERSION@

if HAVE_LD_VERSION_SCRIPT
libwraster_la_LDFLAGS += -Wl,--version-script=libwraster.map
EXTRA_libwraster_la_DEPENDENCIES = libwraster.map
CLEANFILES = libwraster.map
endif

include_HEADERS = wraster.h

libwraster_la_SOURCES = 	\
	imgformat.h 	\
	raster.c 	\
	alpha_combine.c \
	cairo.c		\
	draw.c		\
	color.c		\
	load.c 		\
	save.c		\
	gradient.c 	\
	xpixmap.c	\
	convert.h 	\
	convert.c 	\
	context.c 	\
	misc.c 		\
	scale.c		\
	scale.h		\
	rotate.c	\
	rotate.h	\
	flip.c		\
	convolve.c	\
	palette.c	\
	save_xpm.c	\
	wr_i18n.h	\
	xutil.c		\
	xutil.h		\
	load_ppm.c	\
	load_stb.c	\
	load_xpm.c

if USE_JPEG
libwraster_la_SOURCES += load_jpeg.c
libwraster_la_SOURCES += save_jpeg.c
endif

if USE_JXL
libwraster_la_SOURCES += load_jxl.c
endif

if USE_PNG
libwraster_la_SOURCES += load_png.c
libwraster_la_SOURCES += save_png.c
endif

if USE_TIFF
libwraster_la_SOURCES += load_tiff.c
endif

if USE_SVG
libwraster_la_SOURCES += load_svg.c
endif

if USE_WEBP
libwraster_la_SOURCES += load_webp.c
endif

if USE_HEIF
libwraster_la_SOURCES += load_heif.c
endif

if USE_MAGICK
libwraster_la_SOURCES += load_magick.c
endif

AM_CFLAGS = @MAGICKFLAGS@
AM_CPPFLAGS = $(DFLAGS) @WRASTER_XCB_CFLAGS@ @WRASTER_CAIRO_CFLAGS@ @RSVG_CFLAGS@ @HEIF_CFLAGS@ @HEADER_SEARCH_PATH@

libwraster_la_LIBADD = @LIBRARY_SEARCH_PATH@ @GFXLIBS@ @MAGICKLIBS@ @WRASTER_XCB_LIBS@ @WRASTER_CAIRO_LIBS@ @INTLIBS@ -lm

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = wrlib.pc

DISTCLEANFILES = $(pkgconfig_DATA)

EXTRA_DIST += wrlib.pc.in


if HAVE_LD_VERSION_SCRIPT
libwraster.map: $(include_HEADERS) $(top_srcdir)/script/generate-mapfile-from-header.sh
	$(AM_V_GEN)$(top_srcdir)/script/generate-mapfile-from-header.sh \
		-n LIBWRASTER -v $(WRASTER_VERSION) $<  >  $@
endif
