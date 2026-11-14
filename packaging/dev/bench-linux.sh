#!/bin/sh
# Time rendering on the Linux dev build, in the stim2-dev:trixie image:
# headless, on Mesa's software renderer (llvmpipe).  See bench.tcl for what
# is timed.  The number that matters is the one on the machine that will
# render: on a Mac the image runs x86 code under emulation, so these are a
# rough floor, not what an x86 host gives.
#
#   packaging/dev/build-linux.sh             # first: the build to time
#   packaging/dev/bench-linux.sh             # 1920x1080
#   packaging/dev/bench-linux.sh 1280 720
#   BENCH_N=200 packaging/dev/bench-linux.sh
#   DOCKER_ARGS="--device /dev/dri/renderD128" packaging/dev/bench-linux.sh   # a Linux host's GPU
#   DOCKER_ARGS="--cpus 4 -e LP_NUM_THREADS=4" packaging/dev/bench-linux.sh   # a share of the CPUs
set -e
here=$(cd "$(dirname "$0")/../.." && pwd)
out=${OUT:-$here/build-dev-trixie}
w=${1:-1920}; h=${2:-1080}
[ -x "$out/stim2" ] && [ -d "$out/stimdlls" ] || {
  echo "no dev build in $out: run packaging/dev/build-linux.sh first" >&2; exit 2; }
docker run --rm --platform linux/amd64 $DOCKER_ARGS \
  -e BENCH_N="${BENCH_N:-60}" -e BW="$w" -e BH="$h" \
  -v "$here":/src:ro -v "$out":/out:ro \
  stim2-dev:trixie sh -c '
    mkdir -p /usr/local/stim2
    cp -r /out/. /usr/local/stim2/
    cp -r /src/config /usr/local/stim2/config
    export LD_LIBRARY_PATH=/usr/local/lib
    printf "source /usr/local/stim2/config/linux.cfg\nif {[catch {source /src/packaging/dev/bench.tcl} e]} {puts \"BENCH ERROR: \$e\"; exit 3}\n" > /tmp/run.tcl
    echo "host: $(nproc) cpus, $(uname -m),$(grep -m1 "model name" /proc/cpuinfo | cut -d: -f2)"
    cd /tmp && timeout 900 /usr/local/stim2/stim2 --headless -w $BW -h $BH --port-base 5610 -f /tmp/run.tcl 2>&1 \
      | grep "BENCH\|renderer:" | sed "s/^\[info\] //; s/^.*renderer: /renderer: /"'
