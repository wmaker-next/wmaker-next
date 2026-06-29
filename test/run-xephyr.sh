#!/bin/sh
# Launch the just-built Window Maker inside a Xephyr window for visual
# inspection. Everything is isolated (private WMAKER_USER_ROOT, private
# XDG_RUNTIME_DIR, fixed unusual display) so a wmaker already running on
# the user's :0 desktop is never touched.
#
# Usage:
#   make run                       # default: 1280x800 on :97, xterm auto-spawned
#   RUN_DISPLAY=:98 make run       # pick another display
#   RUN_GEOMETRY=1600x900 make run # pick another size
#   RUN_CLIENTS=no make run        # do not auto-spawn xterm
#   RUN_CLIENTS='xterm WPrefs' make run   # spawn a custom client list
#
# Ctrl-C (or closing the Xephyr window) stops wmaker and cleans up.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)

DPY=${RUN_DISPLAY:-:97}
GEOM=${RUN_GEOMETRY:-1280x800}
STATE=/tmp/wmaker-run

WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKER="$ROOT/src/.libs/wmaker"
XEPHYR=${XEPHYR:-Xephyr}

die() { printf 'run-xephyr: %s\n' "$*" >&2; exit 1; }

[ -x "$WMAKER" ] || die "src/.libs/wmaker not built (run make first)"
command -v "$XEPHYR" >/dev/null 2>&1 || die "Xephyr not installed (apt-get install xserver-xephyr)"

if [ -e "$STATE" ]; then
	die "state path $STATE already exists, a previous run did not clean up; remove it and retry"
fi

# refuse to clobber a display already in use
if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	die "display $DPY already in use, set RUN_DISPLAY=:98 (or another free number)"
fi

# isolated runtime so the IPC socket cannot collide with the live :0 session
( umask 077 && mkdir "$STATE" ) || die "could not create $STATE"
RUNTIME=$(mktemp -d /tmp/wmaker-run-rt.XXXXXX)
WMHOME="$RUNTIME/home"
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
# an empty WindowMaker defaults file prevents the first-run installer
# from overwriting WMState on startup
printf '{\n}\n' > "$WMHOME/Defaults/WindowMaker"

cleanup() {
	rc=$?
	# kill xterm and any other spawned clients first so they do not
	# keep the wmaker session alive on the way out
	if [ -f "$STATE/pids" ]; then
		while read -r pid; do
			[ -n "$pid" ] && kill "$pid" 2>/dev/null
		done < "$STATE/pids"
	fi
	# wmaker exits on its own when Xephyr dies, but be explicit
	[ -n "${WMPID:-}" ] && kill "$WMPID" 2>/dev/null
	# Xephyr must be last so wmaker has a server to exit from
	[ -n "${XEPHYRPID:-}" ] && kill "$XEPHYRPID" 2>/dev/null
	rm -rf "$STATE" "$RUNTIME"
	exit $rc
}
trap cleanup EXIT INT TERM

XEPHYRPID=$(
	DISPLAY=${DISPLAY:-:0} "$XEPHYR" "$DPY" -screen "$GEOM" -ac \
		-title "Window Maker (build tree)" 2>"$STATE/xephyr.log" &
	echo $!
)
echo "$XEPHYRPID" > "$STATE/pids"

# wait for the X server to come up before launching wmaker
i=0
while [ "$i" -lt 50 ]; do
	DISPLAY=$DPY xdpyinfo >/dev/null 2>&1 && break
	sleep 0.1
	i=$((i + 1))
done
if [ "$i" -ge 50 ]; then
	die "Xephyr did not start, see $STATE/xephyr.log"
fi

cat >&2 <<EOF
Window Maker is starting on $DPY (Xephyr window).
  config dir : $WMHOME
  log        : $STATE/wmaker.log
Close the Xephyr window or press Ctrl-C here to stop.
EOF

LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY \
	XDG_RUNTIME_DIR="$RUNTIME" \
	WMAKER_USER_ROOT="$WMHOME" \
	"$WMAKER" --for-real >"$STATE/wmaker.log" 2>&1 &
WMPID=$!

# auto-spawn an xterm (or a custom client list) once wmaker is up so the
# session is not empty on first view. RUN_CLIENTS=no disables this.
CLIENTS=${RUN_CLIENTS:-xterm}
if [ "$CLIENTS" != "no" ]; then
	# wait briefly for wmaker to take over the display so the xterm
	# gets managed instead of dying on an unmapped root
	i=0
	while [ "$i" -lt 30 ]; do
		DISPLAY=$DPY xprop -root _NET_SUPPORTING_WM_CHECK >/dev/null 2>&1 && break
		sleep 0.1
		i=$((i + 1))
	done
	for c in $CLIENTS; do
		if command -v "$c" >/dev/null 2>&1; then
			DISPLAY=$DPY "$c" >>"$STATE/wmaker.log" 2>&1 &
			echo $! >> "$STATE/pids"
		else
			printf 'run-xephyr: not spawning %s (not found)\n' "$c" >&2
		fi
	done
fi

# block until wmaker exits (Xephyr closed, or wmaker killed)
wait "$WMPID" 2>/dev/null
