#!/bin/sh
# Verify the XSETTINGS manager end to end: an independent client
# (test/xsettings-client.c) resolves the _XSETTINGS_S0 selection, parses
# the _XSETTINGS_SETTINGS payload on the wire and a GTK and a Qt client
# consume the same values, while an Xft.dpi change restarts wmaker and
# republishes, and a WMGLOBAL font change republishes live. Also checks
# that wmaker yields the selection to another manager and releases it on
# exit.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${XSETTINGS_DRIVE_DISPLAY:-:$(drive_default_display)}
W=800
H=600
RUNTIME=$(mktemp -d /tmp/xsettings-drive.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG="$RUNTIME/wmaker.log"
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
: > "$PIDFILE"

cleanup() {
    while read -r p; do
        [ -n "$p" ] && kill "$p" 2>/dev/null
    done < "$PIDFILE"
    rm -f "$PIDFILE"
    :
}
trap cleanup EXIT

note() { printf '\n=== %s ===\n' "$*" | tee -a "$LOG"; }
fail() { printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; exit 1; }

wait_for() {
    local n=$1; shift
    while [ "$n" -gt 0 ]; do
        "$@" && return 0
        sleep 0.1
        n=$((n - 1))
    done
    return 1
}
pid_gone() { ! kill -0 "$1" 2>/dev/null; }

command -v Xvfb   >/dev/null || { echo "SKIP: Xvfb not found"; exit 77; }
command -v xrdb   >/dev/null || { echo "SKIP: xrdb not found"; exit 77; }
[ -x "$ROOT/src/.libs/wmaker" ] || { echo "SKIP: wmaker not built"; exit 77; }
[ -x "$WMAKERMSG" ] || { echo "SKIP: wmakermsg not built"; exit 77; }
CLIENT="$RUNTIME/xsettings-client"
${CC:-cc} -std=gnu11 "$ROOT/test/xsettings-client.c" -o "$CLIENT" -lX11 >>"$LOG" 2>&1 \
    || { echo "SKIP: could not build test/xsettings-client.c (needs libX11 headers)"; exit 77; }

xclient() { DISPLAY=$DPY "$CLIENT" "$@"; }
msg() {
    LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        DISPLAY=$DPY XDG_RUNTIME_DIR="$RUNTIME" "$WMAKERMSG" "$@"
}
wdwrite() {
    WMAKER_USER_ROOT="$WMHOME" \
    LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        "$ROOT/util/.libs/wdwrite" "$@"
}

mkdir -p "$WMHOME/Defaults"
cat >"$WMHOME/Defaults/WindowMaker" <<EOF
{
}
EOF
export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use, set XSETTINGS_DRIVE_DISPLAY" >&2
    exit 77
fi
note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 ${W}x${H}x24 -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
xvfb_ready() {
    kill -0 "$XVFB_PID" 2>/dev/null || fail "Xvfb exited early"
    DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xvfb_ready || fail "Xvfb failed to start"

note "start wmaker"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
ASAN_OPTIONS="$(drive_asan_options)" \
DISPLAY=$DPY \
"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"

SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    DISPLAY=$DPY XDG_RUNTIME_DIR="$RUNTIME" "$WMAKERMSG" --get-socketpath)
ipc_ready() { msg -q get_version >/dev/null 2>&1; }
wait_for 120 ipc_ready || fail "wmaker did not come up"
sleep 0.3

# --- 1: initial payload -----------------------------------------------------
note "initial payload"
OUT=$(xclient 2>>"$LOG") || fail "no XSETTINGS selection owner or unparseable payload"
have() { printf '%s\n' "$OUT" | grep -Fx "$1" >/dev/null; }

have 'Xft/DPI	integer	98304' || fail "Xft/DPI is not 98304: $(printf '%s\n' "$OUT" | grep 'Xft/DPI')"
have 'Gtk/IconThemeName	string	hicolor' || fail "missing Gtk/IconThemeName"
have 'Net/IconThemeName	string	hicolor' || fail "missing Net/IconThemeName"
# the WMGLOBAL SystemFont key is unset in this home, so WINGs renders
# with its built-in "sans serif" and that is what must be published
have 'Gtk/FontName	string	sans serif' || fail "Gtk/FontName is not the configured system font: $(printf '%s\n' "$OUT" | grep 'Gtk/FontName')"
have 'Xft/HintStyle	string	hintnone' || have 'Xft/HintStyle	string	hintslight' \
    || have 'Xft/HintStyle	string	hintmedium' || have 'Xft/HintStyle	string	hintfull' \
    || fail "missing or malformed Xft/HintStyle"
printf '%s\n' "$OUT" | grep -E 'Xft/Antialias	integer	[01]$' >/dev/null || fail "missing Xft/Antialias"
printf '%s\n' "$OUT" | grep -E 'Xft/Hinting	integer	[01]$' >/dev/null || fail "missing Xft/Hinting"
printf '%s\n' "$OUT" | grep -E 'Xft/RGBA	string	(none|rgb|bgr|vrgb|vbgr)$' >/dev/null || fail "missing Xft/RGBA"
SERIAL0=$(printf '%s\n' "$OUT" | awk -F'\t' '$1 == "serial" {print $2; exit}')
[ -n "$SERIAL0" ] && [ "$SERIAL0" -ge 1 ] || fail "no usable serial in the payload"
note "ok: initial payload with serial $SERIAL0"

# --- 2: Xft.dpi change restarts wmaker and republishes ----------------------
note "Xft.dpi change restarts and republishes"
: > "$RUNTIME/watch.out"
xclient --watch >"$RUNTIME/watch.out" 2>>"$LOG" &
WATCH_PID=$!
echo "$WATCH_PID" >> "$PIDFILE"
sleep 0.3
printf 'Xft.dpi: 144\n' | DISPLAY=$DPY xrdb -merge
dpi_updated() { grep -Fq 'Xft/DPI	integer	147456' "$RUNTIME/watch.out"; }
wait_for 100 dpi_updated || fail "Xft/DPI was not republished after the xrdb change"
# WMScreenReadUIScale() only logs this at startup, so it proves wmaker
# restarted rather than merely refreshing the live payload
dpi_applied() { grep -Fq 'Xft.dpi 144' "$LOG"; }
wait_for 100 dpi_applied || fail "wmaker did not restart after the Xft.dpi change"
wait_for 100 ipc_ready || fail "wmaker did not come back after the Xft.dpi restart"
new_serial() {
    awk -F'\t' '$1 == "serial" {print $2}' "$RUNTIME/watch.out" | tail -1
}
SERIAL0=$(new_serial)
[ -n "$SERIAL0" ] && [ "$SERIAL0" -ge 1 ] || fail "no usable serial in the restarted payload"
kill "$WATCH_PID" 2>/dev/null
wait "$WATCH_PID" 2>/dev/null
note "ok: restarted and republished with serial $SERIAL0"

# --- 3: live font change through the WMGLOBAL domain ------------------------
note "live Gtk/FontName republish"
wdwrite WMGLOBAL SystemFont '"Foo Sans:bold:pixelsize=20"' >>"$LOG" 2>&1 \
    || fail "wdwrite WMGLOBAL failed"
msg -q reconfigure >/dev/null || fail "reconfigure failed"
# pixelsize=20 at the live DPI 144 is 20*72/144 = 10pt, the conversion
# must track the published DPI
font_updated() {
    xclient 2>/dev/null | grep -Fq 'Gtk/FontName	string	Foo Sans Bold 10'
}
wait_for 50 font_updated || fail "Gtk/FontName was not republished after the WMGLOBAL change"
SERIAL1=$(xclient --serial 2>/dev/null)
[ -n "$SERIAL1" ] && [ "$SERIAL1" -gt "$SERIAL0" ] || fail "serial did not advance after the font change"
have_now() { xclient 2>/dev/null | grep -Fx "$1" >/dev/null; }
have_now 'Xft/DPI	integer	147456' || fail "Xft/DPI regressed after the font change"
note "ok: font republished with serial $SERIAL1"

# --- 4: GTK follows the published values ------------------------------------
note "GTK oracle"
command -v python3 >/dev/null || { echo "SKIP: python3 not found"; exit 77; }
python3 -c 'import gi; gi.require_version("Gtk", "3.0")' 2>/dev/null \
    || { echo "SKIP: python3-gi (GTK 3) not available"; exit 77; }
cat >"$RUNTIME/gtk-oracle.py" <<'EOF'
import sys
import gi
gi.require_version("Gtk", "3.0")
from gi.repository import Gtk

Gtk.init_check()
settings = Gtk.Settings.get_default()
dpi = settings.get_property("gtk-xft-dpi")
font = settings.get_property("gtk-font-name")
icons = settings.get_property("gtk-icon-theme-name")
print("gtk-xft-dpi=%s font=%s icons=%s" % (dpi, font, icons))
if dpi != 144 * 1024:  # the wire value is plain-DPI * 1024
    sys.exit(1)
if font != "Foo Sans Bold 10":
    sys.exit(1)
if icons != "hicolor":
    sys.exit(1)
EOF
DISPLAY=$DPY python3 "$RUNTIME/gtk-oracle.py" >>"$LOG" 2>&1 \
    || fail "GTK did not pick up the published settings"
note "ok: GTK follows Xft/DPI, Gtk/FontName and the icon theme"

# --- 5: Qt follows the published DPI ----------------------------------------
note "Qt oracle"
python3 -c 'import PyQt5.QtWidgets' 2>/dev/null \
    || { echo "SKIP: PyQt5 not available"; exit 77; }
cat >"$RUNTIME/qt-oracle.py" <<'EOF'
import sys
from PyQt5.QtWidgets import QApplication

app = QApplication([])
dpi = app.primaryScreen().logicalDotsPerInch()
print("logicalDotsPerInch=%s" % dpi)
sys.exit(0 if abs(dpi - 144.0) < 0.5 else 1)
EOF
DISPLAY=$DPY python3 "$RUNTIME/qt-oracle.py" >>"$LOG" 2>&1 \
    || fail "Qt did not pick up the published DPI"
note "ok: Qt follows Xft/DPI"

# --- 6: another manager takes the selection ---------------------------------
note "yield to another manager"
xclient --steal >>"$LOG" 2>&1 || fail "client could not steal the selection"
wait_for 20 grep -q 'another XSETTINGS manager is taking over' "$LOG" \
    || fail "wmaker did not notice the lost XSETTINGS selection"
msg -q get_version >/dev/null || fail "wmaker wedged after losing the selection"
note "ok: wmaker yielded and stayed alive"

# --- 7: the selection is released on exit -----------------------------------
note "selection released on exit"
msg -q exit >/dev/null 2>&1
wait_for 100 pid_gone "$WMAKER_PID" || fail "wmaker did not exit"
if xclient --serial >/dev/null 2>&1; then
    fail "_XSETTINGS_S0 still has an owner after wmaker exited"
fi
note "ok: selection released"

printf 'PASS: xsettings-drive\n'
exit 0
