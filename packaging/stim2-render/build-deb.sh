#!/bin/sh
# Build the stim2-render package (architecture: all).
#
#     packaging/stim2-render/build-deb.sh VERSION [OUTDIR]
#
# It carries no program: stim2 ships the unit and the render server. It is
# the switch that turns the service on, its settings file, and the
# Depends (dlsh-interp, ffmpeg) the server needs -- see control.in.
set -eu

VERSION=${1:?usage: build-deb.sh VERSION [OUTDIR]}
OUT=${2:-.}
HERE=$(cd "$(dirname "$0")" && pwd)

# The oldest stim2 this package works with: the unit reads
# /etc/default/stim2-render, and the server resolves its cache directory's
# symlink (systemd's DynamicUser makes /var/cache/stim2-render one).
MIN_STIM2=$(cat "$HERE/min-stim2")

root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT

mkdir -p "$root/DEBIAN" "$root/etc/default" "$root/usr/share/doc/stim2-render"
sed -e "s/@VERSION@/$VERSION/" -e "s/@MIN_STIM2@/$MIN_STIM2/" \
    "$HERE/control.in" > "$root/DEBIAN/control"
echo /etc/default/stim2-render > "$root/DEBIAN/conffiles"
for s in postinst prerm postrm; do
    install -m 0755 "$HERE/$s" "$root/DEBIAN/$s"
done
install -m 0644 "$HERE/stim2-render.default" "$root/etc/default/stim2-render"
cat > "$root/usr/share/doc/stim2-render/README" <<'EOF'
stim2-render turns on stim2's trial-clip service for dserv's trial viewer.
Options: /etc/default/stim2-render. Log: journalctl -u stim2-render.
Documentation: https://github.com/SheinbergLab/stim2/blob/main/docs/render.md
EOF

mkdir -p "$OUT"
dpkg-deb --root-owner-group --build "$root" "$OUT/stim2-render_${VERSION}_all.deb"
