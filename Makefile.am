## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS =

BUILT_SOURCES = config-paths.h

DISTCLEANFILES = config-paths.h

config.h: config-paths.h

config-paths.h: Makefile
	@echo "Generating $@"
	@echo '/* this is a generated file - do not edit */' > $@
	@echo '' >> $@
	@echo '/* define where the translations are stored */' >> $@
	@echo '#define LOCALEDIR "$(localedir)"' >> $@
	@echo '' >> $@
	@echo '/* define an extra path for pixmaps */' >> $@
	@echo '#define PIXMAPDIR "$(pixmapdir)"' >> $@
	@echo '' >> $@
	@echo '/* where shared data is stored */' >> $@
	@echo '#define PKGDATADIR "$(datadir)/$(PACKAGE_TARNAME)"' >> $@
	@echo '' >> $@
	@echo '/* where the global defaults are stored */' >> $@
	@echo '#define PKGCONFDIR "$(pkgconfdir)"' >> $@
	@echo '' >> $@
	@echo '/* where the user data root is located */' >> $@
	@echo '#define GSUSER_SUBDIR "GNUstep"' >> $@
	@echo '' >> $@
	@echo '/* where the user defaults are stored */' >> $@
	@echo '#define DEFAULTS_SUBDIR "Defaults"' >> $@
	@echo '' >> $@
	@echo '/* where the user data are stored */' >> $@
	@echo '#define USERDATA_SUBDIR "Library"' >> $@

ACLOCAL_AMFLAGS = -I m4

# Improve coverage in 'make distcheck' by checking that translations work
AM_DISTCHECK_CONFIGURE_FLAGS = --enable-silent-rules LINGUAS='*'


SUBDIRS = wrlib WINGs src util po WindowMaker wmlib WPrefs.app doc
DIST_SUBDIRS = $(SUBDIRS) test

EXTRA_DIST = TODO BUGS BUGFORM FAQ INSTALL \
	INSTALL-WMAKER README.i18n \
	README COPYING.WTFPL autogen.sh \
	script/checkpatch.pl script/update-changelog.pl \
	script/check-cmdline-options-doc.sh \
	script/check-manifest.sh \
	script/check-translation-sources.sh \
	script/option-matrix.sh \
	script/generate-html-from-man.sh \
	script/generate-mapfile-from-header.sh \
	script/generate-po-from-template.sh \
	script/generate-txt-from-texi.sh \
	script/replace-ac-keywords.sh \
	script/replace-generated-content.sh


.PHONY:	coverage-reset coverage

if USE_LCOV
coverage-reset:
	find . -type f -name '*.gcda' -exec rm -f '{}' ';'
	lcov --directory . --zerocounters

coverage:
	rm -rf coverage @lcov_output_directory@
	-mkdir -p coverage @lcov_output_directory@
	lcov --compat-libtool --capture --directory . --output-file coverage/coverage.info
	genhtml --output-directory @lcov_output_directory@ coverage/coverage.info
endif

.PHONY:	analyze

ANALYZE_DIRS = src WINGs wrlib util WPrefs.app

analyze:
	@if test -f "$(top_builddir)/compile_commands.json" && command -v run-clang-tidy > /dev/null 2>&1 ; then \
	  echo "Running run-clang-tidy against $(top_builddir)/compile_commands.json" ; \
	  run-clang-tidy -quiet -p "$(top_builddir)" $(ANALYZE_DIRS) ; \
	else \
	  echo "compile_commands.json or run-clang-tidy not found, falling back to scan-build" ; \
	  scan-build --status-bugs $(MAKE) $(AM_MAKEFLAGS) clean all ; \
	fi

 .PHONY:	checkpatch

