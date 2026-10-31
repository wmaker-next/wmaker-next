## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS = no-dependencies

noinst_HEADERS = check_common.h

# wmlib smoke test, built on demand only (this directory is not in
# SUBDIRS, only DIST_SUBDIRS)
EXTRA_PROGRAMS = wtest

wtest_SOURCES = wtest.c
wtest_CFLAGS = @XCB_CORE_CFLAGS@
wtest_LDADD = $(top_builddir)/wmlib/libWMaker.la \
	$(top_builddir)/WINGs/libWUtil.la @XCB_CORE_LIBS@

AM_CPPFLAGS = -I$(top_srcdir)/wmlib

EXTRA_DIST = drive-common.sh asan-drive.sh dock-drop-drive.sh dock-orient-drive.sh exec-sn-drive.sh hot-corner-drive.sh ewmh-layout-drive.sh idle-wakeups-drive.sh ipc-drive.sh ipc-verbs-drive.sh layout-drive.sh moveres-drive.sh osd-keychain-drive.sh restart-drift-drive.sh rootmenu-drive.sh roundtrip-drive.sh switchpanel-drive.sh tabgroup-drive.sh titlebar-drive.sh titlebar-dblclick-drive.sh window-menu-drive.sh lock-geometry-drive.sh wsmask-drive.sh workspace-drive.sh xsmp-drive.sh ewmh-client.c sm-listener.c xdnd-source.c lsan-suppressions.txt memcheck-drive.sh memcheck-suppressions.txt mouse-button-drive.sh disable-ws-mouse-drive.sh cycle-workspaces-drive.sh wmsetbg-drive.sh last-workspace-restore-drive.sh iconsize-reflow-drive.sh edge-finder-drive.sh edge-snap-drive.sh snapping-drive.sh restore-iconic-drive.sh show-desktop-drive.sh skip-empty-workspaces-drive.sh sound-drive.sh multiscreen-drive.sh secondary-screen-drive.sh replace-drive.sh shared-edge-resize-drive.sh urgent-click-drive.sh dockapp-drive.sh dockapp-client.c appmenu-drive.sh appmenu-client.c crash-dialog-drive.sh title-fallback-drive.sh mothints-client.c mrs-client.c selreply-client.c urgent-req.c selection-reply-drive.sh mwm-hints-drive.sh leaderless-hidden-drive.sh skip-session-drive.sh capture-drive.sh randr-drive.sh keybind-actions-drive.sh vdesk-phantom-pan-drive.sh wxkb-compose-fallback-drive.sh touch-drive.sh tiny-window-drive.sh tinywin-client.c wtest-drive.sh hint-inspector-drive.sh wmstate-client.c wprefs-reset-drive.sh wprefs-loaddefault-drive.sh xsettings-drive.sh xsettings-client.c findmouse-drive.sh findmouse-client.c cursorhide-drive.sh cursorhide-client.c

EXTRA_DIST += accent-picker-drive.sh accent-picker-client.c
