#!/bin/sh
#
# Place applications to be executed when WindowMaker is started here.
# This should only be used for non-X applications or applications that
# do not support session management. Other applications should be restarted
# by the WindowMaker session restoring mechanism. For that, you should
# either set SaveSessionOnExit=YES or select "Save Session" in the Workspace
# submenu of the root menu when all applications you want started are
# running.
#
# WindowMaker will wait until this script finishes, so if you run any
# commands that take long to execute (like a xterm), put a ``&'' in the
# end of the command line.
#
# This file must be executable.
#

# Uncomment to run the clipboard daemon, which keeps CLIPBOARD contents
# alive after the owning application exits and (with --screenshots) puts
# Window Maker screenshots on the clipboard. See wmclipd(1).
# The pgrep guard stops a second copy starting when Window Maker restarts.
#pgrep -u "$(id -u)" -x wmclipd >/dev/null 2>&1 || wmclipd --clipboard --screenshots &