# Kernel-only conventions that do not apply to Window Maker, plus categories
# that fight the existing tree style (block-comment layout, line length) or
# are near-pure false positives in C ("Window window" repeated-word).
# MACRO_ARG_UNUSED: X-macro projection tables intentionally consume a
# subset of the columns at each expansion site.
# VOLATILE: sig_atomic_t handler state and setjmp/longjmp locals need the
# qualifier; this is not shared-memory synchronization.
CHECKPATCH_IGNORE = NEW_TYPEDEFS,SPDX_LICENSE_TAG,COMPLEX_MACRO,PREFER_KERNEL_TYPES,ARRAY_SIZE,EMAIL_SUBJECT,FILE_PATH_CHANGES,GIT_COMMIT_ID,MISSING_SIGN_OFF,BAD_SIGN_OFF,NO_AUTHOR_SIGN_OFF,BAD_REPORTED_BY_LINK,BAD_FIXES_TAG,STRCPY,STRNCPY,SPRINTF,SSCANF_TO_KSTRTO,COMMIT_MESSAGE,COMMIT_LOG_LONG_LINE,EMBEDDED_FUNCTION_NAME,BLOCK_COMMENT_STYLE,DEEP_INDENTATION,LONG_LINE,LONG_LINE_STRING,LONG_LINE_COMMENT,REPEATED_WORD,SPLIT_STRING,MACRO_ARG_UNUSED,PREFER_DEFINED_ATTRIBUTE_MACRO,VOLATILE
# License texts are verbatim documents, so their diff sections are removed
# before linting rather than adding kernel-only checks to CHECKPATCH_IGNORE.
CHECKPATCH_FILTER_LICENSE = $(top_srcdir)/script/checkpatch-filter-license.pl
SPELLING_URL = https://raw.githubusercontent.com/torvalds/linux/master/scripts/spelling.txt

checkpatch:
	@if test ! -f "$(top_srcdir)/script/spelling.txt" ; then \
	  echo "Fetching spelling.txt (one-time)" ; \
	  if command -v curl > /dev/null 2>&1 ; then \
	    curl -sfL "$(SPELLING_URL)" -o "$(top_srcdir)/script/spelling.txt" ; \
	  elif command -v wget > /dev/null 2>&1 ; then \
	    wget -qO "$(top_srcdir)/script/spelling.txt" "$(SPELLING_URL)" ; \
	  fi ; \
	  test -s "$(top_srcdir)/script/spelling.txt" || \
	    echo "Could not fetch spelling.txt, continuing without typo checks" ; \
	fi
	@test -f "$(top_srcdir)/script/const_structs.checkpatch" || \
	  : > "$(top_srcdir)/script/const_structs.checkpatch"
	@range='$(RANGE)' ; \
	if test -z "$$range" ; then \
	  if test -n "`git -C '$(top_srcdir)' diff HEAD`" ; then \
	    echo "checkpatch: working tree changes" ; \
	    git -C "$(top_srcdir)" diff HEAD | \
	      "$(CHECKPATCH_FILTER_LICENSE)" | \
	      "$(top_srcdir)/script/checkpatch.pl" --no-tree --show-types \
	        --ignore $(CHECKPATCH_IGNORE) - ; \
	    exit $$? ; \
	  fi ; \
	  range="master..HEAD" ; \
	fi ; \
	rc=0 ; \
	for c in `git -C "$(top_srcdir)" rev-list --reverse "$$range"` ; do \
	  s=`git -C "$(top_srcdir)" log -1 --format='%h %s' "$$c"` ; \
	  echo "=== checkpatch: $$s" ; \
	  git -C "$(top_srcdir)" format-patch -1 --stdout "$$c" | \
	    "$(CHECKPATCH_FILTER_LICENSE)" | \
	    "$(top_srcdir)/script/checkpatch.pl" --no-tree --show-types \
	      --ignore $(CHECKPATCH_IGNORE) - || rc=1 ; \
	done ; \
 	exit $$rc
 
################################################################################

# Section for the I18N processing
################################################################################

# make update-lang PO=<lang>
# ==========================
# Update the PO files against the POT file in all the translation sub-directories
#
# We do not use an automatic recursive target from Automake (AM_EXTRA_RECURSIVE_TARGETS)
# because we want to check only once that the variable PO was defined; the added bonus
# being that we do not process all directories but only the related ones, which is
# faster and a lot less verbose

update-lang:
if HAVE_XGETTEXT
	@if echo "$(PO)" | grep -v '^[a-z][a-z]\(_[A-Z][A-Z]\)\?$$' > /dev/null ; then \
	  echo "Error: invalid value \"$(PO)\" for update-lang, use PO=<lang>" >&2 ; exit 1 ; \
	fi ; \
	for subdir in $(SUBDIRS_PO); do \
	  ($(am__cd) $$subdir && $(MAKE) $(AM_MAKEFLAGS) update-lang || exit $$?); \
	done

