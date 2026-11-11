#!/bin/sh
# The mirror's display under cage (stim2-mirror-display-cage.service, the
# arm64 flavor of start-stim2-mirror.sh): stim2 fullscreen on this box's
# monitor, on ports 5610-5613 so it can never be mistaken for a rig's
# display (4610-4613).
#
# cage shows the monitor's preferred mode, and stim2 -F reads the rate from
# the mode it gets, so no -r here. MIRROR_REFRESH in /etc/default/stim2-mirror
# asks for another rate at the current size; under cage that takes wlr-randr
# (apt install wlr-randr). Without it, or when the monitor has no such mode,
# the preferred mode stays and this says so in the journal.

[ -r /etc/default/stim2-mirror ] && . /etc/default/stim2-mirror

if [ -n "$MIRROR_REFRESH" ]; then
    if command -v wlr-randr >/dev/null 2>&1; then
        # wlr-randr lists each output unindented, its modes indented, the
        # one in use marked "current":
        #   HDMI-A-1 "..."
        #       1920x1080 px, 60.000000 Hz (preferred, current)
        OUT=$(wlr-randr | awk '/^[^ \t]/ { o = $1 } /current\)/ && o { print o; exit }')
        SIZE=$(wlr-randr | awk '/current\)/ { print $1; exit }')
        if [ -n "$OUT" ] && [ -n "$SIZE" ] && \
           wlr-randr --output "$OUT" --mode "${SIZE}@${MIRROR_REFRESH}Hz"; then
            echo "stim2 mirror: ${OUT} at ${SIZE}@${MIRROR_REFRESH}Hz"
        else
            echo "stim2 mirror: could not set ${MIRROR_REFRESH} Hz on ${OUT:-the output}; keeping the monitor's mode" >&2
        fi
    else
        echo "stim2 mirror: MIRROR_REFRESH=${MIRROR_REFRESH} needs wlr-randr under cage; keeping the monitor's mode" >&2
    fi
fi

exec /usr/local/stim2/stim2 -F --port-base 5610 \
     --title "stim2 mirror" -f /usr/local/stim2/config/linux.cfg
