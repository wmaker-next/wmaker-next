AUTOMAKE_OPTIONS =

BUILT_SOURCES = wconfig.h

bin_PROGRAMS = wmaker
SUBDIRS = . Tests

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
	config-values.h \
	keybind-descriptions.h

# keybind-descriptions.h holds localized strings; opt it in to the
# translation-source consistency check (see po/check-local).
wmaker_TRANSLATABLE_FILES = \
	keybind-descriptions.h

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
	appmenuparse.c \
	appmenuparse.h \
	balloon.c \
	balloon.h \
	client.c \
	client.h \
	colormap.c \
	colormap.h \
	cmdexpand.c \
	calc.c \
	calc.h \
	geometry.c \
	geometry.h \
	cycling.c \
	cycling.h \
	def_pixmaps.h \
	defaults.c \
	defaults.h \
	desktopfile.c \
	desktopfile.h \
	dialog.c \
	dialog.h \
	dialog_crash.c \
	dialog_icon.c \
	dialog_icon.h \
	dialog_run.c \
	dialog_run.h \
	dock.c \
	dockgeom.c \
	dockgeom.h \
	dockslots.c \
	dockslots.h \
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
	cursor.c \
	cursor.h \
	cursor_find.c \
	cursor_find.h \
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
	keybind.c \
	keybind.h \
	keytree.c \
	keytree.h \
	layout.c \
	layout.h \
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
	placecompute.c \
	placecompute.h \
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
	stacklist.c \
	stacklist.h \
	focusmodel.c \
	focusmodel.h \
	wsplan.c \
	wsplan.h \
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
	xdnduri.h \
	xinerama.h \
	xinerama.c \
	xsmp.c \
	xsmp.h \
	xsettings.h \
	xsettingswire.c \
	xsettings.c \
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
	wdisplay.c \
	wdisplay.h \
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
	vdesk.c \
	vdesk.h \
	wsmap.c \
	wsmap.h \
	wsound.c \
	wsound.h

if USE_DOCK_XDND
wmaker_SOURCES += xdnd.c xdnduri.c
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


AM_CFLAGS = @CAIRO_CFLAGS@

AM_CPPFLAGS = $(DFLAGS) @XRES_CFLAGS@ \
	-I$(top_srcdir)/wrlib \
	-I$(top_srcdir)/WINGs -I$(top_builddir)/WINGs \
	@XCB_CFLAGS@ \
	@XCB_SCREENSAVER_CFLAGS@ \
	@STARTUP_NOTIFICATION_CFLAGS@ \
	@XSMP_CFLAGS@ \
	@HEADER_SEARCH_PATH@


wmaker_LDADD = \
	libwconfigschema.la \
	$(top_builddir)/WINGs/libWINGs.la\
	$(top_builddir)/WINGs/libWUtil.la\
	$(top_builddir)/wrlib/libwraster.la\
	@STARTUP_NOTIFICATION_LIBS@ \
	@XSMP_LIBS@ \
	@XCB_LIBS@ \
	@XCB_SCREENSAVER_LIBS@ \
	@XRES_LIBS@ \
	@CAIRO_LIBS@ \
	@LIBM@ \
	@INTLIBS@