SUBDIRS_PO = wrlib/po WINGs/po po util/po WPrefs.app/po
else
	@echo "Error: the program 'xgettext' was not found by configure, it is mandatory for this operation" >&2 ; exit 1
endif

.PHONY: update-lang

################################################################################
# Section related to generating HTML content for the website
################################################################################

if WITH_WEB_REPO

SUBDIRS_WEB = doc

website:
	@for subdir in $(SUBDIRS_WEB); do \
	  ($(am__cd) $$subdir && $(MAKE) $(AM_MAKEFLAGS) website || exit $$?); \
	done

endif

.PHONY: website

################################################################################
# Section for the automated checks
################################################################################

# Create a 'silent-rule' for our make check the same way automake does
AM_V_CHKOPTS = $(am__v_CHKOPTS_$(V))
am__v_CHKOPTS_  = $(am__v_CHKOPTS_$(AM_DEFAULT_VERBOSITY))
am__v_CHKOPTS_0 = @echo "  CHK      $@" ;
am__v_CHKOPTS_1 =

check-local: configure-documentation check-manifest check-pkgconfig

check-manifest:
	$(AM_V_CHKOPTS)$(top_srcdir)/script/check-manifest.sh $(top_srcdir) $(top_builddir)

CHECK_PCFILES = wrlib/wrlib.pc WINGs/WINGs.pc WINGs/WUtil.pc wmlib/wmlib.pc

check-pkgconfig: $(CHECK_PCFILES)
	$(AM_V_CHKOPTS)ok=true ; \
	pc_path="$(abs_top_builddir)/wrlib:$(abs_top_builddir)/WINGs:$(abs_top_builddir)/wmlib$${PKG_CONFIG_PATH:+:$${PKG_CONFIG_PATH}}" ; \
	for pc in $(CHECK_PCFILES) ; do \
		name=`basename "$$pc" .pc` ; \
		PKG_CONFIG_PATH="$$pc_path" $(PKG_CONFIG) --validate "$$name" || ok=false ; \
		PKG_CONFIG_PATH="$$pc_path" $(PKG_CONFIG) --static --libs "$$name" >/dev/null || ok=false ; \
	done ; \
	$$ok

configure-documentation:
	$(AM_V_CHKOPTS)$(top_srcdir)/script/check-cmdline-options-doc.sh \
		--program "$(top_srcdir)/configure" --text-doc "$(top_srcdir)/INSTALL-WMAKER" \
		--ignore-prg 'with-PACKAGE,without-PACKAGE # only template names from Autoconf' \
		--ignore-prg 'program-prefix,program-suffix,program-transform-name # in INSTALL' \
		--ignore-prg 'version,quiet,srcdir,build,host,cache-file,no-create # in INSTALL' \
		--ignore-prg 'enable-silent-rules,disable-silent-rules # should be in INSTALL' \
		--ignore-prg 'enable-dependency-tracking,disable-dependency-tracking # in INSTALL' \
		--ignore-prg 'enable-shared,enable-static # should be in INSTALL' \
		--ignore-prg 'disable-option-checking,enable-fast-install # should be in INSTALL' \
		--ignore-prg 'disable-libtool-lock,with-gnu-ld,with-sysroot # for libtool' \
		--ignore-prg 'runstatedir #new in autoconf 2.70, backported in Debian' \
		--ignore-prg-optional 'with-pic,with-aix-soname,enable-pic,enable-aix-soname #libtool 2.5 renamed --with- to --enable-' \
		--ignore-prg-optional 'enable-cxx-stdlib #new in libtool 2.6.0'

.PHONY:	configure-documentation check-manifest check-pkgconfig maintainer-check check-drive check-options check-cppcheck memcheck asan run bench fuzz

