#!/bin/sh
# Drive the inspector handling of hint-carried decoration flags:
#   1. a _WINDOWMAKER_STATE no_titlebar flag planted by a client is
#      consumed at manage time (the window comes up titlebar-less),
#   2. an inspector Save with no checkbox touched writes nothing to
#      WMWindowAttributes: the sweep skips flags restored from the
#      hint, and a stale AlwaysUserIcon default is not written either
#      (the icon checkbox saves as a boolean),
#   3. an explicitly toggled checkbox (Keep on top) is persisted, so
#      user choices still save,
#   4. Reload re-derives from the domain and client (the titlebar
#      comes back), after which deliberately re-checking
#      "Disable titlebar" persists NoTitlebar = Yes,
#   5. a same-class sibling spawned after the untouched Save still
#      gets a titlebar (the untouched Save leaked nothing), and the
#      deliberate choice restores across a warm restart.
# The inspector is driven through WindowMenuKey ("Attributes..." is
# window_menu_entries index 9, 9 Downs from the unselected state while
# the Restore geometry entry is hidden)
# and fixed panel geometry (attributes page: checkbox rows every 20px
# from y=61, row 6 = "Disable titlebar", row 9 = "Keep on top").
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${HINT_INSPECTOR_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/wmaker-hintinsp-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/hint-inspector-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
CLIENT="$RUNTIME/wmstate-client"
DOMAIN="$WMHOME/WMWindowAttributes"
WMCLASS=WmstateTest
PASSED=0
FAILED=0
SOCKET=
PANEL=
PANEL_CONTENT=
PANEL_CONTENT_H=
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
record_pass() { PASSED=$((PASSED + 1)); printf 'PASS: %s\n' "$*" | tee -a "$LOG"; }
record_fail() { FAILED=$((FAILED + 1)); printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; }
fatal() { printf 'FATAL: %s\n' "$*" | tee -a "$LOG"; exit 1; }
skip_all() { printf 'SKIP: %s\n' "$*" | tee -a "$LOG"; exit 77; }

check() {
	check_desc=$1
	shift
	if "$@"; then
		record_pass "$check_desc"
	else
		record_fail "$check_desc"
	fi
}

wait_for() {
	_wf=$1; shift
	while [ "$_wf" -gt 0 ]; do
		"$@" && return 0
		sleep 0.1
		_wf=$((_wf - 1))
	done
	return 1
}

pid_gone() { ! kill -0 "$1" 2>/dev/null; }

for tool in Xvfb xdpyinfo xdotool wmakermsg; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"

${CC:-cc} -std=gnu11 "$ROOT/test/wmstate-client.c" -o "$CLIENT" -lX11 >>"$LOG" 2>&1 \
	|| skip_all "could not build test/wmstate-client.c (needs libX11 headers)"

msg() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		"$WMAKERMSG" -s "$SOCKET" "$@"
}
ipc_ready() { msg -q get_version >/dev/null 2>&1; }

# frame-relative Y of a managed client window: 0 when it sits at the
# top of its frame (no titlebar), ~25 with one
client_rely() {
	DISPLAY=$DPY xwininfo -id "$1" 2>/dev/null \
		| sed -n 's/.*Relative upper-left Y:  *//p'
}

# frame-relative Y of a client below/above a threshold, guarding the
# empty read a gone window would produce
client_rely_cmp() {
	_v=$(client_rely "$1")
	[ -n "$_v" ] || return 1
	[ "$_v" -"$2" "$3" ]
}

a_rely_lt() { client_rely_cmp "$XID_A" lt "$1"; }
a_rely_ge() { client_rely_cmp "$XID_A" ge "$1"; }
b_rely_ge() { client_rely_cmp "$XID_B" ge "$1"; }

managed() { msg get_windows 2>/dev/null | grep -F "$WMCLASS" >/dev/null; }

idfile_ready() { [ -s "$1" ]; }

start_wmaker() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		DISPLAY=$DPY \
		"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
	WMAKER_PID=$!
	echo "$WMAKER_PID" >> "$PIDFILE"
	SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		DISPLAY=$DPY "$WMAKERMSG" --get-socketpath)
	wait_for 120 ipc_ready || fatal "wmaker IPC did not come up"
}

stop_wmaker() {
	msg -q exit >>"$LOG" 2>&1
	wait_for 80 pid_gone "$WMAKER_PID" || fatal "wmaker did not exit"
	sleep 0.5
}

