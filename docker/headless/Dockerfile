# Headless stim2 on Debian trixie: Tcl 9 and a null-platform-only GLFW from
# stim2's own submodules, spine-c and box2d at stim2's pinned commits, dlsh
# from its release .debs (stimdlls link libdlsh; dlsh-interp brings dlsh.zip,
# which stim2 loads, and the dlsh interpreter for replay.tcl), stim2 +
# stimdlls from the context. Starts like a Linux rig (linux.cfg: stimdlls,
# ~/.config/stim2, screen_config). See docs/headless.md.
#
# The context is a directory holding stim2/ (with its deps/tcl and deps/glfw
# submodules) and dlsh/ side by side, as the stimdlls build expects:
#   docker build -f stim2/docker/headless/Dockerfile -t stim2-headless .
# Run with the GPU's render node:
#   docker run --rm --device /dev/dri/renderD128 -p 5610-5613:5610-5613 stim2-headless
FROM debian:trixie
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake pkg-config git ca-certificates curl wget jq \
      zlib1g-dev libssl-dev libgl-dev libegl-dev libgles-dev libglm-dev libglew-dev \
      libopenal-dev libfreetype-dev libpng-dev libtinyxml2-dev \
      libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev \
      libegl1 libegl-mesa0 libgl1-mesa-dri mesa-utils-bin ffmpeg \
      va-driver-all vainfo procps \
    && rm -rf /var/lib/apt/lists/*

# a shallow fetch of one pinned commit
RUN printf '#!/bin/sh\nset -e\nmkdir -p "$3"; cd "$3"; git init -q; git remote add origin "$1"\ngit fetch -q --depth 1 origin "$2"; git checkout -q FETCH_HEAD\n' \
      > /usr/local/bin/fetch-commit && chmod +x /usr/local/bin/fetch-commit

COPY stim2/deps/tcl /src/stim2/deps/tcl
RUN cd /src/stim2/deps/tcl/unix && ./configure --prefix=/usr/local >/dev/null \
    && make -j"$(nproc)" >/dev/null && make install >/dev/null

COPY stim2/deps/glfw /src/stim2/deps/glfw
RUN cd /src/stim2/deps/glfw && cmake -B b -DGLFW_BUILD_X11=OFF -DGLFW_BUILD_WAYLAND=OFF \
      -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF \
    && cmake --build b -j"$(nproc)" && cmake --install b

RUN fetch-commit https://github.com/EsotericSoftware/spine-runtimes.git \
      d4a3444328a72ce9b7a4622dbaef83bd9d682253 /src/spine \
    && cd /src/spine/spine-c && cmake -B build -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    && cmake --build build -j"$(nproc)" && cmake --install build --prefix ./build \
    && mkdir -p /usr/local/include/spine && cp build/dist/include/*.h /usr/local/include/spine/ \
    && cp build/dist/lib/*.a /usr/local/lib
RUN fetch-commit https://github.com/erincatto/box2d.git \
      d9b573238d334c86b8a36b2a5584a6a741f1363a /src/box2d \
    && cd /src/box2d && cmake -B build -DBOX2D_UNIT_TESTS=OFF -DBOX2D_SAMPLES=OFF \
      -DBOX2D_BENCHMARKS=OFF -DBUILD_SHARED_LIBS=OFF \
    && cmake --build build -j"$(nproc)" && cmake --install build

ARG DLSH_VERSION=latest
RUN v="$DLSH_VERSION"; if [ "$v" = latest ]; then \
      v=$(curl -s https://api.github.com/repos/SheinbergLab/dlsh/releases/latest | jq -r .tag_name); fi; \
    echo "dlsh $v"; cd /tmp \
    && wget -q https://github.com/SheinbergLab/dlsh/releases/download/$v/dlsh-dg_${v}_amd64.deb \
    && wget -q https://github.com/SheinbergLab/dlsh/releases/download/$v/dlsh-dlsh_${v}_amd64.deb \
    && wget -q https://github.com/SheinbergLab/dlsh/releases/download/$v/dlsh-interp_${v}_amd64.deb \
    && apt-get update && apt-get install -y --no-install-recommends \
         ./dlsh-dg_${v}_amd64.deb ./dlsh-dlsh_${v}_amd64.deb ./dlsh-interp_${v}_amd64.deb \
    && rm -rf /tmp/*.deb /var/lib/apt/lists/* && ldconfig

COPY dlsh /src/dlsh
COPY stim2 /src/stim2
ARG STIM2_VERSION=0.0.0
RUN cd /src/stim2/stimdlls && cmake -DPROJECT_VERSION=$STIM2_VERSION -B build \
    && cmake --build build -j"$(nproc)"
RUN cd /src/stim2 && cmake -DPROJECT_VERSION=$STIM2_VERSION -B build \
    && cmake --build build -j"$(nproc)" && cmake --install build && ldconfig

EXPOSE 5610 5611 5612 5613
ENTRYPOINT ["/usr/local/stim2/stim2", "--headless", "--port-base", "5610"]
CMD ["-w", "1920", "-h", "1080", "-f", "/usr/local/stim2/config/linux.cfg"]
