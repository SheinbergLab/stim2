# Dev builds for a Linux rig

You can try a stim2 change on a Debian 13 x86_64 rig before tagging a
release. These steps build it on a Mac (or any Docker host) in the image
the release workflow uses.

Build the image once, and again whenever the deps change:

    docker build --platform linux/amd64 -f packaging/dev/Dockerfile.trixie \
        --build-arg STIM2_REF=$(git rev-parse HEAD) -t stim2-dev:trixie \
        $(mktemp -d)

Each build compiles the working tree. The first takes several minutes;
after that, a minute or two:

    packaging/dev/build-linux.sh

It writes `build-dev-trixie/`, laid out as `/usr/local/stim2` is:

    stim2        the binary (the release's runpath, /usr/local/stim2/lib)
    stimdlls/    the loadable modules (*.so)
    modules/     the Tcl modules (stimess, ...)
    shaders/     the shader files, passes/ included
    fonts/       the fonts the text module loads

`BINARY_ONLY=1 packaging/dev/build-linux.sh` builds just the binary.

**The binary and the stimdlls go together.** A module needs the functions
its stim2 exports, and a stim2 has only the commands of the modules beside
it: a new binary over the release's stimdlls has no `gazewin` or
`shaderpass` command, and a new `gazewin.so` under the release's binary
does not load. Copy the binary alone only for a change that touches
nothing a module uses.

To swap the dev build in on the rig, and back again:

    scp -r build-dev-trixie rig:stim2-dev

    # once: keep the release's
    sudo cp -a /usr/local/stim2 /usr/local/stim2.release

    sudo cp ~/stim2-dev/stim2 /usr/local/stim2/stim2
    sudo cp ~/stim2-dev/stimdlls/*.so /usr/local/stim2/stimdlls/
    sudo cp -r ~/stim2-dev/modules/. /usr/local/stim2/modules/
    sudo cp -r ~/stim2-dev/shaders/. /usr/local/stim2/shaders/
    sudo cp -r ~/stim2-dev/fonts/. /usr/local/stim2/fonts/
    sudo systemctl restart stim2
    ...
    sudo rm -rf /usr/local/stim2 && sudo mv /usr/local/stim2.release /usr/local/stim2
    sudo systemctl restart stim2

After a restart, reload the ESS system on the host so it reconnects and
loads its stim script again. The next release installed on the rig
replaces all of it.

Not built here: the tools (`tools/`) and the examples, which stay the
release's.

## Running the tests in the image

The image can run stim2 headless on Mesa's software renderer (llvmpipe),
with `dlsh.zip`, the stimdlls, the Tcl modules, the shaders and the fonts
where a rig has them. That checks a Linux build without a rig:

    packaging/dev/build-linux.sh        # the build under test
    packaging/dev/test-linux.sh         # every test it knows
    packaging/dev/test-linux.sh test_gazewin test_shaderpass

It prints a PASS or FAIL line per test, with the failing checks, and
exits non-zero if any test fails, errors or times out. It runs:

- inside stim2, after `config/linux.cfg` as a rig's service starts it:
  `test_frame_grab`, `test_headless_step`, `test_dynlist_numeric`,
  `test_gazewin`, `test_gazewin_step`, `test_shaderpass`,
  `test_shader_fragment_only`;
- as `tclsh` clients of a stim2 it starts: `test_gazewin_client`,
  `test_ack_header`;
- in plain `tclsh`: `test_stimess`.

Not run: `test_late_draw` and `test_frame_log`, which need flip
timestamps or a fake dserv the script does not set up.

What it does not tell you: anything about a GPU driver, vsync, flip
timing or latency. Those need the rig. llvmpipe is also slow, so frame
rates there mean nothing.

The image gets `dlsh.zip` from dlsh's released `dlsh-interp` package. An
image built before that was added has none, and the script says so:
build the image again.

## Timing a render

How fast the build renders without a GPU, for sizing a machine that will
render trials (the `stim2-render` service, a replay):

    packaging/dev/bench-linux.sh             # 1920x1080
    packaging/dev/bench-linux.sh 1280 720
    BENCH_N=200 packaging/dev/bench-linux.sh

It runs `bench.tcl`: stepped frames into a frame sink, as a replay render
makes them, for a planko-like board, then with a gaze window (masked;
graded blur), a noise pass, and both. One line per condition: ms per
frame, frames per second. The same script runs in any stim2:

    stim2 --headless -w 1920 -h 1080 -f /abs/path/packaging/dev/bench.tcl

What was measured on 2026-11-14, at 1920x1080, in frames per second:

| | llvmpipe, Ryzen 9 9950X3D (16 cores, 32 threads) | llvmpipe, emulated on an Apple M3 (8 CPUs) | the M3's GPU |
|---|---|---|---|
| scene + frame sink | 937 | 408 | 360 |
| + gaze window, soft, masked | 430 | 108 | 300 |
| + gaze window, graded blur | 150 | 16 | 238 |
| + noise pass | 447 | 156 | 282 |
| + graded blur + noise | 126 | 15 | 202 |

On a Mac the image runs x86 code under emulation: its column is a floor
that says little about an x86 host, which is the first column. There, the
software renderer keeps up with a 120 Hz display in every condition.

Several containers at once, on the same Ryzen (each with `--cpus T` and
`LP_NUM_THREADS=T`; total frames per second over all of them; one run
each, so read differences under 10% as noise):

| containers x threads | scene + sink | soft gaze | graded blur | blur + noise |
|---|---|---|---|---|
| 1 x 32 | 937 | 434 | 148 | 127 |
| 1 x 8 | 1116 | 329 | 82 | 72 |
| 1 x 4 | 928 | 215 | 48 | 43 |
| 1 x 1 | 446 | 78 | 13 | 12 |
| 4 x 8 | 1255 | 578 | 201 | 176 |
| 8 x 4 | 1667 | 562 | 206 | 186 |
| 16 x 2 | 636 | 421 | 213 | 190 |
| 32 x 1 | 621 | 411 | 204 | 189 |

A container costs nothing measurable (one container with every thread
gives what the same stim2 gives alone). Splitting the machine among 4 to 8
containers renders 1.3 to 1.8 times as many frames in total as one
container with every thread, and each used about 170 MB at its peak. Past
8, a plain scene gets slower in total: those frames are mostly memory
traffic (the readback and the sink), which the containers compete for.

**A GPU.** On a Linux host with one, pass its render device into the
container and stim2 draws on it (no other change: Mesa's surfaceless EGL
finds the device, and the `renderer:` line says which was used):

    DOCKER_ARGS="--device /dev/dri/renderD128" packaging/dev/test-linux.sh
    DOCKER_ARGS="--device /dev/dri/renderD128" packaging/dev/bench-linux.sh

Measured the same day on that Ryzen's integrated Radeon (radeonsi), where
all ten tests pass as they do on llvmpipe. Total frames per second:

| containers, 2 CPUs each | scene + sink | soft gaze | graded blur | blur + noise |
|---|---|---|---|---|
| 1 | 636 | 416 | 206 | 166 |
| 2 | 925 | 675 | 309 | 254 |
| 4 | 927 | 565 | 304 | 262 |
| 8 | 737 | 528 | 300 | 253 |

A small integrated GPU is about what the 32 CPU threads are together, a
little ahead on the blur and behind on a plain scene (the frame comes back
from the GPU to be written). Two containers saturate it, and one CPU each
is enough. Its use is that it leaves the CPUs free. An NVIDIA card, which
needs the vendor's container runtime instead of the render device, was
not tried.

It times stim2 alone. Not in it: encoding the video (frames go to
`/dev/null`), and what a real stim script does each frame (physics, video,
images).