spawn_client() {
	_titlebar=$1
	_idfile=$2
	WMSTATE_TITLEBAR=$_titlebar WMSTATE_CLASS=$WMCLASS \
		DISPLAY=$DPY "$CLIENT" >"$_idfile" 2>>"$LOG" &
	echo "$!" >> "$PIDFILE"
}

# walk WindowMenuKey to "Attributes..." and open the panel; the panel
# frame is the viewable 290-wide top-level that appears
open_inspector() {
	DISPLAY=$DPY xdotool key ctrl+Escape
	sleep 0.4
	DISPLAY=$DPY xdotool key --delay 150 \
		Down Down Down Down Down Down Down Down Down
	sleep 0.3
	DISPLAY=$DPY xdotool key Return
	sleep 0.3
}

panel_up() {
	PANEL=
	for _w in $(DISPLAY=$DPY xwininfo -root -children 2>/dev/null \
			| grep -oE '^     0x[0-9a-f]+' | tr -d ' '); do
		_st=$(DISPLAY=$DPY xwininfo -id "$_w" 2>/dev/null | sed -n 's/.*Map State:  *//p')
		_wd=$(DISPLAY=$DPY xwininfo -id "$_w" 2>/dev/null | sed -n 's/^  Width:  *//p')
		if [ "$_st" = "IsViewable" ] && [ "$_wd" = "290" ]; then
			PANEL=$_w
			return 0
		fi
	done
	return 1
}

panel_content() {
	PANEL_CONTENT=$(DISPLAY=$DPY xwininfo -id "$PANEL" -children 2>/dev/null \
		| grep '290x' | grep -oE '^     0x[0-9a-f]+' | head -1 | tr -d ' ')
	[ -n "$PANEL_CONTENT" ]
}

panel_content_h() {
	DISPLAY=$DPY xwininfo -id "$PANEL" -children 2>/dev/null \
		| sed -n 's/.*290x\([0-9]*\).*/\1/p' | head -1
}

click_at() {
	DISPLAY=$DPY xdotool mousemove --window "$PANEL_CONTENT" "$1" "$2"
	sleep 0.15
	DISPLAY=$DPY xdotool click 1
	sleep 0.4
}

domain_has() {
	[ -f "$DOMAIN" ] && grep -q "$1" "$DOMAIN" 2>/dev/null
}

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY is already in use, set HINT_INSPECTOR_DRIVE_DISPLAY" >&2
	exit 77
fi

mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
cat >"$WMHOME/Defaults/WindowMaker" <<'EOF'
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
}
EOF

export WMAKER_USER_ROOT="$WMHOME"
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET

