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
