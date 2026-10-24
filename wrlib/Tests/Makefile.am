## automake input file for wrlib

AUTOMAKE_OPTIONS =

# interactive demo programs, built on demand only (e.g. `make testdraw`),
# standalone xcb clients like wxcopy/wxpaste (no libWINGs helpers)
EXTRA_PROGRAMS = testdraw testgrad testrot view bench_image

# libFuzzer harness for the libwraster decoders, built on demand only
# (e.g. `make fuzz` or `make fuzz_wrlib`) and only when the configured
# compiler supports libFuzzer (see WINGs/Tests/Makefile.am for the recipe;
# a magic-prefix selector byte steers the payload into each format's
# identFile() branch, so the harness stays on the public API)
if HAVE_FUZZER
EXTRA_PROGRAMS += fuzz_wrlib

fuzz_wrlib_SOURCES = fuzz_wrlib.c
fuzz_wrlib_LDADD = $(top_builddir)/wrlib/libwraster.la \
	$(top_builddir)/WINGs/libWUtil.la
fuzz_wrlib_LDFLAGS = -fsanitize=fuzzer
else
fuzz_wrlib:
	@echo "fuzz_wrlib requires a compiler with libFuzzer support (-fsanitize=fuzzer)." >&2
	@echo "Reconfigure with CC=clang and -fsanitize=fuzzer-no-link in CFLAGS." >&2
	@exit 1
endif

# automated decoder tests, run by `make check`
check_PROGRAMS = check_load check_save check_convolve check_image check_palette check_xside
TESTS = $(check_PROGRAMS)
AM_TESTS_ENVIRONMENT = srcdir='$(srcdir)'; export srcdir;

check_load_SOURCES = check_load.c
check_load_LDADD = $(top_builddir)/wrlib/libwraster.la \
	$(top_builddir)/WINGs/libWUtil.la

check_save_SOURCES = check_save.c
check_save_LDADD = $(top_builddir)/wrlib/libwraster.la \
	$(top_builddir)/WINGs/libWUtil.la

$(top_builddir)/WINGs/libWUtil.la:
	$(MAKE) -C $(top_builddir)/WINGs libWUtil.la

check_convolve_SOURCES = check_convolve.c
check_convolve_LDADD = $(top_builddir)/wrlib/libwraster.la

check_image_SOURCES = check_image.c
check_image_LDADD = $(top_builddir)/wrlib/libwraster.la @WRASTER_CAIRO_LIBS@

check_palette_SOURCES = check_palette.c
check_palette_LDADD = $(top_builddir)/wrlib/libwraster.la

# round-trips RImage -> Pixmap -> RImage so it needs a server, exits 77
# (SKIP) without one so make check stays X-free
check_xside_SOURCES = check_xside.c
check_xside_LDADD = $(top_builddir)/wrlib/libwraster.la @WRASTER_XCB_LIBS@

EXTRA_DIST = test.png tile.xpm ballot_box.xpm \
	pnm-p1.pbm pnm-p2.pgm pnm-p3.ppm \
	pnm-p4.pbm pnm-p5.pgm pnm-p6.ppm \
	heif-test.avif heif-test.heic

AM_CPPFLAGS = -I$(top_srcdir)/wrlib -I$(top_srcdir)/WINGs -I$(srcdir)/.. -I$(top_srcdir)/test $(DFLAGS) @WRASTER_CAIRO_CFLAGS@ @HEADER_SEARCH_PATH@

LIBLIST = $(top_builddir)/wrlib/libwraster.la @WRASTER_XCB_LIBS@

testdraw_SOURCES = testdraw.c
testdraw_LDADD = $(LIBLIST)

testgrad_SOURCES = testgrad.c
testgrad_LDADD = $(LIBLIST)

testrot_SOURCES = testrot.c
testrot_LDADD = $(LIBLIST)

view_SOURCES= view.c
view_LDADD = $(LIBLIST)

# pixel-loop micro-benchmark, run by `make bench` from the top level,
# never by `make check` so the test suite stays fast and deterministic
bench_image_SOURCES = bench_image.c
bench_image_LDADD = $(top_builddir)/wrlib/libwraster.la

bench: bench_image
	@BENCH_SIZES=$(BENCH_SIZES) ./bench_image

.PHONY: bench
