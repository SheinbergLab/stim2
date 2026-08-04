#!/bin/bash
# Native stim2 build for Debian trixie arm64 boards (i.MX95 FRDM bring-up).
# Mirrors .github/workflows/release_linux.yml (trixie/arm64 job) so the
# resulting deb matches what CI would publish. Run as root:
#
#   sudo bash ~/stim2-src/scripts/native-build-imx95.sh 2>&1 | tee ~/build.log
#
set -euo pipefail

SRC=${SRC:-/home/sheinb/stim2-src}
VER=${VER:-0.23.6.1}          # between 0.23.6 and 0.23.7: local test build
BOX2D_SHA=d9b573238d334c86b8a36b2a5584a6a741f1363a   # deps/box2d submodule pin
JOBS=$(nproc)

log(){ printf '\n=== %s ===\n' "$*"; }

log "apt build dependencies"
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y \
  build-essential cmake pkg-config cmake-data git curl jq wget ca-certificates \
  zlib1g-dev \
  libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev \
  libwayland-dev libxkbcommon-dev \
  libglew-dev libglm-dev \
  libopenal-dev libfreetype-dev \
  libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev

log "Tcl 9 (deps/tcl) -> /usr/local"
if [ -f /usr/local/lib/libtcl9.0.so ]; then
  echo "Tcl 9 already in /usr/local -- skipping"
else
  ( cd "$SRC/deps/tcl/unix" \
    && { test -f Makefile && make distclean || true; } \
    && ./configure \
    && make -j"$JOBS" \
    && make install )
fi

log "GLFW static (wayland+x11) -> /usr/local"
( cd "$SRC/deps/glfw" \
  && cmake -B build-full-static -D GLFW_BUILD_WAYLAND=ON -D GLFW_BUILD_X11=ON \
  && cmake --build build-full-static --parallel "$JOBS" \
  && cmake --install build-full-static )

log "spine-c static -> /usr/local"
( cd "$SRC/deps/spine-runtimes/spine-c" \
  && cmake -B build -D CMAKE_POSITION_INDEPENDENT_CODE=ON \
  && cmake --build build --parallel "$JOBS" \
  && cmake --install build --prefix ./build \
  && mkdir -p /usr/local/include/spine \
  && cp build/dist/include/*.h /usr/local/include/spine/ \
  && cp build/dist/lib/*.a /usr/local/lib/ )

log "box2d v3 static -> /usr/local"
if [ ! -f "$SRC/deps/box2d/CMakeLists.txt" ]; then
  rm -rf "$SRC/deps/box2d"
  git clone https://github.com/erincatto/box2d.git "$SRC/deps/box2d"
  git -C "$SRC/deps/box2d" checkout "$BOX2D_SHA"
fi
( cd "$SRC/deps/box2d" \
  && cmake -B build -D BOX2D_UNIT_TESTS=OFF -D BOX2D_SAMPLES=OFF \
           -D BOX2D_BENCHMARKS=OFF -D BUILD_SHARED_LIBS=OFF \
  && cmake --build build --parallel "$JOBS" \
  && cmake --install build )

log "dlsh dg/dlsh dev debs"
DLSH_VERSION=$(curl -s https://api.github.com/repos/SheinbergLab/dlsh/releases/latest | jq -r .tag_name)
( cd /tmp \
  && wget -q "https://github.com/SheinbergLab/dlsh/releases/download/${DLSH_VERSION}/dlsh-dg_${DLSH_VERSION}_arm64.deb" \
  && wget -q "https://github.com/SheinbergLab/dlsh/releases/download/${DLSH_VERSION}/dlsh-dlsh_${DLSH_VERSION}_arm64.deb" \
  && dpkg -i "dlsh-dg_${DLSH_VERSION}_arm64.deb" "dlsh-dlsh_${DLSH_VERSION}_arm64.deb" )

log "stimdlls"
( cd "$SRC/stimdlls" \
  && cmake -D PROJECT_VERSION="$VER" -B build \
  && cmake --build build --parallel "$JOBS" )

log "stim2 + deb package"
( cd "$SRC" \
  && cmake -D PROJECT_VERSION="$VER" -D DISTRO_SUFFIX=trixie -B build \
  && cmake --build build --parallel "$JOBS" \
  && cpack -G DEB --config build/CPackConfig.cmake )

log "install deb"
DEBIAN_FRONTEND=noninteractive apt-get install -y --allow-downgrades \
  "$SRC"/stim2_"$VER"_arm64_trixie.deb

chown -R "$(stat -c %U:%G "$SRC")" "$SRC" 2>/dev/null || true

log "DONE"
echo "Test with:"
echo "  sudo XDG_RUNTIME_DIR=/run WAYLAND_DISPLAY=wayland-0 /usr/local/stim2/stim2 -F -f /usr/local/stim2/config/linux.cfg"
