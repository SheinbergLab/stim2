#!/bin/sh
# Launched by Weston kiosk-shell via [autolaunch] in stim2-kiosk.ini
# (stim2-weston.service). watch=true in that ini means Weston exits with
# stim2, and systemd Restart= brings the whole session back up.
exec /usr/local/stim2/stim2 -F -f /usr/local/stim2/config/linux.cfg
