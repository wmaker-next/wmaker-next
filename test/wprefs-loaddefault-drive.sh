#!/bin/sh
# Drive the WPrefs menu editor "Load Default" button: seed a custom root
# menu through the WMRootMenu domain, open the Applications Menu
# Definition panel, click Load Default, confirm the alert, save, and
# assert the seeded menu file now carries the shipped default content
# while the WMRootMenu domain pointer is left untouched.
#
# Harness conventions as in wprefs-reset-drive.sh: private XDG_RUNTIME_DIR,
# bounded readiness polls, background processes killed by recorded PID.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${WPREFS_LOADDEFAULT_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/wmaker-wprefs-loaddefault.XXXXXX)
WMHOME="$RUNTIME/home"
DEFS="$WMHOME/Defaults"
MENUDIR="$WMHOME/Library/WindowMaker"
PIDFILE="$RUNTIME/pids"
LOG=${WPREFS_LOADDEFAULT_DRIVE_LOG:-/tmp/wprefs-loaddefault-drive.log}
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"

PASSED=0
FAILED=0
W=
A=

: > "$PIDFILE"
: > "$LOG"

cleanup() {
	while read -r p; do
		[ -n "$p" ] && kill "$p" 2>/dev/null
	done < "$PIDFILE"
	rm -rf "$RUNTIME"
}
trap cleanup EXIT

note() { printf '\n=== %s ===\n' "$*" | tee -a "$LOG"; }
fail() { printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; FAILED=$((FAILED + 1)); }
ok()   { printf 'PASS: %s\n' "$*" | tee -a "$LOG"; PASSED=$((PASSED + 1)); }
fatal() { printf 'FATAL: %s\n' "$*" | tee -a "$LOG"; exit 1; }

check() {
	check_desc=$1
	shift
	if "$@"; then
		ok "$check_desc"
	else
		fail "$check_desc"
	fi
}

wait_for() {
	wf_n=$1
	shift
	while [ "$wf_n" -gt 0 ]; do
		"$@" && return 0
		sleep 0.1
		wf_n=$((wf_n - 1))
	done
	return 1
}

pid_gone() { ! kill -0 "$1" 2>/dev/null; }
lacks() {
	if grep -q "$@"; then
		return 1
	fi
	return 0
}

xdo() {
	{ DISPLAY=$DPY xdotool "$@" 2>&1 1>&3 \
		| grep -v '^XGetWindowProperty\[_NET_WM_DESKTOP\] failed' >&2; } 3>&1
}

for t in Xvfb xdotool; do
	command -v "$t" >/dev/null 2>&1 || { echo "SKIP: $t not installed"; exit 77; }
done
[ -x "$ROOT/src/.libs/wmaker" ] || { echo "SKIP: wmaker not built"; exit 77; }
[ -x "$ROOT/WPrefs.app/.libs/WPrefs" ] || { echo "SKIP: WPrefs not built"; exit 77; }
# the Load Default action reads the shipped default menu through the
# compiled-in resource path
[ -r /usr/share/WindowMaker/plmenu ] || { echo "SKIP: shipped plmenu not installed"; exit 77; }

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY already in use, set WPREFS_LOADDEFAULT_DRIVE_DISPLAY" >&2
	exit 77
fi

