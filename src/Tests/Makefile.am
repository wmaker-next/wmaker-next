AUTOMAKE_OPTIONS = subdir-objects

check_PROGRAMS = check_config
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
	../config-schema.c \
	../config-schema.h \
	../wconfigmodel.c \
	../wconfigmodel.h

check_config_LDADD = \
	$(top_builddir)/WINGs/libWUtil.la \
	@INTLIBS@
