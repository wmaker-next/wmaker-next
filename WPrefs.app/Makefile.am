SUBDIRS = xpm tiff po

AUTOMAKE_OPTIONS =

wpexecbindir = @wprefs_bindir@

wpexecbin_PROGRAMS = WPrefs

wpdatadir = @wprefs_datadir@

wpdata_DATA = WPrefs.tiff WPrefs.xpm

applicationsdir = @datadir@/applications

applications_DATA = WPrefs.desktop

WPrefs.desktop: WPrefs.desktop.in
	$(AM_V_GEN)sed -e "s:#wprefs_bindir#:${wprefs_bindir}:" \
		       -e "s:#wprefs_datadir#:${wprefs_datadir}:" $< > $@

EXTRA_DIST = $(wpdata_DATA) \
	KeyboardSettings.c \
	Themes.c \
	WPrefs.desktop.in

DISTCLEANFILES = WPrefs.desktop

WPrefs_SOURCES = \
	main.c \
	WPrefs.c \
	WPrefs.h \
	enumchoices.h \
	Appearance.c \
	Configurations.c \
	Docks.c \
	Expert.c \
	Focus.c \
	FontSimple.c \
	HotCornerShortcuts.c \
	Icons.c \
	KeyboardShortcuts.c \
	Menu.c \
	MenuPreferences.c \
	MouseSettings.c \
	Paths.c \
	Preferences.c \
	TexturePanel.c \
	TexturePanel.h \
	WindowHandling.c \
	Workspace.c \
	double.c \
	editmenu.c \
	editmenu.h \
	xmodifier.c

WPrefs_TRANSLATABLE_FILES = \
	enumchoices.h \
	keybind_descriptions.h

# not_yet_fully_implemented
#WPrefs_SOURCES += \
#	KeyboardSettings.c \
#	Themes.c

AM_CFLAGS = @CAIRO_CFLAGS@ @FONTCONFIG_CFLAGS@

AM_CPPFLAGS = -DRESOURCE_PATH=\"$(wpdatadir)\" -DWMAKER_RESOURCE_PATH=\"$(pkgdatadir)\" \
	 -I$(top_srcdir)/src -I$(top_builddir)/src \
	 -I$(top_srcdir)/wrlib -I$(top_srcdir)/WINGs -I$(top_builddir)/WINGs \
	 @HEADER_SEARCH_PATH@

WPrefs_DEPENDENCIES = \
	$(top_builddir)/src/libwconfigschema.la \
	$(top_builddir)/WINGs/libWINGs.la

WPrefs_LDADD = \
	$(top_builddir)/src/libwconfigschema.la \
	$(top_builddir)/WINGs/libWINGs.la\
	$(top_builddir)/WINGs/libWUtil.la\
	$(top_builddir)/wrlib/libwraster.la \
	@CAIRO_LIBS@ \
	@XCB_LIBS@ \
	@LIBM@ \
	@FCLIBS@ \
	@INTLIBS@
