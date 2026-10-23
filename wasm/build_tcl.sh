#!/bin/sh
# Build the Tcl pinned in deps/tcl as a static WebAssembly library.
# emconfigure runs the configure tests under node. The bundled zlib is
# what this build uses, and its header is not on the generic compile
# line, so CFLAGS gains that include after configure.
set -e
SRC=$1
BUILD=$2
PREFIX=$3
mkdir -p "$BUILD"
cd "$BUILD"
if [ ! -f Makefile ]; then
    emconfigure "$SRC/unix/configure" \
        --disable-shared --disable-load --disable-zipfs \
        --prefix="$PREFIX"
fi
if ! grep -q 'CFLAGS = $(ZLIB_INCLUDE)' Makefile; then
    sed -i 's/^CFLAGS[[:space:]]*=/CFLAGS = $(ZLIB_INCLUDE)/' Makefile
fi
# tclsh links pthread_kill, which this Emscripten does not provide.
# The static library is what the wasm module needs.
emmake make -j4 libtcl9.1.a
emmake make install-libraries install-headers
mkdir -p "$PREFIX/lib"
cp -f libtcl9.1.a "$PREFIX/lib/libtcl9.1.a"
