#!/bin/sh
cd /shared/qpcs/projects/stimulator2
export DLSH_LIBRARY=/usr/local/lib/tcltk/dlsh
/usr/local/stim2/stim2 -f /usr/local/stim2/config/linux.cfg -F -r 100
