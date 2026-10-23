#!/bin/sh
# Static libx264 for the trial encoder. No CLI, no assembler, no threads.
# This is a separate module from the stim image. stim2 is MIT; x264 is GPL,
# so the stim wasm does not link this library.
set -e
ROOT=$1
PREFIX=$2
REV=b35605ace3ddf7c1a5d67a2eb553f034aef41d55
SRC=$ROOT/x264-$REV
TAR=$ROOT/x264-$REV.tar.gz
URL=https://code.videolan.org/videolan/x264/-/archive/$REV/x264-$REV.tar.gz

mkdir -p "$ROOT" "$PREFIX"
if [ ! -d "$SRC" ]; then
    if [ ! -f "$TAR" ]; then
        curl -fsSL -o "$TAR" "$URL"
    fi
    tar -C "$ROOT" -xf "$TAR"
fi
cd "$SRC"
if [ ! -f config.mak ]; then
    emconfigure ./configure \
        --prefix="$PREFIX" \
        --host=i686-gnu \
        --disable-cli \
        --enable-static \
        --disable-shared \
        --disable-opencl \
        --disable-asm \
        --disable-thread \
        --disable-avs \
        --disable-swscale \
        --disable-lavf \
        --disable-ffms \
        --disable-gpac \
        --disable-lsmash \
        --extra-cflags="-O2"
fi
emmake make -j"$(nproc)"
emmake make install
cp "$SRC/COPYING" "$PREFIX/COPYING"
printf '%s\n' "$REV" > "$PREFIX/VERSION"