# Integration drivers under test/ start a private X server and run the
# just-built window manager inside it. They are deliberately not part
# of `make check` (which stays X-free and fast), this target runs them
# on demand. Each driver still does its own per-tool `command -v` check
# and exits 77 (SKIP) when something it needs is missing, the
# AM_CONDITIONAL only short-circuits the obvious "no Xvfb at all" case.
DRIVE_TESTS = test/dock-orient-drive.sh test/ipc-drive.sh test/ipc-verbs-drive.sh test/ewmh-layout-drive.sh test/hot-corner-drive.sh test/osd-keychain-drive.sh test/moveres-drive.sh test/mwm-hints-drive.sh test/restart-drift-drive.sh test/rootmenu-drive.sh test/tabgroup-drive.sh test/titlebar-drive.sh test/titlebar-dblclick-drive.sh test/window-menu-drive.sh test/lock-geometry-drive.sh test/dock-drop-drive.sh test/xsmp-drive.sh test/idle-wakeups-drive.sh test/roundtrip-drive.sh test/layout-drive.sh test/switchpanel-drive.sh test/wsmask-drive.sh test/workspace-drive.sh test/mouse-button-drive.sh test/disable-ws-mouse-drive.sh test/cycle-workspaces-drive.sh test/wmsetbg-drive.sh test/last-workspace-restore-drive.sh test/iconsize-reflow-drive.sh test/edge-finder-drive.sh test/edge-snap-drive.sh test/snapping-drive.sh test/restore-iconic-drive.sh test/show-desktop-drive.sh test/skip-empty-workspaces-drive.sh test/sound-drive.sh test/multiscreen-drive.sh test/replace-drive.sh test/shared-edge-resize-drive.sh test/urgent-click-drive.sh test/dockapp-drive.sh test/appmenu-drive.sh test/crash-dialog-drive.sh test/title-fallback-drive.sh test/leaderless-hidden-drive.sh test/skip-session-drive.sh test/hint-inspector-drive.sh test/capture-drive.sh test/randr-drive.sh test/keybind-actions-drive.sh test/vdesk-phantom-pan-drive.sh test/wxkb-compose-fallback-drive.sh test/touch-drive.sh test/tiny-window-drive.sh test/wtest-drive.sh test/wprefs-reset-drive.sh test/wprefs-loaddefault-drive.sh test/selection-reply-drive.sh test/findmouse-drive.sh test/xsettings-drive.sh
DRIVE_SUBDIRS = util/Tests

# A wedged driver holding an X grab must fail the sweep instead of hanging
# it. GNU timeout runs the driver in its own process group and signals the
# whole group, so the private Xvfb and wmaker die with it, and -k escalates
# to SIGKILL if a driver ignores SIGTERM. Drivers source
# test/drive-common.sh, which routes the signal through their EXIT cleanup.
# 900s is generous for the slowest driver (roundtrip self-caps at 360s).
DRIVE_TIMEOUT ?= 900

# wmaker spawns helper tools such as wmsetbg by name, so the freshly
# built binaries must win the PATH lookup over any installed copy
DRIVE_PATH = $(abs_top_builddir)/src/.libs:$(abs_top_builddir)/util/.libs:$(abs_top_builddir)/WPrefs.app/.libs

check-drive:
if HAVE_XVFB
	@fail=0; \
	if command -v timeout >/dev/null 2>&1; then \
	  DRIVE_TO="timeout -k 10 $(DRIVE_TIMEOUT)"; \
	else \
	  echo "NOTE: timeout(1) not found, drivers run without a hang guard"; \
	  DRIVE_TO=; \
	fi; \
	for t in $(DRIVE_TESTS); do \
	  echo "=== $$t ==="; \
	  PATH="$(DRIVE_PATH):$$PATH" $$DRIVE_TO "$(top_srcdir)/$$t"; rc=$$?; \
	  case $$rc in \
	    0)   echo "PASS  $$t" ;; \
	    77)  echo "SKIP  $$t" ;; \
	    124) echo "FAIL  $$t (timed out after $(DRIVE_TIMEOUT)s)"; fail=1 ;; \
	    *)   echo "FAIL  $$t (rc=$$rc)"; fail=1 ;; \
	  esac; \
	done; \
	for d in $(DRIVE_SUBDIRS); do \
	  echo "=== $$d/check-drive ==="; \
	  PATH="$(DRIVE_PATH):$$PATH" $$DRIVE_TO $(MAKE) -C "$$d" check-drive; rc=$$?; \
	  case $$rc in \
	    0)   echo "PASS  $$d/check-drive" ;; \
	    124) echo "FAIL  $$d/check-drive (timed out after $(DRIVE_TIMEOUT)s)"; fail=1 ;; \
	    *)   echo "FAIL  $$d/check-drive (rc=$$rc)"; fail=1 ;; \
	  esac; \
	done; exit $$fail
