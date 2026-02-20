#!/bin/bash

REFRESH=120

sleep 1
xrandr --rate "$REFRESH"

exec /usr/local/stim2/stim2 -F -r "$REFRESH" -f /usr/local/stim2/config/linux.cfg

