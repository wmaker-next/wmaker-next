## automake input file for WINGs - Headers subdir

AUTOMAKE_OPTIONS =

# is this a kluge? if so, how should i do it?
includedir = @includedir@/WINGs

BUILT_SOURCES = WINGsP.h
CLEANFILES = WINGsP.h
include_HEADERS = WINGs.h WUtil.h WXCore.h WXProp.h WXHints.h WIPC.h
nodist_include_HEADERS = WINGsP.h
EXTRA_DIST = WINGsP.h.in

# nothing left to substitute now that Pango is unconditional, but the
# generated header stays a build product so it lands in $(builddir) for
# VPATH builds and is not shipped in the dist tarball twice
WINGsP.h: WINGsP.h.in
	$(AM_V_GEN)cp $< $@
