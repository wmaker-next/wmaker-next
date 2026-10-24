#!/bin/sh
# Shared helper for the integration drives, sourced once ROOT is set.
#
# drive_default_display [OFFSET] prints a default X display number unique
# to the invoking drive so parallel runs do not collide. The drive's
# position in the sorted list owns a block of three numbers, the primary
# plus the +1/+2 offsets the nested-server drives need. Callers keep their
# per-drive environment override around this.

drive_default_display() {
	_drive_i=$(LC_ALL=C ls "$ROOT"/test/*-drive.sh 2>/dev/null \
		| grep -n "/${0##*/}\$" | head -n1 | cut -d: -f1)
	echo "$((90 + 3 * ${_drive_i:-0} + ${1:-0}))"
}

# Sanitizer options for the process under test. A normal drive keeps
# reports non-fatal so its assertions still run; `make asan` sets
# WM_ASAN_STRICT=1 to abort on the first report and points
# WM_ASAN_LOGDIR at a directory the suite scans independently of the
# driver's own log.
drive_asan_options() {
	_drive_asan_opts=abort_on_error=0:halt_on_error=0:detect_leaks=0
	if [ "${WM_ASAN_STRICT:-0}" = 1 ]; then
		_drive_asan_opts=abort_on_error=1:halt_on_error=1:detect_leaks=0
	fi
	if [ -n "${WM_ASAN_LOGDIR:-}" ]; then
		_drive_asan_opts="${_drive_asan_opts}:log_path=${WM_ASAN_LOGDIR}/asan"
	fi
	printf '%s' "$_drive_asan_opts"
}

# Route a fatal signal through exit so the drive's own EXIT cleanup reaps
# its private Xvfb and wmaker instead of leaking them into the next run.
trap 'exit 143' TERM
trap 'exit 130' INT
