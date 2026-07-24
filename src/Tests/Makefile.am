AUTOMAKE_OPTIONS = subdir-objects

check_PROGRAMS = check_config check_wdefaults check_wprefs_schema check_desktopfile check_cmdexpand check_xsmp check_layout
TESTS = $(check_PROGRAMS)

AM_CFLAGS = @CAIRO_CFLAGS@

AM_CPPFLAGS = $(DFLAGS) \
	-DWMAKER_DEFAULTS_TEMPLATE=\"$(top_srcdir)/WindowMaker/Defaults/WindowMaker.in\" \
	-I$(top_srcdir)/src -I$(top_builddir)/src \
	-I$(top_srcdir)/WINGs -I$(top_builddir)/WINGs \
	-I$(top_srcdir)/wrlib \
	-I$(top_srcdir)/test \
	@HEADER_SEARCH_PATH@

AM_TESTS_ENVIRONMENT = \
	CHECK_DESKTOPFILE_SRCDIR='$(srcdir)'; export CHECK_DESKTOPFILE_SRCDIR;

EXTRA_DIST = \
	xdg/share/mime/globs2 \
	xdg/share/mime/magic \
	xdg/share/applications/viewer.desktop \
	xdg/share/applications/editor.desktop \
	xdg/share/applications/multi.desktop \
	xdg/share/applications/mimeinfo.cache \
	xdg/config/mimeapps.list

check_config_SOURCES = \
	check_config.c \
	../wconfigmodel.c \
	../wconfigmodel.h

check_config_LDADD = \
	$(top_builddir)/src/libwconfigschema.la \
	$(top_builddir)/WINGs/libWUtil.la \
	@INTLIBS@

check_wdefaults_SOURCES = \
	check_wdefaults.c \
	../wdefaults.c \
	../wdefaults.h

check_wdefaults_LDADD = \
	$(top_builddir)/WINGs/libWINGs.la \
	$(top_builddir)/WINGs/libWUtil.la \
	$(top_builddir)/wrlib/libwraster.la \
	@INTLIBS@

check_wprefs_schema_SOURCES = \
	check_wprefs_schema.c \
	$(top_srcdir)/WPrefs.app/enumchoices.h

check_wprefs_schema_LDADD = \
	$(top_builddir)/src/libwconfigschema.la \
	$(top_builddir)/WINGs/libWUtil.la \
	@INTLIBS@

check_desktopfile_SOURCES = \
	check_desktopfile.c \
	../desktopfile.c \
	../desktopfile.h

check_desktopfile_LDADD = \
	$(top_builddir)/WINGs/libWUtil.la \
	@INTLIBS@

check_cmdexpand_SOURCES = \
	check_cmdexpand.c \
	../cmdexpand.c

check_cmdexpand_LDADD = \
	$(top_builddir)/WINGs/libWUtil.la \
	@INTLIBS@

check_xsmp_SOURCES = \
	check_xsmp.c

check_xsmp_LDADD = \
	$(top_builddir)/WINGs/libWUtil.la \
	@INTLIBS@

check_layout_SOURCES = \
	check_layout.c

check_layout_LDADD = \
	$(top_builddir)/WINGs/libWUtil.la \
	@INTLIBS@