note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 1024x768x24 -ac -noreset >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
xvfb_ready() {
	kill -0 "$XVFB_PID" 2>/dev/null || return 1
	DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xvfb_ready || fatal "Xvfb failed to start"

note "seed a custom root menu"
mkdir -p "$DEFS" "$MENUDIR"
printf '{ }\n' > "$DEFS/WindowMaker"
# WMRootMenu points at an absolute, writable menu file so the editor
# edits that file (the common referenced-file layout)
printf '"%s"\n' "$MENUDIR/menu" > "$DEFS/WMRootMenu"
cat > "$MENUDIR/menu" <<'EOS'
("Applications",
 ("Seeded Probe Entry", "EXEC", "true")
)
EOS

export WMAKER_USER_ROOT="$WMHOME"
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_USER_XDG WMAKER_USER_DATA WMAKER_USER_CACHE WMAKER_IPC_SOCKET

note "start wmaker"
LD_LIBRARY_PATH="$WMLIBS" \
DISPLAY=$DPY \
XDG_RUNTIME_DIR="$RUNTIME" \
WMAKER_USER_ROOT="$WMHOME" \
"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
wm_ready() {
	DISPLAY=$DPY xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null \
		| grep 'window id' >/dev/null
}
wait_for 80 wm_ready || fatal "wmaker did not start"

note "start WPrefs"
# the ASan build reports third-party process-lifetime allocations at
# exit; the repo's lsan-suppressions.txt covers exactly those
LSAN_OPTIONS="suppressions=$ROOT/test/lsan-suppressions.txt:print_suppressions=0" \
LD_LIBRARY_PATH="$WMLIBS" \
DISPLAY=$DPY \
WMAKER_BIN_NAME="$ROOT/src/.libs/wmaker" \
WMAKER_USER_ROOT="$WMHOME" \
"$ROOT/WPrefs.app/.libs/WPrefs" >>"$LOG" 2>&1 &
WPID=$!
echo "$WPID" >> "$PIDFILE"

wprefs_win() {
	W=$(xdo search --onlyvisible --class WPrefs 2>/dev/null | head -n 1)
	[ -n "$W" ]
}
wait_for 40 wprefs_win || fatal "WPrefs window did not appear"
sleep 0.5

note "open the Applications Menu Definition panel (section 9)"
CONTENT=$(DISPLAY=$DPY xwininfo -id "$W" -tree 2>/dev/null \
	| grep '1024x64+' | awk '{print $1}' | head -1)
[ -n "$CONTENT" ] || fatal "section bar not found"
n=0
while :; do
	set -- $(DISPLAY=$DPY xwininfo -id "$CONTENT" 2>/dev/null \
		| awk '/Absolute upper-left X/{x=$4+0} /Absolute upper-left Y/{y=$4+0} END{print x, y}')
	TX=$(( $1 + 64 * 9 + 32 ))
	if [ "$TX" -ge 285 ] && [ "$TX" -le 740 ]; then
		xdo mousemove "$TX" "$(($2 + 32))" click 1
		break
	fi
	xdo mousemove 512 281
	sleep 0.15
	xdo click 4
	sleep 0.2
	n=$((n + 1))
	[ $n -gt 25 ] && fatal "section 9 never became visible"
done
sleep 0.6
TITLE=$(DISPLAY=$DPY xprop -id "$W" _NET_WM_NAME 2>/dev/null)
case "$TITLE" in
	*Applications\ Menu\ Definition*) ok "menu editor panel opened" ;;
	*) fail "menu editor panel opened (title: $TITLE)" ;;
esac

note "click Load Default and confirm"
BTN_LIST=$(DISPLAY=$DPY xwininfo -id "$W" -tree 2>/dev/null | grep -c '76x18')
check "exactly one Load Default label found" [ "$BTN_LIST" -eq 1 ]
BID=$(DISPLAY=$DPY xwininfo -id "$W" -tree 2>/dev/null | grep '76x18' | head -1 | awk '{print $1}')
set -- $(DISPLAY=$DPY xwininfo -id "$BID" 2>/dev/null \
	| awk '/Absolute upper-left X/{x=$4+0} /Absolute upper-left Y/{y=$4+0} END{print x, y}')
xdo mousemove "$(( $1 + 38 ))" "$(( $2 + 9 ))" click 1

alert_up() {
	A=$(xdo search --onlyvisible --class WPrefs 2>/dev/null | grep -v -x "$W" | head -n 1)
	[ -n "$A" ]
}
if wait_for 40 alert_up; then
	check "load confirmation alert appeared" true
	xdo windowactivate "$A" >/dev/null 2>&1
	sleep 0.3
	xdo key Return
	sleep 0.5
	if alert_up; then
		xdo key --window "$A" Return
		sleep 0.5
	fi
else
	check "load confirmation alert appeared" false
	xdo key Escape
fi

note "save"
xdo mousemove --window "$W" 375 364 click 1
wait_for 50 grep -q 'INFO_PANEL' "$MENUDIR/menu" \
	|| fail "saved menu does not carry the shipped default content"
check "saved menu matches the shipped default" grep -q 'INFO_PANEL' "$MENUDIR/menu"
check "saved menu drops the seeded entry" \
	lacks 'Seeded Probe Entry' "$MENUDIR/menu"
check "WMRootMenu domain pointer untouched" \
	grep -q "$MENUDIR/menu" "$DEFS/WMRootMenu"

note "close WPrefs"
xdo mousemove --window "$W" 465 364 click 1
wait_for 40 pid_gone "$WPID" || {
	kill "$WPID" 2>/dev/null
	fail "WPrefs did not exit on Close"
}
check "WPrefs exited cleanly" pid_gone "$WPID"
check "no leak reports" lacks LeakSanitizer "$LOG"

printf '\n' | tee -a "$LOG"
printf 'passed=%d failed=%d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
[ "$FAILED" -eq 0 ] || exit 1
exit 0
