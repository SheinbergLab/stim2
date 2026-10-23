#!/bin/sh
# Static FFmpeg for the wasm image: the five libraries video.c links.
# Programs, docs, network, devices, filters, and encoders stay out.
# Demuxers and decoders stay, so avcodec_find_decoder can open the
# codec that is actually in the file.
set -e
ROOT=$1
PREFIX=$2
VER=7.1.5
SRC=$ROOT/ffmpeg-$VER
TAR=$ROOT/ffmpeg-$VER.tar.xz
URL=https://ffmpeg.org/releases/ffmpeg-$VER.tar.xz

mkdir -p "$ROOT" "$PREFIX"
if [ ! -d "$SRC" ]; then
    if [ ! -f "$TAR" ]; then
        curl -fsSL -o "$TAR" "$URL"
    fi
    tar -C "$ROOT" -xf "$TAR"
fi
cd "$SRC"
if [ ! -f ffbuild/config.mak ]; then
    emconfigure ./configure \
        --prefix="$PREFIX" \
        --cc=emcc \
        --cxx=em++ \
        --ar=emar \
        --ranlib=emranlib \
        --nm=emnm \
        --host-cc=gcc \
        --enable-cross-compile \
        --target-os=none \
        --arch=x86_32 \
        --cpu=generic \
        --disable-shared \
        --enable-static \
        --disable-programs \
        --disable-doc \
        --disable-network \
        --disable-avdevice \
        --disable-avfilter \
        --disable-encoders \
        --disable-muxers \
        --disable-hwaccels \
        --disable-autodetect \
        --disable-asm \
        --disable-inline-asm \
        --disable-stripping \
        --disable-pthreads \
        --disable-w32threads \
        --disable-os2threads \
        --disable-debug \
        --disable-iconv \
        --disable-zlib \
        --disable-bzlib \
        --disable-lzma \
        --enable-avcodec \
        --enable-avformat \
        --enable-avutil \
        --enable-swscale \
        --enable-swresample \
        --enable-decoders \
        --enable-demuxers \
        --enable-parsers \
        --enable-bsfs \
        --enable-protocol=file \
        --extra-cflags="-O2"
fi
emmake make -j4
emmake make install
