#!/bin/sh
# Updates this clone to the newest tpw-port and builds it (make update), then starts the launcher (make run). The
# launcher's About box opens it in Terminal (Check for updates, then Update), and it can be run by hand. Your saves and
# checked game files (~/Library/Application Support/STP-TPW Launcher) are never touched.
#
# The shell reads a script while it runs it, and make update may change this file: so the script is one block, which
# the shell reads whole before it starts.
{
    cd "$(dirname "$0")" || exit 1
    if make update && make run; then
        exit 0
    fi
    echo
    echo "The update stopped (see above). Your saves and game files were not touched. Press Return to close this window."
    read -r _
    exit 1
}
