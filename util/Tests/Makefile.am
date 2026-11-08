AUTOMAKE_OPTIONS = subdir-objects

check_PROGRAMS = check_wmgenmenu check_wmakermsg
TESTS = check_wmgenmenu check_wmakermsg
DRIVE_TESTS =
check_PROGRAMS += wmclipd_cliptest
check_PROGRAMS += check_clip_geom
TESTS += check_clip_geom
check_PROGRAMS += check_wtouch
TESTS += check_wtouch
check_PROGRAMS += check_util_tools
TESTS += check_util_tools
DRIVE_TESTS += wmclipd-clipboard.sh wmclipd-ttl.sh wmclipd-persist.sh wmiv-clipboard-paste.sh \
	wmclipd-asan.sh \
	wmclipd-place.sh \
	wxcopy-wxpaste.sh

AM_CPPFLAGS = $(DFLAGS) -I$(top_srcdir)/WINGs -I$(top_srcdir)/util -I$(top_srcdir)/test @HEADER_SEARCH_PATH@

wmclipd_cliptest_SOURCES = wmclipd_cliptest.c ../clip_common.c ../clip_geom.c ../xcconn.c
wmclipd_cliptest_CFLAGS = @XCB_CFLAGS@
wmclipd_cliptest_LDADD = @XCB_LIBS@ $(top_builddir)/WINGs/libwipc.la

check_clip_geom_SOURCES = check_clip_geom.c ../clip_geom.c
check_clip_geom_CFLAGS = @XCB_CFLAGS@
check_clip_geom_LDADD = @XCB_LIBS@

check_wmgenmenu_SOURCES = check_wmgenmenu.c
check_wmgenmenu_LDADD = $(top_builddir)/WINGs/libWUtil.la @INTLIBS@

check_wmakermsg_SOURCES = check_wmakermsg.c
check_wmakermsg_LDADD = $(top_builddir)/WINGs/libWUtil.la @INTLIBS@

check_wtouch_SOURCES = check_wtouch.c ../wmtouch_core.c ../wmtouch_core.h
check_wtouch_LDADD = $(top_builddir)/WINGs/libWUtil.la @INTLIBS@

check_util_tools_SOURCES = check_util_tools.c
check_util_tools_LDADD = $(top_builddir)/WINGs/libWUtil.la @INTLIBS@

EXTRA_DIST = wmclipd-clipboard.sh wmclipd-ttl.sh wmclipd-persist.sh wmiv-clipboard-paste.sh \
	wmclipd-asan.sh \
	wmclipd-place.sh \
	wxcopy-wxpaste.sh

TESTS_ENVIRONMENT = \
	top_builddir='$(top_builddir)'; export top_builddir; \
	top_srcdir='$(top_srcdir)'; export top_srcdir; \
	builddir='$(builddir)'; export builddir; \
	srcdir='$(srcdir)'; export srcdir;

.PHONY: check-drive

check-drive: $(check_PROGRAMS)
if HAVE_XVFB
	@fail=0; for t in $(DRIVE_TESTS); do \
	  echo "=== util/Tests/$$t ==="; \
	  top_builddir='$(top_builddir)'; export top_builddir; \
	  top_srcdir='$(top_srcdir)'; export top_srcdir; \
	  builddir='$(builddir)'; export builddir; \
	  srcdir='$(srcdir)'; export srcdir; \
	  "$(srcdir)/$$t"; rc=$$?; \
	  case $$rc in \
	    0)  echo "PASS  util/Tests/$$t" ;; \
	    77) echo "SKIP  util/Tests/$$t" ;; \
	    *)  echo "FAIL  util/Tests/$$t (rc=$$rc)"; fail=1 ;; \
	  esac; \
	done; exit $$fail
else
	@echo "Xvfb was not found at configure time, install it and re-run configure"
	@echo "to use the utility integration drivers under util/Tests/"
endif
