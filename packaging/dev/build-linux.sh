#!/bin/sh
# Build a dev stim2 binary for Debian 13 x86_64 from this working tree, in
# the stim2-dev:trixie image (Dockerfile.trixie; build that once).  The
# source is mounted read-only; the build dir lives in a docker volume, so
# later builds are incremental.  Output: $OUT/stim2 (default build-dev-trixie/).
#
#   packaging/dev/build-linux.sh
#   scp build-dev-trixie/stim2 rig:   # then install it beside the release one
set -e
here=$(cd "$(dirname "$0")/../.." && pwd)
out=${OUT:-$here/build-dev-trixie}
mkdir -p "$out"
docker run --rm --platform linux/amd64 \
  -v "$here":/src:ro -v stim2-dev-trixie-build:/build -v "$out":/out \
  stim2-dev:trixie sh -c '
    set -e
    # the install rpath (/usr/local/stim2/lib), as the .deb has it
    cmake -S /src -B /build -D PROJECT_VERSION=0.0.0 -D CMAKE_BUILD_WITH_INSTALL_RPATH=ON >/dev/null
    cmake --build /build --target stim2 --parallel
    cp /build/stim2 /out/stim2
    /out/stim2 --help >/dev/null 2>&1 || true
    ls -la /out/stim2'
