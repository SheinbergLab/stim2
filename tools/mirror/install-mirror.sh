#!/bin/sh
# install-mirror.sh -- make a Linux box (amd64, X11, Debian trixie) a live
# mirror of a rig's display: run as root, from a stim2 checkout or a copy
# of its tools/ directory.
#
#     sudo sh tools/mirror/install-mirror.sh RIG_DSERV_HOST
#
# Needs stim2 0.28.4 or newer installed from its .deb (the stim user, the
# Xwrapper setting and linux.cfg come from its postinst). Installs:
#   - dlsh-interp (the dlsh interpreter and dlsh.zip) from the dlsh release,
#     when /usr/local/bin/dlsh is missing or older than DLSH_VERSION
#   - tools/lib, tools/mirror, tools/replay to /usr/local/stim2/tools
#   - stim2-mirror-display.service (stim2 fullscreen, ports 5610-5613) and
#     stim2-mirror.service (the driver), enabled; stim2.service disabled,
#     since both want the X server on vt1
#   - /etc/default/stim2-mirror, with MIRROR_DSERV set (kept if it exists)
# and puts the stim user (and SUDO_USER) in the video and render groups.
set -e

DLSH_VERSION=${DLSH_VERSION:-0.20.6}
RIG=$1
HERE=$(cd "$(dirname "$0")" && pwd)
TOOLS=$(cd "$HERE/.." && pwd)

if [ "$(id -u)" != 0 ]; then echo "run as root (sudo)"; exit 1; fi
if [ ! -x /usr/local/stim2/stim2 ]; then echo "stim2 is not installed (/usr/local/stim2/stim2)"; exit 1; fi
if ! getent passwd stim >/dev/null; then echo "no stim user: reinstall the stim2 .deb"; exit 1; fi

# dlsh interpreter
if [ ! -x /usr/local/bin/dlsh ] || ! dpkg -s dlsh-interp >/dev/null 2>&1 \
   || dpkg --compare-versions "$(dpkg-query -W -f='${Version}' dlsh-interp)" lt "$DLSH_VERSION"; then
    arch=$(dpkg --print-architecture)
    tmp=$(mktemp -d)
    url=https://github.com/SheinbergLab/dlsh/releases/download/$DLSH_VERSION/dlsh-interp_${DLSH_VERSION}_$arch.deb
    echo "installing dlsh-interp $DLSH_VERSION"
    curl -fsSL -o "$tmp/dlsh-interp.deb" "$url"
    apt-get install -y "$tmp/dlsh-interp.deb"
    rm -rf "$tmp"
fi
echo "$DLSH_VERSION" > /usr/local/dlsh/VERSION

# groups: the display and the render node (VAAPI, a headless stim2); the
# installing user also reads the mirror's journal
for u in stim $SUDO_USER; do
    for g in video render; do
        getent group $g >/dev/null && adduser "$u" $g >/dev/null 2>&1 || true
    done
done
[ -n "$SUDO_USER" ] && adduser "$SUDO_USER" systemd-journal >/dev/null 2>&1 || true

# the tools (already in place when run from a stim2 .deb that ships them)
if [ "$TOOLS" != /usr/local/stim2/tools ]; then
    mkdir -p /usr/local/stim2/tools
    for d in lib mirror replay; do
        rm -rf "/usr/local/stim2/tools/$d"
        cp -r "$TOOLS/$d" "/usr/local/stim2/tools/$d"
    done
fi
chmod 755 /usr/local/stim2/tools/mirror/start-stim2-mirror.sh

# units and settings
cp "$HERE/systemd/stim2-mirror-display.service" "$HERE/systemd/stim2-mirror.service" /etc/systemd/system/
if [ ! -f /etc/default/stim2-mirror ]; then
    cp "$HERE/systemd/stim2-mirror.default" /etc/default/stim2-mirror
fi
if [ -n "$RIG" ]; then
    sed -i "s/^MIRROR_DSERV=.*/MIRROR_DSERV=$RIG/" /etc/default/stim2-mirror
fi
systemctl daemon-reload
systemctl disable --now stim2.service 2>/dev/null || true
systemctl enable stim2-mirror-display.service stim2-mirror.service
systemctl restart stim2-mirror-display.service
systemctl restart stim2-mirror.service
echo "mirror of $(sed -n 's/^MIRROR_DSERV=//p' /etc/default/stim2-mirror): journalctl -fu stim2-mirror"
