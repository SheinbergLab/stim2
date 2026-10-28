#!/bin/bash
# The mirror's display, under xinit (stim2-mirror-display.service): stim2
# fullscreen on this box's monitor at the monitor's own refresh, on ports
# 5610-5613 so it can never be mistaken for a rig's display (4610-4613).
# MIRROR_REFRESH in /etc/default/stim2-mirror picks a rate; otherwise the
# monitor's current one is used.

[ -r /etc/default/stim2-mirror ] && . /etc/default/stim2-mirror

sleep 1
if [ -n "$MIRROR_REFRESH" ]; then
    xrandr --rate "$MIRROR_REFRESH"
fi
RATE=$(xrandr | awk '/\*/ { for (i = 2; i <= NF; i++) if ($i ~ /\*/) { gsub(/[*+]/, "", $i); printf "%d", $i + 0.5; exit } }')

exec /usr/local/stim2/stim2 -F -r "${RATE:-60}" --port-base 5610 \
     --title "stim2 mirror" -f /usr/local/stim2/config/linux.cfg
