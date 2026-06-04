AUTOMAKE_OPTIONS = subdir-objects

check_PROGRAMS = check_config check_wdefaults
TESTS = $(check_PROGRAMS)

AM_CFLAGS = @PANGO_CFLAGS@

AM_CPPFLAGS = $(DFLAGS) \
	-DWMAKER_DEFAULTS_TEMPLATE=\"$(top_srcdir)/WindowMaker/Defaults/WindowMaker.in\" \
	-I$(top_srcdir)/src -I$(top_builddir)/src \
	-I$(top_srcdir)/WINGs -I$(top_builddir)/WINGs \
	-I$(top_builddir)/wrlib \
	@HEADER_SEARCH_PATH@

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
	$(top_builddir)/WINGs/libWUtil.la \
	$(top_builddir)/wrlib/libwraster.la \
	@INTLIBS@
