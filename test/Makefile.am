## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS = no-dependencies

# wm_fsm_test.c needs GTK3 which is outside the project's dependency
# policy and is not probed at configure time, ship the source for
# reference but do not wire it into the build
EXTRA_DIST = wm_fsm_test.c asan-drive.sh hot-corner-drive.sh ewmh-layout-drive.sh ipc-drive.sh osd-keychain-drive.sh ewmh-client.c lsan-suppressions.txt
