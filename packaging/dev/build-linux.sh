#!/bin/sh
# Build a dev stim2 for Debian 13 x86_64 from this working tree, in the
# stim2-dev:trixie image (Dockerfile.trixie; build that once).  The source
# is mounted read-only; the build dirs live in docker volumes, so later
# builds are incremental.  Output, in $OUT (default build-dev-trixie/), laid
# out as /usr/local/stim2 is:
#
#   stim2          the binary
#   stimdlls/      the loadable modules (*.so)
#   modules/       the Tcl modules (stimess, ...)
#   shaders/       the shader files, passes/ included
#   fonts/         the fonts the text module loads
#
# The binary and the stimdlls go together: a module needs the functions
# its stim2 exports (gazewin.so and shaderpass.so need a stim2 with the
# post-pass seam), and a stim2 has only the commands of the modules beside
# it.  See README.md here for putting them on a rig.
#
#   packaging/dev/build-linux.sh
#   BINARY_ONLY=1 packaging/dev/build-linux.sh     # skip the rest
set -e
here=$(cd "$(dirname "$0")/../.." && pwd)
out=${OUT:-$here/build-dev-trixie}
mkdir -p "$out"
docker run --rm --platform linux/amd64 \
  -e BINARY_ONLY="${BINARY_ONLY:-}" \
  -v "$here":/src:ro -v stim2-dev-trixie-build:/build \
  -v stim2-dev-trixie-stimdlls:/build-stimdlls -v "$out":/out \
  stim2-dev:trixie sh -c '
    set -e
    # the install rpath (/usr/local/stim2/lib), as the .deb has it
    cmake -S /src -B /build -D PROJECT_VERSION=0.0.0 -D CMAKE_BUILD_WITH_INSTALL_RPATH=ON >/dev/null
    cmake --build /build --target stim2 --parallel
    cp /build/stim2 /out/stim2
    /out/stim2 --help >/dev/null 2>&1 || true
    ls -la /out/stim2
    [ -n "$BINARY_ONLY" ] && exit 0

    # the stimdlls, as the release workflow builds them
    cmake -S /src/stimdlls -B /build-stimdlls -D PROJECT_VERSION=0.0.0 >/dev/null
    cmake --build /build-stimdlls --parallel
    rm -rf /out/stimdlls /out/modules /out/shaders /out/fonts
    mkdir -p /out/stimdlls
    cp /build-stimdlls/*.so /out/stimdlls/
    # the Tcl modules and the shaders, as the .deb installs them
    cp -r /src/modules /out/modules
    mkdir -p /out/shaders
    cp /src/shaders/*.glsl /out/shaders/
    cp -r /src/shaders/passes /out/shaders/passes
    cp -r /src/fonts /out/fonts
    ls /out/stimdlls'
