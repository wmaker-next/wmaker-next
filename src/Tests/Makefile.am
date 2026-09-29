AUTOMAKE_OPTIONS = subdir-objects

check_PROGRAMS = check_config check_wdefaults check_wprefs_schema check_desktopfile check_cmdexpand check_calc check_xsmp check_layout check_geometry check_xlibfree check_xerror
TESTS = $(check_PROGRAMS)

# libFuzzer harnesses for the two X-bound parse cores extracted from
# src/ (the WMMenu text protocol and the XDND uri-list split), built on
# demand only under a clang sanitizer configure, same recipe as the
# WINGs/Tests fuzzers:
#   ./configure CC=clang \
#     CFLAGS="-fsanitize=address,undefined,fuzzer-no-link -fno-sanitize-recover=undefined -g -O1" \
#     LDFLAGS="-fsanitize=address,undefined"
#   make -C src/Tests fuzz_appmenu fuzz_xdnduri
EXTRA_PROGRAMS = fuzz_appmenu fuzz_xdnduri

fuzz_appmenu_SOURCES = fuzz_appmenu.c ../appmenuparse.c
fuzz_appmenu_LDADD = $(top_builddir)/WINGs/libWUtil.la @INTLIBS@
fuzz_appmenu_LDFLAGS = -fsanitize=fuzzer

fuzz_xdnduri_SOURCES = fuzz_xdnduri.c ../xdnduri.c
fuzz_xdnduri_LDADD = $(top_builddir)/WINGs/libWUtil.la @INTLIBS@
fuzz_xdnduri_LDFLAGS = -fsanitize=fuzzer

AM_CFLAGS = @CAIRO_CFLAGS@

AM_CPPFLAGS = $(DFLAGS) \
	-DWMAKER_DEFAULTS_TEMPLATE=\"$(top_srcdir)/WindowMaker/Defaults/WindowMaker.in\" \
	-I$(top_srcdir)/src -I$(top_builddir)/src \
	-I$(top_srcdir)/WINGs -I$(top_builddir)/WINGs \
	-I$(top_srcdir)/wrlib \
	-I$(top_srcdir)/test \
	@HEADER_SEARCH_PATH@

AM_TESTS_ENVIRONMENT = \
	CHECK_DESKTOPFILE_SRCDIR='$(srcdir)'; export CHECK_DESKTOPFILE_SRCDIR; \
	CHECK_XLIBFREE_TOP='$(top_builddir)'; export CHECK_XLIBFREE_TOP; \
	CHECK_XLIBFREE_SRC='$(top_srcdir)'; export CHECK_XLIBFREE_SRC;

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
	$(top_builddir)/src/libwconfigschema.la \
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

check_calc_SOURCES = \
	check_calc.c \
	../calc.c

check_calc_LDADD = \
	$(top_builddir)/WINGs/libWUtil.la \
	@INTLIBS@ \
	@LIBM@

check_geometry_SOURCES = \
	check_geometry.c \
	../geometry.c

check_geometry_LDADD = \
	$(top_builddir)/WINGs/libWUtil.la \
	@INTLIBS@

check_xlibfree_SOURCES = \
	check_xlibfree.c

check_xerror_SOURCES = \
	check_xerror.c \
	../xutil.c

check_xerror_LDADD = \
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
