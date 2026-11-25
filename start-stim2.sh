#!/bin/bash
#
# xinit client for the X11 stim2.service. Package-owned: upgrades replace
# this file, so per-rig settings go in /etc/default/stim2 (seeded from
# /usr/local/stim2/config/stim2.default on first install, never overwritten).

STIM2_REFRESH=120
STIM2_OUTPUT=
[ -r /etc/default/stim2 ] && . /etc/default/stim2

sleep 1
if [ -n "$STIM2_OUTPUT" ]; then
    xrandr --output "$STIM2_OUTPUT" --rate "$STIM2_REFRESH"
else
    xrandr --rate "$STIM2_REFRESH"
fi || echo "start-stim2: xrandr could not set ${STIM2_REFRESH} Hz${STIM2_OUTPUT:+ on $STIM2_OUTPUT}" >&2

exec /usr/local/stim2/stim2 -F -r "$STIM2_REFRESH" -f /usr/local/stim2/config/linux.cfg