note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 1024x768x24 -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
xvfb_ready() {
	kill -0 "$XVFB_PID" 2>/dev/null || return 1
	DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xvfb_ready || fatal "Xvfb failed to start"

# --- 1. planted no_titlebar applies at manage time -------------------------
note "phase 1: planted _WINDOWMAKER_STATE no_titlebar"
start_wmaker
: > "$RUNTIME/a.id"
spawn_client 2 "$RUNTIME/a.id"
XID_A=
wait_for 50 idfile_ready "$RUNTIME/a.id" || fatal "planted client never mapped"
wait_for 50 managed || fatal "planted client never got managed"
XID_A=$(sed -n 1p "$RUNTIME/a.id")

if wait_for 20 a_rely_lt 10; then
	record_pass "planted no_titlebar applies at manage time"
else
	record_fail "planted no_titlebar applies at manage time (relY=$(client_rely "$XID_A"))"
fi

# --- 2. untouched inspector save persists nothing --------------------------
note "phase 2: untouched Save sweeps nothing"
open_inspector
wait_for 50 panel_up || fatal "inspector panel did not open"
panel_content || fatal "panel content window not found"
PANEL_CONTENT_H=$(panel_content_h)
SAVE_Y=$((PANEL_CONTENT_H - 26))

click_at 235 "$SAVE_Y"
sleep 0.6
if [ -f "$DOMAIN" ] && grep -qE 'NoTitlebar|AlwaysUserIcon' "$DOMAIN"; then
	record_fail "untouched Save writes no hint-carried flag ($(grep -oE '(NoTitlebar|AlwaysUserIcon) = [A-Za-z]+' "$DOMAIN" | tr '\n' ' '))"
else
	record_pass "untouched Save writes no hint-carried flag"
fi

# --- 3. explicit toggle persists, hint-carried flag does not ride along ----
note "phase 3: explicit toggle persists"
click_at 30 251
click_at 235 "$SAVE_Y"
sleep 0.6
if domain_has 'KeepOnTop = Yes' && ! grep -q 'NoTitlebar' "$DOMAIN"; then
	record_pass "toggled KeepOnTop persists, NoTitlebar stays out"
else
	record_fail "toggled KeepOnTop persists, NoTitlebar stays out"
fi

# --- 4. sibling spawned after the untouched Save keeps its titlebar --------
note "phase 4: same-class sibling stays clean"
: > "$RUNTIME/b.id"
spawn_client 0 "$RUNTIME/b.id"
XID_B=
wait_for 50 idfile_ready "$RUNTIME/b.id" || fatal "sibling never mapped"
XID_B=$(sed -n 1p "$RUNTIME/b.id")
sleep 0.5
if wait_for 20 b_rely_ge 10; then
	record_pass "same-class sibling keeps its titlebar"
else
	record_fail "same-class sibling keeps its titlebar (relY=$(client_rely "$XID_B"))"
fi

# --- 5. Reload re-derives, deliberate re-choice persists -------------------
note "phase 5: Reload then deliberate NoTitlebar"
click_at 55 "$SAVE_Y"
sleep 0.5
if wait_for 20 a_rely_ge 10; then
	record_pass "Reload re-derives the titlebar"
else
	record_fail "Reload re-derives the titlebar (relY=$(client_rely "$XID_A"))"
fi

click_at 30 191
click_at 235 "$SAVE_Y"
sleep 0.6
if domain_has 'NoTitlebar = Yes'; then
	record_pass "deliberate Disable titlebar persists"
else
	record_fail "deliberate Disable titlebar persists"
fi

# --- 6. the deliberate choice survives a warm restart ---------------------
note "phase 6: warm restart restores the deliberate choice"
stop_wmaker
start_wmaker
wait_for 50 managed || fatal "windows did not come back after restart"
sleep 0.5
if wait_for 20 a_rely_lt 10; then
	record_pass "deliberate NoTitlebar restores across a warm restart"
else
	record_fail "deliberate NoTitlebar restores across a warm restart (relY=$(client_rely "$XID_A"))"
fi

note "phase 7: Apply overrides the domain only for this window"
DISPLAY=$DPY xdotool windowactivate --sync "$XID_A"
open_inspector
wait_for 50 panel_up || fatal "inspector panel did not reopen"
panel_content || fatal "panel content window not found"
PANEL_CONTENT_H=$(panel_content_h)
SAVE_Y=$((PANEL_CONTENT_H - 26))
click_at 30 191
click_at 145 "$SAVE_Y"
if wait_for 20 a_rely_ge 10 && domain_has 'NoTitlebar = Yes'; then
	record_pass "Apply shows the titlebar without changing the domain"
else
	record_fail "Apply did not override the domain"
fi
stop_wmaker
start_wmaker
wait_for 50 managed || fatal "windows did not come back after Apply restart"
if wait_for 20 a_rely_ge 10 && domain_has 'NoTitlebar = Yes'; then
	record_pass "Apply override survives restart while the domain remains set"
else
	record_fail "Apply override was lost across restart (relY=$(client_rely "$XID_A"))"
fi

note "phase 8: Save returns the applied choice to domain control"
DISPLAY=$DPY xdotool windowactivate --sync "$XID_A"
open_inspector
wait_for 50 panel_up || fatal "inspector panel did not reopen for Save"
panel_content || fatal "panel content window not found"
PANEL_CONTENT_H=$(panel_content_h)
SAVE_Y=$((PANEL_CONTENT_H - 26))
click_at 30 191
click_at 145 "$SAVE_Y"
click_at 30 191
click_at 235 "$SAVE_Y"
stop_wmaker
cat >"$DOMAIN" <<'EOF'
{
  WmstateTest = { NoTitlebar = Yes; };
}
EOF
start_wmaker
wait_for 50 managed || fatal "windows did not come back after Save restart"
if wait_for 20 a_rely_lt 10; then
	record_pass "domain change wins after saving an applied override"
else
	record_fail "saved override still masks the domain (relY=$(client_rely "$XID_A"))"
fi

note "hint-inspector drive finished: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
