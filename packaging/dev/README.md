# Dev builds for a Linux rig

You can try a stim2 change on a Debian 13 x86_64 rig before tagging a
release. These steps build it on a Mac (or any Docker host) in the image
the release workflow uses.

Build the image once, and again whenever the deps change:

    docker build --platform linux/amd64 -f packaging/dev/Dockerfile.trixie \
        --build-arg STIM2_REF=$(git rev-parse HEAD) -t stim2-dev:trixie \
        $(mktemp -d)

Each build compiles the working tree. The first takes a few minutes;
after that, about a minute:

    packaging/dev/build-linux.sh        # -> build-dev-trixie/stim2

The binary has the release's runpath (`/usr/local/stim2/lib`). To swap
it in on the rig and back again:

    sudo cp /usr/local/stim2/stim2 /usr/local/stim2/stim2.release   # once
    sudo cp ~/stim2-dev/stim2 /usr/local/stim2/stim2
    sudo systemctl restart stim2
    ...
    sudo cp /usr/local/stim2/stim2.release /usr/local/stim2/stim2
    sudo systemctl restart stim2

After a restart, reload the ESS system on the host so it reconnects and
loads its stim script again. The next release installed on the rig
replaces the dev binary.

Only the `stim2` binary changes; the stimdlls and Tcl library in
`/usr/local/stim2` stay the release's, so a change to those needs a
release.