else
	@echo "Xvfb was not found at configure time, install it and re-run configure"
	@echo "to use the integration drivers under test/"
endif

# cppcheck gate over the analyzer-relevant sources, on demand like
# check-drive. test/run-cppcheck.sh exits 77 when cppcheck is missing
# and nonzero when a finding survives test/cppcheck-suppressions.
check-cppcheck:
	@$(top_srcdir)/test/run-cppcheck.sh; rc=$$?; \
	case $$rc in \
	  0)  echo "PASS  check-cppcheck" ;; \
	  77) echo "SKIP  check-cppcheck (cppcheck not installed)" ;; \
	  *)  echo "FAIL  check-cppcheck (rc=$$rc)" ;; \
	esac; exit $$rc

# run wmaker under Valgrind on a private X server and fail on the first
# unsuppressed memory error or leak, on demand like check-drive.
# test/memcheck-drive.sh exits 77 when valgrind or the X-drive tools are
# missing, 99 when Valgrind reports something. The process-lifetime
# caches that cannot be released are listed in
# test/memcheck-suppressions.txt.
memcheck:
	@$(top_srcdir)/test/memcheck-drive.sh; rc=$$?; \
	case $$rc in \
	  0)  echo "PASS  memcheck" ;; \
	  77) echo "SKIP  memcheck (valgrind or drive tools not installed)" ;; \
	  *)  echo "FAIL  memcheck (rc=$$rc)" ;; \
	esac; exit $$rc

# run the feature drivers and the aggregate sanitizer drive against an
# --enable-asan build. Valgrind and ASan cannot share a build, so this is
# separate from 'make memcheck'. WM_ASAN_STRICT makes the drivers abort on
# the first sanitizer report; WM_ASAN_LOGDIR collects those reports in one
# place so a driver cannot pass just because its assertions still ran.
# Gated on the configure flags so a normal build says so instead of failing;
# asan-drive.sh needs the nested Xephyr server 'make run' uses.
asan:
if HAVE_ASAN
if HAVE_XEPHYR
	@asanlog=$$(mktemp -d /tmp/wmaker-asan-suite.XXXXXX) || exit 1; \
	ASAN_OPTIONS="abort_on_error=1:halt_on_error=1:detect_leaks=0:log_path=$$asanlog/asan" \
	WM_ASAN_STRICT=1 WM_ASAN_LOGDIR="$$asanlog" $(MAKE) check-drive; \
	drive_rc=$$?; \
	if grep -rqE 'ERROR: AddressSanitizer|ERROR: LeakSanitizer|runtime error:' "$$asanlog"; then \
	  echo "=== sanitizer reports from the feature drives ==="; \
	  grep -rHnE 'ERROR: AddressSanitizer|ERROR: LeakSanitizer|runtime error:' "$$asanlog"; \
	  drive_rc=1; \
	fi; \
	$(top_srcdir)/test/asan-drive.sh; \
	asan_rc=$$?; \
	rm -rf "$$asanlog"; \
	if [ $$drive_rc -eq 0 ] && [ $$asan_rc -eq 0 ]; then \
	  echo "PASS  asan"; exit 0; \
	elif [ $$drive_rc -eq 77 ] && [ $$asan_rc -eq 77 ]; then \
	  echo "SKIP  asan (drive tools not installed)"; exit 77; \
	else \
	  echo "FAIL  asan (feature drives rc=$$drive_rc, asan-drive rc=$$asan_rc)"; exit 1; \
	fi
else
	@echo "Xephyr was not found at configure time, install xserver-xephyr"
	@echo "and re-run configure to use 'make asan'"
endif
else
	@echo "this build was not configured with --enable-asan"
	@echo "re-run configure with --enable-asan to use 'make asan'"
endif

