#!/bin/sh
#
# option-matrix - compile-check every configure feature toggle in both
# directions. A minimal configuration (every optional feature
# disabled) compiles the #else branches that a normal build never
# sees, a maximal one (every pure-code toggle enabled) compiles the
# default-off code paths. Each variant runs ./configure, make -k so
# every broken file is reported and make check. The original
# configuration is restored and rebuilt at the end, so the tree is
# left usable.
#
# Only toggles that change compiled code are forced. The
# library-detected ones (shape, randr, xsync, shm, res, the image
# decoders) are disabled for the minimal run and left on auto for the
# maximal run, where configure enables them whenever the headers are
# present.

set -u

cd "$(dirname "$0")/.." || exit 2

if [ ! -f config.status ]; then
	echo "option-matrix: the tree is not configured, run ./configure first" >&2
	exit 2
fi

base_args=$(./config.status --config) || exit 2
echo "option-matrix: base configuration: $base_args"

MINIMAL="--disable-wmreplace --disable-animations --disable-mwm-hints \
--disable-xkb-multilingual --disable-virtual-desktop --disable-usermenu \
--disable-boehm-gc --disable-wmlock --disable-xsmp --disable-xdnd \
--disable-shape --disable-randr --disable-xsync --disable-shm \
--disable-res --without-startup-notification --without-xcb-screensaver \
--without-libarchive --without-pipewire --without-libinput \
--disable-png --disable-jpeg --disable-jxl --disable-tiff \
--disable-webp --disable-heif --disable-svg --disable-magick"
MAXIMAL="--enable-wmreplace --enable-xkb-multilingual --enable-virtual-desktop \
--enable-usermenu --enable-boehm-gc --enable-wmlock"

jobs=$(nproc 2>/dev/null) || jobs=1

LOGDIR=$(mktemp -d "${TMPDIR:-/tmp}/option-matrix.XXXXXX") || exit 2
echo "option-matrix: logs in $LOGDIR"

configure_with() {
	extra=$1
	eval "set -- $base_args $extra"
	./configure "$@"
}

fail=0

run_variant() {
	variant=$1
	extra=$2

	echo "option-matrix: $variant configuration"
	if ! configure_with "$extra" >"$LOGDIR/config-$variant.log" 2>&1; then
		echo "FAIL  option matrix [$variant]: configure failed ($LOGDIR/config-$variant.log)"
		fail=1
		return
	fi
	if ! make -k -j "$jobs" >"$LOGDIR/make-$variant.log" 2>&1; then
		echo "FAIL  option matrix [$variant]: build failed ($LOGDIR/make-$variant.log)"
		grep "error:" "$LOGDIR/make-$variant.log" | sort -u | head -10
		fail=1
		return
	fi
	if ! make check >"$LOGDIR/check-$variant.log" 2>&1; then
		echo "FAIL  option matrix [$variant]: make check failed ($LOGDIR/check-$variant.log)"
		grep "^FAIL:" "$LOGDIR/check-$variant.log" | head -10
		fail=1
		return
	fi
	echo "PASS  option matrix [$variant]"
}

run_variant minimal "$MINIMAL"
run_variant maximal "$MAXIMAL"

# restore the original configuration and leave the tree built
if ! configure_with "" >"$LOGDIR/config-restore.log" 2>&1; then
	echo "FAIL  option matrix: could not restore the original configuration ($LOGDIR/config-restore.log)"
	fail=1
elif ! make -j "$jobs" >"$LOGDIR/make-restore.log" 2>&1; then
	echo "FAIL  option matrix: restore build failed ($LOGDIR/make-restore.log)"
	fail=1
fi

exit $fail
