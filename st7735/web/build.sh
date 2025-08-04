#!/bin/bash
# Builds the main.cpp application as a wasm module that is loaded by lcdsim.html.
# The module has a basic WASI interface to provide stdio to js console logging,
# and can be easily extended if needed.
# The appication is designed to use this LCD library via a framebuffer backend that
# interfaces with the lcdsim web environment to renders the LCD framebuffer. It can
# also run arbitrary other code, provided that it can be built inside a webassembly
# module and doesn't have external dependencies that can't be satisfied
if [ -z $(which emcc) ]; then
    echo "The Emscripten compiler 'emcc' not found in your path. You need to setup your shell environment with"
    echo "source ./emsdk_env.sh"
    exit 1
fi
emcc ./main.cpp $@ -std=c++20 -O3 -sSTANDALONE_WASM=1  -Wl,--export=__wasm_call_ctors -Wl,--export=main -I .. -o ./main.wasm
