#!/bin/sh
# Regression gate for the run-dialog startup-notification opt-out.
# ExecuteInputCommand (the run dialog and root-menu EXEC entries) must
# launch without startup notification, exactly like a terminal launch:
# a wrong timestamp or misattributed id in the _NET_STARTUP_INFO
# exchange poisons CSD clients' focus/launch state (chrome). The dock
# path shares the same LaunchChildSetup pre_exec hook and must keep
# exporting DESKTOP_STARTUP_ID, so it is the positive control.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${EXEC_SN_DRIVE_DISPLAY:-:$(drive_default_display)}
W=1280
H=800
RUNTIME=$(mktemp -d /tmp/exec-sn-drive.XXXXXX)
WMHOME="$RUNTIME/home"
MARK="$RUNTIME/mark"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/exec-sn-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
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
fail() { printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; exit 1; }
xdo()  { DISPLAY=$DPY xdotool "$@"; sleep 0.05; }

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
config_defines() {
    [ -f "$ROOT/config.h" ] || return 1
    grep -E "^#define[ 	]+$1([ 	]+|$)" "$ROOT/config.h" >/dev/null
}

command -v Xvfb    >/dev/null || { echo "SKIP: Xvfb not found"; exit 77; }
command -v xdotool >/dev/null || { echo "SKIP: xdotool not found"; exit 77; }

# --- isolated WMAKER_USER_ROOT ---------------------------------------------
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker" "$MARK"
# the capture helper records the value the launched process actually
# sees. An unset variable records an empty file
cat >"$RUNTIME/cap.sh" <<'WMEOD'
#!/bin/sh
printf '%s' "${DESKTOP_STARTUP_ID-}" > "$1"
WMEOD
chmod +x "$RUNTIME/cap.sh"

cat >"$WMHOME/Defaults/WindowMaker" <<EOF
{
  MouseRightButtonAction = OpenApplicationsMenu;
}
EOF
cat >"$WMHOME/Defaults/WMRootMenu" <<EOF
("Root", ("Env", EXEC, "sh $RUNTIME/cap.sh $MARK/menu"))
EOF
cat > "$WMHOME/Defaults/WMState" <<'WMEOD'
{
  Dock = {
    Applications = (
      { Name = "exec-sn-drive.exec-sn-drive";
        Command = "CAPCMD";
        AutoLaunch = NO; Lock = NO; Forced = NO; BuggyApplication = NO;
        Position = "0,1"; }
    );
  };
}
WMEOD
sed -i "s|CAPCMD|sh $RUNTIME/cap.sh $MARK/dock|" "$WMHOME/Defaults/WMState"
export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET

# --- Xvfb ------------------------------------------------------------------
if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use, set EXEC_SN_DRIVE_DISPLAY" >&2
    exit 77
fi
note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 ${W}x${H}x24 -bs -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
xvfb_ready() {
    kill -0 "$XVFB_PID" 2>/dev/null || fail "Xvfb exited early"
    DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xvfb_ready || fail "Xvfb failed to start"

# --- wmaker ----------------------------------------------------------------
note "start wmaker"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
DISPLAY=$DPY \
"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY XDG_RUNTIME_DIR="$RUNTIME" \
	"$WMAKERMSG" --get-socketpath 2>/dev/null)
wm_ready() {
    msg -q get_version >/dev/null 2>&1
}
msg() {
    LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        "$WMAKERMSG" -s "$SOCKET" "$@"
}
wait_for 120 wm_ready || fail "wmaker did not come up"
sleep 0.3

# --- 1. dock launch keeps startup notification (positive control) ----------
if ! config_defines HAVE_STARTUP_NOTIFICATION; then
    note "dock control skipped: built --without-startup-notification"
else
    note "dock-launch exports DESKTOP_STARTUP_ID"
    msg -q dock-launch 1 >>"$LOG" 2>&1 \
        || fail "dock-launch 1 rejected"
    wait_for 50 test -s "$MARK/dock" \
        || fail "dock child saw no DESKTOP_STARTUP_ID (empty or missing)"
    grep -q 'wmaker/' "$MARK/dock" \
        || fail "dock DESKTOP_STARTUP_ID is not a wmaker id: $(cat "$MARK/dock")"
    note "ok: dock child got $(cat "$MARK/dock")"
fi

# --- 2. menu EXEC (the run dialog path) launches without SN -----------------
note "root-menu EXEC must not receive DESKTOP_STARTUP_ID"
CX=$((W/2))
CY=$((H/2))
xdo mousemove "$CX" "$CY"
xdo mousedown 3
ENTRY_WIN=$(DISPLAY=$DPY xwininfo -root -tree 2>/dev/null \
    | awk -v cx="$CX" -v cy="$CY" '
        /^     0x/ { fx=""; fy=""; fw="";
            if (match($0, /[0-9]+x[0-9]+\+-?[0-9]+\+-?[0-9]+/)) {
                split(substr($0,RSTART,RLENGTH), g, /[x+]/);
                fw=g[1]; fx=g[3]; fy=g[4];
            }
            inframe = (fx!="" && fy==cy && fx<=cx && cx<=fx+fw);
            next; }
        inframe && /^        0x/ {
            if (match($0, /[0-9]+x[0-9]+\+-?[0-9]+\+-?[0-9]+  \+-?[0-9]+\+-?[0-9]+$/)) {
                split(substr($0,RSTART,RLENGTH), g, /[x+ ]+/);
                if (g[6] > cy) { print g[5] " " g[6] " " g[2]; exit; }
            } }')
[ -n "$ENTRY_WIN" ] || { xdo mouseup 3; fail "could not locate the menu entry window"; }
set -- $ENTRY_WIN
EX=$1; EY=$2; EH=$3
ROWH=$((EH + 1))
xdo mousemove "$((EX + 6))" "$((EY + ROWH / 4))"
xdo mouseup 3
wait_for 50 test -f "$MARK/menu" \
    || fail "menu EXEC child never recorded its environment"
[ -s "$MARK/menu" ] \
    && fail "run-dialog path leaked DESKTOP_STARTUP_ID=$(cat "$MARK/menu")"
note "ok: menu EXEC child saw no DESKTOP_STARTUP_ID"

# --- teardown --------------------------------------------------------------
kill -TERM "$WMAKER_PID" 2>/dev/null
wait_for 50 pid_gone "$WMAKER_PID"

note "PASS"
exit 0