# compile-verify every configure feature toggle in both directions:
# a minimal configuration with every optional feature disabled
# compiles the #else branches the normal build never sees, a maximal
# one turns the default-off code paths on. Each variant configures,
# builds with -k so every broken file is reported, and runs make
# check. The original configuration is restored and rebuilt at the
# end, so this is safe to run on a working tree at any time. Requires
# a configured tree, see script/option-matrix.sh for the toggle list.
check-options:
	@$(top_srcdir)/script/option-matrix.sh; rc=$$?; \
	case $$rc in \
	  0)  echo "PASS  check-options" ;; \
	  2)  echo "SKIP  check-options (tree not configured)" ;; \
	  *)  echo "FAIL  check-options (rc=$$rc)" ;; \
	esac; exit $$rc

# Print the wrlib pixel-loop micro-benchmark, the cold-boot reply
# count and per-managed-window cost from xtrace, and the idle and
# pointer-motion event-loop wakeup rates. Informational only, the
# pass/fail thresholds on the same scripts run under check-drive.
# BENCH_SIZES=small skips the 1080p row.
bench:
	@echo '################ wrlib pixel ops ################'
	@$(MAKE) -C wrlib/Tests bench
	@echo
	@echo '################ cold-boot round-trips ################'
	@BENCH=1 "$(top_srcdir)/test/roundtrip-drive.sh" || true
	@echo
	@echo '################ idle wakeups ################'
	@BENCH=1 "$(top_srcdir)/test/idle-wakeups-drive.sh" || true

# Build the on-demand libFuzzer harnesses. They need a compiler with
# libFuzzer support (Clang); the configure probe gates this target.
fuzz:
if HAVE_FUZZER
	$(MAKE) -C wrlib libwraster.la
	$(MAKE) -C WINGs libWUtil.la libWINGs.la
	$(MAKE) -C wrlib/Tests fuzz_wrlib
	$(MAKE) -C WINGs/Tests fuzz_proplist fuzz_wxprop
	@echo "Fuzz harnesses built. Run them with a corpus directory, e.g.:"
	@echo "  wrlib/Tests/fuzz_wrlib corpus/ -max_total_time=600"
else
	@echo "Fuzz harnesses require a compiler with libFuzzer support (-fsanitize=fuzzer)." >&2
	@echo "Reconfigure with CC=clang and -fsanitize=fuzzer-no-link in CFLAGS." >&2
	@exit 1
endif

# Launch the just-built Window Maker inside a Xephyr window for visual
# inspection. Passes RUN_DISPLAY, RUN_GEOMETRY, and RUN_CLIENTS through
# to the launch script. Ctrl-C (or closing the Xephyr window) stops the
# session and cleans up the isolated runtime directory.
run:
if HAVE_XEPHYR
	@"$(top_srcdir)/test/run-xephyr.sh"
else
	@echo "Xephyr was not found at configure time, install xserver-xephyr"
	@echo "and re-run configure to use 'make run'"
endif

# Regenerate every derived file the build knows about, then fail if any
# tracked file changed (a generated file was committed, or its .in/.texi
# source drifted without regeneration) or if the build left an
# untracked, un-ignored artefact under a subdirectory (a .gitignore
# gap). Run from a clean working tree before tagging a release.
maintainer-check:
	@$(MAKE) $(AM_MAKEFLAGS) all
	$(AM_V_at)if test ! -d "$(top_srcdir)/.git" ; then \
		echo "maintainer-check: not a git checkout, skipping" ; \
		exit 0 ; \
	fi ; \
	if ! git -C "$(top_srcdir)" diff --quiet ; then \
		echo "error: regeneration changed tracked files:" >&2 ; \
		git -C "$(top_srcdir)" diff --name-only >&2 ; \
		exit 1 ; \
	fi ; \
	u=$$(git -C "$(top_srcdir)" ls-files --others --exclude-standard \
		| grep '/' || true) ; \
	if test -n "$$u" ; then \
		echo "error: build left untracked, un-ignored artefacts:" >&2 ; \
		echo "$$u" >&2 ; \
		echo "(add to .gitignore or CLEANFILES)" >&2 ; \
		exit 1 ; \
	fi ; \
	echo "maintainer-check: ok"
