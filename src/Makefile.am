AUTOMAKE_OPTIONS =

BUILT_SOURCES = wconfig.h

bin_PROGRAMS = wmaker
SUBDIRS = Tests

EXTRA_DIST =

# config-schema.c compiles to a self-contained object (sole external
# reference is strcasecmp), so build it once as a noinst archive that
# wmaker, WPrefs.app and src/Tests/check_config all link against.
noinst_LTLIBRARIES = libwconfigschema.la
libwconfigschema_la_SOURCES = \
	config-schema.c \
	config-schema.h \
	config-schema-def.h \
	config-schema-public.h \
	config-values.h

wmaker_SOURCES = 	\
	GNUstep.h \
	WindowMaker.h \
	actions.c \
	actions.h \
	appicon.c \
	appicon.h \
	application.c \
	application.h \
	appmenu.c \
	appmenu.h \
	balloon.c \
	balloon.h \
	client.c \
	client.h \
	colormap.c \
	colormap.h \
	cycling.c \
	cycling.h \
	def_pixmaps.h \
	defaults.c \
	defaults.h \
	dialog.c \
	dialog.h \
	dock.c \
	dockedapp.c \
	dockedapp.h \
	dock.h \
	event.c \
	event.h \
	event_client.c \
	event_input.c \
	event_root.c \
	event_ext.c \
	extend_pixmaps.h \
	framewin.c \
	framewin.h \
	geomview.c \
	geomview.h \
	osdview.c \
	osdview.h \
	hotcorner.c \
	hotcorner.h \
	osdep.h \
	icon.c \
	icon.h \
	ipc.c \
	ipc.h \
	keybind.h \
	keytree.c \
	keytree.h \
	main.c \
	main.h \
	menu.c \
	menu.h \
	misc.c \
	misc.h \
	monitor.c \
	monitor.h \
	moveres.c \
	pixmap.c \
	pixmap.h \
	placement.c \
	placement.h \
	properties.c \
	properties.h \
	randr.c \
	randr.h \
	resources.c \
	resources.h \
	rootmenu.c \
	rootmenu.h \
	screen.c \
	screen.h \
	session.h \
	session.c \
	shutdown.h \
	shutdown.c \
	switchpanel.c \
	switchpanel.h \
	stacking.c \
	stacking.h \
	startup.c \
	startup.h \
	startup_notify.c \
	startup_notify.h \
	superfluous.c \
	superfluous.h \
	switchmenu.c \
	switchmenu.h \
	tabgroup.c \
	tabgroup.h \
	texture.c \
	texture.h \
	usermenu.c \
	usermenu.h \
	xdnd.h \
	xinerama.h \
	xinerama.c \
	wxkb.c \
	wxkb.h \
	xutil.c \
	xutil.h \
	wconfig.h \
	wconfigmodel.c \
	wconfigmodel.h \
	wcore.c \
	wcore.h \
	wdefaults.c \
	wdefaults.h \
	window.c \
	window.h \
	winmenu.c \
	winmenu.h \
	winspector.h \
	winspector.c \
	wireframe.c \
	wireframe.h \
	wmspec.h \
	wmspec.c \
	workspace.c \
	workspace.h \
	wsmap.c \
	wsmap.h

if USE_DOCK_XDND
wmaker_SOURCES += xdnd.c
endif
if USE_MWM_HINTS
wmaker_SOURCES += motif.h motif.c
endif

if WM_OSDEP_BSD
wmaker_SOURCES += osdep_bsd.c
endif
if WM_OSDEP_DARWIN
wmaker_SOURCES += osdep_darwin.c
endif
if WM_OSDEP_LINUX
wmaker_SOURCES += osdep_linux.c
endif
if WM_OSDEP_GENERIC
wmaker_SOURCES += osdep_stub.c
endif


AM_CFLAGS = @PANGO_CFLAGS@

AM_CPPFLAGS = $(DFLAGS) \
	-I$(top_builddir)/wrlib \
	-I$(top_srcdir)/WINGs -I$(top_builddir)/WINGs \
	@XCB_CFLAGS@ \
	@STARTUP_NOTIFICATION_CFLAGS@ \
	@HEADER_SEARCH_PATH@


wmaker_LDADD = \
	libwconfigschema.la \
	$(top_builddir)/WINGs/libWINGs.la\
	$(top_builddir)/WINGs/libWUtil.la\
	$(top_builddir)/wrlib/libwraster.la\
	@XLFLAGS@ \
	@STARTUP_NOTIFICATION_LIBS@ \
	@XCB_LIBS@ \
	@XLIBS@ \
	@LIBM@ \
	@INTLIBS@
